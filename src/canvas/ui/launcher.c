/*
        The launcher: compositor overlay for launching apps, PATH programs, and shell commands.

        Dawn Larsson - Apache-2.0 license
        github.com/dawnlarsson/moonwater
*/
#ifndef MOONWATER_CANVAS_LAUNCHER
#define MOONWATER_CANVAS_LAUNCHER

#define LAUNCHER_WIDTH 560 // logical pixels
#define LAUNCHER_NARROW 480
#define LAUNCHER_ROWS 7
#define LAUNCHER_TEXT 256
#define LAUNCHER_NAMES 4096
#define LAUNCHER_NAME 40
#define LAUNCHER_DIRS 64
#define LAUNCHER_PATH 4096
#define LAUNCHER_HASH 8192
#define LAUNCHER_DIRENTS 16384
#define LAUNCHER_DIRENT_HEADER 19
#define LAUNCHER_WORDS 16
#define LAUNCHER_FULL (LAUNCHER_PATH + LAUNCHER_NAME + 2)

#define LAUNCHER_TILE 88
#define LAUNCHER_GRID_LINES 3
#define LAUNCHER_RECENT 8
#define LAUNCHER_RECENT_PATH "/root/launcher.recent"
#define LAUNCHER_MENU_PIN 100
#define LAUNCHER_MENU_UNPIN 101
#define LAUNCHER_MENU_UNINSTALL 102

#define LAUNCHER_WINDOW 1u  // app window
#define LAUNCHER_PROGRAM 2u // PATH program in terminal
#define LAUNCHER_COMMAND 3u // shell command in terminal

#define launcher_program ui_kit_program
#define launcher_programs ui_kit_programs
#define LAUNCHER_PROGRAMS UI_KIT_PROGRAMS

#define LAUNCHER_POWERS 3
#define LAUNCHER_ALWAYS 2
#define LAUNCHER_SLEEP 1u
#define LAUNCHER_POWER_STATE "/sys/power/state"

static const struct launcher_power
{
        unsigned int label, says;
        const char address_to command;
} launcher_powers[LAUNCHER_POWERS] = {
    {LANGUAGE_launcher_power_off, LANGUAGE_launcher_power_off_armed, "poweroff"},
    {LANGUAGE_launcher_restart, LANGUAGE_launcher_restart_armed, "reboot"},
    {LANGUAGE_launcher_sleep, LANGUAGE_launcher_sleep_armed, "moonwater sleep"},
};

// Offer every app except /installer on non-removable internal disk boots.
static b32 launcher_offered(positive at)
{
        static int offer = -1;
        char verdict[64], path[160], text[8], link[256];
        bipolar got;

        if (at >= (positive)ui_apps.count || !string_equals((string_address)ui_apps.app[at].launch, (string_address) "/installer"))
                return at < (positive)ui_apps.count;
        if (offer >= 0)
                return offer;
        offer = 0;
        got = file_slurp_once_at(AT_FDCWD, (string_address) "/run/moonwater/verdict", (p8 address_to)verdict, sizeof(verdict) - 1);
        if (got >= 4 && (!memory_compare(verdict, "live", 4) || !memory_compare(verdict, "ask ", 4)))
                offer = 1;
        else if (got > 5 && !memory_compare(verdict, "disk ", 5) && verdict[5] != '.')
        {
                positive length = 5;

                while (length < (positive)got && verdict[length] > ' ' && verdict[length] != '/')
                        length++;
                verdict[length] = 0;
                string_copy_bounded((string_address)path, (string_address) "/sys/block/", sizeof(path));
                string_append_bounded((string_address)path, (string_address)verdict + 5, sizeof(path));
                string_append_bounded((string_address)path, (string_address) "/removable", sizeof(path));
                got = file_slurp_once_at(AT_FDCWD, (string_address)path, (p8 address_to)text, sizeof(text) - 1);
                offer = got > 0 && text[0] == '1';
                string_copy_bounded((string_address)path, (string_address) "/sys/class/block/", sizeof(path));
                string_append_bounded((string_address)path, (string_address)verdict + 5, sizeof(path));
                got = system_call_4(syscall(readlinkat), (positive)AT_FDCWD, (positive)path, (positive)link, sizeof(link) - 1);
                if (got > 0 && got < (bipolar)sizeof(link))
                        link[got] = 0, offer |= string_find((string_address)link, (string_address) "/usb") != null;
        }
        return offer;
}

static positive launcher_offered_count(void)
{
        positive count = 0;

        for (positive at = 0; at < (positive)ui_apps.count; at++)
                count += launcher_offered(at) != 0;
        return count;
}

static const char address_to const launcher_hidden[] = {"compositor", "init", "launcher", null};

struct launcher_entry
{
        unsigned char kind;
        unsigned short index;
};

static struct
{
        struct window_row own;
        struct ui ui;
        b32 toggle;
        const char address_to regular;
        const char address_to mono;
        const char address_to mono_bold;

        struct ui_text field;
        struct ui_control search;
        struct ui_popup menu;
        char text[LAUNCHER_TEXT];
        struct launcher_entry entry[LAUNCHER_ROWS];
        positive entries;
        struct ui_list list;

        unsigned short apps[UI_APPS_MOST];
        int app_count;
        unsigned short recent[LAUNCHER_RECENT];
        int recent_count;
        struct ui_list grid, recents;
        b32 in_recents;
        int menu_app;
        unsigned int generation;

        char address_to names;
        unsigned char address_to from;
        unsigned short address_to hashed;
        positive count;
        char path[LAUNCHER_PATH];
        const char address_to dirs[LAUNCHER_DIRS];
        positive dir_count;

        unsigned short named[LAUNCHER_NAMES];
        positive named_count, named_length, named_names;
        char named_text[LAUNCHER_TEXT];

        int width, height;
        struct ui_buffers buffers;
        unsigned int ground;
        b32 focused, dirty;

        b32 asked_beside, moved;
        struct ui_anchor anchor;

        struct ui_view view;
        unsigned int power_runs;
        unsigned int session;
        b32 kept_start; // -k: started put away (the rules at the desktop's start), to be shown by its button with no program started
        // -p: a picker (Settings' startup programs asks it): it looks as the start menu does, its search finds the apps alone, and a choice
        // is said on standard output (launcher_pick) in place of a start; nothing chosen, it ends with 1. Never the resident launcher.
        b32 pick, picked;
} launcher;

/* ---- model: ranking, search, and launch argv ---- */

static b32 launcher_command_line(const char address_to text)
{
        return text[0] && string_first_of_set((string_address)text, (string_address) " \t\n|&;<>()$`\\\"'*?[]{}~#!=/");
}

