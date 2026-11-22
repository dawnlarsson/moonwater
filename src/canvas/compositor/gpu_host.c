/* Host-side GPU orchestration: scene submission, async steps/fences, list rendering, clocks, and external dmabuf import cache. */
static int compositor_present_on_card;

#ifdef MOONWATER_GPU_C
static int compositor_gpu;
static positive compositor_slept_clock(void);
static void compositor_gpu_restart_check(void);
static void compositor_say(string_address what, bipolar value);
#define COMPOSITOR_GPU_OPS_AT GPU_DATA
#define COMPOSITOR_GPU_LIST_OPS (512 << 10)
#define COMPOSITOR_GPU_LIST_OPS_AT (COMPOSITOR_GPU_OPS_AT + COMPOSITOR_GPU_LIST_OPS)
#define COMPOSITOR_GPU_TABLES_AT (GPU_DATA + (1 << 20))
#define COMPOSITOR_GPU_BINS_AT (GPU_DATA + (2 << 20))
static struct
{
        int up, dead, force, told, hung_off, cache_next;
        enum compositor_gpu_why dead_why;
        unsigned int failures, cool_steps, blamed;
        b64 cool_until, hung_at, blamed_at, late_first, late_next;
        positive out, out_key, cache[2], cache_key[2], turn_va[WINDOWS_OUTPUTS], turn_key[WINDOWS_OUTPUTS];
        b32 restart_due;
        positive slept_seen, settled, said;
} compositor_gpu_state;

static struct
{
        unsigned int drawn, processor, why[COMPOSITOR_GPU_WHY_COUNT];
        b64 said_at[COMPOSITOR_GPU_WHY_COUNT];
} compositor_gpu_counts;

static unsigned int compositor_gpu_refuse_gen;

// The counts as the table says them (moonwater canvas reads the table, not the processor's memory).
typedef char compositor_gpu_fault_fits[sizeof(gpu_fault) == sizeof(((struct windows *)0)->gpu_fault) ? 1 : -1]; // the table holds the bytes gpu.c writes
static void compositor_gpu_publish(void)
{
        static unsigned int pub_init, pub_faults, pub_maps_user, pub_maps_dma, pub_req_mask, pub_recovered, pub_refuse_gen, pub_map_why, pub_family;
        unsigned int family = gpu.family ? gpu.family : gpu_req.family;

        if (!compositor.table)
                return;
        compositor.table->gpu_drawn = compositor_gpu_counts.drawn, compositor.table->gpu_processor = compositor_gpu_counts.processor;
        if (pub_init && pub_faults == gpu_faults && pub_maps_user == gpu_maps_user && pub_maps_dma == gpu_maps_dma &&
            pub_req_mask == gpu_req.mask && pub_recovered == gpu_recovered &&
            pub_refuse_gen == compositor_gpu_refuse_gen && pub_map_why == gpu_map_why && pub_family == family)
                return;
        pub_init = 1, pub_faults = gpu_faults, pub_maps_user = gpu_maps_user, pub_maps_dma = gpu_maps_dma,
        pub_req_mask = gpu_req.mask, pub_recovered = gpu_recovered,
        pub_refuse_gen = compositor_gpu_refuse_gen, pub_map_why = gpu_map_why, pub_family = family;
        memory_copy(compositor.table->gpu_refused, compositor_gpu_counts.why, sizeof(compositor_gpu_counts.why));
        compositor.table->gpu_map_why = gpu_map_why;
        compositor.table->gpu_family = gpu.family ? gpu.family : gpu_req.family;
        compositor.table->gpu_device = gpu.device ? gpu.device : gpu_req.device;
        compositor.table->gpu_map_probe = gpu_map_probe;
        if (gpu_said[0] && memory_compare(compositor.table->gpu_said, gpu_said, sizeof(gpu_said)))
                memory_copy(compositor.table->gpu_said, gpu_said, sizeof(gpu_said));
        compositor.table->gpu_faults = gpu_faults;
        memory_copy(compositor.table->gpu_fault, gpu_fault, sizeof(gpu_fault));
        compositor.table->gpu_req_first = gpu_req.first, compositor.table->gpu_req_error = gpu_req.error, compositor.table->gpu_req_mask = gpu_req.mask;
        compositor.table->gpu_req_mode = gpu_req.mode, compositor.table->gpu_req_owner = gpu_req.owner;
        compositor.table->gpu_req_route = gpu_req.route, compositor.table->gpu_req_route_error = gpu_req.route_error;
        compositor.table->gpu_map_user = gpu_maps_user, compositor.table->gpu_map_dma = gpu_maps_dma;
        compositor.table->gpu_recovered = gpu_recovered, compositor.table->gpu_priority = gpu_priority, compositor.table->gpu_priority_error = gpu_priority_error;
        memory_copy(compositor.table->gpu_node_bus, gpu_req.bus, sizeof(gpu_req.bus)), memory_copy(compositor.table->gpu_display_bus, gpu_display, sizeof(gpu_display));
        compositor.table->gpu_node_pick = gpu_req.pick;
        memory_copy(compositor.table->gpu_req_driver, gpu_req.driver, sizeof(gpu_req.driver)), memory_copy(compositor.table->gpu_req_node, gpu_req.node, sizeof(gpu_req.node));
}

// The prerequisites of the card, as the table and the issue board say them after a start: the first one that failed is an ERROR under "canvas gpu" with its
// fix (the detail) and the kernel's error; when the card is up, that issue is cleared. The table's record is written either way (moonwater debug).
typedef char gpu_req_words_fit[sizeof(windows_gpu_req_fixes) / sizeof(windows_gpu_req_fixes[0]) == GPU_REQ_COUNT ? 1 : -1];
static void compositor_gpu_requirements(void)
{
        char words[WINDOWS_ISSUE_DETAIL];

        if (!compositor.table)
                return;
        compositor.table->gpu_req_checked = 1;
        compositor_gpu_publish();
        // The detail of a failed step: the machine's own words where gpu.c has them (the driver that has no path and its node, the chip that has no kernels:
        // Settings puts them in its message), else the step's fix.
        if (gpu_req.first)
                windows_issue_set(compositor.table, "canvas gpu", gpu_req.settled ? WINDOWS_ISSUE_WARNING : WINDOWS_ISSUE_ERROR, WINDOWS_ISSUE_GPU_REQUIRES,
                                  gpu_req.first, gpu_req.error,
                                  gpu_req_words(words, sizeof(words)) ? words : (const char *)windows_gpu_req_fixes[gpu_req.first]);
        else if (gpu_req.route) // the card composes; a refused userptr has no fallback until the dma-buf route works
                windows_issue_set(compositor.table, "canvas gpu", WINDOWS_ISSUE_WARNING, WINDOWS_ISSUE_GPU_REQUIRES, gpu_req.route, gpu_req.route_error,
                                  windows_gpu_req_fixes[gpu_req.route]);
        else
                windows_issue_clear(compositor.table, "canvas gpu", WINDOWS_ISSUE_GPU_REQUIRES);
}

// A refusal of the card: the reason is counted, and said when it is first or due. A present the card is refused is the processor's (processor 1);
// a turned screen the card did not turn is a refusal of its own (processor 0: the present was the card's).
static void compositor_gpu_refuse(enum compositor_gpu_why why, int processor)
{
        unsigned int n = ++compositor_gpu_counts.why[why];
        b64 now = 0;

        compositor_gpu_refuse_gen++;
        compositor_gpu_counts.processor += (unsigned int)processor;
        // Off, headless, no-card and untrusted are session-level states already logged once at startup (compositor_gpu_failed); do not spam per-minute refusal lines.
        if (why == COMPOSITOR_GPU_WHY_OFF || why == COMPOSITOR_GPU_WHY_HEADLESS ||
            why == COMPOSITOR_GPU_WHY_NO_CARD || why == COMPOSITOR_GPU_WHY_UNTRUSTED)
        {
                compositor_gpu_publish();
                return;
        }
        if (n == 1 || ((n & 63) == 0 && (now = clock_monotonic_nanoseconds()) - compositor_gpu_counts.said_at[why] >= 60000000000ull))
        {
                compositor_gpu_counts.said_at[why] = now ? now : clock_monotonic_nanoseconds();
                string_format(log, "compositor: the GPU refused a present (%s), the processor composed it; %p so far (the last map refused: step %p, error %p)\n", windows_gpu_reasons[why], (positive)n,
                              (positive)(gpu_map_why & 255), (positive)(gpu_map_why >> 8));
                log_flush();
        }
        compositor_gpu_publish();
}

// The card's buffer as the compositor's scene is laid out in it (the ops, the tables): made again when the card is started again. A new buffer holds
// none of the old one's tables, so the cache of the tables (kept, tables_used) is let go with it: a remembered place would read the new buffer's bytes
// there, a cover of 0 (the corner squares drawn as the ground, the line and the picture both missing) until the pool happened to be refilled.
static void compositor_gpu_layout(void)
{
        struct compositor_gscene *g = &compositor_gscene;

        g->ops = (unsigned int *)((unsigned char *)gpu.at + COMPOSITOR_GPU_OPS_AT), g->room = (compositor_card.lists_nowait ? COMPOSITOR_GPU_LIST_OPS : 1 << 20) / 32;
        g->tables = (unsigned char *)gpu.at + COMPOSITOR_GPU_TABLES_AT, g->tables_at = COMPOSITOR_GPU_TABLES_AT, g->tables_size = 1 << 20;
        g->kept = 0, g->tables_used = 0;
}

// The late probe: a card that is not there yet (no render node, a driver or its firmware that loads late) is looked for again while canvas gpu is on and a present
// wants it: at once, then every five seconds for the first minute, then every sixty. Given the times (ns, monotonic): the first look that found nothing and the
// next one due (0 for none yet). True when the card is due now (and the next is set); the test's clock is the caller's.
static b32 compositor_gpu_late_due(b64 now, b64 *first, b64 *next)
{
        if (!*first)
                *first = now, *next = now + 5000000000ull;
        if (now < *next)
                return false;
        *next = now + (now - *first < 60000000000ull ? 5000000000ull : 60000000000ull);
        return true;
}

// A start that failed: given up (no card, or untrusted), settled where the nodes say it can never start while they stay as they are, the board told, and the
// outcome said in the log the first time it is this one: the step, its error, its fix and the machine's words for it. The outcome is a number (the step, its
// error, the driver, the chip): the same outcome is the same number, never 0.
static void compositor_gpu_failed(void)
{
        char words[WINDOWS_ISSUE_DETAIL];
        positive outcome = (gpu_node_mix(14695981039346656037ull, gpu_req.first, gpu_req.driver) ^ ((positive)gpu_req.error << 48 ^ (positive)gpu_req.family << 24 ^ gpu_req.device)) *
                           1099511628211ull | 1;
        b32 own;

        compositor_gpu_state.up = 0, compositor_gpu_state.dead = 1;
        compositor_gpu_state.dead_why = gpu.broken ? COMPOSITOR_GPU_WHY_UNTRUSTED : COMPOSITOR_GPU_WHY_NO_CARD;
        compositor_gpu_state.settled = gpu_req.settled ? gpu_req.nodes : 0;
        compositor_gpu_requirements();
        if (outcome == compositor_gpu_state.said)
                return;
        compositor_gpu_state.said = outcome, own = gpu_req_words(words, sizeof(words));
        string_format(log, "compositor: the GPU is not up, the processor draws: %s failed (error %p): %s%s%s%s%s\n", windows_gpu_req_names[gpu_req.first],
                      (positive)gpu_req.error, windows_gpu_req_fixes[gpu_req.first], own ? (string_address) " (" : (string_address) "",
                      (string_address)words, own ? (string_address) ")" : (string_address) "",
                      compositor_gpu_state.settled ? (string_address) " (not looked for again while the render nodes stay as they are)" : (string_address) "");
        log_flush();
}

