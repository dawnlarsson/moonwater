/*
        Rover in the kernel: the rules placed here, run at the event.

        The module's half of src/rover. A table compiled in userspace is
        handed in through ROVER_IOCTL (op LOAD), checked whole by rover_check
        before anything reads it, and swapped in under the rules' lock; a
        refused table leaves the one before it. The kernel's state page is
        written here as the events arrive and mapped read-only by whoever
        wants to read it; the ring beside it carries every rule the kernel
        fired, and every event a runner's rule waits for, to userspace.

        Each event is heard at the earliest point the kernel offers it:

          - input, in the input core's filter (bind_filter in moonwater.c),
            which runs under the device's event lock with interrupts off,
            ahead of evdev and the console's keyboard, so before any reader.
            A rule that says take drops the event there: no reader sees it.
          - power, at the power supply notifier, as a supply says it changed.
          - USB, at the USB notifier, once a device is configured.
          - links, at the netdevice notifier, as a carrier comes and goes.
          - displays, at Canvas's DRM client hotplug.
          - sleep and wake, at the pm notifier.

        What is not heard, and why: rfkill has no in-kernel listener (its
        changes go to /dev/rfkill only; the rfkill keys and SW_RFKILL_ALL are
        heard as keys and switches), and a thermal trip reaches only netlink
        and a tracepoint the image does not build.

        The evaluator is rover.c's, built with KERNEL_MODE: bounded,
        allocation-free, no text read. A rule's kernel primitives are queued
        from the hook (irq_work, then a workqueue) and never run in it.

        The ABI half above STANDARD_MODERN_C_KERNEL is what userspace reads:
        the request, the ring's head and its records.
*/

#ifndef MOONWATER_RULES_INCLUDED
#define MOONWATER_RULES_INCLUDED

#include "../rover/rover.c"

#define ROVER_OP_LOAD 0u
#define ROVER_OP_UNLOAD 1u
#define ROVER_OP_STATUS 2u
#define ROVER_OP_DO 3u
#define ROVER_OP_COUNT 4u

#define ROVER_TEXT_ROOM 256u
#define ROVER_WHY_ROOM 96u

/*
        One request, every op. LOAD hands a table (address, length) and is
        answered with the reason and the rule when it is refused; STATUS
        answers the counts; DO asks a kernel primitive (primitive, number,
        text), as a userspace rule's kernel primitive is asked. LOAD, UNLOAD
        and DO are CAP_SYS_ADMIN's.
*/
struct rover_control
{
        p32 op;
        p32 flags; // none; nonzero is refused
        p64 address;
        p32 length;
        p32 rule; // refused: the rule it is about, or ROVER_NO_RULE
        p32 table; // the load: how many tables were loaded since boot, 0 for none live
        p32 rules;
        p32 kernel_rules;
        p16 primitive; // DO
        p16 pad;
        b64 number;     // DO
        p64 events;     // events evaluated against a table
        p64 hits;       // rules the kernel fired
        p64 taken;      // events a take dropped before any reader
        p64 floor;      // stop-key presses a take let through, inside the floor's ten seconds
        p64 queued;     // kernel primitives queued
        p64 lost;       // kernel primitives the queue had no room for
        p64 posted;     // ring records written
        p64 decide_ns;  // from the event's own time to the rule's decision, added
        p64 decide_worst;
        p64 decide_count;
        p64 hook_ns;    // from the hook's start to the decision, added
        p64 hook_worst;
        p64 hook_count;
        p64 reserved;
        char why[ROVER_WHY_ROOM];
        char text[ROVER_TEXT_ROOM]; // DO
};

_Static_assert(sizeof(struct rover_control) == 512, "rover control ABI");

// _IOWR('s', 19, struct rover_control)
#define ROVER_IOCTL 0xc2007313u

/*
        The map: /dev/spark mapped from offset 0. The first 4 KiB are the
        kernel's state page, anyone's to read; the ring's head and records
        after it are CAP_SYS_ADMIN's (a key rule's ring is a key log), as is
        any map of more than the state page.
*/
#define ROVER_MAP_STATE 0u
#define ROVER_MAP_RING 4096u
#define ROVER_MAP_RECORDS 8192u
#define ROVER_RING_SLOTS 256u
#define ROVER_RECORD_BYTES 128u
#define ROVER_MAP_BYTES (ROVER_MAP_RECORDS + ROVER_RING_SLOTS * ROVER_RECORD_BYTES)

#define ROVER_RING_MAGIC 0x474e5252u // "RRNG"

struct rover_ring
{
        p32 magic;
        p32 slots;
        p32 record;
        p32 table; // the load the newest record is of
        p64 head;  // records written since boot; slot head % slots is the next
        p64 user;  // of them, the ones a userspace owner acts on: an event for the runner, a hit with a user action
};

#define ROVER_RECORD_EVENT 1u // an event a runner's rule waits for
#define ROVER_RECORD_HIT 2u   // a rule the kernel fired

#define ROVER_RECORD_TAKEN 1u // the event was dropped before any reader
#define ROVER_RECORD_ELSE 2u  // the hit is its else actions
#define ROVER_RECORD_USER 4u  // the hit has actions that are not the kernel's
#define ROVER_RECORD_NONE 0xffu

/*
        A record. sequence is its place in the ring plus one, written last,
        so a reader that finds another number in it was overtaken. when is
        the event's own time (an input device's timestamp where its driver
        gives one), decided when the kernel had the rule's answer. numbers
        are the event's fields by place; the one word field is word.
*/
struct rover_record
{
        p64 sequence;
        p64 when;
        p64 decided;
        p32 table;
        p16 event;
        p8 kind;
        p8 flags;
        p16 rule;
        p16 action;
        p8 count;
        p8 fields;
        p8 word_field;
        p8 word_length;
        b64 number[ROVER_FIELDS];
        p8 word[56];
};

_Static_assert(sizeof(struct rover_record) == ROVER_RECORD_BYTES, "rover record ABI");
#define ROVER_RECORD_WORD_MOST 55u

#ifdef STANDARD_MODERN_C_KERNEL

IOCTL_IS(ROVER_IOCTL, IOCTL_BOTH, 19, sizeof(struct rover_control));

static DEFINE_RAW_SPINLOCK(rules_lock);

//      A table as it is live: its bytes, the view rover_check found, its run records, and which events have a
//      rule in the runner (so the ring is told) or in the kernel (so the hook looks).
struct rules_live
{
        struct rover_table view;
        struct rover_run *runs;
        p8 *bytes;
        p32 size;
        p32 table;
        p32 kernel_rules;
        p64 runner[ROVER_EVENTS / 64];
        p64 kernel[ROVER_EVENTS / 64];
        struct rcu_head rcu;
};

static struct rules_live __rcu *rules_now;
static DEFINE_MUTEX(rules_load_lock);
static p32 rules_loads;

static p8 *rules_map;          // ROVER_MAP_BYTES, vmalloc_user
static struct rover_ring *rules_ring;
static p8 *rules_page;

static struct {
        p64 events, hits, taken, floor, queued, lost, posted;
        p64 decide_ns, decide_worst, decide_count;
        p64 hook_ns, hook_worst, hook_count;
} rules_count;

static wait_queue_head_t rules_wait;

static inline bool rules_bit(const p64 *set, p16 event)
{
        return event < ROVER_EVENTS && (set[event >> 6] >> (event & 63)) & 1;
}

// ---- the state page ---------------------------------------------------------

/*
        The kernel's states this file writes, by its own number, each found in
        the schema by name at start: a state the schema does not have (yet)
        is written nowhere.
*/
enum {
        RS_LID, RS_SOURCE, RS_PERCENT, RS_BATTERY, RS_HEADPHONE, RS_DOCK, RS_TABLET, RS_CANVAS,
        RS_DISPLAYS, RS_EXTERNAL, RS_USB, RS_LINKS, RS_COUNT
};

static const char *const rules_state_name[RS_COUNT] = {
        "lid", "power.source", "battery.percent", "battery.state", "headphone", "dock", "tablet", "canvas",
        "display.count", "display.external", "usb.count", "net.link",
};

static p16 rules_offset[RS_COUNT];
static p8 rules_state_bit[RS_COUNT];
#define RULES_NOWHERE 0xffffu

