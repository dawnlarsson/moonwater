/*
        src/sh/host/desktop.c: the verb desktop: the desktop a machine starts, and which one.

        A run of src/sh/host.c, which includes this file where the run stood (test/split.py cut it there): it is
        compiled only as part of that file, with its statics and macros, and never on its own. host_main, at the end
        of src/sh/host.c, names the function each verb runs.
*/

/*
        moonwater desktop: what is on the screen once the machine has started.

        The compositor is the desktop unless this says otherwise. A bowl profile's
        session can be the desktop instead (bowl profile desktop installs one):
        it takes the display from Canvas the way any program that opens the card
        does, Canvas lets go and drops the keys while it holds it, and has the
        keyboard back when it ends. Canvas still starts first. off is the text
        console: Canvas is turned off as soon as it has started.

        The choice is /root/desktop, as the keyboard's is /root/keyboard. The
        machine script's canvas on line runs `desktop boot`, once for each boot,
        which waits for the verdict so that /bowls is there and starts the
        choice in a session of its own, and `desktop start` and `desktop stop`
        are the same by hand. A session is its leader's number, which is its
        session's too, and stopping is ending everything in it, again and again
        until nothing is: KDE's wrapper starts KWin again when only KWin ends.
*/
#define DESKTOP_PATH "/root/desktop"
#define DESKTOP_SESSION HOST_STATE "/desktop"
#define DESKTOP_BOOTED HOST_STATE "/desktop.booted"
#define DESKTOP_LOG HOST_STATE "/desktop.log"
#define DESKTOP_MEMBERS 1024
#define DESKTOP_GRACE_PASSES 5
#define DESKTOP_PASSES 25
#define DESKTOP_PASS_NS ((p64)200000000)

static string_address desktop_choice(p8 address_to name, positive room)
{
        if (host_read_word(DESKTOP_PATH, name, room) <= 0 || !name[0])
                string_copy_bounded(name, "canvas", room);
        return (string_address)name;
}

/* Whether the profile's session script is where `bowl profile` puts it. */
static bool desktop_installed(string_address name)
{
        p8 path[HOST_PATH_ROOM];
        p8 want[HOST_NAME_ROOM + 40];
        p8 held[HOST_NAME_ROOM + 40];
        bipolar got;

        return_if(!bowl_find_profile(name) || bowl_find_profile(name)->program ||
                      !host_join(path, sizeof(path), BOWL_EXPOSE_DIRECTORY "/", name) ||
                      !bowl_profile_header(want, sizeof(want), name),
                  false);

        got = file_read_once_at(AT_FDCWD, path, held, sizeof(held));
        return got >= (bipolar)string_length(want) &&
               !string_compare_max(held, want, string_length(want));
}

static positive desktop_scan(positive session, p32 address_to pids, positive room,
                             bool address_to leader);

static bool desktop_named(const struct snapshot_process address_to process, string_address name)
{
        return !string_compare_max((string_address)process->command, name, string_length(name));
}

static const struct snapshot_process address_to desktop_parent(const system_snapshot address_to sample,
                                                                const struct snapshot_process address_to child)
{
        for (positive at = 0; at < sample->header.process_count; at++)
                if (sample->processes[at].pid == child->ppid && child->ppid)
                        return sample->processes + at;
        return null;
}

// The first KWin that is not a zombie, or null.
static const struct snapshot_process address_to desktop_kwin(const system_snapshot address_to sample)
{
        for (positive at = 0; at < sample->header.process_count; at++)
                if (sample->processes[at].state != 'Z' && desktop_named(sample->processes + at, "kwin_wayland"))
                        return sample->processes + at;
        return null;
}

// The top of the chain that runs a KWin (the session bus runner, startplasma, the compositor): 0 when none is.
static positive desktop_root(const system_snapshot address_to sample)
{
        const struct snapshot_process address_to top = desktop_kwin(sample);

        for (positive hops = 0; top && hops < 16; hops++)
        {
                const struct snapshot_process address_to above = desktop_parent(sample, top);

                if (!above || !(desktop_named(above, "dbus-run-sess") || desktop_named(above, "startplasma") ||
                                desktop_named(above, "plasma_session") || desktop_named(above, "kwin_wayland")))
                        break;
                top = above;
        }
        return top ? top->pid : 0;
}