static positive launcher_lower(char address_to into, positive room, const char address_to text, positive size)
{
        memory_utf8_state state = {0};
        positive made = 0;

        for (positive at = 0; at < size && made + 4 < room;)
        {
                b32 said = memory_utf8_feed(&state, (p8)text[at++]);

                if (said > 0)
                        made += memory_utf8_encode((p8 address_to)into + made, 4, (p32)unicode_case(state.value, UNICODE_CASE_LOWER));
                else if (said < 0)
                        into[made++] = text[at - 1], state = (memory_utf8_state){0};
        }
        into[made] = 0;
        return made;
}

static b32 launcher_ascii(const char address_to text, positive size)
{
        for (positive at = 0; at < size; at++)
                if ((unsigned char)text[at] >= 0x80)
                        return false;
        return true;
}

// Match rank: 3 exact, 2 prefix, 1 substring, 0 none (Unicode case-folded when non-ASCII).
static unsigned int launcher_rank(const char address_to text, positive length, const char address_to name, positive size)
{
        char folded_text[96], folded_name[96];

        if (!length || length > size)
                return 0;
        if (!launcher_ascii(text, length) || !launcher_ascii(name, size))
        {
                length = launcher_lower(folded_text, sizeof(folded_text), text, length);
                size = launcher_lower(folded_name, sizeof(folded_name), name, size);
                text = folded_text, name = folded_name;
                if (!length || length > size)
                        return 0;
        }
        if (!memory_compare_ascii_case((address_any)text, (address_any)name, length))
                return length == size ? 3 : 2;
        return memory_search_ascii_case((address_any)name, size, (address_any)text, length) ? 1 : 0;
}

static unsigned int launcher_names_rank(const char address_to text, positive length, const char address_to label, const char address_to words)
{
        unsigned int best = launcher_rank(text, length, label, string_length((string_address)label));

        while (*words)
        {
                positive size = (positive)(string_first_of_or_end((string_address)words, ' ') - (string_address)words);
                unsigned int rank = launcher_rank(text, length, words, size);

                if (rank > best)
                        best = rank;
                words += size + (words[size] == ' ');
        }
        return best;
}

static unsigned int launcher_program_rank(const char address_to text, positive length, const struct launcher_program address_to program)
{
        unsigned int best = launcher_names_rank(text, length, program->label, program->words), said;

        if (!language.loaded)
                return best;
        said = launcher_names_rank(text, length, language_of(program->label), language_of(program->words));
        return said > best ? said : best;
}

static unsigned int launcher_app_rank(const char address_to text, positive length, int which)
{
        const struct ui_app_entry address_to app = &ui_apps.app[which];

        return app->kit ? launcher_program_rank(text, length, &launcher_programs[app->kit - 1])
                        : launcher_names_rank(text, length, app->name, app->words);
}

static b32 launcher_before(unsigned int rank, const char address_to name, unsigned int other_rank, const char address_to other)
{
        positive size = string_length((string_address)name), other_size = string_length((string_address)other);

        if (rank != other_rank)
                return rank > other_rank;
        if (size != other_size)
                return size < other_size;
        return string_compare((string_address)name, (string_address)other) < 0;
}

static const char address_to launcher_name(positive index)
{
        return launcher.names + index * LAUNCHER_NAME;
}

static b32 launcher_hidden_name(const char address_to name)
{
        for (positive at = 0; launcher_hidden[at]; at++)
                if (string_equals((string_address)name, (string_address)launcher_hidden[at]))
                        return true;
        for (positive at = 0; at < LAUNCHER_PROGRAMS; at++)
                if (string_equals((string_address)name, (string_address)launcher_programs[at].command + 1))
                        return true;
        for (int at = 0; at < ui_apps.count; at++)
                if (!ui_apps.app[at].kit && !ui_apps.app[at].terminal &&
                    (string_equals((string_address)name, (string_address)ui_apps_base(ui_apps.app[at].launch)) ||
                     string_equals((string_address)name, (string_address)ui_apps.app[at].profile)))
                        return true;
        return false;
}

static fn launcher_apps_make(void)
{
        int count = 0;

        for (int at = 0; at < ui_apps.count && count < UI_APPS_MOST; at++)
                if (launcher_offered((positive)at))
                {
                        const char address_to name = ui_apps_name(at);
                        int place = count++;

                        while (place > 0 && !ui_apps.app[at].kit && !ui_apps.app[launcher.apps[place - 1]].kit)
                        {
                                const char address_to other = ui_apps_name(launcher.apps[place - 1]);
                                positive a = string_length((string_address)name), b = string_length((string_address)other);
                                b32 order = memory_compare_ascii_case((address_any)name, (address_any)other, a < b ? a : b);

                                if (order > 0 || (!order && a >= b))
                                        break;
                                launcher.apps[place] = launcher.apps[place - 1], place--;
                        }
                        launcher.apps[place] = (unsigned short)at;
                }
        launcher.app_count = count;
        launcher.generation = ui_apps.generation;
}

static fn launcher_search(void)
{
        positive length = string_length((string_address)launcher.text), found = 0, count = 0;
        // A picker finds the apps alone: no command line to run and no program of the PATH (it starts nothing).
        b32 line = launcher_command_line(launcher.text) || launcher.pick, first = line && !launcher.pick, apps = launcher.pick;
        unsigned int programs[UI_APPS_MOST], ranks[LAUNCHER_ROWS];
        unsigned short best[LAUNCHER_ROWS];

        launcher.list.focus = 0;
        launcher.entries = 0;
        if (launcher.generation != ui_apps.generation)
                launcher_apps_make();
        if (!length)
        {
                launcher.named_length = 0;
                if (!launcher.asked_beside)
                {
                        for (int at = 0; at < launcher.recent_count && count < LAUNCHER_ROWS; at++)
                                launcher.entry[count++] = (struct launcher_entry){LAUNCHER_WINDOW, launcher.recent[at]};
                        launcher.entries = count;
                }
                return;
        }
        for (int at = 0; at < ui_apps.count; at++)
        {
                programs[at] = launcher_offered((positive)at) ? launcher_app_rank(launcher.text, length, at) : 0;
                if (programs[at] == 3)
                        first = false;
        }
        if (first)
                launcher.entry[count++] = (struct launcher_entry){LAUNCHER_COMMAND, 0};
        for (unsigned int rank = 3; rank > 0; rank--)
                for (int at = 0; at < ui_apps.count && count < LAUNCHER_ROWS - (first || apps ? 0u : 1u); at++)
                        if (programs[at] == rank)
                                launcher.entry[count++] = (struct launcher_entry){LAUNCHER_WINDOW, (unsigned short)at};
        if (!line)
        {
                b32 narrowed = launcher.named_length && length > launcher.named_length && launcher.named_names == launcher.count &&
                               !memory_compare(launcher.text, launcher.named_text, launcher.named_length);
                positive total = narrowed ? launcher.named_count : launcher.count, kept = 0;
                positive room = count + 1 < LAUNCHER_ROWS ? LAUNCHER_ROWS - 1 - count : 0;

                for (positive seen = 0; seen < total; seen++)
                {
                        positive at = narrowed ? launcher.named[seen] : seen, place = found;
                        const char address_to name = launcher_name(at);
                        unsigned int rank = launcher_rank(launcher.text, length, name, string_length((string_address)name));

                        if (!rank)
                                continue;
                        launcher.named[kept++] = (unsigned short)at;
                        if (!room || (found == room && !launcher_before(rank, name, ranks[found - 1], launcher_name(best[found - 1]))))
                                continue;
                        if (found < room)
                                found++;
                        else
                                place = found - 1;
                        while (place > 0 && launcher_before(rank, name, ranks[place - 1], launcher_name(best[place - 1])))
                                ranks[place] = ranks[place - 1], best[place] = best[place - 1], place--;
                        ranks[place] = rank, best[place] = (unsigned short)at;
                }
                launcher.named_count = kept;
                launcher.named_length = length;
                launcher.named_names = launcher.count;
                memory_copy(launcher.named_text, launcher.text, length + 1);
        }
        else
                launcher.named_length = 0;
        for (positive at = 0; at < found; at++)
                launcher.entry[count++] = (struct launcher_entry){LAUNCHER_PROGRAM, best[at]};
        if (!first && !apps)
                launcher.entry[count++] = (struct launcher_entry){LAUNCHER_COMMAND, 0};
        launcher.entries = count;
}

