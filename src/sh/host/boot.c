/*
        src/sh/host/boot.c: boot's service, its events and the shells it starts; the verbs canvas, latency and bios.

        A run of src/sh/host.c, which includes this file where the run stood (test/split.py cut it there): it is
        compiled only as part of that file, with its statics and macros, and never on its own. host_main, at the end
        of src/sh/host.c, names the function each verb runs.
*/

// Events --------------------------------------------------------

/*
        init and exit, run the way a terminal runs what is typed into it:
        /shell -c, from /root, with the path a terminal gives its shell, so a
        bowl's published launchers -- pacman once bowl setup arch has run on
        a machine that keeps /bowls -- are found by name.

        init runs once per boot, as root, in the background and in the order
        written, once boot has settled the disks and somebody has answered
        its question when it asked one, so a command reads the /root and the
        bowls it will be using. Each entry's output goes to
        /run/moonwater/init/ID.log and how it ended to ID.status, the kernel
        log -- the kernel log window -- says when each starts and ends, by
        its number and never its text, and neither the prompt nor the
        desktop waits for any of it. The text is root's alone, because a
        command can carry a secret (`link group NS SECRET`), and anybody can
        read the kernel log on this image: its first eighty characters went
        there with every boot.

        exit runs when the machine stops, before anything is remounted
        read-only, with its output on the terminal that stopped it. Each
        entry gets ten seconds and all of them thirty; one still running then
        is killed with its process group, so a stuck command cannot keep a
        machine from powering off. A stop that is not the shell's -- the
        kernel's own orderly power off -- runs none of it.
*/
#define HOST_EVENTS_INIT HOST_STATE "/init"
#define HOST_EVENT_SHELL "/shell"
#define HOST_EXIT_EACH_NS ((p64)10000000000)
#define HOST_EXIT_ALL_NS ((p64)30000000000)
#define HOST_EVENT_POLL_NS ((p64)20000000)
//      The machine process takes the verdict's poll and the script's sourcing
//      to say it has the script; a boot that has not heard by then does not.
#define HOST_MACHINE_SOURCED_WAIT_NS ((p64)5000000000)

static string_address address_to host_event_environment(void)
{
        static string_address seed[] = {
            SPARK_COMMAND_ENVIRONMENT(SPARK_ENVIRONMENT_ENTRY) null};

        bowl_session_prepare("/root", null);
        return bowl_environment(seed);
}

static fn host_event_ending(p8 address_to into, positive room, positive status)
{
        p8 number[24];
        positive code = (positive)wait_status_code_base((p32)status, 256);

        positive_into_string(number, code >= 256 ? code & 0xff : code);
        string_copy_bounded(into, code >= 256 ? "killed by signal " : "exited ", room);
        string_append_bounded(into, number, room);
}

/* One line in the kernel log, which the kernel log window shows. */
static fn host_kmsg(string_address address_to parts)
{
        p8 line[320];
        bipolar handle;

        string_copy_bounded(line, "[moonwater] ", sizeof(line));
        for (; *parts; parts++)
                string_append_bounded(line, *parts, sizeof(line));

        handle = system_open_at(AT_FDCWD, "/dev/kmsg", O_WRONLY | O_CLOEXEC);
        if (handle < 0)
                return;

        kmsg_write(handle, line, string_length(line));
        system_close(handle);
}

/*
        /shell in a session of its own, from /root, with the environment init's
        entries get. Its input is /dev/null and its output stays what the
        caller has unless output is a descriptor to put there. Given a
        terminal, all three are that terminal, opened here, after the session
        is made, so that it is the shell's own and its controlling one.
*/
static bipolar host_shell_start(string_address address_to argv, positive count,
                                string_address terminal, bipolar output)
{
        bipolar child = system_fork();
        bipolar handle;

        if (child)
                return child;

        system_call(syscall(setsid));
        handle = system_open_at(AT_FDCWD, terminal ? terminal : (string_address) "/dev/null",
                                FILE_READ_WRITE | O_CLOEXEC | O_NOFOLLOW);
        if (terminal && handle < 3)
                system_call_1(syscall(exit), 126);

        if (handle > 0)
                for (positive at = 0; at < (terminal ? 3 : 1); at++)
                        system_call_3(syscall(dup3), (positive)handle, at, 0);
        if (!terminal && output > 2)
        {
                system_call_3(syscall(dup3), (positive)output, 1, 0);
                system_call_3(syscall(dup3), (positive)output, 2, 0);
        }

        system_signal_install(1, 0, 0, 0, null);
        system_signal_install(13, 0, 0, 0, null);
        system_call_1(syscall(chdir), (positive)(string_address) "/root");
        (void)shell_exec_file(HOST_EVENT_SHELL, argv, count, host_event_environment());
        system_call_1(syscall(exit), 127);
        return -1;
}

/* /shell -c text, its output where asked or inherited. */
static bipolar host_event_start(string_address text, bipolar output)
{
        string_address argv[] = {HOST_EVENT_SHELL, "-c", text, null};

        return host_shell_start(argv, 3, null, output);
}

