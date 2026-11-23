/*
        Xwayland window manager & X11/Proton/Steam peer bridge for Canvas (included directly by compositor.c
        when CONFIG_MOONWATER_WAYLAND is on). Manages up to COMPOSITOR_XWM_SLOTS Xwayland connections on
        /run/moonwater/xwm/wm-0, EWMH/ICCCM/tray lifecycle, CLIPBOARD/PRIMARY + SAVE_TARGETS via XFIXES,
        bidirectional XDND v5, and Steam Overlay input override.
*/
#ifndef COMPOSITOR_XWM_INCLUDED
#define COMPOSITOR_XWM_INCLUDED

#include "xwm_wire.c"

#define COMPOSITOR_XWM_DIR "/run/moonwater/xwm"
#define COMPOSITOR_XWM_PATH "/run/moonwater/xwm/wm-0"
#define COMPOSITOR_XWM_BACKLOG 4
#define COMPOSITOR_XWM_SLOTS 8
#define COMPOSITOR_XWM_POLLS (1 + COMPOSITOR_XWM_SLOTS)
#define COMPOSITOR_XWM_IN (XWM_REPLY_MAX + 4096)
#define COMPOSITOR_XWM_OUT 131072
#define COMPOSITOR_XWM_SETUP_NS 5000000000ull
#define COMPOSITOR_XWM_TARGETS_MOST 64
#define COMPOSITOR_XWM_ATOMS_MAX 2048
#define COMPOSITOR_XWM_WINDOWS_MAX 4096
#define COMPOSITOR_XWM_PENDING_MAX 8192u
#define COMPOSITOR_XWM_IN_MAX 64
#define COMPOSITOR_XWM_OUT_MAX 64
#define COMPOSITOR_XWM_LIST_MAX 1024 /* TARGETS and MULTIPLE lists, in entries: the stack holds them, so they are not 64 KiB */
#define COMPOSITOR_XWM_BIND_NS 15000000000ull
#define COMPOSITOR_XWM_AF_SUP 1u
#define COMPOSITOR_XWM_AF_POP 2u
#define COMPOSITOR_XWM_ATOM_LIST(X) \
        X(PROTOCOLS, "WM_PROTOCOLS", 0) X(DELETE, "WM_DELETE_WINDOW", 0) \
        X(NET_STATE, "_NET_WM_STATE", COMPOSITOR_XWM_AF_SUP) X(FULLSCREEN, "_NET_WM_STATE_FULLSCREEN", COMPOSITOR_XWM_AF_SUP) \
        X(SERIAL, "WL_SURFACE_SERIAL", 0) X(ALLOW_COMMITS, "_XWAYLAND_ALLOW_COMMITS", 0) \
        X(WM_S0, "WM_S0", 0) X(WM_STATE, "WM_STATE", 0) \
        X(NET_NAME, "_NET_WM_NAME", COMPOSITOR_XWM_AF_SUP) X(UTF8, "UTF8_STRING", 0) \
        X(TAKE_FOCUS, "WM_TAKE_FOCUS", 0) X(TYPE, "_NET_WM_WINDOW_TYPE", COMPOSITOR_XWM_AF_SUP) \
        X(TYPE_TOOLTIP, "_NET_WM_WINDOW_TYPE_TOOLTIP", COMPOSITOR_XWM_AF_SUP | COMPOSITOR_XWM_AF_POP) \
        X(TYPE_DROPDOWN_MENU, "_NET_WM_WINDOW_TYPE_DROPDOWN_MENU", COMPOSITOR_XWM_AF_SUP | COMPOSITOR_XWM_AF_POP) \
        X(TYPE_POPUP_MENU, "_NET_WM_WINDOW_TYPE_POPUP_MENU", COMPOSITOR_XWM_AF_SUP | COMPOSITOR_XWM_AF_POP) \
        X(TYPE_COMBO, "_NET_WM_WINDOW_TYPE_COMBO", COMPOSITOR_XWM_AF_SUP | COMPOSITOR_XWM_AF_POP) \
        X(TYPE_NOTIFICATION, "_NET_WM_WINDOW_TYPE_NOTIFICATION", COMPOSITOR_XWM_AF_SUP | COMPOSITOR_XWM_AF_POP) \
        X(TYPE_DND, "_NET_WM_WINDOW_TYPE_DND", COMPOSITOR_XWM_AF_SUP | COMPOSITOR_XWM_AF_POP) \
        X(NET_SUPPORTED, "_NET_SUPPORTED", COMPOSITOR_XWM_AF_SUP) X(NET_CHECK, "_NET_SUPPORTING_WM_CHECK", COMPOSITOR_XWM_AF_SUP) \
        X(NET_MOVERESIZE, "_NET_WM_MOVERESIZE", COMPOSITOR_XWM_AF_SUP) X(NET_ACTIVE, "_NET_ACTIVE_WINDOW", COMPOSITOR_XWM_AF_SUP) \
        X(NET_CLOSE, "_NET_CLOSE_WINDOW", COMPOSITOR_XWM_AF_SUP) X(MAX_VERT, "_NET_WM_STATE_MAXIMIZED_VERT", COMPOSITOR_XWM_AF_SUP) \
        X(MAX_HORZ, "_NET_WM_STATE_MAXIMIZED_HORZ", COMPOSITOR_XWM_AF_SUP) X(FOCUSED, "_NET_WM_STATE_FOCUSED", COMPOSITOR_XWM_AF_SUP) \
        X(HIDDEN, "_NET_WM_STATE_HIDDEN", COMPOSITOR_XWM_AF_SUP) X(CHANGE_STATE, "WM_CHANGE_STATE", 0) \
        X(TYPE_NORMAL, "_NET_WM_WINDOW_TYPE_NORMAL", COMPOSITOR_XWM_AF_SUP) X(TYPE_DIALOG, "_NET_WM_WINDOW_TYPE_DIALOG", COMPOSITOR_XWM_AF_SUP) \
        X(TYPE_UTILITY, "_NET_WM_WINDOW_TYPE_UTILITY", COMPOSITOR_XWM_AF_SUP) X(TYPE_SPLASH, "_NET_WM_WINDOW_TYPE_SPLASH", COMPOSITOR_XWM_AF_SUP) \
        X(CLIPBOARD, "CLIPBOARD", 0) X(TARGETS, "TARGETS", 0) X(TIMESTAMP, "TIMESTAMP", 0) X(INCR, "INCR", 0) \
        X(TEXT, "TEXT", 0) X(MIME_UTF8, "text/plain;charset=utf-8", 0) X(MIME_TEXT, "text/plain", 0) \
        X(SELECTION, "_MOONWATER_SELECTION", 0) X(XDND_AWARE, "XdndAware", 0) X(XDND_ENTER, "XdndEnter", 0) \
        X(XDND_POSITION, "XdndPosition", 0) X(XDND_STATUS, "XdndStatus", 0) X(XDND_LEAVE, "XdndLeave", 0) \
        X(XDND_DROP, "XdndDrop", 0) X(XDND_FINISHED, "XdndFinished", 0) X(XDND_SELECTION, "XdndSelection", 0) \
        X(XDND_TYPES, "XdndTypeList", 0) X(XDND_COPY, "XdndActionCopy", 0) X(XDND_MOVE, "XdndActionMove", 0) \
        X(XDND_ASK, "XdndActionAsk", 0) X(MULTIPLE, "MULTIPLE", 0) X(ATOM_PAIR, "ATOM_PAIR", 0) \
        X(MANAGER, "CLIPBOARD_MANAGER", 0) X(SAVE_TARGETS, "SAVE_TARGETS", 0) X(XDND_PROXY, "XdndProxy", 0) \
        X(XDND_ACTIONS, "XdndActionList", 0) X(XDND_DESCRIPTIONS, "XdndActionDescription", 0) \
        X(XDND_PRIVATE, "XdndActionPrivate", 0) X(XDND_LINK, "XdndActionLink", 0) \
        X(STEAM, "_MOONWATER_STEAM_OVERLAY", 0) X(STEAM_OPEN, "_MOONWATER_STEAM_OVERLAY_OPEN", 0) \
        X(TYPE_DESKTOP, "_NET_WM_WINDOW_TYPE_DESKTOP", COMPOSITOR_XWM_AF_SUP) \
        X(TRAY_S0, "_NET_SYSTEM_TRAY_S0", 0) X(TRAY_OPCODE, "_NET_SYSTEM_TRAY_OPCODE", 0) \
        X(TRAY_ORIENT, "_NET_SYSTEM_TRAY_ORIENTATION", 0) X(TRAY_VISUAL, "_NET_SYSTEM_TRAY_VISUAL", 0) \
        X(XEMBED, "_XEMBED", 0) X(XEMBED_INFO, "_XEMBED_INFO", 0) X(NET_ICON, "_NET_WM_ICON", 0) X(X_MANAGER, "MANAGER", 0) \
        X(FRAME_EXTENTS, "_NET_FRAME_EXTENTS", COMPOSITOR_XWM_AF_SUP) X(REQ_FRAME_EXTENTS, "_NET_REQUEST_FRAME_EXTENTS", COMPOSITOR_XWM_AF_SUP) \
        X(WORKAREA, "_NET_WORKAREA", COMPOSITOR_XWM_AF_SUP) X(ABOVE, "_NET_WM_STATE_ABOVE", COMPOSITOR_XWM_AF_SUP) \
        X(MOTIF_HINTS, "_MOTIF_WM_HINTS", 0)

enum {
#define X(id, name, flags) COMPOSITOR_XWM_A_##id,
        COMPOSITOR_XWM_ATOM_LIST(X)
#undef X
        COMPOSITOR_XWM_ATOMS
};

static const struct { const char *name; unsigned char flags; } xwm_atom_spec[COMPOSITOR_XWM_ATOMS] = {
#define X(id, name, flags) {name, flags},
        COMPOSITOR_XWM_ATOM_LIST(X)
#undef X
};

#define COMPOSITOR_XWM_XA_PRIMARY 1
#define COMPOSITOR_XWM_XA_INTEGER 19
#define COMPOSITOR_XWM_XA_STRING 31
#define COMPOSITOR_XWM_XA_WINDOW 33
#define COMPOSITOR_XWM_XA_WINDOW_TYPE 33
#define COMPOSITOR_XWM_XA_WM_HINTS 35
#define COMPOSITOR_XWM_XA_WM_NAME 39
#define COMPOSITOR_XWM_XA_WM_NORMAL_HINTS 40
#define COMPOSITOR_XWM_XA_WM_CLASS 67
#define COMPOSITOR_XWM_XA_WM_TRANSIENT_FOR 68
#define COMPOSITOR_XWM_MOVERESIZE_MOVE 8u
#define COMPOSITOR_XWM_MOVERESIZE_CANCEL 11u
#define COMPOSITOR_XWM_TITLE 128

enum compositor_xwm_prop
{
        COMPOSITOR_XWM_PROP_NET_NAME = 1, COMPOSITOR_XWM_PROP_NAME, COMPOSITOR_XWM_PROP_PROTOCOLS,
        COMPOSITOR_XWM_PROP_TRANSIENT, COMPOSITOR_XWM_PROP_TYPE, COMPOSITOR_XWM_PROP_STATE,
        COMPOSITOR_XWM_PROP_WM_HINTS, COMPOSITOR_XWM_PROP_NORMAL_HINTS, COMPOSITOR_XWM_PROP_OWNER,
        COMPOSITOR_XWM_PROP_INTERN, COMPOSITOR_XWM_PROP_ATOM_NAME, COMPOSITOR_XWM_PROP_SEL,
        COMPOSITOR_XWM_PROP_SEL_SYNC, COMPOSITOR_XWM_PROP_XDND_AWARE, COMPOSITOR_XWM_PROP_XDND_TYPES,
        COMPOSITOR_XWM_PROP_XDND_SYNC, COMPOSITOR_XWM_PROP_ICON, COMPOSITOR_XWM_PROP_MULTIPLE,
        COMPOSITOR_XWM_PROP_XDND_PROXY, COMPOSITOR_XWM_PROP_XDND_PROXIED, COMPOSITOR_XWM_PROP_XDND_ACTIONS,
        COMPOSITOR_XWM_PROP_XDND_WORDS, COMPOSITOR_XWM_PROP_STEAM, COMPOSITOR_XWM_PROP_STEAM_OPEN,
        COMPOSITOR_XWM_PROP_CLASS, COMPOSITOR_XWM_PROP_EXT, COMPOSITOR_XWM_PROP_MOTIF, COMPOSITOR_XWM_PROP_SHAPE,
};
#define COMPOSITOR_XWM_DND_REPLY(what) (((what) >= COMPOSITOR_XWM_PROP_XDND_AWARE && (what) <= COMPOSITOR_XWM_PROP_XDND_SYNC) || \
                                        ((what) >= COMPOSITOR_XWM_PROP_XDND_PROXY && (what) <= COMPOSITOR_XWM_PROP_XDND_WORDS))
#define COMPOSITOR_XWM_PROTO_DELETE 1u
#define COMPOSITOR_XWM_PROTO_FOCUS 2u
#define COMPOSITOR_XWM_NO_PLACE (-100000)
#define COMPOSITOR_XWM_ROOM 2048
#define COMPOSITOR_XWM_STATE_WITHDRAWN 0u
#define COMPOSITOR_XWM_STATE_NORMAL 1u
#define COMPOSITOR_XWM_STATE_ICONIC 3u
#define COMPOSITOR_XWM_ALLOW_TYPE 6
#define COMPOSITOR_XWM_ATOM_FIRST_SEQ 2
#define COMPOSITOR_XWM_KEY_ESCAPE 9
#define COMPOSITOR_XWM_ATOM_TYPE 4
#define COMPOSITOR_XWM_NET_REMOVE 0
#define COMPOSITOR_XWM_NET_ADD 1
#define COMPOSITOR_XWM_NET_TOGGLE 2

enum { XWM_EXT_COMPOSITE, XWM_EXT_XRES, XWM_EXT_XFIXES, XWM_EXT_XTEST, XWM_EXT_SHAPE };
static const struct { const char *name; unsigned char len; } xwm_ext_spec[] = {
        {"Composite", 9}, {"X-Resource", 10}, {"XFIXES", 6}, {"XTEST", 5}, {"SHAPE", 5},
};

#define COMPOSITOR_XWM_MAPPED 1u
#define COMPOSITOR_XWM_OVERRIDE 2u
#define COMPOSITOR_XWM_FULLSCREEN 4u
#define COMPOSITOR_XWM_SERIAL 8u
#define COMPOSITOR_XWM_MAX_VERT 16u
#define COMPOSITOR_XWM_MAX_HORZ 32768u
#define COMPOSITOR_XWM_MAXIMIZED (COMPOSITOR_XWM_MAX_VERT | COMPOSITOR_XWM_MAX_HORZ)
#define COMPOSITOR_XWM_NET_TITLE 32u
#define COMPOSITOR_XWM_ADOPTED 64u
#define COMPOSITOR_XWM_PROTO_KNOWN 128u
#define COMPOSITOR_XWM_TITLE_AGAIN 256u
#define COMPOSITOR_XWM_POPUPTYPE 512u
#define COMPOSITOR_XWM_PROPS_READ 1024u
#define COMPOSITOR_XWM_RAISED 2048u
#define COMPOSITOR_XWM_COVERED 4096u
#define COMPOSITOR_XWM_DESKTOP 8192u
#define COMPOSITOR_XWM_TRAY_ICON 16384u
#define COMPOSITOR_XWM_ABOVE 65536u
#define COMPOSITOR_XWM_HIDDEN 131072u
#define COMPOSITOR_XWM_FOCUSED 262144u
#define COMPOSITOR_XWM_BORDERLESS 524288u
#define COMPOSITOR_XWM_POPUPISH (COMPOSITOR_XWM_OVERRIDE | COMPOSITOR_XWM_POPUPTYPE)
#define COMPOSITOR_XWM_EAGAIN (-11)
#define COMPOSITOR_XWM_MSG_DONTWAIT 0x40
#define COMPOSITOR_XWM_MSG_NOSIGNAL 0x4000
#define COMPOSITOR_XWM_SO_PEERCRED 17
#define COMPOSITOR_XWM_SEL_WINDOW 2u
#define COMPOSITOR_XWM_PROXY_WINDOW 3u
#define COMPOSITOR_XWM_TRAY_WINDOW 4u
#define COMPOSITOR_XWM_FIRST_REQUESTOR 16u

struct compositor_xwm_window
{
        unsigned int id, state, protocols, transient, owner, asking, dirty;
        unsigned long long serial, sync_present;
        int row, x, y, sent_x, sent_y;
        unsigned int width, height, sent_w, sent_h, restore_w, restore_h, min_w, min_h, max_w, max_h;
        unsigned char closes, placed, no_input;
        unsigned char left_fullscreen; // the person took it out of fullscreen (F11, Super+F11): its own asks to go back are refused till they put it back
        char title[COMPOSITOR_XWM_TITLE];
        char app[WINDOWS_APP_ID];
};

struct compositor_xwm_steam
{
        struct compositor_xwm *slot;
        unsigned int window, pid, told;
        unsigned char open, focused, tab;
        int x, y, wheel[2];
};

struct compositor_xwm_pending { unsigned int seq, window, what, slot; };
struct compositor_xwm_atom { unsigned int atom, at, length; unsigned char known; };

struct compositor_xwm_in
{
        unsigned int window, selection, target, time, text;
        unsigned char which, stage, eager, latin1, incr, asking, ready;
        int fd, row, index;
        long pump, next;
        unsigned long done, offset, total, count;
        positive deadline;
        struct wayland_table targets;
};

struct compositor_xwm_out
{
        unsigned int requestor, property, type, selection, target, time;
        unsigned char which, from, latin1, stage, quiet;
        struct xwm_utf8_cut cut;
        int fd;
        long pump;
        unsigned long base, offset, size;
        positive deadline;
};

struct compositor_xwm_selection { unsigned int atom, owned_time, x_owner; unsigned char owned, dirty; long reading; };

struct compositor_xwm_wdrag
{
        unsigned int target, version, place, action, proxy;
        unsigned char live, entered, waiting, moved, dropped, aware_no;
        int row;
};

struct compositor_xwm_xdrag
{
        unsigned int source, version, action, time, told_action, listed, words;
        unsigned char entered, naming, began, dropped, cancelled, told_accept, proxy_mapped, escaped;
        int proxy_row, proxy_x, proxy_y, proxy_w, proxy_h;
        struct wayland_table types;
};

struct compositor_xwm
{
        int fd, ready, broken, claimed, composite, peer_pid, poll_at, focus_row;
        unsigned int xtest, xres, resource_base, resource_mask, root, seq, atoms[COMPOSITOR_XWM_ATOMS];
        unsigned int atoms_seen, refused, errors, last_stack_gen, focus_window;
        unsigned int pending_at, pending_count, pending_room, xfixes_opcode, xfixes_event, setting, next_requestor;
        unsigned int shape_opcode, shape_event; // SHAPE: windows cut to rectangles (xwm_shape_reply)
        long shape_skip;                        // bytes of a shape's reply too long to keep, still to pass over
        long client, client_pid, in_len, out_len, windows, atoms_indexed, name_bytes, in_count, out_count;
        positive since;
        unsigned char in[COMPOSITOR_XWM_IN], out[COMPOSITOR_XWM_OUT];
        struct wayland_table window_table, window_index, pending_table;
        struct wayland_table atom_table, name_table, in_table, out_table, type_table, atom_index;
        struct compositor_xwm_window *window;
        struct compositor_xwm_pending *pending;
        struct compositor_xwm_atom *atom;
        struct compositor_xwm_in *ins;
        struct compositor_xwm_out *outs;
        struct compositor_xwm_selection sel[3];
        struct compositor_xwm_wdrag wdrag;
        struct compositor_xwm_xdrag xdrag;
        struct { unsigned int requestor, property, time; } save;
};

#define COMPOSITOR_XWM_FRESH {.fd = -1, .poll_at = -1, .focus_row = -2, .client = -1, .client_pid = -1, \
                              .sel = {{0, 0, 0, 0, 0, -1}, {0, 0, 0, 0, 0, -1}, {0, 0, 0, 0, 0, -1}}, \
                              .xdrag = {.proxy_row = -1}}
static const struct compositor_xwm compositor_xwm_fresh = COMPOSITOR_XWM_FRESH;
static struct compositor_xwm compositor_xwm_slots[COMPOSITOR_XWM_SLOTS] = {[0 ... COMPOSITOR_XWM_SLOTS - 1] = COMPOSITOR_XWM_FRESH};
static struct compositor_xwm *compositor_xwm_at = &compositor_xwm_slots[0];
static struct compositor_xwm_steam compositor_xwm_steam;
static unsigned int compositor_xwm_counted[4];
static void compositor_xwm_counted_refusal(void); // below: a window's own ask the person's choice outweighs, counted and said
static int compositor_xwm_listen_fd = -1, compositor_xwm_listen_at = -1, compositor_xwm_uid;

static struct compositor_xwm *compositor_xwm_slot_of_row(long row);
static struct compositor_xwm *compositor_xwm_first_ready(void);
static long compositor_xwm_server(struct compositor_xwm *x);
static void xwm_drop(struct compositor_xwm *x, const char *why);
static unsigned int compositor_xwm_id(struct compositor_xwm *x, unsigned int number);
static void compositor_xwm_atoms_seed(struct compositor_xwm *x);
static void compositor_xwm_flush(struct compositor_xwm *x);
static int compositor_xwm_wait(struct compositor_xwm *x, const unsigned char *req, long n, unsigned int window, unsigned int what, unsigned int slot);

static void compositor_xwm_log(const char *text)
{
        log_direct((address_any)text, (positive)string_length((string_address)text));
}

/* ---- X11 wire request helpers ---- */

static int compositor_xwm_queue(struct compositor_xwm *x, const unsigned char *bytes, long count)
{
        if (count <= 0)
                return -1;
        if (x->out_len + count > COMPOSITOR_XWM_OUT)
                return x->broken = 1, -1;
        memory_copy(x->out + x->out_len, bytes, (positive)count);
        x->out_len += count;
        x->seq = (x->seq + 1) & 0xffffu;
        return 0;
}

static void compositor_xwm_window_op(struct compositor_xwm *x, unsigned int opcode, unsigned int window)
{
        unsigned char req[XWM_REQUEST_SHORT];
        compositor_xwm_queue(x, req, xwm_enc_window_op(req, sizeof req, opcode, window));
}

static unsigned char compositor_xwm_request_bytes[24 + XWM_PROPERTY_MAX];

static void compositor_xwm_put(struct compositor_xwm *x, unsigned int window, unsigned int property, unsigned int type, unsigned int format, const unsigned char *data, unsigned long length)
{
        compositor_xwm_queue(x, compositor_xwm_request_bytes,
                             xwm_enc_change_property(compositor_xwm_request_bytes, sizeof compositor_xwm_request_bytes, XWM_PROP_REPLACE,
                                                     window, property, type, format, data, length));
}

static void compositor_xwm_put32(struct compositor_xwm *x, unsigned int window, unsigned int property, unsigned int type, const unsigned int *vals, unsigned int count)
{
        unsigned char data[256];
        if ((unsigned long)count * 4 > sizeof data)
                return;
        for (unsigned int i = 0; i < count; i++)
                xwm_put32(data + 4 * i, vals[i]);
        compositor_xwm_put(x, window, property, type, XWM_FORMAT_32, data, (unsigned long)count * 4);
}

static void compositor_xwm_send_event(struct compositor_xwm *x, unsigned int dest, int propagate, unsigned int mask, const unsigned char *ev, unsigned long len)
{
        unsigned char req[64];
        compositor_xwm_queue(x, req, xwm_enc_send_event(req, sizeof req, propagate, dest, mask, ev, len));
}

static void compositor_xwm_send_configure(struct compositor_xwm *x, const struct compositor_xwm_window *w, int px, int py, unsigned int width, unsigned int height)
{
        unsigned char ev[XWM_EVENT_SIZE];
        if (w && width && height &&
            xwm_enc_configure_notify(ev, sizeof ev, w->id, px, py, width, height, 0, (w->state & COMPOSITOR_XWM_OVERRIDE) != 0) == XWM_EVENT_SIZE)
                compositor_xwm_send_event(x, w->id, 0, XWM_MASK_STRUCTURE_NOTIFY, ev, XWM_EVENT_SIZE);
}

static void compositor_xwm_active(struct compositor_xwm *x, unsigned int window)
{
        if (x->root && x->atoms[COMPOSITOR_XWM_A_NET_ACTIVE])
                compositor_xwm_put32(x, x->root, x->atoms[COMPOSITOR_XWM_A_NET_ACTIVE], COMPOSITOR_XWM_XA_WINDOW, &window, 1);
}

static void compositor_xwm_focus(struct compositor_xwm *x, unsigned int window)
{
        unsigned char req[64];
        compositor_xwm_queue(x, req, xwm_enc_set_input_focus(req, 16, XWM_REVERT_NONE, window, XWM_CURRENT_TIME));
        compositor_xwm_active(x, window);
}

static void compositor_xwm_configure(struct compositor_xwm *x, unsigned int window, const struct xwm_configure *c)
{
        unsigned char req[64];
        compositor_xwm_queue(x, req, xwm_enc_configure_window(req, sizeof req, window, c));
}

static void compositor_xwm_select(struct compositor_xwm *x, unsigned int window, unsigned int mask)
{
        unsigned char req[32];
        compositor_xwm_queue(x, req, xwm_enc_select_events(req, sizeof req, window, mask));
}

static void xwm_make_win(struct compositor_xwm *x, unsigned int win, int xy, unsigned int cls, unsigned int cw_mask, const unsigned int *vals, unsigned int nvals)
{
        unsigned char req[48];
        compositor_xwm_queue(x, req, xwm_enc_create_window(req, sizeof req, 0, win, x->root, xy, xy, 1, 1, cls, 0));
        if (cw_mask)
                compositor_xwm_queue(x, req, xwm_enc_change_window_attributes(req, sizeof req, win, cw_mask, vals, nvals));
}

static int xwm_wait_prop(struct compositor_xwm *x, unsigned int win, unsigned int prop, unsigned int type, unsigned int len, unsigned int what, unsigned int slot)
{
        unsigned char req[32];
        return compositor_xwm_wait(x, req, xwm_enc_get_property(req, sizeof req, 0, win, prop, type, 0, len), win, what, slot);
}

static void compositor_xwm_wm_state(struct compositor_xwm *x, unsigned int window, unsigned int state)
{
        unsigned int vals[2] = {state, 0};
        compositor_xwm_put32(x, window, x->atoms[COMPOSITOR_XWM_A_WM_STATE], x->atoms[COMPOSITOR_XWM_A_WM_STATE], vals, 2);
}

static void xwm_claim(struct compositor_xwm *x)
{
        static const unsigned char wm_name[] = "moonwater";
        unsigned char req[64], ev[XWM_EVENT_SIZE];
        unsigned int vals[48], zero = 0, count = 0;
        unsigned int window = x->resource_base | (1u & x->resource_mask);
        unsigned int sel = compositor_xwm_id(x, COMPOSITOR_XWM_SEL_WINDOW), proxy = compositor_xwm_id(x, COMPOSITOR_XWM_PROXY_WINDOW);
        unsigned int cw_sel[2] = {1, XWM_MASK_PROPERTY_CHANGE}, ver = XWM_XDND_VERSION;

        if (x->claimed)
                return;
        x->claimed = 1;
        xwm_make_win(x, window, -1, XWM_CLASS_INPUT_ONLY, 0, 0, 0);
        compositor_xwm_queue(x, req, xwm_enc_set_selection_owner(req, sizeof req, window, x->atoms[COMPOSITOR_XWM_A_WM_S0], XWM_CURRENT_TIME));
        compositor_xwm_put32(x, window, x->atoms[COMPOSITOR_XWM_A_NET_CHECK], COMPOSITOR_XWM_XA_WINDOW, &window, 1);
        compositor_xwm_put(x, window, x->atoms[COMPOSITOR_XWM_A_NET_NAME], x->atoms[COMPOSITOR_XWM_A_UTF8], XWM_FORMAT_8, wm_name, sizeof wm_name - 1);
        compositor_xwm_put32(x, x->root, x->atoms[COMPOSITOR_XWM_A_NET_CHECK], COMPOSITOR_XWM_XA_WINDOW, &window, 1);
        compositor_xwm_put32(x, x->root, x->atoms[COMPOSITOR_XWM_A_NET_ACTIVE], COMPOSITOR_XWM_XA_WINDOW, &zero, 1);
        for (unsigned int i = 0; i < COMPOSITOR_XWM_ATOMS && count < 48; i++)
                if (xwm_atom_spec[i].flags & COMPOSITOR_XWM_AF_SUP)
                        vals[count++] = x->atoms[i];
        compositor_xwm_put32(x, x->root, x->atoms[COMPOSITOR_XWM_A_NET_SUPPORTED], COMPOSITOR_XWM_ATOM_TYPE, vals, count);
        xwm_make_win(x, sel, -1, XWM_CLASS_INPUT_ONLY, XWM_CW_OVERRIDE_REDIRECT | XWM_CW_EVENT_MASK, cw_sel, 2);
        xwm_make_win(x, proxy, 0, XWM_CLASS_INPUT_ONLY, XWM_CW_OVERRIDE_REDIRECT, cw_sel, 1);
        compositor_xwm_put32(x, proxy, x->atoms[COMPOSITOR_XWM_A_XDND_AWARE], COMPOSITOR_XWM_ATOM_TYPE, &ver, 1);
        x->sel[0].atom = x->atoms[COMPOSITOR_XWM_A_CLIPBOARD];
        x->sel[1].atom = COMPOSITOR_XWM_XA_PRIMARY;
        x->sel[2].atom = x->atoms[COMPOSITOR_XWM_A_XDND_SELECTION];
        compositor_xwm_atoms_seed(x);
        if (x->atoms[COMPOSITOR_XWM_A_TRAY_S0])
        {
                unsigned int tray = compositor_xwm_id(x, COMPOSITOR_XWM_TRAY_WINDOW);
                unsigned int cw_tray[2] = {1, XWM_MASK_SUBSTRUCTURE_NOTIFY | XWM_MASK_STRUCTURE_NOTIFY};
                unsigned int mdata[5] = {XWM_CURRENT_TIME, x->atoms[COMPOSITOR_XWM_A_TRAY_S0], tray, 0, 0};

                xwm_make_win(x, tray, -1, XWM_CLASS_INPUT_OUTPUT, XWM_CW_OVERRIDE_REDIRECT | XWM_CW_EVENT_MASK, cw_tray, 2);
                if (x->atoms[COMPOSITOR_XWM_A_TRAY_ORIENT])
                        compositor_xwm_put32(x, tray, x->atoms[COMPOSITOR_XWM_A_TRAY_ORIENT], COMPOSITOR_XWM_ALLOW_TYPE, &zero, 1);
                compositor_xwm_queue(x, req, xwm_enc_set_selection_owner(req, sizeof req, tray, x->atoms[COMPOSITOR_XWM_A_TRAY_S0], XWM_CURRENT_TIME));
                if (x->atoms[COMPOSITOR_XWM_A_X_MANAGER] &&
                    xwm_enc_client_message(ev, sizeof ev, x->root, x->atoms[COMPOSITOR_XWM_A_X_MANAGER], mdata, 5) == XWM_EVENT_SIZE)
                        compositor_xwm_send_event(x, x->root, 0, XWM_MASK_STRUCTURE_NOTIFY, ev, XWM_EVENT_SIZE);
        }
        if (x->atoms[COMPOSITOR_XWM_A_WORKAREA])
        {
                unsigned int scount = 0, wa[4];
                const int (*screens)[4] = compositor_screens(&scount);
                int room[4] = {0, 0, compositor.width > 0 ? compositor.width : 1920, compositor.height > 0 ? compositor.height : 1080};

                if (scount > 0)
                        compositor_room(screens[0], room);
                wa[0] = (unsigned int)(room[0] > 0 ? room[0] : 0), wa[1] = (unsigned int)(room[1] > 0 ? room[1] : 0);
                wa[2] = (unsigned int)(room[2] > 0 ? room[2] : 1), wa[3] = (unsigned int)(room[3] > 0 ? room[3] : 1);
                compositor_xwm_put32(x, x->root, x->atoms[COMPOSITOR_XWM_A_WORKAREA], COMPOSITOR_XWM_ALLOW_TYPE, wa, 4);
        }
}

