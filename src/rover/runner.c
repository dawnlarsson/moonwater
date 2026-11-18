/*
        The runner: rover's userspace end, the machine process with rules in
        place of the machine script. Userspace only; included by host.c after
        the verbs it calls, so its actions are the verbs' own code.

        It is the process PID 1 keeps as the machine (moonwater machine), and
        it is chosen over the script when the image carries rules
        (/lib/moonwater/rules). It reads the image's rule files and the
        person's (/root/rules), compiles them into one table, keeps the three
        state pages, supervises the services, hears every event there is, and
        does what the rules say:

            kernel events   the spark path the script was fed by (a WAIT thread, a pipe),
                            until the kernel's own ring;
            desktop events  the compositor's keys.fire lines and pin files, turned into
                            chord and button events;
            settings        the files the Settings rows keep, by inotify on /root;
            the boot        its verdict, once, when /run/moonwater/verdict appears;
            clocks          every DURATION and at CLOCK;
            services        their lives, as service up, down and failed.

        Until the kernel evaluates its own rules, the runner asks every rule
        of an event, wherever it is placed, and does a kernel primitive
        through its verb. Everything it starts is a child of its own: a
        service is watched by its pidfd, anything else is reaped by its pid.
*/

#ifndef ROVER_RUNNER_INCLUDED
#define ROVER_RUNNER_INCLUDED

#define ROVER_IMAGE_RULES "/lib/moonwater/rules"
#define ROVER_ROOT_RULES "/root/rules"
#define ROVER_ROOT_SCRIPTS "/root/rules/scripts"
#define ROVER_IMAGE_SCRIPTS "/lib/moonwater/rules/scripts"
#define ROVER_BOOTS_FILE "/root/rules/.boots" // boots begun and not seen to load a table, a number
#define ROVER_FILES_MOST 64u
#define ROVER_KEYS_FILE "/run/moonwater/keys"
#define ROVER_KEYS_FIRE "/run/moonwater/keys.fire"
#define ROVER_VERDICT "/run/moonwater/verdict"
#define ROVER_CHILDREN_MOST 64u
#define ROVER_QUEUE_MOST 32u
#define ROVER_STOP_GRACE_NS 3000000000ull // TERM, then KILL after this
#define ROVER_KEYS_MOST 32u
#define ROVER_SECOND_NS 1000000000ull

struct rover_running
{
        bipolar pid;
        bipolar pidfd;
        p64 started;    // monotonic
        p64 retry;      // monotonic: not started again before this
        p64 stopping;   // monotonic: TERM sent at this, KILL after the grace
        p8 up;
        p8 failed;      // given up
        p8 shorts;      // short lives in a row
        p8 pad;
};

static struct
{
        struct rover_build build;
        p64 table_words[ROVER_TABLE_MOST / 8];
        positive size;
        struct rover_table view;
        struct rover_run runs[ROVER_RULES_MOST];
        p64 page_words[ROVER_PAGES][ROVER_PAGE_BYTES / 8];
        const p8 address_to pages[ROVER_PAGES];
        struct rover_running service[ROVER_SERVICES_MOST];
        bipolar children[ROVER_CHILDREN_MOST];
        struct rover_event queue[ROVER_QUEUE_MOST]; // cues and the runner's own events, done after the one in hand
        p8 queue_text[ROVER_QUEUE_MOST][ROVER_WORD_MOST + 1];
        positive queued;
        p8 source[ROVER_SOURCE_MOST + ROVER_FILES_MOST];
        positive source_used;
        bipolar kernel_pipe[2];
        bipolar device;
        bipolar watch;
        bipolar fire;
        bipolar latency; // /dev/cpu_dma_latency, held while a latency rule says
        p64 every_next[8];
        p64 every_period[8];
        positive everys;
        b64 minute; // the last minute an at was posted for
        bool verdict_seen;
        bool safe;
        struct { p8 name[48]; b64 chord; } keys[ROVER_KEYS_MOST];
        positive key_count;
} rover_runner;

// ---- the clock -------------------------------------------------------------

static b64 rover_runner_realtime(void)
{
        timespec now;

        system_call_2(syscall(clock_gettime), 0, (positive)address_of now);
        return now.tv_sec;
}

// ---- files -----------------------------------------------------------------

//      A rule file of the person's: regular, root's, written by root alone, at most 64 KiB, not a link (as the machine
//      script was read). The image's are the image's. Answers its length, or -1.
static bipolar rover_runner_read(string_address path, bool trusted_owner, p8 address_to into, positive room)
{
        file_facts facts;
        bipolar handle, got, used = 0;

        if (!file_look(AT_FDCWD, path, AT_SYMLINK_NOFOLLOW, address_of facts) || (facts.mode & MODE_FORMAT) != MODE_FILE)
                return -1;
        if (trusted_owner && (facts.owner != 0 || (facts.mode & 0022)))
                return -1;
        handle = system_open_at(AT_FDCWD, path, FILE_READ | O_CLOEXEC | O_NOFOLLOW);
        if (handle < 0)
                return -1;
        while ((got = system_read_retry((positive)handle, into + used, room - (positive)used)) > 0)
                used += got;
        system_close(handle);
        return got < 0 || (positive)used >= room ? -1 : used;
}

//      The names of a directory's *.rover files, sorted, into names; how many.
static positive rover_runner_names(string_address directory, p8 names[ROVER_FILES_MOST][64])
{
        p8 buffer[4096];
        positive count = 0;
        bipolar handle = system_open_at(AT_FDCWD, directory, FILE_READ | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW), got;

        if (handle < 0)
                return 0;
        while ((got = system_call_3(syscall(getdents64), (positive)handle, (positive)buffer, sizeof(buffer))) > 0)
                for (bipolar at = 0; at < got;)
                {
                        p16 length = memory_load_unaligned(p16, buffer + at + 16);
                        string_address name = (string_address)(buffer + at + 19);
                        positive size = string_length(name);

                        at += length;
                        if (size < 7 || size > 62 || count == ROVER_FILES_MOST || name[0] == '.' ||
                            memory_compare(name + size - 6, ".rover", 6))
                                continue;
                        memory_copy(names[count], name, size + 1);
                        count++;
                }
        system_close(handle);
        //      Sorted, so a table is the same whatever order the directory lists its files in.
        for (positive at = 1; at < count; at++)
                for (positive k = at; k > 0 && string_compare(names[k - 1], names[k]) > 0; k--)
                {
                        p8 swap[64];

                        memory_copy(swap, names[k], 64);
                        memory_copy(names[k], names[k - 1], 64);
                        memory_copy(names[k - 1], swap, 64);
                }
        return count;
}

