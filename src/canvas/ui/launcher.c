/*
        The launcher, as a window on the compositor in userspace.

        A search bar in the middle of the screen: one field with the keys in it
        at once, and under it a short list of what the text names. The desktop's
        own programs come first (the settings, a terminal, the kernel log), then
        the programs found on PATH whose names start with or contain what was
        typed, the nearest names first, and last a row that runs the text itself
        as a shell command, so the bar is also a shell prompt.

        Up and Down (or Control-P and Control-N) move in the list, Tab takes the
        highlighted name into the field, and Enter runs the highlighted row:

            a desktop program    is started as a window of its own;
            a program of PATH    runs in a new terminal window, its words
                                 never seen by a shell;
            the command          is run by the machine's shell (shell -c) in a
                                 new terminal window.

        A terminal that was started for a program shows what it wrote and says
        how it ended, then stays until a key is pressed (terminal.c, -x). A text
        that is a command line (a blank, a pipe, a redirect, a quote, anything a
        shell reads as more than one plain word) or that no program is named
        by is what the highlighted row starts as, the command row, so Enter on it
        is the shell's; a plain word that names programs highlights the nearest.

        PATH is read once, when the bar opens (at most LAUNCHER_NAMES names, the
        first directory having a name keeping it), and every key only filters
        what was read: nothing is started for a key. Nothing here needs more than
        the rights the person has: a program is run as the launcher is.

        Escape, a press anywhere but on it (the desktop, a tab, another window:
        the compositor asks it to go, WINDOW_DISMISS) and losing the keys close
        it, as the same chord that opened it does (-x). It has no close button
        (WINDOW_NO_CLOSE). It is glass
        (window.c, WINDOW_GLASS), drawn with the system's pieces (ui.c) and
        nothing round them: the field is its text and a line under it, the
        rows are their text, and the row with the focus is lit. It is the one
        window of its program: a second start finds the first by the
        program's name and lifts it.

            launcher [-x] [-f FONT.ttf] [-m MONO.ttf] [-M MONOBOLD.ttf]

        -f is the face the bar is drawn in, -m the monospace one the terminals
        it starts are given, with -M their bold. -x closes the launcher that is
        up and ends when there is one, which is what the chord is: a press opens
        it and the next press puts it away.

        Dawn Larsson - Apache-2.0 license
        github.com/dawnlarsson/moonwater
*/
#ifndef MOONWATER_CANVAS_LAUNCHER
#define MOONWATER_CANVAS_LAUNCHER

#define LAUNCHER_WIDTH 560 // logical pixels, times the scale
#define LAUNCHER_FIELD 44  // the field's height
#define LAUNCHER_ROW 36    // a row's
#define LAUNCHER_GAP 8     // between the field and the list
#define LAUNCHER_ROWS 7    // rows the list shows at most
#define LAUNCHER_TEXT_SIZE 17        // the field's text, a little larger than a row's
#define LAUNCHER_TEXT 256  // bytes of the field
#define LAUNCHER_NAMES 4096
#define LAUNCHER_NAME 40
#define LAUNCHER_DIRS 16
#define LAUNCHER_PATH 512
#define LAUNCHER_HASH 8192
#define LAUNCHER_DIRENTS 16384
#define LAUNCHER_DIRENT_HEADER 19
#define LAUNCHER_WORDS 16
#define LAUNCHER_FULL 576

// What a row starts.
#define LAUNCHER_WINDOW 1u  // a program of the desktop: a window of its own
#define LAUNCHER_PROGRAM 2u // a program of PATH: in a terminal
#define LAUNCHER_COMMAND 3u // the text: the shell's, in a terminal

#define LAUNCHER_KEY_ESCAPE 1
#define LAUNCHER_KEY_BACKSPACE 14
#define LAUNCHER_KEY_TAB 15
#define LAUNCHER_KEY_W 17
#define LAUNCHER_KEY_U 22
#define LAUNCHER_KEY_P 25
#define LAUNCHER_KEY_ENTER 28
#define LAUNCHER_KEY_N 49
#define LAUNCHER_KEY_ENTER_PAD 96
#define LAUNCHER_KEY_UP 103
#define LAUNCHER_KEY_DOWN 108
#define LAUNCHER_BUTTON_LEFT 0x110

