/*
        The launcher, as a window on the compositor in userspace.

        A search bar in the middle of the screen: one field with the keys in it
        at once. With nothing typed, under it are the apps as a grid of their
        pictures and names (the kit's registry, ui_apps: the desktop's own
        programs first, then every program a bowl installed that says it is one,
        by name), and under that Recent, the apps started last from here. Once
        something is typed it is a short list of what the text names: the apps
        it names (by name, and by the words their entries give), then the
        programs found on PATH whose names start with or contain what was typed,
        the nearest names first, and last a row that runs the text itself as a
        shell command, so the bar is also a shell prompt.

        An app is started as its own window, never in a terminal: a kit program
        with its face, a bowl's through its launcher of /bowls/bin with the words
        its entry gives, detached and asked for (the compositor gives its first
        window the keys, window.c's asked record). Only an app whose entry says
        Terminal=true, a program of PATH and a typed command open a terminal.
        The arrows go across the grid and down into Recent, Enter starts the one
        the keys are on, and a right press (or Shift-F10, or the menu key) on an
        app opens its menu: Pin to taskbar, or Unpin, which keeps it in the
        panel's pins (ui_apps_pin) by its id.

        The field is the kit's (the text engine's field): a caret placed by the
        hand, a drag, a double and a triple click, Shift and the arrows, Control
        and the arrows by words, Home and End, Control-A, undo and redo, the
        clipboard and the middle button's paste, its context menu, and a blank
        typed anywhere, the first character too (the list is for the text with
        its blanks at either end left off). What the field leaves is the bar's:
        Up and Down (or Control-P and Control-N) move in the list, Tab takes the
        highlighted name into the field, and Enter runs the highlighted row:

            an app               is started as a window of its own;
            a program of PATH    runs in a new terminal window, its words
                                 never seen by a shell;
            the command          is run by the machine's shell (shell -c) in a
                                 new terminal window.

        A terminal that was started for a program shows what it wrote and says
        how it ended, then stays until a key is pressed (ui/terminal.c, -x). A text
        that is a command line (a blank, a pipe, a redirect, a quote, anything a
        shell reads as more than one plain word) or that no program is named
        by is what the highlighted row starts as, the command row, so Enter on it
        is the shell's; a plain word that names programs highlights the nearest.

        Under the list is a row of two buttons, Power off and Restart, declared in the bar's view (launcher_view): the kit
        does the pointer, the keys and the press again to confirm, so a button asks for a second press (and a held key's repeats are
        no press) before the machine's own poweroff or reboot is started, quietly, by the shell as a typed command is, and
        the bar goes. Shift-Tab gives the buttons the keys; the kit says the rest.

        PATH is read once, when the bar opens (at most LAUNCHER_NAMES names, the
        first directory having a name keeping it), and every key only filters
        what was read: nothing is started for a key. Nothing here needs more than
        the rights the person has: a program is run as the launcher is.

        Escape, a press anywhere but on it (the desktop, a tab, another window:
        the compositor asks it to go, WINDOW_DISMISS) and losing the keys close
        it, as the same chord that opened it does (-x). It has no close button
        (WINDOW_NO_CLOSE). It is glass
        (window.c, WINDOW_GLASS), drawn with the system's pieces (ui.c) and
        nothing round them: the field is the kit's field, and the rows the
        kit's list, each a label and the quiet word of where it is from, the
        highlighted one the row the keys are on, with the kit's hover, press
        and ripple. It is the one window of its program: a second start finds
        the first by the program's name and lifts it.

            launcher [-x] [-a] [-f FONT.ttf] [-m MONO.ttf] [-M MONOBOLD.ttf]

        -f is the face the bar is drawn in, -m the monospace one the terminals
        it starts are given, with -M their bold. -x closes the launcher that is
        up and ends when there is one, which is what the chord is: a press opens
        it and the next press puts it away. -a opens it beside the panel's
        launcher button, at the rectangle the compositor wrote in the window
        table when the button was pressed (above a bottom panel, below a top
        one, to the side of a left or right one, all of it on the screen), the
        edge nearest the panel kept where it is as the list grows and shrinks;
        with no such rectangle it is in the middle, as the chord's is.

        Dawn Larsson - Apache-2.0 license
        github.com/dawnlarsson/moonwater
*/
#ifndef MOONWATER_CANVAS_LAUNCHER
#define MOONWATER_CANVAS_LAUNCHER

#define LAUNCHER_WIDTH 560 // logical pixels, times the scale: the bar's width; its field, its gap and its rows are the kit's
#define LAUNCHER_NARROW 480 // logical pixels: a bar narrower than this shares its power row between the buttons
#define LAUNCHER_ROWS 7    // rows the list shows at most
#define LAUNCHER_TEXT 256  // bytes of the field
#define LAUNCHER_NAMES 4096
#define LAUNCHER_NAME 40
#define LAUNCHER_DIRS 64
#define LAUNCHER_PATH 4096
#define LAUNCHER_HASH 8192
#define LAUNCHER_DIRENTS 16384
#define LAUNCHER_DIRENT_HEADER 19
#define LAUNCHER_WORDS 16
#define LAUNCHER_FULL (LAUNCHER_PATH + LAUNCHER_NAME + 2) // a directory of PATH, its slash and a name

#define LAUNCHER_TILE 88         // logical pixels: a tile of the grid across (its height is the kit's, UI_TILE_H)
#define LAUNCHER_GRID_LINES 3    // lines of tiles the grid shows at most: more scroll
#define LAUNCHER_RECENT 8        // apps kept as started last
#define LAUNCHER_RECENT_PATH "/root/launcher.recent" // under the root the settings are kept in: an app's id a line, the last first
#define LAUNCHER_MENU_PIN 100    // the app menu's items (the field's are the kit's, 1 to 7)
#define LAUNCHER_MENU_UNPIN 101

// What a row starts.
#define LAUNCHER_WINDOW 1u  // an app (the registry's: a kit program or a bowl's): a window of its own
#define LAUNCHER_PROGRAM 2u // a program of PATH: in a terminal
#define LAUNCHER_COMMAND 3u // the text: the shell's, in a terminal

// The kit's own programs are the registry's first entries, in this order (ui_kit_programs): an app's index is a kit program's place.
#define launcher_program ui_kit_program
#define launcher_programs ui_kit_programs
#define LAUNCHER_PROGRAMS UI_KIT_PROGRAMS

/*
        The power buttons, left to right: the ids of what each is called and says when it is armed, and what it starts. The first two are
        the machine's own stop and are always there. The third, Sleep, is `moonwater sleep`, there only where the kernel says it can
        suspend to memory (launcher.session's LAUNCHER_SLEEP, launcher_sleeps: the word mem in /sys/power/state).
*/
#define LAUNCHER_POWERS 3
#define LAUNCHER_ALWAYS 2    // the first of them are the machine's and are always offered
#define LAUNCHER_SLEEP 1u    // launcher.session's bit: the third button
#define LAUNCHER_POWER_STATE "/sys/power/state"
static const struct launcher_power
{
        unsigned int label, says;
        const char address_to command;
} launcher_powers[LAUNCHER_POWERS] = {{LANGUAGE_launcher_power_off, LANGUAGE_launcher_power_off_armed, "poweroff"},
                                      {LANGUAGE_launcher_restart, LANGUAGE_launcher_restart_armed, "reboot"},
                                      {LANGUAGE_launcher_sleep, LANGUAGE_launcher_sleep_armed, "moonwater sleep"}};