/*
        A child to its end, or past limit killed with its group and then
        waited for: its wait status, and whether it had to be stopped. A limit
        of zero waits as long as it takes.
*/
static positive host_event_wait(bipolar child, p64 limit, bool address_to stopped)
{
        p64 started = system_clock_ns(HOST_CLOCK_BOOTTIME);
        positive status = 0;
        bipolar pidfd = limit ? system_call_2(syscall(pidfd_open), (positive)child, 0)
                              : -1;

        address_to stopped = false;

        /*      With a limit, the child's pidfd is polled for as long as is
                left of it, so the end is heard when it comes; the 20 ms look
                that used to sit here was late by up to that much on every
                event. */
        for (;;)
        {
                bipolar reaped = system_call_4(syscall(wait4), (positive)child,
                                               (positive)address_of status,
                                               limit ? 1 : 0, 0);

                if (reaped == child)
                {
                        if (pidfd >= 0)
                                system_close(pidfd);
                        return status;
                }
                if (reaped == -4)
                        continue;
                if (reaped < 0)
                {
                        if (pidfd >= 0)
                                system_close(pidfd);
                        return 0;
                }

                p64 elapsed = system_clock_ns(HOST_CLOCK_BOOTTIME) - started;

                if (elapsed >= limit)
                {
                        system_call_2(syscall(kill), (positive)(-child), SIGKILL);
                        system_call_2(syscall(kill), (positive)child, SIGKILL);
                        address_to stopped = true;
                        limit = 0;
                        continue;
                }

                if (pidfd < 0)
                {
                        host_pause(HOST_EVENT_POLL_NS);
                        continue;
                }

                p64 left = limit - elapsed;
                timespec span = {left / 1000000000, left % 1000000000};
                system_poll_descriptor waited = {(b32)pidfd, SYSTEM_POLL_READ, 0};

                system_poll_wait(address_of waited, 1, address_of span, null);
        }
}

/*
        Until boot has said what this session is: a verdict that is not a
        question, or the time a boot is given to say one. What runs at boot
        and does not need to be there before the disks are is held here.
*/
static fn host_verdict_settled(void)
{
        bipolar notify = host_verdict_watch();

        for (;;)
        {
                p8 verdict[HOST_NAME_ROOM + 16];
                bool found = host_read_word(HOST_VERDICT, verdict, sizeof(verdict)) >= 0;

                if (found ? !string_has_prefix(verdict, "ask ")
                          : system_clock_ns(HOST_CLOCK_BOOTTIME) >= HOST_VERDICT_WAIT_NS)
                        break;

                //      A question stands until it is answered, for as long as
                //      that takes; with no verdict at all, boot has until its time.
                host_verdict_pause(notify, found ? 0 : HOST_VERDICT_WAIT_NS,
                                   HOST_POLL_NS * 2);
        }
        host_verdict_unwatch(notify);
}

static fn host_events_boot(host_settings address_to settings)
{
        host_setting setting;
        positive at = 0;

        bipolar runner;

        if (!settings || !host_settings_count(settings, SPARK_SETTINGS_INIT))
                return;
        //      A fork that failed is no runner at all, and boot goes on without its rules: said, not left as nothing.
        if ((runner = system_fork()) != 0)
        {
                if (runner < 0)
                        host_fail("the rules' runner", runner);
                return;
        }

        //      The runner, from here on: its own session, outliving boot.
        system_call(syscall(setsid));
        host_state_ready();
        host_state_directory(HOST_EVENTS_INIT, 0700);
        host_verdict_settled();

        while (host_settings_next(settings, address_of at, address_of setting))
        {
                p8 text[SPARK_SETTINGS_TEXT_MOST + 1];
                p8 id[8];
                p8 path[HOST_PATH_ROOM];
                p8 status_path[HOST_PATH_ROOM];
                p8 ending[48];
                bipolar output;
                bipolar child;
                positive status;
                bool stopped = false;

                if (setting.entry.list != SPARK_SETTINGS_INIT)
                        continue;

                host_settings_text(text, address_of setting);
                positive_into_string(id, setting.entry.id);

                if (!host_join(path, sizeof(path), HOST_EVENTS_INIT "/", id) ||
                    !host_join(status_path, sizeof(status_path), path, ".status") ||
                    !host_join(path, sizeof(path), path, ".log"))
                        continue;

                {
                        string_address line[] = {"init ", id, " started", null};

                        host_kmsg(line);
                }

                host_write_text(status_path, "running\n");
                output = host_open_state(AT_FDCWD, path, 0600, FILE_WRITE);
                child = host_event_start(text, output);
                if (output >= 0)
                        system_close(output);

                status = child < 0 ? (positive)127 << 8
                                   : host_event_wait(child, 0, address_of stopped);
                host_event_ending(ending, sizeof(ending), status);
                string_append_bounded(ending, "\n", sizeof(ending));
                host_write_text(status_path, ending);
                ending[string_length(ending) - 1] = end;

                {
                        string_address line[] = {"init ", id, " ", ending, "; its output is in ",
                                                 path, null};

                        host_kmsg(line);
                }
        }

        system_call_1(syscall(exit), 0);
}

