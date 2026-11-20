/*
        The settings window on the compositor (a ui.c page front-end to /moonwater).
        Rows come from src/sh/registry.c; All commands is parsed from `moonwater help`;
        live feeds poll Wi-Fi, Bluetooth, Devices, Waterlink, Power, Time and Network.

            settings [-f FONT.ttf] [-b BOLD.ttf] [-m MOONWATER]

        Dawn Larsson - Apache-2.0 license
        github.com/dawnlarsson/moonwater
*/
#ifndef MOONWATER_CANVAS_SETTINGS
#define MOONWATER_CANVAS_SETTINGS

#include "../../sh/splash_mark.c"

// The battery reader (ui/ui.c) is read before this file: the Battery row's value is one of its batteries.

// What a row's value is: not asked yet, said, not on this machine, asked and not found in what was said.
#define SETTINGS_UNREAD 0u
#define SETTINGS_READ 1u
#define SETTINGS_ABSENT 2u
#define SETTINGS_FAILED 3u

#define SETTINGS_VALUE 128   // a value read
#define SETTINGS_TYPED 256   // what is typed in a field: a value, or the words of a command
#define SETTINGS_SAID 24576  // what a verb says: the whole of moonwater help is about fifteen thousand
#define SETTINGS_STATUS 160
#define SETTINGS_WORDS 40   // the words of a command run: the desk of the screens map is four a screen (NAME place X Y)
#define SETTINGS_WAIT_NS 20000000000ll
#define SETTINGS_FILTER 64   // what the filter holds, in bytes: its field holds no more
#define SETTINGS_GENERATED 128 // rows of All commands, at most: help's main page is 123, and the kit's list holds 320 in all (ui.c)
#define SETTINGS_POOL 16384    // the bytes of their words
#define SETTINGS_CARD 12288   // the bytes of what a command said, shown as a card
#define SETTINGS_CARD_LINES 256
#define SETTINGS_PROBE 512    // one line, cut
#define SETTINGS_LABEL_CUT 128 // a label up to its bracket, in bytes (settings_label_window)
#define SETTINGS_TRY_SPAN 192  // the largest picture of a pointer its row draws, device pixels (settings_try_pointer_one)
// The built-in Moonwater wordmark from splash_mark.c: its non-zero bounding box, logical size in the About row, and max device cover buffer.
#define SETTINGS_LOGO_X0 14
#define SETTINGS_LOGO_Y0 74
#define SETTINGS_LOGO_SRC_W 292
#define SETTINGS_LOGO_SRC_H 44
#define SETTINGS_LOGO_W 172
#define SETTINGS_LOGO_ROW_H 44
#define SETTINGS_LOGO_MAX_W 576
#define SETTINGS_LOGO_MAX_H 96
#define SETTINGS_NONE 0xffffu

// The size the window opens at, logical pixels; every other size in it is the kit's (ui.c, the page).
#define SETTINGS_WIDTH 700
#define SETTINGS_HEIGHT 520
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

// Live feeds polled while their group is shown (list feeds first, then row feeds).
#define SETTINGS_FEED_ROWS 1u
#define SETTINGS_FEED_WIFI 2u
#define SETTINGS_FEED_DEVICES 3u
#define SETTINGS_FEED_HARDWARE 4u
#define SETTINGS_FEED_LINKS 5u
#define SETTINGS_LISTS 4
#define SETTINGS_LIST_ROWS 24
#define SETTINGS_ITEM_LABEL 96
#define SETTINGS_FEED_WAIT_NS 8000000000ll
#define SETTINGS_ACT_WAIT_NS 60000000000ll
#define SETTINGS_WAIT_PAIR_NS 330000000000ll

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
    {"Devices", null, {"device", "list"}, "Internal storage", 3000, SETTINGS_FEED_HARDWARE},
    {"Waterlink", "Waterlink", {"link"}, "Linked machines", 2000, SETTINGS_FEED_LINKS},
    {"Power", null, {null}, null, 2000, SETTINGS_FEED_ROWS},
    {"Time", null, {null}, null, 1000, SETTINGS_FEED_ROWS},
    {"Network", null, {null}, null, 2000, SETTINGS_FEED_ROWS},
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
#define SETTINGS_ITEM_DRIVE 32u  // a drive the device list shows: its strip, and no verb holds it
#define SETTINGS_ITEM_USB 64u    // a port the device list says is a USB device's
#define SETTINGS_ITEM_DISK 128u  // a USB device that holds a drive (its "drive" line): its row is the drive's too, its strip under its tabs

// Where a device of the device list is, and so the section of the Devices group it is in.
#define SETTINGS_WHERE_INTERNAL 0 // a drive in the machine: Internal storage (a drive a host from before "where" lines says nothing of too)
#define SETTINGS_WHERE_EXTERNAL 1 // plugged in, External devices: a USB device, or a drive the host says is external
#define SETTINGS_WHERE_SD 2       // an SD card, which is external
#define SETTINGS_WHERE_NONE 3     // the note that no device was found: under no header
#define SETTINGS_EXTERNAL_HEADER "External devices"

#define SETTINGS_SEGMENTS 33 // a drive's segments: the host's 16 partitions, and the free room before, between and after them
#define SETTINGS_DRIVES 8        // drives whose partition strips are kept in settings_large->drive
#define SETTINGS_STATE 24        // a device's state as the host says it: allowed, blocked or waiting for a port, view only for a drive
#define SETTINGS_PORT_ROOM 24    // a port's name, which device allow and device deny take (1-1, 3-2.4)
#define SETTINGS_SIZE_TEXT 16    // a size as the strip says it: 512 MB, 500 GB
#define SETTINGS_SEGMENT_NAME 40 // a partition's name as its row says it, cut where a character ends
#define SETTINGS_ALIGNMENT 1048576ull // free room this small is the room a partition table and the partitions' alignment keep, and is not shown

struct settings_drive
{
        unsigned char role[SETTINGS_SEGMENTS];
        unsigned long long bytes[SETTINGS_SEGMENTS];
        char name[SETTINGS_SEGMENTS][SETTINGS_SEGMENT_NAME];
};

struct settings_item
{
        char label[SETTINGS_ITEM_LABEL];
        char security[12];
        signed short dbm;
        unsigned char bars, flags, kind;
        unsigned char where;                            // a device of the device list: SETTINGS_WHERE_*
        unsigned char segment_count;                    // a drive's partitions and free room, in order (SETTINGS_SEGMENTS most)
        unsigned short segment_more;                    // a drive's partitions the host did not show (its "more" line)
        unsigned char address_to segment_role;          // each one's UI_STRIP_* role (into settings_large->drive, null for non-drives)
        unsigned long long address_to segment_bytes;
        unsigned long long total;                       // a drive's bytes: its line's size, or what its part lines add up to where that is more
        char state[SETTINGS_STATE];                     // a device's state, as the host says it
        char port[SETTINGS_PORT_ROOM];                       // a port's name; none for a drive
        char size[SETTINGS_SIZE_TEXT];                  // a drive's size
        char access[48];                                // Waterlink: grants after "may "
        char detail[SETTINGS_ITEM_LABEL];               // Waterlink: last-heard / session status
        char (address_to segment_name)[SETTINGS_SEGMENT_NAME]; // each segment's name, the partition's as the host says it (none: empty)
};

// Named colours for ink rows (background and accent).
struct settings_ink
{
        const char address_to name;
        const char address_to colour;
};

static const struct settings_ink settings_inks[] = {
    {"default", "1e1e1e"}, {"black", "000000"},  {"dark-grey", "3a3a3c"},  {"slate", "334155"}, {"midnight-blue", "101935"},
    {"forest", "163a26"},  {"plum", "3b1f4a"},   {"light-grey", "d4d4d8"}, {"white", "ffffff"},
};

static const struct settings_ink settings_accents[] = {
    {"white", "ffffff"}, {"amber", "ff8800"}, {"blue", "2563eb"}, {"green", "16a34a"}, {"violet", "7c3aed"}, {"rose", "e11d48"}, {"graphite", "52525b"},
};

static const struct settings_ink address_to settings_ink_find(const struct settings_row address_to row, const char address_to text, b32 by_colour)
{
        b32 accent = row->path && string_equals((string_address)row->path, (string_address)SETTING_ACCENT_PATH);
        const struct settings_ink address_to table = accent ? settings_accents : settings_inks;
        positive count = accent ? sizeof(settings_accents) / sizeof(settings_accents[0]) : sizeof(settings_inks) / sizeof(settings_inks[0]);

        for (positive at = 0; at < count; at++)
                if (string_equals((string_address)text, (string_address)(by_colour ? table[at].colour : table[at].name)))
                        return &table[at];
        return null;
}

#define SETTINGS_UNREAD_WORDS ((sizeof(settings_rows) / sizeof(settings_rows[0]) + 63) / 64)
_Static_assert(sizeof(settings_rows) / sizeof(settings_rows[0]) <= 64 * SETTINGS_UNREAD_WORDS, "the rows to ask fit their words");

// Commands that require a second Enter to confirm.
static const char address_to const settings_asks[][3] = {
    {"wipe"},            {"setup", "install"}, {"setup", "update"},   {"setup", "use"},      {"setup", "rollback"}, {"sleep"},
    {"hibernate"},       {"bios", "reboot"},   {"desktop", "start"},  {"desktop", "rest"},   {"desktop", "off"},    {"link", "remove"},
    {"link", "group", "leave"}, {"wifi", "remove"}, {"bluetooth", "remove"}, {"cpu", "offline"}, {"init", "remove"}, {"exit", "remove"},
};

/* ---- the model: what a verb said, and what to run ---- */

// One line of verb output with escapes/controls stripped, tabs as spaces, leading indent preserved.
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

static positive settings_label_length(const char address_to line, positive length)
{
        positive rest = length;

        return (positive)(ui_strip_verb_prefix(line, &rest) - line);
}

// One line stripped of escapes, "[Moonwater] " prefix, and leading spaces.
static positive settings_line(const char address_to said, positive length, positive address_to at, char address_to into,
                              positive room)
{
        positive made = settings_keep(said, length, at, into, room);
        positive skip = settings_label_length(into, made);

        while (skip < made && into[skip] == ' ')
                skip++;
        if (skip)
                memory_copy(into, into + skip, made - skip + 1);
        return made - skip;
}

// Where key occurs in line at start or after space/semicolon, or length if absent.
static positive settings_find(const char address_to line, positive length, const char address_to key)
{
        positive size = string_length((string_address)key);

        for (positive at = 0; at + size <= length; at++)
                if ((!at || line[at - 1] == ' ' || line[at - 1] == ';') && !memory_compare(line + at, key, size))
                        return at;
        return length;
}

// Part of line before `; "` (escaped device product name).
static positive settings_unquoted(const char address_to line, positive length)
{
        for (positive at = 0; at + 2 < length; at++)
                if (line[at] == ';' && line[at + 1] == ' ' && line[at + 2] == '"')
                        return at;
        return length;
}

// Whether a line reports this row's hardware as absent.
static b32 settings_lacks(const char address_to line, positive length, const struct settings_row address_to row)
{
        if (settings_find(line, length, "this machine has no") < length || settings_find(line, length, "no wireless hardware") < length)
                return true;
        return settings_find(line, length, "this battery has no") < length &&
               !(row->key && row->key[0] && settings_find(line, length, row->key) < length);
}

static b32 settings_goes_on(const struct settings_row address_to row, char c)
{
        return (!row->stop || c != row->stop) && ((row->flags & SETTINGS_LINE) || (c != ' ' && c != ',' && c != ';' && c != '('));
}

// Parse a verb's output into row's value and return SETTINGS_*.
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

                        if (!key || (where = key[0] ? settings_find(line, size, key) : 0) >= size)
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
                        found = 1;
                }
        }
        if (found && row->kind == SETTINGS_SWITCH && row->lacks && !string_equals((string_address)value, (string_address) "on") &&
            !string_equals((string_address)value, (string_address) "off"))
                absent = true;
        return absent ? SETTINGS_ABSENT : found ? SETTINGS_READ : row->lacks ? SETTINGS_ABSENT : SETTINGS_FAILED;
}

// Read the Battery row directly via ui.c's battery reader.
static unsigned int settings_battery(string_address chosen, char address_to value, positive room)
{
        static const struct
        {
                unsigned char state;
                unsigned int text;
        } charge_words[] = {
            {BATTERY_CHARGING,     LANGUAGE_settings_battery_charging},
            {BATTERY_DISCHARGING,  LANGUAGE_settings_battery_discharging},
            {BATTERY_FULL,         LANGUAGE_settings_battery_full},
            {BATTERY_NOT_CHARGING, LANGUAGE_settings_battery_idle},
        };
        struct battery_state battery;

        value[0] = 0;
        if (chosen && string_equals(chosen, (string_address) "none"))
                return SETTINGS_ABSENT;
        if (!chosen || !chosen[0] || string_equals(chosen, (string_address) "auto") || !battery_read(chosen, &battery))
                battery_read(null, &battery);
        if (!battery.present)
                return SETTINGS_ABSENT;
        if (battery.percent < 0)
                string_copy_bounded((string_address)value, (string_address)WORDS(settings_unknown), room);
        else
                language_format(value, room, WORDS(settings_percent), (long)battery.percent);
        for (positive k = 0; k < sizeof(charge_words) / sizeof(charge_words[0]); k++)
                if (battery.charging == charge_words[k].state)
                        string_append_bounded((string_address)value, (string_address)language_text(charge_words[k].text), room);
        return SETTINGS_READ;
}

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
                const struct settings_ink address_to ink = settings_ink_find(row, value + 1, true);

                if (ink)
                        string_copy_bounded((string_address)value, (string_address)ink->name, room);
        }
        return state == SETTINGS_ABSENT && failed && !length ? SETTINGS_FAILED : state;
}

// Extract space-separated word at index into into; false past end.
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

static int settings_slider_step(const struct settings_row address_to row)
{
        return row->high - row->low >= 100 ? 5 : 1;
}

// Next value for switch (way > 1 on, < -1 off, else toggle) or step.
static fn settings_next(const struct settings_row address_to row, const char address_to value, int way, char address_to into,
                        positive room)
{
        into[0] = 0;
        if (row->kind == SETTINGS_SWITCH)
                string_copy_bounded((string_address)into,
                                    (string_address)(way > 1 ? "on" : way < -1 ? "off" : string_equals((string_address)value, (string_address) "on") ? "off" : "on"),
                                    room);
        else if (row->kind == SETTINGS_STEP)
                settings_word(row->choices, way > 0 ? 1 : 0, into, room);
}

// Split text on spaces (respecting single/double quotes) into buffer/words; 0 if out of room.
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

// Screen map state above the Display rows (ui_graph desk nodes, selection, pending layout, buttons, wallpaper cache).
struct settings_map
{
        int display;                     // the table's index of the screen the rows are for, -1 none chosen
        b32 keys;                        // the keyboard is on the map (Up from the first Display row): its arrows choose and move a screen
        struct ui_graph graph;
        struct ui_graph_node node[WINDOWS_OUTPUTS];
        char number[WINDOWS_OUTPUTS][4], size[WINDOWS_OUTPUTS][32]; // each screen's number, and its size (and that it is the primary)
        // The layout last sent (settings_map_place), each screen's place in the table's order: shown until the table has it, or
        // SETTINGS_MAP_HOLD_NS have passed (a place refused never comes).
        b32 sent;
        positive sent_at;
        unsigned int sent_count;
        int sent_x[WINDOWS_OUTPUTS], sent_y[WINDOWS_OUTPUTS];
        // The screens as the map last showed them: their names and places.
        unsigned int shown;
        char shown_name[WINDOWS_OUTPUTS][WINDOWS_OUTPUT_NAME];
        int shown_x[WINDOWS_OUTPUTS], shown_y[WINDOWS_OUTPUTS];
        struct ui_rect panel;            // the panel, and the graph's
        struct ui_control identify, fit; // the buttons across its top
        // Each screen's wallpaper, small, drawn on its node: what it was made from (settings_map_wall), so it is made again only for a change.
        unsigned int wall_key[WINDOWS_OUTPUTS];
        int wall_w[WINDOWS_OUTPUTS], wall_h[WINDOWS_OUTPUTS];
};

// Per-screen wallpaper thumbnail on the map (lazy mmap, freed in settings_close).
#define SETTINGS_WALL_W 192
#define SETTINGS_WALL_H 192
#define SETTINGS_WALL_BYTES ((positive)SETTINGS_WALL_W * SETTINGS_WALL_H * sizeof(unsigned int))
#define SETTINGS_WALL_DIR "/root/wallpapers"
#define SETTINGS_WALL_FILE_MOST (64ul << 20)
// Allocated lazily per output via anonymous mmap on first wallpaper draw, freed in settings_close.
static unsigned int address_to settings_wall_pixels[WINDOWS_OUTPUTS];

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

        // Kit page, list and card scroll state.
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
        // room its value has), open while editing is a row; and the context menu of either field, of a choice, or of a row's copies.
        struct ui_text edit;
        char edit_from[SETTINGS_VALUE];
        struct ui_popup popup;
        char status[SETTINGS_STATUS];
        int width, height;
        int least_w, least_h;      // the window's least size, device pixels (settings_least), and what it was worked out for
        unsigned int least_key;
        unsigned int ground;
        b32 focused;

        // What a command said, as a card in place of the list: its text and its lines cut to the window's width (the kit's).
        struct ui_lines card_text;
        char card_title[SETTINGS_STATUS];

        // The press again to confirm (the kit's): a command that asks for a second Enter (its row, and what was typed), a forget.
        struct ui_confirm confirm;
        // The event being taken is a key or a press from a device plugged into the machine (no WINDOW_EVENT_VIRTUAL): only such
        // a one makes a deliberate change (settings_refused). False between events, so a timer's change is never one.
        b32 hand;

        // The verb being run: backed by the kit's ui_job runner (anonymous layout alias preserves direct field access for checks).
        union
        {
                struct ui_job runner;
                struct
                {
                        bipolar child;
                        int output;
                        int ending;
                        b32 exited;
                        positive began;
                        positive runner_timeout;
                        char address_to runner_buf;
                        positive runner_room, said_length;
                };
        };
        int job;
        b32 job_sets;
        char job_value[SETTINGS_TYPED];
        positive unread[SETTINGS_UNREAD_WORDS]; // rows to ask, a bit each
        struct settings_change address_to queue; // changes to make once the verb running is done, in order (grown in memory)
        positive queue_room, queued;
        b32 told;         // the first reading is said in the machine's log

        struct settings_map map;
} settings;

_Static_assert(SETTINGS_ALL <= UI_LIST_MOST, "every row the window shows is a place of the kit's list");

// Lazily mapped large buffers (kept out of the shell image BSS).
static struct settings_large
{
        char said[SETTINGS_SAID];
        char pool[SETTINGS_POOL];
        char card[SETTINGS_CARD];
        unsigned short card_at[SETTINGS_CARD_LINES], card_size[SETTINGS_CARD_LINES];
        char split[SETTINGS_TYPED * 2];
        struct settings_item item[SETTINGS_LISTS][SETTINGS_LIST_ROWS];
        struct settings_drive drive[SETTINGS_DRIVES];
        struct ui_columns columns[SETTINGS_LISTS];
        struct ui_view view;
        char label_cut[SETTINGS_ROWS][SETTINGS_LABEL_CUT];
        unsigned char try_cover[2][SETTINGS_TRY_SPAN * SETTINGS_TRY_SPAN];
        unsigned char logo_cover[SETTINGS_LOGO_MAX_W * SETTINGS_LOGO_MAX_H];
} address_to settings_large;

static void address_to settings_alloc_pages(positive bytes)
{
        long mapped = ui_map((bytes + 4095) & ~(positive)4095);

        return mapped < 0 && mapped > -4096 ? null : (void address_to)mapped;
}

static b32 settings_map(void)
{
        if (!settings_large)
                settings_large = (struct settings_large address_to)settings_alloc_pages(sizeof(struct settings_large));
        return settings_large != null;
}

static fn settings_changed(int top, int bottom)
{
        ui_page_damage(&settings.page, top, bottom);
}

/* ---- the issue board: each row's badge and the lines under it (window.c's windows_issue_*) ---- */
static int settings_section_start(int row);
static fn settings_rebuild(void);
static fn settings_say(string_address text);
// Issue/status/partition sub-rows (list ids starting at SETTINGS_ALL), polled once per second.
#define SETTINGS_SUBS 80
// The rows that show a setting (settings_tries), numbered after the lines under the rows.
#define SETTINGS_TRY_REPEAT 0
#define SETTINGS_TRY_CLICK 1
#define SETTINGS_TRY_POINTER 2
#define SETTINGS_TRIES 3
#define SETTINGS_TRY ((int)SETTINGS_ALL + SETTINGS_SUBS) // the list's row of the first
// The About section on the System page: Moonwater logo + author/website, Discord invite, and GitHub issues link.
#define SETTINGS_ABOUT_BRAND 0
#define SETTINGS_ABOUT_DISCORD 1
#define SETTINGS_ABOUT_GITHUB 2
#define SETTINGS_ABOUT_COUNT 3
#define SETTINGS_ABOUT (SETTINGS_TRY + SETTINGS_TRIES)
#define SETTINGS_LIST_IDS (SETTINGS_ABOUT + SETTINGS_ABOUT_COUNT)
#define SETTINGS_SUB_TEXT 240      // bytes of one line
#define SETTINGS_BOARD_NS 1000000000ll
#define SETTINGS_REAP_NS 10000000ll // how often a child that is ending is asked whether it has exited (settings_reap)
#define SETTINGS_LEFT_NS 2000000000ll // how long a killed child is waited for before it is left (settings_leave)
#define SETTINGS_LEFT 8              // children left, reaped when they exit (settings_left_reap)
#define SETTINGS_GPU_SAMPLES 10    // samples of the GPU's presents the status is judged on (a second each)
struct settings_sub
{
        int section;                 // the section of the row it is under
        b32 detail;                  // a detail line: quieter
        const char address_to words; // what its badge says (Warning, Error), or none
        unsigned int code;           // a message line's issue code (a kernel notice's, for Delete to dismiss it), 0 for none
        unsigned int action;         // SETTINGS_SUB_COPY (the default), or SETTINGS_SUB_DISMISS_ALL (the line that clears every kernel notice)
        int owner;                   // the row the line is under (its fold: how much of a row the line shows while the row opens or shuts)
        int part;                    // a drive's partition row: its segment, or SETTINGS_PART_FREE or SETTINGS_PART_MORE
        char text[SETTINGS_SUB_TEXT];
};
#define SETTINGS_SUB_COPY 0u
#define SETTINGS_SUB_DISMISS_ALL 1u
#define SETTINGS_SUB_PARTITIONS 2u // a drive's disclosure: a press, Enter or Space opens or shuts its partitions, as Right and Left do
#define SETTINGS_SUB_PARTITION 3u  // a row of them, which a press does nothing to
#define SETTINGS_PART_FREE -1      // the row of the free room the host lists, all of it
#define SETTINGS_PART_MORE -2      // the row of the partitions the host did not show
static struct settings_sub settings_sub[SETTINGS_SUBS];
static int settings_sub_count, settings_sub_owner, settings_sub_cut; // settings_sub_cut: the lines past the last kept (settings_sub_put)
static struct windows_issues settings_board;
static unsigned char settings_expanded[SETTINGS_ALL]; // each row's expander: 1 when the lines under it show (the GPU's, or a row with an issue)
// Each row's fold: how far its lines are open, Q30 (0 shut, UI_ONE open), moved by a spring from the active fold pool while live.
static b64 settings_fold_at[SETTINGS_ALL];
static unsigned char settings_fold_live[SETTINGS_ALL];
#define SETTINGS_FOLD_POOL 16
static struct settings_fold_slot
{
        int row; // active row + 1 (0 means free)
        struct ui_spring spring;
} settings_fold_pool[SETTINGS_FOLD_POOL];
static positive settings_board_due;
static char settings_status[SETTINGS_SUB_TEXT], settings_metric_line[SETTINGS_SUB_TEXT];
static char settings_metric_shown[SETTINGS_SUB_TEXT]; // the metric line the page was last made with (settings_rebuild copies it)
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
                string_copy_bounded((string_address)key, (string_address)WINDOWS_KEY_NOTICES, room);
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

/*
        The drives whose partitions show, by their names, while the window is open: a reading of the device list puts a drive's row
        where the list has it now (another device before it moves it), and its partitions show as they did (settings_drive_follow).
*/
static char settings_drives_open[SETTINGS_LIST_ROWS][SETTINGS_ITEM_LABEL];

static b32 settings_drive_is_open(const char address_to name)
{
        for (positive at = 0; at < SETTINGS_LIST_ROWS; at++)
                if (settings_drives_open[at][0] && string_equals((string_address)settings_drives_open[at], (string_address)name))
                        return true;
        return false;
}

// A drive's partitions remembered as showing or not: its name kept, in the first free place (the last when none is), or let go.
static fn settings_drive_keep(const char address_to name, b32 open)
{
        positive free = SETTINGS_LIST_ROWS - 1;

        for (positive at = 0; at < SETTINGS_LIST_ROWS; at++)
                if (settings_drives_open[at][0] && string_equals((string_address)settings_drives_open[at], (string_address)name))
                        settings_drives_open[at][0] = 0;
        if (!open)
                return;
        for (positive at = SETTINGS_LIST_ROWS; at-- > 0;)
                free = settings_drives_open[at][0] ? free : at;
        string_copy_bounded((string_address)settings_drives_open[free], (string_address)name, sizeof(settings_drives_open[free]));
}

static struct settings_item address_to settings_item(int row);

static fn settings_fold_drop(int row)
{
        for (positive k = 0; k < SETTINGS_FOLD_POOL; k++)
                if (settings_fold_pool[k].row == row + 1)
                        settings_fold_pool[k].row = 0;
}

static fn settings_fold_start(int row, b64 from_target)
{
        positive slot = SETTINGS_FOLD_POOL;

        for (positive k = 0; k < SETTINGS_FOLD_POOL; k++)
        {
                int who = settings_fold_pool[k].row - 1;

                if (who == row)
                {
                        slot = k;
                        break;
                }
                if (who < 0 || !settings_fold_live[who])
                {
                        settings_fold_pool[k].row = 0;
                        if (slot == SETTINGS_FOLD_POOL)
                                slot = k;
                }
        }
        if (slot == SETTINGS_FOLD_POOL)
        {
                int old = settings_fold_pool[0].row - 1;

                if (old >= 0 && old < (int)SETTINGS_ALL)
                        settings_fold_at[old] = settings_expanded[old] ? UI_ONE : 0, settings_fold_live[old] = 0;
                slot = 0;
        }
        settings_fold_pool[slot].row = row + 1;
        ui_spring_go(&settings_fold_pool[slot].spring, from_target, 0);
        settings_fold_live[row] = 1;
}

// Opens or shuts a row's lines (an option's, or a drive's partitions), and lists them again. They fold on the kit's spring
// (settings_fold_step moves them frame by frame); with reduced motion they are at their place at once.
static fn settings_toggle(int row)
{
        const struct settings_item address_to item = settings_item(row);
        b64 target;

        if (row < 0 || row >= (int)SETTINGS_ALL || (row >= SETTINGS_ANY && !(item && (item->flags & (SETTINGS_ITEM_DRIVE | SETTINGS_ITEM_DISK)))))
                return;
        settings_expanded[row] = !settings_expanded[row];
        if (item)
                settings_drive_keep(item->flags & SETTINGS_ITEM_DISK ? item->port : item->label, settings_expanded[row]);
        target = settings_expanded[row] ? UI_ONE : 0;
        if (ui_kit.motion)
                settings_fold_at[row] = target, settings_fold_live[row] = 0, settings_fold_drop(row);
        else
                settings_fold_start(row, settings_fold_at[row] - target);
        settings_rebuild();
}

// A line under a row is copied to the clipboard as its text (a press on it): the message, or the detail, as it stands.
static void settings_copy(int at)
{
        if (at < 0 || at >= settings_sub_count)
                return;
        windows_clip_offer(&settings.own, WINDOWS_CLIP_CLIPBOARD, settings_sub[at].text, string_length((string_address)settings_sub[at].text));
        settings_say((string_address)WORDS(settings_copied));
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
        if (settings_gpu_status == WINDOWS_GPU_STATUS_OFF || settings_gpu_status == WINDOWS_GPU_STATUS_NONE)
                language_format(settings_status, sizeof(settings_status),
                                settings_gpu_status == WINDOWS_GPU_STATUS_OFF ? WORDS(settings_status_off) : WORDS(settings_status_none));
        else if (!settings_gpu_shown_total)
                language_format(settings_status, sizeof(settings_status), WORDS(settings_status_waiting));
        else
                language_format(settings_status, sizeof(settings_status),
                                settings_gpu_status == WINDOWS_GPU_STATUS_IN_USE ? WORDS(settings_status_in_use)
                                : settings_gpu_status == WINDOWS_GPU_STATUS_PARTLY ? WORDS(settings_status_partly) : WORDS(settings_status_not),
                                (long)drawn, (long)settings_gpu_shown_total, (long)(drawn * 100 / settings_gpu_shown_total));
        settings_gpu_key = settings_gpu_status;
        if (settings_gpu_status != WINDOWS_GPU_STATUS_OFF && settings_gpu_status != WINDOWS_GPU_STATUS_NONE && settings_gpu_shown_total)
                settings_gpu_key |= 1u << 8 | (drawn * 100 / settings_gpu_shown_total) << 9;
}