static void compositor_xwm_frame_extents(struct compositor_xwm *x, unsigned int window)
{
        static const unsigned int zeroes[4] = {0, 0, 0, 0};
        if (x->atoms[COMPOSITOR_XWM_A_FRAME_EXTENTS])
                compositor_xwm_put32(x, window, x->atoms[COMPOSITOR_XWM_A_FRAME_EXTENTS], COMPOSITOR_XWM_ALLOW_TYPE, zeroes, 4);
}

static void compositor_xwm_allow_commits(struct compositor_xwm *x, unsigned int window)
{
        unsigned int one = 1;
        compositor_xwm_put32(x, window, x->atoms[COMPOSITOR_XWM_A_ALLOW_COMMITS], COMPOSITOR_XWM_ALLOW_TYPE, &one, 1);
}

static const struct { unsigned short atom_idx; unsigned int bit; } xwm_net_state_map[6] = {
        {COMPOSITOR_XWM_A_FULLSCREEN, COMPOSITOR_XWM_FULLSCREEN},
        {COMPOSITOR_XWM_A_MAX_VERT, COMPOSITOR_XWM_MAX_VERT},
        {COMPOSITOR_XWM_A_MAX_HORZ, COMPOSITOR_XWM_MAX_HORZ},
        {COMPOSITOR_XWM_A_ABOVE, COMPOSITOR_XWM_ABOVE},
        {COMPOSITOR_XWM_A_HIDDEN, COMPOSITOR_XWM_HIDDEN},
        {COMPOSITOR_XWM_A_FOCUSED, COMPOSITOR_XWM_FOCUSED},
};

static unsigned int xwm_net_state_bits(const struct compositor_xwm *x, unsigned int a, unsigned int b)
{
        unsigned int bits = 0;
        for (unsigned int i = 0; i < 6; i++)
                if (x->atoms[xwm_net_state_map[i].atom_idx] &&
                    (a == x->atoms[xwm_net_state_map[i].atom_idx] || b == x->atoms[xwm_net_state_map[i].atom_idx]))
                        bits |= xwm_net_state_map[i].bit;
        return bits;
}

static unsigned int xwm_net_state_apply(unsigned int action, unsigned int cur, unsigned int mask)
{
        return action == COMPOSITOR_XWM_NET_ADD ? (cur | mask) : action == COMPOSITOR_XWM_NET_REMOVE ? (cur & ~mask) : (cur ^ mask);
}

static void compositor_xwm_net_state(struct compositor_xwm *x, unsigned int window, int state)
{
        unsigned int vals[6], count = 0;
        for (unsigned int i = 0; i < 6; i++)
                if (((unsigned int)state & xwm_net_state_map[i].bit) && x->atoms[xwm_net_state_map[i].atom_idx])
                        vals[count++] = x->atoms[xwm_net_state_map[i].atom_idx];
        compositor_xwm_put32(x, window, x->atoms[COMPOSITOR_XWM_A_NET_STATE], COMPOSITOR_XWM_ATOM_TYPE, vals, count);
}

static void compositor_xwm_protocol(struct compositor_xwm *x, unsigned int window, unsigned int atom)
{
        unsigned int data[2] = {atom, XWM_CURRENT_TIME};
        unsigned char event[XWM_EVENT_SIZE];
        if (xwm_enc_client_message(event, sizeof event, window, x->atoms[COMPOSITOR_XWM_A_PROTOCOLS], data, 2) == XWM_EVENT_SIZE)
                compositor_xwm_send_event(x, window, 0, 0, event, XWM_EVENT_SIZE);
}

static void compositor_xwm_start_session(struct compositor_xwm *x)
{
        unsigned char req[64];
        compositor_xwm_select(x, x->root, XWM_MASK_WM_ROOT);
        for (unsigned int at = 0; at < COMPOSITOR_XWM_ATOMS; at++)
                compositor_xwm_queue(x, req, xwm_enc_intern_atom(req, sizeof req, 0, (const unsigned char *)xwm_atom_spec[at].name,
                                                                 (unsigned long)string_length((string_address)xwm_atom_spec[at].name)));
        for (unsigned int i = 0; i < sizeof xwm_ext_spec / sizeof xwm_ext_spec[0]; i++)
                compositor_xwm_wait(x, req, xwm_enc_query_extension(req, sizeof req, (const unsigned char *)xwm_ext_spec[i].name, xwm_ext_spec[i].len),
                                    0, COMPOSITOR_XWM_PROP_EXT, i);
}

static void compositor_xwm_origin(int *ox, int *oy)
{
        int count = compositor_output_count();
        *ox = *oy = 0;
        for (int at = 0; at < count; at++)
        {
                int x, y, w, h;
                compositor_output_rect(at, &x, &y, &w, &h);
                if (!at || x < *ox)
                        *ox = x;
                if (!at || y < *oy)
                        *oy = y;
        }
}

static int compositor_xwm_to_x(int place, int vertical) { int ox, oy; compositor_xwm_origin(&ox, &oy); return place - (vertical ? oy : ox); }
static int compositor_xwm_from_x(int place, int vertical) { int ox, oy; compositor_xwm_origin(&ox, &oy); return place + (vertical ? oy : ox); }

static void compositor_xwm_owned(struct compositor_xwm_window *w)
{
        struct windows *table = compositor.table;
        unsigned int now;
        if (!w->owner || w->row < 0 || !table || w->row >= (int)compositor_rows_in_use)
                return;
        now = __atomic_load_n(&table->pid[w->row], __ATOMIC_ACQUIRE);
        if (now && now != WINDOWS_TAKING && now != w->owner)
                __atomic_compare_exchange_n(&table->pid[w->row], &now, w->owner, 0, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED);
}

static void compositor_xwm_ask_owner(struct compositor_xwm *x, unsigned int window);

static void xwm_ext_reply(struct compositor_xwm *x, const unsigned char *in, long size, unsigned int ext)
{
        struct xwm_extension_reply r;
        unsigned char req[32];

        if (xwm_parse_extension_reply(in, (unsigned long)size, &r) != XWM_OK)
        {
                if (ext != XWM_EXT_XRES)
                        x->broken = 1;
                return;
        }
        if (!r.present || r.major_opcode < XWM_EXT_MAJOR_FIRST)
        {
                if (ext == XWM_EXT_COMPOSITE)
                        compositor_xwm_log("xwm: no Composite extension\n");
                else if (ext == XWM_EXT_XFIXES)
                        compositor_xwm_log("xwm: no XFIXES extension\n");
                return;
        }
        if (ext == XWM_EXT_COMPOSITE)
        {
                compositor_xwm_queue(x, req, xwm_enc_composite_query_version(req, sizeof req, r.major_opcode, 0, 4));
                compositor_xwm_queue(x, req, xwm_enc_composite_redirect_subwindows(req, sizeof req, r.major_opcode, x->root, XWM_REDIRECT_MANUAL));
                x->composite = 1;
        }
        else if (ext == XWM_EXT_XRES)
        {
                char mine[64], theirs[64], path[48] = "/proc/";
                long a, b;
                x->xres = 0;
                if (x->peer_pid <= 1)
                        return;
                positive_into_string((p8 *)path + 6, (positive)x->peer_pid);
                string_append_bounded((p8 *)path, "/ns/pid", sizeof path);
                a = compositor_call(syscall(readlinkat), (positive)AT_FDCWD, (positive)"/proc/self/ns/pid", (positive)mine, sizeof mine - 1, 0, 0);
                b = compositor_call(syscall(readlinkat), (positive)AT_FDCWD, (positive)path, (positive)theirs, sizeof theirs - 1, 0, 0);
                if (a <= 0 || a != b || memory_compare(mine, theirs, (positive)a))
                        return;
                compositor_xwm_queue(x, req, xwm_enc_xres_query_version(req, sizeof req, r.major_opcode, 1, 2));
                x->xres = r.major_opcode;
                // A window mapped before the extension was known asked no one for its client (compositor_xwm_ask_owner): it is asked
                // now, as a game's first window is made a moment after its server starts, and its client is the game's (game mode,
                // a stand-in's owner, compositor_xwm_stand_row).
                for (long at = 0; x->window && at < x->window_table.count; at++)
                        if (x->window[at].id && !x->window[at].owner)
                                compositor_xwm_ask_owner(x, x->window[at].id);
        }
        else if (ext == XWM_EXT_XFIXES)
        {
                if (r.first_event <= XWM_EVENT_LAST)
                        return;
                x->xfixes_opcode = r.major_opcode, x->xfixes_event = r.first_event;
                compositor_xwm_queue(x, req, xwm_enc_xfixes_query_version(req, sizeof req, r.major_opcode, 5, 0));
                for (int which = 0; which < 3; which++)
                {
                        compositor_xwm_queue(x, req, xwm_enc_xfixes_select_selection_input(req, sizeof req, r.major_opcode,
                                                                                           compositor_xwm_id(x, COMPOSITOR_XWM_SEL_WINDOW),
                                                                                           x->sel[which].atom, XWM_XFIXES_SELECTION_MASK));
                        x->sel[which].dirty = which < 2;
                }
                compositor_xwm_queue(x, req, xwm_enc_set_selection_owner(req, sizeof req, compositor_xwm_id(x, COMPOSITOR_XWM_SEL_WINDOW),
                                                                         x->atoms[COMPOSITOR_XWM_A_MANAGER], XWM_CURRENT_TIME));
        }
        else if (ext == XWM_EXT_XTEST)
                x->xtest = r.major_opcode;
        else if (ext == XWM_EXT_SHAPE && r.first_event > XWM_EVENT_LAST)
                x->shape_opcode = r.major_opcode, x->shape_event = r.first_event;
}

HOT static struct compositor_xwm_window *xwm_find(struct compositor_xwm *x, unsigned int id, int add);

// A window's ShapeNotify asked for: it comes when the window is cut to a shape or let go of one.
static void xwm_shape_watch(struct compositor_xwm *x, unsigned int window)
{
        unsigned char req[16];

        if (x->shape_opcode)
                compositor_xwm_queue(x, req, xwm_enc_shape_select_input(req, sizeof req, x->shape_opcode, window, 1));
}

// The bounding shape of a window that has a row, asked of the X server: xwm_shape_reply gives it to the row.
static void xwm_shape_ask(struct compositor_xwm *x, const struct compositor_xwm_window *w)
{
        unsigned char req[16];

        if (x->shape_opcode && w->row >= 0)
                compositor_xwm_wait(x, req, xwm_enc_shape_get_rectangles(req, sizeof req, x->shape_opcode, w->id), w->id,
                                    COMPOSITOR_XWM_PROP_SHAPE, 0);
}

/*
        A window's bounding shape (GetRectangles: the count at 8, the rectangles from 32, 8 bytes each) to its row
        (compositor_row_shape_set): none where it is one rectangle holding the whole window, the rectangles where it is cut. Wine
        cuts layered and regioned windows so: the Skyrim launcher's highlight over its buttons is a frame of four.
*/
static void xwm_shape_reply(struct compositor_xwm *x, const unsigned char *in, long size, const struct compositor_xwm_pending *found)
{
        struct compositor_xwm_window *w = xwm_find(x, found->window, 0);
        unsigned long count = size >= 32 ? xwm_get32(in + 8) : 0;
        int whole;

        if (size < 32 || count > (unsigned long)(size - 32) / 8 || !w || w->row < 0)
                return;
        whole = count == 1 && xwm_get_i16(in + 32) <= 0 && xwm_get_i16(in + 34) <= 0 &&
                xwm_get_i16(in + 32) + (long)xwm_get16(in + 36) >= (long)w->width && xwm_get_i16(in + 34) + (long)xwm_get16(in + 38) >= (long)w->height;
        compositor_row_shape_set(w->row, in + 32, whole ? 0 : count, !whole);
}

/* ---- properties, tray, and window table ---- */

static long xwm_tbl_take(struct wayland_table *t, unsigned long unit)
{
        wayland_tables_host(&compositor_wl_host);
        t->unit = unit;
        return wayland_table_take(t);
}

static int compositor_xwm_pending_grow(struct compositor_xwm *x)
{
        unsigned int room = x->pending_room ? 2 * x->pending_room : 512;
        struct wayland_table grown = {0, sizeof(struct compositor_xwm_pending), 0, 0, 0, 0, 0, 0, 0, 0};
        struct compositor_xwm_pending *to;

        if (x->pending_room >= COMPOSITOR_XWM_PENDING_MAX)
                return 0;
        if (room > COMPOSITOR_XWM_PENDING_MAX)
                room = COMPOSITOR_XWM_PENDING_MAX;
        wayland_tables_host(&compositor_wl_host);
        if (!wayland_table_reach(&grown, room))
                return 0;
        to = (struct compositor_xwm_pending *)grown.at;
        for (unsigned int k = 0; k < x->pending_count; k++)
                to[k] = x->pending[(x->pending_at + k) % x->pending_room];
        wayland_table_drop(&x->pending_table);
        x->pending_table = grown, x->pending = to, x->pending_at = 0, x->pending_room = room;
        return 1;
}

static int compositor_xwm_wait(struct compositor_xwm *x, const unsigned char *req, long n, unsigned int window, unsigned int what, unsigned int slot)
{
        struct compositor_xwm_pending *p;

        if (n <= 0 || compositor_xwm_queue(x, req, n) != 0)
                return -1;
        if (x->pending_count == x->pending_room && !compositor_xwm_pending_grow(x))
                return x->broken = 1, -1;
        p = &x->pending[(x->pending_at + x->pending_count++) % x->pending_room];
        *p = (struct compositor_xwm_pending){x->seq, window, what, slot};
        return 0;
}

static int compositor_xwm_ask(struct compositor_xwm *x, unsigned int window, unsigned int what, unsigned int property, unsigned int type)
{
        unsigned char req[32];
        return compositor_xwm_wait(x, req, xwm_enc_get_property(req, sizeof req, 0, window, property, type, 0,
                                                                what == COMPOSITOR_XWM_PROP_ICON ? (XWM_PROPERTY_MAX / 4) : 32),
                                   window, what, 0);
}

static void compositor_xwm_ask_owner(struct compositor_xwm *x, unsigned int window)
{
        unsigned char req[16];
        if (x->xres)
                compositor_xwm_wait(x, req, xwm_enc_xres_client_pid(req, sizeof req, x->xres, window), window, COMPOSITOR_XWM_PROP_OWNER, 0);
}

enum { XWM_PF_MAP = 1u, XWM_PF_PROMOTE = 2u, XWM_PF_NOTIFY = 4u, XWM_PF_TRAY = 8u };
static const struct { unsigned char what, flags; signed short atom; unsigned int type, err_bit; } xwm_prop_spec[] = {
        {COMPOSITOR_XWM_PROP_PROTOCOLS, XWM_PF_MAP | XWM_PF_PROMOTE | XWM_PF_NOTIFY, COMPOSITOR_XWM_A_PROTOCOLS, COMPOSITOR_XWM_ATOM_TYPE, COMPOSITOR_XWM_PROTO_KNOWN},
        {COMPOSITOR_XWM_PROP_TRANSIENT, XWM_PF_MAP | XWM_PF_NOTIFY, -COMPOSITOR_XWM_XA_WM_TRANSIENT_FOR, COMPOSITOR_XWM_XA_WINDOW, 0},
        {COMPOSITOR_XWM_PROP_WM_HINTS, XWM_PF_MAP | XWM_PF_PROMOTE | XWM_PF_NOTIFY, -COMPOSITOR_XWM_XA_WM_HINTS, 0, 0},
        {COMPOSITOR_XWM_PROP_NORMAL_HINTS, XWM_PF_MAP | XWM_PF_NOTIFY, -COMPOSITOR_XWM_XA_WM_NORMAL_HINTS, 0, 0},
        {COMPOSITOR_XWM_PROP_STATE, XWM_PF_MAP, COMPOSITOR_XWM_A_NET_STATE, COMPOSITOR_XWM_ATOM_TYPE, 0},
        {COMPOSITOR_XWM_PROP_CLASS, XWM_PF_MAP | XWM_PF_PROMOTE | XWM_PF_NOTIFY, -COMPOSITOR_XWM_XA_WM_CLASS, COMPOSITOR_XWM_XA_STRING, 0},
        {COMPOSITOR_XWM_PROP_STEAM, XWM_PF_NOTIFY, COMPOSITOR_XWM_A_STEAM, COMPOSITOR_XWM_ALLOW_TYPE, 0},
        {COMPOSITOR_XWM_PROP_STEAM_OPEN, XWM_PF_NOTIFY, COMPOSITOR_XWM_A_STEAM_OPEN, COMPOSITOR_XWM_ALLOW_TYPE, 0},
        {COMPOSITOR_XWM_PROP_ICON, XWM_PF_NOTIFY | XWM_PF_TRAY, COMPOSITOR_XWM_A_NET_ICON, COMPOSITOR_XWM_ALLOW_TYPE, 0},
        {COMPOSITOR_XWM_PROP_TYPE, XWM_PF_MAP, COMPOSITOR_XWM_A_TYPE, COMPOSITOR_XWM_ATOM_TYPE, COMPOSITOR_XWM_PROPS_READ},
        {COMPOSITOR_XWM_PROP_MOTIF, XWM_PF_MAP | XWM_PF_NOTIFY, COMPOSITOR_XWM_A_MOTIF_HINTS, 0, 0},
};

static unsigned int xwm_prop_atom(const struct compositor_xwm *x, unsigned int idx)
{
        return xwm_prop_spec[idx].atom < 0 ? (unsigned int)(-xwm_prop_spec[idx].atom) : x->atoms[xwm_prop_spec[idx].atom];
}

static void xwm_ask_props(struct compositor_xwm *x, struct compositor_xwm_window *w, unsigned int flag)
{
        for (unsigned int i = 0; i < (unsigned int)(sizeof xwm_prop_spec / sizeof xwm_prop_spec[0]); i++)
                if (xwm_prop_spec[i].flags & flag)
                        compositor_xwm_ask(x, w->id, xwm_prop_spec[i].what, xwm_prop_atom(x, i), xwm_prop_spec[i].type);
}

static void xwm_notify_prop(struct compositor_xwm *x, struct compositor_xwm_window *w, unsigned int atom)
{
        for (unsigned int i = 0; atom && i < (unsigned int)(sizeof xwm_prop_spec / sizeof xwm_prop_spec[0]); i++)
                if ((xwm_prop_spec[i].flags & XWM_PF_NOTIFY) && atom == xwm_prop_atom(x, i) &&
                    (!(xwm_prop_spec[i].flags & XWM_PF_TRAY) || (w->state & COMPOSITOR_XWM_TRAY_ICON)))
                {
                        compositor_xwm_ask(x, w->id, xwm_prop_spec[i].what, atom, xwm_prop_spec[i].type);
                        return;
                }
}

static void compositor_xwm_pending_done(struct compositor_xwm *x) { x->pending_at = (x->pending_at + 1) % x->pending_room; x->pending_count--; }

HOT static struct compositor_xwm_window *xwm_find(struct compositor_xwm *x, unsigned int id, int add);
static int compositor_xwm_covers(const struct compositor_xwm_window *w);
static void compositor_xwm_hints_push(const struct compositor_xwm_window *w);
static void compositor_xwm_app(struct compositor_xwm_window *w);

static void xwm_ask_title(struct compositor_xwm *x, struct compositor_xwm_window *w)
{
        if (w->asking)
                w->state |= COMPOSITOR_XWM_TITLE_AGAIN;
        else
                w->asking = compositor_xwm_ask(x, w->id, COMPOSITOR_XWM_PROP_NET_NAME, x->atoms[COMPOSITOR_XWM_A_NET_NAME], 0) == 0;
}

static void compositor_xwm_title_done(struct compositor_xwm *x, struct compositor_xwm_window *w)
{
        if (w->asking && --w->asking == 0 && (w->state & COMPOSITOR_XWM_TITLE_AGAIN))
                w->state &= ~COMPOSITOR_XWM_TITLE_AGAIN, xwm_ask_title(x, w);
}

static unsigned int compositor_xwm_text(char *to, const unsigned char *from, unsigned long count, int latin1)
{
        unsigned int length = 0;

        for (unsigned long at = 0; at < count && length < COMPOSITOR_XWM_TITLE - 4;)
        {
                unsigned char c = from[at];

                if (c < 0x80 || latin1)
                {
                        to[length++] = c < 0x20 || c == 0x7f ? ' ' : c < 0x80 ? (char)c : '?';
                        at++;
                }
                else
                {
                        memory_utf8_state state = {0};
                        unsigned long start = at;
                        b32 said = 0;

                        while (at < count && !(said = memory_utf8_feed(&state, from[at])))
                                at++;
                        if (said > 0)
                                for (unsigned long k = start; k <= at; k++)
                                        to[length++] = (char)from[k];
                        else
                                to[length++] = '?';
                        at = said > 0 ? at + 1 : start + 1;
                }
        }
        to[length] = 0;
        return length;
}

static void compositor_xwm_tray_click_cb(void *owner, unsigned int icon_win, int button)
{
        struct compositor_xwm *x = (struct compositor_xwm *)owner;
        unsigned char ev[XWM_EVENT_SIZE];

        if (!x || x->fd < 0)
                return;
        for (unsigned int t = XWM_EVENT_BUTTON_PRESS; t <= XWM_EVENT_BUTTON_RELEASE; t++)
                if (xwm_enc_input_event(ev, sizeof ev, t, (unsigned int)button, XWM_CURRENT_TIME, x->root, icon_win, 12, 12, 0) == XWM_EVENT_SIZE)
                        compositor_xwm_send_event(x, icon_win, 0, 0, ev, XWM_EVENT_SIZE);
        compositor_xwm_flush(x);
}

static void compositor_xwm_tray_dock(struct compositor_xwm *x, unsigned int icon_win)
{
        struct compositor_xwm_window *w;
        int slot = -1;
        unsigned char req[32], ev[XWM_EVENT_SIZE];
        unsigned int tray = compositor_xwm_id(x, COMPOSITOR_XWM_TRAY_WINDOW), info[2] = {0, 1};
        struct xwm_configure c = {.mask = XWM_CONFIGURE_X | XWM_CONFIGURE_Y | XWM_CONFIGURE_WIDTH | XWM_CONFIGURE_HEIGHT,
                                  .width = TASKBAR_TRAY_ITEM_PX, .height = TASKBAR_TRAY_ITEM_PX};

        if (!icon_win || !x || !x->claimed)
                return;
        /* A dock names a window of the client's own; the window manager's selection, proxy and tray windows have
           predictable ids that any client of the server can name, and docking one reparents it (x11 S3). */
        if (icon_win == compositor_xwm_id(x, COMPOSITOR_XWM_SEL_WINDOW) || icon_win == compositor_xwm_id(x, COMPOSITOR_XWM_PROXY_WINDOW) ||
            icon_win == compositor_xwm_id(x, COMPOSITOR_XWM_TRAY_WINDOW))
                return;
        for (int i = 0; i < TASKBAR_TRAY_ITEM_MAX; i++)
                if (compositor.tray[i].used && compositor.tray[i].owner == x && compositor.tray[i].id == icon_win)
                        return;
                else if (!compositor.tray[i].used && slot < 0)
                        slot = i;
        if (slot < 0)
                return;
        memory_zero(&compositor.tray[slot], sizeof compositor.tray[slot]);
        compositor.tray[slot].used = 1, compositor.tray[slot].owner = x, compositor.tray[slot].id = icon_win;
        compositor.tray[slot].click = compositor_xwm_tray_click_cb;
        compositor.tray_gen++, compositor_tabs_changed();
        if ((w = xwm_find(x, icon_win, 1)))
                w->state |= COMPOSITOR_XWM_TRAY_ICON;
        compositor_xwm_select(x, icon_win, XWM_MASK_PROPERTY_CHANGE | XWM_MASK_STRUCTURE_NOTIFY);
        compositor_xwm_queue(x, req, xwm_enc_reparent_window(req, sizeof req, icon_win, tray, 0, 0));
        compositor_xwm_configure(x, icon_win, &c);
        if (x->atoms[COMPOSITOR_XWM_A_XEMBED])
        {
                unsigned int xembed[5] = {XWM_CURRENT_TIME, 0, tray, 0, 0};

                if (xwm_enc_client_message(ev, sizeof ev, icon_win, x->atoms[COMPOSITOR_XWM_A_XEMBED], xembed, 5) == XWM_EVENT_SIZE)
                        compositor_xwm_send_event(x, icon_win, 0, 0, ev, XWM_EVENT_SIZE);
        }
        if (x->atoms[COMPOSITOR_XWM_A_XEMBED_INFO])
                compositor_xwm_put32(x, icon_win, x->atoms[COMPOSITOR_XWM_A_XEMBED_INFO], x->atoms[COMPOSITOR_XWM_A_XEMBED_INFO], info, 2);
        compositor_xwm_window_op(x, XWM_OP_MAP_WINDOW, icon_win);
        if (x->atoms[COMPOSITOR_XWM_A_NET_ICON])
                compositor_xwm_ask(x, icon_win, COMPOSITOR_XWM_PROP_ICON, x->atoms[COMPOSITOR_XWM_A_NET_ICON], COMPOSITOR_XWM_ALLOW_TYPE);
}

static void compositor_xwm_tray_undock(struct compositor_xwm *x, unsigned int icon_win)
{
        for (int i = 0; i < TASKBAR_TRAY_ITEM_MAX; i++)
                if (compositor.tray[i].used && compositor.tray[i].owner == x && (!icon_win || compositor.tray[i].id == icon_win))
                        compositor.tray[i].used = 0, compositor.tray_gen++, compositor_tabs_changed();
}

static void compositor_xwm_tray_icon_set(struct compositor_xwm *x, unsigned int icon_win, const unsigned char *val, unsigned long len)
{
        unsigned long best_w = 0, best_h = 0, best_off = 0, at = 0;

        while (at + 8 <= len)
        {
                unsigned long w = xwm_get32(val + at), h = xwm_get32(val + at + 4), need;

                at += 8;
                if (!w || !h || w > 512 || h > 512)
                        break;
                need = w * h * 4;
                if (need > len - at)
                        break;
                if (!best_w || (w >= TASKBAR_TRAY_ITEM_PX && w < best_w) || (best_w < TASKBAR_TRAY_ITEM_PX && w > best_w))
                        best_w = w, best_h = h, best_off = at;
                at += need;
        }
        for (int i = 0; best_w && i < TASKBAR_TRAY_ITEM_MAX; i++)
                if (compositor.tray[i].used && compositor.tray[i].owner == x && compositor.tray[i].id == icon_win)
                {
                        for (int py = 0; py < TASKBAR_TRAY_ITEM_PX; py++)
                                for (int px = 0; px < TASKBAR_TRAY_ITEM_PX; px++)
                                        compositor.tray[i].pixels[py * TASKBAR_TRAY_ITEM_PX + px] =
                                            xwm_get32(val + best_off + (((unsigned long)py * best_h / TASKBAR_TRAY_ITEM_PX) * best_w +
                                                                        (unsigned long)px * best_w / TASKBAR_TRAY_ITEM_PX) * 4);
                        compositor.tray[i].has_icon = 1, compositor.tray_gen++, compositor_tabs_changed();
                        break;
                }
}

