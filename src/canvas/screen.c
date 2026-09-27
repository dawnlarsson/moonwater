/*
        The commands that draw.

        Each was its own program and each is a window on the compositor: a
        terminal, a field of colour, a page of text, a report on how long the
        pointer takes to move. They are together because they want the same
        thing -- src/canvas/window.c, the client side of Canvas -- and that is
        included once, above.

        A window that cannot be opened is not an error worth a special path:
        every one of these says so and returns, which is what running them on
        a machine with no compositor does.
*/

/*
        What the far end's line discipline is set to.

        The terminal edits the line being typed rather than sending it a
        character at a time, and that is only right while the program on the
        other end is asking for whole lines. ICANON is the question and this
        is how it is asked; ECHO has to go off with it, or every character
        appears twice -- once where the editor drew it and once where the
        kernel echoed it back.
*/
#define TERMINAL_CANONICAL 0x0002u
#define TERMINAL_ECHO 0x0008u


/*
        What a write did to the queued bytes.

        The pty is nonblocking, so a successful write may consume only a
        prefix and EAGAIN may consume nothing. Keep both cases queued for the
        next pass through the event loop. Any other result means the pty can
        no longer make progress and lets the caller end the session instead
        of spinning on it.
*/
static b32 term_sent(bipolar wrote)
{
        if (wrote > 0)
        {
                positive taken = (positive)wrote < to_shell_length
                                     ? (positive)wrote
                                     : to_shell_length;

                memory_copy(to_shell, to_shell + taken,
                            to_shell_length - taken);
                to_shell_length -= taken;
                return true;
        }

        return wrote == -EAGAIN || wrote == -EINTR;
}

// What the emulator has to say, on its way. A keystroke at a time, so a line
// long enough to fill the buffer cannot be cut in half by the next one.
static b32 term_send(b32 master)
{
        if (!to_shell_length)
                return true;

        return term_sent(system_write_once(master, to_shell, to_shell_length));
}

static fn term_follow_modes(b32 master)
{
        terminal_modes modes;

        if (system_control(master, PTY_TCGETS, address_of modes) != 0)
                return;

        term_line_editing((modes.behaviour & TERMINAL_CANONICAL) != 0);

        // Left off when the far end goes raw. What it turns off for itself is
        // its own to turn back on, and doing that from here would be echoing
        // into a program that had just asked for silence.
        if (!line_editing || !(modes.behaviour & TERMINAL_ECHO))
                return;

        modes.behaviour &= ~TERMINAL_ECHO;
        system_control(master, PTY_TCSETS, address_of modes);
}

/*
        Which of the window and its shell the kernel takes first.

        When memory runs out the kernel kills whichever process holds the most
        pages, and the ring this window draws from is pages its shell does not
        hold -- so once apt and dpkg had gone, on a live session whose root is
        RAM that stays full, the window was next, ahead of the shell that was
        using the memory. openssh's listener does the same for itself. Half of
        memory is taken off what the window seems to hold rather than exempting
        it, so a window that is itself the leak still goes.

        The shell is put back to nought before it runs, or everything it starts
        would inherit the window's protection. Not being allowed to change it
        -- a window run by someone without the privilege -- changes nothing.
*/
#define TERM_OOM_WINDOW "-500\n"
#define TERM_OOM_SHELL "0\n"

static fn term_oom_adjust(string_address value)
{
        bipolar handle = system_open_at(AT_FDCWD,
                                        (string_address) "/proc/self/oom_score_adj",
                                        FILE_WRITE | O_CLOEXEC);

        if (handle < 0)
                return;

        (void)system_write_all((positive)handle, value, string_length(value));
        system_close(handle);
}

/*
        How many processes the kernel has killed for want of memory, or
        positive_max when it will not say. A count that moved while the shell
        ran says memory ran out, not that the shell was the one chosen: it
        counts every process.
*/
static positive term_oom_kills(void)
{
        static p8 text[16384];
        bipolar got = file_slurp_once_at(AT_FDCWD,
                                         (string_address) "/proc/vmstat", text,
                                         sizeof(text));

        positive held = got > 0 ? (positive)got : 0;

        // The name and its digits both have to fit in what was read: the
        // compare walks nine bytes and the digits walked until one was not
        // one, which off the end of a full buffer is a read past it.
        p8 address_to line = held >= 9 && !memory_compare(text, "oom_kill ", 9)
                                 ? text
                                 : memory_search(text, held, "\noom_kill ", 10);

        if (!line)
                return positive_max;

        line += 9 + (line[0] == '\n');

        return string_digits_max(line, held - (positive)(line - text), null);
}

