/*
        src/sh/host/profile.c: the verb profile.

                moonwater profile NAME|PID [SECONDS]

        Where a program's processor time goes, while it runs: every thread of
        the process (by its pid, or every process whose name is NAME, as
        /proc/PID/comm says it: compositor, chromium, Xwayland) is sampled by
        the kernel's clock, PROFILE_HZ times a second of the time it runs, for
        SECONDS (1 to 60, five by default). The kernel's perf events do the
        sampling (perf_event_open, a software clock and no hardware counter,
        so it works on any machine and in a virtual one); nothing is installed.

        What is said, for each thread that ran: how much of a core it took,
        how much of that was in the kernel (a system call, a fault, a driver's
        ioctl), and where in its own code the rest was. A sample in /shell,
        Moonwater's own binary (the compositor, the shell, every tool), is
        named by its function: the image carries /shell.symbols, the
        addresses and names of the linked program, which the build writes
        beside it (src/build/build.c, build_spark). A sample in another
        program is named by the file it is mapped from (/proc/PID/maps): the
        driver's library, the C library, the browser's own binary.

        The kernel's own functions are not named: the image's kernel carries
        no symbol table (kernel/profile/prod, KALLSYMS). Kernel time is the
        system call or the fault the thread was in, which /proc's syscall says
        where it matters.

        Root only: sampling another user's program, and the kernel's side of
        any, are root's (perf_event_paranoid).

        Dawn Larsson - Apache-2.0 license
        github.com/dawnlarsson/moonwater
*/

#define PROFILE_HZ 997            // prime, so the clock does not beat against a program's own period
#define PROFILE_THREADS 512       // threads sampled at most, over every process named
#define PROFILE_PROCESSES 64
#define PROFILE_RING_PAGES 16     // the data pages of a thread's ring: 64 KiB, a second and a half of samples
#define PROFILE_TOP 12            // functions or files said for a thread
#define PROFILE_SYMBOLS_PATH "/shell.symbols"
#define PROFILE_MAPS_ROOM (1024 * 1024)
#define PROFILE_MAPPINGS 4096
#define PROFILE_CALLS 6           // the system calls kept a thread, the most seen

//      perf_event_attr, as far as PERF_ATTR_SIZE_VER5 (112 bytes): the type, the size, the clock, the rate, what a sample holds, and
//      the flags word. Laid out as the kernel reads it on every machine.
struct profile_attr
{
        p32 type, size;
        p64 config, frequency, sample_type, read_format, flags;
        p32 wakeup_events, bp_type;
        p64 config1, config2, branch_sample_type, sample_regs_user;
        p32 sample_stack_user;
        b32 clockid;
        p64 sample_regs_intr;
        p32 aux_watermark;
        p16 sample_max_stack, reserved;
};
_Static_assert(sizeof(struct profile_attr) == 112, "perf_event_attr up to PERF_ATTR_SIZE_VER5");

#define PROFILE_TYPE_SOFTWARE 1
#define PROFILE_CPU_CLOCK 0
#define PROFILE_SAMPLE_IP 1
#define PROFILE_SAMPLE_TID 2
#define PROFILE_FLAG_FREQ (1ull << 10)
#define PROFILE_FLAG_EXCLUDE_HV (1ull << 6)
#define PROFILE_FD_CLOEXEC 8
#define PROFILE_RECORD_SAMPLE 9
#define PROFILE_MISC_KERNEL 1

//      perf_event_mmap_page's ring words: where the kernel has written to, and where the reader has read to.
#define PROFILE_RING_HEAD 1024
#define PROFILE_RING_TAIL 1032
#define PROFILE_RING_OFFSET 1040
#define PROFILE_RING_SIZE 1048

struct profile_mapping
{
        p64 from, to;
        p32 process;      // the process's place in profile_processes
        p32 name;         // the file's name, an offset into profile_names
        p64 samples;
};

struct profile_thread
{
        b32 tid, fd;
        p32 process;
        p8 address_to ring;
        positive ring_bytes;
        p64 samples, kernel;
        char name[16];
        //      The system calls the thread was found in at the drains when its last samples were the kernel's: the number and how often.
        p16 calls[PROFILE_CALLS];
        p32 call_seen[PROFILE_CALLS];
        p64 kernel_before;
};

