/*
        Moonwater Canvas userspace compositor and `surface` test client.
        Drives KMS/evdev (or headless -H), shared window table (window.c), taskbar/glass, GPU/CPU compose,
        direct/overlay scanout, and peer Wayland/XWM bridges across 13 included parts under compositor/.
        Runtime switches under /run/moonwater: display-off, present-early, present-wait, vrr-capable,
        lists-off, lists-wait, gpu-nouserptr, gpu-own-userptr, gpu-gfx10, gpu-round-off, gpu-cut-off,
        gpu-imports-all, touch-trace, layers-off, planes-off, cursor-plane-probe, game-hold-off, gpu-clocks-off.

        Dawn Larsson - Apache-2.0 license - github.com/dawnlarsson/moonwater
*/
#ifndef MOONWATER_CANVAS_COMPOSITOR
#define MOONWATER_CANVAS_COMPOSITOR

#if !defined(SHELL_NO_WAYLAND) && !defined(CONFIG_MOONWATER_WAYLAND)
#define CONFIG_MOONWATER_WAYLAND 1
#endif

/* ---- backend: the look on the CPU ---- */

#define COMPOSITOR_INK_DESKTOP    0x1e1e1eu
#define COMPOSITOR_INK_LINE_HELD  0xe4e4e4u
#define COMPOSITOR_INK_LINE_HOVER 0x9a9a9au
#define COMPOSITOR_LINE           1
enum
{
        COMPOSITOR_LIT_LEFT = 1u, COMPOSITOR_LIT_RIGHT = 2u, COMPOSITOR_LIT_TOP = 4u,
        COMPOSITOR_LIT_BOTTOM = 8u, COMPOSITOR_LIT_ALL = 16u,
        COMPOSITOR_LIT_REACH = 32, COMPOSITOR_BORDER_STANDARD = 2, COMPOSITOR_BORDER_MOST = 4,
        COMPOSITOR_SNAP_PERCENT = 40, COMPOSITOR_SNAP_PERCENT_MOST = 60,
        COMPOSITOR_SNAP_READ = 300, COMPOSITOR_SNAP_LINE = 200, COMPOSITOR_GRID_GAP = 4,
        COMPOSITOR_GRAB = 24, COMPOSITOR_MINIMIZE_IN = 3, COMPOSITOR_RADIUS_MOST = 64
};
#define COMPOSITOR_SNAP_FADE_NS (120 * UI_MS)
#define COMPOSITOR_GRID_TINT    0x40ffffffu
#define COMPOSITOR_GRID_TAKEN   0x80000000u

static unsigned int compositor_ground = COMPOSITOR_INK_DESKTOP;

/* Direct-registration open-addressed epoll reactor (~28 KiB BSS + 8 KiB atomic fd bitmap). */
enum
{
        COMPOSITOR_EPOLL_FDS = 65536, COMPOSITOR_EPOLL_CAP = 1024u, COMPOSITOR_EPOLL_BUCKS = 2048u,
        COMPOSITOR_EPOLL_TOMB = 0xffffu, COMPOSITOR_EPOLL_EVENTS = 0x00ffu, COMPOSITOR_EPOLL_REFUSED = 1u,
        COMPOSITOR_EPOLL_HIGH = 3000, COMPOSITOR_EPOLL_READY = 128
};

struct compositor_epoll_event
{
        unsigned int events;
        unsigned long long data;
}
#if defined(__x86_64__)
__attribute__((packed))
#endif
;

struct compositor_epoll_entry
{
        int fd;
        unsigned int slot, gen;
        unsigned short events, flags;
};

static struct
{
        int ep; // -1 uninitialized, -2 unavailable (ppoll fallback)
        unsigned int turn, active, tombs, whole, count, answered, forgets, forgets_seen;
        unsigned long long in[COMPOSITOR_EPOLL_FDS / 64];
        struct compositor_epoll_entry entries[COMPOSITOR_EPOLL_CAP];
        unsigned short hash[COMPOSITOR_EPOLL_BUCKS]; // 0 empty, idx+1 live, 0xffff tombstone
        unsigned int answers[COMPOSITOR_EPOLL_READY];
} compositor_epoll = {.ep = -1};

static inline int compositor_epoll_in_has(int fd) { return (int)((__atomic_load_n(&compositor_epoll.in[(unsigned int)fd >> 6], __ATOMIC_ACQUIRE) >> ((unsigned int)fd & 63u)) & 1ull); }
static inline void compositor_epoll_in_set(int fd) { __atomic_fetch_or(&compositor_epoll.in[(unsigned int)fd >> 6], 1ull << ((unsigned int)fd & 63u), __ATOMIC_RELEASE); }
static inline void compositor_epoll_in_clear(int fd) { __atomic_fetch_and(&compositor_epoll.in[(unsigned int)fd >> 6], ~(1ull << ((unsigned int)fd & 63u)), __ATOMIC_RELEASE); }
static inline unsigned int compositor_epoll_bucket(int fd) { return ((unsigned int)fd * 2654435761u) & (COMPOSITOR_EPOLL_BUCKS - 1u); }

static int compositor_epoll_find(int fd)
{
        unsigned int b = compositor_epoll_bucket(fd);
        for (unsigned int step = 0; step < COMPOSITOR_EPOLL_BUCKS; step++, b = (b + 1u) & (COMPOSITOR_EPOLL_BUCKS - 1u))
        {
                unsigned short v = compositor_epoll.hash[b];
                if (!v) return -1;
                if (v != COMPOSITOR_EPOLL_TOMB && compositor_epoll.entries[v - 1u].fd == fd) return (int)(v - 1u);
        }
        return -1;
}

static void compositor_epoll_hash_replace(int fd, unsigned short was, unsigned short val)
{
        unsigned int b = compositor_epoll_bucket(fd);
        for (unsigned int step = 0; step < COMPOSITOR_EPOLL_BUCKS; step++, b = (b + 1u) & (COMPOSITOR_EPOLL_BUCKS - 1u))
                if (compositor_epoll.hash[b] == was) { compositor_epoll.hash[b] = val; return; }
}

static void compositor_epoll_rehash(void)
{
        memory_zero(compositor_epoll.hash, sizeof(compositor_epoll.hash));
        compositor_epoll.tombs = 0;
        for (unsigned int i = 0; i < compositor_epoll.active; i++)
        {
                unsigned int b = compositor_epoll_bucket(compositor_epoll.entries[i].fd);
                while (compositor_epoll.hash[b]) b = (b + 1u) & (COMPOSITOR_EPOLL_BUCKS - 1u);
                compositor_epoll.hash[b] = (unsigned short)(i + 1u);
        }
}

static int compositor_epoll_insert(int fd)
{
        unsigned int b, first_tomb = COMPOSITOR_EPOLL_BUCKS, idx;
        if (compositor_epoll.active >= COMPOSITOR_EPOLL_CAP) return -1;
        if (compositor_epoll.active + compositor_epoll.tombs >= COMPOSITOR_EPOLL_BUCKS - COMPOSITOR_EPOLL_BUCKS / 4u)
                compositor_epoll_rehash();
        b = compositor_epoll_bucket(fd);
        for (unsigned int step = 0; step < COMPOSITOR_EPOLL_BUCKS; step++, b = (b + 1u) & (COMPOSITOR_EPOLL_BUCKS - 1u))
        {
                unsigned short v = compositor_epoll.hash[b];
                if (!v)
                {
                        unsigned int dst = first_tomb < COMPOSITOR_EPOLL_BUCKS ? first_tomb : b;
                        if (first_tomb < COMPOSITOR_EPOLL_BUCKS) compositor_epoll.tombs--;
                        idx = compositor_epoll.active++;
                        compositor_epoll.entries[idx] = (struct compositor_epoll_entry){.fd = fd};
                        compositor_epoll.hash[dst] = (unsigned short)(idx + 1u);
                        return (int)idx;
                }
                if (v == COMPOSITOR_EPOLL_TOMB && first_tomb == COMPOSITOR_EPOLL_BUCKS) first_tomb = b;
        }
        return -1;
}

static void compositor_epoll_unlink(unsigned int idx)
{
        int fd = compositor_epoll.entries[idx].fd;
        unsigned int last = --compositor_epoll.active;
        compositor_epoll_hash_replace(fd, (unsigned short)(idx + 1u), COMPOSITOR_EPOLL_TOMB);
        compositor_epoll.tombs++;
        if (idx != last)
        {
                int moved = compositor_epoll.entries[last].fd;
                compositor_epoll.entries[idx] = compositor_epoll.entries[last];
                compositor_epoll_hash_replace(moved, (unsigned short)(last + 1u), (unsigned short)(idx + 1u));
        }
}

static int compositor_epoll_init(void)
{
        if (compositor_epoll.ep == -1)
        {
                long made = system_call_1(syscall(epoll_create1), 0x80000);
                long high = made >= 0 ? (long)system_call_3(syscall(fcntl), (positive)made, 1030, COMPOSITOR_EPOLL_HIGH) : -1;
                if (high >= 0) system_call_1(syscall(close), (positive)made), made = high;
                compositor_epoll.ep = made >= 0 ? (int)made : -2;
        }
        return compositor_epoll.ep;
}

static int compositor_epoll_watch(int fd, unsigned short events, unsigned int slot)
{
        int idx, in_set;
        struct compositor_epoll_entry *e;
        if (compositor_epoll_init() < 0 || fd < 0 || fd >= COMPOSITOR_EPOLL_FDS || fd == compositor_epoll.ep) return -1;
        events &= COMPOSITOR_EPOLL_EVENTS;
        if ((idx = compositor_epoll_find(fd)) < 0 && (idx = compositor_epoll_insert(fd)) < 0) return -1;
        e = &compositor_epoll.entries[idx], in_set = compositor_epoll_in_has(fd);
        if ((e->flags & COMPOSITOR_EPOLL_REFUSED) && e->events == events) return -1;
        if (!in_set || e->events != events)
        {
                struct compositor_epoll_event change = {events, (unsigned long long)(unsigned int)fd};
                long done = (long)system_call_4(syscall(epoll_ctl), (positive)compositor_epoll.ep, in_set ? 3u : 1u, (positive)fd, (positive)&change);
                if (done == -17 || done == -2)
                        done = (long)system_call_4(syscall(epoll_ctl), (positive)compositor_epoll.ep, done == -17 ? 3u : 1u, (positive)fd, (positive)&change);
                if (done < 0)
                {
                        if (done == -9) compositor_epoll_unlink((unsigned int)idx);
                        else e->flags = COMPOSITOR_EPOLL_REFUSED, e->events = events;
                        return -1;
                }
                compositor_epoll_in_set(fd);
                e->events = events, e->flags = 0;
        }
        e->slot = slot, e->gen = compositor_epoll.turn;
        return 0;
}

static void compositor_epoll_drop(int fd)
{
        int idx;
        if (fd < 0 || fd >= COMPOSITOR_EPOLL_FDS || compositor_epoll.ep < 0) return;
        if (compositor_epoll_in_has(fd))
                system_call_4(syscall(epoll_ctl), (positive)compositor_epoll.ep, 2, (positive)fd, 0), compositor_epoll_in_clear(fd);
        if ((idx = compositor_epoll_find(fd)) >= 0) compositor_epoll_unlink((unsigned int)idx);
}

static long compositor_epoll_wait(struct compositor_epoll_event *ready, unsigned int max, const long *wait)
{
        long got;
        if (compositor_epoll.ep < 0) return -38;
        got = (long)system_call_6(syscall(epoll_pwait2), (positive)compositor_epoll.ep, (positive)ready, max, (positive)wait, 0, 0);
        if (got == -38) compositor_epoll.ep = -2, compositor_epoll.whole = 0;
        return got;
}

static void compositor_epoll_forget(long fd)
{
        if (fd < 0 || fd >= COMPOSITOR_EPOLL_FDS || compositor_epoll.ep < 0 || !compositor_epoll_in_has((int)fd)) return;
        system_call_4(syscall(epoll_ctl), (positive)compositor_epoll.ep, 2, (positive)fd, 0);
        compositor_epoll_in_clear((int)fd);
        __atomic_add_fetch(&compositor_epoll.forgets, 1, __ATOMIC_RELEASE);
}

static positive compositor_epoll_close(long fd)
{
        compositor_epoll_forget(fd);
        return system_call_1(syscall(close), (positive)fd);
}
#undef system_close
#define system_close(handle) compositor_epoll_close((long)(handle))

#define UI_SHARED_ONLY
#include "ui/ui.c"
#undef UI_SHARED_ONLY
#include "../audio/state.c"
#include "../audio/control.c"
#if !defined(SHELL_NO_GPU) && !defined(MOONWATER_BUILD_TOOL)
#include "gpu.c"
#else
static unsigned int gpu_epoch;
static int gpu_ready(void) { return 0; }
static int gpu_can_import(void) { return 0; }
static positive gpu_import_fd(int fd, positive bytes) { (void)fd, (void)bytes; return 0; }
static void gpu_release_import(positive at) { (void)at; }
static void gpu_release_import_gen(positive at, unsigned int gen) { (void)at, (void)gen; }
static unsigned int gpu_import_gen(positive at) { (void)at; return 0; }
static void gpu_forget(const void *addr, positive bytes) { (void)addr, (void)bytes; }
static b32 gpu_idle(void) { return true; }
static positive gpu_va_bytes(const void *p, positive bytes) { (void)p, (void)bytes; return 0; }
static positive gpu_va_written(const void *p, positive bytes) { (void)p, (void)bytes; return 0; }
static b32 gpu_map_as(const void *a, positive s, b32 w) { (void)a, (void)s, (void)w; return false; }
#define gpu_map(addr, size) gpu_map_as(addr, size, 0)
#define gpu_map_written(addr, size) gpu_map_as(addr, size, 1)
static b32 gpu_blit(positive s, unsigned int sp, positive d, unsigned int dp, positive t, unsigned int op, unsigned int w, unsigned int h)
{
        (void)s, (void)sp, (void)d, (void)dp, (void)t, (void)op, (void)w, (void)h;
        return false;
}
#endif

static struct taskbar_view compositor_taskbar_view(void);
static unsigned int compositor_line_lit(void), compositor_line_unlit(void), compositor_ink_shadow(void);
static unsigned int compositor_ink_selection(void), compositor_snap_ink(void), compositor_tab_target(b32 lit), compositor_ink_role(int role);
static void compositor_preview(int row);
static unsigned int *compositor_composed_make(unsigned int **pixels, unsigned long *bytes, int *fd, unsigned long want);
static void compositor_composed_drop(unsigned int **pixels, unsigned long *bytes, int *fd);
static positive compositor_composed_va(unsigned int *pixels, unsigned long bytes);

struct compositor_target
{
        unsigned int *pixels;
        unsigned long pitch;
        int width, height, x1, y1, x2, y2, ox, oy;
        unsigned int *scratch;
        int out;
};

unsigned long compositor_out_reads;

static inline void compositor_reading(const struct compositor_target *t)
{
        if (t->out) __atomic_fetch_add(&compositor_out_reads, 1, __ATOMIC_RELAXED);
}

// Backend parts (cut out by test/differential.py for CHECK_canvas_cells), followed by compositor state and the 9 runtime parts.
#include "compositor/compose.c" // the look on the processor and the card's frames: windows, lines, corners, the panel's tabs, the scene and its plan
#include "compositor/keymap.c" // what a key means: the layouts, dead keys and the held modifiers
#include "compositor/pointer.c" // what a hand means: the gain, the repeat, the shake, the pointer's shapes and its picture
#include "compositor/outputs.c" // the screens one desktop spans: a point and a window kept on them, the snap's rules, a screen's cells

/* Kernel DRM & evdev uapi structs and ioctls (identical across x86_64/arm64/riscv64). */
#define COMPOSITOR_MODE_USERDEF 32u
struct compositor_card_res { unsigned long fbs, crtcs, connectors, encoders; unsigned int fb_count, crtc_count, connector_count, encoder_count, min_width, max_width, min_height, max_height; };
struct compositor_mode { unsigned int clock; unsigned short hdisplay, hsync_start, hsync_end, htotal, hskew, vdisplay, vsync_start, vsync_end, vtotal, vscan; unsigned int vrefresh, flags, type; char name[32]; };
struct compositor_connector { unsigned long encoders, modes, props, prop_values; unsigned int mode_count, prop_count, encoder_count, encoder_id, connector_id, connector_type, connector_type_id, connection, mm_width, mm_height, subpixel, pad; };
struct compositor_encoder { unsigned int encoder_id, encoder_type, crtc_id, possible_crtcs, possible_clones; };
struct compositor_crtc { unsigned long connectors; unsigned int connector_count, crtc_id, fb_id, x, y, gamma_size, mode_valid; struct compositor_mode mode; };
struct compositor_dumb { unsigned int height, width, bpp, flags, handle, pitch; unsigned long size; };
struct compositor_map_dumb { unsigned int handle, pad; unsigned long offset; };
struct compositor_fb { unsigned int fb_id, width, height, pitch, bpp, depth, handle; };
struct compositor_dirty { unsigned int fb_id, flags, colour, clip_count; unsigned long clips; };
struct compositor_version { int major, minor, patchlevel; unsigned long name_length; char *name; unsigned long date_length; char *date; unsigned long description_length; char *description; };
struct compositor_cursor { unsigned int flags, crtc_id; int x, y; unsigned int width, height, handle; };

_Static_assert(sizeof(struct compositor_version) == 64, "drm_version");
_Static_assert(sizeof(struct compositor_card_res) == 64, "drm_mode_card_res");
_Static_assert(sizeof(struct compositor_mode) == 68, "drm_mode_modeinfo");
_Static_assert(sizeof(struct compositor_connector) == 80, "drm_mode_get_connector");
_Static_assert(sizeof(struct compositor_crtc) == 104, "drm_mode_crtc");
_Static_assert(sizeof(struct compositor_dumb) == 32, "drm_mode_create_dumb");
_Static_assert(sizeof(struct compositor_fb) == 28, "drm_mode_fb_cmd");
_Static_assert(sizeof(struct compositor_dirty) == 24, "drm_mode_fb_dirty_cmd");
_Static_assert(sizeof(struct compositor_cursor) == 28, "drm_mode_cursor");

