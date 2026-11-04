/*
        The window manager of Xwayland: the compositor is the X11 window manager of the rootless X server the bowl runs, over
        one connection it accepts on a socket of its own. It is included by wayland_host.c, after the shim's host, when
        CONFIG_MOONWATER_WAYLAND is on.

        THE SOCKET

        /run/moonwater/xwm/wm-0 is a unix socket in a directory that is mode 0700, so only the session's own user reaches
        it; the socket is mode 0600 as well, and a peer whose uid is not the compositor's is closed at once (SO_PEERCRED).
        A wrapper (xwayland-moonwater) connects to it and gives the connected descriptor to Xwayland as its -wm argument,
        so Xwayland's end of the connection is its window manager's, and this end is ours: the setup handshake is ours to
        start, and Xwayland answers it as a server does.

        ONE CONNECTION

        One Xwayland has one window manager. A connection that comes while another is live is closed. A connection that
        does not finish its setup within COMPOSITOR_XWM_SETUP_NS is closed, and so is one whose bytes break the codec's
        bounds (xwm_wire.c refuses every length it cannot hold, and the connection is dropped on the first refusal).

        WHAT THE WINDOW MANAGER DOES

        It selects SubstructureRedirect, SubstructureNotify and PropertyChange on the root, which makes it the one that
        maps and configures every top-level window. A MapRequest maps the window and, for a normal window, gives it the
        keyboard (SetInputFocus). A ConfigureRequest is honoured for the fields the client asked for (a fullscreen window
        keeps its size). A _NET_WM_STATE request with _NET_WM_STATE_FULLSCREEN makes the window fill the output: its X
        window is configured to the output's size at the origin, and the shim's row for it is arranged to cover the screen.
        A WL_SURFACE_SERIAL client message gives the window its X11 serial; the shim's surface that set the same serial
        (xwayland_surface_v1) names its row (wayland_serial_row), which is looked for on each pass until the shim has it.
        When the compositor asks a window to go (WINDOW_CLOSING on its row), the window manager sends it WM_DELETE_WINDOW.

        WHAT IT KEEPS

        A fixed table of COMPOSITOR_XWM_WINDOWS records, one for each top-level window the server reports (CreateNotify) or
        the client maps (MapRequest) or the shim names (serial). A window past the table's room is refused and counted.
        The output queue and the input buffer are fixed too. No allocation, no thread, no fork.
*/
#ifndef COMPOSITOR_XWM_INCLUDED
#define COMPOSITOR_XWM_INCLUDED

#include "xwm_wire.c"

#define COMPOSITOR_XWM_DIR "/run/moonwater/xwm"
#define COMPOSITOR_XWM_PATH "/run/moonwater/xwm/wm-0"
#define COMPOSITOR_XWM_BACKLOG 4
#define COMPOSITOR_XWM_WINDOWS 64                    // the top-level windows the window manager keeps records of
#define COMPOSITOR_XWM_IN (XWM_REPLY_MAX + 4096)     // what the server has sent and no message is whole yet: one whole message and more
#define COMPOSITOR_XWM_OUT 16384                     // what is queued for the server; a queue that would overflow closes the connection
#define COMPOSITOR_XWM_SETUP_NS 5000000000ull        // a connection that has not finished its setup in this long is closed
#define COMPOSITOR_XWM_ATOMS 18
#define COMPOSITOR_XWM_A_PROTOCOLS 0
#define COMPOSITOR_XWM_A_DELETE 1
#define COMPOSITOR_XWM_A_NET_STATE 2
#define COMPOSITOR_XWM_A_FULLSCREEN 3
#define COMPOSITOR_XWM_A_SERIAL 4
#define COMPOSITOR_XWM_A_ALLOW_COMMITS 5
#define COMPOSITOR_XWM_A_WM_S0 6
#define COMPOSITOR_XWM_A_WM_STATE 7
#define COMPOSITOR_XWM_A_NET_NAME 8
#define COMPOSITOR_XWM_A_UTF8 9
#define COMPOSITOR_XWM_A_TAKE_FOCUS 10
#define COMPOSITOR_XWM_A_TYPE 11              // _NET_WM_WINDOW_TYPE, then the types that are popups: tooltip, drop-down menu, popup menu,
#define COMPOSITOR_XWM_A_TYPE_FIRST 12        // combo, notification, drag and drop
#define COMPOSITOR_XWM_A_TYPE_LAST 17
#define COMPOSITOR_XWM_XA_WM_TRANSIENT_FOR 68
#define COMPOSITOR_XWM_XA_WINDOW 33
#define COMPOSITOR_XWM_XA_WM_NAME 39          // predefined atoms: WM_NAME, and the types STRING and ATOM
#define COMPOSITOR_XWM_XA_STRING 31
#define COMPOSITOR_XWM_TITLE 128              // a title's bytes, its NUL included (the shim takes fewer)
#define COMPOSITOR_XWM_PENDING 32             // the property requests whose replies are waited for (the oldest is forgotten past it)
#define COMPOSITOR_XWM_PROP_NET_NAME 1
#define COMPOSITOR_XWM_PROP_NAME 2
#define COMPOSITOR_XWM_PROP_PROTOCOLS 3
#define COMPOSITOR_XWM_PROP_TRANSIENT 4
#define COMPOSITOR_XWM_PROP_TYPE 5
#define COMPOSITOR_XWM_PROTO_DELETE 1u
#define COMPOSITOR_XWM_PROTO_FOCUS 2u
#define COMPOSITOR_XWM_NO_PLACE (-100000)
#define COMPOSITOR_XWM_ROOM 2048              // the room left in the output queue that a message is taken with
#define COMPOSITOR_XWM_STATE_NORMAL 1
#define COMPOSITOR_XWM_OP_SET_SELECTION_OWNER 22
#define COMPOSITOR_XWM_ALLOW_TYPE 6          // CARDINAL
#define COMPOSITOR_XWM_ATOM_FIRST_SEQ 2              // the sequence of the first InternAtom: ChangeWindowAttributes on the root is the first request
#define COMPOSITOR_XWM_COMPOSITE_SEQ (COMPOSITOR_XWM_ATOM_FIRST_SEQ + COMPOSITOR_XWM_ATOMS) // QueryExtension("Composite"), after the atoms
#define COMPOSITOR_XWM_COMPOSITE_NAME "Composite"
#define COMPOSITOR_XWM_ATOM_TYPE 4                   // the predefined ATOM
#define COMPOSITOR_XWM_NET_REMOVE 0
#define COMPOSITOR_XWM_NET_ADD 1
#define COMPOSITOR_XWM_NET_TOGGLE 2
#define COMPOSITOR_XWM_MAPPED 1u
#define COMPOSITOR_XWM_OVERRIDE 2u
#define COMPOSITOR_XWM_FULLSCREEN 4u
#define COMPOSITOR_XWM_SERIAL 8u
#define COMPOSITOR_XWM_DELETE_SENT 16u
#define COMPOSITOR_XWM_NET_TITLE 32u          // the title is _NET_WM_NAME's, which WM_NAME does not replace
#define COMPOSITOR_XWM_ADOPTED 64u            // the shim has the window's surface and has been told what the window is
#define COMPOSITOR_XWM_PROTO_KNOWN 128u       // WM_PROTOCOLS has been read
#define COMPOSITOR_XWM_TITLE_AGAIN 256u       // the title changed while its requests were in flight
#define COMPOSITOR_XWM_POPUPTYPE 512u         // its _NET_WM_WINDOW_TYPE is a popup's: it is shown as an override-redirect window is
#define COMPOSITOR_XWM_PROPS_READ 1024u       // the properties asked for at the map (the last is the type) have come
#define COMPOSITOR_XWM_RAISED 2048u           // a transient window has been raised over its parent once
#define COMPOSITOR_XWM_POPUPISH (COMPOSITOR_XWM_OVERRIDE | COMPOSITOR_XWM_POPUPTYPE)
#define COMPOSITOR_XWM_EAGAIN (-11)
#define COMPOSITOR_XWM_MSG_DONTWAIT 0x40
#define COMPOSITOR_XWM_MSG_NOSIGNAL 0x4000
#define COMPOSITOR_XWM_SO_PEERCRED 17

