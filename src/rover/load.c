/*
        Rover and the kernel, from userspace: the verbs that load the rule
        files into the module, ask it what it has done, print its state page
        and watch its ring, and the machine process's part as the ring's
        consumer until the runner is. src/moonwater/rules.c is the kernel's
        half and the ABI; src/rover/README.md is the language.

          moonwater rover load       the image's rules and the person's, compiled into one table and loaded
          moonwater rover unload     the kernel's table cleared
          moonwater rover status     what the kernel's rules have done, and how fast
          moonwater rover state      the kernel's state page, one state a line
          moonwater rover watch [N]  the ring's records as they come, N of them, with the time to each

        The files are /lib/moonwater/rules/NAME.rover, the image's, and
        /root/rules/NAME.rover, the person's: a person's file replaces the
        image's of its name and an empty one masks it. A person's file is read
        only when it is regular, root's, not writable by group or others and
        at most 64 KiB, as the machine script is. moonwater.safe=1 on the
        kernel's command line reads the image's alone. A file that does not
        compile is said with its line and left out; the rest still load.
*/

#ifndef ROVER_LOAD_INCLUDED
#define ROVER_LOAD_INCLUDED

#include "../moonwater/rules.c"

#define ROVER_IMAGE_RULES "/lib/moonwater/rules"
#define ROVER_PERSON_RULES "/root/rules"
#define ROVER_FILES_MOST 64u
#define ROVER_FILE_NAME_MOST 64u
#define ROVER_SET_MOST (4u * ROVER_SOURCE_MOST)

positive shell_function_slot(string_address name);
b32 shell_call_slot(positive slot, string_address name, string_address address_to arguments, positive count);

static struct rover_build rover_load_one;
static struct rover_build rover_load_all;
static p8 rover_load_file[ROVER_SOURCE_MOST + 1];
static p8 rover_load_set[ROVER_SET_MOST];

struct rover_found
{
        p8 name[ROVER_FILES_MOST][ROVER_FILE_NAME_MOST];
        p8 person[ROVER_FILES_MOST]; // 1: /root/rules has it
        positive count;
};

static bool rover_load_named(string_address directory, string_address name, address_any context)
{
        struct rover_found address_to found = context;
        positive length = string_length(name);
        bool person = string_equals(directory, ROVER_PERSON_RULES);

        if (length < 7 || length >= ROVER_FILE_NAME_MOST || !string_equals(name + length - 6, ".rover"))
                return true;
        for (positive at = 0; at < found->count; at++)
                if (string_equals((string_address)found->name[at], name))
                {
                        found->person[at] |= person;
                        return true;
                }
        if (found->count == ROVER_FILES_MOST)
                return false;
        string_copy(found->name[found->count], name);
        found->person[found->count++] = person;
        return true;
}

