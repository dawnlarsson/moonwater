/*
        Wayland peer bridge for Canvas (included by compositor.c after wayland.c when CONFIG_MOONWATER_WAYLAND
        is on). Serves /run/moonwater/wayland-0 (mode 0666), plugs into Canvas via canvas_ext_register,
        compositor_dnd_register, and compositor_clip_observer_register, and protects pool reads via SIGBUS zero-mapping.
*/
#ifndef WAYLAND_HOST_INCLUDED
#define WAYLAND_HOST_INCLUDED

#define COMPOSITOR_WL_PATH "/run/moonwater/wayland-0"
#define COMPOSITOR_WL_BACKLOG 16
#define COMPOSITOR_WL_READ 16384
#define COMPOSITOR_WL_READS 32
#define COMPOSITOR_WL_ACCEPTS 4
#define COMPOSITOR_WL_SIGBUS 7
#define COMPOSITOR_WL_SIGPIPE 13
#define COMPOSITOR_WL_SIGALRM 14
#define COMPOSITOR_WL_TRANSFERS_MOST (1l << 20)
#define COMPOSITOR_WL_PULL_NS 3000000000ull
#define COMPOSITOR_WL_PUSH_NS 5000000000ull
#define COMPOSITOR_WL_CHUNK 4096
#define COMPOSITOR_WL_PULL 1
#define COMPOSITOR_WL_PUSH 2
#define COMPOSITOR_WL_PUMP 3
#define COMPOSITOR_WL_PUMP_NS 10000000000ull
#define COMPOSITOR_WL_DMABUF_WAIT_NS 12000000ull
#define COMPOSITOR_WL_STRAIGHT_WAIT_NS 8000000ull
#define COMPOSITOR_WL_DMABUF_SLOW_NS 100000000ull
#define COMPOSITOR_WL_KEYMAP_ROOM 24576
#define COMPOSITOR_WL_CARD_COPY_PIXELS 262144

struct compositor_wl_transfer
{
        unsigned char state, which;
        int client, row, in, out, keep, index, finished, cancel, nudge;
        unsigned long total, base, count, got;
        long reading, text, nth;
        const char *drop;
        thread *worker;
};

#define CANVAS_BUFFER_REF_FIELDS \
        union { \
                struct canvas_ext_import imp; \
                struct { const unsigned char *map; long fd, size; unsigned long dev, ino; positive import; unsigned int epoch; }; \
        }; \
        positive va; \
        unsigned long modifier; \
        unsigned int fourcc, offset

#define CANVAS_SCAN_FB_FIELDS \
        unsigned int scan_fb, scan_fourcc, scan_x, scan_y; \
        unsigned char scan_failed

struct canvas_scan_fb { CANVAS_SCAN_FB_FIELDS; };
struct compositor_wl_client { int uid; unsigned char asked; positive slow_until; };

static struct
{
        int listen, *fd, *pid, started, handler, wake, hold_row, hold;
        unsigned char *barred, *cursor_asked, *row_blend, *wc_reads, *wc_copy;
        struct compositor_wl_client *client;
        long slots, polled, transfers, dmabuf_table_size, dmabuf_entry_count, zero_row, map_live, map_hint, drag_reading;
        struct wayland_table fd_table, pid_table, barred_table, client_table, transfer_table, map_table, map_index;
        unsigned long frames, keymap_size[16], screens_told, tiled_rects, tiled_refused[8], presents, render;
        struct { unsigned char open, touched, full, which, last; int x1, y1, x2, y2; } *laying;
        struct { int count; int rect[WAYLAND_DAMAGE][4]; } *parts;
        int keymap[16], dmabuf_table, dmabuf_tiled, tiled_node_epoch, tiled_node, tiled_row, tiled_lent;
        int presenting_waiting, landed, render_hw, syncobj_ok, lent_synced;
        unsigned int layout_told, landed_seq, wc_presents, wc_seeded;
        long repeat_told[2];
        struct compositor_wl_transfer *transfer;
        positive tiled_ns, tiled_most_ns, tiled_pixels, tiled_seq, presenting_since, latched_at, landed_when;
        unsigned long tiled_unwaited;
        unsigned int direct_gen;
        struct { unsigned int format; unsigned long modifier; } dmabuf_entries[WAYLAND_DMABUF_ENTRIES];
        struct compositor_wl_opaque { short x1, y1, x2, y2; } *row_opaque;
        char render_path[64];
        struct canvas_syncobj_pool syncobj_pool;
        struct compositor_wl_mapping *map;
        struct
        {
                const unsigned int *pixels;
                unsigned long pitch;
                long width, height;
                CANVAS_BUFFER_REF_FIELDS;
                union { struct canvas_scan_fb scan; struct { CANVAS_SCAN_FB_FIELDS; }; };
                int synced;
        } *lent;
        struct
        {
                struct wayland_picture picture;
                unsigned long dev, ino;
                union { struct canvas_scan_fb scan; struct { CANVAS_SCAN_FB_FIELDS; }; };
                unsigned char live, owed;
        } *straight;
        struct canvas_ext_import *source;
        struct { unsigned int fourcc, pitch, offset, width, height, count; unsigned long long modifier; int error; } scan_refused;
} compositor_wl = {.listen = -1, .wake = -1, .syncobj_pool = {.drm_fd = -1}, .drag_reading = -1};

struct compositor_wl_layer
{
        const unsigned int *pixels;
        unsigned long pitch, luma, chroma, luma_pitch, chroma_pitch, extent;
        int x, y, width, height, blend, synced, swap, gather, nv12;
        unsigned int id, at;
        CANVAS_BUFFER_REF_FIELDS;
        long source_w, source_h, transform, y_invert, sx, sy, sw, sh, tw, th, ox, oy;
};

struct compositor_wl_gathered
{
        unsigned int *memory;
        positive bytes, va;
        int fd, filled;
        unsigned int id, epoch;
};

static struct
{
        struct compositor_wl_layer (*layer)[WAYLAND_LAYERS];
        struct compositor_wl_gathered (*gathered)[WAYLAND_LAYERS];
        unsigned char *count, *root_refused, off;
        unsigned int total;
        int pending_row, pending_count, pending_box[4], pending[WAYLAND_DAMAGE][4];
} compositor_wl_layers = {.pending_row = -1};

_Static_assert(COMPOSITOR_LAYERS >= WAYLAND_LAYERS && COMPOSITOR_LAYERS <= WINDOWS * WAYLAND_LAYERS, "the layers' room is COMPOSITOR_LAYERS");

/* ---- the host ---- */

static void *compositor_wl_alloc(void *context, long bytes)
{
        (void)context;
        return compositor_map_zeroed((unsigned long)bytes);
}

static void compositor_wl_release(void *context, void *block, long bytes)
{
        (void)context;
        compositor_call(syscall(munmap), (positive)block, (positive)bytes, 0, 0, 0, 0);
}

/*
        The shim's tables (struct wayland_table): address space reserved with no access and no memory behind it (PROT_NONE,
        MAP_NORESERVE), as much as asked and never more than the machine's memory (sysinfo's totalram), so a table cannot fill before
        the memory does; and committed a step at a time by mprotect, which the kernel charges then, and refuses (ENOMEM) when the
        memory is not there: the one request that wanted it is refused, not the compositor. Pages are zero when first touched.
*/
static void *compositor_wl_reserve(void *context, long *bytes)
{
        static unsigned long memory;
        long got;

        (void)context;
        if (!memory)
        {
                unsigned char info[128];

                memory_zero(info, sizeof info);
                if (compositor_call(syscall(sysinfo), (positive)info, 0, 0, 0, 0, 0) == 0)
                        memory = *(unsigned long *)(info + 32) * (*(unsigned int *)(info + 104) ? *(unsigned int *)(info + 104) : 1);
                if (!memory)
                        memory = 1ul << 34;
        }
        if ((unsigned long)*bytes > memory)
                *bytes = (long)memory;
        got = compositor_call(syscall(mmap), 0, (positive)*bytes, 0, 0x4022, (positive)-1, 0); // PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE
        return got < 0 && got > -4096 ? 0 : (void *)got;
}

static int compositor_wl_commit(void *context, void *at, long bytes)
{
        (void)context;
        return compositor_call(syscall(mprotect), (positive)at, (positive)bytes, 3, 0, 0, 0) == 0;
}

static void compositor_wl_unreserve(void *context, void *at, long bytes)
{
        (void)context;
        compositor_call(syscall(munmap), (positive)at, (positive)bytes, 0, 0, 0, 0);
}

/*
        Whether a file's pages are memory the kernel holds itself: a file of tmpfs (a memfd, /dev/shm and a file under /run are that), of
        ramfs or of hugetlbfs, by the filesystem the descriptor is on (fstatfs, f_type first). Every other file's pages are read from
        somewhere when they are touched, and the compositor touches a pool's pages in its own loop: a file of a filesystem the client
        serves itself (FUSE) is read by asking the client, which need never answer, and the compositor then waits in the page fault for
        as long as the client likes, deaf to every window and to SIGKILL (MW-2026-195). A network's file or a disk's waits on a server
        or a device the same way. What libwayland's clients make a pool of is a memfd or a file of the runtime directory, which are these.
*/
static int compositor_wl_memory_file(long fd)
{
        unsigned long info[32];

        memory_zero(info, sizeof info);
        if (compositor_call(syscall(fstatfs), (positive)fd, (positive)info, 0, 0, 0, 0) < 0)
                return 0;
        return info[0] == 0x01021994ul || info[0] == 0x858458f6ul || info[0] == 0x958458f6ul; // tmpfs, ramfs, hugetlbfs
}

/* A regular file of at least size bytes whose pages are memory (compositor_wl_memory_file), mapped read-only and shared; null for anything else. */
struct compositor_wl_mapping
{
        volatile unsigned long at, size;
        volatile int bad;
};

static const struct wayland_host compositor_wl_host;

/* ---- shared open-addressing hash index (used by wayland_host map_index [64-bit key] and xwm window_index/atom_index [32-bit key]) ---- */

static unsigned long canvas_htab_key(const struct wayland_table *records, long slot, int key64)
{
        const void *p = records->at + (unsigned long)slot * records->unit;

        return key64 ? *(const volatile unsigned long *)p : (unsigned long)*(const unsigned int *)p;
}

static unsigned long canvas_htab_hash(unsigned long key, long buckets, int key64)
{
        if (key64)
                return ((key >> 12) * 0x9e3779b97f4a7c15ul >> 20) & (unsigned long)(buckets - 1);
        return wayland_u32_hash((unsigned int)key, buckets);
}

static void canvas_htab_put(struct wayland_table *index, unsigned long key, long slot, int key64)
{
        unsigned int *bucket = (unsigned int *)(void *)index->at;
        unsigned long b = canvas_htab_hash(key, index->count, key64);

        while (bucket[b])
                b = (b + 1) & (unsigned long)(index->count - 1);
        bucket[b] = (unsigned int)(slot + 1);
}

static int canvas_htab_grow(struct wayland_table *index, const struct wayland_table *records, long used, long skip, int key64)
{
        struct wayland_table grown = {0, sizeof(unsigned int), 0, 0, 0, 0, 0, 0, 0, 0};

        wayland_tables_host(&compositor_wl_host);
        if (index->count && 2 * (used + 1) <= index->count)
                return 1;
        if (!wayland_table_reach(&grown, index->count ? 2 * index->count : 64))
                return 0;
        for (long at = 0; at < records->count; at++)
                if (at != skip)
                {
                        unsigned long key = canvas_htab_key(records, at, key64);

                        if (key)
                                canvas_htab_put(&grown, key, at, key64);
                }
        wayland_table_drop(index);
        *index = grown;
        return 1;
}

static long canvas_htab_find(const struct wayland_table *index, const struct wayland_table *records, unsigned long key,
                             int first, unsigned long *at, int key64)
{
        long buckets = index->count, found = -1;
        const unsigned int *bucket = (const unsigned int *)(const void *)index->at;

        for (unsigned long b = buckets ? canvas_htab_hash(key, buckets, key64) : 0, n = 0; buckets && n < (unsigned long)buckets;
             n++, b = (b + 1) & (unsigned long)(buckets - 1))
        {
                if (!bucket[b])
                        return found;
                if (canvas_htab_key(records, (long)bucket[b] - 1, key64) == key)
                {
                        long slot = (long)bucket[b] - 1;

                        if (!first)
                        {
                                if (at)
                                        *at = b;
                                return slot;
                        }
                        if (found < 0 || slot < found)
                                found = slot;
                }
        }
        return found;
}

static void canvas_htab_del(struct wayland_table *index, const struct wayland_table *records, unsigned long hole, int key64)
{
        long buckets = index->count;
        unsigned int *bucket = (unsigned int *)(void *)index->at;

        bucket[hole] = 0;
        for (unsigned long b = (hole + 1) & (unsigned long)(buckets - 1); bucket[b]; b = (b + 1) & (unsigned long)(buckets - 1))
        {
                unsigned long home = canvas_htab_hash(canvas_htab_key(records, (long)bucket[b] - 1, key64), buckets, key64);

                if ((b > hole && (home <= hole || home > b)) || (b < hole && home <= hole && home > b))
                {
                        bucket[hole] = bucket[b];
                        bucket[b] = 0;
                        hole = b;
                }
        }
}

/*
        The host's arrays of a client slot reached to `slots` (the shim's own grow first, wayland_connect): tables that never move,
        made by the compositor's host, a new slot's connection none. 1 when they are; 0 when the memory is not there.
*/
static int compositor_wl_slots_reach(long slots)
{
        long was = compositor_wl.slots;
        struct { struct wayland_table *t; unsigned long unit; } specs[4] = {
                {&compositor_wl.fd_table, sizeof(int)},
                {&compositor_wl.pid_table, sizeof(int)},
                {&compositor_wl.barred_table, 1},
                {&compositor_wl.client_table, sizeof(struct compositor_wl_client)},
        };

        if (slots <= was)
                return 1;
        wayland_tables_host(&compositor_wl_host);
        for (unsigned int i = 0; i < 4; i++)
        {
                specs[i].t->unit = specs[i].unit;
                if (!wayland_table_reach(specs[i].t, slots))
                        return 0;
        }
        compositor_wl.fd = (int *)compositor_wl.fd_table.at;
        compositor_wl.pid = (int *)compositor_wl.pid_table.at;
        compositor_wl.barred = compositor_wl.barred_table.at;
        compositor_wl.client = (struct compositor_wl_client *)compositor_wl.client_table.at;
        for (long at = was; at < slots; at++)
        {
                compositor_wl.fd[at] = -1, compositor_wl.pid[at] = 0, compositor_wl.barred[at] = 0;
                memory_zero(&compositor_wl.client[at], sizeof compositor_wl.client[at]);
        }
        compositor_wl.slots = slots;
        return 1;
}

static void compositor_wl_map_forget(unsigned long at)
{
        unsigned long hole = 0;
        long slot = canvas_htab_find(&compositor_wl.map_index, &compositor_wl.map_table, at, 0, &hole, 1);

        if (slot < 0)
                return;
        canvas_htab_del(&compositor_wl.map_index, &compositor_wl.map_table, hole, 1);
        compositor_wl.map[slot].at = 0;
        compositor_wl.map[slot].size = 0;
        compositor_wl.map[slot].bad = 0;
        wayland_table_give(&compositor_wl.map_table, slot);
        compositor_wl.map_live--;
}

// A mapping noted: its slot, or 0 when there is no memory for it (the pool is refused, and the caller unmaps it).
static int compositor_wl_map_note(unsigned long at, unsigned long size)
{
        long slot;

        if (!canvas_htab_grow(&compositor_wl.map_index, &compositor_wl.map_table, compositor_wl.map_live, -1, 1))
                return 0;
        slot = wayland_table_take(&compositor_wl.map_table);
        if (slot < 0)
                return 0;
        compositor_wl.map = (struct compositor_wl_mapping *)compositor_wl.map_table.at;
        compositor_wl.map[slot].bad = 0;
        compositor_wl.map[slot].size = size;
        __atomic_store_n(&compositor_wl.map[slot].at, at, __ATOMIC_RELEASE);
        compositor_wl.map_live++;
        canvas_htab_put(&compositor_wl.map_index, at, slot, 1);
        return 1;
}

static void *compositor_wl_map(void *context, long fd, long size)
{
        // struct statx: stx_mode at 28 (u16), stx_size at 40 (u64); asked for the type and the size, of the descriptor itself.
        unsigned char info[256];
        long got;
        unsigned int mode;
        unsigned long length;

        (void)context;
        memory_zero(info, sizeof info);
        got = compositor_call(syscall(statx), (positive)fd, (positive) "", 0x1000, 0x201, (positive)info, 0);
        if (got < 0)
                return 0;
        mode = (unsigned int)info[28] | (unsigned int)info[29] << 8;
        length = *(unsigned long *)(info + 40);
        if ((mode & 0170000) != 0100000 || length < (unsigned long)size || !compositor_wl_memory_file(fd))
                return 0;
        got = compositor_call(syscall(mmap), 0, (positive)size, 1, 1, (positive)fd, 0);
        if (got < 0 && got > -4096)
                return 0;
        if (compositor_wl_map_note((unsigned long)got, (unsigned long)size))
                return (void *)got;
        compositor_call(syscall(munmap), (positive)got, (positive)size, 0, 0, 0, 0);
        return 0;
}

static void *compositor_wl_remap(void *context, void *at, long old_size, long new_size)
{
        long got;

        (void)context;
        // The card's maps of the old pages are let go of: the pages move (a map of them would be the old ones).
        gpu_forget(at, (positive)old_size);
        got = compositor_call(syscall(mremap), (positive)at, (positive)old_size, (positive)new_size, 1, 0, 0);
        if (got < 0 && got > -4096)
                return 0;
        if (canvas_htab_find(&compositor_wl.map_index, &compositor_wl.map_table, (unsigned long)at, 0, 0, 1) >= 0)
        {
                compositor_wl_map_forget((unsigned long)at);
                if (!compositor_wl_map_note((unsigned long)got, (unsigned long)new_size))
                {
                        compositor_call(syscall(munmap), (positive)got, (positive)new_size, 0, 0, 0, 0);
                        return 0;
                }
        }
        // Update lent, damage-source, and layer references whose pool moved on mremap.
        for (int row = 0; row < (int)compositor_rows_in_use; row++)
        {
                if (compositor_wl.lent[row].pixels)
                        canvas_ext_import_remap(&compositor_wl.lent[row].imp, &compositor_wl.lent[row].pixels, at, got);
                canvas_ext_import_remap(&compositor_wl.source[row], 0, at, got);
                for (unsigned int k = 0; k < compositor_wl_layers.count[row]; k++)
                        canvas_ext_import_remap(&compositor_wl_layers.layer[row][k].imp, &compositor_wl_layers.layer[row][k].pixels, at, got);
        }
        return (void *)got;
}

static void compositor_wl_unlend(int row);
static int compositor_straight(int row);
static void compositor_wl_straight_pay(int row);
static void compositor_wl_scan_retry(int row);
static void compositor_wl_straight_drop(int row);

static void compositor_wl_source_drop(int row)
{
        if (row >= 0 && row < COMPOSITOR_ROWS)
                canvas_ext_import_release(&compositor_wl.source[row]);
}

static void compositor_wl_layers_drop(int row);

static void compositor_wl_unmap(void *context, void *at, long size)
{
        (void)context;
        if (!at || at == WAYLAND_DMABUF_UNMAPPED)
                return;
        gpu_forget(at, (positive)size);
        for (int row = 0; row < (int)compositor_rows_in_use; row++)
        {
                if (compositor_wl.source[row].map == (const unsigned char *)at)
                        compositor_wl_source_drop(row);
                if (compositor_lent(row) && compositor_wl.lent[row].map == (const unsigned char *)at)
                        compositor_wl_unlend(row);
                for (unsigned int k = 0; k < compositor_wl_layers.count[row]; k++)
                        if (compositor_wl_layers.layer[row][k].imp.map == (const unsigned char *)at)
                        {
                                compositor_wl_layers_drop(row);
                                break;
                        }
        }
        compositor_wl_map_forget((unsigned long)at);
        compositor_call(syscall(munmap), (positive)at, (positive)size, 0, 0, 0, 0);
}

static void compositor_wl_close(void *context, long fd)
{
        (void)context;
        if (fd >= 0)
        {
                for (int row = 0; row < (int)compositor_rows_in_use; row++)
                {
                        if (compositor_wl.source[row].va && compositor_wl.source[row].fd == fd)
                                compositor_wl_source_drop(row);
                        if (compositor_lent(row) && compositor_wl.lent[row].fd == fd)
                                compositor_wl_unlend(row);
                        // A straight picture's buffer going: an owed read is made while the descriptor is still the buffer's.
                        if (compositor_straight(row) && compositor_wl.straight[row].picture.sync_fd == fd)
                        {
                                compositor_wl_straight_pay(row);
                                compositor_wl_straight_drop(row);
                        }
                        for (unsigned int k = 0; k < compositor_wl_layers.count[row]; k++)
                                if (compositor_wl_layers.layer[row][k].fd == fd)
                                {
                                        compositor_wl_layers_drop(row);
                                        break;
                                }
                }
        }
        system_close((int)fd);
}

// A chroma plane's mapping (an NV12 picture's plane 1, mapped for one draw) noted as a pool's is, so that a buffer cut under the read is zeros
// (compositor_wl_bus) and not the compositor's end; the extent was checked first (compositor_wl_dmabuf_holds), so this is the second guard. False when
// it cannot be noted: the mapping is let go of and the picture drawn without its colour, as one with no plane 1 is.
static int compositor_wl_uv_noted(long got, unsigned long size)
{
        if (compositor_wl_map_note((unsigned long)got, size))
                return 1;
        compositor_call(syscall(munmap), (positive)got, (positive)size, 0, 0, 0, 0);
        return 0;
}

/*
        A pool whose file was cut under the compositor: the signal handler of SIGBUS. The faulting address names the pool;
        zero pages are mapped over all of it, so the read that faulted goes on and finds zeros, and the pool is marked.
        A SIGBUS that is not on a pool is not this handler's: it is put back to its default and returned from, which
        raises it again and ends the compositor as it would have ended.
*/
static void compositor_wl_bus(int number, void *info, void *context)
{
        unsigned long where = *(unsigned long *)((unsigned char *)info + 16);

        (void)context;
        for (long slot = 0; compositor_wl.map && slot < compositor_wl.map_table.count; slot++)
                if (compositor_wl.map[slot].at && where >= compositor_wl.map[slot].at &&
                    where - compositor_wl.map[slot].at < compositor_wl.map[slot].size)
                {
                        // PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED
                        compositor_call(syscall(mmap), compositor_wl.map[slot].at, compositor_wl.map[slot].size, 1, 0x32,
                                        (positive)-1, 0);
                        compositor_wl.map[slot].bad = 1;
                        return;
                }
        system_signal_install((b32)number, 0, 0, 0, null);
}

/* Whether the pool the pixels are in was cut. */
static int compositor_wl_cut(const void *pixels)
{
        unsigned long at = (unsigned long)pixels;
        long hint = compositor_wl.map_hint;

        // The mapping the last question was answered by first: a frame asks of the same few pools again and again.
        if (compositor_wl.map && hint < compositor_wl.map_table.count && compositor_wl.map[hint].at && at >= compositor_wl.map[hint].at &&
            at - compositor_wl.map[hint].at < compositor_wl.map[hint].size)
                return compositor_wl.map[hint].bad;
        for (long slot = 0; compositor_wl.map && slot < compositor_wl.map_table.count; slot++)
                if (compositor_wl.map[slot].at && at >= compositor_wl.map[slot].at &&
                    at - compositor_wl.map[slot].at < compositor_wl.map[slot].size)
                {
                        compositor_wl.map_hint = slot;
                        return compositor_wl.map[slot].bad;
                }
        return 0;
}

/*
        Whether the person asked for the program a client is: the client's process or one of its forebears (up to 64 of them) has
        the ask recorded in WINDOWS_ASKED_DIR, by the process root started it from (the kit's launcher, a terminal's shell, the
        runner's launch of a person's rule: windows_asked_record), with the start it has now. Where it was the client's own
        environment (MOONWATER_ASKED=1), any program could set that for itself and take the keys at its first window. The
        directory is read only while it is root's (or the compositor's) and no one else's to write (0700): a record another made
        is no ask. A record whose process has gone and whose number another has now is taken away. The first window of a
        connection only.
*/
static int compositor_wl_asked(long client)
{
        unsigned char info[256];
        unsigned int pid = compositor_wl.pid[client] > 0 ? (unsigned int)compositor_wl.pid[client] : 0, mode, owner;
        unsigned int uid = (unsigned int)compositor_call(syscall(getuid), 0, 0, 0, 0, 0, 0);

        // struct statx: stx_uid at 20, stx_mode at 28; the directory itself, not through a link.
        memory_zero(info, sizeof info);
        if (compositor_call(syscall(statx), (positive)AT_FDCWD, (positive)windows_asked_dir, 0x100, 0x3, (positive)info, 0) < 0)
                return 0;
        owner = *(unsigned int *)(info + 20), mode = (unsigned int)info[28] | (unsigned int)info[29] << 8;
        if ((mode & 0170000) != 0040000 || (mode & 077) || (owner != 0 && owner != uid))
                return 0;
        for (unsigned int depth = 0; pid > 1 && depth < 64; depth++)
        {
                char path[64], text[24];
                unsigned int parent;
                unsigned long start = windows_proc_stat(pid, &parent), recorded = 0;
                long handle, got;

                if (!start)
                        return 0;
                windows_asked_path(path, pid);
                handle = (long)system_open_at(AT_FDCWD, (string_address)path, FILE_READ | O_CLOEXEC | O_NOFOLLOW);
                got = handle >= 0 ? (long)system_read_once((int)handle, text, sizeof text - 1) : 0;
                if (handle >= 0)
                        system_close((int)handle);
                for (long k = 0; k < got && text[k] >= '0' && text[k] <= '9'; k++)
                        recorded = recorded * 10 + (unsigned long)(text[k] - '0');
                if (got > 0 && recorded == start)
                        return 1;
                if (got > 0)
                        compositor_call(syscall(unlinkat), (positive)AT_FDCWD, (positive)path, 0, 0, 0, 0); // another process's, gone
                pid = parent;
        }
        return 0;
}

/* A table row for a client's window, claimed via Canvas's compositor_row_open_external. -1 when none is free. */
static int compositor_wl_row_open(void *context, long client, long kind, int parent, long x, long y)
{
        struct compositor_row_spec spec = {0};
        int row;

        (void)context;
        if (client < 0 || client >= compositor_wl.slots)
                return -1;
        spec.pid = compositor_wl.pid[client] > 0 ? (unsigned int)compositor_wl.pid[client] : 1u;
        spec.kind = kind == WAYLAND_KIND_POPUP ? COMPOSITOR_ROW_POPUP
                    : kind == WAYLAND_KIND_PLACED ? COMPOSITOR_ROW_PLACED
                                                  : COMPOSITOR_ROW_TOPLEVEL;
        spec.glass = WINDOW_NO_CLOSE | WINDOW_NO_GRAB;
        if ((kind == WAYLAND_KIND_TOPLEVEL || kind == WAYLAND_KIND_PLACED) && !compositor_wl.client[client].asked)
        {
                compositor_wl.client[client].asked = 1;
                spec.asked = (unsigned int)compositor_wl_asked(client);
        }
        spec.parent = parent;
        spec.x = (int)x, spec.y = (int)y;
        row = compositor_row_open_external(&spec);
        if (row < 0)
                return -1;
        compositor_wl.laying[row].open = 0;
        compositor_wl.laying[row].last = 0;
        compositor_wl.cursor_asked[row] = 0;
        return row;
}

static void compositor_wl_gathers_free(int row);

static void compositor_wl_row_close(void *context, int row)
{
        (void)context;
        // The cursor the client asked for is its own: a program that takes the row next has none asked (the kit's shape shows).
        if (row >= 0 && row < COMPOSITOR_ROWS)
        {
                compositor_wl.laying[row].open = 0;
                compositor_wl.laying[row].last = 0;
                compositor_wl.cursor_asked[row] = 0;
                compositor_wl.row_blend[row] = 0;
                compositor_wl.wc_reads[row] = 0;
                compositor_wl.wc_copy[row] = 0;
                memory_zero(&compositor_wl.row_opaque[row], sizeof compositor_wl.row_opaque[row]);
                compositor_wl_unlend(row);
                compositor_wl_straight_drop(row);
                compositor_wl_source_drop(row);
                compositor_wl_gathers_free(row);
                canvas_ext_row_clear(row);
        }
        compositor_row_release(row);
}

// Whether a rectangle of a window (x1, y1 to x2, y2 in its pixels) is opaque: the whole window is, or its client's opaque region holds it.
static int wl_ext_row_opaque(int row, int x1, int y1, int x2, int y2)
{
        const struct compositor_wl_opaque *o;

        if (row < 0 || row >= COMPOSITOR_ROWS)
                return 0;
        if (!compositor_wl.row_blend[row])
                return 1;
        o = &compositor_wl.row_opaque[row];
        return o->x2 > o->x1 && x1 >= o->x1 && y1 >= o->y1 && x2 <= o->x2 && y2 <= o->y2;
}

/*
        A client's buffer, the rows that changed, into the row's picture. A window's picture is made of one or more of these
        (the window's own buffer and the subsurfaces over it), so they are laid one after another into the buffer of the table
        the compositor will show, and the row is taken once, when the last of them is down, with what they touched as its damage.

        The buffer is written in place, because it is this process that reads it, and the other buffer when the card is reading
        this one (a window that is scanned out by itself), seeded from the one shown unless the first picture covers it. Whatever
        goes into the other buffer is a window the compositor has to compose again from the start.
*/
// One rectangle a commit changed of a row's window (x1, y1 to x2, y2 in its pixels), kept for the take (compositor_wl.parts).
static void compositor_wl_part(int row, long x1, long y1, long x2, long y2)
{
        int count = compositor_wl.parts[row].count;

        if (count < 0 || x2 <= x1 || y2 <= y1)
                return;
        if (count >= WAYLAND_DAMAGE)
        {
                compositor_wl.parts[row].count = -1;
                return;
        }
        compositor_wl.parts[row].rect[count][0] = (int)x1;
        compositor_wl.parts[row].rect[count][1] = (int)y1;
        compositor_wl.parts[row].rect[count][2] = (int)(x2 - x1);
        compositor_wl.parts[row].rect[count][3] = (int)(y2 - y1);
        compositor_wl.parts[row].count = count + 1;
}

/*
        The rectangles the commit being taken changed of a row's window, and how many: what compositor_take damages where the table
        says only their box. A commit of two small changes in far corners of a window was one box of nearly all of it, composed
        whole at every present (2,048 changed pixels, 347 to 555 thousand composed). 0 for a commit that says no more than its box.
*/
static int wl_ext_row_parts(int row, const int (**rects)[4])
{
        if (row < 0 || row >= COMPOSITOR_ROWS || compositor_wl.parts[row].count < 2)
                return 0;
        *rects = compositor_wl.parts[row].rect;
        return compositor_wl.parts[row].count;
}

// The row's picture taken by the compositor, with the commit's rectangles, which are that one take's.
static void compositor_wl_take(int row)
{
        compositor_take(row);
        compositor_wl.parts[row].count = 0;
}

static void compositor_wl_laid(int row, long x1, long y1, long x2, long y2)
{
        compositor_wl_part(row, x1, y1, x2, y2);
        if (compositor_wl.laying[row].touched)
        {
                x1 = x1 < compositor_wl.laying[row].x1 ? x1 : compositor_wl.laying[row].x1;
                y1 = y1 < compositor_wl.laying[row].y1 ? y1 : compositor_wl.laying[row].y1;
                x2 = x2 > compositor_wl.laying[row].x2 ? x2 : compositor_wl.laying[row].x2;
                y2 = y2 > compositor_wl.laying[row].y2 ? y2 : compositor_wl.laying[row].y2;
        }
        compositor_wl.laying[row].touched = 1;
        compositor_wl.laying[row].x1 = (int)x1;
        compositor_wl.laying[row].y1 = (int)y1;
        compositor_wl.laying[row].x2 = (int)x2;
        compositor_wl.laying[row].y2 = (int)y2;
}

static int compositor_wl_dmabuf_ready(long client, int fd);
static int compositor_wl_sync_start(long client, int fd, int ready);

// Whether a row currently holds a lent client buffer.
static inline int wl_row_lent(int row)
{
        return row >= 0 && row < COMPOSITOR_ROWS && compositor_wl.lent && compositor_wl.lent[row].pixels != 0;
}

static int compositor_straight(int row)
{
        return row >= 0 && row < COMPOSITOR_ROWS && compositor_wl.straight && compositor_wl.straight[row].live;
}

/*
        The card's hang blamed on the client whose buffer it was reading: import is the import the fault was in (gpu_fault_import), and the client is the
        one whose row shows it (its lent picture, the dma-buf its damage is copied from, or one of its layers). That client is barred from the card for as
        long as it is connected (compositor_wl_card_barred: its buffers are not imported, so the processor reads what it can map and a tiled picture is not
        shown), and the card goes on for every other window (compositor_gpu_job_failed). False when no row's client had it: the hang is the card's own.
*/
static b32 compositor_wl_card_blame(positive import)
{
        long client = -1;

        if (!import || !compositor_wl.started || !compositor_wl.lent || !compositor_wl.source)
                return false;
        for (int row = 0; row < (int)compositor_rows_in_use && client < 0; row++)
        {
                b32 had = (compositor_lent(row) && compositor_wl.lent[row].import == import) || compositor_wl.source[row].va == import;

                for (unsigned int k = 0; !had && compositor_wl_layers.count && k < compositor_wl_layers.count[row]; k++)
                        had = compositor_wl_layers.layer[row][k].import == import;
                if (had)
                        client = wayland_row_owner(row);
        }
        if (client < 0 || client >= compositor_wl.slots)
                return false;
        compositor_wl.barred[client] = 1;
        string_format(log, "compositor: the card hung reading a buffer of the Wayland client of pid %p: its windows are the processor's while it is connected\n",
                      (positive)compositor_wl.pid[client]),
            log_flush();
        return true;
}

// Whether the row's client is barred from the card (compositor_wl_card_blame): its buffers are not imported.
static b32 compositor_wl_card_barred(int row)
{
        long client = row >= 0 ? wayland_row_owner(row) : -1;

        return client >= 0 && client < compositor_wl.slots && compositor_wl.barred[client];
}

// A copy of the row's client picture counted for its window (struct windows's client_row_copies), with the counts of all.
static void compositor_wl_copied(int row, unsigned int kind)
{
        if (compositor.table && row >= 0 && row < COMPOSITOR_ROWS)
                compositor.table->client_row_copies[row][kind & 3]++;
}

// Whether a fourcc is ARGB's or XRGB's bytes with red and blue the other way round.
static int compositor_wl_fourcc_swapped(unsigned int fourcc)
{
        return fourcc == WAYLAND_FOURCC_ABGR8888 || fourcc == WAYLAND_FOURCC_XBGR8888;
}

// Live external-row flags and lent-buffer query callbacks for Canvas's canvas_ext_ops vtable.
static unsigned int wl_ext_row_flags(int row, unsigned int mask)
{
        unsigned int f = 0;

        if (row < 0 || row >= COMPOSITOR_ROWS)
                return 0;
        if ((mask & (CANVAS_EXT_LENT | CANVAS_EXT_SWAP)) && wl_row_lent(row))
        {
                f |= CANVAS_EXT_LENT;
                if ((mask & CANVAS_EXT_SWAP) && compositor_wl_fourcc_swapped(compositor_wl.lent[row].fourcc))
                        f |= CANVAS_EXT_SWAP;
        }
        if ((mask & CANVAS_EXT_STRAIGHT) && compositor_straight(row))
                f |= CANVAS_EXT_STRAIGHT;
        if ((mask & CANVAS_EXT_BLEND) && compositor_wl.row_blend && compositor_wl.row_blend[row])
                f |= CANVAS_EXT_BLEND;
        if ((mask & CANVAS_EXT_ASYNC) && compositor_wl.started && wayland.client && wayland_row_hint(row, WAYLAND_HINT_ASYNC))
                f |= CANVAS_EXT_ASYNC;
        if ((mask & CANVAS_EXT_UNSCANNABLE) && compositor_wl.started && wayland_row_owner(row) >= 0 && !compositor_straight(row) &&
            (!wl_row_lent(row) || compositor_wl.lent[row].fd < 0))
                f |= CANVAS_EXT_UNSCANNABLE;
        if ((mask & CANVAS_EXT_XOVERRIDE) && compositor_wl.started && wayland.client && wayland_row_xoverride(row))
                f |= CANVAS_EXT_XOVERRIDE;
        return f & mask;
}

static const unsigned int *wl_ext_lent_info(int row, unsigned long *pitch, positive *va)
{
        if (!wl_row_lent(row))
                return 0;
        if (pitch)
                *pitch = compositor_wl.lent[row].pitch;
        if (va)
                *va = canvas_ext_import_live(compositor_wl.lent[row].va, compositor_wl.lent[row].epoch);
        return compositor_wl.lent[row].pixels;
}

