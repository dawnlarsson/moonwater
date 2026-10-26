/*
        The settings, in one table.

        Every setting a person can change by hand is a row here: its place
        in the settings window, the words of the moonwater command that read
        and set it, the line the help page and the README say of it, the
        file in /root that keeps it, and who applies it. The command
        (src/sh/host.c), the window (src/canvas/ui/settings.c) and the
        compositor (src/canvas/compositor.c) are all in one program, so each
        reads this table and none keeps a list of its own: one row added
        here is a verb that reads and sets it, a line on the help page, a
        row in the window and a file wipe leaves alone, and the lane
        that holds the README to the pages asks for its line there too.

        A plain row is read and set by the one generic path of the command
        (a bare verb says it, a word sets it, default or the row's reset word
        puts it back, a word that is none is refused with exit 1, a word
        too many is a usage page, a change needs root) and is kept by the
        word it was given, normalised, in the file of its path. A special
        row has a verb of its own in host.c, which the window and the pages
        still list from here: a layout the kernel is told of at once, a zone
        with its own tools, a switch that is a radio. An alias is a second
        row of the window for the verb of the plain or special row above it.

        The window reads a row as a verb's words after moonwater (read), the
        word after key in what the verb says, and sets it with the words of
        set and the value. A plain row's are made from its verb, and its
        key is its last word and a blank.

        Dawn Larsson - Apache-2.0 license
        github.com/dawnlarsson/moonwater
*/
#ifndef MOONWATER_SETTINGS_REGISTRY
#define MOONWATER_SETTINGS_REGISTRY

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
#define SETTINGS_BATTERIES 16u // a choice of the batteries the machine has: auto, each name from the kernel, and none

// The words of a verb, at most: canvas cursor shake delay.
#define SETTING_WORDS 4

// How the command meets a row.
#define SETTING_SPECIAL 0u // its verb is written by hand in host.c
#define SETTING_PLAIN 1u   // read and set by the generic path
#define SETTING_ALIAS 2u   // another row of the window for the verb of the row above it

// What a plain row's value is.
#define SETTING_WORD 0u   // a switch (on off) or one of its choices
#define SETTING_NUMBER 1u // a whole number from low to high, and the words of choices (off) besides
#define SETTING_COLOUR 2u // rrggbb, kept as #rrggbb

// Who takes a changed value at once.
#define SETTING_LIVE_NONE 0u
#define SETTING_LIVE_COMPOSITOR 1u // the compositor watches /root and reads the file again

// What the files of the plain rows are called, in /root.
#define SETTING_BACKGROUND_PATH "/root/canvas.background"
#define SETTING_CURSOR_SIZE_PATH "/root/canvas.cursor.size"
#define SETTING_CURSOR_LOOK_PATH "/root/canvas.cursor.look"
#define SETTING_SHAKE_PATH "/root/canvas.cursor.shake"
#define SETTING_SHAKE_DELAY_PATH "/root/canvas.cursor.shake.delay"
#define SETTING_REPEAT_DELAY_PATH "/root/keyboard.repeat.delay"
#define SETTING_REPEAT_RATE_PATH "/root/keyboard.repeat.rate"
#define SETTING_SENSITIVITY_PATH "/root/mouse.sensitivity"
#define SETTING_ACCELERATION_PATH "/root/mouse.acceleration"
#define SETTING_TASKBAR_POSITION_PATH "/root/taskbar.position"
#define SETTING_TASKBAR_DISPLAY_PATH "/root/taskbar.display"