/*
        The shell went by a signal this window did not send.

        A window that closed then could not be told from one that crashed, and
        an out-of-memory kill on a live session looked exactly like a crash. So
        the screen is put back the way a person reads it -- off the alternate
        screen, out of any frame a program was holding, without the line being
        typed -- and DECSTR takes back whatever else a dead full-screen program
        left set: colours, a scroll region the line below would scroll inside,
        a hidden cursor, line drawing in G0 that spells the words in box
        glyphs, insert mode, autowrap and origin mode, mouse reports. What
        happened is said after whatever was on the screen. Nothing is sent:
        the pty is already gone.
*/
static fn term_child_killed(positive signal, string_address name,
                            b32 out_of_memory)
{
        static const p8 restore[] = "\x1b[?2026l\x1b[?1049l\x1b[!p\r\n";
        p8 line[96];
        positive at;

        term_line_editing(false);
        term_bytes(restore, sizeof(restore) - 1);

        string_copy(line, (string_address) "[shell killed by signal ");
        at = string_length(line);
        at += positive_into_string(line + at, signal);

        // A signal with a name says it; one known only by its number has
        // already said everything there is.
        if (name[0] < '0' || name[0] > '9')
        {
                string_copy(line + at, (string_address) " (SIG");
                at += 5;
                string_copy_max_end(line + at, name, 16);
                at += string_length(line + at);
                line[at++] = ')';
        }

        if (out_of_memory)
        {
                string_copy(line + at, (string_address) ": out of memory");
                at += 15;
        }

        line[at++] = ']';
        term_bytes(line, at);
}

/*
        What is left of a window whose shell is gone: its cells, until the X.
        Keys and the pointer are taken and dropped, since nothing is there to
        send them to, and a resize is still drawn. It sleeps in between: the
        compositor wakes it for a key, a new grid and the close request.
*/
static fn term_linger(void)
{
        struct window_key dropped;

        cursor_show();
        window_damage(window, 0, ROWS);
        window_flush(window);

        while (!window_closing(window))
        {
                while (window_key(window, address_of dropped))
                        ;

                if (window->columns != COLUMNS || window->rows != ROWS)
                {
                        regrid(-1);
                        window_damage(window, 0, ROWS);
                        window_flush(window);
                }

                window_wait(window, -1, -1);
        }
}

