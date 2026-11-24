/*
        The capture engine's video source (src/cast/cast.h): the rings of the shares the table's casts hold, made when the person
        has picked what to share and composed into at the presents.

        A share asked (WINDOWS_CAST_ASKED) is said to the machine script once (share_button: the share app opens beside the tray
        with what is asked); a share picked (PICKED) gets its ring: a memfd of WINDOWS_CAST_SLOTS frames of the source's size, its
        rows the width in whole 64-byte lines, mapped here and sealed against any other writable mapping before the sink is told
        (LIVE), its slots black and opaque. A share that is no longer picked or live loses its ring. A window that goes, or whose
        row is another program's, ends its share (STOPPED).

        At each present that composed (compositor_cast_present, after the frames are drawn and the flip told: the pictures are
        still the present's own), the present's damage is joined to what each slot of each live ring has not had yet (a slot's
        stale), and the slot to write next is composed where it is stale: the same frames composed again into it, as a screen's
        rectangle of the space, or a window's own frame and layers at its corner, then made opaque and published with the
        present's damage. So a slot is always what the source showed at its frame, whatever it missed while it was held. A
        screen shared composes every present (nothing is shown straight or on a plane while one is: compositor_cast_screens), so
        a fullscreen window is in the capture as on the screen. With no share there is nothing here but the look at the table,
        when its cast_serial moves.
*/

struct compositor_cast
{
        unsigned int ticket;            // the share the ring is for (0: none)
        unsigned int kind, target;
        int x, y, width, height;        // the source in the space: a screen's rectangle, a window's
        unsigned int stride, slots;
        unsigned long slot_bytes, bytes;
        int fd;                         // the ring's memfd plus one (0: none)
        unsigned char *ring;
        int stale[WINDOWS_CAST_SLOTS][4]; // what each slot has not had since it was written: x1, y1, x2, y2 in the space (x2 <= x1: nothing)
};

static struct compositor_cast compositor_casts[WINDOWS_CASTS];
static unsigned int compositor_cast_seen = ~0u; // the table's cast_serial looked at last
static unsigned int compositor_cast_screens_live; // shares of a screen with a ring
static unsigned int compositor_cast_rings;      // shares with a ring
static int compositor_cast_wake = -1;           // the sink's door, open while a ring is

static void compositor_script_say(const char *name); // input.c
static int taskbar_share_set(int present);           // ui/taskbar.c: the tray's share cell there or not

// Whether a screen is shared: then every present is composed (no window straight, no plane), so the capture is what is shown.
static int compositor_cast_screens(void)
{
        return compositor_cast_screens_live != 0;
}

static void compositor_cast_drop(struct compositor_cast *c)
{
        if (c->ring)
                compositor_call(syscall(munmap), (positive)c->ring, c->bytes, 0, 0, 0, 0);
        if (c->fd > 0)
                system_close(c->fd - 1);
        memory_zero(c, sizeof(*c));
}

// Joins rect (x1, y1, x2, y2) into each slot's stale, cut to the source.
static void compositor_cast_stale(struct compositor_cast *c, const int *rect)
{
        int r[4] = {rect[0] > c->x ? rect[0] : c->x, rect[1] > c->y ? rect[1] : c->y, rect[2] < c->x + c->width ? rect[2] : c->x + c->width,
                    rect[3] < c->y + c->height ? rect[3] : c->y + c->height};

        if (r[2] <= r[0] || r[3] <= r[1])
                return;
        for (unsigned int k = 0; k < c->slots; k++)
        {
                int *s = c->stale[k];

                if (s[2] <= s[0] || s[3] <= s[1])
                        memory_copy(s, r, sizeof(r));
                else
                {
                        s[0] = r[0] < s[0] ? r[0] : s[0], s[1] = r[1] < s[1] ? r[1] : s[1];
                        s[2] = r[2] > s[2] ? r[2] : s[2], s[3] = r[3] > s[3] ? r[3] : s[3];
                }
        }
}

