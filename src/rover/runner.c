/*
        The runner: rover's userspace end, the machine process with rules in
        place of the machine script. Userspace only; included by host.c after
        the verbs it calls, so its actions are the verbs' own code.

        It is the process PID 1 keeps as the machine (moonwater machine). It
        compiles the image's rules and the
        person's into one table (src/rover/load.c), hands it to the kernel
        (ROVER_IOCTL), keeps the three state pages, supervises the services,
        hears every event there is, and does what the rules say:

            kernel events   the kernel's ring (src/moonwater/rules.c): a kernel rule's hit, whose
                            actions that are not the kernel's are done here, and an event a runner's
                            rule waits for; the machine's wait says when it has records;
            kernel states   the kernel's page, mapped and copied, with the boot words over it;
            desktop events  the compositor's keys.fire lines and pin files, turned into chord and
                            button events;
            settings        the files the Settings rows keep, by inotify on /root;
            the boot        its verdict, once, when /run/moonwater/verdict appears;
            clocks          every DURATION and at CLOCK;
            services        their lives, as service up, down and failed.

        A kernel rule runs in the kernel and nowhere else; a runner's rule's
        kernel primitive is asked of the kernel (ROVER_OP_DO), as a kernel
        rule's is queued there. Everything the runner starts is a child of
        its own: a service is watched by its pidfd, anything else is reaped by
        its pid.
*/

#ifndef ROVER_RUNNER_INCLUDED
#define ROVER_RUNNER_INCLUDED

#define ROVER_IMAGE_RULES "/lib/moonwater/rules"
#define ROVER_ROOT_SCRIPTS "/root/rules/scripts"
#define ROVER_IMAGE_SCRIPTS "/lib/moonwater/rules/scripts"
#define ROVER_KEYS_FILE "/run/moonwater/keys"
#define ROVER_KEYS_FIRE "/run/moonwater/keys.fire"
#define ROVER_VERDICT "/run/moonwater/verdict"
#define ROVER_CHILDREN_MOST 1024u
#define ROVER_QUEUE_MOST 32u
#define ROVER_STOP_GRACE_NS 3000000000ull // TERM, then KILL after this
#define ROVER_KEYS_MOST 32u
#define ROVER_SECOND_NS 1000000000ull
#define ROVER_DESKTOP_UP "/run/moonwater/windows" // the compositor's table: the desktop is up
#define ROVER_HELD_MOST 8u

struct rover_held
{
        p8 word[ROVER_ARGUMENTS + 1][ROVER_WORD_MOST + 2];
        positive count;
};

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
        p8 up_owed;     // up before boot's verdict: its service up is posted at the verdict
        p8 held_off;    // stopped by a rule's stop: not started again until a rule's start or restart
        p8 usr1_owed;   // to be told to read /root again (SIGUSR1) once it is up
};