#define COMPOSITOR_DRM(nr, size) (0xc0000000u | (size) << 16 | 0x6400u | (nr))
#define COMPOSITOR_VERSION          COMPOSITOR_DRM(0x00u, 64u)
#define COMPOSITOR_SET_MASTER       0x641eu
#define COMPOSITOR_GETRESOURCES     COMPOSITOR_DRM(0xa0u, 64u)
#define COMPOSITOR_GETCRTC          COMPOSITOR_DRM(0xa1u, 104u)
#define COMPOSITOR_SETCRTC          COMPOSITOR_DRM(0xa2u, 104u)
#define COMPOSITOR_CURSOR           COMPOSITOR_DRM(0xa3u, 28u)
#define COMPOSITOR_GETENCODER       COMPOSITOR_DRM(0xa6u, 20u)
#define COMPOSITOR_GETCONNECTOR     COMPOSITOR_DRM(0xa7u, 80u)
#define COMPOSITOR_ADDFB            COMPOSITOR_DRM(0xaeu, 28u)
#define COMPOSITOR_RMFB             COMPOSITOR_DRM(0xafu, 4u)
#define COMPOSITOR_PAGE_FLIP        COMPOSITOR_DRM(0xb0u, 24u)
#define COMPOSITOR_DIRTYFB          COMPOSITOR_DRM(0xb1u, 24u)
#define COMPOSITOR_CREATE_DUMB      COMPOSITOR_DRM(0xb2u, 32u)
#define COMPOSITOR_MAP_DUMB         COMPOSITOR_DRM(0xb3u, 16u)
#define COMPOSITOR_ADDFB2           COMPOSITOR_DRM(0xb8u, 104u)
#define COMPOSITOR_PRIME_FD_TO_HANDLE COMPOSITOR_DRM(0x2eu, 12u)
#define COMPOSITOR_GEM_CLOSE        0x40086409u
#define COMPOSITOR_UDMABUF_CREATE   0x40187542u
#define COMPOSITOR_XRGB8888         0x34325258u
#define COMPOSITOR_FLIP_EVENT       1u
#define COMPOSITOR_FLIP_ASYNC       2u
#define COMPOSITOR_EVENT_FLIP_COMPLETE 2u

struct compositor_input_event { long seconds, microseconds; unsigned short type, code; int value; };
struct compositor_report_event { unsigned long long stamp; unsigned short type, code; int value; };
_Static_assert(sizeof(struct compositor_input_event) == 24 && sizeof(struct compositor_report_event) == 16, "what evdev_decode reads and writes");
struct compositor_absinfo { int value, minimum, maximum, fuzz, flat, resolution; };

#define COMPOSITOR_EVIOCGBIT(type, bytes) (0x80000000u | (bytes) << 16 | 0x4500u | (0x20u + (type)))
#define COMPOSITOR_EVIOCGABS(axis)        (0x80184540u + (axis))
#define COMPOSITOR_EVIOCGID               0x80084502u
#define COMPOSITOR_EVIOCGNAME(bytes)      (0x80000000u | (bytes) << 16 | 0x4506u)
#define COMPOSITOR_EVIOCGPROP(bytes)      (0x80000000u | (bytes) << 16 | 0x4509u)
#define COMPOSITOR_EVIOCSCLOCKID          0x400445a0u
#define COMPOSITOR_EVIOCGREP              0x80084503u
#define COMPOSITOR_EVIOCSREP              0x40084503u

enum
{
        COMPOSITOR_SYN_DROPPED = 3u, COMPOSITOR_BTN_TOOL_PEN = 0x140u, COMPOSITOR_BTN_TOOL_FINGER = 0x145u,
        COMPOSITOR_BTN_TOOL_QUINTTAP = 0x148u, COMPOSITOR_BTN_TOUCH = 0x14au,
        COMPOSITOR_BTN_TOOL_DOUBLETAP = 0x14du, COMPOSITOR_BTN_TOOL_TRIPLETAP = 0x14eu, COMPOSITOR_BTN_TOOL_QUADTAP = 0x14fu,
        COMPOSITOR_ABS_MT_SLOT = 0x2fu, COMPOSITOR_ABS_MT_POSITION_X = 0x35u, COMPOSITOR_ABS_MT_POSITION_Y = 0x36u,
        COMPOSITOR_ABS_MT_TRACKING_ID = 0x39u,
        COMPOSITOR_EV_SYN = 0u, COMPOSITOR_EV_KEY = 1u, COMPOSITOR_EV_REL = 2u, COMPOSITOR_EV_ABS = 3u, COMPOSITOR_EV_SW = 5u,
        COMPOSITOR_REL_X = 0u, COMPOSITOR_REL_Y = 1u, COMPOSITOR_REL_HWHEEL = 6u, COMPOSITOR_REL_WHEEL = 8u,
        COMPOSITOR_REL_WHEEL_HI = 11u, COMPOSITOR_REL_HWHEEL_HI = 12u,
        COMPOSITOR_BTN_LEFT = 0x110u, COMPOSITOR_BTN_RIGHT = 0x111u, COMPOSITOR_BTN_MIDDLE = 0x112u, COMPOSITOR_BTN_TASK = 0x117u,
        COMPOSITOR_KEY_A = 30u, COMPOSITOR_KEY_POWER = 116u,
        COMPOSITOR_PAD_COUNTS = 20, COMPOSITOR_PAD_WHEEL = 12, COMPOSITOR_PAD_WIDTH_MM = 100,
        COMPOSITOR_WHEEL_STREAMS = 3, COMPOSITOR_GAIN_BASE = 2048, COMPOSITOR_STREAM_HIST = 6,
        COMPOSITOR_GAIN_TURN = (256 * 100 * UI_NOTCH)
};
#define COMPOSITOR_WHEEL_MEMORY_NS 3000000000ull

struct compositor_pad
{
        int next_x, next_y, at_x, at_y, moving, fingers, touching, turned;
        int per_mm_x, per_mm_y, left_x, left_y;
        unsigned int clicked;
        unsigned long long gain_at;
        int gain_left_x, gain_left_y, slot;
        int mt_down[UI_TOUCH_SLOTS], mt_x[UI_TOUCH_SLOTS], mt_y[UI_TOUCH_SLOTS];
        int pinch_count, pinch_ref, pinching, pinch_scale;
        int swipe_fingers, swipe_dx, swipe_dy, swipe_fired;
};

struct compositor_touch_screen
{
        int slot, slots, range[4], dropped;
        int id[UI_TOUCH_SLOTS], was[UI_TOUCH_SLOTS], x[UI_TOUCH_SLOTS], y[UI_TOUCH_SLOTS];
};

struct compositor_touch
{
        struct ui_touch gesture;
        int device, hidden, closing, resumed, hold_row, holding, physical, x, y, pinch_row, pinch_level;
};

enum
{
        COMPOSITOR_SWIPE_NONE = 0, COMPOSITOR_SWIPE_UNDECIDED = 1, COMPOSITOR_SWIPE_SWIPED = 2,
        COMPOSITOR_SWIPE_LAUNCHER_DP = 48,
        COMPOSITOR_SIDE_OFF = 0, COMPOSITOR_SIDE_WINDOWS = 1, COMPOSITOR_SIDE_ACT = 2,
        COMPOSITOR_CHIP_NONE = 0, COMPOSITOR_CHIP_CONTROLS = 1, COMPOSITOR_CHIP_HINT = 2, COMPOSITOR_CHIP_DRAG = 3,
        COMPOSITOR_CHIP_DRAG_GAP_DP = 18, COMPOSITOR_CHIP_W_MOST = 960, COMPOSITOR_CHIP_H_MOST = 256,
        COMPOSITOR_CHIP_TOP_DP = 6, COMPOSITOR_CHIP_HEIGHT_DP = 32, COMPOSITOR_CHIP_GLYPH_DP = 6,
        COMPOSITOR_CHIP_HINT_W_DP = 48, COMPOSITOR_CHIP_HINT_H_DP = 4, COMPOSITOR_CHIP_HINT_MARGIN_DP = 6,
        COMPOSITOR_CHIP_BODY_COVER = 224
};
#define COMPOSITOR_SWIPE_ARM_NS  2000000000ull
#define COMPOSITOR_CHIP_HOLD_NS  3000000000ull
#define COMPOSITOR_CHIP_FADE_NS  300000000ull
#define COMPOSITOR_CHIP_HINT_NS  1500000000ull
#define COMPOSITOR_CHIP_SLIDE_NS 180000000ull
#define COMPOSITOR_CHIP_BODY     0x1e1e22u
#define COMPOSITOR_CHIP_GLYPH    0xe4e4e4u

struct compositor_swipe
{
        int state, device, slot, side, cell, row, rect[4], screen, x0, y0;
        positive deadline;
        struct ui_touch_frame landing;
        int armed_row;
        positive armed_until;
};

struct compositor_chip
{
        int kind, row, rel_x, rel_y, w, h, cells, slide, drawn[5];
        unsigned int alpha;
        positive shown, hold, fade;
        unsigned char *cover;
        unsigned int *scratch;
};

/* Dynamic row high-water and row bitset (WINDOWS rows + 1 misc slot). */
#define COMPOSITOR_ROWS_FIRST 32
#define COMPOSITOR_ROWS_SPARE 16
static unsigned int compositor_rows_in_use = COMPOSITOR_ROWS_FIRST, compositor_rows_room = WINDOWS, compositor_rows_made;
static unsigned int compositor_rows_grow(unsigned int want);
#define COMPOSITOR_ROWS ((int)compositor_rows_in_use)
#define COMPOSITOR_ROW_WORDS ((WINDOWS + 1 + 63) / 64)
struct compositor_rows { unsigned long long word[COMPOSITOR_ROW_WORDS]; };

static inline INLINE CONST unsigned int rows_ctz64(unsigned long long w)
{
#if defined(__x86_64__)
        unsigned long long r;
        __asm__("bsfq %1, %0" : "=r"(r) : "r"(w));
        return (unsigned int)r;
#elif defined(__aarch64__)
        unsigned long long r;
        __asm__("rbit %0, %1\n\tclz %0, %0" : "=r"(r) : "r"(w));
        return (unsigned int)r;
#else
        return (unsigned int)__builtin_ctzll(w);
#endif
}
static inline INLINE PURE unsigned int compositor_rows_words(void) { unsigned int u = (compositor_rows_in_use + 63) >> 6; return u < COMPOSITOR_ROW_WORDS ? u : COMPOSITOR_ROW_WORDS; }
static inline INLINE PURE unsigned int rows_walked(void) { unsigned int u = compositor_rows_words(); return u < COMPOSITOR_ROW_WORDS ? u + 1 : u; }
static inline INLINE PURE unsigned int rows_walk(unsigned int k) { return k < compositor_rows_words() ? k : COMPOSITOR_ROW_WORDS - 1; }
static inline INLINE PURE int rows_has(const struct compositor_rows *s, long at) { return (unsigned long)at <= WINDOWS && ((s->word[(unsigned long)at >> 6] >> ((unsigned long)at & 63u)) & 1u); }
static inline INLINE void rows_add(struct compositor_rows *s, long at) { if (likely((unsigned long)at <= WINDOWS)) s->word[(unsigned long)at >> 6] |= 1ULL << ((unsigned long)at & 63u); }
static inline INLINE void rows_del(struct compositor_rows *s, long at) { if (likely((unsigned long)at <= WINDOWS)) s->word[(unsigned long)at >> 6] &= ~(1ULL << ((unsigned long)at & 63u)); }
static inline INLINE void rows_add_atomic(struct compositor_rows *s, long at) { if (likely((unsigned long)at <= WINDOWS)) __atomic_or_fetch(&s->word[(unsigned long)at >> 6], 1ULL << ((unsigned long)at & 63u), __ATOMIC_RELEASE); }
static inline INLINE PURE unsigned long long rows_low(const struct compositor_rows *s) { return s->word[0]; }
static inline INLINE CONST struct compositor_rows rows_from(unsigned long long low) { struct compositor_rows s = {{0}}; s.word[0] = low; return s; }

static inline INLINE PURE int rows_next(const struct compositor_rows *s, int from)
{
        unsigned int limit = compositor_rows_in_use, n = compositor_rows_words(), wi;
        unsigned long long w;

        if (from < 0)
                from = 0;
        if ((unsigned int)from >= limit)
                return -1;
        wi = (unsigned int)from >> 6;
        w = s->word[wi] & (~0ULL << ((unsigned int)from & 63u));
        for (;;)
        {
                if (w)
                {
                        unsigned int idx = (wi << 6) + rows_ctz64(w);
                        return idx < limit ? (int)idx : -1;
                }
                if (++wi >= n)
                        return -1;
                w = s->word[wi];
        }
}

static inline INLINE void rows_take(struct compositor_rows *restrict into, struct compositor_rows *restrict from)
{
        for (unsigned int k = 0, n = rows_walked(), w; k < n; k++)
                w = rows_walk(k), into->word[w] = from->word[w] ? __atomic_exchange_n(&from->word[w], 0ULL, __ATOMIC_ACQ_REL) : 0;
}
static inline INLINE PURE int rows_any(const struct compositor_rows *s)
{
        unsigned int n = compositor_rows_words();
        unsigned long long acc = s->word[COMPOSITOR_ROW_WORDS - 1];
        for (unsigned int w = 0; w < n; w++)
                acc |= s->word[w];
        return acc != 0;
}
static inline INLINE void rows_none(struct compositor_rows *s)
{
        unsigned int n = compositor_rows_words();
        for (unsigned int w = 0; w < n; w++) s->word[w] = 0;
        s->word[COMPOSITOR_ROW_WORDS - 1] = 0;
}
static inline INLINE void rows_or(struct compositor_rows *restrict into, const struct compositor_rows *restrict with)
{
        for (unsigned int k = 0, n = rows_walked(), w; k < n; k++) w = rows_walk(k), into->word[w] |= with->word[w];
}
static inline INLINE void rows_and_not(struct compositor_rows *restrict into, const struct compositor_rows *restrict without)
{
        for (unsigned int k = 0, n = rows_walked(), w; k < n; k++) w = rows_walk(k), into->word[w] &= ~without->word[w];
}

struct compositor_poll { int handle; short events, returned; };

enum
{
        COMPOSITOR_POLLIN = 1, COMPOSITOR_POLLOUT = 4, COMPOSITOR_POLLERR = 8, COMPOSITOR_POLLHUP = 16,
        COMPOSITOR_INPUTS = 1024, COMPOSITOR_INPUTS_KEPT = 32, COMPOSITOR_WHEEL_JOIN = 960, COMPOSITOR_DEVICES = 1024,
        COMPOSITOR_TRAY = TASKBAR_TRAY,
        COMPOSITOR_GRIP = 6, COMPOSITOR_GRIP_IN = 2, COMPOSITOR_TOUCH_GRIP = 16, COMPOSITOR_TOUCH_GRIP_IN = 8,
        COMPOSITOR_FINGER_TARGET = 44, COMPOSITOR_TOUCH_CORNER = 24, COMPOSITOR_TOUCH_BUTTON_SLOP = 4,
        COMPOSITOR_PREDICT_MOUSE = 0, COMPOSITOR_PREDICT_TOUCH = 1, COMPOSITOR_PREDICT_PEN = 2, COMPOSITOR_PREDICTS = 3,
        COMPOSITOR_PREDICT_CAP = 4, COMPOSITOR_PREDICT_FX = 1024, COMPOSITOR_PREDICT_HISTORY = 16,
        COMPOSITOR_PREDICT_STILL_LO = 40, COMPOSITOR_PREDICT_STILL_HI = 80,
        COMPOSITOR_PREDICT_STRAIGHT_LO = 600, COMPOSITOR_PREDICT_STRAIGHT_SPAN = 300
};
#define COMPOSITOR_SLEPT_NS          50000000ull
#define COMPOSITOR_WAKE_FIRST_NS     500000000ull
#define COMPOSITOR_WAKE_SECOND_NS    2000000000ull
#define COMPOSITOR_PREDICT_WINDOW_NS 50000000ull
#define COMPOSITOR_PREDICT_SPAN_NS   4000000ull

struct compositor_predict
{
        long long x[COMPOSITOR_PREDICT_HISTORY], y[COMPOSITOR_PREDICT_HISTORY];
        positive when[COMPOSITOR_PREDICT_HISTORY], arrived[COMPOSITOR_PREDICT_HISTORY];
        unsigned int count;
        positive last;
        long long lead_x, lead_y;
};

// A lead in 1/1024 px, as the whole pixel it is shown at (to the nearest).
static long long compositor_predict_px(long long fx)
{
        long long half = COMPOSITOR_PREDICT_FX / 2;

        return fx >= 0 ? (fx + half) / COMPOSITOR_PREDICT_FX : -((-fx + half) / COMPOSITOR_PREDICT_FX);
}
static void compositor_pointer_sync(void);
static void compositor_pointer_shown(void);

#define COMPOSITOR_SPOT_FADE_NS  1000000000ull
#define COMPOSITOR_GRID_DWELL_NS (400 * UI_MS)
#define COMPOSITOR_MAGNIFIED_NS  1200000000ull
enum
{
        COMPOSITOR_STEP = 20,
        COMPOSITOR_EDGE_LEFT = CP_EDGE_LEFT, COMPOSITOR_EDGE_RIGHT = CP_EDGE_RIGHT,
        COMPOSITOR_EDGE_TOP = CP_EDGE_TOP, COMPOSITOR_EDGE_BOTTOM = CP_EDGE_BOTTOM,
        COMPOSITOR_SNAP = 8, COMPOSITOR_SNAP_EDGE = 8, COMPOSITOR_SNAP_CORNER = 16, COMPOSITOR_SHARED_MARK = 2,
        COMPOSITOR_DAMAGE = 8,
        COMPOSITOR_ENOENT = 2, COMPOSITOR_EAGAIN = 11, COMPOSITOR_EBUSY = 16, COMPOSITOR_ENODEV = 19,
        COMPOSITOR_EINVAL = 22, COMPOSITOR_ENOSPC = 28,
        COMPOSITOR_IN_CLOSE_WRITE = 0x8u, COMPOSITOR_IN_MOVED_TO = 0x80u, COMPOSITOR_IN_CREATE = 0x100u, COMPOSITOR_IN_DELETE = 0x200u,
        COMPOSITOR_NONBLOCK = 0x800, COMPOSITOR_CLOEXEC = 0x80000,
        COMPOSITOR_NONE = -1, COMPOSITOR_OVER = -1,
        COMPOSITOR_PAD_POOL = 16, COMPOSITOR_TOUCH_POOL = 16
};

struct compositor_output
{
        unsigned int crtc, crtc_index, connector;
        struct compositor_mode mode;
        char name[WINDOWS_OUTPUT_NAME], model[WINDOWS_OUTPUT_MODEL];
        int scale;
        unsigned int shows;
        int cursor_x, cursor_y;
        unsigned int fb, hz[WINDOWS_OUTPUT_HZ], hz_count;
        unsigned short sizes[WINDOWS_OUTPUT_SIZES][2];
        unsigned int size_count, size_preferred, turn, turn_software, degrees, turned, turn_plane, turn_property;
        unsigned long turn_value;
};

enum compositor_grab_kind
{
        COMPOSITOR_GRAB_NONE = 0, COMPOSITOR_GRAB_MOVE, COMPOSITOR_GRAB_RESIZE, COMPOSITOR_GRAB_DIVIDER,
        COMPOSITOR_GRAB_EDGE_CHORD, COMPOSITOR_GRAB_MINIMIZE_BTN, COMPOSITOR_GRAB_TOUCH_CLOSE, COMPOSITOR_GRAB_TOUCH_HOLD
};
enum compositor_grab_end_how { COMPOSITOR_GRAB_COMMIT = 0, COMPOSITOR_GRAB_CANCEL, COMPOSITOR_GRAB_STOP, COMPOSITOR_GRAB_DROP };
struct compositor_grab { unsigned int kind; int row; unsigned int code, edges; };

struct compositor_device
{
        int number;
        unsigned char wakeonly, wheel_streamed, pad_slot, touch_slot;
        unsigned int wheel_run;
        positive wheel_at, wheel_stream_at;
        int repeat_now[2];
        unsigned short id[4];
        char name[WINDOWS_INPUT_NAME];
};

