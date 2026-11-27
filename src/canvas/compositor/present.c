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
static int compositor_cast_has_stale(void);

static void compositor_present_frames(void);
static void compositor_dark_set(void); // kms.c: the screens put out again after a mode set, while display-off holds
// cast.c: the capture engine's rings, composed into after a present; while a screen is shared every present is composed
static int compositor_cast_screens(void);
static void compositor_cast_look(int always);
static void compositor_cast_present(const struct compositor_frame *frames, unsigned int count, const unsigned int *rows, unsigned int windows,
                                    const int (*damage)[4], unsigned int damages);

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
        The card's present taken apart, for each one whose last job's sync_file said when it signalled (the kernel's stamp of its fence, SYNC_IOC_FILE_INFO):
        the loop's part (from the present's start to its jobs handed to the card), the card's (from there to the fence: the driver's scheduler, the card waking
        and its clock, the jobs' own run) and the loop's again (from the fence to the turn that saw it done), in rings of the last WINDOWS_METRIC_RUN.
*/
static unsigned long long compositor_metric_hand[WINDOWS_METRIC_RUN], compositor_metric_job[WINDOWS_METRIC_RUN], compositor_metric_seen[WINDOWS_METRIC_RUN];
static unsigned int compositor_metric_split_at, compositor_metric_split_count;
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

        if (!table)
                return;
        {
                const struct
                {
                        const unsigned long long *ring;
                        unsigned int count;
                        unsigned int *mean, *worst;
                } rings[5] = {
                    {compositor_metric_card, compositor_metric_card_count, &table->metric_card_mean, &table->metric_card_worst},
                    {compositor_metric_processor, compositor_metric_processor_count, &table->metric_processor_mean, &table->metric_processor_worst},
                    {compositor_metric_hand, compositor_metric_split_count, &table->metric_card_hand_mean, &table->metric_card_hand_worst},
                    {compositor_metric_job, compositor_metric_split_count, &table->metric_card_job_mean, &table->metric_card_job_worst},
                    {compositor_metric_seen, compositor_metric_split_count, &table->metric_card_seen_mean, &table->metric_card_seen_worst},
                };

                for (unsigned int i = 0; i < 5; i++)
                        windows_metric_stats(rings[i].ring, rings[i].count, rings[i].mean, rings[i].worst);
                table->metric_card_split = compositor_metric_split_count;
        }
#ifdef MOONWATER_GPU_C
        table->metric_maps = gpu_map_count, table->metric_map_hits = gpu_map_hits, table->metric_map_misses = gpu_map_misses;
        table->metric_maps_own_user = gpu_maps_own_user, table->metric_maps_own_errno = gpu_maps_own_errno;
        table->metric_wakes = gpu_wakes, table->metric_jobs = gpu_jobs;
        table->metric_clocks_level = compositor_gpu_clocks.level, table->metric_clocks_held = compositor_gpu_clocks.held;
        table->metric_clocks_asks = gpu_clocks_state.asks, table->metric_clocks_took = (unsigned int)(gpu_clocks_state.took / 1000);
        table->metric_clocks_error = gpu_clocks_state.error;
        table->metric_job_mean = gpu_jobs ? (unsigned int)(gpu_job_ns / gpu_jobs / 1000) : 0;
        waited = gpu_job_ns;
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
                unsigned long long dt = now - compositor_metric_since;

                compositor_metric_presents_second = windows_metric_rate(compositor_metric_presents - compositor_metric_presents_then, dt);
                compositor_metric_lists_second = windows_metric_rate(lists - compositor_metric_lists_then, dt);
                compositor_metric_late_second = windows_metric_rate(compositor_late.made - compositor_metric_late_then, dt);
                compositor_metric_missed_second = windows_metric_rate(compositor_late.missed - compositor_metric_missed_then, dt);
                compositor_metric_flip_jobs_second = windows_metric_rate(compositor_metric_flips_jobs - compositor_metric_flips_jobs_then, dt);
                compositor_metric_loop_most_second = (unsigned int)(compositor_metric_loop_most / 1000);
                compositor_metric_flip_jobs_most_second = compositor_metric_flip_jobs_most;
                compositor_metric_waited_second = windows_metric_rate((waited - compositor_metric_waited_then) / 1000, dt);
                compositor_metric_waited_then = waited, compositor_metric_late_then = compositor_late.made, compositor_metric_missed_then = compositor_late.missed;
                compositor_metric_flips_jobs_then = compositor_metric_flips_jobs, compositor_metric_loop_most = 0, compositor_metric_flip_jobs_most = 0;
                compositor_metric_since = now, compositor_metric_presents_then = compositor_metric_presents, compositor_metric_lists_then = lists;
                kernel_notices_tick(table, now);
        }
        table->metric_presents = compositor_metric_presents_second, table->metric_lists = compositor_metric_lists_second;
        table->metric_late = compositor_metric_late_second, table->metric_missed = compositor_metric_missed_second;
        table->metric_lead = (unsigned int)(compositor_present_lead() / 1000), table->metric_present_most = compositor_metric_loop_most_second;
        table->metric_flip_jobs = compositor_metric_flip_jobs_second, table->metric_flip_jobs_most = compositor_metric_flip_jobs_most_second;
        table->metric_waited = compositor_metric_waited_second;
        // The late-latch steps of the two leads ([0] composed, [1] shown straight) and the warm count after an idle gap.
        {
                const struct compositor_lead_step *steps[2] = {&compositor_late.composed, &compositor_late.direct};

                for (unsigned int at = 0; at < 2; at++)
                        table->lead_widen_us[at] = (unsigned int)(steps[at]->widen / 1000), table->lead_calm[at] = steps[at]->calm,
                        table->lead_calm_need[at] = steps[at]->calm_need ? steps[at]->calm_need : COMPOSITOR_LEAD_CALM;
                table->lead_warm = compositor_late.warm;
        }
#ifdef CONFIG_MOONWATER_WAYLAND
        table->jit_armed = compositor_jit.armed, table->jit_unarmed = compositor_jit.unarmed, table->jit_render_us = (unsigned int)(compositor_wl_game_render_ns / 1000);
        table->jit_margin_us = (unsigned int)(compositor_jit.widen / 1000), table->jit_late = compositor_jit.late;
        table->jit_landed = compositor_jit.landed;
#endif
        table->flips_late = compositor_tear.late, table->tearing = compositor_tear.policy;
}

// The loop's turns a second, and those whose ppoll had nothing to wait for (a timer already due, so the turn spun): the wake rate in moonwater debug.
static unsigned long long compositor_loop_turns, compositor_loop_idle, compositor_loop_since, compositor_loop_then, compositor_loop_idle_then;

static unsigned int compositor_loop_by[WINDOWS_LOOP_SOURCES], compositor_loop_by_second[WINDOWS_LOOP_SOURCES];

