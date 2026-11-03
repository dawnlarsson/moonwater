/*
        The launcher, as a window on the compositor in userspace.

        A search bar in the middle of the screen: one field with the keys in it
        at once, and under it a short list of what the text names. The desktop's
        own programs come first (the settings, a terminal, the kernel log), then
        the programs found on PATH whose names start with or contain what was
        typed, the nearest names first, and last a row that runs the text itself
        as a shell command, so the bar is also a shell prompt.

        The field is the kit's (the text engine's field): a caret placed by the
        hand, a drag, a double and a triple click, Shift and the arrows, Control
        and the arrows by words, Home and End, Control-A, undo and redo, the
        clipboard and the middle button's paste, its context menu, and a blank
        typed anywhere, the first character too (the list is for the text with
        its blanks at either end left off). What the field leaves is the bar's:
        Up and Down (or Control-P and Control-N) move in the list, Tab takes the
        highlighted name into the field, and Enter runs the highlighted row:

            a desktop program    is started as a window of its own;
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

        Under the list is a row of two buttons, Power off and Restart: the kit's row of buttons (ui_buttons), which does the
        pointer, the keys and the press again to confirm, so a button asks for a second press (and a held key's repeats are
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

// What a row starts.
#define LAUNCHER_WINDOW 1u  // a program of the desktop: a window of its own
#define LAUNCHER_PROGRAM 2u // a program of PATH: in a terminal
#define LAUNCHER_COMMAND 3u // the text: the shell's, in a terminal


struct launcher_program
{
        const char address_to label;
        const char address_to words;   // what else names it, a blank apart
        const char address_to command; // the program, which takes -f for its face
        unsigned char mono;            // it is drawn in the monospace face
};

// The power buttons, left to right: the ids of what each is called and says when it is armed, and what it starts (the machine's own stop).
#define LAUNCHER_POWERS 2
static const struct launcher_power
{
        unsigned int label, says;
        const char address_to command;
} launcher_powers[LAUNCHER_POWERS] = {{LANGUAGE_launcher_power_off, LANGUAGE_launcher_power_off_armed, "poweroff"},
                                      {LANGUAGE_launcher_restart, LANGUAGE_launcher_restart_armed, "reboot"}};

// The desktop's own programs, in the order they are listed.
static const struct launcher_program launcher_programs[] = {
    {"Settings", "settings preferences options", "/settings", 0},
    {"Terminal", "terminal shell console", "/terminal", 1},
    {"Kernel log", "kernel log klog dmesg messages", "/klog", 1},
};

#define LAUNCHER_PROGRAMS (sizeof(launcher_programs) / sizeof(launcher_programs[0]))

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

        int width, height, shown;
        unsigned int ground;
        int pointer_x, pointer_y;
        b32 focused, had_focus, dirty;
        struct ui_press press; // a finger's press on a row: shown pressed as it lands, run as it lifts there

        // Opened beside the panel's launcher button (-a): the rectangle it opens beside and the edge of the screen the panel
        // is along (the panel's own numbers), the screen's rectangle and number, and whether the place is asked again with
        // the next picture, as the list grows and shrinks (a bar above a bottom panel keeps its bottom).
        b32 asked_beside, anchored, moved;
        int anchor[4], screen[4];
        unsigned int edge, number;

        // The power row under the list: the kit's buttons, and how many starts they made (what a check counts).
        struct ui_buttons power;
        unsigned int power_runs;
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
static unsigned int launcher_rank(const char address_to text, positive length, const char address_to name, positive size)
{
        if (!length || length > size)
                return 0;
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

// A name that is not a program of its own: the desktop's names, which the list already has, and the system's machinery.
static b32 launcher_hidden_name(const char address_to name)
{
        for (positive at = 0; launcher_hidden[at]; at++)
                if (string_equals((string_address)name, (string_address)launcher_hidden[at]))
                        return true;
        for (positive at = 0; at < LAUNCHER_PROGRAMS; at++)
                if (string_equals((string_address)name, (string_address)launcher_programs[at].command + 1))
                        return true;
        return false;
}

/*
        The rows for the text in the field, into launcher.entry:

            no text             the desktop's programs;
            a plain word        the desktop's programs it names, then the names
                                of PATH it names (the nearest LAUNCHER_ROWS - 1),
                                then the command row;
            a command line      the command row, first, but where the text is the
                                whole name of a desktop program; then the desktop
                                programs it names, and the command row last.

        The command row is always there for a text, and the highlight starts at
        the first row.
*/
static fn launcher_search(void)
{
        positive length = string_length((string_address)launcher.text), found = 0, count = 0;
        b32 line = launcher_command_line(launcher.text), first;
        unsigned int programs[LAUNCHER_PROGRAMS], ranks[LAUNCHER_ROWS];
        unsigned short best[LAUNCHER_ROWS];

        launcher.list.focus = 0;
        launcher.entries = 0;

        if (!length)
        {
                launcher.named_length = 0;
                for (positive at = 0; at < LAUNCHER_PROGRAMS; at++)
                {
                        launcher.entry[at].kind = LAUNCHER_WINDOW;
                        launcher.entry[at].index = (unsigned short)at;
                }
                launcher.entries = LAUNCHER_PROGRAMS;
                return;
        }

        for (positive at = 0; at < LAUNCHER_PROGRAMS; at++)
                programs[at] = launcher_program_rank(launcher.text, length, &launcher_programs[at]);

        // A command line leads with the command row, unless the text is the whole name of a desktop program.
        first = line;
        for (positive at = 0; at < LAUNCHER_PROGRAMS; at++)
                if (programs[at] == 3)
                        first = false;
        if (first)
        {
                launcher.entry[count].kind = LAUNCHER_COMMAND;
                launcher.entry[count++].index = 0;
        }

        for (unsigned int rank = 3; rank > 0; rank--)
                for (positive at = 0; at < LAUNCHER_PROGRAMS && count < LAUNCHER_ROWS; at++)
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
        there are: a desktop program (its program, and the face it draws in),
        a program of PATH (a terminal, and the program by its full name), the
        command (a terminal, and the machine's shell given the text). Answers
        how many, with full holding a name that was made.
*/
static positive launcher_words(const struct launcher_entry address_to entry, string_address address_to words, char address_to full,
                               positive room)
{
        positive count = 0;
        const char address_to mono = launcher.mono ? launcher.mono : launcher.regular;
        b32 terminal = entry->kind != LAUNCHER_WINDOW;
        b32 draws_mono = terminal || launcher_programs[entry->index].mono;

        words[count++] = terminal ? (string_address) "/terminal"
                                  : (string_address)launcher_programs[entry->index].command;
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

/*
        Where the bar's top left goes beside the rectangle the panel's button gave (button: x, y, width, height), for the
        edge the panel is along: a bottom panel's bar ends where the rectangle starts, a top panel's starts where it ends, a
        left panel's starts where it ends and a right panel's ends where it starts, the top of the bar at the top of the
        rectangle for the two sides, and the bar's left at the button's for the other two. Then kept inside the screen
        (screen: x, y, width, height), all of it where it fits and its top left where it does not.
*/
static fn launcher_anchor_place(unsigned int edge, const int address_to button, const int address_to screen, int width, int height,
                                int address_to x, int address_to y)
{
        *x = button[0], *y = button[1];
        if (edge == TASKBAR_BOTTOM)
                *y = button[1] - height;
        else if (edge == TASKBAR_TOP)
                *y = button[1] + button[3];
        else if (edge == TASKBAR_LEFT)
                *x = button[0] + button[2];
        else
                *x = button[0] - width;
        *x = *x > screen[0] + screen[2] - width ? screen[0] + screen[2] - width : *x;
        *y = *y > screen[1] + screen[3] - height ? screen[1] + screen[3] - height : *y;
        *x = *x < screen[0] ? screen[0] : *x;
        *y = *y < screen[1] ? screen[1] : *y;
}

// The most height the bar may have: the output's, and beside a panel along the top or bottom of its screen the room that
// panel leaves, so the bar does not cover it.
static int launcher_most_height(void)
{
        int most = (int)launcher.own.table->output_height, room;

        if (!launcher.anchored || (launcher.edge != TASKBAR_BOTTOM && launcher.edge != TASKBAR_TOP))
                return most;
        room = launcher.edge == TASKBAR_BOTTOM ? launcher.anchor[1] - launcher.screen[1]
                                               : launcher.screen[1] + launcher.screen[3] - launcher.anchor[1] - launcher.anchor[3];
        return room > 0 && room < most ? room : most;
}

// The bar's place asked of the compositor, in its row's placement columns, counted from its screen's top left.
static fn launcher_place(void)
{
        struct window_place place;
        int x, y;

        memory_zero(&place, sizeof(place));
        launcher_anchor_place(launcher.edge, launcher.anchor, launcher.screen, launcher.width, launcher.height, &x, &y);
        place.how = WINDOW_PLACE_AT | WINDOW_PLACE_SCREEN | WINDOW_PLACE_SIZE;
        place.x = x - launcher.screen[0], place.y = y - launcher.screen[1];
        place.width = (unsigned int)launcher.width, place.height = (unsigned int)launcher.height;
        place.screen = launcher.number;
        windows_place_set(launcher.own.table, launcher.own.row, &place);
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
        A program started apart: in a session of its own and a process nobody
        waits for, its input and output nowhere, as the machine script starts
        what is on the desktop. False when it could not be.
*/
static b32 launcher_start(string_address address_to words, positive count)
{
        bipolar child = system_fork();
        positive status = 0;

        if (child < 0)
                return false;
        if (child == 0)
        {
                bipolar quiet;

                if (system_fork())
                        system_call_1(syscall(exit_group), 0);
                system_call(syscall(setsid));
                quiet = system_open_at(AT_FDCWD, (string_address) "/dev/null", FILE_READ_WRITE | O_CLOEXEC);
                for (positive at = 0; quiet >= 0 && at < 3; at++)
                        system_call_3(syscall(dup3), (positive)quiet, at, 0);
                (void)shell_exec_file(words[0], words, count, window_asked_environment());
                system_call_1(syscall(exit_group), 127);
        }
        system_wait4_retry(child, address_of status, 0, null);
        return true;
}

// The row's start: false when nothing could be started, and the launcher stays up.
static b32 launcher_run(int row)
{
        string_address words[LAUNCHER_WORDS];
        char full[LAUNCHER_FULL];
        positive count;

        if (row < 0 || (positive)row >= launcher.entries)
                return false;
        count = launcher_words(&launcher.entry[row], words, full, sizeof(full));
        return launcher_start(words, count);
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

// A power button that acted (it was pressed again to confirm): started, and the bar goes. False when nothing could be started.
static b32 launcher_power_start(int which)
{
        string_address words[4];

        if (which < 0 || which >= LAUNCHER_POWERS)
                return false;
        launcher.power_runs++;
        return launcher_start(words, launcher_power_words(which, words));
}

/* ---- the field and the keys ---- */

// What the power row takes under the list: the kit's gap and a row.
static int launcher_power_block(void)
{
        return ui_px(&launcher.ui, UI_GAP2) + ui_px(&launcher.ui, UI_ROW);
}

// A new text: the list for it, from the top.
static fn launcher_changed(void)
{
        int height, was = launcher.height, most;

        launcher_search();
        height = ui_px(&launcher.ui, UI_PAD) * 2 + ui_px(&launcher.ui, UI_TARGET) +
                 (launcher.entries ? ui_px(&launcher.ui, UI_GAP2) + (int)launcher.entries * ui_px(&launcher.ui, UI_ROW) : 0) +
                 launcher_power_block();
        most = launcher_most_height();
        launcher.height = height > most ? most : height;
        // A bar above a bottom panel keeps its bottom where it is, so it is put again with the picture of its new height.
        launcher.moved |= launcher.anchored && launcher.shown >= 0 && launcher.edge == TASKBAR_BOTTOM && launcher.height != was;
        launcher.dirty = true;
}

static fn launcher_move(int way)
{
        int count = (int)launcher.entries;

        if (count)
                launcher.list.focus = (launcher.list.focus + way + count) % count;
        launcher.dirty = true;
}

// The field, made the first time it is wanted: one line of the engine, its clipboard this window's, the list's room.
static struct ui_text address_to launcher_field(void)
{
        if (!launcher.field.doc.selections && ui_field_open(&launcher.field, &launcher.own, ""))
                launcher.field.most = LAUNCHER_TEXT - 1, launcher.field.keeps_vertical = true; // Up and Down move the list
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
                return !launcher_run(launcher.list.focus);
        if (asks & UI_TEXT_CANCEL)
                return false;
        if (asks & UI_TEXT_CHANGED)
                launcher_take();
        if (asks)
                launcher.dirty = true;
        return true;
}

// A key: the field's first, and what it leaves the bar's, which moves in the list. False when it closes the bar.
static b32 launcher_key(const struct window_event address_to event)
{
        p32 asks = ui_buttons_event(&launcher.ui, &launcher.power, event, 0, 0);

        // The power buttons are the kit's row: what it takes of a key (Shift-Tab, and every key while it has the keys) is not the field's.
        if (asks & UI_BUTTONS_DRAW)
                launcher.dirty = true;
        if (asks & UI_BUTTONS_FIRED)
                return !launcher_power_start(launcher.power.fired);
        if (asks & UI_BUTTONS_TAKEN)
                return true;
        if (!(event->flags & WINDOW_KEY_DOWN))
                return true;
        if ((asks = ui_field_event(&launcher.ui, launcher_field(), &launcher.menu, event, 0, 0)))
                return launcher_asked(asks);
        if (event->a == UI_KEY_UP || event->a == UI_KEY_DOWN ||
            ((event->flags & WINDOW_KEY_CONTROL) && (event->a == UI_KEY_P || event->a == UI_KEY_N)))
                launcher_move(event->a == UI_KEY_UP || event->a == UI_KEY_P ? -1 : 1);
        else if (event->a == UI_KEY_TAB && launcher.entries)
        {
                // The highlighted name into the field, where it is a name.
                const struct launcher_entry address_to entry = &launcher.entry[launcher.list.focus];
                const char address_to name = entry->kind == LAUNCHER_PROGRAM ? launcher_name(entry->index)
                                             : entry->kind == LAUNCHER_WINDOW ? language_of(launcher_programs[entry->index].label)
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
        launcher_asked(ui_text_menu_do(&launcher.ui, launcher.menu.field, id));
}

static fn launcher_describe(int id, struct ui_row address_to row);

static fn launcher_damaged(int top, int bottom)
{
        (void)top, (void)bottom;
        launcher.dirty = true;
}

// The rows are the kit's list under the field: the field (a finger's target tall), the kit's gap, and a row for each entry.
static fn launcher_layout(void)
{
        struct ui address_to ui = &launcher.ui;
        int pad = ui_px(ui, UI_PAD);

        if (!launcher.list.ids)
        {
                launcher.list.ids = LAUNCHER_ROWS;
                launcher.list.describe = launcher_describe;
                launcher.list.damaged = launcher_damaged;
                launcher.list.hover = launcher.list.down = -1;
        }
        // The words are asked each time: a language chosen while the bar is up is the next picture's.
        for (int at = 0; at < LAUNCHER_POWERS; at++)
                ui_buttons_set(&launcher.power, at, language_text(launcher_powers[at].label), language_text(launcher_powers[at].says));
        launcher.list.count = (int)launcher.entries;
        for (int at = 0; at < launcher.list.count; at++)
                launcher.list.id[at] = (unsigned short)at;
        // The rows fill the window below the field; a list longer than the window scrolls (the wheel, and the keys reveal).
        {
                int top = pad + ui_px(ui, UI_TARGET) + ui_px(ui, UI_GAP2), rows = (int)launcher.entries * ui_px(ui, UI_ROW),
                    room = launcher.height - top - pad - launcher_power_block();

                ui_list_place(ui, &launcher.list, pad, top, launcher.width - 2 * pad,
                              launcher.height > 0 && rows > room ? (room > 0 ? room : 0) : rows);
        }
        ui_list_layout(ui, &launcher.list);
        ui_buttons_place(ui, &launcher.power, pad, launcher.height - pad - ui_px(ui, UI_ROW), launcher.width - 2 * pad);
}

static b32 launcher_event(const struct window_event address_to event)
{
        int row;
        p32 asks;

        launcher_layout();
        // The power buttons are the kit's row: its hover, its presses and releases, and a press anywhere else that drops what was
        // armed, before the field has the event; what the row takes of a pointer event is not the field's or the list's.
        if (event->type != WINDOW_EVENT_KEY)
        {
                asks = ui_buttons_event(&launcher.ui, &launcher.power, event, launcher.pointer_x, launcher.pointer_y);
                if (asks & UI_BUTTONS_DRAW)
                        launcher.dirty = true;
                if (asks & UI_BUTTONS_FIRED)
                        return !launcher_power_start(launcher.power.fired);
                if (asks & UI_BUTTONS_TAKEN)
                        return true;
        }
        // The field takes the pointer on its well (a caret, a drag, the middle button's paste, its menu) and the
        // clipboard's answers; the rest is the list's.
        // A press outside the field is the list's (the field's answer to it, commit or cancel, is the kit's and changes nothing here).
        if (event->type != WINDOW_EVENT_KEY &&
            (asks = ui_field_event(&launcher.ui, launcher_field(), &launcher.menu, event,
                                   event->type == WINDOW_EVENT_MOTION ? event->a : launcher.pointer_x,
                                   event->type == WINDOW_EVENT_MOTION ? event->b : launcher.pointer_y)) &&
            !(event->type == WINDOW_EVENT_BUTTON && (asks == UI_TEXT_COMMIT || asks == UI_TEXT_CANCEL)))
        {
                if (event->type == WINDOW_EVENT_MOTION)
                        launcher.pointer_x = event->a, launcher.pointer_y = event->b;
                return launcher_asked(asks);
        }
        switch (event->type)
        {
        case WINDOW_EVENT_KEY:
                return launcher_key(event);
        case WINDOW_EVENT_MOTION:
                row = ui_list_hit(&launcher.ui, &launcher.list, event->a, event->b);
                ui_list_pointer(&launcher.ui, &launcher.list, WINDOW_EVENT_MOTION, event->a, event->b, false);
                // The row the pointer is on is the highlighted one, where it has moved to it and not where a key put it.
                if (row >= 0 && (event->a != launcher.pointer_x || event->b != launcher.pointer_y) && row != launcher.list.focus)
                {
                        launcher.list.focus = row;
                        launcher.dirty = true;
                }
                launcher.pointer_x = event->a;
                launcher.pointer_y = event->b;
                // A finger moved off the slop is no press of the row it landed on.
                if ((event->flags & WINDOW_KEY_TOUCH) && launcher.press.down && !launcher.press.dragged &&
                    ui_press_move(&launcher.ui, &launcher.press, event->a, event->b))
                {
                        ui_list_let_go(&launcher.ui, &launcher.list);
                        launcher.dirty = true;
                }
                // A mouse held on a row shows it pressed while the pointer is on that row, and not once it slides off (the kit's).
                if (!(event->flags & WINDOW_KEY_TOUCH) && ui_list_press_slide(&launcher.ui, &launcher.list, &launcher.press, row))
                        launcher.dirty = true;
                break;
        case WINDOW_EVENT_WHEEL:
                // The list turns under the wheel with the kit's glide, as the settings list does: a notch is a row.
                if (ui_wheel_turn(&launcher.ui, &launcher.list.scroll, event))
                        launcher.dirty = true;
                break;
        case WINDOW_EVENT_BUTTON:
                if (event->a != UI_POINTER_LEFT)
                        break;
                row = ui_list_hit(&launcher.ui, &launcher.list, launcher.pointer_x, launcher.pointer_y);
                // A press is done where it went down, on its release: a finger's and a mouse's alike. The row it lands on is
                // shown pressed, and a press that slides off it (a finger past the slop, a mouse off the row) runs nothing.
                if (event->flags & WINDOW_KEY_DOWN)
                {
                        ui_press_begin(&launcher.press, launcher.pointer_x, launcher.pointer_y, row, (event->flags & WINDOW_KEY_TOUCH) != 0);
                        if (row >= 0)
                                launcher.list.focus = launcher.list.down = row;
                        launcher.dirty = true;
                        break;
                }
                ui_list_let_go(&launcher.ui, &launcher.list);
                launcher.dirty = true;
                if ((row = ui_press_end(&launcher.press, (event->flags & WINDOW_KEY_CANCEL) != 0)) >= 0 &&
                    row == ui_list_hit(&launcher.ui, &launcher.list, launcher.pointer_x, launcher.pointer_y))
                        return !launcher_run(row);
                break;
        }
        return true;
}

/* ---- the picture ---- */

// The label of a row, into into, and what is said at its right.
static fn launcher_label(const struct launcher_entry address_to entry, char address_to into, positive room,
                         const char address_to address_to note)
{
        *note = "";
        into[0] = 0;
        if (entry->kind == LAUNCHER_WINDOW)
        {
                string_copy_bounded((string_address)into, (string_address)language_of(launcher_programs[entry->index].label), room);
                *note = language_of("desktop");
        }
        else if (entry->kind == LAUNCHER_PROGRAM)
        {
                string_copy_bounded((string_address)into, (string_address)launcher_name(entry->index), room);
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
        int pad = ui_px(&launcher.ui, UI_PAD);

        ui_begin(ui, pixels, launcher.own.table->stride, launcher.width, launcher.height);

        // The field is the kit's, bare (no label): its well, and in it the engine's text, selection and caret, which
        // have the keys while the bar does.
        {
                struct ui_control address_to search = &launcher.search;

                if (!search->kind)
                {
                        search->kind = UI_INPUT, search->variant = UI_FIELD_BARE;
                        search->items[0] = WORDS(launcher_search_hint);
                        search->edit = launcher_field();
                }
                search->focused = (launcher.focused || !launcher.had_focus) && !launcher.power.keys;
                search->x = pad * 100 / ui->scale, search->y = pad * 100 / ui->scale, search->w = (launcher.width - 2 * pad) * 100 / ui->scale, search->h = UI_TARGET;
                ui_control_draw(ui, search);
        }
        // The rows: the kit's list, the highlighted one the row the keys are on (the keys themselves stay in the field).
        launcher_layout();
        launcher.list.lit = true;
        ui_list_draw(ui, &launcher.list);
        ui_buttons_draw(ui, &launcher.power, launcher.focused || !launcher.had_focus);
        // A finger's selection in the field: its handles and actions bar, over the rows and the buttons.
        ui_text_overlay(ui, launcher_field());
}

// One picture, into the buffer the compositor is not reading; false when there is none yet.
static b32 launcher_frame(void)
{
        struct windows address_to table = launcher.own.table;
        unsigned int row = launcher.own.row;
        int slot = launcher.shown < 0 ? 0 : 1 - launcher.shown;

        if (launcher.shown >= 0 && !windows_ready(&launcher.own))
                return false;

        launcher_draw(windows_buffer(table, row, (unsigned int)slot));
        table->width[row] = (unsigned int)launcher.width;
        table->height[row] = (unsigned int)launcher.height;
        table->damage_width[row] = 0;
        // A new height of a bar that keeps its bottom: its place, asked with the picture it is for, in one knock.
        if (launcher.moved)
        {
                launcher_place();
                __atomic_add_fetch(&table->moved[row], 1u, __ATOMIC_RELEASE);
                launcher.moved = false;
        }
        windows_show(&launcher.own, (unsigned int)slot);
        launcher.shown = slot;
        launcher.dirty = false;
        return true;
}

/* ---- the program ---- */

// The names of PATH, their directories, the hash table and the scan's buffer: one mapping, not in the image's bss.
static b32 launcher_memory(void)
{
        positive names = (positive)LAUNCHER_NAMES * LAUNCHER_NAME, from = LAUNCHER_NAMES,
                 hashed = (positive)LAUNCHER_HASH * sizeof(unsigned short);
        long mapped = ui_map(names + from + hashed + LAUNCHER_DIRENTS);

        if (mapped < 0 && mapped > -4096)
                return false;
        launcher.names = (char address_to)mapped;
        launcher.from = (unsigned char address_to)(mapped + names);
        launcher.hashed = (unsigned short address_to)(mapped + names + from);
        return true;
}

static fn launcher_size(unsigned int width, unsigned int height)
{
        struct windows address_to table = launcher.own.table;

        launcher.width = (int)(width > table->output_width ? table->output_width : width);
        launcher.height = (int)(height > table->output_height ? table->output_height : height);
        ui_scratch_fit(&launcher.ui, width);
        launcher.dirty = true;
}

/*
        For -a: the rectangle the compositor wrote when the panel's button was pressed, the screen it is on and the edge of it
        the panel is along, taken once. A table with no press in it (a width of 0), or a screen or an edge that is none, leaves
        the bar unanchored, in the middle.
*/
static fn launcher_anchor_take(void)
{
        struct windows address_to table = launcher.own.table;
        unsigned int count = __atomic_load_n(&table->outputs, __ATOMIC_ACQUIRE);

        if (!launcher.asked_beside || !table->button_w || !table->button_h || table->button_edge > TASKBAR_RIGHT ||
            table->button_screen >= (count ? count : 1))
                return;
        launcher.anchor[0] = table->button_x, launcher.anchor[1] = table->button_y;
        launcher.anchor[2] = (int)table->button_w, launcher.anchor[3] = (int)table->button_h;
        launcher.edge = table->button_edge, launcher.number = table->button_screen;
        windows_output(table, launcher.number, &launcher.screen[0], &launcher.screen[1], &launcher.screen[2], &launcher.screen[3]);
        launcher.anchored = true;
}

// The launcher's start, once its window is taken: the lift, the fonts and the first picture. -1 runs the loop.
// The search field keeps the hint it was set up with: a change of language sets it up again.
static fn launcher_language(void)
{
        launcher.search.kind = 0;
}

static int launcher_open(void)
{
        struct windows address_to table = launcher.own.table;
        unsigned int row = launcher.own.row;

        if (ui_lift_other(&launcher.own, (string_address) "launcher", launcher.toggle))
                return 0;

        if (!launcher_memory())
        {
                compositor_kmsg((string_address) "launcher: no memory, pid ",
                                (positive)compositor_call(syscall(getpid), 0, 0, 0, 0, 0, 0), (string_address) "");
                return 1;
        }

        launcher_size((unsigned int)ui_px(&launcher.ui, LAUNCHER_WIDTH), 0);
        launcher_anchor_take();
        launcher_changed();
        if (launcher.anchored)
                // Beside the panel's button, asked as a placement, which is read with the first picture.
                launcher_place();
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
        launcher_frame();

        // The first frame is up before PATH is read. The empty field names no program of PATH, so the picture does not
        // change with the scan and needs no second frame; a key read after this sees every name.
        language_listen(launcher_language);
        launcher_scan(launcher_path_variable());
        return -1;
}

// The keys going elsewhere (a click on the desktop, another window raised) puts the bar away; before they came here it had
// never had them.
static b32 launcher_focus(b32 focused)
{
        if (focused)
                launcher.had_focus = true;
        else if (launcher.had_focus)
                return false;
        return true;
}

// The caret's blink: the next frame it needs, and none once it has stopped (a while after the last key).
static long launcher_tick(long timeout, long address_to other)
{
        positive now = clock_monotonic_nanoseconds();
        b32 redraw = false;

        (void)other;
        timeout = ui_text_timeout(&launcher.ui, launcher_field(), timeout, now, &redraw);
        if (redraw)
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
        .frame = launcher_frame,
        .event = launcher_event,
        .focus = launcher_focus,
        .size = launcher_size,
        .tick = launcher_tick,
        .popup = &launcher.menu,
        .chose = launcher_chose,
};

static b32 launcher_main()
{
        string_address regular = (string_address) UI_FONT_REGULAR;

        memory_zero(&launcher, sizeof(launcher));
        launcher.shown = -1;

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
        launcher.regular = (const char address_to)regular;
        launcher_app.font = regular;
        return ui_app_run(&launcher_app);
}

#endif // MOONWATER_CANVAS_LAUNCHER