static positive launcher_words(const struct launcher_entry address_to entry, string_address address_to words, char address_to full,
                               positive room)
{
        positive count = 0;
        const char address_to mono = launcher.mono ? launcher.mono : launcher.regular;

        if (entry->kind == LAUNCHER_WINDOW)
                return entry->index < ui_apps.count ? ui_apps_words_of(entry->index, words, launcher.regular, launcher.mono, launcher.mono_bold)
                                                    : (words[0] = null, 0);
        words[count++] = (string_address) "/terminal";
        if (mono)
                words[count++] = (string_address) "-f", words[count++] = (string_address)mono;
        if (launcher.mono_bold)
                words[count++] = (string_address) "-b", words[count++] = (string_address)launcher.mono_bold;
        if (entry->kind == LAUNCHER_PROGRAM)
        {
                positive size = string_length((string_address)launcher.dirs[launcher.from[entry->index]]);

                if (size + 1 + LAUNCHER_NAME < room)
                {
                        memory_copy(full, launcher.dirs[launcher.from[entry->index]], size);
                        full[size] = '/';
                        string_copy(full + size + 1, (string_address)launcher_name(entry->index));
                        words[count++] = (string_address) "-x", words[count++] = (string_address)full;
                }
        }
        else if (entry->kind == LAUNCHER_COMMAND)
        {
                words[count++] = (string_address) "-x", words[count++] = (string_address) "/shell";
                words[count++] = (string_address) "-c", words[count++] = (string_address)launcher.text;
        }
        words[count] = null;
        return count;
}

static int launcher_most_height(void)
{
        const struct ui_anchor address_to anchor = &launcher.anchor;
        int most = (int)launcher.own.table->output_height, room;

        if (!anchor->anchored || (anchor->edge != TASKBAR_BOTTOM && anchor->edge != TASKBAR_TOP))
                return most;
        room = anchor->edge == TASKBAR_BOTTOM ? anchor->button[1] - anchor->gap - anchor->screen[1]
                                              : anchor->screen[1] + anchor->screen[3] - anchor->button[1] - anchor->button[3] - anchor->gap;
        return room > 0 && room < most ? room : most;
}

/* ---- PATH scan ---- */

static const char address_to launcher_path_variable(void)
{
        string_address address_to environment = program_environment_list();

        for (positive at = 0; environment && environment[at]; at++)
                if (!memory_compare(environment[at], "PATH=", 5) && environment[at][5])
                        return environment[at] + 5;
        return BOWL_DEFAULT_PATH;
}

static b32 launcher_keep(const char address_to name, positive directory)
{
        positive2 hashed = string_hash_33_length((string_address)name);
        positive slot = hashed.x % LAUNCHER_HASH;

        if (hashed.y >= LAUNCHER_NAME || launcher.count >= LAUNCHER_NAMES)
                return false;
        while (launcher.hashed[slot])
        {
                if (string_equals((string_address)launcher_name(launcher.hashed[slot] - 1u), (string_address)name))
                        return false;
                slot = (slot + 1) % LAUNCHER_HASH;
        }
        string_copy((string_address)launcher.names + launcher.count * LAUNCHER_NAME, (string_address)name);
        launcher.from[launcher.count] = (unsigned char)directory;
        launcher.hashed[slot] = (unsigned short)(++launcher.count);
        return true;
}

static b32 launcher_runnable(b32 directory, const struct linux_dirent64 address_to record)
{
        const char address_to name = (const char address_to)record->d_name;
        b32 opened;

        if (record->d_type == DT_DIR || launcher_command_line(name) || launcher_hidden_name(name) || system_access_at(directory, name, 1) != 0)
                return false;
        if (record->d_type == DT_REG)
                return true;
        opened = (b32)system_open_at(directory, (string_address)name, FILE_READ | O_DIRECTORY | O_CLOEXEC);
        if (opened >= 0)
                system_close(opened);
        return opened < 0;
}

static fn launcher_scan(const char address_to variable)
{
        char address_to buffer = (char address_to)launcher.hashed + LAUNCHER_HASH * sizeof(unsigned short);
        positive size = string_length((string_address)variable);

        launcher.named_length = 0;
        if (size >= LAUNCHER_PATH)
                size = LAUNCHER_PATH - 1;
        memory_copy(launcher.path, variable, size);
        launcher.path[size] = 0;
        for (positive at = 0; launcher.path[at] && launcher.dir_count < LAUNCHER_DIRS;)
        {
                positive stop = at, next;
                b32 seen = false;

                while (launcher.path[stop] && launcher.path[stop] != ':')
                        stop++;
                next = launcher.path[stop] ? stop + 1 : stop;
                launcher.path[stop] = 0;
                for (positive which = 0; which < launcher.dir_count && !seen; which++)
                        seen = string_equals((string_address)launcher.dirs[which], (string_address)launcher.path + at);
                if (stop > at && launcher.path[at] == '/' && !seen)
                        launcher.dirs[launcher.dir_count++] = launcher.path + at;
                at = next;
        }
        for (positive which = 0; which < launcher.dir_count; which++)
        {
                bipolar handle = system_open_at(AT_FDCWD, (string_address)launcher.dirs[which], FILE_READ | O_DIRECTORY | O_CLOEXEC), got;

                if (handle < 0)
                        continue;
                while ((got = system_read_directory(handle, buffer, LAUNCHER_DIRENTS)) > 0)
                        for (positive at = 0; at < (positive)got;)
                        {
                                const struct linux_dirent64 address_to record = (const struct linux_dirent64 address_to)(buffer + at);

                                if (record->d_reclen <= LAUNCHER_DIRENT_HEADER || at + record->d_reclen > (positive)got)
                                        break;
                                at += record->d_reclen;
                                if (record->d_name[0] != '.' && launcher_runnable((b32)handle, record))
                                        launcher_keep((const char address_to)record->d_name, which);
                        }
                system_close(handle);
        }
}