// Keep Canvas's native per-row state table synced with Wayland's lent/straight attachment state.
static void wl_ext_row_sync(int row)
{
        struct canvas_ext_row_state *s = canvas_ext_row_at(row);

        if (!s)
                return;
        s->flags = wl_ext_row_flags(row, ~0u);
        if (wl_row_lent(row))
        {
                s->pixels = compositor_wl.lent[row].pixels;
                s->pitch = compositor_wl.lent[row].pitch;
                s->gpu_va = compositor_wl.lent[row].va;
                s->gpu_epoch = compositor_wl.lent[row].epoch;
                s->scan_fb = compositor_wl.lent[row].scan_fb;
                s->scan_x = compositor_wl.lent[row].scan_x;
                s->scan_y = compositor_wl.lent[row].scan_y;
        }
        else
        {
                s->pixels = 0, s->pitch = 0, s->gpu_va = 0, s->gpu_epoch = 0;
                s->scan_fb = compositor_straight(row) ? compositor_wl.straight[row].scan_fb : 0;
                s->scan_x = s->scan_y = 0;
        }
}

// Delegate deferred-RMFB grave pool to Canvas's canvas_fb_graves (scanout.c); aliases preserve check compatibility.
#define COMPOSITOR_WL_GRAVES CANVAS_FB_GRAVES
#define compositor_wl_graves canvas_fb_graves.fb
#define compositor_wl_grave_count canvas_fb_graves.count
#define compositor_wl_graves_forced canvas_fb_graves.forced
#define compositor_wl_graves_sweep() canvas_scan_fb_reap()
#define compositor_wl_drop_fb(fb) canvas_scan_fb_grave(fb)

static void compositor_wl_lent_drop_fb(int row)
{
        unsigned int fb = compositor_wl.lent[row].scan_fb;

        compositor_wl.lent[row].scan_fb = 0;
        compositor_wl.lent[row].scan_x = compositor_wl.lent[row].scan_y = 0;
        canvas_scan_fb_grave(fb);
}

static void compositor_wl_scan_refuse(int row, unsigned int fourcc, unsigned int pitch, unsigned int offset, int error)
{
        compositor_wl.scan_refused.fourcc = fourcc;
        compositor_wl.scan_refused.pitch = pitch;
        compositor_wl.scan_refused.offset = offset;
        if (compositor_wl.lent[row].pixels)
        {
                compositor_wl.scan_refused.width = (unsigned int)compositor_wl.lent[row].width;
                compositor_wl.scan_refused.height = (unsigned int)compositor_wl.lent[row].height;
                compositor_wl.scan_refused.modifier = compositor_wl.lent[row].modifier;
                compositor_wl.lent[row].scan_failed = 1;
        }
        else
        {
                compositor_wl.scan_refused.width = (unsigned int)compositor_wl.straight[row].picture.width;
                compositor_wl.scan_refused.height = (unsigned int)compositor_wl.straight[row].picture.height;
                compositor_wl.scan_refused.modifier = compositor_wl.straight[row].picture.modifier;
                compositor_wl.straight[row].scan_failed = 1;
        }
        compositor_wl.scan_refused.error = error;
        compositor_wl.scan_refused.count++;
        if (compositor.table)
        {
                compositor.table->client_scan_fourcc = fourcc;
                compositor.table->client_scan_modifier = compositor_wl.scan_refused.modifier;
                compositor.table->client_scan_pitch = pitch;
                compositor.table->client_scan_offset = offset;
                compositor.table->client_scan_width = compositor_wl.scan_refused.width;
                compositor.table->client_scan_height = compositor_wl.scan_refused.height;
                compositor.table->client_scan_errno = error;
                compositor.table->client_scan_refused = compositor_wl.scan_refused.count;
        }
}

// Unified scanout FB creation via Canvas's canvas_scan_fb_make (scanout.c). opaque < 0 uses !row_blend[row].
static unsigned int wl_ext_scan_fb(int row, int opaque)
{
        int is_lent;
        struct canvas_scan_fb *scan;
        struct canvas_scan_fb_spec spec;
        struct canvas_scan_fb_result res;
        unsigned int fourcc;

        if (row < 0 || row >= COMPOSITOR_ROWS || compositor.card < 0 || compositor_wl_layers.count[row])
                return 0;
        is_lent = wl_row_lent(row);
        if (!is_lent && !compositor_straight(row))
                return 0;
        if (opaque < 0)
                opaque = !compositor_wl.row_blend[row];
        scan = is_lent ? &compositor_wl.lent[row].scan : &compositor_wl.straight[row].scan;
        if (scan->scan_failed || (is_lent && compositor_wl.lent[row].fd < 0))
                return 0;
        fourcc = is_lent ? compositor_wl.lent[row].fourcc : compositor_wl.straight[row].picture.fourcc;
        if (!fourcc)
                fourcc = COMPOSITOR_XRGB8888;
        if (opaque && (fourcc == 0x34325241u || fourcc == 0x34324241u ||
                       (is_lent && (fourcc == 0x30335241u || fourcc == 0x30334241u))))
                fourcc = (fourcc & ~0xffu) | 'X';
        if (scan->scan_fb && scan->scan_fourcc != fourcc)
        {
                if (is_lent)
                        compositor_wl_lent_drop_fb(row);
                else
                        canvas_scan_fb_grave(scan->scan_fb), scan->scan_fb = 0;
        }
        if (scan->scan_fb)
                return !is_lent || opaque || (!scan->scan_x && !scan->scan_y) ? scan->scan_fb : 0;
        memory_zero(&spec, sizeof(spec));
        spec.opaque = opaque;
        if (is_lent)
        {
                spec.linear = 1, spec.planes = 1;
                spec.width = (unsigned int)compositor_wl.lent[row].width;
                spec.height = (unsigned int)compositor_wl.lent[row].height;
                spec.fourcc = compositor_wl.lent[row].fourcc;
                spec.base_offset = compositor_wl.lent[row].offset;
                spec.modifier = compositor_wl.lent[row].modifier;
                spec.fds[0] = compositor_wl.lent[row].fd;
                spec.pitches[0] = (unsigned int)(compositor_wl.lent[row].pitch * 4);
                spec.offsets[0] = (unsigned int)((const unsigned char *)compositor_wl.lent[row].pixels - compositor_wl.lent[row].map);
        }
        else
        {
                const struct wayland_picture *picture = &compositor_wl.straight[row].picture;
                unsigned int planes = picture->plane_count < 1 ? 1u : picture->plane_count > 4 ? 4u : (unsigned int)picture->plane_count;

                spec.linear = 0, spec.planes = planes;
                spec.width = compositor.table ? compositor.table->width[row] : (unsigned int)picture->width;
                spec.height = compositor.table ? compositor.table->height[row] : (unsigned int)picture->height;
                spec.fourcc = picture->fourcc;
                spec.modifier = picture->modifier;
                for (unsigned int at = 0; at < planes; at++)
                {
                        spec.fds[at] = at && picture->plane_fd[at] >= 0 ? picture->plane_fd[at] : picture->sync_fd;
                        spec.pitches[at] = (unsigned int)picture->plane_stride[at];
                        spec.offsets[at] = (unsigned int)picture->plane_offset[at];
                }
        }
        if (!canvas_scan_fb_make(&spec, &res))
        {
                scan->scan_x = scan->scan_y = 0;
                compositor_wl_scan_refuse(row, res.fourcc, res.pitch, res.offset, res.error);
                return 0;
        }
        scan->scan_fb = res.fb_id;
        scan->scan_fourcc = res.fourcc;
        scan->scan_x = res.scan_x;
        scan->scan_y = res.scan_y;
        wl_ext_row_sync(row);
        return res.fb_id;
}

static void wl_ext_scan_origin(int row, int *x, int *y)
{
        *x = *y = 0;
        if (row < 0 || row >= COMPOSITOR_ROWS || !wl_row_lent(row) || !compositor_wl.lent[row].scan_fb)
                return;
        *x = (int)compositor_wl.lent[row].scan_x, *y = (int)compositor_wl.lent[row].scan_y;
}

static void wl_ext_scan_fail(int row)
{
        if (row < 0 || row >= COMPOSITOR_ROWS)
                return;
        if (!wl_row_lent(row))
        {
                compositor_wl.straight[row].scan_failed = 1;
                canvas_scan_fb_grave(compositor_wl.straight[row].scan_fb);
                compositor_wl.straight[row].scan_fb = 0;
                return;
        }
        compositor_wl.lent[row].scan_failed = 1;
        compositor_wl_lent_drop_fb(row);
}

/* ---- gathered subsurfaces and GPU/CPU layer composition ---- */

static int compositor_wl_card_composes(void);

static positive wl_gpu_va_written(const void *ptr, positive bytes)
{
        positive va = gpu_va_written(ptr, bytes);
        if (!va && gpu_map_written(ptr, bytes))
                va = gpu_va_written(ptr, bytes);
        return va;
}

#ifdef MOONWATER_GPU_C
static positive wl_gpu_va_read(const void *ptr, positive bytes)
{
        positive va = gpu_va_bytes(ptr, bytes);
        if (!va && gpu_map(ptr, bytes))
                va = gpu_va_bytes(ptr, bytes);
        return va;
}
#endif

static inline INLINE PURE unsigned int compositor_wl_gather_column(const struct compositor_wl_layer *l, long x)
{
        long reach = l->transform & 1 ? l->source_h : l->source_w;
        long t = (l->sx + ((l->ox + x) * 2 + 1) * l->sw / (2 * l->tw)) >> 8;
        int flip_x = 0x36 >> (l->transform & 7) & 1, flip_y = (0x6c >> (l->transform & 7) & 1) ^ (l->y_invert != 0);
        t = t < 0 ? 0 : t >= reach ? reach - 1 : t;
        return (l->transform & 1) ? (unsigned int)((unsigned long)(flip_y ? l->source_h - 1 - t : t) * l->pitch)
                                  : (unsigned int)(flip_x ? l->source_w - 1 - t : t);
}

static inline INLINE PURE unsigned int compositor_wl_gather_line(const struct compositor_wl_layer *l, long y)
{
        long reach = l->transform & 1 ? l->source_w : l->source_h;
        long t = (l->sy + ((l->oy + y) * 2 + 1) * l->sh / (2 * l->th)) >> 8;
        int flip_x = 0x36 >> (l->transform & 7) & 1, flip_y = (0x6c >> (l->transform & 7) & 1) ^ (l->y_invert != 0);
        t = t < 0 ? 0 : t >= reach ? reach - 1 : t;
        return (l->transform & 1) ? (unsigned int)(flip_x ? l->source_w - 1 - t : t)
                                  : (unsigned int)((unsigned long)(flip_y ? l->source_h - 1 - t : t) * l->pitch);
}

static positive compositor_wl_layer_bytes(const struct compositor_wl_layer *l)
{
        if (l->nv12)
                return (positive)l->extent;
        return l->gather ? (positive)(l->source_h - 1) * l->pitch * 4 + (positive)l->source_w * 4
                         : (positive)(l->height - 1) * l->pitch * 4 + (positive)l->width * 4;
}

static int compositor_wl_gather_same(const struct compositor_wl_layer *a, const struct compositor_wl_layer *b)
{
        return a->gather && b->gather && a->pixels == b->pixels && a->pitch == b->pitch && a->map == b->map && a->fd == b->fd && a->dev == b->dev &&
               a->ino == b->ino && a->width == b->width && a->height == b->height && a->source_w == b->source_w && a->source_h == b->source_h &&
               a->transform == b->transform && a->y_invert == b->y_invert && a->sx == b->sx && a->sy == b->sy && a->sw == b->sw && a->sh == b->sh &&
               a->tw == b->tw && a->th == b->th && a->ox == b->ox && a->oy == b->oy;
}

static void compositor_wl_gathered_free(struct compositor_wl_gathered *g)
{
        if (g->memory)
        {
                gpu_forget(g->memory, g->bytes);
#ifdef MOONWATER_GPU_C
                if (g->fd)
                        gpu_unregister_memfd(g->memory);
#endif
                compositor_call(syscall(munmap), (positive)g->memory, g->bytes, 0, 0, 0, 0);
        }
        if (g->fd)
                system_close(g->fd - 1);
        memory_zero(g, sizeof *g);
}

static void compositor_wl_gathers_free(int row)
{
        if (row >= 0 && row < COMPOSITOR_ROWS)
                for (unsigned int at = 0; at < WAYLAND_LAYERS; at++)
                        compositor_wl_gathered_free(&compositor_wl_layers.gathered[row][at]);
}

static unsigned int *compositor_wl_gathered_room(struct compositor_wl_gathered *g, positive bytes)
{
        positive grown = (bytes + (1ull << 20) - 1) & ~((1ull << 20) - 1);
        unsigned int id = g->id;
        long memfd, mapped;

        if (g->memory && g->bytes >= bytes)
                return g->memory;
        compositor_wl_gathered_free(g);
        g->id = id;
        if ((memfd = compositor_call(syscall(memfd_create), (positive)"moonwater-layer", 1 | 2, 0, 0, 0, 0)) < 0)
                return 0;
        if (system_truncate_handle((int)memfd, grown) != 0 ||
            compositor_call(syscall(fcntl), (positive)memfd, WINDOWS_F_ADD_SEALS, 2, 0, 0, 0) != 0 ||
            ((mapped = compositor_call(syscall(mmap), 0, grown, 3, 1, (positive)memfd, 0)) < 0 && mapped > -4096))
        {
                system_close((int)memfd);
                return 0;
        }
        g->memory = (unsigned int *)mapped, g->bytes = grown, g->fd = (int)memfd + 1;
#ifdef MOONWATER_GPU_C
        gpu_register_memfd((int)memfd, g->memory, grown, 0);
#endif
        return g->memory;
}

HOT static void compositor_wl_gather_run(const struct compositor_wl_layer *restrict l, long y, long x, unsigned long n, unsigned int *restrict run)
{
        const unsigned int *line = l->pixels + compositor_wl_gather_line(l, y);
        for (unsigned long at = 0; at < n; at++)
                run[at] = line[compositor_wl_gather_column(l, x + (long)at)];
}

HOT static void compositor_wl_nv12_run(const struct compositor_wl_layer *restrict l, const unsigned char *restrict base, long y, long x, unsigned long n, unsigned int *restrict run)
{
        unsigned long line = compositor_wl_gather_line(l, y);
        const unsigned char *luma = base + l->luma + line * l->luma_pitch, *chroma = base + l->chroma + (line >> 1) * l->chroma_pitch;
        if (!l->transform && !l->y_invert && l->sx == 0 && l->sw == l->source_w * 256 && l->tw == l->source_w &&
            l->ox + x >= 0 && ((l->ox + x) & 1L) == 0 && l->ox + x + (long)n <= l->source_w)
        {
                unsigned long col0 = (unsigned long)(l->ox + x);
                memory_nv12_to_xrgb_row(run, luma + col0, chroma + col0, (positive)n);
                return;
        }
        for (unsigned long at = 0; at < n; at++)
        {
                unsigned long column = compositor_wl_gather_column(l, x + (long)at), pair = column & ~1ul;
                run[at] = canvas_ext_yuv_rgb(luma[column], chroma[pair], chroma[pair + 1]);
        }
}

static const unsigned char *compositor_wl_nv12_map(const struct compositor_wl_layer *l)
{
        long got = compositor_call(syscall(mmap), 0, (positive)l->extent, 1, 1, (positive)l->fd, 0);
        if (got < 0 && got > -4096)
                return 0;
        canvas_dmabuf_sync_read(l->fd, 0);
        return (const unsigned char *)got;
}

static void compositor_wl_nv12_unmap(const struct compositor_wl_layer *l, const unsigned char *base)
{
        canvas_dmabuf_sync_read(l->fd, 1);
        compositor_call(syscall(munmap), (positive)base, (positive)l->extent, 0, 0, 0, 0);
}

static void wl_ext_gathers_fill(void)
{
        if (!compositor_wl_layers.total)
                return;
        for (int row = 0; row < (int)compositor_rows_in_use; row++)
                for (unsigned int k = 0; k < compositor_wl_layers.count[row]; k++)
                {
                        struct compositor_wl_layer *l = &compositor_wl_layers.layer[row][k];
                        struct compositor_wl_gathered *g;
                        positive pixels, bytes;
                        unsigned int *cols, *lines;

                        if (!l->gather || l->at >= WAYLAND_LAYERS || (g = &compositor_wl_layers.gathered[row][l->at])->filled || g->id != l->id)
                                continue;
                        pixels = (positive)l->width * (positive)l->height;
                        bytes = 4 * (pixels + (positive)l->width + (positive)l->height);
                        if (!compositor_wl_gathered_room(g, bytes))
                                continue;
                        cols = g->memory + pixels, lines = cols + l->width;
                        for (long x = 0; x < l->width; x++)
                                cols[x] = compositor_wl_gather_column(l, x);
                        for (long y = 0; y < l->height; y++)
                                lines[y] = compositor_wl_gather_line(l, y);
                        g->va = 0;
#ifdef MOONWATER_GPU_C
                        if (compositor_wl_card_composes() && gpu_ready() && gpu_idle())
                        {
                                positive dst = wl_gpu_va_written(g->memory, g->bytes);
                                positive src = l->fd >= 0 ? canvas_ext_import_live(l->va, l->epoch) : wl_gpu_va_read(l->pixels, compositor_wl_layer_bytes(l));
                                if (dst && src)
                                {
                                        b32 made;
                                        g->va = dst, g->epoch = gpu_epoch;
                                        if (l->nv12)
                                                made = l->luma_pitch < 65536 && l->chroma_pitch < 65536 &&
                                                       gpu_nv12(src + l->luma, (unsigned int)l->luma_pitch, src + l->chroma, (unsigned int)l->chroma_pitch, dst,
                                                                (unsigned int)l->width, dst + 4 * pixels, (unsigned int)l->width, (unsigned int)l->height);
                                        else
                                                made = gpu_blit(src, 1, dst, (unsigned int)l->width, dst + 4 * pixels, 7, (unsigned int)l->width, (unsigned int)l->height);
                                        if (made || (gpu_end == GPU_END_LATE && gpu_finish()))
                                        {
                                                g->filled = 1;
                                                if (compositor.table)
                                                        compositor.table->client_layers_gathered[0]++;
                                                continue;
                                        }
                                        if (gpu_end != GPU_END_NONE)
                                                compositor_gpu_job_failed();
                                        if (!gpu_idle())
                                                continue;
                                }
                        }
#endif
                        if (l->nv12)
                        {
                                const unsigned char *base = compositor_wl_nv12_map(l);
                                if (!base)
                                        continue;
                                for (long y = 0; y < l->height; y++)
                                        compositor_wl_nv12_run(l, base, y, 0, (unsigned long)l->width, g->memory + (unsigned long)y * (unsigned long)l->width);
                                compositor_wl_nv12_unmap(l, base);
                        }
                        else
                        {
                                canvas_dmabuf_sync_read(l->fd, 0);
                                for (long y = 0; y < l->height; y++)
                                {
                                        const unsigned int *restrict line = l->pixels + lines[y];
                                        unsigned int *restrict to = g->memory + (unsigned long)y * (unsigned long)l->width;
                                        if (l->fd < 0 && y + 1 < l->height)
                                                prefetch_read(l->pixels + lines[y + 1]), prefetch_write(to + l->width);
                                        for (long x = 0; x < l->width; x++)
                                                to[x] = line[cols[x]];
                                }
                                canvas_dmabuf_sync_read(l->fd, 1);
                                if (compositor_wl_cut(l->pixels))
                                        continue;
                        }
                        g->filled = 1;
                        if (compositor.table)
                                compositor.table->client_layers_gathered[1]++;
                }
}

static void compositor_wl_gathers_take(int row, struct compositor_wl_layer *made, unsigned int kept, const struct wayland_picture *const *from,
                                       const struct compositor_wl_layer *old, unsigned int olds, long root)
{
        struct compositor_wl_gathered *g = compositor_wl_layers.gathered[row];
        unsigned char used[WAYLAND_LAYERS] = {0};

        for (unsigned int at = 0; at < kept; at++)
        {
                made[at].at = WAYLAND_LAYERS;
                for (unsigned int s = 0; made[at].gather && s < WAYLAND_LAYERS && made[at].at == WAYLAND_LAYERS; s++)
                        if (g[s].id == made[at].id && !used[s])
                                made[at].at = s, used[s] = 1;
        }
        for (unsigned int at = 0; at < kept; at++)
        {
                const struct compositor_wl_layer *was = 0;
                if (!made[at].gather)
                        continue;
                for (unsigned int s = 0; s < WAYLAND_LAYERS && made[at].at == WAYLAND_LAYERS; s++)
                        if (!used[s])
                                made[at].at = s, used[s] = 1, g[s].id = made[at].id, g[s].filled = 0;
                for (unsigned int k = 0; k < olds && !was; k++)
                        if (old[k].id == made[at].id)
                                was = &old[k];
                if (root || from[at]->changed || !was || !compositor_wl_gather_same(was, &made[at]))
                        g[made[at].at].filled = 0;
        }
        for (unsigned int s = 0; s < WAYLAND_LAYERS; s++)
                if (!used[s])
                        g[s].id = 0, g[s].filled = 0;
}

static int compositor_wl_layers_refused(unsigned int why)
{
        if (compositor.table && why < sizeof compositor.table->client_layers_refused / sizeof compositor.table->client_layers_refused[0])
                compositor.table->client_layers_refused[why]++;
        return 0;
}

static void compositor_wl_layers_drop(int row)
{
        if (row < 0 || row >= COMPOSITOR_ROWS)
                return;
        for (unsigned int at = 0; at < compositor_wl_layers.count[row]; at++)
                if (canvas_ext_import_live(compositor_wl_layers.layer[row][at].import, compositor_wl_layers.layer[row][at].epoch))
                        gpu_release_import(compositor_wl_layers.layer[row][at].import);
        compositor_wl_layers.total -= compositor_wl_layers.count[row];
        compositor_wl_layers.count[row] = 0;
        if (compositor_wl_layers.pending_row == row)
                compositor_wl_layers.pending_row = -1;
}

static void compositor_wl_layers_put_list(const struct compositor_wl_layer *list, unsigned int count, unsigned int *to, unsigned long pitch)
{
        if (compositor.table)
                compositor.table->client_layers_laid += count;
        for (unsigned int at = 0; at < count; at++)
        {
                const struct compositor_wl_layer *l = &list[at];
                unsigned int *into = to + (unsigned long)l->y * pitch + (unsigned long)l->x;

                if (l->nv12)
                {
                        const unsigned char *base = compositor_wl_nv12_map(l);
                        if (!base)
                                continue;
                        if (!l->transform && !l->y_invert && l->sx == 0 && l->sw == l->source_w * 256 && l->tw == l->source_w &&
                            l->ox >= 0 && (l->ox & 1L) == 0 && l->ox + l->width <= l->source_w)
                        {
                                for (long y = 0; y < l->height; y++)
                                {
                                        unsigned long line = compositor_wl_gather_line(l, y);
                                        const unsigned char *luma = base + l->luma + line * l->luma_pitch + (unsigned long)l->ox;
                                        const unsigned char *chroma = base + l->chroma + (line >> 1) * l->chroma_pitch + (unsigned long)l->ox;
                                        memory_nv12_to_xrgb_row(into + (unsigned long)y * pitch, luma, chroma, (positive)l->width);
                                }
                        }
                        else
                        {
                                for (long y = 0; y < l->height; y++)
                                        for (long x = 0; x < l->width; x += 1024)
                                        {
                                                unsigned long n = (unsigned long)(l->width - x < 1024 ? l->width - x : 1024);
                                                compositor_wl_nv12_run(l, base, y, x, n, into + (unsigned long)y * pitch + (unsigned long)x);
                                        }
                        }
                        compositor_wl_nv12_unmap(l, base);
                        continue;
                }
                canvas_dmabuf_sync_read(l->fd, 0);
                if (l->swap || l->gather)
                {
                        unsigned int run[1024], alpha = l->fourcc == WAYLAND_FOURCC_XBGR8888 ? 0xff000000u : 0;
                        for (long y = 0; y < l->height; y++)
                                for (long x = 0; x < l->width; x += 1024)
                                {
                                        unsigned long n = (unsigned long)(l->width - x < 1024 ? l->width - x : 1024);
                                        unsigned int *line = into + (unsigned long)y * pitch + (unsigned long)x;
                                        if (l->gather)
                                                compositor_wl_gather_run(l, y, x, n, run);
                                        else
                                                memory_copy_rect_stream_u32(run, n, l->pixels + (unsigned long)y * l->pitch + (unsigned long)x, l->pitch, n, 1);
                                        if (l->swap)
                                                memory_swizzle_rb_u32(run, run, (positive)n, alpha);
                                        if (l->blend)
                                                memory_over_u32_rect(line, pitch, run, n, n, 1);
                                        else
                                                memory_copy_rect_u32(line, pitch, run, n, n, 1);
                                }
                }
                else if (l->blend)
                        memory_over_u32_rect(into, pitch, l->pixels, l->pitch, (unsigned long)l->width, (unsigned long)l->height);
                else if (l->fd >= 0)
                        memory_copy_rect_stream_u32(into, pitch, l->pixels, l->pitch, (unsigned long)l->width, (unsigned long)l->height);
                else
                        memory_copy_rect_u32(into, pitch, l->pixels, l->pitch, (unsigned long)l->width, (unsigned long)l->height);
                canvas_dmabuf_sync_read(l->fd, 1);
        }
}

static void compositor_wl_layers_put(int row, unsigned int *to, unsigned long pitch)
{
        if (compositor_wl_layers.count[row])
                compositor_wl_copied(row, WINDOWS_COPY_WHOLE);
        compositor_wl_layers_put_list(compositor_wl_layers.layer[row], compositor_wl_layers.count[row], to, pitch);
}

static int compositor_wl_card_lay(int row, unsigned int *to, int root)
{
#ifdef MOONWATER_GPU_C
        unsigned long pitch = (unsigned long)compositor_stride();
        unsigned int *op = (unsigned int *)((unsigned char *)gpu.at + COMPOSITOR_GPU_OPS_AT), n = 0, w, h, w4;
        positive to_va, src;

        if (!compositor_wl_card_composes() || !gpu_ready() || !gpu_idle() || compositor_card_holds() || !gpu.at || (pitch & 3) || !compositor.table)
                return 0;
        w = root ? (unsigned int)compositor_wl.lent[row].width : compositor.table->width[row];
        h = root ? (unsigned int)compositor_wl.lent[row].height : compositor.table->height[row];
        w4 = (w + 3) & ~3u;
        if (!w || !h || w4 > pitch || (root && !compositor_lent(row)))
                return 0;
        if (root)
        {
                positive bytes = (positive)(h - 1) * compositor_wl.lent[row].pitch * 4 + (positive)w * 4;
                src = compositor_wl.lent[row].fd >= 0 ? compositor_lent_va(row) : gpu_va_bytes(compositor_wl.lent[row].pixels, bytes);
                if (!src || compositor_wl.lent[row].pitch >= 1ul << 29)
                        return 0;
                op[0] = 4u | (compositor_lent_swap(row) ? 0x80u : 0u), op[1] = op[2] = 0, op[3] = w, op[4] = h;
                op[5] = (unsigned int)src, op[6] = (unsigned int)(src >> 32), op[7] = (unsigned int)compositor_wl.lent[row].pitch;
                n++;
        }
        for (unsigned int k = 0; k < compositor_wl_layers.count[row]; k++)
        {
                const struct compositor_wl_layer *l = &compositor_wl_layers.layer[row][k];
                unsigned int *r = op + 8 * n, at_pitch = (unsigned int)l->pitch;

                if (l->gather)
                {
                        const struct compositor_wl_gathered *g = l->at < WAYLAND_LAYERS ? &compositor_wl_layers.gathered[row][l->at] : 0;
                        src = g && g->filled && g->id == l->id ? canvas_ext_import_live(g->va, g->epoch) : 0, at_pitch = (unsigned int)l->width;
                }
                else
                        src = l->fd >= 0 ? canvas_ext_import_live(l->va, l->epoch) : gpu_va_bytes(l->pixels, compositor_wl_layer_bytes(l));
                if (!src || l->x < 0 || l->y < 0 || (unsigned int)(l->x + l->width) > w || (unsigned int)(l->y + l->height) > h)
                        return 0;
                r[0] = (l->blend ? 11u : 4u) | (l->swap ? 0x80u : 0u), r[1] = (unsigned int)l->x, r[2] = (unsigned int)l->y;
                r[3] = (unsigned int)(l->x + l->width), r[4] = (unsigned int)(l->y + l->height);
                r[5] = (unsigned int)src, r[6] = (unsigned int)(src >> 32), r[7] = at_pitch;
                n++;
        }
        if (!n || !(to_va = wl_gpu_va_written(to, compositor.buffer)) || (to_va & 15))
                return 0;
        if (!gpu_scene(COMPOSITOR_GPU_OPS_AT, n, COMPOSITOR_GPU_BINS_AT, to_va, (unsigned int)pitch, w4, h, 0, 0, w4, h))
        {
                if (gpu.pending)
                        gpu_finish();
                if (gpu_lost)
                        compositor_gpu_recover();
                compositor_gpu_job_failed();
                return 0;
        }
        if (compositor.table)
        {
                if (root)
                        compositor.table->client_seeded[0]++, compositor_wl_copied(row, WINDOWS_COPY_WHOLE);
                compositor.table->client_layers_laid_card += compositor_wl_layers.count[row];
        }
        return 1;
#else
        (void)row, (void)to, (void)root;
        return 0;
#endif
}

static void compositor_wl_unlend(int row)
{
        if (row >= 0 && row < COMPOSITOR_ROWS && compositor_wl_layers.count[row])
                compositor_wl_layers_drop(row);
        if (!wl_row_lent(row))
                return;
        compositor_wl_lent_drop_fb(row);
        canvas_ext_import_release(&compositor_wl.lent[row].imp);
        memory_zero(&compositor_wl.lent[row], sizeof compositor_wl.lent[row]);
        wl_ext_row_sync(row);
}

static void wl_ext_tiled_settle(void);

static void compositor_wl_seed(int row, unsigned int which)
{
        unsigned int *to;
        if (!wl_row_lent(row))
                return;
        wl_ext_tiled_settle();
        to = (unsigned int *)((char *)compositor.table + compositor.pixels + ((unsigned long)row * 2 + (which & 1u)) * compositor.buffer);
        if (compositor_wl_card_lay(row, to, 1))
        {
                compositor_wl.laying[row].last = (unsigned char)((which & 1u) + 1);
                compositor_wl_unlend(row);
                return;
        }
        if (compositor.table)
                compositor.table->client_seeded[1]++, compositor_wl_copied(row, WINDOWS_COPY_WHOLE);
        canvas_dmabuf_sync_read(compositor_wl.lent[row].fd, 0);
        if (compositor_lent_swap(row))
        {
                unsigned int alpha = compositor_wl.lent[row].fourcc == WAYLAND_FOURCC_XBGR8888 ? 0xff000000u : 0;
                for (long y = 0; y < compositor_wl.lent[row].height; y++)
                {
                        unsigned int *line = to + (unsigned long)y * (unsigned long)compositor_stride();
                        memory_copy_rect_stream_u32(line, (unsigned long)compositor_wl.lent[row].width,
                                                    compositor_wl.lent[row].pixels + (unsigned long)y * compositor_wl.lent[row].pitch,
                                                    compositor_wl.lent[row].pitch, (unsigned long)compositor_wl.lent[row].width, 1);
                        memory_swizzle_rb_u32(line, line, (positive)compositor_wl.lent[row].width, alpha);
                }
        }
        else if (compositor_wl.lent[row].fd >= 0)
                memory_copy_rect_stream_u32(to, (unsigned long)compositor_stride(), compositor_wl.lent[row].pixels, compositor_wl.lent[row].pitch,
                                            (unsigned long)compositor_wl.lent[row].width, (unsigned long)compositor_wl.lent[row].height);
        else
                memory_copy_rect_u32(to, (unsigned long)compositor_stride(), compositor_wl.lent[row].pixels, compositor_wl.lent[row].pitch,
                                     (unsigned long)compositor_wl.lent[row].width, (unsigned long)compositor_wl.lent[row].height);
        canvas_dmabuf_sync_read(compositor_wl.lent[row].fd, 1);
        compositor_wl_layers_put(row, to, (unsigned long)compositor_stride());
        compositor_wl.laying[row].last = (unsigned char)((which & 1u) + 1);
        compositor_runs_forget(row);
        compositor_wl_unlend(row);
}

static void compositor_wl_layers_lay(int row)
{
        if (row < 0 || row >= COMPOSITOR_ROWS || !compositor_wl_layers.count[row])
                return;
        if (wl_row_lent(row))
                compositor_wl_seed(row, compositor.front[row]);
        else
        {
                unsigned int *to = (unsigned int *)((char *)compositor.table + compositor.pixels +
                                                    ((unsigned long)row * 2 + (compositor.front[row] & 1u)) * compositor.buffer);
                if (!compositor_wl_card_lay(row, to, 0))
                        wl_ext_tiled_settle(), compositor_wl_layers_put(row, to, (unsigned long)compositor_stride());
                compositor_runs_forget(row);
        }
        compositor_wl_layers_drop(row);
}

static int compositor_wl_meets_corner(int x1, int y1, int x2, int y2, int width, int height, int radius, int line)
{
        int near = radius - line;
        return radius > 0 && (x1 < near || x2 > width - near) && (y1 < near || y2 > height - near);
}

#define COMPOSITOR_WL_WC_PRESENTS 2

static void wl_ext_settle(int row)
{
        if (row >= 0 && row < COMPOSITOR_ROWS && compositor_wl_layers.count[row])
        {
                for (unsigned int at = 0; at < compositor_wl_layers.count[row]; at++)
                        if (compositor_wl_cut(compositor_wl_layers.layer[row][at].pixels))
                                gpu_forget(compositor_wl_layers.layer[row][at].pixels, compositor_wl_layer_bytes(&compositor_wl_layers.layer[row][at]));
                if (!wl_row_lent(row) || compositor.landing[row] || compositor.glassy[row])
                        compositor_wl_layers_refused(11), compositor_wl_layers_lay(row);
        }
        if (!wl_row_lent(row))
                return;
        if (row >= 0 && row < COMPOSITOR_ROWS && compositor_wl.lent[row].fd >= 0 && compositor_wl.wc_reads[row] >= COMPOSITOR_WL_WC_PRESENTS)
        {
                compositor_wl.wc_reads[row] = 0, compositor_wl.wc_copy[row] = 1, compositor_wl.wc_seeded++;
                if (compositor.table)
                        compositor.table->client_wc_seeded = compositor_wl.wc_seeded;
                compositor_wl_seed(row, compositor.front[row]);
                return;
        }
        if (compositor_wl_cut(compositor_wl.lent[row].pixels))
                gpu_forget(compositor_wl.lent[row].pixels, (positive)compositor_wl.lent[row].height * compositor_wl.lent[row].pitch * 4);
        if ((compositor.w[row] != (unsigned int)compositor_wl.lent[row].width || compositor.h[row] != (unsigned int)compositor_wl.lent[row].height) &&
            !(compositor.fullscreen[row] && compositor_wl.lent[row].width > 0 && compositor_wl.lent[row].height > 0 &&
              (unsigned int)compositor_wl.lent[row].width == compositor.picture_w[row] &&
              (unsigned int)compositor_wl.lent[row].height == compositor.picture_h[row]))
                compositor_wl_seed(row, compositor.front[row]);
}

static int compositor_wl_dmabuf_ready(long client, int fd);
static int compositor_wl_sync_start(long client, int fd, int ready);

static void compositor_wl_pic_ident(const struct wayland_picture *picture, unsigned long *dev, unsigned long *ino)
{
        *dev = 0, *ino = 0;
        if (picture->pool_id)
                *ino = picture->pool_id;
        else if (picture->sync_fd >= 0)
                canvas_ext_fd_ident(picture->sync_fd, dev, ino);
}

static positive compositor_gpu_import_layer(struct compositor_wl_layer *old, unsigned int olds, const unsigned char *map, long fd, long size,
                                            unsigned long dev, unsigned long ino, int nv12, unsigned long extent)
{
        for (unsigned int k = 0; k < olds; k++)
                if (old[k].fd == fd && old[k].dev == dev && old[k].ino == ino &&
                    (nv12 ? (old[k].nv12 && old[k].extent == extent) : (old[k].map == map && old[k].size == size)) &&
                    canvas_ext_import_live(old[k].import, old[k].epoch))
                {
                        positive got = old[k].import;
                        old[k].import = 0;
                        return got;
                }
        return gpu_import_fd((int)fd, (positive)(nv12 ? extent : (unsigned long)size));
}

static void compositor_wl_gather_set(struct compositor_wl_layer *l, const struct wayland_picture *p, long tw, long th, long x1, long y1)
{
        l->gather = 1, l->source_w = p->width, l->source_h = p->height;
        l->transform = p->transform & 7, l->y_invert = p->y_invert != 0;
        l->sx = p->src_width > 0 ? p->src_x : 0, l->sy = p->src_width > 0 ? p->src_y : 0;
        l->sw = p->src_width > 0 ? p->src_width : (p->transform & 1 ? p->height : p->width) * 256;
        l->sh = p->src_width > 0 ? p->src_height : (p->transform & 1 ? p->width : p->height) * 256;
        l->tw = tw, l->th = th, l->ox = x1 - p->dest_x, l->oy = y1 - p->dest_y;
}