// Whether a process is the root or one of its descendants.
static bool desktop_below(const system_snapshot address_to sample, const struct snapshot_process address_to process,
                          positive root)
{
        for (positive hops = 0; process && hops < 64; hops++)
        {
                if (process->pid == root)
                        return true;
                process = desktop_parent(sample, process);
        }
        return false;
}

// The session of a KWin that is running, 0 when none is: how a session nobody recorded is found.
static positive desktop_compositor_session(void)
{
        static system_snapshot sample;
        const struct snapshot_process address_to kwin;

        return_if(!system_snapshot_take(address_of sample, SPARK_SNAPSHOT_PROCESS, false), 0);
        kwin = desktop_kwin(address_of sample);
        return kwin ? kwin->session : 0;
}

/*
        The session desktop off and stop end: the one the verb recorded, and where nothing of it is left (or none was
        recorded, as when a session was started some other way) the session of the KWin that is running. A session
        that was in no record was left running by desktop off, with Canvas gone: its compositor and the session bus
        held the keyboard, and the console came back black and deaf until a reboot.
*/
static positive desktop_session(void)
{
        p8 word[24];
        p32 member;
        bool leader;
        positive recorded = 0;
        positive found;

        if (host_read_word(DESKTOP_SESSION, word, sizeof(word)) < 0 ||
            !string_digits_checked_exact(word, 10, address_of recorded) || recorded > 0x7fffffff)
                recorded = 0;

        return_if(recorded && desktop_scan(recorded, address_of member, 1, address_of leader), recorded);
        found = desktop_compositor_session();
        return found ? found : recorded;
}

/*
        The processes of a session: how many there are, the first room of them
        in pids, and whether its own leader is one. A session that was logged
        out of leaves what it started behind it, PipeWire and its manager among
        them, with no leader and nothing on the screen: that is not a desktop
        running, though it is something stop still ends.
*/
static positive desktop_scan(positive session, p32 address_to pids, positive room,
                             bool address_to leader)
{
        static system_snapshot sample;
        positive found = 0;

        address_to leader = false;
        return_if(!session ||
                      !system_snapshot_take(address_of sample, SPARK_SNAPSHOT_PROCESS, false),
                  0);

        //      Session 1 is init's, which every program of the machine is in and no session of a desktop's own: a
        //      desktop found there (started from a shell that is init's, as a serial console is) is the chain that
        //      runs its KWin and what is below it.
        positive root = session == 1 ? desktop_root(address_of sample) : 0;

        return_if(session == 1 && !root, 0);
        for (positive at = 0; at < sample.header.process_count; at++)
        {
                const struct snapshot_process address_to process = sample.processes + at;

                if (process->state == 'Z' ||
                    (session == 1 ? !desktop_below(address_of sample, process, root) : process->session != session))
                        continue;

                address_to leader |= process->pid == (session == 1 ? root : session);
                if (found < room)
                        pids[found] = process->pid;
                found++;
        }

        return found;
}

/* The session script of a profile, started where nothing else is. */
static b32 desktop_launch(string_address name)
{
        p8 text[HOST_PATH_ROOM];
        p8 number[24];
        bipolar output;
        bipolar child;

        host_state_ready();
        return_if(!host_join(text, sizeof(text), "export MOONWATER_SESSION=1; exec " BOWL_EXPOSE_DIRECTORY "/", name),
                  host_refuse("%s is too long a name\n", name));

        output = host_open_state(AT_FDCWD, DESKTOP_LOG, 0600, FILE_WRITE);
        child = host_event_start(text, output);
        if (output >= 0)
                system_close(output);
        return_if(child < 0, host_fail("desktop", child));

        positive_into_string(number, (positive)child);
        //      Started, and said that it is not recorded: desktop stop and off find
        //      a session by this number, or by its KWin where there is one.
        output = host_write_text(DESKTOP_SESSION, (string_address)number);
        return_if(output < 0, host_fail(DESKTOP_SESSION, output));
        return 0;
}