/* ---- starting and recent apps ---- */

static fn launcher_recent_path(char address_to path, positive room)
{
        string_copy_bounded((p8 address_to)path, (string_address)compositor_settings_root, room);
        string_append_bounded((p8 address_to)path, (string_address)LAUNCHER_RECENT_PATH, room);
}

static fn launcher_recent_read(void)
{
        char path[160];

        launcher_recent_path(path, sizeof(path));
        launcher.recent_count = ui_app_list_read(path, launcher.recent, LAUNCHER_RECENT, launcher_offered);
}

/*
        What a pick says (launcher -p): the entry's id, a tab, and its command as sh runs it (ui_apps_command), and a line's end. Its length
        in into, 0 for no entry; room of LAUNCHER_PICK_MOST holds any (an id is WINDOWS_APP_ID at most).
*/
#define LAUNCHER_PICK_MOST (WINDOWS_APP_ID + UI_APP_COMMAND_MOST + 2)
static positive launcher_pick_line(int which, char address_to into, positive room)
{
        positive length, made;

        if (which < 0 || which >= ui_apps.count || (length = string_length((string_address)ui_apps.app[which].id)) + 2 >= room)
                return 0;
        memory_copy(into, ui_apps.app[which].id, length), into[length++] = '\t';
        if (!(made = ui_apps_command(which, into + length, room - length - 1)))
                return into[0] = 0, 0;
        length += made, into[length++] = '\n', into[length] = 0;
        return length;
}

// A choice of the picker said on standard output, and the picker done (its caller ends it).
static b32 launcher_pick(int which)
{
        char line[LAUNCHER_PICK_MOST];
        positive length = launcher_pick_line(which, line, sizeof(line));

        if (!length || system_write_all(1, line, length) != length)
                return false;
        return launcher.picked = true;
}

static b32 launcher_launch(struct launcher_entry entry, b32 mark_starting)
{
        string_address words[LAUNCHER_WORDS];
        char full[LAUNCHER_FULL], path[160];
        positive count;

        if (entry.kind == LAUNCHER_WINDOW && entry.index >= ui_apps.count)
                return false;
        if (launcher.pick)
                return entry.kind == LAUNCHER_WINDOW && launcher_pick(entry.index);
        count = launcher_words(&entry, words, full, sizeof(full));
        if (!count || !ui_start_apart(words, count))
                return false;
        if (entry.kind == LAUNCHER_WINDOW)
        {
                if (mark_starting)
                        ui_app_starting(ui_apps.app[entry.index].id);
                launcher_recent_path(path, sizeof(path));
                ui_app_list_push(path, entry.index, LAUNCHER_RECENT);
        }
        return true;
}

static int launcher_app_focused(void)
{
        const struct ui_list address_to l = launcher.in_recents ? &launcher.recents : &launcher.grid;
        int count = launcher.in_recents ? launcher.recent_count : launcher.app_count;

        return l->focus < 0 || l->focus >= count ? -1 : (launcher.in_recents ? launcher.recent[l->focus] : launcher.apps[l->focus]);
}

/* ---- power actions and view ---- */

static positive launcher_power_words(int which, string_address address_to words)
{
        words[0] = (string_address) "/shell", words[1] = (string_address) "-c";
        words[2] = (string_address)launcher_powers[which].command, words[3] = null;
        return 3;
}

static b32 launcher_power_offered(int which)
{
        return which >= 0 && which < LAUNCHER_POWERS && (which < LAUNCHER_ALWAYS || (launcher.session >> (which - LAUNCHER_ALWAYS) & 1));
}

static b32 launcher_sleeps(const char address_to text, positive length)
{
        for (positive at = 0; at < length;)
        {
                positive stop = at;

                while (stop < length && text[stop] != ' ' && text[stop] != '\n' && text[stop])
                        stop++;
                if (stop - at == 3 && !memory_compare(text + at, "mem", 3))
                        return true;
                at = stop + 1;
        }
        return false;
}

static fn launcher_session_read(const char address_to state_path)
{
        char text[128];
        bipolar got = file_slurp_once_at(AT_FDCWD, (string_address)state_path, (p8 address_to)text, sizeof(text));

        launcher.session = got > 0 && launcher_sleeps(text, (positive)got) ? LAUNCHER_SLEEP : 0;
}

static b32 launcher_power_act(int which)
{
        string_address words[4];

        if (!launcher_power_offered(which))
                return true;
        launcher.power_runs++;
        return !ui_start_apart(words, launcher_power_words(which, words));
}

static struct ui_text address_to launcher_field(void);
static fn launcher_damaged(int top, int bottom);
static fn launcher_grid_describe(int id, struct ui_row address_to row);
static fn launcher_recent_describe(int id, struct ui_row address_to row);
static int launcher_per(void);

static fn launcher_tiles_ready(void)
{
        struct ui_list address_to lists[2] = {&launcher.grid, &launcher.recents};

        if (!launcher.grid.describe)
        {
                for (int k = 0; k < 2; k++)
                {
                        lists[k]->virtual = lists[k]->centre = true;
                        lists[k]->tiles = LAUNCHER_TILE;
                        lists[k]->damaged = launcher_damaged;
                        lists[k]->hover = lists[k]->down = -1;
                }
                launcher.grid.describe = launcher_grid_describe, launcher.recents.describe = launcher_recent_describe;
                launcher.recents.focus = -1;
        }
        launcher.grid.total = launcher.app_count;
        launcher.recents.total = launcher.recent_count < launcher_per() ? launcher.recent_count : launcher_per();
        launcher.grid.lit = !launcher.in_recents && !launcher.text[0];
        launcher.recents.lit = launcher.in_recents && !launcher.text[0];
}

static int launcher_per(void)
{
        int scale = launcher.ui.scale > 0 ? launcher.ui.scale : 100, per;

        if (launcher.grid.w > 0)
                return ui_list_per(&launcher.ui, &launcher.grid);
        per = (launcher.width * 100 / scale - 2 * UI_PAD) / LAUNCHER_TILE;
        return per < 1 ? 1 : per;
}