static void rules_states_find(void)
{
        for (positive mine = 0; mine < RS_COUNT; mine++)
        {
                rules_offset[mine] = RULES_NOWHERE;
                for (positive at = 0; at < ROVER_STATES; at++)
                        if (rover_states[at].page == ROVER_PAGE_KERNEL &&
                            string_equals((string_address)rover_states[at].name, (string_address)rules_state_name[mine]))
                        {
                                rules_offset[mine] = rover_states[at].offset;
                                rules_state_bit[mine] = (p8)at;
                        }
        }
}

static inline b32 rules_state(positive which)
{
        return rules_offset[which] == RULES_NOWHERE ? 0 : READ_ONCE(*(b32 *)(rules_page + rules_offset[which]));
}

/*
        A state written, with the rules' lock held: the page's sequence odd
        while it is, so a reader that maps it sees a write whole or tries
        again. Answers the state's bit in the reads mask when it changed.
*/
static p64 rules_set_locked(positive which, b32 value)
{
        b32 *slot;
        p32 *sequence = (p32 *)(rules_page + 4);

        if (rules_offset[which] == RULES_NOWHERE)
                return 0;
        slot = (b32 *)(rules_page + rules_offset[which]);
        if (READ_ONCE(*slot) == value)
                return 0;
        WRITE_ONCE(*sequence, *sequence + 1);
        smp_wmb();
        WRITE_ONCE(*slot, value);
        WRITE_ONCE(*(b64 *)(rules_page + ROVER_PAGE_ZONE), -(b64)sys_tz.tz_minuteswest * 60);
        smp_wmb();
        WRITE_ONCE(*sequence, *sequence + 1);
        return 1ull << rules_state_bit[which];
}

// ---- the ring ----------------------------------------------------------------

static void rules_post_locked(const struct rules_live *live, const struct rover_event *event, p8 kind, p8 flags,
                              const struct rover_hit *hit, p64 decided)
{
        p64 head = rules_ring->head;
        struct rover_record *record =
                (struct rover_record *)(rules_map + ROVER_MAP_RECORDS) + (head % ROVER_RING_SLOTS);

        WRITE_ONCE(record->sequence, 0);
        smp_wmb();
        record->when = event->when;
        record->decided = decided;
        record->table = live ? live->table : 0;
        record->event = event->id;
        record->kind = kind;
        record->flags = flags;
        record->rule = hit ? hit->rule : 0xffff;
        record->action = hit ? hit->action : 0;
        record->count = hit ? hit->count : 0;
        record->fields = event->count;
        record->word_field = ROVER_RECORD_NONE;
        record->word_length = 0;
        //      The whole word, not the bytes this one covers: the slot's last record must not show through after its NUL.
        memory_zero(record->word, sizeof(record->word));
        for (positive at = 0; at < ROVER_FIELDS; at++)
        {
                record->number[at] = at < event->count && !event->field[at].text ? event->field[at].number : 0;
                if (at < event->count && event->field[at].text && record->word_field == ROVER_RECORD_NONE)
                {
                        p32 length = event->field[at].length;

                        if (length > ROVER_RECORD_WORD_MOST)
                                length = ROVER_RECORD_WORD_MOST;
                        record->word_field = (p8)at;
                        record->word_length = (p8)length;
                        memory_copy(record->word, event->field[at].text, length);
                        record->word[length] = 0;
                }
        }
        smp_wmb();
        WRITE_ONCE(record->sequence, head + 1);
        smp_wmb();
        WRITE_ONCE(rules_ring->table, record->table);
        if (kind == ROVER_RECORD_EVENT || (flags & ROVER_RECORD_USER))
                WRITE_ONCE(rules_ring->user, rules_ring->user + 1);
        WRITE_ONCE(rules_ring->head, head + 1);
        rules_count.posted++;
}

static p64 rules_head(void)
{
        return rules_ring ? READ_ONCE(rules_ring->head) : 0;
}

u64 moonwater_rules_pending(void)
{
        return rules_ring ? READ_ONCE(rules_ring->user) : 0;
}

// ---- kernel primitives, queued -----------------------------------------------

#define RULES_JOBS (4u * ROVER_KERNEL_ACTIONS) // four events' worth; rover_kernel_fits holds one event to sixteen

struct rules_job
{
        p16 primitive;
        p16 rule;
        b64 number;
        char name[ROVER_NAME_MOST + 1];
        char text[ROVER_TEXT_MOST + 1];
};

static struct rules_job rules_jobs[RULES_JOBS];
static p32 rules_job_head, rules_job_tail;
static struct irq_work rules_irq;
static struct work_struct rules_work;
static bool rules_started;

//      The schema's primitives by name, found at start; 0 is none.
static p16 rules_p_poweroff, rules_p_reboot, rules_p_suspend, rules_p_backlight, rules_p_latency, rules_p_unlatency,
        rules_p_radio_off, rules_p_usb_lock, rules_p_log;

static p16 rules_primitive_id(const char *name)
{
        for (positive at = 0; at < ROVER_PRIMITIVES; at++)
                if (string_equals((string_address)rover_primitives[at].name, (string_address)name))
                        return rover_primitives[at].id;
        return 0;
}

static void rules_pool_copy(char *into, positive room, const struct rover_table *view, p32 at)
{
        p16 length;

        memory_copy(&length, view->pool + at, 2);
        if (length >= room)
                length = (p16)(room - 1);
        memory_copy(into, view->pool + at + 2, length);
        into[length] = 0;
}

//      A kernel action of a hit, its arguments resolved now, while the event and the table are in hand.
/*
        Poweroff and reboot are never a slot of the queue: a request is one
        word (the primitive, the rule's name beside it), done by a work item
        of their own, so a job wedged in the queue's worker (a child that
        never returns) does not hold the machine's stop behind it, and a full
        queue does not lose it. The last request asked wins.
*/
static p16 rules_stop_asked;
static char rules_stop_name[ROVER_NAME_MOST + 1];
static struct work_struct rules_stop_work;
static struct delayed_work rules_stop_watchdog;

static void rules_stop_locked(p16 primitive, const char *name)
{
        WRITE_ONCE(rules_stop_asked, primitive);
        strscpy(rules_stop_name, name, sizeof(rules_stop_name));
}

static void rules_queue_locked(const struct rules_live *live, const struct rover_event *event, positive which,
                               p16 rule)
{
        const struct rover_action *action = live->view.action + which;
        struct rover_value values[ROVER_ARGUMENTS];
        const p8 *pages[ROVER_PAGES] = {rules_page, null};
        struct rules_job *job;
        positive count;

        if (action->id == rules_p_poweroff || action->id == rules_p_reboot)
        {
                char name[ROVER_NAME_MOST + 1];

                rules_pool_copy(name, sizeof(name), &live->view, live->view.rule[rule].name);
                rules_stop_locked(action->id, name);
                rules_count.queued++;
                return;
        }
        if (rules_job_head - rules_job_tail >= RULES_JOBS)
        {
                rules_count.lost++;
                return;
        }
        job = rules_jobs + (rules_job_head % RULES_JOBS);
        memory_zero(job, sizeof(*job));
        job->primitive = action->id;
        job->rule = rule;
        rules_pool_copy(job->name, sizeof(job->name), &live->view, live->view.rule[rule].name);
        count = rover_arguments(&live->view, which, event, pages, values);
        if (count)
        {
                job->number = values[0].number;
                if (values[0].text)
                {
                        p32 length = values[0].length > ROVER_TEXT_MOST ? ROVER_TEXT_MOST : values[0].length;

                        memory_copy(job->text, values[0].text, length);
                        job->text[length] = 0;
                }
        }
        rules_job_head++;
        rules_count.queued++;
}

static int rules_spawn(const char *command, const char *rule)
{
        struct bind_spawn spawn;
        pid_t pid;
        int ret = 0, stat = 0;

        strscpy(spawn.command, command, sizeof(spawn.command));
        snprintf(spawn.event, sizeof(spawn.event), "rover %s", rule);
        kernel_sigaction(SIGCHLD, SIG_DFL);
        pid = user_mode_thread(bind_spawn_enter, &spawn, SIGCHLD);
        if (pid > 0)
        {
                ret = kernel_wait(pid, &stat);
                if (ret > 0)
                        ret = stat;
        }
        else
                ret = pid ? pid : -EAGAIN;
        kernel_sigaction(SIGCHLD, SIG_IGN);
        if (ret)
                pr_warn("[rover] %s: %s answered %d\n", rule, command, ret);
        return ret;
}