static int compositor_wl_needs_xform_ordered(const struct wayland_picture *picture, long width, long height, int swapped)
{
        unsigned int fourcc = picture->fourcc;
        int order = (swapped & 1) && compositor_wl_fourcc_swapped(fourcc);
        int cropped = picture->src_width > 0;
        long seen_w = cropped ? picture->src_width / 256 : picture->width, seen_h = cropped ? picture->src_height / 256 : picture->height;

        if ((fourcc && fourcc != WAYLAND_FOURCC_ARGB8888 && fourcc != WAYLAND_FOURCC_XRGB8888 && !order) ||
            (picture->format != WLN_wl_shm_format_argb8888 && picture->format != WLN_wl_shm_format_xrgb8888 &&
             !(order && (unsigned long)picture->format == (unsigned long)fourcc)) ||
            picture->transform || picture->y_invert || (picture->plane_count > 1 && !(swapped & 2)) ||
            (picture->modifier && picture->modifier != WAYLAND_MODIFIER_INVALID && !(swapped & 2)))
                return 1;
        if (cropped && (picture->src_x != 0 || picture->src_y != 0 || picture->src_width % 256 || picture->src_height % 256 ||
                        seen_w > picture->width || seen_h > picture->height || picture->dest_x > 0 || picture->dest_y > 0 ||
                        -picture->dest_x + width > seen_w || -picture->dest_y + height > seen_h))
                return 1;
        return picture->dst_width > 0 && (picture->dst_width != seen_w || picture->dst_height != seen_h);
}

static int compositor_wl_needs_xform(const struct wayland_picture *picture, long width, long height)
{
        return compositor_wl_needs_xform_ordered(picture, width, height, 0);
}

#ifdef MOONWATER_GPU_C
static int compositor_gpu_held(void);
#endif

static int compositor_wl_card_composes(void)
{
#ifdef MOONWATER_GPU_C
        return compositor_gpu && compositor_gpu_state.up && !compositor_gpu_state.dead && !compositor_gpu_held();
#else
        return 0;
#endif
}

static int compositor_wl_card_swaps(const struct wayland_picture *picture)
{
        return compositor_wl_fourcc_swapped(picture->fourcc) && compositor_wl_card_composes();
}

#define COMPOSITOR_WL_SYNC_REACH 512
static int compositor_wl_row_damaged(int row)
{
        long x0 = compositor.x[row] - COMPOSITOR_WL_SYNC_REACH, y0 = compositor.y[row] - COMPOSITOR_WL_SYNC_REACH;
        long x1 = compositor.x[row] + (long)compositor.w[row] + COMPOSITOR_WL_SYNC_REACH, y1 = compositor.y[row] + (long)compositor.h[row] + COMPOSITOR_WL_SYNC_REACH;
        for (unsigned int at = 0; at < compositor.damages; at++)
                if (compositor.damage[at][0] < x1 && compositor.damage[at][2] > x0 && compositor.damage[at][1] < y1 && compositor.damage[at][3] > y0)
                        return 1;
        return 0;
}

static void wl_ext_lent_sync(int start)
{
        if (start)
                compositor_wl.lent_synced = 1;
        else
        {
                if (!compositor_wl.lent_synced)
                        return;
                compositor_wl.lent_synced = 0;
        }
        for (int row = 0; row < (int)compositor_rows_in_use; row++)
                if (wl_row_lent(row) && compositor_wl.lent[row].fd >= 0)
                {
                        if (start)
                        {
                                long client = wayland_row_owner(row);

                                // A present that draws none of the window (a pointer's move, a clock's tick elsewhere) reads none of its buffer: no poll,
                                // two timers and two ioctls a window a present, and no wait for a client's card to finish what it is drawing. The box is
                                // taken COMPOSITOR_WL_SYNC_REACH wider than the window, since the glass is composed from what is round the damage.
                                if (compositor_wl.lent[row].synced || !compositor_wl_row_damaged(row))
                                        continue;
                                compositor_wl.lent[row].synced =
                                        compositor_wl_dmabuf_ready(client, (int)compositor_wl.lent[row].fd) &&
                                        compositor_wl_sync_start(client, (int)compositor_wl.lent[row].fd, 1);
                                // A present the processor reads this dma-buf in, once a present (a card that draws the present after says so, and
                                // the count is none again: wl_ext_count_card).
                                if (compositor_wl.wc_reads[row] < 255)
                                        compositor_wl.wc_reads[row]++;
                                compositor_wl.wc_presents++;
                                if (compositor.table)
                                        compositor.table->client_wc_presents = compositor_wl.wc_presents;
                        }
                        else if (compositor_wl.lent[row].synced)
                        {
                                canvas_dmabuf_sync_read(compositor_wl.lent[row].fd, 1);
                                compositor_wl.lent[row].synced = 0;
                        }
                }
        // The layers of dma-bufs, each bracketed as the picture under them is, for a present that draws their window.
        if (!compositor_wl_layers.total)
                return;
        for (int row = 0; row < (int)compositor_rows_in_use; row++)
                for (unsigned int k = 0, counted = wl_row_lent(row) && compositor_wl.lent[row].fd >= 0;
                     k < compositor_wl_layers.count[row]; k++)
                {
                        struct compositor_wl_layer *l = &compositor_wl_layers.layer[row][k];

                        if (l->fd < 0)
                                continue;
                        if (start && !l->synced && compositor_wl_row_damaged(row))
                        {
                                l->synced = compositor_wl_dmabuf_ready(wayland_row_owner(row), (int)l->fd) &&
                                            compositor_wl_sync_start(wayland_row_owner(row), (int)l->fd, 1);
                                if (!counted && compositor_wl.wc_reads[row] < 255)
                                        compositor_wl.wc_reads[row]++;
                                counted = 1;
                        }
                        else if (!start && l->synced)
                        {
                                canvas_dmabuf_sync_read(l->fd, 1);
                                l->synced = 0;
                        }
                }
}

// A present the card drew: counted once when it read a client's picture in place.
static void wl_ext_count_card(const unsigned int *rows, unsigned int windows)
{
        // The card drew this present: the processor read no lent dma-buf for it, and a row copied for want of the card is lent again.
        memory_zero(compositor_wl.wc_reads, compositor_rows_made * sizeof(*compositor_wl.wc_reads));
        memory_zero(compositor_wl.wc_copy, compositor_rows_made * sizeof(*compositor_wl.wc_copy));
        for (unsigned int at = 0; at < windows && compositor.table; at++)
                if (wl_row_lent((int)rows[at]))
                {
                        compositor.table->client_card++;
                        return;
                }
}

// A picture the card or the processor can read where it is: the whole window's, or the part of its buffer the window is (the picture's
// offset is minus its destination), of the size the window is, from a buffer that holds it. A dma-buf's card address is its import's,
// which is made here, and is refused (no lend) when the card is not up or has no room for it.
// A picture not lent (compositor_wl_lend), counted by its reason for moonwater debug: always 0.
static int compositor_wl_lend_refused(const struct wayland_picture *picture, unsigned int why)
{
        struct windows *table = compositor.table;

        if (table && why < sizeof table->client_lend_refused / sizeof table->client_lend_refused[0])
        {
                table->client_lend_refused[why]++;
                table->client_lend_format = (unsigned int)picture->format;
                table->client_lend_fourcc = picture->fourcc;
                table->client_lend_modifier = (unsigned long long)picture->modifier;
                table->client_lend_dmabuf = picture->sync_fd >= 0;
        }
        return 0;
}

// The card's copy of a damage rectangle refused (compositor_wl_card_rect), counted by its reason: always 0.
static int compositor_wl_card_refused(unsigned int why)
{
        struct windows *table = compositor.table;

        if (table && why < sizeof table->client_card_refused / sizeof table->client_card_refused[0])
                table->client_card_refused[why]++;
        return 0;
}

static int compositor_wl_lend(int row, const struct wayland_picture *picture, long width, long height)
{
        long x = -picture->dest_x, y = -picture->dest_y;
        unsigned long stride = (unsigned long)picture->stride, dev = 0, ino = 0;
        const unsigned char *at;
        positive va = 0, import = 0;
        int same;

        // A buffer is the surface's whole picture at every commit, and the damage only where it differs from the last one (the protocol's
        // word), so a commit with a part damaged is read in place like any other and that part alone is drawn again (the row's damage,
        // compositor_wl_lent_damage). The one buffer not taken at that word is one the shim says may not be whole (fragile: an X window's,
        // whose server draws on in what it committed): that one is read in place only when all of it is damaged, and copied otherwise.
        if (!picture->pixels || !picture->map || !picture->last)
                return compositor_wl_lend_refused(picture, 0);
        if (picture->blend)
                return compositor_wl_lend_refused(picture, 1);
        if (x < 0 || y < 0 || width <= 0 || height <= 0)
                return compositor_wl_lend_refused(picture, 2);
        if (compositor_wl_needs_xform_ordered(picture, width, height, compositor_wl_card_swaps(picture)))
                return compositor_wl_lend_refused(picture, 3);
        if (picture->fragile && !(picture->full || !picture->damages))
                return compositor_wl_lend_refused(picture, 4);
        // A row the processor composes is not lent a dma-buf (compositor_wl.wc_copy): its damage is copied with the streaming loads instead.
        // Clear wc_copy when a window enters fullscreen so it can still use direct scanout.
        if (picture->sync_fd >= 0 && row >= 0 && row < COMPOSITOR_ROWS && compositor_wl.wc_copy[row])
        {
                if (compositor.fullscreen[row])
                        compositor_wl.wc_copy[row] = 0, compositor_wl.wc_reads[row] = 0;
                else
                        return compositor_wl_lend_refused(picture, 5);
        }
        if (width > (long)compositor_most_width() || height > (long)compositor_most_height() || x + width > picture->width || y + height > picture->height)
                return compositor_wl_lend_refused(picture, 6);
        if (stride & 3 || stride < (unsigned long)picture->width * 4)
                return compositor_wl_lend_refused(picture, 7);
        at = picture->pixels + (unsigned long)y * stride + (unsigned long)x * 4;
        // The pool's file cut under the read: the window's first and last pixel are read here, as a copy would read them, so a pool cut before
        // this commit is found now (the SIGBUS handler maps zeros over it and marks it), and the picture is refused as a copy is refused.
        {
                volatile unsigned int probe;

                probe = *(const volatile unsigned int *)at;
                probe = probe + *(const volatile unsigned int *)(at + (unsigned long)(height - 1) * stride + (unsigned long)(width - 1) * 4);
                (void)probe;
        }
        if (compositor_wl_cut(picture->pixels))
                return compositor_wl_lend_refused(picture, 8);
#ifdef MOONWATER_GPU_C
        if (picture->sync_fd < 0 && compositor_gpu && compositor_gpu_state.up && !compositor_gpu_state.dead &&
            !gpu_va_bytes(at, (positive)(height - 1) * stride + (positive)width * 4) &&
            !gpu_map(at, (positive)(height - 1) * stride + (positive)width * 4))
                return compositor_wl_lend_refused(picture, 9);
#endif
        compositor_wl_pic_ident(picture, &dev, &ino);
        same = wl_row_lent(row) && canvas_ext_import_same(&compositor_wl.lent[row].imp, picture->map, picture->sync_fd, picture->map_bytes, dev, ino) &&
               compositor_wl.lent[row].width == width && compositor_wl.lent[row].height == height &&
               compositor_wl.lent[row].pitch == stride / 4 && compositor_wl.lent[row].fourcc == picture->fourcc &&
               compositor_wl.lent[row].offset == (unsigned int)picture->plane_offset[0] &&
               compositor_wl.lent[row].modifier == picture->modifier;
        if (picture->sync_fd >= 0)
        {
                if (!gpu_ready())
                        return compositor_wl_lend_refused(picture, 10);
                if (compositor_wl_card_barred(row))
                        return compositor_wl_lend_refused(picture, 11);
                // The import is the buffer's, from its start: the same one while the buffer is the same, else a new one.
                import = same ? compositor_wl.lent[row].import : gpu_import_fd((int)picture->sync_fd, (positive)picture->map_bytes);
                if (!import)
                        return compositor_wl_lend_refused(picture, 11);
                va = import + (positive)(at - picture->map);
        }
        if (wl_row_lent(row) && !same)
        {
                compositor_wl_lent_drop_fb(row);
                canvas_ext_import_release(&compositor_wl.lent[row].imp);
                compositor_wl.lent[row].scan_failed = 0;
        }
        // The same buffer with the window at another place in it (its geometry moved, its size the same): the framebuffer was made at the old
        // place (its offset, or its origin scan_x and scan_y for the overlay), so it goes; the import, which is the buffer's from its start, stays.
        else if (wl_row_lent(row) && compositor_wl.lent[row].pixels != (const unsigned int *)at)
                compositor_wl_lent_drop_fb(row);
        compositor_wl.lent[row].pixels = (const unsigned int *)at;
        compositor_wl.lent[row].pitch = stride / 4;
        compositor_wl.lent[row].width = width;
        compositor_wl.lent[row].height = height;
        compositor_wl.lent[row].imp = (struct canvas_ext_import){
                .map = picture->map, .fd = picture->sync_fd, .size = picture->map_bytes,
                .dev = dev, .ino = ino, .va = import, .epoch = gpu_epoch,
        };
        compositor_wl.lent[row].va = va;
        compositor_wl.lent[row].modifier = picture->modifier;
        compositor_wl.lent[row].fourcc = picture->fourcc;
        compositor_wl.lent[row].offset = (unsigned int)picture->plane_offset[0];
        compositor_wl.lent[row].synced = 0;
        wl_ext_row_sync(row);
        return 1;
}

// The bytes of a pool picture the card reads: from its first pixel to the end of its last row, inside its pool (a picture that runs past the
// pool's end is cut there, and a rectangle past that is not found in the map). 0 for a picture that is not in its pool or has no rows.
static unsigned long compositor_wl_picture_bytes(const struct wayland_picture *picture)
{
        unsigned long used, room;

        if (picture->pixels < picture->map || picture->map_bytes <= 0 || picture->width <= 0 || picture->height <= 0 || picture->stride < picture->width * 4)
                return 0;
        if ((unsigned long)(picture->pixels - picture->map) >= (unsigned long)picture->map_bytes)
                return 0;
        room = (unsigned long)picture->map_bytes - (unsigned long)(picture->pixels - picture->map);
        if ((unsigned long)(picture->height - 1) > room / (unsigned long)picture->stride)
                return room;
        used = (unsigned long)(picture->height - 1) * (unsigned long)picture->stride + (unsigned long)picture->width * 4;
        return used < room ? used : room;
}

/*
        The card's address of the start of a dma-buf picture's buffer, imported for the row while it is the one copied from (compositor_wl.source): the
        same import while the buffer is, a new one for another. 0 when the kernel refuses it.
*/
static positive compositor_wl_source_import(int row, const struct wayland_picture *picture)
{
        unsigned long dev = 0, ino = 0;
        long need_bytes = picture->map_bytes > 0 ? picture->map_bytes : (long)picture->plane_offset[0] + (long)picture->height * (long)picture->stride;

        compositor_wl_pic_ident(picture, &dev, &ino);
        return canvas_ext_import_cached(&compositor_wl.source[row], picture->map, picture->sync_fd, need_bytes, dev, ino, compositor_wl_card_barred(row), 0);
}

// Whether a picture is tiled: a dma-buf of a modifier other than linear and the implicit one (whose layout is the buffer's own word, as before).
static int compositor_wl_tiled(const struct wayland_picture *picture)
{
        return picture->sync_fd >= 0 && picture->modifier && picture->modifier != WAYLAND_MODIFIER_INVALID;
}

// Whether the card reads a tiled picture of this modifier (gpu_image_takes); never without the card.
static int compositor_wl_tiled_takes(unsigned long modifier)
{
#ifdef MOONWATER_GPU_C
        return gpu_image_takes((p64)modifier);
#else
        (void)modifier;
        return 0;
#endif
}

// Whether tiled pictures are what the card reads now: the setting has the card compose, it is up and not given up, its texture unit's read passed
// at its start, and the render node the clients are told to draw on is the card's own (a client's tiles are its card's layout, which only that card
// reads). The node's match is found once a start of the card (gpu_epoch), the rest is a few loads: it is asked at every wake.
static int compositor_wl_tiled_now(void)
{
#ifdef MOONWATER_GPU_C
        // (the display's primary plane is found by the planes' first look, compositor_overlay_ready: its IN_FORMATS are the pairs told)
        if (!compositor_gpu || !compositor_gpu_state.up || compositor_gpu_state.dead || !compositor_wl.render || compositor.card < 0 || !compositor.primary ||
            !gpu_image_takes(0))
                return 0;
        if (compositor_wl.tiled_node_epoch != (int)gpu_epoch + 1)
        {
                unsigned char info[256];

                memory_zero(info, sizeof info);
                compositor_wl.tiled_node = 0;
                // AT_EMPTY_PATH, STATX_BASIC_STATS: the card's node's device (stx_rdev_major at 128, stx_rdev_minor at 132).
                if (gpu.fd >= 0 && compositor_call(syscall(statx), (positive)gpu.fd, (positive)"", 0x1000, 0x7ff, (positive)info, 0) >= 0)
                        compositor_wl.tiled_node = canvas_drm_makedev(*(unsigned int *)(info + 128), *(unsigned int *)(info + 132)) == compositor_wl.render;
                compositor_wl.tiled_node_epoch = (int)gpu_epoch + 1;
        }
        return compositor_wl.tiled_node;
#else
        return 0;
#endif
}

// The log's word on the tiled pictures read: at the first rectangle, then every 4096th, and at the first refusal of each kind.
static void compositor_wl_tiled_say(const char *what)
{
        string_format(log, "compositor: tiled pictures (%s): %p rectangles read by the texture unit (%p pixels, %p us waited, the longest %p us); refused: %p not composing, %p blend, "
                           "%p transform, %p plane, %p import, %p resource, %p job, %p layout\n",
                      (string_address)what, (positive)compositor_wl.tiled_rects, compositor_wl.tiled_pixels, compositor_wl.tiled_ns / 1000, compositor_wl.tiled_most_ns / 1000,
                      (positive)compositor_wl.tiled_refused[0], (positive)compositor_wl.tiled_refused[1],
                      (positive)compositor_wl.tiled_refused[2], (positive)compositor_wl.tiled_refused[3], (positive)compositor_wl.tiled_refused[4],
                      (positive)compositor_wl.tiled_refused[5], (positive)compositor_wl.tiled_refused[6], (positive)compositor_wl.tiled_refused[7]);
        log_flush();
}

static int compositor_wl_tiled_refused(unsigned int why)
{
        if (compositor_wl.tiled_refused[why & 7]++ == 0)
                compositor_wl_tiled_say("a refusal");
        return 0;
}

// A tiled read submitted without a wait (compositor_wl_card_image) waited for, before the processor reads a table's picture it may have written.
static void wl_ext_tiled_settle(void)
{
#ifdef MOONWATER_GPU_C
        if (compositor_wl.tiled_seq && gpu_done(compositor_wl.tiled_seq) == 0 && !gpu_finish() && gpu_end != GPU_END_NONE)
                compositor_gpu_job_failed();
#endif
        compositor_wl.tiled_seq = 0;
        compositor_wl.tiled_row = -1;
}

static unsigned int compositor_wl_tiled_desc(int row, const struct wayland_picture *picture, unsigned int desc[8], int *refused)
{
#ifdef MOONWATER_GPU_C
        unsigned long stride = (unsigned long)picture->stride, dev = 0, ino = 0;
        unsigned int meta_plane = 0, kind;
        positive base, meta = 0;
        long bytes;

        if (!gpu_ready() || !compositor_gpu || !compositor_gpu_state.up || compositor_gpu_state.dead || compositor_gpu_held())
                return refused ? (*refused = 0), 0u : 0u;
        if (!gpu_image_takes(picture->modifier))
                return refused ? (*refused = 7), 0u : 0u;
        if (compositor_wl_needs_xform_ordered(picture, picture->row_width, picture->row_height, 3))
                return refused ? (*refused = 2), 0u : 0u;
        if (picture->modifier >> 13 & 1)
                meta_plane = picture->modifier >> 14 & 1 ? 2u : 1u;
        if (meta_plane)
                canvas_ext_fd_ident(picture->sync_fd, &dev, &ino);
        else
                compositor_wl_pic_ident(picture, &dev, &ino);
        if (meta_plane)
        {
                unsigned long plane_dev = 0, plane_ino = 0;

                if ((long)meta_plane >= picture->plane_count)
                        return refused ? (*refused = 3), 0u : 0u;
                if (picture->plane_fd[meta_plane] != picture->sync_fd)
                        canvas_ext_fd_ident(picture->plane_fd[meta_plane], &plane_dev, &plane_ino);
                else
                        plane_dev = dev, plane_ino = ino;
                if (plane_dev != dev || plane_ino != ino || !ino)
                        return refused ? (*refused = 3), 0u : 0u;
        }
        bytes = compositor_call(syscall(lseek), (positive)picture->sync_fd, 0, 2, 0, 0, 0);
        if (bytes <= 0)
                return refused ? (*refused = 4), 0u : 0u;
        {
                int reused = 0;

                if (!canvas_ext_import_cached(&compositor_wl.source[row], 0, picture->sync_fd, bytes, dev, ino, compositor_wl_card_barred(row), &reused))
                        return refused ? (*refused = 4), 0u : 0u;
                if (!reused)
                {
                        static int said;

                        if (said++ < 12)
                                string_format(log, "compositor: tiled import %p bytes at %p (fd %p): modifier %p, %p planes, offsets %p %p %p, strides %p %p %p, %px%p\n",
                                              (positive)bytes, (positive)compositor_wl.source[row].va, (positive)picture->sync_fd, (positive)picture->modifier,
                                              (positive)picture->plane_count, (positive)picture->plane_offset[0], (positive)picture->plane_offset[1],
                                              (positive)picture->plane_offset[2], (positive)picture->plane_stride[0], (positive)picture->plane_stride[1],
                                              (positive)picture->plane_stride[2], (positive)picture->width, (positive)picture->height),
                                    log_flush();
                }
        }
        base = compositor_wl.source[row].va + (positive)picture->plane_offset[0];
        if (meta_plane)
                meta = compositor_wl.source[row].va + (positive)picture->plane_offset[meta_plane];
        if ((stride & 3) || (positive)picture->plane_offset[0] >= (positive)bytes || (meta_plane && (positive)picture->plane_offset[meta_plane] >= (positive)bytes) ||
            !gpu_amd_image_fits((positive)bytes, (positive)picture->plane_offset[0], meta_plane ? (positive)picture->plane_offset[meta_plane] : 0,
                                (unsigned int)(stride / 4), (unsigned int)picture->height, (p64)picture->modifier) ||
            !(kind = gpu_amd_image(desc, base, meta, (unsigned int)(stride / 4), (unsigned int)picture->height, (p64)picture->modifier,
                                   compositor_wl_fourcc_swapped(picture->fourcc), false)))
                return refused ? (*refused = 5), 0u : 0u;
        return kind;
#else
        (void)row, (void)picture, (void)desc;
        if (refused)
                *refused = 0;
        return 0;
#endif
}

static unsigned int wl_ext_straight_desc(int row, unsigned int desc[8])
{
        if (!compositor_gpu_tiled_ready() || !compositor_straight(row) || !compositor_wl.straight[row].owed)
                return 0;
        return compositor_wl_tiled_desc(row, &compositor_wl.straight[row].picture, desc, 0);
}

static int compositor_wl_card_image(int row, const struct wayland_picture *picture, unsigned int *to, unsigned long pitch, long dx, long dy, long sx,
                                    long sy, long w, long h)
{
#ifdef MOONWATER_GPU_C
        unsigned int desc[8], kind;
        int why = 0;
        positive dst_va, began;

        if (!(kind = compositor_wl_tiled_desc(row, picture, desc, &why)))
                return compositor_wl_tiled_refused(why);
        if (!(dst_va = wl_gpu_va_written(to, compositor.buffer)))
                return compositor_wl_tiled_refused(6);
        if (picture->last && !picture->blend)
        {
                if (compositor_wl.tiled_seq && compositor_wl.tiled_row != row)
                        wl_ext_tiled_settle();
                positive sequence = gpu_image_go(desc, kind, (unsigned int)sx, (unsigned int)sy, dst_va + 4 * ((positive)dy * pitch + (positive)dx),
                                                 (unsigned int)pitch, (unsigned int)w, (unsigned int)h);
                if (sequence)
                {
                        compositor_wl.tiled_seq = sequence, compositor_wl.tiled_row = row, compositor_wl.tiled_lent = 1, compositor_wl.tiled_unwaited++;
                        compositor_wl.tiled_pixels += (positive)w * (positive)h;
                        if ((compositor_wl.tiled_rects++ & 4095) == 0)
                                compositor_wl_tiled_say("read");
                        return 1;
                }
                if (gpu_lost)
                        return compositor_wl_tiled_refused(6);
        }
        if (!gpu_idle() && !gpu_finish())
                return compositor_wl_tiled_refused(6);
        began = clock_monotonic_nanoseconds();
        if (gpu_image(desc, kind | (picture->blend ? GPU_IMAGE_OVER : 0), (unsigned int)sx, (unsigned int)sy, dst_va + 4 * ((positive)dy * pitch + (positive)dx),
                      (unsigned int)pitch, (unsigned int)w, (unsigned int)h))
        {
                positive took = clock_monotonic_nanoseconds() - began;
                compositor_wl.tiled_ns += took, compositor_wl.tiled_pixels += (positive)w * (positive)h;
                if (took > compositor_wl.tiled_most_ns)
                        compositor_wl.tiled_most_ns = took;
                if ((compositor_wl.tiled_rects++ & 4095) == 0)
                        compositor_wl_tiled_say("read");
                return 1;
        }
        if (gpu_end == GPU_END_LATE && gpu_finish())
                return compositor_wl.tiled_rects++, 1;
        if (gpu_end != GPU_END_NONE)
                compositor_gpu_job_failed();
        return compositor_wl_tiled_refused(6);
#else
        (void)row, (void)picture, (void)to, (void)pitch, (void)dx, (void)dy, (void)sx, (void)sy, (void)w, (void)h;
        return compositor_wl_tiled_refused(0);
#endif
}

static void compositor_wl_scan_retry(int row)
{
        if (row < 0 || row >= COMPOSITOR_ROWS)
                return;
        if (compositor_wl.straight)
                compositor_wl.straight[row].scan_failed = 0;
        if (compositor_wl.lent)
                compositor_wl.lent[row].scan_failed = 0;
}

static void compositor_wl_straight_drop(int row)
{
        if (!compositor_straight(row))
                return;
        canvas_scan_fb_grave(compositor_wl.straight[row].scan_fb);
        memory_zero(&compositor_wl.straight[row], sizeof compositor_wl.straight[row]);
        wl_ext_row_sync(row);
}

static int compositor_wl_straight_read(int row, long y0, long y1)
{
        struct wayland_picture whole;
        struct windows *table = compositor.table;
        unsigned int which = compositor.front[row] & 1u, *to;
        long w, h;

        memory_copy(&whole, &compositor_wl.straight[row].picture, sizeof whole);
        whole.full = 1, whole.damages = 0, whole.last = 1, whole.blend = 0;
        w = (long)table->width[row] < whole.width ? (long)table->width[row] : whole.width;
        h = (long)table->height[row] < whole.height ? (long)table->height[row] : whole.height;
        to = (unsigned int *)((char *)table + compositor.pixels + ((unsigned long)row * 2 + which) * compositor.buffer);
        if (!whole.explicit_sync && whole.sync_fd >= 0)
        {
                struct compositor_poll watch = {(int)whole.sync_fd, COMPOSITOR_POLLIN, 0};
                positive wait[2] = {0, COMPOSITOR_WL_STRAIGHT_WAIT_NS};
                compositor_call(syscall(ppoll), (positive)&watch, 1, (positive)wait, 0, 0, 0);
        }
        y1 = y1 < h ? y1 : h, y0 = y0 < 0 ? 0 : y0;
        if (w <= 0 || y1 <= y0 || !compositor_wl_card_image(row, &whole, to, (unsigned long)compositor_stride(), 0, y0, 0, y0, w, y1 - y0))
                return 0;
        table->client_rects_card++, compositor_wl_copied(row, WINDOWS_COPY_CARD);
        compositor_runs_forget(row);
        return 1;
}

static void compositor_wl_straight_pay(int row)
{
        if (!compositor_straight(row) || !compositor_wl.straight[row].owed || !compositor.table)
                return;
        if (compositor_wl_straight_read(row, 0, compositor_wl.straight[row].picture.height))
        {
                compositor_wl.straight[row].owed = 0;
                wl_ext_tiled_settle(), compositor_wl.laying[row].last = (unsigned char)((compositor.front[row] & 1u) + 1);
        }
}

static void wl_ext_straight_pays(int keep)
{
        for (int row = 0; row < (int)compositor_rows_in_use; row++)
                if (row != keep && compositor_straight(row) && compositor_wl.straight[row].owed)
                        compositor_wl_straight_pay(row);
}

static void wl_ext_straight_settle(int row, long y0, long y1)
{
        if (compositor_straight(row) && compositor_wl_straight_read(row, y0, y1))
                wl_ext_tiled_settle();
}

static int compositor_wl_straight_shown(int row)
{
        if (compositor.direct_row != row || !compositor.showing[row])
                return 0;
        if (compositor.plane_row != row)
                return 1;
        return compositor.plane_rect[0] == compositor.x[row] && compositor.plane_rect[2] == compositor.x[row] + (int)compositor.w[row] &&
               compositor.plane_rect[1] >= compositor.y[row] && compositor.plane_rect[3] <= compositor.y[row] + (int)compositor.h[row];
}

static int compositor_wl_straight_whole(int row)
{
        return compositor.plane_row != row ||
               (compositor.plane_rect[1] == compositor.y[row] && compositor.plane_rect[3] == compositor.y[row] + (int)compositor.h[row]);
}

static int compositor_wl_straight_take(int row, const struct wayland_picture *picture, long width, long height)
{
        struct wayland_picture *was = &compositor_wl.straight[row].picture;
        unsigned long dev = 0, ino = 0;
        int shown, same;

        if (!compositor_wl_tiled(picture) || !picture->last ||
            (picture->blend && !compositor_row_opaque_fs(row)) ||
            picture->dest_x || picture->dest_y || width <= 0 || height <= 0 ||
            picture->width < width || picture->height < height || picture->plane_count > 4 || compositor_wl.laying[row].open ||
            compositor_wl_layers.pending_row == row || compositor_wl_layers.count[row] || !compositor_wl_tiled_now() ||
            !compositor_wl_tiled_takes(picture->modifier) || compositor_wl_needs_xform_ordered(picture, width, height, 3))
        {
                compositor_wl_straight_pay(row);
                compositor_wl_straight_drop(row);
                return 0;
        }
        shown = (compositor_wl_straight_whole(row) || picture->explicit_sync || compositor_wl_dmabuf_ready(wayland_row_owner(row), (int)picture->sync_fd)) &&
                (compositor_wl_straight_shown(row) || compositor_gpu_tiled_ready() ||
                 (compositor.direct_row >= 0 && compositor.direct_row != row && compositor.plane_row != compositor.direct_row)) &&
                compositor.table && width == (long)compositor.table->width[row] && height == (long)compositor.table->height[row];
        compositor_wl_pic_ident(picture, &dev, &ino);
        same = compositor_straight(row) && compositor_wl.straight[row].dev == dev && compositor_wl.straight[row].ino == ino && ino &&
               was->modifier == picture->modifier && was->fourcc == picture->fourcc && was->plane_count == picture->plane_count &&
               was->width == picture->width && was->height == picture->height &&
               memory_compare(was->plane_offset, picture->plane_offset, sizeof was->plane_offset) == 0 &&
               memory_compare(was->plane_stride, picture->plane_stride, sizeof was->plane_stride) == 0;
        if (!shown)
                compositor_wl_straight_pay(row);
        if (!same)
                canvas_scan_fb_grave(compositor_wl.straight[row].scan_fb), compositor_wl.straight[row].scan_fb = 0, compositor_wl.straight[row].scan_failed = 0;
        memory_copy(was, picture, sizeof *was);
        compositor_wl.straight[row].dev = dev, compositor_wl.straight[row].ino = ino;
        compositor_wl.straight[row].live = 1, compositor_wl.straight[row].owed = (unsigned char)(shown != 0);
        if (shown && wl_row_lent(row))
                compositor_wl_unlend(row);
        wl_ext_row_sync(row);
        if (shown && compositor.plane_row == row && !compositor_wl_straight_whole(row))
        {
                compositor_wl_straight_read(row, 0, compositor.plane_rect[1] - compositor.y[row]);
                compositor_wl_straight_read(row, compositor.plane_rect[3] - compositor.y[row], height);
        }
        return shown ? 2 : 1;
}

static int compositor_wl_card_rect(int row, const struct wayland_picture *picture, unsigned int *to, unsigned long pitch,
                                   long dx, long dy, long sx, long sy, long w, long h, int wanted)
{
        unsigned long stride = (unsigned long)picture->stride, extent, span;
        const unsigned char *src = 0;
        positive src_va, dst_va;
        volatile unsigned int probe;

        if (w > 0 && h > 0 && compositor_wl_tiled(picture))
                return compositor_wl_card_image(row, picture, to, pitch, dx, dy, sx, sy, w, h);
        if (!wanted || w <= 0 || h <= 0)
                return compositor_wl_card_refused(0);
        if (!gpu_ready())
                return compositor_wl_card_refused(1);
#ifdef MOONWATER_GPU_C
        if (!compositor_gpu || !compositor_gpu_state.up || compositor_gpu_state.dead || compositor_gpu_held())
                return compositor_wl_card_refused(1);
#endif
        if (picture->blend)
                return compositor_wl_card_refused(3);
        if (!picture->last && !gpu_idle())
                return compositor_wl_card_refused(2);
        if (compositor_wl_needs_xform(picture, picture->row_width, picture->row_height))
                return compositor_wl_card_refused(4);
        if (picture->sync_fd < 0 && (!picture->pixels || !picture->map || picture->map_bytes <= 0))
                return 0;
        extent = 4 * ((stride / 4) * (unsigned long)(h - 1) + (unsigned long)w);
        if (picture->pixels && picture->map)
        {
                src = picture->pixels + (unsigned long)sy * stride + (unsigned long)sx * 4;
                probe = *(const volatile unsigned int *)src;
                probe += *(const volatile unsigned int *)(src + extent - 4);
                (void)probe;
                if (compositor_wl_cut(picture->pixels))
                        return compositor_wl_card_refused(5);
        }
        if (picture->sync_fd < 0)
        {
                src_va = gpu_va_bytes(src, extent);
                if (!src_va && (span = compositor_wl_picture_bytes(picture)) && gpu_map(picture->pixels, (positive)span))
                        src_va = gpu_va_bytes(src, extent);
                if (!src_va)
                        return compositor_wl_card_refused(6);
        }
        else
        {
                positive base = compositor_wl_source_import(row, picture);
                if (!base)
                        return compositor_wl_card_refused(6);
                src_va = picture->pixels && picture->map ? base + (positive)(src - picture->map)
                                                         : base + (positive)picture->plane_offset[0] + (positive)sy * stride + (positive)sx * 4;
        }
        if (!(dst_va = wl_gpu_va_written(to, compositor.buffer)))
                return compositor_wl_card_refused(6);
#ifdef MOONWATER_GPU_C
        if (picture->last && !picture->blend && picture->sync_fd >= 0)
        {
                if (compositor_wl.tiled_seq && compositor_wl.tiled_row != row)
                        wl_ext_tiled_settle();
                positive sequence = gpu_blit_go(src_va, (unsigned int)(stride / 4), dst_va + 4 * ((positive)dy * pitch + (positive)dx),
                                                (unsigned int)pitch, 0, 0, (unsigned int)w, (unsigned int)h);
                if (sequence)
                        return compositor_wl.tiled_seq = sequence, compositor_wl.tiled_row = row, compositor_wl.tiled_lent = 1, compositor_wl.tiled_unwaited++, 1;
                if (gpu_lost)
                        return compositor_wl_card_refused(7);
        }
        if (!gpu_idle() && !gpu_finish())
                return compositor_wl_card_refused(2);
#endif
        if (gpu_blit(src_va, (unsigned int)(stride / 4), dst_va + 4 * ((positive)dy * pitch + (positive)dx), (unsigned int)pitch, 0, 0, (unsigned int)w, (unsigned int)h))
                return 1;
#ifdef MOONWATER_GPU_C
        if (gpu_end == GPU_END_LATE && gpu_finish())
                return 1;
        if (gpu_end != GPU_END_NONE)
                compositor_gpu_job_failed();
#endif
        return compositor_wl_card_refused(7);
}