#define COMPOSITOR_SPACES_MAX 16
enum { COMPOSITOR_SPACE_DESKTOP = 0, COMPOSITOR_SPACE_FULLSCREEN = 1 };

struct compositor_space
{
        unsigned int id;
        unsigned char kind;
        int screen;
        int fs_row;
        unsigned int return_id;
        int last_focus;
};

// The Scenes overview (C8, scenes.c): the Scene cards along the top, the current Scene's windows in the middle, and the press
// in progress. Bounded: a Scene shows at most COMPOSITOR_SCENES_WINDOWS windows.
#define COMPOSITOR_SCENES_WINDOWS 64
struct compositor_scenes
{
        int open, screen, pressed, press_x, press_y, press_row, moved;
        int drag, progress, velocity;   // a drag from the globe: 1 while it is held, its progress 0..1024 and velocity in px/s
        unsigned int cards, windows;
        int card[COMPOSITOR_SPACES_MAX + 1][4];
        int row[COMPOSITOR_SCENES_WINDOWS], rect[COMPOSITOR_SCENES_WINDOWS][4];
};

static struct
{
        int card;
        unsigned int *scanout;
        unsigned long scanout_bytes, scanout_pitch;
        unsigned int fb, crtc;
        unsigned int *composing, *mirror;
        unsigned long mirror_pitch;
        int changed, mirror_stale, changed_turned, width, height;
        struct compositor_output output[WINDOWS_OUTPUTS];
        int screen[WINDOWS_OUTPUTS][4];
        unsigned int outputs, layout;
        int dark, dark_applied, stride, most_w, most_h, room_h;
        unsigned int scanout_handle;
        unsigned long composing_bytes, mirror_bytes;
        int space_wanted, screens_gone, retrain;
        positive screens_look;
        int kept_x, kept_y, absolute, damage[COMPOSITOR_DAMAGE][4];
        unsigned int damages;
        int signals, inputs[COMPOSITOR_INPUTS], absolute_x[COMPOSITOR_INPUTS][2], absolute_y[COMPOSITOR_INPUTS][2];
        unsigned int input_count;
        int watch, probe_owed, uevents;
        unsigned long device_open[COMPOSITOR_DEVICES / 64];
        struct compositor_device devices[COMPOSITOR_INPUTS];
        int scale, boot_scale;

        struct windows *table;
        unsigned long pixels, buffer;
        int table_memfd, table_giver, udmabuf, direct_row, direct_refused, flip_pending, flip_row;
        positive flip_when, flip_issued, flip_serial;
        unsigned int flip_seq;
        int flip_stamped, flip_async, flip_tore, vrr_kick;
        int overlay_state, overlay_on, overlay_rect[4];
        int plane_row, leaving_row, plane_rect[4], scaled_direct, scale_plane_refused;
        unsigned int plane_fb;
        int plane_gone, overlay_owed;
        unsigned int crtc_index, primary, overlay, primary_props[11], overlay_props[11];
        unsigned int overlay_fb[2], overlay_handle[2], overlay_back, overlay_rows;
        unsigned int *overlay_pixels[2];
        unsigned long overlay_pitch, overlay_bytes;
        unsigned int (*scan_fb)[2], (*scan_handle)[2], on_screen, flip_buffer, *taken_held, (*patched)[2];
        struct compositor_rows held_rows, dropping, reclaim, list_later;
        int screen_stale;
        unsigned int memfd_failed;
        unsigned int *under[2];
        int under_rect[2][4], *door, *wake;
        struct compositor_rows woken, fit;
        positive fit_until;
        unsigned int *seen;
        struct compositor_run_row **runs;
        unsigned long *runs_bytes;
        unsigned int *runs_tag, *runs_composed;
        unsigned char *runs_state;
        unsigned int *showing;
        unsigned char *landing;
        positive *land_at;
        unsigned char *glide;
        int (*glide_from)[4];
        positive *glide_at;
        int (*glide_base)[2], drag_glide;
        unsigned int *made;
        int *x, *y;
        unsigned int *w, *h, *front, *state;
        unsigned char *close_asks;
        unsigned int *picture_w, *picture_h, *asking, *arranged;
        unsigned char *fullscreen, *fullscreen_was;
        unsigned int *row_space, *row_space_prev;
        signed char *layer;
        unsigned int *edges;
        int *far_x, *far_y, *saved_x, *saved_y;
        unsigned int *saved_w, *saved_h;
        char (*title)[WINDOWS_TITLE], (*app_id)[WINDOWS_APP_ID];
        unsigned int *icon_seq, (*icon)[WINDOWS_ICON * WINDOWS_ICON];
        char (*name)[WINDOWS_NAME];
        unsigned int *moved, *stack, stacked, stack_gen, next_made;

        unsigned int *listed;
        unsigned char *marks, *lengths;
        unsigned int listing, *band;
        struct
        {
                int used, has_icon;
                void *owner;
                unsigned int id, pixels[TASKBAR_TRAY_ITEM_PX * TASKBAR_TRAY_ITEM_PX];
                void (*click)(void *owner, unsigned int id, int button);
        } tray[TASKBAR_TRAY_ITEM_MAX];
        int tray_gen, band_at, band_hit;
        struct
        {
                struct compositor_space list[COMPOSITOR_SPACES_MAX];
                unsigned int count, next_id, gen;
                unsigned int active[WINDOWS_OUTPUTS];
                int ready;
        } spaces;
        struct compositor_scenes scenes;
        unsigned int *band_more[WINDOWS_OUTPUTS];
        unsigned long band_more_bytes[WINDOWS_OUTPUTS];
        unsigned char (*band_marks)[WINDOWS], *banded;
        unsigned long band_bytes;
        unsigned int *glass;
        unsigned long glass_bytes;

        unsigned int *glassy, *asks, **backdrop, **tinted, **composed, **boosted;
        int *composed_fd; // step 3: the memfd of a glass row's composed picture, plus one (0: none), kept while it is mapped
        unsigned long *composed_bytes;
        unsigned long (*glass_sum)[3];
        int (*glass_blocks)[4];
        unsigned long *glass_cut;
        int (*glass_made)[4], (*stale)[4], (*dirty)[4];
        unsigned int *glass_side, *glass_told, *lifted, theme_word, theme_side;
        unsigned int *blur, *frost, *rise;
        unsigned long blur_bytes, frost_bytes, rise_bytes;
        positive blurred, recomposed, glass_band_card; // glass_band_card: glass bands composed on the card (gpu_glass), counted by glass.c
        int damage_by;
        font_face face, bold;
        int bold_ok;
        font_engine *engine;

        int focus, hover, grab, drag, resize, press;
        struct compositor_grab active_grab;
        unsigned int edge_right;
        struct ui_click press_click;
        struct taskbar_cells cells;
        unsigned int settings_asked, launcher_asked;
        unsigned char logo_alone;
        int drag_x, drag_y, drag_moved, drag_keep[4];
        struct compositor_rows hold_rows;
        int (*hold_rect)[4];
        unsigned int *hold_how;
        int assist_on, assist_armed, assist_row, assist_screen, assist_count;
        unsigned int assist_how, assist_rows[COMPOSITOR_ASSIST_MAX], assist_zone[COMPOSITOR_ASSIST_MAX];
        int assist_rect[COMPOSITOR_ASSIST_MAX][4];
        int grid_on, grid_screen, grid_since_screen;
        unsigned int grid_hover;
        positive grid_since;
        unsigned int overlays_up, overlay_from, overlay_goal;
        positive overlay_at;
        int resize_w, resize_h, press_x, press_y, button_x, button_y;
        int shared_row, shared_lo, shared_hi, shared_vertical;
        struct compositor_rows shared_lo_set, shared_hi_set;
        int *shared_sizes, mark_row, mark_with, mark[4];
        struct compositor_rows mark_rows;
        int pointer_x, pointer_y;
        struct compositor_keymap keymap;
        unsigned char keys_down[32];
        int keys_to;
        struct
        {
                unsigned int handle[2], *mapped[2];
                int back, plane_on;
                struct compositor_pointer plane, drawn_as;
                int soft, drawn, x, y, client_hidden, hold_hidden;
                unsigned int shape;
                int asked_row, asked_seen, magnified;
                positive magnified_until;
                int magnify, shake_off, click_off;
                positive shake_ns;
                int size, look;
                unsigned long paints, moves, refused;
                int style, dyn, squeeze;
                unsigned int held;
                struct compositor_sway press, morph, lens, stretch;
                int ux, uy, lens_ux, lens_uy, last_x, last_y;
                positive last_ns, idle_until;
        } cursor;
        struct compositor_shake shake;
        char driver[16];
        unsigned int driver_flags;
        struct compositor_mode mode;
        unsigned int connector;
        positive asleep, wake_at;
        int wake_stage;
        unsigned int wakes;
        positive motion_ts;
        int hold;
        positive hold_until;
        unsigned long holds;
        int game;
        unsigned long games;
        struct { positive total, count, worst; } latency[3];
        int cycle, raw_x, raw_y, accel_x, accel_y, tablet_x, tablet_y, tablet_have;
        positive stamp;
        unsigned char hires[COMPOSITOR_INPUTS];
        long long wheel_rest[COMPOSITOR_INPUTS][2];
        positive stream_hist[COMPOSITOR_INPUTS][COMPOSITOR_STREAM_HIST];
        unsigned int stream_hist_n[COMPOSITOR_INPUTS], trackpad_speed;
        unsigned char touchpad[COMPOSITOR_INPUTS];
        struct compositor_pad pad[COMPOSITOR_PAD_POOL];
        unsigned int pad_used;
        unsigned char touchscreen[COMPOSITOR_INPUTS];
        struct compositor_touch_screen touches[COMPOSITOR_TOUCH_POOL];
        unsigned int touch_used;
        struct compositor_touch_screen screen_touch[COMPOSITOR_INPUTS];
        struct compositor_touch touch;
        struct compositor_swipe swipe;
        struct compositor_chip chip;
        int side_left, side_right;
        struct compositor_predict predict[COMPOSITOR_PREDICTS];
        unsigned char predict_off[COMPOSITOR_PREDICTS];
        unsigned char osk_setting, osk_typed, osk_shown, osk_turned, osk_turn_blur;
        int osk_turn_focus, *osk_pan, *osk_height;
        unsigned int *osk_edges;
        unsigned char *text_purpose;
        int *caret, real_x, real_y, shown_x, shown_y;
        unsigned int owner, at_real;
        int touching, retracting, spots_on;
        unsigned int spots_shown, spots_fading;
        struct { int x, y, on; positive lifted; unsigned int alpha; int drawn[3]; } spot[UI_TOUCH_SLOTS];
        unsigned int *spot_scratch;
        int lit_row;
        unsigned int lit_edges, lit_held, border;
        int preview_row, preview[4];
        unsigned int snap_held;
        int preview_draw, preview_from[4], preview_fading;
        unsigned int preview_how, preview_from_alpha;
        positive preview_at;
        unsigned int input_kinds[COMPOSITOR_INPUTS];
        int told_row, told_x, told_y;
        unsigned char button_down;
        int button_row[8];
        unsigned char held[COMPOSITOR_INPUTS + 1], repeats[COMPOSITOR_INPUTS];
        int repeat_own[COMPOSITOR_INPUTS][2], repeat_kept, repeat_delay, repeat_rate, sensitivity, flat, grip, layout_given;
        unsigned long painted;
        positive period, presented;
} compositor;

static unsigned int compositor_pool_take(unsigned int at, unsigned int cap, unsigned int *used, unsigned char *slot_out)
{
        unsigned int slot = 0;
        if (*slot_out) slot = (unsigned int)(*slot_out - 1);
        else if (at < cap && !(*used & (1u << at))) slot = at;
        else { while (slot < cap && (*used & (1u << slot))) slot++; if (slot >= cap) slot = 0; }
        *used |= 1u << slot;
        *slot_out = (unsigned char)(slot + 1);
        return slot;
}

static void compositor_pool_drop(unsigned int at, unsigned int *used, unsigned char *slot_out, void *item, unsigned long item_size)
{
        if (at < COMPOSITOR_INPUTS && *slot_out)
        {
                unsigned int slot = (unsigned int)(*slot_out - 1);
                *used &= ~(1u << slot);
                memory_zero((unsigned char *)item + (unsigned long)slot * item_size, item_size);
                *slot_out = 0;
        }
}

static struct compositor_pad *compositor_pad_of(unsigned int at)
{
        unsigned int slot = at < COMPOSITOR_INPUTS && compositor.devices[at].pad_slot
                                ? (unsigned int)(compositor.devices[at].pad_slot - 1)
                                : (at < COMPOSITOR_PAD_POOL ? at : 0u);
        return &compositor.pad[slot];
}

static struct compositor_pad *compositor_pad_alloc(unsigned int at)
{
        unsigned int slot = at < COMPOSITOR_INPUTS ? compositor_pool_take(at, COMPOSITOR_PAD_POOL, &compositor.pad_used, &compositor.devices[at].pad_slot) : 0;
        memory_zero(&compositor.pad[slot], sizeof(compositor.pad[slot]));
        return &compositor.pad[slot];
}

static void compositor_pad_free(unsigned int at)
{
        if (at < COMPOSITOR_INPUTS) compositor_pool_drop(at, &compositor.pad_used, &compositor.devices[at].pad_slot, compositor.pad, sizeof(compositor.pad[0]));
}

static struct compositor_touch_screen *compositor_touch_alloc(unsigned int at)
{
        unsigned int slot = at < COMPOSITOR_INPUTS ? compositor_pool_take(at, COMPOSITOR_TOUCH_POOL, &compositor.touch_used, &compositor.devices[at].touch_slot) : 0;
        memory_zero(&compositor.touches[slot], sizeof(compositor.touches[slot]));
        return &compositor.touches[slot];
}

static void compositor_touch_free(unsigned int at)
{
        if (at < COMPOSITOR_INPUTS) compositor_pool_drop(at, &compositor.touch_used, &compositor.devices[at].touch_slot, compositor.touches, sizeof(compositor.touches[0]));
}

static struct compositor_touch_screen *compositor_touch_of(unsigned int at) { return &compositor.screen_touch[at < COMPOSITOR_INPUTS ? at : 0u]; }

static void compositor_close_count(int row, unsigned int asks)
{
        compositor.close_asks[row] = (unsigned char)asks;
        if (compositor.table) __atomic_store_n(&compositor.table->close_asks[row], (unsigned int)compositor.close_asks[row], __ATOMIC_RELEASE);
}

static int compositor_spots_any(int fading);
static void compositor_spots_draw(const struct compositor_target *t), compositor_spots_marks(const struct compositor_target *t);
static void compositor_spots_compose(const struct compositor_target *t, const struct compositor_frame *frames, unsigned int count);
static void compositor_cursor_compose(const struct compositor_target *t, const struct compositor_frame *frames, unsigned int count);
static unsigned int compositor_pointer_plane_word(void);
static void compositor_spots_ready(void);
static int compositor_turning(void);

static unsigned int compositor_cursor_ink(int outline, unsigned int alpha) { return ((compositor.cursor.look == 2) == outline ? 0xffffffu : 0u) | alpha; }
static int compositor_cursor_size(void)
{
        int base = (compositor_px1(compositor.scale) * 256 * (compositor.cursor.size ? compositor.cursor.size : 100) + 50) / 100;
        return compositor.cursor.magnified ? 3 * base : base;
}
static int compositor_cursor_in_scene(void)
{
        return compositor.cursor.soft || compositor_turning() ||
               (compositor.cursor.handle[0] && !compositor_picture.worn && compositor_cursor_size() > CP_PLANE / CP_DYN_CELL * 256);
}

static const int (*compositor_screens(unsigned int *count))[4]
{
        static int whole[1][4];
        if (compositor.outputs) { *count = compositor.outputs; return (const int (*)[4])compositor.screen; }
        whole[0][0] = whole[0][1] = 0, whole[0][2] = compositor.width, whole[0][3] = compositor.height, *count = 1;
        return (const int (*)[4])whole;
}

static int compositor_stride(void) { return compositor.stride ? compositor.stride : compositor.width; }
static int compositor_most_width(void) { return compositor.most_w ? compositor.most_w : compositor.width; }
static int compositor_most_height(void) { return compositor.most_h ? compositor.most_h : compositor.height; }
static unsigned long compositor_buffer_room(void)
{
        int rows = compositor.room_h > compositor_most_height() ? compositor.room_h : compositor_most_height();
        return ((unsigned long)compositor_stride() * (unsigned long)rows * 4 + 4095) & ~4095ul;
}
static int compositor_output_count(void) { return compositor.outputs ? (int)compositor.outputs : 1; }
static void compositor_output_rect(int index, int *x, int *y, int *width, int *height)
{
        unsigned int count;
        const int (*screens)[4] = compositor_screens(&count);
        index = index >= 0 && (unsigned int)index < count ? index : 0;
        *x = screens[index][0], *y = screens[index][1], *width = screens[index][2], *height = screens[index][3];
}
static const char *compositor_output_name(int index)
{
        return compositor.outputs && index >= 0 && (unsigned int)index < compositor.outputs ? compositor.output[index].name : "";
}
static long compositor_call(positive number, positive a, positive b, positive c, positive d, positive e, positive f)
{
        return (long)system_call_6(number, a, b, c, d, e, f);
}

static void compositor_ordinary(int nice)
{
        int none = 0;
        compositor_call(syscall(sched_setscheduler), 0, 0, (positive)&none, 0, 0, 0);
        if (nice) compositor_call(syscall(setpriority), 0, (positive)compositor_call(syscall(gettid), 0, 0, 0, 0, 0, 0), (positive)nice, 0, 0, 0);
}

static void *compositor_map_zeroed(unsigned long bytes)
{
        long got = compositor_call(syscall(mmap), 0, bytes, 3, 0x22, (positive)-1, 0);
        return got < 0 && got > -4096 ? 0 : (void *)got;
}

static int compositor_tray_nth(int index)
{
        int n = 0;
        for (int i = 0; i < TASKBAR_TRAY_ITEM_MAX; i++)
                if (compositor.tray[i].used && compositor.tray[i].has_icon && n++ == index) return i;
        return index < 0 ? n : -1;
}
static int compositor_tray_count(void) { return compositor_tray_nth(-1); }
static int compositor_tray_gen(void) { return compositor.tray_gen; }
static const unsigned int *compositor_tray_pixels(int index) { int s = compositor_tray_nth(index); return s >= 0 && compositor.tray[s].has_icon ? compositor.tray[s].pixels : 0; }
static void compositor_tray_click(int index, int button) { int s = compositor_tray_nth(index); if (s >= 0 && compositor.tray[s].click) compositor.tray[s].click(compositor.tray[s].owner, compositor.tray[s].id, button); }

static unsigned int compositor_chip_drag_items, compositor_chip_drag_kind;
static void compositor_chip_drag(int action);

