/*
        A present: the metrics the table is told, the loop's tick and its sources, and one frame from the damage to the
        screen (compositor_present), which picks the way to the screen (a window shown straight, the overlay, the card,
        the processor in place or apart), lists the frames (a client's layers among them), composes, and flips or tells
        the card what changed; and the present the card draws without a wait (compositor_card_*): its jobs' fence the
        loop polls, the flip committed with it, the clients held until the jobs are done, and the frame given up and
        drawn again when one fails.

        It is the one caller that ties the parts before it together, so it comes after all of them: compose.c's scene,
        damage.c's list and frames, glass.c's pass, scanout.c's ways to the screen and gpu_host.c's present. It makes no
        buffer (scanout.c and kms.c do) and reads no input. Called by kms.c (a take presents the new space at once),
        scanout.c (the lead of a present) and compositor.c's loop.
*/

/*
        The damage, composed straight onto the scanout and then announced.
        The scanout is a mapping of the card's memory, so it is only ever
        written: every pixel of the damage once, by the stores of
        memory_fill_u32_rect and memory_copy (full 64-byte lines where a row
        is long enough, a masked store or two overlapping ones at the ends),
        none non-temporal and none a read. The card is then told the
        rectangles that changed, in one call: a virtual card copies nothing
        to its host without that.

        The windows go bottom to top, and the tabs over all of them.
*/
static void compositor_present_frames(void);

// A present: a dma-buf a window shows is read by the card or the processor, and its reads are bracketed by the kernel's sync for the
// processor's (compositor_wl_lent_sync: the bracket for each window's dma-buf picture read in place, as a copy of one is).
/*
        The presents' time, on the card and on the processor: each present's whole time (compositor_present), by the path it took, in rings of
        the last WINDOWS_METRIC_RUN presents of each (nanoseconds). The table gets the means, the worst and the counts after each present; the
        rates of presents and of list frames each second. The card's own numbers (maps, wakes, jobs) are gpu.c's, read here.
*/
static unsigned long long compositor_metric_card[WINDOWS_METRIC_RUN], compositor_metric_processor[WINDOWS_METRIC_RUN];
static unsigned int compositor_metric_card_at, compositor_metric_card_count, compositor_metric_processor_at, compositor_metric_processor_count;
static unsigned long long compositor_metric_presents, compositor_metric_since, compositor_metric_presents_then, compositor_metric_lists_then;
static unsigned int compositor_metric_presents_second, compositor_metric_lists_second;
/*
        The present's timing, a second at a time (moonwater debug's presents line): the presents made late in their period and those of them that missed
        their vblank (compositor_late's counts at the last mark), the loop's longest time inside one present since the mark (compositor_present), and the
        flips committed while a job of the card still ran, with the most jobs in flight at one of them since the mark (the card's present without a wait),
        and the loop's time inside the card's waits since the mark (every job gpu_run waited for, gpu.c's gpu_job_ns: a list's render, a client's copy, a
        wake, a scale or a present waited for in place), in microseconds a second.
*/
static unsigned long long compositor_metric_late_then, compositor_metric_missed_then, compositor_metric_loop_most, compositor_metric_flips_jobs,
    compositor_metric_flips_jobs_then, compositor_metric_waited_then;
static unsigned int compositor_metric_flip_jobs_most, compositor_metric_late_second, compositor_metric_missed_second, compositor_metric_loop_most_second,
    compositor_metric_flip_jobs_second, compositor_metric_flip_jobs_most_second, compositor_metric_waited_second;

static void compositor_metric_publish(void)
{
        struct windows address_to table = compositor.table;
        unsigned long long now = clock_monotonic_nanoseconds(), lists = compositor_list_counts[COMPOSITOR_LIST_DRAWN], waited = 0;
        unsigned int mean, worst;

        if (!table)
                return;
        windows_metric_stats(compositor_metric_card, compositor_metric_card_count, &mean, &worst);
        table->metric_card_mean = mean, table->metric_card_worst = worst;
        windows_metric_stats(compositor_metric_processor, compositor_metric_processor_count, &mean, &worst);
        table->metric_processor_mean = mean, table->metric_processor_worst = worst;
#ifdef MOONWATER_GPU_C
        table->metric_maps = gpu_map_count, table->metric_map_hits = gpu_map_hits, table->metric_map_misses = gpu_map_misses;
        // The compositor's own memory the dma-buf route refused, mapped by userptr instead, and the errno of the last refusal (gpu.c's).
        table->metric_maps_own_user = gpu_maps_own_user, table->metric_maps_own_errno = gpu_maps_own_errno;
        table->metric_wakes = gpu_wakes, table->metric_jobs = gpu_jobs;
        table->metric_job_mean = gpu_jobs ? (unsigned int)(gpu_job_ns / gpu_jobs / 1000) : 0;
        waited = gpu_job_ns;
        // The card's jobs since the start by kind (gpu.c's GPU_JOB_KIND: a scene, a present's or a list's; a copy of a client's damage; a picture
        // scaled), each with the time the loop waited for the waited ones; and the jobs waited for against those submitted without a wait, with the
        // waited ones' time in the submit and from there to their end.
        table->metric_gpu_jobs_scene = gpu_jobs_kind[0], table->metric_gpu_job_ns_scene = gpu_job_ns_kind[0];
        table->metric_gpu_jobs_copy = gpu_jobs_kind[1], table->metric_gpu_job_ns_copy = gpu_job_ns_kind[1];
        table->metric_gpu_jobs_scale = gpu_jobs_kind[2], table->metric_gpu_job_ns_scale = gpu_job_ns_kind[2];
        table->metric_jobs_waited = gpu_jobs_waited, table->metric_jobs_unwaited = gpu_jobs_unwaited;
        table->metric_job_submit_ns = gpu_job_submit_ns, table->metric_job_wait_ns = gpu_job_wait_ns;
#endif
        if (!compositor_metric_since)
                compositor_metric_since = now, compositor_metric_presents_then = compositor_metric_presents, compositor_metric_lists_then = lists,
                compositor_metric_late_then = compositor_late.made, compositor_metric_missed_then = compositor_late.missed,
                compositor_metric_flips_jobs_then = compositor_metric_flips_jobs, compositor_metric_waited_then = waited;
        if (now - compositor_metric_since >= 1000000000ull)
        {
                compositor_metric_presents_second = windows_metric_rate(compositor_metric_presents - compositor_metric_presents_then, now - compositor_metric_since);
                compositor_metric_lists_second = windows_metric_rate(lists - compositor_metric_lists_then, now - compositor_metric_since);
                compositor_metric_late_second = windows_metric_rate(compositor_late.made - compositor_metric_late_then, now - compositor_metric_since);
                compositor_metric_missed_second = windows_metric_rate(compositor_late.missed - compositor_metric_missed_then, now - compositor_metric_since);
                compositor_metric_flip_jobs_second = windows_metric_rate(compositor_metric_flips_jobs - compositor_metric_flips_jobs_then, now - compositor_metric_since);
                compositor_metric_loop_most_second = (unsigned int)(compositor_metric_loop_most / 1000);
                compositor_metric_flip_jobs_most_second = compositor_metric_flip_jobs_most;
                compositor_metric_waited_second = windows_metric_rate((waited - compositor_metric_waited_then) / 1000, now - compositor_metric_since);
                compositor_metric_waited_then = waited;
                compositor_metric_late_then = compositor_late.made, compositor_metric_missed_then = compositor_late.missed;
                compositor_metric_flips_jobs_then = compositor_metric_flips_jobs, compositor_metric_loop_most = 0, compositor_metric_flip_jobs_most = 0;
                compositor_metric_since = now, compositor_metric_presents_then = compositor_metric_presents, compositor_metric_lists_then = lists;
                // The kernel's notices, once a second as the metrics are (window.c's kernel_notices_tick reads /dev/kmsg without waiting).
                kernel_notices_tick(table, now);
        }
        table->metric_presents = compositor_metric_presents_second, table->metric_lists = compositor_metric_lists_second;
        table->metric_late = compositor_metric_late_second, table->metric_missed = compositor_metric_missed_second;
        table->metric_lead = (unsigned int)(compositor_present_lead() / 1000), table->metric_present_most = compositor_metric_loop_most_second;
        table->metric_flip_jobs = compositor_metric_flip_jobs_second, table->metric_flip_jobs_most = compositor_metric_flip_jobs_most_second;
        table->metric_waited = compositor_metric_waited_second;
}

