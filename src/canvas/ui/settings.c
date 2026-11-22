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

enum { SETTINGS_UNREAD = 0u, SETTINGS_READ = 1u, SETTINGS_ABSENT = 2u, SETTINGS_FAILED = 3u };
#define SETTINGS_VALUE 128
#define SETTINGS_TYPED 256
#define SETTINGS_SAID 262144
#define SETTINGS_STATUS 160
#define SETTINGS_WORDS 40
#define SETTINGS_WAIT_NS 20000000000ll
#define SETTINGS_FILTER 64
#define SETTINGS_GENERATED 128
#define SETTINGS_POOL 16384
#define SETTINGS_CARD 12288
#define SETTINGS_CARD_LINES 256
#define SETTINGS_PROBE 512
#define SETTINGS_LABEL_CUT 128
#define SETTINGS_TRY_SPAN 192
#define SETTINGS_LOGO_X0 14
#define SETTINGS_LOGO_Y0 74
#define SETTINGS_LOGO_SRC_W 292
#define SETTINGS_LOGO_SRC_H 44
#define SETTINGS_LOGO_W 172
#define SETTINGS_LOGO_ROW_H 44
#define SETTINGS_LOGO_MAX_W 576
#define SETTINGS_LOGO_MAX_H 96
#define SETTINGS_NONE 0xffffu
#define SETTINGS_WIDTH 700
#define SETTINGS_HEIGHT 520
#define SETTINGS_NAME_ROW 0
#define SETTINGS_NAME_MOST 31
#define SETTINGS_JOB_HELP -2
#define SETTINGS_JOB_FEED -3
#define SETTINGS_JOB_ACT -4
#define SETTINGS_JOB_IDENTIFY -5
#define SETTINGS_ANY ((int)SETTINGS_ROWS)
#define SETTINGS_ALL_HEADER "All commands"
#define SETTINGS_EXTERNAL_HEADER "External devices"

enum { SETTINGS_FEED_ROWS = 1u, SETTINGS_FEED_WIFI, SETTINGS_FEED_DEVICES, SETTINGS_FEED_HARDWARE, SETTINGS_FEED_LINKS };
#define SETTINGS_LISTS 4
#define SETTINGS_LIST_STEP 0x040000 // sparse ID stride per dynamic live list (262,144 items/list)
#define SETTINGS_ITEM_LABEL 96
#define SETTINGS_FEED_WAIT_NS 8000000000ll
#define SETTINGS_ACT_WAIT_NS 60000000000ll
#define SETTINGS_WAIT_PAIR_NS 330000000000ll
#define SETTINGS_LIST 0x100000
#define SETTINGS_LIST_ID(list, at) (SETTINGS_LIST + (list) * SETTINGS_LIST_STEP + (at))
#define SETTINGS_SUB_BASE (SETTINGS_LIST + SETTINGS_LISTS * SETTINGS_LIST_STEP)
#define SETTINGS_ALL SETTINGS_SUB_BASE
enum {
        SETTINGS_ITEM_JOINED = 1u, SETTINGS_ITEM_SAVED = 2u, SETTINGS_ITEM_HEARD = 4u, SETTINGS_ITEM_OPEN = 8u,
        SETTINGS_ITEM_CANNOT = 16u, SETTINGS_ITEM_DRIVE = 32u, SETTINGS_ITEM_USB = 64u, SETTINGS_ITEM_DISK = 128u,
};
enum { SETTINGS_WHERE_INTERNAL = 0, SETTINGS_WHERE_EXTERNAL = 1, SETTINGS_WHERE_SD = 2, SETTINGS_WHERE_NONE = 3 };
#define SETTINGS_SEGMENTS 33
#define SETTINGS_STATE 24
#define SETTINGS_PORT_ROOM 24
#define SETTINGS_SIZE_TEXT 16
#define SETTINGS_SEGMENT_NAME 40
#define SETTINGS_ALIGNMENT 1048576ull

struct settings_change { int row; char value[SETTINGS_TYPED]; };

struct settings_feed
{
        const char address_to group, address_to owner, address_to read[3], address_to heading;
        unsigned short every;
        unsigned char kind;
};

static const struct settings_feed settings_feeds[] = {
    {"Wi-Fi", "Wi-Fi", {"wifi"}, "In range and saved", 2000, SETTINGS_FEED_WIFI},
    {"Bluetooth", "Bluetooth", {"bluetooth"}, "Remembered devices", 3000, SETTINGS_FEED_DEVICES},
    {"Devices", null, {"device", "list"}, "Internal storage", 3000, SETTINGS_FEED_HARDWARE},
    {"Waterlink", "Waterlink", {"link"}, "Linked machines", 500, SETTINGS_FEED_LINKS},
    {"Power", null, {null}, null, 2000, SETTINGS_FEED_ROWS},
    {"Time", null, {null}, null, 1000, SETTINGS_FEED_ROWS},
    {"Network", null, {null}, null, 2000, SETTINGS_FEED_ROWS},
};
#define SETTINGS_FEEDS (sizeof(settings_feeds) / sizeof(settings_feeds[0]))

struct settings_drive
{
        unsigned char role[SETTINGS_SEGMENTS];
        unsigned long long bytes[SETTINGS_SEGMENTS];
        char name[SETTINGS_SEGMENTS][SETTINGS_SEGMENT_NAME];
};

struct settings_item
{
        char label[SETTINGS_ITEM_LABEL], security[12], state[SETTINGS_STATE], port[SETTINGS_PORT_ROOM], size[SETTINGS_SIZE_TEXT], access[48], detail[SETTINGS_ITEM_LABEL];
        char link_addr[64], link_rtt[24], link_traffic[48], link_group[32];
        signed short dbm;
        unsigned char bars, flags, kind, where, segment_count, grants;
        unsigned short segment_more;
        int drive_idx;
        unsigned char address_to segment_role;
        unsigned long long address_to segment_bytes, total;
        char (address_to segment_name)[SETTINGS_SEGMENT_NAME];
};

struct settings_ink { const char address_to name, address_to colour; };

static const struct settings_ink settings_inks[] = {
    {"default", "1e1e1e"}, {"black", "000000"}, {"dark-grey", "3a3a3c"}, {"slate", "334155"}, {"midnight-blue", "101935"},
    {"forest", "163a26"}, {"plum", "3b1f4a"}, {"light-grey", "d4d4d8"}, {"white", "ffffff"},
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

static const char address_to const settings_asks[][3] = {
    {"wipe"}, {"setup", "install"}, {"setup", "update"}, {"setup", "use"}, {"setup", "rollback"}, {"sleep"},
    {"hibernate"}, {"bios", "reboot"}, {"desktop", "start"}, {"desktop", "rest"}, {"desktop", "off"}, {"link", "remove"},
    {"link", "group", "leave"}, {"wifi", "remove"}, {"bluetooth", "remove"}, {"cpu", "offline"}, {"init", "remove"}, {"exit", "remove"},
};

/* ---- model & string helpers ---- */

static inline fn settings_wipe(address_any secret, positive length)
{
        if (secret && length)
        {
                memory_fill(secret, 0, length);
                __asm__ __volatile__("" : : "r"(secret) : "memory");
        }
}

static fn settings_slice(char address_to into, positive room, const char address_to text, positive size) { positive t = size >= room ? room - 1 : size; memory_copy(into, text, t); into[t] = 0; }
static positive settings_cat(char address_to into, positive room, positive at, const char address_to text) { for (positive n = 0; text[n] && at + 1 < room; n++) into[at++] = text[n]; into[at] = 0; return at; }
static positive settings_cat_sep(char address_to into, positive room, positive at, const char address_to sep, const char address_to part) { return !part || !part[0] ? at : settings_cat(into, room, at ? settings_cat(into, room, at, sep) : 0, part); }

static positive settings_cat_num(char address_to into, positive room, positive at, long value)
{
        char digits[24];
        unsigned long mag = value < 0 ? (unsigned long)(-(value + 1)) + 1 : (unsigned long)value;
        if (value < 0) at = settings_cat(into, room, at, "-");
        digits[positive_into((p8 address_to)digits, (positive)mag)] = 0;
        return settings_cat(into, room, at, digits);
}

static positive settings_keep(const char address_to said, positive length, positive address_to at, char address_to into, positive room)
{
        positive made = 0;
        while (*at < length && said[*at] != '\n')
        {
                char c = said[(*at)++];
                if (c == '\t') c = ' ';
                if (c == 0x1b)
                {
                        if (*at < length && said[*at] == '[')
                                for ((*at)++; *at < length && said[*at] != '\n' && (said[*at] < 0x40 || said[*at] > 0x7e); (*at)++) ;
                        *at += *at < length && said[*at] != '\n';
                        continue;
                }
                if ((unsigned char)c >= ' ' && c != 0x7f && made + 1 < room) into[made++] = c;
        }
        *at += *at < length, into[made] = 0;
        return made;
}

static positive settings_label_length(const char address_to line, positive length) { positive rest = length; return (positive)(ui_strip_verb_prefix(line, &rest) - line); }

static positive settings_line(const char address_to said, positive length, positive address_to at, char address_to into, positive room)
{
        positive made = settings_keep(said, length, at, into, room), skip = settings_label_length(into, made);
        while (skip < made && into[skip] == ' ') skip++;
        if (skip) memory_copy(into, into + skip, made - skip + 1);
        return made - skip;
}

static positive settings_find(const char address_to line, positive length, const char address_to key)
{
        positive size = string_length((string_address)key);
        for (positive at = 0; at + size <= length; at++)
                if ((!at || line[at - 1] == ' ' || line[at - 1] == ';') && !memory_compare(line + at, key, size)) return at;
        return length;
}

static positive settings_unquoted(const char address_to line, positive length)
{
        for (positive at = 0; at + 2 < length; at++)
                if (line[at] == ';' && line[at + 1] == ' ' && line[at + 2] == '"') return at;
        return length;
}

static b32 settings_lacks(const char address_to line, positive length, const struct settings_row address_to row)
{
        return settings_find(line, length, "this machine has no") < length || settings_find(line, length, "no wireless hardware") < length ||
               (settings_find(line, length, "this battery has no") < length && !(row->key && row->key[0] && settings_find(line, length, row->key) < length));
}

static unsigned int settings_parse(const struct settings_row address_to row, const char address_to said, positive length, char address_to value, positive room)
{
        char line[SETTINGS_STATUS];
        positive at = 0, found = 0;
        b32 absent = false;
        value[0] = 0;
        while (at < length)
        {
                positive size = settings_line(said, length, &at, line, sizeof(line));
                if (!size) continue;
                absent |= settings_lacks(line, settings_unquoted(line, size), row);
                for (positive which = 0; which < 2 && !found; which++)
                {
                        const char address_to key = which ? row->also : row->key;
                        positive where, start, stop;
                        if (!key || (where = key[0] ? settings_find(line, size, key) : 0) >= size) continue;
                        for (start = where + string_length((string_address)key); start < size && line[start] == ' '; start++) ;
                        for (stop = start; stop < size && (!row->stop || line[stop] != row->stop) &&
                                           ((row->flags & SETTINGS_LINE) || (line[stop] != ' ' && line[stop] != ',' && line[stop] != ';' && line[stop] != '(')); stop++) ;
                        while (stop > start && line[stop - 1] == ' ') stop--;
                        if (stop > start) settings_slice(value, room, line + start, stop - start), found = 1;
                }
        }
        if (found && row->kind == SETTINGS_SWITCH && row->lacks && !string_equals((string_address)value, (string_address) "on") && !string_equals((string_address)value, (string_address) "off"))
                absent = true;
        return absent ? SETTINGS_ABSENT : found ? SETTINGS_READ : row->lacks ? SETTINGS_ABSENT : SETTINGS_FAILED;
}

static unsigned int settings_battery(string_address chosen, char address_to value, positive room)
{
        static const struct { unsigned char state; unsigned int text; } charge_words[] = {
            {BATTERY_CHARGING, LANGUAGE_settings_battery_charging}, {BATTERY_DISCHARGING, LANGUAGE_settings_battery_discharging},
            {BATTERY_FULL, LANGUAGE_settings_battery_full}, {BATTERY_NOT_CHARGING, LANGUAGE_settings_battery_idle},
        };
        struct battery_state battery;
        value[0] = 0;
        if (chosen && string_equals(chosen, (string_address) "none")) return SETTINGS_ABSENT;
        if (!chosen || !chosen[0] || string_equals(chosen, (string_address) "auto") || !battery_read(chosen, &battery)) battery_read(null, &battery);
        if (!battery.present) return SETTINGS_ABSENT;
        if (battery.percent < 0) string_copy_bounded((string_address)value, (string_address)WORDS(settings_unknown), room);
        else language_format(value, room, WORDS(settings_percent), (long)battery.percent);
        for (positive k = 0; k < sizeof(charge_words) / sizeof(charge_words[0]); k++)
                if (battery.charging == charge_words[k].state) string_append_bounded((string_address)value, (string_address)language_text(charge_words[k].text), room);
        return SETTINGS_READ;
}

static const char address_to settings_battery_choice(void);

static unsigned int settings_read(const struct settings_row address_to row, const char address_to said, positive length, char address_to value, positive room, b32 failed)
{
        unsigned int state;
        if (row->label && string_equals((string_address)row->label, (string_address) "Battery"))
                return settings_battery((string_address)settings_battery_choice(), value, room);
        state = settings_parse(row, said, length, value, room);
        if (state == SETTINGS_READ && (row->flags & SETTINGS_INK) && value[0] == '#')
        {
                const struct settings_ink address_to ink = settings_ink_find(row, value + 1, true);
                if (ink) string_copy_bounded((string_address)value, (string_address)ink->name, room);
        }
        return state == SETTINGS_ABSENT && failed && !length ? SETTINGS_FAILED : state;
}

static b32 settings_word(const char address_to words, positive index, char address_to into, positive room)
{
        while (words && *words)
        {
                positive size = (positive)(string_first_of_or_end((string_address)words, ' ') - (string_address)words);
                if (!index--) return settings_slice(into, room, words, size), true;
                words += size + (words[size] == ' ');
        }
        return false;
}

static int settings_slider_step(const struct settings_row address_to row) { return row->high - row->low >= 100 ? 5 : 1; }

static fn settings_next(const struct settings_row address_to row, const char address_to value, int way, char address_to into, positive room)
{
        into[0] = 0;
        if (row->kind == SETTINGS_SWITCH)
                string_copy_bounded((string_address)into, (string_address)(way > 1 ? "on" : way < -1 ? "off" : string_equals((string_address)value, (string_address) "on") ? "off" : "on"), room);
        else if (row->kind == SETTINGS_STEP)
                settings_word(row->choices, way > 0 ? 1 : 0, into, room);
}

static positive settings_tokens(const char address_to text, char address_to buffer, positive size, string_address address_to words, positive count, positive room)
{
        positive used = 0;
        while (*text)
        {
                char quote = 0;
                while (*text == ' ') text++;
                if (!*text) break;
                if (*text == '"' || *text == '\'') quote = *text++;
                if (count + 1 >= room) return 0;
                words[count++] = (string_address)buffer + used;
                while (*text && (quote ? *text != quote : *text != ' '))
                {
                        if (used + 1 >= size) return 0;
                        buffer[used++] = *text++;
                }
                text += quote && *text == quote;
                if (used + 1 >= size) return 0;
                buffer[used++] = 0;
        }
        words[count] = null;
        return count;
}

/* ---- program state ---- */

struct settings_generic { unsigned short command, text; unsigned char verb; };

struct settings_map
{
        int display, sent_x[WINDOWS_OUTPUTS], sent_y[WINDOWS_OUTPUTS], shown_x[WINDOWS_OUTPUTS], shown_y[WINDOWS_OUTPUTS], wall_w[WINDOWS_OUTPUTS], wall_h[WINDOWS_OUTPUTS];
        b32 keys, sent;
        positive sent_at;
        unsigned int sent_count, shown, wall_key[WINDOWS_OUTPUTS];
        struct ui_graph graph;
        struct ui_graph_node node[WINDOWS_OUTPUTS];
        char number[WINDOWS_OUTPUTS][4], size[WINDOWS_OUTPUTS][32], shown_name[WINDOWS_OUTPUTS][WINDOWS_OUTPUT_NAME];
        struct ui_rect panel;
        struct ui_control identify, fit;
};

#define SETTINGS_WALL_W 192
#define SETTINGS_WALL_H 192
#define SETTINGS_WALL_BYTES ((positive)SETTINGS_WALL_W * SETTINGS_WALL_H * sizeof(unsigned int))
#define SETTINGS_WALL_DIR "/root/wallpapers"
#define SETTINGS_WALL_FILE_MOST (64ul << 20)
static unsigned int address_to settings_wall_pixels[WINDOWS_OUTPUTS];

static struct
{
        struct window_row own;
        struct ui ui;
        string_address moonwater;
        char value[SETTINGS_ROWS][SETTINGS_VALUE];
        unsigned char state[SETTINGS_ROWS];
        struct settings_generic generic[SETTINGS_GENERATED];
        positive generic_count, pool_used, feed_due, battery_menu_count, unread[SETTINGS_UNREAD_WORDS], queue_room, queued;
        unsigned int last_text, least_key, ground;
        b32 truncated, filter_open, focused, hand, job_sets, told;
        int help, feed_list, width, height, least_w, least_h, job;
        int item_count[SETTINGS_LISTS];
        struct settings_item address_to item[SETTINGS_LISTS];
        positive item_room[SETTINGS_LISTS];
        struct settings_drive address_to drive;
        positive drive_room, drive_count;
        char forget[SETTINGS_ITEM_LABEL], joining[SETTINGS_ITEM_LABEL], head[SETTINGS_VALUE], accents[SETTINGS_VALUE], edit_from[SETTINGS_VALUE], status[SETTINGS_STATUS], card_title[SETTINGS_STATUS], job_value[SETTINGS_TYPED];
        struct ui_page page;
        struct ui_list list;
        struct ui_scroll card;
        struct battery_name battery_menu[BATTERY_LIST];
        struct ui_text filter_edit, edit;
        struct ui_popup popup;
        struct ui_lines card_text;
        struct ui_confirm confirm;
        struct ui_job runner;
        struct settings_change address_to queue;
        struct settings_map map;
} settings;

static struct settings_large
{
        char said[SETTINGS_SAID], pool[SETTINGS_POOL], card[SETTINGS_CARD], split[SETTINGS_TYPED * 2], label_cut[SETTINGS_ROWS][SETTINGS_LABEL_CUT];
        unsigned short card_at[SETTINGS_CARD_LINES], card_size[SETTINGS_CARD_LINES];
        struct ui_columns columns[SETTINGS_LISTS];
        struct ui_view view;
        unsigned char try_cover[2][SETTINGS_TRY_SPAN * SETTINGS_TRY_SPAN], logo_cover[SETTINGS_LOGO_MAX_W * SETTINGS_LOGO_MAX_H];
} address_to settings_large;

static void address_to settings_alloc_pages(positive bytes) { long m = ui_map((bytes + 4095) & ~(positive)4095); return m < 0 && m > -4096 ? null : (void address_to)m; }
static b32 settings_map(void) { return settings_large || (settings_large = (struct settings_large address_to)settings_alloc_pages(sizeof(struct settings_large))) != null; }
static fn settings_changed(int top, int bottom) { ui_page_damage(&settings.page, top, bottom); }

/* ---- issue board, GPU sample, fold springs & sub-rows ---- */

static int settings_section_start(int row);
static fn settings_rebuild(void);
static fn settings_sort_runs(void);
static fn settings_say(string_address text);
static unsigned int settings_copy_serial; // the table's clipboard serial when a row's menu opened: a copy that took moves it
// A copy's word: copied when the clipboard took it, and that it could not be copied when it did not (the window has no clipboard to put it on).
static fn settings_offered(b32 taken)
{
        settings_say(taken ? (string_address)WORDS(settings_copied) : (string_address)WORDS(settings_copy_failed));
}
static struct settings_item address_to settings_item(int row);

enum { SETTINGS_TRY_REPEAT = 0, SETTINGS_TRY_CLICK = 1, SETTINGS_TRY_POINTER = 2, SETTINGS_TRIES = 3 };
#define SETTINGS_TRY 0x300000
enum { SETTINGS_ABOUT_BRAND = 0, SETTINGS_ABOUT_DISCORD = 1, SETTINGS_ABOUT_GITHUB = 2, SETTINGS_ABOUT_COUNT = 3 };
#define SETTINGS_ABOUT (SETTINGS_TRY + SETTINGS_TRIES)
#define SETTINGS_LIST_IDS (SETTINGS_ABOUT + SETTINGS_ABOUT_COUNT)
#define SETTINGS_SUB_TEXT 240
#define SETTINGS_BOARD_NS 1000000000ll
#define SETTINGS_REAP_NS 10000000ll
#define SETTINGS_LEFT_NS 2000000000ll
#define SETTINGS_LEFT 8
#define SETTINGS_GPU_SAMPLES 10
#define SETTINGS_METRIC_NS 10000000000ll
enum { SETTINGS_SUB_COPY = 0u, SETTINGS_SUB_DISMISS_ALL = 1u, SETTINGS_SUB_PARTITIONS = 2u, SETTINGS_SUB_PARTITION = 3u, SETTINGS_SUB_LINK_GRANT = 4u, SETTINGS_SUB_LINK_INFO = 5u };
static const struct { const char address_to word, address_to label, address_to note; } settings_link_grants[4] = {
    {"files", "Filesystem access", "Browse, upload, and download files"},
    {"run",   "Run commands",      "Execute single remote commands"},
    {"shell", "Interactive shell", "Open a remote terminal session"},
    {"log",   "Kernel log",        "Stream system logs"},
};
#define SETTINGS_PART_FREE -1
#define SETTINGS_PART_MORE -2

struct settings_sub
{
        int section, owner, part;
        b32 detail;
        const char address_to words;
        unsigned int code, action;
        char text[SETTINGS_SUB_TEXT];
};
static fn settings_part_describe(const struct settings_sub address_to sub, struct ui_row address_to row);

static struct settings_sub address_to settings_sub;
static positive settings_sub_room;
static int settings_sub_count, settings_sub_owner, settings_sub_cut;
static struct windows_issues settings_board;
static positive settings_board_due, settings_metric_at;
static char settings_status[SETTINGS_SUB_TEXT], settings_metric_line[SETTINGS_SUB_TEXT], settings_metric_shown[SETTINGS_SUB_TEXT], settings_notices_text[SETTINGS_SUB_TEXT];
static unsigned int settings_gpu_status = WINDOWS_GPU_STATUS_NOT, settings_gpu_at, settings_gpu_drawn, settings_gpu_processor, settings_gpu_shown_total, settings_gpu_key = 0xffffffffu;
static unsigned int settings_gpu_ring_drawn[SETTINGS_GPU_SAMPLES], settings_gpu_ring_processor[SETTINGS_GPU_SAMPLES];
static char (address_to settings_drives_open)[SETTINGS_ITEM_LABEL];
static positive settings_drives_open_room;
static char (address_to settings_links_open)[SETTINGS_ITEM_LABEL];
static positive settings_links_open_room;
static unsigned char settings_link_global_grants = 0x0fu;

/* The folds are the kit's (ui_list_fold): it keeps a key's open state and share, below UI_LIST_FOLD_MOST. A fold's share is applied by the
   kit to the row whose id is the key, so Settings' folds are keys that no row has: the lines under a row (its children) fold on the key of the
   row, and the row keeps its own height. A row's key is SETTINGS_FOLD_ROW_AT + its id, a drive's SETTINGS_FOLD_DRIVE_AT + its index, a link's
   SETTINGS_FOLD_LINK_AT + its index (the first 64 of each). */
static int settings_domain_of(int id, int address_to offset);
#define SETTINGS_FOLD_ROW_AT 600
#define SETTINGS_FOLD_DRIVE_AT (SETTINGS_FOLD_ROW_AT + SETTINGS_ANY)
#define SETTINGS_FOLD_LINK_AT (SETTINGS_FOLD_DRIVE_AT + 64)
_Static_assert(SETTINGS_ANY + 128 <= SETTINGS_FOLD_ROW_AT, "the command rows' ids stay below the fold keys");
_Static_assert(SETTINGS_FOLD_LINK_AT + 64 <= UI_LIST_FOLD_MOST, "the fold keys fit the list's folds");

static int settings_fold_key(int row);
static b32 settings_exp_get(int row) { int key = settings_fold_key(row); return key >= 0 && ui_list_folded(&settings.list, key); }
// A row's share of its fold (the kit's, which animates it), or its open state where the kit has no share yet.
static b64 settings_fold_reveal(int row)
{
        int key = settings_fold_key(row);
        b64 share = key >= 0 ? ui_list_fold_share(&settings.list, key) : -1;
        return share >= 0 ? share : settings_exp_get(row) ? UI_ONE : 0;
}
// The chevron's turn while the fold moves (ui_row.turn), or -1 where the kit does not fold the row.
static b64 settings_fold_turn(int row)
{
        int key = settings_fold_key(row);
        return key >= 0 ? ui_list_fold_share(&settings.list, key) : -1;
}
// The lines under a row are listed while the row is open or its lines are still folding shut.
static b32 settings_lines_listed(int row) { return settings_exp_get(row) || settings_fold_reveal(row) > 0; }

static b32 settings_key_of(int row, char address_to key, positive room)
{
        const struct settings_row address_to which = row >= 0 && row < SETTINGS_ANY ? &settings_rows[row] : null;
        positive at = 0;
        key[0] = 0;
        if (which && which->output == SETTINGS_OUT_NOTICES)
                return string_copy_bounded((string_address)key, (string_address)WINDOWS_KEY_NOTICES, room), true;
        if (!which || !which->read[0])
                return false;
        for (positive k = 0; k < SETTING_WORDS && which->read[k]; k++)
                at = settings_cat_sep(key, room, at, " ", which->read[k]);
        return true;
}

static b32 settings_always_open(int row) { return row >= 0 && row < SETTINGS_ANY && settings_rows[row].output == SETTINGS_OUT_NOTICES; }

static int settings_expander(int row)
{
        char key[WINDOWS_ISSUE_KEY];
        if (settings_always_open(row) || row < 0 || row >= SETTINGS_ANY || !settings_key_of(row, key, sizeof(key)))
                return 0;
        if (!windows_issues_worst(&settings_board, key, null) && !string_equals((string_address)key, (string_address)WINDOWS_KEY_GPU))
                return 0;
        return settings_exp_get(row) ? UI_ROW_EXPANDER_OPEN : UI_ROW_EXPANDER_SHUT;
}

/* One set of names that are open (a drive's or a link's, by name), kept in a grown array. */
static b32 settings_name_open(char (address_to set)[SETTINGS_ITEM_LABEL], positive room, const char address_to name)
{
        for (positive at = 0; at < room; at++)
                if (set[at][0] && string_equals((string_address)set[at], (string_address)name)) return true;
        return false;
}

static fn settings_name_keep(char (address_to address_to set)[SETTINGS_ITEM_LABEL], positive address_to room, const char address_to name, b32 open)
{
        positive free = *room;
        for (positive at = 0; at < *room; at++)
                if ((*set)[at][0] && string_equals((string_address)(*set)[at], (string_address)name)) (*set)[at][0] = 0;
        if (!open) return;
        for (positive at = *room; at-- > 0;) free = (*set)[at][0] ? free : at;
        if (free >= *room)
        {
                positive was = *room;
                if (!array_store_reserve(*set, *room, was, was + 1, 16))
                        return;
                memory_zero(*set + was, (*room - was) * sizeof((*set)[0]));
                free = was;
        }
        string_copy_bounded((string_address)(*set)[free], (string_address)name, sizeof((*set)[free]));
}

static b32 settings_drive_is_open(const char address_to name) { return settings_name_open(settings_drives_open, settings_drives_open_room, name); }
static fn settings_drive_keep(const char address_to name, b32 open) { settings_name_keep(&settings_drives_open, &settings_drives_open_room, name, open); }
static b32 settings_link_is_open(const char address_to name) { return settings_name_open(settings_links_open, settings_links_open_room, name); }
static fn settings_link_keep(const char address_to name, b32 open) { settings_name_keep(&settings_links_open, &settings_links_open_room, name, open); }

static fn settings_toggle(int row)
{
        const struct settings_item address_to item = settings_item(row);
        int key = settings_fold_key(row);
        b32 open;
        if (key < 0 || (row >= SETTINGS_ANY && !(item && ((item->flags & (SETTINGS_ITEM_DRIVE | SETTINGS_ITEM_DISK)) || item->kind == SETTINGS_LINK))))
                return;
        open = !ui_list_folded(&settings.list, key);
        ui_list_fold(&settings.list, key, open);
        if (item && item->kind == SETTINGS_LINK)
                settings_link_keep(item->where == 0 ? "__this_machine__" : item->label, open);
        else if (item)
                settings_drive_keep(item->flags & SETTINGS_ITEM_DISK ? item->port : item->label, open);
        settings_rebuild();
}

static void settings_copy(int at) { if (at >= 0 && at < settings_sub_count) settings_offered(windows_clip_offer(&settings.own, WINDOWS_CLIP_CLIPBOARD, settings_sub[at].text, string_length((string_address)settings_sub[at].text)) == 0); }

static void settings_badge(int row, struct ui_row address_to out)
{
        char key[WINDOWS_ISSUE_KEY];
        unsigned int w;
        if (settings_key_of(row, key, sizeof(key)) && (w = windows_issues_worst(&settings_board, key, null)))
                out->badge = w == WINDOWS_ISSUE_ERROR ? UI_ROW_BADGE_ERROR : UI_ROW_BADGE_WARNING,
                out->badge_words = w == WINDOWS_ISSUE_ERROR ? WORDS(settings_badge_error) : WORDS(settings_badge_warning);
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
                language_format(settings_status, sizeof(settings_status), settings_gpu_status == WINDOWS_GPU_STATUS_OFF ? WORDS(settings_status_off) : WORDS(settings_status_none));
        else if (!settings_gpu_shown_total)
                language_format(settings_status, sizeof(settings_status), WORDS(settings_status_waiting));
        else
                language_format(settings_status, sizeof(settings_status),
                                settings_gpu_status == WINDOWS_GPU_STATUS_IN_USE ? WORDS(settings_status_in_use)
                                : settings_gpu_status == WINDOWS_GPU_STATUS_PARTLY ? WORDS(settings_status_partly) : WORDS(settings_status_not),
                                (long)drawn, (long)settings_gpu_shown_total, (long)((unsigned long long)drawn * 100 / settings_gpu_shown_total));
        settings_gpu_key = settings_gpu_status;
        if (settings_gpu_status != WINDOWS_GPU_STATUS_OFF && settings_gpu_status != WINDOWS_GPU_STATUS_NONE && settings_gpu_shown_total)
                settings_gpu_key |= 1u << 8 | (unsigned int)((unsigned long long)drawn * 100 / settings_gpu_shown_total) << 9;
}

static int settings_sub_put(int section, b32 detail, const char address_to words, const char address_to text, int count)
{
        struct settings_sub address_to sub;
        if ((positive)settings_sub_count >= settings_sub_room)
                if (!array_store_reserve(settings_sub, settings_sub_room, (positive)settings_sub_count, (positive)settings_sub_count + 1, 32))
                        return count;
        if (!ui_list_add(&settings.list, SETTINGS_SUB_BASE + settings_sub_count))
                return count;
        sub = &settings_sub[settings_sub_count++];
        memory_zero(sub, sizeof(*sub));
        sub->section = section, sub->owner = settings_sub_owner;
        sub->detail = detail, sub->words = words;
        string_copy_bounded((string_address)sub->text, (string_address)text, sizeof(sub->text));
        return count + 1;
}

static int settings_sub_add(int section, b32 detail, const char address_to words, const char address_to text, unsigned int code, unsigned int action, int part, int count)
{
        int next = settings_sub_put(section, detail, words, text, count);
        if (next != count)
                settings_sub[settings_sub_count - 1].code = code, settings_sub[settings_sub_count - 1].action = action, settings_sub[settings_sub_count - 1].part = part;
        return next;
}

static const unsigned int settings_req_ids[] = {
    0, LANGUAGE_settings_req_node, LANGUAGE_settings_req_driver, LANGUAGE_settings_req_family, LANGUAGE_settings_issue_gpu_userptr,
    LANGUAGE_settings_req_context, LANGUAGE_settings_req_ring, LANGUAGE_settings_req_selftest, LANGUAGE_settings_req_udmabuf, LANGUAGE_settings_req_prime,
};

static const struct { unsigned int code, text; unsigned char fmt; } settings_issue_specs[] = {
    {WINDOWS_ISSUE_GPU_MAP, LANGUAGE_settings_issue_gpu_map, 2}, {WINDOWS_ISSUE_GPU_USERPTR, LANGUAGE_settings_issue_gpu_userptr, 0},
    {WINDOWS_ISSUE_GPU_REFUSED, LANGUAGE_settings_issue_gpu_refused, 4}, {WINDOWS_ISSUE_GPU_LISTS, LANGUAGE_settings_issue_gpu_lists, 0},
    {WINDOWS_ISSUE_GPU_COOLING, LANGUAGE_settings_issue_gpu_cooling, 1}, {WINDOWS_ISSUE_GPU_UNTRUSTED, LANGUAGE_settings_issue_gpu_untrusted, 1},
    {WINDOWS_ISSUE_GPU_FAMILY, LANGUAGE_settings_issue_gpu_family, 1}, {WINDOWS_ISSUE_GPU_FAULT, LANGUAGE_settings_issue_gpu_fault, 3},
    {WINDOWS_ISSUE_GPU_HUNG, LANGUAGE_settings_issue_gpu_hung, 0}, {WINDOWS_ISSUE_GPU_LOST, LANGUAGE_settings_issue_gpu_lost, 2},
    {WINDOWS_ISSUE_GPU_SCANOUT, LANGUAGE_settings_issue_gpu_scanout, 1}, {WINDOWS_ISSUE_AUDIO_OPEN, LANGUAGE_settings_issue_audio_open, 1},
    {WINDOWS_ISSUE_AUDIO_RATE, LANGUAGE_settings_issue_audio_rate, 2},
};

static void settings_issue_text(const struct windows_issue address_to issue, char address_to into, positive room)
{
        if (issue->code == WINDOWS_ISSUE_GPU_REQUIRES)
        {
                if (issue->a > 0 && issue->a < sizeof(settings_req_ids) / sizeof(settings_req_ids[0]))
                        language_format(into, room, language_text(settings_req_ids[issue->a]), issue->detail);
                else
                        language_format(into, room, WORDS(settings_issue_unknown), (long)issue->code);
                return;
        }
        if (windows_issue_kernel(issue->code) || issue->code == WINDOWS_ISSUE_RADIO || issue->code == WINDOWS_ISSUE_NOTIFICATION)
                return (void)string_copy_bounded((string_address)into, (string_address)issue->detail, room);
        for (positive at = 0; at < sizeof(settings_issue_specs) / sizeof(settings_issue_specs[0]); at++)
                if (settings_issue_specs[at].code == issue->code)
                {
                        const char address_to t = language_text(settings_issue_specs[at].text);
                        switch (settings_issue_specs[at].fmt)
                        {
                        case 0: language_format(into, room, t); return;
                        case 1: language_format(into, room, t, (long)issue->a); return;
                        case 2: language_format(into, room, t, (long)issue->a, (long)issue->b); return;
                        case 3: language_format(into, room, t, issue->detail); return;
                        case 4: language_format(into, room, t, (long)issue->a, issue->detail); return;
                        }
                }
        language_format(into, room, WORDS(settings_issue_unknown), (long)issue->code);
}

static int settings_sub_rows(int row, int count)
{
        char key[WINDOWS_ISSUE_KEY], text[SETTINGS_SUB_TEXT];
        const struct settings_row address_to which = row < SETTINGS_ANY ? &settings_rows[row] : null;
        int section;
        if (!settings_key_of(row, key, sizeof(key)) || (!settings_always_open(row) && (!settings_lines_listed(row) || !settings_expander(row))))
                return count;
        settings_sub_owner = row, section = settings_section_start(row);
        if (string_equals((string_address)key, (string_address)WINDOWS_KEY_GPU) && which && which->kind == SETTINGS_SWITCH)
                count = settings_sub_put(section, true, null, settings_metric_line, settings_sub_put(section, false, null, settings_status, count));
        for (int i = 0; i < WINDOWS_ISSUES; i++)
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

static fn settings_sub_describe(int at, struct ui_row address_to row)
{
        const struct settings_sub address_to sub = &settings_sub[at];
        if (sub->action == SETTINGS_SUB_PARTITIONS || sub->action == SETTINGS_SUB_PARTITION)
                return settings_part_describe(sub, row);
        if (sub->action == SETTINGS_SUB_LINK_INFO)
        {
                row->sub = 1, row->section = sub->section, row->label = sub->words, row->value = sub->text, row->dim = true;
                row->reveal = settings_fold_reveal(sub->owner);
                return;
        }
        if (sub->action == SETTINGS_SUB_LINK_GRANT && sub->part >= 0 && sub->part < 4)
        {
                const struct settings_item address_to item = settings_item(sub->owner);
                unsigned char bit = (unsigned char)(1u << sub->part);
                b32 global_on = (settings_link_global_grants & bit) != 0;
                row->sub = 1, row->section = sub->section, row->kind = UI_ROW_SWITCH;
                row->label = settings_link_grants[sub->part].label;
                if (item && item->where == 0)
                        row->note = settings_link_grants[sub->part].note, row->on = global_on;
                else if (!global_on)
                        row->note = "Disabled globally on this machine", row->on = false, row->dim = true;
                else
                        row->note = settings_link_grants[sub->part].note, row->on = item && (item->grants & bit) != 0;
                row->reveal = settings_fold_reveal(sub->owner);
                return;
        }
        row->sub = 1, row->section = sub->section, row->label = sub->text, row->value = sub->words, row->dim = sub->detail;
        row->reveal = sub->owner >= 0 && !settings_always_open(sub->owner) ? settings_fold_reveal(sub->owner) : UI_ONE;
}

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
        if (memory_compare(&copy, &settings_board, sizeof(copy)) || settings_gpu_key != was ||
            (!string_equals((string_address)settings_metric_shown, (string_address)settings_metric_line) && now - settings_metric_at >= (positive)SETTINGS_METRIC_NS))
                settings_board = copy, settings_metric_at = now, settings_rebuild();
        return (long)SETTINGS_BOARD_NS;
}

/* ---- rows, domains, groups & filtering ---- */

enum settings_domain_kind { SETTINGS_DOM_NONE = -1, SETTINGS_DOM_ROW = 0, SETTINGS_DOM_COMMAND = 1, SETTINGS_DOM_LIST = 2, SETTINGS_DOM_SUB = 3, SETTINGS_DOM_TRY = 4, SETTINGS_DOM_ABOUT = 5 };

static int settings_domain_of(int id, int address_to offset)
{
        static const int stops[7] = {0, SETTINGS_ANY, SETTINGS_LIST, SETTINGS_SUB_BASE, SETTINGS_TRY, SETTINGS_ABOUT, SETTINGS_LIST_IDS};
        for (int k = 0; k < 6; k++)
                if (id >= stops[k] && id < stops[k + 1])
                        return offset ? (*offset = id - stops[k], k) : k;
        return offset ? (*offset = -1, SETTINGS_DOM_NONE) : SETTINGS_DOM_NONE;
}

static int settings_fold_key(int row)
{
        int offset, list, at;
        if (row >= 0 && row < SETTINGS_ANY)
                return SETTINGS_FOLD_ROW_AT + row;
        if (settings_domain_of(row, &offset) != SETTINGS_DOM_LIST)
                return -1;
        list = offset / SETTINGS_LIST_STEP, at = offset % SETTINGS_LIST_STEP;
        if (at >= 64)
                return -1;
        return list == 2 ? SETTINGS_FOLD_DRIVE_AT + at : list == 3 ? SETTINGS_FOLD_LINK_AT + at : -1;
}

static positive settings_rows_count(void) { return (positive)SETTINGS_ANY + 1 + settings.generic_count; }
static const struct settings_generic address_to settings_generic_of(int row) { int at; return settings_domain_of(row, &at) == SETTINGS_DOM_COMMAND && at > 0 ? &settings.generic[at - 1] : null; }
static int settings_list_of(int row) { int at; return settings_domain_of(row, &at) == SETTINGS_DOM_LIST ? at / SETTINGS_LIST_STEP : -1; }
static int settings_device_section(const struct settings_item address_to item) { return item->where == SETTINGS_WHERE_INTERNAL ? 0 : item->where == SETTINGS_WHERE_NONE ? 2 : 1; }
static int settings_pos(int row) { return ui_list_at(&settings.list, row); }
static int settings_editing(void) { return settings.edit.open ? settings.edit.owner : -1; }

static int settings_about_of(int id);
static const char address_to settings_about_label(int which);
static b32 settings_about_matches_word(int which, const char address_to word, positive size);
static int settings_system_group(void);
static int settings_groups(void);
static const char address_to settings_group_name(int group);

static struct settings_item address_to settings_item(int row)
{
        int offset, list, at;
        if (settings_domain_of(row, &offset) != SETTINGS_DOM_LIST)
                return null;
        list = offset / SETTINGS_LIST_STEP, at = offset % SETTINGS_LIST_STEP;
        return list >= 0 && list < SETTINGS_LISTS && at >= 0 && at < settings.item_count[list] ? &settings.item[list][at] : null;
}

static const char address_to settings_label_get(int row, b32 shown)
{
        int at;
        switch (settings_domain_of(row, &at))
        {
        case SETTINGS_DOM_ROW:     return shown ? language_of(settings_rows[at].label) : settings_rows[at].label;
        case SETTINGS_DOM_COMMAND: return at > 0 ? settings_large->pool + settings.generic[at - 1].command : WORDS(settings_run_any);
        case SETTINGS_DOM_LIST:    { const struct settings_item address_to it = settings_item(row); return !it ? WORDS(settings_run_any) : shown && it->kind == SETTINGS_ADD ? language_of(it->label) : it->label; }
        case SETTINGS_DOM_ABOUT:   return shown ? settings_about_label(at) : WORDS(settings_run_any);
        default:                   return WORDS(settings_run_any);
        }
}

static const char address_to settings_label(int row) { return settings_label_get(row, false); }
static const char address_to settings_label_shown(int row) { return settings_label_get(row, true); }

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
        settings_slice(settings_large->label_cut[row], sizeof(settings_large->label_cut[0]), shown, open);
        return settings_large->label_cut[row];
}