// Where each part of the loop's poll list is (compositor_loop_wanted builds it): 0 for a part that is not in it this turn.
struct compositor_slots
{
        unsigned int count, inputs, doors, done_at, card_at, fence_at, uevents_at, clip_at, wayland_at, xwm_at;
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

/*
        The loop's turns by how long each took (moonwater debug's loop turns): a turn is from its wait's end (compositor_loop_turn_began) to the top of
        the next, whose clock the loop reads anyway, so a turn costs no clock of its own. They are counted as microseconds in WINDOWS_US_BUCKETS powers
        of two (windows_us_bucket). Beside them the time the loop went with its clients unread because a card job ran (compositor_card_clients_held, the
        owner's clients-held), from the top of a held turn to the top of the next, and the X windows the window manager's pass looked at
        (compositor_xwm_looked).
*/
static unsigned int compositor_loop_spans[WINDOWS_US_BUCKETS], compositor_loop_spans_then[WINDOWS_US_BUCKETS];
static unsigned long long compositor_loop_turn_most, compositor_loop_most_second;
static unsigned long long compositor_loop_turn_began, compositor_loop_top, compositor_clients_held_ns, compositor_clients_held_then;
static unsigned long long compositor_xwm_looked, compositor_xwm_looked_then;
static b32 compositor_loop_top_held;

// The top of the loop at now, held saying whether the clients go unread this turn: the turn that ended counted, and the held time.
static inline void compositor_loop_turned(unsigned long long now, b32 held)
{
        if (compositor_loop_turn_began && now >= compositor_loop_turn_began)
        {
                unsigned long long took = now - compositor_loop_turn_began;

                compositor_loop_spans[windows_us_bucket(took / 1000ull)]++;
                compositor_loop_turn_most = took > compositor_loop_turn_most ? took : compositor_loop_turn_most;
                compositor_loop_most_second = took > compositor_loop_most_second ? took : compositor_loop_most_second;
                compositor_loop_turn_began = 0;
        }
        if (compositor_loop_top_held && now > compositor_loop_top)
                compositor_clients_held_ns += now - compositor_loop_top;
        compositor_loop_top = now, compositor_loop_top_held = held;
}

static void compositor_loop_tick(unsigned long long now, b32 idle)
{
        struct windows address_to table = compositor.table;

        compositor_loop_turns++, compositor_loop_idle += idle;
        if (!compositor_loop_since)
                compositor_loop_since = now, compositor_loop_then = compositor_loop_turns, compositor_loop_idle_then = compositor_loop_idle;
        if (now - compositor_loop_since >= 1000000000ull)
        {
                unsigned long long span = now - compositor_loop_since;

                if (table)
                {
                        table->metric_loops = windows_metric_rate(compositor_loop_turns - compositor_loop_then, span),
                        table->metric_loop_idle = windows_metric_rate(compositor_loop_idle - compositor_loop_idle_then, span);
                        for (unsigned int k = 0; k < WINDOWS_US_BUCKETS; k++)
                                table->metric_turns[k] = compositor_loop_spans[k],
                                table->metric_turns_second[k] = windows_metric_rate(compositor_loop_spans[k] - compositor_loop_spans_then[k], span);
                        table->metric_turn_most = (unsigned int)(compositor_loop_turn_most / 1000ull);
                        table->metric_turn_most_second = (unsigned int)(compositor_loop_most_second / 1000ull);
                        table->metric_clients_held = windows_metric_rate((compositor_clients_held_ns - compositor_clients_held_then) / 1000ull, span);
                        table->metric_xwm_looked = windows_metric_rate(compositor_xwm_looked - compositor_xwm_looked_then, span);
                }
                for (unsigned int k = 0; k < WINDOWS_US_BUCKETS; k++)
                        compositor_loop_spans_then[k] = compositor_loop_spans[k];
                compositor_loop_most_second = 0, compositor_clients_held_then = compositor_clients_held_ns, compositor_xwm_looked_then = compositor_xwm_looked;
                for (unsigned int k = 0; table && k < WINDOWS_LOOP_SOURCES; k++)
                        table->metric_loop_by[k] = windows_metric_rate(compositor_loop_by[k] - compositor_loop_by_second[k], span),
                        compositor_loop_by_second[k] = compositor_loop_by[k];
                compositor_loop_since = now, compositor_loop_then = compositor_loop_turns, compositor_loop_idle_then = compositor_loop_idle;
                compositor_cast_look(1); // a share's window gone, or a doorbell missed: once a second

        }
}

// The paths of the last COMPOSITOR_LEAD_FRAMES presents noted, the newest in bit 0 (1: the card's), and how many there are of them: the lead is
// measured on the presents as they were, the card's and the processor's (compositor_present_lead).
static unsigned int compositor_metric_paths, compositor_metric_paths_count;
static positive compositor_metric_worst_cached;
static int compositor_metric_worst_dirty = 1;

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
        compositor_metric_worst_dirty = 1;
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
HOT static positive compositor_present_lead(void)
{
        unsigned int cards = 0, processors;
        positive worst, worst_processor;

        if (compositor_late.early || compositor.card < 0)
                return 0;
        // Direct scanout only issues a KMS page-flip (~50 us, no GPU compose): use a tight 500 us lead instead of the
        // multi-millisecond composed-GPU lead so input-to-photon latency drops by ~3-4 ms on 144 Hz+ displays.
        if (compositor_lead_direct())
        {
                positive direct_lead = 500000ull + compositor_late.direct.widen;

                return direct_lead < compositor.period ? direct_lead : 0;
        }
        if (compositor_metric_worst_dirty)
        {
                if (compositor_metric_paths_count)
                {
                        unsigned int mask = compositor_metric_paths_count >= 32u ? ~0u : (1u << compositor_metric_paths_count) - 1u;
                        unsigned int v = compositor_metric_paths & mask;
                        v -= (v >> 1) & 0x55555555u;
                        v = (v & 0x33333333u) + ((v >> 2) & 0x33333333u);
                        cards = (((v + (v >> 4)) & 0x0f0f0f0fu) * 0x01010101u) >> 24;
                }
                processors = compositor_metric_paths_count - cards;
                worst = compositor_metric_worst(compositor_metric_card, compositor_metric_card_at, compositor_metric_card_count, cards);
                worst_processor = compositor_metric_worst(compositor_metric_processor, compositor_metric_processor_at, compositor_metric_processor_count, processors);
                compositor_metric_worst_cached = worst_processor > worst ? worst_processor : worst;
                compositor_metric_worst_dirty = 0;
        }
        return compositor_lead_of(compositor_metric_worst_cached, compositor_metric_paths_count, compositor_late.composed.widen, compositor.period);
}

/*
        A window whose flips tear (compositor_row_tears: the tearing policy and the window's async hint, which Xwayland sets for an X
        client's PresentOptionAsync), or whose frame is late (compositor_tear_late, policy late), and is shown straight from its buffer is
        flipped asynchronously (compositor_flip): the card puts its frame up mid-scanout, at once, so a present for it waits for no vblank
        and is due now, and is late for none (no lead: compositor_late_begin aims at no vblank). Every other present
        keeps the rule (compositor_present_due), under variable refresh too, which needs no rule of its own: a present due at once (nothing
        drawn for a period) is shown at once by a screen that waits for its frame, where at a fixed rate it waited for the next vblank, 0 to a
        period, and one due the lead before the soonest the screen can draw again (the mode's period after the last flip, the fastest the
        panel goes) is shown then, as at a fixed rate. A composed frame never tears: the hint is honoured only where the window is the frame.
*/
static int compositor_tearing(void)
{
        int row = compositor.direct_row;

        compositor_tear.now = 0;
        if (compositor.card < 0 || row < 0)
                return 0;
        if (compositor_row_tears(row))
                return 1;
        return compositor_tear.now = compositor_tear_late(row, clock_monotonic_nanoseconds());
}

// A cadence's game (compositor_cadence, k > 1) shown straight flips no sooner than the vblank k after its last: its present is due the lead
// before that one (half a period without a lead), so a frame that came early waits there and its next callback with it.
static positive compositor_present_at(int tearing, positive lead, positive now)
{
        positive due, slot;

        if (tearing)
                return now;
        due = compositor_present_due(compositor.presented, compositor.period, compositor.flip_when, lead, now);
        if (compositor_cadence.k < 2 || compositor.direct_row < 0 || compositor.flip_row != compositor.direct_row || !compositor.flip_when ||
            !compositor.period)
                return due;
        slot = compositor.flip_when + compositor_cadence.k * compositor.period - (lead && lead < compositor.period ? lead : compositor.period / 2);
        return slot > due ? slot : due;
}

/*
        A present's end, once its pixels are in compositor.fb: the screens told what changed (one DIRTYFB, or each screen's), the screens Canvas
        turns given their part (compositor_turn_frame), the overlay taken off where the frame is the compositor's own screen again, and the frame
        shown: the plate composed into flipped to (flip_own), or the compositor's own screen flipped back to (back), with what a refused flip does.
        At the present for one the processor drew; when its jobs are done for one the card drew (compositor_card_done).
*/
static void compositor_damage_space(void); // kms.c

// Synchronous CRTC modeset fallback when page flips fail.
static void compositor_crtc_sync(void)
{
        struct compositor_crtc set = {(unsigned long)address_of compositor.connector, 1, compositor.crtc, compositor.fb,
                                      0, 0, 0, 1, compositor.mode};

        if (compositor.outputs > 1)
                compositor_set_outputs(); // every screen, to the plate it shows
        else
                system_control(compositor.card, COMPOSITOR_SETCRTC, address_of set);
        compositor_scaled_reset();
        compositor_dark_set();
        compositor.on_screen = compositor.fb;
        compositor_flip_refused = 0;
        compositor_flip_done();
}

static void compositor_present_tell(const unsigned short (*clip)[4], unsigned int clips, int turning, int gpu_composed, int flip_own, int back, int plane_off)
{
        // No clips at all would tell the card everything changed. A headless compositor has no card to tell (its scan image is a file).
        // When flipping to a new framebuffer on hardware that does not copy damage (non-virtual GPUs), the page flip / atomic commit presents the new fb directly.
        if (clips && !(flip_own && compositor.on_screen != compositor.fb && !compositor_copies_damage()))
        {
                if (compositor_output_count() < 2 && !turning)
                        compositor_dirty(compositor.fb, clip, clips);
                else if (compositor.card >= 0)
                        compositor_dirty_screens(clip, clips);
        }
        // The screens Canvas turns: their part of the scene, turned into their own buffers and told to their cards.
        if (clips && turning)
                compositor_turn_frame(clip, clips, gpu_composed);

        // Back on the compositor's own screen: what was held for the window is said taken when the card is there.
        if ((back || plane_off) && compositor.overlay_on && !compositor_atomic(compositor.fb, -1, 0, 1, 0, 0))
                compositor_overlay_off();
        // A window on the overlay with a new buffer goes with the plate composed round it, in one atomic commit (compositor_plane_carry); one
        // the card refuses is the plate's flip alone, and the overlay's buffer is committed by the next present.
        if (flip_own && compositor.on_screen != compositor.fb && compositor_plane_carry.fb && compositor.card >= 0)
        {
                if (compositor_atomic_to(compositor.fb, compositor_plane_carry.row, compositor_plane_carry.which, 1, compositor_plane_carry.fb,
                                         compositor_plane_carry.source, compositor_plane_carry.rect))
                        compositor_flip_refused = 0, compositor_plane_carried(1);
                else
                {
                        compositor_damage(compositor_plane_carry.rect[0], compositor_plane_carry.rect[1],
                                          compositor_plane_carry.rect[2] - compositor_plane_carry.rect[0],
                                          compositor_plane_carry.rect[3] - compositor_plane_carry.rect[1]);
                        compositor_plane_carried(0);
                }
        }
        if (compositor_modesetting)
                return;
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
                        compositor_crtc_sync();
                }
                else
                {
                        // Refused once (the card is still on the flip before, EBUSY): the screen keeps the buffer it shows and the frame is
                        // composed again into the other from the next present. Each buffer's stale holds what it misses (the damage of this
                        // present is in the one the screen shows), so nothing is lost; a modeset on every refusal would flicker the screen.
                        // That next present is asked for here, with this one's rectangles as its damage: it is made once the flip in flight
                        // lands. Where none was asked, a client that drew its last frame and went idle had that frame composed and never
                        // shown: the screen kept the one before (a page's dialog over the page as it was before it, the wallpaper through a
                        // browser's blank first frame) until anything else moved, a pointer over the desktop.
                        compositor_plate_swap();
                        for (unsigned int at = 0; at < clips; at++)
                                compositor_damage(clip[at][0], clip[at][1], clip[at][2] - clip[at][0], clip[at][3] - clip[at][1]);
                        if (!clips)
                                compositor_damage_space();
                }
        }
        else if (back && compositor.on_screen != compositor.fb && !compositor_flip(compositor.fb, -1, 0))
        {
                compositor_record_refuse();
                compositor_crtc_sync();
        }
}

#ifdef CONFIG_MOONWATER_WAYLAND
static void compositor_wl_presented(void); // wayland_host.c: a drawn frame's feedbacks, answered (or held for its flip)
static void compositor_wl_frame(void);     // and its callbacks, and the buffers its pictures replaced given back
#endif

/*
        A present whose jobs went to the card is recorded when they are done (compositor_card_done): the record, the film, a shot and the
        keys' trace read the scanout it composes into, and read when the present was handed to the card they took what that buffer held
        from two presents before (the card had not drawn it yet): a frame on the screen whole was filmed as an older one, a flash that never
        was.
*/
static struct
{
        unsigned long long began;
        enum compositor_record_path path;
        int wanted;
} compositor_card_record;

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
                unsigned int id = 0;

                compositor_fence_prop = -1;
                if (compositor.card >= 0)
                        compositor_overlay_ready();
                if (compositor.card >= 0 && compositor.primary && compositor.primary_props[COMPOSITOR_PLANE_FB])
                        id = drm_obj_get_prop(compositor.primary, COMPOSITOR_OBJECT_PLANE, "IN_FENCE_FD", 0);
                if (id)
                        compositor_fence_prop = (int)id;
        }
        return compositor_fence_prop > 0 ? (unsigned int)compositor_fence_prop : 0;
}

static void compositor_vrr_retry(void);