fn host_exit_run(void)
{
        host_settings settings;
        host_setting setting;
        positive at = 0;
        p64 started = system_clock_ns(HOST_CLOCK_BOOTTIME);

        bool machine;

        //      Before the machine process is asked anything: END is root's, a
        //      refusal left the stop polling ten seconds for a detach that
        //      was never coming, and the list is root's too.
        if (!bowl_is_root())
                return;

        machine = host_machine_stop();
        //      Said with its time, so a stop that waits is seen waiting: the machine process let go (or there was
        //      none to ask), or the wait ran out.
        {
                p8 spent[24];
                string_address line[] = {"stop: the machine process ", machine ? (string_address) "let go" : (string_address) "was not there or did not let go",
                                         " after ", (string_address)spent, " ms", null};

                spent[positive_into_string(spent, (system_clock_ns(HOST_CLOCK_BOOTTIME) - started) / 1000000)] = 0;
                host_kmsg(line);
        }
        if (!host_settings_kept(address_of settings) &&
            !host_settings_booted(address_of settings))
                return;

        while (host_settings_next(address_of settings, address_of at, address_of setting))
        {
                p8 text[SPARK_SETTINGS_TEXT_MOST + 1];
                p8 ending[48];
                p64 spent = system_clock_ns(HOST_CLOCK_BOOTTIME) - started;
                p64 limit = HOST_EXIT_ALL_NS - spent;
                bool stopped = false;
                positive status;
                bipolar child;

                if (setting.entry.list != SPARK_SETTINGS_EXIT)
                        continue;

                if (spent >= HOST_EXIT_ALL_NS)
                {
                        host_say(log_error, host_label "exit: thirty seconds are spent, "
                                                       "and the rest do not run\n");
                        break;
                }

                host_settings_text(text, address_of setting);
                host_say(log, host_label "exit %p: %s\n", (positive)setting.entry.id, text);

                //      The environment init's entries get, not the stopping
                //      shell's: a bound poweroff has almost none.
                child = host_event_start(text, -1);
                if (child < 0)
                        continue;

                status = host_event_wait(child, limit < HOST_EXIT_EACH_NS ? limit
                                                                          : HOST_EXIT_EACH_NS,
                                         address_of stopped);
                host_event_ending(ending, sizeof(ending), status);

                if (stopped)
                        string_format(log_error, host_label "exit %p did not finish in time "
                                                            "and was killed\n",
                                      (positive)setting.entry.id);
                else if (status)
                        string_format(log_error, host_label "exit %p %s\n",
                                      (positive)setting.entry.id, ending);
                log_flush();
        }
}

/*
        Canvas, off and on.

        Reading needs nothing; the kernel decides who may turn it off or on.
        Off is usually typed into a terminal of the desktop, which closes under it, so
        the way back is said first and the hangup that closing sends is
        ignored. Where the kernel console is not a screen -- console=ttyS0,
        with init's shell on the serial line -- the text console off leaves
        would have no shell, so one is started on tty1.
*/
static bipolar host_canvas_request(positive request,
                                   struct canvas_control address_to control)
{
        memory_zero(control, sizeof(address_to control));
        control->request = (unsigned int)request;
        return host_spark_once(SPARK_IOCTL_CANVAS, control, FILE_READ);
}

static const struct windows address_to host_windows_header(void);
static fn host_windows_unmap(const struct windows address_to table);

/*
        Where each of Canvas's screens is in the desktop's space, into places
        (x, y, and its place in the desktop's list of screens -- 0 the
        primary, which is the order screen N of moonwater window counts in --
        or -1 for a screen that is dark): the compositor's own list when one
        is running and its table can be read, and else where the kept
        arrangement lays the screens Canvas has (windows_arrange_lay), which
        is where a desktop that starts puts them. A caller who can read
        neither (both are root's) is told so: -2, and no default as the
        answer.
*/
static fn host_canvas_places(struct canvas_control address_to control, int (*places)[3])
{
        const struct windows address_to table = host_windows_header();
        positive count = control->output_count < SPARK_CANVAS_OUTPUTS ? control->output_count : SPARK_CANVAS_OUTPUTS;
        char names[SPARK_CANVAS_OUTPUTS][WINDOWS_OUTPUT_NAME];
        int sizes[SPARK_CANVAS_OUTPUTS][2], rects[SPARK_CANVAS_OUTPUTS][4];
        unsigned char order[SPARK_CANVAS_OUTPUTS];
        p8 text[512];
        bipolar got;
        unsigned int lit;

        for (positive at = 0; at < count; at++)
        {
                string_copy_bounded((p8 address_to)names[at], (string_address)control->output[at].connector, WINDOWS_OUTPUT_NAME);
                sizes[at][0] = (int)control->output[at].width;
                sizes[at][1] = (int)control->output[at].height;
                places[at][0] = places[at][1] = 0;
                places[at][2] = -1;
        }
        if (table && table->outputs && table->outputs <= WINDOWS_OUTPUTS)
        {
                for (positive at = 0; at < count; at++)
                        for (unsigned int screen = 0; screen < table->outputs; screen++)
                                if (!memory_compare(table->output_name[screen], names[at], WINDOWS_OUTPUT_NAME - 1) &&
                                    !table->output_name[screen][WINDOWS_OUTPUT_NAME - 1])
                                {
                                        int width, height;

                                        windows_output(table, screen, &places[at][0], &places[at][1], &width, &height);
                                        places[at][2] = (int)screen;
                                }
                host_windows_unmap(table);
                return;
        }
        host_windows_unmap(table);
        got = host_read_state(SETTING_CANVAS_ARRANGE_PATH, text, sizeof(text) - 1);
        if (got < 0 && got != -ENOENT)
        {
                for (positive at = 0; at < count; at++)
                        places[at][2] = -2;
                return;
        }
        text[got > 0 ? got : 0] = end;
        lit = windows_arrange_lay((const char address_to)text, (const char (*)[WINDOWS_OUTPUT_NAME])names,
                                  (const int (*)[2])sizes, (unsigned int)count, rects, order);
        for (unsigned int at = 0; at < lit; at++)
        {
                places[order[at]][0] = rects[order[at]][0];
                places[order[at]][1] = rects[order[at]][1];
                places[order[at]][2] = (int)at;
        }
}

// Canvas's screens in the desktop's order, the primary first and the dark ones last: each a line from say.
static fn host_canvas_screens(struct canvas_control address_to control, int (*places)[3],
                              fn (*say)(struct canvas_control address_to control, positive at, int (*places)[3], bool label),
                              bool label)
{
        positive count = control->output_count < SPARK_CANVAS_OUTPUTS ? control->output_count : SPARK_CANVAS_OUTPUTS;

        for (int index = 0; index < (int)count; index++)
                for (positive at = 0; at < count; at++)
                        if (places[at][2] == index)
                                say(control, at, places, label);
        for (positive at = 0; at < count; at++)
                if (places[at][2] < 0)
                        say(control, at, places, label);
}