static const char address_to settings_item_text(const struct settings_item address_to item)
{
        if (item->kind == SETTINGS_NETWORK)
                return item->flags & SETTINGS_ITEM_JOINED ? WORDS(settings_item_joined)
                       : item->flags & SETTINGS_ITEM_CANNOT ? WORDS(settings_item_cannot)
                       : item->flags & SETTINGS_ITEM_OPEN ? WORDS(settings_item_open)
                       : item->flags & SETTINGS_ITEM_SAVED ? WORDS(settings_item_saved) : WORDS(settings_item_ask);
        return item->flags & SETTINGS_ITEM_DRIVE ? WORDS(settings_item_drive)
               : item->kind == SETTINGS_DEVICE ? WORDS(settings_item_kept)
               : item->kind == SETTINGS_LINK ? WORDS(settings_item_link)
               : item->kind == SETTINGS_ADD ? WORDS(settings_item_add) : "";
}

static const char address_to settings_text(int row)
{
        const struct settings_generic address_to g = settings_generic_of(row);
        const struct settings_item address_to it = settings_item(row);
        return it ? settings_item_text(it) : g ? settings_large->pool + g->text : WORDS(settings_any_text);
}

static b32 settings_takes(int row) { const struct settings_generic address_to g = settings_generic_of(row); return !g || settings_large->pool[g->command + g->verb] != 0; }

static int settings_section_start(int row)
{
        int offset;
        switch (settings_domain_of(row, &offset))
        {
        case SETTINGS_DOM_ROW:
                while (row > 0 && !settings_rows[row].section && !settings_rows[row].part)
                        row--;
                return row;
        case SETTINGS_DOM_COMMAND: return SETTINGS_ANY;
        case SETTINGS_DOM_LIST:
        {
                int list = offset / SETTINGS_LIST_STEP, at = offset % SETTINGS_LIST_STEP;
                const struct settings_item address_to item = settings_item(row);
                if (list == 2 && item)
                {
                        int sec = settings_device_section(item);
                        while (at > 0 && settings_device_section(&settings.item[2][at - 1]) == sec)
                                at--;
                        return SETTINGS_LIST_ID(2, at);
                }
                if (list == 3 && item)
                {
                        unsigned char w = item->where;
                        while (at > 0 && settings.item[3][at - 1].where == w)
                                at--;
                        return SETTINGS_LIST_ID(3, at);
                }
                return SETTINGS_LIST_ID(list, 0);
        }
        default: return row;
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
        case SETTINGS_DOM_COMMAND: return SETTINGS_ALL_HEADER;
        case SETTINGS_DOM_LIST:
        {
                int list = offset / SETTINGS_LIST_STEP;
                const struct settings_item address_to it = settings_item(row);
                if (list == 2 && it && settings_device_section(it))
                        return settings_device_section(it) == 1 ? SETTINGS_EXTERNAL_HEADER : "";
                if (list == 3 && it && it->where == 0)
                        return "This machine";
                return settings_feeds[list].heading;
        }
        default: return "";
        }
}

static const char address_to settings_header(int row) { int s = settings_section_start(row); return s < SETTINGS_ANY && settings_rows[s].part ? settings_rows[s].part : settings_section(row); }

static int settings_groups(void)
{
        int count = 1;
        for (positive at = 0; at < SETTINGS_ROWS; at++)
                count += settings_rows[at].section != null;
        return count;
}

static const char address_to settings_group_name(int group)
{
        for (positive at = 0, seen = 0; at < SETTINGS_ROWS; at++)
                if (settings_rows[at].section && (int)seen++ == group)
                        return settings_rows[at].section;
        return SETTINGS_ALL_HEADER;
}

static const char address_to settings_group_text(int group) { return language_of(settings_group_name(group)); }

static int settings_group_of(int row)
{
        int offset;
        switch (settings_domain_of(row, &offset))
        {
        case SETTINGS_DOM_ROW:     { int g = -1; for (int at = 0; at <= offset; at++) g += settings_rows[at].section != null; return g; }
        case SETTINGS_DOM_COMMAND: return settings_groups() - 1;
        case SETTINGS_DOM_LIST:
                for (int at = 0; at < settings_groups(); at++)
                        if (string_equals((string_address)settings_group_name(at), (string_address)settings_feeds[offset / SETTINGS_LIST_STEP].group))
                                return at;
                return 0;
        case SETTINGS_DOM_ABOUT:   return settings_system_group();
        default:                   return 0;
        }
}

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
                const struct settings_row address_to w = &settings_rows[offset];
                for (positive at = 0; at < SETTING_WORDS; at++)
                        if (settings_has(w->read[at], word, size) || settings_has(w->set[at], word, size))
                                return true;
                return settings_has(settings.value[offset], word, size) || settings_matches_name(w->label, settings_section(row), word, size) ||
                       settings_matches_name(w->about, settings_rows[settings_section_start(row)].part, word, size);
        }
        case SETTINGS_DOM_LIST:    return settings_matches_name(settings_label(row), settings_section(row), word, size);
        case SETTINGS_DOM_ABOUT:   return settings_about_matches_word(offset, word, size);
        case SETTINGS_DOM_COMMAND: case SETTINGS_DOM_SUB: case SETTINGS_DOM_TRY:
                return settings_has(settings_label(row), word, size) || settings_has(settings_text(row), word, size) || (row == SETTINGS_ANY && settings_has("anything", word, size));
        default:                   return false;
        }
}

static positive settings_filter_text(p8 address_to into, positive room) { return !room ? 0 : (into[0] = 0, settings.filter_open ? ui_text_search(&settings.filter_edit, into, room) : 0); }
static positive settings_filter_length(void) { p8 key[SETTINGS_FILTER]; return settings_filter_text(key, sizeof(key)); }

static b32 settings_matches(int row)
{
        p8 key[SETTINGS_FILTER];
        const char address_to text = (const char address_to)key;
        positive at = 0, length = settings_filter_text(key, sizeof(key));
        while (at < length)
        {
                positive start;
                while (at < length && text[at] == ' ')
                        at++;
                for (start = at; at < length && text[at] != ' '; at++)
                        ;
                if (at > start && !settings_matches_word(row, text + start, at - start))
                        return false;
        }
        return true;
}

/* ---- display screen map, wallpaper thumbnails & layout ---- */

#define SETTINGS_MAP_STRIP 36
#define SETTINGS_MAP_PANEL 150
#define SETTINGS_MAP_TALL 360
#define SETTINGS_MAP_BUTTON_W 168
#define SETTINGS_MAP_BUTTON_H 28
#define SETTINGS_MAP_HOLD_NS 4000000000ull
#define SETTINGS_SHOWN 64
#define SETTINGS_KEEP_NS 15000000000ull
#define SETTINGS_SIZES_SHOWN (UI_MENU_ITEMS - 1)

static char settings_shown_screen[SETTINGS_SHOWN], settings_shown_rate[SETTINGS_SHOWN], settings_noted[2 * SETTINGS_SHOWN];
static struct
{
        b32 on, seen;
        char name[WINDOWS_OUTPUT_NAME];
        unsigned int width, height, old_w, old_h;
        positive due;
} settings_resize;

static int settings_output_row(unsigned char output)
{
        for (positive at = 0; at < SETTINGS_ROWS; at++)
                if (settings_rows[at].output == output)
                        return (int)at;
        return -1;
}

static fn settings_map_follow(unsigned int count)
{
        struct windows address_to table = settings.own.table;
        int dx[WINDOWS_OUTPUTS], dy[WINDOWS_OUTPUTS], best = -1;
        unsigned int moves = 0, most = 0, still = 0;
        for (unsigned int at = 0; at < count; at++)
                for (unsigned int was = 0; was < settings.map.shown; was++)
                        if (string_equals((string_address)settings.map.shown_name[was], (string_address)table->output_name[at]))
                        {
                                dx[moves] = settings.map.node[at].x - settings.map.shown_x[was], dy[moves++] = settings.map.node[at].y - settings.map.shown_y[was];
                                break;
                        }
        for (unsigned int at = 0; at < moves; at++)
        {
                unsigned int same = 0;
                for (unsigned int o = 0; o < moves; o++)
                        same += dx[o] == dx[at] && dy[o] == dy[at];
                if (!dx[at] && !dy[at])
                        still = same;
                else if (same > most)
                        most = same, best = (int)at;
        }
        if (best >= 0 && most > still)
                ui_graph_shift(&settings.map.graph, dx[best], dy[best]);
        settings.map.shown = count;
        for (unsigned int at = 0; at < count; at++)
                memory_copy(settings.map.shown_name[at], table->output_name[at], WINDOWS_OUTPUT_NAME),
                settings.map.shown_x[at] = settings.map.node[at].x, settings.map.shown_y[at] = settings.map.node[at].y;
}

static unsigned int settings_wall_look(const char address_to path, unsigned int address_to stamp)
{
        p8 info[256] = {0};
        if (system_call_5(syscall(statx), (positive)AT_FDCWD, (positive)path, 0x100, 0x41, (positive)info) < 0)
                return 0;
        if (stamp)
                *stamp = (unsigned int)(memory_load_unaligned(b64, info + 112) * 1000003) ^ memory_load_unaligned(p32, info + 120);
        return memory_load_unaligned(p16, info + 28) & 0170000u;
}

static fn settings_wall_word(const char address_to path, char address_to word, positive room)
{
        bipolar handle = system_open_at(AT_FDCWD, (string_address)path, FILE_READ | O_NOFOLLOW | O_CLOEXEC), got;
        word[0] = 0;
        if (handle < 0)
                return;
        got = system_read_once(handle, word, room - 1);
        system_close(handle);
        word[got > 0 ? got : 0] = 0;
        for (positive at = 0; word[at]; at++)
                if (word[at] == '\n' || word[at] == ' ' || word[at] == '\r')
                        return (void)(word[at] = 0);
}

static b32 settings_wall_own(const char address_to name, char address_to path, positive room)
{
        if (!name || !name[0] || file_is_dot((string_address)name) || string_first_of((string_address)name, '/') != null ||
            string_length((string_address)SETTINGS_WALL_DIR "/") + string_length((string_address)name) + 5 > room)
                return false;
        settings_cat(path, room, settings_cat(path, room, settings_cat(path, room, 0, SETTINGS_WALL_DIR "/"), name), ".png");
        return settings_wall_look(path, null) == 0100000u;
}

static b32 settings_wall_picture(const char address_to path, unsigned int address_to into, int w, int h, positive most)
{
        bipolar handle = system_open_at(AT_FDCWD, (string_address)path, FILE_READ | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK), rd;
        p8 info[256] = {0};
        p8 address_to file;
        positive got = 0, vis_w, vis_h, ox, oy;
        unsigned int address_to pic = null;
        int width = 0, height = 0;
        if (handle < 0)
                return false;
        if (system_call_5(syscall(statx), (positive)handle, (positive)"", 0x1000, 0x1, (positive)info) < 0 ||
            (memory_load_unaligned(p16, info + 28) & 0170000u) != 0100000u)
                return system_close(handle), false;
        file = (p8 address_to)memory_checked(SETTINGS_WALL_FILE_MOST);
        while (file && got < SETTINGS_WALL_FILE_MOST && (rd = system_read_once(handle, file + got, SETTINGS_WALL_FILE_MOST - got)) > 0)
                got += (positive)rd;
        system_close(handle);
        if (!file)
                return false;
        if (got >= 24 && got < SETTINGS_WALL_FILE_MOST)
        {
                positive pw = (positive)file[16] << 24 | (positive)file[17] << 16 | (positive)file[18] << 8 | file[19];
                positive ph = (positive)file[20] << 24 | (positive)file[21] << 16 | (positive)file[22] << 8 | file[23];
                if (pw && ph && pw <= 16384 && ph <= 16384 && pw * ph <= most)
                        pic = ui_png_decode_most(file, got, &width, &height, 16384);
        }
        memory_free(file, SETTINGS_WALL_FILE_MOST);
        if (!pic)
                return false;
        if ((positive)width * (positive)h > (positive)height * (positive)w)
                vis_h = (positive)height, vis_w = (positive)height * (positive)w / (positive)h, ox = ((positive)width - vis_w) / 2, oy = 0;
        else
                vis_w = (positive)width, vis_h = (positive)width * (positive)h / (positive)w, ox = 0, oy = ((positive)height - vis_h) / 2;
        vis_w = vis_w ? vis_w : 1, vis_h = vis_h ? vis_h : 1;
        for (int y = 0; y < h; y++)
        {
                positive y0 = oy + (positive)y * vis_h / (positive)h, y1 = oy + ((positive)y + 1) * vis_h / (positive)h;
                y1 += y1 == y0;
                for (int x = 0; x < w; x++)
                {
                        positive x0 = ox + (positive)x * vis_w / (positive)w, x1 = ox + ((positive)x + 1) * vis_w / (positive)w, r = 0, g = 0, b = 0, n;
                        x1 += x1 == x0;
                        n = (y1 - y0) * (x1 - x0);
                        for (positive sy = y0; sy < y1; sy++)
                                for (positive sx = x0; sx < x1; sx++)
                                {
                                        unsigned int px = pic[sy * (positive)width + sx];
                                        r += px >> 16 & 255, g += px >> 8 & 255, b += px & 255;
                                }
                        into[y * w + x] = 0xff000000u | (unsigned int)(r / n) << 16 | (unsigned int)(g / n) << 8 | (unsigned int)(b / n);
                }
        }
        memory_free(pic, (positive)width * (positive)height * 4);
        return true;
}

static fn settings_wall_free(void)
{
        for (unsigned int at = 0; at < WINDOWS_OUTPUTS; at++)
                if (settings_wall_pixels[at])
                        compositor_call(syscall(munmap), (positive)settings_wall_pixels[at], (SETTINGS_WALL_BYTES + 4095) & ~(positive)4095, 0, 0, 0, 0),
                        settings_wall_pixels[at] = null, settings.map.wall_key[at] = 0, settings.map.wall_w[at] = settings.map.wall_h[at] = 0;
}

static unsigned int settings_fnv32_str(unsigned int h, const char address_to s) { for (; *s; s++) h = (h ^ (p8)*s) * 16777619u; return h; }