struct compositor_xwm_window
{
        unsigned int id;               // the X window; 0 for a free record
        unsigned int state;            // COMPOSITOR_XWM_* bits
        unsigned long long serial;     // the X11 serial its WL_SURFACE_SERIAL gave, when COMPOSITOR_XWM_SERIAL is set
        int row;                       // the shim's row for the window, -1 until the shim has one
        unsigned int protocols;        // COMPOSITOR_XWM_PROTO_* from WM_PROTOCOLS
        unsigned int transient;        // the window it is transient for (WM_TRANSIENT_FOR), 0 for none
        int x, y;                      // the X window's place and size, as the server last said (CreateNotify, ConfigureNotify)
        unsigned int width, height;
        int sent_x, sent_y;            // what the compositor last asked of its place and size
        unsigned int sent_w, sent_h;
        unsigned int restore_w, restore_h; // its size before fullscreen
        unsigned int asking;           // title requests in flight
        unsigned int dirty;            // the shim has not the window's latest title
        char title[COMPOSITOR_XWM_TITLE];
};

struct compositor_xwm_pending
{
        unsigned int seq, window, what; // the request's sequence, the window it is for (0: free), and COMPOSITOR_XWM_PROP_*
};

struct compositor_xwm
{
        int listen, fd, uid, ready, broken, claimed, composite;  // composite: 1 once the Composite extension has redirected the root
        unsigned int resource_base, resource_mask;
        int poll_at;                   // where the two polls are in the loop's list, relative to the wayland polls; -1 without a socket
        unsigned int root, seq, atoms[COMPOSITOR_XWM_ATOMS];
        unsigned int atoms_seen, refused, errors;
        long in_len, out_len;
        positive since;
        unsigned char in[COMPOSITOR_XWM_IN], out[COMPOSITOR_XWM_OUT];
        struct compositor_xwm_window window[COMPOSITOR_XWM_WINDOWS];
        struct compositor_xwm_pending pending[COMPOSITOR_XWM_PENDING];
        unsigned int pending_at;
        int focus_row;                 // the row with the keys at the last pass
};

static struct compositor_xwm compositor_xwm = {.listen = -1, .fd = -1, .poll_at = -1, .focus_row = -2};

static void compositor_xwm_drop(const char *why);

/* A line of the compositor's log, from a text that is not a literal. */
static void compositor_xwm_log(const char *text)
{
        unsigned long length = 0;

        while (text[length])
                length++;
        log_direct((address_any)text, (positive)length);
}

/* ---- the requests: each is encoded by xwm_wire.c, queued for the server, and counted in its sequence ---- */

/* Queues one request: its sequence is the count of requests queued, so the server's replies to it carry this number. */
static int compositor_xwm_queue(const unsigned char *bytes, long count)
{
        struct compositor_xwm *x = &compositor_xwm;

        if (count <= 0)
                return -1;
        if (x->out_len + count > COMPOSITOR_XWM_OUT)
        {
                x->broken = 1;
                return -1;
        }
        for (long at = 0; at < count; at++)
                x->out[x->out_len + at] = bytes[at];
        x->out_len += count;
        x->seq = (x->seq + 1) & 0xffffu;
        return 0;
}

static void compositor_xwm_window_op(unsigned int opcode, unsigned int window)
{
        unsigned char req[XWM_REQUEST_SHORT];
        long n = xwm_enc_window_op(req, sizeof req, opcode, window);

        if (n > 0)
                compositor_xwm_queue(req, n);
}

static void compositor_xwm_focus(unsigned int window)
{
        unsigned char req[16];
        long n = xwm_enc_set_input_focus(req, sizeof req, XWM_REVERT_POINTER_ROOT, window, XWM_CURRENT_TIME);

        if (n > 0)
                compositor_xwm_queue(req, n);
}

static void compositor_xwm_configure(unsigned int window, const struct xwm_configure *c)
{
        unsigned char req[64];
        long n = xwm_enc_configure_window(req, sizeof req, window, c);

        if (n > 0)
                compositor_xwm_queue(req, n);
}

/* WM_STATE, as ICCCM 4.1.3.1 has the window manager set it when it maps a window: NormalState, with no icon window. */
static void compositor_xwm_wm_state(unsigned int window)
{
        unsigned char req[64], data[8];
        long n;

        xwm_put32(data, COMPOSITOR_XWM_STATE_NORMAL);
        xwm_put32(data + 4, 0);
        n = xwm_enc_change_property(req, sizeof req, XWM_PROP_REPLACE, window, compositor_xwm.atoms[COMPOSITOR_XWM_A_WM_STATE],
                                    compositor_xwm.atoms[COMPOSITOR_XWM_A_WM_STATE], XWM_FORMAT_32, data, 8);
        if (n > 0)
                compositor_xwm_queue(req, n);
}

/* The window manager's own window, input only, and its ownership of WM_S0 (ICCCM 4.3.8): the selection an X server looks
   at to know a window manager runs. Its id is the first of the resources the setup gave the window manager. */
static void compositor_xwm_claim(void)
{
        struct compositor_xwm *x = &compositor_xwm;
        unsigned char req[64];
        long n;
        unsigned int window = x->resource_base | (1u & x->resource_mask);

        if (x->claimed)
                return;
        x->claimed = 1;
        n = xwm_enc_create_window(req, sizeof req, 0, window, x->root, -1, -1, 1, 1, XWM_CLASS_INPUT_ONLY, 0);
        if (n > 0)
                compositor_xwm_queue(req, n);
        n = xwm_request_start(req, sizeof req, 16, COMPOSITOR_XWM_OP_SET_SELECTION_OWNER);
        if (n > 0)
        {
                xwm_put32(req + 4, window);
                xwm_put32(req + 8, x->atoms[COMPOSITOR_XWM_A_WM_S0]);
                xwm_put32(req + 12, XWM_CURRENT_TIME);
                compositor_xwm_queue(req, n);
        }
}