static void xwm_property(struct compositor_xwm *x, const unsigned char *in, long size, const struct compositor_xwm_pending *p)
{
        struct compositor_xwm_window *w;
        struct xwm_property_reply r;

        if (xwm_parse_property_reply(in, (unsigned long)size, &r) != XWM_OK)
        {
                x->broken = 1;
                return;
        }
        if (p->what == COMPOSITOR_XWM_PROP_ICON)
        {
                if (r.format == XWM_FORMAT_32 && r.value_len >= 12)
                        compositor_xwm_tray_icon_set(x, p->window, r.value, r.value_len);
                return;
        }
        if (!(w = xwm_find(x, p->window, 0)))
                return;
        switch (p->what)
        {
        case COMPOSITOR_XWM_PROP_NET_NAME:
        case COMPOSITOR_XWM_PROP_NAME:
        {
                char text[COMPOSITOR_XWM_TITLE];
                unsigned int length = r.format == XWM_FORMAT_8 ? compositor_xwm_text(text, r.value, r.value_len, p->what == COMPOSITOR_XWM_PROP_NAME) : 0;

                if (p->what == COMPOSITOR_XWM_PROP_NET_NAME)
                {
                        if (length)
                                w->state |= COMPOSITOR_XWM_NET_TITLE;
                        else
                        {
                                w->state &= ~COMPOSITOR_XWM_NET_TITLE;
                                if (compositor_xwm_ask(x, w->id, COMPOSITOR_XWM_PROP_NAME, COMPOSITOR_XWM_XA_WM_NAME, 0) != 0)
                                        compositor_xwm_title_done(x, w);
                                return;
                        }
                }
                if ((p->what == COMPOSITOR_XWM_PROP_NET_NAME && length) || (p->what == COMPOSITOR_XWM_PROP_NAME && !(w->state & COMPOSITOR_XWM_NET_TITLE)))
                {
                        memory_copy(w->title, text, (positive)length + 1);
                        if ((w->state & COMPOSITOR_XWM_ADOPTED) && w->row >= 0)
                                compositor_row_set_title(w->row, w->title, length);
                        else
                                w->dirty = 1;
                }
                compositor_xwm_title_done(x, w);
                break;
        }
        case COMPOSITOR_XWM_PROP_STEAM:
        {
                unsigned int spid = r.format == XWM_FORMAT_32 && r.value_len >= 4 ? xwm_get32(r.value) : 0;

                if (compositor_xwm_steam.slot == x && compositor_xwm_steam.window == w->id)
                {
                        if (!spid)
                                memory_zero(&compositor_xwm_steam, sizeof compositor_xwm_steam);
                        else
                                compositor_xwm_steam.pid = spid;
                }
                else if (spid)
                {
                        struct compositor_xwm_window *cur = compositor_xwm_steam.slot && compositor_xwm_steam.slot->fd >= 0 && compositor_xwm_steam.window && compositor_xwm_steam.pid
                                                                ? xwm_find(compositor_xwm_steam.slot, compositor_xwm_steam.window, 0) : 0;
                        if (!cur || cur->owner != compositor_xwm_steam.pid)
                        {
                                memory_zero(&compositor_xwm_steam, sizeof compositor_xwm_steam);
                                compositor_xwm_steam.slot = x, compositor_xwm_steam.window = w->id, compositor_xwm_steam.pid = spid;
                        }
                }
                if (spid && !w->owner)
                        compositor_xwm_ask_owner(x, w->id);
                break;
        }
        case COMPOSITOR_XWM_PROP_STEAM_OPEN:
                if (compositor_xwm_steam.slot == x && compositor_xwm_steam.window == w->id)
                {
                        compositor_xwm_steam.open = r.format == XWM_FORMAT_32 && r.value_len >= 4 && xwm_get32(r.value) != 0;
                        compositor_xwm_steam.told = (unsigned int)(clock_monotonic_nanoseconds() / 1000000);
                }
                break;
        case COMPOSITOR_XWM_PROP_CLASS:
        {
                unsigned long at = 0, start = 0, length = 0;

                if (r.format == XWM_FORMAT_8)
                {
                        while (at < r.value_len && r.value[at])
                                at++;
                        if (at + 1 < r.value_len && r.value[at + 1])
                                start = at + 1;
                        for (length = 0; start + length < r.value_len && r.value[start + length] && length < WINDOWS_APP_ID - 1; length++)
                                if (r.value[start + length] < ' ' || r.value[start + length] >= 0x7f)
                                        break;
                        if (start + length < r.value_len && r.value[start + length])
                                length = 0;
                }
                memory_copy(w->app, r.value + start, length);
                w->app[length] = 0;
                compositor_xwm_app(w);
                break;
        }
        case COMPOSITOR_XWM_PROP_TRANSIENT:
                w->transient = r.format == XWM_FORMAT_32 && r.value_len >= 4 && xwm_get32(r.value) != w->id ? xwm_get32(r.value) : 0;
                break;
        case COMPOSITOR_XWM_PROP_STATE:
        {
                unsigned int had_fs = w->state & COMPOSITOR_XWM_FULLSCREEN, had_max = w->state & COMPOSITOR_XWM_MAXIMIZED;

                for (unsigned long at = 0; r.format == XWM_FORMAT_32 && at + 4 <= r.value_len; at += 4)
                        w->state |= xwm_net_state_bits(x, xwm_get32(r.value + at), 0);
                if ((w->state & COMPOSITOR_XWM_ADOPTED) && w->row >= 0 && w->row < COMPOSITOR_ROWS)
                {
                        if ((w->state & COMPOSITOR_XWM_FULLSCREEN) && !had_fs && !compositor.fullscreen[w->row] && w->left_fullscreen)
                                w->state &= ~COMPOSITOR_XWM_FULLSCREEN, compositor_xwm_net_state(x, w->id, (int)w->state), compositor_xwm_counted_refusal();
                        else if ((w->state & COMPOSITOR_XWM_FULLSCREEN) && !had_fs && !compositor.fullscreen[w->row])
                        {
                                if (!had_max)
                                        w->restore_w = w->width, w->restore_h = w->height;
                                compositor_fullscreen_on(w->row, compositor_xwm_covers(w));
                        }
                        else if ((w->state & COMPOSITOR_XWM_MAXIMIZED) && !had_max && !(w->state & COMPOSITOR_XWM_FULLSCREEN) &&
                                 compositor.arranged[w->row] == COMPOSITOR_FLOATING)
                                compositor_arrange(w->row, COMPOSITOR_ROOM);
                }
                break;
        }
        case COMPOSITOR_XWM_PROP_TYPE:
        {
                int was_desktop = !!(w->state & COMPOSITOR_XWM_DESKTOP);

                w->state &= ~(COMPOSITOR_XWM_POPUPTYPE | COMPOSITOR_XWM_DESKTOP);
                for (unsigned long at = 0; r.format == XWM_FORMAT_32 && at + 4 <= r.value_len; at += 4)
                {
                        unsigned int atom = xwm_get32(r.value + at);

                        for (unsigned int i = 0; atom && i < COMPOSITOR_XWM_ATOMS; i++)
                                if ((xwm_atom_spec[i].flags & COMPOSITOR_XWM_AF_POP) && atom == x->atoms[i])
                                        w->state |= COMPOSITOR_XWM_POPUPTYPE;
                        if (x->atoms[COMPOSITOR_XWM_A_TYPE_DESKTOP] && atom == x->atoms[COMPOSITOR_XWM_A_TYPE_DESKTOP])
                                w->state |= COMPOSITOR_XWM_DESKTOP;
                }
                w->state |= COMPOSITOR_XWM_PROPS_READ;
                if ((w->state & COMPOSITOR_XWM_DESKTOP) && !was_desktop && (w->state & COMPOSITOR_XWM_ADOPTED) && w->row >= 0)
                        compositor_layer_set(w->row, WINDOW_LAYER_BACKGROUND, -1);
                break;
        }
        case COMPOSITOR_XWM_PROP_PROTOCOLS:
                w->protocols = 0;
                for (unsigned long at = 0; r.format == XWM_FORMAT_32 && at + 4 <= r.value_len; at += 4)
                {
                        unsigned int atom = xwm_get32(r.value + at);

                        if (atom == x->atoms[COMPOSITOR_XWM_A_DELETE])
                                w->protocols |= COMPOSITOR_XWM_PROTO_DELETE;
                        else if (atom == x->atoms[COMPOSITOR_XWM_A_TAKE_FOCUS])
                                w->protocols |= COMPOSITOR_XWM_PROTO_FOCUS;
                }
                w->state |= COMPOSITOR_XWM_PROTO_KNOWN;
                if (w->row >= 0 && w->row == (int)compositor.focus)
                        x->focus_row = -3;
                break;
        case COMPOSITOR_XWM_PROP_WM_HINTS:
                w->no_input = r.format == XWM_FORMAT_32 && r.value_len >= 8 && (xwm_get32(r.value) & 1u) && xwm_get32(r.value + 4) == 0;
                if (w->row >= 0 && w->row == (int)compositor.focus)
                        x->focus_row = -3;
                break;
        case COMPOSITOR_XWM_PROP_MOTIF:
                if (r.format == XWM_FORMAT_32 && r.value_len >= 12 && (xwm_get32(r.value) & 2u) && xwm_get32(r.value + 8) == 0)
                        w->state |= COMPOSITOR_XWM_BORDERLESS;
                else
                        w->state &= ~COMPOSITOR_XWM_BORDERLESS;
                break;
        case COMPOSITOR_XWM_PROP_NORMAL_HINTS:
                w->min_w = w->min_h = w->max_w = w->max_h = 0, w->placed = 0;
                if (r.format == XWM_FORMAT_32 && r.value_len >= 4)
                {
                        unsigned int flags = xwm_get32(r.value);
                        unsigned long min_off = (flags & 16u) && r.value_len >= 28 ? 20ul : 0ul;

                        w->placed = (flags & 5u) != 0;
                        if (min_off)
                        {
                                int mw = (int)xwm_get32(r.value + min_off), mh = (int)xwm_get32(r.value + min_off + 4);
                                w->min_w = mw > 0 && mw <= 65535 ? (unsigned int)mw : 0;
                                w->min_h = mh > 0 && mh <= 65535 ? (unsigned int)mh : 0;
                        }
                        if ((flags & 32u) && r.value_len >= 36)
                        {
                                int xw = (int)xwm_get32(r.value + 28), xh = (int)xwm_get32(r.value + 32);
                                w->max_w = xw > 0 ? (xw > 65535 ? 65535u : (unsigned int)xw) : 0;
                                w->max_h = xh > 0 ? (xh > 65535 ? 65535u : (unsigned int)xh) : 0;
                        }
                        if (w->max_w && w->min_w && w->max_w < w->min_w)
                                w->max_w = w->min_w;
                        if (w->max_h && w->min_h && w->max_h < w->min_h)
                                w->max_h = w->min_h;
                }
                compositor_xwm_hints_push(w);
                break;
        default: break;
        }
}

/* ---- window table and O(1) row -> (slot, win) index ---- */

static struct { unsigned char slot_plus1; int win; } xwm_row_map[WINDOWS];
static int compositor_xwm_is_popup(const struct compositor_xwm_window *w);

static unsigned char xwm_slot_tag(const struct compositor_xwm *x)
{
        long idx = x - compositor_xwm_slots;
        return idx >= 0 && idx < COMPOSITOR_XWM_SLOTS ? (unsigned char)(idx + 1) : 0;
}

static void xwm_row_unbind(const struct compositor_xwm *x, struct compositor_xwm_window *w)
{
        if (w->row >= 0 && w->row < WINDOWS && xwm_row_map[w->row].slot_plus1 == xwm_slot_tag(x) &&
            x->window && xwm_row_map[w->row].win == (int)(w - x->window))
                xwm_row_map[w->row].slot_plus1 = 0;
        w->row = -1;
}

static void xwm_row_bind(const struct compositor_xwm *x, struct compositor_xwm_window *w, int row)
{
        unsigned char tag = xwm_slot_tag(x);

        if (w->row != row)
                xwm_row_unbind(x, w);
        w->row = row;
        if (row < 0 || row >= WINDOWS || !tag || !x->window)
                return;
        if (xwm_row_map[row].slot_plus1 == tag && xwm_row_map[row].win >= 0 && xwm_row_map[row].win < x->window_table.count)
        {
                const struct compositor_xwm_window *cur = &x->window[xwm_row_map[row].win];
                if (cur->id && cur->row == row && !compositor_xwm_is_popup(cur) && compositor_xwm_is_popup(w))
                        return;
        }
        xwm_row_map[row].slot_plus1 = tag, xwm_row_map[row].win = (int)(w - x->window);
}

HOT static struct compositor_xwm_window *xwm_row_lookup(const struct compositor_xwm *only, int row, struct compositor_xwm **slot_out)
{
        if (unlikely((unsigned int)row >= WINDOWS || !xwm_row_map[row].slot_plus1))
                return 0;
        struct compositor_xwm *sx = &compositor_xwm_slots[xwm_row_map[row].slot_plus1 - 1];
        int wi = xwm_row_map[row].win;

        if ((only && sx != only) || sx->fd < 0 || !sx->window || wi < 0 || wi >= sx->window_table.count || !sx->window[wi].id || sx->window[wi].row != row)
                return 0;
        if (slot_out)
                *slot_out = sx;
        return &sx->window[wi];
}

// The X client behind a row, as the X server named it (XRes, compositor_xwm_owned), and that server's pid in *server: 0 for a row no X window
// has, or whose client the server has not named. The server is the Wayland client the window manager's connection belongs to
// (compositor_xwm_server: the child of the process that connected it, as xwayland-moonwater starts Xwayland), or the connecting process
// itself, for a server that made its own connection. The connecting process alone was the wrapper, never the Wayland client, so no X
// row of the steam session was ever vouched for: no game mode for X games and no focused page for them.
static unsigned int compositor_xwm_row_owner(int row, unsigned int *server)
{
        struct compositor_xwm *sx = 0;
        const struct compositor_xwm_window *w = xwm_row_lookup(0, row, &sx);

        if (!w || !w->owner || !sx || sx->peer_pid <= 0)
                return 0;
        *server = compositor_xwm_server(sx) >= 0 && sx->client_pid > 0 ? (unsigned int)sx->client_pid : (unsigned int)sx->peer_pid;
        return w->owner;
}

/*
        The row of the X window `id` for a Wayland client of pid `pid` that would stand in for its pictures (canvas_swapchain_v1, a game's
        Vulkan layer, which runs in the game: the X client that made the window): the window's row once its X server has named its
        client and that client is `pid`; -WAYLAND_STAND_WAITING while the window is not shown or its client not named yet;
        -WAYLAND_STAND_NOT_OWNER for another client's window, or on a server that names no client (no XRes); -WAYLAND_STAND_UNKNOWN
        when no X server of the compositor has a window of that id.
*/
static long compositor_xwm_stand_row(unsigned int id, unsigned int pid)
{
        long answer = -WAYLAND_STAND_UNKNOWN;

        for (int at = 0; at < COMPOSITOR_XWM_SLOTS; at++)
        {
                struct compositor_xwm *x = &compositor_xwm_slots[at];
                const struct compositor_xwm_window *w = x->fd >= 0 && x->window ? xwm_find(x, id, 0) : 0;

                if (!w)
                        continue;
                if (!x->xres || !pid || (w->owner && w->owner != pid))
                {
                        answer = -WAYLAND_STAND_NOT_OWNER;
                        continue;
                }
                return !w->owner || w->row < 0 || w->row >= COMPOSITOR_ROWS ? -WAYLAND_STAND_WAITING : (long)w->row;
        }
        return answer;
}

static void xwm_release(struct compositor_xwm *x, struct compositor_xwm_window *w)
{
        long slot;
        unsigned long hole = 0;

        xwm_row_unbind(x, w);
        if (w->id && compositor_xwm_steam.slot == x && compositor_xwm_steam.window == w->id)
                memory_zero(&compositor_xwm_steam, sizeof compositor_xwm_steam);
        if (w->id && w->id == x->focus_window)
                x->focus_window = 0, x->focus_row = -3;
        if (!w->id || (slot = canvas_htab_find(&x->window_index, &x->window_table, w->id, 0, &hole, 0)) < 0)
        {
                w->id = 0;
                return;
        }
        canvas_htab_del(&x->window_index, &x->window_table, hole, 0);
        w->id = 0;
        wayland_table_give(&x->window_table, slot);
        x->windows--;
}

HOT static struct compositor_xwm_window *xwm_find(struct compositor_xwm *x, unsigned int id, int add)
{
        struct compositor_xwm_window *free;
        long slot;

        if (!id)
                return 0;
        if ((slot = canvas_htab_find(&x->window_index, &x->window_table, id, 0, 0, 0)) >= 0)
                return &x->window[slot];
        if (!add)
                return 0;
        if (x->windows >= COMPOSITOR_XWM_WINDOWS_MAX ||
            !canvas_htab_grow(&x->window_index, &x->window_table, x->windows, -1, 0) ||
            (slot = xwm_tbl_take(&x->window_table, sizeof(struct compositor_xwm_window))) < 0)
                return x->refused++, compositor_xwm_counted[1]++, (struct compositor_xwm_window *)0;
        x->window = (struct compositor_xwm_window *)x->window_table.at;
        free = &x->window[slot];
        memory_zero(free, sizeof(*free));
        free->id = id, free->row = -1, free->sent_x = free->sent_y = COMPOSITOR_XWM_NO_PLACE;
        x->windows++;
        canvas_htab_put(&x->window_index, id, slot, 0);
        return free;
}

static void xwm_forget(struct compositor_xwm *x, unsigned int id) { struct compositor_xwm_window *w = xwm_find(x, id, 0); if (w) xwm_release(x, w); }

static void xwm_unmapped(struct compositor_xwm *x, struct compositor_xwm_window *w)
{
        if (w->id && w->id == x->focus_window)
                x->focus_window = 0, x->focus_row = -3;
        xwm_row_unbind(x, w);
        w->state &= ~(COMPOSITOR_XWM_ADOPTED | COMPOSITOR_XWM_RAISED | COMPOSITOR_XWM_COVERED | COMPOSITOR_XWM_HIDDEN);
        w->closes = 0, w->sent_x = w->sent_y = COMPOSITOR_XWM_NO_PLACE, w->sent_w = w->sent_h = 0, x->last_stack_gen = 0;
}

static int compositor_xwm_close_owed(const struct compositor_xwm_window *w)
{
        return w->row >= 0 && w->row < COMPOSITOR_ROWS && (compositor.state[w->row] & WINDOW_CLOSING) && w->closes != compositor.close_asks[w->row] &&
               (compositor_xwm_is_popup(w) || (w->state & COMPOSITOR_XWM_PROTO_KNOWN));
}

static int compositor_xwm_covers(const struct compositor_xwm_window *w)
{
        unsigned int count;
        const int (*screens)[4] = compositor_screens(&count);
        int x = compositor_xwm_from_x(w->x, 0), y = compositor_xwm_from_x(w->y, 1);

        if ((w->state & COMPOSITOR_XWM_POPUPTYPE) || w->transient)
                return -1;
        for (unsigned int at = 0; at < count; at++)
                if (x <= screens[at][0] && y <= screens[at][1] &&
                    x + (int)w->width >= screens[at][0] + screens[at][2] &&
                    y + (int)w->height >= screens[at][1] + screens[at][3])
                        return (int)at;
        return -1;
}

static int compositor_xwm_is_popup(const struct compositor_xwm_window *w)
{
        return (w->state & COMPOSITOR_XWM_POPUPISH) && !(w->state & (COMPOSITOR_XWM_FULLSCREEN | COMPOSITOR_XWM_DESKTOP)) &&
               !((w->state & COMPOSITOR_XWM_OVERRIDE) && !w->transient && compositor_xwm_covers(w) >= 0);
}

static void compositor_xwm_hints_push(const struct compositor_xwm_window *w)
{
        unsigned int scale;

        if (!w || w->row < 0 || w->row >= COMPOSITOR_ROWS || !compositor.table)
                return;
        scale = compositor.table->window_scale[w->row];
        scale = scale >= 25 && scale <= 800 ? scale : (unsigned int)compositor.scale;
        compositor.table->least_width[w->row] = w->min_w ? (unsigned int)(((unsigned long)w->min_w * 100ul + scale / 2ul) / scale) : 0;
        compositor.table->least_height[w->row] = w->min_h ? (unsigned int)(((unsigned long)w->min_h * 100ul + scale / 2ul) / scale) : 0;
        compositor.table->most_width[w->row] = w->max_w ? (unsigned int)(((unsigned long)w->max_w * 100ul + scale / 2ul) / scale) : 0;
        compositor.table->most_height[w->row] = w->max_h ? (unsigned int)(((unsigned long)w->max_h * 100ul + scale / 2ul) / scale) : 0;
}

static void compositor_xwm_app(struct compositor_xwm_window *w)
{
        if (w->row >= 0 && w->row < COMPOSITOR_ROWS && w->app[0] && compositor.table &&
            windows_app_id_set(compositor.table, (unsigned int)w->row, w->app))
                compositor_take(w->row);
}

static void xwm_promote_props(struct compositor_xwm *x, struct compositor_xwm_window *w)
{
        compositor_xwm_select(x, w->id, XWM_MASK_PROPERTY_CHANGE);
        xwm_ask_title(x, w);
        xwm_ask_props(x, w, XWM_PF_PROMOTE);
        if (!w->owner)
                compositor_xwm_ask_owner(x, w->id);
}

static void xwm_promote_fullscreen(struct compositor_xwm *x, struct compositor_xwm_window *w, unsigned int prev_w, unsigned int prev_h)
{
        if (!w->restore_w || !w->restore_h)
                w->restore_w = prev_w ? prev_w : w->width, w->restore_h = prev_h ? prev_h : w->height;
        w->state |= COMPOSITOR_XWM_FULLSCREEN | COMPOSITOR_XWM_COVERED;
        compositor_xwm_net_state(x, w->id, (int)w->state);
        xwm_promote_props(x, w);
}

static void xwm_restore_size(struct compositor_xwm *x, struct compositor_xwm_window *w)
{
        if (w->restore_w && w->restore_h)
        {
                struct xwm_configure c = {.mask = XWM_CONFIGURE_WIDTH | XWM_CONFIGURE_HEIGHT, .width = w->restore_w, .height = w->restore_h};
                compositor_xwm_configure(x, w->id, &c);
                w->sent_w = w->restore_w, w->sent_h = w->restore_h;
        }
}

/* A newly mapped X toplevel takes the keys, as an asked Wayland toplevel does: a game the person launched is focused when it maps
   (the first-take ask is the table's, and a row already taken is focused now). Popups, transients and windows that take no input
   (WM_HINTS input false: focus comes by WM_TAKE_FOCUS) never take them here. The compositor's focus then sets the X input focus. */
static void compositor_xwm_takes_keys(const struct compositor_xwm_window *w, int row)
{
        if (!compositor.table || row < 0 || row >= COMPOSITOR_ROWS || compositor_xwm_is_popup(w) || w->transient || w->no_input)
                return;
        __atomic_store_n(&compositor.table->asked[row], 1u, __ATOMIC_RELEASE);
        if (compositor.showing[row] && compositor_visible(row))
                compositor_raise(row), compositor_focus(row);
}

static void compositor_xwm_resolve(struct compositor_xwm *x, struct compositor_xwm_window *w)
{
        long row;
        int covers = -1, fresh = 0;

        if (!(w->state & COMPOSITOR_XWM_SERIAL) || (w->state & COMPOSITOR_XWM_TRAY_ICON) ||
            (!(w->state & COMPOSITOR_XWM_OVERRIDE) && !(w->state & COMPOSITOR_XWM_PROPS_READ)))
                return;
        if (!(w->state & COMPOSITOR_XWM_ADOPTED) || w->dirty)
        {
                fresh = !(w->state & COMPOSITOR_XWM_ADOPTED);
                struct compositor_xwm_window *owner = w->transient ? xwm_find(x, w->transient, 0) : 0;
                long how = (compositor_xwm_is_popup(w) ? WAYLAND_XWINDOW_POPUP : 0) | (w->placed ? WAYLAND_XWINDOW_PLACED : 0) |
                           WAYLAND_XWINDOW_OWNER(owner && owner->row >= 0 && owner->row < COMPOSITOR_ROWS ? owner->row : -1);

                if (!(w->state & (COMPOSITOR_XWM_ADOPTED | COMPOSITOR_XWM_FULLSCREEN)) &&
                    (w->state & (COMPOSITOR_XWM_OVERRIDE | COMPOSITOR_XWM_BORDERLESS)) &&
                    (covers = compositor_xwm_covers(w)) >= 0)
                        xwm_promote_fullscreen(x, w, w->width, w->height);
                if ((row = wayland_xwindow_adopt(compositor_xwm_server(x), w->serial, how, compositor_xwm_from_x(w->x, 0),
                                                 compositor_xwm_from_x(w->y, 1), w->title)) == -2)
                        return;
                w->state |= COMPOSITOR_XWM_ADOPTED, w->dirty = 0;
        }
        else if (w->row < 0)
                row = wayland_serial_row(compositor_xwm_server(x), w->serial);
        else
                return;
        if (row < 0 || w->row == (int)row)
                return;
        xwm_row_bind(x, w, (int)row);
        xwm_shape_ask(x, w);
        x->last_stack_gen = 0;
        compositor_xwm_app(w);
        compositor_xwm_owned(w);
        compositor_xwm_hints_push(w);
        if (w->state & COMPOSITOR_XWM_DESKTOP)
                compositor_layer_set((int)row, WINDOW_LAYER_BACKGROUND, -1);
        else
        {
                if (w->state & COMPOSITOR_XWM_ABOVE)
                        compositor_layer_set((int)row, WINDOW_LAYER_ABOVE, -1);
                if (w->state & COMPOSITOR_XWM_FULLSCREEN)
                        compositor_fullscreen_on((int)row, covers >= 0 ? covers : compositor_xwm_covers(w));
                else if (w->state & COMPOSITOR_XWM_MAXIMIZED)
                        compositor_arrange((int)row, COMPOSITOR_ROOM);
        }
        if (fresh)
                compositor_xwm_takes_keys(w, (int)row);
}

// An ask of a window's own the person's choice outweighs (compositor_xwm_may_show, left_fullscreen): counted, and said in the compositor's log
// for the first few, so a game that fights the person shows there.
static unsigned int compositor_xwm_refusals;
static void compositor_xwm_counted_refusal(void)
{
        if (compositor_xwm_refusals++ < 8)
                log_direct(str("xwm: a window's own ask to be shown or made fullscreen was refused: the person chose otherwise (another program has the keys, or they took it out of fullscreen)\n"));
}

/*
        Whether a window's own ask to be the one shown is taken: _NET_ACTIVE_WINDOW from it, or WM_CHANGE_STATE or _NET_WM_STATE
        back from hidden. A pager's ask (source 2: a taskbar, Steam's overlay) is a person's and is always taken. A program's is taken
        while it, or another of its own windows, has the keys, or nothing does; never while another program has them. A Proton game
        the person left for another window through Scenes, the taskbar or a click asked straight to be shown again, was revealed,
        and its fullscreen Space came back over what the person chose (Skyrim). Its own windows are told by the X server's owner of
        each (XRes) or, where that is not known, by the client's resource base.
*/
static int compositor_xwm_may_show(struct compositor_xwm *x, const struct compositor_xwm_window *w, unsigned int source)
{
        int focus = (int)compositor.focus;
        const struct compositor_xwm_window *f;
        unsigned int base = x->resource_mask ? ~x->resource_mask : 0xffe00000u;

        if (source == 2 || focus < 0 || focus >= COMPOSITOR_ROWS || !compositor.showing[focus] || focus == w->row)
                return 1;
        f = xwm_row_lookup(x, focus, 0);
        if (f && ((f->owner && f->owner == w->owner) || (!f->owner && (f->id & base) == (w->id & base))))
                return 1;
        compositor_xwm_counted_refusal();
        return 0;
}

static void compositor_xwm_fullscreen(struct compositor_xwm *x, struct compositor_xwm_window *w, int on)
{
        struct compositor_row_facts f;
        struct xwm_configure c;

        w->state &= ~COMPOSITOR_XWM_COVERED;
        if (on == !!(w->state & COMPOSITOR_XWM_FULLSCREEN))
                return;
        // A window the person took out of fullscreen stays out of it on its own ask (a Proton game put back what F11 took away): it is
        // told its state again, without fullscreen, and goes back when the person puts it back.
        if (on && w->left_fullscreen)
        {
                compositor_xwm_net_state(x, w->id, (int)w->state);
                compositor_xwm_counted_refusal();
                return;
        }
        if (on)
        {
                int has_f;

                if (!(w->state & COMPOSITOR_XWM_MAXIMIZED))
                        w->restore_w = w->width, w->restore_h = w->height;
                w->state |= COMPOSITOR_XWM_FULLSCREEN;
                if (w->row >= 0)
                        compositor_fullscreen_row(w->row);
                has_f = compositor_row_facts(w->row, &f) == 0;
                c.mask = XWM_CONFIGURE_X | XWM_CONFIGURE_Y | XWM_CONFIGURE_WIDTH | XWM_CONFIGURE_HEIGHT;
                c.x = compositor_xwm_to_x(has_f ? f.x : 0, 0), c.y = compositor_xwm_to_x(has_f ? f.y : 0, 1);
                c.width = has_f && f.want_width ? f.want_width : has_f && f.width ? f.width : (compositor.width > 0 ? (unsigned int)compositor.width : 1u);
                c.height = has_f && f.want_height ? f.want_height : has_f && f.height ? f.height : (compositor.height > 0 ? (unsigned int)compositor.height : 1u);
                compositor_xwm_configure(x, w->id, &c);
                w->sent_x = c.x, w->sent_y = c.y, w->sent_w = c.width, w->sent_h = c.height;
                compositor_xwm_net_state(x, w->id, (int)w->state);
        }
        else
        {
                w->state &= ~COMPOSITOR_XWM_FULLSCREEN;
                compositor_xwm_net_state(x, w->id, (int)w->state);
                if (w->row >= 0 && compositor.fullscreen[w->row])
                        compositor_fullscreen_off(w->row);
                else if (w->row >= 0 && (w->state & COMPOSITOR_XWM_MAXIMIZED))
                        compositor_arrange(w->row, COMPOSITOR_ROOM);
                else if (w->row >= 0)
                        compositor_float(w->row);
                if (w->row < 0 || compositor.arranged[w->row] == COMPOSITOR_FLOATING)
                        xwm_restore_size(x, w);
        }
}