/*
        Whether an app is listed: every one, but the installer where this session keeps a drive inside the machine. Boot's
        verdict (/run/moonwater/verdict) says live or ask on a stick, and disk NAME where an install's data is kept; a NAME that says it is
        removable, or is on a USB bus, is a stick made with setup install, which is an installer's place too. Asked once a start.
*/
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
        // A live session (live, ask) is a stick's; no verdict is a machine boot has not settled, or no Moonwater's: not offered.
        if (got >= 4 && (!memory_compare(verdict, "live", 4) || !memory_compare(verdict, "ask ", 4)))
                offer = 1;
        else if (got > 5 && !memory_compare(verdict, "disk ", 5))
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

// How many apps the grid has with nothing typed.
static positive launcher_offered_count(void)
{
        positive count = 0;

        for (positive at = 0; at < (positive)ui_apps.count; at++)
                count += launcher_offered(at) != 0;
        return count;
}

// Names of PATH that are the system's own machinery and not something a person starts.
static const char address_to const launcher_hidden[] = {"compositor", "init", "launcher", null};

struct launcher_entry
{
        unsigned char kind;
        unsigned short index; // the desktop program, or the name
};

static struct
{
        struct window_row own;
        struct ui ui;
        b32 toggle;
        const char address_to regular; // the faces: the bar's, the terminals', and their bold
        const char address_to mono;
        const char address_to mono_bold;

        // The field (the engine's, in the kit's well) and its context menu; text is what it holds, with the blanks at
        // either end left off, which is what the list is for and what the command row runs.
        struct ui_text field;
        struct ui_control search;
        struct ui_popup menu;
        char text[LAUNCHER_TEXT];
        struct launcher_entry entry[LAUNCHER_ROWS];
        positive entries;
        struct ui_list list; // the rows, the kit's: its focus is the highlighted row

        // With nothing typed: the apps of the grid (the registry's entries offered, the kit's first, then by name) and the ones started
        // last that are still there, each the kit's list of tiles; recents is the one the keys are in (else the grid), and menu_app
        // the app a menu was opened for (-1 none).
        unsigned short apps[UI_APPS_MOST];
        int app_count;
        unsigned short recent[LAUNCHER_RECENT];
        int recent_count;
        struct ui_list grid, recents;
        b32 in_recents;
        int menu_app;
        unsigned int generation; // the registry's read the apps are of

        // The names read from PATH, LAUNCHER_NAME bytes each, and the directory each came from: one mapping.
        char address_to names;
        unsigned char address_to from;
        unsigned short address_to hashed;
        positive count;
        char path[LAUNCHER_PATH];
        const char address_to dirs[LAUNCHER_DIRS];
        positive dir_count;

        // The names of PATH the last plain word named (every one, in the order they were read), the word, and the
        // count of names then: a word that extends it names no other name, so the next search reads only these.
        unsigned short named[LAUNCHER_NAMES];
        positive named_count, named_length, named_names;
        char named_text[LAUNCHER_TEXT];

        int width, height;
        struct ui_buffers buffers; // the bar's two buffers (the kit's): its picture is drawn whole each frame
        unsigned int ground;
        b32 focused, dirty;

        // Opened beside the panel's launcher button (-a): the kit's anchor (ui_anchor_take), and whether the place is asked again
        // with the next picture, as the list grows and shrinks (a bar above a bottom panel keeps its bottom).
        b32 asked_beside, moved;
        struct ui_anchor anchor;

        // The bar as the view declares it (launcher_view): its tree, its rows and its buttons, and how many starts the power buttons made
        // (what a check counts).
        struct ui_view view;
        unsigned int power_runs;
        unsigned int session; // the buttons past the first two that are offered: LAUNCHER_SLEEP (launcher_session_read)
} launcher;

/* ---- the model: what a text names, and what a row starts ---- */

/*
        Whether a text is a command line and not the name of one program: a
        blank, a pipe, a redirect, a list, a quote, a glob, an expansion, an
        assignment or a path is more than a shell would read as a plain word,
        and a plain word that names no program is no program's name either.
*/
static b32 launcher_command_line(const char address_to text)
{
        return text[0] && string_first_of_set((string_address)text, (string_address) " \t\n|&;<>()$`\\\"'*?[]{}~#!=/");
}

/*
        How well a text names a name, in 3 (the same), 2 (its start), 1 (inside
        it) or 0 (not at all), whatever the case of either.
*/
// A text with each letter in lower case, code point by code point by the library's table (a byte that is not UTF-8 stays), into room
// bytes less one: a name and a text that have a letter past ASCII are ranked by these, as the ASCII ones are by the case-blind compares.
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
                {
                        into[made++] = text[at - 1];
                        state = (memory_utf8_state){0};
                }
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

static unsigned int launcher_rank(const char address_to text, positive length, const char address_to name, positive size)
{
        if (!length || length > size)
                return 0;
        if (!launcher_ascii(text, length) || !launcher_ascii(name, size))
        {
                char folded_text[96], folded_name[96];
                positive a = launcher_lower(folded_text, sizeof(folded_text), text, length), b = launcher_lower(folded_name, sizeof(folded_name), name, size);

                if (!a || a > b)
                        return 0;
                if (!memory_compare_ascii_case((address_any)folded_text, (address_any)folded_name, a))
                        return a == b ? 3 : 2;
                return memory_search_ascii_case((address_any)folded_name, b, (address_any)folded_text, a) ? 1 : 0;
        }
        if (!memory_compare_ascii_case((address_any)text, (address_any)name, length))
                return length == size ? 3 : 2;
        return memory_search_ascii_case((address_any)name, size, (address_any)text, length) ? 1 : 0;
}

// How well a text names what is called label and also named by words (a blank apart): the label, or any of the words.
static unsigned int launcher_names_rank(const char address_to text, positive length, const char address_to label,
                                        const char address_to words)
{
        unsigned int best = launcher_rank(text, length, label, string_length((string_address)label));

        while (*words)
        {
                positive size = (positive)(string_first_of_or_end((string_address)words, ' ') - (string_address)words);
                unsigned int rank = launcher_rank(text, length, words, size);

                best = rank > best ? rank : best;
                words += size + (words[size] == ' ');
        }
        return best;
}

// How well a text names a desktop program: by its English names, which work in every language, or by the names the language has.
static unsigned int launcher_program_rank(const char address_to text, positive length,
                                          const struct launcher_program address_to program)
{
        unsigned int best = launcher_names_rank(text, length, program->label, program->words), said;

        if (!language.loaded)
                return best;
        said = launcher_names_rank(text, length, language_of(program->label), language_of(program->words));
        return said > best ? said : best;
}

// How well a text names an app: a kit program as above, a bowl's by its name and the words its entry gives.
static unsigned int launcher_app_rank(const char address_to text, positive length, int which)
{
        const struct ui_app_entry address_to app = &ui_apps.app[which];

        if (app->kit)
                return launcher_program_rank(text, length, &launcher_programs[app->kit - 1]);
        return launcher_names_rank(text, length, app->name, app->words);
}

// What an app is called here: a kit program in the language, a bowl's as its entry says it in the language it was read in.
static const char address_to launcher_app_name(int which)
{
        const struct ui_app_entry address_to app = &ui_apps.app[which];

        return app->kit ? language_of(launcher_programs[app->kit - 1].label) : app->name;
}

// The better rank first, then the shorter name, then the earlier in the alphabet.
static b32 launcher_before(unsigned int rank, const char address_to name, unsigned int other_rank,
                           const char address_to other)
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

/*
        A name that is not a program of its own: the system's machinery, and the names of PATH an app is started by, which the list has
        as the app (a kit program's, and a bowl's launcher and the profile it is the session of: chrome, chromium-browser and steam are
        their apps, not a terminal's programs).
*/
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