enum { COMPOSITOR_OVERRIDE_QUERY = 0, COMPOSITOR_OVERRIDE_KEY = 1, COMPOSITOR_OVERRIDE_POINTER = 2 };
struct compositor_input_override_event { unsigned int kind, ev_type, flags; int row; unsigned int pid; int a, b, width, height; };
typedef int (*compositor_input_override_fn)(void *ctx, const struct compositor_input_override_event *ev);
static unsigned int compositor_input_overrides;
static int compositor_input_override_query(int row);
static int compositor_input_override_pointer(int row, unsigned int type, unsigned int flags, int a, int b);
static void compositor_row_notify_stack(int row);
static unsigned int compositor_rows_grow(unsigned int want);
static int compositor_space_row_active(int row);
static int compositor_space_screen_fullscreen(int screen);
static int compositor_space_all_fullscreen(void);
static int compositor_space_is_fs_row(int row);
static void compositor_spaces_ensure(void);
static int compositor_spaces_count_on(int screen);
static int compositor_spaces_active_index(int screen);
static int compositor_spaces_kind_at(int screen, int idx);
static unsigned int compositor_spaces_gen(void);
static void compositor_spaces_on_outputs_changed(const char (*was_names)[WINDOWS_OUTPUT_NAME], unsigned int was_count);
static unsigned int compositor_space_desktop_count(int screen);
static struct compositor_space *compositor_space_at(int screen, int idx);
static int compositor_row_space_exempt(int row);
static unsigned int compositor_space_add_id(int screen, int activate);
static void compositor_space_switch_index(int screen, int idx);
static void compositor_space_remove_index(int screen, int idx);
static void compositor_space_move_row(int row, unsigned int target_sid, int follow);
static int compositor_space_current_screen(void);
static int compositor_band_hit(int x, int y);
static int compositor_screen_of(int row);
// The Scenes overview (compositor/scenes.c), for the parts before it: whether it is drawn, whether a point is its, a press of it let go
// of, and the overview shut at once (the screens changed under it).
static int compositor_scenes_shown(void);
static int compositor_scenes_point(int x, int y);
static void compositor_scenes_let_go(int row);
static void compositor_scenes_shut(void);
static void compositor_row_mode(int row, unsigned int how, int fullscreen, int screen);
static void compositor_pointer_press(unsigned int code, int down);
static int compositor_grab_end(int how, int drop_row);
static void compositor_assist_close(void);
#ifdef CONFIG_MOONWATER_WAYLAND
static int compositor_wl_started(void);
#endif

/* The owner of each row as the compositor took its claim (window.c windows_owner_take): kept here, never in the shared table, so a client's
   rewritten pid is refused by windows_owner_ok wherever the compositor uses a row's pid. Indexed by row, sparse in BSS. */
static struct windows_owner windows_owner_held[WINDOWS];
#include "compositor/damage.c" // what changed: rectangles and damage, landings and glides, the snap preview's state, a row's pixels and runs, the frames and the bands
#include "compositor/glass.c" // the glass under the panel and the tabs: the grain, the frost, the blur and its scene
#include "compositor/scanout.c" // what the card scans out: a window straight from its buffer, the record, the flip and its time, the planes, a turned screen
#include "compositor/gpu_host.c" // the card's part of a present: the render node's state, the scene and the window lists drawn on it, its hangs and its wake
#include "compositor/present.c" // a present: the metrics, the loop's tick and the frame composed and shown
#include "compositor/kms.c" // the card: the displays and their modes, the scanouts and plates, the space taken, the card found and opened, the cursor plane
#include "compositor/session.c" // the session: a machine that slept, the settings read and published, the input devices found, the doors and their threads
#include "compositor/manage.c" // the windows: the stack and the keys, grips and dividers, tiles and the grid, places, a row taken and let go, the keyboard on the screen, the clipboard, popups
#include "compositor/scenes.c" // the Scenes overview's layout: a Scene's windows in a grid (no overlap), the strip of Scene cards, the hit test
#include "compositor/input.c" // what a person does: the keys and the chords, the volume, the hand's motion and buttons, the prediction, the wheel and the pad, the marks and touch

/* Sealed memfd for the window table (udmabuf zero-copy scanout; symlinked at WINDOWS_TABLE). */
static long compositor_table_memfd(unsigned long bytes)
{
        p8 path[48];
        long handle = compositor_call(syscall(memfd_create), (positive) "moonwater-windows", WINDOWS_MFD_CLOEXEC | WINDOWS_MFD_SEALING, 0, 0, 0, 0);
        if (handle < 0) { compositor.memfd_failed = 6; return handle; }
        windows_proc_fd((char *)path, (unsigned int)compositor_call(syscall(getpid), 0, 0, 0, 0, 0, 0), (unsigned int)handle);
        compositor.memfd_failed = compositor_call(syscall(ftruncate), (positive)handle, bytes, 0, 0, 0, 0) < 0 ? 8
                                  : compositor_call(syscall(fcntl), (positive)handle, WINDOWS_F_ADD_SEALS, 1 | 2 | 4, 0, 0, 0) < 0 ? 9
                                  : compositor_call(syscall(fchmod), (positive)handle, WINDOWS_MODE, 0, 0, 0, 0) < 0 ? 10
                                  : compositor_call(syscall(symlinkat), (positive)path, (positive)AT_FDCWD, (positive)WINDOWS_TABLE, 0, 0, 0) < 0 ? 11 : 0;
        if (compositor.memfd_failed)
        {
                system_close(handle);
                compositor_call(syscall(unlinkat), (positive)AT_FDCWD, (positive)WINDOWS_TABLE, 0, 0, 0, 0);
                return -COMPOSITOR_EINVAL;
        }
        return handle;
}

static void compositor_table_refusals(struct windows *table, unsigned int at, const char *name)
{
        const struct windows_display *d = windows_display_of(compositor_display, compositor_displays, name, windows_name_length(name));
        const struct compositor_output *o = &compositor.output[at];
        unsigned int refused = 0;

        table->output_asked_hz[at] = d ? d->refresh : 0, table->output_asked_turn[at] = d ? d->rotation : 0;
        table->output_asked_scale[at] = d ? d->scale : WINDOWS_SCALE_DEFAULT;
        table->output_asked_w[at] = d ? d->width : 0, table->output_asked_h[at] = d ? d->height : 0;
        if (d)
        {
                if (d->refresh && o->mode.vrefresh != d->refresh) refused |= WINDOWS_REFUSED_RATE;
                if (d->rotation && !o->turned) refused |= WINDOWS_REFUSED_TURN;
                if (d->bad & (WINDOWS_DISPLAY_X | WINDOWS_DISPLAY_Y)) refused |= WINDOWS_REFUSED_PLACE;
                if (d->width && ((unsigned int)o->mode.hdisplay != d->width || (unsigned int)o->mode.vdisplay != d->height))
                        refused |= WINDOWS_REFUSED_SIZE;
        }
        table->output_refused[at] = refused;
}

static void compositor_table_outputs(void)
{
        struct windows *table = compositor.table;
        int count = compositor_output_count();
        if (!table) return;
        for (int at = 0; at < count && at < WINDOWS_OUTPUTS; at++)
        {
                const struct compositor_output *o = &compositor.output[at];
                const char *name = compositor_output_name(at);
                int x, y, width, height;
                unsigned int length = 0;
                compositor_output_rect(at, &x, &y, &width, &height);
                table->output_x[at] = x, table->output_y[at] = y;
                table->output_w[at] = (unsigned int)width, table->output_h[at] = (unsigned int)height;
                while (name[length] && length < WINDOWS_OUTPUT_NAME - 1) table->output_name[at][length] = name[length], length++;
                memory_zero(table->output_name[at] + length, WINDOWS_OUTPUT_NAME - length);
                memory_copy(table->output_model[at], o->model, WINDOWS_OUTPUT_MODEL);
                table->output_now[at] = o->mode.vrefresh, table->output_rates[at] = o->hz_count;
                memory_copy(table->output_hz[at], o->hz, sizeof(table->output_hz[at]));
                memory_copy(table->output_size[at], o->sizes, sizeof(table->output_size[at]));
                table->output_sizes[at] = o->size_count, table->output_size_preferred[at] = o->size_preferred;
                table->output_mode_w[at] = (unsigned int)o->mode.hdisplay, table->output_mode_h[at] = (unsigned int)o->mode.vdisplay;
                table->output_turn[at] = o->turn, table->output_software[at] = o->turn_software;
                compositor_table_refusals(table, (unsigned int)at, name);
        }
        table->period = (unsigned int)compositor.period, table->layout = compositor.layout;
        table->output_width = (unsigned int)compositor_most_width(), table->output_height = (unsigned int)compositor_most_height();
        __atomic_store_n(&table->outputs, (unsigned int)count, __ATOMIC_RELEASE);
}

static void compositor_lists_set(int on)
{
        struct windows *table = compositor.table;
        if (!table) return;
        __atomic_store_n(&table->list_mode, on ? 1u : 0u, __ATOMIC_RELEASE);
        __atomic_add_fetch(&table->themed, 1, __ATOMIC_RELEASE);
        compositor_wake_all();
}

static long compositor_door_make(unsigned int row);
#define COMPOSITOR_TABLE_MOST (1ul << 45)

static unsigned int compositor_table_room(unsigned long buffer)
{
        unsigned long rows = (COMPOSITOR_TABLE_MOST - WINDOWS_HEADER) / (2 * buffer + WINDOW_LIST_BYTES);
        rows = rows < WINDOWS ? rows : WINDOWS;
        return rows > COMPOSITOR_ROWS_FIRST ? (unsigned int)rows : COMPOSITOR_ROWS_FIRST;
}

static int compositor_table(void)
{
        unsigned long buffer = compositor_buffer_room();
        unsigned int room = compositor_table_room(buffer);
        unsigned long bytes = WINDOWS_LISTS_AT(buffer, room) + WINDOW_LIST_BYTES * (unsigned long)room;
        long handle, mapped;

        compositor_call(syscall(mkdirat), (positive)AT_FDCWD, (positive) "/run", 0755, 0, 0, 0);
        compositor_call(syscall(mkdirat), (positive)AT_FDCWD, (positive) "/run/moonwater", 0755, 0, 0, 0);
        compositor_call(syscall(unlinkat), (positive)AT_FDCWD, (positive)WINDOWS_TABLE, 0, 0, 0, 0);

        handle = compositor_table_memfd(bytes);
        compositor.table_memfd = handle >= 0 ? (int)handle + 1 : 0;
        if (handle >= 0 && compositor.card >= 0)
        {
                long spark = (long)system_open_at(AT_FDCWD, SPARK_DEVICE, FILE_READ_WRITE | O_CLOEXEC);
                if (spark >= 0 && system_control(spark, WINDOWS_IOCTL_TABLE_SET, handle) == 0) compositor.table_giver = (int)spark + 1;
                else if (spark >= 0) system_close(spark);
        }
        if (handle < 0)
        {
                handle = (long)system_open_at_mode(AT_FDCWD, (string_address)WINDOWS_TABLE, FILE_READ_WRITE | O_CREAT | O_EXCL | O_CLOEXEC, WINDOWS_MODE);
                if (handle < 0) return (int)handle;
                compositor_call(syscall(fchmod), (positive)handle, WINDOWS_MODE, 0, 0, 0, 0);
                if (compositor_call(syscall(ftruncate), (positive)handle, bytes, 0, 0, 0, 0) < 0) { system_close(handle); return -COMPOSITOR_ENOSPC; }
        }
        mapped = compositor_call(syscall(mmap), 0, bytes, 3, 1, (positive)handle, 0);
        if (!compositor.table_memfd) system_close(handle);
        if (mapped < 0 && mapped > -4096) return (int)mapped;

        compositor.table = (struct windows *)mapped, compositor.pixels = WINDOWS_HEADER, compositor.buffer = buffer;
        compositor.table->output_width = (unsigned int)compositor_most_width(), compositor.table->output_height = (unsigned int)compositor_most_height();
        compositor.table->stride = (unsigned int)compositor_stride(), compositor.table->scale = (unsigned int)compositor.scale;
        compositor.table->period = (unsigned int)compositor.period, compositor.table->focus = WINDOWS;
        compositor.table->refused = compositor.memfd_failed, compositor.table->pixels = WINDOWS_HEADER, compositor.table->buffer = buffer;
        compositor.table->lists = WINDOWS_LISTS_AT(buffer, room), compositor.table->capacity = compositor_rows_room = room;
        compositor.table->list_bytes = WINDOW_LIST_BYTES;
        __atomic_store_n(&compositor.table->abi, WINDOWS_ABI, __ATOMIC_RELEASE);
        __atomic_store_n(&compositor.table->rows, compositor_rows_in_use, __ATOMIC_RELEASE);
        compositor_lists_set(0);
        compositor_table_outputs();

        for (unsigned int row = 0; row < compositor_rows_in_use; row++)
                if ((handle = compositor_door_make(row)) < 0) return (int)handle;
        return compositor_clip_start() < 0 ? -1 : 0;
}

static void compositor_table_drop(void)
{
        p8 name[48];
        if (compositor.table_giver)
                system_control(compositor.table_giver - 1, WINDOWS_IOCTL_TABLE_SET, -1l), system_close(compositor.table_giver - 1), compositor.table_giver = 0;
        compositor_call(syscall(unlinkat), (positive)AT_FDCWD, (positive)WINDOWS_TABLE, 0, 0, 0, 0);
        compositor_call(syscall(unlinkat), (positive)AT_FDCWD, (positive)WINDOWS_CLIP_DOOR, 0, 0, 0, 0);
        for (unsigned int row = 0; row < compositor_rows_in_use; row++)
                for (int wake = 0; wake < 2; wake++)
                        windows_door_name((char *)name, row, wake), compositor_call(syscall(unlinkat), (positive)AT_FDCWD, (positive)name, 0, 0, 0, 0);
}

#define COMPOSITOR_SIGUSR1 10
#define COMPOSITOR_SIGUSR2 12
#define COMPOSITOR_SIG_MASK (1ul << 14 | 1ul << 1 | 1ul << 0 | 1ul << (COMPOSITOR_SIGUSR1 - 1) | 1ul << (COMPOSITOR_SIGUSR2 - 1))

static void compositor_pipe_ignore(void) { system_signal_install(13, 1, 0, 0, null); }
static int compositor_signals(void)
{
        unsigned long mask = COMPOSITOR_SIG_MASK;
        compositor_pipe_ignore();
        compositor_call(syscall(rt_sigprocmask), 0, (positive)address_of mask, 0, 8, 0, 0);
        return (int)compositor_call(syscall(signalfd4), (positive)-1, (positive)address_of mask, 8, COMPOSITOR_NONBLOCK | COMPOSITOR_CLOEXEC, 0, 0);
}
static unsigned int compositor_signal_taken(void)
{
        unsigned int said[32];
        return compositor_call(syscall(read), (positive)compositor.signals, (positive)said, sizeof(said), 0, 0, 0) == (long)sizeof(said) ? said[0] : 0;
}
static void compositor_say(string_address what, bipolar value) { string_format(log, "compositor: %s %b\n", what, value); log_flush(); }
static void compositor_screens_say(void);

#define COMPOSITOR_HEADLESS_NAME "Canvas"
static void compositor_headless_scale(void)
{
        compositor_displays_read(0);
        const struct windows_display *d = windows_display_of(compositor_display, compositor_displays, COMPOSITOR_HEADLESS_NAME, windows_name_length(COMPOSITOR_HEADLESS_NAME));
        int scale = d && d->scale ? (int)d->scale : compositor.boot_scale ? compositor.boot_scale : WINDOWS_SCALE_DEFAULT;
        if (scale != compositor.scale) compositor_scale_set(scale);
}

static void compositor_space_retake(void)
{
        unsigned int layout = compositor.layout, was_count = compositor.outputs;
        struct { char name[WINDOWS_OUTPUT_NAME]; unsigned int degrees; } was[WINDOWS_OUTPUTS];
        b32 status = 0, turned = 0;
        int gone = compositor.screens_gone, wake = compositor.retrain && compositor.card >= 0;

        for (unsigned int at = 0; at < was_count && at < WINDOWS_OUTPUTS; at++)
                memory_copy(was[at].name, compositor.output[at].name, WINDOWS_OUTPUT_NAME), was[at].degrees = compositor.output[at].degrees;
        if (wake && system_control(compositor.card, COMPOSITOR_SET_MASTER, 0) != 0)
                compositor_kmsg((string_address) "compositor: wake: the card's master could not be had again", 0, (string_address) "");
        if (compositor.card >= 0) status = compositor_space_take(0), compositor_dark_set();
        else compositor.retrain = 0, compositor_headless_scale();
        if (wake && status >= 0 && !compositor.retrain)
                compositor_kmsg((string_address) "compositor: wake: every crtc set again with a mode set, screens lit ", (positive)compositor.outputs, (string_address) "");
        else if (wake && status < 0)
                compositor_kmsg((string_address) "compositor: wake: the screens could not be set yet, looked at again in 5 s, error ", (positive)-status, (string_address) "");
        if (wake && status < 0 && compositor.screens_gone) compositor.retrain = 0;
        if (wake && compositor.table)
        {
                if (status >= 0 && !compositor.retrain) compositor.table->wake_sets++;
                else if (status < 0) compositor.table->wake_refused++;
        }
        for (unsigned int at = 0; at < compositor.outputs && status >= 0; at++)
                for (unsigned int each = 0; each < was_count && each < WINDOWS_OUTPUTS; each++)
                        if (!memory_compare(was[each].name, compositor.output[at].name, WINDOWS_OUTPUT_NAME) && was[each].degrees != compositor.output[at].degrees)
                                turned = 1;
        if (status < 0 && !(gone && (status == -COMPOSITOR_ENODEV || status == -COMPOSITOR_EINVAL)))
                compositor_say("the screens could not be taken again, error", status);
        else if (status >= 0 && compositor.layout != layout) compositor_screens_say();
        if (turned) compositor_osk_turned();
        for (unsigned int row = 0; row < compositor_rows_in_use; row++)
                if (compositor.asks[row] & WINDOW_KEYBOARD) rows_add(&compositor.woken, row);
}

#define COMPOSITOR_UEVENT 15
static int compositor_uevents_open(void)
{
        bipolar handle = socket_new(AF_NETLINK, SOCK_DGRAM | SOCK_CLOEXEC | SOCK_NONBLOCK, COMPOSITOR_UEVENT);
        socket_address_netlink self = {.family = AF_NETLINK, .groups = 1};
        if (handle < 0) return -1;
        if (socket_bind((b32)handle, address_of self, sizeof(self)) < 0) { system_close(handle); return -1; }
        return (int)handle;
}

static int compositor_uevent_drm(const char *message, long length)
{
        static const char want[] = "SUBSYSTEM=drm";
        for (long at = 0, line; at < length; at++)
        {
                line = at;
                while (at < length && message[at]) at++;
                if (at - line == (long)sizeof(want) - 1 && !memory_compare(message + line, want, sizeof(want) - 1)) return 1;
        }
        return 0;
}

/*
        A drive taken out leaves no mount behind, whether Files is open or not: a block device's remove, heard on this socket, wakes
        a thread of the compositor's own, which lets go of what moonwater device open opened from it (usb_media_tidy: the mount
        detached, its folder gone) under the device lock. The loop only adds to the thread's eventfd, and a burst is one look.
*/
static int compositor_uevent_block_gone(const char *message, long length)
{
        static const char block[] = "SUBSYSTEM=block", gone[] = "ACTION=remove";
        int seen = 0;
        for (long at = 0, line; at < length; at++)
        {
                line = at;
                while (at < length && message[at]) at++;
                seen |= at - line == (long)sizeof(block) - 1 && !memory_compare(message + line, block, sizeof(block) - 1) ? 1 : 0;
                seen |= at - line == (long)sizeof(gone) - 1 && !memory_compare(message + line, gone, sizeof(gone) - 1) ? 2 : 0;
        }
        return seen == 3;
}

