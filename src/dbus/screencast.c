/*
        The ScreenCast portal (org.freedesktop.portal.ScreenCast, version 5): how a browser, an Electron program and OBS ask to
        record the screen on Wayland, and the only way a program of a bowl may: CreateSession, SelectSources (what it may be
        offered: screens, windows, both; and the pointer drawn in or not), Start (the person is asked, in the share app the
        host shows, what to share, and the Response says the stream: its node, size, place and kind, or that the person said
        no), and OpenPipeWireRemote (a socket on which the program's libpipewire sees that stream alone, served by the host's
        PipeWire, src/cast/pipewire.c). The session is the program's until it closes it, goes, or the person stops the share
        (the tray's Stop): then its Closed signal, and the stream ends.

        Nothing is shared without the person's pick: Start only asks the host to show the share app, and the stream exists only
        once the person picked in it (dbus_cast_picked). One session is one share; a program holds DBUS_CAST_PER of them, every
        program together DBUS_CAST_SESSIONS. The session's object path is the specification's, as a request's is
        (/org/freedesktop/portal/desktop/session/SENDER/TOKEN), and every method is answered on the session's owner only: a
        session another client names is not found.

        The Responses here carry results (the session's handle, the streams), so they are written here and sent to their caller
        alone at once: every program that uses this portal (GDBus, libportal, Chromium's own, OBS's) subscribes to its
        request's path before it calls.

        Included by dbus.c after the portal's requests; its host is set with dbus_cast_set_host (the sessionbus's is
        src/cast/service.c).
*/

#define DBUS_CAST_SESSIONS 64 // every client's sessions together
#define DBUS_CAST_PER 4       // one client's
#define DBUS_SESSION_PREFIX "/org/freedesktop/portal/desktop/session/"
// The specification's source types and cursor modes, the ones offered
#define DBUS_CAST_MONITOR 1u
#define DBUS_CAST_WINDOW 2u
#define DBUS_CAST_HIDDEN 1u
#define DBUS_CAST_EMBEDDED 2u
#define DBUS_CAST_SOURCES (DBUS_CAST_MONITOR | DBUS_CAST_WINDOW)
#define DBUS_CAST_CURSORS (DBUS_CAST_HIDDEN | DBUS_CAST_EMBEDDED)

enum
{
        DBUS_CAST_MADE = 1, DBUS_CAST_SELECTED, DBUS_CAST_PICKING, DBUS_CAST_STARTED
};

// What the person is asked (the host's pick).
struct dbus_cast_ask
{
        long ticket;           // the session's number, never 0
        long pid, uid;         // the program that asks
        unsigned types, cursor, multiple;
};

struct dbus_cast_host
{
        void *context;
        int (*pick)(void *context, const struct dbus_cast_ask *ask); // the share app shown: 0, or -1 when it cannot be
        void (*unpick)(void *context, long ticket);                  // a pick, or a share, let go (closed, its program gone)
        int (*remote)(void *context, long ticket);                   // the program's end of a socket to the share's PipeWire, or -1
};

struct dbus_cast_session
{
        long ticket, client, slot;      // client: the owner's id (:1.N), at slot
        unsigned char state;
        unsigned types, cursor, multiple;
        unsigned node, source_type, width, height;
        int x, y;
        char path[DBUS_REQUEST_PATH];   // the session's
        char request[DBUS_REQUEST_PATH]; // Start's, while the person picks
};

static struct
{
        const struct dbus_cast_host *host;
        struct dbus_cast_session session[DBUS_CAST_SESSIONS];
        long tickets;
} dbus_cast;

DBUS_API void dbus_cast_set_host(const struct dbus_cast_host *host)
{
        dbus_cast.host = host;
}

// The session at `path` of client c, or null.
static struct dbus_cast_session *dbus_cast_of(const struct dbus_client *c, const char *path)
{
        for (long k = 0; k < DBUS_CAST_SESSIONS; k++)
                if (dbus_cast.session[k].state && dbus_cast.session[k].client == c->id && dbus_equal(dbus_cast.session[k].path, path))
                        return &dbus_cast.session[k];
        return 0;
}