// The loop's turns a second, and those whose ppoll had nothing to wait for (a timer already due, so the turn spun): the wake rate in moonwater debug.
static unsigned long long compositor_loop_turns, compositor_loop_idle, compositor_loop_since, compositor_loop_then, compositor_loop_idle_then;

static unsigned int compositor_loop_by[WINDOWS_LOOP_SOURCES], compositor_loop_by_second[WINDOWS_LOOP_SOURCES];

// Where each part of the loop's poll list is (compositor_loop_wanted builds it): 0 for a part that is not in it this turn.
struct compositor_slots
{
        unsigned int count, inputs, doors, done_at, card_at, fence_at, uevents_at, clip_at, wayland_at;
};

// The source of a poll slot that returned events (the same order compositor_main builds the slots in). The card's job's fence is the card's.
static unsigned int compositor_loop_source(unsigned int at, unsigned int doors, unsigned int done_at, unsigned int card_at, unsigned int fence_at,
                                           unsigned int uevents_at, unsigned int clip_at, unsigned int wayland_at)
{
        if (at == 0)
                return 0;
        if (at == 1)
                return 1;
        if (at < doors)
                return 2;
        if (at < doors + compositor_rows_in_use)
                return 3;
        if (done_at && at == done_at)
                return 4;
        if ((card_at && at == card_at) || (fence_at && at == fence_at))
                return 5;
        if (uevents_at && at == uevents_at)
                return 6;
        if (clip_at && at == clip_at)
                return 7;
        if (wayland_at && at >= wayland_at)
                return 8;
        return 9;
}

static void compositor_loop_tick(unsigned long long now, b32 idle)
{
        struct windows address_to table = compositor.table;

        compositor_loop_turns++, compositor_loop_idle += idle;
        if (!compositor_loop_since)
                compositor_loop_since = now, compositor_loop_then = compositor_loop_turns, compositor_loop_idle_then = compositor_loop_idle;
        if (now - compositor_loop_since >= 1000000000ull)
        {
                if (table)
                        table->metric_loops = windows_metric_rate(compositor_loop_turns - compositor_loop_then, now - compositor_loop_since),
                        table->metric_loop_idle = windows_metric_rate(compositor_loop_idle - compositor_loop_idle_then, now - compositor_loop_since);
                for (unsigned int k = 0; table && k < WINDOWS_LOOP_SOURCES; k++)
                        table->metric_loop_by[k] = windows_metric_rate(compositor_loop_by[k] - compositor_loop_by_second[k], now - compositor_loop_since),
                        compositor_loop_by_second[k] = compositor_loop_by[k];
                compositor_loop_since = now, compositor_loop_then = compositor_loop_turns, compositor_loop_idle_then = compositor_loop_idle;
        }
}

// The paths of the last COMPOSITOR_LEAD_FRAMES presents noted, the newest in bit 0 (1: the card's), and how many there are of them: the lead is
// measured on the presents as they were, the card's and the processor's (compositor_present_lead).
static unsigned int compositor_metric_paths, compositor_metric_paths_count;

static void compositor_metric_note(unsigned long long nanoseconds)
{
        compositor_metric_paths = compositor_metric_paths << 1 | (compositor_present_on_card ? 1u : 0u);
        compositor_metric_paths_count += compositor_metric_paths_count < COMPOSITOR_LEAD_FRAMES;
        if (compositor_present_on_card)
                compositor_metric_card[compositor_metric_card_at] = nanoseconds, compositor_metric_card_at = (compositor_metric_card_at + 1) % WINDOWS_METRIC_RUN,
                compositor_metric_card_count++;
        else
                compositor_metric_processor[compositor_metric_processor_at] = nanoseconds,
                compositor_metric_processor_at = (compositor_metric_processor_at + 1) % WINDOWS_METRIC_RUN, compositor_metric_processor_count++;
        compositor_metric_presents++;
        if ((compositor_metric_presents & 15) == 1)
                compositor_metric_publish();
}

// The worst of the last `last` times of a ring (at: its next slot; count: the times it was given, of which it holds WINDOWS_METRIC_RUN at the most).
static positive compositor_metric_worst(const unsigned long long *ring, unsigned int at, unsigned int count, unsigned int last)
{
        positive worst = 0;

        for (unsigned int back = 1; back <= last && back <= count && back <= WINDOWS_METRIC_RUN; back++)
        {
                positive time = ring[(at + WINDOWS_METRIC_RUN - back) % WINDOWS_METRIC_RUN];

                worst = time > worst ? time : worst;
        }
        return worst;
}

/*
        The lead of the next present (compositor_present_due): the worst of the last COMPOSITOR_LEAD_FRAMES presents, each on the path it took (so many of
        the card's ring and so many of the processor's as there were of each among them): on one path that path's worst, and where the card and the processor
        take turns the worse of the two, so that a card's short presents do not set the time for the processor's long one that comes next and misses. 0
        headless, or where the owner asked for none.
*/
static positive compositor_present_lead(void)
{
        unsigned int cards = 0, processors;
        positive worst, worst_processor;

        if (compositor_late.early || compositor.card < 0)
                return 0;
        for (unsigned int back = 0; back < compositor_metric_paths_count; back++)
                cards += compositor_metric_paths >> back & 1u;
        processors = compositor_metric_paths_count - cards;
        worst = compositor_metric_worst(compositor_metric_card, compositor_metric_card_at, compositor_metric_card_count, cards);
        worst_processor = compositor_metric_worst(compositor_metric_processor, compositor_metric_processor_at, compositor_metric_processor_count, processors);
        worst = worst_processor > worst ? worst_processor : worst;
        return compositor_lead_of(worst, compositor_metric_paths_count, compositor_late.widen, compositor.period);
}

/*
        A present's end, once its pixels are in compositor.fb: the screens told what changed (one DIRTYFB, or each screen's), the screens Canvas
        turns given their part (compositor_turn_frame), the overlay taken off where the frame is the compositor's own screen again, and the frame
        shown: the plate composed into flipped to (flip_own), or the compositor's own screen flipped back to (back), with what a refused flip does.
        At the present for one the processor drew; when its jobs are done for one the card drew (compositor_card_done).
*/
static void compositor_present_tell(const unsigned short (*clip)[4], unsigned int clips, int turning, int gpu_composed, int flip_own, int back, int plane_off)
{
        // No clips at all would tell the card everything changed. A headless compositor has no card to tell (its scan image is a file).
        if (clips && compositor_output_count() < 2 && !turning)
                compositor_dirty(compositor.fb, clip, clips);
        else if (clips && compositor.card >= 0)
                compositor_dirty_screens(clip, clips);
        // The screens Canvas turns: their part of the scene, turned into their own buffers and told to their cards.
        if (clips && turning)
                compositor_turn_frame(clip, clips, gpu_composed);

        // Back on the compositor's own screen: what was held for the window is said taken when the card is there.
        if ((back || plane_off) && compositor.overlay_on && !compositor_atomic(compositor.fb, -1, 0, 1, 0, 0))
                compositor_overlay_off();
        if (flip_own && compositor.on_screen != compositor.fb && !compositor_flip(compositor.fb, -1, 0))
        {
                compositor_record_refuse();
                if (compositor.card < 0)
                {
                        // The scan image was not exchanged: the composed frame is not shown, so the buffers are both whole again from the next present.
                        compositor_plate_swap();
                        compositor_stale_all(compositor_front_stale, &compositor_front_stale_any, &compositor_front_parts);
                        compositor_stale_all(compositor_spare.stale, &compositor_spare.stale_any, &compositor_spare_parts);
                }
                else if (++compositor_flip_refused >= 3)
                {
                        // Refused three presents running: the crtc is set to the buffer composed into, which shows it at once (a modeset, so it tears).
                        struct compositor_crtc set = {(unsigned long)address_of compositor.connector, 1, compositor.crtc, compositor.fb,
                                                      0, 0, 0, 1, compositor.mode};

                        system_control(compositor.card, COMPOSITOR_SETCRTC, address_of set);
                        compositor.on_screen = compositor.fb;
                        compositor_flip_refused = 0;
                        compositor_flip_done();
                }
                else
                {
                        // Refused once (the card is still on the flip before, EBUSY): the screen keeps the buffer it shows and the frame is
                        // composed again into the other from the next present. Each buffer's stale holds what it misses (the damage of this
                        // present is in the one the screen shows), so nothing is lost; a modeset on every refusal would flicker the screen.
                        compositor_plate_swap();
                }
        }
        else if (back && compositor.on_screen != compositor.fb && !compositor_flip(compositor.fb, -1, 0))
        {
                compositor_record_refuse();
                struct compositor_crtc set = {(unsigned long)address_of compositor.connector, 1, compositor.crtc, compositor.fb,
                                              0, 0, 0, 1, compositor.mode};

                system_control(compositor.card, COMPOSITOR_SETCRTC, address_of set);
                compositor.on_screen = compositor.fb;
                compositor_flip_done();
        }
}