static fn settings_map_wall(struct windows address_to table, unsigned int at, struct ui_graph_node address_to n, int width, int height)
{
        char kind[16], dark_word[16], maria[8], glow[24], ground[16], path[96];
        const struct { const char address_to path; char address_to dst; positive room; } files[] = {
            {"/root/canvas.wallpaper", kind, sizeof(kind)}, {"/root/canvas.wallpaper.dark", dark_word, sizeof(dark_word)},
            {"/root/canvas.wallpaper.maria", maria, sizeof(maria)}, {"/root/canvas.wallpaper.glow", glow, sizeof(glow)},
            {"/root/canvas.background", ground, sizeof(ground)},
        };
        unsigned int key = 2166136261u, stamp = 0, dark = !(table->themed && table->theme == UI_THEME_LIGHT), accent = table->themed ? table->accent & 0xffffffu : 0;
        int ar = (int)(accent >> 16 & 255), ag = (int)(accent >> 8 & 255), ab = (int)(accent & 255);
        int most = ar > ag ? (ar > ab ? ar : ab) : (ag > ab ? ag : ab), least = ar < ag ? (ar < ab ? ar : ab) : (ag < ab ? ag : ab);
        int w = SETTINGS_WALL_W, h = width > 0 ? SETTINGS_WALL_W * height / width : 1;
        unsigned int address_to into = at < WINDOWS_OUTPUTS ? (settings_wall_pixels[at] ? settings_wall_pixels[at] : (settings_wall_pixels[at] = (unsigned int address_to)settings_alloc_pages(SETTINGS_WALL_BYTES))) : null;
        b32 picture;

        if (!into)
                return (void)(n->picture = null, n->picture_w = n->picture_h = 0);
        if (h > SETTINGS_WALL_H)
                h = SETTINGS_WALL_H, w = height > 0 ? SETTINGS_WALL_H * width / height : 1;
        w = w < 1 ? 1 : w, h = h < 1 ? 1 : h;
        for (positive i = 0; i < sizeof(files) / sizeof(files[0]); i++)
                settings_wall_word(files[i].path, files[i].dst, files[i].room);
        picture = settings_wall_own(table->output_name[at], path, sizeof(path)) ||
                  (string_equals((string_address)kind, "picture") &&
                   (string_copy((string_address)path, (string_address)SETTINGS_WALL_DIR "/all.png"), settings_wall_look(path, null) == 0100000u));
        if (picture)
                settings_wall_look(path, &stamp);
        key = settings_fnv32_str(settings_fnv32_str(key, kind), picture ? path : "");
        key = (((key ^ stamp) * 16777619u ^ dark) * 16777619u ^ ((unsigned int)w << 12 ^ (unsigned int)h)) * 16777619u;
        accent = most - least < 40 ? 0x5b7fa6u : accent;
        key = (key ^ accent ^ (p8)dark_word[0] << 24 ^ (p8)glow[1] << 16 ^ (p8)maria[0] << 8 ^ (p8)maria[1] ^ (p8)ground[1] << 4 ^ (p8)ground[3]) * 16777619u;
        if (key != settings.map.wall_key[at] || settings.map.wall_w[at] != w || settings.map.wall_h[at] != h)
        {
                long colour = ground[0] && !string_equals((string_address)ground, "default") ? window_colour_parse((string_address)ground) : -1;
                settings.map.wall_key[at] = key, settings.map.wall_w[at] = w, settings.map.wall_h[at] = h;
                if (!picture || !settings_wall_picture(path, into, w, h, (positive)width * (positive)height * 2))
                {
                        if (string_equals((string_address)kind, "plain"))
                                memory_fill_u32_rect(into, (positive)w, (positive)w, (positive)h, (unsigned int)(colour < 0 ? 0x1e1e1e : colour) | 0xff000000u);
                        else
                        {
                                int rect[4] = {0}, seas = maria[0] ? (int)string_to_bipolar((string_address)maria) : 45;
                                memory_fill_u32_rect(into, (positive)w, (positive)w, (positive)h, 0xff000000u);
                                taskbar_wall_draw(into, w, w, h, (int)dark, !string_equals((string_address)dark_word, "inverted"), seas >= 20 && seas <= 80 ? seas : 45,
                                                  string_equals((string_address)glow, "none") ? 0 : string_equals((string_address)glow, "glow-and-edges") ? 2 : 1, accent, 0, 0, rect);
                                memory_or_u32(into, into, (positive)((long)w * (long)h), 0xff000000u);
                        }
                }
        }
        n->picture = into, n->picture_w = settings.map.wall_w[at], n->picture_h = settings.map.wall_h[at];
}

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
                n->x = x, n->y = y, n->w = width, n->h = height, n->flags = count > 1 && !at ? UI_GRAPH_MARKED : 0;
                settings_cat_num(settings.map.number[at], sizeof(settings.map.number[at]), 0, (long)number[at]);
                language_format(settings.map.size[at], sizeof(settings.map.size[at]), n->flags ? WORDS(settings_map_primary) : WORDS(settings_map_size), (long)width, (long)height);
                n->label = settings.map.number[at], n->line[0] = table->output_model[at][0] ? table->output_model[at] : table->output_name[at], n->line[1] = settings.map.size[at];
                settings_map_wall(table, at, n, width, height);
        }
        if (settings.map.sent && (caught || settings.map.sent_count != count || (positive)ui_kit.now - settings.map.sent_at > SETTINGS_MAP_HOLD_NS))
                settings.map.sent = false;
        for (unsigned int at = 0; settings.map.sent && at < count; at++)
                settings.map.node[at].x = settings.map.sent_x[at], settings.map.node[at].y = settings.map.sent_y[at];
        g->nodes = (int)count;
        if (g->held >= (int)count)
                ui_graph_let_go(g);
        g->chosen = settings.map.display < (int)count ? settings.map.display : -1, g->keys = settings.map.keys;
        if (count != settings.map.shown)
                g->homed = 0;
        settings_map_follow(count);
        return count;
}

static int settings_map_numbered(const unsigned int address_to number, unsigned int count, unsigned int wanted)
{
        for (unsigned int at = 0; at < count; at++)
                if (number[at] == wanted)
                        return (int)at;
        return -1;
}

static int settings_map_chosen(void)
{
        unsigned int number[WINDOWS_OUTPUTS] = {0}, count = settings_map_load(number);
        if (!count)
                return -1;
        if (settings.map.display < 0 || settings.map.display >= (int)count)
                settings.map.display = settings_map_numbered(number, count, 1);
        return settings.map.display;
}

static b32 settings_map_on(void) { int r = settings_output_row(SETTINGS_OUT_SCREEN); return r >= 0 && !settings.page.card && !settings_filter_length() && settings_group_of(r) == settings.page.chosen; }

static const struct settings_turn { const char address_to label; int degrees; } settings_turns[] = {
    {"Normal", 0}, {"Turned right", 90}, {"Upside down", 180}, {"Turned left", 270},
};
static const unsigned int settings_scales[] = {50, 75, 100, 125, 150, 200};

static const char address_to settings_turn_label(unsigned int turn)
{
        for (positive at = 0; at < sizeof(settings_turns) / sizeof(settings_turns[0]); at++)
                if (settings_turns[at].degrees == (int)turn)
                        return language_of(settings_turns[at].label);
        return language_of(settings_turns[0].label);
}

static positive settings_screen_label(unsigned int number, const char address_to name, const char address_to model, char address_to into, positive room)
{
        return !model[0] ? language_format(into, room, WORDS(settings_screen_bare), (long)number, name)
                         : language_format(into, room, WORDS(settings_screen_named), (long)number, model, name);
}

static unsigned int settings_scale_suggested(const struct windows address_to table, int chosen) { unsigned int w = table->output_w[chosen]; return w >= 5120 ? 200 : w >= 3840 ? 150 : 100; }
static positive settings_scale_label(unsigned int percent, b32 recommended, char address_to into, positive room) { return language_format(into, room, recommended ? WORDS(settings_scale_best) : WORDS(settings_scale_plain), (long)percent); }
static positive settings_rate_label(unsigned int hz, b32 recommended, char address_to into, positive room) { return language_format(into, room, recommended ? WORDS(settings_rate_best) : WORDS(settings_rate_plain), (long)hz); }
static unsigned int settings_rates_of(int chosen) { unsigned int r = settings.own.table->output_rates[chosen]; return r > WINDOWS_OUTPUT_HZ ? WINDOWS_OUTPUT_HZ : r; }
static unsigned int settings_sizes_of(int chosen) { unsigned int s = settings.own.table->output_sizes[chosen]; s = s > WINDOWS_OUTPUT_SIZES ? WINDOWS_OUTPUT_SIZES : s; return s > SETTINGS_SIZES_SHOWN ? SETTINGS_SIZES_SHOWN : s; }
static b32 settings_keep_here(int chosen) { return settings_resize.on && chosen >= 0 && string_equals((string_address)settings_resize.name, (string_address)settings.own.table->output_name[chosen]); }
static positive settings_size_label(const struct windows address_to t, int c, unsigned int i, char address_to into, positive room) { return language_format(into, room, i == t->output_size_preferred[c] ? WORDS(settings_size_best) : WORDS(settings_size_plain), (long)t->output_size[c][i][0], (long)t->output_size[c][i][1]); }
static positive settings_keep_left(void) { positive now = (positive)ui_kit.now; return settings_resize.due > now ? (settings_resize.due - now + 999999999ull) / 1000000000ull : 0; }
static fn settings_keep_label(char address_to into, positive room) { language_format(into, room, WORDS(settings_size_keep), (long)settings_resize.width, (long)settings_resize.height, (long)settings_keep_left()); }

static fn settings_change(int row, const char address_to value);
static fn settings_say_redraw(const char address_to line) { settings_say((string_address)line); ui_page_redraw(&settings.page); }

static positive settings_mode_cmd(char address_to value, positive room, const char address_to name, const char address_to verb, unsigned int w, unsigned int h, char address_to back, positive back_room)
{
        positive length = settings_cat(value, room, settings_cat(value, room, 0, name), verb);
        if (w)
                settings_cat_num(back, back_room, settings_cat(back, back_room, settings_cat_num(back, back_room, 0, (long)w), "x"), (long)h);
        else
                settings_cat(back, back_room, 0, "preferred");
        return settings_cat(value, room, length, back);
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
                language_format(line, sizeof(line), WORDS(settings_size_refused), (long)settings_resize.width, (long)settings_resize.height, settings_resize.old_w ? back : WORDS(settings_size_own));
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
                else if (table->output_asked_w[at] == settings_resize.width && table->output_asked_h[at] == settings_resize.height && (table->output_refused[at] & WINDOWS_REFUSED_SIZE))
                        return settings_keep_back(true), -1;
        }
        if (now >= settings_resize.due)
                return settings_keep_back(false), -1;
        ui_page_redraw(&settings.page);
        left = (settings_resize.due - now) % 1000000000ull;
        return (long)(left ? left : 1000000000ull);
}

static const struct settings_out_spec
{
        unsigned int refused_bit, refused_text, empty_text, pair_text[2];
        const char address_to verb;
        b32 plain_when_one;
} settings_out_specs[10] = {
    [SETTINGS_OUT_SCREEN]    = {WINDOWS_REFUSED_PLACE, LANGUAGE_settings_refused_place, 0, {0, 0}, null, true},
    [SETTINGS_OUT_TURN]      = {WINDOWS_REFUSED_TURN, LANGUAGE_settings_refused_turn, 0, {0, 0}, " rotation ", false},
    [SETTINGS_OUT_REFRESH]   = {WINDOWS_REFUSED_RATE, LANGUAGE_settings_refused_rate, LANGUAGE_settings_no_rates, {0, 0}, " refresh ", true},
    [SETTINGS_OUT_NOTE]      = {0, 0, 0, {0, 0}, null, false},
    [SETTINGS_OUT_SCALE]     = {0, 0, 0, {0, 0}, " scale ", false},
    [SETTINGS_OUT_NOTICES]   = {0, 0, 0, {0, 0}, null, false},
    [SETTINGS_OUT_SIZE]      = {WINDOWS_REFUSED_SIZE, LANGUAGE_settings_refused_size, LANGUAGE_settings_no_sizes, {0, 0}, " mode ", true},
    [SETTINGS_OUT_PRIMARY]   = {0, 0, 0, {LANGUAGE_settings_primary_yes, LANGUAGE_settings_primary_no}, " primary", true},
    [SETTINGS_OUT_WALLPAPER] = {0, 0, 0, {LANGUAGE_settings_wall_same, LANGUAGE_settings_wall_own}, null, false},
};

static const char address_to settings_output_text(int row, int chosen)
{
        struct windows address_to t = settings.own.table;
        unsigned int number[WINDOWS_OUTPUTS] = {0};
        if (chosen < 0)
                return "";
        switch (settings_rows[row].output)
        {
        case SETTINGS_OUT_TURN:      return settings_turn_label(t->output_turn[chosen]);
        case SETTINGS_OUT_SCALE:     return settings_scale_label(t->output_asked_scale[chosen], t->output_asked_scale[chosen] == settings_scale_suggested(t, chosen), settings_shown_rate, sizeof(settings_shown_rate)), settings_shown_rate;
        case SETTINGS_OUT_PRIMARY:   return language_text(settings_out_specs[SETTINGS_OUT_PRIMARY].pair_text[chosen != 0]);
        case SETTINGS_OUT_WALLPAPER: { char path[96]; return language_text(settings_out_specs[SETTINGS_OUT_WALLPAPER].pair_text[settings_wall_own(t->output_name[chosen], path, sizeof(path)) != 0]); }
        case SETTINGS_OUT_SIZE:
                if (settings_keep_here(chosen))
                        return settings_keep_label(settings_shown_rate, sizeof(settings_shown_rate)), settings_shown_rate;
                for (unsigned int at = 0; at < settings_sizes_of(chosen); at++)
                        if (t->output_size[chosen][at][0] == t->output_mode_w[chosen] && t->output_size[chosen][at][1] == t->output_mode_h[chosen])
                                return settings_size_label(t, chosen, at, settings_shown_rate, sizeof(settings_shown_rate)), settings_shown_rate;
                return language_format(settings_shown_rate, sizeof(settings_shown_rate), WORDS(settings_size_plain), (long)t->output_mode_w[chosen], (long)t->output_mode_h[chosen]), settings_shown_rate;
        case SETTINGS_OUT_REFRESH:
                for (unsigned int at = 0; at < settings_rates_of(chosen); at++)
                        if (t->output_hz[chosen][at] == t->output_now[chosen])
                                return settings_rate_label(t->output_hz[chosen][at], at == 0, settings_shown_rate, sizeof(settings_shown_rate)), settings_shown_rate;
                return settings_cat_num(settings_shown_rate, sizeof(settings_shown_rate), 0, (long)t->output_now[chosen]), settings_shown_rate;
        default:
                windows_screen_numbers(t, number);
                settings_screen_label(number[chosen], t->output_name[chosen], t->output_model[chosen], settings_shown_screen, sizeof(settings_shown_screen));
                return settings_shown_screen;
        }
}

static const char address_to settings_row_text(int row, int chosen)
{
        struct windows address_to t = settings.own.table;
        const char address_to shown = settings_output_text(row, chosen), address_to fmt;
        unsigned char out = settings_rows[row].output;
        const struct settings_out_spec address_to spec = out < 10 ? &settings_out_specs[out] : null;
        if (chosen < 0 || !spec || !spec->refused_bit || !(t->output_refused[chosen] & spec->refused_bit) || (out == SETTINGS_OUT_SIZE && settings_keep_here(chosen)))
                return shown;
        fmt = language_text(spec->refused_text);
        if (out == SETTINGS_OUT_REFRESH)
                language_format(settings_noted, sizeof(settings_noted), fmt, shown, (long)t->output_asked_hz[chosen]);
        else if (out == SETTINGS_OUT_TURN)
                language_format(settings_noted, sizeof(settings_noted), fmt, shown, settings_turn_label(t->output_asked_turn[chosen]));
        else if (out == SETTINGS_OUT_SIZE)
                language_format(settings_noted, sizeof(settings_noted), fmt, shown, (long)t->output_asked_w[chosen], (long)t->output_asked_h[chosen]);
        else
                language_format(settings_noted, sizeof(settings_noted), fmt, shown);
        return settings_noted;
}

static fn settings_map_frame(void)
{
        struct ui_graph address_to g = &settings.map.graph;
        g->x = settings.map.panel.x, g->y = settings.map.panel.y, g->w = settings.map.panel.w, g->h = settings.map.panel.h;
        g->top = ui_px(&settings.ui, SETTINGS_MAP_STRIP + UI_GAP2);
        g->rules = UI_GRAPH_SIDES | UI_GRAPH_APART | UI_GRAPH_TOUCH, g->grid = g->nodes ? 10 : -1, g->least = UI_GRAPH_ONE / 1024, g->most = UI_GRAPH_ONE;
}

static const char address_to settings_map_hint(unsigned int count)
{
        return settings.map.graph.held >= 0 && settings.map.graph.moving ? WORDS(settings_map_drag)
               : settings.map.keys && count > 1 ? WORDS(settings_map_keys)
               : !count ? WORDS(settings_map_none) : count == 1 ? WORDS(settings_map_one) : WORDS(settings_map_hint);
}

static fn settings_map_above(void)
{
        unsigned int number[WINDOWS_OUTPUTS] = {0};
        struct ui_control address_to identify = &settings.map.identify, address_to fit = &settings.map.fit;
        int tall = settings.ui.scale > 0 ? settings.height * 100 / settings.ui.scale * 2 / 5 : 0;
        if (!settings_map_on())
                return;
        tall = tall < SETTINGS_MAP_PANEL ? SETTINGS_MAP_PANEL : tall > SETTINGS_MAP_TALL ? SETTINGS_MAP_TALL : tall;
        identify->kind = fit->kind = UI_BUTTON, identify->variant = fit->variant = UI_BUTTON_DEFAULT;
        identify->label = (string_address)WORDS(settings_identify_screens), fit->label = (string_address)WORDS(settings_map_fit);
        ui_col(.host = &settings.map.panel, .class = "map", .h = tall)
                ui_row(.class = "foot", .h = SETTINGS_MAP_STRIP)
                {
                        ui_control(fit, .w = SETTINGS_MAP_BUTTON_W, .h = SETTINGS_MAP_BUTTON_H, .shrink = 1);
                        ui_control(identify, .w = SETTINGS_MAP_BUTTON_W, .h = SETTINGS_MAP_BUTTON_H, .shrink = 1);
                }
        ui_text(settings_map_hint(settings_map_load(number)), .class = "caption");
}

static fn settings_describe(int id, struct ui_row address_to row);
static fn settings_size(unsigned int width, unsigned int height);
static const struct ui_list_menu settings_list_menu;
static fn settings_columns_sorted(struct ui_columns address_to hit);

static fn settings_refilter(void);
static b32 settings_least_busy;
static unsigned int settings_least_key_of(const struct ui address_to ui)
{
        unsigned int key = 2166136261u;

        key = (((((key ^ (unsigned int)ui->scale) * 16777619u ^ ui_text_percent) * 16777619u ^ (unsigned int)ui_rtl()) * 16777619u ^ language.code) * 16777619u ^ settings.list.revision) * 16777619u;
        return key;
}

// The window's least width is the widest of the groups: each group is laid out in turn (its rows, its title and column), so the window
// fits the widest one and switching groups at the least size squeezes nothing. The layout a group makes inside this is not a new least.
static int settings_least(void)
{
        struct ui address_to ui = &settings.ui;
        unsigned int key;
        b32 column;
        const char address_to shown;
        int chosen, most = 0;
        if (gallery.on || !ui->regular || !settings.list.ids || !settings.page.view || ui->scale <= 0 || settings_least_busy)
                return 0;
        key = settings_least_key_of(ui);
        if (key == settings.least_key && settings.least_w > 0)
                return 1;
        settings_least_busy = true;
        chosen = settings.page.chosen, column = settings.page.column_on, shown = settings.page.title;
        for (int group = 0; group < settings_groups(); group++)
        {
                int w;
                settings.page.chosen = group;
                settings_refilter();
                settings.page.column_on = true, settings.page.title = language_of("Settings");
                w = ui_page_least(ui, &settings.page, &settings.list, settings.page.title);
                most = w > most ? w : most;
        }
        settings.page.chosen = chosen, settings.page.column_on = column, settings.page.title = shown;
        settings_refilter();
        settings_least_busy = false;
        settings.least_w = most;
        settings.least_key = settings_least_key_of(ui); // the key after the layout above: the rebuilds moved the list's revision
        settings.least_h = ui_px(ui, UI_PAGE_TOP + SETTINGS_MAP_PANEL + 2 * UI_GAP2 + 2 * ui_types[UI_LABEL_MEDIUM].line) + ui_dev(ui, UI_ROW);
        if (settings.own.table && settings.own.row < WINDOWS)
                settings.own.table->least_width[settings.own.row] = (unsigned int)((settings.least_w * 100 + ui->scale - 1) / ui->scale),
                settings.own.table->least_height[settings.own.row] = (unsigned int)((settings.least_h * 100 + ui->scale - 1) / ui->scale);
        return 2;
}

static fn settings_layout(void)
{
        struct ui address_to ui = &settings.ui;
        if (!settings_map())
                return;
        if (!settings.list.ids)
        {
                positive at = 0;
                settings.list.ids = SETTINGS_LIST_IDS, settings.list.describe = settings_describe, settings.list.damaged = settings_changed;
                settings.list.menu = &settings_list_menu, settings.list.sorted = settings_columns_sorted;
                settings.list.hover = settings.list.down = -1, settings.list.wrap = true, settings.map.display = -1;
                settings.list.shut = settings_rebuild;
                ui_graph_start(&settings.map.graph);
                for (positive k = 0; k < sizeof(settings_accents) / sizeof(settings_accents[0]); k++)
                        at = settings_cat_sep(settings.accents, sizeof(settings.accents), at, " ", settings_accents[k].colour);
        }
        settings.page.column_on = !settings.page.card, settings.page.body = settings.page.card ? &settings.card : &settings.list.scroll;
        settings.page.column.count = settings_groups() < UI_ITEMS ? settings_groups() : UI_ITEMS;
        settings.page.view = &settings_large->view, settings.page.above = settings_map_above, settings.page.popup = &settings.popup, settings.page.own = &settings.own;
        settings_least();
        ui_page_fit(ui, &settings.page, &settings.list, settings.width, settings.height, &settings.card, ui_lines_height(ui, settings.card_text.count));
}

static fn settings_edit_end(void);
static int settings_drive_rows(int row, int count);
static int settings_link_rows(int row, int count);
static fn settings_edit_begin(int row, const char address_to text, b32 masked);
static b32 settings_row_is(int at, const char address_to path);

/* ---- interactive preview ("try") rows, About section & rebuild ---- */

static const struct settings_try
{
        const char address_to path;
        b32 before;
        const char address_to shows[6];
        unsigned int label_id, about_id;
} settings_tries[SETTINGS_TRIES] = {
    [SETTINGS_TRY_REPEAT]  = {SETTING_REPEAT_RATE_PATH, false, {null}, LANGUAGE_settings_try_repeat, LANGUAGE_settings_try_repeat_about},
    [SETTINGS_TRY_CLICK]   = {SETTING_DOUBLE_CLICK_PATH, false, {SETTING_DOUBLE_CLICK_PATH}, LANGUAGE_settings_try_click, LANGUAGE_settings_try_click_about},
    [SETTINGS_TRY_POINTER] = {SETTING_CURSOR_STYLE_PATH, true, {SETTING_CURSOR_STYLE_PATH, SETTING_CURSOR_SIZE_PATH, SETTING_CURSOR_LOOK_PATH, SETTING_SQUEEZE_PATH, SETTING_CURSOR_CLICK_PATH}, LANGUAGE_settings_try_pointer, 0},
};
static struct { positive at; int x, y, apart; } settings_click_try;

static int settings_row_of(const char address_to path) { const struct settings_row address_to r = setting_by_path((string_address)path); return r ? (int)(r - settings_rows) : -1; }
static long settings_number_of(const char address_to path, long fallback) { int r = settings_row_of(path); long n = 0; return r >= 0 && settings.state[r] == SETTINGS_READ && setting_typed_number(&settings_rows[r], (string_address)settings.value[r], &n) ? n : fallback; }
static b32 settings_value_is(const char address_to path, const char address_to word) { int r = settings_row_of(path); return r >= 0 && settings.state[r] == SETTINGS_READ && string_equals((string_address)settings.value[r], (string_address)word); }
static int settings_try_of(int id) { int at; return settings_domain_of(id, &at) == SETTINGS_DOM_TRY ? at : -1; }

static int settings_try_cell(int scale, b32 pressed, b32 dynamic)
{
        long percent = settings_number_of(SETTING_CURSOR_SIZE_PATH, 100), squeeze = settings_number_of(SETTING_SQUEEZE_PATH, 25);
        int cell = (int)((compositor_px1(scale) * 256 * percent + 50) / 100), most = (SETTINGS_TRY_SPAN << 8) / (dynamic ? CP_DYN_CELL : CP_CELL);
        if (pressed && !settings_value_is(SETTING_CURSOR_CLICK_PATH, "off"))
                cell = (int)(cell * (100 - (squeeze < 0 ? 0 : squeeze > 50 ? 50 : squeeze)) / 100);
        return cell < 64 ? 64 : cell > most ? most : cell;
}

#define SETTINGS_TRY_ACROSS 24
#define SETTINGS_TRY_DOWN 21

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

static fn settings_try_pointer_paint(struct ui address_to ui, int id, int x, int y, int w, int h)
{
        b32 dynamic = !settings_value_is(SETTING_CURSOR_STYLE_PATH, "classic");
        int cell = settings_try_cell(ui->scale, settings.list.down == id, dynamic), gap = ui_px(ui, UI_GAP4), mid = y + h / 2;
        int first = (dynamic ? 16 : 12) * cell / 256, second = (dynamic ? 8 : 9) * cell / 256, across = first + gap + second;
        int start = ui_rtl() ? x + across : x + w - across;
        int rx = ui_rtl() ? start - first / 2 : start + first / 2, tx = ui_rtl() ? start - first - gap - second / 2 : start + first + gap + second / 2;
        struct compositor_pointer at_rest = {.dyn = dynamic, .scale = cell, .shape = CP_ARROW};
        struct compositor_pointer on_text = {.dyn = dynamic, .scale = cell, .shape = CP_TEXT};
        if (dynamic)
                at_rest.hw = at_rest.hh = at_rest.c = 256 * CP_DYN_RADIUS,
                on_text.hw = 256 * CP_DYN_TEXT_W, on_text.hh = 256 * CP_DYN_TEXT_H, on_text.c = 256 * CP_DYN_TEXT_R;
        settings_try_pointer_one(ui, &at_rest, dynamic ? rx : rx - first / 2, dynamic ? mid : mid - 21 * cell / 512);
        settings_try_pointer_one(ui, &on_text, tx, mid);
}

static fn settings_try_click_paint(struct ui address_to ui, int id, int x, int y, int w, int h)
{
        int high = ui_px(ui, 32), top = y + (h - high) / 2, wide;
        char said[32];
        struct ui_box pill = {x * 256, top * 256, (x + w) * 256, (top + high) * 256, high * 128, high * 128};
        (void)id;
        language_format(said, sizeof(said), WORDS(settings_try_ms), settings_click_try.apart);
        wide = ui_kit_measure(ui, (string_address)said, UI_LABEL_LARGE);
        ui_kit_fill(ui, &pill, ui_role(UI_PRIMARY), null);
        ui_kit_text(ui, x + (w - wide) / 2, ui_kit_baseline(ui, top + (high - ui_px(ui, ui_types[UI_LABEL_LARGE].line)) / 2, UI_LABEL_LARGE), (string_address)said, UI_LABEL_LARGE, ui_text_colour(ui, UI_ON_PRIMARY), 0);
}

static fn settings_try_describe(int id, struct ui_row address_to row)
{
        static char apart[32];
        int which = settings_try_of(id), beside = which >= 0 ? settings_row_of(settings_tries[which].path) : -1, scale = settings.ui.scale > 0 ? settings.ui.scale : 100;
        row->section = beside >= 0 ? settings_section_start(beside) : 0, row->header = beside >= 0 ? language_of(settings_header(beside)) : null;
        if (which < 0)
                return;
        row->label = language_text(settings_tries[which].label_id);
        if (settings_tries[which].about_id)
                row->about = language_text(settings_tries[which].about_id);
        if (which == SETTINGS_TRY_REPEAT)
        {
                if (settings_editing() == id)
                        row->kind = UI_ROW_FIELD, row->edit = &settings.edit, row->placeholder = WORDS(settings_try_repeat_hint);
                else
                        row->value = WORDS(settings_try_repeat_value);
        }
        else if (which == SETTINGS_TRY_CLICK)
        {
                language_format(apart, sizeof(apart), WORDS(settings_try_ms), settings_click_try.apart);
                if (settings_click_try.apart > 0 && settings_click_try.apart < settings_number_of(SETTING_DOUBLE_CLICK_PATH, 450) && settings.ui.regular)
                        row->picture_w = (ui_kit_measure(&settings.ui, (string_address)apart, UI_LABEL_LARGE) * 100 + scale - 1) / scale + 2 * UI_GAP4, row->paint = settings_try_click_paint;
                else
                        row->value = settings_click_try.apart > 0 ? apart : WORDS(settings_try_click_value);
        }
        else if (which == SETTINGS_TRY_POINTER)
        {
                int cell = settings_try_cell(scale, false, true);
                row->paint = settings_try_pointer_paint;
                row->picture_w = (SETTINGS_TRY_ACROSS * cell / 256 * 100 + scale - 1) / scale + UI_GAP4;
                row->picture = (SETTINGS_TRY_DOWN * cell / 256 * 100 + scale - 1) / scale + 2 * UI_GAP3;
        }
}

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
                settings_click_try.apart = (int)((now - settings_click_try.at) / 1000000ull), settings_click_try.apart += !settings_click_try.apart, settings_click_try.at = 0;
        else
                settings_click_try.at = now, settings_click_try.x = x, settings_click_try.y = y;
        ui_list_row_changed(&settings.ui, &settings.list, id);
}

static fn settings_try_changed(int row)
{
        for (int w = 0; w < SETTINGS_TRIES; w++)
                for (int k = 0; k < 6 && settings_tries[w].shows[k]; k++)
                        if (settings_row_is(row, settings_tries[w].shows[k]))
                                ui_list_row_changed(&settings.ui, &settings.list, SETTINGS_TRY + w);
}

static const struct { const char address_to url, address_to label, address_to target; unsigned int label_id, sub_id; } settings_about_specs[SETTINGS_ABOUT_COUNT] = {
    [SETTINGS_ABOUT_BRAND]   = {"https://dawning.dev", null, "dawning.dev", LANGUAGE_settings_about_made_by, 0},
    [SETTINGS_ABOUT_DISCORD] = {"https://discord.gg/nwvFVGYJhR", "Discord", "discord.gg/nwvFVGYJhR", 0, LANGUAGE_settings_about_discord},
    [SETTINGS_ABOUT_GITHUB]  = {"https://github.com/dawnlarsson/moonwater/issues", "GitHub", "github.com/dawnlarsson/moonwater/issues", 0, LANGUAGE_settings_about_github},
};