/*
        Every process of the last session, ended: asked to finish for a second,
        then killed, until none is left. Done from a process of its own in a
        session of its own, since the shell that asks is often one of the
        session's. Whether it did, as a status.
*/
static b32 desktop_end(void)
{
        positive session = desktop_session();
        p32 pids[DESKTOP_MEMBERS];
        bipolar child = system_fork();
        positive status = 0;
        bool leader;

        return_if(child < 0, host_fail("desktop", child));

        if (child)
        {
                system_wait4_retry(child, address_of status, 0, null);
                if (wait_status_code(status))
                        return 1;
                //      The session is gone, and its number with it: left in the
                //      record it is the number of whatever is given it next, and
                //      stop, off and the next profile would end that instead.
                (void)system_remove_at(AT_FDCWD, DESKTOP_SESSION, 0);
                return 0;
        }

        system_call(syscall(setsid));
        for (positive pass = 0; pass < DESKTOP_PASSES; pass++)
        {
                positive found = desktop_scan(session, pids, DESKTOP_MEMBERS, address_of leader);

                if (!found)
                        system_call_1(syscall(exit), 0);

                for (positive at = 0; at < found && at < DESKTOP_MEMBERS; at++)
                        system_call_2(syscall(kill), pids[at],
                                      pass < DESKTOP_GRACE_PASSES ? SIGTERM : SIGKILL);
                host_pause(DESKTOP_PASS_NS);
        }

        system_call_1(syscall(exit), 1);
        return 1;
}

#define DESKTOP_SUPERVISOR HOST_STATE "/supervisor.pid"
#define DESKTOP_COMPOSITOR HOST_STATE "/compositor.pid"
#define DESKTOP_KIDS HOST_STATE "/desktop.kids"
//      The room the list of pids the machine script keeps is read into, and the
//      most digits a number of it may have to be a pid (the kernel's most is 7).
#define DESKTOP_KIDS_ROOM 256
#define DESKTOP_PID_DIGITS 9

// The pid a file of the machine script holds, or 0: a whole number that fits what kill takes it as, and
// nothing after its digits (4294967295 is -1, which every process answers to, and "123abc" is no pid).
static positive desktop_pid_of(string_address path)
{
        p8 word[24];
        string_address stop;
        b32 range = 0;
        positive pid;

        return_if(host_read_word(path, word, sizeof(word)) <= 0, 0);
        stop = (string_address)word;
        pid = string_to_number_unsigned_checked((string_address)word, address_of stop, 10, address_of range);
        return *stop || range || pid > 0x7fffffff ? 0 : pid;
}

static bool desktop_alive(positive pid)
{
        return pid > 1 && system_call_2(syscall(kill), pid, 0) == 0;
}

/*
        The machine woke (moonwater sleep and hibernate, awake again): the compositor of the machine's own desktop is told
        with SIGUSR2, so it takes the screens again at once and does not wait for a key or the clocks to say it slept
        (compositor_woke). Whether there was one to tell.
*/
#define DESKTOP_SIGUSR2 12
static bool desktop_woke(void)
{
        positive compositor = desktop_pid_of((string_address)DESKTOP_COMPOSITOR);

        return desktop_alive(compositor) && system_call_2(syscall(kill), compositor, DESKTOP_SIGUSR2) == 0;
}