/* _XWAYLAND_ALLOW_COMMITS: the window manager's word that Xwayland may commit the window's surface, set once it manages the window. */
static void compositor_xwm_allow_commits(unsigned int window)
{
        unsigned char req[64], data[4];
        long n;

        xwm_put32(data, 1);
        n = xwm_enc_change_property(req, sizeof req, XWM_PROP_REPLACE, window, compositor_xwm.atoms[COMPOSITOR_XWM_A_ALLOW_COMMITS],
                                    COMPOSITOR_XWM_ALLOW_TYPE, XWM_FORMAT_32, data, 4);
        if (n > 0)
                compositor_xwm_queue(req, n);
}

/* The window's _NET_WM_STATE, as one atom (or none): what the client reads of a fullscreen window. */
static void compositor_xwm_net_state(unsigned int window, int fullscreen)
{
        unsigned char req[64], data[4];
        long n;

        xwm_put32(data, fullscreen ? compositor_xwm.atoms[COMPOSITOR_XWM_A_FULLSCREEN] : 0);
        n = xwm_enc_change_property(req, sizeof req, XWM_PROP_REPLACE, window, compositor_xwm.atoms[COMPOSITOR_XWM_A_NET_STATE],
                                    COMPOSITOR_XWM_ATOM_TYPE, XWM_FORMAT_32, data, fullscreen ? 4 : 0);
        if (n > 0)
                compositor_xwm_queue(req, n);
}

/* A ClientMessage of WM_PROTOCOLS sent to the window: WM_DELETE_WINDOW, which the ICCCM asks a client to answer by going, or
   WM_TAKE_FOCUS, which a client that wants the keys given it takes with SetInputFocus. */
static void compositor_xwm_protocol(unsigned int window, unsigned int atom)
{
        unsigned int data[2] = {atom, XWM_CURRENT_TIME};
        unsigned char event[XWM_EVENT_SIZE], req[64];
        long n = xwm_enc_client_message(event, sizeof event, window, compositor_xwm.atoms[COMPOSITOR_XWM_A_PROTOCOLS], data, 2);

        if (n == XWM_EVENT_SIZE)
        {
                n = xwm_enc_send_event(req, sizeof req, 0, window, 0, event, XWM_EVENT_SIZE);
                if (n > 0)
                        compositor_xwm_queue(req, n);
        }
}

static void compositor_xwm_intern(unsigned int at)
{
        static const char *const names[COMPOSITOR_XWM_ATOMS] = {"WM_PROTOCOLS", "WM_DELETE_WINDOW", "_NET_WM_STATE",
                                                                "_NET_WM_STATE_FULLSCREEN", "WL_SURFACE_SERIAL",
                                                                "_XWAYLAND_ALLOW_COMMITS", "WM_S0", "WM_STATE", "_NET_WM_NAME",
                                                                "UTF8_STRING", "WM_TAKE_FOCUS", "_NET_WM_WINDOW_TYPE",
                                                                "_NET_WM_WINDOW_TYPE_TOOLTIP", "_NET_WM_WINDOW_TYPE_DROPDOWN_MENU",
                                                                "_NET_WM_WINDOW_TYPE_POPUP_MENU", "_NET_WM_WINDOW_TYPE_COMBO",
                                                                "_NET_WM_WINDOW_TYPE_NOTIFICATION", "_NET_WM_WINDOW_TYPE_DND"};
        unsigned char req[64];
        unsigned long length = 0;
        long n;

        while (names[at][length])
                length++;
        n = xwm_enc_intern_atom(req, sizeof req, 0, (const unsigned char *)names[at], length);
        if (n > 0)
                compositor_xwm_queue(req, n);
}

/* The setup and the first requests, once the server's setup reply is read: the root's events, and the atoms the events name. */
static void compositor_xwm_start_session(void)
{
        struct compositor_xwm *x = &compositor_xwm;
        unsigned char req[64];
        long n;

        // SubstructureRedirect, SubstructureNotify, PropertyChange and StructureNotify on the root: the one window manager's
        // (a server with another running refuses this with BadAccess, the first error, and the connection is dropped).
        n = xwm_enc_select_events(req, sizeof req, x->root, XWM_MASK_WM_ROOT);
        if (n > 0)
                compositor_xwm_queue(req, n);
        for (unsigned int at = 0; at < COMPOSITOR_XWM_ATOMS; at++)
                compositor_xwm_intern(at);
        // The Composite extension: a rootless Xwayland makes a surface only for a window it redirects, and its reply
        // (QueryExtension, then QueryVersion and RedirectSubwindows of the root) is taken when it comes (compositor_xwm_composite).
        n = xwm_enc_query_extension(req, sizeof req, (const unsigned char *)COMPOSITOR_XWM_COMPOSITE_NAME, 9);
        if (n > 0)
                compositor_xwm_queue(req, n);
}

/* The QueryExtension reply for Composite: when the extension is there, the root's subwindows are redirected (Manual). */
static void compositor_xwm_composite(const unsigned char *in, long size)
{
        struct compositor_xwm *x = &compositor_xwm;
        struct xwm_extension_reply r;
        unsigned char req[32];
        long n;

        if (xwm_parse_extension_reply(in, (unsigned long)size, &r) != XWM_OK)
        {
                x->broken = 1;
                return;
        }
        if (!r.present || r.major_opcode < XWM_EXT_MAJOR_FIRST)
        {
                compositor_xwm_log("xwm: no Composite extension: the X windows are not made surfaces\n");
                return;
        }
        n = xwm_enc_composite_query_version(req, sizeof req, r.major_opcode, 0, 4);
        if (n > 0)
                compositor_xwm_queue(req, n);
        n = xwm_enc_composite_redirect_subwindows(req, sizeof req, r.major_opcode, x->root, XWM_REDIRECT_MANUAL);
        if (n > 0)
                compositor_xwm_queue(req, n);
        x->composite = 1;
        compositor_xwm_log("xwm: the root's subwindows are redirected (Composite, Manual)\n");
}

/* ---- the properties: a request, and the table that says whose reply it is ---- */

/* GetProperty for a window's property (the first 128 bytes of it), the reply to be found by its sequence. */
static void compositor_xwm_ask(unsigned int window, unsigned int what, unsigned int property, unsigned int type)
{
        struct compositor_xwm *x = &compositor_xwm;
        unsigned char req[32];
        long n = xwm_enc_get_property(req, sizeof req, 0, window, property, type, 0, 32);

        if (n > 0 && compositor_xwm_queue(req, n) == 0)
        {
                struct compositor_xwm_pending *p = &x->pending[x->pending_at++ % COMPOSITOR_XWM_PENDING];

                p->seq = x->seq;
                p->window = window;
                p->what = what;
        }
}

static struct compositor_xwm_window *compositor_xwm_find(unsigned int id, int add);

static void compositor_xwm_ask_title(struct compositor_xwm_window *w)
{
        struct compositor_xwm *x = &compositor_xwm;

        // A title that changes in a loop is asked of once at a time: what changes while the replies are awaited is asked of again.
        if (w->asking)
        {
                w->state |= COMPOSITOR_XWM_TITLE_AGAIN;
                return;
        }
        w->asking = 2;
        compositor_xwm_ask(w->id, COMPOSITOR_XWM_PROP_NET_NAME, x->atoms[COMPOSITOR_XWM_A_NET_NAME], x->atoms[COMPOSITOR_XWM_A_UTF8]);
        compositor_xwm_ask(w->id, COMPOSITOR_XWM_PROP_NAME, COMPOSITOR_XWM_XA_WM_NAME, COMPOSITOR_XWM_XA_STRING);
}

