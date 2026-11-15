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
        flips committed while a job of the card still ran, with the most jobs in flight at one of them since the mark (the card's present without a wait).
*/
static unsigned long long compositor_metric_late_then, compositor_metric_missed_then, compositor_metric_loop_most, compositor_metric_flips_jobs,
    compositor_metric_flips_jobs_then;
static unsigned int compositor_metric_flip_jobs_most, compositor_metric_late_second, compositor_metric_missed_second, compositor_metric_loop_most_second,
    compositor_metric_flip_jobs_second, compositor_metric_flip_jobs_most_second;

static void compositor_metric_publish(void)
{
        struct windows address_to table = compositor.table;
        unsigned long long now = clock_monotonic_nanoseconds(), lists = compositor_list_counts[COMPOSITOR_LIST_DRAWN];
        unsigned int mean, worst;

        if (!table)
                return;
        windows_metric_stats(compositor_metric_card, compositor_metric_card_count, &mean, &worst);
        table->metric_card_mean = mean, table->metric_card_worst = worst;
        windows_metric_stats(compositor_metric_processor, compositor_metric_processor_count, &mean, &worst);
        table->metric_processor_mean = mean, table->metric_processor_worst = worst;
#ifdef MOONWATER_GPU_C
        table->metric_maps = gpu_map_count, table->metric_map_hits = gpu_map_hits, table->metric_map_misses = gpu_map_misses;
        table->metric_wakes = gpu_wakes, table->metric_jobs = gpu_jobs;
        table->metric_job_mean = gpu_jobs ? (unsigned int)(gpu_job_ns / gpu_jobs / 1000) : 0;
#endif
        if (!compositor_metric_since)
                compositor_metric_since = now, compositor_metric_presents_then = compositor_metric_presents, compositor_metric_lists_then = lists,
                compositor_metric_late_then = compositor_late.made, compositor_metric_missed_then = compositor_late.missed,
                compositor_metric_flips_jobs_then = compositor_metric_flips_jobs;
        if (now - compositor_metric_since >= 1000000000ull)
        {
                compositor_metric_presents_second = windows_metric_rate(compositor_metric_presents - compositor_metric_presents_then, now - compositor_metric_since);
                compositor_metric_lists_second = windows_metric_rate(lists - compositor_metric_lists_then, now - compositor_metric_since);
                compositor_metric_late_second = windows_metric_rate(compositor_late.made - compositor_metric_late_then, now - compositor_metric_since);
                compositor_metric_missed_second = windows_metric_rate(compositor_late.missed - compositor_metric_missed_then, now - compositor_metric_since);
                compositor_metric_flip_jobs_second = windows_metric_rate(compositor_metric_flips_jobs - compositor_metric_flips_jobs_then, now - compositor_metric_since);
                compositor_metric_loop_most_second = (unsigned int)(compositor_metric_loop_most / 1000);
                compositor_metric_flip_jobs_most_second = compositor_metric_flip_jobs_most;
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
}

// The loop's turns a second, and those whose ppoll had nothing to wait for (a timer already due, so the turn spun): the wake rate in moonwater debug.
static unsigned long long compositor_loop_turns, compositor_loop_idle, compositor_loop_since, compositor_loop_then, compositor_loop_idle_then;

static unsigned int compositor_loop_by[WINDOWS_LOOP_SOURCES], compositor_loop_by_second[WINDOWS_LOOP_SOURCES];

// The source of a poll slot that returned events (the same order compositor_main builds the slots in).
static unsigned int compositor_loop_source(unsigned int at, unsigned int doors, unsigned int done_at, unsigned int card_at,
                                           unsigned int uevents_at, unsigned int clip_at, unsigned int wayland_at)
{
        if (at == 0)
                return 0;
        if (at == 1)
                return 1;
        if (at < doors)
                return 2;
        if (at < doors + WINDOWS)
                return 3;
        if (done_at && at == done_at)
                return 4;
        if (card_at && at == card_at)
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

static void compositor_present(void)
{
        unsigned long long began = clock_monotonic_nanoseconds(), ended;
        enum compositor_record_path path;

        compositor_record_begin();
        compositor_present_on_card = 0;
        compositor_present_frames();
        compositor_wl_lent_sync(0);
        ended = clock_monotonic_nanoseconds();
        // The loop's own time in a present, the longest of each second (what a present costs the loop).
        compositor_metric_loop_most = ended - began > compositor_metric_loop_most ? ended - began : compositor_metric_loop_most;
        compositor_metric_note(ended - began);
        path = compositor_present_on_card ? COMPOSITOR_RECORD_CARD : compositor.plane_row >= 0 ? COMPOSITOR_RECORD_PLANE : COMPOSITOR_RECORD_PROCESSOR;
        compositor_record_present(began, ended, path);
}

static void compositor_present_frames(void)
{
        struct compositor_frame frames[COMPOSITOR_FRAMES];
        unsigned short clip[COMPOSITOR_DAMAGE][4];
        struct compositor_frame band;
        struct compositor_frame preview;
        unsigned char rows[WINDOWS];
        unsigned int count = 0, windows;
        int tinted;

        if (!compositor.damages || compositor.flip_pending)
                return;
        compositor_bands_follow();

        // What moonwater debug says of the card, once there is a table to say it in.
        if (compositor.table)
        {
                memory_copy(compositor.table->driver, compositor.driver, sizeof(compositor.table->driver));
                compositor.table->pointer_plane = !compositor.cursor.soft;
                compositor.table->overlay = compositor.overlay_state;
        }

        for (unsigned int at = 0; at < compositor.stacked; at++)
                if (compositor_visible(compositor.stack[at]))
                {
                        rows[count] = compositor.stack[at];
                        compositor_frame_of(compositor.stack[at], &frames[count++]);
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
                        count += compositor_panel_tabs(&band, &frames[count]);
                }
                compositor.band_at = 0;
        }
        if (tinted)
                frames[count++] = preview;

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

        if (!turning && compositor_output_count() < 2 && compositor.plane_row < 0)
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
        else if (gpu.pending && !gpu_finish())
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
                                if (compositor.cursor.drawn)
                                        compositor_cursor_draw(&strip, compositor.cursor.x, compositor.cursor.y, &compositor.cursor.drawn_as,
                                                               compositor_cursor_ink(0, 0), compositor_cursor_ink(1, 0));
                        }
                }
                else
#endif
                {
                compositor_scene_banded(&target, frames, count, apart ? &slices : 0, rows);

                // Composed apart, each slice had the fingers' marks; on a scanout only written, they are composed aside.
                if (!apart && compositor_spots_any(0))
                        compositor_spots_compose(&target, frames, count);
                if (!apart && compositor.cursor.drawn)
                        compositor_cursor_draw(&target, compositor.cursor.x, compositor.cursor.y, &compositor.cursor.drawn_as,
                                               compositor_cursor_ink(0, 0), compositor_cursor_ink(1, 0));
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
        // No clips at all would tell the card everything changed. A headless compositor has no card to tell (its scan image is a file).
        if (clips && compositor_output_count() < 2 && !turning)
                compositor_dirty(compositor.fb, (const unsigned short (*)[4])clip, clips);
        else if (clips && compositor.card >= 0)
                compositor_dirty_screens((const unsigned short (*)[4])clip, clips);
        // The screens Canvas turns: their part of the scene, turned into their own buffers and told to their cards.
        if (clips && turning)
                compositor_turn_frame((const unsigned short (*)[4])clip, clips, gpu_composed);
        compositor.damages = 0;

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