static fn host_canvas_screen_line(struct canvas_control address_to control, positive at, int (*places)[3], bool label)
{
        host_prefix(label);
        string_format(log, "  %s: %p by %p, %p Hz", (string_address)control->output[at].connector,
                      (positive)control->output[at].width, (positive)control->output[at].height,
                      (positive)control->output[at].refresh);
        // LOCALE_NEEDS_ROOT's words, which the readers of /root say, defined further on.
        if (places[at][2] == -2)
                string_format(log, ", its place (needs root)\n");
        else if (places[at][2] < 0)
                string_format(log, ", dark\n");
        else
                string_format(log, ", at %b,%b%s\n", (bipolar)places[at][0], (bipolar)places[at][1],
                              places[at][2] ? (string_address) "" : (string_address) ", primary");
}

static fn host_canvas_write(bool label,
                            struct canvas_control address_to control)
{
        int places[SPARK_CANVAS_OUTPUTS][3];

        host_prefix(label);
        if (!control->running)
        {
                string_format(log, "Canvas is off; moonwater desktop on starts it\n");
                return;
        }

        string_format(log, "Canvas is on: %s\n", (string_address)control->driver);

        host_canvas_places(control, places);
        host_canvas_screens(control, places, host_canvas_screen_line, label);

        if (control->suspended)
        {
                host_prefix(label);
                string_format(log, "the display is held by the desktop or another "
                                   "program; Control-Alt-Backspace ends it\n");
        }

        host_prefix(label);
        string_format(log, "the desktop is drawn at %p times the size, modes %s\n",
                      (positive)control->scale,
                      control->modes == SPARK_CANVAS_MODES_PREFERRED
                          ? "preferred" : "largest");
}

static fn host_canvas_say(struct canvas_control address_to control)
{
        host_canvas_write(true, control);
        log_flush();
}

/* Whether the kernel console is a screen, where init's shell is on tty1. */
static bool host_console_is_screen(void)
{
        p8 active[128];

        if (host_read_kernel("/sys/class/tty/console/active", active, sizeof(active)) <= 0)
                return true;

        for (string_address at = (string_address)active; *at;)
        {
                if (string_has_prefix(at, "tty") && byte_is_digit(at[3]))
                        return true;

                at = string_first_of_or_end(at, ' ');
                at += string_span_of_set(at, " ");
        }

        return false;
}

/* Whether a process that is not gone is on tty1: its controlling terminal is 4:1. */
static bool host_tty1_held(void)
{
        static system_snapshot sample;
        bool held = false;

        if (!system_snapshot_take(address_of sample, SPARK_SNAPSHOT_PROCESS, false))
                return false;

        for (positive at = 0; at < sample.header.process_count; at++)
                held |= sample.processes[at].tty == (4 << 8 | 1) &&
                        sample.processes[at].state != 'Z';

        return held;
}

#define HOST_VT_GETSTATE 0x5603
#define HOST_VT_ACTIVATE 0x5606
#define HOST_VT_WAITACTIVE 0x5607

/*
        The text console's picture, drawn again. Canvas off gives the card back to the kernel's console while the
        compositor, which is ended after it, still has the master, so the console's own modeset is refused and
        nothing repaints it once the compositor is gone: the shell is there, and what is typed, and the screen is
        black until something draws it. A switch to another virtual console and back does, and costs a frame. It is
        done whether the console is the screen or a serial line, since the shell on tty1 is what the screen shows
        then.
*/
static fn host_console_redraw(void)
{
        unsigned short state[3] = {0, 0, 0};
        bipolar tty;
        unsigned int active;
        unsigned int other;

        tty = system_open_at(AT_FDCWD, "/dev/tty0", O_RDWR | O_NOCTTY | O_CLOEXEC);
        if (tty < 0)
                return;
        if (system_call_3(syscall(ioctl), (positive)tty, HOST_VT_GETSTATE, (positive)state) >= 0 && state[0])
        {
                active = state[0];
                other = active == 1 ? 2 : 1;
                (void)system_call_3(syscall(ioctl), (positive)tty, HOST_VT_ACTIVATE, other);
                (void)system_call_3(syscall(ioctl), (positive)tty, HOST_VT_WAITACTIVE, other);
                (void)system_call_3(syscall(ioctl), (positive)tty, HOST_VT_ACTIVATE, active);
                (void)system_call_3(syscall(ioctl), (positive)tty, HOST_VT_WAITACTIVE, active);
        }
        system_close((positive)tty);
}

/* A shell on tty1, in a session of its own, for a console that has none. */
static fn host_tty1_shell(void)
{
        string_address argv[] = {HOST_EVENT_SHELL, null};

        //      One is enough: each desktop off over a console that is not a
        //      screen forked another, and none ended, so after a few round
        //      trips tty1 had as many shells splitting its keys.
        if (!host_tty1_held())
                host_shell_start(argv, 1, "/dev/tty1", -1);
}

static b32 locale_canvas_setting(string_address address_to arguments, positive count);
static b32 locale_canvas_cursor(string_address address_to arguments, positive count);
static b32 locale_canvas_windows(string_address address_to arguments, positive count);
static b32 locale_canvas_arrange(string_address address_to arguments, positive count);
static b32 locale_canvas_display(string_address address_to arguments, positive count);

