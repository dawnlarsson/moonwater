/*
        init is what the kernel execs.

        It remains a command in the multicall binary, but its responsibility is
        the Moonwater host. Distribution-root policy belongs to src/bowl.
*/

// init -----------------------------------------------------------
#define init_label TERM_BOLD "[Init]" TERM_RESET " "
#define init_program "/shell"

// A shell that dies immediately would otherwise be restarted as fast as the
// machine can fork, forever. Backing off turns that into something a person
// can read and interrupt rather than a spin.
#define RESTART_BACKOFF_NS 250000000
#define RESTART_BACKOFF_MAX_NS 4000000000
#define RESTART_BACKOFF_AFTER 3

// A shell that stayed up this long was doing its job, so whatever came before
// it is not a crash loop and the backoff starts over.
#define SHELL_SETTLED_NS 2000000000

#ifndef CLOCK_MONOTONIC
#define CLOCK_MONOTONIC 1
#endif

// Raw kernel return values: there is no errno here, a failed call comes back
// as the negated error itself.
#define ERROR_NO_CHILDREN (-10)

// PID 1 has two jobs: start the first program, and reap every orphan the
// system ever produces. It must not exec into the shell -- doing that makes
// the shell PID 1, so the kernel panics the moment the shell exits, and
// nothing is left to reap anything.

static string_address init_argv[] = {init_program, null};
//      What init hands what it starts: not the kernel's HOME=/ and
//      TERM=linux, but root's home, which the shell no longer makes up for
//      itself when nothing gave it one.
static string_address init_envp[] = {"HOME=/root", null};

/*
        The network, brought up by the system rather than by whoever logs in.

        ip watch configures whatever is plugged in and then waits on a netlink
        socket for a link to gain or lose carrier, so a machine that boots with
        a cable in is on the network before the prompt appears, and one whose
        cable is moved follows it without anybody typing anything.

        It is started here rather than from the shell's profile because it is
        not a shell thing: a machine with no interactive session should still
        be reachable, and a shell that exits should not take the network with
        it.

        /ip is the shell under another name, like every other utility, so this
        costs no second binary.
*/
#define network_program "/ip"

static string_address network_argv[] = {(string_address)network_program,
                                 (string_address) "watch", null};

/*
        The disks, settled before any shell starts.

        moonwater boot finds an installed Moonwater and mounts its data over
        the directories it keeps, or leaves a question for the first terminal.
        The shell waits for it because a shell that started first would have
        read /root before the disk's /root was there -- but only so long: a
        service stuck on a bad disk costs this wait and no more, and the
        shell starts anyway.
*/
#define settle_program "/moonwater"
#define SETTLE_WAIT_NS 20000000000
#define WAIT_NO_HANG 1

static string_address settle_argv[] = {(string_address)settle_program,
                                (string_address) "boot", null};

/*
        The machine script. Started with the network, not after the disks:
        the kernel already holds the builtin fallback, and the process waits
        for a boot verdict before overlaying /root/main.moonwater.sh. /shell
        -c once at boot, not per event: the kernel queues into that process
        while it is attached. A clean 0 or 1 is an orderly stop or a refused
        attach; those are not a crash loop.
*/
#define machine_program "/shell"
#define MACHINE_SETTLED_NS 1000000000
#define MACHINE_GIVE_UP 5

static string_address machine_argv[] = {(string_address)machine_program,
                                 (string_address) "-c",
                                 (string_address) "moonwater machine", null};

//      A watcher that dies this quickly is not going to work on the next try
//      either -- a missing /ip, most likely -- so say so once and stop.
#define NETWORK_SETTLED_NS 1000000000
#define NETWORK_GIVE_UP 3

/* Start every boot service through the shared final-executable decision.
   The old direct Spark request skipped target-specific run, flag and network
   policy.  Boot starts two long-lived services, so the one fork also removes
   a private launch backend without affecting steady-state performance. */
static bipolar start_service(string_address path,
                             string_address address_to arguments,
                             positive count)
{
        bipolar child = system_fork();

        if (child == 0)
        {
                (void)shell_exec_file(path, arguments, count, init_envp);
                system_call_1(syscall(exit), 127);
        }

        return child;
}

static bipolar start_network()
{
        return start_service((string_address)network_program,
                             network_argv, 2);
}

static bipolar start_machine()
{
        return start_service((string_address)machine_program, machine_argv, 3);
}

/*
        How long before the machine process is started again, by how many
        times in a row it has not stayed. The first failure is tried at once,
        as it was, and after that the shell's own steps, 250 ms doubling to
        four seconds: a machine that failed because something it needs was
        not there yet (the last process still leaving, a device node) is
        given the moment that takes, and one that never will is given up on
        after the same few tries.
*/
static positive machine_restart_wait(positive failures)
{
        positive wait = 0;

        for (positive step = 1; step < failures && wait < RESTART_BACKOFF_MAX_NS; step++)
                wait = wait ? wait * 2 : RESTART_BACKOFF_NS;
        return wait < RESTART_BACKOFF_MAX_NS ? wait : RESTART_BACKOFF_MAX_NS;
}