static int compositor_flip_fenced(unsigned int fb, int fence)
{
        unsigned int prop = compositor_in_fence();
        struct drm_atomic_req req = {0};
        long refused;
        int vrr_was = compositor_vrr_owed();

        if (!prop || fence < 0 || compositor.card < 0)
                return 0;
        drm_atomic_begin_obj(&req, compositor.primary);
        drm_atomic_add(&req, compositor.primary_props[COMPOSITOR_PLANE_FB], fb);
        drm_atomic_add(&req, prop, (unsigned long)(long)fence);
        // A window on the overlay with a new buffer goes with the plate composed round it (compositor_plane_carry).
        if (compositor_plane_carry.fb)
                compositor_plane_carry_add(&req);
        refused = drm_atomic_commit(&req, COMPOSITOR_FLIP_EVENT | COMPOSITOR_ATOMIC_NONBLOCK, compositor.flip_serial + 1);
        if (refused != 0 && req.obj_count == 2 && refused != -16)
        {
                // Refused with the overlay in it: the plate alone, and the overlay's buffer is committed by the next present.
                compositor_damage(compositor_plane_carry.rect[0], compositor_plane_carry.rect[1],
                                  compositor_plane_carry.rect[2] - compositor_plane_carry.rect[0],
                                  compositor_plane_carry.rect[3] - compositor_plane_carry.rect[1]);
                compositor_plane_carried(0);
                if (vrr_was)
                        compositor_vrr_retry();
                req.obj_count = 1;
                req.prop_count = req.counts[0];
                refused = drm_atomic_commit(&req, COMPOSITOR_FLIP_EVENT | COMPOSITOR_ATOMIC_NONBLOCK, compositor.flip_serial + 1);
        }
        if (refused != 0)
        {
                if (refused != -16) // not a flip still in flight: no fenced commit on this card
                        compositor_fence_prop = -1, compositor_fence_errno = (int)-refused;
                return 0;
        }
        compositor_plane_carried(1);
        compositor.flip_serial++;
        compositor_flip_arm(fb, -1, 0, 0);
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
        if (!compositor.table || !rows_any(&compositor_card.rows))
                return;
        for (int row = rows_next(&compositor_card.rows, 0); row >= 0; row = rows_next(&compositor_card.rows, row + 1))
                if (rows_has(&compositor.held_rows, row) && !(compositor.flip_pending && row == compositor.flip_row))
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

/*
        Whether the loop reads no client this turn. It reads them while the card's jobs run: what the jobs read is kept from the clients by
        compositor_card_reads (the doors' taken, the shim's releases) and from the Wayland host's own writes by compositor_card_reads_picture, until the jobs
        are done or the hang path ends them, and a list that finds the card busy waits for it (compositor_lists_later). A commit, a ping or an X window's
        request waited out the job before, up to GPU_LATE_NS: about a millisecond a composed frame on the box's card idle, more under a game's load.
        The owner's /run/moonwater/clients-held (compositor_card.hold_clients) holds them as it was: while a present's jobs are on the card, or a list's,
        and it began less than GPU_LATE_NS ago (compositor_card_held_until), past which a client not read for a hung job's two seconds would be ended by
        its toolkit's watchdog. The input devices are read whatever this says.
*/
static b32 compositor_card_clients_held(positive now)
{
        return compositor_card.hold_clients && (compositor_card.on || rows_any(&compositor_card.list_rows)) && now < compositor_card_held_until();
}

// The present whose jobs are on the card began at `began`: its time is noted when they are done, and the clients' hold is counted from it.
static void compositor_card_began(positive began)
{
        compositor_card.began = began;
}

// The present's frame given up (a job of it refused, hung or its fence an error): not shown, and drawn again whole by the next present.
static void compositor_card_abandon(void)
{
#ifdef MOONWATER_GPU_C
        gpu_handed_hold = 0;
        if (!gpu.pending)
                gpu_handed_clear();
#endif
        compositor_card_record.wanted = 0; // not shown: nothing of it is recorded
        compositor_plane_carried(0);
        compositor_card_fence_close();
        compositor_card.on = 0;
        if (!compositor_roll.on || compositor_roll.old_wall_stale)
                compositor_roll_wall_drop();
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

// When a sync_file's one fence signalled, on the monotonic clock (the kernel's stamp of it, SYNC_IOC_FILE_INFO with room for one fence): 0 when it has
// not, or the file holds more than one, or the kernel will not say.
static positive compositor_fence_signalled(int fd)
{
        struct
        {
                char object[32], driver[32];
                int status;
                unsigned int flags;
                unsigned long long stamp;
        } fence = {0};
        struct
        {
                char name[32];
                int status;
                unsigned int flags, count, pad;
                unsigned long long fences;
        } info = {0};

        info.count = 1, info.fences = (unsigned long long)address_of fence;
        if (fd < 0 || system_control(fd, 0xc0383e04u, address_of info) != 0 || info.count != 1 || fence.status != 1)
                return 0;
        return fence.stamp;
}

// The card's present taken apart (compositor_metric_hand, _job, _seen) at its end, seen at `now`: nothing when its last job's fence did not say when it
// signalled, or says a time out of the present's span.
static void compositor_card_split(positive now)
{
        positive signalled = compositor_card.fence && compositor_card.fence_of == compositor_card.last ? compositor_fence_signalled(compositor_card.fence - 1) : 0;

        if (!signalled || compositor_card.handed < compositor_card.began || signalled < compositor_card.handed || now < signalled)
                return;
        compositor_frametrace_note('F', -1, signalled);
        compositor_metric_hand[compositor_metric_split_at] = compositor_card.handed - compositor_card.began;
        compositor_metric_job[compositor_metric_split_at] = signalled - compositor_card.handed;
        compositor_metric_seen[compositor_metric_split_at] = now - signalled;
        compositor_metric_split_at = (compositor_metric_split_at + 1) % WINDOWS_METRIC_RUN, compositor_metric_split_count++;
}

// The present's jobs are done: its end (compositor_present_tell, the flip a fenced commit made already left as it is), its time as the card's present,
// and what its clients are told of the frame, held till now: no job reads their pictures any more.
static void compositor_card_done(void)
{
        b64 now = clock_monotonic_nanoseconds();

        if (!compositor_card.failed)
                compositor_card_split(now);
        compositor_card_fence_close();
        compositor_card.on = 0;
        if (compositor_card.failed)
        {
                compositor_card_abandon();
                return;
        }
        if (!compositor_roll.on || compositor_roll.old_wall_stale)
                compositor_roll_wall_drop();
        compositor_card_release();
        compositor_present_tell((const unsigned short (*)[4])compositor_card.clip, compositor_card.clips, compositor_card.turning, compositor_card.composed,
                                compositor_card.flip_own, compositor_card.back, compositor_card.plane_off);
        compositor_present_on_card = 1;
        compositor_metric_note(now - compositor_card.began);
        if (compositor_card_record.wanted)
        {
                compositor_card_record.wanted = 0;
                compositor_record_present(compositor_card_record.began, now, compositor_card_record.path);
        }
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
            compositor.flip_pending || compositor.scaled_direct || compositor.outputs > 1 || !compositor_in_fence())
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
        rows_none(&compositor_card.rows), rows_none(&compositor_card.front1);
        for (unsigned int at = 0; at < windows; at++)
                if (rows[at] < COMPOSITOR_ROWS)
                {
                        rows_add(&compositor_card.rows, rows[at]);
                        if (compositor.front[rows[at]] & 1u)
                                rows_add(&compositor_card.front1, rows[at]);
                }
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
        for (int row = rows_next(&compositor_card.list_rows, 0); row >= 0; row = rows_next(&compositor_card.list_rows, row + 1))
        {
                int done;

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
static void compositor_take(int row);

// The rows whose list waited for the card taken again, now that it is idle (compositor_lists_take).
static b32 compositor_list_wait_turn(int row);
static b32 compositor_card_awaits(void);

static void compositor_lists_later(void)
{
        struct compositor_rows later;

        if (!rows_any(&compositor.list_later))
                return;
        rows_take(&later, &compositor.list_later);
        for (int row = rows_next(&later, 0); row >= 0; row = rows_next(&later, row + 1))
        {
                // Still busy: kept in list_later. A job the loop waits on (a present's, a list's: compositor_card_awaits) ends by its fence, which wakes
                // the loop, or by the hang path at its deadline, so the list waits for it with no turn counted: the loop reads its clients while the job
                // runs, and its turns are theirs. Otherwise a turn it waited is counted toward COMPOSITOR_LIST_WAITS, and at the bound the take refuses it.
                if ((compositor_card.on || !gpu_idle()) && (compositor_card_awaits() || compositor_list_wait_turn(row)))
                {
                        rows_add(&compositor.list_later, row);
                        continue;
                }
                compositor_take(row);
        }
}

static void compositor_card_tick(void)
{
        if (!compositor_card.on && !gpu.pending && gpu_found == GPU_END_NONE)
        {
                compositor_card_lists_settle(false); // no job pending: every list's done, or the card started again
                compositor_card_fence_close();
                compositor_lists_later();
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
        if (compositor_card.on && compositor_card.next == compositor_card.steps)
        {
                int done = gpu_done(compositor_card.last);

                if (done < 0)
                {
                        compositor_card.failed = 1;
                        if (gpu_late_end() != GPU_END_NONE)
                                compositor_gpu_job_failed();
                        compositor_card_done();
                }
                else if (done > 0)
                        compositor_card_done();
        }
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

HOT static void compositor_present(void)
{
        if (!compositor.damages || compositor.dark || compositor.flip_pending || compositor_card_holds())
                return;
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
                compositor_card_began(began), compositor_card.handed = ended;
        else if (!compositor_card.on)
                compositor_metric_note(ended - began);
        path = compositor_present_on_card ? COMPOSITOR_RECORD_CARD : compositor.plane_row >= 0 ? COMPOSITOR_RECORD_PLANE : COMPOSITOR_RECORD_PROCESSOR;
        // Its jobs on the card: recorded when they are done, with the card's composing in the buffer (compositor_card_record).
        if (compositor_card.on && !jobs_before)
        {
                compositor_card_record.began = began, compositor_card_record.path = COMPOSITOR_RECORD_CARD, compositor_card_record.wanted = 1;
                return;
        }
        compositor_record_present(began, ended, path);
}

/*
        The wallpaper kept for the space (compositor_wall): made again only when the space's size, the screens, the theme, the accent or
        the word of a Scene shown changed (wallpaper.c). It is made on a thread of its own (compositor_wall_run), never on the loop: until it
        is done the screen keeps what it showed (the last wallpaper, or the colour), and when it is the loop takes it at its next pass
        (compositor_wall_keep), lets the old one go and damages the space once. A change while one is being made waits for it and is
        made after. With no thread to be had it is made on the loop, as it was. Each screen has the wallpaper of the Scene it shows: the
        moon over its sky, dimmed in the dark look (the moon as it is, its seas 45 percent of its highlands) and ink on paper in the white
        one, with the accent's glow round it; or a picture.
*/
static struct
{
        int ready;            // the words were read
        unsigned int stamp;   // COMPOSITOR_WALL_DIR's change time when they were read, 0 with no such directory
        unsigned int sharpen; // the strength a picture is sharpened by (compositor_wall_strength), read with them
} compositor_wall_rows;

/*
        A picture wallpaper (`canvas wallpaper picture FILE`, which puts a copy of the PNG or JPEG in COMPOSITOR_WALL_DIR, or one of the
        feed's): the file of the word of the Scene a screen shows (registry.c's setting_wallpaper_file). It covers the screen, cut at its long
        sides to the screen's shape, and is made on the wallpaper's thread as the moon is; a JPEG is decoded at the fewest eighths of it that
        still cover the screen (a 50-megapixel original is decoded at a half or a quarter for 4K, straight from its DCT), and a file that is
        not a picture the kit reads, or larger than the limits as it is decoded, is the moon.
*/
#define COMPOSITOR_WALL_DIR SETTING_WALLPAPER_DIR
#define COMPOSITOR_WALL_KEY 9
#define COMPOSITOR_WALL_FILE_MOST (64ul << 20)       // the bytes of a picture's file
#define COMPOSITOR_WALL_PIXELS_MOST (48ul << 20)     // and its pixels as decoded, 8K by 4K and more
#define COMPOSITOR_WALL_SIDE_MOST 16384u

#define COMPOSITOR_WALL_SCREENS WINDOWS_OUTPUTS

static struct compositor_wall_job
{
        thread *worker;          // the thread making it, or null
        b32 done;                // the thread has made it (written last, with release)
        unsigned int *pixels;    // what it makes into, as large as the space, its rows pitch pixels apart (the width in whole 64-byte lines)
        positive bytes;
        int width, height, pitch, count;
        unsigned int accent;
        int screens[COMPOSITOR_WALL_SCREENS][4];
        int dark[COMPOSITOR_WALL_SCREENS]; // each screen's moon: the dark look, or the white
        unsigned int colour[COMPOSITOR_WALL_SCREENS]; // each screen's plain colour (colour:RRGGBB) with its alpha, 0 for none
        unsigned int key[COMPOSITOR_WALL_KEY]; // what it is made from
        positive began, took;    // when it was begun, and how long it took (ns)
        b64 turn;                // the moon's turn it is made at
        int step;                // a step of the turn: the wallpaper as it was with each moon's square made again, and those squares its damage
        const unsigned int *from; // a step's: the wallpaper shown, which it starts from
        int squares[COMPOSITOR_WALL_SCREENS][4]; // each screen's moon's square, in the space
        char picture[COMPOSITOR_WALL_SCREENS][sizeof(SETTING_WALLPAPER_DIR "/curated/") + SETTING_WALLPAPER_WORD + 4]; // each screen's picture's file, "" for the moon
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
static int compositor_wall_pictured;
static unsigned int compositor_wall_key[COMPOSITOR_WALL_KEY];
static positive compositor_wall_bytes;

static void compositor_wall_drop(void)
{
        void *old = compositor_wall;
        unsigned long old_bytes = (unsigned long)compositor_wall_bytes;

        mem_buf_free(&old, &old_bytes, MEM_BUF_GPU_FORGET);
        compositor_wall = null, compositor_wall_bytes = 0, compositor_wall_w = compositor_wall_h = compositor_wall_pitch = 0;
}

static void compositor_wall_say(const char *why, int width, int height, positive ms)
{
        char line[120];
        positive at;

        if (why)
        {
                string_copy((string_address)line, (string_address) "compositor: wallpaper plain because ");
                string_append_bounded((string_address)line, (string_address)why, sizeof(line));
        }
        else
        {
                string_copy((string_address)line, (string_address)(compositor_wall_pictured ? "compositor: wallpaper picture " : "compositor: wallpaper moon "));
                at = string_length((string_address)line);
                at += positive_into_string((p8 *)line + at, (positive)width);
                line[at++] = 'x';
                at += positive_into_string((p8 *)line + at, (positive)height);
                string_copy((string_address)line + at, (string_address) " made in ");
                at = string_length((string_address)line);
                at += positive_into_string((p8 *)line + at, ms);
                string_copy((string_address)line + at, (string_address) " ms");
        }
        compositor_kmsg((string_address)line, 0, (string_address) "");
}

static unsigned int compositor_wall_look(const char *path, unsigned int *stamp)
{
        p8 info[256] = {0};

        if (compositor_call(syscall(statx), (positive)AT_FDCWD, (positive)path, 0x100, 0x41, (positive)info, 0) < 0)
                return 0;
        if (stamp)
                *stamp = (unsigned int)(memory_load_unaligned(b64, info + 112) * 1000003) ^ memory_load_unaligned(p32, info + 120);
        return memory_load_unaligned(p16, info + 28) & 0170000u;
}

// The words of the Scenes shown (wallpaper.c), and a word's picture's file into path ("" for the moon's words).
static const char *compositor_wall_shown(int screen);
static unsigned int compositor_walls_gen(void);
static int compositor_wall_picture_of(const char *word, char *path, positive room)
{
        if (setting_wallpaper_file(word, ".png", path, room) && compositor_wall_look(path, null) == 0100000u)
                return 1;
        if (setting_wallpaper_file(word, ".jpg", path, room) && compositor_wall_look(path, null) == 0100000u)
                return 1;
        return path[0] = 0, 0;
}

static b32 ui_image_measure(const p8 *file, positive size, unsigned int most, unsigned int want_w, unsigned int want_h, unsigned int *width,
                            unsigned int *height);
static unsigned int *ui_image_decode_clean(const p8 *file, positive size, int *width_out, int *height_out, unsigned int most, unsigned int want_w,
                                           unsigned int want_h);
static b32 ui_picture_for_screen(unsigned int *into, positive pitch, positive w, positive h, const unsigned int *picture, positive width,
                                 positive height, unsigned int strength);

/*
        What a picture was made into for a screen's size is kept, so the decode, the cleaning, the resampling and the sharpening (a few
        hundred milliseconds for a 50-megapixel original) are done once for each size: COMPOSITOR_WALL_CACHE_DIR/KEY, KEY 16 hex digits
        of the FNV-1a hash of the picture's path, its file's change stamp, the size, the strength and COMPOSITOR_WALL_CACHE_VERSION;
        the file "MWWP", w, h and the strength (32 bits each) and then the w by h pixels, a row after a row. The directory is on tmpfs,
        so a boot makes each again; a file is written whole under a name of its own and renamed over the key, so a reader finds the
        whole of one or none, and one whose head is not the size and strength asked is made again. The learned stage's state is in
        the key too (its licence and its model file's stamp, enhance.c's), so a model fetched later, or the stage turned, makes it again.
*/
#define COMPOSITOR_WALL_CACHE_DIR "/run/moonwater/wallpaper"
#define COMPOSITOR_WALL_CACHE_VERSION 1u
#define COMPOSITOR_WALL_CACHE_KEEP 4 // the files kept at the most (a 4K picture is 33 MB of memory): more screens make theirs again

/*
        The directory held to the COMPOSITOR_WALL_CACHE_KEEP files changed last, the one just written among them: each other is taken
        away, the oldest first, so the pictures of screens and Scenes no longer shown do not stay in memory (tmpfs) for the session.
*/
static void compositor_wall_cache_trim(void)
{
        p8 buffer[4096];
        struct
        {
                char name[17];
                b64 when;
        } kept[COMPOSITOR_WALL_CACHE_KEEP + 1];
        unsigned int count = 0;
        bipolar handle = system_open_at(AT_FDCWD, (string_address)COMPOSITOR_WALL_CACHE_DIR, FILE_READ | O_DIRECTORY | O_CLOEXEC), got;

        if (handle < 0)
                return;
        while ((got = system_call_3(syscall(getdents64), (positive)handle, (positive)buffer, sizeof(buffer))) > 0)
                for (bipolar at = 0; at < got;)
                {
                        const char *name = (const char *)(buffer + at + 19);
                        p8 facts[256] = {0};
                        b64 when;
                        unsigned int oldest = 0;

                        at += memory_load_unaligned(p16, buffer + at + 16);
                        if (string_length((string_address)name) != 16 ||
                            compositor_call(syscall(statx), (positive)handle, (positive)name, 0x100, 0x41, (positive)facts, 0) < 0)
                                continue;
                        when = memory_load_unaligned(b64, facts + 112) * 1000000000ll + memory_load_unaligned(p32, facts + 120);
                        if (count < COMPOSITOR_WALL_CACHE_KEEP)
                        {
                                memory_copy(kept[count].name, name, 17), kept[count++].when = when;
                                continue;
                        }
                        // Full: whichever is older, this one or the oldest kept, goes.
                        for (unsigned int k = 1; k < count; k++)
                                oldest = kept[k].when < kept[oldest].when ? k : oldest;
                        if (when < kept[oldest].when)
                                (void)system_remove_at(handle, (string_address)name, 0);
                        else
                        {
                                (void)system_remove_at(handle, (string_address)kept[oldest].name, 0);
                                memory_copy(kept[oldest].name, name, 17), kept[oldest].when = when;
                        }
                }
        system_close(handle);
}

// The strength kept (SETTING_WALLPAPER_SHARPEN_PATH, 0 to 100), the default when none is or it says something else.
static unsigned int compositor_wall_strength(void)
{
        char word[16];
        long got = windows_read_line(SETTING_WALLPAPER_SHARPEN_PATH, word, sizeof(word), FILE_READ | O_CLOEXEC | O_NONBLOCK | O_NOFOLLOW);
        unsigned int value = 0;
        long at = 0;

        while (at < got && word[at] >= '0' && word[at] <= '9' && value <= 100)
                value = value * 10 + (unsigned int)(word[at++] - '0');
        return at && at == got && value <= 100 ? value : SETTING_WALLPAPER_SHARPEN_DEFAULT;
}

static void compositor_wall_cache_name(char *into, const char *path, unsigned int stamp, int w, int h, unsigned int strength, unsigned int stage)
{
        p64 hash = 14695981039346656037ull;
        unsigned int words[6] = {stamp, (unsigned int)w, (unsigned int)h, strength, stage, COMPOSITOR_WALL_CACHE_VERSION};
        positive at = sizeof(COMPOSITOR_WALL_CACHE_DIR "/") - 1;

        for (const char *c = path; *c; c++)
                hash = (hash ^ (p8)*c) * 1099511628211ull;
        for (positive k = 0; k < sizeof(words); k++)
                hash = (hash ^ ((const p8 *)words)[k]) * 1099511628211ull;
        memory_copy(into, COMPOSITOR_WALL_CACHE_DIR "/", at);
        for (int digit = 0; digit < 16; digit++)
                into[at + (positive)digit] = "0123456789abcdef"[hash >> (60 - 4 * digit) & 15];
        into[at + 16] = 0;
}

static int compositor_wall_cache_read(const char *name, unsigned int *into, int pitch, int w, int h, unsigned int strength)
{
        bipolar handle = system_open_at(AT_FDCWD, (string_address)name, FILE_READ | O_NOFOLLOW | O_CLOEXEC);
        p32 head[4];
        int whole = 0;

        if (handle < 0)
                return 0;
        if (system_read_retry((positive)handle, (p8 *)head, sizeof(head)) == (bipolar)sizeof(head) && !memory_compare(head, "MWWP", 4) &&
            head[1] == (p32)w && head[2] == (p32)h && head[3] == strength)
        {
                whole = 1;
                for (int y = 0; whole && y < h; y++)
                        whole = system_read_retry((positive)handle, (p8 *)(into + (long)y * pitch), (positive)w * 4) == (bipolar)((positive)w * 4);
        }
        system_close(handle);
        return whole;
}

static void compositor_wall_cache_write(const char *name, const unsigned int *from, int pitch, int w, int h, unsigned int strength)
{
        char made[sizeof(COMPOSITOR_WALL_CACHE_DIR "/") + 16 + 8];
        p32 head[4] = {0, (p32)w, (p32)h, strength};
        bipolar handle;
        int whole;

        memory_copy(head, "MWWP", 4);
        (void)compositor_call(syscall(mkdirat), (positive)AT_FDCWD, (positive)COMPOSITOR_WALL_CACHE_DIR, 0755, 0, 0, 0);
        string_copy((string_address)made, (string_address)name);
        string_append_bounded((string_address)made, (string_address) ".new", sizeof(made));
        handle = system_open_at_mode(AT_FDCWD, (string_address)made, FILE_WRITE | O_CREAT | O_TRUNC | O_NOFOLLOW | O_CLOEXEC, 0644);
        if (handle < 0)
                return;
        whole = system_write_all((b32)handle, (p8 *)head, sizeof(head)) == (bipolar)sizeof(head);
        for (int y = 0; whole && y < h; y++)
                whole = system_write_all((b32)handle, (p8 *)(from + (long)y * pitch), (positive)w * 4) == (bipolar)((positive)w * 4);
        system_close(handle);
        if (whole)
                (void)compositor_call(syscall(renameat2), (positive)AT_FDCWD, (positive)made, (positive)AT_FDCWD, (positive)name, 0, 0);
        else
                (void)system_remove_at(AT_FDCWD, (string_address)made, 0);
        compositor_wall_cache_trim();
}

#include "../enhance.c" // the learned stage: restore and double the decoded picture, grade the screen's (no model, no stage)

static int compositor_wall_picture(const char *path, unsigned int *into, int pitch, int w, int h)
{
        bipolar handle;
        p8 *file;
        positive got = 0;
        unsigned int *picture = null, pw = 0, ph = 0, stamp = 0, strength = compositor_wall_strength();
        int width = 0, height = 0;
        char kept[sizeof(COMPOSITOR_WALL_CACHE_DIR "/") + 16 + 8];
        unsigned int stage = 0;

        // Made for this size already, from this file as it is now (and the learned stage as it is now).
        compositor_wall_look(path, &stamp);
#ifdef ENHANCE_MODEL_DIR
        compositor_wall_look(ENHANCE_MODEL_PATH, &stage);
        stage ^= (unsigned int)(enhance_wallpaper_licence() + 1) * 2654435761u;
#endif
        compositor_wall_cache_name(kept, path, stamp, w, h, strength, stage);
        if (compositor_wall_cache_read(kept, into, pitch, w, h, strength))
                return 1;
        handle = system_open_at(AT_FDCWD, (string_address)path, FILE_READ | O_NOFOLLOW | O_CLOEXEC);
        if (handle < 0)
                return 0;
        file = (p8 *)memory_checked(COMPOSITOR_WALL_FILE_MOST);
        while (file && got < COMPOSITOR_WALL_FILE_MOST)
        {
                bipolar read = system_read_once(handle, file + got, COMPOSITOR_WALL_FILE_MOST - got);

                if (read <= 0)
                        break;
                got += (positive)read;
        }
        system_close(handle);
        if (!file)
                return 0;
        if (got < COMPOSITOR_WALL_FILE_MOST && ui_image_measure(file, got, COMPOSITOR_WALL_SIDE_MOST, (unsigned int)w, (unsigned int)h, &pw, &ph) &&
            (positive)pw * ph <= COMPOSITOR_WALL_PIXELS_MOST)
                picture = ui_image_decode_clean(file, got, &width, &height, COMPOSITOR_WALL_SIDE_MOST, (unsigned int)w, (unsigned int)h);
        memory_free(file, COMPOSITOR_WALL_FILE_MOST);
        if (!picture)
                return 0;
#ifdef ENHANCE_MODEL_DIR
        enhance_model enhancer;
        bipolar licence;
        bool enhancing = enhance_wallpaper_begin(&enhancer, &licence);

        if (enhancing)
                picture = enhance_wallpaper_picture(&enhancer, picture, &width, &height, w, h);
#endif
        // Cut to the screen's shape, made its size in linear light and sharpened (ui.c's ui_picture_for_screen).
        if (!ui_picture_for_screen(into, (positive)pitch, (positive)w, (positive)h, picture, (positive)width, (positive)height, strength))
        {
#ifdef ENHANCE_MODEL_DIR
                if (enhancing)
                        enhance_model_drop(&enhancer);
#endif
                memory_free(picture, (positive)width * (positive)height * 4);
                return 0;
        }
#ifdef ENHANCE_MODEL_DIR
        if (enhancing)
                enhance_grade(&enhancer, into, (positive)pitch, (positive)w, (positive)h, (f64)licence / 100.0, 0),
                    enhance_model_drop(&enhancer);
#endif
        compositor_wall_cache_write(kept, into, pitch, w, h, strength);
        memory_free(picture, (positive)width * (positive)height * 4);
        return 1;
}

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
                if (job->colour[at])
                {
                        // A plain colour: the screen's rect filled (a moon's step keeps what it was copied from).
                        memory_zero(job->squares[at], sizeof(job->squares[at]));
                        if (!job->step)
                                memory_fill_u32_rect(job->pixels + (long)y * job->pitch + x, (positive)job->pitch, (positive)w, (positive)h, job->colour[at]);
                        continue;
                }
                if (job->picture[at][0])
                {
                        unsigned int *corner = job->pixels + (long)y * job->pitch + x;

                        memory_zero(job->squares[at], sizeof(job->squares[at]));
                        if (job->step || compositor_wall_picture(job->picture[at], corner, job->pitch, w, h))
                                continue;
                }
                taskbar_wall_draw(job->pixels + (long)y * job->pitch + x, job->pitch, w, h, job->dark[at], 1, 45, 1, job->accent,
                                  job->turn, job->step, job->squares[at < COMPOSITOR_WALL_SCREENS ? at : 0]);
                job->squares[at][0] += x, job->squares[at][1] += y;
        }
}

static int compositor_wall_knock = -1;
static int compositor_wall_deferred;

static fn compositor_wall_run(address_any context)
{
        struct compositor_wall_job *job = (struct compositor_wall_job *)context;
        unsigned long one = 1;

        compositor_ordinary(10);
        compositor_wall_make(job);
        job->took = clock_monotonic_nanoseconds() - job->began;
        __atomic_store_n(&job->done, 1, __ATOMIC_RELEASE);
        thread_wake(&job->done, 1);
        if (compositor_wall_knock >= 0)
                compositor_call(syscall(write), (positive)compositor_wall_knock, (positive)&one, sizeof one, 0, 0, 0);
}

static unsigned int compositor_wall_gen;

static void compositor_wall_take(void)
{
        struct compositor_wall_job *job = &compositor_wall_job;

        if (job->worker)
                thread_join(job->worker), job->worker = null;
        if (compositor_roll.old_wall_stale)
                compositor_roll_wall_drop();
        if (compositor_roll.on && !compositor_roll.from_fs && !compositor_roll.old_wall && compositor_wall)
        {
                compositor_roll.old_wall = compositor_wall;
                compositor_roll.old_wall_bytes = compositor_wall_bytes;
                compositor_roll.old_wall_w = compositor_wall_w;
                compositor_roll.old_wall_h = compositor_wall_h;
                compositor_roll.old_wall_pitch = compositor_wall_pitch;
                compositor_wall = null, compositor_wall_bytes = 0, compositor_wall_w = compositor_wall_h = compositor_wall_pitch = 0;
        }
        else
                compositor_wall_drop();
        compositor_wall = job->pixels, compositor_wall_bytes = job->bytes, compositor_wall_w = job->width, compositor_wall_h = job->height, compositor_wall_pitch = job->pitch;
        compositor_wall_gen++;
        memory_copy(compositor_wall_key, job->key, sizeof(compositor_wall_key));
        job->pixels = null, job->done = 0, compositor_wall_turn = job->turn;
        if (job->step)
        {
                for (int at = 0; at < (job->count ? job->count : 1); at++)
                        compositor_damage(job->squares[at][0], job->squares[at][1], job->squares[at][2], job->squares[at][3]);
                return;
        }
        compositor_wall_pictured = 0;
        for (int at = 0; at < (job->count ? job->count : 1) && at < COMPOSITOR_WALL_SCREENS; at++)
                compositor_wall_pictured |= job->picture[at][0] != 0;
        compositor_wall_say(null, compositor_wall_w, compositor_wall_h, job->took / 1000000);
        compositor_damage(0, 0, compositor.width, compositor.height);
}

static positive compositor_wall_step_now(void)
{
        timespec real = {0, 0};

        clock_gettime(CLOCK_REALTIME, address_of real);
        return ((positive)real.tv_sec * 1000000000ull + (positive)real.tv_nsec) / COMPOSITOR_WALL_STEP;
}

static b64 compositor_wall_turn_of(positive step)
{
        return (b64)(step % COMPOSITOR_WALL_TURN_STEPS) * UI_ONE / COMPOSITOR_WALL_TURN_STEPS;
}

static int compositor_wall_making(void)
{
        return compositor_wall_job.pixels != null;
}

static positive compositor_wall_due(positive now)
{
        // A window shown straight covers the wallpaper: its moon's step (a picture drawn on the processor, on threads of its own) is not
        // made while nobody can see it, and comes once when the window goes (compositor_wall_keep).
        if (compositor_space_all_fullscreen() || compositor.direct_row >= 0)
                return 0;
        if (compositor_wall_deferred && compositor.presented)
                return now;
        if (compositor_wall_job.pixels && compositor_wall_knock < 0)
                return now + (compositor.period ? compositor.period : 16666667ull);
        if (compositor_wall && !compositor_wall_job.pixels && taskbar_motion_about(now))
        {
                timespec real = {0, 0};
                positive at;

                clock_gettime(CLOCK_REALTIME, address_of real);
                at = (positive)real.tv_sec * 1000000000ull + (positive)real.tv_nsec;
                return at / COMPOSITOR_WALL_STEP != compositor_wall_stepped ? now : now + COMPOSITOR_WALL_STEP - at % COMPOSITOR_WALL_STEP + 1000000ull;
        }
        return 0;
}

static void compositor_wall_start(b64 turn, int step)
{
        struct compositor_wall_job *job = &compositor_wall_job;
        unsigned int count = 0;
        const int (*screens)[4] = compositor_screens(&count);
        void *p = 0;
        unsigned long cap = 0;

        job->pitch = (compositor.width + 15) & ~15;
        job->bytes = (positive)job->pitch * (positive)compositor.height * 4;
        if (!mem_buf_grow(&p, &cap, (unsigned long)job->bytes, 0))
        {
                if (!step)
                        compositor_wall_say("there was no memory for it", 0, 0, 0), memory_copy(compositor_wall_key, job->key, sizeof(compositor_wall_key));
                return;
        }
        job->pixels = (unsigned int *)p, job->bytes = (positive)cap, job->from = step ? compositor_wall : null;
        if (step)
                memory_copy(job->key, compositor_wall_key, sizeof(job->key));
        job->width = compositor.width, job->height = compositor.height, job->count = count < COMPOSITOR_WALL_SCREENS ? (int)count : COMPOSITOR_WALL_SCREENS;
        for (int at = 0; at < job->count; at++)
                memory_copy(job->screens[at], screens[at], sizeof(job->screens[at]));
        for (int at = 0; at < (job->count ? job->count : 1) && at < COMPOSITOR_WALL_SCREENS; at++)
        {
                const char *word = compositor_wall_shown(at);

                long colour = setting_wallpaper_colour(word);

                job->dark[at] = setting_wallpaper_dark(word, !(compositor.table && compositor.table->themed && compositor.table->theme == UI_THEME_LIGHT));
                job->colour[at] = colour < 0 ? 0 : 0xff000000u | (unsigned int)colour;
                compositor_wall_picture_of(word, job->picture[at], sizeof(job->picture[at]));
        }
        job->accent = taskbar_wall_accent(), job->done = 0, job->began = clock_monotonic_nanoseconds(), job->took = 0;
        job->turn = turn, job->step = step;
        if (!(job->worker = thread_start(compositor_wall_run, (address_any)job)))
                compositor_wall_make(job), job->took = clock_monotonic_nanoseconds() - job->began, compositor_wall_take();
}

static void compositor_wall_keep(void)
{
        struct compositor_wall_job *job = &compositor_wall_job;
        unsigned int count = 0, now[COMPOSITOR_WALL_KEY], words = 2166136261u;
        const int (*screens)[4] = compositor_screens(&count);
        int dark = !(compositor.table && compositor.table->themed && compositor.table->theme == UI_THEME_LIGHT);

        if (!compositor_wall_rows.ready || compositor_space_all_fullscreen())
                return;
        compositor_wall_deferred = 0;
        if (compositor.scanout && !compositor.presented)
        {
                compositor_wall_deferred = 1;
                return;
        }
        if (job->pixels)
        {
                if (__atomic_load_n(&job->done, __ATOMIC_ACQUIRE))
                        compositor_wall_take();
                return;
        }
        now[0] = (unsigned int)compositor.width, now[1] = (unsigned int)compositor.height, now[2] = count, now[3] = (unsigned int)dark;
        now[4] = taskbar_wall_accent();
        // The words of the Scenes shown, a screen's after another's: a Scene switched to with another word is another wallpaper.
        for (unsigned int at = 0; at < (count ? count : 1) && at < COMPOSITOR_WALL_SCREENS; at++)
        {
                const char *word = compositor_wall_shown((int)at);

                words = hash_fnv1a(words * 16777619u, word, string_length((string_address)word));
        }
        now[5] = compositor_wall_rows.stamp ^ compositor_ground << 7 ^ compositor_wall_rows.sharpen * 2654435761u, now[6] = compositor_walls_gen();
        now[7] = count ? (unsigned int)(screens[0][2] * 31 + screens[0][3]) : 0, now[8] = words;
        if (!memory_compare(compositor_wall_key, now, sizeof(now)))
        {
                if (compositor_wall)
                {
                        positive clock = clock_monotonic_nanoseconds();
                        if (compositor.direct_row < 0 && taskbar_motion_about(clock))
                        {
                                positive step = compositor_wall_step_now();
                                if (step != compositor_wall_stepped)
                                        compositor_wall_stepped = step, compositor_wall_start(compositor_wall_turn_of(step), 1);
                        }
                }
                return;
        }
        if (compositor.width <= 0 || compositor.height <= 0)
        {
                compositor_wall_say("the space has no screen yet", 0, 0, 0);
                memory_copy(compositor_wall_key, now, sizeof(now));
                compositor_wall_drop();
                compositor_damage(0, 0, compositor.width, compositor.height);
                return;
        }
        if (compositor_wall && (compositor_wall_w != compositor.width || compositor_wall_h != compositor.height))
                compositor_wall_drop(), compositor_damage(0, 0, compositor.width, compositor.height);
        if (!compositor_wall_stepped || taskbar_motion_about(clock_monotonic_nanoseconds()))
                compositor_wall_stepped = compositor_wall_step_now(), compositor_wall_turn = compositor_wall_turn_of(compositor_wall_stepped);
        memory_copy(job->key, now, sizeof(now));
        compositor_wall_start(compositor_wall_turn, 0);
}

static void compositor_wall_finish(void)
{
        if (!compositor_wall_job.pixels)
                return;
        while (!__atomic_load_n(&compositor_wall_job.done, __ATOMIC_ACQUIRE))
                thread_wait(&compositor_wall_job.done, 0);
        compositor_wall_take();
}

// The frames of a present and the row of each: the rows' (compositor_rows_make), as many as the rows in use have.
static struct compositor_frame *compositor_present_frame_room;
static unsigned int *compositor_present_row_room;

static void present_direct_cleared(void)
{
        compositor_unscanned(0);
        compositor.plane_row = -1, compositor.plane_fb = 0, compositor.plane_gone = 0;
}

// The Scenes overview (scenes.c): whether it is drawn, the rows it draws itself or hides, and its frames.
static int compositor_scenes_shown(void);
static int compositor_scenes_hides(int row);
static unsigned int compositor_scenes_frames(struct compositor_frame *frames, unsigned int *rows, unsigned int at, unsigned int room);
// The wallpaper's editor (wallpaper.c): whether it is drawn, and its frames, the veil over its screen's windows and its cards.
static int compositor_wall_editor_shown(void);
static unsigned int compositor_wall_editor_frames(struct compositor_frame *frames, unsigned int *rows, unsigned int at, unsigned int room);

// Whether the overview or the wallpaper's editor is drawn: the screen is composed, no window shown straight from its buffer or on a plane.
static int compositor_overview_drawn(void)
{
        return compositor_scenes_shown() || compositor_wall_editor_shown();
}

// The stack's frames, a window's picture and its layers each (a fullscreen window fitted to its screen with the black round it first).
// While the Scenes overview is drawn, the rows on its screen are not drawn where they are: the overview's frames are put where the
// first of them would have been, and draw them at the overview's places.
static unsigned int present_build_frames(struct compositor_frame *frames, unsigned int *rows)
{
        unsigned int count = 0, room = (unsigned int)COMPOSITOR_ROWS_LAYERED(compositor_rows_in_use);
        int scenes = compositor_scenes_shown(); // 1: the overview's frames are still to come; 2: they are in

        if (compositor_roll.on && !scenes)
                count += compositor_roll_wall_frames(frames, rows, count, room);
        for (unsigned int at = 0; at < compositor.stacked; at++)
                if (compositor_visible(compositor.stack[at]) || compositor_roll_holds((int)compositor.stack[at]))
                {
                        int row = (int)compositor.stack[at], dx, dy, dw, dh;

                        if (scenes && compositor_scenes_hides(row))
                        {
                                if (scenes == 1)
                                        count += compositor_scenes_frames(frames, rows, count, room), scenes = 2;
                                continue;
                        }
                        if (count >= room)
                                break;
                        if (compositor_fullscreen_fit(row, &dx, &dy, &dw, &dh) &&
                            (dw < (int)compositor.w[row] || dh < (int)compositor.h[row]) &&
                            count + 1 < room)
                        {
                                struct compositor_frame *back = &frames[count];

                                memory_zero(back, sizeof(*back));
                                back->x = compositor.x[row], back->y = compositor.y[row];
                                back->width = (int)compositor.w[row], back->height = (int)compositor.h[row];
                                back->scale = compositor_row_scale(row), back->fill = 0xff000000u;
                                compositor_roll_frame(row, back);
                                rows[count++] = (unsigned int)row;
                        }
                        rows[count] = compositor.stack[at];
                        compositor_row_prepare(compositor.stack[at]);
                        compositor_frame_of(compositor.stack[at], &frames[count++]);
                        if (count + 8u <= room)
                                count += compositor_wl_layers_frames(compositor.stack[at], frames, rows, count);
                }
        if (scenes == 1)
                count += compositor_scenes_frames(frames, rows, count, room);
        // The editor over every window, under the band: its veil and its cards.
        count += compositor_wall_editor_frames(frames, rows, count, room);
        return count;
}

static unsigned int present_bands_pass(struct compositor_frame *frames, unsigned int windows, int direct, int stay_live_direct)
{
        if (taskbar_shift() >= UI_ONE)
                return 0;
        int stale = taskbar_stale(), bands = compositor_bands();
        const unsigned int *saved_direct_pixels = 0;
        struct compositor_frame band, band_kept[WINDOWS_OUTPUTS];
        struct taskbar_view view_kept[WINDOWS_OUTPUTS];
        unsigned int added = 0;

        for (int band_idx = 0; band_idx < bands; band_idx++)
        {
                struct taskbar_view view;
                unsigned long redo = stale ? COMPOSITOR_TABS_ALL : 0;
                unsigned char solid;
                unsigned int at = (unsigned int)band_idx < WINDOWS_OUTPUTS ? (unsigned int)band_idx : 0;

                compositor.band_at = band_idx;
                compositor_band_frame_for(band_idx, &band);
                view = compositor_taskbar_view_for(band_idx);
                if ((unsigned int)band_idx < WINDOWS_OUTPUTS)
                        band_kept[band_idx] = band, view_kept[band_idx] = view;
                solid = (unsigned char)compositor_band_covers(frames, windows, &band);
                if (solid != compositor_band_solid[at])
                        compositor_band_solid[at] = solid, redo = COMPOSITOR_TABS_ALL;
                if (!redo && view.pixels)
                        redo = taskbar_meets(&view, compositor.damage, compositor.damages, solid);
                if (!redo)
                        continue;
                if (!solid && stay_live_direct && !saved_direct_pixels && windows)
                {
                        unsigned int which = compositor.front[direct] & 1u;
                        unsigned int *stage = compositor_direct_stage_ready();

                        if (stage)
                        {
                                unsigned long stride = (unsigned long)compositor_stride();
                                const int *old = compositor.under_rect[which];
                                int down = compositor_glass_reach(COMPOSITOR_GLASS_DOWN) + ((COMPOSITOR_GLASS_LEVELS + 1) << COMPOSITOR_GLASS_LEVELS);
                                int y0 = band.y - down, y1 = band.y + band.height + down;

                                if (old[3] > old[1])
                                {
                                        if (old[1] < y0) y0 = old[1];
                                        if (old[3] > y1) y1 = old[3];
                                }
                                if (y0 < 0) y0 = 0;
                                if (y1 > compositor.height) y1 = compositor.height;
                                saved_direct_pixels = frames[windows - 1].pixels;
                                if (y1 > y0)
                                        memory_copy(stage + (unsigned long)y0 * stride, saved_direct_pixels + (unsigned long)y0 * stride,
                                                    (unsigned long)(y1 - y0) * stride * 4);
                                memory_copy_rect_u32(stage + (unsigned long)old[1] * stride + (unsigned long)old[0], stride,
                                                     compositor.under[which], (unsigned long)(old[2] - old[0]),
                                                     (unsigned long)(old[2] - old[0]), (unsigned long)(old[3] - old[1]));
                                frames[windows - 1].pixels = stage;
                        }
                }
                compositor_band_draw_for(band_idx, frames, windows, redo);
        }
        if (saved_direct_pixels && windows)
                frames[windows - 1].pixels = saved_direct_pixels;
        for (int band_idx = 0; band_idx < bands; band_idx++)
        {
                struct taskbar_view view;

                compositor.band_at = band_idx;
                if ((unsigned int)band_idx < WINDOWS_OUTPUTS)
                        band = band_kept[band_idx], view = view_kept[band_idx];
                else
                        compositor_band_frame_for(band_idx, &band), view = compositor_taskbar_view_for(band_idx);
                added += compositor_moon_glow_frame_for(band_idx, &frames[windows + added]);
                added += compositor_panel_tabs_with(&view, &band, &frames[windows + added]);
        }
        compositor.band_at = 0;
        return added;
}

// Updates hardware overlay plane assignment for this present. Returns 1 if the present is finished/deferred, 0 to compose.
static int present_overlay_plane(int plane, const int planed[4], int turning, int *plane_off)
{
        compositor_plane_carry.fb = 0;
        if (compositor.overlay_owed && !compositor.flip_pending)
                compositor_overlay_off();
        if (compositor.plane_gone || (compositor.plane_row >= 0 && (plane != compositor.plane_row ||
                                                                    memory_compare(planed, compositor.plane_rect, 4 * sizeof(int)) != 0)))
        {
                if (compositor.flip_pending)
                        return 1;
                compositor_damage(compositor.plane_rect[0], compositor.plane_rect[1],
                                  compositor.plane_rect[2] - compositor.plane_rect[0],
                                  compositor.plane_rect[3] - compositor.plane_rect[1]);
                if (plane < 0)
                {
                        if (compositor_spare.fb)
                                *plane_off = 1;
                        else if (!compositor_atomic_to(compositor_primary_fb(), -1, 0, 1, 0, 0, 0))
                                compositor_overlay_off();
                }
                if (compositor.plane_row >= 0)
                        compositor.leaving_row = compositor.plane_row;
                compositor.plane_row = compositor.direct_row = -1, compositor.plane_fb = 0, compositor.plane_gone = 0;
        }
        if (plane < 0)
                return 0;
        {
                int lent = compositor_client_scans(plane), origin[2] = {0, 0};
                unsigned int which = compositor.front[plane];
                unsigned int fb = lent ? compositor_lent_fb_opaque(plane) : compositor_window_fb(plane, which);
                int source[2] = {planed[0] - compositor.x[plane], planed[1] - compositor.y[plane]};

                if (lent)
                        compositor_lent_fb_origin(plane, &origin[0], &origin[1]);
                source[0] += origin[0], source[1] += origin[1];
                if (compositor.flip_pending)
                        return 1;
                compositor_damage_cut(planed);
                if (fb && fb != compositor.plane_fb && compositor.damages && compositor_plate_wanted(turning) && compositor_spare.fb)
                {
                        compositor_plane_carry.fb = fb, compositor_plane_carry.row = plane, compositor_plane_carry.which = which;
                        memory_copy(compositor_plane_carry.source, source, sizeof(source));
                        memory_copy(compositor_plane_carry.rect, planed, sizeof(compositor_plane_carry.rect));
                }
                else if (!fb || (fb != compositor.plane_fb && !compositor_atomic_to(compositor.fb, plane, which, 1, fb, source, planed)))
                {
                        if (lent)
                                compositor_lent_fb_fail(plane);
                        else
                        {
                                compositor_unscanned(12);
                                if (compositor_refusal_fatal())
                                        compositor.overlay_state = -1;
                                compositor_overlay_off();
                        }
                        compositor.plane_row = compositor.direct_row = -1;
                        compositor_damage(planed[0], planed[1], planed[2] - planed[0], planed[3] - planed[1]);
                        compositor_wl_straight_pays(-1);
                }
                else
                {
                        compositor.plane_fb = fb, compositor_unscanned(0), compositor.plane_row = compositor.direct_row = plane;
                        memory_copy(compositor.plane_rect, planed, sizeof(compositor.plane_rect));
                }
                if (compositor.plane_row == plane)
                        compositor.table->scanned++;
                return compositor.plane_row == plane && !compositor.damages;
        }
}

static unsigned int present_compose_damage(const struct compositor_frame *frames, unsigned int count,
                                           int gpu_drawn, int apart, unsigned short clip[COMPOSITOR_DAMAGE][4])
{
        struct compositor_apart slices = {compositor_slice_flush, compositor.composing, (unsigned long)compositor.width};
        unsigned int clips = 0;

        for (unsigned int at = 0; at < compositor.damages; at++)
        {
                const int *rect = compositor.damage[at];
                struct compositor_target target = {
                    compositor.scanout, compositor.scanout_pitch / 4, compositor.width,
                    compositor.height, rect[0], rect[1], rect[2], rect[3]};
                int rows[2];

#ifdef MOONWATER_GPU_C
                if (gpu_drawn)
                {
                        struct compositor_target strip = target;

                        rows[0] = rect[1], rows[1] = rect[3] - 1;
                        strip.x1 = rect[0] > (compositor.width & ~3) ? rect[0] : compositor.width & ~3;
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
                        (void)gpu_drawn;
                        compositor_scene_banded(&target, frames, count, apart ? &slices : 0, rows);
                        if (!apart && compositor_spots_any(0))
                                compositor_spots_compose(&target, frames, count);
                        if (!apart)
                                compositor_cursor_compose(&target, frames, count);
                }
                compositor.painted += (unsigned long)compositor_area(rect);
                if (apart && rows[0] < 0)
                        continue;
                for (unsigned int side = 0; side < 4; side++)
                        clip[clips][side] = (unsigned short)rect[side];
                if (apart)
                        clip[clips][1] = (unsigned short)rows[0], clip[clips][3] = (unsigned short)(rows[1] + 1);
                clips++;
        }
        return clips;
}

/*
        A present composed while a fullscreen window is up: counted, and by why that window was not shown straight (compositor_direct_why,
        or the screen turned, the Scenes overview drawn, or another window over it), for moonwater debug's "fullscreen composed".
*/
static void present_count_composed(int direct, int turning, const unsigned int *rows, unsigned int windows)
{
        int fs = compositor_fullscreen_top();
        unsigned int why = compositor_direct_why;

        if (!compositor.table || fs < 0 || !compositor_visible(fs))
                return;
        if (turning || compositor_overview_drawn())
                why = COMPOSITOR_DIRECT_SCENE;
        else if (!windows || rows[windows - 1] != (unsigned int)fs)
                why = COMPOSITOR_DIRECT_NOT_TOP;
        else if (direct >= 0 && why == COMPOSITOR_DIRECT_SHOWN)
                why = COMPOSITOR_DIRECT_REFUSED;
        compositor.table->composed_fullscreen++;
        compositor.table->direct_why[why < WINDOWS_DIRECT_WHY ? why : 0]++;
}

static int compositor_cast_has_stale(void);

static void compositor_present_frames(void)
{
        struct compositor_frame *frames = compositor_present_frame_room, preview;
        unsigned short clip[COMPOSITOR_DAMAGE][4];
        unsigned int *rows = compositor_present_row_room, count, windows, scenes = 0, clips;
        int tinted, turning, direct, stay_live_direct, back, planed[4], plane, plane_off = 0, flip_own = 0;
        unsigned int over; // the first frame over the window shown straight: the windows' end, or the first window over it (compositor_direct_under)
        int scene[COMPOSITOR_DAMAGE][4];

        if (!compositor.damages || compositor.dark || compositor.flip_pending || compositor_card_holds())
                return;
        // Steady-state fullscreen direct-scanout fast path.
        if (compositor.direct_row >= 0 && compositor_visible(compositor.direct_row) && !compositor_turning() &&
            (compositor_space_is_fs_row(compositor.direct_row) ||
             (compositor.stacked > 0 && (int)compositor.stack[compositor.stacked - 1] == compositor.direct_row)) &&
            !compositor.overlay_on && !compositor_overlays_shown() && !compositor_overview_drawn() && !compositor_roll.on && taskbar_shift() >= UI_ONE &&
            !compositor_record_until && !compositor_cast_screens() && !compositor_cast_has_stale())
        {
                int row = compositor.direct_row, menu[4];

                if (!taskbar_menu_rect(menu) && compositor_client_scans(row) && !compositor_busy_wait() && !compositor_row_shaped(row) &&
                    compositor_direct_eligible(row, compositor_row_opaque_fs(row) ? 0 : compositor_wl_row_blend(row)) &&
                    compositor_present_direct(row, 0, 0))
                        return present_direct_cleared();
        }
        compositor_bands_follow();
        compositor_wl_gathers_fill();
        if (compositor.table)
        {
                static unsigned int driver_synced;
                if (!driver_synced || memory_compare(compositor.table->driver, compositor.driver, sizeof(compositor.table->driver)))
                {
                        memory_copy(compositor.table->driver, compositor.driver, sizeof(compositor.table->driver));
                        driver_synced = 1;
                }
                compositor.table->pointer_plane = compositor_pointer_plane_word();
                compositor.table->overlay = compositor.overlay_state;
        }
        compositor_glass_lazy = 1; // these frames keep glass as its layers: the card lays them, and what the processor reads is settled below
        windows = count = present_build_frames(frames, rows);
        compositor_glass_lazy = 0;
        compositor_record_scene(frames, windows);
        tinted = compositor_preview_lift(frames, windows, &preview);
        turning = compositor_turning();
        direct = turning || compositor_overview_drawn() || compositor_roll.on || compositor_cast_screens() || compositor_cast_has_stale() ? -1 : compositor_direct_candidate(frames, rows, windows);
        // None at the top: a fullscreen window under pop-ups is shown straight, they on the overlay's band over it (compositor_direct_under).
        over = windows;
        if (direct < 0 && !turning && !compositor_overview_drawn() && !compositor_roll.on && !compositor_cast_screens() && !compositor_cast_has_stale() &&
            (direct = compositor_direct_under(frames, rows, windows, &over)) >= 0)
        {
                // The glass of what is over it is made from it before the band is composed: its rows under the band and the blur's reach
                // read by the card into the table first, where its picture is straight (tiled) and not read by the processor.
                int band[4], reach = compositor_glass_reach(COMPOSITOR_GLASS_DOWN) + ((COMPOSITOR_GLASS_LEVELS + 1) << COMPOSITOR_GLASS_LEVELS);

                if (compositor_tabs_rect(frames + over, windows - over, band))
                        compositor_wl_straight_settle(direct, band[1] - reach - compositor.y[direct], band[3] + reach - compositor.y[direct]);
        }
        stay_live_direct = direct >= 0 && !compositor_client_scans(direct) && direct == compositor.direct_row && !compositor_overlay_ready() &&
                           compositor.patched[direct][compositor.front[direct] & 1u] &&
                           compositor_window_fb(direct, compositor.front[direct]) == compositor.on_screen &&
                           compositor.under[compositor.front[direct] & 1u];
        if (direct < 0 && !compositor_gpu_tiled_ready())
                compositor_wl_straight_pays(-1);
        if (!stay_live_direct)
                compositor_direct_unpatch();
        compositor_glass_pass(frames, rows, windows);
        compositor_runs_note(frames, rows, windows);
        count += present_bands_pass(frames, windows, direct, stay_live_direct);
        if (tinted)
                frames[count++] = preview;
        count += compositor_moon_menu_frame(&frames[count]);
        unsigned int overlays_up = (unsigned int)compositor_overlays_up();
        if (overlays_up != compositor.overlays_up)
        {
                compositor.overlays_up = overlays_up;
                if ((compositor.overlays_up && !compositor.assist_on) || (!compositor.overlays_up && !compositor_glides()))
                        compositor.assist_count = 0;
                compositor_overlay_fade(compositor.overlays_up ? 100 : 0);
        }
        if (compositor_overlays_shown())
        {
                if (compositor.assist_armed)
                {
#ifdef CONFIG_MOONWATER_WAYLAND
                        compositor_wl_tiled_settle();
                        compositor_wl_lent_sync(1);
#endif
                        compositor_assist_refresh();
                        for (int thumb = 0; thumb < compositor.assist_count; thumb++)
                                compositor_damage(compositor.assist_rect[thumb][0], compositor.assist_rect[thumb][1],
                                                  compositor.assist_rect[thumb][2], compositor.assist_rect[thumb][3]);
                }
                count += compositor_overlays(frames, count);
        }
        compositor_corner_masks_ready(frames, count);
        if (compositor_spots_any(0))
                compositor_spots_ready();
        if (turning)
                compositor_unscanned(15);
        // The band over a window under pop-ups with the tabs and the rest drawn after the windows: an eighth of the screen at most, as theirs is.
        if (direct >= 0 && over < windows && compositor_tabs_rect(frames + over, count - over, planed) &&
            (long long)(planed[2] - planed[0]) * (long long)(planed[3] - planed[1]) * 8 > (long long)compositor.width * (long long)compositor.height)
                direct = -1;
        if (direct >= 0 &&
            ((compositor_overlay_ready() && !(compositor_flip_tears(direct) && !compositor.overlay_on) &&
              compositor_present_overlaid(direct, frames, count, over)) ||
             (over == windows && compositor_present_direct(direct, frames + windows, count - windows))))
                return present_direct_cleared();

        back = !turning && compositor.on_screen && compositor.on_screen != compositor.fb;
        if (back)
        {
                if (compositor.flip_pending)
                        return;
                if (compositor.direct_row >= 0)
                        compositor.leaving_row = compositor.direct_row;
                compositor.direct_row = -1;
                compositor_damage(0, 0, compositor.width, compositor.height);
        }
        else if (compositor.screen_stale)
                compositor_damage(0, 0, compositor.width, compositor.height);
        compositor.screen_stale = 0;

        // No window is lifted onto a plane while Scenes roll: the plane shows a window where it is, and the roll draws it on its way.
        plane = !back && !turning && !compositor_overview_drawn() && !compositor_roll.on && !compositor_cast_screens() && !compositor_cast_has_stale()
                    ? compositor_plane_candidate(frames, rows, windows, count, planed) : -1;
        if (plane < 0 && compositor.table)
                compositor.table->unscanned_by[compositor.table->unscanned < WINDOWS_UNSCANNED ? compositor.table->unscanned : 0]++;
        if (back || turning || !compositor_gpu_tiled_ready())
                compositor_wl_straight_pays(plane >= 0 && planed[0] == compositor.x[plane] &&
                                            planed[2] == compositor.x[plane] + (int)compositor.w[plane] &&
                                            plane == compositor.plane_row ? plane : -1);
        if (present_overlay_plane(plane, planed, turning, &plane_off))
                return;
        present_count_composed(direct, turning, rows, windows);

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
                        scanout_plate_replay_front();
                }
        }
#ifdef CONFIG_MOONWATER_WAYLAND
        compositor_wl_tiled_settle();
#endif
        for (unsigned int at = 0; at < windows; at++)
                compositor_controls_read(&frames[at]), compositor_close_mean(&frames[at]), compositor_close_ready(&frames[at]);
        int strip_damage = 0;

        if (compositor.width & 3)
        {
                int cut = compositor.width & ~3;

                for (unsigned int at = 0; at < compositor.damages; at++)
                        if (compositor.damage[at][2] > cut)
                                strip_damage = 1;
        }
#ifdef CONFIG_MOONWATER_WAYLAND
        if (strip_damage)
                compositor_wl_straight_pays(-1), compositor_wl_tiled_settle();
#endif
#ifdef MOONWATER_GPU_C
        int gpu_drawn = compositor_gpu_present(frames, count), gpu_composed = gpu_drawn && !strip_damage;

        if (gpu_drawn)
                compositor_wl_count_card(rows, windows);
        else if (!(compositor_card.nowait && compositor_card.looped) && gpu.pending && !gpu_finish())
                compositor_gpu_fail(0);
        int apart = !gpu_drawn && !flip_own && compositor_changed_ready();
#else
        int gpu_drawn = 0, apart = !flip_own && compositor_changed_ready(), gpu_composed = 0;
#endif
#ifdef CONFIG_MOONWATER_WAYLAND
        if (!gpu_drawn)
                compositor_wl_straight_pays(-1), compositor_wl_tiled_settle(), compositor_wl_lent_sync(1);
        else if (strip_damage)
                compositor_wl_lent_sync(1);
#endif
        if (!compositor.mirror_bytes && compositor.changed > 0)
                compositor.mirror = compositor.scanout, compositor.mirror_pitch = compositor.scanout_pitch / 4;
        if (!apart && compositor.mirror_bytes)
                compositor.mirror_stale = 1;
        // The glass windows' composed pictures made where the processor is about to read them: its own compose, or a share's.
        if (!gpu_composed || compositor_cast_screens())
                compositor_glass_settle_frames(frames, windows);
        clips = present_compose_damage(frames, count, gpu_drawn, apart, clip);
        if (apart)
        {
                int full = !compositor.mirror_bytes;

                for (unsigned int at = 0; at < compositor.damages && !full; at++)
                        full = compositor.damage[at][0] <= 0 && compositor.damage[at][1] <= 0 &&
                               compositor.damage[at][2] >= compositor.width && compositor.damage[at][3] >= compositor.height;
                if (full)
                        compositor.mirror_stale = 0;
        }
        if (!gpu_composed)
                compositor_runs_learn(frames, rows, windows);
        if (compositor_spare.fb)
                scanout_plate_mark_spare(flip_own ? (const int (*)[4])scene : (const int (*)[4])compositor.damage,
                                         flip_own ? scenes : compositor.damages);
        // The buffer composed into is whole now: the plate composed into (flip_own), on one screen or on all of them at once (compositor_flip_all),
        // or one screen's own buffer composed in place with no plane. Under a plane composed in place, and with two or more screens composed in
        // place, its stale is kept for the next present that composes into a plate. A plate's stale kept past the present that composed it whole
        // goes to the other plate at the next swap and is joined there: with two screens every present after the first composed the whole space.
        if (flip_own || (compositor_output_count() < 2 && compositor.plane_row < 0))
                compositor_front_stale_any = 0;
        // The present's damage, for the shares' rings after the flip is told (the frames' pictures are still this present's).
        static int cast_damage[COMPOSITOR_DAMAGE][4];
        unsigned int cast_damages = compositor.damages < COMPOSITOR_DAMAGE ? compositor.damages : COMPOSITOR_DAMAGE;

        memory_copy(cast_damage, compositor.damage, (positive)cast_damages * sizeof(cast_damage[0]));
        compositor.damages = 0;
#ifdef MOONWATER_GPU_C
        if (gpu_drawn && compositor_card.on)
        {
                compositor_card_after((const unsigned short (*)[4])clip, clips, turning, gpu_composed, flip_own, back, plane_off, rows, windows);
                compositor_cast_present(frames, count, rows, windows, (const int (*)[4])cast_damage, cast_damages);
                return;
        }
#endif
        compositor_present_tell((const unsigned short (*)[4])clip, clips, turning, gpu_composed, flip_own, back, plane_off);
        compositor_cast_present(frames, count, rows, windows, (const int (*)[4])cast_damage, cast_damages);
}