/* A title's bytes as plain text, cut to what the record takes: control bytes become spaces, and a byte that is no valid UTF-8 (or any
   byte over 0x7f of a Latin-1 string) becomes a question mark. Answers the length. */
static unsigned int compositor_xwm_text(char *to, const unsigned char *from, unsigned long count, int latin1)
{
        unsigned int length = 0;

        for (unsigned long at = 0; at < count && length < COMPOSITOR_XWM_TITLE - 4;)
        {
                unsigned char c = from[at];
                unsigned long more = 0;

                if (c < 0x20 || c == 0x7f)
                {
                        to[length++] = ' ';
                        at++;
                        continue;
                }
                if (c < 0x80)
                {
                        to[length++] = (char)c;
                        at++;
                        continue;
                }
                if (!latin1 && c >= 0xc2 && c <= 0xdf)
                        more = 1;
                else if (!latin1 && c >= 0xe0 && c <= 0xef)
                        more = 2;
                else if (!latin1 && c >= 0xf0 && c <= 0xf4)
                        more = 3;
                for (unsigned long k = 1; more && k <= more; k++)
                        if (at + k >= count || (from[at + k] & 0xc0) != 0x80)
                                more = 0;
                if (!more)
                {
                        to[length++] = '?';
                        at++;
                        continue;
                }
                for (unsigned long k = 0; k <= more; k++)
                        to[length++] = (char)from[at + k];
                at += more + 1;
        }
        to[length] = 0;
        return length;
}

/* A property reply, whole, for the request it answers. */
static void compositor_xwm_property(const unsigned char *in, long size, const struct compositor_xwm_pending *p)
{
        struct compositor_xwm *x = &compositor_xwm;
        struct compositor_xwm_window *w;
        struct xwm_property_reply r;

        if (xwm_parse_property_reply(in, (unsigned long)size, &r) != XWM_OK)
        {
                x->broken = 1;
                return;
        }
        w = compositor_xwm_find(p->window, 0);
        if (!w)
                return;
        if (p->what == COMPOSITOR_XWM_PROP_NET_NAME || p->what == COMPOSITOR_XWM_PROP_NAME)
        {
                char text[COMPOSITOR_XWM_TITLE];
                unsigned int length = 0;

                if (r.format == XWM_FORMAT_8)
                        length = compositor_xwm_text(text, r.value, r.value_len, p->what == COMPOSITOR_XWM_PROP_NAME);
                if (p->what == COMPOSITOR_XWM_PROP_NET_NAME)
                {
                        if (length)
                                w->state |= COMPOSITOR_XWM_NET_TITLE;
                        else
                                w->state &= ~COMPOSITOR_XWM_NET_TITLE;
                }
                if ((p->what == COMPOSITOR_XWM_PROP_NET_NAME && length) || (p->what == COMPOSITOR_XWM_PROP_NAME && !(w->state & COMPOSITOR_XWM_NET_TITLE)))
                {
                        for (unsigned int at = 0; at <= length; at++)
                                w->title[at] = text[at];
                        w->dirty = 1;
                }
                if (w->asking && --w->asking == 0 && (w->state & COMPOSITOR_XWM_TITLE_AGAIN))
                {
                        w->state &= ~COMPOSITOR_XWM_TITLE_AGAIN;
                        compositor_xwm_ask_title(w);
                }
        }
        else if (p->what == COMPOSITOR_XWM_PROP_TRANSIENT)
                w->transient = r.format == XWM_FORMAT_32 && r.value_len >= 4 && xwm_get32(r.value) != w->id ? xwm_get32(r.value) : 0;
        else if (p->what == COMPOSITOR_XWM_PROP_TYPE)
        {
                // A window of a popup's type (a tooltip, a menu, a combo's list, a notification, a drag) is shown as a popup.
                for (unsigned long at = 0; r.format == XWM_FORMAT_32 && at + 4 <= r.value_len; at += 4)
                        for (unsigned int type = COMPOSITOR_XWM_A_TYPE_FIRST; type <= COMPOSITOR_XWM_A_TYPE_LAST; type++)
                                if (x->atoms[type] && xwm_get32(r.value + at) == x->atoms[type])
                                        w->state |= COMPOSITOR_XWM_POPUPTYPE;
                w->state |= COMPOSITOR_XWM_PROPS_READ;
        }
        else if (p->what == COMPOSITOR_XWM_PROP_PROTOCOLS && r.format == XWM_FORMAT_32)
        {
                w->protocols = 0;
                for (unsigned long at = 0; at + 4 <= r.value_len; at += 4)
                {
                        unsigned int atom = xwm_get32(r.value + at);

                        if (atom == x->atoms[COMPOSITOR_XWM_A_DELETE])
                                w->protocols |= COMPOSITOR_XWM_PROTO_DELETE;
                        else if (atom == x->atoms[COMPOSITOR_XWM_A_TAKE_FOCUS])
                                w->protocols |= COMPOSITOR_XWM_PROTO_FOCUS;
                }
                w->state |= COMPOSITOR_XWM_PROTO_KNOWN;
        }
}

/* ---- the window table ---- */

static struct compositor_xwm_window *compositor_xwm_find(unsigned int id, int add)
{
        struct compositor_xwm *x = &compositor_xwm;
        struct compositor_xwm_window *free = 0;

        if (!id)
                return 0;
        for (unsigned int at = 0; at < COMPOSITOR_XWM_WINDOWS; at++)
        {
                if (x->window[at].id == id)
                        return &x->window[at];
                if (!x->window[at].id && !free)
                        free = &x->window[at];
        }
        if (!add)
                return 0;
        if (!free)
        {
                x->refused++;
                return 0;
        }
        free->id = id;
        free->state = 0;
        free->serial = 0;
        free->row = -1;
        free->protocols = 0;
        free->transient = 0;
        free->x = free->y = 0;
        free->width = free->height = 0;
        free->sent_x = free->sent_y = COMPOSITOR_XWM_NO_PLACE;
        free->sent_w = free->sent_h = 0;
        free->restore_w = free->restore_h = 0;
        free->asking = 0;
        free->dirty = 0;
        free->title[0] = 0;
        return free;
}

static void compositor_xwm_forget(unsigned int id)
{
        struct compositor_xwm_window *w = compositor_xwm_find(id, 0);

        if (w)
                w->id = 0;
}