struct settings_row
{
        // The window.
        const char address_to section; // the header this row starts a card under, or none
        const char address_to label;
        unsigned char kind;
        unsigned char lacks;                    // a machine may have none: no key said is no value, not a failure
        unsigned char flags;                    // SETTINGS_LINE, SETTINGS_SPLIT
        const char address_to read[SETTING_WORDS]; // what is asked, after moonwater
        const char address_to key;              // the words the value follows ("" is the whole first line)
        const char address_to also;             // and where it is when that is said with nothing after it
        const char address_to set[SETTING_WORDS];  // what changes it; the value goes after
        const char address_to choices;          // a choice's words, or a step's two, a space between
        char stop;                              // a value ends here as well as at a blank, a comma, a semicolon or a bracket
        const char address_to marker;           // a line that has this says the value is auto, whatever the word after the key
        // The command and its pages.
        const char address_to usage; // the row of the pages: the words after moonwater, as typed; none when another row's says it
        const char address_to help;  // and what it says of it
        unsigned char how;           // SETTING_SPECIAL, SETTING_PLAIN, SETTING_ALIAS
        unsigned char type;          // SETTING_WORD, SETTING_NUMBER, SETTING_COLOUR
        unsigned char live;          // SETTING_LIVE_*
        // A plain row's.
        const char address_to path;     // the file in /root that keeps it
        const char address_to standard; // what it is when nothing is kept, as it is said
        const char address_to reset;    // the word kept when it is put back (default when none)
        const char address_to say;      // what its line starts with (its verb without the first word when none)
        const char address_to need;     // what a change needs root for, when the page must say more than moonwater VERB needs root
        const char address_to refusal;  // what a word that is none is told: %s is the word (generated when none)
        const char address_to unit;     // a number's unit, said after it
        long low, high;                 // a number's least and greatest
};

/*
        The rows. A group (section) starts at the row that names it and holds
        the rows after it; the order is the window's, and the main page of the
        command shows the rows of a verb in this order too. The labels are
        what the window and its lane find a row by.
*/
#define SETTING_UNWRAP(...) __VA_ARGS__
// A plain row: group, label, window kind and flags, verb words in brackets, key, choices, then the rest by name.
#define SETTING_PLAIN_ROW(section_, label_, kind_, flags_, verb_, key_, choices_, ...)                                \
        {section_, label_, kind_, 0, flags_, {SETTING_UNWRAP verb_}, key_, null, {SETTING_UNWRAP verb_}, choices_, 0, null, \
         .how = SETTING_PLAIN, __VA_ARGS__}