// The GPU started once, on a card it has kernels for, with the pools laid out in its buffer.
static int compositor_gpu_up(void)
{
        if (compositor_gpu_state.up || compositor_gpu_state.dead)
                return compositor_gpu_state.up;
        // The display's device: the render node on the same device is the card (gpu.c's gpu_pick); a headless compositor has none.
        if (compositor.card >= 0)
                gpu_bus_of(compositor.card, gpu_display, sizeof(gpu_display));
        else
                gpu_display[0] = 0;
        // The dma-buf route's memory: the table's memfd, its pictures (the window buffers, not the lists or the table's header) and the pin of their rows.
        gpu_shared.fd = compositor.table_memfd ? compositor.table_memfd - 1 : -1;
        gpu_shared.at = (unsigned char *)compositor.table;
        gpu_shared.bytes = compositor.pixels + compositor.buffer * 2 * compositor_rows_room;
        gpu_shared.pin = compositor_gpu_pin;
        if (!gpu_start((2 << 20) + (16 << 20)))
        {
                compositor_gpu_failed();
                return 0;
        }
        compositor_gpu_state.settled = 0, compositor_gpu_state.said = 0; // (a card that is up again says its next failure, whatever the last was)
        compositor_gpu_requirements();
        if (gpu_req.route)
                string_format(log, "compositor: the GPU is up; its dma-buf route is not (%s, error %p): %s\n", windows_gpu_req_names[gpu_req.route],
                              (positive)gpu_req.route_error, windows_gpu_req_fixes[gpu_req.route]),
                    log_flush();
        compositor_gpu_layout();
        compositor_gpu_state.up = 1;
        return 1;
}


// The card lost its state: started again (gpu.c's gpu_recover). Everything the compositor kept of the card's addresses is forgotten with it: the scanout's
// import and the turned screens' maps (their addresses are the old maps'). A card that does not start again is given up for now (dead, no card: the late
// probe starts it again when it is due). The failures of a present are counted by the caller, so repeated losses cool the card down as hangs do.
static void compositor_gpu_recover(void)
{
        compositor_gpu_state.out = 0, compositor_gpu_state.out_key = 0;
        compositor_gpu_state.cache[0] = compositor_gpu_state.cache[1] = compositor_gpu_state.cache_key[0] = compositor_gpu_state.cache_key[1] = 0;
        memory_zero(compositor_gpu_state.turn_va, sizeof(compositor_gpu_state.turn_va));
        memory_zero(compositor_gpu_state.turn_key, sizeof(compositor_gpu_state.turn_key));
        if (compositor.card >= 0)
                gpu_bus_of(compositor.card, gpu_display, sizeof(gpu_display));
        else
                gpu_display[0] = 0;
        if (!gpu_recover())
        {
                compositor_gpu_failed(); // (no card: looked for again when it is due; one that drew its self-test wrong this time: untrusted)
                return;
        }
        compositor_gpu_layout();
        compositor_gpu_requirements();
}

// A submit said the card lost its state (gpu_lost): started again before anything is drawn on it, so the present that found it draws on the processor
// and the next one on the new card. Nothing else recovers the card on a present that refused before the scene (a picture, a scanout).
static void compositor_gpu_lost_check(void)
{
        if (gpu_lost && !compositor_gpu_state.dead)
                compositor_gpu_recover();
}

// The card is not to be given a job now: a hang's cool-down, a restart due (the card's context and maps are the old ones until it is started again), or a
// submit that said the card lost its state. The processor draws those.
static int compositor_gpu_held(void)
{
        return compositor_gpu_state.restart_due || gpu_lost || (compositor_gpu_state.cool_until && clock_monotonic_nanoseconds() < compositor_gpu_state.cool_until);
}

/*
        A job of the card that failed (gpu_end says how, gpu_last_error why, as this call's answer). A late job (GPU_END_LATE: not done in a tick) has not
        failed: the processor draws this frame, the job stays pending (gpu_idle holds the buffer, and every present after is the processor's while it runs),
        and nothing is cooled or counted. It becomes a hang only by its end (checked before each present draws): its fence answers an error, a page fault is in the
        kernel's log, or it is two seconds old. A hang (HUNG: the ring did not finish the job in two seconds; WAIT: the wait itself failed) is the card's one
        lesson, not a run of refusals: the processor draws for two seconds, and when the cool-down is over the card is
        started again with a clean table of maps (gpu_recover lets every map go: a present no longer names the addresses the hang read). A second hang within
        a minute gives the card up for the session, the processor draws until it ends, and the board says so; a hang whose fault was in a client's buffer is
        that client's (compositor_gpu_hang_blamed), not one of the two. Any other refused job counts as it always did:
        three in a row cool the card down, each cool-down longer.
*/
#define COMPOSITOR_GPU_HUNG_COOL 2000000000ull
#define COMPOSITOR_GPU_HUNG_WINDOW 60000000000ull
#define COMPOSITOR_GPU_BLAMED_MOST 4u // hangs blamed on clients in a minute before the card is the one blamed (a client that connects again and again)
#ifdef CONFIG_MOONWATER_WAYLAND
static b32 compositor_wl_card_blame(positive import);
#endif
// A hang whose fault was in a client's buffer (gpu_fault_import, a row's lent picture, source or layer): that client is barred from the card
// (compositor_wl_card_blame) and the hang is not the card's, so it does not count toward the session's two; the card cools and starts again as at a
// first hang. COMPOSITOR_GPU_BLAMED_MOST or more in a minute are taken as the card's.
static b32 compositor_gpu_hang_blamed(b64 now)
{
#ifdef CONFIG_MOONWATER_WAYLAND
        if (!gpu_fault_import || !compositor_wl_card_blame(gpu_fault_import))
                return false;
        if (!compositor_gpu_state.blamed_at || now - compositor_gpu_state.blamed_at >= COMPOSITOR_GPU_HUNG_WINDOW)
                compositor_gpu_state.blamed_at = now, compositor_gpu_state.blamed = 0;
        return ++compositor_gpu_state.blamed < COMPOSITOR_GPU_BLAMED_MOST;
#else
        (void)now;
        return false;
#endif
}

static void compositor_gpu_job_failed(void)
{
        b64 now = clock_monotonic_nanoseconds();
        b32 blamed;

        if (gpu_end == GPU_END_LATE || gpu_end == GPU_END_OVERFLOW)
                return;
        if (gpu_end != GPU_END_HUNG && gpu_end != GPU_END_WAIT)
        {
                if (++compositor_gpu_state.failures >= 3)
                {
                        unsigned int steps = compositor_gpu_state.cool_steps < 5 ? compositor_gpu_state.cool_steps : 5;

                        compositor_gpu_state.cool_until = now + (COMPOSITOR_GPU_HUNG_COOL << steps);
                        compositor_gpu_state.cool_steps++, compositor_gpu_state.failures = 0;
                }
                return;
        }
        compositor_gpu_state.failures = 0;
        blamed = compositor_gpu_hang_blamed(now);
        gpu_fault_import = 0; // blamed once: a later failure that reads no new fault blames nobody
        if (!blamed && compositor_gpu_state.hung_at && now - compositor_gpu_state.hung_at < COMPOSITOR_GPU_HUNG_WINDOW)
        {
                compositor_gpu_state.dead = 1, compositor_gpu_state.dead_why = COMPOSITOR_GPU_WHY_HANG, compositor_gpu_state.hung_off = 1;
                compositor_gpu_state.cool_until = 0, compositor_gpu_state.restart_due = 0;
                string_format(log, "compositor: the GPU hung twice within a minute (%s): the processor draws the desktop for the rest of the session\n", gpu_fault);
                log_flush();
                return;
        }
        if (!blamed)
                compositor_gpu_state.hung_at = now;
        compositor_gpu_state.cool_until = now + COMPOSITOR_GPU_HUNG_COOL;
        compositor_gpu_state.cool_steps++;
        compositor_gpu_state.restart_due = 1; // started again when the cool-down is over, with no maps (gpu_recover)
        string_format(log, "compositor: the GPU hung (%s): the processor draws for two seconds, then the card starts again with no maps\n", gpu_fault);
        log_flush();
}

// Why a job the card did not finish is the present's loss: it was late (a job that may still run: busy), overflowed the bin pool, or hung/was refused.
static enum compositor_gpu_why compositor_gpu_job_why(void)
{
        return gpu_end == GPU_END_LATE ? COMPOSITOR_GPU_WHY_BUSY : gpu_end == GPU_END_OVERFLOW ? COMPOSITOR_GPU_WHY_POOL : COMPOSITOR_GPU_WHY_HANG;
}

// Unified GPU job failure teardown: optional pending finish, optional handed-hold release, lost-context recovery, and hang/failure accounting.
enum
{
        GPU_FAIL_FINISH = 1u << 0,
        GPU_FAIL_UNHOLD = 1u << 1,
};

static void compositor_gpu_fail(unsigned int flags)
{
        if ((flags & GPU_FAIL_FINISH) && gpu.pending)
                gpu_finish();
        if (flags & GPU_FAIL_UNHOLD)
        {
                gpu_handed_hold = 0;
                if (!gpu.pending)
                        gpu_handed_clear();
        }
        if (gpu_lost)
                compositor_gpu_recover();
        compositor_gpu_job_failed();
}

// Look up or create a writable GPU mapping for ptr[0..bytes) (mapping map_bytes on miss); recovers on gpu_lost if requested.
static positive gpu_host_va_written(const void *ptr, positive bytes, positive map_bytes, int recover)
{
        positive at = gpu_va_written(ptr, bytes);

        if (!at && (!gpu_map_written(ptr, map_bytes) || !(at = gpu_va_written(ptr, bytes))) && recover && gpu_lost)
                compositor_gpu_recover();
        return at;
}

static positive compositor_gpu_output(void)
{
        positive key = compositor.card < 0 ? (positive)compositor.scanout : compositor.scanout_handle;

        if (compositor.card < 0)
                compositor_gpu_state.out = gpu_host_va_written(compositor.scanout, compositor.scanout_bytes, compositor.scanout_bytes, 0);
        else
        {
                int at = compositor_gpu_state.cache[0] && compositor_gpu_state.cache_key[0] == key ? 0 :
                         compositor_gpu_state.cache[1] && compositor_gpu_state.cache_key[1] == key ? 1 : -1;

                if (at < 0)
                {
                        at = compositor_gpu_state.cache_next, compositor_gpu_state.cache_next ^= 1;
                        if (compositor_gpu_state.cache[at])
                                gpu_unimport(compositor_gpu_state.cache[at], GPU_GONE_UNIMPORT);
                        compositor_gpu_state.cache[at] = gpu_import(compositor.card, compositor.scanout_handle, compositor.scanout_bytes);
                        compositor_gpu_state.cache_key[at] = key;
                        // On hybrid/cross-GPU setups (e.g. simpledrm/nouveau display + iGPU render node), a failed scanout
                        // PRIME import would otherwise repeat PRIME_HANDLE_TO_FD/FD_TO_HANDLE on every frame forever; settle onto CPU.
                        if (!compositor_gpu_state.cache[at] && gpu_req.pick == 2)
                        {
                                gpu_stop();
                                compositor_gpu_state.up = 0, compositor_gpu_state.dead = 1;
                                compositor_gpu_state.dead_why = COMPOSITOR_GPU_WHY_OUTPUT;
                                compositor_gpu_state.settled = gpu_req.nodes;
                                return 0;
                        }
                }
                compositor_gpu_state.out = compositor_gpu_state.cache[at];
                if (compositor_gpu_state.out)
                        gpu_import_handed(compositor_gpu_state.out);
        }
        compositor_gpu_state.out_key = key;
        return compositor_gpu_state.out;
}

// A frame's picture, wholly, reachable by the GPU: mapped the first time it is asked for.
static int compositor_gpu_picture(const struct compositor_frame *f)
{
        positive bytes;

        if (f->builtin || f->width <= 0 || f->height <= 0) // no area: no picture to map
                return 1;
        if (!f->pixels) // a frame with no picture is its line and corners only (a grid's border), and nothing to map
                return 1;
        if (f->tiled_kind) // a client's tiled/DCC dma-buf sampled via image_load in scene: already imported to the card
                return 1;
        if (f->gpu_base) // a client's dma-buf, imported to the card (compositor_wl_lend): its address is there already
                return 1;
        if (f->pitch >= 1ull << 29)
                return 0;
        // The kernels reach a picture's pixels by a 32-bit signed offset from the first they are given (gpu.asm's image, mix, close and turn ops):
        // a picture of 2 GiB or more would wrap to bytes before it, so it is the processor's.
        bytes = 4 * (f->pitch * (positive)(f->height - 1) + (positive)f->width);
        if (bytes >= 1ull << 31)
                return 0;
        // A tab is a slice of its band's pixels (compositor_panel_tabs): the band is mapped whole, once, not a map a tab. The kernel looks at the pages
        // of every map at every job: on the box's 610M 34 tab slices cost each present 68 us more, the band as one map nothing to speak of.
        {
                positive va = compositor_gpu_va(f);

                for (int at = 0; at < WINDOWS_OUTPUTS && !va; at++)
                {
                        const unsigned int *band = at ? compositor.band_more[at] : compositor.band;
                        unsigned long had = at ? compositor.band_more_bytes[at] : compositor.band_bytes;

                        if (band && f->pixels >= band && f->pixels < band + had / 4)
                                gpu_map(band, had), va = compositor_gpu_va(f);
                }
                if (!va && compositor.mc.pixels && f->pixels >= compositor.mc.pixels && f->pixels < compositor.mc.pixels + compositor.mc.bytes / 4)
                        gpu_map(compositor.mc.pixels, compositor.mc.bytes), va = compositor_gpu_va(f);
                return va || (gpu_map(f->pixels, bytes) && compositor_gpu_va(f));
        }
}