/*
        The shim told what the window is: a window (or an override-redirect popup at its place) with its title, said again when the
        title changes, and asked again on each pass until the shim has the window's surface, whichever of the two (the serial from the
        X side, the surface from the Wayland side) comes first. The shim's answer is the window's row, once it has one.
*/
static void compositor_xwm_resolve(struct compositor_xwm_window *w)
{
        long row;

        if (!(w->state & COMPOSITOR_XWM_SERIAL))
                return;
        // What kind of window it is waits for what the client says of it at the map (the shim waits for the word a moment).
        if (!(w->state & COMPOSITOR_XWM_OVERRIDE) && !(w->state & COMPOSITOR_XWM_PROPS_READ))
                return;
        if (!(w->state & COMPOSITOR_XWM_ADOPTED) || w->dirty)
        {
                row = wayland_xwindow_adopt(w->serial, !!(w->state & COMPOSITOR_XWM_POPUPISH), w->x, w->y, w->title);
                if (row == -2)
                        return;
                w->state |= COMPOSITOR_XWM_ADOPTED;
                w->dirty = 0;
        }
        else if (w->row < 0)
                row = wayland_serial_row(w->serial);
        else
                return;
        if (row < 0 || w->row == (int)row)
                return;
        w->row = (int)row;
        if (w->state & COMPOSITOR_XWM_FULLSCREEN)
                compositor_arrange((int)row, COMPOSITOR_MAXIMIZED);
}

/* Fullscreen: the window's X size is the output's, at the origin, and the shim's row covers the screen. Let go: floats again. */
static void compositor_xwm_fullscreen(struct compositor_xwm_window *w, int on)
{
        struct xwm_configure c;

        if (on == !!(w->state & COMPOSITOR_XWM_FULLSCREEN))
                return;
        if (on)
        {
                w->state |= COMPOSITOR_XWM_FULLSCREEN;
                w->restore_w = w->width;
                w->restore_h = w->height;
                c.mask = XWM_CONFIGURE_X | XWM_CONFIGURE_Y | XWM_CONFIGURE_WIDTH | XWM_CONFIGURE_HEIGHT;
                c.x = 0;
                c.y = 0;
                c.width = compositor.width > 0 ? (unsigned int)compositor.width : 1;
                c.height = compositor.height > 0 ? (unsigned int)compositor.height : 1;
                compositor_xwm_configure(w->id, &c);
                compositor_xwm_net_state(w->id, 1);
                if (w->row >= 0)
                        compositor_arrange(w->row, COMPOSITOR_MAXIMIZED);
        }
        else
        {
                w->state &= ~COMPOSITOR_XWM_FULLSCREEN;
                compositor_xwm_net_state(w->id, 0);
                if (w->row >= 0)
                        compositor_float(w->row);
                // The size it had is its own again: floating asks nothing of it.
                if (w->restore_w && w->restore_h)
                {
                        c.mask = XWM_CONFIGURE_WIDTH | XWM_CONFIGURE_HEIGHT;
                        c.width = w->restore_w;
                        c.height = w->restore_h;
                        compositor_xwm_configure(w->id, &c);
                        w->sent_w = w->restore_w;
                        w->sent_h = w->restore_h;
                }
        }
}

/*
        What the compositor says of a window's place and size, sent to the X window when it changes: the place the window is at in the
        output (so that the root coordinates an X client reads are the output's), and the size the compositor asks of it (a window it
        has arranged, or is resizing) in the units of the screen's scale. A size is asked once; what the client then makes of it is its
        own, and a window the compositor asks nothing of keeps the size it has.
*/
static void compositor_xwm_sync(struct compositor_xwm_window *w)
{
        struct windows *table = compositor.table;
        struct xwm_configure c;
        int row = w->row;

        c.mask = 0;
        c.x = compositor.x[row];
        c.y = compositor.y[row];
        if (c.x != w->sent_x || c.y != w->sent_y)
                c.mask |= XWM_CONFIGURE_X | XWM_CONFIGURE_Y;
        if (table && (compositor.arranged[row] != COMPOSITOR_FLOATING || compositor.asking[row]))
        {
                long scale = compositor_row_scale(row), width = (long)table->want_width[row], height = (long)table->want_height[row];

                if (scale > 0 && scale != 100)
                {
                        width = width * 100 / scale;
                        height = height * 100 / scale;
                }
                if (width > 0 && height > 0 && ((unsigned int)width != w->sent_w || (unsigned int)height != w->sent_h))
                {
                        c.mask |= XWM_CONFIGURE_WIDTH | XWM_CONFIGURE_HEIGHT;
                        c.width = (unsigned int)width;
                        c.height = (unsigned int)height;
                }
        }
        if (!c.mask)
                return;
        compositor_xwm_configure(w->id, &c);
        if (c.mask & XWM_CONFIGURE_X)
        {
                w->sent_x = c.x;
                w->sent_y = c.y;
        }
        if (c.mask & XWM_CONFIGURE_WIDTH)
        {
                w->sent_w = c.width;
                w->sent_h = c.height;
        }
}

/* ---- the events ---- */

