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
        sets by hand: the machine's name, the keyboard, the time zone, how
        Canvas is drawn (its scale and screen modes, the desktop's colour
        and the size and look of the pointer), the clock and where the network sets it from, the
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
        canvas off, link remove and group leave, wifi and bluetooth remove,
        cpu offline, bind init and exit remove), whichever row it is typed
        in, is not run by the first Enter: the window says so and runs it
        when the same Enter is pressed again, and any other key asks again.

        It is glass (window.c, WINDOW_GLASS) drawn with the system's own
        pieces (ui.c). The groups are listed at the left, one for each
        section of rows and All commands last, and the rows of the group
        chosen are at the right, each section a header and its rows on the
        glass with no box or rule round them. The column of groups is a
        third of the window at most (it is never more than half of it, and
        a name too long for it is cut with three dots), and the keys are
        in it or in the rows, whichever the lit one is: the window opens
        with them in the rows, on the first.

        The keys. Tab and Shift-Tab go between the column and the rows.
        Up, Down, Home, End, Page Up and Page Down move in whichever has the
        keys, and moving in the column puts the rows of that group at the
        right at once. Enter or Right in the column go to its rows, on the
        first. In the rows, Enter or Space changes a row (a switch is
        thrown, a choice opens its list, a field is typed in and Enter
        keeps it, Escape leaves it as it was); Left and Right throw a
        switch off and on and move a step either way, and on any other row
        Left goes back to the column.
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

#define SETTINGS_SWITCH 1u
#define SETTINGS_CHOICE 2u
#define SETTINGS_FIELD 3u
#define SETTINGS_STEP 4u
#define SETTINGS_ACTION 5u
#define SETTINGS_COMMAND 6u // a row of what moonwater help says: its words are typed
#define SETTINGS_INFO 7u    // a value that only says, and that a person cannot set
// The rows of a list a verb's output makes (a network in range, a device kept), which come and go as it is read again.
#define SETTINGS_NETWORK 8u
#define SETTINGS_DEVICE 9u
#define SETTINGS_NOTE 10u // what the verb said that is no row: no networks heard, and why the machine is not joined
#define SETTINGS_ADD 11u  // the last row of a list that adds one by its name

// How a row's value is taken from the line it is on.
#define SETTINGS_LINE 1u  // the value is the rest of the line and not a word
#define SETTINGS_SPLIT 2u // what is set is the value's words, one each, and not one word
#define SETTINGS_TICK 4u  // the value changes by itself: it is asked again while its group is shown
#define SETTINGS_INK 8u   // a choice of named colours: the verb says rrggbb, and a name is what is shown and chosen

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

// In logical pixels, times the scale.
#define SETTINGS_WIDTH 700
#define SETTINGS_HEIGHT 520
#define SETTINGS_SIDE_MOST 170 // the groups' column, a third of the window at most
#define SETTINGS_SIDE_LEAST 88
#define SETTINGS_GROUP_ROW 34  // a group in that column
#define SETTINGS_GUTTER 14     // between the column and the rows
#define SETTINGS_MENU_ITEM 30  // a choice in the list a choice row opens
#define SETTINGS_MENU_PAD 6    // above the first and under the last
#define SETTINGS_MENU_LEAST 140 // the list's width, at least
#define SETTINGS_TOP 60     // the title's band
#define SETTINGS_HEADER 34  // a section's header over its card
#define SETTINGS_GAP 10     // under a card
#define SETTINGS_FOOT 40    // the lines that say what happened
#define SETTINGS_FIELD_W 220
#define SETTINGS_FILTER_W 190
#define SETTINGS_CARD_LINE 20
#define SETTINGS_CARD_PAD 10
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

struct settings_row
{
        const char address_to section; // the header this row starts a card under, or none
        const char address_to label;
        unsigned char kind;
        unsigned char lacks;           // a machine may have none: no key said is no value, not a failure
        unsigned char flags;           // SETTINGS_LINE, SETTINGS_SPLIT
        const char address_to read[3]; // what is asked, after moonwater
        const char address_to key;     // the words the value follows ("" is the whole first line)
        const char address_to also;    // and where it is when that is said with nothing after it
        const char address_to set[3];  // what changes it; the value goes after
        const char address_to choices; // a choice's words, or a step's two, a space between
        char stop;                     // a value ends here as well as at a blank, a comma, a semicolon or a bracket
        const char address_to marker;  // a line that has this says the value is auto, whatever the word after the key
};

static const struct settings_row settings_rows[] = {
    {"Machine", "Name", SETTINGS_FIELD, 0, SETTINGS_LINE, {"name"}, "", null, {"name"}, null, 0, null},
    {null, "Keyboard", SETTINGS_CHOICE, 0, 0, {"keyboard"}, "keyboard ", null, {"keyboard"}, "us uk de se no dk fi fr es it", 0, null},
    {null, "Time zone", SETTINGS_FIELD, 0, 0, {"timezone"}, "timezone ", null, {"timezone"}, null, 0, null},
    {"Canvas", "Scale", SETTINGS_CHOICE, 0, 0, {"canvas", "scale"}, "scale ", null, {"canvas", "scale"}, "auto 1 2 3 4", 0, "auto:"},
    {null, "Screen modes", SETTINGS_CHOICE, 0, 0, {"canvas", "modes"}, "modes ", null, {"canvas", "modes"}, "largest preferred", 0, null},
    {null, "Background", SETTINGS_CHOICE, 0, SETTINGS_INK, {"canvas", "background"}, "background ", null, {"canvas", "background"},
     "default black dark-grey slate midnight-blue forest plum light-grey white", 0, null},
    {null, "Background colour (rrggbb or default)", SETTINGS_FIELD, 0, 0, {"canvas", "background"}, "background ", null,
     {"canvas", "background"}, null, 0, null},
    {null, "Cursor size", SETTINGS_CHOICE, 0, 0, {"canvas", "cursor"}, "size ", null, {"canvas", "cursor", "size"}, "auto 1 2 3 4", 0, null},
    {null, "Cursor look", SETTINGS_CHOICE, 0, 0, {"canvas", "cursor"}, "look ", null, {"canvas", "cursor", "look"}, "auto light dark", 0,
     null},
    {"Time", "Set the clock from the network", SETTINGS_SWITCH, 0, 0, {"ntp"}, "ntp ", null, {"ntp"}, null, 0, null},
    {null, "Clock (Enter sets it now)", SETTINGS_ACTION, 0, SETTINGS_LINE | SETTINGS_TICK, {"time"}, "local ", null, {"time", "sync"}, null, 0, null},
    {null, "Time server (auto for the pool)", SETTINGS_FIELD, 0, 0, {"ntp", "server"}, "ntp server ", null, {"ntp", "server"}, null, 0, null},
    {null, "Keep the lowest-delay sample", SETTINGS_SWITCH, 0, 0, {"ntp", "sampling"}, "ntp sampling ", null, {"ntp", "sampling"}, null, 0, null},
    {null, "Authenticated time (NTS)", SETTINGS_CHOICE, 0, 0, {"ntp", "nts"}, "ntp nts ", null, {"ntp", "nts"}, "on off only", 0, null},
    {null, "NTS servers (or default)", SETTINGS_FIELD, 0, SETTINGS_LINE | SETTINGS_SPLIT, {"ntp", "nts"}, "servers ", null,
     {"ntp", "nts", "servers"}, null, ';', null},
    {"Network", "Wired", SETTINGS_SWITCH, 0, 0, {"wired"}, "wired ", null, {"wired"}, null, 0, null},
    {null, "Airplane mode", SETTINGS_SWITCH, 0, 0, {"airplane"}, "airplane ", null, {"airplane"}, null, 0, null},
    {null, "The internet prefers", SETTINGS_CHOICE, 0, 0, {"priority"}, "internet prefers ", null,
     {"priority", "internet"}, "wired wifi", 0, null},
    {null, "Names over TLS", SETTINGS_CHOICE, 0, 0, {"dns", "tls"}, "dns tls ", null, {"dns", "tls"}, "off on only", 0, null},
    {null, "Name servers (auto for the network)", SETTINGS_FIELD, 0, SETTINGS_LINE | SETTINGS_SPLIT, {"dns"}, "dns ", null, {"dns"}, null, '(', null},
    {"Wi-Fi", "Wi-Fi", SETTINGS_SWITCH, 0, 0, {"wifi"}, "wifi ", null, {"wifi"}, null, 0, null},
    {"Bluetooth", "Bluetooth", SETTINGS_SWITCH, 0, 0, {"bluetooth"}, "bluetooth ", null, {"bluetooth"}, null, 0, null},
    {"Power", "Battery", SETTINGS_INFO, 1, SETTINGS_LINE | SETTINGS_TICK, {"charge"}, "battery ", null, {null}, null, ';', null},
    {null, "Power profile", SETTINGS_CHOICE, 1, 0, {"power"}, "power ", "profile ", {"power"},
     "balanced performance powersave", 0, null},
    {null, "Brightness", SETTINGS_STEP, 1, 0, {"brightness"}, "brightness ", null, {"brightness"}, "-10 +10", 0, null},
    {null, "Boost", SETTINGS_SWITCH, 1, 0, {"cpu"}, "boost ", null, {"cpu", "boost"}, null, 0, null},
    {null, "Simultaneous threads (SMT)", SETTINGS_SWITCH, 1, 0, {"cpu"}, "smt ", null, {"cpu", "smt"}, null, 0, null},
    {null, "Charge limit (20 to 100, or off)", SETTINGS_FIELD, 1, 0, {"charge"}, "charging stops at ", null,
     {"charge", "limit"}, null, '%', null},
    {"Boot", "Start the desktop", SETTINGS_SWITCH, 1, 0, {"bind", "init", "canvas"}, "init canvas ", null, {"bind", "init", "canvas"},
     null, 0, null},
    {null, "Mount kept disks", SETTINGS_SWITCH, 1, 0, {"bind", "init", "mount"}, "init mount ", null, {"bind", "init", "mount"}, null,
     0, null},
};