/*
        A finger's mark as records (compositor_spot_draw): the inner fill's mix of the line's ink over the disc, then the edge's, each cover a table of the
        disc's square (2 radius by 2 radius bytes, rows from its top; the four quarters are the one quarter mirrored, as the processor reads them). Made once
        for a radius, ring and alpha. Its place from the buffer's start, 0 for none. edge 0 is the fill's cover, 1 the edge's.
*/
static unsigned int compositor_gpu_spot_table(int radius, int ring, unsigned int alpha, int edge)
{
        int inner = radius - ring, size = 2 * radius;
        unsigned char *table;
        unsigned int at = compositor_gpu_slot_tag(radius, ring, 16 + edge, (int)alpha, 0, (unsigned int)(size * size), &table);
        const unsigned char *outer_mask, *inner_mask;
        unsigned int fill = COMPOSITOR_SPOT_FILL * alpha / 255, ring_cover = COMPOSITOR_SPOT_RING * alpha / 255;

        if (!table)
                return at;
        outer_mask = compositor_corner_mask(radius), inner_mask = compositor_corner_mask(inner);
        for (int row = 0; row < radius; row++)
                for (int col = 0; col < radius; col++)
                {
                        unsigned int out = outer_mask[row * radius + col];
                        unsigned int in = col >= ring && row >= ring ? inner_mask[(row - ring) * inner + (col - ring)] : 0;
                        unsigned int cover = 0;

                        if (out)
                        {
                                unsigned int v = edge ? (out > in ? (out - in) * ring_cover : 0) : (in ? in * fill : 0);
                                cover = v ? ((v + 1u + (v >> 8)) >> 8) : 0;
                        }
                        table[row * size + col] = (unsigned char)cover;
                        table[row * size + (size - 1 - col)] = (unsigned char)cover;
                        table[(size - 1 - row) * size + col] = (unsigned char)cover;
                        table[(size - 1 - row) * size + (size - 1 - col)] = (unsigned char)cover;
                }
        return at;
}

// The fingers' marks over the scene, in slot order (compositor_spots_draw): each live mark that has area as two records, after the frames and before the
// pointer. False (and the reason) when the pool is full.
static void compositor_chip_place(int *box);
static b32 compositor_gpu_spots(void)
{
        struct compositor_gscene *g = &compositor_gscene;
        int radius = compositor_spot_radius(compositor.scale), ring = compositor_px1(compositor.scale), inner = radius - ring;

        if (!compositor.spots_shown && compositor.chip.kind == COMPOSITOR_CHIP_NONE)
                return true;
        for (unsigned int slot = 0; slot < UI_TOUCH_SLOTS; slot++)
        {
                unsigned int alpha = compositor.spot[slot].alpha, fill_at, edge_at;
                int cx = compositor.spot[slot].x, cy = compositor.spot[slot].y;

                if (!compositor.spot[slot].on || !alpha || inner < 1)
                        continue;
                if (g->count + 8 > g->room - 400)
                        return g->why = COMPOSITOR_GPU_WHY_POOL, false;
                fill_at = compositor_gpu_spot_table(radius, ring, alpha, 0), edge_at = compositor_gpu_spot_table(radius, ring, alpha, 1);
                if (!fill_at || !edge_at)
                        return g->why = COMPOSITOR_GPU_WHY_POOL, false;
                compositor_gpu_rect(5, cx - radius, cy - radius, cx + radius, cy + radius, COMPOSITOR_INK_LINE_HELD, 0, fill_at, (unsigned int)(2 * radius));
                compositor_gpu_rect(5, cx - radius, cy - radius, cx + radius, cy + radius, COMPOSITOR_INK_LINE_HELD, 0, edge_at, (unsigned int)(2 * radius));
        }
        // The chip (compositor_chip_draw) as its body and glyphs, each a table of its coverage at its alpha, in the card's records.
        if (compositor.chip.kind != COMPOSITOR_CHIP_NONE && compositor.chip.alpha)
        {
                const struct compositor_chip *c = &compositor.chip;
                int box[4], layers = c->kind == COMPOSITOR_CHIP_HINT ? 1 : 2;
                unsigned long cells = (unsigned long)c->w * c->h;

                compositor_chip_place(box);
                for (int layer = 0; layer < layers; layer++)
                {
                        const unsigned char *cover = c->cover + (unsigned long)layer * cells;
                        unsigned char *table = 0;
                        unsigned int at, ink = c->kind == COMPOSITOR_CHIP_HINT || layer ? COMPOSITOR_CHIP_GLYPH : COMPOSITOR_CHIP_BODY;
                        unsigned long tag = (unsigned long)memory_hash_33((address_any)cover, (positive)cells) ^ ((unsigned long)c->kind << 56);
                        if (!tag)
                                tag = 1;
                        if (g->count + 8 > g->room - 400)
                                return g->why = COMPOSITOR_GPU_WHY_POOL, false;
                        at = compositor_gpu_slot_tag(c->w, c->h, 20 + layer + (c->kind == COMPOSITOR_CHIP_HINT ? 2 : 0), (int)c->alpha,
                                                     tag, (unsigned int)cells, &table);
                        if (!at)
                                return g->why = COMPOSITOR_GPU_WHY_POOL, false;
                        // A table kept from an earlier present has this key's coverage already (the key is the size, the layer and the alpha).
                        if (table)
                                memory_scale_u8(table, cover, (positive)cells, c->alpha);
                        compositor_gpu_rect(5, box[0], box[1], box[0] + box[2], box[1] + box[3], ink, 0, at, (unsigned int)c->w);
                }
        }
        return true;
}

// The present's damage drawn by the GPU: why it is not, or COMPOSITOR_GPU_WHY_DRAWN when it is.
/*
        A frame drawn at less than its picture's size (a window landing, a snap assist's thumbnail: compositor_scaled) has the picture it would be at its own size made
        first, by the card: the source cell whose centre is the point's (compositor_picture_at), by the tables of columns and rows the processor makes (the card divides
        nothing), into memory of the compositor's own that the card may write (a pool of COMPOSITOR_GPU_SCALED pictures, each grown to the largest asked for and kept), and
        the frame in the list the present is made from is then an ordinary frame of that picture. The caller's frames are not changed: the first scaled frame makes a copy of
        the array, which the answer's pointer is then. COMPOSITOR_GPU_WHY_DRAWN when the frames are as they should be drawn, or the reason the card is refused them.
*/
#define COMPOSITOR_GPU_SCALED 8
#define COMPOSITOR_GPU_SCALED_MOST (128ul << 20)
static struct compositor_frame *compositor_gpu_scaled_copy; // the rows' (compositor_rows_make), COMPOSITOR_FRAMES_FOR the rows in use
static struct
{
        unsigned int *memory;
        unsigned long bytes;
        int fd;
} compositor_gpu_scaled_pool[COMPOSITOR_GPU_SCALED];

#define COMPOSITOR_GPU_SCALE_TABLES (4ull * (16384 + 16384)) // a scaled frame's tables (its columns, then its rows), at the most
// The scenes' bins without a wait: after the scaled frames' tables, which a scene's job may be submitted after, and before the self-test's megabyte.
#define COMPOSITOR_GPU_SCENE_BINS_AT (COMPOSITOR_GPU_BINS_AT + COMPOSITOR_GPU_SCALED * COMPOSITOR_GPU_SCALE_TABLES)
typedef char compositor_card_steps_fit[COMPOSITOR_GPU_SCALED + COMPOSITOR_DAMAGE <= COMPOSITOR_CARD_STEPS ? 1 : -1];
static b32 compositor_card_add(const struct compositor_card_step *step); // the present's jobs (below)

static enum compositor_gpu_why compositor_gpu_scale_frames(const struct compositor_frame **io, unsigned int count)
{
        const struct compositor_frame *frames = *io;
        unsigned int used = 0;

        if (count > COMPOSITOR_FRAMES_FOR(compositor_rows_in_use))
                return COMPOSITOR_GPU_WHY_OTHER;
        for (unsigned int k = 0; k < count; k++)
        {
                const struct compositor_frame *f = &frames[k];
                struct compositor_frame source;
                // Each scaled frame's tables in a place of their own: a blit not waited for reads them when its turn comes.
                positive tables_at = COMPOSITOR_GPU_BINS_AT + (positive)used * COMPOSITOR_GPU_SCALE_TABLES;
                unsigned int address_to tables = (unsigned int address_to)((p8 address_to)gpu.at + tables_at);
                positive bytes, src_va, dst_va;

                if (f->builtin || !f->pixels || !compositor_scaled(f))
                        continue;
                if (used == COMPOSITOR_GPU_SCALED || f->width <= 0 || f->height <= 0 || f->source_w <= 0 || f->source_h <= 0 || f->width > 16384 || f->height > 16384 ||
                    f->source_w > 16384 || f->source_h > 16384 || f->pitch < (unsigned long)f->source_w)
                        return COMPOSITOR_GPU_WHY_SCALED;
                bytes = 4ull * (positive)f->width * (positive)f->height;
                if (bytes > COMPOSITOR_GPU_SCALED_MOST)
                        return COMPOSITOR_GPU_WHY_SCALED;
                source = *f;
                source.width = f->source_w, source.height = f->source_h, source.source_w = source.source_h = 0;
                if (!compositor_gpu_picture(&source) || !(src_va = compositor_gpu_va(&source)))
                        return COMPOSITOR_GPU_WHY_PICTURE;
                if (compositor_gpu_scaled_pool[used].bytes < (unsigned long)bytes)
                {
                        unsigned long grown = ((unsigned long)bytes + (1ul << 20) - 1) & ~((1ul << 20) - 1);
                        long memfd, mapped;
                        void *old = compositor_gpu_scaled_pool[used].memory, *p = old;

                        mem_buf_free(&p, &compositor_gpu_scaled_pool[used].bytes, MEM_BUF_GPU_FORGET);
                        if (old)
                                gpu_unregister_memfd(old);
                        if (compositor_gpu_scaled_pool[used].fd > 0)
                                system_close(compositor_gpu_scaled_pool[used].fd - 1);
                        compositor_gpu_scaled_pool[used].memory = 0, compositor_gpu_scaled_pool[used].fd = 0;
                        memfd = compositor_call(syscall(memfd_create), (positive) "moonwater-scaled", 1 | 2, 0, 0, 0, 0);
                        if (memfd >= 0 && system_truncate_handle((int)memfd, (positive)grown) == 0 &&
                            compositor_call(syscall(fcntl), (positive)memfd, WINDOWS_F_ADD_SEALS, 2, 0, 0, 0) == 0) // F_SEAL_SHRINK
                        {
                                mapped = compositor_call(syscall(mmap), 0, (positive)grown, 3, 1, (positive)memfd, 0);
                                if (mapped < 0 && mapped > -4096)
                                {
                                        system_close((int)memfd);
                                        return COMPOSITOR_GPU_WHY_SCALED;
                                }
                                compositor_gpu_scaled_pool[used].memory = (unsigned int *)mapped, compositor_gpu_scaled_pool[used].bytes = grown;
                                compositor_gpu_scaled_pool[used].fd = (int)memfd + 1;
                                gpu_register_memfd((int)memfd, compositor_gpu_scaled_pool[used].memory, (positive)grown, 0);
                        }
                        else
                        {
                                if (memfd >= 0)
                                        system_close((int)memfd);
                                if (!mem_buf_grow(&p, &compositor_gpu_scaled_pool[used].bytes, grown, 0))
                                        return COMPOSITOR_GPU_WHY_SCALED;
                                compositor_gpu_scaled_pool[used].memory = (unsigned int *)p;
                        }
                }
                if (!(dst_va = gpu_host_va_written(compositor_gpu_scaled_pool[used].memory, bytes, compositor_gpu_scaled_pool[used].bytes, 1)))
                        return COMPOSITOR_GPU_WHY_PICTURE;
                {
                        positive step_x = ((positive)f->source_w << 24) / (positive)f->width, fx = ((positive)f->source_w << 23) / (positive)f->width;
                        positive step_y = ((positive)f->source_h << 24) / (positive)f->height, fy = ((positive)f->source_h << 23) / (positive)f->height;

                        for (int x = 0; x < f->width; x++, fx += step_x)
                                tables[x] = (unsigned int)(fx >> 24);
                        for (int y = 0; y < f->height; y++, fy += step_y)
                                tables[f->width + y] = (unsigned int)(fy >> 24);
                }
                if (compositor_card.nowait && compositor_card.looped)
                {
                        // A job of the present's, submitted with the scenes (compositor_card_go): the scene that reads this picture runs after it on the ring.
                        const struct compositor_card_step blit = {COMPOSITOR_CARD_SCALE, (unsigned int)f->width, (unsigned int)f->height, (unsigned int)f->pitch,
                                                                  (unsigned int)f->width, src_va, dst_va, GPU_VA + tables_at, {0, 0, 0, 0}};

                        if (!compositor_card_add(&blit))
                                return COMPOSITOR_GPU_WHY_SCALED;
                }
                else if (!gpu_blit(src_va, (unsigned int)f->pitch, dst_va, (unsigned int)f->width, GPU_VA + tables_at, 7, (unsigned int)f->width, (unsigned int)f->height))
                {
                        compositor_gpu_fail(0);
                        return compositor_gpu_job_why();
                }
                if (*io == frames)
                        memory_copy(compositor_gpu_scaled_copy, frames, (positive)count * sizeof(*frames)), *io = compositor_gpu_scaled_copy;
                compositor_gpu_scaled_copy[k].pixels = compositor_gpu_scaled_pool[used].memory, compositor_gpu_scaled_copy[k].pitch = (unsigned long)f->width;
                compositor_gpu_scaled_copy[k].gpu_base = dst_va;
                compositor_gpu_scaled_copy[k].source_w = compositor_gpu_scaled_copy[k].source_h = 0;
                used++;
        }
        return COMPOSITOR_GPU_WHY_DRAWN;
}