#ifdef CONFIG_MOONWATER_WAYLAND
static void compositor_wl_presented(void); // wayland_host.c: a drawn frame's feedbacks, answered (or held for its flip)
static void compositor_wl_frame(void);     // and its callbacks, and the buffers its pictures replaced given back
#endif

#ifdef MOONWATER_GPU_C
static void compositor_damage_space(void);

/*
        The flip of the plate composed into, committed with the sync_file of the job that draws it as the primary plane's IN_FENCE_FD (an atomic
        commit that does not wait, with the flip's event): the kernel flips once the card is done. The property is looked for once, on the primary
        plane of this crtc (compositor_overlay_ready finds it and asks for commits); a card with none, or one that refuses such a commit for any
        reason but a flip still in flight (EBUSY), is flipped the legacy way once its jobs are done (compositor_card_done), from then on. False
        when it was not committed.
*/
static int compositor_fence_prop; // the primary plane's IN_FENCE_FD (0: not looked for yet; -1: none, or refused)
static int compositor_fence_errno; // the errno the card refused a fenced commit with, which turned the fence off (0: none)

static unsigned int compositor_in_fence(void)
{
        if (!compositor_fence_prop)
        {
                struct compositor_enum none[1];
                unsigned int id = 0;

                compositor_fence_prop = -1;
                if (compositor.card >= 0)
                        compositor_overlay_ready();
                if (compositor.card >= 0 && compositor.primary && compositor.primary_props[COMPOSITOR_PLANE_FB])
                        compositor_enums_of(compositor.primary, COMPOSITOR_OBJECT_PLANE, "IN_FENCE_FD", &id, none, 1);
                if (id)
                        compositor_fence_prop = (int)id;
        }
        return compositor_fence_prop > 0 ? (unsigned int)compositor_fence_prop : 0;
}

static int compositor_flip_fenced(unsigned int fb, int fence)
{
        unsigned int prop = compositor_in_fence(), objects[1] = {compositor.primary}, counts[1] = {2}, props[2];
        unsigned long values[2] = {fb, (unsigned long)(long)fence};
        struct
        {
                unsigned int flags, count;
                unsigned long objects, counts, props, values, reserved, user_data;
        } commit = {COMPOSITOR_FLIP_EVENT | COMPOSITOR_ATOMIC_NONBLOCK, 1, (unsigned long)objects, (unsigned long)counts, (unsigned long)props, (unsigned long)values, 0, 0};
        long refused;

        if (!prop || fence < 0 || compositor.card < 0)
                return 0;
        props[0] = compositor.primary_props[COMPOSITOR_PLANE_FB], props[1] = prop;
        commit.user_data = compositor.flip_serial + 1;
        if ((refused = system_control(compositor.card, COMPOSITOR_ATOMIC, address_of commit)) != 0)
        {
                if (refused != -16) // not a flip still in flight: no fenced commit on this card
                        compositor_fence_prop = -1, compositor_fence_errno = (int)-refused;
                return 0;
        }
        compositor.flip_serial++;
        compositor_flip_refused = 0;
        compositor.flip_pending = 1;
        compositor.flip_issued = clock_monotonic_nanoseconds();
        compositor.flip_row = -1;
        compositor.flip_buffer = 0;
        compositor.on_screen = fb;
        {
                // The jobs not yet seen done as it was committed (moonwater debug's flips with jobs in flight).
                unsigned int jobs = (gpu.pending != 0) + (gpu_before.sequence != 0);

                compositor_metric_flips_jobs += jobs != 0;
                compositor_metric_flip_jobs_most = jobs > compositor_metric_flip_jobs_most ? jobs : compositor_metric_flip_jobs_most;
        }
        return 1;
}

// Whether a present is held for the card: a list's job not waited for draws a picture the present reads (whether the present waits or not); and
// without a wait (present-nowait), a present's jobs on it, or a job still pending with its end not said (one given up on as late). With one the waits
// are in place, as they were.
static b32 compositor_card_holds(void)
{
        if (rows_any(&compositor_card.list_rows))
                return true;
        if (!compositor_card.nowait)
                return false;
        return compositor_card.on || (gpu.pending && !gpu_end_said && compositor_gpu_state.up && !gpu_idle());
}

// The windows held for the present's jobs (their door's "taken": compositor_card_reads) told their picture was taken, as compositor_flip_done tells
// the ones held for a flip; a row held for a flip still in flight (the one shown straight) is left to it.
static void compositor_card_release(void)
{
        for (int row = 0; row < (int)compositor_rows_in_use && compositor.table; row++)
                if ((rows_has(&compositor.held_rows, row) && rows_has(&compositor_card.rows, row)) && !(compositor.flip_pending && row == compositor.flip_row))
                {
                        __atomic_store_n(&compositor.table->shown[row], compositor.taken_held[row], __ATOMIC_RELEASE);
                        rows_del(&compositor.held_rows, row);
                        rows_add(&compositor.woken, row);
                }
        rows_none(&compositor_card.rows);
}

// When the clients' hold ends: GPU_LATE_NS after the present whose jobs are on the card began, or after the first list's job not waited for did.
static positive compositor_card_held_until(void)
{
        return (compositor_card.on ? compositor_card.began : compositor_card.list_began) + GPU_LATE_NS;
}

// Whether the loop reads no client this turn: a present's jobs are on the card, or a list's not waited for, and it began less than GPU_LATE_NS ago
// (compositor_card_held_until; a list's holds them so that no other list asks for the card meanwhile). Past that the clients are read again (a client
// not read for a hung job's two seconds is ended by its toolkit's watchdog, its pings unanswered), and what the jobs read is kept from them by
// compositor_card_reads (the doors' taken, the shim's releases) until they are done or the hang path ends them. The input devices are read whatever
// this says.
static b32 compositor_card_clients_held(positive now)
{
        return (compositor_card.on || rows_any(&compositor_card.list_rows)) && now < compositor_card_held_until();
}

// The present whose jobs are on the card began at `began`: its time is noted when they are done, and the clients' hold is counted from it.
static void compositor_card_began(positive began)
{
        compositor_card.began = began;
}

// The present's frame given up (a job of it refused, hung or its fence an error): not shown, and drawn again whole by the next present.
static void compositor_card_abandon(void)
{
        compositor_card_fence_close();
        compositor_card.on = 0;
        compositor_card_release();
        if (compositor_card.flip_own && compositor_spare.fb)
        {
                if (compositor.on_screen == compositor.fb) // committed: the kernel shows it once the card is done with it, whatever is in it
                        compositor_stale_all(compositor_front_stale, &compositor_front_stale_any, &compositor_front_parts);
                else // not shown: the screen keeps the buffer it shows, and the other is to be composed whole
                        compositor_plate_swap(), compositor_stale_all(compositor_spare.stale, &compositor_spare.stale_any, &compositor_spare_parts);
        }
        compositor_damage_space();
#ifdef CONFIG_MOONWATER_WAYLAND
        compositor_wl_frame(); // the callbacks the frame was to answer are answered: the clients draw on
#endif
}

// The present's jobs are done: its end (compositor_present_tell, the flip a fenced commit made already left as it is), its time as the card's present,
// and what its clients are told of the frame, held till now: no job reads their pictures any more.
static void compositor_card_done(void)
{
        compositor_card_fence_close();
        compositor_card.on = 0;
        if (compositor_card.failed)
        {
                compositor_card_abandon();
                return;
        }
        compositor_card_release();
        compositor_present_tell((const unsigned short (*)[4])compositor_card.clip, compositor_card.clips, compositor_card.turning, compositor_card.composed,
                                compositor_card.flip_own, compositor_card.back, compositor_card.plane_off);
        compositor_present_on_card = 1;
        compositor_metric_note(clock_monotonic_nanoseconds() - compositor_card.began);
#ifdef CONFIG_MOONWATER_WAYLAND
        compositor_wl_presented();
        compositor_wl_frame();
#endif
}