static const struct settings_row settings_rows[] = {
    {"Machine", "Name", SETTINGS_FIELD, 0, SETTINGS_LINE, {"name"}, "", null, {"name"}, null, 0, null,
     .usage = "name [NEW|random]", .help = "what this machine is called, like space-wizard"},
    //  The menu names each layout once; gb, sv and nb, which the help names as well, are the same layouts as uk, se
    //  and no (locale_keyboards in host.c, the compositor's table), kept as typed, so the menu leaves them out.
    {null, "Keyboard", SETTINGS_CHOICE, 0, 0, {"keyboard"}, "keyboard ", null, {"keyboard"}, "us uk de se no dk fi fr es it", 0, null,
     .usage = "keyboard [LAYOUT|list|xkb]", .help = "us uk gb de se sv no nb dk fi fr es it [us]"},
    SETTING_PLAIN_ROW(null, "Key repeat delay (ms, or default)", SETTINGS_FIELD, 0, ("keyboard", "repeat", "delay"), "delay ", null,
                      .type = SETTING_NUMBER, .live = SETTING_LIVE_COMPOSITOR, .path = SETTING_REPEAT_DELAY_PATH,
                      .standard = "250", .unit = "ms", .low = 100, .high = 2000,
                      .usage = "keyboard repeat delay [MS|default]",
                      .help = "how long a key is held before it repeats, 100 to 2000 ms [250]"),
    SETTING_PLAIN_ROW(null, "Key repeat rate (per second, off or default)", SETTINGS_FIELD, 0, ("keyboard", "repeat", "rate"), "rate ", "off",
                      .type = SETTING_NUMBER, .live = SETTING_LIVE_COMPOSITOR, .path = SETTING_REPEAT_RATE_PATH,
                      .standard = "30", .unit = "per second", .low = 1, .high = 100,
                      .usage = "keyboard repeat rate [PER-SECOND|off|default]",
                      .help = "repeats a second of a held key, 1 to 100, or none [30]"),
    {null, "Time zone", SETTINGS_FIELD, 0, 0, {"timezone"}, "timezone ", null, {"timezone"}, null, 0, null,
     .usage = "timezone [ZONE|list]", .help = "IANA name, country, +1 or POSIX; manual"},
    {"Canvas", "Scale", SETTINGS_CHOICE, 0, 0, {"canvas", "scale"}, "scale ", null, {"canvas", "scale"}, "auto 1 2 3 4", 0, "auto:",
     .usage = "canvas scale [N|auto]", .help = "device pixels to a drawn pixel, 1 to 4, read as the desktop starts [auto]"},
    {null, "Screen modes", SETTINGS_CHOICE, 0, 0, {"canvas", "modes"}, "modes ", null, {"canvas", "modes"}, "largest preferred", 0, null,
     .usage = "canvas modes [largest|preferred]", .help = "which mode a screen is driven at [largest]"},
    SETTING_PLAIN_ROW(null, "Background", SETTINGS_CHOICE, SETTINGS_INK, ("canvas", "background"), "background ",
                      "default black dark-grey slate midnight-blue forest plum light-grey white",
                      .type = SETTING_COLOUR, .live = SETTING_LIVE_COMPOSITOR, .path = SETTING_BACKGROUND_PATH, .standard = "#1e1e1e", .reset = "default",
                      .need = "how Canvas draws", .refusal = "background is a colour like 1e1e1e, or default, not %s\n",
                      .usage = "canvas background [COLOUR|default]", .help = "the desktop colour, rrggbb [1e1e1e]"),
    {null, "Background colour (rrggbb or default)", SETTINGS_FIELD, 0, 0, {"canvas", "background"}, "background ", null,
     {"canvas", "background"}, null, 0, null, .how = SETTING_ALIAS},
    SETTING_PLAIN_ROW(null, "Cursor size", SETTINGS_CHOICE, 0, ("canvas", "cursor", "size"), "size ", "auto 1 2 3 4",
                      .live = SETTING_LIVE_COMPOSITOR, .path = SETTING_CURSOR_SIZE_PATH, .standard = "auto", .reset = "auto", .need = "how Canvas draws",
                      .refusal = "cursor size is auto or a number from 1 to 4, not %s\n",
                      .usage = "canvas cursor [size [N|auto]|look [light|dark|auto]]", .help = "the pointer: size 1 to 4, colours [auto]"),
    SETTING_PLAIN_ROW(null, "Cursor look", SETTINGS_CHOICE, 0, ("canvas", "cursor", "look"), "look ", "auto light dark",
                      .live = SETTING_LIVE_COMPOSITOR, .path = SETTING_CURSOR_LOOK_PATH, .standard = "auto", .reset = "auto", .need = "how Canvas draws",
                      .refusal = "cursor look is light, dark or auto, not %s\n"),
    SETTING_PLAIN_ROW(null, "Shake the pointer to find it", SETTINGS_SWITCH, 0, ("canvas", "cursor", "shake"), "shake ", null,
                      .live = SETTING_LIVE_COMPOSITOR, .path = SETTING_SHAKE_PATH, .standard = "on", .need = "how Canvas draws",
                      .usage = "canvas cursor shake [on|off]", .help = "a shake of the hand makes the pointer large for a moment [on]"),
    SETTING_PLAIN_ROW(null, "Large pointer stays (ms, or default)", SETTINGS_FIELD, 0, ("canvas", "cursor", "shake", "delay"), "delay ", null,
                      .type = SETTING_NUMBER, .live = SETTING_LIVE_COMPOSITOR, .path = SETTING_SHAKE_DELAY_PATH,
                      .standard = "1200", .unit = "ms", .low = 100, .high = 10000, .need = "how Canvas draws",
                      .usage = "canvas cursor shake delay [MS|default]",
                      .help = "how long it stays large after the last shake, 100 to 10000 ms [1200]"),
    SETTING_PLAIN_ROW("Mouse", "Mouse sensitivity (percent, or default)", SETTINGS_FIELD, 0, ("mouse", "sensitivity"), "sensitivity ", null,
                      .type = SETTING_NUMBER, .live = SETTING_LIVE_COMPOSITOR, .path = SETTING_SENSITIVITY_PATH,
                      .standard = "100", .unit = "percent", .low = 10, .high = 500,
                      .usage = "mouse sensitivity [PERCENT|default]", .help = "how far the pointer goes for the hand, 10 to 500 percent [100]"),
    SETTING_PLAIN_ROW(null, "Mouse acceleration", SETTINGS_SWITCH, 0, ("mouse", "acceleration"), "acceleration ", null,
                      .live = SETTING_LIVE_COMPOSITOR, .path = SETTING_ACCELERATION_PATH, .standard = "on",
                      .usage = "mouse acceleration [on|off]", .help = "the pointer outruns a fast hand; off is one to one [on]"),
    SETTING_PLAIN_ROW("Taskbar", "Taskbar position", SETTINGS_CHOICE, 0, ("taskbar", "position"), "position ", "bottom top left right",
                      .live = SETTING_LIVE_COMPOSITOR, .path = SETTING_TASKBAR_POSITION_PATH, .standard = "bottom", .reset = "bottom",
                      .need = "how Canvas draws", .refusal = "taskbar position is bottom, top, left or right, not %s\n",
                      .usage = "taskbar position [bottom|top|left|right]", .help = "the edge of the screen the taskbar sits on [bottom]"),
    SETTING_PLAIN_ROW(null, "Taskbar display", SETTINGS_CHOICE, 0, ("taskbar", "display"), "display ", "primary all",
                      .live = SETTING_LIVE_COMPOSITOR, .path = SETTING_TASKBAR_DISPLAY_PATH, .standard = "primary", .reset = "primary",
                      .need = "how Canvas draws", .refusal = "taskbar display is primary or all, not %s\n",
                      .usage = "taskbar display [primary|all]", .help = "the display the taskbar is drawn on: the one Canvas draws on [primary]"),
    {"Time", "Set the clock from the network", SETTINGS_SWITCH, 0, 0, {"ntp"}, "ntp ", null, {"ntp"}, null, 0, null,
     .usage = "ntp [on|off]", .help = "set the clock from the network [on]"},
    {null, "Clock (Enter sets it now)", SETTINGS_ACTION, 0, SETTINGS_LINE | SETTINGS_TICK, {"time"}, "local ", null, {"time", "sync"}, null, 0,
     null, .usage = "time [sync]", .help = "the clock; sync sets it and the zone now"},
    {null, "Time server (auto for the pool)", SETTINGS_FIELD, 0, 0, {"ntp", "server"}, "ntp server ", null, {"ntp", "server"}, null, 0, null,
     .usage = "ntp server [NAME|auto]", .help = "who is asked first: a name or address [auto]"},
    {null, "Keep the lowest-delay sample", SETTINGS_SWITCH, 0, 0, {"ntp", "sampling"}, "ntp sampling ", null, {"ntp", "sampling"}, null, 0, null,
     .usage = "ntp sampling [on|off]", .help = "keep the lowest-delay sample of five [on]"},
    {null, "Authenticated time (NTS)", SETTINGS_CHOICE, 0, 0, {"ntp", "nts"}, "ntp nts ", null, {"ntp", "nts"}, "on off only", 0, null,
     .usage = "ntp nts [on|off|only]", .help = "authenticated time (RFC 8915) first; only refuses plain [on]"},
    {null, "NTS servers (or default)", SETTINGS_FIELD, 0, SETTINGS_LINE | SETTINGS_SPLIT, {"ntp", "nts"}, "servers ", null,
     {"ntp", "nts", "servers"}, null, ';', null,
     .usage = "ntp nts servers [NAME[:PORT]...|default]", .help = "who is asked for it, up to four"},
    {"Network", "Wired", SETTINGS_SWITCH, 0, 0, {"wired"}, "wired ", null, {"wired"}, null, 0, null,
     .usage = "wired [on|off]", .help = "the wired links; off keeps them down"},
    {null, "Airplane mode", SETTINGS_SWITCH, 0, 0, {"airplane"}, "airplane ", null, {"airplane"}, null, 0, null,
     .usage = "airplane [on|off]", .help = "every radio at once"},
    {null, "The internet prefers", SETTINGS_CHOICE, 0, 0, {"priority"}, "internet prefers ", null,
     {"priority", "internet"}, "wired wifi", 0, null,
     .usage = "priority [internet [wired|wifi]]", .help = "which link wins when both are up [wired]"},
    {null, "Names over TLS", SETTINGS_CHOICE, 0, 0, {"dns", "tls"}, "dns tls ", null, {"dns", "tls"}, "off on only", 0, null,
     .usage = "dns tls [on|off|only]", .help = "names over TLS to 1.1.1.1 and 9.9.9.9 first; only sends none in the clear [off]"},
    {null, "Name servers (auto for the network)", SETTINGS_FIELD, 0, SETTINGS_LINE | SETTINGS_SPLIT, {"dns"}, "dns ", null, {"dns"}, null, '(', null,
     .usage = "dns [ADDRESS...|auto]", .help = "up to three servers names are asked of, or the network gives them [auto]"},
    {"Wi-Fi", "Wi-Fi", SETTINGS_SWITCH, 0, 0, {"wifi"}, "wifi ", null, {"wifi"}, null, 0, null,
     .usage = "wifi [on|off]", .help = "the radio, saved networks and those in range [off]"},
    {"Bluetooth", "Bluetooth", SETTINGS_SWITCH, 0, 0, {"bluetooth"}, "bluetooth ", null, {"bluetooth"}, null, 0, null,
     .usage = "bluetooth [on|off]", .help = "the radio and the remembered devices [off]"},
    // A class row reads its word off the classes line usb says before any device, so what a device calls itself is never read.
    {"USB", "New USB devices", SETTINGS_CHOICE, 0, 0, {"usb"}, "usb ", null, {"usb"}, "unlock lock", 0, null,
     .usage = "usb [lock|unlock]", .help = "lock keeps what is plugged in now, and holds a new device until it is allowed [unlock]"},
    {null, "Storage", SETTINGS_CHOICE, 0, 0, {"usb"}, "storage ", null, {"usb", "class", "storage"}, "deny allow", 0, null,
     .usage = "usb class NAME [allow|deny]",
     .help = "what a new device no rule names may bring up: storage hid audio video net bluetooth hub other [deny]"},
    {null, "Keyboards and mice", SETTINGS_CHOICE, 0, 0, {"usb"}, "hid ", null, {"usb", "class", "hid"}, "deny allow", 0, null, .how = SETTING_SPECIAL},
    {null, "Audio", SETTINGS_CHOICE, 0, 0, {"usb"}, "audio ", null, {"usb", "class", "audio"}, "deny allow", 0, null, .how = SETTING_SPECIAL},
    {null, "Cameras", SETTINGS_CHOICE, 0, 0, {"usb"}, "video ", null, {"usb", "class", "video"}, "deny allow", 0, null, .how = SETTING_SPECIAL},
    {null, "Network adapters", SETTINGS_CHOICE, 0, 0, {"usb"}, "net ", null, {"usb", "class", "net"}, "deny allow", 0, null, .how = SETTING_SPECIAL},
    {null, "Bluetooth adapters", SETTINGS_CHOICE, 0, 0, {"usb"}, "bluetooth ", null, {"usb", "class", "bluetooth"}, "deny allow", 0, null, .how = SETTING_SPECIAL},
    {null, "Hubs", SETTINGS_CHOICE, 0, 0, {"usb"}, "hub ", null, {"usb", "class", "hub"}, "deny allow", 0, null, .how = SETTING_SPECIAL},
    {null, "Other devices", SETTINGS_CHOICE, 0, 0, {"usb"}, "other ", null, {"usb", "class", "other"}, "deny allow", 0, null, .how = SETTING_SPECIAL},
    {"Power", "Battery", SETTINGS_INFO, 1, SETTINGS_LINE | SETTINGS_TICK, {"charge"}, "battery ", null, {null}, null, ';', null},
    {null, "Battery shown", SETTINGS_CHOICE, 1, SETTINGS_BATTERIES, {"battery"}, "battery ", null, {"battery"}, null, 0, null,
     .usage = "battery [NAME|auto|none]", .help = "which battery the Battery row reads: auto is the first present one, none is no battery [auto]"},
    {null, "Power profile", SETTINGS_CHOICE, 1, 0, {"power"}, "power ", "profile ", {"power"},
     "balanced performance powersave", 0, null,
     .usage = "power [performance|balanced|powersave]", .help = "profile and CPU governor, kept across boots"},
    {null, "Brightness", SETTINGS_STEP, 1, 0, {"brightness"}, "brightness ", null, {"brightness"}, "-10 +10", 0, null,
     .usage = "brightness [N%|+N|-N]", .help = "the screen backlight"},
    {null, "Boost", SETTINGS_SWITCH, 1, 0, {"cpu"}, "boost ", null, {"cpu", "boost"}, null, 0, null,
     .usage = "cpu [boost|smt on|off]", .help = "turbo and SMT, kept across boots"},
    {null, "Simultaneous threads (SMT)", SETTINGS_SWITCH, 1, 0, {"cpu"}, "smt ", null, {"cpu", "smt"}, null, 0, null},
    {null, "Charge limit (20 to 100, or off)", SETTINGS_FIELD, 1, 0, {"charge"}, "charging stops at ", null,
     {"charge", "limit"}, null, '%', null,
     .usage = "charge [limit N|limit off]", .help = "where the battery stops charging, 20 to 100"},
    {"Boot", "Start the desktop", SETTINGS_SWITCH, 1, 0, {"bind", "init", "canvas"}, "init canvas ", null, {"bind", "init", "canvas"},
     null, 0, null, .usage = "bind init canvas [on|off]", .help = "start Canvas at boot [on]"},
    {null, "Mount kept disks", SETTINGS_SWITCH, 1, 0, {"bind", "init", "mount"}, "init mount ", null, {"bind", "init", "mount"}, null,
     0, null, .usage = "bind init mount [on|off]", .help = "mount kept disks at boot [on]"},
};