#define SETTINGS_ROWS (sizeof(settings_rows) / sizeof(settings_rows[0]))
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
    {"canvas", "off"},
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
        What a row reads as from its verb's output and how the verb ended. A
        verb that said nothing and failed did not run (no moonwater where it
        was asked for, or it died before it said anything): that is not a
        machine that has none of the thing, so a row it lacks is unknown then.
*/
static unsigned int settings_read(const struct settings_row address_to row, const char address_to said, positive length,
                                  char address_to value, positive room, b32 failed)
{
        unsigned int state = settings_parse(row, said, length, value, room);

        if (state == SETTINGS_READ && (row->flags & SETTINGS_INK) && value[0] == '#')
                for (positive at = 0; at < sizeof(settings_inks) / sizeof(settings_inks[0]); at++)
                        if (string_equals((string_address)value + 1, (string_address)settings_inks[at].colour))
                        {
                                string_copy_bounded((string_address)value, (string_address)settings_inks[at].name, room);
                                break;
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
        font_face face[2];
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

        // The rows that show, in order, and where each is in the list, from the list's top.
        unsigned short visible[SETTINGS_ALL];
        positive visible_count;
        int top[SETTINGS_ALL];
        int content;            // how tall the list is
        int focus, editing, scroll;
        int group, group_scroll; // the group whose rows are at the right, and how far the column of groups is scrolled
        b32 pane;                // where the keys are: 0 the column of groups, 1 the rows

        // The list a choice opens: its row (none is -1), the choice the keys are on, the first in view, how many, how wide.
        int menu, menu_at, menu_top, menu_count, menu_width;
        char filter[SETTINGS_FILTER];
        positive filter_length;
        char typed[SETTINGS_TYPED];
        char status[SETTINGS_STATUS];
        int width, height, shown;
        unsigned int asked_w, asked_h, ground;
        int pointer_x, pointer_y;
        b32 focused, dirty;

        // What a command said, as a card in place of the list: lines cut to the window's width.
        b32 view;
        positive card_length;
        int card_lines, card_scroll;
        char card_title[SETTINGS_STATUS];

        // The command that asked for a second Enter: its row (one more than it, none is 0) and what was typed.
        int confirm;
        char confirm_args[SETTINGS_TYPED];

        // The verb being run: its process, the read end of its output, what it said, when it began, for which row.
        bipolar child;
        b32 output;
        positive said_length, began;
        int job;
        b32 job_sets, stopped;
        char job_value[SETTINGS_TYPED];
        positive unread;  // rows to ask, a bit each
        struct settings_change queue[SETTINGS_QUEUE]; // changes to make once the verb running is done, in order
        positive queued;
        b32 told;         // the first reading is said in the machine's log
} settings;

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
        mapped = compositor_call(syscall(mmap), 0, (sizeof(struct settings_large) + 4095) & ~(positive)4095, 3, 0x22, (positive)-1, 0);
        if (mapped < 0 && mapped > -4096)
                return false;
        settings_large = (struct settings_large address_to)mapped;
        return true;
}

static int settings_px(int logical)
{
        return logical * settings.ui.scale;
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
        for (positive at = 0; at < settings.visible_count; at++)
                if (settings.visible[at] == row)
                        return (int)at;
        return -1;
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

                for (positive at = 0; at < 3; at++)
                        if (settings_has(which->read[at], settings.filter, size) || settings_has(which->set[at], settings.filter, size))
                                return true;
                return settings_has(which->label, settings.filter, size) || settings_has(settings_section(row), settings.filter, size);
        }
        return settings_has(settings_label(row), settings.filter, size) || settings_has(settings_text(row), settings.filter, size) ||
               (row == SETTINGS_ANY && settings_has("anything", settings.filter, size));
}