static fn rover_runner_say(string_address first, string_address second, string_address third)
{
        string_address line[] = {(string_address) "rover: ", first, second ? second : (string_address) "",
                                 third ? third : (string_address) "", null};

        host_kmsg(line);
}

/*
        The files of one directory into the source, each checked alone first so
        a file that is not the language is said by its name and line and left
        out; the rest are one table. A person's file replaces the image's of
        its name, and an empty one masks it.
*/
static fn rover_runner_take(string_address directory, bool person, p8 seen[ROVER_FILES_MOST][64], positive address_to seens,
                            p8 masked[ROVER_FILES_MOST][64], positive address_to maskeds)
{
        p8 names[ROVER_FILES_MOST][64];
        positive count = rover_runner_names(directory, names);
        static p8 one[ROVER_SOURCE_MOST + 1];

        for (positive at = 0; at < count; at++)
        {
                p8 path[160];
                bipolar length;
                bool skip = false;

                for (positive k = 0; k < *seens; k++)
                        skip = skip || string_equals(seen[k], names[at]);
                for (positive k = 0; k < *maskeds; k++)
                        skip = skip || string_equals(masked[k], names[at]);
                if (skip || !string_join(path, sizeof(path), directory, "/", (string_address)names[at]))
                        continue;
                length = rover_runner_read(path, person, one, sizeof(one));
                if (length < 0)
                {
                        rover_runner_say(path, " is not read: not a regular file of root's alone, or larger than 64 KiB", null);
                        continue;
                }
                if (person)
                        memory_copy(seen[(*seens)++], names[at], 64);
                if (!length)
                {
                        memory_copy(masked[(*maskeds)++], names[at], 64);
                        continue;
                }
                if (!rover_compile(&rover_runner.build, one, (positive)length))
                {
                        p8 number[24];

                        number[positive_into_string(number, rover_runner.build.line)] = 0;
                        rover_runner_say(path, (string_address) ": ", (string_address)rover_runner.build.why);
                        rover_runner_say(path, " is left out; its line ", (string_address)number);
                        continue;
                }
                if (rover_runner.source_used + (positive)length + 1 > sizeof(rover_runner.source))
                {
                        rover_runner_say(path, " is left out: the rules are more than 64 KiB", null);
                        continue;
                }
                memory_copy(rover_runner.source + rover_runner.source_used, one, (positive)length);
                rover_runner.source_used += (positive)length;
                rover_runner.source[rover_runner.source_used++] = '\n';
        }
}

//      Whether this boot is to load the image's rules alone: moonwater.safe=1, or two boots that never loaded a table.
static bool rover_runner_safe(void)
{
        p8 line[4096];
        bipolar length = rover_runner_read("/proc/cmdline", false, line, sizeof(line));
        p8 boots[16];

        if (length > 0)
        {
                line[length] = 0;
                if (string_search(line, "moonwater.safe=1"))
                        return true;
        }
        return host_read_word(ROVER_BOOTS_FILE, boots, sizeof(boots)) >= 0 && string_to_positive(boots) >= 2;
}

static bool rover_runner_load(void)
{
        p8 seen[ROVER_FILES_MOST][64], masked[ROVER_FILES_MOST][64];
        positive seens = 0, maskeds = 0;
        struct rover_refusal refusal;

        rover_runner.safe = rover_runner_safe();
        rover_runner.source_used = 0;
        if (!rover_runner.safe)
                rover_runner_take(ROVER_ROOT_RULES, true, seen, &seens, masked, &maskeds);
        else
                rover_runner_say("safe mode: the image's rules alone", null, null);
        rover_runner_take(ROVER_IMAGE_RULES, false, seen, &seens, masked, &maskeds);
        rover_runner.build.merge = true;
        bool made = rover_compile(&rover_runner.build, rover_runner.source, rover_runner.source_used);
        rover_runner.build.merge = false;
        if (!made)
        {
                rover_runner_say("the rules together are refused: ", (string_address)rover_runner.build.why, null);
                return false;
        }
        rover_runner.size = rover_runner.build.size;
        memory_copy(rover_runner.table_words, rover_built(&rover_runner.build), rover_runner.size);
        if (!rover_check((const p8 address_to)rover_runner.table_words, rover_runner.size, &rover_runner.view, &refusal))
        {
                rover_runner_say("the table is refused: ", (string_address)refusal.why, null);
                return false;
        }
        memory_zero(rover_runner.runs, sizeof(rover_runner.runs));
        (void)host_write_text(ROVER_BOOTS_FILE, "0\n");
        return true;
}

// ---- the pages -------------------------------------------------------------

static p8 address_to rover_runner_page(p8 page)
{
        return (p8 address_to)rover_runner.page_words[page];
}

static fn rover_runner_put(const struct rover_state_row address_to state, b64 number, const p8 address_to text,
                           positive length)
{
        p8 address_to page = rover_runner_page(state->page);

        if (rover_kind_is_word(rover_types[state->type].kind))
        {
                if (length > ROVER_WORD_MOST)
                        length = ROVER_WORD_MOST;
                page[state->offset] = (p8)length;
                memory_copy(page + state->offset + 1, text, length);
                page[state->offset + 1 + length] = 0;
                return;
        }
        b32 four = (b32)number;
        memory_copy(page + state->offset, &four, 4);
}

//      A state set from a word, typed by the state: false when the word is none of its type's.
static bool rover_runner_set_word(string_address name, const p8 address_to word, positive length)
{
        const struct rover_state_row address_to state = rover_state_named((const p8 address_to)name, string_length(name));
        struct rover_word typed = {word, (p32)length, 0, {0}};
        b64 number = 0;

        rover_runner.build.why_used = 0;
        if (!state || !rover_literal(&rover_runner.build, &typed, state->type, &number))
                return false;
        rover_runner_put(state, number, word, length);
        return true;
}