static fn launcher_view(void)
{
        struct ui_control address_to search = &launcher.search;

        if (!search->kind)
                search->kind = UI_INPUT, search->variant = UI_FIELD_BARE,
                search->items[0] = WORDS(launcher_search_hint), search->edit = launcher_field();
        search->focused = launcher.focused && !launcher.view.group.keys;
        if (launcher.asked_beside)
                launcher_tiles_ready();
        ui_control(search, .class = "head");
        ui_col(.class = "inset")
        {
                long rows = ui_value(&launcher.entries);
                b32 typed = launcher.text[0] != 0;

                if (launcher.asked_beside)
                {
                        int per = launcher_per(), lines = (launcher.app_count + per - 1) / per;

                        lines = lines > LAUNCHER_GRID_LINES ? LAUNCHER_GRID_LINES : lines;
                        ui_rows(&launcher.grid, .class = "list", .h = lines * UI_TILE_H, .hidden = typed || !lines);
                        if (!typed && launcher.recent_count)
                                ui_row(.class = "middle")
                                        ui_text(WORDS(launcher_recent), .class = "caption");
                        ui_rows(&launcher.recents, .class = "list", .h = UI_TILE_H, .hidden = typed || !launcher.recent_count);
                }
                ui_rows(&launcher.list, .class = "list", .h = (int)rows * UI_ROW, .hidden = !rows);
                if (launcher.asked_beside && !launcher.pick)
                        ui_row(.class = "foot.middle")
                                for (int at = 0; at < LAUNCHER_POWERS; at++)
                                        if (launcher_power_offered(at))
                                                ui_button(language_text(launcher_powers[at].label), .class = "action.outlined",
                                                          .says = language_text(launcher_powers[at].says), .on = launcher_power_act, .arg = at,
                                                          .grow = ui_width() < LAUNCHER_NARROW);
        }
}

static int launcher_build(int limit)
{
        ui_view_begin(&launcher.view, &launcher.ui, launcher.width, limit);
        launcher_view();
        return ui_view_end(&launcher.view);
}

/* ---- field, keys, and pointer ---- */

static fn launcher_changed(void)
{
        int was = launcher.height;

        launcher_search();
        launcher.height = launcher_build(launcher_most_height());
        launcher.moved |= launcher.anchor.anchored && launcher.buffers.shown >= 0 && launcher.anchor.edge == TASKBAR_BOTTOM && launcher.height != was;
        launcher.dirty = true;
}

static struct ui_text address_to launcher_field(void)
{
        if (!launcher.field.doc.selections && ui_field_open(&launcher.field, &launcher.own, ""))
                launcher.field.most = LAUNCHER_TEXT - 1, launcher.field.keeps_vertical = true, launcher.field.centred = true;
        return &launcher.field;
}

static fn launcher_take(void)
{
        char held[LAUNCHER_TEXT];
        positive length = ui_text_string(launcher_field(), (p8 address_to)held, sizeof(held)), start = 0;

        while (start < length && held[start] == ' ')
                start++;
        while (length > start && held[length - 1] == ' ')
                length--;
        held[length] = 0;
        if (string_equals((string_address)held + start, (string_address)launcher.text))
                return;
        string_copy_bounded((string_address)launcher.text, (string_address)held + start, sizeof(launcher.text));
        launcher_changed();
}

static b32 launcher_asked(p32 asks)
{
        if (asks & UI_TEXT_SUBMIT)
        {
                if (launcher.text[0] || !launcher.asked_beside)
                {
                        int row = launcher.list.focus;
                        return !(row >= 0 && (positive)row < launcher.entries && launcher_launch(launcher.entry[row], false));
                }
                {
                        int which = launcher_app_focused();
                        return !(which >= 0 && launcher_launch((struct launcher_entry){LAUNCHER_WINDOW, (unsigned short)which}, true));
                }
        }
        if (asks & UI_TEXT_CANCEL)
                return false;
        if (asks & UI_TEXT_CHANGED)
                launcher_take();
        if (asks)
                launcher.dirty = true;
        return true;
}

static b32 launcher_app_menu(int which, int x, int y, b32 touch)
{
        b32 pinned, pinnable, uninstallable;

        if (which < 0 || which >= ui_apps.count)
                return false;
        pinned = ui_apps_pinned(compositor_settings_root, which);
        pinnable = pinned || ui_apps_pinnable(which);
        uninstallable = ui_apps_uninstallable(which);
        if (!pinnable && !uninstallable)
                return false;
        launcher.menu.menu.count = 0, launcher.menu.field = null;
        {
                const struct ui_menu_spec specs[] = {
                    {pinned ? WORDS(taskbar_tab_unpin) : WORDS(taskbar_tab_pin), null, 0, pinned ? LAUNCHER_MENU_UNPIN : LAUNCHER_MENU_PIN, !pinnable},
                    {null, null, UI_MENU_SEPARATOR, 0, false},
                    {WORDS(taskbar_tab_uninstall), null, 0, LAUNCHER_MENU_UNINSTALL, !uninstallable},
                };
                ui_popmenu_build(&launcher.menu.menu, specs, (int)(sizeof(specs) / sizeof(specs[0])));
        }
        launcher.menu_app = which;
        return ui_popmenu_open(&launcher.menu, &launcher.own, &launcher.ui, x, y, 0, 0, WINDOW_POPUP_POINT, touch);
}

static b32 launcher_grid_key(const struct window_event address_to event)
{
        struct ui_list address_to l = launcher.in_recents ? &launcher.recents : &launcher.grid;
        int key = (int)event->a, per = launcher_per(), focus;
        b32 moved = false;

        if (!launcher.asked_beside || launcher.text[0] || !launcher.app_count)
                return false;
        focus = l->focus < 0 ? 0 : l->focus;
        if ((key == 68 && (event->flags & WINDOW_KEY_SHIFT)) || key == 127)
        {
                int lper = ui_list_per(&launcher.ui, l), wide = ui_dev(&launcher.ui, l->tiles), high = ui_list_unit(&launcher.ui, l);

                launcher_app_menu(launcher_app_focused(), ui_list_tile_x(&launcher.ui, l, focus) + wide / 2,
                                  l->y + focus / lper * high - l->scroll.scroll + high / 2, false);
                return true;
        }
        if (key != UI_KEY_UP && key != UI_KEY_DOWN && key != UI_KEY_LEFT && key != UI_KEY_RIGHT)
                return false;
        if (!launcher.in_recents && key == UI_KEY_DOWN && launcher.recent_count && focus / per == (launcher.app_count - 1) / per)
        {
                launcher.in_recents = true;
                launcher.recents.focus = ui_list_tile_near(&launcher.ui, &launcher.recents, 0, ui_list_tile_middle(&launcher.ui, &launcher.grid, focus));
        }
        else if (launcher.in_recents && key == UI_KEY_UP)
        {
                int to = ui_list_tile_near(&launcher.ui, &launcher.grid, (launcher.app_count - 1) / per,
                                           ui_list_tile_middle(&launcher.ui, &launcher.recents, focus));

                launcher.in_recents = false;
                launcher.grid.focus = to >= 0 ? to : launcher.app_count - 1;
                ui_list_reveal(&launcher.ui, &launcher.grid);
        }
        else
        {
                if (l->focus < 0)
                        l->focus = 0;
                ui_list_key(&launcher.ui, l, key, &moved);
        }
        launcher.dirty = true;
        return true;
}

// Routes event to view buttons: -1 quit, 1 taken, 0 pass through.
static int launcher_view_take(const struct window_event address_to event)
{
        p32 asks = ui_view_event(&launcher.ui, &launcher.view, event);

        if (asks & UI_VIEW_DRAW)
                launcher.dirty = true;
        return (asks & UI_VIEW_QUIT) ? -1 : (asks & UI_VIEW_TAKEN) ? 1 : 0;
}