/*
        The apps of the grid, into launcher.apps: the kit's programs offered, in their order, then the bowls' by name (the case of a
        letter aside). Made again when the registry has been read again.
*/
static fn launcher_apps_make(void)
{
        int count = 0;

        for (int at = 0; at < ui_apps.count && count < UI_APPS_MOST; at++)
                if (launcher_offered((positive)at))
                {
                        const char address_to name = launcher_app_name(at);
                        int place = count++;

                        // The kit's stay first, as they are; a bowl's goes in by its name among the bowls' before it.
                        while (place > 0 && !ui_apps.app[at].kit && !ui_apps.app[launcher.apps[place - 1]].kit)
                        {
                                const char address_to other = launcher_app_name(launcher.apps[place - 1]);
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

/*
        The rows for the text in the field, into launcher.entry:

            no text             none: the grid of the apps, and Recent;
            a plain word        the apps it names, then the names of PATH it names
                                (the nearest LAUNCHER_ROWS - 1), then the command
                                row;
            a command line      the command row, first, but where the text is the
                                whole name of an app; then the apps it names, and
                                the command row last.

        The command row is always there for a text, and the highlight starts at
        the first row.
*/
static fn launcher_search(void)
{
        positive length = string_length((string_address)launcher.text), found = 0, count = 0;
        b32 line = launcher_command_line(launcher.text), first;
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
                        {
                                launcher.entry[count].kind = LAUNCHER_WINDOW;
                                launcher.entry[count++].index = launcher.recent[at];
                        }
                        launcher.entries = count;
                }
                return;
        }

        for (int at = 0; at < ui_apps.count; at++)
                programs[at] = launcher_offered((positive)at) ? launcher_app_rank(launcher.text, length, at) : 0;

        // A command line leads with the command row, unless the text is the whole name of an app.
        first = line;
        for (int at = 0; at < ui_apps.count; at++)
                if (programs[at] == 3)
                        first = false;
        if (first)
        {
                launcher.entry[count].kind = LAUNCHER_COMMAND;
                launcher.entry[count++].index = 0;
        }

        // The apps, the nearest first, with the room for the command row kept where it is last.
        for (unsigned int rank = 3; rank > 0; rank--)
                for (int at = 0; at < ui_apps.count && count < LAUNCHER_ROWS - (first ? 0u : 1u); at++)
                        if (programs[at] == rank)
                        {
                                launcher.entry[count].kind = LAUNCHER_WINDOW;
                                launcher.entry[count++].index = (unsigned short)at;
                        }

        // The names of PATH: the room left under the desktop's, less the command row. A word that extends the last
        // one searched reads only the names that one named (a name that starts with or contains the longer word
        // starts with or contains the shorter), and the names it names are kept for the next word.
        if (!line)
        {
                b32 narrowed = launcher.named_length && length > launcher.named_length && launcher.named_names == launcher.count &&
                               !memory_compare(launcher.text, launcher.named_text, launcher.named_length);
                positive total = narrowed ? launcher.named_count : launcher.count, kept = 0;

                for (positive seen = 0; seen < total; seen++)
                {
                        positive at = narrowed ? launcher.named[seen] : seen;
                        const char address_to name = launcher_name(at);
                        unsigned int rank = launcher_rank(launcher.text, length, name, string_length((string_address)name));
                        positive room = count + 1 < LAUNCHER_ROWS ? LAUNCHER_ROWS - 1 - count : 0, place = found;

                        if (!rank)
                                continue;
                        launcher.named[kept++] = (unsigned short)at;
                        if (!room)
                                continue;
                        if (found == room && !launcher_before(rank, name, ranks[found - 1], launcher_name(best[found - 1])))
                                continue;
                        if (found < room)
                                found++;
                        else
                                place = found - 1;
                        while (place > 0 && launcher_before(rank, name, ranks[place - 1], launcher_name(best[place - 1])))
                        {
                                ranks[place] = ranks[place - 1];
                                best[place] = best[place - 1];
                                place--;
                        }
                        ranks[place] = rank;
                        best[place] = (unsigned short)at;
                }
                launcher.named_count = kept;
                launcher.named_length = length;
                launcher.named_names = launcher.count;
                memory_copy(launcher.named_text, launcher.text, length + 1);
        }
        else
                launcher.named_length = 0;

        for (positive at = 0; at < found; at++)
        {
                launcher.entry[count].kind = LAUNCHER_PROGRAM;
                launcher.entry[count++].index = best[at];
        }

        // Last, where it was not first: the room for it was kept.
        if (!first)
        {
                launcher.entry[count].kind = LAUNCHER_COMMAND;
                launcher.entry[count++].index = 0;
        }
        launcher.entries = count;
}

/*
        The words a row starts, into words (a null after them), as many as
        there are: an app (the registry's words for it: a kit program and the
        face it draws in, a bowl's launcher and its entry's words, in a terminal
        only where the entry says Terminal), a program of PATH (a terminal, and
        the program by its full name), the command (a terminal, and the
        machine's shell given the text). Answers how many, with full holding a
        name that was made.
*/
static positive launcher_words(const struct launcher_entry address_to entry, string_address address_to words, char address_to full,
                               positive room)
{
        positive count = 0;
        const char address_to mono = launcher.mono ? launcher.mono : launcher.regular;
        b32 terminal = entry->kind != LAUNCHER_WINDOW;
        b32 draws_mono = terminal;

        if (!terminal)
                return entry->index < ui_apps.count ? ui_apps_words_of(entry->index, words, launcher.regular, launcher.mono, launcher.mono_bold)
                                                    : (words[0] = null, 0);
        words[count++] = (string_address) "/terminal";
        if (draws_mono ? mono : launcher.regular)
        {
                words[count++] = (string_address) "-f";
                words[count++] = (string_address)(draws_mono ? mono : launcher.regular);
        }
        if (draws_mono && launcher.mono_bold)
        {
                words[count++] = (string_address) "-b";
                words[count++] = (string_address)launcher.mono_bold;
        }
        if (entry->kind == LAUNCHER_PROGRAM)
        {
                // The directory it came from, and its name.
                positive size = string_length((string_address)launcher.dirs[launcher.from[entry->index]]);

                if (size + 1 + LAUNCHER_NAME < room)
                {
                        memory_copy(full, launcher.dirs[launcher.from[entry->index]], size);
                        full[size] = '/';
                        string_copy(full + size + 1, (string_address)launcher_name(entry->index));
                        words[count++] = (string_address) "-x";
                        words[count++] = (string_address)full;
                }
        }
        else if (entry->kind == LAUNCHER_COMMAND)
        {
                words[count++] = (string_address) "-x";
                words[count++] = (string_address) "/shell";
                words[count++] = (string_address) "-c";
                words[count++] = (string_address)launcher.text;
        }
        words[count] = null;
        return count;
}

// The most height the bar may have: the output's, and beside a panel along the top or bottom of its screen the room that
// panel leaves, so the bar does not cover it.
static int launcher_most_height(void)
{
        const struct ui_anchor address_to anchor = &launcher.anchor;
        int most = (int)launcher.own.table->output_height, room;

        if (!anchor->anchored || (anchor->edge != TASKBAR_BOTTOM && anchor->edge != TASKBAR_TOP))
                return most;
        room = anchor->edge == TASKBAR_BOTTOM ? anchor->button[1] - anchor->screen[1]
                                              : anchor->screen[1] + anchor->screen[3] - anchor->button[1] - anchor->button[3];
        return room > 0 && room < most ? room : most;
}

/* ---- PATH ---- */

// PATH as this program has it, or the one a terminal's shell starts with.
static const char address_to launcher_path_variable(void)
{
        string_address address_to environment = program_environment_list();

        for (positive at = 0; environment && environment[at]; at++)
                if (!memory_compare(environment[at], "PATH=", 5) && environment[at][5])
                        return environment[at] + 5;
        return BOWL_DEFAULT_PATH;
}

// A name is kept once: the first directory that has it wins. False when it was kept before or there is no room.
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
        launcher.hashed[slot] = (unsigned short)(launcher.count + 1);
        launcher.count++;
        return true;
}