// A step of the present's jobs; false when there is no room for one (the caller draws on the processor).
static b32 compositor_card_add(const struct compositor_card_step *step)
{
        if (compositor_card.steps == COMPOSITOR_CARD_STEPS)
                return false;
        compositor_card.step[compositor_card.steps++] = *step;
        return true;
}

/*
        The present's jobs submitted in order while the card has room for them: 1 when every one is; 0 when the next waits for one before it (a slot, or a side of
        the bins, that a job running holds: gpu_end LATE); -1 when one was refused (gpu_end and gpu_last_error say why, as a job's that failed). The jobs after a
        refused one are not submitted.
*/
static int compositor_card_go(void)
{
        while (compositor_card.next < compositor_card.steps)
        {
                const struct compositor_card_step *s = &compositor_card.step[compositor_card.next];
                positive sequence = 0;

                gpu_last_error = 0, gpu_end = GPU_END_NONE;
                if (s->kind == COMPOSITOR_CARD_SCALE)
                {
                        if (!gpu_lost && gpu_slot_take() < 0)
                                gpu_end = GPU_END_LATE;
                        else if (!gpu_lost)
                        {
                                struct gpu_args address_to a = GPU_ARGS;

                                // gpu_blit's arguments, in the slot taken: the scale (op 7) through the tables at tab.
                                a->s = s->src, a->d = s->dst, a->g = s->tab, a->sp = s->sp, a->dp = s->dp, a->gp = 0, a->w = s->w, a->op = 7, a->v = 0;
                                a->x0 = a->y0 = 0, a->rows = s->h, a->tab = GPU_VA + GPU_BASES_AT;
                                sequence = gpu_go();
                        }
                }
                else
                        sequence = gpu_scene_submit(COMPOSITOR_GPU_OPS_AT, compositor_card.records, COMPOSITOR_GPU_SCENE_BINS_AT, compositor_card.out, compositor_card.pitch,
                                                    compositor_card.width, compositor_card.height, (unsigned int)s->rect[0], (unsigned int)s->rect[1],
                                                    (unsigned int)s->rect[2], (unsigned int)s->rect[3]);
                if (!sequence)
                {
                        if (gpu_end == GPU_END_LATE && !gpu_lost)
                                return 0;
                        compositor_card.next = compositor_card.steps;
                        gpu_handed_hold = 0, gpu_handed_clear();
                        return -1;
                }
                compositor_card.last = sequence, compositor_card.next++, compositor_card.submitted++;
        }
        gpu_handed_hold = 0, gpu_handed_clear();
        return 1;
}

// The sync_file the loop is to poll closed (the job is done, or given up).
static void compositor_card_fence_close(void)
{
        if (compositor_card.fence > 0)
                system_close(compositor_card.fence - 1);
        compositor_card.fence = 0, compositor_card.fence_of = 0;
}

// The sync_file of a job, made when it is another job's than the one held: the descriptor, or -1 for none (the loop then looks again in a millisecond).
static int compositor_card_fence_for(positive sequence)
{
        int fd;

        if (compositor_card.fence && compositor_card.fence_of == sequence)
                return compositor_card.fence - 1;
        compositor_card_fence_close();
        fd = sequence ? gpu_fence_fd(sequence) : -1;
        if (fd < 0)
                return -1;
        compositor_card.fence = fd + 1, compositor_card.fence_of = sequence;
        return fd;
}

// The job the loop waits for: the last of the present when all are submitted, else the one before the newest (it holds the slot and the side the next one
// needs), and with no present in flight a job still pending (a list's given up on as late); 0 for none.
static positive compositor_card_awaited(void)
{
        if (compositor_card.on)
                return compositor_card.next < compositor_card.steps && gpu_before.sequence ? gpu_before.sequence : compositor_card.next < compositor_card.steps ? gpu.pending
                                                                                                                                                         : compositor_card.last;
        return gpu.pending;
}

// A job of the card is a submit and a wait (tens of microseconds and a kernel entry, more than the card spends on a few tens of thousand pixels), so two
// damage rectangles are one job when the box round them is no more than this many pixels bigger than the two together.
#define COMPOSITOR_GPU_MERGE_SLACK 32768
static enum compositor_gpu_why compositor_gpu_draw(const struct compositor_frame *frames, unsigned int count)
{
        struct compositor_gscene *g = &compositor_gscene;
        struct canvas_damage rects = {.count = 0};
        b32 any_nonempty = false;
        positive out;

        if (!compositor_gpu)
                return COMPOSITOR_GPU_WHY_OFF;
        // A job the card was late with (gpu.c's gpu_run_job: not done in a tick) has come to a bad end, or is two seconds old: the card is hung as one that did
        // not finish in two seconds was (compositor_gpu_job_failed); a late job that finishes, or is still within bounds, is nothing.
        if (gpu_late_end())
                compositor_gpu_job_failed();
        compositor_gpu_restart_check(); // a machine that slept, or a gap between presents that is one, starts the card again before this present
        compositor_gpu_lost_check();
        if (compositor_gpu_state.dead)
                return compositor_gpu_state.dead_why ? compositor_gpu_state.dead_why : COMPOSITOR_GPU_WHY_DEAD;
        if (compositor_gpu_state.cool_until && clock_monotonic_nanoseconds() < compositor_gpu_state.cool_until)
                return COMPOSITOR_GPU_WHY_DEAD; // cooling down after hangs: the processor draws until the card is tried again
        if (compositor.card < 0 && !compositor_gpu_state.force)
                return COMPOSITOR_GPU_WHY_HEADLESS;
        // A width not a multiple of 4 is drawn to the multiple below it, the columns past it by the processor (compositor_present's strip); a pitch
        // that is not 16 bytes is the card's to refuse, its rows would start unaligned.
        if (compositor.scanout_pitch & 15)
                return COMPOSITOR_GPU_WHY_ALIGN;
        if (!compositor_gpu_up())
                return compositor_gpu_state.dead_why;
        if (!gpu_idle()) // a job given up on may still read the buffer: no record is written into it until it is done
                return COMPOSITOR_GPU_WHY_BUSY;
        // A present is a tick of the maps' (gpu_trim) whether the GPU draws it or not: a run of presents the processor draws (a window landing, scaled) made
        // maps of the pictures and let none go, and once all were held the GPU drew nothing again.
        gpu_trim();
        g->width = (unsigned int)(compositor.width & ~3), g->height = (unsigned int)compositor.height; // the card's columns: a multiple of 4
        for (unsigned int at = 0; at < compositor.damages; at++)
        {
                struct canvas_rect r = rect_meet(rect_from_int4(compositor.damage[at]),
                                                 (struct canvas_rect){0, 0, compositor.width, compositor.height});

                if (rect_empty(r))
                        continue;
                any_nonempty = true;
                if (r.x2 > (int)g->width)
                        r.x2 = (int)g->width;
                if (rect_empty(r))
                        continue;
                damage_add(&rects, r, COMPOSITOR_GPU_MERGE_SLACK);
        }
        if (!any_nonempty)
                return COMPOSITOR_GPU_WHY_OTHER;
        // The card draws the columns below g->width; a damage wholly past them is the processor's strip alone (compositor_present_frames), and drawn.
        if (!rects.count)
                return COMPOSITOR_GPU_WHY_DRAWN;
        if (!(out = compositor_gpu_output()))
                return COMPOSITOR_GPU_WHY_OUTPUT;
        compositor_card.steps = compositor_card.next = compositor_card.submitted = 0, compositor_card.failed = compositor_card.fenced = 0, compositor_card.last = 0;
        {
                enum compositor_gpu_why scaled = compositor_gpu_scale_frames(&frames, count); // the landings made pictures of their own size, and the frames those

                if (scaled != COMPOSITOR_GPU_WHY_DRAWN)
                        return scaled;
        }
        for (unsigned int k = 0; k < count; k++)
                if (!compositor_gpu_picture(&frames[k]))
                        return COMPOSITOR_GPU_WHY_PICTURE;
        // The wallpaper, the ground the scene is laid on: mapped once and kept while it is (compositor_wall_take forgets the one let go of).
        {
                struct compositor_frame wall;

                if (compositor_gpu_wall_frame(&wall) && !compositor_gpu_picture(&wall))
                        return COMPOSITOR_GPU_WHY_PICTURE;
        }
        if (!compositor_gpu_frames(frames, count, compositor_ground))
                return g->why;
        if (!compositor_gpu_spots()) // the fingers' marks over the scene, and the pointer over them
                return g->why;
        if (compositor.cursor.drawn && !compositor_gpu_cursor(compositor.cursor.x, compositor.cursor.y, &compositor.cursor.drawn_as, compositor_cursor_ink(0, 0), compositor_cursor_ink(1, 0)))
                return g->why;
        if (compositor_card.nowait && compositor_card.looped)
        {
                // The scenes after the scaled pictures, a rectangle each, submitted as the card has room for them; the present is the card's from the first.
                for (unsigned int at = 0; at < rects.count; at++)
                {
                        const struct compositor_card_step scene = {COMPOSITOR_CARD_SCENE, 0, 0, 0, 0, 0, 0, 0,
                                                                   {rects.r[at].x1, rects.r[at].y1, rects.r[at].x2, rects.r[at].y2}};

                        if (!compositor_card_add(&scene))
                                return COMPOSITOR_GPU_WHY_OTHER;
                }
                compositor_card.records = g->count, compositor_card.out = out, compositor_card.pitch = (unsigned int)(compositor.scanout_pitch / 4);
                compositor_card.width = g->width, compositor_card.height = (unsigned int)compositor.height;
                gpu_handed_hold = 1;
                if (compositor_card_go() < 0)
                {
                        compositor_gpu_fail(0);
                        // Refused before a job of it ran: the processor draws the present. Refused after: the frame is drawn again once those are done.
                        if (!compositor_card.submitted)
                                return compositor_gpu_job_why();
                        compositor_card.failed = 1;
                }
                compositor_card.on = 1;
        }
        else
        {
                gpu_handed_hold = 1;
                for (unsigned int at = 0; at < rects.count; at++)
                {
                        b32 ok = gpu_scene(COMPOSITOR_GPU_OPS_AT, g->count, COMPOSITOR_GPU_BINS_AT, out,
                                           (unsigned int)(compositor.scanout_pitch / 4), g->width, (unsigned int)compositor.height,
                                           (unsigned int)rects.r[at].x1, (unsigned int)rects.r[at].y1, (unsigned int)rects.r[at].x2,
                                           (unsigned int)rects.r[at].y2);
                        if (!ok)
                        {
                                compositor_gpu_fail(GPU_FAIL_UNHOLD);
                                return compositor_gpu_job_why();
                        }
                }
                gpu_handed_hold = 0;
                if (!gpu.pending)
                        gpu_handed_clear();
        }
        if (!compositor_gpu_state.failures && !compositor_gpu_state.told)
                compositor_gpu_state.told = 1, compositor_say("the GPU composes the desktop, frame records", g->count);
        if (!compositor_card.failed)
                compositor_gpu_state.failures = 0;
        return COMPOSITOR_GPU_WHY_DRAWN;
}