static b32 launcher_key(const struct window_event address_to event)
{
        int taken = launcher_view_take(event);
        p32 asks;

        if (taken)
                return taken > 0;
        if (!(event->flags & WINDOW_KEY_DOWN) || launcher_grid_key(event))
                return true;
        if ((asks = ui_field_send(&launcher.ui, launcher_field(), &launcher.menu, event)))
                return launcher_asked(asks);
        if (event->a == UI_KEY_UP || event->a == UI_KEY_DOWN ||
            ((event->flags & WINDOW_KEY_CONTROL) && (event->a == UI_KEY_P || event->a == UI_KEY_N)))
        {
                b32 moved;
                ui_list_key(&launcher.ui, &launcher.list, event->a == UI_KEY_P ? UI_KEY_UP : event->a == UI_KEY_N ? UI_KEY_DOWN : event->a, &moved);
        }
        else if (event->a == UI_KEY_TAB && launcher.entries && launcher.list.focus >= 0 && (positive)launcher.list.focus < launcher.entries)
        {
                const struct launcher_entry address_to entry = &launcher.entry[launcher.list.focus];
                const char address_to name = entry->kind == LAUNCHER_PROGRAM ? launcher_name(entry->index)
                                             : entry->kind == LAUNCHER_WINDOW ? ui_apps_name(entry->index)
                                                                              : null;
                if (name)
                        ui_text_set(launcher_field(), (const p8 address_to)name, string_length((string_address)name)), launcher_take();
        }
        return true;
}

static fn launcher_chose(int id)
{
        if (id == LAUNCHER_MENU_PIN || id == LAUNCHER_MENU_UNPIN)
                ui_apps_pin(compositor_settings_root, launcher.menu_app, id == LAUNCHER_MENU_PIN), launcher.dirty = true;
        else if (id == LAUNCHER_MENU_UNINSTALL)
        {
                int target = launcher.menu_app;

                launcher.menu_app = -1;
                if (ui_apps_uninstall(compositor_settings_root, target, launcher.mono ? launcher.mono : launcher.regular, launcher.mono_bold))
                {
                        launcher_apps_make();
                        launcher_recent_read();
                        if (launcher.grid.focus >= launcher.app_count)
                                launcher.grid.focus = launcher.app_count - 1;
                        if (launcher.recents.focus >= launcher.recent_count)
                                launcher.recents.focus = launcher.recent_count - 1;
                        if (!launcher.recent_count)
                                launcher.in_recents = false;
                        launcher_changed();
                }
        }
        else
                launcher_asked(ui_text_menu_do(&launcher.ui, launcher.menu.field, id));
}

static fn launcher_describe(int id, struct ui_row address_to row);

static fn launcher_damaged(int top, int bottom)
{
        (void)top, (void)bottom;
        launcher.dirty = true;
}

static fn launcher_app_safe_name(int which, char address_to into, positive room)
{
        const struct ui_app_entry address_to app = &ui_apps.app[which];
        const char address_to name = ui_apps_name(which);

        if (app->kit)
                string_copy_bounded((string_address)into, (string_address)name, room);
        else
                ui_label_foreign(into, room, (const p8 address_to)name, string_length((string_address)name));
}

static const unsigned int address_to launcher_grid_picture(int id, int side)
{
        return id >= 0 && id < launcher.app_count ? ui_apps_picture_kept(launcher.apps[id], side) : null;
}

static const unsigned int address_to launcher_recent_picture(int id, int side)
{
        return id >= 0 && id < launcher.recent_count ? ui_apps_picture_kept(launcher.recent[id], side) : null;
}

static fn launcher_tile_describe(const unsigned short address_to ids, int count, int id, struct ui_row address_to row,
                                 const unsigned int address_to (*pic)(int, int))
{
        static char names[1024][UI_APP_NAME + 8];
        static unsigned int next;
        char address_to label;

        if (id < 0 || id >= count)
                return;
        label = names[next++ % 1024];
        launcher_app_safe_name(ids[id], label, UI_APP_NAME + 8);
        row->label = label;
        ui_apps_symbol(ids[id], row);
        row->tile_picture = pic;
}

static fn launcher_grid_describe(int id, struct ui_row address_to row)
{
        launcher_tile_describe(launcher.apps, launcher.app_count, id, row, launcher_grid_picture);
}

static fn launcher_recent_describe(int id, struct ui_row address_to row)
{
        launcher_tile_describe(launcher.recent, launcher.recent_count, id, row, launcher_recent_picture);
}

static fn launcher_layout(void)
{
        if (!launcher.list.ids)
                launcher.list.ids = LAUNCHER_ROWS, launcher.list.describe = launcher_describe,
                launcher.list.damaged = launcher_damaged, launcher.list.hover = launcher.list.down = -1;
        ui_list_begin(&launcher.list);
        for (int at = 0; at < (int)launcher.entries; at++)
                if (!ui_list_add(&launcher.list, at))
                {
                        ui_list_begin(&launcher.list);
                        break;
                }
        ui_list_end(&launcher.list);
        launcher.list.lit = true;
        launcher_build(launcher.height);
}

static int launcher_list_pointer(struct ui_list address_to l, const struct window_event address_to event, int x, int y, b32 focus_group,
                                 b32 update_recents, int address_to right_hit)
{
        b32 moved = event->type == WINDOW_EVENT_MOTION && (event->a != l->pointer_x || event->b != l->pointer_y);
        int hit = ui_list_hit(&launcher.ui, l, x, y), clicked;

        *right_hit = -1;
        if (event->type == WINDOW_EVENT_BUTTON && event->a == UI_POINTER_RIGHT)
        {
                if (hit >= 0 && (event->flags & WINDOW_KEY_DOWN))
                        *right_hit = hit;
                return -1;
        }
        clicked = ui_list_click(&launcher.ui, l, event, x, y);
        if (l->press.down && l->press.dragged)
                launcher.dirty = true;
        else if (hit >= 0 && moved && (hit != l->focus || (update_recents && launcher.in_recents != focus_group)))
        {
                l->focus = hit;
                if (update_recents)
                        launcher.in_recents = focus_group;
                launcher.dirty = true;
        }
        if (event->type == WINDOW_EVENT_BUTTON)
                launcher.dirty = true;
        return clicked;
}