// Append a sub-row at list position count (replacing the last slot with a "not shown" note on overflow).
static int settings_sub_put(int section, b32 detail, const char address_to words, const char address_to text, int count)
{
        struct settings_sub address_to sub;
        b32 overflow = settings_sub_count >= SETTINGS_SUBS;

        if (count >= UI_LIST_MOST - 1)
                return count;
        sub = &settings_sub[overflow ? SETTINGS_SUBS - 1 : settings_sub_count];
        memory_zero(sub, sizeof(*sub));
        sub->section = section, sub->owner = settings_sub_owner;
        if (overflow)
        {
                settings_sub_cut++;
                language_format(sub->text, sizeof(sub->text), WORDS(settings_not_shown), (long)(settings_sub_cut + 1));
                return count;
        }
        sub->detail = detail, sub->words = words;
        string_copy_bounded((string_address)sub->text, (string_address)text, sizeof(sub->text));
        settings.list.id[count] = (unsigned short)(SETTINGS_ALL + (positive)settings_sub_count++);
        return count + 1;
}

static int settings_sub_add(int section, b32 detail, const char address_to words, const char address_to text,
                            unsigned int code, unsigned int action, int part, int count)
{
        int next = settings_sub_put(section, detail, words, text, count);

        if (next != count)
                settings_sub[settings_sub_count - 1].code = code, settings_sub[settings_sub_count - 1].action = action,
                settings_sub[settings_sub_count - 1].part = part;
        return next;
}

// The words of a prerequisite of the GPU path that failed (gpu.c's enum gpu_req, the step the board's a number is): the fix, in the reader's
// language. None for a step this version does not name.
static const unsigned int settings_req_ids[] = {
    0,
    LANGUAGE_settings_req_node,
    LANGUAGE_settings_req_driver,
    LANGUAGE_settings_req_family,
    LANGUAGE_settings_issue_gpu_userptr,
    LANGUAGE_settings_req_context,
    LANGUAGE_settings_req_ring,
    LANGUAGE_settings_req_selftest,
    LANGUAGE_settings_req_udmabuf,
    LANGUAGE_settings_req_prime,
};

static const char address_to settings_req_words(unsigned int step)
{
        return step > 0 && step < sizeof(settings_req_ids) / sizeof(settings_req_ids[0]) ? language_text(settings_req_ids[step]) : null;
}

static const struct settings_issue_spec
{
        unsigned int code, text;
        unsigned char fmt; // 0: none, 1: (a), 2: (a, b), 3: (detail), 4: (a, detail)
} settings_issue_specs[] = {
    {WINDOWS_ISSUE_GPU_MAP,       LANGUAGE_settings_issue_gpu_map,       2},
    {WINDOWS_ISSUE_GPU_USERPTR,   LANGUAGE_settings_issue_gpu_userptr,   0},
    {WINDOWS_ISSUE_GPU_REFUSED,   LANGUAGE_settings_issue_gpu_refused,   4},
    {WINDOWS_ISSUE_GPU_LISTS,     LANGUAGE_settings_issue_gpu_lists,     0},
    {WINDOWS_ISSUE_GPU_COOLING,   LANGUAGE_settings_issue_gpu_cooling,   1},
    {WINDOWS_ISSUE_GPU_UNTRUSTED, LANGUAGE_settings_issue_gpu_untrusted, 1},
    {WINDOWS_ISSUE_GPU_FAMILY,    LANGUAGE_settings_issue_gpu_family,    1},
    {WINDOWS_ISSUE_GPU_FAULT,     LANGUAGE_settings_issue_gpu_fault,     3},
    {WINDOWS_ISSUE_GPU_HUNG,      LANGUAGE_settings_issue_gpu_hung,      0},
    {WINDOWS_ISSUE_GPU_LOST,      LANGUAGE_settings_issue_gpu_lost,      2},
    {WINDOWS_ISSUE_GPU_SCANOUT,   LANGUAGE_settings_issue_gpu_scanout,   1},
    {WINDOWS_ISSUE_AUDIO_OPEN,    LANGUAGE_settings_issue_audio_open,    1},
    {WINDOWS_ISSUE_AUDIO_RATE,    LANGUAGE_settings_issue_audio_rate,    2},
};

// The text of an issue's message, in the reader's language: its code's words, with its numbers, or the words that say a code this version
// does not name.
static void settings_issue_text(const struct windows_issue address_to issue, char address_to into, positive room)
{
        if (issue->code == WINDOWS_ISSUE_GPU_REQUIRES)
        {
                const char address_to req = settings_req_words(issue->a);

                if (req) // (the driver's and the chip's words say the detail: which driver, which chip)
                        language_format(into, room, req, issue->detail);
                else
                        language_format(into, room, WORDS(settings_issue_unknown), (long)issue->code);
                return;
        }
        // A kernel notice's words are the kernel's own, as its line says them (not translated yet: the detail is English).
        if (windows_issue_kernel(issue->code) || issue->code == WINDOWS_ISSUE_RADIO || issue->code == WINDOWS_ISSUE_NOTIFICATION)
        {
                string_copy_bounded((string_address)into, (string_address)issue->detail, room);
                return;
        }
        for (positive at = 0; at < sizeof(settings_issue_specs) / sizeof(settings_issue_specs[0]); at++)
                if (settings_issue_specs[at].code == issue->code)
                {
                        const char address_to text = language_text(settings_issue_specs[at].text);

                        switch (settings_issue_specs[at].fmt)
                        {
                        case 0: language_format(into, room, text); return;
                        case 1: language_format(into, room, text, (long)issue->a); return;
                        case 2: language_format(into, room, text, (long)issue->a, (long)issue->b); return;
                        case 3: language_format(into, room, text, issue->detail); return;
                        case 4: language_format(into, room, text, (long)issue->a, issue->detail); return;
                        }
                }
        language_format(into, room, WORDS(settings_issue_unknown), (long)issue->code);
}

static int settings_sub_rows(int row, int count)
{
        char key[WINDOWS_ISSUE_KEY], text[SETTINGS_SUB_TEXT];
        const struct settings_row address_to which = row < SETTINGS_ANY ? &settings_rows[row] : null;
        int section;

        if (!settings_key_of(row, key, sizeof(key)) ||
            (!settings_always_open(row) && (!(settings_expanded[row] || settings_fold_at[row]) || !settings_expander(row))))
                return count;
        settings_sub_owner = row;
        section = settings_section_start(row);
        if (string_equals((string_address)key, (string_address)WINDOWS_KEY_GPU) && which && which->kind == SETTINGS_SWITCH)
        {
                count = settings_sub_put(section, false, null, settings_status, count);
                count = settings_sub_put(section, true, null, settings_metric_line, count);
        }
        for (int i = 0; i < WINDOWS_ISSUES && count < UI_LIST_MOST - 1; i++)
        {
                const struct windows_issue address_to issue = &settings_board.issue[i];

                if (!issue->used || !windows_issue_same(issue->key, key) || issue->code == WINDOWS_ISSUE_NOTIFICATION)
                        continue;
                settings_issue_text(issue, text, sizeof(text));
                count = settings_sub_add(section, false, issue->severity == WINDOWS_ISSUE_ERROR ? WORDS(settings_badge_error) : WORDS(settings_badge_warning),
                                         text, issue->code, SETTINGS_SUB_COPY, 0, count);
                if (issue->detail[0] && issue->code != WINDOWS_ISSUE_GPU_REQUIRES && (issue->code < WINDOWS_ISSUE_KERNEL_GPU || issue->code > WINDOWS_ISSUE_NOTIFICATION))
                        count = settings_sub_put(section, true, null, issue->detail, count);
        }
        if (which && which->output == SETTINGS_OUT_NOTICES && count > 0 && settings_sub_count > 0)
                count = settings_sub_add(section, false, null, WORDS(settings_notices_dismiss), 0, SETTINGS_SUB_DISMISS_ALL, 0, count);
        return count;
}

// A line under a row, described for the kit: indented, its text quiet, and its badge's words at the right when it is an issue.
static fn settings_part_describe(const struct settings_sub address_to sub, struct ui_row address_to row);

static fn settings_sub_describe(int at, struct ui_row address_to row)
{
        const struct settings_sub address_to sub = &settings_sub[at];

        if (sub->action == SETTINGS_SUB_PARTITIONS || sub->action == SETTINGS_SUB_PARTITION)
        {
                settings_part_describe(sub, row);
                return;
        }

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

        if (!settings.own.table)
                return -1;
        if (settings_board_due && now < settings_board_due)
                return (long)(settings_board_due - now);
        settings_board_due = now + (positive)SETTINGS_BOARD_NS;
        if (!windows_issues_read(settings.own.table, &copy))
                return (long)SETTINGS_BOARD_NS;
        settings_gpu_sample();
        // The status line is made again when its key moves (settings_gpu_sample). The metric line is rates of presents, and a page made again presents,
        // so its own change makes the page again no more often than SETTINGS_METRIC_NS, measured from the last time the page was made: the line
        // is compared with the one the page shows (settings_metric_shown), so a change inside that window is made once the window has passed.
        if (memory_compare(&copy, &settings_board, sizeof(copy)) || settings_gpu_key != was ||
            (!string_equals((string_address)settings_metric_shown, (string_address)settings_metric_line) && now - settings_metric_at >= (positive)SETTINGS_METRIC_NS))
        {
                settings_board = copy;
                settings_metric_at = now;
                settings_rebuild();
        }
        return (long)SETTINGS_BOARD_NS;
}

/* ---- the rows ---- */

enum settings_domain_kind
{
        SETTINGS_DOM_NONE = -1,
        SETTINGS_DOM_ROW = 0,     // [0, SETTINGS_ANY): registry settings_rows[offset]
        SETTINGS_DOM_COMMAND = 1, // [SETTINGS_ANY, SETTINGS_LIST): 0 = Run any, >0 = generic[offset - 1]
        SETTINGS_DOM_LIST = 2,    // [SETTINGS_LIST, SETTINGS_ALL): feed list items
        SETTINGS_DOM_SUB = 3,     // [SETTINGS_ALL, SETTINGS_TRY): issue/status/partition sub-rows
        SETTINGS_DOM_TRY = 4,     // [SETTINGS_TRY, SETTINGS_ABOUT): interactive try rows
        SETTINGS_DOM_ABOUT = 5,   // [SETTINGS_ABOUT, SETTINGS_LIST_IDS): About section rows
};

static int settings_domain_of(int id, int address_to offset)
{
        static const int stops[7] = {0, SETTINGS_ANY, (int)SETTINGS_LIST, (int)SETTINGS_ALL, SETTINGS_TRY, SETTINGS_ABOUT, SETTINGS_LIST_IDS};

        for (int k = 0; k < 6; k++)
                if (id >= stops[k] && id < stops[k + 1])
                {
                        if (offset)
                                *offset = id - stops[k];
                        return k;
                }
        if (offset)
                *offset = -1;
        return SETTINGS_DOM_NONE;
}

static positive settings_rows_count(void)
{
        return (positive)SETTINGS_ANY + 1 + settings.generic_count;
}

static const struct settings_generic address_to settings_generic_of(int row)
{
        int at;

        return settings_domain_of(row, &at) == SETTINGS_DOM_COMMAND && at > 0 ? &settings.generic[at - 1] : null;
}

static int settings_about_of(int id);
static const char address_to settings_about_label(int which);
static b32 settings_about_matches_word(int which, const char address_to word, positive size);
static int settings_system_group(void);
static int settings_groups(void);
static const char address_to settings_group_name(int group);
static int settings_device_section(const struct settings_item address_to item);

static int settings_list_of(int row)
{
        int at;

        return settings_domain_of(row, &at) == SETTINGS_DOM_LIST ? at / SETTINGS_LIST_ROWS : -1;
}

static struct settings_item address_to settings_item(int row)
{
        int offset, list, at;

        if (settings_domain_of(row, &offset) != SETTINGS_DOM_LIST)
                return null;
        list = offset / SETTINGS_LIST_ROWS, at = offset % SETTINGS_LIST_ROWS;
        return list < SETTINGS_LISTS && at < settings.item_count[list] ? &settings_large->item[list][at] : null;
}

static const char address_to settings_label_get(int row, b32 shown)
{
        int at;

        switch (settings_domain_of(row, &at))
        {
        case SETTINGS_DOM_ROW:
                return shown ? language_of(settings_rows[at].label) : settings_rows[at].label;
        case SETTINGS_DOM_COMMAND:
                return at > 0 ? settings_large->pool + settings.generic[at - 1].command : WORDS(settings_run_any);
        case SETTINGS_DOM_LIST:
        {
                const struct settings_item address_to item = settings_item(row);

                return !item ? WORDS(settings_run_any) : shown && item->kind == SETTINGS_ADD ? language_of(item->label) : item->label;
        }
        case SETTINGS_DOM_ABOUT:
                return shown ? settings_about_label(at) : WORDS(settings_run_any);
        default:
                return WORDS(settings_run_any);
        }
}

static const char address_to settings_label(int row) { return settings_label_get(row, false); }
static const char address_to settings_label_shown(int row) { return settings_label_get(row, true); }

// Label up to any trailing " (...)" unit suffix, since controls show units themselves.
static const char address_to settings_label_window(int row)
{
        const char address_to shown = settings_label_shown(row);
        positive length, open;

        if (row < 0 || row >= SETTINGS_ANY || !settings_rows[row].unit || !settings_large)
                return shown;
        length = string_length((string_address)shown);
        if (length < 2 || shown[length - 1] != ')')
                return shown;
        for (open = length - 1; open > 0 && shown[open] != '('; open--)
                ;
        if (!open || shown[open] != '(' || shown[open - 1] != ' ')
                return shown;
        while (open > 0 && shown[open - 1] == ' ')
                open--;
        if (!open || open >= sizeof(settings_large->label_cut[0]))
                return shown;
        memory_copy(settings_large->label_cut[row], shown, open);
        settings_large->label_cut[row][open] = 0;
        return settings_large->label_cut[row];
}

static const char address_to settings_item_text(const struct settings_item address_to item)
{
        if (item->kind == SETTINGS_NETWORK)
                return item->flags & SETTINGS_ITEM_JOINED   ? WORDS(settings_item_joined)
                       : item->flags & SETTINGS_ITEM_CANNOT ? WORDS(settings_item_cannot)
                       : item->flags & SETTINGS_ITEM_OPEN   ? WORDS(settings_item_open)
                       : item->flags & SETTINGS_ITEM_SAVED  ? WORDS(settings_item_saved)
                                                            : WORDS(settings_item_ask);
        return item->flags & SETTINGS_ITEM_DRIVE ? WORDS(settings_item_drive)
               : item->kind == SETTINGS_DEVICE   ? WORDS(settings_item_kept)
               : item->kind == SETTINGS_LINK     ? WORDS(settings_item_link)
               : item->kind == SETTINGS_ADD      ? WORDS(settings_item_add) : "";
}

static const char address_to settings_text(int row)
{
        const struct settings_generic address_to generic = settings_generic_of(row);
        const struct settings_item address_to item = settings_item(row);

        return item ? settings_item_text(item) : generic ? settings_large->pool + generic->text : WORDS(settings_any_text);
}

static b32 settings_takes(int row)
{
        const struct settings_generic address_to generic = settings_generic_of(row);

        return !generic || settings_large->pool[generic->command + generic->verb] != 0;
}

static int settings_device_section(const struct settings_item address_to item)
{
        return item->where == SETTINGS_WHERE_INTERNAL ? 0 : item->where == SETTINGS_WHERE_NONE ? 2 : 1;
}

static int settings_section_start(int row)
{
        int offset;

        switch (settings_domain_of(row, &offset))
        {
        case SETTINGS_DOM_ROW:
                while (row > 0 && !settings_rows[row].section && !settings_rows[row].part)
                        row--;
                return row;
        case SETTINGS_DOM_COMMAND:
                return SETTINGS_ANY;
        case SETTINGS_DOM_LIST:
        {
                int list = offset / SETTINGS_LIST_ROWS, at = offset % SETTINGS_LIST_ROWS;
                const struct settings_item address_to item = settings_item(row);

                if (list == 2 && item)
                {
                        int section = settings_device_section(item);

                        while (at > 0 && settings_device_section(&settings_large->item[2][at - 1]) == section)
                                at--;
                        return (int)SETTINGS_LIST + 2 * SETTINGS_LIST_ROWS + at;
                }
                return (int)SETTINGS_LIST + list * SETTINGS_LIST_ROWS;
        }
        default:
                return row;
        }
}

static const char address_to settings_section(int row)
{
        int offset;

        switch (settings_domain_of(row, &offset))
        {
        case SETTINGS_DOM_ROW:
                while (row > 0 && !settings_rows[row].section)
                        row--;
                return settings_rows[row].section;
        case SETTINGS_DOM_COMMAND:
                return SETTINGS_ALL_HEADER;
        case SETTINGS_DOM_LIST:
        {
                int list = offset / SETTINGS_LIST_ROWS;
                const struct settings_item address_to item = settings_item(row);

                if (list == 2 && item && settings_device_section(item))
                        return settings_device_section(item) == 1 ? SETTINGS_EXTERNAL_HEADER : "";
                return settings_feeds[list].heading;
        }
        default:
                return "";
        }
}

static const char address_to settings_header(int row)
{
        int start = settings_section_start(row);

        return start < SETTINGS_ANY && settings_rows[start].part ? settings_rows[start].part : settings_section(row);
}

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

static const char address_to settings_group_text(int group)
{
        return language_of(settings_group_name(group));
}

static int settings_group_of(int row)
{
        int offset;

        switch (settings_domain_of(row, &offset))
        {
        case SETTINGS_DOM_ROW:
        {
                int group = -1;

                for (int at = 0; at <= offset; at++)
                        group += settings_rows[at].section != null;
                return group;
        }
        case SETTINGS_DOM_COMMAND:
                return settings_groups() - 1;
        case SETTINGS_DOM_LIST:
                for (int at = 0; at < settings_groups(); at++)
                        if (string_equals((string_address)settings_group_name(at), (string_address)settings_feeds[offset / SETTINGS_LIST_ROWS].group))
                                return at;
                return 0;
        case SETTINGS_DOM_ABOUT:
                return settings_system_group();
        default:
                return 0;
        }
}

static int settings_pos(int row) { return ui_list_at(&settings.list, row); }
static int settings_editing(void) { return settings.edit.open ? settings.edit.owner : -1; }

static b32 settings_has(const char address_to text, const char address_to wanted, positive size)
{
        positive length = text ? string_length((string_address)text) : 0;

        return length >= size && (memory_search_ascii_case)((address_any)text, length, (address_any)wanted, size) != null;
}

static b32 settings_matches_name(const char address_to label, const char address_to section, const char address_to word, positive size)
{
        return settings_has(label, word, size) || settings_has(section, word, size) ||
               (language.loaded && (settings_has(language_of(label), word, size) || settings_has(language_of(section), word, size)));
}

static b32 settings_matches_word(int row, const char address_to word, positive size)
{
        int offset;

        switch (settings_domain_of(row, &offset))
        {
        case SETTINGS_DOM_ROW:
        {
                const struct settings_row address_to which = &settings_rows[offset];

                for (positive at = 0; at < SETTING_WORDS; at++)
                        if (settings_has(which->read[at], word, size) || settings_has(which->set[at], word, size))
                                return true;
                return settings_has(settings.value[offset], word, size) || settings_matches_name(which->label, settings_section(row), word, size) ||
                       settings_matches_name(which->about, settings_rows[settings_section_start(row)].part, word, size);
        }
        case SETTINGS_DOM_LIST:
                return settings_matches_name(settings_label(row), settings_section(row), word, size);
        case SETTINGS_DOM_ABOUT:
                return settings_about_matches_word(offset, word, size);
        case SETTINGS_DOM_COMMAND:
        case SETTINGS_DOM_SUB:
        case SETTINGS_DOM_TRY:
                return settings_has(settings_label(row), word, size) || settings_has(settings_text(row), word, size) ||
                       (row == SETTINGS_ANY && settings_has("anything", word, size));
        default:
                return false;
        }
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

#define SETTINGS_MAP_STRIP 36     // logical pixels: the strip across the panel's top, where its buttons sit over the grid
#define SETTINGS_MAP_PANEL 150    // logical pixels: the panel the screens are drawn in, at its least (the window's least height has it)
#define SETTINGS_MAP_TALL 360     // logical pixels: the most it grows to, two fifths of a taller window
#define SETTINGS_MAP_BUTTON_W 168 // logical pixels: the identify button, and the one that shows every screen
#define SETTINGS_MAP_BUTTON_H 28
#define SETTINGS_MAP_HOLD_NS 4000000000ull // how long a layout sent is shown while the table has not caught up with it
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
        unsigned long magnitude = value < 0 ? (unsigned long)(-(value + 1)) + 1 : (unsigned long)value;

        if (value < 0)
                at = settings_map_add(into, room, at, "-");
        digits[positive_into((p8 address_to)digits, (positive)magnitude)] = 0;
        return settings_map_add(into, room, at, digits);
}

/*
        The table moved the screens the map showed (a layout sent came back with the desk's top left at 0, 0 again, or a command
        moved them): the move most of the screens it showed share, by name, is the plane's own, and the camera moves with it
        (ui_graph_shift), so the screens stay where they are on the map and only one that really moved is seen to. With no move
        more common than none, nothing is done.
*/
static fn settings_map_follow(unsigned int count)
{
        struct windows address_to table = settings.own.table;
        int dx[WINDOWS_OUTPUTS], dy[WINDOWS_OUTPUTS];
        unsigned int moves = 0, most = 0, still = 0;
        int best = -1;

        for (unsigned int at = 0; at < count; at++)
                for (unsigned int was = 0; was < settings.map.shown; was++)
                        if (string_equals((string_address)settings.map.shown_name[was], (string_address)table->output_name[at]))
                        {
                                dx[moves] = settings.map.node[at].x - settings.map.shown_x[was];
                                dy[moves++] = settings.map.node[at].y - settings.map.shown_y[was];
                                break;
                        }
        for (unsigned int at = 0; at < moves; at++)
        {
                unsigned int same = 0;

                for (unsigned int other = 0; other < moves; other++)
                        same += dx[other] == dx[at] && dy[other] == dy[at];
                if (!dx[at] && !dy[at])
                        still = same;
                else if (same > most)
                        most = same, best = (int)at;
        }
        if (best >= 0 && most > still)
                ui_graph_shift(&settings.map.graph, dx[best], dy[best]);
        settings.map.shown = count;
        for (unsigned int at = 0; at < count; at++)
        {
                memory_copy(settings.map.shown_name[at], table->output_name[at], WINDOWS_OUTPUT_NAME);
                settings.map.shown_x[at] = settings.map.node[at].x, settings.map.shown_y[at] = settings.map.node[at].y;
        }
}

// A path's type (the S_IFMT bits, 0 for none) and its change time folded to a word, a link not followed.
static unsigned int settings_wall_look(const char address_to path, unsigned int address_to stamp)
{
        // struct statx: stx_mode at 28 (u16), stx_mtime at 112 (i64 seconds, u32 nanoseconds).
        p8 info[256] = {0};

        if (system_call_5(syscall(statx), (positive)AT_FDCWD, (positive)path, 0x100, 0x41, (positive)info) < 0)
                return 0;
        if (stamp)
                *stamp = (unsigned int)(memory_load_unaligned(b64, info + 112) * 1000003) ^ memory_load_unaligned(p32, info + 120);
        return memory_load_unaligned(p16, info + 28) & 0170000u;
}

// A small file of /root's first word into word (room bytes), "" for none.
static fn settings_wall_word(const char address_to path, char address_to word, positive room)
{
        bipolar handle = system_open_at(AT_FDCWD, (string_address)path, FILE_READ | O_NOFOLLOW | O_CLOEXEC), got = 0;

        word[0] = 0;
        if (handle < 0)
                return;
        got = system_read_once(handle, word, room - 1);
        system_close(handle);
        word[got > 0 ? got : 0] = 0;
        for (positive at = 0; word[at]; at++)
                if (word[at] == '\n' || word[at] == ' ' || word[at] == '\r')
                {
                        word[at] = 0;
                        break;
                }
}

// The screen's own picture's file into path, where it has one.
static b32 settings_wall_own(const char address_to name, char address_to path, positive room)
{
        if (!name || !name[0] || string_length((string_address)SETTINGS_WALL_DIR "/") + string_length((string_address)name) + 5 > room)
                return false;
        string_copy((string_address)path, (string_address)SETTINGS_WALL_DIR "/");
        string_append_bounded((string_address)path, (string_address)name, room);
        string_append_bounded((string_address)path, (string_address) ".png", room);
        return settings_wall_look(path, null) == 0100000u;
}

// A PNG's file covering w by h pixels at into (each the mean of the picture's under it): false for one the kit cannot read.
static b32 settings_wall_picture(const char address_to path, unsigned int address_to into, int w, int h)
{
        bipolar handle = system_open_at(AT_FDCWD, (string_address)path, FILE_READ | O_NOFOLLOW | O_CLOEXEC);
        p8 address_to file;
        positive got = 0, vis_w, vis_h, ox, oy;
        unsigned int address_to picture = null;
        int width = 0, height = 0;

        if (handle < 0)
                return false;
        file = (p8 address_to)memory_checked(SETTINGS_WALL_FILE_MOST);
        while (file && got < SETTINGS_WALL_FILE_MOST)
        {
                bipolar read = system_read_once(handle, file + got, SETTINGS_WALL_FILE_MOST - got);

                if (read <= 0)
                        break;
                got += (positive)read;
        }
        system_close(handle);
        if (!file)
                return false;
        if (got >= 24 && got < SETTINGS_WALL_FILE_MOST)
        {
                positive pw = (positive)file[16] << 24 | (positive)file[17] << 16 | (positive)file[18] << 8 | file[19];
                positive ph = (positive)file[20] << 24 | (positive)file[21] << 16 | (positive)file[22] << 8 | file[23];

                if (pw && ph && pw <= 16384 && ph <= 16384 && pw * ph <= (48ul << 20))
                        picture = ui_png_decode_most(file, got, &width, &height, 16384);
        }
        memory_free(file, SETTINGS_WALL_FILE_MOST);
        if (!picture)
                return false;
        if ((positive)width * (positive)h > (positive)height * (positive)w)
                vis_h = (positive)height, vis_w = (positive)height * (positive)w / (positive)h, ox = ((positive)width - vis_w) / 2, oy = 0;
        else
                vis_w = (positive)width, vis_h = (positive)width * (positive)h / (positive)w, ox = 0, oy = ((positive)height - vis_h) / 2;
        vis_w = vis_w ? vis_w : 1, vis_h = vis_h ? vis_h : 1;
        for (int y = 0; y < h; y++)
        {
                positive y0 = oy + (positive)y * vis_h / (positive)h, y1 = oy + ((positive)y + 1) * vis_h / (positive)h;

                y1 = y1 > y0 ? y1 : y0 + 1;
                for (int x = 0; x < w; x++)
                {
                        positive x0 = ox + (positive)x * vis_w / (positive)w, x1 = ox + ((positive)x + 1) * vis_w / (positive)w, r = 0, g = 0, b = 0, n;

                        x1 = x1 > x0 ? x1 : x0 + 1;
                        n = (y1 - y0) * (x1 - x0);
                        for (positive sy = y0; sy < y1; sy++)
                                for (positive sx = x0; sx < x1; sx++)
                                {
                                        unsigned int pixel = picture[sy * (positive)width + sx];

                                        r += pixel >> 16 & 255, g += pixel >> 8 & 255, b += pixel & 255;
                                }
                        into[y * w + x] = 0xff000000u | (unsigned int)(r / n) << 16 | (unsigned int)(g / n) << 8 | (unsigned int)(b / n);
                }
        }
        memory_free(picture, (positive)width * (positive)height * 4);
        return true;
}

// Per-output wallpaper thumbnail buffer, mapped lazily on first draw and released on window close.
static unsigned int address_to settings_wall_slot(unsigned int at)
{
        if (at >= WINDOWS_OUTPUTS)
                return null;
        if (!settings_wall_pixels[at])
                settings_wall_pixels[at] = (unsigned int address_to)settings_alloc_pages(SETTINGS_WALL_BYTES);
        return settings_wall_pixels[at];
}

static fn settings_wall_free(void)
{
        for (unsigned int at = 0; at < WINDOWS_OUTPUTS; at++)
                if (settings_wall_pixels[at])
                {
                        compositor_call(syscall(munmap), (positive)settings_wall_pixels[at], (SETTINGS_WALL_BYTES + 4095) & ~(positive)4095, 0, 0, 0, 0);
                        settings_wall_pixels[at] = null;
                        settings.map.wall_key[at] = 0;
                        settings.map.wall_w[at] = settings.map.wall_h[at] = 0;
                }
}

static unsigned int settings_fnv32_str(unsigned int h, const char address_to s)
{
        for (; *s; s++)
                h = (h ^ (p8)*s) * 16777619u;
        return h;
}

