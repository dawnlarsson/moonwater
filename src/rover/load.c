/*
        Rover and the kernel, from userspace: the verbs that load the rule
        files into the module, ask it what it has done, print its state page
        and watch its ring, and the one loader of the rule files the runner
        (src/rover/runner.c) loads with too. src/moonwater/rules.c is the kernel's
        half and the ABI; src/rover/README.md is the language.

          moonwater rover load       the image's rules and the person's, compiled into one table and loaded
          moonwater rover unload     the kernel's table cleared
          moonwater rover status     what the kernel's rules have done, and how fast
          moonwater rover state      the kernel's state page, one state a line
          moonwater rover watch [N]  the ring's records as they come, N of them, with the time to each

        The image's rules are the program's own (rover_image_file, the files
        /lib/moonwater/rules holds to read) and the person's are
        /root/rules/NAME.rover: a person's file replaces the image's of its
        name and an empty one masks it. A person's file is read only when it
        is regular, root's, not writable by group or others and at most 64
        KiB, as the machine script was; one that may not be read or does not
        compile is said with its line and the image's of its name stands.
        Safe mode reads the image's alone: moonwater.safe=1 on the kernel's
        command line, or two loads in a row (the runner's at its start, or
        this verb's) that did not stand a minute, counted in /root/rules/.boots.
*/

#ifndef ROVER_LOAD_INCLUDED
#define ROVER_LOAD_INCLUDED

#include "../moonwater/rules.c"

#define ROVER_PERSON_RULES "/root/rules"
#define ROVER_BOOTS_FILE "/root/rules/.boots" // loads that did not stand ROVER_STOOD_NS, a number
#define ROVER_STOOD_NS (60ull * 1000000000ull)
#define ROVER_FILES_MOST 64u
#define ROVER_FILE_NAME_MOST 64u
#define ROVER_SET_MOST (4u * ROVER_SOURCE_MOST)

static struct rover_build rover_load_one;
static struct rover_build rover_load_all;
static p8 rover_load_file[ROVER_SOURCE_MOST + 1];
static p8 rover_load_set[ROVER_SET_MOST];
static positive rover_load_used;
static bool rover_load_was_safe;    // the last compile read the image's rules alone
static bool rover_load_forced_safe; // the image's alone from here: the set with the person's did not compile
static p8 rover_load_asked;         // 0: safe mode as the command line and the count say; 1 the person's files too; 2 the image's alone

//      A person's file into rover_load_file: regular, root's, written by root alone, at most 64 KiB, not a link (as the
//      machine script was read). Its length, or -1.
static bipolar rover_load_read(string_address path)
{
        file_facts facts;
        bipolar handle, got;
        positive used = 0;

        if (!file_look(AT_FDCWD, path, AT_SYMLINK_NOFOLLOW, address_of facts) ||
            (facts.mode & MODE_FORMAT) != MODE_FILE || facts.owner != 0 || (facts.mode & 0022))
                return -1;
        handle = system_open_at(AT_FDCWD, path, FILE_READ | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW);
        if (handle < 0)
                return -1;
        for (;;)
        {
                got = system_read_retry((positive)handle, rover_load_file + used, sizeof(rover_load_file) - used);
                if (got <= 0)
                        break;
                used += (positive)got;
                if (used == sizeof(rover_load_file))
                {
                        got = -1;
                        break;
                }
        }
        system_close(handle);
        return got < 0 ? -1 : (bipolar)used;
}

static positive rover_load_boots(void)
{
        p8 boots[16];

        return host_read_word(ROVER_BOOTS_FILE, boots, sizeof(boots)) >= 0 ? string_to_positive(boots) : 0;
}

static fn rover_load_boots_put(positive count)
{
        p8 text[24];
        positive length = positive_into_string(text, count);

        text[length++] = '\n';
        (void)system_call_3(syscall(mkdirat), (positive)AT_FDCWD, (positive)ROVER_PERSON_RULES, 0700);
        (void)host_write_file(ROVER_BOOTS_FILE, text, length, 0600, false);
}

//      The image's rules alone: moonwater.safe=1 on the command line, or two loads in a row that did not stand a minute.
static bool rover_load_safe(void)
{
        bipolar handle = system_open_at(AT_FDCWD, "/proc/cmdline", FILE_READ | O_CLOEXEC);
        p8 line[4096];
        bipolar got = handle < 0 ? 0 : system_read_retry((positive)handle, line, sizeof(line) - 1);

        if (handle >= 0)
                system_close(handle);
        if (got > 0)
        {
                line[got] = 0;
                for (bipolar at = 0; at + 16 <= got; at++)
                        if (!memory_compare(line + at, "moonwater.safe=1", 16) && (at == 0 || line[at - 1] == ' '))
                                return true;
        }
        if (rover_load_asked)
                return rover_load_asked == 2;
        return rover_load_forced_safe || rover_load_boots() >= 2;
}