static struct pm_qos_request rules_qos;

#if IS_REACHABLE(CONFIG_BACKLIGHT_CLASS_DEVICE)
static bool rules_backlight(b64 hundredths)
{
        static const enum backlight_type kinds[] = {BACKLIGHT_FIRMWARE, BACKLIGHT_PLATFORM, BACKLIGHT_RAW};

        for (positive at = 0; at < array_count(kinds); at++)
        {
                struct backlight_device *device = backlight_device_get_by_type(kinds[at]);

                if (device && device->props.max_brightness > 0)
                {
                        p64 level = (p64)hundredths * (p64)device->props.max_brightness / 10000;

                        return !backlight_device_set_brightness(device, (unsigned long)level);
                }
        }
        return false;
}
#else
static bool rules_backlight(b64 hundredths)
{
        (void)hundredths;
        return false;
}
#endif

static void rules_do(const struct rules_job *job)
{
        char line[64];
        p16 id = job->primitive;

        if (id == rules_p_log)
                pr_info("[rover] %s: %*pE\n", job->name, (int)string_length((string_address)job->text), job->text);
        else if (id == rules_p_latency)
        {
                s32 us = (s32)(job->number / 1000);

                if (cpu_latency_qos_request_active(&rules_qos))
                        cpu_latency_qos_update_request(&rules_qos, us);
                else
                        cpu_latency_qos_add_request(&rules_qos, us);
        }
        else if (id == rules_p_unlatency)
        {
                if (cpu_latency_qos_request_active(&rules_qos))
                        cpu_latency_qos_remove_request(&rules_qos);
        }
        else if (id == rules_p_backlight)
        {
                if (!rules_backlight(job->number))
                {
                        snprintf(line, sizeof(line), "moonwater brightness %lld%%", (long long)(job->number / 100));
                        rules_spawn(line, job->name);
                }
        }
        else if (id == rules_p_suspend)
                rules_spawn("moonwater sleep", job->name);
        else if (id == rules_p_radio_off)
                rules_spawn(job->number == 0   ? "moonwater wifi off"
                            : job->number == 1 ? "moonwater bluetooth off"
                                               : "moonwater airplane on",
                            job->name);
        else if (id == rules_p_usb_lock)
                rules_spawn("moonwater device lock", job->name);
}

static void rules_work_run(struct work_struct *work)
{
        (void)work;
        for (;;)
        {
                struct rules_job job;
                unsigned long flags;

                raw_spin_lock_irqsave(&rules_lock, flags);
                if (rules_job_tail == rules_job_head)
                {
                        raw_spin_unlock_irqrestore(&rules_lock, flags);
                        return;
                }
                job = rules_jobs[rules_job_tail % RULES_JOBS];
                rules_job_tail++;
                raw_spin_unlock_irqrestore(&rules_lock, flags);
                rules_do(&job);
        }
}

/*
        The machine stopped as the bindings stop it: the line run, and the
        kernel's own stop when the line fails. A line that never returns is
        not waited for forever: the watchdog, armed before it runs, stops the
        machine RULES_STOP_GRACE after the request whatever the line is doing.
*/
#define RULES_STOP_GRACE (60 * HZ)

static void rules_stop_force(struct work_struct *work)
{
        p16 id = READ_ONCE(rules_stop_asked);

        (void)work;
        pr_warn("[rover] the machine's stop did not come in %u s: stopping it\n", RULES_STOP_GRACE / HZ);
        if (id == rules_p_reboot)
                orderly_reboot();
        else
                orderly_poweroff(true);
}

static void rules_stop_run(struct work_struct *work)
{
        char name[ROVER_NAME_MOST + 1];
        unsigned long flags;
        p16 id;

        (void)work;
        raw_spin_lock_irqsave(&rules_lock, flags);
        id = rules_stop_asked;
        strscpy(name, rules_stop_name, sizeof(name));
        raw_spin_unlock_irqrestore(&rules_lock, flags);
        if (!id)
                return;
        mod_delayed_work(system_highpri_wq, &rules_stop_watchdog, RULES_STOP_GRACE);
        if (rules_spawn(id == rules_p_poweroff ? "poweroff" : "reboot", name))
        {
                pr_warn("[rover] %s: stopping the machine anyway\n", name);
                if (id == rules_p_poweroff)
                        orderly_poweroff(true);
                else
                        orderly_reboot();
        }
}

static void rules_irq_run(struct irq_work *work)
{
        (void)work;
        if (READ_ONCE(rules_stop_asked))
                queue_work(system_highpri_wq, &rules_stop_work);
        queue_work(system_highpri_wq, &rules_work);
}

// ---- an event ------------------------------------------------------------------

#define RULES_HITS ROVER_KERNEL_HITS

/*
        One event against the live table, with the lock held: the kernel's
        rules fired, their kernel actions queued, their hits and the event
        (when a runner's rule waits for it) posted. Answers whether a rule
        that fired says take. started is when the hook began, for the count.
*/
static bool rules_event_locked(const struct rules_live *live, const struct rover_event *event, p64 started,
                               bool *posted, bool *queued)
{
        struct rover_hit hits[RULES_HITS];
        const p8 *pages[ROVER_PAGES] = {rules_page, null};
        positive made = 0;
        bool taken = false;
        p64 decided;

        rules_count.events++;
        if (rules_bit(live->kernel, event->id))
                made = rover_fire(&live->view, live->runs, pages, event, ROVER_KERNEL, hits, RULES_HITS);
        decided = ktime_get_ns();
        if (made)
        {
                p64 since = decided > event->when ? decided - event->when : 0;
                p64 hook = decided - started;

                rules_count.decide_ns += since;
                rules_count.decide_count++;
                if (since > rules_count.decide_worst)
                        rules_count.decide_worst = since;
                rules_count.hook_ns += hook;
                rules_count.hook_count++;
                if (hook > rules_count.hook_worst)
                        rules_count.hook_worst = hook;
        }
        for (positive at = 0; at < made; at++)
        {
                const struct rover_rule *rule = live->view.rule + hits[at].rule;
                p8 flags = hits[at].otherwise ? ROVER_RECORD_ELSE : 0;

                rules_count.hits++;
                if (rule->flags & ROVER_RULE_TAKE)
                        taken = true;
                for (positive k = 0; k < hits[at].count; k++)
                {
                        positive which = (positive)hits[at].action + k;
                        const struct rover_action *action = live->view.action + which;

                        if (action->kind == ROVER_DO && action->runs == ROVER_KERNEL)
                        {
                                rules_queue_locked(live, event, which, hits[at].rule);
                                *queued = true;
                        }
                        else
                                flags |= ROVER_RECORD_USER;
                }
                if (rule->flags & ROVER_RULE_TAKE)
                        flags |= ROVER_RECORD_TAKEN;
                rules_post_locked(live, event, ROVER_RECORD_HIT, flags, &hits[at], decided);
                *posted = true;
        }
        if (rules_bit(live->runner, event->id))
        {
                rules_post_locked(live, event, ROVER_RECORD_EVENT, 0, null, decided);
                *posted = true;
        }
        return taken;
}

//      The when rules whose states changed, with the lock held.
static void rules_edges_locked(const struct rules_live *live, p64 changed, p64 when, bool *posted, bool *queued)
{
        struct rover_hit hits[RULES_HITS];
        const p8 *pages[ROVER_PAGES] = {rules_page, null};
        struct rover_event edge;
        positive made;

        if (!changed || !live->view.index[0].count)
                return;
        memory_zero(&edge, sizeof(edge));
        edge.when = when;
        edge.realtime = ktime_get_real_seconds();
        made = rover_edges(&live->view, live->runs, pages, changed, &edge, ROVER_KERNEL, hits, RULES_HITS);
        for (positive at = 0; at < made; at++)
        {
                p8 flags = hits[at].otherwise ? ROVER_RECORD_ELSE : 0;

                rules_count.hits++;
                for (positive k = 0; k < hits[at].count; k++)
                {
                        positive which = (positive)hits[at].action + k;
                        const struct rover_action *action = live->view.action + which;

                        if (action->kind == ROVER_DO && action->runs == ROVER_KERNEL)
                        {
                                rules_queue_locked(live, &edge, which, hits[at].rule);
                                *queued = true;
                        }
                        else
                                flags |= ROVER_RECORD_USER;
                }
                rules_post_locked(live, &edge, ROVER_RECORD_HIT, flags, &hits[at], ktime_get_ns());
                *posted = true;
        }
}