static void compositor_xwm_clamp_size(const struct compositor_xwm_window *w, unsigned int *width, unsigned int *height)
{
        if (!w || (w->state & COMPOSITOR_XWM_FULLSCREEN))
                return;
        if (w->row >= 0 && w->row < COMPOSITOR_ROWS && compositor.table)
        {
                int iw = (int)*width, ih = (int)*height;

                compositor_row_clamp(w->row, &iw, &ih);
                *width = iw > 0 ? (unsigned int)iw : 1u, *height = ih > 0 ? (unsigned int)ih : 1u;
                return;
        }
        if (w->max_w && *width > w->max_w) *width = w->max_w;
        if (w->max_h && *height > w->max_h) *height = w->max_h;
        if (w->min_w && *width < w->min_w) *width = w->min_w;
        if (w->min_h && *height < w->min_h) *height = w->min_h;
}

static void compositor_xwm_sync(struct compositor_xwm *x, struct compositor_xwm_window *w)
{
        struct compositor_row_facts f;
        struct xwm_configure c = {0};
        unsigned int prev_state = w->state;

        if (compositor_row_facts(w->row, &f) != 0)
                return;
        // The compositor's side changed it (the person's F11 or Super+F11, Scenes): what the person chose is kept against the window's own asks.
        if (!!(w->state & COMPOSITOR_XWM_FULLSCREEN) != !!f.fullscreen)
        {
                if (f.fullscreen)
                        w->restore_w = w->width, w->restore_h = w->height, w->state |= COMPOSITOR_XWM_FULLSCREEN, w->left_fullscreen = 0;
                else
                        w->state &= ~(COMPOSITOR_XWM_FULLSCREEN | COMPOSITOR_XWM_COVERED), w->left_fullscreen = 1;
        }
        if (!(w->state & COMPOSITOR_XWM_FULLSCREEN))
                w->state = f.filling ? (w->state | COMPOSITOR_XWM_MAXIMIZED) : (w->state & ~COMPOSITOR_XWM_MAXIMIZED);
        w->state = f.layer == WINDOW_LAYER_ABOVE ? (w->state | COMPOSITOR_XWM_ABOVE) : (w->state & ~COMPOSITOR_XWM_ABOVE);
        w->state = f.minimized ? (w->state | COMPOSITOR_XWM_HIDDEN) : (w->state & ~COMPOSITOR_XWM_HIDDEN);
        w->state = f.focused ? (w->state | COMPOSITOR_XWM_FOCUSED) : (w->state & ~COMPOSITOR_XWM_FOCUSED);
        if (w->state != prev_state)
        {
                compositor_xwm_net_state(x, w->id, (int)w->state);
                if ((w->state ^ prev_state) & COMPOSITOR_XWM_HIDDEN)
                        compositor_xwm_wm_state(x, w->id, f.minimized ? COMPOSITOR_XWM_STATE_ICONIC : COMPOSITOR_XWM_STATE_NORMAL);
        }
        c.x = compositor_xwm_to_x(f.x, 0), c.y = compositor_xwm_to_x(f.y, 1);
        if (c.x != w->sent_x || c.y != w->sent_y || c.x != w->x || c.y != w->y)
                c.mask |= XWM_CONFIGURE_X | XWM_CONFIGURE_Y;
        if (f.want_width && f.want_height)
        {
                unsigned int cw = f.want_width, ch = f.want_height;

                compositor_xwm_clamp_size(w, &cw, &ch);
                if ((cw != w->sent_w || ch != w->sent_h) && (!f.resizing || w->sync_present != compositor_metric_presents))
                        c.mask |= XWM_CONFIGURE_WIDTH | XWM_CONFIGURE_HEIGHT, c.width = cw, c.height = ch, w->sync_present = compositor_metric_presents;
        }
        if (!c.mask)
                return;
        compositor_xwm_configure(x, w->id, &c);
        if (c.mask & XWM_CONFIGURE_X)
                w->sent_x = w->x = c.x, w->sent_y = w->y = c.y;
        if (c.mask & XWM_CONFIGURE_WIDTH)
                w->sent_w = w->width = c.width, w->sent_h = w->height = c.height;
}

static void compositor_xwm_moveresize(struct compositor_xwm_window *w, unsigned int direction)
{
        static const unsigned int edge_of[8] = {
            COMPOSITOR_EDGE_TOP | COMPOSITOR_EDGE_LEFT, COMPOSITOR_EDGE_TOP, COMPOSITOR_EDGE_TOP | COMPOSITOR_EDGE_RIGHT, COMPOSITOR_EDGE_RIGHT,
            COMPOSITOR_EDGE_BOTTOM | COMPOSITOR_EDGE_RIGHT, COMPOSITOR_EDGE_BOTTOM, COMPOSITOR_EDGE_BOTTOM | COMPOSITOR_EDGE_LEFT, COMPOSITOR_EDGE_LEFT};

        if (!w || w->row < 0)
                return;
        if (direction == COMPOSITOR_XWM_MOVERESIZE_CANCEL)
                (void)compositor_grab_begin_client(w->row, 0, 0, 1);
        else if (direction == COMPOSITOR_XWM_MOVERESIZE_MOVE)
                (void)compositor_grab_begin_client(w->row, 0, 1, 0);
        else if (direction < 8)
                (void)compositor_grab_begin_client(w->row, edge_of[direction], 0, 0);
}

/* ---- selections & dynamic atom cache ---- */

#define COMPOSITOR_XWM_TRANSFER_NS 5000000000ull
#define COMPOSITOR_XWM_PIPE 262144
#define COMPOSITOR_XWM_PIPE_SLACK 8192
#define COMPOSITOR_XWM_PIECE_UNITS (XWM_PROPERTY_MAX / XWM_UNIT)
enum { COMPOSITOR_XWM_IN_TARGETS = 1, COMPOSITOR_XWM_IN_ASKED, COMPOSITOR_XWM_IN_PIECES, COMPOSITOR_XWM_IN_NAMING };
enum { COMPOSITOR_XWM_OUT_START = 0, COMPOSITOR_XWM_OUT_WAIT, COMPOSITOR_XWM_OUT_NEXT, COMPOSITOR_XWM_OUT_MULTIPLE };
enum { COMPOSITOR_XWM_FROM_FILE = 1, COMPOSITOR_XWM_FROM_PIPE = 2 };
#define COMPOSITOR_XWM_FIONREAD 0x541b
#define COMPOSITOR_XWM_F_SETFL 4
#define COMPOSITOR_XWM_F_SETPIPE_SZ 1031
#define COMPOSITOR_XWM_F_GETPIPE_SZ 1032
#define COMPOSITOR_XWM_F_ADD_SEALS 1033
#define COMPOSITOR_XWM_O_NONBLOCK 0x800
#define COMPOSITOR_XWM_O_CLOEXEC 0x80000

static long compositor_wl_pump(int in, int out);
static void compositor_wl_pump_nudge(long at);

static unsigned int compositor_xwm_id(struct compositor_xwm *x, unsigned int number) { return x->resource_base | (number & x->resource_mask); }
static int compositor_xwm_same_client(struct compositor_xwm *x, unsigned int a, unsigned int b) { return a && b && (a & ~x->resource_mask) == (b & ~x->resource_mask); }

static void compositor_xwm_atom_indexed(struct compositor_xwm *x, long slot)
{
        unsigned int atom = x->atom[slot].atom;

        if (!atom)
                return;
        if (!canvas_htab_grow(&x->atom_index, &x->atom_table, x->atoms_indexed, slot, 0))
                x->atoms_indexed = -1, wayland_table_drop(&x->atom_index);
        else
                canvas_htab_put(&x->atom_index, atom, slot, 0), x->atoms_indexed++;
}

static long xwm_atom_add(struct compositor_xwm *x, unsigned int atom, const unsigned char *name, unsigned long length, int known)
{
        long at;

        if (x->atom_table.count >= COMPOSITOR_XWM_ATOMS_MAX)
                return -1; /* a hostile client's XdndEnter or TARGETS names no more than this many atoms per connection (x11 S1) */
        wayland_tables_host(&compositor_wl_host);
        x->name_table.unit = 1;
        if (length && !wayland_table_reach(&x->name_table, x->name_bytes + (long)length))
                return -1;
        if ((at = xwm_tbl_take(&x->atom_table, sizeof(struct compositor_xwm_atom))) < 0)
                return -1;
        x->atom = (struct compositor_xwm_atom *)x->atom_table.at;
        x->atom[at] = (struct compositor_xwm_atom){atom, (unsigned int)x->name_bytes, (unsigned int)length, (unsigned char)known};
        if (length)
                memory_copy(x->name_table.at + x->name_bytes, name, (positive)length);
        x->name_bytes += (long)length;
        if (x->atoms_indexed >= 0)
                compositor_xwm_atom_indexed(x, at);
        return at;
}

static long xwm_atom_index(struct compositor_xwm *x, unsigned int atom)
{
        if (!atom)
                return -1;
        if (x->atoms_indexed >= 0)
                return canvas_htab_find(&x->atom_index, &x->atom_table, atom, 1, 0, 0);
        for (long at = 0; at < x->atom_table.count; at++)
                if (x->atom[at].atom == atom)
                        return at;
        return -1;
}

static long compositor_xwm_atom_named(struct compositor_xwm *x, const unsigned char *name, unsigned long length)
{
        for (long at = 0; length && at < x->atom_table.count; at++)
                if (x->atom[at].length == length && !memory_compare(x->name_table.at + x->atom[at].at, name, (positive)length))
                        return at;
        return -1;
}

static const unsigned char *compositor_xwm_atom_name(struct compositor_xwm *x, unsigned int atom, unsigned long *length)
{
        long at = xwm_atom_index(x, atom);
        if (at < 0 || !x->atom[at].known)
                return 0;
        *length = x->atom[at].length;
        return x->name_table.at + x->atom[at].at;
}

static unsigned int compositor_xwm_intern_name(struct compositor_xwm *x, const unsigned char *name, unsigned long length)
{
        unsigned char req[XWM_REQUEST_SHORT + XWM_ATOM_NAME_MAX];
        long at = compositor_xwm_atom_named(x, name, length);

        if (at >= 0)
                return x->atom[at].known ? x->atom[at].atom : 0;
        if (!length || length > XWM_ATOM_NAME_MAX || (at = xwm_atom_add(x, 0, name, length, 0)) < 0)
                return 0;
        compositor_xwm_wait(x, req, xwm_enc_intern_atom(req, sizeof req, 0, name, length), 0, COMPOSITOR_XWM_PROP_INTERN, (unsigned int)at);
        return 0;
}

static void compositor_xwm_name_atom(struct compositor_xwm *x, unsigned int atom)
{
        unsigned char req[XWM_REQUEST_SHORT];
        long at;

        if (atom && xwm_atom_index(x, atom) < 0 && (at = xwm_atom_add(x, atom, 0, 0, 0)) >= 0)
                compositor_xwm_wait(x, req, xwm_enc_get_atom_name(req, sizeof req, atom), 0, COMPOSITOR_XWM_PROP_ATOM_NAME, (unsigned int)at);
}

static void xwm_interned(struct compositor_xwm *x, const unsigned char *in, long size, const struct compositor_xwm_pending *p)
{
        struct xwm_atom_reply r;

        if (xwm_parse_atom_reply(in, (unsigned long)size, &r) != XWM_OK)
                x->broken = 1;
        else if ((long)p->slot < x->atom_table.count && r.atom)
        {
                b32 had = x->atom[p->slot].atom != 0;

                x->atom[p->slot].atom = r.atom, x->atom[p->slot].known = 1;
                if (!had && x->atoms_indexed >= 0)
                        compositor_xwm_atom_indexed(x, (long)p->slot);
        }
}

static void compositor_xwm_named(struct compositor_xwm *x, const unsigned char *in, long size, const struct compositor_xwm_pending *p)
{
        struct xwm_atom_name_reply r;

        if (xwm_parse_atom_name_reply(in, (unsigned long)size, &r) != XWM_OK)
        {
                x->broken = 1;
                return;
        }
        if ((long)p->slot >= x->atom_table.count)
                return;
        x->name_table.unit = 1;
        if (r.name_len && wayland_table_reach(&x->name_table, x->name_bytes + (long)r.name_len))
        {
                memory_copy(x->name_table.at + x->name_bytes, r.name, (positive)r.name_len);
                x->atom[p->slot].at = (unsigned int)x->name_bytes, x->atom[p->slot].length = (unsigned int)r.name_len;
                x->name_bytes += (long)r.name_len;
        }
        x->atom[p->slot].known = 1;
}

static void compositor_xwm_atoms_seed(struct compositor_xwm *x)
{
        static const struct { unsigned int atom; const char *name; } predefined[] = {
                {COMPOSITOR_XWM_XA_PRIMARY, "PRIMARY"}, {COMPOSITOR_XWM_ATOM_TYPE, "ATOM"},
                {COMPOSITOR_XWM_XA_INTEGER, "INTEGER"}, {COMPOSITOR_XWM_XA_STRING, "STRING"}};

        for (unsigned int at = 0; at < COMPOSITOR_XWM_ATOMS + 4; at++)
        {
                const char *name = at < COMPOSITOR_XWM_ATOMS ? xwm_atom_spec[at].name : predefined[at - COMPOSITOR_XWM_ATOMS].name;
                unsigned int atom = at < COMPOSITOR_XWM_ATOMS ? x->atoms[at] : predefined[at - COMPOSITOR_XWM_ATOMS].atom;

                if (atom && xwm_atom_index(x, atom) < 0)
                        xwm_atom_add(x, atom, (const unsigned char *)name, (unsigned long)string_length((string_address)name), 1);
        }
}

static int compositor_xwm_which(struct compositor_xwm *x, unsigned int selection)
{
        for (int which = 0; selection && which < 3; which++)
                if (x->sel[which].atom == selection)
                        return which;
        return -1;
}

static void compositor_xwm_own(struct compositor_xwm *x, int which, unsigned int owner)
{
        unsigned char req[16];
        compositor_xwm_queue(x, req, xwm_enc_set_selection_owner(req, sizeof req, owner, x->sel[which].atom, XWM_CURRENT_TIME));
        x->sel[which].owned = owner != 0;
}

static void compositor_xwm_notify(struct compositor_xwm *x, unsigned int time, unsigned int requestor, unsigned int selection, unsigned int target, unsigned int property)
{
        unsigned char event[XWM_EVENT_SIZE];
        if (xwm_enc_selection_notify(event, sizeof event, time, requestor, selection, target, property) == XWM_EVENT_SIZE)
                compositor_xwm_send_event(x, requestor, 0, 0, event, XWM_EVENT_SIZE);
}

static int compositor_xwm_pipe(int fds[2], int ours)
{
        if (compositor_call(syscall(pipe2), (positive)fds, COMPOSITOR_XWM_O_CLOEXEC, 0, 0, 0, 0) < 0)
                return -1;
        compositor_call(syscall(fcntl), (positive)fds[ours], COMPOSITOR_XWM_F_SETFL, COMPOSITOR_XWM_O_NONBLOCK, 0, 0, 0);
        compositor_call(syscall(fcntl), (positive)fds[0], COMPOSITOR_XWM_F_SETPIPE_SZ, COMPOSITOR_XWM_PIPE, 0, 0, 0);
        return 0;
}

static long compositor_xwm_pipe_room(int fd)
{
        int used = 0;
        long size = compositor_call(syscall(fcntl), (positive)fd, COMPOSITOR_XWM_F_GETPIPE_SZ, 0, 0, 0, 0);

        if (size <= 0 || compositor_call(syscall(ioctl), (positive)fd, COMPOSITOR_XWM_FIONREAD, (positive)&used, 0, 0, 0) < 0)
                return 0;
        return size - used - COMPOSITOR_XWM_PIPE_SLACK > 0 ? size - used - COMPOSITOR_XWM_PIPE_SLACK : 0;
}

static const char *xwm_mime_next(const char *list, long bytes, long *at, unsigned long *len)
{
        long start = *at, n = 0;

        if (!list || start >= bytes)
                return 0;
        while (start + n < bytes && list[start + n])
                n++;
        *at = start + n + 1, *len = (unsigned long)n;
        return list + start;
}

static int compositor_xwm_listed(const unsigned char *list, long bytes, const unsigned char *name, unsigned long length)
{
        const char *entry;
        unsigned long n;

        for (long at = 0; (entry = xwm_mime_next((const char *)list, bytes, &at, &n));)
                if (n == length && !memory_compare(entry, name, (positive)length))
                        return 1;
        return 0;
}

static int compositor_xwm_copy_row(struct compositor_xwm *x, unsigned int owner)
{
        positive now = clock_monotonic_nanoseconds();
        int best = -1;

        if (!compositor.table || !compositor.table->clip_on)
                return -1;
        for (long at = 0; at < x->window_table.count; at++)
        {
                struct compositor_xwm_window *w = &x->window[at];
                if (w->id && w->row >= 0 && w->row < COMPOSITOR_ROWS && compositor_xwm_same_client(x, w->id, owner) &&
                    now - compositor_clip.gesture[w->row] <= COMPOSITOR_CLIP_GESTURE_NS && (best < 0 || w->row == (int)compositor.focus))
                        best = w->row;
        }
        return best;
}

static int compositor_xwm_may_paste(struct compositor_xwm *x, unsigned int requestor)
{
        positive now = clock_monotonic_nanoseconds();

        if (compositor.focus < 0)
                return 0;
        for (long at = 0; at < x->window_table.count; at++)
                if (x->window[at].id && x->window[at].row >= 0 && compositor_xwm_same_client(x, x->window[at].id, requestor) &&
                    compositor_clip_may_access(x->window[at].row, now))
                        return 1;
        return 0;
}

/* ---- selection conversions requested by XWM (in) ---- */

static struct compositor_xwm_in *compositor_xwm_in_at(struct compositor_xwm *x, long at)
{
        return at >= 0 && at < x->in_table.count && x->ins[at].window ? &x->ins[at] : 0;
}

static long compositor_xwm_in_new(struct compositor_xwm *x, int which, unsigned int target, unsigned int time, int eager, int fd, int row)
{
        unsigned char req[48];
        unsigned int ev_mask = XWM_MASK_PROPERTY_CHANGE;
        struct compositor_xwm_in *in;
        long at;

        if (x->in_count >= COMPOSITOR_XWM_IN_MAX || (at = xwm_tbl_take(&x->in_table, sizeof(struct compositor_xwm_in))) < 0)
                return -1;
        x->in_count++;
        x->ins = (struct compositor_xwm_in *)x->in_table.at;
        in = &x->ins[at];
        memory_zero(in, sizeof *in);
        if (x->next_requestor < COMPOSITOR_XWM_FIRST_REQUESTOR || x->next_requestor > x->resource_mask)
                x->next_requestor = COMPOSITOR_XWM_FIRST_REQUESTOR;
        in->window = compositor_xwm_id(x, x->next_requestor++);
        in->which = (unsigned char)which;
        in->stage = target == x->atoms[COMPOSITOR_XWM_A_TARGETS] ? COMPOSITOR_XWM_IN_TARGETS : COMPOSITOR_XWM_IN_ASKED;
        in->eager = (unsigned char)eager;
        in->latin1 = target == COMPOSITOR_XWM_XA_STRING;
        in->selection = x->sel[which].atom, in->target = target, in->time = time;
        in->fd = fd, in->row = row, in->pump = -1, in->index = -1, in->next = -1;
        in->deadline = clock_monotonic_nanoseconds() + COMPOSITOR_XWM_TRANSFER_NS;
        in->targets.unit = sizeof(unsigned int);
        xwm_make_win(x, in->window, -1, XWM_CLASS_INPUT_ONLY, XWM_CW_EVENT_MASK, &ev_mask, 1);
        compositor_xwm_queue(x, req, xwm_enc_convert_selection(req, sizeof req, in->window, in->selection, target, x->atoms[COMPOSITOR_XWM_A_SELECTION], time));
        return at;
}

static void compositor_xwm_saved(struct compositor_xwm *x, unsigned int which, int ok);

static void compositor_xwm_copied(struct compositor_xwm *x, struct compositor_xwm_in *in)
{
        unsigned int which = in->which;
        if (!compositor.table || which > 1 || !in->count || in->index < 0 || !windows_clip_seal(in->fd, in->index, in->total, in->count))
                return;
        x->setting = which + 1;
        if (compositor_clip_set_memfd(which, in->fd, in->row))
                compositor_xwm_saved(x, which, 1);
        x->setting = 0;
}

static void compositor_xwm_in_free(struct compositor_xwm *x, long at)
{
        struct compositor_xwm_in *in = compositor_xwm_in_at(x, at);
        if (!in)
                return;
        if (in->eager)
                compositor_xwm_saved(x, in->which, 0);
        if (in->fd >= 0)
                system_close(in->fd);
        if (in->index >= 0)
                system_close(in->index);
        compositor_xwm_window_op(x, XWM_OP_DESTROY_WINDOW, in->window);
        wayland_table_drop(&in->targets);
        for (int which = 0; which < 3; which++)
                if (x->sel[which].reading == at)
                        x->sel[which].reading = -1;
        in->window = 0;
        if (x->in_count > 0)
                x->in_count--;
        wayland_table_give(&x->in_table, at);
}

static void compositor_xwm_in_next(struct compositor_xwm *x, long at)
{
        struct compositor_xwm_in *in = compositor_xwm_in_at(x, at);
        const unsigned int *atoms = (const unsigned int *)in->targets.at;
        unsigned int target = 0;
        unsigned char req[32];

        if (in->next < 0)
                in->next = 0, target = in->text;
        while (!target && atoms && in->next < in->targets.count)
        {
                unsigned long length = 0;
                unsigned int atom = atoms[in->next++];
                const unsigned char *name = compositor_xwm_atom_name(x, atom, &length);
                if (name && atom != in->text && xwm_target_kind(name, length) == XWM_TARGET_MIME && !windows_clip_alias((const char *)name, length))
                        target = atom;
        }
        if (!target)
        {
                compositor_xwm_copied(x, in);
                compositor_xwm_in_free(x, at);
                return;
        }
        in->target = target, in->latin1 = target == COMPOSITOR_XWM_XA_STRING, in->stage = COMPOSITOR_XWM_IN_ASKED;
        in->incr = 0, in->offset = 0, in->done = 0;
        in->deadline = clock_monotonic_nanoseconds() + COMPOSITOR_XWM_TRANSFER_NS;
        compositor_xwm_queue(x, req, xwm_enc_convert_selection(req, sizeof req, in->window, in->selection, target, x->atoms[COMPOSITOR_XWM_A_SELECTION], in->time));
}

static void compositor_xwm_in_end(struct compositor_xwm *x, long at, int ok)
{
        struct compositor_xwm_in *in = compositor_xwm_in_at(x, at);
        unsigned long length = 0;
        const unsigned char *name;

        if (!in)
                return;
        if (!in->eager || in->stage == COMPOSITOR_XWM_IN_TARGETS)
        {
                compositor_xwm_in_free(x, at);
                return;
        }
        if (in->stage == COMPOSITOR_XWM_IN_NAMING)
        {
                in->index = ok ? (int)compositor_call(syscall(memfd_create), (positive)"moonwater-x-index", 1, 0, 0, 0, 0) : -1;
                if (in->index < 0)
                        compositor_xwm_in_free(x, at);
                else
                        compositor_xwm_in_next(x, at);
                return;
        }
        name = in->target == in->text ? (const unsigned char *)WINDOWS_CLIP_TEXT : compositor_xwm_atom_name(x, in->target, &length);
        length = in->target == in->text ? sizeof(WINDOWS_CLIP_TEXT) - 1 : length;
        if (ok && in->target == in->text)
        {
                long mapped = in->done ? compositor_call(syscall(mmap), 0, (positive)(in->total + in->done), 1, 1, (positive)in->fd, 0) : -22;
                ok = !(mapped < 0 && mapped > -4096) && windows_clip_text((const unsigned char *)mapped + in->total, in->done);
                if (!(mapped < 0 && mapped > -4096))
                        compositor_call(syscall(munmap), (positive)mapped, (positive)(in->total + in->done), 0, 0, 0, 0);
        }
        if (ok && name && windows_clip_record(in->index, in->total, in->done, (const char *)name, length))
                in->total += in->done, in->count++;
        else
        {
                compositor_call(syscall(ftruncate), (positive)in->fd, (positive)in->total, 0, 0, 0, 0);
                compositor_call(syscall(lseek), (positive)in->fd, (positive)in->total, 0, 0, 0, 0);
        }
        compositor_xwm_in_next(x, at);
}

static void compositor_xwm_in_ask(struct compositor_xwm *x, long at)
{
        struct compositor_xwm_in *in = compositor_xwm_in_at(x, at);
        unsigned char req[32];
        long units = COMPOSITOR_XWM_PIECE_UNITS;

        if (!in || in->asking)
                return;
        if (!in->eager)
        {
                long room = compositor_xwm_pipe_room(in->fd);
                if ((in->latin1 ? room / 2 : room) / XWM_UNIT < 1024 && in->pump >= 0)
                        compositor_wl_pump_nudge(in->pump), room = compositor_xwm_pipe_room(in->fd);
                units = (in->latin1 ? room / 2 : room) / XWM_UNIT;
                if (units > COMPOSITOR_XWM_PIECE_UNITS)
                        units = COMPOSITOR_XWM_PIECE_UNITS;
                if (units < 1024)
                {
                        in->ready = 1;
                        return;
                }
        }
        in->ready = 0;
        if (compositor_xwm_wait(x, req, xwm_enc_get_property(req, sizeof req, 1, in->window, x->atoms[COMPOSITOR_XWM_A_SELECTION], 0, (unsigned int)in->offset,
                                                             (unsigned int)units),
                                in->window, COMPOSITOR_XWM_PROP_SEL, (unsigned int)at) == 0)
                in->asking = 1;
}

static int compositor_xwm_in_write(struct compositor_xwm_in *in, const unsigned char *bytes, unsigned long count)
{
        unsigned char wide[8192];
        for (unsigned long at = 0; at < count;)
        {
                unsigned long take = in->latin1 && count - at > 4096 ? 4096 : count - at;
                unsigned long put = in->latin1 ? xwm_latin1_utf8(bytes + at, take, wide) : take;
                const unsigned char *from = in->latin1 ? wide : bytes + at;
                /* A copy's memfd is no bigger than the clipboard's largest copy: an INCR that never ends (its deadline reset by each
                   piece) is ended here, where the native store would refuse it only after the memfd had grown (x11 B6). */
                if (in->total + in->done + put > WINDOWS_CLIP_MOST)
                        return 0;
                for (unsigned long done = 0; done < put;)
                {
                        long wrote = compositor_call(syscall(write), (positive)in->fd, (positive)(from + done), (positive)(put - done), 0, 0, 0);
                        if (wrote <= 0)
                                return 0;
                        done += (unsigned long)wrote;
                }
                in->done += put, at += take;
        }
        in->deadline = clock_monotonic_nanoseconds() + COMPOSITOR_XWM_TRANSFER_NS;
        return 1;
}

static unsigned int compositor_xwm_best_text(struct compositor_xwm *x, const unsigned int *atoms, long count)
{
        unsigned int best = 0;
        int best_rank = 0;
        for (long k = 0; k < count; k++)
        {
                unsigned long len = 0;
                const unsigned char *name = compositor_xwm_atom_name(x, atoms[k], &len);
                int rank = name ? windows_clip_text_rank((const char *)name, len) : 0;
                if (rank && (!best_rank || rank < best_rank))
                        best_rank = rank, best = atoms[k];
        }
        return best;
}