#define SETTINGS_ROWS (sizeof(settings_rows) / sizeof(settings_rows[0]))

/* ---- what the rows mean, for whoever asks ---- */

// How many words a row's verb has.
static positive setting_words(const struct settings_row address_to row)
{
        positive count = 0;

        while (count < SETTING_WORDS && row->read[count])
                count++;
        return count;
}

// Where word is among the words of list, a blank apart; -1 when it is none of them.
static long setting_place(const char address_to list, string_address word)
{
        positive length = string_length(word), place = 0;

        while (list && *list)
        {
                positive size = (positive)(string_first_of_or_end((string_address)list, ' ') - (string_address)list);

                if (size == length && !string_compare_max((string_address)list, word, length))
                        return (long)place;
                list += size + (list[size] == ' ');
                place++;
        }
        return -1;
}

/*
        The plain row that words (the words after moonwater, count of them)
        start with: the one with the most words of its own, so that
        canvas cursor shake delay is not canvas cursor shake with a word.
        Answers its index, or -1 when none is, and in used how many words
        it takes.
*/
static int setting_find(string_address address_to words, positive count, positive address_to used)
{
        int found = -1;
        positive most = 0;

        for (positive at = 0; at < SETTINGS_ROWS; at++)
        {
                const struct settings_row address_to row = &settings_rows[at];
                positive size = setting_words(row), word = 0;

                if (row->how != SETTING_PLAIN || size > count || size <= most)
                        continue;
                while (word < size && string_equals(words[word], (string_address)row->read[word]))
                        word++;
                if (word == size)
                {
                        found = (int)at;
                        most = size;
                }
        }
        address_to used = most;
        return found;
}