// Where a share's source is now: 1 with its rectangle in the space, 0 when it is not there (a screen gone, a window closed or another's).
static int compositor_cast_where(const struct windows_cast *row, int *x, int *y, int *w, int *h)
{
        if (row->kind == WINDOWS_CAST_SCREEN)
        {
                if ((int)row->target >= compositor_output_count())
                        return 0;
                compositor_output_rect((int)row->target, x, y, w, h);
                return *w > 0 && *h > 0;
        }
        if (row->kind == WINDOWS_CAST_WINDOW)
        {
                int r = (int)row->target;

                if (r < 0 || (unsigned int)r >= compositor_rows_in_use || !compositor.table || compositor.table->pid[r] != row->target_pid ||
                    !compositor.w[r] || !compositor.h[r])
                        return 0;
                *x = compositor.x[r], *y = compositor.y[r], *w = (int)compositor.w[r], *h = (int)compositor.h[r];
                return 1;
        }
        return 0;
}

// The ring of a share picked: made, sealed, black, and the share made live; 0, or -1 (the share is stopped).
static int compositor_cast_make(unsigned int at, struct windows_cast *row)
{
        struct compositor_cast *c = &compositor_casts[at];
        int x, y, w, h;
        long fd, mapped;

        compositor_cast_drop(c);
        if (!compositor_cast_where(row, &x, &y, &w, &h) || w > 16384 || h > 16384)
                return -1;
        c->kind = row->kind, c->target = row->target, c->x = x, c->y = y, c->width = w, c->height = h;
        c->stride = ((unsigned int)w * 4 + 63) & ~63u;
        c->slots = WINDOWS_CAST_SLOTS;
        c->slot_bytes = ((unsigned long)c->stride * (unsigned long)h + 4095) & ~4095ul;
        c->bytes = c->slot_bytes * c->slots;
        fd = compositor_call(syscall(memfd_create), (positive) "moonwater-cast", 1 | 2, 0, 0, 0, 0); // MFD_CLOEXEC | MFD_ALLOW_SEALING
        if (fd < 0)
                return -1;
        c->fd = (int)fd + 1;
        if (compositor_call(syscall(ftruncate), (positive)fd, c->bytes, 0, 0, 0, 0) < 0 ||
            ((mapped = compositor_call(syscall(mmap), 0, c->bytes, 3, 1, (positive)fd, 0)) < 0 && mapped > -4096))
        {
                compositor_cast_drop(c);
                return -1;
        }
        c->ring = (unsigned char *)mapped;
        // F_ADD_SEALS: no shrinking, no growing, no other writable mapping or write ever (only this one), and no more seals
        if (compositor_call(syscall(fcntl), (positive)fd, 1033, 2 | 4 | 16 | 1, 0, 0, 0) < 0)
        {
                compositor_cast_drop(c);
                return -1;
        }
        for (unsigned int k = 0; k < c->slots; k++)
        {
                memory_fill_u32((unsigned int *)(c->ring + k * c->slot_bytes), c->slot_bytes / 4, 0xff000000u);
                c->stale[k][0] = x, c->stale[k][1] = y, c->stale[k][2] = x + w, c->stale[k][3] = y + h;
                row->seq[k] = 0, row->held[k] = 0, row->damages[k] = 0;
        }
        c->ticket = row->ticket;
        row->width = (unsigned int)w, row->height = (unsigned int)h, row->stride = c->stride, row->slots = c->slots;
        row->slot_bytes = c->slot_bytes, row->rate = compositor.table ? compositor.table->output_now[0] : 0;
        row->rate = row->rate ? row->rate : 60;
        row->ring_fd = c->fd - 1, row->ring_pid = (unsigned int)system_call(syscall(getpid));
        __atomic_store_n(&row->latest, ~0u, __ATOMIC_RELEASE);
        if (compositor_cast_wake >= 0)
                system_close(compositor_cast_wake);
        // the sink's door, opened again for each ring (a sink started again made it new); read and write, so it never fills a pipe with no reader
        compositor_cast_wake = (int)system_open_at(AT_FDCWD, (string_address)WINDOWS_CAST_WAKE, FILE_READ_WRITE | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW);
        compositor_damage(x, y, w, h);
        return 0;
}