static void compositor_lists_set(int on);

// List mode follows the card: on while it composes (started, not given up), the setting has it and the switch is there, off otherwise, so that a program records
// its frames as lists only while the compositor can draw them. The table is changed when the card's state does (compositor_lists_set knocks every window), not
// made to agree with it at every present: the card going away (a hang's cool-down, canvas gpu off) puts every window back to painting its own pixels, and a check
// that sets the mode itself to draw lists with a renderer of its own is not undone by a present on a machine with no card.
//
// THE SWITCH: on by default, wherever the card composes in earnest (compositor_gpu_able); off while the file /run/moonwater/lists-off is there (looked for every
// 512th present: touch it, and remove it, to turn only the lists off; canvas gpu off turns the card off and the lists with it). A card that is up but cannot map the
// windows' pictures sends every list back to the program's pixels, which is guarded against there; a flicker seen with the lists on (2026-11-04) was not found on a
// card that composes, and moonwater debug says the lists drawn, refused and switched back to pixels.
static int compositor_lists_switch(void)
{
        static struct canvas_switch sw;

        return !canvas_switch_poll(&sw, "/run/moonwater/lists-off", 511);
}

// Whether the card is composing in earnest: a card that is up but refuses nearly every present (its maps of the window pictures fail, its scanout is one it cannot draw
// into) draws no list either, and the window would go between its list and its own pixels. Sixteen presents in a row refused for want of the card's ability (not a
// scene it does not draw yet: a landing, a preview) put it out, and it is back after thirty-two in a row that it drew. Looked at in the present that counted the last.
#define COMPOSITOR_GPU_ABLE_LOSE 16
#define COMPOSITOR_GPU_ABLE_WIN 32
static int compositor_gpu_able = 1;

static void compositor_gpu_able_count(enum compositor_gpu_why why)
{
        static unsigned int lost, won;

        if (why == COMPOSITOR_GPU_WHY_DRAWN)
        {
                lost = 0;
                if (!compositor_gpu_able && ++won >= COMPOSITOR_GPU_ABLE_WIN)
                        compositor_gpu_able = 1, won = 0;
        }
        else if (why == COMPOSITOR_GPU_WHY_PICTURE || why == COMPOSITOR_GPU_WHY_OUTPUT || why == COMPOSITOR_GPU_WHY_ALIGN || why == COMPOSITOR_GPU_WHY_BUSY ||
                 why == COMPOSITOR_GPU_WHY_HANG || why == COMPOSITOR_GPU_WHY_DEAD)
        {
                won = 0;
                if (compositor_gpu_able && ++lost >= COMPOSITOR_GPU_ABLE_LOSE)
                        compositor_gpu_able = 0, lost = 0;
        }
}

static void compositor_gpu_lists_sync(void)
{
        static int was;
        int want = compositor_gpu && compositor_gpu_state.up && !compositor_gpu_state.dead && compositor_gpu_able && compositor_lists_switch();

        if (compositor.table && want != was)
                was = want, compositor_lists_set(want);
}

// The GPU's issues on the board (window.c's windows_gpu_wanted, under the key canvas gpu): the state the table publishes for Settings
// (gpu_on, gpu_able, gpu_state, gpu_steps), then each code set when it holds now, with its numbers and the kernel's words, and cleared when
// it does not. A code is written only when what it says changed, so a present costs a compare and a board write is rare.
static void compositor_gpu_issues(void)
{
        static struct windows_issue last[WINDOWS_GPU_CODES];
        struct windows_issue want[WINDOWS_GPU_CODES];
        struct windows *table = compositor.table;
        unsigned int count;

        if (!table)
                return;
        table->gpu_on = (unsigned int)compositor_gpu, table->gpu_able = (unsigned int)compositor_gpu_able;
        // The card's fault text (gpu.c's gpu_fault) is on the board in the present that found it, not only when the table is published.
        table->gpu_faults = gpu_faults;
        memory_copy(table->gpu_fault, gpu_fault, sizeof(gpu_fault));
        table->gpu_state = compositor_gpu_state.dead ? (compositor_gpu_state.dead_why == COMPOSITOR_GPU_WHY_UNTRUSTED ? WINDOWS_GPU_UNTRUSTED
                                                       : compositor_gpu_state.hung_off ? WINDOWS_GPU_HUNG : WINDOWS_GPU_NO_CARD)
                           : compositor_gpu_state.cool_until && clock_monotonic_nanoseconds() < compositor_gpu_state.cool_until ? WINDOWS_GPU_COOLING
                           : compositor_gpu_state.up ? WINDOWS_GPU_UP : WINDOWS_GPU_NO_CARD;
        table->gpu_steps = compositor_gpu_state.cool_steps;
        memory_zero(want, sizeof(want));
        count = windows_gpu_wanted(table, want, WINDOWS_GPU_CODES);
        for (unsigned int slot = 0; slot < WINDOWS_GPU_CODES; slot++)
        {
                struct windows_issue *now = null;

                for (unsigned int k = 0; k < count; k++)
                        if (want[k].code == windows_gpu_codes[slot])
                                now = &want[k];
                if (now)
                {
                        now->used = 1;
                        if (!memory_compare(&last[slot], now, sizeof(*now)))
                                continue;
                        if (windows_issue_set(table, WINDOWS_KEY_GPU, now->severity, now->code, now->a, now->b, now->detail))
                                memory_copy(&last[slot], now, sizeof(*now));
                }
                else if (last[slot].used)
                {
                        windows_issue_clear(table, WINDOWS_KEY_GPU, windows_gpu_codes[slot]);
                        memory_zero(&last[slot], sizeof(last[slot]));
                }
        }
}

// The present's damage drawn by the GPU: 1 when it is, 0 when the processor is to draw it (and the reason is counted).
static int compositor_gpu_present(const struct compositor_frame *frames, unsigned int count)
{
        enum compositor_gpu_why why = compositor_gpu_draw(frames, count);

        compositor_gpu_able_count(why);
        compositor_gpu_lists_sync();
        // The board's issues are a function of counters that move at every present (the drawn count) and of faults and states that move at a refusal:
        // a refused present says its news at once, a drawn one every 64th, which is a second of the board's lag and a table walk and a few hundred
        // bytes copied less in each of the others.
        {
                static unsigned int issues_at;

                if ((why != COMPOSITOR_GPU_WHY_DRAWN && !(compositor_gpu_state.settled && why == COMPOSITOR_GPU_WHY_NO_CARD)) || (++issues_at & 63) == 0)
                        compositor_gpu_issues();
        }

        if (why == COMPOSITOR_GPU_WHY_DRAWN)
        {
                compositor_gpu_counts.drawn++;
                compositor_present_on_card = 1;
                compositor_gpu_publish();
                return 1;
        }
        compositor_gpu_refuse(why, 1);
        return 0;
}

/*
        GPU backdrop capture for frosted glass: renders frames[0..count-1] on the GPU over (x0,y0)-(x1,y1) into a
        cached-RAM memfd so compositor_frost never reads WC/tiled client dma-bufs over PCIe on the CPU.
        Preserves gpu_handed[] so the subsequent compositor_gpu_present still has every imported dma-buf in its BO list.
*/
static struct
{
        unsigned int *memory;
        unsigned long bytes;
        int fd;
} compositor_gpu_frost_buf;

static const unsigned int *compositor_gpu_frost_scene(const struct compositor_frame *frames, unsigned int count,
                                                      int x0, int y0, int x1, int y1, unsigned long *pitch)
{
        struct compositor_gscene *g = &compositor_gscene;
        struct compositor_frame wall;
        positive bytes, dst_va;
        b32 was_hold, ok;

        if (!compositor_gpu || !compositor_gpu_state.up || compositor_gpu_state.dead || compositor_gpu_held() ||
            !gpu_idle() || compositor_card.on || compositor.width <= 0 || compositor.height <= 0 || (compositor.width & 3))
                return null;
        if (x0 < 0)
                x0 = 0;
        if (y0 < 0)
                y0 = 0;
        if (x1 > compositor.width)
                x1 = compositor.width;
        if (y1 > compositor.height)
                y1 = compositor.height;
        if (x1 <= x0 || y1 <= y0)
                return null;
        for (unsigned int k = 0; k < count; k++)
                if (compositor_scaled(&frames[k]) || !compositor_gpu_picture(&frames[k]))
                        return null;
        g->width = (unsigned int)compositor.width, g->height = (unsigned int)compositor.height;
        if (compositor_gpu_wall_frame(&wall) && !compositor_gpu_picture(&wall))
                return null;
        bytes = 4ull * (positive)compositor.width * (positive)compositor.height;
        if (compositor_gpu_frost_buf.bytes < (unsigned long)bytes)
        {
                unsigned long grown = ((unsigned long)bytes + (1ul << 20) - 1) & ~((1ul << 20) - 1);
                long memfd, mapped;
                void *old = compositor_gpu_frost_buf.memory, *p = old;

                mem_buf_free(&p, &compositor_gpu_frost_buf.bytes, MEM_BUF_GPU_FORGET);
                if (old)
                        gpu_unregister_memfd(old);
                if (compositor_gpu_frost_buf.fd > 0)
                        system_close(compositor_gpu_frost_buf.fd - 1);
                compositor_gpu_frost_buf.memory = 0, compositor_gpu_frost_buf.fd = 0;
                memfd = compositor_call(syscall(memfd_create), (positive) "moonwater-frost", 1 | 2, 0, 0, 0, 0);
                if (memfd >= 0 && system_truncate_handle((int)memfd, (positive)grown) == 0 &&
                    compositor_call(syscall(fcntl), (positive)memfd, WINDOWS_F_ADD_SEALS, 2, 0, 0, 0) == 0)
                {
                        mapped = compositor_call(syscall(mmap), 0, (positive)grown, 3, 1, (positive)memfd, 0);
                        if (mapped < 0 && mapped > -4096)
                        {
                                system_close((int)memfd);
                                return null;
                        }
                        compositor_gpu_frost_buf.memory = (unsigned int *)mapped, compositor_gpu_frost_buf.bytes = grown;
                        compositor_gpu_frost_buf.fd = (int)memfd + 1;
                        gpu_register_memfd((int)memfd, compositor_gpu_frost_buf.memory, (positive)grown, 0);
                }
                else
                {
                        if (memfd >= 0)
                                system_close((int)memfd);
                        if (!mem_buf_grow(&p, &compositor_gpu_frost_buf.bytes, grown, 0))
                                return null;
                        compositor_gpu_frost_buf.memory = (unsigned int *)p;
                }
        }
        if (!(dst_va = gpu_host_va_written(compositor_gpu_frost_buf.memory, bytes, compositor_gpu_frost_buf.bytes, 1)))
                return null;
        if (!compositor_gpu_frames(frames, count, compositor_ground))
                return null;
        was_hold = gpu_handed_hold;
        gpu_handed_hold = 1;
        ok = gpu_scene(COMPOSITOR_GPU_OPS_AT, g->count, COMPOSITOR_GPU_BINS_AT, dst_va, (unsigned int)compositor.width,
                       g->width, g->height, (unsigned int)x0, (unsigned int)y0, (unsigned int)x1, (unsigned int)y1);
        gpu_handed_hold = was_hold;
        if (!ok)
        {
                compositor_gpu_fail(0);
                return null;
        }
        *pitch = (unsigned long)compositor.width;
        return compositor_gpu_frost_buf.memory;
}