#ifdef USB_MEDIA_KEPT // the device verbs (src/sh/host/device.c) are in this program; a check of the compositor alone has none
static int compositor_tidy_wake = -1;
static string_address compositor_tidy_kept = USB_MEDIA_KEPT;

static void compositor_tidy_run(address_any context)
{
        (void)context;
        for (;;)
        {
                unsigned long long asked = 0;
                long got = compositor_call(syscall(read), (positive)compositor_tidy_wake, (positive)&asked, 8, 0, 0, 0);
                bipolar lock;
                if (got == -4) continue; // EINTR
                if (got != 8) return;
                lock = usb_lock_take(false);
                if (lock >= 0) (void)usb_media_tidy(compositor_tidy_kept), radio_unlock(lock);
        }
}

// The thread is made at the first remove and stays; a remove after it is a count on its eventfd. True when it was asked.
static int compositor_tidy_ask(void)
{
        unsigned long long one = 1;
        if (compositor_tidy_wake < 0)
        {
                compositor_tidy_wake = (int)compositor_call(syscall(eventfd2), 0, COMPOSITOR_CLOEXEC, 0, 0, 0, 0);
                if (compositor_tidy_wake >= 0 && !thread_start(compositor_tidy_run, null))
                        system_close(compositor_tidy_wake), compositor_tidy_wake = -1;
        }
        return compositor_tidy_wake >= 0 && compositor_call(syscall(write), (positive)compositor_tidy_wake, (positive)&one, 8, 0, 0, 0) == 8;
}
#else
static int compositor_tidy_ask(void) { return 0; }
#endif

static void compositor_uevents_read(void)
{
        char message[2048];
        int drm = 0, gone = 0;
        for (int reads = 0; reads < 64; reads++)
        {
                bipolar got = socket_receive((b32)compositor.uevents, message, sizeof(message), MSG_DONTWAIT, 0, 0);
                if (got <= 0) break;
                drm |= compositor_uevent_drm(message, got);
                gone |= compositor_uevent_block_gone(message, got);
        }
        if (gone)
                (void)compositor_tidy_ask();
        if (drm)
        {
                compositor_space_retake();
#ifdef CONFIG_MOONWATER_WAYLAND
                compositor_wl_dmabuf_refresh();
#endif
        }
}

static void compositor_hand_back(void)
{
        gpu_forget((const void *)(compositor.scanout), (positive)(compositor.scanout_bytes));
        compositor_call(syscall(munmap), (positive)compositor.scanout, compositor.scanout_bytes, 0, 0, 0, 0);
        system_close(compositor.card);
}

static void *compositor_map_rw_file(string_address path, long bytes, int *error)
{
        long handle = (long)system_open_at_mode(AT_FDCWD, path, FILE_READ_WRITE | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0644), mapped;
        if (handle < 0) { if (error) *error = -COMPOSITOR_EINVAL; return null; }
        if (compositor_call(syscall(ftruncate), (positive)handle, (positive)bytes, 0, 0, 0, 0) < 0)
                { system_close((positive)handle); if (error) *error = -COMPOSITOR_EINVAL; return null; }
        mapped = compositor_call(syscall(mmap), 0, (positive)bytes, 3, 1, (positive)handle, 0);
        system_close((positive)handle);
        if (mapped < 0 && mapped > -4096) { if (error) *error = -COMPOSITOR_ENOSPC; return null; }
        return (void *)mapped;
}

static b32 compositor_headless_plate(string_address path, long bytes)
{
        static const char suffix[] = ".spare";
        unsigned long length = 0, at;
        void *mapped;
        while (path[length]) length++;
        if (length + sizeof(suffix) > sizeof(compositor_spare_name)) return 0;
        for (at = 0; at <= length; at++) compositor_scan_name[at] = compositor_spare_name[at] = path[at];
        for (at = 0; at < sizeof(suffix); at++) compositor_spare_name[length + at] = suffix[at];
        if (!(mapped = compositor_map_rw_file((string_address)compositor_spare_name, bytes, null))) return 0;
        compositor_spare.fb = 2, compositor_spare.handle = 0, compositor_spare.pixels = (unsigned int *)mapped;
        compositor_spare.bytes = (unsigned long)bytes, compositor_spare.pitch = compositor.scanout_pitch;
        compositor_stale_all(compositor_spare.stale, &compositor_spare.stale_any, &compositor_spare_parts);
        return 1;
}

static b32 compositor_headless(string_address size, string_address path)
{
        long width = 0, height = 0, bytes;
        void *mapped;
        int error = 0;
        while (*size >= '0' && *size <= '9' && width <= 16384) width = width * 10 + (*size++ - '0');
        if (*size++ != 'x') return -COMPOSITOR_EINVAL;
        while (*size >= '0' && *size <= '9' && height <= 16384) height = height * 10 + (*size++ - '0');
        if (width < 64 || width > 16384 || height < 64 || height > 16384) return -COMPOSITOR_EINVAL;
        bytes = width * height * 4;
        if (!(mapped = compositor_map_rw_file(path, bytes, &error))) return error;
        compositor.card = -1, compositor.scanout = (unsigned int *)mapped, compositor.scanout_bytes = (unsigned long)bytes;
        compositor.scanout_pitch = (unsigned long)width * 4, compositor.width = (int)width, compositor.height = (int)height;
        compositor.period = 1000000000ull / 60, compositor.fb = 1, compositor_plate_refused = 0;
        if (!compositor_headless_plate(path, bytes)) compositor_spare.fb = 0;
        compositor_damage(0, 0, compositor.width, compositor.height);
        compositor_present();
        return 0;
}

static b32 compositor_flag_exists(string_address path)
{
        bipolar h = system_open_at(AT_FDCWD, path, FILE_READ | O_CLOEXEC);
        if (h < 0) return 0;
        system_close(h);
        return 1;
}
static b32 compositor_option(string_address argument, char letter) { return argument[0] == '-' && argument[1] == letter && !argument[2]; }

static bipolar compositor_number(string_address text, b32 hex)
{
        bipolar value = 0, sign = 1;
        if (*text == '-') sign = -1, text++;
        for (; *text; text++)
        {
                b32 d = *text >= '0' && *text <= '9' ? *text - '0' : hex && (*text | 32) >= 'a' && (*text | 32) <= 'f' ? (*text | 32) - 'a' + 10 : -1;
                if (d < 0) break;
                value = value * (hex ? 16 : 10) + d;
        }
        return value * sign;
}

static positive compositor_map_file(string_address path, positive address_to size)
{
        bipolar h = system_open_at(AT_FDCWD, path, FILE_READ | O_CLOEXEC);
        long bytes, mapped;
        if (h < 0) return 0;
        bytes = compositor_call(syscall(lseek), (positive)h, 0, 2, 0, 0, 0);
        mapped = bytes > 0 ? compositor_call(syscall(mmap), 0, (positive)bytes, 1, 2, (positive)h, 0) : -1;
        system_close(h);
        if (mapped < 0 && mapped > -4096) return 0;
        address_to size = (positive)bytes;
        return (positive)mapped;
}

static void compositor_fallbacks(font_engine address_to engine)
{
        static const string_address paths[4] = {
                "/usr/share/fonts/noto-sans-arabic/NotoSansArabic-Regular.ttf",
                "/usr/share/fonts/noto-sans-arabic/NotoSansArabic-Bold.ttf",
                "/usr/share/fonts/noto-sans-hebrew/NotoSansHebrew-Regular.ttf",
                "/usr/share/fonts/noto-sans-hebrew/NotoSansHebrew-Bold.ttf"};
        static font_face faces[4];
        static b32 loaded[4], tried;
        if (!tried)
        {
                tried = 1;
                for (int at = 0; at < 4; at++)
                {
                        positive size = 0, bytes = compositor_map_file(paths[at], address_of size);
                        loaded[at] = bytes && font_load(&faces[at], (const p8 address_to)bytes, size, 0) == 0;
                }
        }
        for (int p = 0; p < 2; p++)
                if (loaded[2 * p]) font_fallback(engine, &faces[2 * p], loaded[2 * p + 1] ? &faces[2 * p + 1] : null);
}

static void compositor_font(string_address path)
{
        static p8 atlas[128 * 128];
        positive size = 0, bytes = path ? compositor_map_file(path, address_of size) : 0;
        unsigned long band_bytes = 0, glass_bytes = 0;
        void *engine, *band, *glass;
        if (!bytes) return;

        taskbar_bytes(compositor_most_width(), compositor_most_height(), compositor.scale, &band_bytes, &glass_bytes);
        engine = compositor_map_zeroed(sizeof(font_engine));
        band = compositor_map_zeroed(band_bytes);
        compositor.glass_bytes = glass_bytes, glass = compositor_map_zeroed(glass_bytes);
        if (!engine || !band || !glass || font_load(&compositor.face, (const p8 address_to)bytes, size, 0) != 0)
                { compositor_say("no font for the tabs, error", -COMPOSITOR_EINVAL); return; }

        char bold_path[512];
        positive at = 0, bold_size = 0, bold_bytes;
        while (path[at] && at < sizeof(bold_path) - 8) at++;
        if (path[at] == 0)
        {
                string_copy((string_address)bold_path, path);
                for (positive k = 0; k + 7 <= at; k++)
                        if (!memory_compare(bold_path + k, "Regular", 7))
                        {
                                string_copy((string_address)bold_path + k + 4, (string_address)path + k + 7);
                                memory_copy(bold_path + k, "Bold", 4);
                                bold_bytes = compositor_map_file((string_address)bold_path, address_of bold_size);
                                compositor.bold_ok = bold_bytes && font_load(&compositor.bold, (const p8 address_to)bold_bytes, bold_size, 0) == 0;
                                break;
                        }
        }
        compositor.engine = (font_engine address_to)engine;
        font_start(compositor.engine, atlas, 128, 128);
        compositor_fallbacks(compositor.engine);
        compositor.band = (unsigned int *)band, compositor.band_bytes = band_bytes, compositor.glass = (unsigned int *)glass;
        taskbar_changed();
        compositor_band_room();
}

static void compositor_screens_say(void)
{
        for (int at = 0, x, y, w, h; at < compositor_output_count(); at++)
        {
                compositor_output_rect(at, &x, &y, &w, &h);
                string_format(log, "compositor: screen %b %s %b by %b at %b,%b\n", (bipolar)at,
                              compositor_output_name(at)[0] ? (string_address)compositor_output_name(at) : (string_address) "-",
                              (bipolar)w, (bipolar)h, (bipolar)x, (bipolar)y);
        }
        log_flush();
}

static void compositor_unmap_pixels(unsigned int **pixels, unsigned long *bytes)
{
        if (*pixels) gpu_forget((const void *)(*pixels), (positive)(*bytes)), compositor_call(syscall(munmap), (positive)*pixels, *bytes, 0, 0, 0, 0);
        *pixels = 0, *bytes = 0;
}

static unsigned int *compositor_remap(unsigned int *was, unsigned long had, unsigned long bytes)
{
        unsigned int *mapped = (unsigned int *)compositor_map_zeroed(bytes);
        if (was) compositor_unmap_pixels(&was, &had);
        return mapped;
}

static void compositor_band_room(void)
{
        unsigned long glass_most = 0;
        int bands = compositor_bands();
        if (!compositor.band) return;
        for (int at = 0; at < WINDOWS_OUTPUTS; at++)
        {
                unsigned int **pixels = at ? &compositor.band_more[at] : &compositor.band;
                unsigned long *had = at ? &compositor.band_more_bytes[at] : &compositor.band_bytes, band_bytes = 0, glass_bytes = 0;
                int x, y, width, height;
                if (at >= bands) { compositor_unmap_pixels(pixels, had); continue; }
                compositor_output_rect(at, &x, &y, &width, &height);
                taskbar_bytes(width, height, compositor.scale, &band_bytes, &glass_bytes);
                if (glass_bytes > glass_most) glass_most = glass_bytes;
                if (band_bytes <= *had) continue;
                *pixels = compositor_remap(*pixels, *had, band_bytes), *had = *pixels ? band_bytes : 0;
                taskbar_changed();
                if (!at && !compositor.band) break;
        }
        if (compositor.band && glass_most > compositor.glass_bytes)
                compositor.glass = compositor_remap(compositor.glass, compositor.glass_bytes, glass_most), compositor.glass_bytes = compositor.glass ? glass_most : 0;
        if (!compositor.band || !compositor.glass)
        {
                for (int at = 0; at < WINDOWS_OUTPUTS; at++)
                        compositor_unmap_pixels(at ? &compositor.band_more[at] : &compositor.band, at ? &compositor.band_more_bytes[at] : &compositor.band_bytes);
                compositor_unmap_pixels(&compositor.glass, &compositor.glass_bytes);
                taskbar_changed();
        }
}

#define COMPOSITOR_DESCRIPTORS_WANTED (1L << 30)
#define COMPOSITOR_DESCRIPTORS_WARM   4096
#define COMPOSITOR_DESCRIPTORS_FIXED  (7 + 32 + 32 + 3 + 64 + 32)
#define COMPOSITOR_DESCRIPTORS_OWN    (COMPOSITOR_DESCRIPTORS_FIXED + 2 * (long)compositor_rows_in_use)
static long compositor_descriptors_most = 1024;
static int compositor_descriptor_crowded(long descriptor) { return descriptor >= compositor_descriptors_most - COMPOSITOR_DESCRIPTORS_OWN; }

static int compositor_app_is_steam(const char *app)
{
        return app && app[0] && (!string_compare_folded((string_address)app, (string_address) "steam") ||
                                 !string_compare_folded((string_address)app, (string_address) "steamwebhelper"));
}

static int compositor_row_is_steam(int row)
{
        for (int hops = 0; hops < 8 && row >= 0 && row < COMPOSITOR_ROWS; hops++)
        {
                const char *app = compositor.app_id[row][0] ? compositor.app_id[row] : compositor.table ? compositor.table->app_id[row] : 0;
                int next = compositor_popup_parent && compositor_popup_parent[row] >= 0 ? compositor_popup_parent[row]
                           : compositor_owner && compositor_owner[row] > 0              ? compositor_owner[row] - 1 : COMPOSITOR_NONE;
                if (compositor_app_is_steam(app)) return 1;
                if (next == row) break;
                row = next;
        }
        return 0;
}

#ifdef CONFIG_MOONWATER_WAYLAND
#include "wayland.c"
#include "wayland_host.c"
#include "xwm.c"

static int compositor_wl_started(void) { return compositor_wl.started; }
static int compositor_wl_dmabuf_missing(void) { return compositor_wl.started && !compositor_wl.render; }

static int compositor_picture_poll(int row)
{
        struct wayland_cursor picture;
        unsigned long was = compositor_picture.worn;
        if (row < 0 || row >= COMPOSITOR_ROWS || !compositor_wl.started ||
            (compositor_pointer_dynamic() && compositor_row_is_steam(row)) || !wayland_cursor_picture(row, &picture))
                { compositor_picture.worn = compositor_picture.asked = 0; return was != 0; }
        if (!compositor_picture.asked || picture.serial != compositor_picture.serial)
        {
                compositor_picture.asked = 1, compositor_picture.serial = picture.serial;
                compositor_picture.worn = compositor_picture_take(picture.pixels, picture.stride, picture.width, picture.height,
                                                                  picture.format == WLN_wl_shm_format_xrgb8888, picture.hot_x, picture.hot_y,
                                                                  picture.scale, compositor.scale);
        }
        return compositor_picture.worn != was;
}
#endif

#ifndef COMPOSITOR_WL_POLLS
#define COMPOSITOR_WL_POLLS 0
#endif
#ifndef COMPOSITOR_XWM_POLLS
#define COMPOSITOR_XWM_POLLS 0
#endif
#define COMPOSITOR_POLLS_OWN 7
#define COMPOSITOR_POLLS_FOR(rows) (COMPOSITOR_POLLS_OWN + COMPOSITOR_INPUTS + (unsigned long)(rows) + COMPOSITOR_WL_POLLS + COMPOSITOR_XWM_POLLS)
#define COMPOSITOR_POLLS_MOST (1L << 26)
static struct compositor_poll *compositor_polls_at;
static long compositor_polls_room;

static struct compositor_poll *compositor_polls(void)
{
        long want = (long)COMPOSITOR_POLLS_FOR(compositor_rows_in_use), room;
        if (want <= compositor_polls_room) return compositor_polls_at;
        if (want > COMPOSITOR_POLLS_MOST) return 0;
        if (!compositor_polls_at)
        {
                long got = compositor_call(syscall(mmap), 0, (positive)(COMPOSITOR_POLLS_MOST * (long)sizeof(struct compositor_poll)), 0, 0x4022, (positive)-1, 0);
                if (got < 0 && got > -4096) return 0;
                compositor_polls_at = (struct compositor_poll *)got;
        }
        room = compositor_polls_room ? 2 * compositor_polls_room : 1024;
        room = room < want ? want : room > COMPOSITOR_POLLS_MOST ? COMPOSITOR_POLLS_MOST : room;
        if (compositor_call(syscall(mprotect), (positive)compositor_polls_at, (positive)((room * (long)sizeof(struct compositor_poll) + 4095) & ~4095L), 3, 0, 0, 0) != 0)
                return 0;
        compositor_polls_room = room;
        return compositor_polls_at;
}
_Static_assert(COMPOSITOR_POLLS_OWN + COMPOSITOR_INPUTS_KEPT + COMPOSITOR_CLIP_HISTORY + 3 + 64 <= COMPOSITOR_DESCRIPTORS_FIXED,
               "the compositor's own descriptors fit the numbers kept for them");

/* Single PROT_NONE/MAP_NORESERVE reservation for all per-row arrays, committed lazily by compositor_rows_make. */
#define COMPOSITOR_ROWS_ROW(rows)     (rows)
#define COMPOSITOR_ROWS_ROW1(rows)    (WINDOWS + 1)
#define COMPOSITOR_ROWS_SCREENS(rows) WINDOWS_OUTPUTS
#define COMPOSITOR_ROWS_FRAMES(rows)  COMPOSITOR_FRAMES_FOR(rows)
#define COMPOSITOR_ROWS_PIECES(rows)  COMPOSITOR_PIECES_FOR(rows)
#define COMPOSITOR_ROWS_EDGES(rows)   (2 * COMPOSITOR_PIECES_FOR(rows) + 2)
#define COMPOSITOR_ROWS_SPANS(rows)   (COMPOSITOR_PIECES_FOR(rows) + 1)
#define COMPOSITOR_ROWS_LANE(X, i)    X(compositor_lane_edge[i], COMPOSITOR_ROWS_EDGES) X(compositor_lane_span[i], COMPOSITOR_ROWS_SPANS)