static fn settings_map_wall(struct windows address_to table, unsigned int at, struct ui_graph_node address_to n, int width, int height)
{
        char kind[16], dark_word[16], maria[8], glow[24], ground[16], path[96];
        unsigned int key = 2166136261u, stamp = 0, dark = !(table->themed && table->theme == UI_THEME_LIGHT);
        unsigned int accent = table->themed ? table->accent & 0xffffffu : 0;
        int ar = (int)(accent >> 16 & 255), ag = (int)(accent >> 8 & 255), ab = (int)(accent & 255);
        int most = ar > ag ? (ar > ab ? ar : ab) : (ag > ab ? ag : ab), least = ar < ag ? (ar < ab ? ar : ab) : (ag < ab ? ag : ab);
        int w = SETTINGS_WALL_W, h = width > 0 ? SETTINGS_WALL_W * height / width : 1;
        unsigned int address_to into = settings_wall_slot(at);
        b32 own, picture;

        if (!into)
        {
                n->picture = null, n->picture_w = n->picture_h = 0;
                return;
        }
        if (h > SETTINGS_WALL_H)
                h = SETTINGS_WALL_H, w = height > 0 ? SETTINGS_WALL_H * width / height : 1;
        w = w < 1 ? 1 : w, h = h < 1 ? 1 : h;
        settings_wall_word("/root/canvas.wallpaper", kind, sizeof(kind));
        settings_wall_word("/root/canvas.wallpaper.dark", dark_word, sizeof(dark_word));
        settings_wall_word("/root/canvas.wallpaper.maria", maria, sizeof(maria));
        settings_wall_word("/root/canvas.wallpaper.glow", glow, sizeof(glow));
        settings_wall_word("/root/canvas.background", ground, sizeof(ground));
        own = settings_wall_own(table->output_name[at], path, sizeof(path));
        picture = own || (string_equals((string_address)kind, "picture") &&
                          (string_copy((string_address)path, (string_address)SETTINGS_WALL_DIR "/all.png"), settings_wall_look(path, null) == 0100000u));
        if (picture)
                settings_wall_look(path, &stamp);
        key = settings_fnv32_str(settings_fnv32_str(key, kind), picture ? path : "");
        key = (key ^ stamp) * 16777619u, key = (key ^ dark) * 16777619u, key = (key ^ (unsigned int)w << 12 ^ (unsigned int)h) * 16777619u;
        accent = most - least < 40 ? 0x5b7fa6u : accent;
        key = (key ^ accent ^ (p8)dark_word[0] << 24 ^ (p8)glow[1] << 16 ^ (p8)maria[0] << 8 ^ (p8)maria[1] ^ (p8)ground[1] << 4 ^ (p8)ground[3]) * 16777619u;
        if (key != settings.map.wall_key[at] || settings.map.wall_w[at] != w || settings.map.wall_h[at] != h)
        {
                long colour = ground[0] && !string_equals((string_address)ground, "default") ? window_colour_parse((string_address)ground) : -1;

                settings.map.wall_key[at] = key, settings.map.wall_w[at] = w, settings.map.wall_h[at] = h;
                if (!picture || !settings_wall_picture(path, into, w, h))
                {
                        if (string_equals((string_address)kind, "plain"))
                                memory_fill_u32_rect(into, (positive)w, (positive)w, (positive)h, (unsigned int)(colour < 0 ? 0x1e1e1e : colour) | 0xff000000u);
                        else
                        {
                                int rect[4] = {0, 0, 0, 0}, seas = maria[0] ? (int)string_to_bipolar((string_address)maria) : 45;

                                memory_fill_u32_rect(into, (positive)w, (positive)w, (positive)h, 0xff000000u);
                                taskbar_wall_draw(into, w, w, h, (int)dark, !string_equals((string_address)dark_word, "inverted"), seas >= 20 && seas <= 80 ? seas : 45,
                                                  string_equals((string_address)glow, "none") ? 0 : string_equals((string_address)glow, "glow-and-edges") ? 2 : 1,
                                                  accent, 0, 0, rect);
                                for (int k = 0; k < w * h; k++)
                                        into[k] |= 0xff000000u;
                        }
                }
        }
        n->picture = into, n->picture_w = settings.map.wall_w[at], n->picture_h = settings.map.wall_h[at];
}

/*
        The screens the table lists, as the map's nodes, numbered as the dropdown numbers them: how many, 0 when it lists none or too
        many. A node is a screen's rectangle on the desk (the table's, its turn taken), its number in the middle and its name and size
        under it, the primary marked. While a layout sent has not reached the table, the nodes are where it put them.
*/
static unsigned int settings_map_load(unsigned int address_to number)
{
        struct windows address_to table = settings.own.table;
        struct ui_graph address_to g = &settings.map.graph;
        unsigned int count;
        b32 caught = true;

        g->node = settings.map.node, g->nodes = 0;
        if (!table || !(count = __atomic_load_n(&table->outputs, __ATOMIC_ACQUIRE)) || count > WINDOWS_OUTPUTS)
                return 0;
        windows_screen_numbers(table, number);
        for (unsigned int at = 0; at < count; at++)
        {
                struct ui_graph_node address_to n = &settings.map.node[at];
                int x, y, width, height;

                windows_output(table, at, &x, &y, &width, &height);
                caught = caught && settings.map.sent && settings.map.sent_x[at] == x && settings.map.sent_y[at] == y;
                n->x = x, n->y = y, n->w = width, n->h = height;
                n->flags = count > 1 && !at ? UI_GRAPH_MARKED : 0;
                settings_map_number(settings.map.number[at], sizeof(settings.map.number[at]), 0, (long)number[at]);
                language_format(settings.map.size[at], sizeof(settings.map.size[at]), n->flags ? WORDS(settings_map_primary) : WORDS(settings_map_size),
                                (long)width, (long)height);
                n->label = settings.map.number[at];
                n->line[0] = table->output_model[at][0] ? table->output_model[at] : table->output_name[at];
                n->line[1] = settings.map.size[at];
                settings_map_wall(table, at, n, width, height);
        }
        if (settings.map.sent && (caught || settings.map.sent_count != count || (positive)ui_kit.now - settings.map.sent_at > SETTINGS_MAP_HOLD_NS))
                settings.map.sent = false;
        for (unsigned int at = 0; settings.map.sent && at < count; at++)
                settings.map.node[at].x = settings.map.sent_x[at], settings.map.node[at].y = settings.map.sent_y[at];
        g->nodes = (int)count;
        g->chosen = settings.map.display < (int)count ? settings.map.display : -1;
        g->keys = settings.map.keys;
        // A screen plugged in or taken out is a new desk: the next picture frames it (the camera moves for nothing else).
        if (count != settings.map.shown)
                g->homed = 0;
        settings_map_follow(count);
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
        unsigned int number[WINDOWS_OUTPUTS] = {0};
        unsigned int count = settings_map_load(number);

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

// Resolution change confirmation: revert after SETTINGS_KEEP_NS or refusal unless kept.
#define SETTINGS_KEEP_NS 15000000000ull
#define SETTINGS_SIZES_SHOWN (UI_MENU_ITEMS - 1)

static struct
{
        b32 on;                          // a size sent waits to be kept
        char name[WINDOWS_OUTPUT_NAME];  // the screen's connector
        unsigned int width, height;      // the size sent
        unsigned int old_w, old_h;       // the size its line asked before (0 by 0: none, preferred)
        positive due;                    // when it goes back
        b32 seen;                        // the table has shown the screen at the size sent
} settings_resize;

static unsigned int settings_sizes_of(int chosen)
{
        unsigned int sizes = settings.own.table->output_sizes[chosen];

        sizes = sizes > WINDOWS_OUTPUT_SIZES ? WINDOWS_OUTPUT_SIZES : sizes;
        return sizes > SETTINGS_SIZES_SHOWN ? SETTINGS_SIZES_SHOWN : sizes;
}

// Whether a size sent waits to be kept on the chosen screen: the Resolution menu then starts with the item that keeps it.
static b32 settings_keep_here(int chosen)
{
        return settings_resize.on && chosen >= 0 &&
               string_equals((string_address)settings_resize.name, (string_address)settings.own.table->output_name[chosen]);
}

static positive settings_size_label(const struct windows address_to table, int chosen, unsigned int index, char address_to into, positive room)
{
        unsigned int w = table->output_size[chosen][index][0], h = table->output_size[chosen][index][1];

        return language_format(into, room, index == table->output_size_preferred[chosen] ? WORDS(settings_size_best) : WORDS(settings_size_plain),
                               (long)w, (long)h);
}

// The seconds a size sent has left to be kept, rounded up.
static positive settings_keep_left(void)
{
        positive now = (positive)ui_kit.now;

        return settings_resize.due > now ? (settings_resize.due - now + 999999999ull) / 1000000000ull : 0;
}

static fn settings_change(int row, const char address_to value);

static fn settings_say_redraw(const char address_to line)
{
        settings_say((string_address)line);
        ui_page_redraw(&settings.page);
}

static positive settings_mode_cmd(char address_to value, positive room, const char address_to name, const char address_to verb,
                                  unsigned int w, unsigned int h, char address_to back, positive back_room)
{
        positive length = settings_map_add(value, room, 0, name);

        length = settings_map_add(value, room, length, verb);
        if (w)
        {
                positive at = settings_map_number(back, back_room, 0, (long)w);

                at = settings_map_add(back, back_room, at, "x");
                settings_map_number(back, back_room, at, (long)h);
        }
        else
                settings_map_add(back, back_room, 0, "preferred");
        return settings_map_add(value, room, length, back);
}

static fn settings_keep_back(b32 refused)
{
        char value[SETTINGS_TYPED], back[32], line[SETTINGS_STATUS];
        int row = settings_output_row(SETTINGS_OUT_SIZE);

        settings_resize.on = false;
        if (row < 0)
                return;
        settings_mode_cmd(value, sizeof(value), settings_resize.name, " mode ", settings_resize.old_w, settings_resize.old_h, back, sizeof(back));
        settings_change(row, value);
        if (refused)
                language_format(line, sizeof(line), WORDS(settings_size_refused), (long)settings_resize.width, (long)settings_resize.height,
                                settings_resize.old_w ? back : WORDS(settings_size_own));
        else
                language_format(line, sizeof(line), WORDS(settings_size_back), settings_resize.old_w ? back : WORDS(settings_size_own));
        settings_say_redraw(line);
}

static long settings_keep_tick(void)
{
        struct windows address_to table = settings.own.table;
        positive now = (positive)ui_kit.now, left;

        if (!settings_resize.on || !table)
                return -1;
        for (unsigned int at = 0; at < table->outputs && at < WINDOWS_OUTPUTS; at++)
        {
                if (!string_equals((string_address)table->output_name[at], (string_address)settings_resize.name))
                        continue;
                if (table->output_mode_w[at] == settings_resize.width && table->output_mode_h[at] == settings_resize.height)
                        settings_resize.seen = true;
                else if (table->output_asked_w[at] == settings_resize.width && table->output_asked_h[at] == settings_resize.height &&
                         (table->output_refused[at] & WINDOWS_REFUSED_SIZE))
                {
                        settings_keep_back(true);
                        return -1;
                }
        }
        if (now >= settings_resize.due)
        {
                settings_keep_back(false);
                return -1;
        }
        ui_page_redraw(&settings.page);
        left = (settings_resize.due - now) % 1000000000ull;
        return (long)(left ? left : 1000000000ull);
}

// Per-output-kind descriptor shared by output_text, row_text, output_choice, describe_output, and output_apply.
static const struct settings_out_spec
{
        unsigned int refused_bit;
        unsigned int refused_text;
        unsigned int empty_text;
        unsigned int pair_text[2];
        const char address_to verb;
        b32 plain_when_one;
} settings_out_specs[10] = {
    [SETTINGS_OUT_SCREEN]    = {WINDOWS_REFUSED_PLACE, LANGUAGE_settings_refused_place, 0, {0, 0}, null, true},
    [SETTINGS_OUT_TURN]      = {WINDOWS_REFUSED_TURN,  LANGUAGE_settings_refused_turn,  0, {0, 0}, " rotation ", false},
    [SETTINGS_OUT_REFRESH]   = {WINDOWS_REFUSED_RATE,  LANGUAGE_settings_refused_rate,  LANGUAGE_settings_no_rates, {0, 0}, " refresh ", true},
    [SETTINGS_OUT_NOTE]      = {0, 0, 0, {0, 0}, null, false},
    [SETTINGS_OUT_SCALE]     = {0, 0, 0, {0, 0}, " scale ", false},
    [SETTINGS_OUT_NOTICES]   = {0, 0, 0, {0, 0}, null, false},
    [SETTINGS_OUT_SIZE]      = {WINDOWS_REFUSED_SIZE,  LANGUAGE_settings_refused_size,  LANGUAGE_settings_no_sizes, {0, 0}, " mode ", true},
    [SETTINGS_OUT_PRIMARY]   = {0, 0, 0, {LANGUAGE_settings_primary_yes, LANGUAGE_settings_primary_no}, " primary", true},
    [SETTINGS_OUT_WALLPAPER] = {0, 0, 0, {LANGUAGE_settings_wall_same, LANGUAGE_settings_wall_own}, null, false},
};

static fn settings_keep_label(char address_to into, positive room)
{
        language_format(into, room, WORDS(settings_size_keep), (long)settings_resize.width, (long)settings_resize.height, (long)settings_keep_left());
}

// Current Display row text for the chosen screen.
static const char address_to settings_output_text(int row, int chosen)
{
        struct windows address_to table = settings.own.table;
        unsigned int number[WINDOWS_OUTPUTS] = {0}, rates;

        if (chosen < 0)
                return "";
        switch (settings_rows[row].output)
        {
        case SETTINGS_OUT_TURN:
                return settings_turn_label(table->output_turn[chosen]);
        case SETTINGS_OUT_SCALE:
        {
                unsigned int percent = table->output_asked_scale[chosen];

                settings_scale_label(percent, percent == settings_scale_suggested(table, chosen), settings_shown_rate, sizeof(settings_shown_rate));
                return settings_shown_rate;
        }
        case SETTINGS_OUT_SIZE:
        {
                unsigned int w = table->output_mode_w[chosen], h = table->output_mode_h[chosen];

                if (settings_keep_here(chosen))
                {
                        settings_keep_label(settings_shown_rate, sizeof(settings_shown_rate));
                        return settings_shown_rate;
                }
                for (unsigned int at = 0; at < settings_sizes_of(chosen); at++)
                        if (table->output_size[chosen][at][0] == w && table->output_size[chosen][at][1] == h)
                        {
                                settings_size_label(table, chosen, at, settings_shown_rate, sizeof(settings_shown_rate));
                                return settings_shown_rate;
                        }
                language_format(settings_shown_rate, sizeof(settings_shown_rate), WORDS(settings_size_plain), (long)w, (long)h);
                return settings_shown_rate;
        }
        case SETTINGS_OUT_PRIMARY:
                return language_text(settings_out_specs[SETTINGS_OUT_PRIMARY].pair_text[chosen == 0 ? 0 : 1]);
        case SETTINGS_OUT_WALLPAPER:
        {
                char path[96];

                return language_text(settings_out_specs[SETTINGS_OUT_WALLPAPER].pair_text[settings_wall_own(table->output_name[chosen], path, sizeof(path)) ? 1 : 0]);
        }
        case SETTINGS_OUT_REFRESH:
                rates = settings_rates_of(chosen);
                for (unsigned int at = 0; at < rates; at++)
                        if (table->output_hz[chosen][at] == table->output_now[chosen])
                        {
                                settings_rate_label(table->output_hz[chosen][at], at == 0, settings_shown_rate, sizeof(settings_shown_rate));
                                return settings_shown_rate;
                        }
                settings_map_number(settings_shown_rate, sizeof(settings_shown_rate), 0, (long)table->output_now[chosen]);
                return settings_shown_rate;
        default:
                windows_screen_numbers(table, number);
                settings_screen_label(number[chosen], table->output_name[chosen], table->output_model[chosen], settings_shown_screen, sizeof(settings_shown_screen));
                return settings_shown_screen;
        }
}

// A Display row's value as the row shows it: what settings_output_text says, and after it what the compositor did not do of
// the screen's displays line (the table's output_refused), in plain words: a rate the screen does not offer, a turn it could
// not do, or a place the file does not read. The row says so where the change was made, so nothing fails silently.
static const char address_to settings_row_text(int row, int chosen)
{
        struct windows address_to table = settings.own.table;
        const char address_to shown = settings_output_text(row, chosen);
        unsigned char output = settings_rows[row].output;
        const struct settings_out_spec address_to spec = output < 10 ? &settings_out_specs[output] : null;
        const char address_to fmt;

        if (chosen < 0 || !spec || !spec->refused_bit || !(table->output_refused[chosen] & spec->refused_bit))
                return shown;
        if (output == SETTINGS_OUT_SIZE && settings_keep_here(chosen))
                return shown;
        fmt = language_text(spec->refused_text);
        switch (output)
        {
        case SETTINGS_OUT_REFRESH:
                language_format(settings_noted, sizeof(settings_noted), fmt, shown, (long)table->output_asked_hz[chosen]);
                break;
        case SETTINGS_OUT_TURN:
                language_format(settings_noted, sizeof(settings_noted), fmt, shown, settings_turn_label(table->output_asked_turn[chosen]));
                break;
        case SETTINGS_OUT_SIZE:
                language_format(settings_noted, sizeof(settings_noted), fmt, shown, (long)table->output_asked_w[chosen],
                                (long)table->output_asked_h[chosen]);
                break;
        default:
                language_format(settings_noted, sizeof(settings_noted), fmt, shown);
                break;
        }
        return settings_noted;
}

// The map's graph in its panel: where the page's frame put it, the strip across its top left clear by a frame of the screens. The
// screens are where they are on the desk, left is left whichever way the window reads: the graph is not turned over.
static fn settings_map_frame(void)
{
        struct ui_graph address_to g = &settings.map.graph;

        g->x = settings.map.panel.x, g->y = settings.map.panel.y, g->w = settings.map.panel.w, g->h = settings.map.panel.h;
        g->top = ui_px(&settings.ui, SETTINGS_MAP_STRIP + UI_GAP2);
        g->rules = UI_GRAPH_SIDES | UI_GRAPH_APART | UI_GRAPH_TOUCH;
        g->grid = g->nodes ? 10 : -1; // with no screen the panel is its ground alone, which says nothing is there
        g->least = UI_GRAPH_ONE / 1024, g->most = UI_GRAPH_ONE;
}

// The caption under the map, which says what the map does now: how to move a screen and the view, that a drag is let go to place
// it, that the keys choose and move a screen while the map has them, or that there is nothing to move (no screen, or one screen,
// which has nothing to be moved against).
static const char address_to settings_map_hint(unsigned int count)
{
        return settings.map.graph.held >= 0 && settings.map.graph.moving ? WORDS(settings_map_drag)
               : settings.map.keys && count > 1 ? WORDS(settings_map_keys)
               : !count               ? WORDS(settings_map_none)
               : count == 1           ? WORDS(settings_map_one) : WORDS(settings_map_hint);
}

// The panel's height, logical pixels: two fifths of the window's, no less than the least window has room for and no more than SETTINGS_MAP_TALL.
static int settings_map_tall(void)
{
        int tall = settings.ui.scale > 0 ? settings.height * 100 / settings.ui.scale * 2 / 5 : 0;

        return tall < SETTINGS_MAP_PANEL ? SETTINGS_MAP_PANEL : tall > SETTINGS_MAP_TALL ? SETTINGS_MAP_TALL : tall;
}

/*
        The map above the rows, declared in the page's frame while it is shown: the panel (its rectangle is the map's graph, which paints
        it flush to its edges and takes a press in it, settings_map_tall high), across its top the strip with its buttons at its end over
        the grid (the one that shows every screen, and identify), and under the panel the caption (settings_map_hint), which with no screen
        says why the panel is empty.
*/
static fn settings_map_above(void)
{
        unsigned int number[WINDOWS_OUTPUTS] = {0};
        struct ui_control address_to identify = &settings.map.identify, address_to fit = &settings.map.fit;

        if (!settings_map_on())
                return;
        identify->kind = fit->kind = UI_BUTTON, identify->variant = fit->variant = UI_BUTTON_DEFAULT;
        identify->label = (string_address)WORDS(settings_identify_screens), fit->label = (string_address)WORDS(settings_map_fit);
        ui_col(.host = &settings.map.panel, .class = "map", .h = settings_map_tall())
                ui_row(.class = "foot", .h = SETTINGS_MAP_STRIP)
                {
                        ui_control(fit, .w = SETTINGS_MAP_BUTTON_W, .h = SETTINGS_MAP_BUTTON_H, .shrink = 1);
                        ui_control(identify, .w = SETTINGS_MAP_BUTTON_W, .h = SETTINGS_MAP_BUTTON_H, .shrink = 1);
                }
        ui_text(settings_map_hint(settings_map_load(number)), .class = "caption");
}

// The panel: the kit's graph of the screens (ui_graph_draw), each one's number, name and size. The buttons and the caption are the
// frame's, drawn over it.
static void settings_map_draw(struct ui address_to ui)
{
        unsigned int number[WINDOWS_OUTPUTS] = {0};

        settings_map_load(number);
        settings_map_frame();
        ui_graph_draw(ui, &settings.map.graph);
}

static fn settings_describe(int id, struct ui_row address_to row);
static fn settings_size(unsigned int width, unsigned int height);

// Minimum window size in device pixels (cached by scale/text/RTL/language/list revision) and reported to table.
static int settings_least(void)
{
        struct ui address_to ui = &settings.ui;
        unsigned int key = 2166136261u;
        const char address_to shown;
        b32 column;

        if (gallery.on || !ui->regular || !settings.list.ids || !settings.page.view || ui->scale <= 0)
                return 0;
        key = (key ^ (unsigned int)ui->scale) * 16777619u, key = (key ^ ui_text_percent) * 16777619u, key = (key ^ (unsigned int)ui_rtl()) * 16777619u;
        key = (key ^ language.code) * 16777619u, key = (key ^ settings.list.revision) * 16777619u;
        if (key == settings.least_key && settings.least_w > 0)
                return 1;
        settings.least_key = key;
        column = settings.page.column_on, shown = settings.page.title;
        settings.page.column_on = true, settings.page.title = language_of("Settings");
        settings.least_w = ui_page_least(ui, &settings.page, &settings.list, settings.page.title);
        settings.page.column_on = column, settings.page.title = shown;
        settings.least_h = ui_px(ui, UI_PAGE_TOP + SETTINGS_MAP_PANEL + 2 * UI_GAP2 + 2 * ui_types[UI_LABEL_MEDIUM].line) + ui_dev(ui, UI_ROW);
        if (settings.own.table && settings.own.row < WINDOWS)
        {
                settings.own.table->least_width[settings.own.row] = (unsigned int)((settings.least_w * 100 + ui->scale - 1) / ui->scale);
                settings.own.table->least_height[settings.own.row] = (unsigned int)((settings.least_h * 100 + ui->scale - 1) / ui->scale);
        }
        return 2;
}

static const struct ui_list_menu settings_list_menu;
static fn settings_columns_sorted(struct ui_columns address_to hit);

static fn settings_layout(void)
{
        struct ui address_to ui = &settings.ui;

        if (!settings_map())
                return;
        if (!settings.list.ids)
        {
                settings.list.ids = SETTINGS_LIST_IDS;
                settings.list.describe = settings_describe;
                settings.list.damaged = settings_changed;
                settings.list.menu = &settings_list_menu;
                settings.list.sorted = settings_columns_sorted;
                settings.list.hover = settings.list.down = -1;
                settings.list.wrap = true;
                settings.map.display = -1;
                ui_graph_start(&settings.map.graph);
                for (positive k = 0; k < sizeof(settings_accents) / sizeof(settings_accents[0]); k++)
                {
                        if (k)
                                string_append_bounded((string_address)settings.accents, (string_address) " ", sizeof(settings.accents));
                        string_append_bounded((string_address)settings.accents, (string_address)settings_accents[k].colour, sizeof(settings.accents));
                }
        }
        settings.page.column_on = !settings.page.card;
        settings.page.body = settings.page.card ? &settings.card : &settings.list.scroll;
        settings.page.column.count = settings_groups() < UI_ITEMS ? settings_groups() : UI_ITEMS;
        settings.page.view = &settings_large->view;
        settings.page.above = settings_map_above;
        settings.page.popup = &settings.popup;
        settings.page.own = &settings.own;
        settings_least();
        ui_page_fit(ui, &settings.page, &settings.list, settings.width, settings.height, &settings.card, ui_lines_height(ui, settings.card_text.count));
}

static fn settings_edit_end(void);
static int settings_drive_rows(int row, int count);
static fn settings_edit_begin(int row, const char address_to text, b32 masked);
static b32 settings_row_is(int at, const char address_to path);

/* ---- interactive preview ("try") rows ---- */

static const struct settings_try
{
        const char address_to path;
        b32 before;
        const char address_to shows[6];
} settings_tries[SETTINGS_TRIES] = {
    [SETTINGS_TRY_REPEAT] = {SETTING_REPEAT_RATE_PATH, false, {null}},
    [SETTINGS_TRY_CLICK] = {SETTING_DOUBLE_CLICK_PATH, false, {SETTING_DOUBLE_CLICK_PATH}},
    [SETTINGS_TRY_POINTER] = {SETTING_CURSOR_STYLE_PATH, true, {SETTING_CURSOR_STYLE_PATH, SETTING_CURSOR_SIZE_PATH, SETTING_CURSOR_LOOK_PATH,
                                                                SETTING_SQUEEZE_PATH, SETTING_CURSOR_CLICK_PATH}},
};

// The double-click row: when the last press on it that began a pair was, and where; and how far apart the last pair came, in ms (0 none).
static struct
{
        positive at;
        int x, y, apart;
} settings_click_try;

// The row whose file is path, or -1.
static int settings_row_of(const char address_to path)
{
        const struct settings_row address_to row = setting_by_path((string_address)path);

        return row ? (int)(row - settings_rows) : -1;
}

// A row's value as a number its verb says (100 percent, 450 ms), or fallback where it says none.
static long settings_number_of(const char address_to path, long fallback)
{
        int row = settings_row_of(path);
        long number = 0;

        return row >= 0 && settings.state[row] == SETTINGS_READ && setting_typed_number(&settings_rows[row], (string_address)settings.value[row], &number)
                       ? number
                       : fallback;
}

// Whether a row's value is word.
static b32 settings_value_is(const char address_to path, const char address_to word)
{
        int row = settings_row_of(path);

        return row >= 0 && settings.state[row] == SETTINGS_READ && string_equals((string_address)settings.value[row], (string_address)word);
}

// A list row that shows a setting: its number among them, or -1.
static int settings_try_of(int id)
{
        int at;

        return settings_domain_of(id, &at) == SETTINGS_DOM_TRY ? at : -1;
}

// The pointer as it is drawn at scale percent, in 256ths of a device pixel a cell (compositor_cursor_size's sum): the desktop's whole pixels
// a logical pixel times the size's percent, less the squeeze while a press holds it; no larger than the largest picture its row draws.
static int settings_try_cell(int scale, b32 pressed, b32 dynamic)
{
        long percent = settings_number_of(SETTING_CURSOR_SIZE_PATH, 100), squeeze = settings_number_of(SETTING_SQUEEZE_PATH, 25);
        int cell = (int)((compositor_px1(scale) * 256 * percent + 50) / 100), most = (SETTINGS_TRY_SPAN << 8) / (dynamic ? CP_DYN_CELL : CP_CELL);

        if (pressed && !settings_value_is(SETTING_CURSOR_CLICK_PATH, "off"))
                cell = (int)(cell * (100 - (squeeze < 0 ? 0 : squeeze > 50 ? 50 : squeeze)) / 100);
        return cell < 64 ? 64 : cell > most ? most : cell;
}

/*
        What the two pointers cover, in cells: the arrow from its tip 12 across and 21 down, the I-beam 9 by 18 round its middle (columns 7
        to 15 and rows 3 to 20 of its bitmap); the dynamic shape at rest a circle 16 across, and over text a bar 8 by 20. Side by side with
        a gap between, at the row's end, the pair's part is as wide as the dynamic pair, the wider, so the row does not move as the style
        changes; and the row is as tall as the taller of the two styles and a margin over and under, or as its words where they are taller.
*/
#define SETTINGS_TRY_ACROSS 24 // cells across both
#define SETTINGS_TRY_DOWN 21   // and down

// The pointers' part at the row's end and the row's least height, logical pixels.
static fn settings_try_pointer_room(int scale, int address_to wide, int address_to tall)
{
        int cell = settings_try_cell(scale, false, true);

        *wide = (SETTINGS_TRY_ACROSS * cell / 256 * 100 + scale - 1) / scale + UI_GAP4;
        *tall = (SETTINGS_TRY_DOWN * cell / 256 * 100 + scale - 1) / scale + 2 * UI_GAP3;
}

/*
        One pointer drawn with its hot pixel at x, y (device pixels): its covers made a row at a time by the compositor's code into the window's
        mapping, then its fill ink and its line ink mixed over the picture by them (ui_kit_covers), as the compositor mixes them
        (compositor_cursor_draw). The light look, which is the standard, is white with a black line, and the dark the other way
        (compositor_cursor_ink).
*/
static fn settings_try_pointer_one(struct ui address_to ui, const struct compositor_pointer address_to p, int x, int y)
{
        int hot_x, hot_y, span = compositor_pointer_span(p, &hot_x, &hot_y);
        b32 dark = settings_value_is(SETTING_CURSOR_LOOK_PATH, "dark");

        span = span < SETTINGS_TRY_SPAN ? span : SETTINGS_TRY_SPAN;
        for (int row = 0; row < span; row++)
                compositor_pointer_row(p, row, span, hot_x, hot_y, settings_large->try_cover[0] + row * span, settings_large->try_cover[1] + row * span);
        ui_kit_covers(ui, x - hot_x, y - hot_y, span, span, settings_large->try_cover[0], span, dark ? 0x000000u : 0xffffffu);
        ui_kit_covers(ui, x - hot_x, y - hot_y, span, span, settings_large->try_cover[1], span, dark ? 0xffffffu : 0x000000u);
}

// The two pointers at the row's end, in the middle of it down: the one at rest on the start side, the one over text after it.
static fn settings_try_pointer_paint(struct ui address_to ui, int id, int x, int y, int w, int h)
{
        b32 dynamic = !settings_value_is(SETTING_CURSOR_STYLE_PATH, "classic");
        int cell = settings_try_cell(ui->scale, settings.list.down == id, dynamic), gap = ui_px(ui, UI_GAP4), middle = y + h / 2;
        int first = (dynamic ? 16 : 12) * cell / 256, second = (dynamic ? 8 : 9) * cell / 256, across = first + gap + second;
        // The pair is at the part's end: its left edge reading from the left, its right edge reading from the right, where it starts.
        int start = ui_rtl() ? x + across : x + w - across;
        int rest_x = ui_rtl() ? start - first / 2 : start + first / 2, text_x = ui_rtl() ? start - first - gap - second / 2 : start + first + gap + second / 2;
        struct compositor_pointer at_rest = {.dyn = dynamic, .scale = cell, .shape = CP_ARROW};
        struct compositor_pointer on_text = {.dyn = dynamic, .scale = cell, .shape = CP_TEXT};

        if (dynamic)
        {
                at_rest.hw = at_rest.hh = at_rest.c = 256 * CP_DYN_RADIUS;
                on_text.hw = 256 * CP_DYN_TEXT_W, on_text.hh = 256 * CP_DYN_TEXT_H, on_text.c = 256 * CP_DYN_TEXT_R;
                settings_try_pointer_one(ui, &at_rest, rest_x, middle);
                settings_try_pointer_one(ui, &on_text, text_x, middle);
                return;
        }
        // The arrow's hot pixel is its tip: half its covered box up and to the start side of its middle.
        settings_try_pointer_one(ui, &at_rest, rest_x - first / 2, middle - 21 * cell / 512);
        settings_try_pointer_one(ui, &on_text, text_x, middle);
}

// The double-click row's pair that came within the double-click time: its time apart, in the selected tab's pill at the row's end.
static fn settings_try_click_paint(struct ui address_to ui, int id, int x, int y, int w, int h)
{
        int high = ui_px(ui, 32), top = y + (h - high) / 2, wide;
        char said[32];
        struct ui_box pill = {x * 256, top * 256, (x + w) * 256, (top + high) * 256, high * 128, high * 128};

        (void)id;
        language_format(said, sizeof(said), WORDS(settings_try_ms), settings_click_try.apart);
        wide = ui_kit_measure(ui, (string_address)said, UI_LABEL_LARGE);
        ui_kit_fill(ui, &pill, ui_role(UI_PRIMARY), null);
        ui_kit_text(ui, x + (w - wide) / 2, ui_kit_baseline(ui, top + (high - ui_px(ui, ui_types[UI_LABEL_LARGE].line)) / 2, UI_LABEL_LARGE),
                    (string_address)said, UI_LABEL_LARGE, ui_text_colour(ui, UI_ON_PRIMARY), 0);
}

// Whether the double-click row's last pair came within the double-click time.
static b32 settings_try_click_within(void)
{
        return settings_click_try.apart > 0 && settings_click_try.apart < settings_number_of(SETTING_DOUBLE_CLICK_PATH, 450);
}

// What a row that shows a setting shows, for the kit's list: in the part of the row it is beside, under its header.
static fn settings_try_describe(int id, struct ui_row address_to row)
{
        static char apart[32];
        int which = settings_try_of(id), beside = which >= 0 ? settings_row_of(settings_tries[which].path) : -1;
        int scale = settings.ui.scale > 0 ? settings.ui.scale : 100;

        row->section = beside >= 0 ? settings_section_start(beside) : 0;
        row->header = beside >= 0 ? language_of(settings_header(beside)) : null;
        if (which == SETTINGS_TRY_REPEAT)
        {
                row->label = WORDS(settings_try_repeat), row->about = WORDS(settings_try_repeat_about);
                if (settings_editing() == id)
                        row->kind = UI_ROW_FIELD, row->edit = &settings.edit, row->placeholder = WORDS(settings_try_repeat_hint);
                else
                        row->value = WORDS(settings_try_repeat_value);
        }
        else if (which == SETTINGS_TRY_CLICK)
        {
                // Before a pair, what to do; a pair too far apart says how far, as a value; one within the time says it in the pill.
                row->label = WORDS(settings_try_click), row->about = WORDS(settings_try_click_about);
                language_format(apart, sizeof(apart), WORDS(settings_try_ms), settings_click_try.apart);
                if (settings_try_click_within() && settings.ui.regular)
                        row->picture_w = (ui_kit_measure(&settings.ui, (string_address)apart, UI_LABEL_LARGE) * 100 + scale - 1) / scale + 2 * UI_GAP4,
                        row->paint = settings_try_click_paint;
                else
                        row->value = settings_click_try.apart > 0 ? apart : WORDS(settings_try_click_value);
        }
        else if (which == SETTINGS_TRY_POINTER)
        {
                row->label = WORDS(settings_try_pointer), row->paint = settings_try_pointer_paint;
                settings_try_pointer_room(scale, &row->picture_w, &row->picture);
        }
}

// A press on a row that shows a setting: the key repeat's field opens; the double-click row takes it as a click (the press's place in
// device pixels), the second of a pair when it comes within two seconds of the first and near it.
static fn settings_try_press(int id, int x, int y)
{
        int which = settings_try_of(id);
        positive now = ui_press_now();
        long dx = (long)x - settings_click_try.x, dy = (long)y - settings_click_try.y, reach = UI_CLICK_PX * (long)settings.ui.scale / 100;

        if (which == SETTINGS_TRY_REPEAT)
                settings_edit_begin(id, "", false);
        if (which != SETTINGS_TRY_CLICK)
                return;
        if (settings_click_try.at && now - settings_click_try.at < 2000000000ull && dx * dx + dy * dy <= reach * reach)
        {
                settings_click_try.apart = (int)((now - settings_click_try.at) / 1000000ull);
                settings_click_try.apart += !settings_click_try.apart;
                settings_click_try.at = 0;
        }
        else
                settings_click_try.at = now, settings_click_try.x = x, settings_click_try.y = y;
        ui_list_row_changed(&settings.ui, &settings.list, id);
}

// A row's reading changed: the rows that show it are drawn again, and laid out again where they are another height.
static fn settings_try_changed(int row)
{
        for (int which = 0; which < SETTINGS_TRIES; which++)
                for (int k = 0; k < 6 && settings_tries[which].shows[k]; k++)
                        if (settings_row_is(row, settings_tries[which].shows[k]))
                                ui_list_row_changed(&settings.ui, &settings.list, SETTINGS_TRY + which);
}

/* ---- the About section (Moonwater logo, author and links) ---- */

static const struct settings_about_spec
{
        const char address_to url, address_to label, address_to target;
        unsigned int label_id, sub_id;
} settings_about_specs[SETTINGS_ABOUT_COUNT] = {
    [SETTINGS_ABOUT_BRAND]   = {"https://dawning.dev", null, "dawning.dev", LANGUAGE_settings_about_made_by, 0},
    [SETTINGS_ABOUT_DISCORD] = {"https://discord.gg/nwvFVGYJhR", "Discord", "discord.gg/nwvFVGYJhR", 0, LANGUAGE_settings_about_discord},
    [SETTINGS_ABOUT_GITHUB]  = {"https://github.com/dawnlarsson/moonwater/issues", "GitHub", "github.com/dawnlarsson/moonwater/issues", 0, LANGUAGE_settings_about_github},
};

static int settings_about_of(int id)
{
        int at;

        return settings_domain_of(id, &at) == SETTINGS_DOM_ABOUT ? at : -1;
}

static const char address_to settings_about_url(int which)
{
        return which >= 0 && which < SETTINGS_ABOUT_COUNT ? settings_about_specs[which].url : "";
}

static const char address_to settings_about_label(int which)
{
        if (which < 0 || which >= SETTINGS_ABOUT_COUNT)
                return "";
        return settings_about_specs[which].label ? settings_about_specs[which].label : language_text(settings_about_specs[which].label_id);
}

static const char address_to settings_about_sub(int which, char address_to into, positive room)
{
        if (which < 0 || which >= SETTINGS_ABOUT_COUNT)
                into[0] = 0;
        else if (settings_about_specs[which].sub_id)
                language_format(into, room, language_text(settings_about_specs[which].sub_id), settings_about_specs[which].target);
        else
                string_copy_bounded((string_address)into, (string_address)settings_about_specs[which].target, room);
        return into;
}

static b32 settings_about_matches_word(int which, const char address_to word, positive size)
{
        char sub[SETTINGS_SUB_TEXT];

        settings_about_sub(which, sub, sizeof(sub));
        return settings_has(WORDS(settings_about_header), word, size) || settings_has(settings_about_label(which), word, size) ||
               settings_has(sub, word, size) || settings_has(settings_about_url(which), word, size);
}

// One pixel of the cropped Moonwater wordmark from splash_mark.c, as 0..255 cover.
static inline unsigned int settings_logo_sample(int sx, int sy)
{
        int x = SETTINGS_LOGO_X0 + (sx < 0 ? 0 : sx >= SETTINGS_LOGO_SRC_W ? SETTINGS_LOGO_SRC_W - 1 : sx);
        int y = SETTINGS_LOGO_Y0 + (sy < 0 ? 0 : sy >= SETTINGS_LOGO_SRC_H ? SETTINGS_LOGO_SRC_H - 1 : sy);
        positive at = (positive)y * SPLASH_MARK_W + (positive)x;

        return ((unsigned int)(splash_mark[at >> 2] >> (6 - 2 * (at & 3))) & 3u) * 85u;
}

// The Moonwater wordmark drawn at the trailing end of the brand row, bilinearly scaled from splash_mark and tinted in the theme's ink.
static fn settings_about_logo_paint(struct ui address_to ui, int id, int x, int y, int w, int h)
{
        int max_h = h - 4 < SETTINGS_LOGO_MAX_H ? h - 4 : SETTINGS_LOGO_MAX_H;
        int draw_w = w < SETTINGS_LOGO_MAX_W ? w : SETTINGS_LOGO_MAX_W;
        int draw_h, dx, dy;

        (void)id;
        if (!settings_large || draw_w <= 0 || max_h <= 0)
                return;
        draw_h = draw_w * SETTINGS_LOGO_SRC_H / SETTINGS_LOGO_SRC_W;
        if (draw_h > max_h)
        {
                draw_h = max_h;
                draw_w = draw_h * SETTINGS_LOGO_SRC_W / SETTINGS_LOGO_SRC_H;
        }
        if (draw_w <= 0 || draw_h <= 0)
                return;
        for (int py = 0; py < draw_h; py++)
        {
                int fy = ((2 * py + 1) * (SETTINGS_LOGO_SRC_H << 8)) / (2 * draw_h) - 128;
                int y0, y1, wy;

                fy = fy < 0 ? 0 : fy > ((SETTINGS_LOGO_SRC_H - 1) << 8) ? ((SETTINGS_LOGO_SRC_H - 1) << 8) : fy;
                y0 = fy >> 8, y1 = y0 + 1 < SETTINGS_LOGO_SRC_H ? y0 + 1 : y0, wy = fy & 255;
                for (int px = 0; px < draw_w; px++)
                {
                        int fx = ((2 * px + 1) * (SETTINGS_LOGO_SRC_W << 8)) / (2 * draw_w) - 128;
                        int x0, x1, wx;
                        unsigned int c00, c10, c01, c11, top, bot;

                        fx = fx < 0 ? 0 : fx > ((SETTINGS_LOGO_SRC_W - 1) << 8) ? ((SETTINGS_LOGO_SRC_W - 1) << 8) : fx;
                        x0 = fx >> 8, x1 = x0 + 1 < SETTINGS_LOGO_SRC_W ? x0 + 1 : x0, wx = fx & 255;
                        c00 = settings_logo_sample(x0, y0), c10 = settings_logo_sample(x1, y0);
                        c01 = settings_logo_sample(x0, y1), c11 = settings_logo_sample(x1, y1);
                        top = c00 * (unsigned int)(256 - wx) + c10 * (unsigned int)wx;
                        bot = c01 * (unsigned int)(256 - wx) + c11 * (unsigned int)wx;
                        settings_large->logo_cover[(positive)py * (positive)draw_w + (positive)px] =
                            (unsigned char)((top * (unsigned int)(256 - wy) + bot * (unsigned int)wy + 32768u) >> 16);
                }
        }
        dx = ui_rtl() ? x : x + w - draw_w;
        dy = y + (h - draw_h) / 2;
        UI_KEEP(dx, dy, draw_w, draw_h);
        ui_kit_covers(ui, dx, dy, draw_w, draw_h, settings_large->logo_cover, draw_w, ui_text_colour(ui, UI_ON_SURFACE));
}

static fn settings_about_describe(int id, struct ui_row address_to row)
{
        static char about_text[SETTINGS_ABOUT_COUNT][SETTINGS_SUB_TEXT];
        int which = settings_about_of(id);

        if (which < 0)
                return;
        row->section = SETTINGS_ABOUT;
        row->header = WORDS(settings_about_header);
        row->label = settings_about_label(which);
        row->about = settings_about_sub(which, about_text[which], sizeof(about_text[which]));
        if (which == SETTINGS_ABOUT_BRAND)
        {
                row->picture_w = SETTINGS_LOGO_W;
                row->picture = SETTINGS_LOGO_ROW_H;
                row->paint = settings_about_logo_paint;
        }
        else
        {
                row->kind = UI_ROW_BUTTON;
                row->words = WORDS(settings_button_open);
        }
}

// Whether an installed bowl app entry is a web browser that can open an https URL.
static b32 settings_browser_match(const char address_to text)
{
        static const char address_to const names[] = {
            "chromium", "chrome", "firefox", "librewolf", "brave", "vivaldi", "zen", "epiphany", "falkon", "qutebrowser", "floorp", "waterfox"};

        if (!text || !text[0])
                return false;
        for (positive k = 0; k < sizeof(names) / sizeof(names[0]); k++)
                if (settings_has(text, names[k], string_length((string_address)names[k])))
                        return true;
        return false;
}

// The index of an installed bowl browser in ui_apps (preferring the chrome bowl profile), or -1 when none is installed.
static int settings_browser_find(void)
{
        int fallback = -1;

        ui_apps_fresh((b64)ui_kit_time());
        for (int at = 0; at < ui_apps.count; at++)
        {
                const struct ui_app_entry address_to app = &ui_apps.app[at];

                if (app->kit || app->terminal || !app->launch[0])
                        continue;
                if (string_equals((string_address)app->profile, (string_address) "chrome"))
                        return at;
                if (fallback < 0 &&
                    (settings_browser_match(app->id) || settings_browser_match(app->key) || settings_browser_match(ui_apps_base(app->launch))))
                        fallback = at;
        }
        return fallback;
}

// Opens url in an installed bowl browser if one is found; otherwise copies url to the clipboard.
static b32 settings_open_url(const char address_to url)
{
        int browser = settings_browser_find();

        if (browser >= 0)
        {
                string_address words[UI_APP_START_WORDS + 2];
                positive count = ui_apps_words_of(browser, words, null, null, null);

                if (count > 0 && count + 1 < sizeof(words) / sizeof(words[0]))
                {
                        words[count++] = (string_address)url;
                        words[count] = null;
                        if (ui_start_apart(words, count))
                        {
                                settings_say((string_address)WORDS(settings_opened));
                                return true;
                        }
                }
        }
        windows_clip_offer(&settings.own, WINDOWS_CLIP_CLIPBOARD, url, string_length((string_address)url));
        settings_say((string_address)WORDS(settings_copied));
        return false;
}

static fn settings_about_press(int id)
{
        int which = settings_about_of(id);

        if (which < 0)
                return;
        settings_open_url(settings_about_url(which));
}

/*
        The layout the press was taken on: each place of the list (a row's id) and every feed list's labels, which name the rows (a row
        that comes in, goes out or moves is another label in another place). A press holds a place of the list, so when the layout changes
        under it, settings_layout_check lets it go: its release clicks nothing.
*/
#define SETTINGS_FNV64_SEED 1469598103934665603ull
#define SETTINGS_FNV64_STEP(h, v) (((h) ^ (positive)(v)) * 1099511628211ull)

static positive settings_fnv64_bytes(positive hash, const void address_to data, positive size)
{
        const unsigned char address_to bytes = (const unsigned char address_to)data;

        for (positive at = 0; at < size; at++)
                hash = SETTINGS_FNV64_STEP(hash, bytes[at]);
        return hash;
}

static positive settings_layout_seen;

static positive settings_layout_key(void)
{
        positive key = SETTINGS_FNV64_SEED;

        for (int at = 0; at < settings.list.count; at++)
                key = SETTINGS_FNV64_STEP(key, settings.list.id[at]);
        for (int list = 0; list < SETTINGS_LISTS; list++)
        {
                key = SETTINGS_FNV64_STEP(key, settings.item_count[list]);
                for (int at = 0; at < settings.item_count[list]; at++)
                        key = settings_fnv64_bytes(key, settings_large->item[list][at].label,
                                                   string_length((string_address)settings_large->item[list][at].label));
        }
        return key;
}

static fn settings_layout_check(void)
{
        positive key = settings_layout_key();

        if (key != settings_layout_seen && settings.page.press.down)
                ui_page_let_go(&settings.ui, &settings.page, &settings.list);
        settings_layout_seen = key;
}

static int settings_try_emit(int row, b32 before, int count)
{
        for (int which = 0; which < SETTINGS_TRIES && !settings_filter_length(); which++)
                if (settings_tries[which].before == before && settings_row_is(row, settings_tries[which].path) && count < UI_LIST_MOST)
                        settings.list.id[count++] = (unsigned short)(SETTINGS_TRY + which);
        return count;
}

// Rebuild visible list ids for current group or active filter, preserving scroll and focus when possible.
static fn settings_rebuild(void)
{
        int count = 0;
        b32 filtered = settings_filter_length() != 0;

        string_copy_bounded((string_address)settings_metric_shown, (string_address)settings_metric_line, sizeof(settings_metric_shown));
        settings_sub_count = 0, settings_sub_cut = 0;
        for (positive row = 0; row < settings_rows_count(); row++)
                if (count < UI_LIST_MOST && (filtered ? settings_matches((int)row) : settings_group_of((int)row) == settings.page.chosen))
                {
                        count = settings_try_emit((int)row, true, count);
                        if (count < UI_LIST_MOST)
                                settings.list.id[count++] = (unsigned short)row;
                        count = settings_sub_rows((int)row, count);
                        count = settings_try_emit((int)row, false, count);
                }
        for (int list = 0; list < SETTINGS_LISTS; list++)
        {
                int group = settings_group_of((int)SETTINGS_LIST + list * SETTINGS_LIST_ROWS);

                for (int at = 0; at < settings.item_count[list]; at++)
                {
                        int row = (int)SETTINGS_LIST + list * SETTINGS_LIST_ROWS + at;

                        if (count < UI_LIST_MOST && (filtered ? settings_matches(row) : group == settings.page.chosen))
                                settings.list.id[count++] = (unsigned short)row, count = settings_drive_rows(row, count);
                }
        }
        for (int which = 0; which < SETTINGS_ABOUT_COUNT && count < UI_LIST_MOST; which++)
        {
                int id = SETTINGS_ABOUT + which;

                if (filtered ? settings_matches(id) : settings.page.chosen == settings_system_group())
                        settings.list.id[count++] = (unsigned short)id;
        }
        settings.list.count = count;
        if (settings_editing() >= 0 && settings_pos(settings_editing()) < 0)
                settings_edit_end();
        if (settings.page.menu_row >= 0 && settings_pos(settings.page.menu_row) < 0)
                settings.page.menu_row = -1, ui_popmenu_close(&settings.popup);
        if (settings_pos(settings.list.focus) < 0 && count)
                settings.list.focus = settings.list.id[0];
        settings_layout();
        settings_layout_check();
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

// Parse `moonwater help` output into All commands rows.
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
                        continue;
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
                if (row->flags & SETTINGS_INK)
                {
                        const struct settings_ink address_to ink = settings_ink_find(row, value, false);

                        if (ink)
                                words[count - 1] = (string_address)ink->colour;
                }
        }
        words[count] = null;
        return count;
}

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