static b32 launcher_event(const struct window_event address_to event)
{
        int right_hit, clicked, taken;
        p32 asks;

        launcher_layout();
        if (event->type == WINDOW_EVENT_KEY)
                return launcher_key(event);
        if ((taken = launcher_view_take(event)))
                return taken > 0;
        if ((asks = ui_field_send(&launcher.ui, launcher_field(), &launcher.menu, event)) &&
            !(event->type == WINDOW_EVENT_BUTTON && (asks == UI_TEXT_COMMIT || asks == UI_TEXT_CANCEL)))
                return launcher_asked(asks);
        if (launcher.asked_beside && !launcher.text[0])
        {
                struct ui_list address_to lists[2] = {&launcher.grid, &launcher.recents};

                for (int which = 0; which < 2; which++)
                {
                        clicked = launcher_list_pointer(lists[which], event, ui_kit.pointer_x, ui_kit.pointer_y, which != 0, true, &right_hit);
                        if (right_hit >= 0)
                                launcher_app_menu(which ? launcher.recent[right_hit] : launcher.apps[right_hit], ui_kit.pointer_x, ui_kit.pointer_y, ui_kit_touch);
                        if (clicked >= 0)
                                return !launcher_launch((struct launcher_entry){LAUNCHER_WINDOW, which ? launcher.recent[clicked] : launcher.apps[clicked]}, true);
                }
                return true;
        }
        if (event->type != WINDOW_EVENT_MOTION && event->type != WINDOW_EVENT_BUTTON)
                return true;
        clicked = launcher_list_pointer(&launcher.list, event, ui_kit.pointer_x, ui_kit.pointer_y, false, false, &right_hit);
        if (right_hit >= 0 && (positive)right_hit < launcher.entries && launcher.entry[right_hit].kind == LAUNCHER_WINDOW)
                launcher_app_menu(launcher.entry[right_hit].index, ui_kit.pointer_x, ui_kit.pointer_y, ui_kit_touch);
        return !(clicked >= 0 && (positive)clicked < launcher.entries && launcher_launch(launcher.entry[clicked], false));
}

/* ---- drawing ---- */

static struct ui_app launcher_app;

static fn launcher_label(const struct launcher_entry address_to entry, char address_to into, positive room,
                         const char address_to address_to note)
{
        *note = "", into[0] = 0;
        if (entry->kind == LAUNCHER_WINDOW && entry->index < ui_apps.count)
        {
                const struct ui_app_entry address_to app = &ui_apps.app[entry->index];

                launcher_app_safe_name(entry->index, into, room);
                *note = app->kit ? language_of("desktop")
                                 : ui_apps_local && ui_apps_local[0] && string_equals((string_address)app->root, (string_address)ui_apps_local)
                                       ? (app->profile[0] ? app->profile : "local")
                                       : ui_apps_base(app->root);
        }
        else if (entry->kind == LAUNCHER_PROGRAM)
        {
                const char address_to name = launcher_name(entry->index);

                ui_label_foreign(into, room, (const p8 address_to)name, string_length((string_address)name));
                *note = launcher.dirs[launcher.from[entry->index]];
        }
        else
                language_format(into, room, WORDS(launcher_run), launcher.text), *note = "shell";
}

static const unsigned int address_to launcher_list_picture(int id, int side)
{
        return id >= 0 && (positive)id < launcher.entries && launcher.entry[id].kind == LAUNCHER_WINDOW &&
                       launcher.entry[id].index < ui_apps.count
                   ? ui_apps_picture_kept(launcher.entry[id].index, side)
                   : null;
}

static fn launcher_describe(int id, struct ui_row address_to row)
{
        static char label[LAUNCHER_TEXT + 8];
        const struct launcher_entry address_to entry;

        if (id < 0 || id >= (int)launcher.entries)
                return;
        entry = &launcher.entry[id];
        launcher_label(entry, label, sizeof(label), &row->note);
        row->label = label;
        if (entry->kind == LAUNCHER_WINDOW && entry->index < ui_apps.count)
                ui_apps_symbol(entry->index, row), row->tile_picture = launcher_list_picture;
        else if (entry->kind == LAUNCHER_PROGRAM || entry->kind == LAUNCHER_COMMAND)
                row->icon = UI_ICON_TERMINAL + 1;
}

static fn launcher_draw(unsigned int address_to pixels)
{
        struct ui address_to ui = &launcher.ui;

        ui_begin(ui, pixels, launcher.own.table->stride, launcher.width, launcher.height);
        launcher_layout();
        ui_view_draw(ui, &launcher.view, launcher.focused || !launcher_app.had_keys);
        ui_text_overlay(ui, launcher_field());
}

static fn launcher_paint(unsigned int address_to pixels)
{
        launcher_draw(pixels);
        if (launcher.moved)
        {
                ui_anchor_set(&launcher.anchor, launcher.own.table, launcher.own.row, launcher.width, launcher.height);
                __atomic_add_fetch(&launcher.own.table->moved[launcher.own.row], 1u, __ATOMIC_RELEASE);
                launcher.moved = false;
        }
}

/* ---- program lifecycle ---- */

static fn launcher_language(void)
{
        launcher.search.kind = 0;
        if (launcher.width > 0 && launcher.height > 0)
                launcher_layout();
}

// The launcher sits this far off the band, beside its button: half the button's thickness across the band (its height on a bottom or top band, its
// width on a side one), so the gap scales with the button at every scale and on every edge.
static int launcher_anchor_gap(const struct ui_anchor address_to anchor)
{
        return (anchor->edge == TASKBAR_BOTTOM || anchor->edge == TASKBAR_TOP ? anchor->button[3] : anchor->button[2]) / 2;
}

// The launcher as it opens: anchored to the button pressed (or under the screen's middle), the registry read again when it changed, its
// apps and its recents made again.
static fn launcher_fresh(void)
{
        struct windows address_to table = launcher.own.table;
        unsigned int row = launcher.own.row;

        ui_anchor_take(&launcher.anchor, table, launcher_app.beside);
        if (launcher.anchor.anchored)
                launcher.anchor.gap = launcher_anchor_gap(&launcher.anchor);
        ui_apps_fresh(ui_kit_time());
        launcher_apps_make();
        launcher_recent_read();
        launcher_changed();
        if (launcher.anchor.anchored)
                ui_anchor_set(&launcher.anchor, table, row, launcher.width, launcher.height);
        else
        {
                struct window_place place;
                int x, y, width, height;

                memory_zero(&place, sizeof(place));
                windows_output(table, 0, &x, &y, &width, &height);
                place.how = WINDOW_PLACE_AT | WINDOW_PLACE_SCREEN | WINDOW_PLACE_SIZE;
                place.x = (width - launcher.width) / 2;
                place.y = height / 3 - ui_px(&launcher.ui, UI_PAD) - ui_px(&launcher.ui, UI_TARGET) / 2;
                place.width = (unsigned int)launcher.width, place.height = (unsigned int)launcher.height;
                place.screen = 0;
                windows_place_set(table, row, &place);
        }
}

// Another launcher up: a program named launcher with a row that has shown a picture.
static b32 launcher_other(void)
{
        struct windows address_to table = launcher.own.table;

        for (unsigned int row = 0; row < windows_rows(table); row++)
        {
                unsigned int pid = __atomic_load_n(&table->pid[row], __ATOMIC_ACQUIRE);

                if (row != launcher.own.row && pid && __atomic_load_n(&table->frame[row], __ATOMIC_ACQUIRE) && ui_is_program(pid, (string_address) "launcher"))
                        return true;
        }
        return false;
}