static void compositor_xwm_event(const struct xwm_event *e)
{
        struct compositor_xwm_window *w;

        switch (e->kind)
        {
        case XWM_EV_MAP_REQUEST:
                w = compositor_xwm_find(e->window, 1);
                if (!w)
                        break;
                w->state |= COMPOSITOR_XWM_MAPPED;
                // The window's title and protocols are asked for, and changes of its properties are asked to be told.
                {
                        unsigned char req[32];
                        long n = xwm_enc_select_events(req, sizeof req, e->window, XWM_MASK_PROPERTY_CHANGE);

                        if (n > 0)
                                compositor_xwm_queue(req, n);
                }
                compositor_xwm_ask_title(w);
                compositor_xwm_ask(e->window, COMPOSITOR_XWM_PROP_PROTOCOLS, compositor_xwm.atoms[COMPOSITOR_XWM_A_PROTOCOLS], COMPOSITOR_XWM_ATOM_TYPE);
                compositor_xwm_ask(e->window, COMPOSITOR_XWM_PROP_TRANSIENT, COMPOSITOR_XWM_XA_WM_TRANSIENT_FOR, COMPOSITOR_XWM_XA_WINDOW);
                // The type is asked last: its reply says the others have come.
                compositor_xwm_ask(e->window, COMPOSITOR_XWM_PROP_TYPE, compositor_xwm.atoms[COMPOSITOR_XWM_A_TYPE], COMPOSITOR_XWM_ATOM_TYPE);
                compositor_xwm_window_op(XWM_OP_MAP_WINDOW, e->window);
                compositor_xwm_allow_commits(e->window);
                compositor_xwm_wm_state(e->window);
                // The keys: the X focus follows the window that has them (the pass), and a window the compositor gives none is given none.
                break;
        case XWM_EV_CONFIGURE_REQUEST:
        {
                struct xwm_configure c;

                w = compositor_xwm_find(e->window, 0);
                // A fullscreen window keeps the output's place and size whatever it asks.
                c.mask = e->value_mask & (XWM_CONFIGURE_X | XWM_CONFIGURE_Y | XWM_CONFIGURE_WIDTH | XWM_CONFIGURE_HEIGHT |
                                          XWM_CONFIGURE_BORDER | XWM_CONFIGURE_SIBLING | XWM_CONFIGURE_STACK_MODE);
                if (w && (w->state & COMPOSITOR_XWM_FULLSCREEN))
                        c.mask &= ~(XWM_CONFIGURE_X | XWM_CONFIGURE_Y | XWM_CONFIGURE_WIDTH | XWM_CONFIGURE_HEIGHT);
                // The compositor places a window (its place is sent when it changes): the place a client asks is not taken. A popup's
                // is where it is.
                if (!w || !(w->state & COMPOSITOR_XWM_POPUPTYPE))
                        c.mask &= ~(XWM_CONFIGURE_X | XWM_CONFIGURE_Y);
                c.x = e->x;
                c.y = e->y;
                // A size within the output.
                c.width = compositor.width > 0 && e->width > (unsigned int)compositor.width ? (unsigned int)compositor.width : e->width;
                c.height = compositor.height > 0 && e->height > (unsigned int)compositor.height ? (unsigned int)compositor.height : e->height;
                c.border = e->border;
                c.sibling = e->sibling;
                c.stack_mode = e->detail;
                if (c.mask)
                        compositor_xwm_configure(e->window, &c);
                break;
        }
        case XWM_EV_CREATE_NOTIFY:
                w = compositor_xwm_find(e->window, 1);
                if (w && e->override)
                        w->state |= COMPOSITOR_XWM_OVERRIDE;
                if (w)
                {
                        w->x = e->x;
                        w->y = e->y;
                        w->width = e->width;
                        w->height = e->height;
                }
                break;
        case XWM_EV_MAP_NOTIFY:
                // No MapRequest comes for an override-redirect window: once it is mapped Xwayland may commit its surface.
                w = compositor_xwm_find(e->window, 0);
                if (w && e->override)
                {
                        w->state |= COMPOSITOR_XWM_OVERRIDE;
                        compositor_xwm_allow_commits(e->window);
                }
                break;
        case XWM_EV_CONFIGURE_NOTIFY:
                w = compositor_xwm_find(e->window, 0);
                if (w)
                {
                        w->x = e->x;
                        w->y = e->y;
                        w->width = e->width;
                        w->height = e->height;
                }
                break;
        case XWM_EV_PROPERTY_NOTIFY:
                w = compositor_xwm_find(e->window, 0);
                if (!w)
                        break;
                if (e->atom == compositor_xwm.atoms[COMPOSITOR_XWM_A_NET_NAME] || e->atom == COMPOSITOR_XWM_XA_WM_NAME)
                        compositor_xwm_ask_title(w);
                else if (e->atom == compositor_xwm.atoms[COMPOSITOR_XWM_A_PROTOCOLS])
                        compositor_xwm_ask(w->id, COMPOSITOR_XWM_PROP_PROTOCOLS, e->atom, COMPOSITOR_XWM_ATOM_TYPE);
                break;
        case XWM_EV_UNMAP_NOTIFY:
                w = compositor_xwm_find(e->window, 0);
                if (w)
                        w->state &= ~COMPOSITOR_XWM_MAPPED;
                break;
        case XWM_EV_DESTROY_NOTIFY:
                compositor_xwm_forget(e->window);
                break;
        case XWM_EV_CLIENT_MESSAGE:
                if (e->atom == compositor_xwm.atoms[COMPOSITOR_XWM_A_SERIAL])
                {
                        w = compositor_xwm_find(e->window, 1);
                        if (!w)
                                break;
                        w->serial = ((unsigned long long)e->data[1] << 32) | e->data[0];
                        w->state |= COMPOSITOR_XWM_SERIAL;
                        compositor_xwm_resolve(w);
                }
                else if (e->atom == compositor_xwm.atoms[COMPOSITOR_XWM_A_NET_STATE])
                {
                        unsigned int fullscreen = compositor_xwm.atoms[COMPOSITOR_XWM_A_FULLSCREEN];

                        w = compositor_xwm_find(e->window, 0);
                        if (!w || (e->data[1] != fullscreen && e->data[2] != fullscreen))
                                break;
                        if (e->data[0] == COMPOSITOR_XWM_NET_ADD)
                                compositor_xwm_fullscreen(w, 1);
                        else if (e->data[0] == COMPOSITOR_XWM_NET_REMOVE)
                                compositor_xwm_fullscreen(w, 0);
                        else if (e->data[0] == COMPOSITOR_XWM_NET_TOGGLE)
                                compositor_xwm_fullscreen(w, !(w->state & COMPOSITOR_XWM_FULLSCREEN));
                }
                break;
        default:
                break;
        }
}

/* An error the server sent: its sequence says which request it answers. The first request is the root's selection, and a
   refusal of that is another window manager already running: the connection is dropped. Others are counted and go on. */
static void compositor_xwm_error(const unsigned char *in)
{
        struct xwm_error err;

        if (xwm_parse_error(in, XWM_HEADER, &err) != XWM_OK)
        {
                compositor_xwm.broken = 1;
                return;
        }
        compositor_xwm.errors++;
        if (err.sequence == 1)
        {
                compositor_xwm_drop("the server refused the root's events (another window manager runs)");
                return;
        }
}

/* One message the server sent, whole, at the start of the input: a reply, an error or an event. */
static void compositor_xwm_message(const unsigned char *in, long size, long type)
{
        struct compositor_xwm *x = &compositor_xwm;

        if (type == XWM_ERROR)
        {
                compositor_xwm_error(in);
                return;
        }
        if (type == XWM_REPLY)
        {
                unsigned int sequence = xwm_get16(in + 2);

                for (unsigned int at = 0; at < COMPOSITOR_XWM_PENDING; at++)
                        if (x->pending[at].window && x->pending[at].seq == sequence)
                        {
                                struct compositor_xwm_pending found = x->pending[at];

                                x->pending[at].window = 0;
                                compositor_xwm_property(in, size, &found);
                                return;
                        }
        }
        if (type == XWM_REPLY && xwm_get16(in + 2) == COMPOSITOR_XWM_COMPOSITE_SEQ)
        {
                compositor_xwm_composite(in, size);
                return;
        }
        if (type == XWM_REPLY)
        {
                struct xwm_atom_reply atom;
                unsigned int at = (xwm_get16(in + 2) + 0x10000u - COMPOSITOR_XWM_ATOM_FIRST_SEQ) & 0xffffu;

                if (at >= COMPOSITOR_XWM_ATOMS)
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
                        compositor_xwm_claim();
                return;
        }
        {
                struct xwm_event e;

                if (xwm_parse_event(in, (unsigned long)size, &e) != XWM_OK)
                {
                        x->broken = 1;
                        return;
                }
                compositor_xwm_event(&e);
        }
}

/* Consumes n bytes from the front of the input. */
static void compositor_xwm_consume(long n)
{
        struct compositor_xwm *x = &compositor_xwm;

        for (long at = n; at < x->in_len; at++)
                x->in[at - n] = x->in[at];
        x->in_len -= n;
}

/* Every whole message in the input, in order: the setup reply first, then the messages. A message the codec refuses drops the
   connection, so nothing a server sends is read past its bounds. */
