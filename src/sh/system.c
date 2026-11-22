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
        The machine process, the rover runner (src/rover/runner.c). Started
        with the network, not after the disks: it waits for boot's verdict
        before anything reads /root. /shell -c once at boot, not per event:
        the kernel hands events to it, and its ring, while it is attached. A clean 0 or 1 is an orderly stop or a refused
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

/*
        A boot service, by what the console calls it and what is run.
*/
typedef struct
{
        string_address name;
        string_address path;
        string_address address_to arguments;
        positive count;
} init_service;

typedef bipolar (*init_starter)(string_address path, string_address address_to arguments,
                                positive count);

static const init_service init_settle = {(string_address) "moonwater boot",
                                         (string_address)settle_program, settle_argv, 2};
#ifndef SHELL_NO_UTILITIES
static const init_service init_network = {(string_address) "ip watch",
                                          (string_address)network_program, network_argv, 2};
#endif
static const init_service init_machine = {(string_address) "the machine script",
                                          (string_address)machine_program, machine_argv, 3};

static positive machine_restart_wait(positive failures);

/*
        A boot service started by start (start_service, but for the checks),
        and a start that fails said on the console with its reason and tried
        again: at once, and then after machine_restart_wait's pause for that
        many failures in a row, the machine process's own, 250 ms doubling.
        MACHINE_GIVE_UP of them in a row are said, and the service is left
        alone (-1). A fork that failed -- clone's EAGAIN or ENOMEM, on a
        machine short of processes or of memory -- was neither said nor tried
        again: the settle, network or machine service was not there for the
        rest of the boot, and nothing on the screen said why.
*/
static bipolar init_service_start(writer say, init_starter start,
                                  const init_service address_to service)
{
        for (positive failures = 1;; failures++)
        {
                bipolar child = start(service->path, service->arguments, service->count);

                if (child > 0)
                        return child;

                string_format(say, init_label "could not start %s: %b\n", service->name, child);
                log_flush();
                if (failures >= MACHINE_GIVE_UP)
                {
                        string_format(say, init_label "%s did not start in %p tries; leaving "
                                                      "it alone\n",
                                      service->name, failures);
                        log_flush();
                        return -1;
                }
                host_pause(machine_restart_wait(failures));
        }
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

/*
        Waits for the settling service to end, or for SETTLE_WAIT_NS to pass.
        The service's pidfd is polled with the time left, so the boot goes on
        the moment the service ends; a loop that slept 20 ms between looks
        added up to that much to every boot.
*/
static fn wait_for_settling(bipolar service)
{
        positive started = clock_monotonic_nanoseconds();
        bipolar pidfd = service > 0
                            ? system_call_2(syscall(pidfd_open), (positive)service, 0)
                            : -1;

        while (service > 0)
        {
                positive status = 0;
                positive elapsed = clock_monotonic_nanoseconds() - started;

                if (elapsed >= SETTLE_WAIT_NS)
                        break;

                //      lib.c's retry: an interruption here is not an
                //      answer about the service, and asking again is the
                //      whole of what this loop used to do about one.
                if (system_wait4_retry(service, address_of status,
                                       WAIT_NO_HANG, null) != 0)
                        break;

                if (pidfd < 0)
                {
                        host_pause(20000000);
                        continue;
                }

                positive left = SETTLE_WAIT_NS - elapsed;
                timespec span = {left / 1000000000, left % 1000000000};
                system_poll_descriptor waited = {(b32)pidfd, SYSTEM_POLL_READ, 0};

                system_poll_wait(address_of waited, 1, address_of span, null);
        }

        if (pidfd >= 0)
                system_close(pidfd);
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

        bipolar mounted = system_mount("devpts", "/dev/pts", "devpts",
                                       MS_NOSUID | MS_NOEXEC, 0);

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

        //      The locked tier also refuses to run what is written there: a
        //      program that maps shared memory executable (a JIT through a
        //      memfd or a shm file) is not what a machine that does one job
        //      runs.
#if MOONWATER_STRICT_HOST >= STRICT_LOCKED
        bipolar mounted = system_mount("tmpfs", "/dev/shm", "tmpfs",
                                       MS_NOSUID | MS_NODEV | MS_NOEXEC,
                                       "mode=1777");
#else
        bipolar mounted = system_mount("tmpfs", "/dev/shm", "tmpfs",
                                       MS_NOSUID | MS_NODEV, "mode=1777");
#endif

        if (mounted < 0)
        {
                string_format(log, init_label "/dev/shm mount failed: %b\n", mounted);
                log_flush();
        }
}

/*
        /dev/uinput in the input group, on a build with CONFIG_MOONWATER_INPUT_GROUP
        (SHELL_NO_INPUT_GROUP is the other). The kernel makes it at boot with devtmpfs's
        default, 0600 root: a misc device names no owner, and the input core's device
        type (kernel/patch/apply) does not reach it. So init sets it, 0660 in the group
        MOONWATER_INPUT_GID: the group the input device nodes are owned by, the same
        number the kernel is built with. Without the node (a kernel with no uinput, as
        in the hardened and locked profiles) there is nothing to set.
*/
#ifndef SHELL_NO_INPUT_GROUP
static COLD fn system_input_group(void)
{
        system_path_identity facts;

        if (system_path_identity_at(AT_FDCWD, "/dev/uinput",
                                    AT_SYMLINK_NOFOLLOW, SYSTEM_PATH_STATX_TYPE,
                                    address_of facts) < 0 ||
            (facts.mode & 0170000) != 0020000)
                return;

        system_change_owner_at(AT_FDCWD, "/dev/uinput", 0, MOONWATER_INPUT_GID,
                               AT_SYMLINK_NOFOLLOW);
        system_change_mode_at(AT_FDCWD, "/dev/uinput", 0660);
}
#endif

static COLD fn system_ensure_directory(string_address path, positive mode)
{
        bipolar opened;

        (void)system_make_directory_at(AT_FDCWD, path, mode);
        opened = system_open_at(AT_FDCWD, path,
                                FILE_READ | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        if (opened < 0)
                return;
        (void)system_call_2(syscall(fchmod), (positive)opened, mode);
        system_close((positive)opened);
}

/*
        /dev/dri/renderD* in the render group (105, matching src/bowl.c's
        BOWL_GROUP_RENDER), 0660 root:105, and /tmp/.X11-unix owned by root
        at 01777. devtmpfs leaves DRM render nodes 0600 root:root before any
        bowl or udev runs, and an unprivileged bowl session dropping to uid
        1000 would otherwise either lack permission for Vulkan/EGL or make
        /tmp/.X11-unix owned by user instead of root.
*/
#ifndef SHELL_NO_CANVAS
static COLD fn system_render_group(void)
{
        bowl_grant_devices(MOONWATER_STRICT_HOST < STRICT_LOCKED);
}

static COLD fn system_x11_directory(void)
{
        system_ensure_directory("/tmp", 01777);
        system_ensure_directory("/tmp/.X11-unix", 01777);
}
#endif

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

/*
        What the image asks of the kernel before it starts anything.

        The kernel ships its sysctls at the settings that suit a general
        machine, and nothing in this image wrote /proc/sys but the network
        watcher, so the settings below were applied by whichever process
        happened to be that, a moment after the machine was already running
        everything else, and not at all where the watcher never started or
        gave up. They are init's, written once before the first service.

        FILES: fs.protected_symlinks and protected_hardlinks 1 (a link in a
        sticky world-writable directory is followed or made only by its
        owner or the directory's), protected_fifos and protected_regular 1
        (2 at the tight tier: group-writable sticky directories too).
        Root is not exempt from these four: they judge the file's owner
        against the opener's, not capabilities, so root's `>file` onto a
        file another user made in a sticky /tmp fails with EACCES, and so
        does root following that user's symlink there. That is the attack
        they close, and no tool of this image writes that way. The locked tier
        also writes fs.suid_dumpable 0, the kernel's default, written so that
        nothing earlier in a boot can have raised it.

        HOST: kptr_restrict 1 (pointers read as zero to a process without
        CAP_SYSLOG; 2 hides them from root as well), dmesg_restrict 1, and
        randomize_va_space 2. io_uring stays enabled on the default desktop
        tier for games, browsers and bowls; at the tight tier io_uring_disabled
        is set to 2 (refused to everybody), mmap's randomisation is the widest
        the kernel allows (28 bits is its default on x86-64, 32 its limit), and
        ptrace attach needs CAP_SYS_PTRACE (Yama 2); at the locked tier
        it is refused to everybody (Yama 3) and no module can be loaded
        (modules_disabled), both until the next boot. Floodlight's confined
        launches read Yama's scope and want 1 or more, so every setting
        here keeps them working.

        A leaf the kernel was built without is not there and the write fails
        quietly: there is no unprivileged_bpf_disabled because the image has
        no bpf(2), and user namespaces stay as the kernel has them, because
        bowls need them. The reference tier (STRICT_REFERENCE) writes
        nothing of these: the kernel's own values are what it is for. The
        desktop's max_map_count is written below, on every tier that is not
        locked, reference and hardened included.
*/
#if MOONWATER_STRICT_FILES >= STRICT_SAFE || MOONWATER_STRICT_HOST >= STRICT_SAFE || MOONWATER_STRICT_NET >= STRICT_SAFE || !defined(SHELL_NO_CANVAS)
typedef struct
{
        string_address path;
        string_address value;
} system_sysctl_entry;

static COLD fn system_sysctl(string_address path, string_address value)
{
        bipolar handle = system_open_at(AT_FDCWD, path, 1 | O_CLOEXEC);

        if (handle < 0)
                return;

        system_write_all((positive)handle, (p8 address_to)value,
                         string_length(value));
        system_close((positive)handle);
}

static COLD fn system_sysctl_apply(const system_sysctl_entry address_to entries,
                                   positive count)
{
        for (positive at = 0; at < count; at++)
                system_sysctl(entries[at].path, entries[at].value);
}
#endif

#if MOONWATER_STRICT_FILES >= STRICT_SAFE || MOONWATER_STRICT_HOST >= STRICT_SAFE

//      The widest address space randomisation the kernel allows: bits are
//      tried from the most any architecture has (33 on arm64 with a 48-bit
//      address space, 32 on x86-64) down to the kernel's own default, and the
//      kernel refuses a number above its limit with EINVAL, so the first that
//      is taken is the limit. Nothing is left running on fewer bits than the
//      machine can do, and no architecture's maximum is written down here.
static COLD fn system_sysctl_widest(string_address path, positive most,
                                    positive least)
{
        bipolar handle = system_open_at(AT_FDCWD, path, 1 | O_CLOEXEC);

        if (handle < 0)
                return;

        for (positive bits = most; bits >= least; bits--)
        {
                p8 text[8];
                positive length = positive_into(text, bits);

                text[length++] = '\n';

                system_write_result done = system_write_all_checked(
                    (positive)handle, text, length);

                if (!done.error && done.bytes == length)
                        break;
        }

        system_close((positive)handle);
}

static COLD fn system_kernel_defaults(void)
{
        static const system_sysctl_entry defaults[] = {
#if MOONWATER_STRICT_FILES >= STRICT_SAFE
            {"/proc/sys/fs/protected_symlinks", "1\n"},
            {"/proc/sys/fs/protected_hardlinks", "1\n"},
            {"/proc/sys/fs/protected_fifos",
             MOONWATER_STRICT_FILES >= STRICT_TIGHT ? "2\n" : "1\n"},
            {"/proc/sys/fs/protected_regular",
             MOONWATER_STRICT_FILES >= STRICT_TIGHT ? "2\n" : "1\n"},
#if MOONWATER_STRICT_FILES >= STRICT_LOCKED
            {"/proc/sys/fs/suid_dumpable", "0\n"},
#endif
#endif
#if MOONWATER_STRICT_HOST >= STRICT_SAFE
            {"/proc/sys/kernel/randomize_va_space", "2\n"},
            {"/proc/sys/kernel/kptr_restrict",
             MOONWATER_STRICT_HOST >= STRICT_TIGHT ? "2\n" : "1\n"},
            {"/proc/sys/kernel/dmesg_restrict", "1\n"},
#if MOONWATER_STRICT_HOST >= STRICT_TIGHT
            {"/proc/sys/kernel/io_uring_disabled", "2\n"},
            {"/proc/sys/kernel/yama/ptrace_scope",
             MOONWATER_STRICT_HOST >= STRICT_LOCKED ? "3\n" : "2\n"},
#endif
#if MOONWATER_STRICT_HOST >= STRICT_LOCKED
            {"/proc/sys/kernel/modules_disabled", "1\n"},
#endif
#endif
            /*  Who may open an ICMP echo socket, which is all ping needs: the
                kernel's range is empty, so a program of a bowl (whose root is a
                user namespace with no CAP_NET_RAW in the machine's) could not
                ping at all. The socket is an echo to a peer, and carries no
                packet the program wrote. */
            {"/proc/sys/net/ipv4/ping_group_range", "0 2147483647\n"},
        };

        system_sysctl_apply(defaults, array_count(defaults));
#if MOONWATER_STRICT_HOST >= STRICT_TIGHT
        system_sysctl_widest("/proc/sys/vm/mmap_rnd_bits", 33, 28);
        system_sysctl_widest("/proc/sys/vm/mmap_rnd_compat_bits", 16, 8);
#endif
}
#endif

/*
        What a desktop image asks of the kernel beyond the above. The
        kernel's vm.max_map_count is 65530, a small machine's count of memory
        mappings a process may hold, and a game's allocator and Proton's
        compatibility layer map more than that; Steam documents 1048576. The
        limit caps a count and reserves nothing, so a process that maps no
        more than before costs nothing. It is written on the desktop image at
        every level below the locked tier, which runs no games and keeps
        the kernel's number. The server and console images have no Canvas
        and keep it too.

        The game controller nodes are not made here: devtmpfs makes
        /dev/uinput and the joystick nodes at its own 0600 default, as it
        makes udmabuf's at boot. The tier lane reads the uinput mode back, so
        a change to it is a red row and not a silent one.
*/
#define SYSTEM_NOFILE_TARGET BOWL_NOFILE_TARGET
#define SYSTEM_MEMLOCK_TARGET BOWL_MEMLOCK_TARGET

static COLD fn system_zram_setup(void)
{
        p8 info[256];
        p8 size_text[32];
        static p8 swap_page[4096];
        bipolar fd;
        bipolar got;
        p64 mem_kb = 0, bytes, pages;
        positive at = 0, used;

        got = file_slurp_once_at(AT_FDCWD, "/sys/block/zram0/disksize", size_text, sizeof(size_text));
        if (got <= 0 || size_text[0] != '0')
                return; // already configured

        got = file_slurp_once_at(AT_FDCWD, "/proc/meminfo", info, sizeof(info));
        if (got <= 0)
                return;
        while (at < (positive)got && !byte_is_digit(info[at]))
                at++;
        mem_kb = string_digits_max(info + at, 18, address_of used);
        if (!used || mem_kb < 262144)
                return; // skip tiny <256 MiB VMs
        if (mem_kb > 32ull * 1024ull * 1024ull)
                mem_kb = 32ull * 1024ull * 1024ull;

        bytes = (mem_kb * 1024ull) / 2ull;
        if (bytes > 16ull * 1024ull * 1024ull * 1024ull)
                bytes = 16ull * 1024ull * 1024ull * 1024ull;
        bytes &= ~4095ull;
        pages = bytes / 4096ull;
        if (pages < 16)
                return;

        system_sysctl("/sys/block/zram0/comp_algorithm", "zstd\n");
        used = positive_into_string(size_text, (positive)bytes);
        size_text[used++] = '\n';
        size_text[used] = end;
        system_sysctl("/sys/block/zram0/disksize", (string_address)size_text);

        memory_zero(swap_page, sizeof(swap_page));
        {
                p32 version = 1;
                p32 last_page = (p32)(pages - 1);

                memory_copy(swap_page + 1024, address_of version, 4);
                memory_copy(swap_page + 1028, address_of last_page, 4);
                memory_copy(swap_page + 4086, "SWAPSPACE2", 10);
        }
        fd = system_open_at(AT_FDCWD, "/dev/zram0", 1 /* O_WRONLY */ | O_CLOEXEC);
        if (fd < 0)
                return;
        got = (bipolar)system_write_all((positive)fd, swap_page, sizeof(swap_page));
        system_close((positive)fd);
        if (got != (bipolar)sizeof(swap_page))
                return;
        if (system_call_2(syscall(swapon), (positive) "/dev/zram0", 0x8000 | 100) == 0)
        {
                static const system_sysctl_entry zram_tuning[] = {
                    {"/sys/module/zswap/parameters/enabled", "N\n"},
                    {"/proc/sys/vm/swappiness", "100\n"},
                };

                system_sysctl_apply(zram_tuning, array_count(zram_tuning));
        }
}

static COLD fn system_resource_limits(void)
{
        bowl_raise_rlimit(BOWL_RLIMIT_NOFILE, BOWL_NOFILE_TARGET);
#if MOONWATER_STRICT_HOST < STRICT_LOCKED
        bowl_raise_rlimit(BOWL_RLIMIT_MEMLOCK, BOWL_MEMLOCK_TARGET);
        bowl_raise_rlimit(BOWL_RLIMIT_NICE, BOWL_NICE_TARGET);
        bowl_raise_rlimit(BOWL_RLIMIT_RTPRIO, BOWL_RTPRIO_TARGET);
#endif
}

static DEAD_END b32 system_init()
{
        system_call(syscall(setsid));
        system_resource_limits();
#if MOONWATER_STRICT_FILES >= STRICT_SAFE || MOONWATER_STRICT_HOST >= STRICT_SAFE
        system_kernel_defaults();
#endif
#if !defined(SHELL_NO_CANVAS) && MOONWATER_STRICT_HOST < STRICT_LOCKED
        /*  Desktop memory, compaction, mTHP and network latency tuning:
            fixed dirty byte caps prevent multi-GiB writeback stalls on high-RAM
            machines; zero watermark_boost and compaction_proactiveness avoid
            periodic kswapd/kcompactd frame-time spikes; 64K/128K mTHP cuts
            dTLB misses on advised arenas without 2 MiB compaction cost; and
            tcp_slow_start_after_idle=0 keeps bursty game/web flows warm. */
        {
                static const system_sysctl_entry desktop_tuning[] = {
                    {"/proc/sys/vm/max_map_count", "1048576\n"},
                    {"/proc/sys/kernel/split_lock_mitigate", "0\n"},
                    {"/proc/sys/vm/dirty_background_bytes", "67108864\n"},
                    {"/proc/sys/vm/dirty_bytes", "268435456\n"},
                    {"/proc/sys/vm/watermark_boost_factor", "0\n"},
                    {"/proc/sys/vm/watermark_scale_factor", "125\n"},
                    {"/proc/sys/vm/compaction_proactiveness", "0\n"},
                    {"/proc/sys/vm/page-cluster", "0\n"},
                    {"/sys/kernel/mm/transparent_hugepage/hugepages-64kB/enabled", "madvise\n"},
                    {"/sys/kernel/mm/transparent_hugepage/hugepages-128kB/enabled", "madvise\n"},
                    {"/proc/sys/net/ipv4/tcp_slow_start_after_idle", "0\n"},
                    {"/proc/sys/net/ipv4/tcp_mtu_probing", "1\n"},
                    {"/proc/sys/net/ipv4/tcp_fastopen", "3\n"},
                    {"/proc/sys/net/ipv4/tcp_notsent_lowat", "16384\n"},
                    {"/proc/sys/net/core/rmem_max", "16777216\n"},
                    {"/proc/sys/net/core/wmem_max", "16777216\n"},
                    {"/proc/sys/net/ipv4/tcp_rmem", "4096 131072 16777216\n"},
                    {"/proc/sys/net/ipv4/tcp_wmem", "4096 131072 16777216\n"},
                    {"/proc/sys/net/core/netdev_max_backlog", "4096\n"},
                    {"/proc/sys/fs/inotify/max_user_watches", "524288\n"},
                    {"/proc/sys/fs/inotify/max_user_instances", "1024\n"},
                    {"/proc/sys/vm/vfs_cache_pressure", "50\n"},
                    {"/proc/sys/vm/stat_interval", "10\n"},
                    {"/sys/kernel/mm/transparent_hugepage/defrag", "defer\n"},
                };

                system_sysctl_apply(desktop_tuning, array_count(desktop_tuning));
        }
        system_zram_setup();
#endif
        mount_devpts();
        mount_shm();
#if MOONWATER_STRICT_HOST < STRICT_LOCKED
        if (system_make_directory_at(AT_FDCWD, "/dev/mqueue", 01777) >= 0 ||
            system_access_at(AT_FDCWD, "/dev/mqueue", 0) >= 0)
                (void)system_mount("mqueue", "/dev/mqueue", "mqueue",
                                   MS_NOSUID | MS_NOEXEC | MS_NODEV, 0);
#endif
        link_dev_fd();
        system_ensure_directory("/root", 0700);
        system_ensure_directory("/tmp", 01777);
#ifndef SHELL_NO_INPUT_GROUP
        system_input_group();
#endif
#ifndef SHELL_NO_CANVAS
        system_render_group();
        system_x11_directory();
#endif

        bipolar settling = init_service_start(log, start_service, address_of init_settle);
        positive quick_exits = 0;
        positive backoff = 0;
        positive started = clock_monotonic_nanoseconds();
        bipolar wait_error = 0;

#ifndef SHELL_NO_UTILITIES
        bipolar network = init_service_start(log, start_service, address_of init_network);
        positive network_started = clock_monotonic_nanoseconds();
        positive network_failures = 0;
#endif

        bipolar machine = init_service_start(log, start_service, address_of init_machine);
        positive machine_started = clock_monotonic_nanoseconds();
        positive machine_failures = 0;

        wait_for_settling(settling);
#if !defined(SHELL_NO_CANVAS) && MOONWATER_STRICT_HOST < STRICT_LOCKED
        {
                static const system_sysctl_entry nvme_tuning[] = {
                    {"/sys/block/nvme0n1/queue/rq_affinity", "2\n"},
                    {"/sys/block/nvme0n1/queue/read_ahead_kb", "512\n"},
                    {"/sys/block/nvme1n1/queue/rq_affinity", "2\n"},
                    {"/sys/block/nvme1n1/queue/read_ahead_kb", "512\n"},
                    {"/sys/block/nvme2n1/queue/rq_affinity", "2\n"},
                    {"/sys/block/nvme2n1/queue/read_ahead_kb", "512\n"},
                    {"/sys/block/nvme3n1/queue/rq_affinity", "2\n"},
                    {"/sys/block/nvme3n1/queue/read_ahead_kb", "512\n"},
                };

                system_sysctl_apply(nvme_tuning, array_count(nvme_tuning));
        }
#endif
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
                                network = init_service_start(log, start_service,
                                                             address_of init_network);
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
                                machine = init_service_start(log, start_service,
                                                             address_of init_machine);
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