// The machine slept, or the card was found lost: it is started again before the next present draws on it (compositor_gpu_restart_check).
static void compositor_gpu_resumed(void)
{
        compositor_gpu_state.restart_due = 1;
}

// Whether the machine slept since the last look. The boot clock runs through a sleep and the monotonic clock does not, so the boot clock's lead over the
// monotonic one grows by the length of the sleep: gap is that lead now, seen the lead at the last look (kept). The first look only takes it.
#define COMPOSITOR_GPU_SLEPT_NS 3000000000ull
static b32 compositor_gpu_slept(positive gap, positive *seen)
{
        b32 slept = *seen && gap > *seen + COMPOSITOR_GPU_SLEPT_NS;

        *seen = gap;
        return slept;
}

// The card is started again (compositor_gpu_recover: the maps let go, a new context, the self-test) when the machine slept, by the wake check's flag or by a
// lead that grew between two looks (presents, the loop). Only a card that is up is started again; the flag waits for it otherwise.
static void compositor_gpu_restart_check(void)
{
        if (compositor_gpu_slept(compositor_slept_clock(), &compositor_gpu_state.slept_seen))
                compositor_gpu_state.restart_due = 1;
        if (!compositor_gpu_state.restart_due || !compositor_gpu_state.up || compositor_gpu_state.dead)
                return;
        if (compositor_gpu_state.cool_until && clock_monotonic_nanoseconds() < compositor_gpu_state.cool_until)
                return; // after a hang the card is started again when its cool-down is over (compositor_gpu_job_failed)
        if (!gpu_idle())
                return; // a job given up on may still be on the card: a new context is not started on its queue until it is over
        compositor_gpu_state.restart_due = 0;
        compositor_gpu_recover();
}

// The late probe's deadline for the loop: 0 when there is nothing to look for (canvas gpu off, a card up, a card that drew its self-test wrong), else when
// the next look is due: the first one five seconds after the card was found missing, then (compositor_gpu_late_due) every five seconds for a minute and every minute.
static positive compositor_gpu_late_at(positive now)
{
        b32 gpu_want = compositor_gpu && !compositor_gpu_state.up && compositor_gpu_state.dead &&
                       compositor_gpu_state.dead_why == COMPOSITOR_GPU_WHY_NO_CARD;
#ifdef CONFIG_MOONWATER_WAYLAND
        b32 wl_want = compositor_wl_dmabuf_missing() != 0;
#else
        b32 wl_want = false;
#endif
        if (!gpu_want && !wl_want)
        {
                compositor_gpu_state.late_first = compositor_gpu_state.late_next = 0;
                return 0;
        }
        if (!compositor_gpu_state.late_first)
                compositor_gpu_late_due(now, &compositor_gpu_state.late_first, &compositor_gpu_state.late_next);
        return compositor_gpu_state.late_next;
}

// The loop has come to the late probe's time: the card is looked for again, and the next look is set. Started from its dead state, it is up, or dead again.
// A start that failed by what the nodes are (settled: a driver with no path, a chip with no kernels) is not started again while the render nodes are the ones
// it saw: the look is at the nodes (a version call each), and the card is started, and its outcome said, only when they have changed.
static void compositor_gpu_late_tick(positive now)
{
        if (!compositor_gpu_late_at(now) || !compositor_gpu_late_due(now, &compositor_gpu_state.late_first, &compositor_gpu_state.late_next))
                return;
        if (compositor_gpu && !compositor_gpu_state.up && compositor_gpu_state.dead &&
            compositor_gpu_state.dead_why == COMPOSITOR_GPU_WHY_NO_CARD && (!compositor_gpu_state.settled || gpu_nodes_seen() != compositor_gpu_state.settled))
        {
                compositor_gpu_state.dead = 0;
                if (compositor_gpu_up())
                {
                        compositor_gpu_lists_sync();
                        compositor_gpu_issues();
                }
        }
#ifdef CONFIG_MOONWATER_WAYLAND
        compositor_wl_dmabuf_refresh();
#endif
}

/*
        A program's list of ops (ui_draw_list), as the scene may have them. The list is written by a program the compositor does not trust, so nothing of it
        reaches the card as it stands: each op is read once into words of the compositor's own, and only the kinds a window is made of (a fill 0, a tone 1,
        an anti-aliased box 2 with the records 8 and 9 that follow it, a text run 3, a glyph translated through tones 10) are taken; a rectangle of a fill or a tone is cut to the picture, so that a
        card told to draw it draws in the picture alone; a text run must lie in the picture and its bitmap in the list's cover bytes (cover_at, cover_used), and
        its word 6 is made here: the alpha and stroke the program gave and the number of the base (slot) that is the list's address in the card's table, which
        the program does not name. The lanes of a text run read their neighbours by dwords, up to 64 pixels past the run's rows' ends and a row above and
        below; COMPOSITOR_GPU_LIST_REACH bytes either side of the cover bytes are mapped for them and a list with less room than that after its cover bytes
        has no text. Answers how many ops were written to `to` (room of them at most), or ~0 for a list that is refused: any other kind, a box whose records
        are not 8 or 9, a run outside what it may read, a count past the room. An op that draws nothing is left out.
*/
#define COMPOSITOR_GPU_LIST_REACH 4096ul
#define COMPOSITOR_GPU_LIST_REFUSED (~0u)
#define COMPOSITOR_GPU_LIST_COORD (1 << 24) // a box's place, size and radii in 256ths: a card does not take more (65536 pixels)
static inline INLINE CONST int compositor_gpu_list_cut(unsigned int word, int low, int high)
{
        int value = (int)word;

        return value < low ? low : value > high ? high : value;
}

HOT static unsigned int compositor_gpu_list_ops(const unsigned int *restrict from, unsigned int count, positive cover_at, positive cover_used, positive list_bytes, int width,
                                                int height, unsigned int slot, unsigned int *restrict to, unsigned int room)
{
        struct op8 { unsigned int v[8]; };
        unsigned int n = 0;
        b32 text_room = cover_at >= COMPOSITOR_GPU_LIST_REACH && cover_at <= list_bytes && cover_used <= list_bytes - cover_at &&
                        list_bytes - cover_at - cover_used >= COMPOSITOR_GPU_LIST_REACH;

        if (unlikely(width <= 0 || height <= 0 || slot < 1 || slot > 255))
                return COMPOSITOR_GPU_LIST_REFUSED;
        for (unsigned int i = 0; i < count; i++)
        {
                struct op8 snap = *(const volatile struct op8 *)(from + 8 * (positive)i);
                unsigned int *w = snap.v, kind;

                kind = w[0] & 255;
                if (kind <= 1)
                {
                        int x0 = compositor_gpu_list_cut(w[1], 0, width), y0 = compositor_gpu_list_cut(w[2], 0, height);
                        int x1 = compositor_gpu_list_cut(w[3], 0, width), y1 = compositor_gpu_list_cut(w[4], 0, height);

                        if (unlikely(w[0] >> 8))
                                return COMPOSITOR_GPU_LIST_REFUSED;
                        if (x0 >= x1 || y0 >= y1)
                                continue;
                        if (unlikely(n >= room))
                                return COMPOSITOR_GPU_LIST_REFUSED;
                        to[8 * n] = kind, to[8 * n + 1] = (unsigned int)x0, to[8 * n + 2] = (unsigned int)y0, to[8 * n + 3] = (unsigned int)x1;
                        to[8 * n + 4] = (unsigned int)y1, to[8 * n + 5] = w[5], to[8 * n + 6] = to[8 * n + 7] = 0;
                        n++;
                }
                else if (kind == 2)
                {
                        unsigned int records = w[0] >> 8;

                        if (unlikely(records > 3 || records > count - 1 - i || n + 1 + records > room))
                                return COMPOSITOR_GPU_LIST_REFUSED;
                        for (unsigned int k = 0; k <= records; k++)
                        {
                                struct op8 rsnap;
                                unsigned int *r = rsnap.v;

                                if (k)
                                {
                                        rsnap = *(const volatile struct op8 *)(from + 8 * (positive)(i + k));
                                        if (unlikely(r[0] != 8 && r[0] != 9))
                                                return COMPOSITOR_GPU_LIST_REFUSED;
                                }
                                else
                                        rsnap = snap;
                                to[8 * n] = k ? r[0] : 2u | records << 8;
                                to[8 * n + 1] = (unsigned int)compositor_gpu_list_cut(r[1], -COMPOSITOR_GPU_LIST_COORD, COMPOSITOR_GPU_LIST_COORD);
                                to[8 * n + 2] = (unsigned int)compositor_gpu_list_cut(r[2], -COMPOSITOR_GPU_LIST_COORD, COMPOSITOR_GPU_LIST_COORD);
                                to[8 * n + 3] = (unsigned int)compositor_gpu_list_cut(r[3], -COMPOSITOR_GPU_LIST_COORD, COMPOSITOR_GPU_LIST_COORD);
                                to[8 * n + 4] = (unsigned int)compositor_gpu_list_cut(r[4], -COMPOSITOR_GPU_LIST_COORD, COMPOSITOR_GPU_LIST_COORD);
                                to[8 * n + 5] = r[5];
                                to[8 * n + 6] = (unsigned int)compositor_gpu_list_cut(r[6], 0, COMPOSITOR_GPU_LIST_COORD);
                                to[8 * n + 7] = (unsigned int)compositor_gpu_list_cut(r[7], 0, COMPOSITOR_GPU_LIST_COORD);
                                n++;
                        }
                        i += records;
                }
                else if (kind == 3)
                {
                        positive pitch = w[0] >> 8, bytes_from;
                        int x0 = (int)w[1], y0 = (int)w[2], x1 = (int)w[3], y1 = (int)w[4];

                        if (!text_room || pitch < 3 || pitch > 3500 || x0 < 0 || y0 < 0 || x1 > width || y1 > height)
                                return COMPOSITOR_GPU_LIST_REFUSED;
                        if (x0 >= x1 || y0 >= y1)
                                continue;
                        if (n >= room || (positive)(x1 - x0) + 2 > pitch)
                                return COMPOSITOR_GPU_LIST_REFUSED;
                        // The bitmap's bytes, a row above and below the rows and a byte past either end: all of them cover bytes.
                        bytes_from = (positive)w[7] - pitch - 1;
                        if (w[7] < pitch + 1 || bytes_from < cover_at || (positive)w[7] > cover_at + cover_used ||
                            (positive)(y1 - y0) * pitch + (positive)(x1 - x0) + 1 > cover_at + cover_used - (positive)w[7])
                                return COMPOSITOR_GPU_LIST_REFUSED;
                        to[8 * n] = 3u | (unsigned int)pitch << 8, to[8 * n + 1] = (unsigned int)x0, to[8 * n + 2] = (unsigned int)y0, to[8 * n + 3] = (unsigned int)x1;
                        to[8 * n + 4] = (unsigned int)y1, to[8 * n + 5] = w[5], to[8 * n + 6] = (w[6] & 0xffffu) | slot << 16, to[8 * n + 7] = w[7];
                        n++;
                }
                else if (kind == 10)
                {
                        // A glyph translated through 256 tones (font_cell's cell on its ground): the rectangle in the picture, the coverage bytes (rows of pitch, the first
                        // pixel's at word 7) and the tones (256 words at word 5, aligned) both in the cover bytes; the lanes read round the coverage as a text run's do.
                        positive pitch = w[0] >> 8, cols, rows, table = w[5], at = w[7];
                        int x0 = (int)w[1], y0 = (int)w[2], x1 = (int)w[3], y1 = (int)w[4];

                        if (!text_room || x0 < 0 || y0 < 0 || x1 > width || y1 > height)
                                return COMPOSITOR_GPU_LIST_REFUSED;
                        if (x0 >= x1 || y0 >= y1)
                                continue;
                        cols = (positive)(x1 - x0), rows = (positive)(y1 - y0);
                        if (n >= room || pitch < cols || pitch > 3500 || (table & 3) || table < cover_at || table > cover_at + cover_used ||
                            1024 > cover_at + cover_used - table || at < cover_at || at > cover_at + cover_used ||
                            (rows - 1) * pitch + cols > cover_at + cover_used - at)
                                return COMPOSITOR_GPU_LIST_REFUSED;
                        to[8 * n] = 10u | (unsigned int)pitch << 8, to[8 * n + 1] = (unsigned int)x0, to[8 * n + 2] = (unsigned int)y0, to[8 * n + 3] = (unsigned int)x1;
                        to[8 * n + 4] = (unsigned int)y1, to[8 * n + 5] = w[5], to[8 * n + 6] = slot << 16, to[8 * n + 7] = w[7];
                        n++;
                }
                else
                        return COMPOSITOR_GPU_LIST_REFUSED;
        }
        return n;
}

