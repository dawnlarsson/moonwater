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
        keys repeat, the time zone, how Canvas is drawn (its scale, the
        desktop's colour, the size and look of the pointer and
        whether a shake of the hand makes it large, and for how long), the
        mouse's sensitivity and acceleration, the clock and where the network sets it from, the
        wired link, the radios and airplane mode, which link the internet
        prefers, the servers names and time are asked of, the power profile,
        the processor and the battery, and what the machine starts at boot.
        The Display group is the screens: their modes, where they are
        arranged and the backlight. A row the machine has nothing for (no
        backlight, no boost switch) says so and does nothing. Nothing in those rows ends
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
        Escape closes the window, as Alt-F4 and its close button do. A
        character typed with no row typed in goes to the search field, the
        filter: the rows that have each of its words, in any order, are the
        ones listed, from every group, with the keys on the first of them.
        While it holds something, every key goes to it first, as in any of
        the kit's fields (the caret by the arrows, Home and End, a selection
        by Shift, words by Control, Backspace and Delete, a blank, undo, the
        clipboard), and what it does not take is the rows': Up and Down move
        in them, Enter works the row the keys are on, and Escape clears it
        before it closes the window. Choosing another group clears it too.
        Every field (the search field, and the field a row is typed in) is
        the kit's: the hand puts its caret, a drag selects, a double click a
        word and a triple the line, the middle button pastes and the right
        button opens its menu. With the mouse, a press on a group chooses
        it, a press on a row is Enter on it, and the wheel scrolls what is
        under the pointer.

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
#define SETTINGS_FILTER 64   // what the filter holds, in bytes: its field holds no more
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
#define SETTINGS_JOB_IDENTIFY -5 // the map's button: canvas identify, which is no row of the list

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

// The rows to ask are a bit each, SETTINGS_UNREAD_WORDS words of 64 bits.
#define SETTINGS_UNREAD_WORDS 2
_Static_assert(sizeof(settings_rows) / sizeof(settings_rows[0]) <= 64 * SETTINGS_UNREAD_WORDS, "the rows to ask fit their words");

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
                string_copy_bounded((string_address)value, (string_address)WORDS(settings_unknown), room);
        else
                language_format(value, room, WORDS(settings_percent), (long)battery.percent);
        if (battery.charging == BATTERY_CHARGING)
                string_append_bounded((string_address)value, (string_address)WORDS(settings_battery_charging), room);
        else if (battery.charging == BATTERY_DISCHARGING)
                string_append_bounded((string_address)value, (string_address)WORDS(settings_battery_discharging), room);
        else if (battery.charging == BATTERY_FULL)
                string_append_bounded((string_address)value, (string_address)WORDS(settings_battery_full), room);
        else if (battery.charging == BATTERY_NOT_CHARGING)
                string_append_bounded((string_address)value, (string_address)WORDS(settings_battery_idle), room);
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

// The map of the screens above the Display rows (settings_map_*): the screen the rows are for, a drag of one screen (applied
// on release), and the identify button. The positions are the window's device pixels, set by settings_map_place.
struct settings_map
{
        int display;                            // the table's index of the screen the rows are for, -1 none chosen
        int drag;                               // the table's index of the screen being dragged, -1 none
        int grab_x, grab_y;                     // where on the screen the drag took hold, in table pixels
        int target_x, target_y;                 // where a screen dragged now would be let go at, snapped
        b32 keys;                               // the keyboard is on the map (Up from the first Display row): its arrows choose and move a screen
        b32 nudged;                             // a screen was last sent by a key to nudge_x, nudge_y, the table not caught up with it yet
        int nudge_at, nudge_x, nudge_y;         // the screen that was sent, and where it was sent to
        int x, y, w, h;                         // the panel: its top left and size
        int field_x, field_y, field_w, field_h; // the screens' area, under the panel's top strip
        struct ui_control identify;             // the button on the strip
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
        struct ui_scroll card;
        char head[SETTINGS_VALUE]; // the verb of a command being typed, as its row's label
        char accents[SETTINGS_VALUE]; // the accents' colours, a blank apart, as the swatches' words
        struct battery_name battery_menu[BATTERY_LIST]; // the batteries a picker list was opened with
        positive battery_menu_count;
        // The filter as typed, in a field of the text engine, which is the page's search field (its key is the kit's).
        struct ui_text filter_edit;
        b32 filter_open;
        // The field a row is typed in (the engine's, with the row's rules: a name's characters, a passphrase's dots, the
        // room its value has), open while editing is a row; and the context menu of either field.
        struct ui_text edit;
        char edit_from[SETTINGS_VALUE];
        struct ui_popup popup;
        char status[SETTINGS_STATUS];
        int width, height;
        unsigned int ground;
        b32 focused;

        // What a command said, as a card in place of the list: its text and its lines cut to the window's width (the kit's).
        struct ui_lines card_text;
        char card_title[SETTINGS_STATUS];

        // The press again to confirm (the kit's): a command that asks for a second Enter (its row, and what was typed), a forget.
        struct ui_confirm confirm;

        // The verb being run: its process, the read end of its output, what it said, when it began, for which row.
        bipolar child;
        b32 output;
        positive said_length, began;
        int job;
        b32 job_sets;
        char job_value[SETTINGS_TYPED];
        positive unread[SETTINGS_UNREAD_WORDS]; // rows to ask, a bit each
        struct settings_change queue[SETTINGS_QUEUE]; // changes to make once the verb running is done, in order
        positive queued;
        b32 told;         // the first reading is said in the machine's log