//      A registry row's state from the file it keeps (or its standard when none is kept). Answers the state's bit, or 0.
static p64 rover_runner_setting(const struct settings_row address_to row)
{
        p8 name[96], word[96];
        positive used = 0;
        const struct rover_state_row address_to state;

        if (row->how != SETTING_PLAIN || !row->path)
                return 0;
        memory_copy(name, "setting", 7);
        used = 7;
        for (positive at = 0; at < SETTING_WORDS && row->read[at]; at++)
        {
                positive size = string_length((string_address)row->read[at]);

                if (used + 1 + size >= sizeof(name))
                        return 0;
                name[used++] = '.';
                memory_copy(name + used, row->read[at], size);
                used += size;
        }
        name[used] = 0;
        state = rover_state_named(name, used);
        if (!state)
                return 0;
        if (host_read_word((string_address)row->path, word, sizeof(word)) < 0 || !word[0])
        {
                if (!row->standard)
                        return 0;
                string_copy_max(word, (string_address)row->standard, sizeof(word));
        }
        (void)rover_runner_set_word(name, word, string_length(word));
        return ROVER_READS_BIT((positive)(state - rover_states));
}

static p64 rover_runner_settings(string_address changed)
{
        p64 bits = 0;

        for (positive at = 0; at < array_count(settings_rows); at++)
                if (!changed || (settings_rows[at].path && string_equals((string_address)settings_rows[at].path, changed)))
                        bits |= rover_runner_setting(&settings_rows[at]);
        return bits;
}

//      The kernel's states the runner can read until the kernel keeps its own page.
static fn rover_runner_kernel_states(void)
{
        struct canvas_control control;
        p8 line[4096];
        bipolar length = rover_runner_read("/proc/cmdline", false, line, sizeof(line) - 1);
        b32 east = (b32)clock_local_east(rover_runner_realtime());

        memory_copy(rover_runner_page(ROVER_PAGE_KERNEL) + ROVER_PAGE_ZONE, &east, 4);
        memory_zero(&control, sizeof(control));
        if (host_canvas_request(SPARK_CANVAS_STATUS, address_of control) >= 0)
                rover_runner_set_word("canvas", control.running ? (const p8 address_to) "on" : (const p8 address_to) "off",
                                      control.running ? 2 : 3);
        //      boot.NAME from moonwater.NAME=VALUE, one word each.
        if (length > 0)
        {
                line[length] = 0;
                for (positive at = 0; at < ROVER_STATES; at++)
                {
                        const char address_to name = rover_states[at].name;
                        p8 key[64];

                        if (memory_compare(name, "boot.", 5) ||
                            !string_join(key, sizeof(key), "moonwater.", (string_address)(name + 5), "="))
                                continue;
                        string_address found = string_search(line, key);
                        if (!found)
                                continue;
                        found += string_length(key);
                        positive size = 0;
                        while (found[size] && found[size] != ' ' && found[size] != '\n')
                                size++;
                        rover_runner_put(&rover_states[at], 0, found, size);
                }
        }
}

//      What a kernel event says of the kernel's states, so a rule after it reads the machine as it is now.
static p64 rover_runner_kernel_event(p16 id)
{
        static const struct { p16 id; const char address_to state; const char address_to word; } says[] = {
            {SPARK_BIND_LID_CLOSE, "lid", "closed"},   {SPARK_BIND_LID_OPEN, "lid", "open"},
            {SPARK_BIND_DOCK_ON, "dock", "on"},        {SPARK_BIND_DOCK_OFF, "dock", "off"},
            {SPARK_BIND_TABLET_ON, "tablet", "on"},    {SPARK_BIND_TABLET_OFF, "tablet", "off"},
            {SPARK_BIND_HEADPHONE_ON, "headphone", "on"}, {SPARK_BIND_HEADPHONE_OFF, "headphone", "off"},
            {SPARK_BIND_CANVAS_ON, "canvas", "on"},    {SPARK_BIND_CANVAS_OFF, "canvas", "off"},
        };

        for (positive at = 0; at < array_count(says); at++)
                if (says[at].id == id)
                {
                        const struct rover_state_row address_to state =
                            rover_state_named((const p8 address_to)says[at].state, string_length((string_address)says[at].state));

                        rover_runner_set_word((string_address)says[at].state, (const p8 address_to)says[at].word,
                                              string_length((string_address)says[at].word));
                        return state ? ROVER_READS_BIT((positive)(state - rover_states)) : 0;
                }
        return 0;
}

// ---- children --------------------------------------------------------------

static fn rover_runner_child(bipolar pid)
{
        for (positive at = 0; at < ROVER_CHILDREN_MOST; at++)
                if (!rover_runner.children[at])
                {
                        rover_runner.children[at] = pid;
                        return;
                }
}

static fn rover_runner_reap(void)
{
        for (positive at = 0; at < ROVER_CHILDREN_MOST; at++)
                if (rover_runner.children[at] &&
                    system_wait4_retry(rover_runner.children[at], null, 1 /* WNOHANG */, null) != 0)
                        rover_runner.children[at] = 0;
}

//      A program started from words: its own session, standard input nothing, its output where it is told. extra is
//      one more NAME=value for its environment (MOONWATER_ASKED=1 for what a person asked for). Answers its pid.
static bipolar rover_runner_exec(string_address address_to argv, string_address log_path, string_address extra)
{
        static string_address environment[256];
        string_address address_to inherited = program_environment_list();
        positive used = 0;
        bipolar pid = system_fork();

        if (pid)
                return pid;
        while (inherited && inherited[used] && used < array_count(environment) - 2)
        {
                environment[used] = inherited[used];
                used++;
        }
        if (extra)
                environment[used++] = extra;
        environment[used] = null;
        system_call_1(syscall(setsid), 0);
        bipolar null_handle = system_open_at(AT_FDCWD, "/dev/null", FILE_READ);
        bipolar out = system_open_at_mode(AT_FDCWD, log_path ? log_path : (string_address) "/dev/kmsg",
                                          FILE_WRITE | O_CREAT | O_APPEND, 0600);
        if (null_handle >= 0)
                system_descriptor_move(null_handle, 0);
        if (out >= 0)
        {
                system_descriptor_install(out, 1);
                system_descriptor_move(out, 2);
        }
        system_execute(argv[0], argv, environment);
        exit(127);
}

//      A verb of the CLI's own, in a child, as `moonwater WORDS` would run it.
static fn rover_runner_verb(b32 (address_to verb)(string_address address_to, positive), string_address address_to words,
                            positive count)
{
        bipolar pid = system_fork();

        if (pid > 0)
        {
                rover_runner_child(pid);
                return;
        }
        if (pid == 0)
        {
                b32 answer = verb(words, count);

                log_flush();
                exit(answer & 0xff);
        }
}

// ---- services --------------------------------------------------------------