// Where each visible row is: a header over the first of a section, a gap under the last.
static fn settings_layout(void)
{
        int y = 0;

        for (positive at = 0; at < settings.visible_count; at++)
        {
                int start = settings_section_start(settings.visible[at]);

                if (!at || settings_section_start(settings.visible[at - 1]) != start)
                        y += settings_px(SETTINGS_HEADER);
                settings.top[at] = y;
                y += settings_px(UI_ROW);
                if (at + 1 == settings.visible_count || settings_section_start(settings.visible[at + 1]) != start)
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

// The column of groups at the left: a third of the window, between a least and a most, and never more than half of it.
static int settings_side(void)
{
        int side = settings.width * 30 / 100;

        side = side < settings_px(SETTINGS_SIDE_LEAST) ? settings_px(SETTINGS_SIDE_LEAST) : side;
        side = side > settings_px(SETTINGS_SIDE_MOST) ? settings_px(SETTINGS_SIDE_MOST) : side;
        return side > settings.width / 2 ? settings.width / 2 : side;
}

// Where the rows are: from after the column to the window's margin.
static int settings_pane_left(void)
{
        return settings_px(UI_PAD) + settings_side() + settings_px(SETTINGS_GUTTER);
}

static int settings_pane_right(void)
{
        return settings.width - settings_px(UI_PAD);
}

// How tall the column of groups is, and how far it can be scrolled.
static int settings_side_height(void)
{
        return settings_groups() * settings_px(SETTINGS_GROUP_ROW);
}

static fn settings_side_scroll_to(int scroll)
{
        int most = settings_side_height() - (settings_list_bottom() - settings_list_top());

        most = most < 0 ? 0 : most;
        scroll = scroll > most ? most : scroll < 0 ? 0 : scroll;
        if (scroll != settings.group_scroll)
        {
                settings.group_scroll = scroll;
                settings.dirty = true;
        }
}

// The chosen group kept in sight in a column that does not fit.
static fn settings_side_reveal(void)
{
        int top = settings.group * settings_px(SETTINGS_GROUP_ROW), height = settings_px(SETTINGS_GROUP_ROW);
        int room = settings_list_bottom() - settings_list_top();

        if (top < settings.group_scroll)
                settings_side_scroll_to(top);
        else if (top + height > settings.group_scroll + room)
                settings_side_scroll_to(top + height - room);
        else
                settings_side_scroll_to(settings.group_scroll);
}

/*
        text at size cut to room at its end, with "..." in place of what is
        cut: the text itself when it fits, else fitted, which has room for a
        value or a label (SETTINGS_VALUE).
*/
static string_address settings_fit(struct ui address_to ui, string_address text, int size, int room, char address_to fitted,
                                   positive capacity)
{
        positive length = string_length(text);

        if (ui_measure(ui, text, size, false) <= room)
                return text;
        string_copy_bounded((string_address)fitted, text, capacity);
        while (length > 1)
        {
                length--;
                while (length > 1 && (((unsigned char)fitted[length]) & 0xc0) == 0x80)
                        length--;
                fitted[length] = 0;
                string_append_bounded((string_address)fitted, (string_address) "...", capacity);
                if (ui_measure(ui, (string_address)fitted, size, false) <= room)
                        break;
                fitted[length] = 0;
        }
        return (string_address)fitted;
}

// How tall what scrolls is: the list, or the card of what a command said.
static int settings_extent(void)
{
        return settings.view ? settings.card_lines * settings_px(SETTINGS_CARD_LINE) + 2 * settings_px(SETTINGS_CARD_PAD)
                             : settings.content;
}

static fn settings_scroll_to(int scroll)
{
        int most = settings_extent() - (settings_list_bottom() - settings_list_top());
        int address_to now = settings.view ? &settings.card_scroll : &settings.scroll;

        most = most < 0 ? 0 : most;
        scroll = scroll > most ? most : scroll;
        scroll = scroll < 0 ? 0 : scroll;
        if (scroll != address_to now)
        {
                address_to now = scroll;
                settings.dirty = true;
        }
}

// The row with the focus kept in sight.
static fn settings_reveal(void)
{
        int at = settings_pos(settings.focus);
        int top, height = settings_px(UI_ROW), room = settings_list_bottom() - settings_list_top();

        if (at < 0 || settings.view)
                return;
        top = settings.top[at];
        // A section's first row brings its header with it.
        if (!at || settings_section_start(settings.visible[at - 1]) != settings_section_start(settings.focus))
        {
                top -= settings_px(SETTINGS_HEADER);
                height += settings_px(SETTINGS_HEADER);
        }
        if (top < settings.scroll)
                settings_scroll_to(top);
        else if (top + height > settings.scroll + room)
                settings_scroll_to(top + height - room);
}

/*
        The rows that match the filter, laid out; the focus stays on its row
        when that still shows, and else goes to the first. The view stays
        where it is, which is what a list read again must not move.
*/
static fn settings_rebuild(void)
{
        positive count = 0;

        // What is typed lists the rows that mention it, in every group; with none typed, the rows of the group chosen.
        for (positive row = 0; row < settings_rows_count(); row++)
                if (settings.filter_length ? settings_matches((int)row) : settings_group_of((int)row) == settings.group)
                        settings.visible[count++] = (unsigned short)row;
        // The rows of a list come after, each in the group of its feed.
        for (int list = 0; list < SETTINGS_LISTS; list++)
                for (int at = 0; at < settings.item_count[list]; at++)
                {
                        int row = (int)SETTINGS_LIST + list * SETTINGS_LIST_ROWS + at;

                        if (settings.filter_length ? settings_matches(row) : settings_group_of(row) == settings.group)
                                settings.visible[count++] = (unsigned short)row;
                }
        settings.visible_count = count;
        if (settings.menu >= 0 && settings_pos(settings.menu) < 0)
                settings.menu = -1;
        if (settings_pos(settings.focus) < 0 && count)
                settings.focus = settings.visible[0];
        settings_layout();
        settings_scroll_to(settings.scroll);
        settings.dirty = true;
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

        if (room < 6)
                return 0;
        words[count++] = command;
        for (positive at = 0; at < 3 && verb[at]; at++)
                words[count++] = (string_address)verb[at];
        if (!asking && row->kind != SETTINGS_ACTION)
        {
                if (row->flags & SETTINGS_SPLIT)
                        return settings_tokens(value, settings_large->split, sizeof(settings_large->split), words, count, room);
                words[count++] = (string_address)value;
                // An ink is chosen by its name and set by its rrggbb.
                if (row->flags & SETTINGS_INK)
                        for (positive at = 0; at < sizeof(settings_inks) / sizeof(settings_inks[0]); at++)
                                if (string_equals((string_address)value, (string_address)settings_inks[at].name))
                                        words[count - 1] = (string_address)settings_inks[at].colour;
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
}

// A run of the card, measured as it is drawn.
static int settings_card_width(positive from, positive to)
{
        char probe[SETTINGS_PROBE] = {0};
        positive size = to - from;

        size = size >= sizeof(probe) ? sizeof(probe) - 1 : size;
        memory_copy(probe, settings_large->card + from, size);
        probe[size] = 0;
        return ui_measure(&settings.ui, (string_address)probe, UI_TEXT, false);
}

/*
        The card's lines cut to the window's width: each of what the verb
        said is as many as it takes, a break at a blank where it can be and
        else where the line has gone as far as it fits.
*/
static fn settings_wrap(void)
{
        int room = settings.width - 2 * settings_px(UI_PAD) - 2 * settings_px(14);
        positive at = 0;

        settings.card_lines = 0;
        while (at <= settings.card_length && settings.card_lines < SETTINGS_CARD_LINES)
        {
                positive over = at, start = at;

                while (over < settings.card_length && settings_large->card[over] != '\n')
                        over++;
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
                                if (settings_card_width(start, word) > room)
                                        break;
                                fit = next = word;
                        }
                        // One word wider than the window: as many of its bytes as fit.
                        if (fit == start && start < over)
                        {
                                fit = start + 1;
                                while (fit < over && settings_card_width(start, fit + 1) <= room)
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
}

// The card shown with the title of the command that made it.
static fn settings_card_open(const char address_to title)
{
        settings_card_fill();
        string_copy_bounded((string_address)settings.card_title, (string_address)title, sizeof(settings.card_title));
        settings.view = true;
        settings.card_scroll = 0;
        settings_wrap();
        settings.dirty = true;
}

static fn settings_card_leave(void)
{
        settings.view = false;
        settings.status[0] = 0;
        settings_scroll_to(settings.scroll);
        settings.dirty = true;
}

/* ---- the verbs ---- */

static fn settings_say(string_address text)
{
        string_copy_bounded((string_address)settings.status, text, sizeof(settings.status));
        settings.dirty = true;
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
        for (positive at = 0; at < 3; at++)
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
                settings.dirty = true;
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
        if (killed || settings.editing >= (int)SETTINGS_LIST || settings_feeds[feed].kind == SETTINGS_FEED_ROWS)
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
        if (settings_list_of(settings.focus) == list && settings_item(settings.focus))
                string_copy_bounded((string_address)keep, (string_address)settings_item(settings.focus)->label, sizeof(keep));
        at = settings.focus - (int)SETTINGS_LIST - list * SETTINGS_LIST_ROWS;

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
                settings.focus = (int)SETTINGS_LIST + list * SETTINGS_LIST_ROWS + (found < 0 ? 0 : found);
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
                settings.stopped = false;
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
                        settings.dirty |= settings.state[other] != was ||
                                          !string_equals((string_address)before, (string_address)settings.value[other]);
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
                compositor_kmsg((string_address) "settings: rows unread or not understood when first asked: ", failed,
                                (string_address) "");
        }
}

// A feed's reading under way is dropped, for a change that is wanted now.
static fn settings_feed_drop(void)
{
        if (settings.child > 0 && settings.job == SETTINGS_JOB_FEED)
                settings_finished(true);
}

// What the verb running has said, read; its end, heard.
static fn settings_drain(void)
{
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
        settings.stopped = true;
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

// How many choices a row has.
static positive settings_options(const struct settings_row address_to row)
{
        char word[SETTINGS_VALUE];
        positive count = 0;

        while (settings_word(row->choices, count, word, sizeof(word)))
                count++;
        return count;
}

static fn settings_menu_close(void)
{
        if (settings.menu < 0)
                return;
        settings.menu = -1;
        settings_say((string_address) "");
        settings.dirty = true;
}

/*
        Where the list is: x, y, width, height, and how many choices show.
        Under its row, right edges together, or over it when there is more
        room there; a list taller than either is cut and scrolls. False
        when its row is not in view.
*/
static b32 settings_menu_box(int address_to x, int address_to y, int address_to width, int address_to height, int address_to shown)
{
        int pos = settings_pos(settings.menu), item = settings_px(SETTINGS_MENU_ITEM), pad = settings_px(SETTINGS_MENU_PAD);
        int row_top, below, above, most, count = settings.menu_count;

        if (pos < 0)
                return false;
        row_top = settings_list_top() + settings.top[pos] - settings.scroll;
        below = settings.height - settings_px(8) - (row_top + settings_px(UI_ROW) - settings_px(4));
        above = row_top + settings_px(4) - settings_px(8);
        // Under the row, or over it when there is more room there; with room on neither side for all of it, slid up over
        // the row when it fits the window so, and else cut to the larger side.
        most = (below >= above ? below : above) - 2 * pad;
        most = most / item < 1 ? 1 : most / item;
        if (count > most && count * item + 2 * pad <= settings.height - settings_px(16))
                most = count;
        address_to shown = count < most ? count : most;
        address_to height = address_to shown * item + 2 * pad;
        address_to width = settings.menu_width > settings.width - settings_px(16) ? settings.width - settings_px(16) : settings.menu_width;
        address_to x = settings_pane_right() - settings_px(14) - address_to width;
        address_to x = address_to x < settings_px(8) ? settings_px(8) : address_to x;
        if (address_to height <= below)
                address_to y = row_top + settings_px(UI_ROW) - settings_px(4);
        else if (address_to height <= above)
                address_to y = row_top + settings_px(4) - address_to height;
        else
        {
                address_to y = settings.height - settings_px(8) - address_to height;
                address_to y = address_to y < settings_px(8) ? settings_px(8) : address_to y;
        }
        return true;
}

// The choice under a point of the window, or -1.
static int settings_menu_item(int px, int py)
{
        int x, y, width, height, shown, item = settings_px(SETTINGS_MENU_ITEM);

        if (settings.menu < 0 || !settings_menu_box(&x, &y, &width, &height, &shown) || px < x || px >= x + width ||
            py < y + settings_px(SETTINGS_MENU_PAD) || py >= y + height - settings_px(SETTINGS_MENU_PAD))
                return -1;
        return settings.menu_top + (py - y - settings_px(SETTINGS_MENU_PAD)) / item;
}

// The first choice in view moved to where the keys' choice shows, or by a wheel's steps.
static fn settings_menu_scroll(int top)
{
        int x, y, width, height, shown, most;

        if (!settings_menu_box(&x, &y, &width, &height, &shown))
                return;
        most = settings.menu_count - shown;
        settings.menu_top = top > most ? most : top < 0 ? 0 : top;
        // The keys' choice stays in view.
        if (settings.menu_at < settings.menu_top)
                settings.menu_at = settings.menu_top;
        else if (settings.menu_at >= settings.menu_top + shown)
                settings.menu_at = settings.menu_top + shown - 1;
        settings.dirty = true;
}

// The keys on choice at, kept in view.
static fn settings_menu_go(int at)
{
        int x, y, width, height, shown;

        at = at < 0 ? 0 : at >= settings.menu_count ? settings.menu_count - 1 : at;
        settings.menu_at = at;
        if (settings_menu_box(&x, &y, &width, &height, &shown))
        {
                if (at < settings.menu_top)
                        settings.menu_top = at;
                else if (at >= settings.menu_top + shown)
                        settings.menu_top = at - shown + 1;
        }
        settings.dirty = true;
}

static fn settings_menu_open(int row)
{
        const struct settings_row address_to which = &settings_rows[row];
        char word[SETTINGS_VALUE];
        int widest = 0;

        settings.menu = row;
        settings.focus = row;
        settings.menu_count = (int)settings_options(which);
        settings.menu_at = 0;
        settings.menu_top = 0;
        for (int at = 0; settings_word(which->choices, (positive)at, word, sizeof(word)); at++)
        {
                int wide = ui_measure(&settings.ui, (string_address)word, UI_TEXT, false);

                widest = wide > widest ? wide : widest;
                // The one the row has now is where the keys start; a value that is none of them starts at the first.
                if (string_equals((string_address)word, (string_address)settings.value[row]))
                        settings.menu_at = at;
        }
        settings.menu_width = widest + settings_px(12 + 6 + 10 + 18);
        settings.menu_width = settings.menu_width < settings_px(SETTINGS_MENU_LEAST) ? settings_px(SETTINGS_MENU_LEAST) : settings.menu_width;
        settings_reveal();
        settings_menu_go(settings.menu_at);
        settings_say((string_address) "Up and Down choose, Enter sets it, Escape leaves it");
        settings.dirty = true;
}

// The choice the keys are on, set, and the list closed; a choice the row has already sends nothing.
static fn settings_menu_apply(int at)
{
        const struct settings_row address_to which = &settings_rows[settings.menu];
        char word[SETTINGS_VALUE];
        int row = settings.menu;

        settings_menu_close();
        if (settings_word(which->choices, (positive)at, word, sizeof(word)) &&
            !string_equals((string_address)word, (string_address)settings.value[row]))
                settings_change(row, word);
}

// A key with the list open.
static fn settings_menu_key(const struct window_event address_to event)
{
        int x, y, width, height, shown = 1;
        unsigned int c = (unsigned int)event->b;

        settings_menu_box(&x, &y, &width, &height, &shown);
        switch (event->a)
        {
        case SETTINGS_KEY_ESCAPE:
                settings_menu_close();
                return;
        case SETTINGS_KEY_ENTER:
        case SETTINGS_KEY_ENTER_PAD:
        case SETTINGS_KEY_SPACE:
                settings_menu_apply(settings.menu_at);
                return;
        case SETTINGS_KEY_UP:
                settings_menu_go(settings.menu_at - 1);
                return;
        case SETTINGS_KEY_DOWN:
                settings_menu_go(settings.menu_at + 1);
                return;
        case SETTINGS_KEY_HOME:
                settings_menu_go(0);
                return;
        case SETTINGS_KEY_END:
                settings_menu_go(settings.menu_count - 1);
                return;
        case SETTINGS_KEY_PAGE_UP:
                settings_menu_go(settings.menu_at - shown);
                return;
        case SETTINGS_KEY_PAGE_DOWN:
                settings_menu_go(settings.menu_at + shown);
                return;
        }
        // A letter or a digit: the next choice after the keys' that starts with it, round to the first.
        if (c > ' ' && c < 0x7f && !(event->flags & (WINDOW_KEY_CONTROL | WINDOW_KEY_ALT)))
        {
                const struct settings_row address_to which = &settings_rows[settings.menu];
                char word[SETTINGS_VALUE];

                c = c >= 'A' && c <= 'Z' ? c + 32 : c;
                for (int step = 1; step <= settings.menu_count; step++)
                {
                        int at = (settings.menu_at + step) % settings.menu_count;

                        if (settings_word(which->choices, (positive)at, word, sizeof(word)) && (unsigned char)word[0] == c)
                        {
                                settings_menu_go(at);
                                return;
                        }
                }
        }
}

// A row changed one way or the other: a switch thrown, a choice or a step moved, a field opened, an action run.
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
        settings.dirty = true;
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
        settings.dirty = true;
}

// Delete on a saved network or a kept device forgets it, when it is pressed twice.
static fn settings_forget(void)
{
        const struct settings_item address_to item = settings_item(settings.focus);
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
                        settings.dirty = true;
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
                settings.dirty = true;
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

// The focus moved by whole rows of the ones that show; a command row has what it does in the status.
static fn settings_move(int by)
{
        int at = settings_pos(settings.focus), last = (int)settings.visible_count - 1, before = settings.focus;

        if (last < 0)
                return;
        at = (at < 0 ? 0 : at) + by;
        at = at < 0 ? 0 : at > last ? last : at;
        settings.focus = settings.visible[at];
        if (settings.focus != before)
        {
                settings.dirty = true;
                if (settings.focus >= SETTINGS_ANY)
                        settings_say((string_address)settings_text(settings.focus));
                else if (before >= SETTINGS_ANY)
                        settings_say((string_address) "");
        }
        settings_reveal();
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
        settings.scroll = 0;
        settings.confirm = 0;
        settings_refilter();
        if (settings.visible_count)
                settings.focus = settings.visible[0];
        settings_side_reveal();
        settings_say((string_address)(settings.focus >= SETTINGS_ANY ? settings_text(settings.focus) : ""));
        settings.dirty = true;
}

// The keys go to the rows of the group chosen.
static fn settings_rows_enter(void)
{
        settings.pane = 1;
        if (settings_pos(settings.focus) < 0 && settings.visible_count)
                settings.focus = settings.visible[0];
        settings_reveal();
        settings.dirty = true;
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
        settings.dirty = true;
}

// What is typed with no field open, as a letter of the filter, taken off by Backspace.
static fn settings_filter_key(const struct window_event address_to event)
{
        unsigned int c = (unsigned int)event->b;

        if (event->a == SETTINGS_KEY_BACKSPACE)
        {
                if (!settings.filter_length)
                        return;
                settings.filter[--settings.filter_length] = 0;
        }
        else
        {
                if (c <= ' ' || c >= 0x7f || (event->flags & (WINDOW_KEY_CONTROL | WINDOW_KEY_ALT)) ||
                    settings.filter_length + 1 >= sizeof(settings.filter))
                        return;
                settings.filter[settings.filter_length++] = (char)(c >= 'A' && c <= 'Z' ? c + 32 : c);
                settings.filter[settings.filter_length] = 0;
        }
        settings.scroll = 0;
        settings.pane = 1;
        settings_refilter();
        // The keys are on the first row that matches.
        if (settings.visible_count)
        {
                settings.focus = settings.visible[0];
                settings_reveal();
        }
}

// Whether Left and Right mean something on a row: a switch is thrown, a step moved.
static b32 settings_turns(int row)
{
        return row < SETTINGS_ANY && (settings_rows[row].kind == SETTINGS_SWITCH || settings_rows[row].kind == SETTINGS_STEP);
}

// A key on the card of what a command said: it scrolls, and Escape, Enter, Space or Backspace go back to the list.
static fn settings_card_key(const struct window_event address_to event)
{
        int line = settings_px(SETTINGS_CARD_LINE), page = settings_list_bottom() - settings_list_top();

        switch (event->a)
        {
        case SETTINGS_KEY_UP:
                settings_scroll_to(settings.card_scroll - line);
                break;
        case SETTINGS_KEY_DOWN:
                settings_scroll_to(settings.card_scroll + line);
                break;
        case SETTINGS_KEY_PAGE_UP:
                settings_scroll_to(settings.card_scroll - page);
                break;
        case SETTINGS_KEY_PAGE_DOWN:
                settings_scroll_to(settings.card_scroll + page);
                break;
        case SETTINGS_KEY_HOME:
                settings_scroll_to(0);
                break;
        case SETTINGS_KEY_END:
                settings_scroll_to(settings_extent());
                break;
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
        // The list a choice opened has every key.
        if (settings.menu >= 0)
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
                        settings_refilter();
                        return true;
                }
                return settings_stop();
        case SETTINGS_KEY_TAB:
                // Between the column of groups and the rows, whichever row or group is there.
                settings.pane = !settings.pane;
                if (settings.pane)
                        settings_rows_enter();
                settings.dirty = true;
                break;
        case SETTINGS_KEY_UP:
        case SETTINGS_KEY_DOWN:
        case SETTINGS_KEY_HOME:
        case SETTINGS_KEY_END:
        case SETTINGS_KEY_PAGE_UP:
        case SETTINGS_KEY_PAGE_DOWN:
        {
                int whole = (int)settings.visible_count, by;

                by = event->a == SETTINGS_KEY_UP ? -1 : event->a == SETTINGS_KEY_DOWN ? 1 : event->a == SETTINGS_KEY_HOME ? -whole
                     : event->a == SETTINGS_KEY_END ? whole : event->a == SETTINGS_KEY_PAGE_UP ? -5 : 5;
                if (settings.pane)
                        settings_move(by);
                else
                        settings_group_choose(event->a == SETTINGS_KEY_HOME ? 0
                                              : event->a == SETTINGS_KEY_END ? settings_groups() - 1
                                                                             : settings.group + (by < 0 ? -1 : 1) * (by > 1 || by < -1 ? 3 : 1));
                break;
        }
        case SETTINGS_KEY_ENTER:
        case SETTINGS_KEY_ENTER_PAD:
        case SETTINGS_KEY_SPACE:
                if (!settings.pane)
                        settings_rows_enter();
                else if (settings_pos(settings.focus) >= 0)
                        settings_activate(settings.focus, 1);
                break;
        case SETTINGS_KEY_DELETE:
                if (settings.pane)
                        settings_forget();
                break;
        case SETTINGS_KEY_LEFT:
                // A switch or a step is turned; on any other row Left goes back to the groups.
                if (settings.pane && settings_turns(settings.focus))
                        settings_activate(settings.focus, settings_rows[settings.focus].kind == SETTINGS_SWITCH ? -2 : -1);
                else if (settings.pane)
                {
                        settings.pane = 0;
                        settings.dirty = true;
                }
                break;
        case SETTINGS_KEY_RIGHT:
                if (!settings.pane)
                        settings_rows_enter();
                else if (settings_turns(settings.focus))
                        settings_activate(settings.focus, settings_rows[settings.focus].kind == SETTINGS_SWITCH ? 2 : 1);
                break;
        default:
                settings_filter_key(event);
        }
        return true;
}

// The row under a point of the window, or -1.
static int settings_hit(int x, int y)
{
        int list = y - settings_list_top() + settings.scroll;

        if (y < settings_list_top() || y >= settings_list_bottom() || x < settings_pane_left() || x >= settings_pane_right())
                return -1;
        for (positive at = 0; at < settings.visible_count; at++)
                if (list >= settings.top[at] && list < settings.top[at] + settings_px(UI_ROW))
                        return settings.visible[at];
        return -1;
}

// The group under a point of the window, or -1.
static int settings_group_hit(int x, int y)
{
        int group = (y - settings_list_top() + settings.group_scroll) / settings_px(SETTINGS_GROUP_ROW);

        if (y < settings_list_top() || y >= settings_list_bottom() || x < settings_px(UI_PAD) ||
            x >= settings_px(UI_PAD) + settings_side() || group >= settings_groups())
                return -1;
        return group;
}

static b32 settings_event(const struct window_event address_to event)
{
        switch (event->type)
        {
        case WINDOW_EVENT_KEY:
                return settings_key(event);
        case WINDOW_EVENT_MOTION:
        {
                int over;

                settings.pointer_x = event->a;
                settings.pointer_y = event->b;
                // The pointer over a choice of the list puts the keys' choice there.
                over = settings.menu >= 0 ? settings_menu_item(event->a, event->b) : -1;
                if (over >= 0 && over != settings.menu_at && over < settings.menu_count)
                {
                        settings.menu_at = over;
                        settings.dirty = true;
                }
                break;
        }
        case WINDOW_EVENT_BUTTON:
        {
                int row;

                if (!(event->flags & WINDOW_KEY_DOWN) || event->a != SETTINGS_BUTTON_LEFT)
                        break;
                // A press on a choice of the list sets it; a press anywhere else only closes the list.
                if (settings.menu >= 0)
                {
                        int chosen = settings_menu_item(settings.pointer_x, settings.pointer_y);

                        if (chosen >= 0 && chosen < settings.menu_count)
                                settings_menu_apply(chosen);
                        else
                                settings_menu_close();
                        break;
                }
                // A press on the card of what a command said is done with it.
                if (settings.view)
                {
                        settings_card_leave();
                        break;
                }
                // A press on a group chooses it; the keys are then in the column, and Enter goes to its rows.
                row = settings_group_hit(settings.pointer_x, settings.pointer_y);
                if (row >= 0)
                {
                        settings_edit_end();
                        settings.pane = 0;
                        settings_group_choose(row);
                        settings.dirty = true;
                        break;
                }
                row = settings_hit(settings.pointer_x, settings.pointer_y);
                if (settings.editing >= 0 && row != settings.editing)
                        settings_edit_end();
                if (row < 0)
                        break;
                if (row != settings.focus)
                {
                        settings.confirm = 0;
                        if (row >= SETTINGS_ANY)
                                settings_say((string_address)settings_text(row));
                        else if (settings.focus >= SETTINGS_ANY)
                                settings_say((string_address) "");
                }
                settings.focus = row;
                settings.pane = 1;
                settings.dirty = true;
                if (settings.editing != row)
                {
                        // A step pressed left of its value's middle goes back.
                        int middle = settings_pane_right() - settings_px(14 + 40);

                        settings_activate(row, settings_kind(row) == SETTINGS_STEP && settings.pointer_x < middle ? -1 : 1);
                }
                break;
        }
        case WINDOW_EVENT_WHEEL:
                // The list scrolls under the wheel, and nothing else does while it is open.
                if (settings.menu >= 0)
                {
                        if (!event->b)
                        {
                                // A notch is 120; a smaller turn is still a step.
                                int notches = event->a / 120 ? event->a / 120 : event->a > 0 ? 1 : event->a < 0 ? -1 : 0;

                                settings_menu_scroll(settings.menu_top - notches);
                        }
                        break;
                }
                // The column of groups or the rows, whichever is under the pointer; a card has all the window.
                if (!event->b && !settings.view && settings.pointer_x < settings_pane_left() - settings_px(SETTINGS_GUTTER) / 2)
                        settings_side_scroll_to(settings.group_scroll - event->a * settings_px(SETTINGS_GROUP_ROW) / 120);
                else if (!event->b)
                        settings_scroll_to((settings.view ? settings.card_scroll : settings.scroll) -
                                           event->a * settings_px(UI_ROW) / 120);
                break;
        }
        return true;
}

/* ---- the picture ---- */

/*
        A row of a list: a network (a dot when it is the one joined, its
        name, what it is as a word, what it asks for and the bars of its
        signal), a device kept, the line the verb said that is no row, or
        the row that keeps another device; with its field when a passphrase
        or a name is being typed, shown as dots when it is a passphrase.
*/
static fn settings_draw_item(struct ui address_to ui, int at, int y, const struct settings_item address_to item)
{
        int left = settings_pane_left(), right = settings_pane_right(), inset = settings_px(14);
        int height = settings_px(UI_ROW), baseline = ui_baseline(ui, y, height, UI_TEXT), edge = right - inset, gap = settings_px(10);
        int text_left = left + inset;
        char cut[SETTINGS_ITEM_LABEL], mask[SETTINGS_ITEM_LABEL * 3 + 1];
        const char address_to words;

        if (at == settings.editing)
        {
                int width = settings_px(SETTINGS_FIELD_W), wide;
                string_address label = item->kind == SETTINGS_ADD ? (string_address) "Name of the device" : (string_address)settings.joining;
                positive dots = 0;

                wide = ui_text(ui, text_left, baseline,
                               settings_fit(ui, label, UI_TEXT, (right - left) / 2, cut, sizeof(cut)), UI_TEXT, false, ui->ink, 255);
                width = width > right - inset - (text_left + wide + gap) ? right - inset - (text_left + wide + gap) : width;
                width = width < settings_px(100) ? settings_px(100) : width;
                // A passphrase is shown as a dot for each character, and never as it is.
                if (item->kind == SETTINGS_NETWORK)
                {
                        for (positive each = 0; settings.typed[each] && dots + 1 < SETTINGS_ITEM_LABEL; each++)
                                if (((unsigned char)settings.typed[each] & 0xc0) != 0x80)
                                {
                                        memory_copy(mask + dots * 3, "\xe2\x80\xa2", 3);
                                        dots++;
                                }
                        mask[dots * 3] = 0;
                }
                ui_field(ui, edge - width, y + settings_px(7), width, height - settings_px(14),
                         item->kind == SETTINGS_NETWORK ? (string_address)mask : (string_address)settings.typed, true);
                return;
        }

        if (item->kind == SETTINGS_NETWORK)
        {
                unsigned int alpha = item->flags & SETTINGS_ITEM_HEARD ? 255 : 150;

                if (item->flags & SETTINGS_ITEM_HEARD)
                {
                        ui_signal(ui, edge, y + height / 2, item->bars);
                        edge -= settings_px(18) + gap;
                }
                if (item->security[0])
                {
                        edge -= ui_text(ui, edge - ui_measure(ui, (string_address)item->security, UI_SMALL, false), baseline,
                                        (string_address)item->security, UI_SMALL, false, ui->quiet, alpha) + gap;
                }
                words = item->flags & SETTINGS_ITEM_JOINED ? "joined" : item->flags & SETTINGS_ITEM_SAVED ? "saved" : null;
                if (words)
                        edge -= ui_text(ui, edge - ui_measure(ui, (string_address)words, UI_SMALL, false), baseline, (string_address)words,
                                        UI_SMALL, false, item->flags & SETTINGS_ITEM_JOINED ? ui->ink : ui->quiet, 255) + gap;
                if (item->flags & SETTINGS_ITEM_JOINED)
                {
                        ui_round(ui, text_left, y + (height - settings_px(7)) / 2, settings_px(7), settings_px(7), settings_px(3),
                                 ui_tone(ui->ink, 235));
                        text_left += settings_px(17);
                }
                ui_text(ui, text_left, baseline, settings_fit(ui, (string_address)item->label, UI_TEXT, edge - text_left, cut, sizeof(cut)),
                        UI_TEXT, false, ui->ink, alpha);
                return;
        }
        if (item->kind == SETTINGS_DEVICE)
        {
                edge -= ui_text(ui, edge - ui_measure(ui, (string_address) "kept", UI_SMALL, false), baseline, (string_address) "kept",
                                UI_SMALL, false, ui->quiet, 255) + gap;
                ui_text(ui, text_left, baseline, settings_fit(ui, (string_address)item->label, UI_TEXT, edge - text_left, cut, sizeof(cut)),
                        UI_TEXT, false, ui->ink, 255);
                return;
        }
        ui_text(ui, text_left, baseline, settings_fit(ui, (string_address)item->label, UI_TEXT, edge - text_left, cut, sizeof(cut)),
                UI_TEXT, false, item->kind == SETTINGS_NOTE ? ui->quiet : ui->ink, 255);
}

static fn settings_draw_row(struct ui address_to ui, int at, int y)
{
        const struct settings_row address_to row = at < SETTINGS_ANY ? &settings_rows[at] : null;
        int left = settings_pane_left(), right = settings_pane_right(), inset = settings_px(14);
        int height = settings_px(UI_ROW), baseline = ui_baseline(ui, y, height, UI_TEXT);
        char cut[SETTINGS_VALUE];
        unsigned int state = row ? settings.state[at] : SETTINGS_READ;
        b32 usable = state == SETTINGS_READ || state == SETTINGS_FAILED;
        string_address shown = row ? (string_address)settings.value[at] : (string_address) "";
        int wide;

        if (at == settings.focus && (settings.pane || settings.filter_length))
                ui_focus(ui, left + settings_px(3), y + settings_px(2), right - left - settings_px(6), height - settings_px(4),
                         settings.focused && settings.pane);

        if (settings_item(at))
        {
                settings_draw_item(ui, at, y, settings_item(at));
                return;
        }

        // A command being typed: its verb at the left, and the field after it for what follows.
        if (!row && at == settings.editing)
        {
                const struct settings_generic address_to generic = settings_generic_of(at);
                char head[SETTINGS_VALUE];
                int x, width;

                if (generic)
                {
                        memory_copy(head, settings_large->pool + generic->command, generic->verb);
                        head[generic->verb] = 0;
                }
                else
                        string_copy_bounded((string_address)head, (string_address) "moonwater", sizeof(head));
                wide = ui_text(ui, left + inset, baseline,
                               settings_fit(ui, (string_address)head, UI_TEXT, (right - left) / 3, cut, sizeof(cut)), UI_TEXT, false,
                               ui->ink, 255);
                x = left + inset + wide + settings_px(10);
                width = right - inset - x;
                if (width < settings_px(100))
                {
                        width = settings_px(100);
                        x = right - inset - width;
                }
                ui_field(ui, x, y + settings_px(7), width, height - settings_px(14), (string_address)settings.typed, true);
                if (!settings.typed[0])
                {
                        const char address_to hint = generic ? settings_large->pool + generic->command + generic->verb : "VERB [ARGUMENTS]";

                        while (*hint == ' ')
                                hint++;
                        ui_run(ui, x + settings_px(8), baseline, (string_address)hint, UI_TEXT, false, ui->quiet, 150,
                               x + settings_px(8), x + width - settings_px(8));
                }
                return;
        }

        // A label leaves its value a third of the row at least: a long one is cut, and the value is not.
        {
                int room = right - left - 2 * inset - (row && row->kind == SETTINGS_SWITCH ? settings_px(UI_SWITCH_W) : (right - left) / 3);

                wide = ui_text(ui, left + inset, baseline,
                               settings_fit(ui, (string_address)settings_label(at), UI_TEXT, room < settings_px(40) ? settings_px(40) : room,
                                            cut, sizeof(cut)),
                               UI_TEXT, false, ui->ink, usable ? 255 : 130);
        }

        if (!row)
        {
                if (settings_row_asks(at))
                        ui_value(ui, right - inset, baseline, (string_address) "asks first", UI_VALUE_PLAIN, true);
                return;
        }

        if (state == SETTINGS_UNREAD)
                shown = (string_address) "...";
        else if (state == SETTINGS_ABSENT)
                shown = (string_address) "not on this machine";
        else if (state == SETTINGS_FAILED || !shown[0])
                shown = (string_address) "unknown";

        if (at == settings.editing)
        {
                int width = settings_px(SETTINGS_FIELD_W), room = right - inset - (left + inset + wide + settings_px(10));

                // A long label leaves the field what room it has.
                width = width > room ? (room < settings_px(100) ? settings_px(100) : room) : width;
                ui_field(ui, right - inset - width, y + settings_px(7), width, height - settings_px(14),
                         (string_address)settings.typed, true);
                return;
        }

        if (row->kind == SETTINGS_SWITCH && state == SETTINGS_READ)
                ui_switch(ui, right - inset, y + height / 2, string_equals(shown, (string_address) "on"), true);
        else
        {
                // A value longer than the room the label leaves is cut at its end and says so.
                int room = right - inset - (left + inset + wide + settings_px(16));

                shown = settings_fit(ui, shown, UI_TEXT, room, cut, sizeof(cut));
                ui_value(ui, right - inset, baseline, shown,
                         state != SETTINGS_READ ? UI_VALUE_PLAIN : row->kind == SETTINGS_CHOICE ? UI_VALUE_LIST
                                                                  : row->kind == SETTINGS_STEP   ? UI_VALUE_STEP
                                                                                                 : UI_VALUE_PLAIN,
                         usable);
        }
}

// The list: the rows that show, a header to a section, drawn where they are in view.
// The column of groups at the left: the chosen one lit, the rest plain on the glass.
static fn settings_draw_side(struct ui address_to ui)
{
        int left = settings_px(UI_PAD), width = settings_side(), height = settings_px(SETTINGS_GROUP_ROW);
        int inset = settings_px(12);
        char cut[SETTINGS_VALUE];

        ui->top = settings_list_top();
        ui->bottom = settings_list_bottom();
        for (int group = 0; group < settings_groups(); group++)
        {
                int y = settings_list_top() + group * height - settings.group_scroll;
                // What is typed lists the rows of every group, so none is the chosen one.
                b32 chosen = group == settings.group && !settings.filter_length;

                if (y + height <= ui->top || y >= ui->bottom)
                        continue;
                if (chosen)
                        ui_focus(ui, left, y + settings_px(2), width, height - settings_px(4), settings.focused && !settings.pane);
                ui_text(ui, left + inset, ui_baseline(ui, y, height, UI_TEXT),
                        settings_fit(ui, (string_address)settings_group_name(group), UI_TEXT, width - 2 * inset, cut, sizeof(cut)),
                        UI_TEXT, chosen, ui->ink, chosen ? 255 : 210);
        }
}

static fn settings_draw_list(struct ui address_to ui)
{
        int left = settings_pane_left();
        int list_top = settings_list_top(), list_bottom = settings_list_bottom();
        positive at = 0;

        ui->top = list_top;
        ui->bottom = list_bottom;
        if (!settings.visible_count)
        {
                ui_text(ui, left + settings_px(14), ui_baseline(ui, list_top, settings_px(UI_ROW), UI_TEXT),
                        (string_address) "Nothing matches what is typed", UI_TEXT, false, ui->quiet, 255);
                return;
        }
        while (at < settings.visible_count)
        {
                int start = settings_section_start(settings.visible[at]);
                positive last = at;
                int y = list_top + settings.top[at] - settings.scroll, rows;

                while (last + 1 < settings.visible_count && settings_section_start(settings.visible[last + 1]) == start)
                        last++;
                rows = (int)(last - at + 1) * settings_px(UI_ROW);
                // A section with some of it in view: its header and the rows of it in view, plain on the glass.
                if (y + rows > list_top - settings_px(SETTINGS_HEADER) && y - settings_px(SETTINGS_HEADER) < list_bottom)
                {
                        ui_header(ui, left + settings_px(6), y - settings_px(SETTINGS_HEADER), settings_px(SETTINGS_HEADER),
                                  (string_address)settings_section(settings.visible[at]));
                        for (positive each = at; each <= last; each++)
                        {
                                int row_y = list_top + settings.top[each] - settings.scroll;

                                if (row_y + settings_px(UI_ROW) <= list_top || row_y >= list_bottom)
                                        continue;
                                settings_draw_row(ui, settings.visible[each], row_y);
                        }
                }
                at = last + 1;
        }
}

// What a command said, as the lines of the card.
static fn settings_draw_card(struct ui address_to ui)
{
        int left = settings_px(UI_PAD);
        int line = settings_px(SETTINGS_CARD_LINE), pad = settings_px(SETTINGS_CARD_PAD);
        int top = settings_list_top() - settings.card_scroll;
        char text[SETTINGS_PROBE];

        ui->top = settings_list_top();
        ui->bottom = settings_list_bottom();
        for (int at = 0; at < settings.card_lines; at++)
        {
                int y = top + pad + at * line;
                positive size = settings_large->card_size[at];

                if (y + line <= ui->top || y >= ui->bottom)
                        continue;
                size = size >= sizeof(text) ? sizeof(text) - 1 : size;
                memory_copy(text, settings_large->card + settings_large->card_at[at], size);
                text[size] = 0;
                ui_text(ui, left + settings_px(14), ui_baseline(ui, y, line, UI_TEXT), (string_address)text, UI_TEXT, false, ui->ink,
                        255);
        }
}

// The status line, or two where it does not fit one.
static fn settings_draw_foot(struct ui address_to ui, int top)
{
        int left = settings_px(UI_PAD) + settings_px(6), room = settings.width - left - settings_px(UI_PAD);
        int half = settings_px(SETTINGS_FOOT) / 2;
        char first[SETTINGS_STATUS];
        positive length = string_length((string_address)settings.status), cut = 0;

        ui->top = top;
        ui->bottom = settings.height;
        if (ui_measure(ui, (string_address)settings.status, UI_SMALL, false) <= room)
        {
                ui_text(ui, left, ui_baseline(ui, top, settings_px(SETTINGS_FOOT), UI_SMALL), (string_address)settings.status,
                        UI_SMALL, false, ui->quiet, 255);
                return;
        }
        // The longest start that fits, ending at a blank.
        for (positive at = 1; at < length; at++)
        {
                if (settings.status[at] != ' ')
                        continue;
                memory_copy(first, settings.status, at);
                first[at] = 0;
                if (ui_measure(ui, (string_address)first, UI_SMALL, false) > room)
                        break;
                cut = at;
        }
        if (!cut)
                cut = length;
        memory_copy(first, settings.status, cut);
        first[cut] = 0;
        ui_text(ui, left, ui_baseline(ui, top, half, UI_SMALL), (string_address)first, UI_SMALL, false, ui->quiet, 255);
        ui_text(ui, left, ui_baseline(ui, top + half, half, UI_SMALL), (string_address)settings.status + cut + (cut < length),
                UI_SMALL, false, ui->quiet, 255);
}

// The list a choice opened, over everything else: the choices in view, the keys' lit, the value's dotted.
static fn settings_draw_menu(struct ui address_to ui)
{
        int x, y, width, height, shown, item = settings_px(SETTINGS_MENU_ITEM), pad = settings_px(SETTINGS_MENU_PAD);
        const struct settings_row address_to which = &settings_rows[settings.menu];
        char word[SETTINGS_VALUE];

        if (!settings_menu_box(&x, &y, &width, &height, &shown))
                return;
        ui->top = 0;
        ui->bottom = settings.height;
        ui_menu(ui, x, y, width, height);
        for (int at = 0; at < shown; at++)
        {
                int which_at = settings.menu_top + at;

                if (!settings_word(which->choices, (positive)which_at, word, sizeof(word)))
                        break;
                ui_option(ui, x + settings_px(4), y + pad + at * item, width - settings_px(8), item, (string_address)word,
                          which_at == settings.menu_at,
                          string_equals((string_address)word, (string_address)settings.value[settings.menu]));
        }
        // More choices than show: a thin mark at the right edge, where the view is among them.
        if (settings.menu_count > shown)
        {
                int track = height - 2 * pad, bar = track * shown / settings.menu_count;

                bar = bar < settings_px(10) ? settings_px(10) : bar;
                ui_round(ui, x + width - settings_px(5), y + pad + (track - bar) * settings.menu_top / (settings.menu_count - shown),
                         settings_px(2), bar, settings_px(1), ui_tone(ui->ink, 110));
        }
}

static fn settings_draw(unsigned int address_to pixels)
{
        struct ui address_to ui = &settings.ui;
        int left = settings_px(UI_PAD);
        int list_top = settings_list_top(), list_bottom = settings_list_bottom();

        ui->pixels = pixels;
        ui->pitch = settings.own.table->stride;
        ui->width = settings.width;
        ui->height = settings.height;
        ui->top = 0;
        ui->bottom = settings.height;
        ui_clear(ui);
        ui_title(ui, left + settings_px(6), settings_px(8), list_top - settings_px(8),
                 settings.view ? (string_address)settings.card_title : (string_address) "Settings");

        // The filter, at the right of the title and under where the window is taken hold of.
        if (!settings.view)
        {
                int width = settings_px(SETTINGS_FILTER_W), x = settings.width - left - width, y = settings_px(24);

                ui_field(ui, x, y, width, settings_px(28), (string_address)settings.filter, settings.editing < 0 && settings.filter_length);
                if (!settings.filter_length)
                        ui_run(ui, x + settings_px(8), ui_baseline(ui, y, settings_px(28), UI_TEXT), (string_address) "Type to filter",
                               UI_TEXT, false, ui->quiet, 150, x + settings_px(8), x + width - settings_px(8));
        }

        if (settings.view)
                settings_draw_card(ui);
        else
        {
                settings_draw_side(ui);
                settings_draw_list(ui);
        }

        settings_draw_foot(ui, list_bottom);
        if (settings.menu >= 0)
                settings_draw_menu(ui);
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
        //      The engine and its atlas are one mapping, made here: a static atlas is 256 KiB of the image's
        //      address space in every program that links this, a shell under a 12 MiB cap among them.
        positive size = 0, bytes = regular ? compositor_map_file(regular, &size) : 0;
        positive atlas_at = (sizeof(font_engine) + 4095) & ~(positive)4095;
        long engine = compositor_call(syscall(mmap), 0, atlas_at + 512 * 512, 3, 0x22, (positive)-1, 0);

        if (!bytes || (engine < 0 && engine > -4096) || font_load(&settings.face[0], (const p8 address_to)bytes, size, 0) != 0)
                return false;
        settings.ui.engine = (font_engine address_to)engine;
        font_start(settings.ui.engine, (p8 address_to)(engine + atlas_at), 512, 512);
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
        settings_layout();
        settings_scroll_to(settings.view ? settings.card_scroll : settings.scroll);
        settings_side_reveal();
        if (settings.view)
                settings_wrap();
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
        settings.editing = -1;
        settings.shown = -1;
        settings.output = -1;
        settings.pane = 1; // the keys start in the rows, on the first of them
        settings.menu = -1;

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

        if (!settings_map())
        {
                compositor_kmsg((string_address) "settings: no memory, pid ",
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
        settings_refilter();
        {
                int width = settings_px(SETTINGS_WIDTH), height = settings_px(SETTINGS_HEIGHT);
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
        settings.help = 1;
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

                // A verb that reads or sets a row is given a time (a feed's reading a short one, a join a long one); a command
                // of All commands runs until it ends or is stopped.
                if (settings.child > 0 && !(settings.job_sets && settings.job >= SETTINGS_ANY))
                {
                        positive now = clock_monotonic_nanoseconds(), left;
                        positive most = settings.job == SETTINGS_JOB_FEED  ? (positive)SETTINGS_FEED_WAIT_NS
                                        : settings.job == SETTINGS_JOB_ACT ? (positive)SETTINGS_ACT_WAIT_NS
                                                                           : (positive)SETTINGS_WAIT_NS;

                        if (now - settings.began >= most)
                        {
                                settings_finished(true);
                                continue;
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
                                continue;
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