static struct profile_state
{
        struct profile_thread threads[PROFILE_THREADS];
        positive thread_count;
        b32 processes[PROFILE_PROCESSES];
        bool shell[PROFILE_PROCESSES]; // the process is /shell, named by the symbol map
        positive process_count;
        // Every user sample, the thread's index and the address, for the report.
        p64 address_to ips;
        p16 address_to whose;
        positive sample_count, sample_room;
        // The symbol map: addresses ascending, and each name's offset into the text.
        p64 address_to symbol_at;
        p32 address_to symbol_name;
        positive symbol_count;
        p8 address_to symbol_text;
        // The other programs' mappings, from their maps.
        struct profile_mapping address_to mappings;
        positive mapping_count;
        p8 address_to names;
        positive names_used, names_room;
} profile;

static bool profile_named(string_address directory, string_address name, address_any context)
{
        string_address wanted = (string_address)context;
        p8 path[64] = "/proc/", comm[32];
        positive length = string_length(name);
        positive pid;

        (void)directory;
        if (profile.process_count == PROFILE_PROCESSES || length > 10)
                return profile.process_count < PROFILE_PROCESSES;
        pid = string_digits_max(name, 10, null);
        if (!pid || !string_join(path, sizeof(path), "/proc/", name, "/comm"))
                return true;
        if (host_read_kernel((string_address)path, comm, sizeof(comm)) >= 0 &&
            string_equals((string_address)comm, wanted))
                profile.processes[profile.process_count++] = (b32)pid;
        return true;
}

//      Whether the process runs /shell: its exe link says so.
static bool profile_is_shell(b32 pid)
{
        p8 path[48], target[64];
        bipolar got;

        if (!host_proc_path(path, sizeof(path), (positive)pid, "/exe"))
                return false;
        got = system_call_4(syscall(readlinkat), (positive)(bipolar)AT_FDCWD, (positive)path, (positive)target,
                            sizeof(target) - 1);
        if (got <= 0)
                return false;
        target[got] = end;
        return string_equals((string_address)target, "/shell");
}

//      A process's file mappings that can run (r-xp), each with the file's name.
static fn profile_read_maps(p32 process, p8 address_to text)
{
        p8 path[48];
        bipolar got;
        positive at = 0;

        if (!host_proc_path(path, sizeof(path), (positive)profile.processes[process], "/maps"))
                return;
        //      Read to its end: the kernel hands /proc/PID/maps out a page or so a read, so one read is its first few mappings.
        {
                bipolar handle = system_open_at(AT_FDCWD, (string_address)path, FILE_READ | O_CLOEXEC);
                bipolar part;

                if (handle < 0)
                        return;
                got = 0;
                while (got < PROFILE_MAPS_ROOM - 1 &&
                       (part = system_read_retry((positive)handle, text + got, (positive)(PROFILE_MAPS_ROOM - 1 - got))) > 0)
                        got += part;
                system_close(handle);
        }
        if (got <= 0)
                return;
        text[got] = end;
        while (at < (positive)got && profile.mapping_count < PROFILE_MAPPINGS)
        {
                p8 address_to nl = memory_first_of(text + at, '\n', (positive)got - at);
                positive line = at, stop = nl ? (positive)(nl - text) : (positive)got;
                p64 from = 0, to = 0;
                string_address name, slash;
                positive name_length;

                at = stop + 1;
                text[stop] = end;
                // from-to perms offset dev inode path
                while (line < stop && text[line] != '-')
                        from = from * 16 + (p64)(text[line] <= '9' ? text[line] - '0' : (text[line] | 0x20) - 'a' + 10), line++;
                if (line >= stop)
                        continue;
                line++;
                while (line < stop && text[line] != ' ')
                        to = to * 16 + (p64)(text[line] <= '9' ? text[line] - '0' : (text[line] | 0x20) - 'a' + 10), line++;
                if (line >= stop)
                        continue;
                line++;
                if (line + 4 > stop || text[line + 2] != 'x')
                        continue;
                name = (string_address)text + stop;
                while (name > (string_address)text + line && name[-1] != ' ')
                        name--;
                if (!name[0])
                        name = "[anonymous code]";
                else if ((slash = string_last_of(name, '/')))
                        name = slash + 1;
                name_length = string_length(name);
                if (profile.names_used + name_length + 1 > profile.names_room)
                        return;
                memory_copy(profile.names + profile.names_used, name, name_length + 1);
                profile.mappings[profile.mapping_count].from = from;
                profile.mappings[profile.mapping_count].to = to;
                profile.mappings[profile.mapping_count].process = process;
                profile.mappings[profile.mapping_count].name = (p32)profile.names_used;
                profile.mappings[profile.mapping_count].samples = 0;
                profile.mapping_count++;
                profile.names_used += name_length + 1;
        }
}