static fn rover_runner_post(p16 id, const p8 address_to word, positive length, b64 number)
{
        struct rover_event address_to event;
        positive at = rover_runner.queued;

        if (at == ROVER_QUEUE_MOST)
        {
                rover_runner_say("an event is dropped: the runner's queue is full", null, null);
                return;
        }
        event = &rover_runner.queue[at];
        memory_zero(event, sizeof(*event));
        event->id = id;
        event->when = clock_monotonic_nanoseconds();
        event->realtime = rover_runner_realtime();
        if (word)
        {
                if (length > ROVER_WORD_MOST)
                        length = ROVER_WORD_MOST;
                memory_copy(rover_runner.queue_text[at], word, length);
                rover_runner.queue_text[at][length] = 0;
                event->count = 1;
                event->field[0].text = rover_runner.queue_text[at];
                event->field[0].length = (p32)length;
                event->field[0].number = number;
        }
        else if (number >= 0)
        {
                event->count = 1;
                event->field[0].number = number;
                event->field[0].text = rover_nothing;
        }
        rover_runner.queued++;
}

static const p8 address_to rover_runner_pool_word(p32 at, positive address_to length)
{
        *length = memory_load_unaligned(p16, rover_runner.view.pool + at);
        return rover_runner.view.pool + at + 2;
}

static fn rover_runner_service_event(p16 id, positive which)
{
        positive length;
        const p8 address_to name = rover_runner_pool_word(rover_runner.view.service[which].name, &length);

        rover_runner_post(id, name, length, 0);
}

static b32 rover_runner_service_named(const p8 address_to name, positive length)
{
        for (positive at = 0; at < rover_runner.view.head->services; at++)
        {
                positive size;
                const p8 address_to other = rover_runner_pool_word(rover_runner.view.service[at].name, &size);

                if (size == length && !memory_compare(other, name, length))
                        return (b32)at;
        }
        return -1;
}

static fn rover_runner_start(positive which)
{
        const struct rover_service address_to service = rover_runner.view.service + which;
        struct rover_running address_to running = &rover_runner.service[which];
        string_address argv[ROVER_ARGUMENTS * 4 + 2];
        positive count = service->count < array_count(argv) - 1 ? service->count : array_count(argv) - 1, length;

        for (positive at = 0; at < count; at++)
                argv[at] = (string_address)rover_runner_pool_word(rover_runner.view.argument[service->argument + at].index,
                                                                  &length);
        argv[count] = null;
        running->pid = rover_runner_exec(argv,
                                         service->log ? (string_address)rover_runner_pool_word(service->log, &length) : null,
                                         null);
        running->pidfd = running->pid > 0 ? system_call_2(syscall(pidfd_open), (positive)running->pid, 0) : -1;
        running->started = clock_monotonic_nanoseconds();
        running->stopping = 0;
        running->up = 0;
        if (running->pid <= 0)
                running->pid = 0;
}

static fn rover_runner_stop(positive which)
{
        struct rover_running address_to running = &rover_runner.service[which];

        if (!running->pid || running->stopping)
                return;
        system_call_2(syscall(kill), (positive)running->pid, 15);
        running->stopping = clock_monotonic_nanoseconds();
}

static bool rover_runner_needs_up(positive which)
{
        const struct rover_service address_to service = rover_runner.view.service + which;

        for (positive at = 0; at < ROVER_NEEDS && service->need[at] != ROVER_SERVICE_NONE; at++)
                if (!rover_runner.service[service->need[at]].up)
                        return false;
        return true;
}

//      A service that ended: the count of its short lives, its wait or its giving up, and the events.
static fn rover_runner_ended(positive which)
{
        const struct rover_service address_to service = rover_runner.view.service + which;
        struct rover_running address_to running = &rover_runner.service[which];
        p64 now = clock_monotonic_nanoseconds();
        bool stopped = running->stopping != 0;

        system_wait4_retry(running->pid, null, 0, null);
        if (running->pidfd >= 0)
                system_close(running->pidfd);
        running->pid = 0;
        running->pidfd = -1;
        running->up = 0;
        running->stopping = 0;
        rover_runner_service_event(98, which);
        if (stopped)
                return;
        if (now - running->started >= (p64)service->short_ms * 1000000)
                running->shorts = 0;
        running->shorts++;
        if (running->shorts >= service->lives)
        {
                positive length;

                running->failed = 1;
                rover_runner_say((string_address)rover_runner_pool_word(service->name, &length),
                                 " ended its short lives in a row and is given up", null);
                rover_runner_service_event(99, which);
                return;
        }
        running->retry = now + (p64)service->wait_ms[running->shorts - 1 < 2 ? running->shorts - 1 : 2] * 1000000;
}

/*
        Each service as its rules say now: stopped when its if stopped holding
        (TERM, and KILL after three seconds), started when its if holds and
        its needs are up, and up when its ready path is there.
*/
static fn rover_runner_services(void)
{
        p64 now = clock_monotonic_nanoseconds();

        for (positive which = 0; which < rover_runner.view.head->services; which++)
        {
                const struct rover_service address_to service = rover_runner.view.service + which;
                struct rover_running address_to running = &rover_runner.service[which];
                bool wanted = rover_service_wanted(&rover_runner.view, which, rover_runner.pages) && rover_runner_needs_up(which);

                if (running->pid && !wanted)
                        rover_runner_stop(which);
                if (running->stopping && now - running->stopping >= ROVER_STOP_GRACE_NS)
                        system_call_2(syscall(kill), (positive)running->pid, 9);
                if (!running->pid && wanted && !running->failed && now >= running->retry)
                        rover_runner_start(which);
                if (running->pid && !running->up && !running->stopping)
                {
                        positive length;
                        file_facts facts;
                        bool ready = !service->ready ||
                                     file_look(AT_FDCWD, (string_address)rover_runner_pool_word(service->ready, &length), 0,
                                               address_of facts);

                        if (ready)
                        {
                                running->up = 1;
                                rover_runner_service_event(97, which);
                        }
                }
        }
}

// ---- actions ---------------------------------------------------------------

//      An argument as a word for a program's argv: a word as it is, a number as its type says it.
static string_address rover_runner_word(const struct rover_value address_to value, p8 type, p8 address_to room)
{
        if (rover_kind_is_word(rover_types[type].kind))
                return (string_address)value->text;
        struct rover_text text = {room, 0, 31, false};

        if (rover_types[type].kind == ROVER_T_PERCENT)
        {
                rover_put_number(&text, value->number / 100);
                rover_puts(&text, "%");
        }
        else if (rover_types[type].kind == ROVER_T_ENUM || rover_types[type].kind == ROVER_T_ONOFF)
                rover_choice_put(&text, rover_types[type].choices, value->number);
        else
                rover_put_number(&text, value->number);
        room[text.used] = 0;
        return room;
}