/*
        What a directory has that can be run: not a directory, not what its
        user may not execute, and not a name that no one could type as one word.
        d_type says a directory, a file or a link; a link's target is looked
        at (a link to a directory is opened as one).
*/
static b32 launcher_runnable(b32 directory, const struct linux_dirent64 address_to record)
{
        const char address_to name = (const char address_to)record->d_name;
        b32 opened;

        if (record->d_type == DT_DIR || launcher_command_line(name) || launcher_hidden_name(name))
                return false;
        if (system_access_at(directory, name, 1) != 0)
                return false;
        if (record->d_type == DT_REG)
                return true;
        // A link or a file system that does not say: a directory is not a program.
        opened = (b32)system_open_at(directory, (string_address)name, FILE_READ | O_DIRECTORY | O_CLOEXEC);
        if (opened >= 0)
                system_close(opened);
        return opened < 0;
}

// Whether a directory is one already listed from PATH.
static b32 launcher_listed(const char address_to directory)
{
        for (positive which = 0; which < launcher.dir_count; which++)
                if (string_equals((string_address)launcher.dirs[which], (string_address)directory))
                        return true;
        return false;
}

// Every directory of a PATH once, into the names.
static fn launcher_scan(const char address_to variable)
{
        char address_to buffer = (char address_to)launcher.hashed + LAUNCHER_HASH * sizeof(unsigned short);
        positive size = string_length((string_address)variable);

        launcher.named_length = 0;

        size = size >= LAUNCHER_PATH ? LAUNCHER_PATH - 1 : size;
        memory_copy(launcher.path, variable, size);
        launcher.path[size] = 0;

        for (positive at = 0; launcher.path[at] && launcher.dir_count < LAUNCHER_DIRS;)
        {
                positive stop = at, next;

                while (launcher.path[stop] && launcher.path[stop] != ':')
                        stop++;
                next = launcher.path[stop] ? stop + 1 : stop;
                launcher.path[stop] = 0;
                // An empty entry is no directory, and a directory a PATH repeats is listed once: a repeat takes no room.
                if (stop > at && !launcher_listed(launcher.path + at))
                        launcher.dirs[launcher.dir_count++] = launcher.path + at;
                at = next;
        }

        for (positive which = 0; which < launcher.dir_count; which++)
        {
                bipolar handle = system_open_at(AT_FDCWD, (string_address)launcher.dirs[which],
                                                FILE_READ | O_DIRECTORY | O_CLOEXEC);

                if (handle < 0)
                        continue;
                for (;;)
                {
                        bipolar got = system_read_directory(handle, buffer, LAUNCHER_DIRENTS);

                        if (got <= 0)
                                break;
                        for (positive at = 0; at < (positive)got;)
                        {
                                const struct linux_dirent64 address_to record = (const struct linux_dirent64 address_to)(buffer + at);

                                // The record's header is nineteen bytes, and a name after it: sizeof pads it to 24.
                                if (record->d_reclen <= LAUNCHER_DIRENT_HEADER || at + record->d_reclen > (positive)got)
                                        break;
                                at += record->d_reclen;
                                if (record->d_name[0] != '.' && launcher_runnable((b32)handle, record))
                                        launcher_keep((const char address_to)record->d_name, which);
                        }
                }
                system_close(handle);
        }
}

/* ---- starting ---- */

/*
        The apps started last, as their file keeps them (an id a line, the last first, LAUNCHER_RECENT at most): those the registry still
        has and the grid offers go into launcher.recent. launcher_recent_add puts one first and keeps the file.
*/
static fn launcher_recent_path(char address_to path, positive room)
{
        string_copy_bounded((p8 address_to)path, (string_address)compositor_settings_root, room);
        string_append_bounded((p8 address_to)path, (string_address)LAUNCHER_RECENT_PATH, room);
}

static positive launcher_recent_text(char address_to text, positive room)
{
        char path[160];
        bipolar handle, got = 0;

        launcher_recent_path(path, sizeof(path));
        handle = system_open_at(AT_FDCWD, (string_address)path, FILE_READ | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK);
        if (handle >= 0)
        {
                got = system_read_once(handle, text, room - 1);
                system_close(handle);
        }
        text[got > 0 ? got : 0] = 0;
        return got > 0 ? (positive)got : 0;
}

static fn launcher_recent_read(void)
{
        char text[LAUNCHER_RECENT * (WINDOWS_APP_ID + 1) + 1];
        positive length = launcher_recent_text(text, sizeof(text));

        launcher.recent_count = 0;
        for (positive at = 0; at < length && launcher.recent_count < LAUNCHER_RECENT;)
        {
                positive stop = at;
                int which;

                while (stop < length && text[stop] != '\n')
                        stop++;
                text[stop] = 0;
                which = ui_apps_find(text + at);
                if (which >= 0 && launcher_offered((positive)which))
                {
                        b32 again = false;

                        for (int seen = 0; seen < launcher.recent_count; seen++)
                                again |= launcher.recent[seen] == which;
                        if (!again)
                                launcher.recent[launcher.recent_count++] = (unsigned short)which;
                }
                at = stop + 1;
        }
}

static fn launcher_recent_add(int which)
{
        char text[LAUNCHER_RECENT * (WINDOWS_APP_ID + 1) + 1], made[sizeof(text) + WINDOWS_APP_ID + 1], path[160];
        positive length = launcher_recent_text(text, sizeof(text)), used = 0, lines = 1;
        bipolar handle;

        string_copy_bounded((p8 address_to)made, (string_address)ui_apps.app[which].id, sizeof(made));
        string_append_bounded((p8 address_to)made, (string_address) "\n", sizeof(made));
        used = string_length((string_address)made);
        for (positive at = 0; at < length && lines < LAUNCHER_RECENT;)
        {
                positive stop = at;

                while (stop < length && text[stop] != '\n')
                        stop++;
                if (stop > at && stop - at < WINDOWS_APP_ID && !(stop - at == string_length((string_address)ui_apps.app[which].id) &&
                                                                   !memory_compare(text + at, ui_apps.app[which].id, stop - at)))
                {
                        memory_copy(made + used, text + at, stop - at);
                        used += stop - at, made[used++] = '\n', lines++;
                }
                at = stop + 1;
        }
        launcher_recent_path(path, sizeof(path));
        handle = system_open_at_mode(AT_FDCWD, (string_address)path, O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW | O_CLOEXEC, 0600);
        if (handle < 0)
                return;
        system_write_all((b32)handle, made, used);
        system_close(handle);
}

// One start path for a list row or an app tile: false when nothing could be started, and the launcher stays up.
static b32 launcher_launch(struct launcher_entry entry, b32 mark_starting)
{
        string_address words[LAUNCHER_WORDS];
        char full[LAUNCHER_FULL];
        positive count;

        if (entry.kind == LAUNCHER_WINDOW && entry.index >= ui_apps.count)
                return false;
        count = launcher_words(&entry, words, full, sizeof(full));
        if (!count || !ui_start_apart(words, count))
                return false;
        if (entry.kind == LAUNCHER_WINDOW)
        {
                if (mark_starting)
                        ui_app_starting(ui_apps.app[entry.index].id);
                launcher_recent_add(entry.index);
        }
        return true;
}

static b32 launcher_run(int row)
{
        return row >= 0 && (positive)row < launcher.entries && launcher_launch(launcher.entry[row], false);
}