/*
        Canvas off: every window closes and the console is the text one. What `moonwater desktop off` does; the machine
        script's own `desktop_stop` runs from the event this fires.
*/
static b32 host_canvas_off(void)
{
        struct canvas_control control;
        bipolar failed;

        //      The banner is for a desktop about to close: said over a
        //      text console, without the right to close one, or with no
        //      Canvas in the kernel, it made the refusal after it a lie.
        if (!bowl_is_root())
                return host_refuse("turning Canvas off needs root (CAP_SYS_ADMIN)\n");
        failed = host_canvas_request(SPARK_CANVAS_STATUS, address_of control);
        return_if(failed < 0, host_fail(SPARK_DEVICE, failed));
        if (!control.running)
                return host_refuse("Canvas is already off\n");

        //      A pipe closed on the banner (`desktop off | true`) must not
        //      end this before the desktop is; the signal is put back at once,
        //      since the console shell started below would inherit it ignored.
        positive piped[4];
        bool quiet = system_signal_install(13, 1, 0, 0, piped);

        host_say(log, host_label "Canvas off: every window closes, this one too. "
                                 "On the text console, moonwater desktop on "
                                 "brings the desktop back.\n");
        if (quiet)
                system_signal_action(13, piped, null, 8);

        system_signal_install(1, 1, 0, 0, null);

        failed = host_canvas_request(SPARK_CANVAS_OFF, address_of control);
        if (failed < 0)
                system_signal_install(1, 0, 0, 0, null);
        if (failed == -EPERM)
                return host_refuse("turning Canvas off needs root (CAP_SYS_ADMIN)\n");
        if (failed == -EALREADY)
                return host_refuse("Canvas is already off\n");
        return_if(failed < 0, host_fail("desktop off", failed));

        //      The hangup stays ignored until the console shell has
        //      forked: the terminal this runs on may be the one that
        //      closed. The shell resets it itself, before it execs.
        if (!host_console_is_screen())
                host_tty1_shell();
        system_signal_install(1, 0, 0, 0, null);

        return 0;
}

// Canvas on: the desktop's machine script starts the compositor from the event this fires.
static b32 host_canvas_on(void)
{
        struct canvas_control control;
        bipolar failed;

        failed = host_canvas_request(SPARK_CANVAS_ON, address_of control);
        if (failed == -EPERM)
                return host_refuse("turning Canvas on needs root (CAP_SYS_ADMIN)\n");
        if (failed == -EALREADY)
                return host_refuse("Canvas is already on\n");
        if (failed == -EBUSY)
        {
                if (control.master_command[0] && !control.master_pid)
                        string_format(log_error, host_label "%s, outside this process namespace, "
                                                            "holds the display; "
                                                            "Canvas stays off until it lets go\n",
                                      (string_address)control.master_command);
                else if (control.master_command[0])
                        string_format(log_error, host_label "%s (pid %p) holds the display; "
                                                            "Canvas stays off until it lets go\n",
                                      (string_address)control.master_command,
                                      (positive)control.master_pid);
                else
                        string_format(log_error, host_label "another program holds the display; "
                                                            "Canvas stays off until it lets go\n");
                log_flush();
                return 1;
        }
        if (failed == -ENODEV)
                return host_refuse("there is no display for Canvas to start on\n");
        return_if(failed < 0, host_fail("desktop on", failed));

        host_canvas_say(address_of control);
        return 0;

}

static b32 host_canvas_identify(positive count);

/* moonwater canvas [scale [N|auto]|modes [largest|preferred]|arrange [NAME[@X,Y|@off]...|default]|
   display NAME rotation DEGREES|refresh HZ|preferred|mode WxH|preferred|primary|place X Y [NAME place X Y]...|
   identify|background [COLOUR|default]|cursor [size [PERCENT|default]|look [light|dark|auto]]] */
/*
        moonwater canvas record [SECONDS] and moonwater canvas record report: the compositor's present record (src/canvas/record.h). A recording
        of SECONDS (1 to 10, three by default) is asked for by the file /run/moonwater/record/start, and the compositor takes it at its next look
        (every eighth present) and writes its report when the seconds have run and a present has come after them. The report is asked for by
        /run/moonwater/record/report: this waits up to three seconds for the compositor to take the request, then prints report.txt. The
        compositor is the only writer of report.txt and state; this writes the two requests, and only as root.
*/
#define HOST_RECORD_DIR "/run/moonwater/record"
#define HOST_RECORD_TEXT 8192

static b32 host_canvas_record(string_address address_to arguments, positive count)
{
        p8 request[] = "report\n", report[HOST_RECORD_TEXT + 1], start[4];
        positive seconds = 3, at = 0;
        bipolar got;

        if (count == 4 && string_equals(arguments[3], "report"))
        {
                host_need_root();
                if (host_write_file((string_address)HOST_RECORD_DIR "/report", request, 7, 0644, false) < 0)
                        return host_refuse("the desktop is not running: the compositor's record is not there (%s)\n", (string_address)HOST_RECORD_DIR);
                for (positive wait = 0; wait < 30; wait++)
                {
                        if (host_read_state((string_address)HOST_RECORD_DIR "/report", report, 16) < 0)
                                break;
                        host_pause(100000000);
                }
                got = host_read_state((string_address)HOST_RECORD_DIR "/report.txt", report, HOST_RECORD_TEXT);
                if (got < 0)
                        return host_refuse("no report yet: the compositor has not made one (its record is kept while the desktop runs)\n");
                report[got] = 0;
                host_say(log, "%s", (string_address)report);
                return 0;
        }
        if (count == 4)
        {
                string_address word = arguments[3];
                positive value = 0, digit = 0;

                value = string_digits_max(word, 3, address_of digit);
                if (!digit || word[digit] || value < 1 || value > 10)
                        return host_usage();
                seconds = value;
        }
        else if (count != 3)
                return host_usage();
        if (seconds == 10)
                start[at++] = '1', start[at++] = '0';
        else
                start[at++] = (p8)('0' + seconds);
        start[at++] = '\n';
        host_need_root();
        if (host_write_file((string_address)HOST_RECORD_DIR "/start", start, at, 0644, false) < 0)
                return host_refuse("the desktop is not running: the compositor's record is not there (%s)\n", (string_address)HOST_RECORD_DIR);
        host_say(log, "recording %p s; the compositor takes it at its next look and writes its report when the seconds have run and a present follows\n",
                 (positive)seconds);
        host_say(log, "the report: moonwater canvas record report\n");
        return 0;
}