static fn rover_runner_program(string_address first, const struct rover_value address_to values, positive count,
                               positive from, string_address extra)
{
        string_address argv[ROVER_ARGUMENTS + 2];
        positive used = 0;

        argv[used++] = first;
        for (positive at = from; at < count; at++)
                argv[used++] = (string_address)values[at].text;
        argv[used] = null;
        bipolar pid = rover_runner_exec(argv, null, extra);
        if (pid > 0)
                rover_runner_child(pid);
}

static fn rover_runner_primitive(p16 id, const struct rover_value address_to values, positive count)
{
        p8 path[160], word[32];
        string_address words[6] = {(string_address) "moonwater"};

        switch (id)
        {
        case 1: // poweroff
        case 2: // reboot
                host_machine_end_run();
                shell_stop(log, id == 1 ? REBOOT_POWER_OFF : REBOOT_RESTART);
                return;
        case 3: // suspend
                words[1] = "sleep";
                rover_runner_verb(tune_sleep_verb, words, 2);
                return;
        case 4: // backlight PERCENT
                words[1] = "brightness";
                words[2] = rover_runner_word(&values[0], ROVER_TYPE_PERCENT, word);
                rover_runner_verb(tune_brightness, words, 3);
                return;
        case 5: // latency DURATION: the processor's wake latency held, in microseconds, while the descriptor is open
        {
                p32 microseconds = (p32)(values[0].number / 1000);

                if (rover_runner.latency < 0)
                        rover_runner.latency = system_open_at(AT_FDCWD, "/dev/cpu_dma_latency", FILE_WRITE | O_CLOEXEC);
                if (rover_runner.latency >= 0)
                        system_write_all((positive)rover_runner.latency, &microseconds, 4);
                return;
        }
        case 6: // latency off
                if (rover_runner.latency >= 0)
                        system_close(rover_runner.latency);
                rover_runner.latency = -1;
                return;
        case 7: // radio off WHICH
                words[2] = "off";
                if (values[0].number != 1)
                {
                        words[1] = "wifi";
                        rover_runner_verb(host_wifi, words, 3);
                }
                if (values[0].number != 0)
                {
                        words[1] = "bluetooth";
                        rover_runner_verb(host_bluetooth, words, 3);
                }
                return;
        case 9: // usb lock
                words[1] = "device";
                words[2] = "lock";
                rover_runner_verb(host_device, words, 3);
                return;
        case 11: // log TEXT
        case 15: // notify TEXT: the notices board takes no text yet, so it is said where the log is read
                rover_runner_say(id == 11 ? (string_address) "" : (string_address) "notice: ", (string_address)values[0].text, null);
                return;
        case 12: // display off
        case 13: // display on
                rover_runner_say("display off and on have no mechanism yet: nothing is done", null, null);
                return;
        case 14: // open APP WORDS: a kit program by its name, or a program by its path, asked for by a person
                if (values[0].length && values[0].text[0] == '/')
                        string_copy_max(path, (string_address)values[0].text, sizeof(path));
                else if (!string_join(path, sizeof(path), "/", (string_address)values[0].text))
                        return;
                rover_runner_program(path, values, count, 1, "MOONWATER_ASKED=1");
                return;
        case 16: // start SERVICE
        case 17: // stop SERVICE
        case 18: // restart SERVICE
        {
                b32 which = rover_runner_service_named(values[0].text, values[0].length);

                if (which < 0)
                {
                        rover_runner_say("no service is named ", (string_address)values[0].text, null);
                        return;
                }
                struct rover_running address_to running = &rover_runner.service[which];
                running->failed = 0;
                running->shorts = 0;
                running->retry = 0;
                if (id != 16)
                        rover_runner_stop((positive)which);
                return;
        }
        case 19: // desktop rest
                words[1] = "desktop";
                words[2] = "rest";
                rover_runner_verb(host_desktop, words, 3);
                return;
        }
}

static fn rover_runner_set(p16 id, const struct rover_value address_to value)
{
        const struct rover_switch_row address_to row = rover_switch_find(id);
        p8 word[32];
        string_address words[5] = {(string_address) "moonwater"};
        string_address said;

        if (!row)
                return;
        said = rover_runner_word(value, row->type, word);
        switch (id)
        {
        case 1: words[1] = "brightness"; words[2] = said; rover_runner_verb(tune_brightness, words, 3); return;
        case 2: words[1] = "power"; words[2] = said; rover_runner_verb(tune_power, words, 3); return;
        case 3: words[1] = "cpu"; words[2] = "boost"; words[3] = said; rover_runner_verb(tune_cpu, words, 4); return;
        case 4: words[1] = "cpu"; words[2] = "smt"; words[3] = said; rover_runner_verb(tune_cpu, words, 4); return;
        case 5: words[1] = "charge"; words[2] = "limit"; words[3] = said; rover_runner_verb(tune_charge, words, 4); return;
        case 6: words[1] = "airplane"; words[2] = said; rover_runner_verb(tune_airplane, words, 3); return;
        }
}