static void compositor_wl_lent_damage(struct windows *table, int row, const struct wayland_picture *picture, long width, long height)
{
        long x1 = width, y1 = height, x2 = 0, y2 = 0;

        table->damage_x[row] = table->damage_y[row] = table->damage_width[row] = table->damage_height[row] = 0;
        compositor_wl.parts[row].count = 0;
        if (picture->full || picture->damages <= 0 || picture->damages > WAYLAND_DAMAGE)
                return;
        for (long at = 0; at < picture->damages; at++)
        {
                long left = picture->damage[at].x + picture->dest_x, top = picture->damage[at].y + picture->dest_y;
                long right = left + picture->damage[at].width, bottom = top + picture->damage[at].height;
                if (left < 0) left = 0;
                if (top < 0) top = 0;
                if (right > width) right = width;
                if (bottom > height) bottom = height;
                if (right <= left || bottom <= top)
                        continue;
                compositor_wl_part(row, left, top, right, bottom);
                if (left < x1) x1 = left;
                if (top < y1) y1 = top;
                if (right > x2) x2 = right;
                if (bottom > y2) y2 = bottom;
        }
        if (x2 > x1 && y2 > y1)
                table->damage_x[row] = (int)x1, table->damage_y[row] = (int)y1, table->damage_width[row] = (int)(x2 - x1), table->damage_height[row] = (int)(y2 - y1);
}

static int compositor_wl_gatherable(const struct wayland_picture *picture, long *tw, long *th)
{
        unsigned int fourcc = picture->fourcc;
        long turned_w = picture->transform & 1 ? picture->height : picture->width, turned_h = picture->transform & 1 ? picture->width : picture->height;

        if ((fourcc != WAYLAND_FOURCC_ARGB8888 && fourcc != WAYLAND_FOURCC_XRGB8888 && fourcc != WAYLAND_FOURCC_ABGR8888 && fourcc != WAYLAND_FOURCC_XBGR8888) ||
            (picture->format != WLN_wl_shm_format_argb8888 && picture->format != WLN_wl_shm_format_xrgb8888 && (unsigned long)picture->format != fourcc) ||
            picture->plane_count > 1 || (picture->modifier && picture->modifier != WAYLAND_MODIFIER_INVALID) || picture->transform < 0 || picture->transform > 7)
                return 0;
        if (picture->src_width > 0 && (picture->src_height <= 0 || picture->src_x < 0 || picture->src_y < 0 || picture->src_width > WAYLAND_DIMENSION * 256 ||
                                       picture->src_height > WAYLAND_DIMENSION * 256 || picture->src_x > WAYLAND_DIMENSION * 256 || picture->src_y > WAYLAND_DIMENSION * 256))
                return 0;
        *tw = picture->dst_width > 0 ? picture->dst_width : turned_w;
        *th = picture->dst_height > 0 ? picture->dst_height : turned_h;
        return *tw > 0 && *th > 0 && *tw <= WAYLAND_DIMENSION && *th <= WAYLAND_DIMENSION;
}

static int compositor_wl_layer_nv12(const struct wayland_picture *picture, long width, long height, int radius, int line, struct compositor_wl_layer *old,
                                    unsigned int olds, struct compositor_wl_layer *out)
{
        unsigned long dev = 0, ino = 0, plane_dev = 0, plane_ino = 0, luma_end, chroma_end, extent;
        long tw, th, x1, y1, x2, y2, size;

        if (picture->sync_fd < 0 || !compositor_wl_card_composes() || !gpu_ready() ||
            picture->plane_count != 2 || picture->plane_fd[1] < 0 || (picture->modifier && picture->modifier != WAYLAND_MODIFIER_INVALID) ||
            (picture->transform & 1) || picture->transform < 0 || picture->transform > 7 || picture->width < 2 || picture->height < 2 ||
            picture->width > WAYLAND_DIMENSION || picture->height > WAYLAND_DIMENSION || picture->stride < picture->width ||
            picture->plane_stride[1] < (unsigned long)(picture->width + (picture->width & 1)) || picture->stride >= 65536 || picture->plane_stride[1] >= 65536)
                return 0;
        if (picture->src_width > 0 && (picture->src_height <= 0 || picture->src_x < 0 || picture->src_y < 0 || picture->src_width > WAYLAND_DIMENSION * 256 ||
                                       picture->src_height > WAYLAND_DIMENSION * 256 || picture->src_x > WAYLAND_DIMENSION * 256 || picture->src_y > WAYLAND_DIMENSION * 256))
                return 0;
        canvas_ext_fd_ident(picture->sync_fd, &dev, &ino);
        canvas_ext_fd_ident(picture->plane_fd[1], &plane_dev, &plane_ino);
        if (!ino || plane_dev != dev || plane_ino != ino)
                return 0;
        luma_end = picture->plane_offset[0] + (unsigned long)picture->stride * (unsigned long)picture->height;
        chroma_end = picture->plane_offset[1] + picture->plane_stride[1] * (unsigned long)((picture->height + 1) / 2);
        extent = luma_end > chroma_end ? luma_end : chroma_end;
        size = compositor_call(syscall(lseek), (positive)picture->sync_fd, 0, 2, 0, 0, 0);
        if (size <= 0 || extent > (unsigned long)size)
                return 0;
        tw = picture->dst_width > 0 ? picture->dst_width : picture->src_width > 0 ? picture->src_width / 256 : picture->width;
        th = picture->dst_height > 0 ? picture->dst_height : picture->src_width > 0 ? picture->src_height / 256 : picture->height;
        if (tw <= 0 || th <= 0 || tw > WAYLAND_DIMENSION || th > WAYLAND_DIMENSION || picture->dest_x < -WAYLAND_DIMENSION ||
            picture->dest_x > WAYLAND_DIMENSION || picture->dest_y < -WAYLAND_DIMENSION || picture->dest_y > WAYLAND_DIMENSION)
                return 0;
        x1 = picture->dest_x < 0 ? 0 : picture->dest_x, y1 = picture->dest_y < 0 ? 0 : picture->dest_y;
        x2 = picture->dest_x + tw > width ? width : picture->dest_x + tw, y2 = picture->dest_y + th > height ? height : picture->dest_y + th;
        out->id = (unsigned int)picture->layer;
        if (x2 <= x1 || y2 <= y1)
                return 1;
        if (compositor_wl_meets_corner((int)x1, (int)y1, (int)x2, (int)y2, (int)width, (int)height, radius, line) ||
            !(out->import = compositor_gpu_import_layer(old, olds, picture->map, picture->sync_fd, picture->map_bytes, dev, ino, 1, extent)))
                return 0;
        out->va = out->import, out->pixels = (const unsigned int *)picture->pixels, out->pitch = 1;
        out->x = (int)x1, out->y = (int)y1, out->width = (int)(x2 - x1), out->height = (int)(y2 - y1);
        out->map = picture->map, out->fd = picture->sync_fd, out->size = picture->map_bytes, out->dev = dev, out->ino = ino;
        out->modifier = picture->modifier, out->epoch = gpu_epoch, out->fourcc = picture->fourcc, out->at = WAYLAND_LAYERS;
        out->nv12 = 1, out->luma = picture->plane_offset[0], out->chroma = picture->plane_offset[1];
        out->luma_pitch = (unsigned long)picture->stride, out->chroma_pitch = picture->plane_stride[1], out->extent = extent;
        compositor_wl_gather_set(out, picture, tw, th, x1, y1);
        return 1;
}

static int compositor_wl_layer_make(const struct wayland_picture *picture, long width, long height, int radius, int line,
                                    struct compositor_wl_layer *old, unsigned int olds, struct compositor_wl_layer *out, unsigned int *why)
{
        long x1, y1, x2, y2, tw = picture->width, th = picture->height;
        unsigned long stride = (unsigned long)picture->stride, dev = 0, ino = 0, bytes;
        const unsigned char *at;
        volatile unsigned int probe;
        int gather;

        memory_zero(out, sizeof *out);
        out->fd = -1, out->id = (unsigned int)picture->layer, *why = 9;
        if (picture->layer <= 0 || !picture->pixels || !picture->map)
                return 0;
        if (picture->fragile)
                return *why = 7, 0;
        *why = 6;
        if (picture->fourcc == WAYLAND_FOURCC_NV12)
                return compositor_wl_layer_nv12(picture, width, height, radius, line, old, olds, out);
        gather = compositor_wl_needs_xform_ordered(picture, picture->width, picture->height, 1);
        if (gather && !compositor_wl_gatherable(picture, &tw, &th))
                return 0;
        if (gather && picture->sync_fd >= 0 && !compositor_wl_card_composes())
                return *why = 5, 0;
        *why = 9;
        if (picture->width <= 0 || picture->height <= 0 || picture->width > WAYLAND_DIMENSION || picture->height > WAYLAND_DIMENSION || (stride & 3) ||
            stride < (unsigned long)picture->width * 4 || picture->dest_x < -WAYLAND_DIMENSION || picture->dest_x > WAYLAND_DIMENSION ||
            picture->dest_y < -WAYLAND_DIMENSION || picture->dest_y > WAYLAND_DIMENSION)
                return 0;
        x1 = picture->dest_x < 0 ? 0 : picture->dest_x, y1 = picture->dest_y < 0 ? 0 : picture->dest_y;
        x2 = picture->dest_x + tw > width ? width : picture->dest_x + tw, y2 = picture->dest_y + th > height ? height : picture->dest_y + th;
        if (x2 <= x1 || y2 <= y1)
                return 1;
        if (compositor_wl_meets_corner((int)x1, (int)y1, (int)x2, (int)y2, (int)width, (int)height, radius, line))
                return *why = 8, 0;
        at = gather ? picture->pixels : picture->pixels + (unsigned long)(y1 - picture->dest_y) * stride + (unsigned long)(x1 - picture->dest_x) * 4;
        bytes = gather ? (unsigned long)(picture->height - 1) * stride + (unsigned long)picture->width * 4
                       : (unsigned long)(y2 - y1 - 1) * stride + (unsigned long)(x2 - x1) * 4;
        if (at < picture->map || (unsigned long)(at - picture->map) + bytes > (unsigned long)picture->map_bytes)
                return 0;
        probe = *(const volatile unsigned int *)at;
        probe = probe + *(const volatile unsigned int *)(at + bytes - 4);
        (void)probe;
        if (compositor_wl_cut(picture->pixels))
                return 0;
#ifdef MOONWATER_GPU_C
        if (picture->sync_fd < 0 && compositor_gpu && compositor_gpu_state.up && !compositor_gpu_state.dead && !wl_gpu_va_read(at, (positive)bytes))
                return 0;
#endif
        compositor_wl_pic_ident(picture, &dev, &ino);
        if (picture->sync_fd >= 0)
        {
                if (!gpu_ready() || !(out->import = compositor_gpu_import_layer(old, olds, picture->map, picture->sync_fd, picture->map_bytes, dev, ino, 0, 0)))
                        return 0;
                out->va = out->import + (positive)(at - picture->map);
        }
        out->pixels = (const unsigned int *)at, out->pitch = stride / 4;
        out->x = (int)x1, out->y = (int)y1, out->width = (int)(x2 - x1), out->height = (int)(y2 - y1), out->blend = picture->blend != 0;
        out->map = picture->map, out->fd = picture->sync_fd, out->size = picture->map_bytes, out->dev = dev, out->ino = ino;
        out->modifier = picture->modifier, out->epoch = gpu_epoch, out->fourcc = picture->fourcc;
        out->offset = (unsigned int)(picture->plane_offset[0] + (unsigned long)(at - picture->pixels));
        out->swap = compositor_wl_fourcc_swapped(picture->fourcc), out->at = WAYLAND_LAYERS;
        if (gather)
                compositor_wl_gather_set(out, picture, tw, th, x1, y1);
        return 1;
}

static void compositor_wl_change(int rects[][4], int *count, int room, int box[4], int x1, int y1, int x2, int y2)
{
        if (x2 <= x1 || y2 <= y1)
                return;
        if (*count >= 0 && *count < room)
                rects[*count][0] = x1, rects[*count][1] = y1, rects[*count][2] = x2, rects[*count][3] = y2, (*count)++;
        else
                *count = -1;
        if (box[2] <= box[0])
                box[0] = x1, box[1] = y1, box[2] = x2, box[3] = y2;
        else
                box[0] = x1 < box[0] ? x1 : box[0], box[1] = y1 < box[1] ? y1 : box[1],
                box[2] = x2 > box[2] ? x2 : box[2], box[3] = y2 > box[3] ? y2 : box[3];
}

static void compositor_wl_layers_changes(const struct compositor_wl_layer *old, unsigned int olds, const struct compositor_wl_layer *now,
                                         const struct wayland_picture *const *from, unsigned int nows, int rects[][4], int *count, int room, int box[4])
{
        int restacked = 0, last = -1;
        unsigned char matched[WAYLAND_LAYERS] = {0};

        for (unsigned int at = 0; at < nows; at++)
                for (unsigned int k = 0; k < olds; k++)
                        if (old[k].id == now[at].id)
                                restacked |= (int)k < last, last = (int)k;
        for (unsigned int at = 0; at < nows; at++)
        {
                const struct compositor_wl_layer *l = &now[at];
                const struct wayland_picture *p = from[at];
                int was = -1;
                for (unsigned int k = 0; k < olds && was < 0; k++)
                        if (old[k].id == l->id)
                                was = (int)k;
                if (was >= 0)
                        matched[was] = 1;
                if (was < 0 || restacked || old[was].x != l->x || old[was].y != l->y || old[was].width != l->width || old[was].height != l->height ||
                    old[was].blend != l->blend || old[was].swap != l->swap)
                {
                        compositor_wl_change(rects, count, room, box, l->x, l->y, l->x + l->width, l->y + l->height);
                        if (was >= 0)
                                compositor_wl_change(rects, count, room, box, old[was].x, old[was].y, old[was].x + old[was].width, old[was].y + old[was].height);
                }
                else if (p->changed && !l->gather && !p->full && p->damages > 0 && p->damages <= WAYLAND_DAMAGE)
                        for (long k = 0; k < p->damages; k++)
                        {
                                long x1 = p->damage[k].x + p->dest_x, y1 = p->damage[k].y + p->dest_y;
                                long x2 = x1 + p->damage[k].width, y2 = y1 + p->damage[k].height;
                                x1 = x1 < l->x ? l->x : x1, y1 = y1 < l->y ? l->y : y1;
                                x2 = x2 > l->x + l->width ? l->x + l->width : x2, y2 = y2 > l->y + l->height ? l->y + l->height : y2;
                                compositor_wl_change(rects, count, room, box, (int)x1, (int)y1, (int)x2, (int)y2);
                        }
                else if (p->changed || old[was].pixels != l->pixels || old[was].gather != l->gather ||
                         (l->gather && !compositor_wl_gather_same(&old[was], l)))
                        compositor_wl_change(rects, count, room, box, l->x, l->y, l->x + l->width, l->y + l->height);
        }
        for (unsigned int k = 0; k < olds; k++)
                if (!matched[k])
                        compositor_wl_change(rects, count, room, box, old[k].x, old[k].y, old[k].x + old[k].width, old[k].y + old[k].height);
}

static int compositor_wl_row_layers(void *context, int row, const struct wayland_picture *layers, long count, long root)
{
        struct windows *table = compositor.table;
        struct compositor_wl_layer made[WAYLAND_LAYERS], *old;
        const struct wayland_picture *from[WAYLAND_LAYERS];
        struct compositor_frame corners;
        unsigned int kept = 0, olds, why = 0;
        long width, height;
        int ok = 1, changes = 0, box[4] = {0, 0, 0, 0}, rects[WAYLAND_DAMAGE][4];

        (void)context;
        if (row < 0 || row >= COMPOSITOR_ROWS || !table)
                return 0;
        if (count < 0 || count > WAYLAND_LAYERS || (count && !layers) || compositor.landing[row] || compositor.glassy[row] || compositor_wl_layers.off)
                return compositor_wl_layers_lay(row), compositor_wl_layers_refused(count < 0 ? 0 : 1);
        if (!root && !compositor_lent(row))
        {
                compositor_wl_layers_lay(row);
                if (compositor_wl_layers.root_refused[row])
                        return compositor_wl_layers_refused(3);
                return compositor_wl_layers_refused(2), WAYLAND_LAYERS_ROOT;
        }
        width = root && count ? layers[0].row_width : (long)table->width[row];
        height = root && count ? layers[0].row_height : (long)table->height[row];
        width = width < 0 ? 0 : width > (long)compositor_most_width() ? (long)compositor_most_width() : width;
        height = height < 0 ? 0 : height > (long)compositor_most_height() ? (long)compositor_most_height() : height;
        memory_zero(&corners, sizeof corners);
        corners.width = (int)width, corners.height = (int)height;
        corners.radius = compositor_rounded(row) ? UI_RADIUS * compositor_row_scale(row) / 100 : 0, corners.line = compositor_frame_line(row);
        old = compositor_wl_layers.layer[row], olds = compositor_wl_layers.count[row];
        for (long at = 0; at < count && ok; at++)
        {
                if (layers[at].row_width != (root ? layers[0].row_width : (long)table->width[row]) ||
                    layers[at].row_height != (root ? layers[0].row_height : (long)table->height[row]))
                        ok = 0, why = 4;
                else if (layers[at].sync_fd >= 0 && (compositor_wl.wc_copy[row] || compositor_wl_card_barred(row)))
                        ok = 0, why = 5;
                else if (!compositor_wl_layer_make(&layers[at], width, height, compositor_radius(&corners), corners.line, old, olds, &made[kept], &why))
                        ok = 0;
                else if (made[kept].width > 0)
                        from[kept++] = &layers[at];
        }
        if (ok && compositor_wl_layers.total - olds + kept > COMPOSITOR_LAYERS)
                ok = 0, why = 10;
        if (!ok)
        {
                for (unsigned int at = 0; at < kept; at++)
                        if (canvas_ext_import_live(made[at].import, made[at].epoch))
                                gpu_release_import(made[at].import);
                return compositor_wl_layers_lay(row), compositor_wl_layers_refused(why);
        }
        compositor_wl_layers_changes(old, olds, made, from, kept, rects, &changes, WAYLAND_DAMAGE, box);
        compositor_wl_gathers_take(row, made, kept, from, old, olds, root);
        for (unsigned int at = 0; at < olds; at++)
                if (canvas_ext_import_live(old[at].import, old[at].epoch))
                        gpu_release_import(old[at].import);
        memory_copy(old, made, (positive)kept * sizeof made[0]);
        compositor_wl_layers.total = compositor_wl_layers.total - olds + kept;
        compositor_wl_layers.count[row] = (unsigned char)kept;
        for (unsigned int at = 0; at < kept; at++)
                table->client_in_place += from[at]->changed != 0;
        if (root)
        {
                compositor_wl_layers.pending_row = row, compositor_wl_layers.pending_count = changes;
                memory_copy(compositor_wl_layers.pending_box, box, sizeof box);
                if (changes > 0)
                        memory_copy(compositor_wl_layers.pending, rects, (positive)changes * sizeof rects[0]);
                return WAYLAND_LAYERS_KEPT;
        }
        if (box[2] <= box[0])
                return WAYLAND_LAYERS_KEPT;
        compositor_wl.parts[row].count = 0;
        for (int at = 0; at < changes; at++)
                compositor_wl_part(row, rects[at][0], rects[at][1], rects[at][2], rects[at][3]);
        if (changes < 0)
                compositor_wl.parts[row].count = -1;
        table->damage_x[row] = box[0], table->damage_y[row] = box[1];
        table->damage_width[row] = box[2] - box[0], table->damage_height[row] = box[3] - box[1];
        __atomic_store_n(&table->frame[row], table->frame[row] + 1, __ATOMIC_RELEASE);
        compositor_wl_take(row);
        return WAYLAND_LAYERS_KEPT;
}

static void compositor_wl_layers_pending(struct windows *table, int row)
{
        if (compositor_wl_layers.pending_row != row)
                return;
        compositor_wl_layers.pending_row = -1;
        if (compositor_wl_layers.pending_box[2] <= compositor_wl_layers.pending_box[0] || table->damage_width[row] <= 0)
                return;
        int x1 = table->damage_x[row], y1 = table->damage_y[row], x2 = x1 + table->damage_width[row], y2 = y1 + table->damage_height[row];
        const int *b = compositor_wl_layers.pending_box;
        if (compositor_wl.parts[row].count == 0)
                compositor_wl_part(row, x1, y1, x2, y2);
        if (compositor_wl_layers.pending_count < 0)
                compositor_wl_part(row, b[0], b[1], b[2], b[3]);
        for (int at = 0; at < compositor_wl_layers.pending_count; at++)
                compositor_wl_part(row, compositor_wl_layers.pending[at][0], compositor_wl_layers.pending[at][1],
                                   compositor_wl_layers.pending[at][2], compositor_wl_layers.pending[at][3]);
        x1 = b[0] < x1 ? b[0] : x1, y1 = b[1] < y1 ? b[1] : y1, x2 = b[2] > x2 ? b[2] : x2, y2 = b[3] > y2 ? b[3] : y2;
        table->damage_x[row] = x1, table->damage_y[row] = y1, table->damage_width[row] = x2 - x1, table->damage_height[row] = y2 - y1;
}

static unsigned int wl_ext_layers_frames(int row, struct compositor_frame *frames, unsigned int *rows, unsigned int at)
{
        struct compositor_frame *window = &frames[at - 1];
        unsigned int count = row >= 0 && row < COMPOSITOR_ROWS ? compositor_wl_layers.count[row] : 0;
        int fit = count && wl_row_lent(row) && !compositor_scaled(window) && window->pixels == compositor_wl.lent[row].pixels &&
                  window->width == compositor_wl.lent[row].width && window->height == compositor_wl.lent[row].height;

        for (unsigned int k = 0; fit && k < count; k++)
        {
                const struct compositor_wl_layer *l = &compositor_wl_layers.layer[row][k];
                if (l->gather && (l->at >= WAYLAND_LAYERS || !compositor_wl_layers.gathered[row][l->at].filled ||
                                  compositor_wl_layers.gathered[row][l->at].id != l->id))
                        fit = 0;
                fit = fit && !compositor_wl_meets_corner(l->x, l->y, l->x + l->width, l->y + l->height, window->width, window->height,
                                                         compositor_radius(window), window->builtin || window->line < 0 ? 0 : window->line);
        }
        if (!fit)
        {
                if (count)
                {
                        compositor_wl_layers_refused(11);
                        compositor_wl_layers_lay(row);
                        window->pixels = compositor_pixels(row), window->pitch = (unsigned long)compositor_stride(), window->gpu_base = 0;
                }
                return 0;
        }
        for (unsigned int k = 0; k < count; k++)
        {
                const struct compositor_wl_layer *l = &compositor_wl_layers.layer[row][k];
                struct compositor_frame *f = &frames[at + k];
                memory_zero(f, sizeof *f);
                f->x = window->x + l->x, f->y = window->y + l->y, f->width = l->width, f->height = l->height, f->scale = window->scale;
                f->pixels = l->pixels, f->pitch = l->pitch, f->gpu_base = canvas_ext_import_live(l->va, l->epoch);
                if (l->gather)
                {
                        const struct compositor_wl_gathered *g = &compositor_wl_layers.gathered[row][l->at];
                        f->pixels = g->memory, f->pitch = (unsigned long)l->width, f->gpu_base = canvas_ext_import_live(g->va, g->epoch);
                }
                f->blend = l->blend, f->swap = l->swap, f->layer = 1, rows[at + k] = (unsigned int)row;
        }
        return count;
}

static inline unsigned int compositor_wl_resolve_fourcc(const struct wayland_picture *picture)
{
        if (picture->fourcc)
                return picture->fourcc;
        return picture->format == WLN_wl_shm_format_argb8888   ? WAYLAND_FOURCC_ARGB8888
               : picture->format == WLN_wl_shm_format_xrgb8888 ? WAYLAND_FOURCC_XRGB8888
                                                               : (unsigned int)picture->format;
}

/*
        A picture that is not one as it is (turned, flipped, cropped at an offset or scaled: compositor_wl_needs_xform) laid into the table's picture by
        the card, where the processor walks it (compositor_wl_blit_xform): the part of it in the window as a gathered layer would read it (its tables,
        compositor_wl_gather_column and _line, made into memory of the compositor's own the card reads, compositor_wl_tables), op 7 over them with a
        source pitch of 1, from the client's buffer (a pool's pages through a read map, a dma-buf through the row's import, compositor_wl.source) into
        the picture through its written map. One whose red and blue are the other way round is gathered aside, after the tables, and laid from
        there by a scene of one record with the swap bit, as a lent picture is read. gpu_blit and gpu_scene wait for their jobs, so the picture is
        laid when this answers 1. Refused (0, and the processor walks it): the card not composing or busy, a blend (op 7 copies), a picture the
        card cannot gather (compositor_wl_gatherable), a pool cut under the read, a map or an import refused, or a job that failed.
*/
static struct compositor_wl_gathered compositor_wl_tables;

static int compositor_wl_card_gather(int row, const struct wayland_picture *picture, unsigned int *to, unsigned long pitch, long width, long height)
{
#ifdef MOONWATER_GPU_C
        struct compositor_wl_layer l;
        unsigned long stride = (unsigned long)picture->stride;
        positive src = 0, dst, extent, tables;
        long tw, th, x1, y1, x2, y2;
        unsigned int *cols, *lines;
        volatile unsigned int probe;

        int swap = compositor_wl_fourcc_swapped(picture->fourcc);
        positive aside = 0;

        // Red and blue the other way round are gathered aside and laid by a scene's record that turns them (op 7 moves the bytes as they are, and
        // the table holds ARGB), which reads the records a present's jobs may still read (compositor_card_holds).
        if (!compositor_wl_card_composes() || picture->blend || (swap && compositor_card_holds()) || picture->width <= 0 || picture->height <= 0 || (stride & 3) ||
            stride < (unsigned long)picture->width * 4 || !compositor_wl_gatherable(picture, &tw, &th) || !gpu_ready() || !gpu_idle())
                return 0;
        x1 = picture->dest_x < 0 ? 0 : picture->dest_x, y1 = picture->dest_y < 0 ? 0 : picture->dest_y;
        x2 = picture->dest_x + tw > width ? width : picture->dest_x + tw, y2 = picture->dest_y + th > height ? height : picture->dest_y + th;
        if (x2 <= x1 || y2 <= y1)
                return 0;
        memory_zero(&l, sizeof l);
        l.pixels = (const unsigned int *)picture->pixels, l.pitch = stride / 4;
        compositor_wl_gather_set(&l, picture, tw, th, x1, y1);
        l.width = (int)(x2 - x1), l.height = (int)(y2 - y1);
        extent = compositor_wl_layer_bytes(&l);
        if (picture->sync_fd < 0)
        {
                if (!picture->pixels || !picture->map || picture->pixels < picture->map ||
                    (unsigned long)(picture->pixels - picture->map) + extent > (unsigned long)picture->map_bytes)
                        return 0;
                probe = *(const volatile unsigned int *)picture->pixels;
                probe = probe + *(const volatile unsigned int *)(picture->pixels + extent - 4);
                (void)probe;
                if (compositor_wl_cut(picture->pixels) || !(src = wl_gpu_va_read(picture->pixels, extent)))
                        return 0;
        }
        else
        {
                positive base = compositor_wl_source_import(row, picture);
                if (!base)
                        return 0;
                src = base + (picture->pixels && picture->map ? (positive)(picture->pixels - picture->map) : (positive)picture->plane_offset[0]);
        }
        aside = (4 * ((positive)l.width + (positive)l.height) + 15) & ~15ull;
        if (!compositor_wl_gathered_room(&compositor_wl_tables, aside + (swap ? 4 * (((positive)l.width + 3) & ~3ull) * (positive)l.height : 0)))
                return 0;
        cols = compositor_wl_tables.memory, lines = cols + l.width;
        for (long x = 0; x < l.width; x++)
                cols[x] = compositor_wl_gather_column(&l, x);
        for (long y = 0; y < l.height; y++)
                lines[y] = compositor_wl_gather_line(&l, y);
        if (!(tables = wl_gpu_va_written(compositor_wl_tables.memory, compositor_wl_tables.bytes)) ||
            !(dst = wl_gpu_va_written(to, compositor.buffer)))
                return 0;
        if (swap)
        {
                unsigned int *op = (unsigned int *)((unsigned char *)gpu.at + COMPOSITOR_GPU_OPS_AT), w4 = ((unsigned int)width + 3) & ~3u;
                positive at = tables + aside;
                if (w4 > pitch || (pitch & 3) || (dst & 15))
                        return 0;
                gpu_handed_hold = 1;
                if (gpu_blit(src, 1, at, ((unsigned int)l.width + 3) & ~3u, tables, 7, (unsigned int)l.width, (unsigned int)l.height) ||
                    (gpu_end == GPU_END_LATE && gpu_finish()))
                {
                        gpu_handed_hold = 0;
                        op[0] = 4u | 0x80u, op[1] = (unsigned int)x1, op[2] = (unsigned int)y1, op[3] = (unsigned int)x2, op[4] = (unsigned int)y2;
                        op[5] = (unsigned int)at, op[6] = (unsigned int)(at >> 32), op[7] = ((unsigned int)l.width + 3) & ~3u;
                        if (gpu_scene(COMPOSITOR_GPU_OPS_AT, 1, COMPOSITOR_GPU_BINS_AT, dst, (unsigned int)pitch, w4, (unsigned int)height, (unsigned int)x1,
                                      (unsigned int)y1, (unsigned int)x2, (unsigned int)y2))
                                return compositor_wl_laid(row, x1, y1, x2, y2), 1;
                        if (gpu.pending)
                                gpu_finish();
                        if (gpu_lost)
                                compositor_gpu_recover();
                }
                gpu_handed_hold = 0;
        }
        else if (gpu_blit(src, 1, dst + 4 * ((positive)y1 * pitch + (positive)x1), (unsigned int)pitch, tables, 7, (unsigned int)l.width, (unsigned int)l.height) ||
                 (gpu_end == GPU_END_LATE && gpu_finish()))
                return compositor_wl_laid(row, x1, y1, x2, y2), 1;
        if (gpu_end != GPU_END_NONE)
                compositor_gpu_job_failed();
        if (!gpu_idle())
                gpu_finish();
        return 0;
#else
        (void)row, (void)picture, (void)to, (void)pitch, (void)width, (void)height;
        return 0;
#endif
}

static int compositor_wl_dmabuf_holds(long fd, unsigned long size);

static int compositor_wl_blit_xform(int row, const struct wayland_picture *picture, unsigned int *to, unsigned long pitch,
                                    long width, long height)
{
        long bw = picture->width, bh = picture->height, uv_map_got = 0;
        unsigned int fourcc = compositor_wl_resolve_fourcc(picture);
        const unsigned char *uv_base = 0;
        unsigned long uv_stride = picture->plane_stride[1], uv_map_size = 0;
        int rects[WAYLAND_DAMAGE][4], out_box[4] = {0, 0, 0, 0}, nrects = 0, ok;
        volatile unsigned char probe;
        struct canvas_ext_blit b;

        if (!picture->pixels || bw <= 0 || bh <= 0)
                return 0;
        probe = picture->pixels[0];
        probe = (unsigned char)(probe + picture->pixels[(unsigned long)(bh - 1) * (unsigned long)picture->stride]);
        (void)probe;
        if (compositor_wl_cut(picture->pixels))
                return -1;
        if (fourcc == WAYLAND_FOURCC_NV12)
        {
                long uv_fd = -1;
                if (uv_stride < (unsigned long)(bw + (bw & 1)))
                        return -1;
                uv_map_size = picture->plane_offset[1] + uv_stride * (unsigned long)((bh + 1) / 2);
                if (picture->plane_fd[1] >= 0 && picture->plane_fd[1] != picture->sync_fd)
                {
                        if (!compositor_wl_dmabuf_holds(picture->plane_fd[1], uv_map_size))
                                return -1;
                        uv_fd = picture->plane_fd[1];
                }
                else if (picture->map && (unsigned long)picture->map_bytes >= uv_map_size)
                        uv_base = picture->map + picture->plane_offset[1];
                else if (picture->sync_fd >= 0 && compositor_wl_dmabuf_holds(picture->sync_fd, uv_map_size))
                        uv_fd = picture->sync_fd;
                if (uv_fd >= 0)
                {
                        long got = compositor_call(syscall(mmap), 0, (positive)uv_map_size, 1, 1, (positive)uv_fd, 0);
                        if ((got >= 0 || got <= -4096) && compositor_wl_uv_noted(got, uv_map_size))
                                uv_map_got = got, uv_base = (const unsigned char *)got + picture->plane_offset[1];
                }
        }
        if (!picture->full && picture->damages > 0)
        {
                nrects = picture->damages > WAYLAND_DAMAGE ? WAYLAND_DAMAGE : (int)picture->damages;
                for (int i = 0; i < nrects; i++)
                        rects[i][0] = (int)picture->damage[i].x, rects[i][1] = (int)picture->damage[i].y,
                        rects[i][2] = (int)picture->damage[i].width, rects[i][3] = (int)picture->damage[i].height;
        }
        b = (struct canvas_ext_blit){
                .pixels = picture->pixels, .uv_base = uv_base, .stride = (unsigned long)picture->stride, .uv_stride = uv_stride,
                .width = bw, .height = bh, .fourcc = fourcc, .transform = (int)picture->transform, .y_invert = picture->y_invert != 0,
                .blend = picture->blend != 0, .streaming = picture->sync_fd >= 0,
                .src_x = picture->src_x, .src_y = picture->src_y, .src_w = picture->src_width, .src_h = picture->src_height,
                .dst_w = picture->dst_width, .dst_h = picture->dst_height, .dest_x = picture->dest_x, .dest_y = picture->dest_y,
                .to = to, .to_pitch = pitch, .to_w = width, .to_h = height,
                .rects = nrects ? (const int (*)[4])rects : 0, .rect_count = nrects,
        };
        ok = canvas_ext_xform_blit(&b, out_box);
        if (uv_map_got)
                compositor_wl_map_forget((unsigned long)uv_map_got), compositor_call(syscall(munmap), (positive)uv_map_got, (positive)uv_map_size, 0, 0, 0, 0);
        if (compositor_wl_cut(picture->pixels))
                return -1;
        if (ok && out_box[2] > out_box[0] && out_box[3] > out_box[1])
                compositor_wl_laid(row, out_box[0], out_box[1], out_box[2], out_box[3]);
        return ok;
}

static void compositor_wl_table_copy(int row, unsigned int into, unsigned int from, unsigned long cw, unsigned long ch)
{
        unsigned long pitch = (unsigned long)compositor_stride();
        unsigned int *to;
        const unsigned int *source;

        wl_ext_tiled_settle();
        to = (unsigned int *)((char *)compositor.table + compositor.pixels + ((unsigned long)row * 2 + (into & 1u)) * compositor.buffer);
        source = (const unsigned int *)((char *)compositor.table + compositor.pixels + ((unsigned long)row * 2 + (from & 1u)) * compositor.buffer);
        if (!cw || !ch)
                return;
        // Both table slots are cached WB system RAM: lib.c SIMD copy is ~6x faster than a synchronous GPU blit (54 us vs 330 us on box).
        compositor.table->client_rects_cpu++, compositor_wl_copied(row, WINDOWS_COPY_CPU);
        memory_copy_rect_u32(to, pitch, source, pitch, cw, ch);
}

static int wl_pic_sync_begin(int row, const struct wayland_picture *picture, int *sync_started)
{
        if (picture->sync_fd >= 0 && !*sync_started)
        {
                // Always issue DMA_BUF_SYNC_START|READ before CPU mmap reads (explicit sync only covers GPU fence completion,
                // not CPU cache invalidation on Intel i915/non-coherent DMA-BUFs). ready=1 never sleeps or arms setitimer.
                if (!compositor_wl_sync_start(wayland_row_owner(row), (int)picture->sync_fd, 1))
                        return compositor_wl.laying[row].open = 0, 0;
                *sync_started = 1;
        }
        return 1;
}

static int wl_pic_commit_lent(struct windows *table, int row, const struct wayland_picture *picture, long width, long height, int whole, int with_layers)
{
        if (whole || width != (long)table->width[row] || height != (long)table->height[row])
                table->damage_x[row] = table->damage_y[row] = table->damage_width[row] = table->damage_height[row] = 0, compositor_wl.parts[row].count = 0;
        else
                compositor_wl_lent_damage(table, row, picture, width, height);
        if (with_layers)
                compositor_wl_layers_pending(table, row), table->width[row] = (unsigned int)width, table->height[row] = (unsigned int)height;
        compositor_wl_layers.root_refused[row] = 0;
        table->client_in_place++;
        __atomic_store_n(&table->frame[row], table->frame[row] + 1, __ATOMIC_RELEASE);
        compositor_wl_take(row);
        return WAYLAND_PICTURE_LENT;
}