static fn wait_for_settling(bipolar service)
{
        positive started = clock_monotonic_nanoseconds();

        while (service > 0 &&
               clock_monotonic_nanoseconds() - started < SETTLE_WAIT_NS)
        {
                positive status = 0;

                //      lib.c's retry: an interruption here is not an
                //      answer about the service, and asking again is the
                //      whole of what this loop used to do about one.
                if (system_wait4_retry(service, address_of status,
                                       WAIT_NO_HANG, null) != 0)
                        return;

                host_pause(20000000);
        }
}

static bipolar start_shell_until_ready(positive address_to started)
{
        bipolar shell;

        while ((shell = start_service((string_address)init_program,
                                      init_argv, 1)) < 0)
        {
                string_format(log, init_label "could not start %s: %b\n",
                              init_program, shell);
                log_flush();
                host_pause(RESTART_BACKOFF_MAX_NS);
                address_to started = clock_monotonic_nanoseconds();
        }

        return shell;
}

/*
        A pty needs somewhere for its other end to appear.

        Not the kernel's job here even though the kernel does the other
        mounts: devpts registers itself with module_init, which for built-in
        code runs at the same initcall level the compositor starts at, and
        kernel/ links before fs/. By the time there is an init there is a
        devpts, and this is where a system mounts it anyway.
*/
static fn mount_devpts()
{
        bipolar made = system_make_directory_at(AT_FDCWD, "/dev/pts", 0755);

        if (made < 0 && made != -ERROR_EXISTS)
        {
                string_format(log, init_label "/dev/pts could not be created: %b\n", made);
                log_flush();
        }

        bipolar mounted = system_mount("devpts", "/dev/pts", "devpts", 0, 0);

        // Nothing needs a pty this early, so this is not worth refusing to
        // boot over. It is worth saying: without it the terminal fails much
        // later, and for a reason that looks nothing like this one.
        if (mounted < 0)
        {
                string_format(log, init_label "devpts mount failed: %b, terminals will not work\n",
                              mounted);
                log_flush();
        }
}

/*
        POSIX shared memory lives in /dev/shm, a tmpfs anybody may make files
        in (1777, as every distribution mounts it): Chrome's renderers, Qt
        and Wayland clients' buffers and sem_open all go through it. Without
        the mount it was a directory of devtmpfs, which is root's alone and
        not the filesystem a program that checks what /dev/shm is expects, and
        every bowl launch made it, which is the init's to do once.
*/
static fn mount_shm()
{
        bipolar made = system_make_directory_at(AT_FDCWD, "/dev/shm", 0755);

        if (made < 0 && made != -ERROR_EXISTS)
        {
                string_format(log, init_label "/dev/shm could not be created: %b\n", made);
                log_flush();
                return;
        }

        bipolar mounted = system_mount("tmpfs", "/dev/shm", "tmpfs",
                                       MS_NOSUID | MS_NODEV, "mode=1777");

        if (mounted < 0)
        {
                string_format(log, init_label "/dev/shm mount failed: %b\n", mounted);
                log_flush();
        }
}

/*
        The names a program opens by habit for the descriptors it holds:
        /dev/fd and the three that stand for 0, 1 and 2, which devtmpfs does
        not make and a machine with a udev has made for it. Bash's process
        substitution opens /dev/fd/N, and so does the kernel when it runs a
        script it was given as a descriptor (execveat of an O_PATH handle, as
        every program a bowl launches by its guest path is run), which is
        how Debian's chromium and Google Chrome, both shell scripts, ended at
        "sh: /dev/fd/4: cannot open" on a machine that had not run a package
        manager since it started: bowl made these in the isolated view, whose
        /dev is this one, and only there.
*/
static fn link_dev_fd()
{
        static const struct { string_address target; string_address name; } links[] = {
            {"/proc/self/fd", "/dev/fd"},
            {"/proc/self/fd/0", "/dev/stdin"},
            {"/proc/self/fd/1", "/dev/stdout"},
            {"/proc/self/fd/2", "/dev/stderr"},
        };

        for (positive at = 0; at < array_count(links); at++)
        {
                bipolar made = system_symbolic_link_at(links[at].target, AT_FDCWD,
                                                       links[at].name);

                if (made < 0 && made != -ERROR_EXISTS)
                {
                        string_format(log, init_label "%s could not be made: %b\n",
                                      links[at].name, made);
                        log_flush();
                }
        }
}