static b32 settings_row_asks(int row)
{
        const struct settings_generic address_to generic = settings_generic_of(row);
        char head[SETTINGS_VALUE];
        string_address words[SETTINGS_WORDS];
        positive count;

        if (!generic)
                return false;
        memory_copy(head, settings_large->pool + generic->command, generic->verb);
        head[generic->verb] = 0;
        count = settings_tokens(head, settings_large->split, sizeof(settings_large->split), words, 0, SETTINGS_WORDS);
        return count && settings_asks_for(words, count);
}

/* ---- what a command said, as a card ---- */

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
        if (settings.runner.cut_lines && made + SETTINGS_ITEM_LABEL < sizeof(settings_large->card))
        {
                char note[SETTINGS_ITEM_LABEL];
                positive size;

                language_format(note, sizeof(note), WORDS(settings_not_shown), (long)settings.runner.cut_lines);
                size = string_length((string_address)note);
                if (made)
                        settings_large->card[made++] = '\n';
                memory_copy(settings_large->card + made, note, size);
                made += size;
        }
        ui_lines_set(&settings.card_text, settings_large->card, made, settings_large->card_at, settings_large->card_size, SETTINGS_CARD_LINES);
}

static fn settings_card_open(const char address_to title)
{
        settings_card_fill();
        string_copy_bounded((string_address)settings.card_title, (string_address)title, sizeof(settings.card_title));
        settings.page.card = true, settings.card.scroll = 0;
        ui_lines_wrap(&settings.ui, &settings.card_text, settings.width);
        settings_layout();
        ui_page_redraw(&settings.page);
}

static fn settings_card_leave(void)
{
        settings.page.card = false, settings.status[0] = 0;
        settings_layout();
        ui_page_redraw(&settings.page);
}

/* ---- the verbs ---- */

static fn settings_say(string_address text) { string_copy_bounded((string_address)settings.status, text, sizeof(settings.status)); }
static fn settings_complete(b32 killed, positive status);
static fn settings_waiting_say(void);
static b32 settings_waits(int row);
static fn settings_job_done(struct ui_job address_to j, b32 killed, positive status) { (void)j; settings_complete(killed, status); }
static fn settings_job_data(struct ui_job address_to j) { (void)j; settings_waiting_say(); }
static fn settings_runner_ready(void) { settings.runner.done = settings_job_done; settings.runner.data = settings_job_data; }

static positive settings_most(void)
{
        return settings.job == SETTINGS_JOB_FEED  ? (positive)SETTINGS_FEED_WAIT_NS
               : settings.job == SETTINGS_JOB_ACT ? (positive)SETTINGS_ACT_WAIT_NS
               : settings_waits(settings.job)     ? (positive)SETTINGS_WAIT_PAIR_NS
                                                  : (positive)SETTINGS_WAIT_NS;
}

static b32 settings_start(string_address address_to words, positive count, const char address_to input)
{
        positive limit = settings.job_sets && settings.job >= SETTINGS_ANY ? 0 : settings_most();

        settings_runner_ready();
        return ui_job_start(&settings.runner, words, count, input, settings_large->said, sizeof(settings_large->said), limit);
}

static fn settings_join(string_address address_to words, positive count, char address_to text, positive room)
{
        string_copy_bounded((string_address)text, (string_address) "moonwater", room);
        for (positive at = 1; at < count; at++)
                string_append_bounded((string_address)text, (string_address) " ", room),
                string_append_bounded((string_address)text, words[at], room);
}

/*
        The words of what is done to a row of a list, from the change queued
        (a letter, a newline, a name, and for a join a newline and the
        passphrase): into words, which point into job_value, cut at its
        newlines. Answers the passphrase, which is to be given to the verb's
        input, or none; words[0] is none when the change is no known one.
*/
static const struct settings_act_spec
{
        char code;
        b32 takes_pass;
        const char address_to radio;
        const char address_to how;
} settings_act_specs[] = {
    {'j', true,  "wifi",      "add"},
    {'f', false, "wifi",      "remove"},
    {'a', false, "bluetooth", "add"},
    {'d', false, "bluetooth", "remove"},
    {'v', false, "device",    "allow"},
    {'x', false, "device",    "deny"},
    {'r', false, "link",      "remove"},
};