static int compositor_wl_row_picture(void *context, int row, const struct wayland_picture *picture)
{
        compositor_wl.tiled_lent = 0;
        struct windows *table = compositor.table;
        unsigned long pitch = (unsigned long)compositor_stride();
        long width = picture->row_width, height = picture->row_height;
        long count = picture->full || !picture->damages ? 1 : picture->damages;
        unsigned int *to, aside = 0;
        int was_lent, sync_started = 0, has_content = picture->pixels != 0 || picture->sync_fd >= 0;
        struct compositor_wl_layer over[WAYLAND_LAYERS];

        (void)context;
        if (row < 0 || row >= COMPOSITOR_ROWS || !table)
                return -1;
        width = width < 0 ? 0 : width > (long)compositor_most_width() ? (long)compositor_most_width() : width;
        height = height < 0 ? 0 : height > (long)compositor_most_height() ? (long)compositor_most_height() : height;
        if (picture->own_size && compositor.asking[row] && compositor.arranged[row] == COMPOSITOR_FLOATING && !compositor.fullscreen[row] &&
            compositor.resize != row && compositor.shared_row != row)
                compositor.asking[row] = 0;
        if (has_content)
                compositor_keytrace_commit(row);
        if (has_content && !compositor_wl.laying[row].open)
        {
                compositor_wl.row_blend[row] = (unsigned char)(picture->transparent != 0);
                compositor_wl.row_opaque[row] = (struct compositor_wl_opaque){(short)picture->opaque_x, (short)picture->opaque_y,
                                                                              (short)(picture->opaque_x + picture->opaque_w), (short)(picture->opaque_y + picture->opaque_h)};
        }
        if (has_content && !compositor_wl.laying[row].open && compositor_wl_straight_take(row, picture, width, height) == 2)
                return wl_pic_commit_lent(table, row, picture, width, height, 0, 0);
        was_lent = compositor_lent(row);
        if (picture->pixels && picture->last && !compositor_wl.laying[row].open && compositor_wl_lend(row, picture, width, height))
                return wl_pic_commit_lent(table, row, picture, width, height, !was_lent, 1);
        if (compositor_wl_layers.pending_row == row && !(picture->last && has_content))
                compositor_wl_layers.pending_row = -1;
        if (picture->sync_fd >= 0 && !picture->explicit_sync && !compositor_wl_dmabuf_ready(wayland_row_owner(row), (int)picture->sync_fd))
                return compositor_wl_layers.pending_row = -1, 1;
        if (compositor_wl_layers.pending_row == row)
        {
                aside = compositor_wl_layers.count[row];
                memory_copy(over, compositor_wl_layers.layer[row], (positive)aside * sizeof over[0]);
                for (unsigned int at = 0; at < aside; at++)
                        if (canvas_ext_import_live(over[at].import, over[at].epoch))
                                gpu_release_import(over[at].import);
                compositor_wl_layers.total -= aside, compositor_wl_layers.count[row] = 0;
                compositor_wl_layers.pending_row = -1, compositor_wl_layers.root_refused[row] = 1;
        }
        if (!compositor_wl.laying[row].open)
        {
                unsigned int shown = compositor.showing[row] ? compositor.front[row] & 1u : 0;
                int pinned = compositor.showing[row] && (table->pinned[row] || compositor.direct_row == row);
                long tw = picture->dst_width > 0 ? picture->dst_width : (picture->transform & 1 ? picture->height : picture->width);
                long th = picture->dst_height > 0 ? picture->dst_height : (picture->transform & 1 ? picture->width : picture->height);
                int covers = has_content && !picture->blend && !picture->dest_x && !picture->dest_y && tw >= width &&
                             th >= height && (picture->full || !picture->damages) && (picture->pixels || !compositor_wl_tiled(picture));

                compositor_wl.laying[row].open = 1, compositor_wl.laying[row].touched = 0, compositor_wl.parts[row].count = 0;
                compositor_wl.laying[row].full = (unsigned char)pinned;
                compositor_wl.laying[row].which = (unsigned char)(shown ^ (unsigned int)pinned);
                if (!compositor_lent(row) && !covers && compositor_wl.laying[row].last &&
                    (unsigned int)(compositor_wl.laying[row].last - 1) != compositor_wl.laying[row].which && table->width[row] && table->height[row])
                        compositor_wl_table_copy(row, compositor_wl.laying[row].which, (unsigned int)(compositor_wl.laying[row].last - 1) & 1u,
                                                 table->width[row] < (unsigned int)width ? table->width[row] : (unsigned long)width,
                                                 table->height[row] < (unsigned int)height ? table->height[row] : (unsigned long)height);
                else if (!compositor_lent(row) && pinned && !covers)
                        compositor_wl_table_copy(row, compositor_wl.laying[row].which, shown, (unsigned long)width, (unsigned long)height);
                if (compositor_lent(row))
                {
                        if (!covers)
                                compositor_wl_seed(row, compositor_wl.laying[row].which);
                        else
                                compositor_wl_unlend(row);
                        compositor_wl.laying[row].full = 1;
                }
        }
        to = (unsigned int *)((char *)table + compositor.pixels + ((unsigned long)row * 2 + compositor_wl.laying[row].which) * compositor.buffer);
        if (has_content)
        {
                if (picture->pixels && compositor_wl_needs_xform(picture, width, height))
                {
                        int xst;
                        if (!wl_pic_sync_begin(row, picture, &sync_started))
                        {
                                if (compositor_wl.tiled_lent)
                                        wl_ext_tiled_settle();
                                return 1;
                        }
                        xst = compositor_wl_card_gather(row, picture, to, pitch, width, height) ? 2 : (wl_ext_tiled_settle(), compositor_wl_blit_xform(row, picture, to, pitch, width, height));
                        if (sync_started)
                                canvas_dmabuf_sync_read(picture->sync_fd, 1);
                        if (xst < 0)
                        {
                                if (compositor_wl.tiled_lent)
                                        wl_ext_tiled_settle();
                                return compositor_wl.laying[row].open = 0, -1;
                        }
                        if (xst == 2)
                                table->client_rects_card++, compositor_wl_copied(row, WINDOWS_COPY_CARD);
                        else if (xst > 0)
                                table->client_rects_cpu++, compositor_wl_copied(row, WINDOWS_COPY_CPU);
                }
                else
                {
                        long summed = 0;
                        int card_wanted;
                        if (count > WAYLAND_DAMAGE)
                                count = WAYLAND_DAMAGE;
                        for (long at = 0; at < count; at++)
                                summed += picture->full || !picture->damages ? (long)picture->width * (long)picture->height
                                                                             : (long)picture->damage[at].width * (long)picture->damage[at].height;
                        // Only offload DMA-BUF copies to GPU; cached wl_shm (sync_fd < 0) copies ~6x faster on CPU via lib.c SIMD.
                        card_wanted = !picture->pixels || (picture->sync_fd >= 0 && summed >= COMPOSITOR_WL_CARD_COPY_PIXELS);
                        for (long at = 0; at < count; at++)
                        {
                                long left = 0, top = 0, right = picture->width, bottom = picture->height, x = picture->dest_x, y = picture->dest_y;
                                if (!picture->full && picture->damages)
                                        left = picture->damage[at].x, top = picture->damage[at].y,
                                        right = left + picture->damage[at].width, bottom = top + picture->damage[at].height;
                                if (left < 0) left = 0;
                                if (top < 0) top = 0;
                                if (right > picture->width) right = picture->width;
                                if (bottom > picture->height) bottom = picture->height;
                                if (x + left < 0) left = -x;
                                if (y + top < 0) top = -y;
                                if (x + right > width) right = width - x;
                                if (y + bottom > height) bottom = height - y;
                                if (right <= left || bottom <= top)
                                        continue;
                                if (!picture->pixels)
                                {
                                        if (compositor_wl_card_rect(row, picture, to, pitch, x + left, y + top, left, top, right - left, bottom - top, 1))
                                                table->client_rects_card++, compositor_wl_copied(row, WINDOWS_COPY_CARD),
                                                compositor_wl_laid(row, x + left, y + top, x + right, y + bottom);
                                        continue;
                                }
                                unsigned int *dst_p = to + (unsigned long)(y + top) * pitch + (unsigned long)(x + left);
                                const unsigned int *src_p = (const unsigned int *)(picture->pixels + top * picture->stride) + left;
                                unsigned long sp = (unsigned long)picture->stride / 4, rw = (unsigned long)(right - left), rh = (unsigned long)(bottom - top);
                                if (picture->blend)
                                {
                                        if (!wl_pic_sync_begin(row, picture, &sync_started))
                                        {
                                                if (compositor_wl.tiled_lent)
                                                        wl_ext_tiled_settle();
                                                return 1;
                                        }
                                        wl_ext_tiled_settle();
                                        memory_over_u32_rect(dst_p, pitch, src_p, sp, rw, rh);
                                }
                                else if ((compositor_wl_tiled(picture) || (card_wanted && (right - left) * (bottom - top) >= 32768)) &&
                                         compositor_wl_card_rect(row, picture, to, pitch, x + left, y + top, left, top, right - left, bottom - top,
                                                                 card_wanted && (right - left) * (bottom - top) >= 32768))
                                        table->client_rects_card++, compositor_wl_copied(row, WINDOWS_COPY_CARD);
                                else if (picture->sync_fd >= 0)
                                {
                                        if (!wl_pic_sync_begin(row, picture, &sync_started))
                                        {
                                                if (compositor_wl.tiled_lent)
                                                        wl_ext_tiled_settle();
                                                return 1;
                                        }
                                        wl_ext_tiled_settle();
                                        table->client_rects_cpu++, compositor_wl_copied(row, WINDOWS_COPY_CPU);
                                        memory_copy_rect_stream_u32(dst_p, pitch, src_p, sp, rw, rh);
                                }
                                else
                                {
                                        wl_ext_tiled_settle();
                                        table->client_rects_cpu++, compositor_wl_copied(row, WINDOWS_COPY_CPU);
                                        memory_copy_rect_u32(dst_p, pitch, src_p, sp, rw, rh);
                                }
                                compositor_wl_laid(row, x + left, y + top, x + right, y + bottom);
                        }
                        if (sync_started)
                                canvas_dmabuf_sync_read(picture->sync_fd, 1);
                        if (picture->pixels && compositor_wl_cut(picture->pixels))
                        {
                                if (compositor_wl.tiled_lent)
                                        wl_ext_tiled_settle();
                                return compositor_wl.laying[row].open = 0, -1;
                        }
                }
        }
        if (aside)
        {
                wl_ext_tiled_settle();
                compositor_wl_layers_put_list(over, aside, to, pitch);
                compositor_wl_copied(row, WINDOWS_COPY_WHOLE);
                compositor_wl.laying[row].full = compositor_wl.laying[row].touched = 1;
        }
        if (!picture->last)
                return 0;
        if (has_content && !picture->pixels && !compositor_wl.laying[row].touched)
        {
                compositor_wl.laying[row].open = 0;
                return 1;
        }
        table->client_copied++, compositor_wl_copied(row, WINDOWS_COPY_TABLE);
        if (width != (long)table->width[row] || height != (long)table->height[row] || !compositor_wl.laying[row].touched)
                compositor_wl.laying[row].full = 1;
        table->width[row] = (unsigned int)width, table->height[row] = (unsigned int)height;
        table->buffer_index[row] = compositor_wl.laying[row].which;
        compositor_wl.laying[row].last = (unsigned char)(compositor_wl.laying[row].which + 1);
        table->damage_x[row] = compositor_wl.laying[row].full ? 0 : compositor_wl.laying[row].x1;
        table->damage_y[row] = compositor_wl.laying[row].full ? 0 : compositor_wl.laying[row].y1;
        table->damage_width[row] = compositor_wl.laying[row].full ? 0 : compositor_wl.laying[row].x2 - compositor_wl.laying[row].x1;
        table->damage_height[row] = compositor_wl.laying[row].full ? 0 : compositor_wl.laying[row].y2 - compositor_wl.laying[row].y1;
        compositor_wl.laying[row].open = 0;
        __atomic_store_n(&table->frame[row], table->frame[row] + 1, __ATOMIC_RELEASE);
        compositor_wl_take(row);
        return compositor_wl.tiled_lent ? WAYLAND_PICTURE_LENT : 0;
}

static void compositor_wl_row_title(void *context, int row, const char *text, long length)
{
        (void)context, (void)length;
        compositor_row_set_title(row, text, WINDOWS_TITLE);
        compositor_take(row);
}

static void compositor_wl_row_app(void *context, int row, const char *app_id)
{
        (void)context;
        if (row >= 0 && row < (int)WINDOWS && compositor.table && windows_app_id_set(compositor.table, (unsigned int)row, app_id))
                compositor_take(row);
}

#define COMPOSITOR_WL_HIDDEN 0xffu
static const unsigned char compositor_wl_cursor_of[11] = {CP_ARROW, CP_TEXT, CP_CROSS, CP_MOVE, CP_RESIZE_H, CP_RESIZE_V, CP_RESIZE_NWSE,
                                                          CP_RESIZE_NESW, CP_HAND, CP_WAIT, CP_NOTALLOWED};

static int wl_ext_asked_shape(int row)
{
        if (row < 0 || row >= COMPOSITOR_ROWS || !compositor_wl.cursor_asked[row])
                return -1;
        return compositor_wl.cursor_asked[row] == COMPOSITOR_WL_HIDDEN ? -2 : compositor_wl.cursor_asked[row] - 1;
}

static void compositor_wl_field(int row, long delta, long purpose)
{
        unsigned int *count = &compositor.table->field[row];
        if (delta > 0)
                (*count)++;
        else if (delta < 0 && *count)
                (*count)--;
        compositor.text_purpose[row] = *count && purpose >= 0 && purpose <= 13 ? (unsigned char)purpose : 0;
        compositor_osk_sync();
}

static void compositor_wl_caret(int row, long bottom)
{
        int want = bottom > 0 ? (int)(bottom < WAYLAND_DIMENSION ? bottom : WAYLAND_DIMENSION) : 0;
        if (compositor.caret[row] == want)
                return;
        compositor.caret[row] = want;
        if (compositor.osk_pan[row] || compositor.osk_height[row])
                compositor_osk_unpan(row);
        compositor_osk_sync();
}

static void compositor_wl_row_do(void *context, int row, long what, long a, long b)
{
        (void)context;
        if (row < 0 || row >= COMPOSITOR_ROWS)
                return;
        if (what == WAYLAND_DO_TEXT) { compositor_wl_field(row, a, b); return; }
        if (what == WAYLAND_DO_CARET) { compositor_wl_caret(row, a); return; }
        if (!compositor.showing[row])
                return;
        switch (what)
        {
        case WAYLAND_DO_MINIMIZE: compositor_minimize_row(row); break;
        case WAYLAND_DO_MAXIMIZE: compositor_arrange(row, COMPOSITOR_ROOM); break;
        case WAYLAND_DO_FULLSCREEN: compositor_fullscreen_on(row, (int)a); break;
        case WAYLAND_DO_UNFULLSCREEN: compositor_fullscreen_off(row); break;
        case WAYLAND_DO_UNMAXIMIZE: compositor_float(row); break;
        case WAYLAND_DO_CLOSE: compositor_close(row); break;
        case WAYLAND_DO_FOCUS: compositor_reveal(row); break;
        case WAYLAND_DO_LAYER: compositor_layer_set(row, (signed char)a, (int)b); break;
        case WAYLAND_DO_PINCH_WANT: compositor.table->pinch_wanted[row] = a ? 1u : 0u; break;
        case WAYLAND_DO_MOVE: (void)compositor_grab_begin_client(row, 0, 1, 0); break;
        case WAYLAND_DO_PLACE:
                if (compositor.table && compositor.table->popup_parent[row])
                        compositor_damage_row(row), compositor.table->popup_x[row] = (int)a, compositor.table->popup_y[row] = (int)b, compositor_popup_first(row);
                else if (compositor.table && wayland_row_xoverride(row))
                        compositor_damage_row(row), compositor_put(row, (int)a, (int)b), compositor_damage_row(row);
                break;
        case WAYLAND_DO_MIN_SIZE:
                if (compositor.table)
                        compositor.table->least_width[row] = a > 0 ? (unsigned int)a : 0, compositor.table->least_height[row] = b > 0 ? (unsigned int)b : 0;
                break;
        case WAYLAND_DO_MAX_SIZE:
                if (compositor.table)
                        compositor.table->most_width[row] = a > 0 ? (unsigned int)a : 0, compositor.table->most_height[row] = b > 0 ? (unsigned int)b : 0;
                break;
        case WAYLAND_DO_CURSOR:
                compositor_wl.cursor_asked[row] = (unsigned char)(a == WAYLAND_CURSOR_HIDDEN ? COMPOSITOR_WL_HIDDEN
                                                                  : a < 0 || a > WAYLAND_CURSOR_NOTALLOWED ? 0 : compositor_wl_cursor_of[a] + 1);
                compositor_shape_sync();
                break;
        case WAYLAND_DO_RESIZE:
                (void)compositor_grab_begin_client(row, ((a & 1) ? COMPOSITOR_EDGE_TOP : 0) | ((a & 2) ? COMPOSITOR_EDGE_BOTTOM : 0) |
                                                        ((a & 4) ? COMPOSITOR_EDGE_LEFT : 0) | ((a & 8) ? COMPOSITOR_EDGE_RIGHT : 0), 0, 0);
                break;
        default: break;
        }
}

/* What the compositor says of a row, in the shim's words (from Canvas's canonical compositor_row_facts). */
static int compositor_wl_row_facts(void *context, int row, struct wayland_facts *facts)
{
        struct compositor_row_facts f;
        long states = 0;

        (void)context;
        if (compositor_row_facts(row, &f) != 0)
                return -1;
        facts->width = (long)f.want_width;
        facts->height = (long)f.want_height;
        if (f.fullscreen)
                states |= WAYLAND_STATE_FULLSCREEN;
        else if (f.filling)
                states |= WAYLAND_STATE_MAXIMIZED;
        else if (f.arranged != COMPOSITOR_FLOATING)
                states |= WAYLAND_STATE_TILED_LEFT | WAYLAND_STATE_TILED_RIGHT | WAYLAND_STATE_TILED_TOP | WAYLAND_STATE_TILED_BOTTOM;
        if (f.focused)
                states |= WAYLAND_STATE_ACTIVATED;
        if (f.resizing)
                states |= WAYLAND_STATE_RESIZING;
        if (f.minimized)
                states |= WAYLAND_STATE_SUSPENDED;
        facts->states = states;
        facts->closing = (long)f.closing;
        facts->x = f.x;
        facts->y = f.y;
        facts->scale = f.scale;
        return 0;
}

static long compositor_wl_screens(void *context, struct wayland_screen *screens, long room)
{
        long count = (long)compositor_output_count();

        (void)context;
        count = count > room ? room : count;
        for (long at = 0; at < count; at++)
        {
                const char *name = compositor_output_name((int)at);
                int x, y, width, height;

                compositor_output_rect((int)at, &x, &y, &width, &height);
                screens[at].x = x;
                screens[at].y = y;
                screens[at].width = width;
                screens[at].height = height;
                screens[at].scale = compositor_output_scale((int)at) > 0 ? compositor_output_scale((int)at) : compositor.scale;
                screens[at].refresh = compositor.period ? (long)(1000000000000ull / compositor.period) : 60000;
                screens[at].physical_width = screens[at].physical_height = 0;
                memory_zero(screens[at].name, 16);
                string_copy_bounded((string_address)screens[at].name, (string_address)name, 16);
                if (!screens[at].name[0])
                {
                        screens[at].name[0] = 'C';
                        screens[at].name[1] = 'a';
                        screens[at].name[2] = 'n';
                        screens[at].name[3] = 'v';
                        screens[at].name[4] = 'a';
                        screens[at].name[5] = 's';
                }
        }
        return count;
}

/*
        What the screens are, as one number: their rectangles, scales, rates and names, in order. A scale, a mode, a place or a
        turn that changes any of it changes this, and the clients are told the screens again (compositor_wl_wake).
*/
static unsigned long compositor_wl_screens_digest(void)
{
        struct wayland_screen screens[WAYLAND_SCREENS];
        long count = compositor_wl_screens(0, screens, WAYLAND_SCREENS);
        unsigned long hash = 14695981039346656037ul;

        for (long at = 0; at < count; at++)
        {
                const long values[] = {screens[at].x, screens[at].y, screens[at].width, screens[at].height, screens[at].scale,
                                       screens[at].refresh, screens[at].physical_width, screens[at].physical_height};

                for (unsigned long each = 0; each < sizeof values / sizeof values[0]; each++)
                        hash = (hash ^ (unsigned long)values[each]) * 1099511628211ul;
                for (long letter = 0; letter < 16 && screens[at].name[letter]; letter++)
                        hash = (hash ^ (unsigned char)screens[at].name[letter]) * 1099511628211ul;
                hash = (hash ^ 0x100) * 1099511628211ul;
        }
        return hash ^ (unsigned long)count;
}

static long compositor_wl_now(void *context)
{
        (void)context;
        return (long)(clock_monotonic_nanoseconds() / 1000000ull);
}

// Create a read-only sealed memfd holding `length` bytes of `data` (shared by keymap and dmabuf format table).
static long compositor_wl_memfd_sealed(const char *name, const void *data, unsigned long length)
{
        long fd = compositor_call(syscall(memfd_create), (positive)name, 3, 0, 0, 0, 0); // MFD_CLOEXEC | MFD_ALLOW_SEALING
        unsigned long done = 0;

        if (fd < 0)
                return -1;
        while (done < length)
        {
                long put = compositor_call(syscall(write), (positive)fd, (positive)((const unsigned char *)data + done), length - done, 0, 0, 0);

                if (put <= 0)
                {
                        system_close((int)fd);
                        return -1;
                }
                done += (unsigned long)put;
        }
        compositor_call(syscall(fcntl), (positive)fd, 1033, 15, 0, 0, 0); // F_ADD_SEALS: SEAL|SHRINK|GROW|WRITE
        return fd;
}

/*
        The keymap of the layout in use, as a sealed file the shim hands out beside the keyboard's event: made the first time,
        written whole and sealed against any change, and the same file for every client after. Clients map it private and
        read-only, as xkb's libraries do.
*/
static long compositor_wl_keymap(void *context, long *size)
{
        static char text[COMPOSITOR_WL_KEYMAP_ROOM];
        unsigned int layout = compositor.keymap.layout;

        (void)context;
        if (layout >= 16)
                return -1;
        if (compositor_wl.keymap[layout] < 0)
        {
                unsigned long length = compositor_keymap_xkb_build(layout, text, sizeof text);
                long fd = length ? compositor_wl_memfd_sealed("moonwater-keymap", text, length) : -1;

                if (fd < 0)
                        return -1;
                compositor_wl.keymap[layout] = (int)fd;
                compositor_wl.keymap_size[layout] = length;
        }
        *size = (long)compositor_wl.keymap_size[layout];
        return compositor_wl.keymap[layout];
}

// How fast a held key repeats, as the settings keep it, or as the keyboard has it: the delay in milliseconds and the rate a second.
static void compositor_wl_repeat(void *context, long *rate, long *delay)
{
        long own_delay = 250, own_period = 33;

        (void)context;
        for (unsigned int at = 0; at < compositor.input_count; at++)
                if (compositor.repeats[at])
                {
                        own_delay = compositor.repeat_own[at][0] > 0 ? compositor.repeat_own[at][0] : own_delay;
                        own_period = compositor.repeat_own[at][1] > 0 ? compositor.repeat_own[at][1] : own_period;
                        break;
                }
        *delay = (compositor.repeat_kept & 1) ? compositor.repeat_delay : own_delay;
        *rate = (compositor.repeat_kept & 2) ? compositor.repeat_rate : 1000 / own_period;
        if (*delay < 0 || *delay > 10000)
                *delay = 250;
        if (*rate < 0 || *rate > 1000)
                *rate = 30;
}

// What the seat can do: a pointer and a keyboard; touch is not offered yet.
// A touch capability while a touch screen is on the machine (the screens' fingers are sent to the windows as wl_touch).
// Random bytes for the xdg-activation tokens: the kernel's, from the pool as it is (a token is no secret of a key's size).
static int compositor_wl_random(void *context, unsigned char *into, long bytes)
{
        (void)context;
        return system_random_fill(into, (positive)bytes, 0) == 0;
}

static long compositor_wl_seat(void *context)
{
        long caps = WLN_wl_seat_capability_pointer | WLN_wl_seat_capability_keyboard;

        (void)context;
        for (unsigned int at = 0; at < compositor.input_count; at++)
                if (compositor.touchscreen[at])
                        caps |= WLN_wl_seat_capability_touch;
        return caps;
}

// Caps Lock, as the keymap's own Lock mask.
static long compositor_wl_locks(void *context)
{
        (void)context;
        return compositor.keymap.caps ? 2 : 0;
}

/* ---- games: the pointer held by a client's window ---- */

/*
        A client's window holds the pointer when the shim says so (a lock or a confinement, which the shim ends the moment the window
        is not under the pointer or does not have the keys, so the person can always leave). Locked, the hand's counts are the
        client's as they come, unaccelerated, and the pointer is neither moved nor drawn (it is drawn again where it stopped when the
        lock ends); confined, the pointer moves as ever and is kept inside the window.
*/
static void compositor_wl_direct_hold(void);    // below: the lock the input thread's own sends take
static void compositor_wl_direct_release(void);
static void compositor_wl_direct_make(void);

static void compositor_wl_pointer_hold(void *context, long row, long mode)
{
        long was = compositor_wl.hold_row;

        (void)context;
        // A lock let go of with a hint (set_cursor_position_hint: where a game drew the pointer it held, or where Xwayland has an X
        // game's pointer, which it warps): the pointer is shown there, in the window, where it would otherwise come back where the
        // lock took it and jump. A hint outside the window's picture is not taken.
        if (mode != 1 && wayland.hint_row >= 0 && wayland.hint_row < COMPOSITOR_ROWS &&
            (compositor_wl.hold == 1 ? wayland.hint_row == was : wayland.hint_row == wayland.pointer_row))
        {
                int sx, sy;

                if (compositor_screen_point((int)wayland.hint_row, (int)wayland.hint_x, (int)wayland.hint_y, &sx, &sy))
                {
                        compositor.pointer_x = compositor.kept_x = sx;
                        compositor.pointer_y = compositor.kept_y = sy;
                        compositor_pointer_sync();
                        compositor_pointer_shown();
                }
        }
        wayland.hint_row = -1;
        compositor_wl_direct_hold();
        compositor_wl.hold_row = (int)row;
        compositor_wl.hold = (int)mode;
        compositor_wl_direct_make();
        compositor_wl_direct_release();
        compositor.cursor.hold_hidden = mode == 1;
        compositor_cursor_move();
}

static int wl_ext_held(void)
{
        return (compositor_wl.hold == 1 || compositor_wl.hold == 2) && compositor_wl.hold_row >= 0;
}

// Whether a client's window holds the pointer locked: the hand's counts are the client's alone, and the pointer is not to move.
static int compositor_wl_locked(void)
{
        return compositor_wl.hold == 1 && compositor_wl.hold_row >= 0;
}

static void compositor_wl_counts(int dx, int dy, int udx, int udy, positive now)
{
        long row = wl_ext_held() ? compositor_wl.hold_row : wayland.pointer_row;

        if (row >= 0 && (udx || udy))
                wayland_pointer_relative(row, dx, dy, udx, udy, (long)(now / 1000));
}

// A pointer confined to a window, moved, is put back inside it.
static void compositor_wl_hold_keep(void)
{
        int row = compositor_wl.hold_row;

        if (compositor_wl.hold != 2 || row < 0)
                return;
        if (compositor.real_x < compositor.x[row])
                compositor.real_x = compositor.x[row];
        if (compositor.real_x > compositor.x[row] + (int)compositor.w[row] - 1)
                compositor.real_x = compositor.x[row] + (int)compositor.w[row] - 1;
        if (compositor.real_y < compositor.y[row])
                compositor.real_y = compositor.y[row];
        if (compositor.real_y > compositor.y[row] + (int)compositor.h[row] - 1)
                compositor.real_y = compositor.y[row] + (int)compositor.h[row] - 1;
}

/* ---- dma-bufs ---- */

/*
        What a client that draws with the GPU is told it may hand over: linear ARGB8888 and XRGB8888 on the render node of the
        card the compositor shows on (or, with no card, the first render node there is), because what is composed from a buffer
        here is read by the processor, which can read linear memory and no tiled or compressed kind. A buffer the processor
        cannot wait for is skipped, and one client that makes it wait is not waited for again for a while.
*/
static const struct
{
        unsigned int format;
        unsigned long modifier;
} compositor_wl_dmabuf_entry[2] = {{WAYLAND_FOURCC_ARGB8888, 0}, {WAYLAND_FOURCC_XRGB8888, 0}};

static unsigned long compositor_wl_render_node(void)
{
        struct canvas_drm_render r;

        canvas_drm_render_probe(compositor.card, compositor_wl.render, &r);
        if (r.dev)
        {
                string_copy_bounded((string_address)compositor_wl.render_path, (string_address)r.path, sizeof compositor_wl.render_path);
                compositor_wl.render_hw = r.hw;
                compositor_wl.syncobj_ok = r.syncobj;
                if (r.dev == compositor_wl.render && compositor_wl.syncobj_pool.drm_fd >= 0)
                {
                        // Keep the existing DRM FD so live wp_linux_drm_syncobj_timeline_v1 handles remain valid.
                        if (r.fd >= 0 && r.fd != compositor_wl.syncobj_pool.drm_fd)
                                system_close(r.fd);
                }
                else
                {
                        if (compositor_wl.syncobj_pool.drm_fd >= 0 && compositor_wl.syncobj_pool.drm_fd != r.fd)
                                system_close(compositor_wl.syncobj_pool.drm_fd);
                        canvas_syncobj_init(&compositor_wl.syncobj_pool, r.fd);
                }
                return r.dev;
        }
        compositor_wl.render_path[0] = 0;
        compositor_wl.render_hw = 0;
        compositor_wl.syncobj_ok = 0;
        if (compositor_wl.syncobj_pool.drm_fd >= 0)
                system_close(compositor_wl.syncobj_pool.drm_fd);
        canvas_syncobj_init(&compositor_wl.syncobj_pool, -1);
        return 0;
}

static void compositor_wl_dmabuf_add_pair(struct wayland_dmabuf_info *info, unsigned int format, unsigned long modifier)
{
        if (info->count >= WAYLAND_DMABUF_ENTRIES)
                return;
        for (long i = 0; i < info->count; i++)
                if (info->entry[i].format == format && info->entry[i].modifier == modifier)
                        return;
        info->entry[info->count].format = format;
        info->entry[info->count].modifier = modifier;
        info->count++;
}

static void compositor_wl_dmabuf_tiled_pairs(struct wayland_dmabuf_info *info)
{
        static struct canvas_drm_format_pair plane[512];
        unsigned int count = canvas_drm_plane_formats(compositor.card, compositor.primary, plane, 512), supported = 0;

        for (unsigned int at = 0; at < count; at++)
        {
                unsigned int format = plane[at].format;

                if (!wayland_fourcc_supported(format))
                        continue;
                supported++;
                if ((format == WAYLAND_FOURCC_XRGB8888 || format == WAYLAND_FOURCC_XBGR8888) &&
                    plane[at].modifier && plane[at].modifier != WAYLAND_MODIFIER_INVALID && compositor_wl_tiled_takes(plane[at].modifier))
                        compositor_wl_dmabuf_add_pair(info, format, plane[at].modifier);
        }
        string_format(log, "compositor: the primary plane %p takes %p pairs of the wanted formats\n", (positive)compositor.primary, (positive)supported), log_flush();
}

static long compositor_wl_dmabuf_info(void *context, struct wayland_dmabuf_info *info)
{
        static const unsigned int base_formats[] = {
            WAYLAND_FOURCC_ARGB8888, WAYLAND_FOURCC_XRGB8888,
            WAYLAND_FOURCC_ABGR8888,       WAYLAND_FOURCC_XBGR8888,
            WAYLAND_FOURCC_ARGB2101010,    WAYLAND_FOURCC_XRGB2101010,
            WAYLAND_FOURCC_ABGR2101010,    WAYLAND_FOURCC_XBGR2101010,
            WAYLAND_FOURCC_NV12,
        };
        int changed = 0;

        (void)context;
        compositor_wl.render = compositor_wl_render_node();
        if (!compositor_wl.render)
                return 0;
        info->device = compositor_wl.render;
        info->count = 0;
        for (unsigned long i = 0; i < sizeof base_formats / sizeof base_formats[0]; i++)
                compositor_wl_dmabuf_add_pair(info, base_formats[i], 0);
        compositor_wl.dmabuf_tiled = compositor_wl_tiled_now();
        {
                static const char *const intel[] = {"i915", "xe"};
                int skip_invalid = !compositor_wl.dmabuf_tiled && compositor_driver_in(intel, 2);

                for (unsigned long i = 0; !skip_invalid && i < sizeof base_formats / sizeof base_formats[0]; i++)
                        if (!compositor_wl.dmabuf_tiled || (i != 1 && i != 3))
                                compositor_wl_dmabuf_add_pair(info, base_formats[i], WAYLAND_MODIFIER_INVALID);
        }
        {
                long linear = info->count;

                if (compositor_wl.dmabuf_tiled)
                        compositor_wl_dmabuf_tiled_pairs(info);
                if (compositor_wl.started)
                        string_format(log, "compositor: dma-buf pairs told: %p linear and implicit, %p tiled\n", (positive)linear, (positive)(info->count - linear)), log_flush();
        }
        info->tranches = 1;
        info->tranche[0].device = info->device;
        info->tranche[0].flags = compositor.card >= 0 && compositor.primary ? 1 : 0;
        info->tranche[0].count = info->count;
        for (long at = 0; at < info->count; at++)
                info->tranche[0].index[at] = (unsigned char)at;
        if (compositor_wl.dmabuf_entry_count != info->count)
                changed = 1;
        for (long at = 0; !changed && at < info->count; at++)
                if (compositor_wl.dmabuf_entries[at].format != info->entry[at].format ||
                    compositor_wl.dmabuf_entries[at].modifier != info->entry[at].modifier)
                        changed = 1;
        if (changed)
        {
                if (compositor_wl.dmabuf_table > 0)
                        system_close(compositor_wl.dmabuf_table);
                compositor_wl.dmabuf_table = 0;
                compositor_wl.dmabuf_table_size = 0;
                compositor_wl.dmabuf_entry_count = info->count;
                for (long at = 0; at < info->count; at++)
                {
                        compositor_wl.dmabuf_entries[at].format = info->entry[at].format;
                        compositor_wl.dmabuf_entries[at].modifier = info->entry[at].modifier;
                }
        }
        return 1;
}

// Re-probes the render node after startup (a GPU that probed late or a drm uevent); tells the Wayland shim if it changed.
static void compositor_wl_dmabuf_refresh(void)
{
        unsigned long was;
        int was_hw, was_sync;

        if (!compositor_wl.started)
                return;
        if (compositor_wl_tiled_now() != compositor_wl.dmabuf_tiled)
        {
                compositor_wl_render_node();
                wayland_dmabuf_changed();
                return;
        }
        if (compositor_wl.render && compositor_wl.render_hw && compositor_wl.render_path[0] &&
            canvas_drm_node_dev(compositor_wl.render_path) == compositor_wl.render)
                return;
        was = compositor_wl.render;
        was_hw = compositor_wl.render_hw;
        was_sync = compositor_wl.syncobj_ok;
        if (compositor_wl_render_node() != was || compositor_wl.render_hw != was_hw || compositor_wl.syncobj_ok != was_sync)
                wayland_dmabuf_changed();
}

// The table the feedback names the pairs by: a sealed file of entries of a format, four bytes of padding and a modifier.
static long compositor_wl_dmabuf_table(void *context, long *size)
{
        unsigned char bytes[WAYLAND_DMABUF_ENTRIES * 16];
        long count = compositor_wl.dmabuf_entry_count, total, fd;

        (void)context;
        if (count < 1)
                count = 2;
        total = count * 16;
        if (compositor_wl.dmabuf_table <= 0 || compositor_wl.dmabuf_table_size != total)
        {
                if (compositor_wl.dmabuf_table > 0)
                        system_close(compositor_wl.dmabuf_table);
                compositor_wl.dmabuf_table = 0;
                memory_zero(bytes, (unsigned long)total);
                for (long at = 0; at < count; at++)
                {
                        unsigned int fmt = compositor_wl.dmabuf_entry_count > 0 ? compositor_wl.dmabuf_entries[at].format
                                                                                : compositor_wl_dmabuf_entry[at & 1].format;
                        unsigned long mod = compositor_wl.dmabuf_entry_count > 0 ? compositor_wl.dmabuf_entries[at].modifier : 0;

                        for (long byte = 0; byte < 4; byte++)
                                bytes[at * 16 + byte] = (unsigned char)(fmt >> (8 * byte));
                        for (long byte = 0; byte < 8; byte++)
                                bytes[at * 16 + 8 + byte] = (unsigned char)(mod >> (8 * byte));
                }
                fd = compositor_wl_memfd_sealed("moonwater-dmabuf-table", bytes, (unsigned long)total);
                if (fd < 0)
                        return -1;
                compositor_wl.dmabuf_table = (int)fd;
                compositor_wl.dmabuf_table_size = total;
        }
        *size = compositor_wl.dmabuf_table_size;
        return compositor_wl.dmabuf_table;
}

// Whether a descriptor is a dma-buf (its filesystem's magic, DMAB) of at least size bytes.
static int compositor_wl_dmabuf_holds(long fd, unsigned long size)
{
        unsigned long on[32];
        long extent;

        memory_zero(on, sizeof on);
        if (compositor_call(syscall(fstatfs), (positive)fd, (positive)on, 0, 0, 0, 0) < 0 || on[0] != 0x444d4142ul)
                return 0;
        extent = compositor_call(syscall(lseek), (positive)fd, 0, 2, 0, 0, 0);
        return extent >= 0 && (unsigned long)extent >= size;
}