static fn rover_runner_do(const struct rover_event address_to event, const struct rover_hit address_to hit)
{
        for (positive at = 0; at < hit->count; at++)
        {
                positive which = (positive)hit->action + at;
                const struct rover_action address_to action = rover_runner.view.action + which;
                struct rover_value values[ROVER_ARGUMENTS];
                positive count = rover_arguments(&rover_runner.view, which, event, rover_runner.pages, values);
                p8 path[160];

                switch (action->kind)
                {
                case ROVER_DO:
                        rover_runner_primitive(action->id, values, count);
                        break;
                case ROVER_SET:
                        rover_runner_set(action->id, &values[0]);
                        break;
                case ROVER_RUN:
                {
                        //      The person's script of the name, when it is root's alone, else the image's.
                        file_facts facts;
                        bool person = string_join(path, sizeof(path), ROVER_ROOT_SCRIPTS "/", (string_address)values[0].text) &&
                                      file_look(AT_FDCWD, path, AT_SYMLINK_NOFOLLOW, address_of facts) &&
                                      (facts.mode & MODE_FORMAT) == MODE_FILE && facts.owner == 0 && !(facts.mode & 0022);

                        if (!person && !string_join(path, sizeof(path), ROVER_IMAGE_SCRIPTS "/", (string_address)values[0].text))
                                break;
                        rover_runner_program(path, values, count, 1, null);
                        break;
                }
                case ROVER_TOOL:
                {
                        //      The tool is an applet of /shell, which answers to its name.
                        string_address argv[ROVER_ARGUMENTS + 2] = {(string_address)rover_tools[action->id]};
                        for (positive k = 0; k < count; k++)
                                argv[k + 1] = (string_address)values[k].text;
                        argv[count + 1] = null;
                        bipolar pid = system_fork();
                        if (pid == 0)
                        {
                                system_execute("/shell", argv, program_environment_list());
                                exit(127);
                        }
                        if (pid > 0)
                                rover_runner_child(pid);
                        break;
                }
                case ROVER_EMIT:
                        if (event->depth + 1 < ROVER_EMIT_DEPTH)
                        {
                                rover_runner_post(ROVER_CUE, values[0].text, values[0].length, 0);
                                rover_runner.queue[rover_runner.queued - 1].depth = event->depth + 1;
                        }
                        else
                                rover_runner_say("a cue past depth four is not posted: ", (string_address)values[0].text, null);
                        break;
                }
        }
}

//      An event, asked of every rule of it wherever it is placed (the kernel's and the desktop's ask none yet), and then
//      the when rules of what it changed.
static fn rover_runner_fire(const struct rover_event address_to event, p64 changed)
{
        struct rover_hit hits[16];

        for (p8 place = 0; place < ROVER_PLACES; place++)
        {
                positive made = rover_fire(&rover_runner.view, rover_runner.runs, rover_runner.pages, event, place, hits, 16);

                for (positive at = 0; at < made; at++)
                        rover_runner_do(event, &hits[at]);
                if (changed)
                {
                        struct rover_event edge = *event;

                        edge.id = 0;
                        edge.count = 0;
                        made = rover_edges(&rover_runner.view, rover_runner.runs, rover_runner.pages, changed, &edge, place,
                                           hits, 16);
                        for (positive at = 0; at < made; at++)
                                rover_runner_do(&edge, &hits[at]);
                }
        }
}

static fn rover_runner_drain(void)
{
        for (positive at = 0; at < rover_runner.queued; at++)
        {
                //      An event posted while one is done goes on the queue's end, so the copy keeps its text in place.
                struct rover_event event = rover_runner.queue[at];

                rover_runner_fire(&event, 0);
        }
        rover_runner.queued = 0;
}

// ---- the desktop's lines ---------------------------------------------------

/*
        The chords the rules hear, as the compositor reads them today: one
        key_ line each in /run/moonwater/keys, written before it starts, and
        the FIFO it names a pressed one down.
*/
static fn rover_runner_keys(void)
{
        static p8 text[4096];
        struct rover_text out = {text, 0, sizeof(text), false};

        rover_runner.key_count = 0;
        for (positive rule = 0; rule < rover_runner.view.head->rules; rule++)
        {
                const struct rover_rule address_to row = rover_runner.view.rule + rule;
                b64 chord;
                struct rover_text name = {rover_runner.keys[rover_runner.key_count].name, 0, 47, false};

                if (row->event != 64 || !row->trigger || rover_runner.key_count == ROVER_KEYS_MOST)
                        continue;
                chord = memory_load_unaligned(b64, rover_runner.view.pool + rover_runner.view.comparison[row->comparison].b.index);
                rover_puts(&name, "key");
                for (positive k = 0; k < array_count(rover_modifiers); k++)
                        if ((chord >> 16) & rover_modifiers[k].bit)
                        {
                                rover_puts(&name, "_");
                                rover_puts(&name, rover_modifiers[k].name);
                        }
                for (positive k = 0; k < array_count(rover_keys); k++)
                        if (rover_keys[k].code == (chord & 0xffff))
                        {
                                rover_puts(&name, "_");
                                rover_puts(&name, rover_keys[k].name);
                        }
                name.bytes[name.used] = 0;
                rover_runner.keys[rover_runner.key_count++].chord = chord;
                rover_put(&out, name.bytes, name.used);
                rover_puts(&out, "\n");
        }
        (void)host_write_file(ROVER_KEYS_FILE, text, out.used, 0644, false);
        (void)system_call_3(syscall(unlinkat), (positive)AT_FDCWD, (positive)ROVER_KEYS_FIRE, 0);
        (void)system_call_4(syscall(mknodat), (positive)AT_FDCWD, (positive)ROVER_KEYS_FIRE, 0010600, 0);
        rover_runner.fire = system_open_at(AT_FDCWD, ROVER_KEYS_FIRE, 2 /* O_RDWR */ | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW);
}

//      One line the compositor said: a chord's key_ name, a button's word, or a pin's start.
static fn rover_runner_line(const p8 address_to line, positive length)
{
        static const char address_to const buttons[6] = {"launcher_button", "volume_button", "mic_button",
                                                        "notices_button", "tray_double", null};

        for (positive at = 0; at < rover_runner.key_count; at++)
                if (rover_bytes_are(line, length, (const char address_to)rover_runner.keys[at].name))
                {
                        rover_runner_post(64, null, 0, rover_runner.keys[at].chord);
                        return;
                }
        for (positive at = 0; buttons[at]; at++)
                if (rover_bytes_are(line, length, buttons[at]))
                {
                        rover_runner_post(71, null, 0, (b64)at);
                        return;
                }
        if (length > 10 && !memory_compare(line, "pin_start_", 10))
        {
                p8 slot_text[8], path[48], program[ROVER_WORD_MOST + 1];
                positive slot_length = length - 10 < 7 ? length - 10 : 7;

                memory_copy(slot_text, line + 10, slot_length);
                slot_text[slot_length] = 0;
                if (!string_join(path, sizeof(path), "/run/moonwater/pin.", (string_address)slot_text) ||
                    host_read_word(path, program, sizeof(program)) < 0)
                        return;
                //      The pin's words are a line each; the program is the first.
                string_address newline = string_first_of(program, '\n');
                if (newline)
                        newline[0] = 0;
                (void)system_call_3(syscall(unlinkat), (positive)AT_FDCWD, (positive)path, 0);
                rover_runner_post(71, program, string_length(program), 5);
                struct rover_event address_to event = &rover_runner.queue[rover_runner.queued - 1];
                //      button's fields: name pin, the slot, the program.
                event->count = 3;
                event->field[2] = event->field[0];
                event->field[0].number = 5;
                event->field[0].text = rover_nothing;
                event->field[0].length = 0;
                event->field[1].number = (b64)string_to_positive(slot_text);
                event->field[1].text = rover_nothing;
                event->field[1].length = 0;
        }
}