static int launcher_open(void)
{
        positive names = (positive)LAUNCHER_NAMES * LAUNCHER_NAME, from = LAUNCHER_NAMES,
                 hashed = (positive)LAUNCHER_HASH * sizeof(unsigned short);
        char address_to mapped;

        // A start put away (-k) with a launcher up already leaves it be; another start shows the one up (or puts it away with -x). A picker
        // is its own, whatever is up.
        if (!launcher.pick && (launcher.kept_start ? launcher_other() : ui_lift_other(&launcher.own, (string_address) "launcher", launcher.toggle)))
                return 0;
        if (!(mapped = ui_app_map(&launcher_app, names + from + hashed + LAUNCHER_DIRENTS)))
                return 1;
        launcher.names = mapped;
        launcher.from = (unsigned char address_to)(mapped + names);
        launcher.hashed = (unsigned short address_to)(mapped + names + from);
        ui_app_size(&launcher_app, (unsigned int)ui_px(&launcher.ui, LAUNCHER_WIDTH), 0);
        // Put away before its first picture: the compositor takes the row minimized, and its button shows it (compositor_launcher_kept).
        if (launcher.kept_start)
                launcher.own.table->state[launcher.own.row] = WINDOW_MINIMIZED, launcher_app.put_away = true;
        launcher_fresh();
        ui_app_frame(&launcher_app);
        if (!launcher.pick)
                launcher_scan(launcher_path_variable());
        return -1;
}

/* The launcher kept up (WINDOW_RESIDENT): put away where it would end, and shown again by its button with no program started. Put away,
   its search is emptied and its lists are at their starts, so the picture made then is the one the next show starts with; once that is
   drawn, the pictures' copies at their sizes are let go (launcher_tick). Shown again, it is anchored to the button pressed and its
   registry and recents are read again, as an open does. */
static fn launcher_away(void)
{
        ui_text_set(launcher_field(), (const p8 address_to) "", 0);
        launcher_take();
        launcher.in_recents = false, launcher.menu_app = -1;
        launcher.list.focus = launcher.grid.focus = launcher.recents.focus = -1;
        launcher.list.hover = launcher.grid.hover = launcher.recents.hover = -1;
        launcher.list.scroll.scroll = launcher.grid.scroll.scroll = launcher.recents.scroll.scroll = 0;
        launcher_changed();
}

static fn launcher_back(void)
{
        launcher_fresh();
        launcher.moved = true; // the compositor moves the window to its anchor with the next picture (launcher_paint)
}

/* The apps' pictures are made off the draw (ui_apps_picture_kept): a tile takes its picture from the kept file (mapped with the
   registry at the launcher's open, before its first frame), else is its symbol or its lettered tile until the worker has made it. A
   picture the worker has finished is taken here and the launcher drawn again at once; then the worker is given the next job
   (ui_apps_picture_tend: an app not tried, a kept picture to check, the kept file to write), and while one is under way the loop
   looks again a frame on. */
static long launcher_pictures(long timeout)
{
        b32 taken;

        timeout = ui_apps_pictures_tick(timeout, true, &taken);
        launcher.dirty |= taken;
        return timeout;
}

static long launcher_tick(long timeout, long address_to other)
{
        b32 redraw = false;
        int top, bottom;

        (void)other;
        timeout = ui_text_timeout(&launcher.ui, launcher_field(), timeout, &redraw);
        if (redraw || ui_view_moved(&launcher.view, &top, &bottom))
                launcher.dirty = true;
        // Put away and its picture made: the pictures' copies at their sizes let go (the next draw scales them again).
        if (launcher_app.put_away && !launcher.dirty)
                ui_apps_pictures_trim();
        return launcher_pictures(timeout);
}

static struct ui_app launcher_app = {
    .own = &launcher.own,
    .ui = &launcher.ui,
    .missing = (string_address) "launcher: no window: the compositor's desktop is not up\n",
    .title = (string_address) "Launcher",
    .glass = WINDOW_GLASS | WINDOW_NO_CLOSE | WINDOW_DISMISS | WINDOW_RESIDENT,
    .no_bold = true,
    .nofont = (string_address) "launcher: no font to draw with (-f FONT.ttf), pid ",
    .dirty = &launcher.dirty,
    .focused = &launcher.focused,
    .ground = &launcher.ground,
    .open = launcher_open,
    .language = launcher_language,
    .event = launcher_event,
    .dismiss = true,
    .resident = true,
    .away = launcher_away,
    .back = launcher_back,
    .tick = launcher_tick,
    .popup = &launcher.menu,
    .chose = launcher_chose,
    .width = &launcher.width,
    .height = &launcher.height,
    .shown = &launcher.buffers.shown,
    .draw = launcher_paint,
    .view = &launcher.view,
};

static b32 launcher_main()
{
        memory_zero(&launcher, sizeof(launcher));
        launcher.menu_app = -1, launcher.generation = ~0u;
        launcher_app.font = null, launcher_app.beside = false;
        launcher_session_read(LAUNCHER_POWER_STATE);
        ui_buffers_start(&launcher.buffers);
        for (positive at = 1, took; at < (positive)program_argument_count(); at += took)
        {
                string_address option = program_argument(at), value;

                if ((took = ui_app_option(&launcher_app, at)))
                        continue;
                took = 1;
                if (string_equals(option, (string_address) "-k"))
                        launcher.kept_start = true;
                else if (string_equals(option, (string_address) "-x"))
                        launcher.toggle = true;
                else if (string_equals(option, (string_address) "-p"))
                        launcher.pick = true;
                else if ((value = ui_argument(at, (string_address) "-m")))
                        launcher.mono = (const char address_to)value, took = 2;
                else if ((value = ui_argument(at, (string_address) "-M")))
                        launcher.mono_bold = (const char address_to)value, took = 2;
        }
        // A picker looks as the start menu does (its apps' tiles and its recents) wherever it opens, and is no resident: it ends.
        launcher.asked_beside = launcher_app.beside || launcher.pick;
        if (launcher.pick)
                launcher_app.glass &= ~(unsigned int)WINDOW_RESIDENT, launcher_app.resident = false, launcher.kept_start = false;
        if (!launcher_app.font)
                launcher_app.font = ui_faces()->text[0] ? (string_address)ui_faces()->text : (string_address)UI_FONT_REGULAR;
        if (!launcher.mono && ui_faces()->face[0])
                launcher.mono = (const char address_to)ui_faces()->face;
        if (!launcher.mono_bold && ui_faces()->bold[0])
                launcher.mono_bold = (const char address_to)ui_faces()->bold;
        launcher.regular = (const char address_to)launcher_app.font;
        {
                int status = ui_app_run(&launcher_app);
                return launcher.pick ? !launcher.picked : status;
        }
}

#endif // MOONWATER_CANVAS_LAUNCHER