/*
        The compositor of the machine's own desktop, ended for another desktop to have the display. A profile's
        session (KWin), weston or any other compositor cannot become the card's master while this one is: KWin
        says it has no outputs and Plasma that it has no screen to put a panel on. The supervisor goes first so that
        the compositor is not started again, as the machine script's desktop_stop does it, and what the compositor
        carried (the log, the terminals, the key answerer) goes after it, the programs the script kept the pids of.
        Nothing is done where no compositor is up.
*/
static fn desktop_yield(void)
{
        positive supervisor = desktop_pid_of((string_address)DESKTOP_SUPERVISOR),
                 compositor = desktop_pid_of((string_address)DESKTOP_COMPOSITOR);
        p8 kids[DESKTOP_KIDS_ROOM];
        bipolar got;
        bool cut;

        if (supervisor > 1)
                system_call_2(syscall(kill), supervisor, SIGTERM);
        if (compositor > 1)
                system_call_2(syscall(kill), compositor, SIGTERM);
        for (positive pass = 0; pass < 60 && desktop_alive(compositor); pass++)
                host_pause(50000000);
        if (desktop_alive(compositor))
        {
                system_call_2(syscall(kill), compositor, SIGKILL);
                host_pause(100000000);
        }

        got = file_read_once_at(AT_FDCWD, (p8 address_to)DESKTOP_KIDS, kids, sizeof(kids));
        //      A read that filled the room may have more behind it, and a number
        //      at its end may be the front of a longer one: not a pid, and not
        //      to be killed as the one it looks like. A number of more digits
        //      than a pid has is none either, and is not split into two.
        cut = got == (bipolar)sizeof(kids);
        for (positive at = 0, pid = 0, digits = 0; got > 0 && at <= (positive)got; at++)
        {
                p8 byte = at < (positive)got ? kids[at] : '\n';

                if (byte >= '0' && byte <= '9')
                {
                        if (digits < DESKTOP_PID_DIGITS)
                                pid = pid * 10 + (positive)(byte - '0');
                        if (digits <= DESKTOP_PID_DIGITS)
                                digits++;
                        continue;
                }
                if (digits && digits <= DESKTOP_PID_DIGITS && pid > 1 && !(cut && at == (positive)got))
                        system_call_2(syscall(kill), pid, SIGTERM);
                pid = digits = 0;
        }
        (void)system_remove_at(AT_FDCWD, DESKTOP_SUPERVISOR, 0);
        (void)system_remove_at(AT_FDCWD, DESKTOP_COMPOSITOR, 0);
        (void)system_remove_at(AT_FDCWD, DESKTOP_KIDS, 0);
}

// Whether Canvas is running in the kernel: -1 when it cannot be asked.
static int desktop_canvas_running(void)
{
        struct canvas_control control;

        return host_canvas_request(SPARK_CANVAS_STATUS, address_of control) < 0 ? -1 : control.running != 0;
}

/*
        The choice, put in force. on is the compositor: a session of a profile ends, and Canvas is turned on if it is
        off, or off and on if it is on with no compositor (the machine script starts the compositor from the event
        that turns it on). off is the text console: a session ends and Canvas is turned off. A profile is its
        session, over Canvas, with the compositor out of its way. At boot the machine script has started the
        compositor already, or has not for a profile, so on is nothing then.
*/
static b32 desktop_put(string_address name, bool boot)
{
        p32 pid;
        bool leader;
        positive left;

        if (string_equals(name, "canvas") || string_equals(name, "off"))
        {
                bool want = string_equals(name, "canvas");

                return_if(want && boot, 0);
                left = desktop_scan(desktop_session(), address_of pid, 1, address_of leader);
                return_if(left && desktop_end(), host_refuse("what is left of the last session would not end\n"));

                if (!want)
                {
                        //      The machine script ends the compositor from the event Canvas off fires; the card is
                        //      free when it has gone, and a compositor started straight after (weston, a session)
                        //      is refused its master until then.
                        positive compositor = desktop_pid_of((string_address)DESKTOP_COMPOSITOR);

                        return_if(desktop_canvas_running() <= 0, 0);
                        return_if(host_canvas_off(), 1);
                        for (positive pass = 0; pass < 100 && desktop_alive(compositor); pass++)
                                host_pause(50000000);
                        host_console_redraw();
                        return 0;
                }
                if (desktop_canvas_running() <= 0)
                        return host_canvas_on();
                return_if(desktop_alive(desktop_pid_of((string_address)DESKTOP_COMPOSITOR)), 0);
                return_if(host_canvas_off(), 1);
                return host_canvas_on();
        }

        return_if(!desktop_installed(name),
                  host_refuse("the profile %s is not installed: bowl profile installs it\n", name));
        (void)bowl_profile_refresh(bowl_find_profile(name));
        left = desktop_scan(desktop_session(), address_of pid, 1, address_of leader);
        return_if(leader, host_refuse("the desktop %s is already running\n", name));
        return_if(left && desktop_end(),
                  host_refuse("what is left of the last session would not end\n"));

        // The compositor of the machine's own desktop has the card's master, and a session cannot have the display
        // until it lets go: not at boot, where none was started for a profile, but when the choice is made on a
        // machine that is up.
        desktop_yield();
        return desktop_launch(name);
}

static b32 desktop_start(bool boot)
{
        p8 name[HOST_NAME_ROOM];

        desktop_choice(name, sizeof(name));
        return desktop_put((string_address)name, boot);
}