static int settings_about_of(int id) { int at; return settings_domain_of(id, &at) == SETTINGS_DOM_ABOUT ? at : -1; }
static const char address_to settings_about_url(int w) { return w >= 0 && w < SETTINGS_ABOUT_COUNT ? settings_about_specs[w].url : ""; }
static const char address_to settings_about_label(int w) { return w < 0 || w >= SETTINGS_ABOUT_COUNT ? "" : settings_about_specs[w].label ? settings_about_specs[w].label : language_text(settings_about_specs[w].label_id); }

static const char address_to settings_about_sub(int w, char address_to into, positive room)
{
        if (!room)
                return into;
        if (w < 0 || w >= SETTINGS_ABOUT_COUNT)
                into[0] = 0;
        else if (settings_about_specs[w].sub_id)
                language_format(into, room, language_text(settings_about_specs[w].sub_id), settings_about_specs[w].target);
        else
                string_copy_bounded((string_address)into, (string_address)settings_about_specs[w].target, room);
        return into;
}

static b32 settings_about_matches_word(int w, const char address_to word, positive size)
{
        char sub[SETTINGS_SUB_TEXT];
        return settings_has(WORDS(settings_about_header), word, size) || settings_has(settings_about_label(w), word, size) ||
               settings_has(settings_about_sub(w, sub, sizeof(sub)), word, size) || settings_has(settings_about_url(w), word, size);
}

static inline unsigned int settings_logo_sample(int sx, int sy)
{
        positive at = (positive)(SETTINGS_LOGO_Y0 + (sy < 0 ? 0 : sy >= SETTINGS_LOGO_SRC_H ? SETTINGS_LOGO_SRC_H - 1 : sy)) * SPLASH_MARK_W +
                      (positive)(SETTINGS_LOGO_X0 + (sx < 0 ? 0 : sx >= SETTINGS_LOGO_SRC_W ? SETTINGS_LOGO_SRC_W - 1 : sx));
        return ((unsigned int)(splash_mark[at >> 2] >> (6 - 2 * (at & 3))) & 3u) * 85u;
}

static fn settings_about_logo_paint(struct ui address_to ui, int id, int x, int y, int w, int h)
{
        int max_h = h - 4 < SETTINGS_LOGO_MAX_H ? h - 4 : SETTINGS_LOGO_MAX_H, draw_w = w < SETTINGS_LOGO_MAX_W ? w : SETTINGS_LOGO_MAX_W, draw_h, dx, dy;
        (void)id;
        if (!settings_large || draw_w <= 0 || max_h <= 0)
                return;
        draw_h = draw_w * SETTINGS_LOGO_SRC_H / SETTINGS_LOGO_SRC_W;
        if (draw_h > max_h)
                draw_h = max_h, draw_w = draw_h * SETTINGS_LOGO_SRC_W / SETTINGS_LOGO_SRC_H;
        if (draw_w <= 0 || draw_h <= 0)
                return;
        for (int py = 0; py < draw_h; py++)
        {
                int fy = ((2 * py + 1) * (SETTINGS_LOGO_SRC_H << 8)) / (2 * draw_h) - 128, y0, y1, wy;
                fy = fy < 0 ? 0 : fy > ((SETTINGS_LOGO_SRC_H - 1) << 8) ? ((SETTINGS_LOGO_SRC_H - 1) << 8) : fy;
                y0 = fy >> 8, y1 = y0 + (y0 + 1 < SETTINGS_LOGO_SRC_H), wy = fy & 255;
                for (int px = 0; px < draw_w; px++)
                {
                        int fx = ((2 * px + 1) * (SETTINGS_LOGO_SRC_W << 8)) / (2 * draw_w) - 128, x0, x1, wx;
                        unsigned int top, bot;
                        fx = fx < 0 ? 0 : fx > ((SETTINGS_LOGO_SRC_W - 1) << 8) ? ((SETTINGS_LOGO_SRC_W - 1) << 8) : fx;
                        x0 = fx >> 8, x1 = x0 + (x0 + 1 < SETTINGS_LOGO_SRC_W), wx = fx & 255;
                        top = settings_logo_sample(x0, y0) * (unsigned int)(256 - wx) + settings_logo_sample(x1, y0) * (unsigned int)wx;
                        bot = settings_logo_sample(x0, y1) * (unsigned int)(256 - wx) + settings_logo_sample(x1, y1) * (unsigned int)wx;
                        settings_large->logo_cover[(positive)py * (positive)draw_w + (positive)px] = (unsigned char)((top * (unsigned int)(256 - wy) + bot * (unsigned int)wy + 32768u) >> 16);
                }
        }
        dx = ui_rtl() ? x : x + w - draw_w, dy = y + (h - draw_h) / 2;
        UI_KEEP(dx, dy, draw_w, draw_h);
        ui_kit_covers(ui, dx, dy, draw_w, draw_h, settings_large->logo_cover, draw_w, ui_text_colour(ui, UI_ON_SURFACE));
}

static fn settings_about_describe(int id, struct ui_row address_to row)
{
        static char about_text[SETTINGS_ABOUT_COUNT][SETTINGS_SUB_TEXT];
        int w = settings_about_of(id);
        if (w < 0 || w >= SETTINGS_ABOUT_COUNT)
                return;
        row->section = SETTINGS_ABOUT, row->header = WORDS(settings_about_header), row->label = settings_about_label(w);
        row->about = settings_about_sub(w, about_text[w], sizeof(about_text[w]));
        if (w == SETTINGS_ABOUT_BRAND)
                row->picture_w = SETTINGS_LOGO_W, row->picture = SETTINGS_LOGO_ROW_H, row->paint = settings_about_logo_paint;
        else
                row->kind = UI_ROW_BUTTON, row->words = WORDS(settings_button_open);
}

static b32 settings_browser_match(const char address_to text)
{
        static const char address_to const names[] = {"chromium", "chrome", "firefox", "librewolf", "brave", "vivaldi", "zen", "epiphany", "falkon", "qutebrowser", "floorp", "waterfox"};
        for (positive k = 0; text && text[0] && k < sizeof(names) / sizeof(names[0]); k++)
                if (settings_has(text, names[k], string_length((string_address)names[k])))
                        return true;
        return false;
}

static fn settings_about_press(int id)
{
        int w = settings_about_of(id), browser = -1;
        const char address_to url = settings_about_url(w);
        if (w < 0)
                return;
        ui_apps_fresh((b64)ui_kit_time());
        for (int at = 0; at < ui_apps.count; at++)
        {
                const struct ui_app_entry address_to app = &ui_apps.app[at];
                if (app->kit || app->terminal || !app->launch[0])
                        continue;
                if (string_equals((string_address)app->profile, (string_address) "chrome")) { browser = at; break; }
                if (browser < 0 && (settings_browser_match(app->id) || settings_browser_match(app->key) || settings_browser_match(ui_apps_base(app->launch))))
                        browser = at;
        }
        if (browser >= 0)
        {
                string_address words[UI_APP_START_WORDS + 2];
                positive count = ui_apps_words_of(browser, words, null, null, null);
                if (count > 0 && count + 1 < sizeof(words) / sizeof(words[0]))
                {
                        words[count++] = (string_address)url, words[count] = null;
                        if (ui_start_apart(words, count))
                                return settings_say((string_address)WORDS(settings_opened));
                }
        }
        settings_offered(windows_clip_offer(&settings.own, WINDOWS_CLIP_CLIPBOARD, url, string_length((string_address)url)) == 0);
}

#define SETTINGS_FNV64_SEED 1469598103934665603ull
#define SETTINGS_FNV64_STEP(h, v) (((h) ^ (positive)(v)) * 1099511628211ull)
static positive settings_layout_seen;

static positive settings_fnv64_bytes(positive hash, const void address_to data, positive size)
{
        const unsigned char address_to b = (const unsigned char address_to)data;
        for (positive at = 0; at < size; at++)
                hash = SETTINGS_FNV64_STEP(hash, b[at]);
        return hash;
}

static fn settings_layout_check(void)
{
        positive key = SETTINGS_FNV64_SEED;
        for (int at = 0; at < settings.list.count; at++)
                key = SETTINGS_FNV64_STEP(key, settings.list.id[at]);
        for (int list = 0; list < SETTINGS_LISTS; list++)
        {
                key = SETTINGS_FNV64_STEP(key, settings.item_count[list]);
                for (int at = 0; at < settings.item_count[list]; at++)
                        key = settings_fnv64_bytes(key, settings.item[list][at].label, string_length((string_address)settings.item[list][at].label));
        }
        if (key != settings_layout_seen && settings.page.press.down)
                ui_page_let_go(&settings.ui, &settings.page, &settings.list);
        settings_layout_seen = key;
}

static int settings_try_emit(int row, b32 before, int count)
{
        for (int w = 0; w < SETTINGS_TRIES && !settings_filter_length(); w++)
                if (settings_tries[w].before == before && settings_row_is(row, settings_tries[w].path) && ui_list_add(&settings.list, SETTINGS_TRY + w))
                        count++;
        return count;
}

static fn settings_rebuild(void)
{
        int count = 0;
        b32 filtered = settings_filter_length() != 0;
        string_copy_bounded((string_address)settings_metric_shown, (string_address)settings_metric_line, sizeof(settings_metric_shown));
        settings_sub_count = 0, settings_sub_cut = 0;
        ui_list_begin(&settings.list);
        for (positive row = 0; row < settings_rows_count(); row++)
                if (filtered ? settings_matches((int)row) : settings_group_of((int)row) == settings.page.chosen)
                {
                        count = settings_try_emit((int)row, true, count);
                        if (ui_list_add(&settings.list, (int)row))
                                count++;
                        count = settings_try_emit((int)row, false, settings_sub_rows((int)row, count));
                }
        for (int list = 0; list < SETTINGS_LISTS; list++)
        {
                int group = settings_group_of(SETTINGS_LIST_ID(list, 0));
                for (int at = 0; at < settings.item_count[list]; at++)
                {
                        int row = SETTINGS_LIST_ID(list, at);
                        if ((filtered ? settings_matches(row) : group == settings.page.chosen) && ui_list_add(&settings.list, row))
                                count = settings_link_rows(row, settings_drive_rows(row, count + 1));
                }
        }
        for (int w = 0; w < SETTINGS_ABOUT_COUNT; w++)
                if ((filtered ? settings_matches(SETTINGS_ABOUT + w) : settings.page.chosen == settings_system_group()) && ui_list_add(&settings.list, SETTINGS_ABOUT + w))
                        count++;
        settings_sort_runs();
        ui_list_end(&settings.list);
        if (settings_editing() >= 0 && settings_pos(settings_editing()) < 0)
                settings_edit_end();
        if (settings.page.menu_row >= 0 && settings_pos(settings.page.menu_row) < 0)
                settings.page.menu_row = -1, ui_popmenu_close(&settings.popup);
        settings_layout();
        settings_layout_check();
        ui_page_redraw(&settings.page);
}

static fn settings_refilter(void) { settings_rebuild(); ui_page_reveal(&settings.ui, &settings.page, &settings.list); }

/* ---- help parser, command builder, card & verb job runner ---- */

static positive settings_verb_length(const char address_to command)
{
        positive at = 0, last = 0, word;
        for (;;)
        {
                for (word = 0; command[at + word] >= 'a' && command[at + word] <= 'z'; word++)
                        ;
                if (!word || (command[at + word] && command[at + word] != ' '))
                        break;
                last = at + word, at = last + (command[last] == ' ');
                if (!command[last])
                        break;
        }
        return last;
}

static unsigned int settings_pool_put(const char address_to text, positive size)
{
        unsigned int at = (unsigned int)settings.pool_used;
        if (settings.pool_used + size + 1 > sizeof(settings_large->pool))
                return SETTINGS_NONE;
        memory_copy(settings_large->pool + at, text, size), settings_large->pool[at + size] = 0, settings.pool_used += size + 1;
        return at;
}

static fn settings_help(const char address_to said, positive length)
{
        char line[SETTINGS_PROBE];
        positive at = 0;
        settings.generic_count = 0, settings.pool_used = 0, settings.truncated = false, settings.last_text = SETTINGS_NONE;
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
                        const char address_to apart = (const char address_to)(memory_search)((address_any)(line + 2), size - 2, (address_any) "  ", 2);
                        positive cmd_len = apart ? (positive)(apart - (line + 2)) : size - 2, text_at = cmd_len, text_len;
                        const char address_to bar;
                        unsigned int first, second = SETTINGS_NONE, desc;
                        positive rows;
                        while (apart && 2 + text_at < size && line[2 + text_at] == ' ')
                                text_at++;
                        text_len = size - 2 - text_at;
                        bar = cmd_len >= 3 ? (const char address_to)(memory_search)((address_any)(line + 2), cmd_len, (address_any) " | ", 3) : null;
                        rows = bar ? 2 : 1;
                        if (settings.generic_count + rows > SETTINGS_GENERATED || settings.pool_used + cmd_len + text_len + 8 > sizeof(settings_large->pool) || cmd_len > 120)
                                settings.truncated = true, settings.last_text = SETTINGS_NONE;
                        else
                        {
                                first = settings_pool_put(line + 2, bar ? (positive)(bar - (line + 2)) : cmd_len);
                                if (bar)
                                        second = settings_pool_put(bar + 3, cmd_len - (positive)(bar - (line + 2)) - 3);
                                desc = settings_pool_put(line + 2 + text_at, text_len);
                                for (positive e = 0; e < rows; e++)
                                {
                                        struct settings_generic address_to g = &settings.generic[settings.generic_count++];
                                        g->command = (unsigned short)(e ? second : first), g->text = (unsigned short)desc;
                                        g->verb = (unsigned char)settings_verb_length(settings_large->pool + g->command);
                                }
                                settings.last_text = desc;
                        }
                }
                else if (indent >= 8 && settings.last_text != SETTINGS_NONE && settings.pool_used + (size - indent) + 2 <= sizeof(settings_large->pool))
                {
                        positive dst = settings.pool_used - 1, add = size - indent;
                        if (settings_large->pool[settings.last_text])
                                settings_large->pool[dst++] = ' ';
                        memory_copy(settings_large->pool + dst, line + indent, add), settings_large->pool[dst + add] = 0, settings.pool_used = dst + add + 1;
                }
        }
}

static positive settings_command(const struct settings_row address_to row, string_address command, const char address_to value, string_address address_to words, positive room, b32 asking)
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
        const struct settings_generic address_to g = settings_generic_of(row);
        positive used = 0;
        if (room < 4)
                return 0;
        if (g)
                used = g->verb, memory_copy(joined, settings_large->pool + g->command, used), joined[used++] = ' ';
        string_copy_bounded((string_address)joined + used, (string_address)args, sizeof(joined) - used);
        words[0] = settings.moonwater;
        return settings_tokens(joined, settings_large->split, sizeof(settings_large->split), words, 1, room);
}

static b32 settings_asks_for(string_address address_to words, positive count)
{
        for (positive e = 0; e < sizeof(settings_asks) / sizeof(settings_asks[0]); e++)
        {
                positive at = 0;
                while (at < 3 && settings_asks[e][at] && at < count && string_equals(words[at], (string_address)settings_asks[e][at]))
                        at++;
                if (at == 3 || !settings_asks[e][at])
                        return true;
        }
        return false;
}

static b32 settings_row_asks(int row)
{
        const struct settings_generic address_to g = settings_generic_of(row);
        char head[SETTINGS_VALUE];
        string_address words[SETTINGS_WORDS];
        positive count;
        if (!g)
                return false;
        settings_slice(head, sizeof(head), settings_large->pool + g->command, g->verb);
        count = settings_tokens(head, settings_large->split, sizeof(settings_large->split), words, 0, SETTINGS_WORDS);
        return count && settings_asks_for(words, count);
}

static fn settings_card_fill(void)
{
        char line[SETTINGS_PROBE];
        positive at = 0, made = 0;
        while (at < settings.runner.length)
        {
                positive size = settings_keep(settings_large->said, settings.runner.length, &at, line, sizeof(line)), skip = settings_label_length(line, size);
                while (size > skip && line[size - 1] == ' ')
                        size--;
                if (made + (size - skip) + 1 >= sizeof(settings_large->card))
                        break;
                memory_copy(settings_large->card + made, line + skip, size - skip), made += size - skip, settings_large->card[made++] = '\n';
        }
        while (made && settings_large->card[made - 1] == '\n')
                made--;
        if (settings.runner.cut_lines && made + SETTINGS_ITEM_LABEL < sizeof(settings_large->card))
        {
                char note[SETTINGS_ITEM_LABEL];
                positive sz;
                language_format(note, sizeof(note), WORDS(settings_not_shown), (long)settings.runner.cut_lines);
                sz = string_length((string_address)note);
                if (made)
                        settings_large->card[made++] = '\n';
                memory_copy(settings_large->card + made, note, sz), made += sz;
        }
        ui_lines_set(&settings.card_text, settings_large->card, made, settings_large->card_at, settings_large->card_size, SETTINGS_CARD_LINES);
}

static fn settings_card_open(const char address_to title)
{
        settings_card_fill();
        string_copy_bounded((string_address)settings.card_title, (string_address)title, sizeof(settings.card_title));
        settings.page.card = true, settings.card.scroll = 0;
        ui_lines_wrap(&settings.ui, &settings.card_text, settings.width);
        settings_layout(), ui_page_redraw(&settings.page);
}

static fn settings_card_leave(void) { settings.page.card = false, settings.status[0] = 0; settings_layout(); ui_page_redraw(&settings.page); }
static fn settings_say(string_address text) { string_copy_bounded((string_address)settings.status, text, sizeof(settings.status)); }
static fn settings_complete(b32 killed, positive status);
static fn settings_waiting_say(void);
static b32 settings_waits(int row);
static fn settings_feed_drop(void);
static fn settings_job_done(struct ui_job address_to j, b32 killed, positive status) { (void)j; settings_complete(killed, status); }
static fn settings_job_data(struct ui_job address_to j) { (void)j; settings_waiting_say(); }
static fn settings_runner_ready(void) { settings.runner.done = settings_job_done, settings.runner.data = settings_job_data; }

static b32 settings_start(string_address address_to words, positive count, const char address_to input)
{
        positive limit = settings.job_sets && settings.job >= SETTINGS_ANY ? 0
                         : settings.job == SETTINGS_JOB_FEED ? (positive)SETTINGS_FEED_WAIT_NS
                         : settings.job == SETTINGS_JOB_ACT ? (positive)SETTINGS_ACT_WAIT_NS
                         : settings_waits(settings.job) ? (positive)SETTINGS_WAIT_PAIR_NS : (positive)SETTINGS_WAIT_NS;
        settings_runner_ready();
        return ui_job_start(&settings.runner, words, count, input, settings_large->said, sizeof(settings_large->said), limit);
}

static fn settings_join(string_address address_to words, positive count, char address_to text, positive room)
{
        positive at = settings_cat(text, room, 0, "moonwater");
        for (positive i = 1; i < count; i++)
                at = settings_cat_sep(text, room, at, " ", (const char address_to)words[i]);
}

static const struct settings_act_spec { char code; b32 takes_pass, takes_arg; const char address_to radio, address_to how; } settings_act_specs[] = {
    {'j', true, false, "wifi", "add"}, {'f', false, false, "wifi", "remove"}, {'a', false, false, "bluetooth", "add"}, {'d', false, false, "bluetooth", "remove"},
    {'v', false, false, "device", "allow"}, {'x', false, false, "device", "deny"}, {'r', false, false, "link", "remove"},
    {'l', false, true, "link", "allow"}, {'n', false, true, "link", "deny"},
    {'L', false, false, "link", "allow-all"}, {'N', false, false, "link", "deny-all"},
};

static const char address_to settings_act_words(string_address address_to words)
{
        char address_to what = settings.job_value, address_to name, address_to pass = null;
        const struct settings_act_spec address_to spec = null;
        words[0] = null;
        if (!what[0] || what[1] != '\n')
                return null;
        for (positive at = 0; at < sizeof(settings_act_specs) / sizeof(settings_act_specs[0]); at++)
                if (settings_act_specs[at].code == what[0]) { spec = &settings_act_specs[at]; break; }
        if (!spec || !(name = what + 2)[0])
                return null;
        for (char address_to at = name; *at; at++)
                if (*at == '\n') { *at = 0, pass = at + 1; break; }
        words[0] = settings.moonwater, words[1] = (string_address)spec->radio, words[2] = (string_address)spec->how;
        words[3] = (string_address)name, words[4] = null, words[5] = null;
        if (spec->takes_arg)
        {
                if (!pass || !pass[0])
                        return words[0] = null, null;
                words[4] = (string_address)pass;
                return null;
        }
        pass = spec->takes_pass && pass && pass[0] ? pass : null;
        words[4] = pass ? (string_address) "-" : null;
        return pass;
}

static b32 settings_same_verb(const struct settings_row address_to a, const struct settings_row address_to b)
{
        for (positive at = 0; at < SETTING_WORDS; at++)
                if (a->read[at] != b->read[at] && (!a->read[at] || !b->read[at] || !string_equals((string_address)a->read[at], (string_address)b->read[at])))
                        return false;
        return true;
}

static void settings_unread_set(positive row, int on) { positive b = (positive)1 << (row % 64); if (on) settings.unread[row / 64] |= b; else settings.unread[row / 64] &= ~b; }
static int settings_unread_is(positive row) { return (int)((settings.unread[row / 64] >> (row % 64)) & 1); }
static int settings_unread_any(void) { for (positive w = 0; w < SETTINGS_UNREAD_WORDS; w++) if (settings.unread[w]) return 1; return 0; }
static b32 settings_action_row(int row) { return row >= 0 && row < SETTINGS_ANY && settings_rows[row].kind == SETTINGS_ACTION; }

static positive settings_not_understood(void)
{
        positive failed = 0;
        for (positive at = 0; at < SETTINGS_ROWS; at++)
                if (settings_rows[at].output == SETTINGS_OUT_NONE)
                        failed += settings.state[at] == SETTINGS_FAILED || settings.state[at] == SETTINGS_UNREAD;
        return failed;
}

static fn settings_next_job(void)
{
        string_address words[SETTINGS_WORDS];
        positive count;
        while (settings.runner.child <= 0)
        {
                if (settings.queued)
                {
                        settings.job = settings.queue[0].row, settings.job_sets = true;
                        memory_copy(settings.job_value, settings.queue[0].value, sizeof(settings.job_value));
                        memory_copy(settings.queue, settings.queue + 1, (--settings.queued) * sizeof(settings.queue[0]));
                        settings_wipe(&settings.queue[settings.queued], sizeof(settings.queue[0]));
                }
                else if (settings_unread_any())
                {
                        positive first = 0;
                        while (!settings_unread_is(first)) first++;
                        settings.job = (int)first, settings.job_sets = false, settings_unread_set(first, 0);
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
                        b32 started = words[0] && settings_start(words, words[4] ? 5 : 4, pass);
                        settings_wipe(settings.job_value, sizeof(settings.job_value));
                        if (started) return;
                        count = 0;
                }
                else if (settings.job >= SETTINGS_ANY)
                        count = settings_generic_words(settings.job, settings.job_value, words, SETTINGS_WORDS);
                else
                        count = settings_command(&settings_rows[settings.job], settings.moonwater, settings.job_value, words, SETTINGS_WORDS, !settings.job_sets);
                if (count && settings_start(words, count, null))
                {
                        settings_wipe(settings_large->split, sizeof(settings_large->split));
                        if (settings.job >= SETTINGS_ANY)
                        {
                                char text[SETTINGS_STATUS - 24], line[SETTINGS_STATUS];
                                settings_join(words, count, text, sizeof(text)), language_format(line, sizeof(line), WORDS(settings_running), text), settings_say((string_address)line);
                        }
                        return;
                }
                settings_wipe(settings.job_value, sizeof(settings.job_value));
                settings_wipe(settings_large->split, sizeof(settings_large->split));
                if (settings.job_sets) settings_say((string_address)(count || settings.job < SETTINGS_ANY ? WORDS(settings_cannot_run) : WORDS(settings_too_many)));
                else if (settings.job >= 0) settings.state[settings.job] = SETTINGS_FAILED;
                ui_page_redraw(&settings.page);
        }
}

static fn settings_ask_all(void)
{
        memory_zero(settings.unread, sizeof(settings.unread));
        for (positive r = 0; r < SETTINGS_ROWS; r++)
                if (settings_rows[r].output == SETTINGS_OUT_NONE) { if (settings_rows[r].read[0]) settings_unread_set(r, 1); else settings.state[r] = SETTINGS_READ; }
}

static positive settings_slot(int row) { positive at = 0; while (at < settings.queued && settings.queue[at].row != row) at++; return at; }
static b32 settings_action_busy(int row) { return settings_slot(row) < settings.queued || (settings.runner.child > 0 && settings.job == row && settings.job_sets); }

static b32 settings_refused(int row, const char address_to value)
{
        b32 delib = false;
        if (settings.hand) return false;
        if (row == SETTINGS_JOB_ACT)
                delib = value && (value[0] == 'v' || value[0] == 'l' || value[0] == 'L');
        else if (row >= 0 && row < SETTINGS_ANY)
                delib = settings_rows[row].deliberate && (settings_rows[row].kind != SETTINGS_SWITCH || string_equals((string_address)value, (string_address) "on"));
        else if (row >= SETTINGS_ANY && row < (int)SETTINGS_LIST)
        {
                string_address words[SETTINGS_WORDS];
                positive count = settings_generic_words(row, value, words, SETTINGS_WORDS);
                if (count >= 3 &&
                    ((string_equals(words[1], (string_address) "device") && string_equals(words[2], (string_address) "allow")) ||
                     (string_equals(words[1], (string_address) "link") &&
                      (string_equals(words[2], (string_address) "allow") || string_equals(words[2], (string_address) "allow-all")))))
                        delib = true;
                for (positive at = 0; at < SETTINGS_ROWS && !delib; at++)
                {
                        const struct settings_row address_to w = &settings_rows[at];
                        positive n = 0;
                        b32 same = w->deliberate;
                        while (same && n < SETTING_WORDS && w->set[n])
                                same = 1 + n < count && string_equals(words[1 + n], (string_address)w->set[n]), n++;
                        delib = same && n && (w->kind != SETTINGS_SWITCH || (count >= 2 + n && string_equals(words[1 + n], (string_address) "on")));
                }
                settings_wipe(settings_large->split, sizeof(settings_large->split));
        }
        if (delib) settings_say((string_address)WORDS(settings_virtual_refused));
        return delib;
}

static fn settings_change(int row, const char address_to value)
{
        positive at = 0;
        if (settings_refused(row, value)) return;
        settings_feed_drop();
        if (row >= 0 && row < SETTINGS_ANY) at = settings_slot(row);
        else while (at < settings.queued && !(settings.queue[at].row == row && string_equals((string_address)settings.queue[at].value, (string_address)value))) at++;
        if (at == settings.queued)
        {
                if (!array_store_reserve(settings.queue, settings.queue_room, settings.queued, settings.queued + 1, 8)) return settings_say((string_address)WORDS(settings_busy));
                settings.queue[at].row = row, settings.queued++;
        }
        string_copy_bounded((string_address)settings.queue[at].value, (string_address)value, sizeof(settings.queue[at].value));
        settings_say((string_address) "..."), settings_next_job();
        if (settings_action_row(row)) ui_page_redraw(&settings.page);
}

static fn settings_report(b32 killed, positive status)
{
        char first[SETTINGS_STATUS], other[SETTINGS_STATUS], title[SETTINGS_STATUS];
        string_address words[SETTINGS_WORDS];
        positive at = 0, lines = 0;
        while (at < settings.runner.length) lines += settings_line(settings_large->said, settings.runner.length, &at, lines ? other : first, SETTINGS_STATUS) != 0;
        if (!lines) settings_say((string_address)(killed ? WORDS(settings_stopped) : wait_status_code(status) ? WORDS(settings_refused) : WORDS(settings_done)));
        else if (lines == 1) settings_say((string_address)first);
        else settings_join(words, settings_generic_words(settings.job, settings.job_value, words, SETTINGS_WORDS), title, sizeof(title)), settings_card_open(title), settings_say((string_address)(killed ? WORDS(settings_card_stopped) : WORDS(settings_card_back)));
}

/* ---- live feeds, drives, partitions & sortable columns ---- */

struct settings_cursor { const char address_to s; positive n; };
static inline b32 sc_eat(struct settings_cursor address_to c, const char address_to p) { positive l = string_length((string_address)p); return c->n >= l && !memory_compare(c->s, p, l) ? (c->s += l, c->n -= l, true) : false; }
static inline fn sc_spaces(struct settings_cursor address_to c) { while (c->n && c->s[0] == ' ') c->s++, c->n--; }
static inline unsigned long long sc_u64(struct settings_cursor address_to c, unsigned long long cap) { unsigned long long v = 0; for (; c->n && c->s[0] >= '0' && c->s[0] <= '9'; c->s++, c->n--) if (v <= cap) v = v * 10 + (unsigned long long)(c->s[0] - '0'); return v; }
static inline const char address_to sc_word(struct settings_cursor address_to c, positive address_to len) { const char address_to st = c->s; positive n = 0; while (n < c->n && c->s[n] != ' ') n++; c->s += n, c->n -= n; if (len) *len = n; return st; }

static positive settings_find_last(const char address_to line, positive length, const char address_to key)
{
        positive size = string_length((string_address)key), found = length;
        for (positive at = 0; at + size <= length; at++)
                if (!memory_compare(line + at, key, size))
                        found = at;
        return found;
}