static struct dbus_cast_session *dbus_cast_ticket(long ticket)
{
        for (long k = 0; ticket && k < DBUS_CAST_SESSIONS; k++)
                if (dbus_cast.session[k].state && dbus_cast.session[k].ticket == ticket)
                        return &dbus_cast.session[k];
        return 0;
}

static void dbus_cast_free(struct dbus_cast_session *s)
{
        if (s->state >= DBUS_CAST_PICKING && dbus_cast.host && dbus_cast.host->unpick)
                dbus_cast.host->unpick(dbus_cast.host->context, s->ticket);
        dbus_zero(s, sizeof *s);
}

// A session's path for a caller: as its request's (dbus_request_path), under the session prefix.
static void dbus_cast_path(const struct dbus_client *c, const char *token, char *into)
{
        _Static_assert(sizeof DBUS_SESSION_PREFIX == sizeof DBUS_REQUEST_PREFIX, "the two prefixes are as long");

        dbus_request_path(c, token, into);
        dbus_copy(into, DBUS_SESSION_PREFIX, (long)sizeof DBUS_SESSION_PREFIX - 1); // the request's prefix made the session's
}

// The u value of `key` in the options at argument `index`, or `none`.
static unsigned dbus_cast_option_u(const struct dbus_message *m, long index, const char *key, unsigned none)
{
        long at = dbus_option_at(m, index, key, 'u');

        return at < 0 ? none : (unsigned)dbus_get32(m->bytes + m->body_at + at);
}

static int dbus_cast_option_b(const struct dbus_message *m, long index, const char *key)
{
        long at = dbus_option_at(m, index, key, 'b');

        return at >= 0 && dbus_get32(m->bytes + m->body_at + at) != 0;
}

// The request's path and the reply that names it.
static void dbus_cast_reply_request(struct dbus_client *from, const struct dbus_message *m, long options, char *path)
{
        char token[DBUS_TOKEN + 8];
        struct dbus_out w;
        long body;

        if (dbus_option_string(m, options, "handle_token", token, sizeof token) < 0)
                token[0] = 0;
        dbus_request_path(from, token, path);
        dbus_reply_begin(&w, from, m, "o", dbus.service.unique, &body);
        dbus_o_string(&w, path);
        dbus_reply_end(&w, from, m, body);
}

// One entry of an a{sv}: its key and its variant's signature, the value to follow.
static void dbus_cast_entry(struct dbus_out *w, const char *key, const char *sig)
{
        dbus_o_pad(w, 8);
        dbus_o_string(w, key);
        dbus_o_sig(w, sig);
}

/*
        A Response with its results, to the caller alone: `code` 0 done, 1 the person said no, 2 another way; with code 0, a
        session's handle (CreateSession) or a session's stream (Start), when s is given and `what` says which.
*/
enum
{
        DBUS_CAST_NO_RESULTS, DBUS_CAST_HANDLE, DBUS_CAST_STREAMS
};