//      After the lock is let go: the ring's readers woken, the queue's worker asked for.
static inline void rules_after(bool posted, bool queued)
{
        if (posted)
        {
                wake_up_interruptible_poll(&rules_wait, EPOLLIN);
                //      The machine process is the ring's consumer until the runner is (bind_machine_wait).
                if (atomic_read(&bind_machine_live) && moonwater_rules_pending() != READ_ONCE(bind_machine.ring_seen))
                        wake_up(&bind_machine.wait);
        }
        if (queued)
                irq_work_queue(&rules_irq);
}

/*
        An event that may change states: the states written first, so its own
        rules read the page as the event left it, then its rules, then the
        when rules over what changed. Any context; nothing here sleeps.
*/
#define RULES_SETS 4u

struct rules_change
{
        p8 which;
        b32 value;
};

static bool rules_fire(struct rover_event *event, const struct rules_change *sets, positive count, p64 started)
{
        struct rules_live *live;
        unsigned long flags;
        bool taken = false, posted = false, queued = false;
        p64 changed = 0;

        if (!rules_map)
                return false;
        if (!event->realtime)
                event->realtime = ktime_get_real_seconds();
        rcu_read_lock();
        raw_spin_lock_irqsave(&rules_lock, flags);
        for (positive at = 0; at < count; at++)
                changed |= rules_set_locked(sets[at].which, sets[at].value);
        live = rcu_dereference(rules_now);
        if (live)
        {
                if (event->id)
                        taken = rules_event_locked(live, event, started, &posted, &queued);
                rules_edges_locked(live, changed, event->when, &posted, &queued);
        }
        raw_spin_unlock_irqrestore(&rules_lock, flags);
        rcu_read_unlock();
        rules_after(posted, queued);
        return taken;
}

static inline bool rules_wanted(p16 event)
{
        struct rules_live *live;
        bool wanted;

        rcu_read_lock();
        live = rcu_dereference(rules_now);
        wanted = live && (rules_bit(live->kernel, event) || rules_bit(live->runner, event));
        rcu_read_unlock();
        return wanted;
}

static void rules_event_init(struct rover_event *event, p16 id, p64 when)
{
        memory_zero(event, sizeof(*event));
        event->id = id;
        event->when = when ? when : ktime_get_ns();
}

static inline void rules_word(struct rover_event *event, positive field, const char *text)
{
        positive length = text ? string_length((string_address)text) : 0;

        if (length > ROVER_RECORD_WORD_MOST)
                length = ROVER_RECORD_WORD_MOST;
        event->field[field].text = (const p8 *)(text ? text : "");
        event->field[field].length = (p32)length;
        if (event->count <= field)
                event->count = (p8)(field + 1);
}

static inline void rules_number(struct rover_event *event, positive field, b64 number)
{
        event->field[field].number = number;
        event->field[field].text = null;
        if (event->count <= field)
                event->count = (p8)(field + 1);
}

//      A spark event that came another way than a key: canvas on and off, from Canvas.
void moonwater_rules_spark(unsigned int id)
{
        struct rover_event event;
        struct rules_change set = {RS_CANVAS, id == SPARK_BIND_CANVAS_ON};

        if (id != SPARK_BIND_CANVAS_ON && id != SPARK_BIND_CANVAS_OFF)
                return;
        rules_event_init(&event, (p16)id, 0);
        rules_fire(&event, &set, 1, event.when);
}

// ---- input ---------------------------------------------------------------------

#define RULES_KEY_DOWN 23u
#define RULES_KEY_UP 24u
#define RULES_MOUSE_DOWN 25u
#define RULES_MOUSE_UP 26u
#define RULES_PAD_DOWN 27u
#define RULES_PAD_UP 28u
#define RULES_TOUCH_DOWN 29u
#define RULES_TOUCH_UP 30u
#define RULES_SWITCH_ON 31u
#define RULES_SWITCH_OFF 32u
#define RULES_POWER_AC 33u
#define RULES_POWER_BATTERY 34u
#define RULES_BATTERY 35u
#define RULES_USB_ADDED 36u
#define RULES_USB_REMOVED 37u
#define RULES_INPUT_ADDED 38u
#define RULES_INPUT_REMOVED 39u
#define RULES_DISPLAY_CONNECTED 40u
#define RULES_DISPLAY_DISCONNECTED 41u
#define RULES_LINK_UP 42u
#define RULES_LINK_DOWN 43u
#define RULES_SLEEP 44u

//      Spark's event of each key, for the keys that are one.
static u8 rules_spark_key[KEY_CNT];

/*
        The floor (README, Safety): the power key, Reset and Ctrl-Alt-Delete
        always work. A rule may take a stop key's press only when the press
        before it was not taken inside ten seconds, so a second press goes on
        to the bindings and stops the machine as it always did. Timed on the
        kernel's own clock when the hook began, not a device's stamp (two
        devices, or a driver's stamp, may run backwards), with a signed
        difference; under the rules' lock, since two keyboards may press at
        once. This is the one floor: the bindings below the filter hear what
        it lets through, and a key down rule never takes a stop key at all.
*/
#define RULES_FLOOR_NS (10ll * 1000000000ll)
static p64 rules_floor_last[3];

static b32 rules_floor_slot(p16 spark)
{
        return spark == SPARK_BIND_POWEROFF ? 0 : spark == SPARK_BIND_RESET ? 1 : spark == SPARK_BIND_CTRL_ALT_DELETE ? 2 : -1;
}

//      Whether a stop key's press that a rule took may stay taken; a key that is no stop key always may.
static bool rules_floor(p16 spark, p64 now)
{
        b32 slot = rules_floor_slot(spark);
        unsigned long flags;
        bool kept = true;

        if (slot < 0)
                return true;
        raw_spin_lock_irqsave(&rules_lock, flags);
        if (rules_floor_last[slot] && (b64)(now - rules_floor_last[slot]) < RULES_FLOOR_NS)
        {
                rules_floor_last[slot] = 0;
                rules_count.floor++;
                kept = false;
        }
        else
                rules_floor_last[slot] = now;
        raw_spin_unlock_irqrestore(&rules_lock, flags);
        return kept;
}

//      A press that is one of the stop keys: power, reset, or Delete with Ctrl and Alt held.
static bool rules_stop_press(unsigned int code, p32 mods)
{
        p16 spark = rules_spark_key[code];

        return spark == SPARK_BIND_POWEROFF || spark == SPARK_BIND_RESET ||
               (spark == SPARK_BIND_CTRL_ALT_DELETE &&
                (mods & (ROVER_CHORD_CTRL | ROVER_CHORD_ALT)) == (ROVER_CHORD_CTRL | ROVER_CHORD_ALT));
}


//      The chord modifiers held, over every keyboard: a count for each, and each handle's own bits.
static atomic_t rules_held[4];
static const u8 rules_mod_bit[KEY_CNT] = {
        [KEY_LEFTSHIFT] = 1, [KEY_RIGHTSHIFT] = 2, [KEY_LEFTCTRL] = 4, [KEY_RIGHTCTRL] = 8,
        [KEY_LEFTALT] = 16, [KEY_RIGHTALT] = 32, [KEY_LEFTMETA] = 64, [KEY_RIGHTMETA] = 128,
};

static p32 rules_chord_mods(void)
{
        return (atomic_read(&rules_held[0]) > 0 ? ROVER_CHORD_SHIFT : 0) |
               (atomic_read(&rules_held[1]) > 0 ? ROVER_CHORD_CTRL : 0) |
               (atomic_read(&rules_held[2]) > 0 ? ROVER_CHORD_ALT : 0) |
               (atomic_read(&rules_held[3]) > 0 ? ROVER_CHORD_SUPER : 0);
}