//      /shell.symbols: a line a function, its address in hex, a space and its name, addresses ascending.
static fn profile_read_symbols(void)
{
        bipolar handle = system_open_at(AT_FDCWD, PROFILE_SYMBOLS_PATH, FILE_READ | O_CLOEXEC);
        file_facts facts;
        positive lines = 0, at = 0;
        bipolar got;

        if (handle < 0)
                return;
        if (!file_look(handle, (string_address)"", AT_EMPTY_PATH, address_of facts) || !facts.size ||
            facts.size > 64 * 1024 * 1024 || !(profile.symbol_text = memory_take(facts.size + 1)))
        {
                system_close(handle);
                return;
        }
        got = system_read_retry((positive)handle, profile.symbol_text, facts.size);
        system_close(handle);
        if (got != (bipolar)facts.size)
                return;
        profile.symbol_text[got] = end;
        lines = memory_count(profile.symbol_text, (positive)got, '\n');
        profile.symbol_at = memory_take((lines + 1) * sizeof(p64));
        profile.symbol_name = memory_take((lines + 1) * sizeof(p32));
        if (!profile.symbol_at || !profile.symbol_name)
                return;
        while (at < (positive)got)
        {
                p64 value = 0;
                positive start = at;

                while (at < (positive)got && profile.symbol_text[at] != ' ' && profile.symbol_text[at] != '\n')
                {
                        p8 c = profile.symbol_text[at++];

                        value = value * 16 + (p64)(c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10);
                }
                if (at < (positive)got && profile.symbol_text[at] == ' ' && at > start)
                {
                        profile.symbol_at[profile.symbol_count] = value;
                        profile.symbol_name[profile.symbol_count++] = (p32)(at + 1);
                }
                while (at < (positive)got && profile.symbol_text[at] != '\n')
                        at++;
                if (at < (positive)got)
                        profile.symbol_text[at++] = end;
        }
}

//      The function an address is in: the last symbol at or below it, or -1.
static bipolar profile_symbol(p64 address)
{
        positive low = 0, high = profile.symbol_count;

        while (low < high)
        {
                positive middle = (low + high) / 2;

                if (profile.symbol_at[middle] <= address)
                        low = middle + 1;
                else
                        high = middle;
        }
        return low ? (bipolar)low - 1 : -1;
}

//      The mapping of a process an address is in, or -1.
static bipolar profile_mapping(p32 process, p64 address)
{
        for (positive at = 0; at < profile.mapping_count; at++)
                if (profile.mappings[at].process == process && address >= profile.mappings[at].from &&
                    address < profile.mappings[at].to)
                        return (bipolar)at;
        return -1;
}