/*
        moonwater canvas shot: the screen as the compositor last composed it, whichever of the card or the processor did, written by the
        compositor at its next look (src/canvas/record.h) to /run/moonwater/record/frame-SEQ-shot.ppm (the owner's alone: it is the screen).
        This asks for it, waits up to five seconds for the file and says where it is.
*/
static bool host_canvas_shot_newest(string_address directory, string_address name, address_any context)
{
        p8 address_to newest = context;
        positive length = string_length(name);

        (void)directory;
        //      The frame's number has no leading zeros: a longer name is a later frame.
        if (length > 9 && length < 96 && string_equals(name + length - 9, "-shot.ppm") &&
            (length > string_length((string_address)newest) ||
             (length == string_length((string_address)newest) && string_compare(name, (string_address)newest) > 0)))
                string_copy_bounded(newest, name, 96);
        return true;
}

static b32 host_canvas_shot(positive count)
{
        p8 before[96] = "", after[96] = "";

        return_if(count != 3, host_usage());
        host_need_root();
        host_each_entry((string_address)HOST_RECORD_DIR, host_canvas_shot_newest, before);
        if (host_write_file((string_address)HOST_RECORD_DIR "/shot", (p8 address_to) "1\n", 2, 0644, false) < 0)
                return host_refuse("the desktop is not running: the compositor's record is not there (%s)\n", (string_address)HOST_RECORD_DIR);
        for (positive wait = 0; wait < 50; wait++)
        {
                host_pause(100000000);
                string_copy(after, before);
                host_each_entry((string_address)HOST_RECORD_DIR, host_canvas_shot_newest, after);
                if (!string_equals((string_address)after, (string_address)before))
                {
                        host_say(log, "%s/%s\n", (string_address)HOST_RECORD_DIR, (string_address)after);
                        return 0;
                }
        }
        return host_refuse("the compositor wrote no shot in five seconds (it writes one at a present: nothing on the screen moved)\n");
}

/*
        moonwater canvas film SECONDS [X Y W H]: every present of the compositor for SECONDS (1 to 60), the rectangle X Y W H of the screen or
        all of it, to /run/moonwater/record/film.raw (src/canvas/record.h says its form; test/perf/film.py makes pictures of it). This asks
        for it and says where it goes; film.txt there says when it is done.
*/
static b32 host_canvas_film(string_address address_to arguments, positive count)
{
        p8 line[96];
        positive at = 0;

        return_if(count != 4 && count != 8, host_usage());
        for (positive word = 3; word < count; word++)
        {
                positive used = 0, value = string_digits_max(arguments[word], 5, address_of used);

                return_if(!used || arguments[word][used] || (word == 3 && (value < 1 || value > 60)), host_usage());
                at += positive_into_string(line + at, value);
                line[at++] = word + 1 < count ? ' ' : '\n';
        }
        host_need_root();
        if (host_write_file((string_address)HOST_RECORD_DIR "/film", line, at, 0644, false) < 0)
                return host_refuse("the desktop is not running: the compositor's record is not there (%s)\n", (string_address)HOST_RECORD_DIR);
        host_say(log, "filming every present to %s/film.raw; %s/film.txt says when it is done\n", (string_address)HOST_RECORD_DIR,
                 (string_address)HOST_RECORD_DIR);
        return 0;
}

/*
        moonwater canvas keys SECONDS: every key pressed for SECONDS (1 to 60) followed from the kernel's report to the vblank that showed what its
        window drew of it, each stage stamped by the compositor (src/canvas/record.h); /run/moonwater/record/keys.txt has a line a key at the end.
*/
static b32 host_canvas_timed(string_address address_to arguments, positive count,
                             string_address path, string_address message)
{
        p8 line[16];
        positive at, used = 0, value;

        return_if(count != 4, host_usage());
        value = string_digits_max(arguments[3], 2, address_of used);
        return_if(!used || arguments[3][used] || value < 1 || value > 60, host_usage());
        at = positive_into_string(line, value);
        line[at++] = '\n';
        host_need_root();
        if (host_write_file(path, line, at, 0644, false) < 0)
                return host_refuse("the desktop is not running: the compositor's record is not there (%s)\n", (string_address)HOST_RECORD_DIR);
        host_say(log, message, (string_address)HOST_RECORD_DIR);
        return 0;
}

static b32 host_canvas_keys(string_address address_to arguments, positive count)
{
        return host_canvas_timed(arguments, count, (string_address)HOST_RECORD_DIR "/keys",
                                 "following every key to the screen; %s/keys.txt has them when the time is up and the screen next changes\n");
}

static b32 host_canvas_frames(string_address address_to arguments, positive count)
{
        return host_canvas_timed(arguments, count, (string_address)HOST_RECORD_DIR "/frames",
                                 "recording every frame; %s/frames.txt has them when the time is up and the screen next changes\n");
}