static fn rover_runner_fire_lines(void)
{
        static p8 text[1024];
        static positive kept;
        bipolar got;

        while ((got = system_read_once(rover_runner.fire, text + kept, sizeof(text) - kept)) > 0)
        {
                positive used = kept + (positive)got, start = 0;

                for (positive at = 0; at < used; at++)
                        if (text[at] == '\n')
                        {
                                rover_runner_line(text + start, at - start);
                                start = at + 1;
                        }
                kept = used - start;
                memory_copy(text, text + start, kept);
                if (kept == sizeof(text))
                        kept = 0;
        }
}

// ---- the kernel's events ---------------------------------------------------

//      The WAIT thread: each kernel event's number down the pipe, for the loop.
static fn rover_runner_kernel_thread(address_any unused)
{
        struct machine_control control;

        (void)unused;
        for (;;)
        {
                bipolar failed = host_machine_wait(rover_runner.device, address_of control);
                p32 event = control.event;

                if (failed == -EINTR || failed == -ETIMEDOUT)
                        continue;
                if (failed < 0)
                        event = ~0u;
                system_write_all((positive)rover_runner.kernel_pipe[1], &event, 4);
                if (failed < 0 || !event)
                        return;
        }
}

/*
        What the kernel is to queue to the runner: a script of one empty
        function a kernel event its rules hear, which is how the kernel's
        scanner says an event is owned until the kernel has its own ring.
        Events no rule hears keep their image lines: the power key powers off.
*/
static fn rover_runner_own(void)
{
        static p8 text[4096];
        struct rover_text out = {text, 0, sizeof(text), false};

        for (positive id = 1; id <= SPARK_BIND_EVENTS; id++)
        {
                if (!(rover_runner.view.index[id].count & ROVER_SPAN_COUNT))
                        continue;
                rover_puts(&out, "function moonwater_");
                for (const char address_to name = spark_bind_event_name[id - 1]; *name; name++)
                        rover_put(&out, (const p8 address_to)(*name == ' ' ? "_" : name), 1);
                rover_puts(&out, " {\n  :\n}\n");
        }
        memory_zero(address_of host_machine, sizeof(host_machine));
        host_machine.length = out.used;
        host_machine.address = (unsigned long)text;
        if (host_machine_script(MOONWATER_SCRIPT_SET) < 0)
                rover_runner_say("the kernel took no word of which events are the rules'", null, null);
}

// ---- the loop --------------------------------------------------------------

static fn rover_runner_clocks(p64 now)
{
        b64 realtime = rover_runner_realtime(), zoned = realtime + clock_local_east(realtime);
        b64 minute = zoned / 60;

        for (positive at = 0; at < rover_runner.everys; at++)
                if (now >= rover_runner.every_next[at])
                {
                        rover_runner.every_next[at] = now + rover_runner.every_period[at];
                        rover_runner_post(102, null, 0, (b64)rover_runner.every_period[at]);
                }
        if (minute != rover_runner.minute)
        {
                rover_runner.minute = minute;
                rover_runner_post(103, null, 0, rover_floor_mod(zoned, 86400) / 60);
        }
}

static fn rover_runner_find_everys(void)
{
        p64 now = clock_monotonic_nanoseconds();

        rover_runner.everys = 0;
        for (positive at = 0; at < rover_runner.view.head->rules && rover_runner.everys < 8; at++)
        {
                const struct rover_rule address_to rule = rover_runner.view.rule + at;
                b64 period;
                bool known = false;

                if (rule->event != 102 || !rule->trigger)
                        continue;
                period = memory_load_unaligned(b64, rover_runner.view.pool + rover_runner.view.comparison[rule->comparison].b.index);
                for (positive k = 0; k < rover_runner.everys; k++)
                        known = known || (b64)rover_runner.every_period[k] == period;
                if (known)
                        continue;
                rover_runner.every_period[rover_runner.everys] = (p64)period;
                rover_runner.every_next[rover_runner.everys++] = now + (p64)period;
        }
}

//      The boot's verdict, once: live, disk NAME or ask, as /run/moonwater/verdict says it.
static fn rover_runner_verdict(void)
{
        p8 word[HOST_NAME_ROOM + 16];
        static const char address_to const verdicts[3] = {"live", "disk", "ask"};

        if (rover_runner.verdict_seen || host_read_word(ROVER_VERDICT, word, sizeof(word)) < 0)
                return;
        rover_runner.verdict_seen = true;
        host_restore_session(!string_has_prefix((string_address)word, "ask "), false);
        for (positive at = 0; at < 3; at++)
                if (string_has_prefix((string_address)word, (string_address)verdicts[at]))
                {
                        string_address disk = string_first_of(word, ' ');

                        rover_runner_post(101, disk ? (const p8 address_to)(disk + 1) : null,
                                          disk ? string_length(disk + 1) : 0, (b64)at);
                        struct rover_event address_to event = &rover_runner.queue[rover_runner.queued - 1];
                        //      boot's fields: the verdict, and the disk's name.
                        event->count = 2;
                        event->field[1] = event->field[0];
                        event->field[0].number = (b64)at;
                        event->field[0].text = rover_nothing;
                        event->field[0].length = 0;
                        if (!disk)
                                event->field[1].text = rover_nothing;
                        return;
                }
}