static bool profile_task(string_address directory, string_address name, address_any context)
{
        p32 process = (p32)(positive)context;
        struct profile_thread address_to thread;
        struct profile_attr attr;
        p8 path[64];
        bipolar fd, mapped;

        (void)directory;
        if (profile.thread_count == PROFILE_THREADS)
                return false;
        thread = &profile.threads[profile.thread_count];
        memory_zero(thread, sizeof(*thread));
        thread->tid = (b32)string_digits_max(name, 10, null);
        if (!thread->tid)
                return true;
        thread->process = process;
        memory_zero(&attr, sizeof(attr));
        attr.type = PROFILE_TYPE_SOFTWARE;
        attr.size = sizeof(attr);
        attr.config = PROFILE_CPU_CLOCK;
        attr.frequency = PROFILE_HZ;
        attr.sample_type = PROFILE_SAMPLE_IP | PROFILE_SAMPLE_TID;
        attr.flags = PROFILE_FLAG_FREQ | PROFILE_FLAG_EXCLUDE_HV;
        fd = system_call_5(syscall(perf_event_open), (positive)&attr, (positive)thread->tid, (positive)(bipolar)-1,
                           (positive)(bipolar)-1, PROFILE_FD_CLOEXEC);
        if (fd < 0)
                return true; // a thread that ended between the walk and the open
        thread->ring_bytes = (PROFILE_RING_PAGES + 1) * 4096;
        mapped = system_call_6(syscall(mmap), 0, thread->ring_bytes, FILE_PROTECT_READ | FILE_PROTECT_WRITE,
                               FILE_MAP_SHARED, (positive)fd, 0);
        if (mapped < 0 && mapped > -4096)
        {
                system_close(fd);
                return true;
        }
        thread->fd = (b32)fd;
        thread->ring = (p8 address_to)mapped;
        if (!string_join(path, sizeof(path), directory, "/", name, "/comm") ||
            host_read_kernel((string_address)path, (p8 address_to)thread->name, sizeof(thread->name)) < 0)
                thread->name[0] = end;
        profile.thread_count++;
        return true;
}

//      What a thread's ring holds now, taken, and the ring's reader moved past it.
static fn profile_drain(positive index)
{
        struct profile_thread address_to thread = &profile.threads[index];
        p8 address_to page = thread->ring;
        p64 head = __atomic_load_n((p64 address_to)(page + PROFILE_RING_HEAD), __ATOMIC_ACQUIRE);
        p64 tail = *(p64 address_to)(page + PROFILE_RING_TAIL);
        p64 offset = *(p64 address_to)(page + PROFILE_RING_OFFSET), size = *(p64 address_to)(page + PROFILE_RING_SIZE);
        p8 record[64];

        if (!offset)
                offset = 4096, size = PROFILE_RING_PAGES * 4096;
        if (!size || offset > thread->ring_bytes || size > thread->ring_bytes - offset)
                return;
        while (tail < head)
        {
                p16 bytes;
                p32 type;

                if (head - tail < 8)
                        break;
                // A record may wrap past the end of the ring: it is copied out a byte at a time over the edge.
                for (positive at = 0; at < 8; at++)
                        record[at] = page[offset + (tail + at) % size];
                type = *(p32 address_to)record;
                bytes = *(p16 address_to)(record + 6);
                if (bytes < 8 || (p64)bytes > head - tail)
                        break;
                if (type == PROFILE_RECORD_SAMPLE && bytes >= 16 && bytes <= sizeof(record))
                {
                        p16 misc = *(p16 address_to)(record + 4);
                        p64 ip;

                        for (positive at = 8; at < bytes; at++)
                                record[at] = page[offset + (tail + at) % size];
                        ip = *(p64 address_to)(record + 8);
                        thread->samples++;
                        if ((misc & 7) == PROFILE_MISC_KERNEL)
                                thread->kernel++;
                        else if (profile.sample_count < profile.sample_room)
                        {
                                profile.ips[profile.sample_count] = ip;
                                profile.whose[profile.sample_count++] = (p16)index;
                        }
                }
                tail += bytes;
        }
        __atomic_store_n((p64 address_to)(page + PROFILE_RING_TAIL), tail, __ATOMIC_RELEASE);
}