#define COMPOSITOR_ROWS_OWN(X) \
        X(compositor.scan_fb, COMPOSITOR_ROWS_ROW) X(compositor.scan_handle, COMPOSITOR_ROWS_ROW) X(compositor.taken_held, COMPOSITOR_ROWS_ROW) \
        X(compositor.patched, COMPOSITOR_ROWS_ROW) X(compositor.door, COMPOSITOR_ROWS_ROW) X(compositor.wake, COMPOSITOR_ROWS_ROW) \
        X(compositor.seen, COMPOSITOR_ROWS_ROW) X(compositor.runs, COMPOSITOR_ROWS_ROW) X(compositor.runs_bytes, COMPOSITOR_ROWS_ROW) \
        X(compositor.runs_tag, COMPOSITOR_ROWS_ROW) X(compositor.runs_composed, COMPOSITOR_ROWS_ROW) X(compositor.runs_state, COMPOSITOR_ROWS_ROW) \
        X(compositor.showing, COMPOSITOR_ROWS_ROW) X(compositor.landing, COMPOSITOR_ROWS_ROW) X(compositor.land_at, COMPOSITOR_ROWS_ROW) \
        X(compositor.glide, COMPOSITOR_ROWS_ROW) X(compositor.glide_from, COMPOSITOR_ROWS_ROW) X(compositor.glide_at, COMPOSITOR_ROWS_ROW) \
        X(compositor.glide_base, COMPOSITOR_ROWS_ROW) X(compositor.made, COMPOSITOR_ROWS_ROW) X(compositor.x, COMPOSITOR_ROWS_ROW) \
        X(compositor.y, COMPOSITOR_ROWS_ROW) X(compositor.w, COMPOSITOR_ROWS_ROW) X(compositor.h, COMPOSITOR_ROWS_ROW) \
        X(compositor.front, COMPOSITOR_ROWS_ROW) X(compositor.state, COMPOSITOR_ROWS_ROW) X(compositor.close_asks, COMPOSITOR_ROWS_ROW) \
        X(compositor.picture_w, COMPOSITOR_ROWS_ROW) X(compositor.picture_h, COMPOSITOR_ROWS_ROW) X(compositor.asking, COMPOSITOR_ROWS_ROW) \
        X(compositor.arranged, COMPOSITOR_ROWS_ROW) X(compositor.fullscreen, COMPOSITOR_ROWS_ROW) X(compositor.fullscreen_was, COMPOSITOR_ROWS_ROW) \
        X(compositor.row_space, COMPOSITOR_ROWS_ROW) X(compositor.row_space_prev, COMPOSITOR_ROWS_ROW) \
        X(compositor.layer, COMPOSITOR_ROWS_ROW) X(compositor.edges, COMPOSITOR_ROWS_ROW) X(compositor.far_x, COMPOSITOR_ROWS_ROW) \
        X(compositor.far_y, COMPOSITOR_ROWS_ROW) X(compositor.saved_x, COMPOSITOR_ROWS_ROW) X(compositor.saved_y, COMPOSITOR_ROWS_ROW) \
        X(compositor.saved_w, COMPOSITOR_ROWS_ROW) X(compositor.saved_h, COMPOSITOR_ROWS_ROW) X(compositor.title, COMPOSITOR_ROWS_ROW) \
        X(compositor.app_id, COMPOSITOR_ROWS_ROW) X(compositor.icon_seq, COMPOSITOR_ROWS_ROW) X(compositor.icon, COMPOSITOR_ROWS_ROW) \
        X(compositor.name, COMPOSITOR_ROWS_ROW) X(compositor.moved, COMPOSITOR_ROWS_ROW) X(compositor.stack, COMPOSITOR_ROWS_ROW) \
        X(compositor.listed, COMPOSITOR_ROWS_ROW) X(compositor.marks, COMPOSITOR_ROWS_ROW) X(compositor.lengths, COMPOSITOR_ROWS_ROW) \
        X(compositor.banded, COMPOSITOR_ROWS_ROW) X(compositor.glassy, COMPOSITOR_ROWS_ROW) X(compositor.asks, COMPOSITOR_ROWS_ROW) \
        X(compositor.composed_fd, COMPOSITOR_ROWS_ROW) X(compositor.composed_bytes, COMPOSITOR_ROWS_ROW) \
        X(compositor.backdrop, COMPOSITOR_ROWS_ROW) X(compositor.tinted, COMPOSITOR_ROWS_ROW) X(compositor.composed, COMPOSITOR_ROWS_ROW) \
        X(compositor.boosted, COMPOSITOR_ROWS_ROW) X(compositor.glass_sum, COMPOSITOR_ROWS_ROW) X(compositor.glass_blocks, COMPOSITOR_ROWS_ROW) \
        X(compositor.glass_cut, COMPOSITOR_ROWS_ROW) X(compositor.glass_made, COMPOSITOR_ROWS_ROW) X(compositor.stale, COMPOSITOR_ROWS_ROW) \
        X(compositor.dirty, COMPOSITOR_ROWS_ROW) X(compositor.glass_side, COMPOSITOR_ROWS_ROW) X(compositor.glass_told, COMPOSITOR_ROWS_ROW) \
        X(compositor.lifted, COMPOSITOR_ROWS_ROW) X(compositor.hold_rect, COMPOSITOR_ROWS_ROW) X(compositor.hold_how, COMPOSITOR_ROWS_ROW) \
        X(compositor.shared_sizes, COMPOSITOR_ROWS_ROW) X(compositor.osk_pan, COMPOSITOR_ROWS_ROW) X(compositor.osk_height, COMPOSITOR_ROWS_ROW) \
        X(compositor.osk_edges, COMPOSITOR_ROWS_ROW) X(compositor.text_purpose, COMPOSITOR_ROWS_ROW) X(compositor.caret, COMPOSITOR_ROWS_ROW) \
        X(compositor.band_marks, COMPOSITOR_ROWS_SCREENS)

#ifdef CONFIG_MOONWATER_WAYLAND
#define COMPOSITOR_ROWS_WL(X) \
        X(compositor_wl.laying, COMPOSITOR_ROWS_ROW) X(compositor_wl.parts, COMPOSITOR_ROWS_ROW) X(compositor_wl.cursor_asked, COMPOSITOR_ROWS_ROW) \
        X(compositor_wl.row_blend, COMPOSITOR_ROWS_ROW) X(compositor_wl.row_opaque, COMPOSITOR_ROWS_ROW) X(compositor_wl.lent, COMPOSITOR_ROWS_ROW) \
        X(compositor_wl.straight, COMPOSITOR_ROWS_ROW) X(compositor_wl.source, COMPOSITOR_ROWS_ROW) X(compositor_wl.wc_reads, COMPOSITOR_ROWS_ROW) \
        X(compositor_wl.wc_copy, COMPOSITOR_ROWS_ROW) X(compositor_wl_layers.layer, COMPOSITOR_ROWS_ROW) X(compositor_wl_layers.gathered, COMPOSITOR_ROWS_ROW) \
        X(compositor_wl_layers.count, COMPOSITOR_ROWS_ROW) X(compositor_wl_layers.root_refused, COMPOSITOR_ROWS_ROW)
#else
#define COMPOSITOR_ROWS_WL(X)
#endif

#define COMPOSITOR_ROWS_CARD(X) X(compositor_card.list_seq, COMPOSITOR_ROWS_ROW)

#define COMPOSITOR_ROWS_KEPT(X) \
        X(compositor_popup_parent, COMPOSITOR_ROWS_ROW) X(compositor_owner, COMPOSITOR_ROWS_ROW) X(compositor_listed, COMPOSITOR_ROWS_ROW) \
        X(compositor_list_waits, COMPOSITOR_ROWS_ROW) X(compositor_clip.offer_seen, COMPOSITOR_ROWS_ROW) X(compositor_clip.ask_seen, COMPOSITOR_ROWS_ROW) \
        X(compositor_clip.gesture, COMPOSITOR_ROWS_ROW) X(compositor_dnd.offer_seen, COMPOSITOR_ROWS_ROW) X(compositor_dnd.take_seen, COMPOSITOR_ROWS_ROW) \
        X(compositor_dnd.done_seen, COMPOSITOR_ROWS_ROW) X(compositor_gpu_pins, COMPOSITOR_ROWS_ROW) X(compositor_osk_held, COMPOSITOR_ROWS_ROW) \
        X(compositor_doors.asked, COMPOSITOR_ROWS_ROW1) X(compositor_record_shots[0].rects, COMPOSITOR_ROWS_ROW) \
        X(compositor_record_shots[1].rects, COMPOSITOR_ROWS_ROW) X(compositor_record_shots[2].rects, COMPOSITOR_ROWS_ROW) \
        X(compositor_record_pending, COMPOSITOR_ROWS_ROW) X(compositor_tab_titles, COMPOSITOR_ROWS_ROW) X(compositor_tab_apps, COMPOSITOR_ROWS_ROW) \
        X(compositor_tab_icons, COMPOSITOR_ROWS_ROW) X(compositor_tab_seqs, COMPOSITOR_ROWS_ROW) X(compositor_present_frame_room, COMPOSITOR_ROWS_FRAMES) \
        X(compositor_present_row_room, COMPOSITOR_ROWS_LAYERED) X(compositor_scene_radii, COMPOSITOR_ROWS_FRAMES) \
        X(compositor_plan_box, COMPOSITOR_ROWS_PIECES) X(compositor_plan_owner, COMPOSITOR_ROWS_PIECES) X(compositor_plan_edge, COMPOSITOR_ROWS_EDGES) \
        COMPOSITOR_ROWS_LANE(X, 0) COMPOSITOR_ROWS_LANE(X, 1) COMPOSITOR_ROWS_LANE(X, 2) COMPOSITOR_ROWS_LANE(X, 3) COMPOSITOR_ROWS_LANE(X, 4) \
        COMPOSITOR_ROWS_LANE(X, 5) COMPOSITOR_ROWS_LANE(X, 6) COMPOSITOR_ROWS_LANE(X, 7) COMPOSITOR_ROWS_LANE(X, 8)

#ifdef MOONWATER_GPU_C
#define COMPOSITOR_ROWS_GPU(X) X(compositor_gpu_scaled_copy, COMPOSITOR_ROWS_FRAMES)
#else
#define COMPOSITOR_ROWS_GPU(X)
#endif

#define COMPOSITOR_ROWS_ALL(X) COMPOSITOR_ROWS_OWN(X) COMPOSITOR_ROWS_WL(X) COMPOSITOR_ROWS_CARD(X) COMPOSITOR_ROWS_GPU(X) COMPOSITOR_ROWS_KEPT(X)

static unsigned char *compositor_row_at;
static unsigned long compositor_row_bytes;
static unsigned long compositor_row_round(unsigned long bytes) { return (bytes + 4095ul) & ~4095ul; }
static int compositor_row_commit(void *at, unsigned long bytes)
{
        return !bytes || compositor_call(syscall(mprotect), (positive)at, (positive)compositor_row_round(bytes), 3, 0, 0, 0) == 0;
}

#define COMPOSITOR_ROWS_SIZE(a, k)   bytes += compositor_row_round((unsigned long)k(WINDOWS) * sizeof(*(a)));
#define COMPOSITOR_ROWS_POINT(a, k)  (a) = (__typeof__(a))(void *)(compositor_row_at + at), at += compositor_row_round((unsigned long)k(WINDOWS) * sizeof(*(a)));
#define COMPOSITOR_ROWS_COMMIT(a, k) made = made && compositor_row_commit((a), (unsigned long)k(rows) * sizeof(*(a)));
#define COMPOSITOR_ROWS_ZERO(a, k)   memory_zero((a), (unsigned long)k(compositor_rows_made) * sizeof(*(a)));

static void compositor_rows_point(void) { unsigned long at = 0; if (compositor_row_at) { COMPOSITOR_ROWS_ALL(COMPOSITOR_ROWS_POINT) } }
static void compositor_row_defaults(unsigned int row) { compositor_popup_parent[row] = -1; }

static int compositor_rows_make(unsigned int rows)
{
        int made = 1;
        if (rows > WINDOWS) return 0;
        if (!compositor_row_at)
        {
                unsigned long bytes = 0;
                long got;
                COMPOSITOR_ROWS_ALL(COMPOSITOR_ROWS_SIZE)
                got = compositor_call(syscall(mmap), 0, (positive)bytes, 0, 0x4022, (positive)-1, 0);
                if (got < 0 && got > -4096) return 0;
                compositor_row_at = (unsigned char *)got, compositor_row_bytes = bytes;
                compositor_rows_point();
#ifdef CONFIG_MOONWATER_WAYLAND
                compositor_wl_register_hooks();
                compositor_xwm_register_hooks();
#endif
        }
        if (rows <= compositor_rows_made) return 1;
        COMPOSITOR_ROWS_ALL(COMPOSITOR_ROWS_COMMIT)
        if (!made) return 0;
        for (unsigned int row = compositor_rows_made; row < rows; row++) compositor_row_defaults(row);
        compositor_rows_made = rows;
        return 1;
}

static void compositor_zero(void)
{
        memory_zero(&compositor, sizeof(compositor));
        compositor.active_grab.row = COMPOSITOR_NONE;
        compositor.scenes.press_row = COMPOSITOR_NONE;
        compositor_rows_point();
        compositor_rows_make(compositor_rows_in_use);
        COMPOSITOR_ROWS_OWN(COMPOSITOR_ROWS_ZERO)
        compositor_owed_reset();
}

static void compositor_card_zero(void)
{
        memory_zero(&compositor_card, sizeof(compositor_card));
        compositor_rows_point();
        COMPOSITOR_ROWS_CARD(COMPOSITOR_ROWS_ZERO)
}

static long compositor_door_make(unsigned int row)
{
        p8 name[48];
        for (int wake = 0; wake < 2; wake++)
        {
                long handle;
                windows_door_name((char *)name, row, wake);
                compositor_call(syscall(unlinkat), (positive)AT_FDCWD, (positive)name, 0, 0, 0, 0);
                compositor_call(syscall(mknodat), (positive)AT_FDCWD, (positive)name, 0010000 | WINDOWS_MODE, 0, 0, 0);
                compositor_call(syscall(fchmodat), (positive)AT_FDCWD, (positive)name, WINDOWS_MODE, 0, 0, 0);
                handle = (long)system_open_at(AT_FDCWD, name, (wake ? FILE_READ_WRITE : FILE_READ) | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW);
                if (handle >= 0 && handle >= compositor_descriptors_most - COMPOSITOR_DESCRIPTORS_FIXED)
                        system_close(handle), handle = -24;
                if (handle < 0)
                {
                        if (wake) system_close(compositor.door[row]), compositor.door[row] = -1;
                        return handle;
                }
                *(wake ? &compositor.wake[row] : &compositor.door[row]) = (int)handle;
        }
        return 0;
}

static unsigned int compositor_rows_grow(unsigned int want)
{
        unsigned int row = compositor_rows_in_use, to = want < compositor_rows_room ? want : compositor_rows_room;
        long refused = 0;
        if (!compositor.table || to <= row) return compositor_rows_in_use;
        if (!compositor_rows_make(to)) refused = -12;
        for (; !refused && row < to; row++)
                if ((refused = compositor_door_make(row)) < 0) break;
        if (refused)
                string_format(log, "compositor: rows made to %p of the %p asked, the kernel refused the next (error %b)\n",
                              (positive)row, (positive)to, (bipolar)refused), log_flush();
        if (row > compositor_rows_in_use)
                compositor_rows_in_use = row, __atomic_store_n(&compositor.table->rows, row, __ATOMIC_RELEASE);
        return compositor_rows_in_use;
}

// The table's rows_wanted as the compositor last saw it: a client that asked for rows (windows_rows_ask) grows them here, once per ask.
static unsigned int compositor_rows_asked;

static void compositor_rows_spare(void)
{
        unsigned int free = 0, step = compositor_rows_in_use / 2 > COMPOSITOR_ROWS_SPARE ? compositor_rows_in_use / 2 : COMPOSITOR_ROWS_SPARE;
        unsigned int wanted;

        if (!compositor.table || compositor_rows_in_use >= compositor_rows_room) return;
        wanted = __atomic_load_n(&compositor.table->rows_wanted, __ATOMIC_ACQUIRE);
        if (wanted != compositor_rows_asked)
        {
                compositor_rows_asked = wanted;
                compositor_rows_grow(compositor_rows_in_use + step);
                return;
        }
        for (unsigned int row = compositor_rows_in_use; row-- > 0 && free < COMPOSITOR_ROWS_SPARE;)
                free += __atomic_load_n(&compositor.table->pid[row], __ATOMIC_ACQUIRE) == 0;
        if (free < COMPOSITOR_ROWS_SPARE) compositor_rows_grow(compositor_rows_in_use + step);
}

static void compositor_forward(void)
{
        char byte = 0;
        if (!rows_any(&compositor.woken)) return;
        for (unsigned int w = 0; w < compositor_rows_words(); w++)
                for (int row = (int)w * 64; compositor.woken.word[w] && row < (int)(w + 1) * 64 && row < (int)compositor_rows_in_use; row++)
                {
                        if (!rows_has(&compositor.woken, row)) continue;
#ifdef CONFIG_MOONWATER_WAYLAND
                        if (compositor_wl.started && wayland_row_owner(row) >= 0) continue;
#endif
                        rows_del(&compositor.woken, row);
                        if (!compositor_door_ask((unsigned int)row, COMPOSITOR_DOOR_WAKE))
                                system_write_once(compositor.wake[row], &byte, 1);
                }
}

static void compositor_inputs_turn(const struct compositor_poll *wanted)
{
        compositor_sling_only = 1, compositor_sling_sent = 0;
        if (compositor_reader.on)
        {
                if (wanted[0].returned || __atomic_load_n(&compositor_reader.handed, __ATOMIC_ACQUIRE) != compositor_reader.reading)
                        compositor_reader_take();
        }
        else
                for (unsigned int at = 0; at < compositor.input_count; at++)
                        if (wanted[at].returned) compositor_input_read(at);
        compositor_forward();
}

static unsigned int compositor_inputs_polled(void) { return compositor_reader.on ? 1 : compositor.input_count; }

static void compositor_descriptors_room(void)
{
        positive limits[2] = {0, 0};
        if (compositor_call(syscall(prlimit64), 0, 7, 0, (positive)limits, 0, 0) == 0)
        {
                positive most = limits[1] < COMPOSITOR_DESCRIPTORS_WANTED ? limits[1] : COMPOSITOR_DESCRIPTORS_WANTED;
                if (limits[0] < most)
                {
                        positive raised[2] = {most, limits[1]};
                        if (compositor_call(syscall(prlimit64), 0, 7, (positive)raised, 0, 0, 0) == 0) limits[0] = most;
                }
                compositor_descriptors_most = (long)(limits[0] < COMPOSITOR_DESCRIPTORS_WANTED ? limits[0] : COMPOSITOR_DESCRIPTORS_WANTED);
        }
        for (long last = (compositor_descriptors_most < COMPOSITOR_DESCRIPTORS_WARM ? compositor_descriptors_most : COMPOSITOR_DESCRIPTORS_WARM) - 1;
             last >= 127; last = (last - 1) / 2)
        {
                long made = compositor_call(syscall(fcntl), 0, COMPOSITOR_F_DUPFD_CLOEXEC, (positive)last, 0, 0, 0);
                if (made >= 0) { system_close(made); return; }
        }
}

static void compositor_up_say(int descriptor)
{
        if (descriptor >= 3) system_write_once(descriptor, "up\n", 3), system_close(descriptor);
}