// The flip committed with the last job's sync_file, where the present ends in one and its jobs are all submitted.
static void compositor_card_commit(void)
{
        int fence;

        if (compositor_card.fenced || compositor_card.failed || compositor_card.next < compositor_card.steps || !compositor_card.flip_own || compositor_card.turning ||
            ((compositor_card.back || compositor_card.plane_off) && compositor.overlay_on) || compositor.card < 0 || compositor.on_screen == compositor.fb ||
            compositor.flip_pending || !compositor_in_fence())
                return;
        fence = compositor_card_fence_for(compositor_card.last);
        compositor_card.fenced = fence >= 0 && compositor_flip_fenced(compositor.fb, fence);
}

/*
        How the first present the card draws without a wait reaches the screen, said once in the kernel's log (dmesg | grep compositor), since moonwater
        debug's flips with jobs in flight count only the flips committed with a job's fence: that one, or the legacy flip at the fence and why (no IN_FENCE_FD
        on the primary plane, or the errno it was refused with), or no flip at all (composed in place: a window on the overlay, more than one screen, a turned
        screen, a card that copies its damage).
*/
static void compositor_kmsg(string_address text, positive value, string_address tail);
static void compositor_card_said(void)
{
        static int said;

        if (said++)
                return;
        if (compositor_card.fenced)
                compositor_kmsg((string_address) "compositor: present without a wait: its flip is committed with the job's fence (IN_FENCE_FD)", 0, (string_address) "");
        else if (!compositor_card.flip_own)
                compositor_kmsg((string_address) "compositor: present without a wait: composed in place, no flip, for ", 0,
                                (string_address)(compositor.plane_row >= 0 ? "a window on the overlay" : compositor_output_count() > 1 ? "more than one screen" :
                                                 compositor_card.turning ? "a screen turned" : compositor_copies_damage() ? "a card that copies its damage" :
                                                 "want of a second plate"));
        else if (compositor_fence_prop < 0 && compositor_fence_errno)
                compositor_kmsg((string_address) "compositor: present without a wait: legacy flip at its fence, IN_FENCE_FD refused, error ",
                                (positive)compositor_fence_errno, (string_address) "");
        else if (compositor_fence_prop < 0)
                compositor_kmsg((string_address) "compositor: present without a wait: legacy flip at its fence, no IN_FENCE_FD", 0,
                                (string_address) " on the primary plane");
        else
                compositor_kmsg((string_address) "compositor: present without a wait: legacy flip at its fence (a flip in flight)", 0, (string_address) "");
}

// A present the card draws, at its end in compositor_present_frames: the rows whose pictures it reads, how it ends kept for when its jobs are done, and
// its flip committed now where it can be.
static void compositor_card_after(const unsigned short (*clip)[4], unsigned int clips, int turning, int gpu_composed, int flip_own, int back, int plane_off,
                                  const unsigned int *rows, unsigned int windows)
{
        rows_none(&compositor_card.rows);
        for (unsigned int at = 0; at < windows; at++)
                if (rows[at] < COMPOSITOR_ROWS)
                        rows_add(&compositor_card.rows, rows[at]);
        memory_copy(compositor_card.clip, clip, (positive)clips * sizeof(clip[0]));
        compositor_card.clips = clips, compositor_card.turning = turning, compositor_card.composed = gpu_composed;
        compositor_card.flip_own = flip_own, compositor_card.back = back, compositor_card.plane_off = plane_off;
        compositor_card_commit();
        compositor_card_said();
}

/*
        The rows whose list's job is over given back: the door's taken held for the job (compositor_take, while compositor_card_reads said the row was the
        job's) said, as compositor_card_release says a present's. A job the hang path ended (hung), whose fence answered an error, or that a restart of the
        card took with it is no drawn frame: its list is refused (list_state 2, so the program paints that frame in pixels, whole, and lists again after it)
        and the window drawn again. Nothing waits for a job that will not end: its row is given back at its deadline.
*/
static void compositor_card_lists_settle(b32 hung)
{
        hung |= gpu_recovered != compositor_card.list_recovered;
        for (int row = 0; row < (int)compositor_rows_in_use && rows_any(&compositor_card.list_rows); row++)
        {
                int done;

                if (!rows_has(&compositor_card.list_rows, row))
                        continue;
                done = hung ? -1 : gpu_done(compositor_card.list_seq[row]);
                if (done == 0)
                        continue;
                rows_del(&compositor_card.list_rows, row), compositor_card.list_seq[row] = 0;
                if (!compositor.table)
                        continue;
                if (rows_has(&compositor.held_rows, row) && !(compositor.flip_pending && row == compositor.flip_row) &&
                    !(compositor_card.on && rows_has(&compositor_card.rows, row)))
                {
                        __atomic_store_n(&compositor.table->shown[row], compositor.taken_held[row], __ATOMIC_RELEASE);
                        rows_del(&compositor.held_rows, row);
                }
                rows_add(&compositor.woken, row);
                if (done < 0)
                {
                        __atomic_store_n(&compositor.table->list_state[row], 2u, __ATOMIC_RELEASE);
                        if (compositor.showing[row])
                                compositor_damage(compositor.x[row], compositor.y[row], (int)compositor.w[row], (int)compositor.h[row]);
                }
        }
}

/*
        The loop's turn with a job on the card: a deadline passed or a fence's error is the hang path, and the frame is drawn again; the next jobs of the present
        submitted as their turn comes, and its flip committed once the last is; and once every job is done, the present's end. A job pending with no present in
        flight (a list's given up on as late) is let go of when it is done, or its end said, and a list's drawn without a wait gives its row back then
        (compositor_card_lists_settle). Nothing here waits.
*/
static void compositor_card_tick(void)
{
        if (!compositor_card.on && !gpu.pending)
        {
                compositor_card_lists_settle(false); // no job pending: every list's done, or the card started again
                compositor_card_fence_close();
                return;
        }
        if (gpu_late_end() != GPU_END_NONE) // past its deadline, a page fault in the kernel's log, or its fence an error: the hang path
        {
                compositor_gpu_job_failed();
                compositor_card_fence_close();
                compositor_card_lists_settle(true);
                if (compositor_card.on)
                        compositor_card_abandon();
                return;
        }
        compositor_card_lists_settle(false);
        if (compositor_card.on && compositor_card.next < compositor_card.steps)
        {
                if (compositor_card_go() < 0)
                {
                        compositor_gpu_job_failed();
                        compositor_card.failed = 1;
                }
                compositor_card_commit();
        }
        if (compositor_card.on && compositor_card.next == compositor_card.steps && gpu_done(compositor_card.last) != 0)
                compositor_card_done();
        else if (!compositor_card.on && gpu_idle())
                compositor_card_fence_close();
}

// The descriptor the loop polls for the card (-1 for none: then it looks again in a millisecond, compositor_card_due), and when it is next to look at
// the card whatever the descriptor says: the oldest pending job's deadline, or the next look for a fault in the kernel's log.
static int compositor_card_poll(void)
{
        return compositor_card_fence_for(compositor_card_awaited());
}

static positive compositor_card_due(positive now)
{
        positive began = gpu_before.sequence ? gpu_before.began : gpu_job_began, due = began + GPU_HANG_NS;

        if (!compositor_card.fence)
                due = now + 1000000ull < due ? now + 1000000ull : due;
        due = gpu_fault_poll && gpu_fault_poll < due ? gpu_fault_poll : due;
        return due > now ? due : now + 1000000ull; // a time gone by is looked at a millisecond on, not in a loop that spins
}

// Whether the loop waits on the card this turn: a list's job not waited for, or without a wait a present's jobs or a job still pending.
static b32 compositor_card_awaits(void)
{
        return rows_any(&compositor_card.list_rows) || (compositor_card.nowait && (compositor_card.on || (gpu.pending && !gpu_end_said)));
}
#else
static b32 compositor_card_holds(void)
{
        return false;
}
static b32 compositor_card_clients_held(positive now)
{
        (void)now;
        return false;
}
static void compositor_card_began(positive began)
{
        (void)began;
}
#endif

static void compositor_present(void)
{
        unsigned long long began = clock_monotonic_nanoseconds(), ended;
        enum compositor_record_path path;
        b32 jobs_before = compositor_card.on; // a present made while one's jobs are on the card draws nothing (compositor_card_holds)

        compositor_record_begin();
        compositor_present_on_card = 0;
        compositor_present_frames();
        compositor_wl_lent_sync(0);
        ended = clock_monotonic_nanoseconds();
        // The loop's own time in a present, the longest of each second (what a present costs the loop, its jobs on the card or not).
        compositor_metric_loop_most = ended - began > compositor_metric_loop_most ? ended - began : compositor_metric_loop_most;
        // A present whose jobs are on the card is timed when they are done (compositor_card_done): its time is the card's too, which the lead is measured on.
        if (compositor_card.on && !jobs_before)
                compositor_card_began(began);
        else if (!compositor_card.on)
                compositor_metric_note(ended - began);
        path = compositor_present_on_card ? COMPOSITOR_RECORD_CARD : compositor.plane_row >= 0 ? COMPOSITOR_RECORD_PLANE : COMPOSITOR_RECORD_PROCESSOR;
        compositor_record_present(began, ended, path);
}

