/*
        Shared PTY session helpers (line discipline, child lifecycle, OOM tracking)
        and Spark spawn-cost report, included once by the shell and terminal.
*/
#define TERMINAL_CANONICAL 0x0002u
#define TERMINAL_ECHO 0x0008u

static b32 term_sent(bipolar wrote)
{
        if (wrote > 0)
        {
                positive taken = (positive)wrote < to_shell_length ? (positive)wrote : to_shell_length;
                memory_copy(to_shell, to_shell + taken, to_shell_length - taken);
                to_shell_length -= taken;
                return true;
        }
        return wrote == -EAGAIN || wrote == -EINTR;
}

static b32 term_send(b32 master)
{
        return !to_shell_length || term_sent(system_write_once(master, to_shell, to_shell_length));
}

static fn term_follow_modes(b32 master)
{
        terminal_modes modes;

        if (system_control(master, PTY_TCGETS, address_of modes) != 0)
                return;
        term_line_editing((modes.behaviour & TERMINAL_CANONICAL) != 0);
        if (!line_editing || !(modes.behaviour & TERMINAL_ECHO))
                return;
        modes.behaviour &= ~TERMINAL_ECHO;
        system_control(master, PTY_TCSETS, address_of modes);
}

// Protect the terminal window (-500) over its shell (0) so OOM kills the shell first.
#define TERM_OOM_WINDOW "-500\n"
#define TERM_OOM_SHELL "0\n"

static fn term_oom_adjust(string_address value)
{
        bipolar handle = system_open_at(AT_FDCWD, (string_address) "/proc/self/oom_score_adj",
                                        FILE_WRITE | O_CLOEXEC);
        if (handle < 0)
                return;
        (void)system_write_all((positive)handle, value, string_length(value));
        system_close(handle);
}

static positive term_oom_kills(void)
{
        static p8 text[16384];
        bipolar got = file_slurp_once_at(AT_FDCWD, (string_address) "/proc/vmstat", text, sizeof(text));
        positive held = got > 0 ? (positive)got : 0;
        p8 address_to line = held >= 9 && !memory_compare(text, "oom_kill ", 9)
                                 ? text
                                 : memory_search(text, held, "\noom_kill ", 10);
        if (!line)
                return positive_max;
        line += 9 + (line[0] == '\n');
        return string_digits_max(line, held - (positive)(line - text), null);
}

// Non-null when terminal.c -x runs a program instead of SHELL.
static string_address address_to term_program_words;
static positive term_program_count;

static positive term_append(p8 address_to line, positive at, string_address text, positive cap)
{
        if (cap)
                string_copy_max_end(line + at, text, cap);
        else
                string_copy(line + at, text);
        return at + string_length(line + at);
}

// Reset terminal modes (sync-update off, alt-screen off, DECSTR) and print the fatal signal notice.
static fn term_child_killed(positive signal, string_address name, b32 out_of_memory)
{
        static const p8 restore[] = "\x1b[?2026l\x1b[?1049l\x1b[!p\r\n";
        p8 line[96];
        positive at = 0;

        term_line_editing(false);
        term_bytes(restore, sizeof(restore) - 1);
        at = term_append(line, at, term_program_count ? (string_address) "[program killed by signal "
                                                      : (string_address) "[shell killed by signal ", 0);
        at += positive_into_string(line + at, signal);
        if (name[0] < '0' || name[0] > '9')
        {
                at = term_append(line, at, (string_address) " (SIG", 0);
                at = term_append(line, at, name, 16);
                line[at++] = ')';
        }
        if (out_of_memory)
                at = term_append(line, at, (string_address) ": out of memory", 0);
        line[at++] = ']';
        term_bytes(line, at);
}

static void term_hang_up(bipolar child, b32 address_to hung)
{
        if (*hung)
                return;
        system_call_2(syscall(kill), (positive)(-child), SIGHUP);
        *hung = 1;
}

static positive term_exit_status;

// Spawn a shell (or -x program) on a new PTY, waiting up to 4 s for devpts to mount.
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
                    "TERM_PROGRAM=moonwater",
                    "TERMINFO=" TERM_INFO_DIRECTORY,
                    "MOONWATER_ASKED=1",
                    "HOME=/root",
                    "PATH=" BOWL_DEFAULT_PATH,
                    "LANG=C.UTF-8",
                    null};

                if (process_pty_child_setup(address_to master, slave, -1, -1) < 0)
                        system_call_1(syscall(exit), 126);
                term_oom_adjust(TERM_OOM_SHELL);
                process_signal_default(SIGINT);
                process_signal_default(SIGQUIT);
                process_signal_default(SIGHUP);
                host_terminal_opening();
                windows_asked_record();
                bowl_session_prepare("/root", null);
                if (term_program_count)
                {
                        (void)shell_exec_file(term_program_words[0], term_program_words, term_program_count,
                                              bowl_environment(seed));
                        system_call_1(syscall(exit), 127);
                }
                (void)shell_exec_file((string_address)SHELL, argv, 1, bowl_environment(seed));
                system_call_1(syscall(exit), 127);
        }
        system_close(slave);
        return child;
}

