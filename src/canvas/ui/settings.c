/*
        The settings, as a window on the compositor in userspace.

        `settings` is a front end to the moonwater command and to nothing
        else: every value it shows is what a verb says when it is asked
        (moonwater name, moonwater keyboard, ...), read from the verb's
        output as a person would read it, and every change is the verb run
        with the new value. What the command keeps is what the window shows
        the next time it asks, so the two never disagree, and a value the
        command refuses is refused in the window in the command's words.

        The rows are not written here: they are the table of settings in
        src/sh/registry.c, in its order, and a setting added there is a row
        of this window. They are the switches and choices of the command that
        a person sets by hand: the machine's name, the keyboard and how its
        keys repeat, the time zone, how Canvas is drawn (its scale and screen
        modes, the desktop's colour, the size and look of the pointer and
        whether a shake of the hand makes it large, and for how long), the
        mouse's sensitivity and acceleration, the clock and where the network sets it from, the
        wired link, the radios and airplane mode, which link the internet
        prefers, the servers names and time are asked of, the power profile,
        the backlight, the processor and the battery, and what the machine
        starts at boot. A row the machine has nothing for (no backlight, no
        boost switch) says so and does nothing. Nothing in those rows ends
        the desktop, the session or the machine.

        The desktop's colour is two rows of the one verb (moonwater canvas
        background): a choice of named inks, whose first is the default,
        each one an rrggbb that the verb is run with and that is shown by
        its name when the verb says it, and a field that takes any rrggbb
        (or default) and keeps it on Enter, the verb's own words being the
        status when it refuses what was typed.

        The last section, All commands, is every row of what `moonwater
        help` says, read from it when the window opens (at most 96 rows, and
        the window says when there were more), so a verb the rows above do
        not cover is still one the window runs; and its first row runs any
        moonwater command at all. Enter on one opens a field for the words
        that follow its verb, and Enter in it runs the command as words, with
        the command's own words first: blanks part them and a pair of double
        or single quotes holds blanks in one, and nothing else is read, never
        a shell. What it said is the status line when it is a line, and else
        a card with the lines, which the keys and the wheel scroll and
        Escape or a press closes. A command runs until it ends or Escape
        stops it. A command that ends the desktop, the session or the
        machine, or throws away what a person keeps (wipe, setup install,
        update and use, sleep, hibernate, bios reboot, desktop start and stop,
        desktop off, link remove and group leave, wifi and bluetooth remove,
        cpu offline, bind init and exit remove), whichever row it is typed
        in, is not run by the first Enter: the window says so and runs it
        when the same Enter is pressed again, and any other key asks again.

        It is a page of the kit (ui.c), and says only what it shows: the
        groups, one for each section of rows and All commands last, are the
        page's column at the left, and the rows of the group chosen are the
        kit's list at the right, each row its label, its value and the kind
        of value it is (settings_describe), in sections with their headers.
        Everything else -- the sizes, the type, the theme's colours, the row
        the keys are on, the hover, the press and its ripple, the menu a
        choice opens, the field a row is typed in, every scrollbar, the wheel
        and a finger's drag and throw -- is the kit's. The keys are in the
        column or in the rows, whichever the lit one is: the window opens
        with them in the rows, on the first.

        The keys. Tab and Shift-Tab go between the column and the rows.
        Up, Down, Home, End, Page Up and Page Down move in whichever has the
        keys, and moving in the column puts the rows of that group at the
        right at once. Enter or Right in the column go to its rows, on the
        first. In the rows, Enter or Space changes a row (a switch is
        thrown, a choice opens its list, a field is typed in and Enter
        keeps it, Escape leaves it as it was); Enter or Space throws a switch
        over, Left and Right move a step either way, and on a switch or any
        other row Left goes back to the column and Right does nothing.
        Escape closes the window, as Alt-F4 and its close button do. Any
        other key typed with no field open is a filter: the rows that
        mention what is typed are the ones listed, from every group, with
        the keys on the first of them; Backspace takes a letter off it, and
        Escape clears it before it closes the window. Choosing another group
        clears it too. With the mouse, a press on a group chooses it, a
        press on a row is Enter on it, and the wheel scrolls what is under
        the pointer.

        A choice opens a list of all its choices, under its row (over it
        where there is more room there, and slid up where it fits the window
        no other way; a list taller than the window scrolls), the one it
        has now dotted and the keys on it. While the list is open it takes
        every key: Up, Down, Home, End, Page Up and Page Down move the keys
        in it, a letter or a digit goes to the next choice that starts with
        it, Enter or Space sets the choice the keys are on (the verb is run
        as for any row) and closes it, and Escape closes it with the row as
        it was. A press on a choice sets it, a press anywhere else only
        closes the list, the pointer over a choice puts the keys on it, and
        the wheel scrolls it. A choice the machine has none of opens none.

        What changes under a person's eyes is kept up with. While a group is
        shown, the verb it declares (a feed, settings_feeds) is run again
        every second or two, and never while the window is put away, while
        another group is shown, while what is typed filters every group,
        while a card or a field fills the window, or while any other verb
        is running or waiting (a change a person makes drops a reading under
        way, which is had again). Wi-Fi lists the networks in range under
        its switch (marked joined or saved, with what each asks for and the
        bars of its signal) and the saved ones not heard, from moonwater
        wifi, whose own first line is also the state of the switch; Enter
        joins one that is open, asks for the passphrase of one that is not
        (shown as dots, given to the verb on its input and never in its
        words, and kept nowhere here), and Delete twice forgets a saved
        one. Bluetooth lists the devices kept, from moonwater bluetooth,
        with a last row that keeps another by its name and Delete twice to
        forget one: the command has no scan, no pairing and no connecting,
        so the window has none either. Power asks the battery again and Time
        the clock. The keys stay on the row of the name they were on when
        the list moves under them, and a reading that says what the last
        said draws nothing.

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

// The file that includes this one includes battery.c first: the Battery row's value is one of its batteries.

// What a row's value is: not asked yet, said, not on this machine, asked and not found in what was said.
#define SETTINGS_UNREAD 0u
#define SETTINGS_READ 1u
#define SETTINGS_ABSENT 2u
#define SETTINGS_FAILED 3u

#define SETTINGS_VALUE 128   // a value read
#define SETTINGS_TYPED 256   // what is typed in a field: a value, or the words of a command
#define SETTINGS_SAID 16384  // what a verb says: the whole of moonwater help is five thousand
#define SETTINGS_STATUS 160
#define SETTINGS_WORDS 24
#define SETTINGS_WAIT_NS 20000000000ll
#define SETTINGS_FILTER 24
#define SETTINGS_GENERATED 96 // rows of All commands, at most
#define SETTINGS_POOL 8192    // the bytes of their words
#define SETTINGS_CARD 12288   // the bytes of what a command said, shown as a card
#define SETTINGS_CARD_LINES 256
#define SETTINGS_PROBE 512    // one line, cut
#define SETTINGS_NONE 0xffffu

// The size the window opens at, logical pixels; every other size in it is the kit's (ui.c, the page).
#define SETTINGS_WIDTH 700
#define SETTINGS_HEIGHT 520
#define SETTINGS_QUEUE 8 // changes that may wait for the verb running
#define SETTINGS_NAME_ROW 0 // the machine's name, the first row: a name is letters, digits and hyphens, under 32
#define SETTINGS_NAME_MOST 31

#define SETTINGS_KEY_ESCAPE 1
#define SETTINGS_KEY_BACKSPACE 14
#define SETTINGS_KEY_TAB 15
#define SETTINGS_KEY_ENTER 28
#define SETTINGS_KEY_SPACE 57
#define SETTINGS_KEY_HOME 102
#define SETTINGS_KEY_UP 103
#define SETTINGS_KEY_DELETE 111
#define SETTINGS_KEY_PAGE_UP 104
#define SETTINGS_KEY_LEFT 105
#define SETTINGS_KEY_RIGHT 106
#define SETTINGS_KEY_END 107
#define SETTINGS_KEY_DOWN 108
#define SETTINGS_KEY_PAGE_DOWN 109
#define SETTINGS_KEY_ENTER_PAD 96
#define SETTINGS_BUTTON_LEFT 0x110

#define SETTINGS_JOB_HELP -2 // the job that asks moonwater help, which is no row's

struct settings_change
{
        int row;
        char value[SETTINGS_TYPED];
};

// The rows are the registry's (src/sh/registry.c): every setting there is a row of this window, in its order.
#define SETTINGS_ANY ((int)SETTINGS_ROWS) // the row of All commands that runs any command
#define SETTINGS_ALL_HEADER "All commands"

/*
        A feed is a verb run again while the group it belongs to is shown, so
        that what changes under a person's eyes is what the window shows. It
        is not run while the window is put away, while another group is
        shown, while a field is typed in or a card is up, while what is typed
        filters the rows of every group, or while another verb runs or waits.
        A list feed turns what the verb says into the rows of a list under
        the group's own rows (the networks in range, the devices kept), and
        the verb's own first line is also what the switch of the group reads
        as; a rows feed asks again the rows of the group that say they change
        by themselves (the clock, the battery).
*/
#define SETTINGS_FEED_ROWS 1u
#define SETTINGS_FEED_WIFI 2u
#define SETTINGS_FEED_DEVICES 3u
#define SETTINGS_LISTS 2 // the list feeds, which come first in the table
#define SETTINGS_LIST_ROWS 24
#define SETTINGS_ITEM_LABEL 96
#define SETTINGS_FEED_WAIT_NS 8000000000ll
#define SETTINGS_ACT_WAIT_NS 60000000000ll // a join waits for its handshake

struct settings_feed
{
        const char address_to group;    // the group whose rows it keeps up with
        const char address_to owner;    // the row whose value the verb's own words also say, or none
        const char address_to read[3];  // what is asked, after moonwater
        const char address_to heading;  // what the list is headed
        unsigned short every;           // milliseconds from one reading's end to the next
        unsigned char kind;
};

static const struct settings_feed settings_feeds[] = {
    {"Wi-Fi", "Wi-Fi", {"wifi"}, "In range and saved", 2000, SETTINGS_FEED_WIFI},
    {"Bluetooth", "Bluetooth", {"bluetooth"}, "Remembered devices", 3000, SETTINGS_FEED_DEVICES},
    {"Power", null, {null}, null, 2000, SETTINGS_FEED_ROWS},
    {"Time", null, {null}, null, 1000, SETTINGS_FEED_ROWS},
};

#define SETTINGS_FEEDS (sizeof(settings_feeds) / sizeof(settings_feeds[0]))
// The rows of the lists come after the commands': row SETTINGS_LIST + list * SETTINGS_LIST_ROWS + n.
#define SETTINGS_LIST (SETTINGS_ANY + 1 + SETTINGS_GENERATED)
#define SETTINGS_ALL ((positive)SETTINGS_LIST + SETTINGS_LISTS * SETTINGS_LIST_ROWS)

// Jobs that are no row's: the help, a feed's reading, and what is done to a list's row.
#define SETTINGS_JOB_FEED -3
#define SETTINGS_JOB_ACT -4

// A network or a device, as a row of a list: what the verb said of it.
#define SETTINGS_ITEM_JOINED 1u
#define SETTINGS_ITEM_SAVED 2u
#define SETTINGS_ITEM_HEARD 4u
#define SETTINGS_ITEM_OPEN 8u    // asks for no passphrase
#define SETTINGS_ITEM_CANNOT 16u // moonwater cannot join what it asks for

struct settings_item
{
        char label[SETTINGS_ITEM_LABEL];
        char security[12];
        signed short dbm;
        unsigned char bars, flags, kind;
};

/*
        The named colours of a row that chooses an ink, as the rrggbb the verb
        takes and says (with its # in front). A colour the verb says that is
        none of them is shown as the verb said it.
*/
struct settings_ink
{
        const char address_to name;
        const char address_to colour;
};

static const struct settings_ink settings_inks[] = {
    {"default", "1e1e1e"}, {"black", "000000"},  {"dark-grey", "3a3a3c"},  {"slate", "334155"}, {"midnight-blue", "101935"},
    {"forest", "163a26"},  {"plum", "3b1f4a"},   {"light-grey", "d4d4d8"}, {"white", "ffffff"},
};

// The accents (canvas theme accent), named as the background's inks are: white first, the standard, then the kit's amber.
static const struct settings_ink settings_accents[] = {
    {"white", "ffffff"}, {"amber", "ff8800"}, {"blue", "2563eb"}, {"green", "16a34a"}, {"violet", "7c3aed"}, {"rose", "e11d48"}, {"graphite", "52525b"},
};

// The named inks of a row of them: the accent's, or the background's.
static const struct settings_ink address_to settings_ink_table(const struct settings_row address_to row, positive address_to count)
{
        if (row->path && string_equals((string_address)row->path, (string_address)SETTING_ACCENT_PATH))
        {
                *count = sizeof(settings_accents) / sizeof(settings_accents[0]);
                return settings_accents;
        }
        *count = sizeof(settings_inks) / sizeof(settings_inks[0]);
        return settings_inks;
}

_Static_assert(sizeof(settings_rows) / sizeof(settings_rows[0]) < 64, "the rows to ask are a bit each of a word");

/*
        What changes the machine or ends what a person has, as the words that
        would be run (after moonwater): a pattern is the first words of a
        command, so setup install matches whatever follows it. Such a
        command is run by a second Enter and not the first.
*/
static const char address_to const settings_asks[][3] = {
    {"wipe"},
    {"setup", "install"},
    {"setup", "update"},
    {"setup", "use"},
    {"sleep"},
    {"hibernate"},
    {"bios", "reboot"},
    {"desktop", "start"},
    {"desktop", "stop"},
    {"desktop", "off"},
    {"link", "remove"},
    {"link", "group", "leave"},
    {"wifi", "remove"},
    {"bluetooth", "remove"},
    {"cpu", "offline"},
    {"bind", "init", "remove"},
    {"bind", "exit", "remove"},
};

/* ---- the model: what a verb said, and what to run ---- */

/*
        One line of what a verb said, as words: escapes taken out and the
        control characters, a tab a blank, and the blanks that indent it
        left. Answers the line's length, and where the next line starts.
*/
static positive settings_keep(const char address_to said, positive length, positive address_to at, char address_to into,
                              positive room)
{
        positive made = 0;

        while (address_to at < length && said[address_to at] != '\n')
        {
                char c = said[address_to at];

                address_to at += 1;
                if (c == '\t')
                        c = ' ';
                // An escape sequence: its introducer, its parameters and its final letter. The end of a line is never taken into one.
                if (c == 0x1b)
                {
                        if (address_to at < length && said[address_to at] == '[')
                        {
                                address_to at += 1;
                                while (address_to at < length && said[address_to at] != '\n' &&
                                       (said[address_to at] < 0x40 || said[address_to at] > 0x7e))
                                        address_to at += 1;
                        }
                        address_to at += address_to at < length && said[address_to at] != '\n';
                        continue;
                }
                if ((unsigned char)c < ' ' || c == 0x7f || made + 1 >= room)
                        continue;
                into[made++] = c;
        }
        address_to at += address_to at < length;
        into[made] = 0;
        return made;
}

// The "[Moonwater] " a verb starts its lines with: how many bytes it is at the start of a line, or none.
static positive settings_label_length(const char address_to line, positive length)
{
        static const char label[] = "[Moonwater] ";

        return length >= sizeof(label) - 1 && !memory_compare(line, label, sizeof(label) - 1) ? sizeof(label) - 1 : 0;
}

/*
        One line of what a verb said, as words: escapes taken out, the
        "[Moonwater] " a verb starts its lines with and the blanks that
        indent them taken off. Answers the line's length, and where the
        next line starts.
*/
static positive settings_line(const char address_to said, positive length, positive address_to at, char address_to into,
                              positive room)
{
        positive made = settings_keep(said, length, at, into, room);
        positive skip = settings_label_length(into, made);

        while (skip < made && into[skip] == ' ')
                skip++;
        // To the left, the rest and its terminator.
        if (skip)
                memory_copy(into, into + skip, made - skip + 1);
        return made - skip;
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
               settings_find(line, length, "this battery has no") < length ||
               settings_find(line, length, "no wireless hardware") < length;
}

// The part of a line that a machine said of itself: what follows `; "` is a name some device chose (usb says each device's product
// there, escaped so that no quote but the opening one is bare), and a device that calls itself "this machine has no" must not take
// every row of its verb away.
static positive settings_unquoted(const char address_to line, positive length)
{
        for (positive at = 0; at + 2 < length; at++)
                if (line[at] == ';' && line[at + 1] == ' ' && line[at + 2] == '"')
                        return at;
        return length;
}

// Whether a line says a row has none, for this row. "this battery has no charge limit" is the charge limit's none only: a line that
// also says this row's own key (the battery's percent) is a value of this row and does not take it away.
static b32 settings_lacks(const char address_to line, positive length, const struct settings_row address_to row)
{
        if (settings_find(line, length, "this battery has no") < length && settings_find(line, length, "this machine has no") >= length &&
            settings_find(line, length, "no wireless hardware") >= length && row->key && row->key[0] &&
            settings_find(line, length, row->key) < length)
                return false;
        return settings_absent(line, length);
}

// Whether a value goes on past the character c: a word ends at a blank, a comma, a semicolon or a bracket, a line only at its stop.
static b32 settings_goes_on(const struct settings_row address_to row, char c)
{
        if (row->stop && c == row->stop)
                return false;
        return (row->flags & SETTINGS_LINE) || (c != ' ' && c != ',' && c != ';' && c != '(');
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
                absent |= settings_lacks(line, settings_unquoted(line, size), row);

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
                        while (stop < size && settings_goes_on(row, line[stop]))
                                stop++;
                        while (stop > start && line[stop - 1] == ' ')
                                stop--;
                        if (stop == start)
                                continue;
                        stop = stop - start >= room ? start + room - 1 : stop;
                        memory_copy(value, line + start, stop - start);
                        value[stop - start] = 0;
                        if (row->marker && settings_find(line, size, row->marker) < size)
                                string_copy_bounded((string_address)value, (string_address) "auto", room);
                        found = 1;
                }
        }

        // A switch a machine may have none of says so as a word that is neither (smt notsupported).
        if (found && row->kind == SETTINGS_SWITCH && row->lacks && !string_equals((string_address)value, (string_address) "on") &&
            !string_equals((string_address)value, (string_address) "off"))
                absent = true;
        return absent ? SETTINGS_ABSENT : found ? SETTINGS_READ : row->lacks ? SETTINGS_ABSENT : SETTINGS_FAILED;
}