// term ---------------------------------------------------------
static b32 screen_term()
{
        claim_standard_descriptors();

        window = window_open_text(COLUMNS_WANTED, ROWS_WANTED);

        if (!window)
        {
                log_direct(str("term: no window\n"));
                return 1;
        }

        // Named, because the compositor puts a window of its own beside this
        // one and two untitled terminals are a guessing game.
        string_copy((string_address)window->title, (string_address) "shell");

        grid_take();

        full_reset();

        /*
                Waiting for the other end to exist.

                The compositor starts this the moment it has a screen, which
                is before init has mounted devpts -- there is no ordering
                between the two and there does not need to be, so long as this
                is willing to wait a moment for it.

                Two milliseconds a try, for the same four seconds. The first
                terminal is started as the initcalls finish and init mounts
                devpts a few milliseconds later, so this always waits, and
                twenty milliseconds a try left it asleep long after the mount:
                its first frame came 18 ms later at the median of ten paired
                boots. A failed open of /dev/ptmx is one system call.
        */
        timespec wait = {0, 2000000};
        b32 master = -1;
        b32 slave = -1;

        for (int tries = 0; tries < 2000 && master < 0; tries++)
        {
                if (process_pty_open(address_of master, address_of slave,
                                     true) < 0)
                        master = -1;

                if (master < 0)
                        system_call_2(syscall(nanosleep), (positive)address_of wait, 0);
        }

        if (master < 0)
        {
                log_direct(str("term: no /dev/ptmx\n"));
                window_close(window);
                return 1;
        }

        grid_tell(master);

        terminal_terminfo_install();

        // Here and not sooner: the kernel mounts /proc before init mounts the
        // devpts the pty above waited for.
        term_oom_adjust(TERM_OOM_WINDOW);

        positive oom_kills = term_oom_kills();
        bipolar child = system_fork();

        if (child < 0)
        {
                system_close(slave);
                system_close(master);
                window_close(window);
                log_direct(str("term: cannot fork\n"));
                return 1;
        }

        if (child == 0)
        {
                string_address argv[] = {SHELL, null};
                string_address seed[] = {
                    "TERM=" TERM_NAME,
                    "TERMINFO=" TERM_INFO_DIRECTORY,
                    "HOME=/root",
                    "PATH=" BOWL_DEFAULT_PATH,
                    "LANG=C.UTF-8",
                    null};

                if (process_pty_child_setup(master, slave, -1, -1) < 0)
                        system_call_1(syscall(exit), 126);

                // A fork shares no memory, so this is the shell's alone.
                term_oom_adjust(TERM_OOM_SHELL);

                //      Boot may still be looking for an installed disk, and
                //      may have a question for whoever is at this window.
                host_terminal_opening();

                bowl_session_prepare("/root", null);
                (void)shell_exec_file((string_address)SHELL, argv, 1,
                                      bowl_environment(seed));
                system_call_1(syscall(exit), 127);
        }

        system_close(slave);

        // Centred, and published once below with its cursor already in it.
        window->region = WINDOW_CENTRED;

        // As much as the far end's line discipline hands over in one read,
        // which is its four kilobyte buffer: a quarter of that was four
        // reads, four wakeups of the writer and four passes of the loop
        // below for every one the pty needed.
        p8 from_shell[4096];
        struct window_key typed[WINDOW_KEYS];
        timespec nap = {0, 4000000};
        unsigned int synchronized_wait = 0;
        b32 hung_up = false;
        unsigned int focused = window->state & WINDOW_FOCUSED;

        cursor_show();
        window_damage(window, 0, ROWS);
        window_commit(window);

        for (;;)
        {
                b32 changed = false;
                b32 gone = false;
                unsigned int keys = 0;

                /* Damage survives loop boundaries while mode 2026 holds a
                   frame. Without this, suppressing a flush also forgot the
                   rows that the eventual atomic flush had to publish. */
                if (!synchronized_output)
                {
                        touched_top = ROWS;
                        touched_bottom = 0;
                }

                if (window->columns != COLUMNS || window->rows != ROWS)
                        regrid(master);

                term_follow_modes(master);

                /*
                        Taken out of the ring first, drawn second.

                        The cursor is a cell with its colours the wrong way
                        round, so it has to come off before anything writes
                        where it is -- and taking it off marks a row changed
                        whether or not one was. Reading the keys before
                        deciding is how the loop can tell the difference
                        between a keystroke and four milliseconds passing.
                */
                while (keys < WINDOW_KEYS && window_key(window, address_of typed[keys]))
                        keys++;

                if (keys)
                        cursor_hide();

                for (unsigned int i = 0; i < keys; i++)
                {
                        if (typed[i].flags & WINDOW_KEY_POINTER)
                        {
                                term_pointer(typed[i].reserved & 0xffff,
                                             typed[i].reserved >> 16,
                                             typed[i].character,
                                             typed[i].flags);

                                if (!term_send(master))
                                {
                                        gone = true;
                                        break;
                                }

                                continue;
                        }

                        if (typed[i].flags & WINDOW_KEY_DOWN)
                        {
                                term_key_modified(typed[i].character,
                                                  typed[i].code,
                                                  typed[i].flags);

                                if (!term_send(master))
                                {
                                        gone = true;
                                        break;
                                }
                        }
                }

                {
                        unsigned int now = window->state & WINDOW_FOCUSED;

                        if (now != focused)
                        {
                                focused = now;
                                term_focus(now != 0);
                                if (!term_send(master))
                                        gone = true;
                        }
                }

                for (;;)
                {
                        bipolar got = system_read_once(master, from_shell,
                                                       sizeof(from_shell));

                        if (got > 0)
                        {
                                if (!changed)
                                        cursor_hide();

                                term_bytes(from_shell, (positive)got);

                                changed = true;
                                continue;
                        }

                        /*
                                Nothing waiting is EAGAIN and only EAGAIN.

                                Everything else is the shell gone -- zero, or
                                EIO once the session went with it -- and
                                reading that as nothing waiting left this
                                spinning on a dead pty forever, with a zombie
                                behind it and a window nothing could dismiss.
                        */
                        if (got != -EAGAIN && got != -EINTR)
                                gone = true;

                        break;
                }

                // A report the far end asked for, which is bytes going the
                // way keys go.
                if (!gone && !term_send(master))
                        gone = true;

                /*
                        The X in the titlebar.

                        A terminal window closing is its line hanging up, and
                        SIGHUP is what that has always been: the shell runs
                        whatever it has for one, its children get the same,
                        and the pty then reads EIO so the ordinary way out
                        below does the rest -- there is no second path here
                        that has to be kept working.

                        To the group, because the shell is a session leader on
                        this pty and the job it is running is the reason to
                        hang up at all. Once, because the compositor leaves the
                        request set for as long as the window exists, and a
                        signal every four milliseconds is not asking twice.
                */
                if (!gone && !hung_up && window_closing(window))
                {
                        system_call_2(syscall(kill), (positive)(-child), SIGHUP);
                        hung_up = true;
                }

                // The line editor draws where the shell would have echoed, so
                // what it touched is what says the screen changed.
                if (touched_bottom > touched_top)
                        changed = true;

                /*
                        Only when something actually arrived.

                        Taking the cursor off and putting it back marks a row
                        changed whether or not anything did, and asking for a
                        redraw every four milliseconds because of that is a
                        whole screen recomposed two hundred and fifty times a
                        second for nothing.
                */
                /* A broken or killed client must not freeze its last complete
                   screen forever. Counting the existing 4 ms loop is cheaper
                   than adding a clock syscall; fifty turns are a 200 ms
                   safety ceiling and ordinary dashboard frames finish in
                   roughly one twentieth of it. */
                if (synchronized_output)
                {
                        synchronized_wait++;

                        if (gone || synchronized_wait >= 50)
                        {
                                synchronized_output = false;
                                changed = touched_bottom > touched_top;
                        }
                }
                else
                        synchronized_wait = 0;

                if (changed && !synchronized_output)
                {
                        cursor_show();

                        if (touched_bottom > touched_top)
                        {
                                window_damage(window, touched_top,
                                              touched_bottom - touched_top);
                                window_flush(window);
                        }
                }

                if (gone)
                        break;

                /*
                        Asleep until a key, a resize or the shell's output,
                        rather than for four milliseconds at a time. The nap
                        stays only while a synchronized frame is being held,
                        where the turns of this loop are what counts the
                        200 ms ceiling above.
                */
                window_wait(window, master,
                            synchronized_output ? (long)nap.tv_nsec : -1);
        }

        positive status = 0;

        system_wait4_retry(child, address_of status, 0, null);
        system_close(master);

        // An exit of any status, and the hangup the X sent, close the window
        // as they always have. Only a signal from elsewhere keeps it.
        positive signal = status & 0x7f;

        if (!hung_up && signal && signal != 0x7f)
        {
                p8 name[16];
                positive kills = term_oom_kills();

                kill_name(signal, name);
                cursor_hide();
                term_child_killed(signal, name,
                                  oom_kills != positive_max &&
                                      kills != positive_max &&
                                      kills != oom_kills);
                term_linger();
        }

        window_close(window);

        return 0;
}