static void compositor_xwm_in_reply(struct compositor_xwm *x, const unsigned char *msg, long size, const struct compositor_xwm_pending *p)
{
        long at = (long)p->slot;
        struct compositor_xwm_in *in = compositor_xwm_in_at(x, at);
        struct xwm_property_reply r;
        unsigned long incr = 0;

        if (!in || in->window != p->window)
                return;
        in->asking = 0;
        if (xwm_parse_property_reply(msg, (unsigned long)size, &r) != XWM_OK)
        {
                x->broken = 1;
                return;
        }
        if (p->what == COMPOSITOR_XWM_PROP_SEL_SYNC)
        {
                in->text = compositor_xwm_best_text(x, (const unsigned int *)in->targets.at, in->targets.count);
                compositor_xwm_in_end(x, at, 1);
                return;
        }
        if (in->stage == COMPOSITOR_XWM_IN_TARGETS)
        {
                wayland_tables_host(&compositor_wl_host);
                for (unsigned long k = 0; r.format == XWM_FORMAT_32 && k + 4 <= r.value_len && in->targets.count < COMPOSITOR_XWM_TARGETS_MOST; k += 4)
                {
                        unsigned int atom = xwm_get32(r.value + k);
                        if (atom && wayland_table_reach(&in->targets, in->targets.count + 1))
                                ((unsigned int *)in->targets.at)[in->targets.count - 1] = atom, compositor_xwm_name_atom(x, atom);
                }
                in->offset = 0, in->stage = COMPOSITOR_XWM_IN_NAMING;
                if (xwm_wait_prop(x, in->window, x->atoms[COMPOSITOR_XWM_A_SELECTION], 0, 0, COMPOSITOR_XWM_PROP_SEL_SYNC, (unsigned int)at) != 0)
                        compositor_xwm_in_end(x, at, 0);
                return;
        }
        if (in->stage == COMPOSITOR_XWM_IN_ASKED && !in->offset && xwm_property_incr(&r, x->atoms[COMPOSITOR_XWM_A_INCR], &incr))
        {
                in->incr = 1, in->stage = COMPOSITOR_XWM_IN_PIECES;
                in->deadline = clock_monotonic_nanoseconds() + COMPOSITOR_XWM_TRANSFER_NS;
                return;
        }
        if (in->incr && !in->offset && !r.value_len && !r.bytes_after)
                compositor_xwm_in_end(x, at, 1);
        else if (r.value_len && !compositor_xwm_in_write(in, r.value, r.value_len))
                compositor_xwm_in_end(x, at, 0);
        else if (r.bytes_after)
        {
                if (r.value_len < XWM_UNIT || (r.value_len % XWM_UNIT) != 0 ||
                    in->offset > 0xfffffffful - r.value_len / XWM_UNIT)
                        compositor_xwm_in_end(x, at, 0);
                else
                        in->offset += r.value_len / XWM_UNIT, compositor_xwm_in_ask(x, at);
        }
        else
        {
                in->offset = 0;
                if (!in->incr)
                        compositor_xwm_in_end(x, at, 1);
                else
                        in->stage = COMPOSITOR_XWM_IN_PIECES;
        }
}

static int compositor_xwm_in_notify(struct compositor_xwm *x, const struct xwm_event *e)
{
        for (long at = 0; at < x->in_table.count; at++)
                if (x->ins[at].window && x->ins[at].window == e->requestor)
                {
                        if (!e->property)
                                compositor_xwm_in_end(x, at, 0);
                        else if (x->ins[at].stage == COMPOSITOR_XWM_IN_TARGETS || x->ins[at].stage == COMPOSITOR_XWM_IN_ASKED)
                                x->ins[at].offset = 0, compositor_xwm_in_ask(x, at);
                        return 1;
                }
        return 0;
}

static int compositor_xwm_in_property(struct compositor_xwm *x, const struct xwm_event *e)
{
        for (long at = 0; at < x->in_table.count; at++)
                if (x->ins[at].window && x->ins[at].window == e->window)
                {
                        if (e->atom == x->atoms[COMPOSITOR_XWM_A_SELECTION] && e->state == 0 && x->ins[at].stage == COMPOSITOR_XWM_IN_PIECES)
                                compositor_xwm_in_ask(x, at);
                        return 1;
                }
        return 0;
}

static unsigned int compositor_xwm_drag_target(struct compositor_xwm *x, const char *mime, unsigned long length, unsigned int *time);

static void compositor_xwm_receive(long client, long which, const char *mime, long fd)
{
        struct compositor_xwm *x = compositor_xwm_first_ready();
        unsigned long length = mime ? (unsigned long)string_length((string_address)mime) : 0;
        unsigned int target = 0, time = XWM_CURRENT_TIME;
        int fds[2] = {-1, -1};
        long pump, at;

        (void)client;
        if (which < 0 || which > 2)
        {
                system_close((int)fd);
                return;
        }
        if (which != 2)
                for (int s = 0; s < COMPOSITOR_XWM_SLOTS; s++)
                        if (compositor_xwm_slots[s].fd >= 0 && compositor_xwm_slots[s].ready && !compositor_xwm_slots[s].sel[which].owned &&
                            compositor_xwm_slots[s].sel[which].x_owner)
                        {
                                x = &compositor_xwm_slots[s];
                                break;
                        }
        if (which == 2)
        {
                for (int s = 0; s < COMPOSITOR_XWM_SLOTS; s++)
                        if (compositor_xwm_slots[s].fd >= 0 && compositor_xwm_slots[s].ready &&
                            compositor_xwm_slots[s].xdrag.entered && compositor_xwm_slots[s].xdrag.source)
                        {
                                x = &compositor_xwm_slots[s];
                                break;
                        }
                if (x->fd >= 0 && x->ready)
                        target = compositor_xwm_drag_target(x, mime, length, &time);
        }
        if (!target || compositor_xwm_pipe(fds, 1) != 0)
        {
                system_close((int)fd);
                return;
        }
        if ((pump = compositor_wl_pump(fds[0], (int)fd)) < 0 || (at = compositor_xwm_in_new(x, (int)which, target, time, 0, fds[1], -1)) < 0)
                system_close(fds[1]);
        else
                x->ins[at].pump = pump;
}

/* ---- selection conversions answered by XWM (out) ---- */

static struct compositor_xwm_out *compositor_xwm_out_at(struct compositor_xwm *x, long at)
{
        return at >= 0 && at < x->out_table.count && x->outs[at].requestor ? &x->outs[at] : 0;
}

static void compositor_xwm_out_free(struct compositor_xwm *x, long at)
{
        struct compositor_xwm_out *o = compositor_xwm_out_at(x, at);
        if (!o)
                return;
        if (o->fd >= 0)
                system_close(o->fd);
        o->requestor = 0;
        if (x->out_count > 0)
                x->out_count--;
        wayland_table_give(&x->out_table, at);
}

static unsigned char compositor_xwm_piece[XWM_INCR_CHUNK];
static unsigned char compositor_xwm_latin1_buf[XWM_INCR_CHUNK + 1];

static unsigned long compositor_xwm_out_read(struct compositor_xwm_out *o, unsigned long count)
{
        unsigned long got = 0;
        while (got < count)
        {
                long n = compositor_call(syscall(pread64), (positive)o->fd, (positive)(compositor_xwm_piece + got), (positive)(count - got),
                                         (positive)(o->base + o->offset + got), 0, 0);
                if (n <= 0)
                        break;
                got += (unsigned long)n;
        }
        return got;
}

static void compositor_xwm_out_step(struct compositor_xwm *x, long at)
{
        struct compositor_xwm_out *o = compositor_xwm_out_at(x, at);
        long room = COMPOSITOR_XWM_OUT - COMPOSITOR_XWM_ROOM - x->out_len;

        if (!o || o->stage == COMPOSITOR_XWM_OUT_WAIT || o->stage == COMPOSITOR_XWM_OUT_MULTIPLE || room <= 256)
                return;
        if (o->stage == COMPOSITOR_XWM_OUT_START && o->from == COMPOSITOR_XWM_FROM_FILE && !xwm_incr_needed(o->size))
        {
                if ((unsigned long)room < 32 + o->size)
                        return;
                unsigned long got = compositor_xwm_out_read(o, o->size);
                const unsigned char *src = o->latin1 ? compositor_xwm_latin1_buf : compositor_xwm_piece;
                unsigned long put = o->latin1 ? xwm_utf8_latin1(&o->cut, compositor_xwm_piece, got, compositor_xwm_latin1_buf) : got;
                if (got == o->size)
                        compositor_xwm_put(x, o->requestor, o->property, o->type, XWM_FORMAT_8, src, put);
                if (!o->quiet)
                        compositor_xwm_notify(x, o->time, o->requestor, o->selection, o->target, got == o->size ? o->property : 0);
                compositor_xwm_out_free(x, at);
                return;
        }
        if (o->stage == COMPOSITOR_XWM_OUT_START)
        {
                unsigned char req[32];
                compositor_xwm_select(x, o->requestor, XWM_MASK_PROPERTY_CHANGE);
                compositor_xwm_queue(x, req, xwm_enc_incr(req, sizeof req, o->requestor, o->property, x->atoms[COMPOSITOR_XWM_A_INCR],
                                                          o->size == XWM_INCR_SIZE_UNKNOWN ? XWM_INCR_CHUNK : o->size));
                if (!o->quiet)
                        compositor_xwm_notify(x, o->time, o->requestor, o->selection, o->target, o->property);
                o->stage = COMPOSITOR_XWM_OUT_WAIT, o->deadline = clock_monotonic_nanoseconds() + COMPOSITOR_XWM_TRANSFER_NS;
                return;
        }
        for (;;)
        {
                unsigned long left = o->from == COMPOSITOR_XWM_FROM_FILE ? o->size - o->offset : XWM_INCR_CHUNK;
                unsigned long most = xwm_incr_piece(left, (unsigned long)room), got, put;
                const unsigned char *src;
                if (o->latin1 && o->cut.need && most >= XWM_INCR_CHUNK)
                        most = XWM_INCR_CHUNK - 1;
                if (!most && (o->from == COMPOSITOR_XWM_FROM_PIPE || o->offset < o->size))
                        return;
                if (o->from == COMPOSITOR_XWM_FROM_FILE)
                        got = compositor_xwm_out_read(o, most);
                else
                {
                        long n = compositor_call(syscall(read), (positive)o->fd, (positive)compositor_xwm_piece, (positive)most, 0, 0, 0);
                        if (n == COMPOSITOR_XWM_EAGAIN && o->pump >= 0)
                                compositor_wl_pump_nudge(o->pump), n = compositor_call(syscall(read), (positive)o->fd, (positive)compositor_xwm_piece, (positive)most, 0, 0, 0);
                        if (n == COMPOSITOR_XWM_EAGAIN)
                                return;
                        got = n > 0 ? (unsigned long)n : 0;
                }
                o->offset += got;
                src = o->latin1 ? compositor_xwm_latin1_buf : compositor_xwm_piece;
                put = o->latin1 ? xwm_utf8_latin1(&o->cut, compositor_xwm_piece, got, compositor_xwm_latin1_buf) : got;
                if (got && !put)
                        continue;
                compositor_xwm_put(x, o->requestor, o->property, o->type, XWM_FORMAT_8, src, put);
                if (!got)
                        compositor_xwm_out_free(x, at);
                else
                        o->stage = COMPOSITOR_XWM_OUT_WAIT, o->deadline = clock_monotonic_nanoseconds() + COMPOSITOR_XWM_TRANSFER_NS;
                return;
        }
}

static void compositor_xwm_out_property(struct compositor_xwm *x, const struct xwm_event *e)
{
        if (e->state != 1)
                return;
        for (long at = 0; at < x->out_table.count; at++)
                if (x->outs[at].requestor == e->window && x->outs[at].property == e->atom && x->outs[at].stage == COMPOSITOR_XWM_OUT_WAIT)
                        x->outs[at].stage = COMPOSITOR_XWM_OUT_NEXT, compositor_xwm_out_step(x, at);
}

static long compositor_xwm_drag_types(const char **types);
static int compositor_xwm_drag_hand(int *hx, int *hy);

static long compositor_xwm_side_types(int which, const char **types, int *text)
{
        long bytes = which == 2 ? compositor_xwm_drag_types(types) : 0;
        const char *entry;
        unsigned long n;
        *text = 0;
        for (long at = 0; !*text && (entry = xwm_mime_next(*types, bytes, &at, &n));)
        {
                int k = xwm_target_kind((const unsigned char *)entry, n);
                *text = k == XWM_TARGET_UTF8 || k == XWM_TARGET_LATIN1;
        }
        return bytes;
}

static unsigned int compositor_xwm_atom_of(struct compositor_xwm *x, const char *name, unsigned long length)
{
        long found = compositor_xwm_atom_named(x, (const unsigned char *)name, length);
        return found >= 0 && x->atom[found].known ? x->atom[found].atom : 0;
}

static unsigned int xwm_push_text_atoms(const struct compositor_xwm *x, unsigned int *out, unsigned int count, unsigned int cap)
{
        unsigned int list[5] = {x->atoms[COMPOSITOR_XWM_A_UTF8], x->atoms[COMPOSITOR_XWM_A_MIME_UTF8],
                                x->atoms[COMPOSITOR_XWM_A_MIME_TEXT], COMPOSITOR_XWM_XA_STRING, x->atoms[COMPOSITOR_XWM_A_TEXT]};
        for (unsigned int k = 0; k < 5 && count < cap; k++)
                out[count++] = list[k];
        return count;
}

static void xwm_put_timestamp(struct compositor_xwm *x, unsigned int requestor, unsigned int property, int which)
{
        compositor_xwm_put32(x, requestor, property, COMPOSITOR_XWM_XA_INTEGER, &x->sel[which].owned_time, 1);
}

static void compositor_xwm_targets(struct compositor_xwm *x, const struct xwm_event *e, int which, unsigned int property, int quiet)
{
        unsigned int atoms[COMPOSITOR_XWM_LIST_MAX], count = 0, atom;
        unsigned char data[COMPOSITOR_XWM_LIST_MAX * 4];
        const char *types = 0, *name, *entry;
        unsigned long length, n;
        int text = 0;
        long bytes = compositor_xwm_side_types(which, &types, &text);

        atoms[count++] = x->atoms[COMPOSITOR_XWM_A_TARGETS];
        atoms[count++] = x->atoms[COMPOSITOR_XWM_A_TIMESTAMP];
        atoms[count++] = x->atoms[COMPOSITOR_XWM_A_MULTIPLE];
        for (unsigned long nth = 0; which < 2 && count < sizeof atoms / sizeof atoms[0] &&
                                    (name = windows_clip_offered(compositor_clip.map[which], compositor.table->clip_length[which], nth, 1, &length));
             nth++)
                if ((atom = windows_clip_same(name, length, "STRING") ? COMPOSITOR_XWM_XA_STRING : compositor_xwm_atom_of(x, name, length)))
                        atoms[count++] = atom;
        if (text)
                count = xwm_push_text_atoms(x, atoms, count, (unsigned int)(sizeof atoms / sizeof atoms[0]));
        for (long at = 0; count < sizeof atoms / sizeof atoms[0] && (entry = xwm_mime_next(types, bytes, &at, &n));)
                if (xwm_target_kind((const unsigned char *)entry, n) == XWM_TARGET_MIME && (atom = compositor_xwm_atom_of(x, entry, n)))
                        atoms[count++] = atom;
        for (unsigned int i = 0; i < count; i++)
                xwm_put32(data + 4 * i, atoms[i]);
        compositor_xwm_put(x, e->requestor, property, COMPOSITOR_XWM_ATOM_TYPE, XWM_FORMAT_32, data, count * 4);
        if (!quiet)
                compositor_xwm_notify(x, e->time, e->requestor, e->selection, e->target, property);
}

static long compositor_xwm_side_mime(int which, int kind, const unsigned char *name, unsigned long length, const char **mime)
{
        const char *types = 0, *entry;
        unsigned long n;
        int text = 0;
        long bytes = compositor_xwm_side_types(which, &types, &text);
        for (long at = 0; (entry = xwm_mime_next(types, bytes, &at, &n));)
        {
                int is = xwm_target_kind((const unsigned char *)entry, n);
                if ((kind == XWM_TARGET_MIME && n == length && !memory_compare(entry, name, (positive)length)) ||
                    (kind != XWM_TARGET_MIME && (is == XWM_TARGET_UTF8 || is == XWM_TARGET_LATIN1)))
                        return *mime = entry, (long)n;
        }
        return 0;
}

static int compositor_xwm_drag_send(const char *mime, long fd);

static void compositor_xwm_out_gone(struct compositor_xwm *x, unsigned int requestor, unsigned int property)
{
        for (long at = 0; requestor && at < x->out_table.count; at++)
                if (x->outs[at].requestor == requestor && (!property || x->outs[at].property == property))
                        compositor_xwm_out_free(x, at);
}

static long compositor_xwm_out_begin(struct compositor_xwm *x, const struct xwm_event *e, int which, unsigned int property, int quiet)
{
        unsigned long length = 0;
        const unsigned char *name = compositor_xwm_atom_name(x, e->target, &length);
        int kind = name ? xwm_target_kind(name, length) : XWM_TARGET_NONE, fd = -1, from;
        const char *mime = 0;
        struct windows_clip_type type = {0, 0, 0, 0};
        struct compositor_xwm_out *o;
        long at, pump_at = -1;

        compositor_xwm_out_gone(x, e->requestor, property);
        if (!name || x->out_count >= COMPOSITOR_XWM_OUT_MAX || (which < 2 && !compositor_xwm_may_paste(x, e->requestor)))
                return -1;
        if (which < 2)
        {
                if (compositor_clip.fd[which] < 0 ||
                    !windows_clip_find(compositor_clip.map[which], compositor.table->clip_length[which], (const char *)name, length, &type))
                        return -1;
                if (!windows_clip_same(type.name, type.name_length, WINDOWS_CLIP_TEXT))
                        kind = XWM_TARGET_MIME;
                from = COMPOSITOR_XWM_FROM_FILE;
                if ((fd = (int)compositor_call(syscall(fcntl), (positive)compositor_clip.fd[which], COMPOSITOR_F_DUPFD_CLOEXEC, 0, 0, 0, 0)) < 0)
                        return -1;
        }
        else
        {
                int source[2], ours[2];
                if (kind == XWM_TARGET_NONE || !compositor_xwm_side_mime(which, kind, name, length, &mime) || compositor_xwm_pipe(source, 0) != 0)
                        return -1;
                if (compositor_xwm_pipe(ours, 0) != 0)
                        return system_close(source[0]), system_close(source[1]), -1;
                if (!compositor_xwm_drag_send(mime, source[1]))
                        return system_close(source[0]), system_close(ours[0]), system_close(ours[1]), -1;
                if ((pump_at = compositor_wl_pump(source[0], ours[1])) < 0)
                        return system_close(ours[0]), -1;
                from = COMPOSITOR_XWM_FROM_PIPE, fd = ours[0];
        }
        if ((at = xwm_tbl_take(&x->out_table, sizeof(struct compositor_xwm_out))) < 0)
                return system_close(fd), -1;
        x->out_count++;
        x->outs = (struct compositor_xwm_out *)x->out_table.at;
        o = &x->outs[at];
        memory_zero(o, sizeof *o);
        o->requestor = e->requestor, o->property = property, o->selection = e->selection, o->target = e->target, o->time = e->time;
        o->which = (unsigned char)which, o->from = (unsigned char)from, o->latin1 = kind == XWM_TARGET_LATIN1;
        o->type = kind == XWM_TARGET_LATIN1 ? COMPOSITOR_XWM_XA_STRING : e->target == x->atoms[COMPOSITOR_XWM_A_TEXT] ? x->atoms[COMPOSITOR_XWM_A_UTF8] : e->target;
        o->fd = fd, o->pump = pump_at, o->quiet = (unsigned char)quiet, o->base = type.offset;
        o->size = from == COMPOSITOR_XWM_FROM_FILE ? type.length : XWM_INCR_SIZE_UNKNOWN;
        o->stage = COMPOSITOR_XWM_OUT_START, o->deadline = clock_monotonic_nanoseconds() + COMPOSITOR_XWM_TRANSFER_NS;
        compositor_xwm_out_step(x, at);
        return at;
}

static void compositor_xwm_multiple(struct compositor_xwm *x, const struct xwm_event *e, int which, unsigned int property)
{
        struct compositor_xwm_out *o;
        long at;

        if (x->out_count >= COMPOSITOR_XWM_OUT_MAX || (at = xwm_tbl_take(&x->out_table, sizeof(struct compositor_xwm_out))) < 0)
        {
                compositor_xwm_notify(x, e->time, e->requestor, e->selection, e->target, 0);
                return;
        }
        x->out_count++;
        x->outs = (struct compositor_xwm_out *)x->out_table.at;
        o = &x->outs[at];
        memory_zero(o, sizeof *o);
        o->requestor = e->requestor, o->property = property, o->selection = e->selection, o->target = e->target, o->time = e->time;
        o->which = (unsigned char)which, o->stage = COMPOSITOR_XWM_OUT_MULTIPLE, o->fd = -1, o->pump = -1;
        o->deadline = clock_monotonic_nanoseconds() + COMPOSITOR_XWM_TRANSFER_NS;
        if (xwm_wait_prop(x, e->requestor, property, 0, COMPOSITOR_XWM_PIECE_UNITS, COMPOSITOR_XWM_PROP_MULTIPLE, (unsigned int)at) != 0)
                compositor_xwm_notify(x, e->time, e->requestor, e->selection, e->target, 0), compositor_xwm_out_free(x, at);
}

static void compositor_xwm_multiple_reply(struct compositor_xwm *x, const unsigned char *msg, long size, const struct compositor_xwm_pending *p)
{
        struct compositor_xwm_out *o = compositor_xwm_out_at(x, (long)p->slot), multiple;
        struct xwm_property_reply r;
        unsigned char pairs[COMPOSITOR_XWM_LIST_MAX * 8];
        unsigned long count;

        if (!o || o->requestor != p->window || o->stage != COMPOSITOR_XWM_OUT_MULTIPLE)
                return;
        multiple = *o;
        compositor_xwm_out_free(x, (long)p->slot);
        if (!msg || xwm_parse_property_reply(msg, (unsigned long)size, &r) != XWM_OK || r.format != XWM_FORMAT_32 || r.value_len > sizeof pairs)
        {
                compositor_xwm_notify(x, multiple.time, multiple.requestor, multiple.selection, multiple.target, 0);
                return;
        }
        count = r.value_len / 8;
        memory_copy(pairs, r.value, (positive)(count * 8));
        for (unsigned long k = 0; k < count; k++)
        {
                struct xwm_event sub;
                long at = -1;
                int ok = 1;

                memory_zero(&sub, sizeof sub);
                sub.requestor = multiple.requestor, sub.selection = multiple.selection, sub.time = multiple.time;
                sub.target = xwm_get32(pairs + 8 * k), sub.property = xwm_get32(pairs + 8 * k + 4);
                if (!sub.target || !sub.property || sub.target == x->atoms[COMPOSITOR_XWM_A_MULTIPLE])
                        ok = 0;
                else if (sub.target == x->atoms[COMPOSITOR_XWM_A_TARGETS])
                        compositor_xwm_targets(x, &sub, multiple.which, sub.property, 1);
                else if (sub.target == x->atoms[COMPOSITOR_XWM_A_TIMESTAMP])
                        xwm_put_timestamp(x, sub.requestor, sub.property, multiple.which);
                else if ((at = compositor_xwm_out_begin(x, &sub, multiple.which, sub.property, 1)) < 0)
                        ok = 0;
                else if (compositor_xwm_out_at(x, at) && x->outs[at].stage == COMPOSITOR_XWM_OUT_START)
                {
                        compositor_xwm_out_free(x, at);
                        ok = 0;
                }
                if (!ok)
                        xwm_put32(pairs + 8 * k, 0);
        }
        compositor_xwm_put(x, multiple.requestor, multiple.property, x->atoms[COMPOSITOR_XWM_A_ATOM_PAIR], XWM_FORMAT_32, pairs, count * 8);
        compositor_xwm_notify(x, multiple.time, multiple.requestor, multiple.selection, multiple.target, multiple.property);
}

static void compositor_xwm_save(struct compositor_xwm *x, const struct xwm_event *e, unsigned int property)
{
        int row = compositor.table ? compositor.table->clip_owner[0] : -1;
        struct compositor_xwm_window *ow;

        if (e->target == x->atoms[COMPOSITOR_XWM_A_TARGETS])
        {
                unsigned int t[2] = {x->atoms[COMPOSITOR_XWM_A_TARGETS], x->atoms[COMPOSITOR_XWM_A_SAVE_TARGETS]};
                compositor_xwm_put32(x, e->requestor, property, COMPOSITOR_XWM_ATOM_TYPE, t, 2);
                compositor_xwm_notify(x, e->time, e->requestor, e->selection, e->target, property);
                return;
        }
        if (e->target != x->atoms[COMPOSITOR_XWM_A_SAVE_TARGETS])
        {
                compositor_xwm_notify(x, e->time, e->requestor, e->selection, e->target, 0);
                return;
        }
        if (x->save.requestor)
                compositor_xwm_saved(x, 0, 0);
        if (x->sel[0].reading >= 0 && compositor_xwm_same_client(x, x->sel[0].x_owner, e->requestor))
        {
                x->save.requestor = e->requestor, x->save.property = property, x->save.time = e->time;
                return;
        }
        ow = xwm_row_lookup(x, row, 0);
        compositor_xwm_notify(x, e->time, e->requestor, e->selection, e->target,
                              ow && compositor_xwm_same_client(x, ow->id, e->requestor) && compositor_clip.fd[0] >= 0 ? property : 0);
}

static void compositor_xwm_saved(struct compositor_xwm *x, unsigned int which, int ok)
{
        if (which != 0 || !x->save.requestor)
                return;
        compositor_xwm_notify(x, x->save.time, x->save.requestor, x->atoms[COMPOSITOR_XWM_A_MANAGER], x->atoms[COMPOSITOR_XWM_A_SAVE_TARGETS],
                              ok ? x->save.property : 0);
        x->save.requestor = 0;
}

static void compositor_xwm_selection_request(struct compositor_xwm *x, const struct xwm_event *e)
{
        int which = compositor_xwm_which(x, e->selection);
        unsigned int property = e->property ? e->property : e->target;

        if (e->owner != compositor_xwm_id(x, COMPOSITOR_XWM_SEL_WINDOW) || !e->requestor)
                return;
        if (e->selection && e->selection == x->atoms[COMPOSITOR_XWM_A_MANAGER] && property)
                compositor_xwm_save(x, e, property);
        else if (which < 0 || (which < 2 && !x->sel[which].owned) || (which == 2 && !x->wdrag.live) || !property)
                compositor_xwm_notify(x, e->time, e->requestor, e->selection, e->target, 0);
        else if (e->target == x->atoms[COMPOSITOR_XWM_A_TARGETS])
                compositor_xwm_targets(x, e, which, property, 0);
        else if (e->target == x->atoms[COMPOSITOR_XWM_A_TIMESTAMP])
                xwm_put_timestamp(x, e->requestor, property, which), compositor_xwm_notify(x, e->time, e->requestor, e->selection, e->target, property);
        else if (e->target == x->atoms[COMPOSITOR_XWM_A_MULTIPLE] && which < 2)
                compositor_xwm_multiple(x, e, which, property);
        else if (compositor_xwm_out_begin(x, e, which, property, 0) < 0)
                compositor_xwm_notify(x, e->time, e->requestor, e->selection, e->target, 0);
}

static void compositor_xwm_xdnd_owner(struct compositor_xwm *x, unsigned int owner);

static void compositor_xwm_owner(struct compositor_xwm *x, const struct xwm_event *e)
{
        int which = compositor_xwm_which(x, e->selection), row;
        unsigned int owner = e->detail == 0 ? e->owner : 0;
        long fd, at;

        if (which < 0)
                return;
        if (owner && owner == compositor_xwm_id(x, COMPOSITOR_XWM_SEL_WINDOW))
        {
                x->sel[which].owned_time = e->selection_time;
                return;
        }
        x->sel[which].x_owner = owner;
        if (which == 2)
        {
                compositor_xwm_xdnd_owner(x, owner);
                return;
        }
        x->sel[which].owned = 0;
        if (x->sel[which].reading >= 0)
                compositor_xwm_in_free(x, x->sel[which].reading);
        if (!owner)
        {
                x->sel[which].dirty = 1;
                return;
        }
        if ((row = compositor_xwm_copy_row(x, owner)) < 0 ||
            (fd = compositor_call(syscall(memfd_create), (positive)"moonwater-x-copy", 3, 0, 0, 0, 0)) < 0)
                return;
        if ((at = compositor_xwm_in_new(x, which, x->atoms[COMPOSITOR_XWM_A_TARGETS], e->selection_time, 1, (int)fd, row)) < 0)
                system_close((int)fd);
        else
                x->sel[which].reading = at;
}

static void compositor_xwm_selections_pass(struct compositor_xwm *x)
{
        positive now = clock_monotonic_nanoseconds();

        for (int which = 0; which < 2 && x->claimed && x->xfixes_event; which++)
        {
                if (!x->sel[which].dirty)
                        continue;
                x->sel[which].dirty = 0;
                if (compositor.table && compositor.table->clip_on && compositor_clip.fd[which] >= 0)
                {
                        const char *name;
                        unsigned long length;
                        for (unsigned long nth = 0; (name = windows_clip_offered(compositor_clip.map[which], compositor.table->clip_length[which], nth, 1, &length));
                             nth++)
                                if (!windows_clip_same(name, length, "STRING"))
                                        compositor_xwm_intern_name(x, (const unsigned char *)name, length);
                        compositor_xwm_own(x, which, compositor_xwm_id(x, COMPOSITOR_XWM_SEL_WINDOW));
                }
                else if (x->sel[which].owned)
                        compositor_xwm_own(x, which, 0);
        }
        for (long at = 0; at < x->in_table.count && x->fd >= 0 && !x->broken; at++)
        {
                struct compositor_xwm_in *in = compositor_xwm_in_at(x, at);
                if (in && now > in->deadline)
                        compositor_xwm_in_end(x, at, 0);
                else if (in && in->ready && !in->asking)
                        compositor_xwm_in_ask(x, at);
        }
        for (long at = 0; at < x->out_table.count && x->fd >= 0 && !x->broken; at++)
        {
                struct compositor_xwm_out *o = compositor_xwm_out_at(x, at);
                if (o && now > o->deadline)
                        compositor_xwm_out_free(x, at);
                else if (o && o->stage != COMPOSITOR_XWM_OUT_WAIT)
                        compositor_xwm_out_step(x, at);
        }
}