/*
        From the machine script, when Canvas has started: the choice, once for
        a boot, in a process of its own that waits for the disks. The script
        goes on to its next line at once: the events behind it wait on it.
*/
static b32 desktop_boot(void)
{
        bipolar made;
        bipolar child;

        host_need_root();
        host_state_ready();
        made = system_open_at_mode(AT_FDCWD, DESKTOP_BOOTED,
                                   FILE_WRITE | FILE_EXCLUSIVE | O_NOFOLLOW | O_CLOEXEC,
                                   0600);
        return_if(made < 0, 0);
        system_close(made);
        child = system_fork();
        if (child < 0)
        {
                //      The marker goes with the failed start, so that a boot that asks again is not told it was done.
                (void)system_remove_at(AT_FDCWD, DESKTOP_BOOTED, 0);
                return host_fail("desktop", child);
        }
        return_if(child, 0);

        system_call(syscall(setsid));
        host_verdict_settled();
        system_call_1(syscall(exit), (positive)desktop_start(true));
        return 0;
}

static b32 desktop_stop(void)
{
        p32 pid;
        bool leader;

        host_need_root();
        return_if(!desktop_scan(desktop_session(), address_of pid, 1, address_of leader),
                  host_refuse("no desktop session is running\n"));
        return_if(desktop_end(), host_refuse("the desktop session would not end\n"));
        host_say(log, host_label "desktop stopped\n");
        return 0;
}

static b32 desktop_status(void)
{
        p8 name[HOST_NAME_ROOM];
        p32 pid;
        bool leader;

        desktop_choice(name, sizeof(name));
        if (string_equals(name, "canvas") || string_equals(name, "off"))
                host_say(log, host_label "desktop %s\n", string_equals(name, "canvas") ? (string_address) "on" : (string_address)name);
        else if (!desktop_installed(name))
                host_say(log, host_label "desktop %s, not installed: bowl profile %s "
                                         "installs it\n", (string_address)name,
                         (string_address)name);
        else
        {
                desktop_scan(desktop_session(), address_of pid, 1, address_of leader);
                host_say(log, host_label "desktop %s, %s\n", (string_address)name,
                         leader ? "running" : "not running");
        }
        return 0;
}

/* moonwater desktop [on|off|PROFILE|start|stop|rest]: the choice kept and put in force now. rest is off put in force and
   not kept: the text console until the next start or `desktop on`, for a desktop that would not stay up on this boot. The
   machine script's supervisor said `desktop off` after four quick ends of the compositor, which is kept, so one bad boot (a
   card that came up late, a full disk) left every later one on the text console until somebody typed `desktop on`. */
static b32 host_desktop(string_address address_to arguments, positive count)
{
        string_address word = count > 2 ? arguments[2] : null;
        string_address kept;

        return_if(count == 2, desktop_status());
        return_if(count != 3, host_usage());

        return_if(string_equals(word, "boot"), desktop_boot());
        if (string_equals(word, "start"))
        {
                host_need_root();
                return desktop_start(false);
        }
        return_if(string_equals(word, "stop"), desktop_stop());
        if (string_equals(word, "rest"))
        {
                host_need_root();
                host_say(log, host_label "desktop off until the next start; moonwater desktop on starts it again\n");
                return desktop_put("off", false);
        }

        host_need_root();
        kept = string_equals(word, "on") ? (string_address) "canvas" : word;
        if (!string_equals(word, "on") && !string_equals(word, "off"))
        {
                return_if(!bowl_find_profile(word),
                          host_refuse("unknown desktop %s: on, off, or a profile of "
                                      "bowl profile\n", word));
                return_if(bowl_find_profile(word)->program,
                          host_refuse("%s is a program, not a desktop: it runs on Canvas, "
                                      "which stays the compositor\n", word));
                return_if(!desktop_installed(word),
                          host_refuse("the profile %s is not installed: bowl profile "
                                      "installs it\n", word));
        }

        //      The choice is kept before it is put in force, and stays kept when
        //      putting it is refused (a session that would not end, a desktop
        //      already running): it is what every start does, and the verb is
        //      the person's word for that first and for this moment second. Kept
        //      after, the word of whoever asked for off while a session hung
        //      would be lost, and the next start would not honour it.
        return_if(host_save("desktop", DESKTOP_PATH, kept), 1);
        host_say(log, host_label "desktop %s\n", word);
        return desktop_put(kept, false);
}