/*
        The Battery row is the battery the Battery picker row chose, as
        battery.c reads it: its percent and whether it charges. A machine with
        none, or with none present, has none. The verb's words are not used
        for it, so a battery without a charge limit is still a battery.
*/
static unsigned int settings_battery(string_address chosen, char address_to value, positive room)
{
        struct battery_state battery;
        p8 digits[16];

        value[0] = 0;
        // none is no battery. A chosen name is read as it is; only a name that has gone (no longer a battery) falls back to the first present one.
        if (chosen && string_equals(chosen, (string_address) "none"))
                return SETTINGS_ABSENT;
        if (!chosen || !chosen[0] || string_equals(chosen, (string_address) "auto"))
                battery_read(null, &battery);
        else if (!battery_read(chosen, &battery))
                battery_read(null, &battery);
        if (!battery.present)
                return SETTINGS_ABSENT;
        if (battery.percent < 0)
                string_copy_bounded((string_address)value, (string_address) "unknown", room);
        else
        {
                positive_into_string(digits, (positive)battery.percent);
                string_copy_bounded((string_address)value, (string_address)digits, room);
                string_append_bounded((string_address)value, (string_address) "%", room);
        }
        if (battery.charging == BATTERY_CHARGING)
                string_append_bounded((string_address)value, (string_address) ", Charging", room);
        else if (battery.charging == BATTERY_DISCHARGING)
                string_append_bounded((string_address)value, (string_address) ", Discharging", room);
        else if (battery.charging == BATTERY_FULL)
                string_append_bounded((string_address)value, (string_address) ", Full", room);
        else if (battery.charging == BATTERY_NOT_CHARGING)
                string_append_bounded((string_address)value, (string_address) ", Not charging", room);
        return SETTINGS_READ;
}

/*
        What a row reads as from its verb's output and how the verb ended. A
        verb that said nothing and failed did not run (no moonwater where it
        was asked for, or it died before it said anything): that is not a
        machine that has none of the thing, so a row it lacks is unknown then.
*/
static const char address_to settings_battery_choice(void);

static unsigned int settings_read(const struct settings_row address_to row, const char address_to said, positive length,
                                  char address_to value, positive room, b32 failed)
{
        unsigned int state;

        if (row->label && string_equals((string_address)row->label, (string_address) "Battery"))
                return settings_battery((string_address)settings_battery_choice(), value, room);
        state = settings_parse(row, said, length, value, room);

        if (state == SETTINGS_READ && (row->flags & SETTINGS_INK) && value[0] == '#')
        {
                positive inks;
                const struct settings_ink address_to table = settings_ink_table(row, &inks);

                for (positive at = 0; at < inks; at++)
                        if (string_equals((string_address)value + 1, (string_address)table[at].colour))
                        {
                                string_copy_bounded((string_address)value, (string_address)table[at].name, room);
                                break;
                        }
        }
        return state == SETTINGS_ABSENT && failed && !length ? SETTINGS_FAILED : state;
}