/* ---- bidirectional XDND v5 ---- */

static void compositor_xwm_message_via(struct compositor_xwm *x, unsigned int dest, unsigned int to, unsigned int type, const unsigned int *data)
{
        unsigned char event[XWM_EVENT_SIZE];
        if (to && dest && xwm_enc_client_message(event, sizeof event, to, type, data, XWM_DATA_LONGS) == XWM_EVENT_SIZE)
                compositor_xwm_send_event(x, dest, 0, 0, event, XWM_EVENT_SIZE);
}

static void compositor_xwm_message32(struct compositor_xwm *x, unsigned int to, unsigned int type, const unsigned int *data)
{
        compositor_xwm_message_via(x, to, to, type, data);
}

static void compositor_xwm_wdrag_say(struct compositor_xwm *x, unsigned int type, const unsigned int *data)
{
        compositor_xwm_message_via(x, x->wdrag.proxy ? x->wdrag.proxy : x->wdrag.target, x->wdrag.target, type, data);
}

static unsigned int xwm_dnd_to_wl(const struct compositor_xwm *x, unsigned int atom)
{
        return xwm_dnd_action(atom, x->atoms[COMPOSITOR_XWM_A_XDND_COPY], x->atoms[COMPOSITOR_XWM_A_XDND_MOVE], x->atoms[COMPOSITOR_XWM_A_XDND_ASK]);
}

static unsigned int xwm_dnd_to_x(const struct compositor_xwm *x, unsigned int action)
{
        return xwm_dnd_action_atom(action, x->atoms[COMPOSITOR_XWM_A_XDND_COPY], x->atoms[COMPOSITOR_XWM_A_XDND_MOVE], x->atoms[COMPOSITOR_XWM_A_XDND_ASK]);
}

static struct compositor_xwm_window *compositor_xwm_of_row(struct compositor_xwm *x, long row) { return xwm_row_lookup(x, (int)row, 0); }

static unsigned int compositor_xwm_dnd_atoms(struct compositor_xwm *x, unsigned int *atoms, unsigned int room, int intern)
{
        const char *types = 0, *entry;
        unsigned int count = 0;
        unsigned long n;
        int text = 0;
        long bytes = compositor_xwm_side_types(2, &types, &text);

        if (text)
                count = xwm_push_text_atoms(x, atoms, count, room);
        for (long at = 0; (entry = xwm_mime_next(types, bytes, &at, &n));)
                if (xwm_target_kind((const unsigned char *)entry, n) == XWM_TARGET_MIME)
                {
                        unsigned int atom = compositor_xwm_atom_of(x, entry, n);
                        if (!atom && intern)
                                compositor_xwm_intern_name(x, (const unsigned char *)entry, n);
                        if (atom && count < room)
                                atoms[count++] = atom;
                }
        return count;
}

static void compositor_xwm_wdrag_position(struct compositor_xwm *x)
{
        unsigned int data[5] = {compositor_xwm_id(x, COMPOSITOR_XWM_SEL_WINDOW), 0, x->wdrag.place, XWM_CURRENT_TIME, x->wdrag.action};
        compositor_xwm_wdrag_say(x, x->atoms[COMPOSITOR_XWM_A_XDND_POSITION], data);
        x->wdrag.waiting = 1, x->wdrag.moved = 0;
}

static void compositor_xwm_wdrag_leave(struct compositor_xwm *x)
{
        unsigned int data[5] = {compositor_xwm_id(x, COMPOSITOR_XWM_SEL_WINDOW), 0, 0, 0, 0};
        if (x->wdrag.entered)
                compositor_xwm_wdrag_say(x, x->atoms[COMPOSITOR_XWM_A_XDND_LEAVE], data);
        x->wdrag.target = x->wdrag.proxy = 0;
        x->wdrag.entered = x->wdrag.waiting = x->wdrag.moved = x->wdrag.aware_no = 0;
        x->wdrag.row = -1;
}

static void compositor_xwm_xdrag_reset(struct compositor_xwm *x)
{
        x->xdrag.source = x->xdrag.version = x->xdrag.action = x->xdrag.time = x->xdrag.told_action = x->xdrag.listed = x->xdrag.words = 0;
        x->xdrag.entered = x->xdrag.naming = x->xdrag.began = x->xdrag.dropped = x->xdrag.told_accept = x->xdrag.escaped = 0;
        x->xdrag.types.count = 0;
}

static void compositor_xwm_wdrag_actions(struct compositor_xwm *x, unsigned int actions);
static void compositor_xwm_xdrag_ask_actions(struct compositor_xwm *x, unsigned int source);

static long xwm_xdrag_slot(struct compositor_xwm *x, long what, long row, long px, long py)
{
        if (what == WAYLAND_XDRAG_END)
        {
                if (x->xdrag.began || x->xdrag.dropped)
                {
                        unsigned int done = py ? xwm_dnd_to_x(x, (unsigned int)py) : x->xdrag.action;
                        unsigned int data[5] = {compositor_xwm_id(x, COMPOSITOR_XWM_PROXY_WINDOW), px ? 1u : 0u, px ? done : 0, 0, 0};
                        if (x->xdrag.dropped)
                                compositor_xwm_message32(x, x->xdrag.source, x->atoms[COMPOSITOR_XWM_A_XDND_FINISHED], data), compositor_xwm_xdrag_reset(x);
                        else
                                x->xdrag.began = 0, x->xdrag.cancelled = 1;
                }
                if (x->wdrag.live)
                {
                        compositor_xwm_wdrag_leave(x);
                        x->wdrag.live = x->wdrag.dropped = 0;
                        if (x->fd >= 0 && x->ready)
                                compositor_xwm_own(x, 2, 0);
                }
                return 0;
        }
        if (x->fd < 0 || !x->ready || !x->claimed || !x->xfixes_event)
                return 0;
        if (what == WAYLAND_XDRAG_LEAVE)
                return compositor_xwm_wdrag_leave(x), 0;
        if (what == WAYLAND_XDRAG_DROP)
        {
                unsigned int data[5] = {compositor_xwm_id(x, COMPOSITOR_XWM_SEL_WINDOW), 0, XWM_CURRENT_TIME, 0, 0};
                if (!x->wdrag.entered)
                        return wayland_drag_x_finished(0), 0;
                compositor_xwm_wdrag_say(x, x->atoms[COMPOSITOR_XWM_A_XDND_DROP], data);
                return x->wdrag.dropped = 1, 0;
        }
        if (what == WAYLAND_XDRAG_OVER)
        {
                struct compositor_xwm_window *w = compositor_xwm_of_row(x, row);
                unsigned int atoms[64], actions = 0;
                const char *types = 0;
                if (!w)
                        return 0;
                if (!x->wdrag.live)
                {
                        x->wdrag.live = 1, x->wdrag.dropped = 0;
                        compositor_xwm_dnd_atoms(x, atoms, 64, 1);
                        compositor_xwm_own(x, 2, compositor_xwm_id(x, COMPOSITOR_XWM_SEL_WINDOW));
                        wayland_drag_x_types(&types, &actions);
                        compositor_xwm_wdrag_actions(x, actions);
                }
                wayland_drag_x_types(&types, &actions);
                x->wdrag.action = xwm_dnd_to_x(x, actions);
                x->wdrag.place = xwm_dnd_place(w->x + px, w->y + py), x->wdrag.moved = 1;
                if (x->wdrag.target != w->id)
                {
                        compositor_xwm_wdrag_leave(x);
                        x->wdrag.target = w->id, x->wdrag.row = (int)row;
                        xwm_wait_prop(x, w->id, x->atoms[COMPOSITOR_XWM_A_XDND_PROXY], COMPOSITOR_XWM_XA_WINDOW_TYPE, 1, COMPOSITOR_XWM_PROP_XDND_PROXY, 0);
                }
                else if (x->wdrag.entered && !x->wdrag.waiting)
                        compositor_xwm_wdrag_position(x);
                compositor_xwm_flush(x);
                return 1;
        }
        return 0;
}

static void compositor_xwm_wdrag_proxy(struct compositor_xwm *x, const struct xwm_property_reply *r, const struct compositor_xwm_pending *p)
{
        unsigned int named = r && r->format == XWM_FORMAT_32 && r->value_len >= 4 ? xwm_get32(r->value) : 0, ask;
        if (!x->wdrag.live || (p->what == COMPOSITOR_XWM_PROP_XDND_PROXIED ? p->slot : p->window) != x->wdrag.target)
                return;
        if (p->what == COMPOSITOR_XWM_PROP_XDND_PROXY && named && named != p->window)
        {
                xwm_wait_prop(x, named, x->atoms[COMPOSITOR_XWM_A_XDND_PROXY], COMPOSITOR_XWM_XA_WINDOW_TYPE, 1, COMPOSITOR_XWM_PROP_XDND_PROXIED, p->window);
                return;
        }
        x->wdrag.proxy = p->what == COMPOSITOR_XWM_PROP_XDND_PROXIED && named == p->window ? named : 0;
        ask = x->wdrag.proxy ? x->wdrag.proxy : x->wdrag.target;
        xwm_wait_prop(x, ask, x->atoms[COMPOSITOR_XWM_A_XDND_AWARE], COMPOSITOR_XWM_ATOM_TYPE, 1, COMPOSITOR_XWM_PROP_XDND_AWARE, ask);
}

static void compositor_xwm_wdrag_actions(struct compositor_xwm *x, unsigned int actions)
{
        static const struct { unsigned int bit; unsigned short atom; const char *word; unsigned char len; } spec[3] = {
                {XWM_DND_COPY, COMPOSITOR_XWM_A_XDND_COPY, "Copy", 5},
                {XWM_DND_MOVE, COMPOSITOR_XWM_A_XDND_MOVE, "Move", 5},
                {XWM_DND_ASK, COMPOSITOR_XWM_A_XDND_ASK, "Ask", 4},
        };
        unsigned int window = compositor_xwm_id(x, COMPOSITOR_XWM_SEL_WINDOW), list[3], count = 0;
        unsigned char words[16];
        unsigned long bytes = 0;

        if (!(actions & XWM_DND_ASK) && actions != (XWM_DND_COPY | XWM_DND_MOVE))
        {
                compositor_xwm_put32(x, window, x->atoms[COMPOSITOR_XWM_A_XDND_ACTIONS], COMPOSITOR_XWM_ATOM_TYPE, list, 0);
                return;
        }
        for (unsigned int i = 0; i < 3; i++)
                if (actions & spec[i].bit)
                        list[count++] = x->atoms[spec[i].atom], memory_copy(words + bytes, spec[i].word, spec[i].len), bytes += spec[i].len;
        compositor_xwm_put32(x, window, x->atoms[COMPOSITOR_XWM_A_XDND_ACTIONS], COMPOSITOR_XWM_ATOM_TYPE, list, count);
        compositor_xwm_put(x, window, x->atoms[COMPOSITOR_XWM_A_XDND_DESCRIPTIONS], COMPOSITOR_XWM_XA_STRING, XWM_FORMAT_8, words, bytes);
}

static void compositor_xwm_wdrag_aware(struct compositor_xwm *x, const struct xwm_property_reply *r, unsigned int window)
{
        unsigned int atoms[256], data[5], count, version;
        if (!x->wdrag.live || window != (x->wdrag.proxy ? x->wdrag.proxy : x->wdrag.target))
                return;
        version = r && r->format == XWM_FORMAT_32 && r->value_len >= 4 ? xwm_get32(r->value) : 0;
        if (version < 3)
        {
                x->wdrag.aware_no = 1, wayland_drag_x_status(0, 0);
                return;
        }
        x->wdrag.version = version > XWM_XDND_VERSION ? XWM_XDND_VERSION : version;
        count = compositor_xwm_dnd_atoms(x, atoms, 256, 0);
        if (count > 3)
        {
                unsigned char list[1024];
                for (unsigned int k = 0; k < count; k++)
                        xwm_put32(list + 4 * k, atoms[k]);
                compositor_xwm_put(x, compositor_xwm_id(x, COMPOSITOR_XWM_SEL_WINDOW), x->atoms[COMPOSITOR_XWM_A_XDND_TYPES], COMPOSITOR_XWM_ATOM_TYPE, XWM_FORMAT_32,
                                   list, count * 4);
        }
        xwm_dnd_enter(data, compositor_xwm_id(x, COMPOSITOR_XWM_SEL_WINDOW), x->wdrag.version, atoms, count);
        compositor_xwm_wdrag_say(x, x->atoms[COMPOSITOR_XWM_A_XDND_ENTER], data);
        x->wdrag.entered = 1;
        compositor_xwm_wdrag_position(x);
}

static void compositor_xwm_wdrag_message(struct compositor_xwm *x, const struct xwm_event *e)
{
        if (!x->wdrag.live || !x->wdrag.target || e->data[0] != x->wdrag.target)
                return;
        if (e->atom == x->atoms[COMPOSITOR_XWM_A_XDND_STATUS])
        {
                unsigned int accepted = e->data[1] & 1u, action = xwm_dnd_to_wl(x, e->data[4]);
                x->wdrag.waiting = 0;
                wayland_drag_x_status(accepted, accepted && !action ? XWM_DND_COPY : action);
                if (x->wdrag.moved && x->wdrag.entered && !x->wdrag.dropped)
                        compositor_xwm_wdrag_position(x);
        }
        else if (e->atom == x->atoms[COMPOSITOR_XWM_A_XDND_FINISHED] && x->wdrag.dropped)
                wayland_drag_x_finished(x->wdrag.version < 5 || (e->data[1] & 1u));
}

static void compositor_xwm_wdrag_gone(struct compositor_xwm *x, unsigned int window)
{
        if (!x->wdrag.live || window != x->wdrag.target)
                return;
        x->wdrag.entered = 0;
        if (x->wdrag.dropped)
                wayland_drag_x_finished(0);
        else
                x->wdrag.target = 0, wayland_drag_x_status(0, 0);
}

static void compositor_xwm_xdrag_status(struct compositor_xwm *x)
{
        unsigned long action = 0;
        int accepted = x->xdrag.began && !x->xdrag.escaped && wayland_drag_x_answer(&action);
        unsigned int data[5] = {compositor_xwm_id(x, COMPOSITOR_XWM_PROXY_WINDOW), (accepted ? 1u : 0u) | 2u, 0, 0,
                                accepted ? xwm_dnd_to_x(x, (unsigned int)action) : 0};
        compositor_xwm_message32(x, x->xdrag.source, x->atoms[COMPOSITOR_XWM_A_XDND_STATUS], data);
        x->xdrag.told_accept = (unsigned char)accepted, x->xdrag.told_action = data[4];
        if (accepted)
                x->xdrag.action = data[4];
}

static unsigned int compositor_xwm_xdrag_actions(struct compositor_xwm *x)
{
        unsigned int action = xwm_dnd_to_wl(x, x->xdrag.action);
        if (action == XWM_DND_ASK)
                return XWM_DND_ASK | ((x->xdrag.listed & (XWM_DND_COPY | XWM_DND_MOVE)) ? x->xdrag.listed & (XWM_DND_COPY | XWM_DND_MOVE) : XWM_DND_COPY);
        return action ? action : XWM_DND_COPY;
}

static void compositor_xwm_xdrag_begin(struct compositor_xwm *x)
{
        const unsigned int *atoms = (const unsigned int *)x->xdrag.types.at;
        long bytes = 0;
        wayland_tables_host(&compositor_wl_host);
        x->type_table.unit = 1;
        for (long k = 0; atoms && k < x->xdrag.types.count; k++)
        {
                unsigned long length = 0;
                const unsigned char *name = compositor_xwm_atom_name(x, atoms[k], &length);
                unsigned char mime[XWM_ATOM_NAME_MAX + 1];
                long n = name ? xwm_target_mime(name, length, mime, sizeof mime) : 0;
                if (n > 0 && !compositor_xwm_listed(x->type_table.at, bytes, mime, (unsigned long)n) &&
                    wayland_table_reach(&x->type_table, bytes + n + 1))
                        memory_copy(x->type_table.at + bytes, mime, (positive)n + 1), bytes += n + 1;
        }
        if (bytes && wayland_drag_x_begin((const char *)x->type_table.at, bytes, compositor_xwm_xdrag_actions(x)))
                x->xdrag.began = 1;
}

static void compositor_xwm_proxy_message(struct compositor_xwm *x, const struct xwm_event *e)
{
        if (e->atom == x->atoms[COMPOSITOR_XWM_A_XDND_ENTER])
        {
                if (x->xdrag.began)
                        wayland_drag_x_leave();
                compositor_xwm_xdrag_reset(x);
                x->xdrag.cancelled = 0, x->xdrag.source = e->data[0], x->xdrag.version = e->data[1] >> 24;
                x->xdrag.entered = x->xdrag.naming = 1;
                wayland_tables_host(&compositor_wl_host);
                x->xdrag.types.unit = sizeof(unsigned int);
                if (e->data[1] & 1u)
                {
                        xwm_wait_prop(x, e->data[0], x->atoms[COMPOSITOR_XWM_A_XDND_TYPES], COMPOSITOR_XWM_ATOM_TYPE, 1024, COMPOSITOR_XWM_PROP_XDND_TYPES, 0);
                        compositor_xwm_xdrag_ask_actions(x, e->data[0]);
                        return;
                }
                compositor_xwm_xdrag_ask_actions(x, e->data[0]);
                for (int k = 2; k < 5; k++)
                        if (e->data[k] && wayland_table_reach(&x->xdrag.types, x->xdrag.types.count + 1))
                                ((unsigned int *)x->xdrag.types.at)[x->xdrag.types.count - 1] = e->data[k], compositor_xwm_name_atom(x, e->data[k]);
                xwm_wait_prop(x, e->data[0], x->atoms[COMPOSITOR_XWM_A_XDND_TYPES], COMPOSITOR_XWM_ATOM_TYPE, 0, COMPOSITOR_XWM_PROP_XDND_SYNC, 0);
                return;
        }
        if (!x->xdrag.entered || e->data[0] != x->xdrag.source)
                return;
        if (e->atom == x->atoms[COMPOSITOR_XWM_A_XDND_POSITION])
        {
                int rx = compositor_xwm_from_x((int)(e->data[2] >> 16), 0), ry = compositor_xwm_from_x((int)(e->data[2] & 0xffffu), 1), tab = 0;
                int under = compositor_under(rx, ry, &tab);
                if (e->data[4] && e->data[4] != x->xdrag.action)
                {
                        x->xdrag.action = e->data[4];
                        if (x->xdrag.began)
                                wayland_drag_x_actions(compositor_xwm_xdrag_actions(x));
                }
                if (!x->xdrag.naming && !x->xdrag.began && !x->xdrag.cancelled && !x->xdrag.dropped && !x->xdrag.escaped)
                {
                        /* A drag begins on the Wayland side only while the person's hand is down (a button or a finger):
                           an X client's XdndPosition alone cannot make a drop land on a window (x11 finding S6). */
                        int hx = 0, hy = 0;
                        if (compositor_xwm_drag_hand(&hx, &hy))
                                compositor_xwm_xdrag_begin(x);
                }
                if (x->xdrag.began)
                        wayland_drag_x_motion(under, under >= 0 ? rx - compositor.x[under] : 0, under >= 0 ? ry - compositor.y[under] : 0);
                compositor_xwm_xdrag_status(x);
        }
        else if (e->atom == x->atoms[COMPOSITOR_XWM_A_XDND_LEAVE])
        {
                if (x->xdrag.began)
                        wayland_drag_x_leave();
                compositor_xwm_xdrag_reset(x);
        }
        else if (e->atom == x->atoms[COMPOSITOR_XWM_A_XDND_DROP])
        {
                x->xdrag.time = e->data[2];
                if (x->xdrag.began && !x->xdrag.escaped)
                        x->xdrag.dropped = 1, wayland_drag_x_drop();
                else
                {
                        unsigned int data[5] = {compositor_xwm_id(x, COMPOSITOR_XWM_PROXY_WINDOW), 0, 0, 0, 0};
                        compositor_xwm_message32(x, x->xdrag.source, x->atoms[COMPOSITOR_XWM_A_XDND_FINISHED], data);
                        compositor_xwm_xdrag_reset(x);
                }
        }
}

static void compositor_xwm_dnd_reply(struct compositor_xwm *x, const unsigned char *in, long size, const struct compositor_xwm_pending *p)
{
        struct xwm_property_reply r;
        if (in && xwm_parse_property_reply(in, (unsigned long)size, &r) != XWM_OK)
        {
                x->broken = 1;
                return;
        }
        if (p->what == COMPOSITOR_XWM_PROP_XDND_AWARE)
                compositor_xwm_wdrag_aware(x, in ? &r : 0, p->window);
        else if (p->what == COMPOSITOR_XWM_PROP_XDND_PROXY || p->what == COMPOSITOR_XWM_PROP_XDND_PROXIED)
                compositor_xwm_wdrag_proxy(x, in ? &r : 0, p);
        else if (!x->xdrag.entered || p->window != x->xdrag.source)
                return;
        else if (p->what == COMPOSITOR_XWM_PROP_XDND_ACTIONS)
        {
                x->xdrag.listed = 0;
                for (unsigned long k = 0; in && r.format == XWM_FORMAT_32 && k + 4 <= r.value_len; k += 4)
                        x->xdrag.listed |= xwm_dnd_to_wl(x, xwm_get32(r.value + k));
        }
        else if (p->what == COMPOSITOR_XWM_PROP_XDND_WORDS)
        {
                x->xdrag.words = 0;
                for (unsigned long k = 0; in && r.format == XWM_FORMAT_8 && k < r.value_len; k++)
                        x->xdrag.words += !r.value[k];
        }
        else if (p->what == COMPOSITOR_XWM_PROP_XDND_TYPES)
        {
                for (unsigned long k = 0; in && r.format == XWM_FORMAT_32 && k + 4 <= r.value_len && x->xdrag.types.count < COMPOSITOR_XWM_TARGETS_MOST; k += 4)
                        if (xwm_get32(r.value + k) && wayland_table_reach(&x->xdrag.types, x->xdrag.types.count + 1))
                                ((unsigned int *)x->xdrag.types.at)[x->xdrag.types.count - 1] = xwm_get32(r.value + k), compositor_xwm_name_atom(x, xwm_get32(r.value + k));
                xwm_wait_prop(x, p->window, x->atoms[COMPOSITOR_XWM_A_XDND_TYPES], COMPOSITOR_XWM_ATOM_TYPE, 0, COMPOSITOR_XWM_PROP_XDND_SYNC, 0);
        }
        else
                x->xdrag.naming = 0;
}

static int compositor_xwm_row_is_x(int row) { return row >= 0 && compositor_xwm_slot_of_row(row) != 0; }

static void compositor_xwm_xdrag_ask_actions(struct compositor_xwm *x, unsigned int source)
{
        xwm_wait_prop(x, source, x->atoms[COMPOSITOR_XWM_A_XDND_ACTIONS], COMPOSITOR_XWM_ATOM_TYPE, 64, COMPOSITOR_XWM_PROP_XDND_ACTIONS, 0);
        xwm_wait_prop(x, source, x->atoms[COMPOSITOR_XWM_A_XDND_DESCRIPTIONS], COMPOSITOR_XWM_XA_STRING, 1024, COMPOSITOR_XWM_PROP_XDND_WORDS, 0);
}

static int compositor_xwm_drag_hand(int *hx, int *hy)
{
        for (int button = 0; button < 8; button++)
                if ((compositor.button_down & (1u << button)) && compositor_xwm_row_is_x(compositor.button_row[button]))
                        return *hx = compositor.pointer_x, *hy = compositor.pointer_y, 1;
        for (int at = 0; at < COMPOSITOR_INPUTS; at++)
                for (int slot = 0; slot < UI_TOUCH_SLOTS; slot++)
                        if (compositor.screen_touch[at].id[slot] >= 0 && compositor_xwm_row_is_x((int)wayland_touch_row(compositor.screen_touch[at].id[slot])))
                                return compositor_touch_point((unsigned int)at, slot, hx, hy), 1;
        return 0;
}

static int compositor_xwm_drag_escape(void)
{
        int done = 0;
        for (int at = 0; at < COMPOSITOR_XWM_SLOTS; at++)
        {
                struct compositor_xwm *x = &compositor_xwm_slots[at];
                unsigned char req[48];
                if (x->fd < 0 || !x->xdrag.entered || x->xdrag.dropped || x->xdrag.escaped)
                        continue;
                x->xdrag.escaped = 1;
                if (x->xdrag.began)
                        wayland_drag_x_leave();
                x->xdrag.began = 0;
                compositor_xwm_xdrag_status(x);
                if (x->xtest)
                {
                        compositor_xwm_queue(x, req, xwm_enc_xtest_key(req, sizeof req, x->xtest, XWM_XTEST_KEY_PRESS, COMPOSITOR_XWM_KEY_ESCAPE));
                        compositor_xwm_queue(x, req, xwm_enc_xtest_key(req, sizeof req, x->xtest, XWM_XTEST_KEY_RELEASE, COMPOSITOR_XWM_KEY_ESCAPE));
                }
                compositor_xwm_flush(x);
                done = 1;
        }
        return done;
}

static void compositor_xwm_dnd_pass(struct compositor_xwm *x)
{
        unsigned int proxy = compositor_xwm_id(x, COMPOSITOR_XWM_PROXY_WINDOW);
        int want = -1, tab = 0, hx = 0, hy = 0;

        if (!x->claimed || !x->xfixes_event)
                return;
        if (x->sel[2].x_owner && compositor_xwm_drag_hand(&hx, &hy))
        {
                int under = compositor_under(hx, hy, &tab);
                if (under >= 0 && under < COMPOSITOR_ROWS && !compositor_xwm_row_is_x(under) &&
                    (wayland_row_owner(under) >= 0 || (compositor.table && compositor.table->drop_ok[under])) && compositor.w[under] > 0 && compositor.h[under] > 0)
                        want = under;
        }
        if (want >= 0 && (!x->xdrag.proxy_mapped || x->xdrag.proxy_row != want || x->xdrag.proxy_x != compositor.x[want] ||
                          x->xdrag.proxy_y != compositor.y[want] || x->xdrag.proxy_w != (int)compositor.w[want] || x->xdrag.proxy_h != (int)compositor.h[want]))
        {
                struct xwm_configure c = {.mask = XWM_CONFIGURE_X | XWM_CONFIGURE_Y | XWM_CONFIGURE_WIDTH | XWM_CONFIGURE_HEIGHT | XWM_CONFIGURE_STACK_MODE,
                                          .x = compositor_xwm_to_x(compositor.x[want], 0), .y = compositor_xwm_to_x(compositor.y[want], 1),
                                          .width = (unsigned int)compositor.w[want], .height = (unsigned int)compositor.h[want], .stack_mode = XWM_STACK_ABOVE};
                compositor_xwm_configure(x, proxy, &c);
                if (!x->xdrag.proxy_mapped)
                        compositor_xwm_window_op(x, XWM_OP_MAP_WINDOW, proxy);
                x->xdrag.proxy_mapped = 1, x->xdrag.proxy_row = want;
                x->xdrag.proxy_x = compositor.x[want], x->xdrag.proxy_y = compositor.y[want], x->xdrag.proxy_w = (int)c.width, x->xdrag.proxy_h = (int)c.height;
        }
        else if (want < 0 && x->xdrag.proxy_mapped)
                compositor_xwm_window_op(x, XWM_OP_UNMAP_WINDOW, proxy), x->xdrag.proxy_mapped = 0, x->xdrag.proxy_row = -1;
        if (x->xdrag.began && !x->xdrag.dropped)
        {
                unsigned long action = 0;
                int accepted = wayland_drag_x_answer(&action);
                unsigned int atom = accepted ? xwm_dnd_to_x(x, (unsigned int)action) : 0;
                if (accepted != x->xdrag.told_accept || atom != x->xdrag.told_action)
                        compositor_xwm_xdrag_status(x);
        }
}

static unsigned int compositor_xwm_drag_target(struct compositor_xwm *x, const char *mime, unsigned long length, unsigned int *time)
{
        const unsigned int *atoms = (const unsigned int *)x->xdrag.types.at;
        int kind = xwm_target_kind((const unsigned char *)mime, length);

        if (!x->xdrag.entered || !x->xdrag.source || !atoms)
                return 0;
        *time = x->xdrag.time;
        if (kind == XWM_TARGET_UTF8 || kind == XWM_TARGET_LATIN1)
                return compositor_xwm_best_text(x, atoms, x->xdrag.types.count);
        for (long k = 0; kind == XWM_TARGET_MIME && k < x->xdrag.types.count; k++)
        {
                unsigned long n = 0;
                const unsigned char *name = compositor_xwm_atom_name(x, atoms[k], &n);
                if (name && n == length && !memory_compare(name, (const unsigned char *)mime, (positive)length))
                        return atoms[k];
        }
        return 0;
}

static long compositor_xwm_drag_types(const char **types) { unsigned int actions = 0; return wayland_drag_x_types(types, &actions); }

static int compositor_xwm_drag_send(const char *mime, long fd)
{
        if (compositor_dnd.active == 1 && compositor_dnd.source >= 0)
                return compositor_wl_drag_push(mime, fd);
        if (!wayland_drag_x_send(mime, fd))
                return system_close((int)fd), 0;
        return 1;
}

static void compositor_xwm_xdnd_owner(struct compositor_xwm *x, unsigned int owner)
{
        if (owner)
                return;
        if (x->xdrag.began && !x->xdrag.dropped)
                wayland_drag_x_leave();
        if (!x->xdrag.dropped)
                compositor_xwm_xdrag_reset(x);
}

/* ---- X11 events, replies, errors, and connection lifecycle ---- */

static int xwm_row_showing(const struct compositor_xwm_window *w)
{
        return w && w->row >= 0 && w->row < COMPOSITOR_ROWS && compositor.showing[w->row];
}