static b32 host_canvas(string_address address_to arguments, positive count)
{
        struct canvas_control control;
        bipolar failed;

        //      The two settings are the machine's, kept with the others in
        //      /root: they are asked and set in the locale's section.
        if (count >= 3 && string_equals(arguments[2], "modes"))
                return locale_canvas_setting(arguments, count);
        if (count >= 3 && string_equals(arguments[2], "cursor"))
                return locale_canvas_cursor(arguments, count);
        if (count >= 3 && string_equals(arguments[2], "windows"))
                return locale_canvas_windows(arguments, count);
        if (count >= 3 && string_equals(arguments[2], "arrange"))
                return locale_canvas_arrange(arguments, count);
        if (count >= 3 && string_equals(arguments[2], "display"))
                return locale_canvas_display(arguments, count);
        if (count >= 3 && string_equals(arguments[2], "identify"))
                return host_canvas_identify(count);
        if (count >= 3 && string_equals(arguments[2], "record"))
                return host_canvas_record(arguments, count);
        if (count >= 3 && string_equals(arguments[2], "shot"))
                return host_canvas_shot(count);
        if (count >= 3 && string_equals(arguments[2], "film"))
                return host_canvas_film(arguments, count);
        if (count >= 3 && string_equals(arguments[2], "keys"))
                return host_canvas_keys(arguments, count);
        if (count >= 3 && string_equals(arguments[2], "frames"))
                return host_canvas_frames(arguments, count);

        return_if(count > 3, host_usage());

        if (count < 3)
        {
                failed = host_canvas_request(SPARK_CANVAS_STATUS, address_of control);
                return_if(failed < 0, host_fail(SPARK_DEVICE, failed));

                host_canvas_say(address_of control);
                return 0;
        }

        // Turning Canvas on and off is the desktop's: moonwater desktop on|off.
        return host_usage();
}

/*
        moonwater latency [reset]: where a report from a mouse or a keyboard
        spends its time on this machine, as far as the machine can see it.

        The two input handlers Moonwater attaches to every device are counted
        by the kernel and one report in sixteen is timed, so what each costs a
        report is a figure here and not a guess: Canvas's, which hears the
        modifiers and Backspace, the chords that end a desktop, and returns at
        once for every other report whoever has the display, and the machine's
        key watch, which fires the bound events. The stages
        after the kernel (the compositor, the screen) are not the machine's
        to see. reset
        zeroes the counts, so a minute of moving a mouse is a reset, the
        minute and a bare read.
*/
static fn latency_cost_say(string_address name, const struct latency_cost address_to cost,
                           positive elapsed_ms)
{
        string_format(log, host_label "  %s: %p reports", name, (positive)cost->events);
        if (elapsed_ms)
                string_format(log, " (%p a second)", (positive)(cost->events * 1000 / elapsed_ms));
        if (cost->samples)
                string_format(log, ", %p ns each and %p at worst, from %p timed\n",
                              (positive)(cost->total_ns / cost->samples),
                              (positive)cost->worst_ns, (positive)cost->samples);
        else
                string_format(log, ", none timed yet\n");
}

static b32 host_latency(string_address address_to arguments, positive count)
{
        struct latency_stats stats;
        bipolar failed;
        positive elapsed_ms;
        bool reset = count == 3 && string_equals(arguments[2], "reset");

        return_if(count > 3 || (count == 3 && !reset), host_usage());
        host_need_root();

        memory_zero(address_of stats, sizeof(stats));
        stats.request = reset ? SPARK_LATENCY_RESET : SPARK_LATENCY_READ;
        failed = host_spark_once(SPARK_IOCTL_LATENCY, address_of stats, FILE_READ);
        return_if(failed == -EPERM, host_refuse("moonwater latency needs root (CAP_SYS_ADMIN)\n"));
        return_if(failed < 0, host_fail(SPARK_DEVICE, failed));

        if (reset)
        {
                host_say(log, host_label "counts zeroed; moonwater latency reads them\n");
                return 0;
        }

        elapsed_ms = (positive)(stats.elapsed_ns / 1000000);
        string_format(log, host_label "input reports, over %p s since the counts began:\n",
                      (positive)(elapsed_ms / 1000));
        latency_cost_say("Canvas's handler", address_of stats.canvas, elapsed_ms);
        latency_cost_say("the key watch", address_of stats.bind, elapsed_ms);

        string_format(log, host_label "Canvas's handler returned at once for %p of its reports "
                                      "(every one that is no modifier or Backspace), with no lock "
                                      "taken and nothing woken%s\n",
                      (positive)stats.quiet,
                      stats.yielded ? "; another program has the display" : "");

        log_flush();
        return 0;
}

/*
        moonwater bios: restart into the firmware's own setup screen.

        UEFI firmware asks for it through one bit. OsIndicationsSupported
        says whether the firmware has a setup screen it can start at the next
        boot (bit 0), and OsIndications is where the operating system sets
        that bit; the firmware acts on it and clears it. Both are EFI
        variables, files of the efivarfs filesystem: four bytes of attributes,
        then the value. A machine that did not start from UEFI has neither, and
        nothing here could take it to a setup screen, so that is said instead
        of a reboot that lands where it always did.

        The variable is written last, and the machine is stopped through the
        same path poweroff and reboot take, so the disks are synced and what
        `moonwater exit` runs has run. If the machine does not stop, the
        bit is left set and the next restart of any kind goes to setup.
*/
#define HOST_EFI_DIRECTORY "/sys/firmware/efi/efivars"
#define HOST_EFI_SUPPORTED HOST_EFI_DIRECTORY "/OsIndicationsSupported-8be4df61-93ca-11d2-aa0d-00e098032b8c"
#define HOST_EFI_INDICATIONS HOST_EFI_DIRECTORY "/OsIndications-8be4df61-93ca-11d2-aa0d-00e098032b8c"
#define HOST_EFI_BOOT_TO_SETUP 1u
//      Non-volatile, boot services and runtime: what the firmware's own
//      OsIndications carries, and what it refuses a write without.
#define HOST_EFI_ATTRIBUTES 7u
#define HOST_FS_GETFLAGS 0x80086601u
#define HOST_FS_SETFLAGS 0x40086602u
#define HOST_FS_IMMUTABLE 0x10u