static inline fn sc_trim_at(struct settings_cursor address_to c, const char address_to needle)
{
        positive at = settings_find_last(c->s, c->n, needle);
        if (at < c->n)
                c->n = at;
        while (c->n && c->s[c->n - 1] == ' ')
                c->n--;
}

static fn settings_item_label(struct settings_item address_to item, const char address_to name, positive size) { settings_slice(item->label, sizeof(item->label), name, size); }
static fn settings_item_swap(struct settings_item address_to a, struct settings_item address_to b) { struct settings_item tmp = *a; *a = *b, *b = tmp; }

static struct settings_item address_to settings_item_slot(int list, positive at)
{
        if (list < 0 || list >= SETTINGS_LISTS || at >= (positive)SETTINGS_LIST_STEP)
                return null;
        if (at >= settings.item_room[list])
                if (!array_store_reserve(settings.item[list], settings.item_room[list], at, at + 1, 32))
                        return null;
        memory_zero(&settings.item[list][at], sizeof(settings.item[list][at]));
        settings.item[list][at].drive_idx = -1;
        return &settings.item[list][at];
}

static fn settings_note_add(int list, positive address_to count, unsigned char where, const char address_to text, positive size)
{
        struct settings_item address_to it = settings_item_slot(list, *count);
        if (it)
                it->kind = SETTINGS_NOTE, it->where = where, settings_item_label(it, text, size), (*count)++;
}

static b32 settings_joinable(const char address_to security)
{
        static const char address_to const ok[] = {"open", "WPA2", "WPA2/3", "WPA3", "PMF"};
        for (positive k = 0; k < sizeof(ok) / sizeof(ok[0]); k++)
                if (string_equals((string_address)security, (string_address)ok[k]))
                        return true;
        return false;
}

static b32 settings_feed_line(positive address_to at, char address_to line, positive room, char address_to address_to text, positive address_to size)
{
        positive length, skip;
        if (*at >= settings.runner.length)
                return false;
        length = settings_keep(settings_large->said, settings.runner.length, at, line, room), skip = settings_label_length(line, length);
        while (length > skip && line[length - 1] == ' ')
                length--;
        line[length] = 0, *text = line + skip, *size = length - skip;
        return true;
}

static fn settings_feed_wifi(void)
{
        enum { WIFI_CAP = 256 };
        positive nets_bytes = ((positive)WIFI_CAP * sizeof(struct ui_wifi_net) + 4095) & ~(positive)4095;
        struct ui_wifi_net address_to nets = (struct ui_wifi_net address_to)settings_alloc_pages(nets_bytes);
        char note[SETTINGS_ITEM_LABEL] = {0};
        int n = nets ? ui_wifi_parse(settings_large->said, settings.runner.length, nets, WIFI_CAP, null, note, sizeof(note)) : 0;
        positive count = 0;
        for (int i = 0; i < n; i++)
        {
                struct settings_item address_to it = settings_item_slot(0, count);
                if (!it) break;
                count++;
                it->kind = SETTINGS_NETWORK, it->dbm = nets[i].dbm, it->bars = nets[i].bars;
                string_copy_bounded((string_address)it->label, (string_address)nets[i].name, sizeof(it->label));
                string_copy_bounded((string_address)it->security, (string_address)nets[i].security, sizeof(it->security));
                it->flags = (nets[i].flags & UI_WIFI_HEARD ? SETTINGS_ITEM_HEARD : 0) | (nets[i].flags & UI_WIFI_JOINED ? SETTINGS_ITEM_JOINED : 0) |
                            (nets[i].flags & UI_WIFI_SAVED ? SETTINGS_ITEM_SAVED : 0) | (nets[i].flags & UI_WIFI_OPEN ? SETTINGS_ITEM_OPEN : 0) |
                            ((nets[i].flags & UI_WIFI_HEARD) && !settings_joinable(it->security) ? SETTINGS_ITEM_CANNOT : 0);
        }
        if (nets)
                system_call_2(syscall(munmap), (positive)nets, nets_bytes);
        if (note[0])
                settings_note_add(0, &count, 0, note, string_length((string_address)note));
        settings.item_count[0] = (int)count;
}

static fn settings_feed_devices(void)
{
        char line[SETTINGS_PROBE], address_to text;
        const char address_to keep = language_of("Keep a device by its name");
        positive at, size, count = 0;
        for (at = 0; settings_feed_line(&at, line, sizeof(line), &text, &size);)
        {
                struct settings_cursor c = {text, size};
                if (sc_eat(&c, "  ") && c.n && c.s[0] != ' ')
                {
                        struct settings_item address_to it = settings_item_slot(1, count);
                        if (!it) break;
                        it->kind = SETTINGS_DEVICE, it->flags = SETTINGS_ITEM_SAVED, settings_item_label(it, c.s, c.n), count++;
                }
        }
        {
                struct settings_item address_to add = settings_item_slot(1, count);
                if (add)
                        add->kind = SETTINGS_ADD, settings_item_label(add, keep, string_length((string_address)keep)), count++;
        }
        settings.item_count[1] = (int)count;
}

static int settings_role_code(const char address_to text, positive length)
{
        static const char address_to const words[] = {"efi", "linux", "swap", "other", "free", "unpartitioned"};
        for (int at = 0; at < 6; at++)
                if (length == string_length((string_address)words[at]) && !memory_compare(text, words[at], length))
                        return at;
        return -1;
}

static const char address_to settings_role_word(unsigned int role)
{
        static const unsigned int ids[] = {LANGUAGE_settings_role_efi, LANGUAGE_settings_role_linux, LANGUAGE_settings_role_swap, LANGUAGE_settings_role_other, LANGUAGE_settings_role_free, LANGUAGE_settings_role_unpartitioned};
        return language_text(ids[role < 5 ? role : 5]);
}

static fn settings_size_text(unsigned long long bytes, char address_to into, positive room) { ui_size_decimal(bytes, into, room); }

static b32 settings_name_says(const char address_to name, const char address_to word)
{
        positive len = string_length((string_address)word), nlen = string_length((string_address)name);
        for (positive at = 0; len && at + len <= nlen; at++)
                if (!memory_compare_ascii_case(name + at, word, len))
                        return true;
        return false;
}

static char settings_device_note[SETTINGS_ITEM_LABEL], settings_device_port_cell[SETTINGS_ITEM_LABEL], settings_drive_share[SETTINGS_ITEM_LABEL], settings_drive_amount[2 * SETTINGS_SIZE_TEXT + 16];
static char settings_part_size[SETTINGS_SIZE_TEXT], settings_part_share[SETTINGS_ITEM_LABEL], settings_part_label[SETTINGS_ITEM_LABEL];

static long settings_share_of(unsigned long long part, unsigned long long whole, unsigned long long scale)
{
        while (whole >= 1ull << 40)
                whole >>= 1, part >>= 1;
        return whole ? (long)((part * scale + whole / 2) / whole) : 0;
}

static unsigned long long settings_drive_free(const struct settings_item address_to it)
{
        unsigned long long f = 0;
        for (positive at = 0; at < it->segment_count; at++)
                f += it->segment_role[at] == UI_STRIP_FREE ? it->segment_bytes[at] : 0;
        return f;
}

static fn settings_drive_strip(const struct settings_item address_to item, struct ui_row address_to row)
{
        char used_text[SETTINGS_SIZE_TEXT], total_text[SETTINGS_SIZE_TEXT];
        unsigned long long free, used;
        if (!item->segment_count || !item->segment_role)
                return;
        row->tall = true, row->strip_bytes = item->segment_bytes, row->strip_roles = item->segment_role, row->strip_count = item->segment_count;
        free = settings_drive_free(item), used = item->total > free ? item->total - free : 0;
        settings_size_text(item->total, total_text, sizeof(total_text)), settings_size_text(used, used_text, sizeof(used_text));
        if (item->segment_count == 1 && item->segment_role[0] == UI_STRIP_UNPARTITIONED)
                string_copy_bounded((string_address)settings_drive_share, (string_address)WORDS(settings_role_unpartitioned), sizeof(settings_drive_share)),
                string_copy_bounded((string_address)settings_drive_amount, (string_address)total_text, sizeof(settings_drive_amount));
        else
        {
                long pct = settings_share_of(used, item->total, 100);
                language_format(settings_drive_share, sizeof(settings_drive_share), WORDS(settings_partitioned), free && pct > 99 ? 99l : pct);
                language_format(settings_drive_amount, sizeof(settings_drive_amount), WORDS(settings_used_of), used_text, total_text);
        }
        row->share = settings_drive_share, row->amount = settings_drive_amount;
}

static b32 settings_item_storage(const struct settings_item address_to item) { return item && (item->flags & (SETTINGS_ITEM_DRIVE | SETTINGS_ITEM_DISK)); }
static const char address_to settings_drive_name(const struct settings_item address_to item) { return item->flags & SETTINGS_ITEM_DISK ? item->port : item->label; }
static int settings_partitions_of(const struct settings_item address_to it) { int c = 0; for (positive at = 0; at < it->segment_count; at++) c += it->segment_role[at] < UI_STRIP_FREE; return c; }
static int settings_partitions_owner(int row) { int at; return settings_domain_of(row, &at) == SETTINGS_DOM_SUB && at < settings_sub_count && settings_sub[at].action == SETTINGS_SUB_PARTITIONS ? settings_sub[at].owner : -1; }

static int settings_drive_rows(int row, int count)
{
        const struct settings_item address_to item = settings_item(row);
        int section = settings_section_start(row), next;
        b32 free = false;
        if (!settings_item_storage(item) || !(settings_partitions_of(item) || item->segment_more))
                return count;
        settings_sub_owner = row;
        if ((next = settings_sub_add(section, false, null, "", 0, SETTINGS_SUB_PARTITIONS, 0, count)) == count)
                return count;
        count = next;
        if (!settings_lines_listed(row))
                return count;
        for (int at = 0; at <= (int)item->segment_count + 1; at++)
        {
                int part = at < (int)item->segment_count ? at : at == (int)item->segment_count ? SETTINGS_PART_MORE : SETTINGS_PART_FREE;
                if (part >= 0 && item->segment_role[part] >= UI_STRIP_FREE) { free |= item->segment_role[part] == UI_STRIP_FREE; continue; }
                if ((part == SETTINGS_PART_MORE && !item->segment_more) || (part == SETTINGS_PART_FREE && !free))
                        continue;
                if ((next = settings_sub_add(section, false, null, "", 0, SETTINGS_SUB_PARTITION, part, count)) == count)
                        break;
                count = next;
        }
        return count;
}

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
                row->label = WORDS(settings_partitions), row->expander = settings_exp_get(sub->owner) ? UI_ROW_EXPANDER_OPEN : UI_ROW_EXPANDER_SHUT;
                settings_part_share[positive_into_string((p8 address_to)settings_part_share, (positive)settings_partitions_of(item))] = 0, row->share = settings_part_share;
                return;
        }
        row->reveal = settings_fold_reveal(sub->owner);
        if (sub->part >= 0 && sub->part < (int)item->segment_count)
        {
                const char address_to name = item->segment_name[sub->part], address_to role = settings_role_word(item->segment_role[sub->part]);
                bytes = item->segment_bytes[sub->part], row->swatch = item->segment_role[sub->part] + 1;
                row->label = name[0] ? name : role, row->aside = name[0] && !settings_name_says(name, role) ? role : null;
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
        for (int at = 0; at < settings.item_count[2]; at++)
        {
                int key = settings_fold_key(SETTINGS_LIST_ID(2, at));
                struct settings_item address_to it = &settings.item[2][at];
                b32 open = settings_item_storage(it) && settings_drive_is_open(settings_drive_name(it));
                if (key >= 0 && ui_list_folded(&settings.list, key) != open)
                        ui_list_fold(&settings.list, key, open);
        }
}

static int settings_link_rows(int row, int count)
{
        const struct settings_item address_to item = settings_item(row);
        int section = settings_section_start(row), next;
        if (!item || item->kind != SETTINGS_LINK || (!settings_lines_listed(row)))
                return count;
        settings_sub_owner = row;
        if (item->where == 1)
        {
                if (item->link_addr[0] && (next = settings_sub_add(section, true, "Address", item->link_addr, 0, SETTINGS_SUB_LINK_INFO, 0, count)) != count)
                        count = next;
                if (item->link_rtt[0] && (next = settings_sub_add(section, true, "Latency", item->link_rtt, 0, SETTINGS_SUB_LINK_INFO, 1, count)) != count)
                        count = next;
                if (item->link_traffic[0] && (next = settings_sub_add(section, true, "Traffic", item->link_traffic, 0, SETTINGS_SUB_LINK_INFO, 2, count)) != count)
                        count = next;
                if (item->link_group[0] && (next = settings_sub_add(section, true, "Group", item->link_group, 0, SETTINGS_SUB_LINK_INFO, 3, count)) != count)
                        count = next;
        }
        for (int k = 0; k < 4; k++)
        {
                if ((next = settings_sub_add(section, false, null, settings_link_grants[k].label, 0, SETTINGS_SUB_LINK_GRANT, k, count)) == count)
                        break;
                count = next;
        }
        return count;
}

static fn settings_link_follow(void)
{
        for (int at = 0; at < settings.item_count[3]; at++)
        {
                int key = settings_fold_key(SETTINGS_LIST_ID(3, at));
                struct settings_item address_to it = &settings.item[3][at];
                b32 open = it->kind == SETTINGS_LINK && settings_link_is_open(it->where == 0 ? "__this_machine__" : it->label);
                if (key >= 0 && ui_list_folded(&settings.list, key) != open)
                        ui_list_fold(&settings.list, key, open);
        }
}

static unsigned char settings_link_parse_grants(const char address_to text, positive size)
{
        unsigned char mask = 0;
        positive at = 0;
        while (at < size && text[at] != ',')
        {
                while (at < size && text[at] == ' ') at++;
                positive start = at;
                while (at < size && text[at] != ' ' && text[at] != ',') at++;
                positive len = at - start;
                for (int k = 0; len && k < 4; k++)
                        if (string_length((string_address)settings_link_grants[k].word) == len &&
                            !memory_compare(text + start, settings_link_grants[k].word, len))
                                mask |= (unsigned char)(1u << k);
        }
        return mask;
}

static const char address_to settings_state_word(const char address_to state)
{
        static const struct { const char address_to key; unsigned int text; } table[] = {
            {"allowed", LANGUAGE_settings_state_allowed}, {"blocked", LANGUAGE_settings_state_blocked},
            {"waiting", LANGUAGE_settings_state_waiting}, {"view only", LANGUAGE_settings_state_view_only},
        };
        for (positive at = 0; at < sizeof(table) / sizeof(table[0]); at++)
                if (string_equals((string_address)state, (string_address)table[at].key))
                        return language_text(table[at].text);
        return state;
}

static fn settings_item_port(struct settings_item address_to item, const char address_to text, positive size)
{
        positive state = size, stop, first = 0, from, name;
        while (state > 0 && text[state - 1] != ' ')
                state--;
        for (stop = state; stop > 0 && text[stop - 1] == ' '; stop--)
                ;
        while (first < size && text[first] != ' ')
                first++;
        settings_slice(item->port, sizeof(item->port), text, first);
        if (!state || !stop)
                return settings_item_label(item, text, size);
        settings_slice(item->state, sizeof(item->state), text + state, size - state);
        if (first + 5 > stop || memory_compare(text + first, " usb ", 5))
                return settings_item_label(item, text, stop);
        item->flags |= SETTINGS_ITEM_USB;
        for (from = first + 5; from < stop && text[from] != ' '; from++)
                ;
        for (name = from; name < stop && text[name] == ' '; name++)
                ;
        settings_item_label(item, name < stop ? text + name : text + first + 5, name < stop ? stop - name : from - first - 5);
}

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

static fn settings_item_drive(struct settings_item address_to item, const char address_to name, positive name_len, struct settings_cursor c)
{
        unsigned long long mb = sc_u64(&c, 1000000000000ull);
        settings_item_label(item, name, name_len);
        sc_spaces(&c), sc_eat(&c, "MB"), sc_spaces(&c);
        item->total = mb * 1000000ull;
        settings_size_text(item->total, item->size, sizeof(item->size));
        settings_slice(item->state, sizeof(item->state), c.s, c.n);
}

static b32 settings_drive_bind(struct settings_item address_to item, positive address_to used)
{
        if (*used >= settings.drive_room)
                if (!array_store_reserve(settings.drive, settings.drive_room, *used, *used + 1, 8))
                        return false;
        memory_zero(&settings.drive[*used], sizeof(settings.drive[*used]));
        item->drive_idx = (int)(*used)++;
        return true;
}

static fn settings_feed_hardware(void)
{
        char line[SETTINGS_PROBE], address_to text;
        positive at, size, count = 0, hidden = 0, drives = 0;
        b32 open = false;
        int last_idx = -1;
        unsigned long long spanned = 0;

        for (at = 0; settings_feed_line(&at, line, sizeof(line), &text, &size);)
        {
                struct settings_cursor c = {text, size};
                if (sc_eat(&c, "    "))
                {
                        struct settings_item address_to last = last_idx >= 0 ? &settings.item[2][last_idx] : null;
                        if (sc_eat(&c, "part "))
                        {
                                struct settings_item address_to d = open && count ? &settings.item[2][count - 1] : null;
                                struct settings_drive address_to drv = d && d->drive_idx >= 0 ? &settings.drive[d->drive_idx] : null;
                                unsigned long long bytes;
                                positive rlen, nlen;
                                int code;
                                sc_word(&c, null), sc_spaces(&c), bytes = sc_u64(&c, 1844674407370955160ull), sc_spaces(&c);
                                const char address_to role = sc_word(&c, &rlen); // the word first: its length is what it sets, and the call below reads it
                                code = settings_role_code(role, rlen), sc_spaces(&c);
                                const char address_to named = sc_word(&c, &nlen); // the name is one word: what the line says after it (fs ...) is not the strip's
                                if (d) spanned = spanned + bytes < spanned ? ~0ull : spanned + bytes, d->total = spanned > d->total ? spanned : d->total;
                                if (d && drv && code >= 0 && d->segment_count < SETTINGS_SEGMENTS && (code != 4 || bytes > SETTINGS_ALIGNMENT))
                                        drv->bytes[d->segment_count] = bytes, settings_segment_name(drv->name[d->segment_count], named, nlen),
                                        drv->role[d->segment_count++] = (unsigned char)code;
                        }
                        else if (open && count && sc_eat(&c, "more "))
                        {
                                unsigned long long more = sc_u64(&c, 65535);
                                settings.item[2][count - 1].segment_more = more > 65535 ? 65535 : (unsigned short)more;
                        }
                        else if (sc_eat(&c, "drive "))
                        {
                                struct settings_item address_to port = last;
                                unsigned long long mb;
                                open = port && port->kind == SETTINGS_PORT && !(port->flags & SETTINGS_ITEM_DISK);
                                sc_word(&c, null), sc_spaces(&c), mb = sc_u64(&c, 1000000000000ull), spanned = 0;
                                if (open)
                                        port->flags |= SETTINGS_ITEM_DISK, port->total = mb * 1000000ull, settings_size_text(port->total, port->size, sizeof(port->size)),
                                        open = settings_drive_bind(port, &drives);
                        }
                        else
                        {
                                if (sc_eat(&c, "where ") && last && (last->flags & SETTINGS_ITEM_DRIVE))
                                        last->where = sc_eat(&c, "external")
                                                          ? (c.n >= 3 && !memory_compare(c.s + c.n - 3, " sd", 3) ? SETTINGS_WHERE_SD : SETTINGS_WHERE_EXTERNAL)
                                                          : SETTINGS_WHERE_INTERNAL;
                                open = false;
                        }
                        continue;
                }
                open = false;
                if (sc_eat(&c, "  ") && c.n && c.s[0] != ' ')
                {
                        struct settings_cursor whole = c;
                        positive nlen;
                        const char address_to name;
                        struct settings_item address_to it = settings_item_slot(2, count);
                        if (!it) { hidden++, last_idx = -1; continue; }
                        name = sc_word(&c, &nlen), open = sc_eat(&c, " drive ");
                        it->kind = open ? SETTINGS_NOTE : SETTINGS_PORT, it->where = open ? SETTINGS_WHERE_INTERNAL : SETTINGS_WHERE_EXTERNAL;
                        it->flags = (whole.n > 8 && !memory_compare(whole.s + whole.n - 8, " allowed", 8) ? SETTINGS_ITEM_JOINED : 0) | (open ? SETTINGS_ITEM_DRIVE : 0);
                        spanned = 0, last_idx = (int)count;
                        if (open) settings_item_drive(it, name, nlen, c), open = settings_drive_bind(it, &drives);
                        else settings_item_port(it, whole.s, whole.n);
                        count++;
                }
        }
        settings.drive_count = drives;
        for (positive i = 0; i < count; i++)
                if (settings.item[2][i].drive_idx >= 0)
                {
                        struct settings_drive address_to drv = &settings.drive[settings.item[2][i].drive_idx];
                        settings.item[2][i].segment_role = drv->role, settings.item[2][i].segment_bytes = drv->bytes, settings.item[2][i].segment_name = drv->name;
                }
        for (positive a = 1; a < count; a++)
                for (positive b = a; b > 0 && settings_device_section(&settings.item[2][b]) < settings_device_section(&settings.item[2][b - 1]); b--)
                        settings_item_swap(&settings.item[2][b], &settings.item[2][b - 1]);
        if ((hidden += settings.runner.cut_rows))
        {
                char note[SETTINGS_ITEM_LABEL];
                language_format(note, sizeof(note), WORDS(settings_not_shown), (long)hidden);
                settings_note_add(2, &count, count ? settings.item[2][count - 1].where : SETTINGS_WHERE_NONE, note, string_length((string_address)note));
        }
        if (!count) settings_note_add(2, &count, SETTINGS_WHERE_NONE, WORDS(settings_device_none), string_length((string_address)WORDS(settings_device_none)));
        settings.item_count[2] = (int)count, settings_drive_follow();
}

static fn settings_feed_links(void)
{
        char line[SETTINGS_PROBE], address_to text;
        char self_state[48] = {0};
        b32 self_on = false;
        positive at, size, count = 0;
        for (at = 0; settings_feed_line(&at, line, sizeof(line), &text, &size);)
        {
                struct settings_cursor c = {text, size};
                if (!self_state[0] && sc_eat(&c, "link ") && c.n)
                {
                        sc_trim_at(&c, "  next:");
                        settings_slice(self_state, sizeof(self_state), c.s, c.n);
                        self_on = (c.n >= 2 && !memory_compare(c.s, "on", 2)) || (c.n >= 7 && !memory_compare(c.s, "running", 7));
                }
                else if (sc_eat(&c, "  this machine  ") && c.n)
                {
                        struct settings_item address_to it = settings_item_slot(3, count);
                        positive allow_at;
                        if (!it) continue;
                        sc_trim_at(&c, "  next:");
                        it->kind = SETTINGS_LINK, it->where = 0, it->flags = self_on ? SETTINGS_ITEM_JOINED : 0;
                        it->grants = 0x0fu;
                        if ((allow_at = settings_find_last(c.s, c.n, "  allow ")) < c.n)
                        {
                                it->grants = settings_link_parse_grants(c.s + allow_at + 8, c.n - allow_at - 8);
                                c.n = allow_at;
                        }
                        settings_link_global_grants = it->grants;
                        settings_slice(it->access, sizeof(it->access), self_state, string_length((string_address)self_state));
                        if (sc_eat(&c, "no key yet"))
                        {
                                settings_item_label(it, "this machine", 12);
                                settings_slice(it->detail, sizeof(it->detail), "no key yet", 10);
                        }
                        else
                        {
                                positive name_len = 0;
                                while (name_len + 1 < c.n && (c.s[name_len] != ' ' || c.s[name_len + 1] != ' '))
                                        name_len++;
                                settings_item_label(it, c.s, name_len);
                                c.s += name_len, c.n -= name_len;
                                while (c.n && c.s[0] == ' ') c.s++, c.n--;
                                if (c.n)
                                {
                                        positive key_len = 0;
                                        while (key_len < c.n && c.s[key_len] != ' ')
                                                key_len++;
                                        positive show = key_len > 8 ? 8 : key_len;
                                        settings_slice(it->detail, sizeof(it->detail), c.s, show);
                                        if (key_len > 8)
                                                settings_cat(it->detail, sizeof(it->detail), string_length((string_address)it->detail), "...");
                                        c.s += key_len, c.n -= key_len;
                                        while (c.n && c.s[0] == ' ') c.s++, c.n--;
                                        if (c.n)
                                        {
                                                char rest[SETTINGS_ITEM_LABEL];
                                                settings_slice(rest, sizeof(rest), c.s, c.n);
                                                settings_cat_sep(it->detail, sizeof(it->detail), string_length((string_address)it->detail), "  ", rest);
                                        }
                                }
                        }
                        count++;
                }
                else if (sc_eat(&c, "      ") && c.n && c.s[0] != ' ')
                {
                        if (count && settings.item[3][count - 1].kind == SETTINGS_LINK && settings.item[3][count - 1].where == 1 && (sc_trim_at(&c, "  next:"), c.n))
                        {
                                struct settings_item address_to it = &settings.item[3][count - 1];
                                char piece[SETTINGS_ITEM_LABEL];
                                positive pos;
                                settings_slice(piece, sizeof(piece), c.s, c.n);
                                if (c.n >= 8 && !memory_compare(c.s, "open now", 8))
                                {
                                        it->flags |= SETTINGS_ITEM_JOINED;
                                        settings_slice(it->state, sizeof(it->state), "Online", 6);
                                }
                                else if (c.n >= 20 && !memory_compare(c.s, "not heard since boot", 20))
                                {
                                        if (!it->state[0])
                                                settings_slice(it->state, sizeof(it->state), "Offline", 7);
                                }
                                else if (c.n >= 22 && !memory_compare(c.s, "its key does not match", 22))
                                {
                                        settings_slice(it->state, sizeof(it->state), "Key mismatch", 12);
                                        if (c.n > 24 && c.s[22] == ':' && c.s[23] == ' ')
                                        {
                                                positive s1 = 24;
                                                while (s1 < c.n && c.s[s1] != ' ') s1++;
                                                settings_slice(it->link_addr, sizeof(it->link_addr), c.s + 24, s1 - 24);
                                        }
                                }
                                else if (c.n >= 6 && !memory_compare(c.s, "heard ", 6) && !it->state[0])
                                {
                                        struct settings_cursor hc = {c.s, c.n};
                                        sc_trim_at(&hc, " from ");
                                        settings_slice(it->state, sizeof(it->state), hc.s, hc.n);
                                        if (it->state[0] == 'h') it->state[0] = 'H';
                                }
                                if ((pos = settings_find_last(c.s, c.n, "from ")) < c.n)
                                {
                                        positive s0 = pos + 5, s1 = s0;
                                        while (s1 < c.n && c.s[s1] != ' ' && c.s[s1] != ';') s1++;
                                        if (s1 > s0)
                                                settings_slice(it->link_addr, sizeof(it->link_addr), c.s + s0, s1 - s0);
                                }
                                else if ((pos = settings_find_last(c.s, c.n, "last address ")) < c.n && !it->link_addr[0])
                                {
                                        positive s0 = pos + 13, s1 = s0;
                                        while (s1 < c.n && c.s[s1] != ' ' && c.s[s1] != ';') s1++;
                                        if (s1 > s0)
                                                settings_slice(it->link_addr, sizeof(it->link_addr), c.s + s0, s1 - s0);
                                }
                                if ((pos = settings_find_last(c.s, c.n, "rtt ")) < c.n)
                                {
                                        positive s0 = pos + 4, s1 = s0;
                                        while (s1 < c.n && !(c.s[s1] == ' ' && s1 + 1 < c.n && c.s[s1 + 1] == ' ')) s1++;
                                        if (s1 > s0)
                                                settings_slice(it->link_rtt, sizeof(it->link_rtt), c.s + s0, s1 - s0);
                                }
                                if ((pos = settings_find_last(c.s, c.n, "\xe2\x86\x91")) < c.n)
                                {
                                        positive s0 = pos, s1 = s0;
                                        while (s1 < c.n && !(c.s[s1] == ' ' && s1 + 1 < c.n && c.s[s1 + 1] == ' ')) s1++;
                                        if (s1 > s0)
                                                settings_slice(it->link_traffic, sizeof(it->link_traffic), c.s + s0, s1 - s0);
                                }
                                settings_cat_sep(it->detail, sizeof(it->detail), string_length((string_address)it->detail), "; ", piece);
                        }
                }
                else if (sc_eat(&c, "  ") && c.n > 2 && c.s[0] != ' ')
                {
                        positive may_at = settings_find_last(c.s, c.n, "...  may ");
                        if (may_at < c.n)
                        {
                                positive cut = 0, grp_at;
                                struct settings_cursor grants = {c.s + may_at + 9, c.n - may_at - 9};
                                struct settings_item address_to it = settings_item_slot(3, count);
                                if (!it) continue;
                                while (cut + 1 < c.n && (c.s[cut] != ' ' || c.s[cut + 1] != ' ')) cut++;
                                sc_trim_at(&grants, "  next:");
                                it->kind = SETTINGS_LINK, it->where = 1, it->flags = SETTINGS_ITEM_SAVED;
                                it->grants = settings_link_parse_grants(grants.s, grants.n);
                                if ((grp_at = settings_find_last(grants.s, grants.n, ", in group ")) < grants.n)
                                {
                                        settings_slice(it->link_group, sizeof(it->link_group), grants.s + grp_at + 11, grants.n - grp_at - 11);
                                        grants.n = grp_at;
                                }
                                settings_slice(it->access, sizeof(it->access), grants.s, grants.n);
                                settings_item_label(it, c.s, cut);
                                count++;
                        }
                        else if (sc_eat(&c, "nobody")) settings_note_add(3, &count, 1, text + 2, size - 2);
                }
        }
        settings.item_count[3] = (int)count, settings_link_follow();
}