static b32 launcher_start_app(int which)
{
        return which >= 0 && launcher_launch((struct launcher_entry){LAUNCHER_WINDOW, (unsigned short)which}, true);
}

// The app the keys are on with nothing typed: the grid's, or Recent's; -1 none.
static int launcher_app_focused(void)
{
        const struct ui_list address_to l = launcher.in_recents ? &launcher.recents : &launcher.grid;
        int count = launcher.in_recents ? launcher.recent_count : launcher.app_count;

        if (l->focus < 0 || l->focus >= count)
                return -1;
        return launcher.in_recents ? launcher.recent[l->focus] : launcher.apps[l->focus];
}

/*
        What a power button starts: the machine's shell given the poweroff or the reboot, which is what typing it does (the exit
        hooks, the sync and the quiet of the filesystems are the shell's). The words, a null after them; how many.
*/
static positive launcher_power_words(int which, string_address address_to words)
{
        words[0] = (string_address) "/shell";
        words[1] = (string_address) "-c";
        words[2] = (string_address)launcher_powers[which].command;
        words[3] = null;
        return 3;
}

/* ---- the bar as a view ---- */

// A power button pressed again to confirm: the shell starts what it is, and the bar goes (false) once it has. Kept (true) when
// there is no such button, or when nothing could be started.
// Whether a power button is offered: the machine's two always, Sleep only with its bit.
static b32 launcher_power_offered(int which)
{
        return which >= 0 && which < LAUNCHER_POWERS && (which < LAUNCHER_ALWAYS || (launcher.session >> (which - LAUNCHER_ALWAYS) & 1));
}

// Whether the kernel's list of the sleeps it can do (the text of /sys/power/state: words a blank apart) has suspend to memory, the
// whole word mem, which is what `moonwater sleep` asks for.
static b32 launcher_sleeps(const char address_to text, positive length)
{
        for (positive at = 0; at < length;)
        {
                positive stop = at;

                while (stop < length && text[stop] != ' ' && text[stop] != '\n' && text[stop])
                        stop++;
                if (stop - at == 3 && text[at] == 'm' && text[at + 1] == 'e' && text[at + 2] == 'm')
                        return true;
                at = stop + 1;
        }
        return false;
}

// Whether this machine can sleep, read once as the bar starts: Sleep where the kernel offers it. A file that is not there, or cannot be
// read, offers no sleep.
static fn launcher_session_read(const char address_to state_path)
{
        char text[128];
        bipolar handle = system_open_at(AT_FDCWD, (string_address)state_path, FILE_READ), got = 0;

        launcher.session = 0;
        if (handle < 0)
                return;
        got = system_read_once(handle, text, sizeof(text));
        system_close(handle);
        if (got > 0 && launcher_sleeps(text, (positive)got))
                launcher.session |= LAUNCHER_SLEEP;
}

static b32 launcher_power_act(int which)
{
        string_address words[4];

        if (!launcher_power_offered(which))
                return true;
        launcher.power_runs++;
        return !ui_start_apart(words, launcher_power_words(which, words));
}

// The power row, a component: a subtree of its buttons, each with what it says while it is armed, in the middle of the bar across (the
// layout's justify, so it is the middle at every scale and width, for two buttons or three, and reading from the right too). A bar
// narrower than LAUNCHER_NARROW (logical pixels) shares the row between them.
static fn launcher_power_row(void)
{
        ui_row(.class = "foot.middle")
                for (int at = 0; at < LAUNCHER_POWERS; at++)
                        if (launcher_power_offered(at))
                                ui_button(language_text(launcher_powers[at].label), .class = "action.outlined", .says = language_text(launcher_powers[at].says),
                                          .on = launcher_power_act, .arg = at, .grow = ui_width() < LAUNCHER_NARROW);
}

static struct ui_text address_to launcher_field(void);
static fn launcher_damaged(int top, int bottom);
static fn launcher_grid_describe(int id, struct ui_row address_to row);
static fn launcher_recent_describe(int id, struct ui_row address_to row);
static int launcher_per(void);

// The grid and Recent: the kit's tiles (a picture or a symbol over a name), a list each, made before the first build; the one the keys are
// on is lit, and Recent is a line.
static fn launcher_tiles_ready(void)
{
        if (!launcher.grid.describe)
        {
                launcher.grid.virtual = launcher.recents.virtual = true;
                launcher.grid.tiles = launcher.recents.tiles = LAUNCHER_TILE;
                launcher.grid.centre = launcher.recents.centre = true; // every line in the middle, a short one on its own
                launcher.grid.describe = launcher_grid_describe, launcher.recents.describe = launcher_recent_describe;
                launcher.grid.damaged = launcher.recents.damaged = launcher_damaged;
                launcher.grid.hover = launcher.grid.down = launcher.recents.hover = launcher.recents.down = -1;
                launcher.recents.focus = -1;
        }
        launcher.grid.total = launcher.app_count;
        launcher.recents.total = launcher.recent_count < launcher_per() ? launcher.recent_count : launcher_per();
        launcher.grid.lit = !launcher.in_recents && !launcher.text[0];
        launcher.recents.lit = launcher.in_recents && !launcher.text[0];
}

// How many tiles a line of the grid holds: the grid's own count once it is laid out, else what the bar's width in from its margin holds.
static int launcher_per(void)
{
        int scale = launcher.ui.scale > 0 ? launcher.ui.scale : 100, per;

        if (launcher.grid.w > 0)
                return ui_list_per(&launcher.ui, &launcher.grid);
        per = (launcher.width * 100 / scale - 2 * UI_PAD) / LAUNCHER_TILE;
        return per < 1 ? 1 : per;
}

// The bar as a tree, every size and look a class's: the field across the top, then the rows and the power row in from the margin.
// The field is the kit's, bare (no label), an element: it runs edge to edge, as the list's rows do, from the panel's top to where
// the list begins, and in it are the engine's text, selection and caret, which have the keys while the bar does (the caret is drawn
// only with the keys, not before the window has them). The rows are the kit's list, an element: the view places, lays out, draws and
// turns them, and their rows run from one edge of the bar to the other.
static fn launcher_view(void)
{
        struct ui_control address_to search = &launcher.search;

        if (!search->kind)
        {
                search->kind = UI_INPUT, search->variant = UI_FIELD_BARE;
                search->items[0] = WORDS(launcher_search_hint);
                search->edit = launcher_field();
        }
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
                        // Nothing typed: the grid of the apps, and under it Recent, a line of the ones started last.
                        ui_rows(&launcher.grid, .class = "list", .h = lines * UI_TILE_H, .hidden = typed || !lines);
                        if (!typed && launcher.recent_count)
                                ui_row(.class = "middle")
                                        ui_text(WORDS(launcher_recent), .class = "caption");
                        ui_rows(&launcher.recents, .class = "list", .h = UI_TILE_H, .hidden = typed || !launcher.recent_count);
                }
                ui_rows(&launcher.list, .class = "list", .h = (int)rows * UI_ROW, .hidden = !rows);
                if (launcher.asked_beside)
                        launcher_power_row();
        }
}

// The bar declared and laid out in launcher.width, at most limit tall (0: as tall as it comes to): the height it comes to.
static int launcher_build(int limit)
{
        ui_view_begin(&launcher.view, &launcher.ui, launcher.width, limit);
        launcher_view();
        return ui_view_end(&launcher.view);
}

/* ---- the field and the keys ---- */

// A new text: the list for it, from the top, and the height of the bar that shows it.
static fn launcher_changed(void)
{
        int was = launcher.height;

        launcher_search();
        launcher.height = launcher_build(launcher_most_height());
        // A bar above a bottom panel keeps its bottom where it is, so it is put again with the picture of its new height.
        launcher.moved |= launcher.anchor.anchored && launcher.buffers.shown >= 0 && launcher.anchor.edge == TASKBAR_BOTTOM && launcher.height != was;
        launcher.dirty = true;
}