static void dbus_cast_respond(struct dbus_client *c, const char *path, unsigned long code, int what, const struct dbus_cast_session *s)
{
        struct dbus_out w = {dbus.scratch, 0, DBUS_MESSAGE, 0};
        struct dbus_fields f = {path, "org.freedesktop.portal.Request", "Response", "", c->unique, dbus.service.unique, "ua{sv}", 0, 0};
        long body, length, first;
        struct dbus_message parsed;

        dbus_o_header(&w, DBUS_SIGNAL, DBUS_FLAG_NO_REPLY, dbus_next_serial(), &f);
        body = w.length;
        dbus_o_u32(&w, code);
        dbus_o_u32(&w, 0);
        length = w.length - 4;
        dbus_o_pad(&w, 8);
        first = w.length;
        if (!code && s && what == DBUS_CAST_HANDLE)
        {
                dbus_cast_entry(&w, "session_handle", "s");
                dbus_o_string(&w, s->path);
        }
        if (!code && s && what == DBUS_CAST_STREAMS)
        {
                long streams, streams_first, props, props_first;

                dbus_cast_entry(&w, "streams", "a(ua{sv})");
                dbus_o_u32(&w, 0);
                streams = w.length - 4;
                dbus_o_pad(&w, 8);
                streams_first = w.length;
                dbus_o_pad(&w, 8);
                dbus_o_u32(&w, s->node);
                dbus_o_u32(&w, 0);
                props = w.length - 4;
                dbus_o_pad(&w, 8);
                props_first = w.length;
                dbus_cast_entry(&w, "position", "(ii)");
                dbus_o_pad(&w, 8);
                dbus_o_u32(&w, (unsigned long)(unsigned)s->x), dbus_o_u32(&w, (unsigned long)(unsigned)s->y);
                dbus_cast_entry(&w, "size", "(ii)");
                dbus_o_pad(&w, 8);
                dbus_o_u32(&w, s->width), dbus_o_u32(&w, s->height);
                dbus_cast_entry(&w, "source_type", "u");
                dbus_o_u32(&w, s->source_type);
                dbus_o_patch32(&w, props, (unsigned long)(w.length - props_first));
                dbus_o_patch32(&w, streams, (unsigned long)(w.length - streams_first));
                dbus_cast_entry(&w, "persist_mode", "u");
                dbus_o_u32(&w, 0);
        }
        dbus_o_patch32(&w, length, (unsigned long)(w.length - first));
        dbus_o_end(&w, body);
        if (!w.bad && dbus_parse(w.at, w.length, &parsed) >= 0)
                dbus_deliver(c, dbus.scratch, w.length, DBUS_ASKED_FOR);
}

// The Closed signal of a session, to its owner alone.
static void dbus_cast_closed(struct dbus_client *c, const char *path)
{
        struct dbus_out w = {dbus.scratch, 0, DBUS_MESSAGE, 0};
        struct dbus_fields f = {path, "org.freedesktop.portal.Session", "Closed", "", c->unique, dbus.service.unique, "a{sv}", 0, 0};
        struct dbus_message parsed;
        long body;

        dbus_o_header(&w, DBUS_SIGNAL, DBUS_FLAG_NO_REPLY, dbus_next_serial(), &f);
        body = w.length;
        dbus_o_u32(&w, 0);
        dbus_o_pad(&w, 8);
        dbus_o_end(&w, body);
        if (!w.bad && dbus_parse(w.at, w.length, &parsed) >= 0)
                dbus_deliver(c, dbus.scratch, w.length, 0);
}

// The portal's properties: AvailableSourceTypes, AvailableCursorModes and version (Get of one, GetAll of them).
static void dbus_cast_properties(struct dbus_client *from, const struct dbus_message *m, const char *only)
{
        static const struct
        {
                const char *name;
                unsigned value;
        } property[] = {{"AvailableSourceTypes", DBUS_CAST_SOURCES}, {"AvailableCursorModes", DBUS_CAST_CURSORS}, {"version", 5}};
        struct dbus_out w;
        long body, length = 0, first = 0;
        int found = 0;

        dbus_reply_begin(&w, from, m, only ? "v" : "a{sv}", dbus.service.unique, &body);
        if (!only)
        {
                dbus_o_u32(&w, 0);
                length = w.length - 4;
                dbus_o_pad(&w, 8);
                first = w.length;
        }
        for (long k = 0; k < 3; k++)
        {
                if (only && !dbus_equal(only, property[k].name))
                        continue;
                if (!only)
                {
                        dbus_o_pad(&w, 8);
                        dbus_o_string(&w, property[k].name);
                }
                dbus_o_sig(&w, "u");
                dbus_o_u32(&w, property[k].value);
                found = 1;
        }
        if (only && !found)
        {
                dbus_error_reply(from, m, "org.freedesktop.DBus.Error.UnknownProperty", "No such property", dbus.service.unique);
                return;
        }
        if (!only)
                dbus_o_patch32(&w, length, (unsigned long)(w.length - first));
        dbus_reply_end(&w, from, m, body);
}