static struct ui_columns address_to settings_columns_for(int list)
{
        static const struct { unsigned char count, at_end; unsigned short w[4]; unsigned int title_id[4]; } specs[SETTINGS_LISTS] = {
            {4, 3, {0, 84, 84, 72},   {LANGUAGE_files_column_name, LANGUAGE_settings_column_status, LANGUAGE_settings_column_security, LANGUAGE_settings_column_signal}},
            {2, 1, {0, 100, 0, 0},    {LANGUAGE_files_column_name, LANGUAGE_settings_column_status, 0, 0}},
            {4, 3, {0, 108, 84, 128}, {LANGUAGE_files_column_name, 0, LANGUAGE_settings_column_status, LANGUAGE_settings_column_access}},
            {3, 0, {0, 150, 140, 0},  {LANGUAGE_files_column_name, LANGUAGE_settings_column_access, LANGUAGE_settings_column_status, 0}},
        };
        struct ui_columns address_to c;
        if (!settings_large || list < 0 || list >= SETTINGS_LISTS)
                return null;
        c = &settings_large->columns[list];
        if (!c->count)
        {
                c->sorted = c->hover = c->held = -1, c->count = specs[list].count;
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
        switch (list * 4 + col)
        {
        case 1:  cmp = (a->flags & SETTINGS_ITEM_JOINED ? 0 : a->flags & SETTINGS_ITEM_SAVED ? 1 : 2) - (b->flags & SETTINGS_ITEM_JOINED ? 0 : b->flags & SETTINGS_ITEM_SAVED ? 1 : 2); break;
        case 2:  cmp = (int)string_compare_folded((string_address)a->security, (string_address)b->security); break;
        case 3:  { int ha = (a->flags & SETTINGS_ITEM_HEARD) != 0, hb = (b->flags & SETTINGS_ITEM_HEARD) != 0; cmp = ha != hb ? ha - hb : (int)a->dbm - (int)b->dbm; break; }
        case 9:  cmp = (int)string_compare_folded((string_address)a->port, (string_address)b->port); break;
        case 10: cmp = (int)string_compare_folded((string_address)settings_state_word(a->state), (string_address)settings_state_word(b->state)); break;
        case 11: cmp = ((a->flags & SETTINGS_ITEM_JOINED) ? 0 : 1) - ((b->flags & SETTINGS_ITEM_JOINED) ? 0 : 1); break;
        case 13: cmp = (int)string_compare_folded((string_address)a->access, (string_address)b->access); break;
        case 14: cmp = (int)string_compare_folded((string_address)a->detail, (string_address)b->detail); break;
        }
        return cmp ? cmp : (int)string_compare_folded((string_address)a->label, (string_address)b->label);
}

static b32 settings_item_sortable(int list, const struct settings_item address_to it) { return it->kind != SETTINGS_NOTE && it->kind != SETTINGS_ADD && (list != 2 || settings_device_section(it) == 1) && (list != 3 || it->where == 1); }

/* The sort of a list's column: the kit orders the ids of each run of sortable items of one list (ui_list_order_range); the items
   themselves do not move, so the rows keep their ids and the section adjacency the items keep. */
static int settings_sort_list_now, settings_sort_col_now;
static int settings_sort_by_id(int a, int b)
{
        const struct settings_item address_to x = settings_item(a), address_to y = settings_item(b);
        return x && y ? settings_list_compare(settings_sort_list_now, settings_sort_col_now, x, y) : 0;
}

static fn settings_sort_run(int list, int from, int to)
{
        struct ui_columns address_to c = settings_columns_for(list);
        if (!c || c->sorted < 0)
                return;
        settings_sort_list_now = list, settings_sort_col_now = c->sorted;
        ui_list_order_range(&settings.list, from, to, settings_sort_by_id, c->descending);
}

// Runs of sortable items of one list, in the list's ids as they were added: each run is sorted where it stands.
static fn settings_sort_runs(void)
{
        int run = -1, run_list = -1;
        for (int at = 0; at <= settings.list.count; at++)
        {
                int id = at < settings.list.count ? settings.list.id[at] : 0, list = at < settings.list.count ? settings_list_of(id) : -1;
                const struct settings_item address_to it = list >= 0 ? settings_item(id) : null;
                b32 here = it && settings_item_sortable(list, it);
                if (run >= 0 && (!here || list != run_list))
                        settings_sort_run(run_list, run, at), run = -1;
                if (here && run < 0)
                        run = at, run_list = list;
        }
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
        for (int e = 0; e < settings.item_count[list]; e++)
                if (string_equals((string_address)settings.item[list][e].label, (string_address)keep)) { found = e; break; }
        if (found >= 0)
                settings.list.focus = SETTINGS_LIST_ID(list, found);
}

static fn settings_columns_sorted(struct ui_columns address_to hit)
{
        (void)hit;
        settings_rebuild();
}

static positive settings_list_sum(int list)
{
        positive sum = SETTINGS_FNV64_SEED;
        if (settings.item[list] && settings.item_count[list] > 0)
                sum = settings_fnv64_bytes(sum, settings.item[list], (positive)settings.item_count[list] * sizeof(settings.item[list][0]));
        if (list == 2 && settings.drive && settings.drive_count > 0)
                sum = settings_fnv64_bytes(sum, settings.drive, settings.drive_count * sizeof(settings.drive[0]));
        return sum ^ (positive)settings.item_count[list];
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
        static fn (*const parsers[])(void) = {[SETTINGS_FEED_WIFI] = settings_feed_wifi, [SETTINGS_FEED_DEVICES] = settings_feed_devices, [SETTINGS_FEED_HARDWARE] = settings_feed_hardware, [SETTINGS_FEED_LINKS] = settings_feed_links};
        int feed = settings.feed_list, list = feed, owner = -1, at;
        char keep[SETTINGS_ITEM_LABEL];
        positive before;
        b32 changed = false;

        settings.feed_due = (positive)ui_kit.now + (positive)settings_feeds[feed].every * 1000000ull;
        if (killed || settings_editing() >= (int)SETTINGS_LIST)
                return;
        for (positive r = 0; r < SETTINGS_ROWS && settings_feeds[feed].owner; r++)
                if (string_equals((string_address)settings_rows[r].label, (string_address)settings_feeds[feed].owner)) { owner = (int)r; break; }
        if (owner >= 0)
        {
                char value[SETTINGS_VALUE];
                positive was = settings.state[owner];
                string_copy_bounded((string_address)value, (string_address)settings.value[owner], sizeof(value));
                settings.state[owner] = (unsigned char)settings_read(&settings_rows[owner], settings_large->said, settings.runner.length, settings.value[owner], SETTINGS_VALUE, failed);
                changed = was != settings.state[owner] || !string_equals((string_address)value, (string_address)settings.value[owner]);
        }
        before = settings_list_sum(list);
        settings_focus_keep(list, keep, sizeof(keep));
        at = settings.list.focus - SETTINGS_LIST_ID(list, 0);
        parsers[settings_feeds[feed].kind]();
        settings_layout_check();
        if (settings_list_sum(list) == before && !changed)
                return;
        at = at < settings.item_count[list] ? at : settings.item_count[list] - 1;
        settings_focus_restore(list, keep, at < 0 ? 0 : at);
        settings_rebuild();
}

static fn settings_feed_rows(void)
{
        for (positive at = 0; at < SETTINGS_ROWS; at++)
                if ((settings_rows[at].flags & SETTINGS_TICK) && settings_group_of((int)at) == settings.page.chosen && settings_editing() != (int)at)
                        settings_unread_set(at, 1);
}

static long settings_feed_tick(void)
{
        int feed = settings_feed_of_group();
        positive now = (positive)ui_kit.now, count = 1;
        string_address words[SETTINGS_WORDS];
        if (feed < 0 || settings.page.card || settings_editing() >= 0 || settings_filter_length() ||
            (settings.own.table && (settings.own.table->state[settings.own.row] & WINDOW_MINIMIZED)) || settings.runner.child > 0 || settings.queued || settings_unread_any() || settings.help == 1)
                return -1;
        if (now < settings.feed_due)
                return (long)(settings.feed_due - now);
        settings.feed_due = now + (positive)settings_feeds[feed].every * 1000000ull;
        if (settings_feeds[feed].kind == SETTINGS_FEED_ROWS)
                return settings_feed_rows(), settings_next_job(), -1;
        words[0] = settings.moonwater;
        for (positive at = 0; at < 3 && settings_feeds[feed].read[at]; at++)
                words[count++] = (string_address)settings_feeds[feed].read[at];
        words[count] = null, settings.job = SETTINGS_JOB_FEED, settings.job_sets = false, settings.feed_list = feed;
        settings_start(words, count, null);
        return -1;
}


/* ---- pairing, verb completion, choices & Display output apply ---- */

static fn settings_reading_changed(int row)
{
        if (settings.page.menu_row == row)
                ui_page_redraw(&settings.page);
        else if (!settings.page.card)
                ui_list_row_changed(&settings.ui, &settings.list, row), settings_try_changed(row);
}

static b32 settings_waits(int row) { return row >= 0 && row < SETTINGS_ANY && (settings_rows[row].flags & SETTINGS_WAITS) != 0; }

static positive settings_said_line(char address_to line, positive room, b32 last)
{
        positive at = 0, size = 0;
        while (at < settings.runner.length)
        {
                char got[SETTINGS_STATUS];
                positive made = settings_line(settings_large->said, settings.runner.length, &at, got, sizeof(got));
                if (made)
                {
                        string_copy_bounded((string_address)line, (string_address)got, room), size = made;
                        if (!last)
                                break;
                }
        }
        return size;
}

#define SETTINGS_PAIR_SECONDS 300
#define SETTINGS_PAIR_CODE 16
#define SETTINGS_PAIR_SIZE 36
#define SETTINGS_PAIR_TICK_NS 1000000000ull
static b32 settings_pair_on;
static char settings_pair_code[SETTINGS_PAIR_CODE];
static positive settings_pair_due;

static b32 settings_pair_row(int row) { return row >= 0 && row < SETTINGS_ANY && settings_rows[row].kind == SETTINGS_ACTION && settings_waits(row); }
static b32 settings_pair_waiting(int row) { return settings.runner.child > 0 && settings.job_sets && settings.job == row && settings_pair_row(row); }

static const char address_to settings_button_words(int row)
{
        static const struct { const char address_to verb; unsigned int text; } table[] = {
            {"sync", LANGUAGE_settings_button_set}, {"refresh", LANGUAGE_settings_button_discover},
            {"pair", LANGUAGE_settings_button_pair}, {"end", LANGUAGE_settings_button_end},
        };
        const char address_to last = null;
        if (settings_pair_waiting(row))
                return WORDS(settings_button_stop);
        if (settings_action_busy(row))
                return WORDS(settings_button_running);
        for (positive k = 0; k < SETTING_WORDS && settings_rows[row].set[k]; k++)
                last = settings_rows[row].set[k];
        for (positive at = 0; last && at < sizeof(table) / sizeof(table[0]); at++)
                if (string_equals((string_address)last, (string_address)table[at].verb))
                        return language_text(table[at].text);
        return WORDS(settings_button_run);
}

static fn settings_pair_paint(struct ui address_to ui)
{
        char text[SETTINGS_PROBE], line[SETTINGS_PROBE];
        positive now = (positive)ui_kit.now, gone = now > settings.runner.began ? (now - settings.runner.began) / 1000000000ull : 0;
        positive left = gone < SETTINGS_PAIR_SECONDS ? SETTINGS_PAIR_SECONDS - gone : 0, at;
        int edge = ui_px(ui, UI_PAD) + ui_dev(ui, UI_INSET), x = ui_rtl() ? ui->width - edge : edge;
        int baseline = settings.card.y + ui_dev(ui, UI_LINES_PAD + SETTINGS_PAIR_SIZE), y = baseline + ui_dev(ui, 52);

        ui_kit_start_at(ui, x, baseline, (string_address)settings_pair_code, UI_CODE, SETTINGS_PAIR_SIZE, ui_role(UI_ON_SURFACE), 0, 0, ui->width);
        at = positive_into((p8 address_to)text, left / 60), text[at++] = ':';
        text[at + positive_into_pair((p8 address_to)(text + at), left % 60)] = 0;
        ui_kit_text_flipped(ui, edge, baseline + ui_dev(ui, 28), (string_address)text, UI_BODY_MEDIUM, ui_role(UI_ON_SURFACE), 0, 0, ui->width);
        for (int e = 0; e < settings.card_text.count; e++, y += ui_dev(ui, UI_LINE))
                ui_kit_text_flipped(ui, edge, ui_kit_baseline(ui, y, UI_BODY_MEDIUM), (string_address)ui_lines_copy(&settings.card_text, e, line, sizeof(line)), UI_BODY_MEDIUM, ui_role(UI_ON_SURFACE), 0, 0, ui->width);
}

static fn settings_waiting_say(void)
{
        char line[SETTINGS_STATUS];
        positive size;
        if (settings.runner.child > 0 && settings.job_sets && settings_waits(settings.job) && (size = settings_said_line(line, sizeof(line), false)))
        {
                settings_say((string_address)line);
                if (!settings_pair_on && settings_pair_row(settings.job) && settings_find(line, size, "on the other machine") < size)
                {
                        positive at = 0, made = 0;
                        while (at < size && line[at] != ' ') at++;
                        while (at < size && line[at] == ' ') at++;
                        while (at < size && line[at] != ' ' && line[at] != ':' && made + 1 < sizeof(settings_pair_code))
                                settings_pair_code[made++] = line[at++];
                        settings_pair_code[made] = 0, settings_pair_on = true, settings_pair_due = 0;
                        settings_card_open(settings_label(settings.job));
                }
        }
}

static fn settings_complete(b32 killed, positive status)
{
        int row = settings.job;
        b32 action = settings.job_sets && settings_action_row(row);
        if (settings.job_sets)
        {
                char line[SETTINGS_STATUS];
                if (row >= SETTINGS_ANY)
                        settings_report(killed, status);
                else
                {
                        positive sz = settings_said_line(line, sizeof(line), settings_waits(row));
                        settings_say((string_address)(killed ? WORDS(settings_too_long) : sz ? line : wait_status_code(status) ? WORDS(settings_refused) : WORDS(settings_done)));
                }
                if (settings_pair_on)
                {
                        settings_pair_on = false;
                        settings_wipe(settings_pair_code, sizeof(settings_pair_code));
                        if (settings.page.card)
                                settings_card_fill(), ui_lines_wrap(&settings.ui, &settings.card_text, settings.width), settings_layout();
                        settings_wipe(settings_large->said, sizeof(settings_large->said));
                        ui_page_redraw(&settings.page);
                }
                settings_wipe(settings.job_value, sizeof(settings.job_value));
                settings_wipe(settings_large->split, sizeof(settings_large->split));
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
                        settings_help(settings_large->said, settings.runner.length);
                settings_refilter();
                if (settings.truncated)
                        settings_say((string_address)WORDS(settings_help_more));
        }
        else
                for (positive o = 0; o < SETTINGS_ROWS; o++)
                {
                        char before[SETTINGS_VALUE];
                        unsigned char was = settings.state[o];
                        if (((int)o != row && !settings_unread_is(o)) || !settings_same_verb(&settings_rows[row], &settings_rows[o]))
                                continue;
                        settings_unread_set(o, 0);
                        string_copy_bounded((string_address)before, (string_address)settings.value[o], sizeof(before));
                        settings.state[o] = killed ? SETTINGS_FAILED : (unsigned char)settings_read(&settings_rows[o], settings_large->said, settings.runner.length, settings.value[o], SETTINGS_VALUE, wait_status_code(status) != 0);
                        if (settings.state[o] != was || !string_equals((string_address)before, (string_address)settings.value[o]))
                                settings_reading_changed((int)o);
                }
        settings_next_job();
        if (action)
                ui_page_redraw(&settings.page);
        if (!settings.told && !settings_unread_any() && settings.runner.child <= 0 && !settings.queued)
        {
                positive failed = settings_not_understood();
                settings.told = true;
                compositor_kmsg((string_address) "settings: rows unread or not understood when first asked: ", failed, failed ? (string_address) "" : (string_address) "0");
        }
}

#define settings_left ui_job_left
static b32 settings_reap(void) { settings_runner_ready(); return ui_job_reap(&settings.runner); }
static b32 settings_left_reap(void) { return ui_job_left_reap(); }
static fn settings_finished(b32 killed) { settings_runner_ready(); ui_job_finish(&settings.runner, killed); }
static fn settings_feed_drop(void) { if (settings.runner.child > 0 && settings.job == SETTINGS_JOB_FEED) settings_finished(true); }
static fn settings_confirm_say(const char address_to text) { char line[SETTINGS_STATUS]; ui_confirm_line(line, sizeof(line), text); settings_say((string_address)line); }

static b32 settings_run(int row, const char address_to args)
{
        string_address words[SETTINGS_WORDS];
        positive count = settings_generic_words(row, args, words, SETTINGS_WORDS);
        if (count <= 1)
                return settings_say((string_address)(count ? WORDS(settings_type_command) : WORDS(settings_too_many))), false;
        if (settings_asks_for(words + 1, count - 1) && !ui_confirm_ask(&settings.confirm, row + 1, args))
        {
                char text[SETTINGS_STATUS];
                settings_join(words, count, text, sizeof(text)), settings_confirm_say(text);
                return false;
        }
        return settings_refused(row, args) ? false : (settings_change(row, args), true);
}

static b32 settings_stop(void) { return settings.runner.child > 0 && settings.job_sets && settings.job >= SETTINGS_ANY ? (settings_finished(true), true) : false; }

static b32 settings_output_choice(const struct settings_row address_to which, positive index, char address_to into, positive room);

static b32 settings_modem_ready(void)
{
        for (positive at = 0; at < SETTINGS_ROWS; at++)
                if (settings_rows[at].kind == SETTINGS_INFO && settings_rows[at].key && string_equals((string_address)settings_rows[at].key, (string_address) "audio modem "))
                        return settings.state[at] == SETTINGS_READ && string_equals((string_address)settings.value[at], (string_address) "ready");
        return false;
}

static b32 settings_choice(const struct settings_row address_to row, positive index, char address_to into, positive room)
{
        positive names = settings.battery_menu_count < BATTERY_LIST ? settings.battery_menu_count : BATTERY_LIST;
        if (row->output != SETTINGS_OUT_NONE)
                return settings_output_choice(row, index, into, room);
        if (row->flags & SETTINGS_TRANSPORT)
                return index == 0 || (index == 1 && settings_modem_ready()) ? settings_word(row->choices, index, into, room) : false;
        if (!(row->flags & SETTINGS_BATTERIES))
                return settings_word(row->choices, index, into, room);
        if (index > names + 1)
                return false;
        string_copy_bounded((string_address)into, (string_address)(!index ? "auto" : index <= names ? settings.battery_menu[index - 1].text : "none"), room);
        return true;
}

static const char address_to settings_battery_choice(void)
{
        for (positive at = 0; at < SETTINGS_ROWS; at++)
                if (settings_rows[at].flags & SETTINGS_BATTERIES)
                        return settings.state[at] == SETTINGS_READ && settings.value[at][0] ? settings.value[at] : "auto";
        return "auto";
}

static unsigned int settings_map_count(void)
{
        unsigned int count = settings.own.table ? __atomic_load_n(&settings.own.table->outputs, __ATOMIC_ACQUIRE) : 0;
        return count > WINDOWS_OUTPUTS ? 0 : count;
}

static b32 settings_is_modes(int row) { return settings_rows[row].set[1] && string_equals((string_address)settings_rows[row].set[1], (string_address) "modes"); }

static const char address_to settings_choice_said(int row, const char address_to word)
{
        if (settings_rows[row].path && string_equals((string_address)settings_rows[row].path, (string_address)SETTING_ONSCREEN_PATH))
                return string_equals((string_address)word, (string_address) "off") ? "Always off"
                       : string_equals((string_address)word, (string_address) "on") ? "Always on" : string_equals((string_address)word, (string_address) "auto") ? "Automatic" : word;
        return string_equals((string_address)word, (string_address) "auto") ? "Automatic" : word;
}

static const char address_to settings_label_of(int row, const char address_to word)
{
        if (settings_rows[row].flags & SETTINGS_LANGUAGE)
                return language_name_of(word);
        if (settings_rows[row].flags & SETTINGS_BATTERIES)
                return language_of(word);
        if (!settings_is_modes(row))
                return settings_rows[row].kind == SETTINGS_CHOICE && settings_rows[row].choices && setting_place(settings_rows[row].choices, (string_address)word) >= 0
                           ? language_of(settings_choice_said(row, word)) : word;
        return string_equals((string_address)word, (string_address) "largest") ? WORDS(settings_mode_largest) // shown for a key
               : string_equals((string_address)word, (string_address) "preferred") ? WORDS(settings_mode_preferred) : word; // shown for a key
}

static void settings_word_of(int row, const char address_to label, char address_to into, positive room)
{
        const char address_to word = label;
        if (settings_rows[row].flags & SETTINGS_LANGUAGE)
                word = language_code_of(label);
        else if (!settings_is_modes(row) && settings_rows[row].kind == SETTINGS_CHOICE && settings_rows[row].choices)
        {
                char each[SETTINGS_VALUE];
                for (positive k = 0; settings_word(settings_rows[row].choices, k, each, sizeof(each)); k++)
                        if (string_equals((string_address)language_of(settings_choice_said(row, each)), (string_address)label)) // shown against shown
                                return (void)string_copy_bounded((string_address)into, (string_address)each, room);
        }
        else if (settings_rows[row].flags & SETTINGS_BATTERIES)
                word = string_equals((string_address)label, (string_address)language_of("auto")) ? "auto" // shown against shown
                       : string_equals((string_address)label, (string_address)language_of("none")) ? "none" : label; // shown against shown
        else if (settings_is_modes(row))
                word = string_equals((string_address)label, (string_address)WORDS(settings_mode_largest)) ? "largest" // shown against shown
                       : string_equals((string_address)label, (string_address)WORDS(settings_mode_preferred)) ? "preferred" : label; // shown against shown
        string_copy_bounded((string_address)into, (string_address)word, room);
}

static b32 settings_output_choice(const struct settings_row address_to which, positive index, char address_to into, positive room)
{
        struct windows address_to t = settings.own.table;
        int chosen = settings_map_chosen();
        const struct settings_out_spec address_to spec = which->output < 10 ? &settings_out_specs[which->output] : null;
        if (spec && spec->pair_text[0])
                return chosen >= 0 && index < 2 ? (string_copy_bounded((string_address)into, (string_address)language_text(spec->pair_text[index]), room), true) : false;
        switch (which->output)
        {
        case SETTINGS_OUT_SCREEN:
        {
                unsigned int number[WINDOWS_OUTPUTS] = {0}, count = settings_map_count();
                int at;
                if (index >= count)
                        return false;
                windows_screen_numbers(t, number);
                return (at = settings_map_numbered(number, count, (unsigned int)index + 1)) >= 0
                           ? (settings_screen_label((unsigned int)index + 1, t->output_name[at], t->output_model[at], into, room), true) : false;
        }
        case SETTINGS_OUT_TURN:
                return index < sizeof(settings_turns) / sizeof(settings_turns[0]) ? (string_copy_bounded((string_address)into, (string_address)language_of(settings_turns[index].label), room), true) : false;
        case SETTINGS_OUT_SCALE:
                return chosen >= 0 && index < sizeof(settings_scales) / sizeof(settings_scales[0])
                           ? (settings_scale_label(settings_scales[index], settings_scales[index] == settings_scale_suggested(t, chosen), into, room), true) : false;
        case SETTINGS_OUT_REFRESH:
                return chosen >= 0 && index < settings_rates_of(chosen) ? (settings_rate_label(t->output_hz[chosen][index], index == 0, into, room), true) : false;
        case SETTINGS_OUT_SIZE:
                if (chosen < 0)
                        return false;
                if (settings_keep_here(chosen) && index == 0)
                        return settings_keep_label(into, room), true;
                index -= settings_keep_here(chosen);
                return index < settings_sizes_of(chosen) ? (settings_size_label(t, chosen, (unsigned int)index, into, room), true) : false;
        }
        return false;
}

static int settings_choice_index(int row, const char address_to label)
{
        char word[SETTINGS_VALUE];
        for (int at = 0; settings_choice(&settings_rows[row], (positive)at, word, sizeof(word)); at++)
                if (string_equals((string_address)settings_label_of(row, word), (string_address)label))
                        return at;
        return -1;
}

static const char address_to settings_shown_value(int row)
{
        return settings_rows[row].output == SETTINGS_OUT_NONE ? settings_label_of(row, settings.value[row]) : settings_output_text(row, settings_map_chosen());
}

static void settings_describe_output(int id, struct ui_row address_to row)
{
        struct windows address_to t = settings.own.table;
        int chosen = settings_map_chosen();
        unsigned char out = settings_rows[id].output;
        const struct settings_out_spec address_to spec = out < 10 ? &settings_out_specs[out] : null;
        unsigned int choices;

        row->kind = UI_ROW_CHOICE;
        if (out == SETTINGS_OUT_NOTICES)
        {
                unsigned int count = 0;
                for (int at = 0; at < WINDOWS_ISSUES; at++)
                        count += settings_board.issue[at].used && settings_board.issue[at].code >= WINDOWS_ISSUE_KERNEL_SYSTEM && settings_board.issue[at].code <= WINDOWS_ISSUE_KERNEL_PROGRAM;
                row->kind = UI_ROW_PLAIN, row->dim = count == 0;
                if (!count)
                        row->value = WORDS(settings_notices_none);
                else
                        language_format(settings_notices_text, sizeof(settings_notices_text), WORDS(settings_notices_count), (long)count), row->value = settings_notices_text;
                return;
        }
        if (out == SETTINGS_OUT_NOTE)
                return (void)(row->kind = UI_ROW_PLAIN, row->dim = true, row->label = chosen >= 0 && t->output_software[chosen] ? WORDS(settings_turned_software) : language_of(settings_rows[id].label));
        if (chosen < 0)
                return (void)(row->kind = UI_ROW_PLAIN, row->dim = true, row->value = WORDS(settings_no_screen));
        choices = out == SETTINGS_OUT_REFRESH ? settings_rates_of(chosen) : out == SETTINGS_OUT_SIZE ? settings_sizes_of(chosen)
                  : out == SETTINGS_OUT_SCREEN || out == SETTINGS_OUT_PRIMARY ? settings_map_count() : 2;
        if (spec && spec->empty_text && !choices)
                return (void)(row->kind = UI_ROW_PLAIN, row->dim = true, row->value = language_text(spec->empty_text));
        if (spec && spec->plain_when_one && choices == 1 && !(out == SETTINGS_OUT_SIZE && settings_keep_here(chosen)))
                row->kind = UI_ROW_PLAIN;
        row->value = settings_row_text(id, chosen);
}

static fn settings_output_apply(int row, const char address_to label)
{
        unsigned char out = settings_rows[row].output;
        const struct settings_out_spec address_to spec = out < 10 ? &settings_out_specs[out] : null;
        struct windows address_to t = settings.own.table;
        int at = settings_choice_index(row, label), chosen = settings_map_chosen();
        char value[SETTINGS_TYPED], back[32], line[SETTINGS_STATUS];
        positive length;
        if (at < 0 || !t) return;
        if (out == SETTINGS_OUT_SCREEN)
        {
                unsigned int number[WINDOWS_OUTPUTS] = {0};
                int screen;
                windows_screen_numbers(t, number), screen = settings_map_numbered(number, settings_map_count(), (unsigned int)at + 1);
                if (screen == chosen) goto same;
                return settings.map.display = screen, ui_page_redraw(&settings.page);
        }
        if (chosen < 0) return settings_say((string_address)WORDS(settings_no_screen));
        if (out == SETTINGS_OUT_WALLPAPER)
        {
                char path[96];
                b32 own = settings_wall_own(t->output_name[chosen], path, sizeof(path));
                if (at == 1 && !own) return settings_say((string_address)WORDS(settings_wall_hint));
                if ((at == 1) == own) goto same;
                return settings_cat(value, sizeof(value), settings_cat(value, sizeof(value), 0, "none "), t->output_name[chosen]), settings_change(row, value);
        }
        if (out == SETTINGS_OUT_SIZE)
        {
                b32 keep = settings_keep_here(chosen);
                unsigned int w, h;
                if (keep && at == 0) return settings_resize.on = false, language_format(line, sizeof(line), WORDS(settings_size_kept), (long)settings_resize.width, (long)settings_resize.height), settings_say_redraw(line);
                if ((at -= keep) < 0 || at >= (int)settings_sizes_of(chosen)) return;
                w = t->output_size[chosen][at][0], h = t->output_size[chosen][at][1];
                if (w == t->output_mode_w[chosen] && h == t->output_mode_h[chosen] && !keep) goto same;
                if (!keep) settings_resize.old_w = t->output_asked_w[chosen], settings_resize.old_h = t->output_asked_h[chosen];
                string_copy_bounded((string_address)settings_resize.name, (string_address)t->output_name[chosen], sizeof(settings_resize.name));
                settings_resize.width = w, settings_resize.height = h, settings_resize.due = (positive)ui_kit.now + SETTINGS_KEEP_NS, settings_resize.seen = false, settings_resize.on = true;
                settings_mode_cmd(value, sizeof(value), t->output_name[chosen], spec->verb, w, h, back, sizeof(back)), settings_change(row, value);
                return language_format(line, sizeof(line), WORDS(settings_size_ask), (long)w, (long)h, (long)(SETTINGS_KEEP_NS / 1000000000ull)), settings_say_redraw(line);
        }
        length = settings_cat(value, sizeof(value), 0, t->output_name[chosen]);
        if (out == SETTINGS_OUT_PRIMARY)
        {
                if ((at == 0) == (chosen == 0)) goto same;
                if (at != 0) return settings_say((string_address)WORDS(settings_primary_other));
                return settings_cat(value, sizeof(value), length, spec->verb), settings_change(row, value);
        }
        {
                long target = 0, current = -1;
                b32 preferred = false;
                if (out == SETTINGS_OUT_TURN && at < (int)(sizeof(settings_turns) / sizeof(settings_turns[0]))) target = settings_turns[at].degrees, current = (long)t->output_turn[chosen];
                else if (out == SETTINGS_OUT_SCALE && at < (int)(sizeof(settings_scales) / sizeof(settings_scales[0]))) target = (long)settings_scales[at], current = (long)t->output_asked_scale[chosen];
                else if (out == SETTINGS_OUT_REFRESH && at < (int)settings_rates_of(chosen)) target = (long)t->output_hz[chosen][at], current = (long)t->output_now[chosen], preferred = at == 0;
                else return;
                if (target == current) goto same;
                length = settings_cat(value, sizeof(value), length, spec->verb);
                if (preferred) settings_cat(value, sizeof(value), length, "preferred"); else settings_cat_num(value, sizeof(value), length, target);
                return settings_change(row, value);
        }
same:
        settings_say((string_address)WORDS(settings_already_set));
}

/* ---- menus, editing, filtering, activation & keyboard ---- */

static fn settings_chose(int id);
static fn settings_field_menu(void);
static fn settings_filter_clear(void);
static b32 settings_filter_typed(void);

static fn settings_menu_answer(int answer)
{
        if (answer == UI_MENU_NOTHING) { if (settings.popup.open) ui_popmenu_present(&settings.popup); return; }
        ui_popmenu_close(&settings.popup), settings_chose(answer);
}

static fn settings_menu_close(void)
{
        if (settings.page.menu_row >= 0)
                ui_page_menu_close(&settings.page, &settings.popup), settings_say_redraw("");
}

static const char address_to settings_menu_label(int row, int k)
{
        static char word[SETTINGS_VALUE];
        return settings_choice(&settings_rows[row], (positive)k, word, sizeof(word)) ? settings_label_of(row, word) : null;
}

static fn settings_menu_open(int row)
{
        if (settings_pos(row) < 0 || settings.ui.scale <= 0)
                return;
        if (settings_rows[row].flags & SETTINGS_BATTERIES)
                settings.battery_menu_count = battery_list(settings.battery_menu, BATTERY_LIST);
        if (ui_page_menu_open(&settings.ui, &settings.page, &settings.list, &settings.popup, &settings.own, row, settings_menu_label, settings_shown_value(row)))
                settings_say_redraw(WORDS(settings_menu_keys));
}

static fn settings_menu_apply(int at)
{
        char word[SETTINGS_VALUE], value[SETTINGS_TYPED];
        int row = settings.page.menu_row;
        if (row < 0 || at < 0 || at >= UI_MENU_ITEMS)
                return settings_menu_close();
        string_copy_bounded((string_address)word, (string_address)ui_page_menu_word(&settings.page, at), sizeof(word));
        settings_menu_close();
        if (settings_rows[row].output != SETTINGS_OUT_NONE)
                settings_output_apply(row, word);
        else if (!string_equals((string_address)word, (string_address)settings_shown_value(row)))
                settings_word_of(row, word, value, sizeof(value)), settings_change(row, value);
        else
                settings_say((string_address)WORDS(settings_already_set));
}

static fn settings_act(char what, const char address_to name, const char address_to pass)
{
        char value[SETTINGS_TYPED];
        positive sz;
        if (!name || !name[0] || string_first_of((string_address)name, '\n') != null)
                return;
        if (pass && string_first_of((string_address)pass, '\n') != null)
                return;
        sz = settings_cat(value, sizeof(value) - 1, 2, name);
        value[0] = what, value[1] = '\n';
        if (pass && pass[0])
                value[sz++] = '\n', sz = settings_cat(value, sizeof(value), sz, pass);
        value[sz] = 0;
        settings_change(SETTINGS_JOB_ACT, value);
        settings_wipe(value, sizeof(value));
}

static const char address_to settings_name_accepts(p32 code) { return (code >= '0' && code <= '9') || ((code | 32) >= 'a' && (code | 32) <= 'z') || code == '-' ? null : WORDS(settings_name_chars); }
static fn settings_edit_end(void) { ui_field_close(&settings.edit); ui_page_redraw(&settings.page); }
static fn settings_port_set(const struct settings_item address_to item, b32 allow) { settings_act(allow ? 'v' : 'x', item->port, null); }
static char settings_forget_code(unsigned char kind) { return kind == SETTINGS_NETWORK ? 'f' : kind == SETTINGS_DEVICE ? 'd' : kind == SETTINGS_LINK ? 'r' : 0; }
static fn settings_dismiss_notice(unsigned int code) { if (windows_issue_kernel(code)) windows_kernel_dismiss(settings.own.table, 1u << (code - WINDOWS_ISSUE_KERNEL_GPU)); }

static fn settings_edit_begin(int row, const char address_to text, b32 masked)
{
        struct ui_text address_to t = &settings.edit;
        int dom = settings_domain_of(row, null);
        string_copy_bounded((string_address)settings.edit_from, (string_address)text, sizeof(settings.edit_from));
        ui_field_begin(t, &settings.own, settings.edit_from, row);
        t->masked = masked, ui_field_focus(t, true), t->cancels = dom != SETTINGS_DOM_ROW;
        t->most = dom == SETTINGS_DOM_LIST || dom == SETTINGS_DOM_TRY ? SETTINGS_ITEM_LABEL - 1
                  : dom == SETTINGS_DOM_COMMAND || (dom == SETTINGS_DOM_ROW && (settings_rows[row].flags & SETTINGS_SPLIT)) ? SETTINGS_TYPED - 1 : SETTINGS_VALUE - 1;
        if (row == SETTINGS_NAME_ROW)
                t->accept = settings_name_accepts, t->most = SETTINGS_NAME_MOST, t->most_said = WORDS(settings_name_long);
        ui_page_redraw(&settings.page);
}

static b32 settings_edit_shown(void)
{
        int at = settings_pos(settings_editing()), top = at >= 0 ? ui_list_row_y(&settings.list, at) : 0;
        int tall = at >= 0 && settings.list.tall[at] > 0 ? settings.list.tall[at] : ui_px(&settings.ui, UI_ROW);
        return at >= 0 && top + tall > settings.list.y && top < settings.list.y + settings.list.h;
}

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
                        settings_edit_begin(row, "", true), string_copy_bounded((string_address)settings.joining, (string_address)item->label, sizeof(settings.joining)), settings_say((string_address)WORDS(settings_type_passphrase));
        }
        else if (item->kind == SETTINGS_PORT)
                settings_port_set(item, !(item->flags & SETTINGS_ITEM_JOINED));
        else if (item->kind == SETTINGS_ADD)
                settings_edit_begin(row, "", false), settings_say((string_address)WORDS(settings_type_device));
        else if (item->kind == SETTINGS_LINK)
                settings_toggle(row);
        else if (item->flags & SETTINGS_ITEM_DRIVE)
                settings_say((string_address)WORDS(settings_item_drive));
        else if (item->kind != SETTINGS_NOTE)
                settings_say((string_address)settings_item_text(item));
        ui_page_redraw(&settings.page);
}