// The row whose file is path (one of the SETTING_ paths), which every row that is kept has.
static const struct settings_row address_to setting_by_path(string_address path)
{
        for (positive at = 0; at < SETTINGS_ROWS; at++)
                if (settings_rows[at].path && string_equals(path, (string_address)settings_rows[at].path))
                        return &settings_rows[at];
        return null;
}

/*
        A colour as #rrggbb in lower case, from rrggbb or the three digits of either that stand for
        each one twice: false for anything else.
*/
static bool setting_colour_normal(string_address word, p8 address_to into)
{
        long colour = window_colour_parse(word);
        p8 bytes[3] = {(p8)(colour >> 16), (p8)(colour >> 8), (p8)colour};

        if (colour < 0)
                return false;
        into[0] = '#';
        memory_into_hex(into + 1, bytes, 3);
        into[7] = end;
        return true;
}

// A whole number as it is kept: digits only, no more than nine, none leading but a lone zero.
static bool setting_whole(string_address word, long address_to into)
{
        string_address stop = word;
        b32 range = 0;
        positive length = string_length(word);

        if (!length || length > 9 || !byte_is_digit(word[0]) || (word[0] == '0' && length > 1))
                return false;
        *into = (long)string_to_number_unsigned_checked(word, address_of stop, 10, address_of range);
        return !*stop && !range;
}