static fn rover_runner_watched(void)
{
        static p8 buffer[4096];
        bipolar got;

        while ((got = system_read_once(rover_runner.watch, buffer, sizeof(buffer))) > 0)
                for (bipolar at = 0; at < got;)
                {
                        p32 length = memory_load_unaligned(p32, buffer + at + 12);
                        string_address name = (string_address)(buffer + at + 16);
                        p8 path[96];

                        at += 16 + (bipolar)length;
                        if (!length || !string_join(path, sizeof(path), "/root/", name))
                                continue;
                        //      A Settings row's file: its state again, and the setting event by the row's name.
                        p64 bits = rover_runner_settings(path);
                        for (positive k = 0; bits && k < array_count(settings_rows); k++)
                                if (settings_rows[k].path && string_equals((string_address)settings_rows[k].path, path))
                                {
                                        p8 row[96];
                                        positive used = 0;

                                        for (positive w = 0; w < SETTING_WORDS && settings_rows[k].read[w]; w++)
                                        {
                                                if (w)
                                                        row[used++] = '.';
                                                string_copy_max(row + used, (string_address)settings_rows[k].read[w],
                                                                sizeof(row) - used);
                                                used += string_length((string_address)settings_rows[k].read[w]);
                                        }
                                        rover_runner_post(100, row, used, 0);
                                        rover_runner_drain();
                                        struct rover_event edge;
                                        memory_zero(&edge, sizeof(edge));
                                        edge.when = clock_monotonic_nanoseconds();
                                        edge.realtime = rover_runner_realtime();
                                        rover_runner_fire(&edge, bits);
                                }
                }
}

/*
        The runner, from the machine process's attach on: the table, the pages,
        the services and every event, until the kernel says the machine is
        stopping. Answers as host_machine_run does.
*/
static b32 rover_runner_run(bipolar device)
{
        system_poll_descriptor wait[ROVER_SERVICES_MOST + 3];
        p64 last_keep = 0;

        rover_runner.device = device;
        rover_runner.latency = -1;
        for (positive at = 0; at < ROVER_PAGES; at++)
                rover_runner.pages[at] = rover_runner_page((p8)at);
        //      A boot that begins is counted until it loads a table (rover_runner_load puts it back to 0).
        {
                p8 boots[16], count[24];
                positive now = host_read_word(ROVER_BOOTS_FILE, boots, sizeof(boots)) >= 0 ? string_to_positive(boots) : 0;

                count[positive_into_string(count, now + 1)] = '\n';
                (void)host_write_file(ROVER_BOOTS_FILE, count, string_length_max(count, 23) + 1, 0600, false);
        }
        if (!rover_runner_load())
                return HOST_MACHINE_FAILED;
        (void)host_write_text(HOST_MACHINE_SOURCED, "1\n");
        rover_runner_own();
        rover_runner_kernel_states();
        (void)rover_runner_settings(null);
        rover_runner_keys();
        rover_runner_find_everys();
        for (positive at = 0; at < ROVER_SERVICES_MOST; at++)
                rover_runner.service[at].pidfd = -1;

        rover_runner.watch = system_call_2(syscall(inotify_init1), O_CLOEXEC | O_NONBLOCK, 0);
        if (rover_runner.watch >= 0)
        {
                (void)system_call_3(syscall(inotify_add_watch), (positive)rover_runner.watch, (positive) "/root", 0x88);
                (void)system_call_3(syscall(inotify_add_watch), (positive)rover_runner.watch, (positive) "/run/moonwater", 0x188);
        }
        if (system_call_2(syscall(pipe2), (positive)rover_runner.kernel_pipe, O_CLOEXEC) < 0 ||
            !thread_start(rover_runner_kernel_thread, null))
        {
                rover_runner_say("no thread for the kernel's events", null, null);
                return HOST_MACHINE_FAILED;
        }

        //      Loading a table is every state changing at once.
        {
                struct rover_event edge;

                memory_zero(&edge, sizeof(edge));
                edge.when = clock_monotonic_nanoseconds();
                edge.realtime = rover_runner_realtime();
                rover_runner_fire(&edge, ~0ull);
        }

        for (;;)
        {
                positive count = 0;
                p64 now = clock_monotonic_nanoseconds();
                timespec limit = {0, 100000000};

                //      The keepers the machine loop ran before each wait: the radio, USB, the clock, the link, ssh.
                if (now - last_keep >= (p64)locale_wake_ms(HOST_RADIO_WAIT_MS) * 1000000)
                {
                        radio_recover();
                        usb_recover();
                        locale_recover();
                        link_keep_machine();
                        ssh_keep_machine();
                        last_keep = now;
                }
                rover_runner_verdict();
                rover_runner_services();
                rover_runner_clocks(now);
                rover_runner_drain();
                rover_runner_reap();

                wait[count].descriptor = (b32)rover_runner.kernel_pipe[0];
                wait[count++].events = SYSTEM_POLL_READ;
                wait[count].descriptor = (b32)rover_runner.fire;
                wait[count++].events = SYSTEM_POLL_READ;
                wait[count].descriptor = (b32)rover_runner.watch;
                wait[count++].events = SYSTEM_POLL_READ;
                for (positive at = 0; at < rover_runner.view.head->services; at++)
                {
                        wait[count].descriptor = (b32)rover_runner.service[at].pidfd;
                        wait[count++].events = SYSTEM_POLL_READ;
                }
                for (positive at = 0; at < count; at++)
                        wait[at].returned = 0;
                if (system_poll_wait(wait, count, &limit, null) < 0)
                        continue;
                if (wait[0].returned & SYSTEM_POLL_READ)
                {
                        p32 event;

                        if (system_read_once(rover_runner.kernel_pipe[0], &event, 4) == 4)
                        {
                                if (event == ~0u || !event)
                                        break; // the wait refused, or the machine is stopping
                                if (event <= SPARK_BIND_EVENTS)
                                {
                                        p64 changed = rover_runner_kernel_event((p16)event);
                                        struct rover_event kernel;

                                        memory_zero(&kernel, sizeof(kernel));
                                        kernel.id = (p16)event;
                                        kernel.when = clock_monotonic_nanoseconds();
                                        kernel.realtime = rover_runner_realtime();
                                        rover_runner_fire(&kernel, changed);
                                }
                        }
                }
                if (wait[1].returned & SYSTEM_POLL_READ)
                        rover_runner_fire_lines();
                if (wait[2].returned & SYSTEM_POLL_READ)
                        rover_runner_watched();
                for (positive at = 0; at < rover_runner.view.head->services; at++)
                        if (wait[3 + at].returned & SYSTEM_POLL_READ)
                                rover_runner_ended(at);
                rover_runner_drain();
        }
        for (positive at = 0; at < rover_runner.view.head->services; at++)
                rover_runner_stop(at);
        host_machine_end_run();
        return HOST_MACHINE_ENDED;
}

//      Whether this machine runs rules: the image carries them.
static bool rover_runner_chosen(void)
{
        file_facts facts;

        return file_look(AT_FDCWD, ROVER_IMAGE_RULES, 0, address_of facts) && (facts.mode & MODE_FORMAT) == MODE_DIRECTORY;
}

#endif /* ROVER_RUNNER_INCLUDED */