static fn settings_forget(void)
{
        const struct settings_item address_to item = settings_item(settings.list.focus);
        char code = item && (item->flags & SETTINGS_ITEM_SAVED) ? settings_forget_code(item->kind) : 0, text[SETTINGS_STATUS];
        if (!code)
                return;
        if (!ui_confirm_ask(&settings.confirm, -1 - (int)item->kind, item->label))
                return language_format(text, sizeof(text), WORDS(settings_forget), item->label), settings_confirm_say(text);
        settings_act(code, item->label, null);
}

static fn settings_row_activate(int row, int offset, int way)
{
        const struct settings_row address_to w = &settings_rows[offset];
        char next[SETTINGS_VALUE];
        if (settings_pair_waiting(row))
                return (void)system_call_2(syscall(kill), (positive)settings.runner.child, SIGTERM);
        if (!w->output && settings.state[row] == SETTINGS_ABSENT)
                return settings_say((string_address)WORDS(settings_not_here));
        if (!w->output && settings.state[row] == SETTINGS_UNREAD)
                return;
        switch (w->kind)
        {
        case SETTINGS_FIELD:  return settings_edit_begin(row, settings.value[row], false), settings_say((string_address)WORDS(settings_edit_keys));
        case SETTINGS_INFO:   return;
        case SETTINGS_CHOICE: return settings_menu_open(row);
        case SETTINGS_ACTION: if (!settings_action_busy(row)) settings_change(row, ""); return;
        case SETTINGS_SLIDER:
        {
                long number = 0, to;
                if (settings.state[row] != SETTINGS_READ || !setting_typed_number(w, (string_address)settings.value[row], &number))
                        return;
                to = number + way * settings_slider_step(w), to = to < w->low ? w->low : to > w->high ? w->high : to;
                if (to != number)
                        positive_into_string((p8 address_to)next, (positive)to), settings_change(row, next);
                return;
        }
        }
        settings_next(w, settings.value[row], way, next, sizeof(next));
        if (next[0] && !string_equals((string_address)next, (string_address)settings.value[row]))
                settings_change(row, next);
}

static fn settings_link_grant_toggle(int at)
{
        if (at < 0 || at >= settings_sub_count || settings_sub[at].action != SETTINGS_SUB_LINK_GRANT ||
            settings_sub[at].part < 0 || settings_sub[at].part >= 4)
                return;
        struct settings_item address_to item = settings_item(settings_sub[at].owner);
        if (!item || item->kind != SETTINGS_LINK)
                return;
        unsigned char bit = (unsigned char)(1u << settings_sub[at].part);
        if (item->where == 0)
        {
                b32 allow = (settings_link_global_grants & bit) == 0;
                if (allow && !settings.hand)
                        return settings_say((string_address)WORDS(settings_virtual_refused));
                settings_link_global_grants ^= bit;
                item->grants = settings_link_global_grants;
                settings_act(allow ? 'L' : 'N', settings_link_grants[settings_sub[at].part].word, null);
                return;
        }
        if (!(settings_link_global_grants & bit))
                return;
        b32 allow = (item->grants & bit) == 0;
        if (allow && !settings.hand)
                return settings_say((string_address)WORDS(settings_virtual_refused));
        item->grants ^= bit;
        settings_act(allow ? 'l' : 'n', item->label, settings_link_grants[settings_sub[at].part].word);
}

static fn settings_activate(int row, int way)
{
        int offset;
        switch (settings_domain_of(row, &offset))
        {
        case SETTINGS_DOM_ROW:     settings_row_activate(row, offset, way); break;
        case SETTINGS_DOM_COMMAND: if (settings_takes(row)) settings_edit_begin(row, "", false); else settings_run(row, ""); break;
        case SETTINGS_DOM_LIST:    if (settings_item(row)) settings_item_activate(row, settings_item(row)); break;
        case SETTINGS_DOM_SUB:
                if (settings_partitions_owner(row) >= 0) settings_toggle(settings_partitions_owner(row)), ui_page_redraw(&settings.page);
                else if (offset < settings_sub_count && settings_sub[offset].action == SETTINGS_SUB_LINK_GRANT) settings_link_grant_toggle(offset), ui_page_redraw(&settings.page);
                break;
        case SETTINGS_DOM_TRY:     if (offset == SETTINGS_TRY_REPEAT) settings_edit_begin(row, "", false); break;
        case SETTINGS_DOM_ABOUT:   settings_about_press(row), ui_page_redraw(&settings.page); break;
        }
}

static fn settings_said_of(int before)
{
        int dom = settings_domain_of(settings.list.focus, null);
        if (dom == SETTINGS_DOM_COMMAND || dom == SETTINGS_DOM_LIST)
                settings_say((string_address)settings_text(settings.list.focus));
        else if (dom >= SETTINGS_DOM_SUB || settings_domain_of(before, null) > SETTINGS_DOM_ROW)
                settings_say((string_address) "");
}

static fn settings_group_choose(int group)
{
        int last = settings_groups() - 1, before = settings.page.chosen;
        b32 typed = settings_filter_length() != 0 || settings_filter_typed();
        group = group < 0 ? 0 : group > last ? last : group;
        if (group == before && !typed)
                return;
        settings.page.chosen = group, settings.map.keys = false, settings.feed_due = 0;
        settings_filter_clear(), ui_scroll_set(&settings.list.scroll, 0), ui_confirm_drop(&settings.confirm), settings_refilter();
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
        while (!settings.edit.masked && start < length && typed[start] == ' ') start++;
        while (!settings.edit.masked && length > start && typed[length - 1] == ' ') length--;
        typed[length] = 0, length -= start;
        switch (settings_domain_of(row, null))
        {
        case SETTINGS_DOM_TRY: break;
        case SETTINGS_DOM_LIST:
        {
                const struct settings_item address_to it = settings_item(row);
                if (!length) settings_say((string_address)WORDS(settings_nothing_typed));
                else if (it && it->kind == SETTINGS_NETWORK) settings_act('j', it->label, typed + start);
                else if (it && it->kind == SETTINGS_ADD) settings_act('a', typed + start, null);
                break;
        }
        case SETTINGS_DOM_COMMAND:
                if (!settings_run(row, typed + start))
                {
                        settings_wipe(typed, sizeof(typed));
                        return;
                }
                break;
        case SETTINGS_DOM_ROW:
                if (!length) settings_say((string_address)WORDS(settings_nothing_kept));
                else if (!string_equals((string_address)typed + start, (string_address)settings.edit_from) && !string_equals((string_address)typed + start, (string_address)settings.value[row]))
                        settings_change(row, typed + start);
                break;
        }
        settings_wipe(typed, sizeof(typed));
        settings_edit_end();
}

static fn settings_edit_asked(p32 asks)
{
        if (asks & UI_TEXT_MENU) settings_field_menu();
        if (asks & UI_TEXT_REFUSED) settings_say((string_address)settings.edit.refused);
        if (asks & (UI_TEXT_SUBMIT | UI_TEXT_COMMIT)) settings_edit_commit();
        else if (asks & UI_TEXT_CANCEL) settings_edit_end();
        else if (asks) ui_page_redraw(&settings.page);
}

static struct ui_text address_to settings_filter_field(void)
{
        if (!settings.filter_open)
                ui_field_open(&settings.filter_edit, &settings.own, ""), settings.filter_edit.most = SETTINGS_FILTER - 1, settings.filter_edit.keeps_vertical = true, settings.filter_open = true;
        return &settings.filter_edit;
}

static b32 settings_filter_typed(void) { return settings.filter_open && doc_length(&settings.filter_edit.doc) != 0; }
static fn settings_filter_clear(void) { if (settings.filter_open) ui_text_set(&settings.filter_edit, (const p8 address_to) "", 0), ui_text_search_changed(&settings.filter_edit); }

static fn settings_filter_asked(p32 asks)
{
        if (asks & UI_TEXT_REFUSED)
                settings_say((string_address)settings.filter_edit.refused);
        if (!asks || (asks & (UI_TEXT_SUBMIT | UI_TEXT_CANCEL)))
                return;
        if (!ui_text_search_changed(settings_filter_field()))
                return ui_page_redraw(&settings.page);
        ui_scroll_set(&settings.list.scroll, 0), settings.page.pane = 1, settings_refilter();
        if (settings.list.count)
                settings.list.focus = settings.list.id[0], ui_page_reveal(&settings.ui, &settings.page, &settings.list);
}

static b32 settings_filter_starts(const struct window_event address_to e)
{
        switch (e->a)
        {
        case UI_KEY_ESCAPE: case UI_KEY_TAB: case UI_KEY_ENTER: case UI_KEY_ENTER_PAD: case UI_KEY_SPACE:
        case UI_KEY_HOME: case UI_KEY_UP: case UI_KEY_PAGE_UP: case UI_KEY_LEFT: case UI_KEY_RIGHT:
        case UI_KEY_END: case UI_KEY_DOWN: case UI_KEY_PAGE_DOWN: case UI_KEY_DELETE: case UI_KEY_BACKSPACE: return false;
        }
        return e->b != ' ';
}

static b32 settings_steps(int row) { return row < SETTINGS_ANY && (settings_rows[row].kind == SETTINGS_STEP || settings_rows[row].kind == SETTINGS_SLIDER); }

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
                length = settings_cat(value, room, settings_cat(value, room, settings_cat(value, room, length, k ? " " : ""), settings.own.table->output_name[k]), " place ");
                length = settings_cat_num(value, room, settings_cat(value, room, settings_cat_num(value, room, length, settings.map.sent_x[k]), " "), settings.map.sent_y[k]);
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
        settings_map_frame(), ui_graph_settle(&settings.ui, &settings.map.graph, at, x, y, false);
        return *x != settings.map.node[at].x || *y != settings.map.node[at].y;
}

static fn settings_map_damage(void) { ui_page_damage(&settings.page, settings.map.panel.y, settings.map.panel.y + settings.map.panel.h); }

static b32 settings_map_key(const struct window_event address_to event)
{
        unsigned int number[WINDOWS_OUTPUTS] = {0}, count = settings_map_load(number), did;
        int at = settings.map.display, dx = (event->a == UI_KEY_RIGHT) - (event->a == UI_KEY_LEFT), dy = (event->a == UI_KEY_DOWN) - (event->a == UI_KEY_UP), x, y;
        if (event->a == UI_KEY_ENTER || event->a == UI_KEY_ENTER_PAD)
                return settings_change(SETTINGS_JOB_IDENTIFY, ""), true;
        if (!dx && !dy)
        {
                settings_map_frame(), did = ui_graph_key(&settings.ui, &settings.map.graph, event);
                if (did & UI_GRAPH_DRAW) settings_map_damage();
                if (did & UI_GRAPH_TAKEN) return true;
                return settings.map.keys = false, false;
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
                if (hit >= 0) settings.map.display = hit;
        }
        return ui_page_redraw(&settings.page), true;
}

static const struct ui_page_keys settings_page_keys = {settings_steps, settings_activate, settings_said_of, settings_group_choose, settings_forget};

static b32 settings_key(const struct window_event address_to event)
{
        if (!(event->flags & WINDOW_KEY_DOWN))
                return ui_graph_key(&settings.ui, &settings.map.graph, event), true;
        if (settings.page.menu_row >= 0 || (settings.popup.open && settings.popup.list && !settings.popup.field))
                return settings_menu_answer(ui_popmenu_event(&settings.ui, &settings.popup.menu, event, 0, 0)), true;
        if (settings_editing() >= 0 && !settings_edit_shown())
        {
                if (settings.edit.cancels) settings_edit_end(); else settings_edit_commit();
                ui_page_redraw(&settings.page);
        }
        if (settings_editing() >= 0)
        {
                if (event->a != UI_KEY_ENTER && event->a != UI_KEY_ENTER_PAD)
                        ui_confirm_drop(&settings.confirm);
                ui_field_focus(&settings.edit, true);
                settings_edit_asked(ui_field_event(&settings.ui, &settings.edit, null, event, 0, 0));
                return true;
        }
        if (settings.page.card)
        {
                b32 moved = false;
                if (ui_lines_key(&settings.ui, &settings.card, (int)event->a, &moved))
                        return moved ? (settings_changed(settings.page.top, settings.page.bottom), true) : true;
                if (event->a == UI_KEY_ESCAPE || event->a == UI_KEY_ENTER || event->a == UI_KEY_ENTER_PAD || event->a == UI_KEY_SPACE || event->a == UI_KEY_BACKSPACE)
                        settings_card_leave();
                return true;
        }
        if (ui_page_context_key(&settings.ui, &settings.page, &settings.list, event))
                return true;
        if (event->a == UI_KEY_DELETE && !settings_filter_typed() && settings.list.focus >= SETTINGS_SUB_BASE && settings.list.focus - SETTINGS_SUB_BASE < settings_sub_count)
                return settings_dismiss_notice(settings_sub[settings.list.focus - SETTINGS_SUB_BASE].code), true;
        if ((event->a == UI_KEY_RIGHT || event->a == UI_KEY_LEFT) && settings.list.focus >= 0 && settings_editing() < 0 && !settings_filter_typed())
        {
                int drive = settings_partitions_owner(settings.list.focus), row = drive >= 0 ? drive : settings.list.focus;
                const struct settings_item address_to lit = drive < 0 && row >= SETTINGS_ANY ? settings_item(row) : null;
                int exp = drive >= 0 ? (settings_exp_get(drive) ? UI_ROW_EXPANDER_OPEN : UI_ROW_EXPANDER_SHUT)
                        : row < SETTINGS_ANY ? settings_expander(row)
                        : (lit && lit->kind == SETTINGS_LINK) ? (settings_exp_get(row) ? UI_ROW_EXPANDER_OPEN : UI_ROW_EXPANDER_SHUT) : 0;
                if (exp && (event->a == UI_KEY_RIGHT) == (exp == UI_ROW_EXPANDER_SHUT))
                        return settings_toggle(row), ui_page_redraw(&settings.page), true;
        }
        if (settings.map.keys)
        {
                if (settings_map_on() && settings.page.pane && settings_map_key(event))
                        return true;
                settings.map.keys = false;
        }
        if (settings.confirm.what < 0 ? event->a != UI_KEY_DELETE : event->a != UI_KEY_ENTER && event->a != UI_KEY_ENTER_PAD && event->a != UI_KEY_SPACE)
                ui_confirm_drop(&settings.confirm);
        if (settings_filter_typed() || settings_filter_starts(event))
        {
                struct ui_text address_to field = settings_filter_field();
                p32 asks;
                ui_field_focus(field, true);
                asks = ui_field_event(&settings.ui, field, &settings.popup, event, 0, 0);
                settings_filter_asked(asks);
                if (asks & UI_TEXT_CANCEL)
                        return settings_filter_clear(), settings_refilter(), true;
                if ((asks & UI_TEXT_SUBMIT) && settings_pos(settings.list.focus) >= 0)
                        settings.page.pane = 1, settings_activate(settings.list.focus, 1);
                if (asks)
                        return true;
        }
        if (event->a == UI_KEY_ESCAPE)
                return settings_stop();
        if (settings.page.pane && event->a == UI_KEY_UP && settings_map_on() && settings.list.focus == settings_output_row(SETTINGS_OUT_SCREEN) && settings_map_count())
                return settings.map.keys = true, ui_page_redraw(&settings.page), true;
        ui_page_key(&settings.ui, &settings.page, &settings.list, (int)event->a, &settings_page_keys);
        return true;
}

/* ---- row description & context menus ---- */

static b32 settings_row_is(int at, const char address_to path) { return at >= 0 && at < (int)SETTINGS_ROWS && settings_rows[at].path && string_equals((string_address)settings_rows[at].path, (string_address)path); }