static void compositor_loop_wanted(struct compositor_poll *wanted, struct compositor_slots *slots, b32 held)
{
        unsigned int count = 0;
        memory_zero(slots, sizeof(*slots));
        wanted[count++] = (struct compositor_poll){compositor.signals, COMPOSITOR_POLLIN, 0};
        wanted[count++] = (struct compositor_poll){compositor.watch, COMPOSITOR_POLLIN, 0};
        slots->inputs = count;
        if (compositor_reader.on)
                wanted[count++] = (struct compositor_poll){compositor_reader.knock, COMPOSITOR_POLLIN, 0};
        else
                for (unsigned int at = 0; at < compositor.input_count; at++)
                        wanted[count++] = (struct compositor_poll){compositor.inputs[at], COMPOSITOR_POLLIN, 0};
        slots->doors = count;
        for (unsigned int row = 0; row < compositor_rows_in_use; row++)
                wanted[count++] = (struct compositor_poll){held || rows_has(&compositor_doors.busy, row) ? -1 : __atomic_load_n(&compositor.door[row], __ATOMIC_ACQUIRE), COMPOSITOR_POLLIN, 0};
        if (compositor_doors.done >= 0 && !held) slots->done_at = count, wanted[count++] = (struct compositor_poll){compositor_doors.done, COMPOSITOR_POLLIN, 0};
        if (compositor.flip_pending) slots->card_at = count, wanted[count++] = (struct compositor_poll){compositor.card, COMPOSITOR_POLLIN, 0};
#ifdef MOONWATER_GPU_C
        if (compositor_card_awaits()) slots->fence_at = count, wanted[count++] = (struct compositor_poll){compositor_card_poll(), COMPOSITOR_POLLIN, 0};
#endif
        if (compositor.uevents >= 0) slots->uevents_at = count, wanted[count++] = (struct compositor_poll){compositor.uevents, COMPOSITOR_POLLIN, 0};
        if (compositor_clip.door >= 0 && !rows_has(&compositor_doors.busy, COMPOSITOR_DOOR_MISC))
                slots->clip_at = count, wanted[count++] = (struct compositor_poll){compositor_clip.door, COMPOSITOR_POLLIN, 0};
#ifdef CONFIG_MOONWATER_WAYLAND
        if (!held)
                slots->wayland_at = count, count = compositor_wl_poll(wanted, count),
                slots->xwm_at = count, count = compositor_xwm_poll(wanted, count, count);
#endif
        slots->count = count;
}

static inline void compositor_soon(positive *until, positive due) { if (due && (!*until || due < *until)) *until = due; }

static positive compositor_minute_due;
static unsigned int compositor_clock_lang;

static inline int compositor_minute_check(positive now)
{
        unsigned int lang = compositor.table ? compositor.table->language : 0;

        if (!compositor_minute_due || now >= compositor_minute_due || lang != compositor_clock_lang)
        {
                timespec real = {0, 0};

                clock_gettime(CLOCK_REALTIME, address_of real);
                compositor_minute_due = now + (positive)(59 - real.tv_sec % 60) * 1000000000ull + (positive)(1000000000l - real.tv_nsec) + 1000000ull;
                compositor_clock_lang = lang;
                return 1;
        }
        return 0;
}

// Earliest active loop deadline for the current turn (0 = wait forever).
static positive compositor_timers_arm(positive now, unsigned int fence_at, int held)
{
        positive period = compositor.period ? compositor.period : 16666667ull, until = 0;
        (void)fence_at, (void)held;
        if (compositor.band || compositor.theme_word == 2)
        {
                if (!compositor_minute_due || now >= compositor_minute_due ||
                    (compositor.table && compositor.table->language != compositor_clock_lang))
                {
                        if (compositor.band && taskbar_clock_stale())
                                compositor_soon(&until, now);
                        else
                        {
                                (void)compositor_minute_check(now);
                                compositor_soon(&until, compositor_minute_due);
                        }
                }
                else
                        compositor_soon(&until, compositor_minute_due);
        }
        if (compositor.screens_gone && !compositor.screens_look) compositor.screens_look = now + COMPOSITOR_SCREEN_LOOK;
        if (compositor.flip_pending)
        {
                positive waiting = compositor_flip_wait(now);
                compositor_soon(&until, waiting ? waiting : compositor.flip_issued + COMPOSITOR_FLIP_LOST);
        }
        else if ((compositor.damages || compositor.vrr_kick) && !compositor.dark && !compositor_card_holds())
        {
                int tear = compositor_tearing();
                compositor_soon(&until, compositor_present_at(tear, tear ? 0 : compositor_present_lead(), now));
        }
        if (compositor.wake_stage) compositor_soon(&until, compositor.wake_at);
        if (compositor.hold >= 0) compositor_soon(&until, compositor.hold_until);
        if (compositor.cursor.magnified) compositor_soon(&until, compositor.cursor.magnified_until);
        compositor_soon(&until, compositor_cursor_due());
        if (compositor.band)
        {
                compositor_soon(&until, compositor_volume.look_at ? compositor_volume.look_at : 1);
                compositor_soon(&until, taskbar_moon_due(now));
                compositor_soon(&until, taskbar_starting_due());
        }
        compositor_soon(&until, compositor_predict_due());
        compositor_soon(&until, compositor_touch_due());
        compositor_soon(&until, compositor_snap_due());
        if (compositor_spots_any(1)) compositor_soon(&until, compositor.presented + period > now ? compositor.presented + period : now);
        compositor_soon(&until, compositor_wall_due(now));
        // A list waiting on a busy card wakes the loop when the card's fence does; a card with no fence to wait on is looked at each millisecond.
        if (rows_any(&compositor.list_later) && !fence_at) compositor_soon(&until, now + 1000000ull);
        if (compositor.screens_gone) compositor_soon(&until, compositor.screens_look);
#ifdef CONFIG_MOONWATER_WAYLAND
        if (compositor_wl.started)
        {
                if (wayland_frame_wanted() && !compositor.damages && !compositor_card.on && !compositor.flip_pending && !compositor_wl_frame_owed)
                        compositor_soon(&until, now - compositor.presented < period ? compositor.presented + period : now);
                compositor_soon(&until, compositor_wl_due());
                compositor_soon(&until, compositor_wl_frame_at);
                compositor_soon(&until, compositor_wl_callbacks_due());
        }
#endif
#ifdef MOONWATER_GPU_C
        if (fence_at) compositor_soon(&until, compositor_card_due(now));
        if (held) compositor_soon(&until, compositor_card_held_until());
        compositor_soon(&until, compositor_gpu_late_at(now));
        compositor_soon(&until, compositor_gpu_clocks_at());
#endif
        return until;
}

static long compositor_loop_wait(struct compositor_poll *wanted, unsigned int count, const long *wait, b32 again, b32 held)
{
        struct compositor_epoll_event ready[COMPOSITOR_EPOLL_READY];
        unsigned int forgets, turn;
        long got, awaited = 0;

        if (held)
        {
                for (unsigned int at = 0; at < count; at++) wanted[at].returned = 0;
                goto ppoll;
        }
        if (compositor_epoll_init() < 0) goto ppoll;
        forgets = __atomic_load_n(&compositor_epoll.forgets, __ATOMIC_ACQUIRE);
        if (again && compositor_epoll.whole && count == compositor_epoll.count && forgets == compositor_epoll.forgets_seen)
        {
                for (unsigned int k = 0; k < compositor_epoll.answered; k++) wanted[compositor_epoll.answers[k]].returned = 0;
                goto wait;
        }
        compositor_epoll.whole = 0, compositor_epoll.count = count, compositor_epoll.forgets_seen = forgets;
        turn = ++compositor_epoll.turn;
        for (unsigned int at = 0; at < count; at++)
        {
                int fd = wanted[at].handle, existing;
                wanted[at].returned = 0;
                if (fd < 0) continue;
                existing = compositor_epoll_find(fd);
                if ((existing >= 0 && compositor_epoll.entries[existing].gen == turn) || compositor_epoll_watch(fd, (unsigned short)wanted[at].events, at) < 0)
                        goto ppoll;
        }
        for (unsigned int k = 0; k < compositor_epoll.active;)
                if (compositor_epoll.entries[k].gen == turn) k++;
                else compositor_epoll_drop(compositor_epoll.entries[k].fd);
        compositor_epoll.whole = 1;
wait:
        turn = compositor_epoll.turn;
        if ((got = compositor_epoll_wait(ready, COMPOSITOR_EPOLL_READY, wait)) == -38) goto ppoll;
        compositor_epoll.answered = 0;
        if (got < 0) return got;
        for (long k = 0; k < got; k++)
        {
                int idx = compositor_epoll_find((int)ready[k].data);
                if (idx >= 0 && compositor_epoll.entries[idx].gen == turn)
                {
                        unsigned int slot = compositor_epoll.entries[idx].slot;
                        compositor_epoll.answers[compositor_epoll.answered] = slot;
                        compositor_epoll.answered += !wanted[slot].returned, awaited += !wanted[slot].returned;
                        wanted[slot].returned = (short)(wanted[slot].returned | (short)(ready[k].events & 0xffffu));
                }
        }
        return awaited;
ppoll:
        return compositor_call(syscall(ppoll), (positive)wanted, count, (positive)wait, 0, 0, 0);
}

#ifdef CONFIG_MOONWATER_WAYLAND
static b32 compositor_shot_ok(const struct compositor_slots *slots, positive now, positive until)
{
        return !slots->fence_at && !compositor.cursor.magnify && !compositor.cursor.magnified && !(until && now >= until) &&
               !(compositor.flip_pending && (now < compositor.flip_issued || now - compositor.flip_issued >= COMPOSITOR_FLIP_LOST / 2));
}

static b32 compositor_slingshot(const struct compositor_poll *wanted, const struct compositor_slots *slots, long awaited, positive now, positive until)
{
        long answered = 0;
        if (!compositor_sling_only || !compositor_sling_sent || !compositor_shot_ok(slots, now, until) ||
            slots->doors - slots->inputs != compositor_inputs_polled())
                return 0;
        for (unsigned int at = 0; at < slots->count; at++)
                if (wanted[at].returned)
                {
                        if (at < slots->inputs || at >= slots->doors) return 0;
                        answered++;
                }
        if (answered != awaited) return 0;
        for (unsigned int at = 0; at < compositor_inputs_polled(); at++)
                if (wanted[slots->inputs + at].handle != (compositor_reader.on ? compositor_reader.knock : compositor.inputs[at])) return 0;
        for (long slot = 0; compositor_wl.started && slot < compositor_wl.slots; slot++)
                if (compositor_wl.fd[slot] >= 0 && wayland_out_dirty(slot)) return 0;
        return 1;
}

static b32 compositor_quietshot(const struct compositor_poll *wanted, const struct compositor_slots *slots, long awaited, positive now, positive until)
{
        return compositor_wl.started && slots->wayland_at && compositor_shot_ok(slots, now, until) &&
               compositor_wl_quiet_read(wanted + slots->wayland_at, awaited, now);
}
#endif

static void compositor_present_pass(void)
{
        if (compositor.damages && !compositor.flip_pending && !compositor_card_holds())
        {
                int tearing = compositor_tearing();
                positive stamp = clock_monotonic_nanoseconds(), lead = tearing ? 0 : compositor_present_lead();
                if (stamp >= compositor_present_at(tearing, lead, stamp) && !compositor_flip_wait(stamp))
                {
                        positive after;
                        compositor_late_begin(lead, stamp);
                        compositor.presented = compositor_late.aim ? stamp : compositor_frame_dated(compositor.presented, compositor.period, stamp);
                        compositor_vrr_keep();
                        compositor_card.looped = 1;
#ifdef CONFIG_MOONWATER_WAYLAND
                        compositor_wl_latch();
#endif
                        compositor_present();
                        compositor_card.looped = 0;
                        after = clock_monotonic_nanoseconds();
#ifdef CONFIG_MOONWATER_WAYLAND
                        if (!compositor_card.on) compositor_wl_presented();
#endif
                        compositor_land_keep(), compositor_glide_keep(), compositor_taskbar_keep(after);
                        compositor_preview_keep(), compositor_overlays_keep();
                        if (compositor.motion_ts) compositor_latency(1, compositor.motion_ts, after), compositor.motion_ts = 0;
#ifdef CONFIG_MOONWATER_WAYLAND
                        {
                                int jit = compositor_wl_jit_row();
                                if (!compositor_card.on && compositor.flip_pending && jit >= 0 &&
                                    (compositor.direct_row < 0 || compositor.flip_row == compositor.direct_row) &&
                                    !compositor_wl_row_async(jit))
                                {
                                        compositor_wl_jit_measure(stamp);
                                        compositor_wl_frame_owed = 1;
                                }
                                else if (!compositor_card.on)
                                {
                                        if (jit < 0) compositor_wl_game_render_ns = 0, compositor_wl_jit_sent(0);
                                        compositor_wl_frame();
                                }
                        }
#endif
                }
        }
#ifdef CONFIG_MOONWATER_WAYLAND
        else if (!compositor.damages && compositor_wl.started && wayland_frame_wanted() && !compositor_card.on)
        {
                positive stamp = clock_monotonic_nanoseconds(), period = compositor.period ? compositor.period : 16666667ull;
                if (stamp - compositor.presented >= period)
                {
                        compositor_wl_idle();
                        if (!compositor.damages) compositor.presented = compositor_frame_dated(compositor.presented, period, stamp);
                }
        }
#endif
        if (compositor.vrr_kick && !compositor.damages && !compositor.flip_pending && !compositor_card_holds())
        {
                int tearing = compositor_tearing();
                positive stamp = clock_monotonic_nanoseconds();
                if (stamp >= compositor_present_at(tearing, tearing ? 0 : compositor_present_lead(), stamp) && !compositor_flip_wait(stamp))
                        compositor_vrr_kick();
        }
}

static void compositor_stats_log(void)
{
        static const char *const lat_names[3] = {"pointer to the plane", "pointer to the frame", "key to the window"};
        string_format(log, "compositor: %p pixels composed, the plane was given %p shapes and %p moves and refused %p times\n",
                      (positive)compositor.painted, (positive)compositor.cursor.paints, (positive)compositor.cursor.moves, (positive)compositor.cursor.refused);
        string_format(log, "compositor: the processor was held %p times\n", (positive)(compositor.holds == ~0ul ? 0 : compositor.holds));
        if (!compositor_late.early)
                string_format(log, "compositor: %p presents were made late in their period, and %p of them missed the vblank they were for\n",
                              compositor_late.made, compositor_late.missed);
        string_format(log, "compositor: glass blurred again %p pixels, composed again %p, and %p bands composed on the card\n", compositor.blurred, compositor.recomposed, compositor.glass_band_card);
        if (compositor_reader.on)
                string_format(log, "compositor: the input thread sent %p reports itself (%p of them 100 us or more after the device's stamp, the "
                                   "latest %p us) and %p keys, and handed %p records to the loop\n",
                              (positive)__atomic_load_n(&compositor_reader.direct, __ATOMIC_ACQUIRE), (positive)compositor_reader.late,
                              (positive)compositor_reader.late_most, (positive)compositor_reader.keys,
                              (positive)__atomic_load_n(&compositor_reader.handed, __ATOMIC_ACQUIRE));
        for (unsigned int w = 0; w < 3; w++)
                string_format(log, "compositor: %s: %p, on average %p us, worst %p us\n", lat_names[w], (positive)compositor.latency[w].count,
                              (positive)(compositor.latency[w].count ? compositor.latency[w].total / compositor.latency[w].count / 1000 : 0),
                              (positive)(compositor.latency[w].worst / 1000));
        log_flush();
}