static void rules_mods(struct bind_handle *bind, unsigned int code, int value)
{
        u8 bit = code < KEY_CNT ? rules_mod_bit[code] : 0;
        positive which;

        if (!bit)
                return;
        which = bit & 3 ? 0 : bit & 12 ? 1 : bit & 48 ? 2 : 3;
        if (value == 1 && !(bind->rover_mods & bit))
        {
                bind->rover_mods |= bit;
                atomic_fetch_add(1, &rules_held[which]);
        }
        else if (value == 0 && (bind->rover_mods & bit))
        {
                bind->rover_mods &= ~bit;
                atomic_fetch_sub(1, &rules_held[which]);
        }
}

static const struct {
        u16 code;
        u8 event[2]; // spark's for 0 and 1
        u8 state;
} rules_switch[] = {
        {SW_LID, {SPARK_BIND_LID_OPEN, SPARK_BIND_LID_CLOSE}, RS_LID},
        {SW_TABLET_MODE, {SPARK_BIND_TABLET_OFF, SPARK_BIND_TABLET_ON}, RS_TABLET},
        {SW_HEADPHONE_INSERT, {SPARK_BIND_HEADPHONE_OFF, SPARK_BIND_HEADPHONE_ON}, RS_HEADPHONE},
        {SW_DOCK, {SPARK_BIND_DOCK_OFF, SPARK_BIND_DOCK_ON}, RS_DOCK},
};

//      A switch's spark event and state; the generic switch on or off is posted besides.
static bool rules_switched(unsigned int code, int value, p64 when, p64 started)
{
        struct rover_event event;
        struct rules_change set;

        if (code > SW_MAX || (unsigned int)value > 1)
                return false;
        for (positive at = 0; at < array_count(rules_switch); at++)
                if (rules_switch[at].code == code)
                {
                        set.which = rules_switch[at].state;
                        set.value = value;
                        rules_event_init(&event, rules_switch[at].event[value], when);
                        rules_fire(&event, &set, 1, started);
                }
        if (!rules_wanted(value ? RULES_SWITCH_ON : RULES_SWITCH_OFF))
                return false;
        rules_event_init(&event, value ? RULES_SWITCH_ON : RULES_SWITCH_OFF, when);
        rules_number(&event, 0, code);
        rules_fire(&event, null, 0, started);
        return false;
}

static bool rules_button(unsigned int code, int value, p64 when, p64 started)
{
        struct rover_event event;
        p16 id;
        b64 which;

        if (code >= BTN_MOUSE && code <= BTN_TASK)
                id = value ? RULES_MOUSE_DOWN : RULES_MOUSE_UP, which = code - BTN_MOUSE;
        else if (code >= BTN_SOUTH && code <= BTN_THUMBR)
                id = value ? RULES_PAD_DOWN : RULES_PAD_UP, which = code - BTN_SOUTH;
        else if (code >= BTN_DPAD_UP && code <= BTN_DPAD_RIGHT)
                id = value ? RULES_PAD_DOWN : RULES_PAD_UP, which = 15 + code - BTN_DPAD_UP;
        else if (code == BTN_TOUCH)
                id = value ? RULES_TOUCH_DOWN : RULES_TOUCH_UP, which = -1;
        else
                return false;
        if (!rules_wanted(id))
                return false;
        rules_event_init(&event, id, when);
        if (which >= 0)
                rules_number(&event, 0, which);
        return rules_fire(&event, null, 0, started);
}

/*
        Where an input device comes from. One is the machine's own when the
        device the kernel hangs it under is a real bus's: its chain of
        parents ends at a device of no class (a platform, ACPI's, a PCI root),
        where a program's device (uinput's input device has no parent at all;
        uhid's hangs under the uhid misc device, a class device) ends at one
        of a class, which sysfs puts under devices/virtual. The compositor's
        rule (session.c, WINDOW_EVENT_VIRTUAL), asked of the kernel's own
        devices rather than of a sysfs link. A program writes a uinput or
        uhid device's bus, vendor, name and phys, so none of those is asked.
*/
static bool rules_device_physical(struct input_dev *dev)
{
        struct device *at = &dev->dev;

        while (at->parent)
                at = at->parent;
        return at != &dev->dev && !at->class;
}

/*
        One input event, from bind_filter, in the input core, under the
        device's event lock with interrupts off. Answers whether a rule took
        it, so no reader ever sees it; a key taken on its way down is taken on
        its repeats and its way up as well. A device that is not the
        machine's own is heard by no rule: its keys fire none, its switches
        write no state, and nothing of it is taken (the origin model; a rule
        cannot tell a program's power key from the person's, so a program's
        is never one).
*/
static bool rules_input(struct bind_handle *bind, unsigned int type, unsigned int code, int value)
{
        p64 started, when;
        struct rover_event event;
        bool taken = false;
        p16 spark;

        if (likely(type != EV_KEY && type != EV_SW) || !rules_map || bind->rover_virtual)
                return false;
        started = ktime_get_ns();
        when = ktime_to_ns(input_get_timestamp(bind->handle.dev)[INPUT_CLK_MONO]);
        if (!when || when > started)
                when = started;
        if (type == EV_SW)
                return rules_switched(code, value, when, started);
        if (code >= KEY_CNT)
                return false;
        if (value == 2)
                return test_bit(code, bind->rover_taken);
        if (code >= BTN_MISC && code < KEY_OK)
                taken = rules_button(code, value, when, started);
        else
        {
                p32 mods = rules_chord_mods();

                rules_mods(bind, code, value);
                spark = value == 1 ? rules_spark_key[code] : 0;
                if (spark == SPARK_BIND_CTRL_ALT_DELETE &&
                    (mods & (ROVER_CHORD_CTRL | ROVER_CHORD_ALT)) != (ROVER_CHORD_CTRL | ROVER_CHORD_ALT))
                        spark = 0;
                if (spark && rules_wanted(spark))
                {
                        rules_event_init(&event, spark, when);
                        if (rules_fire(&event, null, 0, started))
                                taken = rules_floor(spark, started);
                }
                if (rules_wanted(value ? RULES_KEY_DOWN : RULES_KEY_UP))
                {
                        rules_event_init(&event, value ? RULES_KEY_DOWN : RULES_KEY_UP, when);
                        rules_number(&event, 0, (b64)(value ? mods : rules_chord_mods()) << 16 | code);
                        //      The stop keys keep their floor: key down power cannot take what key power may not.
                        if (rules_fire(&event, null, 0, started) && !rules_stop_press(code, mods))
                                taken = true;
                }
        }
        //      A press and its release are taken together: a release is taken when its press was, and only then,
        //      whatever a rule on the release said, so no reader is ever left holding a key, a button or a finger.
        if (value == 1)
                __assign_bit(code, bind->rover_taken, taken);
        else if (value == 0)
        {
                taken = test_bit(code, bind->rover_taken);
                if (taken)
                        __clear_bit(code, bind->rover_taken);
        }
        if (taken)
        {
                unsigned long flags;

                raw_spin_lock_irqsave(&rules_lock, flags);
                rules_count.taken++;
                raw_spin_unlock_irqrestore(&rules_lock, flags);
        }
        return taken;
}

//      A device's kind, as input added says it: what its bits make it.
static p8 rules_input_kind(struct input_dev *dev)
{
        if (test_bit(EV_ABS, dev->evbit) && test_bit(BTN_TOUCH, dev->keybit) && test_bit(INPUT_PROP_DIRECT, dev->propbit))
                return 3; // touchscreen
        if (test_bit(BTN_TOOL_PEN, dev->keybit))
                return 5; // tablet
        if (test_bit(BTN_TOOL_FINGER, dev->keybit) && test_bit(INPUT_PROP_POINTER, dev->propbit))
                return 2; // touchpad
        if (test_bit(BTN_SOUTH, dev->keybit) || test_bit(BTN_JOYSTICK, dev->keybit))
                return 4; // pad
        if (test_bit(EV_REL, dev->evbit) && test_bit(BTN_LEFT, dev->keybit))
                return 1; // mouse
        if (test_bit(KEY_A, dev->keybit) || test_bit(KEY_ENTER, dev->keybit))
                return 0; // keyboard
        if (test_bit(EV_SW, dev->evbit))
                return 6; // switch
        return 7;
}