// A call of the ScreenCast portal, of its sessions or of its pending request: 1 when the call was one and is answered.
static int dbus_cast_call(struct dbus_client *from, const struct dbus_message *m)
{
        char text[96], path[DBUS_REQUEST_PATH], token[DBUS_TOKEN + 8];
        struct dbus_cast_session *s = 0;
        const unsigned char *raw;
        long n;

        // with no host, there is no portal: unclaimed, its version UnknownProperty and its methods UnknownMethod, so a program hides the feature
        if (!dbus_cast.host)
                return 0;
        if (dbus_equal(m->interface, "org.freedesktop.DBus.Properties") && dbus_equal(m->path, DBUS_PATH_PORTAL) &&
            dbus_arg_string(m, 0, text, sizeof text) >= 0 && dbus_equal(text, "org.freedesktop.portal.ScreenCast"))
        {
                char key[64];

                if (dbus_equal(m->member, "GetAll") && dbus_equal(m->signature, "s"))
                        dbus_cast_properties(from, m, 0);
                else if (dbus_equal(m->member, "Get") && dbus_equal(m->signature, "ss") && dbus_arg_string(m, 1, key, sizeof key) >= 0)
                        dbus_cast_properties(from, m, key);
                else
                        dbus_error_reply(from, m, "org.freedesktop.DBus.Error.PropertyReadOnly", "The portal's properties are read only", dbus.service.unique);
                return 1;
        }
        // Start's request closed while the person picks: the pick let go, and no Response
        if (dbus_starts(m->path, DBUS_REQUEST_PREFIX) && dbus_equal(m->interface, "org.freedesktop.portal.Request") && dbus_equal(m->member, "Close"))
        {
                for (long k = 0; k < DBUS_CAST_SESSIONS; k++)
                        if (dbus_cast.session[k].state == DBUS_CAST_PICKING && dbus_cast.session[k].client == from->id &&
                            dbus_equal(dbus_cast.session[k].request, m->path))
                                s = &dbus_cast.session[k];
                if (!s)
                        return 0;
                if (dbus_cast.host && dbus_cast.host->unpick)
                        dbus_cast.host->unpick(dbus_cast.host->context, s->ticket);
                s->state = DBUS_CAST_SELECTED, s->request[0] = 0;
                dbus_reply_empty(from, m, dbus.service.unique);
                return 1;
        }
        if (dbus_starts(m->path, DBUS_SESSION_PREFIX))
        {
                if (!(s = dbus_cast_of(from, m->path)))
                        return 0;
                if (dbus_equal(m->interface, "org.freedesktop.portal.Session") && dbus_equal(m->member, "Close"))
                {
                        dbus_reply_empty(from, m, dbus.service.unique);
                        dbus_cast_free(s);
                        return 1;
                }
                return 0;
        }
        if (!dbus_equal(m->path, DBUS_PATH_PORTAL) || !dbus_equal(m->interface, "org.freedesktop.portal.ScreenCast"))
                return 0;
        if (dbus_equal(m->member, "CreateSession") && dbus_equal(m->signature, "a{sv}"))
        {
                long mine = 0, free_at = -1;

                for (long k = 0; k < DBUS_CAST_SESSIONS; k++)
                {
                        mine += dbus_cast.session[k].state && dbus_cast.session[k].client == from->id;
                        free_at = free_at < 0 && !dbus_cast.session[k].state ? k : free_at;
                }
                if (dbus_option_string(m, 0, "session_handle_token", token, sizeof token) < 0)
                        token[0] = 0;
                if (mine >= DBUS_CAST_PER || free_at < 0 || !dbus_cast.host)
                {
                        dbus_error_reply(from, m, "org.freedesktop.portal.Error.Failed", mine >= DBUS_CAST_PER ? "Too many sessions" : "No screen to share",
                                         dbus.service.unique);
                        return 1;
                }
                s = &dbus_cast.session[free_at];
                dbus_zero(s, sizeof *s);
                dbus_cast_path(from, token, s->path);
                if (dbus_cast_of(from, s->path))
                {
                        // a token used twice: the bus's own instead
                        token[0] = 0;
                        dbus_cast_path(from, token, s->path);
                }
                s->state = DBUS_CAST_MADE, s->client = from->id, s->slot = from->slot, s->ticket = ++dbus_cast.tickets;
                s->types = DBUS_CAST_MONITOR, s->cursor = DBUS_CAST_HIDDEN;
                dbus_cast_reply_request(from, m, 0, path);
                dbus_cast_respond(from, path, 0, DBUS_CAST_HANDLE, s);
                return 1;
        }
        // the other methods name their session first, an object path
        {
                long sig, at = dbus_arg_at(m, 0, &sig);

                if (at < 0 || m->signature[sig] != 'o')
                        return 0;
                at = DBUS_ALIGN(at, 4);
                n = (long)dbus_get32(m->bytes + m->body_at + at);
                raw = m->bytes + m->body_at + at + 4;
                if (n >= (long)sizeof path)
                        return 0;
        }
        dbus_copy(path, raw, n);
        path[n] = 0;
        if (!(s = dbus_cast_of(from, path)))
        {
                dbus_error_reply(from, m, "org.freedesktop.portal.Error.NotFound", "No such session", dbus.service.unique);
                return 1;
        }
        if (dbus_equal(m->member, "SelectSources") && dbus_equal(m->signature, "oa{sv}"))
        {
                if (s->state >= DBUS_CAST_PICKING)
                {
                        dbus_error_reply(from, m, "org.freedesktop.portal.Error.Failed", "The session has started", dbus.service.unique);
                        return 1;
                }
                s->types = dbus_cast_option_u(m, 1, "types", DBUS_CAST_MONITOR) & DBUS_CAST_SOURCES;
                s->types = s->types ? s->types : DBUS_CAST_MONITOR;
                s->cursor = dbus_cast_option_u(m, 1, "cursor_mode", DBUS_CAST_HIDDEN) & DBUS_CAST_CURSORS;
                s->cursor = s->cursor ? s->cursor : DBUS_CAST_HIDDEN;
                s->multiple = (unsigned)dbus_cast_option_b(m, 1, "multiple");
                s->state = DBUS_CAST_SELECTED;
                dbus_cast_reply_request(from, m, 1, path);
                dbus_cast_respond(from, path, 0, DBUS_CAST_NO_RESULTS, 0);
                return 1;
        }
        if (dbus_equal(m->member, "Start") && dbus_equal(m->signature, "osa{sv}"))
        {
                struct dbus_cast_ask ask = {s->ticket, from->pid, from->uid, s->types, s->cursor, s->multiple};

                dbus_cast_reply_request(from, m, 2, path);
                if (s->state != DBUS_CAST_SELECTED || !dbus_cast.host || !dbus_cast.host->pick ||
                    dbus_cast.host->pick(dbus_cast.host->context, &ask) < 0)
                {
                        dbus_cast_respond(from, path, 2, DBUS_CAST_NO_RESULTS, 0);
                        return 1;
                }
                s->state = DBUS_CAST_PICKING;
                dbus_text(s->request, sizeof s->request, path);
                return 1;
        }
        if (dbus_equal(m->member, "OpenPipeWireRemote") && dbus_equal(m->signature, "oa{sv}"))
        {
                struct dbus_out w;
                long body;
                int fd = s->state == DBUS_CAST_STARTED && dbus_cast.host && dbus_cast.host->remote ? dbus_cast.host->remote(dbus_cast.host->context, s->ticket) : -1;

                if (fd < 0 || !from->unix_fds)
                {
                        if (fd >= 0 && dbus_fd_ops.close_fd)
                                dbus_fd_ops.close_fd(fd);
                        dbus_error_reply(from, m, "org.freedesktop.portal.Error.Failed", fd < 0 ? "The session has no stream" : "No file descriptors on this connection",
                                         dbus.service.unique);
                        return 1;
                }
                {
                        struct dbus_fields f = {"", "", "", "", from->unique, dbus.service.unique, "h", m->serial, 1};

                        w.at = dbus.scratch, w.length = 0, w.room = DBUS_MESSAGE, w.bad = 0;
                        dbus_o_header_fds(&w, DBUS_RETURN, DBUS_FLAG_NO_REPLY, dbus_next_serial(), &f, 1);
                        body = w.length;
                        dbus_o_u32(&w, 0);
                        dbus_o_end(&w, body);
                }
                if (w.bad || !dbus_deliver_fds(from, w.at, w.length, DBUS_ASKED_FOR, &fd, 1))
                {
                        if (dbus_fd_ops.close_fd)
                                dbus_fd_ops.close_fd(fd);
                        dbus_error_reply(from, m, "org.freedesktop.portal.Error.Failed", "No room for the stream's socket", dbus.service.unique);
                }
                return 1;
        }
        dbus_error_reply(from, m, "org.freedesktop.DBus.Error.UnknownMethod", "No such method", dbus.service.unique);
        return 1;
}