/*
        What a word is to a plain row. False when it is not one of its
        words; else it is kept as it is wanted in the file, normalised (a
        colour as #rrggbb), and what it stands for is value: a switch's 1
        or 0, a choice's place in its words, a number, a colour's rgb, and
        for the standard (default, or the reset word) the standard's.
*/
static long setting_standard(const struct settings_row address_to row);

// The words a value is one of: a switch's on and off, else the row's choices.
static const char address_to setting_words_of(const struct settings_row address_to row)
{
        return row->kind == SETTINGS_SWITCH ? "on off" : row->choices;
}

// The word kept when a row is put back: its reset word, else default.
static const char address_to setting_reset_of(const struct settings_row address_to row)
{
        return row->reset ? row->reset : "default";
}

static bool setting_parse(const struct settings_row address_to row, string_address word, p8 address_to kept, positive room,
                          long address_to value)
{
        const char address_to words = setting_words_of(row);
        const char address_to reset = setting_reset_of(row);
        p8 colour[8];
        long place;

        kept[0] = end;
        if (string_equals(word, (string_address) "default") || string_equals(word, (string_address)reset))
        {
                string_copy_bounded(kept, (string_address)reset, room);
                address_to value = setting_standard(row);
                return true;
        }
        if (row->type == SETTING_COLOUR)
        {
                if (!setting_colour_normal(word, colour))
                        return false;
                string_copy_bounded(kept, (string_address)colour, room);
                address_to value = window_colour_parse((string_address)colour);
                return true;
        }
        place = setting_place(words, word);
        if (row->type == SETTING_NUMBER && place < 0)
        {
                long number;

                if (!setting_whole(word, &number) || number < row->low || number > row->high)
                        return false;
                string_copy_bounded(kept, word, room);
                address_to value = number;
                return true;
        }
        if (place < 0)
                return false;
        string_copy_bounded(kept, word, room);
        address_to value = row->type == SETTING_NUMBER ? 0 : place;
        return true;
}