//      Which system call a thread is in now (/proc/PID/task/TID/syscall's first number), counted when its samples since the last drain were
//      the kernel's: what the kernel's share of it was spent in (a GPU's ioctl, a futex, a read).
static fn profile_call_note(positive index)
{
        struct profile_thread address_to thread = &profile.threads[index];
        p8 path[64], text[32];
        positive number;
        bipolar got;
        byte_cursor cursor = byte_cursor_on(path);

        if (thread->kernel == thread->kernel_before)
                return;
        thread->kernel_before = thread->kernel;
        byte_cursor_put_bytes(address_of cursor, "/proc/", 6);
        byte_cursor_put_u64(address_of cursor, (p64)profile.processes[thread->process]);
        byte_cursor_put_bytes(address_of cursor, "/task/", 6);
        byte_cursor_put_u64(address_of cursor, (p64)thread->tid);
        byte_cursor_put_bytes(address_of cursor, "/syscall", 8);
        if (!byte_cursor_terminate(address_of cursor))
                return;
        got = host_read_kernel((string_address)path, text, sizeof(text));
        if (got <= 0 || text[0] < '0' || text[0] > '9')
                return;
        number = string_digits_max(text, 5, null);
        for (positive at = 0; at < PROFILE_CALLS; at++)
                if (thread->call_seen[at] && thread->calls[at] == number)
                {
                        thread->call_seen[at]++;
                        return;
                }
        for (positive at = 0; at < PROFILE_CALLS; at++)
                if (!thread->call_seen[at])
                {
                        thread->calls[at] = (p16)number, thread->call_seen[at] = 1;
                        return;
                }
}

//      One thread's report: its share of a core, the kernel's part, and the functions or files of the rest.
static fn profile_say_thread(positive index, positive seconds, p64 address_to counts, positive count_room)
{
        struct profile_thread address_to thread = &profile.threads[index];
        bool shell = profile.shell[thread->process] && profile.symbol_count;
        p64 user = thread->samples - thread->kernel;

        host_say(log, "  thread %p %w: %p%% of a core, %p%% of it in the kernel\n", (positive)thread->tid,
                 writer_terminal_name,
                 thread->name[0] ? (string_address)thread->name : (string_address) "?",
                 (positive)(thread->samples * 100 / (PROFILE_HZ * seconds)),
                 (positive)(thread->samples ? thread->kernel * 100 / thread->samples : 0));
        if (thread->kernel)
        {
                bool any = false;

                for (positive at = 0; at < PROFILE_CALLS; at++)
                        if (thread->call_seen[at])
                        {
                                if (!any)
                                {
                                        host_say(log, "    in the kernel, in system calls:");
                                        any = true;
                                }
                                host_say(log, " %p (%p times)", (positive)thread->calls[at], (positive)thread->call_seen[at]);
                        }
                if (any)
                        host_say(log, "\n");
        }
        if (!user)
                return;
        memory_zero(counts, count_room * sizeof(p64));
        for (positive at = 0; at < profile.sample_count; at++)
        {
                bipolar which;

                if (profile.whose[at] != index)
                        continue;
                which = shell ? profile_symbol(profile.ips[at]) : profile_mapping(thread->process, profile.ips[at]);
                if (which >= 0 && (positive)which < count_room)
                        counts[which]++;
        }
        for (positive shown = 0; shown < PROFILE_TOP; shown++)
        {
                positive best = 0;
                p64 most = 0;

                for (positive at = 0; at < count_room; at++)
                        if (counts[at] > most)
                                most = counts[at], best = at;
                if (!most || most * 200 < user)
                        break;
                host_say(log, "    %p%%  %w\n", (positive)(most * 100 / user),
                         writer_terminal_name,
                         shell ? (string_address)profile.symbol_text + profile.symbol_name[best]
                               : (string_address)profile.names + profile.mappings[best].name);
                counts[best] = 0;
        }
}

static fn profile_cleanup(p8 address_to maps_text, p64 address_to counts)
{
        for (positive at = 0; at < profile.thread_count; at++)
        {
                if (profile.threads[at].ring)
                        system_call_2(syscall(munmap), (positive)profile.threads[at].ring,
                                      profile.threads[at].ring_bytes);
                if (profile.threads[at].fd >= 0)
                        system_close(profile.threads[at].fd);
        }
        if (counts)
                memory_give(counts);
        if (maps_text)
                memory_give(maps_text);
        if (profile.whose)
                memory_give(profile.whose);
        if (profile.ips)
                memory_give(profile.ips);
        if (profile.mappings)
                memory_give(profile.mappings);
        if (profile.names)
                memory_give(profile.names);
        if (profile.symbol_name)
                memory_give(profile.symbol_name);
        if (profile.symbol_at)
                memory_give(profile.symbol_at);
        if (profile.symbol_text)
                memory_give(profile.symbol_text);
        memory_zero(&profile, sizeof(profile));
}