/*
        A device the input core connected or let go, from bind_connect and
        bind_disconnect. A new device's switches are read into the page, so
        a lid closed at boot reads closed before it ever moves.
*/
static void rules_device(struct bind_handle *bind, bool added)
{
        struct input_dev *dev = bind->handle.dev;
        struct rover_event event;
        struct rules_change sets[RULES_SETS];
        positive count = 0;

        //      A program's device is no rule's: not even its coming and going, whose name it chose.
        if (!rules_map || bind->rover_virtual)
                return;
        if (!added)
        {
                for (positive at = 0; at < KEY_CNT; at++)
                        if (rules_mod_bit[at])
                                rules_mods(bind, (unsigned int)at, 0);
        }
        else if (test_bit(EV_SW, dev->evbit))
                for (positive at = 0; at < array_count(rules_switch) && count < RULES_SETS; at++)
                        if (test_bit(rules_switch[at].code, dev->swbit))
                        {
                                sets[count].which = rules_switch[at].state;
                                sets[count].value = !!test_bit(rules_switch[at].code, dev->sw);
                                count++;
                        }
        rules_event_init(&event, added ? RULES_INPUT_ADDED : RULES_INPUT_REMOVED, 0);
        rules_number(&event, 0, rules_input_kind(dev));
        rules_word(&event, 1, dev->name);
        rules_fire(&event, sets, count, event.when);
}

// ---- power ----------------------------------------------------------------------

#if IS_REACHABLE(CONFIG_POWER_SUPPLY)
struct rules_power_seen
{
        bool mains, battery;
        b32 percent, status;
};

static int rules_power_each(struct power_supply *psy, void *data)
{
        struct rules_power_seen *seen = data;
        union power_supply_propval value;

        if (psy->desc->type == POWER_SUPPLY_TYPE_BATTERY)
        {
                if (!power_supply_get_property(psy, POWER_SUPPLY_PROP_SCOPE, &value) &&
                    value.intval == POWER_SUPPLY_SCOPE_DEVICE)
                        return 0;
                if (!seen->battery)
                {
                        seen->battery = true;
                        if (!power_supply_get_property(psy, POWER_SUPPLY_PROP_CAPACITY, &value))
                                seen->percent = value.intval < 0 ? 0 : value.intval > 100 ? 10000 : value.intval * 100;
                        if (!power_supply_get_property(psy, POWER_SUPPLY_PROP_STATUS, &value))
                        {
                                //      The kernel's order is unknown, charging, discharging, not charging, full.
                                static const b32 ours[] = {0, 1, 2, 4, 3};

                                seen->status = (unsigned int)value.intval < array_count(ours) ? ours[value.intval] : 0;
                        }
                }
                return 0;
        }
        if (!power_supply_get_property(psy, POWER_SUPPLY_PROP_ONLINE, &value) && value.intval)
                seen->mains = true;
        return 0;
}

//      Every supply read again at the notifier, which a supply's change work calls: no polling.
static void rules_power_read(void)
{
        struct rules_power_seen seen = {0};
        struct rules_change sets[3];
        struct rover_event event;
        b32 source, before_source = rules_state(RS_SOURCE), before_percent = rules_state(RS_PERCENT),
                    before_status = rules_state(RS_BATTERY);

        power_supply_for_each_psy(&seen, rules_power_each);
        source = seen.battery && !seen.mains ? 1 : 0;
        sets[0] = (struct rules_change){RS_SOURCE, source};
        sets[1] = (struct rules_change){RS_PERCENT, seen.percent};
        sets[2] = (struct rules_change){RS_BATTERY, seen.status};
        if (source != before_source)
        {
                rules_event_init(&event, source ? RULES_POWER_BATTERY : RULES_POWER_AC, 0);
                rules_fire(&event, sets, 3, event.when);
        }
        if (seen.battery && (seen.percent != before_percent || seen.status != before_status))
        {
                rules_event_init(&event, RULES_BATTERY, 0);
                rules_number(&event, 0, seen.percent);
                rules_number(&event, 1, seen.status);
                rules_fire(&event, sets, 3, event.when);
        }
        else if (source == before_source)
        {
                rules_event_init(&event, 0, 0);
                rules_fire(&event, sets, 3, event.when);
        }
}

static int rules_power_notify(struct notifier_block *nb, unsigned long what, void *data)
{
        (void)nb;
        (void)data;
        if (what == PSY_EVENT_PROP_CHANGED)
                rules_power_read();
        return NOTIFY_OK;
}

static struct notifier_block rules_power_nb = {.notifier_call = rules_power_notify};
#endif

// ---- USB --------------------------------------------------------------------------

#if IS_REACHABLE(CONFIG_USB)
static p32 rules_usb_devices;

//      The class: the device's, or its first interface's when the device leaves it to them.
static b64 rules_usb_class(struct usb_device *udev)
{
        u8 code = udev->descriptor.bDeviceClass;

        if (!code && udev->actconfig && udev->actconfig->desc.bNumInterfaces && udev->actconfig->interface[0])
                code = udev->actconfig->interface[0]->cur_altsetting->desc.bInterfaceClass;
        switch (code)
        {
        case USB_CLASS_AUDIO: return 1;
        case USB_CLASS_COMM: return 2;
        case USB_CLASS_HID: return 3;
        case USB_CLASS_STILL_IMAGE: return 4;
        case USB_CLASS_PRINTER: return 5;
        case USB_CLASS_MASS_STORAGE: return 6;
        case USB_CLASS_HUB: return 7;
        case USB_CLASS_CDC_DATA: return 8;
        case USB_CLASS_CSCID: return 9;
        case USB_CLASS_VIDEO: return 10;
        case USB_CLASS_WIRELESS_CONTROLLER: return 11;
        case USB_CLASS_VENDOR_SPEC: return 12;
        }
        return 0;
}

static int rules_usb_notify(struct notifier_block *nb, unsigned long what, void *data)
{
        struct usb_device *udev = data;
        struct rover_event event;
        struct rules_change set;
        bool added = what == USB_DEVICE_ADD;

        (void)nb;
        if ((what != USB_DEVICE_ADD && what != USB_DEVICE_REMOVE) || !udev->parent)
                return NOTIFY_OK;
        if (added)
                rules_usb_devices++;
        else if (rules_usb_devices)
                rules_usb_devices--;
        set = (struct rules_change){RS_USB, rules_usb_devices > 255 ? 255 : (b32)rules_usb_devices};
        rules_event_init(&event, added ? RULES_USB_ADDED : RULES_USB_REMOVED, 0);
        rules_number(&event, 0, rules_usb_class(udev));
        rules_number(&event, 1, le16_to_cpu(udev->descriptor.idVendor));
        rules_number(&event, 2, le16_to_cpu(udev->descriptor.idProduct));
        rules_word(&event, 3, dev_name(&udev->dev));
        rules_fire(&event, &set, 1, event.when);
        return NOTIFY_OK;
}

static struct notifier_block rules_usb_nb = {.notifier_call = rules_usb_notify};
#endif

// ---- links ---------------------------------------------------------------------------

//      Which links had a carrier, by ifindex, the first 256 of them; the notifier is called under RTNL.
static DECLARE_BITMAP(rules_link_up, 256);
static p32 rules_links;

static int rules_link_notify(struct notifier_block *nb, unsigned long what, void *data)
{
        struct net_device *dev = netdev_notifier_info_to_dev(data);
        struct rover_event event;
        struct rules_change set;
        bool up, was;

        (void)nb;
        if (what != NETDEV_CHANGE && what != NETDEV_UP && what != NETDEV_DOWN && what != NETDEV_UNREGISTER)
                return NOTIFY_DONE;
        if (!net_eq(dev_net(dev), &init_net) || (dev->flags & IFF_LOOPBACK) || dev->ifindex <= 0 ||
            dev->ifindex >= 256)
                return NOTIFY_DONE;
        up = what != NETDEV_UNREGISTER && what != NETDEV_DOWN && netif_running(dev) && netif_carrier_ok(dev);
        was = test_bit(dev->ifindex, rules_link_up);
        if (up == was)
                return NOTIFY_DONE;
        if (up)
        {
                __set_bit(dev->ifindex, rules_link_up);
                rules_links++;
        }
        else
        {
                __clear_bit(dev->ifindex, rules_link_up);
                rules_links--;
        }
        set = (struct rules_change){RS_LINKS, (b32)rules_links};
        rules_event_init(&event, up ? RULES_LINK_UP : RULES_LINK_DOWN, 0);
        rules_word(&event, 0, dev->name);
        rules_fire(&event, &set, 1, event.when);
        return NOTIFY_DONE;
}