/*
        The wallpaper kept for the space (compositor_wall): made again only when the space's size, the screens, the theme, the accent or
        a row of `canvas wallpaper` changed. It is made on a thread of its own (compositor_wall_run), never on the loop: until it is
        done the screen keeps what it showed (the last wallpaper, or the colour), and when it is the loop takes it at its next pass
        (compositor_wall_keep), lets the old one go and damages the space once. A change while one is being made waits for it and is
        made after. With no thread to be had it is made on the loop, as it was. Each screen has its own moon over its sky, in the dark
        theme dimmed (the moon as it is, its seas `canvas wallpaper maria` of its highlands) or inverted (chalk on charcoal), in the light
        one ink on paper, with the accent's glow round it and along the edges as `canvas wallpaper glow` says.
*/
static struct
{
        int moon, dimmed, maria, glow; // the rows: the moon or plain, dimmed or inverted, the seas' grey (percent), none, glow or edges too
        int ready;                     // the rows were read
} compositor_wall_rows = {1, 1, 45, 1, 0};

#define COMPOSITOR_WALL_SCREENS 8

static struct compositor_wall_job
{
        thread *worker;          // the thread making it, or null
        int done;                // the thread has made it (written last, with release)
        unsigned int *pixels;    // what it makes into, as large as the space
        positive bytes;
        int width, height, count, dark, dimmed, maria, glow;
        unsigned int accent;
        int screens[COMPOSITOR_WALL_SCREENS][4];
        unsigned int key[8];     // what it is made from
        positive began, took;    // when it was begun, and how long it took (ns)
        b64 turn;                // the moon's turn it is made at
        int step;                // a step of the turn: the wallpaper as it was with each moon's square made again, and those squares its damage
        const unsigned int *from; // a step's: the wallpaper shown, which it starts from
        int squares[COMPOSITOR_WALL_SCREENS][4]; // each screen's moon's square, in the space
} compositor_wall_job;

/*
        The wallpaper's moon turns while the motion is on and a hand is about (taskbar_motion_about, as the taskbar's moon: it rests with
        reduced motion, with the motion off and two minutes after the last key or move), slowly: a turn in COMPOSITOR_WALL_TURN_STEPS steps
        of COMPOSITOR_WALL_STEP each (a fifth of a degree, three pixels at the middle of a 1080 row moon), on the real clock. A step is made
        on the wallpaper's thread as a whole wallpaper is: a copy of the one shown with each moon's square made again (its rows' sky, then
        the moon at the new turn), and taken with those squares alone its damage. At rest nothing is asked: no step, no wake.
*/
#define COMPOSITOR_WALL_STEP (10ull * 1000000000ull)
#define COMPOSITOR_WALL_TURN_STEPS 2160
static b64 compositor_wall_turn;       // the turn the wallpaper shown is at
static positive compositor_wall_stepped; // the step of the real clock it was made for

static void compositor_kmsg(string_address text, positive value, string_address tail);

/*
        What became of the wallpaper, in the kernel's log (dmesg): "compositor: wallpaper moon 1920x1080 made in 850 ms" or
        "compositor: wallpaper plain because ...", once for each change, so a screen with no wallpaper says why.
*/
static void compositor_wall_say(const char *why, int width, int height, positive ms)
{
        char line[120];
        positive at = 0;

        if (!why)
        {
                string_copy((string_address)line, (string_address) "compositor: wallpaper moon ");
                at = string_length((string_address)line);
                at += positive_into_string((p8 *)line + at, (positive)width);
                line[at++] = 'x';
                at += positive_into_string((p8 *)line + at, (positive)height);
                string_copy((string_address)line + at, (string_address) " made in ");
                at = string_length((string_address)line);
                at += positive_into_string((p8 *)line + at, ms);
                string_copy((string_address)line + at, (string_address) " ms");
        }
        else
        {
                string_copy((string_address)line, (string_address) "compositor: wallpaper plain because ");
                string_copy((string_address)line + string_length((string_address)line), (string_address)why);
        }
        compositor_kmsg((string_address)line, 0, (string_address) "");
}

static unsigned int compositor_wall_key[8]; // what the wallpaper shown was made from
static positive compositor_wall_bytes;

// Make the job's wallpaper: each screen's in its rectangle of the space.
static void compositor_wall_make(struct compositor_wall_job *job)
{
        if (job->step && job->from)
                memory_copy(job->pixels, job->from, job->bytes);
        for (int at = 0; at < (job->count ? job->count : 1); at++)
        {
                int x = job->count ? job->screens[at][0] : 0, y = job->count ? job->screens[at][1] : 0;
                int w = job->count ? job->screens[at][2] : job->width, h = job->count ? job->screens[at][3] : job->height;

                if (x < 0 || y < 0 || w <= 0 || h <= 0 || x + w > job->width || y + h > job->height)
                        continue;
                taskbar_wall_draw(job->pixels + (long)y * job->width + x, job->width, w, h, job->dark, job->dimmed, job->maria, job->glow, job->accent,
                                  job->turn, job->step, job->squares[at < COMPOSITOR_WALL_SCREENS ? at : 0]);
                job->squares[at][0] += x, job->squares[at][1] += y;
        }
}

/*
        The eventfd the wallpaper's thread knocks the loop with when it is done (the doors' own, compositor_doors_start sets it): the loop
        wakes for it and takes the wallpaper at that pass, and asks the time of nothing while one is made. -1 before the doors are open;
        then the loop looks again a frame later (compositor.period) while one is made.
*/
static int compositor_wall_knock = -1;

// The wallpaper is deferred till the first frame is out: set when compositor_wall_keep held one back, so the loop wakes for it after.
static int compositor_wall_deferred;

static fn compositor_wall_run(address_any context)
{
        struct compositor_wall_job *job = (struct compositor_wall_job *)context;
        unsigned long one = 1;

        // Under every other thread of the compositor (nice 10, and the disc's threads it starts with it): it is never what a frame waits for.
        compositor_call(syscall(setpriority), 0, (positive)compositor_call(syscall(gettid), 0, 0, 0, 0, 0, 0), 10, 0, 0, 0);
        compositor_wall_make(job);
        job->took = clock_monotonic_nanoseconds() - job->began;
        __atomic_store_n(&job->done, 1, __ATOMIC_RELEASE);
        if (compositor_wall_knock >= 0)
                compositor_call(syscall(write), (positive)compositor_wall_knock, (positive)&one, sizeof one, 0, 0, 0);
}

// The wallpaper made on its thread taken: it is the one shown from now, the one before let go, the space damaged.
static void compositor_wall_take(void)
{
        struct compositor_wall_job *job = &compositor_wall_job;

        if (job->worker)
                thread_join(job->worker);
        job->worker = null;
        if (compositor_wall)
                gpu_forget(compositor_wall, compositor_wall_bytes), compositor_call(syscall(munmap), (positive)compositor_wall, compositor_wall_bytes, 0, 0, 0, 0);
        compositor_wall = job->pixels, compositor_wall_bytes = job->bytes, compositor_wall_w = job->width, compositor_wall_h = job->height;
        memory_copy(compositor_wall_key, job->key, sizeof(compositor_wall_key));
        job->pixels = null, job->done = 0, compositor_wall_turn = job->turn;
        if (job->step)
        {
                // A step of the turn: the moons' squares alone changed.
                for (int at = 0; at < (job->count ? job->count : 1); at++)
                        compositor_damage(job->squares[at][0], job->squares[at][1], job->squares[at][2], job->squares[at][3]);
                return;
        }
        compositor_wall_say(null, compositor_wall_w, compositor_wall_h, job->took / 1000000);
        compositor_damage(0, 0, compositor.width, compositor.height);
}

// The step of the turn the real clock is at now.
static positive compositor_wall_step_now(void)
{
        timespec real = {0, 0};

        clock_gettime(CLOCK_REALTIME, address_of real);
        return ((positive)real.tv_sec * 1000000000ull + (positive)real.tv_nsec) / COMPOSITOR_WALL_STEP;
}