static void xwm_event(struct compositor_xwm *x, const struct xwm_event *e)
{
        struct compositor_xwm_window *w;

        switch (e->kind)
        {
        case XWM_EV_MAP_REQUEST:
                if (e->synthetic)
                        break;
                if (!(w = xwm_find(x, e->window, 1)))
                {
                        compositor_xwm_counted[2]++;
                        break;
                }
                w->state = (w->state & ~(COMPOSITOR_XWM_OVERRIDE | COMPOSITOR_XWM_HIDDEN)) | COMPOSITOR_XWM_MAPPED;
                compositor_xwm_select(x, e->window, XWM_MASK_PROPERTY_CHANGE);
                xwm_ask_title(x, w);
                if (!w->owner)
                        compositor_xwm_ask_owner(x, e->window);
                xwm_ask_props(x, w, XWM_PF_MAP);
                compositor_xwm_frame_extents(x, e->window);
                compositor_xwm_window_op(x, XWM_OP_MAP_WINDOW, e->window);
                compositor_xwm_allow_commits(x, e->window);
                compositor_xwm_wm_state(x, e->window, COMPOSITOR_XWM_STATE_NORMAL);
                break;
        case XWM_EV_CONFIGURE_REQUEST:
        {
                struct xwm_configure c;
                unsigned int prev_w, prev_h, mw = (unsigned int)compositor_most_width(), mh = (unsigned int)compositor_most_height();

                if (e->synthetic)
                        break;
                w = xwm_find(x, e->window, 0);
                int prev_x = w ? w->x : 0, prev_y = w ? w->y : 0;
                prev_w = w ? w->width : 0, prev_h = w ? w->height : 0;
                if (w && (w->state & COMPOSITOR_XWM_COVERED) && (w->state & COMPOSITOR_XWM_FULLSCREEN) && w->row >= 0 &&
                    (e->value_mask & (XWM_CONFIGURE_WIDTH | XWM_CONFIGURE_HEIGHT)) == (XWM_CONFIGURE_WIDTH | XWM_CONFIGURE_HEIGHT) &&
                    (e->width != compositor.w[w->row] || e->height != compositor.h[w->row]))
                        w->restore_w = e->width, w->restore_h = e->height, compositor_xwm_fullscreen(x, w, 0);
                c.mask = e->value_mask & (XWM_CONFIGURE_X | XWM_CONFIGURE_Y | XWM_CONFIGURE_WIDTH | XWM_CONFIGURE_HEIGHT |
                                          XWM_CONFIGURE_BORDER | XWM_CONFIGURE_SIBLING | XWM_CONFIGURE_STACK_MODE);
                if (w && (w->state & COMPOSITOR_XWM_FULLSCREEN))
                        c.mask &= ~(XWM_CONFIGURE_X | XWM_CONFIGURE_Y | XWM_CONFIGURE_WIDTH | XWM_CONFIGURE_HEIGHT);
                if (w && ((w->state & COMPOSITOR_XWM_MAXIMIZED) ||
                          (w->row >= 0 && (compositor.resize == w->row || compositor.arranged[w->row] != COMPOSITOR_FLOATING))))
                        c.mask &= ~(XWM_CONFIGURE_WIDTH | XWM_CONFIGURE_HEIGHT);
                if (!w || !(w->state & COMPOSITOR_XWM_POPUPTYPE))
                {
                        c.mask &= ~(XWM_CONFIGURE_SIBLING | XWM_CONFIGURE_STACK_MODE);
                        if (!w || (w->state & COMPOSITOR_XWM_MAPPED))
                                c.mask &= ~(XWM_CONFIGURE_X | XWM_CONFIGURE_Y);
                }
                c.x = e->x, c.y = e->y;
                c.width = mw > 0 && e->width > mw ? mw : e->width, c.height = mh > 0 && e->height > mh ? mh : e->height;
                // Per ICCCM 4.1.5, WM_NORMAL_HINTS min/max constrain user/WM resizes (compositor_xwm_sync), never
                // a client's own ConfigureRequest: clients (e.g. Wine dialogs, SkyrimSELauncher, notification boxes)
                // update WM_NORMAL_HINTS and send ConfigureRequest in the same flush before async GetProperty returns.
                if ((c.mask & XWM_CONFIGURE_WIDTH) && (c.width < 1 || c.width > 65535))
                        c.width = c.width < 1 ? 1u : 65535u;
                if ((c.mask & XWM_CONFIGURE_HEIGHT) && (c.height < 1 || c.height > 65535))
                        c.height = c.height < 1 ? 1u : 65535u;
                c.border = 0, c.sibling = e->sibling, c.stack_mode = e->detail <= 4 ? e->detail : 0;
                if (c.mask)
                {
                        compositor_xwm_configure(x, e->window, &c);
                        if (w && (c.mask & XWM_CONFIGURE_X))
                                w->x = w->sent_x = c.x, w->placed = 1;
                        if (w && (c.mask & XWM_CONFIGURE_Y))
                                w->y = w->sent_y = c.y, w->placed = 1;
                        if (w && (c.mask & XWM_CONFIGURE_WIDTH))
                                w->width = w->sent_w = c.width;
                        if (w && (c.mask & XWM_CONFIGURE_HEIGHT))
                                w->height = w->sent_h = c.height;
                }
                if (w && (!c.mask ||
                          ((!(c.mask & XWM_CONFIGURE_X) || c.x == prev_x) &&
                           (!(c.mask & XWM_CONFIGURE_Y) || c.y == prev_y) &&
                           (!(c.mask & XWM_CONFIGURE_WIDTH) || c.width == prev_w) &&
                           (!(c.mask & XWM_CONFIGURE_HEIGHT) || c.height == prev_h) &&
                           !(c.mask & (XWM_CONFIGURE_SIBLING | XWM_CONFIGURE_STACK_MODE)))))
                {
                        int sh = xwm_row_showing(w);
                        compositor_xwm_send_configure(x, w, sh ? compositor_xwm_to_x(compositor.x[w->row], 0) : w->x,
                                                      sh ? compositor_xwm_to_x(compositor.y[w->row], 1) : w->y,
                                                      w->width ? w->width : (sh ? compositor.w[w->row] : 1u),
                                                      w->height ? w->height : (sh ? compositor.h[w->row] : 1u));
                }
                break;
        }
        case XWM_EV_CREATE_NOTIFY:
                if (e->synthetic || !(w = xwm_find(x, e->window, 1)))
                        break;
                xwm_shape_watch(x, e->window);
                if (e->override)
                        w->state |= COMPOSITOR_XWM_OVERRIDE;
                else if (x->atoms[COMPOSITOR_XWM_A_STEAM])
                {
                        compositor_xwm_select(x, e->window, XWM_MASK_PROPERTY_CHANGE);
                        compositor_xwm_ask(x, e->window, COMPOSITOR_XWM_PROP_STEAM, x->atoms[COMPOSITOR_XWM_A_STEAM], COMPOSITOR_XWM_ALLOW_TYPE);
                }
                w->x = e->x, w->y = e->y, w->width = e->width, w->height = e->height;
                break;
        case XWM_EV_MAP_NOTIFY:
                if (!e->synthetic && (w = xwm_find(x, e->window, 0)) && e->override)
                        w->state |= COMPOSITOR_XWM_OVERRIDE, compositor_xwm_allow_commits(x, e->window);
                break;
        case XWM_EV_CONFIGURE_NOTIFY:
                if ((w = xwm_find(x, e->window, 0)))
                {
                        unsigned int prev_w = w->width, prev_h = w->height;
                        int covers;
                        w->x = e->x, w->y = e->y, w->width = e->width, w->height = e->height;
                        // A shape is told whole by the window's size (xwm_shape_reply): a shaped window's new size asks it again.
                        if (w->row >= 0 && (w->width != prev_w || w->height != prev_h) && compositor_row_shaped(w->row))
                                xwm_shape_ask(x, w);
                        if ((w->state & COMPOSITOR_XWM_ADOPTED) && (w->state & (COMPOSITOR_XWM_OVERRIDE | COMPOSITOR_XWM_BORDERLESS)) &&
                            w->row >= 0 && w->row < COMPOSITOR_ROWS && !(w->state & COMPOSITOR_XWM_FULLSCREEN) &&
                            (w->width != prev_w || w->height != prev_h) && (covers = compositor_xwm_covers(w)) >= 0)
                                xwm_promote_fullscreen(x, w, prev_w, prev_h), compositor_row_promote_toplevel(w->row, covers);
                        else if ((w->state & COMPOSITOR_XWM_ADOPTED) && compositor_xwm_is_popup(w) && w->row >= 0)
                                w->dirty = 1;
                }
                break;
        case XWM_EV_PROPERTY_NOTIFY:
                if (e->synthetic || compositor_xwm_in_property(x, e))
                        break;
                compositor_xwm_out_property(x, e);
                if (!(w = xwm_find(x, e->window, 0)))
                        break;
                if (e->atom == x->atoms[COMPOSITOR_XWM_A_NET_NAME] ||
                    (e->atom == COMPOSITOR_XWM_XA_WM_NAME && !(w->state & COMPOSITOR_XWM_NET_TITLE)))
                        xwm_ask_title(x, w);
                else
                        xwm_notify_prop(x, w, e->atom);
                break;
        case XWM_EV_UNMAP_NOTIFY:
                if ((w = xwm_find(x, e->window, 0)))
                {
                        int was_managed = !(w->state & COMPOSITOR_XWM_OVERRIDE) && (w->state & (COMPOSITOR_XWM_MAPPED | COMPOSITOR_XWM_ADOPTED));
                        w->state &= ~(COMPOSITOR_XWM_MAPPED | COMPOSITOR_XWM_SERIAL | COMPOSITOR_XWM_FULLSCREEN |
                                      COMPOSITOR_XWM_MAXIMIZED | COMPOSITOR_XWM_POPUPTYPE | COMPOSITOR_XWM_PROPS_READ |
                                      COMPOSITOR_XWM_DESKTOP | COMPOSITOR_XWM_ABOVE | COMPOSITOR_XWM_FOCUSED);
                        if (was_managed && !e->from_configure)
                                compositor_xwm_wm_state(x, w->id, COMPOSITOR_XWM_STATE_WITHDRAWN);
                        xwm_unmapped(x, w);
                }
                break;
        case XWM_EV_DESTROY_NOTIFY:
                if (e->synthetic)
                        break;
                compositor_xwm_tray_undock(x, e->window);
                compositor_xwm_wdrag_gone(x, e->window);
                compositor_xwm_out_gone(x, e->window, 0);
                xwm_forget(x, e->window);
                break;
        case XWM_EV_SELECTION_REQUEST:
                if (!e->synthetic)
                        compositor_xwm_selection_request(x, e);
                break;
        case XWM_EV_SELECTION_NOTIFY: compositor_xwm_in_notify(x, e); break;
        case XWM_EV_SELECTION_CLEAR:
                if (!e->synthetic && e->owner == compositor_xwm_id(x, COMPOSITOR_XWM_SEL_WINDOW))
                {
                        int which = compositor_xwm_which(x, e->selection);
                        if (which >= 0 && which < 2)
                                x->sel[which].owned = 0;
                }
                break;
        case XWM_EV_CLIENT_MESSAGE:
                if (e->format == XWM_FORMAT_32 && x->claimed)
                {
                        if (e->window == compositor_xwm_id(x, COMPOSITOR_XWM_PROXY_WINDOW))
                        {
                                compositor_xwm_proxy_message(x, e);
                                break;
                        }
                        if (e->window == compositor_xwm_id(x, COMPOSITOR_XWM_SEL_WINDOW))
                        {
                                compositor_xwm_wdrag_message(x, e);
                                break;
                        }
                        if (e->window == compositor_xwm_id(x, COMPOSITOR_XWM_TRAY_WINDOW) &&
                            e->atom == x->atoms[COMPOSITOR_XWM_A_TRAY_OPCODE] && e->data[1] == 0)
                        {
                                compositor_xwm_tray_dock(x, e->data[2]);
                                break;
                        }
                }
                if (e->atom && e->format == XWM_FORMAT_32 && e->atom == x->atoms[COMPOSITOR_XWM_A_SERIAL])
                {
                        unsigned long long serial = ((unsigned long long)e->data[1] << 32) | e->data[0];
                        if (!(w = xwm_find(x, e->window, 1)))
                                break;
                        if (((w->state & COMPOSITOR_XWM_SERIAL) && w->serial != serial) || w->row >= 0 || (w->state & COMPOSITOR_XWM_ADOPTED))
                                xwm_unmapped(x, w);
                        w->serial = serial, w->state |= COMPOSITOR_XWM_SERIAL;
                        compositor_xwm_resolve(x, w);
                }
                else if (e->atom && e->format == XWM_FORMAT_32 && e->atom == x->atoms[COMPOSITOR_XWM_A_NET_STATE] && (w = xwm_find(x, e->window, 0)))
                {
                        unsigned int bits = xwm_net_state_bits(x, e->data[1], e->data[2]), hidden = x->atoms[COMPOSITOR_XWM_A_HIDDEN];
                        unsigned int want_max = bits & COMPOSITOR_XWM_MAXIMIZED;
                        if (bits & COMPOSITOR_XWM_FULLSCREEN)
                                compositor_xwm_fullscreen(x, w, (xwm_net_state_apply(e->data[0], w->state, COMPOSITOR_XWM_FULLSCREEN) & COMPOSITOR_XWM_FULLSCREEN) != 0);
                        if (want_max)
                        {
                                unsigned int was_max = w->state & COMPOSITOR_XWM_MAXIMIZED;
                                unsigned int next_bits = xwm_net_state_apply(e->data[0], was_max, want_max) & COMPOSITOR_XWM_MAXIMIZED;
                                if (next_bits != was_max)
                                {
                                        int on = next_bits != 0, was_on = was_max != 0;
                                        if (on && !was_on && !(w->state & COMPOSITOR_XWM_FULLSCREEN))
                                                w->restore_w = w->width, w->restore_h = w->height;
                                        w->state = (w->state & ~COMPOSITOR_XWM_MAXIMIZED) | next_bits;
                                        compositor_xwm_net_state(x, w->id, (int)w->state);
                                        if (on && !was_on && w->row >= 0 && !(w->state & COMPOSITOR_XWM_FULLSCREEN))
                                                compositor_arrange(w->row, COMPOSITOR_ROOM);
                                        else if (!on && was_on)
                                        {
                                                if (w->state & COMPOSITOR_XWM_FULLSCREEN)
                                                {
                                                        if (w->row >= 0)
                                                                compositor.fullscreen_was[w->row] = COMPOSITOR_FLOATING;
                                                }
                                                else
                                                {
                                                        if (w->row >= 0)
                                                                compositor_float(w->row);
                                                        xwm_restore_size(x, w);
                                                }
                                        }
                                }
                        }
                        if (hidden && (e->data[1] == hidden || e->data[2] == hidden) && xwm_row_showing(w) && !compositor_xwm_is_popup(w))
                        {
                                if (xwm_net_state_apply(e->data[0], (compositor.state[w->row] & WINDOW_MINIMIZED) ? 1u : 0u, 1u))
                                        compositor_minimize_row(w->row);
                                else if (compositor_xwm_may_show(x, w, 1))
                                        compositor_reveal(w->row);
                        }
                        if ((bits & COMPOSITOR_XWM_ABOVE) && !(w->state & COMPOSITOR_XWM_DESKTOP))
                        {
                                int on = (xwm_net_state_apply(e->data[0], w->state, COMPOSITOR_XWM_ABOVE) & COMPOSITOR_XWM_ABOVE) != 0;
                                if (on != !!(w->state & COMPOSITOR_XWM_ABOVE))
                                {
                                        w->state = on ? (w->state | COMPOSITOR_XWM_ABOVE) : (w->state & ~COMPOSITOR_XWM_ABOVE);
                                        compositor_xwm_net_state(x, w->id, (int)w->state);
                                        if (xwm_row_showing(w))
                                                compositor_layer_set(w->row, on ? WINDOW_LAYER_ABOVE : WINDOW_LAYER_NORMAL, -1);
                                }
                        }
                }
                else if (e->atom && e->atom == x->atoms[COMPOSITOR_XWM_A_REQ_FRAME_EXTENTS])
                        compositor_xwm_frame_extents(x, e->window);
                else if (e->atom && e->atom == x->atoms[COMPOSITOR_XWM_A_NET_MOVERESIZE] && e->format == XWM_FORMAT_32)
                        compositor_xwm_moveresize(xwm_find(x, e->window, 0), e->data[2]);
                else if (e->atom && e->atom == x->atoms[COMPOSITOR_XWM_A_NET_ACTIVE])
                {
                        if ((w = xwm_find(x, e->window, 0)) && xwm_row_showing(w) && !compositor_xwm_is_popup(w) &&
                            compositor_xwm_may_show(x, w, e->format == XWM_FORMAT_32 ? e->data[0] : 0))
                                compositor_reveal(w->row);
                }
                else if (e->atom && e->atom == x->atoms[COMPOSITOR_XWM_A_NET_CLOSE])
                {
                        if ((w = xwm_find(x, e->window, 0)) && xwm_row_showing(w))
                                compositor_close(w->row);
                }
                else if (e->atom && e->atom == x->atoms[COMPOSITOR_XWM_A_CHANGE_STATE] && e->format == XWM_FORMAT_32 &&
                         (e->data[0] == COMPOSITOR_XWM_STATE_ICONIC || e->data[0] == COMPOSITOR_XWM_STATE_NORMAL))
                {
                        if ((w = xwm_find(x, e->window, 0)) && xwm_row_showing(w) && !compositor_xwm_is_popup(w))
                        {
                                if (e->data[0] == COMPOSITOR_XWM_STATE_ICONIC)
                                        compositor_minimize_row(w->row);
                                else if (compositor_xwm_may_show(x, w, 1))
                                        compositor_reveal(w->row);
                        }
                }
                break;
        default:
                break;
        }
}

static void compositor_xwm_error(struct compositor_xwm *x, const unsigned char *in)
{
        struct xwm_error err;
        if (xwm_parse_error(in, XWM_HEADER, &err) != XWM_OK)
        {
                x->broken = 1;
                return;
        }
        x->errors++;
        if (err.sequence == 1)
        {
                xwm_drop(x, "another window manager runs");
                return;
        }
        if (x->pending_count && x->pending[x->pending_at].seq == err.sequence)
        {
                struct compositor_xwm_pending failed = x->pending[x->pending_at];
                struct compositor_xwm_window *w;
                compositor_xwm_pending_done(x);
                w = xwm_find(x, failed.window, 0);
                if (w && (failed.what == COMPOSITOR_XWM_PROP_NET_NAME || failed.what == COMPOSITOR_XWM_PROP_NAME))
                        compositor_xwm_title_done(x, w);
                else if (failed.what == COMPOSITOR_XWM_PROP_INTERN && (long)failed.slot < x->atom_table.count)
                        x->atom[failed.slot].length = 0;
                else if (failed.what == COMPOSITOR_XWM_PROP_ATOM_NAME && (long)failed.slot < x->atom_table.count)
                        x->atom[failed.slot].known = 1;
                else if ((failed.what == COMPOSITOR_XWM_PROP_SEL || failed.what == COMPOSITOR_XWM_PROP_SEL_SYNC) &&
                         compositor_xwm_in_at(x, (long)failed.slot) && x->ins[failed.slot].window == failed.window)
                        compositor_xwm_in_end(x, (long)failed.slot, 0);
                else if (COMPOSITOR_XWM_DND_REPLY(failed.what))
                        compositor_xwm_dnd_reply(x, 0, 0, &failed);
                else if (failed.what == COMPOSITOR_XWM_PROP_MULTIPLE)
                        compositor_xwm_multiple_reply(x, 0, 0, &failed);
                else if (w)
                        for (unsigned int i = 0; i < (unsigned int)(sizeof xwm_prop_spec / sizeof xwm_prop_spec[0]); i++)
                                if (xwm_prop_spec[i].what == failed.what && xwm_prop_spec[i].err_bit)
                                        w->state |= xwm_prop_spec[i].err_bit;
        }
}

static void compositor_xwm_message(struct compositor_xwm *x, const unsigned char *in, long size, long type)
{
        if (type == XWM_ERROR)
                compositor_xwm_error(x, in);
        else if (type == XWM_REPLY && x->pending_count && x->pending[x->pending_at].seq == xwm_get16(in + 2))
        {
                struct compositor_xwm_pending found = x->pending[x->pending_at];
                compositor_xwm_pending_done(x);
                if (found.what == COMPOSITOR_XWM_PROP_EXT)
                        xwm_ext_reply(x, in, size, found.slot);
                else if (found.what == COMPOSITOR_XWM_PROP_OWNER)
                {
                        struct compositor_xwm_window *w = xwm_find(x, found.window, 0);
                        unsigned int pid = 0;
                        if (xwm_parse_xres_client_pid(in, (unsigned long)size, &pid) != XWM_OK)
                                x->broken = 1;
                        else if (w && pid > 1 && pid <= 0x7fffffffu && pid != (unsigned int)x->peer_pid &&
                                 pid != (unsigned int)compositor_call(syscall(getpid), 0, 0, 0, 0, 0, 0))
                                w->owner = pid, compositor_xwm_owned(w);
                }
                else if (found.what == COMPOSITOR_XWM_PROP_INTERN)
                        xwm_interned(x, in, size, &found);
                else if (found.what == COMPOSITOR_XWM_PROP_ATOM_NAME)
                        compositor_xwm_named(x, in, size, &found);
                else if (found.what == COMPOSITOR_XWM_PROP_SEL || found.what == COMPOSITOR_XWM_PROP_SEL_SYNC)
                        compositor_xwm_in_reply(x, in, size, &found);
                else if (COMPOSITOR_XWM_DND_REPLY(found.what))
                        compositor_xwm_dnd_reply(x, in, size, &found);
                else if (found.what == COMPOSITOR_XWM_PROP_MULTIPLE)
                        compositor_xwm_multiple_reply(x, in, size, &found);
                else if (found.what == COMPOSITOR_XWM_PROP_SHAPE)
                        xwm_shape_reply(x, in, size, &found);
                else
                        xwm_property(x, in, size, &found);
        }
        else if (type == XWM_REPLY)
        {
                struct xwm_atom_reply atom;
                unsigned int at = (xwm_get16(in + 2) + 0x10000u - COMPOSITOR_XWM_ATOM_FIRST_SEQ) & 0xffffu;
                if (at >= COMPOSITOR_XWM_ATOMS || x->atoms_seen >= COMPOSITOR_XWM_ATOMS)
                        return;
                if (xwm_parse_atom_reply(in, (unsigned long)size, &atom) != XWM_OK)
                {
                        x->broken = 1;
                        return;
                }
                if (!x->atoms[at])
                        x->atoms_seen++;
                x->atoms[at] = atom.atom;
                if (x->atoms_seen == COMPOSITOR_XWM_ATOMS)
                        xwm_claim(x);
        }
        else if (x->xfixes_event && (in[0] & XWM_TYPE_MASK) == x->xfixes_event)
        {
                struct xwm_event e;
                if (xwm_parse_xfixes_selection(in, (unsigned long)size, x->xfixes_event, &e) != XWM_OK)
                        x->broken = 1;
                else if (!e.synthetic)
                        compositor_xwm_owner(x, &e);
        }
        else if (x->shape_event && (in[0] & XWM_TYPE_MASK) == x->shape_event)
        {
                struct compositor_xwm_window *w;

                // ShapeNotify: the shape's kind at 1 (0 the bounding one), the window at 4.
                if (in[1] == XWM_SHAPE_BOUNDING && (w = xwm_find(x, xwm_get32(in + 4), 0)))
                        xwm_shape_ask(x, w);
        }
        else
        {
                struct xwm_event e;
                if (xwm_parse_event(in, (unsigned long)size, &e) != XWM_OK)
                        x->broken = 1;
                else
                        xwm_event(x, &e);
        }
}

static void compositor_xwm_consume(struct compositor_xwm *x, long n)
{
        if (n > 0 && x->in_len > n)
                memory_copy(x->in, x->in + n, (positive)(x->in_len - n));
        x->in_len -= n;
}

static void compositor_xwm_messages(struct compositor_xwm *x)
{
        long at = 0;
        while (x->fd >= 0 && !x->broken)
        {
                long size, type;
                if (!x->ready)
                {
                        struct xwm_setup setup;
                        size = xwm_setup_size(x->in + at, (unsigned long)(x->in_len - at));
                        if (size == XWM_ERR_SHORT)
                                break;
                        if (size < 0)
                        {
                                xwm_drop(x, "setup reply too long");
                                return;
                        }
                        if (x->in_len - at < size)
                                break;
                        if (xwm_parse_setup(x->in + at, (unsigned long)size, &setup) != XWM_OK || setup.status != XWM_SETUP_SUCCESS)
                        {
                                xwm_drop(x, "setup refused or malformed");
                                return;
                        }
                        x->root = setup.root, x->resource_base = setup.resource_base, x->resource_mask = setup.resource_mask;
                        x->ready = 1, at += size;
                        compositor_xwm_start_session(x);
                        continue;
                }
                if (x->shape_skip)
                {
                        long here = x->in_len - at < x->shape_skip ? x->in_len - at : x->shape_skip;

                        at += here, x->shape_skip -= here;
                        if (x->shape_skip)
                                break;
                        continue;
                }
                if ((type = xwm_message_type(x->in + at, (unsigned long)(x->in_len - at))) == XWM_ERR_SHORT)
                        break;
                if ((size = xwm_server_message_size(x->in + at, (unsigned long)(x->in_len - at))) < 0)
                {
                        // A shape of more rectangles than a reply is kept for (a window cut pixel by pixel) is passed over as it
                        // comes, and its window is drawn whole, as before shapes were read. Any other reply that long ends the link.
                        if (size == XWM_ERR_CAP && type == XWM_REPLY && x->pending_count &&
                            x->pending[x->pending_at].what == COMPOSITOR_XWM_PROP_SHAPE && x->pending[x->pending_at].seq == xwm_get16(x->in + at + 2))
                        {
                                struct compositor_xwm_window *w = xwm_find(x, x->pending[x->pending_at].window, 0);

                                compositor_xwm_pending_done(x);
                                if (w && w->row >= 0)
                                        compositor_row_shape_set(w->row, 0, 0, 0);
                                x->shape_skip = XWM_HEADER + (long)XWM_UNIT * (long)xwm_get32(x->in + at + 4);
                                continue;
                        }
                        xwm_drop(x, "reply too long");
                        return;
                }
                if (x->in_len - at < size || x->out_len > COMPOSITOR_XWM_OUT - COMPOSITOR_XWM_ROOM)
                        break;
                compositor_xwm_message(x, x->in + at, size, type);
                if (x->fd < 0)
                        return;
                at += size;
        }
        compositor_xwm_consume(x, at);
}

static void compositor_xwm_close_rows(struct compositor_xwm *x)
{
        compositor_xwm_tray_undock(x, 0);
        for (long at = 0; at < x->window_table.count; at++)
        {
                struct compositor_xwm_window *w = &x->window[at];
                if (w->id && (w->state & COMPOSITOR_XWM_SERIAL))
                        wayland_xwindow_gone(compositor_xwm_server(x), w->serial);
                xwm_release(x, w);
        }
        x->pending_at = x->pending_count = 0, x->focus_row = -2;
        for (long at = 0; at < x->in_table.count; at++)
                if (x->ins[at].window)
                {
                        x->ins[at].eager = 0;
                        if (x->ins[at].fd >= 0)
                                system_close(x->ins[at].fd);
                        if (x->ins[at].index >= 0)
                                system_close(x->ins[at].index);
                        x->ins[at].fd = -1, x->ins[at].index = -1, x->ins[at].window = 0;
                        wayland_table_drop(&x->ins[at].targets);
                }
        for (long at = 0; at < x->out_table.count; at++)
                if (x->outs[at].requestor && x->outs[at].fd >= 0)
                        system_close(x->outs[at].fd);
        wayland_table_drop(&x->window_table);
        wayland_table_drop(&x->window_index);
        wayland_table_drop(&x->pending_table);
        wayland_table_drop(&x->in_table);
        wayland_table_drop(&x->out_table);
        wayland_table_drop(&x->atom_table);
        wayland_table_drop(&x->atom_index);
        wayland_table_drop(&x->name_table);
        wayland_table_drop(&x->type_table);
        x->window = 0, x->pending = 0, x->pending_room = 0, x->windows = 0;
        x->ins = 0, x->outs = 0, x->atom = 0, x->name_bytes = 0, x->atoms_indexed = 0, x->in_count = 0, x->out_count = 0;
        for (int which = 0; which < 3; which++)
                x->sel[which] = (struct compositor_xwm_selection){0, 0, 0, 0, 0, -1};
        x->xfixes_opcode = x->xfixes_event = x->setting = x->save.requestor = 0;
        x->shape_opcode = x->shape_event = 0, x->shape_skip = 0;
        if (x->xdrag.began && !x->xdrag.dropped)
                wayland_drag_x_leave();
        if (x->wdrag.live && x->wdrag.dropped)
                wayland_drag_x_finished(0);
        else if (x->wdrag.live)
                wayland_drag_x_status(0, 0);
        x->wdrag.live = x->wdrag.entered = x->wdrag.dropped = 0, x->wdrag.target = 0;
        compositor_xwm_xdrag_reset(x);
        wayland_table_drop(&x->xdrag.types);
        x->xdrag.proxy_mapped = 0, x->xdrag.proxy_row = -1, x->xdrag.cancelled = 0;
}