// window ---------------------------------------------------------
// Colours in no compositor palette, so a screenshot can tell them apart.
#define INK_BACK 0x00ff9900
#define INK_FRONT 0x000066cc
#define INK_BARE 0x0022bb55

static void fill(struct window *window, unsigned int colour)
{
        unsigned int *pixels = window_pixels(window);

        for (unsigned int y = 0; y < window->height; y++)
                memory_fill_u32(pixels + y * window->pitch,
                                window->width, colour);
}

static void hold(long seconds, long nanoseconds)
{
        long timespec[2] = {seconds, nanoseconds};

        system_call_2(syscall(nanosleep), (positive)timespec, 0);
}

static b32 screen_window()
{
        struct window *back = window_open(400, 260);
        struct window *front = window_open(200, 120);
        struct window *bare = window_open(160, 100);

        if (!back || !front || !bare)
        {
                if (bare)
                        window_close(bare);
                if (front)
                        window_close(front);
                if (back)
                        window_close(back);
                log_direct(str("no window\n"));
                return 1;
        }

        fill(back, INK_BACK);
        back->region = WINDOW_CENTRED;
        back->edge = 16;
        string_copy_max_end((p8 address_to)back->title,
                            (string_address) "Centred, rounded",
                            WINDOW_TITLE_MAX - 1);
        window_commit(back);

        fill(front, INK_FRONT);
        front->x = back->x + 60;
        front->y = back->y + 60;
        string_copy_max_end((p8 address_to)front->title,
                            (string_address) "On top", WINDOW_TITLE_MAX - 1);
        window_commit(front);

        // No frame, so no titlebar, no border, and nothing to drag it by.
        fill(bare, INK_BARE);
        bare->style = 0;
        bare->x = 100;
        bare->y = 620;
        window_commit(bare);

        // Show what a keyboard says, in the titlebar of the window that has
        // focus. Nothing is polled: keys arrive in the page.
        for (int i = 0; i < 240; i++)
        {
                struct window_key key;
                unsigned int typed = 0;
                char line[] = "typed:  ";

                while (window_key(back, &key))
                        if ((key.flags & WINDOW_KEY_DOWN) && key.character >= ' ')
                                typed = key.character;

                if (typed)
                {
                        line[sizeof("typed: ") - 1] = (char)typed;
                        string_copy_max_end((p8 address_to)back->title,
                                            (string_address)line,
                                            WINDOW_TITLE_MAX - 1);
                        window_commit(back);
                }

                hold(0, 25000000);
        }

        // Put one away and let the other cover its display.
        front->style |= WINDOW_MINIMIZED;
        window_commit(front);

        bare->style |= WINDOW_FULLSCREEN;
        window_commit(bare);

        hold(7, 0);

        window_close(bare);
        window_close(front);
        window_close(back);
        return 0;
}