//      One file's text into the set, when it compiles alone: said by its name and line when it does not.
static bool rover_load_add(const p8 address_to text, positive length, string_address said, bool say)
{
        if (!rover_compile(&rover_load_one, text, length))
        {
                if (say)
                        string_format(log_error, "rover: %s:%p: %s\n", said, (positive)rover_load_one.line, rover_load_one.why);
                return false;
        }
        if (rover_load_used + length + 1 > sizeof(rover_load_set))
        {
                if (say)
                        string_format(log_error, "rover: %s is past what one table holds\n", said);
                return false;
        }
        memory_copy(rover_load_set + rover_load_used, text, length);
        rover_load_used += length;
        rover_load_set[rover_load_used++] = '\n';
        return true;
}

/*
        The rules compiled into rover_load_all: the image's (the program's own,
        rover_image_file) and the person's, /root/rules/NAME.rover, by name. A
        person's file replaces the image's of its name and an empty one masks
        it; one that may not be read or does not compile is said, and the
        image's of its name stands. Answers how many files went in, or -1 when
        the set as a whole does not compile (two files with a rule of one
        name), which loads nothing.
*/
static bipolar rover_load_compile(bool say)
{
        static p8 names[ROVER_FILES_MOST][ROVER_FILE_NAME_MOST];
        positive count = 0, files = 0;
        bipolar handle;

        rover_load_was_safe = rover_load_safe();
        rover_load_used = 0;
        if (rover_load_was_safe && say)
                string_format(log_error, "rover: safe mode: the image's rules alone\n");
        for (positive at = 0; at < ROVER_IMAGE_FILES; at++)
        {
                const char address_to name;

                (void)rover_image_file(at, &name);
                string_copy(names[count++], (string_address)name);
        }
        handle = rover_load_was_safe ? -1
                                     : system_open_at(AT_FDCWD, ROVER_PERSON_RULES, FILE_READ | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
        if (handle >= 0)
        {
                p8 buffer[4096];
                bipolar got;

                while ((got = system_call_3(syscall(getdents64), (positive)handle, (positive)buffer, sizeof(buffer))) > 0)
                        for (bipolar at = 0; at < got;)
                        {
                                string_address name = (string_address)(buffer + at + 19);
                                positive size = string_length(name);
                                bool known = false;

                                at += memory_load_unaligned(p16, buffer + at + 16);
                                if (size < 7 || size >= ROVER_FILE_NAME_MOST || name[0] == '.' ||
                                    memory_compare(name + size - 6, ".rover", 6) || count == ROVER_FILES_MOST)
                                        continue;
                                for (positive k = 0; k < count; k++)
                                        known = known || string_equals((string_address)names[k], name);
                                if (!known)
                                        string_copy(names[count++], name);
                        }
                system_close(handle);
        }
        //      One order whatever the directory's: by name.
        for (positive at = 1; at < count; at++)
                for (positive k = at; k && string_compare(names[k - 1], names[k]) > 0; k--)
                {
                        p8 swap[ROVER_FILE_NAME_MOST];

                        memory_copy(swap, names[k], sizeof(swap));
                        memory_copy(names[k], names[k - 1], sizeof(swap));
                        memory_copy(names[k - 1], swap, sizeof(swap));
                }
        for (positive at = 0; at < count; at++)
        {
                const char address_to image = null;
                p8 path[160];
                file_facts facts;

                for (positive k = 0; k < ROVER_IMAGE_FILES; k++)
                {
                        const char address_to name;
                        const char address_to text = rover_image_file(k, &name);

                        if (string_equals((string_address)names[at], (string_address)name))
                                image = text;
                }
                if (!rover_load_was_safe && string_join(path, sizeof(path), ROVER_PERSON_RULES "/", (string_address)names[at]) &&
                    file_look(AT_FDCWD, path, AT_SYMLINK_NOFOLLOW, address_of facts))
                {
                        bipolar length = rover_load_read((string_address)path);

                        if (!length)
                                continue; // an empty file masks the image's
                        if (length > 0 && rover_load_add(rover_load_file, (positive)length, (string_address)path, say))
                        {
                                files++;
                                continue;
                        }
                        if (say && length < 0)
                                string_format(log_error, "rover: %s may not be read (a person's file is regular, root's, "
                                                         "not writable by others, at most 64 KiB)\n", path);
                        if (say)
                                string_format(log_error, "rover: %s is left out%s\n", path,
                                              image ? "; the image's of its name stands" : "");
                }
                if (image && rover_load_add((const p8 address_to)image, string_length((string_address)image),
                                            (string_address)names[at], say))
                        files++;
        }
        rover_load_all.merge = true;
        bool made = rover_compile(&rover_load_all, rover_load_set, rover_load_used);
        rover_load_all.merge = false;
        if (!made)
        {
                if (say)
                        string_format(log_error, "rover: the files together do not compile: %s\n", rover_load_all.why);
                return -1;
        }
        return (bipolar)files;
}

static bipolar rover_load_ask(bipolar device, struct rover_control address_to request)
{
        return system_control(device, ROVER_IOCTL, request);
}

static bipolar rover_load_device(void)
{
        return system_open_at(AT_FDCWD, SPARK_DEVICE, FILE_READ | O_CLOEXEC);
}

//      The set compiled and handed to the kernel: the load's number, or the error.
static bipolar rover_load_now(bipolar device, bool say)
{
        struct rover_control request;
        bipolar files = rover_load_compile(say), answer;

        if (files < 0)
                return -22;
        //      Counted until it has stood a minute (the runner puts the count back to 0 then).
        rover_load_boots_put(rover_load_boots() + 1);
        memory_zero(&request, sizeof(request));
        request.op = ROVER_OP_LOAD;
        request.address = (p64)(positive)rover_built(&rover_load_all);
        request.length = (p32)rover_load_all.size;
        answer = rover_load_ask(device, &request);
        if (answer < 0)
        {
                if (say && request.why[0])
                        string_format(log_error, "rover: the kernel refused the table: %s (rule %p)\n", request.why,
                                      (positive)request.rule);
                else if (say)
                        host_fail("rover load", answer);
                return answer;
        }
        if (say)
                string_format(log, "rover: table %p loaded from %p files: %p rules, %p of them the kernel's\n",
                              (positive)request.table, (positive)files, (positive)request.rules,
                              (positive)request.kernel_rules);
        return (bipolar)request.table;
}

static b32 rover_verb_status(bipolar device)
{
        struct rover_control request;
        bipolar answer;

        memory_zero(&request, sizeof(request));
        request.op = ROVER_OP_STATUS;
        answer = rover_load_ask(device, &request);
        if (answer < 0)
                return host_fail("rover status", answer);
        string_format(log,
                      "table %p: %p rules, %p of them the kernel's\n"
                      "events %p, hits %p, taken %p, floor %p, queued %p, lost %p, ring records %p\n",
                      (positive)request.table, (positive)request.rules, (positive)request.kernel_rules,
                      (positive)request.events, (positive)request.hits, (positive)request.taken,
                      (positive)request.floor, (positive)request.queued, (positive)request.lost,
                      (positive)request.posted);
        if (request.decide_count)
                string_format(log, "event to decision: %p ns on average, %p ns at worst, over %p hits\n",
                              (positive)(request.decide_ns / request.decide_count), (positive)request.decide_worst,
                              (positive)request.decide_count);
        if (request.hook_count)
                string_format(log, "hook to decision: %p ns on average, %p ns at worst\n",
                              (positive)(request.hook_ns / request.hook_count), (positive)request.hook_worst);
        return 0;
}

static const p8 address_to rover_load_map(bipolar device, positive length)
{
        bipolar mapped = system_call_6(syscall(mmap), 0, length, FILE_PROTECT_READ, FILE_MAP_SHARED, (positive)device, 0);

        return mapped < 0 && mapped > -4096 ? null : (const p8 address_to)mapped;
}

static b32 rover_verb_state(bipolar device)
{
        const p8 address_to page = rover_load_map(device, ROVER_PAGE_BYTES);
        p8 line[160];

        if (!page)
                return host_refuse("rover: the kernel's state page cannot be mapped (an older kernel?)\n");
        for (positive at = 0; at < ROVER_STATES; at++)
        {
                const struct rover_state_row address_to state = rover_states + at;
                struct rover_text text = {line, 0, sizeof(line) - 1, false};
                b32 value;

                if (state->page != ROVER_PAGE_KERNEL)
                        continue;
                rover_puts(&text, state->name);
                rover_puts(&text, " ");
                if (rover_kind_is_word(rover_types[state->type].kind))
                        rover_literal_put(&text, state->type, 0, page + state->offset + 1, page[state->offset]);
                else
                {
                        memory_copy(&value, page + state->offset, 4);
                        rover_literal_put(&text, state->type, value, null, 0);
                }
                line[text.used] = 0;
                string_format(log, "%s\n", line);
        }
        return 0;
}

//      A record copied out whole, or false when the kernel wrote over it while it was read.
static bool rover_record_take(const p8 address_to map, p64 at, struct rover_record address_to into)
{
        const volatile struct rover_record address_to record =
            (const volatile struct rover_record address_to)(map + ROVER_MAP_RECORDS) + (at % ROVER_RING_SLOTS);

        if (record->sequence != at + 1)
                return false;
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
        memory_copy(into, (const void address_to)record, sizeof(*into));
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
        return record->sequence == at + 1;
}

static fn rover_record_event(const struct rover_record address_to record, struct rover_event address_to event)
{
        memory_zero(event, sizeof(*event));
        event->id = record->event;
        event->count = record->fields;
        event->when = record->when;
        event->realtime = (b64)(system_clock_ns(0) / 1000000000);
        for (positive at = 0; at < ROVER_FIELDS && at < record->fields; at++)
                event->field[at].number = record->number[at];
        if (record->word_field < ROVER_FIELDS)
        {
                event->field[record->word_field].text = record->word;
                event->field[record->word_field].length = record->word_length;
        }
}

static b32 rover_verb_watch(bipolar device, positive most)
{
        const p8 address_to map = rover_load_map(device, ROVER_MAP_BYTES);
        const volatile struct rover_ring address_to ring;
        p64 tail, seen = 0, wake_total = 0, decide_total = 0, wake_worst = 0;

        if (!map)
                return host_refuse("rover: the ring cannot be mapped: it is root's (or an older kernel)\n");
        ring = (const volatile struct rover_ring address_to)(map + ROVER_MAP_RING);
        tail = ring->head;
        while (!most || seen < most)
        {
                p64 head;
                bipolar got = system_read_retry((positive)device, (address_any)&head, sizeof(head));
                p64 woken = system_clock_ns(1);

                if (got != sizeof(head))
                        return host_fail("rover watch", got < 0 ? got : -5);
                if (head - tail > ROVER_RING_SLOTS)
                {
                        string_format(log, "rover: %p records were overtaken before they were read\n",
                                      (positive)(head - tail - ROVER_RING_SLOTS));
                        tail = head - ROVER_RING_SLOTS;
                }
                for (; tail < head && (!most || seen < most); tail++)
                {
                        struct rover_record record;
                        const struct rover_event_row address_to row;

                        if (!rover_record_take(map, tail, &record))
                                continue;
                        row = rover_event_find(record.event);
                        string_format(log, "%s event=\"%s\" rule=%p table=%p decided=%pns woken=%pns%s%s\n",
                                      record.kind == ROVER_RECORD_HIT ? "hit" : "event",
                                      row ? row->name : "edge", (positive)record.rule, (positive)record.table,
                                      (positive)(record.decided - record.when), (positive)(woken - record.when),
                                      record.flags & ROVER_RECORD_TAKEN ? " taken" : "",
                                      record.flags & ROVER_RECORD_USER ? " user" : "");
                        wake_total += woken - record.when;
                        decide_total += record.decided - record.when;
                        if (woken - record.when > wake_worst)
                                wake_worst = woken - record.when;
                        seen++;
                }
                log_flush();
        }
        if (seen)
                string_format(log, "rover: %p records: event to decision %p ns, event to this process awake %p ns on "
                                   "average, %p ns at worst\n",
                              (positive)seen, (positive)(decide_total / seen), (positive)(wake_total / seen),
                              (positive)wake_worst);
        return 0;
}

static b32 rover_kernel_verb(string_address address_to arguments, positive count)
{
        string_address what = count > 2 ? arguments[2] : null;
        bipolar device;
        b32 answer;

        if (!what || (!string_equals(what, "load") && !string_equals(what, "unload") && !string_equals(what, "status") &&
                      !string_equals(what, "state") && !string_equals(what, "watch")))
                return rover_verb(arguments, count);
        device = rover_load_device();
        if (device < 0)
                return host_fail(SPARK_DEVICE, device);
        if (string_equals(what, "load"))
                answer = rover_load_now(device, true) < 0;
        else if (string_equals(what, "unload"))
        {
                struct rover_control request;
                bipolar failed;

                memory_zero(&request, sizeof(request));
                request.op = ROVER_OP_UNLOAD;
                failed = rover_load_ask(device, &request);
                answer = failed < 0 ? host_fail("rover unload", failed) : 0;
        }
        else if (string_equals(what, "status"))
                answer = rover_verb_status(device);
        else if (string_equals(what, "state"))
                answer = rover_verb_state(device);
        else
        {
                positive most = 0;

                if (count > 3)
                        most = string_to_positive(arguments[3]);
                answer = rover_verb_watch(device, most);
        }
        log_flush();
        system_close(device);
        return answer;
}

#endif /* ROVER_LOAD_INCLUDED */