static void *compositor_wl_dmabuf_map(void *context, long fd, long size)
{
        unsigned long on[32];
        long extent, got;
        int tiled = size < 0;

        (void)context;
        memory_zero(on, sizeof on);
        size = tiled ? -size : size;
        if (size < 1 || compositor_call(syscall(fstatfs), (positive)fd, (positive)on, 0, 0, 0, 0) < 0 || on[0] != 0x444d4142ul)
                return 0;
        if (canvas_dmabuf_sync_fd(fd, 0) != -22)
                return 0;
        extent = compositor_call(syscall(lseek), (positive)fd, 0, 2, 0, 0, 0);
        if (extent >= 0 && extent < size)
                return 0;
        if (tiled)
        {
                positive import = gpu_ready() ? gpu_import_fd((int)fd, (positive)(extent > 0 ? extent : size)) : 0;

                if (!import)
                        return 0;
                gpu_release_import(import);
                return WAYLAND_DMABUF_UNMAPPED;
        }
        got = compositor_call(syscall(mmap), 0, (positive)size, 1, 1, (positive)fd, 0);
        if (got < 0 && got > -4096)
        {
                if (gpu_ready())
                {
                        positive import = gpu_import_fd((int)fd, (positive)size);

                        if (!import)
                                return 0;
                        gpu_release_import(import);
                        return WAYLAND_DMABUF_UNMAPPED;
                }
                if (compositor.card >= 0 && compositor_output_count() <= 1)
                        return WAYLAND_DMABUF_UNMAPPED;
                return 0;
        }
        if (compositor_wl_map_note((unsigned long)got, (unsigned long)size))
                return (void *)got;
        compositor_call(syscall(munmap), (positive)got, (positive)size, 0, 0, 0, 0);
        return 0;
}

static long compositor_wl_syncobj_available(void *context, char *node, long node_cap)
{
        long flags = 0;

        (void)context;
        if (!compositor_wl.render)
                compositor_wl.render = compositor_wl_render_node();
        if (node && node_cap > 0)
        {
                long i = 0;

                for (; i < node_cap - 1 && compositor_wl.render_path[i]; i++)
                        node[i] = compositor_wl.render_path[i];
                node[i] = 0;
                if (i > 0)
                        flags |= WAYLAND_SYNCOBJ_NODE_OK;
        }
        if (compositor_wl.syncobj_ok && compositor_wl.syncobj_pool.drm_fd >= 0)
                flags |= WAYLAND_SYNCOBJ_TIMELINE_OK;
        return flags;
}

static long compositor_wl_syncobj_import(void *context, long fd)
{
        (void)context;
        return canvas_syncobj_import(&compositor_wl.syncobj_pool, fd);
}

static void compositor_wl_syncobj_drop(void *context, long slot)
{
        (void)context;
        canvas_syncobj_drop(&compositor_wl.syncobj_pool, slot);
}

static int compositor_wl_syncobj_ready(void *context, long slot, unsigned long long point)
{
        (void)context;
        return canvas_syncobj_ready(&compositor_wl.syncobj_pool, slot, point);
}

static void compositor_wl_syncobj_signal(void *context, long slot, unsigned long long point)
{
        (void)context;
        canvas_syncobj_signal(&compositor_wl.syncobj_pool, slot, point);
}

static long compositor_wl_syncobj_eventfd(void *context, long slot, unsigned long long point)
{
        (void)context;
        return canvas_syncobj_eventfd(&compositor_wl.syncobj_pool, slot, point);
}

static const struct wayland_syncobj_host compositor_wl_syncobj = {
    .context = 0,
    .available = compositor_wl_syncobj_available,
    .import = compositor_wl_syncobj_import,
    .drop = compositor_wl_syncobj_drop,
    .ready = compositor_wl_syncobj_ready,
    .signal = compositor_wl_syncobj_signal,
    .eventfd = compositor_wl_syncobj_eventfd,
};

static int compositor_wl_dmabuf_ready(long client, int fd)
{
        struct compositor_poll watch = {fd, COMPOSITOR_POLLIN, 0};
        positive wait[2] = {0, 0};

        (void)client;
        compositor_call(syscall(ppoll), (positive)&watch, 1, (positive)wait, 0, 0, 0);
        return (watch.returned & COMPOSITOR_POLLIN) != 0;
}

static void compositor_wl_alarm(int number)
{
        (void)number;
}

static int compositor_wl_sync_start(long client, int fd, int ready)
{
        positive timer[4] = {0, 0, 0, COMPOSITOR_WL_DMABUF_WAIT_NS / 1000}, off[4] = {0, 0, 0, 0}; // struct itimerval, in us
        long got;

        if (client >= 0 && client < compositor_wl.slots && compositor_wl.client[client].slow_until > clock_monotonic_nanoseconds())
                return 0;
        if (ready)
                return canvas_dmabuf_sync_fd(fd, 1) != -5;

        compositor_call(syscall(setitimer), 0, (positive)timer, 0, 0, 0, 0); // ITIMER_REAL
        got = canvas_dmabuf_sync_fd(fd, 1); // DMA_BUF_SYNC_START | DMA_BUF_SYNC_READ
        compositor_call(syscall(setitimer), 0, (positive)off, 0, 0, 0, 0);
        if (got != -4 && got != -5) // EINTR: timer ended wait; EIO: GPU reset; anything else (0 or ENOTTY on non-dmabuf) proceeds
                return 1;
        if (got == -4 && client >= 0 && client < compositor_wl.slots)
                compositor_wl.client[client].slow_until = clock_monotonic_nanoseconds() + COMPOSITOR_WL_DMABUF_SLOW_NS;
        return 0;
}

/* ---- clipboard transfers, pump threads, and DnD bridge ---- */

static void compositor_wl_transfer_free(long at)
{
        struct compositor_wl_transfer *t = &compositor_wl.transfer[at];
        if (t->in >= 0) system_close(t->in);
        if (t->out >= 0) system_close(t->out);
        if (t->keep >= 0) system_close(t->keep);
        if (t->index >= 0) system_close(t->index);
        t->state = 0, t->drop = 0, t->in = t->out = t->keep = t->index = -1;
}

static const char *compositor_wl_clip_type(void *context, long which, long nth)
{
        struct windows *table = compositor.table;
        unsigned long length;
        (void)context;
        if (!table || which < 0 || which > 1 || !table->clip_on || compositor_clip.fd[which] < 0 || nth < 0)
                return 0;
        return windows_clip_offered(compositor_clip.map[which], table->clip_length[which], (unsigned long)nth, 0, &length);
}

static long compositor_wl_transfer_place(void)
{
        for (long at = 0; at < compositor_wl.transfers; at++)
                if (!compositor_wl.transfer[at].state)
                        return at;
        wayland_tables_host(&compositor_wl_host);
        compositor_wl.transfer_table.unit = sizeof(struct compositor_wl_transfer);
        if (compositor_wl.transfers >= COMPOSITOR_WL_TRANSFERS_MOST || !wayland_table_reach(&compositor_wl.transfer_table, compositor_wl.transfers + 1))
                return -1;
        compositor_wl.transfer = (struct compositor_wl_transfer *)compositor_wl.transfer_table.at;
        long slot = compositor_wl.transfers++;
        memory_zero(&compositor_wl.transfer[slot], sizeof compositor_wl.transfer[slot]);
        compositor_wl.transfer[slot].in = compositor_wl.transfer[slot].out = compositor_wl.transfer[slot].keep = compositor_wl.transfer[slot].index = -1;
        return slot;
}

#define COMPOSITOR_WL_SLICE_NS 50000000ull
static void compositor_wl_pump_run(long at);

static void compositor_wl_transfer_run(address_any context)
{
        long at = (long)context;
        unsigned char bytes[65536];

        compositor_ordinary(0);
        if (compositor_wl.transfer[at].state == COMPOSITOR_WL_PUMP)
        {
                compositor_wl_pump_run(at);
                return;
        }
        int pull = compositor_wl.transfer[at].state == COMPOSITOR_WL_PULL, result = 1;
        int fd = pull ? compositor_wl.transfer[at].in : compositor_wl.transfer[at].out, from = compositor_wl.transfer[at].in;
        int keep = compositor_wl.transfer[at].keep;
        unsigned long done = 0, total = compositor_wl.transfer[at].total, base = compositor_wl.transfer[at].base, one = 1;
        positive still = pull ? COMPOSITOR_WL_PULL_NS : COMPOSITOR_WL_PUSH_NS, deadline = clock_monotonic_nanoseconds() + still;

        while (pull || done < total)
        {
                struct compositor_poll watch = {fd, pull ? COMPOSITOR_POLLIN : COMPOSITOR_POLLOUT, 0};
                positive now = clock_monotonic_nanoseconds(), wait[2] = {0, 0};
                long got, put = 0;

                if (__atomic_load_n(&compositor_wl.transfer[at].cancel, __ATOMIC_ACQUIRE) || now >= deadline)
                        break;
                wait[1] = deadline - now < COMPOSITOR_WL_SLICE_NS ? deadline - now : COMPOSITOR_WL_SLICE_NS;
                compositor_call(syscall(ppoll), (positive)&watch, 1, (positive)wait, 0, 0, 0);
                if (!pull && (watch.returned & (COMPOSITOR_POLLHUP | COMPOSITOR_POLLERR)))
                        break;
                if (!(watch.returned & (pull ? COMPOSITOR_POLLIN | COMPOSITOR_POLLHUP | COMPOSITOR_POLLERR : COMPOSITOR_POLLOUT)))
                        continue;
                if (pull)
                {
                        got = compositor_call(syscall(read), (positive)fd, (positive)bytes, sizeof bytes, 0, 0, 0);
                        if (got == -11) continue;
                        if (got == 0) result = 2;
                        if (got <= 0) break;
                        while (put < got)
                        {
                                long wrote = compositor_call(syscall(write), (positive)keep, (positive)(bytes + put), (positive)(got - put), 0, 0, 0);
                                if (wrote <= 0) break;
                                put += wrote;
                        }
                        if (put < got) break;
                        done += (unsigned long)got, deadline = clock_monotonic_nanoseconds() + still;
                        continue;
                }
                got = compositor_call(syscall(pread64), (positive)from, (positive)bytes,
                                      total - done > COMPOSITOR_WL_CHUNK ? COMPOSITOR_WL_CHUNK : total - done, base + done, 0, 0);
                if (got <= 0) break;
                put = compositor_call(syscall(write), (positive)fd, (positive)bytes, (positive)got, 0, 0, 0);
                if (put == -11) continue;
                if (put <= 0) break;
                done += (unsigned long)put, deadline = clock_monotonic_nanoseconds() + still;
        }
        if (!pull && done >= total)
                result = 2;
        compositor_wl.transfer[at].got = done;
        __atomic_store_n(&compositor_wl.transfer[at].finished, result, __ATOMIC_RELEASE);
        compositor_call(syscall(write), (positive)compositor_wl.wake, (positive)&one, sizeof one, 0, 0, 0);
}

static void compositor_wl_pump_run(long at)
{
        unsigned char bytes[COMPOSITOR_WL_CHUNK];
        int in = compositor_wl.transfer[at].in, out = compositor_wl.transfer[at].out, result = 1;
        long have = 0, put = 0;
        unsigned long one = 1;
        positive deadline = clock_monotonic_nanoseconds() + COMPOSITOR_WL_PUMP_NS;

        for (;;)
        {
                struct compositor_poll watch = {have ? out : in, have ? COMPOSITOR_POLLOUT : COMPOSITOR_POLLIN, 0};
                positive now = clock_monotonic_nanoseconds(), wait[2] = {0, 0};
                long n;
                int moved = 0;

                if (__atomic_load_n(&compositor_wl.transfer[at].cancel, __ATOMIC_ACQUIRE) || now >= deadline)
                        break;
                wait[1] = deadline - now < COMPOSITOR_WL_SLICE_NS ? deadline - now : COMPOSITOR_WL_SLICE_NS;
                compositor_call(syscall(ppoll), (positive)&watch, 1, (positive)wait, 0, 0, 0);
                if (!have)
                {
                        if (!(watch.returned & (COMPOSITOR_POLLIN | COMPOSITOR_POLLHUP | COMPOSITOR_POLLERR)))
                                continue;
                        n = compositor_call(syscall(read), (positive)in, (positive)bytes, sizeof bytes, 0, 0, 0);
                        if (n == -11) continue;
                        if (n == 0) result = 2;
                        if (n <= 0) break;
                        have = n, put = 0, moved = 1;
                }
                else
                {
                        if (watch.returned & (COMPOSITOR_POLLHUP | COMPOSITOR_POLLERR)) break;
                        if (!(watch.returned & COMPOSITOR_POLLOUT)) continue;
                        n = compositor_call(syscall(write), (positive)out, (positive)(bytes + put), (positive)(have - put), 0, 0, 0);
                        if (n == -11) continue;
                        if (n <= 0) break;
                        put += n, moved = 1;
                        if (put == have) have = 0;
                }
                if (moved)
                {
                        deadline = clock_monotonic_nanoseconds() + COMPOSITOR_WL_PUMP_NS;
                        if (__atomic_exchange_n(&compositor_wl.transfer[at].nudge, 0, __ATOMIC_ACQ_REL))
                                compositor_call(syscall(write), (positive)compositor_wl.wake, (positive)&one, sizeof one, 0, 0, 0);
                }
        }
        __atomic_store_n(&compositor_wl.transfer[at].finished, result, __ATOMIC_RELEASE);
        compositor_call(syscall(write), (positive)compositor_wl.wake, (positive)&one, sizeof one, 0, 0, 0);
}

static int compositor_wl_transfer_start(long at)
{
        compositor_wl.transfer[at].finished = compositor_wl.transfer[at].cancel = 0, compositor_wl.transfer[at].got = 0;
        compositor_wl.transfer[at].worker = compositor_wl.wake >= 0 ? thread_start(compositor_wl_transfer_run, (address_any)at) : 0;
        if (compositor_wl.transfer[at].worker)
                return 1;
        return compositor_wl_transfer_free(at), 0;
}

static long compositor_wl_pump(int in, int out)
{
        long slot = compositor_wl.wake >= 0 ? compositor_wl_transfer_place() : -1;
        if (slot < 0)
                return system_close(in), system_close(out), -1;
        struct compositor_wl_transfer *t = &compositor_wl.transfer[slot];
        t->state = COMPOSITOR_WL_PUMP, t->which = 0, t->client = t->row = -1, t->in = in, t->out = out, t->total = 0, t->nudge = 0;
        return compositor_wl_transfer_start(slot) ? slot : -1;
}

static void compositor_wl_pump_nudge(long at)
{
        if (at >= 0 && at < compositor_wl.transfers && compositor_wl.transfer[at].state == COMPOSITOR_WL_PUMP)
                __atomic_store_n(&compositor_wl.transfer[at].nudge, 1, __ATOMIC_RELEASE);
}

static void compositor_wl_transfer_cancel(long at)
{
        compositor_wl.transfer[at].client = -1;
        __atomic_store_n(&compositor_wl.transfer[at].cancel, 1, __ATOMIC_RELEASE);
}

static void compositor_wl_pull_end(long at, int ok);

static void compositor_wl_transfers_done(void)
{
        unsigned long knocks = 0;
        compositor_call(syscall(read), (positive)compositor_wl.wake, (positive)&knocks, sizeof knocks, 0, 0, 0);
        for (long at = 0; at < compositor_wl.transfers; at++)
        {
                int finished = compositor_wl.transfer[at].worker ? __atomic_load_n(&compositor_wl.transfer[at].finished, __ATOMIC_ACQUIRE) : 0;
                if (!finished)
                        continue;
                thread_join(compositor_wl.transfer[at].worker);
                compositor_wl.transfer[at].worker = 0;
                if (compositor_wl.transfer[at].state == COMPOSITOR_WL_PULL && !compositor_wl.transfer[at].cancel)
                        compositor_wl_pull_end(at, finished == 2);
                else
                        compositor_wl_transfer_free(at);
        }
}

static int compositor_wl_pull_open(const char *name, int *keep_out, int *index_out)
{
        long keep = compositor_call(syscall(memfd_create), (positive)name, 3, 0, 0, 0, 0);
        long index = keep >= 0 ? compositor_call(syscall(memfd_create), (positive)"moonwater-index", 1, 0, 0, 0, 0) : -1;
        if (index < 0)
        {
                if (keep >= 0) system_close((int)keep);
                return 0;
        }
        *keep_out = (int)keep, *index_out = (int)index;
        return 1;
}

static int compositor_wl_push_start(unsigned int which, long client, long row, const char *mime, long fd, int check_pipe)
{
        struct windows *table = compositor.table;
        struct windows_clip_type type;
        long src, slot;

        if (!table || which > WINDOWS_CLIP_DRAG || compositor_clip.fd[which] < 0 || !mime ||
            !windows_clip_find(compositor_clip.map[which], table->clip_length[which], mime, string_length((string_address)mime), &type))
                goto fail;
        if (check_pipe)
        {
                unsigned char info[256];
                memory_zero(info, sizeof info);
                if (compositor_call(syscall(statx), (positive)fd, (positive)"", 0x1000, 0x1, (positive)info, 0) < 0 ||
                    ((((unsigned int)info[28] | (unsigned int)info[29] << 8) & 0170000) != 0010000))
                        goto fail;
        }
        slot = compositor_wl_transfer_place();
        src = slot >= 0 ? compositor_call(syscall(fcntl), (positive)compositor_clip.fd[which], COMPOSITOR_F_DUPFD_CLOEXEC, 0, 0, 0, 0) : -1;
        if (slot < 0 || src < 0)
        {
                if (src >= 0) system_close((int)src);
                goto fail;
        }
        struct compositor_wl_transfer *t = &compositor_wl.transfer[slot];
        t->state = COMPOSITOR_WL_PUSH, t->which = (unsigned char)which, t->client = (int)client, t->row = (int)row;
        t->in = (int)src, t->out = (int)fd, t->base = type.offset, t->total = type.length;
        return compositor_wl_transfer_start(slot);
fail:
        system_close((int)fd);
        return 0;
}

static void compositor_wl_pull_whole(long at);

static void compositor_wl_pull_next(long at)
{
        struct compositor_wl_transfer *t = &compositor_wl.transfer[at];

        for (;;)
        {
                const char *type;
                int fds[2] = {-1, -1};
                long nth;

                if (t->text == -2)
                {
                        int best_rank = 0;
                        t->text = -1;
                        for (nth = 0; (type = wayland_clip_type(t->client, t->which, nth)); nth++)
                        {
                                int rank = windows_clip_text_rank(type, string_length((string_address)type));
                                if (rank >= 1 && rank <= 3 && (!best_rank || rank < best_rank))
                                        best_rank = rank, t->text = nth;
                        }
                        nth = t->text, t->nth = -1;
                        if (nth < 0) continue;
                }
                else
                {
                        nth = ++t->nth;
                        if (!(type = wayland_clip_type(t->client, t->which, nth)))
                        {
                                compositor_wl_pull_whole(at);
                                return;
                        }
                        if (nth == t->text || (t->text >= 0 && windows_clip_alias(type, string_length((string_address)type))))
                                continue;
                }
                t->reading = nth;
                if (compositor_call(syscall(pipe2), (positive)fds, 0x80000, 0, 0, 0, 0) < 0)
                {
                        compositor_wl_pull_whole(at);
                        return;
                }
                compositor_call(syscall(fcntl), (positive)fds[0], 4, 0x800, 0, 0, 0);
                if (!wayland_clip_send(t->client, t->which, nth, fds[1]))
                {
                        system_close(fds[0]);
                        continue;
                }
                t->in = fds[0];
                if (!compositor_wl_transfer_start(at))
                        wayland_clip_pulled(t->client, t->which, 0);
                return;
        }
}

static long compositor_wl_clip_pull(void *context, long client, long which)
{
        struct windows *table = compositor.table;
        positive now = clock_monotonic_nanoseconds();
        int row = -1, keep = -1, index = -1;
        long slot;

        (void)context;
        if (!table || which < 0 || which > 1 || !table->clip_on)
                return 0;
        for (int at = 0; at < (int)compositor_rows_in_use; at++)
                if (wayland_row_owner(at) == client && now - compositor_clip.gesture[at] <= COMPOSITOR_CLIP_GESTURE_NS &&
                    (row < 0 || at == compositor.focus))
                        row = at;
        slot = row >= 0 ? compositor_wl_transfer_place() : -1;
        if (slot < 0 || compositor_wl.wake < 0 || !compositor_wl_pull_open("moonwater-copy", &keep, &index))
                return 0;
        struct compositor_wl_transfer *t = &compositor_wl.transfer[slot];
        t->state = COMPOSITOR_WL_PULL, t->which = (unsigned char)which, t->client = (int)client, t->row = row;
        t->keep = keep, t->index = index, t->total = t->count = 0, t->text = -2;
        compositor_wl_pull_next(slot);
        return 1;
}

static void compositor_wl_clip_abort(void *context, long client, long which)
{
        (void)context;
        for (long at = 0; at < compositor_wl.transfers; at++)
                if (compositor_wl.transfer[at].state == COMPOSITOR_WL_PULL && compositor_wl.transfer[at].client == client &&
                    compositor_wl.transfer[at].which == which)
                {
                        if (compositor_wl.transfer[at].worker)
                                compositor_wl_transfer_cancel(at);
                        else
                                compositor_wl_transfer_free(at);
                }
}

static void compositor_wl_drag_pulled(long at, int ok);

static void compositor_wl_pull_end(long at, int ok)
{
        struct compositor_wl_transfer *t = &compositor_wl.transfer[at];
        const char *type = t->drop ? 0 : wayland_clip_type(t->client, t->which, t->reading);
        unsigned long got = t->got;

        if (t->drop) { compositor_wl_drag_pulled(at, ok); return; }
        system_close(t->in), t->in = -1;
        if (ok && type && t->reading == t->text)
        {
                long mapped = got ? compositor_call(syscall(mmap), 0, (positive)(t->total + got), 1, 1, (positive)t->keep, 0) : -22;
                ok = !(mapped < 0 && mapped > -4096) && windows_clip_text((const unsigned char *)mapped + t->total, got);
                if (!(mapped < 0 && mapped > -4096))
                        compositor_call(syscall(munmap), (positive)mapped, (positive)(t->total + got), 0, 0, 0, 0);
        }
        if (ok && type && windows_clip_record(t->index, t->total, got, t->reading == t->text ? WINDOWS_CLIP_TEXT : type,
                                              t->reading == t->text ? sizeof(WINDOWS_CLIP_TEXT) - 1 : string_length((string_address)type)))
                t->total += got, t->count++;
        else
                compositor_call(syscall(ftruncate), (positive)t->keep, (positive)t->total, 0, 0, 0, 0),
                compositor_call(syscall(lseek), (positive)t->keep, (positive)t->total, 0, 0, 0, 0);
        compositor_wl_pull_next(at);
}

static void compositor_wl_pull_whole(long at)
{
        struct compositor_wl_transfer *t = &compositor_wl.transfer[at];
        long client = t->client;
        unsigned int which = t->which;
        b32 ok = t->count && windows_clip_seal(t->keep, t->index, t->total, t->count) && compositor_clip_set_memfd(which, t->keep, t->row);
        compositor_wl_transfer_free(at);
        wayland_clip_pulled(client, (long)which, ok != 0);
}

static void compositor_wl_clip_push(void *context, long row, long which, const char *mime, long fd)
{
        (void)context;
        if (which < 0 || which > 1 || !compositor_clip_may_access((int)row, clock_monotonic_nanoseconds()))
                system_close((int)fd);
        else
                compositor_wl_push_start((unsigned int)which, wayland_row_owner(row), row, mime, fd, 1);
}

/* ---- drag and drop: Wayland bridge registered onto Canvas's native compositor_dnd_* engine ---- */

static void compositor_xwm_receive(long client, long which, const char *mime, long fd);
static long compositor_xwm_xdrag(long what, long row, long x, long y);
static int compositor_xwm_row_is_x(int row);

static int compositor_wl_drag_push(const char *mime, long fd)
{
        if (compositor_dnd.active != 1 || compositor_dnd.source < 0)
                return system_close((int)fd), 0;
        return compositor_wl_push_start(WINDOWS_CLIP_DRAG, -1, -1, mime, fd, 0);
}

static void compositor_wl_drag_pulled(long at, int ok)
{
        struct compositor_wl_transfer *t = &compositor_wl.transfer[at];
        unsigned long length = 0;
        int kept = -1;

        if (ok && windows_clip_record(t->index, 0, t->got, t->drop, string_length((string_address)t->drop)) &&
            windows_clip_seal(t->keep, t->index, t->got, 1))
                kept = compositor_clip_open(compositor.table->clip_pid, t->keep, &length);
        compositor_wl_transfer_free(at);
        compositor_wl.drag_reading = -1;
        if (kept < 0)
                wayland_drag_x_finished(0), compositor_dnd_end(0);
        else
                compositor_dnd_ext_ready(kept, length);
}

static int wl_dnd_active(void *ctx) { (void)ctx; return compositor_wl.started && (wayland_dragging() || wayland_x_dragging()); }
static int wl_dnd_begin(void *ctx, int source_row, const char *types, long types_bytes, unsigned int actions, int under, int wx, int wy)
{
        (void)ctx, (void)source_row;
        return compositor_wl.started && types && types_bytes > 0 ? wayland_drag_host_begin(types, types_bytes, actions, under, wx, wy) : 0;
}
static int wl_dnd_target_owns(void *ctx, int row) { (void)ctx; return compositor_wl.started && wayland_row_owner(row) >= 0; }
static long wl_dnd_target_event(void *ctx, int what, int row, int x, int y)
{
        (void)ctx;
        if (!compositor_wl.started)
                return 0;
        if (what == COMPOSITOR_DND_ENTER || what == COMPOSITOR_DND_MOTION)
        {
                if (wayland_dragging() && wayland.drag.held) wayland_drag_motion(row, x, y);
                return (long)wayland.drag.action;
        }
        if (what == COMPOSITOR_DND_LEAVE && wayland_dragging() && wayland.drag.held) wayland_drag_motion(-1, 0, 0);
        if (what == COMPOSITOR_DND_DROP)
        {
                if (wayland.drag.live && wayland.drag.held) wayland_drag_host_release();
                return wayland.drag.live != 0;
        }
        if (what == COMPOSITOR_DND_END && wayland.drag.live && wayland.drag.held) wayland_drag_cancel();
        return 0;
}
static void wl_dnd_status(void *ctx, int accept, unsigned int action) { (void)ctx; if (compositor_wl.started) wayland_drag_x_status(accept, action); }

static int wl_dnd_read_drop(void *ctx, const char *mime, int target_row)
{
        int fds[2] = {-1, -1}, keep = -1, index = -1;
        long slot = compositor_wl.wake >= 0 ? compositor_wl_transfer_place() : -1;
        (void)ctx;
        if (slot < 0 || !compositor_wl_pull_open("moonwater-drop", &keep, &index) ||
            compositor_call(syscall(pipe2), (positive)fds, 0x80000, 0, 0, 0, 0) < 0)
        {
                if (keep >= 0) system_close(keep);
                if (index >= 0) system_close(index);
                return wayland_drag_x_finished(0), 0;
        }
        compositor_call(syscall(fcntl), (positive)fds[0], 4, 0x800, 0, 0, 0);
        struct compositor_wl_transfer *t = &compositor_wl.transfer[slot];
        t->state = COMPOSITOR_WL_PULL, t->which = WINDOWS_CLIP_DRAG, t->client = -1, t->row = target_row;
        t->in = fds[0], t->keep = keep, t->index = index, t->total = t->count = 0, t->drop = mime;
        if (wayland_drag_source())
        {
                if (!wayland_drag_x_send(mime, fds[1])) system_close(fds[1]);
        }
        else
                compositor_xwm_receive(-1, 2, mime, fds[1]);
        if (compositor_wl_transfer_start(slot))
                return compositor_wl.drag_reading = slot, 1;
        return wayland_drag_x_finished(0), 0;
}

static void wl_dnd_finished(void *ctx, int ok) { (void)ctx; if (compositor_wl.started) wayland_drag_x_finished(ok); }
static void wl_dnd_cancel(void *ctx) { (void)ctx; if (compositor_wl.started && wayland_dragging()) wayland_drag_cancel(); }
static unsigned int wl_dnd_ext_types(void *ctx, const char **types_out, long *bytes_out)
{
        unsigned int actions = 0; (void)ctx; *bytes_out = wayland_drag_x_types(types_out, &actions); return actions;
}
static int wl_dnd_ext_action(void *ctx)
{
        (void)ctx; return compositor_wl.started && (wayland_dragging() || wayland_x_dragging()) ? (int)wayland.drag.action : -1;
}

static const struct compositor_drag_ops compositor_wl_drag_ops = {
        .active = wl_dnd_active, .begin = wl_dnd_begin, .target_owns = wl_dnd_target_owns, .target_event = wl_dnd_target_event,
        .status = wl_dnd_status, .read_drop = wl_dnd_read_drop, .finished = wl_dnd_finished, .cancel = wl_dnd_cancel,
        .ext_types = wl_dnd_ext_types, .ext_action = wl_dnd_ext_action,
};

static long compositor_wl_drag_host(long what, long row, long x, long y)
{
        if (what == WAYLAND_XDRAG_END)
        {
                if (compositor_wl.drag_reading >= 0 && compositor_wl.drag_reading < compositor_wl.transfers)
                        compositor_wl_transfer_cancel(compositor_wl.drag_reading), compositor_wl.drag_reading = -1;
                if (compositor_dnd.active)
                        compositor_dnd.action = (unsigned int)y, compositor_dnd_end((int)x);
                else
                        compositor_chip_drag(-1);
                return 0;
        }
        if (what == WAYLAND_XDRAG_LEAVE)
        {
                if (compositor_dnd.active == 2 && !compositor_dnd.dropped)
                {
                        if (compositor_dnd.target >= 0) compositor_dnd_tell(compositor_dnd.target, WINDOWS_DRAG_LEAVE, 0);
                        compositor_dnd.target = -1, compositor_dnd.take = -1, compositor_dnd.action = 0;
                }
                return 0;
        }
        if (what == WAYLAND_XDRAG_DROP)
        {
                if (compositor_dnd.active == 2) compositor_dnd_ext_drop();
                else wayland_drag_x_finished(0);
                return 0;
        }
        if (row < 0 || row >= COMPOSITOR_ROWS || !compositor.table || !compositor.table->drop_ok[row] || !compositor.table->pid[row])
                return 0;
        if (!compositor_dnd.active)
        {
                const char *types = 0; unsigned int actions = 0;
                wayland_drag_x_types(&types, &actions);
                compositor_dnd_ext_begin(&compositor_wl_drag_ops, actions);
        }
        if (compositor_dnd.active == 2)
        {
                int prev = compositor_dnd.target;
                compositor_dnd_name_ext_types();
                compositor_dnd.place = compositor_dnd_place((int)x, (int)y);
                if ((int)row != prev)
                {
                        if (prev >= 0) compositor_dnd_tell(prev, WINDOWS_DRAG_LEAVE, 0);
                        compositor_dnd.target = (int)row, compositor_dnd.take = -1, compositor_dnd.action = 0;
                        compositor_dnd_tell((int)row, WINDOWS_DRAG_ENTER, compositor_dnd.place);
                }
                else
                        compositor_dnd_tell((int)row, WINDOWS_DRAG_MOTION, compositor_dnd.place);
                return 1;
        }
        return 0;
}

static void compositor_wl_clip_clear(void *context, long client, long which)
{
        (void)context, (void)client;
        if (compositor.table && which >= 0 && which < 2 && compositor_clip.fd[which] >= 0)
                compositor_clip_clear((unsigned int)which);
}

static void compositor_wl_selection(unsigned int which) { if (compositor_wl.started) wayland_selection_changed((long)which); }

static void compositor_wl_xreceive(void *context, long client, long which, const char *mime, long fd)
{
        (void)context;
        if (compositor_dnd.active == 1 && compositor_dnd.source >= 0)
                compositor_wl_drag_push(mime, fd);
        else
                compositor_xwm_receive(client, which, mime, fd);
}

static long compositor_wl_xdrag(void *context, long what, long row, long x, long y)
{
        (void)context;
        if (what == WAYLAND_XDRAG_END)
                return compositor_xwm_xdrag(what, row, x, y), compositor_wl_drag_host(what, row, x, y);
        return compositor_xwm_row_is_x((int)row) ? compositor_xwm_xdrag(what, row, x, y) : compositor_wl_drag_host(what, row, x, y);
}

static void compositor_wl_transfers_drop(long client)
{
        for (long at = 0; at < compositor_wl.transfers; at++)
                if (compositor_wl.transfer[at].state && compositor_wl.transfer[at].client == client)
                        compositor_wl_transfer_cancel(at);
}

static positive compositor_wl_due(void)
{
        positive first = 0;
        long idle = wayland_idle_due();
        for (long slot = 0; slot < compositor_wl.slots; slot++)
        {
                long fd, due = compositor_wl.fd[slot] >= 0 ? wayland_stall_due(slot, &fd) : 0;
                positive at = due > 0 ? ((positive)due + 1) * 1000000ull : 0;
                if (at && (!first || at < first))
                        first = at;
        }
        if (idle > 0 && (!first || (positive)(idle + 1) * 1000000ull < first))
                first = (positive)(idle + 1) * 1000000ull;
        return first;
}

static void compositor_wl_table(void)
{
        long input = 0, inhibitors = 0, idled = 0, bells = 0, icons = 0;
        struct windows *table = compositor.table;

        wayland_idle_state(&input, &inhibitors, &idled);
        wayland_hint_counts(&bells, &icons);
        table->wl_input_ms = (unsigned long)input, table->wl_inhibitors = (unsigned int)inhibitors, table->wl_idled = (unsigned int)idled;
        table->wl_bells = (unsigned int)bells, table->wl_icons = (unsigned int)icons, table->wl_shortcuts = 0;
        for (long row = 0; row < (long)compositor_rows_in_use; row++)
        {
                long content, icon, modal, shortcuts;
                wayland_row_hints(row, &content, &icon, &modal, &shortcuts);
                table->wl_content[row] = (unsigned char)content, table->wl_modal[row] = (unsigned char)modal;
                table->wl_icon[row] = (unsigned char)icon, table->wl_shortcuts += shortcuts != 0;
        }
}

static void compositor_wl_input(void) { if (compositor_wl.started) wayland_input_seen(); }

static int compositor_wl_dmabuf_signaled(void *context, long fd)
{
        struct compositor_poll watch = {(int)fd, COMPOSITOR_POLLIN, 0};
        positive none[2] = {0, 0};
        (void)context;
        compositor_call(syscall(ppoll), (positive)&watch, 1, (positive)none, 0, 0, 0);
        return (watch.returned & COMPOSITOR_POLLIN) != 0;
}

static int compositor_wl_row_busy(void *context, int row)
{
        (void)context;
        return compositor_card_reads(row) ||
#ifdef MOONWATER_GPU_C
               (row >= 0 && compositor_wl.tiled_seq && row == compositor_wl.tiled_row && gpu_done(compositor_wl.tiled_seq) == 0) ||
#endif
               (row >= 0 && (row == compositor.direct_row || row == compositor.plane_row ||
                             (compositor.flip_pending && (row == compositor.flip_row || row == compositor.leaving_row))));
}

static const struct wayland_host compositor_wl_host = {
        0, compositor_wl_alloc, compositor_wl_release, compositor_wl_map, compositor_wl_remap, compositor_wl_unmap, compositor_wl_close,
        compositor_wl_row_open, compositor_wl_row_close, compositor_wl_row_picture, compositor_wl_row_title, compositor_wl_row_do,
        compositor_wl_row_facts, compositor_wl_screens, compositor_wl_now, compositor_wl_keymap, compositor_wl_repeat, compositor_wl_seat,
        compositor_wl_locks, compositor_wl_pointer_hold, compositor_wl_dmabuf_info, compositor_wl_dmabuf_table, compositor_wl_dmabuf_map,
        compositor_wl_clip_type, compositor_wl_clip_pull, compositor_wl_clip_abort, compositor_wl_clip_push, compositor_wl_clip_clear,
        compositor_wl_dmabuf_signaled, compositor_wl_random, compositor_wl_row_layers, compositor_wl_row_busy, compositor_wl_reserve,
        compositor_wl_commit, compositor_wl_unreserve, compositor_wl_xreceive, compositor_wl_xdrag, compositor_wl_row_app,
};

/* ---- the socket ---- */

/*
        The listening socket, made as the compositor starts: an old one at the path is taken away first (a compositor that
        crashed leaves it), the new one is open to every user, and no client waits on it: a connection waits in the kernel's
        queue (COMPOSITOR_WL_BACKLOG) until the loop accepts it.
*/
static const struct canvas_ext_ops compositor_wl_ext_ops = {
        .lent_info = wl_ext_lent_info,
        .row_flags = wl_ext_row_flags,
        .scan_fb = wl_ext_scan_fb,
        .scan_origin = wl_ext_scan_origin,
        .scan_fail = wl_ext_scan_fail,
        .row_opaque = wl_ext_row_opaque,
        .settle = wl_ext_settle,
        .straight_pays = wl_ext_straight_pays,
        .straight_settle = wl_ext_straight_settle,
        .straight_desc = wl_ext_straight_desc,
        .held = wl_ext_held,
        .count_card = wl_ext_count_card,
        .lent_sync = wl_ext_lent_sync,
        .tiled_settle = wl_ext_tiled_settle,
        .layers_frames = wl_ext_layers_frames,
        .gathers_fill = wl_ext_gathers_fill,
        .row_parts = wl_ext_row_parts,
        .asked_shape = wl_ext_asked_shape,
};