// text ---------------------------------------------------------
// A window of text: the program writes cells, Canvas draws the glyphs.
//
// The size is the compositor's, not this program's: dragging an edge changes
// how many cells there are, and the layout is done again at whatever it
// becomes. window_grid is what says the cells are in that shape now.
#define TEXT_COLUMNS_WANTED 60
#define TEXT_ROWS_WANTED 18
#define TYPED_MAX 46

static unsigned int columns, rows;
static char typed[TYPED_MAX];
static unsigned int at;

// The rows are the last lines of the ring, the same as any window of cells.
#define text_row(row) window_line(window, window->head - rows + (row))

static void text_write(unsigned int row, unsigned int column,
                       unsigned int character, unsigned char ink,
                       unsigned char paper)
{
        struct window_cell *cell = text_row(row) + column;
        unsigned int *length = window_length(window, window->head - rows + row);

        // Every cell up to this one has to exist, because nothing past the
        // length of a line is drawn.
        while (*length < column)
        {
                struct window_cell *blank = text_row(row) + *length;

                blank->character = ' ';
                blank->ink = 7;
                blank->paper = 0;
                *length += 1;
        }

        cell->character = character;
        cell->ink = ink;
        cell->paper = paper;

        if (*length < column + 1)
                *length = column + 1;
}

static void say(unsigned int row, unsigned int column, const char *text,
                unsigned char ink, unsigned char paper)
{
        unsigned int i;

        if (row >= rows)
                return;

        for (i = 0; text[i] && column + i < columns; i++)
                text_write(row, column + i, (unsigned char)text[i], ink, paper);

        window_damage(window, row, 1);
}