static b32 compositor_main()
{
        b32 status;
        int running = 1, extra = -1, tablet = 0, up = -1, x, y, width, height;
        string_address headless = null, file = null, font = null;
        unsigned long sigmask = COMPOSITOR_SIG_MASK;

        compositor_call(syscall(rt_sigprocmask), 0, (positive)address_of sigmask, 0, 8, 0, 0);
        claim_standard_descriptors();
        compositor_descriptors_room();
        compositor_call(syscall(prctl), 29, 1, 0, 0, 0, 0); // PR_SET_TIMERSLACK = 1 ns

        compositor_zero();
        if (compositor_rows_made < compositor_rows_in_use)
                { string_format(log, "compositor: no memory for the rows of the window table\n"), log_flush(); return 1; }
        compositor.watch = compositor.uevents = compositor.hold = compositor.game = -1;
        compositor.focus = compositor.hover = compositor.grab = compositor.drag = compositor.told_row = COMPOSITOR_NONE;
        compositor.resize = compositor.press = compositor.keys_to = compositor.lit_row = compositor.preview_row = COMPOSITOR_NONE;
        compositor.mark_row = compositor.mark_with = compositor.shared_row = COMPOSITOR_NONE;
        compositor.active_grab = (struct compositor_grab){COMPOSITOR_GRAB_NONE, COMPOSITOR_NONE, 0, 0};
        compositor.touch.closing = compositor.touch.hold_row = COMPOSITOR_NONE;
        compositor.swipe.row = compositor.swipe.armed_row = compositor.osk_turn_focus = compositor.cursor.asked_row = COMPOSITOR_NONE;
        compositor.scenes.press_row = COMPOSITOR_NONE;

        for (positive at = 1, argc = program_argument_count(); at < argc; at++)
        {
                string_address o = program_argument(at);
                if (compositor_option(o, 't')) taskbar_position_default(TASKBAR_TOP);
                else if (compositor_option(o, 'T')) tablet = 1;
                else if (compositor_option(o, 'P')) compositor.cursor.soft = 1;
#ifdef MOONWATER_GPU_C
                else if (compositor_option(o, 'G')) compositor_gpu_state.force = 1;
#endif
                else if (at + 2 < argc && compositor_option(o, 'H')) headless = program_argument(at + 1), file = program_argument(at + 2), at += 2;
                else if (at + 1 < argc)
                {
                        if (compositor_option(o, 's')) compositor.scale = (int)compositor_number(program_argument(++at), 0) * 100;
                        else if (compositor_option(o, 'f')) font = program_argument(++at);
                        else if (compositor_option(o, 'u')) up = (int)compositor_number(program_argument(++at), 0);
                        else if (compositor_option(o, 'l'))
                        {
                                int l = compositor_layout_index((const char *)program_argument(++at));
                                if (l >= 0) compositor.keymap.layout = (unsigned char)l, compositor.layout_given = 1;
                        }
                        else if (compositor_option(o, 'i'))
                                extra = (int)system_open_at(AT_FDCWD, program_argument(++at), FILE_READ_WRITE | O_CLOEXEC | O_NONBLOCK);
                }
        }

        compositor_keys_read();
        compositor_late.early = compositor_flag_exists((string_address) "/run/moonwater/present-early");
        if (compositor_flag_exists((string_address) "/run/moonwater/game-hold-off")) compositor.games = ~0ul, compositor_game_off = 1;
        compositor_vrr.said = compositor_flag_exists((string_address) "/run/moonwater/vrr-capable");
#ifdef MOONWATER_GPU_C
        compositor_card.nowait = !compositor_flag_exists((string_address) "/run/moonwater/present-wait");
        if (compositor_flag_exists((string_address) "/run/moonwater/gpu-clocks-off")) compositor_gpu_clocks.level = 0, compositor_gpu_clocks.game_off = 1;
        compositor_card.lists_nowait = !compositor_flag_exists((string_address) "/run/moonwater/lists-wait");
#endif
        if (compositor.scale < 25 || compositor.scale > 800) compositor.scale = WINDOWS_SCALE_DEFAULT;
        compositor.boot_scale = compositor.scale;
        if (!headless && !compositor.layout_given) compositor_layout_read();

        bipolar oom = system_open_at(AT_FDCWD, (string_address) "/proc/self/oom_score_adj", FILE_WRITE | O_CLOEXEC);
        if (oom >= 0) (void)system_write_all((positive)oom, (string_address) "-900\n", 5), system_close(oom);
        if (!headless)
        {
                int prio = 40;
                compositor_call(syscall(sched_setscheduler), 0, 1, (positive)&prio, 0, 0, 0);
                compositor_pin_ccd0(), compositor_pin_irqs_ccd0();
        }
        if ((status = headless ? compositor_headless(headless, file) : compositor_card_open()) < 0)
                { compositor_say("cannot take the display, error", status); return 1; }
        compositor_spaces_ensure();
        if (!headless) compositor_cursor_start();
        compositor_inputs_open();
        if (extra >= 0 && compositor.input_count < COMPOSITOR_INPUTS)
        {
                compositor.input_kinds[compositor.input_count] = (tablet ? 0 : WINDOWS_INPUT_MOUSE) | WINDOWS_INPUT_WHEEL | WINDOWS_INPUT_BUTTONS;
                compositor.inputs[compositor.input_count] = extra;
                compositor.devices[compositor.input_count++].number = COMPOSITOR_DEVICES - 1;
        }
        compositor_output_rect(0, &x, &y, &width, &height);
        compositor.real_x = compositor.kept_x = x + width / 2, compositor.real_y = compositor.kept_y = y + height / 2;
        compositor.owner = COMPOSITOR_PREDICT_MOUSE;
        compositor_pointer_shown();
        compositor_settings_apply(COMPOSITOR_SET_WALL | COMPOSITOR_SET_LOOK | COMPOSITOR_SET_INPUT | COMPOSITOR_SET_TASKBAR |
                                  COMPOSITOR_SET_TOUCH | COMPOSITOR_SET_PREDICT | COMPOSITOR_SET_CLICK | COMPOSITOR_SET_HAND);
        compositor_cursor_move();
        compositor_font(font ? font : ui_faces()->text[0] ? (string_address)ui_faces()->text : null);

        compositor.signals = compositor_signals();
        compositor.direct_row = compositor.flip_row = compositor.plane_row = compositor.leaving_row = -1;
        compositor.on_screen = compositor.fb;
        if ((status = compositor_table()) < 0 || compositor.signals < 0)
        {
                compositor_say("cannot make the window table, error", status < 0 ? status : compositor.signals);
                if (!headless) compositor_hand_back();
                return 1;
        }
        compositor_doors_start(compositor_clip.door);
        if (headless) compositor_headless_scale();
#ifdef CONFIG_MOONWATER_WAYLAND
        compositor_wl_start(), compositor_xwm_start();
#endif
        compositor_settings_apply(COMPOSITOR_SET_THEME | COMPOSITOR_SET_SCROLL | COMPOSITOR_SET_VRR | COMPOSITOR_SET_HAND | COMPOSITOR_SET_OSK);
        for (unsigned int at = 0; at < COMPOSITOR_INPUTS; at++) compositor_input_publish(at);
        if (!headless) compositor_reader_start();
        compositor_up_say(up);

        string_format(log, "compositor: %b by %b (scale %b), %p input devices, pointer %s, windows in %s\n",
                      (bipolar)compositor.width, (bipolar)compositor.height, (bipolar)compositor.scale, (positive)compositor.input_count,
                      compositor.cursor.soft ? "drawn" : compositor.cursor.handle[0] ? "on the plane" : "none", WINDOWS_TABLE);
        compositor_screens_say();
        compositor.asleep = compositor_slept_clock();

        while (running)
        {
                struct compositor_poll *wanted = compositor_polls();
                struct compositor_slots slots;
                static unsigned int turns;
                positive now = 0, until = 0;
                b32 idle_turn = 0, held, again = 0;
                long awaited, wait[2];
#ifdef CONFIG_MOONWATER_WAYLAND
                static b32 sling, sling_held;
                static positive sling_until;
                static struct compositor_slots sling_slots;
                if (sling)
                {
                        sling = 0, again = 1, slots = sling_slots, held = sling_held, until = sling_until;
                        now = clock_monotonic_nanoseconds();
                        goto compositor_sling_poll;
                }
#endif
                if (compositor.probe_owed && turns++ && !compositor_card.on && !rows_any(&compositor_card.list_rows))
                        compositor.probe_owed = 0, compositor_space_retake();
                held = compositor_card_clients_held(clock_monotonic_nanoseconds());
                if (!wanted) { positive nap[2] = {0, 10000000}; compositor_call(syscall(nanosleep), (positive)nap, 0, 0, 0, 0, 0); continue; }
                compositor_loop_wanted(wanted, &slots, held);
                if (slots.count > COMPOSITOR_POLLS_FOR(compositor_rows_in_use))
                        { compositor_say("the poll list ran past its count, entries", (bipolar)slots.count); break; }

                now = clock_monotonic_nanoseconds();
                if (compositor.flip_pending && compositor_flip_lost(now)) compositor_flip_done(), compositor_damage_space();
                until = compositor_timers_arm(now, slots.fence_at, held);
#ifdef CONFIG_MOONWATER_WAYLAND
        compositor_sling_poll:
#endif
                wait[0] = until > now ? (long)((until - now) / 1000000000ull) : 0;
                wait[1] = until > now ? (long)((until - now) % 1000000000ull) : 0;
                idle_turn = until && until <= now;
                awaited = compositor_loop_wait(wanted, slots.count, until ? wait : 0, again, held);
                now = clock_monotonic_nanoseconds();

                if (awaited > 0)
                {
                        unsigned int any = 0;
                        compositor_inputs_turn(wanted + slots.inputs);
                        for (unsigned int at = 0; at < compositor_inputs_polled() && !any; at++) any = wanted[slots.inputs + at].returned != 0;
#ifdef CONFIG_MOONWATER_WAYLAND
                        if (any) compositor_wl_wake_input();
#endif
                        compositor_reader_settle();
#ifdef MOONWATER_GPU_C
                        compositor_gpu_input_turn(now);
#endif
#ifdef CONFIG_MOONWATER_WAYLAND
                        if ((any && compositor_slingshot(wanted, &slots, awaited, now, until)) ||
                            (!any && compositor_quietshot(wanted, &slots, awaited, now, until)))
                        {
                                for (unsigned int at = 0; at < slots.count; at++)
                                        if (wanted[at].returned)
                                                compositor_loop_by[compositor_loop_source(at, slots.doors, slots.done_at, slots.card_at, slots.fence_at,
                                                                                          slots.uevents_at, slots.clip_at, slots.wayland_at)]++;
                                compositor_loop_tick(now, 0);
                                sling = 1, sling_slots = slots, sling_held = held, sling_until = until;
                                continue;
                        }
                        compositor_sling_only = 0;
#endif
                }
                int minute_tick = (compositor.band || compositor.theme_word == 2) && compositor_minute_check(now);
                compositor_loop_tick(now, idle_turn);
                if (minute_tick)
                        compositor_theme_tick();
                compositor_wake_check(now), compositor_wake_pass(now);
                compositor_hold_drop(now), compositor_game_watch(), compositor_game_slice();
#ifdef MOONWATER_GPU_C
                compositor_gpu_late_tick(now), compositor_gpu_clocks_tick(now);
#endif
                if (awaited < 0) continue;

                for (unsigned int at = 0; awaited > 0 && at < slots.count; at++)
                        if (wanted[at].returned)
                                compositor_loop_by[compositor_loop_source(at, slots.doors, slots.done_at, slots.card_at, slots.fence_at,
                                                                          slots.uevents_at, slots.clip_at, slots.wayland_at)]++;

                if (wanted[0].returned)
                {
                        unsigned int taken = compositor_signal_taken();
                        if (taken == COMPOSITOR_SIGUSR1) compositor_root_again();
                        else if (taken == COMPOSITOR_SIGUSR2) compositor_woke(now, COMPOSITOR_WAKE_SIGNAL);
                        else running = 0;
                }
                compositor_forward();
#ifdef MOONWATER_GPU_C
                compositor_gpu_input_turn(now);
#endif
                if (wanted[1].returned) compositor_inputs_watch();

                b32 listing = rows_any(&compositor_card.list_rows);
                for (unsigned int k = 0, row; k < compositor_rows_in_use; k++)
                {
                        row = (compositor_card.door_from + k) % compositor_rows_in_use;
                        if (!wanted[slots.doors + row].returned) continue;
                        if (compositor_card_door_waits(listing)) { compositor_card.door_from = row; break; }
                        compositor_door((int)row, wanted[slots.doors + row].returned);
                }
                if (slots.done_at && wanted[slots.done_at].returned) compositor_doors_done();
#ifdef CONFIG_MOONWATER_WAYLAND
                if (slots.wayland_at && compositor_wl.started) compositor_wl_pass(wanted + slots.wayland_at);
                if (slots.xwm_at) { compositor_xwm_pass(wanted + slots.xwm_at); if (compositor_wl.started) compositor_wl_resume_stalled(); }
#endif
                if (slots.card_at && wanted[slots.card_at].returned) compositor_card_read();
#ifdef MOONWATER_GPU_C
                if (slots.fence_at) compositor_card_tick();
                if (rows_any(&compositor.list_later)) compositor_lists_later();
#endif
                if (slots.clip_at && wanted[slots.clip_at].returned) compositor_clip_cli();
                if (slots.uevents_at && wanted[slots.uevents_at].returned) compositor_uevents_read();
                if (compositor.space_wanted && !compositor.flip_pending && !compositor_card.on && !rows_any(&compositor_card.list_rows))
                        compositor_space_retake();
                if (compositor.screens_gone && compositor.screens_look && now >= compositor.screens_look && !compositor_card.on && !rows_any(&compositor_card.list_rows))
                        compositor.screens_look = 0, compositor_space_retake();
                if (compositor.cursor.magnified && now >= compositor.cursor.magnified_until)
                        compositor.cursor.magnified = 0, compositor_cursor_move();

                compositor_cursor_tick(now), compositor_shape_sync();
                if (compositor_predict_due()) compositor_predict_expire(now);
                compositor_owed_pass();
                if (compositor_touch_due()) compositor_touch_tick(now);
                if (compositor_snap_due()) compositor_snap_tick(now);
                if (compositor_spots_any(1)) compositor_spots_tick(now);
                if (compositor_notices_look() && compositor.band) compositor_tabs_changed();
                if (compositor.band && minute_tick && taskbar_tick()) compositor_tabs_changed();
                if (compositor.band && now >= compositor_volume.look_at) compositor_volume_look(now);
#ifdef CONFIG_MOONWATER_WAYLAND
                compositor_wl_flip_overdue(now), compositor_wl_callbacks_watch(now);
                if (compositor_wl_frame_at && (now >= compositor_wl_frame_at || compositor_wl_jit_row() < 0))
                        compositor_wl_frame_at = 0, compositor_wl_jit_sent(now), compositor_wl_frame();
                compositor_wl_wake();
#endif
                compositor_taskbar_slide(now), compositor_moon_keep(now), compositor_starting_keep(now);
                if (!compositor_card_holds()) compositor_wall_keep();
                compositor_present_pass();
                compositor_knock(), compositor_rows_spare();
        }

#ifdef CONFIG_MOONWATER_WAYLAND
        compositor_xwm_stop(), compositor_wl_stop();
#endif
        compositor_vrr_off(), compositor_qos_keep(0, 0), compositor_table_drop(), compositor_stats_log();
        if (!headless) compositor_hand_back();
        return 0;
}

/* surface: test client (-x -y -w -h -c -T -N -p -m -R -n -G -a -F -f -t -s). */
static void surface_draw(const struct window_row *own, unsigned int which, unsigned int width, unsigned int height,
                         unsigned int colour, string_address font, string_address text)
{
        unsigned int *pixels = windows_pixels(own, which);
        memory_fill_u32_rect(pixels, own->stride, width, height, colour);
        if (font && !own->table->glass[own->row])
        {
                positive size = 0, bytes = compositor_map_file(font, address_of size), atlas_at = (sizeof(font_engine) + 4095) & ~(positive)4095;
                void *engine = compositor_map_zeroed(atlas_at + 256 * 256);
                font_face face;
                const font_face address_to faces[1] = {&face};
                font_target target = {pixels, own->stride, (b32)width, (b32)height};
                p32 em = 16 << 6;
                if (bytes && engine && font_load(&face, (const p8 address_to)bytes, size, 0) == 0)
                {
                        font_start((font_engine address_to)engine, (p8 address_to)((positive)engine + atlas_at), 256, 256);
                        string_format(log, "surface: text drawn %b px wide\n",
                                      (bipolar)(font_draw((font_engine address_to)engine, faces, 1, (const p8 address_to)text, string_length(text), em, 0,
                                                          &target, 8 << 16, 8 + font_metrics_at(&face, em).baseline, 0xffffff, colour) >> 16));
                }
                else log_direct(str("surface: no font\n"));
                if (engine) compositor_call(syscall(munmap), (positive)engine, atlas_at + 256 * 256, 0, 0, 0, 0);
                if (bytes) compositor_call(syscall(munmap), bytes, size, 0, 0, 0, 0);
        }
        own->table->width[own->row] = width, own->table->height[own->row] = height, own->table->damage_width[own->row] = 0;
}

static b32 surface_main()
{
        bipolar x = 0, y = 0, width = 320, height = 200, colour = 0x3366aa, seconds = -1, flood = 0, alpha = 0;
        b32 pointer = 0, never = 0, away = 0, sized = 0, focused = 0, glass = 0, placed = 0;
        string_address title = null, font = null, text = (string_address) "Moonwater", name = null;
        struct window_row own;
        struct windows *table;
        unsigned int which = 0;
        positive until;
        long taken;

        claim_standard_descriptors();
        for (positive at = 1, argc = program_argument_count(); at < argc; at++)
        {
                string_address o = program_argument(at), v = at + 1 < argc ? program_argument(at + 1) : null;
                if (o[0] != '-' || !o[1] || o[2]) continue;
                if (o[1] == 'p' || o[1] == 'n' || o[1] == 'm' || o[1] == 'R' || o[1] == 'G')
                {
                        glass |= o[1] == 'G', pointer |= o[1] == 'p', sized |= o[1] == 'R', never |= o[1] == 'n', away |= o[1] == 'm';
                        continue;
                }
                if (!v) break;
                at++;
                switch (o[1])
                {
                case 'x': x = compositor_number(v, 0), placed = 1; break;
                case 'y': y = compositor_number(v, 0), placed = 1; break;
                case 'w': width = compositor_number(v, 0); break;
                case 'h': height = compositor_number(v, 0); break;
                case 'c': colour = compositor_number(v, 1); break;
                case 's': seconds = compositor_number(v, 0); break;
                case 'F': flood = compositor_number(v, 0); break;
                case 'a': alpha = compositor_number(v, 0); break;
                case 'T': title = v; break;
                case 'N': name = v; break;
                case 'f': font = v; break;
                case 't': text = v; break;
                }
        }
        if ((taken = windows_take(&own)) < 0)
                { string_format(log, "surface: no window, error %b\n", (bipolar)taken), log_flush(); return 1; }

        table = own.table;
        width = width < 1 ? 1 : width > (bipolar)table->output_width ? (bipolar)table->output_width : width;
        height = height < 1 ? 1 : height > (bipolar)table->output_height ? (bipolar)table->output_height : height;
        colour &= 0xffffff;
        if (glass)
        {
                alpha = alpha < 0 ? 0 : alpha > 255 ? 255 : alpha;
                colour = (bipolar)window_mix(0, (unsigned int)colour, (unsigned int)(alpha + (alpha >> 7))) | (bipolar)((unsigned int)alpha << 24);
                table->glass[own.row] = WINDOW_GLASS;
        }
        table->x[own.row] = (int)x, table->y[own.row] = (int)y;
        if (!placed) windows_place_free(table, own.row);
        table->state[own.row] = away ? WINDOW_MINIMIZED : 0;
        windows_title_set(&own, (const char *)title), windows_name(&own, (const char *)name);
        surface_draw(&own, which, (unsigned int)width, (unsigned int)height, (unsigned int)colour, font, text);
        if (!never)
        {
                windows_show(&own, which);
                for (bipolar at = 0; at < flood; at++) windows_show(&own, which);
                until = clock_monotonic_nanoseconds() + 2000000000ull;
                while (!windows_ready(&own) && clock_monotonic_nanoseconds() < until) windows_wait(&own, -1, 50000000);
        }
        string_format(log, "surface: %p at %b,%b\n", (positive)own.row, (bipolar)table->x[own.row], (bipolar)table->y[own.row]);
        log_flush();
        until = seconds >= 0 ? clock_monotonic_nanoseconds() + (positive)seconds * 1000000000ull : 0;

        for (;;)
        {
                struct window_event event;
                long timeout = -1;
                b32 has = table->focus == own.row;
                if (has != focused) focused = has, string_format(log, "surface: focus %b\n", (bipolar)focused);
                if (sized && table->want_width[own.row] && table->want_height[own.row] &&
                    (table->want_width[own.row] != (unsigned int)width || table->want_height[own.row] != (unsigned int)height) && windows_ready(&own))
                {
                        width = table->want_width[own.row] > table->output_width ? table->output_width : table->want_width[own.row];
                        height = table->want_height[own.row] > table->output_height ? table->output_height : table->want_height[own.row];
                        which ^= 1;
                        surface_draw(&own, which, (unsigned int)width, (unsigned int)height, (unsigned int)colour, null, text);
                        windows_show(&own, which);
                        string_format(log, "surface: size %b %b\n", width, height);
                }
                log_flush();
                if (table->state[own.row] & WINDOW_CLOSING) { log_direct(str("surface: close\n")); break; }
                if (until) { positive now = clock_monotonic_nanoseconds(); if (now >= until) break; timeout = (long)(until - now); }
                if (windows_wait(&own, -1, timeout) < 0) { log_direct(str("surface: the compositor has gone\n")); break; }
                while (windows_event(&own, &event))
                        if (event.type == WINDOW_EVENT_KEY) string_format(log, "surface: key %b %b %p\n", (bipolar)event.a, (bipolar)event.b, (positive)event.flags);
                        else if (event.type == WINDOW_EVENT_BUTTON) string_format(log, "surface: button %b %p\n", (bipolar)event.a, (positive)event.flags);
                        else if (event.type == WINDOW_EVENT_WHEEL) string_format(log, "surface: wheel %b %b\n", (bipolar)event.a, (bipolar)event.b);
                        else if (pointer) string_format(log, "surface: motion %b %b\n", (bipolar)event.a, (bipolar)event.b);
        }
        log_flush();
        windows_leave(&own);
        return 0;
}

#undef system_close
#define system_close(handle) system_call_1(syscall(close), (positive)(handle))

#endif // MOONWATER_CANVAS_COMPOSITOR
