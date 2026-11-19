/*
        test/checks/wayland.c: the Wayland shim and the X window manager's wire.

        A run of test/checks.c, which includes this file where the run stood (test/split.py cut it there): it is
        compiled only as part of that file, with its statics and macros, and never on its own. A section is built as
        test/checks.c is, from the repository root: a freestanding one by gcc -O2 -static -nostdlib -nostartfiles
        -march=x86-64 -w -DCHECK_<name> -fwhole-program -o OUT test/checks.c (sh test/run builds it with its
        freestanding_check), a hosted one as test/run builds it (gcc -std=gnu11 -O2 -D_GNU_SOURCE -DCHECK_<name>
        test/checks.c).
*/

#ifdef CHECK_wayland
/*
        CHECK_wayland: the Wayland shim against a client that speaks the wire from the generated tables and a fake
        compositor, in one process. Every request is built from the table the shim reads, and every event the shim sends
        is read back by the table's event signatures, so a malformed event is a failed check wherever it appears.

            cc -O2 -DSTANDARD_NO_ENTRY -c src/lib.c -o lib.o
            cc -std=gnu11 -O2 -Wall -Wextra -DCHECK_wayland -DCONFIG_MOONWATER_WAYLAND test/checks.c lib.o

        The library is linked beside it: the engine's head, id and damage routines are src/lib.c's.
*/
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#ifndef CONFIG_MOONWATER_WAYLAND
#define CONFIG_MOONWATER_WAYLAND 1
#endif
#include "../../src/canvas/wayland.c"

static unsigned wl_checks, wl_failures;

#define WLCHECK(condition, ...)                                                                                       \
        do                                                                                                            \
        {                                                                                                             \
                wl_checks++;                                                                                          \
                if (!(condition))                                                                                     \
                {                                                                                                     \
                        wl_failures++;                                                                                \
                        printf("  FAIL: ");                                                                           \
                        printf(__VA_ARGS__);                                                                          \
                        printf(" (line %d)\n", __LINE__);                                                             \
                }                                                                                                     \
        } while (0)

/* ---- the fake compositor ---- */

#define FAKE_ROWS 32 // the fake host's table: a host has as many rows as it makes, and this one makes 32
#define FAKE_W 64
#define FAKE_H 48

struct fake_row
{
        int used, kind, parent;
        long x, y, width, height;
        long picture_w, picture_h, pictures;
        long transparent, opaque[4]; // the last picture's: not opaque, and where it is all the same (x, y, width, height in the window)
        long states, closing, width_asked, height_asked, scale;
        char title[128];
        unsigned pixels[FAKE_W * FAKE_H];
        long did[16];
        long did_count;
};

static struct fake_host_state
{
        struct fake_row row[FAKE_ROWS];
        long fds_open, fds_closed, maps, unmaps, allocs, releases;
        long refuse_open;
        long copied; // pixels of pictures the shim handed the host, counted per pixel read (test_holds)
        long now;
        long locks, caps, keymap_fd, rate, delay;
        struct wayland_screen screen;
        long screens; // how many screens the host has: the first is `screen`, the others sit to its right at its size (0 is one)
        struct wayland_screen extra[4]; // the screens after the first, as a check has set them: a width of 0 is the default one
        // the clipboard the compositor holds: the bytes of each selection, and what the shim asked of it
        long clip_size[2], pull_refuse, pulls, aborts, pushes, clears, pull_client, pull_which, push_row, push_which;
        char clip_types[2][256];  // the selection's types past its text, each with its NUL, as the host names them (clip_type)
        long clip_types_bytes[2];
        char push_mime[64];       // the type the last paste asked for
        int pull_wfd, pull_rfd;
        char clip_text[2][64];
        // dma-bufs: whether the fake takes any, how many pictures were read through a descriptor, whether the next is skipped
        long dmabuf_off, syncs, skip_next, dmabuf_maps, asked, pattern_at;
        long dmabuf_bgr; // the table offers ABGR8888 and XBGR8888 as well, as the compositor's does (compositor_wl_dmabuf_info)
        const char *pattern; // what the GPU says each time it is asked: N for not done, R for done; done once it has no more to say
        long hold_row, hold_mode, holds; // the window that holds the pointer and how
        long clock_still; // the clock does not move by itself (each now() is 16 ms on otherwise)
        long lend, lends; // the host reads a window's picture where it is, as the compositor does (WAYLAND_PICTURE_LENT), and how many it has
        long lent_parts;  // of those, the ones with a part damaged, and the box of the last one's damage in the window
        struct wayland_rect lent_box;
        int dmabuf_table_fd;
        // The tables' reservations (reserve, commit, unreserve) and the commit that is refused: commit_fail_at counts down the commits
        // that go through, and the one it reaches 1 at, and every one after, fails (0: none fails).
        long reserves, unreserves, commits, commit_fail_at, commits_refused;
} fake;

static void *fake_alloc(void *context, long bytes)
{
        (void)context;
        fake.allocs++;
        return calloc(1, (size_t)bytes);
}

static void fake_release(void *context, void *block, long bytes)
{
        (void)context;
        (void)bytes;
        fake.releases++;
        free(block);
}

static void *fake_reserve(void *context, long *bytes)
{
        void *at;

        (void)context;
        if (*bytes > (1L << 30))
                *bytes = 1L << 30;
        at = mmap(0, (size_t)*bytes, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
        if (at == MAP_FAILED)
                return 0;
        fake.reserves++;
        return at;
}

static int fake_commit(void *context, void *at, long bytes)
{
        (void)context;
        fake.commits++;
        if (fake.commit_fail_at && --fake.commit_fail_at <= 0)
        {
                fake.commit_fail_at = 1; // and every one after
                fake.commits_refused++;
                return 0;
        }
        return mprotect(at, (size_t)bytes, PROT_READ | PROT_WRITE) == 0;
}

static void fake_unreserve(void *context, void *at, long bytes)
{
        (void)context;
        fake.unreserves++;
        munmap(at, (size_t)bytes);
}

static void *fake_map(void *context, long fd, long size)
{
        struct stat st;
        void *at;

        (void)context;
        if (fstat((int)fd, &st) < 0 || !S_ISREG(st.st_mode) || st.st_size < size)
                return 0;
        at = mmap(0, (size_t)size, PROT_READ, MAP_SHARED, (int)fd, 0);
        if (at == MAP_FAILED)
                return 0;
        fake.maps++;
        return at;
}

static void *fake_remap(void *context, void *at, long old_size, long new_size)
{
        (void)context;
#ifdef __linux__
        void *grown = mremap(at, (size_t)old_size, (size_t)new_size, MREMAP_MAYMOVE);

        return grown == MAP_FAILED ? 0 : grown;
#else
        (void)at;
        (void)old_size;
        (void)new_size;
        return 0;
#endif
}

static void fake_unmap(void *context, void *at, long size)
{
        (void)context;
        munmap(at, (size_t)size);
        fake.unmaps++;
}

static void fake_close(void *context, long fd)
{
        (void)context;
        fake.fds_closed++;
        close((int)fd);
}

static int fake_row_open(void *context, long client, long kind, int parent, long x, long y)
{
        (void)context;
        (void)client;
        if (fake.refuse_open)
                return -1;
        for (int row = 0; row < FAKE_ROWS; row++)
                if (!fake.row[row].used)
                {
                        memset(&fake.row[row], 0, sizeof fake.row[row]);
                        fake.row[row].used = 1;
                        fake.row[row].kind = (int)kind;
                        fake.row[row].parent = parent;
                        fake.row[row].x = x;
                        fake.row[row].y = y;
                        return row;
                }
        return -1;
}

static void fake_row_close(void *context, int row)
{
        (void)context;
        fake.row[row].used = 0;
}

static int fake_row_picture(void *context, int row, const struct wayland_picture *picture)
{
        struct fake_row *r = &fake.row[row];

        (void)context;
        if (picture->sync_fd >= 0)
        {
                fake.syncs++;
                if (fake.skip_next)
                {
                        fake.skip_next = 0;
                        return 1;
                }
        }
        r->width = picture->row_width;
        r->height = picture->row_height;
        r->picture_w = picture->row_width;
        r->picture_h = picture->row_height;
        r->transparent = picture->transparent;
        r->opaque[0] = picture->opaque_x, r->opaque[1] = picture->opaque_y, r->opaque[2] = picture->opaque_w, r->opaque[3] = picture->opaque_h;
        if (picture->pixels)
        {
                long count = picture->full || !picture->damages ? 1 : picture->damages;

                for (long at = 0; at < count; at++)
                {
                        long x0 = 0, y0 = 0, x1 = picture->width, y1 = picture->height;

                        if (!picture->full && picture->damages)
                        {
                                x0 = picture->damage[at].x;
                                y0 = picture->damage[at].y;
                                x1 = x0 + picture->damage[at].width;
                                y1 = y0 + picture->damage[at].height;
                        }
                        for (long y = y0; y < y1; y++)
                                for (long x = x0; x < x1; x++)
                                {
                                        long dx = picture->dest_x + x, dy = picture->dest_y + y;
                                        unsigned source = ((const unsigned *)(picture->pixels + y * picture->stride))[x];

                                        fake.copied++;

                                        if (dx < 0 || dy < 0 || dx >= FAKE_W || dy >= FAKE_H || dx >= r->width || dy >= r->height)
                                                continue;
                                        if (picture->blend)
                                        {
                                                unsigned alpha = source >> 24, under = r->pixels[dy * FAKE_W + dx], out = 0xff000000u;

                                                for (int shift = 0; shift < 24; shift += 8)
                                                {
                                                        unsigned s8 = (source >> shift) & 255, u8 = (under >> shift) & 255;

                                                        out |= (s8 + (u8 * (255 - alpha) + 127) / 255) << shift;
                                                }
                                                r->pixels[dy * FAKE_W + dx] = out;
                                        }
                                        else
                                                r->pixels[dy * FAKE_W + dx] = source;
                                }
                }
        }
        if (picture->last)
                r->pictures++;
        // The compositor's own rule (compositor_wl_lend): a picture that is the window's last, not laid over anything, whole or with a
        // part damaged; one that may not be whole (fragile) only when all of it is damaged.
        if (fake.lend && picture->pixels && picture->map && picture->last && !picture->blend &&
            (picture->full || !picture->damages || !picture->fragile))
        {
                fake.lends++;
                if (!picture->full && picture->damages)
                {
                        fake.lent_parts++;
                        fake.lent_box = picture->damage[0];
                }
                return WAYLAND_PICTURE_LENT;
        }
        return 0;
}

static void fake_row_title(void *context, int row, const char *text, long length)
{
        (void)context;
        (void)length;
        snprintf(fake.row[row].title, sizeof fake.row[row].title, "%s", text);
}

static long fake_cursor[FAKE_ROWS]; // the cursor code the last WAYLAND_DO_CURSOR gave each row (-1 none)

static void fake_row_do(void *context, int row, long what, long a, long b)
{
        struct fake_row *r = &fake.row[row];

        (void)context;
        (void)b;
        if (what == WAYLAND_DO_CURSOR)
                fake_cursor[row] = a;
        if (r->did_count < 16)
                r->did[r->did_count++] = what;
        if (what == WAYLAND_DO_MAXIMIZE)
                r->states |= WAYLAND_STATE_MAXIMIZED;
        if (what == WAYLAND_DO_UNMAXIMIZE)
                r->states &= ~(long)WAYLAND_STATE_MAXIMIZED;
        if (what == WAYLAND_DO_FULLSCREEN)
                r->states |= WAYLAND_STATE_FULLSCREEN;
        if (what == WAYLAND_DO_UNFULLSCREEN)
                r->states &= ~(long)WAYLAND_STATE_FULLSCREEN;
        if (what == WAYLAND_DO_MINIMIZE)
                r->states |= WAYLAND_STATE_SUSPENDED;
        if (what == WAYLAND_DO_PLACE)
        {
                r->x = a;
                r->y = b;
        }
}

static int fake_row_facts(void *context, int row, struct wayland_facts *facts)
{
        struct fake_row *r = &fake.row[row];

        (void)context;
        if (!r->used)
                return -1;
        facts->width = r->width_asked;
        facts->height = r->height_asked;
        facts->states = r->states;
        facts->closing = r->closing;
        facts->x = r->x;
        facts->y = r->y;
        facts->scale = r->scale ? r->scale : fake.screen.scale;
        return 0;
}

static long fake_screens(void *context, struct wayland_screen *screens, long room)
{
        (void)context;
        (void)room;
        long count = fake.screens > 1 ? fake.screens : 1;

        for (long at = 0; at < count && at < room; at++)
        {
                screens[at] = fake.screen;
                screens[at].x = fake.screen.x + at * fake.screen.width;
                screens[at].name[8] = (char)('1' + at);
                if (at >= 1 && at <= 4 && fake.extra[at - 1].width)
                {
                        screens[at] = fake.extra[at - 1];
                        screens[at].name[8] = (char)('1' + at);
                }
        }
        return count < room ? count : room;
}

static long fake_now(void *context)
{
        (void)context;
        return fake.clock_still ? fake.now : (fake.now += 16);
}

/* The keymap is a file the compositor owns and keeps: the shim hands out its descriptor and never closes it. */
static long fake_keymap(void *context, long *size)
{
        static const char text[] = "xkb_keymap { };\n";

        (void)context;
        if (fake.keymap_fd <= 0)
        {
                char name[] = "/tmp/wlkeymapXXXXXX";
                int fd = mkstemp(name);

                unlink(name);
                if (fd < 0 || write(fd, text, sizeof text - 1) != (ssize_t)(sizeof text - 1))
                        abort();
                fake.keymap_fd = fd;
        }
        *size = (long)sizeof text - 1;
        return fake.keymap_fd;
}

static void fake_repeat(void *context, long *rate, long *delay)
{
        (void)context;
        *rate = fake.rate ? fake.rate : 25;
        *delay = fake.delay ? fake.delay : 600;
}

/* Random bytes that differ from one call to the next, so no two tokens are the same. */
static int fake_random(void *context, unsigned char *into, long bytes)
{
        static unsigned int calls;

        (void)context;
        for (long at = 0; at < bytes; at++)
                into[at] = (unsigned char)(calls * 131u + (unsigned int)at * 17u + 7u);
        calls++;
        return 1;
}

static long fake_seat(void *context)
{
        (void)context;
        return fake.caps;
}

static long fake_locks(void *context)
{
        (void)context;
        return fake.locks;
}

// The read end of a copy's pipe goes; its write end is the shim's from the moment it was handed over, and the shim closes it.
static void fake_pull_close(void)
{
        if (fake.pull_rfd > 0)
                close(fake.pull_rfd);
        fake.pull_wfd = fake.pull_rfd = 0;
}

/* The names a selection is offered as: the three text types while it has text, then its other types. */
static const char *fake_clip_type(void *context, long which, long nth)
{
        static const char *const text[3] = {"text/plain;charset=utf-8", "UTF8_STRING", "text/plain"};

        (void)context;
        if (fake.clip_size[which] > 0 && nth < 3)
                return text[nth];
        nth -= fake.clip_size[which] > 0 ? 3 : 0;
        for (long at = 0; at < fake.clip_types_bytes[which]; at += (long)strlen(fake.clip_types[which] + at) + 1)
                if (!nth--)
                        return fake.clip_types[which] + at;
        return 0;
}

/* A source to read: its first type asked for into a pipe whose write end goes to the client and is the shim's to close (counted open
   here, closed by fake_close); the next is the check's to ask for (wayland_clip_send). */
static long fake_clip_pull(void *context, long client, long which)
{
        int fds[2];

        (void)context;
        if (fake.pull_refuse)
                return 0;
        fake_pull_close();
        if (pipe(fds) < 0)
                return 0;
        fake.pull_rfd = fds[0];
        fake.pull_wfd = fds[1];
        fake.fds_open++;
        fake.pulls++;
        fake.pull_client = client;
        fake.pull_which = which;
        wayland_clip_send(client, which, 0, fds[1]);
        return 1;
}

static void fake_clip_abort(void *context, long client, long which)
{
        (void)context;
        (void)client;
        (void)which;
        fake.aborts++;
}

/* The text written to the client's descriptor, which is closed here as the compositor closes it. */
static void fake_clip_push(void *context, long row, long which, const char *mime, long fd)
{
        const char *type;
        long held = 0;

        (void)context;
        for (long nth = 0; (type = fake_clip_type(0, which, nth)); nth++)
                held |= !strcmp(type, mime);
        snprintf(fake.push_mime, sizeof fake.push_mime, "%s", mime);
        if (held)
        {
                fake.pushes++;
                fake.push_row = row;
                fake.push_which = which;
                if (write((int)fd, fake.clip_text[which], strlen(fake.clip_text[which])) < 0)
                        fake.pushes += 1000;
        }
        fake_close(NULL, fd);
}

static void fake_clip_clear(void *context, long client, long which)
{
        (void)context;
        (void)client;
        fake.clears++;
        fake.clip_size[which] = 0;
        fake.clip_types_bytes[which] = 0;
}

/* A GPU that says what fake.pattern says each time it is asked, and done after it. */
static int fake_dmabuf_ready(void *context, long fd)
{
        (void)context;
        (void)fd;
        fake.asked++;
        if (fake.pattern && fake.pattern[fake.pattern_at])
                return fake.pattern[fake.pattern_at++] != 'N';
        return 1;
}

/* The fake takes ARGB8888 and XRGB8888 linear on a device of its own, in one tranche; the descriptor a client gives is any file. */
static long fake_dmabuf_info(void *context, struct wayland_dmabuf_info *info)
{
        (void)context;
        if (fake.dmabuf_off)
                return 0;
        info->device = (226UL << 8) | 128;
        info->count = fake.dmabuf_bgr ? 4 : 2;
        info->entry[0].format = 0x34325241;
        info->entry[1].format = 0x34325258;
        info->entry[2].format = (unsigned int)WAYLAND_FOURCC_ABGR8888;
        info->entry[3].format = (unsigned int)WAYLAND_FOURCC_XBGR8888;
        info->tranches = 1;
        info->tranche[0].device = info->device;
        info->tranche[0].flags = 0;
        info->tranche[0].count = info->count;
        for (long at = 0; at < info->count; at++)
                info->tranche[0].index[at] = (unsigned char)at;
        return 1;
}

static long fake_dmabuf_table(void *context, long *size)
{
        (void)context;
        if (fake.dmabuf_table_fd <= 0)
        {
                char name[] = "/tmp/wltableXXXXXX";
                int fd = mkstemp(name);
                unsigned char table[64] = {0};

                unlink(name);
                if (fd < 0 || write(fd, table, sizeof table) != (ssize_t)sizeof table)
                        abort();
                fake.dmabuf_table_fd = fd;
        }
        *size = fake.dmabuf_bgr ? 64 : 32; // sixteen bytes a pair the info names
        return fake.dmabuf_table_fd;
}

static void *fake_dmabuf_map(void *context, long fd, long size)
{
        struct stat st;
        void *at;

        (void)context;
        if (fstat((int)fd, &st) < 0 || !S_ISREG(st.st_mode) || st.st_size < size)
                return 0;
        at = mmap(0, (size_t)size, PROT_READ, MAP_SHARED, (int)fd, 0);
        if (at == MAP_FAILED)
                return 0;
        fake.maps++;
        fake.dmabuf_maps++;
        return at;
}

static void fake_pointer_hold(void *context, long row, long mode)
{
        (void)context;
        fake.hold_row = row;
        fake.hold_mode = mode;
        fake.holds++;
}

static const struct wayland_host fake_host = {
    0, fake_alloc, fake_release, fake_map, fake_remap, fake_unmap, fake_close, fake_row_open, fake_row_close,
    fake_row_picture, fake_row_title, fake_row_do, fake_row_facts, fake_screens, fake_now, fake_keymap, fake_repeat, fake_seat,
    fake_locks, fake_pointer_hold, fake_dmabuf_info, fake_dmabuf_table, fake_dmabuf_map, fake_clip_type, fake_clip_pull, fake_clip_abort, fake_clip_push,
    fake_clip_clear, fake_dmabuf_ready, fake_random, 0, 0, // no row_layers (test_layers gives one) and no row_busy (test_row_busy)
    fake_reserve, fake_commit, fake_unreserve, 0, 0, 0}; // no bridge to X (test_bridge gives one), no row_app

/* ---- a client that builds its requests from the table ---- */

#define EVENTS 512

struct wl_event_seen
{
        unsigned long object;
        long opcode, iface;
        unsigned long u[8];
        char text[8][64];
        long n;
        long fd_count;
};

#define WLC_IDS 65536 // ids a check's client may make: more than ten thousand surfaces, with what they need

struct wlc
{
        long slot;
        unsigned long next;
        unsigned char iface[WLC_IDS];
        unsigned char version[WLC_IDS];
        unsigned char siface[4096], sversion[4096]; /* the objects the server makes: ids from WAYLAND_SERVER_BASE */
        struct wl_event_seen ev[EVENTS];
        long events, taken;
        long fds_seen;
        long malformed;
        long errors;
        unsigned long error_object, error_code;
        char error_text[128];
};

static struct wlc *wlc_open(void)
{
        struct wlc *c = calloc(1, sizeof *c);

        c->slot = wayland_connect(1000, 1000);
        c->next = 2;
        c->iface[1] = WLI_wl_display;
        c->version[1] = 1;
        return c;
}

/* A client gone: the shim gives back what it held, and the client's own record goes. */
static void wlc_end(struct wlc *c)
{
        wayland_drop(c->slot);
        free(c);
}

static unsigned long wlc_id(struct wlc *c, long iface, long version)
{
        unsigned long id = c->next++;

        c->iface[id] = (unsigned char)iface;
        c->version[id] = (unsigned char)version;
        return id;
}

static int wlc_pump(struct wlc *c);

static int wlc_known(unsigned long id)
{
        return id < WLC_IDS || (id >= WAYLAND_SERVER_BASE && id - WAYLAND_SERVER_BASE < 4096);
}

static unsigned char wlc_iface(struct wlc *c, unsigned long id)
{
        return id < WLC_IDS ? c->iface[id] : id >= WAYLAND_SERVER_BASE && id - WAYLAND_SERVER_BASE < 4096 ? c->siface[id - WAYLAND_SERVER_BASE] : 0;
}

static unsigned char wlc_version(struct wlc *c, unsigned long id)
{
        return id < WLC_IDS ? c->version[id] : id >= WAYLAND_SERVER_BASE && id - WAYLAND_SERVER_BASE < 4096 ? c->sversion[id - WAYLAND_SERVER_BASE] : 0;
}

static void wlc_set(struct wlc *c, unsigned long id, long iface, long version)
{
        if (id < WLC_IDS)
        {
                c->iface[id] = (unsigned char)iface;
                c->version[id] = (unsigned char)version;
        }
        else if (id >= WAYLAND_SERVER_BASE && id - WAYLAND_SERVER_BASE < 4096)
        {
                c->siface[id - WAYLAND_SERVER_BASE] = (unsigned char)iface;
                c->sversion[id - WAYLAND_SERVER_BASE] = (unsigned char)version;
        }
}

static void wlc_put(unsigned char *at, unsigned long v)
{
        wayland_put(at, v);
}

/*
        One request, encoded by the table. The arguments follow in the table's order: an integer for i, u and f, an id for
        o, O and n, a C string for s and S, (text, length) for a, a descriptor for h, and for N the interface's name, the
        version and the id. Returns what the shim answered.
*/
static int wlc_raw(struct wlc *c, unsigned char *bytes, long size, const long *fds, long count)
{
        int status;

        wayland_put(bytes + 4, wayland_get(bytes + 4) | (unsigned long)size << 16);
        status = wayland_feed(c->slot, bytes, size, fds, count);
        wlc_pump(c);
        return status;
}

static int wlc_send(struct wlc *c, unsigned long object, long op, ...)
{
        unsigned char b[WAYLAND_MESSAGE];
        const struct wl_message *msg = &wl_interfaces[wlc_iface(c, object)].request[op];
        long n = 8, fds[8], fd_count = 0;
        va_list ap;

        memset(b, 0, sizeof b);
        wlc_put(b, object);
        wlc_put(b + 4, (unsigned long)op);
        va_start(ap, op);
        for (const unsigned char *k = msg->args; k[0]; k += 2)
        {
                switch (k[0])
                {
                case 'i':
                case 'u':
                case 'f':
                        wlc_put(b + n, (unsigned long)va_arg(ap, long));
                        n += 4;
                        break;
                case 'o':
                case 'O':
                case 'n':
                {
                        unsigned long id = va_arg(ap, unsigned long);

                        wlc_put(b + n, id);
                        n += 4;
                        if (k[0] == 'n')
                                wlc_set(c, id, k[1], wlc_version(c, object));
                        break;
                }
                case 's':
                case 'S':
                case 'N':
                {
                        const char *s = va_arg(ap, const char *);
                        unsigned long count = s ? strlen(s) + 1 : 0, padded = (count + 3) & ~3UL;

                        wlc_put(b + n, count);
                        n += 4;
                        memcpy(b + n, s ? s : "", count);
                        n += (long)padded;
                        if (k[0] == 'N')
                        {
                                wlc_put(b + n, (unsigned long)va_arg(ap, long));
                                wlc_put(b + n + 4, va_arg(ap, unsigned long));
                                n += 8;
                        }
                        break;
                }
                case 'a':
                {
                        const unsigned char *p = va_arg(ap, const unsigned char *);
                        long len = va_arg(ap, long);

                        wlc_put(b + n, (unsigned long)len);
                        n += 4;
                        memcpy(b + n, p, (size_t)len);
                        n += (len + 3) & ~3L;
                        break;
                }
                case 'h':
                        fds[fd_count++] = va_arg(ap, long);
                        break;
                }
        }
        va_end(ap);
        return wlc_raw(c, b, n, fds, fd_count);
}

/* A client whose socket is not read: the pump is held, and what the shim queues stays queued. */
static int wlc_hold;

/* The events the shim has queued, read by the table: every one must be a known object's event, in its version, whole. */
static int wlc_pump(struct wlc *c)
{
        const unsigned char *bytes;
        const long *fds;
        long fd_count, length, at = 0;

        if (wlc_hold)
                return 0;
        length = wayland_out(c->slot, &bytes, &fds, &fd_count);

        while (at < length)
        {
                unsigned long object, header;
                long size, opcode, n = 8;
                const struct wl_interface_info *info;
                const struct wl_message *msg;
                struct wl_event_seen *e;

                if (length - at < 8)
                {
                        c->malformed++;
                        if (getenv("WLDEBUG"))
                                fprintf(stderr, "malformed: short header\n");
                        break;
                }
                object = wayland_get(bytes + at);
                header = wayland_get(bytes + at + 4);
                size = (long)(header >> 16);
                opcode = (long)(header & 0xffff);
                if (size < 8 || size > length - at || (size & 3) || !wlc_known(object) || !wlc_iface(c, object))
                {
                        c->malformed++;
                        if (getenv("WLDEBUG"))
                                fprintf(stderr, "malformed: object %lu size %ld iface %d\n", object, size, wlc_known(object) ? wlc_iface(c, object) : -1);
                        break;
                }
                info = &wl_interfaces[wlc_iface(c, object)];
                if (opcode >= info->events || info->event[opcode].since > wlc_version(c, object))
                {
                        c->malformed++;
                        if (getenv("WLDEBUG"))
                                fprintf(stderr, "malformed: %s opcode %ld (version %d)\n", info->name, opcode, wlc_version(c, object));
                        at += size;
                        continue;
                }
                msg = &info->event[opcode];
                e = c->events < EVENTS ? &c->ev[c->events++] : &c->ev[EVENTS - 1];
                memset(e, 0, sizeof *e);
                e->object = object;
                e->opcode = opcode;
                e->iface = wlc_iface(c, object);
                for (const unsigned char *k = msg->args; k[0]; k += 2)
                {
                        unsigned long word, count, padded;

                        if (k[0] == 'h')
                        {
                                e->fd_count++;
                                continue;
                        }
                        if (at + n + 4 > at + size)
                        {
                                c->malformed++;
                                break;
                        }
                        word = wayland_get(bytes + at + n);
                        n += 4;
                        if (k[0] == 's' || k[0] == 'S' || k[0] == 'a')
                        {
                                count = word;
                                padded = (count + 3) & ~3UL;
                                if (count > (unsigned long)(size - n) || padded > (unsigned long)(size - n) ||
                                    (k[0] != 'a' && count && bytes[at + n + count - 1]))
                                        c->malformed++;
                                else if (count && e->n < 8)
                                        snprintf(e->text[e->n], sizeof e->text[0], "%.*s", (int)(count - (k[0] != 'a')),
                                                 (const char *)bytes + at + n);
                                n += (long)padded;
                        }
                        else if (k[0] == 'n')
                        {
                                /* an event that makes an object: the id is the server's */
                                wlc_set(c, word, k[1], wlc_version(c, object));
                        }
                        if (e->n < 8)
                                e->u[e->n++] = word;
                }
                if (n != size)
                {
                        c->malformed++;
                        if (getenv("WLDEBUG"))
                                fprintf(stderr, "malformed: %s opcode %ld read %ld of %ld\n", info->name, opcode, n, size);
                }
                if (wlc_iface(c, object) == WLI_wl_display && opcode == WLE_wl_display_error)
                {
                        c->errors++;
                        c->error_object = e->u[0];
                        c->error_code = e->u[1];
                        snprintf(c->error_text, sizeof c->error_text, "%s", e->text[2]);
                }
                if (wlc_iface(c, object) == WLI_wl_display && opcode == WLE_wl_display_delete_id && e->u[0] < WLC_IDS)
                        c->iface[e->u[0]] = 0;
                at += size;
        }
        c->fds_seen += fd_count;
        // a descriptor of a copy's source that has been sent is closed by the shim, in wayland_sent (0976821d)
        for (long fd_at = 0; fd_at < fd_count; fd_at++)
                if (fake.pull_wfd > 0 && fds[fd_at] == fake.pull_wfd)
                        fake.pull_wfd = 0;
        wayland_sent(c->slot, length);
        return (int)length;
}

/* The first unread event of an interface and opcode, or 0; reading it takes it. */
static struct wl_event_seen *wlc_take(struct wlc *c, long iface, long opcode)
{
        for (long at = c->taken; at < c->events; at++)
                if (c->ev[at].iface == iface && c->ev[at].opcode == opcode)
                {
                        c->taken = at + 1;
                        return &c->ev[at];
                }
        return 0;
}

static long wlc_count(struct wlc *c, long iface, long opcode)
{
        long count = 0;

        for (long at = 0; at < c->events; at++)
                count += c->ev[at].iface == iface && c->ev[at].opcode == opcode;
        return count;
}

static void wlc_forget(struct wlc *c)
{
        c->events = 0;
        c->taken = 0;
}

/* A pool: a file of size bytes whose pixels the client wrote, and its descriptor. */
static long wl_file(long size, unsigned char fill)
{
        char name[] = "/tmp/wlcheckXXXXXX";
        int fd = mkstemp(name);

        unlink(name);
        if (fd < 0 || ftruncate(fd, size) < 0)
                return -1;
        {
                unsigned char *at = mmap(0, (size_t)size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

                memset(at, fill, (size_t)size);
                munmap(at, (size_t)size);
        }
        fake.fds_open++;
        return fd;
}

static void wl_file_put(long fd, long offset, const void *bytes, long count)
{
        if (pwrite((int)fd, bytes, (size_t)count, offset) != count)
                abort();
}

/* ---- a session: the display, the registry, and a window of the usual kind ---- */

struct session
{
        struct wlc *c;
        unsigned long registry, compositor, shm, wm_base, seat, output, pool, buffer, surface, xdg, toplevel, callback, data, primary, dmabuf, relative, constraints, idle, shortcuts, tearing, xshell, xgrab;
        long fd;
};

static unsigned long wl_global(struct wlc *c, unsigned long registry, long iface, long version)
{
        unsigned long id = wlc_id(c, iface, version);

        for (long at = 0; at < c->events; at++)
                if (c->ev[at].iface == WLI_wl_registry && c->ev[at].opcode == WLE_wl_registry_global &&
                    !strcmp(c->ev[at].text[1], wl_interfaces[iface].name))
                {
                        wlc_send(c, registry, WLQ_wl_registry_bind, c->ev[at].u[0], wl_interfaces[iface].name, (long)version, id);
                        return id;
                }
        return 0;
}

/* The user a session names to the shim as the one the X window manager's socket takes (the host's word); wlc_open's clients are 1000. */
static long wl_check_xuser = 1000;

static void session_open(struct session *s)
{
        memset(s, 0, sizeof *s);
        wayland_xwayland_user(wl_check_xuser);
        s->c = wlc_open();
        s->registry = wlc_id(s->c, WLI_wl_registry, 1);
        wlc_send(s->c, 1, WLQ_wl_display_get_registry, s->registry);
        s->compositor = wl_global(s->c, s->registry, WLI_wl_compositor, 6);
        s->shm = wl_global(s->c, s->registry, WLI_wl_shm, 1);
        s->wm_base = wl_global(s->c, s->registry, WLI_xdg_wm_base, 6);
        s->seat = wl_global(s->c, s->registry, WLI_wl_seat, 9);
        s->output = wl_global(s->c, s->registry, WLI_wl_output, 4);
        wl_global(s->c, s->registry, WLI_wl_subcompositor, 1);
        s->data = wl_global(s->c, s->registry, WLI_wl_data_device_manager, 3);
        s->primary = wl_global(s->c, s->registry, WLI_zwp_primary_selection_device_manager_v1, 1);
        s->dmabuf = wl_global(s->c, s->registry, WLI_zwp_linux_dmabuf_v1, 4);
        s->relative = wl_global(s->c, s->registry, WLI_zwp_relative_pointer_manager_v1, 1);
        s->constraints = wl_global(s->c, s->registry, WLI_zwp_pointer_constraints_v1, 1);
        s->idle = wl_global(s->c, s->registry, WLI_zwp_idle_inhibit_manager_v1, 1);
        s->shortcuts = wl_global(s->c, s->registry, WLI_zwp_keyboard_shortcuts_inhibit_manager_v1, 1);
        s->tearing = wl_global(s->c, s->registry, WLI_wp_tearing_control_manager_v1, 1);
        s->xshell = wl_global(s->c, s->registry, WLI_xwayland_shell_v1, 1);
        s->xgrab = wl_global(s->c, s->registry, WLI_zwp_xwayland_keyboard_grab_manager_v1, 1);
}

/* A toplevel, configured and acknowledged, with a buffer of 32 by 24 of one colour shown. */
static void session_window(struct session *s, unsigned colour)
{
        struct wlc *c = s->c;
        unsigned pixels[32 * 24];
        struct wl_event_seen *configure;

        for (int at = 0; at < 32 * 24; at++)
                pixels[at] = colour;
        s->fd = wl_file(32 * 24 * 4 * 2, 0);
        wl_file_put(s->fd, 0, pixels, sizeof pixels);
        s->pool = wlc_id(c, WLI_wl_shm_pool, 1);
        wlc_send(c, s->shm, WLQ_wl_shm_create_pool, s->pool, s->fd, (long)(32 * 24 * 4 * 2));
        close((int)s->fd);
        s->buffer = wlc_id(c, WLI_wl_buffer, 1);
        wlc_send(c, s->pool, WLQ_wl_shm_pool_create_buffer, s->buffer, 0L, 32L, 24L, 128L, (long)WLN_wl_shm_format_xrgb8888);
        s->surface = wlc_id(c, WLI_wl_surface, 6);
        wlc_send(c, s->compositor, WLQ_wl_compositor_create_surface, s->surface);
        s->xdg = wlc_id(c, WLI_xdg_surface, 6);
        wlc_send(c, s->wm_base, WLQ_xdg_wm_base_get_xdg_surface, s->xdg, s->surface);
        s->toplevel = wlc_id(c, WLI_xdg_toplevel, 6);
        wlc_send(c, s->xdg, WLQ_xdg_surface_get_toplevel, s->toplevel);
        wlc_send(c, s->surface, WLQ_wl_surface_commit);
        configure = wlc_take(c, WLI_xdg_surface, WLE_xdg_surface_configure);
        if (configure)
                wlc_send(c, s->xdg, WLQ_xdg_surface_ack_configure, (long)configure->u[0]);
        wlc_send(c, s->surface, WLQ_wl_surface_attach, s->buffer, 0L, 0L);
        wlc_send(c, s->surface, WLQ_wl_surface_damage_buffer, 0L, 0L, 32L, 24L);
        wlc_send(c, s->surface, WLQ_wl_surface_commit);
}

static void wl_reset(void)
{
        if (fake.keymap_fd > 0)
                close((int)fake.keymap_fd);
        if (fake.dmabuf_table_fd > 0)
                close(fake.dmabuf_table_fd);
        memset(&fake, 0, sizeof fake);
        fake.hold_row = -1;
        fake.caps = WLN_wl_seat_capability_pointer | WLN_wl_seat_capability_keyboard;
        fake.screen = (struct wayland_screen){0, 0, 1280, 720, 100, 60000, 340, 190, "Virtual-1"};
}

/* ---- the tests ---- */

static void leak_check(const char *what)
{
        struct wayland_stats stats;

        wayland_stats_get(&stats);
        WLCHECK(stats.clients == 0 && stats.objects == 0 && stats.rows == 0 && stats.fds == 0 && stats.pools == 0 &&
                    stats.buffers == 0 && stats.surfaces == 0,
                "%s: everything comes back (clients %ld objects %ld rows %ld fds %ld pools %ld buffers %ld surfaces %ld)", what,
                stats.clients, stats.objects, stats.rows, stats.fds, stats.pools, stats.buffers, stats.surfaces);
        WLCHECK(fake.maps == fake.unmaps, "%s: every mapping is unmapped (%ld %ld)", what, fake.maps, fake.unmaps);
        WLCHECK(fake.allocs == fake.releases, "%s: every client record is released (%ld %ld)", what, fake.allocs, fake.releases);
        WLCHECK(fake.reserves == fake.unreserves, "%s: every table's reservation is given back (%ld %ld)", what, fake.reserves, fake.unreserves);
        for (int row = 0; row < FAKE_ROWS; row++)
                if (fake.row[row].used)
                {
                        WLCHECK(0, "%s: row %d is still open", what, row);
                        break;
                }
}

static int fds_balanced(void)
{
        return fake.fds_open == fake.fds_closed;
}

/* The refusal a client got: the code and the object it names. */
static int refused_with(struct wlc *c, unsigned long code)
{
        return c->errors == 1 && c->error_code == code;
}

static void test_flow(void)
{
        struct session s;
        struct wl_event_seen *e;

        wl_reset();
        WLCHECK(wayland_start(&fake_host) == 0, "start");
        session_open(&s);
        WLCHECK(s.compositor && s.shm && s.wm_base && s.seat && s.output, "the globals bind");
        e = wlc_take(s.c, WLI_wl_output, WLE_wl_output_mode);
        WLCHECK(e && e->u[1] == 1280 && e->u[2] == 720, "the output says its mode");
        session_window(&s, 0xff336699);
        WLCHECK(s.c->errors == 0, "no error (%s)", s.c->error_text);
        WLCHECK(s.c->malformed == 0, "every event is well-formed");
        WLCHECK(fake.row[0].used && fake.row[0].picture_w == 32 && fake.row[0].pixels[5] == 0xff336699, "the picture is in the window");
        WLCHECK(wlc_count(s.c, WLI_wl_buffer, WLE_wl_buffer_release) == 1, "the buffer is released");
        wlc_send(s.c, s.xdg, WLQ_xdg_surface_get_toplevel, wlc_id(s.c, WLI_xdg_toplevel, 6));
        WLCHECK(refused_with(s.c, WLN_xdg_surface_error_already_constructed), "a second toplevel is refused");
        wlc_end(s.c);
        leak_check("flow");
        WLCHECK(fds_balanced(), "every descriptor was closed (%ld %ld)", fake.fds_open, fake.fds_closed);
        wayland_stop();
}

/* What a toplevel is first sent, in order, and what a client may do with it. */
static void test_configure(void)
{
        struct session s;
        struct wlc *c;
        struct wl_event_seen *e;
        unsigned long first_serial;

        wl_reset();
        wayland_start(&fake_host);
        session_open(&s);
        c = s.c;
        s.surface = wlc_id(c, WLI_wl_surface, 6);
        wlc_send(c, s.compositor, WLQ_wl_compositor_create_surface, s.surface);
        s.xdg = wlc_id(c, WLI_xdg_surface, 6);
        wlc_send(c, s.wm_base, WLQ_xdg_wm_base_get_xdg_surface, s.xdg, s.surface);
        s.toplevel = wlc_id(c, WLI_xdg_toplevel, 6);
        wlc_send(c, s.xdg, WLQ_xdg_surface_get_toplevel, s.toplevel);
        wlc_forget(c);
        WLCHECK(wlc_count(c, WLI_xdg_surface, WLE_xdg_surface_configure) == 0, "no configure before the first commit");
        wlc_send(c, s.surface, WLQ_wl_surface_commit);
        WLCHECK(wlc_count(c, WLI_xdg_toplevel, WLE_xdg_toplevel_configure) == 1 &&
                    wlc_count(c, WLI_xdg_surface, WLE_xdg_surface_configure) == 1 &&
                    wlc_count(c, WLI_xdg_toplevel, WLE_xdg_toplevel_configure_bounds) == 1 &&
                    wlc_count(c, WLI_xdg_toplevel, WLE_xdg_toplevel_wm_capabilities) == 1,
                "the first commit is answered with the toplevel's configure, bounds and capabilities, then the surface's");
        e = wlc_take(c, WLI_xdg_toplevel, WLE_xdg_toplevel_configure_bounds);
        WLCHECK(e && e->u[0] == 1280 && e->u[1] == 720, "the bounds are the screen");
        {
                /* the toplevel's configure comes before the surface's, in the stream */
                long toplevel_at = -1, surface_at = -1;

                for (long at = 0; at < c->events; at++)
                {
                        if (c->ev[at].iface == WLI_xdg_toplevel && c->ev[at].opcode == WLE_xdg_toplevel_configure)
                                toplevel_at = at;
                        if (c->ev[at].iface == WLI_xdg_surface && c->ev[at].opcode == WLE_xdg_surface_configure)
                                surface_at = at;
                }
                WLCHECK(toplevel_at >= 0 && toplevel_at < surface_at, "xdg_toplevel.configure precedes xdg_surface.configure");
        }
        e = wlc_take(c, WLI_xdg_surface, WLE_xdg_surface_configure);
        first_serial = e ? e->u[0] : 0;

        /* A buffer before the configure is acknowledged is the client's error */
        {
                struct session t;
                unsigned pixels[4] = {0, 0, 0, 0};

                session_open(&t);
                t.surface = wlc_id(t.c, WLI_wl_surface, 6);
                wlc_send(t.c, t.compositor, WLQ_wl_compositor_create_surface, t.surface);
                t.xdg = wlc_id(t.c, WLI_xdg_surface, 6);
                wlc_send(t.c, t.wm_base, WLQ_xdg_wm_base_get_xdg_surface, t.xdg, t.surface);
                t.toplevel = wlc_id(t.c, WLI_xdg_toplevel, 6);
                wlc_send(t.c, t.xdg, WLQ_xdg_surface_get_toplevel, t.toplevel);
                t.fd = wl_file(64, 0);
                wl_file_put(t.fd, 0, pixels, sizeof pixels);
                t.pool = wlc_id(t.c, WLI_wl_shm_pool, 1);
                wlc_send(t.c, t.shm, WLQ_wl_shm_create_pool, t.pool, t.fd, 64L);
                close((int)t.fd);
                t.buffer = wlc_id(t.c, WLI_wl_buffer, 1);
                wlc_send(t.c, t.pool, WLQ_wl_shm_pool_create_buffer, t.buffer, 0L, 2L, 2L, 8L, (long)WLN_wl_shm_format_xrgb8888);
                wlc_send(t.c, t.surface, WLQ_wl_surface_attach, t.buffer, 0L, 0L);
                wlc_send(t.c, t.surface, WLQ_wl_surface_commit);
                WLCHECK(refused_with(t.c, WLN_xdg_surface_error_unconfigured_buffer), "a buffer before any configure is refused");
                wlc_end(t.c);
        }

        /* the initial configure sent and not acknowledged: a buffer is still the client's error */
        {
                struct session t;
                unsigned pixels[4] = {0, 0, 0, 0};

                session_open(&t);
                t.surface = wlc_id(t.c, WLI_wl_surface, 6);
                wlc_send(t.c, t.compositor, WLQ_wl_compositor_create_surface, t.surface);
                t.xdg = wlc_id(t.c, WLI_xdg_surface, 6);
                wlc_send(t.c, t.wm_base, WLQ_xdg_wm_base_get_xdg_surface, t.xdg, t.surface);
                t.toplevel = wlc_id(t.c, WLI_xdg_toplevel, 6);
                wlc_send(t.c, t.xdg, WLQ_xdg_surface_get_toplevel, t.toplevel);
                wlc_send(t.c, t.surface, WLQ_wl_surface_commit);
                t.fd = wl_file(64, 0);
                wl_file_put(t.fd, 0, pixels, sizeof pixels);
                t.pool = wlc_id(t.c, WLI_wl_shm_pool, 1);
                wlc_send(t.c, t.shm, WLQ_wl_shm_create_pool, t.pool, t.fd, 64L);
                close((int)t.fd);
                t.buffer = wlc_id(t.c, WLI_wl_buffer, 1);
                wlc_send(t.c, t.pool, WLQ_wl_shm_pool_create_buffer, t.buffer, 0L, 2L, 2L, 8L, (long)WLN_wl_shm_format_xrgb8888);
                wlc_send(t.c, t.surface, WLQ_wl_surface_attach, t.buffer, 0L, 0L);
                wlc_send(t.c, t.surface, WLQ_wl_surface_commit);
                WLCHECK(refused_with(t.c, WLN_xdg_surface_error_unconfigured_buffer), "a buffer before the configure is acknowledged is refused");
                wlc_end(t.c);
        }

        /* an acknowledgement of a serial that was not sent is refused; one sent is not */
        wlc_send(c, s.xdg, WLQ_xdg_surface_ack_configure, (long)(first_serial + 100));
        WLCHECK(refused_with(c, WLN_xdg_surface_error_invalid_serial), "a serial that was never sent is refused");
        wlc_end(c);

        /* size asked, states, close */
        session_open(&s);
        session_window(&s, 0xff112233);
        c = s.c;
        wlc_forget(c);
        fake.row[0].width_asked = 500;
        fake.row[0].height_asked = 300;
        wayland_row_wake(0);
        wlc_pump(c);
        e = wlc_take(c, WLI_xdg_toplevel, WLE_xdg_toplevel_configure);
        WLCHECK(e && e->u[0] == 500 && e->u[1] == 300, "a size the compositor asks is a configure");
        WLCHECK(wlc_count(c, WLI_xdg_surface, WLE_xdg_surface_configure) == 1, "with its serial");
        wlc_forget(c);
        wlc_send(c, s.toplevel, WLQ_xdg_toplevel_set_min_size, 320L, 240L);
        wlc_send(c, s.toplevel, WLQ_xdg_toplevel_set_max_size, 640L, 480L);
        wlc_send(c, s.surface, WLQ_wl_surface_commit);
        wlc_pump(c);
        wlc_forget(c);
        fake.row[0].width_asked = 200;
        fake.row[0].height_asked = 800;
        wayland_row_wake(0);
        wlc_pump(c);
        e = wlc_take(c, WLI_xdg_toplevel, WLE_xdg_toplevel_configure);
        WLCHECK(e && e->u[0] == 320 && e->u[1] == 480, "xdg_toplevel configure clamps asked size to min/max bounds");
        wlc_forget(c);
        wlc_send(c, s.toplevel, WLQ_xdg_toplevel_set_min_size, 0L, 0L);
        wlc_send(c, s.toplevel, WLQ_xdg_toplevel_set_max_size, 0L, 0L);
        wlc_send(c, s.surface, WLQ_wl_surface_commit);
        fake.row[0].width_asked = 500;
        fake.row[0].height_asked = 300;
        wayland_row_wake(0);
        wlc_pump(c);
        wlc_forget(c);
        wayland_row_wake(0);
        WLCHECK(wlc_pump(c) == 0, "nothing changed, nothing is sent");
        fake.row[0].states |= WAYLAND_STATE_ACTIVATED;
        wayland_row_wake(0);
        wlc_pump(c);
        e = wlc_take(c, WLI_xdg_toplevel, WLE_xdg_toplevel_configure);
        WLCHECK(e != 0, "a state that changed is a configure");
        wlc_forget(c);
        fake.row[0].closing = 1;
        wayland_row_wake(0);
        wayland_row_wake(0);
        wlc_pump(c);
        WLCHECK(wlc_count(c, WLI_xdg_toplevel, WLE_xdg_toplevel_close) == 1, "a close asked is said once");
        // The client kept its window (it asked the person, who said no): the close button pressed again is a new ask, and is said again.
        WLCHECK(fake.row[0].closing == 1, "and the count of asks is the host's: the shim changes nothing of the row");
        wayland_row_wake(0);
        wlc_pump(c);
        WLCHECK(wlc_count(c, WLI_xdg_toplevel, WLE_xdg_toplevel_close) == 1, "an ask that was said is not said again while the host counts no other");
        fake.row[0].closing = 2;
        wayland_row_wake(0);
        wayland_row_wake(0);
        wlc_pump(c);
        WLCHECK(wlc_count(c, WLI_xdg_toplevel, WLE_xdg_toplevel_close) == 2, "a close asked again of a window that stayed is said again (%ld)",
                wlc_count(c, WLI_xdg_toplevel, WLE_xdg_toplevel_close));
        // And of the window the surface shows after a null buffer took the first away.
        {
                struct wl_event_seen *again;

                wlc_send(c, s.surface, WLQ_wl_surface_attach, 0UL, 0L, 0L);
                wlc_send(c, s.surface, WLQ_wl_surface_commit);
                wlc_forget(c);
                wlc_send(c, s.surface, WLQ_wl_surface_commit);
                again = wlc_take(c, WLI_xdg_surface, WLE_xdg_surface_configure);
                wlc_send(c, s.xdg, WLQ_xdg_surface_ack_configure, again ? (long)again->u[0] : 0L);
                wlc_send(c, s.surface, WLQ_wl_surface_attach, s.buffer, 0L, 0L);
                wlc_send(c, s.surface, WLQ_wl_surface_commit);
                wlc_forget(c);
                // The window before was told of two asks, and this one has been asked as many times: the count told is a window's, not
                // its surface's, so this is an ask the client has not heard.
                fake.row[0].closing = 2;
                wayland_row_wake(0);
                wlc_pump(c);
                WLCHECK(c->errors == 0 && fake.row[0].used && wlc_count(c, WLI_xdg_toplevel, WLE_xdg_toplevel_close) == 1,
                        "and of the window it shows again after a null buffer (%ld)", wlc_count(c, WLI_xdg_toplevel, WLE_xdg_toplevel_close));
        }
        wlc_send(c, s.toplevel, WLQ_xdg_toplevel_destroy);
        WLCHECK(c->errors == 0 && !fake.row[0].used, "the toplevel's destroy takes the window");
        wlc_end(c);

        /* a client that never acknowledges is sent no more than the cap */
        session_open(&s);
        session_window(&s, 0xff000000);
        c = s.c;
        wlc_forget(c);
        for (int at = 0; at < 20; at++)
        {
                fake.row[0].width_asked = 100 + at;
                wayland_row_wake(0);
        }
        wlc_pump(c);
        WLCHECK(wlc_count(c, WLI_xdg_surface, WLE_xdg_surface_configure) <= WAYLAND_SERIALS, "unacknowledged configures are capped (%ld)",
                wlc_count(c, WLI_xdg_surface, WLE_xdg_surface_configure));
        wlc_end(c);
        leak_check("configure");
        wayland_stop();
}

/*
        A toplevel's fullscreen and maximized are the client's last word on them, whatever window they were asked of: a window that a
        null buffer took away and that is shown again is in the state the client last asked, not an older one, nor none.
*/
static void state_role(struct session *s)
{
        struct wlc *c = s->c;
        unsigned pixels[32 * 24];

        for (int at = 0; at < 32 * 24; at++)
                pixels[at] = 0xff00ff00u;
        s->fd = wl_file(32 * 24 * 4 * 2, 0);
        wl_file_put(s->fd, 0, pixels, sizeof pixels);
        s->pool = wlc_id(c, WLI_wl_shm_pool, 1);
        wlc_send(c, s->shm, WLQ_wl_shm_create_pool, s->pool, s->fd, (long)(32 * 24 * 4 * 2));
        close((int)s->fd);
        s->buffer = wlc_id(c, WLI_wl_buffer, 1);
        wlc_send(c, s->pool, WLQ_wl_shm_pool_create_buffer, s->buffer, 0L, 32L, 24L, 128L, (long)WLN_wl_shm_format_xrgb8888);
        s->surface = wlc_id(c, WLI_wl_surface, 6);
        wlc_send(c, s->compositor, WLQ_wl_compositor_create_surface, s->surface);
        s->xdg = wlc_id(c, WLI_xdg_surface, 6);
        wlc_send(c, s->wm_base, WLQ_xdg_wm_base_get_xdg_surface, s->xdg, s->surface);
        s->toplevel = wlc_id(c, WLI_xdg_toplevel, 6);
        wlc_send(c, s->xdg, WLQ_xdg_surface_get_toplevel, s->toplevel);
}

/* The configure a commit was answered with, acknowledged; its state words are the array's length (4 for each). */
static unsigned long state_ack(struct session *s, struct wl_event_seen *e)
{
        if (e)
                wlc_send(s->c, s->xdg, WLQ_xdg_surface_ack_configure, (long)e->u[0]);
        return e ? e->u[2] : 0;
}

/*
        A window that changed while its client had WAYLAND_SERIALS configures unacknowledged is told of the change once the client has
        acknowledged them: the configure that was not sent is owed, and nothing else would send it until the window changed again.
*/
static void test_configure_owed(void)
{
        struct session s;
        struct wlc *c;
        struct wl_event_seen *e;
        unsigned long serial[WAYLAND_SERIALS];
        long count = 0;

        wl_reset();
        wayland_start(&fake_host);
        session_open(&s);
        session_window(&s, 0xff112233);
        c = s.c;
        wlc_forget(c);
        for (int at = 0; at < WAYLAND_SERIALS + 2; at++)
        {
                fake.row[0].width_asked = 100 + at;
                wayland_row_wake(0);
        }
        wlc_pump(c);
        for (long at = 0; at < c->events; at++)
                if (c->ev[at].iface == WLI_xdg_surface && c->ev[at].opcode == WLE_xdg_surface_configure && count < WAYLAND_SERIALS)
                        serial[count++] = c->ev[at].u[0];
        WLCHECK(count == WAYLAND_SERIALS, "the configures up to the cap are sent (%ld)", count);
        wlc_forget(c);
        for (long at = 0; at < count; at++)
                wlc_send(c, s.xdg, WLQ_xdg_surface_ack_configure, (long)serial[at]);
        wlc_pump(c);
        e = wlc_take(c, WLI_xdg_toplevel, WLE_xdg_toplevel_configure);
        WLCHECK(e && e->u[0] == 109 && c->errors == 0,
                "a change made while the client had the cap unacknowledged is told once it acknowledges (%lu)", e ? e->u[0] : 0UL);
        wlc_end(c);
        leak_check("configure owed");
        wayland_stop();
}

static void test_state_remap(void)
{
        struct session s;
        struct wlc *c;
        struct wl_event_seen *e;

        wl_reset();
        wayland_start(&fake_host);

        /* fullscreen asked before the window is shown, let go while it is shown, the window unmapped and shown again: not fullscreen */
        session_open(&s);
        state_role(&s);
        c = s.c;
        wlc_send(c, s.toplevel, WLQ_xdg_toplevel_set_fullscreen, 0L);
        wlc_send(c, s.surface, WLQ_wl_surface_commit);
        wlc_pump(c);
        e = wlc_take(c, WLI_xdg_toplevel, WLE_xdg_toplevel_configure);
        WLCHECK(e && e->u[0] == 1280 && e->u[1] == 720 && e->u[2] == 8,
                "fullscreen before initial commit configures the screen's size and fullscreen state (%lu %lu %lu)",
                e ? e->u[0] : 0UL, e ? e->u[1] : 0UL, e ? e->u[2] : 0UL);
        e = wlc_take(c, WLI_xdg_surface, WLE_xdg_surface_configure);
        WLCHECK(e != 0, "the initial configure comes with the first commit");
        state_ack(&s, e);
        wlc_send(c, s.surface, WLQ_wl_surface_attach, s.buffer, 0L, 0L);
        wlc_send(c, s.surface, WLQ_wl_surface_damage_buffer, 0L, 0L, 32L, 24L);
        wlc_send(c, s.surface, WLQ_wl_surface_commit);
        wlc_forget(c);
        wayland_row_wake(0);
        wlc_pump(c);
        WLCHECK(c->errors == 0 && (fake.row[0].states & WAYLAND_STATE_FULLSCREEN), "a fullscreen asked before the first buffer is shown (%s)",
                c->error_text);
        e = wlc_take(c, WLI_xdg_toplevel, WLE_xdg_toplevel_configure);
        WLCHECK(e && e->u[2] == 4, "and told in the configure (states %lu)", e ? e->u[2] : 0UL);
        state_ack(&s, wlc_take(c, WLI_xdg_surface, WLE_xdg_surface_configure));
        wlc_send(c, s.toplevel, WLQ_xdg_toplevel_unset_fullscreen);
        wlc_forget(c);
        wayland_row_wake(0);
        wlc_pump(c);
        e = wlc_take(c, WLI_xdg_toplevel, WLE_xdg_toplevel_configure);
        WLCHECK(e && e->u[2] == 0 && !(fake.row[0].states & WAYLAND_STATE_FULLSCREEN), "unfullscreen while shown is a configure with no state");
        state_ack(&s, wlc_take(c, WLI_xdg_surface, WLE_xdg_surface_configure));
        wlc_send(c, s.surface, WLQ_wl_surface_attach, 0UL, 0L, 0L);
        wlc_send(c, s.surface, WLQ_wl_surface_commit);
        WLCHECK(c->errors == 0 && !fake.row[0].used, "a null buffer unmaps the window (%s)", c->error_text);
        wlc_forget(c);
        wlc_send(c, s.surface, WLQ_wl_surface_commit);
        wlc_pump(c);
        e = wlc_take(c, WLI_xdg_surface, WLE_xdg_surface_configure);
        WLCHECK(e != 0, "the next commit is the initial one again");
        state_ack(&s, e);
        wlc_send(c, s.surface, WLQ_wl_surface_attach, s.buffer, 0L, 0L);
        wlc_send(c, s.surface, WLQ_wl_surface_damage_buffer, 0L, 0L, 32L, 24L);
        wlc_send(c, s.surface, WLQ_wl_surface_commit);
        wayland_row_wake(0);
        wlc_pump(c);
        WLCHECK(c->errors == 0 && fake.row[0].used && !(fake.row[0].states & WAYLAND_STATE_FULLSCREEN),
                "a window shown again is not fullscreen when the client let go of it (states %ld)", fake.row[0].states);
        wlc_end(c);
        leak_check("state, unfullscreen then remapped");
        wayland_stop();

        /* fullscreen asked while shown, the window unmapped and shown again: still asked for, so fullscreen again */
        wl_reset();
        wayland_start(&fake_host);
        session_open(&s);
        state_role(&s);
        c = s.c;
        wlc_send(c, s.surface, WLQ_wl_surface_commit);
        wlc_pump(c);
        state_ack(&s, wlc_take(c, WLI_xdg_surface, WLE_xdg_surface_configure));
        wlc_send(c, s.surface, WLQ_wl_surface_attach, s.buffer, 0L, 0L);
        wlc_send(c, s.surface, WLQ_wl_surface_damage_buffer, 0L, 0L, 32L, 24L);
        wlc_send(c, s.surface, WLQ_wl_surface_commit);
        wlc_send(c, s.toplevel, WLQ_xdg_toplevel_set_fullscreen, 0L);
        wayland_row_wake(0);
        wlc_forget(c);
        wlc_pump(c);
        WLCHECK(c->errors == 0 && (fake.row[0].states & WAYLAND_STATE_FULLSCREEN), "fullscreen asked while shown is done (%s)", c->error_text);
        state_ack(&s, wlc_take(c, WLI_xdg_surface, WLE_xdg_surface_configure));
        wlc_send(c, s.surface, WLQ_wl_surface_attach, 0UL, 0L, 0L);
        wlc_send(c, s.surface, WLQ_wl_surface_commit);
        wlc_send(c, s.surface, WLQ_wl_surface_commit);
        wlc_pump(c);
        state_ack(&s, wlc_take(c, WLI_xdg_surface, WLE_xdg_surface_configure));
        wlc_send(c, s.surface, WLQ_wl_surface_attach, s.buffer, 0L, 0L);
        wlc_send(c, s.surface, WLQ_wl_surface_damage_buffer, 0L, 0L, 32L, 24L);
        wlc_send(c, s.surface, WLQ_wl_surface_commit);
        wayland_row_wake(0);
        wlc_pump(c);
        WLCHECK(c->errors == 0 && fake.row[0].used && (fake.row[0].states & WAYLAND_STATE_FULLSCREEN),
                "a window shown again is fullscreen when the client has not let go of it (states %ld)", fake.row[0].states);
        wlc_end(c);
        leak_check("state, fullscreen then remapped");
        wayland_stop();

        /* fullscreen asked while shown, the compositor takes it away, the window unmapped and shown again: not asked for any more */
        wl_reset();
        wayland_start(&fake_host);
        session_open(&s);
        state_role(&s);
        c = s.c;
        wlc_forget(c);
        wlc_send(c, s.surface, WLQ_wl_surface_commit);
        wlc_pump(c);
        state_ack(&s, wlc_take(c, WLI_xdg_surface, WLE_xdg_surface_configure));
        wlc_forget(c);
        wlc_send(c, s.surface, WLQ_wl_surface_attach, s.buffer, 0L, 0L);
        wlc_send(c, s.surface, WLQ_wl_surface_damage_buffer, 0L, 0L, 32L, 24L);
        wlc_send(c, s.surface, WLQ_wl_surface_commit);
        wayland_row_wake(0);
        wlc_pump(c);
        state_ack(&s, wlc_take(c, WLI_xdg_surface, WLE_xdg_surface_configure));
        wlc_forget(c);
        wlc_send(c, s.toplevel, WLQ_xdg_toplevel_set_fullscreen, 0L);
        wayland_row_wake(0);
        wlc_pump(c);
        WLCHECK(c->errors == 0 && (fake.row[0].states & WAYLAND_STATE_FULLSCREEN), "fullscreen asked while shown is done (%s)", c->error_text);
        state_ack(&s, wlc_take(c, WLI_xdg_surface, WLE_xdg_surface_configure));
        wlc_forget(c);
        fake.row[0].states &= ~(long)WAYLAND_STATE_FULLSCREEN; /* the compositor (the person) takes the fullscreen away, the client not asking */
        wayland_row_wake(0);
        wlc_pump(c);
        WLCHECK(wlc_take(c, WLI_xdg_toplevel, WLE_xdg_toplevel_configure) != 0, "the compositor's own change of state is a configure");
        state_ack(&s, wlc_take(c, WLI_xdg_surface, WLE_xdg_surface_configure));
        wlc_forget(c);
        wlc_send(c, s.surface, WLQ_wl_surface_attach, 0UL, 0L, 0L);
        wlc_send(c, s.surface, WLQ_wl_surface_commit);
        wlc_send(c, s.surface, WLQ_wl_surface_commit);
        wlc_pump(c);
        state_ack(&s, wlc_take(c, WLI_xdg_surface, WLE_xdg_surface_configure));
        wlc_forget(c);
        wlc_send(c, s.surface, WLQ_wl_surface_attach, s.buffer, 0L, 0L);
        wlc_send(c, s.surface, WLQ_wl_surface_damage_buffer, 0L, 0L, 32L, 24L);
        wlc_send(c, s.surface, WLQ_wl_surface_commit);
        wayland_row_wake(0);
        wlc_pump(c);
        WLCHECK(c->errors == 0 && fake.row[0].used && !(fake.row[0].states & WAYLAND_STATE_FULLSCREEN),
                "a window the compositor took out of fullscreen is not put back in it when shown again (states %ld)", fake.row[0].states);
        wlc_end(c);
        leak_check("state, fullscreen taken away then remapped");
        wayland_stop();
}

/* A window's life in every order a client can end it in. */
static void test_destroy_orders(void)
{
        struct session s;
        struct wlc *c;

        wl_reset();
        wayland_start(&fake_host);

        /* surface first, then the xdg_surface, which is inert and may be destroyed */
        session_open(&s);
        session_window(&s, 0xff00ff00);
        c = s.c;
        wlc_send(c, s.surface, WLQ_wl_surface_destroy);
        WLCHECK(c->errors == 0 && !fake.row[0].used, "a destroyed surface takes its window");
        wlc_send(c, s.toplevel, WLQ_xdg_toplevel_destroy);
        wlc_send(c, s.xdg, WLQ_xdg_surface_destroy);
        WLCHECK(c->errors == 0, "the role objects of a destroyed surface may be destroyed (%s)", c->error_text);
        wlc_end(c);

        /* the xdg_surface with its toplevel still alive is the client's error */
        session_open(&s);
        session_window(&s, 0xff00ff00);
        c = s.c;
        wlc_send(c, s.xdg, WLQ_xdg_surface_destroy);
        WLCHECK(refused_with(c, WLN_xdg_surface_error_defunct_role_object), "an xdg_surface destroyed before its toplevel");
        wlc_end(c);

        /* the toplevel destroyed, then made again on the same xdg_surface: the window shows again */
        session_open(&s);
        session_window(&s, 0xff0000ff);
        c = s.c;
        wlc_send(c, s.toplevel, WLQ_xdg_toplevel_destroy);
        WLCHECK(!fake.row[0].used, "the toplevel destroyed unmaps the window");
        s.toplevel = wlc_id(c, WLI_xdg_toplevel, 6);
        wlc_send(c, s.xdg, WLQ_xdg_surface_get_toplevel, s.toplevel);
        wlc_send(c, s.surface, WLQ_wl_surface_commit);
        WLCHECK(c->errors == 0 && wlc_count(c, WLI_xdg_surface, WLE_xdg_surface_configure) == 2, "a new toplevel is configured again (%s)",
                c->error_text);
        wlc_end(c);

        /* a null buffer unmaps; the next buffer needs a configure again */
        session_open(&s);
        session_window(&s, 0xff0000ff);
        c = s.c;
        wlc_send(c, s.surface, WLQ_wl_surface_attach, 0UL, 0L, 0L);
        wlc_send(c, s.surface, WLQ_wl_surface_commit);
        WLCHECK(c->errors == 0 && !fake.row[0].used, "a null buffer unmaps the window");
        wlc_send(c, s.surface, WLQ_wl_surface_commit);
        WLCHECK(c->errors == 0 && wlc_count(c, WLI_xdg_surface, WLE_xdg_surface_configure) == 2, "and the next commit is the initial one");
        wlc_end(c);

        /* the pool destroyed before its buffer, the buffer after the surface */
        session_open(&s);
        session_window(&s, 0xff0000ff);
        c = s.c;
        wlc_send(c, s.pool, WLQ_wl_shm_pool_destroy);
        wlc_send(c, s.surface, WLQ_wl_surface_destroy);
        wlc_send(c, s.buffer, WLQ_wl_buffer_destroy);
        WLCHECK(c->errors == 0, "pool, surface and buffer in the wrong order (%s)", c->error_text);
        wlc_end(c);

        /* a client that goes at every stage of the opening */
        for (int stage = 0; stage < 8; stage++)
        {
                struct session t;
                unsigned pixels[4] = {0, 0, 0, 0};

                session_open(&t);
                if (stage > 0)
                {
                        t.surface = wlc_id(t.c, WLI_wl_surface, 6);
                        wlc_send(t.c, t.compositor, WLQ_wl_compositor_create_surface, t.surface);
                }
                if (stage > 1)
                {
                        t.xdg = wlc_id(t.c, WLI_xdg_surface, 6);
                        wlc_send(t.c, t.wm_base, WLQ_xdg_wm_base_get_xdg_surface, t.xdg, t.surface);
                }
                if (stage > 2)
                {
                        t.toplevel = wlc_id(t.c, WLI_xdg_toplevel, 6);
                        wlc_send(t.c, t.xdg, WLQ_xdg_surface_get_toplevel, t.toplevel);
                }
                if (stage > 3)
                        wlc_send(t.c, t.surface, WLQ_wl_surface_commit);
                if (stage > 4)
                {
                        t.fd = wl_file(64, 0);
                        wl_file_put(t.fd, 0, pixels, sizeof pixels);
                        t.pool = wlc_id(t.c, WLI_wl_shm_pool, 1);
                        wlc_send(t.c, t.shm, WLQ_wl_shm_create_pool, t.pool, t.fd, 64L);
                        close((int)t.fd);
                }
                if (stage > 5)
                {
                        t.buffer = wlc_id(t.c, WLI_wl_buffer, 1);
                        wlc_send(t.c, t.pool, WLQ_wl_shm_pool_create_buffer, t.buffer, 0L, 2L, 2L, 8L, (long)WLN_wl_shm_format_xrgb8888);
                }
                if (stage > 6)
                {
                        struct wl_event_seen *e = wlc_take(t.c, WLI_xdg_surface, WLE_xdg_surface_configure);

                        wlc_send(t.c, t.xdg, WLQ_xdg_surface_ack_configure, e ? (long)e->u[0] : 0L);
                        wlc_send(t.c, t.surface, WLQ_wl_surface_attach, t.buffer, 0L, 0L);
                        wlc_send(t.c, t.surface, WLQ_wl_surface_frame, wlc_id(t.c, WLI_wl_callback, 1));
                        wlc_send(t.c, t.surface, WLQ_wl_surface_commit);
                }
                wlc_end(t.c);
        }
        leak_check("destroy orders");
        WLCHECK(fds_balanced(), "descriptors balance (%ld %ld)", fake.fds_open, fake.fds_closed);
        wayland_stop();
}

/* The shm pool and buffer rules, the client's every mistake. */
static void test_shm(void)
{
        struct session s;
        struct wlc *c;
        long fd, pipes[2];

        wl_reset();
        wayland_start(&fake_host);

#define FRESH() (session_open(&s), c = s.c, s.pool = wlc_id(c, WLI_wl_shm_pool, 1))
        /* a size that is zero or absurd, a file too short, a file that is not a file */
        FRESH();
        fd = wl_file(4096, 0);
        wlc_send(c, s.shm, WLQ_wl_shm_create_pool, s.pool, fd, 0L);
        WLCHECK(refused_with(c, WLN_wl_shm_error_invalid_stride), "a pool of size 0");
        wlc_end(c);
        FRESH();
        fd = wl_file(4096, 0);
        wlc_send(c, s.shm, WLQ_wl_shm_create_pool, s.pool, fd, -1L);
        WLCHECK(refused_with(c, WLN_wl_shm_error_invalid_stride), "a pool of a negative size");
        wlc_end(c);
        /* pools past the 1 GiB one was held to, and past the 2 GiB a client was: three of 1.5 GiB (sparse files), each mapped */
        FRESH();
        {
                unsigned long pools[3];

                for (int at = 0; at < 3; at++)
                {
                        char name[] = "/tmp/wlcheckXXXXXX";
                        int big = mkstemp(name);

                        unlink(name);
                        if (big < 0 || ftruncate(big, 0x60000000L) < 0)
                                abort();
                        fake.fds_open++;
                        pools[at] = wlc_id(c, WLI_wl_shm_pool, 1);
                        wlc_send(c, s.shm, WLQ_wl_shm_create_pool, pools[at], (long)big, 0x60000000L);
                }
                wlc_pump(c);
                long mapped = 0;

                for (long at = 0; wayland.client[c->slot] && at < wayland.client[c->slot]->pool_table.count; at++)
                        mapped += wayland.client[c->slot]->pools[at].live && wayland.client[c->slot]->pools[at].at ? wayland.client[c->slot]->pools[at].size : 0;
                WLCHECK(c->errors == 0 && wayland.client[c->slot] && mapped == 3 * 0x60000000L, "three pools of 1.5 GiB are each mapped, 4.5 GiB for one client");
                for (int at = 0; at < 3; at++)
                        wlc_send(c, pools[at], WLQ_wl_shm_pool_destroy);
        }
        wlc_end(c);
        FRESH();
        fd = wl_file(4096, 0);
        wlc_send(c, s.shm, WLQ_wl_shm_create_pool, s.pool, fd, 8192L);
        WLCHECK(refused_with(c, WLN_wl_shm_error_invalid_fd), "a file shorter than the pool");
        wlc_end(c);
        FRESH();
        if (pipe((int *)pipes) == 0)
        {
                int p[2];

                if (pipe(p) == 0)
                {
                        fake.fds_open++;
                        wlc_send(c, s.shm, WLQ_wl_shm_create_pool, s.pool, (long)p[0], 4096L);
                        WLCHECK(refused_with(c, WLN_wl_shm_error_invalid_fd), "a pipe is not a pool");
                        close(p[1]);
                }
                close((int)pipes[0]);
                close((int)pipes[1]);
        }
        wlc_end(c);

        /* a buffer that does not fit, a format that is not offered, a stride that is short */
        FRESH();
        fd = wl_file(4096, 0);
        wlc_send(c, s.shm, WLQ_wl_shm_create_pool, s.pool, fd, 4096L);
        close((int)fd);
        s.buffer = wlc_id(c, WLI_wl_buffer, 1);
        wlc_send(c, s.pool, WLQ_wl_shm_pool_create_buffer, s.buffer, 0L, 32L, 33L, 128L, (long)WLN_wl_shm_format_xrgb8888);
        WLCHECK(refused_with(c, WLN_wl_shm_error_invalid_stride), "a buffer past the end of its pool");
        wlc_end(c);
        FRESH();
        fd = wl_file(4096, 0);
        wlc_send(c, s.shm, WLQ_wl_shm_create_pool, s.pool, fd, 4096L);
        close((int)fd);
        s.buffer = wlc_id(c, WLI_wl_buffer, 1);
        wlc_send(c, s.pool, WLQ_wl_shm_pool_create_buffer, s.buffer, 0L, 8L, 8L, 32L, 777L);
        WLCHECK(refused_with(c, WLN_wl_shm_error_invalid_format), "a format that is not offered");
        wlc_end(c);
        FRESH();
        fd = wl_file(4096, 0);
        wlc_send(c, s.shm, WLQ_wl_shm_create_pool, s.pool, fd, 4096L);
        close((int)fd);
        s.buffer = wlc_id(c, WLI_wl_buffer, 1);
        wlc_send(c, s.pool, WLQ_wl_shm_pool_create_buffer, s.buffer, 0L, 8L, 8L, 16L, (long)WLN_wl_shm_format_xrgb8888);
        WLCHECK(refused_with(c, WLN_wl_shm_error_invalid_stride), "a stride shorter than a row");
        wlc_end(c);
        FRESH();
        fd = wl_file(4096, 0);
        wlc_send(c, s.shm, WLQ_wl_shm_create_pool, s.pool, fd, 4096L);
        close((int)fd);
        s.buffer = wlc_id(c, WLI_wl_buffer, 1);
        wlc_send(c, s.pool, WLQ_wl_shm_pool_create_buffer, s.buffer, 0L, -1L, 8L, 32L, (long)WLN_wl_shm_format_xrgb8888);
        WLCHECK(refused_with(c, WLN_wl_shm_error_invalid_stride), "a negative width");
        wlc_end(c);
        FRESH();
        fd = wl_file(4096, 0);
        wlc_send(c, s.shm, WLQ_wl_shm_create_pool, s.pool, fd, 4096L);
        close((int)fd);
        s.buffer = wlc_id(c, WLI_wl_buffer, 1);
        wlc_send(c, s.pool, WLQ_wl_shm_pool_create_buffer, s.buffer, 4000L, 8L, 8L, 32L, (long)WLN_wl_shm_format_xrgb8888);
        WLCHECK(refused_with(c, WLN_wl_shm_error_invalid_stride), "an offset that runs past the end");
        wlc_end(c);
        FRESH();
        fd = wl_file(4096, 0);
        wlc_send(c, s.shm, WLQ_wl_shm_create_pool, s.pool, fd, 4096L);
        close((int)fd);
        s.buffer = wlc_id(c, WLI_wl_buffer, 1);
        wlc_send(c, s.pool, WLQ_wl_shm_pool_create_buffer, s.buffer, 0L, 0x7fffffffL, 0x7fffffffL, 0x7fffffffL, (long)WLN_wl_shm_format_xrgb8888);
        WLCHECK(refused_with(c, WLN_wl_shm_error_invalid_stride), "sizes that would overflow");
        wlc_end(c);
#ifdef __linux__
        /* a pool may grow and may not shrink */
        FRESH();
        fd = wl_file(8192, 0);
        wlc_send(c, s.shm, WLQ_wl_shm_create_pool, s.pool, fd, 4096L);
        close((int)fd);
        wlc_send(c, s.pool, WLQ_wl_shm_pool_resize, 8192L);
        WLCHECK(c->errors == 0, "a pool grows (%s)", c->error_text);
        wlc_send(c, s.pool, WLQ_wl_shm_pool_resize, 4096L);
        WLCHECK(refused_with(c, WLN_wl_shm_error_invalid_fd), "a pool does not shrink");
        wlc_end(c);
#endif
        /* pools past the old cap of 96, then a commit refused: the next pool is refused with no_memory, and nothing else */
        FRESH();
        for (int at = 0; at < 1000 && !c->errors; at++)
        {
                unsigned long id = wlc_id(c, WLI_wl_shm_pool, 1);

                fd = wl_file(4096, 0);
                wlc_send(c, s.shm, WLQ_wl_shm_create_pool, id, fd, 4096L);
                close((int)fd);
        }
        WLCHECK(c->errors == 0, "a client holds a thousand pools (%s)", c->error_text);
        fake.commit_fail_at = 1;
        for (int at = 0; at < 2000 && !c->errors; at++)
        {
                unsigned long id = wlc_id(c, WLI_wl_shm_pool, 1);

                fd = wl_file(4096, 0);
                wlc_send(c, s.shm, WLQ_wl_shm_create_pool, id, fd, 4096L);
                close((int)fd);
        }
        fake.commit_fail_at = 0;
        WLCHECK(refused_with(c, WLN_wl_display_error_no_memory), "a pool the memory is not there for is refused with no_memory, not crashed");
        wlc_end(c);
        leak_check("shm");
        WLCHECK(fds_balanced(), "descriptors balance (%ld %ld)", fake.fds_open, fake.fds_closed);
        wayland_stop();
#undef FRESH
}

static void test_frames(void)
{
        struct session s;
        struct wlc *c;
        unsigned long callback;

        wl_reset();
        wayland_start(&fake_host);
        session_open(&s);
        session_window(&s, 0xff223344);
        c = s.c;
        callback = wlc_id(c, WLI_wl_callback, 1);
        wlc_forget(c);
        wlc_send(c, s.surface, WLQ_wl_surface_frame, callback);
        wlc_send(c, s.surface, WLQ_wl_surface_commit);
        WLCHECK(wlc_count(c, WLI_wl_callback, WLE_wl_callback_done) == 0, "a frame callback waits for the frame");
        WLCHECK(wayland.client[c->slot]->waiting == 1, "and the client counts one surface waiting for it (%ld)", wayland.client[c->slot]->waiting);
        WLCHECK(wayland_frame_wanted(), "and the compositor is told one is wanted");
        wayland_frame();
        wlc_pump(c);
        WLCHECK(wlc_count(c, WLI_wl_callback, WLE_wl_callback_done) == 1, "it is answered when the frame is drawn");
        WLCHECK(wayland.client[c->slot]->waiting == 0, "and the client counts none waiting after it");
        WLCHECK(!wayland_frame_wanted(), "and nothing waits after it");
        wayland_frame();
        wlc_pump(c);
        WLCHECK(wlc_count(c, WLI_wl_callback, WLE_wl_callback_done) == 1, "once");

        /* a window put away is told of no frame, and of the next one when it is back */
        callback = wlc_id(c, WLI_wl_callback, 1);
        wlc_send(c, s.surface, WLQ_wl_surface_frame, callback);
        wlc_send(c, s.surface, WLQ_wl_surface_commit);
        fake.row[0].states |= WAYLAND_STATE_SUSPENDED;
        wlc_forget(c);
        WLCHECK(!wayland_frame_wanted(), "a window put away is not woken for the frame no one will answer");
        wayland_frame();
        wlc_pump(c);
        WLCHECK(wlc_count(c, WLI_wl_callback, WLE_wl_callback_done) == 0, "no frame for a window put away");
        fake.row[0].states &= ~(long)WAYLAND_STATE_SUSPENDED;
        WLCHECK(wayland_frame_wanted(), "and a frame is wanted again once it is shown");
        wayland_frame();
        wlc_pump(c);
        WLCHECK(wlc_count(c, WLI_wl_callback, WLE_wl_callback_done) == 1, "and the next one when it is shown again");

        /* a surface with no window has no frame to wait for */
        {
                unsigned long plain = wlc_id(c, WLI_wl_surface, 6), cb = wlc_id(c, WLI_wl_callback, 1);

                wlc_send(c, s.compositor, WLQ_wl_compositor_create_surface, plain);
                wlc_forget(c);
                wlc_send(c, plain, WLQ_wl_surface_frame, cb);
                wlc_send(c, plain, WLQ_wl_surface_commit);
                WLCHECK(wlc_count(c, WLI_wl_callback, WLE_wl_callback_done) == 1, "a surface with no window is answered at its commit");
        }

        /* more callbacks than the cap are answered, not kept without end */
        for (int at = 0; at < WAYLAND_CALLBACKS + 8; at++)
                wlc_send(c, s.surface, WLQ_wl_surface_frame, wlc_id(c, WLI_wl_callback, 1));
        WLCHECK(c->errors == 0, "many frame requests are no error");
        wlc_send(c, s.surface, WLQ_wl_surface_destroy);
        WLCHECK(c->errors == 0, "a surface destroyed with callbacks waiting");
        wlc_end(c);
        leak_check("frames");
        wayland_stop();
}

static unsigned long wl_buffer_of(struct session *t, long width, long height, unsigned colour, long format);

/* Whether a callback of the client was answered (wl_callback.done on that object). */
static int wl_done(struct wlc *c, unsigned long callback)
{
        for (long at = 0; at < c->events; at++)
                if (c->ev[at].iface == WLI_wl_callback && c->ev[at].opcode == WLE_wl_callback_done && c->ev[at].object == callback)
                        return 1;
        return 0;
}

/*
        An SDL 2 OpenGL window opened the way SDL 2.32 opens it (Half-Life 2's trace on box, 2026-11-18): the surface made with
        its two frame callbacks while the window is hidden, then shown (a null attach and a commit, the xdg_surface and the
        toplevel, app id, size limits, title, a commit, the wait for the configure), hidden and shown again on the same
        surface (what SDL does to change shells), a frame callback asked after the last commit, then the first eglSwapBuffers.
        The registry offers no xdg-decoration: told client_side by it, SDL tore the first toplevel down inside its wait for
        that toplevel's configure and the wait never ended (no window, the game in wl_display_dispatch forever).
*/
static void test_sdl_show(void)
{
        struct session s;
        struct wlc *c;
        struct wl_event_seen *e;
        unsigned long gl_frame, damage_frame, sync, first_serial, buffer;
        long decoration_events = 0;

        wl_reset();
        wayland_start(&fake_host);
        session_open(&s);
        c = s.c;
        WLCHECK(wl_global(c, s.registry, WLI_zxdg_decoration_manager_v1, 1) == 0,
                "sdl: the registry offers no xdg-decoration (a client that lacks it draws its own header, as this compositor wants)");
        /* SDL_CreateWindow, hidden: the surface and its two frame callbacks (Mesa's swap queue and SDL's damage), no commit */
        s.surface = wlc_id(c, WLI_wl_surface, 6);
        wlc_send(c, s.compositor, WLQ_wl_compositor_create_surface, s.surface);
        wlc_send(c, s.surface, WLQ_wl_surface_set_buffer_scale, 1L);
        gl_frame = wlc_id(c, WLI_wl_callback, 1);
        damage_frame = wlc_id(c, WLI_wl_callback, 1);
        wlc_send(c, s.surface, WLQ_wl_surface_frame, gl_frame);
        wlc_send(c, s.surface, WLQ_wl_surface_frame, damage_frame);
        WLCHECK(!wl_done(c, gl_frame) && !wl_done(c, damage_frame), "sdl: the hidden window's frame callbacks wait for a commit");
        /* Wayland_ShowWindow: a null attach and a commit, then the role */
        wlc_send(c, s.surface, WLQ_wl_surface_attach, 0L, 0L, 0L);
        wlc_send(c, s.surface, WLQ_wl_surface_commit);
        WLCHECK(wl_done(c, gl_frame) && wl_done(c, damage_frame), "sdl: a bufferless surface with no role answers its frames at the commit");
        damage_frame = wlc_id(c, WLI_wl_callback, 1);
        wlc_send(c, s.surface, WLQ_wl_surface_frame, damage_frame);
        s.xdg = wlc_id(c, WLI_xdg_surface, 6);
        wlc_send(c, s.wm_base, WLQ_xdg_wm_base_get_xdg_surface, s.xdg, s.surface);
        s.toplevel = wlc_id(c, WLI_xdg_toplevel, 6);
        wlc_send(c, s.xdg, WLQ_xdg_surface_get_toplevel, s.toplevel);
        wlc_send(c, s.toplevel, WLQ_xdg_toplevel_set_app_id, "hl2_linux");
        wlc_send(c, s.toplevel, WLQ_xdg_toplevel_set_min_size, 640L, 480L);
        wlc_send(c, s.toplevel, WLQ_xdg_toplevel_set_max_size, 640L, 480L);
        wlc_send(c, s.toplevel, WLQ_xdg_toplevel_set_title, "HALF-LIFE 2 - OpenGL");
        wlc_forget(c);
        wlc_send(c, s.surface, WLQ_wl_surface_commit);
        WLCHECK(wlc_count(c, WLI_xdg_toplevel, WLE_xdg_toplevel_configure) == 1 && wlc_count(c, WLI_xdg_surface, WLE_xdg_surface_configure) == 1,
                "sdl: the first commit of the toplevel is answered with its configure, then the surface's, in the same batch");
        for (long at = 0; at < c->events; at++)
                decoration_events += c->ev[at].iface == WLI_zxdg_toplevel_decoration_v1;
        WLCHECK(decoration_events == 0, "sdl: and nothing in that batch tells it a decoration mode (%ld)", decoration_events);
        e = wlc_take(c, WLI_xdg_surface, WLE_xdg_surface_configure);
        first_serial = e ? e->u[0] : 0;
        WLCHECK(e != 0, "sdl: the wait for the configure ends");
        if (e)
                wlc_send(c, s.xdg, WLQ_xdg_surface_ack_configure, (long)first_serial);
        sync = wlc_id(c, WLI_wl_callback, 1);
        wlc_send(c, 1, WLQ_wl_display_sync, sync);
        WLCHECK(wl_done(c, sync) && c->errors == 0, "sdl: the show's roundtrip is answered (%s)", c->error_text);
        /* hidden and shown again on the same surface (SDL changing shells, fullscreen and libdecor): the role objects go */
        wlc_send(c, s.surface, WLQ_wl_surface_attach, 0L, 0L, 0L);
        wlc_send(c, s.surface, WLQ_wl_surface_commit);
        wlc_send(c, s.toplevel, WLQ_xdg_toplevel_destroy);
        wlc_send(c, s.xdg, WLQ_xdg_surface_destroy);
        sync = wlc_id(c, WLI_wl_callback, 1);
        wlc_send(c, 1, WLQ_wl_display_sync, sync);
        WLCHECK(wl_done(c, sync) && c->errors == 0, "sdl: a window hidden by destroying its toplevel and xdg_surface is no error (%s)", c->error_text);
        wlc_send(c, s.surface, WLQ_wl_surface_attach, 0L, 0L, 0L);
        wlc_send(c, s.surface, WLQ_wl_surface_commit);
        s.xdg = wlc_id(c, WLI_xdg_surface, 6);
        wlc_send(c, s.wm_base, WLQ_xdg_wm_base_get_xdg_surface, s.xdg, s.surface);
        s.toplevel = wlc_id(c, WLI_xdg_toplevel, 6);
        wlc_send(c, s.xdg, WLQ_xdg_surface_get_toplevel, s.toplevel);
        wlc_send(c, s.toplevel, WLQ_xdg_toplevel_set_app_id, "hl2_linux");
        wlc_forget(c);
        wlc_send(c, s.surface, WLQ_wl_surface_commit);
        wlc_send(c, s.toplevel, WLQ_xdg_toplevel_set_title, "HALF-LIFE 2 - OpenGL");
        e = wlc_take(c, WLI_xdg_surface, WLE_xdg_surface_configure);
        WLCHECK(e && e->u[0] != first_serial, "sdl: the second toplevel on the same surface is configured anew (serial %lu after %lu)",
                e ? e->u[0] : 0, first_serial);
        if (e)
                wlc_send(c, s.xdg, WLQ_xdg_surface_ack_configure, (long)e->u[0]);
        wlc_send(c, s.toplevel, WLQ_xdg_toplevel_set_min_size, 640L, 480L);
        wlc_send(c, s.toplevel, WLQ_xdg_toplevel_set_max_size, 640L, 480L);
        wlc_send(c, s.xdg, WLQ_xdg_surface_set_window_geometry, 0L, 0L, 640L, 480L);
        sync = wlc_id(c, WLI_wl_callback, 1);
        wlc_send(c, 1, WLQ_wl_display_sync, sync);
        WLCHECK(wl_done(c, sync) && c->errors == 0, "sdl: the second show's roundtrip is answered (%s)", c->error_text);
        /* SDL's damage callback asked after the last commit, then Mesa's first swap: the buffer, its damage, the commit */
        damage_frame = wlc_id(c, WLI_wl_callback, 1);
        wlc_send(c, s.surface, WLQ_wl_surface_damage_buffer, 0L, 0L, 640L, 480L);
        wlc_send(c, s.surface, WLQ_wl_surface_frame, damage_frame);
        WLCHECK(!wl_done(c, damage_frame), "sdl: a frame callback asked and not committed waits");
        buffer = wl_buffer_of(&s, 640, 480, 0xff4466aa, WLN_wl_shm_format_xrgb8888);
        gl_frame = wlc_id(c, WLI_wl_callback, 1);
        wlc_send(c, s.surface, WLQ_wl_surface_frame, gl_frame);
        wlc_send(c, s.surface, WLQ_wl_surface_attach, buffer, 0L, 0L);
        wlc_send(c, s.surface, WLQ_wl_surface_damage_buffer, 0L, 0L, 640L, 480L);
        wlc_send(c, s.surface, WLQ_wl_surface_commit);
        {
                int shown = -1, open = 0;

                for (int row = 0; row < FAKE_ROWS; row++)
                        if (fake.row[row].used)
                        {
                                open++;
                                if (fake.row[row].picture_w == 640 && fake.row[row].pixels[5] == 0xff4466aa)
                                        shown = row;
                        }
                WLCHECK(c->errors == 0 && open == 1 && shown >= 0, "sdl: the first swap's picture is the one window's (%d open, %s)", open,
                        c->error_text);
        }
        wayland_frame();
        wlc_pump(c);
        WLCHECK(wl_done(c, damage_frame) && wl_done(c, gl_frame), "sdl: both frame callbacks are answered at the frame drawn");
        wlc_end(c);
        leak_check("sdl show");
        wayland_stop();
}

/*
        A surface that commits more frame callbacks than it holds ready, with no frame drawn between them: each callback is
        answered by the frame that comes, none is let go without its done (the client waits for it, and a video that waits
        for its frame never starts again).
*/
static void test_frame_overflow(void)
{
        struct session s;
        struct wlc *c;

        wl_reset();
        wayland_start(&fake_host);
        session_open(&s);
        session_window(&s, 0xff223344);
        c = s.c;
        wlc_forget(c);
        for (int at = 0; at < 4 * WAYLAND_CALLBACKS + 1; at++)
        {
                wlc_send(c, s.surface, WLQ_wl_surface_frame, wlc_id(c, WLI_wl_callback, 1));
                wlc_send(c, s.surface, WLQ_wl_surface_commit);
        }
        wlc_pump(c);
        WLCHECK(wlc_count(c, WLI_wl_callback, WLE_wl_callback_done) == 0,
                "past the callbacks a surface holds in itself none is answered before its frame (%ld were)", wlc_count(c, WLI_wl_callback, WLE_wl_callback_done));
        wayland_frame();
        wlc_pump(c);
        WLCHECK(wlc_count(c, WLI_wl_callback, WLE_wl_callback_done) == 4 * WAYLAND_CALLBACKS + 1,
                "every frame callback is answered by the frame: %ld of %d", wlc_count(c, WLI_wl_callback, WLE_wl_callback_done), 4 * WAYLAND_CALLBACKS + 1);
        wlc_end(c);
        leak_check("frame overflow");
        wayland_stop();
}

/*
        wp_presentation: the feedback a commit takes is answered by the frame it is drawn in, with the flip's time, the period
        and the count; a timer's frame that drew nothing answers none. A surface past its limit, a surface with no window and
        a surface destroyed each discard, as the protocol says, and every object is let go by its event.
*/
static long event_at(struct wlc *c, long iface, long opcode, long from);

/*
        wl_touch: a seat with a touch screen gives touches; a finger down on a window is sent down to the surface under it, moves
        are motions in that surface's own coordinates (its subsurface's place taken away) and go to it whatever is under them,
        its lift is up, each followed by a frame, in that order; a cancel reaches a client once. A seat without touch refuses
        get_touch.
*/
static void test_touch(void)
{
        struct session s;
        struct wlc *c;
        unsigned long seat, touch;
        long a, b, d, count;

        /* the seat has no touch screen: the request is refused */
        wl_reset();
        fake.caps = WLN_wl_seat_capability_pointer | WLN_wl_seat_capability_keyboard;
        wayland_start(&fake_host);
        session_open(&s);
        c = s.c;
        seat = wl_global(c, s.registry, WLI_wl_seat, 9);
        wlc_forget(c);
        wlc_send(c, seat, WLQ_wl_seat_get_touch, wlc_id(c, WLI_wl_touch, 9));
        WLCHECK(c->errors == 1, "a seat with no touch refuses get_touch");
        wlc_end(c);
        wayland_stop();

        /* a seat with touch: a finger's down, motion, and up, in order, and a cancel to the client once */
        wl_reset();
        fake.caps = WLN_wl_seat_capability_pointer | WLN_wl_seat_capability_keyboard | WLN_wl_seat_capability_touch;
        wayland_start(&fake_host);
        session_open(&s);
        session_window(&s, 0xff223344);
        c = s.c;
        seat = wl_global(c, s.registry, WLI_wl_seat, 9);
        touch = wlc_id(c, WLI_wl_touch, 9);
        wlc_forget(c);
        wlc_send(c, seat, WLQ_wl_seat_get_touch, touch);
        WLCHECK(c->errors == 0, "a seat with touch gives a touch object");
        WLCHECK(wayland_row_takes_touch(0) == 1, "a client with a touch takes the fingers on its window, so the pointer's are not sent to it");
        wlc_forget(c);

        wayland_touch_down(0, 3, 10, 20);
        wlc_pump(c);
        a = event_at(c, WLI_wl_touch, WLE_wl_touch_down, 0);
        WLCHECK(a >= 0 && c->ev[a].u[2] == s.surface && c->ev[a].u[3] == 3 && c->ev[a].u[4] == 10 * 256 && c->ev[a].u[5] == 20 * 256,
                "a finger down is a down on the surface under it, with its id and its place in the surface, in fixed point");
        WLCHECK(wayland_touch_row(3) == 0, "a finger down is kept on its window until it is lifted");
        wayland_touch_motion(3, 20, 12);
        wlc_pump(c);
        b = event_at(c, WLI_wl_touch, WLE_wl_touch_motion, 0);
        WLCHECK(b > a && c->ev[b].u[1] == 3 && c->ev[b].u[2] == 20 * 256 && c->ev[b].u[3] == 12 * 256,
                "a move is a motion after the down, to the same surface's coordinates");
        wayland_touch_motion(3, 20, 12);
        wlc_pump(c);
        WLCHECK(wlc_count(c, WLI_wl_touch, WLE_wl_touch_motion) == 1, "a finger that did not move sends nothing");
        wayland_touch_up(3);
        wlc_pump(c);
        d = event_at(c, WLI_wl_touch, WLE_wl_touch_up, 0);
        WLCHECK(d > b && c->ev[d].u[2] == 3, "a lift is an up after the motion, with its id");
        WLCHECK(wlc_count(c, WLI_wl_touch, WLE_wl_touch_frame) == 3, "each event is followed by a frame");
        WLCHECK(wayland_touch_row(3) == -1, "a finger that is up is down no more");
        wayland_touch_up(3);
        wlc_pump(c);
        WLCHECK(wlc_count(c, WLI_wl_touch, WLE_wl_touch_up) == 1, "a lift of a finger that is not down sends nothing");

        /* two fingers: the cancel reaches the client once, and none of them is down after it */
        wlc_forget(c);
        wayland_touch_down(0, 4, 5, 5);
        wayland_touch_down(0, 6, 6, 6);
        wayland_touch_cancel();
        wlc_pump(c);
        WLCHECK(wlc_count(c, WLI_wl_touch, WLE_wl_touch_down) == 2, "two fingers go down");
        WLCHECK(wlc_count(c, WLI_wl_touch, WLE_wl_touch_cancel) == 1, "a cancel reaches the client once, not once a finger");
        WLCHECK(wayland_touch_row(4) == -1 && wayland_touch_row(6) == -1, "after a cancel no finger is down");

        /* a surface destroyed while a finger is down: its moves go nowhere, and its lift is still sent, so the client's sequence balances */
        wlc_forget(c);
        wayland_touch_down(0, 8, 9, 9);
        wlc_send(c, s.surface, WLQ_wl_surface_destroy);
        wlc_pump(c);
        wayland_touch_motion(8, 10, 10);
        wayland_touch_up(8);
        wlc_pump(c);
        WLCHECK(c->errors == 0, "a surface gone under a finger is no protocol error");
        count = wlc_count(c, WLI_wl_touch, WLE_wl_touch_motion);
        WLCHECK(count == 0, "and a move of it goes nowhere");
        WLCHECK(wlc_count(c, WLI_wl_touch, WLE_wl_touch_up) == 1, "its lift is still sent");
        wlc_end(c);
        leak_check("touch");
        wayland_stop();

        /* a touch press's serial also authorizes an interactive xdg_toplevel.move */
        {
                struct wl_event_seen *e;

                wl_reset();
                fake.caps = WLN_wl_seat_capability_touch | WLN_wl_seat_capability_pointer;
                wayland_start(&fake_host);
                session_open(&s);
                session_window(&s, 0xff223344);
                c = s.c;
                touch = wlc_id(c, WLI_wl_touch, 9);
                wlc_send(c, s.seat, WLQ_wl_seat_get_touch, touch);
                wlc_pump(c);
                wlc_forget(c);
                wayland_touch_down(0, 1, 10, 10);
                wlc_pump(c);
                e = wlc_take(c, WLI_wl_touch, WLE_wl_touch_down);
                WLCHECK(e && e->n >= 1, "touch down delivers a serial");
                if (e)
                {
                        unsigned long serial = e->u[0];

                        wlc_send(c, s.toplevel, WLQ_xdg_toplevel_move, s.seat, (long)serial);
                        WLCHECK(fake.row[0].did_count == 1 && fake.row[0].did[0] == WAYLAND_DO_MOVE,
                                "a move that names a touch press's serial is asked of the window");
                }
                wlc_end(c);
                leak_check("touch move");
                wayland_stop();
        }
}

/* A finger's notch: the kit's UI_NOTCH (the theme in ui/ui.c, not included here), in pixels at 100 percent, written as the number the shim is handed. */
#define WAYLAND_FINGER_NOTCH 40

/*
        wl_pointer.axis_stop: a finger scroll ends with axis_stop on both axes, once; a wheel's scroll has none; a version 9
        pointer says each axis' direction, identical, and the finger source comes before the axis.
*/
static void test_axis_stop(void)
{
        struct session s;
        struct wlc *c;
        unsigned long seat, pointer;
        long a, b;

        wl_reset();
        wayland_start(&fake_host);
        session_open(&s);
        session_window(&s, 0xff223344);
        c = s.c;
        seat = wl_global(c, s.registry, WLI_wl_seat, 9);
        pointer = wlc_id(c, WLI_wl_pointer, 9);
        wlc_forget(c);
        wlc_send(c, seat, WLQ_wl_seat_get_pointer, pointer);
        wayland_pointer_to(0, 10, 10);
        wlc_pump(c);
        wlc_forget(c);

        wayland_pointer_axis(0, 0, 120, WAYLAND_FINGER_NOTCH);
        wlc_pump(c);
        a = event_at(c, WLI_wl_pointer, WLE_wl_pointer_axis_source, 0);
        WLCHECK(a >= 0 && c->ev[a].u[0] == WLN_wl_pointer_axis_source_finger, "a finger scroll says its source is the finger");
        b = event_at(c, WLI_wl_pointer, WLE_wl_pointer_axis, 0);
        WLCHECK(b >= 0 && (long)(int)c->ev[b].u[2] == WAYLAND_FINGER_NOTCH * 256, "a finger's notch is 40 pixels (the kit's UI_NOTCH) to the client, one for one with the kit's turn (a wheel's notch is 15 units)");
        WLCHECK(wlc_count(c, WLI_wl_pointer, WLE_wl_pointer_axis_relative_direction) == 1,
                "a version 9 pointer says the direction of the axis, identical");
        wlc_forget(c);
        wayland_pointer_axis_stop(0);
        wlc_pump(c);
        WLCHECK(wlc_count(c, WLI_wl_pointer, WLE_wl_pointer_axis_stop) == 2, "the end of a finger scroll is axis_stop on both axes");
        b = event_at(c, WLI_wl_pointer, WLE_wl_pointer_axis_stop, 0);
        WLCHECK(b >= 0 && c->ev[b].u[1] == WLN_wl_pointer_axis_vertical_scroll, "the vertical axis first");

        wlc_forget(c);
        wayland_pointer_axis_stop(0);
        wlc_pump(c);
        WLCHECK(wlc_count(c, WLI_wl_pointer, WLE_wl_pointer_axis_stop) == 0, "a stop with no finger scroll before it goes to no one");

        wlc_forget(c);
        wayland_pointer_axis(0, 0, 120, 0);
        wayland_pointer_axis_stop(0);
        wlc_pump(c);
        WLCHECK(wlc_count(c, WLI_wl_pointer, WLE_wl_pointer_axis_stop) == 0, "a wheel's scroll has no stop");
        WLCHECK(wayland_row_takes_touch(0) == 0, "a client with no touch takes no finger, and keeps the pointer's events for one");
        WLCHECK(c->errors == 0, "none of it is a protocol error");
        wlc_end(c);
        leak_check("axis stop");
        wayland_stop();
}

/*
        The hints a window is given, read by the compositor: an xdg_dialog's modal state (set, unset, and gone with the dialog),
        a content type (set at once, none on destroy), an alpha multiplier (shown from the commit after it is set, opaque again on
        destroy), and a bell ring that is taken. Each of the four is advertised at version 1.
*/
static void test_hints(void)
{
        struct session s;
        struct wlc *c;
        unsigned long dialogs, bells, contents, alphas, dialog, content, alpha;

        wl_reset();
        wayland_start(&fake_host);
        session_open(&s);
        session_window(&s, 0xff223344);
        c = s.c;
        dialogs = wl_global(c, s.registry, WLI_xdg_wm_dialog_v1, 1);
        bells = wl_global(c, s.registry, WLI_xdg_system_bell_v1, 1);
        contents = wl_global(c, s.registry, WLI_wp_content_type_manager_v1, 1);
        alphas = wl_global(c, s.registry, WLI_wp_alpha_modifier_v1, 1);
        WLCHECK(dialogs && bells && contents && alphas, "the dialog, bell, content type and alpha modifier are offered");
        wlc_forget(c);

        dialog = wlc_id(c, WLI_xdg_dialog_v1, 1);
        wlc_send(c, dialogs, WLQ_xdg_wm_dialog_v1_get_xdg_dialog, dialog, s.toplevel);
        wlc_send(c, dialog, WLQ_xdg_dialog_v1_set_modal);
        wlc_pump(c);
        WLCHECK(c->errors == 0 && wayland_row_hint(0, WAYLAND_HINT_MODAL) == 1, "a dialog set modal is modal for its window");
        wlc_send(c, dialog, WLQ_xdg_dialog_v1_unset_modal);
        WLCHECK(wayland_row_hint(0, WAYLAND_HINT_MODAL) == 0, "and not when unset");
        wlc_send(c, dialog, WLQ_xdg_dialog_v1_set_modal);
        wlc_send(c, dialog, WLQ_xdg_dialog_v1_destroy);
        WLCHECK(wayland_row_hint(0, WAYLAND_HINT_MODAL) == 0, "and not once the dialog is destroyed");

        wlc_send(c, bells, WLQ_xdg_system_bell_v1_ring, s.surface);
        WLCHECK(c->errors == 0, "a bell ring is taken");

        content = wlc_id(c, WLI_wp_content_type_v1, 1);
        wlc_send(c, contents, WLQ_wp_content_type_manager_v1_get_surface_content_type, content, s.surface);
        WLCHECK(wayland_row_hint(0, WAYLAND_HINT_CONTENT) == WLN_wp_content_type_v1_type_none, "a window has no content type to begin with");
        wlc_send(c, content, WLQ_wp_content_type_v1_set_content_type, (unsigned long)WLN_wp_content_type_v1_type_game);
        WLCHECK(c->errors == 0 && wayland_row_hint(0, WAYLAND_HINT_CONTENT) == WLN_wp_content_type_v1_type_game, "a content type is set at once");
        wlc_send(c, content, WLQ_wp_content_type_v1_destroy);
        WLCHECK(wayland_row_hint(0, WAYLAND_HINT_CONTENT) == WLN_wp_content_type_v1_type_none, "and none once destroyed");

        alpha = wlc_id(c, WLI_wp_alpha_modifier_surface_v1, 1);
        wlc_send(c, alphas, WLQ_wp_alpha_modifier_v1_get_surface, alpha, s.surface);
        wlc_send(c, alpha, WLQ_wp_alpha_modifier_surface_v1_set_multiplier, 0x80000000ul);
        WLCHECK(wayland_row_hint(0, WAYLAND_HINT_ALPHA) == 0xffffffffu, "an alpha multiplier is shown from the commit after it is set");
        wlc_send(c, s.surface, WLQ_wl_surface_commit);
        WLCHECK(wayland_row_hint(0, WAYLAND_HINT_ALPHA) == 0x80000000u, "and it is shown at the commit");
        wlc_send(c, alpha, WLQ_wp_alpha_modifier_surface_v1_destroy);
        wlc_send(c, s.surface, WLQ_wl_surface_commit);
        WLCHECK(wayland_row_hint(0, WAYLAND_HINT_ALPHA) == 0xffffffffu && c->errors == 0, "opaque again once the modifier is destroyed");
        WLCHECK(wayland_row_hint(7, WAYLAND_HINT_ALPHA) == 0xffffffffu && wayland_row_hint(-1, WAYLAND_HINT_MODAL) == 0, "a row with no window is opaque and not modal");

        wlc_end(c);
        leak_check("hints");
        wayland_stop();
}

/*
        The inhibitors and the tearing hint, kept for the compositor: an idle inhibitor is counted while it lives, and let go by its
        destroy or by its client's going; a keyboard shortcuts inhibitor is read per window; a tearing hint is shown from the commit
        after it is set (asynchronous, or back to vsync on destroy).
*/
/*
        xdg-activation: a token made by a client that named a serial of its own input and a surface, given out at its commit, focuses
        its window when it is used (once), with the serial still recent; a token with no serial, or one that is ten seconds old,
        only marks the window as wanting attention; an unknown token is nothing.
*/
/*
        wp_fifo and wp_commit_timing: a wait_barrier applies to the commit that follows it only, and a set_timestamp gives the time
        of that commit (nanoseconds, seconds in two words); a second of nanoseconds or more is a protocol error.
*/
static void test_pacing(void)
{
        struct session s;
        struct wlc *c;
        unsigned long fifos, timers, fifo, timer;

        wl_reset();
        wayland_start(&fake_host);
        session_open(&s);
        session_window(&s, 0xff223344);
        c = s.c;
        fifos = wl_global(c, s.registry, WLI_wp_fifo_manager_v1, 1);
        timers = wl_global(c, s.registry, WLI_wp_commit_timing_manager_v1, 1);
        WLCHECK(fifos && timers, "the fifo and commit timing managers are offered");
        wlc_forget(c);

        fifo = wlc_id(c, WLI_wp_fifo_v1, 1);
        wlc_send(c, fifos, WLQ_wp_fifo_manager_v1_get_fifo, fifo, s.surface);
        wlc_send(c, fifo, WLQ_wp_fifo_v1_wait_barrier);
        WLCHECK(wayland_row_hint(0, WAYLAND_HINT_FIFO) == 0, "a barrier applies from the commit after it");
        wlc_send(c, s.surface, WLQ_wl_surface_commit);
        WLCHECK(c->errors == 0 && wayland_row_hint(0, WAYLAND_HINT_FIFO) == 1, "a commit that waits for the barrier is kept as waiting");
        wlc_send(c, s.surface, WLQ_wl_surface_commit);
        WLCHECK(wayland_row_hint(0, WAYLAND_HINT_FIFO) == 0, "and the next commit does not wait unless it is told to");

        timer = wlc_id(c, WLI_wp_commit_timer_v1, 1);
        wlc_send(c, timers, WLQ_wp_commit_timing_manager_v1_get_timer, timer, s.surface);
        wlc_send(c, timer, WLQ_wp_commit_timer_v1_set_timestamp, 0ul, 5ul, 7ul);
        WLCHECK(wayland_row_hint(0, WAYLAND_HINT_TIMESTAMP) == 0, "a timestamp is for the commit after it is set");
        wlc_send(c, s.surface, WLQ_wl_surface_commit);
        WLCHECK(c->errors == 0 && wayland_stalled(c->slot) && wayland_row_hint(0, WAYLAND_HINT_TIMESTAMP) == 0, "a commit for a later time is held until that time");
        fake.now = 5001;
        WLCHECK(wayland_resume(c->slot) == 0 && c->errors == 0 && wayland_row_hint(0, WAYLAND_HINT_TIMESTAMP) == 5000000007ul,
                "and is made at its time, which is the commit's, in nanoseconds");
        wlc_send(c, s.surface, WLQ_wl_surface_commit);
        WLCHECK(wayland_row_hint(0, WAYLAND_HINT_TIMESTAMP) == 0, "and it is for that commit only");
        // With the vblank the next latch shows at known (wayland_target), a commit for that vblank is made now, to be shown at it, and one
        // for the vblank after it waits until a refresh before that.
        wayland_target(6000000000ul, 6957000ul);
        wlc_send(c, timer, WLQ_wp_commit_timer_v1_set_timestamp, 0ul, 6ul, 0ul);
        wlc_send(c, s.surface, WLQ_wl_surface_commit);
        WLCHECK(c->errors == 0 && !wayland_stalled(c->slot) && wayland_row_hint(0, WAYLAND_HINT_TIMESTAMP) == 6000000000ul,
                "a commit for the vblank the next latch shows at is made at once, though the clock has not reached it");
        wlc_send(c, timer, WLQ_wp_commit_timer_v1_set_timestamp, 0ul, 6ul, 500000ul);
        wlc_send(c, s.surface, WLQ_wl_surface_commit);
        WLCHECK(!wayland_stalled(c->slot), "and so is one for a time within an eighth of a refresh after it");
        wlc_send(c, timer, WLQ_wp_commit_timer_v1_set_timestamp, 0ul, 6ul, 6957000ul);
        wlc_send(c, s.surface, WLQ_wl_surface_commit);
        {
                long fd;

                WLCHECK(wayland_stalled(c->slot) && wayland_stall_due(c->slot, &fd) == 6000, "one for the vblank after waits, until that next latch (%ld)",
                        wayland_stall_due(c->slot, &fd));
        }
        wayland_target(6006957000ul, 6957000ul);
        WLCHECK(wayland_resume(c->slot) == 0 && !wayland_stalled(c->slot) && wayland_row_hint(0, WAYLAND_HINT_TIMESTAMP) == 6006957000ul,
                "and is made when the next latch shows at its vblank");
        wayland_target(0ul, 0ul);
        wlc_send(c, timer, WLQ_wp_commit_timer_v1_set_timestamp, 0ul, 5ul, 1000000000ul);
        WLCHECK(c->errors == 1 && c->error_code == WLN_wp_commit_timer_v1_error_invalid_timestamp,
                "a second of nanoseconds or more is invalid_timestamp");
        wlc_end(c);
        leak_check("pacing");
        wayland_stop();
}

/*
        wp_fifo's barrier: a commit that sets one is made, and the next frame drawn clears it; a commit that waits for it is held (its
        client's stream waits) until that frame, and is made then. A surface with no window holds nothing, since no frame would come for it.
*/
/*
        zwp_keyboard_shortcuts_inhibitor_v1: active while its window has the keys (active is sent, inactive when they go), so the compositor's
        chords are the window's while it is. Super with Escape, held a second, is the one chord kept: it gives the keys back and the inhibitor
        is inactive until the keys come to its window again. Escape alone, Tab and the rest are the window's.
*/
static void test_shortcuts(void)
{
        struct session s;
        struct wlc *c;
        unsigned long shortcuts, inhibitor;
        long active, inactive;

        wl_reset();
        fake.clock_still = 1;
        fake.now = 1000;
        wayland_start(&fake_host);
        session_open(&s);
        session_window(&s, 0xff223344);
        c = s.c;
        shortcuts = wl_global(c, s.registry, WLI_zwp_keyboard_shortcuts_inhibit_manager_v1, 1);
        wlc_forget(c);
        inhibitor = wlc_id(c, WLI_zwp_keyboard_shortcuts_inhibitor_v1, 1);
        wlc_send(c, shortcuts, WLQ_zwp_keyboard_shortcuts_inhibit_manager_v1_inhibit_shortcuts, inhibitor, s.surface, s.seat);
        WLCHECK(c->errors == 0 && wayland_row_shortcuts(0) == 0, "an inhibitor is not active before its window has the keys");
        wayland_keyboard_to(0);
        wlc_pump(c);
        active = wlc_count(c, WLI_zwp_keyboard_shortcuts_inhibitor_v1, WLE_zwp_keyboard_shortcuts_inhibitor_v1_active);
        WLCHECK(active == 1 && wayland_row_shortcuts(0) == 1, "it is active, and says so, once its window has the keys");
        WLCHECK(wayland_shortcuts_key(0, 15, 1, 0) == 1, "while active, Tab is the window's, and so is any key");
        WLCHECK(wayland_shortcuts_key(0, 1, 1, 0) == 1, "Escape without Super is the window's");
        WLCHECK(wayland_shortcuts_key(0, 1, 1, 1) == 2, "Super with Escape is the compositor's kept chord, and the window does not get it");
        fake.now += 500;
        WLCHECK(wayland_shortcuts_key(0, 1, 0, 1) == 2 && wayland_row_shortcuts(0) == 1, "let go before a second, it does not give the keys back");
        fake.now += 500;
        wlc_pump(c);
        WLCHECK(wlc_count(c, WLI_zwp_keyboard_shortcuts_inhibitor_v1, WLE_zwp_keyboard_shortcuts_inhibitor_v1_inactive) == 0,
                "and nothing is said about it");
        WLCHECK(wayland_shortcuts_key(0, 1, 1, 1) == 2, "held again for a second");
        fake.now += 1000;
        WLCHECK(wayland_shortcuts_key(0, 1, 0, 1) == 2 && wayland_row_shortcuts(0) == 0, "held a second, the kept chord gives the keys back");
        wlc_pump(c);
        inactive = wlc_count(c, WLI_zwp_keyboard_shortcuts_inhibitor_v1, WLE_zwp_keyboard_shortcuts_inhibitor_v1_inactive);
        WLCHECK(inactive == 1, "and the inhibitor is told inactive");
        WLCHECK(wayland_shortcuts_key(0, 15, 1, 0) == 0, "with the inhibitor inactive, the compositor's chords run again");
        wayland_keyboard_to(-1);
        wlc_pump(c);
        WLCHECK(wlc_count(c, WLI_zwp_keyboard_shortcuts_inhibitor_v1, WLE_zwp_keyboard_shortcuts_inhibitor_v1_inactive) == 1,
                "losing the keys after an escape does not say inactive twice");
        wayland_keyboard_to(0);
        wlc_pump(c);
        WLCHECK(wlc_count(c, WLI_zwp_keyboard_shortcuts_inhibitor_v1, WLE_zwp_keyboard_shortcuts_inhibitor_v1_active) == 2 && wayland_row_shortcuts(0) == 1,
                "back on the keys, it is active again");
        wayland_keyboard_to(-1);
        wlc_pump(c);
        WLCHECK(wlc_count(c, WLI_zwp_keyboard_shortcuts_inhibitor_v1, WLE_zwp_keyboard_shortcuts_inhibitor_v1_inactive) == 2 && wayland_row_shortcuts(0) == 0,
                "losing the keys says inactive once it is active again");
        WLCHECK(wayland_shortcuts_key(0, 15, 1, 0) == 0, "and no window has the keys, so the chords run");
        wlc_send(c, inhibitor, WLQ_zwp_keyboard_shortcuts_inhibitor_v1_destroy);
        WLCHECK(c->errors == 0, "the inhibitor is destroyed");
        wayland_keyboard_to(0);
        wlc_pump(c);
        wlc_forget(c);
        inhibitor = wlc_id(c, WLI_zwp_keyboard_shortcuts_inhibitor_v1, 1);
        wlc_send(c, shortcuts, WLQ_zwp_keyboard_shortcuts_inhibit_manager_v1_inhibit_shortcuts, inhibitor, s.surface, s.seat);
        wlc_pump(c);
        WLCHECK(c->errors == 0 && wayland_row_shortcuts(0) == 1 &&
                    wlc_count(c, WLI_zwp_keyboard_shortcuts_inhibitor_v1, WLE_zwp_keyboard_shortcuts_inhibitor_v1_active) == 1,
                "an inhibitor created while its window already has the keys is active at once");
        wlc_send(c, inhibitor, WLQ_zwp_keyboard_shortcuts_inhibitor_v1_destroy);
        wlc_end(c);
        wayland_stop();
}

static void test_barrier(void)
{
        struct session s;
        struct wlc *c;
        unsigned long fifos, fifo, plain, plain_fifo;

        wl_reset();
        wayland_start(&fake_host);
        session_open(&s);
        session_window(&s, 0xff223344);
        c = s.c;
        fifos = wl_global(c, s.registry, WLI_wp_fifo_manager_v1, 1);
        wlc_forget(c);
        fifo = wlc_id(c, WLI_wp_fifo_v1, 1);
        wlc_send(c, fifos, WLQ_wp_fifo_manager_v1_get_fifo, fifo, s.surface);
        wlc_send(c, fifo, WLQ_wp_fifo_v1_set_barrier);
        wlc_send(c, s.surface, WLQ_wl_surface_commit);
        WLCHECK(c->errors == 0 && !wayland_stalled(c->slot) && !wayland_frame_wanted(), "a commit that sets a barrier is made, and nothing waits");
        wlc_send(c, fifo, WLQ_wp_fifo_v1_wait_barrier);
        wlc_send(c, s.surface, WLQ_wl_surface_commit);
        WLCHECK(c->errors == 0 && wayland_stalled(c->slot) && wayland_frame_wanted(), "a commit that waits for the barrier is held, and frames are wanted");
        wayland_frame();
        WLCHECK(wayland_resume(c->slot) == 0 && c->errors == 0 && !wayland_stalled(c->slot) && wayland_row_hint(0, WAYLAND_HINT_FIFO) == 1,
                "the next frame clears the barrier, and the held commit is made");
        WLCHECK(!wayland_frame_wanted(), "and no frame is wanted after it");

        // A barrier set while a latched frame is composed is the next frame's: that frame's end does not clear it, the next latch does.
        wayland_latch();
        wlc_send(c, fifo, WLQ_wp_fifo_v1_set_barrier);
        wlc_send(c, s.surface, WLQ_wl_surface_commit);
        wlc_send(c, fifo, WLQ_wp_fifo_v1_wait_barrier);
        wlc_send(c, s.surface, WLQ_wl_surface_commit);
        WLCHECK(c->errors == 0 && wayland_stalled(c->slot), "a barrier set while a frame is in flight holds the commit that waits for it");
        wayland_frame();
        WLCHECK(wayland_resume(c->slot) == 1 && wayland_stalled(c->slot), "the end of the frame that did not latch it does not clear it");
        wayland_latch();
        WLCHECK(wayland_resume(c->slot) == 0 && !wayland_stalled(c->slot), "the next latch does, and the held commit is made");
        wayland_frame();

        plain = wlc_id(c, WLI_wl_surface, 6);
        wlc_send(c, s.compositor, WLQ_wl_compositor_create_surface, plain);
        plain_fifo = wlc_id(c, WLI_wp_fifo_v1, 1);
        wlc_send(c, fifos, WLQ_wp_fifo_manager_v1_get_fifo, plain_fifo, plain);
        wlc_send(c, plain_fifo, WLQ_wp_fifo_v1_set_barrier);
        wlc_send(c, plain, WLQ_wl_surface_commit);
        wlc_send(c, plain_fifo, WLQ_wp_fifo_v1_wait_barrier);
        wlc_send(c, plain, WLQ_wl_surface_commit);
        WLCHECK(c->errors == 0 && !wayland_stalled(c->slot), "a surface with no window holds nothing");
        wlc_end(c);
        leak_check("barrier");
        wayland_stop();
}

static void test_activation(void)
{
        struct session s;
        struct wlc *c;
        unsigned long seat, pointer, activation, token, plain;
        long a, serial, before;
        char given[64], unvouched[64];

        wl_reset();
        wayland_start(&fake_host);
        session_open(&s);
        session_window(&s, 0xff223344);
        c = s.c;
        activation = wl_global(c, s.registry, WLI_xdg_activation_v1, 1);
        seat = wl_global(c, s.registry, WLI_wl_seat, 9);
        pointer = wlc_id(c, WLI_wl_pointer, 9);
        wlc_forget(c);
        wlc_send(c, seat, WLQ_wl_seat_get_pointer, pointer);
        wayland_pointer_to(0, 10, 10);
        wlc_pump(c);
        a = event_at(c, WLI_wl_pointer, WLE_wl_pointer_enter, 0);
        serial = a >= 0 ? (long)c->ev[a].u[0] : 0;
        WLCHECK(activation && serial != 0, "an xdg-activation is offered, and an enter gives a serial the client may name");

        wlc_forget(c);
        token = wlc_id(c, WLI_xdg_activation_token_v1, 1);
        wlc_send(c, activation, WLQ_xdg_activation_v1_get_activation_token, token);
        wlc_send(c, token, WLQ_xdg_activation_token_v1_set_serial, (unsigned long)serial, seat);
        wlc_send(c, token, WLQ_xdg_activation_token_v1_commit);
        wlc_pump(c);
        a = event_at(c, WLI_xdg_activation_token_v1, WLE_xdg_activation_token_v1_done, 0);
        WLCHECK(a >= 0 && strlen(c->ev[a].text[0]) == 32, "a committed token is given out as 32 digits");
        if (a >= 0)
                memcpy(given, c->ev[a].text[0], sizeof given);
        else
                given[0] = 0;

        /* a token with the person's serial focuses its window, once */
        before = fake.row[0].did_count;
        wlc_forget(c);
        wlc_send(c, activation, WLQ_xdg_activation_v1_activate, given, s.surface);
        WLCHECK(fake.row[0].did_count == before + 1 && fake.row[0].did[before] == WAYLAND_DO_FOCUS,
                "a token whose serial is still the client's recent input focuses its window");
        WLCHECK(wayland_row_hint(0, WAYLAND_HINT_ATTENTION) == 0, "and that window does not want attention");
        before = fake.row[0].did_count;
        wlc_send(c, activation, WLQ_xdg_activation_v1_activate, given, s.surface);
        WLCHECK(fake.row[0].did_count == before, "a token is used once");

        /* a token with no serial of the person's marks its window as wanting attention, and does not focus it */
        plain = wlc_id(c, WLI_xdg_activation_token_v1, 1);
        wlc_send(c, activation, WLQ_xdg_activation_v1_get_activation_token, plain);
        wlc_send(c, plain, WLQ_xdg_activation_token_v1_commit);
        wlc_pump(c);
        a = event_at(c, WLI_xdg_activation_token_v1, WLE_xdg_activation_token_v1_done, 0);
        if (a >= 0)
                memcpy(unvouched, c->ev[a].text[0], sizeof unvouched);
        else
                unvouched[0] = 0;
        before = fake.row[0].did_count;
        wlc_send(c, activation, WLQ_xdg_activation_v1_activate, unvouched, s.surface);
        WLCHECK(fake.row[0].did_count == before, "a token with no serial does not focus the window");
        WLCHECK(wayland_row_hint(0, WAYLAND_HINT_ATTENTION) == 1, "it marks the window as wanting attention");

        /* a token gone old is no focus, even with a serial */
        wlc_forget(c);
        token = wlc_id(c, WLI_xdg_activation_token_v1, 1);
        wlc_send(c, activation, WLQ_xdg_activation_v1_get_activation_token, token);
        wlc_send(c, token, WLQ_xdg_activation_token_v1_set_serial, (unsigned long)serial, seat);
        wlc_send(c, token, WLQ_xdg_activation_token_v1_commit);
        wlc_pump(c);
        a = event_at(c, WLI_xdg_activation_token_v1, WLE_xdg_activation_token_v1_done, 0);
        if (a >= 0)
                memcpy(given, c->ev[a].text[0], sizeof given);
        else
                given[0] = 0;
        fake.now += 20000;
        before = fake.row[0].did_count;
        wlc_send(c, activation, WLQ_xdg_activation_v1_activate, given, s.surface);
        WLCHECK(fake.row[0].did_count == before, "a token ten seconds old is no focus");
        WLCHECK(c->errors == 0, "and none of it is a protocol error");

        /* a text that is no token of the compositor's is nothing */
        before = fake.row[0].did_count;
        wlc_send(c, activation, WLQ_xdg_activation_v1_activate, "0123456789abcdef0123456789abcdef", s.surface);
        WLCHECK(fake.row[0].did_count == before && c->errors == 0, "an unknown token is nothing, and no error");

        /* tokens taken past the sixteen there were, and the four one client could hold, uncommitted: each is had, and the last given out */
        {
                unsigned long many = 0;

                for (int at = 0; at < 40; at++)
                {
                        many = wlc_id(c, WLI_xdg_activation_token_v1, 1);
                        wlc_send(c, activation, WLQ_xdg_activation_v1_get_activation_token, many);
                }
                wlc_forget(c);
                wlc_send(c, many, WLQ_xdg_activation_token_v1_commit);
                wlc_pump(c);
                WLCHECK(c->errors == 0 && wlc_count(c, WLI_xdg_activation_token_v1, WLE_xdg_activation_token_v1_done) == 1,
                        "forty tokens held by one client are no error, and the fortieth is given out at its commit");
        }
        wlc_end(c);
        leak_check("activation");
        wayland_stop();
}

/*
        wp_cursor_shape: the shape a client asks for is given to the window it is over, when its serial is one of its recent inputs
        (a stale one is nothing); a shape the compositor lacks is the arrow; the shape is none again on enter and on leave.
*/
/*
        zwp_pointer_gestures_v1: a client that asks for a pinch gets it for its windows (they take the pinch, not Ctrl and a wheel),
        and a pinch of the pointer's window is begin (two fingers), updates (the focal point's move, the scale in 256ths of 1.0) and
        end, each for the pinch objects of that client only.
*/
static void test_pinch(void)
{
        struct session s;
        struct wlc *c;
        unsigned long seat, pointer, gestures, pinch;
        long a, before;

        wl_reset();
        wayland_start(&fake_host);
        session_open(&s);
        session_window(&s, 0xff223344);
        c = s.c;
        seat = wl_global(c, s.registry, WLI_wl_seat, 9);
        pointer = wlc_id(c, WLI_wl_pointer, 9);
        gestures = wl_global(c, s.registry, WLI_zwp_pointer_gestures_v1, 3);
        wlc_forget(c);
        wlc_send(c, seat, WLQ_wl_seat_get_pointer, pointer);
        wayland_pointer_to(0, 10, 10);
        wlc_pump(c);
        wlc_forget(c);
        pinch = wlc_id(c, WLI_zwp_pointer_gesture_pinch_v1, 3);
        before = fake.row[0].did_count;
        wlc_send(c, gestures, WLQ_zwp_pointer_gestures_v1_get_pinch_gesture, pinch, pointer);
        WLCHECK(gestures && c->errors == 0, "the pointer gestures are offered, and a pinch object is no error");
        {
                int asked = 0;

                for (long at = before; at < fake.row[0].did_count && at < 16; at++)
                        asked |= fake.row[0].did[at] == WAYLAND_DO_PINCH_WANT;
                WLCHECK(asked, "a client that asks for a pinch takes it for its window");
        }

        wayland_pointer_pinch(0, 1, 1024, 20, 12);
        wlc_pump(c);
        a = event_at(c, WLI_zwp_pointer_gesture_pinch_v1, WLE_zwp_pointer_gesture_pinch_v1_begin, 0);
        WLCHECK(a >= 0 && c->ev[a].u[2] == s.surface && c->ev[a].u[3] == 2, "a pinch begins with two fingers on the window it began on");
        wlc_forget(c);
        wayland_pointer_pinch(0, 0, 2048, 25, 12);
        wlc_pump(c);
        a = event_at(c, WLI_zwp_pointer_gesture_pinch_v1, WLE_zwp_pointer_gesture_pinch_v1_update, 0);
        WLCHECK(a >= 0 && c->ev[a].u[1] == 5 * 256 && c->ev[a].u[2] == 0 && c->ev[a].u[3] == 512 && c->ev[a].u[4] == 0,
                "an update is the focal point's move, the scale (2.0 is 512 in fixed point) and no rotation");
        wlc_forget(c);
        wayland_pointer_pinch(0, 2, 0, 25, 12);
        wlc_pump(c);
        a = event_at(c, WLI_zwp_pointer_gesture_pinch_v1, WLE_zwp_pointer_gesture_pinch_v1_end, 0);
        WLCHECK(a >= 0 && c->ev[a].u[2] == 0 && c->errors == 0, "a pinch ends, not cancelled");
        wlc_end(c);
        leak_check("pinch");
        wayland_stop();
}

static void test_cursor_shape(void)
{
        struct session s;
        struct wlc *c;
        unsigned long seat, pointer, manager, device;
        long a, serial;

        wl_reset();
        wayland_start(&fake_host);
        session_open(&s);
        session_window(&s, 0xff223344);
        c = s.c;
        seat = wl_global(c, s.registry, WLI_wl_seat, 9);
        pointer = wlc_id(c, WLI_wl_pointer, 9);
        manager = wl_global(c, s.registry, WLI_wp_cursor_shape_manager_v1, 2);
        wlc_forget(c);
        wlc_send(c, seat, WLQ_wl_seat_get_pointer, pointer);
        wayland_pointer_to(0, 10, 10);
        wlc_pump(c);
        a = event_at(c, WLI_wl_pointer, WLE_wl_pointer_enter, 0);
        serial = a >= 0 ? (long)c->ev[a].u[0] : 0;
        device = wlc_id(c, WLI_wp_cursor_shape_device_v1, 2);
        wlc_send(c, manager, WLQ_wp_cursor_shape_manager_v1_get_pointer, device, pointer);
        WLCHECK(manager && serial, "the cursor shape manager is offered, and the enter gives a serial");
        WLCHECK(fake_cursor[0] == -1, "a window that the pointer enters has no shape of its client's yet");

        wlc_send(c, device, WLQ_wp_cursor_shape_device_v1_set_shape, (unsigned long)serial,
                 (unsigned long)WLN_wp_cursor_shape_device_v1_shape_text);
        WLCHECK(c->errors == 0 && fake_cursor[0] == WAYLAND_CURSOR_TEXT, "an I-beam asked for over the window is the text shape");
        wlc_send(c, device, WLQ_wp_cursor_shape_device_v1_set_shape, (unsigned long)(serial - 500),
                 (unsigned long)WLN_wp_cursor_shape_device_v1_shape_crosshair);
        WLCHECK(fake_cursor[0] == WAYLAND_CURSOR_TEXT, "a shape with a serial that is not a recent input of the client is nothing");
        wlc_send(c, device, WLQ_wp_cursor_shape_device_v1_set_shape, (unsigned long)serial,
                 (unsigned long)WLN_wp_cursor_shape_device_v1_shape_zoom_in);
        WLCHECK(fake_cursor[0] == WAYLAND_CURSOR_ARROW, "a shape the compositor does not draw is the arrow");
        wlc_send(c, device, WLQ_wp_cursor_shape_device_v1_set_shape, (unsigned long)serial,
                 (unsigned long)WLN_wp_cursor_shape_device_v1_shape_nwse_resize);
        WLCHECK(fake_cursor[0] == WAYLAND_CURSOR_RESIZE_NWSE, "a diagonal resize is the diagonal resize");

        wlc_send(c, device, WLQ_wp_cursor_shape_device_v1_set_shape, (unsigned long)serial,
                 (unsigned long)WLN_wp_cursor_shape_device_v1_shape_pointer);
        WLCHECK(fake_cursor[0] == WAYLAND_CURSOR_HAND, "a link's pointer shape is the hand");
        wlc_send(c, device, WLQ_wp_cursor_shape_device_v1_set_shape, (unsigned long)serial,
                 (unsigned long)WLN_wp_cursor_shape_device_v1_shape_wait);
        WLCHECK(fake_cursor[0] == WAYLAND_CURSOR_WAIT, "a busy window's shape is the hourglass");
        wlc_send(c, device, WLQ_wp_cursor_shape_device_v1_set_shape, (unsigned long)serial,
                 (unsigned long)WLN_wp_cursor_shape_device_v1_shape_not_allowed);
        WLCHECK(fake_cursor[0] == WAYLAND_CURSOR_NOTALLOWED, "a not-allowed shape is the circle and slash");

        /* wl_pointer.set_cursor: no surface hides the pointer over the window; a stale serial is nothing; a surface is the arrow */
        wlc_send(c, pointer, WLQ_wl_pointer_set_cursor, (unsigned long)(serial - 500), 0ul, 0ul, 0ul);
        WLCHECK(fake_cursor[0] == WAYLAND_CURSOR_NOTALLOWED, "a set_cursor with a serial that is not a recent input is nothing");
        wlc_send(c, pointer, WLQ_wl_pointer_set_cursor, (unsigned long)serial, 0ul, 0ul, 0ul);
        WLCHECK(c->errors == 0 && fake_cursor[0] == WAYLAND_CURSOR_HIDDEN, "a set_cursor with no surface hides the pointer over the window");
        {
                unsigned long drawn = wlc_id(c, WLI_wl_surface, 6);

                // A surface of its own: the window's surface has a role, and naming it is the role error (test_cursor_picture).
                wlc_send(c, s.compositor, WLQ_wl_compositor_create_surface, drawn);
                wlc_send(c, pointer, WLQ_wl_pointer_set_cursor, (unsigned long)serial, drawn, 0ul, 0ul);
        }
        WLCHECK(c->errors == 0 && fake_cursor[0] == WAYLAND_CURSOR_ARROW, "a set_cursor with a surface gives the arrow until the surface is drawn");

        wayland_pointer_to(-1, 0, 0);
        WLCHECK(fake_cursor[0] == -1, "a shape is none again when the pointer leaves the window");
        WLCHECK(c->errors == 0, "none of it is a protocol error");
        wlc_end(c);
        leak_check("cursor shape");
        wayland_stop();
}

static void test_inhibit_state(void)
{
        struct session s;
        struct wlc *c;
        unsigned long idle, shortcuts, tearing, inhibitor, other, control;

        wl_reset();
        wayland_start(&fake_host);
        session_open(&s);
        session_window(&s, 0xff223344);
        c = s.c;
        idle = wl_global(c, s.registry, WLI_zwp_idle_inhibit_manager_v1, 1);
        shortcuts = wl_global(c, s.registry, WLI_zwp_keyboard_shortcuts_inhibit_manager_v1, 1);
        tearing = wl_global(c, s.registry, WLI_wp_tearing_control_manager_v1, 1);
        wlc_forget(c);

        inhibitor = wlc_id(c, WLI_zwp_idle_inhibitor_v1, 1);
        wlc_send(c, idle, WLQ_zwp_idle_inhibit_manager_v1_create_inhibitor, inhibitor, s.surface);
        WLCHECK(c->errors == 0 && wayland_idle_inhibited() == 1, "an idle inhibitor is held while it lives");
        other = wlc_id(c, WLI_zwp_idle_inhibitor_v1, 1);
        wlc_send(c, idle, WLQ_zwp_idle_inhibit_manager_v1_create_inhibitor, other, s.surface);
        WLCHECK(wayland_idle_inhibited() == 2, "a second inhibitor on a shown surface is counted: each holds while its surface is shown");
        wlc_send(c, inhibitor, WLQ_zwp_idle_inhibitor_v1_destroy);
        wlc_send(c, other, WLQ_zwp_idle_inhibitor_v1_destroy);
        WLCHECK(wayland_idle_inhibited() == 0, "and none held once they are destroyed");
        inhibitor = wlc_id(c, WLI_zwp_idle_inhibitor_v1, 1);
        wlc_send(c, idle, WLQ_zwp_idle_inhibit_manager_v1_create_inhibitor, inhibitor, s.surface);
        WLCHECK(wayland_idle_inhibited() == 1, "a new one is counted again");

        other = wlc_id(c, WLI_zwp_keyboard_shortcuts_inhibitor_v1, 1);
        wlc_send(c, shortcuts, WLQ_zwp_keyboard_shortcuts_inhibit_manager_v1_inhibit_shortcuts, other, s.surface, s.seat);
        WLCHECK(wayland_row_shortcuts(0) == 0, "a shortcuts inhibitor is not active until its window has the keys");
        wayland_keyboard_to(0);
        WLCHECK(wayland_row_shortcuts(0) == 1, "and is active once its window has them");

        control = wlc_id(c, WLI_wp_tearing_control_v1, 1);
        wlc_send(c, tearing, WLQ_wp_tearing_control_manager_v1_get_tearing_control, control, s.surface);
        wlc_send(c, control, WLQ_wp_tearing_control_v1_set_presentation_hint, (unsigned long)WLN_wp_tearing_control_v1_presentation_hint_async);
        WLCHECK(wayland_row_hint(0, WAYLAND_HINT_ASYNC) == 0, "a tearing hint is shown from the commit after it is set");
        wlc_send(c, s.surface, WLQ_wl_surface_commit);
        WLCHECK(c->errors == 0 && wayland_row_hint(0, WAYLAND_HINT_ASYNC) == 1, "and is asynchronous from it");
        wlc_send(c, control, WLQ_wp_tearing_control_v1_destroy);
        wlc_send(c, s.surface, WLQ_wl_surface_commit);
        WLCHECK(wayland_row_hint(0, WAYLAND_HINT_ASYNC) == 0, "and back to vsync once destroyed");

        /* a client going lets go of the inhibitor it held */
        wlc_end(c);
        WLCHECK(wayland_idle_inhibited() == 0, "a client that goes lets go of the inhibitors it held");
        wayland_stop();
}

/*
        ext-idle-notify, and what holds it idle back. The clock is the last input the compositor gave (wayland_input_seen), and the fake
        host's clock is held still, so each row says exactly when a notification goes idle. An inhibitor holds only while its surface is
        shown and not minimized; a get_input_idle_notification counts input alone.
*/
static long idle_count(struct wlc *c, long opcode)
{
        wlc_pump(c);
        return wlc_count(c, WLI_ext_idle_notification_v1, opcode);
}

static void test_idle_notify(void)
{
        struct session s, s2;
        struct wlc *c;
        unsigned long notifier, note, later, inhibitor, input_note, first;
        unsigned long s2_notifier, s2_note[9];
        long in_ms, inhibitors, idled, at;

        wl_reset();
        fake.clock_still = 1;
        fake.now = 10000;
        wayland_start(&fake_host);
        session_open(&s);
        session_window(&s, 0xff223344);
        c = s.c;
        notifier = wl_global(c, s.registry, WLI_ext_idle_notifier_v1, 2);
        wlc_forget(c);
        WLCHECK(c->errors == 0 && notifier != 0, "ext-idle-notify is advertised, at version 2");

        /* a notification goes idle at its timeout from the last input (the start), not a millisecond before, and says so once */
        note = wlc_id(c, WLI_ext_idle_notification_v1, 2);
        wlc_send(c, notifier, WLQ_ext_idle_notifier_v1_get_idle_notification, note, 1000UL, s.seat);
        WLCHECK(c->errors == 0 && wayland_idle_due() == 11000, "a notification is due its timeout after the input it counts from");
        fake.now = 10999;
        wayland_idle_tick();
        WLCHECK(idle_count(c, WLE_ext_idle_notification_v1_idled) == 0, "not idle a millisecond before its timeout");
        fake.now = 11000;
        wayland_idle_tick();
        WLCHECK(idle_count(c, WLE_ext_idle_notification_v1_idled) == 1, "idle at its timeout, with one idled event");
        wayland_idle_tick();
        WLCHECK(idle_count(c, WLE_ext_idle_notification_v1_idled) == 1, "and told once, not again on the next pass");
        WLCHECK(wayland_idle_due() == 0, "nothing is due while it waits for input to resume it");

        /* input resumes it, and its timeout counts from that input */
        fake.now = 12000;
        wayland_input_seen();
        wayland_idle_tick();
        WLCHECK(idle_count(c, WLE_ext_idle_notification_v1_resumed) == 1, "input after idle sends resumed, once");
        fake.now = 12999;
        wayland_idle_tick();
        WLCHECK(idle_count(c, WLE_ext_idle_notification_v1_idled) == 1, "the timeout counts from that input, not from the start");
        fake.now = 13000;
        wayland_idle_tick();
        WLCHECK(idle_count(c, WLE_ext_idle_notification_v1_idled) == 2, "idle again a timeout after it");

        /* an inhibitor on a shown surface holds a notification from going idle; its destroy lets it go at once */
        inhibitor = wlc_id(c, WLI_zwp_idle_inhibitor_v1, 1);
        wlc_send(c, s.idle, WLQ_zwp_idle_inhibit_manager_v1_create_inhibitor, inhibitor, s.surface);
        later = wlc_id(c, WLI_ext_idle_notification_v1, 2);
        wlc_send(c, notifier, WLQ_ext_idle_notifier_v1_get_idle_notification, later, 500UL, s.seat);
        fake.now = 14000;
        wayland_idle_tick();
        WLCHECK(idle_count(c, WLE_ext_idle_notification_v1_idled) == 2, "an inhibitor on a shown surface holds a notification from going idle");
        WLCHECK(wayland_idle_due() == 0 && wayland_idle_inhibited() == 1, "nothing is due while it holds, and the inhibitor is counted");
        wlc_send(c, inhibitor, WLQ_zwp_idle_inhibitor_v1_destroy);
        wayland_idle_tick();
        WLCHECK(idle_count(c, WLE_ext_idle_notification_v1_idled) == 3, "destroying the inhibitor lets the notification it held go idle at once");

        /* get_input_idle_notification ignores inhibitors: only input counts */
        inhibitor = wlc_id(c, WLI_zwp_idle_inhibitor_v1, 1);
        wlc_send(c, s.idle, WLQ_zwp_idle_inhibit_manager_v1_create_inhibitor, inhibitor, s.surface);
        input_note = wlc_id(c, WLI_ext_idle_notification_v1, 2);
        wlc_send(c, notifier, WLQ_ext_idle_notifier_v1_get_input_idle_notification, input_note, 500UL, s.seat);
        fake.now = 14600;
        wayland_idle_tick();
        WLCHECK(idle_count(c, WLE_ext_idle_notification_v1_idled) == 4, "an input-only notification goes idle though an inhibitor holds");

        /* a minimized window's inhibitor holds nothing; shown again, it holds */
        fake.row[0].states = WAYLAND_STATE_SUSPENDED;
        WLCHECK(wayland_idle_inhibited() == 0, "a minimized window's inhibitor holds nothing");
        fake.row[0].states = 0;
        WLCHECK(wayland_idle_inhibited() == 1, "and holds again once the window is shown");

        /* the idle clock: the input it counts from, the inhibitors that hold, the notifications idle (three: note, later, input_note) */
        wayland_idle_state(&in_ms, &inhibitors, &idled);
        WLCHECK(in_ms == 12000 && inhibitors == 1 && idled == 3, "the idle clock says when input last came, what holds, and what is idle");
        wlc_send(c, inhibitor, WLQ_zwp_idle_inhibitor_v1_destroy);

        /* a timeout of none is idle at once; the largest is due a long way off and does not go idle */
        first = wlc_id(c, WLI_ext_idle_notification_v1, 2);
        wlc_send(c, notifier, WLQ_ext_idle_notifier_v1_get_idle_notification, first, 0UL, s.seat);
        wayland_idle_tick();
        WLCHECK(c->errors == 0 && idle_count(c, WLE_ext_idle_notification_v1_idled) == 5, "a timeout of 0 is idle at once");
        first = wlc_id(c, WLI_ext_idle_notification_v1, 2);
        wlc_send(c, notifier, WLQ_ext_idle_notifier_v1_get_idle_notification, first, 0xffffffffUL, s.seat);
        wayland_idle_tick();
        WLCHECK(c->errors == 0 && idle_count(c, WLE_ext_idle_notification_v1_idled) == 5 && wayland_idle_due() > 0,
                "the largest timeout is accepted and is due far off, not at once");

        /* a shown surface destroyed takes its inhibitor with it */
        inhibitor = wlc_id(c, WLI_zwp_idle_inhibitor_v1, 1);
        wlc_send(c, s.idle, WLQ_zwp_idle_inhibit_manager_v1_create_inhibitor, inhibitor, s.surface);
        WLCHECK(wayland_idle_inhibited() == 1, "a new inhibitor holds on the shown surface");
        wlc_send(c, s.toplevel, WLQ_xdg_toplevel_destroy);
        wlc_send(c, s.xdg, WLQ_xdg_surface_destroy);
        wlc_send(c, s.surface, WLQ_wl_surface_destroy);
        WLCHECK(c->errors == 0 && wayland_idle_inhibited() == 0, "destroying its surface ends its inhibitor");

        /* one client holds eight, a destroyed one gives its place back, and the ninth is given as well */
        session_open(&s2);
        s2_notifier = wl_global(s2.c, s2.registry, WLI_ext_idle_notifier_v1, 2);
        wlc_forget(s2.c);
        for (at = 0; at < 8; at++)
        {
                s2_note[at] = wlc_id(s2.c, WLI_ext_idle_notification_v1, 2);
                wlc_send(s2.c, s2_notifier, WLQ_ext_idle_notifier_v1_get_idle_notification, s2_note[at], 100UL, s2.seat);
        }
        WLCHECK(s2.c->errors == 0, "eight notifications a client is given");
        wlc_send(s2.c, s2_note[0], WLQ_ext_idle_notification_v1_destroy);
        s2_note[8] = wlc_id(s2.c, WLI_ext_idle_notification_v1, 2);
        wlc_send(s2.c, s2_notifier, WLQ_ext_idle_notifier_v1_get_idle_notification, s2_note[8], 100UL, s2.seat);
        WLCHECK(s2.c->errors == 0, "a destroyed notification gives its place back");
        wlc_send(s2.c, s2_notifier, WLQ_ext_idle_notifier_v1_get_idle_notification, wlc_id(s2.c, WLI_ext_idle_notification_v1, 2), 100UL,
                 s2.seat);
        WLCHECK(s2.c->errors == 0, "a ninth notification is given too: a client holds as many as it makes");
        wlc_end(s2.c);

        /* a client that goes takes its notifications with it: nothing is due, and nothing is idle */
        wlc_end(c);
        WLCHECK(wayland_idle_due() == 0, "a client that goes takes its notifications with it");
        wayland_idle_state(&in_ms, &inhibitors, &idled);
        WLCHECK(idled == 0 && inhibitors == 0, "and nothing is idle or held");
        wayland_stop();
}

/*
        xdg-toplevel-icon and xdg_system_bell. An icon with a square buffer is shown on its toplevel from the commit after it is set; an
        icon with neither pixels nor a name resets it; an icon on a toplevel is immutable; a buffer that is not a square of shared memory,
        or a scale below one, is refused; a ring is counted with or without a surface. A refusal ends its client, so each has its own.
*/
static unsigned long square_buffer(struct session *s, long n)
{
        struct wlc *c = s->c;
        unsigned long pool, buffer;
        long fd = wl_file(n * n * 4, 0);

        pool = wlc_id(c, WLI_wl_shm_pool, 1);
        wlc_send(c, s->shm, WLQ_wl_shm_create_pool, pool, fd, (long)(n * n * 4));
        close((int)fd);
        buffer = wlc_id(c, WLI_wl_buffer, 1);
        wlc_send(c, pool, WLQ_wl_shm_pool_create_buffer, buffer, 0L, n, n, n * 4, (long)WLN_wl_shm_format_xrgb8888);
        return buffer;
}

static unsigned long icon_session(struct session *s)
{
        session_open(s);
        session_window(s, 0xff556677);
        return wl_global(s->c, s->registry, WLI_xdg_toplevel_icon_manager_v1, 1);
}

static void test_icon(void)
{
        struct session s;
        struct wlc *c;
        unsigned long manager, buffer, icon, empty, bell;
        long bells, icons, before;

        wl_reset();
        wayland_start(&fake_host);
        manager = icon_session(&s);
        c = s.c;
        WLCHECK(manager != 0 && c->errors == 0 && wlc_count(c, WLI_xdg_toplevel_icon_manager_v1, WLE_xdg_toplevel_icon_manager_v1_done) == 1,
                "xdg-toplevel-icon is advertised, and its manager is sent done at once (no icon size is preferred)");
        buffer = square_buffer(&s, 16);
        icon = wlc_id(c, WLI_xdg_toplevel_icon_v1, 1);
        wlc_send(c, manager, WLQ_xdg_toplevel_icon_manager_v1_create_icon, icon);
        wlc_send(c, icon, WLQ_xdg_toplevel_icon_v1_add_buffer, buffer, 1L);
        wlc_send(c, manager, WLQ_xdg_toplevel_icon_manager_v1_set_icon, s.toplevel, icon);
        WLCHECK(c->errors == 0 && wayland_row_hint(0, WAYLAND_HINT_ICON) == 0, "an icon is applied from the commit after it is set, as set_icon is double-buffered");
        wlc_send(c, s.surface, WLQ_wl_surface_commit);
        WLCHECK(c->errors == 0 && wayland_row_hint(0, WAYLAND_HINT_ICON) == 1, "and the window has an icon from that commit");
        wayland_hint_counts(&bells, &icons);
        WLCHECK(icons == 1, "the icon set is counted once");
        empty = wlc_id(c, WLI_xdg_toplevel_icon_v1, 1);
        wlc_send(c, manager, WLQ_xdg_toplevel_icon_manager_v1_create_icon, empty);
        wlc_send(c, manager, WLQ_xdg_toplevel_icon_manager_v1_set_icon, s.toplevel, empty);
        wlc_send(c, s.surface, WLQ_wl_surface_commit);
        WLCHECK(c->errors == 0 && wayland_row_hint(0, WAYLAND_HINT_ICON) == 0, "an icon with neither pixels nor a name resets the icon");
        wayland_hint_counts(&bells, &icons);
        WLCHECK(icons == 1, "and is not counted as a set");
        wlc_end(c);

        /* an icon on a toplevel is immutable: a later buffer is refused with immutable */
        manager = icon_session(&s);
        c = s.c;
        buffer = square_buffer(&s, 16);
        icon = wlc_id(c, WLI_xdg_toplevel_icon_v1, 1);
        wlc_send(c, manager, WLQ_xdg_toplevel_icon_manager_v1_create_icon, icon);
        wlc_send(c, manager, WLQ_xdg_toplevel_icon_manager_v1_set_icon, s.toplevel, icon);
        wlc_send(c, icon, WLQ_xdg_toplevel_icon_v1_add_buffer, buffer, 1L);
        WLCHECK(c->errors == 1 && c->error_code == WLN_xdg_toplevel_icon_v1_error_immutable, "an icon set on a toplevel cannot take a buffer");
        wlc_end(c);

        /* a buffer that is not a square of shared memory, or a scale below one, is invalid_buffer */
        manager = icon_session(&s);
        c = s.c;
        icon = wlc_id(c, WLI_xdg_toplevel_icon_v1, 1);
        wlc_send(c, manager, WLQ_xdg_toplevel_icon_manager_v1_create_icon, icon);
        wlc_send(c, icon, WLQ_xdg_toplevel_icon_v1_add_buffer, s.buffer, 1L);
        WLCHECK(c->errors == 1 && c->error_code == WLN_xdg_toplevel_icon_v1_error_invalid_buffer, "a buffer that is not square is invalid_buffer");
        wlc_end(c);
        manager = icon_session(&s);
        c = s.c;
        buffer = square_buffer(&s, 16);
        icon = wlc_id(c, WLI_xdg_toplevel_icon_v1, 1);
        wlc_send(c, manager, WLQ_xdg_toplevel_icon_manager_v1_create_icon, icon);
        wlc_send(c, icon, WLQ_xdg_toplevel_icon_v1_add_buffer, buffer, 0L);
        WLCHECK(c->errors == 1 && c->error_code == WLN_xdg_toplevel_icon_v1_error_invalid_buffer, "a scale below one is invalid_buffer");
        wlc_end(c);

        /* a name is content; a ring is counted, with a surface or without */
        manager = icon_session(&s);
        c = s.c;
        bell = wl_global(c, s.registry, WLI_xdg_system_bell_v1, 1);
        icon = wlc_id(c, WLI_xdg_toplevel_icon_v1, 1);
        wlc_send(c, manager, WLQ_xdg_toplevel_icon_manager_v1_create_icon, icon);
        wlc_send(c, icon, WLQ_xdg_toplevel_icon_v1_set_name, "applications-games");
        wlc_send(c, manager, WLQ_xdg_toplevel_icon_manager_v1_set_icon, s.toplevel, icon);
        wlc_send(c, s.surface, WLQ_wl_surface_commit);
        WLCHECK(c->errors == 0 && wayland_row_hint(0, WAYLAND_HINT_ICON) == 1, "an icon named by a name is an icon");
        wayland_hint_counts(&before, &icons);
        wlc_send(c, bell, WLQ_xdg_system_bell_v1_ring, s.surface);
        wlc_send(c, bell, WLQ_xdg_system_bell_v1_ring, 0UL);
        wayland_hint_counts(&bells, &icons);
        WLCHECK(c->errors == 0 && bells == before + 2, "each ring is counted, with a surface or without one");
        wlc_end(c);
        wayland_stop();
}

static void test_presentation(void)
{
        struct session s;
        struct wlc *c;
        unsigned long presentation, feedback, plain;
        long a;

        wl_reset();
        wayland_start(&fake_host);
        session_open(&s);
        session_window(&s, 0xff223344);
        c = s.c;
        presentation = wl_global(c, s.registry, WLI_wp_presentation, 2);
        WLCHECK(presentation != 0 && c->errors == 0, "wp_presentation is offered at version 2, the version clients bind (weston-presentation-shm), and a bind at it is no error");
        a = event_at(c, WLI_wp_presentation, WLE_wp_presentation_clock_id, 0);
        WLCHECK(a >= 0 && c->ev[a].u[0] == 1, "its clock is CLOCK_MONOTONIC (1), told when it is bound");
        wlc_forget(c);

        feedback = wlc_id(c, WLI_wp_presentation_feedback, 1);
        wlc_send(c, presentation, WLQ_wp_presentation_feedback, s.surface, feedback);
        wlc_send(c, s.surface, WLQ_wl_surface_commit);
        wayland_frame();
        wlc_pump(c);
        WLCHECK(wlc_count(c, WLI_wp_presentation_feedback, WLE_wp_presentation_feedback_presented) == 0,
                "a feedback waits for the frame its commit is drawn in, and a timer's frame answers none");
        wlc_forget(c);
        wayland_presented(5000000123ul, 16666667ul, 7, 0ul, -1L);
        wlc_pump(c);
        a = event_at(c, WLI_wp_presentation_feedback, WLE_wp_presentation_feedback_presented, 0);
        WLCHECK(a >= 0 && c->ev[a].u[0] == 0 && c->ev[a].u[1] == 5 && c->ev[a].u[2] == 123 && c->ev[a].u[3] == 16666667 &&
                    c->ev[a].u[4] == 0 && c->ev[a].u[5] == 7 && c->ev[a].u[6] == 0,
                "a drawn frame presents it: the seconds split in two, the nanoseconds, the period, the count, no flags");
        wayland_presented(6000000000ul, 16666667ul, 8, 7ul, -1L);
        wlc_pump(c);
        WLCHECK(wlc_count(c, WLI_wp_presentation_feedback, WLE_wp_presentation_feedback_presented) == 1,
                "the object is let go by its event: a later frame answers no one");

        /* a commit that draws nothing (the screen already shows its picture): the feedback waits for a frame, the compositor is told one is
           wanted, and the idle frame answers it with the time it is at */
        wlc_forget(c);
        feedback = wlc_id(c, WLI_wp_presentation_feedback, 1);
        wlc_send(c, presentation, WLQ_wp_presentation_feedback, s.surface, feedback);
        wlc_send(c, s.surface, WLQ_wl_surface_commit);
        wlc_pump(c);
        WLCHECK(wayland_frame_wanted(), "a feedback that waits for a frame asks the compositor for one while its window is shown");
        fake.row[0].states |= WAYLAND_STATE_SUSPENDED;
        WLCHECK(!wayland_frame_wanted(), "a window put away with a feedback waiting asks for no frame");
        fake.row[0].states &= ~(long)WAYLAND_STATE_SUSPENDED;
        wlc_forget(c);
        wayland_presented_idle(7000000000ul, 16666667ul, 9, 0ul);
        wlc_pump(c);
        a = event_at(c, WLI_wp_presentation_feedback, WLE_wp_presentation_feedback_presented, 0);
        WLCHECK(a >= 0 && c->ev[a].u[1] == 7 && c->ev[a].u[2] == 0 && c->ev[a].u[6] == 0 && !wayland_frame_wanted(),
                "an idle frame presents the feedback at its time, with no flags, and nothing waits after it");

        /* a present latches what was committed before it began (wayland_latch): a commit made while that frame is composed or its flip is on
           its way is the next frame's, its feedback answered by the next flip and its callback by the next frame's end */
        {
                unsigned long first = wlc_id(c, WLI_wp_presentation_feedback, 1), second, cb_first = wlc_id(c, WLI_wl_callback, 1), cb_second;

                wlc_forget(c);
                wlc_send(c, presentation, WLQ_wp_presentation_feedback, s.surface, first);
                wlc_send(c, s.surface, WLQ_wl_surface_frame, cb_first);
                wlc_send(c, s.surface, WLQ_wl_surface_commit);
                wayland_latch();
                second = wlc_id(c, WLI_wp_presentation_feedback, 1);
                cb_second = wlc_id(c, WLI_wl_callback, 1);
                wlc_send(c, presentation, WLQ_wp_presentation_feedback, s.surface, second);
                wlc_send(c, s.surface, WLQ_wl_surface_frame, cb_second);
                wlc_send(c, s.surface, WLQ_wl_surface_commit);
                wlc_pump(c);
                WLCHECK(wlc_count(c, WLI_wp_presentation_feedback, WLE_wp_presentation_feedback_discarded) == 0,
                        "a commit made while a frame is in flight does not discard the feedback that frame latched");
                WLCHECK(wlc_count(c, WLI_wl_callback, WLE_wl_callback_done) == 1, "the latch answers the callback taken before it, at once");
                wayland_frame();
                wlc_pump(c);
                WLCHECK(wlc_count(c, WLI_wl_callback, WLE_wl_callback_done) == 1 && wayland_frame_wanted(),
                        "the frame's end answers none taken after the latch, and a frame is wanted for them");
                wayland_presented(8000000000ul, 6957000ul, 20, 7ul, 0L);
                wlc_pump(c);
                a = event_at(c, WLI_wp_presentation_feedback, WLE_wp_presentation_feedback_presented, 0);
                WLCHECK(wlc_count(c, WLI_wp_presentation_feedback, WLE_wp_presentation_feedback_presented) == 1 && a >= 0 && c->ev[a].u[5] == 20 &&
                            c->ev[a].u[6] == 15,
                        "its flip presents the feedback it latched, the row shown from its client's buffer flagged zero copy (0x%lx), and not the later one",
                        a >= 0 ? (unsigned long)c->ev[a].u[6] : 0ul);
                wayland_latch();
                wayland_frame();
                wayland_presented(8006957000ul, 6957000ul, 21, 7ul, -1L);
                wlc_pump(c);
                a = event_at(c, WLI_wp_presentation_feedback, WLE_wp_presentation_feedback_presented, a + 1);
                WLCHECK(wlc_count(c, WLI_wl_callback, WLE_wl_callback_done) == 2 && a >= 0 && c->ev[a].u[5] == 21 && c->ev[a].u[6] == 7 &&
                            !wayland_frame_wanted(),
                        "the next frame answers the later commit's callback and feedback, at its own flip, with no zero copy");
                (void)second, (void)cb_second;
        }

        /* a commit that replaces one no frame latched yet discards that one's feedback: its content was never shown */
        wlc_forget(c);
        wlc_send(c, presentation, WLQ_wp_presentation_feedback, s.surface, wlc_id(c, WLI_wp_presentation_feedback, 1));
        wlc_send(c, s.surface, WLQ_wl_surface_commit);
        wlc_send(c, presentation, WLQ_wp_presentation_feedback, s.surface, wlc_id(c, WLI_wp_presentation_feedback, 1));
        wlc_send(c, s.surface, WLQ_wl_surface_commit);
        wlc_pump(c);
        WLCHECK(wlc_count(c, WLI_wp_presentation_feedback, WLE_wp_presentation_feedback_discarded) == 1,
                "a commit replacing one that no frame latched discards that one's feedback");
        wayland_latch();
        wayland_presented(9000000000ul, 6957000ul, 30, 7ul, -1L);
        wlc_pump(c);
        WLCHECK(wlc_count(c, WLI_wp_presentation_feedback, WLE_wp_presentation_feedback_presented) == 1 && !wayland_frame_wanted(),
                "and the frame presents the one that replaced it");

        /* more than the surface holds in itself: kept, none discarded (the list grows past them) */
        wlc_forget(c);
        for (int at = 0; at < 4 * WAYLAND_PRESENTATIONS + 1; at++)
                wlc_send(c, presentation, WLQ_wp_presentation_feedback, s.surface, wlc_id(c, WLI_wp_presentation_feedback, 1));
        wlc_pump(c);
        WLCHECK(wlc_count(c, WLI_wp_presentation_feedback, WLE_wp_presentation_feedback_discarded) == 0,
                "a surface asked for more feedbacks than it holds in itself keeps them all, and discards none");

        /* a surface with no window has no frame: its feedback is discarded at the commit */
        plain = wlc_id(c, WLI_wl_surface, 6);
        wlc_send(c, s.compositor, WLQ_wl_compositor_create_surface, plain);
        wlc_forget(c);
        wlc_send(c, presentation, WLQ_wp_presentation_feedback, plain, wlc_id(c, WLI_wp_presentation_feedback, 1));
        wlc_send(c, plain, WLQ_wl_surface_commit);
        WLCHECK(wlc_count(c, WLI_wp_presentation_feedback, WLE_wp_presentation_feedback_discarded) == 1,
                "a surface with no window discards its feedback at its commit");

        /* a surface destroyed discards the feedbacks it holds */
        wlc_forget(c);
        wlc_send(c, s.surface, WLQ_wl_surface_destroy);
        wlc_pump(c);
        WLCHECK(wlc_count(c, WLI_wp_presentation_feedback, WLE_wp_presentation_feedback_discarded) == 4 * WAYLAND_PRESENTATIONS + 1,
                "destroying a surface discards the feedbacks it holds, every one");
        WLCHECK(c->errors == 0, "none of it is a protocol error");
        wlc_end(c);
        leak_check("presentation");
        wayland_stop();
}

static void test_text(void)
{
        struct session s;
        struct wlc *c;
        static const char *titles[] = {"plain", "t\x01\x02" "ab", "\xff\xfe""bad", "caf\xc3\xa9", "\xed\xa0\x80surrogate", "\xf4\x90\x80\x80over",
                                       "\xc0\xaf", ""};

        wl_reset();
        wayland_start(&fake_host);
        session_open(&s);
        session_window(&s, 0xff000000);
        c = s.c;
        for (unsigned at = 0; at < sizeof titles / sizeof titles[0]; at++)
        {
                char want[WAYLAND_TEXT];

                wlc_send(c, s.toplevel, WLQ_xdg_toplevel_set_title, titles[at]);
                snprintf(want, sizeof want, "%s", fake.row[0].title);
                for (const char *p = fake.row[0].title; *p; p++)
                        WLCHECK((unsigned char)*p >= 0x20 && *p != 0x7f, "title %u has no control byte", at);
        }
        wlc_send(c, s.toplevel, WLQ_xdg_toplevel_set_title, "caf\xc3\xa9");
        WLCHECK(!strcmp(fake.row[0].title, "caf\xc3\xa9"), "valid UTF-8 is kept");
        wlc_send(c, s.toplevel, WLQ_xdg_toplevel_set_title, "\xff");
        WLCHECK(!strcmp(fake.row[0].title, "?"), "an invalid byte is a question mark");
        {
                char big[1000];

                memset(big, 'a', sizeof big - 1);
                big[sizeof big - 1] = 0;
                wlc_send(c, s.toplevel, WLQ_xdg_toplevel_set_title, big);
                WLCHECK(strlen(fake.row[0].title) == WAYLAND_TEXT - 1, "a long title is cut to the room (%zu)", strlen(fake.row[0].title));
                /* a multi-byte character that would not fit whole is not cut in half */
                memset(big, 'a', WAYLAND_TEXT - 2);
                big[WAYLAND_TEXT - 2] = 0;
                strcat(big, "\xc3\xa9");
                wlc_send(c, s.toplevel, WLQ_xdg_toplevel_set_title, big);
                WLCHECK(strlen(fake.row[0].title) == WAYLAND_TEXT - 1 || strlen(fake.row[0].title) == WAYLAND_TEXT - 2,
                        "a character is not cut in half (%zu)", strlen(fake.row[0].title));
        }
        wlc_end(c);

        /* a window with no title is called what its program is called, until it sets a title */
        session_open(&s);
        session_window(&s, 0xff000000);
        c = s.c;
        WLCHECK(fake.row[0].title[0] == 0, "no title, no app id: no text");
        wlc_send(c, s.toplevel, WLQ_xdg_toplevel_set_app_id, "foot");
        WLCHECK(!strcmp(fake.row[0].title, "foot"), "the app id is the tab's text when there is no title (%s)", fake.row[0].title);
        wlc_send(c, s.toplevel, WLQ_xdg_toplevel_set_title, "shell");
        wlc_send(c, s.toplevel, WLQ_xdg_toplevel_set_app_id, "bar");
        WLCHECK(!strcmp(fake.row[0].title, "shell"), "and the title wins once it is set");
        wlc_end(c);
        leak_check("text");
        wayland_stop();
}

/* The pure popup rule, held to every edge. */
static void test_popup_place(void)
{
        struct wayland_positioner p;
        struct wayland_rect screen = {0, 0, 1000, 800};
        long x, y, w, h;

        memset(&p, 0, sizeof p);
        p.width = 100;
        p.height = 50;
        p.anchor_x = 10;
        p.anchor_y = 20;
        p.anchor_width = 40;
        p.anchor_height = 10;
        p.anchor = WLN_xdg_positioner_anchor_bottom_left;
        p.gravity = WLN_xdg_positioner_gravity_bottom_right;
        wayland_popup_place(&p, 100, 100, &screen, &x, &y, &w, &h);
        WLCHECK(x == 10 && y == 30 && w == 100 && h == 50, "below the anchor, to the right (%ld %ld)", x, y);
        p.offset_x = 5;
        p.offset_y = 7;
        wayland_popup_place(&p, 100, 100, &screen, &x, &y, &w, &h);
        WLCHECK(x == 15 && y == 37, "the offset is added (%ld %ld)", x, y);
        p.offset_x = p.offset_y = 0;
        p.anchor = WLN_xdg_positioner_anchor_none;
        p.gravity = WLN_xdg_positioner_gravity_none;
        wayland_popup_place(&p, 0, 0, &screen, &x, &y, &w, &h);
        WLCHECK(x == 10 + 20 - 50 && y == 20 + 5 - 25, "centered on the anchor's centre (%ld %ld)", x, y);

        /* past the right edge: flipped, slid or made narrower, each only when allowed */
        p.anchor = WLN_xdg_positioner_anchor_bottom_right;
        p.gravity = WLN_xdg_positioner_gravity_bottom_right;
        p.anchor_x = 950;
        p.anchor_y = 10;
        p.adjustment = 0;
        wayland_popup_place(&p, 0, 0, &screen, &x, &y, &w, &h);
        WLCHECK(x == 990 && w == 100, "with no adjustment it is where it asks even past the edge (%ld)", x);
        p.adjustment = WLN_xdg_positioner_constraint_adjustment_flip_x;
        wayland_popup_place(&p, 0, 0, &screen, &x, &y, &w, &h);
        WLCHECK(x + w <= 1000 && x >= 0 && x < 990, "flip_x puts it left of the anchor (%ld)", x);
        p.adjustment = WLN_xdg_positioner_constraint_adjustment_slide_x;
        wayland_popup_place(&p, 0, 0, &screen, &x, &y, &w, &h);
        WLCHECK(x == 900 && w == 100, "slide_x slides it to the edge (%ld)", x);
        p.adjustment = WLN_xdg_positioner_constraint_adjustment_resize_x;
        wayland_popup_place(&p, 0, 0, &screen, &x, &y, &w, &h);
        WLCHECK(x == 990 && w == 10, "resize_x cuts it to what fits (%ld %ld)", x, w);
        p.adjustment = WLN_xdg_positioner_constraint_adjustment_flip_y | WLN_xdg_positioner_constraint_adjustment_slide_x;
        p.anchor_y = 790;
        wayland_popup_place(&p, 0, 0, &screen, &x, &y, &w, &h);
        WLCHECK(y + h <= 800 && x == 900, "the axes are separate (%ld %ld)", x, y);

        /* an anchor far off any screen does not make the arithmetic wrap */
        p.anchor_x = 1L << 24;
        p.anchor_y = -(1L << 24);
        p.offset_x = 1L << 24;
        p.offset_y = -(1L << 24);
        p.adjustment = 63;
        wayland_popup_place(&p, 1L << 24, -(1L << 24), &screen, &x, &y, &w, &h);
        WLCHECK(w >= 0 && h >= 0 && w <= 100 && h <= 50, "extremes stay sane (%ld %ld)", w, h);
}

static void test_popups(void)
{
        struct session s;
        struct wlc *c;
        unsigned long positioner, popup_surface, popup_xdg, popup;
        struct wl_event_seen *e;

        wl_reset();
        wayland_start(&fake_host);
        session_open(&s);
        session_window(&s, 0xff000000);
        c = s.c;
        positioner = wlc_id(c, WLI_xdg_positioner, 6);
        wlc_send(c, s.wm_base, WLQ_xdg_wm_base_create_positioner, positioner);
        wlc_send(c, positioner, WLQ_xdg_positioner_set_size, 40L, 30L);
        wlc_send(c, positioner, WLQ_xdg_positioner_set_anchor_rect, 4L, 5L, 10L, 8L);
        wlc_send(c, positioner, WLQ_xdg_positioner_set_anchor, (long)WLN_xdg_positioner_anchor_bottom_left);
        wlc_send(c, positioner, WLQ_xdg_positioner_set_gravity, (long)WLN_xdg_positioner_gravity_bottom_right);
        popup_surface = wlc_id(c, WLI_wl_surface, 6);
        wlc_send(c, s.compositor, WLQ_wl_compositor_create_surface, popup_surface);
        popup_xdg = wlc_id(c, WLI_xdg_surface, 6);
        wlc_send(c, s.wm_base, WLQ_xdg_wm_base_get_xdg_surface, popup_xdg, popup_surface);
        popup = wlc_id(c, WLI_xdg_popup, 6);
        wlc_forget(c);
        wlc_send(c, popup_xdg, WLQ_xdg_surface_get_popup, popup, s.xdg, positioner);
        WLCHECK(c->errors == 0, "a popup of a toplevel (%s)", c->error_text);
        e = wlc_take(c, WLI_xdg_popup, WLE_xdg_popup_configure);
        WLCHECK(e && e->u[0] == 4 && e->u[1] == 13 && e->u[2] == 40 && e->u[3] == 30, "the popup is told where it is (%ld %ld)", e ? (long)e->u[0] : -1,
                e ? (long)e->u[1] : -1);
        WLCHECK(wlc_count(c, WLI_xdg_surface, WLE_xdg_surface_configure) == 1, "with a serial");
        {
                struct wl_event_seen *conf = wlc_take(c, WLI_xdg_surface, WLE_xdg_surface_configure);
                unsigned pixels[40 * 30];
                long fd;
                unsigned long pool = wlc_id(c, WLI_wl_shm_pool, 1), buffer = wlc_id(c, WLI_wl_buffer, 1);

                for (int at = 0; at < 40 * 30; at++)
                        pixels[at] = 0xffabcdef;
                fd = wl_file(sizeof pixels, 0);
                wl_file_put(fd, 0, pixels, sizeof pixels);
                wlc_send(c, s.shm, WLQ_wl_shm_create_pool, pool, fd, (long)sizeof pixels);
                close((int)fd);
                wlc_send(c, pool, WLQ_wl_shm_pool_create_buffer, buffer, 0L, 40L, 30L, 160L, (long)WLN_wl_shm_format_xrgb8888);
                wlc_send(c, popup_xdg, WLQ_xdg_surface_ack_configure, conf ? (long)conf->u[0] : 0L);
                wlc_send(c, popup_surface, WLQ_wl_surface_attach, buffer, 0L, 0L);
                wlc_send(c, popup_surface, WLQ_wl_surface_commit);
        }
        WLCHECK(c->errors == 0 && fake.row[1].used && fake.row[1].kind == WAYLAND_KIND_POPUP && fake.row[1].parent == 0,
                "the popup is a window over its parent (%s)", c->error_text);
        WLCHECK(fake.row[1].x == 4 && fake.row[1].y == 13, "at the place it was told (%ld %ld)", fake.row[1].x, fake.row[1].y);
        {
                unsigned long pos2 = wlc_id(c, WLI_xdg_positioner, 6);

                wlc_forget(c);
                wlc_send(c, s.wm_base, WLQ_xdg_wm_base_create_positioner, pos2);
                wlc_send(c, pos2, WLQ_xdg_positioner_set_size, 40L, 30L);
                wlc_send(c, pos2, WLQ_xdg_positioner_set_anchor_rect, 20L, 30L, 4L, 4L);
                wlc_send(c, pos2, WLQ_xdg_positioner_set_anchor, (long)WLN_xdg_positioner_anchor_bottom_right);
                wlc_send(c, pos2, WLQ_xdg_positioner_set_gravity, (long)WLN_xdg_positioner_gravity_bottom_right);
                wlc_send(c, popup, WLQ_xdg_popup_reposition, pos2, 42L);
                e = wlc_take(c, WLI_xdg_popup, WLE_xdg_popup_repositioned);
                WLCHECK(e && e->u[0] == 42 && fake.row[1].x == 24 && fake.row[1].y == 34,
                        "reposition updates an already-mapped popup's position (%ld %ld)", fake.row[1].x, fake.row[1].y);
        }
        wlc_forget(c);
        fake.row[1].closing = 1;
        wayland_row_wake(1);
        wlc_pump(c);
        WLCHECK(wlc_count(c, WLI_xdg_popup, WLE_xdg_popup_popup_done) == 1, "a dismissed popup is sent popup_done");

        /* the parent cannot be destroyed under it without the popup going: surface destroy takes all */
        wlc_send(c, popup, WLQ_xdg_popup_destroy);
        WLCHECK(c->errors == 0 && !fake.row[1].used, "a popup destroyed takes its window (%s)", c->error_text);

        /* an incomplete positioner, a parent that is nothing, a grab after it is shown */
        {
                unsigned long bare = wlc_id(c, WLI_xdg_positioner, 6), surf = wlc_id(c, WLI_wl_surface, 6), xs = wlc_id(c, WLI_xdg_surface, 6),
                              pop = wlc_id(c, WLI_xdg_popup, 6);

                wlc_send(c, s.wm_base, WLQ_xdg_wm_base_create_positioner, bare);
                wlc_send(c, s.compositor, WLQ_wl_compositor_create_surface, surf);
                wlc_send(c, s.wm_base, WLQ_xdg_wm_base_get_xdg_surface, xs, surf);
                wlc_send(c, xs, WLQ_xdg_surface_get_popup, pop, s.xdg, bare);
                WLCHECK(refused_with(c, WLN_xdg_wm_base_error_invalid_positioner), "a positioner with no size is refused");
        }
        wlc_end(c);
        leak_check("popups");
        wayland_stop();
}

static void test_surface_rules(void)
{
        struct session s;
        struct wlc *c;

        wl_reset();
        wayland_start(&fake_host);
        session_open(&s);
        c = s.c;
        s.surface = wlc_id(c, WLI_wl_surface, 6);
        wlc_send(c, s.compositor, WLQ_wl_compositor_create_surface, s.surface);
        wlc_send(c, s.surface, WLQ_wl_surface_set_buffer_scale, 0L);
        WLCHECK(refused_with(c, WLN_wl_surface_error_invalid_scale), "scale 0");
        wlc_end(c);
        session_open(&s);
        c = s.c;
        s.surface = wlc_id(c, WLI_wl_surface, 6);
        wlc_send(c, s.compositor, WLQ_wl_compositor_create_surface, s.surface);
        wlc_send(c, s.surface, WLQ_wl_surface_set_buffer_transform, 9L);
        WLCHECK(refused_with(c, WLN_wl_surface_error_invalid_transform), "transform 9");
        wlc_end(c);
        session_open(&s);
        c = s.c;
        s.surface = wlc_id(c, WLI_wl_surface, 6);
        wlc_send(c, s.compositor, WLQ_wl_compositor_create_surface, s.surface);
        wlc_send(c, s.surface, WLQ_wl_surface_attach, 0UL, 3L, 0L);
        WLCHECK(refused_with(c, WLN_wl_surface_error_invalid_offset), "attach with an offset in version 6");
        wlc_end(c);
        /* a second role for a surface, an xdg_surface on a surface with a buffer */
        session_open(&s);
        session_window(&s, 0xff000000);
        c = s.c;
        {
                unsigned long again = wlc_id(c, WLI_xdg_surface, 6);

                wlc_send(c, s.wm_base, WLQ_xdg_wm_base_get_xdg_surface, again, s.surface);
                WLCHECK(refused_with(c, WLN_xdg_wm_base_error_role), "a second xdg_surface for a surface");
        }
        wlc_end(c);
        /* geometry that is not positive, a min above a max */
        session_open(&s);
        session_window(&s, 0xff000000);
        c = s.c;
        wlc_send(c, s.xdg, WLQ_xdg_surface_set_window_geometry, 0L, 0L, 0L, 10L);
        WLCHECK(refused_with(c, WLN_xdg_surface_error_invalid_size), "window geometry of no width");
        wlc_end(c);
        session_open(&s);
        session_window(&s, 0xff000000);
        c = s.c;
        wlc_send(c, s.toplevel, WLQ_xdg_toplevel_set_min_size, 100L, 100L);
        wlc_send(c, s.toplevel, WLQ_xdg_toplevel_set_max_size, 50L, 50L);
        wlc_send(c, s.surface, WLQ_wl_surface_commit);
        WLCHECK(refused_with(c, WLN_xdg_toplevel_error_invalid_size), "a minimum above the maximum, at the commit");
        wlc_end(c);
        session_open(&s);
        session_window(&s, 0xff000000);
        c = s.c;
        wlc_send(c, s.toplevel, WLQ_xdg_toplevel_set_min_size, -1L, 10L);
        WLCHECK(refused_with(c, WLN_xdg_toplevel_error_invalid_size), "a negative minimum");
        wlc_end(c);
        /* a parent that is the window itself, or its child */
        session_open(&s);
        session_window(&s, 0xff000000);
        c = s.c;
        wlc_send(c, s.toplevel, WLQ_xdg_toplevel_set_parent, s.toplevel);
        WLCHECK(refused_with(c, WLN_xdg_toplevel_error_invalid_parent), "a toplevel is not its own parent");
        wlc_end(c);
        /* a resize edge that does not exist */
        session_open(&s);
        session_window(&s, 0xff000000);
        c = s.c;
        wlc_send(c, s.toplevel, WLQ_xdg_toplevel_resize, s.seat, 1L, 3L);
        WLCHECK(refused_with(c, WLN_xdg_toplevel_error_invalid_resize_edge), "resize edge 3");
        wlc_end(c);
        /* set_maximized and the others reach the window, and before it is shown they wait for it */
        session_open(&s);
        c = s.c;
        s.surface = wlc_id(c, WLI_wl_surface, 6);
        wlc_send(c, s.compositor, WLQ_wl_compositor_create_surface, s.surface);
        s.xdg = wlc_id(c, WLI_xdg_surface, 6);
        wlc_send(c, s.wm_base, WLQ_xdg_wm_base_get_xdg_surface, s.xdg, s.surface);
        s.toplevel = wlc_id(c, WLI_xdg_toplevel, 6);
        wlc_send(c, s.xdg, WLQ_xdg_surface_get_toplevel, s.toplevel);
        wlc_send(c, s.toplevel, WLQ_xdg_toplevel_set_maximized);
        wlc_send(c, s.surface, WLQ_wl_surface_commit);
        {
                struct wl_event_seen *conf = wlc_take(c, WLI_xdg_surface, WLE_xdg_surface_configure);
                unsigned pixels[16];
                long fd = wl_file(64, 0);
                unsigned long pool = wlc_id(c, WLI_wl_shm_pool, 1), buffer = wlc_id(c, WLI_wl_buffer, 1);

                memset(pixels, 0, sizeof pixels);
                wl_file_put(fd, 0, pixels, sizeof pixels);
                wlc_send(c, s.shm, WLQ_wl_shm_create_pool, pool, fd, 64L);
                close((int)fd);
                wlc_send(c, pool, WLQ_wl_shm_pool_create_buffer, buffer, 0L, 4L, 4L, 16L, (long)WLN_wl_shm_format_xrgb8888);
                wlc_send(c, s.xdg, WLQ_xdg_surface_ack_configure, conf ? (long)conf->u[0] : 0L);
                wlc_send(c, s.surface, WLQ_wl_surface_attach, buffer, 0L, 0L);
                wlc_send(c, s.surface, WLQ_wl_surface_commit);
        }
        WLCHECK(fake.row[0].did_count && fake.row[0].did[0] == WAYLAND_DO_MAXIMIZE, "maximize asked before the window was shown is done when it is");
        wlc_send(c, s.toplevel, WLQ_xdg_toplevel_set_fullscreen, 0UL);
        wlc_send(c, s.toplevel, WLQ_xdg_toplevel_unset_fullscreen);
        wlc_send(c, s.toplevel, WLQ_xdg_toplevel_set_minimized);
        WLCHECK(fake.row[0].did_count >= 4, "and later requests reach it at once");
        wlc_end(c);
        leak_check("surface rules");
        wayland_stop();
}

/* Everything a client may send that is not a request: sizes, ids, objects that are not there, bytes that end. */
static void test_wire(void)
{
        struct wlc *c;
        unsigned char b[64];
        long fds[2];

        wl_reset();
        wayland_start(&fake_host);

        /* a word of the wire is four bytes, the low first, read and written at any alignment, the bytes beside it untouched; an
           event's size is the high half of its second word, its opcode the low half */
        for (int offset = 0; offset < 4; offset++)
        {
                memset(b, 0xee, sizeof b);
                wayland_put(b + offset, 0x11223344UL);
                WLCHECK(b[offset] == 0x44 && b[offset + 1] == 0x33 && b[offset + 2] == 0x22 && b[offset + 3] == 0x11 &&
                            (offset == 0 || b[offset - 1] == 0xee) && b[offset + 4] == 0xee && wayland_get(b + offset) == 0x11223344UL,
                        "a word put at offset %d is its four bytes, low first, and is read back", offset);
        }
        {
                static struct wayland_client one;
                static unsigned char queue[WAYLAND_OUT];
                struct wayland_event e;

                memset(&one, 0, sizeof one);
                one.slot = -1;
                one.out = queue;
                one.out_room = sizeof queue;
                e = wayland_begin(&one, 0x01020304UL, 0x0506);
                wayland_word(&e, 0xa0b0c0d0UL);
                wayland_end(&e);
                WLCHECK(one.out_length == 12 && wayland_get(one.out) == 0x01020304UL && wayland_get(one.out + 4) == (0x0506UL | 12UL << 16) &&
                            wayland_get(one.out + 8) == 0xa0b0c0d0UL,
                        "an event is its object, its opcode with its size above it, and its words (%ld)", one.out_length);
        }

        /* a send that took part of the queue: the rest moved up in its order and the slot still to flush; the rest sent, the queue
           empty and the slot clean */
        {
                struct wlc *p = wlc_open();
                struct wayland_client *pc = wayland.client[p->slot];
                const unsigned char *bytes;
                const long *out_fds;
                long fd_count, whole, rest;
                unsigned int word = 7;

                wayland_sent(p->slot, wayland_out(p->slot, &bytes, &out_fds, &fd_count));
                wayland_send(pc, 1, WLE_wl_display_delete_id, &word, 1);
                word = 9;
                wayland_send(pc, 1, WLE_wl_display_delete_id, &word, 1);
                whole = wayland_out(p->slot, &bytes, &out_fds, &fd_count);
                wayland_sent(p->slot, 12);
                rest = wayland_out(p->slot, &bytes, &out_fds, &fd_count);
                WLCHECK(whole == 24 && rest == 12 && wayland_get(bytes) == 1 && wayland_get(bytes + 8) == 9 && wayland_out_dirty(p->slot),
                        "a send of part of the queue leaves the rest, moved up, still to flush (%ld %ld)", whole, rest);
                wayland_sent(p->slot, rest);
                WLCHECK(wayland_out(p->slot, &bytes, &out_fds, &fd_count) == 0 && !wayland_out_dirty(p->slot),
                        "and a send of all of it leaves the queue empty and the slot clean");
                wlc_end(p);
        }

        /* units of a fixed number: at the scale of one a pixel is 256, with no division; at any other the division, rounded to zero */
        {
                long scales[] = {WAYLAND_UNIT, 0, -3, 180, 240, 150, 90, 960, 30};
                long pixels[] = {0, 1, -1, 7, -7, 30, -30, 1919, 1L << 20, -(1L << 20)};
                int good = 1;

                for (unsigned s = 0; s < sizeof scales / sizeof scales[0]; s++)
                        for (unsigned p = 0; p < sizeof pixels / sizeof pixels[0]; p++)
                        {
                                long scale = scales[s] > 0 ? scales[s] : WAYLAND_UNIT;

                                good &= wayland_units_fixed(pixels[p], scales[s]) == (unsigned long)(pixels[p] * 256 * WAYLAND_UNIT / scale) &&
                                        wayland_units(pixels[p], scales[s]) == pixels[p] * WAYLAND_UNIT / scale;
                        }
                WLCHECK(good, "pixels to units, fixed and whole, are the division at every scale (none, one, a fraction, a whole)");
        }

        /* a header whose size is below eight, above the largest, or not a multiple of four */
        for (int size = 0; size < 5; size++)
        {
                long sizes[5] = {0, 7, 9, WAYLAND_MESSAGE + 4, 0xffff};

                c = wlc_open();
                memset(b, 0, sizeof b);
                wlc_put(b, 1);
                wlc_put(b + 4, (unsigned long)sizes[size] << 16);
                wayland_feed(c->slot, b, 8, 0, 0);
                wlc_pump(c);
                WLCHECK(c->errors == 1, "a message of size %ld is a protocol error", sizes[size]);
                WLCHECK(wayland_dead(c->slot) == 1, "and the client is dead");
                WLCHECK(wayland_feed(c->slot, b, 8, 0, 0) < 0, "and nothing more is read");
                wlc_end(c);
        }

        /* the display: a request that is not one, an object that is not there, a bad id */
        c = wlc_open();
        memset(b, 0, sizeof b);
        wlc_put(b, 1);
        wlc_put(b + 4, 9UL | 8UL << 16);
        wayland_feed(c->slot, b, 8, 0, 0);
        wlc_pump(c);
        WLCHECK(c->errors == 1 && c->error_code == WLN_wl_display_error_invalid_method, "an opcode that does not exist");
        wlc_end(c);
        c = wlc_open();
        memset(b, 0, sizeof b);
        wlc_put(b, 777);
        wlc_put(b + 4, 0UL | 8UL << 16);
        wayland_feed(c->slot, b, 8, 0, 0);
        wlc_pump(c);
        WLCHECK(c->errors == 1 && c->error_code == WLN_wl_display_error_invalid_object && c->error_object == 777, "an object that is not there");
        wlc_end(c);
        c = wlc_open();
        wlc_send(c, 1, WLQ_wl_display_get_registry, 0UL);
        WLCHECK(c->errors == 1, "a new id of 0");
        wlc_end(c);
        c = wlc_open();
        wlc_send(c, 1, WLQ_wl_display_get_registry, (unsigned long)WAYLAND_IDS);
        WLCHECK(c->errors == 1, "a new id past the table");
        wlc_end(c);
        c = wlc_open();
        wlc_send(c, 1, WLQ_wl_display_get_registry, 0xff000001UL);
        WLCHECK(c->errors == 1, "a new id in the server's range");
        wlc_end(c);
        c = wlc_open();
        c->iface[2] = WLI_wl_registry;
        wlc_send(c, 1, WLQ_wl_display_get_registry, 2UL);
        c->iface[3] = WLI_wl_registry;
        wlc_send(c, 1, WLQ_wl_display_get_registry, 2UL);
        WLCHECK(c->errors == 1, "a new id already in use");
        wlc_end(c);

        /* a message cut at every byte arrives whole */
        c = wlc_open();
        {
                unsigned long registry = wlc_id(c, WLI_wl_registry, 1);
                unsigned char m[12];

                memset(m, 0, sizeof m);
                wlc_put(m, 1);
                wlc_put(m + 4, 1UL | 12UL << 16);
                wlc_put(m + 8, registry);
                for (int cut = 1; cut < 12; cut++)
                {
                        wayland_feed(c->slot, m, cut, 0, 0);
                        wayland_feed(c->slot, m + cut, 12 - cut, 0, 0);
                        wlc_pump(c);
                        WLCHECK(c->errors == 0, "a message split at byte %d is read once whole", cut);
                        c->iface[registry] = 0;
                        registry = wlc_id(c, WLI_wl_registry, 1);
                        wlc_put(m + 8, registry);
                }
        }
        wlc_end(c);

        /* a request with bytes left over, or fewer than it needs */
        c = wlc_open();
        memset(b, 0, sizeof b);
        wlc_put(b, 1);
        wlc_put(b + 4, 1UL | 16UL << 16);
        wlc_put(b + 8, 2);
        wlc_put(b + 12, 0xdead);
        wayland_feed(c->slot, b, 16, 0, 0);
        wlc_pump(c);
        WLCHECK(c->errors == 1, "bytes left over after the arguments");
        wlc_end(c);
        c = wlc_open();
        memset(b, 0, sizeof b);
        wlc_put(b, 1);
        wlc_put(b + 4, 1UL | 8UL << 16);
        wayland_feed(c->slot, b, 8, 0, 0);
        wlc_pump(c);
        WLCHECK(c->errors == 1, "an argument missing");
        wlc_end(c);

        /* a string with no NUL, or longer than its message */
        {
                struct session s;
                unsigned char m[64];

                session_open(&s);
                c = s.c;
                memset(m, 'x', sizeof m);
                wlc_put(m, s.registry);
                wlc_put(m + 4, 0UL | 32UL << 16);
                wlc_put(m + 8, 1);
                wlc_put(m + 12, 8);
                memset(m + 16, 'a', 8);
                wlc_put(m + 24, 1);
                wlc_put(m + 28, 99);
                wayland_feed(c->slot, m, 32, 0, 0);
                wlc_pump(c);
                WLCHECK(c->errors == 1, "a string that is not ended");
                wlc_end(c);
                session_open(&s);
                c = s.c;
                wlc_put(m, s.registry);
                wlc_put(m + 4, 0UL | 28UL << 16);
                wlc_put(m + 8, 1);
                wlc_put(m + 12, 4000);
                wayland_feed(c->slot, m, 28, 0, 0);
                wlc_pump(c);
                WLCHECK(c->errors == 1, "a string longer than its message");
                wlc_end(c);
        }

        /* a bind of the wrong interface, an unknown global, a version that is not there */
        {
                struct session s;

                session_open(&s);
                c = s.c;
                wlc_send(c, s.registry, WLQ_wl_registry_bind, 1UL, "wl_shm", 1L, wlc_id(c, WLI_wl_shm, 1));
                WLCHECK(c->errors == 1, "a global bound as another interface");
                wlc_end(c);
                session_open(&s);
                c = s.c;
                wlc_send(c, s.registry, WLQ_wl_registry_bind, 999UL, "wl_shm", 1L, wlc_id(c, WLI_wl_shm, 1));
                WLCHECK(c->errors == 1, "a global that does not exist");
                wlc_end(c);
                session_open(&s);
                c = s.c;
                wlc_send(c, s.registry, WLQ_wl_registry_bind, 1UL, "wl_compositor", 99L, wlc_id(c, WLI_wl_compositor, 1));
                WLCHECK(c->errors == 1, "a version above the one built");
                wlc_end(c);
                session_open(&s);
                c = s.c;
                wlc_send(c, s.registry, WLQ_wl_registry_bind, 1UL, "wl_compositor", 0L, wlc_id(c, WLI_wl_compositor, 1));
                WLCHECK(c->errors == 1, "version 0");
                wlc_end(c);
                /* an older version binds, and sends no event it does not have */
                session_open(&s);
                c = s.c;
                {
                        unsigned long out = wlc_id(c, WLI_wl_output, 1);

                        for (long at = 0; at < c->events; at++)
                                if (c->ev[at].iface == WLI_wl_registry && !strcmp(c->ev[at].text[1], "wl_output"))
                                        wlc_send(c, s.registry, WLQ_wl_registry_bind, c->ev[at].u[0], "wl_output", 1L, out);
                        WLCHECK(c->errors == 0 && c->malformed == 0, "wl_output bound at version 1 (%s)", c->error_text);
                }
                wlc_end(c);
        }

        /* a request of an object on another interface's object: a surface where a buffer goes */
        {
                struct session s;

                session_open(&s);
                c = s.c;
                s.surface = wlc_id(c, WLI_wl_surface, 6);
                wlc_send(c, s.compositor, WLQ_wl_compositor_create_surface, s.surface);
                {
                        /* an attach whose buffer argument is the surface itself: encoded by hand, since the table would not */
                        unsigned char m[24];

                        memset(m, 0, sizeof m);
                        wlc_put(m, s.surface);
                        wlc_put(m + 4, (unsigned long)WLQ_wl_surface_attach | 20UL << 16);
                        wlc_put(m + 8, s.surface);
                        wayland_feed(c->slot, m, 20, 0, 0);
                        wlc_pump(c);
                        WLCHECK(c->errors == 1, "an object of the wrong interface");
                }
                wlc_end(c);
        }

        /* descriptors: a request that needs one with none sent, and more than the cap */
        {
                struct session s;

                session_open(&s);
                c = s.c;
                {
                        unsigned char m[20];

                        memset(m, 0, sizeof m);
                        wlc_put(m, s.shm);
                        wlc_put(m + 4, (unsigned long)WLQ_wl_shm_create_pool | 16UL << 16);
                        wlc_put(m + 8, wlc_id(c, WLI_wl_shm_pool, 1));
                        wlc_put(m + 12, 4096);
                        wayland_feed(c->slot, m, 16, 0, 0);
                        wlc_pump(c);
                        WLCHECK(c->errors == 1, "a request without the descriptor it needs");
                }
                wlc_end(c);
                session_open(&s);
                c = s.c;
                {
                        unsigned char m[8];

                        for (int at = 0; at < WAYLAND_FDS + 3; at++)
                        {
                                fds[0] = wl_file(16, 0);
                                memset(m, 0, sizeof m);
                                wlc_put(m, 1);
                                wlc_put(m + 4, 3UL | 8UL << 16);
                                (void)m;
                                wayland_feed(c->slot, 0, 0, fds, 1);
                        }
                        wlc_pump(c);
                        WLCHECK(c->errors == 1 && wayland_dead(c->slot), "more descriptors than the cap is the client's death");
                }
                wlc_end(c);
        }
        leak_check("wire");
        WLCHECK(fds_balanced(), "every descriptor was closed (%ld %ld)", fake.fds_open, fake.fds_closed);
        wayland_stop();
}

/*
        The tables grow: a client makes as many objects and surfaces as memory lets it, and the one the memory is not there for (a
        commit of the table refused, as the kernel refuses one) is refused with no_memory, that client alone, and what it held comes
        back. Each table's growth is refused in its turn: an injected failure at the first commit after the table's last, at every one.
*/
static void test_caps(void)
{
        struct session s;
        struct wlc *c;
        long made = 0;

        wl_reset();
        wayland_start(&fake_host);
        session_open(&s);
        c = s.c;
        while (!c->errors && made < 20000)
        {
                wlc_send(c, s.compositor, WLQ_wl_compositor_create_region, wlc_id(c, WLI_wl_region, 1));
                made++;
        }
        WLCHECK(c->errors == 0, "twenty thousand objects in one client, past the old cap of 2048 (%s)", c->error_text);
        wlc_end(c);

        session_open(&s);
        c = s.c;
        for (int at = 0; at < 10000 && !c->errors; at++)
                wlc_send(c, s.compositor, WLQ_wl_compositor_create_surface, wlc_id(c, WLI_wl_surface, 6));
        WLCHECK(c->errors == 0, "ten thousand surfaces in one client, past the old cap of 64 (%s)", c->error_text);
        // A second client beside it, made while the first's tables are large: its objects are its own, and both are answered.
        {
                struct session t;

                session_open(&t);
                wlc_send(t.c, t.compositor, WLQ_wl_compositor_create_surface, wlc_id(t.c, WLI_wl_surface, 6));
                wlc_pump(t.c);
                WLCHECK(t.c->errors == 0, "a second client beside one of ten thousand surfaces is answered");
                wlc_end(t.c);
        }
        // The memory runs out: the next surface (its object's table, or its own) is refused with no_memory, and the client alone.
        fake.commit_fail_at = 1;
        for (int at = 0; at < 100000 && !c->errors; at++)
                wlc_send(c, s.compositor, WLQ_wl_compositor_create_surface, wlc_id(c, WLI_wl_surface, 6));
        fake.commit_fail_at = 0;
        WLCHECK(c->errors == 1 && c->error_code == WLN_wl_display_error_no_memory, "a surface the memory is not there for is refused with no_memory (%lu)",
                c->error_code);
        wlc_end(c);
        leak_check("growth");

        // A failure injected at every growth point: the commit that fails is the 1st, 2nd, ... of a client that makes pools, buffers,
        // surfaces, subsurfaces, regions and positioners; each run ends with a refusal or with all of it made, and gives everything back.
        for (long fail = 1; fail <= 40; fail++)
        {
                long refused = 0;

                session_open(&s);
                c = s.c;
                fake.commit_fail_at = fail;
                {
                        unsigned long sub = wl_global(c, s.registry, WLI_wl_subcompositor, 1), wm = wl_global(c, s.registry, WLI_xdg_wm_base, 1);

                        for (int round = 0; round < 300 && !c->errors; round++)
                        {
                                unsigned long surface = wlc_id(c, WLI_wl_surface, 6), child = wlc_id(c, WLI_wl_surface, 6), region = wlc_id(c, WLI_wl_region, 1);
                                long fd = wl_file(4096, 0);
                                unsigned long pool = wlc_id(c, WLI_wl_shm_pool, 1);

                                wlc_send(c, s.compositor, WLQ_wl_compositor_create_surface, surface);
                                wlc_send(c, s.compositor, WLQ_wl_compositor_create_surface, child);
                                wlc_send(c, sub, WLQ_wl_subcompositor_get_subsurface, wlc_id(c, WLI_wl_subsurface, 1), child, surface);
                                wlc_send(c, s.compositor, WLQ_wl_compositor_create_region, region);
                                wlc_send(c, wm, WLQ_xdg_wm_base_create_positioner, wlc_id(c, WLI_xdg_positioner, 1));
                                wlc_send(c, s.shm, WLQ_wl_shm_create_pool, pool, fd, 4096L);
                                close((int)fd);
                                wlc_send(c, pool, WLQ_wl_shm_pool_create_buffer, wlc_id(c, WLI_wl_buffer, 1), 0L, 16L, 16L, 64L, 0L);
                        }
                        wlc_pump(c);
                        refused = c->errors && c->error_code == WLN_wl_display_error_no_memory;
                        WLCHECK(!c->errors || refused, "growth point %ld: a refusal is no_memory (%lu: %s)", fail, c->error_code, c->error_text);
                }
                fake.commit_fail_at = 0;
                wlc_end(c);
                (void)refused;
                leak_check("growth point");
        }

        /* the client slots: as many as connect, grown a slot at a time (no count of clients is a cap), given back with the last */
        {
                static long slots[200];
                long got = 0, distinct = 1;

                for (int at = 0; at < 200; at++)
                {
                        slots[at] = wayland_connect(1, 1);
                        got += slots[at] >= 0;
                        distinct &= slots[at] == at;
                }
                WLCHECK(got == 200 && distinct && wayland_client_slots() == 200, "two hundred clients each have a slot of their own (%ld, %ld slots)", got,
                        wayland_client_slots());
                // A client of the 200th slot has its bytes marked to flush past the first word of bits.
                wayland_out_mark(199);
                WLCHECK(wayland_out_dirty(199) && !wayland_out_dirty(198) && !wayland_out_dirty(63), "a slot past 64 has its own bit to flush");
                wayland_out_clear(199);
                for (int at = 0; at < 200; at++)
                        wayland_drop(slots[at]);
                WLCHECK(wayland_client_slots() == 0, "the slots are given back with the last client");
                leak_check("client slots");
        }

        /* a client that never reads is dropped when its queue is full (grown to WAYLAND_OUT_MOST), and nothing blocks */
        session_open(&s);
        c = s.c;
        {
                long rounds = 0;
                unsigned char m[12];

                while (!wayland_dead(c->slot) && rounds < 400000)
                {
                        /* a sync is answered with two events and gives its id back: the queue grows and no object does */
                        memset(m, 0, sizeof m);
                        wlc_put(m, 1);
                        wlc_put(m + 4, (unsigned long)WLQ_wl_display_sync | 12UL << 16);
                        wlc_put(m + 8, 500);
                        wayland_feed(c->slot, m, 12, 0, 0);
                        rounds++;
                }
                WLCHECK(wayland_dead(c->slot) == 2 && c->errors == 0, "a client that does not read is dead, not waited for (after %ld)", rounds);
                WLCHECK(rounds * 24 >= WAYLAND_OUT_MOST - WAYLAND_EVENT - 24 && rounds * 24 <= WAYLAND_OUT_MOST,
                        "after a queue's worth of answers, the most a queue grows to (%ld)", rounds);
        }
        wlc_end(c);
        leak_check("caps");
        wayland_stop();
}

/* A fixed-seed fuzz: every request of every interface with arguments that are valid, nearly valid and wild. */
static unsigned long wl_seed = 0x9e3779b97f4a7c15UL;

static unsigned long wl_random(void)
{
        wl_seed ^= wl_seed << 13;
        wl_seed ^= wl_seed >> 7;
        wl_seed ^= wl_seed << 17;
        return wl_seed;
}

static void test_fuzz(long rounds)
{
        long accepted = 0, refused = 0, malformed = 0;
        struct session s;
        struct wlc *c = 0;
        long made = 0;

        wl_reset();
        wayland_start(&fake_host);
        for (long round = 0; round < rounds; round++)
        {
                if (!c || wayland_dead(c->slot) || round % 400 == 0)
                {
                        if (c)
                        {
                                malformed += c->malformed;
                                wlc_end(c);
                        }
                        session_open(&s);
                        c = s.c;
                        if (wl_random() & 1)
                                session_window(&s, 0xff112233);
                        if (wl_random() & 1)
                        {
                                wlc_send(c, s.seat, WLQ_wl_seat_get_pointer, wlc_id(c, WLI_wl_pointer, 9));
                                wlc_send(c, s.seat, WLQ_wl_seat_get_keyboard, wlc_id(c, WLI_wl_keyboard, 9));
                        }
                        made = 0;
                }
                {
                        /* an object of the client's, and one of its requests, with arguments chosen to be wrong in some way */
                        unsigned char m[256];
                        unsigned long object = wl_random() % 4 ? 1 + wl_random() % (c->next + 4) : 1;
                        long iface = object < WLC_IDS ? c->iface[object] : 0, op, n = 8;
                        const struct wl_interface_info *info;

                        if (!iface)
                                iface = WLI_wl_display, object = 1;
                        info = &wl_interfaces[iface];
                        op = info->requests ? (long)(wl_random() % (info->requests + (wl_random() % 8 == 0))) : 0;
                        memset(m, 0, sizeof m);
                        wlc_put(m, object);
                        if (op < info->requests)
                        {
                                for (const unsigned char *k = info->request[op].args; k[0] && n < 200; k += 2)
                                {
                                        unsigned long v;

                                        switch (wl_random() % 6)
                                        {
                                        case 0: v = 0; break;
                                        case 1: v = 1; break;
                                        case 2: v = 0xffffffffUL; break;
                                        case 3: v = c->next > 2 ? 2 + wl_random() % (c->next - 2) : 2; break;
                                        case 4: v = wl_random() & 0xff; break;
                                        default: v = wl_random() & 0xffffffffUL; break;
                                        }
                                        if (k[0] == 'n' && wl_random() % 4)
                                        {
                                                v = c->next++;
                                                made++;
                                        }
                                        if (k[0] == 'n' && v < WLC_IDS && !c->iface[v])
                                        {
                                                c->iface[v] = k[1];
                                                c->version[v] = c->version[object];
                                        }
                                        if (k[0] == 's' || k[0] == 'S' || k[0] == 'N' || k[0] == 'a')
                                        {
                                                unsigned long len = wl_random() % 24, padded = (len + 3) & ~3UL;

                                                wlc_put(m + n, len);
                                                n += 4;
                                                for (unsigned long at = 0; at < padded; at++)
                                                        m[n + at] = at + 1 < len ? (unsigned char)('a' + wl_random() % 26) : 0;
                                                if (wl_random() % 16 == 0 && len)
                                                        m[n + len - 1] = 'z';
                                                n += (long)padded;
                                                if (k[0] == 'N')
                                                {
                                                        wlc_put(m + n, wl_random() % 4);
                                                        wlc_put(m + n + 4, 2 + wl_random() % 50);
                                                        n += 8;
                                                }
                                        }
                                        else if (k[0] != 'h')
                                        {
                                                wlc_put(m + n, v);
                                                n += 4;
                                        }
                                }
                        }
                        wlc_put(m + 4, (unsigned long)op);
                        /* sometimes the size is wrong */
                        if (wl_random() % 20 == 0)
                                n = 8 + (long)(wl_random() % 16) * 4;
                        {
                                long fds[1], fd_count = 0;

                                if (wl_random() % 3 == 0)
                                {
                                        fds[0] = wl_file(4096, 0);
                                        fd_count = 1;
                                }
                                if (wlc_raw(c, m, n, fds, fd_count) < 0)
                                        refused++;
                                else
                                        accepted++;
                        }
                }
                if (round % 64 == 0 && c && !wayland_dead(c->slot))
                {
                        /* the compositor's side too: wakes and frames, with the window in whatever state it is in */
                        for (int row = 0; row < FAKE_ROWS; row++)
                                if (fake.row[row].used)
                                {
                                        fake.row[row].states = (long)(wl_random() & 0x1f);
                                        fake.row[row].width_asked = (long)(wl_random() % 400);
                                        fake.row[row].closing = (long)(wl_random() % 8 == 0);
                                        wayland_row_wake(row);
                                }
                        wayland_frame();
                        /* and the seat's: a pointer, keys and wheels sent to rows that are there, are gone, or are no row */
                        for (int input = 0; input < 6; input++)
                        {
                                long row = (long)(wl_random() % 36) - 2;

                                switch (wl_random() % 8)
                                {
                                case 0: wayland_pointer_to(row, (long)(wl_random() % 200) - 50, (long)(wl_random() % 200) - 50); break;
                                case 1: wayland_pointer_motion(row, (long)(wl_random() % 300), (long)(wl_random() % 300)); break;
                                case 2: wayland_pointer_button(row, (long)(wl_random() % 0x400) - 8, (long)(wl_random() & 1)); break;
                                case 3: wayland_pointer_axis(row, (long)(wl_random() & 1), (long)(wl_random() % 2000) - 1000, (long)(wl_random() & 1) * WAYLAND_FINGER_NOTCH); break;
                                case 4: wayland_keyboard_to(row); break;
                                case 5: wayland_key(row, (long)(wl_random() % 0x500) - 16, (long)(wl_random() & 1)); break;
                                case 6: fake.locks = (long)(wl_random() & 18); wayland_key(row, 58, (long)(wl_random() & 1)); break;
                                default: wayland_keymap_changed(); break;
                                }
                        }
                        wlc_pump(c);
                }
        }
        if (c)
        {
                malformed += c->malformed;
                wlc_end(c);
        }
        WLCHECK(malformed == 0, "every event of the fuzz was well-formed (%ld malformed)", malformed);
        WLCHECK(accepted > 0 && refused > 0, "the fuzz has accepted and refused requests (%ld, %ld)", accepted, refused);
        leak_check("fuzz");
        WLCHECK(fds_balanced(), "every descriptor of the fuzz was closed (%ld %ld)", fake.fds_open, fake.fds_closed);
        printf("CHECK_wayland fuzz: %ld rounds, %ld accepted, %ld refused, %ld objects made\n", rounds, accepted, refused, made);
        wayland_stop();
}

/* Raw bytes, as a hostile stream is: random, and random with plausible headers. */
static void test_raw(long rounds)
{
        long malformed = 0;
        struct wlc *c = 0;

        wl_reset();
        wayland_start(&fake_host);
        for (long round = 0; round < rounds; round++)
        {
                unsigned char b[256];
                long n = 1 + (long)(wl_random() % 200);

                if (!c || wayland_dead(c->slot))
                {
                        if (c)
                        {
                                wlc_end(c);
                        }
                        c = wlc_open();
                }
                for (long at = 0; at < n; at++)
                        b[at] = (unsigned char)wl_random();
                if (wl_random() % 2 && n >= 8)
                {
                        wlc_put(b, wl_random() % 4 ? 1 : wl_random() % 40);
                        wlc_put(b + 4, (wl_random() % 12) | ((unsigned long)((n & ~3L) > 8 ? n & ~3L : 8) << 16));
                }
                wayland_feed(c->slot, b, n, 0, 0);
                malformed += wlc_pump(c) < 0;
        }
        if (c)
        {
                wlc_end(c);
        }
        WLCHECK(malformed == 0, "raw bytes");
        leak_check("raw");
        wayland_stop();
}


/* A client with a pointer and a keyboard on a window of its own. */
struct seated
{
        struct session s;
        unsigned long pointer, keyboard;
};

static void seated_open(struct seated *t, long pointer_version, long keyboard_version)
{
        unsigned long pointer_seat, keyboard_seat;

        session_open(&t->s);
        pointer_seat = wl_global(t->s.c, t->s.registry, WLI_wl_seat, pointer_version);
        keyboard_seat = wl_global(t->s.c, t->s.registry, WLI_wl_seat, keyboard_version);
        t->pointer = wlc_id(t->s.c, WLI_wl_pointer, pointer_version);
        wlc_send(t->s.c, pointer_seat, WLQ_wl_seat_get_pointer, t->pointer);
        t->keyboard = wlc_id(t->s.c, WLI_wl_keyboard, keyboard_version);
        wlc_send(t->s.c, keyboard_seat, WLQ_wl_seat_get_keyboard, t->keyboard);
        session_window(&t->s, 0xff445566);
        wlc_forget(t->s.c);
}

static void test_seat(void)
{
        struct seated t;
        struct wlc *c;
        struct wl_event_seen *e;

        wl_reset();
        wayland_start(&fake_host);

        /* the keymap and the repeat come with the keyboard, the keymap as a descriptor beside its bytes */
        session_open(&t.s);
        c = t.s.c;
        t.keyboard = wlc_id(c, WLI_wl_keyboard, 9);
        wlc_send(c, t.s.seat, WLQ_wl_seat_get_keyboard, t.keyboard);
        e = wlc_take(c, WLI_wl_keyboard, WLE_wl_keyboard_keymap);
        WLCHECK(e && e->u[0] == WLN_wl_keyboard_keymap_format_xkb_v1 && e->u[1] == 16 && c->fds_seen == 1, "a keyboard is sent its keymap with a descriptor");
        e = wlc_take(c, WLI_wl_keyboard, WLE_wl_keyboard_repeat_info);
        WLCHECK(e && e->u[0] == 25 && e->u[1] == 600, "and how fast a held key repeats");
        /* the settings change it: the keyboard is told the new rate and delay, and not its keymap again */
        wlc_forget(c);
        fake.rate = 40;
        fake.delay = 180;
        wayland_repeat_changed();
        wlc_pump(c);
        e = wlc_take(c, WLI_wl_keyboard, WLE_wl_keyboard_repeat_info);
        WLCHECK(e && e->u[0] == 40 && e->u[1] == 180 && wlc_count(c, WLI_wl_keyboard, WLE_wl_keyboard_keymap) == 0 && c->malformed == 0,
                "a change of the key repeat is told to a keyboard as it is (40 a second after 180 ms), with no keymap");
        wlc_end(c);
        fake.rate = fake.delay = 0;

        /* a client with nine descriptors queued (its socket not read) binds a tenth keyboard: it is sent the xkb keymap with
           its descriptor, and all ten keymaps and their descriptors go out once the socket is read */
        session_open(&t.s);
        c = t.s.c;
        wlc_hold = 1;
        for (long k = 0; k < 10; k++)
        {
                unsigned long keyboard = wlc_id(c, WLI_wl_keyboard, 9);

                wlc_send(c, t.s.seat, WLQ_wl_seat_get_keyboard, keyboard);
        }
        wlc_hold = 0;
        for (long pass = 0; pass < 4; pass++)
                wlc_pump(c);
        {
                long xkb = 0;

                for (long at = 0; at < c->events; at++)
                        xkb += c->ev[at].iface == WLI_wl_keyboard && c->ev[at].opcode == WLE_wl_keyboard_keymap &&
                               c->ev[at].u[0] == WLN_wl_keyboard_keymap_format_xkb_v1;
                WLCHECK(xkb == 10 && c->fds_seen == 10 && c->malformed == 0,
                        "a client with nine descriptors queued is sent the xkb keymap when it binds a tenth keyboard (%ld of 10 xkb, %ld descriptors sent)",
                        xkb, c->fds_seen);
        }
        wlc_end(c);

        /* version 3 knows no repeat_info, and the shim does not send what the version has not got */
        session_open(&t.s);
        c = t.s.c;
        t.keyboard = wlc_id(c, WLI_wl_keyboard, 3);
        wlc_send(c, wl_global(c, t.s.registry, WLI_wl_seat, 3), WLQ_wl_seat_get_keyboard, t.keyboard);
        WLCHECK(wlc_count(c, WLI_wl_keyboard, WLE_wl_keyboard_repeat_info) == 0 && c->malformed == 0, "a version 3 keyboard has no repeat_info");
        /* and a version 3 keyboard is told nothing of the repeat when it changes */
        wlc_forget(c);
        wayland_repeat_changed();
        WLCHECK(wlc_pump(c) == 0 || wlc_count(c, WLI_wl_keyboard, WLE_wl_keyboard_repeat_info) == 0, "a version 3 keyboard is sent no repeat_info when it changes");
        wlc_end(c);

        seated_open(&t, 9, 9);
        c = t.s.c;
        /* the pointer enters the window, moves, presses, wheels and leaves */
        wayland_pointer_to(0, 10, 20);
        wlc_pump(c);
        e = wlc_take(c, WLI_wl_pointer, WLE_wl_pointer_enter);
        WLCHECK(e && e->u[1] == t.s.surface && e->u[2] == 10 * 256 && e->u[3] == 20 * 256, "the pointer enters at its place in the picture");
        WLCHECK(wlc_count(c, WLI_wl_pointer, WLE_wl_pointer_frame) == 1, "an enter ends with a frame");
        wlc_forget(c);
        wayland_pointer_to(0, 10, 20);
        WLCHECK(wlc_pump(c) == 0, "the same window is no event");
        wayland_pointer_motion(0, 11, 22);
        wlc_pump(c);
        e = wlc_take(c, WLI_wl_pointer, WLE_wl_pointer_motion);
        WLCHECK(e && e->u[1] == 11 * 256 && e->u[2] == 22 * 256, "a motion is sent in fixed point");
        wlc_forget(c);
        wayland_pointer_button(0, 0x110, 1);
        wayland_pointer_button(0, 0x110, 0);
        wlc_pump(c);
        e = wlc_take(c, WLI_wl_pointer, WLE_wl_pointer_button);
        WLCHECK(e && e->u[2] == 0x110 && e->u[3] == WLN_wl_pointer_button_state_pressed, "a press");
        e = wlc_take(c, WLI_wl_pointer, WLE_wl_pointer_button);
        WLCHECK(e && e->u[3] == WLN_wl_pointer_button_state_released, "and its release");
        wlc_forget(c);
        wayland_pointer_axis(0, 0, 120, 0);
        wlc_pump(c);
        WLCHECK(wlc_count(c, WLI_wl_pointer, WLE_wl_pointer_axis_source) == 1 && wlc_count(c, WLI_wl_pointer, WLE_wl_pointer_axis_value120) == 1 &&
                    wlc_count(c, WLI_wl_pointer, WLE_wl_pointer_axis) == 1 && wlc_count(c, WLI_wl_pointer, WLE_wl_pointer_axis_discrete) == 0,
                "a notch to version 9 is a source, a value120 and an axis");
        e = wlc_take(c, WLI_wl_pointer, WLE_wl_pointer_axis);
        WLCHECK(e && (long)(int)e->u[2] == 15 * 256, "a notch is 15 units");
        wlc_forget(c);
        wayland_pointer_axis(0, 1, -60, WAYLAND_FINGER_NOTCH);
        wlc_pump(c);
        e = wlc_take(c, WLI_wl_pointer, WLE_wl_pointer_axis_source);
        WLCHECK(e && e->u[0] == WLN_wl_pointer_axis_source_finger && wlc_count(c, WLI_wl_pointer, WLE_wl_pointer_axis_value120) == 0, "two fingers are a finger's smooth value");
        wlc_forget(c);
        wayland_pointer_to(-1, 0, 0);
        wlc_pump(c);
        WLCHECK(wlc_count(c, WLI_wl_pointer, WLE_wl_pointer_leave) == 1, "the pointer leaves");
        wlc_forget(c);
        wayland_pointer_motion(0, 5, 5);
        wayland_pointer_button(0, 0x110, 1);
        WLCHECK(wlc_pump(c) == 0, "no event for a pointer that is not on the window");

        /* the keyboard: an enter with no key held, keys, and the modifiers after the key that changed them */
        wayland_keyboard_to(0);
        wlc_pump(c);
        e = wlc_take(c, WLI_wl_keyboard, WLE_wl_keyboard_enter);
        WLCHECK(e && e->u[1] == t.s.surface, "the keys enter the window");
        WLCHECK(wlc_count(c, WLI_wl_keyboard, WLE_wl_keyboard_modifiers) == 1, "with the modifiers");
        wlc_forget(c);
        wayland_key(0, 30, 1);
        wayland_key(0, 30, 0);
        wlc_pump(c);
        WLCHECK(wlc_count(c, WLI_wl_keyboard, WLE_wl_keyboard_key) == 2 && wlc_count(c, WLI_wl_keyboard, WLE_wl_keyboard_modifiers) == 0, "a letter's press and release change no modifier");
        wlc_forget(c);
        wayland_key(0, 42, 1);
        wlc_pump(c);
        e = wlc_take(c, WLI_wl_keyboard, WLE_wl_keyboard_modifiers);
        WLCHECK(e && e->u[1] == 1 && wlc_count(c, WLI_wl_keyboard, WLE_wl_keyboard_key) == 1, "Shift pressed: its key, then the modifiers with Shift down");
        wlc_forget(c);
        wayland_key(0, 100, 1);
        wlc_pump(c);
        e = wlc_take(c, WLI_wl_keyboard, WLE_wl_keyboard_modifiers);
        WLCHECK(e && e->u[1] == (1 | 128), "AltGr is Mod5");
        wlc_forget(c);
        wayland_key(0, 42, 0);
        wayland_key(0, 100, 0);
        wlc_pump(c);
        e = wlc_take(c, WLI_wl_keyboard, WLE_wl_keyboard_modifiers);
        e = wlc_take(c, WLI_wl_keyboard, WLE_wl_keyboard_modifiers);
        WLCHECK(e && e->u[1] == 0, "and let go again");
        wlc_forget(c);
        fake.locks = 2;
        wayland_key(0, 58, 1);
        wlc_pump(c);
        e = wlc_take(c, WLI_wl_keyboard, WLE_wl_keyboard_modifiers);
        WLCHECK(e && e->u[3] == 2, "Caps Lock is a locked modifier the compositor says");
        wlc_forget(c);
        wayland_key(0, 0, 1);
        wayland_key(0, 0x3000, 1);
        wayland_key(0, -5, 1);
        WLCHECK(wlc_pump(c) == 0, "a key code that is not one is no event");
        wayland_keyboard_to(-1);
        wlc_pump(c);
        WLCHECK(wlc_count(c, WLI_wl_keyboard, WLE_wl_keyboard_leave) == 1, "the keys leave");
        wlc_forget(c);
        wayland_key(0, 30, 1);
        WLCHECK(wlc_pump(c) == 0, "no key to a window that has not the keys");
        wlc_end(c);

        /* a pointer or keyboard made when the window already has the pointer or the keys is told so at once */
        session_open(&t.s);
        session_window(&t.s, 0xff112233);
        c = t.s.c;
        wayland_pointer_to(0, 3, 4);
        wayland_keyboard_to(0);
        wlc_forget(c);
        t.pointer = wlc_id(c, WLI_wl_pointer, 9);
        wlc_send(c, t.s.seat, WLQ_wl_seat_get_pointer, t.pointer);
        WLCHECK(wlc_count(c, WLI_wl_pointer, WLE_wl_pointer_enter) == 1, "a pointer made over the window enters it at once");
        t.keyboard = wlc_id(c, WLI_wl_keyboard, 9);
        wlc_send(c, t.s.seat, WLQ_wl_seat_get_keyboard, t.keyboard);
        WLCHECK(wlc_count(c, WLI_wl_keyboard, WLE_wl_keyboard_enter) == 1, "a keyboard made when the keys are the window's enters it at once");
        /* released, it is told no more */
        wlc_send(c, t.pointer, WLQ_wl_pointer_release);
        wlc_forget(c);
        wayland_pointer_motion(0, 4, 4);
        WLCHECK(wlc_count(c, WLI_wl_pointer, WLE_wl_pointer_motion) == 0 && c->errors == 0, "a released pointer is sent nothing");
        wlc_end(c);

        /* a pointer of version 1 has no frame; of version 5 a notch is discrete steps, the remainder kept */
        seated_open(&t, 1, 9);
        c = t.s.c;
        wayland_pointer_to(0, 1, 1);
        wlc_pump(c);
        WLCHECK(wlc_count(c, WLI_wl_pointer, WLE_wl_pointer_enter) == 1 && wlc_count(c, WLI_wl_pointer, WLE_wl_pointer_frame) == 0 && c->malformed == 0,
                "a version 1 pointer has no frame");
        wlc_end(c);
        seated_open(&t, 5, 9);
        c = t.s.c;
        wayland_pointer_to(0, 1, 1);
        wlc_forget(c);
        wayland_pointer_axis(0, 0, 60, 0);
        wayland_pointer_axis(0, 0, 60, 0);
        wlc_pump(c);
        WLCHECK(wlc_count(c, WLI_wl_pointer, WLE_wl_pointer_axis_discrete) == 1, "half a notch twice is one discrete step to a version 5 pointer (%ld)",
                wlc_count(c, WLI_wl_pointer, WLE_wl_pointer_axis_discrete));
        wlc_end(c);

        /* a move or a resize names the press the client was last sent, and no other */
        seated_open(&t, 9, 9);
        c = t.s.c;
        wayland_pointer_to(0, 5, 5);
        wayland_pointer_button(0, 0x110, 1);
        wlc_pump(c);
        e = wlc_take(c, WLI_wl_pointer, WLE_wl_pointer_button);
        WLCHECK(e != 0, "a press");
        {
                unsigned long serial = e ? e->u[0] : 0;

                fake.row[0].did_count = 0;
                wlc_send(c, t.s.toplevel, WLQ_xdg_toplevel_move, t.s.seat, (long)(serial + 7));
                WLCHECK(fake.row[0].did_count == 0 && c->errors == 0, "a move that names a serial it was never sent is ignored");
                wlc_send(c, t.s.toplevel, WLQ_xdg_toplevel_resize, t.s.seat, (long)(serial + 7), (long)WLN_xdg_toplevel_resize_edge_bottom_right);
                WLCHECK(fake.row[0].did_count == 0, "and so is a resize");
                wlc_send(c, t.s.toplevel, WLQ_xdg_toplevel_move, t.s.seat, (long)serial);
                WLCHECK(fake.row[0].did_count == 1 && fake.row[0].did[0] == WAYLAND_DO_MOVE, "a move that names the press is asked of the window");
                wlc_send(c, t.s.toplevel, WLQ_xdg_toplevel_resize, t.s.seat, (long)serial, (long)WLN_xdg_toplevel_resize_edge_bottom_right);
                WLCHECK(fake.row[0].did_count == 2 && fake.row[0].did[1] == WAYLAND_DO_RESIZE, "and so is a resize");
        }
        wlc_end(c);

        /* a client holds as many pointers as it makes, and each is sent the pointer's events */
        session_open(&t.s);
        c = t.s.c;
        for (int at = 0; at < 40 && !c->errors; at++)
                wlc_send(c, t.s.seat, WLQ_wl_seat_get_pointer, wlc_id(c, WLI_wl_pointer, 9));
        WLCHECK(c->errors == 0, "forty pointers a client is given (%s)", c->errors ? c->error_text : "none refused");
        wlc_end(c);
        /* touch, which the seat does not have */
        session_open(&t.s);
        c = t.s.c;
        wlc_send(c, t.s.seat, WLQ_wl_seat_get_touch, wlc_id(c, WLI_wl_touch, 9));
        WLCHECK(refused_with(c, WLN_wl_seat_error_missing_capability), "a touch from a seat with none");
        wlc_end(c);

        /* the window destroyed while it has the pointer and the keys: nothing is sent, nothing is kept */
        seated_open(&t, 9, 9);
        c = t.s.c;
        wayland_pointer_to(0, 1, 1);
        wayland_keyboard_to(0);
        wlc_send(c, t.s.surface, WLQ_wl_surface_destroy);
        wlc_forget(c);
        wayland_pointer_motion(0, 2, 2);
        wayland_key(0, 30, 1);
        WLCHECK(wlc_pump(c) == 0 && c->errors == 0, "a destroyed window is sent no input");
        wlc_end(c);

        /* two clients: the pointer leaves one and enters the other */
        {
                struct seated a, b;

                seated_open(&a, 9, 9);
                seated_open(&b, 9, 9);
                wayland_pointer_to(0, 1, 1);
                wlc_forget(a.s.c);
                wlc_forget(b.s.c);
                wayland_pointer_to(1, 2, 2);
                wlc_pump(a.s.c);
                wlc_pump(b.s.c);
                WLCHECK(wlc_count(a.s.c, WLI_wl_pointer, WLE_wl_pointer_leave) == 1 && wlc_count(b.s.c, WLI_wl_pointer, WLE_wl_pointer_enter) == 1,
                        "the pointer leaves one client and enters the other");
                wayland_keyboard_to(0);
                wayland_keyboard_to(1);
                wlc_pump(a.s.c);
                wlc_pump(b.s.c);
                WLCHECK(wlc_count(a.s.c, WLI_wl_keyboard, WLE_wl_keyboard_leave) == 1 && wlc_count(b.s.c, WLI_wl_keyboard, WLE_wl_keyboard_enter) == 1,
                        "and so the keys");
                /* a client gone while it has them */
                wlc_end(b.s.c);
                wayland_pointer_motion(1, 3, 3);
                WLCHECK(wlc_pump(a.s.c) == 0, "a client that goes with the pointer leaves nobody told");
                wlc_end(a.s.c);
        }
        leak_check("seat");
        wayland_stop();
}


/* A buffer of one colour, on a pool of its own: its id. */
static unsigned long wl_buffer_of(struct session *t, long width, long height, unsigned colour, long format)
{
        struct wlc *c = t->c;
        long bytes = width * height * 4, fd = wl_file(bytes, 0);
        unsigned long pool = wlc_id(c, WLI_wl_shm_pool, 1), buffer = wlc_id(c, WLI_wl_buffer, 1);
        unsigned *pixels = malloc((size_t)bytes);

        for (long at = 0; at < width * height; at++)
                pixels[at] = colour;
        wl_file_put(fd, 0, pixels, bytes);
        free(pixels);
        wlc_send(c, t->shm, WLQ_wl_shm_create_pool, pool, fd, bytes);
        close((int)fd);
        wlc_send(c, pool, WLQ_wl_shm_pool_create_buffer, buffer, 0L, width, height, width * 4, format);
        wlc_send(c, pool, WLQ_wl_shm_pool_destroy);
        return buffer;
}

struct tree
{
        struct session s;
        unsigned long subcompositor, child, sub;
};

/* A window of 32 by 24 in 0xff202020 with a subsurface of 8 by 8 in 0xff00ff00 at (4, 4), made and committed in step. */
static void tree_open(struct tree *t, int desync)
{
        struct wlc *c;
        unsigned long buffer;

        session_open(&t->s);
        c = t->s.c;
        t->subcompositor = wl_global(c, t->s.registry, WLI_wl_subcompositor, 1);
        session_window(&t->s, 0xff202020);
        t->child = wlc_id(c, WLI_wl_surface, 6);
        wlc_send(c, t->s.compositor, WLQ_wl_compositor_create_surface, t->child);
        t->sub = wlc_id(c, WLI_wl_subsurface, 1);
        wlc_send(c, t->subcompositor, WLQ_wl_subcompositor_get_subsurface, t->sub, t->child, t->s.surface);
        wlc_send(c, t->sub, WLQ_wl_subsurface_set_position, 4L, 4L);
        if (desync)
                wlc_send(c, t->sub, WLQ_wl_subsurface_set_desync);
        buffer = wl_buffer_of(&t->s, 8, 8, 0xff00ff00, WLN_wl_shm_format_xrgb8888);
        wlc_send(c, t->child, WLQ_wl_surface_attach, buffer, 0L, 0L);
        wlc_send(c, t->child, WLQ_wl_surface_damage_buffer, 0L, 0L, 8L, 8L);
        wlc_send(c, t->child, WLQ_wl_surface_commit);
}

/* How many releases the client was sent for one of its buffers. */
static long releases_of(struct wlc *c, unsigned long object)
{
        long count = 0;

        for (long at = 0; at < c->events; at++)
                count += c->ev[at].object == object && c->ev[at].opcode == WLE_wl_buffer_release;
        return count;
}

/*
        A window's own buffer that it commits again while a subsurface is laid over it is its picture still: it is not released
        (the window's commit lets go of nothing it still shows). Once the subsurface is gone the same buffer's commit releases it
        once, not twice. A subsurface set to the place it has is no move: its window is not laid again (only its own damage is copied).
*/
static void test_holds(void)
{
        struct tree t;
        struct wlc *c;

        wl_reset();
        wayland_start(&fake_host);
        tree_open(&t, 1);
        c = t.s.c;
        wlc_send(c, t.s.surface, WLQ_wl_surface_attach, t.s.buffer, 0L, 0L);
        wlc_send(c, t.s.surface, WLQ_wl_surface_damage_buffer, 0L, 0L, 32L, 24L);
        wlc_send(c, t.s.surface, WLQ_wl_surface_commit);
        wlc_forget(c);
        wlc_send(c, t.s.surface, WLQ_wl_surface_attach, t.s.buffer, 0L, 0L);
        wlc_send(c, t.s.surface, WLQ_wl_surface_damage_buffer, 0L, 0L, 32L, 24L);
        wlc_send(c, t.s.surface, WLQ_wl_surface_commit);
        WLCHECK(releases_of(c, t.s.buffer) == 0 && c->errors == 0, "a window's buffer committed again under a subsurface is not let go (%ld)",
                releases_of(c, t.s.buffer));
        wlc_forget(c);
        wlc_send(c, t.sub, WLQ_wl_subsurface_destroy);
        wlc_send(c, t.s.surface, WLQ_wl_surface_attach, t.s.buffer, 0L, 0L);
        wlc_send(c, t.s.surface, WLQ_wl_surface_damage_buffer, 0L, 0L, 32L, 24L);
        wlc_send(c, t.s.surface, WLQ_wl_surface_commit);
        WLCHECK(releases_of(c, t.s.buffer) == 1 && c->errors == 0, "then, with the subsurface gone, it is released once (%ld)", releases_of(c, t.s.buffer));
        wlc_end(c);

        tree_open(&t, 1);
        c = t.s.c;
        wlc_send(c, t.s.surface, WLQ_wl_surface_attach, t.s.buffer, 0L, 0L);
        wlc_send(c, t.s.surface, WLQ_wl_surface_damage_buffer, 0L, 0L, 32L, 24L);
        wlc_send(c, t.s.surface, WLQ_wl_surface_commit);
        fake.copied = 0;
        for (int at = 0; at < 10; at++)
        {
                wlc_send(c, t.sub, WLQ_wl_subsurface_set_position, 4L, 4L);
                wlc_send(c, t.child, WLQ_wl_surface_commit);
        }
        WLCHECK(fake.copied == 0 && fake.row[0].pixels[5 * FAKE_W + 5] == 0xff00ff00 && c->errors == 0,
                "a subsurface set to its own place, with nothing new, copies nothing (%ld copied)", fake.copied);
        wlc_end(c);
}

static void test_subsurfaces(void)
{
        struct tree t;
        struct wlc *c;
        unsigned long buffer;

        wl_reset();
        wayland_start(&fake_host);

        /* synchronized: shown with the parent's commit, and not before */
        tree_open(&t, 0);
        c = t.s.c;
        WLCHECK(c->errors == 0, "a subsurface is made (%s)", c->error_text);
        WLCHECK(fake.row[0].pixels[5 * FAKE_W + 5] == 0xff202020, "a synchronized subsurface's commit shows nothing yet");
        wlc_send(c, t.s.surface, WLQ_wl_surface_commit);
        WLCHECK(fake.row[0].pixels[5 * FAKE_W + 5] == 0xff00ff00 && fake.row[0].pixels[3 * FAKE_W + 3] == 0xff202020 &&
                    fake.row[0].pixels[12 * FAKE_W + 12] == 0xff202020,
                "the parent's commit lays it at its place and no further");
        wlc_forget(c);
        /* a second commit of it replaces the picture and lets the first go */
        buffer = wl_buffer_of(&t.s, 8, 8, 0xff0000ff, WLN_wl_shm_format_xrgb8888);
        wlc_send(c, t.child, WLQ_wl_surface_attach, buffer, 0L, 0L);
        wlc_send(c, t.child, WLQ_wl_surface_damage_buffer, 0L, 0L, 8L, 8L);
        wlc_send(c, t.child, WLQ_wl_surface_commit);
        WLCHECK(wlc_count(c, WLI_wl_buffer, WLE_wl_buffer_release) == 0, "the picture it holds is not let go before its parent shows the next");
        wlc_send(c, t.s.surface, WLQ_wl_surface_commit);
        WLCHECK(fake.row[0].pixels[5 * FAKE_W + 5] == 0xff0000ff && wlc_count(c, WLI_wl_buffer, WLE_wl_buffer_release) == 1,
                "shown at the parent's commit, and the first picture is let go");
        wlc_end(c);

        /* desynchronized: at once */
        tree_open(&t, 1);
        c = t.s.c;
        WLCHECK(fake.row[0].pixels[5 * FAKE_W + 5] == 0xff00ff00, "a desynchronized subsurface shows at its own commit");
        /* the parent drawn over it again: the subsurface is laid back on */
        {
                unsigned long over = wl_buffer_of(&t.s, 32, 24, 0xff808080, WLN_wl_shm_format_xrgb8888);

                wlc_send(c, t.s.surface, WLQ_wl_surface_attach, over, 0L, 0L);
                wlc_send(c, t.s.surface, WLQ_wl_surface_damage_buffer, 0L, 0L, 32L, 24L);
                wlc_send(c, t.s.surface, WLQ_wl_surface_commit);
        }
        WLCHECK(fake.row[0].pixels[5 * FAKE_W + 5] == 0xff00ff00 && fake.row[0].pixels[20 * FAKE_W + 20] == 0xff808080,
                "the root drawn over it is drawn, and the subsurface is laid on it again");
        /* taken off: what it covered is the root's again */
        wlc_send(c, t.child, WLQ_wl_surface_attach, 0UL, 0L, 0L);
        wlc_send(c, t.child, WLQ_wl_surface_commit);
        WLCHECK(fake.row[0].pixels[5 * FAKE_W + 5] == 0xff808080, "a null buffer takes it off and the root shows where it was");
        wlc_end(c);

        /* moved: drawn at the new place, the old place the root's */
        tree_open(&t, 1);
        c = t.s.c;
        {
                unsigned long over = wl_buffer_of(&t.s, 32, 24, 0xff808080, WLN_wl_shm_format_xrgb8888);

                wlc_send(c, t.s.surface, WLQ_wl_surface_attach, over, 0L, 0L);
                wlc_send(c, t.s.surface, WLQ_wl_surface_damage_buffer, 0L, 0L, 32L, 24L);
                wlc_send(c, t.s.surface, WLQ_wl_surface_commit);
        }
        wlc_send(c, t.sub, WLQ_wl_subsurface_set_position, 16L, 10L);
        wlc_send(c, t.s.surface, WLQ_wl_surface_commit);
        WLCHECK(fake.row[0].pixels[12 * FAKE_W + 18] == 0xff00ff00 && fake.row[0].pixels[5 * FAKE_W + 5] == 0xff808080,
                "a subsurface moved at its parent's commit is where it was put and the place it left is the root's");
        wlc_end(c);

        /* translucent: laid over, not copied */
        tree_open(&t, 1);
        c = t.s.c;
        {
                unsigned long ghost = wl_buffer_of(&t.s, 8, 8, 0x80000000, WLN_wl_shm_format_argb8888);

                wlc_send(c, t.s.surface, WLQ_wl_surface_attach,
                         wl_buffer_of(&t.s, 32, 24, 0xff808080, WLN_wl_shm_format_xrgb8888), 0L, 0L);
                wlc_send(c, t.s.surface, WLQ_wl_surface_damage_buffer, 0L, 0L, 32L, 24L);
                wlc_send(c, t.s.surface, WLQ_wl_surface_commit);
                wlc_send(c, t.child, WLQ_wl_surface_attach, ghost, 0L, 0L);
                wlc_send(c, t.child, WLQ_wl_surface_damage_buffer, 0L, 0L, 8L, 8L);
                wlc_send(c, t.child, WLQ_wl_surface_commit);
        }
        {
                unsigned pixel = fake.row[0].pixels[5 * FAKE_W + 5];

                WLCHECK((pixel & 0xff) >= 0x3f && (pixel & 0xff) <= 0x41, "an ARGB subsurface is laid over what is under it (%#x)", pixel);
        }
        wlc_end(c);

        /* set_opaque_region: an ARGB8888 subsurface declared opaque takes partial damage without blending or re-copying the root */
        tree_open(&t, 1);
        c = t.s.c;
        {
                unsigned long reg = wlc_id(c, WLI_wl_region, 1);
                unsigned long opaque = wl_buffer_of(&t.s, 8, 8, 0x80112233u, WLN_wl_shm_format_argb8888);
                unsigned long dirty = wl_buffer_of(&t.s, 8, 8, 0x80445566u, WLN_wl_shm_format_argb8888);

                wlc_send(c, t.s.surface, WLQ_wl_surface_attach,
                         wl_buffer_of(&t.s, 32, 24, 0xff808080u, WLN_wl_shm_format_xrgb8888), 0L, 0L);
                wlc_send(c, t.s.surface, WLQ_wl_surface_damage_buffer, 0L, 0L, 32L, 24L);
                wlc_send(c, t.s.surface, WLQ_wl_surface_commit);
                wlc_send(c, t.s.compositor, WLQ_wl_compositor_create_region, reg);
                wlc_send(c, reg, WLQ_wl_region_add, 0L, 0L, 8L, 8L);
                wlc_send(c, t.child, WLQ_wl_surface_set_opaque_region, reg);
                wlc_send(c, reg, WLQ_wl_region_destroy);
                wlc_send(c, t.child, WLQ_wl_surface_attach, opaque, 0L, 0L);
                wlc_send(c, t.child, WLQ_wl_surface_damage_buffer, 0L, 0L, 8L, 8L);
                wlc_send(c, t.child, WLQ_wl_surface_commit);
                WLCHECK(fake.row[0].pixels[5 * FAKE_W + 5] == 0x80112233u,
                        "an ARGB subsurface with full opaque_region copies without blending (%#x)", fake.row[0].pixels[5 * FAKE_W + 5]);

                fake.copied = 0;
                wlc_send(c, t.child, WLQ_wl_surface_attach, dirty, 0L, 0L);
                wlc_send(c, t.child, WLQ_wl_surface_damage_buffer, 1L, 1L, 2L, 2L);
                wlc_send(c, t.child, WLQ_wl_surface_commit);
                WLCHECK(fake.copied == 4 && fake.row[0].pixels[5 * FAKE_W + 5] == 0x80445566u &&
                        fake.row[0].pixels[4 * FAKE_W + 4] == 0x80112233u,
                        "opaque ARGB subsurface partial damage copies only damaged pixels (%ld copied, corner %#x)",
                        fake.copied, fake.row[0].pixels[4 * FAKE_W + 4]);

                /* Clearing opaque_region restores translucent blending over root */
                wlc_send(c, t.child, WLQ_wl_surface_set_opaque_region, 0L);
                wlc_send(c, t.child, WLQ_wl_surface_attach, wl_buffer_of(&t.s, 8, 8, 0x80000000u, WLN_wl_shm_format_argb8888), 0L, 0L);
                wlc_send(c, t.child, WLQ_wl_surface_damage_buffer, 0L, 0L, 8L, 8L);
                wlc_send(c, t.child, WLQ_wl_surface_commit);
                {
                        unsigned pixel = fake.row[0].pixels[5 * FAKE_W + 5];

                        WLCHECK((pixel & 0xffu) >= 0x3fu && (pixel & 0xffu) <= 0x41u,
                                "clearing opaque_region restores blending (%#x)", pixel);
                }
        }
        wlc_end(c);

        /* order: the later sibling is on top, and place_below puts it under */
        tree_open(&t, 1);
        c = t.s.c;
        {
                unsigned long other = wlc_id(c, WLI_wl_surface, 6), other_sub = wlc_id(c, WLI_wl_subsurface, 1);

                wlc_send(c, t.s.compositor, WLQ_wl_compositor_create_surface, other);
                wlc_send(c, t.subcompositor, WLQ_wl_subcompositor_get_subsurface, other_sub, other, t.s.surface);
                wlc_send(c, other_sub, WLQ_wl_subsurface_set_desync);
                wlc_send(c, other_sub, WLQ_wl_subsurface_set_position, 6L, 6L);
                wlc_send(c, t.s.surface, WLQ_wl_surface_commit);
                wlc_send(c, other, WLQ_wl_surface_attach, wl_buffer_of(&t.s, 8, 8, 0xffff0000, WLN_wl_shm_format_xrgb8888), 0L, 0L);
                wlc_send(c, other, WLQ_wl_surface_damage_buffer, 0L, 0L, 8L, 8L);
                wlc_send(c, other, WLQ_wl_surface_commit);
                WLCHECK(fake.row[0].pixels[8 * FAKE_W + 8] == 0xffff0000, "of two overlapping subsurfaces the later is on top");
                wlc_send(c, other_sub, WLQ_wl_subsurface_place_below, t.child);
                wlc_send(c, t.s.surface, WLQ_wl_surface_commit);
                WLCHECK(fake.row[0].pixels[8 * FAKE_W + 8] == 0xff00ff00 && c->errors == 0, "placed below its sibling it is under it (%s)", c->error_text);
                wlc_send(c, other_sub, WLQ_wl_subsurface_place_above, t.s.surface);
                WLCHECK(c->errors == 0, "above the parent is a place");
                wlc_send(c, other_sub, WLQ_wl_subsurface_place_above, wlc_id(c, WLI_wl_surface, 6));
        }
        WLCHECK(c->errors == 1, "a place above a surface that is nobody's sibling is refused (%s)", c->error_text);
        wlc_end(c);

        /* the rules of making one */
        session_open(&t.s);
        c = t.s.c;
        t.subcompositor = wl_global(c, t.s.registry, WLI_wl_subcompositor, 1);
        t.child = wlc_id(c, WLI_wl_surface, 6);
        wlc_send(c, t.s.compositor, WLQ_wl_compositor_create_surface, t.child);
        wlc_send(c, t.subcompositor, WLQ_wl_subcompositor_get_subsurface, wlc_id(c, WLI_wl_subsurface, 1), t.child, t.child);
        WLCHECK(refused_with(c, WLN_wl_subcompositor_error_bad_surface), "a surface is not its own subsurface");
        wlc_end(c);
        session_open(&t.s);
        session_window(&t.s, 0xff000000);
        c = t.s.c;
        t.subcompositor = wl_global(c, t.s.registry, WLI_wl_subcompositor, 1);
        t.child = wlc_id(c, WLI_wl_surface, 6);
        wlc_send(c, t.s.compositor, WLQ_wl_compositor_create_surface, t.child);
        wlc_send(c, t.subcompositor, WLQ_wl_subcompositor_get_subsurface, wlc_id(c, WLI_wl_subsurface, 1), t.s.surface, t.child);
        WLCHECK(refused_with(c, WLN_wl_subcompositor_error_bad_surface), "a window's surface cannot become a subsurface");
        wlc_end(c);
        session_open(&t.s);
        c = t.s.c;
        t.subcompositor = wl_global(c, t.s.registry, WLI_wl_subcompositor, 1);
        {
                unsigned long a = wlc_id(c, WLI_wl_surface, 6), b = wlc_id(c, WLI_wl_surface, 6);

                wlc_send(c, t.s.compositor, WLQ_wl_compositor_create_surface, a);
                wlc_send(c, t.s.compositor, WLQ_wl_compositor_create_surface, b);
                wlc_send(c, t.subcompositor, WLQ_wl_subcompositor_get_subsurface, wlc_id(c, WLI_wl_subsurface, 1), a, b);
                wlc_send(c, t.subcompositor, WLQ_wl_subcompositor_get_subsurface, wlc_id(c, WLI_wl_subsurface, 1), b, a);
        }
        WLCHECK(c->errors == 1, "a subsurface cycle is refused");
        wlc_end(c);

        /* a surface that has a subsurface object takes no second; one whose object was destroyed takes a new one and shows again */
        tree_open(&t, 1);
        c = t.s.c;
        wlc_send(c, t.subcompositor, WLQ_wl_subcompositor_get_subsurface, wlc_id(c, WLI_wl_subsurface, 1), t.child, t.s.surface);
        WLCHECK(refused_with(c, WLN_wl_subcompositor_error_bad_surface), "a surface that has its subsurface takes no second");
        wlc_end(c);
        tree_open(&t, 1);
        c = t.s.c;
        wlc_send(c, t.sub, WLQ_wl_subsurface_destroy);
        wlc_forget(c);
        t.sub = wlc_id(c, WLI_wl_subsurface, 1);
        wlc_send(c, t.subcompositor, WLQ_wl_subcompositor_get_subsurface, t.sub, t.child, t.s.surface);
        wlc_send(c, t.sub, WLQ_wl_subsurface_set_position, 10L, 10L);
        wlc_send(c, t.sub, WLQ_wl_subsurface_set_desync);
        buffer = wl_buffer_of(&t.s, 8, 8, 0xff123456, WLN_wl_shm_format_xrgb8888);
        wlc_send(c, t.child, WLQ_wl_surface_attach, buffer, 0L, 0L);
        wlc_send(c, t.child, WLQ_wl_surface_damage_buffer, 0L, 0L, 8L, 8L);
        wlc_send(c, t.child, WLQ_wl_surface_commit);
        WLCHECK(c->errors == 0 && fake.row[0].pixels[12 * FAKE_W + 12] == 0xff123456,
                "a surface whose subsurface was destroyed is made one again and is shown (%s)", c->error_text);
        wlc_end(c);

        /* destroyed in every order: the subsurface object, its surface, the parent first */
        tree_open(&t, 1);
        c = t.s.c;
        wlc_send(c, t.sub, WLQ_wl_subsurface_destroy);
        WLCHECK(c->errors == 0, "a subsurface destroyed");
        wlc_send(c, t.child, WLQ_wl_surface_destroy);
        wlc_send(c, t.s.surface, WLQ_wl_surface_commit);
        WLCHECK(c->errors == 0, "and its surface, and the parent goes on");
        wlc_end(c);
        tree_open(&t, 1);
        c = t.s.c;
        wlc_send(c, t.s.surface, WLQ_wl_surface_destroy);
        wlc_send(c, t.child, WLQ_wl_surface_commit);
        wlc_send(c, t.sub, WLQ_wl_subsurface_set_position, 1L, 1L);
        wlc_send(c, t.sub, WLQ_wl_subsurface_destroy);
        WLCHECK(c->errors == 0, "a subsurface whose parent is gone is inert (%s)", c->error_text);
        wlc_end(c);
        tree_open(&t, 1);
        c = t.s.c;
        wlc_send(c, t.child, WLQ_wl_surface_destroy);
        wlc_send(c, t.sub, WLQ_wl_subsurface_set_sync);
        WLCHECK(c->errors == 0, "a subsurface's object outliving its surface is inert");
        wlc_end(c);
        /* the parent's window destroyed while a subsurface holds a picture: all comes back */
        tree_open(&t, 1);
        c = t.s.c;
        wlc_send(c, t.s.toplevel, WLQ_xdg_toplevel_destroy);
        wlc_end(c);

        /* a picture's buffer destroyed while it is held: shown still, the record freed when it is let go */
        tree_open(&t, 1);
        c = t.s.c;
        buffer = wl_buffer_of(&t.s, 32, 24, 0xff808080, WLN_wl_shm_format_xrgb8888);
        wlc_send(c, t.s.surface, WLQ_wl_surface_attach, buffer, 0L, 0L);
        wlc_send(c, t.s.surface, WLQ_wl_surface_damage_buffer, 0L, 0L, 32L, 24L);
        wlc_send(c, t.s.surface, WLQ_wl_surface_commit);
        wlc_send(c, buffer, WLQ_wl_buffer_destroy);
        wlc_send(c, t.child, WLQ_wl_surface_attach, 0UL, 0L, 0L);
        wlc_send(c, t.child, WLQ_wl_surface_commit);
        WLCHECK(c->errors == 0 && fake.row[0].pixels[5 * FAKE_W + 5] == 0xff808080,
                "a destroyed buffer that was the picture still draws the root (%s)", c->error_text);
        {
                long live = fake.maps - fake.unmaps;

                buffer = wl_buffer_of(&t.s, 32, 24, 0xff404040, WLN_wl_shm_format_xrgb8888);
                wlc_send(c, t.s.surface, WLQ_wl_surface_attach, buffer, 0L, 0L);
                wlc_send(c, t.s.surface, WLQ_wl_surface_damage_buffer, 0L, 0L, 32L, 24L);
                wlc_send(c, t.s.surface, WLQ_wl_surface_commit);
                WLCHECK(c->errors == 0 && fake.maps - fake.unmaps == live,
                        "and its record and its mapping go when the next picture replaces it (%ld mapped, %ld before)", fake.maps - fake.unmaps, live);
        }
        wlc_end(c);

        /* a subsurface committed before its window is shown is shown with it */
        session_open(&t.s);
        c = t.s.c;
        t.subcompositor = wl_global(c, t.s.registry, WLI_wl_subcompositor, 1);
        t.s.surface = wlc_id(c, WLI_wl_surface, 6);
        wlc_send(c, t.s.compositor, WLQ_wl_compositor_create_surface, t.s.surface);
        t.child = wlc_id(c, WLI_wl_surface, 6);
        wlc_send(c, t.s.compositor, WLQ_wl_compositor_create_surface, t.child);
        t.sub = wlc_id(c, WLI_wl_subsurface, 1);
        wlc_send(c, t.subcompositor, WLQ_wl_subcompositor_get_subsurface, t.sub, t.child, t.s.surface);
        wlc_send(c, t.sub, WLQ_wl_subsurface_set_desync);
        wlc_send(c, t.child, WLQ_wl_surface_attach, wl_buffer_of(&t.s, 8, 8, 0xff00ff00, WLN_wl_shm_format_xrgb8888), 0L, 0L);
        wlc_send(c, t.child, WLQ_wl_surface_commit);
        WLCHECK(c->errors == 0 && !fake.row[0].used, "a subsurface of a window not shown shows nothing and is no error");
        wlc_end(c);

        /* the pointer picks the topmost subsurface that shows where it is, in that surface's own pixels */
        {
                struct wl_event_seen *e;
                unsigned long pointer;

                tree_open(&t, 1);
                c = t.s.c;
                pointer = wlc_id(c, WLI_wl_pointer, 9);
                wlc_send(c, t.s.seat, WLQ_wl_seat_get_pointer, pointer);
                wlc_forget(c);
                wayland_pointer_to(0, 6, 7);
                wlc_pump(c);
                e = wlc_take(c, WLI_wl_pointer, WLE_wl_pointer_enter);
                WLCHECK(e && e->u[1] == t.child && e->u[2] == 2 * 256 && e->u[3] == 3 * 256, "over a subsurface the pointer enters it, at its own place in it");
                wlc_forget(c);
                wayland_pointer_motion(0, 20, 20);
                wlc_pump(c);
                e = wlc_take(c, WLI_wl_pointer, WLE_wl_pointer_leave);
                WLCHECK(e && e->u[1] == t.child, "moving off it is a leave of it");
                e = wlc_take(c, WLI_wl_pointer, WLE_wl_pointer_enter);
                WLCHECK(e && e->u[1] == t.s.surface && e->u[2] == 20 * 256, "and an enter of the window's own surface");
                /* a subsurface with an input region of nothing takes no pointer */
                {
                        unsigned long region = wlc_id(c, WLI_wl_region, 6);

                        wlc_send(c, t.s.compositor, WLQ_wl_compositor_create_region, region);
                        wlc_send(c, t.child, WLQ_wl_surface_set_input_region, region);
                        wlc_forget(c);
                        wayland_pointer_motion(0, 6, 7);
                        wlc_pump(c);
                        e = wlc_take(c, WLI_wl_pointer, WLE_wl_pointer_motion);
                        WLCHECK(e && wlc_count(c, WLI_wl_pointer, WLE_wl_pointer_enter) == 0, "a surface whose input region is empty is passed through");
                }
                wlc_end(c);
        }

        /* a partial input_region (add + subtract) lets hits pass through the cutout, and subtracting it all passes through */
        tree_open(&t, 1);
        c = t.s.c;
        {
                unsigned long pointer = wlc_id(c, WLI_wl_pointer, 9), reg = wlc_id(c, WLI_wl_region, 1);
                struct wl_event_seen *e;

                wlc_send(c, t.s.seat, WLQ_wl_seat_get_pointer, pointer);
                wlc_send(c, t.s.compositor, WLQ_wl_compositor_create_region, reg);
                wlc_send(c, reg, WLQ_wl_region_add, 0L, 0L, 8L, 8L);
                wlc_send(c, reg, WLQ_wl_region_subtract, 4L, 0L, 4L, 8L);
                wlc_send(c, t.child, WLQ_wl_surface_set_input_region, reg);
                wlc_pump(c);
                wlc_forget(c);

                wayland_pointer_to(0, 6, 6);
                wlc_pump(c);
                e = wlc_take(c, WLI_wl_pointer, WLE_wl_pointer_enter);
                WLCHECK(e && e->u[1] == t.child, "the kept half of a subsurface input region takes the pointer");
                wlc_forget(c);

                wayland_pointer_to(0, 10, 6);
                wlc_pump(c);
                e = wlc_take(c, WLI_wl_pointer, WLE_wl_pointer_enter);
                WLCHECK(e && e->u[1] == t.s.surface, "the subtracted half passes through to the root below");
                wlc_forget(c);

                wlc_send(c, reg, WLQ_wl_region_subtract, 0L, 0L, 4L, 8L);
                wlc_send(c, t.child, WLQ_wl_surface_set_input_region, reg);
                wlc_send(c, reg, WLQ_wl_region_destroy);
                wayland_pointer_motion(0, 6, 6);
                wlc_pump(c);
                e = wlc_take(c, WLI_wl_pointer, WLE_wl_pointer_motion);
                WLCHECK(e && wlc_count(c, WLI_wl_pointer, WLE_wl_pointer_enter) == 0,
                        "a region emptied by subtract passes through rather than blocking");
        }
        wlc_end(c);

        /* a subsurface placed below the root does not receive pointer hits under the root */
        tree_open(&t, 1);
        c = t.s.c;
        {
                unsigned long pointer = wlc_id(c, WLI_wl_pointer, 9);
                struct wl_event_seen *e;

                wlc_send(c, t.s.seat, WLQ_wl_seat_get_pointer, pointer);
                wlc_send(c, t.sub, WLQ_wl_subsurface_place_below, t.s.surface);
                wlc_send(c, t.s.surface, WLQ_wl_surface_commit);
                wlc_pump(c);
                wlc_forget(c);
                wayland_pointer_to(0, 6, 6);
                wlc_pump(c);
                e = wlc_take(c, WLI_wl_pointer, WLE_wl_pointer_enter);
                WLCHECK(e && e->u[1] == t.s.surface, "a subsurface placed below root does not steal pointer focus");
        }
        wlc_end(c);

        /* frame callbacks of a subsurface wait for the window's frame, with its parent's */
        tree_open(&t, 1);
        c = t.s.c;
        {
                unsigned long callback = wlc_id(c, WLI_wl_callback, 1);

                wlc_send(c, t.child, WLQ_wl_surface_frame, callback);
                wlc_send(c, t.child, WLQ_wl_surface_commit);
                WLCHECK(wayland_frame_wanted() && wlc_count(c, WLI_wl_callback, WLE_wl_callback_done) == 0, "a subsurface's frame callback waits");
                wayland_frame();
                wlc_pump(c);
                WLCHECK(wlc_count(c, WLI_wl_callback, WLE_wl_callback_done) == 1, "and is answered with the window's frame");
        }
        wlc_end(c);
        leak_check("subsurfaces");
        WLCHECK(fds_balanced(), "descriptors balance (%ld %ld)", fake.fds_open, fake.fds_closed);
        wayland_stop();
}

/*
        What one commit of a window costs the compositor when its subsurfaces are as many as a client may make, hung in one chain
        (the deepest tree), each with a picture of eight damaged rectangles waiting for the root's commit (ab981848): every copy
        of a run made the window's rectangle again, a walk of every surface and its chain, and the root's commit took a quarter of a
        second on an M2 (424 ms at worst) for some 14 KB a client sends; made once a run, it is under 10 ms. The bound is a mean of
        three under 50 ms, which a loaded or sanitized build keeps and the walk in every copy (158 ms) does not.
*/
static void test_tree_cost(void)
{
        struct session s;
        struct wlc *c;
        unsigned long subcompositor, surface[64], buffer[64];
        double total = 0;
        long count = 64 - 1, rounds = 3;

        wl_reset();
        wayland_start(&fake_host);
        session_open(&s);
        c = s.c;
        subcompositor = wl_global(c, s.registry, WLI_wl_subcompositor, 1);
        session_window(&s, 0xff303030);
        surface[0] = s.surface;
        for (long k = 1; k <= count; k++)
        {
                surface[k] = wlc_id(c, WLI_wl_surface, 6);
                wlc_send(c, s.compositor, WLQ_wl_compositor_create_surface, surface[k]);
                wlc_send(c, subcompositor, WLQ_wl_subcompositor_get_subsurface, wlc_id(c, WLI_wl_subsurface, 1), surface[k], surface[k - 1]);
                buffer[k] = wl_buffer_of(&s, 24, 16, 0xff00ff00u, WLN_wl_shm_format_xrgb8888);
        }
        for (long round = 0; round < rounds; round++)
        {
                struct timespec start, end;

                for (long k = 1; k <= count; k++)
                {
                        wlc_send(c, surface[k], WLQ_wl_surface_attach, buffer[k], 0L, 0L);
                        for (long d = 0; d < 8; d++)
                                wlc_send(c, surface[k], WLQ_wl_surface_damage_buffer, d * 3, 0L, 2L, 2L);
                        wlc_send(c, surface[k], WLQ_wl_surface_commit);
                }
                clock_gettime(CLOCK_MONOTONIC, &start);
                wlc_send(c, surface[0], WLQ_wl_surface_commit);
                clock_gettime(CLOCK_MONOTONIC, &end);
                total += (double)(end.tv_sec - start.tv_sec) * 1e3 + (double)(end.tv_nsec - start.tv_nsec) / 1e6;
        }
        WLCHECK(c->errors == 0 && fake.row[0].pixels[5 * FAKE_W + 5] == 0xff00ff00, "a chain of %ld subsurfaces is shown (%s)", count,
                c->error_text);
        WLCHECK(total / (double)rounds < 50.0, "a commit of a window with a chain of %ld subsurfaces costs %.1f ms, under 50", count,
                total / (double)rounds);
        wlc_end(c);
        leak_check("tree cost");
        wayland_stop();
}

/*
        Trees of surfaces: made, hung from each other (and from themselves, and in rings), drawn on, moved, ordered,
        synchronized and desynchronized, and taken apart by random steps, with the pointer moving over them and the
        window woken between. What each step shows is the rows above's; what is asked here is that nothing fails, no event
        is malformed, and every buffer, mapping and window comes back.
*/
static void test_tree_fuzz(long rounds)
{
        enum { SURFACES = 8, BUFFERS = 5 };
        struct session s;
        struct wlc *c = 0;
        unsigned long surface[SURFACES], sub[SURFACES], buffer[BUFFERS], subcompositor = 0;
        long trees = 0, made = 0, drawn = 0, malformed = 0;

        wl_reset();
        wayland_start(&fake_host);
        for (long round = 0; round < rounds; round++)
        {
                long i = (long)(wl_random() % SURFACES), j = (long)(wl_random() % SURFACES), b = (long)(wl_random() % BUFFERS);

                if (!c || wayland_dead(c->slot) || round % 250 == 0)
                {
                        if (c)
                        {
                                malformed += c->malformed;
                                wlc_end(c);
                        }
                        session_open(&s);
                        c = s.c;
                        subcompositor = wl_global(c, s.registry, WLI_wl_subcompositor, 1);
                        session_window(&s, 0xff303030);
                        memset(surface, 0, sizeof surface);
                        memset(sub, 0, sizeof sub);
                        memset(buffer, 0, sizeof buffer);
                        surface[0] = s.surface;
                        wlc_send(c, s.seat, WLQ_wl_seat_get_pointer, wlc_id(c, WLI_wl_pointer, 9));
                        trees++;
                }
                switch (wl_random() % 17)
                {
                case 0:
                case 1:
                        if (i && !surface[i])
                        {
                                surface[i] = wlc_id(c, WLI_wl_surface, 6);
                                wlc_send(c, s.compositor, WLQ_wl_compositor_create_surface, surface[i]);
                        }
                        break;
                case 2:
                case 3:
                        // Mostly from another surface than itself, which is a fault of the client and the end of it.
                        if (j == i && wl_random() % 16)
                                j = (j + 1) % SURFACES;
                        if (i && surface[i] && !sub[i] && surface[j])
                        {
                                sub[i] = wlc_id(c, WLI_wl_subsurface, 1);
                                if (wlc_send(c, subcompositor, WLQ_wl_subcompositor_get_subsurface, sub[i], surface[i], surface[j]) >= 0)
                                        made++;
                        }
                        break;
                case 4:
                        buffer[b] = wl_buffer_of(&s, 4 + (long)(wl_random() % 28), 4 + (long)(wl_random() % 20), 0xff000000u | (unsigned)wl_random(),
                                                 wl_random() & 1 ? WLN_wl_shm_format_argb8888 : WLN_wl_shm_format_xrgb8888);
                        break;
                case 5:
                case 6:
                case 7:
                        // A null buffer takes the window off, and it needs a configure before it shows again: only a subsurface is taken off here.
                        if (surface[i] && (buffer[b] || i))
                        {
                                wlc_send(c, surface[i], WLQ_wl_surface_attach, buffer[b] && (!i || wl_random() % 8) ? buffer[b] : 0UL, 0L, 0L);
                                wlc_send(c, surface[i], WLQ_wl_surface_damage_buffer, 0L, 0L, 40L, 30L);
                                if (wlc_send(c, surface[i], WLQ_wl_surface_commit) >= 0)
                                        drawn++;
                        }
                        break;
                case 8:
                        if (surface[i])
                                wlc_send(c, surface[i], WLQ_wl_surface_commit);
                        break;
                case 9:
                        if (sub[i])
                                wlc_send(c, sub[i], WLQ_wl_subsurface_set_position, (long)(wl_random() % 40) - 8, (long)(wl_random() % 40) - 8);
                        break;
                case 10:
                        if (sub[i] && surface[j])
                                wlc_send(c, sub[i], wl_random() & 1 ? WLQ_wl_subsurface_place_above : WLQ_wl_subsurface_place_below, surface[j]);
                        break;
                case 11:
                        if (sub[i])
                                wlc_send(c, sub[i], wl_random() & 1 ? WLQ_wl_subsurface_set_sync : WLQ_wl_subsurface_set_desync);
                        break;
                case 12:
                        if (sub[i])
                        {
                                wlc_send(c, sub[i], WLQ_wl_subsurface_destroy);
                                sub[i] = 0;
                        }
                        break;
                case 13:
                        if (i && surface[i])
                        {
                                wlc_send(c, surface[i], WLQ_wl_surface_destroy);
                                surface[i] = 0;
                        }
                        break;
                case 14:
                        if (buffer[b])
                        {
                                wlc_send(c, buffer[b], WLQ_wl_buffer_destroy);
                                buffer[b] = 0;
                        }
                        break;
                case 15:
                        if (wl_random() & 1)
                                wayland_pointer_to(0, (long)(wl_random() % 44) - 6, (long)(wl_random() % 36) - 6);
                        else
                                wayland_pointer_motion(0, (long)(wl_random() % 44) - 6, (long)(wl_random() % 36) - 6);
                        break;
                default:
                        for (int row = 0; row < FAKE_ROWS; row++)
                                if (fake.row[row].used)
                                {
                                        fake.row[row].states = (long)(wl_random() & 0x1f);
                                        fake.row[row].width_asked = (long)(wl_random() % 60);
                                        wayland_row_wake(row);
                                }
                        wayland_frame();
                        break;
                }
                if (round % 8 == 0 && c && !wayland_dead(c->slot))
                        wlc_pump(c);
        }
        if (c)
        {
                malformed += c->malformed;
                wlc_end(c);
        }
        WLCHECK(malformed == 0, "every event of the tree fuzz was well-formed (%ld malformed)", malformed);
        WLCHECK(made > 0 && drawn > 0 && trees > 1, "the tree fuzz hung subsurfaces and drew on surfaces (%ld, %ld, %ld trees)", made, drawn, trees);
        leak_check("tree fuzz");
        WLCHECK(fds_balanced(), "every descriptor of the tree fuzz was closed (%ld %ld)", fake.fds_open, fake.fds_closed);
        printf("CHECK_wayland tree fuzz: %ld rounds, %ld subsurfaces, %ld commits, %ld trees\n", rounds, made, drawn, trees);
        wayland_stop();
}

/* ---- the clipboard ---- */

struct clipc
{
        struct session s;
        unsigned long keyboard, device, primary_device;
        long row;
};

/* A client with a window, a keyboard and both kinds of device. */
static void clip_open(struct clipc *k)
{
        struct wlc *c;

        session_open(&k->s);
        c = k->s.c;
        session_window(&k->s, 0xff404040);
        k->row = -1;
        for (long row = 0; row < FAKE_ROWS; row++)
                if (fake.row[row].used && wayland_row_owner(row) == c->slot)
                        k->row = row;
        k->keyboard = wlc_id(c, WLI_wl_keyboard, 9);
        wlc_send(c, k->s.seat, WLQ_wl_seat_get_keyboard, k->keyboard);
        k->device = wlc_id(c, WLI_wl_data_device, 3);
        wlc_send(c, k->s.data, WLQ_wl_data_device_manager_get_data_device, k->device, k->s.seat);
        k->primary_device = wlc_id(c, WLI_zwp_primary_selection_device_v1, 1);
        wlc_send(c, k->s.primary, WLQ_zwp_primary_selection_device_manager_v1_get_device, k->primary_device, k->s.seat);
}

/* The serial of a key the client has just been sent (the keys are its). */
static unsigned long clip_serial(struct clipc *k)
{
        struct wl_event_seen *e;

        wlc_forget(k->s.c);
        wayland_key(k->row, 30, 1);
        wayland_key(k->row, 30, 0);
        wlc_pump(k->s.c);
        e = wlc_take(k->s.c, WLI_wl_keyboard, WLE_wl_keyboard_key);
        return e ? e->u[0] : 0;
}

static unsigned long clip_source(struct clipc *k, int primary, const char *mime)
{
        struct wlc *c = k->s.c;
        unsigned long source = wlc_id(c, primary ? WLI_zwp_primary_selection_source_v1 : WLI_wl_data_source, primary ? 1 : 3);

        if (primary)
                wlc_send(c, k->s.primary, WLQ_zwp_primary_selection_device_manager_v1_create_source, source);
        else
                wlc_send(c, k->s.data, WLQ_wl_data_device_manager_create_data_source, source);
        if (mime)
                wlc_send(c, source, primary ? WLQ_zwp_primary_selection_source_v1_offer : WLQ_wl_data_source_offer, mime);
        return source;
}

static void clip_set(struct clipc *k, int primary, unsigned long source, unsigned long serial)
{
        wlc_send(k->s.c, primary ? k->primary_device : k->device,
                 primary ? WLQ_zwp_primary_selection_device_v1_set_selection : WLQ_wl_data_device_set_selection, source, (long)serial);
}

/* The host has read the source's text to its end: it is the selection, and then the source is told so. */
static void clip_finish(struct clipc *k, int primary, const char *text)
{
        snprintf(fake.clip_text[primary], sizeof fake.clip_text[primary], "%s", text);
        fake.clip_size[primary] = (long)strlen(text);
        wayland_selection_changed(primary);
        wayland_clip_pulled(k->s.c->slot, primary, 1);
        wlc_pump(k->s.c);
}

/* The text of a selection changed by someone else: the compositor's own copy. */
static void clip_other(int primary, const char *text)
{
        snprintf(fake.clip_text[primary], sizeof fake.clip_text[primary], "%s", text);
        fake.clip_size[primary] = (long)strlen(text);
        wayland_selection_changed(primary);
}

static long wl_pipe(int *reader)
{
        int fds[2];

        if (pipe(fds) < 0)
                abort();
        fcntl(fds[0], F_SETFL, O_NONBLOCK);
        *reader = fds[0];
        fake.fds_open++;
        return fds[1];
}

/* A paste asked of an offer: the bytes the compositor wrote to the descriptor (0 for none, an end of file), -1 if it is still open. */
static long clip_paste(struct clipc *k, int primary, unsigned long offer, const char *mime, char *text, long room)
{
        int reader;
        long write_end = wl_pipe(&reader), got;

        wlc_send(k->s.c, offer, primary ? WLQ_zwp_primary_selection_offer_v1_receive : WLQ_wl_data_offer_receive, mime, write_end);
        got = (long)read(reader, text, (size_t)room - 1);
        close(reader);
        if (got >= 0)
                text[got] = 0;
        return got;
}

static unsigned long clip_offer_of(struct wlc *c, int primary, struct wl_event_seen **selection)
{
        struct wl_event_seen *e = wlc_take(c, primary ? WLI_zwp_primary_selection_device_v1 : WLI_wl_data_device,
                                           primary ? WLE_zwp_primary_selection_device_v1_data_offer : WLE_wl_data_device_data_offer);
        unsigned long offer = e ? e->u[0] : 0;

        *selection = wlc_take(c, primary ? WLI_zwp_primary_selection_device_v1 : WLI_wl_data_device,
                              primary ? WLE_zwp_primary_selection_device_v1_selection : WLE_wl_data_device_selection);
        return offer;
}

static long clip_cancelled(struct wlc *c, int primary)
{
        return wlc_count(c, primary ? WLI_zwp_primary_selection_source_v1 : WLI_wl_data_source,
                         primary ? WLE_zwp_primary_selection_source_v1_cancelled : WLE_wl_data_source_cancelled);
}

static void test_clipboard(void)
{
        struct clipc a, b, k;
        struct wlc *ca, *cb;
        struct wl_event_seen *e, *chosen;
        unsigned long source, serial, offer, offer_b, second;
        char text[64];
        long pulls;

        wl_reset();
        wayland_start(&fake_host);
        clip_open(&a);
        clip_open(&b);
        ca = a.s.c;
        cb = b.s.c;
        WLCHECK(a.s.data && a.s.primary && a.row >= 0 && b.row >= 0 && a.row != b.row,
                "the data device manager and the primary selection manager are offered, to windows of their own");

        /* getting the keys is being told the selection, and there is none yet */
        wlc_forget(ca);
        wayland_keyboard_to(a.row);
        wlc_pump(ca);
        e = wlc_take(ca, WLI_wl_data_device, WLE_wl_data_device_selection);
        WLCHECK(e && e->u[0] == 0, "a client that gets the keys is told there is no selection when there is none");
        e = wlc_take(ca, WLI_zwp_primary_selection_device_v1, WLE_zwp_primary_selection_device_v1_selection);
        WLCHECK(e && e->u[0] == 0, "and no primary selection");

        /* a copy: the source is asked for its text, in the UTF-8 type, with a descriptor, and the text is the selection when it is read */
        serial = clip_serial(&a);
        WLCHECK(serial != 0, "a key has a serial");
        source = clip_source(&a, 0, "text/plain;charset=utf-8");
        wlc_forget(ca);
        clip_set(&a, 0, source, serial);
        e = wlc_take(ca, WLI_wl_data_source, WLE_wl_data_source_send);
        WLCHECK(fake.pulls == 1 && fake.pull_which == 0 && fake.pull_client == ca->slot && e && e->fd_count == 1 &&
                    !strcmp(e->text[0], "text/plain;charset=utf-8"),
                "a source set with a serial of the client's is asked for its text, in the UTF-8 type, with a descriptor (%ld pulls)", fake.pulls);
        WLCHECK(ca->errors == 0 && clip_cancelled(ca, 0) == 0, "and it is not cancelled while it is read");
        wlc_forget(ca);
        clip_finish(&a, 0, "hello");
        offer = clip_offer_of(ca, 0, &chosen);
        WLCHECK(offer >= WAYLAND_SERVER_BASE && chosen && chosen->u[0] == offer,
                "the text the compositor holds is offered to the client that has the keys, as an object of the server's, and selected");
        WLCHECK(wlc_count(ca, WLI_wl_data_offer, WLE_wl_data_offer_offer) == 3, "with the three types of text");
        WLCHECK(clip_cancelled(ca, 0) == 0, "the source that made it is not told it is not wanted");
        WLCHECK(clip_paste(&a, 0, offer, "text/plain;charset=utf-8", text, sizeof text) == 5 && !strcmp(text, "hello") &&
                    fake.pushes == 1 && fake.push_row == a.row && fake.push_which == 0,
                "a paste on the offer is the text, written to the descriptor for the window with the keys");
        WLCHECK(clip_paste(&a, 0, offer, "UTF8_STRING", text, sizeof text) == 5 && fake.pushes == 2, "in the other text type too");
        pulls = fake.pushes;
        WLCHECK(clip_paste(&a, 0, offer, "image/png", text, sizeof text) == 0 && fake.pushes == pulls && !strcmp(fake.push_mime, "image/png"),
                "a type the copy has not is asked of the host, which writes nothing and closes the descriptor");
        WLCHECK(ca->errors == 0, "no error of these (%s)", ca->error_text);

        /* B gets the keys: it is offered the text; A, which no longer has them, is given nothing for its old offer */
        wlc_forget(cb);
        wayland_keyboard_to(b.row);
        wlc_pump(cb);
        offer_b = clip_offer_of(cb, 0, &chosen);
        WLCHECK(offer_b >= WAYLAND_SERVER_BASE && chosen && chosen->u[0] == offer_b, "a client that gets the keys is offered the selection");
        pulls = fake.pushes;
        WLCHECK(clip_paste(&a, 0, offer, "text/plain;charset=utf-8", text, sizeof text) == 0 && fake.pushes == pulls,
                "a client that does not have the keys is given nothing");
        WLCHECK(clip_paste(&b, 0, offer_b, "text/plain;charset=utf-8", text, sizeof text) == 5 && fake.push_row == b.row,
                "and the one that has them is");

        /* the text changes under it: the old offer is no longer the selection, and the source that was it is cancelled */
        wlc_forget(cb);
        wlc_forget(ca);
        clip_other(0, "next");
        wlc_pump(ca);
        wlc_pump(cb);
        WLCHECK(clip_cancelled(ca, 0) == 1, "when the text is another's the source that made it is told it is not wanted");
        offer = clip_offer_of(cb, 0, &chosen);
        pulls = fake.pushes;
        WLCHECK(clip_paste(&b, 0, offer_b, "text/plain;charset=utf-8", text, sizeof text) == 0 && fake.pushes == pulls,
                "an offer of the text before it changed gets nothing");
        WLCHECK(clip_paste(&b, 0, offer, "text/plain;charset=utf-8", text, sizeof text) == 4 && !strcmp(text, "next"),
                "and the new offer gets the new text");
        wlc_send(cb, offer_b, WLQ_wl_data_offer_destroy);
        WLCHECK(cb->errors == 0, "an offer is destroyed");

        /* what is refused is cancelled and is nothing more */
        serial = clip_serial(&b);
        pulls = fake.pulls;
        source = clip_source(&b, 0, "text/plain;charset=utf-8");
        wlc_forget(cb);
        clip_set(&b, 0, source, serial + 1000);
        wlc_pump(cb);
        WLCHECK(clip_cancelled(cb, 0) == 1 && fake.pulls == pulls, "a serial the client was never sent cancels the source");
        source = clip_source(&b, 0, "image/png");
        wlc_forget(cb);
        clip_set(&b, 0, source, serial);
        wlc_pump(cb);
        e = wlc_take(cb, WLI_wl_data_source, WLE_wl_data_source_send);
        WLCHECK(clip_cancelled(cb, 0) == 0 && fake.pulls == pulls + 1 && e && e->fd_count == 1 && !strcmp(e->text[0], "image/png"),
                "a source with no text is read as any other: asked for its image/png");
        wayland_clip_pulled(cb->slot, 0, 0);
        wlc_pump(cb);
        WLCHECK(clip_cancelled(cb, 0) == 1, "and cancelled when the host could not take it");
        pulls = fake.pulls;
        source = clip_source(&b, 0, 0);
        wlc_forget(cb);
        clip_set(&b, 0, source, serial);
        wlc_pump(cb);
        WLCHECK(clip_cancelled(cb, 0) == 1 && fake.pulls == pulls, "a source with no type at all is cancelled");
        source = clip_source(&b, 0, "text/plain");
        fake.pull_refuse = 1;
        wlc_forget(cb);
        clip_set(&b, 0, source, serial);
        wlc_pump(cb);
        WLCHECK(clip_cancelled(cb, 0) == 1 && fake.pulls == pulls, "a copy the host will not take (no gesture, the service off) cancels the source");
        fake.pull_refuse = 0;
        WLCHECK(cb->errors == 0, "none of these is an error (%s)", cb->error_text);

        /* a second copy before the first is read ends the first */
        {
                unsigned long first = clip_source(&b, 0, "text/plain;charset=utf-8");

                second = clip_source(&b, 0, "UTF8_STRING");
                serial = clip_serial(&b);
                wlc_forget(cb);
                pulls = fake.aborts;
                clip_set(&b, 0, first, serial);
                serial = clip_serial(&b);
                clip_set(&b, 0, second, serial);
                wlc_pump(cb);
                WLCHECK(clip_cancelled(cb, 0) == 1 && fake.aborts == pulls + 1, "a copy set while another is being read ends the other");
                e = wlc_take(cb, WLI_wl_data_source, WLE_wl_data_source_send);
                WLCHECK(e && e->fd_count == 1 && !strcmp(e->text[0], "UTF8_STRING"), "and the second is asked for its own type, with a descriptor");
                /* the host ended the second: it is the selection */
                wlc_forget(cb);
                clip_finish(&b, 0, "second");
                WLCHECK(clip_cancelled(cb, 0) == 0, "the source that is read last is the selection and is not cancelled");
                clip_other(0, "third");
                wlc_pump(cb);
                WLCHECK(clip_cancelled(cb, 0) == 1, "and is cancelled when the text is another's");
                /* a source destroyed while it is read ends the read; the host's end afterwards is nothing */
                first = clip_source(&b, 0, "text/plain");
                serial = clip_serial(&b);
                clip_set(&b, 0, first, serial);
                pulls = fake.aborts;
                wlc_send(cb, first, WLQ_wl_data_source_destroy);
                WLCHECK(fake.aborts == pulls + 1, "a source destroyed while it is read ends the read");
                wayland_clip_pulled(cb->slot, 0, 1);
                WLCHECK(cb->errors == 0, "and the host saying it ended afterwards is nothing");
        }

        /* clearing: only the client whose source it is may */
        {
                unsigned long mine = clip_source(&b, 0, "text/plain");

                serial = clip_serial(&b);
                clip_set(&b, 0, mine, serial);
                clip_finish(&b, 0, "mine");
                pulls = fake.clears;
                clip_set(&a, 0, 0UL, 0UL);
                WLCHECK(fake.clears == pulls, "a client that is not the selection's owner cannot clear it");
                clip_set(&b, 0, 0UL, 0UL);
                WLCHECK(fake.clears == pulls + 1, "its owner can");
        }

        /* a drag started with a key's serial, no button held, is none: its source is told it is not going */
        {
                unsigned long drag = clip_source(&b, 0, "text/plain");

                serial = clip_serial(&b);
                wlc_forget(cb);
                wlc_send(cb, b.device, WLQ_wl_data_device_start_drag, drag, b.s.surface, 0UL, (long)serial);
                wlc_pump(cb);
                WLCHECK(clip_cancelled(cb, 0) == 1 && cb->errors == 0 && !wayland_dragging(), "a drag with no button held is cancelled");
        }

        /* drag and drop in one window: entered with an offer of the source's types and actions, accepted, dropped where the button lets
           go, the target's descriptor handed to the source's send as it is, and finished */
        {
                unsigned long drag = clip_source(&b, 0, "application/x-moonwater-test"), offer;
                struct wl_event_seen *enter, *sent;
                int reader;
                long write_end;

                wlc_send(cb, drag, WLQ_wl_data_source_offer, "text/plain");
                wlc_send(cb, drag, WLQ_wl_data_source_set_actions, 3L);
                wayland_pointer_to(b.row, 10, 10);
                wayland_pointer_button(b.row, 272, 1);
                wlc_pump(cb);
                wlc_forget(cb);
                wlc_send(cb, b.device, WLQ_wl_data_device_start_drag, drag, b.s.surface, 0UL, (long)wayland.client[cb->slot]->press_serial);
                wlc_pump(cb);
                enter = wlc_take(cb, WLI_wl_data_device, WLE_wl_data_device_enter);
                offer = enter ? enter->u[4] : 0;
                WLCHECK(wayland_dragging() && enter && offer && wlc_count(cb, WLI_wl_data_offer, WLE_wl_data_offer_offer) == 2 &&
                            wlc_count(cb, WLI_wl_data_offer, WLE_wl_data_offer_source_actions) == 1 && cb->errors == 0,
                        "a drag with the press's serial enters the window it began in, its offer the source's two types and its actions");
                wayland_pointer_motion(b.row, 14, 12);
                wlc_pump(cb);
                WLCHECK(wlc_count(cb, WLI_wl_data_device, WLE_wl_data_device_motion) == 1 && wlc_count(cb, WLI_wl_pointer, WLE_wl_pointer_motion) == 0,
                        "the pointer's motion is the drag's, no pointer's");
                wlc_send(cb, offer, WLQ_wl_data_offer_accept, enter ? enter->u[0] : 0UL, "text/plain");
                wlc_send(cb, offer, WLQ_wl_data_offer_set_actions, 3L, 1L);
                wlc_pump(cb);
                WLCHECK(wlc_count(cb, WLI_wl_data_source, WLE_wl_data_source_target) == 1 && wlc_count(cb, WLI_wl_data_source, WLE_wl_data_source_action) == 1 &&
                            wlc_count(cb, WLI_wl_data_offer, WLE_wl_data_offer_action) == 1,
                        "an accepted type is the source's target, and the action both take (copy, the preferred) is told to both");
                wayland_pointer_button(b.row, 272, 0);
                wlc_pump(cb);
                WLCHECK(wlc_count(cb, WLI_wl_data_device, WLE_wl_data_device_drop) == 1 &&
                            wlc_count(cb, WLI_wl_data_source, WLE_wl_data_source_dnd_drop_performed) == 1 && !wayland_dragging() && clip_cancelled(cb, 0) == 0,
                        "let go over the window that accepted: drop, and the source told the drop was performed");
                write_end = wl_pipe(&reader);
                wlc_forget(cb);
                wlc_send(cb, offer, WLQ_wl_data_offer_receive, "text/plain", write_end);
                wlc_pump(cb);
                sent = wlc_take(cb, WLI_wl_data_source, WLE_wl_data_source_send);
                WLCHECK(sent && sent->fd_count == 1 && !strcmp(sent->text[0], "text/plain"), "the target's receive is the source's send, the descriptor handed on");
                close(reader);
                wlc_send(cb, offer, WLQ_wl_data_offer_finish);
                wlc_pump(cb);
                WLCHECK(wlc_count(cb, WLI_wl_data_source, WLE_wl_data_source_dnd_finished) == 1 && cb->errors == 0, "and its finish the source's dnd_finished");
                wlc_send(cb, offer, WLQ_wl_data_offer_destroy);
                wlc_send(cb, drag, WLQ_wl_data_source_destroy);
        }

        /* a drag let go where nothing took a type, and one ended by Escape: cancelled */
        {
                unsigned long drag = clip_source(&b, 0, "text/plain");

                wayland_pointer_to(b.row, 10, 10);
                wayland_pointer_button(b.row, 272, 1);
                wlc_pump(cb);
                wlc_forget(cb);
                wlc_send(cb, b.device, WLQ_wl_data_device_start_drag, drag, b.s.surface, 0UL, (long)wayland.client[cb->slot]->press_serial);
                wayland_pointer_button(b.row, 272, 0);
                wlc_pump(cb);
                WLCHECK(clip_cancelled(cb, 0) == 0 && wayland.drag.live && wayland.drag.released,
                        "a drag let go before its target answered waits for the answer");
                fake.now += WAYLAND_DRAG_ANSWER_MS;
                wayland_idle_tick();
                wlc_pump(cb);
                WLCHECK(clip_cancelled(cb, 0) == 1 && wlc_count(cb, WLI_wl_data_device, WLE_wl_data_device_drop) == 0 && !wayland.drag.live,
                        "and with no type accepted by then it is cancelled, and nothing dropped");
                drag = clip_source(&b, 0, "text/plain");
                wayland_pointer_button(b.row, 272, 1);
                wlc_pump(cb);
                wlc_forget(cb);
                wlc_send(cb, b.device, WLQ_wl_data_device_start_drag, drag, b.s.surface, 0UL, (long)wayland.client[cb->slot]->press_serial);
                wayland_key(b.row, 1, 1);
                wayland_key(b.row, 1, 0);
                wayland_pointer_button(b.row, 272, 0);
                wlc_pump(cb);
                WLCHECK(clip_cancelled(cb, 0) == 1 && wlc_count(cb, WLI_wl_data_device, WLE_wl_data_device_leave) == 1 && !wayland_dragging(),
                        "Escape cancels a drag, the window it was over left");
        }

        /* the primary selection is a selection of its own, by the same rules */
        serial = clip_serial(&b);
        source = clip_source(&b, 1, "text/plain;charset=utf-8");
        wlc_forget(cb);
        pulls = fake.pulls;
        clip_set(&b, 1, source, serial);
        e = wlc_take(cb, WLI_zwp_primary_selection_source_v1, WLE_zwp_primary_selection_source_v1_send);
        WLCHECK(fake.pulls == pulls + 1 && fake.pull_which == 1 && e && e->fd_count == 1, "a primary selection is read as a copy is, for its own selection");
        wlc_forget(cb);
        clip_finish(&b, 1, "prim");
        offer = clip_offer_of(cb, 1, &chosen);
        WLCHECK(offer >= WAYLAND_SERVER_BASE && chosen && chosen->u[0] == offer, "and is offered as primary selection offers");
        WLCHECK(clip_paste(&b, 1, offer, "text/plain;charset=utf-8", text, sizeof text) == 4 && !strcmp(text, "prim") && fake.push_which == 1,
                "and pasted as itself");
        WLCHECK(cb->errors == 0 && ca->errors == 0, "no error (%s)", cb->error_text);

        /*
                A copy ended in the same breath as it was asked (0976821d): set_selection and the source's destroy in one read, so
                the copy is aborted while its send event and the pipe's end still wait in the queue. The descriptor that waits is the
                pipe the host made, open, and the shim closes it once it is sent; one that waits when its client goes is closed too.
                The host closed it at the abort, and the queue then sent a number that was nothing, or whatever was opened next.
        */
        for (int client_goes = 0; client_goes < 2; client_goes++)
        {
                struct clipc d;
                unsigned char b2[32];
                const unsigned char *queued;
                const long *fds;
                long count = 0, fd = -1, aborts = fake.aborts;
                char mark[1] = {0};
                unsigned long copy;
                int same;

                clip_open(&d);
                wayland_keyboard_to(d.row);
                copy = clip_source(&d, 0, "text/plain;charset=utf-8");
                serial = clip_serial(&d);
                wlc_put(b2, d.device);
                wlc_put(b2 + 4, WLQ_wl_data_device_set_selection | 16UL << 16);
                wlc_put(b2 + 8, copy);
                wlc_put(b2 + 12, serial);
                wlc_put(b2 + 16, copy);
                wlc_put(b2 + 20, WLQ_wl_data_source_destroy | 8UL << 16);
                wayland_feed(d.s.c->slot, b2, 24, 0, 0);
                wayland_out(d.s.c->slot, &queued, &fds, &count);
                fd = count == 1 ? fds[0] : -1;
                // The number in the queue is the pipe's write end: a byte written to it comes out of the read end the host kept.
                same = fd >= 0 && fake.pull_rfd > 0 && write((int)fd, "x", 1) == 1 && read(fake.pull_rfd, mark, 1) == 1 && mark[0] == 'x';
                WLCHECK(fake.aborts == aborts + 1 && same,
                        "a copy ended before its send event went out leaves the pipe's end in the queue, open%s (%ld queued, %ld aborts)",
                        client_goes ? " (its client then goes)" : "", count, fake.aborts - aborts);
                if (!client_goes)
                {
                        wlc_pump(d.s.c);
                        WLCHECK(fd >= 0 && fcntl((int)fd, F_GETFD) < 0, "and the shim closes it once it is sent");
                }
                wlc_end(d.s.c);
                if (client_goes)
                        WLCHECK(fd >= 0 && fcntl((int)fd, F_GETFD) < 0, "and the shim closes it when its client goes");
        }

        wlc_end(ca);
        wlc_end(cb);
        leak_check("clipboard");
        WLCHECK(fds_balanced(), "every descriptor of the clipboard rows was closed (%ld %ld)", fake.fds_open, fake.fds_closed);

        /* what a client may not do */
        wl_reset();
        wayland_start(&fake_host);
        clip_open(&k);
        wlc_send(k.s.c, k.device, WLQ_wl_data_device_set_selection, clip_source(&k, 0, "text/plain"), 0L);
        WLCHECK(k.s.c->errors == 0, "a copy with no gesture is cancelled and no error");
        {
                unsigned long used;

                wayland_keyboard_to(k.row);
                serial = clip_serial(&k);
                used = clip_source(&k, 0, "text/plain");
                clip_set(&k, 0, used, serial);
                clip_set(&k, 0, used, serial);
                WLCHECK(k.s.c->errors == 0 && fake.pulls == 1,
                        "a source that is being read, set again, is no change (a toolkit does after adding a type) (%ld pulls, %s)", fake.pulls,
                        k.s.c->error_text);
                clip_finish(&k, 0, "once");
                clip_set(&k, 0, used, serial);
                WLCHECK(k.s.c->errors == 0 && fake.pulls == 1, "and so is the source that is the selection");
                clip_other(0, "other");
                clip_set(&k, 0, used, serial);
                WLCHECK(refused_with(k.s.c, WLN_wl_data_device_error_used_source), "but a source that was cancelled is used up");
        }
        wlc_end(k.s.c);
        clip_open(&k);
        for (int at = 0; at < 100; at++)
                clip_source(&k, at & 1, "text/plain");
        WLCHECK(k.s.c->errors == 0, "a client holds as many sources as it makes: a hundred (%s)", k.s.c->errors ? k.s.c->error_text : "none refused");
        wlc_end(k.s.c);
        clip_open(&k);
        wlc_send(k.s.c, clip_source(&k, 0, "text/plain"), WLQ_wl_data_source_set_actions, 8L);
        WLCHECK(refused_with(k.s.c, WLN_wl_data_source_error_invalid_action_mask), "actions beyond the three are refused");
        wlc_end(k.s.c);
        clip_open(&k);
        wayland_keyboard_to(k.row);
        clip_other(0, "offered");
        wlc_forget(k.s.c);
        for (int at = 0; at < 22; at++)
                clip_other(0, "offered");
        wlc_pump(k.s.c);
        WLCHECK(wlc_count(k.s.c, WLI_wl_data_device, WLE_wl_data_device_data_offer) >= 22 && k.s.c->errors == 0,
                "a client that keeps its offers is given every one, each a selection (%ld)", wlc_count(k.s.c, WLI_wl_data_device, WLE_wl_data_device_data_offer));
        {
                long last = 0;

                for (long at = 0; at < k.s.c->events; at++)
                        if (k.s.c->ev[at].iface == WLI_wl_data_device && k.s.c->ev[at].opcode == WLE_wl_data_device_selection)
                                last = (long)k.s.c->ev[at].u[0];
                WLCHECK(last != 0, "and told of the last as the selection");
        }
        wlc_end(k.s.c);
        /* a client that goes while its copy is being read: the host is told, and everything comes back */
        clip_open(&k);
        wayland_keyboard_to(k.row);
        serial = clip_serial(&k);
        source = clip_source(&k, 0, "text/plain");
        clip_set(&k, 0, source, serial);
        pulls = fake.aborts;
        wlc_end(k.s.c);
        WLCHECK(fake.aborts == pulls + 1, "a client that goes in the middle of a copy ends the read");
        wayland_clip_pulled(0, 0, 1);
        fake_pull_close();
        leak_check("clipboard rules");
        WLCHECK(fds_balanced(), "every descriptor of the rules was closed (%ld %ld)", fake.fds_open, fake.fds_closed);
        wayland_stop();
}

/*
        ---- the bridge to X: what the shim gives the window manager of Xwayland (xwm.c) and takes from it ----

        An X copy's types past text offered with the selection (and alone, with no text), a receive of one handed to the X side; the
        Wayland source that is the selection, its types and its send, for an X client's conversion; a Wayland drag over an X window (a
        client with no data device) offered to the X side, its status, drop, send and finish; Escape over it; and an X drag over a
        Wayland window: its types and actions offered, its answer, drop, receive and finish, and its leave.
*/
static struct
{
        long receives, client, which, fd_ok, accept, end_ok, end_action, row, x, y;
        long drags[5];
        char mime[64];
} fx;

static void fake_xreceive(void *context, long client, long which, const char *mime, long fd)
{
        (void)context;
        fx.receives++;
        fx.client = client, fx.which = which;
        snprintf(fx.mime, sizeof fx.mime, "%s", mime);
        fx.fd_ok = fcntl((int)fd, F_GETFD) >= 0;
        fake_close(NULL, fd);
}

static long fake_xdrag(void *context, long what, long row, long x, long y)
{
        (void)context;
        if (what >= 1 && what <= 4)
                fx.drags[what]++;
        if (what == WAYLAND_XDRAG_OVER)
                fx.row = row, fx.x = x, fx.y = y;
        if (what == WAYLAND_XDRAG_END)
                fx.end_ok = x, fx.end_action = y;
        return what == WAYLAND_XDRAG_OVER ? fx.accept : 0;
}

static long wl_offered(struct wlc *c, const char *type)
{
        long n = 0;

        for (long at = 0; at < c->events; at++)
                n += c->ev[at].iface == WLI_wl_data_offer && c->ev[at].opcode == WLE_wl_data_offer_offer && !strcmp(c->ev[at].text[0], type);
        return n;
}

static void test_bridge(void)
{
        struct wayland_host host = fake_host;
        struct clipc a;
        struct session xs;
        struct wl_event_seen *e, *chosen;
        unsigned long serial, source, offer;
        char text[64];
        long xrow = -1, before, write_end;
        int reader;

        host.xreceive = fake_xreceive;
        host.xdrag = fake_xdrag;
        memset(&fx, 0, sizeof fx);
        wl_reset();
        wayland_start(&host);
        clip_open(&a);
        wayland_keyboard_to(a.row);
        wlc_pump(a.s.c);

        /* a copy of any types (an X client's, read into the compositor's): offered as its text and its other types, each pasted by the host */
        wlc_forget(a.s.c);
        memcpy(fake.clip_types[0], "image/png\0text/uri-list\0", 24);
        fake.clip_types_bytes[0] = 24;
        clip_other(0, "copied in X");
        wlc_pump(a.s.c);
        offer = clip_offer_of(a.s.c, 0, &chosen);
        WLCHECK(offer && chosen && chosen->u[0] == offer && wlc_count(a.s.c, WLI_wl_data_offer, WLE_wl_data_offer_offer) == 5 &&
                    wl_offered(a.s.c, "image/png") == 1 && wl_offered(a.s.c, "text/uri-list") == 1 && wl_offered(a.s.c, "text/plain") == 1,
                "a copy is offered as its three text types and its two others (%ld types)", wlc_count(a.s.c, WLI_wl_data_offer, WLE_wl_data_offer_offer));
        before = fake.pushes;
        WLCHECK(clip_paste(&a, 0, offer, "image/png", text, sizeof text) == 11 && fake.pushes == before + 1 && !strcmp(fake.push_mime, "image/png") &&
                    fake.push_row == a.row && fx.receives == 0,
                "a receive of its image/png is the host's paste of that type, for the window with the keys (none of it the X side's)");
        before = fake.pushes;
        WLCHECK(clip_paste(&a, 0, offer, "image/jpeg", text, sizeof text) == 0 && fake.pushes == before, "a type the copy has not is nothing");
        WLCHECK(clip_paste(&a, 0, offer, "text/plain;charset=utf-8", text, sizeof text) == 11 && !strcmp(text, "copied in X"),
                "its text is the compositor's, as any copy's");
        wlc_forget(a.s.c);
        fake.clip_size[0] = 0;
        wayland_selection_changed(0);
        wlc_pump(a.s.c);
        offer = clip_offer_of(a.s.c, 0, &chosen);
        WLCHECK(offer && wlc_count(a.s.c, WLI_wl_data_offer, WLE_wl_data_offer_offer) == 2 && wl_offered(a.s.c, "image/png") == 1,
                "a copy with no text is offered as its own types alone");
        wlc_forget(a.s.c);
        fake.clip_types_bytes[0] = 0;
        wayland_selection_changed(0);
        wlc_pump(a.s.c);
        offer = clip_offer_of(a.s.c, 0, &chosen);
        WLCHECK(!offer && chosen && chosen->u[0] == 0, "and none at all once there is no copy");

        /* a Wayland source read into the compositor's copy a type at a time: each type it offered, whole, asked for in turn */
        serial = clip_serial(&a);
        source = clip_source(&a, 0, "text/plain;charset=utf-8");
        wlc_send(a.s.c, source, WLQ_wl_data_source_offer, "image/png");
        wlc_forget(a.s.c);
        clip_set(&a, 0, source, serial);
        wlc_pump(a.s.c);
        e = wlc_take(a.s.c, WLI_wl_data_source, WLE_wl_data_source_send);
        WLCHECK(e && !strcmp(e->text[0], "text/plain;charset=utf-8") && wayland_clip_type(a.s.c->slot, 0, 0) &&
                    !strcmp(wayland_clip_type(a.s.c->slot, 0, 0), "text/plain;charset=utf-8") && wayland_clip_type(a.s.c->slot, 0, 1) &&
                    !strcmp(wayland_clip_type(a.s.c->slot, 0, 1), "image/png") && !wayland_clip_type(a.s.c->slot, 0, 2),
                "the source being read has its types, each whole, for the host to ask for in turn");
        write_end = wl_pipe(&reader);
        wlc_forget(a.s.c);
        WLCHECK(wayland_clip_send(a.s.c->slot, 0, 1, write_end) == 1, "the host asks for its image/png");
        wlc_pump(a.s.c);
        e = wlc_take(a.s.c, WLI_wl_data_source, WLE_wl_data_source_send);
        WLCHECK(e && e->fd_count == 1 && !strcmp(e->text[0], "image/png"), "with the type and the descriptor");
        close(reader);
        clip_finish(&a, 0, "with a picture");
        WLCHECK(!wayland_clip_type(a.s.c->slot, 0, 0), "a source read whole has no types left to ask for");
        write_end = wl_pipe(&reader);
        before = fake.fds_closed;
        WLCHECK(wayland_clip_send(a.s.c->slot, 0, 1, write_end) == 0 && fake.fds_closed == before + 1,
                "and a send asked of it is refused, its descriptor closed");
        close(reader);
        clip_other(0, "another");

        /* a Wayland drag over an X window: a client with a window and no data device, as Xwayland is */
        session_open(&xs);
        session_window(&xs, 0xff204060);
        for (long row = 0; row < FAKE_ROWS; row++)
                if (fake.row[row].used && wayland_row_owner(row) == xs.c->slot)
                        xrow = row;
        source = clip_source(&a, 0, "application/x-moonwater-test");
        wlc_send(a.s.c, source, WLQ_wl_data_source_set_actions, 3L);
        wayland_pointer_to(a.row, 10, 10);
        wayland_pointer_button(a.row, 272, 1);
        wlc_pump(a.s.c);
        wlc_send(a.s.c, a.device, WLQ_wl_data_device_start_drag, source, a.s.surface, 0UL, (long)wayland.client[a.s.c->slot]->press_serial);
        wlc_pump(a.s.c);
        wlc_forget(a.s.c);
        fx.accept = 1;
        wayland_drag_motion(xrow, 20, 30);
        wlc_pump(a.s.c);
        WLCHECK(xrow >= 0 && fx.drags[WAYLAND_XDRAG_OVER] == 1 && fx.row == xrow && fx.x == 20 && fx.y == 30 && wayland.drag.xtarget == xrow &&
                    wlc_count(a.s.c, WLI_wl_data_device, WLE_wl_data_device_leave) == 1,
                "a drag over an X window is offered to the X side at the place, and the window it was over is left");
        {
                const char *types = 0;
                unsigned int actions = 0;
                long bytes = wayland_drag_x_types(&types, &actions);

                WLCHECK(bytes == 29 && types && !strcmp(types, "application/x-moonwater-test") && actions == 3,
                        "the drag's types are whole, each with its null (the last letter was lost), and its actions (%ld bytes)", bytes);
        }
        wlc_forget(a.s.c);
        wayland_drag_x_status(1, 1);
        wlc_pump(a.s.c);
        e = wlc_take(a.s.c, WLI_wl_data_source, WLE_wl_data_source_target);
        WLCHECK(e && !strcmp(e->text[0], "application/x-moonwater-test") && wlc_count(a.s.c, WLI_wl_data_source, WLE_wl_data_source_action) == 1,
                "what the X window takes is the source's target (its first type) and action");
        wayland_drag_motion(xrow, 25, 30);
        WLCHECK(fx.drags[WAYLAND_XDRAG_OVER] == 2 && fx.x == 25, "a motion over it is the X side's");
        wlc_forget(a.s.c);
        wayland_pointer_button(a.row, 272, 0);
        wlc_pump(a.s.c);
        WLCHECK(fx.drags[WAYLAND_XDRAG_DROP] == 1 && wlc_count(a.s.c, WLI_wl_data_source, WLE_wl_data_source_dnd_drop_performed) == 1 && !wayland_dragging(),
                "let go over it: the X side drops, the source is told the drop was performed");
        write_end = wl_pipe(&reader);
        wlc_forget(a.s.c);
        WLCHECK(wayland_drag_x_send("application/x-moonwater-test", write_end) == 1, "the X window's conversion is the source's send");
        wlc_pump(a.s.c);
        e = wlc_take(a.s.c, WLI_wl_data_source, WLE_wl_data_source_send);
        WLCHECK(e && e->fd_count == 1 && !strcmp(e->text[0], "application/x-moonwater-test"), "with the type and the descriptor");
        close(reader);
        wlc_forget(a.s.c);
        wayland_drag_x_finished(1);
        wlc_pump(a.s.c);
        WLCHECK(wlc_count(a.s.c, WLI_wl_data_source, WLE_wl_data_source_dnd_finished) == 1 && fx.drags[WAYLAND_XDRAG_END] == 1 && fx.end_ok == 1 &&
                    !wayland.drag.live,
                "the X window's finish is the source's dnd_finished, and the X side is told the drag ended finished");
        wlc_send(a.s.c, source, WLQ_wl_data_source_destroy);

        /* Escape over the X window: it is left, and the source cancelled */
        source = clip_source(&a, 0, "text/plain");
        wayland_pointer_button(a.row, 272, 1);
        wlc_pump(a.s.c);
        wlc_send(a.s.c, a.device, WLQ_wl_data_device_start_drag, source, a.s.surface, 0UL, (long)wayland.client[a.s.c->slot]->press_serial);
        wlc_pump(a.s.c);
        wayland_drag_motion(xrow, 5, 5);
        wlc_forget(a.s.c);
        before = fx.drags[WAYLAND_XDRAG_LEAVE];
        wayland_drag_cancel();
        wlc_pump(a.s.c);
        WLCHECK(fx.drags[WAYLAND_XDRAG_LEAVE] == before + 1 && clip_cancelled(a.s.c, 0) == 1 && fx.end_ok == 0 && !wayland.drag.live,
                "Escape over an X window: the X side told the drag left it, the source cancelled, the end not finished");
        wayland_pointer_button(a.row, 272, 0);
        wlc_send(a.s.c, source, WLQ_wl_data_source_destroy);

        /* an X drag over a Wayland window */
        WLCHECK(wayland_drag_x_begin("text/plain;charset=utf-8\0image/png\0", 35, 3) == 1 && !wayland_dragging(),
                "an X drag begins, and the pointer stays the X client's (its button and motion are Xwayland's)");
        WLCHECK(wayland_drag_x_begin("text/plain\0", 11, 1) == 0, "and no other drag begins while it is");
        WLCHECK(wayland_x_dragging() && !wayland_dragging(), "an X drag over the Wayland side is one the host's Escape ends");
        wlc_forget(a.s.c);
        wayland_drag_x_motion(a.row, 12, 14);
        wlc_pump(a.s.c);
        e = wlc_take(a.s.c, WLI_wl_data_device, WLE_wl_data_device_enter);
        offer = e ? e->u[4] : 0;
        WLCHECK(e && offer && wlc_count(a.s.c, WLI_wl_data_offer, WLE_wl_data_offer_offer) == 2 && wl_offered(a.s.c, "image/png") == 1 &&
                    wlc_count(a.s.c, WLI_wl_data_offer, WLE_wl_data_offer_source_actions) == 1,
                "the Wayland window under it is entered with an offer of the X source's types and actions");
        {
                unsigned long action = 7;

                WLCHECK(wayland_drag_x_answer(&action) == 0 && action == 0, "nothing taken before the target says");
                wlc_send(a.s.c, offer, WLQ_wl_data_offer_accept, e ? e->u[0] : 0UL, "text/plain;charset=utf-8");
                wlc_send(a.s.c, offer, WLQ_wl_data_offer_set_actions, 1L, 1L);
                wlc_pump(a.s.c);
                WLCHECK(wayland_drag_x_answer(&action) == 1 && action == 1, "what the target takes (a type, copy) is the X source's status");
                // The X source asks now (XdndActionAsk, its XdndActionList copy and move): the offer is told, and what the target prefers taken.
                wlc_forget(a.s.c);
                wayland_drag_x_actions(4 | 1 | 2);
                wlc_pump(a.s.c);
                e = wlc_take(a.s.c, WLI_wl_data_offer, WLE_wl_data_offer_source_actions);
                WLCHECK(e && e->u[0] == 7 && wayland.drag.x_actions == 7, "an X source's actions that change are its offer's source_actions again");
                wlc_send(a.s.c, offer, WLQ_wl_data_offer_set_actions, 7L, 4L);
                wlc_pump(a.s.c);
                WLCHECK(wayland_drag_x_answer(&action) == 1 && action == 4, "and a target that prefers to ask the person is ask to the X source");
                wlc_send(a.s.c, offer, WLQ_wl_data_offer_set_actions, 3L, 2L);
                wlc_pump(a.s.c);
                WLCHECK(wayland_drag_x_answer(&action) == 1 && action == 2, "then the move it chose");
        }
        wlc_forget(a.s.c);
        wayland_drag_x_drop();
        wlc_pump(a.s.c);
        WLCHECK(wlc_count(a.s.c, WLI_wl_data_device, WLE_wl_data_device_drop) == 1, "the X source's drop is the target's");
        write_end = wl_pipe(&reader);
        before = fx.receives;
        wlc_send(a.s.c, offer, WLQ_wl_data_offer_receive, "image/png", write_end);
        WLCHECK(fx.receives == before + 1 && fx.which == 2 && !strcmp(fx.mime, "image/png") && fx.fd_ok && fx.client == a.s.c->slot,
                "the target's receive is the X side's, for the drag, with the type and an open descriptor");
        close(reader);
        before = fx.drags[WAYLAND_XDRAG_END];
        wlc_send(a.s.c, offer, WLQ_wl_data_offer_finish);
        wlc_pump(a.s.c);
        WLCHECK(fx.drags[WAYLAND_XDRAG_END] == before + 1 && fx.end_ok == 1 && fx.end_action == 2 && !wayland.drag.live,
                "the target's finish ends the X drag, finished, with the action it came to (move), the one XdndFinished names");
        WLCHECK(!wayland_x_dragging(), "and none is dragging after");
        wlc_send(a.s.c, offer, WLQ_wl_data_offer_destroy);
        wayland_drag_x_begin("text/plain\0", 11, 1);
        wayland_drag_x_motion(a.row, 3, 3);
        wlc_pump(a.s.c);
        wlc_forget(a.s.c);
        wayland_drag_x_leave();
        wlc_pump(a.s.c);
        WLCHECK(wlc_count(a.s.c, WLI_wl_data_device, WLE_wl_data_device_leave) == 1 && !wayland.drag.live && fx.end_ok == 0,
                "an X drag that leaves: the Wayland window left, the end not finished");

        /* a program's drag (the host's, its pointer the compositor's): over a Wayland window, dropped, received, finished; over a program's */
        WLCHECK(wayland_drag_host_begin("text/uri-list\0text/plain;charset=utf-8\0", 39, 3, -1, 0, 0) == 1 && wayland_dragging() && !wayland_x_dragging(),
                "a program's drag begins and holds the pointer (its motion and its release are the compositor's), and is no X drag");
        WLCHECK(wayland_drag_host_begin("text/plain\0", 11, 1, -1, 0, 0) == 0, "and no other drag begins while it is");
        wlc_forget(a.s.c);
        wayland_drag_motion(a.row, 20, 20);
        wlc_pump(a.s.c);
        e = wlc_take(a.s.c, WLI_wl_data_device, WLE_wl_data_device_enter);
        offer = e ? e->u[4] : 0;
        WLCHECK(e && offer && wlc_count(a.s.c, WLI_wl_data_offer, WLE_wl_data_offer_offer) == 2 && wl_offered(a.s.c, "text/uri-list") == 1,
                "over a Wayland window it is entered with an offer of the program's types");
        wlc_send(a.s.c, offer, WLQ_wl_data_offer_accept, e ? e->u[0] : 0UL, "text/uri-list");
        wlc_send(a.s.c, offer, WLQ_wl_data_offer_set_actions, 3L, 2L);
        wlc_pump(a.s.c);
        wlc_forget(a.s.c);
        wayland_drag_host_release();
        wlc_pump(a.s.c);
        WLCHECK(wlc_count(a.s.c, WLI_wl_data_device, WLE_wl_data_device_drop) == 1 && !wayland_dragging() && wayland.drag.action == 2,
                "its release over the window that took a type is the drop, with the move the window preferred");
        write_end = wl_pipe(&reader);
        before = fx.receives;
        wlc_send(a.s.c, offer, WLQ_wl_data_offer_receive, "text/uri-list", write_end);
        WLCHECK(fx.receives == before + 1 && fx.which == 2 && !strcmp(fx.mime, "text/uri-list") && fx.fd_ok,
                "the window's receive is the host's to answer from the program's copy");
        close(reader);
        before = fx.drags[WAYLAND_XDRAG_END];
        wlc_send(a.s.c, offer, WLQ_wl_data_offer_finish);
        wlc_pump(a.s.c);
        WLCHECK(fx.drags[WAYLAND_XDRAG_END] == before + 1 && fx.end_ok == 1 && fx.end_action == 2 && !wayland.drag.live,
                "and its finish the drag's end, told to the host finished, with its move");
        wlc_send(a.s.c, offer, WLQ_wl_data_offer_destroy);
        {
                long program = FAKE_ROWS - 1; // a row no client has: a program's of the compositor's

                wayland_drag_host_begin("text/plain;charset=utf-8\0", 25, 1, -1, 0, 0);
                fx.accept = 1;
                before = fx.drags[WAYLAND_XDRAG_OVER];
                wayland_drag_motion(program, 40, 50);
                WLCHECK(fx.drags[WAYLAND_XDRAG_OVER] == before + 1 && fx.row == program && fx.x == 40 && fx.y == 50 && wayland.drag.xtarget == program,
                        "over a program's window (no client's row) the host is asked, and is the target when it says so");
                wayland_drag_x_status(1, 1);
                before = fx.drags[WAYLAND_XDRAG_DROP];
                wayland_drag_host_release();
                WLCHECK(fx.drags[WAYLAND_XDRAG_DROP] == before + 1 && wayland.drag.dropped, "the program's take and the release are its drop, told to the host");
                before = fx.drags[WAYLAND_XDRAG_END];
                wayland_drag_x_finished(1);
                WLCHECK(fx.drags[WAYLAND_XDRAG_END] == before + 1 && fx.end_ok == 1 && !wayland.drag.live, "and its done the drag's end, finished");
                wayland_drag_host_begin("text/plain;charset=utf-8\0", 25, 1, -1, 0, 0);
                wayland_drag_motion(program, 4, 5);
                before = fx.drags[WAYLAND_XDRAG_END];
                wayland_drag_cancel();
                WLCHECK(fx.drags[WAYLAND_XDRAG_END] == before + 1 && fx.end_ok == 0 && !wayland.drag.live && fx.drags[WAYLAND_XDRAG_LEAVE] > 0,
                        "Escape (the host's cancel) ends a program's drag, left and not finished");
                fx.accept = 0;
        }
        WLCHECK(a.s.c->errors == 0 && xs.c->errors == 0, "no error of these (%s)", a.s.c->error_text);
        wlc_end(a.s.c);
        wlc_end(xs.c);
        leak_check("bridge");
        WLCHECK(fds_balanced(), "every descriptor of the bridge rows was closed (%ld %ld)", fake.fds_open, fake.fds_closed);
        wayland_stop();
}

/*
        Copies, pastes, focus changes and the compositor's own copies in random order between two clients, with offers kept
        and let go and sources destroyed where they stand: nothing may fail, no event be malformed, and everything (descriptors
        too) come back.
*/
static void test_clip_fuzz(long rounds)
{
        struct clipc k[2];
        unsigned long source[2][4], offer[2][6];
        int open = 0;
        long malformed = 0, copies = 0, pastes = 0;

        wl_reset();
        wayland_start(&fake_host);
        for (long round = 0; round < rounds; round++)
        {
                long who = (long)(wl_random() & 1), slot = (long)(wl_random() % 4), primary = (long)(wl_random() & 1);
                struct clipc *c;
                struct wlc *wc;

                if (round % 300 == 0 || !open || wayland_dead(k[0].s.c->slot) || wayland_dead(k[1].s.c->slot))
                {
                        if (open)
                        {
                                malformed += k[0].s.c->malformed + k[1].s.c->malformed;
                                wlc_end(k[0].s.c);
                                wlc_end(k[1].s.c);
                        }
                        clip_open(&k[0]);
                        clip_open(&k[1]);
                        memset(source, 0, sizeof source);
                        memset(offer, 0, sizeof offer);
                        open = 1;
                }
                c = &k[who];
                wc = c->s.c;
                switch (wl_random() % 12)
                {
                case 0:
                        wayland_keyboard_to(c->row);
                        break;
                case 1:
                case 2:
                        if (!source[who][slot])
                                source[who][slot] = clip_source(c, (int)primary, wl_random() % 8 ? "text/plain;charset=utf-8" : "image/png");
                        break;
                case 3:
                case 4:
                        if (source[who][slot] && wl_random() % 4)
                        {
                                unsigned long serial = wayland.keyboard_row == c->row ? clip_serial(c) : (unsigned long)wl_random();

                                clip_set(c, (int)primary, source[who][slot], serial);
                                copies++;
                        }
                        break;
                case 5:
                        wlc_end(wc);
                        clip_open(c);
                        source[who][0] = source[who][1] = source[who][2] = source[who][3] = 0;
                        memset(offer[who], 0, sizeof offer[who]);
                        break;
                case 6:
                        if (wl_random() & 1)
                                clip_finish(c, (int)primary, "fuzz text");
                        else
                                clip_other((int)primary, wl_random() & 1 ? "other" : "");
                        if ((wl_random() & 3) == 0)
                                fake.clip_size[primary] = 0;
                        break;
                case 7:
                        if (source[who][slot])
                        {
                                wlc_send(wc, source[who][slot], primary ? WLQ_zwp_primary_selection_source_v1_destroy : WLQ_wl_data_source_destroy);
                                source[who][slot] = 0;
                        }
                        break;
                case 8:
                {
                        /* the newest offer of a kind, pasted or let go */
                        long found = -1;
                        unsigned long id = 0;

                        for (long at = wc->events - 1; at >= 0 && found < 0; at--)
                                if (wc->ev[at].iface == (primary ? WLI_zwp_primary_selection_device_v1 : WLI_wl_data_device) &&
                                    wc->ev[at].opcode == (primary ? WLE_zwp_primary_selection_device_v1_data_offer : WLE_wl_data_device_data_offer))
                                {
                                        found = at;
                                        id = wc->ev[at].u[0];
                                }
                        if (id && wl_random() % 3)
                        {
                                char text[64];

                                clip_paste(c, (int)primary, id, wl_random() % 4 ? "UTF8_STRING" : "x/y", text, sizeof text);
                                pastes++;
                        }
                        else if (id)
                        {
                                wlc_send(wc, id, primary ? WLQ_zwp_primary_selection_offer_v1_destroy : WLQ_wl_data_offer_destroy);
                                wc->ev[found].u[0] = 0;
                        }
                        break;
                }
                case 9:
                        if (wl_random() & 1)
                                clip_set(c, (int)primary, 0UL, 0UL);
                        else if (source[who][slot])
                                wayland_clip_pulled(wc->slot, primary, (long)(wl_random() & 1));
                        break;
                case 10:
                        fake.pull_refuse = (long)(wl_random() % 4 == 0);
                        break;
                default:
                        wlc_forget(wc);
                        break;
                }
                if (round % 5 == 0)
                {
                        wlc_pump(k[0].s.c);
                        wlc_pump(k[1].s.c);
                }
        }
        if (open)
        {
                malformed += k[0].s.c->malformed + k[1].s.c->malformed;
                wlc_end(k[0].s.c);
                wlc_end(k[1].s.c);
        }
        fake_pull_close();
        WLCHECK(malformed == 0, "every event of the clipboard fuzz was well-formed (%ld malformed)", malformed);
        WLCHECK(copies > 0 && pastes > 0, "the clipboard fuzz copied and pasted (%ld, %ld)", copies, pastes);
        leak_check("clipboard fuzz");
        WLCHECK(fds_balanced(), "every descriptor of the clipboard fuzz was closed (%ld %ld)", fake.fds_open, fake.fds_closed);
        printf("CHECK_wayland clipboard fuzz: %ld rounds, %ld copies, %ld pastes\n", rounds, copies, pastes);
        wayland_stop();
}

/* A buffer whose pixel at (x, y) says so: 0xff000000 | y << 8 | x. */
static unsigned long wl_buffer_pattern(struct session *t, long width, long height)
{
        struct wlc *c = t->c;
        long bytes = width * height * 4, fd = wl_file(bytes, 0);
        unsigned long pool = wlc_id(c, WLI_wl_shm_pool, 1), buffer = wlc_id(c, WLI_wl_buffer, 1);
        unsigned *pixels = malloc((size_t)bytes);

        for (long y = 0; y < height; y++)
                for (long x = 0; x < width; x++)
                        pixels[y * width + x] = 0xff000000u | (unsigned)(y << 8) | (unsigned)x;
        wl_file_put(fd, 0, pixels, bytes);
        free(pixels);
        wlc_send(c, t->shm, WLQ_wl_shm_create_pool, pool, fd, bytes);
        close((int)fd);
        wlc_send(c, pool, WLQ_wl_shm_pool_create_buffer, buffer, 0L, width, height, width * 4, (long)WLN_wl_shm_format_xrgb8888);
        wlc_send(c, pool, WLQ_wl_shm_pool_destroy);
        return buffer;
}

#define PATTERN(x, y) (0xff000000u | (unsigned)((y) << 8) | (unsigned)(x))

/* The window is its geometry: the margin a toolkit draws round it is not shown, and what is pointed at and placed by is the geometry's. */
/*
        A window opaque but for its round top corners (Chromium's opaque region: a band of the top rows less the corners, then the rest of the
        window under it, of a buffer with a shadow round it): the picture is not opaque, and the host is told the rectangle of it that is, in the
        window's pixels, so it may show that part unblended (an overlay plane). The region's rectangle is the larger of the two and not the band
        joined to the body at the band's width, which narrowed it at both sides; a region of the buffer outside the window is cut to the window.
*/
static void test_opaque_rect(void)
{
        struct session s;
        struct wlc *c;
        unsigned long region, buffer;

        wl_reset();
        wayland_start(&fake_host);
        session_open(&s);
        session_window(&s, 0xff000000);
        c = s.c;
        region = wlc_id(c, WLI_wl_region, 1);
        wlc_send(c, s.xdg, WLQ_xdg_surface_set_window_geometry, 4L, 2L, 32L, 26L);
        wlc_send(c, s.compositor, WLQ_wl_compositor_create_region, region);
        wlc_send(c, region, WLQ_wl_region_add, 6L, 2L, 28L, 2L);
        wlc_send(c, region, WLQ_wl_region_add, 4L, 4L, 32L, 24L);
        wlc_send(c, s.surface, WLQ_wl_surface_set_opaque_region, region);
        wlc_send(c, region, WLQ_wl_region_destroy);
        buffer = wl_buffer_of(&s, 40, 30, 0xff336699u, WLN_wl_shm_format_argb8888);
        wlc_send(c, s.surface, WLQ_wl_surface_attach, buffer, 0L, 0L);
        wlc_send(c, s.surface, WLQ_wl_surface_damage_buffer, 0L, 0L, 40L, 30L);
        wlc_send(c, s.surface, WLQ_wl_surface_commit);
        WLCHECK(c->errors == 0 && fake.row[0].picture_w == 32 && fake.row[0].picture_h == 26 && fake.row[0].transparent,
                "a window with round top corners outside its opaque region is not opaque (%ldx%ld, %ld)", fake.row[0].picture_w,
                fake.row[0].picture_h, fake.row[0].transparent);
        WLCHECK(fake.row[0].opaque[0] == 0 && fake.row[0].opaque[1] == 2 && fake.row[0].opaque[2] == 32 && fake.row[0].opaque[3] == 24,
                "and is told opaque below its corners' rows, all its width, in its own pixels (%ld %ld %ld %ld)", fake.row[0].opaque[0],
                fake.row[0].opaque[1], fake.row[0].opaque[2], fake.row[0].opaque[3]);

        /* a region past the window (over its shadow) is cut to it; a window all opaque is told no rectangle */
        region = wlc_id(c, WLI_wl_region, 1);
        wlc_send(c, s.compositor, WLQ_wl_compositor_create_region, region);
        wlc_send(c, region, WLQ_wl_region_add, 0L, 6L, 40L, 10L);
        wlc_send(c, s.surface, WLQ_wl_surface_set_opaque_region, region);
        wlc_send(c, region, WLQ_wl_region_destroy);
        buffer = wl_buffer_of(&s, 40, 30, 0xff336699u, WLN_wl_shm_format_argb8888);
        wlc_send(c, s.surface, WLQ_wl_surface_attach, buffer, 0L, 0L);
        wlc_send(c, s.surface, WLQ_wl_surface_damage_buffer, 0L, 0L, 40L, 30L);
        wlc_send(c, s.surface, WLQ_wl_surface_commit);
        WLCHECK(fake.row[0].transparent && fake.row[0].opaque[0] == 0 && fake.row[0].opaque[1] == 4 && fake.row[0].opaque[2] == 32 &&
                    fake.row[0].opaque[3] == 10,
                "an opaque region over the shadow is cut to the window (%ld %ld %ld %ld)", fake.row[0].opaque[0], fake.row[0].opaque[1],
                fake.row[0].opaque[2], fake.row[0].opaque[3]);
        buffer = wl_buffer_of(&s, 40, 30, 0xff336699u, WLN_wl_shm_format_xrgb8888);
        wlc_send(c, s.surface, WLQ_wl_surface_attach, buffer, 0L, 0L);
        wlc_send(c, s.surface, WLQ_wl_surface_damage_buffer, 0L, 0L, 40L, 30L);
        wlc_send(c, s.surface, WLQ_wl_surface_commit);
        WLCHECK(!fake.row[0].transparent && fake.row[0].opaque[2] == 0, "an opaque window is told no rectangle (%ld, %ld)", fake.row[0].transparent,
                fake.row[0].opaque[2]);
        wlc_end(c);
}

static void test_geometry(void)
{
        struct session s;
        struct wlc *c;
        struct wl_event_seen *e;
        unsigned long buffer, pointer;

        wl_reset();
        wayland_start(&fake_host);
        session_open(&s);
        session_window(&s, 0xff000000);
        c = s.c;
        pointer = wlc_id(c, WLI_wl_pointer, 9);
        wlc_send(c, s.seat, WLQ_wl_seat_get_pointer, pointer);

        /* a geometry of 20 by 10 at (4, 5) in a picture of 32 by 24: the row is that part of it */
        wlc_send(c, s.xdg, WLQ_xdg_surface_set_window_geometry, 4L, 5L, 20L, 10L);
        buffer = wl_buffer_pattern(&s, 32, 24);
        wlc_send(c, s.surface, WLQ_wl_surface_attach, buffer, 0L, 0L);
        wlc_send(c, s.surface, WLQ_wl_surface_damage_buffer, 0L, 0L, 32L, 24L);
        wlc_send(c, s.surface, WLQ_wl_surface_commit);
        WLCHECK(c->errors == 0 && fake.row[0].picture_w == 20 && fake.row[0].picture_h == 10,
                "the window is the size of its geometry (%ldx%ld, %s)", fake.row[0].picture_w, fake.row[0].picture_h, c->error_text);
        WLCHECK(fake.row[0].pixels[0] == PATTERN(4, 5) && fake.row[0].pixels[9 * FAKE_W + 19] == PATTERN(23, 14),
                "and begins at the geometry's corner and ends at its other (%08x, %08x)", fake.row[0].pixels[0],
                fake.row[0].pixels[9 * FAKE_W + 19]);

        /* the pointer is told places of the surface, which are the row's and the geometry's corner */
        wlc_forget(c);
        wayland_pointer_to(0, 0, 0);
        wlc_pump(c);
        e = wlc_take(c, WLI_wl_pointer, WLE_wl_pointer_enter);
        WLCHECK(e && e->u[2] == 4 * 256 && e->u[3] == 5 * 256, "a pointer at the window's corner is at the geometry's corner of the surface (%ld %ld)",
                e ? (long)e->u[2] : -1, e ? (long)e->u[3] : -1);
        wlc_forget(c);
        wayland_pointer_motion(0, 3, 2);
        wlc_pump(c);
        e = wlc_take(c, WLI_wl_pointer, WLE_wl_pointer_motion);
        WLCHECK(e && e->u[1] == 7 * 256 && e->u[2] == 7 * 256, "and motion is told in the surface's places too");

        /* a geometry that changes with only a small part of the picture damaged is the whole window drawn again */
        wlc_send(c, s.xdg, WLQ_xdg_surface_set_window_geometry, 0L, 0L, 16L, 12L);
        buffer = wl_buffer_pattern(&s, 32, 24);
        wlc_send(c, s.surface, WLQ_wl_surface_attach, buffer, 0L, 0L);
        wlc_send(c, s.surface, WLQ_wl_surface_damage_buffer, 0L, 0L, 1L, 1L);
        wlc_send(c, s.surface, WLQ_wl_surface_commit);
        WLCHECK(fake.row[0].picture_w == 16 && fake.row[0].picture_h == 12 && fake.row[0].pixels[11 * FAKE_W + 15] == PATTERN(15, 11) &&
                    fake.row[0].pixels[0] == PATTERN(0, 0),
                "a new geometry is drawn whole, whatever was damaged (%ldx%ld, %08x)", fake.row[0].picture_w, fake.row[0].picture_h,
                fake.row[0].pixels[11 * FAKE_W + 15]);

        /* a geometry that is past the picture, or has none of it, is the picture */
        wlc_send(c, s.xdg, WLQ_xdg_surface_set_window_geometry, 0L, 0L, 100L, 100L);
        buffer = wl_buffer_pattern(&s, 32, 24);
        wlc_send(c, s.surface, WLQ_wl_surface_attach, buffer, 0L, 0L);
        wlc_send(c, s.surface, WLQ_wl_surface_damage_buffer, 0L, 0L, 32L, 24L);
        wlc_send(c, s.surface, WLQ_wl_surface_commit);
        WLCHECK(fake.row[0].picture_w == 32 && fake.row[0].picture_h == 24, "a geometry past the picture is cut to the picture (%ldx%ld)",
                fake.row[0].picture_w, fake.row[0].picture_h);
        wlc_send(c, s.xdg, WLQ_xdg_surface_set_window_geometry, 40L, 40L, 10L, 10L);
        buffer = wl_buffer_pattern(&s, 32, 24);
        wlc_send(c, s.surface, WLQ_wl_surface_attach, buffer, 0L, 0L);
        wlc_send(c, s.surface, WLQ_wl_surface_damage_buffer, 0L, 0L, 32L, 24L);
        wlc_send(c, s.surface, WLQ_wl_surface_commit);
        WLCHECK(c->errors == 0 && fake.row[0].picture_w == 32 && fake.row[0].picture_h == 24,
                "a geometry with none of the picture in it is the picture (%ldx%ld)", fake.row[0].picture_w, fake.row[0].picture_h);
        wlc_end(c);

        /* a subsurface is placed by the surface's places, so it lands that much nearer the corner */
        {
                struct tree t;

                tree_open(&t, 1);
                c = t.s.c;
                wlc_send(c, t.s.xdg, WLQ_xdg_surface_set_window_geometry, 2L, 2L, 16L, 12L);
                buffer = wl_buffer_of(&t.s, 32, 24, 0xff202020, WLN_wl_shm_format_xrgb8888);
                wlc_send(c, t.s.surface, WLQ_wl_surface_attach, buffer, 0L, 0L);
                wlc_send(c, t.s.surface, WLQ_wl_surface_damage_buffer, 0L, 0L, 32L, 24L);
                wlc_send(c, t.s.surface, WLQ_wl_surface_commit);
                WLCHECK(c->errors == 0 && fake.row[0].picture_w == 16 && fake.row[0].pixels[2 * FAKE_W + 2] == 0xff00ff00 &&
                            fake.row[0].pixels[1 * FAKE_W + 1] == 0xff202020 && fake.row[0].pixels[10 * FAKE_W + 10] == 0xff202020,
                        "a subsurface at (4, 4) of the surface is at (2, 2) of a window whose geometry begins at (2, 2) (%s)", c->error_text);
                wlc_end(c);
        }

        /* a subsurface that hangs past the root's edge (a title bar, a border) is part of the window, which begins at it */
        {
                struct tree t;
                struct wl_event_seen *e2;

                tree_open(&t, 1);
                c = t.s.c;
                wlc_send(c, t.sub, WLQ_wl_subsurface_set_position, -3L, -5L);
                buffer = wl_buffer_of(&t.s, 32, 24, 0xff202020, WLN_wl_shm_format_xrgb8888);
                wlc_send(c, t.s.surface, WLQ_wl_surface_attach, buffer, 0L, 0L);
                wlc_send(c, t.s.surface, WLQ_wl_surface_damage_buffer, 0L, 0L, 32L, 24L);
                wlc_send(c, t.s.surface, WLQ_wl_surface_commit);
                WLCHECK(c->errors == 0 && fake.row[0].picture_w == 35 && fake.row[0].picture_h == 29 && fake.row[0].pixels[0] == 0xff00ff00 &&
                            fake.row[0].pixels[10 * FAKE_W + 10] == 0xff202020 && fake.row[0].pixels[12 * FAKE_W + 12] == 0xff202020,
                        "a subsurface past the root's corner makes the window begin there, and the root is drawn in it (%ldx%ld, %s)",
                        fake.row[0].picture_w, fake.row[0].picture_h, c->error_text);
                pointer = wlc_id(c, WLI_wl_pointer, 9);
                wlc_send(c, t.s.seat, WLQ_wl_seat_get_pointer, pointer);
                wlc_forget(c);
                wayland_pointer_to(0, 0, 0);
                wlc_pump(c);
                e2 = wlc_take(c, WLI_wl_pointer, WLE_wl_pointer_enter);
                WLCHECK(e2 && e2->u[1] == t.child && e2->u[2] == 0 && e2->u[3] == 0,
                        "the pointer at the window's corner is on that subsurface, at its own corner");
                wlc_forget(c);
                wayland_pointer_to(0, 10, 10);
                wlc_pump(c);
                e2 = wlc_take(c, WLI_wl_pointer, WLE_wl_pointer_enter);
                WLCHECK(e2 && e2->u[1] == t.s.surface && e2->u[2] == 7 * 256 && e2->u[3] == 5 * 256,
                        "and further in it is on the root, at the place in the root's own pixels (%ld %ld)", e2 ? (long)e2->u[2] : -1,
                        e2 ? (long)e2->u[3] : -1);
                /* the subsurface let go: the window is the root's again, whole */
                wlc_send(c, t.sub, WLQ_wl_subsurface_destroy);
                WLCHECK(c->errors == 0 && fake.row[0].picture_w == 32 && fake.row[0].picture_h == 24 && fake.row[0].pixels[0] == 0xff202020,
                        "a window whose subsurface went is the root's again (%ldx%ld)", fake.row[0].picture_w, fake.row[0].picture_h);
                wlc_end(c);
        }

        /* a popup is placed against the geometry, which is where the parent's window begins */
        {
                unsigned long positioner, popup_surface, popup_xdg, popup;

                session_open(&s);
                session_window(&s, 0xff000000);
                c = s.c;
                wlc_send(c, s.xdg, WLQ_xdg_surface_set_window_geometry, 4L, 5L, 20L, 10L);
                positioner = wlc_id(c, WLI_xdg_positioner, 6);
                wlc_send(c, s.wm_base, WLQ_xdg_wm_base_create_positioner, positioner);
                wlc_send(c, positioner, WLQ_xdg_positioner_set_size, 8L, 6L);
                wlc_send(c, positioner, WLQ_xdg_positioner_set_anchor_rect, 3L, 2L, 1L, 1L);
                wlc_send(c, positioner, WLQ_xdg_positioner_set_anchor, (long)WLN_xdg_positioner_anchor_top_left);
                wlc_send(c, positioner, WLQ_xdg_positioner_set_gravity, (long)WLN_xdg_positioner_gravity_bottom_right);
                popup_surface = wlc_id(c, WLI_wl_surface, 6);
                wlc_send(c, s.compositor, WLQ_wl_compositor_create_surface, popup_surface);
                popup_xdg = wlc_id(c, WLI_xdg_surface, 6);
                wlc_send(c, s.wm_base, WLQ_xdg_wm_base_get_xdg_surface, popup_xdg, popup_surface);
                popup = wlc_id(c, WLI_xdg_popup, 6);
                wlc_forget(c);
                wlc_send(c, popup_xdg, WLQ_xdg_surface_get_popup, popup, s.xdg, positioner);
                e = wlc_take(c, WLI_xdg_surface, WLE_xdg_surface_configure);
                wlc_send(c, popup_xdg, WLQ_xdg_surface_ack_configure, e ? (long)e->u[0] : 0L);
                wlc_send(c, popup_surface, WLQ_wl_surface_attach, wl_buffer_of(&s, 8, 6, 0xff123456, WLN_wl_shm_format_xrgb8888), 0L, 0L);
                wlc_send(c, popup_surface, WLQ_wl_surface_commit);
                WLCHECK(c->errors == 0 && fake.row[1].used && fake.row[1].x == 3 && fake.row[1].y == 2,
                        "a popup is put where the positioner says against the parent's geometry, which is its row's corner (%ld %ld, %s)",
                        fake.row[1].x, fake.row[1].y, c->error_text);
                wlc_end(c);
        }
        leak_check("geometry");
        WLCHECK(fds_balanced(), "descriptors balance (%ld %ld)", fake.fds_open, fake.fds_closed);
        wayland_stop();
}

/* The pointer's cursor: a shape named, a surface for it, or none; the compositor draws its own, so these are taken and checked and no more. */
static void test_cursor(void)
{
        struct session s;
        struct wlc *c;
        unsigned long manager, device, pointer, surface, buffer;

        wl_reset();
        wayland_start(&fake_host);
        session_open(&s);
        c = s.c;
        manager = wl_global(c, s.registry, WLI_wp_cursor_shape_manager_v1, 2);
        WLCHECK(manager != 0, "the cursor shape manager is offered");
        pointer = wlc_id(c, WLI_wl_pointer, 9);
        wlc_send(c, s.seat, WLQ_wl_seat_get_pointer, pointer);
        device = wlc_id(c, WLI_wp_cursor_shape_device_v1, 2);
        wlc_send(c, manager, WLQ_wp_cursor_shape_manager_v1_get_pointer, device, pointer);
        wlc_send(c, device, WLQ_wp_cursor_shape_device_v1_set_shape, 1L, (long)WLN_wp_cursor_shape_device_v1_shape_text);
        wlc_send(c, device, WLQ_wp_cursor_shape_device_v1_set_shape, 2L, (long)WLN_wp_cursor_shape_device_v1_shape_all_resize);
        WLCHECK(c->errors == 0, "a shape the protocol has is taken (%s)", c->error_text);
        /* a cursor of the client's own drawing: a surface with a buffer, or none */
        surface = wlc_id(c, WLI_wl_surface, 6);
        wlc_send(c, s.compositor, WLQ_wl_compositor_create_surface, surface);
        wlc_send(c, pointer, WLQ_wl_pointer_set_cursor, 3L, surface, 2L, 2L);
        buffer = wl_buffer_of(&s, 8, 8, 0xff000000, WLN_wl_shm_format_argb8888);
        wlc_send(c, surface, WLQ_wl_surface_attach, buffer, 0L, 0L);
        wlc_send(c, surface, WLQ_wl_surface_damage_buffer, 0L, 0L, 8L, 8L);
        wlc_send(c, surface, WLQ_wl_surface_commit);
        wlc_send(c, pointer, WLQ_wl_pointer_set_cursor, 4L, 0UL, 0L, 0L);
        WLCHECK(c->errors == 0 && !fake.row[0].used, "a cursor surface and none are taken, and the surface is no window (%s)", c->error_text);
        wlc_send(c, device, WLQ_wp_cursor_shape_device_v1_destroy);
        wlc_end(c);
        /* a shape that does not exist, and one past the last */
        session_open(&s);
        c = s.c;
        manager = wl_global(c, s.registry, WLI_wp_cursor_shape_manager_v1, 2);
        pointer = wlc_id(c, WLI_wl_pointer, 9);
        wlc_send(c, s.seat, WLQ_wl_seat_get_pointer, pointer);
        device = wlc_id(c, WLI_wp_cursor_shape_device_v1, 2);
        wlc_send(c, manager, WLQ_wp_cursor_shape_manager_v1_get_pointer, device, pointer);
        wlc_send(c, device, WLQ_wp_cursor_shape_device_v1_set_shape, 1L, 0L);
        WLCHECK(refused_with(c, WLN_wp_cursor_shape_device_v1_error_invalid_shape), "shape zero is refused");
        wlc_end(c);
        session_open(&s);
        c = s.c;
        manager = wl_global(c, s.registry, WLI_wp_cursor_shape_manager_v1, 2);
        pointer = wlc_id(c, WLI_wl_pointer, 9);
        wlc_send(c, s.seat, WLQ_wl_seat_get_pointer, pointer);
        device = wlc_id(c, WLI_wp_cursor_shape_device_v1, 2);
        wlc_send(c, manager, WLQ_wp_cursor_shape_manager_v1_get_pointer, device, pointer);
        wlc_send(c, device, WLQ_wp_cursor_shape_device_v1_set_shape, 1L, 37L);
        WLCHECK(refused_with(c, WLN_wp_cursor_shape_device_v1_error_invalid_shape), "a shape past the last is refused");
        wlc_end(c);
        leak_check("cursor");
        WLCHECK(fds_balanced(), "descriptors balance (%ld %ld)", fake.fds_open, fake.fds_closed);
        wayland_stop();
}

/*
        The pointer's picture a client draws (wl_pointer.set_cursor with a surface), as the compositor reads it (wayland_cursor_picture): none
        until the surface has a buffer, then that buffer's pixels, size and hotspot, for the window under the pointer only; the hotspot moved
        by the buffer's offset; a serial that moves with every change; the buffer held while it is the picture and released when replaced;
        none again when a shape is named, when the pointer leaves, and when the surface goes; and a surface with another role refused.
*/
static void test_cursor_picture(void)
{
        struct session s;
        struct wlc *c;
        struct wayland_cursor cur;
        unsigned long pointer, cursor, first, second, manager, device, was;
        long a, serial;

        wl_reset();
        wayland_start(&fake_host);
        session_open(&s);
        session_window(&s, 0xff223344);
        c = s.c;
        pointer = wlc_id(c, WLI_wl_pointer, 9);
        manager = wl_global(c, s.registry, WLI_wp_cursor_shape_manager_v1, 2);
        wlc_forget(c);
        wlc_send(c, s.seat, WLQ_wl_seat_get_pointer, pointer);
        wayland_pointer_to(0, 10, 10);
        wlc_pump(c);
        a = event_at(c, WLI_wl_pointer, WLE_wl_pointer_enter, 0);
        serial = a >= 0 ? (long)c->ev[a].u[0] : 0;
        cursor = wlc_id(c, WLI_wl_surface, 6);
        wlc_send(c, s.compositor, WLQ_wl_compositor_create_surface, cursor);
        WLCHECK(wayland_cursor_picture(0, &cur) == 0 && !cur.pixels, "no picture of the client's before set_cursor");
        was = cur.serial;
        wlc_send(c, pointer, WLQ_wl_pointer_set_cursor, (unsigned long)serial, cursor, 3L, 5L);
        WLCHECK(wayland_cursor_picture(0, &cur) == 0 && cur.serial != was && fake_cursor[0] == WAYLAND_CURSOR_ARROW,
                "a surface named that has drawn nothing is no picture yet, the arrow is worn, and the serial moved");
        was = cur.serial;
        first = wl_buffer_of(&s, 8, 6, 0x80402010, WLN_wl_shm_format_argb8888);
        wlc_send(c, cursor, WLQ_wl_surface_attach, first, 0L, 0L);
        wlc_send(c, cursor, WLQ_wl_surface_damage_buffer, 0L, 0L, 8L, 6L);
        wlc_send(c, cursor, WLQ_wl_surface_commit);
        WLCHECK(wayland_cursor_picture(0, &cur) == 1 && cur.pixels && cur.width == 8 && cur.height == 6 && cur.stride == 32 &&
                    cur.format == WLN_wl_shm_format_argb8888 && cur.hot_x == 3 && cur.hot_y == 5 && cur.scale == 1 && cur.serial != was &&
                    ((const unsigned *)cur.pixels)[0] == 0x80402010u && ((const unsigned *)cur.pixels)[8 * 6 - 1] == 0x80402010u,
                "the surface's commit is the picture: its pixels, 8 by 6, the hotspot named, and a new serial (%ld by %ld, hot %ld %ld)",
                cur.width, cur.height, cur.hot_x, cur.hot_y);
        WLCHECK(wayland_cursor_picture(1, &cur) == 0 && !cur.pixels, "it is the picture of the window under the pointer only");
        WLCHECK(releases_of(c, first) == 0, "the buffer is held while it is the picture");
        was = cur.serial;
        second = wl_buffer_of(&s, 16, 16, 0xff00ff00, WLN_wl_shm_format_xrgb8888);
        wlc_send(c, cursor, WLQ_wl_surface_offset, 1L, -2L);
        wlc_send(c, cursor, WLQ_wl_surface_attach, second, 0L, 0L);
        wlc_send(c, cursor, WLQ_wl_surface_set_buffer_scale, 2L);
        wlc_send(c, cursor, WLQ_wl_surface_commit);
        WLCHECK(wayland_cursor_picture(0, &cur) == 1 && cur.width == 16 && cur.format == WLN_wl_shm_format_xrgb8888 && cur.scale == 2 &&
                    cur.hot_x == 4 && cur.hot_y == 14 && cur.serial != was && ((const unsigned *)cur.pixels)[17] == 0xff00ff00u,
                "a buffer at an offset moves the hotspot the other way, in the buffer's pixels at its scale (hot %ld %ld, scale %ld)", cur.hot_x,
                cur.hot_y, cur.scale);
        WLCHECK(releases_of(c, first) == 1 && releases_of(c, second) == 0, "the buffer it replaced is released, the new one held");
        device = wlc_id(c, WLI_wp_cursor_shape_device_v1, 2);
        wlc_send(c, manager, WLQ_wp_cursor_shape_manager_v1_get_pointer, device, pointer);
        was = cur.serial;
        wlc_send(c, device, WLQ_wp_cursor_shape_device_v1_set_shape, (unsigned long)serial, (unsigned long)WLN_wp_cursor_shape_device_v1_shape_text);
        WLCHECK(wayland_cursor_picture(0, &cur) == 0 && cur.serial != was && fake_cursor[0] == WAYLAND_CURSOR_TEXT,
                "a shape named in its place is no picture");
        wlc_send(c, pointer, WLQ_wl_pointer_set_cursor, (unsigned long)serial, cursor, 0L, 0L);
        WLCHECK(wayland_cursor_picture(0, &cur) == 1 && cur.hot_x == 0 && cur.hot_y == 0, "the surface named again is the picture it holds");
        was = cur.serial;
        wayland_pointer_to(-1, 0, 0);
        WLCHECK(wayland_cursor_picture(0, &cur) == 0 && cur.serial != was, "the pointer leaving the window is no picture");
        wayland_pointer_to(0, 10, 10);
        wlc_pump(c);
        WLCHECK(wayland_cursor_picture(0, &cur) == 0, "and coming back is none until the client names it again");
        a = event_at(c, WLI_wl_pointer, WLE_wl_pointer_enter, a + 1);
        serial = a >= 0 ? (long)c->ev[a].u[0] : 0;
        wlc_send(c, pointer, WLQ_wl_pointer_set_cursor, (unsigned long)serial, cursor, 1L, 1L);
        WLCHECK(wayland_cursor_picture(0, &cur) == 1, "named again after the enter, it is the picture");
        was = cur.serial;
        wlc_send(c, cursor, WLQ_wl_surface_destroy);
        WLCHECK(wayland_cursor_picture(0, &cur) == 0 && cur.serial != was && releases_of(c, second) == 1,
                "the surface destroyed is no picture, and its buffer is released");
        WLCHECK(c->errors == 0, "none of it is a protocol error (%s)", c->error_text);
        /* the window's own surface has a role: named as the cursor, it is the role error */
        wlc_send(c, pointer, WLQ_wl_pointer_set_cursor, (unsigned long)serial, s.surface, 0L, 0L);
        WLCHECK(refused_with(c, WLN_wl_pointer_error_role), "a toplevel's surface named as the cursor is the role error");
        wlc_end(c);
        leak_check("cursor picture");
        /* and a cursor's surface may not be made a window */
        session_open(&s);
        c = s.c;
        pointer = wlc_id(c, WLI_wl_pointer, 9);
        wlc_send(c, s.seat, WLQ_wl_seat_get_pointer, pointer);
        cursor = wlc_id(c, WLI_wl_surface, 6);
        wlc_send(c, s.compositor, WLQ_wl_compositor_create_surface, cursor);
        wlc_send(c, pointer, WLQ_wl_pointer_set_cursor, 1L, cursor, 0L, 0L);
        WLCHECK(c->errors == 0, "a surface is named as the cursor whatever the serial");
        s.xdg = wlc_id(c, WLI_xdg_surface, 6);
        wlc_send(c, s.wm_base, WLQ_xdg_wm_base_get_xdg_surface, s.xdg, cursor);
        WLCHECK(refused_with(c, WLN_xdg_wm_base_error_role), "and has the cursor role from then on: it is no xdg surface");
        wlc_end(c);
        leak_check("cursor role");
        WLCHECK(fds_balanced(), "descriptors balance (%ld %ld)", fake.fds_open, fake.fds_closed);
        wayland_stop();
}

/*
        A window's subsurfaces handed to the host as layers (row_layers): the fake host says what it keeps, as the compositor would
        (answer with root 0, answer_root with root 1), and keeps what it was handed last.
*/
static struct
{
        long answer, answer_root;
        long calls, roots, drops, count, kept;
        struct wayland_picture last[WAYLAND_LAYERS], held[WAYLAND_LAYERS];
} fake_layers;

// What the host does with the layers it kept when it keeps them no more (a refusal, none): lays them into the window's picture, bottom first.
static void fake_layers_lay(int row)
{
        for (long at = 0; at < fake_layers.kept; at++)
        {
                struct wayland_picture laid = fake_layers.held[at];

                laid.last = 0;
                laid.full = 1;
                laid.damages = 0;
                fake_row_picture(0, row, &laid);
        }
        fake_layers.kept = 0;
}

static int fake_row_layers(void *context, int row, const struct wayland_picture *layers, long count, long root)
{
        long answer;

        (void)context;
        if (count < 0)
        {
                fake_layers.drops++;
                fake_layers_lay(row);
                return 0;
        }
        fake_layers.calls++;
        fake_layers.roots += root != 0;
        fake_layers.count = count;
        for (long at = 0; at < count && at < WAYLAND_LAYERS; at++)
                fake_layers.last[at] = layers[at];
        answer = root ? fake_layers.answer_root : fake_layers.answer;
        if (answer != WAYLAND_LAYERS_KEPT)
                fake_layers_lay(row);
        else
        {
                fake_layers.kept = count;
                for (long at = 0; at < count && at < WAYLAND_LAYERS; at++)
                        fake_layers.held[at] = layers[at];
        }
        return (int)answer;
}

// A subsurface of t's window at (x, y), size by size of one colour, synchronized or not, its buffer committed.
static unsigned long layer_child(struct tree *t, long x, long y, long size, unsigned colour, int desync, unsigned long *sub_out, unsigned long *buffer_out)
{
        struct wlc *c = t->s.c;
        unsigned long surface = wlc_id(c, WLI_wl_surface, 6), sub = wlc_id(c, WLI_wl_subsurface, 1), buffer;

        wlc_send(c, t->s.compositor, WLQ_wl_compositor_create_surface, surface);
        wlc_send(c, t->subcompositor, WLQ_wl_subcompositor_get_subsurface, sub, surface, t->s.surface);
        wlc_send(c, sub, WLQ_wl_subsurface_set_position, x, y);
        if (desync)
                wlc_send(c, sub, WLQ_wl_subsurface_set_desync);
        buffer = wl_buffer_of(&t->s, size, size, colour, WLN_wl_shm_format_xrgb8888);
        wlc_send(c, surface, WLQ_wl_surface_attach, buffer, 0L, 0L);
        wlc_send(c, surface, WLQ_wl_surface_damage_buffer, 0L, 0L, size, size);
        wlc_send(c, surface, WLQ_wl_surface_commit);
        if (sub_out)
                *sub_out = sub;
        if (buffer_out)
                *buffer_out = buffer;
        return surface;
}

/*
        A window's subsurfaces as the host's layers (row_layers): handed at a subsurface's commit with its damage and nothing laid into the
        window's picture; the window's own commit handed with them (root 1) and its picture the last; a buffer a layer was read from held
        until a frame is drawn after it is replaced, taken off or destroyed; the host that would have them only with the root again
        (WAYLAND_LAYERS_ROOT) handed the root's picture again; a host that refuses, nine of them, and one under the root that it does not
        hide, laid into the picture as before; and everything given back when the client goes mid-way.
*/
static void test_layers(void)
{
        struct wayland_host host = fake_host;
        struct tree t;
        struct wlc *c;
        unsigned long second, third, sub, extra, gone_buffer, gone_sub, below;
        long calls, pictures, lends, copied;

        host.row_layers = fake_row_layers;
        wl_reset();
        memset(&fake_layers, 0, sizeof fake_layers);
        fake_layers.answer = fake_layers.answer_root = WAYLAND_LAYERS_KEPT;
        fake.lend = 1;
        wayland_start(&host);
        tree_open(&t, 1); // a window of 32 by 24, a subsurface of 8 by 8 at (4, 4), not synchronized (its desync hands the window's none first)
        c = t.s.c;
        WLCHECK(c->errors == 0 && fake_layers.calls == 2 && fake_layers.count == 1 && fake_layers.last[0].dest_x == 4 && fake_layers.last[0].dest_y == 4 &&
                    fake_layers.last[0].layer > 0 && fake_layers.last[0].changed == 1 && fake_layers.last[0].full == 1 && fake_layers.last[0].row_width == 32 &&
                    fake_layers.last[0].row_height == 24 && fake_layers.last[0].width == 8 && !fake_layers.last[0].blend,
                "a subsurface's commit is handed as a layer: its place, its picture whole, the window's size (%ld calls, %ld at %ld,%ld, %s)", fake_layers.calls,
                fake_layers.count, fake_layers.last[0].dest_x, fake_layers.last[0].dest_y, c->error_text);
        WLCHECK(fake.row[0].pixels[5 * FAKE_W + 5] == 0xff202020u, "and nothing of it is laid into the window's picture (%08x)", fake.row[0].pixels[5 * FAKE_W + 5]);
        // Its next buffer, a part of it damaged: handed with that damage; the buffer it replaced is held until a frame is drawn.
        second = wl_buffer_of(&t.s, 8, 8, 0xffff0000, WLN_wl_shm_format_xrgb8888);
        wlc_send(c, t.child, WLQ_wl_surface_attach, second, 0L, 0L);
        wlc_send(c, t.child, WLQ_wl_surface_damage_buffer, 2L, 2L, 3L, 3L);
        wlc_send(c, t.child, WLQ_wl_surface_commit);
        WLCHECK(fake_layers.calls == 3 && fake_layers.last[0].changed == 1 && fake_layers.last[0].full == 0 && fake_layers.last[0].damages == 1 &&
                    fake_layers.last[0].damage[0].x == 2 && fake_layers.last[0].damage[0].width == 3,
                "its next commit is handed with the rectangle it damaged (%ld calls, full %ld, %ld rectangles)", fake_layers.calls, fake_layers.last[0].full,
                fake_layers.last[0].damages);
        WLCHECK(wlc_count(c, WLI_wl_buffer, WLE_wl_buffer_release) == 0, "the buffer it replaced is not released before a frame is drawn");
        wayland_drawn();
        wlc_pump(c);
        WLCHECK(wlc_count(c, WLI_wl_buffer, WLE_wl_buffer_release) == 1, "and is once one is (%ld)", wlc_count(c, WLI_wl_buffer, WLE_wl_buffer_release));

        // The window's own commit: handed with its layers (root 1), its picture the last, read in place.
        pictures = fake.row[0].pictures;
        lends = fake.lends;
        calls = fake_layers.roots;
        wlc_send(c, t.s.surface, WLQ_wl_surface_attach, t.s.buffer, 0L, 0L);
        wlc_send(c, t.s.surface, WLQ_wl_surface_damage_buffer, 0L, 0L, 32L, 24L);
        wlc_send(c, t.s.surface, WLQ_wl_surface_commit);
        WLCHECK(fake_layers.roots == calls + 1 && fake.row[0].pictures == pictures + 1 && fake.lends == lends + 1 && fake_layers.last[0].changed == 0 &&
                    fake.row[0].pixels[5 * FAKE_W + 5] == 0xff202020u,
                "the window's commit is handed with its layers, unchanged, and its picture is its last and read in place (%ld roots, %ld pictures, %ld lends)",
                fake_layers.roots - calls, fake.row[0].pictures - pictures, fake.lends - lends);

        // A host that would have them only with the root again: the root's picture is handed again, whole, with them.
        fake_layers.answer = WAYLAND_LAYERS_ROOT;
        pictures = fake.row[0].pictures;
        calls = fake_layers.roots;
        third = wl_buffer_of(&t.s, 8, 8, 0xff0000ff, WLN_wl_shm_format_xrgb8888);
        wlc_send(c, t.child, WLQ_wl_surface_attach, third, 0L, 0L);
        wlc_send(c, t.child, WLQ_wl_surface_damage_buffer, 0L, 0L, 8L, 8L);
        wlc_send(c, t.child, WLQ_wl_surface_commit);
        WLCHECK(fake_layers.roots == calls + 1 && fake.row[0].pictures == pictures + 1 && c->errors == 0,
                "a host that keeps them only with the root's picture is handed that again with them (%ld, %ld)", fake_layers.roots - calls,
                fake.row[0].pictures - pictures);
        fake_layers.answer = WAYLAND_LAYERS_KEPT;

        // A second subsurface, and then the first given no buffer: handed without it, and its buffer let go of after a frame.
        extra = layer_child(&t, 16, 8, 6, 0xff00ffff, 1, &gone_sub, &gone_buffer);
        WLCHECK(fake_layers.count == 2 && fake_layers.last[1].dest_x == 16 && fake_layers.last[1].changed && !fake_layers.last[0].changed,
                "a second subsurface is handed over the first, the first unchanged (%ld)", fake_layers.count);
        wlc_forget(c);
        wlc_send(c, t.child, WLQ_wl_surface_attach, 0UL, 0L, 0L);
        wlc_send(c, t.child, WLQ_wl_surface_commit);
        wlc_pump(c);
        WLCHECK(fake_layers.count == 1 && fake_layers.last[0].dest_x == 16 && releases_of(c, third) == 0,
                "a subsurface given no buffer is handed no more, and its buffer is held until a frame is drawn (%ld, %ld)", fake_layers.count,
                releases_of(c, third));
        wayland_drawn();
        wlc_pump(c);
        WLCHECK(releases_of(c, third) == 1, "and released once one is");
        // Destroyed while it is a layer: handed no more, its buffer held until a frame is drawn.
        wlc_forget(c);
        wlc_send(c, gone_sub, WLQ_wl_subsurface_destroy);
        wlc_send(c, extra, WLQ_wl_surface_destroy);
        wlc_pump(c);
        WLCHECK(fake_layers.count == 0 && releases_of(c, gone_buffer) == 0, "a subsurface destroyed while it is a layer is handed no more, its buffer held (%ld)",
                fake_layers.count);
        wayland_drawn();
        wlc_pump(c);
        WLCHECK(releases_of(c, gone_buffer) == 1, "until a frame is drawn without it");

        // Under the root and hidden by it: left out. Past its edge, which the root does not hide: no layers, laid as before.
        below = layer_child(&t, 10, 10, 4, 0xff112233, 1, &sub, 0);
        wlc_send(c, sub, WLQ_wl_subsurface_place_below, t.s.surface);
        wlc_send(c, t.s.surface, WLQ_wl_surface_commit);
        WLCHECK(c->errors == 0 && fake_layers.count == 0 && fake_layers.drops == 0, "a subsurface under the opaque root, inside it, is left out (%ld, %ld drops)",
                fake_layers.count, fake_layers.drops);
        wlc_send(c, sub, WLQ_wl_subsurface_set_position, 30L, 10L);
        wlc_send(c, t.s.surface, WLQ_wl_surface_commit);
        copied = fake.copied;
        wlc_send(c, below, WLQ_wl_surface_attach, wl_buffer_of(&t.s, 4, 4, 0xff112233, WLN_wl_shm_format_xrgb8888), 0L, 0L);
        wlc_send(c, below, WLQ_wl_surface_commit);
        WLCHECK(fake_layers.drops == 1 && fake.copied > copied, "one under it past its edge is no layers: the host is told, and they are laid (%ld drops)",
                fake_layers.drops);
        wlc_send(c, sub, WLQ_wl_subsurface_destroy);
        wlc_send(c, below, WLQ_wl_surface_destroy);

        // Nine: no layers, laid into the picture.
        calls = fake_layers.calls;
        for (int k = 0; k < WAYLAND_LAYERS; k++)
                layer_child(&t, 2 + 3 * k, 2, 2, 0xff445566, 1, 0, 0);
        // Each made at its place and then not synchronized: its window handed once for the place, and once for its buffer.
        WLCHECK(fake_layers.calls == calls + 2 * WAYLAND_LAYERS && fake_layers.count == WAYLAND_LAYERS, "eight are handed as layers (%ld, %ld calls)",
                fake_layers.count, fake_layers.calls - calls);
        layer_child(&t, 2, 20, 2, 0xff778899, 1, 0, 0);
        WLCHECK(fake_layers.drops == 2 && fake.row[0].pixels[20 * FAKE_W + 2] == 0xff778899u && fake.row[0].pixels[2 * FAKE_W + 2] == 0xff445566u,
                "a ninth is no layers: the host is told, and all of them are laid into the picture (%ld drops, %08x)", fake_layers.drops,
                fake.row[0].pixels[20 * FAKE_W + 2]);

        wlc_end(c);
        leak_check("layers");
        WLCHECK(fds_balanced(), "descriptors balance (%ld %ld)", fake.fds_open, fake.fds_closed);
        wayland_stop();

        // A host that refuses: laid as before.
        wl_reset();
        memset(&fake_layers, 0, sizeof fake_layers);
        fake.lend = 1;
        wayland_start(&host);
        tree_open(&t, 1);
        c = t.s.c;
        WLCHECK(fake_layers.calls == 2 && fake.row[0].pixels[5 * FAKE_W + 5] == 0xff00ff00u,
                "a host that refuses the layers has the subsurface laid into the picture as before (%08x)", fake.row[0].pixels[5 * FAKE_W + 5]);
        wlc_end(c);
        leak_check("layers refused");
        wayland_stop();
}

// The fake host's card: whether a job of it may still read the rows' pictures in place (row_busy).
static int fake_busy;

static int fake_row_busy(void *context, int row)
{
        (void)context;
        (void)row;
        return fake_busy;
}

/*
        A buffer read in place while a job of the card may still read it (the present does not wait for its jobs, row_busy): a window's second
        commit before a frame is drawn does not release the buffer two pictures back, nor does its window's close the ones it showed; the client is
        read and answered all along; and they are released at the first frame drawn after the job is done.
*/
static void test_row_busy(void)
{
        struct wayland_host host = fake_host;
        struct session s;
        struct wlc *c;
        unsigned long second, third, sync;

        host.row_busy = fake_row_busy;
        wl_reset();
        fake.lend = 1;
        fake_busy = 0;
        wayland_start(&host);
        session_open(&s);
        session_window(&s, 0xff101010); // its buffer read in place
        c = s.c;
        second = wl_buffer_of(&s, 32, 24, 0xff202020, WLN_wl_shm_format_xrgb8888);
        third = wl_buffer_of(&s, 32, 24, 0xff303030, WLN_wl_shm_format_xrgb8888);
        fake_busy = 1;
        wlc_forget(c);
        wlc_send(c, s.surface, WLQ_wl_surface_attach, second, 0L, 0L);
        wlc_send(c, s.surface, WLQ_wl_surface_damage_buffer, 0L, 0L, 32L, 24L);
        wlc_send(c, s.surface, WLQ_wl_surface_commit);
        wlc_send(c, s.surface, WLQ_wl_surface_attach, third, 0L, 0L);
        wlc_send(c, s.surface, WLQ_wl_surface_damage_buffer, 0L, 0L, 32L, 24L);
        wlc_send(c, s.surface, WLQ_wl_surface_commit);
        sync = wlc_id(c, WLI_wl_callback, 1);
        wlc_send(c, 1, WLQ_wl_display_sync, sync);
        wlc_pump(c);
        WLCHECK(fake.lends == 3 && releases_of(c, s.buffer) == 0 && wlc_count(c, WLI_wl_callback, WLE_wl_callback_done) == 1 && c->errors == 0,
                "a second commit while the card may still read the first picture releases nothing, and the client is answered (%ld lends, %ld released)",
                fake.lends, releases_of(c, s.buffer));
        fake_busy = 0;
        wayland_drawn();
        wlc_pump(c);
        WLCHECK(releases_of(c, s.buffer) == 1 && releases_of(c, second) == 1 && releases_of(c, third) == 0,
                "the frame drawn once the job is done releases both it held, and not the picture");
        // The window's close while busy: its buffer is the client's again only at the frame after.
        fake_busy = 1;
        wlc_forget(c);
        wlc_send(c, s.toplevel, WLQ_xdg_toplevel_destroy);
        wlc_send(c, s.xdg, WLQ_xdg_surface_destroy);
        wlc_send(c, s.surface, WLQ_wl_surface_destroy);
        wlc_pump(c);
        WLCHECK(releases_of(c, third) == 0 && c->errors == 0, "a window closed while the card may read it releases nothing yet (%s)", c->error_text);
        fake_busy = 0;
        wayland_drawn();
        wlc_pump(c);
        WLCHECK(releases_of(c, third) == 1, "and its buffer is released at the frame after");
        wlc_end(c);
        leak_check("row busy");
        wayland_stop();
}

/* ---- dma-bufs ---- */

#define DRM_ARGB8888 0x34325241L
#define DRM_XRGB8888 0x34325258L

/* A file of height rows of stride bytes whose pixel at (x, y) says so, like a dma-buf's pages (the fake takes any file for one). */
static long dmabuf_file(long width, long stride, long height, unsigned base)
{
        long fd = wl_file(stride * height, 0);
        unsigned *row = calloc(1, (size_t)stride);

        for (long y = 0; y < height; y++)
        {
                memset(row, 0, (size_t)stride);
                for (long x = 0; x < width; x++)
                        row[x] = base | (unsigned)(y << 8) | (unsigned)x;
                wl_file_put(fd, y * stride, row, stride);
        }
        free(row);
        return fd;
}

/* One plane added to a params object made and given. */
static unsigned long dmabuf_params(struct session *s, long fd, long plane, long offset, long stride, long modifier_low)
{
        struct wlc *c = s->c;
        unsigned long params = wlc_id(c, WLI_zwp_linux_buffer_params_v1, 4);

        wlc_send(c, s->dmabuf, WLQ_zwp_linux_dmabuf_v1_create_params, params);
        if (fd >= 0)
                wlc_send(c, params, WLQ_zwp_linux_buffer_params_v1_add, fd, plane, offset, stride, 0L, modifier_low);
        return params;
}

/* A buffer made at once from a file of width by height pixels. */
static unsigned long dmabuf_buffer(struct session *s, long width, long height, unsigned base, long format)
{
        struct wlc *c = s->c;
        long stride = width * 4 + 16;
        unsigned long params = dmabuf_params(s, dmabuf_file(width, stride, height, base), 0, 0, stride, 0), buffer = wlc_id(c, WLI_wl_buffer, 1);

        wlc_send(c, params, WLQ_zwp_linux_buffer_params_v1_create_immed, buffer, width, height, format, 0L);
        wlc_send(c, params, WLQ_zwp_linux_buffer_params_v1_destroy);
        return buffer;
}

static void dmabuf_commit(struct session *s, unsigned long buffer, long width, long height)
{
        wlc_send(s->c, s->surface, WLQ_wl_surface_attach, buffer, 0L, 0L);
        wlc_send(s->c, s->surface, WLQ_wl_surface_damage_buffer, 0L, 0L, width, height);
        wlc_send(s->c, s->surface, WLQ_wl_surface_commit);
}

/* How a feedback object is told: the table, the device, each tranche, done. */
static int feedback_in_order(struct wlc *c, unsigned long object, const long *want, long count)
{
        long seen = 0;

        for (long at = 0; at < c->events; at++)
                if (c->ev[at].object == object)
                {
                        if (seen >= count || c->ev[at].opcode != want[seen])
                                return 0;
                        seen++;
                }
        return seen == count;
}

/*
        A client's share of the copies (WAYLAND_BUDGET_*): one read of twenty commits of a window's buffer (52 bytes each) is made
        only as far as WAYLAND_BUDGET_SCREENS of the largest screen's pixels (a screen the window's size here, so that many pictures, eight
        since e933763 raised the budget from two screens), then waits, stalled as a commit of a dma-buf whose GPU is not done does,
        and goes on in the next window of time (16 ms), the rest still in its order. A buffer larger than the budget is never
        refused: it is made in a window of its own. On the box, before this, one read of 1000 such commits of a 4K window held the
        compositor for 4.5 s.
*/
static void test_budget(void)
{
        struct session s;
        struct wlc *c;
        unsigned char run[20 * 52];
        long fd, pictures, due, at = 0;
        unsigned long big;

        _Static_assert(2 * WAYLAND_BUDGET_SCREENS < 20 && 3 * WAYLAND_BUDGET_SCREENS >= 20, "twenty commits are three windows of the budget");
        wl_reset();
        fake.screen.width = 32;
        fake.screen.height = 24;
        wayland_start(&fake_host);
        session_open(&s);
        c = s.c;
        session_window(&s, 0xff123456); // 32 by 24 on a screen of 32 by 24: a budget of WAYLAND_BUDGET_SCREENS pictures
        fake.clock_still = 1;
        fake.now += WAYLAND_BUDGET_MS;
        for (int k = 0; k < 20; k++)
        {
                wlc_put(run + at, s.surface);
                wlc_put(run + at + 4, WLQ_wl_surface_attach | 20UL << 16);
                wlc_put(run + at + 8, s.buffer);
                wlc_put(run + at + 12, 0);
                wlc_put(run + at + 16, 0);
                wlc_put(run + at + 20, s.surface);
                wlc_put(run + at + 24, WLQ_wl_surface_damage_buffer | 24UL << 16);
                wlc_put(run + at + 28, 0);
                wlc_put(run + at + 32, 0);
                wlc_put(run + at + 36, 32);
                wlc_put(run + at + 40, 24);
                wlc_put(run + at + 44, s.surface);
                wlc_put(run + at + 48, WLQ_wl_surface_commit | 8UL << 16);
                at += 52;
        }
        pictures = fake.row[0].pictures;
        wayland_feed(c->slot, run, at, 0, 0);
        wlc_pump(c);
        WLCHECK(fake.row[0].pictures - pictures == WAYLAND_BUDGET_SCREENS && wayland_stalled(c->slot),
                "twenty commits in one read: the budget's pictures (%d), and the rest wait (%ld)", WAYLAND_BUDGET_SCREENS, fake.row[0].pictures - pictures);
        due = wayland_stall_due(c->slot, &fd);
        WLCHECK(due == fake.now + WAYLAND_BUDGET_MS && fd == -1, "the client is due again at the end of its window, with no buffer to poll");
        WLCHECK(wayland_resume(c->slot) == 1 && fake.row[0].pictures - pictures == WAYLAND_BUDGET_SCREENS, "and nothing more is made before then");
        fake.now += WAYLAND_BUDGET_MS;
        wayland_resume(c->slot);
        wlc_pump(c);
        WLCHECK(fake.row[0].pictures - pictures == 2 * WAYLAND_BUDGET_SCREENS && wayland_stalled(c->slot), "the next window makes as many more (%ld)",
                fake.row[0].pictures - pictures);
        fake.now += WAYLAND_BUDGET_MS;
        wayland_resume(c->slot);
        wlc_pump(c);
        WLCHECK(fake.row[0].pictures - pictures == 20 && !wayland_stalled(c->slot) && c->errors == 0,
                "and the one after the rest, in their order, and the client goes on (%ld, %s)", fake.row[0].pictures - pictures,
                c->error_text);
        // A picture larger than the budget: made, in a window of its own.
        big = wl_buffer_of(&s, 128, 96, 0xff00ff00, WLN_wl_shm_format_xrgb8888);
        fake.now += WAYLAND_BUDGET_MS;
        pictures = fake.row[0].pictures;
        for (int k = 0; k < 2; k++)
        {
                wlc_send(c, s.surface, WLQ_wl_surface_attach, big, 0L, 0L);
                wlc_send(c, s.surface, WLQ_wl_surface_damage_buffer, 0L, 0L, 128L, 96L);
                wlc_send(c, s.surface, WLQ_wl_surface_commit);
        }
        WLCHECK(fake.row[0].pictures - pictures == 1 && wayland_stalled(c->slot), "a picture past the budget is made, and the next waits");
        fake.now += WAYLAND_BUDGET_MS;
        wayland_resume(c->slot);
        wlc_pump(c);
        WLCHECK(fake.row[0].pictures - pictures == 2 && !wayland_stalled(c->slot) && c->errors == 0, "for a window of its own (%s)",
                c->error_text);
        fake.clock_still = 0;
        wlc_end(c);
        leak_check("budget");
        wayland_stop();
}

/* One commit of the session's window, as the budget rows send them: a picture of 32 by 24 when it is made. */
static void stall_commit(struct wlc *c, struct session *s)
{
        wlc_send(c, s->surface, WLQ_wl_surface_attach, s->buffer, 0L, 0L);
        wlc_send(c, s->surface, WLQ_wl_surface_damage_buffer, 0L, 0L, 32L, 24L);
        wlc_send(c, s->surface, WLQ_wl_surface_commit);
}

/*
        A commit that is taken for its state (wp_fifo's wait, wp_commit_timing's time, wp_fifo's set_barrier) and then held
        back for its budget is made again in its turn with the state it was sent with: the wait and the time are the
        commit's, and the barrier it sets is up from the frame it is made in, so a commit that waits for it is held. The
        state is taken once the commit is sure to be made, not before, so a commit that waits again takes none twice.
*/
static void test_stall_state(void)
{
        struct session s;
        struct wlc *c;
        unsigned long fifos, timers, fifo, timer;
        long pictures;

        wl_reset();
        fake.screen.width = 32;
        fake.screen.height = 24;
        wayland_start(&fake_host);
        session_open(&s);
        c = s.c;
        session_window(&s, 0xff123456); // 32 by 24 on a screen of 32 by 24: a budget of WAYLAND_BUDGET_SCREENS pictures (e933763)
        fifos = wl_global(c, s.registry, WLI_wp_fifo_manager_v1, 1);
        timers = wl_global(c, s.registry, WLI_wp_commit_timing_manager_v1, 1);
        wlc_forget(c);
        fifo = wlc_id(c, WLI_wp_fifo_v1, 1);
        wlc_send(c, fifos, WLQ_wp_fifo_manager_v1_get_fifo, fifo, s.surface);
        timer = wlc_id(c, WLI_wp_commit_timer_v1, 1);
        wlc_send(c, timers, WLQ_wp_commit_timing_manager_v1_get_timer, timer, s.surface);
        fake.clock_still = 1;

        // A commit with a wait and a time, past the budget: held, and made again in the next window with both.
        fake.now += WAYLAND_BUDGET_MS;
        pictures = fake.row[0].pictures;
        for (int k = 0; k < WAYLAND_BUDGET_SCREENS; k++)
                stall_commit(c, &s);
        WLCHECK(c->errors == 0 && !wayland_stalled(c->slot) && fake.row[0].pictures - pictures == WAYLAND_BUDGET_SCREENS, "the budget's commits spend it (%ld)",
                fake.row[0].pictures - pictures);
        wlc_send(c, fifo, WLQ_wp_fifo_v1_wait_barrier);
        wlc_send(c, timer, WLQ_wp_commit_timer_v1_set_timestamp, 0ul, 0ul, 500000ul);
        stall_commit(c, &s);
        WLCHECK(c->errors == 0 && wayland_stalled(c->slot) && fake.row[0].pictures - pictures == WAYLAND_BUDGET_SCREENS,
                "a commit with a wait and a time is held for the budget (%ld, %s)", fake.row[0].pictures - pictures, c->error_text);
        fake.now += WAYLAND_BUDGET_MS;
        WLCHECK(wayland_resume(c->slot) == 0 && c->errors == 0 && !wayland_stalled(c->slot) && fake.row[0].pictures - pictures == WAYLAND_BUDGET_SCREENS + 1,
                "in its window it is made (%ld, %s)", fake.row[0].pictures - pictures, c->error_text);
        wlc_pump(c);
        WLCHECK(wayland_row_hint(0, WAYLAND_HINT_FIFO) == 1, "the commit made again still waits for the barrier (%lu)", wayland_row_hint(0, WAYLAND_HINT_FIFO));
        WLCHECK(wayland_row_hint(0, WAYLAND_HINT_TIMESTAMP) == 500000ul, "and is for its time, the one it was sent with (%lu)", wayland_row_hint(0, WAYLAND_HINT_TIMESTAMP));

        // A commit that sets a barrier, held for the budget, with a frame drawn while it waits: the barrier is up from the
        // frame it is made in, so the commit that waits for it after is held.
        fake.now += WAYLAND_BUDGET_MS;
        pictures = fake.row[0].pictures;
        for (int k = 0; k < WAYLAND_BUDGET_SCREENS; k++)
                stall_commit(c, &s);
        wlc_send(c, fifo, WLQ_wp_fifo_v1_set_barrier);
        stall_commit(c, &s);
        WLCHECK(c->errors == 0 && wayland_stalled(c->slot) && fake.row[0].pictures - pictures == WAYLAND_BUDGET_SCREENS,
                "a commit that sets a barrier is held for the budget (%ld)", fake.row[0].pictures - pictures);
        wayland_frame();
        fake.now += WAYLAND_BUDGET_MS;
        WLCHECK(wayland_resume(c->slot) == 0 && c->errors == 0 && !wayland_stalled(c->slot), "and is made in its window (%s)",
                c->error_text);
        wlc_send(c, fifo, WLQ_wp_fifo_v1_wait_barrier);
        wlc_send(c, s.surface, WLQ_wl_surface_commit);
        WLCHECK(c->errors == 0 && wayland_stalled(c->slot),
                "the barrier the commit made is up in the frame it is made in, so the next wait is held (%s)", c->error_text);
        wayland_frame();
        WLCHECK(wayland_resume(c->slot) == 0 && c->errors == 0 && !wayland_stalled(c->slot), "the frame after clears it: the wait is made");
        wlc_end(c);
        leak_check("stall state");
        fake.clock_still = 0;
        wayland_stop();
}

/*
        A commit of a dma-buf its GPU is not done with is held as a commit for the budget is: when the GPU says done it is made
        with the wp_fifo wait and the commit time it was sent with.
*/
static void test_stall_dmabuf(void)
{
        struct session s;
        struct wlc *c;
        unsigned long fifos, timers, fifo, timer, buffer;

        wl_reset();
        wayland_start(&fake_host);
        session_open(&s);
        session_window(&s, 0xff000000);
        c = s.c;
        fifos = wl_global(c, s.registry, WLI_wp_fifo_manager_v1, 1);
        timers = wl_global(c, s.registry, WLI_wp_commit_timing_manager_v1, 1);
        wlc_forget(c);
        fifo = wlc_id(c, WLI_wp_fifo_v1, 1);
        wlc_send(c, fifos, WLQ_wp_fifo_manager_v1_get_fifo, fifo, s.surface);
        timer = wlc_id(c, WLI_wp_commit_timer_v1, 1);
        wlc_send(c, timers, WLQ_wp_commit_timing_manager_v1_get_timer, timer, s.surface);
        buffer = dmabuf_buffer(&s, 16, 8, 0xff300000u, DRM_XRGB8888);
        wlc_send(c, fifo, WLQ_wp_fifo_v1_wait_barrier);
        wlc_send(c, timer, WLQ_wp_commit_timer_v1_set_timestamp, 0ul, 0ul, 500000ul);
        fake.pattern = "NR";
        fake.pattern_at = 0;
        fake.asked = 0;
        dmabuf_commit(&s, buffer, 16, 8);
        WLCHECK(c->errors == 0 && wayland_stalled(c->slot), "a commit of a dma-buf whose GPU is not done is held (%ld asked)", fake.asked);
        WLCHECK(wayland_resume(c->slot) == 0 && c->errors == 0 && !wayland_stalled(c->slot) &&
                    fake.row[0].pixels[6 * FAKE_W + 12] == (PATTERN(12, 6) | 0x300000u),
                "and is made once it is done (%s)", c->error_text);
        WLCHECK(wayland_row_hint(0, WAYLAND_HINT_FIFO) == 1, "made with the wait it was sent with (%lu)", wayland_row_hint(0, WAYLAND_HINT_FIFO));
        WLCHECK(wayland_row_hint(0, WAYLAND_HINT_TIMESTAMP) == 500000ul, "and the time it was sent with (%lu)", wayland_row_hint(0, WAYLAND_HINT_TIMESTAMP));
        wlc_end(c);
        leak_check("stall dmabuf");
        wayland_stop();
}

static void test_dmabuf(void)
{
        struct session s, other;
        struct wlc *c;
        struct wl_event_seen *e;
        unsigned long feedback, buffer, params, made;
        int resumed;

        wl_reset();
        wayland_start(&fake_host);
        session_open(&s);
        session_window(&s, 0xff000000);
        c = s.c;
        WLCHECK(s.dmabuf != 0, "zwp_linux_dmabuf_v1 is offered, at version 4, when the host takes dma-bufs");

        /* the feedback of version 4: the table, the main device, a tranche, and done, in that order */
        feedback = wlc_id(c, WLI_zwp_linux_dmabuf_feedback_v1, 4);
        wlc_forget(c);
        wlc_send(c, s.dmabuf, WLQ_zwp_linux_dmabuf_v1_get_default_feedback, feedback);
        {
                static const long order[] = {WLE_zwp_linux_dmabuf_feedback_v1_format_table,         WLE_zwp_linux_dmabuf_feedback_v1_main_device,
                                             WLE_zwp_linux_dmabuf_feedback_v1_tranche_target_device, WLE_zwp_linux_dmabuf_feedback_v1_tranche_flags,
                                             WLE_zwp_linux_dmabuf_feedback_v1_tranche_formats,      WLE_zwp_linux_dmabuf_feedback_v1_tranche_done,
                                             WLE_zwp_linux_dmabuf_feedback_v1_done};

                WLCHECK(c->errors == 0 && c->malformed == 0 && feedback_in_order(c, feedback, order, 7), "the feedback is told in the protocol's order (%s)",
                        c->error_text);
        }
        e = wlc_take(c, WLI_zwp_linux_dmabuf_feedback_v1, WLE_zwp_linux_dmabuf_feedback_v1_format_table);
        WLCHECK(e && e->fd_count == 1 && e->u[0] == 32, "the table is a descriptor of two pairs of sixteen bytes");
        e = wlc_take(c, WLI_zwp_linux_dmabuf_feedback_v1, WLE_zwp_linux_dmabuf_feedback_v1_main_device);
        WLCHECK(e && e->u[0] == 8, "the main device is a dev_t's eight bytes");
        e = wlc_take(c, WLI_zwp_linux_dmabuf_feedback_v1, WLE_zwp_linux_dmabuf_feedback_v1_tranche_target_device);
        e = wlc_take(c, WLI_zwp_linux_dmabuf_feedback_v1, WLE_zwp_linux_dmabuf_feedback_v1_tranche_flags);
        WLCHECK(e && e->u[0] == 0, "the tranche has no flags");
        e = wlc_take(c, WLI_zwp_linux_dmabuf_feedback_v1, WLE_zwp_linux_dmabuf_feedback_v1_tranche_formats);
        WLCHECK(e && e->u[0] == 4, "and names both pairs, two bytes each");
        wlc_send(c, feedback, WLQ_zwp_linux_dmabuf_feedback_v1_destroy);

        /* a buffer made at once, shown, and its picture read through the descriptor */
        buffer = dmabuf_buffer(&s, 16, 8, 0xff000000u, DRM_XRGB8888);
        WLCHECK(c->errors == 0, "a buffer is made of a file the host maps (%s)", c->error_text);
        wlc_forget(c);
        dmabuf_commit(&s, buffer, 16, 8);
        WLCHECK(c->errors == 0 && fake.row[0].picture_w == 16 && fake.row[0].picture_h == 8 && fake.row[0].pixels[5 * FAKE_W + 9] == PATTERN(9, 5) &&
                    fake.row[0].pixels[0] == PATTERN(0, 0) && fake.row[0].pixels[7 * FAKE_W + 15] == PATTERN(15, 7),
                "its pixels are the window's (%ldx%ld, %08x)", fake.row[0].picture_w, fake.row[0].picture_h, fake.row[0].pixels[5 * FAKE_W + 9]);
        WLCHECK(fake.syncs == 1 && wlc_count(c, WLI_wl_buffer, WLE_wl_buffer_release) == 1, "read through its descriptor once, and released after (%ld syncs)",
                fake.syncs);

        /* a buffer made by create: told in a created event, with an id of the server's */
        {
                long stride = 16 * 4 + 16;

                params = dmabuf_params(&s, dmabuf_file(16, stride, 8, 0xff000000u | 0x400000u), 0, 0, stride, 0);
                wlc_forget(c);
                wlc_send(c, params, WLQ_zwp_linux_buffer_params_v1_create, 16L, 8L, DRM_ARGB8888, 0L);
                e = wlc_take(c, WLI_zwp_linux_buffer_params_v1, WLE_zwp_linux_buffer_params_v1_created);
                made = e ? e->u[0] : 0;
                WLCHECK(made >= WAYLAND_SERVER_BASE && c->errors == 0, "create is answered with created and an id of the server's (%s)", c->error_text);
        }
        dmabuf_commit(&s, made, 16, 8);
        WLCHECK(c->errors == 0 && fake.row[0].pixels[3 * FAKE_W + 4] == (PATTERN(4, 3) | 0x400000u), "and that buffer is shown (%08x)", fake.row[0].pixels[3 * FAKE_W + 4]);
        wlc_send(c, made, WLQ_wl_buffer_destroy);
        WLCHECK(c->errors == 0, "a buffer of the server's id is destroyed as any");

        /* a picture the host did not read (the buffer's writer was not done) is not shown, and the next is shown whole */
        buffer = dmabuf_buffer(&s, 16, 8, 0xff100000u, DRM_XRGB8888);
        fake.skip_next = 1;
        dmabuf_commit(&s, buffer, 16, 8);
        WLCHECK(c->errors == 0 && fake.row[0].pixels[3 * FAKE_W + 4] == (PATTERN(4, 3) | 0x400000u), "a picture the host skipped changes nothing");
        buffer = dmabuf_buffer(&s, 16, 8, 0xff200000u, DRM_XRGB8888);
        wlc_send(c, s.surface, WLQ_wl_surface_attach, buffer, 0L, 0L);
        wlc_send(c, s.surface, WLQ_wl_surface_damage_buffer, 0L, 0L, 1L, 1L);
        wlc_send(c, s.surface, WLQ_wl_surface_commit);
        WLCHECK(fake.row[0].pixels[6 * FAKE_W + 12] == (PATTERN(12, 6) | 0x200000u), "and the next, of one pixel's damage, is the whole of it (%08x)", fake.row[0].pixels[6 * FAKE_W + 12]);

        /*
                A commit of a buffer its GPU is not done with is not waited for by the loop: it is not made, the buffer is not released
                and the client's frame callback is not sent, and what the client sends after it waits behind it, until the buffer is
                ready, when it is made and what came after follows in its order.
        */
        buffer = dmabuf_buffer(&s, 16, 8, 0xff300000u, DRM_XRGB8888);
        wlc_forget(c);
        fake.pattern = "NNRR";
        fake.pattern_at = 0;
        fake.asked = 0;
        dmabuf_commit(&s, buffer, 16, 8);
        wlc_send(c, s.toplevel, WLQ_xdg_toplevel_set_title, "after the commit");
        WLCHECK(c->errors == 0 && wayland_stalled(c->slot) && fake.row[0].pixels[6 * FAKE_W + 12] == (PATTERN(12, 6) | 0x200000u) &&
                    wlc_count(c, WLI_wl_buffer, WLE_wl_buffer_release) == 0 && strcmp(fake.row[0].title, "after the commit") != 0,
                "a commit of a buffer whose GPU is not done is not made, its buffer is not released, and what comes after it waits (%ld asked)", fake.asked);
        {
                // The host is told the buffer to poll and to come back at the end of the client's time, not a millisecond at a time.
                long due, fd;

                due = wayland_stall_due(c->slot, &fd);
                WLCHECK(fd >= 0 && fd == wayland.client[c->slot]->stalled_fd &&
                            due == wayland.client[c->slot]->stalled_at + WAYLAND_STALL_MS + 1,
                        "a client that waits for its GPU names its buffer to poll and is due at the end of its time (%ld)", due);
        }
        resumed = wayland_resume(c->slot);
        wlc_pump(c);
        WLCHECK(resumed == 1 && wayland_stalled(c->slot) && fake.row[0].pixels[6 * FAKE_W + 12] == (PATTERN(12, 6) | 0x200000u) &&
                    strcmp(fake.row[0].title, "after the commit") != 0,
                "while the GPU is not done it is asked again and nothing changes");
        resumed = wayland_resume(c->slot);
        wlc_pump(c);
        WLCHECK(resumed == 0 && !wayland_stalled(c->slot) && fake.row[0].pixels[6 * FAKE_W + 12] == (PATTERN(12, 6) | 0x300000u) &&
                    !strcmp(fake.row[0].title, "after the commit") && wlc_count(c, WLI_wl_buffer, WLE_wl_buffer_release) == 1 && c->errors == 0,
                "when it is done the commit is made, the buffer released, and what was sent after is taken (%08x, %s)",
                fake.row[0].pixels[6 * FAKE_W + 12], fake.row[0].title);

        /* a second commit behind the first waits in its turn, and what is sent after both is taken after both */
        wlc_forget(c);
        buffer = dmabuf_buffer(&s, 16, 8, 0xff400000u, DRM_XRGB8888);
        made = dmabuf_buffer(&s, 16, 8, 0xff500000u, DRM_XRGB8888);
        fake.pattern = "NRRN";
        fake.pattern_at = 0;
        dmabuf_commit(&s, buffer, 16, 8);
        dmabuf_commit(&s, made, 16, 8);
        wlc_send(c, s.toplevel, WLQ_xdg_toplevel_set_title, "after both");
        WLCHECK(wayland_stalled(c->slot) && fake.row[0].pixels[6 * FAKE_W + 12] == (PATTERN(12, 6) | 0x300000u) && strcmp(fake.row[0].title, "after both") != 0,
                "two commits and a title sent while the first waits all wait");
        resumed = wayland_resume(c->slot);
        wlc_pump(c);
        WLCHECK(resumed == 1 && wayland_stalled(c->slot) && fake.row[0].pixels[6 * FAKE_W + 12] == (PATTERN(12, 6) | 0x400000u) &&
                    strcmp(fake.row[0].title, "after both") != 0 && wlc_count(c, WLI_wl_buffer, WLE_wl_buffer_release) == 1,
                "the first is made when its GPU is done and the second, whose is not, waits again with what follows it");
        resumed = wayland_resume(c->slot);
        wlc_pump(c);
        WLCHECK(resumed == 0 && !wayland_stalled(c->slot) && fake.row[0].pixels[6 * FAKE_W + 12] == (PATTERN(12, 6) | 0x500000u) &&
                    !strcmp(fake.row[0].title, "after both") && wlc_count(c, WLI_wl_buffer, WLE_wl_buffer_release) == 2 && c->errors == 0,
                "and the second is made when its is, and the title after it, in the order sent");
        WLCHECK(wayland_resume(c->slot) == 0, "a client that waits for nothing is not stalled by being asked");

        /* a GPU that is not done in ten seconds is a client ended, with what it held given back */
        buffer = dmabuf_buffer(&s, 16, 8, 0xff600000u, DRM_XRGB8888);
        fake.pattern = "NNNNNNNNNNNNNNNNNNNN";
        fake.pattern_at = 0;
        dmabuf_commit(&s, buffer, 16, 8);
        WLCHECK(wayland_stalled(c->slot), "a buffer whose GPU does not finish waits");
        fake.now += 20000;
        wayland_resume(c->slot);
        WLCHECK(!wayland_stalled(c->slot) && wayland_dead(c->slot), "and is given up after ten seconds: the client is ended, with the error to say so");
        fake.pattern = 0;

        /* the rules of params */
        wlc_end(c);
        session_open(&s);
        session_window(&s, 0xff000000);
        c = s.c;
        params = dmabuf_params(&s, dmabuf_file(16, 80, 8, 0), 0, 0, 80, 0);
        wlc_send(c, params, WLQ_zwp_linux_buffer_params_v1_add, dmabuf_file(16, 80, 8, 0), 0L, 0L, 80L, 0L, 0L);
        WLCHECK(refused_with(c, WLN_zwp_linux_buffer_params_v1_error_plane_set), "a plane set twice is refused");
        wlc_end(c);
        session_open(&s);
        c = s.c;
        params = dmabuf_params(&s, dmabuf_file(16, 80, 8, 0), 4, 0, 80, 0);
        WLCHECK(refused_with(c, WLN_zwp_linux_buffer_params_v1_error_plane_idx), "a plane past the fourth is refused");
        wlc_end(c);
        session_open(&s);
        c = s.c;
        params = dmabuf_params(&s, -1, 0, 0, 0, 0);
        wlc_send(c, params, WLQ_zwp_linux_buffer_params_v1_create, 16L, 8L, DRM_XRGB8888, 0L);
        WLCHECK(refused_with(c, WLN_zwp_linux_buffer_params_v1_error_incomplete), "a buffer with no plane is refused");
        wlc_end(c);
        session_open(&s);
        c = s.c;
        params = dmabuf_params(&s, dmabuf_file(16, 80, 8, 0), 1, 0, 80, 0);
        wlc_send(c, params, WLQ_zwp_linux_buffer_params_v1_create, 16L, 8L, DRM_XRGB8888, 0L);
        WLCHECK(refused_with(c, WLN_zwp_linux_buffer_params_v1_error_incomplete), "a buffer with only a second plane is refused");
        wlc_end(c);
        session_open(&s);
        c = s.c;
        params = dmabuf_params(&s, dmabuf_file(16, 80, 8, 0), 0, 0, 80, 0);
        wlc_send(c, params, WLQ_zwp_linux_buffer_params_v1_add, dmabuf_file(16, 80, 8, 0), 1L, 0L, 80L, 0L, 0L);
        wlc_send(c, params, WLQ_zwp_linux_buffer_params_v1_create, 16L, 8L, DRM_XRGB8888, 0L);
        WLCHECK(refused_with(c, WLN_zwp_linux_buffer_params_v1_error_incomplete), "a format of one plane given two is refused");
        wlc_end(c);
        session_open(&s);
        c = s.c;
        params = dmabuf_params(&s, dmabuf_file(16, 80, 8, 0), 0, 0, 80, 0);
        wlc_send(c, params, WLQ_zwp_linux_buffer_params_v1_create, 16L, 8L, 0x30303030L, 0L);
        WLCHECK(refused_with(c, WLN_zwp_linux_buffer_params_v1_error_invalid_format), "a format not offered is refused");
        wlc_end(c);
        session_open(&s);
        c = s.c;
        params = dmabuf_params(&s, dmabuf_file(16, 80, 8, 0), 0, 0, 80, 0);
        wlc_send(c, params, WLQ_zwp_linux_buffer_params_v1_create, 0L, 8L, DRM_XRGB8888, 0L);
        WLCHECK(refused_with(c, WLN_zwp_linux_buffer_params_v1_error_invalid_dimensions), "a width of nothing is refused");
        wlc_end(c);
        session_open(&s);
        c = s.c;
        params = dmabuf_params(&s, dmabuf_file(16, 80, 8, 0), 0, 0, 80, 0);
        wlc_send(c, params, WLQ_zwp_linux_buffer_params_v1_create, 100000L, 8L, DRM_XRGB8888, 0L);
        WLCHECK(refused_with(c, WLN_zwp_linux_buffer_params_v1_error_invalid_dimensions), "a width past the most is refused");
        wlc_end(c);
        session_open(&s);
        c = s.c;
        params = dmabuf_params(&s, dmabuf_file(16, 80, 8, 0), 0, 0, 32, 0);
        wlc_send(c, params, WLQ_zwp_linux_buffer_params_v1_create, 16L, 8L, DRM_XRGB8888, 0L);
        WLCHECK(refused_with(c, WLN_zwp_linux_buffer_params_v1_error_out_of_bounds), "a stride less than a row is refused");
        wlc_end(c);
        session_open(&s);
        c = s.c;
        params = dmabuf_params(&s, dmabuf_file(16, 80, 8, 0), 0, 0, 83, 0);
        wlc_send(c, params, WLQ_zwp_linux_buffer_params_v1_create, 16L, 8L, DRM_XRGB8888, 0L);
        WLCHECK(refused_with(c, WLN_zwp_linux_buffer_params_v1_error_out_of_bounds), "a stride that is not whole pixels is refused");
        wlc_end(c);

        /* params that made a buffer make no second */
        session_open(&s);
        c = s.c;
        params = dmabuf_params(&s, dmabuf_file(16, 80, 8, 0), 0, 0, 80, 0);
        wlc_send(c, params, WLQ_zwp_linux_buffer_params_v1_create, 16L, 8L, DRM_XRGB8888, 0L);
        wlc_send(c, params, WLQ_zwp_linux_buffer_params_v1_create, 16L, 8L, DRM_XRGB8888, 0L);
        WLCHECK(refused_with(c, WLN_zwp_linux_buffer_params_v1_error_already_used), "params that made a buffer make no second");
        wlc_end(c);

        /* what cannot be made is a failed event, or for the buffer made at once the client's error */
        session_open(&s);
        c = s.c;
        params = dmabuf_params(&s, dmabuf_file(16, 80, 8, 0), 0, 0, 80, 0);
        wlc_forget(c);
        wlc_send(c, params, WLQ_zwp_linux_buffer_params_v1_create, 16L, 100L, DRM_XRGB8888, 0L);
        WLCHECK(c->errors == 0 && wlc_count(c, WLI_zwp_linux_buffer_params_v1, WLE_zwp_linux_buffer_params_v1_failed) == 1,
                "a buffer larger than its file is a failed (%s)", c->error_text);
        wlc_send(c, params, WLQ_zwp_linux_buffer_params_v1_add, dmabuf_file(16, 80, 8, 0), 0L, 0L, 80L, 0L, 0L);
        WLCHECK(refused_with(c, WLN_zwp_linux_buffer_params_v1_error_already_used), "params that were used take no more");
        wlc_end(c);
        session_open(&s);
        c = s.c;
        params = dmabuf_params(&s, dmabuf_file(16, 80, 8, 0), 0, 0, 80, 1);
        wlc_forget(c);
        wlc_send(c, params, WLQ_zwp_linux_buffer_params_v1_create, 16L, 8L, DRM_XRGB8888, 0L);
        WLCHECK(c->errors == 0 && wlc_count(c, WLI_zwp_linux_buffer_params_v1, WLE_zwp_linux_buffer_params_v1_failed) == 1,
                "a modifier that was not offered is a failed");
        wlc_end(c);
        session_open(&s);
        c = s.c;
        params = dmabuf_params(&s, dmabuf_file(16, 80, 8, 0), 0, 0, 80, 1);
        wlc_send(c, params, WLQ_zwp_linux_buffer_params_v1_create_immed, wlc_id(c, WLI_wl_buffer, 1), 16L, 8L, DRM_XRGB8888, 0L);
        WLCHECK(refused_with(c, WLN_zwp_linux_buffer_params_v1_error_invalid_wl_buffer), "and for the buffer made at once it is the client's error");
        wlc_end(c);
        session_open(&s);
        c = s.c;
        params = dmabuf_params(&s, dmabuf_file(16, 80, 8, 0), 0, 0, 80, 0);
        wlc_forget(c);
        wlc_send(c, params, WLQ_zwp_linux_buffer_params_v1_create, 16L, 8L, DRM_XRGB8888, 8L);
        WLCHECK(c->errors == 0 && wlc_count(c, WLI_zwp_linux_buffer_params_v1, WLE_zwp_linux_buffer_params_v1_failed) == 1, "flags the shim does not do are a failed");
        wlc_end(c);
        session_open(&s);
        c = s.c;
        params = dmabuf_params(&s, dmabuf_file(16, 80, 8, 0), 0, 0, 80, 0);
        wlc_forget(c);
        wlc_send(c, params, WLQ_zwp_linux_buffer_params_v1_create, 16L, 8L, DRM_XRGB8888, 1L);
        wlc_pump(c);
        WLCHECK(c->errors == 0 && wlc_count(c, WLI_zwp_linux_buffer_params_v1, WLE_zwp_linux_buffer_params_v1_created) == 1,
                "Y_INVERT flag (1) on linux_buffer_params creates a buffer");
        wlc_end(c);

        /* no cap: a client holds 200 buffers made of dma-bufs, past the old 48, each with a descriptor, and params with descriptors given are closed with it */
        session_open(&s);
        c = s.c;
        {
                long failed = 0, created = 0;

                for (int at = 0; at < 200; at++)
                {
                        params = dmabuf_params(&s, dmabuf_file(4, 32, 2, 0), 0, 0, 32, 0);
                        wlc_forget(c);
                        wlc_send(c, params, WLQ_zwp_linux_buffer_params_v1_create, 4L, 2L, DRM_XRGB8888, 0L);
                        failed += (long)wlc_count(c, WLI_zwp_linux_buffer_params_v1, WLE_zwp_linux_buffer_params_v1_failed);
                        created += (long)wlc_count(c, WLI_zwp_linux_buffer_params_v1, WLE_zwp_linux_buffer_params_v1_created);
                        wlc_send(c, params, WLQ_zwp_linux_buffer_params_v1_destroy);
                }
                WLCHECK(c->errors == 0 && created == 200 && failed == 0, "a client has 200 dma-buf buffers (%ld made, %ld failed)", created, failed);
        }
        wlc_end(c);
        session_open(&s);
        c = s.c;
        for (int at = 0; at < 3; at++)
                params = dmabuf_params(&s, dmabuf_file(16, 80, 8, 0), 0, 0, 80, 0);
        wlc_send(c, params, WLQ_zwp_linux_buffer_params_v1_destroy);
        WLCHECK(c->errors == 0, "params destroyed with their descriptor close it (%s)", c->error_text);
        wlc_end(c); /* two params still holding descriptors, and the client gone */
        leak_check("dmabuf");
        WLCHECK(fds_balanced(), "every descriptor of the dma-buf rows was closed (%ld %ld)", fake.fds_open, fake.fds_closed);

        /* a version 3 client is told each pair, and has no feedback */
        session_open(&other);
        c = other.c;
        {
                unsigned long old = wl_global(c, other.registry, WLI_zwp_linux_dmabuf_v1, 3);

                WLCHECK(wlc_count(c, WLI_zwp_linux_dmabuf_v1, WLE_zwp_linux_dmabuf_v1_format) >= 2 &&
                            wlc_count(c, WLI_zwp_linux_dmabuf_v1, WLE_zwp_linux_dmabuf_v1_modifier) >= 2,
                        "a client of version 3 is told the formats and the modifiers when it binds");
                (void)old;
        }
        wlc_end(c);

        /* the host that takes none: the global is not offered, is announced to open clients when it appears, and removed when it goes */
        fake.dmabuf_off = 1;
        wayland_dmabuf_changed();
        session_open(&other);
        c = other.c;
        {
                long listed = 0;
                unsigned long dmabuf_name = 0;
                long remove_at;

                for (long at = 0; at < c->events; at++)
                        listed += c->ev[at].iface == WLI_wl_registry && c->ev[at].opcode == WLE_wl_registry_global &&
                                  !strcmp(c->ev[at].text[1], "zwp_linux_dmabuf_v1");
                WLCHECK(listed == 0 && other.dmabuf == 0, "a host that takes no dma-bufs has no global for them");

                wlc_forget(c);
                fake.dmabuf_off = 0;
                wayland_dmabuf_changed();
                wlc_pump(c);
                for (long at = 0; at < c->events; at++)
                        if (c->ev[at].iface == WLI_wl_registry && c->ev[at].opcode == WLE_wl_registry_global &&
                            !strcmp(c->ev[at].text[1], "zwp_linux_dmabuf_v1"))
                                dmabuf_name = c->ev[at].u[0];
                other.dmabuf = wl_global(c, other.registry, WLI_zwp_linux_dmabuf_v1, 4);
                feedback = wlc_id(c, WLI_zwp_linux_dmabuf_feedback_v1, 4);
                wlc_forget(c);
                wlc_send(c, other.dmabuf, WLQ_zwp_linux_dmabuf_v1_get_default_feedback, feedback);
                wlc_pump(c);
                WLCHECK(dmabuf_name != 0 && other.dmabuf != 0 && c->errors == 0 &&
                            wlc_count(c, WLI_zwp_linux_dmabuf_feedback_v1, WLE_zwp_linux_dmabuf_feedback_v1_done) == 1,
                        "an open client is offered zwp_linux_dmabuf_v1 when a render node appears (%s)", c->error_text);

                wlc_forget(c);
                fake.dmabuf_off = 1;
                wayland_dmabuf_changed();
                wlc_pump(c);
                remove_at = event_at(c, WLI_wl_registry, WLE_wl_registry_global_remove, 0);
                WLCHECK(wlc_count(c, WLI_wl_registry, WLE_wl_registry_global_remove) == 1 && remove_at >= 0 &&
                            c->ev[remove_at].u[0] == dmabuf_name,
                        "and zwp_linux_dmabuf_v1 is removed when the host no longer takes dma-bufs");
        }
        wlc_end(c);
        fake.dmabuf_off = 0;
        wayland_dmabuf_changed();
        session_open(&other);
        c = other.c;
        other.surface = wlc_id(c, WLI_wl_surface, 6);
        wlc_send(c, other.compositor, WLQ_wl_compositor_create_surface, other.surface);
        feedback = wlc_id(c, WLI_zwp_linux_dmabuf_feedback_v1, 4);
        wlc_send(c, other.dmabuf, WLQ_zwp_linux_dmabuf_v1_get_surface_feedback, feedback, other.surface);
        wlc_forget(c);
        wayland_dmabuf_changed();
        wlc_pump(c);
        WLCHECK(c->errors == 0 && wlc_count(c, WLI_zwp_linux_dmabuf_feedback_v1, WLE_zwp_linux_dmabuf_feedback_v1_done) == 1,
                "a change is sent to the feedback objects again (%s)", c->error_text);
        wlc_end(c);
        leak_check("dmabuf global");
        wayland_stop();
}

/*
        Xwayland asks a feedback of every surface it has (get_surface_feedback), and one of its default: an X server with eight windows
        mapped in one flush has nine surfaces, and nine feedbacks. The client must not be ended for the ninth (the eighth window
        never got its surface, and its serial never reached the window manager).
*/
static void test_dmabuf_feedbacks(void)
{
        struct session s;
        struct wlc *c;
        unsigned long surface, feedback;

        wl_reset();
        wayland_start(&fake_host);
        session_open(&s);
        c = s.c;
        WLCHECK(s.dmabuf != 0, "zwp_linux_dmabuf_v1 is offered for the feedbacks of nine surfaces");
        for (long at = 0; at < 9; at++)
        {
                surface = wlc_id(c, WLI_wl_surface, 6);
                wlc_send(c, s.compositor, WLQ_wl_compositor_create_surface, surface);
                feedback = wlc_id(c, WLI_zwp_linux_dmabuf_feedback_v1, 4);
                wlc_send(c, s.dmabuf, WLQ_zwp_linux_dmabuf_v1_get_surface_feedback, feedback, surface);
        }
        wlc_pump(c);
        WLCHECK(c->errors == 0 && c->malformed == 0 && wlc_count(c, WLI_zwp_linux_dmabuf_feedback_v1, WLE_zwp_linux_dmabuf_feedback_v1_done) == 9,
                "nine surface feedbacks are each answered, and the client is kept (%s)", c->error_text);
        wlc_end(c);
        leak_check("dmabuf feedbacks");
        wayland_stop();
}

/* ---- games: the pointer held, motion without a place ---- */

struct gamec
{
        struct clipc k;
        unsigned long pointer, relative;
};

/* A client with a window and a keyboard, a pointer, and the managers a game binds. */
static void game_open(struct gamec *g)
{
        struct wlc *c;

        clip_open(&g->k);
        c = g->k.s.c;
        g->pointer = wlc_id(c, WLI_wl_pointer, 9);
        wlc_send(c, g->k.s.seat, WLQ_wl_seat_get_pointer, g->pointer);
        g->k.s.relative = wl_global(c, g->k.s.registry, WLI_zwp_relative_pointer_manager_v1, 1);
        g->k.s.constraints = wl_global(c, g->k.s.registry, WLI_zwp_pointer_constraints_v1, 1);
        g->k.s.idle = wl_global(c, g->k.s.registry, WLI_zwp_idle_inhibit_manager_v1, 1);
        g->k.s.shortcuts = wl_global(c, g->k.s.registry, WLI_zwp_keyboard_shortcuts_inhibit_manager_v1, 1);
        g->k.s.tearing = wl_global(c, g->k.s.registry, WLI_wp_tearing_control_manager_v1, 1);
        g->k.s.xshell = wl_global(c, g->k.s.registry, WLI_xwayland_shell_v1, 1);
        g->k.s.xgrab = wl_global(c, g->k.s.registry, WLI_zwp_xwayland_keyboard_grab_manager_v1, 1);
        g->relative = 0;
}

static unsigned long game_lock(struct gamec *g, int confine, long lifetime)
{
        struct wlc *c = g->k.s.c;
        unsigned long object = wlc_id(c, confine ? WLI_zwp_confined_pointer_v1 : WLI_zwp_locked_pointer_v1, 1);

        wlc_send(c, g->k.s.constraints, confine ? WLQ_zwp_pointer_constraints_v1_confine_pointer : WLQ_zwp_pointer_constraints_v1_lock_pointer, object,
                 g->k.s.surface, g->pointer, 0UL, lifetime);
        return object;
}

static void test_games(void)
{
        struct gamec a, b, d;
        struct wlc *ca, *cb, *cd;
        struct wl_event_seen *e;
        unsigned long second, rel, inhibitor, lock_a;

        wl_reset();
        wayland_start(&fake_host);
        game_open(&a);
        game_open(&b);
        ca = a.k.s.c;
        cb = b.k.s.c;
        WLCHECK(a.k.s.relative && a.k.s.constraints && a.k.s.idle && a.k.s.shortcuts && a.k.s.tearing, "the games' globals are offered");

        /* relative motion of the pointer over a window: each report of the hand, the motion with the gain and the hand's own
           counts apart, at the device's microsecond, in a frame of its own; a place the pointer moved to is no relative motion */
        wayland_keyboard_to(a.k.row);
        wayland_pointer_to(a.k.row, 5, 5);
        rel = wlc_id(ca, WLI_zwp_relative_pointer_v1, 1);
        wlc_send(ca, a.k.s.relative, WLQ_zwp_relative_pointer_manager_v1_get_relative_pointer, rel, a.pointer);
        wlc_forget(ca);
        wayland_pointer_motion(a.k.row, 9, 12);
        wlc_pump(ca);
        WLCHECK(wlc_count(ca, WLI_wl_pointer, WLE_wl_pointer_motion) == 1 &&
                    wlc_count(ca, WLI_zwp_relative_pointer_v1, WLE_zwp_relative_pointer_v1_relative_motion) == 0,
                "a place the pointer moved to is a motion and no relative motion (a place stops at the screen's edge)");
        wlc_forget(ca);
        wayland_pointer_relative(a.k.row, 5, 9, 3, 5, 9000);
        wayland_pointer_relative(a.k.row, 3, 5, 1, 2, 9001);
        wlc_pump(ca);
        WLCHECK(ca->events == 0, "the reports' counts wait for the frame of the pointer's motion");
        wayland_pointer_motion(a.k.row, 10, 14);
        wlc_pump(ca);
        WLCHECK(wlc_count(ca, WLI_wl_pointer, WLE_wl_pointer_frame) == 1 && wlc_count(ca, WLI_wl_pointer, WLE_wl_pointer_motion) == 1 &&
                    wlc_count(ca, WLI_zwp_relative_pointer_v1, WLE_zwp_relative_pointer_v1_relative_motion) == 1,
                "and go in that frame, with the place (Xwayland makes a raw event of a place alone)");
        e = wlc_take(ca, WLI_zwp_relative_pointer_v1, WLE_zwp_relative_pointer_v1_relative_motion);
        WLCHECK(e && e->u[1] == 9001 && e->u[2] == 8 * 256 && e->u[3] == 14 * 256 && e->u[4] == 4 * 256 && e->u[5] == 7 * 256,
                "summed: the motion with the gain, then the hand's counts as they came, at the last report's microsecond");
        wlc_forget(ca);
        wayland_pointer_relative(a.k.row, 0, 0, 2, -1, 9004);
        wayland_pointer_counts_flush();
        wlc_pump(ca);
        e = wlc_take(ca, WLI_zwp_relative_pointer_v1, WLE_zwp_relative_pointer_v1_relative_motion);
        WLCHECK(e && e->u[2] == 0 && e->u[4] == 2 * 256 && wlc_count(ca, WLI_wl_pointer, WLE_wl_pointer_frame) == 1 &&
                    wlc_count(ca, WLI_wl_pointer, WLE_wl_pointer_motion) == 0,
                "counts no motion took (the pointer at the screen's edge) are sent at the pass's end, in a frame of their own");
        wlc_forget(cb);
        wayland_pointer_relative(b.k.row, 8, 14, 4, 7, 9002);
        wlc_pump(cb);
        WLCHECK(cb->events == 0, "a window the pointer is not over is told nothing of the hand");

        /* a lock holds while the window is under the pointer and has the keys, and the hand's counts are the client's */
        lock_a = game_lock(&a, 0, WLN_zwp_pointer_constraints_v1_lifetime_persistent);
        wlc_pump(ca);
        e = wlc_take(ca, WLI_zwp_locked_pointer_v1, WLE_zwp_locked_pointer_v1_locked);
        WLCHECK(e && fake.hold_row == a.k.row && fake.hold_mode == 1, "a lock on the window under the pointer that has the keys holds it (%ld, mode %ld)",
                fake.hold_row, fake.hold_mode);
        wlc_forget(ca);
        wayland_pointer_relative(a.k.row, 5, -3, 5, -3, 7000);
        wlc_pump(ca);
        e = wlc_take(ca, WLI_zwp_relative_pointer_v1, WLE_zwp_relative_pointer_v1_relative_motion);
        WLCHECK(e && e->u[0] == 0 && e->u[1] == 7000 && e->u[2] == 5 * 256 && e->u[3] == (0xffffffffUL & (unsigned long)(-3 * 256)),
                "the hand's counts of a locked pointer are relative motion, with the time in microseconds");
        WLCHECK(wlc_count(ca, WLI_wl_pointer, WLE_wl_pointer_motion) == 0, "and no motion of a place");

        /* Xwayland's warp at each motion of an X game (a hint, then a commit with nothing pending) is quiet: nothing the host shows
           changes, and the loop's turn need not be whole (compositor_quietshot); a commit with damage or a frame asked is not */
        {
                long heard, quiet, heard_now, quiet_now;

                heard = wayland_heard(&quiet);
                wlc_send(ca, lock_a, WLQ_zwp_locked_pointer_v1_set_cursor_position_hint, 3UL * 256, 4UL * 256);
                wlc_send(ca, a.k.s.surface, WLQ_wl_surface_commit);
                wlc_send(ca, lock_a, WLQ_zwp_locked_pointer_v1_set_cursor_position_hint, 5UL * 256, 6UL * 256);
                wlc_send(ca, a.k.s.surface, WLQ_wl_surface_commit);
                heard_now = wayland_heard(&quiet_now);
                WLCHECK(heard_now - heard == 4 && quiet_now - quiet == 4 && ca->errors == 0,
                        "a lock's hint and a commit with nothing pending are quiet (%ld heard, %ld quiet)", heard_now - heard, quiet_now - quiet);
                heard = wayland_heard(&quiet);
                wlc_send(ca, a.k.s.surface, WLQ_wl_surface_damage_buffer, 0L, 0L, 4L, 4L);
                wlc_send(ca, a.k.s.surface, WLQ_wl_surface_commit);
                heard_now = wayland_heard(&quiet_now);
                WLCHECK(heard_now - heard == 2 && quiet_now == quiet, "a commit of damage is not quiet, nor the damage (%ld heard, %ld quiet)",
                        heard_now - heard, quiet_now - quiet);
                heard = wayland_heard(&quiet);
                wlc_send(ca, a.k.s.surface, WLQ_wl_surface_frame, wlc_id(ca, WLI_wl_callback, 1));
                wlc_send(ca, a.k.s.surface, WLQ_wl_surface_commit);
                heard_now = wayland_heard(&quiet_now);
                WLCHECK(heard_now - heard == 2 && quiet_now == quiet, "a commit that takes a frame callback is not quiet (%ld quiet)", quiet_now - quiet);
                wlc_pump(ca);
                wlc_forget(ca);
        }

        /* Alt-Tab to another window ends it, and it holds again when its window has the keys again (persistent); the place the
           client hinted is where the host is told the pointer is, in the window's picture */
        wlc_forget(ca);
        wayland.hint_row = -1;
        wlc_send(ca, lock_a, WLQ_zwp_locked_pointer_v1_set_cursor_position_hint, 12UL * 256, 34UL * 256);
        wayland_keyboard_to(b.k.row);
        wlc_pump(ca);
        e = wlc_take(ca, WLI_zwp_locked_pointer_v1, WLE_zwp_locked_pointer_v1_unlocked);
        WLCHECK(e && fake.hold_mode == 0, "the keys going to another window let the pointer go and say so");
        WLCHECK(wayland.hint_row == a.k.row && wayland.hint_x == 12 && wayland.hint_y == 34,
                "a lock let go of tells where its client hinted the pointer is (row %ld at %ld, %ld)", wayland.hint_row, wayland.hint_x,
                wayland.hint_y);
        wayland.hint_row = -1;
        wlc_forget(ca);
        wayland_keyboard_to(a.k.row);
        wlc_pump(ca);
        e = wlc_take(ca, WLI_zwp_locked_pointer_v1, WLE_zwp_locked_pointer_v1_locked);
        WLCHECK(e && fake.hold_mode == 1, "a persistent lock holds again when its window has the keys again");
        wlc_forget(ca);
        wayland_pointer_to(a.k.row, -1, -1);
        wayland_pointer_to(-1, 0, 0);
        wlc_pump(ca);
        WLCHECK(wlc_count(ca, WLI_zwp_locked_pointer_v1, WLE_zwp_locked_pointer_v1_unlocked) == 1 && fake.hold_mode == 0,
                "the pointer leaving the window lets it go");
        wayland_pointer_to(a.k.row, 5, 5);

        /* a second constraint on the surface is the client's error */
        game_lock(&a, 1, WLN_zwp_pointer_constraints_v1_lifetime_oneshot);
        WLCHECK(refused_with(ca, WLN_zwp_pointer_constraints_v1_error_already_constrained), "a surface with a constraint takes no second");

        /* a lock destroyed while it holds lets the pointer go, with no event of its own */
        wlc_end(cb);
        game_open(&b);
        cb = b.k.s.c;
        wayland_keyboard_to(b.k.row);
        wayland_pointer_to(b.k.row, 3, 3);
        second = game_lock(&b, 0, WLN_zwp_pointer_constraints_v1_lifetime_persistent);
        WLCHECK(fake.hold_row == b.k.row && fake.hold_mode == 1, "another client's window holds it, now that it is the one with the keys");
        wlc_forget(cb);
        wlc_send(cb, second, WLQ_zwp_locked_pointer_v1_destroy);
        WLCHECK(fake.hold_mode == 0 && wlc_count(cb, WLI_zwp_locked_pointer_v1, WLE_zwp_locked_pointer_v1_unlocked) == 0 && cb->errors == 0,
                "a lock that is destroyed lets the pointer go");

        /* a one-shot lock is over the first time it is let go; a confinement holds the window the same way */
        second = game_lock(&b, 0, WLN_zwp_pointer_constraints_v1_lifetime_oneshot);
        WLCHECK(fake.hold_mode == 1, "a one-shot lock holds");
        wayland_keyboard_to(a.k.row);
        wayland_keyboard_to(b.k.row);
        WLCHECK(fake.hold_mode == 0, "and does not hold again after the keys went away: it was one-shot");
        wlc_send(cb, second, WLQ_zwp_locked_pointer_v1_destroy);
        second = game_lock(&b, 1, WLN_zwp_pointer_constraints_v1_lifetime_persistent);
        wlc_pump(cb);
        e = wlc_take(cb, WLI_zwp_confined_pointer_v1, WLE_zwp_confined_pointer_v1_confined);
        WLCHECK(e && fake.hold_mode == 2 && fake.hold_row == b.k.row, "a confinement holds the window, mode 2");
        wlc_forget(cb);
        wayland_pointer_relative(b.k.row, 6, 2, 3, 1, 9003);
        wlc_pump(cb);
        WLCHECK(cb->events == 0, "a client with no relative pointer is sent nothing for a report, not even a frame");
        wlc_send(cb, second, WLQ_zwp_confined_pointer_v1_destroy);

        /* the surface gone while it is held, and the client gone while it is held */
        second = game_lock(&b, 0, WLN_zwp_pointer_constraints_v1_lifetime_persistent);
        WLCHECK(fake.hold_mode == 1, "a lock again");
        {
                long held = wayland.constraints;

                wlc_send(cb, b.k.s.surface, WLQ_wl_surface_destroy);
                WLCHECK(wayland.constraints == held - 1, "the constraint of a destroyed surface is gone");
        }
        WLCHECK(fake.hold_mode == 0 && cb->errors == 0,
                "the surface that is destroyed while it is held lets the pointer go (%s)", cb->error_text);
        wlc_end(cb);
        game_open(&b);
        cb = b.k.s.c;
        wayland_keyboard_to(b.k.row);
        wayland_pointer_to(b.k.row, 3, 3);
        game_lock(&b, 0, WLN_zwp_pointer_constraints_v1_lifetime_persistent);
        WLCHECK(fake.hold_mode == 1, "and again");
        wlc_end(cb);
        WLCHECK(fake.hold_mode == 0 && fake.hold_row < 0, "a client that goes while it holds the pointer lets it go");

        /* the rules: a lifetime that is neither, the number of constraints and relative pointers */
        game_open(&d);
        cd = d.k.s.c;
        {
                unsigned long bad = wlc_id(cd, WLI_zwp_locked_pointer_v1, 1);

                wlc_send(cd, d.k.s.constraints, WLQ_zwp_pointer_constraints_v1_lock_pointer, bad, d.k.s.surface, d.pointer, 0UL, 7L);
                WLCHECK(cd->errors == 1 && cd->error_code == WAYLAND_INVALID_METHOD, "a lifetime that is neither is refused");
        }
        wlc_end(cd);
        game_open(&d);
        cd = d.k.s.c;
        for (int at = 0; at < 40 && !cd->errors; at++)
        {
                unsigned long surface = wlc_id(cd, WLI_wl_surface, 6), object = wlc_id(cd, WLI_zwp_locked_pointer_v1, 1);

                wlc_send(cd, d.k.s.compositor, WLQ_wl_compositor_create_surface, surface);
                wlc_send(cd, d.k.s.constraints, WLQ_zwp_pointer_constraints_v1_lock_pointer, object, surface, d.pointer, 0UL, 2L);
        }
        WLCHECK(cd->errors == 0, "a client holds as many constraints as it makes: forty (%s)", cd->errors ? cd->error_text : "none refused");
        wlc_end(cd);
        game_open(&d);
        cd = d.k.s.c;
        for (int at = 0; at < 40 && !cd->errors; at++)
                wlc_send(cd, d.k.s.relative, WLQ_zwp_relative_pointer_manager_v1_get_relative_pointer, wlc_id(cd, WLI_zwp_relative_pointer_v1, 1), d.pointer);
        WLCHECK(cd->errors == 0, "and as many relative pointers: forty (%s)", cd->errors ? cd->error_text : "none refused");
        wlc_end(cd);

        /* what a game asks that the desktop does not do: taken, and nothing happens */
        game_open(&d);
        cd = d.k.s.c;
        inhibitor = wlc_id(cd, WLI_zwp_idle_inhibitor_v1, 1);
        wlc_send(cd, d.k.s.idle, WLQ_zwp_idle_inhibit_manager_v1_create_inhibitor, inhibitor, d.k.s.surface);
        wlc_send(cd, inhibitor, WLQ_zwp_idle_inhibitor_v1_destroy);
        WLCHECK(cd->errors == 0, "an idle inhibitor is taken and let go (%s)", cd->error_text);
        inhibitor = wlc_id(cd, WLI_zwp_keyboard_shortcuts_inhibitor_v1, 1);
        wlc_forget(cd);
        wlc_send(cd, d.k.s.shortcuts, WLQ_zwp_keyboard_shortcuts_inhibit_manager_v1_inhibit_shortcuts, inhibitor, d.k.s.surface, d.k.s.seat);
        wayland_keyboard_to(d.k.row);
        wlc_pump(cd);
        WLCHECK(cd->errors == 0 && wlc_count(cd, WLI_zwp_keyboard_shortcuts_inhibitor_v1, WLE_zwp_keyboard_shortcuts_inhibitor_v1_active) == 1,
                "a shortcuts inhibitor is active on the window with the keys, and says so");
        wlc_send(cd, d.k.s.shortcuts, WLQ_zwp_keyboard_shortcuts_inhibit_manager_v1_inhibit_shortcuts, wlc_id(cd, WLI_zwp_keyboard_shortcuts_inhibitor_v1, 1),
                 d.k.s.surface, d.k.s.seat);
        WLCHECK(refused_with(cd, WLN_zwp_keyboard_shortcuts_inhibit_manager_v1_error_already_inhibited), "a surface has one inhibitor");
        wlc_end(cd);

        /* what an X server asks of its windows (xwayland_shell_v1, the keyboard grab): a surface with no role gets its
           xwayland_surface_v1 and a serial once; a second serial, a zero one and a surface with a role of its own are refused;
           the serial is known while no window has the surface, and the grab holds the keys until its destroy */
        game_open(&d);
        cd = d.k.s.c;
        WLCHECK(d.k.s.xshell && d.k.s.xgrab, "the Xwayland shell and the keyboard grab manager are offered");
        {
                unsigned long surface = wlc_id(cd, WLI_wl_surface, 6), xwin = wlc_id(cd, WLI_xwayland_surface_v1, 1);
                unsigned long manager = wl_global(cd, d.k.s.registry, WLI_zxdg_output_manager_v1, 3), xdg = wlc_id(cd, WLI_zxdg_output_v1, 3);
                struct wl_event_seen *size, *scale;
                long was = fake.screen.scale;

                // At 200 percent an X server is told its screen as the pixels it is: its windows are drawn a buffer pixel a pixel, and
                // its root is the compositor's pixels (xwm.c's origin aside), so a drag's or a pointer's X place is the compositor's.
                fake.screen.scale = 200;
                wayland_screens_changed();
                wlc_send(cd, manager, WLQ_zxdg_output_manager_v1_get_xdg_output, xdg, d.k.s.output);
                wlc_pump(cd);
                size = wlc_take(cd, WLI_zxdg_output_v1, WLE_zxdg_output_v1_logical_size);
                WLCHECK(size && size->u[0] == (unsigned long)fake.screen.width / 2, "before it has an X window a client is told its screen in units (%lu)",
                        size ? size->u[0] : 0);
                wlc_forget(cd);
                wlc_send(cd, d.k.s.compositor, WLQ_wl_compositor_create_surface, surface);
                wlc_send(cd, d.k.s.xshell, WLQ_xwayland_shell_v1_get_xwayland_surface, xwin, surface);
                WLCHECK(cd->errors == 0, "a surface with no role gets an xwayland_surface_v1 (%s)", cd->error_text);
                wlc_pump(cd);
                size = wlc_take(cd, WLI_zxdg_output_v1, WLE_zxdg_output_v1_logical_size);
                scale = wlc_take(cd, WLI_wl_output, WLE_wl_output_scale);
                WLCHECK(size && size->u[0] == (unsigned long)fake.screen.width && size->u[1] == (unsigned long)fake.screen.height && scale && scale->u[0] == 1,
                        "the client whose surface is an X window is the X server: its screen told again as its pixels, at scale 1 (%lu by %lu)",
                        size ? size->u[0] : 0, size ? size->u[1] : 0);
                fake.screen.scale = was;
                wayland_screens_changed();
                wlc_send(cd, xwin, WLQ_xwayland_surface_v1_set_serial, 0x1234UL, 0UL);
                WLCHECK(cd->errors == 0 && wayland_serial_row(cd->slot, 0x1234) == -1,
                        "its serial is known to the window manager, with no window yet (%s)", cd->error_text);
                wlc_send(cd, xwin, WLQ_xwayland_surface_v1_set_serial, 0x5678UL, 0UL);
                WLCHECK(refused_with(cd, WLN_xwayland_surface_v1_error_already_associated), "a surface has one serial");
        }
        wlc_end(cd);
        game_open(&d);
        cd = d.k.s.c;
        wlc_send(cd, d.k.s.xshell, WLQ_xwayland_shell_v1_get_xwayland_surface, wlc_id(cd, WLI_xwayland_surface_v1, 1), d.k.s.surface);
        WLCHECK(refused_with(cd, WLN_xwayland_shell_v1_error_role), "a window's surface, with its role, is refused an xwayland_surface_v1");
        wlc_end(cd);
        game_open(&d);
        cd = d.k.s.c;
        {
                unsigned long surface = wlc_id(cd, WLI_wl_surface, 6), xwin = wlc_id(cd, WLI_xwayland_surface_v1, 1);

                wlc_send(cd, d.k.s.compositor, WLQ_wl_compositor_create_surface, surface);
                wlc_send(cd, d.k.s.xshell, WLQ_xwayland_shell_v1_get_xwayland_surface, xwin, surface);
                wlc_send(cd, xwin, WLQ_xwayland_surface_v1_set_serial, 0UL, 0UL);
                WLCHECK(refused_with(cd, WLN_xwayland_surface_v1_error_invalid_serial), "a zero serial is refused");
        }
        wlc_end(cd);
        game_open(&d);
        cd = d.k.s.c;
        {
                unsigned long grab = wlc_id(cd, WLI_zwp_xwayland_keyboard_grab_v1, 1);

                wlc_send(cd, d.k.s.xgrab, WLQ_zwp_xwayland_keyboard_grab_manager_v1_grab_keyboard, grab, d.k.s.surface, d.k.s.seat);
                wayland_keyboard_to(d.k.row);
                WLCHECK(cd->errors == 0 && wayland_row_shortcuts(d.k.row) == 1, "a keyboard grab holds the keys of its window while it has them (%s)", cd->error_text);
                wlc_send(cd, grab, WLQ_zwp_xwayland_keyboard_grab_v1_destroy);
                WLCHECK(cd->errors == 0 && wayland_row_shortcuts(d.k.row) == 0, "and lets them go with its destroy (%s)", cd->error_text);
                {
                        unsigned long grab2 = wlc_id(cd, WLI_zwp_xwayland_keyboard_grab_v1, 1);

                        wlc_send(cd, d.k.s.xgrab, WLQ_zwp_xwayland_keyboard_grab_manager_v1_grab_keyboard, grab2, d.k.s.surface, d.k.s.seat);
                        WLCHECK(cd->errors == 0 && wayland_row_shortcuts(d.k.row) == 1, "a keyboard grab on an already-focused window is active immediately (%s)", cd->error_text);
                        wlc_send(cd, grab2, WLQ_zwp_xwayland_keyboard_grab_v1_destroy);
                        WLCHECK(cd->errors == 0 && wayland_row_shortcuts(d.k.row) == 0, "and clears shortcut_active on destroy (%s)", cd->error_text);
                }
        }
        wlc_end(cd);
        /* the X server's protocols are offered to the user the X window manager's socket takes and to no other, and to nobody while
           none is named: a client of another user sees neither global, and binding either by its name is refused as no such
           global, so it cannot name an X window's serial before its X server does */
        for (int who = 0; who < 2; who++)
        {
                wl_check_xuser = who ? 2000 : -1; /* the clients of wlc_open are 1000 */
                game_open(&d);
                cd = d.k.s.c;
                WLCHECK(!d.k.s.xshell && !d.k.s.xgrab, "the Xwayland shell and the keyboard grab manager are not offered to %s",
                        who ? "another user's client" : "any client while no user is named");
                wlc_end(cd);
                for (long name = 1; name <= WAYLAND_GLOBAL_COUNT; name++)
                {
                        long iface = wayland_globals[name - 1].iface;

                        if (iface != WLI_xwayland_shell_v1 && iface != WLI_zwp_xwayland_keyboard_grab_manager_v1)
                                continue;
                        game_open(&d);
                        cd = d.k.s.c;
                        wlc_send(cd, d.k.s.registry, WLQ_wl_registry_bind, (unsigned long)name, wl_interfaces[iface].name, 1L,
                                 wlc_id(cd, iface, 1));
                        WLCHECK(refused_with(cd, WAYLAND_INVALID_OBJECT), "%s, bound by its name, is refused to %s", wl_interfaces[iface].name,
                                who ? "another user's client" : "any client while no user is named");
                        wlc_end(cd);
                }
        }
        wl_check_xuser = 1000;
        game_open(&d);
        cd = d.k.s.c;
        {
                unsigned long control = wlc_id(cd, WLI_wp_tearing_control_v1, 1);

                wlc_send(cd, d.k.s.tearing, WLQ_wp_tearing_control_manager_v1_get_tearing_control, control, d.k.s.surface);
                wlc_send(cd, control, WLQ_wp_tearing_control_v1_set_presentation_hint, (long)WLN_wp_tearing_control_v1_presentation_hint_async);
                WLCHECK(cd->errors == 0, "tearing control is taken, and the hint (%s)", cd->error_text);
                wlc_send(cd, control, WLQ_wp_tearing_control_v1_set_presentation_hint, 9L);
                WLCHECK(cd->errors == 1 && cd->error_code == WAYLAND_INVALID_METHOD, "a hint that is neither is refused");
        }
        wlc_end(cd);
        game_open(&d);
        cd = d.k.s.c;
        {
                unsigned long control = wlc_id(cd, WLI_wp_tearing_control_v1, 1);

                wlc_send(cd, d.k.s.tearing, WLQ_wp_tearing_control_manager_v1_get_tearing_control, control, d.k.s.surface);
                wlc_send(cd, d.k.s.tearing, WLQ_wp_tearing_control_manager_v1_get_tearing_control, wlc_id(cd, WLI_wp_tearing_control_v1, 1), d.k.s.surface);
                WLCHECK(refused_with(cd, WLN_wp_tearing_control_manager_v1_error_tearing_control_exists), "a surface has one tearing control");
        }
        wlc_end(cd);
        wlc_end(ca);
        leak_check("games");
        WLCHECK(fake.hold_mode == 0, "nothing is held when everything is gone");
        WLCHECK(fds_balanced(), "descriptors balance (%ld %ld)", fake.fds_open, fake.fds_closed);
        wayland_stop();
}


/*
        The scale, from the screen to the surface. The screen changed is told whole and in order. A surface is told the scale of
        the screen it is on, as a fractional scale and as a whole one, whenever either is another; a window follows what it
        is told in the size it is asked for and in the places of the pointer it is sent; and a viewport is judged as the
        protocol says, every number the client's.
*/
static long event_at(struct wlc *c, long iface, long opcode, long from)
{
        for (long at = from; at < c->events; at++)
                if (c->ev[at].iface == iface && c->ev[at].opcode == opcode)
                        return at;
        return -1;
}

/* A toplevel made and acknowledged with no buffer: with a fractional scale and a viewport when the globals are given. */
struct scale_window
{
        struct session s;
        unsigned long manager, viewporter, fractional, viewport;
        long whole, preferred; // what the surface was told as it was made: the whole scale, and the fractional one (0 for none told)
};

static void scale_open(struct scale_window *w, int fractional, int viewport)
{
        struct wlc *c;
        struct wl_event_seen *configure, *told;

        memset(w, 0, sizeof *w);
        session_open(&w->s);
        c = w->s.c;
        w->manager = wl_global(c, w->s.registry, WLI_wp_fractional_scale_manager_v1, 1);
        w->viewporter = wl_global(c, w->s.registry, WLI_wp_viewporter, 1);
        w->s.surface = wlc_id(c, WLI_wl_surface, 6);
        wlc_send(c, w->s.compositor, WLQ_wl_compositor_create_surface, w->s.surface);
        told = wlc_take(c, WLI_wl_surface, WLE_wl_surface_preferred_buffer_scale);
        w->whole = told ? (long)told->u[0] : 0;
        if (fractional)
        {
                w->fractional = wlc_id(c, WLI_wp_fractional_scale_v1, 1);
                wlc_send(c, w->manager, WLQ_wp_fractional_scale_manager_v1_get_fractional_scale, w->fractional, w->s.surface);
                told = wlc_take(c, WLI_wp_fractional_scale_v1, WLE_wp_fractional_scale_v1_preferred_scale);
                w->preferred = told ? (long)told->u[0] : 0;
        }
        if (viewport)
        {
                w->viewport = wlc_id(c, WLI_wp_viewport, 1);
                wlc_send(c, w->viewporter, WLQ_wp_viewporter_get_viewport, w->viewport, w->s.surface);
        }
        w->s.xdg = wlc_id(c, WLI_xdg_surface, 6);
        wlc_send(c, w->s.wm_base, WLQ_xdg_wm_base_get_xdg_surface, w->s.xdg, w->s.surface);
        w->s.toplevel = wlc_id(c, WLI_xdg_toplevel, 6);
        wlc_send(c, w->s.xdg, WLQ_xdg_surface_get_toplevel, w->s.toplevel);
        wlc_send(c, w->s.surface, WLQ_wl_surface_commit);
        configure = wlc_take(c, WLI_xdg_surface, WLE_xdg_surface_configure);
        if (configure)
                wlc_send(c, w->s.xdg, WLQ_xdg_surface_ack_configure, (long)configure->u[0]);
}

/* A buffer of width by height shown as the window's picture. */
static void scale_show(struct scale_window *w, long width, long height)
{
        unsigned long buffer = wl_buffer_of(&w->s, width, height, 0xff336699, WLN_wl_shm_format_xrgb8888);

        wlc_send(w->s.c, w->s.surface, WLQ_wl_surface_attach, buffer, 0L, 0L);
        wlc_send(w->s.c, w->s.surface, WLQ_wl_surface_damage_buffer, 0L, 0L, width, height);
        wlc_send(w->s.c, w->s.surface, WLQ_wl_surface_commit);
}

/* ---- a client that reads nothing for a while: its queue grows, and it is all there when it reads ---- */

// The events queued for a client, walked by their heads (none read off): how many there are of an object's opcode, and whether
// every head is whole (a size of at least eight, a multiple of four, inside what is queued).
static long backlog_count(long slot, unsigned long object, long opcode, long *events, int *whole)
{
        const unsigned char *bytes;
        const long *fds;
        long fd_count, length = wayland_out(slot, &bytes, &fds, &fd_count), found = 0, at = 0;

        *events = 0;
        *whole = 1;
        while (at + 8 <= length)
        {
                long size = (long)(wayland_get(bytes + at + 4) >> 16);

                if (size < 8 || size % 4 || at + size > length)
                {
                        *whole = 0;
                        break;
                }
                if (wayland_get(bytes + at) == object && (long)(wayland_get(bytes + at + 4) & 0xffff) == opcode)
                        found++;
                ++*events;
                at += size;
        }
        if (at != length)
                *whole = 0;
        return found;
}

// What is queued, taken off in sends of `step` bytes (a socket that takes a little at a time), every send's bytes the queue's front.
static long backlog_drain(long slot, long step)
{
        const unsigned char *bytes;
        const long *fds;
        long fd_count, length, taken = 0;

        while ((length = wayland_out(slot, &bytes, &fds, &fd_count)) > 0)
        {
                long take = length < step ? length : step;

                wayland_sent(slot, take);
                taken += take;
        }
        return taken;
}

static void test_backlog(void)
{
        struct gamec g;
        struct wlc *c;
        unsigned char sync[12 * 1365]; // one read of the host's (16384 bytes) of wl_display.sync, as near as whole ones go
        unsigned long id;
        long events, done, rounds = 0;
        int whole;

        wl_reset();
        wayland_start(&fake_host);

        /* replies to requests: a client that sends and does not read is queued all of them, past the 64 KiB a queue starts at */
        game_open(&g);
        c = g.k.s.c;
        wlc_pump(c);
        wlc_forget(c);
        id = wlc_id(c, WLI_wl_callback, 1); // the callback's id, let go of by each reply and made again by the next sync
        for (long at = 0; at < 1365; at++)
        {
                wayland_put(sync + 12 * at, 1);
                wayland_put(sync + 12 * at + 4, (unsigned long)WLQ_wl_display_sync | 12ul << 16);
                wayland_put(sync + 12 * at + 8, id);
        }
        wlc_hold = 1;
        for (int read = 0; read < 8; read++)
                wayland_feed(c->slot, sync, sizeof sync, 0, 0);
        done = backlog_count(c->slot, id, WLE_wl_callback_done, &events, &whole);
        WLCHECK(!wayland_dead(c->slot) && whole && done == 8 * 1365 && events == 2 * 8 * 1365 && !wayland_out_backlog(c->slot),
                "a client that reads nothing is queued every reply to eight reads of syncs (%ld done of %ld events, %ld bytes)", done, events,
                (long)(events * 12));
        /* and a locked pointer's reports, the hand moving at 8 kHz while it does not read: 20,000 of them, 800 KB */
        wayland_keyboard_to(g.k.row);
        wayland_pointer_to(g.k.row, 5, 5);
        {
                unsigned long rel = wlc_id(c, WLI_zwp_relative_pointer_v1, 1);

                wlc_send(c, g.k.s.relative, WLQ_zwp_relative_pointer_manager_v1_get_relative_pointer, rel, g.pointer);
                game_lock(&g, 0, WLN_zwp_pointer_constraints_v1_lifetime_persistent);
                for (long report = 0; report < 20000; report++)
                        wayland_pointer_relative(g.k.row, 1, -1, 1, -1, 1000 + report);
                done = backlog_count(c->slot, rel, WLE_zwp_relative_pointer_v1_relative_motion, &events, &whole);
                WLCHECK(!wayland_dead(c->slot) && whole && done == 20000 && wayland_out_backlog(c->slot),
                        "and every one of 20,000 reports of the hand, each in its frame, past the mark the host stops reading at (%ld)", done);
        }
        /* it reads: a little at a time, all of it in its order, the client alive, the mark passed back */
        backlog_drain(c->slot, 4096 + 4);
        wlc_hold = 0;
        WLCHECK(!wayland_dead(c->slot) && !wayland_out_dirty(c->slot) && !wayland_out_backlog(c->slot) && c->errors == 0,
                "a client that then reads takes it all and is kept, with no error, and the host reads it again");
        /* the queue keeps what it grew to and starts from its front: a send of all of it after more reports leaves nothing */
        wayland_pointer_relative(g.k.row, 1, -1, 1, -1, 99);
        {
                long found = backlog_count(c->slot, 0, -1, &events, &whole), taken = backlog_drain(c->slot, 1 << 20);

                WLCHECK(found == 0 && whole && events == 2 && taken == 40, "after it, a report is its two events at the queue's front (%ld %ld)",
                        events, taken);
        }

        /* a client that reads nothing at all: past WAYLAND_OUT_MOST it is ended (a full queue), and everything comes back */
        wlc_hold = 1;
        while (!wayland_dead(c->slot) && rounds++ < 1000000)
                wayland_pointer_relative(g.k.row, 1, -1, 1, -1, 7);
        WLCHECK(wayland_dead(c->slot) == 2 && rounds * 40 >= WAYLAND_OUT_MOST - WAYLAND_EVENT - 40 && rounds * 40 <= WAYLAND_OUT_MOST,
                "a client that reads none of WAYLAND_OUT_MOST of its events is ended as a full queue is (after %ld reports, held %ld)", rounds,
                fake.hold_mode);
        wlc_hold = 0;
        wlc_end(c);
        leak_check("a backed-up queue");
        wayland_stop();
}

static void test_scale(void)
{
        struct scale_window w;
        struct wlc *c;
        struct wl_event_seen *e;
        unsigned long xdg_manager, xdg_output, pointer;
        long a, b, d, f, g, h;

        wl_reset();
        wayland_start(&fake_host);

        /* the screen, changed: told whole, in the order a client applies it */
        scale_open(&w, 0, 0);
        c = w.s.c;
        xdg_manager = wl_global(c, w.s.registry, WLI_zxdg_output_manager_v1, 3);
        WLCHECK(w.manager && w.viewporter && xdg_manager, "the fractional scale, the viewporter and xdg-output are offered");
        xdg_output = wlc_id(c, WLI_zxdg_output_v1, 3);
        wlc_forget(c);
        wlc_send(c, xdg_manager, WLQ_zxdg_output_manager_v1_get_xdg_output, xdg_output, w.s.output);
        a = event_at(c, WLI_zxdg_output_v1, WLE_zxdg_output_v1_logical_size, 0);
        WLCHECK(a >= 0 && c->ev[a].u[0] == 1280 && c->ev[a].u[1] == 720 &&
                    wlc_count(c, WLI_zxdg_output_v1, WLE_zxdg_output_v1_done) == 0 &&
                    wlc_count(c, WLI_wl_output, WLE_wl_output_done) == 1,
                "an xdg_output of version 3 says the logical size and its change is the wl_output's done, not its own");
        wlc_forget(c);
        fake.screen.scale = 150;
        wayland_screens_changed();
        wlc_pump(c);
        a = event_at(c, WLI_zxdg_output_v1, WLE_zxdg_output_v1_logical_position, 0);
        b = event_at(c, WLI_zxdg_output_v1, WLE_zxdg_output_v1_logical_size, 0);
        d = event_at(c, WLI_wl_output, WLE_wl_output_geometry, 0);
        f = event_at(c, WLI_wl_output, WLE_wl_output_mode, 0);
        g = event_at(c, WLI_wl_output, WLE_wl_output_scale, 0);
        h = event_at(c, WLI_wl_output, WLE_wl_output_done, 0);
        WLCHECK(a >= 0 && a < b && b < d && d < f && f < g && g < h,
                "the screen changed is told whole: the xdg_output's position and size, then the wl_output's geometry, mode, scale and done");
        WLCHECK(b >= 0 && c->ev[b].u[0] == 853 && c->ev[b].u[1] == 480 && g >= 0 && c->ev[g].u[0] == 2,
                "at 150 percent the logical size is 853 by 480 and the whole scale is 2");
        WLCHECK(wlc_count(c, WLI_zxdg_output_v1, WLE_zxdg_output_v1_done) == 0 && wlc_count(c, WLI_wl_output, WLE_wl_output_name) == 1,
                "and the name comes once, with no xdg_output done");
        fake.screen.scale = 125;
        wlc_forget(c);
        wayland_screens_changed();
        wlc_pump(c);
        b = event_at(c, WLI_zxdg_output_v1, WLE_zxdg_output_v1_logical_size, 0);
        g = event_at(c, WLI_wl_output, WLE_wl_output_scale, 0);
        WLCHECK(b >= 0 && c->ev[b].u[0] == 1024 && c->ev[b].u[1] == 576 && g >= 0 && c->ev[g].u[0] == 2,
                "at 125 percent it is 1024 by 576 and the whole scale 2 (the next whole one up)");
        wlc_end(c);

        /* a window that follows the fractional scale, on a screen at 150 percent */
        fake.screen.scale = 150;
        wayland_screens_changed();
        scale_open(&w, 1, 1); // a client of its own: the one before was ended (w.s.c is that one, freed, and was forgotten here)
        c = w.s.c;
        WLCHECK(w.whole == 2, "a surface made at 150 percent is told the whole scale 2");
        WLCHECK(w.preferred == 180, "and its fractional scale is 180 as the object is made");
        wlc_send(c, w.viewport, WLQ_wp_viewport_set_destination, 21L, 16L);
        scale_show(&w, 32, 24);
        WLCHECK(c->errors == 0 && fake.row[0].used && fake.row[0].picture_w == 32 && fake.row[0].picture_h == 24,
                "a buffer of 32 by 24 with a destination of 21 by 16 is shown as it is, pixel for pixel (%s)", c->error_text);
        pointer = wlc_id(c, WLI_wl_pointer, 9);
        wlc_send(c, w.s.seat, WLQ_wl_seat_get_pointer, pointer);
        wlc_forget(c);
        wayland_pointer_to(0, 30, 24);
        wlc_pump(c);
        e = wlc_take(c, WLI_wl_pointer, WLE_wl_pointer_enter);
        WLCHECK(e && e->u[2] == 20 * 256 && e->u[3] == 16 * 256, "30 by 24 pixels at 1.5 pixels a unit is 20 by 16 units to the client");

        /* a popup of that window: the positioner is in the window's units, so the place told is, and the compositor's is in pixels */
        {
                unsigned long positioner = wlc_id(c, WLI_xdg_positioner, 6), popup_surface = wlc_id(c, WLI_wl_surface, 6);
                unsigned long popup_xdg = wlc_id(c, WLI_xdg_surface, 6), popup = wlc_id(c, WLI_xdg_popup, 6);
                struct wl_event_seen *conf;
                unsigned pixels[40 * 30];
                long fd;
                unsigned long pool = wlc_id(c, WLI_wl_shm_pool, 1), popup_buffer = wlc_id(c, WLI_wl_buffer, 1);

                wlc_send(c, w.s.wm_base, WLQ_xdg_wm_base_create_positioner, positioner);
                wlc_send(c, positioner, WLQ_xdg_positioner_set_size, 40L, 30L);
                wlc_send(c, positioner, WLQ_xdg_positioner_set_anchor_rect, 4L, 5L, 10L, 8L);
                wlc_send(c, positioner, WLQ_xdg_positioner_set_anchor, (long)WLN_xdg_positioner_anchor_bottom_left);
                wlc_send(c, positioner, WLQ_xdg_positioner_set_gravity, (long)WLN_xdg_positioner_gravity_bottom_right);
                wlc_send(c, w.s.compositor, WLQ_wl_compositor_create_surface, popup_surface);
                wlc_send(c, w.s.wm_base, WLQ_xdg_wm_base_get_xdg_surface, popup_xdg, popup_surface);
                wlc_forget(c);
                wlc_send(c, popup_xdg, WLQ_xdg_surface_get_popup, popup, w.s.xdg, positioner);
                e = wlc_take(c, WLI_xdg_popup, WLE_xdg_popup_configure);
                WLCHECK(e && e->u[0] == 4 && e->u[1] == 13 && e->u[2] == 40 && e->u[3] == 30,
                        "a popup is told its place in the units of its parent: 4, 13, 40 by 30 as it asked");
                conf = wlc_take(c, WLI_xdg_surface, WLE_xdg_surface_configure);
                for (int at = 0; at < 40 * 30; at++)
                        pixels[at] = 0xffabcdef;
                fd = wl_file(sizeof pixels, 0);
                wl_file_put(fd, 0, pixels, sizeof pixels);
                wlc_send(c, w.s.shm, WLQ_wl_shm_create_pool, pool, fd, (long)sizeof pixels);
                close((int)fd);
                wlc_send(c, pool, WLQ_wl_shm_pool_create_buffer, popup_buffer, 0L, 40L, 30L, 160L, (long)WLN_wl_shm_format_xrgb8888);
                wlc_send(c, popup_xdg, WLQ_xdg_surface_ack_configure, conf ? (long)conf->u[0] : 0L);
                wlc_send(c, popup_surface, WLQ_wl_surface_attach, popup_buffer, 0L, 0L);
                wlc_send(c, popup_surface, WLQ_wl_surface_commit);
                WLCHECK(c->errors == 0 && fake.row[1].used && fake.row[1].x == 6 && fake.row[1].y == 20,
                        "and is put at 6, 20 pixels, which are 4, 13 units at 1.5 (%ld %ld; %s)", fake.row[1].x, fake.row[1].y, c->error_text);
                wlc_send(c, popup, WLQ_xdg_popup_destroy);
        }

        /* the screen goes to 200 percent: the window's surface is told its fractional scale (the whole one is still 2), then its size */
        fake.row[0].width_asked = 960;
        fake.row[0].height_asked = 540;
        fake.screen.scale = 200;
        wlc_forget(c);
        wayland_screens_changed();
        wayland_scales_sweep();
        wlc_pump(c);
        a = event_at(c, WLI_wp_fractional_scale_v1, WLE_wp_fractional_scale_v1_preferred_scale, 0);
        b = event_at(c, WLI_xdg_toplevel, WLE_xdg_toplevel_configure, 0);
        WLCHECK(a >= 0 && c->ev[a].u[0] == 240 && wlc_count(c, WLI_wl_surface, WLE_wl_surface_preferred_buffer_scale) == 0,
                "at 200 percent the fractional scale is 240 and the whole scale, still 2, is not said again");
        WLCHECK(b > a && c->ev[b].u[0] == 480 && c->ev[b].u[1] == 270,
                "after it the window is asked for the same 960 by 540 pixels in the new unit: 480 by 270");
        e = 0;
        wlc_forget(c);
        wayland_scales_sweep();
        wlc_pump(c);
        WLCHECK(c->events == 0, "a scale told is not told again (%ld events)", c->events);

        /* back at 100 percent: both scales change; the fractional one is exact for the presets below one too */
        fake.screen.scale = 100;
        wayland_screens_changed();
        wayland_scales_sweep();
        wlc_pump(c);
        e = wlc_take(c, WLI_wp_fractional_scale_v1, WLE_wp_fractional_scale_v1_preferred_scale);
        WLCHECK(e && e->u[0] == 120, "at 100 percent the fractional scale is 120");
        e = wlc_take(c, WLI_wl_surface, WLE_wl_surface_preferred_buffer_scale);
        WLCHECK(e && e->u[0] == 1, "and the whole scale is 1");
        fake.screen.scale = 75;
        wlc_forget(c);
        wayland_screens_changed();
        wayland_scales_sweep();
        wlc_pump(c);
        e = wlc_take(c, WLI_wp_fractional_scale_v1, WLE_wp_fractional_scale_v1_preferred_scale);
        WLCHECK(e && e->u[0] == 90 && wlc_count(c, WLI_wl_surface, WLE_wl_surface_preferred_buffer_scale) == 0,
                "at 75 percent it is 90, and the whole scale stays 1");
        /* a window that moved to another screen is told that screen's scale */
        fake.row[0].scale = 200;
        wlc_forget(c);
        wayland_scales_sweep();
        wlc_pump(c);
        e = wlc_take(c, WLI_wp_fractional_scale_v1, WLE_wp_fractional_scale_v1_preferred_scale);
        WLCHECK(e && e->u[0] == 240, "a window whose screen has another scale than the first is told its own (240)");
        wlc_end(c);

        /* a client that knows whole scales only: told 2 at 150 percent, its pixels are twice its units */
        fake.screen.scale = 150;
        fake.row[0].scale = 0;
        wayland_screens_changed();
        scale_open(&w, 0, 0);
        c = w.s.c;
        WLCHECK(w.whole == 2, "a client with no fractional scale is told the whole scale 2");
        wlc_send(c, w.s.surface, WLQ_wl_surface_set_buffer_scale, 2L);
        scale_show(&w, 64, 48);
        WLCHECK(c->errors == 0 && fake.row[0].picture_w == 64, "it draws 64 by 48 pixels for 32 by 24 units (%s)", c->error_text);
        pointer = wlc_id(c, WLI_wl_pointer, 9);
        wlc_send(c, w.s.seat, WLQ_wl_seat_get_pointer, pointer);
        wlc_forget(c);
        wayland_pointer_to(0, 30, 24);
        wlc_pump(c);
        e = wlc_take(c, WLI_wl_pointer, WLE_wl_pointer_enter);
        WLCHECK(e && e->u[2] == 15 * 256 && e->u[3] == 12 * 256, "and 30 by 24 pixels is 15 by 12 units to it");
        fake.row[0].width_asked = 960;
        fake.row[0].height_asked = 540;
        fake.screen.scale = 100;
        wlc_forget(c);
        wayland_screens_changed();
        wayland_scales_sweep();
        wlc_pump(c);
        e = wlc_take(c, WLI_wl_surface, WLE_wl_surface_preferred_buffer_scale);
        WLCHECK(e && e->u[0] == 1, "back at 100 percent it is told 1");
        e = wlc_take(c, WLI_xdg_toplevel, WLE_xdg_toplevel_configure);
        WLCHECK(e && e->u[0] == 960 && e->u[1] == 540, "and asked for 960 by 540 units, which are pixels again");
        wlc_end(c);
        leak_check("scale");
        wayland_stop();

        /* the viewport, judged as the protocol says */
        wl_reset();
        wayland_start(&fake_host);
        scale_open(&w, 0, 1);
        c = w.s.c;
        wlc_send(c, w.viewporter, WLQ_wp_viewporter_get_viewport, wlc_id(c, WLI_wp_viewport, 1), w.s.surface);
        WLCHECK(refused_with(c, WLN_wp_viewporter_error_viewport_exists), "a surface has one viewport");
        wlc_end(c);
        scale_open(&w, 1, 0);
        c = w.s.c;
        wlc_send(c, w.manager, WLQ_wp_fractional_scale_manager_v1_get_fractional_scale, wlc_id(c, WLI_wp_fractional_scale_v1, 1), w.s.surface);
        WLCHECK(refused_with(c, WLN_wp_fractional_scale_manager_v1_error_fractional_scale_exists), "a surface has one fractional scale");
        wlc_end(c);
        scale_open(&w, 0, 1);
        c = w.s.c;
        wlc_send(c, w.viewport, WLQ_wp_viewport_set_destination, 0L, 5L);
        WLCHECK(refused_with(c, WLN_wp_viewport_error_bad_value), "a destination of no width is refused");
        wlc_end(c);
        scale_open(&w, 0, 1);
        c = w.s.c;
        wlc_send(c, w.viewport, WLQ_wp_viewport_set_destination, -1L, 5L);
        WLCHECK(refused_with(c, WLN_wp_viewport_error_bad_value), "and so is one of -1 by 5");
        wlc_end(c);
        scale_open(&w, 0, 1);
        c = w.s.c;
        wlc_send(c, w.viewport, WLQ_wp_viewport_set_destination, -1L, -1L);
        wlc_send(c, w.viewport, WLQ_wp_viewport_set_source, -256L, -256L, -256L, -256L);
        WLCHECK(c->errors == 0, "a destination and a source of -1 are the unset ones");
        wlc_send(c, w.viewport, WLQ_wp_viewport_set_source, 0L, 0L, 0L, 5L * 256);
        WLCHECK(refused_with(c, WLN_wp_viewport_error_bad_value), "a source with no width is refused");
        wlc_end(c);
        scale_open(&w, 0, 1);
        c = w.s.c;
        wlc_send(c, w.viewport, WLQ_wp_viewport_set_source, 0L, 0L, 40L * 256, 10L * 256);
        scale_show(&w, 32, 24);
        WLCHECK(refused_with(c, WLN_wp_viewport_error_out_of_buffer), "a source outside the buffer is refused at the commit");
        wlc_end(c);
        scale_open(&w, 0, 1);
        c = w.s.c;
        wlc_send(c, w.viewport, WLQ_wp_viewport_set_source, 0L, 0L, 128L, 128L);
        scale_show(&w, 32, 24);
        WLCHECK(refused_with(c, WLN_wp_viewport_error_bad_size), "a source of half a unit and no destination is refused at the commit");
        wlc_end(c);
        scale_open(&w, 0, 1);
        c = w.s.c;
        wlc_send(c, w.viewport, WLQ_wp_viewport_set_destination, 21L, 16L);
        wlc_send(c, w.viewport, WLQ_wp_viewport_destroy);
        scale_show(&w, 33, 25);
        WLCHECK(c->errors == 0 && fake.row[0].picture_w == 33, "a viewport let go leaves the surface as it was: 33 by 25, no scale of 33 asked (%s)", c->error_text);
        wlc_end(c);
        scale_open(&w, 0, 1);
        c = w.s.c;
        wlc_send(c, w.viewport, WLQ_wp_viewport_set_destination, 11L, 7L);
        scale_show(&w, 33, 25);
        WLCHECK(c->errors == 0, "with a destination a buffer need not be a whole number of the scale (%s)", c->error_text);
        wlc_send(c, w.s.surface, WLQ_wl_surface_destroy);
        wlc_send(c, w.viewport, WLQ_wp_viewport_set_destination, 3L, 3L);
        WLCHECK(refused_with(c, WLN_wp_viewport_error_no_surface), "a viewport whose surface is gone is refused");
        wlc_end(c);
        leak_check("viewport");
        wayland_stop();
}

/*
        The screens that come and go. A screen that comes is a global a client can bind, announced to each registry it holds;
        one that goes is left by the surfaces on it, then removed from the registries, and what a client bound of it hears
        nothing more; a screen that comes in its place is another global (a name is never used twice), and the old name binds
        nothing.
*/
static long output_names(struct wlc *c, long from, long address[], long room)
{
        long count = 0;

        for (long at = from; at < c->events; at++)
                if (c->ev[at].iface == WLI_wl_registry && c->ev[at].opcode == WLE_wl_registry_global &&
                    !strcmp(c->ev[at].text[1], "wl_output") && count < room)
                        address[count++] = (long)c->ev[at].u[0];
        return count;
}

static long output_events(struct wlc *c, unsigned long object, long opcode)
{
        long count = 0;

        for (long at = 0; at < c->events; at++)
                if (c->ev[at].iface == WLI_wl_output && c->ev[at].object == object && c->ev[at].opcode == opcode)
                        count++;
        return count;
}

/*
        The screens in one logical space. A screen that follows another is placed after what the other measures in units, not in
        pixels, so two screens that touch in the desktop's pixels touch in the units the clients are told of, whatever their scales
        are: the xdg_output's logical position and size of one is the end of the other's.
*/
static void logical_of(struct wlc *c, unsigned long output, long address[4])
{
        for (long at = 0; at < c->events; at++)
                if (c->ev[at].iface == WLI_zxdg_output_v1 && c->ev[at].object == output)
                {
                        if (c->ev[at].opcode == WLE_zxdg_output_v1_logical_position)
                                address[0] = (long)c->ev[at].u[0], address[1] = (long)c->ev[at].u[1];
                        else if (c->ev[at].opcode == WLE_zxdg_output_v1_logical_size)
                                address[2] = (long)c->ev[at].u[0], address[3] = (long)c->ev[at].u[1];
                }
}

static void test_logical_space(void)
{
        struct session s;
        struct wlc *c;
        unsigned long manager, first, second, third;
        long a[4], b[4], d[4];

        wl_reset();
        wayland_start(&fake_host);
        fake.screen = (struct wayland_screen){0, 0, 3840, 2160, 200, 60000, 600, 340, "Virtual-1"};
        fake.screens = 2;
        fake.extra[0] = (struct wayland_screen){3840, 0, 1920, 1080, 100, 60000, 300, 170, "Virtual-2"};
        wayland_screens_changed();
        session_open(&s);
        c = s.c;
        manager = wl_global(c, s.registry, WLI_zxdg_output_manager_v1, 3);
        first = wlc_id(c, WLI_zxdg_output_v1, 3);
        second = wlc_id(c, WLI_zxdg_output_v1, 3);
        wlc_send(c, manager, WLQ_zxdg_output_manager_v1_get_xdg_output, first, s.output);
        {
                unsigned long output2 = wlc_id(c, WLI_wl_output, 4);

                wlc_send(c, s.registry, WLQ_wl_registry_bind, 65L, "wl_output", 4L, output2);
                wlc_send(c, manager, WLQ_zxdg_output_manager_v1_get_xdg_output, second, output2);
        }
        memset(a, 0, sizeof a);
        memset(b, 0, sizeof b);
        logical_of(c, first, a);
        logical_of(c, second, b);
        WLCHECK(a[0] == 0 && a[1] == 0 && a[2] == 1920 && a[3] == 1080,
                "a screen of 3840 by 2160 at 200 percent is 1920 by 1080 units at the origin");
        WLCHECK(b[0] == 1920 && b[1] == 0 && b[2] == 1920 && b[3] == 1080,
                "the screen that abuts it on the right, at 100 percent, starts where it ends in units, 1920, and not in pixels, 3840");
        /* the other way round: the 100 percent screen first, the 200 percent one after it */
        wlc_forget(c);
        fake.screen = (struct wayland_screen){0, 0, 1920, 1080, 100, 60000, 300, 170, "Virtual-1"};
        fake.extra[0] = (struct wayland_screen){1920, 0, 3840, 2160, 200, 60000, 600, 340, "Virtual-2"};
        wayland_screens_changed();
        wlc_pump(c);
        memset(a, 0, sizeof a);
        memset(b, 0, sizeof b);
        logical_of(c, first, a);
        logical_of(c, second, b);
        WLCHECK(a[0] == 0 && b[0] == 1920 && b[2] == 1920 && b[3] == 1080,
                "and the 200 percent one after the 100 percent one is at 1920 units too, 1920 units wide");
        /* a screen below, and a gap: the gap is in the screen's own units */
        wlc_forget(c);
        fake.screen = (struct wayland_screen){0, 0, 1920, 1080, 100, 60000, 300, 170, "Virtual-1"};
        fake.extra[0] = (struct wayland_screen){0, 1080 + 300, 2880, 1620, 150, 60000, 450, 255, "Virtual-2"};
        wayland_screens_changed();
        wlc_pump(c);
        memset(b, 0, sizeof b);
        logical_of(c, second, b);
        WLCHECK(b[0] == 0 && b[1] == 1080 + 200 && b[2] == 1920 && b[3] == 1080,
                "a screen below another with a gap of 300 pixels at 150 percent is that gap, 200 units, below its end");
        /* a screen that follows nothing is at its pixel distance in its own units; one that follows two is after the nearer */
        wlc_forget(c);
        fake.screens = 3;
        fake.screen = (struct wayland_screen){0, 0, 1920, 1080, 100, 60000, 300, 170, "Virtual-1"};
        fake.extra[0] = (struct wayland_screen){1920, 0, 3840, 2160, 200, 60000, 600, 340, "Virtual-2"};
        fake.extra[1] = (struct wayland_screen){5760, 0, 1920, 1080, 100, 60000, 300, 170, "Virtual-3"};
        wayland_screens_changed();
        third = wlc_id(c, WLI_zxdg_output_v1, 3);
        {
                unsigned long output3 = wlc_id(c, WLI_wl_output, 4);

                wlc_send(c, s.registry, WLQ_wl_registry_bind, 66L, "wl_output", 4L, output3);
                wlc_send(c, manager, WLQ_zxdg_output_manager_v1_get_xdg_output, third, output3);
        }
        memset(d, 0, sizeof d);
        logical_of(c, third, d);
        WLCHECK(d[0] == 1920 + 1920 && d[1] == 0,
                "a third screen after the 200 percent one is after its 1920 units, at 3840, and not at 5760 pixels");
        wlc_end(c);
        leak_check("logical");
        wayland_stop();
}

static void test_outputs(void)
{
        struct session s;
        struct scale_window w;
        struct wlc *c;
        struct wl_event_seen *e;
        long names[4], count, a;
        unsigned long second, third;

        wl_reset();
        wayland_start(&fake_host);
        session_open(&s);
        c = s.c;
        count = output_names(c, 0, names, 4);
        WLCHECK(count == 1 && names[0] == 64, "one screen is one wl_output global, named 64");
        wlc_forget(c);
        fake.screens = 2;
        wayland_screens_changed();
        wlc_pump(c);
        count = output_names(c, 0, names, 4);
        WLCHECK(count == 1 && names[0] == 65 && wlc_count(c, WLI_wl_registry, WLE_wl_registry_global_remove) == 0,
                "a screen that comes is announced to a registry that is open: a new global, 65, and nothing removed");
        second = wlc_id(c, WLI_wl_output, 4);
        wlc_forget(c);
        wlc_send(c, s.registry, WLQ_wl_registry_bind, 65L, "wl_output", 4L, second);
        a = output_events(c, second, WLE_wl_output_geometry);
        e = wlc_take(c, WLI_wl_output, WLE_wl_output_mode);
        WLCHECK(c->errors == 0 && a == 1 && e && e->u[1] == 1280,
                "and it binds, and says itself: its geometry and a mode of 1280 by 720 (%s)", c->error_text);
        /* a window on the second screen leaves it when it goes, and then the global is removed */
        wlc_end(c);
        wayland_stop();
        wl_reset();
        wayland_start(&fake_host);
        fake.screens = 2;
        wayland_screens_changed();
        scale_open(&w, 0, 0);
        c = w.s.c;
        second = wlc_id(c, WLI_wl_output, 4);
        wlc_send(c, w.s.registry, WLQ_wl_registry_bind, 65L, "wl_output", 4L, second);
        scale_show(&w, 32, 24);
        fake.row[0].x = 1300;
        wayland_scales_sweep();
        wlc_pump(c);
        WLCHECK(wlc_count(c, WLI_wl_surface, WLE_wl_surface_enter) >= 1, "a window on the second screen enters it");
        wlc_forget(c);
        fake.screens = 1;
        wayland_screens_changed();
        wlc_pump(c);
        a = event_at(c, WLI_wl_surface, WLE_wl_surface_leave, 0);
        count = event_at(c, WLI_wl_registry, WLE_wl_registry_global_remove, 0);
        WLCHECK(a >= 0 && c->ev[a].u[0] == second && count > a && c->ev[count].u[0] == 65,
                "a screen that goes is left by the window on it, and then its global, 65, is removed");
        wlc_forget(c);
        fake.screen.scale = 150;
        wayland_screens_changed();
        wlc_pump(c);
        WLCHECK(output_events(c, second, WLE_wl_output_geometry) == 0 && output_events(c, w.s.output, WLE_wl_output_geometry) == 1,
                "what was bound of the screen that went hears nothing more, and the one that stays is told as before");
        wlc_forget(c);
        fake.screen.scale = 100;
        fake.screens = 2;
        wayland_screens_changed();
        wlc_pump(c);
        count = output_names(c, 0, names, 4);
        WLCHECK(count == 1 && names[0] == 65 + WAYLAND_SCREENS, "a screen that comes in its place is another global: 65 and a turn of the screens, and not the 65 that was removed");
        wlc_forget(c);
        fake.screen.scale = 125;
        wayland_screens_changed();
        wlc_pump(c);
        WLCHECK(output_events(c, second, WLE_wl_output_geometry) == 0 && output_events(c, w.s.output, WLE_wl_output_geometry) == 1,
                "and what was bound of the screen that went is not told of the one that came in its place");
        wlc_forget(c);
        wlc_send(c, w.s.registry, WLQ_wl_registry_bind, 65L, "wl_output", 4L, wlc_id(c, WLI_wl_output, 4));
        WLCHECK(refused_with(c, WLN_wl_display_error_invalid_object), "and the name that was removed binds nothing");
        wlc_end(c);
        /* twenty screens (past the eight there were): a client is offered twenty outputs */
        wayland_stop();
        wl_reset();
        wayland_start(&fake_host);
        fake.screens = 20;
        wayland_screens_changed();
        session_open(&s);
        c = s.c;
        {
                long many[32];

                count = output_names(c, 0, many, 32);
                WLCHECK(count == 20 && many[19] == 64 + 19, "a client is offered twenty outputs for twenty screens (%ld)", count);
        }
        wlc_end(c);
        /* a client made after sees the screens there are, and two that go and come again are two new globals */
        wayland_stop();
        wl_reset();
        wayland_start(&fake_host);
        fake.screens = 3;
        wayland_screens_changed();
        session_open(&s);
        c = s.c;
        count = output_names(c, 0, names, 4);
        WLCHECK(count == 3 && names[0] == 64 && names[1] == 65 && names[2] == 66, "a client made with three screens is offered three, 64 to 66");
        wlc_forget(c);
        fake.screens = 1;
        wayland_screens_changed();
        wlc_pump(c);
        WLCHECK(wlc_count(c, WLI_wl_registry, WLE_wl_registry_global_remove) == 2,
                "two screens gone are two globals removed");
        wlc_forget(c);
        fake.screens = 3;
        wayland_screens_changed();
        wlc_pump(c);
        count = output_names(c, 0, names, 4);
        WLCHECK(count == 2 && names[0] == 65 + WAYLAND_SCREENS && names[1] == 66 + WAYLAND_SCREENS, "and two that come are 65 and 66, a turn of the screens on");
        third = wlc_id(c, WLI_wl_output, 4);
        (void)third;
        wlc_end(c);
        leak_check("outputs");
        wayland_stop();
}

/*
        Damage in a surface's units at a fractional scale: one unit at 150 percent is the buffer's pixels 1.5 to 3.0, which are
        pixels 1 and 2, so a damage of (1, 1, 1, 1) must copy pixel 1 as well as pixel 2. Each edge is rounded to the nearest
        pixel, so the copy used to start at 2 and miss the pixel the damage touches.
*/
static void test_fractional_damage(void)
{
        struct scale_window w;
        struct wlc *c;
        unsigned long buffer;

        wl_reset();
        wayland_start(&fake_host);
        fake.screen.scale = 150;
        wayland_screens_changed();
        scale_open(&w, 1, 1);
        c = w.s.c;
        wlc_send(c, w.viewport, WLQ_wp_viewport_set_destination, 21L, 16L);
        scale_show(&w, 32, 24);
        buffer = wl_buffer_of(&w.s, 32, 24, 0xffcc4422, WLN_wl_shm_format_xrgb8888);
        wlc_send(c, w.s.surface, WLQ_wl_surface_attach, buffer, 0L, 0L);
        wlc_send(c, w.s.surface, WLQ_wl_surface_damage, 1L, 1L, 1L, 1L);
        wlc_send(c, w.s.surface, WLQ_wl_surface_commit);
        WLCHECK(c->errors == 0, "a damage of one unit at 150 percent is no error (%s)", c->error_text);
        WLCHECK(fake.row[0].pixels[1 * FAKE_W + 1] == 0xffcc4422,
                "the pixel a damaged unit covers in part is copied (pixel 1 is %08x)", fake.row[0].pixels[1 * FAKE_W + 1]);
        WLCHECK(fake.row[0].pixels[2 * FAKE_W + 2] == 0xffcc4422, "and the pixel it covers whole is copied (%08x)", fake.row[0].pixels[2 * FAKE_W + 2]);
        WLCHECK(fake.row[0].pixels[0] == 0xff336699, "and nothing outside the damage is (%08x)", fake.row[0].pixels[0]);
        wlc_end(c);
        leak_check("fractional damage");
        wayland_stop();
}

/*
        The client counts its live subsurfaces (the child scans are passed over at none): a subsurface counts from the
        request that gives it its role, and stops counting when its surface goes, whatever its object did before.
*/
static void test_subsurface_count(void)
{
        struct tree t;
        struct wlc *c;
        struct wayland_client *shim;

        wl_reset();
        wayland_start(&fake_host);
        tree_open(&t, 1);
        c = t.s.c;
        shim = wayland.client[c->slot];
        WLCHECK(shim->subsurfaces == 1, "a subsurface is counted (%ld)", shim->subsurfaces);
        wlc_send(c, t.sub, WLQ_wl_subsurface_destroy);
        WLCHECK(shim->subsurfaces == 1, "and still while its object is gone, its surface having its role (%ld)", shim->subsurfaces);
        wlc_send(c, t.child, WLQ_wl_surface_destroy);
        WLCHECK(shim->subsurfaces == 0, "and not once its surface is gone (%ld)", shim->subsurfaces);
        wlc_end(c);
        leak_check("subsurface count");
        wayland_stop();
}

/*
        A window's commit whose damage is all outside its buffer: no rectangle of it is left, and the picture is taken whole, as a commit
        with no damage is. The subsurface over the window is laid on it again; it was not, where what was remembered as copied was a
        rectangle read from the stack, and the subsurface was gone until the window's next whole commit.
*/
static void test_damage_outside(void)
{
        struct tree t;
        struct wlc *c;

        wl_reset();
        wayland_start(&fake_host);
        for (int round = 0; round < 4; round++)
        {
                unsigned long over;

                tree_open(&t, 1);
                c = t.s.c;
                over = wl_buffer_of(&t.s, 32, 24, 0xff808080, WLN_wl_shm_format_xrgb8888);
                wlc_send(c, t.s.surface, WLQ_wl_surface_attach, over, 0L, 0L);
                if (round & 1)
                        wlc_send(c, t.s.surface, WLQ_wl_surface_damage, 5000L + round, 7000L, 16L, 16L);
                else
                        wlc_send(c, t.s.surface, WLQ_wl_surface_damage_buffer, -900L, -900L, 10L + round, 10L);
                wlc_send(c, t.s.surface, WLQ_wl_surface_commit);
                WLCHECK(c->errors == 0 && fake.row[0].pixels[5 * FAKE_W + 5] == 0xff00ff00 && fake.row[0].pixels[11 * FAKE_W + 11] == 0xff00ff00 &&
                            fake.row[0].pixels[4 * FAKE_W + 4] == 0xff00ff00,
                        "damage all outside the buffer: the subsurface is still laid over the window (round %d: %08x %08x)", round,
                        fake.row[0].pixels[5 * FAKE_W + 5], fake.row[0].pixels[11 * FAKE_W + 11]);
                WLCHECK(fake.row[0].pixels[20 * FAKE_W + 20] == 0xff808080 && fake.row[0].pixels[0] == 0xff808080,
                        "and the window's picture is the buffer it attached, whole (%08x)", fake.row[0].pixels[20 * FAKE_W + 20]);
                WLCHECK(wayland.client[c->slot]->surfaces[0].shown_full == 1, "and what was copied is remembered as the whole of it");
                wlc_end(c);
        }
        leak_check("damage outside");
        wayland_stop();
}

/*
        A host that reads a window's whole picture where the client keeps it (the compositor's compositor_wl_lend): which commits are whole,
        when the buffer a lent picture replaced is the client's again, and what a lent picture costs the client's budget.
*/
static void test_lent(void)
{
        struct session s;
        struct wlc *c;
        unsigned long second;
        unsigned char run[20 * 52];
        long at = 0, pictures;

        wl_reset();
        fake.lend = 1;
        fake.screen.width = 64;
        fake.screen.height = 48;
        wayland_start(&fake_host);
        session_open(&s);
        session_window(&s, 0xff101010);
        c = s.c;
        second = wl_buffer_of(&s, 32, 24, 0xff202020, WLN_wl_shm_format_xrgb8888);
        WLCHECK(fake.lends == 1 && releases_of(c, s.buffer) == 0, "a window's first picture is read where it is, and its buffer is kept (%ld lent, %ld released)",
                fake.lends, releases_of(c, s.buffer));
        // damage that is the whole buffer, in buffer and in surface coordinates, and past its edges: whole
        wlc_send(c, s.surface, WLQ_wl_surface_attach, second, 0L, 0L);
        wlc_send(c, s.surface, WLQ_wl_surface_damage_buffer, 0L, 0L, 32L, 24L);
        wlc_send(c, s.surface, WLQ_wl_surface_commit);
        WLCHECK(fake.lends == 2, "a commit whose damage is the whole buffer is a whole picture, read where it is and not copied (%ld)", fake.lends);
        WLCHECK(releases_of(c, s.buffer) == 0 && releases_of(c, second) == 0, "the buffer it replaced is not released before a frame shows the new one");
        wayland_presented_idle(1000000000ul, 16666667ul, 1, 0ul);
        wlc_pump(c);
        WLCHECK(releases_of(c, s.buffer) == 0, "nor by a frame's time in which nothing was drawn");
        // The frame drawn releases it, before the frame's callbacks and before its flip lands: a client with two buffers has one to draw in.
        wayland_drawn();
        wlc_pump(c);
        WLCHECK(releases_of(c, s.buffer) == 1 && releases_of(c, second) == 0, "the frame drawn releases it, before its flip has landed (%ld)",
                releases_of(c, s.buffer));
        wayland_presented(1000000000ul, 16666667ul, 1, 0ul, -1L);
        wlc_pump(c);
        WLCHECK(releases_of(c, s.buffer) == 1 && releases_of(c, second) == 0, "once (%ld)", releases_of(c, s.buffer));
        wlc_send(c, s.surface, WLQ_wl_surface_attach, s.buffer, 0L, 0L);
        wlc_send(c, s.surface, WLQ_wl_surface_damage, -5L, -5L, 0x7fffffffL, 0x7fffffffL);
        wlc_send(c, s.surface, WLQ_wl_surface_commit);
        WLCHECK(fake.lends == 3, "damage of everything, as toolkits say it (a rectangle past every edge), is whole too (%ld)", fake.lends);
        wayland_presented(2000000000ul, 16666667ul, 2, 0ul, -1L);
        wlc_pump(c);
        // damage of a part is not whole: copied, and the buffer released by the commit
        wlc_forget(c);
        wlc_send(c, s.surface, WLQ_wl_surface_attach, second, 0L, 0L);
        wlc_send(c, s.surface, WLQ_wl_surface_damage_buffer, 0L, 0L, 32L, 23L);
        wlc_send(c, s.surface, WLQ_wl_surface_commit);
        // A buffer is the surface's whole picture whatever part of it is damaged: read in place too, the damage handed over for the part to draw
        // again, the buffer kept as the picture and the one it replaced released by the frame drawn. It was copied, and after a whole frame
        // the whole window was copied first.
        WLCHECK(fake.lends == 4 && fake.lent_parts == 1 && releases_of(c, second) == 0 && releases_of(c, s.buffer) == 0,
                "a commit with a part of its buffer damaged is read in place, and its buffer is kept (%ld lent, %ld of a part, %ld released)", fake.lends,
                fake.lent_parts, releases_of(c, second));
        WLCHECK(fake.lent_box.x == 0 && fake.lent_box.y == 0 && fake.lent_box.width == 32 && fake.lent_box.height == 23,
                "with the rectangle that changed, for that part alone to be drawn again (%ld %ld %ld %ld)", fake.lent_box.x, fake.lent_box.y,
                fake.lent_box.width, fake.lent_box.height);
        fake.copied = 0;
        wayland_drawn();
        wlc_pump(c);
        WLCHECK(releases_of(c, s.buffer) == 1, "and the buffer it replaced is released by the frame drawn (%ld)", releases_of(c, s.buffer));
        wlc_forget(c);
        wlc_send(c, s.surface, WLQ_wl_surface_attach, s.buffer, 0L, 0L);
        wlc_send(c, s.surface, WLQ_wl_surface_damage_buffer, 3L, 4L, 5L, 6L);
        wlc_send(c, s.surface, WLQ_wl_surface_commit);
        wayland_drawn();
        wlc_pump(c);
        WLCHECK(fake.lends == 5 && fake.lent_parts == 2 && fake.lent_box.x == 3 && fake.lent_box.y == 4 && fake.lent_box.width == 5 && fake.lent_box.height == 6 &&
                    releases_of(c, second) == 1,
                "a small part after it, on the other buffer, is read in place as well, with no copy of the window between (%ld %ld)", fake.lends, fake.lent_parts);
        // back to `second` as the picture, for the rows below
        wlc_send(c, s.surface, WLQ_wl_surface_attach, second, 0L, 0L);
        wlc_send(c, s.surface, WLQ_wl_surface_commit);
        wayland_presented(3000000000ul, 16666667ul, 3, 0ul, -1L);
        wlc_pump(c);
        fake.lends = 3;

        // a window put away is in no frame: the buffer its picture replaced comes back at the next frame's time, drawn or not
        wlc_send(c, s.surface, WLQ_wl_surface_attach, s.buffer, 0L, 0L);
        wlc_send(c, s.surface, WLQ_wl_surface_commit);
        wayland_presented(4000000000ul, 16666667ul, 4, 0ul, -1L);
        wlc_pump(c);
        fake.row[0].states |= WAYLAND_STATE_SUSPENDED;
        wlc_forget(c);
        wlc_send(c, s.surface, WLQ_wl_surface_attach, second, 0L, 0L);
        wlc_send(c, s.surface, WLQ_wl_surface_commit);
        WLCHECK(fake.lends == 5 && releases_of(c, s.buffer) == 0, "a window put away that draws: its new picture is lent, the one before is held (%ld)", fake.lends);
        WLCHECK(wayland_frame_wanted(), "and a frame's time is asked for, to let it go in");
        wayland_presented_idle(5000000000ul, 16666667ul, 5, 0ul);
        wlc_pump(c);
        WLCHECK(releases_of(c, s.buffer) == 1, "which releases it though the window is in no frame: a client with two buffers has one again (%ld)",
                releases_of(c, s.buffer));
        WLCHECK(!wayland_frame_wanted(), "and no more frames are asked for the window put away");
        fake.row[0].states &= ~(long)WAYLAND_STATE_SUSPENDED;

        // a dma-buf may be the buffer the card shows until the flip lands: the one a lent picture replaced waits for the frame presented
        {
                unsigned long one = dmabuf_buffer(&s, 32, 24, 0xff000000u, DRM_XRGB8888), two = dmabuf_buffer(&s, 32, 24, 0xff400000u, DRM_XRGB8888);

                dmabuf_commit(&s, one, 32, 24);
                wayland_drawn();
                wayland_presented(6000000000ul, 16666667ul, 6, 0ul, -1L);
                wlc_forget(c);
                dmabuf_commit(&s, two, 32, 24);
                wayland_drawn();
                wlc_pump(c);
                WLCHECK(c->errors == 0 && fake.lends >= 7 && releases_of(c, one) == 0, "a dma-buf a lent picture replaced is not released by the frame drawn (%ld, %s)",
                        releases_of(c, one), c->error_text);
                wayland_presented(7000000000ul, 16666667ul, 7, 0ul, -1L);
                wlc_pump(c);
                WLCHECK(releases_of(c, one) == 1, "the frame presented releases it (%ld)", releases_of(c, one));
                dmabuf_commit(&s, s.buffer, 32, 24);
                wayland_drawn();
                wayland_presented(8000000000ul, 16666667ul, 8, 0ul, -1L);
                wlc_pump(c);
        }

        // a lent picture is not copied and costs the budget nothing: twenty whole commits in one read are twenty pictures
        fake.clock_still = 1;
        fake.now += WAYLAND_BUDGET_MS;
        for (int k = 0; k < 20; k++)
        {
                wlc_put(run + at, s.surface);
                wlc_put(run + at + 4, WLQ_wl_surface_attach | 20UL << 16);
                wlc_put(run + at + 8, k & 1 ? second : s.buffer);
                wlc_put(run + at + 12, 0);
                wlc_put(run + at + 16, 0);
                wlc_put(run + at + 20, s.surface);
                wlc_put(run + at + 24, WLQ_wl_surface_damage_buffer | 24UL << 16);
                wlc_put(run + at + 28, 0);
                wlc_put(run + at + 32, 0);
                wlc_put(run + at + 36, 32);
                wlc_put(run + at + 40, 24);
                wlc_put(run + at + 44, s.surface);
                wlc_put(run + at + 48, WLQ_wl_surface_commit | 8UL << 16);
                at += 52;
        }
        pictures = fake.row[0].pictures;
        {
                // A steady stream of commits asks the host for nothing: no block, no mapping, no descriptor, and no pixel is copied.
                long allocs = fake.allocs, maps = fake.maps, unmaps = fake.unmaps, closed = fake.fds_closed, releases = fake.releases;
                struct wayland_stats before, after;

                wayland_stats_get(&before);
                wayland_feed(c->slot, run, at, 0, 0);
                wlc_pump(c);
                wayland_drawn();
                wayland_frame();
                wayland_presented(9000000000ul, 16666667ul, 9, 0ul, -1L);
                wlc_pump(c);
                wayland_stats_get(&after);
                WLCHECK(fake.allocs == allocs && fake.maps == maps && fake.unmaps == unmaps && fake.fds_closed == closed && fake.releases == releases &&
                            before.objects == after.objects && before.pools == after.pools && before.buffers == after.buffers && before.fds == after.fds,
                        "twenty commits and the frame that shows them allocate nothing: no block, mapping or descriptor is asked of the host, and no object is left (%ld %ld %ld)",
                        fake.allocs - allocs, fake.maps - maps, after.objects - before.objects);
        }
        WLCHECK(fake.row[0].pictures - pictures == 20 && !wayland_stalled(c->slot) && c->errors == 0,
                "twenty whole pictures read in place in one read are all made: none was copied, and the budget is of copies (%ld, stalled %ld)",
                fake.row[0].pictures - pictures, wayland_stalled(c->slot));
        fake.clock_still = 0;
        wlc_end(c);

        // An X window's buffer is not taken at the protocol's word: a part damaged is copied, all of it damaged is read in place.
        {
                struct session x;
                unsigned long surface, xwin, first, next;
                long lends;

                session_open(&x);
                c = x.c;
                surface = wlc_id(c, WLI_wl_surface, 6);
                xwin = wlc_id(c, WLI_xwayland_surface_v1, 1);
                wlc_send(c, x.compositor, WLQ_wl_compositor_create_surface, surface);
                wlc_send(c, x.xshell, WLQ_xwayland_shell_v1_get_xwayland_surface, xwin, surface);
                wlc_send(c, xwin, WLQ_xwayland_surface_v1_set_serial, 0x4242UL, 0UL);
                wayland_xwindow_adopt(c->slot, 0x4242, 0, 0, 0, "x");
                first = wl_buffer_of(&x, 32, 24, 0xff111111, WLN_wl_shm_format_xrgb8888);
                next = wl_buffer_of(&x, 32, 24, 0xff222222, WLN_wl_shm_format_xrgb8888);
                wlc_send(c, surface, WLQ_wl_surface_attach, first, 0L, 0L);
                wlc_send(c, surface, WLQ_wl_surface_commit);
                lends = fake.lends;
                fake.lent_parts = 0;
                wlc_forget(c);
                wlc_send(c, surface, WLQ_wl_surface_attach, next, 0L, 0L);
                wlc_send(c, surface, WLQ_wl_surface_damage_buffer, 2L, 2L, 8L, 8L);
                wlc_send(c, surface, WLQ_wl_surface_commit);
                WLCHECK(c->errors == 0 && fake.row[0].used && fake.lends == lends && fake.lent_parts == 0 && releases_of(c, next) == 1 &&
                            fake.row[0].pixels[3 * FAKE_W + 3] == 0xff222222 && fake.row[0].pixels[0] == 0xff111111,
                        "an X window's commit with a part damaged is copied, that part alone, and its buffer released (%ld lent, %s)", fake.lends - lends,
                        c->error_text);
                wlc_send(c, surface, WLQ_wl_surface_attach, first, 0L, 0L);
                wlc_send(c, surface, WLQ_wl_surface_damage_buffer, 0L, 0L, 32L, 24L);
                wlc_send(c, surface, WLQ_wl_surface_commit);
                WLCHECK(c->errors == 0 && fake.lends == lends + 1, "and one with all of it damaged is read in place (%ld)", fake.lends - lends);
                wlc_end(c);
        }
        leak_check("lent");
        wayland_stop();
}

/*
        A toplevel is told the states its version has: tiled from version 2, suspended from 6. A client that bound xdg_wm_base below that
        is not sent the numbers. And a buffer destroyed between its attach and the commit is an attach unmade, not the client's end.
*/
static void test_versions(void)
{
        struct session s;
        struct wlc *c;
        struct wl_event_seen *e;
        unsigned long old_base, surface, xdg, toplevel, buffer, next;

        wl_reset();
        wayland_start(&fake_host);
        session_open(&s);
        c = s.c;
        old_base = wl_global(c, s.registry, WLI_xdg_wm_base, 1);
        buffer = wl_buffer_of(&s, 32, 24, 0xff334455, WLN_wl_shm_format_xrgb8888);
        surface = wlc_id(c, WLI_wl_surface, 6);
        wlc_send(c, s.compositor, WLQ_wl_compositor_create_surface, surface);
        xdg = wlc_id(c, WLI_xdg_surface, 1);
        wlc_send(c, old_base, WLQ_xdg_wm_base_get_xdg_surface, xdg, surface);
        toplevel = wlc_id(c, WLI_xdg_toplevel, 1);
        wlc_send(c, xdg, WLQ_xdg_surface_get_toplevel, toplevel);
        wlc_send(c, surface, WLQ_wl_surface_commit);
        e = wlc_take(c, WLI_xdg_surface, WLE_xdg_surface_configure);
        wlc_send(c, xdg, WLQ_xdg_surface_ack_configure, e ? (long)e->u[0] : 0L);
        wlc_send(c, surface, WLQ_wl_surface_attach, buffer, 0L, 0L);
        wlc_send(c, surface, WLQ_wl_surface_commit);
        wlc_forget(c);
        fake.row[0].states = WAYLAND_STATE_MAXIMIZED | WAYLAND_STATE_ACTIVATED | WAYLAND_STATE_SUSPENDED | WAYLAND_STATE_TILED_LEFT | WAYLAND_STATE_TILED_TOP;
        fake.row[0].width_asked = 640;
        fake.row[0].height_asked = 480;
        wayland_row_wake(0);
        wlc_pump(c);
        e = wlc_take(c, WLI_xdg_toplevel, WLE_xdg_toplevel_configure);
        WLCHECK(e && e->u[2] == 8 && c->malformed == 0, "a toplevel of version 1 is told maximized and activated, and neither tiled nor suspended (%lu bytes of states)",
                e ? e->u[2] : 0);
        e = wlc_take(c, WLI_xdg_surface, WLE_xdg_surface_configure);
        wlc_send(c, xdg, WLQ_xdg_surface_ack_configure, e ? (long)e->u[0] : 0L);
        wlc_end(c);

        session_open(&s);
        session_window(&s, 0xff000000);
        c = s.c;
        wlc_forget(c);
        fake.row[0].states = WAYLAND_STATE_MAXIMIZED | WAYLAND_STATE_ACTIVATED | WAYLAND_STATE_SUSPENDED | WAYLAND_STATE_TILED_LEFT | WAYLAND_STATE_TILED_TOP;
        wayland_row_wake(0);
        wlc_pump(c);
        e = wlc_take(c, WLI_xdg_toplevel, WLE_xdg_toplevel_configure);
        WLCHECK(e && e->u[2] == 20, "a toplevel of version 6 is told all five (%lu bytes of states)", e ? e->u[2] : 0);
        fake.row[0].states = 0;

        // a buffer destroyed after its attach and before the commit
        buffer = wl_buffer_of(&s, 32, 24, 0xffabcdef, WLN_wl_shm_format_xrgb8888);
        wlc_send(c, s.surface, WLQ_wl_surface_attach, buffer, 0L, 0L);
        wlc_send(c, buffer, WLQ_wl_buffer_destroy);
        wlc_send(c, s.surface, WLQ_wl_surface_commit);
        WLCHECK(c->errors == 0 && fake.row[0].used && fake.row[0].pixels[0] == 0xff000000,
                "a buffer destroyed between its attach and the commit is an attach unmade: the window shows what it showed, and the client lives (%s)",
                c->error_text);
        // and the id, made a buffer again, is not attached for having been named before
        next = wl_buffer_of(&s, 32, 24, 0xff00ff00, WLN_wl_shm_format_xrgb8888);
        wlc_send(c, s.surface, WLQ_wl_surface_attach, next, 0L, 0L);
        wlc_send(c, next, WLQ_wl_buffer_destroy);
        c->next = next; // the client's next buffer has the id the destroyed one had, as libwayland gives ids out again
        c->next -= 1;   // (wl_buffer_of makes its pool first)
        next = wl_buffer_of(&s, 32, 24, 0xffff0000, WLN_wl_shm_format_xrgb8888);
        wlc_send(c, s.surface, WLQ_wl_surface_commit);
        WLCHECK(c->errors == 0 && fake.row[0].pixels[0] == 0xff000000,
                "a buffer made with the id of one destroyed while attached is not shown unasked (%08x, %s)", fake.row[0].pixels[0], c->error_text);
        wlc_end(c);
        leak_check("versions");
        wayland_stop();
}

/*
        The budget of copies is a window's, not its client's: one client's two windows each have their own, so a busy one does not
        use up what the other may draw. And a window resized by hand is told a size a frame, the last one the hand came to.
*/
static void test_window_scope(void)
{
        struct session s;
        struct wlc *c;
        unsigned long surface, xdg, toplevel, buffer;
        struct wl_event_seen *e;
        unsigned char run[16 * 52];
        long at = 0, first, second;

        _Static_assert(WAYLAND_BUDGET_SCREENS <= 8, "the run below holds eight commits of each window");
        wl_reset();
        fake.screen.width = 32;
        fake.screen.height = 24;
        wayland_start(&fake_host);
        session_open(&s);
        session_window(&s, 0xff123456); // 32 by 24 on a screen of 32 by 24: a budget of WAYLAND_BUDGET_SCREENS pictures (e933763)
        c = s.c;
        buffer = wl_buffer_of(&s, 32, 24, 0xff654321, WLN_wl_shm_format_xrgb8888);
        surface = wlc_id(c, WLI_wl_surface, 6);
        wlc_send(c, s.compositor, WLQ_wl_compositor_create_surface, surface);
        xdg = wlc_id(c, WLI_xdg_surface, 6);
        wlc_send(c, s.wm_base, WLQ_xdg_wm_base_get_xdg_surface, xdg, surface);
        toplevel = wlc_id(c, WLI_xdg_toplevel, 6);
        wlc_send(c, xdg, WLQ_xdg_surface_get_toplevel, toplevel);
        wlc_send(c, surface, WLQ_wl_surface_commit);
        e = wlc_take(c, WLI_xdg_surface, WLE_xdg_surface_configure);
        wlc_send(c, xdg, WLQ_xdg_surface_ack_configure, e ? (long)e->u[0] : 0L);
        wlc_send(c, surface, WLQ_wl_surface_attach, buffer, 0L, 0L);
        wlc_send(c, surface, WLQ_wl_surface_commit);
        WLCHECK(c->errors == 0 && fake.row[0].used && fake.row[1].used, "one client has two windows (%s)", c->error_text);
        fake.clock_still = 1;
        fake.now += WAYLAND_BUDGET_MS;
        // The budget's copies of each window in one read, in turn: twice as many pictures, each window's own.
        for (int k = 0; k < 2 * WAYLAND_BUDGET_SCREENS; k++)
        {
                wlc_put(run + at, k & 1 ? surface : s.surface);
                wlc_put(run + at + 4, WLQ_wl_surface_attach | 20UL << 16);
                wlc_put(run + at + 8, k & 1 ? buffer : s.buffer);
                wlc_put(run + at + 12, 0);
                wlc_put(run + at + 16, 0);
                wlc_put(run + at + 20, k & 1 ? surface : s.surface);
                wlc_put(run + at + 24, WLQ_wl_surface_damage_buffer | 24UL << 16);
                wlc_put(run + at + 28, 0);
                wlc_put(run + at + 32, 0);
                wlc_put(run + at + 36, 32);
                wlc_put(run + at + 40, 24);
                wlc_put(run + at + 44, k & 1 ? surface : s.surface);
                wlc_put(run + at + 48, WLQ_wl_surface_commit | 8UL << 16);
                at += 52;
        }
        first = fake.row[0].pictures;
        second = fake.row[1].pictures;
        wayland_feed(c->slot, run, at, 0, 0);
        wlc_pump(c);
        WLCHECK(fake.row[0].pictures - first == WAYLAND_BUDGET_SCREENS && fake.row[1].pictures - second == WAYLAND_BUDGET_SCREENS &&
                    !wayland_stalled(c->slot) && c->errors == 0,
                "a budget's copies of each of a client's two windows in one read are all made: the budget is a window's (%ld and %ld, stalled %ld)",
                fake.row[0].pictures - first, fake.row[1].pictures - second, wayland_stalled(c->slot));
        // The ninth of one window waits, and is made in that window's next window of time.
        wayland_feed(c->slot, run, 52, 0, 0);
        wlc_pump(c);
        WLCHECK(fake.row[0].pictures - first == WAYLAND_BUDGET_SCREENS && wayland_stalled(c->slot), "the one past the budget of one of them waits (%ld)",
                fake.row[0].pictures - first);
        fake.now += WAYLAND_BUDGET_MS;
        wayland_resume(c->slot);
        wlc_pump(c);
        WLCHECK(fake.row[0].pictures - first == WAYLAND_BUDGET_SCREENS + 1 && !wayland_stalled(c->slot) && c->errors == 0, "and is made in its next window of time (%ld)",
                fake.row[0].pictures - first);
        fake.clock_still = 0;

        // A hand's resize: twenty sizes in one frame are one configure, and the frame's end says the last.
        wlc_forget(c);
        fake.row[0].states = WAYLAND_STATE_RESIZING | WAYLAND_STATE_ACTIVATED;
        fake.row[0].width_asked = 100;
        fake.row[0].height_asked = 80;
        wayland_row_wake(0);
        wlc_pump(c);
        e = wlc_take(c, WLI_xdg_toplevel, WLE_xdg_toplevel_configure);
        WLCHECK(e && e->u[0] == 100 && wlc_count(c, WLI_xdg_surface, WLE_xdg_surface_configure) == 1, "a resize begun is said at once, with its size");
        e = wlc_take(c, WLI_xdg_surface, WLE_xdg_surface_configure);
        wlc_send(c, s.xdg, WLQ_xdg_surface_ack_configure, e ? (long)e->u[0] : 0L);
        wlc_forget(c);
        for (int k = 1; k <= 20; k++)
        {
                fake.row[0].width_asked = 100 + k;
                wayland_row_wake(0);
        }
        wlc_pump(c);
        WLCHECK(wlc_count(c, WLI_xdg_surface, WLE_xdg_surface_configure) == 0 && wayland_frame_wanted(),
                "twenty sizes of the hand in the frame the last was told in are not said, and a frame is wanted (%ld said)",
                wlc_count(c, WLI_xdg_surface, WLE_xdg_surface_configure));
        wayland_frame();
        wlc_pump(c);
        e = wlc_take(c, WLI_xdg_toplevel, WLE_xdg_toplevel_configure);
        WLCHECK(e && e->u[0] == 120 && wlc_count(c, WLI_xdg_surface, WLE_xdg_surface_configure) == 1,
                "the frame's end says the size the hand came to, once (%lu, %ld)", e ? e->u[0] : 0, wlc_count(c, WLI_xdg_surface, WLE_xdg_surface_configure));
        e = wlc_take(c, WLI_xdg_surface, WLE_xdg_surface_configure);
        wlc_send(c, s.xdg, WLQ_xdg_surface_ack_configure, e ? (long)e->u[0] : 0L);
        wlc_forget(c);
        wayland_frame();
        fake.row[0].width_asked = 121;
        wayland_row_wake(0);
        wlc_pump(c);
        WLCHECK(wlc_count(c, WLI_xdg_surface, WLE_xdg_surface_configure) == 1, "a size in a frame that has told none yet is said in it, at once");
        e = wlc_take(c, WLI_xdg_surface, WLE_xdg_surface_configure);
        wlc_send(c, s.xdg, WLQ_xdg_surface_ack_configure, e ? (long)e->u[0] : 0L);
        wlc_forget(c);
        // The hand lets go in the same frame: the state's change is said at once, with the size.
        fake.row[0].width_asked = 130;
        fake.row[0].states = WAYLAND_STATE_ACTIVATED;
        wayland_row_wake(0);
        wlc_pump(c);
        e = wlc_take(c, WLI_xdg_toplevel, WLE_xdg_toplevel_configure);
        WLCHECK(e && e->u[0] == 130, "the resize ended is said at once, not kept for the frame (%lu)", e ? e->u[0] : 0);
        WLCHECK(!wayland_frame_wanted() || wayland.client[c->slot]->waiting, "and no size is owed after it");
        wlc_end(c);
        leak_check("window scope");
        wayland_stop();
}

/*
        The pointer's picture is its client's to ask for while the pointer is on the client's surface, by the serial of the enter:
        hidden (set_cursor with no surface), or one of the compositor's shapes (wp_cursor_shape). The enter's serial is good for as
        long as the pointer stays, however many keys and clicks the client is sent meanwhile; one from an enter before the last is
        stale. The pointer leaving, and the client going, give the pointer back.
*/
static void test_cursor_serial(void)
{
        struct seated t;
        struct wlc *c;
        struct wl_event_seen *e;
        unsigned long manager, device, enter, older;

        wl_reset();
        wayland_start(&fake_host);
        seated_open(&t, 9, 9);
        c = t.s.c;
        {
                // The globals again (seated_open has read its registry's events away).
                unsigned long registry = wlc_id(c, WLI_wl_registry, 1);

                wlc_send(c, 1, WLQ_wl_display_get_registry, registry);
                manager = wl_global(c, registry, WLI_wp_cursor_shape_manager_v1, 2);
        }
        device = wlc_id(c, WLI_wp_cursor_shape_device_v1, 2);
        wlc_send(c, manager, WLQ_wp_cursor_shape_manager_v1_get_pointer, device, t.pointer);
        wlc_forget(c);
        wayland_pointer_to(0, 5, 5);
        wlc_pump(c);
        e = wlc_take(c, WLI_wl_pointer, WLE_wl_pointer_enter);
        older = e ? e->u[0] : 0;
        wayland_pointer_to(-1, 0, 0);
        wlc_forget(c);
        wayland_pointer_to(0, 6, 6);
        wlc_pump(c);
        e = wlc_take(c, WLI_wl_pointer, WLE_wl_pointer_enter);
        enter = e ? e->u[0] : 0;
        WLCHECK(enter && older && enter != older && fake_cursor[0] == -1, "the pointer enters, with a serial, and wears no shape of the client's yet");
        // A dozen keys and clicks, as a person watching a video makes: the enter's serial is far out of the last eight.
        wayland_keyboard_to(0);
        for (int at = 0; at < 12; at++)
        {
                wayland_key(0, 30, 1);
                wayland_key(0, 30, 0);
                wayland_pointer_button(0, 0x110, at & 1);
        }
        wlc_pump(c);
        wlc_send(c, t.pointer, WLQ_wl_pointer_set_cursor, (long)enter, 0UL, 0L, 0L);
        WLCHECK(c->errors == 0 && fake_cursor[0] == WAYLAND_CURSOR_HIDDEN,
                "set_cursor with no surface and the enter's serial hides the pointer, a dozen keys and clicks after the enter (%ld)", fake_cursor[0]);
        wlc_send(c, device, WLQ_wp_cursor_shape_device_v1_set_shape, (long)enter, (long)WLN_wp_cursor_shape_device_v1_shape_pointer);
        WLCHECK(fake_cursor[0] == WAYLAND_CURSOR_HAND, "and a shape by the same serial is worn (%ld)", fake_cursor[0]);
        wlc_send(c, t.pointer, WLQ_wl_pointer_set_cursor, (long)older, 0UL, 0L, 0L);
        WLCHECK(fake_cursor[0] == WAYLAND_CURSOR_HAND, "the serial of an enter before the last is stale: it hides nothing (%ld)", fake_cursor[0]);
        wlc_send(c, t.pointer, WLQ_wl_pointer_set_cursor, (long)enter + 100000, 0UL, 0L, 0L);
        WLCHECK(fake_cursor[0] == WAYLAND_CURSOR_HAND, "nor does a serial the client was never sent (%ld)", fake_cursor[0]);
        wlc_send(c, t.pointer, WLQ_wl_pointer_set_cursor, (long)enter, 0UL, 0L, 0L);
        WLCHECK(fake_cursor[0] == WAYLAND_CURSOR_HIDDEN, "hidden again by the enter's");
        // The pointer leaves: the window's pointer is the compositor's again. Back on it, it stays so until the client asks anew.
        wayland_pointer_to(-1, 0, 0);
        WLCHECK(fake_cursor[0] == -1, "the pointer leaving gives the compositor's pointer back (%ld)", fake_cursor[0]);
        wlc_forget(c);
        wayland_pointer_to(0, 7, 7);
        wlc_pump(c);
        WLCHECK(fake_cursor[0] == -1, "and it is the compositor's when the pointer comes back");
        wlc_send(c, t.pointer, WLQ_wl_pointer_set_cursor, (long)enter, 0UL, 0L, 0L);
        WLCHECK(fake_cursor[0] == -1, "where the serial it hid with before is stale now (%ld)", fake_cursor[0]);
        e = wlc_take(c, WLI_wl_pointer, WLE_wl_pointer_enter);
        wlc_send(c, t.pointer, WLQ_wl_pointer_set_cursor, e ? (long)e->u[0] : 0L, 0UL, 0L, 0L);
        WLCHECK(fake_cursor[0] == WAYLAND_CURSOR_HIDDEN, "and the new enter's hides it");
        // The client goes with the pointer hidden: its window goes, and with it the hiding (the host's word of a row ends with the row).
        wlc_end(c);
        WLCHECK(!fake.row[0].used && wayland.pointer_row == -1, "a client gone with the pointer hidden has no window for it to be hidden over");
        leak_check("cursor serial");
        wayland_stop();
}

/*
        A commit with more damage requests than the shim keeps apart (WAYLAND_DAMAGE): the rest are joined into the last, the picture
        is taken, and the commit's frame callback is answered by the next frame like any other's.
*/
static void test_damage_many(void)
{
        struct session s;
        struct wlc *c;

        wl_reset();
        fake.lend = 1;
        wayland_start(&fake_host);
        session_open(&s);
        session_window(&s, 0xff101010);
        c = s.c;
        for (int kind = 0; kind < 3; kind++)
        {
                unsigned long callback = wlc_id(c, WLI_wl_callback, 1);
                long pictures = fake.row[0].pictures;

                wlc_forget(c);
                wlc_send(c, s.surface, WLQ_wl_surface_attach, s.buffer, 0L, 0L);
                for (long at = 0; at < 24; at++)
                        wlc_send(c, s.surface, kind == 0 || (kind == 2 && (at & 1)) ? WLQ_wl_surface_damage_buffer : WLQ_wl_surface_damage, at, at % 20, 4L, 3L);
                wlc_send(c, s.surface, WLQ_wl_surface_frame, callback);
                wlc_send(c, s.surface, WLQ_wl_surface_commit);
                WLCHECK(c->errors == 0 && !wayland_stalled(c->slot) && fake.row[0].pictures == pictures + 1 && wayland_frame_wanted(),
                        "a commit with twenty-four damage requests is made, and a frame is wanted for its callback (kind %d, %s)", kind, c->error_text);
                wayland_drawn();
                wayland_frame();
                wlc_pump(c);
                WLCHECK(wlc_count(c, WLI_wl_callback, WLE_wl_callback_done) == 1, "and the next frame answers it (kind %d: %ld)", kind,
                        wlc_count(c, WLI_wl_callback, WLE_wl_callback_done));
        }
        wlc_end(c);
        leak_check("damage many");
        wayland_stop();
}

static struct wayland_picture cap_pic;
static int cap_pic_seen;

static int cap_row_picture(void *context, int row, const struct wayland_picture *picture)
{
        cap_pic = *picture;
        cap_pic_seen = 1;
        return fake_row_picture(context, row, picture);
}

static void *unmapped_dmabuf_map(void *context, long fd, long size)
{
        (void)context;
        (void)fd;
        (void)size;
        return WAYLAND_DMABUF_UNMAPPED;
}

static struct
{
        int imports, drops, readies, signals;
        int ready_answer;
        unsigned long long last_acq, last_rel;
} fake_sync;

static long fake_sync_available(void *context, char *node, long node_cap)
{
        (void)context;
        snprintf(node, (size_t)node_cap, "/dev/dri/renderD128");
        return WAYLAND_SYNCOBJ_TIMELINE_OK | WAYLAND_SYNCOBJ_NODE_OK;
}

static long fake_sync_import(void *context, long fd)
{
        (void)context;
        (void)fd;
        return 100 + (++fake_sync.imports);
}

static void fake_sync_drop(void *context, long handle)
{
        (void)context;
        (void)handle;
        fake_sync.drops++;
}

static int fake_sync_ready(void *context, long handle, unsigned long long point)
{
        (void)context;
        (void)handle;
        fake_sync.readies++;
        fake_sync.last_acq = point;
        return fake_sync.ready_answer;
}

static void fake_sync_signal(void *context, long handle, unsigned long long point)
{
        (void)context;
        (void)handle;
        fake_sync.signals++;
        fake_sync.last_rel = point;
}

static void test_syncobj_viewporter_and_formats(void)
{
        struct wayland_host host = fake_host;
        struct wayland_syncobj_host shost = {0, fake_sync_available, fake_sync_import, fake_sync_drop, fake_sync_ready, fake_sync_signal, 0};
        struct session s;
        struct wlc *c;
        unsigned long vp_mgr, vp, params, buf, drm, sync_mgr, sync_surf, tl;
        unsigned char abgr_px[4 * 4 * 4];

        /* 1. Non-mmapable dma-buf (WAYLAND_DMABUF_UNMAPPED) + ABGR8888/MODIFIER_INVALID + wp_viewporter. The table offers ABGR8888, as the
              compositor's does: a pair not in it is refused (wayland_dmabuf_alloc_buffer, since 5f74af4), which the row was not told of. */
        wayland_stop();
        memset(&fake, 0, sizeof fake);
        fake.dmabuf_bgr = 1;
        fake.screen.width = 1280;
        fake.screen.height = 720;
        fake.screen.scale = 100;
        fake.screen.refresh = 60000;
        host.row_picture = cap_row_picture;
        host.dmabuf_map = unmapped_dmabuf_map;
        wayland_start(&host);
        session_open(&s);
        session_window(&s, 0xff112233u);
        c = s.c;

        /* shm abgr8888 & xbgr8888 */
        memset(abgr_px, 0xaa, sizeof abgr_px);
        {
                long fd = wl_file(sizeof abgr_px, 0);
                unsigned long pool = wlc_id(c, WLI_wl_shm_pool, 1);
                unsigned long b_abgr = wlc_id(c, WLI_wl_buffer, 1);
                unsigned long b_xbgr = wlc_id(c, WLI_wl_buffer, 1);

                wl_file_put(fd, 0, abgr_px, sizeof abgr_px);
                wlc_send(c, s.shm, WLQ_wl_shm_create_pool, pool, fd, (long)sizeof abgr_px);
                close((int)fd);
                wlc_send(c, pool, WLQ_wl_shm_pool_create_buffer, b_abgr, 0L, 4L, 4L, 16L, (long)WLN_wl_shm_format_abgr8888);
                wlc_send(c, pool, WLQ_wl_shm_pool_create_buffer, b_xbgr, 0L, 4L, 4L, 16L, (long)WLN_wl_shm_format_xbgr8888);
                WLCHECK(c->errors == 0, "wl_shm accepts abgr8888 and xbgr8888 formats");
        }

        /* wp_viewporter source crop & destination scale: a source inside the 16 by 8 buffer below (one past its edge is the viewport's
           out_of_buffer error, which 5f74af4 holds the commit to, and which a source of 16 by 12 at 2, 3 was) */
        vp_mgr = wl_global(c, s.registry, WLI_wp_viewporter, 1);
        WLCHECK(vp_mgr != 0, "wp_viewporter global is advertised");
        vp = wlc_id(c, WLI_wp_viewport, 1);
        wlc_send(c, vp_mgr, WLQ_wp_viewporter_get_viewport, vp, s.surface);
        wlc_send(c, vp, WLQ_wp_viewport_set_source, (long)(2 * 256), (long)(3 * 256), (long)(12 * 256), (long)(4 * 256));
        wlc_send(c, vp, WLQ_wp_viewport_set_destination, 640L, 480L);

        /* non-mmapable dma-buf with ABGR8888 & MODIFIER_INVALID */
        params = wlc_id(c, WLI_zwp_linux_buffer_params_v1, 4);
        wlc_send(c, s.dmabuf, WLQ_zwp_linux_dmabuf_v1_create_params, params);
        wlc_send(c, params, WLQ_zwp_linux_buffer_params_v1_add, dmabuf_file(16, 80, 8, 0), 0L, 0L, 80L,
                 (unsigned long)(WAYLAND_MODIFIER_INVALID >> 32), (unsigned long)(WAYLAND_MODIFIER_INVALID & 0xffffffffUL));
        buf = wlc_id(c, WLI_wl_buffer, 1);
        wlc_send(c, params, WLQ_zwp_linux_buffer_params_v1_create_immed, buf, 16L, 8L, (long)WAYLAND_FOURCC_ABGR8888, 0L);
        cap_pic_seen = 0;
        wlc_send(c, s.surface, WLQ_wl_surface_attach, buf, 0L, 0L);
        wlc_send(c, s.surface, WLQ_wl_surface_damage_buffer, 0L, 0L, 16L, 8L);
        wlc_send(c, s.surface, WLQ_wl_surface_commit);
        WLCHECK(c->errors == 0 && cap_pic_seen && cap_pic.map == 0 && cap_pic.sync_fd >= 0 &&
                    cap_pic.src_x == 2 * 256 && cap_pic.src_y == 3 * 256 && cap_pic.src_width == 12 * 256 && cap_pic.src_height == 4 * 256 &&
                    cap_pic.dst_width == 640 && cap_pic.dst_height == 480 &&
                    cap_pic.fourcc == WAYLAND_FOURCC_ABGR8888 && cap_pic.modifier == WAYLAND_MODIFIER_INVALID,
                "unmapped dma-buf passes sync_fd with map==0, fourcc/modifier, and wp_viewporter crop+scale");

        /* feedback queue past WAYLAND_OUT_FDS */
        {
                unsigned long fbs[WAYLAND_OUT_FDS + 2];
                long done_count = 0;

                wlc_forget(c);
                for (long i = 0; i < WAYLAND_OUT_FDS + 2; i++)
                {
                        fbs[i] = wlc_id(c, WLI_zwp_linux_dmabuf_feedback_v1, 4);
                        wlc_send(c, s.dmabuf, WLQ_zwp_linux_dmabuf_v1_get_default_feedback, fbs[i]);
                }
                for (int round = 0; round < 6; round++)
                        wlc_pump(c);
                done_count = wlc_count(c, WLI_zwp_linux_dmabuf_feedback_v1, WLE_zwp_linux_dmabuf_feedback_v1_done);
                WLCHECK(done_count == WAYLAND_OUT_FDS + 2,
                        "dmabuf feedback sends past WAYLAND_OUT_FDS flush queued feedbacks (%ld)", done_count);
        }
        wlc_end(c);
        wayland_stop();

        /* 1b. The same buffer where the table offers no ABGR8888: refused at create_immed, as the protocol says (invalid_wl_buffer) */
        memset(&fake, 0, sizeof fake);
        fake.screen.width = 1280;
        fake.screen.height = 720;
        fake.screen.scale = 100;
        fake.screen.refresh = 60000;
        wayland_start(&host);
        session_open(&s);
        c = s.c;
        params = wlc_id(c, WLI_zwp_linux_buffer_params_v1, 4);
        wlc_send(c, s.dmabuf, WLQ_zwp_linux_dmabuf_v1_create_params, params);
        wlc_send(c, params, WLQ_zwp_linux_buffer_params_v1_add, dmabuf_file(16, 80, 8, 0), 0L, 0L, 80L,
                 (unsigned long)(WAYLAND_MODIFIER_INVALID >> 32), (unsigned long)(WAYLAND_MODIFIER_INVALID & 0xffffffffUL));
        buf = wlc_id(c, WLI_wl_buffer, 1);
        wlc_send(c, params, WLQ_zwp_linux_buffer_params_v1_create_immed, buf, 16L, 8L, (long)WAYLAND_FOURCC_ABGR8888, 0L);
        WLCHECK(refused_with(c, WLN_zwp_linux_buffer_params_v1_error_invalid_wl_buffer),
                "a format and modifier the table does not offer is refused at create_immed (invalid_wl_buffer)");
        wlc_end(c);
        wayland_stop();

        /* 2. wayland_syncobj_host + wl_drm + uid 1000 access when xwayland_uid == 0 */
        memset(&fake, 0, sizeof fake);
        memset(&fake_sync, 0, sizeof fake_sync);
        fake.screen.width = 1280;
        fake.screen.height = 720;
        fake.screen.scale = 100;
        wayland_start(&fake_host);
        wayland_syncobj_host(&shost);
        wl_check_xuser = 0; /* uid 1000 client from wlc_open still gets xwayland globals */
        session_open(&s);
        wayland_xwayland_user(0);
        c = s.c;
        WLCHECK(s.xshell != 0, "wayland_xwayland_user(0) still exposes xwayland_shell_v1 to uid 1000");

        drm = wl_global(c, s.registry, WLI_wl_drm, 2);
        sync_mgr = wl_global(c, s.registry, WLI_wp_linux_drm_syncobj_manager_v1, 1);
        WLCHECK(drm != 0 && sync_mgr != 0, "wl_drm and wp_linux_drm_syncobj_manager_v1 globals advertised when syncobj host is up");
        session_window(&s, 0xff00ff00u);
        tl = wlc_id(c, WLI_wp_linux_drm_syncobj_timeline_v1, 1);
        sync_surf = wlc_id(c, WLI_wp_linux_drm_syncobj_surface_v1, 1);
        wlc_send(c, sync_mgr, WLQ_wp_linux_drm_syncobj_manager_v1_import_timeline, tl, wl_file(4096, 0));
        wlc_send(c, sync_mgr, WLQ_wp_linux_drm_syncobj_manager_v1_get_surface, sync_surf, s.surface);
        wlc_send(c, sync_surf, WLQ_wp_linux_drm_syncobj_surface_v1_set_acquire_point, tl, 0L, 42L);
        wlc_send(c, sync_surf, WLQ_wp_linux_drm_syncobj_surface_v1_set_release_point, tl, 0L, 43L);
        params = dmabuf_params(&s, dmabuf_file(16, 80, 8, 0), 0, 0, 80, 0);
        buf = wlc_id(c, WLI_wl_buffer, 1);
        wlc_send(c, params, WLQ_zwp_linux_buffer_params_v1_create_immed, buf, 16L, 8L, DRM_XRGB8888, 0L);
        fake_sync.ready_answer = 0; /* wait on acquire point -> stall */
        wlc_send(c, s.surface, WLQ_wl_surface_attach, buf, 0L, 0L);
        wlc_send(c, s.surface, WLQ_wl_surface_damage_buffer, 0L, 0L, 16L, 8L);
        wlc_send(c, s.surface, WLQ_wl_surface_commit);
        WLCHECK(wayland_stalled(c->slot) && fake_sync.readies >= 1 && fake_sync.last_acq == 42ULL,
                "unready syncobj acquire point stalls commit and queries host");
        fake_sync.ready_answer = 1;
        wayland_resume(c->slot);
        wlc_pump(c);
        WLCHECK(!wayland_stalled(c->slot) && c->errors == 0, "ready syncobj acquire point unblocks commit cleanly");
        wlc_send(c, sync_surf, WLQ_wp_linux_drm_syncobj_surface_v1_destroy);
        wlc_send(c, tl, WLQ_wp_linux_drm_syncobj_timeline_v1_destroy);
        wlc_end(c);
        WLCHECK(fake_sync.signals >= 1 && fake_sync.last_rel == 43ULL && fake_sync.drops >= 1,
                "syncobj release point is signalled and timeline handle dropped on destroy");
        wayland_syncobj_host(0);
        wl_check_xuser = 1000;
        leak_check("syncobj/viewporter/formats");
        wayland_stop();
}

/* An override-redirect X popup moved by its window manager (adopted again with its new place) is sent to the host at the new place. */
static void test_xwm_moves(void)
{
        struct session s;
        struct wlc *c;
        unsigned long parent, popup, pbuf, qbuf, xwin, xpopup;
        long prow;

        wl_reset();
        wayland_start(&fake_host);
        session_open(&s);
        c = s.c;
        parent = wlc_id(c, WLI_wl_surface, 6);
        wlc_send(c, s.compositor, WLQ_wl_compositor_create_surface, parent);
        xwin = wlc_id(c, WLI_xwayland_surface_v1, 1);
        wlc_send(c, s.xshell, WLQ_xwayland_shell_v1_get_xwayland_surface, xwin, parent);
        wlc_send(c, xwin, WLQ_xwayland_surface_v1_set_serial, 0x5151UL, 0UL);
        wayland_xwindow_adopt(c->slot, 0x5151, 0, 0, 0, "parent");
        pbuf = wl_buffer_of(&s, 32, 24, 0xff111111, WLN_wl_shm_format_xrgb8888);
        wlc_send(c, parent, WLQ_wl_surface_attach, pbuf, 0L, 0L);
        wlc_send(c, parent, WLQ_wl_surface_commit);
        popup = wlc_id(c, WLI_wl_surface, 6);
        wlc_send(c, s.compositor, WLQ_wl_compositor_create_surface, popup);
        xpopup = wlc_id(c, WLI_xwayland_surface_v1, 1);
        wlc_send(c, s.xshell, WLQ_xwayland_shell_v1_get_xwayland_surface, xpopup, popup);
        wlc_send(c, xpopup, WLQ_xwayland_surface_v1_set_serial, 0x5252UL, 0UL);
        wayland_xwindow_adopt(c->slot, 0x5252, 1, 5, 6, "popup");
        qbuf = wl_buffer_of(&s, 8, 8, 0xff222222, WLN_wl_shm_format_xrgb8888);
        wlc_send(c, popup, WLQ_wl_surface_attach, qbuf, 0L, 0L);
        wlc_send(c, popup, WLQ_wl_surface_commit);
        prow = wayland_xwindow_adopt(c->slot, 0x5252, 1, 5, 6, "popup");
        WLCHECK(c->errors == 0 && prow >= 0, "an override-redirect popup under a parent is shown (%ld, %s)", prow, c->error_text);
        prow = wayland_xwindow_adopt(c->slot, 0x5252, 1, 10, 12, "popup");
        WLCHECK(prow >= 0 && fake.row[prow].x == 10 && fake.row[prow].y == 12,
                "a popup moved by its window manager is placed at its new place in the host (%ld, %ld)", prow >= 0 ? fake.row[prow].x : -1L,
                prow >= 0 ? fake.row[prow].y : -1L);
        wlc_end(c);
        leak_check("xwm popup moved");
        wayland_stop();
}

/*
        Where an X window is put: one with no place of its own is a toplevel the compositor centres (no parent); one that hard-set its place
        (USPosition, PPosition: WAYLAND_XWINDOW_PLACED) is a placed toplevel at X's place; a transient (WAYLAND_XWINDOW_OWNER) is a
        toplevel whose parent is the window it is for, which the compositor centres it on.
*/
static long xwm_places_show(struct session *s, unsigned long serial, long how, long x, long y)
{
        struct wlc *c = s->c;
        unsigned long surface = wlc_id(c, WLI_wl_surface, 6), xwin, buffer;

        wlc_send(c, s->compositor, WLQ_wl_compositor_create_surface, surface);
        xwin = wlc_id(c, WLI_xwayland_surface_v1, 1);
        wlc_send(c, s->xshell, WLQ_xwayland_shell_v1_get_xwayland_surface, xwin, surface);
        wlc_send(c, xwin, WLQ_xwayland_surface_v1_set_serial, serial, 0UL);
        wayland_xwindow_adopt(c->slot, serial, how, x, y, "x");
        buffer = wl_buffer_of(s, 32, 24, 0xff333333, WLN_wl_shm_format_xrgb8888);
        wlc_send(c, surface, WLQ_wl_surface_attach, buffer, 0L, 0L);
        wlc_send(c, surface, WLQ_wl_surface_commit);
        return wayland_xwindow_adopt(c->slot, serial, how, x, y, "x");
}

static void test_xwm_places(void)
{
        struct session s;
        long free_row, placed_row, owned_row;

        wl_reset();
        wayland_start(&fake_host);
        session_open(&s);
        free_row = xwm_places_show(&s, 0x6161, 0, 7, 9);
        placed_row = xwm_places_show(&s, 0x6262, WAYLAND_XWINDOW_PLACED, 30, 40);
        owned_row = free_row >= 0 ? xwm_places_show(&s, 0x6363, WAYLAND_XWINDOW_OWNER(free_row), 7, 9) : -1;
        WLCHECK(s.c->errors == 0 && free_row >= 0 && fake.row[free_row].kind == WAYLAND_KIND_TOPLEVEL && fake.row[free_row].parent == -1,
                "an X window with no place of its own is a toplevel with no parent, for the compositor to centre (%ld)", free_row);
        WLCHECK(placed_row >= 0 && fake.row[placed_row].kind == WAYLAND_KIND_PLACED && fake.row[placed_row].x == 30 &&
                    fake.row[placed_row].y == 40,
                "an X window that hard-set its place is a placed toplevel at X's place (%ld)", placed_row);
        WLCHECK(owned_row >= 0 && fake.row[owned_row].kind == WAYLAND_KIND_TOPLEVEL && fake.row[owned_row].parent == (int)free_row,
                "an X transient is a toplevel whose parent is the window it is for (%ld)", owned_row);
        wlc_end(s.c);
        leak_check("xwm places");
        wayland_stop();
}

/* A feedback of a frame that was not drawn, its window let go (a null buffer), is discarded, never answered presented later. */
static void test_presented_discard_row(void)
{
        struct session s;
        struct wlc *c;
        unsigned long presentation, feedback, buffer;

        wl_reset();
        wayland_start(&fake_host);
        session_open(&s);
        session_window(&s, 0xff223344);
        c = s.c;
        presentation = wl_global(c, s.registry, WLI_wp_presentation, 2);
        wlc_forget(c);
        feedback = wlc_id(c, WLI_wp_presentation_feedback, 1);
        wlc_send(c, presentation, WLQ_wp_presentation_feedback, s.surface, feedback);
        wlc_send(c, s.surface, WLQ_wl_surface_commit);
        wlc_pump(c);
        wlc_forget(c);
        wlc_send(c, s.surface, WLQ_wl_surface_attach, 0UL, 0L, 0L);
        wlc_send(c, s.surface, WLQ_wl_surface_commit);
        wlc_pump(c);
        WLCHECK(c->errors == 0 && !fake.row[0].used && wlc_count(c, WLI_wp_presentation_feedback, WLE_wp_presentation_feedback_discarded) == 1,
                "a window let go with a feedback still waiting answers that feedback discarded (%s)", c->error_text);
        wlc_forget(c);
        wlc_send(c, s.surface, WLQ_wl_surface_commit);
        wlc_pump(c);
        state_ack(&s, wlc_take(c, WLI_xdg_surface, WLE_xdg_surface_configure));
        wlc_forget(c);
        buffer = wl_buffer_of(&s, 32, 24, 0xff556677, WLN_wl_shm_format_xrgb8888);
        wlc_send(c, s.surface, WLQ_wl_surface_attach, buffer, 0L, 0L);
        wlc_send(c, s.surface, WLQ_wl_surface_damage_buffer, 0L, 0L, 32L, 24L);
        wlc_send(c, s.surface, WLQ_wl_surface_commit);
        wayland_frame();
        wayland_presented(6000000000ul, 16666667ul, 9, 0ul, -1L);
        wlc_pump(c);
        WLCHECK(c->errors == 0 && wlc_count(c, WLI_wp_presentation_feedback, WLE_wp_presentation_feedback_presented) == 0,
                "the window shown again presents no feedback of the frame that was let go (%s)", c->error_text);
        wlc_end(c);
        leak_check("presenting let go");
        wayland_stop();
}

/* A child committed under a root with no buffer yet (not mapped): its frame callback is answered without the root mapping. */
/*
        The buffers read in place that a busy row's job may still read are kept until a frame is drawn, however many there are: a client that
        commits 33 times before that frame has none of them released early (wayland_lent_let, wayland_gone_drawn), and all of them at the frame.
*/
static void test_gone_list(void)
{
        struct wayland_host host = fake_host;
        struct session s;
        struct wlc *c;
        unsigned long buffer[40];
        long early = 0, at;

        host.row_busy = fake_row_busy;
        wl_reset();
        fake.lend = 1;
        fake_busy = 0;
        wayland_start(&host);
        session_open(&s);
        session_window(&s, 0xff101010);
        c = s.c;
        fake_busy = 1;
        for (at = 0; at < 40; at++)
        {
                buffer[at] = wl_buffer_of(&s, 32, 24, 0xff000000u | (unsigned int)(at * 7 + 1), WLN_wl_shm_format_xrgb8888);
                wlc_send(c, s.surface, WLQ_wl_surface_attach, buffer[at], 0L, 0L);
                wlc_send(c, s.surface, WLQ_wl_surface_damage_buffer, 0L, 0L, 32L, 24L);
                wlc_send(c, s.surface, WLQ_wl_surface_commit);
                wlc_pump(c);
        }
        early = releases_of(c, s.buffer);
        for (at = 0; at < 40; at++)
                early += releases_of(c, buffer[at]);
        WLCHECK(early == 0, "a client committing 40 times before a frame with a busy row gets no buffer released early (%ld released)", early);
        fake_busy = 0;
        wayland_drawn();
        wlc_pump(c);
        early = releases_of(c, s.buffer);
        for (at = 0; at < 39; at++)
                early += releases_of(c, buffer[at]);
        WLCHECK(early == 40, "and the frame drawn after the job lets all 40 go (%ld released of 40)", early);
        wlc_end(c);
        wayland_stop();
}

/*
        A destroyed activation token is the one its object was given, not whichever token the slot holds by then: X is made, committed and used,
        so its slot is free; Y takes that slot; X is destroyed, and Y, which is still pending, is still made (its commit gives it out).
*/
static void test_token_reuse(void)
{
        struct session s;
        struct wlc *c;
        unsigned long activation, first, second;
        char given[64];
        long a;

        wl_reset();
        wayland_start(&fake_host);
        session_open(&s);
        session_window(&s, 0xff223344);
        c = s.c;
        activation = wl_global(c, s.registry, WLI_xdg_activation_v1, 1);
        first = wlc_id(c, WLI_xdg_activation_token_v1, 1);
        wlc_send(c, activation, WLQ_xdg_activation_v1_get_activation_token, first);
        wlc_send(c, first, WLQ_xdg_activation_token_v1_commit);
        wlc_pump(c);
        a = event_at(c, WLI_xdg_activation_token_v1, WLE_xdg_activation_token_v1_done, 0);
        WLCHECK(a >= 0, "the first token is given out");
        if (a >= 0)
                memcpy(given, c->ev[a].text[0], sizeof given);
        else
                given[0] = 0;
        wlc_forget(c);
        wlc_send(c, activation, WLQ_xdg_activation_v1_activate, given, s.surface);
        second = wlc_id(c, WLI_xdg_activation_token_v1, 1);
        wlc_send(c, activation, WLQ_xdg_activation_v1_get_activation_token, second);
        wlc_send(c, first, WLQ_xdg_activation_token_v1_destroy);
        wlc_send(c, second, WLQ_xdg_activation_token_v1_commit);
        wlc_pump(c);
        a = event_at(c, WLI_xdg_activation_token_v1, WLE_xdg_activation_token_v1_done, 0);
        WLCHECK(a >= 0 && strlen(c->ev[a].text[0]) == 32,
                "destroying a consumed token does not kill the next token of the same client (its commit gives it out)");
        wlc_end(c);
        wayland_stop();
}

static void test_sub_root_unmapped(void)
{
        struct session s;
        struct wlc *c;
        unsigned long root, child, sub, subcompositor, callback;

        wl_reset();
        wayland_start(&fake_host);
        session_open(&s);
        c = s.c;
        subcompositor = wl_global(c, s.registry, WLI_wl_subcompositor, 1);
        root = wlc_id(c, WLI_wl_surface, 6);
        wlc_send(c, s.compositor, WLQ_wl_compositor_create_surface, root);
        child = wlc_id(c, WLI_wl_surface, 6);
        wlc_send(c, s.compositor, WLQ_wl_compositor_create_surface, child);
        sub = wlc_id(c, WLI_wl_subsurface, 1);
        wlc_send(c, subcompositor, WLQ_wl_subcompositor_get_subsurface, sub, child, root);
        wlc_send(c, sub, WLQ_wl_subsurface_set_desync);
        callback = wlc_id(c, WLI_wl_callback, 1);
        wlc_forget(c);
        wlc_send(c, child, WLQ_wl_surface_frame, callback);
        wlc_send(c, child, WLQ_wl_surface_commit);
        wlc_pump(c);
        WLCHECK(c->errors == 0 && wlc_count(c, WLI_wl_callback, WLE_wl_callback_done) == 1,
                "a subsurface's frame callback under a root that is not mapped is answered at once (%s)", c->error_text);
        wlc_end(c);
        leak_check("sub root unmapped");
        wayland_stop();
}

int main(int argc, char **argv)
{
        long rounds = argc > 1 ? atol(argv[1]) : 20000;

        test_damage_outside();
        test_cursor_serial();
        test_damage_many();
        test_window_scope();
        test_lent();
        test_versions();
        test_popup_place();
        test_flow();
        test_configure();
        test_configure_owed();
        test_state_remap();
        test_destroy_orders();
        test_shm();
        test_frames();
        test_sdl_show();
        test_xwm_moves();
        test_xwm_places();
        test_presented_discard_row();
        test_sub_root_unmapped();
        test_gone_list();
        test_token_reuse();
        test_frame_overflow();
        test_presentation();
        test_touch();
        test_axis_stop();
        test_hints();
        test_inhibit_state();
        test_idle_notify();
        test_icon();
        test_activation();
        test_pacing();
        test_barrier();
        test_shortcuts();
        test_cursor_shape();
        test_pinch();
        test_text();
        test_popups();
        test_surface_rules();
        test_wire();
        test_caps();
        test_seat();
        test_subsurfaces();
        test_holds();
        test_subsurface_count();
        test_tree_cost();
        test_geometry();
        test_opaque_rect();
        test_cursor();
        test_cursor_picture();
        test_layers();
        test_row_busy();
        test_budget();
        test_stall_state();
        test_dmabuf();
        test_stall_dmabuf();
        test_dmabuf_feedbacks();
        test_syncobj_viewporter_and_formats();
        test_games();
        test_backlog();
        test_scale();
        test_fractional_damage();
        test_outputs();
        test_logical_space();
        test_tree_fuzz(rounds);
        test_clipboard();
        test_bridge();
        test_clip_fuzz(rounds);
        test_fuzz(rounds);
        test_raw(rounds);
        printf("CHECK_wayland: %u checks, %u failures\n", wl_checks, wl_failures);
        return wl_failures != 0;
}
#endif /* CHECK_wayland */

#ifdef CHECK_xwm_wire
/*
        CHECK_xwm_wire: the X11 wire codec (src/canvas/xwm_wire.c) that the window manager of Xwayland speaks. Every request
        is checked against the bytes it should be, written out by hand here; every event, reply and error is parsed from a
        sample laid out the same way; and the hostile rows feed every parser each prefix of a sample, lengths that lie, caps,
        a buffer too small for an encoder, and 10000 bytes from a fixed seed, which must never crash and always answer a
        bounded result. Built with the sanitizers (address and undefined) it proves no byte is read or written past a buffer.

            cc -std=gnu11 -O2 -Wall -Wextra -DCHECK_xwm_wire test/checks.c
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../src/canvas/xwm_wire.c"

static unsigned xw_checks, xw_failures;

#define XWCHECK(condition, ...)                                                                                       \
        do                                                                                                            \
        {                                                                                                             \
                xw_checks++;                                                                                          \
                if (!(condition))                                                                                     \
                {                                                                                                     \
                        xw_failures++;                                                                                \
                        printf("  FAIL: ");                                                                           \
                        printf(__VA_ARGS__);                                                                          \
                        printf(" (line %d)\n", __LINE__);                                                             \
                }                                                                                                     \
        } while (0)

/* ---- helpers: a copy of exactly n bytes, so that an overread is past the allocation and the sanitizers see it ---- */

static unsigned char *xw_exact(const unsigned char *src, unsigned long n)
{
        unsigned char *p = malloc(n ? n : 1);

        if (!p)
        {
                printf("  FAIL: out of memory\n");
                exit(2);
        }
        if (n)
                memcpy(p, src, n);
        return p;
}

static unsigned char *xw_blank(unsigned long n)
{
        unsigned char *p = malloc(n ? n : 1);

        if (!p)
        {
                printf("  FAIL: out of memory\n");
                exit(2);
        }
        memset(p, 0, n ? n : 1);
        return p;
}

static int xw_same(const unsigned char *a, const unsigned char *b, unsigned long n)
{
        return memcmp(a, b, n) == 0;
}

/* Writes v into n bytes at off, little-endian, as the test's own layout of a message. */
static void xw_u(unsigned char *b, unsigned long off, unsigned long v, int n)
{
        for (int i = 0; i < n; i++)
                b[off + i] = (unsigned char)((v >> (8 * i)) & 0xffu);
}

/* The parsers as one shape, so the truncation rows can run each one at every length. */
static int xw_p_event(const unsigned char *b, unsigned long n)
{
        struct xwm_event e;
        return xwm_parse_event(b, n, &e);
}

static int xw_p_error(const unsigned char *b, unsigned long n)
{
        struct xwm_error e;
        return xwm_parse_error(b, n, &e);
}

static int xw_p_atom(const unsigned char *b, unsigned long n)
{
        struct xwm_atom_reply r;
        return xwm_parse_atom_reply(b, n, &r);
}

static int xw_p_property(const unsigned char *b, unsigned long n)
{
        struct xwm_property_reply r;
        return xwm_parse_property_reply(b, n, &r);
}

static int xw_p_geometry(const unsigned char *b, unsigned long n)
{
        struct xwm_geometry_reply r;
        return xwm_parse_geometry_reply(b, n, &r);
}

static int xw_p_attributes(const unsigned char *b, unsigned long n)
{
        struct xwm_attributes_reply r;
        return xwm_parse_attributes_reply(b, n, &r);
}

static int xw_p_setup(const unsigned char *b, unsigned long n)
{
        struct xwm_setup s;
        return xwm_parse_setup(b, n, &s);
}

/* The sample messages, laid out by hand from the protocol's field offsets. */

static void xw_sample_map_request(unsigned char *e)
{
        memset(e, 0, 32);
        e[0] = 20; xw_u(e, 2, 0x0102, 2); xw_u(e, 4, 0x5a1, 4); xw_u(e, 8, 0x400001, 4);
}

static void xw_sample_configure_request(unsigned char *e)
{
        memset(e, 0, 32);
        e[0] = 0x80 | 23; e[1] = 1; xw_u(e, 2, 7, 2); xw_u(e, 4, 0x5a1, 4); xw_u(e, 8, 0x400001, 4);
        xw_u(e, 12, 0, 4); xw_u(e, 16, (unsigned long)-3 & 0xffff, 2); xw_u(e, 18, 10, 2);
        xw_u(e, 20, 640, 2); xw_u(e, 22, 480, 2); xw_u(e, 24, 0, 2); xw_u(e, 26, 0x0f, 2);
}

static void xw_sample_setup(unsigned char *b)
{
        memset(b, 0, 144);
        b[0] = 1; xw_u(b, 2, 11, 2); xw_u(b, 4, 0, 2); xw_u(b, 6, 34, 2); xw_u(b, 8, 11, 4);
        xw_u(b, 12, 0x00200000, 4); xw_u(b, 16, 0x001fffff, 4); xw_u(b, 20, 256, 4);
        xw_u(b, 24, 8, 2); xw_u(b, 26, 65535, 2); b[28] = 1; b[29] = 2; b[30] = 0; b[31] = 0;
        b[32] = 32; b[33] = 32; b[34] = 8; b[35] = 255;
        memcpy(b + 40, "Xwayland", 8);
        b[48] = 24; b[49] = 32; b[50] = 32;
        b[56] = 32; b[57] = 32; b[58] = 32;
        /* the screen, at 64 */
        xw_u(b, 64, 0x5a1, 4); xw_u(b, 68, 0x20, 4); xw_u(b, 72, 0xffffff, 4); xw_u(b, 76, 0, 4);
        xw_u(b, 80, 0x001a0000, 4); xw_u(b, 84, 1920, 2); xw_u(b, 86, 1080, 2); xw_u(b, 88, 508, 2); xw_u(b, 90, 285, 2);
        xw_u(b, 92, 1, 2); xw_u(b, 94, 1, 2); xw_u(b, 96, 0x21, 4); b[100] = 0; b[101] = 0; b[102] = 24; b[103] = 2;
        /* depth 24 with one visual, then depth 32 with none */
        b[104] = 24; xw_u(b, 106, 1, 2);
        xw_u(b, 112, 0x21, 4); b[116] = 4; b[117] = 8; xw_u(b, 118, 256, 2); xw_u(b, 120, 0xff0000, 4); xw_u(b, 124, 0xff00, 4); xw_u(b, 128, 0xff, 4);
        b[136] = 32; xw_u(b, 138, 0, 2);
}

/* ---- the encoders: each request as the bytes it must be ---- */

static void xw_test_setup_request(void)
{
        static const unsigned char want[12] = {0x6c, 0x00, 0x0b, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
        unsigned char out[32];

        memset(out, 0xee, sizeof out);
        XWCHECK(xwm_enc_setup_request(out, sizeof out) == 12, "the setup request is 12 bytes");
        XWCHECK(xw_same(out, want, 12), "the setup request asks for little-endian, protocol 11.0, and no authorization");
        XWCHECK(xwm_enc_setup_request(out, 11) == XWM_ERR_SPACE, "a setup request into 11 bytes is refused");
        XWCHECK(xwm_enc_setup_request(0, 12) == XWM_ERR_ARG, "a setup request into no buffer is refused");
}

static void xw_test_intern_atom(void)
{
        static const unsigned char wm_protocols[20] = {0x10, 0x00, 0x05, 0x00, 0x0c, 0x00, 0x00, 0x00,
                'W', 'M', '_', 'P', 'R', 'O', 'T', 'O', 'C', 'O', 'L', 'S'};
        static const unsigned char wm_s0[16] = {0x10, 0x01, 0x04, 0x00, 0x05, 0x00, 0x00, 0x00,
                'W', 'M', '_', 'S', '0', 0x00, 0x00, 0x00};
        unsigned char out[32];
        long n;

        n = xwm_enc_intern_atom(out, sizeof out, 0, (const unsigned char *)"WM_PROTOCOLS", 12);
        XWCHECK(n == 20 && xw_same(out, wm_protocols, 20), "InternAtom WM_PROTOCOLS: 20 bytes, length 5 units, the name at 8");
        n = xwm_enc_intern_atom(out, sizeof out, 1, (const unsigned char *)"WM_S0", 5);
        XWCHECK(n == 16 && xw_same(out, wm_s0, 16), "InternAtom WM_S0 with only-if-exists: the name padded to 8 bytes, length 4 units");
        XWCHECK(xwm_enc_intern_atom(out, 19, 0, (const unsigned char *)"WM_PROTOCOLS", 12) == XWM_ERR_SPACE, "InternAtom into 19 bytes is refused");
        XWCHECK(xwm_enc_intern_atom(out, sizeof out, 0, (const unsigned char *)"", 0) == XWM_ERR_ARG, "an empty atom name is refused");
        XWCHECK(xwm_enc_intern_atom(out, sizeof out, 0, 0, 5) == XWM_ERR_ARG, "an atom name with no bytes is refused");
}

static void xw_test_get_property(void)
{
        static const unsigned char want[24] = {0x14, 0x00, 0x06, 0x00, 0x01, 0x00, 0x40, 0x00, 0x27, 0x00, 0x00, 0x00,
                0x1f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00};
        unsigned char out[24];
        long n;

        n = xwm_enc_get_property(out, sizeof out, 0, 0x00400001, 39, 31, 0, 256);
        XWCHECK(n == 24 && xw_same(out, want, 24), "GetProperty of WM_NAME as STRING, 256 units at offset 0");
        out[1] = 0xee;
        XWCHECK(xwm_enc_get_property(out, sizeof out, 1, 0x00400001, 39, 31, 0, 256) == 24 && out[1] == 1, "GetProperty with delete set sends the delete flag");
        XWCHECK(xwm_enc_get_property(out, sizeof out, 0, 0x00400001, 39, 31, 0, 16385) == XWM_ERR_CAP, "a GetProperty asking for more than the property cap is refused");
        XWCHECK(xwm_enc_get_property(out, 23, 0, 0x00400001, 39, 31, 0, 256) == XWM_ERR_SPACE, "GetProperty into 23 bytes is refused");
}

/* QueryExtension and the Composite requests, with the bytes written out by hand: "Composite" is nine bytes, padded to twelve,
   so the request is 20 bytes, five units. Composite's major opcode is 139 here (any value from 128 to 255 is one). */
static void xw_test_extensions(void)
{
        static const unsigned char query[20] = {0x62, 0x00, 0x05, 0x00, 0x09, 0x00, 0x00, 0x00,
                'C', 'o', 'm', 'p', 'o', 's', 'i', 't', 'e', 0x00, 0x00, 0x00};
        static const unsigned char version[12] = {0x8b, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00};
        static const unsigned char redirect[12] = {0x8b, 0x02, 0x03, 0x00, 0x01, 0x00, 0x20, 0x00, 0x01, 0x00, 0x00, 0x00};
        static const unsigned char reply[32] = {0x01, 0x00, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00,
                0x01, 0x8b, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
        unsigned char out[40], bad[32];
        struct xwm_extension_reply r;
        long n;

        n = xwm_enc_query_extension(out, sizeof out, (const unsigned char *)"Composite", 9);
        XWCHECK(n == 20 && xw_same(out, query, 20), "QueryExtension Composite: 20 bytes, length 5 units, the name at 8");
        XWCHECK(xwm_enc_query_extension(out, 19, (const unsigned char *)"Composite", 9) == XWM_ERR_SPACE, "QueryExtension into 19 bytes is refused");
        XWCHECK(xwm_enc_query_extension(out, sizeof out, 0, 0) == XWM_ERR_ARG, "QueryExtension with no name is refused");
        XWCHECK(xwm_enc_query_extension(out, sizeof out, (const unsigned char *)"Composite", 65) == XWM_ERR_CAP, "a QueryExtension name over the cap is refused");
        n = xwm_enc_composite_query_version(out, sizeof out, 139, 0, 4);
        XWCHECK(n == 12 && xw_same(out, version, 12), "Composite QueryVersion 0.4: minor 0, client 0.4, twelve bytes");
        XWCHECK(xwm_enc_composite_query_version(out, sizeof out, 127, 0, 4) == XWM_ERR_ARG, "a core opcode is no Composite opcode");
        XWCHECK(xwm_enc_composite_query_version(out, 11, 139, 0, 4) == XWM_ERR_SPACE, "QueryVersion into 11 bytes is refused");
        n = xwm_enc_composite_redirect_subwindows(out, sizeof out, 139, 0x00200001, XWM_REDIRECT_MANUAL);
        XWCHECK(n == 12 && xw_same(out, redirect, 12), "Composite RedirectSubwindows of the root, Manual: minor 2, the window at 4, update at 8");
        XWCHECK(xwm_enc_composite_redirect_subwindows(out, sizeof out, 139, 0x00200001, 2) == XWM_ERR_ARG, "an update that is neither Automatic nor Manual is refused");
        XWCHECK(xwm_enc_composite_redirect_subwindows(out, sizeof out, 0, 0x00200001, 1) == XWM_ERR_ARG, "a zero opcode is no extension opcode");
        XWCHECK(xwm_parse_extension_reply(reply, 32, &r) == XWM_OK && r.sequence == 5 && r.present == 1 && r.major_opcode == 139,
                "QueryExtension reply: present, the major opcode 139, the sequence 5");
        XWCHECK(xwm_parse_extension_reply(reply, 31, &r) == XWM_ERR_SHORT, "an extension reply of 31 bytes is short");
        memcpy(bad, reply, 32);
        bad[8] = 2;
        XWCHECK(xwm_parse_extension_reply(bad, 32, &r) == XWM_ERR_ARG, "a present flag that is neither 0 nor 1 is refused");
        {
                // X-Resource: QueryClientIds for one window's client, the pid only, and its reply read to the pid.
                static const unsigned char ids[16] = {140, 4, 4, 0, 1, 0, 0, 0, 0x05, 0x00, 0x40, 0x00, 2, 0, 0, 0};
                unsigned char rep[48] = {1, 0, 9, 0, 4, 0, 0, 0, 1, 0, 0, 0};
                unsigned int pid = 7;

                n = xwm_enc_xres_client_pid(out, sizeof out, 140, 0x00400005);
                XWCHECK(n == 16 && xw_same(out, ids, 16), "X-Resource QueryClientIds: minor 4, one spec, the window's client, LocalClientPID");
                XWCHECK(xwm_enc_xres_client_pid(out, sizeof out, 140, 0) == XWM_ERR_ARG, "QueryClientIds of resource 0 (every client) is refused");
                XWCHECK(xwm_enc_xres_client_pid(out, 15, 140, 5) == XWM_ERR_SPACE, "QueryClientIds into 15 bytes is refused");
                n = xwm_enc_xres_query_version(out, sizeof out, 140, 1, 2);
                XWCHECK(n == 8 && out[1] == 0 && out[4] == 1 && out[5] == 2 && out[2] == 2, "X-Resource QueryVersion 1.2: eight bytes");
                // One id: spec (client 0x00400000, mask 2), length 4, the pid 4242.
                rep[32 + 0] = 0x00, rep[32 + 2] = 0x40, rep[32 + 4] = 2, rep[32 + 8] = 4, rep[32 + 12] = 0x92, rep[32 + 13] = 0x10;
                XWCHECK(xwm_parse_xres_client_pid(rep, 48, &pid) == XWM_OK && pid == 4242, "QueryClientIds reply: the pid of a LocalClientPID id");
                rep[32 + 4] = 1;
                XWCHECK(xwm_parse_xres_client_pid(rep, 48, &pid) == XWM_OK && pid == 0, "an id of another mask is no pid");
                rep[32 + 4] = 2, rep[32 + 8] = 200;
                XWCHECK(xwm_parse_xres_client_pid(rep, 48, &pid) == XWM_ERR_LENGTH, "an id whose length is past the reply is refused");
                rep[32 + 8] = 4, rep[8] = 3;
                XWCHECK(xwm_parse_xres_client_pid(rep, 48, &pid) == XWM_ERR_LENGTH, "more ids than the reply holds is refused");
                rep[8] = 1;
                XWCHECK(xwm_parse_xres_client_pid(rep, 40, &pid) == XWM_ERR_LENGTH, "a reply shorter than its length is refused");
        }
        memcpy(bad, reply, 32);
        bad[4] = 1;
        XWCHECK(xwm_parse_extension_reply(bad, 32, &r) == XWM_ERR_LENGTH, "an extension reply whose length is not zero units is refused");
        memcpy(bad, reply, 32);
        bad[0] = 0;
        XWCHECK(xwm_parse_extension_reply(bad, 32, &r) == XWM_ERR_TYPE, "an error is not an extension reply");
}

static void xw_test_change_property(void)
{
        static const unsigned char ab[28] = {0x12, 0x00, 0x07, 0x00, 0x01, 0x00, 0x40, 0x00, 0x27, 0x00, 0x00, 0x00,
                0x1f, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 'a', 'b', 0x00, 0x00};
        static const unsigned char two32[32] = {0x12, 0x02, 0x08, 0x00, 0x01, 0x00, 0x40, 0x00, 0x27, 0x00, 0x00, 0x00,
                0x1f, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00,
                0x44, 0x33, 0x22, 0x11, 0x88, 0x77, 0x66, 0x55};
        unsigned char out[40];
        long n;

        n = xwm_enc_change_property(out, sizeof out, XWM_PROP_REPLACE, 0x00400001, 39, 31, 8, (const unsigned char *)"ab", 2);
        XWCHECK(n == 28 && xw_same(out, ab, 28), "ChangeProperty: two bytes of format 8, padded to 28 bytes, length 7 units");
        n = xwm_enc_change_property(out, sizeof out, XWM_PROP_APPEND, 0x00400001, 39, 31, 32,
                (const unsigned char *)"\x44\x33\x22\x11\x88\x77\x66\x55", 8);
        XWCHECK(n == 32 && xw_same(out, two32, 32), "ChangeProperty append of two 32-bit elements: the count is 2, the data little-endian");
        XWCHECK(xwm_enc_change_property(out, sizeof out, 0, 1, 39, 31, 16, (const unsigned char *)"abc", 3) == XWM_ERR_ARG, "format 16 with an odd byte count is refused");
        XWCHECK(xwm_enc_change_property(out, sizeof out, 0, 1, 39, 31, 7, (const unsigned char *)"ab", 2) == XWM_ERR_ARG, "a format of 7 is refused");
        XWCHECK(xwm_enc_change_property(out, sizeof out, 3, 1, 39, 31, 8, (const unsigned char *)"ab", 2) == XWM_ERR_ARG, "a mode of 3 is refused");
        XWCHECK(xwm_enc_change_property(out, sizeof out, 0, 1, 39, 31, 8, 0, 2) == XWM_ERR_ARG, "data with no bytes is refused");
        XWCHECK(xwm_enc_change_property(out, sizeof out, 0, 1, 39, 31, 8, 0, XWM_PROPERTY_MAX + 1) == XWM_ERR_CAP, "a property over the cap is refused");
        XWCHECK(xwm_enc_change_property(out, 27, 0, 1, 39, 31, 8, (const unsigned char *)"ab", 2) == XWM_ERR_SPACE, "ChangeProperty into 27 bytes is refused");
}

static void xw_test_select_events(void)
{
        static const unsigned char want[16] = {0x02, 0x00, 0x04, 0x00, 0x00, 0x01, 0x00, 0x00,
                0x00, 0x08, 0x00, 0x00, 0x00, 0x00, 0x5a, 0x00};
        unsigned char out[16];
        unsigned int wm_root = XWM_MASK_SUBSTRUCTURE_REDIRECT | XWM_MASK_SUBSTRUCTURE_NOTIFY | XWM_MASK_PROPERTY_CHANGE |
                XWM_MASK_STRUCTURE_NOTIFY;

        XWCHECK(wm_root == 0x5a0000u, "the root's mask is the four bits 20, 19, 22 and 17");
        XWCHECK(xwm_enc_select_events(out, sizeof out, 0x100, wm_root) == 16 && xw_same(out, want, 16),
                "ChangeWindowAttributes sets the event mask of the root");
        XWCHECK(xwm_enc_select_events(out, sizeof out, 0x100, 1u << 25) == XWM_ERR_ARG, "an event mask with a bit the protocol does not have is refused");
}

static void xw_test_create_window(void)
{
        static const unsigned char want[32] = {0x01, 0x00, 0x08, 0x00, 0x01, 0x00, 0x40, 0x00, 0x00, 0x01, 0x00, 0x00,
                0xff, 0xff, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00,
                0x00, 0x00, 0x00, 0x00};
        unsigned char out[32];

        XWCHECK(xwm_enc_create_window(out, sizeof out, 0, 0x00400001, 0x100, -1, 0, 1, 1, XWM_CLASS_INPUT_ONLY, 0) == 32
                && xw_same(out, want, 32), "CreateWindow of an InputOnly check window at -1,0, 1 by 1, with no values");
        XWCHECK(xwm_enc_create_window(out, sizeof out, 0, 0x00400001, 0x100, 0, 0, 0, 1, 1, 0) == XWM_ERR_ARG, "a window of width 0 is refused");
        XWCHECK(xwm_enc_create_window(out, sizeof out, 0, 0x00400001, 0x100, 40000, 0, 1, 1, 1, 0) == XWM_ERR_ARG, "an x out of INT16 is refused");
        XWCHECK(xwm_enc_create_window(out, sizeof out, 0, 0x00400001, 0x100, 0, 0, 1, 1, 3, 0) == XWM_ERR_ARG, "a window class of 3 is refused");
        XWCHECK(xwm_enc_create_window(out, 31, 0, 0x00400001, 0x100, 0, 0, 1, 1, 2, 0) == XWM_ERR_SPACE, "CreateWindow into 31 bytes is refused");
}

static void xw_test_window_ops(void)
{
        static const unsigned int ops[5] = {XWM_OP_MAP_WINDOW, XWM_OP_UNMAP_WINDOW, XWM_OP_DESTROY_WINDOW, XWM_OP_GET_GEOMETRY,
                XWM_OP_GET_WINDOW_ATTRIBUTES};
        static const unsigned char codes[5] = {8, 10, 4, 14, 3};
        unsigned char out[8];

        for (int i = 0; i < 5; i++)
        {
                unsigned char want[8] = {0, 0x00, 0x02, 0x00, 0x01, 0x00, 0x40, 0x00};

                want[0] = codes[i];
                XWCHECK(xwm_enc_window_op(out, sizeof out, ops[i], 0x00400001) == 8 && xw_same(out, want, 8),
                        "a window request of opcode %u is eight bytes: the opcode, length 2, the window", (unsigned)codes[i]);
        }
        XWCHECK(xwm_enc_window_op(out, sizeof out, 1, 0x00400001) == XWM_ERR_ARG, "CreateWindow is not a window request of eight bytes");
        XWCHECK(xwm_enc_window_op(out, 7, XWM_OP_MAP_WINDOW, 0x00400001) == XWM_ERR_SPACE, "a window request into 7 bytes is refused");
}

static void xw_test_configure_window(void)
{
        static const unsigned char want[24] = {0x0c, 0x00, 0x06, 0x00, 0x01, 0x00, 0x40, 0x00, 0x45, 0x00, 0x00, 0x00,
                0xfb, 0xff, 0xff, 0xff, 0x20, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
        static const unsigned char none[12] = {0x0c, 0x00, 0x03, 0x00, 0x01, 0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00};
        unsigned char out[32];
        struct xwm_configure c;

        c.mask = XWM_CONFIGURE_X | XWM_CONFIGURE_WIDTH | XWM_CONFIGURE_STACK_MODE;
        c.x = -5; c.y = 0; c.width = 800; c.height = 0; c.border = 0; c.sibling = 0; c.stack_mode = XWM_STACK_ABOVE;
        XWCHECK(xwm_enc_configure_window(out, sizeof out, 0x00400001, &c) == 24 && xw_same(out, want, 24),
                "ConfigureWindow of x, width and stack mode: the mask 0x45, the values in bit order, length 6 units");
        c.mask = 0;
        XWCHECK(xwm_enc_configure_window(out, sizeof out, 0x00400001, &c) == 12 && xw_same(out, none, 12),
                "ConfigureWindow with no fields is the window and an empty mask");
        c.mask = 0x80;
        XWCHECK(xwm_enc_configure_window(out, sizeof out, 0x00400001, &c) == XWM_ERR_ARG, "a mask bit the protocol does not have is refused");
        c.mask = XWM_CONFIGURE_WIDTH; c.width = 0;
        XWCHECK(xwm_enc_configure_window(out, sizeof out, 0x00400001, &c) == XWM_ERR_ARG, "a width of 0 is refused");
        c.mask = XWM_CONFIGURE_X; c.x = 40000;
        XWCHECK(xwm_enc_configure_window(out, sizeof out, 0x00400001, &c) == XWM_ERR_ARG, "an x out of INT16 is refused");
        c.mask = XWM_CONFIGURE_STACK_MODE; c.stack_mode = 5;
        XWCHECK(xwm_enc_configure_window(out, sizeof out, 0x00400001, &c) == XWM_ERR_ARG, "a stack mode of 5 is refused");
        c.mask = XWM_CONFIGURE_ALL; c.x = c.y = 1; c.width = c.height = 1; c.stack_mode = 0; c.border = 1; c.sibling = 1;
        XWCHECK(xwm_enc_configure_window(out, 39, 0x00400001, &c) == XWM_ERR_SPACE, "ConfigureWindow of seven fields into 39 bytes is refused");
        XWCHECK(xwm_enc_configure_window(out, sizeof out, 0x00400001, 0) == XWM_ERR_ARG, "ConfigureWindow with no values is refused");
}

static void xw_test_set_input_focus(void)
{
        static const unsigned char want[12] = {0x2a, 0x01, 0x03, 0x00, 0x01, 0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00};
        unsigned char out[12];

        XWCHECK(xwm_enc_set_input_focus(out, sizeof out, XWM_REVERT_POINTER_ROOT, 0x00400001, XWM_CURRENT_TIME) == 12
                && xw_same(out, want, 12), "SetInputFocus to the window, revert to the pointer's root, at CurrentTime");
        XWCHECK(xwm_enc_set_input_focus(out, sizeof out, 3, 0x00400001, 0) == XWM_ERR_ARG, "a revert-to of 3 is refused");
}

/* A ClientMessage of format 32, as the 32 bytes the event is (its type 33, its format 32, the window, the type atom, and the
   data words from byte 12). */
static void xw_test_client_message(unsigned char *ev)
{
        static const unsigned char want[32] = {0x21, 0x20, 0x00, 0x00, 0x01, 0x00, 0x40, 0x00, 0x00, 0x01, 0x00, 0x00,
                0x01, 0x01, 0x00, 0x00, 0x22, 0x22, 0x00, 0x00, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
        unsigned int data[2] = {0x101, 0x2222};
        long n;

        n = xwm_enc_client_message(ev, 32, 0x00400001, 0x100, data, 2);
        XWCHECK(n == 32 && xw_same(ev, want, 32), "a ClientMessage of format 32: window, type, and two data words, rest zero");
        XWCHECK(xwm_enc_client_message(ev, 32, 0x00400001, 0x100, data, 6) == XWM_ERR_ARG, "a ClientMessage with six data words is refused");
        XWCHECK(xwm_enc_client_message(ev, 31, 0x00400001, 0x100, data, 2) == XWM_ERR_SPACE, "a ClientMessage into 31 bytes is refused");
}

// The input events the window manager sends Steam's overlay window of a game on Wayland: a key, a motion, a focus.
static void xw_test_input_event(unsigned char *ev)
{
        static const unsigned char key[32] = {0x02, 23, 0, 0, 0x10, 0x27, 0, 0, 0x08, 0x04, 0, 0, 0x01, 0x00, 0x40, 0x00,
                0, 0, 0, 0, 0x0a, 0, 0x14, 0, 0x0a, 0, 0x14, 0, 0x01, 0x00, 0x01, 0};
        static const unsigned char focus[32] = {0x09, 0x03, 0, 0, 0x01, 0x00, 0x40, 0x00, 0x00};
        long n;

        n = xwm_enc_input_event(ev, 32, XWM_EVENT_KEY_PRESS, 23, 10000, 0x408, 0x00400001, 10, 20, 1);
        XWCHECK(n == 32 && xw_same(ev, key, 32), "a KeyPress: keycode, time, root, window, the place twice, Shift's state, same-screen");
        n = xwm_enc_input_event(ev, 32, XWM_EVENT_FOCUS_IN, 3, 10000, 0x408, 0x00400001, 10, 20, 0);
        XWCHECK(n == 32 && xw_same(ev, focus, 32), "a FocusIn: NotifyNonlinear, the window, NotifyNormal, the rest zero");
        n = xwm_enc_input_event(ev, 32, XWM_EVENT_MOTION_NOTIFY, 0, 1, 0x408, 0x00400001, -5, 70000, 0);
        XWCHECK(n == XWM_ERR_ARG, "a motion outside 16 bits is refused");
        XWCHECK(xwm_enc_input_event(ev, 32, XWM_EVENT_CLIENT_MESSAGE, 0, 0, 0, 1, 0, 0, 0) == XWM_ERR_ARG, "a ClientMessage is not an input event");
        XWCHECK(xwm_enc_input_event(ev, 31, XWM_EVENT_KEY_RELEASE, 23, 0, 0, 1, 0, 0, 0) == XWM_ERR_SPACE, "an input event into 31 bytes is refused");
}

static void xw_test_send_event(void)
{
        static const unsigned char want[44] = {0x19, 0x00, 0x0b, 0x00, 0x01, 0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00,
                0x21, 0x20, 0x00, 0x00, 0x01, 0x00, 0x40, 0x00, 0x00, 0x01, 0x00, 0x00, 0x01, 0x01, 0x00, 0x00,
                0x22, 0x22, 0x00, 0x00, 0, 0, 0, 0, 0, 0, 0, 0};
        unsigned char ev[32], out[44];
        long n;

        xw_test_client_message(ev);
        n = xwm_enc_send_event(out, sizeof out, 0, 0x00400001, 0, ev, 32);
        XWCHECK(n == 44 && xw_same(out, want, 44), "SendEvent of a WM_DELETE_WINDOW-shaped ClientMessage: 44 bytes, length 11 units, the event inside");
        XWCHECK(xwm_enc_send_event(out, sizeof out, 0, 0x00400001, 0, ev, 31) == XWM_ERR_ARG, "an event that is not 32 bytes is refused");
        ev[0] = 1;
        XWCHECK(xwm_enc_send_event(out, sizeof out, 0, 0x00400001, 0, ev, 32) == XWM_ERR_ARG, "a reply is not an event that can be sent");
        ev[0] = 0x63;
        XWCHECK(xwm_enc_send_event(out, sizeof out, 0, 0x00400001, 0, ev, 32) == XWM_ERR_ARG, "an event of type 99 is refused");
        ev[0] = 33;
        XWCHECK(xwm_enc_send_event(out, 43, 0, 0x00400001, 0, ev, 32) == XWM_ERR_SPACE, "SendEvent into 43 bytes is refused");
        xw_test_input_event(ev);
        {
                struct xwm_event parsed;

                n = xwm_enc_configure_notify(ev, sizeof ev, 0x00400001u, 240, 0, 1440, 1080, 0, 0);
                XWCHECK(n == 32 && xwm_parse_event(ev, 32, &parsed) == XWM_OK && parsed.kind == XWM_EV_CONFIGURE_NOTIFY &&
                        parsed.window == 0x00400001u && parsed.x == 240 && parsed.y == 0 && parsed.width == 1440 && parsed.height == 1080 && !parsed.override,
                        "ConfigureNotify synthetic event encodes and parses back with exact window and geometry");
                XWCHECK(xwm_enc_configure_notify(ev, 31, 0x00400001u, 0, 0, 100, 100, 0, 0) == XWM_ERR_SPACE,
                        "ConfigureNotify into 31 bytes is refused");
                XWCHECK(xwm_enc_configure_notify(ev, sizeof ev, 0, 0, 0, 100, 100, 0, 0) == XWM_ERR_ARG,
                        "ConfigureNotify with window 0 is refused");
        }
}

/* Every encoder into a buffer of exactly its size writes it, and into one byte less answers XWM_ERR_SPACE (the buffers are
   exact, so a write past what the encoder reported is a sanitizer report). */
static void xw_test_encoders_fit(void)
{
        unsigned char ev[32];
        unsigned int one[1] = {5};
        struct xwm_configure c;
        unsigned char *out;

        xw_test_client_message(ev);
        c.mask = XWM_CONFIGURE_Y;
        c.y = 9;
        out = xw_blank(12);
        XWCHECK(xwm_enc_setup_request(out, 12) == 12, "the setup request fits its exact size");
        XWCHECK(xwm_enc_setup_request(out, 11) == XWM_ERR_SPACE, "the setup request one byte short is refused");
        free(out);
        out = xw_blank(20);
        XWCHECK(xwm_enc_intern_atom(out, 20, 0, (const unsigned char *)"WM_PROTOCOLS", 12) == 20, "InternAtom fits its exact size");
        XWCHECK(xwm_enc_intern_atom(out, 19, 0, (const unsigned char *)"WM_PROTOCOLS", 12) == XWM_ERR_SPACE, "InternAtom one byte short is refused");
        free(out);
        out = xw_blank(16);
        XWCHECK(xwm_enc_configure_window(out, 16, 1, &c) == 16, "ConfigureWindow of one value fits its exact size");
        XWCHECK(xwm_enc_configure_window(out, 15, 1, &c) == XWM_ERR_SPACE, "ConfigureWindow of one value one byte short is refused");
        free(out);
        out = xw_blank(32);
        XWCHECK(xwm_enc_client_message(out, 32, 1, 2, one, 1) == 32, "a ClientMessage fits its exact size");
        XWCHECK(xwm_enc_client_message(out, 31, 1, 2, one, 1) == XWM_ERR_SPACE, "a ClientMessage one byte short is refused");
        free(out);
        out = xw_blank(44);
        XWCHECK(xwm_enc_send_event(out, 44, 0, 1, 0, ev, 32) == 44, "SendEvent fits its exact size");
        XWCHECK(xwm_enc_send_event(out, 43, 0, 1, 0, ev, 32) == XWM_ERR_SPACE, "SendEvent one byte short is refused");
        free(out);
        out = xw_blank(24);
        XWCHECK(xwm_enc_get_property(out, 24, 0, 1, 2, 3, 0, 4) == 24, "GetProperty fits its exact size");
        XWCHECK(xwm_enc_get_property(out, 23, 0, 1, 2, 3, 0, 4) == XWM_ERR_SPACE, "GetProperty one byte short is refused");
        free(out);
}

/* ---- the replies, errors and events: each parsed from a sample laid out by hand ---- */

static void xw_test_events(void)
{
        unsigned char e[32];
        struct xwm_event v;
        int r;

        xw_sample_map_request(e);
        r = xwm_parse_event(e, 32, &v);
        XWCHECK(r == XWM_OK && v.kind == XWM_EV_MAP_REQUEST && v.sequence == 0x0102 && v.parent == 0x5a1 && v.window == 0x400001 && v.synthetic == 0,
                "MapRequest: the parent, the window and the sequence");

        xw_sample_configure_request(e);
        r = xwm_parse_event(e, 32, &v);
        XWCHECK(r == XWM_OK && v.kind == XWM_EV_CONFIGURE_REQUEST && v.synthetic == 1 && v.detail == 1 && v.x == -3 && v.y == 10,
                "ConfigureRequest: a synthetic event with the stack mode and a negative x");
        XWCHECK(v.width == 640 && v.height == 480 && v.border == 0 && v.value_mask == 0x0f && v.window == 0x400001 && v.sibling == 0,
                "ConfigureRequest: the size, the border, the mask and the window");

        memset(e, 0, 32); e[0] = 19; xw_u(e, 4, 0x5a1, 4); xw_u(e, 8, 0x400003, 4); e[12] = 1;
        r = xwm_parse_event(e, 32, &v);
        XWCHECK(r == XWM_OK && v.kind == XWM_EV_MAP_NOTIFY && v.event == 0x5a1 && v.window == 0x400003 && v.override == 1,
                "MapNotify: the event, the window, and the override-redirect flag");

        memset(e, 0, 32); e[0] = 18; xw_u(e, 4, 0x5a1, 4); xw_u(e, 8, 0x400001, 4); e[12] = 1;
        r = xwm_parse_event(e, 32, &v);
        XWCHECK(r == XWM_OK && v.kind == XWM_EV_UNMAP_NOTIFY && v.event == 0x5a1 && v.window == 0x400001 && v.from_configure == 1,
                "UnmapNotify: the event, the window, and from-configure");

        memset(e, 0, 32); e[0] = 17; xw_u(e, 4, 0x5a1, 4); xw_u(e, 8, 0x400001, 4);
        r = xwm_parse_event(e, 32, &v);
        XWCHECK(r == XWM_OK && v.kind == XWM_EV_DESTROY_NOTIFY && v.window == 0x400001, "DestroyNotify: the window");

        memset(e, 0, 32); e[0] = 22; xw_u(e, 4, 0x5a1, 4); xw_u(e, 8, 0x400001, 4); xw_u(e, 20, 1920, 2); xw_u(e, 22, 1080, 2); e[26] = 1;
        r = xwm_parse_event(e, 32, &v);
        XWCHECK(r == XWM_OK && v.kind == XWM_EV_CONFIGURE_NOTIFY && v.width == 1920 && v.height == 1080 && v.override == 1,
                "ConfigureNotify: the size and the override-redirect flag");

        memset(e, 0, 32); e[0] = 28; xw_u(e, 4, 0x400001, 4); xw_u(e, 8, 39, 4); xw_u(e, 12, 0x1234, 4); e[16] = 1;
        r = xwm_parse_event(e, 32, &v);
        XWCHECK(r == XWM_OK && v.kind == XWM_EV_PROPERTY_NOTIFY && v.atom == 39 && v.time == 0x1234 && v.state == 1,
                "PropertyNotify: the window, the atom, the time, and the state Deleted");

        /* a ClientMessage carrying the surface serial as two words: the low word first */
        memset(e, 0, 32); e[0] = 33; e[1] = 32; xw_u(e, 4, 0x400001, 4); xw_u(e, 8, 0x200, 4); xw_u(e, 12, 7, 4); xw_u(e, 16, 2, 4);
        r = xwm_parse_event(e, 32, &v);
        XWCHECK(r == XWM_OK && v.kind == XWM_EV_CLIENT_MESSAGE && v.format == 32 && v.atom == 0x200 && v.data[0] == 7 && v.data[1] == 2,
                "ClientMessage of format 32: the serial's two words");
        XWCHECK(v.data[2] == 0 && v.data[3] == 0 && v.data[4] == 0, "the data words past the ones sent are zero");
        e[1] = 8; memcpy(e + 12, "abcd", 4);
        r = xwm_parse_event(e, 32, &v);
        XWCHECK(r == XWM_OK && v.format == 8 && v.data[0] == 0, "ClientMessage of format 8 is read as a format, its data not read as words");
        e[1] = 24;
        XWCHECK(xwm_parse_event(e, 32, &v) == XWM_ERR_ARG, "ClientMessage of format 24 is refused");

        memset(e, 0, 32); e[0] = 16; xw_u(e, 4, 0x5a1, 4); xw_u(e, 8, 0x400002, 4); xw_u(e, 12, 5, 2); xw_u(e, 14, 6, 2);
        xw_u(e, 16, 100, 2); xw_u(e, 18, 50, 2); e[22] = 1;
        r = xwm_parse_event(e, 32, &v);
        XWCHECK(r == XWM_OK && v.kind == XWM_EV_CREATE_NOTIFY && v.window == 0x400002 && v.x == 5 && v.width == 100 && v.override == 1,
                "CreateNotify: the window, its place and size, and the override-redirect flag");

        memset(e, 0, 32); e[0] = 9; e[1] = 0; xw_u(e, 4, 0x400001, 4);
        r = xwm_parse_event(e, 32, &v);
        XWCHECK(r == XWM_OK && v.kind == XWM_EV_FOCUS_IN && v.event == 0x400001, "FocusIn: the window it is for");
        memset(e, 0, 32); e[0] = 10; e[1] = 3; xw_u(e, 4, 0x400001, 4); e[8] = 1;
        r = xwm_parse_event(e, 32, &v);
        XWCHECK(r == XWM_OK && v.kind == XWM_EV_FOCUS_OUT && v.mode == 1 && v.detail == 3, "FocusOut: its mode and its detail");
        memset(e, 0, 32); e[0] = 7; xw_u(e, 4, 0x5a1, 4); xw_u(e, 12, 0x400001, 4);
        r = xwm_parse_event(e, 32, &v);
        XWCHECK(r == XWM_OK && v.kind == XWM_EV_ENTER && v.event == 0x400001, "EnterNotify: the event window");
        memset(e, 0, 32); e[0] = 8; xw_u(e, 12, 0x400001, 4); e[30] = 2;
        r = xwm_parse_event(e, 32, &v);
        XWCHECK(r == XWM_OK && v.kind == XWM_EV_LEAVE && v.mode == 2, "LeaveNotify: its mode");

        memset(e, 0, 32); e[0] = 12; xw_u(e, 4, 0x400001, 4);
        r = xwm_parse_event(e, 32, &v);
        XWCHECK(r == XWM_OK && v.kind == XWM_EV_IGNORED && v.code == 12, "Expose is known and ignored");
        memset(e, 0, 32); e[0] = 21; xw_u(e, 4, 0x5a1, 4); xw_u(e, 8, 0x400001, 4);
        r = xwm_parse_event(e, 32, &v);
        XWCHECK(r == XWM_OK && v.kind == XWM_EV_IGNORED && v.code == 21, "ReparentNotify is known and ignored");

        memset(e, 0, 32); e[0] = 2;
        r = xwm_parse_event(e, 32, &v);
        XWCHECK(r == XWM_OK && v.kind == XWM_EV_UNKNOWN && v.code == 2, "a KeyPress is an event this codec does not read: unknown, and it succeeds");
        memset(e, 0, 32); e[0] = 35; e[1] = 0; xw_u(e, 4, 0, 4);
        r = xwm_parse_event(e, 32, &v);
        XWCHECK(r == XWM_OK && v.kind == XWM_EV_UNKNOWN && v.code == 35, "a GenericEvent is unknown, framed by its own length");
        memset(e, 0, 32); e[0] = 0x80 | 99;
        r = xwm_parse_event(e, 32, &v);
        XWCHECK(r == XWM_OK && v.kind == XWM_EV_UNKNOWN && v.code == 99 && v.synthetic == 1, "a type of 99 sent by a client is unknown, with the send-event bit");

        memset(e, 0, 32); e[0] = 0; XWCHECK(xwm_parse_event(e, 32, &v) == XWM_ERR_TYPE, "an error is not an event");
        memset(e, 0, 32); e[0] = 1; XWCHECK(xwm_parse_event(e, 32, &v) == XWM_ERR_TYPE, "a reply is not an event");
        memset(e, 0, 32); e[0] = 0x80; XWCHECK(xwm_parse_event(e, 32, &v) == XWM_ERR_TYPE, "a send-event bit on type 0 is still an error, not an event");
        XWCHECK(xwm_message_type(e, 32) == 0 && xwm_message_type(e, 31) == XWM_ERR_SHORT, "the message type is the first byte without the send-event bit; 31 bytes is short");
}

static void xw_test_replies(void)
{
        unsigned char r[48];
        struct xwm_atom_reply a;
        struct xwm_property_reply p;
        struct xwm_geometry_reply g;
        struct xwm_attributes_reply t;
        struct xwm_error er;

        memset(r, 0, 48);
        r[0] = 1; xw_u(r, 2, 7, 2); xw_u(r, 8, 0x123, 4);
        XWCHECK(xwm_parse_atom_reply(r, 32, &a) == XWM_OK && a.sequence == 7 && a.atom == 0x123, "InternAtom reply: the sequence and the atom");
        XWCHECK(xwm_parse_atom_reply(r, 31, &a) == XWM_ERR_SHORT, "an InternAtom reply of 31 bytes is short");
        xw_u(r, 4, 1, 4);
        XWCHECK(xwm_parse_atom_reply(r, 40, &a) == XWM_ERR_LENGTH, "an InternAtom reply with a length of one unit is refused");
        xw_u(r, 4, 0, 4); r[0] = 0;
        XWCHECK(xwm_parse_atom_reply(r, 32, &a) == XWM_ERR_TYPE, "an error is not an InternAtom reply");

        /* GetProperty: STRING "hello", format 8, two units, padded */
        memset(r, 0, 48);
        r[0] = 1; r[1] = 8; xw_u(r, 2, 9, 2); xw_u(r, 4, 2, 4); xw_u(r, 8, 31, 4); xw_u(r, 12, 0, 4); xw_u(r, 16, 5, 4);
        memcpy(r + 32, "hello", 5);
        XWCHECK(xwm_parse_property_reply(r, 40, &p) == XWM_OK && p.sequence == 9 && p.format == 8 && p.type == 31 && p.elements == 5,
                "GetProperty reply: format 8, five elements, type STRING, sequence 9");
        XWCHECK(p.value == r + 32 && p.value_len == 5 && memcmp(p.value, "hello", 5) == 0, "the value is the five bytes at 32, which is inside the buffer");
        XWCHECK(xwm_parse_property_reply(r, 39, &p) == XWM_ERR_LENGTH, "a GetProperty reply cut one byte short of its value is refused");
        XWCHECK(xwm_parse_property_reply(r, 31, &p) == XWM_ERR_SHORT, "a GetProperty reply of 31 bytes is short");
        xw_u(r, 4, 0xffffffffu, 4);
        XWCHECK(xwm_parse_property_reply(r, 40, &p) == XWM_ERR_LENGTH, "a length field of 0xffffffff is refused, not read");
        xw_u(r, 4, 3, 4);
        XWCHECK(xwm_parse_property_reply(r, 44, &p) == XWM_ERR_LENGTH, "a length field that disagrees with the value's length is refused");
        xw_u(r, 4, 2, 4);
        xw_u(r, 16, 65537, 4);
        XWCHECK(xwm_parse_property_reply(r, 40, &p) == XWM_ERR_CAP, "a value of 65537 bytes is over the property cap");
        xw_u(r, 16, 5, 4); r[1] = 9;
        XWCHECK(xwm_parse_property_reply(r, 40, &p) == XWM_ERR_ARG, "a format of 9 is refused");
        r[1] = 0; xw_u(r, 4, 0, 4); xw_u(r, 16, 0, 4);
        XWCHECK(xwm_parse_property_reply(r, 32, &p) == XWM_OK && p.value_len == 0, "an empty property reply is 32 bytes and no value");
        r[1] = 0; xw_u(r, 16, 5, 4);
        XWCHECK(xwm_parse_property_reply(r, 40, &p) == XWM_ERR_ARG, "a format of 0 with elements is refused");
        r[1] = 16; xw_u(r, 16, 4, 4); xw_u(r, 4, 2, 4);
        XWCHECK(xwm_parse_property_reply(r, 48, &p) == XWM_OK && p.value_len == 8, "a format-16 reply of four elements is eight bytes");

        memset(r, 0, 48);
        r[0] = 1; r[1] = 24; xw_u(r, 2, 9, 2); xw_u(r, 8, 0x5a1, 4); xw_u(r, 12, 0xfffe, 2); xw_u(r, 14, 4, 2);
        xw_u(r, 16, 1920, 2); xw_u(r, 18, 1080, 2); xw_u(r, 20, 0, 2);
        XWCHECK(xwm_parse_geometry_reply(r, 32, &g) == XWM_OK && g.sequence == 9 && g.depth == 24 && g.root == 0x5a1, "GetGeometry reply: depth and root");
        XWCHECK(g.x == -2 && g.y == 4 && g.width == 1920 && g.height == 1080 && g.border == 0, "GetGeometry reply: a negative x, the size, the border");
        XWCHECK(xwm_parse_geometry_reply(r, 31, &g) == XWM_ERR_SHORT, "a GetGeometry reply of 31 bytes is short");

        memset(r, 0, 48);
        r[0] = 1; xw_u(r, 2, 10, 2); xw_u(r, 4, 3, 4); xw_u(r, 8, 0x21, 4); xw_u(r, 12, 1, 2); r[15] = 1; r[25] = 1; r[26] = 2;
        xw_u(r, 28, 0x20, 4); xw_u(r, 32, 0x40000, 4); xw_u(r, 36, 0x40000, 4);
        XWCHECK(xwm_parse_attributes_reply(r, 44, &t) == XWM_OK && t.sequence == 10 && t.visual == 0x21 && t.window_class == 1,
                "GetWindowAttributes reply: the visual and the class");
        XWCHECK(t.map_state == XWM_VIEWABLE && t.map_installed == 1 && t.colormap == 0x20 && t.your_mask == 0x40000 && t.win_gravity == 1,
                "GetWindowAttributes reply: viewable, its colormap, its mask, its gravity");
        XWCHECK(xwm_parse_attributes_reply(r, 43, &t) == XWM_ERR_LENGTH, "a GetWindowAttributes reply of 43 bytes is refused");
        xw_u(r, 4, 0, 4);
        XWCHECK(xwm_parse_attributes_reply(r, 44, &t) == XWM_ERR_LENGTH, "a GetWindowAttributes reply whose length is zero is refused");

        memset(r, 0, 48);
        r[0] = 0; r[1] = 3; xw_u(r, 2, 5, 2); xw_u(r, 4, 0x400001, 4); xw_u(r, 8, 0, 2); r[10] = 4;
        XWCHECK(xwm_parse_error(r, 32, &er) == XWM_OK && er.code == 3 && er.sequence == 5 && er.bad_value == 0x400001 && er.major == 4,
                "an error: the code, the sequence, the bad value and the major opcode");
        XWCHECK(xwm_parse_error(r, 31, &er) == XWM_ERR_SHORT, "an error of 31 bytes is short");
        XWCHECK(xwm_parse_error(r + 0, 32, 0) == XWM_ERR_ARG, "an error into no struct is refused");
        r[0] = 1;
        XWCHECK(xwm_parse_error(r, 32, &er) == XWM_ERR_TYPE, "a reply is not an error");
}

static void xw_test_setup(void)
{
        unsigned char b[144];
        unsigned char f[16];
        struct xwm_setup s;
        int r;

        xw_sample_setup(b);
        r = xwm_parse_setup(b, 144, &s);
        XWCHECK(r == XWM_OK && s.status == XWM_SETUP_SUCCESS && s.major == 11 && s.minor == 0 && s.release == 11, "setup: a success with protocol 11.0");
        XWCHECK(s.resource_base == 0x200000 && s.resource_mask == 0x1fffff && s.motion_buffer == 256, "setup: the resource-id base and mask, and the motion buffer");
        XWCHECK(s.vendor == 8 && s.max_request == 65535 && s.formats == 2 && s.screens == 1, "setup: the vendor length, the request size, the formats and the screens");
        XWCHECK(s.root == 0x5a1 && s.root_visual == 0x21 && s.root_depth == 24 && s.root_events == 0x001a0000, "setup: the first screen's root, visual, depth and event mask");
        XWCHECK(s.width == 1920 && s.height == 1080 && s.width_mm == 508 && s.height_mm == 285, "setup: the first screen's size");
        XWCHECK(s.min_keycode == 8 && s.max_keycode == 255 && s.scanline_unit == 32, "setup: the keycodes and the bitmap format");

        memset(f, 0, sizeof f);
        f[0] = 0; f[1] = 5; xw_u(f, 2, 11, 2); xw_u(f, 6, 2, 2); memcpy(f + 8, "nope!", 5);
        XWCHECK(xwm_parse_setup(f, 16, &s) == XWM_OK && s.status == XWM_SETUP_FAILED && s.reason == 5 && s.major == 11,
                "a refused setup: its reason's length, not its text");
        memset(f, 0, sizeof f);
        f[0] = 2; xw_u(f, 6, 1, 2);
        XWCHECK(xwm_parse_setup(f, 12, &s) == XWM_OK && s.status == XWM_SETUP_AUTHENTICATE, "an authenticate setup is reported as such");
        f[0] = 9;
        XWCHECK(xwm_parse_setup(f, 12, &s) == XWM_ERR_TYPE, "a setup status of 9 is refused");
}

static void xw_test_framing(void)
{
        unsigned char e[32], r[48];

        memset(e, 0, 32); e[0] = 22;
        XWCHECK(xwm_server_message_size(e, 32) == 32, "an event is 32 bytes");
        memset(r, 0, 48); r[0] = 1; xw_u(r, 4, 2, 4);
        XWCHECK(xwm_server_message_size(r, 48) == 40, "a reply of two units is 40 bytes");
        xw_u(r, 4, 16384, 4);
        XWCHECK(xwm_server_message_size(r, 48) == 32 + 65536, "a reply of the largest size the cap allows is 65568 bytes");
        xw_u(r, 4, 16385, 4);
        XWCHECK(xwm_server_message_size(r, 48) == XWM_ERR_CAP, "a reply one unit past the cap is refused");
        xw_u(r, 4, 0xffffffffu, 4);
        XWCHECK(xwm_server_message_size(r, 48) == XWM_ERR_CAP, "a reply that claims four gigabytes is refused at the frame");
        memset(r, 0, 48); r[0] = 35; xw_u(r, 4, 1, 4);
        XWCHECK(xwm_server_message_size(r, 48) == 36, "a GenericEvent is framed by its length");
        XWCHECK(xwm_server_message_size(e, 31) == XWM_ERR_SHORT, "framing needs the 32 bytes of a message");
}

/* ---- hostile input: every prefix of a sample, lengths that lie, caps, and types that are not the parser's ---- */

typedef int (*xw_parse_fn)(const unsigned char *, unsigned long);

/* Every prefix of the sample, shorter than it, is refused; the whole sample is accepted. */
static void xw_truncate_rows(const char *what, const unsigned char *whole, unsigned long n, xw_parse_fn parse)
{
        for (unsigned long cut = 0; cut < n; cut++)
        {
                unsigned char *p = xw_exact(whole, cut);
                int r = parse(p, cut);

                XWCHECK(r < 0, "%s cut to %lu of %lu bytes is refused", what, cut, n);
                free(p);
        }
        {
                unsigned char *p = xw_exact(whole, n);
                int r = parse(p, n);

                XWCHECK(r == XWM_OK, "%s whole is accepted (%d)", what, r);
                free(p);
        }
}

static void xw_test_truncation(void)
{
        unsigned char e[32], r[48], s[144];
        unsigned char f[16];

        xw_sample_map_request(e);
        xw_truncate_rows("MapRequest", e, 32, xw_p_event);
        xw_sample_configure_request(e);
        xw_truncate_rows("ConfigureRequest", e, 32, xw_p_event);
        xw_sample_map_request(e);
        e[0] = 33; e[1] = 32; xw_u(e, 8, 0x200, 4); xw_u(e, 12, 7, 4);
        xw_truncate_rows("ClientMessage", e, 32, xw_p_event);

        memset(r, 0, sizeof r);
        r[0] = 1; r[1] = 8; xw_u(r, 2, 9, 2); xw_u(r, 4, 2, 4); xw_u(r, 8, 31, 4); xw_u(r, 16, 5, 4); memcpy(r + 32, "hello", 5);
        xw_truncate_rows("GetProperty reply", r, 40, xw_p_property);
        memset(r, 0, sizeof r);
        r[0] = 1; xw_u(r, 2, 7, 2); xw_u(r, 8, 0x123, 4);
        xw_truncate_rows("InternAtom reply", r, 32, xw_p_atom);
        memset(r, 0, sizeof r);
        r[0] = 1; r[1] = 24; xw_u(r, 2, 9, 2); xw_u(r, 8, 0x5a1, 4); xw_u(r, 16, 1920, 2);
        xw_truncate_rows("GetGeometry reply", r, 32, xw_p_geometry);
        memset(r, 0, sizeof r);
        r[0] = 1; xw_u(r, 2, 10, 2); xw_u(r, 4, 3, 4); xw_u(r, 8, 0x21, 4);
        xw_truncate_rows("GetWindowAttributes reply", r, 44, xw_p_attributes);
        memset(r, 0, sizeof r);
        r[0] = 0; r[1] = 3; xw_u(r, 2, 5, 2); xw_u(r, 4, 0x400001, 4); r[10] = 4;
        xw_truncate_rows("error", r, 32, xw_p_error);

        xw_sample_setup(s);
        xw_truncate_rows("setup", s, 144, xw_p_setup);
        memset(f, 0, sizeof f);
        f[0] = 0; f[1] = 5; xw_u(f, 2, 11, 2); xw_u(f, 6, 2, 2); memcpy(f + 8, "nope!", 5);
        xw_truncate_rows("refused setup", f, 16, xw_p_setup);
}

/* Lengths that lie: a length field that claims more than the buffer holds, or more than the cap, is refused, and a length
   that disagrees with the structure is refused too. None of them is read past. */
static void xw_test_lengths(void)
{
        unsigned char r[40], s[144], e[32];
        struct xwm_property_reply p;
        struct xwm_setup st;

        memset(r, 0, sizeof r);
        r[0] = 1; r[1] = 8; xw_u(r, 4, 2, 4); xw_u(r, 16, 5, 4); memcpy(r + 32, "hello", 5);
        xw_u(r, 4, 0xffffffffu, 4);
        XWCHECK(xwm_server_message_size(r, 40) == XWM_ERR_CAP, "a reply whose length is 0xffffffff is not framed");
        XWCHECK(xwm_parse_property_reply(r, 40, &p) == XWM_ERR_LENGTH, "a GetProperty reply claiming 0xffffffff units is refused");
        xw_u(r, 4, 1000000, 4);
        XWCHECK(xwm_server_message_size(r, 40) == XWM_ERR_CAP, "a reply of a million units is not framed");
        xw_u(r, 4, 9, 4);
        XWCHECK(xwm_server_message_size(r, 40) == 32 + 36 && xwm_parse_property_reply(r, 40, &p) == XWM_ERR_LENGTH,
                "a reply of nine units in a buffer of 40 is framed as 68 and does not parse from 40");
        xw_u(r, 4, 2, 4);
        xw_u(r, 16, 1005, 4);
        XWCHECK(xwm_parse_property_reply(r, 40, &p) == XWM_ERR_LENGTH, "a value count that disagrees with the length is refused");

        xw_sample_setup(s);
        xw_u(s, 6, 0xffff, 2);
        XWCHECK(xwm_setup_size(s, 144) == XWM_ERR_CAP, "a setup whose length is 65535 units is over the setup cap");
        xw_u(s, 6, 34 + 10, 2);
        XWCHECK(xwm_parse_setup(s, 144, &st) == XWM_ERR_LENGTH, "a setup that claims ten more units than the buffer holds is refused");
        xw_sample_setup(s);
        xw_u(s, 6, 33, 2);
        XWCHECK(xwm_parse_setup(s, 144, &st) == XWM_ERR_LENGTH, "a setup that claims one unit less than its structure is refused");

        memset(e, 0, sizeof e);
        e[0] = 35; xw_u(e, 4, 0x7fffffffu, 4);
        XWCHECK(xwm_server_message_size(e, 32) == XWM_ERR_CAP, "a GenericEvent claiming two gigabytes is refused at the frame");
}

/* Caps: every count and every length the parsers take, one over its cap, is refused before anything is sized by it. */
static void xw_test_caps(void)
{
        unsigned char s[144], r[40];
        struct xwm_setup st;
        struct xwm_property_reply p;
        unsigned char *big = xw_blank(XWM_PROPERTY_MAX);
        unsigned char *out = xw_blank(XWM_PROPERTY_MAX + 24);
        unsigned char name[XWM_ATOM_NAME_MAX + 1];

        for (unsigned long i = 0; i < sizeof name; i++)
                name[i] = 'a';
        XWCHECK(xwm_enc_intern_atom(out, XWM_PROPERTY_MAX + 24, 0, name, XWM_ATOM_NAME_MAX + 1) == XWM_ERR_CAP,
                "an atom name of 257 bytes is over the atom cap");
        XWCHECK(xwm_enc_intern_atom(out, XWM_PROPERTY_MAX + 24, 0, name, XWM_ATOM_NAME_MAX) > 0,
                "an atom name of exactly the cap is written");
        XWCHECK(xwm_enc_change_property(out, XWM_PROPERTY_MAX + 24, 0, 1, 39, 31, 8, big, XWM_PROPERTY_MAX + 1) == XWM_ERR_CAP,
                "a ChangeProperty of 65537 bytes is over the property cap");
        XWCHECK(xwm_enc_change_property(out, XWM_PROPERTY_MAX + 24, 0, 1, 39, 31, 8, big, XWM_PROPERTY_MAX) == XWM_PROPERTY_MAX + 24,
                "a ChangeProperty of exactly the property cap is written");
        XWCHECK(xwm_enc_get_property(out, XWM_PROPERTY_MAX + 24, 0, 1, 39, 31, 0, XWM_PROPERTY_MAX / 4 + 1) == XWM_ERR_CAP,
                "a GetProperty of one unit past the cap is refused");
        free(big);
        free(out);

        memset(r, 0, sizeof r);
        r[0] = 1; r[1] = 32; xw_u(r, 4, 0, 4); xw_u(r, 16, XWM_PROPERTY_MAX / 4 + 1, 4);
        XWCHECK(xwm_parse_property_reply(r, 40, &p) == XWM_ERR_CAP, "a 32-bit reply of more elements than the cap holds is refused");

        xw_sample_setup(s);
        xw_u(s, 24, XWM_VENDOR_MAX + 1, 2);
        XWCHECK(xwm_parse_setup(s, 144, &st) == XWM_ERR_CAP, "a vendor string one byte over its cap is refused");
        xw_sample_setup(s);
        s[29] = XWM_FORMATS_MAX + 1;
        XWCHECK(xwm_parse_setup(s, 144, &st) == XWM_ERR_CAP, "a format count one over its cap is refused");
        xw_sample_setup(s);
        s[28] = XWM_SCREENS_MAX + 1;
        XWCHECK(xwm_parse_setup(s, 144, &st) == XWM_ERR_CAP, "a screen count one over its cap is refused");
        xw_sample_setup(s);
        s[28] = 0;
        XWCHECK(xwm_parse_setup(s, 144, &st) == XWM_ERR_ARG, "a server with no screen is refused");
        xw_sample_setup(s);
        s[103] = XWM_DEPTHS_MAX + 1;
        XWCHECK(xwm_parse_setup(s, 144, &st) == XWM_ERR_CAP, "a screen with one depth over the depth cap is refused");
        xw_sample_setup(s);
        xw_u(s, 106, 300, 2);
        XWCHECK(xwm_parse_setup(s, 144, &st) == XWM_ERR_LENGTH, "a depth with 300 visuals (over the old cap of 256, a GLX server's count) is not refused for its count");
        xw_sample_setup(s);
        xw_u(s, 106, XWM_VISUALS_MAX + 1, 2);
        XWCHECK(xwm_parse_setup(s, 144, &st) == XWM_ERR_CAP, "a depth with more visuals than the visual cap is refused");
        xw_sample_setup(s);
        xw_u(s, 16, 0, 4);
        XWCHECK(xwm_parse_setup(s, 144, &st) == XWM_ERR_ARG, "a resource-id mask of zero is refused");
        xw_sample_setup(s);
        xw_u(s, 12, 0x00200000 | 0x1, 4);
        XWCHECK(xwm_parse_setup(s, 144, &st) == XWM_ERR_ARG, "a resource-id base that overlaps the mask is refused");
        xw_sample_setup(s);
        xw_u(s, 2, 12, 2);
        XWCHECK(xwm_parse_setup(s, 144, &st) == XWM_ERR_TYPE, "a server of protocol major 12 is refused");
}

/* Types that are not the parser's: refused as types, and an unknown type is framed and reported, not parsed further. */
static void xw_test_types(void)
{
        unsigned char e[32], r[32], s[144];
        struct xwm_atom_reply a;
        struct xwm_error er;
        struct xwm_setup st;

        memset(e, 0, sizeof e);
        e[0] = 2; xw_u(e, 2, 3, 2);
        XWCHECK(xwm_parse_error(e, 32, &er) == XWM_ERR_TYPE, "a KeyPress is not an error");
        XWCHECK(xwm_parse_atom_reply(e, 32, &a) == XWM_ERR_TYPE, "a KeyPress is not an InternAtom reply");
        memset(r, 0, sizeof r);
        r[0] = 0x81;
        XWCHECK(xwm_parse_atom_reply(r, 32, &a) == XWM_ERR_TYPE, "a reply with the send-event bit is not a reply");
        xw_sample_setup(s);
        s[0] = 9;
        XWCHECK(xwm_parse_setup(s, 144, &st) == XWM_ERR_TYPE, "a setup of status 9 is refused");
        memset(e, 0, sizeof e);
        e[0] = 0x63;
        XWCHECK(xwm_message_type(e, 32) == 0x63, "a message of type 99 is typed 99");
        XWCHECK(xwm_server_message_size(e, 32) == 32, "a message of type 99 is framed as 32 bytes, and no more is read");
}

/* ---- fuzz: a fixed seed, 10000 rounds of random bytes into every parser and every encoder, and a stream of 10000 bytes
   walked the way the connection reads it. Nothing crashes, every answer is one of the codes, and every frame is bounded. */

static unsigned int xw_rng = 0x2545f491u;

static unsigned int xw_rand(void)
{
        unsigned int x = xw_rng;

        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        return xw_rng = x;
}

/* An encoder's answer is an error, or the bytes it wrote, which fit the buffer. */
static int xw_fits(long n, unsigned long cap)
{
        return n < 0 || (unsigned long)n <= cap;
}

static int xw_bounded(long r)
{
        return r == XWM_OK || (r < 0 && r >= XWM_ERR_TYPE) || r > 0;
}

static void xw_test_fuzz(void)
{
        static unsigned char stream[10000];
        unsigned long at = 0, frames = 0, unknown = 0, bad = 0, parsed = 0;
        unsigned int rounds = 10000;

        for (unsigned long i = 0; i < sizeof stream; i++)
                stream[i] = (unsigned char)xw_rand();
        while (at < sizeof stream)
        {
                unsigned long rest = sizeof stream - at;
                long size = xwm_server_message_size(stream + at, rest);
                unsigned char *p;
                long type;
                struct xwm_event ev;

                if (size <= 0 || (unsigned long)size > rest)
                        break;
                p = xw_exact(stream + at, (unsigned long)size);
                type = xwm_message_type(p, (unsigned long)size);
                if (type == XWM_ERROR)
                {
                        struct xwm_error er;
                        bad += !xw_bounded(xwm_parse_error(p, (unsigned long)size, &er));
                }
                else if (type == XWM_REPLY)
                {
                        struct xwm_atom_reply a;
                        struct xwm_property_reply pr;
                        struct xwm_geometry_reply g;
                        struct xwm_attributes_reply t;

                        bad += !xw_bounded(xwm_parse_atom_reply(p, (unsigned long)size, &a));
                        bad += !xw_bounded(xwm_parse_property_reply(p, (unsigned long)size, &pr));
                        bad += !xw_bounded(xwm_parse_geometry_reply(p, (unsigned long)size, &g));
                        bad += !xw_bounded(xwm_parse_attributes_reply(p, (unsigned long)size, &t));
                }
                else
                {
                        int r = xwm_parse_event(p, (unsigned long)size, &ev);

                        bad += !xw_bounded(r);
                        if (r == XWM_OK && ev.kind == XWM_EV_UNKNOWN)
                                unknown++;
                }
                parsed++;
                free(p);
                frames++;
                at += (unsigned long)size;
        }
        XWCHECK(bad == 0, "the stream of 10000 random bytes: every frame's parse answers a code (%lu bad)", bad);
        XWCHECK(at <= sizeof stream && frames > 0, "the stream is walked forward only, and not past its end");
        printf("CHECK_xwm_wire stream: %lu bytes, %lu frames parsed, %lu unknown, %lu bytes walked\n", (unsigned long)sizeof stream, frames, unknown, at);
        (void)parsed;

        bad = 0;
        for (unsigned int round = 0; round < rounds; round++)
        {
                unsigned long len = xw_rand() % 200;
                unsigned char *b = xw_blank(len ? len : 1);
                unsigned long i;
                unsigned int pick = xw_rand();

                for (i = 0; i < len; i++)
                        b[i] = (unsigned char)xw_rand();
                if (len >= 1 && (pick & 1))
                        b[0] = (unsigned char)(xw_rand() % 40 | ((pick & 2) ? 0x80 : 0));
                if (len >= 8 && (pick & 4))
                        xw_u(b, 4, xw_rand() % 5, 4);
                if (len >= 8 && (pick & 8))
                        xw_u(b, 6, xw_rand() % 9, 2);
                {
                        struct xwm_event ev;
                        struct xwm_error er;
                        struct xwm_atom_reply a;
                        struct xwm_property_reply pr;
                        struct xwm_geometry_reply g;
                        struct xwm_attributes_reply t;
                        struct xwm_setup st;
                        long size = xwm_server_message_size(b, len);
                        long setup = xwm_setup_size(b, len);

                        bad += !xw_bounded(xwm_parse_event(b, len, &ev));
                        bad += !xw_bounded(xwm_parse_error(b, len, &er));
                        bad += !xw_bounded(xwm_parse_atom_reply(b, len, &a));
                        bad += !xw_bounded(xwm_parse_property_reply(b, len, &pr));
                        bad += !xw_bounded(xwm_parse_geometry_reply(b, len, &g));
                        bad += !xw_bounded(xwm_parse_attributes_reply(b, len, &t));
                        bad += !xw_bounded(xwm_parse_setup(b, len, &st));
                        bad += !xw_bounded(size) || !xw_bounded(setup);
                        if (len > 0)
                        {
                                bad += !xw_bounded(xwm_parse_event(b, len - 1, &ev));
                                bad += !xw_bounded(xwm_parse_property_reply(b, len - 1, &pr));
                                bad += !xw_bounded(xwm_parse_setup(b, len - 1, &st));
                        }
                        if (xwm_parse_property_reply(b, len, &pr) == XWM_OK && (pr.value < b || pr.value + pr.value_len > b + len))
                                bad++;
                }
                free(b);
        }
        XWCHECK(bad == 0, "10000 rounds of random messages into every parser: every answer is a code, every value inside its buffer (%lu bad)", bad);

        bad = 0;
        for (unsigned int round = 0; round < rounds; round++)
        {
                unsigned long cap = xw_rand() % 64;
                unsigned char *out = xw_blank(cap ? cap : 1);
                unsigned long nlen = xw_rand() % 300;
                unsigned char *name = xw_blank(nlen ? nlen : 1);
                unsigned char ev[33];
                unsigned int data[7];
                struct xwm_configure c;
                unsigned int pick = xw_rand();
                long n;

                for (unsigned long i = 0; i < nlen; i++)
                        name[i] = (unsigned char)xw_rand();
                for (unsigned long i = 0; i < sizeof ev; i++)
                        ev[i] = (unsigned char)xw_rand();
                for (unsigned long i = 0; i < 7; i++)
                        data[i] = xw_rand();
                c.mask = xw_rand() & 0xff;
                c.x = (int)(xw_rand() % 70000) - 35000;
                c.y = (int)(xw_rand() % 70000) - 35000;
                c.width = xw_rand() % 70000;
                c.height = xw_rand() % 70000;
                c.border = xw_rand() % 70000;
                c.sibling = xw_rand();
                c.stack_mode = xw_rand() % 7;

                n = xwm_enc_setup_request(out, cap);
                bad += !xw_bounded(n) || !xw_fits(n, cap);
                n = xwm_enc_intern_atom(out, cap, pick & 1, name, nlen);
                bad += !xw_bounded(n) || !xw_fits(n, cap);
                n = xwm_enc_get_property(out, cap, pick & 2, xw_rand(), xw_rand(), xw_rand(), xw_rand(), xw_rand() % 20000);
                bad += !xw_bounded(n) || !xw_fits(n, cap);
                n = xwm_enc_change_property(out, cap, xw_rand() % 4, xw_rand(), xw_rand(), xw_rand(), xw_rand() % 40, name, nlen);
                bad += !xw_bounded(n) || !xw_fits(n, cap);
                n = xwm_enc_select_events(out, cap, xw_rand(), xw_rand());
                bad += !xw_bounded(n) || !xw_fits(n, cap);
                n = xwm_enc_create_window(out, cap, xw_rand() % 300, xw_rand(), xw_rand(), (int)(xw_rand() % 70000) - 35000,
                        (int)(xw_rand() % 70000) - 35000, xw_rand() % 70000, xw_rand() % 70000, xw_rand() % 4, xw_rand());
                bad += !xw_bounded(n) || !xw_fits(n, cap);
                n = xwm_enc_window_op(out, cap, xw_rand() % 50, xw_rand());
                bad += !xw_bounded(n) || !xw_fits(n, cap);
                n = xwm_enc_configure_window(out, cap, xw_rand(), &c);
                bad += !xw_bounded(n) || !xw_fits(n, cap);
                n = xwm_enc_set_input_focus(out, cap, xw_rand() % 4, xw_rand(), xw_rand());
                bad += !xw_bounded(n) || !xw_fits(n, cap);
                n = xwm_enc_send_event(out, cap, pick & 1, xw_rand(), xw_rand(), ev, sizeof ev - 1 - (pick & 1));
                bad += !xw_bounded(n) || !xw_fits(n, cap);
                n = xwm_enc_client_message(out, cap, xw_rand(), xw_rand(), data, xw_rand() % 8);
                bad += !xw_bounded(n) || !xw_fits(n, cap);
                free(name);
                free(out);
        }
        XWCHECK(bad == 0, "10000 rounds of random arguments into every encoder: every answer is a code, every write inside its buffer (%lu bad)", bad);
}

/*
        The selection bridge's wire (the clipboard, the primary selection and XDND between X and Wayland): the selection requests and
        events laid out by hand, XFIXES, the names of targets as Wayland types, Latin-1 and UTF-8 a piece at a time, INCR's framing,
        and XDND's messages and actions; then every new parser fed every prefix and random bytes, and every new encoder random arguments.
*/
static void xw_test_selection(void)
{
        unsigned char out[64], ev[32], *b;
        unsigned int values[2] = {1, 0x400000};
        struct xwm_event e;
        struct xwm_atom_name_reply an;
        struct xwm_property_reply pr;
        unsigned long size = 0, bad = 0;
        long n;

        {
                static const unsigned char want[16] = {22, 0, 4, 0, 0x01, 0x00, 0x40, 0x00, 0x45, 0, 0, 0, 0, 0, 0, 0};
                n = xwm_enc_set_selection_owner(out, sizeof out, 0x400001, 0x45, 0);
                XWCHECK(n == 16 && xw_same(out, want, 16), "SetSelectionOwner: opcode 22, 4 units, the owner, the selection, the time");
                XWCHECK(xwm_enc_set_selection_owner(out, sizeof out, 0, 0x45, 0) == 16, "SetSelectionOwner to none is a request");
                XWCHECK(xwm_enc_set_selection_owner(out, sizeof out, 1, 0, 0) == XWM_ERR_ARG, "SetSelectionOwner of no selection is refused");
                XWCHECK(xwm_enc_set_selection_owner(out, 15, 1, 2, 0) == XWM_ERR_SPACE, "SetSelectionOwner into 15 bytes is refused");
        }
        {
                static const unsigned char want[24] = {24, 0, 6, 0, 0x02, 0x00, 0x40, 0x00, 0x45, 0, 0, 0, 0x50, 0, 0, 0, 0x51, 0, 0, 0, 0x10, 0x20, 0, 0};
                n = xwm_enc_convert_selection(out, sizeof out, 0x400002, 0x45, 0x50, 0x51, 0x2010);
                XWCHECK(n == 24 && xw_same(out, want, 24), "ConvertSelection: opcode 24, 6 units, requestor, selection, target, property, time");
                XWCHECK(xwm_enc_convert_selection(out, sizeof out, 0x400002, 0x45, 0x50, 0, 0) == 24, "ConvertSelection into no property (obsolete clients) is a request");
                XWCHECK(xwm_enc_convert_selection(out, sizeof out, 0x400002, 0x45, 0, 0x51, 0) == XWM_ERR_ARG, "ConvertSelection of no target is refused");
                XWCHECK(xwm_enc_convert_selection(out, 23, 0x400002, 0x45, 0x50, 0x51, 0) == XWM_ERR_SPACE, "ConvertSelection into 23 bytes is refused");
        }
        {
                static const unsigned char want[12] = {19, 0, 3, 0, 0x02, 0x00, 0x40, 0x00, 0x51, 0, 0, 0};
                static const unsigned char name[8] = {17, 0, 2, 0, 0x51, 0, 0, 0};
                n = xwm_enc_delete_property(out, sizeof out, 0x400002, 0x51);
                XWCHECK(n == 12 && xw_same(out, want, 12), "DeleteProperty: opcode 19, 3 units, the window, the property");
                XWCHECK(xwm_enc_delete_property(out, sizeof out, 0x400002, 0) == XWM_ERR_ARG, "DeleteProperty of no property is refused");
                n = xwm_enc_get_atom_name(out, sizeof out, 0x51);
                XWCHECK(n == 8 && xw_same(out, name, 8), "GetAtomName: opcode 17, 2 units, the atom");
                XWCHECK(xwm_enc_get_atom_name(out, sizeof out, 0) == XWM_ERR_ARG, "GetAtomName of atom 0 is refused");
        }
        {
                static const unsigned char want[32] = {31, 0, 0, 0, 0x10, 0x20, 0, 0, 0x07, 0x00, 0x60, 0x00, 0x45, 0, 0, 0, 0x50, 0, 0, 0, 0x51, 0, 0, 0};
                n = xwm_enc_selection_notify(ev, sizeof ev, 0x2010, 0x600007, 0x45, 0x50, 0x51);
                XWCHECK(n == 32 && xw_same(ev, want, 32), "SelectionNotify as SendEvent carries it: type 31, time, requestor, selection, target, property");
                n = xwm_enc_send_event(out, sizeof out, 0, 0x600007, 0, ev, 32);
                XWCHECK(n == 44 && out[12] == 31, "a SelectionNotify is an event SendEvent takes");
                XWCHECK(xwm_enc_selection_notify(ev, 31, 0, 1, 2, 3, 0) == XWM_ERR_SPACE, "SelectionNotify into 31 bytes is refused");
                XWCHECK(xwm_enc_selection_notify(ev, 32, 0, 0, 2, 3, 0) == XWM_ERR_ARG, "SelectionNotify to no requestor is refused");
        }
        {
                static const unsigned char want[20] = {2, 0, 5, 0, 0x03, 0x00, 0x40, 0x00, 0x00, 0x0a, 0, 0, 1, 0, 0, 0, 0, 0, 0x40, 0};
                n = xwm_enc_change_window_attributes(out, sizeof out, 0x400003, XWM_CW_OVERRIDE_REDIRECT | XWM_CW_EVENT_MASK, values, 2);
                XWCHECK(n == 20 && xw_same(out, want, 20), "ChangeWindowAttributes: override-redirect then the event mask, in the order of their bits");
                XWCHECK(xwm_enc_change_window_attributes(out, sizeof out, 0x400003, XWM_CW_EVENT_MASK, values, 2) == XWM_ERR_ARG,
                        "ChangeWindowAttributes with more values than its mask has bits is refused");
                XWCHECK(xwm_enc_change_window_attributes(out, sizeof out, 0x400003, 0x8000u, values, 1) == XWM_ERR_ARG,
                        "ChangeWindowAttributes with a bit the protocol has not is refused");
        }
        {
                static const unsigned char version[12] = {140, 0, 3, 0, 5, 0, 0, 0, 0, 0, 0, 0};
                static const unsigned char select[16] = {140, 2, 4, 0, 0x01, 0x00, 0x40, 0x00, 0x45, 0, 0, 0, 7, 0, 0, 0};
                n = xwm_enc_xfixes_query_version(out, sizeof out, 140, 5, 0);
                XWCHECK(n == 12 && xw_same(out, version, 12), "XFIXES QueryVersion: the extension's opcode, minor 0, the version");
                n = xwm_enc_xfixes_select_selection_input(out, sizeof out, 140, 0x400001, 0x45, XWM_XFIXES_SELECTION_MASK);
                XWCHECK(n == 16 && xw_same(out, select, 16), "XFIXES SelectSelectionInput: minor 2, the window, the selection, the mask of three");
                XWCHECK(xwm_enc_xfixes_select_selection_input(out, sizeof out, 140, 0x400001, 0x45, 8) == XWM_ERR_ARG, "a mask bit XFIXES has not is refused");
                XWCHECK(xwm_enc_xfixes_query_version(out, sizeof out, 100, 5, 0) == XWM_ERR_ARG, "a core opcode is no extension's");
        }
        {
                static const unsigned char press[36] = {143, 2, 9, 0, 2, 9};
                static const unsigned char release[36] = {143, 2, 9, 0, 3, 9};
                n = xwm_enc_xtest_key(out, sizeof out, 143, XWM_XTEST_KEY_PRESS, 9);
                XWCHECK(n == 36 && xw_same(out, press, 36), "XTEST FakeInput: minor 2, nine units, a key press of keycode 9 (Escape), the rest none");
                n = xwm_enc_xtest_key(out, sizeof out, 143, XWM_XTEST_KEY_RELEASE, 9);
                XWCHECK(n == 36 && xw_same(out, release, 36), "and its release");
                XWCHECK(xwm_enc_xtest_key(out, sizeof out, 143, 4, 9) == XWM_ERR_ARG && xwm_enc_xtest_key(out, sizeof out, 143, 2, 7) == XWM_ERR_ARG &&
                            xwm_enc_xtest_key(out, sizeof out, 100, 2, 9) == XWM_ERR_ARG && xwm_enc_xtest_key(out, 35, 143, 2, 9) == XWM_ERR_SPACE,
                        "a button or motion, a keycode below 8, a core opcode and a buffer one short are refused");
        }
        {
                unsigned char m[32];
                memset(m, 0, 32);
                m[0] = 30; xw_u(m, 4, 0x1234, 4); xw_u(m, 8, 0x400001, 4); xw_u(m, 12, 0x600007, 4); xw_u(m, 16, 0x45, 4); xw_u(m, 20, 0x50, 4); xw_u(m, 24, 0x51, 4);
                b = xw_exact(m, 32);
                XWCHECK(xwm_parse_event(b, 32, &e) == XWM_OK && e.kind == XWM_EV_SELECTION_REQUEST && e.time == 0x1234 && e.owner == 0x400001 &&
                        e.requestor == 0x600007 && e.selection == 0x45 && e.target == 0x50 && e.property == 0x51, "SelectionRequest: its six fields");
                free(b);
                m[0] = 31; xw_u(m, 8, 0x600007, 4); xw_u(m, 12, 0x45, 4); xw_u(m, 16, 0x50, 4); xw_u(m, 20, 0, 4);
                b = xw_exact(m, 32);
                XWCHECK(xwm_parse_event(b, 32, &e) == XWM_OK && e.kind == XWM_EV_SELECTION_NOTIFY && e.requestor == 0x600007 && e.selection == 0x45 &&
                        e.target == 0x50 && e.property == 0, "SelectionNotify: a refusal is property 0");
                free(b);
                m[0] = 29 | 0x80; xw_u(m, 8, 0x400001, 4); xw_u(m, 12, 0x45, 4);
                b = xw_exact(m, 32);
                XWCHECK(xwm_parse_event(b, 32, &e) == XWM_OK && e.kind == XWM_EV_SELECTION_CLEAR && e.owner == 0x400001 && e.selection == 0x45 && e.synthetic,
                        "SelectionClear: the owner that lost it, the selection (a sent one says so)");
                free(b);
                memset(m, 0, 32);
                m[0] = 87; m[1] = 0; xw_u(m, 4, 0x400001, 4); xw_u(m, 8, 0x800004, 4); xw_u(m, 12, 0x45, 4); xw_u(m, 16, 77, 4); xw_u(m, 20, 66, 4);
                b = xw_exact(m, 32);
                XWCHECK(xwm_parse_xfixes_selection(b, 32, 87, &e) == XWM_OK && e.kind == XWM_EV_XFIXES_SELECTION && e.window == 0x400001 &&
                        e.owner == 0x800004 && e.selection == 0x45 && e.time == 77 && e.selection_time == 66 && e.detail == 0,
                        "XFIXES SelectionNotify: its subtype, window, owner, selection and both times");
                XWCHECK(xwm_parse_xfixes_selection(b, 32, 88, &e) == XWM_ERR_TYPE, "an event that is not the extension's first is not its SelectionNotify");
                XWCHECK(xwm_parse_xfixes_selection(b, 32, 30, &e) == XWM_ERR_TYPE, "a core event's code is no extension's");
                for (unsigned long k = 0; k < 32; k++)
                        bad += xwm_parse_xfixes_selection(b, k, 87, &e) != XWM_ERR_SHORT;
                XWCHECK(bad == 0, "every prefix of XFIXES SelectionNotify is short (%lu not)", bad);
                free(b);
        }
        {
                unsigned char m[44];
                memset(m, 0, sizeof m);
                m[0] = 1; xw_u(m, 2, 9, 2); xw_u(m, 4, 3, 4); xw_u(m, 8, 10, 2); memcpy(m + 32, "image/png\0\0\0", 12);
                b = xw_exact(m, 44);
                XWCHECK(xwm_parse_atom_name_reply(b, 44, &an) == XWM_OK && an.name_len == 10 && !memcmp(an.name, "image/png", 9) && an.sequence == 9,
                        "GetAtomName's reply: the name inside its padded length");
                XWCHECK(xwm_parse_atom_name_reply(b, 43, &an) == XWM_ERR_LENGTH, "a name that runs past the buffer is refused");
                free(b);
                xw_u(m, 4, 4, 4);
                b = xw_exact(m, 44);
                XWCHECK(xwm_parse_atom_name_reply(b, 44, &an) == XWM_ERR_LENGTH, "a length field that is not the padded name is refused");
                free(b);
                bad = 0;
                xw_u(m, 4, 3, 4);
                for (unsigned long k = 0; k < 44; k++)
                {
                        b = xw_exact(m, k);
                        int r = xwm_parse_atom_name_reply(b, k, &an);
                        bad += r == XWM_OK || !xw_bounded(r);
                        free(b);
                }
                XWCHECK(bad == 0, "every prefix of a GetAtomName reply is refused, never read past (%lu bad)", bad);
        }
        {
                static const struct
                {
                        const char *name;
                        int kind;
                        const char *mime;
                } rows[] = {
                    {"UTF8_STRING", XWM_TARGET_UTF8, XWM_TEXT_MIME}, {"TEXT", XWM_TARGET_UTF8, XWM_TEXT_MIME},
                    {"text/plain;charset=utf-8", XWM_TARGET_UTF8, XWM_TEXT_MIME}, {"text/plain;charset=UTF-8", XWM_TARGET_UTF8, XWM_TEXT_MIME},
                    {"text/plain", XWM_TARGET_UTF8, XWM_TEXT_MIME}, {"STRING", XWM_TARGET_LATIN1, XWM_TEXT_MIME},
                    {"image/png", XWM_TARGET_MIME, "image/png"}, {"text/uri-list", XWM_TARGET_MIME, "text/uri-list"},
                    {"application/x-moonwater-own", XWM_TARGET_MIME, "application/x-moonwater-own"}, {"TARGETS", XWM_TARGET_NONE, 0},
                    {"TIMESTAMP", XWM_TARGET_NONE, 0}, {"MULTIPLE", XWM_TARGET_NONE, 0}, {"SAVE_TARGETS", XWM_TARGET_NONE, 0},
                    {"COMPOUND_TEXT", XWM_TARGET_NONE, 0}, {"/etc", XWM_TARGET_NONE, 0}, {"image/", XWM_TARGET_NONE, 0}, {"", XWM_TARGET_NONE, 0}};
                unsigned char mime[64];

                bad = 0;
                for (unsigned long r = 0; r < sizeof rows / sizeof rows[0]; r++)
                {
                        unsigned long len = strlen(rows[r].name);
                        long got = xwm_target_mime((const unsigned char *)rows[r].name, len, mime, sizeof mime);

                        if (xwm_target_kind((const unsigned char *)rows[r].name, len) != rows[r].kind ||
                            (rows[r].mime ? got != (long)strlen(rows[r].mime) || strcmp((char *)mime, rows[r].mime) : got != 0))
                        {
                                bad++;
                                printf("    target %s: kind %d mime %ld\n", rows[r].name, xwm_target_kind((const unsigned char *)rows[r].name, len), got);
                        }
                }
                XWCHECK(bad == 0, "the targets as Wayland types: text of four names is UTF-8, STRING is Latin-1, a type's name passes through, the rest is none (%lu wrong)", bad);
                XWCHECK(xwm_target_mime((const unsigned char *)"image/png", 9, mime, 9) == XWM_ERR_SPACE, "a type with no room for its null is refused");
        }
        {
                static const unsigned char latin[4] = {'a', 0xe9, 0xff, 'z'};
                static const unsigned char utf8[6] = {'a', 0xc3, 0xa9, 0xc3, 0xbf, 'z'};
                static const unsigned char wide[7] = {0xe2, 0x82, 0xac, 0xc3, 0xa9, 0x80, 'q'};
                unsigned char got[16];
                struct xwm_utf8_cut cut = {0, 0};
                unsigned long put;

                put = xwm_latin1_utf8(latin, 4, got);
                XWCHECK(put == 6 && xw_same(got, utf8, 6), "Latin-1 to UTF-8: e9 and ff are two bytes each");
                put = xwm_utf8_latin1(&cut, utf8, 2, got);
                put += xwm_utf8_latin1(&cut, utf8 + 2, 4, got + put);
                XWCHECK(put == 4 && xw_same(got, latin, 4) && cut.need == 0, "UTF-8 to Latin-1 cut inside a character goes on in the next piece");
                cut.need = 0;
                put = xwm_utf8_latin1(&cut, wide, 7, got);
                XWCHECK(put == 4 && got[0] == '?' && got[1] == 0xe9 && got[2] == '?' && got[3] == 'q', "a character past ff and a stray byte are each one '?'");
        }
        {
                static const unsigned char want[28] = {18, 0, 7, 0, 0x07, 0x00, 0x60, 0x00, 0x51, 0, 0, 0, 0x52, 0, 0, 0, 32, 0, 0, 0, 1, 0, 0, 0, 0x00, 0x00, 0x10, 0x00};
                unsigned char reply[36];

                XWCHECK(!xwm_incr_needed(0) && !xwm_incr_needed(XWM_INCR_CHUNK) && xwm_incr_needed(XWM_INCR_CHUNK + 1) && xwm_incr_needed(XWM_INCR_SIZE_UNKNOWN),
                        "INCR: a value of one piece or less goes whole, one longer or of no known size in pieces");
                n = xwm_enc_incr(out, sizeof out, 0x600007, 0x51, 0x52, 1 << 20);
                XWCHECK(n == 28 && xw_same(out, want, 28), "INCR's start: ChangeProperty of type INCR, format 32, one element, the size (1 MiB)");
                n = xwm_enc_incr(out, sizeof out, 0x600007, 0x51, 0x52, 1ul << 40);
                XWCHECK(n == 28 && xwm_get32(out + 24) == 0xffffffffu, "a size past 32 bits is the largest the element holds (a lower bound)");
                n = xwm_enc_change_property(out, sizeof out, XWM_PROP_REPLACE, 0x600007, 0x51, 0x53, XWM_FORMAT_8, 0, 0);
                XWCHECK(n == 24 && xwm_get32(out + 20) == 0, "INCR's end: an empty piece is a ChangeProperty of no elements");
                memset(reply, 0, sizeof reply);
                reply[0] = 1; reply[1] = 32; xw_u(reply, 4, 1, 4); xw_u(reply, 8, 0x52, 4); xw_u(reply, 16, 1, 4); xw_u(reply, 32, 5000000, 4);
                b = xw_exact(reply, 36);
                XWCHECK(xwm_parse_property_reply(b, 36, &pr) == XWM_OK && xwm_property_incr(&pr, 0x52, &size) && size == 5000000,
                        "a property reply of type INCR starts a transfer of its size");
                XWCHECK(!xwm_property_incr(&pr, 0x53, &size), "a property of another type is the value itself");
                free(b);
                XWCHECK(xwm_incr_piece(100000, 131072) == XWM_INCR_CHUNK, "a piece is at most XWM_INCR_CHUNK");
                XWCHECK(xwm_incr_piece(1000, 131072) == 1000, "the last piece is what is left");
                XWCHECK(xwm_incr_piece(100000, 4120) == 4096, "a piece fits the queue's room less a request's head, in whole units");
                XWCHECK(xwm_incr_piece(100000, 24) == 0 && xwm_incr_piece(0, 131072) == 0, "no room is no piece, and nothing left is the empty end");
        }
        {
                unsigned int data[5], types[4] = {0x60, 0x61, 0x62, 0x63};

                xwm_dnd_enter(data, 0x400005, XWM_XDND_VERSION, types, 2);
                XWCHECK(data[0] == 0x400005 && data[1] == 0x05000000u && data[2] == 0x60 && data[3] == 0x61 && data[4] == 0,
                        "XdndEnter: the source, version 5 in the top byte, two types and a zero");
                xwm_dnd_enter(data, 0x400005, XWM_XDND_VERSION, types, 4);
                XWCHECK(data[1] == 0x05000001u && data[4] == 0x62, "XdndEnter of four types: bit 0 says the rest are in XdndTypeList, the first three in it");
                XWCHECK(xwm_dnd_place(300, 40) == (300u << 16 | 40u) && xwm_dnd_place(-5, 40000) == 32767u, "XdndPosition's place: x high, y low, held to the protocol's range");
                XWCHECK(xwm_dnd_action(0x70, 0x70, 0x71, 0x72) == XWM_DND_COPY && xwm_dnd_action(0x71, 0x70, 0x71, 0x72) == XWM_DND_MOVE &&
                        xwm_dnd_action(0x72, 0x70, 0x71, 0x72) == XWM_DND_ASK && xwm_dnd_action(0x73, 0x70, 0x71, 0x72) == 0 && xwm_dnd_action(0, 0, 0x71, 0x72) == 0,
                        "XdndActionCopy, Move and Ask are the Wayland drag's copy, move and ask; Private and none are no action");
                XWCHECK(xwm_dnd_action_atom(XWM_DND_MOVE, 0x70, 0x71, 0x72) == 0x71 && xwm_dnd_action_atom(XWM_DND_ASK, 0x70, 0x71, 0x72) == 0x72 &&
                        xwm_dnd_action_atom(XWM_DND_COPY | XWM_DND_MOVE, 0x70, 0x71, 0x72) == 0x70 && xwm_dnd_action_atom(0, 0x70, 0x71, 0x72) == 0x70,
                        "a Wayland action as X's: copy first of those it allows, and copy when it came to none");
        }
        bad = 0;
        for (unsigned int round = 0; round < 10000; round++)
        {
                unsigned long len = xw_rand() % 80, cap = xw_rand() % 64;
                unsigned char *in = xw_blank(len ? len : 1), *o = xw_blank(cap ? cap : 1);
                unsigned int vals[16];

                for (unsigned long i = 0; i < len; i++)
                        in[i] = (unsigned char)xw_rand();
                for (unsigned long i = 0; i < 16; i++)
                        vals[i] = xw_rand();
                bad += !xw_bounded(xwm_parse_xfixes_selection(in, len, 64 + xw_rand() % 80, &e));
                bad += !xw_bounded(xwm_parse_atom_name_reply(in, len, &an));
                if (xwm_parse_atom_name_reply(in, len, &an) == XWM_OK && an.name + an.name_len > in + len)
                        bad++;
                bad += !xw_bounded(xwm_parse_event(in, len, &e));
                n = xwm_enc_set_selection_owner(o, cap, xw_rand(), xw_rand() % 3, xw_rand());
                bad += !xw_bounded(n) || !xw_fits(n, cap);
                n = xwm_enc_convert_selection(o, cap, xw_rand() % 3, xw_rand() % 3, xw_rand() % 3, xw_rand(), xw_rand());
                bad += !xw_bounded(n) || !xw_fits(n, cap);
                n = xwm_enc_delete_property(o, cap, xw_rand() % 3, xw_rand() % 3);
                bad += !xw_bounded(n) || !xw_fits(n, cap);
                n = xwm_enc_get_atom_name(o, cap, xw_rand() % 3);
                bad += !xw_bounded(n) || !xw_fits(n, cap);
                n = xwm_enc_selection_notify(o, cap, xw_rand(), xw_rand() % 3, xw_rand() % 3, xw_rand(), xw_rand());
                bad += !xw_bounded(n) || !xw_fits(n, cap);
                n = xwm_enc_change_window_attributes(o, cap, xw_rand() % 3, xw_rand() & 0xffffu, vals, xw_rand() % 17);
                bad += !xw_bounded(n) || !xw_fits(n, cap);
                n = xwm_enc_xfixes_query_version(o, cap, xw_rand() % 300, xw_rand(), xw_rand());
                bad += !xw_bounded(n) || !xw_fits(n, cap);
                n = xwm_enc_xfixes_select_selection_input(o, cap, xw_rand() % 300, xw_rand() % 3, xw_rand() % 3, xw_rand() % 9);
                bad += !xw_bounded(n) || !xw_fits(n, cap);
                n = xwm_enc_incr(o, cap, xw_rand(), xw_rand(), xw_rand() % 3, xw_rand());
                bad += !xw_bounded(n) || !xw_fits(n, cap);
                n = xwm_enc_xtest_key(o, cap, xw_rand() % 300, xw_rand() % 5, xw_rand() % 300);
                bad += !xw_bounded(n) || !xw_fits(n, cap);
                n = xwm_target_mime(in, len, o, cap);
                bad += !xw_bounded(n) || (n > 0 && (unsigned long)n + 1 > cap);
                free(in);
                free(o);
        }
        XWCHECK(bad == 0, "10000 rounds of random bytes into the selection parsers and random arguments into its encoders: codes, inside every buffer (%lu bad)", bad);
}

int main(void)
{
        xw_test_selection();
        xw_test_setup_request();
        xw_test_intern_atom();
        xw_test_get_property();
        xw_test_change_property();
        xw_test_extensions();
        xw_test_select_events();
        xw_test_create_window();
        xw_test_window_ops();
        xw_test_configure_window();
        xw_test_set_input_focus();
        xw_test_send_event();
        xw_test_encoders_fit();
        xw_test_events();
        xw_test_replies();
        xw_test_setup();
        xw_test_framing();
        xw_test_truncation();
        xw_test_lengths();
        xw_test_caps();
        xw_test_types();
        xw_test_fuzz();
        printf("CHECK_xwm_wire: %u checks, %u failures\n", xw_checks, xw_failures);
        return xw_failures != 0;
}
#endif /* CHECK_xwm_wire */