//      A file's bytes into rover_load_file: its length, or -1 when it may not be read (a person's must be root's).
static bipolar rover_load_read(string_address path, bool person)
{
        file_facts facts;
        bipolar handle, got;
        positive used = 0;

        if (!file_look(AT_FDCWD, path, AT_SYMLINK_NOFOLLOW, address_of facts) ||
            (facts.mode & MODE_FORMAT) != MODE_FILE ||
            (person && (facts.owner != 0 || (facts.mode & 0022))))
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

static bool rover_load_safe(void)
{
        bipolar handle = system_open_at(AT_FDCWD, "/proc/cmdline", FILE_READ | O_CLOEXEC);
        p8 line[4096];
        bipolar got;

        if (handle < 0)
                return false;
        got = system_read_retry((positive)handle, line, sizeof(line) - 1);
        system_close(handle);
        if (got <= 0)
                return false;
        line[got] = 0;
        for (bipolar at = 0; at + 16 <= got; at++)
                if (!memory_compare(line + at, "moonwater.safe=1", 16) && (at == 0 || line[at - 1] == ' '))
                        return true;
        return false;
}

static int rover_name_order(const p8 address_to a, const p8 address_to b)
{
        while (*a && *a == *b)
                a++, b++;
        return (int)*a - (int)*b;
}

/*
        The rule files compiled into rover_load_all: answers how many files
        went in, or -1 when the set as a whole did not compile (two files
        with a rule of one name), which loads nothing.
*/
static bipolar rover_load_compile(bool say)
{
        static struct rover_found found;
        bool safe = rover_load_safe();
        positive used = 0, files = 0;

        memory_zero(&found, sizeof(found));
        host_each_entry(ROVER_IMAGE_RULES, rover_load_named, &found);
        if (!safe)
                host_each_entry(ROVER_PERSON_RULES, rover_load_named, &found);
        //      One order whatever the directories' order: by name.
        for (positive at = 1; at < found.count; at++)
                for (positive k = at; k && rover_name_order(found.name[k - 1], found.name[k]) > 0; k--)
                {
                        p8 name[ROVER_FILE_NAME_MOST], person = found.person[k];

                        string_copy(name, (string_address)found.name[k]);
                        string_copy(found.name[k], (string_address)found.name[k - 1]);
                        string_copy(found.name[k - 1], (string_address)name);
                        found.person[k] = found.person[k - 1];
                        found.person[k - 1] = person;
                }
        for (positive at = 0; at < found.count; at++)
        {
                p8 path[160];
                bipolar length;

                (void)string_join(path, sizeof(path), found.person[at] ? ROVER_PERSON_RULES "/" : ROVER_IMAGE_RULES "/",
                                  (string_address)found.name[at]);
                length = rover_load_read((string_address)path, found.person[at]);
                if (length < 0)
                {
                        if (say)
                                string_format(log_error, "rover: %s may not be read (a person's file is root's, "
                                                         "not writable by others, at most 64 KiB); left out\n", path);
                        continue;
                }
                if (!length)
                        continue; // an empty file masks the image's
                if (!rover_compile(&rover_load_one, rover_load_file, (positive)length))
                {
                        if (say)
                                string_format(log_error, "rover: %s:%p: %s; left out\n", path,
                                              (positive)rover_load_one.line, rover_load_one.why);
                        continue;
                }
                if (used + (positive)length + 1 > sizeof(rover_load_set))
                {
                        if (say)
                                string_format(log_error, "rover: %s is past what one table holds; left out\n", path);
                        continue;
                }
                memory_copy(rover_load_set + used, rover_load_file, (positive)length);
                used += (positive)length;
                if (rover_load_set[used - 1] != '\n')
                        rover_load_set[used++] = '\n';
                files++;
        }
        if (!rover_compile(&rover_load_all, rover_load_set, used))
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

// ---- the machine process, the ring's consumer until the runner is ----------

static const p8 address_to rover_machine_map;

static fn rover_load_pool_text(p8 address_to into, positive room, const struct rover_table address_to view, p32 at)
{
        p16 length;

        memory_copy(&length, view->pool + at, 2);
        if (length >= room)
                length = (p16)(room - 1);
        memory_copy(into, view->pool + at + 2, length);
        into[length] = 0;
}
static p64 rover_machine_tail;
static p32 rover_machine_table;
static struct rover_run rover_machine_runs[ROVER_RULES_MOST];
static struct rover_table rover_machine_view;

/*
        At the machine's start: the rules loaded, the table kept to read the
        ring's records by, and the ring mapped. A machine with no rule files
        loads an empty table, which clears one an earlier start left.
*/
static fn rover_machine_start(bipolar device)
{
        struct rover_refusal refusal;
        bipolar table = rover_load_now(device, true);

        rover_machine_table = 0;
        if (!rover_machine_map)
                rover_machine_map = rover_load_map(device, ROVER_MAP_BYTES);
        if (rover_machine_map)
                rover_machine_tail = ((const volatile struct rover_ring address_to)(rover_machine_map + ROVER_MAP_RING))->head;
        if (table <= 0 || !rover_check(rover_built(&rover_load_all), rover_load_all.size, &rover_machine_view, &refusal))
                return;
        rover_machine_table = (p32)table;
        memory_zero(rover_machine_runs, sizeof(rover_machine_runs));
}

//      One action of a hit, done by its owner as it is today: the machine script's moonwater_rover when it has one.
static fn rover_machine_act(const struct rover_event address_to event, positive which, p16 rule)
{
        const struct rover_action address_to action = rover_machine_view.action + which;
        const p8 address_to pages[ROVER_PAGES] = {rover_machine_map, null};
        struct rover_value values[ROVER_ARGUMENTS];
        p8 words[ROVER_ARGUMENTS + 2][ROVER_TEXT_MOST + 2];
        string_address arguments[ROVER_ARGUMENTS + 2];
        positive count = rover_arguments(&rover_machine_view, which, event, pages, values), slot;
        static const char address_to const kinds[] = {"", "do", "run", "tool", "emit"};

        rover_load_pool_text(words[0], sizeof(words[0]), &rover_machine_view, rover_machine_view.rule[rule].name);
        string_copy(words[1], kinds[action->kind <= ROVER_EMIT ? action->kind : 0]);
        if (action->kind == ROVER_DO)
        {
                const struct rover_primitive_row address_to primitive = rover_primitive_find(action->id);

                string_copy(words[1], primitive ? primitive->name : "do");
                if (primitive && primitive->runs == ROVER_KERNEL)
                {
                        //      A runner's rule asking the kernel: through the queue a kernel rule's goes through.
                        struct rover_control request;
                        bipolar device = rover_load_device();

                        memory_zero(&request, sizeof(request));
                        request.op = ROVER_OP_DO;
                        request.primitive = action->id;
                        request.number = count ? values[0].number : 0;
                        if (count && values[0].text)
                                memory_copy(request.text, values[0].text,
                                            values[0].length < ROVER_TEXT_ROOM - 1 ? values[0].length : ROVER_TEXT_ROOM - 1);
                        if (device >= 0)
                        {
                                (void)rover_load_ask(device, &request);
                                system_close(device);
                        }
                        return;
                }
        }
        for (positive at = 0; at < count; at++)
        {
                struct rover_text text = {words[2 + at], 0, ROVER_TEXT_MOST, false};

                if (values[at].text)
                        rover_put(&text, values[at].text, values[at].length);
                else if (action->kind == ROVER_DO && rover_primitive_find(action->id))
                        rover_literal_put(&text, rover_primitive_find(action->id)->type[at], values[at].number, null, 0);
                words[2 + at][text.used] = 0;
        }
        for (positive at = 0; at < count + 2; at++)
                arguments[at] = (string_address)words[at];
        slot = shell_function_slot("moonwater_rover");
        if (slot == positive_max)
        {
                string_format(log, "rover: %s: %s %s has no owner yet: define moonwater_rover in the machine script\n",
                              words[0], words[1], count ? words[2] : (p8 address_to)"");
                log_flush();
                return;
        }
        (void)shell_call_slot(slot, "moonwater_rover", arguments, count + 2);
}

static fn rover_machine_hit(const struct rover_event address_to event, const struct rover_hit address_to hit);

//      emit cue NAME: the runner's rules of the cue, here, cut at ROVER_EMIT_DEPTH cues deep.
static fn rover_machine_cue(const struct rover_event address_to from, positive which)
{
        const p8 address_to pages[ROVER_PAGES] = {rover_machine_map, null};
        struct rover_value values[ROVER_ARGUMENTS];
        struct rover_event cue;
        struct rover_hit hits[16];
        positive made;

        if (from->depth + 1 >= ROVER_EMIT_DEPTH ||
            !rover_arguments(&rover_machine_view, which, from, pages, values) || !values[0].text)
                return;
        memory_zero(&cue, sizeof(cue));
        cue.id = ROVER_CUE;
        cue.count = 1;
        cue.depth = (p8)(from->depth + 1);
        cue.when = system_clock_ns(1);
        cue.realtime = from->realtime;
        cue.field[0] = values[0];
        made = rover_fire(&rover_machine_view, rover_machine_runs, pages, &cue, ROVER_RUNNER, hits, 16);
        for (positive at = 0; at < made; at++)
                rover_machine_hit(&cue, &hits[at]);
}

static fn rover_machine_hit(const struct rover_event address_to event, const struct rover_hit address_to hit)
{
        for (positive k = 0; k < hit->count; k++)
        {
                positive which = (positive)hit->action + k;
                const struct rover_action address_to action = rover_machine_view.action + which;

                if (action->kind == ROVER_DO && action->runs == ROVER_KERNEL &&
                    rover_machine_view.rule[hit->rule].place == ROVER_KERNEL)
                        continue; // the kernel did it
                if (action->kind == ROVER_EMIT)
                        rover_machine_cue(event, which);
                else
                        rover_machine_act(event, which, hit->rule);
        }
}

//      Every record since the last: a kernel hit's userspace actions done, and the runner's rules of an event.
static fn rover_machine_drain(void)
{
        const volatile struct rover_ring address_to ring;
        p64 head;

        if (!rover_machine_map)
                return;
        ring = (const volatile struct rover_ring address_to)(rover_machine_map + ROVER_MAP_RING);
        //      A table loaded since (moonwater rover load, by hand): the same files compiled again here, to read it by.
        if (ring->table != rover_machine_table && ring->table)
        {
                struct rover_refusal refusal;

                if (rover_load_compile(false) >= 0 &&
                    rover_check(rover_built(&rover_load_all), rover_load_all.size, &rover_machine_view, &refusal))
                {
                        rover_machine_table = ring->table;
                        memory_zero(rover_machine_runs, sizeof(rover_machine_runs));
                }
        }
        if (!rover_machine_table)
                return;
        head = ring->head;
        if (head - rover_machine_tail > ROVER_RING_SLOTS)
                rover_machine_tail = head - ROVER_RING_SLOTS;
        for (; rover_machine_tail < head; rover_machine_tail++)
        {
                struct rover_record record;
                struct rover_event event;

                if (!rover_record_take(rover_machine_map, rover_machine_tail, &record) || record.table != rover_machine_table)
                        continue;
                rover_record_event(&record, &event);
                if (record.kind == ROVER_RECORD_HIT && (record.flags & ROVER_RECORD_USER) &&
                    record.rule < rover_machine_view.head->rules)
                {
                        struct rover_hit hit = {record.rule, record.action, record.count,
                                                (p8)!!(record.flags & ROVER_RECORD_ELSE), 0};

                        rover_machine_hit(&event, &hit);
                }
                else if (record.kind == ROVER_RECORD_EVENT)
                {
                        const p8 address_to pages[ROVER_PAGES] = {rover_machine_map, null};
                        struct rover_hit hits[16];
                        positive made = rover_fire(&rover_machine_view, rover_machine_runs, pages, &event, ROVER_RUNNER,
                                                   hits, 16);

                        for (positive at = 0; at < made; at++)
                                rover_machine_hit(&event, &hits[at]);
                }
        }
}

#endif /* ROVER_LOAD_INCLUDED */