static struct
{
        p64 table_words[ROVER_TABLE_MOST / 8];
        positive size;
        struct rover_table view;
        struct rover_run runs[ROVER_RULES_MOST];
        p64 page_words[ROVER_PAGES][ROVER_PAGE_BYTES / 8];
        const p8 address_to pages[ROVER_PAGES];
        struct rover_running service[ROVER_SERVICES_MOST];
        bipolar children[ROVER_CHILDREN_MOST];
        p8 child_name[ROVER_CHILDREN_MOST][48]; // each child's program, for the line its failure is said in
        p8 child_desktop[ROVER_CHILDREN_MOST];  // a program opened on the desktop, ended with the compositor
        struct rover_event queue[ROVER_QUEUE_MOST]; // cues and the runner's own events, done after the one in hand
        p8 queue_text[ROVER_QUEUE_MOST][ROVER_WORD_MOST + 1];
        positive queued;
        const p8 address_to map; // the kernel's page, its ring's head and its records (ROVER_MAP_BYTES)
        p64 tail;                // the next record to read
        p32 table;               // the kernel's number for the load the runner has, 0 for none
        p64 stood;               // monotonic: the load has stood a minute at this, and the count of loads goes to 0
        p16 boot[16];            // the boot.NAME states, which the runner keeps over the kernel's page
        positive boots;
        bipolar kernel_pipe[2];
        bipolar device;
        bipolar watch;
        bipolar fire;
        p64 every_next[8];
        p64 every_period[8];
        positive everys;
        b64 minute; // the last minute an at was posted for
        bool verdict_seen;   // boot's verdict is settled (live or disk): /root is the session's
        bool verdict_posted; // the boot event was posted (at the first word, ask included)
        p8 compositor_font[256]; // /root/font's first line when the compositor was started
        p8 pin_text[1024];                       // the pinned program's words, the last pin pressed
        string_address pin[ROVER_ARGUMENTS + 5];
        positive pins;
        struct rover_held held[ROVER_HELD_MOST]; // opens asked before the desktop was up
        bool desktop_was_up;                     // the desktop has been up this boot: opens are not held for it again
        positive helds;
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

// ---- the table -------------------------------------------------------------

static fn rover_runner_say(string_address first, string_address second, string_address third)
{
        string_address line[] = {(string_address) "rover: ", first, second ? second : (string_address) "",
                                 third ? third : (string_address) "", null};

        host_kmsg(line);
}

static const p8 address_to rover_runner_pool_word(p32 at, positive address_to length);
static fn rover_runner_child(bipolar pid);
static fn rover_runner_leaving(bipolar pid);
static p8 rover_runner_naming[48] = "a program"; // the program the next child kept is, for the line its failure is said in
static bool rover_runner_on_desktop; // the next child kept is a program opened on the desktop
static fn rover_runner_find_everys(void);
static fn rover_runner_keys(void);

/*
        The table of bytes given, taken: its view, its runs, its clocks
        and its chords. A service of the table before keeps its life when the
        new one has a service of its name; one it does not have is stopped.
        table is the kernel's number for the load, 0 when the kernel has none.
*/
static bool rover_runner_adopt_bytes(const p8 address_to bytes, positive size, p32 table)
{
        static struct rover_running before[ROVER_SERVICES_MOST];
        static p8 names[ROVER_SERVICES_MOST][ROVER_WORD_MOST + 1];
        static bool kept[ROVER_SERVICES_MOST];
        positive had = rover_runner.view.head ? rover_runner.view.head->services : 0;
        struct rover_refusal refusal;
        struct rover_table view;

        if (size > sizeof(rover_runner.table_words) || !rover_check(bytes, size, &view, &refusal))
        {
                rover_runner_say("the table is refused: ", (string_address)refusal.why, null);
                return false;
        }
        for (positive at = 0; at < had; at++)
        {
                positive length;
                const p8 address_to name = rover_runner_pool_word(rover_runner.view.service[at].name, &length);

                memory_copy(names[at], name, length);
                names[at][length] = 0;
                before[at] = rover_runner.service[at];
                kept[at] = false;
        }
        rover_runner.size = size;
        memory_copy(rover_runner.table_words, bytes, rover_runner.size);
        (void)rover_check((const p8 address_to)rover_runner.table_words, rover_runner.size, &rover_runner.view, &refusal);
        memory_zero(rover_runner.runs, sizeof(rover_runner.runs));
        for (positive at = 0; at < rover_runner.view.head->services; at++)
        {
                positive length;
                const p8 address_to name = rover_runner_pool_word(rover_runner.view.service[at].name, &length);

                memory_zero(&rover_runner.service[at], sizeof(rover_runner.service[at]));
                rover_runner.service[at].pidfd = -1;
                for (positive k = 0; k < had; k++)
                        if (!kept[k] && rover_bytes_are(name, length, (const char address_to)names[k]))
                        {
                                rover_runner.service[at] = before[k];
                                kept[k] = true;
                        }
        }
        for (positive k = 0; k < had; k++)
                if (!kept[k] && before[k].pid > 0)
                {
                        p8 path[96];

                        (void)system_call_2(syscall(kill), (positive)before[k].pid, 15);
                        if (before[k].pidfd >= 0)
                                system_close(before[k].pidfd);
                        rover_runner_leaving(before[k].pid);
                        if (string_join(path, sizeof(path), HOST_STATE "/", (string_address)names[k]) &&
                            string_join(path, sizeof(path), (string_address)path, ".pid"))
                                (void)system_remove_at(AT_FDCWD, (string_address)path, 0);
                }
        rover_runner.table = table;
        rover_runner.stood = clock_monotonic_nanoseconds() + ROVER_STOOD_NS;
        rover_runner_keys();
        rover_runner_find_everys();
        return true;
}

static bool rover_runner_adopt(p32 table)
{
        return rover_runner_adopt_bytes(rover_built(&rover_load_all), rover_load_all.size, table);
}

/*
        The table a hand load gave the kernel, as `moonwater rover load` left
        it in ROVER_LOADED: its load's number, then its bytes. Taken only when
        the number is the kernel's live one, so the runner reads the kernel's
        records by the very table the kernel has, never by the files as they
        are now.
*/
static bool rover_runner_adopt_loaded(p32 table)
{
        static p64 words[(ROVER_TABLE_MOST + 8) / 8];
        p8 address_to bytes = (p8 address_to)words;
        bipolar handle = system_open_at(AT_FDCWD, ROVER_LOADED, FILE_READ | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
        bipolar got = handle < 0 ? -1 : system_read_retry((positive)handle, bytes, sizeof(words));
        file_facts facts;
        bool owned = handle >= 0 && file_look(handle, (string_address) "", AT_EMPTY_PATH, address_of facts) &&
                     (facts.mode & MODE_FORMAT) == MODE_FILE && facts.owner == 0 && !(facts.mode & 0077);
        p32 number;

        if (handle >= 0)
                system_close(handle);
        if (!owned || got <= 8)
                return false;
        memory_copy(&number, bytes, 4);
        //      The table starts 8 bytes in, where it keeps its own alignment.
        return number == table && rover_runner_adopt_bytes(bytes + 8, (positive)got - 8, table);
}

/*
        The rules at the start: compiled, handed to the kernel, taken. A set
        that does not compile at all is tried again as the image's alone; a
        table the kernel refuses is still the runner's, with no kernel rule
        run (the kernel says why).
*/
static bool rover_runner_load(void)
{
        bipolar table = rover_load_now(rover_runner.device, true);

        if (table == -22)
        {
                rover_load_forced_safe = true;
                table = rover_load_now(rover_runner.device, true);
        }
        if (table == -22)
                return false;
        if (table < 0)
                rover_runner_say("the kernel took no table: its rules are not run", null, null);
        return rover_runner_adopt(table > 0 ? (p32)table : 0);
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

        rover_load_one.why_used = 0;
        if (!state || !rover_literal(&rover_load_one, &typed, state->type, &number))
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
        //      A value its state's type does not take keeps the state as it was, and is said (it was dropped silently).
        if (!rover_runner_set_word(name, word, string_length(word)))
                rover_runner_say((string_address)row->path, " holds a value its setting does not take; the rules keep the one before: ",
                                 (string_address)word);
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

//      boot.NAME from moonwater.NAME=VALUE on the kernel's command line, one word each, into the runner's kernel page:
//      the kernel keeps them nowhere, so the copy of its page keeps them over it.
static fn rover_runner_boot_states(void)
{
        p8 line[4096];
        bipolar handle = system_open_at(AT_FDCWD, "/proc/cmdline", FILE_READ | O_CLOEXEC);
        bipolar length = handle < 0 ? 0 : system_read_retry((positive)handle, line, sizeof(line) - 1);

        if (handle >= 0)
                system_close(handle);
        line[length > 0 ? length : 0] = 0;
        rover_runner.boots = 0;
        for (positive at = 0; at < ROVER_STATES; at++)
        {
                const char address_to name = rover_states[at].name;
                p8 key[64];
                string_address found;
                positive size = 0;

                if (memory_compare(name, "boot.", 5) || rover_states[at].page != ROVER_PAGE_KERNEL ||
                    rover_runner.boots == array_count(rover_runner.boot))
                        continue;
                rover_runner.boot[rover_runner.boots++] = (p16)at;
                if (!string_join(key, sizeof(key), "moonwater.", (string_address)(name + 5), "=") ||
                    !(found = string_search(line, key)))
                        continue;
                found += string_length(key);
                while (found[size] && found[size] != ' ' && found[size] != '\n')
                        size++;
                rover_runner_put(&rover_states[at], 0, found, size);
        }
}

/*
        The kernel's state page, copied whole when its sequence is even and
        the same after (the kernel writes it under its lock, odd while it
        does), with the boot words and the zone kept over it. Answers the
        states that changed since the last copy, as the reads mask has them.
*/
static p64 rover_runner_kernel_page(void)
{
        static p64 fresh_words[ROVER_PAGE_BYTES / 8];
        p8 address_to fresh = (p8 address_to)fresh_words, address_to mine = rover_runner_page(ROVER_PAGE_KERNEL);
        const volatile p32 address_to sequence;
        p64 changed = 0;
        b32 east;

        if (!rover_runner.map)
                return 0;
        sequence = (const volatile p32 address_to)(rover_runner.map + 4);
        bool ok = false;
        for (positive tries = 0; tries < 16; tries++)
        {
                p32 before = *sequence;

                if (before & 1)
                        continue;
                __atomic_thread_fence(__ATOMIC_ACQUIRE);
                memory_copy(fresh, (const void address_to)rover_runner.map, ROVER_PAGE_BYTES);
                __atomic_thread_fence(__ATOMIC_ACQUIRE);
                if (*sequence == before)
                {
                        ok = true;
                        break;
                }
        }
        if (!ok)
                return 0;
        for (positive at = 0; at < rover_runner.boots; at++)
        {
                const struct rover_state_row address_to state = &rover_states[rover_runner.boot[at]];

                memory_copy(fresh + state->offset, mine + state->offset, ROVER_WORD_SLOT);
        }
        east = (b32)clock_local_east(rover_runner_realtime());
        memory_copy(fresh + ROVER_PAGE_ZONE, &east, 4);
        for (positive at = 0; at < ROVER_STATES; at++)
        {
                const struct rover_state_row address_to state = &rover_states[at];

                if (state->page == ROVER_PAGE_KERNEL &&
                    memory_compare(fresh + state->offset, mine + state->offset,
                                   rover_kind_is_word(rover_types[state->type].kind) ? ROVER_WORD_SLOT : 4))
                        changed |= ROVER_READS_BIT(at);
        }
        memory_copy(mine, fresh, ROVER_PAGE_BYTES);
        return changed;
}

// ---- children --------------------------------------------------------------

/*
        The runner's children reaped, and one that failed said: an exit
        other than 0 (127 is an exec that failed) or a signal, with its pid
        and its program, which was said nowhere before.
*/
static fn rover_runner_reap(void)
{
        for (positive at = 0; at < ROVER_CHILDREN_MOST; at++)
        {
                b32 status = 0;
                bipolar pid = rover_runner.children[at];

                if (!pid || system_wait4_retry(pid, &status, 1 /* WNOHANG */, null) == 0)
                        continue;
                rover_runner.children[at] = 0;
                if (status)
                {
                        p8 number[24], code[24];

                        number[positive_into_string(number, (positive)pid)] = 0;
                        code[positive_into_string(code, (status & 0x7f) ? (positive)(status & 0x7f) : (positive)((status >> 8) & 0xff))] = 0;
                        rover_runner_say((string_address)rover_runner.child_name[at],
                                         (status & 0x7f) ? " was ended by signal " : " exited with status ", (string_address)code);
                }
        }
}

/*
        A child kept to be reaped. A pid is never dropped (one was, past 64,
        and left a zombie for good): with every slot taken the ended are
        reaped first, and with none ended the oldest is waited for here,
        which holds the runner only while a thousand children run at once.
*/
static fn rover_runner_child(bipolar pid)
{
        for (positive pass = 0; pass < 2; pass++)
        {
                for (positive at = 0; at < ROVER_CHILDREN_MOST; at++)
                        if (!rover_runner.children[at])
                        {
                                rover_runner.children[at] = pid;
                                string_copy_max(rover_runner.child_name[at], (string_address)rover_runner_naming,
                                                sizeof(rover_runner.child_name[at]));
                                rover_runner.child_desktop[at] = rover_runner_on_desktop;
                                rover_runner_on_desktop = false;
                                return;
                        }
                rover_runner_reap();
        }
        rover_runner_say("a thousand children run at once: the runner waits for the oldest", null, null);
        (void)system_wait4_retry(rover_runner.children[0], null, 0, null);
        rover_runner.children[0] = pid;
}

/*
        A service a reload dropped: TERM was sent, and it is reaped as a child,
        and KILLed if it is still there after the grace a stop gives.
*/
#define ROVER_LEAVING_MOST 16u
static struct
{
        bipolar pid;
        p64 kill_at;
} rover_runner_gone[ROVER_LEAVING_MOST];

static fn rover_runner_leaving(bipolar pid)
{
        for (positive at = 0; at < ROVER_LEAVING_MOST; at++)
                if (!rover_runner_gone[at].pid)
                {
                        rover_runner_gone[at].pid = pid;
                        rover_runner_gone[at].kill_at = clock_monotonic_nanoseconds() + ROVER_STOP_GRACE_NS;
                        return;
                }
        (void)system_call_2(syscall(kill), (positive)pid, 9);
        rover_runner_child(pid);
}

static fn rover_runner_gone_look(p64 now)
{
        for (positive at = 0; at < ROVER_LEAVING_MOST; at++)
        {
                bipolar pid = rover_runner_gone[at].pid;

                if (!pid)
                        continue;
                if (system_wait4_retry(pid, null, 1 /* WNOHANG */, null) != 0)
                        rover_runner_gone[at].pid = 0;
                else if (now >= rover_runner_gone[at].kill_at)
                {
                        (void)system_call_2(syscall(kill), (positive)pid, 9);
                        rover_runner_gone[at].pid = 0;
                        rover_runner_child(pid);
                }
        }
}

//      A program started from words: its own session, standard input nothing, its output where it is told. extra is
//      one more NAME=value for its environment (MOONWATER_ASKED=1 for what a person asked for). Answers its pid.
static bipolar rover_runner_exec(string_address address_to argv, string_address log_path, string_address extra)
{
        static string_address environment[256];
        string_address address_to inherited = program_environment_list();
        positive used = 0;
        bipolar pid;

        string_copy_max(rover_runner_naming, argv[0], sizeof(rover_runner_naming));
        pid = system_fork();
        if (pid < 0)
                rover_runner_on_desktop = false; // no child to mark
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
#ifdef WINDOWS_ASKED_DIR
        // A person's ask (MOONWATER_ASKED=1): recorded for this process where only root writes, which is what the compositor gives a Wayland
        // client's first window the keys by (compositor_wl_asked; the client's own environment is anyone's to set).
        if (extra && string_has_prefix(extra, WINDOW_ASKED_VARIABLE "=1"))
                windows_asked_record();
#endif
        bipolar null_handle = system_open_at(AT_FDCWD, "/dev/null", FILE_READ);
        bipolar out = system_open_at_mode(AT_FDCWD, log_path ? log_path : (string_address) "/dev/kmsg",
                                          FILE_WRITE | O_CREAT | O_APPEND | O_NOFOLLOW, 0600);
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

        if (pid < 0)
        {
                rover_runner_say("a verb could not be started (no process to be had): moonwater ", words[0], null);
                return;
        }
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

//      An event on the queue: false when the queue was full and it was dropped (said), so no caller writes to the tail.
static bool rover_runner_post(p16 id, const p8 address_to word, positive length, b64 number)
{
        struct rover_event address_to event;
        positive at = rover_runner.queued;

        if (at == ROVER_QUEUE_MOST)
        {
                rover_runner_say("an event is dropped: the runner's queue is full", null, null);
                return false;
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
        return true;
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

        (void)rover_runner_post(id, name, length, 0);
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

static bool rover_runner_service_is(positive which, const char address_to name)
{
        positive length;
        const p8 address_to word = rover_runner_pool_word(rover_runner.view.service[which].name, &length);

        return rover_bytes_are(word, length, name);
}

//      The desktop is up while the compositor's service runs and has said it is up: a table file a killed compositor
//      left behind said up when nothing was.
static bool rover_runner_desktop_up(void)
{
        for (positive at = 0; rover_runner.view.head && at < rover_runner.view.head->services; at++)
                if (rover_runner_service_is(at, "compositor"))
                {
                        bool up = rover_runner.service[at].pid > 0 && rover_runner.service[at].up;

                        rover_runner.desktop_was_up = rover_runner.desktop_was_up || up;
                        return up;
                }
        return false;
}

//      HOST_STATE/NAME.pid while a service runs, which the verbs that tell one find it by (desktop's compositor.pid).
static fn rover_runner_pid_file(positive which, bipolar pid)
{
        positive length;
        const p8 address_to name = rover_runner_pool_word(rover_runner.view.service[which].name, &length);
        p8 path[96], text[24];

        if (length > 48 || !string_join(path, sizeof(path), HOST_STATE "/", (string_address)name) ||
            !string_join(path, sizeof(path), (string_address)path, ".pid"))
                return;
        if (pid <= 0)
        {
                (void)system_remove_at(AT_FDCWD, (string_address)path, 0);
                return;
        }
        positive used = positive_into_string(text, (positive)pid);
        text[used++] = '\n';
        (void)host_write_file((string_address)path, text, used, 0644, false);
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
        //      The face the compositor starts with, to know at the verdict whether the disk's /root names another.
        if (rover_runner_service_is(which, "compositor") &&
            host_read_word("/root/font", rover_runner.compositor_font, sizeof(rover_runner.compositor_font)) < 0)
                rover_runner.compositor_font[0] = 0;
        //      A ready path left by the life before is no word of this one's: removed before the start.
        if (service->ready)
                (void)system_remove_at(AT_FDCWD, (string_address)rover_runner_pool_word(service->ready, &length), 0);
        running->pid = rover_runner_exec(argv,
                                         service->log ? (string_address)rover_runner_pool_word(service->log, &length) : null,
                                         null);
        running->pidfd = running->pid > 0 ? system_call_2(syscall(pidfd_open), (positive)running->pid, 0) : -1;
        if (running->pidfd < 0)
                running->pidfd = -1; // looked at by its pid each turn (rover_runner_services)
        running->started = clock_monotonic_nanoseconds();
        running->stopping = 0;
        running->up = 0;
        running->usr1_owed = 0; // a life started now reads the /root there is
        if (running->pid <= 0)
                running->pid = 0;
        rover_runner_pid_file(which, running->pid);
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

//      Whether the compositor's service needs this one.
static bool rover_runner_compositor_needs(positive which)
{
        for (positive at = 0; at < rover_runner.view.head->services; at++)
        {
                const struct rover_service address_to service = rover_runner.view.service + at;

                if (!rover_runner_service_is(at, "compositor"))
                        continue;
                for (positive need = 0; need < ROVER_NEEDS && service->need[need] != ROVER_SERVICE_NONE; need++)
                        if (service->need[need] == which)
                                return true;
        }
        return false;
}

//      A service that ended: the count of its short lives, its wait or its giving up, and the events.
static fn rover_runner_ended(positive which)
{
        const struct rover_service address_to service = rover_runner.view.service + which;
        struct rover_running address_to running = &rover_runner.service[which];
        p64 now = clock_monotonic_nanoseconds();
        bool stopped = running->stopping != 0;

        if (running->pid > 0)
                system_wait4_retry(running->pid, null, 0, null);
        if (running->pidfd >= 0)
                system_close(running->pidfd);
        running->pid = 0;
        running->pidfd = -1;
        rover_runner_pid_file(which, 0);
        //      The desktop's programs end with its compositor, as the session the machine script kept (desktop.kids) did:
        //      left, they ran on with no window to show.
        if (rover_runner_service_is(which, "compositor"))
                for (positive at = 0; at < ROVER_CHILDREN_MOST; at++)
                        if (rover_runner.children[at] > 0 && rover_runner.child_desktop[at])
                                (void)system_call_2(syscall(kill), (positive)rover_runner.children[at], 15);
        running->up = 0;
        running->stopping = 0;
        rover_runner_service_event(98, which);
        if (stopped)
                return;
        //      A long life's end starts the count again and is no short life itself: four short lives in a row, not three
        //      after a long one.
        if (now - running->started >= (p64)service->short_ms * 1000000)
        {
                running->shorts = 0;
                running->retry = now;
                return;
        }
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
                bool wanted = rover_service_wanted(&rover_runner.view, which, rover_runner.pages, rover_runner_realtime()) && rover_runner_needs_up(which);

                //      One with no pidfd (pidfd_open refused it) is seen to end by its pid.
                if (running->pid && running->pidfd < 0 &&
                    system_wait4_retry(running->pid, null, 1 /* WNOHANG */, null) == running->pid)
                {
                        running->pid = -running->pid; // reaped: ended must not wait for it again
                        rover_runner_ended(which);
                        continue;
                }
                //      A service given up is let start again once it stops being wanted (desktop rest turns Canvas off, and
                //      moonwater desktop on turns it on), as desktop.c promises: given up was for good.
                if (!wanted && running->failed)
                        running->failed = 0, running->shorts = 0, running->retry = 0;
                if (running->pid && !wanted)
                        rover_runner_stop(which);
                if (running->stopping && now - running->stopping >= ROVER_STOP_GRACE_NS)
                        system_call_2(syscall(kill), (positive)running->pid, 9);
                //      Before boot's verdict /root is the image's, and a disk's is mounted over it at the verdict: a service
                //      started now would read the image's settings and keep watching the directory underneath (the
                //      session bus read no canvas.theme). Only the compositor starts early, for the screen, and is told to
                //      read /root again at the verdict. Under ask the question is put on that screen, so what the compositor
                //      needs (the session bus, which follows its /root being bound over) starts then too: held, the
                //      compositor never started, nothing asked, and the boot stayed on Canvas's grey with no services.
                if (!running->pid && wanted && !running->failed && !running->held_off && now >= running->retry &&
                    (rover_runner.verdict_seen || rover_runner_service_is(which, "compositor") ||
                     (rover_runner.verdict_posted && rover_runner_compositor_needs(which))))
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
                                if (running->usr1_owed)
                                {
                                        running->usr1_owed = 0;
                                        (void)system_call_2(syscall(kill), (positive)running->pid, 10 /* SIGUSR1 */);
                                }
                                //      Its rules (autostart, the keyboard, the installer) read /root: they hear it at the verdict.
                                if (rover_runner.verdict_seen)
                                        rover_runner_service_event(97, which);
                                else
                                        running->up_owed = 1;
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

/*
        Whether a pin's first word is a program the machine knows, which is all
        of a pin the runner starts: the pin is made from a window's own
        process (its /proc cmdline), which the window's program chose, so its
        words are never root's argv. A kit program, /NAME, or a bowl's
        launcher, /bowls/bin/NAME, NAME a bare word, that is a regular file
        (through its link) of root's that no one else may write.
*/
static bool rover_runner_pinned(string_address path)
{
        string_address name = null;
        file_facts facts;

        if (string_has_prefix(path, "/bowls/bin/"))
                name = path + 11;
        else if (path[0] == '/')
                name = path + 1;
        if (!name || !name[0] || string_length(name) > 48)
                return false;
        for (string_address at = name; *at; at++)
                if (!((*at >= 'a' && *at <= 'z') || (*at >= 'A' && *at <= 'Z') || (*at >= '0' && *at <= '9') ||
                      *at == '-' || *at == '_' || (*at == '.' && at != name)))
                        return false;
        return file_look(AT_FDCWD, path, 0, address_of facts) && (facts.mode & MODE_FORMAT) == MODE_FILE &&
               facts.owner == 0 && !(facts.mode & 0022);
}

/*
        A run's script, looked at as it is run: the directory, opened with no
        link followed, root's and not written by anyone else (for the
        person's, 0700, so nothing but root can read or swap in it), and the
        file in it regular, root's and written by root alone, looked at
        through the directory's descriptor. Answers 1 with its path in path,
        0 when the directory or the file is not there, and -1 when either is
        there and is not root's alone (which runs nothing, and the image's of
        the name is not tried in its place).
*/
static bipolar rover_runner_script(string_address directory, bool person, string_address name, p8 address_to path,
                                   positive room)
{
        file_facts facts;
        bipolar handle = system_open_at(AT_FDCWD, directory, FILE_READ | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
        bool fits;

        if (handle < 0)
                return 0;
        fits = file_look(handle, (string_address) "", AT_EMPTY_PATH, address_of facts) &&
               (facts.mode & MODE_FORMAT) == MODE_DIRECTORY && facts.owner == 0 &&
               !(facts.mode & (person ? 0077 : 0022));
        if (!fits)
        {
                system_close(handle);
                return -1;
        }
        fits = file_look(handle, name, AT_SYMLINK_NOFOLLOW, address_of facts);
        system_close(handle);
        if (!fits)
                return 0;
        if ((facts.mode & MODE_FORMAT) != MODE_FILE || facts.owner != 0 || (facts.mode & 0022))
                return -1;
        return string_join(path, room, directory, "/", name) ? 1 : 0;
}

//      A primitive that is not the kernel's (those are asked of it, rover_runner_do).
static fn rover_runner_primitive(p16 id, const struct rover_value address_to values, positive count)
{
        p8 path[160];
        string_address words[6] = {(string_address) "moonwater"};

        switch (id)
        {
        case 12: // display off: the compositor puts every screen out (DPMS) while the file is there, and a person lights them
                if (host_write_file("/run/moonwater/display-off", (const p8 address_to) "", 0, 0644, false) < 0)
                        rover_runner_say("display off: /run/moonwater/display-off could not be written, the screens stay on",
                                         null, null);
                return;
        case 13: // display on
                (void)system_call_3(syscall(unlinkat), (positive)AT_FDCWD, (positive)(string_address) "/run/moonwater/display-off", 0);
                return;
        case 15: // notify TEXT: a notice on the board, under rules, as a program's notification is; and said in the log
        {
                struct windows *table = windows_board_map();
                static p32 notices;

                if (!table || !windows_issue_set(table, "rules", WINDOWS_ISSUE_WARNING, WINDOWS_ISSUE_NOTIFICATION, ++notices, 1,
                                                 (const char address_to)values[0].text))
                        rover_runner_say("a notice the board did not take: ", (string_address)values[0].text, null);
                else
                        rover_runner_say("notice: ", (string_address)values[0].text, null);
                return;
        }
        case 14: // open APP WORDS: a kit program by its name, or a program by its path, asked for by a person
        {
                file_facts facts;

                if (values[0].length && values[0].text[0] == '/')
                        string_copy_max(path, (string_address)values[0].text, sizeof(path));
                else if (!string_join(path, sizeof(path), "/", (string_address)values[0].text))
                        return;
                //      A window wants the desktop: before the compositor is up the open is held, and done when it is
                //      (rover_runner_held).
                if (!rover_runner_desktop_up())
                {
                        //      Held only while the boot's first desktop is coming: an open asked while a desktop that was up
                        //      is down is dropped (a compositor started again replayed them all at once), and one already
                        //      held for the same program is not held twice.
                        if (rover_runner.desktop_was_up)
                        {
                                rover_runner_say("an open is dropped: the desktop is down: ", (string_address)path, null);
                                return;
                        }
                        for (positive at = 0; at < rover_runner.helds; at++)
                                if (string_equals((string_address)rover_runner.held[at].word[0], (string_address)path))
                                        return;
                        if (rover_runner.helds == ROVER_HELD_MOST)
                        {
                                rover_runner_say("an open is dropped: eight are held for the desktop already", null, null);
                                return;
                        }
                        struct rover_held address_to held = &rover_runner.held[rover_runner.helds++];

                        string_copy_max(held->word[0], (string_address)path, sizeof(held->word[0]));
                        held->count = 1;
                        for (positive at = 1; at < count && held->count < array_count(held->word); at++)
                                string_copy_max(held->word[held->count++], (string_address)values[at].text, sizeof(held->word[0]));
                        return;
                }
                rover_runner_on_desktop = true;
                rover_runner_program(path, values, count, 1, "MOONWATER_ASKED=1");
                return;
        }
        case 20: // open pin: the last pin's program, when it is one the machine knows
        {
                string_address argv[2] = {null, null};

                if (!rover_runner.pins || !rover_runner_pinned(rover_runner.pin[0]))
                {
                        rover_runner_say("a pin names no program of this machine's (a kit program, or a bowl's in /bowls/bin, "
                                         "root's alone); not started: ", rover_runner.pins ? rover_runner.pin[0] : (string_address) "", null);
                        rover_runner.pins = 0;
                        return;
                }
                argv[0] = rover_runner.pin[0];
                rover_runner.pins = 0;
                rover_runner_on_desktop = true;
                bipolar pid = rover_runner_exec(argv, null, "MOONWATER_ASKED=1");
                if (pid > 0)
                        rover_runner_child(pid);
                return;
        }
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

                //      A rule keeps the policy: a service given up stays given up (a person starts it, moonwater rover
                //      load), and a start waits out the wait its last end set. restart is a stop the policy starts again.
                if (running->failed)
                {
                        rover_runner_say("a rule asked for a service that was given up; not started: ",
                                         (string_address)values[0].text, null);
                        return;
                }
                //      stop holds it off until a start; start lets it be started (now, when its if lines hold); restart is
                //      both. A start was nothing at all (the service started only as its if lines said).
                if (id != 16 && running->pid > 0)
                        rover_runner_stop((positive)which);
                running->held_off = id == 17;
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

//      A hit's actions. kernel_did: the hit is a kernel rule's, whose kernel primitives the kernel has done.
/*
        A rule's budget: at most ROVER_BUDGET_MOST firings a second, past
        which its actions are not done (and that is said once a second). The
        depth cut bounds emit's own chains; a loop through anything else (a
        run that writes the setting its rule hears, a when rule whose action
        changes the state it reads, a kernel event the action makes) starts
        each round at depth 0, and is bounded by this.
*/
#define ROVER_BUDGET_MOST 64u
static struct
{
        p64 second;
        p32 count;
        p8 said;
} rover_runner_budget[ROVER_RULES_MOST];

static bool rover_runner_within(p16 rule)
{
        p64 second = clock_monotonic_nanoseconds() / ROVER_SECOND_NS;

        if (rule >= ROVER_RULES_MOST)
                return false;
        if (rover_runner_budget[rule].second != second)
        {
                rover_runner_budget[rule].second = second;
                rover_runner_budget[rule].count = 0;
                rover_runner_budget[rule].said = 0;
        }
        if (++rover_runner_budget[rule].count <= ROVER_BUDGET_MOST)
                return true;
        if (!rover_runner_budget[rule].said)
        {
                positive length;

                rover_runner_budget[rule].said = 1;
                rover_runner_say(rover_runner.view.head && rule < rover_runner.view.head->rules
                                     ? (string_address)rover_runner_pool_word(rover_runner.view.rule[rule].name, &length)
                                     : (string_address) "a rule",
                                 " fired more than 64 times in a second: its actions wait for the next", null);
        }
        return false;
}

static fn rover_runner_do(const struct rover_event address_to event, const struct rover_hit address_to hit, bool kernel_did)
{
        if (!rover_runner_within(hit->rule))
                return;
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
                        if (action->runs != ROVER_KERNEL)
                                rover_runner_primitive(action->id, values, count);
                        else if (!kernel_did)
                        {
                                //      A kernel primitive: asked of the kernel, which queues it as a kernel rule's.
                                struct rover_control request;

                                memory_zero(&request, sizeof(request));
                                request.op = ROVER_OP_DO;
                                request.primitive = action->id;
                                request.number = count ? values[0].number : 0;
                                if (count && values[0].text)
                                        memory_copy(request.text, values[0].text,
                                                    values[0].length < ROVER_TEXT_ROOM - 1 ? values[0].length : ROVER_TEXT_ROOM - 1);
                                if (rover_load_ask(rover_runner.device, &request) < 0)
                                        rover_runner_say("the kernel refused a primitive asked of it", null, null);
                        }
                        break;
                case ROVER_SET:
                        rover_runner_set(action->id, &values[0]);
                        break;
                case ROVER_RUN:
                {
                        //      The person's script of the name, else the image's, each only from a directory and as a
                        //      file root alone may write (rover_runner_script).
                        bipolar found = rover_runner_script(ROVER_ROOT_SCRIPTS, true, (string_address)values[0].text, path,
                                                            sizeof(path));

                        if (found == 0)
                                found = rover_runner_script(ROVER_IMAGE_SCRIPTS, false, (string_address)values[0].text, path,
                                                            sizeof(path));
                        if (found <= 0)
                        {
                                rover_runner_say("run ", (string_address)values[0].text,
                                                 found ? ": its script is not root's alone (the directory root's and, for "
                                                         "/root/rules/scripts, 0700; the file regular, root's, written by "
                                                         "root alone); not run"
                                                       : ": no such script");
                                break;
                        }
                        rover_runner_program(path, values, count, 1, null);
                        break;
                }
                case ROVER_TOOL:
                {
                        //      The tool is an applet of /shell, which answers to its name.
                        string_address argv[ROVER_ARGUMENTS + 2] = {(string_address)rover_tools[action->id]};

                        string_copy_max(rover_runner_naming, (string_address)rover_tools[action->id], sizeof(rover_runner_naming));
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
                        else if (pid < 0)
                                rover_runner_say("a tool could not be started (no process to be had): ", argv[0], null);
                        break;
                }
                case ROVER_EMIT:
                        if (event->depth + 1 < ROVER_EMIT_DEPTH)
                        {
                                if (rover_runner_post(ROVER_CUE, values[0].text, values[0].length, 0))
                                        rover_runner.queue[rover_runner.queued - 1].depth = event->depth + 1;
                        }
                        else
                                rover_runner_say("a cue past depth four is not posted: ", (string_address)values[0].text, null);
                        break;
                }
        }
}

//      An event, asked of the runner's rules and the desktop's (the compositor asks none itself yet), and then the when
//      rules of what it changed; the kernel's rules too when the kernel has no table.
//      As many hits as the table has rules, so no rule of an event, on or when, is cut by the room (the kernel's own
//      room is its own).
static fn rover_runner_fire(const struct rover_event address_to event, p64 changed)
{
        static struct rover_hit hits[ROVER_RULES_MOST];

        for (p8 place = 0; place < ROVER_PLACES; place++)
        {
                positive made;

                if (place == ROVER_KERNEL && rover_runner.table)
                        continue;
                made = rover_fire(&rover_runner.view, rover_runner.runs, rover_runner.pages, event, place, hits, ROVER_RULES_MOST);
                for (positive at = 0; at < made; at++)
                        rover_runner_do(event, &hits[at], false);
                if (changed)
                {
                        struct rover_event edge = *event;

                        edge.id = 0;
                        edge.count = 0;
                        made = rover_edges(&rover_runner.view, rover_runner.runs, rover_runner.pages, changed, &edge, place,
                                           hits, ROVER_RULES_MOST);
                        for (positive at = 0; at < made; at++)
                                rover_runner_do(&edge, &hits[at], false);
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

                if (row->event != 64 || !row->trigger)
                        continue;
                if (rover_runner.key_count == ROVER_KEYS_MOST)
                {
                        rover_runner_say("a chord rule past the 32 the desktop hears is not heard", null, null);
                        continue;
                }
                chord = memory_load_unaligned(b64, rover_runner.view.pool + rover_runner.view.comparison[row->comparison].b.index);
                rover_puts(&name, "key");
                for (positive k = 0; k < array_count(rover_modifiers); k++)
                        if ((chord >> 16) & rover_modifiers[k].bit)
                        {
                                rover_puts(&name, "_");
                                rover_puts(&name, rover_modifiers[k].name);
                        }
                //      The key by Linux's number, which the compositor reads as it reads a name (compositor_key_word), so a
                //      key rover names and the compositor's table does not is still heard.
                {
                        p8 number[8];

                        number[positive_into_string(number, (positive)(chord & 0xffff))] = 0;
                        rover_puts(&name, "_");
                        rover_puts(&name, (const char address_to)number);
                }
                name.bytes[name.used] = 0;
                rover_runner.keys[rover_runner.key_count++].chord = chord;
                rover_put(&out, name.bytes, name.used);
                rover_puts(&out, "\n");
        }
        //      The compositor reads its chords from this file: one not written leaves it the old table, which is said.
        if (host_write_file(ROVER_KEYS_FILE, text, out.used, 0644, false) < 0)
                rover_runner_say("the chords could not be written to " ROVER_KEYS_FILE ": the desktop keeps the ones before",
                                 null, null);
        //      The FIFO is made once and kept open across loads: made again at every load, the old descriptor was left
        //      open and a key said between the unlink and the mknod found no FIFO at all.
        if (rover_runner.fire > 0)
                return;
        (void)system_call_3(syscall(unlinkat), (positive)AT_FDCWD, (positive)ROVER_KEYS_FIRE, 0);
        (void)system_call_4(syscall(mknodat), (positive)AT_FDCWD, (positive)ROVER_KEYS_FIRE, 0010600, 0);
        rover_runner.fire = system_open_at(AT_FDCWD, ROVER_KEYS_FIRE, 2 /* O_RDWR */ | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW);
}

/*
        A pin's line, pin_start_N: N whole, its digits and nothing else, into
        digits. The taskbar makes N from the milliseconds of uptime and a count
        (taskbar_pin_start), nine digits from 156 s on and more later, so N is
        taken whole up to ROVER_PIN_DIGITS: one cut short names another file.
*/
#define ROVER_PIN_DIGITS 19u
static bool rover_runner_pin_number(const p8 address_to line, positive length, p8 address_to digits)
{
        positive count = length - 10;

        if (length <= 10 || count > ROVER_PIN_DIGITS)
                return false;
        for (positive at = 0; at < count; at++)
        {
                if (line[10 + at] < '0' || line[10 + at] > '9')
                        return false;
                digits[at] = line[10 + at];
        }
        digits[count] = 0;
        return true;
}

//      One line the compositor said: a chord's key_ name, a button's word, or a pin's start.
static fn rover_runner_line(const p8 address_to line, positive length)
{
        static const char address_to const buttons[6] = {"launcher_button", "volume_button", "mic_button",
                                                        "notices_button", "tray_double", null};

        for (positive at = 0; at < rover_runner.key_count; at++)
                if (rover_bytes_are(line, length, (const char address_to)rover_runner.keys[at].name))
                {
                        (void)rover_runner_post(64, null, 0, rover_runner.keys[at].chord);
                        return;
                }
        for (positive at = 0; buttons[at]; at++)
                if (rover_bytes_are(line, length, buttons[at]))
                {
                        (void)rover_runner_post(71, null, 0, (b64)at);
                        return;
                }
        if (length > 10 && !memory_compare(line, "pin_start_", 10))
        {
                p8 slot_text[ROVER_PIN_DIGITS + 1], path[48];
                bipolar got;

                positive slot = 0, digits_end = 10;

                //      pin_start_N SLOT: the start's number, then the pin's place on the taskbar.
                while (digits_end < length && line[digits_end] != ' ')
                        digits_end++;
                for (positive at = digits_end + 1; at < length && line[at] >= '0' && line[at] <= '9' && slot < 64; at++)
                        slot = slot * 10 + (positive)(line[at] - '0');
                if (!rover_runner_pin_number(line, digits_end, slot_text) ||
                    !string_join(path, sizeof(path), "/run/moonwater/pin.", (string_address)slot_text))
                        return;
                //      The pin's words, a line each, made new by the compositor: the program is the first, and the open
                //      of it is given the rest (rover_runner_pinned).
                bipolar handle = system_open_at(AT_FDCWD, (string_address)path, FILE_READ | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
                file_facts facts;

                //      A regular file only: a FIFO at the name would hold the runner's loop.
                if (handle >= 0 && (!file_look(handle, (string_address) "", AT_EMPTY_PATH, address_of facts) ||
                                    (facts.mode & MODE_FORMAT) != MODE_FILE))
                {
                        system_close(handle);
                        handle = -1;
                }
                got = handle < 0 ? -1 : system_read_retry((positive)handle, rover_runner.pin_text, sizeof(rover_runner.pin_text) - 1);
                if (handle >= 0)
                        system_close(handle);
                (void)system_call_3(syscall(unlinkat), (positive)AT_FDCWD, (positive)path, 0);
                if (got <= 0)
                        return;
                rover_runner.pin_text[got] = 0;
                rover_runner.pins = 0;
                for (p8 address_to word = rover_runner.pin_text; *word && rover_runner.pins < ROVER_ARGUMENTS + 4;)
                {
                        string_address newline = string_first_of(word, '\n');

                        rover_runner.pin[rover_runner.pins++] = (string_address)word;
                        if (!newline)
                                break;
                        newline[0] = 0;
                        word = (p8 address_to)newline + 1;
                }
                if (!rover_runner.pins || !rover_runner_post(71, rover_runner.pin_text, string_length(rover_runner.pin[0]), 5))
                        return;
                struct rover_event address_to event = &rover_runner.queue[rover_runner.queued - 1];
                //      button's fields: name pin, the slot, the program.
                event->count = 3;
                event->field[2] = event->field[0];
                event->field[0].number = 5;
                event->field[0].text = rover_nothing;
                event->field[0].length = 0;
                event->field[1].number = (b64)(slot < 64 ? slot : 63);
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
                //      The machine is stopping: let go of the attach here and now, whatever the loop is doing (a keeper's
                //      or a verb's child it waits for), so the stop that asked for it (shell_stop, host_machine_stop) goes on
                //      at once. It polled for ten seconds for a detach the loop made only after its turn.
                if (!failed && !event)
                {
                        struct machine_control detach;

                        (void)host_machine_ioctl(rover_runner.device, MOONWATER_DETACH, address_of detach);
                }
                system_write_all((positive)rover_runner.kernel_pipe[1], &event, 4);
                if (failed < 0 || !event)
                        return;
        }
}

/*
        The kernel's ring, from the last record read: a kernel rule's hit has
        its actions that are not the kernel's done here, and an event a
        runner's rule waits for is asked of the runner's rules. A record of
        another load than the runner's is a table loaded by hand (moonwater
        rover load): the same files are compiled again here and taken, to
        read it by. A ring that went past the reader is said, and read on
        from what is still there.
*/
static fn rover_runner_ring(void)
{
        const volatile struct rover_ring address_to ring;
        p64 head;
        bool asked = false;

        if (!rover_runner.map)
                return;
        ring = (const volatile struct rover_ring address_to)(rover_runner.map + ROVER_MAP_RING);
        head = ring->head;
        if (head - rover_runner.tail > ROVER_RING_SLOTS)
        {
                rover_runner_say("the kernel's ring went past the runner: records are lost", null, null);
                rover_runner.tail = head - ROVER_RING_SLOTS;
        }
        for (; rover_runner.tail < head; rover_runner.tail++)
        {
                struct rover_record record;
                struct rover_event event;

                if (!rover_record_take(rover_runner.map, rover_runner.tail, &record))
                        continue;
                if (record.table != rover_runner.table && !asked)
                {
                        struct rover_control request;

                        asked = true;
                        memory_zero(&request, sizeof(request));
                        request.op = ROVER_OP_STATUS;
                        //      Compiled as it was loaded: with the person's files, else (a load in safe mode)
                        //      the image's alone, whichever makes as many rules as the kernel holds.
                        if (rover_load_ask(rover_runner.device, &request) >= 0 && request.table &&
                            request.table != rover_runner.table)
                        {
                                bool taken = rover_runner_adopt_loaded(request.table);

                                //      With no word of what was loaded, the files compiled as they were loaded.
                                for (p8 how = 1; how <= 2 && !taken; how++)
                                {
                                        rover_load_asked = how;
                                        taken = rover_load_compile(false) >= 0 &&
                                                rover_load_all.rules == request.rules &&
                                                rover_runner_adopt(request.table);
                                }
                                rover_load_asked = 0;
                                if (!taken)
                                        rover_runner_say("a table loaded by hand is not one these files make: "
                                                         "its records are left", null, null);
                        }
                }
                if (record.table != rover_runner.table)
                        continue;
                rover_record_event(&record, &event);
                //      The record's rule and its run of actions held to the table, as anything read from outside is.
                if (record.kind == ROVER_RECORD_HIT && (record.flags & ROVER_RECORD_USER) &&
                    record.rule < rover_runner.view.head->rules &&
                    (positive)record.action + record.count <= rover_runner.view.head->actions)
                {
                        struct rover_hit hit = {record.rule, record.action, record.count,
                                                (p8)!!(record.flags & ROVER_RECORD_ELSE), 0};

                        rover_runner_do(&event, &hit, true);
                }
                else if (record.kind == ROVER_RECORD_EVENT)
                        rover_runner_fire(&event, 0);
                rover_runner_drain();
        }
}

// ---- the loop --------------------------------------------------------------


//      The opens held for the desktop, done once its table is there.
static fn rover_runner_held(void)
{
        if (!rover_runner.helds || !rover_runner_desktop_up())
                return;
        for (positive at = 0; at < rover_runner.helds; at++)
        {
                string_address argv[ROVER_ARGUMENTS + 2];
                positive used = 0;

                for (; used < rover_runner.held[at].count; used++)
                        argv[used] = (string_address)rover_runner.held[at].word[used];
                argv[used] = null;
                rover_runner_on_desktop = true;
                bipolar pid = rover_runner_exec(argv, null, "MOONWATER_ASKED=1");
                if (pid > 0)
                        rover_runner_child(pid);
        }
        rover_runner.helds = 0;
}

static fn rover_runner_clocks(p64 now)
{
        b64 realtime = rover_runner_realtime(), zoned = realtime + clock_local_east(realtime);
        b64 minute = zoned / 60;

        for (positive at = 0; at < rover_runner.everys; at++)
                if (now >= rover_runner.every_next[at])
                {
                        rover_runner.every_next[at] = now + rover_runner.every_period[at];
                        (void)rover_runner_post(102, null, 0, (b64)rover_runner.every_period[at]);
                }
        /*
                at, once for each minute of the day the clock passes: a minute a
                sleep or a step forward went over is posted late (up to a day of
                them), and one a step back repeats is not posted again, since the
                runner keeps the latest minute it posted and posts only past it.
        */
        if (minute > rover_runner.minute)
        {
                struct rover_event edge;
                bool day = rover_runner.minute / 1440 != minute / 1440;
                b64 from = rover_runner.minute && minute - rover_runner.minute <= 1440 ? rover_runner.minute + 1 : minute;

                rover_runner.minute = minute;
                for (b64 passed = from; passed <= minute; passed++)
                {
                        struct rover_event at;

                        memory_zero(&at, sizeof(at));
                        at.id = 103;
                        at.when = now;
                        at.realtime = realtime;
                        at.count = 1;
                        at.field[0].number = rover_floor_mod(passed, 1440);
                        at.field[0].text = rover_nothing;
                        rover_runner_fire(&at, 0);
                        rover_runner_drain();
                }
                //      The clock moved: the when rules over it (and over the weekday, at a new day) asked again.
                memory_zero(&edge, sizeof(edge));
                edge.when = now;
                edge.realtime = realtime;
                rover_runner_fire(&edge, ROVER_READS_CLOCK | (day ? ROVER_READS_WEEKDAY : 0));
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
//      The boot event, once a boot: the verdict (live, disk or ask) and the disk's name.
static fn rover_runner_boot_event(const p8 address_to word)
{
        static const char address_to const verdicts[3] = {"live", "disk", "ask"};

        for (positive at = 0; at < 3; at++)
                if (string_has_prefix((string_address)word, (string_address)verdicts[at]))
                {
                        string_address disk = string_first_of(word, ' ');

                        if (!rover_runner_post(101, disk ? (const p8 address_to)(disk + 1) : null,
                                               disk ? string_length(disk + 1) : 0, (b64)at))
                                return;
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

static fn rover_runner_verdict(void)
{
        p8 word[HOST_NAME_ROOM + 16];

        bool ask;

        if (rover_runner.verdict_seen || host_read_word(ROVER_VERDICT, word, sizeof(word)) < 0)
                return;
        ask = string_has_prefix((string_address)word, "ask");
        /*
                ask is boot holding while a person is asked which disk: the boot
                event is posted at it (the installer's rule waits for it), but
                /root is not settled until the answer, which boot writes as disk
                NAME or live once it has bound the disk over /root. The re-read
                below waits for that word.
        */
        if (!rover_runner.verdict_posted)
        {
                rover_runner.verdict_posted = true;
                host_restore_session(!ask, false);
                rover_runner_boot_event(word);
        }
        if (ask)
                return;
        rover_runner.verdict_seen = true;
        /*
                /root is the session's from here, a disk's mounted over the
                image's when boot took one: the person's rules on it loaded, the
                watch made on the directory that is there now (the one made at
                the start watches the image's, underneath), every setting read
                again and its when rules asked, the compositor told to read
                /root again (SIGUSR1, as the machine script's desktop_settled
                did), and the services' up said, which their rules (autostart,
                the keyboard, the installer) were held for.
        */
        {
                positive boots = rover_load_boots();
                bipolar table = rover_load_now(rover_runner.device, true);
                p64 changed;

                //      The load at the verdict is this boot's second, not another boot: the count is the start's.
                rover_load_boots_put(boots);
                struct rover_event edge;

                if (table > 0 && table != (bipolar)rover_runner.table)
                        (void)rover_runner_adopt((p32)table);
                if (rover_runner.watch >= 0)
                        (void)system_call_3(syscall(inotify_add_watch), (positive)rover_runner.watch, (positive) "/root", 0x88);
                changed = rover_runner_settings(null);
                memory_zero(&edge, sizeof(edge));
                edge.when = clock_monotonic_nanoseconds();
                edge.realtime = rover_runner_realtime();
                rover_runner_fire(&edge, changed ? changed : ~0ull);
                for (positive at = 0; at < rover_runner.view.head->services; at++)
                {
                        struct rover_running address_to running = &rover_runner.service[at];

                        //      The compositor started on the image's /root: told to read the disk's again, or, when the
                        //      disk's /root/font names another face than it started with, started again with it (the
                        //      machine script's desktop_settled did both).
                        if (running->pid > 0 && string_has_prefix((string_address)word, "disk") &&
                            rover_runner_service_is(at, "compositor"))
                        {
                                p8 font[256];

                                if (host_read_word("/root/font", font, sizeof(font)) < 0)
                                        font[0] = 0;
                                if (!string_equals((string_address)font, (string_address)rover_runner.compositor_font))
                                        rover_runner_stop(at);
                                //      Only once it has said it is up: before, it may not yet block the signal, whose
                                //      default ends it.
                                else if (running->up)
                                        (void)system_call_2(syscall(kill), (positive)running->pid, 10 /* SIGUSR1 */);
                                else
                                        running->usr1_owed = 1;
                        }
                        if (running->up_owed)
                        {
                                running->up_owed = 0;
                                if (running->pid > 0 && running->up)
                                        rover_runner_service_event(97, at);
                        }
                }
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
                                        (void)rover_runner_post(100, row, used, 0);
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
        for (positive at = 0; at < ROVER_PAGES; at++)
                rover_runner.pages[at] = rover_runner_page((p8)at);
        //      The kernel's page and ring, mapped before the load, so no record of the table is missed.
        rover_runner.map = rover_load_map(device, ROVER_MAP_BYTES);
        if (!rover_runner.map)
                rover_runner_say("the kernel's page and ring cannot be mapped: no kernel event is heard", null, null);
        else
                rover_runner.tail = ((const volatile struct rover_ring address_to)(rover_runner.map + ROVER_MAP_RING))->head;
        rover_runner_boot_states();
        (void)rover_runner_kernel_page();
        (void)rover_runner_settings(null);
        if (!rover_runner_load())
                return HOST_MACHINE_FAILED;

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
                (void)rover_runner_desktop_up(); // noted each turn: an open is held only while the first desktop comes
                rover_runner_held();
                rover_runner_clocks(now);
                rover_runner_drain();
                rover_runner_reap();
                rover_runner_gone_look(now);

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
                        p32 event = 0;
                        bipolar got = system_read_once(rover_runner.kernel_pipe[0], &event, 4);

                        //      The machine's wait: 0 when the machine is stopping, MOONWATER_ROVER when the ring has records.
                        if (got == 0 || (got == 4 && (event == ~0u || !event)))
                                break;
                        //      A stop the kernel handed here (an event its bindings give the machine): done.
                        if (got == 4 && event <= SPARK_BIND_EVENTS && spark_bind_is_stop(event))
                        {
                                for (positive at = 0; at < rover_runner.view.head->services; at++)
                                        rover_runner_stop(at);
                                host_machine_end_run();
                                host_machine_close(device);
                                shell_stop(log, event == SPARK_BIND_POWEROFF ? REBOOT_POWER_OFF : REBOOT_RESTART);
                                return HOST_MACHINE_STOPPED;
                        }
                }
                //      Every turn, whatever woke it: the kernel's page looked at (a runner's when rule over a kernel state
                //      hears its change here) and the ring read to its head.
                {
                        p64 changed = rover_runner_kernel_page();

                        rover_runner_ring();
                        if (changed)
                        {
                                struct rover_event edge;

                                memory_zero(&edge, sizeof(edge));
                                edge.when = clock_monotonic_nanoseconds();
                                edge.realtime = rover_runner_realtime();
                                rover_runner_fire(&edge, changed);
                        }
                }
                //      A load that stood a minute: the count of loads that did not is 0 again.
                if (rover_runner.stood && now >= rover_runner.stood)
                {
                        rover_runner.stood = 0;
                        rover_load_boots_put(0);
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


#endif /* ROVER_RUNNER_INCLUDED */