struct launcher_program
{
        const char address_to label;
        const char address_to words;   // what else names it, a blank apart
        const char address_to command; // the program, which takes -f for its face
        unsigned char mono;            // it is drawn in the monospace face
};

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

        char text[LAUNCHER_TEXT];
        struct launcher_entry entry[LAUNCHER_ROWS];
        positive entries;
        int highlight;

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

// How well a text names a desktop program: its label, or any of the words that also name it.
static unsigned int launcher_program_rank(const char address_to text, positive length,
                                          const struct launcher_program address_to program)
{
        const char address_to words = program->words;
        unsigned int best = launcher_rank(text, length, program->label, string_length((string_address)program->label));

        while (*words)
        {
                positive size = (positive)(string_first_of_or_end((string_address)words, ' ') - (string_address)words);
                unsigned int rank = launcher_rank(text, length, words, size);

                best = rank > best ? rank : best;
                words += size + (words[size] == ' ');
        }
        return best;
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

        launcher.highlight = 0;
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
                positive stop = at;

                while (launcher.path[stop] && launcher.path[stop] != ':')
                        stop++;
                if (stop > at)
                        launcher.dirs[launcher.dir_count++] = launcher.path + at;
                at = launcher.path[stop] ? stop + 1 : stop;
                launcher.path[stop] = 0;
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
                (void)shell_exec_file(words[0], words, count, program_environment_list());
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

/* ---- the field and the keys ---- */

// A new text: the list for it, from the top.
static fn launcher_changed(void)
{
        int height;

        launcher_search();
        height = ui_px(&launcher.ui, UI_PAD) * 2 + ui_px(&launcher.ui, LAUNCHER_FIELD) +
                 (launcher.entries ? ui_px(&launcher.ui, LAUNCHER_GAP) + (int)launcher.entries * ui_px(&launcher.ui, LAUNCHER_ROW) : 0);
        launcher.height = height > (int)launcher.own.table->output_height ? (int)launcher.own.table->output_height : height;
        launcher.dirty = true;
}

static fn launcher_move(int way)
{
        int count = (int)launcher.entries;

        if (count)
                launcher.highlight = (launcher.highlight + way + count) % count;
        launcher.dirty = true;
}

// A key: false when it closes the bar (or started what it was asked for).
static b32 launcher_key(const struct window_event address_to event)
{
        positive length = string_length((string_address)launcher.text);
        b32 control = (event->flags & WINDOW_KEY_CONTROL) != 0;

        if (!(event->flags & WINDOW_KEY_DOWN))
                return true;

        switch (event->a)
        {
        case LAUNCHER_KEY_ESCAPE:
                return false;
        case LAUNCHER_KEY_ENTER:
        case LAUNCHER_KEY_ENTER_PAD:
                return !launcher_run(launcher.highlight);
        case LAUNCHER_KEY_UP:
                launcher_move(-1);
                return true;
        case LAUNCHER_KEY_DOWN:
                launcher_move(1);
                return true;
        case LAUNCHER_KEY_TAB:
        {
                // The highlighted name, where it is a name.
                const struct launcher_entry address_to entry = &launcher.entry[launcher.highlight];
                const char address_to name = entry->kind == LAUNCHER_PROGRAM
                                                 ? launcher_name(entry->index)
                                             : entry->kind == LAUNCHER_WINDOW ? launcher_programs[entry->index].label
                                                                              : null;

                if (launcher.entries && name)
                {
                        string_copy_bounded((string_address)launcher.text, (string_address)name, sizeof(launcher.text));
                        launcher_changed();
                }
                return true;
        }
        case LAUNCHER_KEY_BACKSPACE:
                while (length && ((unsigned char)launcher.text[length - 1] & 0xc0) == 0x80)
                        length--;
                launcher.text[length ? length - 1 : 0] = 0;
                launcher_changed();
                return true;
        }

        if (control && (event->a == LAUNCHER_KEY_P || event->a == LAUNCHER_KEY_N))
        {
                launcher_move(event->a == LAUNCHER_KEY_P ? -1 : 1);
                return true;
        }
        if (control && event->a == LAUNCHER_KEY_U)
        {
                launcher.text[0] = 0;
                launcher_changed();
                return true;
        }
        if (control && event->a == LAUNCHER_KEY_W)
        {
                // The last word, and the blanks before it.
                while (length && launcher.text[length - 1] == ' ')
                        length--;
                while (length && launcher.text[length - 1] != ' ')
                        length--;
                launcher.text[length] = 0;
                launcher_changed();
                return true;
        }

        {
                unsigned int c = (unsigned int)event->b;
                p8 bytes[4];
                positive size;

                // A character is a printable one, no chord's; a blank is not the first thing in the field.
                if (c < ' ' || c == 0x7f || (event->flags & (WINDOW_KEY_CONTROL | WINDOW_KEY_ALT)) || (c == ' ' && !length))
                        return true;
                size = memory_utf8_encode(bytes, sizeof(bytes), c);
                if (!size || length + size >= sizeof(launcher.text))
                        return true;
                memory_copy(launcher.text + length, bytes, size);
                launcher.text[length + size] = 0;
                launcher_changed();
        }
        return true;
}

// The row under a point of the window, or -1.
static int launcher_hit(int x, int y)
{
        int top = ui_px(&launcher.ui, UI_PAD) + ui_px(&launcher.ui, LAUNCHER_FIELD) + ui_px(&launcher.ui, LAUNCHER_GAP);
        int row = (y - top) / ui_px(&launcher.ui, LAUNCHER_ROW);

        if (y < top || x < ui_px(&launcher.ui, UI_PAD) || x >= launcher.width - ui_px(&launcher.ui, UI_PAD) || row >= (int)launcher.entries)
                return -1;
        return row;
}

static b32 launcher_event(const struct window_event address_to event)
{
        switch (event->type)
        {
        case WINDOW_EVENT_KEY:
                return launcher_key(event);
        case WINDOW_EVENT_MOTION:
        {
                int row = launcher_hit(event->a, event->b);

                // The row the pointer is on is the highlighted one, where it has moved to it and not where a key put it.
                if (row >= 0 && (event->a != launcher.pointer_x || event->b != launcher.pointer_y) && row != launcher.highlight)
                {
                        launcher.highlight = row;
                        launcher.dirty = true;
                }
                launcher.pointer_x = event->a;
                launcher.pointer_y = event->b;
                break;
        }
        case WINDOW_EVENT_BUTTON:
                if ((event->flags & WINDOW_KEY_DOWN) && event->a == LAUNCHER_BUTTON_LEFT)
                {
                        int row = launcher_hit(launcher.pointer_x, launcher.pointer_y);

                        if (row >= 0)
                        {
                                launcher.highlight = row;
                                return !launcher_run(row);
                        }
                }
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
                string_copy_bounded((string_address)into, (string_address)launcher_programs[entry->index].label, room);
                *note = "desktop";
        }
        else if (entry->kind == LAUNCHER_PROGRAM)
        {
                string_copy_bounded((string_address)into, (string_address)launcher_name(entry->index), room);
                *note = launcher.dirs[launcher.from[entry->index]];
        }
        else
        {
                string_copy_bounded((string_address)into, (string_address) "Run  ", room);
                string_append_bounded((string_address)into, (string_address)launcher.text, room);
                *note = "shell";
        }
}

static fn launcher_draw(unsigned int address_to pixels)
{
        struct ui address_to ui = &launcher.ui;
        int pad = ui_px(&launcher.ui, UI_PAD), inset = ui_px(&launcher.ui, UI_INSET);
        int field = ui_px(&launcher.ui, LAUNCHER_FIELD), row = ui_px(&launcher.ui, LAUNCHER_ROW);
        int top = pad + field + ui_px(&launcher.ui, LAUNCHER_GAP), width = launcher.width - 2 * pad;

        ui_begin(ui, pixels, launcher.own.table->stride, launcher.width, launcher.height);

        // The field is its text, a caret and one line under it: no well and no ring.
        {
                int left = pad + inset, right = pad + width - inset, baseline = ui_baseline(ui, pad, field, LAUNCHER_TEXT_SIZE);

                ui_field_text(ui, left, right, baseline, (string_address)launcher.text, LAUNCHER_TEXT_SIZE, true);
                if (!launcher.text[0])
                        ui_text(ui, left + 3 * ui->scale, baseline, (string_address) "Search programs, or type a command", LAUNCHER_TEXT_SIZE,
                                false, ui->quiet, 255);
                ui_fill(ui, pad, pad + field, width, ui->scale, ui_tone(ui->ink, 26));
        }

        for (positive at = 0; at < launcher.entries; at++)
        {
                int y = top + (int)at * row, baseline = ui_baseline(ui, y, row, UI_TEXT);
                char label[LAUNCHER_TEXT + 8];
                const char address_to note;
                int note_width, right = launcher.width - pad - inset;

                launcher_label(&launcher.entry[at], label, sizeof(label), &note);
                note_width = ui_measure(ui, (string_address)note, UI_SMALL, false);
                if ((int)at == launcher.highlight)
                        ui_focus(ui, pad, y, width, row, false);
                ui_run(ui, pad + inset, baseline, (string_address)label, UI_TEXT, false, ui->ink, 255, pad + inset,
                       right - note_width - inset);
                ui_text(ui, right - note_width, ui_baseline(ui, y, row, UI_SMALL), (string_address)note, UI_SMALL, false,
                        ui->quiet, 255);
        }
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
        windows_show(&launcher.own, (unsigned int)slot);
        launcher.shown = slot;
        launcher.dirty = false;
        return true;
}

/* ---- the program ---- */

// Whether a process is this program: its name.
static b32 launcher_is(unsigned int pid)
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
        return !memory_compare(name, "launcher\n", 9);
}