static void compositor_wl_register_hooks(void)
{
        canvas_ext_register(&compositor_wl_ext_ops);
        compositor_dnd_register(&compositor_wl_drag_ops);
        compositor_clip_observer_register(compositor_wl_selection);
}

static void compositor_wl_start(void)
{
        unsigned char address[110];
        long length = 0, got;

        compositor_wl_register_hooks();
        if (compositor_wl.started)
                return;
        compositor_wl.map_table.unit = sizeof(struct compositor_wl_mapping);
        // Layers off where the owner asks (/run/moonwater/layers-off), looked for once at the start: subsurfaces laid into their window as before.
        {
                bipolar off = system_open_at(AT_FDCWD, (string_address) "/run/moonwater/layers-off", FILE_READ | O_CLOEXEC);

                compositor_wl_layers.off = off >= 0;
                if (off >= 0)
                        system_close(off);
        }
        memory_zero(address, sizeof address);
        address[0] = 1; // AF_UNIX
        while (COMPOSITOR_WL_PATH[length])
        {
                address[2 + length] = (unsigned char)COMPOSITOR_WL_PATH[length];
                length++;
        }
        compositor_call(syscall(unlinkat), (positive)AT_FDCWD, (positive)COMPOSITOR_WL_PATH, 0, 0, 0, 0);
        // SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC
        got = compositor_call(syscall(socket), 1, 1 | 0x800 | 0x80000, 0, 0, 0, 0);
        if (got < 0)
                return;
        compositor_wl.listen = (int)got;
        if (compositor_call(syscall(bind), (positive)got, (positive)address, (positive)(2 + length + 1), 0, 0, 0) < 0 ||
            compositor_call(syscall(listen), (positive)got, COMPOSITOR_WL_BACKLOG, 0, 0, 0, 0) < 0)
        {
                system_close(compositor_wl.listen);
                compositor_wl.listen = -1;
                return;
        }
        compositor_call(syscall(fchmodat), (positive)AT_FDCWD, (positive)COMPOSITOR_WL_PATH, 0666, 0, 0, 0);
        for (long at = 0; at < compositor_wl.slots; at++)
                compositor_wl.fd[at] = -1;
        for (long at = 0; at < 16; at++)
                compositor_wl.keymap[at] = -1;
        compositor_wl.layout_told = compositor.keymap.layout;
        compositor_wl.screens_told = compositor_wl_screens_digest();
        compositor_wl_repeat(0, &compositor_wl.repeat_told[0], &compositor_wl.repeat_told[1]);
        if (wayland_start(&compositor_wl_host) < 0)
        {
                system_close(compositor_wl.listen);
                compositor_wl.listen = -1;
                return;
        }
        wayland_syncobj_host(&compositor_wl_syncobj);
        compositor_wl.started = 1;
        compositor_wl.zero_row = -1;
        // The X server's protocols go to the user the window manager's socket takes (compositor_xwm_accept), and to no other.
        wayland_xwayland_user(compositor_call(syscall(getuid), 0, 0, 0, 0, 0, 0));
        for (long at = 0; at < compositor_wl.transfers; at++)
                compositor_wl.transfer[at].in = compositor_wl.transfer[at].out = compositor_wl.transfer[at].keep = compositor_wl.transfer[at].index = -1;
        // EFD_CLOEXEC | EFD_NONBLOCK: what the transfers' threads knock on; with none, no copy or paste is taken.
        compositor_wl.wake = (int)compositor_call(syscall(eventfd2), 0, 0x80000 | 0x800, 0, 0, 0, 0);
        compositor_wl.hold_row = -1;
        // A paste's reader that has gone is an error of the write, not a signal that ends the compositor.
        system_signal_install(COMPOSITOR_WL_SIGPIPE, 1, 0, 0, null);
        // The timer that bounds a dma-buf's sync (compositor_wl_sync_start): a handler that does nothing, and no restart, so the wait ends.
        system_signal_install(COMPOSITOR_WL_SIGALRM, (positive)compositor_wl_alarm, SIGNAL_CATCH_FLAGS & ~SIGNAL_RESTART,
                              SIGNAL_CATCH_RESTORER, null);
        // The handler of a pool cut under a read: SA_SIGINFO and SA_NODEFER, with the library's restorer.
        compositor_wl.handler = system_signal_install(COMPOSITOR_WL_SIGBUS, (positive)compositor_wl_bus,
                                                      (SIGNAL_CATCH_FLAGS & ~SIGNAL_RESTART) | 4 | 0x40000000,
                                                      SIGNAL_CATCH_RESTORER, null);
}

/*
        The input thread's own send of a locked pointer's report (compositor_reader_direct, input.c): what it sends to is made here by the
        loop from the shim's state (compositor_wl_direct_make: the client whose window holds the pointer locked, its socket, the ids of its
        relative pointers and of its pointers that take a frame, its surface's scale), and both threads take one lock word for it: the
        reader with a try, which it never waits on (a lock it does not get is a report the loop sends), and the loop around every send to a
        client (compositor_wl_flush), every change of the hold and every client let go of. So the reader never sends after the loop sent an
        id's delete_id (the loop makes what the reader reads again, under the lock, as it sends), never to a socket the loop closed, never
        into the middle of a send of the loop's, and only to a client whose queue is empty (what the loop has for it goes first). A send the
        socket took part of is finished by the loop before its own queue (compositor_wl_direct.rest).
*/
#define COMPOSITOR_WL_DIRECT_IDS 4
#define COMPOSITOR_WL_DIRECT_TRIES 2000 // the reader's spins on the loop's hold of the lock, some 20 us
#if defined(__x86_64__)
#define COMPOSITOR_WL_SPIN() __asm__ volatile("pause" ::: "memory")
#elif defined(__aarch64__)
#define COMPOSITOR_WL_SPIN() __asm__ volatile("yield" ::: "memory")
#else
#define COMPOSITOR_WL_SPIN() __asm__ volatile("" ::: "memory")
#endif
static struct
{
        unsigned int lock;  // 1: one thread has it
        unsigned int depth; // the loop's own holds (its sends nest: a flush that lets a client go)
        int valid, slot, fd, row;
        long scale120, relatives, framed, ptrs;
        unsigned int relative[COMPOSITOR_WL_DIRECT_IDS], frames[COMPOSITOR_WL_DIRECT_IDS], ptr_ids[COMPOSITOR_WL_DIRECT_IDS];
        unsigned char ptr_ver[COMPOSITOR_WL_DIRECT_IDS];
        unsigned long *ptr_serial;
        int ptr_valid;
        // Unlocked fullscreen direct wl_pointer.motion target.
        int abs_valid, abs_slot, abs_fd, abs_row, abs_x, abs_y, abs_w, abs_h;
        long abs_scale120, abs_ptrs, abs_framed;
        unsigned int abs_ptr_ids[COMPOSITOR_WL_DIRECT_IDS], abs_frames[COMPOSITOR_WL_DIRECT_IDS];
        unsigned int rest_length; // bytes of the reader's last send the socket did not take
        int rest_slot;            // the client they are for
        unsigned char rest[COMPOSITOR_WL_DIRECT_IDS * 128];
        // The keys: the client of the window that has them, its socket and keyboards, where its serials are counted, the row, and the keys
        // that are not the client's alone with the modifiers held now (a modifier, a lock, a volume key, a chord the script binds).
        int keys_valid, keys_slot, keys_fd, keys_row;
        long keys_count;
        unsigned int keys_ids[COMPOSITOR_WL_DIRECT_IDS];
        unsigned long *keys_serial;
        unsigned char keys_refused[32];
        // Dirty-check snapshot so steady-state loop turns never spin on `lock`.
        unsigned int pub_valid, pub_gen;
        int pub_locked, pub_hold_row, pub_key_row, pub_kb_row, pub_ptr_row, pub_focus, pub_drag, pub_resize, pub_chip, pub_fs, pub_ov, pub_keys_to;
        unsigned int pub_mods, pub_stack_gen, pub_binds, pub_assist, pub_cycle, pub_keys_any;
        int pub_ax, pub_ay, pub_aw, pub_ah;
} compositor_wl_direct = {.slot = -1, .fd = -1, .row = -1, .abs_slot = -1, .abs_fd = -1, .abs_row = -1, .rest_slot = -1, .keys_slot = -1, .keys_fd = -1, .keys_row = -1};

static void compositor_wl_direct_hold(void)
{
        if (compositor_wl_direct.depth++)
                return;
        while (__atomic_exchange_n(&compositor_wl_direct.lock, 1u, __ATOMIC_ACQUIRE))
                while (__atomic_load_n(&compositor_wl_direct.lock, __ATOMIC_RELAXED))
                        COMPOSITOR_WL_SPIN();
}

static void compositor_wl_direct_release(void)
{
        if (!--compositor_wl_direct.depth)
                __atomic_store_n(&compositor_wl_direct.lock, 0u, __ATOMIC_RELEASE);
}

// The loop, holding the lock: what the reader sends to made again from the shim as it is now (nothing, when no client holds it locked).
static void compositor_wl_direct_make(void)
{
        unsigned int was = __atomic_load_n(&compositor_spark_want, __ATOMIC_RELAXED);
        long slot;

        compositor_wl_direct.valid = compositor_wl_direct.ptr_valid = compositor_wl_direct.abs_valid = 0;
        if (!compositor_reader.on || !compositor_wl.started)
                goto sync_spark;
        if (compositor_wl_locked())
        {
                int row = compositor_wl.hold_row;

                slot = wayland_pointer_direct(row, compositor_wl_direct.relative, &compositor_wl_direct.relatives,
                                              compositor_wl_direct.frames, &compositor_wl_direct.framed, COMPOSITOR_WL_DIRECT_IDS,
                                              &compositor_wl_direct.scale120) - 1;
                if (slot >= 0 && slot < compositor_wl.slots && compositor_wl.fd[slot] >= 0 && !wayland_out_dirty(slot))
                {
                        compositor_wl_direct.slot = (int)slot, compositor_wl_direct.fd = compositor_wl.fd[slot], compositor_wl_direct.row = row;
                        compositor_wl_direct.valid = 1;
                        if (compositor.drag < 0 && compositor.resize < 0 && !compositor.assist_armed && !compositor.cycle &&
                            !compositor_input_override_query(row))
                        {
                                long pslot = wayland_pointer_direct_ptrs(row, compositor_wl_direct.ptr_ids, compositor_wl_direct.ptr_ver,
                                                                         &compositor_wl_direct.ptrs, compositor_wl_direct.frames, &compositor_wl_direct.framed,
                                                                         COMPOSITOR_WL_DIRECT_IDS, 0, &compositor_wl_direct.ptr_serial) - 1;
                                if (pslot == slot && compositor_wl_direct.ptrs > 0)
                                        compositor_wl_direct.ptr_valid = 1;
                        }
                }
        }
        else if (compositor.focus >= 0 && compositor.fullscreen[compositor.focus] && compositor.focus == wayland.pointer_row &&
                 compositor.drag < 0 && compositor.resize < 0 && !compositor.assist_armed && !compositor.cycle &&
                 !compositor.chip.kind && !compositor_input_override_query(compositor.focus) &&
                 (compositor_space_is_fs_row(compositor.focus) ||
                  (compositor.stacked > 0 && compositor.stack[compositor.stacked - 1] == compositor.focus)))
        {
                int row = compositor.focus;
                unsigned char dummy_ver[COMPOSITOR_WL_DIRECT_IDS];

                slot = wayland_pointer_direct_ptrs(row, compositor_wl_direct.abs_ptr_ids, dummy_ver, &compositor_wl_direct.abs_ptrs,
                                                   compositor_wl_direct.abs_frames, &compositor_wl_direct.abs_framed, COMPOSITOR_WL_DIRECT_IDS,
                                                   &compositor_wl_direct.abs_scale120, 0) - 1;
                if (slot >= 0 && slot < compositor_wl.slots && compositor_wl.fd[slot] >= 0 && compositor_wl_direct.abs_ptrs > 0)
                {
                        compositor_wl_direct.abs_slot = (int)slot, compositor_wl_direct.abs_fd = compositor_wl.fd[slot], compositor_wl_direct.abs_row = row;
                        compositor_wl_direct.abs_x = compositor.x[row], compositor_wl_direct.abs_y = compositor.y[row];
                        compositor_wl_direct.abs_w = (int)compositor.w[row], compositor_wl_direct.abs_h = (int)compositor.h[row];
                        compositor_wl_direct.abs_valid = 1;
                }
        }
sync_spark:
        if (was != (unsigned int)(compositor_wl_direct.valid || compositor_wl_direct.abs_valid))
        {
                unsigned int want = (unsigned int)(compositor_wl_direct.valid || compositor_wl_direct.abs_valid);
                __atomic_store_n(&compositor_spark_want, want, __ATOMIC_RELEASE);
                if (compositor_reader.wake >= 0)
                {
                        unsigned long long one = 1;

                        system_write_once(compositor_reader.wake, &one, sizeof one);
                }
        }
}

static int compositor_wl_key_row(void); // below: the row whose client has the keys

// The loop, holding the lock: the keys' part made again (nothing while a modifier but Shift is held, a hand moves or resizes a window, the snap
// assist or Alt-Tab is up, a drag holds the pointer, or the keys held went to another window).
static void compositor_wl_direct_keys_make(void)
{
        unsigned int modifiers;
        int row;
        long slot;

        compositor_wl_direct.keys_valid = 0;
        if (!compositor_reader.on || !compositor_wl.started || (row = compositor_wl_key_row()) < 0 || row != wayland.keyboard_row)
                return;
        modifiers = compositor_modifiers();
        if ((modifiers & (WINDOW_KEY_CONTROL | WINDOW_KEY_ALT | WINDOW_KEY_ALTGR | WINDOW_KEY_SUPER)) || compositor.drag >= 0 || compositor.resize >= 0 ||
            compositor.assist_armed || compositor.cycle)
                return;
        if (compositor.keys_to != row)
                for (unsigned int k = 0; k < sizeof compositor.keys_down; k++)
                        if (compositor.keys_down[k])
                                return;
        slot = wayland_keyboard_direct(row, compositor_wl_direct.keys_ids, &compositor_wl_direct.keys_count, COMPOSITOR_WL_DIRECT_IDS,
                                       &compositor_wl_direct.keys_serial) - 1;
        if (slot < 0 || slot >= compositor_wl.slots || compositor_wl.fd[slot] < 0)
                return;
        // The keys refused whatever is held, made once: the modifiers, the locks (Caps, Num, Scroll) and the volume keys.
        {
                static unsigned char always[32];
                static int made;

                if (!made)
                        for (unsigned int code = 0; code < 256; code++, made = 1)
                                if (compositor_held_bit(code) || code == CK_CAPSLOCK || code == 69 || code == 70 || code == CK_KEY_MUTE ||
                                    code == CK_KEY_VOLUME_DOWN || code == CK_KEY_VOLUME_UP || code == CK_KEY_MICMUTE)
                                        always[code >> 3] |= (unsigned char)(1u << (code & 7));
                memory_copy(compositor_wl_direct.keys_refused, always, sizeof always);
        }
        for (unsigned int at = 0; at < compositor_keys.count; at++)
                if ((modifiers & (WINDOW_KEY_SHIFT | WINDOW_KEY_CONTROL | WINDOW_KEY_ALT | WINDOW_KEY_SUPER)) == compositor_keys.modifiers[at])
                        compositor_wl_direct.keys_refused[compositor_keys.code[at] >> 3] |= (unsigned char)(1u << (compositor_keys.code[at] & 7));
        // Input override query (e.g. Steam overlay): Tab routes through loop (Shift+Tab); while active (2), no direct keys.
        {
                int ov = compositor_input_override_query(row);

                if (ov == 2)
                        return;
                if (ov)
                        compositor_wl_direct.keys_refused[CK_KEY_TAB >> 3] |= (unsigned char)(1u << (CK_KEY_TAB & 7));
        }
        compositor_wl_direct.keys_slot = (int)slot, compositor_wl_direct.keys_fd = compositor_wl.fd[slot], compositor_wl_direct.keys_row = row;
        compositor_wl_direct.keys_valid = 1;
}

static void compositor_wl_direct_publish(void)
{
        int locked, hold_row, key_row, kb_row, ptr_row, focus, drag, resize, chip, fs, ov, keys_to, ax = 0, ay = 0, aw = 0, ah = 0;
        unsigned int mods, keys_any = 0, gen = compositor_wl.direct_gen ^ wayland.seat_gen ^ (unsigned int)(wayland_dragging() ? 0x80000000u : 0u);

        if (!compositor_reader.on)
                return;
        locked = compositor_wl.started && compositor_wl_locked();
        hold_row = locked ? compositor_wl.hold_row : -1;
        key_row = compositor_wl.started ? compositor_wl_key_row() : -1;
        kb_row = compositor_wl.started ? (int)wayland.keyboard_row : -1;
        ptr_row = compositor_wl.started ? (int)wayland.pointer_row : -1;
        focus = compositor.focus;
        fs = focus >= 0 ? (int)compositor.fullscreen[focus] : 0;
        drag = compositor.drag, resize = compositor.resize, chip = (int)compositor.chip.kind;
        ov = key_row >= 0 ? compositor_input_override_query(key_row) : (focus >= 0 ? compositor_input_override_query(focus) : 0);
        mods = compositor_modifiers();
        keys_to = compositor.keys_to;
        if (keys_to != key_row)
                for (unsigned int k = 0; k < sizeof compositor.keys_down; k++)
                        if (compositor.keys_down[k]) { keys_any = 1; break; }
        if (focus >= 0 && fs)
                ax = compositor.x[focus], ay = compositor.y[focus], aw = (int)compositor.w[focus], ah = (int)compositor.h[focus];
        if (compositor_wl_direct.pub_valid && compositor_wl_direct.pub_gen == gen &&
            compositor_wl_direct.pub_locked == locked && compositor_wl_direct.pub_hold_row == hold_row &&
            compositor_wl_direct.pub_key_row == key_row && compositor_wl_direct.pub_kb_row == kb_row &&
            compositor_wl_direct.pub_ptr_row == ptr_row && compositor_wl_direct.pub_focus == focus &&
            compositor_wl_direct.pub_fs == fs && compositor_wl_direct.pub_drag == drag &&
            compositor_wl_direct.pub_resize == resize && compositor_wl_direct.pub_chip == chip &&
            compositor_wl_direct.pub_ov == ov && compositor_wl_direct.pub_mods == mods &&
            compositor_wl_direct.pub_keys_to == keys_to && compositor_wl_direct.pub_keys_any == keys_any &&
            compositor_wl_direct.pub_stack_gen == compositor.stack_gen && compositor_wl_direct.pub_binds == compositor_keys.count &&
            compositor_wl_direct.pub_assist == compositor.assist_armed && compositor_wl_direct.pub_cycle == (unsigned int)compositor.cycle &&
            compositor_wl_direct.pub_ax == ax && compositor_wl_direct.pub_ay == ay &&
            compositor_wl_direct.pub_aw == aw && compositor_wl_direct.pub_ah == ah)
                return;
        compositor_wl_direct_hold();
        compositor_wl_direct_make();
        compositor_wl_direct_keys_make();
        compositor_wl_direct.pub_valid = 1, compositor_wl_direct.pub_gen = gen;
        compositor_wl_direct.pub_locked = locked, compositor_wl_direct.pub_hold_row = hold_row;
        compositor_wl_direct.pub_key_row = key_row, compositor_wl_direct.pub_kb_row = kb_row, compositor_wl_direct.pub_ptr_row = ptr_row;
        compositor_wl_direct.pub_focus = focus, compositor_wl_direct.pub_fs = fs, compositor_wl_direct.pub_drag = drag;
        compositor_wl_direct.pub_resize = resize, compositor_wl_direct.pub_chip = chip, compositor_wl_direct.pub_ov = ov;
        compositor_wl_direct.pub_mods = mods, compositor_wl_direct.pub_keys_to = keys_to, compositor_wl_direct.pub_keys_any = keys_any;
        compositor_wl_direct.pub_stack_gen = compositor.stack_gen, compositor_wl_direct.pub_binds = compositor_keys.count;
        compositor_wl_direct.pub_assist = compositor.assist_armed, compositor_wl_direct.pub_cycle = (unsigned int)compositor.cycle;
        compositor_wl_direct.pub_ax = ax, compositor_wl_direct.pub_ay = ay, compositor_wl_direct.pub_aw = aw, compositor_wl_direct.pub_ah = ah;
        compositor_wl_direct_release();
}

/*
        The loop's keeping of a key the input thread sent itself (its note, in the ring's order): what compositor_key and compositor_wl_key
        keep of a key a client's window was given (the moon's hand, the key trace, the on-screen keyboard's last input, the keys down and the
        window they went to, the clipboard's gesture), the shim's (wayland_key_noted: the keys down, the serial among the client's), and
        what any read of a device keeps (the idle clock, the card's wake, the processor's hold).
*/
static void compositor_wl_key_noted(unsigned int at, unsigned int code, int value, unsigned long serial, int row, positive stamp, positive read,
                                    positive sent)
{
        positive now = clock_monotonic_nanoseconds();
        unsigned char mask = (unsigned char)(1u << (code & 7)), *down = &compositor.keys_down[(code & 255) >> 3];

        taskbar_moon_seen(now);
        if (value == 1)
        {
                // The trace's read and send are the reader's own (its stamps in the note), not this keeping of them a turn later.
                compositor_keytrace_read(code, stamp, read ? read : now);
                if (at < COMPOSITOR_INPUTS && (compositor.input_kinds[at] & WINDOWS_INPUT_KEYBOARD))
                        compositor.osk_typed = 1;
                if (compositor.keys_to != row)
                        compositor_keys_release();
                *down |= mask;
                compositor.keys_to = row;
                compositor_clip_gesture(row);
        }
        else
                *down &= (unsigned char)~mask;
        wayland_key_noted(row, code, value, serial);
        if (value == 1)
                compositor_keytrace_direct(row, sent);
        compositor_wl_input();
#ifdef MOONWATER_GPU_C
        compositor_gpu_wake();
#endif
        compositor_hold_touch(now);
}

// The thread's send of the bytes of one event to a client (under the lock): 1 when they went, whole or in part (the loop sends the rest).
static int compositor_wl_direct_bytes(int slot, int fd, const unsigned char *bytes, unsigned int length)
{
        long got = compositor_call(syscall(sendto), (positive)fd, (positive)bytes, length, 0x40 | 0x4000, 0, 0); // MSG_DONTWAIT | MSG_NOSIGNAL

        if (got > 0 && got < (long)length)
        {
                compositor_wl_direct.rest_length = length - (unsigned int)got, compositor_wl_direct.rest_slot = slot;
                memory_copy(compositor_wl_direct.rest, bytes + got, compositor_wl_direct.rest_length);
        }
        return got > 0;
}

// The lock taken by the thread: waited for some 20 us at the most (COMPOSITOR_WL_DIRECT_TRIES); 0 when the event is the loop's.
static int compositor_wl_direct_try(void)
{
        for (unsigned int tries = 0; __atomic_exchange_n(&compositor_wl_direct.lock, 1u, __ATOMIC_ACQUIRE); tries++)
        {
                if (tries == COMPOSITOR_WL_DIRECT_TRIES)
                        return 0;
                COMPOSITOR_WL_SPIN();
        }
        return 1;
}

/*
        The thread's send of a key to the window that has the keys (compositor_reader_key): wl_keyboard.key (event 3: serial, time, key,
        state) to each of its keyboards, the words wayland_key sends, with a serial of the client's own count. 1 with the serial and the row
        when it went, 0 when the key is the loop's (no window of a client's has the keys as made, the key is refused with the modifiers
        held, the lock is the loop's, a rest of a send is still out, a release whose press went to another window: to, -1 for a press).
*/
HOT static int compositor_wl_direct_key(unsigned int code, int pressed, unsigned long *serial, int *row, int to)
{
        unsigned char bytes[COMPOSITOR_WL_DIRECT_IDS * 24];
        unsigned int length = 0, time;
        int sent;

        if (code >= 256 || !compositor_wl_direct_try())
                return 0;
        if (!compositor_wl_direct.keys_valid || compositor_wl_direct.rest_length || (to >= 0 && to != compositor_wl_direct.keys_row) ||
            (compositor_wl_direct.keys_refused[code >> 3] >> (code & 7) & 1))
        {
                __atomic_store_n(&compositor_wl_direct.lock, 0u, __ATOMIC_RELEASE);
                return 0;
        }
        *serial = __atomic_add_fetch(compositor_wl_direct.keys_serial, 1, __ATOMIC_RELAXED);
        *row = compositor_wl_direct.keys_row;
        time = (unsigned int)(clock_monotonic_nanoseconds() / 1000000ull);
        for (long at = 0; at < compositor_wl_direct.keys_count; at++)
        {
                unsigned int words[6] = {compositor_wl_direct.keys_ids[at], 24u << 16 | 3u, (unsigned int)*serial, time, code, pressed ? 1u : 0u};

                memory_copy(bytes + length, words, sizeof words);
                length += sizeof words;
        }
        sent = compositor_wl_direct_bytes(compositor_wl_direct.keys_slot, compositor_wl_direct.keys_fd, bytes, length);
        __atomic_store_n(&compositor_wl_direct.lock, 0u, __ATOMIC_RELEASE);
        return sent;
}

static void compositor_wl_button_noted(unsigned int code, int pressed, unsigned long serial, int row)
{
        unsigned int bit = code >= COMPOSITOR_BTN_LEFT && code <= COMPOSITOR_BTN_TASK ? (1u << (code - COMPOSITOR_BTN_LEFT)) : 0u;
        positive now = clock_monotonic_nanoseconds();

        if (pressed)
                compositor.logo_alone = 0, compositor.button_down |= bit;
        else
                compositor.button_down &= ~bit;
        if (pressed && row >= 0)
                compositor_clip_gesture(row);
        wayland_pointer_noted(row, pressed, serial);
        compositor_wl_input();
#ifdef MOONWATER_GPU_C
        compositor_gpu_wake();
#endif
        compositor_hold_touch(now);
}

// The reader's send of one locked-pointer packet (relative motion, button, and/or wheel) in one sendto(): 1 when it went, 0 when it is the loop's.
HOT static int compositor_wl_direct_send_packet(long dx, long dy, unsigned int btn, int btn_val, int wheel_v120, int wheel_horiz,
                                            positive micro, unsigned long *btn_serial, int *btn_row)
{
        unsigned char bytes[COMPOSITOR_WL_DIRECT_IDS * 128];
        unsigned int length = 0, time_ms;
        long got;

        if (!compositor_wl_direct_try())
                return 0;
        if (!compositor_wl_direct.valid || compositor_wl_direct.rest_length ||
            ((btn || wheel_v120) && !compositor_wl_direct.ptr_valid))
        {
                __atomic_store_n(&compositor_wl_direct.lock, 0u, __ATOMIC_RELEASE);
                return 0;
        }
        time_ms = (unsigned int)(micro / 1000ull);
        if (dx || dy)
        {
                for (long at = 0; at < compositor_wl_direct.relatives; at++)
                {
                        unsigned int words[8] = {compositor_wl_direct.relative[at], 32u << 16, (unsigned int)(micro >> 32), (unsigned int)micro,
                                                 (unsigned int)wayland_fixed_of(dx, compositor_wl_direct.scale120),
                                                 (unsigned int)wayland_fixed_of(dy, compositor_wl_direct.scale120),
                                                 (unsigned int)wayland_fixed_of(dx, compositor_wl_direct.scale120),
                                                 (unsigned int)wayland_fixed_of(dy, compositor_wl_direct.scale120)};

                        memory_copy(bytes + length, words, sizeof words);
                        length += sizeof words;
                }
        }
        if (btn)
        {
                unsigned long ser = __atomic_add_fetch(compositor_wl_direct.ptr_serial, 1, __ATOMIC_RELAXED);
                if (btn_serial) *btn_serial = ser;
                if (btn_row) *btn_row = compositor_wl_direct.row;
                for (long at = 0; at < compositor_wl_direct.ptrs; at++)
                {
                        unsigned int words[6] = {compositor_wl_direct.ptr_ids[at], 24u << 16 | 3u, (unsigned int)ser, time_ms, btn, btn_val ? 1u : 0u};

                        memory_copy(bytes + length, words, sizeof words);
                        length += sizeof words;
                }
        }
        if (wheel_v120)
        {
                unsigned int axis = wheel_horiz ? 1u : 0u;
                int v = wheel_horiz ? wheel_v120 : -wheel_v120;
                unsigned int fixed;
                int notches;

                if (compositor.table && (compositor.table->scroll_off & WINDOW_SCROLL_FLIP))
                        v = -v;
                fixed = (unsigned int)((long long)v * 15 * 256 / 120);
                notches = v / 120;

                for (long at = 0; at < compositor_wl_direct.ptrs; at++)
                {
                        unsigned int id = compositor_wl_direct.ptr_ids[at];
                        unsigned char ver = compositor_wl_direct.ptr_ver[at];

                        if (ver >= 5)
                        {
                                unsigned int src[3] = {id, 12u << 16 | 6u, 0u}; // wl_pointer.axis_source = wheel
                                memory_copy(bytes + length, src, sizeof src);
                                length += sizeof src;
                        }
                        if (ver >= 8)
                        {
                                unsigned int v120w[4] = {id, 16u << 16 | 9u, axis, (unsigned int)v}; // wl_pointer.axis_value120
                                memory_copy(bytes + length, v120w, sizeof v120w);
                                length += sizeof v120w;
                        }
                        else if (ver >= 5 && notches)
                        {
                                unsigned int disc[4] = {id, 16u << 16 | 8u, axis, (unsigned int)notches}; // wl_pointer.axis_discrete
                                memory_copy(bytes + length, disc, sizeof disc);
                                length += sizeof disc;
                        }
                        {
                                unsigned int ax[5] = {id, 20u << 16 | 4u, time_ms, axis, fixed}; // wl_pointer.axis
                                memory_copy(bytes + length, ax, sizeof ax);
                                length += sizeof ax;
                        }
                }
        }
        for (long at = 0; at < compositor_wl_direct.framed; at++)
        {
                unsigned int words[2] = {compositor_wl_direct.frames[at], 8u << 16 | 5u};

                memory_copy(bytes + length, words, sizeof words);
                length += sizeof words;
        }
        got = compositor_wl_direct_bytes(compositor_wl_direct.slot, compositor_wl_direct.fd, bytes, length);
        __atomic_store_n(&compositor_wl_direct.lock, 0u, __ATOMIC_RELEASE);
        return (int)got;
}

static int compositor_wl_direct_send(long dx, long dy, positive micro)
{
        return compositor_wl_direct_send_packet(dx, dy, 0, 0, 0, 0, micro, 0, 0);
}

// Unlocked fullscreen direct wl_pointer.motion + wl_pointer.frame from the SCHED_FIFO reader thread.
HOT static void compositor_wl_direct_abs(int x, int y, positive now)
{
        unsigned char bytes[COMPOSITOR_WL_DIRECT_IDS * 28];
        unsigned int length = 0, time_ms;
        long sx, sy;

        if (!__atomic_load_n(&compositor_wl_direct.abs_valid, __ATOMIC_RELAXED))
                return;
        if (!compositor_wl_direct_try())
                return;
        if (!compositor_wl_direct.abs_valid || compositor_wl_direct.rest_length ||
            x < compositor_wl_direct.abs_x || y < compositor_wl_direct.abs_y ||
            x >= compositor_wl_direct.abs_x + compositor_wl_direct.abs_w || y >= compositor_wl_direct.abs_y + compositor_wl_direct.abs_h)
        {
                __atomic_store_n(&compositor_wl_direct.lock, 0u, __ATOMIC_RELEASE);
                return;
        }
        sx = wayland_fixed_of(x - compositor_wl_direct.abs_x, compositor_wl_direct.abs_scale120);
        sy = wayland_fixed_of(y - compositor_wl_direct.abs_y, compositor_wl_direct.abs_scale120);
        time_ms = (unsigned int)(now / 1000000ull);
        for (long at = 0; at < compositor_wl_direct.abs_ptrs; at++)
        {
                unsigned int words[5] = {compositor_wl_direct.abs_ptr_ids[at], 20u << 16 | 2u, time_ms, (unsigned int)sx, (unsigned int)sy};

                memory_copy(bytes + length, words, sizeof words);
                length += sizeof words;
        }
        for (long at = 0; at < compositor_wl_direct.abs_framed; at++)
        {
                unsigned int words[2] = {compositor_wl_direct.abs_frames[at], 8u << 16 | 5u};

                memory_copy(bytes + length, words, sizeof words);
                length += sizeof words;
        }
        compositor_wl_direct_bytes(compositor_wl_direct.abs_slot, compositor_wl_direct.abs_fd, bytes, length);
        __atomic_store_n(&compositor_wl_direct.lock, 0u, __ATOMIC_RELEASE);
}

static int compositor_wl_direct_clean(void)
{
        return (!compositor_wl_direct.valid || !wayland_out_dirty(compositor_wl_direct.slot)) &&
               (!compositor_wl_direct.abs_valid || !wayland_out_dirty(compositor_wl_direct.abs_slot));
}

// The loop, holding the lock, before the client's own queue: the rest of the reader's send, which goes first. 0 when it is not all out.
static int compositor_wl_direct_rest(long slot)
{
        long got;

        if (!compositor_wl_direct.rest_length || compositor_wl_direct.rest_slot != slot)
                return 1;
        got = compositor_call(syscall(sendto), (positive)compositor_wl.fd[slot], (positive)compositor_wl_direct.rest, compositor_wl_direct.rest_length,
                              0x40 | 0x4000, 0, 0);
        if (got <= 0)
                return got == -11 ? 0 : 1; // EAGAIN: later; a socket that fails is the flush's to find
        compositor_wl_direct.rest_length -= (unsigned int)got;
        for (unsigned int at = 0; at < compositor_wl_direct.rest_length; at++)
                compositor_wl_direct.rest[at] = compositor_wl_direct.rest[at + (unsigned int)got];
        return !compositor_wl_direct.rest_length;
}

static void compositor_wl_drop(long slot)
{
        compositor_wl_direct_hold();
        if (compositor_wl_direct.slot == slot)
                compositor_wl_direct.valid = compositor_wl_direct.ptr_valid = 0;
        if (compositor_wl_direct.abs_slot == slot)
                compositor_wl_direct.abs_valid = 0;
        if (compositor_wl_direct.keys_slot == slot)
                compositor_wl_direct.keys_valid = 0;
        if (compositor_wl_direct.rest_slot == slot)
                compositor_wl_direct.rest_length = 0;
        if (compositor_wl.fd[slot] >= 0)
                system_close(compositor_wl.fd[slot]);
        compositor_wl.fd[slot] = -1;
        compositor_wl.direct_gen++;
        compositor_wl_direct_release();
        wayland_drop(slot);
        compositor_wl_transfers_drop(slot);
}


/* One connection, if one waits: its peer is read from the socket, and it takes a slot unless the caps say no. */
static void compositor_wl_accept(void)
{
        for (int tries = 0; tries < COMPOSITOR_WL_ACCEPTS; tries++)
        {
                int credentials[3] = {0, 0, 0};
                unsigned int size = sizeof credentials;
                long fd = compositor_call(syscall(accept4), (positive)compositor_wl.listen, 0, 0, 0x800 | 0x80000, 0, 0), slot;

                if (fd < 0)
                        return;
                // The clients' share of the descriptor table is full (compositor_descriptor_crowded): the connection is taken off the
                // socket and closed, so the socket does not stay readable and the numbers left are the compositor's own.
                if (compositor_descriptor_crowded(fd))
                {
                        system_close((int)fd);
                        continue;
                }
                // SOL_SOCKET, SO_PEERCRED: the kernel's word for who is on the other end, not the client's.
                if (compositor_call(syscall(getsockopt), (positive)fd, 1, 17, (positive)credentials, (positive)&size, 0) < 0)
                {
                        system_close((int)fd);
                        continue;
                }
                slot = wayland_connect(credentials[0], credentials[1]);
                // A slot the host has no room for yet: its arrays grown with the shim's, or the client refused when the memory is not there.
                if (slot >= 0 && !compositor_wl_slots_reach(slot + 1))
                {
                        wayland_drop(slot);
                        slot = -1;
                }
                if (slot < 0)
                {
                        system_close((int)fd);
                        continue;
                }
                compositor_wl.fd[slot] = (int)fd;
                compositor_wl.barred[slot] = 0;
                compositor_wl.pid[slot] = credentials[0];
                compositor_wl.client[slot] = (struct compositor_wl_client){.uid = credentials[1], .asked = 0, .slow_until = 0};
                compositor_wl.direct_gen++;
        }
}