/*
        A list's job submitted without a wait (compositor_card.lists_nowait): 1 submitted, the row's picture the job's until the loop's turn has seen it done
        (compositor_card_lists_settle); 0 refused, compositor_list_why saying why. The card's slots or the bins' sides held by running jobs (gpu_end LATE)
        is no failure of the card's: the frame is refused as the card's being busy (COMPOSITOR_LIST_CARD) and nothing is counted against the card.
*/
static int compositor_card_list_submit(int row, unsigned int made, positive picture_va, unsigned int pitch, unsigned int w4, unsigned int height, int x0, int y0,
                                       int x1, int y1)
{
        positive sequence = gpu_scene_submit(COMPOSITOR_GPU_LIST_OPS_AT, made, COMPOSITOR_GPU_SCENE_BINS_AT, picture_va, pitch, w4, height, (unsigned int)x0,
                                             (unsigned int)y0, (unsigned int)x1, (unsigned int)y1);

        if (!sequence)
        {
                if (gpu_end == GPU_END_LATE && !gpu_lost)
                        return compositor_list_busy = true, compositor_list_why = COMPOSITOR_LIST_CARD, 0;
                compositor_gpu_fail(0);
                return compositor_list_why = COMPOSITOR_LIST_JOB, 0;
        }
        if (!rows_any(&compositor_card.list_rows))
                compositor_card.list_began = gpu_job_began;
        compositor_card.list_seq[row] = sequence, rows_add(&compositor_card.list_rows, row), compositor_card.list_recovered = gpu_recovered;
        compositor_gpu_state.failures = 0;
        return 1;
}

/*
        A program's list drawn into its row's picture by the card: the list (list_bytes of it, a program's shared memory), the picture (a half of the row in
        the window table, pitch pixels a row) and the damage of the frame answered in damage (x0, y0, x1, y1 in the picture). The program paints no pixels in
        this mode and the compositor is the only writer of the picture, so the card may write it; the card is given the picture as a map it may write and the
        list's cover bytes (with the margin its lanes read into) as a map to read, whose address the ops reach through the table of bases at the number the cut
        gave this row. Answers 1 for a frame drawn (or one with nothing to draw), 0 for a list that is not taken or a card that is busy, away or hung: the
        program then paints that frame in the picture itself. The hand scene kernel draws whole patches of 64 pixels by 4 rows, so the width it is told is the
        picture's rounded up to a multiple of 4, which the pitch must hold: no op of the cut reaches past the picture's own width.
*/
static int compositor_gpu_list_render_bytes(int row, unsigned char *list, unsigned long list_bytes, unsigned int *picture, unsigned long pitch, int width, int height,
                                            int *damage)
{
        struct compositor_gscene *g = &compositor_gscene;
        struct ui_draw_view view;
        unsigned int w4 = ((unsigned int)width + 3) & ~3u, slot = 1 + (unsigned int)row % 255, made;
        positive address_to bases = (positive address_to)((p8 address_to)gpu.at + GPU_BASES_AT), cover_from, cover_to, cover_va = 0, picture_va, bytes;
        int x0, y0, x1, y1;

        damage[0] = damage[1] = damage[2] = damage[3] = 0;
        compositor_list_why = COMPOSITOR_LIST_DRAWN;
        if (row < 0 || row >= COMPOSITOR_ROWS || width <= 0 || height <= 0 || !picture || pitch < w4 || (pitch & 3))
                return compositor_list_why = COMPOSITOR_LIST_OPEN, 0;
        // Not while a present's jobs are on the card (compositor_card): its records and tables are the card's until they are done. Nor while any job runs, a
        // list's drawn without a wait among them: its records, its row's base and its picture are that job's.
        compositor_list_busy = false;
        if (!compositor_gpu || compositor_gpu_state.dead || compositor_gpu_held() || !compositor_gpu_up())
                return compositor_list_why = COMPOSITOR_LIST_CARD, 0;
        // Busy, which passes: the take waits for the card (compositor_lists_take), where off or gone it refuses.
        if (compositor_card.on || !gpu_idle())
                return compositor_list_busy = true, compositor_list_why = COMPOSITOR_LIST_CARD, 0;
        if (ui_draw_list_open(list, list_bytes, &view) != 1 || (int)view.width != width || (int)view.height != height)
                return compositor_list_why = COMPOSITOR_LIST_OPEN, 0;
        made = compositor_gpu_list_ops(view.ops, view.count, view.cover_at, view.cover_used, list_bytes, width, height, slot,
                                       compositor_card.lists_nowait ? (unsigned int *)((unsigned char *)gpu.at + COMPOSITOR_GPU_LIST_OPS_AT) : g->ops,
                                       compositor_card.lists_nowait ? COMPOSITOR_GPU_LIST_OPS / 32 : g->room);
        if (made == COMPOSITOR_GPU_LIST_REFUSED)
                return compositor_list_why = COMPOSITOR_LIST_CUT, 0;
        if (!ui_draw_list_still(list, &view)) // the generation moved under the read: the next present reads the new one
                return compositor_list_why = COMPOSITOR_LIST_TORN, 0;
        x0 = view.x0 < 0 ? 0 : view.x0 > width ? width : view.x0, y0 = view.y0 < 0 ? 0 : view.y0 > height ? height : view.y0;
        x1 = view.x1 < 0 ? 0 : view.x1 > width ? width : view.x1, y1 = view.y1 < 0 ? 0 : view.y1 > height ? height : view.y1;
        if (!made || x1 <= x0 || y1 <= y0)
                return 1;
        if (view.cover_used)
        {
                cover_from = (positive)list + view.cover_at - COMPOSITOR_GPU_LIST_REACH, cover_to = (positive)list + view.cover_at + view.cover_used + COMPOSITOR_GPU_LIST_REACH;
                // The map of an earlier frame holds this one's cover bytes when the span is inside it: a frame's cover bytes are a few more or less than the last's, and a map
                // a frame was a userptr, a mapping of the card's and, 120 presents on, both undone again (697 maps for 665 frames in the animation lane, and a map named in
                // every job of its present). A map made is rounded up to 64 KiB, inside the list, so the frames after it are in it.
                if (!(cover_va = gpu_va_bytes((const void *)cover_from, cover_to - cover_from)))
                {
                        positive last = (positive)list + list_bytes, room = (cover_to + 65535) & ~65535ull;

                        room = room > last ? last : room;
                        room = room < cover_to ? cover_to : room;
                        gpu_forget((const void *)cover_from, room - cover_from);
                        if (!gpu_map((const void *)cover_from, room - cover_from) || !(cover_va = gpu_va_bytes((const void *)cover_from, cover_to - cover_from)))
                        {
                                if (gpu_lost)
                                        compositor_gpu_recover();
                                return compositor_list_why = COMPOSITOR_LIST_MAPS, 0;
                        }
                }
                bases[slot] = cover_va - (view.cover_at - COMPOSITOR_GPU_LIST_REACH);
        }
        bytes = 4 * ((positive)pitch * (positive)(height - 1) + w4);
        if (!(picture_va = gpu_host_va_written(picture, bytes, bytes, 1)))
                return compositor_list_why = COMPOSITOR_LIST_MAPS, 0;
        if (compositor_card.lists_nowait)
        {
                if (!compositor_card_list_submit(row, made, picture_va, (unsigned int)pitch, w4, (unsigned int)height, x0, y0, x1, y1))
                        return 0;
        }
        else if (!gpu_scene(COMPOSITOR_GPU_OPS_AT, made, COMPOSITOR_GPU_BINS_AT, picture_va, (unsigned int)pitch, w4, (unsigned int)height, (unsigned int)x0,
                            (unsigned int)y0, (unsigned int)x1, (unsigned int)y1))
        {
                compositor_gpu_fail(GPU_FAIL_FINISH);
                return compositor_list_why = COMPOSITOR_LIST_JOB, 0;
        }
        compositor_gpu_state.failures = 0;
        damage[0] = x0, damage[1] = y0, damage[2] = x1 - x0, damage[3] = y1 - y0;
        return 1;
}

// GPU clock hold & wake on input / fullscreen game (async worker thread avoids blocking compositor loop).
#define COMPOSITOR_GPU_CLOCKS_HOLD 500000000ull
#define COMPOSITOR_GPU_CLOCKS_LEVEL 0u
static struct
{
        unsigned int level, epoch, want, want_epoch, want_lock;
        b32 held, refused, input, asked, answered, error, started, inline_asks, game, game_off;
        positive until;
        int want_fd;
} compositor_gpu_clocks = {.level = COMPOSITOR_GPU_CLOCKS_LEVEL, .want_fd = -1};

static void compositor_gpu_clocks_lock(void)
{
        while (__atomic_exchange_n(&compositor_gpu_clocks.want_lock, 1u, __ATOMIC_ACQUIRE))
                while (__atomic_load_n(&compositor_gpu_clocks.want_lock, __ATOMIC_RELAXED))
                        ;
}

static void compositor_gpu_clocks_unlock(void)
{
        __atomic_store_n(&compositor_gpu_clocks.want_lock, 0u, __ATOMIC_RELEASE);
}

// The thread the driver is asked on: it sleeps on the loop's ask count, and answers each newest ask in turn (an ask overtaken while the driver was
// answering the one before is not made: only the newest level matters).
// The newest ask is taken whole under the lock (its level, epoch and the descriptor made for it, which is the answerer's to close), so a level is
// never paired with another ask's card; one whose card was let go of since (its epoch is old) is not made. Nothing taken: the newest was already.
static void compositor_gpu_clocks_answer(void)
{
        b32 asked = __atomic_load_n(&compositor_gpu_clocks.asked, __ATOMIC_ACQUIRE);
        unsigned int level, epoch, e = __atomic_load_n(&compositor_gpu_clocks.error, __ATOMIC_RELAXED);
        int fd;

        compositor_gpu_clocks_lock();
        level = compositor_gpu_clocks.want, epoch = compositor_gpu_clocks.want_epoch, fd = compositor_gpu_clocks.want_fd;
        compositor_gpu_clocks.want_fd = -1;
        compositor_gpu_clocks_unlock();
        if (fd >= 0)
        {
                e = epoch == __atomic_load_n(&gpu_epoch, __ATOMIC_ACQUIRE) ? gpu_clocks(fd, epoch, level) : 0;
                GPU_UNDUP(fd);
        }
        __atomic_store_n(&compositor_gpu_clocks.error, e, __ATOMIC_RELAXED);
        __atomic_store_n(&compositor_gpu_clocks.answered, asked, __ATOMIC_RELEASE);
}

static fn compositor_gpu_clocks_run(address_any context)
{
        (void)context;
        compositor_ordinary(0); // no frame waits for it: ordinary scheduling, not the loop's real time it was started with
        for (;;)
        {
                b32 asked = __atomic_load_n(&compositor_gpu_clocks.asked, __ATOMIC_ACQUIRE);

                if (asked == __atomic_load_n(&compositor_gpu_clocks.answered, __ATOMIC_ACQUIRE))
                        thread_wait(&compositor_gpu_clocks.asked, asked);
                else
                        compositor_gpu_clocks_answer();
        }
}