static bipolar host_efi_get(string_address path, p64 address_to value)
{
        p8 bytes[16];
        bipolar got = file_slurp_once_at(AT_FDCWD, path, bytes, sizeof(bytes));

        if (got < 0)
                return got;
        if (got != 12)
                return -ERROR_INVALID;
        memory_copy(value, bytes + 4, 8);
        return 0;
}

/* Whether efivarfs is the filesystem at HOST_EFI_DIRECTORY already. */
static bool host_efi_mounted(void)
{
        file_mount_facts facts;

        if (system_call_2(syscall(statfs), (positive)HOST_EFI_DIRECTORY,
                          (positive)address_of facts) < 0)
                return false;
        return facts.type == 0xDE5E81E4;
}

/* Whether the firmware offers its setup at the next boot: 1 yes, 0 not, and
   negative when this is not UEFI or the variables cannot be read. */
static bipolar host_efi_setup_offered(void)
{
        p64 value = 0;
        bipolar handle = system_open_at(AT_FDCWD, "/sys/firmware/efi", FILE_READ | O_CLOEXEC);
        bipolar failed;

        if (handle < 0)
                return -ENODEV;
        system_close((positive)handle);

        failed = host_efi_get(HOST_EFI_SUPPORTED, address_of value);
        //      A machine that has not mounted efivarfs yet. Only that: a mount
        //      on one already there stacks, and a read that fails for another
        //      reason (the variable is absent) would stack one more at every
        //      ask, a status read included.
        if (failed < 0 && !host_efi_mounted())
        {
                system_mount("efivarfs", HOST_EFI_DIRECTORY, "efivarfs", 0, 0);
                failed = host_efi_get(HOST_EFI_SUPPORTED, address_of value);
        }
        if (failed < 0)
                return failed;
        return (value & HOST_EFI_BOOT_TO_SETUP) != 0;
}

static b32 host_bios_set(void)
{
        p64 indications = 0;
        p8 image[12];
        p32 attributes = HOST_EFI_ATTRIBUTES;
        bipolar handle;
        bipolar failed;
        positive flags = 0;

        //      The bits the firmware already had set stay set. A variable that
        //      is not there has none; one that is there and cannot be read has
        //      bits not known, and writing it from zero would clear them.
        failed = host_efi_get(HOST_EFI_INDICATIONS, address_of indications);
        return_if(failed < 0 && failed != -ENOENT, host_fail("firmware setup", failed));
        indications |= HOST_EFI_BOOT_TO_SETUP;
        memory_copy(image, address_of attributes, 4);
        memory_copy(image + 4, address_of indications, 8);

        //      efivarfs marks a variable immutable, and a write to one is
        //      refused until the mark is cleared; a variable that does not
        //      exist yet has nothing to clear.
        handle = system_open_at(AT_FDCWD, HOST_EFI_INDICATIONS, FILE_READ | O_CLOEXEC);
        if (handle >= 0)
        {
                if (system_call_3(syscall(ioctl), (positive)handle, HOST_FS_GETFLAGS,
                                  (positive)address_of flags) == 0 &&
                    (flags & HOST_FS_IMMUTABLE))
                {
                        flags &= ~(positive)HOST_FS_IMMUTABLE;
                        system_call_3(syscall(ioctl), (positive)handle, HOST_FS_SETFLAGS,
                                      (positive)address_of flags);
                }
                system_close((positive)handle);
        }

        handle = system_open_at_mode(AT_FDCWD, HOST_EFI_INDICATIONS,
                                     O_WRONLY | FILE_CREATE | O_CLOEXEC | O_NOFOLLOW, 0644);
        return_if(handle < 0, host_fail("firmware setup", handle));
        if (system_write_all((positive)handle, image, sizeof(image)) != sizeof(image))
        {
                system_close((positive)handle);
                return host_fail("firmware setup", -EIO);
        }
        system_close((positive)handle);
        return 0;
}

/* moonwater bios [reboot] */
static b32 host_bios(string_address address_to arguments, positive count)
{
        bipolar offered;

        if (count > 3 || (count == 3 && !string_equals(arguments[2], "reboot")))
                return host_usage();

        offered = host_efi_setup_offered();

        if (count == 2)
        {
                if (offered > 0)
                        string_format(log, host_label "UEFI firmware with a setup screen; "
                                                     "moonwater bios reboot restarts into it\n");
                else if (offered == 0)
                        string_format(log, host_label "UEFI firmware, but it offers no setup "
                                                     "screen at the next boot\n");
                else if (offered == -ENODEV)
                        string_format(log, host_label "this machine did not start from UEFI "
                                                     "firmware, so there is no setup to restart into\n");
                else
                        string_format(log, host_label "UEFI firmware, but its variables cannot "
                                                     "be read here (error %p)\n",
                                      (positive)-offered);
                log_flush();
                return 0;
        }

        host_need_root();
        if (offered == -ENODEV)
                return host_refuse("this machine did not start from UEFI firmware, so "
                                   "there is no setup to restart into\n");
        return_if(offered < 0, host_fail("firmware setup", offered));
        if (!offered)
                return host_refuse("this firmware offers no setup screen at the next boot\n");

        if (host_bios_set())
                return 1;

        host_say(log, host_label "restarting into the firmware's setup\n");
        shell_stop(log, REBOOT_RESTART);

        //      Only reached when the machine did not stop.
        return 1;
}

