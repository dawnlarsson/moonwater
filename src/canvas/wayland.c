/* Moonwater Canvas Wayland protocol shim: freestanding, table-driven via wayland_protocol.inc, host seam via struct wayland_host. */
#ifndef WAYLAND_INCLUDED
#define WAYLAND_INCLUDED

#ifndef CONFIG_MOONWATER_WAYLAND
#error "wayland.c is built only with CONFIG_MOONWATER_WAYLAND"
#endif

#include "wayland_protocol.inc"
#include "compositor/place.h"

#define WAYLAND_API static __attribute__((unused))
#ifndef likely
#define likely(x) (!!(x)) // lib.c's is the hint; this is only for a build without it
#define unlikely(x) (!!(x))
#endif
#ifndef INLINE
#define INLINE __attribute__((always_inline))
#endif
#ifndef PURE
#define PURE __attribute__((pure))
#endif
#ifndef HOT
#define HOT __attribute__((hot))
#endif
#ifndef COLD
#define COLD __attribute__((cold))
#endif

/* Protocol caps, limits, and constants. */
#define WAYLAND_IDS 0xff000000UL
#define WAYLAND_SERVER_IDS 0x01000000UL
#define WAYLAND_SERVER_BASE 0xff000000UL
#define WAYLAND_POOL_BYTES 0x7fffffffL
#define WAYLAND_OUT_MOST (4L << 20)
#define WAYLAND_OUT_HIGH (1L << 20)
#define WAYLAND_KEYMAP_MAX (1L << 20)
#define WAYLAND_FOURCC_ARGB8888 0x34325241UL
#define WAYLAND_FOURCC_XRGB8888 0x34325258UL
#define WAYLAND_FOURCC_ABGR8888 0x34324241UL
#define WAYLAND_FOURCC_XBGR8888 0x34324258UL
#define WAYLAND_FOURCC_ARGB2101010 0x30335241UL
#define WAYLAND_FOURCC_XRGB2101010 0x30335258UL
#define WAYLAND_FOURCC_ABGR2101010 0x30334241UL
#define WAYLAND_FOURCC_XBGR2101010 0x30334258UL
#define WAYLAND_FOURCC_NV12 0x3231564eUL
#define WAYLAND_MODIFIER_INVALID 0x00ffffffffffffffUL
#define WAYLAND_DMABUF_UNMAPPED ((void *)1ul)
#define WAYLAND_SYNCOBJ_TIMELINE_OK 1L
#define WAYLAND_SYNCOBJ_NODE_OK 2L
#define WAYLAND_XWINDOW_POPUP 1L
#define WAYLAND_XWINDOW_PLACED 2L
#define WAYLAND_XWINDOW_OWNER_SHIFT 8
#define WAYLAND_XWINDOW_OWNER(row) ((row) >= 0 ? ((long)(row) + 1) << WAYLAND_XWINDOW_OWNER_SHIFT : 0L)
#define WAYLAND_TABLE_WANT (1L << 36)
#define WAYLAND_TABLE_FALLBACK (1L << 26)
#define WAYLAND_TABLE_PAGE 4096L
#define WAYLAND_TABLE_FIRST 64L
#define WAYLAND_OBJECTS (1L << 17) /* live objects per client */
#define WAYLAND_CLIENT_IDS_MAX (1UL << 21) /* max client-allocated object id (bounds WLT_INDEX to 8 MiB/client) */
#define WAYLAND_POOLS_MAX 1024L /* live wl_shm_pools per client (prevents vm.max_map_count exhaustion) */
#define WAYLAND_IDS_MAX 4096 /* one seat, device or relative-pointer list per client */
#define WAYLAND_MIME_BYTES (64L << 10) /* all the mime types one source offers, NULs included */
#define WAYLAND_SUB_LEVELS 32L /* subsurface nesting; flatten and the sub walks recurse once per level */
#define WAYLAND_TOKENS_MAX 256L /* global live activation tokens */
#define WAYLAND_OUT_FDS_MAX (WAYLAND_OUT_FDS * 16L) /* queued outgoing FDs per client */

enum
{
        WAYLAND_MESSAGE = 4096, WAYLAND_ARGS = 12, WAYLAND_FDS = 28, WAYLAND_OUT = 65536, WAYLAND_OUT_FDS = 8,
        WAYLAND_EVENT = 1024, WAYLAND_DAMAGE = 8, WAYLAND_CALLBACKS = 1024, WAYLAND_SERIALS = 8,
        WAYLAND_TEXT = 128, WAYLAND_DIMENSION = 16384, WAYLAND_ID_GAP = 4096, WAYLAND_SCREENS = 32, WAYLAND_GLOBALS_MAX = 40,
        WAYLAND_TOUCHES = 10, WAYLAND_TOKEN_MS = 10000, WAYLAND_INPUT_SERIALS = 8, WAYLAND_CLIP_CLIPBOARD = 0, WAYLAND_CLIP_PRIMARY = 1,
        WAYLAND_PICTURE_LENT = 2, WAYLAND_DMABUF_ENTRIES = 64, WAYLAND_DMABUF_TRANCHES = 4, WAYLAND_REGION_RECTS = 8,
        WAYLAND_OK = 0, WAYLAND_DEAD = -1, WAYLAND_STALL = 1, WAYLAND_STASH = 20480, WAYLAND_STALL_MS = 10000,
        WAYLAND_BUDGET_MS = 16, WAYLAND_BUDGET_SCREENS = 8, WAYLAND_XWAIT_MS = 30,
        WAYLAND_KIND_TOPLEVEL = 1, WAYLAND_KIND_POPUP = 2, WAYLAND_KIND_PLACED = 3,
        WAYLAND_XDRAG_OVER = 1, WAYLAND_XDRAG_LEAVE = 2, WAYLAND_XDRAG_DROP = 3, WAYLAND_XDRAG_END = 4,
        WAYLAND_LAYERS = 8, WAYLAND_LAYERS_KEPT = 1, WAYLAND_LAYERS_ROOT = 2,
        WAYLAND_ROLE_NONE = 0, WAYLAND_ROLE_TOPLEVEL = 1, WAYLAND_ROLE_POPUP = 2, WAYLAND_ROLE_SUBSURFACE = 3, WAYLAND_ROLE_CURSOR = 4,
        WAYLAND_ROLE_STAND_IN = 5, WAYLAND_STAND_SHOWN = 0, WAYLAND_STAND_WAITING = 1, WAYLAND_STAND_UNKNOWN = 2, WAYLAND_STAND_NOT_OWNER = 3,
        WAYLAND_STAND_PARTIAL = 4, WAYLAND_STAND_OWN = 5,
        WAYLAND_SURFACE_XDG = 1, WAYLAND_SURFACE_CONFIGURED = 2, WAYLAND_SURFACE_SENT = 4, WAYLAND_SURFACE_MAPPED = 8,
        WAYLAND_UNIT = 120, WAYLAND_NOTCH = 15, WAYLAND_SCALE_LEAST = 30, WAYLAND_SCALE_MOST = 960, WAYLAND_OUTPUT_NAME = 64,
        WAYLAND_DRAG_ACTIONS = 7, WAYLAND_DRAG_ANSWER_MS = 1000, WAYLAND_ICON_CONTENT = 1, WAYLAND_ICON_SET = 2,
        WAYLAND_KEY_ESC = 1, WAYLAND_ESCAPE_MS = 1000, WAYLAND_KEYS_CHORDS = 0, WAYLAND_KEYS_CLIENT = 1, WAYLAND_KEYS_ESCAPE = 2
};

enum
{
        WAYLAND_DO_MINIMIZE = 1, WAYLAND_DO_MAXIMIZE, WAYLAND_DO_UNMAXIMIZE, WAYLAND_DO_FULLSCREEN, WAYLAND_DO_UNFULLSCREEN,
        WAYLAND_DO_MOVE, WAYLAND_DO_RESIZE, WAYLAND_DO_CLOSE, WAYLAND_DO_FOCUS, WAYLAND_DO_CURSOR, WAYLAND_DO_PINCH_WANT,
        WAYLAND_DO_TEXT, WAYLAND_DO_CARET, WAYLAND_DO_PLACE, WAYLAND_DO_LAYER, WAYLAND_DO_MIN_SIZE, WAYLAND_DO_MAX_SIZE,
        WAYLAND_DO_HINT // a locked pointer's cursor hint from the client in a (the row holding the pointer locked): its answer to a motion
};

enum
{
        WAYLAND_CURSOR_ARROW = 0, WAYLAND_CURSOR_TEXT, WAYLAND_CURSOR_CROSS, WAYLAND_CURSOR_MOVE, WAYLAND_CURSOR_RESIZE_H,
        WAYLAND_CURSOR_RESIZE_V, WAYLAND_CURSOR_RESIZE_NWSE, WAYLAND_CURSOR_RESIZE_NESW, WAYLAND_CURSOR_HAND, WAYLAND_CURSOR_WAIT,
        WAYLAND_CURSOR_NOTALLOWED, WAYLAND_CURSOR_HIDDEN
};

enum
{
        WAYLAND_STATE_MAXIMIZED = 1, WAYLAND_STATE_FULLSCREEN = 2, WAYLAND_STATE_ACTIVATED = 4, WAYLAND_STATE_RESIZING = 8,
        WAYLAND_STATE_SUSPENDED = 16, WAYLAND_STATE_TILED_LEFT = 32, WAYLAND_STATE_TILED_RIGHT = 64,
        WAYLAND_STATE_TILED_TOP = 128, WAYLAND_STATE_TILED_BOTTOM = 256
};

struct wayland_rect { long x, y, width, height; };

struct wayland_dmabuf_info
{
        unsigned long device;
        long count;
        struct { unsigned int format; unsigned long modifier; } entry[WAYLAND_DMABUF_ENTRIES];
        long tranches;
        struct { unsigned long device, flags; long count; unsigned char index[WAYLAND_DMABUF_ENTRIES]; } tranche[WAYLAND_DMABUF_TRANCHES];
};

struct wayland_picture
{
        const unsigned char *pixels;
        long stride, width, height, format;
        struct wayland_rect damage[WAYLAND_DAMAGE];
        long damages, full;
        long dest_x, dest_y, row_width, row_height, blend, last, sync_fd;
        const unsigned char *map;
        long map_bytes, fragile, transparent;
        long opaque_x, opaque_y, opaque_w, opaque_h;
        long transform, y_invert;
        long src_x, src_y, src_width, src_height, dst_width, dst_height;
        unsigned int fourcc;
        unsigned long modifier, pool_id;
        long own_size, explicit_sync, plane_count, plane_fd[4];
        unsigned long plane_offset[4], plane_stride[4];
        long layer, changed;
};

struct wayland_syncobj_host
{
        void *context;
        long (*available)(void *context, char *node, long node_cap);
        long (*import)(void *context, long fd);
        void (*drop)(void *context, long handle);
        int (*ready)(void *context, long handle, unsigned long long point);
        void (*signal)(void *context, long handle, unsigned long long point);
        long (*eventfd)(void *context, long handle, unsigned long long point);
};

struct wayland_facts { long width, height, states, closing, x, y, scale; };
struct wayland_screen { long x, y, width, height, scale, refresh, physical_width, physical_height; char name[16]; };

struct wayland_host
{
        void *context;
        void *(*alloc)(void *context, long bytes);
        void (*release)(void *context, void *block, long bytes);
        void *(*map)(void *context, long fd, long size);
        void *(*remap)(void *context, void *at, long old_size, long new_size);
        void (*unmap)(void *context, void *at, long size);
        void (*close)(void *context, long fd);
        int (*row_open)(void *context, long client, long kind, int parent_row, long x, long y);
        void (*row_close)(void *context, int row);
        int (*row_picture)(void *context, int row, const struct wayland_picture *picture);
        void (*row_title)(void *context, int row, const char *text, long length);
        void (*row_do)(void *context, int row, long what, long a, long b);
        int (*row_facts)(void *context, int row, struct wayland_facts *facts);
        long (*screens)(void *context, struct wayland_screen *screens, long room);
        long (*now)(void *context);
        long (*keymap)(void *context, long *size);
        void (*repeat)(void *context, long *rate, long *delay);
        long (*seat)(void *context);
        long (*locks)(void *context);
        void (*pointer_hold)(void *context, long row, long mode);
        long (*dmabuf_info)(void *context, struct wayland_dmabuf_info *info);
        long (*dmabuf_table)(void *context, long *size);
        void *(*dmabuf_map)(void *context, long fd, long size);
        const char *(*clip_type)(void *context, long which, long nth);
        long (*clip_pull)(void *context, long client, long which);
        void (*clip_abort)(void *context, long client, long which);
        void (*clip_push)(void *context, long row, long which, const char *mime, long fd);
        void (*clip_clear)(void *context, long client, long which);
        int (*dmabuf_ready)(void *context, long fd);
        int (*random)(void *context, unsigned char *into, long bytes);
        int (*row_layers)(void *context, int row, const struct wayland_picture *layers, long count, long root);
        int (*row_busy)(void *context, int row);
        void *(*reserve)(void *context, long *bytes);
        int (*commit)(void *context, void *at, long bytes);
        void (*unreserve)(void *context, void *at, long bytes);
        void (*xreceive)(void *context, long client, long which, const char *mime, long fd);
        long (*xdrag)(void *context, long what, long row, long x, long y);
        void (*row_app)(void *context, int row, const char *app_id);
        // The row of the X window `window` for a stand-in of the Wayland client `client` (canvas_swapchain_v1.override_window): the
        // row, or -WAYLAND_STAND_WAITING (the window is the client's and not shown yet, or its X client is not named yet),
        // -WAYLAND_STAND_UNKNOWN (no X server of the compositor has it), -WAYLAND_STAND_NOT_OWNER (another X client made it).
        long (*stand_in)(void *context, long client, unsigned long window);
};

struct wayland_object { unsigned int id; unsigned char iface, version; unsigned short flags; int link; unsigned int aux; };

struct wayland_table
{
        unsigned char *at;
        long unit, count, usable, reserved, bytes, frees, committed, stack_committed, stack;
};

enum wayland_cb_list { WLCB_CALLBACK = 0, WLCB_READY, WLCB_FEEDBACK, WLCB_PRESENTING, WLCB_LISTS };
struct wayland_cb { unsigned int id, next; };
struct wayland_cbq { unsigned int head, tail; long count; };
struct wayland_ids { unsigned int *id; long count; struct wayland_table table; };

struct wayland_surf_state
{
        unsigned int attached;
        unsigned char attach_made, damage_full, damages, damage_kind[WAYLAND_DAMAGE];
        struct wayland_rect damage[WAYLAND_DAMAGE];
        long scale, transform, dx, dy, src_x, src_y, src_w, src_h, dst_w, dst_h;
        struct wayland_rect opaque;
        unsigned char opaque_set;
        unsigned int alpha;
        unsigned char icon, async_hint, fifo_wait, barrier;
        unsigned long timestamp;
        unsigned char sync_acq_set, sync_rel_set;
        unsigned short sync_acq_slot, sync_rel_slot;
        unsigned long long sync_acq_point, sync_rel_point;
};

struct wayland_surface
{
        unsigned int gen;
        unsigned char live, role;
        unsigned short flags;
        unsigned int id;
        struct wayland_surf_state pending, cur, cached;
        struct wayland_cbq cbq[WLCB_LISTS];
        long presentings_framed, scale120, width, height;
        unsigned int viewport_id, fractional_id;
        long told120, told_whole, told_unit;
        unsigned int content;
        unsigned char modal, attention;
        long barrier_frame;
        unsigned int xdg_id, role_id, serial[WAYLAND_SERIALS];
        unsigned char serials;
        unsigned int last_serial;
        long geometry_x, geometry_y, geometry_width, geometry_height;
        unsigned char geometry_set;
        long window_x, window_y, window_w, window_h;
        unsigned int lent, lent_gone, lent_latched, lent_shown, held;
        unsigned char layered, laying;
        char title[WAYLAND_TEXT], app_id[WAYLAND_TEXT];
        unsigned int parent, sub_parent, first_child[2], last_child[2], next_sib, prev_sib;
        unsigned char sub_above, tearing, shortcuts, xsurface, xgrab;
        long closes;
        unsigned int shortcut_id;
        unsigned char shortcut_active, escape_down;
        long escape_at;
        unsigned char need_full, input_none, input_bounded, input_count, shown_full;
        struct wayland_rect input_rects[WAYLAND_REGION_RECTS], shown_rect;
        long budget_at, budget_used;
        unsigned int entered, syncobj_id;
        int row;
        union
        {
                struct
                {
                        long min_width, min_height, max_width, max_height, configure_frame;
                        long told_width, told_height, told_states, layer_level, layer_output, layer_w, layer_h;
                        unsigned char layer_shell, layer_configured, configure_late, told, want_maximized, want_fullscreen;
                        short want_screen;
                } top;
                struct { long x, y, width, height; unsigned char grab; } popup;
                struct { unsigned int id; long x, y, pend_x, pend_y; unsigned char sync, dirty, pend_pos, translucent; long translucent_x, translucent_y, translucent_w, translucent_h; } sub;
                struct { unsigned long long serial; long owner, x, y, wait; unsigned char serial_set, adopted, popup, dead, alone, placed; } xwin;
                // A stand-in (canvas_swapchain_v1): the X window it is the picture of, or its target surface of this client (index + 1,
                // with the surface's gen then), the manager its answers go to, the answer last sent, and whether another took its row.
                struct { unsigned int window, target, target_gen, manager; unsigned char said, displaced; } stand;
        } u;
};

struct wayland_pool { long live, fd, extra_fd[3], dmabuf, unmapped, size, refs, gone; unsigned long id; void *at; };

struct wayland_buffer
{
        long live, pool, offset, width, height, stride, format;
        unsigned int fourcc;
        unsigned long modifier;
        long plane_count;
        unsigned long plane_offset[4], plane_stride[4];
        unsigned char y_invert, rel_pending, explicit_sync;
        unsigned short rel_timeline;
        unsigned long long rel_point;
        unsigned int id;
        long held, zombie;
};

struct wayland_sync_timeline { unsigned char live, gone; int refs; long handle; };
struct wayland_positioner { long live; unsigned int id; long width, height, anchor_x, anchor_y, anchor_width, anchor_height, anchor, gravity, adjustment, offset_x, offset_y, reactive; };
struct wayland_region { unsigned char live, exact, count, overflow; struct wayland_rect rect, bounds, rects[WAYLAND_REGION_RECTS]; };
struct wayland_source { unsigned int id; unsigned char live, which, state; unsigned int actions; struct wayland_table mime_table; long mime_bytes; };
struct wayland_offer { unsigned int id; unsigned char live, which; unsigned long serial; unsigned char drag, accepted, finished; unsigned int actions, preferred; };
struct wayland_constraint { unsigned int id; unsigned char live, kind, active, oneshot, hinted; unsigned int surface; int hint_x, hint_y; };
struct wayland_notify { unsigned int id; unsigned char live, input_only, idle; long timeout, since, idled; };
struct wayland_params { unsigned int id; unsigned char live, used; long fd[4]; unsigned long offset[4], stride[4], modifier[4]; };
struct wayland_text { unsigned int id; unsigned char live, enabled, pending_enabled, purpose, pending_purpose; long caret, pending_caret; int row, counted; unsigned long serial; };
struct wayland_feedback { unsigned int id; unsigned char need; };
struct wayland_outfd { long fd, at, owned; };

enum wayland_tbl_kind
{
        WLT_POOL = 0, WLT_INDEX, WLT_SERVER, WLT_OBJECT, WLT_SURFACE, WLT_BUFFER, WLT_POSITIONER, WLT_REGION, WLT_FEEDBACK,
        WLT_SOURCE, WLT_OFFER, WLT_PARAMS, WLT_TEXT, WLT_TIMELINE, WLT_CONSTRAINT, WLT_NOTIFY, WLT_CB, WLT_OUT, WLT_OUT_FD, WLT_COUNT
};

struct wayland_client
{
        long live, dead, slot, pid, uid;
        unsigned char xspace;
        long in_length;
        unsigned char in[WAYLAND_MESSAGE], stash[WAYLAND_STASH];
        long stash_length, stalled, stalled_fd, stalled_at, stalled_sync_handle;
        unsigned long long stalled_sync_point;
        long budget_surface, waiting, presenting_surfaces, subsurfaces, out_length, out_room;
        unsigned char *out;
        struct wayland_outfd *out_fds;
        long out_fd_count, in_fds[WAYLAND_FDS], in_fd_count;
        unsigned long serial;
        struct wayland_ids seat_ids[3];
        unsigned char finger_scroll;
        unsigned int press_serial, pointer_enter;
        struct wayland_ids device_ids[2];
        unsigned int input_serial[WAYLAND_INPUT_SERIALS];
        unsigned char input_next;
        long server_hint, server_live, pending[2], feedbacks;
        unsigned char feedback_pending;
        long dmabufs, inhibitors, pinches;
        unsigned char hold;
        long hold_surface, hold_at;
        struct wayland_ids relative_ids;
        long axis_left[2];
        struct wayland_ids gone;
        union { struct wayland_table tbl[WLT_COUNT]; struct { struct wayland_table pool_table; }; };
        unsigned int *index, *server_index;
        struct wayland_object *objects;
        struct wayland_surface *surfaces;
        struct wayland_pool *pools;
        struct wayland_buffer *buffers;
        struct wayland_positioner *positioners;
        struct wayland_region *regions;
        struct wayland_feedback *feedback;
        struct wayland_source *sources;
        struct wayland_offer *offers;
        struct wayland_params *params;
        struct wayland_text *texts;
        struct wayland_sync_timeline *timelines;
        struct wayland_constraint *constraints;
        struct wayland_notify *notifies;
        struct wayland_cb *cbs;
};

// stand_*: the stand-in whose pictures the row shows, -1 for none; lifted: that is a subsurface of the row's own window, lifted (wayland_lift)
struct wayland_row { long client, surface, percent, stand_client, stand_surface; unsigned char lifted; };
struct wayland_touch { unsigned char live; int client, row, surface; long id, x, y; };
struct wayland_token { unsigned char live, committed; int client; unsigned long serial; long when; unsigned int gen; char text[33]; };

static struct
{
        const struct wayland_host *host;
        struct wayland_client **client;
        unsigned long long *out_dirty;
        struct wayland_table client_table, out_table;
        long client_slots;
        struct wayland_row *row;
        struct wayland_table row_table;
        long row_made;
        struct wayland_screen screen[WAYLAND_SCREENS];
        long screens, output_age[WAYLAND_SCREENS];
        long clients, objects, surfaces, pools, buffers, fds, rows;
        long pointer_row, pointer_surface, keyboard_row, pointer_x, pointer_y;
        unsigned int seat_gen;
        unsigned long mods[4];
        unsigned char keys[16];
        long buttons;
        struct
        {
                int live, wl_drop_sent, released; /* wl_drop_sent: the shim has sent the wl drop to its target (not the engine's dropped, which is the native release) */
                long released_at, client, source, origin, target, surface, offer;
                unsigned int action;
                long xtarget;
                int x_accepted, xsource, x_seen, held;
                unsigned int x_actions;
                struct wayland_table x_types;
                long x_bytes;
        } drag;
        struct wayland_dmabuf_info dmabuf;
        long dmabufs;
        struct wayland_syncobj_host syncobj;
        long syncobj_ok;
        char drm_node[64];
        long constraints, hold_row, hold_mode, unheld_row, hint_row, hint_x, hint_y, heard, quiet;
        long counts_row, counts[4], counts_micro, input_at, bells, icons, frames, latches, answered, waiting, late_quiet;
        int framing, calling;
        unsigned long target, target_period;
        struct wayland_touch touch[WAYLAND_TOUCHES];
        struct wayland_token *token;
        long tokens;
        struct wayland_table token_table;
        long pinch_x, pinch_y;
        unsigned long synth_code;
        long xwayland_uid, idle_notifies;
        unsigned long pool_id;
        long cursor_client, cursor_surface, cursor_hot_x, cursor_hot_y;
        unsigned int cursor_gen;
        unsigned long cursor_serial;
        long lift_off, lifts, lifts_ended; // the host's lift-off switch (wayland_lifts_off); the windows lifted and let down (wayland_lift)
} wayland;

static const struct wayland_host *wayland_tables_by;
WAYLAND_API void wayland_tables_host(const struct wayland_host *host) { wayland_tables_by = host; }
WAYLAND_API unsigned long wayland_u32_hash(unsigned long key, long buckets) { return (key * 0x9e3779b97f4a7c15ul >> 24) & (unsigned long)(buckets - 1); }

static inline const struct wayland_host *wayland_table_host(void) { return wayland.host ? wayland.host : wayland_tables_by; }
static inline long wayland_round_page(long bytes) { return (bytes + WAYLAND_TABLE_PAGE - 1) & ~(WAYLAND_TABLE_PAGE - 1); }
static inline struct wayland_client *wayland_client_of(long slot) { return slot >= 0 && slot < wayland.client_slots ? wayland.client[slot] : 0; }
static inline struct wayland_client *wayland_live_client(long slot) { return wayland.host ? wayland_client_of(slot) : 0; }
static inline long wayland_now(void) { return wayland.host->now(wayland.host->context); }
static inline void wayland_fd_close(long fd) { wayland.host->close(wayland.host->context, fd); wayland.fds--; }
static inline void wayland_row_do(long row, long what, long a, long b) { wayland.host->row_do(wayland.host->context, (int)row, what, a, b); }
static inline int wayland_row_facts(long row, struct wayland_facts *f) { return wayland.host->row_facts(wayland.host->context, (int)row, f); }
static inline int wayland_row_busy(long row) { return row >= 0 && wayland.host->row_busy && wayland.host->row_busy(wayland.host->context, (int)row); }
static inline int wayland_row_suspended(long row) { struct wayland_facts f; return wayland_row_facts(row, &f) >= 0 && (f.states & WAYLAND_STATE_SUSPENDED); }

static struct wayland_surface *wayland_row_surface(long row, struct wayland_client **client)
{
        struct wayland_client *c;
        if (row < 0 || row >= wayland.row_made || wayland.row[row].client < 0)
                return 0;
        c = wayland.client[wayland.row[row].client];
        if (!c || !c->surfaces[wayland.row[row].surface].live)
                return 0;
        return *client = c, &c->surfaces[wayland.row[row].surface];
}

static struct wayland_surface *wayland_root_of(struct wayland_client *c, struct wayland_surface *s);

// The stand-in whose pictures the row shows (canvas_swapchain_v1), or the subsurface it lifted (wayland_lift), and its client: 0 for none.
static struct wayland_surface *wayland_row_stand(long row, struct wayland_client **client)
{
        struct wayland_client *c;
        struct wayland_surface *s;

        if (row < 0 || row >= wayland.row_made || wayland.row[row].stand_client < 0 || !(c = wayland_client_of(wayland.row[row].stand_client)) ||
            wayland.row[row].stand_surface < 0 || wayland.row[row].stand_surface >= c->tbl[WLT_SURFACE].count)
                return 0;
        s = &c->surfaces[wayland.row[row].stand_surface];
        if (!s->live || (wayland.row[row].lifted ? s->role != WAYLAND_ROLE_SUBSURFACE || !s->sub_parent || wayland_root_of(c, s)->row != row
                                                 : s->role != WAYLAND_ROLE_STAND_IN || s->row != row))
                return 0;
        return *client = c, s;
}

static inline int wayland_row_stood(long row) { return row >= 0 && row < wayland.row_made && wayland.row[row].stand_client >= 0; }

static void wayland_table_free_mem(const struct wayland_host *h, void *at, long bytes)
{
        if (h->reserve && h->commit && h->unreserve)
                h->unreserve(h->context, at, bytes);
        else
                h->release(h->context, at, bytes);
}

static int wayland_table_room(struct wayland_table *t, long want)
{
        const struct wayland_host *h = wayland_table_host();
        long usable, need, stack_need;
        int has_res;

        if (want <= t->usable)
                return 1;
        if (!h)
                return 0;
        has_res = h->reserve && h->commit && h->unreserve;
        if (!t->at)
        {
                long bytes = has_res ? WAYLAND_TABLE_WANT : WAYLAND_TABLE_FALLBACK;
                void *at = has_res ? h->reserve(h->context, &bytes) : h->alloc(h->context, bytes);
                if (!at || bytes < 2 * WAYLAND_TABLE_PAGE + t->unit + 4)
                        return at ? (wayland_table_free_mem(h, at, bytes), 0) : 0;
                t->at = (unsigned char *)at, t->bytes = bytes;
                t->reserved = (bytes - 2 * WAYLAND_TABLE_PAGE) / (t->unit + 4);
                t->stack = wayland_round_page(t->reserved * t->unit);
        }
        if (want > t->reserved)
                return 0;
        usable = t->usable * 2 > want ? t->usable * 2 : want;
        usable = usable < WAYLAND_TABLE_FIRST ? WAYLAND_TABLE_FIRST : usable > t->reserved ? t->reserved : usable;
        need = wayland_round_page(usable * t->unit), stack_need = wayland_round_page(usable * 4);
        if (has_res)
        {
                if (need > t->committed && !h->commit(h->context, t->at + t->committed, need - t->committed))
                        return 0;
                if (need > t->committed)
                        t->committed = need;
                if (stack_need > t->stack_committed &&
                    !h->commit(h->context, t->at + t->stack + t->stack_committed, stack_need - t->stack_committed))
                        return 0;
        }
        if (need > t->committed) t->committed = need;
        if (stack_need > t->stack_committed) t->stack_committed = stack_need;
        return t->usable = usable, 1;
}

static long wayland_table_take(struct wayland_table *t)
{
        if (t->frees > 0)
                return (long)((unsigned int *)(t->at + t->stack))[--t->frees];
        return wayland_table_room(t, t->count + 1) ? t->count++ : -1;
}

static void wayland_table_give(struct wayland_table *t, long index)
{
        if (index >= 0 && index < t->count && t->frees < t->count)
                ((unsigned int *)(t->at + t->stack))[t->frees++] = (unsigned int)index;
}

static int wayland_table_reach(struct wayland_table *t, long count)
{
        if (!wayland_table_room(t, count))
                return 0;
        if (count > t->count)
                t->count = count;
        return 1;
}

static void wayland_table_drop(struct wayland_table *t)
{
        const struct wayland_host *h = wayland_table_host();
        long unit = t->unit;

        if (t->at && h)
                wayland_table_free_mem(h, t->at, t->bytes);
        *t = (struct wayland_table){.unit = unit};
}

#ifndef STANDARD_MODERN_C
unsigned long long wire_message(const unsigned char *bytes, unsigned long long size, unsigned long long most, unsigned int *head);
unsigned long long rects_clip(long long *rects, unsigned long long count, unsigned long long width, unsigned long long height);
unsigned long long wire_arguments(const unsigned char *kinds, const unsigned char *classes, const unsigned char *bytes, unsigned long long left,
                                  unsigned int *out, unsigned long long room);
unsigned long long wire_event(unsigned char *into, unsigned long long object, unsigned long long opcode, const unsigned int *words,
                              unsigned long long count);
int bits_trailing_zeros(unsigned long long value);
void memory_zero(void *destination, unsigned long size);
unsigned long memory_into_hex(void *destination, const void *source, unsigned long size);
enum { WIRE_NONE = 1, WIRE_WORD = 2, WIRE_TEXT = 4, WIRE_EMPTY = 8, WIRE_ENDED = 16, WIRE_PAIR = 32 };
enum { WIRE_END_DONE = 0, WIRE_END_SHORT, WIRE_END_NOTHING, WIRE_END_LONG, WIRE_END_OPEN, WIRE_END_PAIR, WIRE_END_KIND, WIRE_END_ROOM, WIRE_END_OVER };
#endif

#ifdef STANDARD_MODERN_C_LIB_UTIL
#define wayland_first_bit(mask) ((long)bottom_bit_known((positive)(mask)))
#else
#define wayland_first_bit(mask) ((long)bits_trailing_zeros(mask))
#endif

#define WAYLAND_FOFF(T, f) ((long)(unsigned long)&((T *)0)->f)
#define WAYLAND_OFF(f) WAYLAND_FOFF(struct wayland_client, f)
#define WAYLAND_SOFF(f) WAYLAND_FOFF(struct wayland_surface, f)

static const struct { long unit, ptr_off; } wayland_tbl_meta[WLT_COUNT] = {
        [WLT_POOL]       = {sizeof(struct wayland_pool), WAYLAND_OFF(pools)},
        [WLT_INDEX]      = {sizeof(unsigned int), WAYLAND_OFF(index)},
        [WLT_SERVER]     = {sizeof(unsigned int), WAYLAND_OFF(server_index)},
        [WLT_OBJECT]     = {sizeof(struct wayland_object), WAYLAND_OFF(objects)},
        [WLT_SURFACE]    = {sizeof(struct wayland_surface), WAYLAND_OFF(surfaces)},
        [WLT_BUFFER]     = {sizeof(struct wayland_buffer), WAYLAND_OFF(buffers)},
        [WLT_POSITIONER] = {sizeof(struct wayland_positioner), WAYLAND_OFF(positioners)},
        [WLT_REGION]     = {sizeof(struct wayland_region), WAYLAND_OFF(regions)},
        [WLT_FEEDBACK]   = {sizeof(struct wayland_feedback), WAYLAND_OFF(feedback)},
        [WLT_SOURCE]     = {sizeof(struct wayland_source), WAYLAND_OFF(sources)},
        [WLT_OFFER]      = {sizeof(struct wayland_offer), WAYLAND_OFF(offers)},
        [WLT_PARAMS]     = {sizeof(struct wayland_params), WAYLAND_OFF(params)},
        [WLT_TEXT]       = {sizeof(struct wayland_text), WAYLAND_OFF(texts)},
        [WLT_TIMELINE]   = {sizeof(struct wayland_sync_timeline), WAYLAND_OFF(timelines)},
        [WLT_CONSTRAINT] = {sizeof(struct wayland_constraint), WAYLAND_OFF(constraints)},
        [WLT_NOTIFY]     = {sizeof(struct wayland_notify), WAYLAND_OFF(notifies)},
        [WLT_CB]         = {sizeof(struct wayland_cb), WAYLAND_OFF(cbs)},
        [WLT_OUT]        = {WAYLAND_TABLE_PAGE, WAYLAND_OFF(out)},
        [WLT_OUT_FD]     = {sizeof(struct wayland_outfd), WAYLAND_OFF(out_fds)},
};

static inline void wayland_tbl_point(struct wayland_client *c, enum wayland_tbl_kind k)
{
        if (wayland_tbl_meta[k].ptr_off >= 0)
                *(void **)((unsigned char *)c + wayland_tbl_meta[k].ptr_off) = c->tbl[k].at;
}

static inline void wayland_tables_init(struct wayland_client *c) { for (long k = 0; k < WLT_COUNT; k++) c->tbl[k].unit = wayland_tbl_meta[k].unit; }
static inline void wayland_tbl_free(struct wayland_client *c, enum wayland_tbl_kind k, long idx) { wayland_table_give(&c->tbl[k], idx); }

static int wayland_tbl_reach(struct wayland_client *c, enum wayland_tbl_kind k, long count)
{
        return wayland_table_reach(&c->tbl[k], count) ? (wayland_tbl_point(c, k), 1) : 0;
}

static long wayland_tbl_alloc(struct wayland_client *c, enum wayland_tbl_kind k)
{
        long at = wayland_table_take(&c->tbl[k]);

        if (at < 0)
                return -1;
        wayland_tbl_point(c, k);
        unsigned int gen = k == WLT_SURFACE ? c->surfaces[at].gen : 0;
        memory_zero(c->tbl[k].at + at * c->tbl[k].unit, (unsigned long)c->tbl[k].unit);
        if (k == WLT_SURFACE)
                c->surfaces[at].gen = gen;
        return at;
}

static __attribute__((noinline)) int wayland_out_grow(struct wayland_client *c)
{
        long want = (c->out_length + WAYLAND_EVENT + WAYLAND_TABLE_PAGE - 1) / WAYLAND_TABLE_PAGE;

        if (want * WAYLAND_TABLE_PAGE > WAYLAND_OUT_MOST || !wayland_tbl_reach(c, WLT_OUT, want))
                return 0;
        c->out_room = c->tbl[WLT_OUT].usable * WAYLAND_TABLE_PAGE < WAYLAND_OUT_MOST ? c->tbl[WLT_OUT].usable * WAYLAND_TABLE_PAGE : WAYLAND_OUT_MOST;
        return c->out_length + WAYLAND_EVENT <= c->out_room;
}

static inline int wayland_out_has(struct wayland_client *c) { return c->out_length + WAYLAND_EVENT <= c->out_room || wayland_out_grow(c); }
static inline int wayland_out_fd_room(struct wayland_client *c) { return c->out_fd_count < WAYLAND_OUT_FDS_MAX && wayland_tbl_reach(c, WLT_OUT_FD, c->out_fd_count + 1); }
static inline long wayland_out_fd_batch(const struct wayland_client *c) { return c->out_fd_count < WAYLAND_OUT_FDS ? c->out_fd_count : WAYLAND_OUT_FDS; }
static inline void wayland_out_fd_put(struct wayland_client *c, long fd, long owned, long at) { c->out_fds[c->out_fd_count++] = (struct wayland_outfd){fd, at, owned}; }

static int wayland_cb_push(struct wayland_client *c, struct wayland_cbq *q, unsigned int id)
{
        long idx;

        /* a queue past WAYLAND_CALLBACKS is refused: the caller answers or discards the callback at once, so the queue cannot grow without bound */
        if (q->count >= WAYLAND_CALLBACKS)
                return 0;
        idx = wayland_tbl_alloc(c, WLT_CB);

        if (idx < 0)
                return 0;
        c->cbs[idx] = (struct wayland_cb){id, 0};
        *(q->tail ? &c->cbs[q->tail - 1].next : &q->head) = (unsigned int)(idx + 1);
        q->tail = (unsigned int)(idx + 1), q->count++;
        return 1;
}

static unsigned int wayland_cb_pop(struct wayland_client *c, struct wayland_cbq *q)
{
        unsigned int h = q->head, id;

        if (!h)
                return 0;
        id = c->cbs[h - 1].id, q->head = c->cbs[h - 1].next;
        if (!q->head)
                q->tail = 0;
        q->count--, wayland_tbl_free(c, WLT_CB, (long)(h - 1));
        return id;
}

static void wayland_cb_splice(struct wayland_client *c, struct wayland_cbq *dst, struct wayland_cbq *src)
{
        if (!src->head)
                return;
        *(dst->tail ? &c->cbs[dst->tail - 1].next : &dst->head) = src->head;
        dst->tail = src->tail, dst->count += src->count;
        *src = (struct wayland_cbq){0, 0, 0};
}

static int wayland_rows_reach(long row)
{
        long was = wayland.row_made;

        if (row < was)
                return 1;
        wayland.row_table.unit = sizeof(struct wayland_row);
        if (!wayland_table_reach(&wayland.row_table, row + 1))
                return 0;
        wayland.row = (struct wayland_row *)wayland.row_table.at;
        for (long at = was; at <= row; at++)
                wayland.row[at] = (struct wayland_row){-1, -1, 0, -1, -1};
        return wayland.row_made = row + 1, 1;
}

static inline void wayland_out_mark(long slot) { if (likely((unsigned long)slot < (unsigned long)wayland.client_slots)) wayland.out_dirty[(unsigned long)slot >> 6] |= 1ull << ((unsigned long)slot & 63u); }
static inline void wayland_out_clear(long slot) { if (likely((unsigned long)slot < (unsigned long)wayland.client_slots)) wayland.out_dirty[(unsigned long)slot >> 6] &= ~(1ull << ((unsigned long)slot & 63u)); }

static long wayland_client_slot_grow(void)
{
        long slot = wayland.client_slots;

        wayland.client_table.unit = sizeof(struct wayland_client *), wayland.out_table.unit = sizeof(unsigned long long);
        if (!wayland_table_reach(&wayland.client_table, slot + 1) || !wayland_table_reach(&wayland.out_table, slot / 64 + 1))
                return -1;
        wayland.client = (struct wayland_client **)wayland.client_table.at;
        wayland.out_dirty = (unsigned long long *)wayland.out_table.at;
        wayland.client[slot] = 0;
        if (slot % 64 == 0)
                wayland.out_dirty[slot / 64] = 0;
        return wayland.client_slots = slot + 1, slot;
}

static void wayland_client_slots_drop(void)
{
        wayland_table_drop(&wayland.client_table), wayland_table_drop(&wayland.out_table);
        wayland.client = 0, wayland.out_dirty = 0, wayland.client_slots = 0;
}

static int wayland_ids_add(struct wayland_ids *ids, unsigned int id)
{
        if (ids->count >= WAYLAND_IDS_MAX) /* a list of seat, device or relative objects: no client needs more; a refusal is NO_MEMORY */
                return 0;
        ids->table.unit = sizeof(unsigned int);
        if (!wayland_table_reach(&ids->table, ids->count + 1))
                return 0;
        ids->id = (unsigned int *)ids->table.at, ids->id[ids->count++] = id;
        return 1;
}

static void wayland_ids_remove(struct wayland_ids *ids, unsigned int id)
{
        for (long at = 0; at < ids->count; at++)
                if (ids->id[at] == id)
                {
                        ids->id[at] = ids->id[--ids->count];
                        return;
                }
}

static void wayland_ids_drop(struct wayland_ids *ids) { wayland_table_drop(&ids->table); ids->id = 0, ids->count = 0; }

static void wayland_tables_drop(struct wayland_client *c)
{
        for (long at = 0; at < c->tbl[WLT_SOURCE].count; at++)
                wayland_table_drop(&c->sources[at].mime_table);
        for (long k = 0; k < 3; k++)
                wayland_ids_drop(&c->seat_ids[k]);
        wayland_ids_drop(&c->device_ids[0]), wayland_ids_drop(&c->device_ids[1]);
        wayland_ids_drop(&c->relative_ids), wayland_ids_drop(&c->gone);
        for (long k = 0; k < WLT_COUNT; k++)
                wayland_table_drop(&c->tbl[k]), wayland_tbl_point(c, (enum wayland_tbl_kind)k);
        c->out_room = c->out_length = 0;
}

static struct wayland_surface *wayland_tree_next(struct wayland_client *c, const struct wayland_surface *root, struct wayland_surface *cur)
{
        unsigned int ch = cur->first_child[0] ? cur->first_child[0] : cur->first_child[1];

        if (ch)
                return &c->surfaces[ch - 1];
        while (cur != root && cur->sub_parent)
        {
                struct wayland_surface *p = &c->surfaces[cur->sub_parent - 1];

                if (cur->next_sib)
                        return &c->surfaces[cur->next_sib - 1];
                if (!cur->sub_above && p->first_child[1])
                        return &c->surfaces[p->first_child[1] - 1];
                cur = p;
        }
        return 0;
}

/* ---- the wire ---- */

typedef unsigned int __attribute__((may_alias, aligned(1))) wayland_wire_word;
typedef unsigned short __attribute__((may_alias, aligned(1))) wayland_wire_half;
typedef unsigned long long __attribute__((may_alias, aligned(1))) wayland_wire_long;

static inline unsigned long wayland_get(const unsigned char *at) { return *(const wayland_wire_word *)at; }
static inline void wayland_put(unsigned char *at, unsigned long value) { *(wayland_wire_word *)at = (unsigned int)value; }
static inline INLINE void wayland_copy(unsigned char *restrict to, const unsigned char *restrict from, long count)
{
#ifdef HOT_STATE
        if (count > 0) memory_copy(to, from, (positive)count);
#else
        for (long at = 0; at < count; at++) to[at] = from[at];
#endif
}

struct wayland_arg { unsigned long u; long i; const unsigned char *text; long length; int slot; long fd, version; };
struct wayland_event { struct wayland_client *client; unsigned char *at; long start; int ok; };

static unsigned char wayland_sink[WAYLAND_EVENT + 8];

static struct wayland_event wayland_begin(struct wayland_client *c, unsigned long object, long opcode)
{
        struct wayland_event e = {c, 0, c->out_length, !c->dead && wayland_out_has(c)};

        e.at = e.ok ? c->out + c->out_length : wayland_sink;
        if (!e.ok && !c->dead)
                c->dead = 2, wayland_out_mark(c->slot);
        wayland_put(e.at, object), wayland_put(e.at + 4, (unsigned long)opcode);
        e.at += 8;
        return e;
}

static inline void wayland_word(struct wayland_event *e, unsigned long value) { wayland_put(e->at, value); e->at += 4; }

static void wayland_pad_bytes(struct wayland_event *restrict e, const unsigned char *restrict bytes, unsigned long len, int nul)
{
        unsigned long wire = len + (unsigned long)nul, padded = (wire + 3) & ~3UL;

        wayland_word(e, wire);
        if (padded)
                *(wayland_wire_word *)(e->at + padded - 4) = 0;
        if (len)
                wayland_copy(e->at, bytes, (long)len);
        e->at += padded;
}

static void wayland_string(struct wayland_event *e, const char *text)
{
        unsigned long len = 0;

        while (text[len] && len < 255)
                len++;
        wayland_pad_bytes(e, (const unsigned char *)text, len, 1);
}

static void wayland_words(struct wayland_event *e, const unsigned long *words, long count)
{
        wayland_word(e, (unsigned long)count * 4);
        for (long at = 0; at < count; at++)
                wayland_word(e, words[at]);
}

HOT static void wayland_send(struct wayland_client *restrict c, unsigned long object, long opcode, const unsigned int *restrict words, long count)
{
        int ok = !c->dead && wayland_out_has(c);
        long size;

        if (!ok && !c->dead)
                c->dead = 2;
        size = (long)wire_event(ok ? c->out + c->out_length : wayland_sink, object, (unsigned long long)opcode, words, (unsigned long long)count);
        if (ok)
                c->out_length += size;
        if (c->out_length || c->dead)
                wayland_out_mark(c->slot);
}

static inline __attribute__((always_inline)) void wayland_emit(struct wayland_client *c, unsigned long object, long opcode,
                                                               const unsigned int *words, long count)
{
        unsigned char *at;

        if (c->dead || !wayland_out_has(c))
        {
                wayland_send(c, object, opcode, words, count);
                return;
        }
        at = c->out + c->out_length;
        wayland_put(at, object);
        wayland_put(at + 4, (unsigned long)opcode | (unsigned long)(8 + 4 * count) << 16);
#pragma GCC unroll 8
        for (long word = 0; word < count; word++)
                wayland_put(at + 8 + 4 * word, words[word]);
        c->out_length += 8 + 4 * count;
        wayland_out_mark(c->slot);
}

static void wayland_end(struct wayland_event *e)
{
        unsigned char *base = e->ok ? e->client->out + e->start : wayland_sink;
        long size = (long)(e->at - base);

        *(wayland_wire_half *)(base + 6) = (unsigned short)size;
        if (e->ok)
                e->client->out_length += size, wayland_out_mark(e->client->slot);
}

static inline void wayland_ev0(struct wayland_client *c, unsigned long object, long opcode) { wayland_send(c, object, opcode, 0, 0); }

#define wayland_ev(c, obj, op, ...) wayland_send((c), (obj), (op), (const unsigned int[]){__VA_ARGS__}, (long)(sizeof((const unsigned int[]){__VA_ARGS__}) / sizeof(unsigned int)))

static void wayland_ev_str(struct wayland_client *c, unsigned long object, long opcode, const char *text)
{
        struct wayland_event e = wayland_begin(c, object, opcode);

        if (text) wayland_string(&e, text);
        else wayland_word(&e, 0);
        wayland_end(&e);
}

static void wayland_ev_fd(struct wayland_client *c, unsigned long object, long opcode, const char *text, long fd, long owned)
{
        struct wayland_event e = wayland_begin(c, object, opcode);

        if (text) wayland_string(&e, text);
        wayland_out_fd_put(c, fd, owned, e.start);
        wayland_end(&e);
}

static void wayland_ev_array(struct wayland_client *c, unsigned long id, long op, const unsigned char *bytes, unsigned long len)
{
        struct wayland_event e = wayland_begin(c, id, op);

        wayland_pad_bytes(&e, bytes, len, 0);
        wayland_end(&e);
}

static void wayland_ev_dev(struct wayland_client *c, unsigned long id, long op, unsigned long dev)
{
        unsigned char b[8];

        for (long at = 0; at < 8; at++)
                b[at] = (unsigned char)(dev >> (8 * at));
        wayland_ev_array(c, id, op, b, 8);
}

static int wayland_fail(struct wayland_client *c, unsigned long object, unsigned long code, const char *text)
{
        if (!c->dead)
        {
                struct wayland_event e = wayland_begin(c, 1, WLE_wl_display_error);

                wayland_word(&e, object), wayland_word(&e, code), wayland_string(&e, text);
                wayland_end(&e);
                c->dead = 1, wayland_out_mark(c->slot);
        }
        return WAYLAND_DEAD;
}

/* ---- objects ---- */

static inline INLINE PURE long wayland_slot_of(const struct wayland_client *c, unsigned long id)
{
        if (likely(id < WAYLAND_IDS))
                return id && id < (unsigned long)c->tbl[WLT_INDEX].count ? (long)c->index[id] - 1 : -1;
        return id - WAYLAND_SERVER_BASE < (unsigned long)c->tbl[WLT_SERVER].count ? (long)c->server_index[id - WAYLAND_SERVER_BASE] - 1 : -1;
}

static long wayland_object_new(struct wayland_client *c, unsigned long id, long iface, long version)
{
        long slot;

        if (iface <= WLI_NONE || iface >= WLI_COUNT || wayland_slot_of(c, id) >= 0)
                return -1;
        /* live objects (the table's count less its free slots) are bounded per client: a refused new_id is a client error */
        if (c->tbl[WLT_OBJECT].count - c->tbl[WLT_OBJECT].frees >= WAYLAND_OBJECTS)
                return -1;
        if (!(id >= 1 && id < WAYLAND_CLIENT_IDS_MAX) && !(id >= WAYLAND_SERVER_BASE && id - WAYLAND_SERVER_BASE < WAYLAND_SERVER_IDS))
                return -1;
        if (id < WAYLAND_IDS)
        {
                long next = c->tbl[WLT_INDEX].count > 1 ? c->tbl[WLT_INDEX].count : 1;
                if ((long)id > next + WAYLAND_ID_GAP || !wayland_tbl_reach(c, WLT_INDEX, (long)id + 1))
                        return -1;
        }
        else if (id - WAYLAND_SERVER_BASE >= (unsigned long)c->tbl[WLT_SERVER].count)
                return -1;
        if ((slot = wayland_tbl_alloc(c, WLT_OBJECT)) < 0)
                return -1;
        c->objects[slot] = (struct wayland_object){
                (unsigned int)id, (unsigned char)iface,
                (unsigned char)(version > wl_interfaces[iface].version ? wl_interfaces[iface].version : version), 0, -1, 0
        };
        if (id < WAYLAND_IDS) c->index[id] = (unsigned int)(slot + 1);
        else c->server_index[id - WAYLAND_SERVER_BASE] = (unsigned int)(slot + 1), c->server_live++;
        return wayland.objects++, slot;
}

static void wayland_notify_free(struct wayland_client *c, long idx)
{
        if (idx < 0 || idx >= c->tbl[WLT_NOTIFY].count || !c->notifies[idx].live)
                return;
        c->notifies[idx].live = 0, wayland_tbl_free(c, WLT_NOTIFY, idx);
        if (wayland.idle_notifies > 0)
                wayland.idle_notifies--;
}

static void wayland_region_free(struct wayland_client *c, long idx)
{
        if (idx >= 0 && idx < c->tbl[WLT_REGION].count && c->regions[idx].live)
                c->regions[idx].live = 0, wayland_tbl_free(c, WLT_REGION, idx);
}

static void wayland_object_free(struct wayland_client *c, long slot)
{
        struct wayland_object *o = &c->objects[slot];
        unsigned long id = o->id;

        if (o->iface == WLI_zwp_idle_inhibitor_v1 && c->inhibitors > 0) c->inhibitors--;
        else if (o->iface == WLI_zwp_pointer_gesture_pinch_v1 && c->pinches > 0) c->pinches--;
        else if (o->iface == WLI_ext_idle_notification_v1) wayland_notify_free(c, o->link), o->link = -1;
        else if (o->iface == WLI_wl_region) wayland_region_free(c, o->link), o->link = -1;
        if (id < WAYLAND_IDS) c->index[id] = 0;
        else c->server_index[id - WAYLAND_SERVER_BASE] = 0, c->server_live--;
        o->iface = 0, o->id = 0;
        wayland_tbl_free(c, WLT_OBJECT, slot), wayland.objects--;
}

static void wayland_object_delete(struct wayland_client *c, long slot)
{
        unsigned long id = c->objects[slot].id;

        wayland_object_free(c, slot);
        if (id < WAYLAND_IDS)
                wayland_ev(c, 1, WLE_wl_display_delete_id, (unsigned int)id);
}

static long wayland_find(struct wayland_client *c, unsigned long id, long iface)
{
        long slot = wayland_slot_of(c, id);

        return slot >= 0 && (!iface || c->objects[slot].iface == iface) ? slot : -1;
}

static inline unsigned long wayland_serial(struct wayland_client *c) { return __atomic_add_fetch(&c->serial, 1, __ATOMIC_RELAXED); }

static unsigned long wayland_input_serial(struct wayland_client *c)
{
        unsigned long s = wayland_serial(c);

        return c->input_serial[c->input_next++ % WAYLAND_INPUT_SERIALS] = (unsigned int)s, s;
}

static inline int wayland_pointer_serial_ok(const struct wayland_client *c, unsigned long serial)
{
        return c->pointer_enter && (unsigned int)serial - c->pointer_enter <= (unsigned int)c->serial - c->pointer_enter;
}

static int wayland_input_serial_ok(const struct wayland_client *c, unsigned long serial)
{
        for (long at = 0; serial && at < WAYLAND_INPUT_SERIALS; at++)
                if (c->input_serial[at] == (unsigned int)serial)
                        return 1;
        return 0;
}

/* ---- text, math, surfaces, and buffer release ---- */

static long wayland_text(char *into, const unsigned char *text, long length)
{
        long at = 0, used = 0;

        while (at < length && used < WAYLAND_TEXT - 1)
        {
                unsigned char c = text[at];
                long want = c < 0x80 ? 1 : (c & 0xe0) == 0xc0 ? 2 : (c & 0xf0) == 0xe0 ? 3 : (c & 0xf8) == 0xf0 ? 4 : 0;
                int good = want && at + want <= length && used + want < WAYLAND_TEXT;

                for (long more = 1; good && more < want; more++)
                        good = (text[at + more] & 0xc0) == 0x80;
                if (good && ((want == 2 && c < 0xc2) ||
                             (want == 3 && ((c == 0xe0 && text[at + 1] < 0xa0) || (c == 0xed && text[at + 1] >= 0xa0))) ||
                             (want == 4 && ((c == 0xf0 && text[at + 1] < 0x90) || c > 0xf4 || (c == 0xf4 && text[at + 1] >= 0x90)))))
                        good = 0;
                if (want == 1 && (c < 0x20 || c == 0x7f))
                        good = 0;
                if (!good)
                {
                        into[used++] = '?', at++;
                        continue;
                }
                for (long more = 0; more < want; more++)
                        into[used++] = (char)text[at + more];
                at += want;
        }
        return into[used] = 0, used;
}

static inline long wayland_min(long a, long b) { return a < b ? a : b; }
static inline long wayland_max(long a, long b) { return a > b ? a : b; }
static inline long wayland_clamp(long v, long lo, long hi) { return v < lo ? lo : v > hi ? hi : v; }
static inline long wayland_percent120(long percent) { return (wayland_clamp(percent, 25, 800) * WAYLAND_UNIT + 50) / 100; }
static inline long wayland_whole_scale(long percent) { return wayland_max(1, (wayland_min(percent, 800) + 99) / 100); }
static inline long wayland_div120(long v, long pb, long nb) { return v >= 0 ? (v + pb) / WAYLAND_UNIT : -((-v + nb) / WAYLAND_UNIT); }
static inline long wayland_pixels(long u, long s) { return wayland_div120(u * s, WAYLAND_UNIT / 2, WAYLAND_UNIT / 2); }
static inline long wayland_pixels_floor(long u, long s) { return wayland_div120(u * s, 0, WAYLAND_UNIT - 1); }
static inline long wayland_pixels_ceil(long u, long s) { return wayland_div120(u * s, WAYLAND_UNIT - 1, 0); }
static inline unsigned long wayland_units_fixed(long p, long s) { return (unsigned long)(s == WAYLAND_UNIT || s <= 0 ? p * 256 : p * 256 * WAYLAND_UNIT / s); }
static inline long wayland_units(long p, long s) { return s == WAYLAND_UNIT || s <= 0 ? p : p * WAYLAND_UNIT / s; }

static inline struct wayland_rect wayland_cover(struct wayland_rect r, long s)
{
        long x1 = wayland_pixels_floor(r.x, s), y1 = wayland_pixels_floor(r.y, s);
        return (struct wayland_rect){x1, y1, wayland_pixels_ceil(r.x + r.width, s) - x1, wayland_pixels_ceil(r.y + r.height, s) - y1};
}

static inline long wayland_surface_index(struct wayland_client *c, const struct wayland_surface *s) { return (long)(s - c->surfaces); }

static struct wayland_surface *wayland_surface_of(struct wayland_client *c, long slot)
{
        struct wayland_object *o = &c->objects[slot];
        return o->link >= 0 && o->link < c->tbl[WLT_SURFACE].count && c->surfaces[o->link].live && c->surfaces[o->link].gen == o->aux
                   ? &c->surfaces[o->link] : 0;
}

static inline void wayland_surface_link(struct wayland_client *c, long slot, struct wayland_surface *s)
{
        if (slot >= 0 && s)
                c->objects[slot].link = (int)wayland_surface_index(c, s), c->objects[slot].aux = s->gen;
}

static void wayland_timeline_drop(struct wayland_client *c, long idx)
{
        struct wayland_sync_timeline *t = &c->timelines[idx];

        if (wayland.syncobj.drop && t->handle >= 0)
                wayland.syncobj.drop(wayland.syncobj.context, t->handle);
        t->handle = -1, t->live = 0, wayland_tbl_free(c, WLT_TIMELINE, idx);
}

static void wayland_timeline_unref(struct wayland_client *c, long idx)
{
        if (idx >= 0 && idx < c->tbl[WLT_TIMELINE].count && c->timelines[idx].live && c->timelines[idx].gone && c->timelines[idx].refs <= 0)
                wayland_timeline_drop(c, idx);
}

static inline void wayland_timeline_put(struct wayland_client *c, long idx)
{
        if (idx >= 0 && idx < c->tbl[WLT_TIMELINE].count && c->timelines[idx].live)
                c->timelines[idx].refs--, wayland_timeline_unref(c, idx);
}

static void wayland_buffer_signal_release(struct wayland_client *c, struct wayland_buffer *b)
{
        long idx = b->rel_timeline;

        if (!b->rel_pending)
                return;
        b->rel_pending = 0;
        if (idx >= 0 && idx < c->tbl[WLT_TIMELINE].count && c->timelines[idx].live)
        {
                if (wayland.syncobj.signal && c->timelines[idx].handle >= 0)
                        wayland.syncobj.signal(wayland.syncobj.context, c->timelines[idx].handle, b->rel_point);
                c->timelines[idx].refs--, wayland_timeline_unref(c, idx);
        }
}

static void wayland_release_buffer(struct wayland_client *c, long slot)
{
        long link = c->objects[slot].link;

        if (link >= 0 && link < c->tbl[WLT_BUFFER].count && c->buffers[link].live)
                wayland_buffer_signal_release(c, &c->buffers[link]);
        wayland_ev0(c, c->objects[slot].id, WLE_wl_buffer_release);
}

/* ---- the globals & outputs ---- */

static void wayland_dmabuf_formats(struct wayland_client *c, unsigned long id, long version, int drm);

struct wayland_global { unsigned char iface, version; };

static const struct wayland_global wayland_globals[] = {
        {WLI_wl_compositor, 6}, {WLI_wl_shm, 1}, {WLI_wl_subcompositor, 1}, {WLI_wl_seat, 9}, {WLI_xdg_wm_base, 6},
        {WLI_zxdg_output_manager_v1, 3}, {WLI_wl_data_device_manager, 3}, {WLI_zwp_primary_selection_device_manager_v1, 1},
        {WLI_wp_cursor_shape_manager_v1, 2}, {WLI_zwp_linux_dmabuf_v1, 4}, {WLI_zwp_relative_pointer_manager_v1, 1},
        {WLI_zwp_pointer_constraints_v1, 1}, {WLI_zwp_idle_inhibit_manager_v1, 1}, {WLI_zwp_keyboard_shortcuts_inhibit_manager_v1, 1},
        {WLI_wp_tearing_control_manager_v1, 1}, {WLI_wp_viewporter, 1}, {WLI_wp_fractional_scale_manager_v1, 1}, {WLI_wp_presentation, 2},
        {WLI_xdg_wm_dialog_v1, 1}, {WLI_xdg_system_bell_v1, 1}, {WLI_wp_content_type_manager_v1, 1}, {WLI_wp_alpha_modifier_v1, 1},
        {WLI_xdg_activation_v1, 1}, {WLI_wp_fifo_manager_v1, 1}, {WLI_wp_commit_timing_manager_v1, 1}, {WLI_zwp_pointer_gestures_v1, 3},
        {WLI_ext_idle_notifier_v1, 2}, {WLI_xdg_toplevel_icon_manager_v1, 1}, {WLI_xwayland_shell_v1, 1},
        {WLI_zwp_xwayland_keyboard_grab_manager_v1, 1}, {WLI_zwp_text_input_manager_v3, 1}, {WLI_wp_linux_drm_syncobj_manager_v1, 1},
        {WLI_wl_drm, 2}, {WLI_zwlr_layer_shell_v1, 4}, {WLI_canvas_swapchain_v1, 1},
};
#define WAYLAND_GLOBAL_COUNT ((long)(sizeof wayland_globals / sizeof wayland_globals[0]))

static int wayland_global_offered(const struct wayland_client *c, long name)
{
        long iface = name >= 1 && name <= WAYLAND_GLOBAL_COUNT ? wayland_globals[name - 1].iface : 0;

        if (iface == WLI_zwp_linux_dmabuf_v1) return wayland.dmabuf.count > 0;
        if (iface == WLI_wl_drm) return wayland.dmabuf.count > 0 && wayland.drm_node[0] != 0;
        if (iface == WLI_wp_linux_drm_syncobj_manager_v1) return wayland.syncobj_ok != 0;
        if (iface == WLI_xwayland_shell_v1 || iface == WLI_zwp_xwayland_keyboard_grab_manager_v1)
                return wayland.xwayland_uid >= 0 && (c->uid == wayland.xwayland_uid || (wayland.xwayland_uid == 0 && c->uid == 1000));
        return 1;
}

static int wayland_name_is(const unsigned char *text, long length, const char *name)
{
        long at = 0;

        for (; at < length; at++)
                if (!name[at] || text[at] != (unsigned char)name[at])
                        return 0;
        return name[at] == 0;
}

static inline long wayland_output_name(long screen) { return WAYLAND_OUTPUT_NAME + screen + WAYLAND_SCREENS * wayland.output_age[screen]; }

static long wayland_output_of(long name)
{
        long screen = name >= WAYLAND_OUTPUT_NAME ? (name - WAYLAND_OUTPUT_NAME) % WAYLAND_SCREENS : -1;

        return screen >= 0 && screen < wayland.screens && wayland_output_name(screen) == name ? screen : -1;
}

static void wayland_global_send(struct wayland_client *c, unsigned long registry, long name)
{
        struct wayland_event e = wayland_begin(c, registry, WLE_wl_registry_global);
        long iface = name >= WAYLAND_OUTPUT_NAME ? WLI_wl_output : wayland_globals[name - 1].iface;
        long version = name >= WAYLAND_OUTPUT_NAME ? 4 : wayland_globals[name - 1].version;

        wayland_word(&e, (unsigned long)name), wayland_string(&e, wl_interfaces[iface].name), wayland_word(&e, (unsigned long)version);
        wayland_end(&e);
}

static void wayland_x_screen(long output, long *x, long *y)
{
        long ox = 0, oy = 0;

        for (long at = 0; at < wayland.screens; at++)
                ox = !at || wayland.screen[at].x < ox ? wayland.screen[at].x : ox,
                oy = !at || wayland.screen[at].y < oy ? wayland.screen[at].y : oy;
        *x = wayland.screen[output].x - ox, *y = wayland.screen[output].y - oy;
}

static void wayland_ev_names(struct wayland_client *c, unsigned long id, long op, const char *name)
{
        wayland_ev_str(c, id, op, name), wayland_ev_str(c, id, op + 1, name);
}

static void wayland_output_send(struct wayland_client *c, long slot)
{
        struct wayland_object *o = &c->objects[slot];
        const struct wayland_screen *sc;
        struct wayland_event e;
        long x, y;

        if (o->aux >= wayland.screens)
                return;
        sc = &wayland.screen[o->aux], x = sc->x, y = sc->y;
        if (c->xspace)
                wayland_x_screen(o->aux, &x, &y);
        e = wayland_begin(c, o->id, WLE_wl_output_geometry);
        wayland_word(&e, (unsigned long)x), wayland_word(&e, (unsigned long)y);
        wayland_word(&e, (unsigned long)(sc->physical_width > 0 ? sc->physical_width : (sc->width * 254 + 480) / 960));
        wayland_word(&e, (unsigned long)(sc->physical_height > 0 ? sc->physical_height : (sc->height * 254 + 480) / 960));
        wayland_word(&e, WLN_wl_output_subpixel_unknown), wayland_string(&e, "Moonwater"), wayland_string(&e, sc->name);
        wayland_word(&e, WLN_wl_output_transform_normal), wayland_end(&e);
        wayland_ev(c, o->id, WLE_wl_output_mode, WLN_wl_output_mode_current | WLN_wl_output_mode_preferred,
                   (unsigned int)sc->width, (unsigned int)sc->height, (unsigned int)sc->refresh);
        if (o->version >= 2)
                wayland_ev(c, o->id, WLE_wl_output_scale, c->xspace ? 1u : (unsigned int)wayland_whole_scale(sc->scale));
        if (o->version >= 4)
                wayland_ev_names(c, o->id, WLE_wl_output_name, sc->name);
        if (o->version >= 2)
                wayland_ev0(c, o->id, WLE_wl_output_done);
}

static long wayland_logical_axis(long screen, int vertical, int depth)
{
        const struct wayland_screen *self = &wayland.screen[screen];
        long start = vertical ? self->y : self->x, across = vertical ? self->x : self->y;
        long across_size = vertical ? self->width : self->height, best = -1, best_end = 0, scale120 = wayland_percent120(self->scale);

        for (long at = 0; at < wayland.screens && depth < WAYLAND_SCREENS; at++)
        {
                const struct wayland_screen *o = &wayland.screen[at];
                long o_start = vertical ? o->y : o->x, o_size = vertical ? o->height : o->width;
                long o_across = vertical ? o->x : o->y, o_across_size = vertical ? o->width : o->height, stop = o_start + o_size;

                if (at != screen && stop <= start && o_across < across + across_size && across < o_across + o_across_size &&
                    (best < 0 || stop > best_end))
                        best = at, best_end = stop;
        }
        if (best < 0)
                return wayland_units(start, scale120);
        return wayland_logical_axis(best, vertical, depth + 1) +
               wayland_units(vertical ? wayland.screen[best].height : wayland.screen[best].width, wayland_percent120(wayland.screen[best].scale)) +
               wayland_units(start - best_end, scale120);
}

static void wayland_xdg_output_send(struct wayland_client *c, long slot, long output)
{
        const struct wayland_screen *sc;
        unsigned long id = c->objects[slot].id;
        long scale120, x, y;

        if (output >= wayland.screens)
                return;
        sc = &wayland.screen[output];
        scale120 = c->xspace ? WAYLAND_UNIT : wayland_percent120(sc->scale);
        if (c->xspace) wayland_x_screen(output, &x, &y);
        else x = wayland_logical_axis(output, 0, 0), y = wayland_logical_axis(output, 1, 0);
        wayland_ev(c, id, WLE_zxdg_output_v1_logical_position, (unsigned int)x, (unsigned int)y);
        wayland_ev(c, id, WLE_zxdg_output_v1_logical_size, (unsigned int)wayland_units(sc->width, scale120), (unsigned int)wayland_units(sc->height, scale120));
        if (c->objects[slot].version >= 2)
                wayland_ev_names(c, id, WLE_zxdg_output_v1_name, sc->name);
        if (c->objects[slot].version < 3)
                wayland_ev0(c, id, WLE_zxdg_output_v1_done);
}

static inline long wayland_surface_unit(const struct wayland_surface *s)
{
        return s->told120 <= 0 ? (s->scale120 > 0 ? s->scale120 : WAYLAND_UNIT) : (s->fractional_id ? s->told120 : WAYLAND_UNIT * s->told_whole);
}

/* ---- the windows & callbacks ---- */

static long wayland_screen_at(long x, long y);

static int wayland_xdg_send_serial(struct wayland_client *c, struct wayland_surface *s)
{
        unsigned long serial = wayland_serial(c);

        if (wayland_find(c, s->xdg_id, WLI_xdg_surface) < 0)
                return 0;
        wayland_ev(c, s->xdg_id, WLE_xdg_surface_configure, (unsigned int)serial);
        s->serial[s->serials++] = (unsigned int)serial, s->flags |= WAYLAND_SURFACE_SENT;
        return 1;
}

static void wayland_configure(struct wayland_client *c, struct wayland_surface *s, long initial)
{
        struct wayland_facts facts = {0}, ask;
        struct wayland_event e;
        unsigned long states[16];
        long count = 0, slot;

        if (s->serials >= WAYLAND_SERIALS || s->role == WAYLAND_ROLE_NONE)
                return;
        if (s->row >= 0)
                wayland_row_facts(s->row, &facts);
        ask = facts;
        if (s->role == WAYLAND_ROLE_TOPLEVEL)
        {
                static const struct { short mask; unsigned char min_ver, wln; } smap[] = {
                        {WAYLAND_STATE_MAXIMIZED, 1, WLN_xdg_toplevel_state_maximized},
                        {WAYLAND_STATE_FULLSCREEN, 1, WLN_xdg_toplevel_state_fullscreen},
                        {WAYLAND_STATE_RESIZING, 1, WLN_xdg_toplevel_state_resizing},
                        {WAYLAND_STATE_ACTIVATED, 1, WLN_xdg_toplevel_state_activated},
                        {WAYLAND_STATE_TILED_LEFT, 2, WLN_xdg_toplevel_state_tiled_left},
                        {WAYLAND_STATE_TILED_RIGHT, 2, WLN_xdg_toplevel_state_tiled_right},
                        {WAYLAND_STATE_TILED_TOP, 2, WLN_xdg_toplevel_state_tiled_top},
                        {WAYLAND_STATE_TILED_BOTTOM, 2, WLN_xdg_toplevel_state_tiled_bottom},
                        {WAYLAND_STATE_SUSPENDED, 6, WLN_xdg_toplevel_state_suspended},
                };
                long unit = wayland_surface_unit(s), version, cw, ch;

                if (s->row < 0 && s->u.top.want_fullscreen)
                {
                        if (wayland.screens)
                        {
                                const struct wayland_screen *own =
                                        &wayland.screen[s->u.top.want_screen >= 0 && s->u.top.want_screen < wayland.screens
                                                                ? s->u.top.want_screen : wayland_screen_at(ask.x, ask.y)];
                                ask.x = own->x, ask.y = own->y, ask.width = own->width, ask.height = own->height;
                        }
                        ask.states |= WAYLAND_STATE_FULLSCREEN | WAYLAND_STATE_ACTIVATED;
                }
                else if (s->row < 0 && s->u.top.want_maximized)
                {
                        if (wayland.screens)
                        {
                                const struct wayland_screen *own = &wayland.screen[wayland_screen_at(ask.x, ask.y)];
                                ask.x = own->x, ask.y = own->y, ask.width = own->width, ask.height = own->height;
                        }
                        ask.states |= WAYLAND_STATE_MAXIMIZED | WAYLAND_STATE_ACTIVATED;
                }
                if ((ask.states & (WAYLAND_STATE_MAXIMIZED | WAYLAND_STATE_FULLSCREEN)) && (ask.width <= 0 || ask.height <= 0) && wayland.screens)
                {
                        const struct wayland_screen *own = &wayland.screen[wayland_screen_at(ask.x, ask.y)];
                        ask.width = own->width, ask.height = own->height;
                }
                if ((slot = wayland_find(c, s->role_id, WLI_xdg_toplevel)) < 0)
                        return;
                version = c->objects[slot].version;
                for (unsigned long i = 0; i < sizeof smap / sizeof smap[0]; i++)
                        if (version >= smap[i].min_ver && (ask.states & smap[i].mask))
                                states[count++] = smap[i].wln;
                if (initial && version >= 5)
                {
                        static const unsigned long caps[4] = {WLN_xdg_toplevel_wm_capabilities_window_menu,
                                                              WLN_xdg_toplevel_wm_capabilities_maximize,
                                                              WLN_xdg_toplevel_wm_capabilities_fullscreen, 4};
                        e = wayland_begin(c, s->role_id, WLE_xdg_toplevel_wm_capabilities), wayland_words(&e, caps, 4), wayland_end(&e);
                }
                if (initial && version >= 4 && wayland.screens)
                {
                        const struct wayland_screen *own = &wayland.screen[wayland_screen_at(ask.x, ask.y)];
                        long bw = wayland_units(own->width, unit), bh = wayland_units(own->height, unit);
                        if (bw > 0)
                                wayland_ev(c, s->role_id, WLE_xdg_toplevel_configure_bounds, (unsigned int)bw, (unsigned int)bh);
                }
                cw = ask.width > 0 ? wayland_max(1, wayland_units(ask.width, unit)) : 0;
                ch = ask.height > 0 ? wayland_max(1, wayland_units(ask.height, unit)) : 0;
                if (cw > 0 && ch > 0 && !(ask.states & WAYLAND_STATE_FULLSCREEN))
                {
                        if (s->u.top.max_width > 0) cw = wayland_min(cw, s->u.top.max_width);
                        if (s->u.top.max_height > 0) ch = wayland_min(ch, s->u.top.max_height);
                        if (s->u.top.min_width > 0) cw = wayland_max(cw, s->u.top.min_width);
                        if (s->u.top.min_height > 0) ch = wayland_max(ch, s->u.top.min_height);
                }
                e = wayland_begin(c, s->role_id, WLE_xdg_toplevel_configure);
                wayland_word(&e, (unsigned long)cw), wayland_word(&e, (unsigned long)ch), wayland_words(&e, states, count), wayland_end(&e);
                s->u.top.told_width = facts.width, s->u.top.told_height = facts.height;
                s->u.top.told_states = facts.states, s->told_unit = unit, s->u.top.told = 1;
        }
        if (wayland_xdg_send_serial(c, s) && s->role == WAYLAND_ROLE_TOPLEVEL)
                s->u.top.configure_frame = wayland.frames + 1, s->u.top.configure_late = 0;
}

static void wayland_constraints_update(void);
static void wayland_counts_send(int frame);
static void wayland_let_go(struct wayland_client *c, long index);
static void wayland_lent_let(struct wayland_client *c, long row, unsigned int index1);
static struct wayland_surface *wayland_root_of(struct wayland_client *c, struct wayland_surface *s);
static void wayland_presentings_discard(struct wayland_client *c, struct wayland_surface *s);
static void wayland_texts_row_gone(struct wayland_client *c, long row);
static void wayland_pool_unref(struct wayland_client *c, long pool);

static void wayland_stand_lose(long row);

static void wayland_row_release(struct wayland_client *c, struct wayland_surface *s)
{
        unsigned int held[4] = {s->lent, s->lent_gone, s->lent_latched, s->lent_shown};

        if (s->row < 0)
                return;
        wayland_texts_row_gone(c, s->row), wayland_stand_lose(s->row);
        wayland.row[s->row] = (struct wayland_row){-1, -1, 0, -1, -1};
        if (wayland.pointer_row == s->row) wayland.pointer_row = -1;
        if (wayland.keyboard_row == s->row) wayland.keyboard_row = -1;
        wayland.host->row_close(wayland.host->context, s->row);
        s->lent = s->lent_gone = s->lent_latched = s->lent_shown = 0, s->layered = s->laying = 0;
        for (long at = 0; at < 4; at++)
                wayland_lent_let(c, s->row, held[at]);
        for (struct wayland_surface *ch = wayland_tree_next(c, s, s); ch; ch = wayland_tree_next(c, s, ch))
        {
                // (a subsurface that was lifted holds a latched and a shown buffer too)
                unsigned int kept[4] = {ch->lent, ch->lent_gone, ch->lent_latched, ch->lent_shown};
                ch->lent = ch->lent_gone = ch->lent_latched = ch->lent_shown = 0;
                for (long at = 0; at < 4; at++)
                        wayland_lent_let(c, s->row, kept[at]);
        }
        wayland.rows--, wayland_presentings_discard(c, s), s->row = -1;
        if (!wayland.rows)
                wayland_table_drop(&wayland.row_table), wayland.row = 0, wayland.row_made = 0;
        s->flags &= ~(unsigned)WAYLAND_SURFACE_MAPPED, s->closes = 0;
        wayland_constraints_update();
}

static void wayland_callback_answer(struct wayland_client *c, unsigned int id, long time)
{
        long slot = wayland_find(c, id, WLI_wl_callback);
        if (slot >= 0)
                wayland_ev(c, id, WLE_wl_callback_done, (unsigned int)time), wayland_object_delete(c, slot);
}

static void wayland_callbacks_done(struct wayland_client *c, struct wayland_surface *s, long time)
{
        if (s->cbq[WLCB_READY].count) c->waiting--, wayland.waiting--;
        while (s->cbq[WLCB_READY].count)
                wayland_callback_answer(c, wayland_cb_pop(c, &s->cbq[WLCB_READY]), time);
}

static void wayland_callbacks_take(struct wayland_client *c, struct wayland_surface *s)
{
        if (!s->cbq[WLCB_CALLBACK].count)
                return;
        if (!s->cbq[WLCB_READY].count) c->waiting++, wayland.waiting++;
        wayland_cb_splice(c, &s->cbq[WLCB_READY], &s->cbq[WLCB_CALLBACK]);
}

static void wayland_feedback_discard(struct wayland_client *c, unsigned int id)
{
        long slot = wayland_find(c, id, WLI_wp_presentation_feedback);
        if (slot >= 0)
                wayland_ev0(c, id, WLE_wp_presentation_feedback_discarded), wayland_object_delete(c, slot);
}

static void wayland_presenting_drop(struct wayland_client *c, struct wayland_surface *o)
{
        if (o->cbq[WLCB_PRESENTING].count) c->presenting_surfaces--;
        while (o->cbq[WLCB_PRESENTING].count)
                wayland_feedback_discard(c, wayland_cb_pop(c, &o->cbq[WLCB_PRESENTING]));
        o->presentings_framed = 0;
}

static void wayland_presentings_discard(struct wayland_client *c, struct wayland_surface *s)
{
        for (struct wayland_surface *o = s; o; o = wayland_tree_next(c, s, o))
                wayland_presenting_drop(c, o);
}

static void wayland_feedback_take(struct wayland_client *c, struct wayland_surface *s, int shown)
{
        struct wayland_cbq *pq = &s->cbq[WLCB_PRESENTING], *fq = &s->cbq[WLCB_FEEDBACK];
        long keep = !shown ? pq->count : !wayland.framing ? 0 : wayland_min(s->presentings_framed, pq->count);

        if (shown && keep < pq->count)
        {
                if (!keep)
                        wayland_presenting_drop(c, s);
                else
                {
                        unsigned int cur = pq->head, drop;
                        for (long i = 1; i < keep; i++)
                                cur = c->cbs[cur - 1].next;
                        drop = c->cbs[cur - 1].next, c->cbs[cur - 1].next = 0, pq->tail = cur, pq->count = keep;
                        for (unsigned int next; drop; drop = next)
                        {
                                unsigned int id = c->cbs[drop - 1].id;
                                next = c->cbs[drop - 1].next, wayland_tbl_free(c, WLT_CB, (long)(drop - 1)), wayland_feedback_discard(c, id);
                        }
                }
        }
        if (shown && fq->count)
        {
                if (!pq->count) c->presenting_surfaces++;
                wayland_cb_splice(c, pq, fq);
        }
        else if (!shown)
                while (fq->count)
                        wayland_feedback_discard(c, wayland_cb_pop(c, fq));
}

/* ---- subsurfaces, pictures, and layers ---- */

static struct wayland_surface *wayland_root_of(struct wayland_client *c, struct wayland_surface *s)
{
        for (long hops = 0; s->role == WAYLAND_ROLE_SUBSURFACE && s->sub_parent && hops < c->tbl[WLT_SURFACE].count; hops++)
                s = &c->surfaces[s->sub_parent - 1];
        return s;
}

static void wayland_sub_unlink(struct wayland_client *c, struct wayland_surface *s)
{
        struct wayland_surface *p;
        int side;

        if (!s->sub_parent)
                return;
        p = &c->surfaces[s->sub_parent - 1], side = s->sub_above ? 1 : 0;
        *(s->prev_sib ? &c->surfaces[s->prev_sib - 1].next_sib : &p->first_child[side]) = s->next_sib;
        *(s->next_sib ? &c->surfaces[s->next_sib - 1].prev_sib : &p->last_child[side]) = s->prev_sib;
        s->prev_sib = s->next_sib = 0;
}

static void wayland_sub_link_after(struct wayland_client *c, struct wayland_surface *p, struct wayland_surface *s, int side, unsigned int after)
{
        unsigned int si = (unsigned int)(wayland_surface_index(c, s) + 1);
        unsigned int next = after ? c->surfaces[after - 1].next_sib : p->first_child[side];

        s->sub_above = (unsigned char)side, s->prev_sib = after, s->next_sib = next;
        *(after ? &c->surfaces[after - 1].next_sib : &p->first_child[side]) = si;
        *(next ? &c->surfaces[next - 1].prev_sib : &p->last_child[side]) = si;
}

static void wayland_sub_offset(struct wayland_client *c, struct wayland_surface *s, long *x, long *y)
{
        long dx = 0, dy = 0, scale = wayland_root_of(c, s)->scale120;

        for (long hops = 0; s->role == WAYLAND_ROLE_SUBSURFACE && s->sub_parent && hops < c->tbl[WLT_SURFACE].count; hops++)
                dx += s->u.sub.x, dy += s->u.sub.y, s = &c->surfaces[s->sub_parent - 1];
        scale = scale > 0 ? scale : WAYLAND_UNIT;
        *x = wayland_pixels(dx, scale), *y = wayland_pixels(dy, scale);
}

static int wayland_sub_synced(struct wayland_client *c, struct wayland_surface *s)
{
        for (long hops = 0; s->role == WAYLAND_ROLE_SUBSURFACE && s->sub_parent && hops < c->tbl[WLT_SURFACE].count; hops++)
        {
                if (s->u.sub.sync)
                        return 1;
                s = &c->surfaces[s->sub_parent - 1];
        }
        return 0;
}

static void wayland_let_go(struct wayland_client *c, long index)
{
        struct wayland_buffer *b = &c->buffers[index];

        if (--b->held > 0)
                return;
        wayland_buffer_signal_release(c, b);
        if (b->zombie)
        {
                c->pools[b->pool].refs--, wayland_pool_unref(c, b->pool);
                b->live = 0, b->zombie = 0, wayland_tbl_free(c, WLT_BUFFER, index), wayland.buffers--;
        }
        else
        {
                long slot = wayland_find(c, b->id, WLI_wl_buffer);
                if (slot >= 0)
                        wayland_release_buffer(c, slot);
        }
}

static void wayland_lent_let(struct wayland_client *c, long row, unsigned int index1)
{
        if (index1 && (!wayland_row_busy(row) || !wayland_ids_add(&c->gone, index1)))
                wayland_let_go(c, index1 - 1);
}

static void wayland_gone_let(struct wayland_client *c, struct wayland_surface *s, struct wayland_buffer *now)
{
        long index = s->lent_gone - 1;

        s->lent_gone = 0;
        if (&c->buffers[index] == now) c->buffers[index].held--;
        else wayland_lent_let(c, wayland_root_of(c, s)->row, (unsigned int)(index + 1));
}

static void wayland_unhold(struct wayland_client *c, struct wayland_surface *s)
{
        if (s->held)
        {
                long index = s->held - 1;
                s->held = 0, wayland_let_go(c, index);
        }
}

static inline int wayland_has_children(const struct wayland_surface *s) { return s->first_child[0] != 0 || s->first_child[1] != 0; }
static inline int wayland_rect_contains(struct wayland_rect a, struct wayland_rect b) { return a.x <= b.x && a.y <= b.y && a.x + a.width >= b.x + b.width && a.y + a.height >= b.y + b.height; }

static struct wayland_rect wayland_rect_union(struct wayland_rect a, struct wayland_rect b)
{
        long x1 = wayland_min(a.x, b.x), y1 = wayland_min(a.y, b.y);
        return (struct wayland_rect){x1, y1, wayland_max(a.x + a.width, b.x + b.width) - x1, wayland_max(a.y + a.height, b.y + b.height) - y1};
}

static struct wayland_rect wayland_rect_bounds(const struct wayland_rect *rects, long count)
{
        struct wayland_rect box = rects[0];
        for (long i = 1; i < count; i++) box = wayland_rect_union(box, rects[i]);
        return box;
}

static long wayland_rect_sub(struct wayland_rect b, struct wayland_rect sub, struct wayland_rect out[4])
{
        long bx2 = b.x + b.width, by2 = b.y + b.height, sx2 = sub.x + sub.width, sy2 = sub.y + sub.height, n = 0;

        if (b.width <= 0 || b.height <= 0)
                return 0;
        if (sub.x >= bx2 || sx2 <= b.x || sub.y >= by2 || sy2 <= b.y)
                return out[0] = b, 1;
        long iy1 = wayland_max(b.y, sub.y), iy2 = wayland_min(by2, sy2);
        struct wayland_rect p[4] = {{b.x, b.y, b.width, sub.y - b.y}, {b.x, sy2, b.width, by2 - sy2},
                                    {b.x, iy1, sub.x - b.x, iy2 - iy1}, {sx2, iy1, bx2 - sx2, iy2 - iy1}};
        for (long k = 0; k < 4; k++)
                if (p[k].width > 0 && p[k].height > 0)
                        out[n++] = p[k];
        return n;
}

static void wayland_window_rect(struct wayland_client *c, struct wayland_surface *root, long width, long height, long scale120,
                                long *x, long *y, long *w, long *h)
{
        struct wayland_rect box = {0, 0, width, height};

        if (!wayland_has_children(root) && !root->geometry_set)
        {
                *x = *y = 0, *w = wayland_min(width, WAYLAND_DIMENSION), *h = wayland_min(height, WAYLAND_DIMENSION);
                return;
        }
        for (struct wayland_surface *ch = wayland_tree_next(c, root, root); ch; ch = wayland_tree_next(c, root, ch))
        {
                long ox, oy;
                if (!ch->held) continue;
                wayland_sub_offset(c, ch, &ox, &oy);
                box = wayland_rect_union(box, (struct wayland_rect){wayland_clamp(ox, -WAYLAND_DIMENSION, WAYLAND_DIMENSION),
                                                                    wayland_clamp(oy, -WAYLAND_DIMENSION, WAYLAND_DIMENSION), ch->width, ch->height});
        }
        if (root->geometry_set && scale120 >= WAYLAND_SCALE_LEAST)
        {
                long gx1 = wayland_max(wayland_pixels(root->geometry_x, scale120), box.x);
                long gy1 = wayland_max(wayland_pixels(root->geometry_y, scale120), box.y);
                long gx2 = wayland_min(wayland_pixels(root->geometry_x + root->geometry_width, scale120), box.x + box.width);
                long gy2 = wayland_min(wayland_pixels(root->geometry_y + root->geometry_height, scale120), box.y + box.height);
                if (gx2 > gx1 && gy2 > gy1)
                        box = (struct wayland_rect){gx1, gy1, gx2 - gx1, gy2 - gy1};
        }
        *x = box.x, *y = box.y, *w = wayland_min(box.width, WAYLAND_DIMENSION), *h = wayland_min(box.height, WAYLAND_DIMENSION);
}

static struct wayland_rect wayland_window_of(struct wayland_client *c, struct wayland_surface *root)
{
        struct wayland_rect w;
        wayland_window_rect(c, root, root->width, root->height, root->scale120, &w.x, &w.y, &w.width, &w.height);
        return w;
}

static void wayland_row_take(struct wayland_client *c, struct wayland_surface *root)
{
        struct wayland_picture none;
        struct wayland_rect w;

        if (root->row < 0 || !root->laying || wayland_row_stood(root->row))
                return;
        root->laying = 0, memory_zero(&none, sizeof none), w = wayland_window_of(c, root);
        none.row_width = w.width, none.row_height = w.height, none.last = 1, none.sync_fd = -1;
        none.plane_fd[0] = none.plane_fd[1] = none.plane_fd[2] = none.plane_fd[3] = -1;
        wayland.host->row_picture(wayland.host->context, root->row, &none);
}

static int wayland_budget_left(struct wayland_surface *root)
{
        long now = wayland_now(), most = wayland.screens ? 0 : 1920L * 1080L;

        for (long at = 0; at < wayland.screens; at++)
                most = wayland_max(most, wayland.screen[at].width * wayland.screen[at].height);
        if (now - root->budget_at >= WAYLAND_BUDGET_MS || now < root->budget_at)
                root->budget_at = now, root->budget_used = 0;
        return root->budget_used < WAYLAND_BUDGET_SCREENS * most;
}

static inline int wayland_format_has_alpha(long f)
{
        return f == WLN_wl_shm_format_argb8888 || f == WLN_wl_shm_format_abgr8888 ||
               f == (long)WAYLAND_FOURCC_ARGB2101010 || f == (long)WAYLAND_FOURCC_ABGR2101010;
}

static inline void wayland_transformed_size(long w, long h, long t, long *ow, long *oh) { *ow = (t & 1) ? h : w, *oh = (t & 1) ? w : h; }

static void wayland_surface_target_pixels(const struct wayland_surface *s, const struct wayland_surf_state *st,
                                          const struct wayland_buffer *b, long *ow, long *oh)
{
        long bw_t, bh_t, disp = s->told120 > 0 ? s->told120 : WAYLAND_UNIT;

        wayland_transformed_size(b->width, b->height, st->transform, &bw_t, &bh_t);
        *ow = wayland_min(st->dst_w > 0 ? wayland_pixels(st->dst_w, disp) : st->src_w > 0 ? (st->src_w * st->scale + 255) / 256 : bw_t, WAYLAND_DIMENSION);
        *oh = wayland_min(st->dst_w > 0 ? wayland_pixels(st->dst_h, disp) : st->src_w > 0 ? (st->src_h * st->scale + 255) / 256 : bh_t, WAYLAND_DIMENSION);
}

static inline int wayland_surface_is_opaque_at(const struct wayland_surface *s, const struct wayland_surf_state *st, const struct wayland_buffer *b)
{
        long sw, sh, bw_t, bh_t, scale = st->scale > 0 ? st->scale : 1;

        if (!wayland_format_has_alpha(b->format)) return 1;
        if (s->cur.opaque.width <= 0 || s->cur.opaque.height <= 0 || s->cur.opaque.x > 0 || s->cur.opaque.y > 0) return 0;
        wayland_transformed_size(b->width, b->height, st->transform, &bw_t, &bh_t);
        sw = st->dst_w > 0 ? st->dst_w : st->src_w > 0 ? (st->src_w + 255) / 256 : (bw_t + scale - 1) / scale;
        sh = st->dst_w > 0 ? st->dst_h : st->src_w > 0 ? (st->src_h + 255) / 256 : (bh_t + scale - 1) / scale;
        return s->cur.opaque.x + s->cur.opaque.width >= sw && s->cur.opaque.y + s->cur.opaque.height >= sh;
}

static inline int wayland_surface_is_opaque(const struct wayland_surface *s, const struct wayland_buffer *b) { return wayland_surface_is_opaque_at(s, &s->cur, b); }

static void wayland_picture_opaque(struct wayland_picture *p, const struct wayland_surface *s, const struct wayland_surf_state *st,
                                   long scale120, long wx, long wy, long ww, long wh)
{
        long x1, y1, x2, y2;

        p->opaque_x = p->opaque_y = p->opaque_w = p->opaque_h = 0;
        if (!p->transparent || s->cur.opaque.width <= 0 || s->cur.opaque.height <= 0 || st->transform || st->src_w > 0 || st->dst_w > 0)
                return;
        x1 = wayland_max(wayland_pixels_ceil(s->cur.opaque.x, scale120) - wx, 0);
        y1 = wayland_max(wayland_pixels_ceil(s->cur.opaque.y, scale120) - wy, 0);
        x2 = wayland_min(wayland_pixels_floor(s->cur.opaque.x + s->cur.opaque.width, scale120) - wx, ww);
        y2 = wayland_min(wayland_pixels_floor(s->cur.opaque.y + s->cur.opaque.height, scale120) - wy, wh);
        if (x2 > x1 && y2 > y1)
                p->opaque_x = x1, p->opaque_y = y1, p->opaque_w = x2 - x1, p->opaque_h = y2 - y1;
}

static int wayland_crop_plain(const struct wayland_surf_state *st, const struct wayland_buffer *b, long tw, long th)
{
        return st->src_w > 0 && !st->transform && !b->y_invert && !st->src_x && !st->src_y && st->scale == 1 &&
               !(st->src_w & 255) && !(st->src_h & 255) && st->src_w / 256 <= b->width && st->src_h / 256 <= b->height &&
               tw == st->src_w / 256 && th == st->src_h / 256;
}

static void wayland_picture_init(struct wayland_picture *pic, const struct wayland_surf_state *st, const struct wayland_buffer *b,
                                 const struct wayland_pool *p, long tw, long th, long dest_x, long dest_y, long row_w, long row_h)
{
        long bw_t, bh_t;

        memory_zero(pic, sizeof *pic);
        wayland_transformed_size(b->width, b->height, st->transform, &bw_t, &bh_t);
        pic->pixels = p->unmapped ? 0 : (const unsigned char *)p->at + b->offset;
        pic->map = p->unmapped ? 0 : (const unsigned char *)p->at, pic->map_bytes = p->unmapped ? 0 : p->size;
        pic->stride = b->stride, pic->width = b->width, pic->height = b->height, pic->format = b->format;
        pic->sync_fd = p->dmabuf ? p->fd : -1, pic->explicit_sync = b->explicit_sync, pic->pool_id = p->id;
        pic->transform = st->transform, pic->y_invert = b->y_invert, pic->modifier = b->modifier;
        pic->fourcc = b->fourcc ? b->fourcc : (unsigned int)(b->format == WLN_wl_shm_format_argb8888 ? WAYLAND_FOURCC_ARGB8888
                                                              : b->format == WLN_wl_shm_format_xrgb8888 ? WAYLAND_FOURCC_XRGB8888 : (unsigned long)b->format);
        pic->plane_count = b->plane_count > 0 ? b->plane_count : 1;
        for (long k = 0; k < 4; k++)
        {
                pic->plane_fd[k] = !p->dmabuf || k >= pic->plane_count ? -1 : k == 0 ? p->fd : p->extra_fd[k - 1];
                pic->plane_offset[k] = k < pic->plane_count ? b->plane_offset[k] : 0;
                pic->plane_stride[k] = k < pic->plane_count ? b->plane_stride[k] : 0;
        }
        if (st->src_w > 0)
                pic->src_x = st->src_x * st->scale, pic->src_y = st->src_y * st->scale,
                pic->src_width = st->src_w * st->scale, pic->src_height = st->src_h * st->scale;
        if (tw != bw_t || th != bh_t || st->src_w > 0)
                pic->dst_width = tw, pic->dst_height = th;
        if (wayland_crop_plain(st, b, tw, th))
                pic->width = tw, pic->height = th, pic->src_x = pic->src_y = pic->src_width = pic->src_height = pic->dst_width = pic->dst_height = 0;
        pic->dest_x = dest_x, pic->dest_y = dest_y, pic->row_width = row_w, pic->row_height = row_h, pic->full = 1;
}

static void wayland_picture_clip_damage(struct wayland_picture *p, const struct wayland_surf_state *st, long scale120, long bw, long bh)
{
        unsigned long long kept;

        for (long at = 0; at < st->damages; at++)
                p->damage[at] = st->damage_kind[at] ? st->damage[at] : wayland_cover(st->damage[at], scale120);
        kept = rects_clip((long long *)p->damage, (unsigned long long)st->damages, (unsigned long long)bw, (unsigned long long)bh);
        p->damages = (long)(kept << 1 >> 1), p->full = (kept >> 63) || !p->damages;
}

static void wayland_child_copy(struct wayland_client *c, struct wayland_surface *child, const struct wayland_rect *only,
                               const struct wayland_rect *window)
{
        struct wayland_surface *root = wayland_root_of(c, child);
        struct wayland_picture picture;
        struct wayland_buffer *b;
        long ox, oy, tw, th;
        unsigned int shown = child->held ? child->held : child == root ? child->lent : 0;

        if (root->row < 0 || !shown || (child != root && !child->sub_parent) || wayland_row_stood(root->row))
                return;
        b = &c->buffers[shown - 1], wayland_sub_offset(c, child, &ox, &oy), wayland_surface_target_pixels(child, &child->cur, b, &tw, &th);
        wayland_picture_init(&picture, &child->cur, b, &c->pools[b->pool], tw, th, ox - window->x, oy - window->y, window->width, window->height);
        picture.transparent = !wayland_surface_is_opaque(child, b), picture.blend = child != root && picture.transparent;
        if (only && !picture.src_width && !picture.dst_width && !picture.transform && !picture.y_invert)
        {
                long x1 = wayland_max(only->x - ox, 0), y1 = wayland_max(only->y - oy, 0);
                long x2 = wayland_min(only->x + only->width - ox, tw), y2 = wayland_min(only->y + only->height - oy, th);
                if (x2 <= x1 || y2 <= y1)
                        return;
                picture.full = 0, picture.damages = 1, picture.damage[0] = (struct wayland_rect){x1, y1, x2 - x1, y2 - y1};
        }
        root->laying = 1;
        if (wayland.host->row_picture(wayland.host->context, root->row, &picture) > 0)
                root->need_full = 1;
        root->budget_used += picture.full ? wayland_min(tw, window->width) * wayland_min(th, window->height) : picture.damage[0].width * picture.damage[0].height;
}

static struct wayland_rect wayland_flatten_reach(struct wayland_client *c, struct wayland_surface *root, struct wayland_rect only)
{
        if (!wayland_has_children(root))
                return only;
        for (struct wayland_surface *ch = wayland_tree_next(c, root, root); ch; ch = wayland_tree_next(c, root, ch))
        {
                long ox, oy, tw, th;
                struct wayland_buffer *b;
                if (!ch->held || !(ch->cur.src_w > 0 || ch->cur.dst_w > 0 || ch->cur.transform || c->buffers[ch->held - 1].y_invert))
                        continue;
                b = &c->buffers[ch->held - 1], wayland_surface_target_pixels(ch, &ch->cur, b, &tw, &th);
                if (wayland_crop_plain(&ch->cur, b, tw, th)) continue;
                wayland_sub_offset(c, ch, &ox, &oy);
                if (ox < only.x + only.width && oy < only.y + only.height && ox + ch->width > only.x && oy + ch->height > only.y)
                        only = wayland_rect_union(only, (struct wayland_rect){ox, oy, ch->width, ch->height});
        }
        return only;
}

static void wayland_flatten(struct wayland_client *c, long parent, const struct wayland_rect *only, const struct wayland_rect *window)
{
        struct wayland_surface *s = &c->surfaces[parent < 0 ? ~parent : parent];

        for (int side = 0; side < 2; side++)
        {
                if (side && parent < 0 && s->held)
                        wayland_child_copy(c, s, only, window);
                for (unsigned int ch = s->first_child[side]; ch; ch = c->surfaces[ch - 1].next_sib)
                        wayland_flatten(c, ~(long)(ch - 1), only, window);
        }
}

static void wayland_lend_hold(struct wayland_client *c, struct wayland_surface *s, unsigned int now, struct wayland_buffer *now_buf)
{
        if (!now || s->lent == now)
                return;
        if (s->lent_gone == now)
                s->lent_gone = 0;
        else
        {
                c->buffers[now - 1].held++;
                if (s->lent_gone)
                        wayland_gone_let(c, s, now_buf);
        }
        if (s->lent) s->lent_gone = s->lent;
        s->lent = now;
}

static void wayland_lend_end(struct wayland_client *c, struct wayland_surface *s, struct wayland_buffer *now_buf)
{
        if (!s->lent)
                return;
        if (s->lent_gone)
                wayland_gone_let(c, s, now_buf);
        s->lent_gone = s->lent, s->lent = 0;
}

static long wayland_picture_submit(struct wayland_client *c, struct wayland_surface *s, struct wayland_buffer *b,
                                   const struct wayland_picture *picture, int promote_held, int *lent_out)
{
        unsigned int now = (unsigned int)((b - c->buffers) + 1);
        long status = wayland.host->row_picture(wayland.host->context, s->row, picture);

        if (lent_out) *lent_out = 0;
        if (status == WAYLAND_PICTURE_LENT)
                return wayland_lend_hold(c, s, now, promote_held ? 0 : b), lent_out ? (*lent_out = 1, 0) : 0;
        if (status == 0)
        {
                if (promote_held && !s->held && s->lent == now) s->held = s->lent, s->lent = 0;
                else if (s->lent) wayland_lend_end(c, s, promote_held ? 0 : b);
        }
        else if (status > 0)
                s->need_full = 1;
        return status;
}

static void wayland_lend_leave(struct wayland_client *c, struct wayland_surface *s)
{
        unsigned int kept[4] = {s->lent, s->lent_gone, s->lent_latched, s->lent_shown};

        s->lent = s->lent_gone = s->lent_latched = s->lent_shown = 0;
        for (long at = 0; at < 4; at++)
                if (kept[at] && !wayland_ids_add(&c->gone, kept[at]))
                        wayland_let_go(c, kept[at] - 1);
}

static void wayland_gone_drawn(struct wayland_client *c, int dmabufs)
{
        long kept = 0;

        for (long at = 0; at < c->gone.count; at++)
        {
                long index = c->gone.id[at] - 1;
                if (!dmabufs && c->pools[c->buffers[index].pool].dmabuf) c->gone.id[kept++] = c->gone.id[at];
                else wayland_let_go(c, index);
        }
        c->gone.count = kept;
}

struct wayland_layering { struct wayland_picture *list; long count; int refused, root_opaque; struct wayland_surface *root; struct wayland_rect window, root_box; };

static void wayland_layers_walk(struct wayland_client *c, long self, struct wayland_layering *l, int below, long depth)
{
        struct wayland_surface *s = &c->surfaces[self];

        if (depth > c->tbl[WLT_SURFACE].count)
        {
                l->refused = 1;
                return;
        }
        for (int side = 0; side < 2 && !l->refused; side++)
        {
                if (side && s != l->root && s->held)
                {
                        if (l->count >= WAYLAND_LAYERS)
                        {
                                l->refused = 1;
                                break;
                        }
                        struct wayland_buffer *b = &c->buffers[s->held - 1];
                        struct wayland_picture *p = &l->list[l->count];
                        long ox, oy;
                        wayland_sub_offset(c, s, &ox, &oy);
                        wayland_picture_init(p, &s->cur, b, &c->pools[b->pool], s->width, s->height,
                                             ox - l->window.x, oy - l->window.y, l->window.width, l->window.height);
                        p->blend = p->transparent = !wayland_surface_is_opaque(s, b);
                        p->layer = wayland_surface_index(c, s) + 1;
                        if (below)
                                l->refused = !l->root_opaque || !wayland_rect_contains(l->root_box, (struct wayland_rect){p->dest_x, p->dest_y, s->width, s->height});
                        else
                                l->count++;
                }
                for (unsigned int ch = s->first_child[side]; ch && !l->refused; ch = c->surfaces[ch - 1].next_sib)
                        wayland_layers_walk(c, (long)(ch - 1), l, below || (!side && s == l->root), depth + 1);
        }
}

static long wayland_layers_make(struct wayland_client *c, struct wayland_surface *root, struct wayland_surface *changed, int full,
                                const struct wayland_rect *window, const struct wayland_rect *root_box, int root_opaque,
                                struct wayland_picture *list)
{
        struct wayland_layering l = {list, 0, 0, root_opaque, root, *window, *root_box};

        wayland_layers_walk(c, wayland_surface_index(c, root), &l, 0, 0);
        if (l.refused)
                return -1;
        for (long at = 0; changed && at < l.count; at++)
                if (list[at].layer == wayland_surface_index(c, changed) + 1)
                {
                        list[at].changed = list[at].full = 1;
                        if (!full && changed->pending.damages && !list[at].transform && !list[at].src_width && !list[at].dst_width)
                                wayland_picture_clip_damage(&list[at], &changed->pending, changed->scale120, list[at].width, list[at].height);
                }
        return l.count;
}

static void wayland_layers_lent(struct wayland_client *c, struct wayland_surface *root, const struct wayland_picture *list, long count)
{
        for (struct wayland_surface *s = wayland_tree_next(c, root, root); s; s = wayland_tree_next(c, root, s))
        {
                long at = wayland_surface_index(c, s);
                int kept = 0;
                for (long k = 0; list && k < count; k++)
                        kept |= list[k].layer == at + 1;
                if (kept) wayland_lend_hold(c, s, s->held, 0);
                else wayland_lend_end(c, s, 0);
        }
        root->layered = list != 0;
}

/*
        A lift (lev A10): a window whose picture is all one subsurface -- opaque, a dma-buf, over the whole window, nothing over it -- shows
        that subsurface's buffer as its picture, as it shows a stand-in's (wayland_stand_show): a client that draws into a subsurface over
        its window (winewayland's client area, SDL with libdecor, a video player's) is shown straight from the buffer it draws, on the plane
        and scaled by it, where its window and layers were composed every frame (1.2-1.8 ms of the card's time and 16.6 MB a 1080p frame).
        What is under it is hidden, so nothing of it is lost. The subsurface keeps its role, its input and its sync: its commits that apply
        (wayland_sub_apply) are the row's pictures, its latch, shown buffer, hints and feedback are the stand-in's (wayland_row_stand), and the
        window's own commits put nothing on the screen while it is lifted (the stood branch of wayland_commit). The lift is looked at again
        wherever the tree can change (wayland_layers: a subsurface's commit, a relay, a window's commit; wayland_sub_drop) and let down the
        moment its rule breaks: the window's own picture and its layers come back (the ROOT branch of wayland_layers). A stand-in of the
        client's (canvas_swapchain_v1) always has the row before a lift. /run/moonwater/lift-off turns it off (wayland_lifts_off).
*/
static void wayland_relay(struct wayland_client *c, struct wayland_surface *root);

struct wayland_lifting { struct wayland_surface *root, *top; int bad; };

// The top surface of the window that has a buffer (the window's own counts as one: nothing under it can be the top).
static void wayland_lift_walk(struct wayland_client *c, struct wayland_surface *s, struct wayland_lifting *l, long depth)
{
        if (depth > c->tbl[WLT_SURFACE].count)
        {
                l->bad = 1;
                return;
        }
        for (int side = 0; side < 2 && !l->bad; side++)
        {
                if (side && (s->held || s == l->root))
                        l->top = s;
                for (unsigned int ch = s->first_child[side]; ch && !l->bad; ch = c->surfaces[ch - 1].next_sib)
                        wayland_lift_walk(c, &c->surfaces[ch - 1], l, depth + 1);
        }
}

// The subsurface the window can be lifted to, or 0.
static struct wayland_surface *wayland_lift_pick(struct wayland_client *c, struct wayland_surface *root)
{
        struct wayland_lifting l = {root, 0, 0};
        struct wayland_surface *t;
        struct wayland_buffer *b;
        struct wayland_rect window;
        long ox, oy;

        if (wayland.lift_off || root->row < 0 || !(root->flags & WAYLAND_SURFACE_MAPPED) || !wayland_has_children(root))
                return 0;
        wayland_lift_walk(c, root, &l, 0);
        if (l.bad || !(t = l.top) || t == root || t->role != WAYLAND_ROLE_SUBSURFACE || !t->held || t->cur.alpha != 0xffffffffu ||
            t->pending.alpha != 0xffffffffu)
                return 0;
        b = &c->buffers[t->held - 1];
        if (!c->pools[b->pool].dmabuf || !wayland_surface_is_opaque(t, b))
                return 0;
        window = wayland_window_of(c, root), wayland_sub_offset(c, t, &ox, &oy);
        if (window.width <= 0 || window.height <= 0 || ox > window.x || oy > window.y || ox + t->width < window.x + window.width ||
            oy + t->height < window.y + window.height)
                return 0;
        return t;
}

// The lifted subsurface's buffer to the row as its picture: the part of it over the window, at the window's size.
static void wayland_lift_show(struct wayland_client *c, struct wayland_surface *root, struct wayland_surface *t, int full)
{
        struct wayland_buffer *b = &c->buffers[t->held - 1];
        struct wayland_picture picture;
        struct wayland_rect window = wayland_window_of(c, root);
        long ox, oy, scale120 = t->scale120 > 0 ? t->scale120 : WAYLAND_UNIT;
        int plain;

        wayland_sub_offset(c, t, &ox, &oy);
        wayland_picture_init(&picture, &t->cur, b, &c->pools[b->pool], t->width, t->height, ox - window.x, oy - window.y, window.width, window.height);
        picture.transparent = 0, picture.last = 1;
        plain = !picture.dest_x && !picture.dest_y && t->width == window.width && t->height == window.height && !picture.src_width &&
                !picture.dst_width && !picture.transform && !picture.y_invert;
        picture.full = full || t->need_full || !plain || t->pending.damage_full || !t->pending.damages;
        if (!picture.full)
                wayland_picture_clip_damage(&picture, &t->pending, scale120, b->width, b->height);
        t->need_full = 0, t->window_x = window.x, t->window_y = window.y, t->window_w = window.width, t->window_h = window.height;
        // (the submit names its row by its surface's: the window's, for the time of the call)
        t->row = root->row;
        wayland_picture_submit(c, t, b, &picture, 0, 0);
        t->row = -1;
}

// The row's lift let down: its subsurface's buffers go back once no frame shows them (wayland_lend_leave), as a stand-in's do.
static void wayland_lift_end(long row)
{
        struct wayland_client *c = wayland_client_of(wayland.row[row].stand_client);
        long at = wayland.row[row].stand_surface;

        wayland.row[row].stand_client = wayland.row[row].stand_surface = -1, wayland.row[row].lifted = 0, wayland.lifts_ended++;
        if (c && at >= 0 && at < c->tbl[WLT_SURFACE].count && c->surfaces[at].live)
                wayland_lend_leave(c, &c->surfaces[at]);
}

// A subsurface going (wayland_sub_drop): the window's lift let down when it is the lifted one or holds it, and the window's picture shown again.
static void wayland_lift_drop(struct wayland_client *c, struct wayland_surface *root, struct wayland_surface *s)
{
        struct wayland_surface *t;
        long row = root->row;

        if (row < 0 || row >= wayland.row_made || !wayland.row[row].lifted || wayland.row[row].stand_client != c->slot ||
            wayland.row[row].stand_surface < 0 || wayland.row[row].stand_surface >= c->tbl[WLT_SURFACE].count)
                return;
        t = &c->surfaces[wayland.row[row].stand_surface];
        for (long hops = 0; t != s && t->role == WAYLAND_ROLE_SUBSURFACE && t->sub_parent && hops < c->tbl[WLT_SURFACE].count; hops++)
                t = &c->surfaces[t->sub_parent - 1];
        if (t == s)
                wayland_lift_end(row), root->need_full = 1, root->window_w = -1;
}

/*
        The window's lift looked at again, with what changed (0: all of it, a relay's): true while the window is lifted (its picture is the
        lifted subsurface's, shown again here when that one changed or the lift moved to it), false when it is not (the caller shows the
        window and its layers; a lift let down here has the window's own picture shown again, its window_w forgotten).
*/
static int wayland_lift(struct wayland_client *c, struct wayland_surface *root, struct wayland_surface *changed, int full)
{
        long row = root->row;
        struct wayland_client *wc;
        struct wayland_surface *was, *now;
        struct wayland_rect window;

        if (row < 0 || row >= wayland.row_made || (wayland_row_stood(row) && !wayland.row[row].lifted))
                return 0;
        was = wayland.row[row].lifted ? wayland_row_stand(row, &wc) : 0;
        now = wayland_lift_pick(c, root);
        if (wayland.row[row].lifted && (was != now || !was))
        {
                wayland_lift_end(row);
                if (!now)
                        return root->need_full = 1, root->window_w = -1, 0;
        }
        if (!now)
                return 0;
        if (!wayland.row[row].lifted)
        {
                // The window's layers given back by the host (none kept), so its picture is the subsurface's alone.
                if (root->layered && wayland.host->row_layers)
                        wayland.host->row_layers(wayland.host->context, root->row, 0, 0, 0);
                wayland_layers_lent(c, root, 0, 0), root->laying = 0; // (the picture below closes what the window had open)
                wayland.row[row].stand_client = c->slot, wayland.row[row].stand_surface = wayland_surface_index(c, now), wayland.row[row].lifted = 1;
                wayland.lifts++, changed = now, full = 1;
        }
        window = wayland_window_of(c, root);
        if (!changed || changed == now || window.x != now->window_x || window.y != now->window_y || window.width != now->window_w ||
            window.height != now->window_h)
                wayland_lift_show(c, root, now, full || changed != now);
        root->need_full = 0;
        return 1;
}

// A lifted window's own commit: what it changed (its size, its subsurfaces' places and order) looked at against the lift.
static void wayland_lift_again(struct wayland_client *c, struct wayland_surface *root)
{
        if (root->row < 0 || root->row >= wayland.row_made || !wayland.row[root->row].lifted)
                return;
        if (!wayland_lift(c, root, root->need_full ? 0 : root, 0))
                root->need_full = 1, wayland_relay(c, root);
}

WAYLAND_API void wayland_lifts_off(int off) { wayland.lift_off = off != 0; }

static int wayland_layers(struct wayland_client *c, struct wayland_surface *root, struct wayland_surface *changed, int full)
{
        struct wayland_picture list[WAYLAND_LAYERS];
        unsigned int shown = root->held ? root->held : root->lent;
        struct wayland_rect window, box;
        long count, status = 0;

        if (wayland_lift(c, root, changed, full))
                return 1;
        if (!wayland.host->row_layers || root->row < 0 || !(root->flags & WAYLAND_SURFACE_MAPPED) || wayland_row_stood(root->row))
                return 0;
        window = wayland_window_of(c, root), box = (struct wayland_rect){-window.x, -window.y, root->width, root->height};
        count = wayland_layers_make(c, root, changed, full, &window, &box, shown && wayland_surface_is_opaque(root, &c->buffers[shown - 1]), list);
        if (count >= 0 && (window.x != root->window_x || window.y != root->window_y || window.width != root->window_w || window.height != root->window_h))
                status = WAYLAND_LAYERS_ROOT;
        else if (count >= 0)
                status = wayland.host->row_layers(wayland.host->context, root->row, list, count, 0);
        else if (root->layered)
                wayland.host->row_layers(wayland.host->context, root->row, 0, -1, 0);
        if (status == WAYLAND_LAYERS_ROOT)
        {
                struct wayland_picture picture;
                status = 0;
                if (shown && wayland.host->row_layers(wayland.host->context, root->row, list, count, 1) == WAYLAND_LAYERS_KEPT)
                {
                        struct wayland_buffer *b = &c->buffers[shown - 1];
                        wayland_picture_init(&picture, &root->cur, b, &c->pools[b->pool], root->width, root->height,
                                             -window.x, -window.y, window.width, window.height);
                        picture.transparent = !wayland_surface_is_opaque(root, b);
                        wayland_picture_opaque(&picture, root, &root->cur, root->scale120 > 0 ? root->scale120 : WAYLAND_UNIT,
                                               window.x, window.y, window.width, window.height);
                        picture.fragile = root->xsurface != 0 && !c->pools[b->pool].dmabuf, picture.last = 1;
                        wayland_picture_submit(c, root, b, &picture, 1, 0);
                        status = WAYLAND_LAYERS_KEPT;
                }
        }
        wayland_layers_lent(c, root, status == WAYLAND_LAYERS_KEPT ? list : 0, count);
        if (status == WAYLAND_LAYERS_KEPT)
                root->window_x = window.x, root->window_y = window.y, root->window_w = window.width, root->window_h = window.height;
        return status == WAYLAND_LAYERS_KEPT;
}

static void wayland_relay(struct wayland_client *c, struct wayland_surface *root)
{
        if (root->row < 0 || !root->need_full || !(root->flags & WAYLAND_SURFACE_MAPPED))
                return;
        root->need_full = 0;
        if (wayland_layers(c, root, 0, 0))
                return;
        struct wayland_rect w = wayland_window_of(c, root);
        root->window_x = w.x, root->window_y = w.y, root->window_w = w.width, root->window_h = w.height;
        wayland_flatten(c, ~wayland_surface_index(c, root), 0, &w), wayland_row_take(c, root);
}

static inline int wayland_viewport_used(const struct wayland_surf_state *st) { return st->dst_w > 0 || st->src_w > 0; }

static inline int wayland_state_xform_changed(const struct wayland_surf_state *a, const struct wayland_surf_state *b)
{
        return a->transform != b->transform || a->src_x != b->src_x || a->src_y != b->src_y ||
               a->src_w != b->src_w || a->src_h != b->src_h || a->dst_w != b->dst_w || a->dst_h != b->dst_h;
}

static void wayland_state_apply(struct wayland_surface *s, long tw, long th, long scale120)
{
        s->width = tw, s->height = th, s->scale120 = scale120;
        s->cur.scale = s->pending.scale, s->cur.transform = s->pending.transform;
        s->cur.src_x = s->pending.src_x, s->cur.src_y = s->pending.src_y, s->cur.src_w = s->pending.src_w, s->cur.src_h = s->pending.src_h;
        s->cur.dst_w = s->pending.dst_w, s->cur.dst_h = s->pending.dst_h;
}

static inline long wayland_commit_scale(const struct wayland_surface *s)
{
        return s->pending.dst_w > 0 ? (s->told120 > 0 ? s->told120 : WAYLAND_UNIT) : WAYLAND_UNIT * s->pending.scale;
}

static long wayland_viewport_error(const struct wayland_surf_state *st, const struct wayland_buffer *b)
{
        long bw_t, bh_t;

        wayland_transformed_size(b->width, b->height, st->transform, &bw_t, &bh_t);
        if (st->src_w > 0 && ((st->src_x + st->src_w) * st->scale > bw_t * 256 || (st->src_y + st->src_h) * st->scale > bh_t * 256))
                return WLN_wp_viewport_error_out_of_buffer;
        return st->src_w > 0 && st->dst_w <= 0 && ((st->src_w & 255) || (st->src_h & 255)) ? WLN_wp_viewport_error_bad_size : 0;
}

static void wayland_sub_apply(struct wayland_client *c, struct wayland_surface *s)
{
        struct wayland_surface *root = wayland_root_of(c, s);
        long buffer = -1;

        if (root->row < 0 || !(root->flags & WAYLAND_SURFACE_MAPPED))
        {
                if (root->row < 0)
                        wayland_callbacks_take(c, s), wayland_callbacks_done(c, s, wayland_now());
                s->u.sub.dirty = 1;
                return;
        }
        s->u.sub.dirty = 0;
        if (s->cached.opaque_set)
                s->cur.opaque = s->cached.opaque, s->cached.opaque_set = 0;
        if (s->u.sub.pend_pos)
                s->u.sub.x = s->u.sub.pend_x, s->u.sub.y = s->u.sub.pend_y, s->u.sub.pend_pos = 0, root->need_full = 1;
        if (s->pending.attach_made && s->pending.attached && (buffer = wayland_find(c, s->pending.attached, WLI_wl_buffer)) >= 0)
        {
                struct wayland_buffer *b = &c->buffers[c->objects[buffer].link];
                if ((!wayland_viewport_used(&s->pending) && (b->width % s->pending.scale || b->height % s->pending.scale)) ||
                    wayland_viewport_error(&s->pending, b))
                        buffer = -1;
        }
        if (buffer >= 0)
        {
                struct wayland_buffer *b = &c->buffers[c->objects[buffer].link];
                long ox, oy, tw, th, scale120 = wayland_commit_scale(s);
                int full, is_opaque;
                struct wayland_rect window;

                wayland_surface_target_pixels(s, &s->pending, b, &tw, &th);
                full = !s->held || s->width != tw || s->height != th || s->pending.damage_full || !s->pending.damages ||
                       scale120 != s->scale120 || wayland_state_xform_changed(&s->cur, &s->pending);
                wayland_unhold(c, s), b->held++, s->held = (unsigned int)(c->objects[buffer].link + 1);
                wayland_state_apply(s, tw, th, scale120);
                is_opaque = wayland_surface_is_opaque(s, b);
                wayland_sub_offset(c, s, &ox, &oy), window = wayland_window_of(c, root);
                if (wayland_layers(c, root, s, full))
                        root->need_full = 0;
                else if (root->need_full)
                        wayland_relay(c, root);
                else if (!is_opaque || s->u.sub.translucent)
                {
                        struct wayland_rect rect = {ox, oy, tw, th};
                        if (s->u.sub.translucent)
                                rect = wayland_rect_union(rect, (struct wayland_rect){s->u.sub.translucent_x, s->u.sub.translucent_y,
                                                                                      s->u.sub.translucent_w, s->u.sub.translucent_h});
                        if (root->held || root->lent) wayland_child_copy(c, root, &rect, &window);
                        wayland_flatten(c, wayland_surface_index(c, root), &rect, &window);
                }
                else if (full)
                        wayland_child_copy(c, s, 0, &window), wayland_flatten(c, wayland_surface_index(c, s), 0, &window);
                else
                        for (long at = 0; at < s->pending.damages; at++)
                        {
                                struct wayland_rect r = s->pending.damage_kind[at] ? s->pending.damage[at] : wayland_cover(s->pending.damage[at], s->scale120);
                                r.x += ox, r.y += oy;
                                wayland_child_copy(c, s, &r, &window), wayland_flatten(c, wayland_surface_index(c, s), &r, &window);
                        }
        }
        else if (s->pending.attach_made && !s->pending.attached)
                wayland_unhold(c, s), s->width = s->height = 0, root->need_full = 1;
        else if (s->pending.attach_made)
        {
                long gone = wayland_find(c, s->pending.attached, WLI_wl_buffer);
                if (gone >= 0) wayland_release_buffer(c, gone);
        }
        if (root->need_full) wayland_relay(c, root);
        if (s->held)
        {
                struct wayland_buffer *shown = &c->buffers[s->held - 1];
                long px, py, tw, th;
                wayland_sub_offset(c, s, &px, &py), wayland_surface_target_pixels(s, &s->cur, shown, &tw, &th);
                s->u.sub.translucent = !wayland_surface_is_opaque(s, shown);
                s->u.sub.translucent_x = px, s->u.sub.translucent_y = py, s->u.sub.translucent_w = tw, s->u.sub.translucent_h = th;
        }
        else
                s->u.sub.translucent = 0;
        s->pending.attach_made = s->pending.attached = s->cached.attached = s->pending.damages = s->pending.damage_full = 0;
        s->cur.transform = s->pending.transform, s->cur.alpha = s->pending.alpha, s->cur.async_hint = s->pending.async_hint;
        wayland_callbacks_take(c, s), wayland_feedback_take(c, s, 1);
}

static void wayland_sub_apply_children(struct wayland_client *c, long parent)
{
        struct wayland_surface *p = &c->surfaces[parent];

        for (int side = 0; side < 2; side++)
                for (unsigned int ch = p->first_child[side]; ch; ch = c->surfaces[ch - 1].next_sib)
                {
                        struct wayland_surface *s = &c->surfaces[ch - 1];
                        if (s->u.sub.dirty || s->u.sub.pend_pos) wayland_sub_apply(c, s);
                        wayland_sub_apply_children(c, (long)(ch - 1));
                }
}

static int wayland_sub_commit(struct wayland_client *c, struct wayland_object *self, struct wayland_surface *s)
{
        struct wayland_surface *parent = s->sub_parent ? &c->surfaces[s->sub_parent - 1] : 0;

        if (s->pending.attach_made && s->pending.attached && wayland_find(c, s->pending.attached, WLI_wl_buffer) < 0)
                return wayland_fail(c, self->id, WLN_wl_display_error_invalid_object, "the attached buffer is gone");
        if (!parent || !parent->live)
        {
                long gone = s->pending.attach_made && s->pending.attached ? wayland_find(c, s->pending.attached, WLI_wl_buffer) : -1;
                if (gone >= 0) wayland_release_buffer(c, gone);
                s->pending.attach_made = s->pending.attached = s->pending.damages = 0;
                wayland_callbacks_take(c, s), wayland_feedback_take(c, s, 0), wayland_callbacks_done(c, s, wayland_now());
                return WAYLAND_OK;
        }
        if (wayland_sub_synced(c, s))
        {
                if (s->pending.attach_made && s->cached.attached && s->cached.attached != s->pending.attached)
                {
                        long old = wayland_find(c, s->cached.attached, WLI_wl_buffer);
                        if (old >= 0) wayland_release_buffer(c, old);
                }
                if (s->pending.attach_made) s->cached.attached = s->pending.attached;
                return s->u.sub.dirty = 1, WAYLAND_OK;
        }
        wayland_sub_apply(c, s), wayland_sub_apply_children(c, wayland_surface_index(c, s)), wayland_row_take(c, wayland_root_of(c, s));
        return WAYLAND_OK;
}

static void wayland_sub_drop(struct wayland_client *c, struct wayland_surface *s)
{
        struct wayland_surface *root = s->role == WAYLAND_ROLE_SUBSURFACE && s->sub_parent ? wayland_root_of(c, s) : 0;

        if (root && root != s)
                wayland_lift_drop(c, root, s);
        wayland_unhold(c, s), wayland_sub_unlink(c, s), s->sub_parent = 0;
        for (int side = 0; side < 2; side++)
                for (unsigned int ch = s->first_child[side]; ch; ch = c->surfaces[ch - 1].next_sib)
                        wayland_unhold(c, &c->surfaces[ch - 1]);
        if (root && root != s && root->live)
                root->need_full = 1, wayland_relay(c, root);
        for (struct wayland_surface *o = s; o; o = wayland_tree_next(c, s, o))
                if ((o != s || s->row < 0) && (o->lent || o->lent_gone || o->lent_latched || o->lent_shown))
                        wayland_lend_leave(c, o);
        for (int side = 0; side < 2; side++)
        {
                for (unsigned int ch = s->first_child[side], next; ch; ch = next)
                {
                        struct wayland_surface *cs = &c->surfaces[ch - 1];
                        next = cs->next_sib, cs->sub_parent = cs->prev_sib = cs->next_sib = cs->sub_above = 0;
                }
                s->first_child[side] = s->last_child[side] = 0;
        }
}

static void wayland_constraints_drop(struct wayland_client *c, long surface);
static void wayland_stand_end(struct wayland_client *c, struct wayland_surface *s, int restore);
static void wayland_drag_leave(void);
static void wayland_drag_end(int how);

static void wayland_surface_drop(struct wayland_client *c, struct wayland_surface *s)
{
        long idx = wayland_surface_index(c, s);

        if (wayland.drag.live)
        {
                if (!wayland.drag.xsource && wayland.drag.client == c->slot && wayland.drag.origin == idx)
                        wayland_drag_end(0);
                else if (wayland.drag.target == c->slot && wayland.drag.surface == idx)
                        wayland_drag_leave();
        }
        wayland_constraints_drop(c, idx), wayland_sub_drop(c, s);
        if (s->role == WAYLAND_ROLE_STAND_IN)
                wayland_stand_end(c, s, 1);
        else
                wayland_row_release(c, s);
        for (int q = 0; q < 4; q++)
        {
                if (q == WLCB_READY && s->cbq[q].count) c->waiting--, wayland.waiting--;
                if (q == WLCB_PRESENTING && s->cbq[q].count) c->presenting_surfaces--;
                while (s->cbq[q].count)
                {
                        unsigned int id = wayland_cb_pop(c, &s->cbq[q]);
                        long slot = q <= WLCB_READY ? wayland_find(c, id, WLI_wl_callback) : -1;
                        if (slot >= 0) wayland_object_delete(c, slot);
                }
        }
        s->presentings_framed = 0;
        if (s->live && s->role == WAYLAND_ROLE_SUBSURFACE) c->subsurfaces--;
        if (wayland.pointer_row >= 0 && wayland.row[wayland.pointer_row].client == c->slot && wayland.pointer_surface == idx)
                wayland.pointer_row = -1;
        if (wayland.cursor_client == c->slot && wayland.cursor_surface == idx)
                wayland.cursor_client = wayland.cursor_surface = -1, wayland.cursor_serial++;
        if (s->pending.sync_acq_set)
                wayland_timeline_put(c, s->pending.sync_acq_slot);
        if (s->pending.sync_rel_set)
                wayland_timeline_put(c, s->pending.sync_rel_slot);
        s->pending.sync_acq_set = s->pending.sync_rel_set = 0, s->pending.dx = s->pending.dy = 0;
        for (long at = 0; at < c->tbl[WLT_SURFACE].count; at++)
                if (c->surfaces[at].live && c->surfaces[at].parent == (unsigned int)(idx + 1))
                        c->surfaces[at].parent = 0;
        s->syncobj_id = 0, s->live = 0, s->gen++, wayland_tbl_free(c, WLT_SURFACE, idx), wayland.surfaces--;
}

/* ---- core requests, regions, show, and commit ---- */

enum
{
        WAYLAND_INVALID_OBJECT = WLN_wl_display_error_invalid_object,
        WAYLAND_INVALID_METHOD = WLN_wl_display_error_invalid_method,
        WAYLAND_NO_MEMORY = WLN_wl_display_error_no_memory,
        WAYLAND_IMPLEMENTATION = WLN_wl_display_error_implementation
};

static int wayland_new_fail(struct wayland_client *c, unsigned long self, unsigned long id)
{
        if (id < 1 || id >= WAYLAND_CLIENT_IDS_MAX || (long)id > (c->tbl[WLT_INDEX].count > 1 ? c->tbl[WLT_INDEX].count : 1) + WAYLAND_ID_GAP)
                return wayland_fail(c, self, WAYLAND_INVALID_OBJECT, "the new id is out of range");
        return wayland_slot_of(c, id) >= 0 ? wayland_fail(c, self, WAYLAND_INVALID_OBJECT, "the new id is in use")
                                           : wayland_fail(c, 1, WAYLAND_NO_MEMORY, "no memory for the object");
}

static int wayland_display_request(struct wayland_client *c, struct wayland_object *self, long op, struct wayland_arg *a)
{
        (void)self;
        if (op == WLQ_wl_display_sync)
                return wayland_ev(c, c->objects[a[0].slot].id, WLE_wl_callback_done, (unsigned int)wayland_serial(c)),
                       wayland_object_delete(c, a[0].slot), WAYLAND_OK;
        for (long name = 1; name <= WAYLAND_GLOBAL_COUNT; name++)
                if (wayland_global_offered(c, name))
                        wayland_global_send(c, c->objects[a[0].slot].id, name);
        for (long screen = 0; screen < wayland.screens; screen++)
                wayland_global_send(c, c->objects[a[0].slot].id, wayland_output_name(screen));
        return WAYLAND_OK;
}

WAYLAND_API void wayland_xwayland_client(long slot);

static int wayland_registry_request(struct wayland_client *c, struct wayland_object *self, long op, struct wayland_arg *a)
{
        long name = (long)a[0].u, iface, version, slot;
        unsigned long id = a[1].u;

        (void)op;
        if (name >= 1 && name <= WAYLAND_GLOBAL_COUNT && wayland_global_offered(c, name))
                iface = wayland_globals[name - 1].iface, version = wayland_globals[name - 1].version;
        else if (wayland_output_of(name) >= 0)
                iface = WLI_wl_output, version = 4;
        else
                return wayland_fail(c, self->id, WAYLAND_INVALID_OBJECT, "no such global");
        if (!wayland_name_is(a[1].text, a[1].length, wl_interfaces[iface].name))
                return wayland_fail(c, self->id, WAYLAND_INVALID_OBJECT, "the global is another interface");
        if (a[1].version < 1 || a[1].version > version)
                return wayland_fail(c, self->id, WAYLAND_INVALID_OBJECT, "no such version");
        if ((slot = id >= 1 && id < WAYLAND_IDS ? wayland_object_new(c, id, iface, a[1].version) : -1) < 0)
                return wayland_new_fail(c, self->id, id);
        if (iface == WLI_wl_output)
                c->objects[slot].aux = (unsigned short)wayland_output_of(name), wayland_output_send(c, slot);
        else if (iface == WLI_wl_shm)
        {
                static const unsigned int fmts[4] = {WLN_wl_shm_format_argb8888, WLN_wl_shm_format_xrgb8888,
                                                     WLN_wl_shm_format_abgr8888, WLN_wl_shm_format_xbgr8888};
                for (long at = 0; at < 4; at++)
                        wayland_ev(c, id, WLE_wl_shm_format, fmts[at]);
        }
        else if (iface == WLI_zwp_linux_dmabuf_v1 && a[1].version < 4)
                wayland_dmabuf_formats(c, id, a[1].version, 0);
        else if (iface == WLI_wl_drm)
        {
                if (wayland.drm_node[0]) wayland_ev_str(c, id, WLE_wl_drm_device, wayland.drm_node);
                if (a[1].version >= 2) wayland_ev(c, id, WLE_wl_drm_capabilities, WLN_wl_drm_capability_prime);
                wayland_dmabuf_formats(c, id, 1, 1);
        }
        else if (iface == WLI_xdg_toplevel_icon_manager_v1)
                wayland_ev0(c, id, WLE_xdg_toplevel_icon_manager_v1_done);
        else if (iface == WLI_wp_presentation)
                wayland_ev(c, id, WLE_wp_presentation_clock_id, 1u);
        else if (iface == WLI_wl_seat)
        {
                wayland_ev(c, id, WLE_wl_seat_capabilities, (unsigned int)wayland.host->seat(wayland.host->context));
                if (a[1].version >= 2) wayland_ev_str(c, id, WLE_wl_seat_name, "seat0");
        }
        return WAYLAND_OK;
}

static void wayland_scale_tell(struct wayland_client *c, struct wayland_surface *s, long percent)
{
        if (c->xspace) percent = 100;
        long scale120 = wayland_percent120(percent), whole = wayland_whole_scale(percent), slot;

        if (s->told120 == scale120 && s->told_whole == whole)
                return;
        if (s->fractional_id && s->told120 != scale120)
                wayland_ev(c, s->fractional_id, WLE_wp_fractional_scale_v1_preferred_scale, (unsigned int)scale120);
        if ((slot = wayland_find(c, s->id, WLI_wl_surface)) >= 0 && c->objects[slot].version >= 6 && s->told_whole != whole)
                wayland_ev(c, s->id, WLE_wl_surface_preferred_buffer_scale, (unsigned int)whole);
        s->told120 = scale120, s->told_whole = whole;
}

static int wayland_compositor_request(struct wayland_client *c, struct wayland_object *self, long op, struct wayland_arg *a)
{
        long slot;
        struct wayland_surface *s;

        (void)self;
        if (op == WLQ_wl_compositor_create_region)
        {
                if ((slot = wayland_tbl_alloc(c, WLT_REGION)) < 0)
                        return wayland_fail(c, c->objects[a[0].slot].id, WAYLAND_NO_MEMORY, "no memory for the region");
                return c->regions[slot].live = 1, c->objects[a[0].slot].link = (int)slot, WAYLAND_OK;
        }
        if ((slot = wayland_tbl_alloc(c, WLT_SURFACE)) < 0)
                return wayland_fail(c, c->objects[a[0].slot].id, WAYLAND_NO_MEMORY, "too many surfaces");
        s = &c->surfaces[slot];
        s->live = 1, s->id = c->objects[a[0].slot].id, s->row = -1;
        s->cur.scale = s->pending.scale = 1, s->cur.alpha = s->pending.alpha = 0xffffffffu, s->scale120 = WAYLAND_UNIT;
        wayland_surface_link(c, a[0].slot, s), wayland.surfaces++;
        wayland_scale_tell(c, s, wayland.screens ? wayland.screen[0].scale : 100);
        if (c->objects[a[0].slot].version >= 6)
                wayland_ev(c, s->id, WLE_wl_surface_preferred_buffer_transform, WLN_wl_output_transform_normal);
        return WAYLAND_OK;
}

static void wayland_region_add_exact(struct wayland_region *r, struct wayland_rect add)
{
        struct wayland_rect a = r->rect, joined = a;
        long ax2 = a.x + a.width, ay2 = a.y + a.height, nx2 = add.x + add.width, ny2 = add.y + add.height;

        if (!r->exact || wayland_rect_contains(add, a))
        {
                r->exact = 1, r->rect = add;
                return;
        }
        if (wayland_rect_contains(a, add))
                return;
        if (add.y <= a.y && ny2 >= ay2 && add.x <= ax2 && nx2 >= a.x)
                joined.x = wayland_min(a.x, add.x), joined.width = wayland_max(ax2, nx2) - joined.x;
        else if (add.x <= a.x && nx2 >= ax2 && add.y <= ay2 && ny2 >= a.y)
                joined.y = wayland_min(a.y, add.y), joined.height = wayland_max(ay2, ny2) - joined.y;
        r->rect = add.width * add.height > joined.width * joined.height ? add : joined;
}

static void wayland_region_sub_overflow(struct wayland_region *r, struct wayland_rect sub)
{
        struct wayland_rect p[4];
        long n = wayland_rect_sub(r->bounds, sub, p);

        if (n == 0) r->count = r->overflow = 0, r->bounds = (struct wayland_rect){0, 0, 0, 0};
        else if (n == 1) r->bounds = p[0];
}

static int wayland_region_request(struct wayland_client *c, struct wayland_object *self, long op, struct wayland_arg *a)
{
        struct wayland_region *r = self->link >= 0 && self->link < c->tbl[WLT_REGION].count ? &c->regions[self->link] : 0;

        if (op == WLQ_wl_region_destroy)
                return wayland_region_free(c, self->link), self->link = -1, WAYLAND_OK;
        if (a[2].i <= 0 || a[3].i <= 0)
                return WAYLAND_OK;
        struct wayland_rect arg = {a[0].i, a[1].i, a[2].i, a[3].i};
        if (op == WLQ_wl_region_add)
        {
                self->flags |= 1;
                if (!r) return WAYLAND_OK;
                wayland_region_add_exact(r, arg);
                r->bounds = !r->count && !r->overflow ? arg : wayland_rect_union(r->bounds, arg);
                if (r->overflow) return WAYLAND_OK;
                for (long i = 0; i < r->count;)
                {
                        struct wayland_rect b = r->rects[i];
                        if (wayland_rect_contains(b, arg)) return WAYLAND_OK;
                        if (wayland_rect_contains(arg, b)) r->rects[i] = r->rects[--r->count];
                        else if ((b.y == arg.y && b.height == arg.height && arg.x <= b.x + b.width && arg.x + arg.width >= b.x) ||
                                 (b.x == arg.x && b.width == arg.width && arg.y <= b.y + b.height && arg.y + arg.height >= b.y))
                                arg = wayland_rect_union(b, arg), r->rects[i] = r->rects[--r->count];
                        else i++;
                }
                if (r->count < WAYLAND_REGION_RECTS) r->rects[r->count++] = arg;
                else r->overflow = 1;
        }
        else if (op == WLQ_wl_region_subtract && r && (r->count || r->overflow))
        {
                struct wayland_rect out[WAYLAND_REGION_RECTS], p[4];
                long out_count = 0;
                int over = 0;

                r->exact = 0, r->rect.width = r->rect.height = 0;
                if (r->overflow)
                        wayland_region_sub_overflow(r, arg);
                else
                {
                        for (long i = 0; i < r->count; i++)
                                for (long k = 0, n = wayland_rect_sub(r->rects[i], arg, p); k < n; k++)
                                        if (out_count < WAYLAND_REGION_RECTS) out[out_count++] = p[k];
                                        else over = 1;
                        if (over)
                                r->bounds = wayland_rect_bounds(r->rects, r->count), r->count = 0, r->overflow = 1, wayland_region_sub_overflow(r, arg);
                        else
                        {
                                r->count = (unsigned char)out_count;
                                for (long i = 0; i < out_count; i++) r->rects[i] = out[i];
                                r->bounds = out_count ? wayland_rect_bounds(out, out_count) : (struct wayland_rect){0, 0, 0, 0};
                                if (out_count == 1) r->exact = 1, r->rect = out[0];
                        }
                }
                if (!r->count && !r->overflow) self->flags &= ~1u;
        }
        return WAYLAND_OK;
}

static void wayland_damage_add(struct wayland_surface *s, struct wayland_arg *a, int buffer)
{
        struct wayland_rect r = {a[0].i, a[1].i, a[2].i, a[3].i};

        if (r.width <= 0 || r.height <= 0)
                return;
        r.x = wayland_clamp(r.x, -(1L << 30), 1L << 30), r.y = wayland_clamp(r.y, -(1L << 30), 1L << 30);
        r.width = wayland_min(r.width, 1L << 30), r.height = wayland_min(r.height, 1L << 30);
        if (s->pending.damages < WAYLAND_DAMAGE)
                s->pending.damage[s->pending.damages] = r, s->pending.damage_kind[s->pending.damages++] = (unsigned char)buffer;
        else if (s->pending.damage_kind[s->pending.damages - 1] != buffer)
                s->pending.damage_full = 1;
        else
                s->pending.damage[s->pending.damages - 1] = wayland_rect_union(s->pending.damage[s->pending.damages - 1], r);
}

static void wayland_pool_drop(struct wayland_client *c, long pool)
{
        struct wayland_pool *p = &c->pools[pool];

        if (!p->unmapped && p->at)
                wayland.host->unmap(wayland.host->context, p->at, p->size);
        if (p->dmabuf)
        {
                wayland_fd_close(p->fd);
                for (long k = 0; k < 3; k++)
                        if (p->extra_fd[k] >= 0)
                                wayland_fd_close(p->extra_fd[k]), p->extra_fd[k] = -1;
                p->dmabuf = p->unmapped = 0, c->dmabufs--, wayland.dmabufs--;
        }
        p->live = 0, wayland_tbl_free(c, WLT_POOL, pool), wayland.pools--;
}

static void wayland_pool_unref(struct wayland_client *c, long pool)
{
        if (c->pools[pool].live && c->pools[pool].refs <= 0 && c->pools[pool].gone)
                wayland_pool_drop(c, pool);
}

static long wayland_screen_at(long x, long y)
{
        for (long at = 0; at < wayland.screens; at++)
                if (x >= wayland.screen[at].x && x < wayland.screen[at].x + wayland.screen[at].width &&
                    y >= wayland.screen[at].y && y < wayland.screen[at].y + wayland.screen[at].height)
                        return at;
        return 0;
}

static void wayland_surface_screens(struct wayland_client *c, struct wayland_surface *s, long screen)
{
        unsigned int now = screen >= 0 && screen < WAYLAND_SCREENS ? 1u << screen : 0;

        if (now == s->entered)
                return;
        for (long at = 0; at < c->tbl[WLT_OBJECT].count; at++)
        {
                struct wayland_object *o = &c->objects[at];
                if (o->iface != WLI_wl_output || o->aux >= WAYLAND_SCREENS) continue;
                if ((s->entered ^ now) & (1u << o->aux))
                        wayland_ev(c, s->id, (now & (1u << o->aux)) ? WLE_wl_surface_enter : WLE_wl_surface_leave, o->id);
        }
        s->entered = now;
}

static long wayland_xparent(struct wayland_client *c, const struct wayland_surface *s, long *px, long *py)
{
        long best_row = -1, best_x = 0, best_y = 0, best_score = 0;

        for (long at = 0; at < c->tbl[WLT_SURFACE].count; at++)
        {
                const struct wayland_surface *o = &c->surfaces[at];
                struct wayland_facts facts = {0};
                long score;

                if (o == s || !o->live || !o->xsurface || !o->u.xwin.adopted || o->u.xwin.popup || o->row < 0 || wayland_row_facts(o->row, &facts) < 0)
                        continue;
                score = o->row == wayland.pointer_row ? 4
                        : (o->row == wayland.keyboard_row || (facts.states & WAYLAND_STATE_ACTIVATED)) ? 3
                        : (facts.width > 0 && facts.height > 0 && s->u.xwin.x >= facts.x && s->u.xwin.x < facts.x + facts.width &&
                           s->u.xwin.y >= facts.y && s->u.xwin.y < facts.y + facts.height) ? 2 : 1;
                if (score > best_score)
                        best_score = score, best_row = o->row, best_x = facts.x, best_y = facts.y;
        }
        if (best_row >= 0)
                *px = s->u.xwin.x - best_x, *py = s->u.xwin.y - best_y;
        return best_row;
}

/*
        A stand-in (canvas_swapchain_v1): a surface whose pictures a window shows in place of its own, the swapchain a game's Vulkan layer
        made for the window (src/shim/wsi.c). The window stays the window: its own surface keeps the input, the focus, the size, the
        title and the X window's place, and while it is stood in for its commits put nothing on the screen (their buffers go back at once,
        their callbacks and feedback are answered with the frames). The stand-in's commits are the row's pictures at the window's size, a
        smaller buffer scaled up to it (a game drawing below the screen's size: the display plane's scaler, compositor_wl_view_take). It
        takes its row at the first commit the window is shown at and the client may show it (wayland_stand_row), says so with an answer
        (and says again whenever that changes), and lets the row go when it goes, the window goes, or another stand-in takes the row.
*/
static void wayland_stand_answer(struct wayland_client *c, struct wayland_surface *s, long answer, long width, long height)
{
        s->u.stand.said = (unsigned char)answer;
        if (s->u.stand.manager && wayland_find(c, s->u.stand.manager, WLI_canvas_swapchain_v1) >= 0)
                wayland_ev(c, s->u.stand.manager, WLE_canvas_swapchain_v1_answer, s->id, (unsigned int)answer, (unsigned int)width,
                           (unsigned int)height);
}

static void wayland_stand_end(struct wayland_client *c, struct wayland_surface *s, int restore)
{
        long row = s->row;
        struct wayland_client *rc;
        struct wayland_surface *root;

        if (s->role != WAYLAND_ROLE_STAND_IN || row < 0)
                return;
        if (row < wayland.row_made && wayland.row[row].stand_client == c->slot && wayland.row[row].stand_surface == wayland_surface_index(c, s))
                wayland.row[row].stand_client = wayland.row[row].stand_surface = -1;
        s->row = -1, s->flags &= ~(unsigned)WAYLAND_SURFACE_MAPPED;
        wayland_lend_leave(c, s), wayland_presenting_drop(c, s);
        if (s->cbq[WLCB_READY].count)
                wayland_callbacks_done(c, s, wayland_now());
        if (!restore || !(root = wayland_row_surface(row, &rc)) || root->row != row || !(root->flags & WAYLAND_SURFACE_MAPPED))
                return;
        root->need_full = 1;
        if (wayland_has_children(root))
                wayland_relay(rc, root);
        else if (root->lent || root->held)
        {
                struct wayland_rect window = wayland_window_of(rc, root);
                wayland_child_copy(rc, root, 0, &window), wayland_row_take(rc, root);
        }
}

// The row a stand-in lost: its row's own surface is gone or no longer shown.
static void wayland_stand_lose(long row)
{
        struct wayland_client *sc;
        struct wayland_surface *st;

        if (row >= 0 && row < wayland.row_made && wayland.row[row].lifted)
        {
                wayland_lift_end(row);
                return;
        }
        if ((st = wayland_row_stand(row, &sc)))
                wayland_stand_end(sc, st, 0);
        if (row >= 0 && row < wayland.row_made)
                wayland.row[row].stand_client = wayland.row[row].stand_surface = -1;
}

/*
        A target of the stand-in's own client: its window's row when the target is all of that window's picture (a subsurface over the
        whole window, as winewayland's client area of a fullscreen window is); -PARTIAL when it is a part of the window (the window's
        own picture keeps being composed with it), -OWN when it is the window's own surface (shown straight as it is), -WAITING while
        the window is not shown, -UNKNOWN once it is gone. The window's size goes to width and height.
*/
static long wayland_stand_target(struct wayland_client *c, struct wayland_surface *s, long *width, long *height)
{
        struct wayland_surface *t = s->u.stand.target && s->u.stand.target <= (unsigned int)c->tbl[WLT_SURFACE].count ? &c->surfaces[s->u.stand.target - 1] : 0;
        struct wayland_surface *root;
        struct wayland_rect window;
        long ox, oy, tw, th, scale;

        if (!t || !t->live || t->gen != s->u.stand.target_gen || t == s || t->role == WAYLAND_ROLE_STAND_IN)
                return -WAYLAND_STAND_UNKNOWN;
        root = wayland_root_of(c, t);
        if (root == t && t->role != WAYLAND_ROLE_SUBSURFACE)
                return -WAYLAND_STAND_OWN;
        if (root->row < 0 || !(root->flags & WAYLAND_SURFACE_MAPPED))
                return -WAYLAND_STAND_WAITING;
        window = wayland_window_of(c, root), *width = window.width, *height = window.height;
        wayland_sub_offset(c, t, &ox, &oy);
        scale = root->scale120 > 0 ? root->scale120 : WAYLAND_UNIT;
        tw = t->width > 0 ? t->width : t->cur.dst_w > 0 ? wayland_pixels(t->cur.dst_w, scale) : 0;
        th = t->height > 0 ? t->height : t->cur.dst_h > 0 ? wayland_pixels(t->cur.dst_h, scale) : 0;
        if (ox > window.x || oy > window.y || ox + tw < window.x + window.width || oy + th < window.y + window.height)
                return -WAYLAND_STAND_PARTIAL;
        return root->row;
}

// The row the stand-in's pictures go to now, taken here when it can be (another stand-in of the row lets it go), or -1; the client
// is told when the answer is not the one it had.
static long wayland_stand_row(struct wayland_client *c, struct wayland_surface *s)
{
        long row = -WAYLAND_STAND_UNKNOWN, width = 0, height = 0, answer;
        struct wayland_client *oc, *rc;
        struct wayland_surface *other, *root;

        if (s->row >= 0 || s->u.stand.displaced)
                return s->row >= 0 ? s->row : -1;
        if (s->u.stand.window)
                row = wayland.host->stand_in ? wayland.host->stand_in(wayland.host->context, c->slot, s->u.stand.window) : -WAYLAND_STAND_UNKNOWN;
        else if (s->u.stand.target)
                row = wayland_stand_target(c, s, &width, &height);
        if (row >= 0 && (row >= wayland.row_made || !(root = wayland_row_surface(row, &rc))))
                row = -WAYLAND_STAND_WAITING;
        if (row >= 0)
        {
                struct wayland_rect window = wayland_window_of(rc, root);
                if (wayland.row[row].lifted) // a stand-in has the row before a lift
                        wayland_lift_end(row);
                if ((other = wayland_row_stand(row, &oc)) && other != s)
                        other->u.stand.displaced = 1, wayland_stand_end(oc, other, 0);
                wayland.row[row].stand_client = c->slot, wayland.row[row].stand_surface = wayland_surface_index(c, s);
                s->row = (int)row, s->need_full = 1, s->window_w = s->window_h = 0, width = window.width, height = window.height;
        }
        answer = row >= 0 ? WAYLAND_STAND_SHOWN : -row;
        if (answer != s->u.stand.said)
                wayland_stand_answer(c, s, answer, width, height);
        return row >= 0 ? row : -1;
}

// A stand-in's commit as its row's picture: the window's size, the buffer scaled to it when it is not.
static int wayland_stand_show(struct wayland_client *c, struct wayland_object *self, struct wayland_surface *s, struct wayland_buffer *b, long scale120)
{
        struct wayland_pool *p = &c->pools[b->pool];
        struct wayland_picture picture;
        struct wayland_client *rc;
        struct wayland_surface *root = wayland_row_surface(s->row, &rc);
        long tw, th;
        int lent = 0;

        wayland_surface_target_pixels(s, &s->pending, b, &tw, &th);
        if (root)
        {
                struct wayland_rect window = wayland_window_of(rc, root);
                if (window.width > 0 && window.height > 0)
                        tw = window.width, th = window.height;
        }
        wayland_picture_init(&picture, &s->pending, b, p, tw, th, 0, 0, tw, th);
        picture.transparent = !wayland_surface_is_opaque_at(s, &s->pending, b), picture.last = 1;
        wayland_picture_opaque(&picture, s, &s->pending, scale120, 0, 0, tw, th);
        picture.full = s->need_full || s->pending.damage_full || !s->pending.damages || tw != s->window_w || th != s->window_h ||
                       wayland_state_xform_changed(&s->cur, &s->pending) || picture.src_width > 0 || picture.dst_width > 0 ||
                       picture.transform != 0 || picture.y_invert != 0;
        s->window_x = s->window_y = 0, s->window_w = tw, s->window_h = th, s->need_full = 0;
        if (!picture.full)
                wayland_picture_clip_damage(&picture, &s->pending, scale120, b->width, b->height);
        if (wayland_picture_submit(c, s, b, &picture, 0, &lent) < 0)
                return wayland_fail(c, self->id, WAYLAND_IMPLEMENTATION, "the window refused the picture");
        s->flags |= WAYLAND_SURFACE_MAPPED, s->shown_full = (unsigned char)picture.full;
        return WAYLAND_OK;
}

static int wayland_show(struct wayland_client *c, struct wayland_object *self, struct wayland_surface *s, struct wayland_buffer *b, long scale120)
{
        struct wayland_pool *p = &c->pools[b->pool];
        struct wayland_picture picture;
        int first = s->row < 0, lent = 0;
        long wx, wy, ww, wh, tw, th, status;

        wayland_surface_target_pixels(s, &s->pending, b, &tw, &th);
        if (first)
        {
                int parent = -1, popup = s->role == WAYLAND_ROLE_POPUP, placed;
                long open_x = popup ? s->u.popup.x : 0, open_y = popup ? s->u.popup.y : 0;
                struct wayland_facts facts = {0};

                if (popup)
                {
                        struct wayland_surface *above = s->parent ? &c->surfaces[s->parent - 1] : 0;
                        if (!above || !above->live || above->row < 0) return WAYLAND_OK;
                        parent = above->row;
                }
                else if (s->xsurface && s->u.xwin.popup)
                {
                        long px = 0, py = 0;
                        if ((parent = (int)wayland_xparent(c, s, &px, &py)) >= 0)
                                popup = 1, open_x = px, open_y = py;
                        s->u.xwin.alone = parent < 0;
                }
                else
                {
                        struct wayland_surface *above = !s->xsurface && s->parent ? &c->surfaces[s->parent - 1] : 0;
                        parent = above && above->live && above->row >= 0 ? (int)above->row : s->xsurface && s->u.xwin.owner > 0 ? (int)s->u.xwin.owner - 1 : -1;
                }
                placed = !popup && s->xsurface && (s->u.xwin.alone || s->u.xwin.placed);
                s->row = wayland.host->row_open(wayland.host->context, c->slot,
                                                popup ? WAYLAND_KIND_POPUP : placed ? WAYLAND_KIND_PLACED : WAYLAND_KIND_TOPLEVEL,
                                                parent, placed ? s->u.xwin.x : open_x, placed ? s->u.xwin.y : open_y);
                if (s->row >= 0 && !wayland_rows_reach(s->row))
                        wayland.host->row_close(wayland.host->context, s->row), s->row = -1;
                if (s->row < 0)
                        return s->row = -1, WAYLAND_OK;
                wayland.row[s->row].client = c->slot, wayland.row[s->row].surface = wayland_surface_index(c, s), wayland.rows++;
                if (s->title[0] || s->app_id[0])
                        wayland.host->row_title(wayland.host->context, s->row, s->title[0] ? s->title : s->app_id, 0);
                if (s->app_id[0] && wayland.host->row_app)
                        wayland.host->row_app(wayland.host->context, s->row, s->app_id);
                if (wayland_row_facts(s->row, &facts) >= 0)
                        wayland_surface_screens(c, s, wayland_screen_at(facts.x, facts.y));
        }
        wayland_window_rect(c, s, tw, th, scale120, &wx, &wy, &ww, &wh);
        wayland_picture_init(&picture, &s->pending, b, p, tw, th, -wx, -wy, ww, wh);
        picture.transparent = !wayland_surface_is_opaque_at(s, &s->pending, b);
        wayland_picture_opaque(&picture, s, &s->pending, scale120, wx, wy, ww, wh);
        picture.fragile = s->xsurface != 0 && !p->dmabuf, picture.last = !wayland_has_children(s);
        picture.full = first || s->width != tw || s->height != th || s->pending.damage_full || scale120 != s->scale120 || !s->pending.damages ||
                       s->need_full || wx != s->window_x || wy != s->window_y || ww != s->window_w || wh != s->window_h ||
                       wayland_state_xform_changed(&s->cur, &s->pending) ||
                       picture.src_width > 0 || picture.dst_width > 0 || picture.transform != 0 || picture.y_invert != 0;
        s->window_x = wx, s->window_y = wy, s->window_w = ww, s->window_h = wh, s->need_full = 0;
        if (!picture.full)
                wayland_picture_clip_damage(&picture, &s->pending, scale120, b->width, b->height);
        if (!picture.last)
        {
                struct wayland_picture list[WAYLAND_LAYERS];
                struct wayland_rect window = {wx, wy, ww, wh}, box = {-wx, -wy, tw, th};
                long count = wayland.host->row_layers && scale120 == (s->scale120 > 0 ? s->scale120 : WAYLAND_UNIT)
                                 ? wayland_layers_make(c, s, 0, 0, &window, &box, !picture.transparent, list) : -1;
                if (count >= 0)
                        picture.last = wayland.host->row_layers(wayland.host->context, s->row, list, count, 1) == WAYLAND_LAYERS_KEPT;
                else if (s->layered)
                        wayland.host->row_layers(wayland.host->context, s->row, 0, -1, 0);
                wayland_layers_lent(c, s, picture.last ? list : 0, count), s->laying = !picture.last;
        }
        if (s->role == WAYLAND_ROLE_TOPLEVEL && s->u.top.told && !s->serials && s->row >= 0)
        {
                struct wayland_facts now;
                picture.own_size = wayland_row_facts(s->row, &now) >= 0 && now.width == s->u.top.told_width && now.height == s->u.top.told_height;
        }
        status = wayland_picture_submit(c, s, b, &picture, 0, &lent);
        if (!lent)
                s->budget_used += picture.full ? wayland_min(tw, ww) * wayland_min(th, wh) : 0;
        for (long at = 0; !lent && !picture.full && at < picture.damages; at++)
                s->budget_used += picture.damage[at].width * picture.damage[at].height;
        if (status < 0)
                return wayland_fail(c, self->id, WAYLAND_IMPLEMENTATION, "the window refused the picture");
        s->flags |= WAYLAND_SURFACE_MAPPED, s->shown_full = (unsigned char)picture.full;
        if (!picture.full)
                s->shown_rect = wayland_rect_bounds(picture.damage, picture.damages);
        if (first)
        {
                if (s->role == WAYLAND_ROLE_TOPLEVEL)
                {
                        if (s->u.top.layer_shell) wayland_row_do(s->row, WAYLAND_DO_LAYER, s->u.top.layer_level <= 1 ? -1 : 2, s->u.top.layer_output);
                        if (s->u.top.min_width || s->u.top.min_height) wayland_row_do(s->row, WAYLAND_DO_MIN_SIZE, s->u.top.min_width, s->u.top.min_height);
                        if (s->u.top.max_width || s->u.top.max_height) wayland_row_do(s->row, WAYLAND_DO_MAX_SIZE, s->u.top.max_width, s->u.top.max_height);
                        if (s->u.top.want_maximized) wayland_row_do(s->row, WAYLAND_DO_MAXIMIZE, 0, 0);
                        if (s->u.top.want_fullscreen) wayland_row_do(s->row, WAYLAND_DO_FULLSCREEN, s->u.top.want_screen, 0);
                }
                if (s->xsurface && s->u.xwin.alone)
                        wayland_row_do(s->row, WAYLAND_DO_PLACE, s->u.xwin.x, s->u.xwin.y);
        }
        return WAYLAND_OK;
}

static int wayland_timing_held(unsigned long stamp)
{
        if (!stamp) return 0;
        return wayland.target && wayland.target_period ? stamp > wayland.target + wayland.target_period / 8
                                                       : (unsigned long)wayland_now() * 1000000ul < stamp;
}

static long wayland_timing_wake(unsigned long stamp)
{
        if (wayland.target && wayland.target_period && stamp > wayland.target + wayland.target_period / 8)
        {
                unsigned long p = wayland.target_period, ahead = stamp - p / 8 - wayland.target;
                return (long)((wayland.target + (ahead + p - 1) / p * p - p) / 1000000ul);
        }
        return (long)(stamp / 1000000ul);
}

WAYLAND_API void wayland_target(unsigned long when, unsigned long period) { wayland.target = when, wayland.target_period = period; }

static int wayland_hold_wanted(struct wayland_client *c, struct wayland_surface *s)
{
        struct wayland_surface *root = wayland_root_of(c, s);

        if (s->xsurface && !s->u.xwin.adopted && !s->u.xwin.dead && s->row < 0 && s->pending.attach_made && s->pending.attached)
        {
                long now = wayland_now();
                if (!s->u.xwin.wait) s->u.xwin.wait = now + WAYLAND_XWAIT_MS;
                if (now < s->u.xwin.wait) return 1;
                s->u.xwin.adopted = 1;
        }
        if (!root || root->row < 0 || (s->role == WAYLAND_ROLE_SUBSURFACE && s->u.sub.sync))
                return 0;
        return (s->pending.fifo_wait && s->cur.barrier && s->barrier_frame == wayland.latches) || wayland_timing_held(s->pending.timestamp);
}

static void wayland_commit_take(struct wayland_client *c, struct wayland_surface *s)
{
        if (s->pending.barrier)
                s->cur.barrier = 1, s->barrier_frame = wayland.latches, s->pending.barrier = 0;
        if (s->pending.opaque_set)
        {
                if (s->role == WAYLAND_ROLE_SUBSURFACE && wayland_sub_synced(c, s))
                        s->cached.opaque = s->pending.opaque, s->cached.opaque_set = 1;
                else
                        s->cur.opaque = s->pending.opaque, s->cached.opaque_set = 0;
                s->pending.opaque_set = 0;
        }
        s->cur.alpha = s->pending.alpha, s->cur.icon = s->pending.icon, s->cur.async_hint = s->pending.async_hint;
        s->cur.fifo_wait = s->pending.fifo_wait, s->pending.fifo_wait = 0;
        s->cur.timestamp = s->pending.timestamp, s->pending.timestamp = 0;
}

static inline void wayland_stall_set(struct wayland_client *c, long fd, long sync_handle, unsigned long long sync_point, long budget_surface)
{
        c->stalled_fd = fd, c->stalled_sync_handle = sync_handle, c->stalled_sync_point = sync_point, c->budget_surface = budget_surface;
}

static int wayland_syncobj_check(struct wayland_client *c, struct wayland_surface *s, long buffer)
{
        long tl;

        if (!s->pending.sync_acq_set && !s->pending.sync_rel_set)
                return 0;
        if (buffer < 0)
                return wayland_fail(c, s->syncobj_id, WLN_wp_linux_drm_syncobj_surface_v1_error_no_buffer, "no buffer attached for syncobj"), -1;
        if (!c->pools[c->buffers[c->objects[buffer].link].pool].dmabuf)
                return wayland_fail(c, s->syncobj_id, WLN_wp_linux_drm_syncobj_surface_v1_error_unsupported_buffer, "syncobj requires a dma-buf"), -1;
        if (s->pending.sync_acq_set != s->pending.sync_rel_set)
                return wayland_fail(c, s->syncobj_id, s->pending.sync_acq_set ? WLN_wp_linux_drm_syncobj_surface_v1_error_no_release_point
                                                                              : WLN_wp_linux_drm_syncobj_surface_v1_error_no_acquire_point,
                                    s->pending.sync_acq_set ? "missing release point" : "missing acquire point"), -1;
        if (s->pending.sync_acq_slot == s->pending.sync_rel_slot && s->pending.sync_acq_point >= s->pending.sync_rel_point)
                return wayland_fail(c, s->syncobj_id, WLN_wp_linux_drm_syncobj_surface_v1_error_conflicting_points, "acquire and release points conflict"), -1;
        tl = s->pending.sync_acq_slot;
        if (tl >= 0 && tl < c->tbl[WLT_TIMELINE].count && c->timelines[tl].live && c->timelines[tl].handle >= 0 && wayland.syncobj.ready &&
            !wayland.syncobj.ready(wayland.syncobj.context, c->timelines[tl].handle, s->pending.sync_acq_point))
                return wayland_stall_set(c, -1, c->timelines[tl].handle, s->pending.sync_acq_point, -1), 1;
        return 0;
}

static void wayland_syncobj_clear_pending(struct wayland_client *c, struct wayland_surface *s)
{
        if (s->pending.sync_acq_set)
                wayland_timeline_put(c, s->pending.sync_acq_slot), s->pending.sync_acq_set = 0;
        if (s->pending.sync_rel_set)
                wayland_timeline_put(c, s->pending.sync_rel_slot), s->pending.sync_rel_set = 0;
}

static void wayland_syncobj_take(struct wayland_client *c, struct wayland_surface *s, struct wayland_buffer *b)
{
        long tl = s->pending.sync_rel_slot;

        if (!s->pending.sync_acq_set && !s->pending.sync_rel_set)
        {
                if (b) b->explicit_sync = 0;
                return;
        }
        if (b) b->explicit_sync = (unsigned char)(s->pending.sync_acq_set != 0);
        if (s->pending.sync_rel_set && b && tl >= 0 && tl < c->tbl[WLT_TIMELINE].count && c->timelines[tl].live)
        {
                wayland_buffer_signal_release(c, b);
                b->rel_pending = 1, b->rel_timeline = (unsigned short)tl, b->rel_point = s->pending.sync_rel_point, c->timelines[tl].refs++;
        }
        wayland_syncobj_clear_pending(c, s);
}

static int wayland_commit_ready(struct wayland_client *c, struct wayland_surface *s, long buffer)
{
        int sync_st = wayland_syncobj_check(c, s, buffer);

        if (sync_st)
                return sync_st < 0 ? WAYLAND_DEAD : WAYLAND_STALL;
        if (buffer >= 0 && !s->pending.sync_acq_set)
        {
                struct wayland_pool *held = &c->pools[c->buffers[c->objects[buffer].link].pool];
                if (held->dmabuf && wayland.host->dmabuf_ready && !wayland.host->dmabuf_ready(wayland.host->context, held->fd))
                        return wayland_stall_set(c, held->fd, -1, 0, -1), WAYLAND_STALL;
        }
        return WAYLAND_OK;
}

static inline int wayland_toplevel_bad_bounds(const struct wayland_surface *s)
{
        return s->role == WAYLAND_ROLE_TOPLEVEL &&
               ((s->u.top.min_width && s->u.top.max_width && s->u.top.min_width > s->u.top.max_width) ||
                (s->u.top.min_height && s->u.top.max_height && s->u.top.min_height > s->u.top.max_height));
}

static int wayland_commit_quiet(const struct wayland_surface *s, int xdg, int shown)
{
        return !s->pending.attach_made && !s->pending.damages && !s->pending.damage_full && !s->cbq[WLCB_CALLBACK].count &&
               !s->cbq[WLCB_FEEDBACK].count && !s->pending.barrier && !s->pending.opaque_set && s->pending.alpha == s->cur.alpha &&
               s->pending.icon == s->cur.icon && s->pending.async_hint == s->cur.async_hint && !s->pending.fifo_wait &&
               !s->pending.timestamp && !s->pending.sync_acq_set && !s->pending.sync_rel_set && !s->pending.dx && !s->pending.dy &&
               s->pending.scale == s->cur.scale && !wayland_state_xform_changed(&s->cur, &s->pending) &&
               s->role != WAYLAND_ROLE_CURSOR && s->role != WAYLAND_ROLE_SUBSURFACE && (!xdg || (s->flags & WAYLAND_SURFACE_SENT)) &&
               !(s->row < 0 && s->cbq[WLCB_READY].count) && !(shown && s->row >= 0 && (s->flags & WAYLAND_SURFACE_MAPPED) && wayland_has_children(s));
}

static void wayland_hold_buf(struct wayland_client *c, struct wayland_surface *s, long buf_idx)
{
        unsigned int now = (unsigned int)(buf_idx + 1);
        if (s->held != now)
                wayland_unhold(c, s), c->buffers[buf_idx].held++, s->held = now;
}

static int wayland_commit(struct wayland_client *c, struct wayland_object *self, struct wayland_surface *s)
{
        long buffer = -1;
        int layer = s->role == WAYLAND_ROLE_TOPLEVEL && s->u.top.layer_shell;
        int xdg = ((s->flags & WAYLAND_SURFACE_XDG) != 0) || layer, initial = 0, ready, stand = s->role == WAYLAND_ROLE_STAND_IN, stood;
        int shown = xdg || (s->xsurface && s->u.xwin.adopted && !s->u.xwin.dead);

        if (wayland_commit_quiet(s, xdg, shown) && (!xdg || s->role_id) && !wayland_toplevel_bad_bounds(s))
        {
                wayland_commit_take(c, s);
                if (s->cbq[WLCB_PRESENTING].count) wayland_feedback_take(c, s, s->row >= 0);
                return wayland.quiet++, WAYLAND_OK;
        }
        if (wayland_hold_wanted(c, s))
        {
                int fifo_hold = s->pending.fifo_wait && s->cur.barrier && s->barrier_frame == wayland.latches;
                c->hold = 1, c->hold_surface = wayland_surface_index(c, s);
                c->hold_at = s->xsurface && s->u.xwin.wait && !s->u.xwin.adopted ? s->u.xwin.wait
                             : (!fifo_hold && s->pending.timestamp) ? wayland_timing_wake(s->pending.timestamp) : 0;
                return wayland_stall_set(c, -1, -1, 0, -1), WAYLAND_STALL;
        }
        if (!wayland_budget_left(wayland_root_of(c, s)))
                return wayland_stall_set(c, -1, -1, 0, wayland_surface_index(c, wayland_root_of(c, s))), WAYLAND_STALL;
        shown = xdg || (s->xsurface && s->u.xwin.adopted && !s->u.xwin.dead) || (stand && wayland_stand_row(c, s) >= 0);
        stood = shown && !stand && wayland_row_stood(s->row);
        if (s->role == WAYLAND_ROLE_SUBSURFACE)
        {
                long sub_buf = s->pending.attach_made && s->pending.attached ? wayland_find(c, s->pending.attached, WLI_wl_buffer) : -1;
                if ((ready = wayland_commit_ready(c, s, sub_buf)) != WAYLAND_OK) return ready;
                if (sub_buf >= 0) wayland_syncobj_take(c, s, &c->buffers[c->objects[sub_buf].link]);
                return wayland_commit_take(c, s), wayland_sub_commit(c, self, s);
        }
        if (xdg)
        {
                if (!s->role_id)
                        return wayland_fail(c, s->xdg_id, WLN_xdg_surface_error_not_constructed, "the xdg_surface has no role");
                if (!(s->flags & WAYLAND_SURFACE_SENT))
                {
                        if (s->pending.attach_made && s->pending.attached)
                                return wayland_fail(c, layer ? s->role_id : s->xdg_id, WLN_xdg_surface_error_unconfigured_buffer, "a buffer before the first configure");
                        initial = 1;
                }
                else if (s->pending.attach_made && s->pending.attached && !(s->flags & WAYLAND_SURFACE_CONFIGURED))
                        return wayland_fail(c, layer ? s->role_id : s->xdg_id, WLN_xdg_surface_error_unconfigured_buffer, "a buffer before the first configure was acknowledged");
        }
        if (s->pending.attach_made && s->pending.attached && (buffer = wayland_find(c, s->pending.attached, WLI_wl_buffer)) < 0)
                return wayland_fail(c, self->id, WAYLAND_INVALID_OBJECT, "the attached buffer is gone");
        if ((ready = wayland_commit_ready(c, s, buffer)) != WAYLAND_OK)
                return ready;
        wayland_commit_take(c, s);
        if (wayland_toplevel_bad_bounds(s))
                return wayland_fail(c, s->role_id, WLN_xdg_toplevel_error_invalid_size, "the minimum size is above the maximum");
        if (initial)
        {
                if (s->role == WAYLAND_ROLE_TOPLEVEL && s->u.top.layer_shell)
                {
                        long out = s->u.top.layer_output >= 0 && s->u.top.layer_output < wayland.screens ? s->u.top.layer_output : 0;
                        long cw = s->u.top.layer_w > 0 ? s->u.top.layer_w : (wayland.screens ? wayland.screen[out].width : 1920);
                        long ch = s->u.top.layer_h > 0 ? s->u.top.layer_h : (wayland.screens ? wayland.screen[out].height : 1080);
                        s->u.top.layer_configured = 1, s->flags |= WAYLAND_SURFACE_SENT | WAYLAND_SURFACE_CONFIGURED;
                        wayland_ev(c, s->role_id, WLE_zwlr_layer_surface_v1_configure, (unsigned int)wayland_serial(c), (unsigned int)cw, (unsigned int)ch);
                }
                else if (s->role == WAYLAND_ROLE_TOPLEVEL)
                        wayland_configure(c, s, 1);
        }
        else if (buffer >= 0)
        {
                struct wayland_buffer *b = &c->buffers[c->objects[buffer].link];
                long tw, th, bw_t, bh_t, scale120 = wayland_commit_scale(s), error = wayland_viewport_error(&s->pending, b);

                wayland_transformed_size(b->width, b->height, s->pending.transform, &bw_t, &bh_t);
                if (error)
                        return wayland_fail(c, s->viewport_id, error, error == WLN_wp_viewport_error_out_of_buffer
                                                                          ? "the source is not inside the buffer"
                                                                          : "the source is not whole units and there is no destination");
                if (!wayland_viewport_used(&s->pending) && (bw_t % s->pending.scale || bh_t % s->pending.scale))
                        return wayland_fail(c, self->id, WLN_wl_surface_error_invalid_size, "the buffer is not a whole number of the scale");
                wayland_syncobj_take(c, s, b);
                if (shown && !stood && (stand ? wayland_stand_show(c, self, s, b, scale120) : wayland_show(c, self, s, b, scale120)) < 0)
                        return WAYLAND_DEAD;
                wayland_surface_target_pixels(s, &s->pending, b, &tw, &th), wayland_state_apply(s, tw, th, scale120);
                if (shown && !stood && !stand && s->row >= 0 && wayland_has_children(s))
                {
                        struct wayland_rect window = wayland_window_of(c, s);
                        wayland_hold_buf(c, s, c->objects[buffer].link);
                        if (!s->layered)
                        {
                                struct wayland_rect reach;
                                const struct wayland_rect *only = s->shown_full ? 0 : &s->shown_rect;
                                if (only)
                                {
                                        reach = wayland_flatten_reach(c, s, *only);
                                        if (reach.x != only->x || reach.y != only->y || reach.width != only->width || reach.height != only->height)
                                                wayland_child_copy(c, s, &reach, &window), only = &reach;
                                }
                                wayland_flatten(c, wayland_surface_index(c, s), only, &window);
                        }
                        wayland_sub_apply_children(c, wayland_surface_index(c, s)), wayland_row_take(c, s);
                }
                else if (s->role == WAYLAND_ROLE_CURSOR || stood) // a window stood in for holds its newest picture, to show again after
                {
                        wayland_hold_buf(c, s, c->objects[buffer].link);
                        if (stood && wayland_has_children(s))
                                wayland_sub_apply_children(c, wayland_surface_index(c, s));
                        if (stood)
                                wayland_lift_again(c, s);
                }
                else
                {
                        unsigned int now = (unsigned int)(c->objects[buffer].link + 1);
                        if (s->held == now) s->held = 0, b->held--;
                        else wayland_unhold(c, s);
                        if (b->held <= 0 && s->lent != now && s->lent_gone != now && s->lent_latched != now && s->lent_shown != now)
                                wayland_release_buffer(c, buffer);
                }
        }
        else if (s->pending.attach_made && !s->pending.attached)
        {
                if (stand)
                        wayland_unhold(c, s);
                else if (shown)
                        wayland_unhold(c, s), wayland_row_release(c, s),
                        s->flags &= ~(unsigned)(WAYLAND_SURFACE_CONFIGURED | WAYLAND_SURFACE_SENT | WAYLAND_SURFACE_MAPPED),
                        s->serials = 0, s->entered = 0;
                if (s->role == WAYLAND_ROLE_CURSOR) wayland_unhold(c, s);
                s->width = s->height = 0;
        }
        if (s->role == WAYLAND_ROLE_CURSOR && wayland.cursor_client == c->slot && wayland.cursor_surface == wayland_surface_index(c, s))
        {
                if (wayland.cursor_gen == s->gen)
                        wayland.cursor_hot_x = wayland_clamp(wayland.cursor_hot_x - s->pending.dx, -WAYLAND_DIMENSION, WAYLAND_DIMENSION),
                        wayland.cursor_hot_y = wayland_clamp(wayland.cursor_hot_y - s->pending.dy, -WAYLAND_DIMENSION, WAYLAND_DIMENSION);
                if (wayland.pointer_row >= 0 && wayland.row[wayland.pointer_row].client == c->slot)
                        wayland_row_do(wayland.pointer_row, WAYLAND_DO_CURSOR, s->held ? WAYLAND_CURSOR_ARROW : WAYLAND_CURSOR_HIDDEN, 0);
                wayland.cursor_serial++;
        }
        s->pending.dx = s->pending.dy = 0;
        if (buffer < 0 && !initial && shown && !stand && !stood && s->row >= 0 && (s->flags & WAYLAND_SURFACE_MAPPED) && wayland_has_children(s))
        {
                if (s->need_full) wayland_relay(c, s);
                wayland_sub_apply_children(c, wayland_surface_index(c, s)), wayland_row_take(c, s);
        }
        else if (buffer < 0 && !initial && stood && s->row >= 0 && s->row < wayland.row_made && wayland.row[s->row].lifted)
                wayland_sub_apply_children(c, wayland_surface_index(c, s)), wayland_lift_again(c, s); // its subsurfaces' state applied, as ever
        s->pending.attach_made = s->pending.attached = s->pending.damages = s->pending.damage_full = 0;
        if (s->cbq[WLCB_CALLBACK].count) wayland_callbacks_take(c, s);
        wayland_feedback_take(c, s, s->row >= 0);
        if (s->row < 0 && s->cbq[WLCB_READY].count) wayland_callbacks_done(c, s, wayland_now());
        return WAYLAND_OK;
}

static int wayland_surface_request(struct wayland_client *c, struct wayland_object *self, long op, struct wayland_arg *a)
{
        struct wayland_surface *s = wayland_surface_of(c, self - c->objects);

        if (!s)
                return WAYLAND_OK;
        switch (op)
        {
        case WLQ_wl_surface_destroy:
                while (s->cbq[WLCB_FEEDBACK].count) wayland_feedback_discard(c, wayland_cb_pop(c, &s->cbq[WLCB_FEEDBACK]));
                return wayland_presenting_drop(c, s), wayland_surface_drop(c, s), WAYLAND_OK;
        case WLQ_wl_surface_attach:
                if (self->version >= 5 && (a[1].i || a[2].i))
                        return wayland_fail(c, self->id, WLN_wl_surface_error_invalid_offset, "attach with an offset");
                s->pending.attached = a[0].slot >= 0 ? c->objects[a[0].slot].id : 0, s->pending.attach_made = 1;
                if (self->version < 5) s->pending.dx = a[1].i, s->pending.dy = a[2].i;
                return WAYLAND_OK;
        case WLQ_wl_surface_damage:
        case WLQ_wl_surface_damage_buffer:
                return wayland_damage_add(s, a, op == WLQ_wl_surface_damage_buffer), WAYLAND_OK;
        case WLQ_wl_surface_frame:
                if (!wayland_cb_push(c, &s->cbq[WLCB_CALLBACK], c->objects[a[0].slot].id))
                        wayland_callback_answer(c, c->objects[a[0].slot].id, wayland_now());
                return WAYLAND_OK;
        case WLQ_wl_surface_set_input_region:
                s->input_none = s->input_bounded = s->input_count = 0;
                if (a[0].slot >= 0)
                {
                        struct wayland_object *ro = &c->objects[a[0].slot];
                        struct wayland_region *r = ro->link >= 0 && ro->link < c->tbl[WLT_REGION].count ? &c->regions[ro->link] : 0;
                        if (!(ro->flags & 1)) s->input_none = s->input_bounded = 1;
                        else if (r && !r->overflow && r->count > 0 && r->count <= WAYLAND_REGION_RECTS)
                        {
                                s->input_bounded = 1, s->input_count = r->count;
                                for (long i = 0; i < r->count; i++) s->input_rects[i] = r->rects[i];
                        }
                        else if (r && r->bounds.width > 0 && r->bounds.height > 0)
                                s->input_bounded = 1, s->input_count = 1, s->input_rects[0] = r->bounds;
                }
                return WAYLAND_OK;
        case WLQ_wl_surface_set_opaque_region:
                s->pending.opaque_set = 1;
                s->pending.opaque = a[0].slot >= 0 && c->objects[a[0].slot].link >= 0 &&
                                    c->objects[a[0].slot].link < c->tbl[WLT_REGION].count && c->regions[c->objects[a[0].slot].link].exact
                                        ? c->regions[c->objects[a[0].slot].link].rect : (struct wayland_rect){0, 0, 0, 0};
                return WAYLAND_OK;
        case WLQ_wl_surface_offset: return s->pending.dx = a[0].i, s->pending.dy = a[1].i, WAYLAND_OK;
        case WLQ_wl_surface_commit: return wayland_commit(c, self, s);
        case WLQ_wl_surface_set_buffer_transform:
                return a[0].i < 0 || a[0].i > 7 ? wayland_fail(c, self->id, WLN_wl_surface_error_invalid_transform, "no such transform")
                                                : (s->pending.transform = a[0].i, WAYLAND_OK);
        case WLQ_wl_surface_set_buffer_scale:
                return a[0].i < 1 || a[0].i > 16 ? wayland_fail(c, self->id, WLN_wl_surface_error_invalid_scale, "no such scale")
                                                 : (s->pending.scale = a[0].i, WAYLAND_OK);
        default: return WAYLAND_OK;
        }
}

static int wayland_shm_request(struct wayland_client *c, struct wayland_object *self, long op, struct wayland_arg *a)
{
        long size = a[2].i, pool;
        void *at;

        (void)op;
        if (size < 1 || size > WAYLAND_POOL_BYTES)
                return wayland_fail(c, self->id, WLN_wl_shm_error_invalid_stride, "the pool's size is out of range");
        if (c->tbl[WLT_POOL].count - c->tbl[WLT_POOL].frees >= WAYLAND_POOLS_MAX)
                return wayland_fail(c, self->id, WAYLAND_NO_MEMORY, "too many pools");
        if ((pool = wayland_tbl_alloc(c, WLT_POOL)) < 0)
                return wayland_fail(c, self->id, WAYLAND_NO_MEMORY, "no memory for the pool");
        at = wayland.host->map(wayland.host->context, a[1].fd, size);
        wayland_fd_close(a[1].fd), a[1].fd = -1;
        if (!at)
                return wayland_tbl_free(c, WLT_POOL, pool), wayland_fail(c, self->id, WLN_wl_shm_error_invalid_fd, "not a shared memory file of that size");
        c->pools[pool] = (struct wayland_pool){.live = 1, .fd = -1, .extra_fd = {-1, -1, -1}, .size = size, .at = at};
        return c->objects[a[0].slot].link = (int)pool, wayland.pools++, WAYLAND_OK;
}

static int wayland_pool_request(struct wayland_client *c, struct wayland_object *self, long op, struct wayland_arg *a)
{
        long pool = self->link;
        struct wayland_pool *p = &c->pools[pool];

        if (op == WLQ_wl_shm_pool_destroy)
                return p->gone = 1, wayland_pool_unref(c, pool), WAYLAND_OK;
        if (op == WLQ_wl_shm_pool_resize)
        {
                long size = a[0].i;
                void *at;
                if (size < p->size || size > WAYLAND_POOL_BYTES)
                        return wayland_fail(c, self->id, WLN_wl_shm_error_invalid_fd, "the pool can only grow");
                if (!(at = wayland.host->remap(wayland.host->context, p->at, p->size, size)))
                        return wayland_fail(c, self->id, WLN_wl_shm_error_invalid_fd, "the pool cannot grow");
                return p->at = at, p->size = size, WAYLAND_OK;
        }
        long offset = a[1].i, width = a[2].i, height = a[3].i, stride = a[4].i, format = a[5].i, slot;
        if (format != WLN_wl_shm_format_argb8888 && format != WLN_wl_shm_format_xrgb8888 &&
            format != WLN_wl_shm_format_abgr8888 && format != WLN_wl_shm_format_xbgr8888)
                return wayland_fail(c, self->id, WLN_wl_shm_error_invalid_format, "the format is not offered");
        if (offset < 0 || width < 1 || height < 1 || width > WAYLAND_DIMENSION || height > WAYLAND_DIMENSION ||
            stride < width * 4 || stride > (1L << 20) || (stride & 3) || offset > p->size || stride * height > p->size - offset)
                return wayland_fail(c, self->id, WLN_wl_shm_error_invalid_stride, "the buffer does not fit the pool");
        if ((slot = wayland_tbl_alloc(c, WLT_BUFFER)) < 0)
                return wayland_fail(c, self->id, WAYLAND_NO_MEMORY, "no memory for the buffer");
        c->buffers[slot] = (struct wayland_buffer){
                .live = 1, .pool = pool, .offset = offset, .width = width, .height = height, .stride = stride, .format = format, .plane_count = 1,
                .fourcc = format == WLN_wl_shm_format_argb8888 ? WAYLAND_FOURCC_ARGB8888
                          : format == WLN_wl_shm_format_xrgb8888 ? WAYLAND_FOURCC_XRGB8888 : (unsigned int)format,
                .id = c->objects[a[0].slot].id,
        };
        return c->objects[a[0].slot].link = (int)slot, p->refs++, wayland.buffers++, WAYLAND_OK;
}

static int wayland_buffer_request(struct wayland_client *c, struct wayland_object *self, long op, struct wayland_arg *a)
{
        struct wayland_buffer *b = &c->buffers[self->link];

        (void)op, (void)a;
        for (long at = 0; at < c->tbl[WLT_SURFACE].count; at++)
        {
                struct wayland_surface *s = &c->surfaces[at];
                if (!s->live) continue;
                if (s->pending.attach_made && s->pending.attached == self->id) s->pending.attach_made = 0, s->pending.attached = 0;
                if (s->cached.attached == self->id) s->cached.attached = 0;
        }
        if (b->held > 0)
                return b->zombie = 1, WAYLAND_OK;
        wayland_buffer_signal_release(c, b), c->pools[b->pool].refs--, wayland_pool_unref(c, b->pool);
        return b->live = 0, wayland_tbl_free(c, WLT_BUFFER, self->link), wayland.buffers--, WAYLAND_OK;
}

/* The subsurface levels above a surface (0 for a root): the depth its subtree may hang below. */
static long wayland_sub_levels(struct wayland_client *c, const struct wayland_surface *s)
{
        long levels = 0;

        for (long hops = 0; s->role == WAYLAND_ROLE_SUBSURFACE && s->sub_parent && hops < c->tbl[WLT_SURFACE].count; hops++, levels++)
                s = &c->surfaces[s->sub_parent - 1];
        return levels;
}

/* The height of a surface's subsurface tree below it (0 with no children). Attaches keep it within WAYLAND_SUB_LEVELS. */
static long wayland_sub_height(struct wayland_client *c, const struct wayland_surface *s)
{
        long height = 0;

        for (int side = 0; side < 2; side++)
                for (unsigned int ch = s->first_child[side]; ch; ch = c->surfaces[ch - 1].next_sib)
                        height = wayland_max(height, 1 + wayland_sub_height(c, &c->surfaces[ch - 1]));
        return height;
}

static int wayland_subcompositor_request(struct wayland_client *c, struct wayland_object *self, long op, struct wayland_arg *a)
{
        struct wayland_surface *s, *parent;

        if (op == WLQ_wl_subcompositor_destroy)
                return WAYLAND_OK;
        if (!(s = wayland_surface_of(c, a[1].slot)) || !(parent = wayland_surface_of(c, a[2].slot)))
                return wayland_fail(c, self->id, WAYLAND_INVALID_OBJECT, "a surface is gone");
        if (s == parent)
                return wayland_fail(c, self->id, WLN_wl_subcompositor_error_bad_surface, "a surface cannot hang from itself");
        if (s->xsurface || (s->role != WAYLAND_ROLE_NONE && !(s->role == WAYLAND_ROLE_SUBSURFACE && !s->u.sub.id)) || (s->flags & WAYLAND_SURFACE_XDG))
                return wayland_fail(c, self->id, WLN_wl_subcompositor_error_bad_surface, "the surface has a role");
        long hops = 0;
        for (struct wayland_surface *up = parent; up && hops < c->tbl[WLT_SURFACE].count;
             hops++, up = up->role == WAYLAND_ROLE_SUBSURFACE && up->sub_parent ? &c->surfaces[up->sub_parent - 1] : 0)
                if (up == s)
                        return wayland_fail(c, self->id, WLN_wl_subcompositor_error_bad_parent, "the parent hangs from the surface");
        if (wayland_sub_levels(c, parent) + 1 + wayland_sub_height(c, s) > WAYLAND_SUB_LEVELS)
                return wayland_fail(c, self->id, WLN_wl_subcompositor_error_bad_parent, "the subsurfaces nest too deep");
        if (s->role != WAYLAND_ROLE_SUBSURFACE) c->subsurfaces++;
        wayland_sub_unlink(c, s);
        s->role = WAYLAND_ROLE_SUBSURFACE, s->sub_parent = (unsigned int)(wayland_surface_index(c, parent) + 1);
        s->u.sub.id = c->objects[a[0].slot].id, s->u.sub.sync = 1, s->u.sub.dirty = s->u.sub.pend_pos = 0;
        s->u.sub.x = s->u.sub.y = s->u.sub.pend_x = s->u.sub.pend_y = 0;
        wayland_sub_link_after(c, parent, s, 1, parent->last_child[1]), wayland_surface_link(c, a[0].slot, s);
        return WAYLAND_OK;
}

static int wayland_subsurface_request(struct wayland_client *c, struct wayland_object *self, long op, struct wayland_arg *a)
{
        struct wayland_surface *s = wayland_surface_of(c, self - c->objects), *other;

        if (!s)
                return WAYLAND_OK;
        if (op == WLQ_wl_subsurface_destroy)
                return wayland_sub_drop(c, s), s->u.sub.id = 0, WAYLAND_OK;
        if (op == WLQ_wl_subsurface_set_position)
        {
                s->u.sub.pend_x = wayland_clamp(a[0].i, -(1L << 20), 1L << 20), s->u.sub.pend_y = wayland_clamp(a[1].i, -(1L << 20), 1L << 20);
                return s->u.sub.pend_pos = s->u.sub.pend_x != s->u.sub.x || s->u.sub.pend_y != s->u.sub.y, WAYLAND_OK;
        }
        if (op == WLQ_wl_subsurface_place_above || op == WLQ_wl_subsurface_place_below)
        {
                struct wayland_surface *parent = s->sub_parent ? &c->surfaces[s->sub_parent - 1] : 0;
                int above = op == WLQ_wl_subsurface_place_above;
                other = wayland_surface_of(c, a[0].slot);
                if (!other || !parent || (other != parent && !(other->role == WAYLAND_ROLE_SUBSURFACE && other->sub_parent == s->sub_parent && other != s)))
                        return wayland_fail(c, self->id, WLN_wl_subsurface_error_bad_surface, "the surface is not a sibling or the parent");
                wayland_sub_unlink(c, s);
                wayland_sub_link_after(c, parent, s, other == parent ? above : other->sub_above,
                                       other == parent ? (above ? 0 : parent->last_child[0])
                                                       : (above ? (unsigned int)(wayland_surface_index(c, other) + 1) : other->prev_sib));
                return wayland_root_of(c, s)->need_full = 1, WAYLAND_OK;
        }
        s->u.sub.sync = op == WLQ_wl_subsurface_set_sync;
        if (!s->u.sub.sync && s->sub_parent && !wayland_sub_synced(c, s) && (s->u.sub.dirty || s->u.sub.pend_pos))
                wayland_sub_apply(c, s), wayland_sub_apply_children(c, wayland_surface_index(c, s)), wayland_row_take(c, wayland_root_of(c, s));
        return WAYLAND_OK;
}

/* ---- xdg_shell ---- */

static inline int wayland_place_int(long v) { return (int)wayland_clamp(v, -(1L << 30), 1L << 30); }

/* The compositor's one placement (compositor/place.h), in screen space: the anchor is moved by the parent's origin and the answer is moved back. */
static void wayland_popup_place(const struct wayland_positioner *p, long ox, long oy, const struct wayland_rect *sc,
                                long *x, long *y, long *width, long *height)
{
        struct compositor_place place = {0};
        int out[4];

        place.anchor[0] = wayland_place_int(ox + p->anchor_x), place.anchor[1] = wayland_place_int(oy + p->anchor_y);
        place.anchor[2] = wayland_place_int(p->anchor_width), place.anchor[3] = wayland_place_int(p->anchor_height);
        place.side = p->anchor >= 0 && p->anchor < 9 ? (unsigned int)p->anchor : 0;
        place.gravity = p->gravity >= 0 && p->gravity < 9 ? (unsigned int)p->gravity : 0;
        place.offset[0] = wayland_place_int(p->offset_x), place.offset[1] = wayland_place_int(p->offset_y);
        place.adjust = (p->adjustment & WLN_xdg_positioner_constraint_adjustment_flip_x ? COMPOSITOR_PLACE_FLIP_X : 0) |
                       (p->adjustment & WLN_xdg_positioner_constraint_adjustment_slide_x ? COMPOSITOR_PLACE_SLIDE_X : 0) |
                       (p->adjustment & WLN_xdg_positioner_constraint_adjustment_resize_x ? COMPOSITOR_PLACE_RESIZE_X : 0) |
                       (p->adjustment & WLN_xdg_positioner_constraint_adjustment_flip_y ? COMPOSITOR_PLACE_FLIP_Y : 0) |
                       (p->adjustment & WLN_xdg_positioner_constraint_adjustment_slide_y ? COMPOSITOR_PLACE_SLIDE_Y : 0) |
                       (p->adjustment & WLN_xdg_positioner_constraint_adjustment_resize_y ? COMPOSITOR_PLACE_RESIZE_Y : 0);
        place.size[0] = wayland_place_int(p->width), place.size[1] = wayland_place_int(p->height);
        place.screen[0] = wayland_place_int(sc->x), place.screen[1] = wayland_place_int(sc->y);
        place.screen[2] = wayland_place_int(sc->width), place.screen[3] = wayland_place_int(sc->height);
        compositor_place_rect(&place, out);
        *x = out[0] - ox, *y = out[1] - oy, *width = out[2], *height = out[3];
}

static void wayland_popup_configure(struct wayland_client *c, struct wayland_surface *s, const struct wayland_positioner *p,
                                    struct wayland_surface *parent)
{
        struct wayland_rect screen = {0, 0, 1 << 20, 1 << 20};
        struct wayland_facts facts = {0};
        long ox = 0, oy = 0, x, y, w, h, unit = parent ? wayland_surface_unit(parent) : WAYLAND_UNIT;

        if (parent && parent->row >= 0 && wayland_row_facts(parent->row, &facts) >= 0)
                ox = wayland_units(facts.x, unit), oy = wayland_units(facts.y, unit);
        for (long at = 0; at < wayland.screens; at++)
        {
                struct wayland_rect each = {wayland_units(wayland.screen[at].x, unit), wayland_units(wayland.screen[at].y, unit),
                                            wayland_units(wayland.screen[at].width, unit), wayland_units(wayland.screen[at].height, unit)};
                if (ox + p->anchor_x >= each.x && ox + p->anchor_x < each.x + each.width &&
                    oy + p->anchor_y >= each.y && oy + p->anchor_y < each.y + each.height)
                        screen = each;
        }
        wayland_popup_place(p, ox, oy, &screen, &x, &y, &w, &h);
        s->u.popup.x = wayland_pixels(x, unit), s->u.popup.y = wayland_pixels(y, unit);
        s->u.popup.width = wayland_pixels(w, unit), s->u.popup.height = wayland_pixels(h, unit);
        if (wayland_find(c, s->role_id, WLI_xdg_popup) >= 0 && s->serials < WAYLAND_SERIALS)
                wayland_ev(c, s->role_id, WLE_xdg_popup_configure, (unsigned int)x, (unsigned int)y, (unsigned int)w, (unsigned int)h),
                wayland_xdg_send_serial(c, s);
}

static int wayland_positioner_request(struct wayland_client *c, struct wayland_object *self, long op, struct wayland_arg *a)
{
        struct wayland_positioner *p = &c->positioners[self->link];

        switch (op)
        {
        case WLQ_xdg_positioner_destroy: return p->live = 0, wayland_tbl_free(c, WLT_POSITIONER, self->link), WAYLAND_OK;
        case WLQ_xdg_positioner_set_size:
                if (a[0].i < 1 || a[1].i < 1) return wayland_fail(c, self->id, WLN_xdg_positioner_error_invalid_input, "the size must be positive");
                return p->width = wayland_min(a[0].i, WAYLAND_DIMENSION), p->height = wayland_min(a[1].i, WAYLAND_DIMENSION), WAYLAND_OK;
        case WLQ_xdg_positioner_set_anchor_rect:
                if (a[2].i < 1 || a[3].i < 1) return wayland_fail(c, self->id, WLN_xdg_positioner_error_invalid_input, "the anchor rectangle must be positive");
                p->anchor_x = wayland_clamp(a[0].i, -(1L << 24), 1L << 24), p->anchor_y = wayland_clamp(a[1].i, -(1L << 24), 1L << 24);
                return p->anchor_width = wayland_min(a[2].i, WAYLAND_DIMENSION), p->anchor_height = wayland_min(a[3].i, WAYLAND_DIMENSION), WAYLAND_OK;
        case WLQ_xdg_positioner_set_anchor:
                return a[0].u > WLN_xdg_positioner_anchor_bottom_right ? wayland_fail(c, self->id, WLN_xdg_positioner_error_invalid_input, "no such anchor")
                                                                       : (p->anchor = (long)a[0].u, WAYLAND_OK);
        case WLQ_xdg_positioner_set_gravity:
                return a[0].u > WLN_xdg_positioner_gravity_bottom_right ? wayland_fail(c, self->id, WLN_xdg_positioner_error_invalid_input, "no such gravity")
                                                                        : (p->gravity = (long)a[0].u, WAYLAND_OK);
        case WLQ_xdg_positioner_set_constraint_adjustment: return p->adjustment = (long)(a[0].u & 63), WAYLAND_OK;
        case WLQ_xdg_positioner_set_offset:
                return p->offset_x = wayland_clamp(a[0].i, -(1L << 24), 1L << 24), p->offset_y = wayland_clamp(a[1].i, -(1L << 24), 1L << 24), WAYLAND_OK;
        case WLQ_xdg_positioner_set_reactive: return p->reactive = 1, WAYLAND_OK;
        default: return WAYLAND_OK;
        }
}

static int wayland_wm_base_request(struct wayland_client *c, struct wayland_object *self, long op, struct wayland_arg *a)
{
        struct wayland_surface *s;
        long slot;

        if (op == WLQ_xdg_wm_base_destroy)
        {
                for (long at = 0; at < c->tbl[WLT_OBJECT].count; at++)
                        if (c->objects[at].iface == WLI_xdg_surface)
                                return wayland_fail(c, self->id, WLN_xdg_wm_base_error_defunct_surfaces, "xdg_surfaces still exist");
        }
        else if (op == WLQ_xdg_wm_base_create_positioner)
        {
                if ((slot = wayland_tbl_alloc(c, WLT_POSITIONER)) < 0)
                        return wayland_fail(c, self->id, WAYLAND_NO_MEMORY, "no memory for the positioner");
                c->positioners[slot].live = 1, c->positioners[slot].id = c->objects[a[0].slot].id, c->objects[a[0].slot].link = (int)slot;
        }
        else if (op == WLQ_xdg_wm_base_get_xdg_surface)
        {
                if (!(s = wayland_surface_of(c, a[1].slot)))
                        return wayland_fail(c, self->id, WAYLAND_INVALID_OBJECT, "the surface is gone");
                if (s->flags & WAYLAND_SURFACE_XDG)
                        return wayland_fail(c, self->id, WLN_xdg_wm_base_error_role, "the surface has an xdg_surface");
                if (s->xsurface || s->role_id || (s->role == WAYLAND_ROLE_TOPLEVEL && s->u.top.layer_shell) ||
                    (s->role != WAYLAND_ROLE_NONE && s->role != WAYLAND_ROLE_TOPLEVEL && s->role != WAYLAND_ROLE_POPUP))
                        return wayland_fail(c, self->id, WLN_xdg_wm_base_error_role, "the surface has another role");
                if (s->width || (s->pending.attach_made && s->pending.attached))
                        return wayland_fail(c, self->id, WLN_xdg_wm_base_error_invalid_surface_state, "the surface has a buffer");
                s->flags |= WAYLAND_SURFACE_XDG, s->xdg_id = c->objects[a[0].slot].id, wayland_surface_link(c, a[0].slot, s);
        }
        return WAYLAND_OK;
}

static int wayland_facts_freed(struct wayland_surface *s, const struct wayland_facts *f)
{
        if (f->width || f->height || f->states != s->u.top.told_states || !s->u.top.told_width || !s->u.top.told_height)
                return 0;
        return s->u.top.told_width = s->u.top.told_height = 0, 1;
}

static void wayland_configure_owed(struct wayland_client *c, struct wayland_surface *s)
{
        struct wayland_facts f = {0};
        if (s->role == WAYLAND_ROLE_TOPLEVEL && s->u.top.told && s->row >= 0 && s->serials < WAYLAND_SERIALS &&
            wayland_row_facts(s->row, &f) >= 0 &&
            (f.width != s->u.top.told_width || f.height != s->u.top.told_height || f.states != s->u.top.told_states) &&
            !wayland_facts_freed(s, &f))
                wayland_configure(c, s, 0);
}

static int wayland_ancestor(struct wayland_client *c, long maybe, long of);

static void wayland_xs_role_drop(struct wayland_client *c, struct wayland_surface *s)
{
        wayland_unhold(c, s), wayland_row_release(c, s);
        s->role_id = 0, s->parent = 0, s->serials = 0, s->width = s->height = 0;
        s->flags &= ~(unsigned)(WAYLAND_SURFACE_CONFIGURED | WAYLAND_SURFACE_SENT | WAYLAND_SURFACE_MAPPED);
}

static int wayland_xdg_surface_request(struct wayland_client *c, struct wayland_object *self, long op, struct wayland_arg *a)
{
        struct wayland_surface *s = wayland_surface_of(c, self - c->objects), *parent = 0;

        if (!s) return WAYLAND_OK;
        if (op == WLQ_xdg_surface_destroy)
        {
                if (s->role_id)
                        return wayland_fail(c, self->id, WLN_xdg_surface_error_defunct_role_object, "the role object still exists");
                s->flags &= ~(unsigned)(WAYLAND_SURFACE_XDG | WAYLAND_SURFACE_CONFIGURED | WAYLAND_SURFACE_SENT);
                return s->xdg_id = 0, WAYLAND_OK;
        }
        if (op == WLQ_xdg_surface_get_toplevel || op == WLQ_xdg_surface_get_popup)
        {
                int popup = op == WLQ_xdg_surface_get_popup;
                struct wayland_positioner *p = 0;
                if (s->role_id)
                        return wayland_fail(c, self->id, WLN_xdg_surface_error_already_constructed, "the xdg_surface has a role");
                if (s->role != WAYLAND_ROLE_NONE && s->role != (popup ? WAYLAND_ROLE_POPUP : WAYLAND_ROLE_TOPLEVEL))
                        return wayland_fail(c, self->id, WLN_xdg_wm_base_error_role, "the surface has another role");
                if (s->width || (s->pending.attach_made && s->pending.attached))
                        return wayland_fail(c, self->id, WLN_xdg_surface_error_unconfigured_buffer, "the surface has a buffer");
                if (popup)
                {
                        parent = a[1].slot >= 0 ? wayland_surface_of(c, a[1].slot) : 0;
                        if (!parent || parent == s || !(parent->flags & WAYLAND_SURFACE_XDG) || !parent->role_id ||
                            (parent->role != WAYLAND_ROLE_TOPLEVEL && parent->role != WAYLAND_ROLE_POPUP) ||
                            wayland_ancestor(c, wayland_surface_index(c, s), wayland_surface_index(c, parent) + 1))
                                return wayland_fail(c, self->id, WLN_xdg_wm_base_error_invalid_popup_parent, "the popup's parent is not a window");
                        p = &c->positioners[c->objects[a[2].slot].link];
                        if (p->width < 1 || p->anchor_width < 1)
                                return wayland_fail(c, self->id, WLN_xdg_wm_base_error_invalid_positioner, "the positioner is not complete");
                }
                s->role = popup ? WAYLAND_ROLE_POPUP : WAYLAND_ROLE_TOPLEVEL;
                s->role_id = c->objects[a[0].slot].id, wayland_surface_link(c, a[0].slot, s);
                if (popup) s->parent = (unsigned int)(wayland_surface_index(c, parent) + 1), wayland_popup_configure(c, s, p, parent);
                else s->title[0] = s->app_id[0] = 0, s->u.top.min_width = s->u.top.min_height = s->u.top.max_width = s->u.top.max_height = 0;
                return WAYLAND_OK;
        }
        if (!s->role_id)
                return wayland_fail(c, self->id, WLN_xdg_surface_error_not_constructed, "the xdg_surface has no role");
        if (op == WLQ_xdg_surface_set_window_geometry)
        {
                if (a[2].i < 1 || a[3].i < 1)
                        return wayland_fail(c, self->id, WLN_xdg_surface_error_invalid_size, "the geometry must be positive");
                s->geometry_x = wayland_clamp(a[0].i, -(1L << 24), 1L << 24), s->geometry_y = wayland_clamp(a[1].i, -(1L << 24), 1L << 24);
                s->geometry_width = wayland_min(a[2].i, WAYLAND_DIMENSION), s->geometry_height = wayland_min(a[3].i, WAYLAND_DIMENSION);
                return s->geometry_set = 1, WAYLAND_OK;
        }
        for (long at = 0; at < s->serials; at++)
                if (s->serial[at] == a[0].u)
                {
                        for (long more = at + 1; more < s->serials; more++) s->serial[more - at - 1] = s->serial[more];
                        s->serials = (unsigned char)(s->serials - at - 1), s->flags |= WAYLAND_SURFACE_CONFIGURED, s->last_serial = (unsigned int)a[0].u;
                        return wayland_configure_owed(c, s), WAYLAND_OK;
                }
        return wayland_fail(c, self->id, WLN_xdg_surface_error_invalid_serial, "the serial was not sent");
}

static int wayland_ancestor(struct wayland_client *c, long maybe, long of)
{
        for (long hops = 0; hops < c->tbl[WLT_SURFACE].count && of; hops++, of = (long)c->surfaces[of - 1].parent)
                if (of - 1 == maybe) return 1;
        return 0;
}

static int wayland_toplevel_request(struct wayland_client *c, struct wayland_object *self, long op, struct wayland_arg *a)
{
        struct wayland_surface *s = wayland_surface_of(c, self - c->objects);
        if (!s) return WAYLAND_OK;
        long row = s->row;
        switch (op)
        {
        case WLQ_xdg_toplevel_destroy:
                wayland_xs_role_drop(c, s), s->u.top.want_maximized = s->u.top.want_fullscreen = 0;
                break;
        case WLQ_xdg_toplevel_set_parent:
        {
                struct wayland_surface *p = a[0].slot >= 0 ? wayland_surface_of(c, a[0].slot) : 0;
                if (a[0].slot >= 0 && (!p || p->role != WAYLAND_ROLE_TOPLEVEL || p == s ||
                                       wayland_ancestor(c, wayland_surface_index(c, s), wayland_surface_index(c, p) + 1)))
                        return wayland_fail(c, self->id, WLN_xdg_toplevel_error_invalid_parent, "that parent makes a loop");
                s->parent = p ? (unsigned int)(wayland_surface_index(c, p) + 1) : 0;
                break;
        }
        case WLQ_xdg_toplevel_set_title:
                wayland_text(s->title, a[0].text, a[0].length);
                if (row >= 0) wayland.host->row_title(wayland.host->context, row, s->title, 0);
                break;
        case WLQ_xdg_toplevel_set_app_id:
                wayland_text(s->app_id, a[0].text, a[0].length);
                if (row >= 0 && !s->title[0]) wayland.host->row_title(wayland.host->context, row, s->app_id, 0);
                if (row >= 0 && wayland.host->row_app) wayland.host->row_app(wayland.host->context, (int)row, s->app_id);
                break;
        case WLQ_xdg_toplevel_move:
                if (row >= 0 && c->press_serial && a[1].u == c->press_serial) wayland_row_do(row, WAYLAND_DO_MOVE, (long)a[1].u, 0);
                break;
        case WLQ_xdg_toplevel_resize:
                if (a[2].u > WLN_xdg_toplevel_resize_edge_bottom_right || (a[2].u & 3) == 3)
                        return wayland_fail(c, self->id, WLN_xdg_toplevel_error_invalid_resize_edge, "no such edge");
                if (row >= 0 && a[2].u && c->press_serial && a[1].u == c->press_serial) wayland_row_do(row, WAYLAND_DO_RESIZE, (long)a[2].u, 0);
                break;
        case WLQ_xdg_toplevel_set_max_size:
        case WLQ_xdg_toplevel_set_min_size:
                if (a[0].i < 0 || a[1].i < 0) return wayland_fail(c, self->id, WLN_xdg_toplevel_error_invalid_size, "a negative size");
                if (op == WLQ_xdg_toplevel_set_max_size) s->u.top.max_width = a[0].i, s->u.top.max_height = a[1].i;
                else s->u.top.min_width = a[0].i, s->u.top.min_height = a[1].i;
                if (row >= 0)
                {
                        /* logical units, as the configure path stores them (the table scales them itself) */
                        wayland_row_do(row, op == WLQ_xdg_toplevel_set_max_size ? WAYLAND_DO_MAX_SIZE : WAYLAND_DO_MIN_SIZE, a[0].i, a[1].i);
                }
                break;
        case WLQ_xdg_toplevel_set_maximized:
        case WLQ_xdg_toplevel_unset_maximized:
        case WLQ_xdg_toplevel_set_fullscreen:
        case WLQ_xdg_toplevel_unset_fullscreen:
        case WLQ_xdg_toplevel_set_minimized:
        {
                static const unsigned char sdo[5] = {WAYLAND_DO_MAXIMIZE, WAYLAND_DO_UNMAXIMIZE, WAYLAND_DO_FULLSCREEN, WAYLAND_DO_UNFULLSCREEN, WAYLAND_DO_MINIMIZE};
                long what = sdo[op - WLQ_xdg_toplevel_set_maximized];
                if (op <= WLQ_xdg_toplevel_unset_maximized) s->u.top.want_maximized = op == WLQ_xdg_toplevel_set_maximized;
                else if (op <= WLQ_xdg_toplevel_unset_fullscreen && (s->u.top.want_fullscreen = op == WLQ_xdg_toplevel_set_fullscreen))
                        s->u.top.want_screen = a[0].slot >= 0 && c->objects[a[0].slot].iface == WLI_wl_output &&
                                               c->objects[a[0].slot].aux < WAYLAND_SCREENS ? (short)c->objects[a[0].slot].aux : -1;
                if (row >= 0) wayland_row_do(row, what, what == WAYLAND_DO_FULLSCREEN ? s->u.top.want_screen : 0, 0);
                break;
        }
        }
        return WAYLAND_OK;
}

static int wayland_popup_request(struct wayland_client *c, struct wayland_object *self, long op, struct wayland_arg *a)
{
        struct wayland_surface *s = wayland_surface_of(c, self - c->objects);
        if (!s) return WAYLAND_OK;
        if (op == WLQ_xdg_popup_destroy)
        {
                for (long at = 0; at < c->tbl[WLT_SURFACE].count; at++)
                        if (c->surfaces[at].live && c->surfaces[at].role == WAYLAND_ROLE_POPUP && c->surfaces[at].role_id &&
                            c->surfaces[at].parent == (unsigned int)(wayland_surface_index(c, s) + 1))
                                return wayland_fail(c, self->id, WLN_xdg_wm_base_error_not_the_topmost_popup, "a popup is destroyed before the one over it");
                wayland_xs_role_drop(c, s);
        }
        else if (op == WLQ_xdg_popup_grab)
        {
                if (s->flags & WAYLAND_SURFACE_MAPPED)
                        return wayland_fail(c, self->id, WLN_xdg_popup_error_invalid_grab, "the popup is already shown");
                s->u.popup.grab = 1;
        }
        else
        {
                const struct wayland_positioner *p = &c->positioners[c->objects[a[0].slot].link];
                if (p->width < 1 || p->anchor_width < 1)
                        return wayland_fail(c, self->id, WLN_xdg_wm_base_error_invalid_positioner, "the positioner is not complete");
                wayland_ev(c, self->id, WLE_xdg_popup_repositioned, (unsigned int)a[1].u);
                wayland_popup_configure(c, s, p, s->parent ? &c->surfaces[s->parent - 1] : 0);
                if (s->row >= 0) wayland_row_do(s->row, WAYLAND_DO_PLACE, s->u.popup.x, s->u.popup.y);
        }
        return WAYLAND_OK;
}

static int wayland_output_manager_request(struct wayland_client *c, struct wayland_object *self, long op, struct wayland_arg *a)
{
        (void)self;
        if (op == WLQ_zxdg_output_manager_v1_get_xdg_output)
        {
                long slot = a[0].slot;
                c->objects[slot].aux = c->objects[a[1].slot].aux;
                wayland_xdg_output_send(c, slot, c->objects[slot].aux);
                if (c->objects[slot].version >= 3 && c->objects[a[1].slot].version >= 2)
                        wayland_ev0(c, c->objects[a[1].slot].id, WLE_wl_output_done);
        }
        return WAYLAND_OK;
}

/* ---- seat: pointer, touch, text-input, keyboard ---- */

WAYLAND_API long wayland_row_grabs(long row)
{
        struct wayland_client *c;
        struct wayland_surface *s = wayland_row_surface(row, &c);
        return s && s->role == WAYLAND_ROLE_POPUP && s->u.popup.grab;
}

static inline struct wayland_ids *wayland_seat_ids(struct wayland_client *c, long iface) { return &c->seat_ids[iface == WLI_wl_pointer ? 0 : iface == WLI_wl_touch ? 2 : 1]; }

static long wayland_ids_next(struct wayland_client *c, const struct wayland_ids *ids, long iface, long *cursor)
{
        while (*cursor < ids->count)
        {
                long slot = wayland_slot_of(c, ids->id[(*cursor)++]);
                if (slot >= 0 && c->objects[slot].iface == iface)
                        return slot;
        }
        return -1;
}

static inline long wayland_seat_next(struct wayland_client *c, long iface, long *cursor) { return wayland_ids_next(c, wayland_seat_ids(c, iface), iface, cursor); }
static inline int wayland_seat_any(struct wayland_client *c, long iface) { long at = 0; return wayland_seat_next(c, iface, &at) >= 0; }
static inline void wayland_pointer_frame(struct wayland_client *c, long slot) { if (c->objects[slot].version >= 5) wayland_emit(c, c->objects[slot].id, WLE_wl_pointer_frame, 0, 0); }

static void wayland_pointer_frames(struct wayland_client *c)
{
        for (long at = 0, slot; (slot = wayland_seat_next(c, WLI_wl_pointer, &at)) >= 0;)
                wayland_pointer_frame(c, slot);
}

static void wayland_seat_send(struct wayland_client *c, long iface, long op, const unsigned int *w, long n, int frame)
{
        for (long at = 0, slot; (slot = wayland_seat_next(c, iface, &at)) >= 0;)
        {
                wayland_emit(c, c->objects[slot].id, op, w, n);
                if (frame && iface == WLI_wl_pointer) wayland_pointer_frame(c, slot);
                else if (frame && iface == WLI_wl_touch) wayland_ev0(c, c->objects[slot].id, WLE_wl_touch_frame);
        }
}

#define wayland_seat_ev(c, iface, op, frame, ...) wayland_seat_send((c), (iface), (op), (const unsigned int[]){__VA_ARGS__}, (long)(sizeof((const unsigned int[]){__VA_ARGS__}) / sizeof(unsigned int)), (frame))

static int wayland_surface_hits(const struct wayland_surface *s, int is_root, long px, long py)
{
        if (s->input_none || (!is_root && !s->held) || (s->width > 0 && s->height > 0 && (px < 0 || py < 0 || px >= s->width || py >= s->height)))
                return 0;
        if (!s->input_bounded)
                return 1;
        long scale = s->scale120 > 0 ? s->scale120 : WAYLAND_UNIT, lx = wayland_units(px, scale), ly = wayland_units(py, scale);
        for (long i = 0; i < s->input_count; i++)
                if (lx >= s->input_rects[i].x && lx < s->input_rects[i].x + s->input_rects[i].width &&
                    ly >= s->input_rects[i].y && ly < s->input_rects[i].y + s->input_rects[i].height)
                        return 1;
        return 0;
}

static struct wayland_surface *wayland_pick_node(struct wayland_client *c, struct wayland_surface *node, const struct wayland_surface *root,
                                                 long ax, long ay, long root_scale, long x, long y, long depth, long *sx, long *sy)
{
        if (depth >= c->tbl[WLT_SURFACE].count)
                return 0;
        for (int layer = 1; layer >= 0; layer--)
        {
                for (unsigned int ch = node->last_child[layer]; ch; ch = c->surfaces[ch - 1].prev_sib)
                {
                        struct wayland_surface *cs = &c->surfaces[ch - 1];
                        struct wayland_surface *hit = wayland_pick_node(c, cs, root, ax + cs->u.sub.x, ay + cs->u.sub.y, root_scale, x, y, depth + 1, sx, sy);
                        if (hit) return hit;
                }
                if (layer == 1)
                {
                        long lx = x - wayland_pixels(ax, root_scale), ly = y - wayland_pixels(ay, root_scale);
                        if (wayland_surface_hits(node, node == root, lx, ly))
                                return *sx = lx, *sy = ly, node;
                }
        }
        return 0;
}

static struct wayland_surface *wayland_pick(struct wayland_client *c, struct wayland_surface *root, long x, long y, long *sx, long *sy)
{
        struct wayland_rect w = wayland_window_of(c, root);
        struct wayland_surface *hit = 0;
        x += w.x, y += w.y;
        if (wayland_has_children(root) || root->input_bounded)
                hit = wayland_pick_node(c, root, root, 0, 0, root->scale120 > 0 ? root->scale120 : WAYLAND_UNIT, x, y, 0, sx, sy);
        return hit ? hit : (*sx = x, *sy = y, root);
}

WAYLAND_API int wayland_dragging(void);
WAYLAND_API void wayland_drag_motion(long row, long x, long y);
WAYLAND_API void wayland_drag_cancel(void);
static void wayland_drag_release(void);

static void wayland_pointer_to_at(long row, long x, long y, struct wayland_surface **picked, long *sx_out, long *sy_out)
{
        struct wayland_client *old = 0, *now = 0;
        struct wayland_surface *root, *is = 0;
        long sx = 0, sy = 0;

        if (picked) *picked = 0;
        if (!wayland.host) return;
        if (wayland_dragging()) { wayland_drag_motion(row, x, y); return; }
        if ((root = wayland_row_surface(row, &now)))
                is = wayland_pick(now, root, x, y, &sx, &sy);
        if (picked) *picked = is;
        if (sx_out) *sx_out = sx;
        if (sy_out) *sy_out = sy;
        wayland.pointer_x = x, wayland.pointer_y = y;
        if (is && row == wayland.pointer_row && wayland_surface_index(now, is) == wayland.pointer_surface)
                return;
        wayland_counts_send(1);
        if (wayland.pointer_row >= 0 && wayland_row_surface(wayland.pointer_row, &old) && wayland.pointer_surface >= 0 &&
            old->surfaces[wayland.pointer_surface].live)
                wayland_seat_ev(old, WLI_wl_pointer, WLE_wl_pointer_leave, 1,
                                (unsigned int)wayland_serial(old), old->surfaces[wayland.pointer_surface].id);
        if (wayland.pointer_row >= 0 && wayland.pointer_row != (is ? row : -1))
                wayland_row_do(wayland.pointer_row, WAYLAND_DO_CURSOR, -1, 0);
        if (is)
                wayland_row_do(row, WAYLAND_DO_CURSOR, -1, 0);
        if (wayland.cursor_client >= 0)
                wayland.cursor_client = wayland.cursor_surface = -1, wayland.cursor_serial++;
        wayland.pointer_row = is ? row : -1, wayland.pointer_surface = is ? wayland_surface_index(now, is) : -1;
        if (is)
        {
                unsigned long serial = wayland_input_serial(now);
                now->pointer_enter = (unsigned int)serial;
                wayland_seat_ev(now, WLI_wl_pointer, WLE_wl_pointer_enter, 1, (unsigned int)serial, is->id,
                                (unsigned int)wayland_units_fixed(sx, is->scale120), (unsigned int)wayland_units_fixed(sy, is->scale120));
        }
        wayland_constraints_update();
}

WAYLAND_API void wayland_pointer_to(long row, long x, long y) { wayland_pointer_to_at(row, x, y, 0, 0, 0); }

WAYLAND_API void wayland_pointer_motion(long row, long x, long y)
{
        struct wayland_client *c;
        struct wayland_surface *s = 0;
        long sx = 0, sy = 0;

        if (!wayland.host) return;
        if (wayland_dragging()) { wayland_drag_motion(row, x, y); return; }
        if (row != wayland.pointer_row || (wayland.hold_mode == 1 && row == wayland.hold_row) || !wayland_row_surface(row, &c))
                return;
        wayland_pointer_to_at(row, x, y, &s, &sx, &sy);
        if (wayland.pointer_row != row || wayland.pointer_surface < 0 || !s)
                return;
        wayland_seat_ev(c, WLI_wl_pointer, WLE_wl_pointer_motion, 0, (unsigned int)wayland_now(),
                        (unsigned int)wayland_units_fixed(sx, s->scale120), (unsigned int)wayland_units_fixed(sy, s->scale120));
        if (wayland.counts_row == row) wayland_counts_send(0);
        wayland_pointer_frames(c);
}

static int wayland_touch_find(long id)
{
        for (int at = 0; at < WAYLAND_TOUCHES; at++)
                if (wayland.touch[at].live && wayland.touch[at].id == id) return at;
        return -1;
}

WAYLAND_API long wayland_row_takes_touch(long row) { struct wayland_client *c = 0; return wayland_row_surface(row, &c) && wayland_seat_any(c, WLI_wl_touch); }
WAYLAND_API long wayland_touch_row(long id) { int at = wayland_touch_find(id); return at < 0 ? -1 : wayland.touch[at].row; }

WAYLAND_API void wayland_touch_down(long row, long id, long x, long y)
{
        struct wayland_client *c = 0;
        struct wayland_surface *root, *s;
        long sx = 0, sy = 0, point = -1;

        if (!wayland.host || id < 0 || wayland_touch_find(id) >= 0 || !(root = wayland_row_surface(row, &c)))
                return;
        for (int at = 0; at < WAYLAND_TOUCHES && point < 0; at++)
                if (!wayland.touch[at].live) point = at;
        if (point < 0 || !wayland_seat_any(c, WLI_wl_touch) || !(s = wayland_pick(c, root, x, y, &sx, &sy)))
                return;
        unsigned long serial = wayland_input_serial(c);
        c->press_serial = (unsigned int)serial;
        wayland.touch[point] = (struct wayland_touch){1, (int)c->slot, (int)row, (int)wayland_surface_index(c, s), id, x, y};
        wayland_seat_ev(c, WLI_wl_touch, WLE_wl_touch_down, 1, (unsigned int)serial, (unsigned int)wayland_now(), s->id, (unsigned int)id,
                        (unsigned int)wayland_units_fixed(sx, s->scale120), (unsigned int)wayland_units_fixed(sy, s->scale120));
}

WAYLAND_API void wayland_touch_motion(long id, long x, long y)
{
        struct wayland_client *c = 0;
        struct wayland_surface *s, *root;
        long ox = 0, oy = 0;
        int at = wayland_touch_find(id);

        if (!wayland.host || at < 0 || (wayland.touch[at].x == x && wayland.touch[at].y == y) ||
            !(root = wayland_row_surface(wayland.touch[at].row, &c)) || c->slot != wayland.touch[at].client)
                return;
        if (!(s = &c->surfaces[wayland.touch[at].surface])->live)
                return;
        wayland.touch[at].x = x, wayland.touch[at].y = y;
        {
                struct wayland_rect w = wayland_window_of(c, root);
                x += w.x, y += w.y;
        }
        if (s->role == WAYLAND_ROLE_SUBSURFACE) wayland_sub_offset(c, s, &ox, &oy);
        wayland_seat_ev(c, WLI_wl_touch, WLE_wl_touch_motion, 1, (unsigned int)wayland_now(), (unsigned int)id,
                        (unsigned int)wayland_units_fixed(x - ox, s->scale120), (unsigned int)wayland_units_fixed(y - oy, s->scale120));
}

WAYLAND_API void wayland_touch_up(long id)
{
        int at = wayland_touch_find(id);
        if (!wayland.host || at < 0) return;
        struct wayland_client *c = wayland.client[wayland.touch[at].client];
        wayland.touch[at].live = 0;
        if (c) wayland_seat_ev(c, WLI_wl_touch, WLE_wl_touch_up, 1, (unsigned int)wayland_input_serial(c), (unsigned int)wayland_now(), (unsigned int)id);
}

static void wayland_touch_forget(long slot)
{
        for (int at = 0; at < WAYLAND_TOUCHES; at++)
                if (wayland.touch[at].live && wayland.touch[at].client == slot) wayland.touch[at].live = 0;
}

WAYLAND_API void wayland_touch_cancel(void)
{
        for (int at = 0; at < WAYLAND_TOUCHES; at++)
                if (wayland.touch[at].live)
                {
                        struct wayland_client *c = wayland.client[wayland.touch[at].client];
                        wayland_touch_forget(wayland.touch[at].client);
                        if (c) wayland_seat_send(c, WLI_wl_touch, WLE_wl_touch_cancel, 0, 0, 1);
                }
}

WAYLAND_API void wayland_pointer_button(long row, long code, long pressed)
{
        struct wayland_client *c;
        if (!wayland.host || code < 0 || code > 0x2ff) return;
        wayland.buttons += pressed ? 1 : wayland.buttons > 0 ? -1 : 0;
        if (wayland_dragging()) { if (!pressed) wayland_drag_release(); return; }
        if (row != wayland.pointer_row || !wayland_row_surface(row, &c)) return;
        unsigned long serial = wayland_input_serial(c);
        if (pressed) c->press_serial = (unsigned int)serial;
        wayland_seat_ev(c, WLI_wl_pointer, WLE_wl_pointer_button, 1, (unsigned int)serial, (unsigned int)wayland_now(), (unsigned int)code,
                        pressed ? WLN_wl_pointer_button_state_pressed : WLN_wl_pointer_button_state_released);
}

WAYLAND_API void wayland_pointer_axis(long row, long horizontal, long v120, long notch_px)
{
        struct wayland_client *c;
        if (!wayland.host || row != wayland.pointer_row || !wayland_row_surface(row, &c) || !v120) return;
        int precise = notch_px > 0;
        unsigned int time = (unsigned int)wayland_now();
        unsigned int axis = horizontal ? WLN_wl_pointer_axis_horizontal_scroll : WLN_wl_pointer_axis_vertical_scroll;
        long notches = 0;
        if (precise) c->finger_scroll = 1;
        else
        {
                long *left = &c->axis_left[horizontal ? 1 : 0];
                notches = (*left += v120) / 120;
                *left -= notches * 120;
        }
        for (long at = 0, slot; (slot = wayland_seat_next(c, WLI_wl_pointer, &at)) >= 0;)
        {
                struct wayland_object *o = &c->objects[slot];
                if (o->version >= 5)
                        wayland_ev(c, o->id, WLE_wl_pointer_axis_source, precise ? WLN_wl_pointer_axis_source_finger : WLN_wl_pointer_axis_source_wheel);
                if (o->version >= 9)
                        wayland_ev(c, o->id, WLE_wl_pointer_axis_relative_direction, axis, WLN_wl_pointer_axis_relative_direction_identical);
                if (!precise && o->version >= 8)
                        wayland_ev(c, o->id, WLE_wl_pointer_axis_value120, axis, (unsigned int)v120);
                else if (!precise && o->version >= 5 && notches)
                        wayland_ev(c, o->id, WLE_wl_pointer_axis_discrete, axis, (unsigned int)notches);
                wayland_ev(c, o->id, WLE_wl_pointer_axis, time, axis, (unsigned int)(v120 * (precise ? notch_px : WAYLAND_NOTCH) * 256 / 120));
                wayland_pointer_frame(c, slot);
        }
}

WAYLAND_API void wayland_pointer_axis_stop(long row)
{
        struct wayland_client *c;
        if (!wayland.host || row != wayland.pointer_row || !wayland_row_surface(row, &c) || !c->finger_scroll) return;
        c->finger_scroll = 0;
        unsigned int time = (unsigned int)wayland_now();
        for (long at = 0, slot; (slot = wayland_seat_next(c, WLI_wl_pointer, &at)) >= 0;)
                if (c->objects[slot].version >= 5)
                {
                        wayland_ev(c, c->objects[slot].id, WLE_wl_pointer_axis_source, WLN_wl_pointer_axis_source_finger);
                        wayland_ev(c, c->objects[slot].id, WLE_wl_pointer_axis_stop, time, WLN_wl_pointer_axis_vertical_scroll);
                        wayland_ev(c, c->objects[slot].id, WLE_wl_pointer_axis_stop, time, WLN_wl_pointer_axis_horizontal_scroll);
                        wayland_pointer_frame(c, slot);
                }
}

static void wayland_text_sync(struct wayland_client *c, struct wayland_text *t)
{
        int want = t->enabled && t->row >= 0 ? t->row : -1;
        struct wayland_client *owner = 0;
        struct wayland_surface *s;
        long caret = -1;

        if (t->counted >= 0 && t->counted != want) wayland_row_do(t->counted, WAYLAND_DO_TEXT, -1, 0), t->counted = -1;
        if (want >= 0 && t->counted < 0) wayland_row_do(want, WAYLAND_DO_TEXT, 1, (long)t->purpose), t->counted = want;
        if (t->counted < 0) return;
        if ((s = wayland_row_surface(t->counted, &owner)) && owner == c && t->caret >= 0) caret = wayland_pixels(t->caret, s->scale120);
        wayland_row_do(t->counted, WAYLAND_DO_TEXT, 0, (long)t->purpose), wayland_row_do(t->counted, WAYLAND_DO_CARET, caret, 0);
}

static void wayland_text_close(struct wayland_client *c, struct wayland_text *t) { t->enabled = 0, t->row = -1; wayland_text_sync(c, t); t->live = 0; }

static void wayland_texts_row_gone(struct wayland_client *c, long row)
{
        for (long at = 0; at < c->tbl[WLT_TEXT].count; at++)
                if (c->texts[at].live && c->texts[at].row == row) c->texts[at].row = -1, wayland_text_sync(c, &c->texts[at]);
}

static void wayland_texts_focus(struct wayland_client *c, long row, unsigned int surface, int entered)
{
        for (long at = 0; at < c->tbl[WLT_TEXT].count; at++)
        {
                struct wayland_text *t = &c->texts[at];
                if (!t->live || (!entered && t->row != row)) continue;
                t->row = entered ? (int)row : -1;
                wayland_ev(c, t->id, entered ? WLE_zwp_text_input_v3_enter : WLE_zwp_text_input_v3_leave, surface);
                wayland_text_sync(c, t);
        }
}

static void wayland_texts_drop(struct wayland_client *c)
{
        for (long at = 0; at < c->tbl[WLT_TEXT].count; at++)
                if (c->texts[at].live) wayland_text_close(c, &c->texts[at]);
}

static int wayland_text_request(struct wayland_client *c, struct wayland_object *self, long op, struct wayland_arg *a)
{
        struct wayland_text *t;
        if (self->iface == WLI_zwp_text_input_manager_v3)
        {
                if (op != WLQ_zwp_text_input_manager_v3_get_text_input) return WAYLAND_OK;
                long index = wayland_tbl_alloc(c, WLT_TEXT);
                if (index < 0) return wayland_new_fail(c, self->id, a[0].u);
                c->texts[index] = (struct wayland_text){.id = (unsigned int)a[0].u, .live = 1, .caret = -1, .pending_caret = -1, .row = -1, .counted = -1};
                c->objects[a[0].slot].link = (int)index;
                struct wayland_client *owner = 0;
                struct wayland_surface *focus = wayland_row_surface(wayland.keyboard_row, &owner);
                if (focus && owner == c) wayland_texts_focus(c, wayland.keyboard_row, focus->id, 1);
                return WAYLAND_OK;
        }
        if (self->link < 0 || self->link >= c->tbl[WLT_TEXT].count || !c->texts[self->link].live)
                return WAYLAND_OK;
        t = &c->texts[self->link];
        switch (op)
        {
        case WLQ_zwp_text_input_v3_destroy: wayland_text_close(c, t), wayland_tbl_free(c, WLT_TEXT, self->link), self->link = -1; break;
        case WLQ_zwp_text_input_v3_enable:
        case WLQ_zwp_text_input_v3_disable: t->pending_enabled = op == WLQ_zwp_text_input_v3_enable; break;
        case WLQ_zwp_text_input_v3_set_content_type:
                t->pending_purpose = a[1].u <= WLN_zwp_text_input_v3_content_purpose_terminal
                                         ? (unsigned char)a[1].u : (unsigned char)WLN_zwp_text_input_v3_content_purpose_normal;
                break;
        case WLQ_zwp_text_input_v3_set_cursor_rectangle:
                t->pending_caret = a[2].i < 0 || a[3].i < 0 || a[1].i + a[3].i < 0 ? -1 : a[1].i + a[3].i;
                break;
        case WLQ_zwp_text_input_v3_commit:
                t->enabled = t->pending_enabled, t->purpose = t->pending_purpose, t->caret = t->pending_caret;
                wayland_text_sync(c, t), wayland_ev(c, t->id, WLE_zwp_text_input_v3_done, (unsigned int)++t->serial);
                break;
        }
        return WAYLAND_OK;
}

static void wayland_keyboard_repeat(struct wayland_client *c, long slot)
{
        long rate = 25, delay = 600;
        if (c->objects[slot].version < 4) return;
        wayland.host->repeat(wayland.host->context, &rate, &delay);
        wayland_ev(c, c->objects[slot].id, WLE_wl_keyboard_repeat_info, (unsigned int)rate, (unsigned int)delay);
}

static void wayland_keyboard_keymap(struct wayland_client *c, long slot)
{
        long size = 0, fd = wayland.host->keymap(wayland.host->context, &size);
        struct wayland_event e = wayland_begin(c, c->objects[slot].id, WLE_wl_keyboard_keymap);
        int ok = fd >= 0 && size > 0 && size <= WAYLAND_KEYMAP_MAX && wayland_out_fd_room(c);

        wayland_word(&e, ok ? WLN_wl_keyboard_keymap_format_xkb_v1 : WLN_wl_keyboard_keymap_format_no_keymap);
        wayland_word(&e, ok ? (unsigned long)size : 0);
        if (ok) wayland_out_fd_put(c, fd, 0, e.start);
        wayland_end(&e), wayland_keyboard_repeat(c, slot);
}

static inline int wayland_key_down(long code) { return (wayland.keys[code >> 3] >> (code & 7)) & 1; }

static unsigned long wayland_depressed(void)
{
        static const unsigned char map[8][2] = {{42, 1}, {54, 1}, {29, 4}, {97, 4}, {56, 8}, {125, 64}, {126, 64}, {100, 128}};
        unsigned long mods = 0;
        for (long i = 0; i < 8; i++)
                if (wayland_key_down(map[i][0])) mods |= map[i][1];
        return mods;
}

static void wayland_modifiers_to(struct wayland_client *c, long slot)
{
        wayland_ev(c, c->objects[slot].id, WLE_wl_keyboard_modifiers, (unsigned int)wayland_serial(c),
                   (unsigned int)wayland.mods[0], (unsigned int)wayland.mods[1], (unsigned int)wayland.mods[2], (unsigned int)wayland.mods[3]);
}

static void wayland_keyboard_enter_one(struct wayland_client *c, long slot, struct wayland_surface *s, unsigned long serial)
{
        struct wayland_event e = wayland_begin(c, c->objects[slot].id, WLE_wl_keyboard_enter);
        long count = 0;

        wayland_word(&e, serial), wayland_word(&e, s->id);
        for (long key = 0; key < 128; key++) count += wayland_key_down(key);
        wayland_word(&e, (unsigned long)count * 4);
        for (long key = 0; key < 128; key++)
                if (wayland_key_down(key)) wayland_word(&e, (unsigned long)key);
        wayland_end(&e), wayland_modifiers_to(c, slot);
}

static void wayland_selections_to(struct wayland_client *c);
static void wayland_shortcuts_send(struct wayland_client *c, struct wayland_surface *s, int active);

WAYLAND_API void wayland_keyboard_to(long row)
{
        struct wayland_client *old = 0, *now = 0;
        if (!wayland.host || row == wayland.keyboard_row) return;
        struct wayland_surface *was = wayland_row_surface(wayland.keyboard_row, &old), *is = wayland_row_surface(row, &now);

        if (was)
                wayland_seat_ev(old, WLI_wl_keyboard, WLE_wl_keyboard_leave, 0, (unsigned int)wayland_serial(old), was->id),
                wayland_texts_focus(old, wayland.keyboard_row, was->id, 0);
        wayland.keyboard_row = is ? row : -1;
        if (was && old && (was->shortcuts || was->xgrab) && was->shortcut_active) wayland_shortcuts_send(old, was, 0);
        if (is && now && (is->shortcuts || is->xgrab) && !is->shortcut_active) wayland_shortcuts_send(now, is, 1);
        wayland_constraints_update();
        if (is)
        {
                unsigned long serial = wayland_input_serial(now);
                wayland.mods[0] = wayland_depressed();
                wayland.mods[1] = wayland.mods[3] = 0;
                wayland.mods[2] = (unsigned long)wayland.host->locks(wayland.host->context);
                for (long at = 0, slot; (slot = wayland_seat_next(now, WLI_wl_keyboard, &at)) >= 0;)
                        wayland_keyboard_enter_one(now, slot, is, serial);
                wayland_selections_to(now), wayland_texts_focus(now, row, is->id, 1);
        }
}

WAYLAND_API void wayland_key(long row, long code, long pressed)
{
        struct wayland_client *c;
        if (!wayland.host || code < 1 || code > 0x2ff) return;
        if (code == 1 && wayland_dragging()) { if (pressed) wayland_drag_cancel(); return; }
        if (code < 128)
                wayland.keys[code >> 3] = pressed ? (unsigned char)(wayland.keys[code >> 3] | (1u << (code & 7)))
                                                  : (unsigned char)(wayland.keys[code >> 3] & ~(1u << (code & 7)));
        if (row != wayland.keyboard_row || !wayland_row_surface(row, &c)) return;
        wayland_seat_ev(c, WLI_wl_keyboard, WLE_wl_keyboard_key, 0, (unsigned int)wayland_input_serial(c), (unsigned int)wayland_now(),
                        (unsigned int)code, pressed ? WLN_wl_keyboard_key_state_pressed : WLN_wl_keyboard_key_state_released);
        unsigned long depressed = wayland_depressed(), locked = (unsigned long)wayland.host->locks(wayland.host->context);
        if (depressed != wayland.mods[0] || locked != wayland.mods[2])
        {
                wayland.mods[0] = depressed, wayland.mods[2] = locked;
                for (long at = 0, slot; (slot = wayland_seat_next(c, WLI_wl_keyboard, &at)) >= 0;)
                        wayland_modifiers_to(c, slot);
        }
}

WAYLAND_API void wayland_key_shift(long row, long code, long pressed, long shift)
{
        if (!wayland.host) return;
        if (pressed && shift && code != 42 && code != 54 && code >= 1 && code < 128 && !wayland.synth_code &&
            !wayland_key_down(42) && !wayland_key_down(54))
                wayland_key(row, 42, 1), wayland.synth_code = (unsigned long)code;
        wayland_key(row, code, pressed);
        if (!pressed && wayland.synth_code == (unsigned long)code)
                wayland_key(row, 42, 0), wayland.synth_code = 0;
}

static void wayland_keyboards_update(int keymap)
{
        for (long i = 0; wayland.host && i < wayland.client_slots; i++)
                for (long at = 0, slot; wayland.client[i] && (slot = wayland_seat_next(wayland.client[i], WLI_wl_keyboard, &at)) >= 0;)
                        if (keymap) wayland_keyboard_keymap(wayland.client[i], slot), wayland_modifiers_to(wayland.client[i], slot);
                        else wayland_keyboard_repeat(wayland.client[i], slot);
}

WAYLAND_API void wayland_repeat_changed(void) { wayland_keyboards_update(0); }
WAYLAND_API void wayland_keymap_changed(void) { wayland_keyboards_update(1); }

static int wayland_seat_request(struct wayland_client *c, struct wayland_object *self, long op, struct wayland_arg *a)
{
        static const short ifaces[3] = {WLI_wl_pointer, WLI_wl_keyboard, WLI_wl_touch};
        if (op < 0 || op > 2) return WAYLAND_OK;
        long slot = a[0].slot;
        if (op == WLQ_wl_seat_get_touch && !(wayland.host->seat(wayland.host->context) & WLN_wl_seat_capability_touch))
                return wayland_fail(c, self->id, WLN_wl_seat_error_missing_capability, "the seat has no touch");
        if (!wayland_ids_add(wayland_seat_ids(c, ifaces[op]), c->objects[slot].id))
                return wayland_fail(c, self->id, WAYLAND_NO_MEMORY, "too many seat objects");
        wayland.seat_gen++;
        struct wayland_client *owner;
        struct wayland_surface *s;
        if (op == WLQ_wl_seat_get_pointer && wayland.pointer_surface >= 0 &&
            (s = wayland_row_surface(wayland.pointer_row, &owner)) && owner == c && c->surfaces[wayland.pointer_surface].live)
        {
                long sx = 0, sy = 0;
                unsigned long serial = wayland_input_serial(c);
                c->pointer_enter = (unsigned int)serial, s = wayland_pick(c, s, wayland.pointer_x, wayland.pointer_y, &sx, &sy);
                wayland_ev(c, c->objects[slot].id, WLE_wl_pointer_enter, (unsigned int)serial, s->id,
                           (unsigned int)wayland_units_fixed(sx, s->scale120), (unsigned int)wayland_units_fixed(sy, s->scale120));
                wayland_pointer_frame(c, slot);
        }
        else if (op == WLQ_wl_seat_get_keyboard)
        {
                wayland_keyboard_keymap(c, slot);
                if ((s = wayland_row_surface(wayland.keyboard_row, &owner)) && owner == c)
                        wayland_keyboard_enter_one(c, slot, s, wayland_serial(c));
        }
        return WAYLAND_OK;
}

static int wayland_seat_object_request(struct wayland_client *c, struct wayland_object *self, long op, struct wayland_arg *a)
{
        if (self->iface == WLI_wl_pointer && op == WLQ_wl_pointer_set_cursor)
        {
                struct wayland_surface *named = a[1].slot >= 0 ? wayland_surface_of(c, a[1].slot) : 0;
                if (a[1].slot >= 0)
                {
                        if (!named || (named->role != WAYLAND_ROLE_NONE && named->role != WAYLAND_ROLE_CURSOR) ||
                            (named->flags & WAYLAND_SURFACE_XDG) || named->xsurface)
                                return wayland_fail(c, self->id, WLN_wl_pointer_error_role, "the cursor's surface has another role");
                        named->role = WAYLAND_ROLE_CURSOR;
                }
                if (wayland.pointer_row >= 0 && wayland.row[wayland.pointer_row].client == c->slot && wayland_pointer_serial_ok(c, a[0].u))
                {
                        wayland_row_do(wayland.pointer_row, WAYLAND_DO_CURSOR, (named && named->held) ? WAYLAND_CURSOR_ARROW : WAYLAND_CURSOR_HIDDEN, 0);
                        wayland.cursor_client = named ? c->slot : -1, wayland.cursor_surface = named ? wayland_surface_index(c, named) : -1;
                        wayland.cursor_gen = named ? named->gen : 0;
                        wayland.cursor_hot_x = wayland_clamp(a[2].i, -WAYLAND_DIMENSION, WAYLAND_DIMENSION);
                        wayland.cursor_hot_y = wayland_clamp(a[3].i, -WAYLAND_DIMENSION, WAYLAND_DIMENSION);
                        wayland.cursor_serial++;
                }
        }
        else if (op == (self->iface == WLI_wl_pointer ? WLQ_wl_pointer_release
                        : self->iface == WLI_wl_keyboard ? WLQ_wl_keyboard_release : WLQ_wl_touch_release))
                wayland_ids_remove(wayland_seat_ids(c, self->iface), self->id), wayland.seat_gen++;
        return WAYLAND_OK;
}

/* ---- clipboard and drag-and-drop ---- */

static inline long wayland_device_next(struct wayland_client *c, long which, long *cursor) { return wayland_ids_next(c, &c->device_ids[which], which ? WLI_zwp_primary_selection_device_v1 : WLI_wl_data_device, cursor); }
static inline int wayland_has_keys(const struct wayland_client *c) { return wayland.keyboard_row >= 0 && wayland.row[wayland.keyboard_row].client == c->slot; }

static unsigned long wayland_server_id(struct wayland_client *c)
{
        long n = c->tbl[WLT_SERVER].count;

        if (2 * (c->server_live + 1) > n)
        {
                long want = n ? 2 * n : 64;
                if ((unsigned long)want <= WAYLAND_SERVER_IDS && wayland_tbl_reach(c, WLT_SERVER, want)) n = c->tbl[WLT_SERVER].count;
                else if (c->server_live >= n) return 0;
        }
        for (long step = 0; step < n; step++)
        {
                long at = (c->server_hint + step) % n;
                if (!c->server_index[at]) return c->server_hint = (at + 1) % n, WAYLAND_SERVER_BASE + (unsigned long)at;
        }
        return 0;
}

static int wayland_mime_buf_append(struct wayland_table *tbl, long *bytes, const unsigned char *text, long len)
{
        if (!text || len < 1 || *bytes + len + 1 > WAYLAND_MIME_BYTES || (tbl->unit = 1, !wayland_table_reach(tbl, *bytes + len + 1)))
                return 0;
        wayland_copy(tbl->at + *bytes, text, len), tbl->at[*bytes + len] = 0, *bytes += len + 1;
        return 1;
}

static const char *wayland_mime_buf_nth(const unsigned char *buf, long bytes, long nth)
{
        for (long at = 0; buf && at < bytes;)
        {
                const char *type = (const char *)buf + at;
                long len = 0;
                while (at + len < bytes && type[len]) len++;
                if (!nth--) return type;
                at += len + 1;
        }
        return 0;
}

/* The store's owner of a selection and its change count (wayland_host.c): the owning Wayland client's slot, or -1 for native or none. */
static long compositor_wl_selection_query(void *context, long which, unsigned long *serial);
static long wayland_selection_owner(long which, unsigned long *serial)
{
        return wayland.host ? compositor_wl_selection_query(wayland.host->context, which, serial) : -1;
}

static unsigned long wayland_selection_serial(long which)
{
        unsigned long serial = 0;

        (void)wayland_selection_owner(which, &serial);
        return serial;
}

static unsigned long wayland_offer_create_and_send(struct wayland_client *c, long device_slot, long which, int is_drag,
                                                   const unsigned char *types, long type_bytes, unsigned int src_actions, long *out_index)
{
        long index = wayland_tbl_alloc(c, WLT_OFFER);
        unsigned long id = index >= 0 ? wayland_server_id(c) : 0;
        long offer = id ? wayland_object_new(c, id, which ? WLI_zwp_primary_selection_offer_v1 : WLI_wl_data_offer, c->objects[device_slot].version) : -1;

        if (out_index) *out_index = -1;
        if (offer < 0)
                return index >= 0 ? (wayland_tbl_free(c, WLT_OFFER, index), 0UL) : 0UL;
        c->offers[index] = (struct wayland_offer){.id = (unsigned int)id, .live = 1, .which = (unsigned char)which,
                                                  .serial = is_drag ? 0 : wayland_selection_serial(which), .drag = (unsigned char)is_drag};
        c->objects[offer].link = (int)index;
        if (out_index) *out_index = index;
        wayland_ev(c, c->objects[device_slot].id, which ? WLE_zwp_primary_selection_device_v1_data_offer : WLE_wl_data_device_data_offer, (unsigned int)id);
        for (long nth = 0;; nth++)
        {
                const char *type = is_drag ? wayland_mime_buf_nth(types, type_bytes, nth) : wayland.host->clip_type(wayland.host->context, which, nth);
                if (!type) break;
                wayland_ev_str(c, id, which ? WLE_zwp_primary_selection_offer_v1_offer : WLE_wl_data_offer_offer, type);
        }
        if (is_drag && c->objects[offer].version >= 3)
                wayland_ev(c, id, WLE_wl_data_offer_source_actions, src_actions);
        return id;
}

static int wayland_source_send_fd(struct wayland_client *c, const struct wayland_source *s, const char *mime, long fd, int from_host)
{
        if (!c || c->dead || !s || !mime || fd < 0 || wayland_slot_of(c, s->id) < 0 || !wayland_out_fd_room(c))
                return 0;
        wayland_ev_fd(c, s->id, s->which ? WLE_zwp_primary_selection_source_v1_send : WLE_wl_data_source_send, mime, fd, 1);
        if (from_host) wayland.fds++;
        return 1;
}

static void wayland_selection_to(struct wayland_client *c, long slot, long which)
{
        unsigned long id = wayland.host->clip_type(wayland.host->context, which, 0) ? wayland_offer_create_and_send(c, slot, which, 0, 0, 0, 0, 0) : 0;
        wayland_ev(c, c->objects[slot].id, which ? WLE_zwp_primary_selection_device_v1_selection : WLE_wl_data_device_selection, (unsigned int)id);
}

static void wayland_selection_client(struct wayland_client *c, long which)
{
        for (long at = 0, slot; (slot = wayland_device_next(c, which, &at)) >= 0;)
                wayland_selection_to(c, slot, which);
}

static void wayland_selections_to(struct wayland_client *c) { wayland_selection_client(c, 0), wayland_selection_client(c, 1); }

static void wayland_source_cancel(struct wayland_client *c, long index)
{
        struct wayland_source *s = &c->sources[index];
        s->state = 3;
        if (wayland_slot_of(c, s->id) >= 0)
                wayland_ev0(c, s->id, s->which ? WLE_zwp_primary_selection_source_v1_cancelled : WLE_wl_data_source_cancelled);
}

static void wayland_drag_end(int how);

static void wayland_source_drop(struct wayland_client *c, long index)
{
        struct wayland_source *s = &c->sources[index];
        long which = s->which;

        if (!s->live) return;
        if (s->state == 1 && c->pending[which] == index + 1)
                c->pending[which] = 0, wayland.host->clip_abort(wayland.host->context, c->slot, which);
        if (wayland.drag.live && wayland.drag.client == c->slot && wayland.drag.source == index)
                wayland_drag_end(0);
        wayland_table_drop(&s->mime_table), s->mime_bytes = 0, s->live = 0, wayland_tbl_free(c, WLT_SOURCE, index);
}

WAYLAND_API void wayland_selection_changed(long which)
{
        struct wayland_client *c;
        if (!wayland.host || which < 0 || which > 1) return;
        /* The store changed hands (the observer runs before the pull that follows an install): every pulled source of this selection
           (state 2) has lost it. A pending one (state 1) has not been pulled yet, so it is left alone. */
        for (long slot = 0; slot < wayland.client_slots; slot++)
                if ((c = wayland.client[slot]))
                        for (long at = 0; at < c->tbl[WLT_SOURCE].count; at++)
                                if (c->sources[at].live && c->sources[at].which == which && c->sources[at].state == 2)
                                        wayland_source_cancel(c, at);
        if (wayland.keyboard_row >= 0 && wayland.row[wayland.keyboard_row].client >= 0 && (c = wayland.client[wayland.row[wayland.keyboard_row].client]))
                wayland_selection_client(c, which);
}

static struct wayland_source *wayland_clip_pending(long client, long which, struct wayland_client **owner)
{
        struct wayland_client *c = wayland_live_client(client);
        long index = c && which >= 0 && which < 2 ? c->pending[which] - 1 : -1;
        return index >= 0 && index < c->tbl[WLT_SOURCE].count && c->sources[index].live && c->sources[index].state == 1
                   ? (*owner = c, &c->sources[index]) : 0;
}

WAYLAND_API const char *wayland_clip_type(long client, long which, long nth)
{
        struct wayland_client *c = 0;
        struct wayland_source *s = wayland_clip_pending(client, which, &c);
        return s ? wayland_mime_buf_nth(s->mime_table.at, s->mime_bytes, nth) : 0;
}

WAYLAND_API int wayland_clip_send(long client, long which, long nth, long fd)
{
        struct wayland_client *c = 0;
        struct wayland_source *s = wayland_clip_pending(client, which, &c);
        if (wayland_source_send_fd(c, s, s ? wayland_mime_buf_nth(s->mime_table.at, s->mime_bytes, nth) : 0, fd, 1))
                return 1;
        if (fd >= 0 && wayland.host && wayland.host->close)
                wayland.host->close(wayland.host->context, fd);
        return 0;
}

WAYLAND_API void wayland_clip_pulled(long client, long which, long ok)
{
        struct wayland_client *c = 0;
        struct wayland_source *s = wayland_clip_pending(client, which, &c);
        if (!s) return;
        long index = c->pending[which] - 1;
        c->pending[which] = 0;
        if (!ok) { wayland_source_cancel(c, index); return; }
        s->state = 2; /* the store holds the owner now: the observer ran before this */
}

static int wayland_set_selection(struct wayland_client *c, struct wayland_object *self, long which, const struct wayland_arg *a)
{
        long index = a[0].slot >= 0 ? c->objects[a[0].slot].link : -1;
        struct wayland_source *s;

        if (index < 0)
        {
                if (wayland_selection_owner(which, 0) == c->slot) wayland.host->clip_clear(wayland.host->context, c->slot, which);
                return WAYLAND_OK;
        }
        s = &c->sources[index];
        if (s->state == 1 || s->state == 2) return WAYLAND_OK;
        if (s->state != 0) return wayland_fail(c, self->id, WLN_wl_data_device_error_used_source, "the source was used before");
        if (!wayland_input_serial_ok(c, a[1].u) || !s->mime_bytes) return wayland_source_cancel(c, index), WAYLAND_OK;
        if (c->pending[which])
                wayland.host->clip_abort(wayland.host->context, c->slot, which), wayland_source_cancel(c, c->pending[which] - 1), c->pending[which] = 0;
        s->state = 1, c->pending[which] = index + 1;
        if (!wayland.host->clip_pull(wayland.host->context, c->slot, which) && c->pending[which] == index + 1)
                c->pending[which] = 0, wayland_source_cancel(c, index);
        return WAYLAND_OK;
}

static void wayland_drag_decide(void);
static inline long wayland_obj_ver(struct wayland_client *c, unsigned long id) { long s = c ? wayland_slot_of(c, id) : -1; return s >= 0 ? (long)c->objects[s].version : -1; }
static inline struct wayland_client *wayland_drag_cli(long slot) { return wayland.drag.live ? wayland_client_of(slot) : 0; }
static inline long wayland_drag_device(struct wayland_client *c) { long at = 0; return wayland_device_next(c, 0, &at); }
static inline int wayland_drag_is_offer(struct wayland_client *c, long o) { return wayland.drag.live && wayland.drag.target == c->slot && wayland.drag.offer == o; }
static inline unsigned int wayland_drag_pick(unsigned int m, unsigned int p) { return (m & p) ? p : (m & 1u) ? 1u : (m & 2u) ? 2u : (m & 4u) ? 4u : 0; }
static inline long wayland_xdrag(long what, long row, long x, long y) { return wayland.host && wayland.host->xdrag ? wayland.host->xdrag(wayland.host->context, what, row, x, y) : 0; }

static struct wayland_source *wayland_drag_src(struct wayland_client **cc)
{
        struct wayland_client *c = wayland_drag_cli(wayland.drag.client);
        if (cc) *cc = c;
        return c && wayland.drag.source >= 0 && wayland.drag.source < c->tbl[WLT_SOURCE].count && c->sources[wayland.drag.source].live
                   ? &c->sources[wayland.drag.source] : 0;
}

static struct wayland_source *wayland_drag_source(void) { return wayland_drag_src(0); }

static struct wayland_offer *wayland_drag_offer(struct wayland_client **tc, long *ver)
{
        struct wayland_client *t = wayland_drag_cli(wayland.drag.target);
        struct wayland_offer *o = t && wayland.drag.offer >= 0 && wayland.drag.offer < t->tbl[WLT_OFFER].count ? &t->offers[wayland.drag.offer] : 0;
        if (tc) *tc = t;
        if (ver) *ver = o ? wayland_obj_ver(t, o->id) : -1;
        return o;
}

static void wayland_drag_clear(void)
{
        memory_zero(&wayland.drag, sizeof wayland.drag);
        wayland.drag.client = wayland.drag.source = wayland.drag.origin = wayland.drag.target = wayland.drag.surface = wayland.drag.offer = wayland.drag.xtarget = -1;
}

static void wayland_drag_tell_source(long op, unsigned int word)
{
        struct wayland_client *c;
        struct wayland_source *s = wayland_drag_src(&c);
        if (!s || wayland_obj_ver(c, s->id) < (op == WLE_wl_data_source_cancelled ? 1 : 3)) return;
        if (op == WLE_wl_data_source_action) wayland_ev(c, s->id, op, word);
        else wayland_ev0(c, s->id, op);
}

static void wayland_drag_send_target(const char *type)
{
        struct wayland_client *c;
        struct wayland_source *s = wayland_drag_src(&c);
        if (s && wayland_obj_ver(c, s->id) >= 0) wayland_ev_str(c, s->id, WLE_wl_data_source_target, type);
}

static void wayland_drag_xleave(void)
{
        if (wayland.drag.xtarget < 0) return;
        wayland_xdrag(WAYLAND_XDRAG_LEAVE, wayland.drag.xtarget, 0, 0), wayland.drag.xtarget = -1;
        if (wayland.drag.x_accepted) wayland_drag_send_target(0);
        wayland.drag.x_accepted = 0, wayland.drag.action = 0;
}

static void wayland_drag_actions(void)
{
        struct wayland_client *t;
        long ver;
        struct wayland_offer *o = wayland_drag_offer(&t, &ver);
        struct wayland_source *s = wayland_drag_source();
        if (!o) return;
        unsigned int action = wayland_drag_pick((s ? s->actions : wayland.drag.xsource ? wayland.drag.x_actions : WAYLAND_DRAG_ACTIONS) & o->actions, o->preferred);
        if (action == wayland.drag.action) return;
        wayland.drag.action = action, wayland_drag_tell_source(WLE_wl_data_source_action, action);
        if (ver >= 3) wayland_ev(t, o->id, WLE_wl_data_offer_action, action);
        if (wayland.drag.released && o->accepted && action) wayland_drag_decide();
}

static void wayland_drag_leave(void)
{
        struct wayland_client *t;
        struct wayland_offer *o = wayland_drag_offer(&t, 0);
        long device = t ? wayland_drag_device(t) : -1;

        if (device >= 0) wayland_ev0(t, t->objects[device].id, WLE_wl_data_device_leave);
        if (o) o->drag = 0;
        if (t && wayland.drag.target != wayland.drag.client) wayland_drag_send_target(0);
        wayland.drag.target = wayland.drag.surface = wayland.drag.offer = -1, wayland.drag.action = 0;
}

static void wayland_drag_end(int how)
{
        unsigned int action = wayland.drag.action;

        if (!wayland.drag.live) return;
        if (!wayland.drag.wl_drop_sent)
        {
                if (wayland_drag_cli(wayland.drag.target)) wayland_drag_leave();
                wayland_drag_xleave();
        }
        if (how == 0 || how == 2)
                wayland_drag_tell_source(how ? WLE_wl_data_source_dnd_finished : WLE_wl_data_source_cancelled, 0);
        if (wayland.drag.xsource) wayland_table_drop(&wayland.drag.x_types);
        wayland_drag_clear();
        wayland_xdrag(WAYLAND_XDRAG_END, -1, how == 2, (long)action);
}

static void wayland_drag_over(long row, long x, long y)
{
        struct wayland_client *t = 0, *c = wayland_drag_cli(wayland.drag.client);
        struct wayland_surface *root = row >= 0 ? wayland_row_surface(row, &t) : 0, *is = 0;
        struct wayland_source *source = wayland_drag_source();
        long sx = 0, sy = 0, device = -1, index = -1;
        unsigned long id = 0;

        if ((!c && !wayland.drag.xsource) || wayland.drag.wl_drop_sent) return;
        if (root) is = wayland_pick(t, root, x, y, &sx, &sy);
        if (is && wayland.drag.source < 0 && !wayland.drag.xsource && t != c) is = 0;
        if (is) device = wayland_drag_device(t);
        if ((is && device < 0) || (row >= 0 && !root))
        {
                if ((source || wayland.drag.xsource) && wayland.host->xdrag)
                {
                        if (wayland.drag.xtarget == row) return (void)wayland_xdrag(WAYLAND_XDRAG_OVER, row, x, y);
                        if (wayland_drag_cli(wayland.drag.target)) wayland_drag_leave();
                        wayland_drag_xleave();
                        if (wayland_xdrag(WAYLAND_XDRAG_OVER, row, x, y) == 1)
                                wayland.drag.xtarget = row, wayland.drag.x_accepted = 0, wayland.drag.action = 0, wayland.drag.x_seen = 1;
                        return;
                }
                is = 0;
        }
        if (!is || wayland.drag.xtarget != row) wayland_drag_xleave();
        if (is && wayland.drag.target == t->slot && wayland.drag.surface == wayland_surface_index(t, is))
        {
                wayland_ev(t, t->objects[device].id, WLE_wl_data_device_motion, (unsigned int)wayland_now(),
                           (unsigned int)wayland_units_fixed(sx, is->scale120), (unsigned int)wayland_units_fixed(sy, is->scale120));
                return;
        }
        if (wayland_drag_cli(wayland.drag.target)) wayland_drag_leave();
        if (!is) return;
        if (source || wayland.drag.xsource)
                if (!(id = wayland_offer_create_and_send(t, device, 0, 1, source ? source->mime_table.at : wayland.drag.x_types.at,
                                                         source ? source->mime_bytes : wayland.drag.x_bytes,
                                                         source ? source->actions : wayland.drag.x_actions, &index)))
                        return;
        wayland.drag.target = t->slot, wayland.drag.surface = wayland_surface_index(t, is), wayland.drag.offer = index;
        wayland_ev(t, t->objects[device].id, WLE_wl_data_device_enter, (unsigned int)wayland_input_serial(t), is->id,
                   (unsigned int)wayland_units_fixed(sx, is->scale120), (unsigned int)wayland_units_fixed(sy, is->scale120), (unsigned int)id);
}

static void wayland_drag_release(void)
{
        struct wayland_offer *o = wayland_drag_offer(0, 0);
        if (!wayland.drag.released &&
            (((wayland.drag.source >= 0 || wayland.drag.xsource) && o && !o->accepted) || (wayland.drag.xtarget >= 0 && !wayland.drag.x_accepted)))
                wayland.drag.released = 1, wayland.drag.released_at = wayland_now();
        else
                wayland_drag_decide();
}

static void wayland_drag_decide(void)
{
        struct wayland_client *t, *sc;
        long ver;
        struct wayland_offer *o = wayland_drag_offer(&t, &ver);

        if (wayland.drag.wl_drop_sent)
                return;
        wayland.drag.released = 0;
        if (wayland.drag.xtarget >= 0)
        {
                struct wayland_source *s = wayland_drag_src(&sc);
                if (!wayland.drag.x_accepted || (!wayland.drag.action && (wayland.drag.xsource || (s && wayland_obj_ver(sc, s->id) >= 3))))
                        return wayland_drag_end(0);
                wayland.drag.wl_drop_sent = 1, wayland_xdrag(WAYLAND_XDRAG_DROP, wayland.drag.xtarget, 0, 0);
        }
        else
        {
                int carry = wayland.drag.source >= 0 || wayland.drag.xsource;
                long device = t ? wayland_drag_device(t) : -1;
                if (device < 0 || (carry && (!o || !o->accepted || (ver >= 3 && !wayland.drag.action))))
                        return wayland_drag_end(0);
                wayland_ev0(t, t->objects[device].id, WLE_wl_data_device_drop), wayland.drag.wl_drop_sent = 1;
                if (!carry) return wayland_drag_end(1);
        }
        wayland_drag_tell_source(WLE_wl_data_source_dnd_drop_performed, 0);
}

static inline void wayland_drag_move(long row, long x, long y) { wayland.pointer_x = x, wayland.pointer_y = y; wayland_drag_over(row, x, y); }
WAYLAND_API int wayland_dragging(void) { return wayland.drag.live && !wayland.drag.wl_drop_sent && !wayland.drag.released && (!wayland.drag.xsource || wayland.drag.held); }
WAYLAND_API int wayland_x_dragging(void) { return wayland.drag.live && wayland.drag.xsource && !wayland.drag.held && !wayland.drag.wl_drop_sent; }
WAYLAND_API void wayland_drag_x_finished(long ok) { if (wayland.drag.live && wayland.drag.wl_drop_sent && wayland.drag.xtarget >= 0) wayland_drag_end(ok ? 2 : 0); }
WAYLAND_API void wayland_drag_host_release(void) { if (wayland.drag.held && wayland_dragging()) wayland_drag_release(); }
WAYLAND_API void wayland_drag_x_motion(long row, long x, long y) { if (wayland_x_dragging() && !wayland.drag.released) wayland_drag_move(row, x, y); }
WAYLAND_API void wayland_drag_x_leave(void) { if (wayland_x_dragging()) wayland_drag_end(0); }
WAYLAND_API void wayland_drag_x_drop(void) { if (wayland_x_dragging() && !wayland.drag.released) wayland_drag_release(); }
WAYLAND_API void wayland_drag_cancel(void) { if (wayland_dragging()) wayland_drag_end(0); }
WAYLAND_API void wayland_drag_motion(long row, long x, long y) { if (wayland_dragging()) wayland_drag_move(row, x, y); }

WAYLAND_API long wayland_drag_x_types(const char **types, unsigned int *actions)
{
        struct wayland_source *s = wayland_drag_source();
        int xs = wayland.drag.live && wayland.drag.xsource && wayland.drag.x_bytes;
        if (!xs && (!wayland.drag.live || !s || !s->mime_bytes)) return 0;
        *types = (const char *)(xs ? wayland.drag.x_types.at : s->mime_table.at);
        if (actions) *actions = xs ? wayland.drag.x_actions : (s->actions ? s->actions : 1u);
        return xs ? wayland.drag.x_bytes : s->mime_bytes;
}

WAYLAND_API void wayland_drag_x_status(long accepted, unsigned long action)
{
        struct wayland_source *s = wayland_drag_source();
        if (!wayland.drag.live || wayland.drag.xtarget < 0 || wayland.drag.wl_drop_sent || (!s && !wayland.drag.xsource)) return;
        action = accepted ? wayland_drag_pick((unsigned int)action & (s ? (s->actions ? s->actions : WAYLAND_DRAG_ACTIONS) : wayland.drag.x_actions), 0) : 0;
        if (!!accepted != wayland.drag.x_accepted)
                wayland.drag.x_accepted = !!accepted, wayland_drag_send_target(accepted && s && s->mime_bytes ? (const char *)s->mime_table.at : 0);
        if (action != wayland.drag.action)
                wayland.drag.action = (unsigned int)action, wayland_drag_tell_source(WLE_wl_data_source_action, (unsigned int)action);
        if (wayland.drag.released) wayland_drag_decide();
}

WAYLAND_API int wayland_drag_x_send(const char *mime, long fd)
{
        struct wayland_client *sc;
        struct wayland_source *s = wayland_drag_src(&sc);
        return (wayland.drag.live && (wayland.drag.wl_drop_sent || wayland.drag.xtarget >= 0)) ? wayland_source_send_fd(sc, s, mime, fd, 1) : 0;
}

WAYLAND_API int wayland_drag_x_begin(const char *types, long bytes, unsigned long actions)
{
        if (!wayland.host || wayland.drag.live || !types || bytes <= 0) return 0;
        wayland_drag_clear();
        if (!wayland_mime_buf_append(&wayland.drag.x_types, &wayland.drag.x_bytes, (const unsigned char *)types, bytes)) return 0;
        wayland.drag.x_bytes = bytes, wayland.drag.live = wayland.drag.xsource = 1;
        wayland.drag.x_actions = (unsigned int)(actions & WAYLAND_DRAG_ACTIONS ? actions & WAYLAND_DRAG_ACTIONS : 1u);
        return 1;
}

WAYLAND_API void wayland_drag_x_actions(unsigned long actions)
{
        struct wayland_client *t;
        long ver;
        struct wayland_offer *o = wayland_drag_offer(&t, &ver);

        actions &= WAYLAND_DRAG_ACTIONS;
        if (!wayland.drag.live || !wayland.drag.xsource || wayland.drag.wl_drop_sent || !actions || actions == wayland.drag.x_actions) return;
        wayland.drag.x_actions = (unsigned int)actions;
        if (ver >= 3) wayland_ev(t, o->id, WLE_wl_data_offer_source_actions, (unsigned int)actions);
        wayland_drag_actions();
}

WAYLAND_API int wayland_drag_host_begin(const char *types, long bytes, unsigned long actions, long row, long x, long y)
{
        if (!wayland_drag_x_begin(types, bytes, actions)) return 0;
        return wayland.drag.held = 1, wayland_drag_move(row, x, y), 1;
}

WAYLAND_API int wayland_drag_x_answer(unsigned long *action)
{
        long ver;
        struct wayland_offer *o = wayland_drag_offer(0, &ver);
        *action = 0;
        if (!wayland.drag.live || !wayland.drag.xsource || !o || !o->accepted) return 0;
        *action = wayland.drag.action ? wayland.drag.action : (ver >= 0 && ver < 3 ? 1u : 0);
        return *action != 0;
}

static int wayland_data_request(struct wayland_client *c, struct wayland_object *self, long op, struct wayland_arg *a)
{
        long which;

        switch (self->iface)
        {
        case WLI_wl_data_device_manager:
        case WLI_zwp_primary_selection_device_manager_v1:
                which = self->iface == WLI_zwp_primary_selection_device_manager_v1;
                if (op == WLQ_wl_data_device_manager_create_data_source)
                {
                        long idx = wayland_tbl_alloc(c, WLT_SOURCE);
                        if (idx < 0) return wayland_fail(c, self->id, WAYLAND_NO_MEMORY, "no memory for a data source");
                        c->sources[idx].id = c->objects[a[0].slot].id, c->sources[idx].live = 1, c->sources[idx].which = (unsigned char)which;
                        c->objects[a[0].slot].link = (int)idx;
                }
                else if (op == WLQ_wl_data_device_manager_get_data_device)
                {
                        if (!wayland_ids_add(&c->device_ids[which], c->objects[a[0].slot].id))
                                return wayland_fail(c, self->id, WAYLAND_NO_MEMORY, "no memory for a data device");
                        if (wayland_has_keys(c)) wayland_selection_to(c, a[0].slot, which);
                }
                return WAYLAND_OK;
        case WLI_wl_data_source:
        case WLI_zwp_primary_selection_source_v1:
        {
                struct wayland_source *s = &c->sources[self->link];
                if (op == WLQ_wl_data_source_offer && s->state == 0)
                        wayland_mime_buf_append(&s->mime_table, &s->mime_bytes, a[0].text, a[0].length);
                else if (op == WLQ_wl_data_source_set_actions)
                {
                        if (a[0].u & ~7UL) return wayland_fail(c, self->id, WLN_wl_data_source_error_invalid_action_mask, "the actions are not a mask of three");
                        s->actions = (unsigned int)a[0].u;
                }
                else if (op == WLQ_wl_data_source_destroy)
                        wayland_source_drop(c, self->link);
                return WAYLAND_OK;
        }
        case WLI_wl_data_device:
        case WLI_zwp_primary_selection_device_v1:
                which = self->iface == WLI_zwp_primary_selection_device_v1;
                if (op == (which ? WLQ_zwp_primary_selection_device_v1_set_selection : WLQ_wl_data_device_set_selection))
                        return wayland_set_selection(c, self, which, a);
                if (!which && op == WLQ_wl_data_device_start_drag)
                {
                        long source = -1;
                        if (a[0].slot >= 0 && c->objects[a[0].slot].iface == WLI_wl_data_source)
                        {
                                source = c->objects[a[0].slot].link;
                                if (source < 0 || source >= c->tbl[WLT_SOURCE].count || c->sources[source].state != 0)
                                        return wayland_fail(c, c->objects[a[0].slot].id, WLN_wl_data_device_error_used_source, "the source was used before");
                        }
                        if (!wayland.buttons || (unsigned int)a[3].u != c->press_serial || !wayland_surface_of(c, a[1].slot) || wayland.drag.live)
                        {
                                if (source >= 0) wayland_source_cancel(c, source);
                                return WAYLAND_OK;
                        }
                        if (source >= 0) c->sources[source].state = 4;
                        wayland_drag_clear();
                        wayland.drag.live = 1, wayland.drag.client = c->slot, wayland.drag.source = source, wayland.drag.origin = c->objects[a[1].slot].link;
                        wayland_drag_over(wayland.pointer_row, wayland.pointer_x, wayland.pointer_y);
                        return WAYLAND_OK;
                }
                return wayland_ids_remove(&c->device_ids[which], self->id), WAYLAND_OK;
        case WLI_wl_data_offer:
        case WLI_zwp_primary_selection_offer_v1:
        {
                struct wayland_offer *o = &c->offers[self->link];
                long pri = self->iface == WLI_zwp_primary_selection_offer_v1;
                if (op == (pri ? WLQ_zwp_primary_selection_offer_v1_receive : WLQ_wl_data_offer_receive))
                {
                        if (o->drag)
                        {
                                struct wayland_client *sc;
                                struct wayland_source *s = wayland_drag_src(&sc);
                                if (!wayland_drag_is_offer(c, self->link) || !a[0].text) return WAYLAND_OK;
                                if (wayland.drag.xsource && a[0].length > 0 && wayland.host->xreceive)
                                        wayland.host->xreceive(wayland.host->context, c->slot, 2, (const char *)a[0].text, a[1].fd), wayland.fds--, a[1].fd = -1;
                                else if (a[0].length > 1 && wayland_source_send_fd(sc, s, (const char *)a[0].text, a[1].fd, 0))
                                        a[1].fd = -1;
                        }
                        else if (o->serial == wayland_selection_serial(o->which) && wayland_has_keys(c) && a[0].text && a[0].length > 0)
                                wayland.host->clip_push(wayland.host->context, wayland.keyboard_row, o->which, (const char *)a[0].text, a[1].fd), wayland.fds--, a[1].fd = -1;
                        return WAYLAND_OK;
                }
                if (op == (pri ? WLQ_zwp_primary_selection_offer_v1_destroy : WLQ_wl_data_offer_destroy))
                {
                        if (o->live)
                        {
                                o->live = 0;
                                if (!pri && wayland_drag_is_offer(c, self->link))
                                {
                                        if (!wayland.drag.wl_drop_sent) wayland.drag.offer = -1;
                                        else
                                        {
                                                long ver = wayland_obj_ver(c, o->id);
                                                wayland_drag_end(o->finished || (ver >= 0 && ver < 3) ? 2 : 0);
                                        }
                                }
                                wayland_tbl_free(c, WLT_OFFER, self->link), self->link = -1;
                        }
                }
                else if (op == WLQ_wl_data_offer_finish)
                {
                        if (!o->drag || !wayland.drag.wl_drop_sent || !wayland_drag_is_offer(c, self->link))
                                return wayland_fail(c, self->id, WLN_wl_data_offer_error_invalid_finish, "finish is for a dropped drag's offer");
                        o->finished = 1, wayland_drag_end(2);
                }
                else if (op == WLQ_wl_data_offer_accept && o->drag)
                {
                        const char *type = a[1].length > 0 ? (const char *)a[1].text : 0;
                        if (wayland_drag_is_offer(c, self->link) && !wayland.drag.wl_drop_sent)
                        {
                                o->accepted = type != 0, wayland_drag_send_target(type);
                                if (wayland.drag.released)
                                {
                                        long ver = wayland_obj_ver(c, o->id);
                                        if (!type || wayland.drag.action || (ver >= 0 && ver < 3)) wayland_drag_decide();
                                }
                        }
                }
                else if (op == WLQ_wl_data_offer_set_actions && o->drag)
                {
                        if ((a[0].u & ~7UL) || (a[1].u & (a[1].u - 1)) || (a[1].u & ~7UL))
                                return wayland_fail(c, self->id, WLN_wl_data_offer_error_invalid_action_mask, "the actions are not a mask of three");
                        o->actions = (unsigned int)a[0].u, o->preferred = (unsigned int)a[1].u, wayland_drag_actions();
                }
                return WAYLAND_OK;
        }
        default: return WAYLAND_OK;
        }
}

/* ---- declarative surface-extension binding & property tables ---- */

struct wayland_ext_spec { unsigned char iface, bind_op, fail_dead, check_role, uniq_size; long uniq_off; unsigned int uniq_err; const char *uniq_msg; };

#define WL_EXT(i, o, fd, cr, ...) {WLI_##i, WLQ_##i##_##o, fd, cr, __VA_ARGS__}
static const struct wayland_ext_spec wayland_ext_specs[] = {
        WL_EXT(wp_viewporter, get_viewport, 0, 0, 4, WAYLAND_SOFF(viewport_id), WLN_wp_viewporter_error_viewport_exists, "the surface has a viewport"),
        WL_EXT(wp_fractional_scale_manager_v1, get_fractional_scale, 0, 0, 4, WAYLAND_SOFF(fractional_id), WLN_wp_fractional_scale_manager_v1_error_fractional_scale_exists, "the surface has a fractional scale"),
        WL_EXT(wp_linux_drm_syncobj_manager_v1, get_surface, 0, 0, 4, WAYLAND_SOFF(syncobj_id), WLN_wp_linux_drm_syncobj_manager_v1_error_surface_exists, "the surface already has a syncobj surface"),
        WL_EXT(xdg_wm_dialog_v1, get_xdg_dialog, 1, 1, 0, -1, 0, 0),
        WL_EXT(wp_content_type_manager_v1, get_surface_content_type, 1, 0, 0, -1, 0, 0),
        WL_EXT(wp_alpha_modifier_v1, get_surface, 1, 0, 0, -1, 0, 0),
        WL_EXT(wp_fifo_manager_v1, get_fifo, 1, 0, 0, -1, 0, 0),
        WL_EXT(wp_commit_timing_manager_v1, get_timer, 1, 0, 0, -1, 0, 0),
        WL_EXT(zwp_xwayland_keyboard_grab_manager_v1, grab_keyboard, 1, 0, 0, -1, 0, 0),
        WL_EXT(zwp_keyboard_shortcuts_inhibit_manager_v1, inhibit_shortcuts, 1, 0, 1, WAYLAND_SOFF(shortcuts), WLN_zwp_keyboard_shortcuts_inhibit_manager_v1_error_already_inhibited, "the surface has an inhibitor"),
        WL_EXT(wp_tearing_control_manager_v1, get_tearing_control, 1, 0, 1, WAYLAND_SOFF(tearing), WLN_wp_tearing_control_manager_v1_error_tearing_control_exists, "the surface has a tearing control"),
        WL_EXT(zwp_idle_inhibit_manager_v1, create_inhibitor, 1, 0, 0, -1, 0, 0),
};
#undef WL_EXT

static int wayland_ext_bind(struct wayland_client *c, struct wayland_object *self, long op, struct wayland_arg *a, struct wayland_surface **out_s)
{
        const struct wayland_ext_spec *sp = 0;
        struct wayland_surface *s;

        if (out_s) *out_s = 0;
        for (unsigned long i = 0; i < sizeof wayland_ext_specs / sizeof wayland_ext_specs[0] && !sp; i++)
                if (wayland_ext_specs[i].iface == self->iface) sp = &wayland_ext_specs[i];
        if (!sp || op != sp->bind_op) return WAYLAND_OK;
        s = wayland_surface_of(c, a[1].slot);
        if (s && sp->check_role && s->role_id != c->objects[a[1].slot].id) s = 0;
        if (!s)
                return sp->fail_dead ? wayland_fail(c, self->id, WAYLAND_INVALID_OBJECT, sp->check_role ? "no such toplevel" : "a surface is gone") : WAYLAND_OK;
        if (sp->uniq_size)
        {
                unsigned char *p = (unsigned char *)s + sp->uniq_off;
                if (sp->uniq_size == 4 ? *(unsigned int *)p : *p)
                        return wayland_fail(c, self->id, sp->uniq_err, sp->uniq_msg);
                if (sp->uniq_size == 4) *(unsigned int *)p = c->objects[a[0].slot].id;
                else *p = 1;
        }
        wayland_surface_link(c, a[0].slot, s);
        if (out_s) *out_s = s;
        return WAYLAND_OK;
}

/* Table-driven surface property mutations/clears (portable WAYLAND_SOFF, zero __builtin_offsetof). */
enum { WLOP_SET_CONST = 1, WLOP_SET_U32 = 2, WLOP_SET_CLAMP = 3 };
struct wayland_sprop_spec { unsigned char iface, op, mode, width; long off; unsigned int val; };

#define WL_SP(i, o, m, w, f, v) {WLI_##i, WLQ_##i##_##o, WLOP_##m, w, WAYLAND_SOFF(f), v}
static const struct wayland_sprop_spec wayland_sprop_specs[] = {
        WL_SP(wp_fractional_scale_v1, destroy, SET_CONST, 4, fractional_id, 0),
        WL_SP(xdg_dialog_v1, set_modal, SET_CONST, 1, modal, 1),
        WL_SP(xdg_dialog_v1, unset_modal, SET_CONST, 1, modal, 0),
        WL_SP(xdg_dialog_v1, destroy, SET_CONST, 1, modal, 0),
        WL_SP(wp_content_type_v1, set_content_type, SET_CLAMP, 4, content, WLN_wp_content_type_v1_type_game),
        WL_SP(wp_content_type_v1, destroy, SET_CONST, 4, content, WLN_wp_content_type_v1_type_none),
        WL_SP(wp_alpha_modifier_surface_v1, set_multiplier, SET_U32, 4, pending.alpha, 0),
        WL_SP(wp_alpha_modifier_surface_v1, destroy, SET_CONST, 4, pending.alpha, 0xffffffffu),
        WL_SP(wp_fifo_v1, wait_barrier, SET_CONST, 1, pending.fifo_wait, 1),
        WL_SP(wp_fifo_v1, set_barrier, SET_CONST, 1, pending.barrier, 1),
};
#undef WL_SP

static int wayland_sprop_apply(struct wayland_surface *s, long iface, long op, const struct wayland_arg *a)
{
        for (unsigned long i = 0; i < sizeof wayland_sprop_specs / sizeof wayland_sprop_specs[0]; i++)
        {
                const struct wayland_sprop_spec *sp = &wayland_sprop_specs[i];
                if (sp->iface != iface || sp->op != op) continue;
                unsigned char *p = (unsigned char *)s + sp->off;
                unsigned int v = sp->mode == WLOP_SET_CONST ? sp->val
                                 : sp->mode == WLOP_SET_U32 ? (unsigned int)a[0].u
                                 : (a[0].u <= sp->val ? (unsigned int)a[0].u : 0u);
                if (sp->width == 4) *(unsigned int *)p = v;
                else *p = (unsigned char)v;
                return 1;
        }
        return 0;
}

static int wayland_viewport_request(struct wayland_client *c, struct wayland_object *self, long op, struct wayland_arg *a)
{
        struct wayland_surface *s;

        if (self->iface == WLI_wp_viewporter) return wayland_ext_bind(c, self, op, a, 0);
        s = wayland_surface_of(c, self - c->objects);
        if (op == WLQ_wp_viewport_destroy)
                return s ? (s->viewport_id = 0, s->pending.dst_w = s->pending.dst_h = s->pending.src_w = s->pending.src_h = 0, WAYLAND_OK) : WAYLAND_OK;
        if (!s) return wayland_fail(c, self->id, WLN_wp_viewport_error_no_surface, "the surface is gone");
        if (op == WLQ_wp_viewport_set_source)
        {
                if (a[0].i == -256 && a[1].i == -256 && a[2].i == -256 && a[3].i == -256)
                        return s->pending.src_w = s->pending.src_h = 0, WAYLAND_OK;
                if (a[0].i < 0 || a[1].i < 0 || a[2].i <= 0 || a[3].i <= 0)
                        return wayland_fail(c, self->id, WLN_wp_viewport_error_bad_value, "the source is negative or empty");
                s->pending.src_x = a[0].i, s->pending.src_y = a[1].i, s->pending.src_w = a[2].i, s->pending.src_h = a[3].i;
        }
        else if (op == WLQ_wp_viewport_set_destination)
        {
                if (a[0].i == -1 && a[1].i == -1) return s->pending.dst_w = s->pending.dst_h = 0, WAYLAND_OK;
                if (a[0].i <= 0 || a[1].i <= 0) return wayland_fail(c, self->id, WLN_wp_viewport_error_bad_value, "the destination is empty");
                s->pending.dst_w = wayland_min(a[0].i, WAYLAND_DIMENSION), s->pending.dst_h = wayland_min(a[1].i, WAYLAND_DIMENSION);
        }
        return WAYLAND_OK;
}

static int wayland_cursor_shape_request(struct wayland_client *c, struct wayland_object *self, long op, struct wayland_arg *a)
{
        if (self->iface == WLI_wp_cursor_shape_manager_v1)
                return op == WLQ_wp_cursor_shape_manager_v1_get_tablet_tool_v2
                           ? wayland_fail(c, self->id, WAYLAND_INVALID_OBJECT, "the seat has no tablet") : WAYLAND_OK;
        if (op != WLQ_wp_cursor_shape_device_v1_set_shape) return WAYLAND_OK;
        if (a[1].u < WLN_wp_cursor_shape_device_v1_shape_default || a[1].u > WLN_wp_cursor_shape_device_v1_shape_all_resize)
                return wayland_fail(c, self->id, WLN_wp_cursor_shape_device_v1_error_invalid_shape, "no such shape");
        if (wayland.pointer_row >= 0 && wayland.row[wayland.pointer_row].client == c->slot && wayland_pointer_serial_ok(c, a[0].u))
        {
#define CS(n, v) [WLN_wp_cursor_shape_device_v1_shape_##n] = WAYLAND_CURSOR_##v
                static const signed char map[WLN_wp_cursor_shape_device_v1_shape_all_resize + 1] = {
                        CS(text, TEXT), CS(vertical_text, TEXT), CS(crosshair, CROSS), CS(move, MOVE), CS(all_scroll, MOVE),
                        CS(e_resize, RESIZE_H), CS(w_resize, RESIZE_H), CS(ew_resize, RESIZE_H), CS(col_resize, RESIZE_H),
                        CS(n_resize, RESIZE_V), CS(s_resize, RESIZE_V), CS(ns_resize, RESIZE_V), CS(row_resize, RESIZE_V),
                        CS(nw_resize, RESIZE_NWSE), CS(se_resize, RESIZE_NWSE), CS(nwse_resize, RESIZE_NWSE),
                        CS(ne_resize, RESIZE_NESW), CS(sw_resize, RESIZE_NESW), CS(nesw_resize, RESIZE_NESW),
                        CS(pointer, HAND), CS(grab, HAND), CS(grabbing, HAND), CS(progress, WAIT), CS(wait, WAIT),
                        CS(not_allowed, NOTALLOWED), CS(no_drop, NOTALLOWED),
                };
#undef CS
                wayland_row_do(wayland.pointer_row, WAYLAND_DO_CURSOR, a[1].u < sizeof map ? map[a[1].u] : WAYLAND_CURSOR_ARROW, 0);
                if (wayland.cursor_client >= 0) wayland.cursor_client = wayland.cursor_surface = -1, wayland.cursor_serial++;
        }
        return WAYLAND_OK;
}

/* ---- dma-bufs, wl_drm, drm_syncobj ---- */

static void wayland_dmabuf_formats(struct wayland_client *c, unsigned long id, long version, int drm)
{
        const struct wayland_dmabuf_info *info = &wayland.dmabuf;

        for (long at = 0; at < info->count; at++)
        {
                int again = 0;
                for (long before = 0; before < at && !again; before++)
                        again = info->entry[before].format == info->entry[at].format;
                if (!again)
                        wayland_ev(c, id, drm ? WLE_wl_drm_format : WLE_zwp_linux_dmabuf_v1_format, info->entry[at].format);
                if (!drm && version >= 3)
                        wayland_ev(c, id, WLE_zwp_linux_dmabuf_v1_modifier, info->entry[at].format,
                                   (unsigned int)(info->entry[at].modifier >> 32), (unsigned int)(info->entry[at].modifier & 0xffffffffUL));
        }
}

static int wayland_fourcc_supported(unsigned int fourcc)
{
        static const unsigned int ok[] = {
                WAYLAND_FOURCC_ARGB8888, WAYLAND_FOURCC_XRGB8888, WAYLAND_FOURCC_ABGR8888, WAYLAND_FOURCC_XBGR8888,
                WAYLAND_FOURCC_ARGB2101010, WAYLAND_FOURCC_XRGB2101010, WAYLAND_FOURCC_ABGR2101010, WAYLAND_FOURCC_XBGR2101010,
                WAYLAND_FOURCC_NV12,
        };
        for (unsigned long i = 0; i < sizeof ok / sizeof ok[0]; i++)
                if (ok[i] == fourcc) return 1;
        return 0;
}

static void wayland_syncobj_probe(void)
{
        wayland.syncobj_ok = 0, wayland.drm_node[0] = 0;
        if (wayland.host && wayland.syncobj.available)
        {
                long caps = wayland.syncobj.available(wayland.syncobj.context, wayland.drm_node, (long)sizeof wayland.drm_node);
                wayland.syncobj_ok = (caps & WAYLAND_SYNCOBJ_TIMELINE_OK) != 0;
                if (!(caps & WAYLAND_SYNCOBJ_NODE_OK)) wayland.drm_node[0] = 0;
        }
}

static void wayland_feedback_send(struct wayland_client *c, unsigned long id)
{
        const struct wayland_dmabuf_info *info = &wayland.dmabuf;
        long slot = wayland_slot_of(c, id), size = 0, fd;
        struct wayland_event e;

        if (slot < 0 || c->objects[slot].iface != WLI_zwp_linux_dmabuf_feedback_v1 ||
            (fd = wayland.host->dmabuf_table(wayland.host->context, &size)) < 0 || size <= 0)
                return;
        if (!wayland_out_fd_room(c))
        {
                for (long at = 0; at < c->feedbacks; at++)
                        if (c->feedback[at].id == id)
                                return c->feedback[at].need = 1, c->feedback_pending = 1, wayland_out_mark(c->slot);
                return;
        }
        e = wayland_begin(c, id, WLE_zwp_linux_dmabuf_feedback_v1_format_table);
        wayland_word(&e, (unsigned long)size), wayland_out_fd_put(c, fd, 0, e.start), wayland_end(&e);
        wayland_ev_dev(c, id, WLE_zwp_linux_dmabuf_feedback_v1_main_device, info->device);
        for (long at = 0; at < info->tranches; at++)
        {
                unsigned char indices[WAYLAND_DMABUF_ENTRIES * 2];
                wayland_ev_dev(c, id, WLE_zwp_linux_dmabuf_feedback_v1_tranche_target_device, info->tranche[at].device);
                wayland_ev(c, id, WLE_zwp_linux_dmabuf_feedback_v1_tranche_flags, (unsigned int)info->tranche[at].flags);
                for (long each = 0; each < info->tranche[at].count; each++)
                        indices[each * 2] = info->tranche[at].index[each], indices[each * 2 + 1] = 0;
                wayland_ev_array(c, id, WLE_zwp_linux_dmabuf_feedback_v1_tranche_formats, indices, (unsigned long)info->tranche[at].count * 2);
                wayland_ev0(c, id, WLE_zwp_linux_dmabuf_feedback_v1_tranche_done);
        }
        wayland_ev0(c, id, WLE_zwp_linux_dmabuf_feedback_v1_done);
}

static void wayland_feedback_flush(struct wayland_client *c)
{
        int more = 0;
        if (!c->feedback_pending || c->dead) return;
        for (long at = 0; at < c->feedbacks; at++)
        {
                if (!c->feedback[at].need) continue;
                if (!wayland_out_fd_room(c)) { more = 1; break; }
                c->feedback[at].need = 0, wayland_feedback_send(c, c->feedback[at].id);
        }
        c->feedback_pending = (unsigned char)more;
        if (more) wayland_out_mark(c->slot);
}

static void wayland_dmabuf_load(void)
{
        struct wayland_dmabuf_info *info = &wayland.dmabuf;
        long size = 0;

        memory_zero(info, sizeof *info);
        if (!wayland.host->dmabuf_info(wayland.host->context, info) || info->count < 1 || info->count > WAYLAND_DMABUF_ENTRIES ||
            info->tranches < 1 || info->tranches > WAYLAND_DMABUF_TRANCHES || wayland.host->dmabuf_table(wayland.host->context, &size) < 0 ||
            size != info->count * 16)
                goto none;
        for (long at = 0; at < info->count; at++)
                if (!wayland_fourcc_supported(info->entry[at].format)) goto none;
        for (long at = 0; at < info->tranches; at++)
        {
                if (info->tranche[at].count < 1 || info->tranche[at].count > info->count) goto none;
                for (long each = 0; each < info->tranche[at].count; each++)
                        if (info->tranche[at].index[each] >= info->count) goto none;
        }
        wayland_syncobj_probe();
        return;
none:
        info->count = 0, wayland.drm_node[0] = 0;
}

static void wayland_global_remove_send(struct wayland_client *c, unsigned long registry, long name) { wayland_ev(c, registry, WLE_wl_registry_global_remove, (unsigned int)name); }

static void wayland_global_flip_notify(long iface, int was_on, int now_on)
{
        long name = 0;
        if (was_on == now_on) return;
        for (long at = 0; at < WAYLAND_GLOBAL_COUNT && !name; at++)
                if (wayland_globals[at].iface == iface) name = at + 1;
        for (long slot = 0; name && slot < wayland.client_slots; slot++)
                for (long at = 0; wayland.client[slot] && at < wayland.client[slot]->tbl[WLT_OBJECT].count; at++)
                        if (wayland.client[slot]->objects[at].iface == WLI_wl_registry)
                                (now_on ? wayland_global_send : wayland_global_remove_send)(wayland.client[slot], wayland.client[slot]->objects[at].id, name);
}

WAYLAND_API void wayland_dmabuf_changed(void)
{
        if (!wayland.host) return;
        int was_db = wayland.dmabuf.count > 0, was_drm = was_db && wayland.drm_node[0], was_so = wayland.syncobj_ok != 0;
        wayland_dmabuf_load();
        int now_db = wayland.dmabuf.count > 0, now_drm = now_db && wayland.drm_node[0], now_so = wayland.syncobj_ok != 0;
        wayland_global_flip_notify(WLI_zwp_linux_dmabuf_v1, was_db, now_db);
        wayland_global_flip_notify(WLI_wl_drm, was_drm, now_drm);
        wayland_global_flip_notify(WLI_wp_linux_drm_syncobj_manager_v1, was_so, now_so);
        for (long slot = 0; now_db && slot < wayland.client_slots; slot++)
                for (long at = 0; wayland.client[slot] && at < wayland.client[slot]->feedbacks; at++)
                        wayland_feedback_send(wayland.client[slot], wayland.client[slot]->feedback[at].id);
}

static void wayland_params_free(struct wayland_client *c, struct wayland_params *p)
{
        if (!p->live) return;
        for (long plane = 0; plane < 4; plane++)
                if (p->fd[plane] >= 0) wayland_fd_close(p->fd[plane]), p->fd[plane] = -1;
        p->live = 0, wayland_tbl_free(c, WLT_PARAMS, p - c->params);
}

static long wayland_dmabuf_alloc_buffer(struct wayland_client *c, unsigned long width, unsigned long height, unsigned long format,
                                        unsigned long flags, long plane_count, long fds[4], const unsigned long offsets[4],
                                        const unsigned long strides[4], unsigned long modifier, long id_slot)
{
        long offset = (long)offsets[0], stride = (long)strides[0], pool, buffer, found = 0;
        long need = (format != WAYLAND_FOURCC_NV12 && (modifier == 0 || modifier == WAYLAND_MODIFIER_INVALID))
                        ? offset + stride * ((long)height - 1) + (long)width * 4 : offset + stride * (long)height;
        void *at;

        for (long e = 0; e < wayland.dmabuf.count; e++)
                found |= wayland.dmabuf.entry[e].format == format && (modifier == WAYLAND_MODIFIER_INVALID || wayland.dmabuf.entry[e].modifier == modifier);
        if (!found || (flags & ~1UL) || need < 1 || need > WAYLAND_POOL_BYTES) return -2;
        pool = wayland_tbl_alloc(c, WLT_POOL), buffer = pool >= 0 ? wayland_tbl_alloc(c, WLT_BUFFER) : -1;
        at = buffer >= 0 ? wayland.host->dmabuf_map(wayland.host->context, fds[0], modifier && modifier != WAYLAND_MODIFIER_INVALID ? -need : need) : 0;
        if (!at)
        {
                if (buffer >= 0) wayland_tbl_free(c, WLT_BUFFER, buffer);
                if (pool >= 0) wayland_tbl_free(c, WLT_POOL, pool);
                return -2;
        }
        struct wayland_pool *pl = &c->pools[pool];
        struct wayland_buffer *b = &c->buffers[buffer];
        pl->live = pl->dmabuf = pl->refs = pl->gone = 1, pl->fd = fds[0], fds[0] = -1;
        for (long k = 0; k < 3; k++)
                pl->extra_fd[k] = k + 1 < plane_count ? fds[k + 1] : -1, fds[k + 1] = k + 1 < plane_count ? -1 : fds[k + 1];
        pl->unmapped = (at == WAYLAND_DMABUF_UNMAPPED), pl->size = need, pl->id = ++wayland.pool_id, pl->at = pl->unmapped ? 0 : at;
        *b = (struct wayland_buffer){
                .live = 1, .pool = pool, .offset = offset, .width = (long)width, .height = (long)height, .stride = stride,
                .format = format == WAYLAND_FOURCC_ARGB8888 ? WLN_wl_shm_format_argb8888 : format == WAYLAND_FOURCC_XRGB8888 ? WLN_wl_shm_format_xrgb8888 : (long)format,
                .fourcc = (unsigned int)format, .modifier = modifier, .plane_count = plane_count, .y_invert = (unsigned char)(flags & 1), .id = c->objects[id_slot].id,
        };
        for (long k = 0; k < plane_count; k++) b->plane_offset[k] = offsets[k], b->plane_stride[k] = strides[k];
        return c->objects[id_slot].link = (int)buffer, c->dmabufs++, wayland.dmabufs++, wayland.pools++, wayland.buffers++, buffer;
}

static int wayland_dmabuf_validate(unsigned long width, unsigned long height, unsigned long format,
                                   long plane_count, int linear, const unsigned long *offsets, const unsigned long *strides)
{
        if (plane_count < 1) return 1;
        if (width < 1 || height < 1 || width > WAYLAND_DIMENSION || height > WAYLAND_DIMENSION) return 2;
        if (!wayland_fourcc_supported((unsigned int)format)) return 3;
        if (format == WAYLAND_FOURCC_NV12 ? (plane_count != 2 || (width & 1) || (height & 1)) : (linear && plane_count != 1)) return 1;
        if (format != WAYLAND_FOURCC_NV12 && (!strides[0] || (linear && (strides[0] < width * 4 || (strides[0] & 3))))) return 4;
        for (long k = 0; k < plane_count; k++)
                if (offsets[k] > 0x7fffffffUL || strides[k] > (1UL << 20) || (format == WAYLAND_FOURCC_NV12 && strides[k] < width)) return 4;
        return 0;
}

static long wayland_dmabuf_make(struct wayland_client *c, struct wayland_object *self, struct wayland_params *p, const struct wayland_arg *a, long id_slot)
{
        unsigned long width = a[0].u, height = a[1].u, format = a[2].u, flags = a[3].u;
        long plane_count = 0;
        int err;

        p->used = 1;
        for (long k = 0; k < 4; k++)
                if (p->fd[k] >= 0 && k != plane_count++)
                        return wayland_fail(c, self->id, WLN_zwp_linux_buffer_params_v1_error_incomplete, "planes must be contiguous from 0"), -1;
        if ((err = wayland_dmabuf_validate(width, height, format, plane_count, p->modifier[0] == 0 || p->modifier[0] == WAYLAND_MODIFIER_INVALID, p->offset, p->stride)))
        {
                static const struct { unsigned int code; const char *msg; } errs[5] = {
                        {0, 0}, {WLN_zwp_linux_buffer_params_v1_error_incomplete, "incomplete planes"},
                        {WLN_zwp_linux_buffer_params_v1_error_invalid_dimensions, "the size is out of range"},
                        {WLN_zwp_linux_buffer_params_v1_error_invalid_format, "the format is not offered"},
                        {WLN_zwp_linux_buffer_params_v1_error_out_of_bounds, "the plane does not fit its size"},
                };
                return wayland_fail(c, self->id, errs[err].code, errs[err].msg), -1;
        }
        for (long k = 1; k < plane_count; k++)
                if (p->modifier[k] != p->modifier[0]) return -2;
        return wayland_dmabuf_alloc_buffer(c, width, height, format, flags, plane_count, p->fd, p->offset, p->stride, p->modifier[0], id_slot);
}

static int wayland_dmabuf_request(struct wayland_client *c, struct wayland_object *self, long op, struct wayland_arg *a)
{
        struct wayland_params *p;

        if (self->iface == WLI_zwp_linux_dmabuf_v1)
        {
                if (op == WLQ_zwp_linux_dmabuf_v1_create_params)
                {
                        long index = wayland_tbl_alloc(c, WLT_PARAMS);
                        if (index < 0) return wayland_fail(c, self->id, WAYLAND_NO_MEMORY, "no memory for buffer params");
                        c->params[index] = (struct wayland_params){.id = c->objects[a[0].slot].id, .live = 1, .fd = {-1, -1, -1, -1}};
                        c->objects[a[0].slot].link = (int)index;
                }
                else if (op == WLQ_zwp_linux_dmabuf_v1_get_default_feedback || op == WLQ_zwp_linux_dmabuf_v1_get_surface_feedback)
                {
                        if (!wayland_tbl_reach(c, WLT_FEEDBACK, c->feedbacks + 1))
                                return wayland_fail(c, self->id, WAYLAND_NO_MEMORY, "no memory for the feedback");
                        c->feedback[c->feedbacks++] = (struct wayland_feedback){c->objects[a[0].slot].id, 0};
                        wayland_feedback_send(c, c->objects[a[0].slot].id);
                }
                return WAYLAND_OK;
        }
        if (self->iface == WLI_zwp_linux_dmabuf_feedback_v1)
        {
                for (long at = 0; at < c->feedbacks; at++)
                        if (c->feedback[at].id == self->id)
                                return c->feedback[at] = c->feedback[--c->feedbacks], c->feedback[c->feedbacks].need = 0, WAYLAND_OK;
                return WAYLAND_OK;
        }
        p = &c->params[self->link];
        if (op == WLQ_zwp_linux_buffer_params_v1_destroy) return wayland_params_free(c, p), WAYLAND_OK;
        if (p->used) return wayland_fail(c, self->id, WLN_zwp_linux_buffer_params_v1_error_already_used, "the params were used");
        if (op == WLQ_zwp_linux_buffer_params_v1_add)
        {
                unsigned long plane = a[1].u;
                if (plane > 3) return wayland_fail(c, self->id, WLN_zwp_linux_buffer_params_v1_error_plane_idx, "no such plane");
                if (p->fd[plane] >= 0) return wayland_fail(c, self->id, WLN_zwp_linux_buffer_params_v1_error_plane_set, "the plane is set");
                p->fd[plane] = a[0].fd, a[0].fd = -1, p->offset[plane] = a[2].u, p->stride[plane] = a[3].u, p->modifier[plane] = a[4].u << 32 | a[5].u;
                return WAYLAND_OK;
        }
        if (op == WLQ_zwp_linux_buffer_params_v1_set_sampling_device) return WAYLAND_OK;
        int immed = op != WLQ_zwp_linux_buffer_params_v1_create;
        unsigned long id = immed ? 0 : wayland_server_id(c);
        long obj = immed ? a[0].slot : (id ? wayland_object_new(c, id, WLI_wl_buffer, 1) : -1);
        long made = obj >= 0 ? wayland_dmabuf_make(c, self, p, a + immed, obj) : (p->used = 1, -2);
        if (made < 0 && !immed && obj >= 0) wayland_object_free(c, obj);
        if (made == -1) return WAYLAND_DEAD;
        if (made < 0)
                return immed ? wayland_fail(c, self->id, WLN_zwp_linux_buffer_params_v1_error_invalid_wl_buffer, "the buffer cannot be made")
                             : (wayland_ev0(c, self->id, WLE_zwp_linux_buffer_params_v1_failed), WAYLAND_OK);
        if (!immed) wayland_ev(c, self->id, WLE_zwp_linux_buffer_params_v1_created, (unsigned int)id);
        return WAYLAND_OK;
}

static int wayland_drm_request(struct wayland_client *c, struct wayland_object *self, long op, struct wayland_arg *a)
{
        if (op == WLQ_wl_drm_authenticate) return wayland_ev0(c, self->id, WLE_wl_drm_authenticated), WAYLAND_OK;
        if (op != WLQ_wl_drm_create_prime_buffer)
                return wayland_fail(c, self->id, WLN_wl_drm_error_invalid_name, "flink buffer names are not supported");
        unsigned long w = (unsigned long)a[2].i, h = (unsigned long)a[3].i, fmt = a[4].u;
        long fds[4] = {a[1].fd, -1, -1, -1}, planes = fmt == WAYLAND_FOURCC_NV12 ? 2 : 1, made;
        unsigned long off[4] = {(unsigned long)a[5].i, (unsigned long)a[7].i, (unsigned long)a[9].i, 0};
        unsigned long str[4] = {(unsigned long)a[6].i, (unsigned long)a[8].i, (unsigned long)a[10].i, 0};
        if (wayland_dmabuf_validate(w, h, fmt, planes, 1, off, str))
                return wayland_fail(c, self->id, WLN_wl_drm_error_invalid_format, "invalid prime buffer parameters");
        made = wayland_dmabuf_alloc_buffer(c, w, h, fmt, 0, planes, fds, off, str, WAYLAND_MODIFIER_INVALID, a[0].slot), a[1].fd = fds[0];
        return made < 0 ? wayland_fail(c, self->id, WLN_wl_drm_error_invalid_name, "cannot import prime buffer") : WAYLAND_OK;
}

static int wayland_syncobj_request(struct wayland_client *c, struct wayland_object *self, long op, struct wayland_arg *a)
{
        struct wayland_surface *s;

        if (self->iface == WLI_wp_linux_drm_syncobj_manager_v1)
        {
                if (op == WLQ_wp_linux_drm_syncobj_manager_v1_get_surface) return wayland_ext_bind(c, self, op, a, 0);
                if (op != WLQ_wp_linux_drm_syncobj_manager_v1_import_timeline) return WAYLAND_OK;
                long idx = wayland_tbl_alloc(c, WLT_TIMELINE), handle;
                if (idx < 0) return wayland_fail(c, self->id, WAYLAND_NO_MEMORY, "no memory for a syncobj timeline");
                if (idx > 0xffffL) /* sync slots and rel_timeline are 16 bits: a higher index would alias another timeline */
                        return wayland_tbl_free(c, WLT_TIMELINE, idx),
                               wayland_fail(c, self->id, WAYLAND_NO_MEMORY, "too many syncobj timelines");
                handle = wayland.syncobj.import ? wayland.syncobj.import(wayland.syncobj.context, a[1].fd) : -1;
                wayland_fd_close(a[1].fd), a[1].fd = -1;
                if (handle < 0)
                        return wayland_tbl_free(c, WLT_TIMELINE, idx),
                               wayland_fail(c, self->id, WLN_wp_linux_drm_syncobj_manager_v1_error_invalid_timeline, "invalid drm syncobj timeline fd");
                c->timelines[idx].live = 1, c->timelines[idx].handle = handle, c->objects[a[0].slot].link = (int)idx;
                return WAYLAND_OK;
        }
        if (self->iface == WLI_wp_linux_drm_syncobj_timeline_v1)
        {
                if (self->link >= 0 && self->link < c->tbl[WLT_TIMELINE].count && c->timelines[self->link].live)
                        c->timelines[self->link].gone = 1, wayland_timeline_unref(c, self->link);
                return WAYLAND_OK;
        }
        s = wayland_surface_of(c, self - c->objects);
        if (op == WLQ_wp_linux_drm_syncobj_surface_v1_destroy)
                return s ? (s->syncobj_id = 0, wayland_syncobj_clear_pending(c, s), WAYLAND_OK) : WAYLAND_OK;
        if (!s) return wayland_fail(c, self->id, WLN_wp_linux_drm_syncobj_surface_v1_error_no_surface, "the surface is gone");
        if (op == WLQ_wp_linux_drm_syncobj_surface_v1_set_acquire_point || op == WLQ_wp_linux_drm_syncobj_surface_v1_set_release_point)
        {
                long tl = a[0].slot >= 0 ? c->objects[a[0].slot].link : -1;
                unsigned long long pt = ((unsigned long long)a[1].u << 32) | a[2].u;
                if (tl < 0 || tl >= c->tbl[WLT_TIMELINE].count || !c->timelines[tl].live)
                        return wayland_fail(c, self->id, WAYLAND_INVALID_OBJECT, "invalid syncobj timeline");
                if (op == WLQ_wp_linux_drm_syncobj_surface_v1_set_acquire_point)
                {
                        if (s->pending.sync_acq_set) wayland_timeline_put(c, s->pending.sync_acq_slot);
                        s->pending.sync_acq_set = 1, s->pending.sync_acq_slot = (unsigned short)tl, s->pending.sync_acq_point = pt, c->timelines[tl].refs++;
                }
                else
                {
                        if (s->pending.sync_rel_set) wayland_timeline_put(c, s->pending.sync_rel_slot);
                        s->pending.sync_rel_set = 1, s->pending.sync_rel_slot = (unsigned short)tl, s->pending.sync_rel_point = pt, c->timelines[tl].refs++;
                }
        }
        return WAYLAND_OK;
}

/* ---- relative pointer, direct input, constraints, extensions, and dispatch ---- */

// The accelerated motion in the surface's units; the unaccelerated is the device's own counts, unscaled, as other compositors send it
// (Xwayland makes its raw events of it, and SDL 2 truncates a raw delta's fraction: above 100% a lone count was read as 0).
static long wayland_relative_send(struct wayland_client *c, long scale120, long dx, long dy, long udx, long udy, long micro)
{
        long sent = 0;
        const unsigned int words[6] = {(unsigned int)((unsigned long)micro >> 32), (unsigned int)micro,
                                       (unsigned int)wayland_units_fixed(dx, scale120), (unsigned int)wayland_units_fixed(dy, scale120),
                                       (unsigned int)((unsigned long)udx * 256), (unsigned int)((unsigned long)udy * 256)};
        for (long at = 0, slot; (slot = wayland_ids_next(c, &c->relative_ids, WLI_zwp_relative_pointer_v1, &at)) >= 0; sent++)
                wayland_emit(c, c->objects[slot].id, WLE_zwp_relative_pointer_v1_relative_motion, words, 6);
        return sent;
}

static void wayland_counts_send(int frame)
{
        struct wayland_client *c;
        struct wayland_surface *s;
        long row = wayland.counts_row;
        wayland.counts_row = -1;
        if (row >= 0 && (s = wayland_row_surface(row, &c)) &&
            wayland_relative_send(c, s->scale120, wayland.counts[0], wayland.counts[1], wayland.counts[2], wayland.counts[3], wayland.counts_micro) && frame)
                wayland_pointer_frames(c);
}

WAYLAND_API void wayland_pointer_relative(long row, long dx, long dy, long udx, long udy, long micro)
{
        struct wayland_client *c;
        struct wayland_surface *s = wayland_row_surface(row, &c);
        if (!wayland.host || !s || row != (wayland.hold_mode ? wayland.hold_row : wayland.pointer_row) || wayland_dragging() || !c->relative_ids.count)
                return;
        if (wayland.hold_mode == 1)
        {
                if (wayland_relative_send(c, s->scale120, dx, dy, udx, udy, micro)) wayland_pointer_frames(c);
                return;
        }
        if (wayland.counts_row != row)
                wayland_counts_send(1), memory_zero(wayland.counts, sizeof wayland.counts), wayland.counts_row = row;
        wayland.counts[0] += dx, wayland.counts[1] += dy, wayland.counts[2] += udx, wayland.counts[3] += udy, wayland.counts_micro = micro;
}

WAYLAND_API void wayland_pointer_counts_flush(void) { if (wayland.host) wayland_counts_send(1); }
WAYLAND_API unsigned long wayland_fixed_of(long pixels, long scale120) { return wayland_units_fixed(pixels, scale120); }
WAYLAND_API void wayland_unhold_row(long row) { if (row != wayland.unheld_row) wayland.unheld_row = row, wayland_constraints_update(); }

static int wayland_ids_collect(struct wayland_client *c, const struct wayland_ids *ids, long iface, long min_ver,
                               unsigned int *out, long *count, long room)
{
        long n = 0;
        for (long at = 0, slot; (slot = wayland_ids_next(c, ids, iface, &at)) >= 0;)
                if (c->objects[slot].version >= min_ver)
                {
                        if (n >= room) return 0;
                        out[n++] = c->objects[slot].id;
                }
        return *count = n, 1;
}

WAYLAND_API long wayland_pointer_direct(long row, unsigned int *relative, long *relatives, unsigned int *frames, long *framed, long room, long *scale120)
{
        struct wayland_client *c;
        struct wayland_surface *s = wayland_row_surface(row, &c);
        *relatives = *framed = 0;
        if (!wayland.host || !s || wayland.hold_mode != 1 || row != wayland.hold_row || wayland_dragging() || !c->relative_ids.count ||
            !wayland_ids_collect(c, &c->relative_ids, WLI_zwp_relative_pointer_v1, 1, relative, relatives, room) ||
            !wayland_ids_collect(c, &c->seat_ids[0], WLI_wl_pointer, 5, frames, framed, room))
                return 0;
        return *scale120 = s->scale120, *relatives ? c->slot + 1 : 0;
}

WAYLAND_API long wayland_pointer_direct_ptrs(long row, unsigned int *ptrs, unsigned char *vers, long *count, unsigned int *frames, long *framed,
                                             long room, long *scale120, unsigned long **serial)
{
        struct wayland_client *c;
        struct wayland_surface *s = wayland_row_surface(row, &c);
        long n = 0;

        *count = *framed = 0;
        if (!wayland.host || !s || row != wayland.pointer_row || wayland_dragging() ||
            !wayland_ids_collect(c, &c->seat_ids[0], WLI_wl_pointer, 5, frames, framed, room))
                return 0;
        for (long at = 0, slot; (slot = wayland_ids_next(c, &c->seat_ids[0], WLI_wl_pointer, &at)) >= 0;)
        {
                if (n >= room) return 0;
                ptrs[n] = c->objects[slot].id;
                vers[n] = (unsigned char)(c->objects[slot].version > 255 ? 255 : c->objects[slot].version);
                n++;
        }
        if (!n) return 0;
        *count = n;
        if (scale120) *scale120 = s->scale120;
        if (serial) *serial = &c->serial;
        return c->slot + 1;
}

WAYLAND_API void wayland_pointer_noted(long row, long pressed, unsigned long serial)
{
        struct wayland_client *c;
        wayland.buttons += pressed ? 1 : wayland.buttons > 0 ? -1 : 0;
        if (!wayland.host || row != wayland.pointer_row || !wayland_row_surface(row, &c))
                return;
        if (pressed) c->press_serial = (unsigned int)serial;
        c->input_serial[c->input_next++ % WAYLAND_INPUT_SERIALS] = (unsigned int)serial;
}

WAYLAND_API long wayland_keyboard_direct(long row, unsigned int *ids, long *count, long room, unsigned long **serial)
{
        struct wayland_client *c;
        *count = 0;
        if (!wayland.host || row != wayland.keyboard_row || !wayland_row_surface(row, &c) || wayland_dragging() ||
            !wayland_ids_collect(c, wayland_seat_ids(c, WLI_wl_keyboard), WLI_wl_keyboard, 1, ids, count, room) || !*count)
                return 0;
        return *serial = &c->serial, c->slot + 1;
}

WAYLAND_API void wayland_key_noted(long row, long code, long pressed, unsigned long serial)
{
        struct wayland_client *c;
        if (!wayland.host || code < 1 || code > 0x2ff) return;
        if (code < 128)
        {
                unsigned char bit = (unsigned char)(1u << (code & 7));
                wayland.keys[code >> 3] = pressed ? (unsigned char)(wayland.keys[code >> 3] | bit) : (unsigned char)(wayland.keys[code >> 3] & ~bit);
        }
        if (row == wayland.keyboard_row && wayland_row_surface(row, &c))
                c->input_serial[c->input_next++ % WAYLAND_INPUT_SERIALS] = (unsigned int)serial;
}

static void wayland_constraint_event(struct wayland_client *c, struct wayland_constraint *con, int on)
{
        if (wayland_slot_of(c, con->id) >= 0)
                wayland_ev0(c, con->id, con->kind == 1 ? (on ? WLE_zwp_locked_pointer_v1_locked : WLE_zwp_locked_pointer_v1_unlocked)
                                                       : (on ? WLE_zwp_confined_pointer_v1_confined : WLE_zwp_confined_pointer_v1_unconfined));
}

static void wayland_constraint_release(struct wayland_client *c, struct wayland_constraint *con, int say)
{
        struct wayland_surface *s = con->surface ? &c->surfaces[con->surface - 1] : 0;
        if (con->kind == 1 && con->hinted && s && s->live && wayland_root_of(c, s) == s && s->row >= 0 &&
            (con->active || (s->row == wayland.pointer_row && wayland_has_keys(c))))
        {
                struct wayland_rect w = wayland_window_of(c, s);
                wayland.hint_row = s->row;
                wayland.hint_x = wayland_pixels_floor(con->hint_x / 256, s->scale120) - w.x;
                wayland.hint_y = wayland_pixels_floor(con->hint_y / 256, s->scale120) - w.y;
        }
        if (con->active && say) wayland_constraint_event(c, con, 0);
        con->active = 0;
        if ((con->oneshot || !say) && con->live)
                con->live = 0, wayland.constraints--, wayland_tbl_free(c, WLT_CONSTRAINT, con - c->constraints);
}

static void wayland_constraints_update(void)
{
        struct wayland_client *owner = 0;
        struct wayland_constraint *chosen = 0;
        long row = -1, mode = 0;

        if (!wayland.host || (!wayland.constraints && !wayland.hold_mode)) return;
        for (long slot = 0; slot < wayland.client_slots && !chosen; slot++)
        {
                struct wayland_client *c = wayland.client[slot];
                for (long at = 0; c && at < c->tbl[WLT_CONSTRAINT].count && !chosen; at++)
                {
                        struct wayland_constraint *con = &c->constraints[at];
                        struct wayland_surface *s = con->live && con->surface ? &c->surfaces[con->surface - 1] : 0;
                        struct wayland_surface *root = s && s->live ? wayland_root_of(c, s) : 0;
                        if (root && root->row >= 0 && root->row == wayland.pointer_row && root->row != wayland.unheld_row &&
                            (root->row == wayland.keyboard_row ||
                             ((root->role == WAYLAND_ROLE_POPUP || (root->xsurface && root->u.xwin.popup)) && wayland_has_keys(c))))
                                owner = c, chosen = con, row = root->row, mode = con->kind;
                }
        }
        for (long slot = 0; slot < wayland.client_slots; slot++)
                for (long at = 0; wayland.client[slot] && at < wayland.client[slot]->tbl[WLT_CONSTRAINT].count; at++)
                        if (wayland.client[slot]->constraints[at].live && wayland.client[slot]->constraints[at].active &&
                            &wayland.client[slot]->constraints[at] != chosen)
                                wayland_constraint_release(wayland.client[slot], &wayland.client[slot]->constraints[at], 1);
        if (chosen && !chosen->active) chosen->active = 1, wayland_constraint_event(owner, chosen, 1);
        if (row != wayland.hold_row || mode != wayland.hold_mode || wayland.hint_row >= 0)
                wayland.hold_row = row, wayland.hold_mode = mode, wayland.host->pointer_hold(wayland.host->context, row, mode);
}

static void wayland_constraints_drop(struct wayland_client *c, long surface)
{
        for (long at = 0; at < c->tbl[WLT_CONSTRAINT].count; at++)
                if (c->constraints[at].live && c->constraints[at].surface == surface + 1)
                        wayland_constraint_release(c, &c->constraints[at], 0);
        wayland_constraints_update();
}

static inline void wayland_lent_drop(struct wayland_client *c, unsigned int *slot) { if (*slot) wayland_let_go(c, *slot - 1), *slot = 0; }

static int wayland_tree_has_cb(struct wayland_client *c, struct wayland_surface *root, int which)
{
        for (struct wayland_surface *o = root; o; o = wayland_tree_next(c, root, o))
                if (o->cbq[which].count) return 1;
        return 0;
}

static void wayland_outputs_send(struct wayland_client *c)
{
        for (long at = 0; at < c->tbl[WLT_OBJECT].count; at++)
                if (c->objects[at].iface == WLI_zxdg_output_v1) wayland_xdg_output_send(c, at, c->objects[at].aux);
        for (long at = 0; at < c->tbl[WLT_OBJECT].count; at++)
                if (c->objects[at].iface == WLI_wl_output) wayland_output_send(c, at);
}

static int wayland_token_free(void)
{
        long now = wayland_now(), free_at = -1;
        for (long at = 0; at < wayland.tokens; at++)
        {
                /* a token expires WAYLAND_TOKEN_MS after it was made, committed or not: an uncommitted one never did, and grew the table */
                if (wayland.token[at].live && now - wayland.token[at].when >= WAYLAND_TOKEN_MS)
                        wayland.token[at].live = 0;
                if (!wayland.token[at].live && free_at < 0) free_at = at;
        }
        if (free_at >= 0) return wayland.token[free_at].gen++, (int)free_at;
        wayland.token_table.unit = sizeof(struct wayland_token);
        if (wayland.tokens >= WAYLAND_TOKENS_MAX || !wayland_table_reach(&wayland.token_table, wayland.tokens + 1)) return -1;
        wayland.token = (struct wayland_token *)wayland.token_table.at;
        wayland.token[wayland.tokens].live = 0, wayland.token[wayland.tokens].gen = 0;
        return (int)wayland.tokens++;
}

static void wayland_token_forget(long slot)
{
        long live = 0;
        for (long at = 0; at < wayland.tokens; at++)
        {
                if (wayland.token[at].live && wayland.token[at].client == slot) wayland.token[at].live = 0;
                live += wayland.token[at].live;
        }
        if (!live && wayland.tokens)
                wayland_table_drop(&wayland.token_table), wayland.token = 0, wayland.tokens = 0;
}

static int wayland_activation_request(struct wayland_client *c, struct wayland_object *self, long op, struct wayland_arg *a)
{
        int at = self->link;
        struct wayland_token *t = at >= 0 && at < wayland.tokens && wayland.token[at].live && wayland.token[at].client == c->slot &&
                                          wayland.token[at].gen == self->aux ? &wayland.token[at] : 0;

        if (self->iface == WLI_xdg_activation_v1)
        {
                struct wayland_surface *s;
                if (op == WLQ_xdg_activation_v1_get_activation_token)
                {
                        if ((at = wayland_token_free()) < 0) return wayland_fail(c, self->id, WAYLAND_NO_MEMORY, "no memory for an activation token");
                        wayland.token[at] = (struct wayland_token){.live = 1, .client = (int)c->slot, .gen = wayland.token[at].gen, .when = wayland_now()};
                        c->objects[a[0].slot].link = at, c->objects[a[0].slot].aux = wayland.token[at].gen;
                        return WAYLAND_OK;
                }
                if (op != WLQ_xdg_activation_v1_activate) return WAYLAND_OK;
                if (!(s = wayland_surface_of(c, a[1].slot))) return wayland_fail(c, self->id, WAYLAND_INVALID_OBJECT, "a surface is gone");
                for (at = 0; at < wayland.tokens; at++)
                        if (wayland.token[at].live && wayland.token[at].committed && wayland_name_is(a[0].text, a[0].length, wayland.token[at].text))
                                break;
                if (at < wayland.tokens)
                {
                        struct wayland_token *g = &wayland.token[at];
                        struct wayland_client *issuer = wayland.client[g->client];
                        g->live = 0;
                        if (wayland_now() - g->when < WAYLAND_TOKEN_MS && issuer && s->row >= 0 && wayland_input_serial_ok(issuer, g->serial))
                                s->attention = 0, wayland_row_do(s->row, WAYLAND_DO_FOCUS, 0, 0);
                        else
                                s->attention = 1;
                }
                return WAYLAND_OK;
        }
        if (!t || t->committed) return WAYLAND_OK;
        if (op == WLQ_xdg_activation_token_v1_set_serial) t->serial = a[0].u;
        else if (op == WLQ_xdg_activation_token_v1_commit)
        {
                unsigned char bytes[16];
                if (!wayland.host->random(wayland.host->context, bytes, (long)sizeof bytes)) return t->live = 0, WAYLAND_OK;
                memory_into_hex(t->text, bytes, 16), t->text[32] = 0, t->committed = 1, t->when = wayland_now();
                wayland_ev_str(c, self->id, WLE_xdg_activation_token_v1_done, t->text);
        }
        else if (op == WLQ_xdg_activation_token_v1_destroy) t->live = 0;
        return WAYLAND_OK;
}

static long wayland_inhibitors_now(void)
{
        long count = 0;
        for (long slot = 0; slot < wayland.client_slots; slot++)
        {
                struct wayland_client *c = wayland.client[slot];
                if (!c || c->dead || !c->inhibitors) continue;
                for (long at = 0, seen = 0; at < c->tbl[WLT_OBJECT].count && seen < c->inhibitors; at++)
                        if (c->objects[at].iface == WLI_zwp_idle_inhibitor_v1 && c->objects[at].id)
                        {
                                struct wayland_surface *s = wayland_surface_of(c, at);
                                seen++;
                                if (s && (s = wayland_root_of(c, s))->live && s->row >= 0 && !wayland_row_suspended(s->row))
                                        count++;
                        }
        }
        return count;
}

/* Unified handler for surface-attached extensions, hints, games/constraints, xwayland, idle, gestures, presentation, and layer-shell. */
static int wayland_ext_request(struct wayland_client *c, struct wayland_object *self, long op, struct wayland_arg *a)
{
        struct wayland_surface *s;
        int status;

        switch (self->iface)
        {
        case WLI_xdg_wm_dialog_v1:
        case WLI_wp_content_type_manager_v1:
        case WLI_wp_alpha_modifier_v1:
        case WLI_wp_fifo_manager_v1:
        case WLI_wp_commit_timing_manager_v1:
        case WLI_wp_tearing_control_manager_v1:
                return wayland_ext_bind(c, self, op, a, 0);
        case WLI_wp_fractional_scale_manager_v1:
                if ((status = wayland_ext_bind(c, self, op, a, &s)) == WAYLAND_OK && s)
                        wayland_ev(c, s->fractional_id, WLE_wp_fractional_scale_v1_preferred_scale, (unsigned int)(s->told120 > 0 ? s->told120 : WAYLAND_UNIT));
                return status;
        case WLI_zwp_idle_inhibit_manager_v1:
                if ((status = wayland_ext_bind(c, self, op, a, &s)) == WAYLAND_OK && s) c->inhibitors++;
                return status;
        case WLI_zwp_keyboard_shortcuts_inhibit_manager_v1:
                if ((status = wayland_ext_bind(c, self, op, a, &s)) == WAYLAND_OK && s)
                {
                        s->shortcut_id = c->objects[a[0].slot].id;
                        if (s->row >= 0 && s->row == wayland.keyboard_row)
                                s->shortcut_active = 1, wayland_ev0(c, s->shortcut_id, WLE_zwp_keyboard_shortcuts_inhibitor_v1_active);
                }
                return status;
        case WLI_zwp_xwayland_keyboard_grab_manager_v1:
                if ((status = wayland_ext_bind(c, self, op, a, &s)) == WAYLAND_OK && s)
                {
                        s->xgrab = 1;
                        if (s->row >= 0 && s->row == wayland.keyboard_row && !s->shortcut_active) s->shortcut_active = 1, s->escape_down = 0;
                }
                return status;
        case WLI_wp_presentation:
                if (op != WLQ_wp_presentation_feedback) return WAYLAND_OK;
                if (!(s = wayland_surface_of(c, a[0].slot))) return wayland_fail(c, self->id, WAYLAND_INVALID_OBJECT, "a surface is gone");
                if (!wayland_cb_push(c, &s->cbq[WLCB_FEEDBACK], c->objects[a[1].slot].id)) wayland_feedback_discard(c, c->objects[a[1].slot].id);
                return WAYLAND_OK;
        case WLI_xdg_toplevel_icon_manager_v1:
                if (op == WLQ_xdg_toplevel_icon_manager_v1_set_icon)
                {
                        long icon = a[1].u ? a[1].slot : -1;
                        if (icon >= 0) c->objects[icon].flags |= WAYLAND_ICON_SET;
                        if ((s = wayland_surface_of(c, a[0].slot)) && s->role_id == c->objects[a[0].slot].id)
                                s->pending.icon = icon >= 0 && (c->objects[icon].flags & WAYLAND_ICON_CONTENT) ? 1 : 0, wayland.icons += s->pending.icon;
                }
                return WAYLAND_OK;
        case WLI_xdg_toplevel_icon_v1:
                if (op != WLQ_xdg_toplevel_icon_v1_set_name && op != WLQ_xdg_toplevel_icon_v1_add_buffer) return WAYLAND_OK;
                if (self->flags & WAYLAND_ICON_SET) return wayland_fail(c, self->id, WLN_xdg_toplevel_icon_v1_error_immutable, "the icon is on a toplevel");
                if (op == WLQ_xdg_toplevel_icon_v1_add_buffer)
                {
                        struct wayland_buffer *b = &c->buffers[c->objects[a[0].slot].link];
                        if (c->pools[b->pool].dmabuf || b->width != b->height || b->width <= 0 || a[1].i < 1)
                                return wayland_fail(c, self->id, WLN_xdg_toplevel_icon_v1_error_invalid_buffer, "the buffer is not a square of shared memory");
                }
                return self->flags |= WAYLAND_ICON_CONTENT, WAYLAND_OK;
        case WLI_xdg_system_bell_v1:
                if (op == WLQ_xdg_system_bell_v1_ring) wayland.bells++;
                return WAYLAND_OK;
        case WLI_canvas_swapchain_v1:
        {
                struct wayland_surface *t = 0;
                if (op != WLQ_canvas_swapchain_v1_override_window && op != WLQ_canvas_swapchain_v1_override_surface) return WAYLAND_OK;
                if (!(s = wayland_surface_of(c, a[0].slot))) return wayland_fail(c, self->id, WAYLAND_INVALID_OBJECT, "the surface is gone");
                if (op == WLQ_canvas_swapchain_v1_override_surface && !(t = wayland_surface_of(c, a[1].slot)))
                        return wayland_fail(c, self->id, WAYLAND_INVALID_OBJECT, "the target is gone");
                if ((s->role != WAYLAND_ROLE_NONE && s->role != WAYLAND_ROLE_STAND_IN) || s->xsurface || (s->flags & WAYLAND_SURFACE_XDG) ||
                    s == t || (s->role == WAYLAND_ROLE_NONE && (s->flags & WAYLAND_SURFACE_MAPPED)))
                        return wayland_fail(c, self->id, WLN_canvas_swapchain_v1_error_role, "the surface has another role");
                wayland_stand_end(c, s, 1);
                s->role = WAYLAND_ROLE_STAND_IN, memory_zero(&s->u.stand, sizeof s->u.stand);
                s->u.stand.manager = self->id, s->u.stand.said = 0xff;
                if (t)
                        s->u.stand.target = (unsigned int)wayland_surface_index(c, t) + 1, s->u.stand.target_gen = t->gen;
                else
                        s->u.stand.window = a[1].u;
                wayland_stand_row(c, s);
                return WAYLAND_OK;
        }
        case WLI_xwayland_shell_v1:
                if (op != WLQ_xwayland_shell_v1_get_xwayland_surface) return WAYLAND_OK;
                if (!(s = wayland_surface_of(c, a[1].slot))) return wayland_fail(c, self->id, WAYLAND_INVALID_OBJECT, "the surface is gone");
                if (s->xsurface || (s->flags & WAYLAND_SURFACE_XDG) || s->role != WAYLAND_ROLE_NONE)
                        return wayland_fail(c, self->id, WLN_xwayland_shell_v1_error_role, "the surface has another role");
                return s->xsurface = 1, wayland_surface_link(c, a[0].slot, s), wayland_xwayland_client(c->slot), WAYLAND_OK;
        case WLI_xwayland_surface_v1:
                if (op == WLQ_xwayland_surface_v1_set_serial)
                {
                        unsigned long long serial = ((unsigned long long)a[1].u << 32) | a[0].u;
                        if (!(s = wayland_surface_of(c, self - c->objects))) return wayland_fail(c, self->id, WAYLAND_INVALID_OBJECT, "the surface is gone");
                        if (s->u.xwin.serial_set) return wayland_fail(c, self->id, WLN_xwayland_surface_v1_error_already_associated, "the surface has a serial");
                        if (!serial) return wayland_fail(c, self->id, WLN_xwayland_surface_v1_error_invalid_serial, "the serial is zero");
                        s->u.xwin.serial = serial, s->u.xwin.serial_set = 1;
                }
                return WAYLAND_OK;
        case WLI_zwp_relative_pointer_manager_v1:
                if (op == WLQ_zwp_relative_pointer_manager_v1_get_relative_pointer)
                {
                        if (!wayland_ids_add(&c->relative_ids, c->objects[a[0].slot].id))
                                return wayland_fail(c, self->id, WAYLAND_NO_MEMORY, "no memory for a relative pointer");
                        wayland.seat_gen++;
                }
                return WAYLAND_OK;
        case WLI_zwp_relative_pointer_v1:
                return wayland_ids_remove(&c->relative_ids, self->id), wayland.seat_gen++, WAYLAND_OK;
        case WLI_zwp_pointer_constraints_v1:
        {
                long index;
                if (op == WLQ_zwp_pointer_constraints_v1_destroy) return WAYLAND_OK;
                if (!(s = wayland_surface_of(c, a[1].slot))) return wayland_fail(c, self->id, WAYLAND_INVALID_OBJECT, "a surface is gone");
                if (a[4].u != WLN_zwp_pointer_constraints_v1_lifetime_oneshot && a[4].u != WLN_zwp_pointer_constraints_v1_lifetime_persistent)
                        return wayland_fail(c, self->id, WAYLAND_INVALID_METHOD, "the lifetime is oneshot or persistent");
                for (long at = 0; at < c->tbl[WLT_CONSTRAINT].count; at++)
                        if (c->constraints[at].live && c->constraints[at].surface == wayland_surface_index(c, s) + 1)
                                return wayland_fail(c, self->id, WLN_zwp_pointer_constraints_v1_error_already_constrained, "the surface is constrained already");
                if ((index = wayland_tbl_alloc(c, WLT_CONSTRAINT)) < 0)
                        return wayland_fail(c, self->id, WAYLAND_NO_MEMORY, "no memory for a pointer constraint");
                c->constraints[index] = (struct wayland_constraint){
                        .id = c->objects[a[0].slot].id, .live = 1, .kind = op == WLQ_zwp_pointer_constraints_v1_lock_pointer ? 1 : 2,
                        .oneshot = a[4].u == WLN_zwp_pointer_constraints_v1_lifetime_oneshot, .surface = (unsigned int)(wayland_surface_index(c, s) + 1),
                };
                return c->objects[a[0].slot].link = (int)index, wayland.constraints++, wayland_constraints_update(), WAYLAND_OK;
        }
        case WLI_zwp_locked_pointer_v1:
        case WLI_zwp_confined_pointer_v1:
        {
                struct wayland_constraint *con = self->link >= 0 && self->link < c->tbl[WLT_CONSTRAINT].count &&
                                                         c->constraints[self->link].live && c->constraints[self->link].id == self->id
                                                     ? &c->constraints[self->link] : 0;
                if (self->iface == WLI_zwp_locked_pointer_v1 && op == WLQ_zwp_locked_pointer_v1_set_cursor_position_hint)
                {
                        wayland.quiet++;
                        if (con) con->hinted = 1, con->hint_x = (int)a[0].i, con->hint_y = (int)a[1].i;
                        if (wayland.hold_row >= 0) wayland_row_do(wayland.hold_row, WAYLAND_DO_HINT, c->slot, 0);
                }
                else if (op == WLQ_zwp_locked_pointer_v1_destroy)
                {
                        if (con) wayland_constraint_release(c, con, 0), wayland_constraints_update();
                        self->link = -1;
                }
                return WAYLAND_OK;
        }
        case WLI_ext_idle_notifier_v1:
                if (op != WLQ_ext_idle_notifier_v1_get_idle_notification && op != WLQ_ext_idle_notifier_v1_get_input_idle_notification)
                        return WAYLAND_OK;
                {
                        long at = wayland_tbl_alloc(c, WLT_NOTIFY);
                        if (at < 0) return wayland_fail(c, self->id, WAYLAND_NO_MEMORY, "no memory for an idle notification");
                        c->notifies[at] = (struct wayland_notify){.live = 1, .input_only = op == WLQ_ext_idle_notifier_v1_get_input_idle_notification,
                                                                  .id = c->objects[a[0].slot].id, .timeout = (long)a[1].u, .since = wayland_now()};
                        return c->objects[a[0].slot].link = (int)at, wayland.idle_notifies++, WAYLAND_OK;
                }
        case WLI_ext_idle_notification_v1:
                if (op == WLQ_ext_idle_notification_v1_destroy) wayland_notify_free(c, self->link), self->link = -1;
                return WAYLAND_OK;
        case WLI_zwp_pointer_gestures_v1:
                if (op == WLQ_zwp_pointer_gestures_v1_get_pinch_gesture)
                {
                        c->pinches++;
                        for (long at = 0; at < wayland.row_made; at++)
                                if (wayland.row[at].client == c->slot) wayland_row_do(at, WAYLAND_DO_PINCH_WANT, 1, 0);
                }
                return WAYLAND_OK;
        case WLI_zwlr_layer_shell_v1:
                if (op == WLQ_zwlr_layer_shell_v1_destroy) return WAYLAND_OK;
                if (!(s = wayland_surface_of(c, a[1].slot))) return wayland_fail(c, self->id, WAYLAND_INVALID_OBJECT, "a surface is gone");
                if (s->role != WAYLAND_ROLE_NONE || s->xsurface || (s->flags & WAYLAND_SURFACE_XDG))
                        return wayland_fail(c, self->id, WLN_zwlr_layer_shell_v1_error_already_constructed, "the surface has a role");
                if (a[3].u > 3) return wayland_fail(c, self->id, WLN_zwlr_layer_shell_v1_error_invalid_layer, "unknown layer");
                s->role = WAYLAND_ROLE_TOPLEVEL, s->role_id = c->objects[a[0].slot].id, s->u.top.layer_shell = 1;
                s->u.top.layer_level = (long)a[3].u, s->u.top.layer_w = s->u.top.layer_h = 0, s->u.top.layer_configured = 0;
                s->u.top.layer_output = a[2].slot >= 0 && c->objects[a[2].slot].iface == WLI_wl_output && c->objects[a[2].slot].aux < WAYLAND_SCREENS
                                            ? (long)c->objects[a[2].slot].aux : 0;
                return wayland_surface_link(c, a[0].slot, s), WAYLAND_OK;
        case WLI_zwlr_layer_surface_v1:
                if (!(s = wayland_surface_of(c, self - c->objects)) || s->role_id != self->id) return WAYLAND_OK;
                if (op == WLQ_zwlr_layer_surface_v1_destroy)
                        wayland_xs_role_drop(c, s), s->role = WAYLAND_ROLE_NONE, s->u.top.layer_shell = 0;
                else if (op == WLQ_zwlr_layer_surface_v1_set_size)
                        s->u.top.layer_w = wayland_min((long)a[0].u, WAYLAND_DIMENSION), s->u.top.layer_h = wayland_min((long)a[1].u, WAYLAND_DIMENSION);
                else if (op == WLQ_zwlr_layer_surface_v1_set_layer)
                {
                        if (a[0].u > 3) return wayland_fail(c, self->id, WLN_zwlr_layer_shell_v1_error_invalid_layer, "unknown layer");
                        s->u.top.layer_level = (long)a[0].u;
                        if (s->row >= 0) wayland_row_do(s->row, WAYLAND_DO_LAYER, s->u.top.layer_level <= 1 ? -1 : 2, s->u.top.layer_output);
                }
                else if (op == WLQ_zwlr_layer_surface_v1_ack_configure)
                        s->flags |= WAYLAND_SURFACE_CONFIGURED;
                return WAYLAND_OK;
        case WLI_wp_tearing_control_v1:
                s = wayland_surface_of(c, self - c->objects);
                if (op == WLQ_wp_tearing_control_v1_destroy) { if (s) s->tearing = s->pending.async_hint = 0; }
                else if (a[0].u > WLN_wp_tearing_control_v1_presentation_hint_async)
                        return wayland_fail(c, self->id, WAYLAND_INVALID_METHOD, "no such presentation hint");
                else if (s) s->pending.async_hint = a[0].u == WLN_wp_tearing_control_v1_presentation_hint_async;
                return WAYLAND_OK;
        default: break;
        }
        if (!(s = wayland_surface_of(c, self - c->objects))) return WAYLAND_OK;
        if (wayland_sprop_apply(s, self->iface, op, a)) return WAYLAND_OK;
        if (self->iface == WLI_zwp_keyboard_shortcuts_inhibitor_v1)
                s->shortcuts = s->shortcut_active = s->escape_down = 0;
        else if (self->iface == WLI_zwp_xwayland_keyboard_grab_v1 && op == WLQ_zwp_xwayland_keyboard_grab_v1_destroy)
        {
                s->xgrab = 0;
                if (!s->shortcuts) s->shortcut_active = s->escape_down = 0;
        }
        else if (self->iface == WLI_wp_commit_timer_v1 && op == WLQ_wp_commit_timer_v1_set_timestamp)
        {
                unsigned long sec = ((unsigned long)a[0].u << 32) | a[1].u;
                if (s->pending.timestamp) return wayland_fail(c, self->id, WLN_wp_commit_timer_v1_error_timestamp_exists, "the commit has a time already");
                if (a[2].u >= 1000000000ul || sec > 18000000000ul)
                        return wayland_fail(c, self->id, WLN_wp_commit_timer_v1_error_invalid_timestamp, "the timestamp is not a time");
                s->pending.timestamp = sec * 1000000000ul + a[2].u;
                /* a client may hold its own commits, but not past a second from now: a far-future stamp froze the surface's stream */
                if (s->pending.timestamp > (unsigned long)wayland_now() * 1000000ul + 1000000000ul)
                        s->pending.timestamp = (unsigned long)wayland_now() * 1000000ul + 1000000000ul;
                if (!s->pending.timestamp) s->pending.timestamp = 1;
        }
        return WAYLAND_OK;
}

/* ---- wire argument decoder and request dispatcher ---- */

static const unsigned char wayland_kinds[256] = {
        ['h'] = WIRE_NONE, ['i'] = WIRE_WORD, ['f'] = WIRE_WORD, ['u'] = WIRE_WORD, ['o'] = WIRE_WORD, ['O'] = WIRE_WORD, ['n'] = WIRE_WORD,
        ['s'] = WIRE_TEXT | WIRE_ENDED, ['S'] = WIRE_TEXT | WIRE_ENDED | WIRE_EMPTY,
        ['N'] = WIRE_TEXT | WIRE_ENDED | WIRE_PAIR, ['a'] = WIRE_TEXT | WIRE_EMPTY,
};

static int wayland_args(struct wayland_client *c, const struct wayland_object *self, const struct wl_message *msg,
                        const unsigned char *at, long left, struct wayland_arg *a, long *begun, int *took_fd)
{
        static const char *const errs[] = {
                [WIRE_END_SHORT] = "the arguments are short", [WIRE_END_PAIR] = "the arguments are short",
                [WIRE_END_NOTHING] = "a string has no length", [WIRE_END_LONG] = "a string is longer than its message",
                [WIRE_END_OPEN] = "a string is not ended", [WIRE_END_ROOM] = "too many arguments",
                [WIRE_END_OVER] = "the arguments are longer than the request",
        };
        unsigned int records[WAYLAND_ARGS][4];
        unsigned long long walked = wire_arguments(msg->args, wayland_kinds, at, (unsigned long long)left, &records[0][0], WAYLAND_ARGS);
        long whole = (long)(walked & 255), why = (long)(walked >> 8);
        long count = whole + (why != WIRE_END_DONE && why != WIRE_END_ROOM && why != WIRE_END_OVER);

        *begun = 0;
        for (long n = 0; n < count; n++)
        {
                const unsigned char *kind = msg->args + 2 * n;
                struct wayland_arg *arg = &a[n];
                unsigned long word = records[n][0];

                *begun = n + 1, *arg = (struct wayland_arg){.slot = -1, .fd = -1};
                if (n == whole) break;
                switch (kind[0])
                {
                case 'h':
                        if (c->in_fd_count < 1) return wayland_fail(c, self->id, WAYLAND_INVALID_METHOD, "a descriptor is missing");
                        arg->fd = c->in_fds[0], *took_fd = 1;
                        for (long m = 1; m < c->in_fd_count; m++) c->in_fds[m - 1] = c->in_fds[m];
                        c->in_fd_count--;
                        break;
                case 'i': case 'f': arg->i = (long)(int)word, arg->u = word; break;
                case 'u': arg->u = word, arg->i = (long)word; break;
                case 'o': case 'O':
                        arg->u = word;
                        if (!word)
                        {
                                if (kind[0] == 'o') return wayland_fail(c, self->id, WAYLAND_INVALID_METHOD, "an object is null");
                                break;
                        }
                        if ((arg->slot = (int)wayland_slot_of(c, word)) < 0)
                                return wayland_fail(c, word, WAYLAND_INVALID_OBJECT, "no such object");
                        if (kind[1] && c->objects[arg->slot].iface != kind[1])
                                return wayland_fail(c, word, WAYLAND_INVALID_METHOD, "the object is another interface");
                        break;
                case 'n':
                        arg->u = word, arg->slot = word >= 1 && word < WAYLAND_IDS ? (int)wayland_object_new(c, word, kind[1], self->version) : -1;
                        if (arg->slot < 0) return wayland_new_fail(c, self->id, word);
                        break;
                default:
                        if (!word && kind[0] == 'S') break;
                        arg->text = at + records[n][1], arg->length = kind[0] == 'a' ? (long)word : (long)word - 1;
                        if (kind[0] == 'N') arg->version = (long)records[n][2], arg->u = records[n][3];
                        break;
                }
        }
        return why == WIRE_END_DONE ? WAYLAND_OK
                                    : wayland_fail(c, self->id, WAYLAND_INVALID_METHOD,
                                                   (unsigned long)why < sizeof errs / sizeof *errs && errs[why] ? errs[why] : "the table has an argument that cannot be read");
}

typedef int (*wayland_request_fn)(struct wayland_client *, struct wayland_object *, long, struct wayland_arg *);

static const wayland_request_fn wayland_handlers[WLI_COUNT] = {
        [WLI_wl_display] = wayland_display_request, [WLI_wl_registry] = wayland_registry_request,
        [WLI_wl_compositor] = wayland_compositor_request, [WLI_wl_surface] = wayland_surface_request,
        [WLI_wl_region] = wayland_region_request, [WLI_wl_shm] = wayland_shm_request,
        [WLI_wl_shm_pool] = wayland_pool_request, [WLI_wl_buffer] = wayland_buffer_request,
        [WLI_wl_subcompositor] = wayland_subcompositor_request, [WLI_wl_subsurface] = wayland_subsurface_request,
        [WLI_wl_seat] = wayland_seat_request,
        [WLI_wl_pointer] = wayland_seat_object_request, [WLI_wl_keyboard] = wayland_seat_object_request, [WLI_wl_touch] = wayland_seat_object_request,
        [WLI_xdg_wm_base] = wayland_wm_base_request, [WLI_xdg_positioner] = wayland_positioner_request,
        [WLI_xdg_surface] = wayland_xdg_surface_request, [WLI_xdg_toplevel] = wayland_toplevel_request, [WLI_xdg_popup] = wayland_popup_request,
        [WLI_zxdg_output_manager_v1] = wayland_output_manager_request,
        [WLI_xdg_activation_v1] = wayland_activation_request, [WLI_xdg_activation_token_v1] = wayland_activation_request,
        [WLI_zwp_linux_dmabuf_v1] = wayland_dmabuf_request, [WLI_zwp_linux_buffer_params_v1] = wayland_dmabuf_request, [WLI_zwp_linux_dmabuf_feedback_v1] = wayland_dmabuf_request,
        [WLI_wl_drm] = wayland_drm_request,
        [WLI_wp_linux_drm_syncobj_manager_v1] = wayland_syncobj_request, [WLI_wp_linux_drm_syncobj_timeline_v1] = wayland_syncobj_request, [WLI_wp_linux_drm_syncobj_surface_v1] = wayland_syncobj_request,
        [WLI_wp_cursor_shape_manager_v1] = wayland_cursor_shape_request, [WLI_wp_cursor_shape_device_v1] = wayland_cursor_shape_request,
        [WLI_zwp_text_input_manager_v3] = wayland_text_request, [WLI_zwp_text_input_v3] = wayland_text_request,
        [WLI_wp_viewporter] = wayland_viewport_request, [WLI_wp_viewport] = wayland_viewport_request,
        [WLI_wl_data_device_manager] = wayland_data_request, [WLI_wl_data_device] = wayland_data_request,
        [WLI_wl_data_source] = wayland_data_request, [WLI_wl_data_offer] = wayland_data_request,
        [WLI_zwp_primary_selection_device_manager_v1] = wayland_data_request, [WLI_zwp_primary_selection_device_v1] = wayland_data_request,
        [WLI_zwp_primary_selection_source_v1] = wayland_data_request, [WLI_zwp_primary_selection_offer_v1] = wayland_data_request,
        [WLI_zwp_relative_pointer_manager_v1] = wayland_ext_request, [WLI_zwp_relative_pointer_v1] = wayland_ext_request,
        [WLI_zwp_pointer_constraints_v1] = wayland_ext_request, [WLI_zwp_locked_pointer_v1] = wayland_ext_request, [WLI_zwp_confined_pointer_v1] = wayland_ext_request,
        [WLI_zwp_idle_inhibit_manager_v1] = wayland_ext_request, [WLI_zwp_keyboard_shortcuts_inhibit_manager_v1] = wayland_ext_request,
        [WLI_zwp_keyboard_shortcuts_inhibitor_v1] = wayland_ext_request, [WLI_wp_tearing_control_v1] = wayland_ext_request,
        [WLI_xwayland_shell_v1] = wayland_ext_request, [WLI_xwayland_surface_v1] = wayland_ext_request,
        [WLI_zwp_xwayland_keyboard_grab_manager_v1] = wayland_ext_request, [WLI_zwp_xwayland_keyboard_grab_v1] = wayland_ext_request,
        [WLI_zwp_pointer_gestures_v1] = wayland_ext_request, [WLI_ext_idle_notifier_v1] = wayland_ext_request, [WLI_ext_idle_notification_v1] = wayland_ext_request,
        [WLI_wp_fifo_manager_v1] = wayland_ext_request, [WLI_wp_fifo_v1] = wayland_ext_request,
        [WLI_wp_commit_timing_manager_v1] = wayland_ext_request, [WLI_wp_commit_timer_v1] = wayland_ext_request,
        [WLI_xdg_wm_dialog_v1] = wayland_ext_request, [WLI_xdg_dialog_v1] = wayland_ext_request, [WLI_xdg_system_bell_v1] = wayland_ext_request,
        [WLI_wp_tearing_control_manager_v1] = wayland_ext_request, [WLI_xdg_toplevel_icon_manager_v1] = wayland_ext_request,
        [WLI_xdg_toplevel_icon_v1] = wayland_ext_request, [WLI_wp_content_type_manager_v1] = wayland_ext_request,
        [WLI_wp_content_type_v1] = wayland_ext_request, [WLI_wp_alpha_modifier_v1] = wayland_ext_request,
        [WLI_wp_alpha_modifier_surface_v1] = wayland_ext_request, [WLI_wp_presentation] = wayland_ext_request,
        [WLI_wp_fractional_scale_manager_v1] = wayland_ext_request, [WLI_wp_fractional_scale_v1] = wayland_ext_request,
        [WLI_zwlr_layer_shell_v1] = wayland_ext_request, [WLI_zwlr_layer_surface_v1] = wayland_ext_request,
        [WLI_canvas_swapchain_v1] = wayland_ext_request,
};

HOT static int wayland_dispatch(struct wayland_client *c, const unsigned char *message, long size, unsigned long id, long op)
{
        long slot = wayland_slot_of(c, id), begun = 0;
        const struct wl_interface_info *info;
        const struct wl_message *msg;
        struct wayland_object *o;
        struct wayland_arg a[WAYLAND_ARGS];
        int status, took_fd = 0;

        wayland.heard++;
        if (unlikely(slot < 0)) return wayland_fail(c, id, WAYLAND_INVALID_OBJECT, "no such object");
        o = &c->objects[slot], info = &wl_interfaces[o->iface];
        if (unlikely(op >= info->requests || info->request[op].since > o->version))
                return wayland_fail(c, id, WAYLAND_INVALID_METHOD, "no such request");
        msg = &info->request[op];
        if (likely((status = wayland_args(c, o, msg, message + 8, size - 8, a, &begun, &took_fd)) >= 0))
        {
                wayland_request_fn handler = o->iface < WLI_COUNT ? wayland_handlers[o->iface] : 0;
                status = handler ? handler(c, o, op, a) : WAYLAND_OK;
        }
        if (unlikely(took_fd))
                for (long n = 0; n < begun; n++)
                        if (a[n].fd >= 0) wayland_fd_close(a[n].fd);
        if (status >= 0 && msg->destructor && o->iface && c->objects[slot].id == id)
                wayland_object_delete(c, slot);
        return status;
}

/* ---- compositor-side shim API ---- */

struct wayland_stats { long clients, objects, surfaces, pools, buffers, fds, rows; };

WAYLAND_API void wayland_stats_get(struct wayland_stats *into) { *into = (struct wayland_stats){wayland.clients, wayland.objects, wayland.surfaces, wayland.pools, wayland.buffers, wayland.fds, wayland.rows}; }
WAYLAND_API void wayland_xwayland_user(long uid) { wayland.xwayland_uid = uid; }
WAYLAND_API void wayland_xwayland_client(long slot) { struct wayland_client *c = wayland_live_client(slot); if (c && !c->xspace) c->xspace = 1, wayland_outputs_send(c); }
WAYLAND_API int wayland_row_xoverride(long row) { struct wayland_client *c = 0; struct wayland_surface *s = wayland_row_surface(row, &c); return s && s->xsurface && s->u.xwin.popup; }
WAYLAND_API long wayland_dead(long slot) { struct wayland_client *c = wayland_client_of(slot); return c ? c->dead : 2; }
WAYLAND_API long wayland_stalled(long slot) { struct wayland_client *c = wayland_client_of(slot); return c && !c->dead && c->stalled; }
WAYLAND_API long wayland_heard(long *quiet) { return *quiet = wayland.quiet, wayland.heard; }
WAYLAND_API int wayland_out_dirty(long slot) { return (unsigned long)slot < (unsigned long)wayland.client_slots && ((wayland.out_dirty[(unsigned long)slot >> 6] >> ((unsigned long)slot & 63u)) & 1u); }
WAYLAND_API long wayland_out_dirty_next(long from)
{
        unsigned long limit = (unsigned long)(wayland.client_slots > 0 ? wayland.client_slots : 0);
        unsigned long wi, words = (limit + 63ul) >> 6;
        unsigned long long w;

        if (from < 0)
                from = 0;
        if ((unsigned long)from >= limit || !wayland.out_dirty)
                return -1;
        wi = (unsigned long)from >> 6;
        w = wayland.out_dirty[wi] & (~0ULL << ((unsigned long)from & 63u));
        for (;;)
        {
                if (w)
                {
                        unsigned long idx = (wi << 6) + (unsigned long)wayland_first_bit(w);
                        return idx < limit ? (long)idx : -1;
                }
                if (++wi >= words)
                        return -1;
                w = wayland.out_dirty[wi];
        }
}
WAYLAND_API int wayland_out_backlog(long slot) { struct wayland_client *c = wayland_live_client(slot); return c && c->out_length > WAYLAND_OUT_HIGH; }
WAYLAND_API long wayland_client_slots(void) { return wayland.client_slots; }
WAYLAND_API long wayland_client_count(void) { return wayland.client_slots; }
WAYLAND_API long wayland_client_pid(long slot) { struct wayland_client *c = wayland_client_of(slot); return c && c->live ? c->pid : -1; }
WAYLAND_API long wayland_row_owner(long row) { return row >= 0 && row < wayland.row_made ? wayland.row[row].client : -1; }
// The client whose buffers the row shows: its stand-in's when it has one, else its own.
WAYLAND_API long wayland_row_painter(long row) { return wayland_row_stood(row) ? wayland.row[row].stand_client : wayland_row_owner(row); }
WAYLAND_API long wayland_idle_inhibited(void) { return wayland_inhibitors_now(); }
WAYLAND_API void wayland_input_seen(void) { if (wayland.host) wayland.input_at = wayland_now(); }
WAYLAND_API void wayland_hint_counts(long *bells, long *icons) { *bells = wayland.bells, *icons = wayland.icons; }
WAYLAND_API long wayland_row_shortcuts(long row) { struct wayland_client *c = 0; struct wayland_surface *s = wayland_row_surface(row, &c); return s && (s->shortcuts || s->xgrab) ? s->shortcut_active : 0; }

WAYLAND_API int wayland_start(const struct wayland_host *host)
{
        if (wayland.host || !host || !host->alloc || !host->release || !host->map || !host->remap || !host->unmap ||
            !host->close || !host->row_open || !host->row_close || !host->row_picture || !host->row_title || !host->row_do ||
            !host->row_facts || !host->screens || !host->now || !host->keymap || !host->repeat || !host->seat || !host->locks ||
            !host->clip_type || !host->clip_pull || !host->clip_abort || !host->clip_push || !host->clip_clear || !host->dmabuf_info ||
            !host->dmabuf_table || !host->dmabuf_map || !host->pointer_hold || !host->random)
                return -1;
        memory_zero(&wayland, sizeof wayland);
        wayland.pointer_row = wayland.pointer_surface = wayland.keyboard_row = -1;
        wayland.drag.client = wayland.drag.source = wayland.drag.target = wayland.drag.surface = wayland.drag.offer = wayland.drag.xtarget = -1;
        wayland.cursor_client = wayland.cursor_surface = wayland.xwayland_uid = -1;
        wayland.hold_row = wayland.hint_row = wayland.counts_row = wayland.unheld_row = -1;
        wayland.host = host, wayland.input_at = wayland_now();
        wayland.screens = wayland_max(0, host->screens(host->context, wayland.screen, WAYLAND_SCREENS));
        return wayland_dmabuf_load(), 0;
}

WAYLAND_API void wayland_syncobj_host(const struct wayland_syncobj_host *syncobj)
{
        if (syncobj) wayland.syncobj = *syncobj;
        else memory_zero(&wayland.syncobj, sizeof wayland.syncobj);
        wayland_syncobj_probe();
}

WAYLAND_API long wayland_connect(long pid, long uid)
{
        struct wayland_client *c;
        long slot = -1;

        if (!wayland.host) return -1;
        for (long at = 0; at < wayland.client_slots && slot < 0; at++)
                if (!wayland.client[at]) slot = at;
        if (slot < 0 && (slot = wayland_client_slot_grow()) < 0) return -1;
        if (!(c = (struct wayland_client *)wayland.host->alloc(wayland.host->context, (long)sizeof *c)))
        {
                if (!wayland.clients) wayland_client_slots_drop();
                return -1;
        }
        c->live = 1, c->slot = slot, c->pid = pid, c->uid = uid, c->stalled_sync_handle = -1;
        wayland_tables_init(c), wayland.client[slot] = c, wayland.clients++;
        c->out_length = WAYLAND_OUT - WAYLAND_EVENT;
        if (!wayland_out_grow(c)) c->out = 0;
        c->out_length = 0;
        if (!c->out || wayland_object_new(c, 1, WLI_wl_display, 1) < 0)
        {
                wayland_tables_drop(c), wayland.client[slot] = 0, wayland.clients--;
                wayland.host->release(wayland.host->context, c, (long)sizeof *c);
                if (!wayland.clients) wayland_client_slots_drop();
                return -1;
        }
        return slot;
}

static void wayland_out_fds_drop(struct wayland_client *c, long drop, long shift)
{
        struct wayland_outfd *f = c->out_fds;
        long kept = 0;
        for (long at = 0; at < drop; at++)
                if (f[at].owned) wayland_fd_close(f[at].fd);
        for (long at = drop; at < c->out_fd_count; at++, kept++)
                f[kept] = f[at], f[kept].at -= shift;
        c->out_fd_count = kept;
}

WAYLAND_API void wayland_drop(long slot)
{
        struct wayland_client *c = wayland_client_of(slot);
        if (!c) return;
        wayland.waiting -= c->waiting;
        wayland_touch_forget(slot), wayland_token_forget(slot);
        if (wayland.drag.live && wayland.drag.client == slot) wayland.drag.source = -1, wayland_drag_end(1);
        else if (wayland.drag.live && wayland.drag.target == slot)
        {
                if (wayland.drag.wl_drop_sent) wayland.drag.target = -1, wayland_drag_end(0);
                else wayland.drag.target = wayland.drag.surface = wayland.drag.offer = -1;
        }
        if (wayland.cursor_client == slot) wayland.cursor_client = wayland.cursor_surface = -1, wayland.cursor_serial++;
        for (long at = 0; at < c->tbl[WLT_SURFACE].count; at++) if (c->surfaces[at].live) wayland_row_release(c, &c->surfaces[at]), wayland.surfaces--;
        wayland_texts_drop(c);
        for (long at = 0; at < c->tbl[WLT_POOL].count; at++) if (c->pools[at].live) wayland_pool_drop(c, at);
        for (long at = 0; at < c->tbl[WLT_TIMELINE].count; at++) if (c->timelines[at].live) wayland_timeline_drop(c, at);
        for (long at = 0; at < c->tbl[WLT_PARAMS].count; at++) if (c->params[at].live) wayland_params_free(c, &c->params[at]);
        for (long at = 0; at < c->tbl[WLT_CONSTRAINT].count; at++) if (c->constraints[at].live) c->constraints[at].live = 0, wayland.constraints--;
        for (long at = 0; at < c->tbl[WLT_BUFFER].count; at++) if (c->buffers[at].live) wayland.buffers--;
        for (long at = 0; at < c->tbl[WLT_OBJECT].count; at++) if (c->objects[at].iface) wayland.objects--;
        for (long at = 0; at < c->in_fd_count; at++) wayland_fd_close(c->in_fds[at]);
        wayland_out_fds_drop(c, c->out_fd_count, 0);
        for (long which = 0; which < 2; which++)
        {
                if (c->pending[which]) wayland.host->clip_abort(wayland.host->context, c->slot, which);
        }
        for (long at = 0; at < c->tbl[WLT_NOTIFY].count; at++) wayland_notify_free(c, at);
        wayland_out_clear(slot), wayland.client[slot] = 0, wayland.clients--, wayland_tables_drop(c);
        wayland.host->release(wayland.host->context, c, (long)sizeof *c);
        if (!wayland.clients) wayland_client_slots_drop();
}

WAYLAND_API void wayland_stop(void)
{
        if (!wayland.host) return;
        for (long slot = 0; slot < wayland.client_slots; slot++) wayland_drop(slot);
        wayland_table_drop(&wayland.token_table), wayland_table_drop(&wayland.drag.x_types);
        wayland.token = 0, wayland.tokens = 0, wayland.drag.x_bytes = 0;
        memory_zero(&wayland.syncobj, sizeof wayland.syncobj);
        wayland.syncobj_ok = 0, wayland.drm_node[0] = 0, wayland.host = 0;
}

static void wayland_stall(struct wayland_client *c, const unsigned char *head, long head_size, const unsigned char *rest, long rest_length)
{
        unsigned char *dst = c->stash + head_size;

        if (head_size < 0 || rest_length < 0 || head_size + rest_length > WAYLAND_STASH)
        {
                wayland_fail(c, 1, WAYLAND_NO_MEMORY, "more sent than can wait for a buffer");
                return;
        }
        if (rest >= c->stash && rest < c->stash + WAYLAND_STASH)
        {
                if (dst > rest)
                        for (long i = rest_length; i > 0; i--) dst[i - 1] = rest[i - 1];
                else if (dst < rest)
                        for (long i = 0; i < rest_length; i++) dst[i] = rest[i];
                if (head_size > 0)
                        wayland_copy(c->stash, head, head_size);
        }
        else
                wayland_copy(c->stash, head, head_size), wayland_copy(dst, rest, rest_length);
        c->stash_length = head_size + rest_length, c->stalled = 1, c->stalled_at = wayland_now();
}

static int wayland_pump(struct wayland_client *c, const unsigned char *bytes, long length)
{
        int status = WAYLAND_OK;

        while (length > 0 && !c->dead && !c->stalled)
        {
                const unsigned char *msg;
                long size;
                unsigned int head[2];
                int partial = c->in_length != 0;

                if (partial)
                {
                        long want = c->in_length < 8 ? 8 - c->in_length : (long)(wayland_get(c->in + 4) >> 16) - c->in_length;
                        long take = wayland_min(want, length);
                        wayland_copy(c->in + c->in_length, bytes, take), c->in_length += take, bytes += take, length -= take;
                        if (c->in_length < 8) continue;
                        msg = c->in, size = (long)wire_message(c->in, (unsigned long long)c->in_length, WAYLAND_MESSAGE, head);
                }
                else
                        msg = bytes, size = (long)wire_message(bytes, (unsigned long long)length, WAYLAND_MESSAGE, head);
                if (size < 0)
                        return c->in_length = 0, wayland_fail(c, 1, WAYLAND_INVALID_METHOD, "a message of an impossible size");
                if (!size)
                {
                        if (!partial) { wayland_copy(c->in, bytes, length), c->in_length = length; break; }
                        continue;
                }
                c->in_length = 0, status = wayland_dispatch(c, msg, size, head[0], (long)head[1]);
                if (status == WAYLAND_STALL)
                {
                        wayland_stall(c, msg, partial ? size : 0, bytes, length), status = WAYLAND_OK;
                        break;
                }
                if (!partial) bytes += size, length -= size;
        }
        return c->dead ? WAYLAND_DEAD : status;
}

WAYLAND_API int wayland_feed(long slot, const unsigned char *bytes, long length, const long *fds, long count)
{
        struct wayland_client *c = wayland_live_client(slot);

        for (long at = 0; at < count; at++)
                if (c && !c->dead && c->in_fd_count < WAYLAND_FDS)
                        c->in_fds[c->in_fd_count++] = fds[at], wayland.fds++;
                else
                {
                        wayland.host->close(wayland.host->context, fds[at]);
                        if (c && !c->dead) wayland_fail(c, 1, WAYLAND_INVALID_METHOD, "too many descriptors");
                }
        if (!c || c->dead) return WAYLAND_DEAD;
        if (c->stalled)
        {
                if (c->stash_length + length > WAYLAND_STASH)
                        return wayland_fail(c, 1, WAYLAND_NO_MEMORY, "more sent than can wait for a buffer");
                return wayland_copy(c->stash + c->stash_length, bytes, length), c->stash_length += length, WAYLAND_OK;
        }
        return wayland_pump(c, bytes, length);
}

WAYLAND_API long wayland_stall_due(long slot, long *fd)
{
        struct wayland_client *c = wayland_live_client(slot);
        long cap;

        *fd = -1;
        if (!c || c->dead || !c->stalled) return 0;
        if (c->hold) return c->hold_at;
        cap = c->stalled_at + WAYLAND_STALL_MS + 1;
        if (c->stalled_sync_handle >= 0)
        {
                long efd = wayland.syncobj.eventfd ? wayland.syncobj.eventfd(wayland.syncobj.context, c->stalled_sync_handle, c->stalled_sync_point) : -1;
                return efd >= 0 ? (*fd = efd, cap) : wayland_min(wayland_now() + 1, cap);
        }
        if (c->stalled_fd < 0)
                return c->budget_surface >= 0 ? c->surfaces[c->budget_surface].budget_at + WAYLAND_BUDGET_MS : 0;
        return *fd = c->stalled_fd, cap;
}

WAYLAND_API int wayland_resume(long slot)
{
        struct wayland_client *c = wayland_live_client(slot);
        long length;

        if (!c || c->dead || !c->stalled) return 0;
        if (c->hold)
        {
                if (c->surfaces[c->hold_surface].live && wayland_hold_wanted(c, &c->surfaces[c->hold_surface])) return 1;
                c->hold = 0;
        }
        if (c->stalled_sync_handle >= 0
                ? (wayland.syncobj.ready && !wayland.syncobj.ready(wayland.syncobj.context, c->stalled_sync_handle, c->stalled_sync_point))
            : c->stalled_fd < 0
                ? c->budget_surface >= 0 && c->surfaces[c->budget_surface].live && !wayland_budget_left(&c->surfaces[c->budget_surface])
                : wayland.host->dmabuf_ready && !wayland.host->dmabuf_ready(wayland.host->context, c->stalled_fd))
        {
                if ((c->stalled_fd >= 0 || c->stalled_sync_handle >= 0) && wayland_now() - c->stalled_at > WAYLAND_STALL_MS)
                        return c->stalled = 0, c->stalled_sync_handle = -1, c->stash_length = 0,
                               wayland_fail(c, 1, WAYLAND_IMPLEMENTATION, "the buffer's writer never finished"), 0;
                return 1;
        }
        length = c->stash_length, c->stalled_sync_handle = -1, c->stash_length = 0, c->stalled = 0;
        wayland_pump(c, c->stash, length);
        return c->stalled ? 1 : 0;
}

static long wayland_out_fd_view[WAYLAND_OUT_FDS];

WAYLAND_API long wayland_out(long slot, const unsigned char **bytes, const long **fds, long *fd_count)
{
        struct wayland_client *c = wayland_live_client(slot);
        *bytes = 0, *fds = 0, *fd_count = 0;
        if (!c) return 0;
        *bytes = c->out, *fd_count = wayland_out_fd_batch(c), *fds = wayland_out_fd_view;
        for (long at = 0; at < *fd_count; at++) wayland_out_fd_view[at] = c->out_fds[at].fd;
        return *fd_count < c->out_fd_count ? c->out_fds[*fd_count].at : c->out_length;
}

static __attribute__((noinline)) void wayland_sent_part(struct wayland_client *c, long slot, long count)
{
        long at = count;
        for (; at + 8 <= c->out_length; at += 8)
                *(wayland_wire_long *)(c->out + at - count) = *(const wayland_wire_long *)(c->out + at);
        for (; at < c->out_length; at++) c->out[at - count] = c->out[at];
        c->out_length -= count, wayland_out_fds_drop(c, wayland_out_fd_batch(c), count), wayland_feedback_flush(c);
        if (!c->out_length && !c->dead && !c->feedback_pending) wayland_out_clear(slot);
}

WAYLAND_API void wayland_sent(long slot, long count)
{
        struct wayland_client *c = wayland_live_client(slot);
        if (!c || count <= 0 || count > c->out_length) return;
        if (count == c->out_length && !c->out_fd_count && !c->feedback_pending)
        {
                c->out_length = 0;
                if (!c->dead) wayland_out_clear(slot);
                return;
        }
        wayland_sent_part(c, slot, count);
}

WAYLAND_API void wayland_screens_changed(void)
{
        if (!wayland.host) return;
        long was = wayland.screens;
        wayland.screens = wayland_max(0, wayland.host->screens(wayland.host->context, wayland.screen, WAYLAND_SCREENS));
        for (long slot = 0; slot < wayland.client_slots; slot++)
        {
                struct wayland_client *c = wayland.client[slot];
                if (!c) continue;
                for (long screen = wayland.screens; screen < was && screen < WAYLAND_SCREENS; screen++)
                {
                        for (long at = 0; at < c->tbl[WLT_SURFACE].count; at++)
                                if (c->surfaces[at].live && (c->surfaces[at].entered & (1u << screen)))
                                {
                                        for (long o = 0; o < c->tbl[WLT_OBJECT].count; o++)
                                                if (c->objects[o].iface == WLI_wl_output && c->objects[o].aux == screen)
                                                        wayland_ev(c, c->surfaces[at].id, WLE_wl_surface_leave, c->objects[o].id);
                                        c->surfaces[at].entered &= ~(1u << screen);
                                }
                        for (long o = 0; o < c->tbl[WLT_OBJECT].count; o++)
                                if ((c->objects[o].iface == WLI_wl_output || c->objects[o].iface == WLI_zxdg_output_v1) && c->objects[o].aux == screen)
                                        c->objects[o].aux = 0xffff;
                }
                for (long at = 0; at < c->tbl[WLT_OBJECT].count && wayland.screens != was; at++)
                        if (c->objects[at].iface == WLI_wl_registry)
                        {
                                for (long s = was; s < wayland.screens; s++) wayland_global_send(c, c->objects[at].id, wayland_output_name(s));
                                for (long s = wayland.screens; s < was; s++) wayland_global_remove_send(c, c->objects[at].id, wayland_output_name(s));
                        }
                wayland_outputs_send(c);
        }
        for (long s = wayland.screens; s < was && s < WAYLAND_SCREENS; s++) wayland.output_age[s]++;
}

WAYLAND_API void wayland_scales_sweep(void)
{
        for (long row = 0; wayland.host && row < wayland.row_made; row++)
        {
                struct wayland_client *c;
                struct wayland_surface *root = wayland_row_surface(row, &c);
                struct wayland_facts facts = {0};
                if (!root || root->row != row || wayland_row_facts(row, &facts) < 0) continue;
                wayland_surface_screens(c, root, wayland_screen_at(facts.x, facts.y));
                long percent = facts.scale > 0 ? facts.scale : 100;
                if (wayland.row[row].percent == percent) continue;
                wayland.row[row].percent = percent;
                for (struct wayland_surface *o = root; o; o = wayland_tree_next(c, root, o))
                        wayland_scale_tell(c, o, percent);
                if (root->role == WAYLAND_ROLE_TOPLEVEL && root->u.top.told && wayland_surface_unit(root) != root->told_unit && (facts.width > 0 || facts.height > 0))
                        wayland_configure(c, root, 0);
        }
}

WAYLAND_API void wayland_row_wake(long row)
{
        struct wayland_client *c;
        struct wayland_surface *s;
        struct wayland_facts facts;

        if (!wayland.host || !(s = wayland_row_surface(row, &c)) || s->row != row || wayland_row_facts(row, &facts) < 0)
                return;
        if (facts.closing && facts.closing != s->closes)
        {
                int popup = s->role == WAYLAND_ROLE_POPUP;
                if (wayland_find(c, s->role_id, popup ? WLI_xdg_popup : WLI_xdg_toplevel) >= 0)
                        wayland_ev0(c, s->role_id, popup ? WLE_xdg_popup_popup_done : WLE_xdg_toplevel_close);
                s->closes = facts.closing;
        }
        if (s->role != WAYLAND_ROLE_TOPLEVEL) return;
        if (!(facts.states & WAYLAND_STATE_FULLSCREEN)) s->u.top.want_fullscreen = 0;
        if (!(facts.states & (WAYLAND_STATE_MAXIMIZED | WAYLAND_STATE_FULLSCREEN))) s->u.top.want_maximized = 0;
        if (s->u.top.told && (facts.width != s->u.top.told_width || facts.height != s->u.top.told_height || facts.states != s->u.top.told_states) &&
            !wayland_facts_freed(s, &facts))
        {
                if (facts.states == s->u.top.told_states && (facts.states & WAYLAND_STATE_RESIZING) && s->u.top.configure_frame == wayland.frames + 1)
                        s->u.top.configure_late = 1;
                else
                        wayland_configure(c, s, 0);
        }
}

static void wayland_callbacks_due(long now)
{
        wayland.answered = now, wayland.late_quiet = 0;
        for (long row = 0; row < wayland.row_made; row++)
        {
                struct wayland_client *c, *sc;
                struct wayland_surface *s = wayland_row_surface(row, &c), *st = wayland_row_stand(row, &sc);
                if (s && c->waiting && wayland_tree_has_cb(c, s, WLCB_READY) && !wayland_row_suspended(row))
                        for (struct wayland_surface *o = s; o; o = wayland_tree_next(c, s, o))
                                wayland_callbacks_done(c, o, now);
                if (st && st->cbq[WLCB_READY].count && !wayland_row_suspended(row))
                        wayland_callbacks_done(sc, st, now);
        }
}

WAYLAND_API void wayland_frame(void)
{
        if (!wayland.host) return;
        wayland.frames++;
        for (long row = 0; row < wayland.row_made; row++)
        {
                struct wayland_client *c;
                struct wayland_surface *s = wayland_row_surface(row, &c);
                if (s && s->role == WAYLAND_ROLE_TOPLEVEL && s->u.top.configure_late)
                        s->u.top.configure_late = 0, wayland_configure_owed(c, s);
        }
        if (!wayland.calling) wayland.latches++, wayland_callbacks_due(wayland_now());
        wayland.calling = 0;
}

WAYLAND_API void wayland_latch_defer(int defer_callbacks)
{
        if (!wayland.host) return;
        wayland.latches++, wayland.framing = 1, wayland.calling = defer_callbacks ? 0 : 1;
        for (long row = 0; row < wayland.row_made; row++)
        {
                struct wayland_client *c = 0, *sc = 0;
                struct wayland_surface *s = wayland_row_surface(row, &c), *st = wayland_row_stand(row, &sc);
                for (int which = 0; which < 2; which++, s = st, c = sc)
                {
                        if (!s) continue;
                        wayland_lent_drop(c, &s->lent_latched);
                        if (s->lent) s->lent_latched = s->lent, c->buffers[s->lent - 1].held++;
                        if (c->presenting_surfaces)
                                for (struct wayland_surface *o = s; o; o = wayland_tree_next(c, s, o))
                                        o->presentings_framed = o->cbq[WLCB_PRESENTING].count;
                }
        }
        if (!defer_callbacks)
                wayland_callbacks_due(wayland_now());
}

WAYLAND_API void wayland_latch(void) { wayland_latch_defer(0); }

/*
        The frame callbacks waiting on a shown window: the time (ms, the host's clock) they were last answered, or -1 when none waits. Every
        present answers them (at its latch, its flip or its job's end) and an idle frame does; one no present answered (a flip whose event is
        late, a mode set, screens being taken again, a card job held) kept Xwayland's Present waiting for its msc to move, which it does on a
        frame callback or after a second (gamebench: every wake's mode set froze fullscreen HL2 for exactly 1.0 s). The host answers them late
        (wayland_callbacks_late) past a few periods of that. The surfaces waiting are counted as they change (wayland.waiting), so the
        loop's look each turn walks nothing; a late answer that answered none (every one waiting is a window put away, told of no frame)
        is quiet until the count changes, so a minimized window's callback does not wake the loop every few periods.
*/
WAYLAND_API long wayland_callbacks_waiting(void)
{
        return wayland.host && wayland.waiting > 0 && wayland.waiting != wayland.late_quiet ? wayland.answered : -1;
}

WAYLAND_API void wayland_callbacks_late(void)
{
        long before = wayland.waiting;

        if (!wayland.host)
                return;
        wayland_callbacks_due(wayland_now());
        wayland.late_quiet = before > 0 && wayland.waiting == before ? before : 0;
}

static void wayland_presentings_done(struct wayland_client *c, struct wayland_surface *s, unsigned long when, unsigned long refresh,
                                     unsigned long seq, unsigned long flags)
{
        unsigned long sec = when / 1000000000ul, nsec = when % 1000000000ul;
        struct wayland_cbq *pq = &s->cbq[WLCB_PRESENTING];
        long k = wayland.framing && s->presentings_framed < pq->count ? s->presentings_framed : pq->count;

        if (k && k == pq->count) c->presenting_surfaces--;
        for (long at = 0; at < k; at++)
        {
                unsigned int id = wayland_cb_pop(c, pq);
                long slot = wayland_find(c, id, WLI_wp_presentation_feedback);
                if (slot < 0) continue;
                wayland_ev(c, id, WLE_wp_presentation_feedback_presented, (unsigned int)(sec >> 32), (unsigned int)sec,
                           (unsigned int)nsec, (unsigned int)refresh, (unsigned int)(seq >> 32), (unsigned int)seq, (unsigned int)flags);
                wayland_object_delete(c, slot);
        }
        s->presentings_framed = 0;
}

static void wayland_window_gone(struct wayland_client *c, struct wayland_surface *root, int dmabufs)
{
        for (struct wayland_surface *s = root; s; s = wayland_tree_next(c, root, s))
                if (s->lent_gone && (dmabufs || !c->pools[c->buffers[s->lent_gone - 1].pool].dmabuf))
                        wayland_lent_drop(c, &s->lent_gone);
}

static void wayland_presented_rows(unsigned long when, unsigned long refresh, unsigned long seq, unsigned long flags, int drawn, long zero_row)
{
        for (long row = 0; row < wayland.row_made; row++)
        {
                struct wayland_client *c = 0, *sc = 0;
                struct wayland_surface *s = wayland_row_surface(row, &c), *st = wayland_row_stand(row, &sc);
                struct wayland_surface *zero = st && wayland.row[row].lifted ? st : s; // the surface whose buffer the screen shows
                for (int which = 0; which < 2; which++, s = st, c = sc)
                {
                        if (!s) continue;
                        if (wayland_row_suspended(row))
                        {
                                wayland_lent_drop(c, &s->lent_latched), wayland_lent_drop(c, &s->lent_shown), wayland_window_gone(c, s, 1);
                                continue;
                        }
                        if (drawn)
                        {
                                if (row == zero_row)
                                {
                                        unsigned int old_shown = s->lent_shown;
                                        s->lent_shown = s->lent_latched, s->lent_latched = 0, wayland_lent_drop(c, &old_shown);
                                }
                                else
                                        wayland_lent_drop(c, &s->lent_latched), wayland_lent_drop(c, &s->lent_shown);
                                wayland_window_gone(c, s, 1);
                        }
                        if (c->presenting_surfaces)
                                for (struct wayland_surface *o = s; o; o = wayland_tree_next(c, s, o))
                                        wayland_presentings_done(c, o, when, refresh, seq,
                                                                 o == (which ? s : zero) && row == zero_row ? flags | WLN_wp_presentation_feedback_kind_zero_copy : flags);
                }
        }
        for (long slot = 0; slot < wayland.client_slots; slot++)
                if (wayland.client[slot] && wayland.client[slot]->gone.count) wayland_gone_drawn(wayland.client[slot], 1);
        wayland.framing = 0;
}

static void wayland_drawn_rows(int dmabuf_busy_row)
{
        for (long row = 0; wayland.host && row < wayland.row_made; row++)
        {
                struct wayland_client *c, *sc;
                struct wayland_surface *s = wayland_row_surface(row, &c), *st = wayland_row_stand(row, &sc);
                if (s && (s->lent_gone || wayland_has_children(s)) && !wayland_row_suspended(row))
                        wayland_window_gone(c, s, !(dmabuf_busy_row < -1 || (int)row == dmabuf_busy_row));
                if (st && st->lent_gone && !wayland_row_suspended(row))
                        wayland_window_gone(sc, st, !(dmabuf_busy_row < -1 || (int)row == dmabuf_busy_row));
        }
        for (long slot = 0; wayland.host && slot < wayland.client_slots; slot++)
                if (wayland.client[slot] && wayland.client[slot]->gone.count) wayland_gone_drawn(wayland.client[slot], 0);
}

WAYLAND_API void wayland_drawn(void) { wayland_drawn_rows(-2); }
WAYLAND_API void wayland_drawn_composed(int busy_row) { wayland_drawn_rows(busy_row); }
WAYLAND_API void wayland_presented(unsigned long when, unsigned long refresh, unsigned long seq, unsigned long flags, long zero_row) { if (wayland.host) wayland_presented_rows(when, refresh, seq, flags, 1, zero_row); }
WAYLAND_API void wayland_presented_idle(unsigned long when, unsigned long refresh, unsigned long seq, unsigned long flags) { if (wayland.host) wayland.framing = 0, wayland_presented_rows(when, refresh, seq, flags, 0, -1); }

WAYLAND_API long wayland_frame_wanted(void)
{
        for (long slot = 0; slot < wayland.client_slots; slot++)
                if (wayland.client[slot] && wayland.client[slot]->stalled && wayland.client[slot]->hold && !wayland.client[slot]->hold_at)
                        return 1;
        for (long row = 0; wayland.host && row < wayland.row_made; row++)
        {
                struct wayland_client *c = wayland_client_of(wayland.row[row].client);
                struct wayland_surface *s = c ? &c->surfaces[wayland.row[row].surface] : 0;
                int live = s && s->live;
                struct wayland_client *sc;
                struct wayland_surface *st = wayland_row_stand(row, &sc);
                if ((c && c->gone.count) || (live && s->role == WAYLAND_ROLE_TOPLEVEL && s->u.top.configure_late)) return 1;
                if (st && ((sc->waiting && st->cbq[WLCB_READY].count) || (sc->presenting_surfaces && st->cbq[WLCB_PRESENTING].count)) &&
                    !wayland_row_suspended(row))
                        return 1;
                if (!live) continue;
                if (s->lent_gone && wayland_row_suspended(row)) return 1;
                if (((c->waiting && wayland_tree_has_cb(c, s, WLCB_READY)) ||
                     (c->presenting_surfaces && wayland_tree_has_cb(c, s, WLCB_PRESENTING))) && !wayland_row_suspended(row))
                        return 1;
        }
        return 0;
}

// name, field, fallback, and whether the hint is the picture's (a stand-in's when the row has one) or the window's.
#define WAYLAND_ROW_HINTS(X) \
        X(ALPHA, s->cur.alpha, 0xffffffffu, 1) X(CONTENT, s->content, WLN_wp_content_type_v1_type_none, 1) \
        X(FIFO, s->cur.fifo_wait, 0, 1) X(TIMESTAMP, s->cur.timestamp, 0, 1) X(ATTENTION, s->attention, 0, 0) \
        X(ASYNC, s->cur.async_hint, 0, 1) X(ICON, s->cur.icon, 0, 0) X(MODAL, s->modal, 0, 0)

enum wayland_row_hint {
#define X(name, field, fallback, picture) WAYLAND_HINT_##name,
        WAYLAND_ROW_HINTS(X)
#undef X
};

WAYLAND_API unsigned long wayland_row_hint(long row, enum wayland_row_hint hint)
{
        struct wayland_client *c = 0, *sc = 0;
        struct wayland_surface *s = wayland_row_surface(row, &c), *st = wayland_row_stand(row, &sc);
        switch (hint)
        {
#define X(name, field, fallback, picture) \
        case WAYLAND_HINT_##name: s = (picture) && st ? st : s; return s ? (unsigned long)(field) : (unsigned long)(fallback);
                WAYLAND_ROW_HINTS(X)
#undef X
        }
        return 0;
}

WAYLAND_API void wayland_pointer_pinch(long row, long phase, long scale, long fx, long fy)
{
        struct wayland_client *c = 0;
        struct wayland_surface *s;
        long time, dx = 0, dy = 0;
        unsigned long serial = 0;

        if (!wayland.host || row != wayland.pointer_row || !(s = wayland_row_surface(row, &c)) || !c->pinches) return;
        time = wayland_now();
        if (phase == 1 || phase == 2) serial = wayland_input_serial(c);
        if (phase != 2)
        {
                if (phase != 1) dx = fx - wayland.pinch_x, dy = fy - wayland.pinch_y;
                wayland.pinch_x = fx, wayland.pinch_y = fy;
        }
        for (long at = 0, left = c->pinches; at < c->tbl[WLT_OBJECT].count && left > 0; at++)
        {
                struct wayland_object *o = &c->objects[at];
                if (o->iface != WLI_zwp_pointer_gesture_pinch_v1 || !o->id) continue;
                left--;
                if (phase == 1) wayland_ev(c, o->id, WLE_zwp_pointer_gesture_pinch_v1_begin, (unsigned int)serial, (unsigned int)time, s->id, 2u);
                else if (phase == 2) wayland_ev(c, o->id, WLE_zwp_pointer_gesture_pinch_v1_end, (unsigned int)serial, (unsigned int)time, 0u);
                else wayland_ev(c, o->id, WLE_zwp_pointer_gesture_pinch_v1_update, (unsigned int)time,
                                (unsigned int)wayland_units_fixed(dx, s->scale120), (unsigned int)wayland_units_fixed(dy, s->scale120),
                                (unsigned int)(scale > 0 ? scale / 4 : 0), 0u);
        }
}

static struct wayland_surface *wayland_xserial_surface(long client, unsigned long long serial, struct wayland_client **out_c)
{
        struct wayland_client *c = wayland_client_of(client);
        if (out_c) *out_c = c;
        for (long slot = 0; c && slot < c->tbl[WLT_SURFACE].count; slot++)
                if (c->surfaces[slot].live && c->surfaces[slot].xsurface && c->surfaces[slot].u.xwin.serial_set &&
                    c->surfaces[slot].u.xwin.serial == serial)
                        return &c->surfaces[slot];
        return 0;
}

WAYLAND_API long wayland_serial_row(long client, unsigned long long serial)
{
        struct wayland_surface *s = wayland_xserial_surface(client, serial, 0);
        return !s ? -2 : s->row >= 0 && s->row < wayland.row_made ? s->row : -1;
}

WAYLAND_API int wayland_row_has_xserial(long row, long client, unsigned long long serial)
{
        struct wayland_client *c = 0;
        struct wayland_surface *s = wayland_row_surface(row, &c);
        return s && c && c->slot == client && s->xsurface && s->u.xwin.serial_set && s->u.xwin.serial == serial;
}

WAYLAND_API long wayland_xwindow_adopt(long client, unsigned long long serial, long popup, long x, long y, const char *title)
{
        struct wayland_client *c = 0;
        struct wayland_surface *s = wayland_xserial_surface(client, serial, &c);
        long at = 0;
        int changed = 0;

        if (!s || !s->xsurface || s->u.xwin.dead) return -2;
        s->u.xwin.adopted = 1;
        if (s->row < 0)
        {
                s->u.xwin.popup = (popup & WAYLAND_XWINDOW_POPUP) ? 1 : 0;
                s->u.xwin.placed = (popup & WAYLAND_XWINDOW_PLACED) ? 1 : 0;
                s->u.xwin.owner = popup >> WAYLAND_XWINDOW_OWNER_SHIFT, s->u.xwin.x = x, s->u.xwin.y = y;
        }
        else if (s->u.xwin.popup && wayland.host && (s->u.xwin.x != x || s->u.xwin.y != y))
        {
                long px = 0, py = 0;
                s->u.xwin.x = x, s->u.xwin.y = y;
                if (c && !s->u.xwin.alone && wayland_xparent(c, s, &px, &py) >= 0) wayland_row_do(s->row, WAYLAND_DO_PLACE, px, py);
                else if (s->u.xwin.alone) wayland_row_do(s->row, WAYLAND_DO_PLACE, x, y);
        }
        for (; title && title[at] && at < WAYLAND_TEXT - 1; at++)
                changed |= s->title[at] != title[at], s->title[at] = title[at];
        changed |= s->title[at] != 0, s->title[at] = 0;
        if (changed && s->row >= 0 && wayland.host)
                wayland.host->row_title(wayland.host->context, s->row, s->title[0] ? s->title : s->app_id, 0);
        return s->row >= 0 ? s->row : -1;
}

WAYLAND_API void wayland_xwindow_gone(long client, unsigned long long serial)
{
        struct wayland_client *c = 0;
        struct wayland_surface *s = wayland_xserial_surface(client, serial, &c);
        if (!s || !s->xsurface) return;
        s->u.xwin.dead = 1, s->u.xwin.adopted = 0;
        if (c) wayland_row_release(c, s);
}

static void wayland_shortcuts_send(struct wayland_client *c, struct wayland_surface *s, int active)
{
        s->shortcut_active = (unsigned char)active, s->escape_down = 0;
        if (wayland_find(c, s->shortcut_id, WLI_zwp_keyboard_shortcuts_inhibitor_v1) >= 0)
                wayland_ev0(c, s->shortcut_id, active ? WLE_zwp_keyboard_shortcuts_inhibitor_v1_active
                                                      : WLE_zwp_keyboard_shortcuts_inhibitor_v1_inactive);
}

WAYLAND_API long wayland_shortcuts_key(long row, long code, long value, int super)
{
        struct wayland_client *c = 0;
        struct wayland_surface *s = wayland_row_surface(row, &c);

        if (!s || !(s->shortcuts || s->xgrab) || !s->shortcut_active) return WAYLAND_KEYS_CHORDS;
        if (code == WAYLAND_KEY_ESC && value == 1 && super && !s->escape_down)
                return s->escape_down = 1, s->escape_at = wayland_now(), WAYLAND_KEYS_ESCAPE;
        if (code == WAYLAND_KEY_ESC && s->escape_down)
        {
                if (value == 0 && (s->escape_down = 0, wayland_now() - s->escape_at >= WAYLAND_ESCAPE_MS))
                        wayland_shortcuts_send(c, s, 0);
                return WAYLAND_KEYS_ESCAPE;
        }
        return WAYLAND_KEYS_CLIENT;
}

static long wayland_idle_scan(int tick, long now)
{
        long due = 0;
        int inhibited = -1;

        for (long slot = 0; slot < wayland.client_slots; slot++)
        {
                struct wayland_client *c = wayland.client[slot];
                if (!c || c->dead) continue;
                for (long at = 0; at < c->tbl[WLT_NOTIFY].count; at++)
                {
                        struct wayland_notify *n = &c->notifies[at];
                        if (!n->live) continue;
                        if (n->idle)
                        {
                                if (tick && wayland.input_at > n->idled)
                                        n->idle = 0, n->since = wayland.input_at, wayland_ev0(c, n->id, WLE_ext_idle_notification_v1_resumed);
                                continue;
                        }
                        if (!n->input_only && inhibited < 0) inhibited = wayland_inhibitors_now() > 0;
                        if (!n->input_only && inhibited) continue;
                        long base = wayland.input_at > n->since ? wayland.input_at : n->since;
                        if (tick) { if (now - base >= n->timeout) n->idle = 1, n->idled = now, wayland_ev0(c, n->id, WLE_ext_idle_notification_v1_idled); }
                        else if (!due || base + n->timeout < due) due = base + n->timeout;
                }
        }
        return due;
}

WAYLAND_API void wayland_idle_tick(void)
{
        if (!wayland.host) return;
        if (wayland.drag.live && wayland.drag.released && !wayland.drag.wl_drop_sent && wayland_now() - wayland.drag.released_at >= WAYLAND_DRAG_ANSWER_MS)
                wayland_drag_decide();
        if (wayland.idle_notifies) wayland_idle_scan(1, wayland_now());
}

WAYLAND_API long wayland_idle_due(void)
{
        long due = wayland.host && wayland.idle_notifies ? wayland_idle_scan(0, 0) : 0;

        if (wayland.host && wayland.drag.live && wayland.drag.released && !wayland.drag.wl_drop_sent)
        {
                long drag_due = wayland.drag.released_at + WAYLAND_DRAG_ANSWER_MS;

                if (!due || drag_due < due)
                        due = drag_due;
        }
        return due;
}

WAYLAND_API void wayland_idle_state(long *input_ms, long *inhibitors, long *idled)
{
        *input_ms = wayland.host ? wayland.input_at : 0, *inhibitors = wayland_inhibitors_now(), *idled = 0;
        if (!wayland.idle_notifies) return;
        for (long slot = 0; slot < wayland.client_slots; slot++)
                if (wayland.client[slot] && !wayland.client[slot]->dead)
                        for (long at = 0; at < wayland.client[slot]->tbl[WLT_NOTIFY].count; at++)
                                *idled += wayland.client[slot]->notifies[at].live && wayland.client[slot]->notifies[at].idle;
}

WAYLAND_API void wayland_row_hints(long row, long *content, long *icon, long *modal, long *shortcuts)
{
        struct wayland_client *c = 0;
        struct wayland_surface *s = wayland_row_surface(row, &c);
        *content = s ? (long)s->content : WLN_wp_content_type_v1_type_none;
        *icon = s ? s->cur.icon : 0, *modal = s ? s->modal : 0;
        *shortcuts = s && (s->shortcuts || s->xgrab) ? s->shortcut_active : 0;
}

struct wayland_cursor { const unsigned char *pixels; long stride, width, height, format, hot_x, hot_y, scale; unsigned long serial; };

WAYLAND_API int wayland_cursor_picture(long row, struct wayland_cursor *out)
{
        struct wayland_client *c = wayland_client_of(wayland.cursor_client);
        struct wayland_surface *s = c && wayland.cursor_surface >= 0 && wayland.cursor_surface < c->tbl[WLT_SURFACE].count ? &c->surfaces[wayland.cursor_surface] : 0;

        *out = (struct wayland_cursor){.scale = 1, .serial = wayland.cursor_serial};
        if (!s || !s->live || s->gen != wayland.cursor_gen || s->role != WAYLAND_ROLE_CURSOR || !s->held || row < 0 || row >= wayland.row_made ||
            row != wayland.pointer_row || wayland.row[row].client != wayland.cursor_client)
                return 0;
        const struct wayland_buffer *b = &c->buffers[s->held - 1];
        const struct wayland_pool *p = &c->pools[b->pool];
        if (!b->live || !p->live || p->unmapped || !p->at || (b->format != WLN_wl_shm_format_argb8888 && b->format != WLN_wl_shm_format_xrgb8888) ||
            b->width <= 0 || b->height <= 0)
                return 0;
        long scale = s->cur.scale > 0 ? s->cur.scale : 1;
        *out = (struct wayland_cursor){(const unsigned char *)p->at + b->offset, b->stride, b->width, b->height, b->format,
                                       wayland.cursor_hot_x * scale, wayland.cursor_hot_y * scale, scale, wayland.cursor_serial};
        return 1;
}

#endif /* WAYLAND_INCLUDED */