#define TERM_BATCH 1024

static b32 term_input(b32 master, const struct window_key address_to key)
{
        if (key->flags & WINDOW_KEY_POINTER)
                term_pointer(key->reserved & 0xffff, key->reserved >> 16, key->character, key->flags);
        else if (key->flags & WINDOW_KEY_DOWN)
                term_key_modified(key->character, key->code, key->flags);
        else
                return true;
        if ((key->flags & WINDOW_KEY_POINTER_MOVE) && to_shell_length < TERM_BATCH)
                return true;
        return term_send(master);
}

// Charge 64 bytes of drain budget per repeated cell so REP floods stay bounded.
#define TERM_REPEAT_CHARGE 64

static b32 term_drain(b32 master, b32 address_to gone, positive budget)
{
        p8 from_shell[4096];
        b32 changed = false;

        for (;;)
        {
                bipolar got = system_read_once(master, from_shell, sizeof(from_shell));
                if (got > 0)
                {
                        positive repeated = term_repeated;
                        if (!changed)
                                cursor_hide();
                        term_bytes(from_shell, (positive)got);
                        changed = true;
                        budget = budget > (positive)got ? budget - (positive)got : 0;
                        repeated = (term_repeated - repeated) * TERM_REPEAT_CHARGE;
                        budget = budget > repeated ? budget - repeated : 0;
                        if (!budget)
                                return changed;
                        continue;
                }
                if (got != -EAGAIN && got != -EINTR)
                        address_to gone = true;
                return changed;
        }
}

// Reap child for up to 2 s: SIGHUP at 1 s, SIGKILL at 2 s.
static positive term_reap(bipolar child, b32 address_to sent)
{
        positive status = 0;
        timespec wait = {0, 25000000};

        for (positive tries = 0; tries < 80; tries++)
        {
                if (system_wait4_retry(child, address_of status, 1, null) != 0)
                        return status;
                if (tries == 40)
                {
                        system_call_2(syscall(kill), (positive)(-child), SIGHUP);
                        address_to sent = 1;
                }
                system_call_2(syscall(nanosleep), (positive)address_of wait, 0);
        }
        system_call_2(syscall(kill), (positive)(-child), SIGKILL);
        address_to sent = 1;
        system_wait4_retry(child, address_of status, 0, null);
        return status;
}

static b32 term_finish(bipolar child, b32 master, b32 hung_up, positive oom_kills)
{
        b32 sent = 0;
        positive status = term_reap(child, address_of sent);
        positive signal = status & 0x7f;

        system_close(master);
        term_exit_status = status;
        if (!hung_up && !sent && signal && signal != 0x7f)
        {
                p8 name[16];
                positive kills = term_oom_kills();
                kill_name(signal, name);
                cursor_hide();
                term_child_killed(signal, name,
                                  oom_kills != positive_max && kills != positive_max && kills != oom_kills);
                return true;
        }
        return false;
}

// Spark spawn phase cost report.
static b32 screen_spawn()
{
        b32 device = system_open_at(AT_FDCWD, SPARK_DEVICE, FILE_READ_WRITE | O_CLOEXEC);
        struct stats stats;

        if (device < 0)
        {
                string_format(log, "cannot open %s: %b\n", SPARK_DEVICE, device);
                log_flush();
                return 1;
        }
        if (system_control(device, SPARK_IOCTL_STATS, address_of stats) != 0)
        {
                string_format(log, "could not read spawn stats\n");
                system_close(device);
                log_flush();
                return 1;
        }

        positive spawns = stats.spawns ? stats.spawns : 1;
        struct { string_address label; positive ns; } rows[] = {
            {"  task           ", stats.task_ns},
            {"  exec           ", stats.exec_ns},
            {"    loader       ", stats.loader_ns},
            {"      mapping    ", stats.map_ns},
            {"    prologue     ", stats.exec_ns > stats.loader_ns ? stats.exec_ns - stats.loader_ns : 0},
            {"    loader rest  ", stats.loader_ns > stats.map_ns ? stats.loader_ns - stats.map_ns : 0},
        };

        string_format(log, "spawns           %p\nloads            %p\n", stats.spawns, stats.loads);
        for (positive i = 0; i < array_count(rows); i++)
                if (i < 4 || rows[i].ns)
                        string_format(log, "%s%p ns each\n", rows[i].label, rows[i].ns / spawns);
        string_format(log, "totals           %p ns task, %p ns exec\n", stats.task_ns, stats.exec_ns);
        system_close(device);
        log_flush();
        return 0;
}