        struct settings_map map;
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

// Rows [top, bottom) of the list changed (the list's damage hook, which the kit calls for its rows): the page's rows are drawn again.
static fn settings_changed(int top, int bottom)
{
        ui_page_damage(&settings.page, top, bottom);
}

/* ---- the issue board: each row's badge and the lines under it (window.c's windows_issue_*) ---- */
static int settings_section_start(int row);
static fn settings_rebuild(void);
static fn settings_say(string_address text);
/*
        A row whose option has issues on the board (its key is the row's verb, "canvas gpu") shows a badge at its right, the worst severity,
        and the lines under it: one for each issue, its message and then its detail (the kernel's words, copyable as text). The GPU's row
        also has a status line first, what the card does now (windows_gpu_judge, from the table's counters over the last ten seconds).
        The board is read once a second while Settings is open, and the lines are made again when what they say changed. Each line is a
        list row of its own, numbered after the rows, so the kit lays them out, scrolls them and moves the rows under them.
*/
#define SETTINGS_SUBS 40           // the lines under the rows at most
#define SETTINGS_SUB_TEXT 240      // bytes of one line
#define SETTINGS_BOARD_NS 1000000000ll
#define SETTINGS_GPU_SAMPLES 10    // samples of the GPU's presents the status is judged on (a second each)
struct settings_sub
{
        int section;                 // the section of the row it is under
        b32 detail;                  // a detail line: quieter
        const char address_to words; // what its badge says (Warning, Error), or none
        unsigned int code;           // a message line's issue code (a kernel notice's, for Delete to dismiss it), 0 for none
        unsigned int action;         // SETTINGS_SUB_COPY (the default), or SETTINGS_SUB_DISMISS_ALL (the line that clears every kernel notice)
        int owner;                   // the row the line is under (its fold: how much of a row the line shows while the row opens or shuts)
        char text[SETTINGS_SUB_TEXT];
};
#define SETTINGS_SUB_COPY 0u
#define SETTINGS_SUB_DISMISS_ALL 1u
static struct settings_sub settings_sub[SETTINGS_SUBS];
static int settings_sub_count, settings_sub_owner;
static struct windows_issues settings_board;
static unsigned char settings_expanded[SETTINGS_ALL]; // each row's expander: 1 when the lines under it show (the GPU's, or a row with an issue)
// Each row's fold: how far its lines are open, Q30 (0 shut, UI_ONE open), moved by its own spring from where it was when it was turned, so
// a turn in the middle of a fold carries on from there. settings_fold_live is 1 while that spring moves.
static b64 settings_fold_at[SETTINGS_ALL];
static struct ui_spring settings_fold_spring[SETTINGS_ALL];
static unsigned char settings_fold_live[SETTINGS_ALL];
static positive settings_board_due;
static char settings_status[SETTINGS_SUB_TEXT], settings_metric_line[SETTINGS_SUB_TEXT];
static char settings_notices_text[SETTINGS_SUB_TEXT];
static unsigned int settings_gpu_status = WINDOWS_GPU_STATUS_NOT, settings_gpu_at, settings_gpu_drawn, settings_gpu_processor;
static unsigned int settings_gpu_ring_drawn[SETTINGS_GPU_SAMPLES], settings_gpu_ring_processor[SETTINGS_GPU_SAMPLES];
static unsigned int settings_gpu_shown_total;
// The status line's key: its status, whether any present was counted, and the card's share of them. The line says counts, and every present moves
// them, so the page is made again when the key moves, not when a count does (a page made again presents, and the counts would move again).
static unsigned int settings_gpu_key = 0xffffffffu;
#define SETTINGS_METRIC_NS 10000000000ll // the metric line makes the page again at most this often (the board's own time)
static positive settings_metric_at;

// The key a row's issues are filed under: its verb's words, joined with a blank (canvas gpu). The key is empty for a row that has no verb.
static b32 settings_key_of(int row, char address_to key, positive room)
{
        const struct settings_row address_to which = row >= 0 && row < SETTINGS_ANY ? &settings_rows[row] : null;

        key[0] = 0;
        // The kernel's notices row has no verb: its key is the one the feeder files them under.
        if (which && which->output == SETTINGS_OUT_NOTICES)
        {
                string_copy_bounded((string_address)key, (string_address)"system notices", room);
                return true;
        }
        if (!which || !which->read[0])
                return false;
        for (positive k = 0; k < SETTING_WORDS && which->read[k]; k++)
        {
                if (k)
                        string_append_bounded((string_address)key, (string_address)" ", room);
                string_append_bounded((string_address)key, (string_address)which->read[k], room);
        }
        return true;
}

// The row whose lines ARE its content: the kernel's notices list under their row all the time, with no chevron and no fold (nothing to open).
static b32 settings_always_open(int row)
{
        return row >= 0 && row < SETTINGS_ANY && settings_rows[row].output == SETTINGS_OUT_NOTICES;
}

// The expander a row has (UI_ROW_EXPANDER_*), or none: a row whose key has an issue on the board, and the GPU row always (its status and its
// metrics are under it). The row's own state is settings_open.
static int settings_expander(int row)
{
        char key[WINDOWS_ISSUE_KEY];

        if (settings_always_open(row) || row < 0 || row >= SETTINGS_ANY || !settings_key_of(row, key, sizeof(key)))
                return 0;
        if (!windows_issues_worst(&settings_board, key, null) && !string_equals((string_address)key, (string_address)WINDOWS_KEY_GPU))
                return 0;
        return settings_expanded[row] ? UI_ROW_EXPANDER_OPEN : UI_ROW_EXPANDER_SHUT;
}

// Opens or shuts a row's lines, and lists them again. They fold on the kit's spring (settings_fold_step moves them frame by frame); with
// reduced motion they are at their place at once.
static fn settings_toggle(int row)
{
        b64 target;

        if (row < 0 || row >= SETTINGS_ANY)
                return;
        settings_expanded[row] = !settings_expanded[row];
        target = settings_expanded[row] ? UI_ONE : 0;
        if (ui_kit.motion)
                settings_fold_at[row] = target, settings_fold_live[row] = 0;
        else
        {
                // The spring's displacement from its target: it starts where the fold is now and comes to rest at the target.
                ui_spring_go(&settings_fold_spring[row], settings_fold_at[row] - target, 0);
                settings_fold_live[row] = 1;
        }
        settings_rebuild();
}

// A line under a row is copied to the clipboard as its text (a press on it): the message, or the detail, as it stands.
static void settings_copy(int at)
{
        if (at < 0 || at >= settings_sub_count)
                return;
        windows_clip_offer(&settings.own, WINDOWS_CLIP_CLIPBOARD, settings_sub[at].text, string_length((string_address)settings_sub[at].text));
        settings_say(WORDS(settings_copied));
}

// The row's badge, when the board has an issue under its key: the worst severity, its words.
static void settings_badge(int row, struct ui_row address_to out)
{
        char key[WINDOWS_ISSUE_KEY];
        unsigned int worst;

        if (!settings_key_of(row, key, sizeof(key)))
                return;
        worst = windows_issues_worst(&settings_board, key, null);
        if (!worst)
                return;
        out->badge = worst == WINDOWS_ISSUE_ERROR ? UI_ROW_BADGE_ERROR : UI_ROW_BADGE_WARNING;
        out->badge_words = worst == WINDOWS_ISSUE_ERROR ? WORDS(settings_badge_error) : WORDS(settings_badge_warning);
}

// The GPU's status from the table, judged on the presents of the last samples: the card's drawn and the processor's, each read as the
// difference from the last sample (a table that counts down is read as none), and the words it says.
static void settings_gpu_sample(void)
{
        struct windows address_to table = settings.own.table;
        unsigned int drawn = 0, processor = 0;

        if (!table)
                return;
        settings_gpu_ring_drawn[settings_gpu_at] = table->gpu_drawn >= settings_gpu_drawn ? table->gpu_drawn - settings_gpu_drawn : 0;
        settings_gpu_ring_processor[settings_gpu_at] = table->gpu_processor >= settings_gpu_processor ? table->gpu_processor - settings_gpu_processor : 0;
        settings_gpu_drawn = table->gpu_drawn, settings_gpu_processor = table->gpu_processor;
        settings_gpu_at = (settings_gpu_at + 1) % SETTINGS_GPU_SAMPLES;
        for (positive k = 0; k < SETTINGS_GPU_SAMPLES; k++)
                drawn += settings_gpu_ring_drawn[k], processor += settings_gpu_ring_processor[k];
        settings_gpu_status = windows_gpu_judge(table, settings_gpu_status, drawn, processor);
        settings_gpu_shown_total = drawn + processor;
        language_format(settings_metric_line, sizeof(settings_metric_line), WORDS(settings_metrics), (long)table->metric_card_mean, (long)table->metric_card_worst,
                        (long)table->metric_processor_mean, (long)table->metric_processor_worst, (long)table->metric_maps, (long)table->metric_wakes,
                        (long)table->metric_presents, (long)table->metric_lists);
        switch (settings_gpu_status)
        {
        case WINDOWS_GPU_STATUS_OFF: language_format(settings_status, sizeof(settings_status), WORDS(settings_status_off)); break;
        case WINDOWS_GPU_STATUS_NONE: language_format(settings_status, sizeof(settings_status), WORDS(settings_status_none)); break;
        case WINDOWS_GPU_STATUS_IN_USE:
        case WINDOWS_GPU_STATUS_PARTLY:
        case WINDOWS_GPU_STATUS_NOT:
                if (!settings_gpu_shown_total)
                {
                        language_format(settings_status, sizeof(settings_status), WORDS(settings_status_waiting));
                        break;
                }
                language_format(settings_status, sizeof(settings_status),
                                settings_gpu_status == WINDOWS_GPU_STATUS_IN_USE ? WORDS(settings_status_in_use)
                                : settings_gpu_status == WINDOWS_GPU_STATUS_PARTLY ? WORDS(settings_status_partly) : WORDS(settings_status_not),
                                (long)drawn, (long)(drawn + processor), (long)(drawn * 100 / (drawn + processor)));
                break;
        }
        settings_gpu_key = settings_gpu_status;
        if (settings_gpu_status == WINDOWS_GPU_STATUS_IN_USE || settings_gpu_status == WINDOWS_GPU_STATUS_PARTLY || settings_gpu_status == WINDOWS_GPU_STATUS_NOT)
                settings_gpu_key |= settings_gpu_shown_total ? 1u << 8 | (drawn * 100 / settings_gpu_shown_total) << 9 : 0u;
}

// Adds a line under the row being listed: its text (a message, a detail or a status), at list position count. Returns the count.
static int settings_sub_put(int section, b32 detail, const char address_to words, const char address_to text, int count)
{
        struct settings_sub address_to sub;

        if (settings_sub_count >= SETTINGS_SUBS || count >= UI_LIST_MOST - 1)
                return count;
        sub = &settings_sub[settings_sub_count];
        sub->section = section, sub->detail = detail, sub->words = words, sub->code = 0, sub->action = SETTINGS_SUB_COPY, sub->owner = settings_sub_owner;
        string_copy_bounded((string_address)sub->text, (string_address)text, sizeof(sub->text));
        settings.list.id[count] = (unsigned short)(SETTINGS_ALL + (positive)settings_sub_count++);
        return count + 1;
}

// The words of a prerequisite of the GPU path that failed (gpu.c's enum gpu_req, the step the board's a number is): the fix, in the reader's
// language. None for a step this version does not name.
static const char address_to settings_req_words(unsigned int step)
{
        switch (step)
        {
        case 1: return WORDS(settings_req_node);
        case 2: return WORDS(settings_req_driver);
        case 3: return WORDS(settings_req_family);
        case 4: return WORDS(settings_issue_gpu_userptr);
        case 5: return WORDS(settings_req_context);
        case 6: return WORDS(settings_req_ring);
        case 7: return WORDS(settings_req_selftest);
        case 8: return WORDS(settings_req_udmabuf);
        case 9: return WORDS(settings_req_prime);
        default: return null;
        }
}

// The text of an issue's message, in the reader's language: its code's words, with its numbers, or the words that say a code this version
// does not name.
static void settings_issue_text(const struct windows_issue address_to issue, char address_to into, positive room)
{
        switch (issue->code)
        {
        case WINDOWS_ISSUE_GPU_REQUIRES:
                if (settings_req_words(issue->a))
                        language_format(into, room, settings_req_words(issue->a));
                else
                        language_format(into, room, WORDS(settings_issue_unknown), (long)issue->code);
                break;
        case WINDOWS_ISSUE_GPU_MAP: language_format(into, room, WORDS(settings_issue_gpu_map), (long)issue->a, (long)issue->b); break;
        case WINDOWS_ISSUE_GPU_USERPTR: language_format(into, room, WORDS(settings_issue_gpu_userptr)); break;
        case WINDOWS_ISSUE_GPU_REFUSED: language_format(into, room, WORDS(settings_issue_gpu_refused), (long)issue->a, issue->detail); break;
        case WINDOWS_ISSUE_GPU_LISTS: language_format(into, room, WORDS(settings_issue_gpu_lists)); break;
        case WINDOWS_ISSUE_GPU_COOLING: language_format(into, room, WORDS(settings_issue_gpu_cooling), (long)issue->a); break;
        case WINDOWS_ISSUE_GPU_UNTRUSTED: language_format(into, room, WORDS(settings_issue_gpu_untrusted), (long)issue->a); break;
        case WINDOWS_ISSUE_GPU_FAMILY: language_format(into, room, WORDS(settings_issue_gpu_family), (long)issue->a); break;
        case WINDOWS_ISSUE_GPU_FAULT: language_format(into, room, WORDS(settings_issue_gpu_fault), issue->detail); break;
        case WINDOWS_ISSUE_GPU_HUNG: language_format(into, room, WORDS(settings_issue_gpu_hung)); break;
        case WINDOWS_ISSUE_GPU_LOST: language_format(into, room, WORDS(settings_issue_gpu_lost), (long)issue->a, (long)issue->b); break;
        case WINDOWS_ISSUE_GPU_SCANOUT: language_format(into, room, WORDS(settings_issue_gpu_scanout), (long)issue->a); break;
        case WINDOWS_ISSUE_AUDIO_OPEN: language_format(into, room, WORDS(settings_issue_audio_open), (long)issue->a); break;
        case WINDOWS_ISSUE_AUDIO_RATE: language_format(into, room, WORDS(settings_issue_audio_rate), (long)issue->a, (long)issue->b); break;
        // A kernel notice's words are the kernel's own, as its line says them (not translated yet: the detail is English).
        case WINDOWS_ISSUE_KERNEL_GPU:
        case WINDOWS_ISSUE_KERNEL_USB:
        case WINDOWS_ISSUE_KERNEL_WIFI:
        case WINDOWS_ISSUE_KERNEL_BLUETOOTH:
        case WINDOWS_ISSUE_KERNEL_AUDIO:
        case WINDOWS_ISSUE_KERNEL_STORAGE:
        case WINDOWS_ISSUE_KERNEL_SYSTEM:
        case WINDOWS_ISSUE_KERNEL_PROGRAM:
        case WINDOWS_ISSUE_RADIO: string_copy_bounded((string_address)into, (string_address)issue->detail, room); break;
        default: language_format(into, room, WORDS(settings_issue_unknown), (long)issue->code); break;
        }
}

// The lines under row, listed after it from position count: the GPU's status first, then each issue under its key, its message and its
// detail when it has one. Returns the new count.
static int settings_sub_rows(int row, int count)
{
        char key[WINDOWS_ISSUE_KEY], text[SETTINGS_SUB_TEXT];
        const struct settings_row address_to which = row < SETTINGS_ANY ? &settings_rows[row] : null;
        int section;

        // The lines show when the row is open, and while it shuts (they fold away on the spring): each issue's message and its detail, and the
        // GPU's status and metrics first.
        if (!settings_key_of(row, key, sizeof(key)) ||
            (!settings_always_open(row) && (!(settings_expanded[row] || settings_fold_at[row]) || !settings_expander(row))))
                return count;
        settings_sub_owner = row;
        section = settings_section_start(row);
        if (string_equals((string_address)key, (string_address)WINDOWS_KEY_GPU) && which && which->kind == SETTINGS_SWITCH)
        {
                string_copy_bounded((string_address)text, (string_address)settings_status, sizeof(text));
                count = settings_sub_put(section, false, null, text, count);
                // The card's and the processor's presents, the maps and the wakes, as the table counts them (one line, a second old at most).
                string_copy_bounded((string_address)text, (string_address)settings_metric_line, sizeof(text));
                count = settings_sub_put(section, true, null, text, count);
        }
        for (int i = 0; i < WINDOWS_ISSUES && count < UI_LIST_MOST - 1; i++)
        {
                const struct windows_issue address_to issue = &settings_board.issue[i];

                if (!issue->used || !windows_issue_same(issue->key, key))
                        continue;
                settings_issue_text(issue, text, sizeof(text));
                count = settings_sub_put(section, false, issue->severity == WINDOWS_ISSUE_ERROR ? WORDS(settings_badge_error) : WORDS(settings_badge_warning),
                                         text, count);
                if (settings_sub_count > 0)
                        settings_sub[settings_sub_count - 1].code = issue->code;
                // The detail of a prerequisite is its fix, which the message already says in the reader's language: no English copy of it.
                if (issue->detail[0] && issue->code != WINDOWS_ISSUE_GPU_REQUIRES && (issue->code < WINDOWS_ISSUE_KERNEL_GPU || issue->code > WINDOWS_ISSUE_RADIO))
                        count = settings_sub_put(section, true, null, issue->detail, count);
        }
        // The kernel's notices row lists them with one line that dismisses every kernel notice, after the last of them.
        if (which && which->output == SETTINGS_OUT_NOTICES && count > 0 && settings_sub_count > 0)
        {
                count = settings_sub_put(section, false, null, WORDS(settings_notices_dismiss), count);
                if (settings_sub_count > 0)
                        settings_sub[settings_sub_count - 1].action = SETTINGS_SUB_DISMISS_ALL;
        }
        return count;
}

// A line under a row, described for the kit: indented, its text quiet, and its badge's words at the right when it is an issue.
static fn settings_sub_describe(int at, struct ui_row address_to row)
{
        const struct settings_sub address_to sub = &settings_sub[at];

        row->sub = 1, row->section = sub->section, row->label = sub->text, row->value = sub->words;
        row->dim = sub->detail;
        // A line under its row is as tall as the row's fold: a share of a row while the row opens or shuts.
        row->reveal = sub->owner >= 0 && sub->owner < (int)SETTINGS_ALL && !settings_always_open(sub->owner) ? settings_fold_at[sub->owner] : UI_ONE;
}

// The board, read once a second while Settings is open (its due time is when it is read next): the GPU's sample, and when what the rows
// show changed, the rows made again. Returns how long till it is due, in nanoseconds (-1 with no table).
static long settings_board_tick(void)
{
        positive now = (positive)ui_kit.now;
        struct windows_issues copy;
        unsigned int was = settings_gpu_key;
        char before_metric[SETTINGS_SUB_TEXT];

        if (!settings.own.table)
                return -1;
        if (settings_board_due && now < settings_board_due)
                return (long)(settings_board_due - now);
        settings_board_due = now + (positive)SETTINGS_BOARD_NS;
        if (!windows_issues_read(settings.own.table, &copy))
                return (long)SETTINGS_BOARD_NS;
        string_copy_bounded((string_address)before_metric, (string_address)settings_metric_line, sizeof(before_metric));
        settings_gpu_sample();
        // The status line is made again when its key moves (settings_gpu_sample). The metric line is rates of presents, and a page made again presents,
        // so its own change makes the page again no more often than SETTINGS_METRIC_NS (the line is as of the last time the page was made).
        if (memory_compare(&copy, &settings_board, sizeof(copy)) || settings_gpu_key != was ||
            (!string_equals((string_address)before_metric, (string_address)settings_metric_line) && now - settings_metric_at >= (positive)SETTINGS_METRIC_NS))
        {
                settings_board = copy;
                settings_metric_at = now;
                settings_rebuild();
        }
        return (long)SETTINGS_BOARD_NS;
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

/*
        A row's label as the window shows it. A setting's label is English the language says (registry.c keeps it, and code
        that compares it compares the English); the name of a network or a device, and a command's verb, are what they are.
*/
static const char address_to settings_label_shown(int row)
{
        if (row < SETTINGS_ANY)
                return language_of(settings_rows[row].label);
        if (settings_item(row) && settings_item(row)->kind == SETTINGS_ADD)
                return language_of(settings_item(row)->label);
        return !settings_item(row) && !settings_generic_of(row) ? WORDS(settings_run_any) : settings_label(row);
}

// What a row of a list does when it is pressed, said where the status is.
static const char address_to settings_item_text(const struct settings_item address_to item)
{
        if (item->kind == SETTINGS_NETWORK)
                return item->flags & SETTINGS_ITEM_JOINED   ? WORDS(settings_item_joined)
                       : item->flags & SETTINGS_ITEM_CANNOT ? WORDS(settings_item_cannot)
                       : item->flags & SETTINGS_ITEM_OPEN   ? WORDS(settings_item_open)
                       : item->flags & SETTINGS_ITEM_SAVED  ? WORDS(settings_item_saved)
                                                            : WORDS(settings_item_ask);
        return item->kind == SETTINGS_DEVICE ? WORDS(settings_item_kept)
               : item->kind == SETTINGS_ADD  ? WORDS(settings_item_add) : "";
}

// What a command of All commands does, as help says; the any row's own words.
static const char address_to settings_text(int row)
{
        const struct settings_generic address_to generic = settings_generic_of(row);
        const struct settings_item address_to item = settings_item(row);

        if (item)
                return settings_item_text(item);
        return generic ? settings_large->pool + generic->text : WORDS(settings_any_text);
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

// A group's name as the column shows it; settings_group_name is the English that code compares.
static const char address_to settings_group_text(int group)
{
        return language_of(settings_group_name(group));
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

// The row typed in, or -1: the kit's field is open for it (the field knows its owner).
static int settings_editing(void)
{
        return settings.edit.open ? settings.edit.owner : -1;
}

// Whether text has what is typed in the filter, whatever the case.
static b32 settings_has(const char address_to text, const char address_to wanted, positive size)
{
        positive length = text ? string_length((string_address)text) : 0;

        return length >= size && (memory_search_ascii_case)((address_any)text, length, (address_any)wanted, size) != null;
}

// Whether the label or the group of a row has the word, in English or in the language taken.
static b32 settings_matches_name(const char address_to label, const char address_to section, const char address_to word, positive size)
{
        return settings_has(label, word, size) || settings_has(section, word, size) ||
               (language.loaded && (settings_has(language_of(label), word, size) || settings_has(language_of(section), word, size)));
}

static b32 settings_matches_word(int row, const char address_to word, positive size)
{
        if (row >= (int)SETTINGS_LIST)
                return settings_matches_name(settings_label(row), settings_section(row), word, size);
        if (row < SETTINGS_ANY)
        {
                const struct settings_row address_to which = &settings_rows[row];

                for (positive at = 0; at < SETTING_WORDS; at++)
                        if (settings_has(which->read[at], word, size) || settings_has(which->set[at], word, size))
                                return true;
                return settings_matches_name(which->label, settings_section(row), word, size);
        }
        return settings_has(settings_label(row), word, size) || settings_has(settings_text(row), word, size) ||
               (row == SETTINGS_ANY && settings_has("anything", word, size));
}

/*
        Whether a row has every word typed in the filter: the words are split at blanks, in any order, and each
        is found in the row's label, its section, its verb or its value, whatever the case.
*/
// The filter's key: the field's text folded and trimmed by the kit (ui_text_search), into room bytes; none while no field is open.
static positive settings_filter_text(p8 address_to into, positive room)
{
        if (!room)
                return 0;
        into[0] = 0;
        return settings.filter_open ? ui_text_search(&settings.filter_edit, into, room) : 0;
}

// The length of the filter's key: the rows are filtered while it is not none.
static positive settings_filter_length(void)
{
        p8 key[SETTINGS_FILTER];

        return settings_filter_text(key, sizeof(key));
}

static b32 settings_matches(int row)
{
        p8 key[SETTINGS_FILTER];
        const char address_to text = (const char address_to)key;
        positive at = 0, length = settings_filter_text(key, sizeof(key));

        if (!length)
                return true;
        while (at < length)
        {
                positive start;

                while (at < length && text[at] == ' ')
                        at++;
                start = at;
                while (at < length && text[at] != ' ')
                        at++;
                if (at > start && !settings_matches_word(row, text + start, at - start))
                        return false;
        }
        return true;
}

/* ---- the map of the screens, above the Display rows ---- */

// A screen as the map draws it: the table's rectangle (device pixels, before any turn) and its turn in degrees.
struct settings_screen
{
        int x, y, width, height;
        unsigned int turn;
};

// The map's view of the screens: a table point (x, y) is at ox + (x - bx) * f / 1000 in device pixels.
struct settings_map_view
{
        long bx, by, f, ox, oy;
};

#define SETTINGS_MAP_PAD 8        // logical pixels: the panel's inset, and the screens' area from its edges
#define SETTINGS_MAP_STRIP 36     // logical pixels: the top strip of the panel, where the identify button sits
#define SETTINGS_MAP_PANEL 150    // logical pixels: the panel the screens are drawn in
#define SETTINGS_MAP_CAPTION 24   // logical pixels: the caption's line under the panel
#define SETTINGS_MAP_BUTTON_W 168 // logical pixels: the identify button
#define SETTINGS_MAP_BUTTON_H 28
#define SETTINGS_SHOWN 64 // bytes of a screen's label or a rate's, shown in a row

static char settings_shown_screen[SETTINGS_SHOWN], settings_shown_rate[SETTINGS_SHOWN];
static char settings_noted[2 * SETTINGS_SHOWN]; // a Display row's value with what was not done of it said after it

// The Display row of an output kind (SETTINGS_OUT_*), or -1 when there is none.
static int settings_output_row(unsigned char output)
{
        for (positive at = 0; at < SETTINGS_ROWS; at++)
                if (settings_rows[at].output == output)
                        return (int)at;
        return -1;
}

static long settings_map_abs(long value)
{
        return value < 0 ? -value : value;
}

// A screen's size once its turn is taken: a quarter turn swaps its width and height.
static void settings_map_turned(const struct settings_screen address_to s, int address_to width, int address_to height)
{
        b32 quarter = s->turn == 90 || s->turn == 270;

        *width = quarter ? s->height : s->width;
        *height = quarter ? s->width : s->height;
}

// The table's rectangle of every screen: the field is drawn to fit it.
static void settings_map_bounds(const struct settings_screen address_to screens, unsigned int count, long address_to x0,
                            long address_to y0, long address_to x1, long address_to y1)
{
        *x0 = *y0 = 0;
        *x1 = *y1 = 1;
        for (unsigned int at = 0; at < count; at++)
        {
                int width, height;

                settings_map_turned(&screens[at], &width, &height);
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
static void settings_map_view_of(const struct settings_screen address_to screens, unsigned int count, int fx, int fy, int fw,
                             int fh, struct settings_map_view address_to view)
{
        long x0, y0, x1, y1, bw, bh, by_width, by_height, f, across, down;

        settings_map_bounds(screens, count, &x0, &y0, &x1, &y1);
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

static int settings_map_field_x(const struct settings_map_view address_to view, long x)
{
        return (int)(view->ox + (x - view->bx) * view->f / 1000);
}

static int settings_map_field_y(const struct settings_map_view address_to view, long y)
{
        return (int)(view->oy + (y - view->by) * view->f / 1000);
}

static long settings_map_table_x(const struct settings_map_view address_to view, long px)
{
        return view->bx + (px - view->ox) * 1000 / view->f;
}

static long settings_map_table_y(const struct settings_map_view address_to view, long py)
{
        return view->by + (py - view->oy) * 1000 / view->f;
}

// A screen's rectangle on the field, in device pixels; a screen is never less than a pixel.
static void settings_map_tile(const struct settings_map_view address_to view, const struct settings_screen address_to s, int address_to x,
                          int address_to y, int address_to width, int address_to height)
{
        int w, h;

        settings_map_turned(s, &w, &h);
        *x = settings_map_field_x(view, s->x);
        *y = settings_map_field_y(view, s->y);
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
static b32 settings_map_snap(const struct settings_screen address_to screens, unsigned int count, unsigned int index, int address_to x,
                         int address_to y, int margin)
{
        int width, height, best_x = *x, best_y = *y;
        long dist_x = margin + 1, dist_y = margin + 1;
        b32 moved = false;

        settings_map_turned(&screens[index], &width, &height);
        for (unsigned int at = 0; at < count; at++)
        {
                int other_w, other_h;
                long d;

                if (at == index)
                        continue;
                settings_map_turned(&screens[at], &other_w, &other_h);
                if (*y < screens[at].y + other_h && *y + height > screens[at].y)
                {
                        d = settings_map_abs(*x - (screens[at].x + other_w));
                        if (d <= margin && d < dist_x)
                                dist_x = d, best_x = screens[at].x + other_w;
                        d = settings_map_abs(*x + width - screens[at].x);
                        if (d <= margin && d < dist_x)
                                dist_x = d, best_x = screens[at].x - width;
                }
                if (*x < screens[at].x + other_w && *x + width > screens[at].x)
                {
                        d = settings_map_abs(*y - (screens[at].y + other_h));
                        if (d <= margin && d < dist_y)
                                dist_y = d, best_y = screens[at].y + other_h;
                        d = settings_map_abs(*y + height - screens[at].y);
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
static positive settings_map_add(char address_to into, positive room, positive at, const char address_to text)
{
        for (positive n = 0; text[n] && at + 1 < room; n++)
                into[at++] = text[n];
        into[at] = 0;
        return at;
}

static positive settings_map_number(char address_to into, positive room, positive at, long value)
{
        char digits[24];
        positive count = 0;
        unsigned long magnitude = value < 0 ? (unsigned long)(-(value + 1)) + 1 : (unsigned long)value;

        if (value < 0)
                at = settings_map_add(into, room, at, "-");
        do
                digits[count++] = (char)('0' + magnitude % 10), magnitude /= 10;
        while (magnitude && count < sizeof(digits));
        while (count && at + 1 < room)
                into[at++] = digits[--count];
        into[at] = 0;
        return at;
}

// The screens the table lists, as the map draws them and numbers them: how many, 0 when it lists none or too many.
static unsigned int settings_map_load(struct settings_screen address_to screens, unsigned int address_to number)
{
        struct windows address_to table = settings.own.table;
        unsigned int count;

        if (!table || !(count = __atomic_load_n(&table->outputs, __ATOMIC_ACQUIRE)) || count > WINDOWS_OUTPUTS)
                return 0;
        windows_screen_numbers(table, number);
        for (unsigned int at = 0; at < count; at++)
        {
                int x, y, width, height;

                windows_output(table, at, &x, &y, &width, &height);
                screens[at].x = x, screens[at].y = y, screens[at].width = width, screens[at].height = height;
                screens[at].turn = table->output_turn[at];
        }
        return count;
}

// The table's index of the screen numbered wanted (from 1), or -1.
static int settings_map_numbered(const unsigned int address_to number, unsigned int count, unsigned int wanted)
{
        for (unsigned int at = 0; at < count; at++)
                if (number[at] == wanted)
                        return (int)at;
        return -1;
}

// The screen the Display rows are for: the one chosen on the map, or the first by number while none is. Its table index,
// or -1 when the table lists no screen.
static int settings_map_chosen(void)
{
        struct settings_screen screens[WINDOWS_OUTPUTS];
        unsigned int number[WINDOWS_OUTPUTS] = {0};
        unsigned int count = settings_map_load(screens, number);

        if (!count)
                return -1;
        if (settings.map.display < 0 || settings.map.display >= (int)count)
                settings.map.display = settings_map_numbered(number, count, 1);
        return settings.map.display;
}

// Whether the map is shown: the Display group is the one chosen, with no filter typed and no card.
static b32 settings_map_on(void)
{
        int row = settings_output_row(SETTINGS_OUT_SCREEN);

        return row >= 0 && !settings.page.card && !settings_filter_length() && settings_group_of(row) == settings.page.chosen;
}

static int settings_map_height(void)
{
        return ui_px(&settings.ui, SETTINGS_MAP_PANEL) + ui_px(&settings.ui, UI_GAP2) + ui_px(&settings.ui, SETTINGS_MAP_CAPTION);
}

// The panel, its screens' area and the button, from the page's place: where the map is drawn and hit.
static void settings_map_place(void)
{
        struct ui address_to ui = &settings.ui;
        int pad = ui_px(ui, SETTINGS_MAP_PAD), strip = ui_px(ui, SETTINGS_MAP_STRIP);
        int button_w = ui_px(ui, SETTINGS_MAP_BUTTON_W), button_h = ui_px(ui, SETTINGS_MAP_BUTTON_H);
        struct ui_control address_to c = &settings.map.identify;

        settings.map.x = settings.page.left, settings.map.y = settings.page.top;
        settings.map.w = settings.page.right - settings.page.left, settings.map.h = ui_px(ui, SETTINGS_MAP_PANEL);
        settings.map.field_x = settings.map.x + pad, settings.map.field_y = settings.map.y + strip;
        settings.map.field_w = settings.map.w - 2 * pad, settings.map.field_h = settings.map.h - strip - pad;
        c->kind = UI_BUTTON, c->variant = UI_BUTTON_OUTLINED, c->label = (string_address)WORDS(settings_identify_screens);
        c->w = button_w * 100 / ui->scale, c->h = button_h * 100 / ui->scale;
        c->x = ui_mirror(settings.map.x + settings.map.w - pad - button_w, button_w, settings.map.x, settings.map.w) * 100 / ui->scale;
        c->y = (settings.map.y + (strip - button_h) / 2) * 100 / ui->scale;
}

// The Rotation dropdown's words, in the order it lists them, and the degrees each one is.
static const struct settings_turn
{
        const char address_to label;
        int degrees;
} settings_turns[] = {{"Normal", 0}, {"Turned right", 90}, {"Upside down", 180}, {"Turned left", 270}};

static const char address_to settings_turn_label(unsigned int turn)
{
        for (positive at = 0; at < sizeof(settings_turns) / sizeof(settings_turns[0]); at++)
                if (settings_turns[at].degrees == (int)turn)
                        return language_of(settings_turns[at].label);
        return language_of(settings_turns[0].label);
}

// A screen's name as the Screen dropdown and the rows show it: its number and its monitor's name from its EDID with its
// connector's name in brackets (Screen 1: Dell U2720Q [DP-1]), or the connector's name alone when the EDID names none.
static positive settings_screen_label(unsigned int number, const char address_to name, const char address_to model, char address_to into,
                                      positive room)
{
        if (!model[0])
                return language_format(into, room, WORDS(settings_screen_bare), (long)number, name);
        return language_format(into, room, WORDS(settings_screen_named), (long)number, model, name);
}

// A rate in Hz, with the screen's first rate, the one it recommends, marked.
// The six scale presets in percent, in the order the Scale dropdown lists them.
static const unsigned int settings_scales[] = {50, 75, 100, 125, 150, 200};

// The scale a screen's native size suggests, in percent: a 5K or 6K screen 200, a 4K one 150, any smaller one 100.
static unsigned int settings_scale_suggested(const struct windows address_to table, int chosen)
{
        unsigned int width = table->output_w[chosen];

        return width >= 5120 ? 200 : width >= 3840 ? 150 : 100;
}

// A scale in percent, with the marker of the one the screen suggests: 150% (recommended).
static positive settings_scale_label(unsigned int percent, b32 recommended, char address_to into, positive room)
{
        return language_format(into, room, recommended ? WORDS(settings_scale_best) : WORDS(settings_scale_plain), (long)percent);
}

static positive settings_rate_label(unsigned int hz, b32 recommended, char address_to into, positive room)
{
        return language_format(into, room, recommended ? WORDS(settings_rate_best) : WORDS(settings_rate_plain), (long)hz);
}

// How many refresh rates the chosen screen lists, at most WINDOWS_OUTPUT_HZ.
static unsigned int settings_rates_of(int chosen)
{
        unsigned int rates = settings.own.table->output_rates[chosen];

        return rates > WINDOWS_OUTPUT_HZ ? WINDOWS_OUTPUT_HZ : rates;
}

// What a Display row shows for the chosen screen now: its label, its turn's word, or its rate's.
static const char address_to settings_output_text(int row, int chosen)
{
        struct windows address_to table = settings.own.table;
        unsigned int number[WINDOWS_OUTPUTS] = {0}, rates;

        if (chosen < 0)
                return "";
        if (settings_rows[row].output == SETTINGS_OUT_TURN)
                return settings_turn_label(table->output_turn[chosen]);
        if (settings_rows[row].output == SETTINGS_OUT_SCALE)
        {
                unsigned int percent = table->output_asked_scale[chosen];

                settings_scale_label(percent, percent == settings_scale_suggested(table, chosen), settings_shown_rate, sizeof(settings_shown_rate));
                return settings_shown_rate;
        }
        if (settings_rows[row].output == SETTINGS_OUT_REFRESH)
        {
                rates = settings_rates_of(chosen);
                for (unsigned int at = 0; at < rates; at++)
                        if (table->output_hz[chosen][at] == table->output_now[chosen])
                        {
                                settings_rate_label(table->output_hz[chosen][at], at == 0, settings_shown_rate, sizeof(settings_shown_rate));
                                return settings_shown_rate;
                        }
                settings_map_number(settings_shown_rate, sizeof(settings_shown_rate), 0, (long)table->output_now[chosen]);
                return settings_shown_rate;
        }
        windows_screen_numbers(table, number);
        settings_screen_label(number[chosen], table->output_name[chosen], table->output_model[chosen], settings_shown_screen,
                              sizeof(settings_shown_screen));
        return settings_shown_screen;
}

// A Display row's value as the row shows it: what settings_output_text says, and after it what the compositor did not do of
// the screen's displays line (the table's output_refused), in plain words: a rate the screen does not offer, a turn it could
// not do, or a place the file does not read. The row says so where the change was made, so nothing fails silently.
static const char address_to settings_row_text(int row, int chosen)
{
        struct windows address_to table = settings.own.table;
        const char address_to shown = settings_output_text(row, chosen);
        unsigned char output = settings_rows[row].output;
        unsigned int refused;

        if (chosen < 0)
                return shown;
        refused = table->output_refused[chosen];
        if (output == SETTINGS_OUT_REFRESH && (refused & WINDOWS_REFUSED_RATE))
        {
                language_format(settings_noted, sizeof(settings_noted), WORDS(settings_refused_rate), shown, (long)table->output_asked_hz[chosen]);
                return settings_noted;
        }
        if (output == SETTINGS_OUT_TURN && (refused & WINDOWS_REFUSED_TURN))
        {
                language_format(settings_noted, sizeof(settings_noted), WORDS(settings_refused_turn), shown,
                                settings_turn_label(table->output_asked_turn[chosen]));
                return settings_noted;
        }
        if (output == SETTINGS_OUT_SCREEN && (refused & WINDOWS_REFUSED_PLACE))
        {
                language_format(settings_noted, sizeof(settings_noted), WORDS(settings_refused_place), shown);
                return settings_noted;
        }
        return shown;
}

// The screen under (px, py) on the map, the one on top (the last drawn), or -1.
static int settings_map_at(int px, int py, const struct settings_screen address_to screens, unsigned int count)
{
        struct settings_map_view view;

        settings_map_view_of(screens, count, settings.map.field_x, settings.map.field_y, settings.map.field_w, settings.map.field_h, &view);
        for (int at = (int)count - 1; at >= 0; at--)
        {
                int x, y, width, height;

                settings_map_tile(&view, &screens[at], &x, &y, &width, &height);
                if (px >= x && px < x + width && py >= y && py < y + height)
                        return at;
        }
        return -1;
}

// The caption under the map, which says what the map does now: how to move a screen, that a drag is let go to place it, that
// the keys choose and move a screen while the map has them, or that there is nothing to move (no screen, or one screen,
// which has nothing to be moved against).
static const char address_to settings_map_hint(unsigned int count)
{
        return settings.map.drag >= 0 ? WORDS(settings_map_drag)
               : settings.map.keys && count > 1 ? WORDS(settings_map_keys)
               : !count               ? WORDS(settings_map_none)
               : count == 1           ? WORDS(settings_map_one) : WORDS(settings_map_hint);
}

// The panel, its screens drawn to fit it (a drag shows the screen where it would be let go), each one's number, and the
// caption under the panel (settings_map_hint). With no screen the panel is empty and the caption says why.
static void settings_map_draw(struct ui address_to ui)
{
        struct settings_screen screens[WINDOWS_OUTPUTS];
        unsigned int number[WINDOWS_OUTPUTS] = {0};
        unsigned int count = settings_map_load(screens, number);
        int radius = ui_px(ui, UI_RADIUS), pad = ui_px(ui, UI_INSET), line = ui_px(ui, ui_types[UI_LABEL_MEDIUM].line);
        int caption = settings.map.y + settings.map.h + ui_px(ui, UI_GAP2);
        const char address_to hint = settings_map_hint(count);

        ui_round(ui, settings.map.x, settings.map.y, settings.map.w, settings.map.h, radius, ui_role(UI_SURFACE_LOW) | 0xff000000u);
        // The screens are where they are on the desk, left is left whichever way the window reads: the field is not turned over.
        UI_KEEP(settings.map.field_x, settings.map.field_y, settings.map.field_w, settings.map.field_h);
        for (unsigned int at = 0; count && at < count; at++)
        {
                struct settings_map_view view;
                struct settings_screen moved;
                int x, y, width, height;
                char text[SETTINGS_SHOWN];

                settings_map_view_of(screens, count, settings.map.field_x, settings.map.field_y, settings.map.field_w, settings.map.field_h, &view);
                moved = screens[at];
                if (settings.map.drag == (int)at)
                        moved.x = settings.map.target_x, moved.y = settings.map.target_y;
                settings_map_tile(&view, &moved, &x, &y, &width, &height);
                ui_round(ui, x, y, width, height, radius, ui_role(UI_SURFACE_HIGHEST) | 0xff000000u);
                if ((int)at == settings.map.display)
                        ui_fill(ui, x, y, width, height, (ui_role(UI_PRIMARY) & 0x00ffffffu) | 0x40000000u);
                // The chosen screen is outlined while the keys are on the map (the focus), in the primary colour: the kit's focus outline,
                // the list row's, round at the tile's corners.
                if ((int)at == settings.map.display && settings.map.keys)
                        ui_kit_outline(ui, &(struct ui_box){x * 256, y * 256, (x + width) * 256, (y + height) * 256, radius * 256, radius * 256},
                                       ui_q8(ui, 2), ui_role(UI_PRIMARY));
                settings_map_number(text, sizeof(text), 0, (long)number[at]);
                ui_kit_text(ui, x + (width - ui_kit_measure(ui, (string_address)text, UI_TITLE_LARGE)) / 2,
                            ui_kit_baseline(ui, y + (height - ui_px(ui, ui_types[UI_TITLE_LARGE].line)) / 2, UI_TITLE_LARGE), (string_address)text,
                            UI_TITLE_LARGE, ui_role(UI_ON_SURFACE), 0);
                if (height >= 3 * line)
                        ui_kit_text_flipped(ui, x + pad, ui_kit_baseline(ui, y + height - pad - line, UI_LABEL_MEDIUM),
                                            (string_address)settings.own.table->output_name[at], UI_LABEL_MEDIUM, ui_role(UI_ON_SURFACE_VARIANT), 0, x, width);
        }
        ui_kit_text_flipped(ui, settings.map.x, ui_kit_baseline(ui, caption, UI_LABEL_MEDIUM), (string_address)hint, UI_LABEL_MEDIUM,
                            ui_role(UI_ON_SURFACE_VARIANT), 0, settings.map.x, settings.map.w);
        ui_control_draw(ui, &settings.map.identify);
}

static fn settings_describe(int id, struct ui_row address_to row);

// The page laid out for the window's size: the kit's bands, its column and the list in the body.
static int settings_map_inset(void);

static fn settings_layout(void)
{
        struct ui address_to ui = &settings.ui;

        // What the list shows is the app's: its rows, described by settings_describe, and where a change is drawn again.
        if (!settings.list.ids)
        {
                settings.list.ids = (int)SETTINGS_ALL + SETTINGS_SUBS;
                settings.list.describe = settings_describe;
                settings.list.damaged = settings_changed;
                settings.list.hover = settings.list.down = -1;
                settings.map.display = settings.map.drag = -1;
                for (positive k = 0; k < sizeof(settings_accents) / sizeof(settings_accents[0]); k++)
                {
                        if (k)
                                string_append_bounded((string_address)settings.accents, (string_address) " ", sizeof(settings.accents));
                        string_append_bounded((string_address)settings.accents, (string_address)settings_accents[k].colour, sizeof(settings.accents));
                }
        }
        // The page's column shows with the rows and not with a card, which has the body and the whole width.
        settings.page.column_on = !settings.page.card;
        settings.page.body = settings.page.card ? &settings.card : &settings.list.scroll;
        settings.page.column.count = settings_groups() < UI_ITEMS ? settings_groups() : UI_ITEMS;
        ui_page_fit(ui, &settings.page, &settings.list, settings.width, settings.height, settings_map_inset, &settings.card, ui_lines_height(ui, settings.card_text.count));
}

// The map's panel above the rows, placed in the page (its height is the rows' inset), or none when the map is not shown.
static int settings_map_inset(void)
{
        if (!settings_map_on())
                return 0;
        settings_map_place();
        return settings_map_height();
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
        settings_sub_count = 0;
        for (positive row = 0; row < settings_rows_count(); row++)
                if (settings_filter_length() ? settings_matches((int)row) : settings_group_of((int)row) == settings.page.chosen)
                {
                        settings.list.id[count++] = (unsigned short)row;
                        count = settings_sub_rows((int)row, count);
                }
        // The rows of a list come after, each in the group of its feed.
        for (int list = 0; list < SETTINGS_LISTS; list++)
                for (int at = 0; at < settings.item_count[list]; at++)
                {
                        int row = (int)SETTINGS_LIST + list * SETTINGS_LIST_ROWS + at;

                        if (settings_filter_length() ? settings_matches(row) : settings_group_of(row) == settings.page.chosen)
                                settings.list.id[count++] = (unsigned short)row;
                }
        settings.list.count = count;
        if (settings.page.menu_row >= 0 && settings_pos(settings.page.menu_row) < 0)
                settings.page.menu_row = -1, ui_popmenu_close(&settings.popup);
        if (settings_pos(settings.list.focus) < 0 && count)
                settings.list.focus = settings.list.id[0];
        settings_layout();
        ui_page_redraw(&settings.page);
}

// The same, and the row with the focus brought into view.
static fn settings_refilter(void)
{
        settings_rebuild();
        ui_page_reveal(&settings.ui, &settings.page, &settings.list);
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
        ui_lines_set(&settings.card_text, settings_large->card, made, settings_large->card_at, settings_large->card_size, SETTINGS_CARD_LINES);
}

// The card shown with the title of the command that made it.
static fn settings_card_open(const char address_to title)
{
        settings_card_fill();
        string_copy_bounded((string_address)settings.card_title, (string_address)title, sizeof(settings.card_title));
        settings.page.card = true;
        settings.card.scroll = 0;
        ui_lines_wrap(&settings.ui, &settings.card_text, settings.width);
        settings_layout();
        ui_page_redraw(&settings.page);
}

static fn settings_card_leave(void)
{
        settings.page.card = false;
        settings.status[0] = 0;
        settings_layout();
        ui_page_redraw(&settings.page);
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
                (void)shell_exec_file(words[0], words, count, window_asked_environment());
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
        settings.began = (positive)ui_kit.now;
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

// The rows to ask (settings.unread): a bit each, and the lowest is asked first.
static void settings_unread_set(positive row, int on)
{
        positive bit = (positive)1 << (row % 64);

        if (on)
                settings.unread[row / 64] |= bit;
        else
                settings.unread[row / 64] &= ~bit;
}

static int settings_unread_is(positive row)
{
        return (int)((settings.unread[row / 64] >> (row % 64)) & 1);
}

static int settings_unread_any(void)
{
        for (positive word = 0; word < SETTINGS_UNREAD_WORDS; word++)
                if (settings.unread[word])
                        return 1;
        return 0;
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
                else if (settings_unread_any())
                {
                        positive first = 0;

                        while (!settings_unread_is(first))
                                first++;
                        settings.job = (int)first;
                        settings_unread_set(first, 0);
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
                else if (settings.job == SETTINGS_JOB_IDENTIFY)
                {
                        words[0] = settings.moonwater;
                        words[1] = (string_address) "canvas";
                        words[2] = (string_address) "identify";
                        words[3] = null;
                        count = 3;
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
                                language_format(line, sizeof(line), WORDS(settings_running), text);
                                settings_say((string_address)line);
                        }
                        return;
                }

                if (settings.job_sets)
                        settings_say((string_address)(count || settings.job < SETTINGS_ANY
                                                          ? WORDS(settings_cannot_run)
                                                          : WORDS(settings_too_many)));
                else if (settings.job >= 0)
                        settings.state[settings.job] = SETTINGS_FAILED;
                ui_page_redraw(&settings.page);
        }
}

// Every row asked again: a change to one can move another (airplane mode turns the radios off).
static fn settings_ask_all(void)
{
        memory_zero(settings.unread, sizeof(settings.unread));
        for (positive row = 0; row < SETTINGS_ROWS; row++)
                if (settings_rows[row].output == SETTINGS_OUT_NONE)
                        settings_unread_set(row, 1);
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

/*
        Where a change to value waits: a change to a settings row takes the place of the one waiting for that row. A command is
        not a state: a join, a forget, or a command typed in the Run any row (all of them the same row here) waits in its own
        place, after the others, unless the same command already waits (a double click asks it twice, and it is done once).
*/
static positive settings_waiting(int row, const char address_to value)
{
        positive at = 0;

        if (row >= 0 && row < SETTINGS_ANY)
                return settings_slot(row);
        while (at < settings.queued && !(settings.queue[at].row == row && string_equals((string_address)settings.queue[at].value, (string_address)value)))
                at++;
        return at;
}

// A row changed to value, now or once the verb running has finished (see settings_waiting for where it waits).
static fn settings_change(int row, const char address_to value)
{
        positive at;

        // A feed's reading is one that can be had again: a change waits for no feed.
        settings_feed_drop();
        at = settings_waiting(row, value);

        if (at == settings.queued)
        {
                if (settings.queued == SETTINGS_QUEUE)
                {
                        settings_say((string_address)WORDS(settings_busy));
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
                settings_say((string_address)(killed ? WORDS(settings_stopped) : wait_status_code(status) ? WORDS(settings_refused) : WORDS(settings_done)));
        else if (lines == 1)
                settings_say((string_address)first);
        else
        {
                count = settings_generic_words(settings.job, settings.job_value, words, SETTINGS_WORDS);
                settings_join(words, count, title, sizeof(title));
                settings_card_open(title);
                settings_say((string_address)(killed ? WORDS(settings_card_stopped) : WORDS(settings_card_back)));
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
                if (string_equals((string_address)settings_feeds[feed].group, (string_address)settings_group_name(settings.page.chosen)))
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

        settings.feed_due = (positive)ui_kit.now + (positive)settings_feeds[feed].every * 1000000ull;
        // What is being typed for a row of the list is for the row as it was: nothing is changed under it.
        if (killed || settings_editing() >= (int)SETTINGS_LIST)
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
                if ((settings_rows[at].flags & SETTINGS_TICK) && settings_group_of((int)at) == settings.page.chosen &&
                    settings_editing() != (int)at)
                        settings_unread_set(at, 1);
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
        positive now = (positive)ui_kit.now;
        string_address words[SETTINGS_WORDS];
        positive count = 1;

        if (feed < 0 || settings.page.card || settings_editing() >= 0 || settings_filter_length() ||
            (settings.own.table->state[settings.own.row] & WINDOW_MINIMIZED) || settings.child > 0 || settings.queued ||
            settings_unread_any() || settings.help == 1)
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


/*
        What a row says changed (its value or whether it was read): where it shows, its rows are drawn again, and
        nothing else is (a row is its band of the list whatever it holds, and what is typed matches no value). The
        menu it opened lists its value, and is drawn whole.
*/
static fn settings_reading_changed(int row)
{
        if (settings.page.menu_row == row)
                ui_page_redraw(&settings.page);
        else if (!settings.page.card)
                ui_list_damage(&settings.ui, &settings.list, row);
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
                                settings_say((string_address)WORDS(settings_too_long));
                        else if (size)
                                settings_say((string_address)line);
                        else
                                settings_say((string_address)(wait_status_code(status) ? WORDS(settings_refused) : WORDS(settings_done)));
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
                        settings_say((string_address)WORDS(settings_help_more));
        }
        else
        {
                // What a row's verb said is what every row waiting to be asked the same of says too: one run for them all.
                for (positive other = 0; other < SETTINGS_ROWS; other++)
                {
                        char before[SETTINGS_VALUE];
                        unsigned char was = settings.state[other];

                        if ((int)other != row && !settings_unread_is(other))
                                continue;
                        if (!settings_same_verb(&settings_rows[row], &settings_rows[other]))
                                continue;
                        settings_unread_set(other, 0);
                        string_copy_bounded((string_address)before, (string_address)settings.value[other], sizeof(before));
                        settings.state[other] = killed ? SETTINGS_FAILED
                                                       : (unsigned char)settings_read(&settings_rows[other], settings_large->said,
                                                                                       settings.said_length, settings.value[other],
                                                                                       SETTINGS_VALUE, wait_status_code(status) != 0);
                        // A row asked again that says what it said is no change to draw.
                        if (settings.state[other] != was ||
                            !string_equals((string_address)before, (string_address)settings.value[other]))
                                settings_reading_changed((int)other);
                }
        }

        settings_next_job();

        // The first reading of every row, said once where the machine's log is: how many were unread or not understood.
        if (!settings.told && !settings_unread_any() && settings.child <= 0 && !settings.queued)
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
                settings_say((string_address)WORDS(settings_too_many));
                return false;
        }
        if (count == 1)
        {
                settings_say((string_address)WORDS(settings_type_command));
                return false;
        }
        if (settings_asks_for(words + 1, count - 1) && !ui_confirm_ask(&settings.confirm, row + 1, args))
        {
                char line[SETTINGS_STATUS], text[SETTINGS_STATUS];

                settings_join(words, count, text, sizeof(text));
                ui_confirm_line(line, sizeof(line), text);
                settings_say((string_address)line);
                return false;
        }
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
static b32 settings_output_choice(const struct settings_row address_to which, positive index, char address_to into, positive room);

static b32 settings_choice(const struct settings_row address_to row, positive index, char address_to into, positive room)
{
        if (row->output != SETTINGS_OUT_NONE)
                return settings_output_choice(row, index, into, room);
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

// The screens the table lists, counted for the map and the dropdowns: how many, 0 when it lists none or too many.
static unsigned int settings_map_count(void)
{
        unsigned int count = settings.own.table ? __atomic_load_n(&settings.own.table->outputs, __ATOMIC_ACQUIRE) : 0;

        return count > WINDOWS_OUTPUTS ? 0 : count;
}

// Whether a row sets the screen's Screen modes: its verb is canvas modes, which says largest and preferred.
static b32 settings_is_modes(int row)
{
        return settings_rows[row].set[1] && string_equals((string_address)settings_rows[row].set[1], (string_address) "modes");
}

// The English a choice's word is shown as, before the language says it: the on-screen keyboard's three say what they do (the
// verb and the file keep auto, off and on); any other choice says its own word.
static const char address_to settings_choice_said(int row, const char address_to word)
{
        if (!settings_rows[row].path || !string_equals((string_address)settings_rows[row].path, (string_address)SETTING_ONSCREEN_PATH))
                return word;
        return string_equals((string_address)word, (string_address) "auto") ? "Automatic"
               : string_equals((string_address)word, (string_address) "off") ? "Always off"
               : string_equals((string_address)word, (string_address) "on") ? "Always on" : word;
}

// The plain word a row's verb word is read as: Screen modes says Biggest picture and Recommended, others say their word.
static const char address_to settings_label_of(int row, const char address_to word)
{
        // The Language row says each language by its own name, whichever language is taken (language_names).
        if (settings_rows[row].flags & SETTINGS_LANGUAGE)
                return language_name_of(word);
        if (!settings_is_modes(row))
                // A choice's own word is English the language says (language_of is English when no language is taken); anything else
                // a row holds is what it is.
                return settings_rows[row].kind == SETTINGS_CHOICE && settings_rows[row].choices &&
                               setting_place(settings_rows[row].choices, (string_address)word) >= 0
                           ? language_of(settings_choice_said(row, word))
                           : word;
        // The word is the verb's, a key; what comes back is what is shown.
        return string_equals((string_address)word, (string_address) "largest") ? WORDS(settings_mode_largest) // shown for a key
               : string_equals((string_address)word, (string_address) "preferred") ? WORDS(settings_mode_preferred) : word; // shown for a key
}

// The verb's word for a plain word shown in a row's menu (the inverse of settings_label_of).
static void settings_word_of(int row, const char address_to label, char address_to into, positive room)
{
        const char address_to word = label;

        if (settings_rows[row].flags & SETTINGS_LANGUAGE)
                word = language_code_of(label);
        else if (!settings_is_modes(row) && settings_rows[row].kind == SETTINGS_CHOICE && settings_rows[row].choices)
        {
                // The word of the row's choices that the language says as label does: its menu lists the language's words.
                char each[SETTINGS_VALUE];

                for (positive k = 0; settings_word(settings_rows[row].choices, k, each, sizeof(each)); k++)
                        if (string_equals((string_address)language_of(settings_choice_said(row, each)), (string_address)label)) // shown against shown
                        {
                                string_copy_bounded((string_address)into, (string_address)each, room);
                                return;
                        }
        }
        else if (settings_is_modes(row))
                word = string_equals((string_address)label, (string_address)WORDS(settings_mode_largest)) ? "largest" // shown against shown
                       : string_equals((string_address)label, (string_address)WORDS(settings_mode_preferred)) ? "preferred" : label; // shown against shown
        string_copy_bounded((string_address)into, (string_address)word, room);
}

// The words a Display dropdown lists, one by one: the screens by number, the four turns, or the refresh rates the chosen
// screen lists, the first marked as recommended. False past the last one.
static b32 settings_output_choice(const struct settings_row address_to which, positive index, char address_to into, positive room)
{
        struct windows address_to table = settings.own.table;
        int chosen = settings_map_chosen();

        if (which->output == SETTINGS_OUT_SCREEN)
        {
                unsigned int number[WINDOWS_OUTPUTS] = {0}, count = settings_map_count();
                int at;

                if (index >= count)
                        return false;
                windows_screen_numbers(table, number);
                at = settings_map_numbered(number, count, (unsigned int)index + 1);
                if (at < 0)
                        return false;
                settings_screen_label((unsigned int)index + 1, table->output_name[at], table->output_model[at], into, room);
                return true;
        }
        if (which->output == SETTINGS_OUT_TURN)
        {
                if (index >= sizeof(settings_turns) / sizeof(settings_turns[0]))
                        return false;
                string_copy_bounded((string_address)into, (string_address)language_of(settings_turns[index].label), room);
                return true;
        }
        if (which->output == SETTINGS_OUT_SCALE && chosen >= 0 && index < sizeof(settings_scales) / sizeof(settings_scales[0]))
        {
                unsigned int percent = settings_scales[index];

                settings_scale_label(percent, percent == settings_scale_suggested(table, chosen), into, room);
                return true;
        }
        if (which->output == SETTINGS_OUT_REFRESH && chosen >= 0 && index < settings_rates_of(chosen))
        {
                settings_rate_label(table->output_hz[chosen][index], index == 0, into, room);
                return true;
        }
        return false;
}

// The place of a plain word, or a Display label, in a row's dropdown: -1 when the row lists none of them.
static int settings_choice_index(int row, const char address_to label)
{
        char word[SETTINGS_VALUE];

        for (int at = 0; settings_choice(&settings_rows[row], (positive)at, word, sizeof(word)); at++)
                if (string_equals((string_address)settings_label_of(row, word), (string_address)label))
                        return at;
        return -1;
}

// What a row shows as its current value, in the words its menu lists: a Display row's now, or a plain row's word.
static const char address_to settings_shown_value(int row)
{
        if (settings_rows[row].output == SETTINGS_OUT_NONE)
                return settings_label_of(row, settings.value[row]);
        return settings_output_text(row, settings_map_chosen());
}

// A Display row as the kit lists it: the Screen, Rotation and Refresh rate dropdowns with the chosen screen's words, the
// note under Rotation (who turns the picture), and a dim word where there is no screen or no rate to choose.
static void settings_describe_output(int id, struct ui_row address_to row)
{
        struct windows address_to table = settings.own.table;
        int chosen = settings_map_chosen();
        unsigned char output = settings_rows[id].output;

        row->kind = UI_ROW_CHOICE;
        if (output == SETTINGS_OUT_NOTICES)
        {
                // The kernel's notices on the board (the classes the feeder raises, which the row lists): their count, and no word when none.
                unsigned int count = 0;

                for (int at = 0; at < WINDOWS_ISSUES; at++)
                        if (settings_board.issue[at].used && settings_board.issue[at].code >= WINDOWS_ISSUE_KERNEL_SYSTEM && settings_board.issue[at].code <= WINDOWS_ISSUE_KERNEL_PROGRAM)
                                count++;
                row->kind = UI_ROW_PLAIN, row->dim = count == 0;
                if (!count)
                        row->value = WORDS(settings_notices_none);
                else
                {
                        language_format(settings_notices_text, sizeof(settings_notices_text), WORDS(settings_notices_count), (long)count);
                        row->value = settings_notices_text;
                }
                return;
        }
        if (output == SETTINGS_OUT_NOTE)
        {
                row->kind = UI_ROW_PLAIN, row->dim = true;
                row->label = chosen >= 0 && table->output_software[chosen] ? WORDS(settings_turned_software) : language_of(settings_rows[id].label);
                return;
        }
        if (chosen < 0)
        {
                row->kind = UI_ROW_PLAIN, row->dim = true, row->value = WORDS(settings_no_screen);
                return;
        }
        // A screen with no rate to choose says so, and a screen with one rate has nothing to choose either: a word, not a menu.
        if (output == SETTINGS_OUT_REFRESH && !settings_rates_of(chosen))
        {
                row->kind = UI_ROW_PLAIN, row->dim = true, row->value = WORDS(settings_no_rates);
                return;
        }
        if ((output == SETTINGS_OUT_REFRESH && settings_rates_of(chosen) == 1) || (output == SETTINGS_OUT_SCREEN && settings_map_count() == 1))
                row->kind = UI_ROW_PLAIN;
        row->value = settings_row_text(id, chosen);
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

// A Display dropdown's choice, made: the screen the rows are for, or a turn or a refresh rate the verb is run with for the
// chosen screen by its name (canvas display NAME rotation|refresh ...). A choice the screen has already is sent nothing.
static fn settings_output_apply(int row, const char address_to label)
{
        const struct settings_row address_to which = &settings_rows[row];
        struct windows address_to table = settings.own.table;
        int at = settings_choice_index(row, label), chosen = settings_map_chosen();
        char value[SETTINGS_TYPED];
        positive length;

        if (at < 0 || !table)
                return;
        if (which->output == SETTINGS_OUT_SCREEN)
        {
                unsigned int number[WINDOWS_OUTPUTS] = {0};

                windows_screen_numbers(table, number);
                settings.map.display = settings_map_numbered(number, settings_map_count(), (unsigned int)at + 1);
                ui_page_redraw(&settings.page);
                return;
        }
        if (chosen < 0)
                return;
        length = settings_map_add(value, sizeof(value), 0, table->output_name[chosen]);
        if (which->output == SETTINGS_OUT_TURN)
        {
                if (settings_turns[at].degrees == (int)table->output_turn[chosen])
                        return;
                length = settings_map_add(value, sizeof(value), length, " rotation ");
                settings_map_number(value, sizeof(value), length, settings_turns[at].degrees);
        }
        else if (which->output == SETTINGS_OUT_SCALE && at < (int)(sizeof(settings_scales) / sizeof(settings_scales[0])))
        {
                if (settings_scales[at] == table->output_asked_scale[chosen])
                        return;
                length = settings_map_add(value, sizeof(value), length, " scale ");
                settings_map_number(value, sizeof(value), length, (long)settings_scales[at]);
        }
        else if (which->output == SETTINGS_OUT_REFRESH && at < (int)settings_rates_of(chosen))
        {
                unsigned int hz = table->output_hz[chosen][at];

                if (hz == table->output_now[chosen])
                        return;
                length = settings_map_add(value, sizeof(value), length, " refresh ");
                if (at == 0)
                        settings_map_add(value, sizeof(value), length, "preferred");
                else
                        settings_map_number(value, sizeof(value), length, (long)hz);
        }
        else
                return;
        settings_change(row, value);
}

/*
        A choice's menu is the kit's popup (ui.c's menu defaults), opened on the settings window: its words are its items,
        the one the row has now is marked and lit, and it is glass, a temporary overlay. It chooses on a release, closes on
        a press outside, Escape or Left, and its keys are the popup's: the settings window does not see them while it is open.
        Its words are copied here, since a popup keeps the labels it is given (a & is doubled, so it is no mnemonic).
*/
/*
        The menu's answer, as the runner gives it (ui_popmenu_pump): the popup closes as it answers, and the answer goes to the app.
        A negative answer is the menu gone (Escape, Left, or the compositor's press outside), and closes the choice.
*/
static fn settings_chose(int id);

static fn settings_menu_answer(int answer)
{
        if (answer != UI_MENU_NOTHING)
        {
                ui_popmenu_close(&settings.popup);
                settings_chose(answer);
        }
}

static fn settings_menu_close(void)
{
        if (settings.page.menu_row < 0)
                return;
        ui_page_menu_close(&settings.page, &settings.popup);
        settings_say((string_address) "");
        ui_page_redraw(&settings.page);
}

// A choice's menu, under its row and right-aligned to it, kept in the window's margin: its words, the one the row has now
// marked (lit where the keys start, the first when none is), and the words said where the keys are.
static const char address_to settings_menu_label(int row, int k)
{
        static char word[SETTINGS_VALUE];

        if (!settings_choice(&settings_rows[row], (positive)k, word, sizeof(word)))
                return null;
        return settings_label_of(row, word);
}

static fn settings_menu_open(int row)
{
        if (settings_pos(row) < 0 || settings.ui.scale <= 0)
                return;
        if (settings_rows[row].flags & SETTINGS_BATTERIES)
                settings.battery_menu_count = battery_list(settings.battery_menu, BATTERY_LIST);
        if (!ui_page_menu_open(&settings.ui, &settings.page, &settings.list, &settings.popup, &settings.own, row, settings_menu_label, settings_shown_value(row)))
                return;
        settings_say((string_address)WORDS(settings_menu_keys));
        ui_page_redraw(&settings.page);
}

// The item's word (as the row has it) set, and the menu closed; a choice the row has already sends nothing.
static fn settings_menu_apply(int at)
{
        char word[SETTINGS_VALUE];
        int row = settings.page.menu_row;

        if (row < 0 || at < 0 || at >= UI_MENU_ITEMS)
        {
                settings_menu_close();
                return;
        }
        string_copy_bounded((string_address)word, (string_address)ui_page_menu_word(&settings.page, at), sizeof(word));
        settings_menu_close();
        if (settings_rows[row].output != SETTINGS_OUT_NONE)
                settings_output_apply(row, word);
        else if (!string_equals((string_address)word, (string_address)settings_shown_value(row)))
        {
                char value[SETTINGS_TYPED];

                settings_word_of(row, word, value, sizeof(value));
                settings_change(row, value);
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

// What a machine's name may hold, said as the verb says it: letters, digits and hyphens (the kit's field asks it of each).
static const char address_to settings_name_accepts(p32 code)
{
        return (code >= '0' && code <= '9') || ((code | 32) >= 'a' && (code | 32) <= 'z') || code == '-' ? null
                                                                                                     : WORDS(settings_name_chars);
}

/*
        A row typed in: the kit's field opened on what it holds now (text), with the row's rules -- a name's characters
        and its length, a passphrase's dots, the room its value has (one word or line is kept in what a row reads; what
        is typed for a list or a command is longer) -- and the keys in it, the caret at its end.
*/
static fn settings_edit_begin(int row, const char address_to text, b32 masked)
{
        struct ui_text address_to t = &settings.edit;

        // What it opens on is the document's own first text (kept here while it is open), so undo goes back to it and no further.
        string_copy_bounded((string_address)settings.edit_from, (string_address)text, sizeof(settings.edit_from));
        ui_field_begin(t, &settings.own, settings.edit_from, row);
        t->masked = masked;
        // The field has the keys from its first character, and a passphrase or a network's, device's or command's is a command:
        // a press outside it cancels (the kit's rule), where a plain value is kept.
        ui_field_focus(t, true);
        t->cancels = settings_item(row) != null;
        t->most =row >= (int)SETTINGS_LIST ? SETTINGS_ITEM_LABEL - 1
                  : row >= SETTINGS_ANY || (settings_rows[row].flags & SETTINGS_SPLIT) ? SETTINGS_TYPED - 1
                                                                                        : SETTINGS_VALUE - 1;
        if (row == SETTINGS_NAME_ROW)
                t->accept = settings_name_accepts, t->most = SETTINGS_NAME_MOST, t->most_said = WORDS(settings_name_long);
        ui_page_redraw(&settings.page);
}

// A field typed in is done with: closed, and a passphrase typed in it wiped (the kit's field close).
static fn settings_edit_end(void)
{
        ui_field_close(&settings.edit);
        ui_page_redraw(&settings.page);
}

// Whether the row typed in is in view, so that the well its field was last drawn in is where it is.
static b32 settings_edit_shown(void)
{
        int at = settings_pos(settings_editing()), top = at >= 0 ? ui_list_row_y(&settings.list, at) : 0;

        return at >= 0 && top + ui_px(&settings.ui, UI_ROW) > settings.list.y && top < settings.list.y + settings.list.h;
}

// A row of a list pressed: a network is joined (asking its passphrase when it has one), a device is said what can be done.
static fn settings_item_activate(int row, const struct settings_item address_to item)
{
        char line[SETTINGS_STATUS];

        if (item->kind == SETTINGS_NETWORK)
        {
                if (item->flags & SETTINGS_ITEM_JOINED)
                {
                        language_format(line, sizeof(line), WORDS(settings_already_joined), item->label);
                        settings_say((string_address)line);
                }
                else if (item->flags & SETTINGS_ITEM_CANNOT)
                        settings_say((string_address)WORDS(settings_cannot_join));
                else if (item->flags & SETTINGS_ITEM_OPEN)
                        settings_act('j', item->label, null);
                else
                {
                        settings_edit_begin(row, "", true);
                        string_copy_bounded((string_address)settings.joining, (string_address)item->label, sizeof(settings.joining));
                        settings_say((string_address)WORDS(settings_type_passphrase));
                }
        }
        else if (item->kind == SETTINGS_ADD)
        {
                settings_edit_begin(row, "", false);
                settings_say((string_address)WORDS(settings_type_device));
        }
        else
                settings_say((string_address)settings_item_text(item));
        ui_page_redraw(&settings.page);
}

// Delete on a saved network or a kept device forgets it, when it is pressed twice.
static fn settings_forget(void)
{
        const struct settings_item address_to item = settings_item(settings.list.focus);
        char line[SETTINGS_STATUS], text[SETTINGS_STATUS];

        if (!item || !(item->flags & SETTINGS_ITEM_SAVED) || (item->kind != SETTINGS_NETWORK && item->kind != SETTINGS_DEVICE))
                return;
        // A forget is armed by its kind (a number no row has, below zero) and the label it is for.
        if (!ui_confirm_ask(&settings.confirm, -1 - (int)item->kind, item->label))
        {
                language_format(text, sizeof(text), WORDS(settings_forget), item->label);
                ui_confirm_line(line, sizeof(line), text);
                settings_say((string_address)line);
                return;
        }
        settings_act(item->kind == SETTINGS_NETWORK ? 'f' : 'd', item->label, null);
}

// A row changed one way or the other: a switch thrown, a choice or a step moved, a field opened, an action run.
static fn settings_activate(int row, int way)
{
        const struct settings_item address_to item = settings_item(row);
        const struct settings_row address_to which = row < SETTINGS_ANY ? &settings_rows[row] : null;
        char next[SETTINGS_VALUE];

        // A line under a row (an issue, a status) is no row: Enter on it does nothing.
        if (row >= (int)SETTINGS_ALL)
                return;

        if (item)
        {
                settings_item_activate(row, item);
                return;
        }
        if (!which)
        {
                // A command with something to type opens a field; one without is run.
                if (settings_takes(row))
                        settings_edit_begin(row, "", false);
                else
                        settings_run(row, "");
                return;
        }
        if (!which->output && (settings.state[row] == SETTINGS_ABSENT || settings.state[row] == SETTINGS_UNREAD))
                return;

        switch (which->kind)
        {
        case SETTINGS_FIELD:
                settings_edit_begin(row, settings.value[row], false);
                settings_say((string_address)WORDS(settings_edit_keys));
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
        if (settings.list.focus >= (int)SETTINGS_ALL)
                settings_say((string_address) "");
        else if (settings.list.focus >= SETTINGS_ANY)
                settings_say((string_address)settings_text(settings.list.focus));
        else if (before >= SETTINGS_ANY)
                settings_say((string_address) "");
}

/*
        The rows of another group at the right, as the column of groups is
        moved: what is typed is cleared (it listed the rows of every group),
        and the focus goes to the first row, which is where the keys go
        when the rows are entered.
*/
static fn settings_filter_clear(void);
static b32 settings_filter_typed(void);

static fn settings_group_choose(int group)
{
        int last = settings_groups() - 1, before = settings.page.chosen;
        b32 typed = settings_filter_length() != 0 || settings_filter_typed();

        group = group < 0 ? 0 : group > last ? last : group;
        if (group == before && !typed)
                return;
        settings.page.chosen = group;
        settings.map.keys = false;
        settings.feed_due = 0;
        settings_filter_clear();
        ui_scroll_set(&settings.list.scroll, 0);
        ui_confirm_drop(&settings.confirm);
        settings_refilter();
        if (settings.list.count)
                settings.list.focus = settings.list.id[0];
        ui_page_column_show(&settings.ui, &settings.page);
        settings_say((string_address)(settings.list.focus >= SETTINGS_ANY ? settings_text(settings.list.focus) : ""));
        ui_page_redraw(&settings.page);
}

/*
        Enter in the field a row is typed in: what it holds is done with as the row says, its blanks at either end left
        off (a passphrase's are its own, and kept). A passphrase joins the network and a name keeps the device, neither
        kept here after; a command runs with what is typed, which may be nothing, the field staying when it asked for
        another Enter; a value kept as it was, or typed to what it is, is no change and sends nothing.
*/
static fn settings_edit_commit(void)
{
        char typed[SETTINGS_TYPED];
        positive length = ui_text_string(&settings.edit, (p8 address_to)typed, sizeof(typed)), start = 0;

        while (!settings.edit.masked && start < length && typed[start] == ' ')
                start++;
        while (!settings.edit.masked && length > start && typed[length - 1] == ' ')
                length--;
        typed[length] = 0;
        length -= start;
        if (settings_editing() >= (int)SETTINGS_LIST)
        {
                const struct settings_item address_to item = settings_item(settings_editing());

                if (!length)
                        settings_say((string_address)WORDS(settings_nothing_typed));
                else if (item && item->kind == SETTINGS_NETWORK)
                        settings_act('j', item->label, typed + start);
                else if (item && item->kind == SETTINGS_ADD)
                        settings_act('a', typed + start, null);
                memory_zero(typed, sizeof(typed));
                settings_edit_end();
                return;
        }
        if (settings_editing() >= SETTINGS_ANY)
        {
                if (settings_run(settings_editing(), typed + start))
                        settings_edit_end();
                return;
        }
        // What the field opened on is kept as it was: a value that changed under the field is not written back over with it.
        if (!length)
                settings_say((string_address)WORDS(settings_nothing_kept));
        else if (!string_equals((string_address)typed + start, (string_address)settings.edit_from) &&
                 !string_equals((string_address)typed + start, (string_address)settings.value[settings_editing()]))
                settings_change(settings_editing(), typed + start);
        settings_edit_end();
}

// What the field a row is typed in asked after an event: Enter keeps it, Escape leaves it, a refusal is said.
static fn settings_edit_asked(p32 asks)
{
        if (asks & UI_TEXT_REFUSED)
                settings_say((string_address)settings.edit.refused);
        if (asks & (UI_TEXT_SUBMIT | UI_TEXT_COMMIT))
                settings_edit_commit();
        else if (asks & UI_TEXT_CANCEL)
                settings_edit_end();
        else if (asks)
                ui_page_redraw(&settings.page);
}

// A key in a field being typed in: the field's (a key between a command's two Enters asks again).
static fn settings_type(const struct window_event address_to event)
{
        if (event->a != UI_KEY_ENTER && event->a != UI_KEY_ENTER_PAD)
                ui_confirm_drop(&settings.confirm);
        ui_field_focus(&settings.edit, true);
        settings_edit_asked(ui_field_event(&settings.ui, &settings.edit, &settings.popup, event, 0, 0));
}

// The filter's field, made the first time it is wanted: one line of the text engine, its clipboard this window's.
static struct ui_text address_to settings_filter_field(void)
{
        if (!settings.filter_open)
        {
                ui_field_open(&settings.filter_edit, &settings.own, "");
                settings.filter_edit.most = SETTINGS_FILTER - 1;
                settings.filter_edit.keeps_vertical = true; // Up and Down move the rows
                settings.filter_open = true;
        }
        return &settings.filter_edit;
}

// Whether the filter's field holds anything, a blank of its own as well.
static b32 settings_filter_typed(void)
{
        return settings.filter_open && doc_length(&settings.filter_edit.doc) != 0;
}

// Whether the filter's key changed with the last event (the kit keeps the key it last answered: a blank typed at the end is none).
static b32 settings_filter_take(void)
{
        return ui_text_search_changed(settings_filter_field());
}

// The filter emptied: its field, and the rows of the group chosen again.
static fn settings_filter_clear(void)
{
        if (settings.filter_open)
        {
                ui_text_set(&settings.filter_edit, (const p8 address_to) "", 0);
                ui_text_search_changed(&settings.filter_edit);
        }
}

/*
        What the filter's field asked after an event: a new filter lists the rows that match it from every group,
        with the keys on the first of them; Escape (its cancel) is the window's to do, and a refusal is said.
*/
static fn settings_filter_asked(p32 asks)
{
        if (asks & UI_TEXT_REFUSED)
                settings_say((string_address)settings.filter_edit.refused);
        if (!asks || (asks & (UI_TEXT_SUBMIT | UI_TEXT_CANCEL)))
                return;
        if (!settings_filter_take())
        {
                ui_page_redraw(&settings.page);
                return;
        }
        ui_scroll_set(&settings.list.scroll, 0);
        settings.page.pane = 1;
        settings_refilter();
        if (settings.list.count)
        {
                settings.list.focus = settings.list.id[0];
                ui_page_reveal(&settings.ui, &settings.page, &settings.list);
        }
}

// A key to the filter's field, which has the keys while no row is typed in: what it asked (0: not its key).
static p32 settings_filter_key(const struct window_event address_to event)
{
        struct ui_text address_to field = settings_filter_field();
        p32 asks;

        ui_field_focus(field, true);
        asks = ui_field_event(&settings.ui, field, &settings.popup, event, 0, 0);
        settings_filter_asked(asks);
        return asks;
}

/*
        Whether a key goes to the filter's field when it holds nothing: what starts a filter does (a character, a
        paste, an undo), and what works the rows does not -- Space, Enter, Escape, Tab, the arrows, Home, End, the
        pages and Delete, which are the rows' and the column's.
*/
static b32 settings_filter_starts(const struct window_event address_to event)
{
        switch (event->a)
        {
        case UI_KEY_ESCAPE: case UI_KEY_TAB: case UI_KEY_ENTER: case UI_KEY_ENTER_PAD: case UI_KEY_SPACE:
        case UI_KEY_HOME: case UI_KEY_UP: case UI_KEY_PAGE_UP: case UI_KEY_LEFT: case UI_KEY_RIGHT:
        case UI_KEY_END: case UI_KEY_DOWN: case UI_KEY_PAGE_DOWN: case UI_KEY_DELETE: case UI_KEY_BACKSPACE:
                return false;
        }
        return event->b != ' ';
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
        case UI_KEY_ESCAPE:
        case UI_KEY_ENTER:
        case UI_KEY_ENTER_PAD:
        case UI_KEY_SPACE:
        case UI_KEY_BACKSPACE:
                settings_card_leave();
                break;
        }
}

// A key: false when it closes the window.
// A screen written where it is put: the Rotation row's verb with its name and place (canvas display NAME place X Y), which a
// drag's release and a key's step both send. Nothing is sent when the row or the screen is not there.
static void settings_map_place_change(int at, int x, int y)
{
        int row = settings_output_row(SETTINGS_OUT_TURN);
        char value[SETTINGS_TYPED];
        positive length;

        if (row < 0 || at < 0 || !settings.own.table || at >= (int)settings_map_count())
                return;
        length = settings_map_add(value, sizeof(value), 0, settings.own.table->output_name[at]);
        length = settings_map_add(value, sizeof(value), length, " place ");
        length = settings_map_number(value, sizeof(value), length, x);
        length = settings_map_add(value, sizeof(value), length, " ");
        settings_map_number(value, sizeof(value), length, y);
        settings_change(row, value);
}

// The step a Shift and an arrow takes a screen by, in table pixels.
#define SETTINGS_MAP_STEP 10

/*
        Where the chosen screen goes for a key's step (dx, dy steps of SETTINGS_MAP_STEP), snapped to a neighbour's edge as a
        drag is, and answered as x and y. It starts from where it was last sent while the table has not caught up with that
        (so a held key takes a step each time, not the same one); a screen sent to where the table already has it is no longer
        held. Answers whether it moved.
*/
static b32 settings_map_stepped(int dx, int dy, int *x, int *y)
{
        struct settings_screen screens[WINDOWS_OUTPUTS];
        unsigned int number[WINDOWS_OUTPUTS] = {0}, count = settings_map_load(screens, number);
        int at = settings.map.display, base_x, base_y;

        if (!count || at < 0 || at >= (int)count)
                return false;
        if (settings.map.nudged && (settings.map.nudge_at != at || (screens[at].x == settings.map.nudge_x && screens[at].y == settings.map.nudge_y)))
                settings.map.nudged = false;
        base_x = settings.map.nudged ? settings.map.nudge_x : screens[at].x;
        base_y = settings.map.nudged ? settings.map.nudge_y : screens[at].y;
        *x = base_x + dx * SETTINGS_MAP_STEP, *y = base_y + dy * SETTINGS_MAP_STEP;
        settings_map_snap(screens, count, (unsigned int)at, x, y, COMPOSITOR_SNAP * (int)settings.own.table->scale / 100);
        if (*x == base_x && *y == base_y)
                return false;
        settings.map.nudged = true, settings.map.nudge_at = at, settings.map.nudge_x = *x, settings.map.nudge_y = *y;
        return true;
}

// The map's keys, while it has them: the arrows choose a screen by its number (Left and Up the one before, Right the one
// after, clamped), Shift with an arrow moves the chosen screen a step, Enter identifies the screens, and Down leaves the map
// for the rows. Answers whether it took the key; any other key leaves the map and goes on to the rows, as Tab and Escape do.
static b32 settings_map_key(const struct window_event address_to event)
{
        struct settings_screen screens[WINDOWS_OUTPUTS];
        unsigned int number[WINDOWS_OUTPUTS] = {0}, count = settings_map_load(screens, number);
        int at = settings.map.display, dx = 0, dy = 0, x, y, wanted;
        b32 shift = (event->flags & WINDOW_KEY_SHIFT) != 0;

        switch (event->a)
        {
        case UI_KEY_LEFT:
                dx = -1;
                break;
        case UI_KEY_RIGHT:
                dx = 1;
                break;
        case UI_KEY_UP:
                dy = -1;
                break;
        case UI_KEY_DOWN:
                dy = 1;
                break;
        case UI_KEY_ENTER:
        case UI_KEY_ENTER_PAD:
                settings_change(SETTINGS_JOB_IDENTIFY, "");
                return true;
        default:
                settings.map.keys = false;
                return false;
        }
        if (shift)
        {
                if (settings_map_stepped(dx, dy, &x, &y))
                        settings_map_place_change(at, x, y);
                ui_page_redraw(&settings.page);
                return true;
        }
        if (event->a == UI_KEY_DOWN)
        {
                settings.map.keys = false;
                ui_page_redraw(&settings.page);
                return true;
        }
        if (count && at >= 0 && at < (int)count)
        {
                wanted = (int)number[at] + (event->a == UI_KEY_LEFT || event->a == UI_KEY_UP ? -1 : 1);
                wanted = wanted < 1 ? 1 : wanted > (int)count ? (int)count : wanted;
                if (settings_map_numbered(number, count, (unsigned int)wanted) >= 0)
                        settings.map.display = settings_map_numbered(number, count, (unsigned int)wanted);
        }
        ui_page_redraw(&settings.page);
        return true;
}

// What the page's keys ask of the settings: a row's step and its action, the said line, the group the column chose, and Delete.
static const struct ui_page_keys settings_page_keys = {settings_steps, settings_activate, settings_said_of, settings_group_choose, settings_forget};

static b32 settings_key(const struct window_event address_to event)
{
        if (!(event->flags & WINDOW_KEY_DOWN))
                return true;
        // A choice's menu has every key: its popup has them while it is open, and a key that comes here is the menu's too.
        if (settings.page.menu_row >= 0)
        {
                settings_menu_answer(ui_popmenu_event(&settings.ui, &settings.popup.menu, event, 0, 0));
                return true;
        }
        if (settings_editing() >= 0)
        {
                settings_type(event);
                return true;
        }
        if (settings.page.card)
        {
                settings_card_key(event);
                return true;
        }
        // Delete on a kernel notice's line dismisses its class (the feeder starts it again from nothing); a line is no row, so the keys stop there.
        if (event->a == UI_KEY_DELETE && settings.list.focus >= (int)SETTINGS_ALL && settings.list.focus - (int)SETTINGS_ALL < settings_sub_count)
        {
                unsigned int code = settings_sub[settings.list.focus - (int)SETTINGS_ALL].code;

                if (code >= WINDOWS_ISSUE_KERNEL_GPU && code <= WINDOWS_ISSUE_KERNEL_PROGRAM)
                        windows_kernel_dismiss(settings.own.table, 1u << (code - WINDOWS_ISSUE_KERNEL_GPU));
                return true;
        }
        // Right opens the lines under the row the keys are on, and Left shuts them (an expander's keys); any other key is the row's.
        if ((event->a == UI_KEY_RIGHT || event->a == UI_KEY_LEFT) && settings.list.focus >= 0 && settings.list.focus < SETTINGS_ANY && settings_editing() < 0)
        {
                int expander = settings_expander(settings.list.focus);

                if (expander && (event->a == UI_KEY_RIGHT) == (expander == UI_ROW_EXPANDER_SHUT))
                {
                        settings_toggle(settings.list.focus);
                        ui_page_redraw(&settings.page);
                        return true;
                }
        }
        // A key between a confirm and its next press drops it: Enter or Space fires a command's, Delete a forget's.
        if (settings.confirm.what < 0 ? event->a != UI_KEY_DELETE
                                      : event->a != UI_KEY_ENTER && event->a != UI_KEY_ENTER_PAD && event->a != UI_KEY_SPACE)
                ui_confirm_drop(&settings.confirm);
        /*
                The search field has the keys while no row is typed in, and gets each key first: with something in it,
                every key (the caret's moves and selections, Backspace and Delete, a blank, the clipboard, undo; Enter
                and Escape come back as its submit and cancel), and with nothing in it what starts a filter. What it
                does not take (Up, Down, the pages, Tab, a chord) is the rows' and the column's, below.
        */
        if (settings_filter_typed() || settings_filter_starts(event))
        {
                p32 asks = settings_filter_key(event);

                if (asks & UI_TEXT_CANCEL)
                {
                        // What is typed is cleared first, a command running is stopped next, and only then the window closes.
                        settings_filter_clear();
                        settings_refilter();
                        return true;
                }
                if ((asks & UI_TEXT_SUBMIT) && settings_pos(settings.list.focus) >= 0)
                {
                        settings.page.pane = 1;
                        settings_activate(settings.list.focus, 1);
                }
                if (asks)
                        return true;
        }

        // The map has the keys while it has the focus (settings_map_key); what it does not take goes on to the rows below.
        if (settings.map.keys)
        {
                if (settings_map_on() && settings.page.pane && settings_map_key(event))
                        return true;
                settings.map.keys = false;
        }
        if (event->a == UI_KEY_ESCAPE)
                return settings_stop();
        // Up from the first row of the Display group goes up to the map above it, which has the keys then.
        if (settings.page.pane && event->a == UI_KEY_UP && settings_map_on() && settings.list.focus == settings_output_row(SETTINGS_OUT_SCREEN) &&
            settings_map_count())
        {
                settings.map.keys = true;
                ui_page_redraw(&settings.page);
                return true;
        }
        // The page's keys: Tab, the moves, Enter and Space, Delete and the arrows (what each row does is the app's, in settings_page_keys).
        ui_page_key(&settings.ui, &settings.page, &settings.list, (int)event->a, &settings_page_keys);
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
// The words of a row of tabs (the theme's, the motion's) as the language says them, one word each with a blank between, which
// a check holds the translations to; English is the row's own list.
static const char address_to settings_tab_words(const struct settings_row address_to which, int slot)
{
        static char shown[2][SETTINGS_VALUE];
        positive at = 0;

        if (!language.loaded)
                return which->choices;
        for (positive k = 0; k < 8; k++)
        {
                char each[SETTINGS_VALUE];
                const char address_to said;

                if (!settings_word(which->choices, k, each, sizeof(each)))
                        break;
                said = language_of(each);
                if (at + string_length((string_address)said) + 2 >= sizeof(shown[slot]))
                        break;
                if (at)
                        shown[slot][at++] = ' ';
                string_copy_bounded((string_address)shown[slot] + at, (string_address)said, sizeof(shown[slot]) - at);
                at += string_length((string_address)said);
        }
        return shown[slot];
}

static fn settings_describe(int id, struct ui_row address_to row)
{
        const struct settings_row address_to which = id < SETTINGS_ANY ? &settings_rows[id] : null;
        const struct settings_item address_to item = settings_item(id);
        unsigned int state = which ? settings.state[id] : SETTINGS_READ;
        const char address_to shown = which ? settings.value[id] : "";

        if (id >= (int)SETTINGS_ALL)
        {
                if (id - (int)SETTINGS_ALL < settings_sub_count)
                        settings_sub_describe(id - (int)SETTINGS_ALL, row);
                return;
        }
        row->section = settings_section_start(id);
        row->header = language_of(settings_section(id));
        row->label = settings_label_shown(id);
        if (which)
        {
                settings_badge(id, row);
                row->expander = settings_expander(id);
                // The chevron turns with the fold: pointing right shut, down open, and between them while the spring moves it.
                row->turn = settings_fold_at[id];
        }
        if (id == settings_editing())
        {
                const struct settings_generic address_to generic = settings_generic_of(id);

                row->kind = UI_ROW_FIELD, row->edit = &settings.edit;
                if (item)
                        row->label = item->kind == SETTINGS_ADD ? WORDS(settings_device_name) : settings.joining;
                else if (!which)
                {
                        // A command being typed: its verb, and the field for what follows it, which says what it takes.
                        const char address_to hint = generic ? settings_large->pool + generic->command + generic->verb : WORDS(settings_verb_hint);

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
                        row->note = item->flags & SETTINGS_ITEM_JOINED ? WORDS(settings_joined) : item->flags & SETTINGS_ITEM_SAVED ? WORDS(settings_saved) : null;
                        row->value = item->security[0] ? item->security : null;
                        row->bars = item->flags & SETTINGS_ITEM_HEARD ? item->bars : -1;
                        row->dim = !(item->flags & SETTINGS_ITEM_HEARD);
                }
                else if (item->kind == SETTINGS_DEVICE)
                        row->value = WORDS(settings_kept);
                row->dim |= item->kind == SETTINGS_NOTE;
                return;
        }
        if (!which)
        {
                row->value = settings_row_asks(id) ? WORDS(settings_asks_first) : null;
                return;
        }
        if (which->output != SETTINGS_OUT_NONE)
        {
                settings_describe_output(id, row);
                return;
        }
        row->dim = state == SETTINGS_UNREAD || state == SETTINGS_ABSENT;
        row->value = state == SETTINGS_UNREAD ? "..." : state == SETTINGS_ABSENT ? WORDS(settings_not_here)
                     : state == SETTINGS_FAILED || !shown[0] ? WORDS(settings_unknown) : settings_label_of(id, shown);
        if (state != SETTINGS_READ)
                return;
        if (which->kind == SETTINGS_SWITCH)
                row->kind = UI_ROW_SWITCH, row->on = string_equals((string_address)shown, (string_address) "on");
        else if (settings_row_is(id, SETTING_THEME_PATH) || settings_row_is(id, SETTING_MOTION_PATH))
                row->kind = UI_ROW_TABS, row->words = settings_tab_words(which, settings_row_is(id, SETTING_THEME_PATH) ? 0 : 1),
                row->at = (int)setting_place(which->choices, (string_address)shown);
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

/* ---- the pointer and a finger: the kit's page (ui_page_pointer) ---- */


// The map's drag: the screen taken moves with the pointer, snapped to a neighbour's edge within the compositor's snap band
// (COMPOSITOR_SNAP, in device pixels as the compositor draws it), and where it is let go is written on release.
static void settings_map_drag_to(int x, int y)
{
        struct settings_screen screens[WINDOWS_OUTPUTS];
        unsigned int number[WINDOWS_OUTPUTS] = {0}, count = settings_map_load(screens, number);
        struct settings_map_view view;
        int tx, ty;

        if (!count || settings.map.drag < 0 || settings.map.drag >= (int)count)
                return;
        settings_map_view_of(screens, count, settings.map.field_x, settings.map.field_y, settings.map.field_w, settings.map.field_h, &view);
        tx = (int)(settings_map_table_x(&view, x) - settings.map.grab_x);
        ty = (int)(settings_map_table_y(&view, y) - settings.map.grab_y);
        settings_map_snap(screens, count, (unsigned int)settings.map.drag, &tx, &ty, COMPOSITOR_SNAP * (int)settings.own.table->scale / 100);
        settings.map.target_x = tx, settings.map.target_y = ty;
        ui_page_redraw(&settings.page);
}

// A press in the map's panel: the button runs canvas identify, and a screen is chosen and taken to drag. True when the
// press was the map's, which it is anywhere in the panel.
static b32 settings_map_press(int x, int y)
{
        struct settings_screen screens[WINDOWS_OUTPUTS];
        unsigned int number[WINDOWS_OUTPUTS] = {0}, count;
        struct settings_map_view view;
        int at;

        if (x < settings.map.x || x >= settings.map.x + settings.map.w || y < settings.map.y || y >= settings.map.y + settings.map.h)
                return false;
        settings.map.keys = false; // a pointer has the map now, and its focus with it
        // ui_control_hit answers 0 for a hit on a button and -1 for a miss, so the test is that it hits, not that it answers.
        if (ui_control_hit(&settings.ui, &settings.map.identify, x, y) >= 0)
        {
                settings_change(SETTINGS_JOB_IDENTIFY, "");
                return true;
        }
        count = settings_map_load(screens, number);
        if ((at = count ? settings_map_at(x, y, screens, count) : -1) >= 0)
        {
                settings_map_view_of(screens, count, settings.map.field_x, settings.map.field_y, settings.map.field_w, settings.map.field_h, &view);
                settings.map.display = settings.map.drag = at, settings.map.nudged = false;
                settings.map.grab_x = (int)(settings_map_table_x(&view, x) - screens[at].x);
                settings.map.grab_y = (int)(settings_map_table_y(&view, y) - screens[at].y);
                settings.map.target_x = screens[at].x, settings.map.target_y = screens[at].y;
        }
        ui_page_redraw(&settings.page);
        return true;
}

// A drag let go: a screen that moved is written where it was dropped, as the Rotation row's verb with its name and the
// place; nothing is written for a drag back to where it was.
static void settings_map_release(void)
{
        struct settings_screen screens[WINDOWS_OUTPUTS];
        unsigned int number[WINDOWS_OUTPUTS] = {0}, count = settings_map_load(screens, number);
        int at = settings.map.drag;

        settings.map.drag = -1, settings.map.nudged = false;
        if (count && at >= 0 && at < (int)count && (settings.map.target_x != screens[at].x || settings.map.target_y != screens[at].y))
                settings_map_place_change(at, settings.map.target_x, settings.map.target_y);
        ui_page_redraw(&settings.page);
}

static fn settings_press_at(void);

static b32 settings_event(const struct window_event address_to event)
{
        if (gallery.on)
        {
                b32 redraw = false, keep = gallery_event(&settings.ui, settings.width, settings.height, &settings.popup, event, &redraw);

                if (redraw)
                        ui_page_redraw(&settings.page);
                return keep;
        }
        /*
                The map of the screens (above the Display rows): a press in its panel, the drag of a screen it took, and its
                release. A finger does the same on the map, a tap choosing a screen and a drag moving it, and a drag that starts
                on the map never scrolls the page under it; a pen is a pointer, and is a mouse here.
        */
        if (settings_map_on() && settings.page.menu_row < 0)
        {
                if (event->type == WINDOW_EVENT_MOTION && settings.map.drag >= 0)
                {
                        settings_map_drag_to(ui_kit.pointer_x, ui_kit.pointer_y);
                        return true;
                }
                if (event->type == WINDOW_EVENT_BUTTON && event->a == UI_POINTER_LEFT)
                {
                        if ((event->flags & WINDOW_KEY_DOWN) && settings_map_press(ui_kit.pointer_x, ui_kit.pointer_y))
                                return true;
                        if (!(event->flags & WINDOW_KEY_DOWN) && settings.map.drag >= 0)
                        {
                                settings_map_release();
                                return true;
                        }
                }
        }
        // A press anywhere else is the pointer's, and the keys leave the map for it.
        if (event->type == WINDOW_EVENT_BUTTON && (event->flags & WINDOW_KEY_DOWN))
                settings.map.keys = false;
        // The scrollbars take a pointer's motion, press and release on them, and a finger's on a thumb (the kit knows which it is).
        if ((event->type == WINDOW_EVENT_MOTION || event->type == WINDOW_EVENT_BUTTON) &&
            (event->type != WINDOW_EVENT_BUTTON || event->a == UI_POINTER_LEFT) &&
            ui_page_bars(&settings.ui, &settings.page, (int)event->type, (event->flags & WINDOW_KEY_DOWN) != 0))
                return true;
        /*
                The fields take the pointer on their wells (a caret, a drag, a double or triple click, the middle
                button's paste, the right button's menu), the motion and release of a drag they began, and the
                clipboard's answer to a paste they asked for: the field a row is typed in while it shows, and the
                search field. A press on the search field ends the typing in a row, as a press anywhere else does.
        */
        if (event->type != WINDOW_EVENT_KEY && event->type != WINDOW_EVENT_WHEEL && !settings.page.card && settings.page.menu_row < 0)
        {
                int x = ui_kit.pointer_x, y = ui_kit.pointer_y;
                p32 asks;

                /*
                        A press outside the field a row is typed in ends the typing, by the kit's one rule (ui_field_event answers
                        UI_TEXT_COMMIT for a plain value, UI_TEXT_CANCEL for a command): the press then goes on to the row it is on,
                        unless that is the row typed in, whose label a press only leaves.
                */
                if (settings_editing() >= 0 && (settings_edit_shown() || event->type != WINDOW_EVENT_MOTION || settings.edit.sel.dragging) &&
                    (asks = ui_field_event(&settings.ui, &settings.edit, &settings.popup, event, x, y)))
                {
                        b32 outside = event->type == WINDOW_EVENT_BUTTON && (asks == UI_TEXT_COMMIT || asks == UI_TEXT_CANCEL);
                        b32 own = outside && ui_page_target(&settings.ui, &settings.page, &settings.list, x, y) == settings_editing();

                        settings_edit_asked(asks);
                        if (!outside || own)
                        {
                                ui_page_redraw(&settings.page);
                                return true;
                        }
                }
                if (settings.filter_open && (asks = ui_field_event(&settings.ui, &settings.filter_edit, &settings.popup, event, x, y)))
                {
                        if (event->type != WINDOW_EVENT_BUTTON || (asks != UI_TEXT_COMMIT && asks != UI_TEXT_CANCEL))
                        {
                                settings_filter_asked(asks);
                                ui_page_redraw(&settings.page);
                                return true;
                        }
                }
        }
        switch (event->type)
        {
        case WINDOW_EVENT_KEY:
                return settings_key(event);
        case WINDOW_EVENT_MOTION:
        case WINDOW_EVENT_BUTTON:
                // The rows, the column, a press and a finger: the kit's page (its press is done by settings_press_at).
                ui_page_pointer(&settings.ui, &settings.page, &settings.list, event, settings_press_at);
                break;
        case WINDOW_EVENT_WHEEL:
                // The menu turns under the wheel, and nothing else does while it is open; else what is under the pointer.
                ui_page_wheel(&settings.ui, &settings.page, event);
                break;
        }
        return true;
}

// A press at the pointer, a mouse's at once and a finger's as it comes up where it went down: what it is on is done.
static fn settings_press_at(void)
{
        int x = ui_kit.pointer_x, y = ui_kit.pointer_y, row, before = settings.list.focus;

        // A press on the card of what a command said is done with it.
        if (settings.page.card)
        {
                settings_card_leave();
                return;
        }
        // A press on a group chooses it, rippling it; the keys are then in the column, and Enter goes to its rows.
        row = ui_page_column_hit(&settings.ui, &settings.page, x, y);
        if (row >= 0)
        {
                settings_edit_end();
                settings.page.pane = 0;
                settings_group_choose(row);
                ui_page_redraw(&settings.page);
                return;
        }
        // A row: the kit gives it the keys, shows it pressed and ripples it from the press.
        row = ui_list_pointer(&settings.ui, &settings.list, WINDOW_EVENT_BUTTON, x, y, true);
        if (settings_editing() >= 0 && row != settings_editing())
                settings_edit_end();
        if (row < 0)
                return;
        if (row >= (int)SETTINGS_ALL)
        {
                int at = row - (int)SETTINGS_ALL;

                if (at < settings_sub_count && settings_sub[at].action == SETTINGS_SUB_DISMISS_ALL)
                        windows_kernel_dismiss(settings.own.table, (1u << KERNEL_NOTICE_CLASSES) - 1);
                else
                        settings_copy(at);
                ui_page_redraw(&settings.page);
                return;
        }
        if (row < SETTINGS_ANY && ui_list_expander_at(&settings.ui, &settings.list, row, x))
        {
                settings_toggle(row);
                ui_page_redraw(&settings.page);
                return;
        }
        if (row != before)
        {
                ui_confirm_drop(&settings.confirm);
                settings_said_of(before);
        }
        settings.page.pane = 1;
        ui_page_redraw(&settings.page);
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
        if (settings_editing() != row)
                settings_activate(row, settings.list.way);
}

/* ---- the picture ---- */


/*
        The picture drawn into pixels, every row of it that lies in the band [band_top, band_bottom): the kit's page
        (its title, the search field that shows the filter, the status line), then the column of groups and the rows of
        the group chosen, or the card, and over them the menu a choice opened. settings_paint calls it only for a band
        with rows in it (ui_buffers_present calls it).
*/
// The map above the rows, drawn between the column and the rows: a panel the page's inset holds, when the map is shown.
static void settings_map_paint(struct ui address_to ui)
{
        if (settings_map_on())
                settings_map_draw(ui);
}

// The card's lines, in place of the rows when the page is a card.
static void settings_card_paint(struct ui address_to ui)
{
        ui_lines_draw(ui, &settings.card, ui_px(ui, UI_PAD), &settings.card_text);
}

static fn settings_draw_rows(unsigned int address_to pixels, int band_top, int band_bottom)
{
        struct ui address_to ui = &settings.ui;
        struct ui_page_parts parts;

        ui_paint_into(ui, pixels, settings.own.table->stride, settings.width, settings.height, band_top, band_bottom);
        if (gallery.on)
        {
                gallery_draw(ui);
                return;
        }
        settings.page.title = settings.page.card ? settings.card_title : language_of("Settings");
        settings.page.status = settings.status;
        settings.list.empty = WORDS(settings_nothing_matches);
        // The search field is the filter: the kit's bare field, the text engine's field in it (typing, a selection, the
        // clipboard, its caret), with the keys while the window has them and no row is typed in.
        settings.page.search.items[0] = settings.page.card ? null : WORDS(settings_filter_hint);
        settings.page.search.edit = settings.page.card ? null : settings_filter_field();
        memory_zero(&parts, sizeof(parts));
        parts.keys = settings_editing() < 0 && settings.focused;
        parts.group_name = settings_group_text;
        parts.groups = settings_groups();
        parts.column_keys = settings.focused && !settings.page.pane;
        parts.column_off = settings_filter_length() != 0;
        // A row typed in is a field on the page, not a band: its well is the only thing lit there. The map has the keys while it
        // is focused, so no row is lit or has the focus then.
        parts.list_lit = (settings.page.pane || settings_filter_length()) && settings_editing() < 0 && !settings.map.keys;
        parts.list_keys = settings.focused && settings.page.pane && !settings.map.keys;
        parts.list = &settings.list;
        parts.between = settings_map_paint;
        parts.card = settings_card_paint;
        // A finger's selection in the field a row is typed in, or in the filter: its handles and actions bar, over the rows.
        parts.overlays[0] = settings_editing() >= 0 && settings_edit_shown() ? &settings.edit : null;
        parts.overlays[1] = settings.filter_open && !settings.page.card ? &settings.filter_edit : null;
        ui_page_paint(ui, &settings.page, &parts);
}

/*
        The folds, one frame's step: each row whose spring moves takes its fold from it (ui_spring_at, the kit's spring), and the list is laid
        out again, so the rows under a folding line move with it. The damage starts at the topmost row that moved (its own place does not
        move, only what is under it). A row that has shut has no lines any more: the rows are listed again without them.
*/
static fn settings_fold_step(void)
{
        b32 moved = false, shut = false;
        int first = -1;

        for (int row = 0; row < SETTINGS_ANY; row++)
        {
                b64 target = settings_expanded[row] ? UI_ONE : 0, was = settings_fold_at[row], now;
                b32 live;
                int at;

                if (!settings_fold_live[row])
                        continue;
                live = ui_spring_at(&settings_fold_spring[row]);
                now = live ? target + settings_fold_spring[row].value[0] : target;
                now = now < 0 ? 0 : now > UI_ONE ? UI_ONE : now;
                settings_fold_at[row] = now;
                settings_fold_live[row] = live;
                ui_kit.busy |= live;
                if (!live && !target)
                        shut = true;
                if (now == was)
                        continue;
                moved = true;
                at = ui_list_at(&settings.list, row);
                if (at >= 0 && (first < 0 || ui_list_row_y(&settings.list, at) < first))
                        first = ui_list_row_y(&settings.list, at);
        }
        if (shut)
                settings_rebuild();
        else if (moved)
        {
                settings_layout();
                if (first >= 0)
                        settings_changed(first, settings.list.y + settings.list.h);
        }
}

/*
        One picture into the kit's buffers (ui_buffers_present draws the rows that changed or were spoiled, and shows the
        buffer): the picture is due until the compositor has taken the last one, and clean after it.
*/
static b32 settings_frame(void)
{
        settings_fold_step();
        if (!ui_buffers_present(&settings.page.buffers, &settings.own, settings.width, settings.height, settings_draw_rows, null))
                return false;
        settings.page.dirty = false;
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
        if (settings.page.card)
                ui_lines_wrap(&settings.ui, &settings.card_text, settings.width);
        settings_layout();
        ui_page_column_show(&settings.ui, &settings.page);
        // A choice's menu is a popup under its row, which the layout may have moved: it closes, and the row is asked again.
        ui_page_menu_close(&settings.page, &settings.popup);
        ui_page_redraw(&settings.page);
}

// The settings' start, once its window is taken: the lift, the layout and the first picture. -1 runs the loop.
// The status line holds a message of the language it was said in, so a change of language ends it.
static fn settings_language(void)
{
        settings_say((string_address) "");
        settings_gpu_key = 0xffffffffu; // the status line is said again in the new language at the next board tick
        // A language that reads the other way is a page laid out the other way: the column, the bars and the rows' extent are placed again.
        if (settings.width > 0 && settings.height > 0)
                settings_layout();
        ui_page_redraw(&settings.page);
}

// -n: the start that the notices popup makes (notices.c): Settings opens on its System group, where the kernel's notices are. A Settings
// already up is lifted and asked for the same page (ui_lift_page), its page hook choosing it.
static b32 settings_notices_asked;
static int settings_system_group(void)
{
        for (int at = 0; at < SETTINGS_ROWS; at++)
                if (settings_rows[at].output == SETTINGS_OUT_NOTICES)
                        return settings_group_of(at);
        return 0;
}

static void settings_asked(unsigned int page)
{
        settings_group_choose((int)page);
}

static int settings_open(void)
{
        struct windows address_to table = settings.own.table;
        unsigned int row = settings.own.row;

        if (!gallery.on && (settings_notices_asked ? ui_lift_page(&settings.own, (string_address) "settings", (unsigned int)settings_system_group())
                                                   : ui_lift_other(&settings.own, (string_address) "settings", false)))
                return 0;

        settings_refilter();
        if (settings_notices_asked)
                settings.page.chosen = settings_system_group();
        if (gallery.on)
        {
                int width = ui_px(&settings.ui, GALLERY_W), height = ui_px(&settings.ui, GALLERY_H);
                int room = (int)table->output_height - ui_px(&settings.ui, 2 * (24 + 24));

                gallery_build(&settings.own, settings_name_accepts, SETTINGS_NAME_MOST);
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

// An item of a field's context menu, done to the field it was opened for.
static fn settings_chose(int id)
{
        struct ui_text address_to t = settings.popup.field;

        // A choice's menu: the item it chose, or a negative id when it closed (the compositor's press outside, or Escape).
        if (!t && settings.page.menu_row >= 0)
        {
                if (id >= 0)
                        settings_menu_apply(id);
                else
                        settings_menu_close();
                return;
        }
        if (t == &settings.edit && settings_editing() >= 0)
                settings_edit_asked(ui_text_menu_do(&settings.ui, t, id));
        else if (t == &settings.filter_edit && settings_editing() < 0)
        {
                p32 asks = ui_text_menu_do(&settings.ui, t, id);

                settings_filter_asked(asks);
                ui_page_redraw(&settings.page);
        }
        else if (t && (t == &gallery.field[0] || t == &gallery.field[1]))
        {
                ui_text_menu_do(&settings.ui, t, id);
                ui_page_redraw(&settings.page);
        }
}

// The settings' timers: the caret's blink, a verb's time, the due reading of a feed, and the descriptor of a verb running.
// 0 runs the loop again.
static long settings_tick(long timeout, long address_to other)
{
        // The board, read once a second (settings_board_tick): when the rows' lines change, the page is laid out again.
        if (settings.list.ids)
        {
                long board = settings_board_tick();

                if (board >= 0 && (timeout < 0 || board < timeout))
                        timeout = board;
        }
        // The field with the keys blinks its caret a while after its last key, its well drawn again at each turn.
        if (!settings.page.card && (gallery.on || settings_editing() >= 0 || settings.filter_open))
        {
                struct ui_text address_to t = gallery.on ? &gallery.field[gallery.field[1].focused]
                                              : settings_editing() >= 0 ? &settings.edit : &settings.filter_edit;

                timeout = ui_field_tick(&settings.ui, &settings.page, t, timeout, gallery.on ? t->focused : settings.focused);
        }
        if (settings.child > 0 && !(settings.job_sets && settings.job >= SETTINGS_ANY))
        {
                positive now = (positive)ui_kit.now, left;
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
static struct ui_app settings_app = {
        .own = &settings.own,
        .ui = &settings.ui,
        .missing = (string_address) "settings: no window: the compositor's desktop is not up\n",
        .title = (string_address) "Settings",
        .name = (string_address) "settings",
        .glass = 0, // the kit's surface: the window is the theme's, opaque, as every kit window is
        .font = null,
        .nofont = (string_address) "settings: no font to draw with (-f FONT.ttf), pid ",
        .dirty = &settings.page.dirty,
        .focused = &settings.focused,
        .ground = &settings.ground,
        .open = settings_open,
        .page = settings_asked,
        .frame = settings_frame,
        .language = settings_language,
        .event = settings_event,
        .buffers = &settings.page.buffers,
        .size = settings_size,
        .tick = settings_tick,
        .ready = settings_drain,
        .close = settings_close,
        .popup = &settings.popup,
        .chose = settings_chose,
};

static b32 settings_main()
{
        memory_zero(&settings, sizeof(settings));
        settings.moonwater = (string_address) "/moonwater";
        ui_buffers_start(&settings.page.buffers);
        settings.output = -1;
        settings.page.pane = 1; // the keys start in the rows, on the first of them
        settings.page.menu_row = -1;

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
        for (positive at = 1; at < (positive)program_argument_count(); at++)
                if (string_equals(program_argument(at), (string_address) "-n"))
                        settings_notices_asked = true;
        // -g: the kit's gallery, on its own paper and not glass, with the mono for its tags and code.
        for (positive at = 1; at < (positive)program_argument_count(); at++)
                if (string_equals(program_argument(at), (string_address) "-g"))
                {
                        gallery.on = 1;
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