static void compositor_xwm_messages(void)
{
        struct compositor_xwm *x = &compositor_xwm;

        while (x->fd >= 0 && !x->broken)
        {
                long size, type;

                if (!x->ready)
                {
                        struct xwm_setup setup;

                        size = xwm_setup_size(x->in, (unsigned long)x->in_len);
                        if (size == XWM_ERR_SHORT)
                                return;
                        if (size < 0)
                        {
                                compositor_xwm_drop("the setup reply is too long");
                                return;
                        }
                        if (x->in_len < size)
                                return;
                        if (xwm_parse_setup(x->in, (unsigned long)size, &setup) != XWM_OK || setup.status != XWM_SETUP_SUCCESS)
                        {
                                string_format(log, (string_address)"xwm: the setup reply has status %p and %p bytes, and the codec says %p\n",
                                              (positive)x->in[0], (positive)size, (positive)xwm_parse_setup(x->in, (unsigned long)size, &setup));
                                log_flush();
                                compositor_xwm_drop("the setup was refused or is malformed");
                                return;
                        }
                        x->root = setup.root;
                        x->resource_base = setup.resource_base;
                        x->resource_mask = setup.resource_mask;
                        x->ready = 1;
                        compositor_xwm_consume(size);
                        compositor_xwm_start_session();
                        continue;
                }
                type = xwm_message_type(x->in, (unsigned long)x->in_len);
                if (type == XWM_ERR_SHORT)
                        return;
                size = xwm_server_message_size(x->in, (unsigned long)x->in_len);
                if (size < 0)
                {
                        compositor_xwm_drop("a reply is longer than the window manager takes");
                        return;
                }
                if (x->in_len < size)
                        return;
                // A server that sends faster than the queue to it drains is taken a pass at a time: the rest waits in the input.
                if (x->out_len > COMPOSITOR_XWM_OUT - COMPOSITOR_XWM_ROOM)
                        return;
                compositor_xwm_message(x->in, size, type);
                if (x->fd < 0)
                        return;
                compositor_xwm_consume(size);
        }
}

/* ---- the connection ---- */

static void compositor_xwm_close_rows(void)
{
        struct compositor_xwm *x = &compositor_xwm;

        // The windows the connection made are taken away, and their surfaces are shown no more (those of an Xwayland that has died
        // are gone with it, and the shim finds none).
        for (unsigned int at = 0; at < COMPOSITOR_XWM_WINDOWS; at++)
        {
                struct compositor_xwm_window *w = &x->window[at];

                if (w->id && (w->state & COMPOSITOR_XWM_SERIAL))
                        wayland_xwindow_gone(w->serial);
                w->id = 0;
        }
        for (unsigned int at = 0; at < COMPOSITOR_XWM_PENDING; at++)
                x->pending[at].window = 0;
        x->focus_row = -2;
}

static void compositor_xwm_drop(const char *why)
{
        struct compositor_xwm *x = &compositor_xwm;

        if (x->fd < 0)
                return;
        compositor_xwm_log("xwm: the window manager connection is closed: ");
        compositor_xwm_log(why);
        compositor_xwm_log("\n");
        log_flush();
        system_close(x->fd);
        x->fd = -1;
        compositor_xwm_close_rows();
        x->ready = 0;
        x->broken = 0;
        x->in_len = 0;
        x->out_len = 0;
        x->seq = 0;
        x->atoms_seen = 0;
        x->claimed = 0;
        x->composite = 0;
        for (unsigned int at = 0; at < COMPOSITOR_XWM_ATOMS; at++)
                x->atoms[at] = 0;
}

/* The setup request goes out first, before anything else is read: it is the only message the window manager sends unasked. */
static void compositor_xwm_accept(void)
{
        struct compositor_xwm *x = &compositor_xwm;
        int credentials[3] = {0, 0, 0};
        unsigned int size = sizeof credentials;
        long fd;
        unsigned char setup[XWM_SETUP_REQUEST];
        long n;

        // SOCK_NONBLOCK | SOCK_CLOEXEC
        fd = compositor_call(syscall(accept4), (positive)x->listen, 0, 0, 0x800 | 0x80000, 0, 0);
        if (fd < 0)
                return;
        if (compositor_call(syscall(getsockopt), (positive)fd, 1, COMPOSITOR_XWM_SO_PEERCRED, (positive)credentials, (positive)&size, 0) < 0 ||
            credentials[1] != x->uid || x->fd >= 0)
        {
                if (x->fd >= 0)
                        x->refused++;
                system_close((int)fd);
                return;
        }
        x->fd = (int)fd;
        x->ready = 0;
        x->broken = 0;
        x->in_len = 0;
        x->out_len = 0;
        x->seq = 0;
        x->atoms_seen = 0;
        for (unsigned int at = 0; at < COMPOSITOR_XWM_ATOMS; at++)
                x->atoms[at] = 0;
        x->since = clock_monotonic_nanoseconds();
        x->focus_row = -2;
        n = xwm_enc_setup_request(setup, sizeof setup);
        if (n > 0)
                for (long at = 0; at < n; at++)
                        x->out[x->out_len++] = setup[at];
        compositor_xwm_log("xwm: the window manager of Xwayland is connected\n");
        log_flush();
}

static void compositor_xwm_flush(void)
{
        struct compositor_xwm *x = &compositor_xwm;
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
                compositor_xwm_drop("the write failed");
                return;
        }
        for (long at = got; at < x->out_len; at++)
                x->out[at - got] = x->out[at];
        x->out_len -= got;
}

/* What the server has sent, a bounded amount of it: each read is taken whole into the input and its messages are answered. */
static void compositor_xwm_read(void)
{
        struct compositor_xwm *x = &compositor_xwm;

        for (int reads = 0; reads < 4 && x->fd >= 0 && !x->broken; reads++)
        {
                long got;

                if (x->in_len >= COMPOSITOR_XWM_IN)
                {
                        compositor_xwm_drop("the input is full");
                        return;
                }
                // MSG_DONTWAIT
                got = compositor_call(syscall(recvfrom), (positive)x->fd, (positive)(x->in + x->in_len),
                                      (positive)(COMPOSITOR_XWM_IN - x->in_len), COMPOSITOR_XWM_MSG_DONTWAIT, 0, 0);
                if (got == COMPOSITOR_XWM_EAGAIN)
                        return;
                if (got <= 0)
                {
                        compositor_xwm_drop("the connection ended");
                        return;
                }
                x->in_len += got;
                compositor_xwm_messages();
        }
}

/* ---- the socket and the loop ---- */