// What the row is when nothing is kept: its standard as a value.
static long setting_standard(const struct settings_row address_to row)
{
        long value = 0;

        if (row->type == SETTING_NUMBER)
                return setting_whole((string_address)row->standard, &value) ? value : 0;
        if (row->type == SETTING_COLOUR)
                return window_colour_parse((string_address)row->standard);
        // A word's own place among its words.
        value = setting_place(setting_words_of(row), (string_address)row->standard);
        return value < 0 ? 0 : value;
}

/*
        What a file of a plain row holds, as a value, in value: the standard when it holds nothing,
        the word default or a word that is none of the row's. True when something is kept.
*/
static bool setting_kept(const struct settings_row address_to row, string_address word, long address_to value)
{
        p8 kept[16];
        const char address_to reset = setting_reset_of(row);

        long found;

        address_to value = setting_standard(row);
        if (!word[0] || string_equals(word, (string_address) "default") || string_equals(word, (string_address)reset) ||
            !setting_parse(row, word, kept, sizeof(kept), &found))
                return false;
        address_to value = found;
        return true;
}

/*
        What a plain row says of what its file holds: the word, a colour
        normalised, a number and its unit; the standard when nothing is kept
        or what is kept is none of its words.
*/
static string_address setting_shown(const struct settings_row address_to row, string_address word, p8 address_to into, positive room)
{
        p8 kept[16];
        long value = 0;
        positive length;

        if (!word[0] || string_equals(word, (string_address) "default") || !setting_parse(row, word, kept, sizeof(kept), &value))
                string_copy_bounded(into, (string_address)row->standard, room);
        else
                string_copy_bounded(into, kept, room);
        if (row->type == SETTING_NUMBER && byte_is_digit(into[0]) && row->unit)
        {
                length = string_length(into);
                string_copy_bounded(into + length, (string_address) " ", room - length);
                string_copy_bounded(into + length + 1, (string_address)row->unit, room - length - 1);
        }
        return into;
}

#endif // MOONWATER_SETTINGS_REGISTRY