// The table's casts looked at: asks said, rings made for picks and let go for the rest. When cast_serial moved, or `always`.
static void compositor_cast_look(int always)
{
        struct windows *table = compositor.table;
        unsigned int serial, screens = 0, rings = 0, changed = 0;

        if (!table)
                return;
        serial = __atomic_load_n(&table->cast_serial, __ATOMIC_ACQUIRE);
        if (serial == compositor_cast_seen && !always)
                return;
        compositor_cast_seen = serial;
        for (unsigned int at = 0; at < WINDOWS_CASTS; at++)
        {
                struct windows_cast *row = &table->casts[at];
                struct compositor_cast *c = &compositor_casts[at];
                unsigned int state = __atomic_load_n(&row->state, __ATOMIC_ACQUIRE), zero = 0;
                int x, y, w, h;

                if (state == WINDOWS_CAST_ASKED && __atomic_compare_exchange_n(&row->said, &zero, 1u, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
                        compositor_script_say("share_button");
                if (state == WINDOWS_CAST_PICKED)
                {
                        unsigned int picked = WINDOWS_CAST_PICKED;

                        if (compositor_cast_make(at, row) < 0)
                        {
                                compositor_cast_drop(c);
                                __atomic_compare_exchange_n(&row->state, &picked, WINDOWS_CAST_STOPPED, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
                        }
                        else
                                __atomic_compare_exchange_n(&row->state, &picked, WINDOWS_CAST_LIVE, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
                        changed = 1;
                        state = __atomic_load_n(&row->state, __ATOMIC_ACQUIRE);
                }
                if (state == WINDOWS_CAST_LIVE && c->ticket == row->ticket && c->ticket && !compositor_cast_where(row, &x, &y, &w, &h))
                {
                        unsigned int live = WINDOWS_CAST_LIVE;

                        __atomic_compare_exchange_n(&row->state, &live, WINDOWS_CAST_STOPPED, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
                        state = WINDOWS_CAST_STOPPED, changed = 1;
                }
                if ((state != WINDOWS_CAST_LIVE || c->ticket != row->ticket) && c->ticket)
                        compositor_cast_drop(c);
                if (c->ticket)
                        rings++, screens += c->kind == WINDOWS_CAST_SCREEN;
        }
        if (screens != compositor_cast_screens_live)
                compositor_damage(0, 0, compositor.width, compositor.height); // shown straight or composed, the screen made again
        compositor_cast_screens_live = screens, compositor_cast_rings = rings;
        {
                unsigned int shown = 0;

                for (unsigned int at = 0; at < WINDOWS_CASTS; at++)
                {
                        unsigned int state = __atomic_load_n(&table->casts[at].state, __ATOMIC_ACQUIRE);

                        shown |= state == WINDOWS_CAST_ASKED || state == WINDOWS_CAST_PICKED || state == WINDOWS_CAST_LIVE;
                }
                if (taskbar_share_set((int)shown))
                        compositor_tabs_changed();
        }
        if (!rings && compositor_cast_wake >= 0)
                system_close(compositor_cast_wake), compositor_cast_wake = -1;
        if (changed)
                __atomic_add_fetch(&table->cast_serial, 1u, __ATOMIC_ACQ_REL), compositor_cast_seen++;
}

/*
        A present composed: its damage (x1, y1, x2, y2 in the space) joined to every live ring's slots, and the next slot of each
        that has any composed from the present's frames (`windows` of them the rows', each of `rows`) and published.
*/
static void compositor_cast_present(const struct compositor_frame *frames, unsigned int count, const unsigned int *rows, unsigned int windows,
                                    const int (*damage)[4], unsigned int damages)
{
        static struct compositor_frame own[1 + COMPOSITOR_LAYERS];
        static unsigned int own_rows[1 + COMPOSITOR_LAYERS];
        struct windows *table = compositor.table;
        unsigned long long now;
        int published = 0;

        if (!compositor_cast_rings || !table)
                return;
        now = clock_monotonic_nanoseconds();
        for (unsigned int at = 0; at < WINDOWS_CASTS; at++)
        {
                struct compositor_cast *c = &compositor_casts[at];
                struct windows_cast *row = &table->casts[at];
                int x, y, w, h, k, told[WINDOWS_CAST_DAMAGE][4], *s;
                unsigned int n = 0;
                const struct compositor_frame *drawn = frames;
                unsigned int drawn_count = count;

                if (!c->ticket || row->ticket != c->ticket || __atomic_load_n(&row->state, __ATOMIC_ACQUIRE) != WINDOWS_CAST_LIVE)
                        continue;
                // a window moved: its frames are drawn at its new corner, and all of it is stale
                if (c->kind == WINDOWS_CAST_WINDOW && compositor_cast_where(row, &x, &y, &w, &h) && (x != c->x || y != c->y))
                {
                        int whole[4] = {x, y, x + c->width, y + c->height};

                        c->x = x, c->y = y;
                        compositor_cast_stale(c, whole);
                }
                for (unsigned int d = 0; d < damages; d++)
                {
                        int r[4] = {damage[d][0] > c->x ? damage[d][0] : c->x, damage[d][1] > c->y ? damage[d][1] : c->y,
                                    damage[d][2] < c->x + c->width ? damage[d][2] : c->x + c->width,
                                    damage[d][3] < c->y + c->height ? damage[d][3] : c->y + c->height};

                        if (r[2] <= r[0] || r[3] <= r[1])
                                continue;
                        compositor_cast_stale(c, r);
                        if (n < WINDOWS_CAST_DAMAGE)
                                told[n][0] = r[0] - c->x, told[n][1] = r[1] - c->y, told[n][2] = r[2] - r[0], told[n][3] = r[3] - r[1];
                        n++;
                }
                if ((k = cast_write_begin(row)) < 0)
                        continue; // every slot held: what changed waits in the stale of each
                s = c->stale[k];
                if (s[2] <= s[0] || s[3] <= s[1])
                {
                        // nothing new for this slot: it is let go as it was
                        __atomic_store_n(&row->seq[k], __atomic_load_n(&row->seq[k], __ATOMIC_RELAXED) & ~1u, __ATOMIC_RELEASE);
                        continue;
                }
                if (c->kind == WINDOWS_CAST_WINDOW)
                {
                        int r = (int)c->target;

                        compositor_row_prepare(r);
                        compositor_frame_of(r, &own[0]);
                        own_rows[0] = (unsigned int)r;
                        if (own[0].tiled_kind)
                                compositor_wl_straight_settle(r, 0, (long)compositor.h[r]), own[0].tiled_kind = 0;
                        drawn = own, drawn_count = 1 + compositor_wl_layers_frames(r, own, own_rows, 1);
                        compositor_corner_masks_ready(own, drawn_count);
                }
                else
                        for (unsigned int f = 0; f < windows && f < count; f++)
                                if (frames[f].tiled_kind)
                                        compositor_wl_straight_settle((int)rows[f], 0, (long)compositor.h[rows[f]]);
                {
                        struct compositor_target t = {(unsigned int *)(c->ring + (unsigned long)k * c->slot_bytes), c->stride / 4, c->width, c->height,
                                                      s[0], s[1], s[2], s[3], c->x, c->y, 0, 0};
                        int done[2];

                        compositor_scene_banded(&t, drawn, drawn_count, 0, done);
                        if (c->kind == WINDOWS_CAST_SCREEN && compositor_spots_any(0))
                                compositor_spots_compose(&t, drawn, drawn_count);
                        if (c->kind == WINDOWS_CAST_SCREEN)
                                compositor_cursor_compose(&t, drawn, drawn_count);
                        // opaque, as the sink says the frames are (BGRA as well as BGRx)
                        for (int py = s[1]; py < s[3]; py++)
                        {
                                unsigned int *p = t.pixels + (unsigned long)(py - c->y) * t.pitch + (unsigned long)(s[0] - c->x);

                                for (int px = 0; px < s[2] - s[0]; px++)
                                        p[px] |= 0xff000000u;
                        }
                }
                s[0] = s[1] = s[2] = s[3] = 0;
                cast_write_end(row, (unsigned int)k, now, (const int (*)[4])told, n);
                published = 1;
        }
        if (published && compositor_cast_wake >= 0)
                system_write_once(compositor_cast_wake, "f", 1);
}

static void compositor_cast_stop(void)
{
        for (unsigned int at = 0; at < WINDOWS_CASTS; at++)
                if (compositor_casts[at].ticket)
                        compositor_cast_drop(&compositor_casts[at]);
        if (compositor_cast_wake >= 0)
                system_close(compositor_cast_wake), compositor_cast_wake = -1;
}