/*
        The person picked (ok, with the stream: its node, its kind as the portal says it, its place and size) or said no, for the
        share `ticket`: Start's Response to the program that asked, when it still waits.
*/
DBUS_API void dbus_cast_picked(long ticket, int ok, unsigned node, unsigned source_type, int x, int y, unsigned width, unsigned height)
{
        struct dbus_cast_session *s = dbus_cast_ticket(ticket);
        struct dbus_client *c = s ? dbus_client_of(s->slot) : 0;

        if (!s || s->state != DBUS_CAST_PICKING || !c || c->id != s->client)
                return;
        if (ok)
        {
                s->node = node, s->source_type = source_type, s->x = x, s->y = y, s->width = width, s->height = height;
                s->state = DBUS_CAST_STARTED;
                dbus_cast_respond(c, s->request, 0, DBUS_CAST_STREAMS, s);
        }
        else
        {
                dbus_cast_respond(c, s->request, 1, DBUS_CAST_NO_RESULTS, 0);
                s->state = DBUS_CAST_SELECTED;
                if (dbus_cast.host && dbus_cast.host->unpick)
                        dbus_cast.host->unpick(dbus_cast.host->context, s->ticket);
        }
        s->request[0] = 0;
}

// The share `ticket` ended by the person or the desktop (the tray's Stop, its window gone): Closed to its program, the session let go.
DBUS_API void dbus_cast_ended(long ticket)
{
        struct dbus_cast_session *s = dbus_cast_ticket(ticket);
        struct dbus_client *c = s ? dbus_client_of(s->slot) : 0;

        if (!s)
                return;
        if (c && c->id == s->client)
        {
                if (s->state == DBUS_CAST_PICKING)
                        dbus_cast_respond(c, s->request, 1, DBUS_CAST_NO_RESULTS, 0);
                dbus_cast_closed(c, s->path);
        }
        dbus_cast_free(s);
}

// The sessions of programs that are gone, let go (and their shares with them). Called by the host now and then.
DBUS_API void dbus_cast_sweep(void)
{
        for (long k = 0; k < DBUS_CAST_SESSIONS; k++)
        {
                struct dbus_cast_session *s = &dbus_cast.session[k];
                struct dbus_client *c = s->state ? dbus_client_of(s->slot) : 0;

                if (s->state && (!c || c->id != s->client))
                        dbus_cast_free(s);
        }
}