static struct notifier_block rules_link_nb = {.notifier_call = rules_link_notify};

// ---- displays ------------------------------------------------------------------------

//      From Canvas's DRM client hotplug: the counts after it, and one connector that came or went.
void moonwater_rules_display(unsigned int count, unsigned int external, const char *connector, _Bool connected)
{
        struct rover_event event;
        struct rules_change sets[2] = {{RS_DISPLAYS, (b32)(count > 16 ? 16 : count)},
                                       {RS_EXTERNAL, (b32)(external > 16 ? 16 : external)}};

        rules_event_init(&event, connector ? (connected ? RULES_DISPLAY_CONNECTED : RULES_DISPLAY_DISCONNECTED) : 0, 0);
        if (connector)
                rules_word(&event, 0, connector);
        rules_fire(&event, sets, 2, event.when);
}

// ---- sleep and wake ---------------------------------------------------------------------

#ifdef CONFIG_PM
static int rules_pm_notify(struct notifier_block *nb, unsigned long what, void *data)
{
        struct rover_event event;

        (void)nb;
        (void)data;
        if (what != PM_SUSPEND_PREPARE && what != PM_POST_SUSPEND)
                return NOTIFY_DONE;
        rules_event_init(&event, what == PM_SUSPEND_PREPARE ? RULES_SLEEP : SPARK_BIND_RESUME, 0);
        rules_fire(&event, null, 0, event.when);
        return NOTIFY_OK;
}

static struct notifier_block rules_pm_nb = {.notifier_call = rules_pm_notify};
#endif

// ---- the load ------------------------------------------------------------------------------

static void rules_free(struct rules_live *live)
{
        if (!live)
                return;
        kvfree(live->runs);
        kvfree(live->bytes);
        kfree(live);
}

static void rules_free_rcu(struct rcu_head *head)
{
        rules_free(container_of(head, struct rules_live, rcu));
}

/*
        A table in: copied, checked whole, its runs made, and swapped in
        under the lock. A refused table is answered with the reason and the
        rule, and the one before it stays. Loading is every state changing
        at once, so the when rules that already hold fire now.
*/
static long rules_load(struct rover_control *request)
{
        struct rules_live *live, *before;
        struct rover_refusal why;
        unsigned long flags;
        bool posted = false, queued = false;

        if (!request->length || request->length > ROVER_TABLE_MOST)
                return -EINVAL;
        live = kzalloc(sizeof(*live), GFP_KERNEL);
        if (!live)
                return -ENOMEM;
        live->bytes = kvmalloc(request->length, GFP_KERNEL);
        if (!live->bytes)
        {
                rules_free(live);
                return -ENOMEM;
        }
        if (copy_from_user(live->bytes, u64_to_user_ptr(request->address), request->length))
        {
                rules_free(live);
                return -EFAULT;
        }
        live->size = request->length;
        if (!rover_check(live->bytes, live->size, &live->view, &why))
        {
                strscpy(request->why, why.why, sizeof(request->why));
                request->rule = why.rule;
                rules_free(live);
                return -EINVAL;
        }
        //      Never a table partly inert: whatever can fire on one event fits what one event is answered.
        {
                p32 event = 0;
                const char *unfit = rover_kernel_fits(&live->view, &event);

                if (unfit)
                {
                        snprintf(request->why, sizeof(request->why), "%s (event %u)", unfit, event);
                        rules_free(live);
                        return -EINVAL;
                }
        }
        live->runs = kvcalloc(live->view.head->rules ? live->view.head->rules : 1, sizeof(struct rover_run),
                              GFP_KERNEL);
        if (!live->runs)
        {
                rules_free(live);
                return -ENOMEM;
        }
        for (positive at = 0; at < live->view.head->rules; at++)
        {
                const struct rover_rule *rule = live->view.rule + at;
                p64 *set = rule->place == ROVER_KERNEL ? live->kernel : rule->place == ROVER_RUNNER ? live->runner : null;

                if (rule->place == ROVER_KERNEL)
                        live->kernel_rules++;
                if (set && rule->event && rover_event_find(rule->event) &&
                    rover_event_find(rule->event)->source == ROVER_KERNEL)
                        set[rule->event >> 6] |= 1ull << (rule->event & 63);
        }

        mutex_lock(&rules_load_lock);
        live->table = ++rules_loads;
        raw_spin_lock_irqsave(&rules_lock, flags);
        before = rcu_dereference_protected(rules_now, lockdep_is_held(&rules_load_lock));
        rcu_assign_pointer(rules_now, live);
        rules_edges_locked(live, ~0ull, ktime_get_ns(), &posted, &queued);
        raw_spin_unlock_irqrestore(&rules_lock, flags);
        mutex_unlock(&rules_load_lock);
        rules_after(posted, queued);
        if (before)
                call_rcu(&before->rcu, rules_free_rcu);
        request->table = live->table;
        request->rules = live->view.head->rules;
        request->kernel_rules = live->kernel_rules;
        pr_info("[rover] table %u loaded: %u rules, %u of them the kernel's\n", live->table, live->view.head->rules,
                live->kernel_rules);
        return 0;
}

static long rules_unload(void)
{
        struct rules_live *before;
        unsigned long flags;

        mutex_lock(&rules_load_lock);
        raw_spin_lock_irqsave(&rules_lock, flags);
        before = rcu_dereference_protected(rules_now, lockdep_is_held(&rules_load_lock));
        RCU_INIT_POINTER(rules_now, null);
        raw_spin_unlock_irqrestore(&rules_lock, flags);
        mutex_unlock(&rules_load_lock);
        if (before)
                call_rcu(&before->rcu, rules_free_rcu);
        return 0;
}

static void rules_status(struct rover_control *request)
{
        struct rules_live *live;
        unsigned long flags;

        raw_spin_lock_irqsave(&rules_lock, flags);
        live = rcu_dereference_check(rules_now, lockdep_is_held(&rules_lock));
        request->table = live ? live->table : 0;
        request->rules = live ? live->view.head->rules : 0;
        request->kernel_rules = live ? live->kernel_rules : 0;
        request->events = rules_count.events;
        request->hits = rules_count.hits;
        request->taken = rules_count.taken;
        request->floor = rules_count.floor;
        request->queued = rules_count.queued;
        request->lost = rules_count.lost;
        request->posted = rules_count.posted;
        request->decide_ns = rules_count.decide_ns;
        request->decide_worst = rules_count.decide_worst;
        request->decide_count = rules_count.decide_count;
        request->hook_ns = rules_count.hook_ns;
        request->hook_worst = rules_count.hook_worst;
        request->hook_count = rules_count.hook_count;
        raw_spin_unlock_irqrestore(&rules_lock, flags);
}

//      DO: a userspace rule's kernel primitive, through the same queue a kernel rule's goes through.
static long rules_do_asked(struct rover_control *request)
{
        const struct rover_primitive_row *primitive = rover_primitive_find(request->primitive);
        struct rules_job *job;
        unsigned long flags;

        if (!primitive || primitive->runs != ROVER_KERNEL)
                return -EINVAL;
        if (primitive->klass == ROVER_DELIBERATE)
                return -EPERM;
        //      Held to the schema's range as a table's constant is by the loader: latency at most a day, a backlight
        //      0 to 100%, an enum one of its words.
        if (primitive->count && !rover_kind_is_word(rover_types[primitive->type[0]].kind) &&
            !rover_number_ok(primitive->type[0], request->number))
                return -ERANGE;
        raw_spin_lock_irqsave(&rules_lock, flags);
        if (request->primitive == rules_p_poweroff || request->primitive == rules_p_reboot)
        {
                rules_stop_locked(request->primitive, "asked");
                rules_count.queued++;
                raw_spin_unlock_irqrestore(&rules_lock, flags);
                irq_work_queue(&rules_irq);
                return 0;
        }
        if (rules_job_head - rules_job_tail >= RULES_JOBS)
        {
                rules_count.lost++;
                raw_spin_unlock_irqrestore(&rules_lock, flags);
                return -EBUSY;
        }
        job = rules_jobs + (rules_job_head % RULES_JOBS);
        memory_zero(job, sizeof(*job));
        job->primitive = request->primitive;
        job->number = request->number;
        strscpy(job->name, "asked", sizeof(job->name));
        request->text[sizeof(request->text) - 1] = 0;
        strscpy(job->text, request->text, sizeof(job->text));
        rules_job_head++;
        rules_count.queued++;
        raw_spin_unlock_irqrestore(&rules_lock, flags);
        irq_work_queue(&rules_irq);
        return 0;
}