static DEAD_END b32 system_init()
{
        system_call(syscall(setsid));
        mount_devpts();
        mount_shm();
        link_dev_fd();

        bipolar settling = start_service((string_address)settle_program,
                                         settle_argv, 2);
        positive quick_exits = 0;
        positive backoff = 0;
        positive started = clock_monotonic_nanoseconds();
        bipolar wait_error = 0;

#ifndef SHELL_NO_UTILITIES
        bipolar network = start_network();
        positive network_started = clock_monotonic_nanoseconds();
        positive network_failures = 0;
#endif

        bipolar machine = start_machine();
        positive machine_started = clock_monotonic_nanoseconds();
        positive machine_failures = 0;

        wait_for_settling(settling);
        started = clock_monotonic_nanoseconds();

        // Returning from PID 1 panics the kernel, which on a machine with no
        // serial console says nothing at all. Retrying at a bounded rate keeps
        // the reason on screen instead.
        bipolar shell = start_shell_until_ready(address_of started);

        while (1)
        {
                positive status = 0;

                // -1 reaps any child, not just the shell: as PID 1 every
                // orphan on the system is eventually ours to collect, and
                // lib.c's retry absorbs the interruptions.
                bipolar reaped = system_wait4_retry(-1, address_of status, 0,
                                                    null);

                // Retrying a failing wait as fast as the CPU allows is the one
                // way PID 1 can spin with nothing to show for it. Say it once
                // per run of the same error, then slow down.
                if (reaped < 0 && reaped != ERROR_NO_CHILDREN)
                {
                        if (reaped != wait_error)
                        {
                                wait_error = reaped;
                                string_format(log, init_label "wait failed: %b\n", reaped);
                                log_flush();
                        }

                        host_pause(RESTART_BACKOFF_NS);
                        continue;
                }

                if (reaped > 0)
                {
                        wait_error = 0;

#ifndef SHELL_NO_UTILITIES
                        if (reaped == network)
                        {
                                if (clock_monotonic_nanoseconds() - network_started >=
                                    NETWORK_SETTLED_NS)
                                        network_failures = 0;
                                else
                                        network_failures++;

                                if (network_failures >= NETWORK_GIVE_UP)
                                {
                                        string_format(log, init_label
                                                      "%s keeps exiting; leaving the "
                                                      "network alone\n", network_program);
                                        log_flush();
                                        network = -1;
                                        continue;
                                }

                                network_started = clock_monotonic_nanoseconds();
                                network = start_network();
                                continue;
                        }
#endif

                        if (reaped == machine)
                        {
                                positive code = (positive)wait_status_code(status);
                                positive pause_ns;

                                if (code == 0 || code == 1)
                                {
                                        machine = -1;
                                        continue;
                                }

                                //      Having run for a while is what tells a
                                //      transient apart from a machine that was
                                //      never going to work -- for a death by
                                //      signal, or a /shell that could not exec.
                                //      HOST_MACHINE_FAILED is neither: the
                                //      process got as far as its own code and
                                //      said it could not start or could not keep
                                //      going, and the verdict wait alone puts
                                //      fifteen seconds in front of the two ways
                                //      that happens late. Letting that clear the
                                //      count is a script nobody can source
                                //      restarting for the rest of the boot.
                                if (code != HOST_MACHINE_FAILED &&
                                    clock_monotonic_nanoseconds() - machine_started >=
                                        MACHINE_SETTLED_NS)
                                        machine_failures = 0;
                                else
                                        machine_failures++;

                                if (machine_failures >= MACHINE_GIVE_UP)
                                {
                                        string_format(log, init_label
                                                      "the machine script keeps exiting; "
                                                      "leaving it alone\n");
                                        log_flush();
                                        machine = -1;
                                        continue;
                                }

                                pause_ns = machine_restart_wait(machine_failures);
                                if (pause_ns)
                                        host_pause(pause_ns);
                                machine_started = clock_monotonic_nanoseconds();
                                machine = start_machine();
                                continue;
                        }

                        if (reaped != shell)
                                continue;
                }

                // ECHILD means there is nothing left to wait for at all, so
                // the shell is gone whether or not anyone reported it.
                if (reaped == ERROR_NO_CHILDREN)
                {
                        string_format(log, init_label "nothing left to wait for, restarting %s\n",
                                      init_program);
                        log_flush();
                }
                else
                {
                        positive code = (positive)wait_status_code_base((p32)status, 256);

                        string_format(log, code >= 256
                            ? init_label "%s killed by signal %p, restarting\n"
                            : init_label "%s exited (%p), restarting\n", init_program,
                            code >= 256 ? code & 0xff : code);
                        log_flush();
                }

                if (clock_monotonic_nanoseconds() - started >= SHELL_SETTLED_NS)
                {
                        quick_exits = 0;
                        backoff = 0;
                }
                else if (++quick_exits > RESTART_BACKOFF_AFTER)
                {
                        backoff = backoff ? backoff * 2 : RESTART_BACKOFF_NS;

                        if (backoff > RESTART_BACKOFF_MAX_NS)
                                backoff = RESTART_BACKOFF_MAX_NS;

                        string_format(log, init_label "%p restarts in a row, waiting %p ms\n",
                                      quick_exits, backoff / 1000000);
                        log_flush();

                        host_pause(backoff);
                }

                started = clock_monotonic_nanoseconds();
                // Retried here rather than by falling back into wait4,
                // which would have no children to wait for and would report
                // the shell as having died when it never started.
                shell = start_shell_until_ready(address_of started);
        }
}