/*
        Another launcher up -- one that has shown a picture -- is lifted, or
        with toggle put away (it is ended, and the compositor sees its door
        close as it does any program's end); and this one goes: true when there
        was one.
*/
static b32 launcher_other(b32 toggle)
{
        struct windows address_to table = launcher.own.table;

        for (unsigned int row = 0; row < WINDOWS; row++)
        {
                unsigned int pid = __atomic_load_n(&table->pid[row], __ATOMIC_ACQUIRE);
                char name[48];
                bipolar door;

                if (row == launcher.own.row || !pid || !__atomic_load_n(&table->frame[row], __ATOMIC_ACQUIRE) ||
                    !launcher_is(pid))
                        continue;

                if (toggle)
                {
                        system_call_2(syscall(kill), (positive)pid, SIGTERM);
                        return true;
                }
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

// The launcher's start, once its window is taken: the lift, the fonts and the first picture. -1 runs the loop.
static int launcher_open(void)
{
        struct windows address_to table = launcher.own.table;
        unsigned int row = launcher.own.row;

        if (launcher_other(launcher.toggle))
                return 0;

        if (!launcher_memory())
        {
                compositor_kmsg((string_address) "launcher: no memory, pid ",
                                (positive)compositor_call(syscall(getpid), 0, 0, 0, 0, 0, 0), (string_address) "");
                return 1;
        }

        launcher_size((unsigned int)ui_px(&launcher.ui, LAUNCHER_WIDTH), 0);
        launcher_changed();
        table->x[row] = ((int)table->output_width - launcher.width) / 2;
        table->y[row] = (int)table->output_height / 3 - ui_px(&launcher.ui, UI_PAD) - ui_px(&launcher.ui, LAUNCHER_FIELD) / 2;
        launcher_frame();

        // The first frame is up before PATH is read. The empty field names no program of PATH, so the picture does not
        // change with the scan and needs no second frame; a key read after this sees every name.
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

static struct ui_app launcher_app = {
        .own = &launcher.own,
        .ui = &launcher.ui,
        .missing = (string_address) "launcher: no window: the compositor's desktop is not up\n",
        .title = (string_address) "Launcher",
        .glass = WINDOW_GLASS | WINDOW_NO_CLOSE | WINDOW_DISMISS,
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
};

static b32 launcher_main()
{
        string_address regular = (string_address) UI_FONT_REGULAR;

        memory_zero(&launcher, sizeof(launcher));
        launcher.shown = -1;

        for (positive at = 1; at < (positive)program_argument_count(); at++)
        {
                string_address option = program_argument(at);

                if (option[0] != '-' || !option[1] || option[2])
                        continue;
                if (option[1] == 'x')
                        launcher.toggle = true;
                else if (at + 1 < (positive)program_argument_count() && option[1] == 'f')
                        regular = program_argument(++at);
                else if (at + 1 < (positive)program_argument_count() && option[1] == 'm')
                        launcher.mono = (const char address_to)program_argument(++at);
                else if (at + 1 < (positive)program_argument_count() && option[1] == 'M')
                        launcher.mono_bold = (const char address_to)program_argument(++at);
        }
        launcher.regular = (const char address_to)regular;
        launcher_app.font = regular;
        return ui_app_run(&launcher_app);
}

#endif // MOONWATER_CANVAS_LAUNCHER