// The loop's ask of a level (0: let go) of the card as it is now: handed to the thread, or made in place where there is none.
static void compositor_gpu_clocks_ask(unsigned int level)
{
        // The ask's words are written under the lock before its count, and the thread takes them whole after reading the count: an ask the loop
        // makes while the thread answers the one before is answered next, the count having moved. The descriptor is a duplicate of the card's, so
        // a card the loop lets go of (gpu_stop) is never another file's number in the thread's hands; one an ask overtook is closed here.
        int fd = gpu.fd >= 0 ? GPU_DUP(gpu.fd) : -1, old;

        compositor_gpu_clocks_lock();
        old = compositor_gpu_clocks.want_fd;
        compositor_gpu_clocks.want = level, compositor_gpu_clocks.want_fd = fd < 0 ? -1 : fd, compositor_gpu_clocks.want_epoch = gpu_epoch;
        compositor_gpu_clocks_unlock();
        if (old >= 0)
                GPU_UNDUP(old);
        __atomic_store_n(&compositor_gpu_clocks.asked, compositor_gpu_clocks.asked + 1, __ATOMIC_RELEASE);
        if (!compositor_gpu_clocks.started && !compositor_gpu_clocks.inline_asks)
        {
                compositor_gpu_clocks.started = 1;
                compositor_gpu_clocks.inline_asks = thread_start(compositor_gpu_clocks_run, null) == null;
        }
        if (compositor_gpu_clocks.inline_asks)
                compositor_gpu_clocks_answer();
        else
                thread_wake(&compositor_gpu_clocks.asked, 1);
}

// Whether the card composes in earnest: on, up and not dead.
static b32 compositor_gpu_awake_wanted(void)
{
        return compositor_gpu && compositor_gpu_state.up && !compositor_gpu_state.dead;
}

// An input event on a card that composes (input.c, as a device's reports are read): noted for the turn's end.
static void compositor_gpu_wake(void)
{
        compositor_gpu_clocks.input = 1;
}

// The turn's input forwarded: the hold asked, or held on for another COMPOSITOR_GPU_CLOCKS_HOLD; a card with no hold, or one that refused it, has
// its idle clock woken (gpu_wake: a tiny job, only when it has been idle a while). A refusal is known once the thread has answered the ask.
static void compositor_gpu_input_turn(positive now)
{
        b32 answered = __atomic_load_n(&compositor_gpu_clocks.answered, __ATOMIC_ACQUIRE);

        if (compositor_gpu_clocks.held && answered == compositor_gpu_clocks.asked && __atomic_load_n(&compositor_gpu_clocks.error, __ATOMIC_RELAXED))
                compositor_gpu_clocks.held = 0, compositor_gpu_clocks.refused = 1;
        if (!compositor_gpu_clocks.input)
                return;
        compositor_gpu_clocks.input = 0;
        if (!compositor_gpu_awake_wanted())
                return;
        if (compositor_gpu_clocks.refused && compositor_gpu_clocks.epoch != gpu_epoch)
                compositor_gpu_clocks.refused = 0; // a card started again: asked again
        if (compositor_gpu_clocks.level && !compositor_gpu_clocks.refused)
        {
                compositor_gpu_clocks.until = now + COMPOSITOR_GPU_CLOCKS_HOLD;
                if (compositor_gpu_clocks.held && compositor_gpu_clocks.epoch == gpu_epoch)
                        return;
                compositor_gpu_clocks.held = 1, compositor_gpu_clocks.epoch = gpu_epoch;
                compositor_gpu_clocks_ask(compositor_gpu_clocks.level);
                if (!compositor_gpu_clocks.inline_asks || !compositor_gpu_clocks.error)
                        return;
                compositor_gpu_clocks.held = 0, compositor_gpu_clocks.refused = 1;
        }
        gpu_wake();
}

// When the loop is to let the hold go (0: nothing held).
static positive compositor_gpu_clocks_at(void)
{
        return compositor_gpu_clocks.held && !compositor_gpu_clocks.game ? compositor_gpu_clocks.until : 0;
}

// The hold let go of at its end, or as soon as the card does not compose (canvas gpu off, a card dead). A card started again since the hold has a
// new descriptor: the old context went with the old one, and its level with it, so nothing is asked.
static void compositor_gpu_clocks_tick(positive now)
{
        if (!compositor_gpu_clocks.held || ((compositor_gpu_clocks.game || now < compositor_gpu_clocks.until) && compositor_gpu_awake_wanted()))
                return;
        compositor_gpu_clocks.held = 0, compositor_gpu_clocks.game = 0;
        if (compositor_gpu_clocks.epoch == gpu_epoch)
                compositor_gpu_clocks_ask(0);
}

// Hold AMDGPU_CTX_STABLE_PSTATE_PEAK (4) while a fullscreen game is active on AC power (0 W extra at the desktop; cuts GPU
// present time and mouse-to-photon latency by ~3 ms by eliminating 600 MHz -> 2200 MHz clock ramps).
static void compositor_gpu_game_clocks(int on)
{
        if (on)
        {
                if (compositor_gpu_clocks.game || compositor_gpu_clocks.game_off || compositor_gpu_clocks.refused || !compositor_gpu_awake_wanted())
                        return;
                compositor_gpu_clocks.game = 1, compositor_gpu_clocks.held = 1, compositor_gpu_clocks.epoch = gpu_epoch;
                compositor_gpu_clocks_ask(4u);
                return;
        }
        if (!compositor_gpu_clocks.game)
                return;
        compositor_gpu_clocks.game = 0, compositor_gpu_clocks.held = 0;
        if (compositor_gpu_clocks.epoch == gpu_epoch)
                compositor_gpu_clocks_ask(0u);
}

// A screen Canvas turns, the damage of it (x0, y0, x1, y1 of the screen) turned from the scanout into the screen's own buffer by the GPU (windows_rotate_u32's turn:
// one op over the whole panel with a step along its rows and one down them): 1 when it is, 0 when the processor is to.
static int compositor_gpu_rotate(unsigned int at, const int *screen, unsigned int degrees, unsigned int x0, unsigned int y0, unsigned int x1, unsigned int y1)
{
        struct drm_dumb *turn = &compositor_turns[at];
        struct compositor_gscene *g = &compositor_gscene;
        unsigned int lw = (unsigned int)screen[2], lh = (unsigned int)screen[3], pw = degrees == 180 ? lw : lh, ph = degrees == 180 ? lh : lw, panel[4];
        long pitch = (long)(compositor.scanout_pitch / 4), origin = (long)screen[1] * pitch + screen[0], first, stepx, stepy;
        unsigned int *op;
        positive from, to;

        if (!compositor_gpu_state.up || compositor_gpu_state.dead || !compositor_gpu_state.out || compositor.card < 0 || !gpu_idle() || compositor_gpu_held()) // a job given up on may read the import it lets go of
                return 0;
        if ((degrees != 90 && degrees != 180 && degrees != 270) || !pw || !ph || pw > 16384 || ph > 16384 || (pw & 3) || (turn->pitch & 15) || turn->pitch / 4 < pw)
                return 0;
        if (compositor_gpu_state.turn_key[at] != turn->handle || !compositor_gpu_state.turn_va[at])
        {
                gpu_unimport(compositor_gpu_state.turn_va[at], GPU_GONE_UNIMPORT);
                compositor_gpu_state.turn_va[at] = gpu_import(compositor.card, turn->handle, turn->bytes), compositor_gpu_state.turn_key[at] = turn->handle;
        }
        if (!compositor_gpu_state.turn_va[at])
                return 0;
        gpu_import_handed(compositor_gpu_state.out);
        gpu_import_handed(compositor_gpu_state.turn_va[at]);
        if (degrees == 90)
                stepx = -pitch, stepy = 1, first = origin + ((long)lh - 1) * pitch;
        else if (degrees == 270)
                stepx = pitch, stepy = -1, first = origin + (long)lw - 1;
        else
                stepx = -1, stepy = -pitch, first = origin + ((long)lh - 1) * pitch + (long)lw - 1;
        from = compositor_gpu_state.out + 4ull * (positive)first;
        to = compositor_gpu_state.turn_va[at];
        op = g->ops;
        op[0] = 7u | (unsigned int)(stepx + 0x800000) << 8, op[1] = op[2] = 0, op[3] = pw, op[4] = ph;
        op[5] = (unsigned int)from, op[6] = (unsigned int)(from >> 32), op[7] = (unsigned int)stepy;
        windows_rotate_panel(lw, lh, degrees, x0, y0, x1, y1, panel);
        if (!gpu_scene(COMPOSITOR_GPU_OPS_AT, 1, COMPOSITOR_GPU_BINS_AT, to, (unsigned int)(turn->pitch / 4), pw, ph, panel[0], panel[1], panel[2], panel[3]))
        {
                compositor_gpu_fail(GPU_FAIL_FINISH);
                return 0;
        }
        compositor_gpu_state.failures = 0;
        return 1;
}

// Single-pass tiled/DCC sampling in the RDNA2 scene kernel is disabled until its sync/fallback/DCC lifecycle is verified
// on live hardware (enabling it on RDNA2 caused black tears, wallpaper bleed, and cursor damage-rect artifacts in Chrome;
// tiled/DCC DMA-BUFs use the proven async gpu_image_go path in compositor_wl_card_image instead).
static int compositor_gpu_tiled_ready(void)
{
        return 0;
}
#else
static int compositor_gpu_tiled_ready(void)
{
        return 0;
}
static void compositor_gpu_game_clocks(int on)
{
        (void)on;
}
#endif

/*
        Native Canvas external-buffer GPU import cache:
        protocol-neutral dma-buf fd identity (statx dev/ino), epoch-checked live lookup,
        single cached gpu_import_fd() slot lifecycle, and pool-remap pointer tracking.
*/
struct canvas_ext_import
{
        const unsigned char *map;
        long fd, size;
        unsigned long dev, ino;
        positive va;
        unsigned int epoch;
};

static void canvas_ext_fd_ident(long fd, unsigned long *dev, unsigned long *ino)
{
        unsigned char info[256];

        *dev = 0, *ino = 0;
        if (fd < 0)
                return;
        if (compositor_call(syscall(statx), (positive)fd, (positive)"", 0x1000, 0x7ff, (positive)info, 0) < 0)
                return;
        *ino = *(unsigned long *)(info + 32);
        *dev = (((unsigned long)*(unsigned int *)(info + 136)) << 32) | *(unsigned int *)(info + 140);
}

static positive canvas_ext_import_live(positive va, unsigned int epoch)
{
#ifdef MOONWATER_GPU_C
        if (va && epoch == gpu_epoch)
        {
                gpu_import_handed(va);
                return va;
        }
#else
        (void)va, (void)epoch;
#endif
        return 0;
}

static void canvas_ext_import_release(struct canvas_ext_import *ref)
{
        if (!ref)
                return;
#ifdef MOONWATER_GPU_C
        if (ref->va && ref->epoch == gpu_epoch)
                gpu_release_import(ref->va);
#endif
        memory_zero(ref, sizeof(*ref));
        ref->fd = -1;
}

static int canvas_ext_import_same(const struct canvas_ext_import *ref, const unsigned char *map, long fd, long size,
                                  unsigned long dev, unsigned long ino)
{
        return ref->map == map && ref->fd == fd && ref->size == size && ref->dev == dev && ref->ino == ino &&
               (!ref->va || canvas_ext_import_live(ref->va, ref->epoch));
}

static void canvas_ext_import_remap(struct canvas_ext_import *ref, const unsigned int **pixels, const void *old_at, long new_at)
{
        if (!ref || ref->map != (const unsigned char *)old_at)
                return;
        if (pixels && *pixels)
                *pixels = (const unsigned int *)((const unsigned char *)*pixels + (new_at - (long)old_at));
        ref->map = (const unsigned char *)new_at;
}

HOT static positive canvas_ext_import_cached(struct canvas_ext_import *restrict ref, const unsigned char *map, long fd, long size,
                                             unsigned long dev, unsigned long ino, int barred, int *restrict reused)
{
#ifdef MOONWATER_GPU_C
        positive va;

        if (unlikely(!ref))
                return 0;
        if (likely(ref->va && canvas_ext_import_same(ref, map, fd, size, dev, ino)))
        {
                if (reused)
                        *reused = 1;
                return ref->va;
        }
        canvas_ext_import_release(ref);
        if (barred || fd < 0 || size <= 0)
                return 0;
        va = gpu_import_fd((int)fd, (positive)size);
        if (!va)
                return 0;
        ref->map = map, ref->fd = fd, ref->size = size, ref->dev = dev, ref->ino = ino;
        ref->va = va, ref->epoch = gpu_epoch;
        if (reused)
                *reused = 0;
        return va;
#else
        (void)ref, (void)map, (void)fd, (void)size, (void)dev, (void)ino, (void)barred, (void)reused;
        return 0;
#endif
}

