/*
        What the terminal and the spawn report share.

        The terminal (terminal.c) is a window on the compositor, and runs the
        far end's shell through the helpers below: the line discipline, the
        child, its death and what killed it. The spawn report reads the
        kernel's counters of what a program start costs. They are together
        because the shell includes them once, together.
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
        The X in the titlebar, once: a window closing is its line hanging up, and SIGHUP to the shell's group is
        what that has always been. Not sent again, though the request stays set for as long as the window
        exists. Hung is set when it is sent.
*/
static void term_hang_up(bipolar child, b32 address_to hung)
{
        if (*hung)
                return;

        system_call_2(syscall(kill), (positive)(-child), SIGHUP);
        *hung = 1;
}

/*
        What the pty's far end runs when it is not the shell: a program and its
        words, set by a terminal that was asked for one (terminal.c, -x). Null
        is the shell, as it always was.
*/
static string_address address_to term_program_words;
static positive term_program_count;

// How the program that ended did, as wait4 said it: set by term_finish.
static positive term_exit_status;

/*
        A shell on a pty of its own, started.

        Answers the shell's pid, with the pty's master in master and the
        count of processes the kernel had killed for want of memory in
        oom_kills, or a negative number after saying why not and with nothing
        left open. The window the caller draws in is its own to close then.

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
static bipolar term_session(b32 address_to master, positive address_to oom_kills)
{
        timespec wait = {0, 2000000};
        b32 slave = -1;
        bipolar child;

        address_to master = -1;

        for (int tries = 0; tries < 2000 && address_to master < 0; tries++)
        {
                if (process_pty_open(master, address_of slave, true) < 0)
                        address_to master = -1;

                if (address_to master < 0)
                        system_call_2(syscall(nanosleep), (positive)address_of wait, 0);
        }

        if (address_to master < 0)
        {
                log_direct(str("term: no /dev/ptmx\n"));
                return -1;
        }

        grid_tell(address_to master);

        terminal_terminfo_install();

        // Here and not sooner: the kernel mounts /proc before init mounts the
        // devpts the pty above waited for.
        term_oom_adjust(TERM_OOM_WINDOW);

        address_to oom_kills = term_oom_kills();
        child = system_fork();

        if (child < 0)
        {
                system_close(slave);
                system_close(address_to master);
                log_direct(str("term: cannot fork\n"));
                return -1;
        }

        if (child == 0)
        {
                string_address argv[] = {SHELL, null};
                string_address seed[] = {
                    "TERM=" TERM_NAME,
                    // What the shell looks for before it writes a failed command's status at the right of its prompt.
                    "TERM_PROGRAM=moonwater",
                    "TERMINFO=" TERM_INFO_DIRECTORY,
                    "HOME=/root",
                    "PATH=" BOWL_DEFAULT_PATH,
                    "LANG=C.UTF-8",
                    null};

                if (process_pty_child_setup(address_to master, slave, -1, -1) < 0)
                        system_call_1(syscall(exit), 126);

                // A fork shares no memory, so this is the shell's alone.
                term_oom_adjust(TERM_OOM_SHELL);

                // What the shell is asked to do by the keyboard is its own to decide: a
                // program started in the background by a script ignores these, and the
                // shell on this pty would inherit that and not be interrupted.
                process_signal_default(SIGINT);
                process_signal_default(SIGQUIT);
                process_signal_default(SIGHUP);

                //      Boot may still be looking for an installed disk, and
                //      may have a question for whoever is at this window.
                host_terminal_opening();

                bowl_session_prepare("/root", null);
                if (term_program_count)
                {
                        // A program that cannot start is its window's end, not a shell in its place.
                        (void)shell_exec_file(term_program_words[0], term_program_words, term_program_count,
                                              bowl_environment(seed));
                        system_call_1(syscall(exit), 127);
                }
                (void)shell_exec_file((string_address)SHELL, argv, 1,
                                      bowl_environment(seed));
                system_call_1(syscall(exit), 127);
        }

        system_close(slave);
        return child;
}

/*
        A pointer's motion waits in the queue for the loop's send, which is made once the wake's events have all been taken,
        so a wake's motion is one write and not one write a motion. The queue is sent here once it is long, so the fixed
        queue (term.c) does not refuse a report for want of room. Anything else is sent as it is taken, as it always was:
        a button, a key or a wheel notch that the far end refuses ends its run at once.
*/
#define TERM_BATCH 1024

/*
        A key or a pointer report, as the bytes it is for.

        A compositor window's events come through here as a window_key, so
        what a Return, a Ctrl+C and a click turn into is one rule. False when
        the pty will take no more.
*/
static b32 term_input(b32 master, const struct window_key address_to key)
{
        if (key->flags & WINDOW_KEY_POINTER)
                term_pointer(key->reserved & 0xffff, key->reserved >> 16,
                             key->character, key->flags);
        else if (key->flags & WINDOW_KEY_DOWN)
                term_key_modified(key->character, key->code, key->flags);
        else
                return true;

        if ((key->flags & WINDOW_KEY_POINTER_MOVE) && to_shell_length < TERM_BATCH)
                return true;

        return term_send(master);
}

/*
        Everything the far end has said, taken into the emulator, as much as
        the far end's line discipline hands over in one read, which is its
        four kilobyte buffer: a quarter of that was four reads, four wakeups
        of the writer and four passes of the loop for every one the pty
        needed. Answers whether anything came, and stops after budget bytes so a
        far end that never pauses cannot keep the caller from drawing; gone is
        set when the pty will say no more.

        Nothing waiting is EAGAIN and only EAGAIN. Everything else is the
        shell gone -- zero, or EIO once the session went with it -- and
        reading that as nothing waiting left this spinning on a dead pty
        forever, with a zombie behind it and a window nothing could dismiss.
*/
//      Budget a repeated cell costs, in bytes: one write of a cell is some
//      tens of nanoseconds, so 64 bytes of budget a cell keeps a pass to a few
//      milliseconds of repeats.
#define TERM_REPEAT_CHARGE 64

static b32 term_drain(b32 master, b32 address_to gone, positive budget)
{
        p8 from_shell[4096];
        b32 changed = false;

        for (;;)
        {
                bipolar got = system_read_once(master, from_shell,
                                               sizeof(from_shell));

                if (got > 0)
                {
                        positive repeated = term_repeated;

                        if (!changed)
                                cursor_hide();

                        term_bytes(from_shell, (positive)got);

                        changed = true;
                        budget = budget > (positive)got ? budget - (positive)got : 0;
                        repeated = term_repeated - repeated;
                        budget = budget > repeated * TERM_REPEAT_CHARGE ? budget - repeated * TERM_REPEAT_CHARGE : 0;

                        if (!budget)
                                return changed;

                        continue;
                }

                if (got != -EAGAIN && got != -EINTR)
                        address_to gone = true;

                return changed;
        }
}

/*
        The shell is over: reaped, the pty closed, and whether it went by a
        signal this window did not send. An exit of any status, and the hangup
        the X sent, close the window as they always have; only a signal from
        elsewhere keeps it, and what happened is said after whatever was on
        the screen. Answers whether the window stays.
*/
static b32 term_finish(bipolar child, b32 master, b32 hung_up, positive oom_kills)
{
        positive status = 0;
        positive signal;

        system_wait4_retry(child, address_of status, 0, null);
        system_close(master);

        term_exit_status = status;
        signal = status & 0x7f;

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
                return true;
        }

        return false;
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
        written, and until this report read them the cost of a spawn was
        only visible from outside as wall clock. The phases are
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