static void lay_out(void)
{
        unsigned int row;

        window_widen(window, window->columns);
        columns = window->columns < window->stride ? window->columns
                                                   : window->stride;
        rows = window->rows;

        // Emptying a line is its length, not its cells.
        for (row = 0; row < rows; row++)
                *window_length(window, window->head - rows + row) = 0;

        for (unsigned int i = 0; i < 8; i++)
                say(1, 2 + i * 6, "colour", (unsigned char)(8 + i), 0);

        say(3, 2, "Moonwater Canvas draws these glyphs.", 15, 0);
        say(4, 2, "This window is cells, not pixels.", 7, 0);
        say(6, 2, "Type here:", 11, 0);

        for (unsigned int i = 0; i < at; i++)
                if (rows > 6 && 13 + i < columns)
                        text_write(6, 13 + i, (unsigned char)typed[i], 15, 4);

        window_grid(window, columns, rows);
        window_damage(window, 0, rows);
}


static b32 screen_text()
{
        window = window_open_text(TEXT_COLUMNS_WANTED, TEXT_ROWS_WANTED);

        if (!window)
        {
                log_direct(str("no window\n"));
                return 1;
        }

        lay_out();
        window->region = WINDOW_CENTRED;
        window_commit(window);

        for (int tick = 0; tick < 800; tick++)
        {
                struct window_key key;
                b32 changed = 0;

                if (window_regrid(window))
                {
                        lay_out();
                        changed = 1;
                }

                while (window_key(window, &key))
                {
                        if (!(key.flags & WINDOW_KEY_DOWN) || key.character < ' ')
                                continue;

                        if (at < TYPED_MAX && rows > 6 && 13 + at < columns)
                        {
                                text_write(6, 13 + at, key.character, 15, 4);
                                typed[at++] = (char)key.character;
                                window_damage(window, 6, 1);
                                changed = 1;
                        }
                }

                // Now, not on the compositor's clock: a letter that has been
                // typed should not wait for a frame it has no reason to be on.
                if (changed)
                        window_flush(window);

                hold(0, 10000000);
        }

        window_close(window);
        return 0;
}

/*
        The Spark device, for a report. A machine without the module says so
        and gets no report.
*/
static b32 spark_report_open()
{
        b32 device = system_open_at(AT_FDCWD, SPARK_DEVICE,
                                    FILE_READ_WRITE | O_CLOEXEC);

        if (device < 0)
        {
                string_format(log, "cannot open %s: %b\n", SPARK_DEVICE, device);
                log_flush();
        }

        return device;
}

// One set of its counters, or a said reason why not.
static b32 spark_report_read(b32 device, positive request, address_any into,
                             string_address what)
{
        if (system_control(device, request, into) == 0)
                return true;

        string_format(log, "could not read %s stats\n", what);
        return false;
}

static b32 spark_report_close(b32 device, b32 result)
{
        system_close(device);
        log_flush();
        return result;
}

// spawn -----------------------------------------------------------
/*
        What the kernel spends starting a program, per phase.

        The module has kept these counters since the spark loader was
        written and nothing has ever read them, so the cost of a spawn has
        only ever been visible from outside as wall clock. The phases are
        nested: task is user_mode_thread, exec is the whole of
        kernel_execve, loader is our binary format handler inside it, and
        map is the three region mappings inside that. What exec has that
        loader does not is the generic prologue -- the bprm, the path walk
        of the image, and the argument stack -- which is where the work
        that is left to remove lives.
*/
static b32 screen_spawn()
{
        b32 device = spark_report_open();
        struct stats stats;
        positive spawns;

        if (device < 0)
                return 1;

        if (!spark_report_read(device, SPARK_IOCTL_STATS, address_of stats,
                               "spawn"))
                return spark_report_close(device, 1);

        spawns = stats.spawns ? stats.spawns : 1;

        string_format(log, "spawns           %p\n", stats.spawns);
        string_format(log, "loads            %p\n", stats.loads);
        string_format(log, "  task           %p ns each\n", stats.task_ns / spawns);
        string_format(log, "  exec           %p ns each\n", stats.exec_ns / spawns);
        string_format(log, "    loader       %p ns each\n", stats.loader_ns / spawns);
        string_format(log, "      mapping    %p ns each\n", stats.map_ns / spawns);

        // What kernel_execve did around our handler: the bprm, opening the
        // image, and the argument stack it builds and then moves.
        if (stats.exec_ns > stats.loader_ns)
                string_format(log, "    prologue     %p ns each\n",
                              (stats.exec_ns - stats.loader_ns) / spawns);

        if (stats.loader_ns > stats.map_ns)
                string_format(log, "    loader rest  %p ns each\n",
                              (stats.loader_ns - stats.map_ns) / spawns);

        string_format(log, "totals           %p ns task, %p ns exec\n",
                      stats.task_ns, stats.exec_ns);
        return spark_report_close(device, 0);
}