// The word of a list (words a blank apart) at index, into into; false past its end.
static b32 settings_word(const char address_to words, positive index, char address_to into, positive room)
{
        while (words && *words)
        {
                positive size = (positive)(string_first_of_or_end((string_address)words, ' ') - (string_address)words);

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
        What a switch or a step goes to from value, one way (1) or the other
        (-1): a switch to the other of on and off (2 throws it on and -2
        off, whatever it said), a step to its word for that way. A choice is
        not moved this way: it opens a list of its words, and a word of it is
        what it goes to.
*/
static fn settings_next(const struct settings_row address_to row, const char address_to value, int way, char address_to into,
                        positive room)
{
        into[0] = 0;
        if (row->kind == SETTINGS_SWITCH)
        {
                // Right throws it on and Left off, whatever it said; an Enter or a press turns it over.
                const char address_to to = way > 1 ? "on" : way < -1 ? "off"
                                           : string_equals((string_address)value, (string_address) "on") ? "off" : "on";

                string_copy_bounded((string_address)into, (string_address)to, room);
                return;
        }
        if (row->kind == SETTINGS_STEP)
        {
                settings_word(row->choices, way > 0 ? 1 : 0, into, room);
                return;
        }
}

/*
        text as words, after those words already in words (count of them):
        blanks part them, and a pair of double or single quotes holds blanks
        in one word and is not part of it; nothing else is read. The words
        are copied into buffer, and words ends with a null. Answers the new
        count, or none when there is not room for them.
*/
static positive settings_tokens(const char address_to text, char address_to buffer, positive size,
                                string_address address_to words, positive count, positive room)
{
        positive used = 0;

        while (*text)
        {
                char quote = 0;

                while (*text == ' ')
                        text++;
                if (!*text)
                        break;
                if (*text == '"' || *text == '\'')
                        quote = *text++;
                if (count + 1 >= room)
                        return 0;
                words[count++] = (string_address)buffer + used;
                while (*text && (quote ? *text != quote : *text != ' '))
                {
                        if (used + 1 >= size)
                                return 0;
                        buffer[used++] = *text++;
                }
                text += quote && *text == quote;
                if (used + 1 >= size)
                        return 0;
                buffer[used++] = 0;
        }
        words[count] = null;
        return count;
}

/* ---- the program ---- */

// A command of All commands, read from what moonwater help said: where its words are in the pool.
struct settings_generic
{
        unsigned short command, text; // the command as help prints it, and what it does
        unsigned char verb;           // the bytes of the command that are its verb, which are not typed
};

static struct
{
        struct window_row own;
        struct ui ui;
        string_address moonwater;
        char value[SETTINGS_ROWS][SETTINGS_VALUE];
        unsigned char state[SETTINGS_ROWS];

        // What moonwater help said, as the rows of All commands: texts in a pool, a row after the first (which runs any command).
        struct settings_generic generic[SETTINGS_GENERATED];
        positive generic_count;
        positive pool_used;
        unsigned int last_text; // where the text of the row last added is in the pool, or none
        b32 truncated;          // help said more than the rows and the pool hold
        int help;               // 0 not asked, 1 to be asked, 2 asked

        // The lists' rows read last (their counts), when the shown group's feed is next due, and what is done to a row:
        // the name a Delete asked to forget, and the name a passphrase is being typed for.
        unsigned short item_count[SETTINGS_LISTS];
        positive feed_due;
        int feed_list; // the feed whose verb is being run
        char forget[SETTINGS_ITEM_LABEL], joining[SETTINGS_ITEM_LABEL];

        /*
                The window is the kit's page (ui.c): the column of groups at the left, the list of the rows of the
                group chosen at the right (its places are the rows that show, in order, and its focus the row the
                keys are on), the menu a choice opens, a finger on any of them, and the card's lines.
        */
        struct ui_page page;
        struct ui_list list;
        struct ui_menu menu;
        struct ui_finger finger;
        struct ui_scroll card;
        int editing;             // the row typed in, -1 none
        int group;               // the group whose rows are at the right
        b32 pane;                // where the keys are: 0 the column of groups, 1 the rows
        b32 touching;            // a finger's press is being done: what it opens is a finger's
        char head[SETTINGS_VALUE]; // the verb of a command being typed, as its row's label
        char accents[SETTINGS_VALUE]; // the accents' colours, a blank apart, as the swatches' words
        struct battery_name battery_menu[BATTERY_LIST]; // the batteries a picker list was opened with
        positive battery_menu_count;
        char filter[SETTINGS_FILTER];
        positive filter_length;
        // The filter as typed, in a field of the text engine (copied, folded to lower case, into filter as it changes),
        // which is the page's search field.
        struct ui_text filter_edit;
        b32 filter_open;
        char typed[SETTINGS_TYPED];
        char status[SETTINGS_STATUS];
        int width, height, shown;
        unsigned int ground;
        b32 focused, dirty;

        // What a command said, as a card in place of the list: lines cut to the window's width.
        b32 view;
        positive card_length;
        int card_lines;
        char card_title[SETTINGS_STATUS];

        // The command that asked for a second Enter: its row (one more than it, none is 0) and what was typed.
        int confirm;
        char confirm_args[SETTINGS_TYPED];

        // The verb being run: its process, the read end of its output, what it said, when it began, for which row.
        bipolar child;
        b32 output;
        positive said_length, began;
        int job;
        b32 job_sets;
        char job_value[SETTINGS_TYPED];
        positive unread;  // rows to ask, a bit each
        struct settings_change queue[SETTINGS_QUEUE]; // changes to make once the verb running is done, in order
        positive queued;
        b32 told;         // the first reading is said in the machine's log

        // The picture: the rows changed since the last one was made (empty when bottom <= top), or all of it; and for each
        // buffer, the rows it lacks since it was last drawn, or all of it. The two buffers alternate, so each lacks what
        // changed since the frame before last.
        int change_top, change_bottom;
        b32 change_all;
        int stale_top[2], stale_bottom[2];
        b32 stale_all[2];
        // What the wrapped lines were made for: the card, the width and the scale.
        unsigned int card_generation;
        b32 wrap_made;
        int wrap_width, wrap_scale;
        unsigned int wrap_card;
} settings;

_Static_assert(SETTINGS_ALL <= UI_LIST_MOST, "every row the window shows is a place of the kit's list");

/*
        What is kept in a mapping made when the window opens and not in the
        image (the shell image links this file, and a tool under a 12 MiB
        address cap has no room for forty kilobytes it never uses): what a
        verb said, the words of help, the card, and the words of a command
        being made.
*/
static struct settings_large
{
        char said[SETTINGS_SAID];
        char pool[SETTINGS_POOL];
        char card[SETTINGS_CARD];
        unsigned short card_at[SETTINGS_CARD_LINES], card_size[SETTINGS_CARD_LINES];
        char split[SETTINGS_TYPED * 2];
        struct settings_item item[SETTINGS_LISTS][SETTINGS_LIST_ROWS];
} address_to settings_large;

static b32 settings_map(void)
{
        long mapped;

        if (settings_large)
                return true;
        mapped = ui_map((sizeof(struct settings_large) + 4095) & ~(positive)4095);
        if (mapped < 0 && mapped > -4096)
                return false;
        settings_large = (struct settings_large address_to)mapped;
        return true;
}

// Rows [from, to) added to the rows [top, bottom) (empty when bottom <= top): the union, the empty one none.
static void settings_rows_add(int address_to top, int address_to bottom, int from, int to)
{
        if (to <= from)
                return;
        if (*bottom <= *top)
        {
                *top = from;
                *bottom = to;
                return;
        }
        *top = from < *top ? from : *top;
        *bottom = to > *bottom ? to : *bottom;
}

// The picture may have changed anywhere: the next one is drawn whole.
static fn settings_dirty(void)
{
        settings.dirty = true;
        settings.change_all = true;
}

// Rows [top, bottom) of the picture may have changed, and no others: the next one draws them.
static fn settings_changed(int top, int bottom)
{
        settings.dirty = true;
        settings_rows_add(&settings.change_top, &settings.change_bottom, top, bottom);
}

/* ---- the rows ---- */

static positive settings_rows_count(void)
{
        return (positive)SETTINGS_ANY + 1 + settings.generic_count;
}

static const struct settings_generic address_to settings_generic_of(int row)
{
        return row > SETTINGS_ANY && row < (int)SETTINGS_LIST ? &settings.generic[row - SETTINGS_ANY - 1] : null;
}

// Which list a row is in (an index of the feeds), or -1 when it is in none.
static int settings_list_of(int row)
{
        return row >= (int)SETTINGS_LIST ? (row - (int)SETTINGS_LIST) / SETTINGS_LIST_ROWS : -1;
}

// The network or the device a row is, or none: a row of a list that was read and is still there.
static struct settings_item address_to settings_item(int row)
{
        int list = settings_list_of(row), at = row - (int)SETTINGS_LIST - list * SETTINGS_LIST_ROWS;

        return list >= 0 && list < SETTINGS_LISTS && at < settings.item_count[list] ? &settings_large->item[list][at] : null;
}

static unsigned char settings_kind(int row)
{
        const struct settings_item address_to item = settings_item(row);

        if (item)
                return item->kind;
        return row < SETTINGS_ANY ? settings_rows[row].kind : (unsigned char)SETTINGS_COMMAND;
}

static const char address_to settings_label(int row)
{
        const struct settings_generic address_to generic = settings_generic_of(row);
        const struct settings_item address_to item = settings_item(row);

        if (item)
                return item->label;
        if (row < SETTINGS_ANY)
                return settings_rows[row].label;
        return generic ? settings_large->pool + generic->command : "Run any moonwater command";
}

// What a row of a list does when it is pressed, said where the status is.
static const char address_to settings_item_text(const struct settings_item address_to item)
{
        if (item->kind == SETTINGS_NETWORK)
                return item->flags & SETTINGS_ITEM_JOINED   ? "joined: Delete forgets it"
                       : item->flags & SETTINGS_ITEM_CANNOT ? "moonwater cannot join what this network asks for"
                       : item->flags & SETTINGS_ITEM_OPEN   ? "Enter joins it, and saves it"
                       : item->flags & SETTINGS_ITEM_SAVED  ? "saved: Enter types its passphrase again, Delete forgets it"
                                                            : "Enter asks for its passphrase and joins it";
        return item->kind == SETTINGS_DEVICE ? "kept by its name: Delete forgets it"
               : item->kind == SETTINGS_ADD  ? "Enter keeps the name of a device; the radio goes on for what pairs" : "";
}

// What a command of All commands does, as help says; the any row's own words.
static const char address_to settings_text(int row)
{
        const struct settings_generic address_to generic = settings_generic_of(row);
        const struct settings_item address_to item = settings_item(row);

        if (item)
                return settings_item_text(item);
        return generic ? settings_large->pool + generic->text : "any command, as the words after moonwater: its verb, then its arguments";
}

// Whether a row's command takes words after its verb (the any row all of them).
static b32 settings_takes(int row)
{
        const struct settings_generic address_to generic = settings_generic_of(row);

        return !generic || settings_large->pool[generic->command + generic->verb] != 0;
}

// The first row of the section a row is in: a list is a section of its own.
static int settings_section_start(int row)
{
        if (row >= (int)SETTINGS_LIST)
                return (int)SETTINGS_LIST + settings_list_of(row) * SETTINGS_LIST_ROWS;
        if (row >= SETTINGS_ANY)
                return SETTINGS_ANY;
        while (row > 0 && !settings_rows[row].section)
                row--;
        return row;
}

static const char address_to settings_section(int row)
{
        if (row >= (int)SETTINGS_LIST)
                return settings_feeds[settings_list_of(row)].heading;
        return row >= SETTINGS_ANY ? SETTINGS_ALL_HEADER : settings_rows[settings_section_start(row)].section;
}

/*
        The groups: one for each header the rows start a section under, and
        All commands last. A group is what the column at the left lists and
        what the rows at the right are of.
*/
static int settings_groups(void)
{
        int count = 1;

        for (positive at = 0; at < SETTINGS_ROWS; at++)
                count += settings_rows[at].section != null;
        return count;
}

static const char address_to settings_group_name(int group)
{
        int seen = 0;

        for (positive at = 0; at < SETTINGS_ROWS; at++)
                if (settings_rows[at].section && seen++ == group)
                        return settings_rows[at].section;
        return SETTINGS_ALL_HEADER;
}

// The group a row is in; a list is in the group its feed keeps up with.
static int settings_group_of(int row)
{
        int group = -1;

        if (row >= (int)SETTINGS_LIST)
        {
                for (int at = 0; at < settings_groups(); at++)
                        if (string_equals((string_address)settings_group_name(at),
                                          (string_address)settings_feeds[settings_list_of(row)].group))
                                return at;
                return 0;
        }
        if (row >= SETTINGS_ANY)
                return settings_groups() - 1;
        for (int at = 0; at <= row; at++)
                group += settings_rows[at].section != null;
        return group;
}

// Where a row is in the rows that show, or -1.
static int settings_pos(int row)
{
        return ui_list_at(&settings.list, row);
}

// Whether text has what is typed in the filter, whatever the case.
static b32 settings_has(const char address_to text, const char address_to wanted, positive size)
{
        positive length = text ? string_length((string_address)text) : 0;

        return length >= size && (memory_search_ascii_case)((address_any)text, length, (address_any)wanted, size) != null;
}

static b32 settings_matches(int row)
{
        positive size = settings.filter_length;

        if (!size)
                return true;
        if (row >= (int)SETTINGS_LIST)
                return settings_has(settings_label(row), settings.filter, size) || settings_has(settings_section(row), settings.filter, size);
        if (row < SETTINGS_ANY)
        {
                const struct settings_row address_to which = &settings_rows[row];

                for (positive at = 0; at < SETTING_WORDS; at++)
                        if (settings_has(which->read[at], settings.filter, size) || settings_has(which->set[at], settings.filter, size))
                                return true;
                return settings_has(which->label, settings.filter, size) || settings_has(settings_section(row), settings.filter, size);
        }
        return settings_has(settings_label(row), settings.filter, size) || settings_has(settings_text(row), settings.filter, size) ||
               (row == SETTINGS_ANY && settings_has("anything", settings.filter, size));
}

static fn settings_describe(int id, struct ui_row address_to row);

// The page laid out for the window's size: the kit's bands, its column and the list in the body.
static fn settings_layout(void)
{
        struct ui address_to ui = &settings.ui;

        // What the list shows is the app's: its rows, described by settings_describe, and where a change is drawn again.
        if (!settings.list.ids)
        {
                settings.list.ids = (int)SETTINGS_ALL;
                settings.list.describe = settings_describe;
                settings.list.damaged = settings_changed;
                settings.list.empty = "Nothing matches what is typed";
                settings.list.hover = settings.list.down = -1;
                for (positive k = 0; k < sizeof(settings_accents) / sizeof(settings_accents[0]); k++)
                {
                        if (k)
                                string_append_bounded((string_address)settings.accents, (string_address) " ", sizeof(settings.accents));
                        string_append_bounded((string_address)settings.accents, (string_address)settings_accents[k].colour, sizeof(settings.accents));
                }
        }
        settings.page.column_on = true;
        settings.page.column.count = settings_groups() < UI_ITEMS ? settings_groups() : UI_ITEMS;
        ui_page_layout(ui, &settings.page, settings.width, settings.height);
        ui_list_place(ui, &settings.list, settings.page.left, settings.page.top, settings.page.right - settings.page.left,
                      settings.page.bottom - settings.page.top);
        ui_list_layout(ui, &settings.list);
        // The card has the body's band and the whole width: the column is not shown with it.
        settings.card.x = settings.page.right + (ui_px(ui, UI_PAD) - ui_px(ui, UI_SCROLL_WIDTH)) / 2;
        settings.card.y = settings.page.top, settings.card.height = settings.card.view = settings.page.bottom - settings.page.top;
        settings.card.content = ui_lines_height(ui, settings.card_lines);
        ui_scroll_set(&settings.card, settings.card.scroll);
}

// The list or the card scrolled, and drawn again.
static fn settings_scroll_to(int scroll)
{
        if (ui_scroll_set(settings.view ? &settings.card : &settings.list.scroll, scroll))
                settings_changed(settings.page.top, settings.page.bottom);
}

// The row with the focus kept in sight.
static fn settings_reveal(void)
{
        if (!settings.view)
                ui_list_reveal(&settings.ui, &settings.list);
}

// The chosen group kept in sight in a column that does not fit.
static fn settings_side_reveal(void)
{
        if (ui_page_column_reveal(&settings.ui, &settings.page, settings.group))
                settings_changed(settings.page.top, settings.page.bottom);
}

/*
        The rows that match the filter, laid out; the focus stays on its row
        when that still shows, and else goes to the first. The view stays
        where it is, which is what a list read again must not move.
*/
static fn settings_rebuild(void)
{
        int count = 0;

        // What is typed lists the rows that mention it, in every group; with none typed, the rows of the group chosen.
        for (positive row = 0; row < settings_rows_count(); row++)
                if (settings.filter_length ? settings_matches((int)row) : settings_group_of((int)row) == settings.group)
                        settings.list.id[count++] = (unsigned short)row;
        // The rows of a list come after, each in the group of its feed.
        for (int list = 0; list < SETTINGS_LISTS; list++)
                for (int at = 0; at < settings.item_count[list]; at++)
                {
                        int row = (int)SETTINGS_LIST + list * SETTINGS_LIST_ROWS + at;

                        if (settings.filter_length ? settings_matches(row) : settings_group_of(row) == settings.group)
                                settings.list.id[count++] = (unsigned short)row;
                }
        settings.list.count = count;
        if (settings.menu.owner >= 0 && settings_pos(settings.menu.owner) < 0)
                settings.menu.owner = -1;
        if (settings_pos(settings.list.focus) < 0 && count)
                settings.list.focus = settings.list.id[0];
        settings_layout();
        settings_dirty();
}

// The same, and the row with the focus brought into view.
static fn settings_refilter(void)
{
        settings_rebuild();
        settings_reveal();
}

/* ---- what help says ---- */

// The bytes of a command help prints that are its verb: the leading words of lower case letters. A word with a bracket,
// a bar or a capital in it is the first that is typed.
static positive settings_verb_length(const char address_to command)
{
        positive at = 0, last = 0;

        for (;;)
        {
                positive word = 0;

                while (command[at + word] >= 'a' && command[at + word] <= 'z')
                        word++;
                if (!word || (command[at + word] && command[at + word] != ' '))
                        break;
                last = at + word;
                at = last + (command[last] == ' ');
                if (!command[last])
                        break;
        }
        return last;
}

// Text into the pool, with its terminator: where it is, or none when the pool has not the room.
static unsigned int settings_pool_put(const char address_to text, positive size)
{
        unsigned int at;

        if (settings.pool_used + size + 1 > sizeof(settings_large->pool))
                return SETTINGS_NONE;
        at = (unsigned int)settings.pool_used;
        memory_copy(settings_large->pool + at, text, size);
        settings_large->pool[at + size] = 0;
        settings.pool_used += size + 1;
        return at;
}

/*
        A row of help's page: its command and what it says. "sleep | hibernate"
        is two commands that say one thing, and so two rows.
*/
static fn settings_generic_add(const char address_to command, positive size, const char address_to text, positive text_size)
{
        const char address_to bar = size >= 3 ? (const char address_to)(memory_search)((address_any)command, size, (address_any)" | ", 3) : null;
        unsigned int first, second = SETTINGS_NONE, said;
        positive rows = bar ? 2 : 1;

        if (settings.generic_count + rows > SETTINGS_GENERATED || settings.pool_used + size + text_size + 8 > sizeof(settings_large->pool) ||
            size > 120)
        {
                settings.truncated = true;
                settings.last_text = SETTINGS_NONE;
                return;
        }
        if (bar)
        {
                first = settings_pool_put(command, (positive)(bar - command));
                second = settings_pool_put(bar + 3, size - (positive)(bar - command) - 3);
        }
        else
                first = settings_pool_put(command, size);
        said = settings_pool_put(text, text_size);
        for (positive each = 0; each < rows; each++)
        {
                struct settings_generic address_to into = &settings.generic[settings.generic_count++];

                into->command = (unsigned short)(each ? second : first);
                into->text = (unsigned short)said;
                into->verb = (unsigned char)settings_verb_length(settings_large->pool + into->command);
        }
        settings.last_text = said;
}

// More of what the row last added says, from the line under it.
static fn settings_generic_more(const char address_to text, positive size)
{
        positive at;

        if (settings.last_text == SETTINGS_NONE || settings.pool_used + size + 2 > sizeof(settings_large->pool))
                return;
        // The text is the last thing in the pool: its terminator is taken back and the words follow it.
        at = settings.pool_used - 1;
        if (settings_large->pool[settings.last_text])
                settings_large->pool[at++] = ' ';
        memory_copy(settings_large->pool + at, text, size);
        settings_large->pool[at + size] = 0;
        settings.pool_used = at + size + 1;
}

/*
        The rows of moonwater help's main page as rows of All commands. A row
        is two blanks, its command, two blanks or more and what it does; a
        command too long for that has what it does on the line under it, and
        a line indented far under a row is more of what that row does. The
        page ends at its first blank line, before the notes.
*/
static fn settings_help(const char address_to said, positive length)
{
        char line[SETTINGS_PROBE];
        positive at = 0;

        settings.generic_count = 0;
        settings.pool_used = 0;
        settings.truncated = false;
        settings.last_text = SETTINGS_NONE;

        while (at < length)
        {
                positive size = settings_keep(said, length, &at, line, sizeof(line)), indent = 0;

                while (indent < size && line[indent] == ' ')
                        indent++;
                while (size > indent && line[size - 1] == ' ')
                        size--;
                if (size == indent)
                {
                        if (settings.generic_count)
                                break;
                        continue;
                }
                if (indent == 2)
                {
                        const char address_to apart = (const char address_to)(memory_search)((address_any)(line + 2), size - 2, (address_any)"  ", 2);
                        positive command = apart ? (positive)(apart - (line + 2)) : size - 2, text = command;

                        while (apart && 2 + text < size && line[2 + text] == ' ')
                                text++;
                        settings_generic_add(line + 2, command, line + 2 + text, size - 2 - text);
                }
                else if (indent >= 8)
                        settings_generic_more(line + indent, size - indent);
        }
}

/* ---- what to run ---- */

/*
        The words that change a row: the command, the row's verb and the
        value (none for an action), into words with a null after them. A
        value that is a list is its words. Never a shell's: a name typed is
        one word, whatever is in it. Answers how many, none when they do
        not fit.
*/
static positive settings_command(const struct settings_row address_to row, string_address command, const char address_to value,
                                 string_address address_to words, positive room, b32 asking)
{
        positive count = 0;
        const char address_to const address_to verb = asking ? row->read : row->set;

        if (room < SETTING_WORDS + 3)
                return 0;
        words[count++] = command;
        for (positive at = 0; at < SETTING_WORDS && verb[at]; at++)
                words[count++] = (string_address)verb[at];
        if (!asking && row->kind != SETTINGS_ACTION)
        {
                if (row->flags & SETTINGS_SPLIT)
                        return settings_tokens(value, settings_large->split, sizeof(settings_large->split), words, count, room);
                words[count++] = (string_address)value;
                // An ink is chosen by its name and set by its rrggbb.
                if (row->flags & SETTINGS_INK)
                {
                        positive inks;
                        const struct settings_ink address_to table = settings_ink_table(row, &inks);

                        for (positive at = 0; at < inks; at++)
                                if (string_equals((string_address)value, (string_address)table[at].name))
                                        words[count - 1] = (string_address)table[at].colour;
                }
        }
        words[count] = null;
        return count;
}

/*
        The words that run a row of All commands with args: the command, the
        row's verb and then args as words. The any row has the verb in args.
        Answers how many, none when they do not fit.
*/
static positive settings_generic_words(int row, const char address_to args, string_address address_to words, positive room)
{
        char joined[SETTINGS_VALUE + SETTINGS_TYPED + 2] = {0};
        const struct settings_generic address_to generic = settings_generic_of(row);
        positive used = 0;

        if (room < 4)
                return 0;
        if (generic)
        {
                used = generic->verb;
                memory_copy(joined, settings_large->pool + generic->command, used);
                joined[used++] = ' ';
        }
        string_copy_bounded((string_address)joined + used, (string_address)args, sizeof(joined) - used);
        words[0] = settings.moonwater;
        return settings_tokens(joined, settings_large->split, sizeof(settings_large->split), words, 1, room);
}

// Whether words (what follows moonwater) are a command that ends what a person has or has going, and so asks twice.
static b32 settings_asks_for(string_address address_to words, positive count)
{
        for (positive each = 0; each < sizeof(settings_asks) / sizeof(settings_asks[0]); each++)
        {
                positive at = 0;

                while (at < 3 && settings_asks[each][at] && at < count &&
                       string_equals(words[at], (string_address)settings_asks[each][at]))
                        at++;
                if (at == 3 || !settings_asks[each][at])
                        return true;
        }
        return false;
}

// Whether a row of All commands asks twice whatever is typed after its verb.
static b32 settings_row_asks(int row)
{
        const struct settings_generic address_to generic = settings_generic_of(row);
        char head[SETTINGS_VALUE];
        string_address words[SETTINGS_WORDS];
        positive count = 0, size;

        if (!generic)
                return false;
        size = generic->verb;
        memory_copy(head, settings_large->pool + generic->command, size);
        head[size] = 0;
        count = settings_tokens(head, settings_large->split, sizeof(settings_large->split), words, 0, SETTINGS_WORDS);
        return count && settings_asks_for(words, count);
}

/* ---- what a command said, as a card ---- */

/*
        What the verb said, kept in the card: its lines with the escapes and
        the control characters taken out and the blanks at their ends, the
        "[Moonwater] " a verb starts its lines with taken off, and its
        indentation kept. No blank line ends it.
*/
static fn settings_card_fill(void)
{
        char line[SETTINGS_PROBE];
        positive at = 0, made = 0;

        while (at < settings.said_length)
        {
                positive size = settings_keep(settings_large->said, settings.said_length, &at, line, sizeof(line));
                positive skip = settings_label_length(line, size);

                while (size > skip && line[size - 1] == ' ')
                        size--;
                if (made + (size - skip) + 1 >= sizeof(settings_large->card))
                        break;
                memory_copy(settings_large->card + made, line + skip, size - skip);
                made += size - skip;
                settings_large->card[made++] = '\n';
        }
        while (made && settings_large->card[made - 1] == '\n')
                made--;
        settings.card_length = made;
        settings.card_generation++;
}

// A run of the card, measured as it is drawn.
static int settings_card_width(positive from, positive to)
{
        char probe[SETTINGS_PROBE] = {0};
        positive size = to - from;

        size = size >= sizeof(probe) ? sizeof(probe) - 1 : size;
        memory_copy(probe, settings_large->card + from, size);
        probe[size] = 0;
        return ui_kit_measure(&settings.ui, (string_address)probe, UI_BODY_MEDIUM);
}

/*
        A run of the card laid on as it is measured: the pen after the bytes
        from its start to at, so a wider prefix costs its new bytes only.
        Only for a plain card (ASCII): one byte is one code point there.
*/
struct settings_run
{
        positive from, at;
        struct ui_pen pen;
};

static int settings_run_width(struct ui address_to ui, struct settings_run address_to run, positive from, positive to)
{
        const font_face address_to faces[2] = {ui->regular, ui->regular};
        p32 em = (p32)(ui_types[UI_BODY_MEDIUM].size * ui->scale) << 6;

        if (from != run->from || to < run->at)
        {
                run->from = from;
                run->at = from;
                ui_pen_start(faces, em, &run->pen);
        }
        while (run->at < to)
        {
                ui_pen_step(ui, faces, em, (unsigned char)settings_large->card[run->at], &run->pen);
                run->at++;
        }
        return ui_pen_width(&run->pen);
}

/*
        The card's lines cut to the window's width: each of what the verb
        said is as many as it takes, a break at a blank where it can be and
        else where the line has gone as far as it fits. Kept until the width,
        the scale or the card changes.
*/
static fn settings_wrap(void)
{
        int room = settings.width - 2 * ui_px(&settings.ui, UI_PAD) - 2 * ui_px(&settings.ui, UI_INSET);
        positive at = 0;

        if (settings.wrap_made && settings.wrap_width == settings.width && settings.wrap_scale == settings.ui.scale &&
            settings.wrap_card == settings.card_generation)
                return;
        settings.card_lines = 0;
        while (at <= settings.card_length && settings.card_lines < SETTINGS_CARD_LINES)
        {
                positive over = at, start = at;
                // No run is started yet: the first width laid out starts the pen, even for a line that begins at byte 0.
                struct settings_run run = {~(positive)0, 0, {0, 0, 0, 0}};
                b32 plain;

                while (over < settings.card_length && settings_large->card[over] != '\n')
                        over++;
                plain = over - at < SETTINGS_PROBE;
                for (positive byte = at; plain && byte < over; byte++)
                        plain = (unsigned char)settings_large->card[byte] < 0x80;
                do
                {
                        positive fit = start, next = start;

                        // As many whole words as fit.
                        while (next < over)
                        {
                                positive word = next;

                                while (word < over && settings_large->card[word] == ' ')
                                        word++;
                                while (word < over && settings_large->card[word] != ' ')
                                        word++;
                                if ((plain ? settings_run_width(&settings.ui, &run, start, word) : settings_card_width(start, word)) > room)
                                        break;
                                fit = next = word;
                        }
                        // One word wider than the window: as many of its bytes as fit.
                        if (fit == start && start < over)
                        {
                                fit = start + 1;
                                while (fit < over && (plain ? settings_run_width(&settings.ui, &run, start, fit + 1)
                                                            : settings_card_width(start, fit + 1)) <= room)
                                        fit++;
                        }
                        if (settings.card_lines < SETTINGS_CARD_LINES)
                        {
                                settings_large->card_at[settings.card_lines] = (unsigned short)start;
                                settings_large->card_size[settings.card_lines++] = (unsigned short)(fit - start);
                        }
                        start = fit;
                        while (start < over && settings_large->card[start] == ' ')
                                start++;
                } while (start < over && settings.card_lines < SETTINGS_CARD_LINES);
                at = over + 1;
        }
        settings.wrap_made = true;
        settings.wrap_width = settings.width;
        settings.wrap_scale = settings.ui.scale;
        settings.wrap_card = settings.card_generation;
}

// The card shown with the title of the command that made it.
static fn settings_card_open(const char address_to title)
{
        settings_card_fill();
        string_copy_bounded((string_address)settings.card_title, (string_address)title, sizeof(settings.card_title));
        settings.view = true;
        settings.card.scroll = 0;
        settings_wrap();
        settings_layout();
        settings_dirty();
}

static fn settings_card_leave(void)
{
        settings.view = false;
        settings.status[0] = 0;
        settings_dirty();
}

/* ---- the verbs ---- */

static fn settings_say(string_address text)
{
        string_copy_bounded((string_address)settings.status, text, sizeof(settings.status));
        settings_changed(settings.page.bottom, settings.height);
}

/*
        A verb run with its output to a pipe this end reads, and for its input
        what is in input (a password, which the verb takes from a line of its
        standard input and not from its words, where ps would show it) or
        nothing; false when it could not be.
*/
static b32 settings_start(string_address address_to words, positive count, const char address_to input)
{
        int ends[2] = {-1, -1}, feeds[2] = {-1, -1};
        positive length = input ? string_length((string_address)input) : 0;
        bipolar child;

        // Only this program's end does not block: the verb's standard output and error are the other end, and a
        // verb that writes more than the pipe holds would be told EAGAIN, not made to wait. F_SETFL is 4.
        if (system_call_2(syscall(pipe2), (positive)ends, O_CLOEXEC) != 0)
                return false;
        system_call_3(syscall(fcntl), (positive)ends[0], 4, O_NONBLOCK);
        // What it reads is written before it starts, whole, to a pipe that holds it, and closed: a line and then its end.
        if (length && (system_call_2(syscall(pipe2), (positive)feeds, O_CLOEXEC) != 0 ||
                       system_write_once((b32)feeds[1], input, length) != (bipolar)length ||
                       system_write_once((b32)feeds[1], "\n", 1) != 1))
        {
                system_close(ends[0]);
                system_close(ends[1]);
                if (feeds[0] >= 0)
                        system_close(feeds[0]);
                if (feeds[1] >= 0)
                        system_close(feeds[1]);
                return false;
        }
        if (feeds[1] >= 0)
                system_close(feeds[1]);

        child = system_fork();
        if (child == 0)
        {
                bipolar quiet = feeds[0] >= 0 ? feeds[0] : system_open_at(AT_FDCWD, (string_address) "/dev/null", FILE_READ | O_CLOEXEC);

                if (quiet >= 0)
                        system_call_3(syscall(dup3), (positive)quiet, 0, 0);
                system_call_3(syscall(dup3), (positive)ends[1], 1, 0);
                system_call_3(syscall(dup3), (positive)ends[1], 2, 0);
                // As its own: not reading from where this program writes.
                (void)shell_exec_file(words[0], words, count, program_environment_list());
                system_call_1(syscall(exit), 127);
        }
        system_close(ends[1]);
        if (feeds[0] >= 0)
                system_close(feeds[0]);
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

// The command's words as one line, after "moonwater", into text.
static fn settings_join(string_address address_to words, positive count, char address_to text, positive room)
{
        string_copy_bounded((string_address)text, (string_address) "moonwater", room);
        for (positive at = 1; at < count; at++)
        {
                string_append_bounded((string_address)text, (string_address) " ", room);
                string_append_bounded((string_address)text, words[at], room);
        }
}

/*
        The words of what is done to a row of a list, from the change queued
        (a letter, a newline, a name, and for a join a newline and the
        passphrase): into words, which point into job_value, cut at its
        newlines. Answers the passphrase, which is to be given to the verb's
        input, or none; words[0] is none when the change is no known one.
*/
static const char address_to settings_act_words(string_address address_to words)
{
        char address_to what = settings.job_value, address_to name, address_to pass = null;
        string_address radio = (string_address) "wifi", how = (string_address) "add";

        words[0] = null;
        if (!what[0] || what[1] != '\n' || !(what[0] == 'j' || what[0] == 'f' || what[0] == 'a' || what[0] == 'd'))
                return null;
        name = what + 2;
        for (char address_to at = name; *at; at++)
                if (*at == '\n')
                {
                        *at = 0;
                        pass = at + 1;
                        break;
                }
        if (!name[0])
                return null;
        if (what[0] == 'a' || what[0] == 'd')
                radio = (string_address) "bluetooth";
        if (what[0] == 'f' || what[0] == 'd')
                how = (string_address) "remove";
        pass = what[0] == 'j' && pass && pass[0] ? pass : null;
        words[0] = settings.moonwater;
        words[1] = radio;
        words[2] = how;
        words[3] = (string_address)name;
        // A password is read from a line of the input, which "-" says.
        words[4] = pass ? (string_address) "-" : null;
        words[5] = null;
        return pass;
}

// Whether two rows ask the same verb, and so are answered by one run of it.
static b32 settings_same_verb(const struct settings_row address_to one, const struct settings_row address_to other)
{
        for (positive at = 0; at < SETTING_WORDS; at++)
                if (one->read[at] != other->read[at] &&
                    (!one->read[at] || !other->read[at] || !string_equals((string_address)one->read[at], (string_address)other->read[at])))
                        return false;
        return true;
}

// The next verb to run: a change first, then the rows not yet asked, then help.
static fn settings_next_job(void)
{
        string_address words[SETTINGS_WORDS];
        positive count;

        while (settings.child <= 0)
        {
                if (settings.queued)
                {
                        settings.job = settings.queue[0].row;
                        settings.job_sets = true;
                        memory_copy(settings.job_value, settings.queue[0].value, sizeof(settings.job_value));
                        settings.queued--;
                        memory_copy(settings.queue, settings.queue + 1, settings.queued * sizeof(settings.queue[0]));
                        // Nothing a change carried (a passphrase) stays in the slot it leaves.
                        memory_zero(&settings.queue[settings.queued], sizeof(settings.queue[0]));
                }
                else if (settings.unread)
                {
                        settings.job = (int)bits_trailing_zeros(settings.unread);
                        settings.unread &= settings.unread - 1;
                        settings.job_sets = false;
                }
                else if (settings.help == 1)
                {
                        settings.job = SETTINGS_JOB_HELP;
                        settings.job_sets = false;
                        settings.help = 2;
                }
                else
                        return;

                if (settings.job == SETTINGS_JOB_HELP)
                {
                        words[0] = settings.moonwater;
                        words[1] = (string_address) "help";
                        words[2] = null;
                        count = 2;
                }
                else if (settings.job == SETTINGS_JOB_ACT)
                {
                        const char address_to pass = settings_act_words(words);

                        count = words[0] ? 4 + (positive)(pass != null) : 0;
                        if (count && settings_start(words, count, pass))
                        {
                                // The password went to the verb's input; none is left here.
                                memory_zero(settings.job_value, sizeof(settings.job_value));
                                return;
                        }
                        memory_zero(settings.job_value, sizeof(settings.job_value));
                        count = 0;
                }
                else if (settings.job >= SETTINGS_ANY)
                        count = settings_generic_words(settings.job, settings.job_value, words, SETTINGS_WORDS);
                else
                        count = settings_command(&settings_rows[settings.job], settings.moonwater, settings.job_value, words,
                                                 SETTINGS_WORDS, !settings.job_sets);
                if (count && settings_start(words, count, null))
                {
                        if (settings.job >= SETTINGS_ANY)
                        {
                                char text[SETTINGS_STATUS - 24];
                                char line[SETTINGS_STATUS];

                                settings_join(words, count, text, sizeof(text));
                                string_copy_bounded((string_address)line, (string_address) "running ", sizeof(line));
                                string_append_bounded((string_address)line, (string_address)text, sizeof(line));
                                string_append_bounded((string_address)line, (string_address) " (Escape stops it)", sizeof(line));
                                settings_say((string_address)line);
                        }
                        return;
                }

                if (settings.job_sets)
                        settings_say((string_address)(count || settings.job < SETTINGS_ANY
                                                          ? "the moonwater command could not be run"
                                                          : "that is more words than one command takes"));
                else if (settings.job >= 0)
                        settings.state[settings.job] = SETTINGS_FAILED;
                settings_dirty();
        }
}

// Every row asked again: a change to one can move another (airplane mode turns the radios off).
static fn settings_ask_all(void)
{
        settings.unread = ((positive)1 << SETTINGS_ROWS) - 1;
}

// Where a change to row waits in the queue, or queued when it has none.
static positive settings_slot(int row)
{
        positive at = 0;

        while (at < settings.queued && settings.queue[at].row != row)
                at++;
        return at;
}

static fn settings_feed_drop(void);

// A row changed to value, now or once the verb running has finished: a change to a row already waiting replaces it.
static fn settings_change(int row, const char address_to value)
{
        positive at;

        // A feed's reading is one that can be had again: a change waits for no feed.
        settings_feed_drop();
        at = settings_slot(row);

        if (at == settings.queued)
        {
                if (settings.queued == SETTINGS_QUEUE)
                {
                        settings_say((string_address) "moonwater is busy: the changes before this one are not done");
                        return;
                }
                settings.queue[at].row = row;
                settings.queued++;
        }
        string_copy_bounded((string_address)settings.queue[at].value, (string_address)value, sizeof(settings.queue[at].value));
        settings_say((string_address) "...");
        settings_next_job();
}

// What a command of All commands said, now it has ended: a line is the status, more is a card.
static fn settings_report(b32 killed, positive status)
{
        char first[SETTINGS_STATUS], other[SETTINGS_STATUS], title[SETTINGS_STATUS];
        string_address words[SETTINGS_WORDS];
        positive at = 0, lines = 0, count;

        while (at < settings.said_length)
                lines += settings_line(settings_large->said, settings.said_length, &at, lines ? other : first, SETTINGS_STATUS) != 0;
        if (!lines)
                settings_say((string_address)(killed ? "stopped" : wait_status_code(status) ? "moonwater refused" : "done"));
        else if (lines == 1)
                settings_say((string_address)first);
        else
        {
                count = settings_generic_words(settings.job, settings.job_value, words, SETTINGS_WORDS);
                settings_join(words, count, title, sizeof(title));
                settings_card_open(title);
                settings_say((string_address)(killed ? "stopped: Escape or a press goes back" : "Escape or a press goes back"));
        }
}

/* ---- feeds: what is read again while it is on show ---- */

// Where a (non-empty) key is in a line, the last time; the line's length when it is not there.
static positive settings_find_last(const char address_to line, positive length, const char address_to key)
{
        positive size = string_length((string_address)key), found = length;

        for (positive at = 0; at + size <= length; at++)
                if (!memory_compare(line + at, key, size))
                        found = at;
        return found;
}

static b32 settings_starts(const char address_to line, const char address_to prefix)
{
        positive size = string_length((string_address)prefix);

        return !memory_compare(line, prefix, size);
}

// A name into an item's label, cut to what it holds; a name a person did not make is shown as it was said.
static fn settings_item_label(struct settings_item address_to item, const char address_to name, positive size)
{
        size = size >= sizeof(item->label) ? sizeof(item->label) - 1 : size;
        memory_copy(item->label, name, size);
        item->label[size] = 0;
}

static struct settings_item address_to settings_item_find(int list, const char address_to name, positive size)
{
        for (int at = 0; at < settings.item_count[list]; at++)
        {
                struct settings_item address_to item = &settings_large->item[list][at];

                if (item->kind != SETTINGS_NOTE && string_length((string_address)item->label) == size &&
                    !memory_compare(item->label, name, size))
                        return item;
        }
        return null;
}

// Whether moonwater can join what a network asks for: open, WPA2, WPA3 and the two together, with protected management frames.
static b32 settings_joinable(const char address_to security)
{
        return string_equals((string_address)security, (string_address) "open") ||
               string_equals((string_address)security, (string_address) "WPA2") ||
               string_equals((string_address)security, (string_address) "WPA2/3") ||
               string_equals((string_address)security, (string_address) "WPA3") || string_equals((string_address)security, (string_address) "PMF");
}

/*
        One line of what a verb said, past its label: where it is in line
        (kept with its indentation, which says what kind of line it is), its
        length without the blanks that end it. False at the end of what was
        said.
*/
static b32 settings_feed_line(positive address_to at, char address_to line, positive room, char address_to address_to text,
                              positive address_to size)
{
        positive length, skip;

        if (address_to at >= settings.said_length)
                return false;
        length = settings_keep(settings_large->said, settings.said_length, at, line, room);
        skip = settings_label_length(line, length);
        while (length > skip && line[length - 1] == ' ')
                length--;
        line[length] = 0;
        address_to text = line + skip;
        address_to size = length - skip;
        return true;
}

/*
        What moonwater wifi said, as the list of networks: the ones heard,
        strongest first as it prints them (marker, name, dBm, bars, what it
        asks for), then the saved ones not heard, then the one line it said
        that is about none of them (no networks heard, why it is not joined).
*/
static fn settings_feed_wifi(void)
{
        struct settings_item address_to items = settings_large->item[0];
        char line[SETTINGS_PROBE], note[SETTINGS_ITEM_LABEL] = {0};
        char address_to text;
        positive at, size, count = 0;

        memory_zero(items, sizeof(settings_large->item[0]));
        for (int pass = 0; pass < 2; pass++)
                for (at = 0; settings_feed_line(&at, line, sizeof(line), &text, &size);)
                {
                        positive dbm_at = size > 7 ? settings_find_last(text, size, " dBm ") : size;

                        if (!pass && dbm_at < size && dbm_at > 2)
                        {
                                // marker, name, blanks, the signal in dBm, " dBm ", four bars, what it asks for, band and channel.
                                positive number = dbm_at, name_end, word;
                                struct settings_item address_to item;
                                int level = 0;
                                b32 below = false;

                                while (number > 2 && ((text[number - 1] >= '0' && text[number - 1] <= '9') || text[number - 1] == '-'))
                                        number--;
                                name_end = number;
                                while (name_end > 2 && text[name_end - 1] == ' ')
                                        name_end--;
                                if (name_end <= 2 || count + 1 >= SETTINGS_LIST_ROWS)
                                        continue;
                                item = &items[count++];
                                item->kind = SETTINGS_NETWORK;
                                settings_item_label(item, text + 2, name_end - 2);
                                item->flags = SETTINGS_ITEM_HEARD | (text[0] == '*' ? SETTINGS_ITEM_JOINED | SETTINGS_ITEM_SAVED
                                                                      : text[0] == '+' ? SETTINGS_ITEM_SAVED : 0);
                                for (positive digit = number; digit < dbm_at; digit++)
                                        if (text[digit] == '-')
                                                below = true;
                                        else
                                                level = level * 10 + (text[digit] - '0');
                                item->dbm = (signed short)(below ? -level : level);
                                word = dbm_at + 5;
                                for (positive bar = 0; bar < 4 && word < size; bar++, word++)
                                        item->bars += text[word] == '#';
                                while (word < size && text[word] == ' ')
                                        word++;
                                for (positive own = 0; word + own < size && text[word + own] != ' ' && own + 1 < sizeof(item->security); own++)
                                        item->security[own] = text[word + own];
                                if (string_equals((string_address)item->security, (string_address) "open"))
                                        item->flags |= SETTINGS_ITEM_OPEN;
                                if (!settings_joinable(item->security))
                                        item->flags |= SETTINGS_ITEM_CANNOT;
                        }
                        else if (pass && dbm_at >= size && size > 2 && text[0] == ' ' && text[1] == ' ' && text[2] != ' ')
                        {
                                // A saved network, as a name with two blanks before it, and not "and 3 more".
                                struct settings_item address_to item = settings_item_find(0, text + 2, size - 2);

                                if (settings_starts(text + 2, "and ") && size > 7 && !memory_compare(text + size - 5, " more", 5))
                                        continue;
                                settings.item_count[0] = (unsigned short)count;
                                if (item)
                                        item->flags |= SETTINGS_ITEM_SAVED;
                                else if (count + 1 < SETTINGS_LIST_ROWS)
                                {
                                        item = &items[count++];
                                        item->kind = SETTINGS_NETWORK;
                                        item->flags = SETTINGS_ITEM_SAVED;
                                        settings_item_label(item, text + 2, size - 2);
                                }
                        }
                        else if (pass && dbm_at >= size && size && text[0] != ' ' && !note[0] && !settings_starts(text, "wifi ") &&
                                 !settings_starts(text, "* joined"))
                        {
                                size = size >= sizeof(note) ? sizeof(note) - 1 : size;
                                memory_copy(note, text, size);
                                note[size] = 0;
                        }
                        settings.item_count[0] = (unsigned short)count;
                }
        if (note[0] && count < SETTINGS_LIST_ROWS)
        {
                items[count].kind = SETTINGS_NOTE;
                settings_item_label(&items[count++], note, string_length((string_address)note));
        }
        settings.item_count[0] = (unsigned short)count;
}

// What moonwater bluetooth said, as the list of devices kept: the lines after its first, a name each, and a last row that keeps one more.
static fn settings_feed_devices(void)
{
        struct settings_item address_to items = settings_large->item[1];
        char line[SETTINGS_PROBE];
        char address_to text;
        positive at, size, count = 0;

        memory_zero(items, sizeof(settings_large->item[1]));
        for (at = 0; settings_feed_line(&at, line, sizeof(line), &text, &size);)
                if (size > 2 && text[0] == ' ' && text[1] == ' ' && text[2] != ' ' && count + 1 < SETTINGS_LIST_ROWS)
                {
                        items[count].kind = SETTINGS_DEVICE;
                        items[count].flags = SETTINGS_ITEM_SAVED;
                        settings_item_label(&items[count++], text + 2, size - 2);
                }
        items[count].kind = SETTINGS_ADD;
        settings_item_label(&items[count++], "Keep a device by its name", string_length((string_address) "Keep a device by its name"));
        settings.item_count[1] = (unsigned short)count;
}

// A sum of a list as it stands, to tell whether a reading changed what is shown.
static positive settings_list_sum(int list)
{
        positive sum = 1469598103934665603ull;
        const unsigned char address_to bytes = (const unsigned char address_to)settings_large->item[list];

        for (positive at = 0; at < sizeof(settings_large->item[list]); at++)
                sum = (sum ^ bytes[at]) * 1099511628211ull;
        return sum ^ settings.item_count[list];
}

// The row whose switch a feed's verb also says the state of, or none.
static int settings_feed_owner(int feed)
{
        for (positive at = 0; at < SETTINGS_ROWS && settings_feeds[feed].owner; at++)
                if (string_equals((string_address)settings_rows[at].label, (string_address)settings_feeds[feed].owner))
                        return (int)at;
        return -1;
}

// The feed that keeps up with the group shown, or none.
static int settings_feed_of_group(void)
{
        for (positive feed = 0; feed < SETTINGS_FEEDS; feed++)
                if (string_equals((string_address)settings_feeds[feed].group, (string_address)settings_group_name(settings.group)))
                        return (int)feed;
        return -1;
}

/*
        A feed's verb has said its words, or was stopped: they are the rows
        of its list and the state of its switch. The row the keys are on is
        found again by its name, so a list that moves under it does not move
        the keys, and a reading that says what the last said draws nothing.
*/
static fn settings_feed_read(b32 killed, b32 failed)
{
        int feed = settings.feed_list, list = feed, owner = settings_feed_owner(feed), at;
        char keep[SETTINGS_ITEM_LABEL] = {0};
        positive before, was = 0;
        b32 changed;

        settings.feed_due = clock_monotonic_nanoseconds() + (positive)settings_feeds[feed].every * 1000000ull;
        // What is being typed for a row of the list is for the row as it was: nothing is changed under it.
        if (killed || settings.editing >= (int)SETTINGS_LIST)
                return;
        if (owner >= 0)
        {
                char value[SETTINGS_VALUE];

                string_copy_bounded((string_address)value, (string_address)settings.value[owner], sizeof(value));
                was = settings.state[owner];
                settings.state[owner] = (unsigned char)settings_read(&settings_rows[owner], settings_large->said, settings.said_length,
                                                                     settings.value[owner], SETTINGS_VALUE, failed);
                changed = was != settings.state[owner] || !string_equals((string_address)value, (string_address)settings.value[owner]);
        }
        else
                changed = false;
        before = settings_list_sum(list);
        if (settings_list_of(settings.list.focus) == list && settings_item(settings.list.focus))
                string_copy_bounded((string_address)keep, (string_address)settings_item(settings.list.focus)->label, sizeof(keep));
        at = settings.list.focus - (int)SETTINGS_LIST - list * SETTINGS_LIST_ROWS;

        if (settings_feeds[feed].kind == SETTINGS_FEED_WIFI)
                settings_feed_wifi();
        else
                settings_feed_devices();
        if (settings_list_sum(list) == before && !changed)
                return;

        // The keys stay on the row of that name; one gone leaves them at the place it was, or the last.
        if (keep[0])
        {
                int found = at < settings.item_count[list] ? at : settings.item_count[list] - 1;

                for (int each = 0; each < settings.item_count[list]; each++)
                        if (string_equals((string_address)settings_large->item[list][each].label, (string_address)keep))
                        {
                                found = each;
                                break;
                        }
                settings.list.focus = (int)SETTINGS_LIST + list * SETTINGS_LIST_ROWS + (found < 0 ? 0 : found);
        }
        settings_rebuild();
}

// The rows of the group shown that change by themselves, asked again.
static fn settings_feed_rows(void)
{
        for (positive at = 0; at < SETTINGS_ROWS; at++)
                if ((settings_rows[at].flags & SETTINGS_TICK) && settings_group_of((int)at) == settings.group &&
                    settings.editing != (int)at)
                        settings.unread |= (positive)1 << at;
}

/*
        What a feed wants while the window runs: nothing when it is put
        away, when the group shown has none, when a card or a field or what
        is typed fills the window, and nothing while another verb is
        running or waited for. Then, when it is due, its verb is run; until
        then, how long to wait, in nanoseconds, or -1 for nothing to wait for.
*/
static long settings_feed_tick(void)
{
        int feed = settings_feed_of_group();
        positive now = clock_monotonic_nanoseconds();
        string_address words[SETTINGS_WORDS];
        positive count = 1;

        if (feed < 0 || settings.view || settings.editing >= 0 || settings.filter_length ||
            (settings.own.table->state[settings.own.row] & WINDOW_MINIMIZED) || settings.child > 0 || settings.queued ||
            settings.unread || settings.help == 1)
                return -1;
        if (now < settings.feed_due)
                return (long)(settings.feed_due - now);

        settings.feed_due = now + (positive)settings_feeds[feed].every * 1000000ull;
        if (settings_feeds[feed].kind == SETTINGS_FEED_ROWS)
        {
                settings_feed_rows();
                settings_next_job();
                return -1;
        }
        words[0] = settings.moonwater;
        for (positive at = 0; at < 3 && settings_feeds[feed].read[at]; at++)
                words[count++] = (string_address)settings_feeds[feed].read[at];
        words[count] = null;
        if (settings_start(words, count, null))
        {
                settings.job = SETTINGS_JOB_FEED;
                settings.job_sets = false;
                settings.feed_list = feed;
        }
        return -1;
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

                if (row >= SETTINGS_ANY)
                        settings_report(killed, status);
                else
                {
                        // The verb's own first words, which say what it did or why it would not.
                        while (at < settings.said_length && !size)
                                size = settings_line(settings_large->said, settings.said_length, &at, line, sizeof(line));
                        if (killed)
                                settings_say((string_address) "moonwater took too long, and was stopped");
                        else if (size)
                                settings_say((string_address)line);
                        else
                                settings_say((string_address)(wait_status_code(status) ? "moonwater refused" : "done"));
                }
                // What was done to a row of a list is seen at once in the list; any other change may move another row.
                if (row == SETTINGS_JOB_ACT)
                        settings.feed_due = 0;
                else
                        settings_ask_all();
        }
        else if (row == SETTINGS_JOB_FEED)
                settings_feed_read(killed, wait_status_code(status) != 0);
        else if (row == SETTINGS_JOB_HELP)
        {
                if (!killed)
                        settings_help(settings_large->said, settings.said_length);
                settings_refilter();
                if (settings.truncated)
                        settings_say((string_address) "moonwater help says more than All commands lists: the first row runs any command");
        }
        else
        {
                // What a row's verb said is what every row waiting to be asked the same of says too: one run for them all.
                for (positive other = 0; other < SETTINGS_ROWS; other++)
                {
                        char before[SETTINGS_VALUE];
                        unsigned char was = settings.state[other];

                        if ((int)other != row && !((settings.unread >> other) & 1))
                                continue;
                        if (!settings_same_verb(&settings_rows[row], &settings_rows[other]))
                                continue;
                        settings.unread &= ~((positive)1 << other);
                        string_copy_bounded((string_address)before, (string_address)settings.value[other], sizeof(before));
                        settings.state[other] = killed ? SETTINGS_FAILED
                                                       : (unsigned char)settings_read(&settings_rows[other], settings_large->said,
                                                                                       settings.said_length, settings.value[other],
                                                                                       SETTINGS_VALUE, wait_status_code(status) != 0);
                        // A row asked again that says what it said is no change to draw.
                        if (settings.state[other] != was ||
                            !string_equals((string_address)before, (string_address)settings.value[other]))
                                settings_dirty();
                }
        }

        settings_next_job();

        // The first reading of every row, said once where the machine's log is: how many were unread or not understood.
        if (!settings.told && !settings.unread && settings.child <= 0 && !settings.queued)
        {
                positive failed = 0;

                for (positive at = 0; at < SETTINGS_ROWS; at++)
                        failed += settings.state[at] == SETTINGS_FAILED || settings.state[at] == SETTINGS_UNREAD;
                settings.told = true;
                // None is a number too: compositor_kmsg writes no number for a zero, which is its way to say none, so the
                // 0 is the tail here.
                compositor_kmsg((string_address) "settings: rows unread or not understood when first asked: ", failed,
                                failed ? (string_address) "" : (string_address) "0");
        }
}

// A feed's reading under way is dropped, for a change that is wanted now.
static fn settings_feed_drop(void)
{
        if (settings.child > 0 && settings.job == SETTINGS_JOB_FEED)
                settings_finished(true);
}

//      Read in one turn at most this much: a verb writing forever (yes, a log
//      that never stops) never answers EAGAIN, and the loop read it for ever
//      with the window frozen. What is left is read on the next wake.
#define SETTINGS_DRAIN_BUDGET (64u << 10)

// What the verb running has said, read; its end, heard.
static fn settings_drain(void)
{
        positive budget = SETTINGS_DRAIN_BUDGET;

        for (;;)
        {
                bipolar got;

                if (settings.said_length >= sizeof(settings_large->said))
                {
                        char spill[256];

                        got = system_read_once(settings.output, spill, sizeof(spill));
                }
                else
                        got = system_read_once(settings.output, settings_large->said + settings.said_length,
                                               sizeof(settings_large->said) - settings.said_length);
                if (got > 0)
                {
                        settings.said_length += settings.said_length < sizeof(settings_large->said) ? (positive)got : 0;
                        budget = budget > (positive)got ? budget - (positive)got : 0;
                        if (!budget)
                                return;
                        continue;
                }
                if (got == 0 || (got != -EAGAIN && got != -EINTR))
                        settings_finished(false);
                return;
        }
}

/* ---- what a person does ---- */

/*
        A row of All commands run with args, in words: false when it did not
        run and the field stays open, because there was nothing to run, there
        were too many words, or it ends what a person has and asked for a
        second Enter.
*/
static b32 settings_run(int row, const char address_to args)
{
        string_address words[SETTINGS_WORDS];
        positive count = settings_generic_words(row, args, words, SETTINGS_WORDS);

        if (!count)
        {
                settings_say((string_address) "that is more words than one command takes");
                return false;
        }
        if (count == 1)
        {
                settings_say((string_address) "type a command: its verb, then what follows it");
                return false;
        }
        if (settings_asks_for(words + 1, count - 1) &&
            (settings.confirm != row + 1 || !string_equals((string_address)settings.confirm_args, (string_address)args)))
        {
                char line[SETTINGS_STATUS];

                settings.confirm = row + 1;
                string_copy_bounded((string_address)settings.confirm_args, (string_address)args, sizeof(settings.confirm_args));
                string_copy_bounded((string_address)line, (string_address) "press Enter again to run: ", sizeof(line));
                {
                        char text[SETTINGS_STATUS];

                        settings_join(words, count, text, sizeof(text));
                        string_append_bounded((string_address)line, (string_address)text, sizeof(line));
                }
                settings_say((string_address)line);
                return false;
        }
        settings.confirm = 0;
        settings_change(row, args);
        return true;
}

// A running command of All commands ended by hand: what it said so far is shown.
static b32 settings_stop(void)
{
        if (settings.child <= 0 || !settings.job_sets || settings.job < SETTINGS_ANY)
                return false;
        settings_finished(true);
        return true;
}

/* ---- the list a choice opens ---- */

/*
        A choice row opens a list of all its choices under it, the one it
        has now lit and the keys on it. The list takes every key and every
        press until it is closed: Up, Down, Home, End, Page Up and Page Down
        move the keys, a letter goes to the next choice that starts with it,
        Enter or Space (or a press on a choice) sets the choice the keys
        are on and closes it, Escape (or a press anywhere else) closes it
        and leaves the row as it was, and the wheel scrolls it.
*/

/*
        The words of a choice: the row's own, or for the Battery picker auto
        (the first present battery, the default), then each battery the list
        was taken with, then none. The list is taken when the list opens and
        kept, so drawing it reads no kernel file.
*/
static b32 settings_choice(const struct settings_row address_to row, positive index, char address_to into, positive room)
{
        positive names = settings.battery_menu_count < BATTERY_LIST ? settings.battery_menu_count : BATTERY_LIST;

        if (!(row->flags & SETTINGS_BATTERIES))
                return settings_word(row->choices, index, into, room);
        if (index == 0)
                string_copy_bounded((string_address)into, (string_address) "auto", room);
        else if (index <= names)
                string_copy_bounded((string_address)into, (string_address)settings.battery_menu[index - 1].text, room);
        else if (index == names + 1)
                string_copy_bounded((string_address)into, (string_address) "none", room);
        else
                return false;
        return true;
}

// The Battery picker's row, or -1.
static int settings_picker(void)
{
        for (positive at = 0; at < SETTINGS_ROWS; at++)
                if (settings_rows[at].flags & SETTINGS_BATTERIES)
                        return (int)at;
        return -1;
}

// What the Battery row reads: the picker's value, which is auto until the picker has been read with one.
static const char address_to settings_battery_choice(void)
{
        int picker = settings_picker();

        if (picker < 0 || settings.state[picker] != SETTINGS_READ || !settings.value[picker][0])
                return "auto";
        return settings.value[picker];
}

// How many choices a row has.
static positive settings_options(const struct settings_row address_to row)
{
        char word[SETTINGS_VALUE];
        positive count = 0;

        while (settings_choice(row, count, word, sizeof(word)))
                count++;
        return count;
}

static fn settings_menu_close(void)
{
        if (settings.menu.owner < 0)
                return;
        settings.menu.owner = -1;
        settings_say((string_address) "");
        settings_dirty();
}

// The menu placed under its row (or over it), its right edge at the row's: false when its row is not in the list.
static b32 settings_menu_place(void)
{
        int at = settings_pos(settings.menu.owner), top;

        if (at < 0 || settings.ui.scale <= 0)
                return false;
        top = ui_list_row_y(&settings.list, at);
        ui_menu_place(&settings.ui, &settings.menu, top, top + ui_px(&settings.ui, UI_ROW), settings.page.right - ui_px(&settings.ui, UI_INSET),
                      settings.width, settings.height);
        return true;
}

// A choice's menu: its words, the one the row has now where the keys start (a value none of them is starts at the first).
static fn settings_menu_open(int row)
{
        const struct settings_row address_to which = &settings_rows[row];
        char word[SETTINGS_VALUE];

        if (which->flags & SETTINGS_BATTERIES)
                settings.battery_menu_count = battery_list(settings.battery_menu, BATTERY_LIST);
        settings.list.focus = row;
        ui_menu_open(&settings.menu, row, settings.touching);
        for (int at = 0; settings_choice(which, (positive)at, word, sizeof(word)); at++)
                ui_menu_add(&settings.ui, &settings.menu, word, string_equals((string_address)word, (string_address)settings.value[row]));
        settings_reveal();
        if (settings_menu_place())
                ui_menu_go(&settings.ui, &settings.menu, settings.menu.at);
        settings_say((string_address) "Up and Down choose, Enter sets it, Escape leaves it");
        settings_dirty();
}

// The choice the keys are on, set, and the menu closed; a choice the row has already sends nothing.
static fn settings_menu_apply(int at)
{
        char word[SETTINGS_VALUE];
        int row = settings.menu.owner;

        string_copy_bounded((string_address)word, (string_address)settings.menu.word[at < 0 ? 0 : at], sizeof(word));
        settings_menu_close();
        if (at >= 0 && !string_equals((string_address)word, (string_address)settings.value[row]))
                settings_change(row, word);
}

// A key with the menu open: it has every key.
static fn settings_menu_key(const struct window_event address_to event)
{
        settings_menu_place();
        switch (ui_menu_key(&settings.ui, &settings.menu, (int)event->a, (unsigned int)event->b, event->flags))
        {
        case UI_MENU_CLOSED: settings_menu_close(); break;
        case UI_MENU_CHOSEN: settings_menu_apply(settings.menu.at); break;
        default: settings_dirty();
        }
}

/*
        What is done to a network or a device, as a change for the verb:
        a letter (j joins a network, f forgets it, a keeps a device, d
        forgets it), a newline, the name and for a join a newline and its
        passphrase. What is built here is not left behind.
*/
static fn settings_act(char what, const char address_to name, const char address_to pass)
{
        char value[SETTINGS_TYPED];
        positive size = 0;

        value[size++] = what;
        value[size++] = '\n';
        for (positive at = 0; name[at] && size + 2 < sizeof(value); at++)
                value[size++] = name[at];
        if (pass && pass[0])
        {
                value[size++] = '\n';
                for (positive at = 0; pass[at] && size + 1 < sizeof(value); at++)
                        value[size++] = pass[at];
        }
        value[size] = 0;
        settings_change(SETTINGS_JOB_ACT, value);
        memory_zero(value, sizeof(value));
}

// A field typed in is done with, and nothing typed for a passphrase is kept.
static fn settings_edit_end(void)
{
        if (settings.editing >= (int)SETTINGS_LIST)
                memory_zero(settings.typed, sizeof(settings.typed));
        settings.editing = -1;
        settings_dirty();
}

// A row of a list pressed: a network is joined (asking its passphrase when it has one), a device is said what can be done.
static fn settings_item_activate(int row, const struct settings_item address_to item)
{
        char line[SETTINGS_STATUS];

        if (item->kind == SETTINGS_NETWORK)
        {
                if (item->flags & SETTINGS_ITEM_JOINED)
                {
                        string_copy_bounded((string_address)line, (string_address) "already joined to ", sizeof(line));
                        string_append_bounded((string_address)line, (string_address)item->label, sizeof(line));
                        settings_say((string_address)line);
                }
                else if (item->flags & SETTINGS_ITEM_CANNOT)
                        settings_say((string_address) "moonwater cannot join what that network asks for");
                else if (item->flags & SETTINGS_ITEM_OPEN)
                        settings_act('j', item->label, null);
                else
                {
                        settings.editing = row;
                        settings.typed[0] = 0;
                        string_copy_bounded((string_address)settings.joining, (string_address)item->label, sizeof(settings.joining));
                        settings_say((string_address) "type its passphrase: Enter joins it, Escape leaves it");
                }
        }
        else if (item->kind == SETTINGS_ADD)
        {
                settings.editing = row;
                settings.typed[0] = 0;
                settings_say((string_address) "type the name of a device: Enter keeps it, Escape leaves it");
        }
        else
                settings_say((string_address)settings_item_text(item));
        settings_dirty();
}

// Delete on a saved network or a kept device forgets it, when it is pressed twice.
static fn settings_forget(void)
{
        const struct settings_item address_to item = settings_item(settings.list.focus);
        char line[SETTINGS_STATUS];

        if (!item || !(item->flags & SETTINGS_ITEM_SAVED) || (item->kind != SETTINGS_NETWORK && item->kind != SETTINGS_DEVICE))
                return;
        if (!string_equals((string_address)settings.forget, (string_address)item->label))
        {
                string_copy_bounded((string_address)settings.forget, (string_address)item->label, sizeof(settings.forget));
                string_copy_bounded((string_address)line, (string_address) "press Delete again to forget ", sizeof(line));
                string_append_bounded((string_address)line, (string_address)item->label, sizeof(line));
                settings_say((string_address)line);
                return;
        }
        settings.forget[0] = 0;
        settings_act(item->kind == SETTINGS_NETWORK ? 'f' : 'd', item->label, null);
}

// A row changed one way or the other: a switch thrown, a choice or a step moved, a field opened, an action run.
static fn settings_activate(int row, int way)
{
        const struct settings_item address_to item = settings_item(row);
        const struct settings_row address_to which = row < SETTINGS_ANY ? &settings_rows[row] : null;
        char next[SETTINGS_VALUE];

        if (item)
        {
                settings_item_activate(row, item);
                return;
        }
        if (!which)
        {
                // A command with something to type opens a field; one without is run.
                if (settings_takes(row))
                {
                        settings.editing = row;
                        settings.typed[0] = 0;
                        settings_dirty();
                }
                else
                        settings_run(row, "");
                return;
        }
        if (settings.state[row] == SETTINGS_ABSENT || settings.state[row] == SETTINGS_UNREAD)
                return;

        switch (which->kind)
        {
        case SETTINGS_FIELD:
                settings.editing = row;
                string_copy_bounded((string_address)settings.typed, (string_address)settings.value[row], sizeof(settings.typed));
                settings_dirty();
                return;
        case SETTINGS_INFO:
                return;
        case SETTINGS_CHOICE:
                settings_menu_open(row);
                return;
        case SETTINGS_ACTION:
                // A set asked for while one is running or waiting is that set: the clock is set once for it.
                if (settings_slot(row) < settings.queued || (settings.child > 0 && settings.job == row && settings.job_sets))
                        return;
                settings_change(row, "");
                return;
        }

        settings_next(which, settings.value[row], way, next, sizeof(next));
        if (next[0] && !string_equals((string_address)next, (string_address)settings.value[row]))
                settings_change(row, next);
}

// The status as the keys come to a row from another: a command says what it does, and leaving one clears it.
static fn settings_said_of(int before)
{
        if (settings.list.focus >= SETTINGS_ANY)
                settings_say((string_address)settings_text(settings.list.focus));
        else if (before >= SETTINGS_ANY)
                settings_say((string_address) "");
}

// The focus moved by whole rows of the ones that show; a command row has what it does in the status.
static fn settings_move(int by)
{
        int before = settings.list.focus;

        if (ui_list_move(&settings.ui, &settings.list, by))
                settings_said_of(before);
}

/*
        The rows of another group at the right, as the column of groups is
        moved: what is typed is cleared (it listed the rows of every group),
        and the focus goes to the first row, which is where the keys go
        when the rows are entered.
*/
static fn settings_group_choose(int group)
{
        int last = settings_groups() - 1, before = settings.group;
        b32 typed = settings.filter_length != 0;

        group = group < 0 ? 0 : group > last ? last : group;
        if (group == before && !typed)
                return;
        settings.group = group;
        settings.feed_due = 0;
        settings.filter[0] = 0;
        settings.filter_length = 0;
        if (settings.filter_open)
                ui_text_set(&settings.filter_edit, (const p8 address_to) "", 0);
        settings.list.scroll.scroll = 0;
        settings.confirm = 0;
        settings_refilter();
        if (settings.list.count)
                settings.list.focus = settings.list.id[0];
        settings_side_reveal();
        settings_say((string_address)(settings.list.focus >= SETTINGS_ANY ? settings_text(settings.list.focus) : ""));
        settings_dirty();
}

// The keys go to the rows of the group chosen.
static fn settings_rows_enter(void)
{
        settings.pane = 1;
        if (settings_pos(settings.list.focus) < 0 && settings.list.count)
                settings.list.focus = settings.list.id[0];
        settings_reveal();
        settings_dirty();
}

// A key in a field being typed in: Enter keeps it, Escape leaves it, Backspace takes a character.
static fn settings_type(const struct window_event address_to event)
{
        positive length = string_length((string_address)settings.typed);

        if (event->a != SETTINGS_KEY_ENTER && event->a != SETTINGS_KEY_ENTER_PAD)
                settings.confirm = 0;
        switch (event->a)
        {
        case SETTINGS_KEY_ENTER:
        case SETTINGS_KEY_ENTER_PAD:
                // A passphrase joins the network, a name keeps the device; neither is kept here after.
                if (settings.editing >= (int)SETTINGS_LIST)
                {
                        const struct settings_item address_to item = settings_item(settings.editing);

                        if (!length)
                                settings_say((string_address) "nothing typed");
                        else if (item && item->kind == SETTINGS_NETWORK)
                                settings_act('j', item->label, settings.typed);
                        else if (item && item->kind == SETTINGS_ADD)
                                settings_act('a', settings.typed, null);
                        settings_edit_end();
                        break;
                }
                // A command runs with what is typed, which may be nothing; the field stays when it asked for another Enter.
                if (settings.editing >= SETTINGS_ANY)
                {
                        if (settings_run(settings.editing, settings.typed))
                                settings.editing = -1;
                        break;
                }
                // Kept as it was, or typed to what it is, is no change and sends nothing.
                if (!length)
                        settings_say((string_address) "nothing typed: the value is kept");
                else if (!string_equals((string_address)settings.typed, (string_address)settings.value[settings.editing]))
                        settings_change(settings.editing, settings.typed);
                settings.editing = -1;
                break;
        case SETTINGS_KEY_ESCAPE:
                settings_edit_end();
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
                positive size;
                b32 name = settings.editing == SETTINGS_NAME_ROW;
                // A value of one word or line is kept in what a row reads; what is typed for a list or a command is longer.
                positive most = settings.editing >= (int)SETTINGS_LIST ? SETTINGS_ITEM_LABEL
                                : settings.editing >= SETTINGS_ANY || (settings_rows[settings.editing].flags & SETTINGS_SPLIT)
                                    ? sizeof(settings.typed)
                                    : SETTINGS_VALUE;

                if (c < ' ' || c == 0x7f || (event->flags & (WINDOW_KEY_CONTROL | WINDOW_KEY_ALT)))
                        return;
                // What the verb takes a name for, and says so when it is not: a name is typed in, not cut short.
                if (name && !((c >= '0' && c <= '9') || ((c | 32) >= 'a' && (c | 32) <= 'z') || c == '-'))
                {
                        settings_say((string_address) "a name is letters, digits and hyphens");
                        return;
                }
                if (name && length >= SETTINGS_NAME_MOST)
                {
                        settings_say((string_address) "a name is under 32 characters");
                        return;
                }
                size = memory_utf8_encode(bytes, sizeof(bytes), c);
                if (!size || length + size >= most)
                        return;
                memory_copy(settings.typed + length, bytes, size);
                settings.typed[length + size] = 0;
        }
        }
        settings_dirty();
}

// The filter's field, made the first time it is wanted: one line of the text engine, its clipboard this window's.
static struct ui_text address_to settings_filter_field(void)
{
        if (!settings.filter_open)
        {
                ui_text_open(&settings.filter_edit, UI_TEXT_FIELD, (const p8 address_to) "", 0, ui_text_keymap());
                settings.filter_edit.own = &settings.own;
                settings.filter_open = true;
        }
        return &settings.filter_edit;
}

// The filter's text as the rows are matched against it: the field's, folded to lower case.
static b32 settings_filter_take(void)
{
        p8 typed[SETTINGS_FILTER];
        positive length = ui_text_string(settings_filter_field(), typed, sizeof(typed));

        for (positive at = 0; at < length; at++)
                typed[at] = (p8)byte_to_lower((b8)typed[at]);
        if (length == settings.filter_length && !memory_compare(typed, settings.filter, length))
                return false;
        memory_copy(settings.filter, typed, length + 1);
        settings.filter_length = length;
        return true;
}

/*
        What is typed with no field open goes to the filter's field: a letter, Backspace, Ctrl+A and the rest of the
        text keys, Ctrl+V's paste (whose text comes with the clipboard's answer).
*/
static fn settings_filter_key(const struct window_event address_to event)
{
        struct ui_text address_to field = settings_filter_field();

        // A blank is no letter of the filter, as it never was: rows are matched by words that have none.
        if (event->b == ' ' && !(event->flags & (WINDOW_KEY_CONTROL | WINDOW_KEY_ALT)))
                return;
        field->focused = true;
        ui_text_key(&settings.ui, field, event);
        if (!settings_filter_take())
        {
                settings_dirty();
                return;
        }
        settings.list.scroll.scroll = 0;
        settings.pane = 1;
        settings_refilter();
        // The keys are on the first row that matches.
        if (settings.list.count)
        {
                settings.list.focus = settings.list.id[0];
                settings_reveal();
        }
}

// Whether Left and Right move a row: a step does. A switch is turned by Enter or Space only, so the arrows never change it.
static b32 settings_steps(int row)
{
        return row < SETTINGS_ANY && settings_rows[row].kind == SETTINGS_STEP;
}

// A key on the card of what a command said: it scrolls as a page of text does, and Escape, Enter, Space or Backspace go back to the list.
static fn settings_card_key(const struct window_event address_to event)
{
        b32 moved = false;

        if (ui_lines_key(&settings.ui, &settings.card, (int)event->a, &moved))
        {
                if (moved)
                        settings_changed(settings.page.top, settings.page.bottom);
                return;
        }
        switch (event->a)
        {
        case SETTINGS_KEY_ESCAPE:
        case SETTINGS_KEY_ENTER:
        case SETTINGS_KEY_ENTER_PAD:
        case SETTINGS_KEY_SPACE:
        case SETTINGS_KEY_BACKSPACE:
                settings_card_leave();
                break;
        }
}

// A key: false when it closes the window.
static b32 settings_key(const struct window_event address_to event)
{
        if (!(event->flags & WINDOW_KEY_DOWN))
                return true;
        // The menu a choice opened has every key.
        if (settings.menu.owner >= 0)
        {
                settings_menu_key(event);
                return true;
        }
        if (settings.editing >= 0)
        {
                settings_type(event);
                return true;
        }
        if (settings.view)
        {
                settings_card_key(event);
                return true;
        }
        if (event->a != SETTINGS_KEY_ENTER && event->a != SETTINGS_KEY_ENTER_PAD && event->a != SETTINGS_KEY_SPACE)
                settings.confirm = 0;
        if (event->a != SETTINGS_KEY_DELETE)
                settings.forget[0] = 0;

        switch (event->a)
        {
        case SETTINGS_KEY_ESCAPE:
                // What is typed is cleared first, a command running is stopped next, and only then the window closes.
                if (settings.filter_length)
                {
                        settings.filter[0] = 0;
                        settings.filter_length = 0;
                        if (settings.filter_open)
                                ui_text_set(&settings.filter_edit, (const p8 address_to) "", 0);
                        settings_refilter();
                        return true;
                }
                return settings_stop();
        case SETTINGS_KEY_TAB:
                // Between the column of groups and the rows, whichever row or group is there.
                settings.pane = !settings.pane;
                if (settings.pane)
                        settings_rows_enter();
                settings_dirty();
                break;
        case SETTINGS_KEY_UP:
        case SETTINGS_KEY_DOWN:
        case SETTINGS_KEY_HOME:
        case SETTINGS_KEY_END:
        case SETTINGS_KEY_PAGE_UP:
        case SETTINGS_KEY_PAGE_DOWN:
                // The kit's moves: a row at a time, a view, an end in the rows; a group, three, an end in the column.
                if (settings.pane)
                {
                        int before = settings.list.focus;
                        b32 moved = false;

                        ui_list_key(&settings.ui, &settings.list, (int)event->a, &moved);
                        if (moved)
                                settings_said_of(before);
                }
                else
                        settings_group_choose(ui_page_column_key(&settings.page, (int)event->a, settings.group));
                break;
        case SETTINGS_KEY_ENTER:
        case SETTINGS_KEY_ENTER_PAD:
        case SETTINGS_KEY_SPACE:
                if (!settings.pane)
                        settings_rows_enter();
                else if (settings_pos(settings.list.focus) >= 0)
                        settings_activate(settings.list.focus, 1);
                break;
        case SETTINGS_KEY_DELETE:
                if (settings.pane)
                        settings_forget();
                break;
        case SETTINGS_KEY_LEFT:
                // A step moves; on a switch and on any other row Left goes back to the column.
                if (settings.pane && settings_steps(settings.list.focus))
                        settings_activate(settings.list.focus, -1);
                else if (settings.pane)
                {
                        settings.pane = 0;
                        settings_dirty();
                }
                break;
        case SETTINGS_KEY_RIGHT:
                if (!settings.pane)
                        settings_rows_enter();
                else if (settings_steps(settings.list.focus))
                        settings_activate(settings.list.focus, 1);
                break;
        default:
                settings_filter_key(event);
        }
        return true;
}

/* ---- the rows, as the kit draws them ---- */

static b32 settings_row_is(int at, const char address_to path)
{
        return at >= 0 && at < (int)SETTINGS_ROWS && settings_rows[at].path && string_equals((string_address)settings_rows[at].path, (string_address)path);
}

/*
        What a row shows, for the kit's list: its label, its section and the section's header, and its value as the kind
        the verb gives it -- a switch is the kit's switch (it moves when the verb says it changed), the theme and the
        motion the kit's segmented tabs, the accent a swatch for each named accent, a choice opens a menu, a step moves
        either way, and a field typed in is the kit's field. A network is its name, marked when it is the one joined,
        with what is said of it, what it asks for and the bars of its signal; a device kept, the row that keeps
        another, the line the verb said that is no row; a command, and whether it asks first. A value not read yet,
        not on this machine or not understood says so, quietly.
*/
static fn settings_describe(int id, struct ui_row address_to row)
{
        const struct settings_row address_to which = id < SETTINGS_ANY ? &settings_rows[id] : null;
        const struct settings_item address_to item = settings_item(id);
        unsigned int state = which ? settings.state[id] : SETTINGS_READ;
        const char address_to shown = which ? settings.value[id] : "";

        row->section = settings_section_start(id);
        row->header = settings_section(id);
        row->label = settings_label(id);
        if (id == settings.editing)
        {
                const struct settings_generic address_to generic = settings_generic_of(id);

                row->kind = UI_ROW_FIELD, row->typed = settings.typed;
                if (item)
                {
                        row->label = item->kind == SETTINGS_ADD ? "Name of the device" : settings.joining;
                        row->masked = item->kind == SETTINGS_NETWORK;
                }
                else if (!which)
                {
                        // A command being typed: its verb, and the field for what follows it, which says what it takes.
                        const char address_to hint = generic ? settings_large->pool + generic->command + generic->verb : "VERB [ARGUMENTS]";

                        if (generic)
                        {
                                memory_copy(settings.head, settings_large->pool + generic->command, generic->verb);
                                settings.head[generic->verb] = 0;
                        }
                        else
                                string_copy_bounded((string_address)settings.head, (string_address) "moonwater", sizeof(settings.head));
                        while (*hint == ' ')
                                hint++;
                        row->label = settings.head, row->placeholder = hint, row->head = true;
                }
                return;
        }
        if (item)
        {
                if (item->kind == SETTINGS_NETWORK)
                {
                        row->mark = (item->flags & SETTINGS_ITEM_JOINED) != 0;
                        row->note = item->flags & SETTINGS_ITEM_JOINED ? "joined" : item->flags & SETTINGS_ITEM_SAVED ? "saved" : null;
                        row->value = item->security[0] ? item->security : null;
                        row->bars = item->flags & SETTINGS_ITEM_HEARD ? item->bars : -1;
                        row->dim = !(item->flags & SETTINGS_ITEM_HEARD);
                }
                else if (item->kind == SETTINGS_DEVICE)
                        row->value = "kept";
                row->dim |= item->kind == SETTINGS_NOTE;
                return;
        }
        if (!which)
        {
                row->value = settings_row_asks(id) ? "asks first" : null;
                return;
        }
        row->dim = state == SETTINGS_UNREAD || state == SETTINGS_ABSENT;
        row->value = state == SETTINGS_UNREAD ? "..." : state == SETTINGS_ABSENT ? "not on this machine"
                     : state == SETTINGS_FAILED || !shown[0] ? "unknown" : shown;
        if (state != SETTINGS_READ)
                return;
        if (which->kind == SETTINGS_SWITCH)
                row->kind = UI_ROW_SWITCH, row->on = string_equals((string_address)shown, (string_address) "on");
        else if (settings_row_is(id, SETTING_THEME_PATH) || settings_row_is(id, SETTING_MOTION_PATH))
                row->kind = UI_ROW_TABS, row->words = which->choices, row->at = (int)setting_place(which->choices, (string_address)shown);
        else if (settings_row_is(id, SETTING_ACCENT_PATH) && which->kind == SETTINGS_CHOICE)
        {
                row->kind = UI_ROW_SWATCHES, row->words = settings.accents;
                for (positive k = 0; k < sizeof(settings_accents) / sizeof(settings_accents[0]); k++)
                        if (string_equals((string_address)shown, (string_address)settings_accents[k].name))
                                row->at = (int)k;
        }
        else if (which->kind == SETTINGS_CHOICE)
                row->kind = UI_ROW_CHOICE;
        else if (which->kind == SETTINGS_STEP)
                row->kind = UI_ROW_STEP;
}

/* ---- the gallery ---- */

/*
        The kit's gallery (moonwater settings gallery, -g): every control of
        ui.c's kit, laid out as Dawning Native's own demo page lays them out in
        Chrome at 1200 pixels (the provision dialog, the preferences, the tabs,
        the build target's radios and checkboxes, the actions and the upgrades'
        progress), and in the grid's empty cell the pieces its demo does not
        show (badges, tags, keys, avatars, spinners, a callout and a code well).
        The page is the theme's paper and not glass, so it is the web page's
        picture to compare, and the wheel scrolls it. Nothing in it runs a
        verb: it is there to be pressed.
*/
#define SETTINGS_GALLERY_W 1092
#define SETTINGS_GALLERY_H 1538
#define SETTINGS_GALLERY_MOST 72

static struct
{
        b32 on;
        struct ui_control control[SETTINGS_GALLERY_MOST];
        int count;
        struct ui_scroll scroll; // the page, device pixels: the wheel, the keys and its scrollbar move it
} settings_gallery;

static struct ui_control address_to settings_gallery_put(int kind, int variant, int x, int y, int w, int h, const char address_to label)
{
        struct ui_control address_to c = &settings_gallery.control[settings_gallery.count < SETTINGS_GALLERY_MOST - 1 ? settings_gallery.count++ : settings_gallery.count];

        memory_zero(c, sizeof(*c));
        c->kind = (unsigned char)kind, c->variant = (unsigned char)variant;
        c->x = x, c->y = y, c->w = w, c->h = h;
        c->label = label;
        return c;
}

static fn settings_gallery_slider(struct ui_control address_to c, int low, int high, int step, int value)
{
        c->low = ui_q(low), c->high = ui_q(high), c->step = ui_q(step), c->value = ui_q(value);
}

// The page, from the demo's layout in Chrome (page pixels, the first card's top left at 24, 24).
static fn settings_gallery_build(void)
{
        settings_gallery.count = 0;
        settings_gallery_put(UI_WINDOW, 0, 24, 24, 332, 606, "Provision node");
        settings_gallery_put(UI_CARD, 0, 380, 24, 332, 606, null);
        settings_gallery_put(UI_CARD, 0, 736, 24, 332, 606, null);
        settings_gallery_put(UI_CARD, 0, 24, 654, 332, 507, null);
        settings_gallery_put(UI_CARD, 0, 380, 654, 332, 507, null);
        settings_gallery_put(UI_CARD, 0, 24, 1185, 1044, 329, null);
        settings_gallery_put(UI_RULE, 0, 48, 537, 284, 1, null);
        settings_gallery_put(UI_RULE, 0, 405, 233, 282, 1, null);
        settings_gallery_put(UI_RULE, 0, 49, 927, 282, 1, null);
        settings_gallery_put(UI_RULE, 0, 49, 1420, 994, 1, null);
        settings_gallery_put(UI_HEADING, UI_TITLE_MEDIUM, 405, 49, 282, 24, "System preferences")->role = UI_ON_SURFACE;
        settings_gallery_put(UI_HEADING, UI_TITLE_MEDIUM, 761, 49, 282, 24, "Navigation")->role = UI_ON_SURFACE;
        settings_gallery_put(UI_HEADING, UI_TITLE_MEDIUM, 49, 679, 282, 24, "Build target")->role = UI_ON_SURFACE;
        settings_gallery_put(UI_HEADING, UI_TITLE_MEDIUM, 405, 679, 282, 24, "Actions")->role = UI_ON_SURFACE;
        settings_gallery_put(UI_HEADING, UI_TITLE_MEDIUM, 49, 1210, 994, 24, "System upgrades")->role = UI_ON_SURFACE;
        settings_gallery_put(UI_HEADING, UI_BODY_MEDIUM, 49, 1250, 994, 20, "Firmware DB-9 patching in progress\xe2\x80\xa6")->role = UI_ON_SURFACE_VARIANT;
        settings_gallery_put(UI_HEADING, UI_BODY_MEDIUM, 49, 1338, 994, 20, "Node replication (80%)")->role = UI_ON_SURFACE_VARIANT;
        settings_gallery_put(UI_HEADING, UI_BODY_MEDIUM, 49, 1461, 221, 20, "3 nodes pending a kernel rebuild.")->role = UI_ON_SURFACE_VARIANT;
        settings_gallery_put(UI_INPUT, 0, 48, 117, 284, 78, "Node name")->items[0] = "my-kernel";
        settings_gallery_put(UI_INPUT, 0, 48, 203, 284, 78, "Region")->items[0] = "moon-1";
        settings_gallery_slider(settings_gallery_put(UI_SLIDER, 0, 48, 289, 284, 88, "vCPU allocation"), 1, 64, 1, 8);
        settings_gallery_slider(settings_gallery_put(UI_SLIDER, 0, 405, 266, 282, 88, "Log verbosity"), 0, 100, 1, 70);
        settings_gallery_slider(settings_gallery_put(UI_SLIDER, UI_SLIDER_STEPS, 405, 370, 282, 88, "Replication limit"), 0, 100, 25, 50);
        settings_gallery_put(UI_SWITCH, 0, 48, 393, 284, 48, "Pin to bare metal")->on = 1;
        settings_gallery_put(UI_SWITCH, 0, 48, 457, 284, 48, "Auto-deploy on push")->on = 0;
        settings_gallery_put(UI_SWITCH, 0, 405, 89, 282, 48, "Telemetry")->on = 1;
        settings_gallery_put(UI_SWITCH, 0, 405, 153, 282, 48, "Offline cache")->on = 0;
        settings_gallery_put(UI_CHECKBOX, 0, 49, 960, 282, 48, "Enable WASM target")->on = 1;
        settings_gallery_put(UI_CHECKBOX, 0, 49, 1024, 282, 48, "Build as no_std")->on = 1;
        settings_gallery_put(UI_CHECKBOX, 0, 49, 1088, 282, 48, "Strip symbols")->on = 0;
        {
                struct ui_control address_to radios = settings_gallery_put(UI_RADIOS, 0, 49, 719, 282, 48, null);

                radios->items[0] = "x86-64";
                radios->items[1] = "ARM64";
                radios->items[2] = "RISC-V (experimental)";
                radios->count = 3;
                radios->at = 0;
        }
        settings_gallery_put(UI_BUTTON, UI_BUTTON_TEXT, 48, 570, 67, 36, "Cancel")->disabled = 0;
        settings_gallery_put(UI_BUTTON, UI_BUTTON_PRIMARY, 204, 570, 129, 36, "Provision node")->disabled = 0;
        settings_gallery_put(UI_BUTTON, UI_BUTTON_PRIMARY, 405, 719, 75, 36, "Deploy")->disabled = 0;
        settings_gallery_put(UI_BUTTON, UI_BUTTON_DEFAULT, 492, 719, 62, 36, "Build")->disabled = 0;
        settings_gallery_put(UI_BUTTON, UI_BUTTON_OUTLINED, 566, 719, 63, 36, "Logs")->disabled = 0;
        settings_gallery_put(UI_BUTTON, UI_BUTTON_TEXT, 405, 767, 67, 36, "Cancel")->disabled = 0;
        settings_gallery_put(UI_BUTTON, UI_BUTTON_DEFAULT, 405, 819, 87, 36, "Disabled")->disabled = 1;
        settings_gallery_put(UI_BUTTON, UI_BUTTON_OUTLINED, 504, 819, 87, 36, "Disabled")->disabled = 1;
        settings_gallery_put(UI_BUTTON, UI_BUTTON_PRIMARY, 916, 1453, 127, 36, "Verify updates")->disabled = 0;
        {
                struct ui_control address_to tabs = settings_gallery_put(UI_TABS, UI_TABS_SEGMENTED, 761, 89, 223, 32, null);

                tabs->items[0] = "All";
                tabs->items[1] = "Running";
                tabs->items[2] = "Stopped";
                tabs->count = 3;
        }
        settings_gallery_put(UI_PROGRESS, 0, 49, 1294, 994, 6, null)->value = -1;
        settings_gallery_put(UI_PROGRESS, 0, 49, 1382, 994, 6, null)->value = UI_ONE / 100 * 80;
        // The pieces, in the grid's cell the demo leaves empty: a heading, then rows that flow left to right (x -1).
        settings_gallery_put(UI_CARD, 0, 736, 654, 332, 507, null);
        settings_gallery_put(UI_HEADING, UI_TITLE_MEDIUM, 761, 679, 282, 24, "Pieces")->role = UI_ON_SURFACE;
        settings_gallery_put(UI_BADGE, 1, 761, 719, 0, 22, "Accent")->role = UI_PRIMARY;
        settings_gallery_put(UI_BADGE, 1, -1, 719, 0, 22, "Healthy")->role = UI_SUCCESS;
        settings_gallery_put(UI_BADGE, 0, -1, 719, 0, 22, "Degraded")->role = UI_WARNING;
        settings_gallery_put(UI_BADGE, 0, 761, 749, 0, 22, "Failed")->role = UI_ERROR;
        settings_gallery_put(UI_BADGE, 0, -1, 749, 0, 22, "Info")->role = UI_INFO;
        settings_gallery_put(UI_BADGE, 0, -1, 749, 0, 22, "Idle")->role = UI_SURFACE_HIGHEST;
        settings_gallery_put(UI_BADGE, 0, -1, 749, 0, 22, "Draft")->role = -1;
        settings_gallery_put(UI_TAG, 0, 761, 787, 0, 26, "riscv64");
        settings_gallery_put(UI_TAG, 0, -1, 787, 0, 26, "kernel")->on = 1;
        settings_gallery_put(UI_KBD, 0, -1, 790, 0, 20, "Ctrl");
        settings_gallery_put(UI_KBD, 0, -1, 790, 0, 20, "K");
        settings_gallery_put(UI_AVATAR, 0, 761, 829, 32, 32, "AL")->on = 1;
        settings_gallery_put(UI_AVATAR, 0, 805, 833, 24, 24, "DL");
        settings_gallery_put(UI_AVATAR, 1, 841, 829, 32, 32, "MW");
        settings_gallery_put(UI_SPINNER, 0, 905, 838, 14, 14, null);
        settings_gallery_put(UI_SPINNER, 0, 931, 835, 20, 20, null);
        settings_gallery_put(UI_SPINNER, 0, 963, 831, 28, 28, null);
        {
                struct ui_control address_to callout = settings_gallery_put(UI_CALLOUT, 0, 761, 877, 282, 70, "Kernel rebuild pending");

                callout->role = UI_WARNING;
                string_copy_bounded((string_address)callout->text, (string_address) "Three nodes wait for it.", sizeof(callout->text));
        }
        {
                struct ui_control address_to code = settings_gallery_put(UI_CODE_WELL, 0, 761, 963, 282, 172, "build.sh");

                code->items[0] = "$ sh build.sh arch/x64";
                code->items[1] = "  kernel   ok";
                code->items[2] = "  shell    ok";
                code->items[3] = "  canvas   ok";
                code->count = 4;
        }
        for (int at = 0; at < settings_gallery.count; at++)
                ui_control_start(&settings_gallery.control[at]);
}

static fn settings_gallery_place(void);

// The page drawn into the rows of the band: the paper, then every control, the scroll taken off their places.
static fn settings_gallery_draw(struct ui address_to ui)
{
        int right = 0, row_y = -1, scroll = settings_gallery.scroll.scroll / ui->scale;

        ui->top = 0;
        ui->bottom = ui->height;
        ui_fill(ui, 0, 0, ui->width, ui->height, ui_role(UI_SURFACE) | 0xff000000u);
        for (int at = 0; at < settings_gallery.count; at++)
        {
                struct ui_control address_to c = &settings_gallery.control[at];
                int y = c->y;

                // A piece that flows: after the last on its row, 8 apart.
                if (c->x < 0 || (c->kind == UI_BADGE || c->kind == UI_TAG || c->kind == UI_KBD))
                {
                        if (c->x < 0 && (row_y == c->y || row_y == c->y - 3 || row_y == c->y + 3))
                                c->x = right + 8;
                        else if (c->x < 0)
                                c->x = 761;
                }
                c->y = y - scroll;
                if (c->y + c->h + 120 > 0 && c->y - 120 < ui->height / ui->scale)
                        ui_control_draw(ui, c);
                c->y = y;
                if (c->kind == UI_BADGE || c->kind == UI_TAG || c->kind == UI_KBD)
                        right = c->x + c->w, row_y = c->y;
        }
        settings_gallery_place();
        ui_scroll_draw(ui, &settings_gallery.scroll);
}

// The page's region: the window, over the whole page's height.
static fn settings_gallery_place(void)
{
        struct ui_scroll address_to s = &settings_gallery.scroll;

        s->x = settings.width - ui_px(&settings.ui, UI_SCROLL_WIDTH), s->y = 0, s->height = s->view = settings.height;
        s->content = ui_px(&settings.ui, SETTINGS_GALLERY_H);
        ui_scroll_set(s, s->scroll);
}

// The pointer, the wheel and the keys on the page: the kit's controls take the pointer, in page pixels; the region the rest.
static b32 settings_gallery_event(const struct window_event address_to event)
{
        struct ui_scroll address_to s = &settings_gallery.scroll;
        int offset = s->scroll / settings.ui.scale;
        b32 moved = false;

        settings_gallery_place();
        if (event->type == WINDOW_EVENT_KEY)
        {
                if ((event->flags & WINDOW_KEY_DOWN) && event->a == SETTINGS_KEY_ESCAPE)
                        return false;
                if ((event->flags & WINDOW_KEY_DOWN) && (event->a == SETTINGS_KEY_DOWN || event->a == SETTINGS_KEY_UP))
                        moved = ui_scroll_set(s, s->scroll + (event->a == SETTINGS_KEY_DOWN ? 1 : -1) * ui_px(&settings.ui, UI_ROW));
        }
        else if (event->type == WINDOW_EVENT_WHEEL)
                moved = !event->b && ui_scroll_wheel(&settings.ui, s, event->a);
        else if (event->type == WINDOW_EVENT_MOTION || event->type == WINDOW_EVENT_BUTTON)
        {
                int y = event->type == WINDOW_EVENT_MOTION ? event->b : settings.page.pointer_y, bar;

                if (event->type == WINDOW_EVENT_BUTTON && event->a != SETTINGS_BUTTON_LEFT)
                        return true;
                if (event->type == WINDOW_EVENT_MOTION)
                        settings.page.pointer_x = event->a, settings.page.pointer_y = event->b;
                bar = ui_scroll_pointer(&settings.ui, s, (int)event->type, event->type == WINDOW_EVENT_MOTION ? event->a : settings.page.pointer_x, y,
                                        (event->flags & WINDOW_KEY_DOWN) != 0);
                if (bar >= 0)
                        moved = ui_scroll_set(s, bar) || true;
                else
                {
                        // The controls are drawn with the scroll taken off; the pointer is put on the page.
                        for (int at = 0; at < settings_gallery.count; at++)
                                settings_gallery.control[at].y -= offset;
                        ui_kit_pointer(&settings.ui, settings_gallery.control, settings_gallery.count, (int)event->type, event->a, y,
                                       (event->flags & WINDOW_KEY_DOWN) != 0, event->flags);
                        for (int at = 0; at < settings_gallery.count; at++)
                                settings_gallery.control[at].y += offset;
                        moved = true;
                }
        }
        if (moved)
                settings_dirty();
        return true;
}

/* ---- the pointer and a finger ---- */

// A press is numbered by what it went down on: a row by its number, a group and a choice of the open menu past them, and the card.
#define SETTINGS_PRESS_GROUP (SETTINGS_ALL + 1)
#define SETTINGS_PRESS_MENU (SETTINGS_PRESS_GROUP + 64)
#define SETTINGS_PRESS_CARD (SETTINGS_PRESS_MENU + 256)

static int settings_press_target(int x, int y)
{
        int at;

        if (settings.menu.owner >= 0)
                return (at = ui_menu_item(&settings.ui, &settings.menu, x, y)) >= 0 ? SETTINGS_PRESS_MENU + at : -1;
        if (settings.view)
                return SETTINGS_PRESS_CARD;
        if ((at = ui_page_column_hit(&settings.ui, &settings.page, x, y)) >= 0)
                return SETTINGS_PRESS_GROUP + at;
        return ui_list_hit(&settings.ui, &settings.list, x, y);
}

// The region under a point, which the wheel turns and a finger drags: the open menu, the card, the column, or the rows.
static struct ui_scroll address_to settings_region(int x, int y)
{
        const struct ui_menu address_to m = &settings.menu;

        if (m->owner >= 0 && x >= m->x && x < m->x + m->w && y >= m->y && y < m->y + m->h)
                return &settings.menu.scroll;
        if (settings.view)
                return &settings.card;
        return x < settings.page.left - ui_px(&settings.ui, UI_COLUMN_GUTTER) / 2 ? &settings.page.column_scroll : &settings.list.scroll;
}

// A region moved: drawn again (the menu keeps the keys' choice in view as it goes).
static fn settings_region_moved(const struct ui_scroll address_to region)
{
        if (region == &settings.menu.scroll)
        {
                settings_menu_place();
                settings_dirty();
        }
        else
                settings_changed(settings.page.top, settings.page.bottom);
}

// A mouse's motion, press or release on a scrollbar (the menu's, the rows' or the card's, the column's): true when it was a bar's.
static b32 settings_bars(const struct window_event address_to event)
{
        struct ui_scroll address_to bars[3] = {&settings.menu.scroll, settings.view ? &settings.card : &settings.list.scroll,
                                               &settings.page.column_scroll};
        b32 taken = false;

        if (event->type == WINDOW_EVENT_BUTTON && event->a != SETTINGS_BUTTON_LEFT)
                return false;
        for (int k = settings.menu.owner >= 0 ? 0 : 1; k < (settings.menu.owner >= 0 ? 1 : settings.view ? 2 : 3); k++)
        {
                b32 lit = bars[k]->hover;
                int scroll = ui_scroll_pointer(&settings.ui, bars[k], (int)event->type, settings.page.pointer_x, settings.page.pointer_y,
                                               (event->flags & WINDOW_KEY_DOWN) != 0);

                if (lit != bars[k]->hover)
                        settings_region_moved(bars[k]);
                if (scroll < 0)
                        continue;
                taken = true;
                ui_scroll_set(bars[k], scroll);
                settings_region_moved(bars[k]);
        }
        return taken;
}

static fn settings_press_at(void);

/*
        A finger: down, what it is on shows pressed and nothing is done yet; past the slop it drags the region it went
        down on, which follows it; up, the press is done where it went down if it did not move off it (as a mouse's
        press is done at once), and the region is thrown if it was moving; a press taken back does nothing.
*/
static fn settings_finger(const struct window_event address_to event)
{
        int x = settings.page.pointer_x, y = settings.page.pointer_y;

        if (event->type == WINDOW_EVENT_MOTION)
        {
                b32 dragged = settings.finger.press.dragged;

                if (ui_finger_move(&settings.ui, &settings.finger, event->a, event->b))
                {
                        if (!dragged && settings.finger.press.dragged)
                        {
                                ui_list_let_go(&settings.ui, &settings.list);
                                settings.menu.pressed = -1;
                        }
                        settings_region_moved(settings.finger.region);
                }
                return;
        }
        if (event->a != SETTINGS_BUTTON_LEFT)
                return;
        if (event->flags & WINDOW_KEY_DOWN)
        {
                int target = settings_press_target(x, y);

                ui_finger_down(&settings.finger, x, y, target, settings_region(x, y));
                // What it is on shows pressed at once: a row, a choice of the menu.
                if (target >= 0 && target < SETTINGS_PRESS_GROUP)
                        settings.list.down = target;
                else if (target >= SETTINGS_PRESS_MENU && target < SETTINGS_PRESS_CARD)
                        settings.menu.pressed = target - SETTINGS_PRESS_MENU;
                settings_dirty();
                return;
        }
        {
                int clicked;

                x = settings.finger.press.x, y = settings.finger.press.y;
                clicked = ui_finger_up(&settings.finger, (event->flags & WINDOW_KEY_CANCEL) != 0);
                ui_list_let_go(&settings.ui, &settings.list);
                settings.menu.pressed = -1;
                settings_dirty();
                // Done where it went down, as a mouse's press is: the target it was shown on is what is under that point.
                if (clicked >= 0 && clicked == settings_press_target(x, y))
                {
                        settings.page.pointer_x = x, settings.page.pointer_y = y;
                        settings.touching = true;
                        settings_press_at();
                        settings.touching = false;
                        ui_list_let_go(&settings.ui, &settings.list);
                        ui_kit_ripples_exit(&settings.page.column);
                }
        }
}

static b32 settings_event(const struct window_event address_to event)
{
        if (settings_gallery.on)
                return settings_gallery_event(event);
        if (event->type == WINDOW_EVENT_MOTION)
                settings.page.pointer_x = event->a, settings.page.pointer_y = event->b;
        // The scrollbars take a mouse's motion, press and release on them (a finger drags the region itself).
        if ((event->type == WINDOW_EVENT_MOTION || event->type == WINDOW_EVENT_BUTTON) && !(event->flags & WINDOW_KEY_TOUCH) &&
            settings_bars(event))
                return true;
        // The filter's field takes a press, a drag and a release over it (a selection by the hand), and the clipboard's answers.
        if (!settings.view && settings.filter_open &&
            (event->type == WINDOW_EVENT_CLIP ||
             ((event->type == WINDOW_EVENT_BUTTON || event->type == WINDOW_EVENT_MOTION) &&
              (settings.filter_edit.dragging ||
               (event->type == WINDOW_EVENT_BUTTON &&
                ui_control_hit(&settings.ui, &settings.page.search, settings.page.pointer_x, settings.page.pointer_y) >= 0)))))
        {
                p32 asks = ui_text_event(&settings.ui, &settings.filter_edit, event, settings.page.pointer_x, settings.page.pointer_y);

                if (asks && settings_filter_take())
                {
                        settings.list.scroll.scroll = 0;
                        settings.pane = 1;
                        settings_refilter();
                }
                settings_dirty();
                return true;
        }
        switch (event->type)
        {
        case WINDOW_EVENT_KEY:
                ui_drag_stop(&settings.finger.drag);
                return settings_key(event);
        case WINDOW_EVENT_MOTION:
                if (event->flags & WINDOW_KEY_TOUCH)
                {
                        settings_finger(event);
                        break;
                }
                // Over the menu the keys' choice follows the pointer; over the rows and the column, what is under it is lit.
                if (settings.menu.owner >= 0)
                {
                        int over = ui_menu_item(&settings.ui, &settings.menu, event->a, event->b);

                        if (over >= 0 && over != settings.menu.at)
                        {
                                settings.menu.at = over;
                                settings_dirty();
                        }
                }
                else if (!settings.view)
                {
                        ui_list_pointer(&settings.ui, &settings.list, WINDOW_EVENT_MOTION, event->a, event->b, false);
                        if (ui_control_hit(&settings.ui, &settings.page.column, event->a, event->b) != settings.page.column.item)
                                settings_changed(settings.page.top, settings.page.bottom);
                }
                break;
        case WINDOW_EVENT_BUTTON:
                if (event->a != SETTINGS_BUTTON_LEFT)
                        break;
                // The release: the column's ripple and the row's go out (a finger's as a mouse's).
                if (!(event->flags & WINDOW_KEY_DOWN))
                {
                        ui_kit_ripples_exit(&settings.page.column);
                        ui_list_pointer(&settings.ui, &settings.list, WINDOW_EVENT_BUTTON, settings.page.pointer_x, settings.page.pointer_y, false);
                        settings_dirty();
                }
                if (event->flags & WINDOW_KEY_TOUCH)
                {
                        settings_finger(event);
                        break;
                }
                if (event->flags & WINDOW_KEY_DOWN)
                {
                        ui_drag_stop(&settings.finger.drag);
                        settings_press_at();
                }
                break;
        case WINDOW_EVENT_WHEEL:
        {
                // The menu turns under the wheel, and nothing else does while it is open; else what is under the pointer.
                struct ui_scroll address_to region = settings.menu.owner >= 0 ? &settings.menu.scroll
                                                                               : settings_region(settings.page.pointer_x, settings.page.pointer_y);

                ui_drag_stop(&settings.finger.drag);
                if (!event->b && ui_scroll_wheel(&settings.ui, region, event->a))
                        settings_region_moved(region);
                break;
        }
        }
        return true;
}

// A press at the pointer, a mouse's at once and a finger's as it comes up where it went down: what it is on is done.
static fn settings_press_at(void)
{
        int x = settings.page.pointer_x, y = settings.page.pointer_y, row, before = settings.list.focus;

        // A press on a choice of the menu sets it; a press anywhere else only closes the menu.
        if (settings.menu.owner >= 0)
        {
                int chosen = ui_menu_item(&settings.ui, &settings.menu, x, y);

                if (chosen >= 0)
                        settings_menu_apply(chosen);
                else
                        settings_menu_close();
                return;
        }
        // A press on the card of what a command said is done with it.
        if (settings.view)
        {
                settings_card_leave();
                return;
        }
        // A press on a group chooses it, rippling it; the keys are then in the column, and Enter goes to its rows.
        row = ui_page_column_hit(&settings.ui, &settings.page, x, y);
        if (row >= 0)
        {
                ui_page_column_press(&settings.ui, &settings.page, row, x, y);
                settings_edit_end();
                settings.pane = 0;
                settings_group_choose(row);
                settings_dirty();
                return;
        }
        // A row: the kit gives it the keys, shows it pressed and ripples it from the press.
        row = ui_list_pointer(&settings.ui, &settings.list, WINDOW_EVENT_BUTTON, x, y, true);
        if (settings.editing >= 0 && row != settings.editing)
                settings_edit_end();
        if (row < 0)
                return;
        if (row != before)
        {
                settings.confirm = 0;
                settings_said_of(before);
        }
        settings.pane = 1;
        settings_dirty();
        // A segment or a swatch is set where it is pressed.
        if (settings.list.part >= 0 && row < SETTINGS_ANY && settings.state[row] == SETTINGS_READ)
        {
                char word[SETTINGS_VALUE];

                if (settings_row_is(row, SETTING_ACCENT_PATH) && settings.list.part < (int)(sizeof(settings_accents) / sizeof(settings_accents[0])))
                        settings_change(row, settings_accents[settings.list.part].name);
                else if (settings_word(settings_rows[row].choices, (positive)settings.list.part, word, sizeof(word)))
                        settings_change(row, word);
                return;
        }
        if (settings.editing != row)
                settings_activate(row, settings.list.way);
}

/* ---- the picture ---- */

// One line of the card.
static const char address_to settings_card_line(int at, char address_to into, positive room)
{
        positive size = settings_large->card_size[at];

        size = size >= room ? room - 1 : size;
        memory_copy(into, settings_large->card + settings_large->card_at[at], size);
        into[size] = 0;
        return into;
}

/*
        The picture drawn into pixels, every row of it that lies in the band [band_top, band_bottom): the kit's page
        (its title, the search field that shows the filter, the status line), then the column of groups and the rows of
        the group chosen, or the card, and over them the menu a choice opened. settings_paint calls it only for a band
        with rows in it.
*/
static fn settings_draw_rows(unsigned int address_to pixels, int band_top, int band_bottom)
{
        struct ui address_to ui = &settings.ui;

        ui->pixels = pixels;
        ui->pitch = settings.own.table->stride;
        ui->width = settings.width;
        ui->height = settings.height;
        ui->top = 0;
        ui->bottom = settings.height;
        ui->band_top = band_bottom > band_top ? band_top : 0;
        ui->band_bottom = band_bottom > band_top ? band_bottom : 0;
        if (settings_gallery.on)
        {
                settings_gallery_draw(ui);
                return;
        }
        settings.page.title = settings.view ? settings.card_title : "Settings";
        settings.page.status = settings.status;
        // The search field is the filter: the kit's bare field, the text engine's field in it (typing, a selection, the
        // clipboard, its carets), with the keys while no row is typed in.
        settings.page.search.items[0] = settings.view ? null : "Type to filter";
        settings.page.search.edit = settings.view ? null : settings_filter_field();
        ui_page_draw(ui, &settings.page, settings.filter, settings.editing < 0 && (settings.filter_length || settings.focused));
        if (settings.view)
                ui_lines_draw(ui, &settings.card, ui_px(ui, UI_PAD), settings.card_lines, settings_card_line);
        else
        {
                ui_page_groups(ui, &settings.page, settings_group_name, settings_groups());
                ui_page_column(ui, &settings.page, settings.group, settings.focused && !settings.pane, settings.filter_length != 0);
                settings.list.lit = settings.pane || settings.filter_length;
                settings.list.keys = settings.focused && settings.pane;
                ui_list_draw(ui, &settings.list);
        }
        if (settings.menu.owner >= 0 && settings_menu_place())
                ui_menu_draw(ui, &settings.menu);
}

/*
        The picture in the buffer slot, redrawn in the rows it lacks: those
        that changed since it was last drawn (the frame before, or the one
        before that for the other buffer) and those the compositor spoiled in
        it. Answers whether the whole of it was drawn, and the rows drawn,
        [top, bottom): the damage the compositor reads again.
*/
static b32 settings_paint(unsigned int address_to pixels, int slot, int spoiled_top, int spoiled_bottom, int address_to top,
                          int address_to bottom)
{
        int drawn_top = settings.stale_top[slot], drawn_bottom = settings.stale_bottom[slot], other = 1 - slot;
        b32 whole = settings.stale_all[slot] || settings.change_all;

        settings_rows_add(&drawn_top, &drawn_bottom, settings.change_top, settings.change_bottom);
        settings_rows_add(&drawn_top, &drawn_bottom, spoiled_top, spoiled_bottom);
        if (whole)
        {
                drawn_top = 0;
                drawn_bottom = settings.height;
        }
        drawn_top = drawn_top < 0 ? 0 : drawn_top;
        drawn_bottom = drawn_bottom > settings.height ? settings.height : drawn_bottom;
        if (drawn_bottom > drawn_top)
                settings_draw_rows(pixels, drawn_top, drawn_bottom);
        else
                drawn_top = drawn_bottom = 0;

        // The buffer is the picture now; the other lacks what changed since it was last drawn, which is what changed now.
        settings.stale_top[slot] = settings.stale_bottom[slot] = 0;
        settings.stale_all[slot] = false;
        settings.stale_top[other] = settings.change_top;
        settings.stale_bottom[other] = settings.change_bottom;
        settings.stale_all[other] = settings.change_all;
        settings.change_top = settings.change_bottom = 0;
        settings.change_all = false;
        *top = drawn_top;
        *bottom = drawn_bottom;
        return whole;
}

// One picture, into the buffer the compositor is not reading; false when there is none yet.
static b32 settings_frame(void)
{
        struct windows address_to table = settings.own.table;
        unsigned int row = settings.own.row;
        int slot = settings.shown < 0 ? 0 : 1 - settings.shown, spoiled_x, spoiled_y, spoiled_width, spoiled_height;
        int spoiled_top = 0, spoiled_bottom = 0, top, bottom;
        b32 whole;

        if (settings.shown >= 0 && !windows_ready(&settings.own))
                return false;
        if (windows_spoiled(&settings.own, (unsigned int)slot, &spoiled_x, &spoiled_y, &spoiled_width, &spoiled_height))
        {
                spoiled_top = spoiled_y;
                spoiled_bottom = spoiled_y + spoiled_height;
        }
        whole = settings_paint(windows_buffer(table, row, (unsigned int)slot), slot, spoiled_top, spoiled_bottom, &top, &bottom);
        table->width[row] = (unsigned int)settings.width;
        table->height[row] = (unsigned int)settings.height;
        if (whole)
                table->damage_width[row] = 0;
        else
        {
                table->damage_x[row] = 0;
                table->damage_y[row] = top;
                table->damage_width[row] = settings.width;
                table->damage_height[row] = bottom - top;
        }
        windows_show(&settings.own, (unsigned int)slot);
        settings.shown = slot;
        settings.dirty = false;
        return true;
}

/* ---- the program ---- */

// The size the window is drawn at: its own, or what the compositor asked, never past the output.
static fn settings_size(unsigned int width, unsigned int height)
{
        struct windows address_to table = settings.own.table;

        settings.width = (int)(width > table->output_width ? table->output_width : width);
        settings.height = (int)(height > table->output_height ? table->output_height : height);
        ui_scratch_fit(&settings.ui, (unsigned int)settings.width);
        if (settings.view)
                settings_wrap();
        settings_layout();
        settings_side_reveal();
        // An open menu shows as many choices as the window holds now, placed again for it.
        if (settings.menu.owner >= 0)
                settings_menu_place();
        settings_dirty();
}

// The settings' start, once its window is taken: the lift, the layout and the first picture. -1 runs the loop.
static int settings_open(void)
{
        struct windows address_to table = settings.own.table;
        unsigned int row = settings.own.row;

        if (!settings_gallery.on && ui_lift_other(&settings.own, (string_address) "settings", false))
                return 0;

        settings_refilter();
        if (settings_gallery.on)
        {
                int width = ui_px(&settings.ui, SETTINGS_GALLERY_W), height = ui_px(&settings.ui, SETTINGS_GALLERY_H);
                int room = (int)table->output_height - ui_px(&settings.ui, 2 * (24 + 24));

                settings_gallery_build();
                settings_size((unsigned int)width, (unsigned int)(height < room ? height : room > 0 ? room : height));
                ui_centre(table, row, settings.width, settings.height, 0);
                settings_frame();
                return -1;
        }
        {
                int width = ui_px(&settings.ui, SETTINGS_WIDTH), height = ui_px(&settings.ui, SETTINGS_HEIGHT);
                int room = (int)table->output_height - ui_px(&settings.ui, 2 * (24 + 24));

                settings_size((unsigned int)width, (unsigned int)(height < room ? height : room > 0 ? room : height));
                ui_centre(table, row, settings.width, settings.height, ui_px(&settings.ui, 12));
        }
        settings_say((string_address) "");

        settings_ask_all();
        settings.help = 1;
        settings_next_job();
        settings_frame();
        return -1;
}

// The settings' timers: a verb's time, the due reading of a feed, and the descriptor of a verb running. 0 runs the loop again.
static long settings_tick(long timeout, long address_to other)
{
        // A region thrown by a finger moves a frame at a time until it is still.
        {
                b32 moved;
                long thrown = ui_finger_throw(&settings.finger, &moved);

                if (moved)
                        settings_region_moved(settings.finger.region);
                if (thrown >= 0 && (timeout < 0 || thrown < timeout))
                        timeout = thrown;
        }
        if (settings.child > 0 && !(settings.job_sets && settings.job >= SETTINGS_ANY))
        {
                positive now = clock_monotonic_nanoseconds(), left;
                positive most = settings.job == SETTINGS_JOB_FEED  ? (positive)SETTINGS_FEED_WAIT_NS
                                : settings.job == SETTINGS_JOB_ACT ? (positive)SETTINGS_ACT_WAIT_NS
                                                                   : (positive)SETTINGS_WAIT_NS;

                if (now - settings.began >= most)
                {
                        settings_finished(true);
                        return 0;
                }
                left = most - (now - settings.began);
                timeout = timeout < 0 || (positive)timeout > left ? (long)left : timeout;
        }
        else if (settings.child <= 0)
        {
                // What the group shown keeps up with, when it is due; otherwise how long till it is.
                long wait = settings_feed_tick();

                if (wait >= 0 && (timeout < 0 || wait < timeout))
                        timeout = wait;
                if (settings.child > 0)
                        return 0;
        }
        *other = settings.child > 0 ? settings.output : -1;
        return timeout;
}

static fn settings_ready(void)
{
        settings_drain();
}

// A verb still running when the window closes is stopped.
static fn settings_close(void)
{
        if (settings.child > 0)
        {
                positive status = 0;

                system_call_2(syscall(kill), (positive)settings.child, SIGKILL);
                system_wait4_retry(settings.child, address_of status, 0, null);
        }
}

// The kit's controls moved this frame: their rows are drawn again at the next (the whole picture while the theme turns).
static fn settings_moving(int top, int bottom, b32 all)
{
        if (all)
                settings_dirty();
        else if (bottom > top)
                settings_changed(top, bottom);
}

static struct ui_app settings_app = {
        .own = &settings.own,
        .ui = &settings.ui,
        .missing = (string_address) "settings: no window: the compositor's desktop is not up\n",
        .title = (string_address) "Settings",
        .name = (string_address) "settings",
        .glass = 0, // the kit's surface: the window is the theme's, opaque, as every kit window is
        .font = null,
        .nofont = (string_address) "settings: no font to draw with (-f FONT.ttf), pid ",
        .dirty = &settings.dirty,
        .focused = &settings.focused,
        .ground = &settings.ground,
        .open = settings_open,
        .frame = settings_frame,
        .event = settings_event,
        .changed = settings_dirty,
        .moving = settings_moving,
        .size = settings_size,
        .tick = settings_tick,
        .ready = settings_ready,
        .close = settings_close,
};

static b32 settings_main()
{
        memory_zero(&settings, sizeof(settings));
        settings.moonwater = (string_address) "/moonwater";
        settings.editing = -1;
        settings.shown = -1;
        settings.output = -1;
        settings.pane = 1; // the keys start in the rows, on the first of them
        settings.menu.owner = -1;
        settings.change_all = settings.stale_all[0] = settings.stale_all[1] = true;

        for (positive at = 1; at + 1 < (positive)program_argument_count(); at++)
        {
                string_address value;

                if ((value = ui_argument(at, (string_address) "-f")))
                        settings_app.font = value;
                else if ((value = ui_argument(at, (string_address) "-b")))
                        settings_app.bold = value;
                else if ((value = ui_argument(at, (string_address) "-m")))
                        settings.moonwater = value;
                else
                        continue;
                at++;
        }
        // -g: the kit's gallery, on its own paper and not glass, with the mono for its tags and code.
        for (positive at = 1; at < (positive)program_argument_count(); at++)
                if (string_equals(program_argument(at), (string_address) "-g"))
                {
                        settings_gallery.on = 1;
                        settings_app.glass = 0;
                        settings_app.mono = 1;
                        settings_app.title = (string_address) "Gallery";
                        settings_app.name = (string_address) "gallery";
                }

        if (!settings_map())
        {
                compositor_kmsg((string_address) "settings: no memory, pid ",
                                (positive)compositor_call(syscall(getpid), 0, 0, 0, 0, 0, 0), (string_address) "");
                return 1;
        }

        return ui_app_run(&settings_app);
}

#endif // MOONWATER_CANVAS_SETTINGS