static void xwm_drop(struct compositor_xwm *x, const char *why)
{
        if (x->fd < 0)
                return;
        compositor_xwm_counted[3]++;
        compositor_xwm_log("xwm: connection closed: "), compositor_xwm_log(why), compositor_xwm_log("\n"), log_flush();
        system_close(x->fd);
        x->fd = -1;
        if (compositor_xwm_steam.slot == x)
                memory_zero(&compositor_xwm_steam, sizeof compositor_xwm_steam);
        compositor_xwm_close_rows(x);
        x->ready = x->broken = x->claimed = x->composite = 0;
        x->in_len = x->out_len = 0, x->seq = x->atoms_seen = 0;
        memory_zero(x->atoms, sizeof x->atoms);
}

static void compositor_xwm_accept(void)
{
        int credentials[3] = {0, 0, 0};
        unsigned int size = sizeof credentials;
        long fd = compositor_call(syscall(accept4), (positive)compositor_xwm_listen_fd, 0, 0, 0x800 | 0x80000, 0, 0), n;
        unsigned char setup[XWM_SETUP_REQUEST];
        struct compositor_xwm *x = 0;
        int ok;

        if (fd < 0)
                return;
        if (compositor_call(syscall(getsockopt), (positive)fd, 1, COMPOSITOR_XWM_SO_PEERCRED, (positive)credentials, (positive)&size, 0) < 0)
                credentials[0] = 0;
        ok = credentials[0] > 0 && (credentials[1] == compositor_xwm_uid || (compositor_xwm_uid == 0 && credentials[1] == 1000));
        if (ok)
                for (int at = 0; at < COMPOSITOR_XWM_SLOTS; at++)
                        if (compositor_xwm_slots[at].fd >= 0 && compositor_xwm_slots[at].peer_pid == credentials[0])
                                xwm_drop(&compositor_xwm_slots[at], "wrapper reconnected");
        for (int at = 0; at < COMPOSITOR_XWM_SLOTS && !x; at++)
                if (compositor_xwm_slots[at].fd < 0)
                        x = &compositor_xwm_slots[at];
        if (!ok || !x)
        {
                if (!x)
                        compositor_xwm_slots[0].refused++;
                compositor_xwm_counted[0]++;
                system_close((int)fd);
                return;
        }
        *x = compositor_xwm_fresh;
        x->fd = (int)fd, x->peer_pid = credentials[0], x->since = clock_monotonic_nanoseconds();
        if ((n = xwm_enc_setup_request(setup, sizeof setup)) > 0)
                memory_copy(x->out, setup, (positive)n), x->out_len = n;
        compositor_xwm_log("xwm: connected\n"), log_flush();
}

static void compositor_xwm_flush(struct compositor_xwm *x)
{
        long got;

        if (x->fd < 0 || x->out_len == 0)
                return;
        // MSG_DONTWAIT | MSG_NOSIGNAL
        got = compositor_call(syscall(sendto), (positive)x->fd, (positive)x->out, (positive)x->out_len,
                              COMPOSITOR_XWM_MSG_DONTWAIT | COMPOSITOR_XWM_MSG_NOSIGNAL, 0, 0);
        if (got == COMPOSITOR_XWM_EAGAIN)
                return;
        if (got <= 0)
        {
                xwm_drop(x, "the write failed");
                return;
        }
        if (x->out_len > got)
                memory_copy(x->out, x->out + got, (positive)(x->out_len - got));
        x->out_len -= got;
}

static void compositor_xwm_read(struct compositor_xwm *x)
{
        for (int reads = 0; reads < 4 && x->fd >= 0 && !x->broken && x->out_len <= COMPOSITOR_XWM_OUT - COMPOSITOR_XWM_ROOM; reads++)
        {
                if (x->in_len >= COMPOSITOR_XWM_IN)
                {
                        xwm_drop(x, "input full");
                        return;
                }
                long room = COMPOSITOR_XWM_IN - x->in_len;
                long got = compositor_call(syscall(recvfrom), (positive)x->fd, (positive)(x->in + x->in_len),
                                           (positive)room, COMPOSITOR_XWM_MSG_DONTWAIT, 0, 0);
                if (got == COMPOSITOR_XWM_EAGAIN)
                        return;
                if (got <= 0)
                {
                        xwm_drop(x, "connection ended");
                        return;
                }
                x->in_len += got;
                compositor_xwm_messages(x);
                if (got < room)
                        return;
        }
}

/* ---- socket, poll loop, slot lookup, Steam overlay, and Canvas bridge hooks ---- */

static void compositor_xwm_register_hooks(void);

static void compositor_xwm_start(void)
{
        unsigned char address[110];
        unsigned long length = sizeof COMPOSITOR_XWM_PATH - 1;
        long got;

        compositor_xwm_register_hooks();
        if (compositor_xwm_listen_fd >= 0)
                return;
        memory_zero(address, sizeof address);
        address[0] = 1;
        memory_copy(address + 2, COMPOSITOR_XWM_PATH, (positive)length);
        if (compositor_call(syscall(mkdirat), (positive)AT_FDCWD, (positive)"/tmp", 01777, 0, 0, 0) >= 0)
                compositor_call(syscall(fchmodat), (positive)AT_FDCWD, (positive)"/tmp", 01777, 0, 0, 0);
        if (compositor_call(syscall(mkdirat), (positive)AT_FDCWD, (positive)"/tmp/.X11-unix", 01777, 0, 0, 0) >= 0)
                compositor_call(syscall(fchmodat), (positive)AT_FDCWD, (positive)"/tmp/.X11-unix", 01777, 0, 0, 0);
        compositor_xwm_uid = (int)compositor_call(syscall(getuid), 0, 0, 0, 0, 0, 0);
        compositor_call(syscall(mkdirat), (positive)AT_FDCWD, (positive)COMPOSITOR_XWM_DIR, 0711, 0, 0, 0);
        compositor_call(syscall(fchmodat), (positive)AT_FDCWD, (positive)COMPOSITOR_XWM_DIR, 0711, 0, 0, 0);
        compositor_call(syscall(unlinkat), (positive)AT_FDCWD, (positive)COMPOSITOR_XWM_PATH, 0, 0, 0, 0);
        if ((got = compositor_call(syscall(socket), 1, 1 | 0x800 | 0x80000, 0, 0, 0, 0)) < 0)
                return;
        compositor_xwm_listen_fd = (int)got;
        if (compositor_call(syscall(bind), (positive)got, (positive)address, (positive)(2 + length + 1), 0, 0, 0) < 0 ||
            compositor_call(syscall(listen), (positive)got, COMPOSITOR_XWM_BACKLOG, 0, 0, 0, 0) < 0)
        {
                system_close(compositor_xwm_listen_fd);
                compositor_xwm_listen_fd = -1;
                compositor_call(syscall(unlinkat), (positive)AT_FDCWD, (positive)COMPOSITOR_XWM_PATH, 0, 0, 0, 0);
                return;
        }
        if (compositor_xwm_uid == 0)
                compositor_call(syscall(fchownat), (positive)AT_FDCWD, (positive)COMPOSITOR_XWM_PATH, (positive)(unsigned int)-1, (positive)1000u, 0, 0);
        compositor_call(syscall(fchmodat), (positive)AT_FDCWD, (positive)COMPOSITOR_XWM_PATH, 0660, 0, 0, 0);
        compositor_xwm_log("xwm: listening on " COMPOSITOR_XWM_PATH "\n"), log_flush();
}

static void compositor_xwm_stop(void)
{
        for (int at = 0; at < COMPOSITOR_XWM_SLOTS; at++)
                if (compositor_xwm_slots[at].fd >= 0)
                        xwm_drop(&compositor_xwm_slots[at], "compositor stops");
        if (compositor_xwm_listen_fd >= 0)
                system_close(compositor_xwm_listen_fd);
        compositor_xwm_listen_fd = -1;
        compositor_call(syscall(unlinkat), (positive)AT_FDCWD, (positive)COMPOSITOR_XWM_PATH, 0, 0, 0, 0);
}

static unsigned int compositor_xwm_poll(struct compositor_poll *wanted, unsigned int count, unsigned int base)
{
        compositor_xwm_listen_at = -1;
        for (int at = 0; at < COMPOSITOR_XWM_SLOTS; at++)
                compositor_xwm_slots[at].poll_at = -1;
        if (compositor_xwm_listen_fd < 0)
                return count;
        compositor_xwm_listen_at = (int)(count - base);
        wanted[count++] = (struct compositor_poll){compositor_xwm_listen_fd, COMPOSITOR_POLLIN, 0};
        for (int at = 0; at < COMPOSITOR_XWM_SLOTS; at++)
                if (compositor_xwm_slots[at].fd >= 0)
                {
                        compositor_xwm_slots[at].poll_at = (int)(count - base);
                        wanted[count++] = (struct compositor_poll){compositor_xwm_slots[at].fd,
                                                                   (short)(COMPOSITOR_POLLIN | (compositor_xwm_slots[at].out_len ? COMPOSITOR_POLLOUT : 0)), 0};
                }
        return count;
}

static void xwm_pass_one(struct compositor_xwm *x, const struct compositor_poll *wanted)
{
        short returned;
        if (x->poll_at < 0)
                return;
        returned = wanted[x->poll_at].returned;
        compositor_xwm_flush(x);
        if (x->fd >= 0 && (returned & COMPOSITOR_POLLIN))
                compositor_xwm_read(x);
        else if (x->fd >= 0 && (returned & (COMPOSITOR_POLLHUP | COMPOSITOR_POLLERR)))
                xwm_drop(x, "hung up");
        if (x->fd >= 0 && x->broken)
                xwm_drop(x, "protocol or queue error");
        if (x->fd >= 0 && !x->ready && clock_monotonic_nanoseconds() - x->since > COMPOSITOR_XWM_SETUP_NS)
                xwm_drop(x, "setup timeout");
        if (x->fd >= 0 && x->client < 0 && clock_monotonic_nanoseconds() - x->since > COMPOSITOR_XWM_BIND_NS && compositor_xwm_server(x) < 0)
                xwm_drop(x, "no Wayland client for X server");
        if (x->fd < 0 || !x->ready)
                return;
        compositor_xwm_flush(x);
        compositor_xwm_messages(x);
        if (x->fd >= 0 && x->broken)
        {
                xwm_drop(x, "protocol or queue error");
                return;
        }
        compositor_xwm_selections_pass(x);
        compositor_xwm_dnd_pass(x);
        for (long at = 0; at < x->window_table.count && x->fd >= 0 && !x->broken; at++)
        {
                struct compositor_xwm_window *w = &x->window[at];
                if (!w->id)
                        continue;
                if (w->row >= 0 && (!(w->state & COMPOSITOR_XWM_SERIAL) || !wayland_row_has_xserial(w->row, compositor_xwm_server(x), w->serial)))
                        xwm_unmapped(x, w);
                compositor_xwm_resolve(x, w);
                if (w->row < 0 || w->row >= COMPOSITOR_ROWS)
                        continue;
                if (compositor_xwm_close_owed(w))
                {
                        w->closes = compositor.close_asks[w->row];
                        if (compositor_xwm_is_popup(w))
                                compositor_xwm_window_op(x, XWM_OP_UNMAP_WINDOW, w->id);
                        else if ((w->protocols & COMPOSITOR_XWM_PROTO_DELETE) && w->closes < 2)
                                compositor_xwm_protocol(x, w->id, x->atoms[COMPOSITOR_XWM_A_DELETE]);
                        else
                        {
                                compositor_xwm_window_op(x, XWM_OP_DESTROY_WINDOW, w->id);
                                if (w->closes >= 2 && w->owner > 1 && w->owner <= 0x7fffffffu)
                                        compositor_call(syscall(kill), (positive)w->owner, w->closes >= 3 ? 9 : 15, 0, 0, 0, 0);
                        }
                }
                if (!compositor_xwm_is_popup(w))
                        compositor_xwm_sync(x, w);
                if (w->transient && !(w->state & COMPOSITOR_XWM_RAISED))
                {
                        struct compositor_xwm_window *parent = xwm_find(x, w->transient, 0);
                        if (parent && parent->row >= 0 && parent->row < COMPOSITOR_ROWS)
                        {
                                w->state |= COMPOSITOR_XWM_RAISED;
                                compositor_owner[w->row] = parent->row + 1;
                                compositor_raise(w->row);
                                if ((int)compositor.focus == parent->row && !w->no_input && !compositor_xwm_is_popup(w))
                                        compositor_focus(w->row);
                        }
                }
        }
        if (x->fd >= 0 && x->broken)
        {
                xwm_drop(x, "protocol or queue error");
                return;
        }
        if (x->fd >= 0)
        {
                struct compositor_xwm_window *fw = xwm_row_lookup(x, (int)compositor.focus, 0);
                int popup_focus = fw && compositor_xwm_is_popup(fw);
                unsigned int now_window = fw && !popup_focus ? fw->id : popup_focus ? x->focus_window : 0;
                if (now_window != x->focus_window)
                        x->focus_row = -3;
                x->focus_window = now_window;
                if ((int)compositor.focus != x->focus_row)
                {
                        int focused = popup_focus;
                        x->focus_row = (int)compositor.focus;
                        if (fw && !popup_focus)
                        {
                                if (!fw->no_input)
                                        compositor_xwm_focus(x, fw->id);
                                else if (fw->protocols & COMPOSITOR_XWM_PROTO_FOCUS)
                                        compositor_xwm_active(x, fw->id);
                                if (fw->protocols & COMPOSITOR_XWM_PROTO_FOCUS)
                                        compositor_xwm_protocol(x, fw->id, x->atoms[COMPOSITOR_XWM_A_TAKE_FOCUS]);
                                focused = !fw->no_input || (fw->protocols & COMPOSITOR_XWM_PROTO_FOCUS);
                        }
                        if (!focused)
                                compositor_xwm_focus(x, 0);
                }
        }
        if (x->fd >= 0 && x->last_stack_gen != compositor.stack_gen)
        {
                struct xwm_configure sc = {.mask = XWM_CONFIGURE_STACK_MODE, .stack_mode = XWM_STACK_ABOVE};
                x->last_stack_gen = compositor.stack_gen;
                for (unsigned int s = 0; s < compositor.stacked; s++)
                {
                        struct compositor_xwm_window *w = xwm_row_lookup(x, (int)compositor.stack[s], 0);
                        if (w)
                                compositor_xwm_configure(x, w->id, &sc);
                }
        }
        compositor_xwm_flush(x);
}

static void compositor_xwm_pass(const struct compositor_poll *wanted)
{
        if (compositor_xwm_listen_at >= 0 && (wanted[compositor_xwm_listen_at].returned & COMPOSITOR_POLLIN))
                compositor_xwm_accept();
        for (int at = 0; at < COMPOSITOR_XWM_SLOTS; at++)
                if (compositor_xwm_slots[at].fd >= 0 && compositor_xwm_slots[at].poll_at >= 0)
                        xwm_pass_one(&compositor_xwm_slots[at], wanted);
        if (compositor.table)
                for (unsigned int k = 0; k < 4; k++)
                        compositor.table->xwm_counts[k] = compositor_xwm_counted[k];
}

static long compositor_xwm_server(struct compositor_xwm *x)
{
        if (x->client >= 0 && wayland_client_pid(x->client) == x->client_pid && x->client_pid > 0)
                return x->client;
        x->client = x->client_pid = -1;
        if (x->peer_pid <= 0)
                return -1;
        for (long at = 0; at < wayland_client_count(); at++)
        {
                long pid = wayland_client_pid(at);
                unsigned int parent = 0;
                if (pid > 0)
                {
                        windows_proc_stat((unsigned int)pid, &parent);
                        if (parent == (unsigned int)x->peer_pid)
                                return x->client = at, x->client_pid = pid, at;
                }
        }
        return -1;
}

static struct compositor_xwm *compositor_xwm_slot_of_row(long row)
{
        struct compositor_xwm *sx = 0;
        if (row < 0 || row >= COMPOSITOR_ROWS)
                return 0;
        if (xwm_row_lookup(0, (int)row, &sx))
                return sx;
        for (int at = 0; at < COMPOSITOR_XWM_SLOTS; at++)
                if (compositor_xwm_slots[at].fd >= 0)
                        for (long w = 0; w < compositor_xwm_slots[at].window_table.count; w++)
                                if (compositor_xwm_slots[at].window[w].id && compositor_xwm_slots[at].window[w].row == (int)row)
                                        return &compositor_xwm_slots[at];
        return 0;
}

static struct compositor_xwm *compositor_xwm_first_ready(void)
{
        for (int at = 0; at < COMPOSITOR_XWM_SLOTS; at++)
                if (compositor_xwm_slots[at].fd >= 0 && compositor_xwm_slots[at].ready)
                        return &compositor_xwm_slots[at];
        return &compositor_xwm_slots[0];
}

static void compositor_xwm_selection_changed(unsigned int which)
{
        if (which > 1)
                return;
        for (int at = 0; at < COMPOSITOR_XWM_SLOTS; at++)
        {
                struct compositor_xwm *x = &compositor_xwm_slots[at];
                if (x->fd < 0 || !x->ready || compositor_xwm_server(x) < 0 || x->setting == which + 1)
                        continue;
                if (x->sel[which].reading >= 0)
                        compositor_xwm_in_free(x, x->sel[which].reading);
                x->sel[which].dirty = 1;
        }
}

static long compositor_xwm_xdrag(long what, long row, long px, long py)
{
        struct compositor_xwm *x = compositor_xwm_slot_of_row(row);
        if (what == WAYLAND_XDRAG_END)
        {
                for (int at = 0; at < COMPOSITOR_XWM_SLOTS; at++)
                        if (compositor_xwm_slots[at].fd >= 0 &&
                            (compositor_xwm_slots[at].xdrag.began || compositor_xwm_slots[at].xdrag.dropped || compositor_xwm_slots[at].wdrag.live))
                                xwm_xdrag_slot(&compositor_xwm_slots[at], what, row, px, py);
                return 0;
        }
        return xwm_xdrag_slot(x ? x : compositor_xwm_first_ready(), what, row, px, py);
}

#define COMPOSITOR_XWM_STEAM_QUIET 3000u
static struct compositor_xwm_window *compositor_xwm_steam_window(unsigned int pid, struct compositor_xwm **slot_out)
{
        struct compositor_xwm *x = compositor_xwm_steam.slot;
        struct compositor_xwm_window *w;
        if (!pid || pid == 1 || !x || x->fd < 0 || !compositor_xwm_steam.window || compositor_xwm_steam.pid != pid)
                return 0;
        if (!(w = xwm_find(x, compositor_xwm_steam.window, 0)) || w->owner != pid)
                return 0;
        if (compositor_xwm_steam.open && (unsigned int)(clock_monotonic_nanoseconds() / 1000000) - compositor_xwm_steam.told > COMPOSITOR_XWM_STEAM_QUIET)
                compositor_xwm_steam.open = 0;
        if (slot_out)
                *slot_out = x;
        return w;
}

static unsigned int compositor_xwm_steam_state(unsigned int modifiers)
{
        return ((modifiers & WINDOW_KEY_SHIFT) ? 1u : 0u) | ((modifiers & WINDOW_KEY_CONTROL) ? 4u : 0u) |
               ((modifiers & WINDOW_KEY_ALT) ? 8u : 0u) | ((modifiers & WINDOW_KEY_SUPER) ? 0x40u : 0u);
}

static void compositor_xwm_steam_send(struct compositor_xwm *x, struct compositor_xwm_window *w, unsigned int type, unsigned int detail, unsigned int state)
{
        static unsigned int last;
        unsigned int now = (unsigned int)(clock_monotonic_nanoseconds() / 1000000);
        unsigned char event[XWM_EVENT_SIZE];
        now = last = now > last ? now : last + 1;
        if (xwm_enc_input_event(event, sizeof event, type, detail, now, x->root, w->id, compositor_xwm_steam.x, compositor_xwm_steam.y, state) == XWM_EVENT_SIZE)
                compositor_xwm_send_event(x, w->id, 0, 0, event, XWM_EVENT_SIZE);
}

static int compositor_xwm_steam_wants(unsigned int pid)
{
        return compositor_xwm_steam_window(pid, 0) ? (compositor_xwm_steam.open ? 2 : 1) : 0;
}

static int compositor_xwm_steam_key(unsigned int pid, unsigned int code, int value, unsigned int modifiers)
{
        struct compositor_xwm *x = 0;
        struct compositor_xwm_window *w = compositor_xwm_steam_window(pid, &x);
        int taken = 0;
        if (!w)
                return 0;
        if (!compositor_xwm_steam.focused)
        {
                unsigned char event[XWM_EVENT_SIZE];
                if (xwm_enc_input_event(event, sizeof event, XWM_EVENT_FOCUS_IN, 3, 0, 0, w->id, 0, 0, 0) == XWM_EVENT_SIZE)
                        compositor_xwm_send_event(x, w->id, 0, 0, event, XWM_EVENT_SIZE);
                compositor_xwm_steam.focused = 1;
        }
        if (code == CK_KEY_TAB && compositor_xwm_steam.tab && value != 1)
                compositor_xwm_steam.tab = value ? compositor_xwm_steam.tab : 0, taken = 1;
        else if (compositor_xwm_steam.open)
        {
                if (value != 2 && code + 8 <= 255)
                        compositor_xwm_steam_send(x, w, value ? XWM_EVENT_KEY_PRESS : XWM_EVENT_KEY_RELEASE, code + 8, compositor_xwm_steam_state(modifiers));
                taken = 1;
        }
        else if (code == CK_KEY_TAB && value == 1 && (modifiers & WINDOW_KEY_SHIFT) &&
                 !(modifiers & (WINDOW_KEY_CONTROL | WINDOW_KEY_ALT | WINDOW_KEY_SUPER)))
        {
                compositor_xwm_steam_send(x, w, XWM_EVENT_KEY_PRESS, 50, 0);
                compositor_xwm_steam_send(x, w, XWM_EVENT_KEY_PRESS, 23, 1);
                compositor_xwm_steam_send(x, w, XWM_EVENT_KEY_RELEASE, 23, 1);
                compositor_xwm_steam_send(x, w, XWM_EVENT_KEY_RELEASE, 50, 1);
                compositor_xwm_steam.tab = 1, taken = 1;
        }
        if (taken)
                compositor_xwm_flush(x);
        return taken;
}

static int compositor_xwm_steam_scale(int at, int across, unsigned int x_across)
{
        return across <= 0 || x_across <= 1 || x_across == (unsigned int)across ? at : (int)((long long)at * (long long)x_across / (long long)across);
}

static int compositor_xwm_steam_pointer(unsigned int pid, unsigned int type, unsigned int flags, int a, int b, int width, int height, int notch)
{
        struct compositor_xwm *x = 0;
        struct compositor_xwm_window *w = compositor_xwm_steam_window(pid, &x);
        if (!w || !compositor_xwm_steam.open)
                return 0;
        if (type == WINDOW_EVENT_MOTION)
        {
                compositor_xwm_steam.x = compositor_xwm_steam_scale(a, width, w->width);
                compositor_xwm_steam.y = compositor_xwm_steam_scale(b, height, w->height);
                compositor_xwm_steam_send(x, w, XWM_EVENT_MOTION_NOTIFY, 0, 0);
        }
        else if (type == WINDOW_EVENT_BUTTON)
        {
                unsigned int button = a == 0x110 ? 1u : a == 0x112 ? 2u : a == 0x111 ? 3u : 0u;
                if (button)
                        compositor_xwm_steam_send(x, w, (flags & WINDOW_KEY_DOWN) ? XWM_EVENT_BUTTON_PRESS : XWM_EVENT_BUTTON_RELEASE, button, 0);
        }
        else if (type == WINDOW_EVENT_WHEEL && (flags & WINDOW_WHEEL_STOP))
                compositor_xwm_steam.wheel[0] = compositor_xwm_steam.wheel[1] = 0;
        else if (type == WINDOW_EVENT_WHEEL && a)
        {
                int axis = b != 0, turn = a, *left = &compositor_xwm_steam.wheel[axis];
                notch = notch > 0 ? notch : 120;
                /* the turn is a client-sized input number: at most 16 notches count, so the accumulator cannot overflow (x11 S5) */
                if (turn > 16 * notch)
                        turn = 16 * notch;
                else if (turn < -16 * notch)
                        turn = -16 * notch;
                if ((*left > 0 && turn < 0) || (*left < 0 && turn > 0))
                        *left = 0;
                *left += turn;
                for (int sent = 0; sent < 16 && (*left >= notch || *left <= -notch); sent++)
                {
                        unsigned int button = axis ? (*left > 0 ? 7u : 6u) : (*left > 0 ? 4u : 5u);
                        *left += *left > 0 ? -notch : notch;
                        compositor_xwm_steam_send(x, w, XWM_EVENT_BUTTON_PRESS, button, 0);
                        compositor_xwm_steam_send(x, w, XWM_EVENT_BUTTON_RELEASE, button, 0);
                }
                if (*left >= notch || *left <= -notch)
                        *left = 0;
        }
        compositor_xwm_flush(x);
        return 1;
}

static int xwm_steam_input_override(void *ctx, const struct compositor_input_override_event *ev)
{
        (void)ctx;
        if (!ev || !ev->pid)
                return 0;
        if (ev->kind == COMPOSITOR_OVERRIDE_QUERY)
                return compositor_xwm_steam_wants(ev->pid);
        if (ev->kind == COMPOSITOR_OVERRIDE_KEY)
                return compositor_xwm_steam_key(ev->pid, (unsigned int)ev->a, ev->b, ev->flags);
        if (ev->kind == COMPOSITOR_OVERRIDE_POINTER)
        {
                int notch = (ev->flags & WINDOW_WHEEL_PRECISE) && !(ev->flags & WINDOW_WHEEL_STREAM) ? UI_NOTCH : 120;
                return compositor_xwm_steam_pointer(ev->pid, ev->ev_type, ev->flags, ev->a, ev->b, ev->width, ev->height, notch);
        }
        return 0;
}

static int xwm_dnd_active(void *ctx)
{
        (void)ctx;
        for (int at = 0; at < COMPOSITOR_XWM_SLOTS; at++)
                if (compositor_xwm_slots[at].fd >= 0 && compositor_xwm_slots[at].xdrag.entered &&
                    !compositor_xwm_slots[at].xdrag.dropped && !compositor_xwm_slots[at].xdrag.escaped)
                        return 1;
        return 0;
}

static void xwm_dnd_cancel(void *ctx) { (void)ctx; (void)compositor_xwm_drag_escape(); }

static void xwm_hook_detach(void *ctx, int row)
{
        struct compositor_xwm *x = 0;
        struct compositor_xwm_window *w = xwm_row_lookup(0, row, &x);
        (void)ctx;
        if (w && x)
                xwm_unmapped(x, w);
}

static void xwm_hook_focus(void *ctx, int old_row, int new_row)
{
        (void)ctx; (void)old_row; (void)new_row;
        for (int at = 0; at < COMPOSITOR_XWM_SLOTS; at++)
                if (compositor_xwm_slots[at].fd >= 0)
                        compositor_xwm_slots[at].focus_row = -3;
}

static const struct compositor_row_hooks compositor_xwm_row_hooks = {.detach = xwm_hook_detach, .focus = xwm_hook_focus};

static void compositor_xwm_register_hooks(void)
{
        compositor_input_override_register(xwm_steam_input_override, 0);
        compositor_row_hooks_register(&compositor_xwm_row_hooks);
        compositor_clip_observer_register(compositor_xwm_selection_changed);
}

/* Test-suite compatibility shims for CHECK_gpu_kit (internal code passes struct compositor_xwm *x explicitly). */
static __attribute__((unused)) void compositor_xwm_claim(void) { xwm_claim(compositor_xwm_at); }
static __attribute__((unused)) long compositor_xwm_atom_add(unsigned int atom, const unsigned char *name, unsigned long length, int known) { return xwm_atom_add(compositor_xwm_at, atom, name, length, known); }
static __attribute__((unused)) long compositor_xwm_atom_index(unsigned int atom) { return xwm_atom_index(compositor_xwm_at, atom); }
static __attribute__((unused)) void compositor_xwm_interned(const unsigned char *in, long size, const struct compositor_xwm_pending *p) { xwm_interned(compositor_xwm_at, in, size, p); }
static __attribute__((unused)) struct compositor_xwm_window *compositor_xwm_find(unsigned int id, int add) { return xwm_find(compositor_xwm_at, id, add); }
static __attribute__((unused)) void compositor_xwm_release(struct compositor_xwm_window *w) { xwm_release(compositor_xwm_at, w); }
static __attribute__((unused)) void compositor_xwm_forget(unsigned int id) { xwm_forget(compositor_xwm_at, id); }
static __attribute__((unused)) void compositor_xwm_pass_one(const struct compositor_poll *wanted) { xwm_pass_one(compositor_xwm_at, wanted); }
static __attribute__((unused)) void compositor_xwm_drop(const char *why) { xwm_drop(compositor_xwm_at, why); }
static __attribute__((unused)) void compositor_xwm_event(const struct xwm_event *e) { xwm_event(compositor_xwm_at, e); }
static __attribute__((unused)) void compositor_xwm_property(const unsigned char *in, long size, const struct compositor_xwm_pending *p) { xwm_property(compositor_xwm_at, in, size, p); }
static __attribute__((unused)) void compositor_xwm_promote_props(struct compositor_xwm_window *w) { xwm_promote_props(compositor_xwm_at, w); }
static __attribute__((unused)) void compositor_xwm_unmapped(struct compositor_xwm_window *w) { xwm_unmapped(compositor_xwm_at, w); }
static __attribute__((unused)) void compositor_xwm_ask_title(struct compositor_xwm_window *w) { xwm_ask_title(compositor_xwm_at, w); }

#endif /* COMPOSITOR_XWM_INCLUDED */