static b32 settings_is_tabs(int row)
{
        const struct settings_row address_to w = &settings_rows[row];
        char each[SETTINGS_VALUE];
        positive count = 0;
        if (w->kind != SETTINGS_CHOICE || w->output != SETTINGS_OUT_NONE || (w->flags & (SETTINGS_LANGUAGE | SETTINGS_INK | SETTINGS_BATTERIES | SETTINGS_TRANSPORT)))
                return false;
        for (; settings_word(w->choices, count, each, sizeof(each)); count++)
        {
                if (count == 3)
                        return false;
                for (const char address_to p = settings_label_of(row, each); *p; p++)
                        if (*p == ' ')
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
        return settings_word_append(shown, sizeof(shown), settings_word_append(shown, sizeof(shown), 0, language_of("allow")), language_of("deny")), shown;
}

static const char address_to settings_number_shown(int row, const char address_to word)
{
        static char shown[SETTINGS_VALUE];
        const struct settings_row address_to w = &settings_rows[row];
        long number = 0;
        positive len;
        if (w->type != SETTING_NUMBER || w->kind != SETTINGS_FIELD || !setting_typed_number(w, (string_address)word, &number))
                return settings_label_of(row, word);
        len = settings_cat_num(shown, sizeof(shown), 0, number);
        if (setting_percent(w))
                settings_cat(shown, sizeof(shown), len, "%");
        else if (w->unit)
                settings_cat_sep(shown, sizeof(shown), len, " ", w->unit);
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
                row->columns = settings_columns_for(0), row->mark = (item->flags & SETTINGS_ITEM_JOINED) != 0,
                row->note = item->flags & SETTINGS_ITEM_JOINED ? WORDS(settings_joined) : item->flags & SETTINGS_ITEM_SAVED ? WORDS(settings_saved) : null,
                row->value = item->security[0] ? item->security : null, row->bars = item->flags & SETTINGS_ITEM_HEARD ? item->bars : -1,
                row->dim = !(item->flags & SETTINGS_ITEM_HEARD), row->cells[1] = row->note, row->cells[2] = row->value;
        else if (item->kind == SETTINGS_DEVICE || item->kind == SETTINGS_ADD)
                row->columns = settings_columns_for(1), row->value = row->cells[1] = item->kind == SETTINGS_DEVICE ? WORDS(settings_kept) : null;
        else if (item->kind == SETTINGS_PORT)
        {
                b32 waiting = string_equals((string_address)item->state, (string_address) "waiting");
                const char address_to sw = item->state[0] ? settings_state_word(item->state) : null;
                row->columns = settings_columns_for(2), row->kind = UI_ROW_TABS, row->words = settings_port_words(), row->at = item->flags & SETTINGS_ITEM_JOINED ? 0 : 1;
                if (item->flags & SETTINGS_ITEM_DISK)
                        language_format(settings_device_note, sizeof(settings_device_note), WORDS(settings_device_usb_drive), item->port),
                        string_copy_bounded((string_address)settings_device_port_cell, (string_address)settings_device_note, sizeof(settings_device_port_cell)),
                        settings_drive_strip(item, row);
                else if (item->flags & SETTINGS_ITEM_USB)
                {
                        language_format(settings_device_port_cell, sizeof(settings_device_port_cell), WORDS(settings_device_usb_port), item->port);
                        if (waiting) language_format(settings_device_note, sizeof(settings_device_note), WORDS(settings_device_usb), item->port, sw);
                        else string_copy_bounded((string_address)settings_device_note, (string_address)settings_device_port_cell, sizeof(settings_device_note));
                }
                else
                        string_copy_bounded((string_address)settings_device_note, (string_address)(waiting ? sw : ""), sizeof(settings_device_note)),
                        string_copy_bounded((string_address)settings_device_port_cell, (string_address)item->port, sizeof(settings_device_port_cell));
                row->note = settings_device_note[0] ? settings_device_note : null, row->cells[1] = settings_device_port_cell[0] ? settings_device_port_cell : null, row->cells[2] = sw;
        }
        else if (item->flags & SETTINGS_ITEM_DRIVE)
        {
                language_format(settings_device_note, sizeof(settings_device_note), item->where == SETTINGS_WHERE_SD ? WORDS(settings_device_sd) : WORDS(settings_device_drive), settings_state_word(item->state));
                row->note = settings_device_note, row->value = item->segment_count ? null : item->size;
                if (settings_device_section(item) == 1)
                        row->columns = settings_columns_for(2), row->cells[1] = settings_device_note,
                        row->cells[2] = item->state[0] ? settings_state_word(item->state) : null, row->cells[3] = row->value;
                settings_drive_strip(item, row);
        }
        else if (item->kind == SETTINGS_LINK)
        {
                row->mark = (item->flags & SETTINGS_ITEM_JOINED) != 0, row->mark_ink = ui_role(UI_SUCCESS);
                if (item->where == 0)
                        row->turn = settings_fold_turn(id), row->expander = settings_exp_get(id) ? UI_ROW_EXPANDER_OPEN : UI_ROW_EXPANDER_SHUT,
                        row->note = item->detail[0] ? item->detail : null, row->value = item->access[0] ? item->access : null;
                else
                {
                        const char address_to st = item->state[0] ? item->state : item->detail[0] ? item->detail : null;
                        row->turn = settings_fold_turn(id), row->expander = settings_exp_get(id) ? UI_ROW_EXPANDER_OPEN : UI_ROW_EXPANDER_SHUT,
                        row->columns = settings_columns_for(3), row->cells[1] = item->access[0] ? item->access : null,
                        row->cells[2] = st, row->value = st ? st : row->cells[1];
                }
        }
        else if (item->kind == SETTINGS_NOTE)
        {
                if ((list == 0 && settings.item_count[list] > 1) || (list == 2 && settings_device_section(item) == 1)) row->columns = settings_columns_for(list);
                row->dim = true;
        }
}

static fn settings_describe(int id, struct ui_row address_to row)
{
        int offset, dom = settings_domain_of(id, &offset);
        const struct settings_row address_to w = dom == SETTINGS_DOM_ROW ? &settings_rows[offset] : null;
        const struct settings_item address_to item = dom == SETTINGS_DOM_LIST ? settings_item(id) : null;
        unsigned int state = w ? settings.state[offset] : SETTINGS_READ;
        const char address_to shown = w ? settings.value[offset] : "", address_to header;

        if (dom < 0)
                return;
        if (dom == SETTINGS_DOM_SUB) { if (offset < settings_sub_count) settings_sub_describe(offset, row); return; }
        if (dom == SETTINGS_DOM_TRY) return settings_try_describe(id, row);
        if (dom == SETTINGS_DOM_ABOUT) return settings_about_describe(id, row);
        row->section = settings_section_start(id), header = settings_header(id);
        row->header = header && !header[0] ? null : language_of(header), row->label = settings_label_window(id);
        if (w)
                row->about = language_of(w->about), settings_badge(id, row), row->expander = settings_expander(id), row->turn = settings_fold_turn(id);
        if (id == settings_editing())
        {
                const struct settings_generic address_to g = settings_generic_of(id);
                row->kind = UI_ROW_FIELD, row->edit = &settings.edit;
                if (item)
                        row->label = item->kind == SETTINGS_ADD ? WORDS(settings_device_name) : settings.joining;
                else if (!w)
                {
                        const char address_to hint = g ? settings_large->pool + g->command + g->verb : WORDS(settings_verb_hint);
                        if (g) settings_slice(settings.head, sizeof(settings.head), settings_large->pool + g->command, g->verb);
                        else string_copy_bounded((string_address)settings.head, (string_address) "moonwater", sizeof(settings.head));
                        while (*hint == ' ') hint++;
                        row->label = settings.head, row->placeholder = hint, row->head = true;
                }
                return;
        }
        if (item) return settings_describe_item(id, item, row);
        if (!w) return (void)(row->value = settings_row_asks(id) ? WORDS(settings_asks_first) : null);
        if (w->output != SETTINGS_OUT_NONE) return settings_describe_output(id, row);
        if (w->kind == SETTINGS_ACTION)
                return (void)(row->kind = UI_ROW_BUTTON, row->words = settings_button_words(id),
                              row->dim = state == SETTINGS_UNREAD || state == SETTINGS_ABSENT || (settings_action_busy(id) && !settings_pair_waiting(id)),
                              row->note = w->read[0] ? settings_state_text(id, state, shown) : null);
        row->dim = state == SETTINGS_UNREAD || state == SETTINGS_ABSENT, row->value = settings_state_text(id, state, shown);
        if (state != SETTINGS_READ)
                return;
        switch (w->kind)
        {
        case SETTINGS_SWITCH: row->kind = UI_ROW_SWITCH, row->on = string_equals((string_address)shown, (string_address) "on"); break;
        case SETTINGS_STEP:   row->kind = UI_ROW_STEP; break;
        case SETTINGS_CHOICE:
                if (settings_is_tabs(id) && setting_place(w->choices, (string_address)shown) >= 0)
                        row->kind = UI_ROW_TABS, row->words = settings_tab_words(id), row->at = (int)setting_place(w->choices, (string_address)shown);
                else if (settings_row_is(id, SETTING_ACCENT_PATH))
                {
                        const struct settings_ink address_to ink = settings_ink_find(w, shown, false);
                        row->kind = UI_ROW_SWATCHES, row->words = settings.accents;
                        if (ink) row->at = (int)(ink - settings_accents);
                }
                else
                        row->kind = UI_ROW_CHOICE;
                break;
        case SETTINGS_SLIDER:
        {
                long n = 0;
                setting_typed_number(w, (string_address)shown, &n);
                n = n < w->low ? w->low : n > w->high ? w->high : n;
                row->kind = UI_ROW_SLIDER, row->at = (int)n, row->low = (int)w->low, row->high = (int)w->high;
                row->step = settings_slider_step(w), row->words = setting_percent(w) ? "%" : language_of(w->unit);
                break;
        }
        }
}

#define SETTINGS_COPY_VALUE UI_LIST_MENU_COPY
#define SETTINGS_COPY_BOTH UI_LIST_MENU_COPY_ROW
#define SETTINGS_MENU_REMOVE UI_LIST_MENU_REMOVE
#define SETTINGS_MENU_ACT 0x103
#define SETTINGS_KEY_MENU UI_KEY_MENU
#define SETTINGS_KEY_F10 UI_KEY_F10

static const char address_to settings_copy_text(int id)
{
        int offset, dom = settings_domain_of(id, &offset);
        struct ui_row row;
        if (dom < 0 || (dom != SETTINGS_DOM_ABOUT && !settings_large))
                return null;
        switch (dom)
        {
        case SETTINGS_DOM_ROW:
                if (settings_rows[offset].output == SETTINGS_OUT_NONE)
                        return settings.state[offset] == SETTINGS_READ && settings.value[offset][0] ? settings_number_shown(offset, settings.value[offset]) : null;
                memory_zero(&row, sizeof(row)), settings_describe(id, &row);
                return !row.dim && row.value && row.value[0] ? row.value : null;
        case SETTINGS_DOM_COMMAND: return offset > 0 ? settings_label_shown(id) : null;
        case SETTINGS_DOM_LIST:    { const struct settings_item address_to it = settings_item(id); return it && it->kind != SETTINGS_ADD && it->label[0] ? it->label : null; }
        case SETTINGS_DOM_SUB:
                if (offset >= settings_sub_count) return null;
                if (settings_sub[offset].action == SETTINGS_SUB_COPY) return settings_sub[offset].text[0] ? settings_sub[offset].text : null;
                if (settings_sub[offset].action != SETTINGS_SUB_PARTITION) return null;
                memory_zero(&row, sizeof(row)), settings_describe(id, &row);
                return row.label && row.label[0] ? row.label : null;
        case SETTINGS_DOM_ABOUT:   return settings_about_url(offset);
        default:                   return null;
        }
}

static p32 settings_row_menu_flags(int id)
{
        int offset, dom = settings_domain_of(id, &offset);
        b32 has = dom >= 0 && settings_copy_text(id) != null;
        switch (dom)
        {
        case SETTINGS_DOM_ROW: case SETTINGS_DOM_ABOUT: return UI_LIST_MENU_VALUE_WORDS | UI_LIST_MENU_HAS_COPY_ROW | (has ? UI_LIST_MENU_HAS_COPY : 0);
        case SETTINGS_DOM_COMMAND: return offset > 0 && has ? UI_LIST_MENU_HAS_COPY | UI_LIST_MENU_HAS_COPY_ROW : 0;
        case SETTINGS_DOM_LIST:
        {
                const struct settings_item address_to it = settings_item(id);
                p32 copy = has ? UI_LIST_MENU_HAS_COPY | UI_LIST_MENU_HAS_COPY_ROW : 0;
                if (!it || it->kind == SETTINGS_ADD) return 0;
                if (it->kind == SETTINGS_NOTE) return has ? UI_LIST_MENU_HAS_COPY : 0;
                if (it->kind == SETTINGS_NETWORK) return copy | ((it->flags & SETTINGS_ITEM_SAVED) ? UI_LIST_MENU_CAN_REMOVE : 0);
                return copy | (it->kind == SETTINGS_DEVICE || (it->kind == SETTINGS_LINK && it->where == 1) ? UI_LIST_MENU_CAN_REMOVE : 0);
        }
        case SETTINGS_DOM_SUB:
                if (offset >= settings_sub_count) return 0;
                if (settings_sub[offset].action == SETTINGS_SUB_COPY) return (has ? UI_LIST_MENU_HAS_COPY : 0) | (windows_issue_kernel(settings_sub[offset].code) ? UI_LIST_MENU_CAN_REMOVE : 0);
                return settings_sub[offset].action == SETTINGS_SUB_PARTITION && has ? UI_LIST_MENU_HAS_COPY | UI_LIST_MENU_HAS_COPY_ROW : 0;
        default: return 0;
        }
}

static fn settings_row_menu_extra(struct ui_popmenu address_to m, int id)
{
        const struct settings_item address_to it = settings_item(id);
        const char address_to label = !it ? null
            : it->kind == SETTINGS_NETWORK && !(it->flags & (SETTINGS_ITEM_JOINED | SETTINGS_ITEM_CANNOT)) ? WORDS(installer_wifi_join)
            : it->kind == SETTINGS_PORT ? ((it->flags & SETTINGS_ITEM_JOINED) ? WORDS(settings_device_deny) : WORDS(mixer_allow)) : null;
        if (label)
                ui_popmenu_add(m, label, null, 0, SETTINGS_MENU_ACT), ui_popmenu_add(m, null, null, UI_MENU_SEPARATOR, 0);
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
                return (void)language_format(into, room, WORDS(settings_copy_line), dom == SETTINGS_DOM_ROW ? settings_label_window(id) : settings_label_shown(id), value);
        if (dom == SETTINGS_DOM_COMMAND && offset > 0)
                second = settings_text(id);
        else if (dom == SETTINGS_DOM_LIST || (dom == SETTINGS_DOM_SUB && offset < settings_sub_count && settings_sub[offset].action == SETTINGS_SUB_PARTITION))
        {
                struct ui_row row;
                positive at = 0;
                memory_zero(&row, sizeof(row)), settings_describe(id, &row);
                if (dom == SETTINGS_DOM_SUB)
                        at = settings_cat_sep(summary, sizeof(summary), settings_cat_sep(summary, sizeof(summary), 0, " ", row.amount), " ", row.share);
                else
                {
                        const struct settings_item address_to it = settings_item(id);
                        if (it && it->kind == SETTINGS_PORT)
                                at = settings_cat_sep(summary, sizeof(summary), settings_cat_sep(summary, sizeof(summary), 0, ", ", row.cells[1]), ", ", row.cells[2]);
                        else
                                at = settings_cat_sep(summary, sizeof(summary), settings_cat_sep(summary, sizeof(summary), 0, ", ", row.note), ", ",
                                                      row.value ? row.value : (it && (it->flags & SETTINGS_ITEM_DRIVE) ? it->size : null));
                }
                (void)at, second = summary;
        }
        if (second && second[0]) language_format(into, room, WORDS(settings_copy_line), value, second);
        else string_copy_bounded((string_address)into, (string_address)value, room);
}

static fn settings_row_copied(int id)
{
        (void)id;
        // The list offers the copy and then tells the app: the row was copied when the offer took, which the serial the menu opened on says.
        settings_offered(settings.own.table && settings.own.table->clip_offer_serial[settings.own.row] != settings_copy_serial);
}
static fn settings_row_before_menu(int before, int row) { settings_copy_serial = settings.own.table ? settings.own.table->clip_offer_serial[settings.own.row] : 0; settings_menu_close(); if (row != before) ui_confirm_drop(&settings.confirm), settings_said_of(before); }

static fn settings_row_menu_act(int id, int item_id)
{
        if (item_id == SETTINGS_MENU_REMOVE)
        {
                int offset, dom = settings_domain_of(id, &offset);
                if (dom == SETTINGS_DOM_LIST)
                {
                        const struct settings_item address_to it = settings_item(id);
                        char code = it && (it->flags & SETTINGS_ITEM_SAVED) ? settings_forget_code(it->kind) : 0;
                        if (code) settings_act(code, it->label, null);
                }
                else if (dom == SETTINGS_DOM_SUB && offset < settings_sub_count)
                        settings_dismiss_notice(settings_sub[offset].code);
        }
        else if (item_id == SETTINGS_MENU_ACT)
        {
                const struct settings_item address_to it = settings_item(id);
                if (it && it->kind == SETTINGS_NETWORK) settings_item_activate(id, it);
                else if (it && it->kind == SETTINGS_PORT) settings_port_set(it, !(it->flags & SETTINGS_ITEM_JOINED));
        }
}

static const struct ui_list_menu settings_list_menu = {
    .flags = settings_row_menu_flags, .copy = settings_copy_text, .copy_row = settings_copy_row_text,
    .extra = settings_row_menu_extra, .act = settings_row_menu_act, .copied = settings_row_copied, .before_open = settings_row_before_menu,
};

static fn settings_field_menu(void)
{
        struct ui_text address_to t = &settings.edit;
        struct ui_popmenu address_to m = &settings.popup.menu;
        int row = settings_editing();
        if (!t->own)
                return;
        m->count = 0, m->typed_length = 0, ui_text_menu_items(m, t, false), settings.popup.list = null, settings.popup.list_row = -1;
        if (row >= 0 && row < SETTINGS_ANY)
                ui_list_menu_attach(&settings.popup, &settings.list, row);
        settings.popup.field = t;
        if (!ui_popmenu_open(&settings.popup, t->own, &settings.ui, t->menu_x, t->menu_y, 0, 0, WINDOW_POPUP_POINT, ui_kit_touch))
                settings.popup.list = null, settings.popup.list_row = -1;
}

/* ---- pointer/touch events, drawing, fold stepping & lifecycle ---- */

static fn settings_press_at(void);

static b32 settings_map_event(const struct window_event address_to event)
{
        struct ui_graph address_to g = &settings.map.graph;
        unsigned int number[WINDOWS_OUTPUTS] = {0}, did;
        int x = ui_kit.pointer_x, y = ui_kit.pointer_y;
        const struct ui_rect address_to p = &settings.map.panel;
        b32 inside = x >= p->x && x < p->x + p->w && y >= p->y && y < p->y + p->h, had = g->held >= 0 || g->panning;

        settings_map_load(number), settings_map_frame();
        if (event->type == WINDOW_EVENT_BUTTON && (event->flags & WINDOW_KEY_DOWN) && inside)
        {
                settings.map.keys = g->keys = false;
                if (event->a == UI_POINTER_LEFT && ui_control_hit(&settings.ui, &settings.map.identify, x, y) >= 0)
                        return settings_change(SETTINGS_JOB_IDENTIFY, ""), true;
                if (event->a == UI_POINTER_LEFT && ui_control_hit(&settings.ui, &settings.map.fit, x, y) >= 0)
                        return ui_graph_home(&settings.ui, g), settings_map_damage(), true;
        }
        did = ui_graph_event(&settings.ui, g, event);
        if (did & UI_GRAPH_CHOSE) settings.map.display = g->chosen;
        if (did & UI_GRAPH_DROP) settings_map_place(g->dropped, g->at_x, g->at_y);
        if ((did & (UI_GRAPH_CHOSE | UI_GRAPH_DROP)) || (event->type == WINDOW_EVENT_BUTTON && (did & UI_GRAPH_TAKEN)) || ((did & UI_GRAPH_DRAG) && !had))
                ui_page_redraw(&settings.page);
        else if (did & UI_GRAPH_DRAW)
                settings_map_damage();
        return event->type == WINDOW_EVENT_MOTION ? had : (did & UI_GRAPH_TAKEN) != 0;
}

// A slider is written while the hand moves it: each value the kit reports while a drag holds it goes out once (the queue keeps one
// change per row), and the release writes the value it was left at.
static int settings_live_sent = -1;
static void settings_slid(void)
{
        int row = settings.list.slide > 0 ? settings.list.slide - 1 : settings.list.slid - 1;
        char word[SETTINGS_VALUE];
        b32 dragging = settings.list.slide > 0;
        if (dragging)
        {
                if (row < 0 || row >= SETTINGS_ANY || settings_rows[row].kind != SETTINGS_SLIDER || settings.state[row] != SETTINGS_READ ||
                    settings.list.slid_value == settings_live_sent)
                        return;
                settings_live_sent = settings.list.slid_value;
                positive_into_string((p8 address_to)word, (positive)settings.list.slid_value);
                if (!string_equals((string_address)word, (string_address)settings.value[row]))
                        settings_change(row, word);
                return;
        }
        if (settings.list.slid <= 0)
                return;
        settings.list.slid = 0;
        settings_live_sent = -1;
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
                if (redraw)
                        if (gallery.band_bottom > gallery.band_top) ui_page_damage(&settings.page, gallery.band_top, gallery.band_bottom);
                        else ui_page_redraw(&settings.page);
                return keep;
        }
        if (settings_map_on() && settings.page.menu_row < 0 && event->type != WINDOW_EVENT_KEY) { if (settings_map_event(event)) return true; }
        else if (!settings_map_on()) ui_graph_let_go(&settings.map.graph);
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
                                return ui_page_redraw(&settings.page), true;
                }
                if (settings.filter_open && (asks = ui_field_event(&settings.ui, &settings.filter_edit, &settings.popup, event, x, y)) &&
                    (event->type != WINDOW_EVENT_BUTTON || (asks != UI_TEXT_COMMIT && asks != UI_TEXT_CANCEL)))
                        return settings_filter_asked(asks), ui_page_redraw(&settings.page), true;
        }
        switch (event->type)
        {
        case WINDOW_EVENT_KEY:    return settings_key(event);
        case WINDOW_EVENT_MOTION: case WINDOW_EVENT_BUTTON: ui_page_pointer(&settings.ui, &settings.page, &settings.list, event, settings_press_at), settings_slid(); break;
        case WINDOW_EVENT_WHEEL:  ui_page_wheel(&settings.ui, &settings.page, event); break;
        }
        return true;
}

static b32 settings_event(const struct window_event address_to event) { b32 t; settings.hand = ui_event_hand(event); t = settings_event_take(event); settings.hand = false; return t; }

static fn settings_press_at(void)
{
        int x = ui_kit.pointer_x, y = ui_kit.pointer_y, row, before = settings.list.focus, offset, dom;
        if (settings.page.card)
                return settings_card_leave();
        if ((row = ui_page_column_hit(&settings.ui, &settings.page, x, y)) >= 0)
                return settings_edit_end(), settings.page.pane = 0, settings_group_choose(row), ui_page_redraw(&settings.page);
        // ui_page_pointer already ran ui_list_pointer(..., true) on press (setting focus/part/way and starting the single press ripple).
        row = ui_list_hit(&settings.ui, &settings.list, x, y);
        if (settings_editing() >= 0 && row != settings_editing())
                settings_edit_end();
        if (row < 0)
                return;
        dom = settings_domain_of(row, &offset);
        if (dom == SETTINGS_DOM_SUB)
        {
                if (settings_partitions_owner(row) >= 0) settings_toggle(settings_partitions_owner(row));
                else if (offset < settings_sub_count && settings_sub[offset].action == SETTINGS_SUB_LINK_GRANT) settings_link_grant_toggle(offset);
                else if (offset < settings_sub_count && settings_sub[offset].action == SETTINGS_SUB_DISMISS_ALL) windows_kernel_dismiss(settings.own.table, KERNEL_NOTICE_ALL);
                else if (offset < settings_sub_count && settings_sub[offset].action != SETTINGS_SUB_PARTITION) settings_copy(offset);
                return ui_page_redraw(&settings.page);
        }
        if (dom == SETTINGS_DOM_TRY || dom == SETTINGS_DOM_ABOUT)
        {
                settings.page.pane = 1;
                if (dom == SETTINGS_DOM_TRY) settings_try_press(row, x, y); else settings_about_press(row);
                return ui_page_redraw(&settings.page);
        }
        if (row < SETTINGS_ANY && ui_list_expander_at(&settings.ui, &settings.list, row, x))
                return settings_toggle(row), ui_page_redraw(&settings.page);
        if (row != before)
                ui_confirm_drop(&settings.confirm), settings_said_of(before);
        settings.page.pane = 1, ui_page_redraw(&settings.page);
        if (settings.list.part >= 0 && row < (int)SETTINGS_ALL && settings_item(row) && settings_item(row)->kind == SETTINGS_PORT)
        {
                const struct settings_item address_to it = settings_item(row);
                if (((it->flags & SETTINGS_ITEM_JOINED) != 0) != (settings.list.part == 0))
                        settings_port_set(it, settings.list.part == 0);
                return;
        }
        if (settings.list.part >= 0 && row < SETTINGS_ANY && settings.state[row] == SETTINGS_READ)
        {
                char word[SETTINGS_VALUE];
                if (settings_row_is(row, SETTING_ACCENT_PATH) && settings.list.part < (int)(sizeof(settings_accents) / sizeof(settings_accents[0])))
                        settings_change(row, settings_accents[settings.list.part].name);
                else if (settings_word(settings_rows[row].choices, (positive)settings.list.part, word, sizeof(word)))
                        if (string_equals((string_address)word, (string_address)settings.value[row])) settings_say((string_address)WORDS(settings_already_set));
                        else settings_change(row, word);
                return;
        }
        if (settings_item(row) && (settings_item(row)->flags & SETTINGS_ITEM_DISK))
                return;
        if (settings_editing() != row && !(row < SETTINGS_ANY && settings_rows[row].kind == SETTINGS_SLIDER))
                settings_activate(row, settings.list.way);
}

static void settings_map_paint(struct ui address_to ui)
{
        unsigned int number[WINDOWS_OUTPUTS] = {0};
        if (settings_map_on())
                settings_map_load(number), settings_map_frame(), ui_graph_draw(ui, &settings.map.graph);
}

static void settings_card_paint(struct ui address_to ui)
{
        if (settings_pair_on && settings.runner.child > 0) settings_pair_paint(ui);
        else ui_lines_draw(ui, &settings.card, ui_px(ui, UI_PAD), &settings.card_text);
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
        parts.keys = settings_editing() < 0 && settings.focused, parts.group_name = settings_group_text, parts.groups = settings_groups();
        parts.column_keys = settings.focused && !settings.page.pane, parts.column_off = settings_filter_length() != 0;
        parts.list_lit = (settings.page.pane || settings_filter_length()) && settings_editing() < 0 && !settings.map.keys;
        parts.list_keys = settings.focused && settings.page.pane && !settings.map.keys;
        parts.list = &settings.list, parts.between = settings_map_paint, parts.card = settings_card_paint;
        parts.overlays[0] = settings_editing() >= 0 && settings_edit_shown() ? &settings.edit : null;
        parts.overlays[1] = settings.filter_open && !settings.page.card ? &settings.filter_edit : null;
        ui_page_paint(ui, &settings.page, &parts);
}

static b32 settings_frame(void)
{
        ui_list_fold_step(&settings.ui, &settings.list);
        if (settings.own.table && settings.width > 0 && settings_least() &&
            ((settings.width < settings.least_w && settings.width < (int)settings.own.table->output_width &&
              (!settings.own.table->stride || settings.width < (int)settings.own.table->stride)) ||
             (settings.height < settings.least_h && settings.height < (int)settings.own.table->output_height)))
                settings_size((unsigned int)settings.width, (unsigned int)settings.height);
        if (!ui_buffers_present(&settings.page.buffers, &settings.own, settings.width, settings.height, settings_draw_rows, null))
                return false;
        return settings.page.dirty = false, true;
}

static fn settings_size(unsigned int width, unsigned int height)
{
        struct windows address_to t = settings.own.table;
        unsigned int most = t->stride && t->stride < t->output_width ? t->stride : t->output_width;
        if (settings_least())
                width = width < (unsigned int)settings.least_w ? (unsigned int)settings.least_w : width,
                height = height < (unsigned int)settings.least_h ? (unsigned int)settings.least_h : height;
        settings.width = (int)(width > most ? most : width), settings.height = (int)(height > t->output_height ? t->output_height : height);
        ui_scratch_fit(&settings.ui, (unsigned int)settings.width);
        if (settings.page.card)
                ui_lines_wrap(&settings.ui, &settings.card_text, settings.width);
        settings_layout(), ui_page_column_show(&settings.ui, &settings.page), ui_page_menu_close(&settings.page, &settings.popup), ui_page_redraw(&settings.page);
}

static fn settings_language(void)
{
        settings_say((string_address) ""), settings_gpu_key = 0xffffffffu;
        if (settings.width > 0 && settings.height > 0)
                settings_layout();
        ui_page_redraw(&settings.page);
}

static b32 settings_notices_asked;
static int settings_system_group(void) { int r = settings_output_row(SETTINGS_OUT_NOTICES); return r >= 0 ? settings_group_of(r) : 0; }
static void settings_asked(unsigned int page) { settings_group_choose((int)page); }

static int settings_open(void)
{
        struct windows address_to t = settings.own.table;
        int width = ui_px(&settings.ui, gallery.on ? GALLERY_W : SETTINGS_WIDTH), height = ui_px(&settings.ui, gallery.on ? GALLERY_H : SETTINGS_HEIGHT);
        int room = (int)t->output_height - ui_px(&settings.ui, 2 * (24 + 24));
        if (!gallery.on && (settings_notices_asked ? ui_lift_row(&settings.own, (string_address) "settings", false, (unsigned int)settings_system_group() + 1)
                                                   : ui_lift_other(&settings.own, (string_address) "settings", false)))
                return 0;
        if (settings_notices_asked)
                settings.page.chosen = settings_system_group();
        settings_refilter();
        if (gallery.on)
                gallery_build(&settings.own, settings_name_accepts, SETTINGS_NAME_MOST);
        settings_size((unsigned int)width, (unsigned int)(height < room ? height : room > 0 ? room : height));
        ui_free(t, settings.own.row, settings.width, settings.height, gallery.on ? 0 : ui_px(&settings.ui, 12));
        if (!gallery.on)
                settings_say((string_address) ""), settings_ask_all(), settings.help = 1, settings_next_job();
        settings_frame();
        return -1;
}

static fn settings_chose(int id)
{
        struct ui_text address_to t = settings.popup.field;
        if (ui_list_menu_do(&settings.popup, &settings.own, id))
                return ui_page_redraw(&settings.page);
        if (!t && settings.page.menu_row >= 0)
                return id >= 0 ? settings_menu_apply(id) : settings_menu_close();
        if (t == &settings.edit && settings_editing() >= 0)
                settings_edit_asked(ui_text_menu_do(&settings.ui, t, id));
        else if (t == &settings.filter_edit && settings_editing() < 0)
                settings_filter_asked(ui_text_menu_do(&settings.ui, t, id)), ui_page_redraw(&settings.page);
        else if (t && (t == &gallery.field[0] || t == &gallery.field[1]))
                ui_text_menu_do(&settings.ui, t, id), ui_page_redraw(&settings.page);
}

static long settings_tick(long timeout, long address_to other)
{
        if (settings_pair_on && settings.page.card && settings.runner.child > 0)
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
                struct ui_text address_to t = gallery.on ? &gallery.field[gallery.field[1].focused] : settings_editing() >= 0 ? &settings.edit : &settings.filter_edit;
                timeout = ui_field_tick(&settings.ui, &settings.page, t, timeout, gallery.on ? t->focused : settings.focused);
        }
        timeout = ui_timeout_min(timeout, settings_keep_tick());
        if (other)
                *other = -1;
        settings_runner_ready();
        if (settings.runner.child > 0 && settings.runner.ending == 1 && settings.runner.timeout_ns && (positive)ui_kit.now >= settings.runner.began + settings.runner.timeout_ns)
                return ui_job_kill(&settings.runner), 0;
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
        settings_wipe(settings_pair_code, sizeof(settings_pair_code));
        settings_wipe(settings.job_value, sizeof(settings.job_value));
        settings_wipe(settings.status, sizeof(settings.status));
        if (settings_large)
        {
                settings_wipe(settings_large->said, sizeof(settings_large->said));
                settings_wipe(settings_large->card, sizeof(settings_large->card));
                settings_wipe(settings_large->split, sizeof(settings_large->split));
        }
        if (settings.queue && settings.queue_room)
                settings_wipe(settings.queue, settings.queue_room * sizeof(settings.queue[0]));
        ui_field_close(&settings.edit);
        if (settings.runner.child > 0)
        {
                positive status = 0;
                system_call_2(syscall(kill), (positive)settings.runner.child, settings_pair_on ? SIGTERM : SIGKILL);
                system_wait4_retry(settings.runner.child, address_of status, 1, null);
        }
}

static b32 settings_focus(b32 focused)
{
        if (!focused)
                ui_graph_let_go(&settings.map.graph), ui_page_let_go(&settings.ui, &settings.page, &settings.list);
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
        settings.moonwater = (string_address) "/moonwater", ui_buffers_start(&settings.page.buffers);
        settings.runner.output = -1, settings.page.pane = 1, settings.page.menu_row = -1, settings_runner_ready();
        for (positive at = 1; at < (positive)program_argument_count();)
        {
                positive used = ui_app_option(&settings_app, at);
                string_address value;
                if (used) at += used;
                else if ((value = ui_argument(at, (string_address) "-m"))) settings.moonwater = value, at += 2;
                else if (string_equals(program_argument(at), (string_address) "-n")) settings_notices_asked = true, at++;
                else if (string_equals(program_argument(at), (string_address) "-g"))
                        gallery.on = 1, settings_app.glass = 0, settings_app.side = false, settings_app.mono = 1,
                        settings_app.title = (string_address) "Gallery", settings_app.name = (string_address) "gallery", at++;
                else at++;
        }
        if (!settings_map())
                return compositor_kmsg((string_address) "settings: no memory, pid ", (positive)compositor_call(syscall(getpid), 0, 0, 0, 0, 0, 0), (string_address) ""), 1;
        return ui_app_run(&settings_app);
}

#endif // MOONWATER_CANVAS_SETTINGS