// The turn of a step.
static b64 compositor_wall_turn_of(positive step)
{
        return (b64)(step % COMPOSITOR_WALL_TURN_STEPS) * UI_ONE / COMPOSITOR_WALL_TURN_STEPS;
}

static positive compositor_wall_step_now(void);

// Whether a wallpaper is being made on its thread.
static int compositor_wall_making(void)
{
        return compositor_wall_job.pixels != null;
}

// When the loop is to look at the wallpaper again, 0 for no time: one held back for the first frame now that a frame is out; one being
// made only a frame later, and only with no knock to wake the loop when it is done.
static positive compositor_wall_due(positive now)
{
        if (compositor_wall_deferred && compositor.presented)
                return now;
        if (compositor_wall_job.pixels && compositor_wall_knock < 0)
                return now + (compositor.period ? compositor.period : 16666667ull);
        // Turning: woken at the next step of the clock, and no sooner.
        if (compositor_wall && !compositor_wall_job.pixels && taskbar_motion_about(now))
        {
                timespec real = {0, 0};
                positive at;

                clock_gettime(CLOCK_REALTIME, address_of real);
                at = (positive)real.tv_sec * 1000000000ull + (positive)real.tv_nsec;
                return compositor_wall_step_now() != compositor_wall_stepped ? now : now + COMPOSITOR_WALL_STEP - at % COMPOSITOR_WALL_STEP + 1000000ull;
        }
        return 0;
}

/*
        A wallpaper begun on its thread for the space: whole (step 0), or a step of the moon's turn (step 1: a copy of the one shown, each
        moon's square made again at turn). With no memory it is said (whole) or the step is not taken.
*/
static void compositor_wall_start(b64 turn, int step)
{
        struct compositor_wall_job *job = &compositor_wall_job;
        unsigned int count = 0;
        const int (*screens)[4] = compositor_screens(&count);

        job->bytes = (positive)compositor.width * (positive)compositor.height * 4;
        {
                long got = (long)compositor_call(syscall(mmap), 0, job->bytes, 3, 0x22, (positive)-1, 0);

                if (got < 0 && got > -4096)
                {
                        // Said once for this space and look; looked for again only when something changes.
                        if (!step)
                                compositor_wall_say("there was no memory for it", 0, 0, 0), memory_copy(compositor_wall_key, job->key, sizeof(compositor_wall_key));
                        return;
                }
                job->pixels = (unsigned int *)got;
        }
        // A step's copy of the wallpaper shown is the thread's to make: the one shown is let go only once the step is taken.
        job->from = step ? compositor_wall : null;
        if (step)
                memory_copy(job->key, compositor_wall_key, sizeof(job->key));
        job->width = compositor.width, job->height = compositor.height, job->count = count < COMPOSITOR_WALL_SCREENS ? (int)count : COMPOSITOR_WALL_SCREENS;
        for (int at = 0; at < job->count; at++)
                memory_copy(job->screens[at], screens[at], sizeof(job->screens[at]));
        job->dark = !(compositor.table && compositor.table->themed && compositor.table->theme == UI_THEME_LIGHT);
        job->dimmed = compositor_wall_rows.dimmed, job->maria = compositor_wall_rows.maria, job->glow = compositor_wall_rows.glow;
        job->accent = taskbar_wall_accent(), job->done = 0, job->began = clock_monotonic_nanoseconds(), job->took = 0;
        job->turn = turn, job->step = step;
        job->worker = thread_start(compositor_wall_run, (address_any)job);
        // No thread: made here, as it was.
        if (!job->worker)
        {
                compositor_wall_make(job);
                job->took = clock_monotonic_nanoseconds() - job->began;
                compositor_wall_take();
        }
}

static void compositor_wall_keep(void)
{
        struct compositor_wall_job *job = &compositor_wall_job;
        unsigned int count = 0, now[8];
        const int (*screens)[4] = compositor_screens(&count);
        int dark = !(compositor.table && compositor.table->themed && compositor.table->theme == UI_THEME_LIGHT);

        // Not before the rows are read: a compositor that has read no settings (a check's) keeps its colour.
        if (!compositor_wall_rows.ready)
                return;
        // The first frame first, on a screen: the wallpaper after it, never in its way at boot.
        compositor_wall_deferred = 0;
        if (compositor.scanout && !compositor.presented)
        {
                compositor_wall_deferred = 1;
                return;
        }
        // One being made: taken when it is done, and nothing else asked till then.
        if (job->pixels)
        {
                if (__atomic_load_n(&job->done, __ATOMIC_ACQUIRE))
                        compositor_wall_take();
                return;
        }
        now[0] = (unsigned int)compositor.width, now[1] = (unsigned int)compositor.height, now[2] = count, now[3] = (unsigned int)dark;
        now[4] = taskbar_wall_accent(), now[5] = (unsigned int)(compositor_wall_rows.moon | compositor_wall_rows.dimmed << 1 | compositor_wall_rows.glow << 2);
        now[6] = (unsigned int)compositor_wall_rows.maria, now[7] = count ? (unsigned int)(screens[0][2] * 31 + screens[0][3]) : 0;
        if (!memory_compare(compositor_wall_key, now, sizeof(now)))
        {
                // Nothing of it changed: a step of the moon's turn, if it is turning and the clock has moved on a step.
                positive clock = clock_monotonic_nanoseconds(), step = compositor_wall_step_now();

                if (!compositor_wall || !taskbar_motion_about(clock) || step == compositor_wall_stepped)
                        return;
                compositor_wall_stepped = step;
                compositor_wall_start(compositor_wall_turn_of(step), 1);
                return;
        }
        // Plain, or a space with nothing in it: the colour, at once.
        if (!compositor_wall_rows.moon || compositor.width <= 0 || compositor.height <= 0)
        {
                compositor_wall_say(!compositor_wall_rows.moon ? "canvas wallpaper is plain" : "the space has no screen yet", 0, 0, 0);
                memory_copy(compositor_wall_key, now, sizeof(now));
                if (compositor_wall)
                        gpu_forget(compositor_wall, compositor_wall_bytes), compositor_call(syscall(munmap), (positive)compositor_wall, compositor_wall_bytes, 0, 0, 0, 0);
                compositor_wall = null, compositor_wall_w = compositor_wall_h = 0;
                compositor_damage(0, 0, compositor.width, compositor.height);
                return;
        }
        // A space of another size: the old picture is no ground for it, so the colour until the new one is made.
        if (compositor_wall && (compositor_wall_w != compositor.width || compositor_wall_h != compositor.height))
        {
                gpu_forget(compositor_wall, compositor_wall_bytes), compositor_call(syscall(munmap), (positive)compositor_wall, compositor_wall_bytes, 0, 0, 0, 0);
                compositor_wall = null, compositor_wall_w = compositor_wall_h = 0;
                compositor_damage(0, 0, compositor.width, compositor.height);
        }
        // The moon at the turn it is at while it turns, and the step of the clock that is.
        if (taskbar_motion_about(clock_monotonic_nanoseconds()))
                compositor_wall_stepped = compositor_wall_step_now(), compositor_wall_turn = compositor_wall_turn_of(compositor_wall_stepped);
        memory_copy(job->key, now, sizeof(now));
        compositor_wall_start(compositor_wall_turn, 0);
}

// For a check: the wallpaper being made, waited for and taken.
static void compositor_wall_finish(void)
{
        if (!compositor_wall_job.pixels)
                return;
        while (!__atomic_load_n(&compositor_wall_job.done, __ATOMIC_ACQUIRE))
                compositor_call(syscall(sched_yield), 0, 0, 0, 0, 0, 0);
        compositor_wall_take();
}

// The frames of a present and the row of each: the rows' (compositor_rows_make), as many as the rows in use have.
static struct compositor_frame *compositor_present_frame_room;
static unsigned int *compositor_present_row_room;