static void compositor_xwm_start(void)
{
        struct compositor_xwm *x = &compositor_xwm;
        unsigned char address[110];
        long length = 0, got;

        if (x->listen >= 0)
                return;
        memory_zero(address, sizeof address);
        address[0] = 1; // AF_UNIX
        while (COMPOSITOR_XWM_PATH[length])
        {
                address[2 + length] = (unsigned char)COMPOSITOR_XWM_PATH[length];
                length++;
        }
        // The directory is private to the session's user, and an old socket at the path is taken away (a compositor that died left it).
        compositor_call(syscall(mkdirat), (positive)AT_FDCWD, (positive)COMPOSITOR_XWM_DIR, 0700, 0, 0, 0);
        compositor_call(syscall(fchmodat), (positive)AT_FDCWD, (positive)COMPOSITOR_XWM_DIR, 0700, 0, 0, 0);
        compositor_call(syscall(unlinkat), (positive)AT_FDCWD, (positive)COMPOSITOR_XWM_PATH, 0, 0, 0, 0);
        // SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC
        got = compositor_call(syscall(socket), 1, 1 | 0x800 | 0x80000, 0, 0, 0, 0);
        if (got < 0)
                return;
        x->listen = (int)got;
        if (compositor_call(syscall(bind), (positive)got, (positive)address, (positive)(2 + length + 1), 0, 0, 0) < 0 ||
            compositor_call(syscall(listen), (positive)got, COMPOSITOR_XWM_BACKLOG, 0, 0, 0, 0) < 0)
        {
                system_close(x->listen);
                x->listen = -1;
                compositor_call(syscall(unlinkat), (positive)AT_FDCWD, (positive)COMPOSITOR_XWM_PATH, 0, 0, 0, 0);
                return;
        }
        compositor_call(syscall(fchmodat), (positive)AT_FDCWD, (positive)COMPOSITOR_XWM_PATH, 0600, 0, 0, 0);
        x->uid = (int)compositor_call(syscall(getuid), 0, 0, 0, 0, 0, 0);
        compositor_xwm_log("xwm: listening on " COMPOSITOR_XWM_PATH "\n");
        log_flush();
}

static void compositor_xwm_stop(void)
{
        struct compositor_xwm *x = &compositor_xwm;

        if (x->fd >= 0)
                compositor_xwm_drop("the compositor stops");
        if (x->listen >= 0)
                system_close(x->listen);
        x->listen = -1;
        compositor_call(syscall(unlinkat), (positive)AT_FDCWD, (positive)COMPOSITOR_XWM_PATH, 0, 0, 0, 0);
}

/* The two polls of the window manager: its listening socket and its connection, at the end of the wayland polls. */
static unsigned int compositor_xwm_poll(struct compositor_poll *wanted, unsigned int count, unsigned int base)
{
        struct compositor_xwm *x = &compositor_xwm;

        if (x->listen < 0)
        {
                x->poll_at = -1;
                return count;
        }
        x->poll_at = (int)(count - base);
        wanted[count++] = (struct compositor_poll){x->listen, COMPOSITOR_POLLIN, 0};
        wanted[count++] = (struct compositor_poll){x->fd, (short)(COMPOSITOR_POLLIN | (x->out_len ? COMPOSITOR_POLLOUT : 0)), 0};
        return count;
}

/* One pass: a connection accepted, what the server sent answered, what it asks of the rows, and the connection timed out. */
static void compositor_xwm_pass(const struct compositor_poll *wanted)
{
        struct compositor_xwm *x = &compositor_xwm;
        short returned;

        if (x->poll_at < 0)
                return;
        if (wanted[x->poll_at].returned & COMPOSITOR_POLLIN)
                compositor_xwm_accept();
        returned = wanted[x->poll_at + 1].returned;
        // The setup request, queued at the accept, goes out before anything is waited for.
        compositor_xwm_flush();
        if (x->fd >= 0 && (returned & COMPOSITOR_POLLIN))
                compositor_xwm_read();
        else if (x->fd >= 0 && (returned & (COMPOSITOR_POLLHUP | COMPOSITOR_POLLERR)))
                compositor_xwm_drop("the connection hung up");
        if (x->fd >= 0 && x->broken)
                compositor_xwm_drop("a message the codec refused, or a queue that overflowed");
        if (x->fd >= 0 && !x->ready && clock_monotonic_nanoseconds() - x->since > COMPOSITOR_XWM_SETUP_NS)
                compositor_xwm_drop("the setup did not finish in time");
        if (x->fd < 0 || !x->ready)
                return;
        // What a drained queue lets be taken of the input.
        compositor_xwm_flush();
        compositor_xwm_messages();
        for (unsigned int at = 0; at < COMPOSITOR_XWM_WINDOWS && x->fd >= 0; at++)
        {
                struct compositor_xwm_window *w = &x->window[at];

                if (!w->id)
                        continue;
                compositor_xwm_resolve(w);
                if (w->row < 0 || w->row >= WINDOWS)
                        continue;
                // The compositor asked this window to go: a popup is unmapped, a window is asked by WM_DELETE_WINDOW (once), and is
                // destroyed when it said it does not take it.
                if ((compositor.state[w->row] & WINDOW_CLOSING) && !(w->state & COMPOSITOR_XWM_DELETE_SENT))
                {
                        w->state |= COMPOSITOR_XWM_DELETE_SENT;
                        if (w->state & COMPOSITOR_XWM_POPUPISH)
                                compositor_xwm_window_op(XWM_OP_UNMAP_WINDOW, w->id);
                        else
                        {
                                compositor_xwm_protocol(w->id, x->atoms[COMPOSITOR_XWM_A_DELETE]);
                                if ((w->state & COMPOSITOR_XWM_PROTO_KNOWN) && !(w->protocols & COMPOSITOR_XWM_PROTO_DELETE))
                                        compositor_xwm_window_op(XWM_OP_DESTROY_WINDOW, w->id);
                        }
                }
                if (!(w->state & COMPOSITOR_XWM_POPUPISH))
                        compositor_xwm_sync(w);
                // A dialog that is transient for a window is over it when it is shown, and takes the keys from it if it has them.
                if (w->transient && !(w->state & COMPOSITOR_XWM_RAISED))
                {
                        struct compositor_xwm_window *parent = compositor_xwm_find(w->transient, 0);

                        if (parent && parent->row >= 0 && parent->row < WINDOWS)
                        {
                                w->state |= COMPOSITOR_XWM_RAISED;
                                compositor_raise(w->row);
                                if ((int)compositor.focus == parent->row)
                                        compositor_focus(w->row);
                        }
                }
        }
        // The window that has the keys has the X focus (and is told, if it asked to be: WM_TAKE_FOCUS).
        if (x->fd >= 0 && (int)compositor.focus != x->focus_row)
        {
                x->focus_row = (int)compositor.focus;
                for (unsigned int at = 0; at < COMPOSITOR_XWM_WINDOWS; at++)
                {
                        struct compositor_xwm_window *w = &x->window[at];

                        if (w->id && w->row >= 0 && w->row == x->focus_row && !(w->state & COMPOSITOR_XWM_POPUPISH))
                        {
                                compositor_xwm_focus(w->id);
                                if (w->protocols & COMPOSITOR_XWM_PROTO_FOCUS)
                                        compositor_xwm_protocol(w->id, x->atoms[COMPOSITOR_XWM_A_TAKE_FOCUS]);
                        }
                }
                // The window that took the keys has its transient dialogs over it, as it had them.
                for (unsigned int at = 0; at < COMPOSITOR_XWM_WINDOWS; at++)
                {
                        struct compositor_xwm_window *w = &x->window[at], *parent;

                        if (w->id && w->transient && w->row >= 0 && (w->state & COMPOSITOR_XWM_RAISED) &&
                            (parent = compositor_xwm_find(w->transient, 0)) && parent->row >= 0 && parent->row == x->focus_row)
                                compositor_raise(w->row);
                }
        }
        compositor_xwm_flush();
}

#endif /* COMPOSITOR_XWM_INCLUDED */