// The field, made the first time it is wanted: one line of the engine, its clipboard this window's, the list's room, its text in the
// middle of the bar as the grid and the power row are (the kit's centred field).
static struct ui_text address_to launcher_field(void)
{
        if (!launcher.field.doc.selections && ui_field_open(&launcher.field, &launcher.own, ""))
                launcher.field.most = LAUNCHER_TEXT - 1, launcher.field.keeps_vertical = true, launcher.field.centred = true; // Up and Down move the list
        return &launcher.field;
}

// What the field holds now, its blanks at either end left off, into text: the list for it when it changed.
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

/*
        What the field asked after an event: Enter runs the highlighted row (false: what it started, and the bar goes),
        Escape closes the bar, a change is a new list.
*/
static b32 launcher_asked(p32 asks)
{
        if (asks & UI_TEXT_SUBMIT)
                return (launcher.text[0] || !launcher.asked_beside) ? !launcher_run(launcher.list.focus) : !launcher_start_app(launcher_app_focused());
        if (asks & UI_TEXT_CANCEL)
                return false;
        if (asks & UI_TEXT_CHANGED)
                launcher_take();
        if (asks)
                launcher.dirty = true;
        return true;
}

/*
        The menu of an app (the grid's, Recent's or a row's): Pin to taskbar where it is not pinned and may be, Unpin where it is, at a
        point of the bar (device pixels). It is the bar's popup, which the field's menu is too: what is chosen comes to launcher_chose.
*/
static b32 launcher_app_menu(int which, int x, int y, b32 touch)
{
        b32 pinned;

        if (which < 0 || which >= ui_apps.count)
                return false;
        pinned = ui_apps_pinned(compositor_settings_root, which);
        if (!pinned && !ui_apps_pinnable(which))
                return false;
        launcher.menu.menu.count = 0;
        launcher.menu.field = null;
        ui_popmenu_add(&launcher.menu.menu, pinned ? WORDS(taskbar_tab_unpin) : WORDS(taskbar_tab_pin), null, 0,
                       pinned ? LAUNCHER_MENU_UNPIN : LAUNCHER_MENU_PIN);
        launcher.menu_app = which;
        return ui_popmenu_open(&launcher.menu, &launcher.own, &launcher.ui, x, y, 0, 0, WINDOW_POPUP_POINT, touch);
}

// Where an app's tile is, for a menu opened from the keys: the middle of the tile the keys are on, as the list last laid it out.
static fn launcher_tile_point(const struct ui_list address_to l, int at, int address_to x, int address_to y)
{
        int per = ui_list_per(&launcher.ui, l), wide = ui_dev(&launcher.ui, l->tiles), high = ui_list_unit(&launcher.ui, l);

        *x = ui_list_tile_x(&launcher.ui, l, at) + wide / 2;
        *y = l->y + at / per * high - l->scroll.scroll + high / 2;
}