static b32 host_profile(string_address address_to arguments, positive count)
{
        string_address target;
        positive seconds = 5, used = 0, pid;
        p8 address_to maps_text;
        p64 address_to counts = null;
        positive count_room;

        return_if(count < 3 || count > 4, host_usage());
        host_need_root();
        target = arguments[2];
        if (count == 4)
        {
                seconds = string_digits_max(arguments[3], 3, address_of used);
                if (!used || arguments[3][used] || seconds < 1 || seconds > 60)
                        return host_usage();
        }
        memory_zero(&profile, sizeof(profile));
        pid = string_digits_max(target, 10, address_of used);
        if (used && !target[used])
                profile.processes[profile.process_count++] = (b32)pid;
        else
                host_each_entry("/proc", profile_named, (address_any)target);
        if (!profile.process_count)
                return host_refuse("no process is called %s\n", target);

        profile.names_room = 256 * 1024;
        profile.names = memory_take(profile.names_room);
        profile.mappings = memory_take(PROFILE_MAPPINGS * sizeof(struct profile_mapping));
        maps_text = memory_take(PROFILE_MAPS_ROOM);
        profile.sample_room = (positive)PROFILE_HZ * seconds * 64 + 4096;
        profile.ips = memory_take(profile.sample_room * sizeof(p64));
        profile.whose = memory_take(profile.sample_room * sizeof(p16));
        if (!profile.names || !profile.mappings || !maps_text || !profile.ips || !profile.whose)
        {
                profile_cleanup(maps_text, null);
                return host_fail("memory", -ENOMEM);
        }

        for (positive at = 0; at < profile.process_count; at++)
        {
                p8 path[48];

                profile.shell[at] = profile_is_shell(profile.processes[at]);
                if (profile.shell[at] && !profile.symbol_count)
                        profile_read_symbols();
                if (!profile.shell[at])
                        profile_read_maps((p32)at, maps_text);
                if (host_proc_path(path, sizeof(path), (positive)profile.processes[at], "/task"))
                        host_each_entry((string_address)path, profile_task, (address_any)at);
        }
        if (!profile.thread_count)
        {
                profile_cleanup(maps_text, null);
                return host_refuse("the kernel would sample none of its threads (perf_event_open)\n");
        }

        host_say(log, "profiling %p threads of %p processes called %s for %p s, %p samples a second of each thread's time\n",
                 profile.thread_count, profile.process_count, target, seconds, (positive)PROFILE_HZ);
        for (positive tick = 0; tick < seconds * 10; tick++)
        {
                host_pause(100000000);
                for (positive at = 0; at < profile.thread_count; at++)
                {
                        profile_drain(at);
                        profile_call_note(at);
                }
        }

        count_room = profile.symbol_count > profile.mapping_count ? profile.symbol_count : profile.mapping_count;
        counts = memory_take((count_room + 1) * sizeof(p64));
        if (!counts)
        {
                profile_cleanup(maps_text, null);
                return host_fail("memory", -ENOMEM);
        }
        if (!profile.symbol_count)
                for (positive at = 0; at < profile.process_count; at++)
                        if (profile.shell[at])
                                host_say(log, "  (%s is not in this image: /shell's functions are not named)\n",
                                         (string_address)PROFILE_SYMBOLS_PATH);
        for (positive process = 0; process < profile.process_count; process++)
        {
                host_say(log, "process %p\n", (positive)profile.processes[process]);
                for (positive at = 0; at < profile.thread_count; at++)
                        if (profile.threads[at].process == process &&
                            profile.threads[at].samples * 100 >= (p64)PROFILE_HZ * seconds) // a percent of a core or more
                                profile_say_thread(at, seconds, counts, count_room);
        }
        profile_cleanup(maps_text, counts);
        log_flush();
        return 0;
}