// pointer ---------------------------------------------------------
// Reports how long the kernel takes from a pointer event arriving to the
// cursor being on screen. Move the mouse, then run this.
static b32 screen_pointer()
{
        b32 device = spark_report_open();
        struct input_stats stats;
        struct cursor_stats cursor;

        if (device < 0)
                return 1;

        if (!spark_report_read(device, SPARK_IOCTL_INPUT_STATS,
                               address_of stats, "input") ||
            !spark_report_read(device, SPARK_IOCTL_CURSOR_STATS,
                               address_of cursor, "cursor"))
                return spark_report_close(device, 1);

        // Drawing happens whether or not anything has touched the mouse.
        string_format(log, "composes         %p\n", stats.composes);
        string_format(log, "compose ns       %p\n", stats.compose_ns);
        string_format(log, "pixels painted   %p\n", stats.painted);
        string_format(log, "runs             %p\n", stats.runs);
        string_format(log, "driver ns        %p\n", stats.driver_ns);
        string_format(log, "text ns          %p\n", stats.text_ns);
        string_format(log, "cursor planes    %p active, %p shown\n",
                      (positive)cursor.active, (positive)cursor.shown);
        string_format(log, "cursor plane io  %p updates, %p failures\n",
                      cursor.updates, cursor.failures);
        string_format(log, "cursor sync      %p requested, %p armed\n",
                      cursor.requested_generation, cursor.armed_generation);
        string_format(log, "cursor requested %b,%b armed %b,%b\n",
                      (bipolar)cursor.requested_x, (bipolar)cursor.requested_y,
                      (bipolar)cursor.armed_x, (bipolar)cursor.armed_y);
        string_format(log, "cursor outputs   %p wanted, recovery %p\n",
                      (positive)cursor.wanted, (positive)cursor.recovering);

        // moonwater.cursor_plane, so a cursor drawn in software says whether
        // it was asked for or the plane was lost.
        {
                p8 knob[8];
                bipolar got = file_slurp_once_at(AT_FDCWD,
                    (string_address) "/sys/module/moonwater/parameters/cursor_plane",
                    knob, sizeof(knob) - 1);

                if (got > 0)
                {
                        knob[knob[got - 1] == '\n' ? got - 1 : got] = 0;
                        string_format(log, "cursor plane knob %s\n",
                                      (string_address)knob);
                }
        }

        // Every device the kernel attached, so a mouse that is silent can be
        // told from one whose reports the cursor did not follow.
        struct input_devices devices;

        if (system_control(device, SPARK_IOCTL_INPUT_DEVICES,
                          address_of devices) == 0)
        {
                positive listed = devices.count < INPUT_DEVICES_MAX
                                      ? devices.count : INPUT_DEVICES_MAX;

                string_format(log, "input devices    %p\n", devices.count);

                for (positive i = 0; i < listed; i++)
                {
                        if (devices.device[i].opened)
                                string_format(log, "  %s: would not open (%b)\n",
                                              devices.device[i].name,
                                              (bipolar)devices.device[i].opened);
                        else
                                string_format(log, "  %s: %p reports\n",
                                              devices.device[i].name,
                                              devices.device[i].events);
                }
        }

        if (!stats.events)
        {
                string_format(log, "no pointer movement seen yet\n");
                return spark_report_close(device, 0);
        }

        string_format(log, "pointer events   %p\n", stats.events);
        string_format(log, "event to screen  %p ns mean, %p ns worst\n",
                      stats.mean_ns, stats.worst_ns);
        string_format(log, "  queued         %p ns\n", stats.queue_ns);
        string_format(log, "  drawing        %p ns\n", stats.draw_ns);
        string_format(log, "  flush          %p ns\n", stats.flush_ns);
        string_format(log, "counts reported  %p\n", stats.counts);
        string_format(log, "pixels moved     %p\n", stats.moved);
        return spark_report_close(device, 0);
}