/*
        What a client has sent, a bounded amount of it: up to COMPOSITOR_WL_READS reads of COMPOSITOR_WL_READ bytes with the
        descriptors that came, to the shim. A client that sends faster than this has the rest next pass, after everyone
        else's turn. A stream that ended or failed, or control data that did not fit, is a client gone.
*/
static void compositor_wl_read(long slot)
{
        static unsigned char bytes[COMPOSITOR_WL_READ];

        // A client whose events back up (it is not reading them) is not read until it has: every request may queue a reply.
        for (int reads = 0; reads < COMPOSITOR_WL_READS && !wayland_out_backlog(slot); reads++)
        {
                positive control[(sizeof(positive) * 3 + WAYLAND_FDS * sizeof(int) + 15) / sizeof(positive) + 2];
                positive message[7], vector[2] = {(positive)bytes, sizeof bytes};
                long got, fds[WAYLAND_FDS], count = 0;
                int excess = 0;

                memory_zero(message, sizeof message);
                memory_zero(control, sizeof control);
                message[2] = (positive)vector;
                message[3] = 1;
                message[4] = (positive)control;
                message[5] = sizeof control;
                // MSG_DONTWAIT | MSG_CMSG_CLOEXEC
                got = compositor_call(syscall(recvmsg), (positive)compositor_wl.fd[slot], (positive)message, 0x40 | 0x40000000, 0, 0, 0);
                if (got == -11) // EAGAIN
                        return;
                if (got <= 0)
                {
                        compositor_wl_drop(slot);
                        return;
                }
                // The descriptors, from the control data: SCM_RIGHTS in SOL_SOCKET. The kernel puts as many in this process as
                // the control buffer holds, which is more than the shim takes (36 against 28): one past the shim's are closed
                // here, and the client is one that sent more than it may, so none of them stays open in the compositor.
                for (unsigned char *at = (unsigned char *)control; message[5] >= 16 && at < (unsigned char *)control + message[5];)
                {
                        unsigned long length = *(unsigned long *)at;
                        int level = *(int *)(at + 8), type = *(int *)(at + 12);

                        if (length < 16 || at + length > (unsigned char *)control + message[5])
                                break;
                        if (level == 1 && type == 1)
                                for (unsigned long item = 16; item + 4 <= length; item += 4)
                                        if (count < WAYLAND_FDS)
                                                fds[count++] = *(int *)(at + item);
                                        else
                                        {
                                                system_close(*(int *)(at + item));
                                                excess = 1;
                                        }
                        at += (length + 7) & ~7ul;
                }
                // A descriptor numbered in the compositor's own share of the table: the clients' share is full, and this client is the one
                // that is refused the room, with what it sent (compositor_descriptor_crowded).
                for (long item = 0; item < count; item++)
                        excess |= compositor_descriptor_crowded(fds[item]);
                // MSG_CTRUNC: descriptors were lost, and what is left of the stream can no longer be trusted to line up.
                if ((message[6] & 0x8) || excess)
                {
                        for (long item = 0; item < count; item++)
                                system_close((int)fds[item]);
                        compositor_wl_drop(slot);
                        return;
                }
                if (wayland_feed(slot, bytes, got, fds, count) < 0)
                        return;
                // A commit that waits for its GPU holds what follows it: no more is read until it is made.
                if (got < (long)sizeof bytes || wayland_stalled(slot))
                        return;
        }
}

/*
        What the shim has queued for a client, written: the bytes with the descriptors that go with them, once. What the
        socket takes is taken off the queue and the rest waits for the loop to find the socket writable. A client whose
        queue is full is dead (the shim says so), and one that was sent a protocol error is dropped once it is sent.
*/
static void compositor_wl_flush_held(long slot);

static void compositor_wl_flush(long slot)
{
        // Only the socket the input thread sends to is shared with it; a hold it does not know of yet is made known at the next send.
        if (!compositor_reader.on || ((compositor_wl_direct.valid ? compositor_wl_direct.slot != slot : !compositor_wl_locked()) &&
                                      (compositor_wl_direct.keys_valid ? compositor_wl_direct.keys_slot != slot : wayland.keyboard_row < 0) &&
                                      (!compositor_wl_direct.abs_valid || compositor_wl_direct.abs_slot != slot) &&
                                      compositor_wl_direct.rest_slot != slot))
        {
                compositor_wl_flush_held(slot);
                return;
        }
        compositor_wl_direct_hold();
        if (compositor_wl_direct_rest(slot))
                compositor_wl_flush_held(slot);
        // Only rebuild direct tables when seat/direct generation changed; avoids holding the reader spinlock across table scans on every frame flush.
        {
                unsigned int gen = compositor_wl.direct_gen ^ wayland.seat_gen ^ (unsigned int)(wayland_dragging() ? 0x80000000u : 0u);
                if (!compositor_wl_direct.pub_valid || compositor_wl_direct.pub_gen != gen)
                {
                        compositor_wl_direct_make();
                        compositor_wl_direct_keys_make();
                        compositor_wl_direct.pub_valid = 0;
                }
        }
        compositor_wl_direct_release();
}

static void compositor_wl_flush_held(long slot)
{
        const unsigned char *bytes;
        const long *fds;
        long fd_count, length, got;
        int dead;

        if (compositor_wl.fd[slot] < 0)
                return;
        dead = (int)wayland_dead(slot);
        length = wayland_out(slot, &bytes, &fds, &fd_count);
        if (dead == 2)
        {
                compositor_wl_drop(slot);
                return;
        }
        if (length)
        {
                if (fd_count <= 0)
                {
                        // Fast path (99.9% of flushes): sendto() skips kernel copy_msghdr_from_user + iovec overhead.
                        got = compositor_call(syscall(sendto), (positive)compositor_wl.fd[slot], (positive)bytes, (positive)length, 0x40 | 0x4000, 0, 0);
                }
                else
                {
                        positive control[2 + WAYLAND_OUT_FDS], vector[2] = {(positive)bytes, (positive)length};
                        positive message[7] = {0, 0, (positive)vector, 1, 0, 0, 0};

                        // The queue takes at most WAYLAND_OUT_FDS (the keymap, the format table and a copy's pipe each check it); the control buffer is sized for that.
                        if (fd_count > WAYLAND_OUT_FDS)
                                fd_count = WAYLAND_OUT_FDS;
                        memory_zero(control, sizeof control);
                        control[0] = 16 + (positive)fd_count * 4;
                        control[1] = 1 | (positive)1 << 32; // SOL_SOCKET, SCM_RIGHTS
                        for (long at = 0; at < fd_count; at++)
                                ((int *)(control + 2))[at] = (int)fds[at];
                        message[4] = (positive)control;
                        message[5] = ((16 + (positive)fd_count * 4) + 7) & ~(positive)7;
                        // MSG_DONTWAIT | MSG_NOSIGNAL
                        got = compositor_call(syscall(sendmsg), (positive)compositor_wl.fd[slot], (positive)message, 0x40 | 0x4000, 0, 0, 0);
                }
                if (got > 0)
                        wayland_sent(slot, got); // which closes a copy's pipe end that went with it: it is the shim's
                else if (got != -11)
                {
                        compositor_wl_drop(slot);
                        return;
                }
        }
        if (dead == 1 && !wayland_out(slot, &bytes, &fds, &fd_count))
                compositor_wl_drop(slot);
}

HOT static void compositor_wl_flush_all(void)
{
        for (long slot = wayland_out_dirty_next(0); slot >= 0 && slot < compositor_wl.slots; slot = wayland_out_dirty_next(slot + 1))
                if (compositor_wl.fd[slot] >= 0)
                        compositor_wl_flush(slot);
        compositor_keytrace_flushed();
}

/* Where the polls for the clients go in the loop's list, and what comes of them. COMPOSITOR_WL_POLLS is the most this adds, which
   the loop's list is sized by (COMPOSITOR_POLLS): the socket, every client, the transfers' knock, the dma-buf of every client
   stalled on one, and the X window manager's. An entry added here is added to it. */
#define COMPOSITOR_WL_POLLS (1 + compositor_wl.slots + 1 + compositor_wl.slots)
static unsigned int compositor_wl_poll(struct compositor_poll *wanted, unsigned int count)
{
        if (!compositor_wl.started)
                return count;
        wanted[count++] = (struct compositor_poll){compositor_wl.listen, COMPOSITOR_POLLIN, 0};
        compositor_wl.polled = compositor_wl.slots;
        for (long slot = 0; slot < compositor_wl.slots; slot++)
        {
                const unsigned char *bytes;
                const long *fds;
                long fd_count;
                // A client whose commit waits for its GPU is not read (a hangup is told all the same), and the loop wakes to ask.
                // Nor is one whose queue of events is past its high mark (wayland_out_backlog): it is polled for room, and read again
                // once a send has taken it below.
                short events = compositor_wl.fd[slot] >= 0 && (wayland_stalled(slot) || wayland_out_backlog(slot)) ? 0 : COMPOSITOR_POLLIN;

                if (compositor_wl.fd[slot] >= 0 && wayland_out_dirty(slot) && wayland_out(slot, &bytes, &fds, &fd_count))
                        events |= COMPOSITOR_POLLOUT;
                wanted[count++] = (struct compositor_poll){compositor_wl.fd[slot], events, 0};
        }
        // The knock of a transfer's thread that has ended, after the clients (a descriptor of -1, with no eventfd, is not polled).
        wanted[count++] = (struct compositor_poll){compositor_wl.wake, COMPOSITOR_POLLIN, 0};
        // The dma-bufs that stalled clients wait for, readable when their GPU is done (nothing is read of them: the pass resumes).
        for (long slot = 0; slot < compositor_wl.slots; slot++)
        {
                long fd = -1;

                if (compositor_wl.fd[slot] >= 0 && wayland_stall_due(slot, &fd) && fd >= 0)
                        wanted[count++] = (struct compositor_poll){(int)fd, COMPOSITOR_POLLIN, 0};
        }
        return count;
}

static void compositor_wl_target(void); // the vblank a commit for a time is held against, said to the shim (below)

static void compositor_wl_target_at(positive now);

/*
        The clients a quiet turn woke (compositor_quietshot) read before the rest of the turn, when every one of the descriptors the poll
        answered (awaited) is a client's socket that answered POLLIN alone: whether every request they sent was quiet (wayland_heard: a
        lock's hint, a commit with nothing pending), none of them waits now for its GPU, its budget or a hold or went, and what they were
        answered went out. A read that was not quiet has been dispatched as the turn's wl pass would have (after the vblank a commit for a
        time is held against, said to the shim at the turn's now), and the turn goes on whole (its pass reads the sockets again and finds
        nothing). A client that waits and was not answered is the whole turn's to resume, at its descriptor's answer or its time.
*/
static b32 compositor_wl_quiet_read(const struct compositor_poll *wanted, long awaited, positive now)
{
        long heard, quiet, heard_now, quiet_now, answered = 0, polled = compositor_wl.polled < compositor_wl.slots ? compositor_wl.polled : compositor_wl.slots;

        for (long slot = 0; slot < polled; slot++)
                if (wanted[1 + slot].returned)
                {
                        if (wanted[1 + slot].returned != COMPOSITOR_POLLIN || compositor_wl.fd[slot] < 0)
                                return 0;
                        answered++;
                }
        if (answered != awaited)
                return 0;
        compositor_wl_target_at(now);
        heard = wayland_heard(&quiet);
        for (long slot = 0; slot < polled; slot++)
                if (wanted[1 + slot].returned)
                        compositor_wl_read(slot);
        heard_now = wayland_heard(&quiet_now);
        for (long slot = 0; slot < polled; slot++)
        {
                if (!wanted[1 + slot].returned)
                        continue;
                if (compositor_wl.fd[slot] < 0 || wayland_stalled(slot) || wayland_dead(slot))
                        return 0;
                if (wayland_out_dirty(slot))
                        compositor_wl_flush(slot);
                if (wayland_out_dirty(slot))
                        return 0;
        }
        return heard_now - heard == quiet_now - quiet;
}

static void compositor_wl_pass(const struct compositor_poll *wanted)
{
        if (!compositor_wl.started)
                return;
        compositor_wl_target();
        // The commits that waited for their GPU, made when it is done, before what else the clients sent this pass.
        for (long slot = 0; slot < compositor_wl.slots; slot++)
                if (compositor_wl.fd[slot] >= 0 && wayland_stalled(slot))
                {
                        wayland_resume(slot);
                        if (wayland_dead(slot))
                                compositor_wl_flush(slot);
                }
        long polled = compositor_wl.polled < compositor_wl.slots ? compositor_wl.polled : compositor_wl.slots;

        if (wanted[0].returned)
                compositor_wl_accept();
        for (long slot = 0; slot < polled; slot++)
        {
                short returned = wanted[1 + slot].returned;

                if (compositor_wl.fd[slot] < 0 || !returned)
                        continue;
                if (returned & COMPOSITOR_POLLIN)
                        compositor_wl_read(slot);
                else if (returned & (COMPOSITOR_POLLHUP | COMPOSITOR_POLLERR))
                {
                        compositor_wl_drop(slot);
                        continue;
                }
                if (compositor_wl.fd[slot] >= 0 && wayland_dead(slot))
                        compositor_wl_flush(slot);
        }
        // The transfers whose threads have ended: each thread ends itself at its deadline, so the loop waits on nothing else of them.
        if (wanted[1 + polled].returned)
                compositor_wl_transfers_done();
}

/* ---- input: the pointer and the keys, as a client is told of them ---- */

/*
        The window the pointer is over, for a client: the one a button is held on, else the one under it now (compositor_under, as
        a motion finds it), when it is a client's window and it is shown and is not being moved or resized. Just outside a window's
        picture is the edge the compositor resizes by, and no client's. Now, not where it last moved: a window that comes up under a
        resting pointer (a game going fullscreen, a window raised by a key) is entered at once, as a motion would have entered it,
        where it was entered only when the hand next moved, and a game's lock or confinement waited for that move: Xwayland then
        had the enter, the confinement and the lock together with the first report, and SDL2's mouse-look through Xwayland got
        counts that cancelled out (hasty-tail: -2 of 3999 counts in 8 of 10 runs).
*/
static int compositor_wl_pointer_row(void)
{
        int tab, row;

        // A pointer locked to a window is over that window, and stays where it was locked: no other window can be under it, so it is not looked
        // for (compositor_under, which walks the band's tree for a tab first: a third of a game's every report on hasty-tail, 8000 a second).
        if (compositor_wl.hold == 1 && compositor_wl.hold_row >= 0 && compositor.grab < 0 && compositor.drag < 0 && compositor.resize < 0 &&
            wayland_row_owner(compositor_wl.hold_row) >= 0 && compositor_visible(compositor_wl.hold_row))
                return compositor_wl.hold_row;
        row = compositor.grab >= 0 ? compositor.grab : compositor_under(compositor.pointer_x, compositor.pointer_y, &tab);

        if (row < 0 || compositor.drag >= 0 || compositor.resize >= 0 || wayland_row_owner(row) < 0 || !compositor_visible(row))
                return -1;
        if (compositor.grab < 0 &&
            (compositor.pointer_x < compositor.x[row] || compositor.pointer_y < compositor.y[row] ||
             compositor.pointer_x >= compositor.x[row] + (int)compositor.w[row] || compositor.pointer_y >= compositor.y[row] + (int)compositor.h[row]))
                return -1;
        return row;
}

/*
        The window the keys go to, for a client: the topmost popup of the window that has them, but a popup that did not ask
        to grab (a tooltip) is not given the keys, its window is. -1 when it is not a client's window.
*/
static int compositor_wl_key_row(void)
{
        int row = compositor.focus;

        if (row < 0)
                return -1;
        row = compositor_popup_top(row);
        while (row >= 0 && compositor_popup_parent[row] >= 0 && wayland_row_owner(row) >= 0 && !wayland_row_grabs(row))
                row = compositor_popup_parent[row];
        return wayland_row_owner(row) >= 0 ? row : -1;
}

// What the compositor's chords do for a key, for the window that has the keys: 0 they run, 1 the window has the key (its shortcuts
// inhibitor is active), 2 the one chord the inhibitor keeps (Super and Escape), which is the compositor's (wayland_shortcuts_key).
static int compositor_wl_suspends(unsigned int code, int value, int super)
{
        int row;

        if (!compositor_wl.started)
                return 0;
        row = compositor_wl_key_row();
        return row < 0 ? 0 : (int)wayland_shortcuts_key(row, (long)code, (long)value, super);
}

/*
        A key that is the window's own to read: the key as it is, to the client that has the keys, with Caps Lock kept here
        where the compositor keeps it, and no repeat (the client repeats at the rate it was told). The compositor's own
        reading of a key (its characters, its dead accents) is for its own windows, which a client does by its keymap. True
        when the key was a client's.
*/
static int compositor_wl_key(unsigned int code, int value)
{
        int row;

        if (!compositor_wl.started)
                return 0;
        if (compositor.focus < 0)
                compositor_refocus();
        row = compositor_wl_key_row();
        if (row < 0)
                return 0;
        // The window is told it has the keys before it is given one: the focus may have changed in this very pass (the first
        // key after a window was shown, or the first of all), and the shim is told of focus only once a pass otherwise.
        wayland_keyboard_to(row);
        if (code == CK_CAPSLOCK && value == 1)
                compositor.keymap.caps ^= 1;
        // The keys down, and the window they went to, as the compositor keeps them for its own windows.
        if (code < 256)
        {
                unsigned char mask = (unsigned char)(1u << (code & 7)), *down = &compositor.keys_down[code >> 3];

                if (value == 1)
                {
                        if (compositor.keys_to != row)
                                compositor_keys_release();
                        *down |= mask;
                        compositor.keys_to = row;
                }
                else if (!(*down & mask))
                        return 1;
                else if (!value)
                        *down &= (unsigned char)~mask;
        }
        if (value == 1)
                compositor_clip_gesture(row);
        if (value != 2)
                wayland_key(row, code, value);
        if (value == 1)
                compositor_keytrace_sent(row);
        return 1;
}

// A client's ring, read as the compositor posted it: a motion, a button, a turn of the wheel, and the releases of keys held.
static void compositor_wl_drain(int row)
{
        struct windows *table = compositor.table;
        unsigned int tail = table->event_tail[row], head = __atomic_load_n(&table->event_head[row], __ATOMIC_ACQUIRE);

        for (unsigned int taken = 0; tail != head && taken < WINDOWS_EVENTS; taken++, tail++)
        {
                unsigned int at = tail % WINDOWS_EVENTS, type = table->event_type[row][at], flags = table->event_flags[row][at];
                int a = __atomic_exchange_n(&table->event_a[row][at], 0, __ATOMIC_ACQ_REL), b = table->event_b[row][at];

                if (type == WINDOW_EVENT_KEY)
                {
                        // The keyboard's key carries the Shift it makes its character with (osk.c): the client is told Shift as a person's.
                        if (!(flags & WINDOW_KEY_REPEAT))
                                wayland_key_shift(row, a, (flags & WINDOW_KEY_DOWN) ? 1 : 0, (flags & WINDOW_KEY_SHIFT) ? 1 : 0);
                        continue;
                }
                if (type == WINDOW_EVENT_PINCH)
                {
                        // A trackpad's pinch: its phase, its scale (1024ths) and its focal point, packed as the window's pixels.
                        long phase = flags & WINDOW_PINCH_BEGIN ? 1 : flags & WINDOW_PINCH_END ? 2 : 0;

                        wayland_pointer_pinch(row, phase, a, (long)(((unsigned int)b >> 16) & 0xffffu), (long)((unsigned int)b & 0xffffu));
                        continue;
                }
                if (type != WINDOW_EVENT_MOTION && type != WINDOW_EVENT_BUTTON && type != WINDOW_EVENT_WHEEL)
                        continue;
                // A finger on a window whose client holds a wl_touch is told of by its touch (compositor_wl_touch_report): the
                // pointer's version of the same finger (the kit's press and motion, which say so) is not sent to that client.
                if ((flags & WINDOW_KEY_TOUCH) && wayland_row_takes_touch(row))
                        continue;
                // A drag holds the pointer: its motion is over whatever window is under the pointer now, however the press holds
                // this one (the target may be another client's window, or none).
                if (type == WINDOW_EVENT_MOTION && wayland_dragging())
                {
                        int tab, under = compositor_under(compositor.pointer_x, compositor.pointer_y, &tab);
                        int wx = 0, wy = 0;

                        if (under >= 0)
                                compositor_window_point(under, compositor.pointer_x, compositor.pointer_y, &wx, &wy);
                        wayland_drag_motion(under, wx, wy);
                        continue;
                }
                // Whatever the event, the pointer is on this window for the client first.
                if (wayland.pointer_row != row)
                {
                        int ex, ey;

                        if (type == WINDOW_EVENT_MOTION)
                                ex = a, ey = b;
                        else
                                compositor_window_point(row, compositor.pointer_x, compositor.pointer_y, &ex, &ey);
                        wayland_pointer_to(row, ex, ey);
                }
                if (type == WINDOW_EVENT_MOTION)
                        wayland_pointer_motion(row, a, b);
                else if (type == WINDOW_EVENT_BUTTON)
                        wayland_pointer_button(row, a, (flags & WINDOW_KEY_DOWN) ? 1 : 0);
                else if (flags & WINDOW_WHEEL_STOP)
                        // The last finger of a pad lifted: the precise scroll is over (axis_stop on both axes).
                        wayland_pointer_axis_stop(row);
                else
                        // A wheel's v120 is the evdev sign, up positive; the protocol's vertical axis is positive downward. A stream's
                        // notch is a wheel's (value120), a finger's turn is the window's notch in pixels (UI_NOTCH). The Scrolling
                        // group's natural direction, on, turns the turn the other way for the client too (WINDOW_SCROLL_FLIP).
                        wayland_pointer_axis(row, b != 0, table->scroll_off & WINDOW_SCROLL_FLIP ? -(b ? a : -a) : b ? a : -a,
                                             (flags & WINDOW_WHEEL_PRECISE) && !(flags & WINDOW_WHEEL_STREAM) ? UI_NOTCH : 0);
        }
        __atomic_store_n(&table->event_tail[row], tail, __ATOMIC_RELEASE);
}

/*
        What changed for the clients this pass: where the pointer and the keys are, the layout (every keyboard is sent the
        new keymap), and each window the compositor knocked on, its ring read and its state and size said. Called before the
        frame is drawn, so no event waits for it; everything queued is sent.
*/
#define COMPOSITOR_WL_KEEP_NS 16000000ull
static positive compositor_wl_kept_at; // when compositor_wl_wake last looked at the screens, the hints and the scales

static void compositor_wl_wake(void)
{
        struct wayland_stats stats;

        if (!compositor_wl.started)
                return;
        // A slingshot turn (a locked pointer's counts and nothing else: compositor_sling_only) has no end, and its input's send
        // (compositor_wl_wake_input) makes this whole once a frame (COMPOSITOR_WL_KEEP_NS) and not at each of the hand's 8000 reports a second.
        compositor_wl_kept_at = clock_monotonic_nanoseconds();
        wayland_stats_get(&stats);
        // The screens: a scale, a mode, a place or a turn is told to every client whole, and each window the scale of its screen.
        {
                static unsigned int screens_layout;
                static int screens_scale;
                static positive screens_period;
                if (compositor.layout != screens_layout || compositor.scale != screens_scale || compositor.period != screens_period || !compositor_wl.screens_told)
                {
                        unsigned long digest = compositor_wl_screens_digest();
                        screens_layout = compositor.layout, screens_scale = compositor.scale, screens_period = compositor.period;
                        if (digest != compositor_wl.screens_told)
                        {
                                compositor_wl.screens_told = digest;
                                wayland_screens_changed();
                        }
                }
        }
        wayland_idle_tick();
        if (compositor_wl_tiled_now() != compositor_wl.dmabuf_tiled)
                compositor_wl_dmabuf_refresh();
        compositor_wl_table();
        if (stats.rows > 0)
                wayland_scales_sweep();
        for (int row = rows_next(&compositor.woken, 0); row >= 0; row = rows_next(&compositor.woken, row + 1))
                if (wayland_row_owner(row) >= 0)
                        compositor_wl_drain(row);
        // Steam's overlay open over the game with the keys: the game's hold of the pointer is let go (the overlay has the pointer,
        // as on X11), and taken again when it closes.
        if (compositor.table)
        {
                int keys = compositor_wl_key_row();

                wayland_unhold_row(keys >= 0 && (compositor_mc_shown() || compositor_input_override_query(keys) == 2) ? keys : -1);
        }
        if (stats.rows > 0 || stats.clients > 0)
        {
                int want, keys = compositor_wl_key_row();

                if (compositor.keymap.layout != compositor_wl.layout_told)
                {
                        compositor_wl.layout_told = compositor.keymap.layout;
                        wayland_keymap_changed();
                }
                {
                        long rate, delay;

                        compositor_wl_repeat(0, &rate, &delay);
                        if (rate != compositor_wl.repeat_told[0] || delay != compositor_wl.repeat_told[1])
                        {
                                compositor_wl.repeat_told[0] = rate;
                                compositor_wl.repeat_told[1] = delay;
                                wayland_repeat_changed();
                        }
                }
                // Update keyboard focus first so losing focus off a pointer-locked window releases the lock before
                // computing the new pointer row.
                if (keys != wayland.keyboard_row)
                        wayland_keyboard_to(keys);
                want = compositor_wl_pointer_row();
                // A finger was the last thing used (the pointer is hidden under it) on a window whose client holds a touch: the
                // pointer is not on that window for the client, the touch is; no enter is sent for the finger's move.
                if (want >= 0 && compositor.touch.hidden && wayland_row_takes_touch(want))
                        want = -1;
                if (want != wayland.pointer_row)
                {
                        int ex = 0, ey = 0;

                        if (want >= 0)
                                compositor_window_point(want, compositor.pointer_x, compositor.pointer_y, &ex, &ey);
                        wayland_pointer_to(want, ex, ey);
                        compositor_motion();
                }
        }
        for (int row = 0; row < (int)compositor_rows_in_use; row++)
                if (rows_has(&compositor.woken, row) && wayland_row_owner(row) >= 0)
                {
                        wayland_row_wake(row);
                        rows_del(&compositor.woken, row);
                }
        // The hand's counts no motion took (the pointer stopped at an edge), in a frame of their own.
        wayland_pointer_counts_flush();
        compositor_wl_flush_all();
        compositor_wl_direct_publish();
}

/*
        A turn's input, sent as soon as the devices are read (compositor_inputs_turn): each woken client's ring drained, its rows woken, the
        hand's counts no motion took, and every socket flushed. What else the clients are told (the screens, the idle clock, the hints, the
        scales, the pointer's and the keys' windows) is the turn's end's (compositor_wl_wake again, after the doors and the commits), and
        nothing of it changes for a key or a report, so it is not looked at in front of them: a key turn sent its key after the screens'
        digest, every row's hints and facts and the pointer's window under the band (new-dragonfly, Haswell: keys 31 us inject to client
        where a locked pointer's report, which skipped them, took 28). Whole when the keymap changed under the turn's keys (its layout is
        told before them), and on a slingshot turn whose keep is due (no turn's end follows it).
*/
static void compositor_wl_wake_input(void)
{
        if (!compositor_wl.started)
                return;
        if (compositor.keymap.layout != compositor_wl.layout_told ||
            (compositor_sling_only && compositor_sling_sent && clock_monotonic_nanoseconds() - compositor_wl_kept_at >= COMPOSITOR_WL_KEEP_NS))
        {
                compositor_wl_wake();
                return;
        }
        for (int row = 0; row < (int)compositor_rows_in_use; row++)
                if (rows_has(&compositor.woken, row) && wayland_row_owner(row) >= 0)
                {
                        compositor_wl_drain(row);
                        wayland_row_wake(row);
                        rows_del(&compositor.woken, row);
                }
        if (!compositor_sling_only)
                wayland_pointer_counts_flush();
        compositor_wl_flush_all();
}

// Target vblank for wp_commit_timing, using Canvas's native vblank PLL (scanout.c).
static void compositor_wl_target(void)
{
        compositor_wl_target_at(clock_monotonic_nanoseconds());
}

static void compositor_wl_target_at(positive now)
{
        unsigned long seq;
        positive next = canvas_vblank_next(now, &seq);

        if (next && (compositor.flip_pending || compositor_card.on))
                next += canvas_vblank_refresh();
        wayland_target((unsigned long)next, next ? (unsigned long)canvas_vblank_refresh() : 0ul);
}

/*
        A present begins: the shim latches what the clients committed (wayland_latch), answers the frame callbacks, and what comes from
        now on is the next frame's.
*/
static void compositor_wl_latch(void)
{
        int jit, defer;

        if (!compositor_wl.started)
                return;
        jit = compositor_wl_jit_row();
        defer = jit >= 0 && !compositor_wl_row_async(jit);
        wayland_latch_defer(defer);
        compositor_wl.latched_at = clock_monotonic_nanoseconds();
        compositor_wl.landed = 0;
        if (!defer)
                compositor_wl_flush_all();
}

/* Resume clients whose frame-barrier hold cleared when wayland_frame() advanced the frame count. */
static void compositor_wl_resume_stalled(void)
{
        compositor_wl_target();
        for (long slot = 0; slot < compositor_wl.slots; slot++)
                if (compositor_wl.fd[slot] >= 0 && wayland_stalled(slot))
                {
                        wayland_resume(slot);
                        if (wayland_dead(slot))
                                compositor_wl_flush(slot);
                }
}

/*
        A frame was drawn: the buffers its pictures replaced are given back, and the clients whose commits waited for a barrier it cleared
        go on. Its callbacks went at its latch (compositor_wl_latch); a frame no latch began (a present outside the loop's) answers them here.
*/
static void compositor_wl_frame(void)
{
        int busy_row;

        if (!compositor_wl.started)
                return;
        busy_row = compositor.direct_row >= 0 ? compositor.direct_row : compositor.plane_row;
        if (compositor.flip_pending && compositor.leaving_row >= 0)
                busy_row = busy_row >= 0 && busy_row != compositor.leaving_row ? -2 : compositor.leaving_row;
        wayland_drawn_composed(busy_row);
        wayland_frame();
        compositor_wl_resume_stalled();
        compositor_wl_flush_all();
}

/*
        A frame's time went by with nothing drawn (nothing on the screen changed): the callbacks that waited for a frame are answered as
        for a drawn one, and the feedbacks a commit took are answered too, since the picture is the one on the screen. A flip still in
        flight is not answered for: its feedbacks wait for the flip's completion, and the next frame's time answers them.
*/
static void compositor_wl_idle(void)
{
        if (!compositor_wl.started)
                return;
        wayland_frame();
        if (!compositor.flip_pending)
        {
                positive now = clock_monotonic_nanoseconds();
                unsigned long seq;
                positive vblank = canvas_vblank_next(now, &seq);

                if (vblank)
                {
                        if (seq >= compositor_wl.presents)
                                compositor_wl.presents = seq + 1;
                        wayland_presented_idle((unsigned long)vblank, (unsigned long)canvas_vblank_refresh(), seq, WLN_wp_presentation_feedback_kind_vsync);
                }
                else
                        wayland_presented_idle((unsigned long)now, (unsigned long)canvas_vblank_refresh(), compositor_wl.presents++, 0ul);
        }
        compositor_wl_resume_stalled();
        compositor_wl_flush_all();
}

/*
        The fingers of a touch screen, as the windows' clients are told them (wl_touch): a finger that is down now and was not in
        the last report is a touch down on the window under it, one that was down and is down still moved (a motion, when it
        is somewhere else), and one that was down and is not is lifted. dropped: the device lost its events, so every finger
        is cancelled instead. Called at the end of compositor_touch_report, before its ids become the last report's.
*/
static void compositor_wl_touch_report(unsigned int at, int dropped)
{
        const struct compositor_touch_screen *screen = &compositor.screen_touch[at];

        if (!compositor_wl.started)
                return;
        if (dropped)
        {
                wayland_touch_cancel();
                compositor_wl_flush_all();
                return;
        }
        for (int slot = 0; slot < UI_TOUCH_SLOTS; slot++)
        {
                int x, y, row;

                if (screen->id[slot] >= 0)
                {
                        compositor_touch_point(at, slot, &x, &y);
                        if (screen->was[slot] < 0)
                        {
                                row = compositor_picture_under(x, y);
                                // Input override active on row (e.g. Steam overlay): suppress new wl_touch fingers.
                                if (row >= 0 && compositor_input_override_query(row) != 2)
                                {
                                        int wx, wy;

                                        compositor_window_point(row, x, y, &wx, &wy);
                                        wayland_touch_down(row, screen->id[slot], wx, wy);
                                }
                        }
                        else if ((row = wayland_touch_row(screen->id[slot])) >= 0)
                        {
                                int wx, wy;

                                compositor_window_point(row, x, y, &wx, &wy);
                                wayland_touch_motion(screen->id[slot], wx, wy);
                        }
                }
                else if (screen->was[slot] >= 0)
                        wayland_touch_up(screen->was[slot]);
        }
        compositor_wl_flush_all();
}

/*
        A frame was drawn and flipped: the presentation feedbacks its commits took are answered with the time of the flip, which is
        the compositor's clock as the flip is issued (no vblank event is read here, so the shim claims none), and the period.
*/
static void compositor_wl_answer_stamp(positive when, unsigned int seq)
{
        if ((unsigned long)seq >= compositor_wl.presents)
                compositor_wl.presents = (unsigned long)seq + 1;
        wayland_presented((unsigned long)when, (unsigned long)canvas_vblank_refresh(), seq,
                          WLN_wp_presentation_feedback_kind_vsync | WLN_wp_presentation_feedback_kind_hw_clock |
                              WLN_wp_presentation_feedback_kind_hw_completion,
                          compositor_wl.zero_row);
}

static void compositor_wl_presented(void)
{
        positive now = clock_monotonic_nanoseconds();

        if (!compositor_wl.started)
                return;
        compositor_wl.zero_row = compositor.direct_row >= 0 && compositor_client_scans(compositor.direct_row) ? compositor.direct_row
                                 : compositor.plane_row >= 0 && compositor_client_scans(compositor.plane_row) ? compositor.plane_row
                                                                                                              : -1;
        if (compositor_wl.landed)
        {
                compositor_wl.landed = 0;
                compositor_wl.presenting_waiting = 0;
                compositor_wl_answer_stamp(compositor_wl.landed_when, compositor_wl.landed_seq);
                return;
        }
        if (compositor.flip_pending && !compositor_wl.presenting_waiting)
        {
                compositor_wl.presenting_waiting = 1;
                compositor_wl.presenting_since = now;
                return;
        }
        if (compositor_wl.presenting_waiting && now - compositor_wl.presenting_since < 2 * compositor.period)
                return;
        compositor_wl.presenting_waiting = 0;
        wayland_presented((unsigned long)now, (unsigned long)canvas_vblank_refresh(), compositor_wl.presents++, 0ul, compositor_wl.zero_row);
}

static void compositor_wl_flip_presented(void)
{
        if (!compositor_wl.started)
                return;
        if (!compositor_wl.presenting_waiting)
        {
                if (compositor.flip_stamped && compositor.flip_when && compositor_wl.latched_at && compositor.flip_issued >= compositor_wl.latched_at)
                {
                        compositor_wl.landed = 1;
                        compositor_wl.landed_when = compositor.flip_when;
                        compositor_wl.landed_seq = compositor.flip_seq;
                }
                return;
        }
        compositor_wl.presenting_waiting = 0;
        if (compositor.flip_stamped && compositor.flip_when)
                compositor_wl_answer_stamp(compositor.flip_when, compositor.flip_seq);
        else
                wayland_presented((unsigned long)clock_monotonic_nanoseconds(), (unsigned long)canvas_vblank_refresh(), compositor_wl.presents++,
                                  compositor.flip_tore ? 4ul : 0ul, compositor_wl.zero_row);
        compositor_wl_flush_all();
}

static void compositor_wl_stop(void)
{
        if (!compositor_wl.started)
                return;
        for (long slot = 0; slot < compositor_wl.slots; slot++)
                if (compositor_wl.fd[slot] >= 0)
                        compositor_wl_drop(slot);
        wayland_stop();
        for (long at = 0; at < compositor_wl.transfers; at++)
                if (compositor_wl.transfer[at].state && !compositor_wl.transfer[at].worker)
                        compositor_wl_transfer_free(at);
                else if (compositor_wl.transfer[at].state)
                        compositor_wl_transfer_cancel(at);
        if (compositor_wl.dmabuf_table > 0)
                system_close(compositor_wl.dmabuf_table);
        compositor_wl.dmabuf_table = 0;
        compositor_wl.dmabuf_table_size = 0;
        compositor_wl.dmabuf_entry_count = 0;
        canvas_syncobj_reset(&compositor_wl.syncobj_pool);
        if (compositor_wl.syncobj_pool.drm_fd >= 0)
                system_close(compositor_wl.syncobj_pool.drm_fd);
        canvas_syncobj_init(&compositor_wl.syncobj_pool, -1);
        for (long at = 0; at < 16; at++)
                if (compositor_wl.keymap[at] >= 0)
                        system_close(compositor_wl.keymap[at]);
        system_close(compositor_wl.listen);
        compositor_wl.listen = -1;
        compositor_wl.started = 0;
        compositor_call(syscall(unlinkat), (positive)AT_FDCWD, (positive)COMPOSITOR_WL_PATH, 0, 0, 0, 0);
}

#endif /* WAYLAND_HOST_INCLUDED */