static long report_rover(struct rover_control __user *out)
{
        struct rover_control request;
        long ret = 0;

        if (copy_from_user(&request, out, sizeof(request)))
                return -EFAULT;
        if (request.flags || request.op >= ROVER_OP_COUNT)
                return -EINVAL;
        if (request.op != ROVER_OP_STATUS && !capable(CAP_SYS_ADMIN))
                return -EPERM;
        if (!rules_map)
                return -ENODEV;
        request.rule = ROVER_NO_RULE;
        request.why[0] = 0;
        switch (request.op)
        {
        case ROVER_OP_LOAD:
                ret = rules_load(&request);
                break;
        case ROVER_OP_UNLOAD:
                ret = rules_unload();
                break;
        case ROVER_OP_DO:
                ret = rules_do_asked(&request);
                break;
        }
        if (ret && ret != -EINVAL)
                return ret;
        rules_status(&request);
        //      The counts are root's: with a key rule loaded, events and hits are keystrokes counted, and the
        //      timings when they came. Anyone may ask which table is live and how many rules it has.
        if (!capable(CAP_SYS_ADMIN))
        {
                request.events = request.hits = request.taken = request.floor = request.queued = request.lost = 0;
                request.posted = request.decide_ns = request.decide_worst = request.decide_count = 0;
                request.hook_ns = request.hook_worst = request.hook_count = 0;
        }
        if (copy_to_user(out, &request, sizeof(request)))
                return -EFAULT;
        return ret;
}

// ---- the map, the poll and the read --------------------------------------------------------

/*
        /dev/spark mapped: the state page to anyone, read only; the ring to
        CAP_SYS_ADMIN. A map can never be made writable (VM_MAYWRITE is taken
        off), so userspace cannot write a state or a record.
*/
static int rules_mmap(struct file *file, struct vm_area_struct *vma)
{
        positive length = vma->vm_end - vma->vm_start;

        (void)file;
        if (!rules_map)
                return -ENODEV;
        if (vma->vm_pgoff || length > PAGE_ALIGN(ROVER_MAP_BYTES))
                return -EINVAL;
        if (vma->vm_flags & (VM_WRITE | VM_EXEC))
                return -EPERM;
        if ((length > ROVER_MAP_RING || PAGE_SIZE > ROVER_MAP_RING) && !capable(CAP_SYS_ADMIN))
                return -EPERM;
        vm_flags_clear(vma, VM_MAYWRITE | VM_MAYEXEC);
        return remap_vmalloc_range(vma, rules_map, 0);
}

//      Readable when the ring has a record past what this descriptor read last: the read answers the head.
static __poll_t rules_poll(struct file *file, p64 *seen, poll_table *wait)
{
        //      The ring's movement is root's as its records are (a key rule's ring moves at each key): a poll by
        //      anyone else never says readable, and its read is refused.
        poll_wait(file, &rules_wait, wait);
        if (!file_ns_capable(file, &init_user_ns, CAP_SYS_ADMIN) || !capable(CAP_SYS_ADMIN))
                return 0;
        return rules_head() != READ_ONCE(*seen) ? EPOLLIN | EPOLLRDNORM : 0;
}

static ssize_t rules_read(struct file *file, p64 *seen, char __user *into, size_t room)
{
        p64 head;

        if (room < sizeof(head))
                return -EINVAL;
        if (!capable(CAP_SYS_ADMIN))
                return -EPERM;
        if (!rules_map)
                return -ENODEV;
        for (;;)
        {
                head = rules_head();
                if (head != READ_ONCE(*seen))
                        break;
                if (file->f_flags & O_NONBLOCK)
                        return -EAGAIN;
                if (wait_event_interruptible(rules_wait, rules_head() != READ_ONCE(*seen)))
                        return -ERESTARTSYS;
        }
        WRITE_ONCE(*seen, head);
        if (copy_to_user(into, &head, sizeof(head)))
                return -EFAULT;
        return sizeof(head);
}

// ---- start and stop ---------------------------------------------------------------------------

static void rules_start(void)
{
        init_waitqueue_head(&rules_wait);
        init_irq_work(&rules_irq, rules_irq_run);
        INIT_WORK(&rules_work, rules_work_run);
        INIT_WORK(&rules_stop_work, rules_stop_run);
        INIT_DELAYED_WORK(&rules_stop_watchdog, rules_stop_force);
        rules_states_find();
        rules_p_poweroff = rules_primitive_id("poweroff");
        rules_p_reboot = rules_primitive_id("reboot");
        rules_p_suspend = rules_primitive_id("suspend");
        rules_p_backlight = rules_primitive_id("backlight");
        rules_p_latency = rules_primitive_id("latency");
        rules_p_unlatency = rules_primitive_id("latency off");
        rules_p_radio_off = rules_primitive_id("radio off");
        rules_p_usb_lock = rules_primitive_id("usb lock");
        rules_p_log = rules_primitive_id("log");
        for (positive event = 1; event <= SPARK_BIND_EVENTS; event++)
                for (positive at = 0; at < BIND_CODES_PER; at++)
                        if (bind_spec[event - 1].code[at] && bind_spec[event - 1].code[at] < KEY_CNT)
                                rules_spark_key[bind_spec[event - 1].code[at]] = (u8)event;

        rules_map = vmalloc_user(ROVER_MAP_BYTES);
        if (!rules_map)
        {
                pr_alert("[rover] no memory for the state page and the ring; the kernel's rules are off\n");
                return;
        }
        rules_page = rules_map + ROVER_MAP_STATE;
        rules_ring = (struct rover_ring *)(rules_map + ROVER_MAP_RING);
        *(p32 *)rules_page = 1;
        rules_ring->magic = ROVER_RING_MAGIC;
        rules_ring->slots = ROVER_RING_SLOTS;
        rules_ring->record = ROVER_RECORD_BYTES;
        rules_started = true;

#if IS_REACHABLE(CONFIG_POWER_SUPPLY)
        power_supply_reg_notifier(&rules_power_nb);
        rules_power_read();
#endif
#if IS_REACHABLE(CONFIG_USB)
        usb_register_notify(&rules_usb_nb);
#endif
        register_netdevice_notifier(&rules_link_nb);
#ifdef CONFIG_PM
        register_pm_notifier(&rules_pm_nb);
#endif
}

static void rules_stop(void)
{
        if (!rules_started)
                return;
#ifdef CONFIG_PM
        unregister_pm_notifier(&rules_pm_nb);
#endif
        unregister_netdevice_notifier(&rules_link_nb);
#if IS_REACHABLE(CONFIG_USB)
        usb_unregister_notify(&rules_usb_nb);
#endif
#if IS_REACHABLE(CONFIG_POWER_SUPPLY)
        power_supply_unreg_notifier(&rules_power_nb);
#endif
        rules_unload();
        irq_work_sync(&rules_irq);
        cancel_work_sync(&rules_work);
        cancel_work_sync(&rules_stop_work);
        cancel_delayed_work_sync(&rules_stop_watchdog);
        if (cpu_latency_qos_request_active(&rules_qos))
                cpu_latency_qos_remove_request(&rules_qos);
        rcu_barrier();
        vfree(rules_map);
        rules_map = null;
        rules_started = false;
}

#endif /* STANDARD_MODERN_C_KERNEL */

#endif /* MOONWATER_RULES_INCLUDED */