static void compositor_present_frames(void)
{
        struct compositor_frame *frames = compositor_present_frame_room; // the rows' (compositor_rows_make), COMPOSITOR_FRAMES_FOR the rows in use
        unsigned short clip[COMPOSITOR_DAMAGE][4];
        struct compositor_frame band;
        struct compositor_frame preview;
        unsigned int *rows = compositor_present_row_room; // and the row of each frame, the rows in use and the layers
        unsigned int count = 0, windows;
        int tinted;

        // Not while a flip is in flight, nor while the card still has jobs that write or read what this present would (compositor_card_holds).
        if (!compositor.damages || compositor.flip_pending || compositor_card_holds())
                return;
        compositor_bands_follow();

        // What moonwater debug says of the card, once there is a table to say it in.
        if (compositor.table)
        {
                memory_copy(compositor.table->driver, compositor.driver, sizeof(compositor.table->driver));
                compositor.table->pointer_plane = compositor_pointer_plane_word();
                compositor.table->overlay = compositor.overlay_state;
        }

        for (unsigned int at = 0; at < compositor.stacked; at++)
                if (compositor_visible(compositor.stack[at]))
                {
                        rows[count] = compositor.stack[at];
                        compositor_frame_of(compositor.stack[at], &frames[count++]);
                        // Its client's subsurfaces as frames of their own over it, read where they are (wayland_host.c), each with the row.
                        count += compositor_wl_layers_frames(compositor.stack[at], frames, rows, count);
                }
        windows = count;
        compositor_record_scene(frames, windows); // the window pictures of this present, for the record's frames
        // The snap preview leaves its window's frame for one of its own, after the tabs, that is drawn under the window and over the windows under it.
        tinted = compositor_preview_lift(frames, windows, &preview);
        int turning = compositor_turning();
        int direct = turning ? -1 : compositor_direct_candidate(frames, rows, windows);
        int stay_live_direct = direct >= 0 && !compositor_lent(direct) && direct == compositor.direct_row && !compositor_overlay_ready() &&
                               compositor.patched[direct][compositor.front[direct] & 1u] &&
                               compositor_window_fb(direct, compositor.front[direct]) == compositor.on_screen &&
                               compositor.under[compositor.front[direct] & 1u];

        // A window the card shows straight has what the tabs covered put back, so the glass is made from the window,
        // unless we are staying on the same patched live on-screen framebuffer.
        if (!stay_live_direct)
                compositor_direct_unpatch();
        compositor_glass_pass(frames, rows, windows);
        // After the glass, which puts a glass window's own composed picture in its frame: that one has no runs.
        compositor_runs_note(frames, rows, windows);

        // What is under the tabs changed, or what they say: the glass is made again over it, and shown. The glass
        // is made from the scene over the tabs' span and the blur's reach round it, so only a change there is one.
        // Each band in turn, with whether all are stale read once: the first one drawn says they are not.
        {
                int stale = taskbar_stale(), bands = compositor_bands();
                const unsigned int *saved_direct_pixels = 0;

                for (compositor.band_at = 0; compositor.band_at < bands; compositor.band_at++)
                {
                        struct taskbar_view view;
                        unsigned long redo = stale ? COMPOSITOR_TABS_ALL : 0;

                        compositor_band_frame(&band);
                        view = compositor_taskbar_view();
                        if (!redo && view.pixels)
                                redo = taskbar_meets(&view, compositor.damage, compositor.damages);
                        if (redo)
                        {
                                if (stay_live_direct && !saved_direct_pixels && windows)
                                {
                                        unsigned int which = compositor.front[direct] & 1u;
                                        unsigned int *stage = compositor_direct_stage_ready();

                                        if (stage)
                                        {
                                                unsigned long stride = (unsigned long)compositor_stride();
                                                const int *old = compositor.under_rect[which];

                                                saved_direct_pixels = frames[windows - 1].pixels;
                                                memory_copy(stage, saved_direct_pixels, stride * (unsigned long)compositor.height * 4);
                                                memory_copy_rect_u32(stage + (unsigned long)old[1] * stride + (unsigned long)old[0], stride,
                                                                     compositor.under[which], (unsigned long)(old[2] - old[0]),
                                                                     (unsigned long)(old[2] - old[0]), (unsigned long)(old[3] - old[1]));
                                                frames[windows - 1].pixels = stage;
                                        }
                                }
#ifdef CONFIG_MOONWATER_WAYLAND
                                compositor_wl_lent_sync(1);
#endif
                                compositor_band_draw(frames, windows, redo);
                        }
                }
                if (saved_direct_pixels && windows)
                        frames[windows - 1].pixels = saved_direct_pixels;
                for (compositor.band_at = 0; compositor.band_at < bands; compositor.band_at++)
                {
                        compositor_band_frame(&band);
                        count += compositor_moon_glow_frame(&frames[count]); // under the plates
                        count += compositor_panel_tabs(&band, &frames[count]);
                }
                compositor.band_at = 0;
        }
        if (tinted)
                frames[count++] = preview;
        count += compositor_moon_menu_frame(&frames[count]); // the moon's menu, over the band and the windows

        // The snap assist and the grid, over the windows (the one held too), the tabs and the snap preview: a thumbnail's picture is drawn again each present. They
        // fade in when they come up and out when they go (compositor_overlay_fade), a thumbnail with them.
        if ((unsigned int)compositor_overlays_up() != compositor.overlays_up)
        {
                compositor.overlays_up = (unsigned int)compositor_overlays_up();
                // Up with no assist, the thumbnails of an earlier one are none; down, they fade out with the rest (or go now).
                if (compositor.overlays_up && !compositor.assist_on)
                        compositor.assist_count = 0;
                if (!compositor.overlays_up && !compositor_glides())
                        compositor.assist_count = 0;
                compositor_overlay_fade(compositor.overlays_up ? 100 : 0);
        }
        if (compositor_overlays_shown())
        {
                if (compositor.assist_armed)
                {
                        compositor_assist_refresh();
                        for (int thumb = 0; thumb < compositor.assist_count; thumb++)
                                compositor_damage(compositor.assist_rect[thumb][0], compositor.assist_rect[thumb][1],
                                                  compositor.assist_rect[thumb][2], compositor.assist_rect[thumb][3]);
                }
                // The grid is over the window held too, so its tiles, the targets the hand is aiming at, stay seen.
                count += compositor_overlays(frames, count);
        }

        compositor_corner_masks_ready(frames, count);
        if (compositor_spots_any(0))
                compositor_spots_ready();

        // A window that fills the output alone is shown straight from its buffer, and nothing is composed.
        // A turned screen is composed and shown from its buffer, never straight from a window's: no direct scanout.
        if (turning)
                compositor_unscanned(15);

        if (direct >= 0 &&
            ((compositor_overlay_ready() && compositor_present_overlaid(direct, frames, count, windows)) ||
             compositor_present_direct(direct, frames + windows, count - windows)))
        {
                // The overlay is the tabs' now, or nothing's.
                compositor_unscanned(0);
                compositor.plane_row = -1;
                compositor.plane_fb = 0;
                return;
        }

        // Not now: the compositor's own screen is shown again, drawn whole, once the card has finished a flip.
        int back = !turning && compositor.on_screen && compositor.on_screen != compositor.fb;

        if (back)
        {
                if (compositor.flip_pending)
                        return;
                compositor.direct_row = -1;
                compositor_damage(0, 0, compositor.width, compositor.height);
        }
        else if (compositor.screen_stale)
                compositor_damage(0, 0, compositor.width, compositor.height);
        compositor.screen_stale = 0;

        // The top window on the overlay where it can be: nothing of it composed under the plane.
        int planed[4], plane = !back && !turning ? compositor_plane_candidate(frames, rows, windows, planed) : -1;
        int plane_off = 0;

        if (compositor.plane_row >= 0 && (plane != compositor.plane_row ||
                                          memory_compare(planed, compositor.plane_rect, sizeof(planed)) != 0))
        {
                // Off the plane, or moved on it: the card is to have it no more, and the compositor composes it.
                if (compositor.flip_pending)
                        return;
                compositor_damage(compositor.plane_rect[0], compositor.plane_rect[1],
                                  compositor.plane_rect[2] - compositor.plane_rect[0],
                                  compositor.plane_rect[3] - compositor.plane_rect[1]);
                if (plane < 0)
                {
                        if (compositor_spare.fb)
                                plane_off = 1;
                        else if (!compositor_atomic_to(compositor_primary_fb(), -1, 0, 1, 0, 0, 0))
                                compositor_overlay_off();
                }
                compositor.plane_row = compositor.direct_row = -1;
                compositor.plane_fb = 0;
        }
        if (plane >= 0)
        {
                int lent = compositor_lent(plane);
                unsigned int which = compositor.front[plane];
                unsigned int fb = lent ? compositor_lent_fb(plane) : compositor_window_fb(plane, which);
                int source[2] = {planed[0] - compositor.x[plane], planed[1] - compositor.y[plane]};

                if (compositor.flip_pending)
                        return;
                if (!fb || (fb != compositor.plane_fb && !compositor_atomic_to(compositor.fb, plane, which, 1, fb, source, planed)))
                {
                        if (lent)
                                compositor_lent_fb_fail(plane);
                        else
                        {
                                // Refused: off the overlay for good, and the window composed whole from now.
                                compositor_unscanned(12);
                                compositor.overlay_state = -1;
                                compositor_overlay_off();
                        }
                        compositor.plane_row = compositor.direct_row = -1;
                        compositor_damage(planed[0], planed[1], planed[2] - planed[0], planed[3] - planed[1]);
                }
                else
                {
                        compositor.plane_fb = fb;
                        compositor_unscanned(0);
                        compositor.plane_row = compositor.direct_row = plane;
                        memory_copy(compositor.plane_rect, planed, sizeof(planed));
                        compositor.table->scanned++;
                        // Where the pieces do not fit the list, what is under the plane is composed too, unseen.
                        compositor_damage_cut(planed);
                }
        }

        // The frame goes into the buffer the screen is not showing, which is then shown (compositor_plate_swap): when the screens are one and no window
        // is on a plane. Its stale is composed with this present's damage, the damage is what the one shown does not have.
        int flip_own = 0;
        // This present's own damage, its box, taken before the stale of the buffer composed into is added to it: the other buffer
        // misses this and nothing else (see the join below).
        int scene[COMPOSITOR_DAMAGE][4];
        unsigned int scenes = 0;

        if (compositor_plate_wanted(turning))
        {
                if (compositor.card >= 0)
                        compositor_plate_make();
                if (compositor_spare.fb)
                {
                        if (compositor.flip_pending)
                                return;
                        compositor_plate_swap();
                        flip_own = 1;
                        for (unsigned int at = 0; at < compositor.damages; at++)
                                memory_copy(scene[scenes++], compositor.damage[at], sizeof(scene[0]));
                        // What this buffer missed: its rectangles where they are known, each its own damage, or the box round them.
                        if (compositor_parts_hold(&compositor_front_parts, compositor_front_stale, compositor_front_stale_any))
                        {
                                struct compositor_parts parts = compositor_front_parts;

                                for (unsigned int at = 0; at < parts.count; at++)
                                        compositor_damage(parts.rect[at][0], parts.rect[at][1], parts.rect[at][2] - parts.rect[at][0],
                                                          parts.rect[at][3] - parts.rect[at][1]);
                        }
                        else if (compositor_front_stale_any)
                                compositor_damage(compositor_front_stale[0], compositor_front_stale[1], compositor_front_stale[2] - compositor_front_stale[0],
                                                  compositor_front_stale[3] - compositor_front_stale[1]);
                }
        }

        // The close buttons' means, made once the buffer's stale is in the damage (the swap above adds what this buffer missed): a glyph in a
        // buffer composed from the means of the last present showed that present's ink for a frame (the title bar's buttons, 2026-11-04).
        for (unsigned int at = 0; at < windows; at++)
        {
                compositor_close_mean(&frames[at]);
                compositor_close_ready(&frames[at]);
        }
#ifdef MOONWATER_GPU_C
        int gpu_drawn = compositor_gpu_present(frames, count), gpu_composed = gpu_drawn && !(compositor.width & 3);

        if (gpu_drawn)
                compositor_wl_count_card(rows, windows); // a width not a multiple of 4: the processor turns it (its strip is the processor's)
        // The processor draws: a job given up on is waited for here, as it was, but without a wait (present-nowait) none is pending but one whose end was
        // said (the hang path, which draws on the processor regardless), since the present was held while one ran (compositor_card_holds).
        else if (!(compositor_card.nowait && compositor_card.looped) && gpu.pending && !gpu_finish())
        {
                if (gpu_lost)
                        compositor_gpu_recover();
                compositor_gpu_job_failed();
        }
        int apart = !gpu_drawn && !flip_own && compositor_changed_ready();
#else
        int apart = !flip_own && compositor_changed_ready(), gpu_composed = 0;
#endif
#ifdef CONFIG_MOONWATER_WAYLAND
        if (!gpu_composed)
                compositor_wl_lent_sync(1);
#endif
        if (!compositor.mirror_bytes && compositor.changed > 0)
        {
                compositor.mirror = compositor.scanout;
                compositor.mirror_pitch = compositor.scanout_pitch / 4;
        }
        if (!apart && compositor.mirror_bytes)
                compositor.mirror_stale = 1;
        struct compositor_apart slices = {compositor_slice_flush, compositor.composing, (unsigned long)compositor.width};
        unsigned int clips = 0;

        for (unsigned int at = 0; at < compositor.damages; at++)
        {
                const int *rectangle = compositor.damage[at];
                struct compositor_target target = {
                    compositor.scanout, compositor.scanout_pitch / 4, compositor.width,
                    compositor.height, rectangle[0], rectangle[1], rectangle[2], rectangle[3]};
                int rows[2];

#ifdef MOONWATER_GPU_C
                // The GPU drew the damage's columns below a multiple of 4 (compositor_gpu_draw), with the fingers' marks and the pointer over them, as records.
                // The columns past it are the processor's, with those over them; nothing of the scene or the pointer is drawn twice.
                if (gpu_drawn)
                {
                        struct compositor_target strip = target;

                        rows[0] = rectangle[1], rows[1] = rectangle[3] - 1;
                        strip.x1 = rectangle[0] > (compositor.width & ~3) ? rectangle[0] : compositor.width & ~3;
                        if (strip.x1 < strip.x2)
                        {
                                compositor_scene_banded(&strip, frames, count, 0, rows);
                                if (compositor_spots_any(0))
                                        compositor_spots_compose(&strip, frames, count);
                                compositor_cursor_compose(&strip, frames, count);
                        }
                }
                else
#endif
                {
                compositor_scene_banded(&target, frames, count, apart ? &slices : 0, rows);

                // Composed apart, each slice had the fingers' marks; on a scanout only written, they are composed aside.
                if (!apart && compositor_spots_any(0))
                        compositor_spots_compose(&target, frames, count);
                if (!apart)
                        compositor_cursor_compose(&target, frames, count);
                }

                compositor.painted += (unsigned long)compositor_area(rectangle);
                // Composed apart, the card is told only the rows that changed on it, and nothing where none did.
                if (apart && rows[0] < 0)
                        continue;
                for (unsigned int side = 0; side < 4; side++)
                        clip[clips][side] = (unsigned short)rectangle[side];
                if (apart)
                {
                        clip[clips][1] = (unsigned short)rows[0];
                        clip[clips][3] = (unsigned short)(rows[1] + 1);
                }
                clips++;
        }
        if (apart)
        {
                int full = 0;

                for (unsigned int at = 0; at < compositor.damages; at++)
                        if (compositor.damage[at][0] <= 0 && compositor.damage[at][1] <= 0 &&
                            compositor.damage[at][2] >= compositor.width && compositor.damage[at][3] >= compositor.height)
                                full = 1;
                if (full || !compositor.mirror_bytes)
                        compositor.mirror_stale = 0;
        }
        compositor_runs_learn(frames, rows, windows);

        // The buffer composed into is whole now; the one the screen does not show misses this present's own damage, not the stale composed
        // into the buffer above: the shown one was composed at the last present and has that already. Joined in as well, each buffer's stale
        // took in the other's on every present and grew to the whole screen.
        if (compositor_spare.fb)
        {
                if (flip_own)
                {
                        for (unsigned int at = 0; at < scenes; at++)
                                compositor_stale_part(compositor_spare.stale, &compositor_spare.stale_any, &compositor_spare_parts, scene[at]);
                }
                else
                        for (unsigned int at = 0; at < compositor.damages; at++)
                        {
                                const int *d = compositor.damage[at];
                                int r[4] = {d[0], d[1], d[2], d[3]};

                                compositor_stale_part(compositor_spare.stale, &compositor_spare.stale_any, &compositor_spare_parts, r);
                        }
        }
        if (compositor_output_count() < 2 && compositor.plane_row < 0)
                compositor_front_stale_any = 0;
        compositor.damages = 0;
        // The screens told, turned and shown (compositor_present_tell): now for the processor's present; for the card's, when its jobs are done, its flip
        // committed now where the card takes the job's fence with it (compositor_card_after).
#ifdef MOONWATER_GPU_C
        if (gpu_drawn && compositor_card.on)
        {
                compositor_card_after((const unsigned short (*)[4])clip, clips, turning, gpu_composed, flip_own, back, plane_off, rows, windows);
                return;
        }
#endif
        compositor_present_tell((const unsigned short (*)[4])clip, clips, turning, gpu_composed, flip_own, back, plane_off);
}