/*
        A key over the grid and Recent, with nothing typed: the arrows go along the tiles (Left and Right the way a line is read, Up and
        Down a line, the kit's ui_list_key), Down from the grid's last line goes into Recent and Up from Recent back to the grid's last
        line, and Shift-F10 or the menu key opens the app's menu. True when the key was one of these.
*/
static b32 launcher_grid_key(const struct window_event address_to event)
{
        struct ui_list address_to l = launcher.in_recents ? &launcher.recents : &launcher.grid;
        int key = (int)event->a, per = launcher_per(), focus;
        b32 moved = false;

        if (!launcher.asked_beside || launcher.text[0] || !launcher.app_count)
                return false;
        if ((key == 68 && (event->flags & WINDOW_KEY_SHIFT)) || key == 127)
        {
                int x, y, which = launcher_app_focused();

                launcher_tile_point(l, l->focus < 0 ? 0 : l->focus, &x, &y);
                launcher_app_menu(which, x, y, false);
                return true;
        }
        if (key != UI_KEY_UP && key != UI_KEY_DOWN && key != UI_KEY_LEFT && key != UI_KEY_RIGHT)
                return false;
        focus = l->focus < 0 ? 0 : l->focus;
        if (!launcher.in_recents && key == UI_KEY_DOWN && launcher.recent_count && focus / per == (launcher.app_count - 1) / per)
        {
                // Into Recent at the tile nearest across, as Up and Down go between centred lines (ui_list_tile_near).
                launcher.in_recents = true;
                launcher.recents.focus = ui_list_tile_near(&launcher.ui, &launcher.recents, 0, ui_list_tile_middle(&launcher.ui, &launcher.grid, focus));
        }
        else if (launcher.in_recents && key == UI_KEY_UP)
        {
                int to = ui_list_tile_near(&launcher.ui, &launcher.grid, (launcher.app_count - 1) / per, ui_list_tile_middle(&launcher.ui, &launcher.recents, focus));

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

// A key: the field's first, and what it leaves the bar's, which moves in the list. False when it closes the bar.
static b32 launcher_key(const struct window_event address_to event)
{
        p32 asks = ui_view_event(&launcher.ui, &launcher.view, event);

        // The power buttons are the view's: what it takes of a key (Shift-Tab, and every key while it has the keys) is not the field's.
        if (asks & UI_VIEW_DRAW)
                launcher.dirty = true;
        if (asks & UI_VIEW_QUIT)
                return false;
        if (asks & UI_VIEW_TAKEN)
                return true;
        if (!(event->flags & WINDOW_KEY_DOWN))
                return true;
        // With nothing typed the arrows are the grid's: the field has no text to move in.
        if (launcher_grid_key(event))
                return true;
        if ((asks = ui_field_send(&launcher.ui, launcher_field(), &launcher.menu, event)))
                return launcher_asked(asks);
        if (event->a == UI_KEY_UP || event->a == UI_KEY_DOWN ||
            ((event->flags & WINDOW_KEY_CONTROL) && (event->a == UI_KEY_P || event->a == UI_KEY_N)))
        {
                // Control-P and Control-N are Up and Down: the list's own keys (ui_list_key, a row each way, held at the ends).
                int key = event->a == UI_KEY_P ? UI_KEY_UP : event->a == UI_KEY_N ? UI_KEY_DOWN : event->a;
                b32 moved;

                ui_list_key(&launcher.ui, &launcher.list, key, &moved);
        }
        else if (event->a == UI_KEY_TAB && launcher.entries)
        {
                // The highlighted name into the field, where it is a name.
                const struct launcher_entry address_to entry = &launcher.entry[launcher.list.focus];
                const char address_to name = entry->kind == LAUNCHER_PROGRAM ? launcher_name(entry->index)
                                             : entry->kind == LAUNCHER_WINDOW ? launcher_app_name(entry->index)
                                                                              : null;

                if (name)
                {
                        ui_text_set(launcher_field(), (const p8 address_to)name, string_length((string_address)name));
                        launcher_take();
                }
        }
        return true;
}

// An item of the field's context menu, done to the field.
static fn launcher_chose(int id)
{
        // An app's menu: pinned or not, kept in the pins' file, which the panel reads when it changes.
        if (id == LAUNCHER_MENU_PIN || id == LAUNCHER_MENU_UNPIN)
        {
                ui_apps_pin(compositor_settings_root, launcher.menu_app, id == LAUNCHER_MENU_PIN);
                launcher.dirty = true;
                return;
        }
        launcher_asked(ui_text_menu_do(&launcher.ui, launcher.menu.field, id));
}

static fn launcher_describe(int id, struct ui_row address_to row);

static fn launcher_damaged(int top, int bottom)
{
        (void)top, (void)bottom;
        launcher.dirty = true;
}

// A tile of an app: its name under its picture, a kit program's symbol where it has one, or the lettered tile of its name.
static const unsigned int address_to launcher_grid_picture(int id, int side)
{
        return id >= 0 && id < launcher.app_count ? ui_apps_picture(launcher.apps[id], side) : null;
}

static const unsigned int address_to launcher_recent_picture(int id, int side)
{
        return id >= 0 && id < launcher.recent_count ? ui_apps_picture(launcher.recent[id], side) : null;
}

static fn launcher_tile(int which, struct ui_row address_to row)
{
        const struct ui_app_entry address_to app = &ui_apps.app[which];
        int symbol = app->kit ? ui_icon_of(app->id) : -1;

        // A name the entry chose is words from outside: a control byte in it is seen, and a mark that turns the letters round does nothing.
        static char names[2 * UI_LIST_MOST][UI_APP_NAME + 8];
        static unsigned int next;
        char address_to label = names[next++ % (2 * UI_LIST_MOST)];
        const char address_to name = launcher_app_name(which);

        if (app->kit)
                string_copy_bounded((p8 address_to)label, (string_address)name, UI_APP_NAME + 8);
        else
                ui_label_foreign(label, UI_APP_NAME + 8, (const p8 address_to)name, string_length((string_address)name));
        row->label = label;
        if (symbol >= 0)
                row->icon = symbol + 1;
        else
                row->tile_name = app->kit ? app->id : app->name;
}

static fn launcher_grid_describe(int id, struct ui_row address_to row)
{
        if (id < 0 || id >= launcher.app_count)
                return;
        launcher_tile(launcher.apps[id], row);
        row->tile_picture = launcher_grid_picture;
}

static fn launcher_recent_describe(int id, struct ui_row address_to row)
{
        if (id < 0 || id >= launcher.recent_count)
                return;
        launcher_tile(launcher.recent[id], row);
        row->tile_picture = launcher_recent_picture;
}

// The bar laid out: the rows the list has, and the tree, which puts the field and the rows where they go.
static fn launcher_layout(void)
{
        if (!launcher.list.ids)
        {
                launcher.list.ids = LAUNCHER_ROWS;
                launcher.list.describe = launcher_describe;
                launcher.list.damaged = launcher_damaged;
                launcher.list.hover = launcher.list.down = -1;
        }
        launcher.list.count = (int)launcher.entries;
        for (int at = 0; at < launcher.list.count; at++)
                launcher.list.id[at] = (unsigned short)at;
        // The words are asked each time (a language chosen while the bar is up is the next picture's), and the rows fill the window below
        // the field; a list longer than the window scrolls (the wheel, and the keys reveal).
        launcher.list.lit = true;
        launcher_build(launcher.height);
}

// Pointer handling shared by the search list, the app grid, and Recent: hover-focuses on motion, clicks on release,
// and reports right-presses via *right_hit (>= 0). Returns clicked index or -1.
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

// The pointer over the grid and Recent (nothing typed): false when what it did closes the bar (an app started).
static b32 launcher_tiles_event(const struct window_event address_to event)
{
        struct ui_list address_to lists[2] = {&launcher.grid, &launcher.recents};
        int x = event->type == WINDOW_EVENT_MOTION ? event->a : ui_kit.pointer_x, y = event->type == WINDOW_EVENT_MOTION ? event->b : ui_kit.pointer_y;

        for (int which = 0; which < 2; which++)
        {
                int right_hit, clicked = launcher_list_pointer(lists[which], event, x, y, which != 0, true, &right_hit);

                if (right_hit >= 0)
                        launcher_app_menu(which ? launcher.recent[right_hit] : launcher.apps[right_hit], x, y, ui_kit_touch);
                if (clicked >= 0)
                        return !launcher_start_app(which ? launcher.recent[clicked] : launcher.apps[clicked]);
        }
        return true;
}

static b32 launcher_event(const struct window_event address_to event)
{
        int right_hit, clicked, x, y;
        p32 asks;

        launcher_layout();
        // The power buttons are the view's: its hover, its presses and releases, and a press anywhere else that drops what was
        // armed, before the field has the event; what the view takes of a pointer event is not the field's or the list's.
        if (event->type != WINDOW_EVENT_KEY)
        {
                asks = ui_view_event(&launcher.ui, &launcher.view, event);
                if (asks & UI_VIEW_DRAW)
                        launcher.dirty = true;
                if (asks & UI_VIEW_QUIT)
                        return false;
                if (asks & UI_VIEW_TAKEN)
                        return true;
        }
        // The field takes the pointer on its well (a caret, a drag, the middle button's paste, its menu) and the
        // clipboard's answers; the rest is the list's.
        // A press outside the field is the list's (the field's answer to it, commit or cancel, is the kit's and changes nothing here).
        if (event->type != WINDOW_EVENT_KEY &&
            (asks = ui_field_send(&launcher.ui, launcher_field(), &launcher.menu, event)) &&
            !(event->type == WINDOW_EVENT_BUTTON && (asks == UI_TEXT_COMMIT || asks == UI_TEXT_CANCEL)))
                return launcher_asked(asks);
        // With nothing typed beside the panel the pointer is the grid's and Recent's: the tile it moves onto is the one the keys are on,
        // a click starts it, and a right press opens its menu.
        if (event->type != WINDOW_EVENT_KEY && launcher.asked_beside && !launcher.text[0])
                return launcher_tiles_event(event);
        if (event->type == WINDOW_EVENT_KEY)
                return launcher_key(event);
        if (event->type != WINDOW_EVENT_MOTION && event->type != WINDOW_EVENT_BUTTON)
                return true;
        x = event->type == WINDOW_EVENT_MOTION ? event->a : ui_kit.pointer_x;
        y = event->type == WINDOW_EVENT_MOTION ? event->b : ui_kit.pointer_y;
        clicked = launcher_list_pointer(&launcher.list, event, x, y, false, false, &right_hit);
        if (right_hit >= 0 && (positive)right_hit < launcher.entries && launcher.entry[right_hit].kind == LAUNCHER_WINDOW)
                launcher_app_menu(launcher.entry[right_hit].index, x, y, ui_kit_touch);
        return clicked >= 0 ? !launcher_run(clicked) : true;
}

/* ---- the picture ---- */

static struct ui_app launcher_app; // the app (defined with its runner below): its had_keys is the kit's, read by the picture

// The label of a row, into into, and what is said at its right.
static fn launcher_label(const struct launcher_entry address_to entry, char address_to into, positive room,
                         const char address_to address_to note)
{
        *note = "";
        into[0] = 0;
        if (entry->kind == LAUNCHER_WINDOW && entry->index < ui_apps.count)
        {
                // A kit program is the desktop's; a bowl's app says the bowl it is from (its name is the entry's: words from outside).
                const struct ui_app_entry address_to app = &ui_apps.app[entry->index];

                if (app->kit)
                        string_copy_bounded((string_address)into, (string_address)launcher_app_name(entry->index), room);
                else
                        ui_label_foreign(into, room, (const p8 address_to)app->name, string_length((string_address)app->name));
                *note = app->kit ? language_of("desktop")
                                 : ui_apps_local && ui_apps_local[0] && string_equals((string_address)app->root, (string_address)ui_apps_local)
                                       ? (app->profile[0] ? app->profile : "local")
                                       : ui_apps_base(app->root);
        }
        else if (entry->kind == LAUNCHER_PROGRAM)
        {
                // A name of PATH is a file's name, which whoever can write a folder of PATH chose: it is written as words from outside
                // (ui_label_foreign), so a control byte in it is seen and a mark that turns its letters round does nothing. What is
                // started is the name as it is (launcher_run); only the row's words are made safe.
                const char address_to name = launcher_name(entry->index);

                ui_label_foreign(into, room, (const p8 address_to)name, string_length((string_address)name));
                *note = launcher.dirs[launcher.from[entry->index]];
        }
        else
        {
                language_format(into, room, WORDS(launcher_run), launcher.text);
                *note = "shell";
        }
}

// A row, for the kit's list: its label and the quiet word of where it is from.
static fn launcher_describe(int id, struct ui_row address_to row)
{
        static char label[LAUNCHER_TEXT + 8];

        if (id < 0 || id >= (int)launcher.entries)
                return;
        launcher_label(&launcher.entry[id], label, sizeof(label), &row->note);
        row->label = label;
}

static fn launcher_draw(unsigned int address_to pixels)
{
        struct ui address_to ui = &launcher.ui;

        ui_begin(ui, pixels, launcher.own.table->stride, launcher.width, launcher.height);
        launcher_layout();
        // The view: the field, the rows (the highlighted one the row the keys are on, the keys themselves staying in the field) and the power row.
        ui_view_draw(ui, &launcher.view, launcher.focused || !launcher_app.had_keys);
        // A finger's selection in the field: its handles and actions bar, over the rows and the buttons.
        ui_text_overlay(ui, launcher_field());
}

// The bar's picture, drawn whole into the buffer the kit gives (every frame is due whole: the bar has no rows to draw apart), and a new
// height of it that keeps its bottom: its place, asked with the picture it is for, in one knock (before the buffer is shown).
static fn launcher_paint(unsigned int address_to pixels)
{
        struct windows address_to table = launcher.own.table;
        unsigned int row = launcher.own.row;

        launcher_draw(pixels);
        if (launcher.moved)
        {
                ui_anchor_set(&launcher.anchor, table, row, launcher.width, launcher.height);
                __atomic_add_fetch(&table->moved[row], 1u, __ATOMIC_RELEASE);
                launcher.moved = false;
        }
}

/* ---- the program ---- */

// The names of PATH, their directories, the hash table and the scan's buffer: one mapping, not in the image's bss.
static b32 launcher_memory(void)
{
        positive names = (positive)LAUNCHER_NAMES * LAUNCHER_NAME, from = LAUNCHER_NAMES,
                 hashed = (positive)LAUNCHER_HASH * sizeof(unsigned short);
        char address_to mapped = ui_app_map(&launcher_app, names + from + hashed + LAUNCHER_DIRENTS);

        if (!mapped)
                return false;
        launcher.names = mapped;
        launcher.from = (unsigned char address_to)(mapped + names);
        launcher.hashed = (unsigned short address_to)(mapped + names + from);
        return true;
}

// The launcher's start, once its window is taken: the lift, the fonts and the first picture. -1 runs the loop.
// The search field keeps the hint it was set up with: a change of language sets it up again.
static fn launcher_language(void)
{
        launcher.search.kind = 0;
        if (launcher.width > 0 && launcher.height > 0)
                launcher_layout();
}

static int launcher_open(void)
{
        struct windows address_to table = launcher.own.table;
        unsigned int row = launcher.own.row;

        if (ui_lift_other(&launcher.own, (string_address) "launcher", launcher.toggle))
                return 0;

        if (!launcher_memory())
                return 1;

        ui_app_size(&launcher_app, (unsigned int)ui_px(&launcher.ui, LAUNCHER_WIDTH), 0);
        ui_anchor_take(&launcher.anchor, launcher.own.table, launcher.asked_beside);
        // The apps, as the bowls have them now, and the ones started last: the grid is the first picture's.
        ui_apps_fresh(ui_kit_time());
        launcher_apps_make();
        launcher_recent_read();
        launcher_changed();
        if (launcher.anchor.anchored)
                // Beside the panel's button, asked as a placement, which is read with the first picture.
                ui_anchor_set(&launcher.anchor, table, row, launcher.width, launcher.height);
        else
        {
                // The bar is centred across, and sits a third of the way down: its y is not the middle, so it is set after.
                ui_centre(table, row, launcher.width, 0, 0);
                {
                        int x, y, width, height;

                        windows_output(table, 0, &x, &y, &width, &height);
                        table->y[row] = y + height / 3 - ui_px(&launcher.ui, UI_PAD) - ui_px(&launcher.ui, UI_TARGET) / 2;
                }
        }
        ui_app_frame(&launcher_app);

        // The first frame is up before PATH is read. The empty field names no program of PATH, so the picture does not
        // change with the scan and needs no second frame; a key read after this sees every name.
        launcher_scan(launcher_path_variable());
        return -1;
}

// The keys going elsewhere (a click on the desktop, another window raised) puts the bar away; before they came here it had
// never had them.
// The caret's blink: the next frame it needs, and none once it has stopped (a while after the last key).
static long launcher_tick(long timeout, long address_to other)
{
        positive now = (positive)ui_kit.now;
        b32 redraw = false;
        int top, bottom;

        (void)other;
        timeout = ui_text_timeout(&launcher.ui, launcher_field(), timeout, &redraw);
        if (redraw || ui_view_moved(&launcher.view, &top, &bottom))
                launcher.dirty = true;
        return timeout;
}

static struct ui_app launcher_app = {
        .own = &launcher.own,
        .ui = &launcher.ui,
        .missing = (string_address) "launcher: no window: the compositor's desktop is not up\n",
        .title = (string_address) "Launcher",
        .glass = WINDOW_GLASS | WINDOW_NO_CLOSE | WINDOW_DISMISS, // a temporary overlay: glass over what is under it
        .no_bold = true,
        .nofont = (string_address) "launcher: no font to draw with (-f FONT.ttf), pid ",
        .dirty = &launcher.dirty,
        .focused = &launcher.focused,
        .ground = &launcher.ground,
        .open = launcher_open,
        .language = launcher_language,
        .event = launcher_event,
        .dismiss = true,        // a temporary overlay: the keys went elsewhere after it had them, so it is put away (ui_app_keys)
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
        string_address regular = (string_address) UI_FONT_REGULAR;

        memory_zero(&launcher, sizeof(launcher));
        launcher.menu_app = -1, launcher.generation = ~0u;
        launcher_session_read(LAUNCHER_POWER_STATE);
        ui_buffers_start(&launcher.buffers);

        for (positive at = 1; at < (positive)program_argument_count(); at++)
        {
                string_address option = program_argument(at), value;

                if (option[0] == '-' && option[1] == 'x' && !option[2])
                {
                        launcher.toggle = true;
                        continue;
                }
                if (option[0] == '-' && option[1] == 'a' && !option[2])
                {
                        launcher.asked_beside = true;
                        continue;
                }
                if ((value = ui_argument(at, (string_address) "-f")))
                        regular = value;
                else if ((value = ui_argument(at, (string_address) "-m")))
                        launcher.mono = (const char address_to)value;
                else if ((value = ui_argument(at, (string_address) "-M")))
                        launcher.mono_bold = (const char address_to)value;
                else
                        continue;
                at++;
        }
        //      No face given is the faces the desktop's programs find for themselves (ui_faces).
        if (regular == (string_address)UI_FONT_REGULAR && ui_faces()->text[0])
                regular = (string_address)ui_faces()->text;
        if (!launcher.mono && ui_faces()->face[0])
                launcher.mono = (const char address_to)ui_faces()->face;
        if (!launcher.mono_bold && ui_faces()->bold[0])
                launcher.mono_bold = (const char address_to)ui_faces()->bold;
        launcher.regular = (const char address_to)regular;
        launcher_app.font = regular;
        return ui_app_run(&launcher_app);
}

#endif // MOONWATER_CANVAS_LAUNCHER