static const char address_to settings_act_words(string_address address_to words)
{
        char address_to what = settings.job_value, address_to name, address_to pass = null;
        const struct settings_act_spec address_to spec = null;

        words[0] = null;
        if (!what[0] || what[1] != '\n')
                return null;
        for (positive at = 0; at < sizeof(settings_act_specs) / sizeof(settings_act_specs[0]); at++)
                if (settings_act_specs[at].code == what[0])
                {
                        spec = &settings_act_specs[at];
                        break;
                }
        if (!spec)
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
        pass = spec->takes_pass && pass && pass[0] ? pass : null;
        words[0] = settings.moonwater;
        words[1] = (string_address)spec->radio;
        words[2] = (string_address)spec->how;
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

// The rows a verb reads that are unread or not understood. A row the page reads from the compositor's table (its output is not
// SETTINGS_OUT_NONE: the Display map, the notices) is never asked by a verb, so its state is never set, and it is not counted.
static positive settings_not_understood(void)
{
        positive failed = 0;

        for (positive at = 0; at < SETTINGS_ROWS; at++)
                if (settings_rows[at].output == SETTINGS_OUT_NONE)
                        failed += settings.state[at] == SETTINGS_FAILED || settings.state[at] == SETTINGS_UNREAD;
        return failed;
}

// Next verb to run: queued changes first, then unread rows, then help.
static fn settings_next_job(void)
{
        string_address words[SETTINGS_WORDS];
        positive count;

        while (settings.child <= 0)
        {
                if (settings.queued)
                {
                        settings.job = settings.queue[0].row, settings.job_sets = true;
                        memory_copy(settings.job_value, settings.queue[0].value, sizeof(settings.job_value));
                        settings.queued--;
                        memory_copy(settings.queue, settings.queue + 1, settings.queued * sizeof(settings.queue[0]));
                        memory_zero(&settings.queue[settings.queued], sizeof(settings.queue[0]));
                }
                else if (settings_unread_any())
                {
                        positive first = 0;

                        while (!settings_unread_is(first))
                                first++;
                        settings.job = (int)first, settings.job_sets = false;
                        settings_unread_set(first, 0);
                }
                else if (settings.help == 1)
                        settings.job = SETTINGS_JOB_HELP, settings.job_sets = false, settings.help = 2;
                else
                        return;

                if (settings.job == SETTINGS_JOB_HELP)
                        words[0] = settings.moonwater, words[1] = (string_address) "help", words[2] = null, count = 2;
                else if (settings.job == SETTINGS_JOB_IDENTIFY)
                        words[0] = settings.moonwater, words[1] = (string_address) "canvas", words[2] = (string_address) "identify", words[3] = null, count = 3;
                else if (settings.job == SETTINGS_JOB_ACT)
                {
                        const char address_to pass = settings_act_words(words);
                        b32 started = words[0] && settings_start(words, 4 + (positive)(pass != null), pass);

                        memory_zero(settings.job_value, sizeof(settings.job_value));
                        if (started)
                                return;
                        count = 0;
                }
                else if (settings.job >= SETTINGS_ANY)
                        count = settings_generic_words(settings.job, settings.job_value, words, SETTINGS_WORDS);
                else
                        count = settings_command(&settings_rows[settings.job], settings.moonwater, settings.job_value, words, SETTINGS_WORDS, !settings.job_sets);
                if (count && settings_start(words, count, null))
                {
                        if (settings.job >= SETTINGS_ANY)
                        {
                                char text[SETTINGS_STATUS - 24], line[SETTINGS_STATUS];

                                settings_join(words, count, text, sizeof(text));
                                language_format(line, sizeof(line), WORDS(settings_running), text);
                                settings_say((string_address)line);
                        }
                        return;
                }
                if (settings.job_sets)
                        settings_say((string_address)(count || settings.job < SETTINGS_ANY ? WORDS(settings_cannot_run) : WORDS(settings_too_many)));
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
                {
                        // A row with no verb to read (a pairing's field and button) has nothing to ask: it is ready at once. Asking
                        // it would run the bare moonwater, and until that read ended a press on it did nothing.
                        if (settings_rows[row].read[0])
                                settings_unread_set(row, 1);
                        else
                                settings.state[row] = SETTINGS_READ;
                }
}

// Where a change to row waits in the queue, or queued when it has none.
static positive settings_slot(int row)
{
        positive at = 0;

        while (at < settings.queued && settings.queue[at].row != row)
                at++;
        return at;
}

// Whether a row is an action: a verb run by its button, with no value to set.
static b32 settings_action_row(int row)
{
        return row >= 0 && row < SETTINGS_ANY && settings_rows[row].kind == SETTINGS_ACTION;
}

/*
        Whether an action's verb waits to run or runs now: its button says so and is greyed, and a press, Enter or a tap asks for it
        no second time (a pairing that waits is no such action: its button stops the wait).
*/
static b32 settings_action_busy(int row)
{
        return settings_slot(row) < settings.queued || (settings.child > 0 && settings.job == row && settings.job_sets);
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

/*
        A deliberate change (registry.c's deliberate: the SSH server on, root let in, a key added) made by a key or a press that a
        virtual device sent (uinput, uhid, the on-screen keyboard, the Wayland shim, the headless pipe) is refused, and the status
        says how it is made. It is the row's own, or a command of All commands that is the same verb: its words then the row's
        "on", or for a field its words and anything after them. Turning off, and every other row, needs no hand.
*/
static b32 settings_refused(int row, const char address_to value)
{
        b32 deliberate = false;

        if (settings.hand)
                return false;
        if (row >= 0 && row < SETTINGS_ANY)
                deliberate = settings_rows[row].deliberate &&
                             (settings_rows[row].kind != SETTINGS_SWITCH || string_equals((string_address)value, (string_address) "on"));
        else if (row >= SETTINGS_ANY && row < (int)SETTINGS_LIST)
        {
                string_address words[SETTINGS_WORDS];
                positive count = settings_generic_words(row, value, words, SETTINGS_WORDS);

                for (positive at = 0; at < SETTINGS_ROWS && !deliberate; at++)
                {
                        const struct settings_row address_to which = &settings_rows[at];
                        positive n = 0;
                        b32 same = which->deliberate;

                        while (same && n < SETTING_WORDS && which->set[n])
                        {
                                same = 1 + n < count && string_equals(words[1 + n], (string_address)which->set[n]);
                                n++;
                        }
                        deliberate = same && n && (which->kind != SETTINGS_SWITCH ||
                                                   (count == 2 + n && string_equals(words[1 + n], (string_address) "on")));
                }
        }
        if (deliberate)
                settings_say((string_address)WORDS(settings_virtual_refused));
        return deliberate;
}

// A row changed to value, now or once the verb running has finished (see settings_waiting for where it waits).
static fn settings_change(int row, const char address_to value)
{
        positive at;

        if (settings_refused(row, value))
                return;
        // A feed's reading is one that can be had again: a change waits for no feed.
        settings_feed_drop();
        at = settings_waiting(row, value);

        if (at == settings.queued)
        {
                // A row that waits takes its change in its own entry (above), so the queue holds one entry per row and per command: it grows with
                // them, and only the kernel refuses a change it cannot hold.
                if (!array_store_reserve(settings.queue, settings.queue_room, settings.queued, settings.queued + 1, 8))
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
        // An action's button says it is busy from now (settings_describe).
        if (settings_action_row(row))
                ui_page_redraw(&settings.page);
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

// Copy bounded slice into buffer, NUL-terminated.
static fn settings_item_word(char address_to into, positive room, const char address_to text, positive size)
{
        positive took = size >= room ? room - 1 : size;

        memory_copy(into, text, took);
        into[took] = 0;
}

static fn settings_item_label(struct settings_item address_to item, const char address_to name, positive size)
{
        settings_item_word(item->label, sizeof(item->label), name, size);
}

static fn settings_note_add(struct settings_item address_to items, positive address_to count, unsigned char where, const char address_to text, positive size)
{
        if (*count >= SETTINGS_LIST_ROWS)
                return;
        items[*count].kind = SETTINGS_NOTE;
        items[*count].where = where;
        settings_item_label(&items[(*count)++], text, size);
}

// Whether moonwater can join this Wi-Fi security type.
static b32 settings_joinable(const char address_to security)
{
        static const char address_to const ok[] = {"open", "WPA2", "WPA2/3", "WPA3", "PMF"};

        for (positive k = 0; k < sizeof(ok) / sizeof(ok[0]); k++)
                if (string_equals((string_address)security, (string_address)ok[k]))
                        return true;
        return false;
}

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
        address_to text = line + skip, address_to size = length - skip;
        return true;
}

// Parse `moonwater wifi` output via ui_wifi_parse into settings_large->item[0].
static fn settings_feed_wifi(void)
{
        struct settings_item address_to items = settings_large->item[0];
        struct ui_wifi_net nets[SETTINGS_LIST_ROWS];
        char note[SETTINGS_ITEM_LABEL] = {0};
        int n = ui_wifi_parse(settings_large->said, settings.said_length, nets, SETTINGS_LIST_ROWS - 1, null, note, sizeof(note));
        positive count = 0;

        memory_zero(items, sizeof(settings_large->item[0]));
        for (int i = 0; i < n; i++)
        {
                struct settings_item address_to item = &items[count++];

                item->kind = SETTINGS_NETWORK;
                string_copy_bounded((string_address)item->label, (string_address)nets[i].name, sizeof(item->label));
                string_copy_bounded((string_address)item->security, (string_address)nets[i].security, sizeof(item->security));
                item->dbm = nets[i].dbm, item->bars = nets[i].bars;
                item->flags = (nets[i].flags & UI_WIFI_HEARD ? SETTINGS_ITEM_HEARD : 0) |
                              (nets[i].flags & UI_WIFI_JOINED ? SETTINGS_ITEM_JOINED : 0) |
                              (nets[i].flags & UI_WIFI_SAVED ? SETTINGS_ITEM_SAVED : 0) |
                              (nets[i].flags & UI_WIFI_OPEN ? SETTINGS_ITEM_OPEN : 0) |
                              ((nets[i].flags & UI_WIFI_HEARD) && !settings_joinable(item->security) ? SETTINGS_ITEM_CANNOT : 0);
        }
        if (note[0])
                settings_note_add(items, &count, 0, note, string_length((string_address)note));
        settings.item_count[0] = (unsigned short)count;
}

// Tiny slice cursor for feed line parsing (hardware, bluetooth devices, waterlink peers).
struct settings_cursor
{
        const char address_to s;
        positive n;
};

static inline b32 sc_eat(struct settings_cursor address_to c, const char address_to prefix)
{
        positive len = string_length((string_address)prefix);

        if (c->n < len || memory_compare(c->s, prefix, len))
                return false;
        c->s += len, c->n -= len;
        return true;
}

static inline fn sc_spaces(struct settings_cursor address_to c)
{
        while (c->n && c->s[0] == ' ')
                c->s++, c->n--;
}

static inline unsigned long long sc_u64(struct settings_cursor address_to c, unsigned long long cap)
{
        unsigned long long v = 0;

        for (; c->n && c->s[0] >= '0' && c->s[0] <= '9'; c->s++, c->n--)
                if (v <= cap)
                        v = v * 10 + (unsigned long long)(c->s[0] - '0');
        return v;
}

static inline const char address_to sc_word(struct settings_cursor address_to c, positive address_to len)
{
        const char address_to start = c->s;
        positive n = 0;

        while (n < c->n && c->s[n] != ' ')
                n++;
        c->s += n, c->n -= n;
        if (len)
                *len = n;
        return start;
}

static inline fn sc_trim_at(struct settings_cursor address_to c, const char address_to needle)
{
        positive at = settings_find_last(c->s, c->n, needle);

        if (at < c->n)
                c->n = at;
        while (c->n && c->s[c->n - 1] == ' ')
                c->n--;
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
        {
                struct settings_cursor c = {text, size};

                if (sc_eat(&c, "  ") && c.n && c.s[0] != ' ' && count + 1 < SETTINGS_LIST_ROWS)
                {
                        items[count].kind = SETTINGS_DEVICE;
                        items[count].flags = SETTINGS_ITEM_SAVED;
                        settings_item_label(&items[count++], c.s, c.n);
                }
        }
        items[count].kind = SETTINGS_ADD;
        {
                const char address_to keep = language_of("Keep a device by its name");

                settings_item_label(&items[count++], keep, string_length((string_address)keep));
        }
        settings.item_count[1] = (unsigned short)count;
}

/*
        The devices the system sees, as device list says them: two blanks, then the name a press
        allows or blocks (a port, or a drive's name), the kind, the size and the state, which ends
        the line: allowed, blocked or waiting. A line that does not say allowed is not allowed. A drive's line
        has drive as its second word and is a note: no verb holds a drive, so a press on it does nothing.
*/
// The role a segment's word is (efi, linux, swap, other, free, unpartitioned), or -1 for a word that is none.
static int settings_role_code(const char address_to text, positive length)
{
        static const char address_to const words[] = {"efi", "linux", "swap", "other", "free", "unpartitioned"};

        for (int at = 0; at < 6; at++)
                if (length == string_length((string_address)words[at]) && !memory_compare(text, words[at], length))
                        return at;
        return -1;
}

// A role in words, in the language the window speaks.
static const char address_to settings_role_word(unsigned int role)
{
        static const unsigned int ids[] = {
            LANGUAGE_settings_role_efi, LANGUAGE_settings_role_linux, LANGUAGE_settings_role_swap,
            LANGUAGE_settings_role_other, LANGUAGE_settings_role_free, LANGUAGE_settings_role_unpartitioned,
        };

        return language_text(ids[role < 5 ? role : 5]);
}

// A size in a whole number of GB (from 1 GB, keeping GB at >= 1000 GB for drive headings) or MB, delegating mid-range to ui_size_decimal.
static fn settings_size_text(unsigned long long bytes, char address_to into, positive room)
{
        char digits[24];
        unsigned long long scaled;

        if (bytes >= 1000000ull && bytes < 1000000000000ull)
        {
                ui_size_decimal(bytes, into, room);
                return;
        }
        scaled = bytes >= 1000000000ull ? bytes / 1000000000ull : bytes / 1000000ull;
        digits[positive_into_string((p8 address_to)digits, (positive)scaled)] = 0;
        string_copy_bounded((string_address)into, (string_address)digits, room);
        string_append_bounded((string_address)into, (string_address)(bytes >= 1000000000ull ? " GB" : " MB"), room);
}

/*
        Whether a partition's name says its role's word already, whatever the case of its ASCII letters (EFI system partition, Linux
        swap): its row then says the role no second time.
*/
static b32 settings_name_says(const char address_to name, const char address_to word)
{
        positive length = string_length((string_address)word), name_len = string_length((string_address)name);

        for (positive at = 0; length && at + length <= name_len; at++)
                if (!memory_compare_ascii_case(name + at, word, length))
                        return true;
        return false;
}

static char settings_device_note[SETTINGS_ITEM_LABEL];
static char settings_device_port_cell[SETTINGS_ITEM_LABEL];

// part of whole in parts of scale (100: a percent, 1000: tenths of one), to the nearest; none of nothing.
static long settings_share_of(unsigned long long part, unsigned long long whole, unsigned long long scale)
{
        while (whole >= 1ull << 40)
                whole >>= 1, part >>= 1;
        return whole ? (long)((part * scale + whole / 2) / whole) : 0;
}

static char settings_drive_share[SETTINGS_ITEM_LABEL], settings_drive_amount[2 * SETTINGS_SIZE_TEXT + 16];

static unsigned long long settings_drive_free(const struct settings_item address_to item)
{
        unsigned long long free = 0;

        for (positive at = 0; at < item->segment_count; at++)
                free += item->segment_role[at] == UI_STRIP_FREE ? item->segment_bytes[at] : 0;
        return free;
}

static fn settings_drive_heading(const struct settings_item address_to item, struct ui_row address_to row)
{
        char used_text[SETTINGS_SIZE_TEXT], total_text[SETTINGS_SIZE_TEXT];
        unsigned long long free = settings_drive_free(item), used = item->total > free ? item->total - free : 0;

        settings_size_text(item->total, total_text, sizeof(total_text));
        settings_size_text(used, used_text, sizeof(used_text));
        if (item->segment_count == 1 && item->segment_role[0] == UI_STRIP_UNPARTITIONED)
        {
                string_copy_bounded((string_address)settings_drive_share, (string_address)WORDS(settings_role_unpartitioned), sizeof(settings_drive_share));
                string_copy_bounded((string_address)settings_drive_amount, (string_address)total_text, sizeof(settings_drive_amount));
        }
        else
        {
                long percent = settings_share_of(used, item->total, 100);

                language_format(settings_drive_share, sizeof(settings_drive_share), WORDS(settings_partitioned), free && percent > 99 ? 99l : percent);
                language_format(settings_drive_amount, sizeof(settings_drive_amount), WORDS(settings_used_of), used_text, total_text);
        }
        row->share = settings_drive_share, row->amount = settings_drive_amount;
}

static fn settings_drive_strip(const struct settings_item address_to item, struct ui_row address_to row)
{
        if (!item->segment_count || !item->segment_role)
                return;
        row->tall = true, row->strip_bytes = item->segment_bytes, row->strip_roles = item->segment_role, row->strip_count = item->segment_count;
        settings_drive_heading(item, row);
}

static b32 settings_item_storage(const struct settings_item address_to item) { return item && (item->flags & (SETTINGS_ITEM_DRIVE | SETTINGS_ITEM_DISK)); }
static const char address_to settings_drive_name(const struct settings_item address_to item) { return item->flags & SETTINGS_ITEM_DISK ? item->port : item->label; }

static int settings_partitions_of(const struct settings_item address_to item)
{
        int count = 0;

        for (positive at = 0; at < item->segment_count; at++)
                count += item->segment_role[at] < UI_STRIP_FREE;
        return count;
}

static int settings_drive_rows(int row, int count)
{
        const struct settings_item address_to item = settings_item(row);
        int section = settings_section_start(row), next;
        b32 free = false;

        if (!settings_item_storage(item) || !(settings_partitions_of(item) || item->segment_more))
                return count;
        settings_sub_owner = row;
        next = settings_sub_add(section, false, null, "", 0, SETTINGS_SUB_PARTITIONS, 0, count);
        if (next == count)
                return count;
        count = next;
        if (!settings_expanded[row] && !settings_fold_at[row])
                return count;
        for (int at = 0; at <= (int)item->segment_count + 1; at++)
        {
                int part = at < (int)item->segment_count ? at : at == (int)item->segment_count ? SETTINGS_PART_MORE : SETTINGS_PART_FREE;

                if (part >= 0 && item->segment_role[part] >= UI_STRIP_FREE)
                {
                        free |= item->segment_role[part] == UI_STRIP_FREE;
                        continue;
                }
                if ((part == SETTINGS_PART_MORE && !item->segment_more) || (part == SETTINGS_PART_FREE && !free))
                        continue;
                next = settings_sub_add(section, false, null, "", 0, SETTINGS_SUB_PARTITION, part, count);
                if (next == count)
                        break;
                count = next;
        }
        return count;
}

static int settings_partitions_owner(int row)
{
        int at;

        return settings_domain_of(row, &at) == SETTINGS_DOM_SUB && at < settings_sub_count &&
               settings_sub[at].action == SETTINGS_SUB_PARTITIONS ? settings_sub[at].owner : -1;
}

static char settings_part_size[SETTINGS_SIZE_TEXT], settings_part_share[SETTINGS_ITEM_LABEL], settings_part_label[SETTINGS_ITEM_LABEL];

static fn settings_part_describe(const struct settings_sub address_to sub, struct ui_row address_to row)
{
        const struct settings_item address_to item = settings_item(sub->owner);
        unsigned long long bytes = 0;
        long tenths;

        row->section = sub->section, row->kind = UI_ROW_SHARE;
        if (!item)
                return;
        if (sub->action == SETTINGS_SUB_PARTITIONS)
        {
                row->label = WORDS(settings_partitions);
                row->expander = settings_expanded[sub->owner] ? UI_ROW_EXPANDER_OPEN : UI_ROW_EXPANDER_SHUT;
                row->turn = settings_fold_at[sub->owner];
                settings_part_share[positive_into_string((p8 address_to)settings_part_share, (positive)settings_partitions_of(item))] = 0;
                row->share = settings_part_share;
                return;
        }
        row->reveal = settings_fold_at[sub->owner];
        if (sub->part >= 0 && sub->part < (int)item->segment_count)
        {
                const char address_to name = item->segment_name[sub->part], address_to role = settings_role_word(item->segment_role[sub->part]);

                bytes = item->segment_bytes[sub->part];
                row->swatch = item->segment_role[sub->part] + 1;
                row->label = name[0] ? name : role;
                row->aside = name[0] && !settings_name_says(name, role) ? role : null;
        }
        else if (sub->part == SETTINGS_PART_FREE)
                bytes = settings_drive_free(item), row->swatch = UI_STRIP_FREE + 1, row->label = WORDS(settings_role_free);
        else
        {
                unsigned long long shown = 0;

                for (positive at = 0; at < item->segment_count; at++)
                        shown += item->segment_bytes[at];
                bytes = item->total > shown ? item->total - shown : 0;
                language_format(settings_part_label, sizeof(settings_part_label), WORDS(settings_not_shown), (long)item->segment_more);
                row->label = settings_part_label, row->dim = true;
                if (bytes <= SETTINGS_ALIGNMENT)
                        return;
        }
        settings_size_text(bytes, settings_part_size, sizeof(settings_part_size));
        tenths = settings_share_of(bytes, item->total, 1000);
        language_format(settings_part_share, sizeof(settings_part_share), WORDS(settings_share), tenths / 10, tenths % 10);
        row->amount = settings_part_size, row->share = settings_part_share;
}

static fn settings_drive_follow(void)
{
        for (int at = 0; at < SETTINGS_LIST_ROWS; at++)
        {
                int row = (int)SETTINGS_LIST + 2 * SETTINGS_LIST_ROWS + at;
                const struct settings_item address_to item = at < settings.item_count[2] ? &settings_large->item[2][at] : null;
                b32 open = settings_item_storage(item) && settings_drive_is_open(settings_drive_name(item));

                if (settings_expanded[row] != open)
                        settings_expanded[row] = (unsigned char)open, settings_fold_at[row] = open ? UI_ONE : 0, settings_fold_live[row] = 0,
                        settings_fold_drop(row);
        }
}

static const char address_to settings_state_word(const char address_to state)
{
        static const struct
        {
                const char address_to key;
                unsigned int text;
        } table[] = {
            {"allowed",   LANGUAGE_settings_state_allowed},
            {"blocked",   LANGUAGE_settings_state_blocked},
            {"waiting",   LANGUAGE_settings_state_waiting},
            {"view only", LANGUAGE_settings_state_view_only},
        };

        for (positive at = 0; at < sizeof(table) / sizeof(table[0]); at++)
                if (string_equals((string_address)state, (string_address)table[at].key))
                        return language_text(table[at].text);
        return state;
}

/*
        A port's line, "PORT usb ID NAME STATE": the port is the name device allow and device deny take, the label is what the device
        says it is (its id when it says nothing), and the state is the line's last word (allowed, blocked or waiting), its own and
        always shown. A line whose second word is not usb is a port with what comes before its state as its label. The label is cut
        by the row when it is too long.
*/
static fn settings_item_port(struct settings_item address_to item, const char address_to text, positive size)
{
        positive state = size, stop, first = 0, from;

        while (state > 0 && text[state - 1] != ' ')
                state--;
        stop = state;
        while (stop > 0 && text[stop - 1] == ' ')
                stop--;
        while (first < size && text[first] != ' ')
                first++;
        settings_item_word(item->port, sizeof(item->port), text, first);
        if (!state || !stop)
                return settings_item_label(item, text, size);
        settings_item_word(item->state, sizeof(item->state), text + state, size - state);
        if (first + 5 > stop || memory_compare(text + first, " usb ", 5))
                return settings_item_label(item, text, stop);
        // After usb, the id, and after it what the device says it is.
        item->flags |= SETTINGS_ITEM_USB;
        from = first + 5;
        while (from < stop && text[from] != ' ')
                from++;
        {
                positive name = from;

                while (name < stop && text[name] == ' ')
                        name++;
                if (name < stop)
                        settings_item_label(item, text + name, stop - name);
                else
                        settings_item_label(item, text + first + 5, from - first - 5);
        }
}

/*
        A partition's name as the host says it, into the item: the host writes a blank of the name as an underscore (its line is
        words a blank apart), and "-" for no name. It is cut where a character ends, so a name in any script stays whole letters.
*/
static fn settings_segment_name(char address_to into, const char address_to text, positive size)
{
        positive took = 0;

        into[0] = 0;
        if (!size || (size == 1 && text[0] == '-'))
                return;
        for (positive at = 0; at < size;)
        {
                unsigned char lead = (unsigned char)text[at];
                positive span = lead < 0x80 ? 1 : lead >= 0xf0 ? 4 : lead >= 0xe0 ? 3 : lead >= 0xc0 ? 2 : 1;

                if (took + span >= SETTINGS_SEGMENT_NAME || at + span > size)
                        break;
                for (positive k = 0; k < span; k++)
                        into[took + k] = text[at + k] == '_' ? ' ' : text[at + k];
                took += span, at += span;
        }
        into[took] = 0;
}

// Parse a drive line ("NAME drive N MB STATE") from after " drive ".
static fn settings_item_drive(struct settings_item address_to item, const char address_to name, positive name_len, struct settings_cursor c)
{
        unsigned long long megabytes = sc_u64(&c, 1000000000000ull);

        settings_item_label(item, name, name_len);
        sc_spaces(&c);
        sc_eat(&c, "MB");
        sc_spaces(&c);
        item->total = megabytes * 1000000ull;
        settings_size_text(item->total, item->size, sizeof(item->size));
        settings_item_word(item->state, sizeof(item->state), c.s, c.n);
}

static fn settings_item_swap(struct settings_item address_to a, struct settings_item address_to b)
{
        struct settings_item tmp = *a;

        *a = *b, *b = tmp;
}

static fn settings_device_order(struct settings_item address_to items, positive count)
{
        for (positive at = 1; at < count; at++)
                for (positive back = at; back > 0 && settings_device_section(&items[back]) < settings_device_section(&items[back - 1]); back--)
                        settings_item_swap(&items[back], &items[back - 1]);
}

static b32 settings_drive_bind(struct settings_item address_to item, positive address_to used)
{
        struct settings_drive address_to d;

        if (*used >= SETTINGS_DRIVES)
                return false;
        d = &settings_large->drive[(*used)++];
        item->segment_role = d->role, item->segment_bytes = d->bytes, item->segment_name = d->name;
        return true;
}

static fn settings_feed_hardware(void)
{
        struct settings_item address_to items = settings_large->item[2], address_to last = null;
        char line[SETTINGS_PROBE];
        char address_to text;
        positive at, size, count = 0, hidden = 0, drives = 0;
        b32 open = false;
        unsigned long long spanned = 0;

        memory_zero(items, sizeof(settings_large->item[2]));
        memory_zero(settings_large->drive, sizeof(settings_large->drive));
        for (at = 0; settings_feed_line(&at, line, sizeof(line), &text, &size);)
        {
                struct settings_cursor c = {text, size};

                if (sc_eat(&c, "    "))
                {
                        if (sc_eat(&c, "part "))
                        {
                                struct settings_item address_to drive = open ? &items[count - 1] : null;
                                unsigned long long bytes;
                                positive role_len;
                                int code;

                                sc_word(&c, null), sc_spaces(&c);
                                bytes = sc_u64(&c, 1844674407370955160ull), sc_spaces(&c);
                                code = settings_role_code(sc_word(&c, &role_len), role_len), sc_spaces(&c);
                                if (drive)
                                        spanned = spanned + bytes < spanned ? ~0ull : spanned + bytes,
                                        drive->total = spanned > drive->total ? spanned : drive->total;
                                if (drive && drive->segment_role && code >= 0 && drive->segment_count < SETTINGS_SEGMENTS && (code != 4 || bytes > SETTINGS_ALIGNMENT))
                                        drive->segment_bytes[drive->segment_count] = bytes,
                                        settings_segment_name(drive->segment_name[drive->segment_count], c.s, c.n),
                                        drive->segment_role[drive->segment_count++] = (unsigned char)code;
                        }
                        else if (open && sc_eat(&c, "more "))
                        {
                                unsigned long long more = sc_u64(&c, 65535);

                                items[count - 1].segment_more = more > 65535 ? 65535 : (unsigned short)more;
                        }
                        else if (sc_eat(&c, "drive "))
                        {
                                struct settings_item address_to port = last;
                                unsigned long long megabytes;

                                open = port && port->kind == SETTINGS_PORT && !(port->flags & SETTINGS_ITEM_DISK);
                                sc_word(&c, null), sc_spaces(&c);
                                megabytes = sc_u64(&c, 1000000000000ull);
                                if (open)
                                {
                                        port->flags |= SETTINGS_ITEM_DISK, port->total = megabytes * 1000000ull;
                                        settings_size_text(port->total, port->size, sizeof(port->size));
                                        open = settings_drive_bind(port, &drives);
                                }
                                spanned = 0;
                        }
                        else if (sc_eat(&c, "where "))
                        {
                                if (last && (last->flags & SETTINGS_ITEM_DRIVE))
                                        last->where = sc_eat(&c, "external")
                                                          ? (c.n >= 3 && !memory_compare(c.s + c.n - 3, " sd", 3) ? SETTINGS_WHERE_SD : SETTINGS_WHERE_EXTERNAL)
                                                          : SETTINGS_WHERE_INTERNAL;
                                open = false;
                        }
                        else
                                open = false;
                        continue;
                }
                open = false;
                if (sc_eat(&c, "  ") && c.n && c.s[0] != ' ')
                {
                        struct settings_cursor whole = c;
                        positive name_len;
                        const char address_to name;

                        if (count + 1 >= SETTINGS_LIST_ROWS)
                        {
                                hidden++, last = null;
                                continue;
                        }
                        name = sc_word(&c, &name_len);
                        open = sc_eat(&c, " drive ");
                        items[count].kind = open ? SETTINGS_NOTE : SETTINGS_PORT;
                        items[count].flags = (whole.n > 8 && !memory_compare(whole.s + whole.n - 8, " allowed", 8) ? SETTINGS_ITEM_JOINED : 0) |
                                             (open ? SETTINGS_ITEM_DRIVE : 0);
                        items[count].where = open ? SETTINGS_WHERE_INTERNAL : SETTINGS_WHERE_EXTERNAL;
                        spanned = 0, last = &items[count];
                        if (open)
                                settings_item_drive(&items[count], name, name_len, c), open = settings_drive_bind(&items[count], &drives);
                        else
                                settings_item_port(&items[count], whole.s, whole.n);
                        count++;
                }
        }
        settings_device_order(items, count);
        hidden += settings.runner.cut_rows;
        if (hidden)
        {
                char note[SETTINGS_ITEM_LABEL];

                language_format(note, sizeof(note), WORDS(settings_not_shown), (long)hidden);
                settings_note_add(items, &count, count ? items[count - 1].where : SETTINGS_WHERE_NONE, note, string_length((string_address)note));
        }
        if (!count)
                settings_note_add(items, &count, SETTINGS_WHERE_NONE, WORDS(settings_device_none), string_length((string_address)WORDS(settings_device_none)));
        settings.item_count[2] = (unsigned short)count;
        settings_drive_follow();
}

// Parse `moonwater link` peers and last-heard detail lines into settings_large->item[3].
static fn settings_feed_links(void)
{
        struct settings_item address_to items = settings_large->item[3];
        char line[SETTINGS_PROBE];
        char address_to text;
        positive at, size, count = 0;

        memory_zero(items, sizeof(settings_large->item[3]));
        for (at = 0; settings_feed_line(&at, line, sizeof(line), &text, &size);)
        {
                struct settings_cursor c = {text, size};

                if (sc_eat(&c, "      ") && c.n && c.s[0] != ' ')
                {
                        if (count && items[count - 1].kind == SETTINGS_LINK)
                        {
                                sc_trim_at(&c, "  next:");
                                if (c.n)
                                {
                                        char piece[SETTINGS_ITEM_LABEL];

                                        settings_item_word(piece, sizeof(piece), c.s, c.n);
                                        if (items[count - 1].detail[0])
                                                string_append_bounded((string_address)items[count - 1].detail, (string_address) "; ", sizeof(items[0].detail));
                                        string_append_bounded((string_address)items[count - 1].detail, (string_address)piece, sizeof(items[0].detail));
                                }
                        }
                }
                else if (sc_eat(&c, "  ") && c.n > 2 && c.s[0] != ' ' && count + 1 < SETTINGS_LIST_ROWS)
                {
                        positive may_at = settings_find_last(c.s, c.n, "...  may ");

                        if (may_at < c.n)
                        {
                                positive cut = 0;
                                struct settings_cursor grants = {c.s + may_at + 9, c.n - may_at - 9};

                                while (cut + 1 < c.n && (c.s[cut] != ' ' || c.s[cut + 1] != ' '))
                                        cut++;
                                sc_trim_at(&grants, "  next:");
                                items[count].kind = SETTINGS_LINK, items[count].flags = SETTINGS_ITEM_SAVED;
                                settings_item_word(items[count].access, sizeof(items[count].access), grants.s, grants.n);
                                settings_item_label(&items[count++], c.s, cut);
                        }
                        else if (settings_starts(c.s, "nobody"))
                                settings_note_add(items, &count, 0, c.s, c.n);
                }
        }
        settings.item_count[3] = (unsigned short)count;
}

// Details column headers for the four feed lists (Wi-Fi, Bluetooth, External devices, Waterlink).
static struct ui_columns address_to settings_columns_for(int list)
{
        static const struct
        {
                unsigned char count, at_end;
                unsigned short w[4];
                unsigned int title_id[4];
        } specs[SETTINGS_LISTS] = {
            {4, 3, {0, 84, 84, 72},   {LANGUAGE_files_column_name, LANGUAGE_settings_column_status, LANGUAGE_settings_column_security, LANGUAGE_settings_column_signal}},
            {2, 1, {0, 100, 0, 0},    {LANGUAGE_files_column_name, LANGUAGE_settings_column_status, 0, 0}},
            {4, 3, {0, 108, 84, 128}, {LANGUAGE_files_column_name, 0, LANGUAGE_settings_column_status, LANGUAGE_settings_column_access}},
            {3, 0, {0, 140, 180, 0},  {LANGUAGE_files_column_name, LANGUAGE_settings_column_access, LANGUAGE_settings_column_status, 0}},
        };
        struct ui_columns address_to c;

        if (!settings_large || list < 0 || list >= SETTINGS_LISTS)
                return null;
        c = &settings_large->columns[list];
        if (!c->count)
        {
                c->sorted = c->hover = c->held = -1;
                c->count = specs[list].count;
                for (int k = 1; k < specs[list].count; k++)
                        c->width[k] = specs[list].w[k];
                if (specs[list].at_end)
                        c->at_end[specs[list].at_end] = true;
        }
        for (int k = 0; k < specs[list].count; k++)
                c->title[k] = specs[list].title_id[k] ? language_text(specs[list].title_id[k]) : language_of("Port");
        return c;
}

static int settings_list_compare(int list, int col, const struct settings_item address_to a, const struct settings_item address_to b)
{
        int cmp = 0;

        if (list == 0 && col == 1)
                cmp = (a->flags & SETTINGS_ITEM_JOINED ? 0 : a->flags & SETTINGS_ITEM_SAVED ? 1 : 2) -
                      (b->flags & SETTINGS_ITEM_JOINED ? 0 : b->flags & SETTINGS_ITEM_SAVED ? 1 : 2);
        else if (list == 0 && col == 2)
                cmp = (int)string_compare_folded((string_address)a->security, (string_address)b->security);
        else if (list == 0 && col == 3)
        {
                int ha = (a->flags & SETTINGS_ITEM_HEARD) != 0, hb = (b->flags & SETTINGS_ITEM_HEARD) != 0;

                cmp = ha != hb ? ha - hb : (int)a->dbm - (int)b->dbm;
        }
        else if (list == 2 && col == 1)
                cmp = (int)string_compare_folded((string_address)a->port, (string_address)b->port);
        else if (list == 2 && col == 2)
                cmp = (int)string_compare_folded((string_address)settings_state_word(a->state), (string_address)settings_state_word(b->state));
        else if (list == 2 && col == 3)
                cmp = ((a->flags & SETTINGS_ITEM_JOINED) ? 0 : 1) - ((b->flags & SETTINGS_ITEM_JOINED) ? 0 : 1);
        else if (list == 3 && col == 1)
                cmp = (int)string_compare_folded((string_address)a->access, (string_address)b->access);
        else if (list == 3 && col == 2)
                cmp = (int)string_compare_folded((string_address)a->detail, (string_address)b->detail);
        return cmp ? cmp : (int)string_compare_folded((string_address)a->label, (string_address)b->label);
}

static b32 settings_item_sortable(int list, const struct settings_item address_to item)
{
        return item->kind != SETTINGS_NOTE && item->kind != SETTINGS_ADD && (list != 2 || settings_device_section(item) == 1);
}

static fn settings_list_sort(int list)
{
        struct ui_columns address_to c = settings_columns_for(list);
        struct settings_item address_to items;
        int count;

        if (!c || c->sorted < 0 || list < 0 || list >= SETTINGS_LISTS)
                return;
        items = settings_large->item[list], count = settings.item_count[list];
        for (int at = 1; at < count; at++)
                if (settings_item_sortable(list, &items[at]))
                        for (int back = at; back > 0 && settings_item_sortable(list, &items[back - 1]); back--)
                        {
                                int cmp = settings_list_compare(list, c->sorted, &items[back - 1], &items[back]);

                                if ((c->descending ? -cmp : cmp) <= 0)
                                        break;
                                settings_item_swap(&items[back], &items[back - 1]);
                        }
        if (list == 2)
                settings_drive_follow();
}

static fn settings_focus_keep(int list, char address_to keep, positive room)
{
        keep[0] = 0;
        if (settings_list_of(settings.list.focus) == list && settings_item(settings.list.focus))
                string_copy_bounded((string_address)keep, (string_address)settings_item(settings.list.focus)->label, room);
}

static fn settings_focus_restore(int list, const char address_to keep, int fallback)
{
        int found = fallback;

        if (!keep[0])
                return;
        for (int each = 0; each < settings.item_count[list]; each++)
                if (string_equals((string_address)settings_large->item[list][each].label, (string_address)keep))
                {
                        found = each;
                        break;
                }
        if (found >= 0)
                settings.list.focus = (int)SETTINGS_LIST + list * SETTINGS_LIST_ROWS + found;
}

static fn settings_columns_sorted(struct ui_columns address_to hit)
{
        int list = -1;
        char keep[SETTINGS_ITEM_LABEL];

        if (!settings_large || !hit)
                return;
        for (int k = 0; k < SETTINGS_LISTS; k++)
                if (&settings_large->columns[k] == hit)
                {
                        list = k;
                        break;
                }
        if (list < 0)
                return;
        settings_focus_keep(list, keep, sizeof(keep));
        settings_list_sort(list);
        settings_focus_restore(list, keep, -1);
        settings_rebuild();
}

static positive settings_list_sum(int list)
{
        positive sum = settings_fnv64_bytes(SETTINGS_FNV64_SEED, settings_large->item[list], sizeof(settings_large->item[list]));

        if (list == 2)
                sum = settings_fnv64_bytes(sum, settings_large->drive, sizeof(settings_large->drive));
        return sum ^ settings.item_count[list];
}

static int settings_feed_owner(int feed)
{
        for (positive at = 0; at < SETTINGS_ROWS && settings_feeds[feed].owner; at++)
                if (string_equals((string_address)settings_rows[at].label, (string_address)settings_feeds[feed].owner))
                        return (int)at;
        return -1;
}

static int settings_feed_of_group(void)
{
        for (positive feed = 0; feed < SETTINGS_FEEDS; feed++)
                if (string_equals((string_address)settings_feeds[feed].group, (string_address)settings_group_name(settings.page.chosen)))
                        return (int)feed;
        return -1;
}

static fn settings_feed_read(b32 killed, b32 failed)
{
        static fn (*const parsers[])(void) = {
            [SETTINGS_FEED_WIFI] = settings_feed_wifi,
            [SETTINGS_FEED_DEVICES] = settings_feed_devices,
            [SETTINGS_FEED_HARDWARE] = settings_feed_hardware,
            [SETTINGS_FEED_LINKS] = settings_feed_links,
        };
        int feed = settings.feed_list, list = feed, owner = settings_feed_owner(feed), at;
        char keep[SETTINGS_ITEM_LABEL];
        positive before, was = 0;
        b32 changed = false;

        settings.feed_due = (positive)ui_kit.now + (positive)settings_feeds[feed].every * 1000000ull;
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
        before = settings_list_sum(list);
        settings_focus_keep(list, keep, sizeof(keep));
        at = settings.list.focus - (int)SETTINGS_LIST - list * SETTINGS_LIST_ROWS;
        parsers[settings_feeds[feed].kind]();
        settings_list_sort(list);
        settings_layout_check();
        if (settings_list_sum(list) == before && !changed)
                return;
        {
                int fallback = at < settings.item_count[list] ? at : settings.item_count[list] - 1;

                settings_focus_restore(list, keep, fallback < 0 ? 0 : fallback);
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
        settings.job = SETTINGS_JOB_FEED;
        settings.job_sets = false;
        settings.feed_list = feed;
        settings_start(words, count, null);
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
                ui_list_row_changed(&settings.ui, &settings.list, row), settings_try_changed(row);
}

// Whether a row's verb waits for another machine (a pairing): its job has the pairing's time, and what it says while it waits is shown.
static b32 settings_waits(int row)
{
        return row >= 0 && row < SETTINGS_ANY && (settings_rows[row].flags & SETTINGS_WAITS) != 0;
}

// The first non-blank line of what the verb has said so far, or its last when last is set: its size, and 0 when there is none.
static positive settings_said_line(char address_to line, positive room, b32 last)
{
        positive at = 0, size = 0;

        while (at < settings.said_length)
        {
                char got[SETTINGS_STATUS];
                positive made = settings_line(settings_large->said, settings.said_length, &at, got, sizeof(got));

                if (made)
                {
                        string_copy_bounded((string_address)line, (string_address)got, room);
                        size = made;
                        if (!last)
                                break;
                }
        }
        return size;
}

/*
        The Pair a machine row's wait shows its code large, with the seconds left, in a card: the card's lines are the CLI's own words
        (the line that says the code and what the other machine types, then its outcome once it has one). The code is read from the
        CLI's first line, NAME CODE: on the other machine, ...; the seconds count the five minutes a code lasts, from when the verb
        started. A press on the row while it waits stops the wait (settings_activate): the CLI closes the code and says why.
*/
#define SETTINGS_PAIR_SECONDS 300 // a code lasts five minutes: link pair waits that long, and two seconds more
#define SETTINGS_PAIR_CODE 16
#define SETTINGS_PAIR_SIZE 36     // the code's size in logical pixels, in the mono face
#define SETTINGS_PAIR_TICK_NS 1000000000ull

static b32 settings_pair_on;                                       // the card of a pairing's wait is up: its code is drawn large
static char settings_pair_code[SETTINGS_PAIR_CODE];
static positive settings_pair_due;                                 // when the seconds left are drawn next

// Whether a row is a pairing: the action whose verb waits for another machine.
static b32 settings_pair_row(int row)
{
        return row >= 0 && row < SETTINGS_ANY && settings_rows[row].kind == SETTINGS_ACTION && settings_waits(row);
}

// Whether the pairing row's verb is running and waiting now.
static b32 settings_pair_waiting(int row)
{
        return settings.child > 0 && settings.job_sets && settings.job == row && settings_pair_row(row);
}

// What an action's button says: what it does (the last word of its verb says which), Running while it is busy, and Stop while a pairing waits.
static const char address_to settings_button_words(int row)
{
        static const struct
        {
                const char address_to verb;
                unsigned int text;
        } table[] = {
            {"sync",    LANGUAGE_settings_button_set},
            {"refresh", LANGUAGE_settings_button_discover},
            {"pair",    LANGUAGE_settings_button_pair},
            {"end",     LANGUAGE_settings_button_end},
        };
        const struct settings_row address_to which = &settings_rows[row];
        const char address_to last = null;

        if (settings_pair_waiting(row))
                return WORDS(settings_button_stop);
        if (settings_action_busy(row))
                return WORDS(settings_button_running);
        for (positive k = 0; k < SETTING_WORDS && which->set[k]; k++)
                last = which->set[k];
        for (positive at = 0; last && at < sizeof(table) / sizeof(table[0]); at++)
                if (string_equals((string_address)last, (string_address)table[at].verb))
                        return language_text(table[at].text);
        return WORDS(settings_button_run);
}

// The code, from the CLI's first line: NAME CODE: on the other machine, moonwater link NAME CODE (the name is before the first blank).
static fn settings_pair_parse(const char address_to line, positive size)
{
        positive at = 0, made = 0;

        while (at < size && line[at] != ' ')
                at++;
        while (at < size && line[at] == ' ')
                at++;
        while (at < size && line[at] != ' ' && line[at] != ':' && made + 1 < sizeof(settings_pair_code))
                settings_pair_code[made++] = line[at++];
        settings_pair_code[made] = 0;
}

// The pairing's card opens once the CLI has said its code: its first line is the one that says the other machine's command.
static fn settings_pair_open(void)
{
        char line[SETTINGS_STATUS];
        positive size = settings_said_line(line, sizeof(line), false);

        settings_pair_parse(line, size);
        settings_pair_on = true;
        settings_pair_due = 0;
        settings_card_open(settings_label(settings.job));
}

// The wait has ended: the card (if it is up) is made again from what the CLI said, its outcome among the lines.
static fn settings_pair_end(void)
{
        settings_pair_on = false;
        if (settings.page.card)
        {
                settings_card_fill();
                ui_lines_wrap(&settings.ui, &settings.card_text, settings.width);
                settings_layout();
        }
        ui_page_redraw(&settings.page);
}

// The seconds the code has left: SETTINGS_PAIR_SECONDS from the start of the verb, none once they are gone.
static positive settings_pair_left(void)
{
        positive now = (positive)ui_kit.now, gone = now > settings.began ? (now - settings.began) / 1000000000ull : 0;

        return gone < SETTINGS_PAIR_SECONDS ? SETTINGS_PAIR_SECONDS - gone : 0;
}

// The pairing's card: the code large at the top, its seconds left as m:ss under it, and the CLI's lines under those.
static fn settings_pair_paint(struct ui address_to ui)
{
        char text[SETTINGS_PROBE], line[SETTINGS_PROBE];
        positive left = settings_pair_left(), at = 0;
        int edge = ui_px(ui, UI_PAD) + ui_dev(ui, UI_INSET), x = ui_rtl() ? ui->width - edge : edge;
        int top = settings.card.y + ui_dev(ui, UI_LINES_PAD), baseline = top + ui_dev(ui, SETTINGS_PAIR_SIZE), y;
        int count = settings.card_text.count;

        ui_kit_start_at(ui, x, baseline, (string_address)settings_pair_code, UI_CODE, SETTINGS_PAIR_SIZE, ui_role(UI_ON_SURFACE), 0, 0, ui->width);
        at = positive_into((p8 address_to)text, left / 60);
        text[at++] = ':';
        at += positive_into_pair((p8 address_to)(text + at), left % 60);
        text[at] = 0;
        ui_kit_text_flipped(ui, edge, baseline + ui_dev(ui, 28), (string_address)text, UI_BODY_MEDIUM, ui_role(UI_ON_SURFACE), 0, 0, ui->width);
        y = baseline + ui_dev(ui, 28 + 24);
        for (int each = 0; each < count; each++, y += ui_dev(ui, UI_LINE))
        {
                string_address card_line = (string_address)ui_lines_copy(&settings.card_text, each, line, sizeof(line));

                ui_kit_text_flipped(ui, edge, ui_kit_baseline(ui, y, UI_BODY_MEDIUM), card_line, UI_BODY_MEDIUM, ui_role(UI_ON_SURFACE), 0, 0, ui->width);
        }
}

// A pairing shows its code while it waits, not when it ends: the first line the verb said is the status until then.
static fn settings_waiting_say(void)
{
        char line[SETTINGS_STATUS];
        positive size = 0;

        if (settings.child > 0 && settings.job_sets && settings_waits(settings.job) && (size = settings_said_line(line, sizeof(line), false)))
        {
                settings_say((string_address)line);
                if (!settings_pair_on && settings_pair_row(settings.job) && settings_find(line, size, "on the other machine") < size)
                        settings_pair_open();
        }
}

// Verb completion: distribute output, trigger follow-up reads, and start next queued job.
static fn settings_complete(b32 killed, positive status)
{
        int row = settings.job;
        b32 action = settings.job_sets && settings_action_row(row);

        if (settings.job_sets)
        {
                char line[SETTINGS_STATUS];
                positive size;

                if (row >= SETTINGS_ANY)
                        settings_report(killed, status);
                else
                {
                        size = settings_said_line(line, sizeof(line), settings_waits(row));
                        settings_say((string_address)(killed ? WORDS(settings_too_long)
                                                      : size ? line : wait_status_code(status) ? WORDS(settings_refused) : WORDS(settings_done)));
                }
                if (settings_pair_on)
                        settings_pair_end();
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
                for (positive other = 0; other < SETTINGS_ROWS; other++)
                {
                        char before[SETTINGS_VALUE];
                        unsigned char was = settings.state[other];

                        if (((int)other != row && !settings_unread_is(other)) || !settings_same_verb(&settings_rows[row], &settings_rows[other]))
                                continue;
                        settings_unread_set(other, 0);
                        string_copy_bounded((string_address)before, (string_address)settings.value[other], sizeof(before));
                        settings.state[other] = killed ? SETTINGS_FAILED
                                                       : (unsigned char)settings_read(&settings_rows[other], settings_large->said, settings.said_length,
                                                                                      settings.value[other], SETTINGS_VALUE, wait_status_code(status) != 0);
                        if (settings.state[other] != was || !string_equals((string_address)before, (string_address)settings.value[other]))
                                settings_reading_changed((int)other);
                }
        settings_next_job();
        if (action)
                ui_page_redraw(&settings.page);
        if (!settings.told && !settings_unread_any() && settings.child <= 0 && !settings.queued)
        {
                positive failed = settings_not_understood();

                settings.told = true;
                compositor_kmsg((string_address) "settings: rows unread or not understood when first asked: ", failed,
                                failed ? (string_address) "" : (string_address) "0");
        }
}

#define settings_left ui_job_left
static b32 settings_reap(void) { settings_runner_ready(); return ui_job_reap(&settings.runner); }
static b32 settings_left_reap(void) { return ui_job_left_reap(); }
static fn settings_finished(b32 killed) { settings_runner_ready(); ui_job_finish(&settings.runner, killed); }
static fn settings_feed_drop(void) { if (settings.child > 0 && settings.job == SETTINGS_JOB_FEED) settings_finished(true); }

/* ---- what a person does ---- */

/*
        A row of All commands run with args, in words: false when it did not
        run and the field stays open, because there was nothing to run, there
        were too many words, or it ends what a person has and asked for a
        second Enter.
*/
static fn settings_confirm_say(const char address_to text)
{
        char line[SETTINGS_STATUS];

        ui_confirm_line(line, sizeof(line), text);
        settings_say((string_address)line);
}

static b32 settings_run(int row, const char address_to args)
{
        string_address words[SETTINGS_WORDS];
        positive count = settings_generic_words(row, args, words, SETTINGS_WORDS);

        if (!count || count == 1)
        {
                settings_say((string_address)(count ? WORDS(settings_type_command) : WORDS(settings_too_many)));
                return false;
        }
        if (settings_asks_for(words + 1, count - 1) && !ui_confirm_ask(&settings.confirm, row + 1, args))
        {
                char text[SETTINGS_STATUS];

                settings_join(words, count, text, sizeof(text));
                settings_confirm_say(text);
                return false;
        }
        if (settings_refused(row, args))
                return false;
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

static b32 settings_output_choice(const struct settings_row address_to which, positive index, char address_to into, positive room);

// The row that says whether the audio modem is ready: the Audio modem row, which reads what moonwater link transport says; or -1.
static int settings_modem_row(void)
{
        for (positive at = 0; at < SETTINGS_ROWS; at++)
                if (settings_rows[at].kind == SETTINGS_INFO && settings_rows[at].key &&
                    string_equals((string_address)settings_rows[at].key, (string_address) "audio modem "))
                        return (int)at;
        return -1;
}

// Whether the audio modem is ready, as its row was last read: the sound server has a sound card open for it.
static b32 settings_modem_ready(void)
{
        int row = settings_modem_row();

        return row >= 0 && settings.state[row] == SETTINGS_READ &&
               string_equals((string_address)settings.value[row], (string_address) "ready");
}

static b32 settings_choice(const struct settings_row address_to row, positive index, char address_to into, positive room)
{
        if (row->output != SETTINGS_OUT_NONE)
                return settings_output_choice(row, index, into, room);
        //      Connect over offers network always, and modem only while the audio modem is ready: a modem that is not there is not
        //      a choice, and one chosen anyway is refused by the verb, which says why in the status line.
        if (row->flags & SETTINGS_TRANSPORT)
        {
                if (index == 0)
                        return settings_word(row->choices, 0, into, room);
                if (index == 1 && settings_modem_ready())
                        return settings_word(row->choices, 1, into, room);
                return false;
        }
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

// The English a choice's word is shown as, before the language says it. One mapping for every choice row, so a word reads the
// same wherever it is shown (the menu, the value, a row's tabs): auto is Automatic, which the language files translate by name.
// The on-screen keyboard's off and on say what they do (the verb and the file keep auto, off and on); any other word is its own.
static const char address_to settings_choice_said(int row, const char address_to word)
{
        if (settings_rows[row].path && string_equals((string_address)settings_rows[row].path, (string_address)SETTING_ONSCREEN_PATH))
        {
                if (string_equals((string_address)word, (string_address) "off"))
                        return "Always off";
                if (string_equals((string_address)word, (string_address) "on"))
                        return "Always on";
        }
        return string_equals((string_address)word, (string_address) "auto") ? "Automatic" : word;
}

// The plain word a row's verb word is read as: Screen modes says Biggest picture and Recommended, others say their word.
static const char address_to settings_label_of(int row, const char address_to word)
{
        // The Language row says each language by its own name, whichever language is taken (language_names).
        if (settings_rows[row].flags & SETTINGS_LANGUAGE)
                return language_name_of(word);
        // The Battery picker's auto and none are words the window shows (its batteries' names are their own).
        if (settings_rows[row].flags & SETTINGS_BATTERIES)
                return language_of(word);
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
        else if (settings_rows[row].flags & SETTINGS_BATTERIES)
                word = string_equals((string_address)label, (string_address)language_of("auto")) ? "auto" // shown against shown
                       : string_equals((string_address)label, (string_address)language_of("none")) ? "none" : label; // shown against shown
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
        const struct settings_out_spec address_to spec = which->output < 10 ? &settings_out_specs[which->output] : null;

        if (spec && spec->pair_text[0])
        {
                if (chosen < 0 || index >= 2)
                        return false;
                string_copy_bounded((string_address)into, (string_address)language_text(spec->pair_text[index]), room);
                return true;
        }
        switch (which->output)
        {
        case SETTINGS_OUT_SCREEN:
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
        case SETTINGS_OUT_TURN:
                if (index >= sizeof(settings_turns) / sizeof(settings_turns[0]))
                        return false;
                string_copy_bounded((string_address)into, (string_address)language_of(settings_turns[index].label), room);
                return true;
        case SETTINGS_OUT_SCALE:
                if (chosen < 0 || index >= sizeof(settings_scales) / sizeof(settings_scales[0]))
                        return false;
                settings_scale_label(settings_scales[index], settings_scales[index] == settings_scale_suggested(table, chosen), into, room);
                return true;
        case SETTINGS_OUT_REFRESH:
                if (chosen < 0 || index >= settings_rates_of(chosen))
                        return false;
                settings_rate_label(table->output_hz[chosen][index], index == 0, into, room);
                return true;
        case SETTINGS_OUT_SIZE:
                if (chosen < 0)
                        return false;
                if (settings_keep_here(chosen) && index == 0)
                {
                        settings_keep_label(into, room);
                        return true;
                }
                index -= settings_keep_here(chosen);
                if (index >= settings_sizes_of(chosen))
                        return false;
                settings_size_label(table, chosen, (unsigned int)index, into, room);
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
        const struct settings_out_spec address_to spec = output < 10 ? &settings_out_specs[output] : null;
        unsigned int choices;

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
        choices = output == SETTINGS_OUT_REFRESH ? settings_rates_of(chosen)
                  : output == SETTINGS_OUT_SIZE  ? settings_sizes_of(chosen)
                  : output == SETTINGS_OUT_SCREEN || output == SETTINGS_OUT_PRIMARY ? settings_map_count() : 2;
        if (spec && spec->empty_text && !choices)
        {
                row->kind = UI_ROW_PLAIN, row->dim = true, row->value = language_text(spec->empty_text);
                return;
        }
        if (spec && spec->plain_when_one && choices == 1 && !(output == SETTINGS_OUT_SIZE && settings_keep_here(chosen)))
                row->kind = UI_ROW_PLAIN;
        row->value = settings_row_text(id, chosen);
}

static fn settings_output_apply(int row, const char address_to label)
{
        const struct settings_row address_to which = &settings_rows[row];
        const struct settings_out_spec address_to spec = which->output < 10 ? &settings_out_specs[which->output] : null;
        struct windows address_to table = settings.own.table;
        int at = settings_choice_index(row, label), chosen = settings_map_chosen();
        char value[SETTINGS_TYPED], back[32], line[SETTINGS_STATUS];
        positive length;

        if (at < 0 || !table)
                return;
        if (which->output == SETTINGS_OUT_SCREEN)
        {
                unsigned int number[WINDOWS_OUTPUTS] = {0};
                int screen;

                windows_screen_numbers(table, number);
                screen = settings_map_numbered(number, settings_map_count(), (unsigned int)at + 1);
                if (screen == chosen)
                        settings_say((string_address)WORDS(settings_already_set));
                else
                        settings.map.display = screen, ui_page_redraw(&settings.page);
                return;
        }
        if (chosen < 0)
        {
                settings_say((string_address)WORDS(settings_no_screen));
                return;
        }
        if (which->output == SETTINGS_OUT_WALLPAPER)
        {
                char path[96];
                b32 own = settings_wall_own(table->output_name[chosen], path, sizeof(path));

                if (at == 1 && !own)
                        settings_say((string_address)WORDS(settings_wall_hint));
                else if ((at == 1) == own)
                        settings_say((string_address)WORDS(settings_already_set));
                else
                {
                        length = settings_map_add(value, sizeof(value), 0, "none ");
                        settings_map_add(value, sizeof(value), length, table->output_name[chosen]);
                        settings_change(row, value);
                }
                return;
        }
        if (which->output == SETTINGS_OUT_SIZE)
        {
                b32 keep = settings_keep_here(chosen);
                unsigned int w, h;

                if (keep && at == 0)
                {
                        settings_resize.on = false;
                        language_format(line, sizeof(line), WORDS(settings_size_kept), (long)settings_resize.width, (long)settings_resize.height);
                        settings_say_redraw(line);
                        return;
                }
                at -= keep;
                if (at < 0 || at >= (int)settings_sizes_of(chosen))
                        return;
                w = table->output_size[chosen][at][0], h = table->output_size[chosen][at][1];
                if (w == table->output_mode_w[chosen] && h == table->output_mode_h[chosen] && !keep)
                {
                        settings_say((string_address)WORDS(settings_already_set));
                        return;
                }
                if (!keep)
                        settings_resize.old_w = table->output_asked_w[chosen], settings_resize.old_h = table->output_asked_h[chosen];
                string_copy_bounded((string_address)settings_resize.name, (string_address)table->output_name[chosen], sizeof(settings_resize.name));
                settings_resize.width = w, settings_resize.height = h;
                settings_resize.due = (positive)ui_kit.now + SETTINGS_KEEP_NS, settings_resize.seen = false, settings_resize.on = true;
                settings_mode_cmd(value, sizeof(value), table->output_name[chosen], spec->verb, w, h, back, sizeof(back));
                settings_change(row, value);
                language_format(line, sizeof(line), WORDS(settings_size_ask), (long)w, (long)h, (long)(SETTINGS_KEEP_NS / 1000000000ull));
                settings_say_redraw(line);
                return;
        }
        length = settings_map_add(value, sizeof(value), 0, table->output_name[chosen]);
        if (which->output == SETTINGS_OUT_PRIMARY)
        {
                if ((at == 0) == (chosen == 0))
                        settings_say((string_address)WORDS(settings_already_set));
                else if (at != 0)
                        settings_say((string_address)WORDS(settings_primary_other));
                else
                        settings_map_add(value, sizeof(value), length, spec->verb), settings_change(row, value);
                return;
        }
        {
                long target = 0, current = -1;
                b32 preferred = false;

                if (which->output == SETTINGS_OUT_TURN && at < (int)(sizeof(settings_turns) / sizeof(settings_turns[0])))
                        target = settings_turns[at].degrees, current = (long)table->output_turn[chosen];
                else if (which->output == SETTINGS_OUT_SCALE && at < (int)(sizeof(settings_scales) / sizeof(settings_scales[0])))
                        target = (long)settings_scales[at], current = (long)table->output_asked_scale[chosen];
                else if (which->output == SETTINGS_OUT_REFRESH && at < (int)settings_rates_of(chosen))
                        target = (long)table->output_hz[chosen][at], current = (long)table->output_now[chosen], preferred = at == 0;
                else
                        return;
                if (target == current)
                {
                        settings_say((string_address)WORDS(settings_already_set));
                        return;
                }
                length = settings_map_add(value, sizeof(value), length, spec->verb);
                if (preferred)
                        settings_map_add(value, sizeof(value), length, "preferred");
                else
                        settings_map_number(value, sizeof(value), length, target);
                settings_change(row, value);
        }
}

// Choice popup menu handlers.
static fn settings_chose(int id);

static fn settings_menu_answer(int answer)
{
        // A key that only moved the hot item (no answer) is drawn now: the keys come to this window, not to the popup's pump, which reads nothing.
        if (answer == UI_MENU_NOTHING)
        {
                if (settings.popup.open)
                        ui_popmenu_present(&settings.popup);
                return;
        }
        ui_popmenu_close(&settings.popup);
        settings_chose(answer);
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

// The item's word (as the row has it) set, and the menu closed; a choice the row already shows sends nothing, and says so.
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
        else
                settings_say((string_address)WORDS(settings_already_set));
}

// Queue a list-item action: "<code char>\n<name>[\n<pass>]" (wiped from stack after queueing).
static fn settings_act(char what, const char address_to name, const char address_to pass)
{
        char value[SETTINGS_TYPED];
        positive size = 2;

        value[0] = what, value[1] = '\n';
        size = settings_map_add(value, sizeof(value) - 1, size, name);
        if (pass && pass[0])
                value[size++] = '\n', size = settings_map_add(value, sizeof(value), size, pass);
        value[size] = 0;
        settings_change(SETTINGS_JOB_ACT, value);
        memory_zero(value, sizeof(value));
}

static const char address_to settings_name_accepts(p32 code)
{
        return (code >= '0' && code <= '9') || ((code | 32) >= 'a' && (code | 32) <= 'z') || code == '-' ? null : WORDS(settings_name_chars);
}

static fn settings_edit_begin(int row, const char address_to text, b32 masked)
{
        struct ui_text address_to t = &settings.edit;
        int dom = settings_domain_of(row, null);

        string_copy_bounded((string_address)settings.edit_from, (string_address)text, sizeof(settings.edit_from));
        ui_field_begin(t, &settings.own, settings.edit_from, row);
        t->masked = masked;
        ui_field_focus(t, true);
        t->cancels = dom != SETTINGS_DOM_ROW;
        t->most = dom == SETTINGS_DOM_LIST || dom == SETTINGS_DOM_TRY ? SETTINGS_ITEM_LABEL - 1
                  : dom == SETTINGS_DOM_COMMAND || (dom == SETTINGS_DOM_ROW && (settings_rows[row].flags & SETTINGS_SPLIT)) ? SETTINGS_TYPED - 1
                                                                                                                            : SETTINGS_VALUE - 1;
        if (row == SETTINGS_NAME_ROW)
                t->accept = settings_name_accepts, t->most = SETTINGS_NAME_MOST, t->most_said = WORDS(settings_name_long);
        ui_page_redraw(&settings.page);
}

static fn settings_edit_end(void) { ui_field_close(&settings.edit); ui_page_redraw(&settings.page); }

static b32 settings_edit_shown(void)
{
        int at = settings_pos(settings_editing()), top = at >= 0 ? ui_list_row_y(&settings.list, at) : 0;
        int tall = at >= 0 && settings.list.tall[at] > 0 ? settings.list.tall[at] : ui_px(&settings.ui, UI_ROW);

        return at >= 0 && top + tall > settings.list.y && top < settings.list.y + settings.list.h;
}

static fn settings_port_set(const struct settings_item address_to item, b32 allow) { settings_act(allow ? 'v' : 'x', item->port, null); }

static fn settings_item_activate(int row, const struct settings_item address_to item)
{
        char line[SETTINGS_STATUS];

        if (item->kind == SETTINGS_NETWORK)
        {
                if (item->flags & SETTINGS_ITEM_JOINED)
                        language_format(line, sizeof(line), WORDS(settings_already_joined), item->label), settings_say((string_address)line);
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
        else if (item->kind == SETTINGS_PORT)
                settings_port_set(item, !(item->flags & SETTINGS_ITEM_JOINED));
        else if (item->kind == SETTINGS_ADD)
                settings_edit_begin(row, "", false), settings_say((string_address)WORDS(settings_type_device));
        else if (item->flags & SETTINGS_ITEM_DRIVE)
                settings_say((string_address)WORDS(settings_item_drive));
        else if (item->kind != SETTINGS_NOTE)
                settings_say((string_address)settings_item_text(item));
        ui_page_redraw(&settings.page);
}

static char settings_forget_code(unsigned char kind)
{
        return kind == SETTINGS_NETWORK ? 'f' : kind == SETTINGS_DEVICE ? 'd' : kind == SETTINGS_LINK ? 'r' : 0;
}

static fn settings_dismiss_notice(unsigned int code)
{
        if (windows_issue_kernel(code))
                windows_kernel_dismiss(settings.own.table, 1u << (code - WINDOWS_ISSUE_KERNEL_GPU));
}

// Delete twice on a saved network, kept device, or linked machine forgets/removes it.
static fn settings_forget(void)
{
        const struct settings_item address_to item = settings_item(settings.list.focus);
        char code = item && (item->flags & SETTINGS_ITEM_SAVED) ? settings_forget_code(item->kind) : 0;
        char text[SETTINGS_STATUS];

        if (!code)
                return;
        if (!ui_confirm_ask(&settings.confirm, -1 - (int)item->kind, item->label))
        {
                language_format(text, sizeof(text), WORDS(settings_forget), item->label);
                settings_confirm_say(text);
                return;
        }
        settings_act(code, item->label, null);
}

// A row changed one way or the other: a switch thrown, a choice or a step moved, a field opened, an action run.
static fn settings_row_activate(int row, int offset, int way)
{
        const struct settings_row address_to which = &settings_rows[offset];
        char next[SETTINGS_VALUE];

        // A press on a pairing's row while it waits stops the wait: SIGTERM, which the CLI answers by closing the code and saying so.
        if (settings_pair_waiting(row))
        {
                system_call_2(syscall(kill), (positive)settings.child, SIGTERM);
                return;
        }
        // A row the machine has none of takes no choice: its status says so, where a press on it would otherwise look dead.
        if (!which->output && settings.state[row] == SETTINGS_ABSENT)
        {
                settings_say((string_address)WORDS(settings_not_here));
                return;
        }
        if (!which->output && settings.state[row] == SETTINGS_UNREAD)
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
        case SETTINGS_SLIDER:
        {
                // Left and Right (and Enter, which is a step on) move it a step, held inside its range; the pointer's is the slider's (settings_slid).
                long number = 0, to;

                // A value not read as a number (or not read at all) is not stepped: the step would start from 0 and write the row's least.
                if (settings.state[row] != SETTINGS_READ || !setting_typed_number(which, (string_address)settings.value[row], &number))
                        return;
                to = number + way * settings_slider_step(which);
                to = to < which->low ? which->low : to > which->high ? which->high : to;
                if (to != number)
                {
                        char word[SETTINGS_VALUE];

                        positive_into_string((p8 address_to)word, (positive)to);
                        settings_change(row, word);
                }
                return;
        }
        case SETTINGS_ACTION:
                // A set asked for while one is running or waiting is that set: the clock is set once for it.
                if (settings_action_busy(row))
                        return;
                settings_change(row, "");
                return;
        }

        settings_next(which, settings.value[row], way, next, sizeof(next));
        if (next[0] && !string_equals((string_address)next, (string_address)settings.value[row]))
                settings_change(row, next);
}

static fn settings_activate(int row, int way)
{
        int offset;

        switch (settings_domain_of(row, &offset))
        {
        case SETTINGS_DOM_ROW:
                settings_row_activate(row, offset, way);
                break;
        case SETTINGS_DOM_COMMAND:
                if (settings_takes(row))
                        settings_edit_begin(row, "", false);
                else
                        settings_run(row, "");
                break;
        case SETTINGS_DOM_LIST:
                if (settings_item(row))
                        settings_item_activate(row, settings_item(row));
                break;
        case SETTINGS_DOM_SUB:
                if (settings_partitions_owner(row) >= 0)
                        settings_toggle(settings_partitions_owner(row)), ui_page_redraw(&settings.page);
                break;
        case SETTINGS_DOM_TRY:
                if (offset == SETTINGS_TRY_REPEAT)
                        settings_edit_begin(row, "", false);
                break;
        case SETTINGS_DOM_ABOUT:
                settings_about_press(row);
                ui_page_redraw(&settings.page);
                break;
        }
}

// The status as the keys come to a row from another: a command says what it does, and leaving one clears it.
static fn settings_said_of(int before)
{
        int dom = settings_domain_of(settings.list.focus, null);

        if (dom == SETTINGS_DOM_COMMAND || dom == SETTINGS_DOM_LIST)
                settings_say((string_address)settings_text(settings.list.focus));
        else if (dom >= SETTINGS_DOM_SUB || settings_domain_of(before, null) > SETTINGS_DOM_ROW)
                settings_say((string_address) "");
}

static fn settings_filter_clear(void);
static b32 settings_filter_typed(void);

static fn settings_group_choose(int group)
{
        int last = settings_groups() - 1, before = settings.page.chosen;
        b32 typed = settings_filter_length() != 0 || settings_filter_typed();

        group = group < 0 ? 0 : group > last ? last : group;
        if (group == before && !typed)
                return;
        settings.page.chosen = group, settings.map.keys = false, settings.feed_due = 0;
        settings_filter_clear();
        ui_scroll_set(&settings.list.scroll, 0);
        ui_confirm_drop(&settings.confirm);
        settings_refilter();
        if (settings.list.count)
                settings.list.focus = settings.list.id[0];
        ui_page_column_show(&settings.ui, &settings.page);
        settings_say_redraw(settings.list.focus >= SETTINGS_ANY ? settings_text(settings.list.focus) : "");
}

static fn settings_edit_commit(void)
{
        char typed[SETTINGS_TYPED];
        positive length = ui_text_string(&settings.edit, (p8 address_to)typed, sizeof(typed)), start = 0;
        int row = settings_editing();

        while (!settings.edit.masked && start < length && typed[start] == ' ')
                start++;
        while (!settings.edit.masked && length > start && typed[length - 1] == ' ')
                length--;
        typed[length] = 0, length -= start;
        switch (settings_domain_of(row, null))
        {
        case SETTINGS_DOM_TRY:
                memory_zero(typed, sizeof(typed));
                break;
        case SETTINGS_DOM_LIST:
        {
                const struct settings_item address_to item = settings_item(row);

                if (!length)
                        settings_say((string_address)WORDS(settings_nothing_typed));
                else if (item && item->kind == SETTINGS_NETWORK)
                        settings_act('j', item->label, typed + start);
                else if (item && item->kind == SETTINGS_ADD)
                        settings_act('a', typed + start, null);
                memory_zero(typed, sizeof(typed));
                break;
        }
        case SETTINGS_DOM_COMMAND:
                if (!settings_run(row, typed + start))
                        return;
                break;
        case SETTINGS_DOM_ROW:
                if (!length)
                        settings_say((string_address)WORDS(settings_nothing_kept));
                else if (!string_equals((string_address)typed + start, (string_address)settings.edit_from) &&
                         !string_equals((string_address)typed + start, (string_address)settings.value[row]))
                        settings_change(row, typed + start);
                break;
        }
        settings_edit_end();
}

static fn settings_field_menu(void);

static fn settings_edit_asked(p32 asks)
{
        if (asks & UI_TEXT_MENU)
                settings_field_menu();
        if (asks & UI_TEXT_REFUSED)
                settings_say((string_address)settings.edit.refused);
        if (asks & (UI_TEXT_SUBMIT | UI_TEXT_COMMIT))
                settings_edit_commit();
        else if (asks & UI_TEXT_CANCEL)
                settings_edit_end();
        else if (asks)
                ui_page_redraw(&settings.page);
}

static fn settings_type(const struct window_event address_to event)
{
        if (event->a != UI_KEY_ENTER && event->a != UI_KEY_ENTER_PAD)
                ui_confirm_drop(&settings.confirm);
        ui_field_focus(&settings.edit, true);
        settings_edit_asked(ui_field_event(&settings.ui, &settings.edit, null, event, 0, 0));
}

static struct ui_text address_to settings_filter_field(void)
{
        if (!settings.filter_open)
        {
                ui_field_open(&settings.filter_edit, &settings.own, "");
                settings.filter_edit.most = SETTINGS_FILTER - 1, settings.filter_edit.keeps_vertical = true;
                settings.filter_open = true;
        }
        return &settings.filter_edit;
}

static b32 settings_filter_typed(void) { return settings.filter_open && doc_length(&settings.filter_edit.doc) != 0; }

static fn settings_filter_clear(void)
{
        if (settings.filter_open)
                ui_text_set(&settings.filter_edit, (const p8 address_to) "", 0), ui_text_search_changed(&settings.filter_edit);
}

static fn settings_filter_asked(p32 asks)
{
        if (asks & UI_TEXT_REFUSED)
                settings_say((string_address)settings.filter_edit.refused);
        if (!asks || (asks & (UI_TEXT_SUBMIT | UI_TEXT_CANCEL)))
                return;
        if (!ui_text_search_changed(settings_filter_field()))
        {
                ui_page_redraw(&settings.page);
                return;
        }
        ui_scroll_set(&settings.list.scroll, 0);
        settings.page.pane = 1;
        settings_refilter();
        if (settings.list.count)
                settings.list.focus = settings.list.id[0], ui_page_reveal(&settings.ui, &settings.page, &settings.list);
}

static p32 settings_filter_key(const struct window_event address_to event)
{
        struct ui_text address_to field = settings_filter_field();
        p32 asks;

        ui_field_focus(field, true);
        asks = ui_field_event(&settings.ui, field, &settings.popup, event, 0, 0);
        settings_filter_asked(asks);
        return asks;
}

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

static b32 settings_steps(int row)
{
        return row < SETTINGS_ANY && (settings_rows[row].kind == SETTINGS_STEP || settings_rows[row].kind == SETTINGS_SLIDER);
}

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
        case UI_KEY_ESCAPE: case UI_KEY_ENTER: case UI_KEY_ENTER_PAD: case UI_KEY_SPACE: case UI_KEY_BACKSPACE:
                settings_card_leave();
                break;
        }
}

// Format full desk layout (normalised to 0,0) as "NAME place X Y ..." into value.
static positive settings_map_layout(int at, int x, int y, char address_to value, positive room)
{
        int low_x = x, low_y = y, count = settings.map.graph.nodes;
        const struct ui_graph_node address_to node = settings.map.node;
        positive length = 0;

        if (at < 0 || !settings.own.table || at >= count || !room)
                return 0;
        for (int k = 0; k < count; k++)
                low_x = k != at && node[k].x < low_x ? node[k].x : low_x, low_y = k != at && node[k].y < low_y ? node[k].y : low_y;
        for (int k = 0; k < count; k++)
        {
                settings.map.sent_x[k] = (k == at ? x : node[k].x) - low_x, settings.map.sent_y[k] = (k == at ? y : node[k].y) - low_y;
                length = settings_map_add(value, room, length, k ? " " : "");
                length = settings_map_add(value, room, length, settings.own.table->output_name[k]);
                length = settings_map_add(value, room, length, " place ");
                length = settings_map_number(value, room, length, settings.map.sent_x[k]);
                length = settings_map_add(value, room, length, " ");
                length = settings_map_number(value, room, length, settings.map.sent_y[k]);
        }
        return length + 1 < room ? length : 0;
}

static void settings_map_place(int at, int x, int y)
{
        int row = settings_output_row(SETTINGS_OUT_TURN);
        char value[SETTINGS_TYPED];

        if (row < 0 || !settings_map_layout(at, x, y, value, sizeof(value)))
                return;
        settings.map.sent = true, settings.map.sent_at = (positive)ui_kit.now, settings.map.sent_count = (unsigned int)settings.map.graph.nodes;
        if (at < (int)settings.map.shown)
                settings.map.shown_x[at] = x, settings.map.shown_y[at] = y;
        settings_change(row, value);
}

#define SETTINGS_MAP_STEP 10

static b32 settings_map_stepped(int dx, int dy, int *x, int *y)
{
        unsigned int number[WINDOWS_OUTPUTS] = {0}, count = settings_map_load(number);
        int at = settings.map.display;

        if (count < 2 || at < 0 || at >= (int)count)
                return false;
        *x = settings.map.node[at].x + dx * SETTINGS_MAP_STEP, *y = settings.map.node[at].y + dy * SETTINGS_MAP_STEP;
        settings_map_frame();
        ui_graph_settle(&settings.ui, &settings.map.graph, at, x, y, false);
        return *x != settings.map.node[at].x || *y != settings.map.node[at].y;
}

static fn settings_map_damage(void) { ui_page_damage(&settings.page, settings.map.panel.y, settings.map.panel.y + settings.map.panel.h); }

static b32 settings_map_key(const struct window_event address_to event)
{
        unsigned int number[WINDOWS_OUTPUTS] = {0}, count = settings_map_load(number), did;
        int at = settings.map.display, dx = (event->a == UI_KEY_RIGHT) - (event->a == UI_KEY_LEFT), dy = (event->a == UI_KEY_DOWN) - (event->a == UI_KEY_UP), x, y;

        if (event->a == UI_KEY_ENTER || event->a == UI_KEY_ENTER_PAD)
        {
                settings_change(SETTINGS_JOB_IDENTIFY, "");
                return true;
        }
        if (!dx && !dy)
        {
                settings_map_frame();
                did = ui_graph_key(&settings.ui, &settings.map.graph, event);
                if (did & UI_GRAPH_DRAW)
                        settings_map_damage();
                if (did & UI_GRAPH_TAKEN)
                        return true;
                settings.map.keys = false;
                return false;
        }
        if (event->flags & WINDOW_KEY_SHIFT)
        {
                if (settings_map_stepped(dx, dy, &x, &y))
                        settings_map_place(at, x, y);
        }
        else if (dy > 0)
                settings.map.keys = false;
        else if (count && at >= 0 && at < (int)count)
        {
                int wanted = (int)number[at] + (dx < 0 || dy < 0 ? -1 : 1);
                int hit = settings_map_numbered(number, count, (unsigned int)(wanted < 1 ? 1 : wanted > (int)count ? (int)count : wanted));

                if (hit >= 0)
                        settings.map.display = hit;
        }
        ui_page_redraw(&settings.page);
        return true;
}

static const struct ui_page_keys settings_page_keys = {settings_steps, settings_activate, settings_said_of, settings_group_choose, settings_forget};

static b32 settings_key(const struct window_event address_to event)
{
        if (!(event->flags & WINDOW_KEY_DOWN))
        {
                ui_graph_key(&settings.ui, &settings.map.graph, event);
                return true;
        }
        if (settings.page.menu_row >= 0 || (settings.popup.open && settings.popup.list && !settings.popup.field))
        {
                settings_menu_answer(ui_popmenu_event(&settings.ui, &settings.popup.menu, event, 0, 0));
                return true;
        }
        if (settings_editing() >= 0 && !settings_edit_shown())
        {
                if (settings.edit.cancels)
                        settings_edit_end();
                else
                        settings_edit_commit();
                ui_page_redraw(&settings.page);
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
        if (ui_page_context_key(&settings.ui, &settings.page, &settings.list, event))
                return true;
        if (event->a == UI_KEY_DELETE && !settings_filter_typed() && settings.list.focus >= (int)SETTINGS_ALL && settings.list.focus - (int)SETTINGS_ALL < settings_sub_count)
        {
                settings_dismiss_notice(settings_sub[settings.list.focus - (int)SETTINGS_ALL].code);
                return true;
        }
        if ((event->a == UI_KEY_RIGHT || event->a == UI_KEY_LEFT) && settings.list.focus >= 0 && settings_editing() < 0 && !settings_filter_typed())
        {
                int drive = settings_partitions_owner(settings.list.focus), row = drive >= 0 ? drive : settings.list.focus;
                int expander = drive >= 0 ? (settings_expanded[drive] ? UI_ROW_EXPANDER_OPEN : UI_ROW_EXPANDER_SHUT)
                               : row < SETTINGS_ANY ? settings_expander(row) : 0;

                if (expander && (event->a == UI_KEY_RIGHT) == (expander == UI_ROW_EXPANDER_SHUT))
                {
                        settings_toggle(row);
                        ui_page_redraw(&settings.page);
                        return true;
                }
        }
        if (settings.map.keys)
        {
                if (settings_map_on() && settings.page.pane && settings_map_key(event))
                        return true;
                settings.map.keys = false;
        }
        if (settings.confirm.what < 0 ? event->a != UI_KEY_DELETE
                                      : event->a != UI_KEY_ENTER && event->a != UI_KEY_ENTER_PAD && event->a != UI_KEY_SPACE)
                ui_confirm_drop(&settings.confirm);
        if (settings_filter_typed() || settings_filter_starts(event))
        {
                p32 asks = settings_filter_key(event);

                if (asks & UI_TEXT_CANCEL)
                {
                        settings_filter_clear(), settings_refilter();
                        return true;
                }
                if ((asks & UI_TEXT_SUBMIT) && settings_pos(settings.list.focus) >= 0)
                        settings.page.pane = 1, settings_activate(settings.list.focus, 1);
                if (asks)
                        return true;
        }
        if (event->a == UI_KEY_ESCAPE)
                return settings_stop();
        if (settings.page.pane && event->a == UI_KEY_UP && settings_map_on() && settings.list.focus == settings_output_row(SETTINGS_OUT_SCREEN) &&
            settings_map_count())
        {
                settings.map.keys = true, ui_page_redraw(&settings.page);
                return true;
        }
        ui_page_key(&settings.ui, &settings.page, &settings.list, (int)event->a, &settings_page_keys);
        return true;
}

/* ---- the rows, as the kit draws them ---- */

static b32 settings_row_is(int at, const char address_to path)
{
        return at >= 0 && at < (int)SETTINGS_ROWS && settings_rows[at].path && string_equals((string_address)settings_rows[at].path, (string_address)path);
}

// Whether a choice row renders as inline segmented tabs (<= 3 single-word labels, not dynamic/ink/language).
static b32 settings_is_tabs(int row)
{
        const struct settings_row address_to which = &settings_rows[row];
        char each[SETTINGS_VALUE];
        positive count = 0;

        if (which->kind != SETTINGS_CHOICE || which->output != SETTINGS_OUT_NONE)
                return false;
        if (which->flags & (SETTINGS_LANGUAGE | SETTINGS_INK | SETTINGS_BATTERIES | SETTINGS_TRANSPORT))
                return false;
        for (; settings_word(which->choices, count, each, sizeof(each)); count++)
        {
                const char address_to said = settings_label_of(row, each);

                if (count == 3)
                        return false;
                for (const char address_to letter = said; *letter; letter++)
                        if (*letter == ' ')
                                return false;
        }
        return count > 0;
}

static positive settings_word_append(char address_to into, positive room, positive at, const char address_to word)
{
        positive len = string_length((string_address)word);

        if (at + len + 2 >= room)
                return at;
        if (at)
                into[at++] = ' ';
        string_copy_bounded((string_address)into + at, (string_address)word, room - at);
        return at + len;
}

static const char address_to settings_tab_words(int row)
{
        static char shown[SETTINGS_VALUE];
        char each[SETTINGS_VALUE];
        positive at = 0;

        shown[0] = 0;
        for (positive k = 0; settings_word(settings_rows[row].choices, k, each, sizeof(each)); k++)
                at = settings_word_append(shown, sizeof(shown), at, settings_label_of(row, each));
        return shown;
}

static const char address_to settings_port_words(void)
{
        static char shown[SETTINGS_VALUE];
        positive at = settings_word_append(shown, sizeof(shown), 0, language_of("allow"));

        settings_word_append(shown, sizeof(shown), at, language_of("deny"));
        return shown;
}

/*
        What a typed number row shows as its value: the number and its unit the way the verb says it (30 per second, 100%), so a
        number typed as 100 percent and one the verb printed read the same. A slider shows its number on its own (the kit draws
        the unit after it), and a word that is no number is shown as it is.
*/
static const char address_to settings_number_shown(int row, const char address_to word)
{
        static char shown[SETTINGS_VALUE];
        const struct settings_row address_to which = &settings_rows[row];
        long number = 0;
        positive length;

        if (which->type != SETTING_NUMBER || which->kind != SETTINGS_FIELD || !setting_typed_number(which, (string_address)word, &number))
                return settings_label_of(row, word);
        positive_into_string((p8 address_to)shown, (positive)number);
        length = string_length((string_address)shown);
        if (setting_percent(which))
                string_copy_bounded((string_address)shown + length, (string_address) "%", sizeof(shown) - length);
        else if (which->unit)
        {
                string_copy_bounded((string_address)shown + length, (string_address) " ", sizeof(shown) - length);
                string_copy_bounded((string_address)shown + length + 1, (string_address)which->unit, sizeof(shown) - length - 1);
        }
        return shown;
}

static const char address_to settings_state_text(int id, unsigned int state, const char address_to shown)
{
        return state == SETTINGS_UNREAD ? "..." : state == SETTINGS_ABSENT ? WORDS(settings_not_here)
               : state == SETTINGS_FAILED || !shown[0] ? WORDS(settings_unknown) : settings_number_shown(id, shown);
}

static fn settings_describe_item(int id, const struct settings_item address_to item, struct ui_row address_to row)
{
        int list = settings_list_of(id);

        if (item->kind == SETTINGS_NETWORK)
        {
                row->columns = settings_columns_for(0);
                row->mark = (item->flags & SETTINGS_ITEM_JOINED) != 0;
                row->note = item->flags & SETTINGS_ITEM_JOINED ? WORDS(settings_joined) : item->flags & SETTINGS_ITEM_SAVED ? WORDS(settings_saved) : null;
                row->value = item->security[0] ? item->security : null;
                row->bars = item->flags & SETTINGS_ITEM_HEARD ? item->bars : -1;
                row->dim = !(item->flags & SETTINGS_ITEM_HEARD);
                row->cells[1] = row->note, row->cells[2] = row->value;
        }
        else if (item->kind == SETTINGS_DEVICE || item->kind == SETTINGS_ADD)
        {
                row->columns = settings_columns_for(1);
                if (item->kind == SETTINGS_DEVICE)
                        row->value = row->cells[1] = WORDS(settings_kept);
        }
        else if (item->kind == SETTINGS_PORT)
        {
                b32 waiting = string_equals((string_address)item->state, (string_address) "waiting");
                const char address_to state_w = item->state[0] ? settings_state_word(item->state) : null;

                row->columns = settings_columns_for(2);
                row->kind = UI_ROW_TABS, row->words = settings_port_words();
                row->at = item->flags & SETTINGS_ITEM_JOINED ? 0 : 1;
                if (item->flags & SETTINGS_ITEM_DISK)
                {
                        language_format(settings_device_note, sizeof(settings_device_note), WORDS(settings_device_usb_drive), item->port);
                        string_copy_bounded((string_address)settings_device_port_cell, (string_address)settings_device_note, sizeof(settings_device_port_cell));
                        settings_drive_strip(item, row);
                }
                else if (item->flags & SETTINGS_ITEM_USB)
                {
                        language_format(settings_device_port_cell, sizeof(settings_device_port_cell), WORDS(settings_device_usb_port), item->port);
                        if (waiting)
                                language_format(settings_device_note, sizeof(settings_device_note), WORDS(settings_device_usb), item->port, state_w);
                        else
                                string_copy_bounded((string_address)settings_device_note, (string_address)settings_device_port_cell, sizeof(settings_device_note));
                }
                else
                {
                        string_copy_bounded((string_address)settings_device_note, (string_address)(waiting ? state_w : ""), sizeof(settings_device_note));
                        string_copy_bounded((string_address)settings_device_port_cell, (string_address)item->port, sizeof(settings_device_port_cell));
                }
                row->note = settings_device_note[0] ? settings_device_note : null;
                row->cells[1] = settings_device_port_cell[0] ? settings_device_port_cell : null;
                row->cells[2] = state_w;
        }
        else if (item->flags & SETTINGS_ITEM_DRIVE)
        {
                language_format(settings_device_note, sizeof(settings_device_note),
                                item->where == SETTINGS_WHERE_SD ? WORDS(settings_device_sd) : WORDS(settings_device_drive), settings_state_word(item->state));
                row->note = settings_device_note;
                row->value = item->segment_count ? null : item->size;
                if (settings_device_section(item) == 1)
                {
                        row->columns = settings_columns_for(2);
                        row->cells[1] = settings_device_note;
                        row->cells[2] = item->state[0] ? settings_state_word(item->state) : null;
                        row->cells[3] = row->value;
                }
                settings_drive_strip(item, row);
        }
        else if (item->kind == SETTINGS_LINK)
        {
                row->columns = settings_columns_for(3);
                row->cells[1] = item->access[0] ? item->access : null;
                row->cells[2] = item->detail[0] ? item->detail : null;
                row->value = row->cells[2] ? row->cells[2] : row->cells[1];
        }
        else if (item->kind == SETTINGS_NOTE)
        {
                if (((list == 0 || list == 3) && settings.item_count[list] > 1) || (list == 2 && settings_device_section(item) == 1))
                        row->columns = settings_columns_for(list);
                row->dim = true;
        }
}

static fn settings_describe(int id, struct ui_row address_to row)
{
        int offset, dom = settings_domain_of(id, &offset);
        const struct settings_row address_to which = dom == SETTINGS_DOM_ROW ? &settings_rows[offset] : null;
        const struct settings_item address_to item = dom == SETTINGS_DOM_LIST ? settings_item(id) : null;
        unsigned int state = which ? settings.state[offset] : SETTINGS_READ;
        const char address_to shown = which ? settings.value[offset] : "";
        const char address_to header;

        if (dom < 0)
                return;
        if (dom == SETTINGS_DOM_SUB)
        {
                if (offset < settings_sub_count)
                        settings_sub_describe(offset, row);
                return;
        }
        if (dom == SETTINGS_DOM_TRY)
                return settings_try_describe(id, row);
        if (dom == SETTINGS_DOM_ABOUT)
                return settings_about_describe(id, row);
        row->section = settings_section_start(id);
        header = settings_header(id);
        row->header = header && !header[0] ? null : language_of(header);
        row->label = settings_label_window(id);
        if (which)
        {
                row->about = language_of(which->about);
                settings_badge(id, row);
                row->expander = settings_expander(id), row->turn = settings_fold_at[id];
        }
        if (id == settings_editing())
        {
                const struct settings_generic address_to generic = settings_generic_of(id);

                row->kind = UI_ROW_FIELD, row->edit = &settings.edit;
                if (item)
                        row->label = item->kind == SETTINGS_ADD ? WORDS(settings_device_name) : settings.joining;
                else if (!which)
                {
                        const char address_to hint = generic ? settings_large->pool + generic->command + generic->verb : WORDS(settings_verb_hint);

                        if (generic)
                                settings_item_word(settings.head, sizeof(settings.head), settings_large->pool + generic->command, generic->verb);
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
                settings_describe_item(id, item, row);
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
        if (which->kind == SETTINGS_ACTION)
        {
                row->kind = UI_ROW_BUTTON, row->words = settings_button_words(id);
                row->dim = state == SETTINGS_UNREAD || state == SETTINGS_ABSENT || (settings_action_busy(id) && !settings_pair_waiting(id));
                row->note = which->read[0] ? settings_state_text(id, state, shown) : null;
                return;
        }
        row->dim = state == SETTINGS_UNREAD || state == SETTINGS_ABSENT;
        row->value = settings_state_text(id, state, shown);
        if (state != SETTINGS_READ)
                return;
        switch (which->kind)
        {
        case SETTINGS_SWITCH:
                row->kind = UI_ROW_SWITCH, row->on = string_equals((string_address)shown, (string_address) "on");
                break;
        case SETTINGS_CHOICE:
                if (settings_is_tabs(id) && setting_place(which->choices, (string_address)shown) >= 0)
                        row->kind = UI_ROW_TABS, row->words = settings_tab_words(id), row->at = (int)setting_place(which->choices, (string_address)shown);
                else if (settings_row_is(id, SETTING_ACCENT_PATH))
                {
                        const struct settings_ink address_to ink = settings_ink_find(which, shown, false);

                        row->kind = UI_ROW_SWATCHES, row->words = settings.accents;
                        if (ink)
                                row->at = (int)(ink - settings_accents);
                }
                else
                        row->kind = UI_ROW_CHOICE;
                break;
        case SETTINGS_STEP:
                row->kind = UI_ROW_STEP;
                break;
        case SETTINGS_SLIDER:
        {
                long number = 0;

                setting_typed_number(which, (string_address)shown, &number);
                number = number < which->low ? which->low : number > which->high ? which->high : number;
                row->kind = UI_ROW_SLIDER, row->at = (int)number, row->low = (int)which->low, row->high = (int)which->high;
                row->step = settings_slider_step(which), row->words = setting_percent(which) ? "%" : language_of(which->unit);
                break;
        }
        }
}

// Row context menu ids (extending ui_list_menu_items).
#define SETTINGS_COPY_VALUE UI_LIST_MENU_COPY
#define SETTINGS_COPY_BOTH UI_LIST_MENU_COPY_ROW
#define SETTINGS_MENU_REMOVE UI_LIST_MENU_REMOVE
#define SETTINGS_MENU_ACT 0x103 // row-specific action (Join network, Allow/Deny port)
#define SETTINGS_KEY_MENU UI_KEY_MENU
#define SETTINGS_KEY_F10 UI_KEY_F10

// Primary copy text for Copy / Copy value (null when nothing to copy).
static const char address_to settings_copy_text(int id)
{
        int offset, dom = settings_domain_of(id, &offset);

        if (dom < 0 || (dom != SETTINGS_DOM_ABOUT && !settings_large))
                return null;
        switch (dom)
        {
        case SETTINGS_DOM_ROW:
        {
                struct ui_row row;

                if (settings_rows[offset].output == SETTINGS_OUT_NONE)
                        return settings.state[offset] == SETTINGS_READ && settings.value[offset][0] ? settings_number_shown(offset, settings.value[offset]) : null;
                memory_zero(&row, sizeof(row));
                settings_describe(id, &row);
                return !row.dim && row.value && row.value[0] ? row.value : null;
        }
        case SETTINGS_DOM_COMMAND:
                return offset > 0 ? settings_label_shown(id) : null;
        case SETTINGS_DOM_LIST:
        {
                const struct settings_item address_to item = settings_item(id);

                return item && item->kind != SETTINGS_ADD && item->label[0] ? item->label : null;
        }
        case SETTINGS_DOM_SUB:
                if (offset >= settings_sub_count)
                        return null;
                if (settings_sub[offset].action == SETTINGS_SUB_COPY)
                        return settings_sub[offset].text[0] ? settings_sub[offset].text : null;
                if (settings_sub[offset].action == SETTINGS_SUB_PARTITION)
                {
                        struct ui_row row;

                        memory_zero(&row, sizeof(row));
                        settings_describe(id, &row);
                        return row.label && row.label[0] ? row.label : null;
                }
                return null;
        case SETTINGS_DOM_ABOUT:
                return settings_about_url(offset);
        default:
                return null;
        }
}

// Base ui_list_menu_items flags for row id (0 when the row offers no context menu).
static p32 settings_row_menu_flags(int id)
{
        int offset, dom = settings_domain_of(id, &offset);
        b32 has = dom >= 0 && settings_copy_text(id) != null;

        switch (dom)
        {
        case SETTINGS_DOM_ROW:
        case SETTINGS_DOM_ABOUT:
                return UI_LIST_MENU_VALUE_WORDS | UI_LIST_MENU_HAS_COPY_ROW | (has ? UI_LIST_MENU_HAS_COPY : 0);
        case SETTINGS_DOM_COMMAND:
                return offset > 0 && has ? UI_LIST_MENU_HAS_COPY | UI_LIST_MENU_HAS_COPY_ROW : 0;
        case SETTINGS_DOM_LIST:
        {
                const struct settings_item address_to item = settings_item(id);
                p32 copy = has ? UI_LIST_MENU_HAS_COPY | UI_LIST_MENU_HAS_COPY_ROW : 0;

                if (!item || item->kind == SETTINGS_ADD)
                        return 0;
                if (item->kind == SETTINGS_NOTE)
                        return has ? UI_LIST_MENU_HAS_COPY : 0;
                if (item->kind == SETTINGS_NETWORK)
                        return copy | ((item->flags & SETTINGS_ITEM_SAVED) ? UI_LIST_MENU_CAN_REMOVE : 0);
                return copy | (item->kind == SETTINGS_DEVICE || item->kind == SETTINGS_LINK ? UI_LIST_MENU_CAN_REMOVE : 0);
        }
        case SETTINGS_DOM_SUB:
                if (offset >= settings_sub_count)
                        return 0;
                if (settings_sub[offset].action == SETTINGS_SUB_COPY)
                        return (has ? UI_LIST_MENU_HAS_COPY : 0) | (windows_issue_kernel(settings_sub[offset].code) ? UI_LIST_MENU_CAN_REMOVE : 0);
                return settings_sub[offset].action == SETTINGS_SUB_PARTITION && has ? UI_LIST_MENU_HAS_COPY | UI_LIST_MENU_HAS_COPY_ROW : 0;
        default:
                return 0;
        }
}

static fn settings_row_menu_extra(struct ui_popmenu address_to m, int id)
{
        const struct settings_item address_to item = settings_item(id);
        const char address_to label =
            !item ? null
            : item->kind == SETTINGS_NETWORK && !(item->flags & (SETTINGS_ITEM_JOINED | SETTINGS_ITEM_CANNOT)) ? WORDS(installer_wifi_join)
            : item->kind == SETTINGS_PORT ? ((item->flags & SETTINGS_ITEM_JOINED) ? WORDS(settings_device_deny) : WORDS(mixer_allow)) : null;

        if (label)
                ui_popmenu_add(m, label, null, 0, SETTINGS_MENU_ACT), ui_popmenu_add(m, null, null, UI_MENU_SEPARATOR, 0);
}

static fn settings_copy_append(char address_to into, positive room, const char address_to sep, const char address_to part)
{
        if (!part || !part[0])
                return;
        if (into[0])
                string_append_bounded((string_address)into, (string_address)sep, room);
        string_append_bounded((string_address)into, (string_address)part, room);
}

static fn settings_copy_row_text(int id, char address_to into, positive room)
{
        int offset, dom = settings_domain_of(id, &offset);
        const char address_to value = settings_copy_text(id), address_to second = null;
        char summary[SETTINGS_SUB_TEXT] = {0};

        into[0] = 0;
        if (!value)
                return;
        if (dom == SETTINGS_DOM_ROW || dom == SETTINGS_DOM_ABOUT)
        {
                language_format(into, room, WORDS(settings_copy_line), dom == SETTINGS_DOM_ROW ? settings_label_window(id) : settings_label_shown(id), value);
                return;
        }
        if (dom == SETTINGS_DOM_COMMAND && offset > 0)
                second = settings_text(id);
        else if (dom == SETTINGS_DOM_LIST || (dom == SETTINGS_DOM_SUB && offset < settings_sub_count && settings_sub[offset].action == SETTINGS_SUB_PARTITION))
        {
                struct ui_row row;

                memory_zero(&row, sizeof(row));
                settings_describe(id, &row);
                if (dom == SETTINGS_DOM_SUB)
                        settings_copy_append(summary, sizeof(summary), " ", row.amount), settings_copy_append(summary, sizeof(summary), " ", row.share);
                else
                {
                        const struct settings_item address_to item = settings_item(id);

                        if (item && item->kind == SETTINGS_PORT)
                                settings_copy_append(summary, sizeof(summary), ", ", row.cells[1]), settings_copy_append(summary, sizeof(summary), ", ", row.cells[2]);
                        else
                                settings_copy_append(summary, sizeof(summary), ", ", row.note),
                                settings_copy_append(summary, sizeof(summary), ", ",
                                                     row.value ? row.value : (item && (item->flags & SETTINGS_ITEM_DRIVE) ? item->size : null));
                }
                second = summary;
        }
        if (second && second[0])
                language_format(into, room, WORDS(settings_copy_line), value, second);
        else
                string_copy_bounded((string_address)into, (string_address)value, room);
}

static fn settings_row_copied(int id) { (void)id; settings_say((string_address)WORDS(settings_copied)); }

static fn settings_row_before_menu(int before, int row)
{
        settings_menu_close();
        if (row != before)
                ui_confirm_drop(&settings.confirm), settings_said_of(before);
}

static fn settings_row_menu_act(int id, int item_id)
{
        if (item_id == SETTINGS_MENU_REMOVE)
        {
                int offset, dom = settings_domain_of(id, &offset);

                if (dom == SETTINGS_DOM_LIST)
                {
                        const struct settings_item address_to item = settings_item(id);
                        char code = item && (item->flags & SETTINGS_ITEM_SAVED) ? settings_forget_code(item->kind) : 0;

                        if (code)
                                settings_act(code, item->label, null);
                }
                else if (dom == SETTINGS_DOM_SUB && offset < settings_sub_count)
                        settings_dismiss_notice(settings_sub[offset].code);
        }
        else if (item_id == SETTINGS_MENU_ACT)
        {
                const struct settings_item address_to item = settings_item(id);

                if (item && item->kind == SETTINGS_NETWORK)
                        settings_item_activate(id, item);
                else if (item && item->kind == SETTINGS_PORT)
                        settings_port_set(item, !(item->flags & SETTINGS_ITEM_JOINED));
        }
}

static const struct ui_list_menu settings_list_menu = {
        .flags = settings_row_menu_flags,
        .copy = settings_copy_text,
        .copy_row = settings_copy_row_text,
        .extra = settings_row_menu_extra,
        .act = settings_row_menu_act,
        .copied = settings_row_copied,
        .before_open = settings_row_before_menu,
};

// The menu of the field a row is typed in: the kit's items for its text, and after them the row's copies when it is a setting's.
static fn settings_field_menu(void)
{
        struct ui_text address_to t = &settings.edit;
        struct ui_popmenu address_to m = &settings.popup.menu;
        int row = settings_editing();
        b32 copies = row >= 0 && row < SETTINGS_ANY;

        if (!t->own)
                return;
        m->count = 0, m->typed_length = 0;
        ui_text_menu_items(m, t, false);
        settings.popup.list = null, settings.popup.list_row = -1;
        if (copies)
                ui_list_menu_attach(&settings.popup, &settings.list, row);
        settings.popup.field = t;
        if (!ui_popmenu_open(&settings.popup, t->own, &settings.ui, t->menu_x, t->menu_y, 0, 0, WINDOW_POPUP_POINT, ui_kit_touch))
                settings.popup.list = null, settings.popup.list_row = -1;
}

/* ---- the pointer and a finger: the kit's page (ui_page_pointer) ---- */

static b32 settings_map_event(const struct window_event address_to event)
{
        struct ui_graph address_to g = &settings.map.graph;
        unsigned int number[WINDOWS_OUTPUTS] = {0}, did;
        int x = ui_kit.pointer_x, y = ui_kit.pointer_y;
        const struct ui_rect address_to panel = &settings.map.panel;
        b32 inside = x >= panel->x && x < panel->x + panel->w && y >= panel->y && y < panel->y + panel->h, had = g->held >= 0 || g->panning;

        settings_map_load(number);
        settings_map_frame();
        if (event->type == WINDOW_EVENT_BUTTON && (event->flags & WINDOW_KEY_DOWN) && inside)
        {
                settings.map.keys = g->keys = false;
                if (event->a == UI_POINTER_LEFT && ui_control_hit(&settings.ui, &settings.map.identify, x, y) >= 0)
                {
                        settings_change(SETTINGS_JOB_IDENTIFY, "");
                        return true;
                }
                if (event->a == UI_POINTER_LEFT && ui_control_hit(&settings.ui, &settings.map.fit, x, y) >= 0)
                {
                        ui_graph_home(&settings.ui, g), settings_map_damage();
                        return true;
                }
        }
        did = ui_graph_event(&settings.ui, g, event);
        if (did & UI_GRAPH_CHOSE)
                settings.map.display = g->chosen;
        if (did & UI_GRAPH_DROP)
                settings_map_place(g->dropped, g->at_x, g->at_y);
        if ((did & (UI_GRAPH_CHOSE | UI_GRAPH_DROP)) || (event->type == WINDOW_EVENT_BUTTON && (did & UI_GRAPH_TAKEN)) ||
            ((did & UI_GRAPH_DRAG) && !had))
                ui_page_redraw(&settings.page);
        else if (did & UI_GRAPH_DRAW)
                settings_map_damage();
        return event->type == WINDOW_EVENT_MOTION ? had : (did & UI_GRAPH_TAKEN) != 0;
}

static fn settings_press_at(void);

static void settings_slid(void)
{
        int row = settings.list.slid - 1;
        char word[SETTINGS_VALUE];

        if (settings.list.slid <= 0)
                return;
        settings.list.slid = 0;
        if (row < 0 || row >= SETTINGS_ANY || settings_rows[row].kind != SETTINGS_SLIDER || settings.state[row] != SETTINGS_READ)
                return;
        positive_into_string((p8 address_to)word, (positive)settings.list.slid_value);
        if (!string_equals((string_address)word, (string_address)settings.value[row]))
                settings_change(row, word);
}

static b32 settings_event_take(const struct window_event address_to event)
{
        if (gallery.on)
        {
                b32 redraw = false, keep = gallery_event(&settings.ui, settings.width, settings.height, &settings.popup, event, &redraw);

                if (redraw && gallery.band_bottom > gallery.band_top)
                        ui_page_damage(&settings.page, gallery.band_top, gallery.band_bottom);
                else if (redraw)
                        ui_page_redraw(&settings.page);
                return keep;
        }
        if (settings_map_on() && settings.page.menu_row < 0 && event->type != WINDOW_EVENT_KEY)
        {
                if (settings_map_event(event))
                        return true;
        }
        else if (!settings_map_on())
                ui_graph_let_go(&settings.map.graph);
        if (event->type == WINDOW_EVENT_BUTTON && (event->flags & WINDOW_KEY_DOWN))
                settings.map.keys = false;
        if ((event->type == WINDOW_EVENT_MOTION || event->type == WINDOW_EVENT_BUTTON) &&
            (event->type != WINDOW_EVENT_BUTTON || event->a == UI_POINTER_LEFT) &&
            ui_page_bars(&settings.ui, &settings.page, (int)event->type, (event->flags & WINDOW_KEY_DOWN) != 0))
                return true;
        if (event->type != WINDOW_EVENT_KEY && event->type != WINDOW_EVENT_WHEEL && !settings.page.card && settings.page.menu_row < 0)
        {
                int x = ui_kit.pointer_x, y = ui_kit.pointer_y;
                p32 asks;

                if (settings_editing() >= 0 && (settings_edit_shown() || event->type != WINDOW_EVENT_MOTION || settings.edit.sel.dragging) &&
                    (asks = ui_field_event(&settings.ui, &settings.edit, null, event, x, y)))
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
                if (settings.filter_open && (asks = ui_field_event(&settings.ui, &settings.filter_edit, &settings.popup, event, x, y)) &&
                    (event->type != WINDOW_EVENT_BUTTON || (asks != UI_TEXT_COMMIT && asks != UI_TEXT_CANCEL)))
                {
                        settings_filter_asked(asks), ui_page_redraw(&settings.page);
                        return true;
                }
        }
        switch (event->type)
        {
        case WINDOW_EVENT_KEY:
                return settings_key(event);
        case WINDOW_EVENT_MOTION:
        case WINDOW_EVENT_BUTTON:
                ui_page_pointer(&settings.ui, &settings.page, &settings.list, event, settings_press_at);
                settings_slid();
                break;
        case WINDOW_EVENT_WHEEL:
                ui_page_wheel(&settings.ui, &settings.page, event);
                break;
        }
        return true;
}

static b32 settings_event(const struct window_event address_to event)
{
        b32 taken;

        settings.hand = ui_event_hand(event);
        taken = settings_event_take(event);
        settings.hand = false;
        return taken;
}

static fn settings_press_at(void)
{
        int x = ui_kit.pointer_x, y = ui_kit.pointer_y, row, before = settings.list.focus;

        if (settings.page.card)
                return settings_card_leave();
        row = ui_page_column_hit(&settings.ui, &settings.page, x, y);
        if (row >= 0)
        {
                settings_edit_end(), settings.page.pane = 0, settings_group_choose(row), ui_page_redraw(&settings.page);
                return;
        }
        row = ui_list_pointer(&settings.ui, &settings.list, WINDOW_EVENT_BUTTON, x, y, true);
        if (settings_editing() >= 0 && row != settings_editing())
                settings_edit_end();
        if (row < 0)
                return;
        {
                int offset, dom = settings_domain_of(row, &offset);

                if (dom == SETTINGS_DOM_SUB)
                {
                        if (settings_partitions_owner(row) >= 0)
                                settings_toggle(settings_partitions_owner(row));
                        else if (offset < settings_sub_count && settings_sub[offset].action == SETTINGS_SUB_DISMISS_ALL)
                                windows_kernel_dismiss(settings.own.table, KERNEL_NOTICE_ALL);
                        else if (offset < settings_sub_count && settings_sub[offset].action != SETTINGS_SUB_PARTITION)
                                settings_copy(offset);
                        ui_page_redraw(&settings.page);
                        return;
                }
                if (dom == SETTINGS_DOM_TRY || dom == SETTINGS_DOM_ABOUT)
                {
                        settings.page.pane = 1;
                        if (dom == SETTINGS_DOM_TRY)
                                settings_try_press(row, x, y);
                        else
                                settings_about_press(row);
                        ui_page_redraw(&settings.page);
                        return;
                }
        }
        if (row < SETTINGS_ANY && ui_list_expander_at(&settings.ui, &settings.list, row, x))
        {
                settings_toggle(row), ui_page_redraw(&settings.page);
                return;
        }
        if (row != before)
                ui_confirm_drop(&settings.confirm), settings_said_of(before);
        settings.page.pane = 1, ui_page_redraw(&settings.page);
        if (settings.list.part >= 0 && row < (int)SETTINGS_ALL && settings_item(row) && settings_item(row)->kind == SETTINGS_PORT)
        {
                const struct settings_item address_to item = settings_item(row);

                if (((item->flags & SETTINGS_ITEM_JOINED) != 0) != (settings.list.part == 0))
                        settings_port_set(item, settings.list.part == 0);
                return;
        }
        if (settings.list.part >= 0 && row < SETTINGS_ANY && settings.state[row] == SETTINGS_READ)
        {
                char word[SETTINGS_VALUE];

                if (settings_row_is(row, SETTING_ACCENT_PATH) && settings.list.part < (int)(sizeof(settings_accents) / sizeof(settings_accents[0])))
                        settings_change(row, settings_accents[settings.list.part].name);
                else if (settings_word(settings_rows[row].choices, (positive)settings.list.part, word, sizeof(word)))
                {
                        if (string_equals((string_address)word, (string_address)settings.value[row]))
                                settings_say((string_address)WORDS(settings_already_set));
                        else
                                settings_change(row, word);
                }
                return;
        }
        if (settings_item(row) && (settings_item(row)->flags & SETTINGS_ITEM_DISK))
                return;
        if (settings_editing() != row && !(row < SETTINGS_ANY && settings_rows[row].kind == SETTINGS_SLIDER))
                settings_activate(row, settings.list.way);
}

/* ---- the picture ---- */

static void settings_map_paint(struct ui address_to ui) { if (settings_map_on()) settings_map_draw(ui); }

static void settings_card_paint(struct ui address_to ui)
{
        if (settings_pair_on && settings.child > 0)
                settings_pair_paint(ui);
        else
                ui_lines_draw(ui, &settings.card, ui_px(ui, UI_PAD), &settings.card_text);
}

static fn settings_draw_rows(unsigned int address_to pixels, int band_top, int band_bottom)
{
        struct ui address_to ui = &settings.ui;
        struct ui_page_parts parts;

        ui_paint_into(ui, pixels, settings.own.table->stride, settings.width, settings.height, band_top, band_bottom);
        if (gallery.on)
                return gallery_draw(ui);
        settings.page.title = settings.page.card ? settings.card_title : language_of("Settings");
        settings.list.empty = WORDS(settings_nothing_matches);
        settings.page.search.items[0] = settings.page.card ? null : WORDS(settings_filter_hint);
        settings.page.search.edit = settings.page.card ? null : settings_filter_field();
        memory_zero(&parts, sizeof(parts));
        parts.keys = settings_editing() < 0 && settings.focused;
        parts.group_name = settings_group_text, parts.groups = settings_groups();
        parts.column_keys = settings.focused && !settings.page.pane, parts.column_off = settings_filter_length() != 0;
        parts.list_lit = (settings.page.pane || settings_filter_length()) && settings_editing() < 0 && !settings.map.keys;
        parts.list_keys = settings.focused && settings.page.pane && !settings.map.keys;
        parts.list = &settings.list, parts.between = settings_map_paint, parts.card = settings_card_paint;
        parts.overlays[0] = settings_editing() >= 0 && settings_edit_shown() ? &settings.edit : null;
        parts.overlays[1] = settings.filter_open && !settings.page.card ? &settings.filter_edit : null;
        ui_page_paint(ui, &settings.page, &parts);
}

static fn settings_fold_step(void)
{
        b32 moved = false, shut = false;
        int first = -1;

        for (positive k = 0; k < SETTINGS_FOLD_POOL; k++)
        {
                int row = settings_fold_pool[k].row - 1, at;
                b64 target, was, now;
                b32 live;

                if (row < 0 || row >= (int)SETTINGS_ALL || !settings_fold_live[row])
                {
                        settings_fold_pool[k].row = 0;
                        continue;
                }
                target = settings_expanded[row] ? UI_ONE : 0, was = settings_fold_at[row];
                live = ui_spring_at(&settings_fold_pool[k].spring);
                now = live ? target + settings_fold_pool[k].spring.value[0] : target;
                now = now < 0 ? 0 : now > UI_ONE ? UI_ONE : now;
                settings_fold_at[row] = now, settings_fold_live[row] = live;
                if (!live)
                        settings_fold_pool[k].row = 0;
                ui_kit.busy |= live;
                shut |= !live && !target;
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

static b32 settings_frame(void)
{
        settings_fold_step();
        if (settings.own.table && settings.width > 0 && settings_least() &&
            ((settings.width < settings.least_w && settings.width < (int)settings.own.table->output_width &&
              (!settings.own.table->stride || settings.width < (int)settings.own.table->stride)) ||
             (settings.height < settings.least_h && settings.height < (int)settings.own.table->output_height)))
                settings_size((unsigned int)settings.width, (unsigned int)settings.height);
        if (!ui_buffers_present(&settings.page.buffers, &settings.own, settings.width, settings.height, settings_draw_rows, null))
                return false;
        settings.page.dirty = false;
        return true;
}

/* ---- the program ---- */

static fn settings_size(unsigned int width, unsigned int height)
{
        struct windows address_to table = settings.own.table;
        unsigned int most = table->stride && table->stride < table->output_width ? table->stride : table->output_width;

        if (settings_least())
                width = width < (unsigned int)settings.least_w ? (unsigned int)settings.least_w : width,
                height = height < (unsigned int)settings.least_h ? (unsigned int)settings.least_h : height;
        settings.width = (int)(width > most ? most : width);
        settings.height = (int)(height > table->output_height ? table->output_height : height);
        ui_scratch_fit(&settings.ui, (unsigned int)settings.width);
        if (settings.page.card)
                ui_lines_wrap(&settings.ui, &settings.card_text, settings.width);
        settings_layout();
        ui_page_column_show(&settings.ui, &settings.page);
        ui_page_menu_close(&settings.page, &settings.popup);
        ui_page_redraw(&settings.page);
}

static fn settings_language(void)
{
        settings_say((string_address) "");
        settings_gpu_key = 0xffffffffu;
        if (settings.width > 0 && settings.height > 0)
                settings_layout();
        ui_page_redraw(&settings.page);
}

static b32 settings_notices_asked;
static int settings_system_group(void)
{
        for (int at = 0; at < SETTINGS_ROWS; at++)
                if (settings_rows[at].output == SETTINGS_OUT_NOTICES)
                        return settings_group_of(at);
        return 0;
}

static void settings_asked(unsigned int page) { settings_group_choose((int)page); }

static int settings_open(void)
{
        struct windows address_to table = settings.own.table;
        unsigned int row = settings.own.row;
        int width = ui_px(&settings.ui, gallery.on ? GALLERY_W : SETTINGS_WIDTH);
        int height = ui_px(&settings.ui, gallery.on ? GALLERY_H : SETTINGS_HEIGHT);
        int room = (int)table->output_height - ui_px(&settings.ui, 2 * (24 + 24));

        if (!gallery.on && (settings_notices_asked ? ui_lift_row(&settings.own, (string_address) "settings", false, ((unsigned int)((unsigned int)settings_system_group())) + 1)
                                                   : ui_lift_other(&settings.own, (string_address) "settings", false)))
                return 0;
        if (settings_notices_asked)
                settings.page.chosen = settings_system_group();
        settings_refilter();
        if (gallery.on)
                gallery_build(&settings.own, settings_name_accepts, SETTINGS_NAME_MOST);
        settings_size((unsigned int)width, (unsigned int)(height < room ? height : room > 0 ? room : height));
        ui_free(table, row, settings.width, settings.height, gallery.on ? 0 : ui_px(&settings.ui, 12));
        if (!gallery.on)
        {
                settings_say((string_address) "");
                settings_ask_all();
                settings.help = 1;
                settings_next_job();
        }
        settings_frame();
        return -1;
}

static fn settings_chose(int id)
{
        struct ui_text address_to t = settings.popup.field;

        if (ui_list_menu_do(&settings.popup, &settings.own, id))
        {
                ui_page_redraw(&settings.page);
                return;
        }
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
                settings_filter_asked(ui_text_menu_do(&settings.ui, t, id)), ui_page_redraw(&settings.page);
        else if (t && (t == &gallery.field[0] || t == &gallery.field[1]))
                ui_text_menu_do(&settings.ui, t, id), ui_page_redraw(&settings.page);
}

static long settings_tick(long timeout, long address_to other)
{
        if (settings_pair_on && settings.page.card && settings.child > 0)
        {
                positive now = (positive)ui_kit.now;

                if (now >= settings_pair_due)
                        settings_pair_due = now + (positive)SETTINGS_PAIR_TICK_NS, ui_page_redraw(&settings.page);
                timeout = ui_deadline_wait(timeout, (b64)now, (b64)settings_pair_due);
        }
        if (settings.list.ids)
                timeout = ui_timeout_min(timeout, settings_board_tick());
        if (!settings.page.card && (gallery.on || settings_editing() >= 0 || settings.filter_open))
        {
                struct ui_text address_to t = gallery.on ? &gallery.field[gallery.field[1].focused]
                                              : settings_editing() >= 0 ? &settings.edit : &settings.filter_edit;

                timeout = ui_field_tick(&settings.ui, &settings.page, t, timeout, gallery.on ? t->focused : settings.focused);
        }
        timeout = ui_timeout_min(timeout, settings_keep_tick());
        if (other)
                *other = -1;
        settings_runner_ready();
        if (settings.runner.child > 0 && settings.runner.ending == 1 && settings.runner.timeout_ns &&
            (positive)ui_kit.now >= settings.runner.began + settings.runner.timeout_ns)
        {
                ui_job_kill(&settings.runner);
                return 0;
        }
        timeout = ui_job_tick(&settings.runner, timeout, other);
        if (settings.runner.child <= 0)
        {
                timeout = ui_timeout_min(timeout, settings_feed_tick());
                if (settings.runner.child > 0)
                        return 0;
        }
        return timeout;
}

static fn settings_close(void)
{
        settings_wall_free();
        if (settings.child > 0)
        {
                positive status = 0;

                system_call_2(syscall(kill), (positive)settings.child, settings_pair_on ? SIGTERM : SIGKILL);
                system_wait4_retry(settings.child, address_of status, 1, null);
        }
}

static b32 settings_focus(b32 focused)
{
        if (!focused)
                ui_page_let_go(&settings.ui, &settings.page, &settings.list);
        return true;
}

static struct ui_app settings_app = {
        .own = &settings.own, .ui = &settings.ui,
        .missing = (string_address) "settings: no window: the compositor's desktop is not up\n",
        .title = (string_address) "Settings", .name = (string_address) "settings",
        .glass = 0, .side = true, .font = null,
        .nofont = (string_address) "settings: no font to draw with (-f FONT.ttf), pid ",
        .dirty = &settings.page.dirty, .focused = &settings.focused, .ground = &settings.ground,
        .open = settings_open, .page = settings_asked, .frame = settings_frame,
        .language = settings_language, .event = settings_event, .focus = settings_focus,
        .buffers = &settings.page.buffers, .size = settings_size, .tick = settings_tick,
        .job = &settings.runner, .close = settings_close, .popup = &settings.popup, .chose = settings_chose,
};

static b32 settings_main()
{
        memory_zero(&settings, sizeof(settings));
        settings.moonwater = (string_address) "/moonwater";
        ui_buffers_start(&settings.page.buffers);
        settings.output = -1, settings.page.pane = 1, settings.page.menu_row = -1;
        settings_runner_ready();
        for (positive at = 1; at < (positive)program_argument_count();)
        {
                positive used = ui_app_option(&settings_app, at);
                string_address value;

                if (used)
                        at += used;
                else if ((value = ui_argument(at, (string_address) "-m")))
                        settings.moonwater = value, at += 2;
                else if (string_equals(program_argument(at), (string_address) "-n"))
                        settings_notices_asked = true, at++;
                else if (string_equals(program_argument(at), (string_address) "-g"))
                        gallery.on = 1, settings_app.glass = 0, settings_app.side = false, settings_app.mono = 1,
                        settings_app.title = (string_address) "Gallery", settings_app.name = (string_address) "gallery", at++;
                else
                        at++;
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
