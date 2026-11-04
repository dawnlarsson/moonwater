/*
        Moonwater's session bus: the D-Bus message bus a program of a bowl finds at /run/moonwater/bus, with the one service
        a desktop's programs ask of it for how the desktop looks, org.freedesktop.portal.Desktop's Settings.

        WHY

        A browser, a GTK 4 program and a Qt 6 program are told that the desktop is dark, or what its accent colour is, over
        the session bus: they read org.freedesktop.appearance's color-scheme and accent-color from the Settings portal, and
        listen for its SettingChanged signal to change as the setting does. A bowl has no bus, so a browser logs thirty
        lines of errors at every start, never changes colour, and a change in Settings reaches Canvas's own windows only.
        This is the smallest bus those programs can talk to: not a general one. It speaks the wire (the SASL
        authentication of a peer by its uid, Hello, and the bus's own methods), routes calls, replies and signals between
        the programs that connect (a program may own names and be called, as a media player is), and serves the
        Settings from a table the host fills from Moonwater's settings and keeps current.

        WHAT IT IS NOT

        There is no activation, no policy files, no monitoring, no eavesdropping, no file descriptors passed (the
        negotiation is refused, as a bus may), no byte order but the machine's (a message in the other one drops the
        client), no queue of owners of a name. A call to a name nobody owns is an error, as it is on a bus with no
        services installed.

        THREAT MODEL

        Everything a connection sends is hostile: the authentication lines, every header field, every length and the
        body, and the order and number of calls. A connection is any program a person runs, in a bowl or not. So:
        1. Fixed tables only: a client's record is one block the host maps when it connects, and nothing a client says
           is the size of anything.
        2. A message is read whole and validated whole (the header's fields by type, every name, path and signature by
           its grammar, the body by its signature down to the padding and the UTF-8 of each string) before anything of it
           is acted on or passed on; a message that is not valid is the end of that connection.
        3. The sender of a message is the bus's to say: it is stamped on what is passed on and what a client wrote there
           is not kept. A reply is passed only to the caller it answers, from the callee it was asked of.
        4. A client that does not read never blocks the bus: its output queue is bounded, and a client that fills it is
           dropped. A connection that has not said Hello in a few seconds is dropped.
        5. A name is a client's only while it is connected; the bus's own names, and the service's, are not for taking.

        It is freestanding C with no allocator, in ring 3, the same way the Wayland shim is, with the host behind a small
        seam (struct dbus_host) so that the checks can put a fake one behind it.
*/
#ifndef DBUS_INCLUDED
#define DBUS_INCLUDED

#define DBUS_API static __attribute__((unused))

#define DBUS_CLIENTS 32
#define DBUS_PER_UID 12          // connections of one uid, root's as many as the table has room for
#define DBUS_MESSAGE 262144      // the most a message may be, header and body
#define DBUS_OUT 262144          // a client's output queue
#define DBUS_NAMES 128           // well-known names, all clients together
#define DBUS_NAMES_PER 8
#define DBUS_RULES 64            // match rules one client holds
#define DBUS_PENDING 512         // method calls waiting for a reply, all clients together
#define DBUS_PENDING_PER 64
#define DBUS_SETTINGS 64
#define DBUS_NAME_TEXT 128       // the longest well-known name kept (the protocol's 255 are refused)
#define DBUS_DEPTH 32            // how deep the containers of a value nest
#define DBUS_AUTH_LINE 512
#define DBUS_AUTH_TOTAL 4096
#define DBUS_HELLO_MS 5000       // how long a connection has to authenticate and say Hello
#define DBUS_SERVICE DBUS_CLIENTS // the slot of the portal service, which has no socket

#define DBUS_CALL 1
#define DBUS_RETURN 2
#define DBUS_ERROR 3
#define DBUS_SIGNAL 4
#define DBUS_FLAG_NO_REPLY 1
#define DBUS_FLAG_NO_START 2

#define DBUS_NAME_BUS "org.freedesktop.DBus"
#define DBUS_NAME_PORTAL "org.freedesktop.portal.Desktop"
#define DBUS_PATH_PORTAL "/org/freedesktop/portal/desktop"

/* What the bus asks of its host. now is milliseconds on a clock that only goes forward; random fills bytes with random ones. */
struct dbus_host
{
        void *context;
        void *(*alloc)(void *context, long bytes);
        void (*release)(void *context, void *block, long bytes);
        long (*now)(void *context);
        void (*random)(void *context, unsigned char *bytes, long count);
        void (*trace)(void *context, const char *line); // optional: one line for each message the bus routes, to read what a program asks
};

/* A match rule, as AddMatch gave it. */
struct dbus_rule
{
        unsigned char used, type;       // type 0 for any, else DBUS_CALL..DBUS_SIGNAL
        unsigned char has;              // which of the strings below were given
        char sender[DBUS_NAME_TEXT], interface[DBUS_NAME_TEXT], member[64], path[128], path_namespace[128], destination[DBUS_NAME_TEXT];
        char arg[4][96];
        unsigned char arg_has, arg_mode[4]; // arg_mode: 0 the argument is the string, 1 a path of it (argNpath), 2 a name of its namespace (arg0namespace)
};

struct dbus_client
{
        long live, dead, slot, pid, uid, state; // state: 0 awaits the first byte, 1 the authentication, 2 messages
        const char *why;                        // what ended it, for the log
        long in_length, out_length, auth_total, born;
        long id;                                // the unique name is :1.id
        unsigned char hello, uid_ok, no_fds, want_data; // want_data: EXTERNAL was named with no data, and DATA is asked for
        long names, pending;
        char unique[16];
        struct dbus_rule rule[DBUS_RULES];
        unsigned char in[DBUS_MESSAGE + 16];
        unsigned char out[DBUS_OUT];
};

struct dbus_setting
{
        char ns[48], key[40];
        unsigned char used, kind;       // kind: u, i, b, d (bits[0]), s (text), c (a colour: three doubles in bits)
        long number;
        unsigned long bits[3];
        char text[64];
};

struct dbus_name
{
        char text[DBUS_NAME_TEXT];
        short owner;                    // a client's slot, DBUS_SERVICE for the service, -1 for none
        unsigned char used, replaceable;
};

struct dbus_pending
{
        unsigned long serial;           // the caller's serial
        short caller, callee;
        unsigned char used;
};

static struct
{
        const struct dbus_host *host;
        struct dbus_client *client[DBUS_CLIENTS];
        struct dbus_client service;     // the portal's record: no socket, a unique name and a name, its calls answered in here
        struct dbus_name name[DBUS_NAMES];
        struct dbus_pending pending[DBUS_PENDING];
        struct dbus_setting setting[DBUS_SETTINGS];
        long next_id, clients;
        unsigned long serial;
        char guid[33];
        unsigned char scratch[DBUS_MESSAGE + 4096];
        unsigned char stamped[DBUS_MESSAGE + 4096];
} dbus;

/* ---- small things (no library) ---- */

static long dbus_length(const char *s)
{
        long n = 0;

        while (s[n])
                n++;
        return n;
}

static int dbus_equal(const char *a, const char *b)
{
        long n = 0;

        while (a[n] && a[n] == b[n])
                n++;
        return a[n] == b[n];
}

static int dbus_starts(const char *text, const char *prefix)
{
        long n = 0;

        while (prefix[n] && text[n] == prefix[n])
                n++;
        return !prefix[n];
}

static void dbus_copy(void *to, const void *from, long n)
{
        unsigned char *d = (unsigned char *)to;
        const unsigned char *s = (const unsigned char *)from;

        for (long at = 0; at < n; at++)
                d[at] = s[at];
}

static void dbus_text(char *to, long room, const char *from)
{
        long at = 0;

        while (from[at] && at + 1 < room)
        {
                to[at] = from[at];
                at++;
        }
        to[at] = 0;
}

static unsigned long dbus_get32(const unsigned char *p)
{
        return (unsigned long)p[0] | (unsigned long)p[1] << 8 | (unsigned long)p[2] << 16 | (unsigned long)p[3] << 24;
}

#define DBUS_ALIGN(at, n) (((at) + (n)-1) & ~((long)(n)-1))

/* ---- grammar: UTF-8, names, paths, signatures ---- */

// One valid UTF-8 text with no NUL: the shortest forms, no surrogates, nothing past U+10FFFF.
static int dbus_utf8(const unsigned char *s, long n)
{
        for (long at = 0; at < n;)
        {
                unsigned char c = s[at];
                long more = c < 0x80 ? 0 : c >= 0xc2 && c < 0xe0 ? 1 : c >= 0xe0 && c < 0xf0 ? 2 : c >= 0xf0 && c < 0xf5 ? 3 : -1;
                unsigned long code;

                if (!c || more < 0 || at + more >= n + (more == 0))
                        return 0;
                if (!more)
                {
                        at++;
                        continue;
                }
                code = more == 1 ? c & 0x1fu : more == 2 ? c & 0x0fu : c & 0x07u;
                for (long k = 1; k <= more; k++)
                {
                        if ((s[at + k] & 0xc0) != 0x80)
                                return 0;
                        code = code << 6 | (s[at + k] & 0x3fu);
                }
                if ((more == 2 && (code < 0x800 || (code >= 0xd800 && code < 0xe000))) || (more == 3 && (code < 0x10000 || code > 0x10ffff)))
                        return 0;
                at += more + 1;
        }
        return 1;
}

static int dbus_letter(char c)
{
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static int dbus_digit(char c)
{
        return c >= '0' && c <= '9';
}

// An object path: / or elements of letters, digits and underscores after slashes, none empty, no slash at the end.
static int dbus_valid_path(const char *s, long n)
{
        if (n < 1 || n > 255 || s[0] != '/')
                return 0;
        if (n == 1)
                return 1;
        for (long at = 1, start = 1; at <= n; at++)
        {
                if (at == n || s[at] == '/')
                {
                        if (at == start)
                                return 0;
                        start = at + 1;
                }
                else if (!dbus_letter(s[at]) && !dbus_digit(s[at]))
                        return 0;
        }
        return s[n - 1] != '/';
}

// kind 0: an interface or error name (two elements at least; letters, digits and underscores, none leading a digit);
// 1: a bus name, well known (elements may have a dash) or unique (a colon first, elements may lead with a digit);
// 2: a member name.
static int dbus_valid_name(const char *s, long n, int kind)
{
        long dots = 0, start = 0;
        int unique = kind == 1 && n > 0 && s[0] == ':';

        if (n < 1 || n > 255)
                return 0;
        if (kind == 2)
        {
                if (!dbus_letter(s[0]))
                        return 0;
                for (long at = 1; at < n; at++)
                        if (!dbus_letter(s[at]) && !dbus_digit(s[at]))
                                return 0;
                return 1;
        }
        if (unique)
                start = 1;
        for (long at = start; at <= n; at++)
        {
                if (at == n || s[at] == '.')
                {
                        if (at == start)
                                return 0;
                        start = at + 1;
                        dots += at < n;
                }
                else
                {
                        int first = at == start;
                        int dash = kind == 1 && s[at] == '-';

                        if (!dbus_letter(s[at]) && !dash && !(dbus_digit(s[at]) && (!first || unique)))
                                return 0;
                }
        }
        return dots >= 1 && s[n - 1] != '.';
}

// One complete type of a signature, from *at; the depth of arrays and of structs counted.
static int dbus_sig_one(const char *s, long n, long *at, int arrays, int structs)
{
        char c;

        if (*at >= n || arrays > 32 || structs > 32)
                return 0;
        c = s[(*at)++];
        switch (c)
        {
        case 'y': case 'b': case 'n': case 'q': case 'i': case 'u': case 'x': case 't': case 'd': case 's': case 'o': case 'g':
        case 'h': case 'v':
                return 1;
        case 'a':
                if (*at < n && s[*at] == '{')
                {
                        (*at)++;
                        // a dict entry: a basic key and any value
                        if (*at >= n || !(s[*at] == 'y' || s[*at] == 'b' || s[*at] == 'n' || s[*at] == 'q' || s[*at] == 'i' || s[*at] == 'u' ||
                                          s[*at] == 'x' || s[*at] == 't' || s[*at] == 'd' || s[*at] == 's' || s[*at] == 'o' || s[*at] == 'g' ||
                                          s[*at] == 'h'))
                                return 0;
                        (*at)++;
                        if (!dbus_sig_one(s, n, at, arrays + 1, structs + 1) || *at >= n || s[(*at)++] != '}')
                                return 0;
                        return 1;
                }
                return dbus_sig_one(s, n, at, arrays + 1, structs);
        case '(':
        {
                long members = 0;

                while (*at < n && s[*at] != ')')
                {
                        if (!dbus_sig_one(s, n, at, arrays, structs + 1))
                                return 0;
                        members++;
                }
                if (*at >= n || !members)
                        return 0;
                (*at)++;
                return 1;
        }
        default:
                return 0;
        }
}

// A signature: complete types, up to 255 characters.
static int dbus_valid_signature(const char *s, long n)
{
        long at = 0;

        if (n > 255)
                return 0;
        while (at < n)
                if (!dbus_sig_one(s, n, &at, 0, 0))
                        return 0;
        return 1;
}

/* ---- a body, valid down to its padding ---- */

static int dbus_value(const unsigned char *p, long *at, long limit, const char *sig, long sig_length, long *s, int depth);

static long dbus_fixed_size(char c)
{
        return c == 'y' ? 1 : c == 'n' || c == 'q' ? 2 : c == 'b' || c == 'i' || c == 'u' || c == 'h' ? 4 : c == 'x' || c == 't' || c == 'd' ? 8 : 0;
}

static int dbus_pad_zero(const unsigned char *p, long *at, long limit, long align)
{
        long to = DBUS_ALIGN(*at, align);

        if (to > limit)
                return 0;
        for (long k = *at; k < to; k++)
                if (p[k])
                        return 0;
        *at = to;
        return 1;
}

// One value of the type at sig[*s] from p[*at] on, within limit; both cursors move past it. 1 when it is whole and valid.
static int dbus_value(const unsigned char *p, long *at, long limit, const char *sig, long sig_length, long *s, int depth)
{
        char c = sig[(*s)++];
        long size = dbus_fixed_size(c), length;

        if (depth > DBUS_DEPTH)
                return 0;
        if (size)
        {
                if (!dbus_pad_zero(p, at, limit, size) || *at + size > limit)
                        return 0;
                if (c == 'b' && dbus_get32(p + *at) > 1)
                        return 0;
                *at += size;
                return 1;
        }
        switch (c)
        {
        case 's':
        case 'o':
                if (!dbus_pad_zero(p, at, limit, 4) || *at + 4 > limit)
                        return 0;
                length = (long)dbus_get32(p + *at);
                *at += 4;
                if (length < 0 || length > limit - *at - 1 || p[*at + length] || !dbus_utf8(p + *at, length) ||
                    (c == 'o' && !dbus_valid_path((const char *)p + *at, length)))
                        return 0;
                *at += length + 1;
                return 1;
        case 'g':
                if (*at + 1 > limit)
                        return 0;
                length = p[(*at)++];
                if (length > limit - *at - 1 || p[*at + length] || !dbus_valid_signature((const char *)p + *at, length))
                        return 0;
                *at += length + 1;
                return 1;
        case 'v':
        {
                long inner = 0, start;

                if (*at + 1 > limit)
                        return 0;
                length = p[(*at)++];
                start = *at;
                if (length > limit - *at - 1 || p[*at + length] || !dbus_valid_signature((const char *)p + *at, length))
                        return 0;
                *at += length + 1;
                if (!dbus_sig_one((const char *)p + start, length, &inner, 0, 0) || inner != length)
                        return 0;
                inner = 0;
                return dbus_value(p, at, limit, (const char *)p + start, length, &inner, depth + 1);
        }
        case 'a':
        {
                long element = *s, after, stop, align;
                char first;

                if (!dbus_pad_zero(p, at, limit, 4) || *at + 4 > limit)
                        return 0;
                length = (long)dbus_get32(p + *at);
                *at += 4;
                first = sig[element];
                align = first == '{' || first == '(' ? 8 : dbus_fixed_size(first) ? dbus_fixed_size(first) : first == 's' || first == 'o' || first == 'a' ? 4 : 1;
                if (!dbus_pad_zero(p, at, limit, align) || length < 0 || length > 67108864 || length > limit - *at)
                        return 0;
                stop = *at + length;
                after = element - 1; // the array's own type, from its a, for where the signature goes on
                if (!dbus_sig_one(sig, sig_length, &after, 0, 0))
                        return 0;
                while (*at < stop)
                {
                        long each = element;

                        if (first == '{')
                        {
                                if (!dbus_pad_zero(p, at, stop, 8))
                                        return 0;
                                each++;
                                if (!dbus_value(p, at, stop, sig, sig_length, &each, depth + 1) ||
                                    !dbus_value(p, at, stop, sig, sig_length, &each, depth + 1))
                                        return 0;
                        }
                        else if (!dbus_value(p, at, stop, sig, sig_length, &each, depth + 1))
                                return 0;
                }
                if (*at != stop)
                        return 0;
                *s = after;
                return 1;
        }
        case '(':
                if (!dbus_pad_zero(p, at, limit, 8))
                        return 0;
                while (*s < sig_length && sig[*s] != ')')
                        if (!dbus_value(p, at, limit, sig, sig_length, s, depth + 1))
                                return 0;
                (*s)++;
                return 1;
        default:
                return 0;
        }
}

static int dbus_body_valid(const unsigned char *body, long length, const char *sig)
{
        long at = 0, s = 0, n = dbus_length(sig);

        while (s < n)
                if (!dbus_value(body, &at, length, sig, n, &s, 0))
                        return 0;
        return at == length;
}

/* ---- a message, parsed ---- */

struct dbus_message
{
        const unsigned char *bytes;
        long size, body_at, body_length;
        unsigned char type, flags;
        unsigned long serial, reply;
        const char *path, *interface, *member, *error, *destination, *sender, *signature;
        unsigned has;
};

#define DBUS_HAS_PATH 1u
#define DBUS_HAS_INTERFACE 2u
#define DBUS_HAS_MEMBER 4u
#define DBUS_HAS_ERROR 8u
#define DBUS_HAS_REPLY 16u
#define DBUS_HAS_DESTINATION 32u
#define DBUS_HAS_SENDER 64u
#define DBUS_HAS_SIGNATURE 128u
#define DBUS_HAS_FDS 256u

// 0 when the bytes are one whole message, valid; the message read into m.
static int dbus_parse(const unsigned char *b, long size, struct dbus_message *m)
{
        long fields, limit, at = 16, body, pos;
        unsigned seen = 0;

        m->has = 0;
        m->path = m->interface = m->member = m->error = m->destination = m->sender = m->signature = "";
        m->reply = 0;
        if (size < 16 || size > DBUS_MESSAGE || b[0] != 'l' || b[3] != 1 || b[1] < DBUS_CALL || b[1] > DBUS_SIGNAL)
                return -1;
        m->bytes = b;
        m->size = size;
        m->type = b[1];
        m->flags = b[2];
        m->serial = dbus_get32(b + 8);
        body = (long)dbus_get32(b + 4);
        fields = (long)dbus_get32(b + 12);
        if (!m->serial || fields < 0 || fields > size - 16 || body < 0 || body > size)
                return -1;
        limit = 16 + fields;
        pos = DBUS_ALIGN(limit, 8);
        if (pos + body != size)
                return -1;
        for (long k = limit; k < pos; k++)
                if (b[k])
                        return -1;
        while (at < limit)
        {
                unsigned char code;
                char kind;
                long length;

                for (long k = at; k < DBUS_ALIGN(at, 8); k++)
                        if (k >= limit || b[k])
                                return -1;
                at = DBUS_ALIGN(at, 8);
                if (at + 4 > limit)
                        return -1;
                code = b[at];
                if (b[at + 1] != 1 || b[at + 3])
                        return -1;
                kind = (char)b[at + 2];
                at += 4;
                if (code >= 1 && code <= 9 && (seen & (1u << code)))
                        return -1;
                if (kind == 'u')
                {
                        for (long k = at; k < DBUS_ALIGN(at, 4); k++)
                                if (k >= limit || b[k])
                                        return -1;
                        at = DBUS_ALIGN(at, 4);
                        if (at + 4 > limit)
                                return -1;
                        if (code == 5)
                        {
                                m->reply = dbus_get32(b + at);
                                m->has |= DBUS_HAS_REPLY;
                        }
                        else if (code == 9)
                        {
                                // GDBus says 0 descriptors on every call; a message with any is one this bus cannot take (none are passed)
                                if (dbus_get32(b + at))
                                        return -1;
                                m->has |= DBUS_HAS_FDS;
                        }
                        at += 4;
                }
                else if (kind == 's' || kind == 'o')
                {
                        for (long k = at; k < DBUS_ALIGN(at, 4); k++)
                                if (k >= limit || b[k])
                                        return -1;
                        at = DBUS_ALIGN(at, 4);
                        if (at + 4 > limit)
                                return -1;
                        length = (long)dbus_get32(b + at);
                        at += 4;
                        if (length < 0 || length > limit - at - 1 || b[at + length] || !dbus_utf8(b + at, length))
                                return -1;
                        if (code == 1 && kind == 'o' && dbus_valid_path((const char *)b + at, length))
                                m->path = (const char *)b + at, m->has |= DBUS_HAS_PATH;
                        else if (code == 2 && kind == 's' && dbus_valid_name((const char *)b + at, length, 0))
                                m->interface = (const char *)b + at, m->has |= DBUS_HAS_INTERFACE;
                        else if (code == 3 && kind == 's' && dbus_valid_name((const char *)b + at, length, 2))
                                m->member = (const char *)b + at, m->has |= DBUS_HAS_MEMBER;
                        else if (code == 4 && kind == 's' && dbus_valid_name((const char *)b + at, length, 0))
                                m->error = (const char *)b + at, m->has |= DBUS_HAS_ERROR;
                        else if (code == 6 && kind == 's' && dbus_valid_name((const char *)b + at, length, 1))
                                m->destination = (const char *)b + at, m->has |= DBUS_HAS_DESTINATION;
                        else if (code == 7 && kind == 's' && dbus_valid_name((const char *)b + at, length, 1))
                                m->sender = (const char *)b + at, m->has |= DBUS_HAS_SENDER;
                        else if (code >= 1 && code <= 7 && code != 5)
                                return -1;
                        at += length + 1;
                }
                else if (kind == 'g')
                {
                        if (at + 1 > limit)
                                return -1;
                        length = b[at++];
                        if (length > limit - at - 1 || b[at + length] || !dbus_valid_signature((const char *)b + at, length))
                                return -1;
                        if (code == 8)
                                m->signature = (const char *)b + at, m->has |= DBUS_HAS_SIGNATURE;
                        at += length + 1;
                }
                else
                        return -1;
                if (code >= 1 && code <= 9)
                {
                        // a field of the wrong type for its code
                        if ((code == 1 && kind != 'o') || (code >= 2 && code <= 4 && kind != 's') || (code == 5 && kind != 'u') ||
                            (code >= 6 && code <= 7 && kind != 's') || (code == 8 && kind != 'g') || (code == 9 && kind != 'u'))
                                return -1;
                        seen |= 1u << code;
                }
        }
        if (at != limit)
                return -1;
        if ((m->type == DBUS_CALL && (!(m->has & DBUS_HAS_PATH) || !(m->has & DBUS_HAS_MEMBER))) ||
            (m->type == DBUS_SIGNAL && (!(m->has & DBUS_HAS_PATH) || !(m->has & DBUS_HAS_INTERFACE) || !(m->has & DBUS_HAS_MEMBER))) ||
            (m->type == DBUS_RETURN && !(m->has & DBUS_HAS_REPLY)) ||
            (m->type == DBUS_ERROR && (!(m->has & DBUS_HAS_REPLY) || !(m->has & DBUS_HAS_ERROR))))
                return -1;
        m->body_at = pos;
        m->body_length = body;
        if (body > 0 && !*m->signature)
                return -1;
        if (!dbus_body_valid(b + pos, body, m->signature))
                return -1;
        return 0;
}

/* ---- a message, written ---- */

struct dbus_out
{
        unsigned char *at;
        long length, room;
        int bad;
};

static void dbus_o_pad(struct dbus_out *w, long align)
{
        while (w->length % align)
        {
                if (w->length >= w->room)
                {
                        w->bad = 1;
                        return;
                }
                w->at[w->length++] = 0;
        }
}

static void dbus_o_raw(struct dbus_out *w, const void *bytes, long n)
{
        if (n < 0 || w->length + n > w->room)
        {
                w->bad = 1;
                return;
        }
        dbus_copy(w->at + w->length, bytes, n);
        w->length += n;
}

static void dbus_o_u8(struct dbus_out *w, unsigned v)
{
        unsigned char c = (unsigned char)v;

        dbus_o_raw(w, &c, 1);
}

static void dbus_o_u32(struct dbus_out *w, unsigned long v)
{
        unsigned char c[4] = {(unsigned char)v, (unsigned char)(v >> 8), (unsigned char)(v >> 16), (unsigned char)(v >> 24)};

        dbus_o_pad(w, 4);
        dbus_o_raw(w, c, 4);
}

static void dbus_o_u64(struct dbus_out *w, unsigned long v)
{
        unsigned char c[8];

        for (int k = 0; k < 8; k++)
                c[k] = (unsigned char)(v >> (8 * k));
        dbus_o_pad(w, 8);
        dbus_o_raw(w, c, 8);
}

static void dbus_o_string(struct dbus_out *w, const char *s)
{
        long n = dbus_length(s);

        dbus_o_u32(w, (unsigned long)n);
        dbus_o_raw(w, s, n + 1);
}

static void dbus_o_sig(struct dbus_out *w, const char *s)
{
        long n = dbus_length(s);

        dbus_o_u8(w, (unsigned)n);
        dbus_o_raw(w, s, n + 1);
}

static void dbus_o_patch32(struct dbus_out *w, long at, unsigned long v)
{
        if (at + 4 > w->length)
                return;
        for (int k = 0; k < 4; k++)
                w->at[at + k] = (unsigned char)(v >> (8 * k));
}

struct dbus_fields
{
        const char *path, *interface, *member, *error, *destination, *sender, *signature;
        unsigned long reply;
        int has_reply;
};

static void dbus_o_field_string(struct dbus_out *w, unsigned code, char kind, const char *value)
{
        dbus_o_pad(w, 8);
        dbus_o_u8(w, code);
        dbus_o_u8(w, 1);
        dbus_o_u8(w, (unsigned char)kind);
        dbus_o_u8(w, 0);
        if (kind == 'g')
        {
                // the variant's signature is g and the value a signature: byte count, text, NUL (1 byte aligned)
                dbus_o_sig(w, value);
                return;
        }
        dbus_o_string(w, value);
}

// A message's header; the body follows, then dbus_o_end. Returns where the body's alignment starts counting (its start).
static void dbus_o_header(struct dbus_out *w, unsigned type, unsigned flags, unsigned long serial, const struct dbus_fields *f)
{
        long fields_at;

        dbus_o_u8(w, 'l');
        dbus_o_u8(w, type);
        dbus_o_u8(w, flags);
        dbus_o_u8(w, 1);
        dbus_o_u32(w, 0);
        dbus_o_u32(w, serial);
        dbus_o_u32(w, 0);
        fields_at = w->length;
        if (f->path && *f->path)
                dbus_o_field_string(w, 1, 'o', f->path);
        if (f->interface && *f->interface)
                dbus_o_field_string(w, 2, 's', f->interface);
        if (f->member && *f->member)
                dbus_o_field_string(w, 3, 's', f->member);
        if (f->error && *f->error)
                dbus_o_field_string(w, 4, 's', f->error);
        if (f->has_reply)
        {
                dbus_o_pad(w, 8);
                dbus_o_u8(w, 5);
                dbus_o_u8(w, 1);
                dbus_o_u8(w, 'u');
                dbus_o_u8(w, 0);
                dbus_o_u32(w, f->reply);
        }
        if (f->destination && *f->destination)
                dbus_o_field_string(w, 6, 's', f->destination);
        if (f->sender && *f->sender)
                dbus_o_field_string(w, 7, 's', f->sender);
        if (f->signature && *f->signature)
                dbus_o_field_string(w, 8, 'g', f->signature);
        dbus_o_patch32(w, 12, (unsigned long)(w->length - fields_at));
        dbus_o_pad(w, 8);
}

static void dbus_o_end(struct dbus_out *w, long body_at)
{
        dbus_o_patch32(w, 4, (unsigned long)(w->length - body_at));
}

/* ---- the clients, the names, the pending calls ---- */

static struct dbus_client *dbus_client_of(long slot)
{
        return slot == DBUS_SERVICE ? &dbus.service : slot >= 0 && slot < DBUS_CLIENTS ? dbus.client[slot] : 0;
}

// The slot a unique name (:1.N) is, or -1.
static long dbus_unique_slot(const char *name)
{
        if (name[0] != ':' || name[1] != '1' || name[2] != '.')
                return -1;
        if (dbus_equal(name, dbus.service.unique))
                return DBUS_SERVICE;
        for (long at = 0; at < DBUS_CLIENTS; at++)
                if (dbus.client[at] && dbus.client[at]->hello && dbus_equal(name, dbus.client[at]->unique))
                        return at;
        return -1;
}

static struct dbus_name *dbus_name_find(const char *name)
{
        for (long at = 0; at < DBUS_NAMES; at++)
                if (dbus.name[at].used && dbus_equal(dbus.name[at].text, name))
                        return &dbus.name[at];
        return 0;
}

// The slot that owns a name (unique or well known), or -1.
static long dbus_resolve(const char *name)
{
        struct dbus_name *n;

        if (name[0] == ':')
                return dbus_unique_slot(name);
        n = dbus_name_find(name);
        return n ? n->owner : -1;
}

static void dbus_unique_text(char *into, long id)
{
        char digits[12];
        long n = 0, at = 3;

        into[0] = ':';
        into[1] = '1';
        into[2] = '.';
        do
                digits[n++] = (char)('0' + id % 10);
        while ((id /= 10) > 0);
        while (n)
                into[at++] = digits[--n];
        into[at] = 0;
}

// Queued to a client; a client with no room for it is dropped.
static void dbus_deliver(struct dbus_client *c, const unsigned char *bytes, long n)
{
        if (!c || c->slot == DBUS_SERVICE || c->dead)
                return;
        if (c->out_length + n > DBUS_OUT)
        {
                c->dead = 2;
                c->why = "it did not read what it was sent";
                return;
        }
        dbus_copy(c->out + c->out_length, bytes, n);
        c->out_length += n;
}

static unsigned long dbus_next_serial(void)
{
        dbus.serial++;
        if (!dbus.serial || dbus.serial > 0x7fffffffUL)
                dbus.serial = 1;
        return dbus.serial;
}

/* ---- match rules ---- */

// The string argument number index of a message's body, when every argument before it is a basic one; its length, or -1.
static long dbus_arg_string(const struct dbus_message *m, long index, char *into, long room)
{
        const unsigned char *body = m->bytes + m->body_at;
        long at = 0, limit = m->body_length;
        const char *sig = m->signature;

        for (long k = 0; sig[k] && k <= index; k++)
        {
                char c = sig[k];
                long size = dbus_fixed_size(c), length;

                if (size)
                {
                        at = DBUS_ALIGN(at, size) + size;
                        continue;
                }
                if (c == 's' || c == 'o')
                {
                        at = DBUS_ALIGN(at, 4);
                        if (at + 4 > limit)
                                return -1;
                        length = (long)dbus_get32(body + at);
                        at += 4;
                        if (length < 0 || at + length + 1 > limit)
                                return -1;
                        if (k == index)
                        {
                                if (length + 1 > room)
                                        return -1;
                                dbus_copy(into, body + at, length);
                                into[length] = 0;
                                return length;
                        }
                        at += length + 1;
                        continue;
                }
                if (c == 'g')
                {
                        length = body[at];
                        at += 1 + length + 1;
                        continue;
                }
                return -1;
        }
        return -1;
}

static int dbus_rule_sender_is(const char *wanted, long from)
{
        struct dbus_client *c = dbus_client_of(from);
        struct dbus_name *n;

        if (from < 0 || !c)
                return dbus_equal(wanted, DBUS_NAME_BUS);
        if (wanted[0] == ':')
                return dbus_equal(wanted, c->unique);
        n = dbus_name_find(wanted);
        return n && n->owner == from;
}

// Whether a message (sent by `from`, -1 for the bus) is what a rule asks for.
static int dbus_rule_matches(const struct dbus_rule *r, long from, const struct dbus_message *m)
{
        if (r->type && r->type != m->type)
                return 0;
        if ((r->has & 1) && !dbus_rule_sender_is(r->sender, from))
                return 0;
        if ((r->has & 2) && !dbus_equal(r->interface, m->interface))
                return 0;
        if ((r->has & 4) && !dbus_equal(r->member, m->member))
                return 0;
        if ((r->has & 8) && !dbus_equal(r->path, m->path))
                return 0;
        if (r->has & 16)
        {
                long n = dbus_length(r->path_namespace);

                if (!dbus_starts(m->path, r->path_namespace) || (m->path[n] && m->path[n] != '/' && n > 1))
                        return 0;
        }
        if ((r->has & 32) && !dbus_equal(r->destination, m->destination))
                return 0;
        for (long k = 0; k < 4; k++)
                if (r->arg_has & (1u << k))
                {
                        char value[96];
                        long want = dbus_length(r->arg[k]), got = dbus_arg_string(m, k, value, sizeof value);

                        if (got < 0)
                                return 0;
                        if (r->arg_mode[k] == 1)
                        {
                                // a path rule: the argument is the path, or one under it (the rule ends in a slash), or one it is under
                                if (!(dbus_equal(value, r->arg[k]) || (want && r->arg[k][want - 1] == '/' && dbus_starts(value, r->arg[k])) ||
                                      (got && value[got - 1] == '/' && dbus_starts(r->arg[k], value))))
                                        return 0;
                        }
                        else if (r->arg_mode[k] == 2)
                        {
                                // a namespace rule: the argument is the name or one under it
                                if (!(dbus_equal(value, r->arg[k]) || (dbus_starts(value, r->arg[k]) && value[want] == '.')))
                                        return 0;
                        }
                        else if (!dbus_equal(value, r->arg[k]))
                                return 0;
                }
        return 1;
}

// A rule in the text AddMatch gives: key='value',key=value. 0 when it is one this bus takes.
static int dbus_rule_parse(const char *text, struct dbus_rule *r)
{
        long at = 0, n = dbus_length(text);

        for (unsigned long k = 0; k < sizeof *r; k++)
                ((unsigned char *)r)[k] = 0;
        while (at < n)
        {
                char key[24], value[160];
                long klen = 0, vlen = 0;

                while (at < n && text[at] == ' ')
                        at++;
                while (at < n && text[at] != '=' && klen < 23)
                        key[klen++] = text[at++];
                key[klen] = 0;
                if (at >= n || text[at] != '=' || !klen)
                        return -1;
                at++;
                while (at < n && text[at] != ',')
                {
                        if (text[at] == '\'')
                        {
                                at++;
                                while (at < n && text[at] != '\'')
                                {
                                        if (vlen >= 159)
                                                return -1;
                                        value[vlen++] = text[at++];
                                }
                                if (at >= n)
                                        return -1;
                                at++;
                        }
                        else if (text[at] == '\\' && at + 1 < n && text[at + 1] == '\'')
                        {
                                if (vlen >= 159)
                                        return -1;
                                value[vlen++] = '\'';
                                at += 2;
                        }
                        else
                        {
                                if (vlen >= 159)
                                        return -1;
                                value[vlen++] = text[at++];
                        }
                }
                value[vlen] = 0;
                if (at < n)
                        at++;
                if (dbus_equal(key, "type"))
                {
                        r->type = dbus_equal(value, "signal") ? DBUS_SIGNAL : dbus_equal(value, "method_call") ? DBUS_CALL
                                  : dbus_equal(value, "method_return") ? DBUS_RETURN : dbus_equal(value, "error") ? DBUS_ERROR : 0;
                        if (!r->type)
                                return -1;
                }
                else if (dbus_equal(key, "sender") && vlen < DBUS_NAME_TEXT)
                        dbus_text(r->sender, sizeof r->sender, value), r->has |= 1;
                else if (dbus_equal(key, "interface") && vlen < DBUS_NAME_TEXT)
                        dbus_text(r->interface, sizeof r->interface, value), r->has |= 2;
                else if (dbus_equal(key, "member") && vlen < 64)
                        dbus_text(r->member, sizeof r->member, value), r->has |= 4;
                else if (dbus_equal(key, "path") && vlen < 128)
                        dbus_text(r->path, sizeof r->path, value), r->has |= 8;
                else if (dbus_equal(key, "path_namespace") && vlen < 128)
                        dbus_text(r->path_namespace, sizeof r->path_namespace, value), r->has |= 16;
                else if (dbus_equal(key, "destination") && vlen < DBUS_NAME_TEXT)
                        dbus_text(r->destination, sizeof r->destination, value), r->has |= 32;
                else if (key[0] == 'a' && key[1] == 'r' && key[2] == 'g' && key[3] >= '0' && key[3] <= '3' && vlen < 96 &&
                         (!key[4] || dbus_equal(key + 4, "path") || (key[3] == '0' && dbus_equal(key + 4, "namespace"))))
                {
                        r->arg_mode[key[3] - '0'] = !key[4] ? 0 : key[4] == 'p' ? 1 : 2;
                        dbus_text(r->arg[key[3] - '0'], sizeof r->arg[0], value);
                        r->arg_has |= (unsigned char)(1u << (key[3] - '0'));
                }
                else if (dbus_equal(key, "eavesdrop"))
                        ;
                else
                        return -1;
        }
        r->used = 1;
        return 0;
}

static int dbus_rule_same(const struct dbus_rule *a, const struct dbus_rule *b)
{
        if (a->type != b->type || a->has != b->has || a->arg_has != b->arg_has)
                return 0;
        for (long k = 0; k < 4; k++)
                if (a->arg_mode[k] != b->arg_mode[k])
                        return 0;
        if (!dbus_equal(a->sender, b->sender) || !dbus_equal(a->interface, b->interface) || !dbus_equal(a->member, b->member) ||
            !dbus_equal(a->path, b->path) || !dbus_equal(a->path_namespace, b->path_namespace) || !dbus_equal(a->destination, b->destination))
                return 0;
        for (long k = 0; k < 4; k++)
                if (!dbus_equal(a->arg[k], b->arg[k]))
                        return 0;
        return 1;
}

/* ---- delivery ---- */

// A message to every client with a rule it matches; from is the sender's slot, -1 for the bus.
static void dbus_broadcast(const unsigned char *bytes, long n, long from, const struct dbus_message *m)
{
        for (long at = 0; at < DBUS_CLIENTS; at++)
        {
                struct dbus_client *c = dbus.client[at];

                if (!c || !c->hello || c->dead)
                        continue;
                for (long k = 0; k < DBUS_RULES; k++)
                        if (c->rule[k].used && dbus_rule_matches(&c->rule[k], from, m))
                        {
                                dbus_deliver(c, bytes, n);
                                break;
                        }
        }
}

// A signal of the bus: NameOwnerChanged to those who ask for it, or a unicast one (NameAcquired, NameLost) to a client.
static void dbus_bus_signal(long to, const char *member, const char *a, const char *b, const char *c)
{
        struct dbus_out w = {dbus.scratch, 0, DBUS_MESSAGE, 0};
        struct dbus_fields f = {"/org/freedesktop/DBus", DBUS_NAME_BUS, member, "", "", DBUS_NAME_BUS, c ? "sss" : "s", 0, 0};
        struct dbus_message m;
        long body;

        if (to >= 0)
                f.destination = dbus_client_of(to)->unique;
        dbus_o_header(&w, DBUS_SIGNAL, 0, dbus_next_serial(), &f);
        body = w.length;
        dbus_o_string(&w, a);
        if (c)
        {
                dbus_o_string(&w, b);
                dbus_o_string(&w, c);
        }
        dbus_o_end(&w, body);
        if (w.bad)
                return;
        if (to >= 0)
        {
                dbus_deliver(dbus_client_of(to), w.at, w.length);
                return;
        }
        if (dbus_parse(w.at, w.length, &m) == 0)
                dbus_broadcast(w.at, w.length, -1, &m);
}

static void dbus_name_changed(const char *name, const char *old_owner, const char *new_owner)
{
        dbus_bus_signal(-1, "NameOwnerChanged", name, old_owner, new_owner);
}

/* ---- the bus's own methods ---- */

// An error reply to a call.
static void dbus_error_reply(struct dbus_client *to, const struct dbus_message *m, const char *name, const char *text, const char *from)
{
        struct dbus_out w = {dbus.scratch, 0, DBUS_MESSAGE, 0};
        struct dbus_fields f = {"", "", "", name, to->unique, from, "s", m->serial, 1};
        long body;

        if (m->flags & DBUS_FLAG_NO_REPLY)
                return;
        dbus_o_header(&w, DBUS_ERROR, DBUS_FLAG_NO_REPLY, dbus_next_serial(), &f);
        body = w.length;
        dbus_o_string(&w, text);
        dbus_o_end(&w, body);
        if (!w.bad)
                dbus_deliver(to, w.at, w.length);
}

static void dbus_reply_begin(struct dbus_out *w, struct dbus_client *to, const struct dbus_message *m, const char *signature,
                             const char *from, long *body)
{
        struct dbus_fields f = {"", "", "", "", to->unique, from, signature, m->serial, 1};

        w->at = dbus.scratch;
        w->length = 0;
        w->room = DBUS_MESSAGE;
        w->bad = 0;
        dbus_o_header(w, DBUS_RETURN, DBUS_FLAG_NO_REPLY, dbus_next_serial(), &f);
        *body = w->length;
}

static void dbus_reply_end(struct dbus_out *w, struct dbus_client *to, const struct dbus_message *m, long body)
{
        dbus_o_end(w, body);
        if (!w->bad && !(m->flags & DBUS_FLAG_NO_REPLY))
                dbus_deliver(to, w->at, w->length);
}

static void dbus_reply_u32(struct dbus_client *to, const struct dbus_message *m, unsigned long v, const char *from)
{
        struct dbus_out w;
        long body;

        dbus_reply_begin(&w, to, m, "u", from, &body);
        dbus_o_u32(&w, v);
        dbus_reply_end(&w, to, m, body);
}

static void dbus_reply_string(struct dbus_client *to, const struct dbus_message *m, const char *text, const char *from)
{
        struct dbus_out w;
        long body;

        dbus_reply_begin(&w, to, m, "s", from, &body);
        dbus_o_string(&w, text);
        dbus_reply_end(&w, to, m, body);
}

static void dbus_reply_empty(struct dbus_client *to, const struct dbus_message *m, const char *from)
{
        struct dbus_out w;
        long body;

        dbus_reply_begin(&w, to, m, "", from, &body);
        dbus_reply_end(&w, to, m, body);
}

// Reads the first string of a call's body into text, when its signature starts with s; its length or -1.
static long dbus_call_string(const struct dbus_message *m, char *text, long room)
{
        if (m->signature[0] != 's')
                return -1;
        return dbus_arg_string(m, 0, text, room);
}

static int dbus_name_ok(const char *name)
{
        return dbus_valid_name(name, dbus_length(name), 1);
}

static void dbus_name_release(long slot, struct dbus_name *n)
{
        struct dbus_client *c = dbus_client_of(slot);

        if (!n || !n->used || n->owner != slot)
                return;
        n->used = 0;
        n->owner = -1;
        if (c && c->names > 0)
                c->names--;
        dbus_name_changed(n->text, c ? c->unique : "", "");
}

// RequestName: 1 primary owner, 3 exists, 4 already owner; the flags: 1 allow replacement, 2 replace existing, 4 do not queue.
static unsigned long dbus_request_name(struct dbus_client *c, const char *name, unsigned long flags)
{
        struct dbus_name *n = dbus_name_find(name);
        long free_at = -1;

        if (n)
        {
                struct dbus_client *old;

                if (n->owner == c->slot)
                        return 4;
                if (n->owner == DBUS_SERVICE || !(flags & 2) || !n->replaceable)
                        return 3;
                old = dbus_client_of(n->owner);
                dbus_bus_signal(n->owner, "NameLost", name, 0, 0);
                if (old && old->names > 0)
                        old->names--;
                n->owner = c->slot;
                n->replaceable = (flags & 1) != 0;
                c->names++;
                dbus_name_changed(name, old ? old->unique : "", c->unique);
                dbus_bus_signal(c->slot, "NameAcquired", name, 0, 0);
                return 1;
        }
        for (long at = 0; at < DBUS_NAMES && free_at < 0; at++)
                if (!dbus.name[at].used)
                        free_at = at;
        if (free_at < 0 || c->names >= DBUS_NAMES_PER)
                return 0;
        dbus.name[free_at].used = 1;
        dbus.name[free_at].owner = (short)c->slot;
        dbus.name[free_at].replaceable = (flags & 1) != 0;
        dbus_text(dbus.name[free_at].text, DBUS_NAME_TEXT, name);
        c->names++;
        dbus_name_changed(name, "", c->unique);
        dbus_bus_signal(c->slot, "NameAcquired", name, 0, 0);
        return 1;
}

// A client's credentials as a{sv}: UnixUserID and ProcessID.
static void dbus_reply_credentials(struct dbus_client *to, const struct dbus_message *m, struct dbus_client *of)
{
        struct dbus_out w;
        long body, length_at, first;

        dbus_reply_begin(&w, to, m, "a{sv}", DBUS_NAME_BUS, &body);
        dbus_o_u32(&w, 0);
        length_at = w.length - 4;
        dbus_o_pad(&w, 8);
        first = w.length;
        dbus_o_pad(&w, 8);
        dbus_o_string(&w, "UnixUserID");
        dbus_o_sig(&w, "u");
        dbus_o_u32(&w, (unsigned long)of->uid);
        dbus_o_pad(&w, 8);
        dbus_o_string(&w, "ProcessID");
        dbus_o_sig(&w, "u");
        dbus_o_u32(&w, (unsigned long)of->pid);
        dbus_o_patch32(&w, length_at, (unsigned long)(w.length - first));
        dbus_reply_end(&w, to, m, body);
}

static void dbus_reply_names(struct dbus_client *to, const struct dbus_message *m, int all)
{
        struct dbus_out w;
        long body, length_at, first;

        dbus_reply_begin(&w, to, m, "as", DBUS_NAME_BUS, &body);
        dbus_o_u32(&w, 0);
        length_at = w.length - 4;
        first = w.length;
        dbus_o_string(&w, DBUS_NAME_BUS);
        for (long at = 0; at < DBUS_NAMES; at++)
                if (dbus.name[at].used)
                        dbus_o_string(&w, dbus.name[at].text);
        if (!all)
                dbus_o_string(&w, DBUS_NAME_PORTAL);
        if (all)
        {
                dbus_o_string(&w, dbus.service.unique);
                for (long at = 0; at < DBUS_CLIENTS; at++)
                        if (dbus.client[at] && dbus.client[at]->hello)
                                dbus_o_string(&w, dbus.client[at]->unique);
        }
        dbus_o_patch32(&w, length_at, (unsigned long)(w.length - first));
        dbus_reply_end(&w, to, m, body);
}

static const char dbus_introspect_bus[] =
    "<!DOCTYPE node PUBLIC \"-//freedesktop//DTD D-BUS Object Introspection 1.0//EN\" "
    "\"http://www.freedesktop.org/standards/dbus/1.0/introspect.dtd\">\n<node>\n"
    "<interface name=\"org.freedesktop.DBus\">\n"
    "<method name=\"Hello\"><arg type=\"s\" direction=\"out\"/></method>\n"
    "<method name=\"RequestName\"><arg type=\"s\" direction=\"in\"/><arg type=\"u\" direction=\"in\"/><arg type=\"u\" direction=\"out\"/></method>\n"
    "<method name=\"ReleaseName\"><arg type=\"s\" direction=\"in\"/><arg type=\"u\" direction=\"out\"/></method>\n"
    "<method name=\"ListNames\"><arg type=\"as\" direction=\"out\"/></method>\n"
    "<method name=\"ListActivatableNames\"><arg type=\"as\" direction=\"out\"/></method>\n"
    "<method name=\"NameHasOwner\"><arg type=\"s\" direction=\"in\"/><arg type=\"b\" direction=\"out\"/></method>\n"
    "<method name=\"GetNameOwner\"><arg type=\"s\" direction=\"in\"/><arg type=\"s\" direction=\"out\"/></method>\n"
    "<method name=\"StartServiceByName\"><arg type=\"s\" direction=\"in\"/><arg type=\"u\" direction=\"in\"/><arg type=\"u\" direction=\"out\"/></method>\n"
    "<method name=\"AddMatch\"><arg type=\"s\" direction=\"in\"/></method>\n"
    "<method name=\"RemoveMatch\"><arg type=\"s\" direction=\"in\"/></method>\n"
    "<method name=\"GetConnectionUnixUser\"><arg type=\"s\" direction=\"in\"/><arg type=\"u\" direction=\"out\"/></method>\n"
    "<method name=\"GetConnectionUnixProcessID\"><arg type=\"s\" direction=\"in\"/><arg type=\"u\" direction=\"out\"/></method>\n"
    "<method name=\"GetConnectionCredentials\"><arg type=\"s\" direction=\"in\"/><arg type=\"a{sv}\" direction=\"out\"/></method>\n"
    "<method name=\"GetId\"><arg type=\"s\" direction=\"out\"/></method>\n"
    "<signal name=\"NameOwnerChanged\"><arg type=\"s\"/><arg type=\"s\"/><arg type=\"s\"/></signal>\n"
    "<signal name=\"NameAcquired\"><arg type=\"s\"/></signal>\n<signal name=\"NameLost\"><arg type=\"s\"/></signal>\n"
    "</interface>\n</node>\n";

// Peer and Introspectable, which every object answers; 1 when the call was one of them.
static int dbus_common(struct dbus_client *from, const struct dbus_message *m, const char *as, const char *introspection)
{
        if (dbus_equal(m->interface, "org.freedesktop.DBus.Peer") && dbus_equal(m->member, "Ping"))
        {
                dbus_reply_empty(from, m, as);
                return 1;
        }
        if (dbus_equal(m->interface, "org.freedesktop.DBus.Peer") && dbus_equal(m->member, "GetMachineId"))
        {
                dbus_reply_string(from, m, dbus.guid, as);
                return 1;
        }
        if (dbus_equal(m->member, "Introspect") && (!m->interface[0] || dbus_equal(m->interface, "org.freedesktop.DBus.Introspectable")))
        {
                dbus_reply_string(from, m, introspection, as);
                return 1;
        }
        return 0;
}

// A call to org.freedesktop.DBus.
static void dbus_bus_call(struct dbus_client *from, const struct dbus_message *m)
{
        char text[DBUS_NAME_TEXT + 160];
        long length;
        const char *member = m->member;

        if (dbus_common(from, m, DBUS_NAME_BUS, dbus_introspect_bus))
                return;
        if (m->interface[0] && !dbus_equal(m->interface, DBUS_NAME_BUS))
        {
                dbus_error_reply(from, m, "org.freedesktop.DBus.Error.UnknownMethod", "No such interface", DBUS_NAME_BUS);
                return;
        }
        if (dbus_equal(member, "Hello"))
        {
                dbus_error_reply(from, m, "org.freedesktop.DBus.Error.Failed", "Already handled an Hello message", DBUS_NAME_BUS);
                return;
        }
        if (dbus_equal(member, "RequestName"))
        {
                unsigned long flags = 0;

                if (!dbus_equal(m->signature, "su") || (length = dbus_arg_string(m, 0, text, sizeof text)) < 0 || length >= DBUS_NAME_TEXT)
                {
                        dbus_error_reply(from, m, "org.freedesktop.DBus.Error.InvalidArgs", "RequestName takes a name and flags", DBUS_NAME_BUS);
                        return;
                }
                flags = dbus_get32(m->bytes + m->body_at + DBUS_ALIGN(4 + length + 1, 4));
                if (text[0] == ':' || !dbus_name_ok(text) || dbus_equal(text, DBUS_NAME_BUS))
                {
                        dbus_error_reply(from, m, "org.freedesktop.DBus.Error.InvalidArgs", "That name cannot be owned", DBUS_NAME_BUS);
                        return;
                }
                dbus_reply_u32(from, m, dbus_request_name(from, text, flags), DBUS_NAME_BUS);
                return;
        }
        if (dbus_equal(member, "ReleaseName"))
        {
                struct dbus_name *n;

                if (!dbus_equal(m->signature, "s") || (length = dbus_call_string(m, text, sizeof text)) < 0)
                {
                        dbus_error_reply(from, m, "org.freedesktop.DBus.Error.InvalidArgs", "ReleaseName takes a name", DBUS_NAME_BUS);
                        return;
                }
                n = dbus_name_find(text);
                if (!n)
                        dbus_reply_u32(from, m, 2, DBUS_NAME_BUS);
                else if (n->owner != from->slot)
                        dbus_reply_u32(from, m, 3, DBUS_NAME_BUS);
                else
                {
                        dbus_reply_u32(from, m, 1, DBUS_NAME_BUS);
                        dbus_name_release(from->slot, n);
                }
                return;
        }
        if (dbus_equal(member, "ListNames") || dbus_equal(member, "ListActivatableNames"))
        {
                dbus_reply_names(from, m, dbus_equal(member, "ListNames"));
                return;
        }
        if (dbus_equal(member, "NameHasOwner") || dbus_equal(member, "GetNameOwner") || dbus_equal(member, "GetConnectionUnixUser") ||
            dbus_equal(member, "GetConnectionUnixProcessID") || dbus_equal(member, "GetConnectionCredentials"))
        {
                long slot;
                struct dbus_client *of;

                if (!dbus_equal(m->signature, "s") || (length = dbus_call_string(m, text, sizeof text)) < 0 || !dbus_name_ok(text))
                {
                        dbus_error_reply(from, m, "org.freedesktop.DBus.Error.InvalidArgs", "That is not a name", DBUS_NAME_BUS);
                        return;
                }
                slot = dbus_equal(text, DBUS_NAME_BUS) ? -2 : dbus_resolve(text);
                of = dbus_client_of(slot);
                if (dbus_equal(member, "NameHasOwner"))
                {
                        struct dbus_out w;
                        long body;

                        dbus_reply_begin(&w, from, m, "b", DBUS_NAME_BUS, &body);
                        dbus_o_u32(&w, slot != -1);
                        dbus_reply_end(&w, from, m, body);
                        return;
                }
                if (slot == -1)
                {
                        dbus_error_reply(from, m, "org.freedesktop.DBus.Error.NameHasNoOwner", "The name does not have an owner", DBUS_NAME_BUS);
                        return;
                }
                if (dbus_equal(member, "GetNameOwner"))
                {
                        dbus_reply_string(from, m, slot == -2 ? DBUS_NAME_BUS : of->unique, DBUS_NAME_BUS);
                        return;
                }
                if (slot == -2)
                {
                        dbus_error_reply(from, m, "org.freedesktop.DBus.Error.Failed", "The bus has no credentials", DBUS_NAME_BUS);
                        return;
                }
                if (dbus_equal(member, "GetConnectionUnixUser"))
                        dbus_reply_u32(from, m, (unsigned long)of->uid, DBUS_NAME_BUS);
                else if (dbus_equal(member, "GetConnectionUnixProcessID"))
                        dbus_reply_u32(from, m, (unsigned long)of->pid, DBUS_NAME_BUS);
                else
                        dbus_reply_credentials(from, m, of);
                return;
        }
        if (dbus_equal(member, "StartServiceByName"))
        {
                if (!dbus_equal(m->signature, "su") || (length = dbus_arg_string(m, 0, text, sizeof text)) < 0)
                {
                        dbus_error_reply(from, m, "org.freedesktop.DBus.Error.InvalidArgs", "StartServiceByName takes a name and flags", DBUS_NAME_BUS);
                        return;
                }
                if (dbus_resolve(text) >= 0)
                        dbus_reply_u32(from, m, 2, DBUS_NAME_BUS);
                else
                        dbus_error_reply(from, m, "org.freedesktop.DBus.Error.ServiceUnknown", "The name is not provided by any service", DBUS_NAME_BUS);
                return;
        }
        if (dbus_equal(member, "AddMatch") || dbus_equal(member, "RemoveMatch"))
        {
                struct dbus_rule rule;
                long free_at = -1, found = -1;

                if (!dbus_equal(m->signature, "s") || (length = dbus_call_string(m, text, sizeof text)) < 0 || length > 400)
                {
                        dbus_error_reply(from, m, "org.freedesktop.DBus.Error.InvalidArgs", "A match rule is a string", DBUS_NAME_BUS);
                        return;
                }
                if (dbus_rule_parse(text, &rule) < 0)
                {
                        dbus_error_reply(from, m, "org.freedesktop.DBus.Error.MatchRuleInvalid", "That match rule is not one this bus takes", DBUS_NAME_BUS);
                        return;
                }
                for (long k = 0; k < DBUS_RULES; k++)
                {
                        if (!from->rule[k].used && free_at < 0)
                                free_at = k;
                        else if (from->rule[k].used && dbus_rule_same(&from->rule[k], &rule))
                                found = k;
                }
                if (member[0] == 'A')
                {
                        if (free_at < 0)
                        {
                                dbus_error_reply(from, m, "org.freedesktop.DBus.Error.LimitsExceeded", "Too many match rules", DBUS_NAME_BUS);
                                return;
                        }
                        from->rule[free_at] = rule;
                }
                else if (found < 0)
                {
                        dbus_error_reply(from, m, "org.freedesktop.DBus.Error.MatchRuleNotFound", "No such match rule", DBUS_NAME_BUS);
                        return;
                }
                else
                        from->rule[found].used = 0;
                dbus_reply_empty(from, m, DBUS_NAME_BUS);
                return;
        }
        if (dbus_equal(member, "GetId"))
        {
                dbus_reply_string(from, m, dbus.guid, DBUS_NAME_BUS);
                return;
        }
        if (dbus_equal(member, "UpdateActivationEnvironment") || dbus_equal(member, "ReloadConfig"))
        {
                dbus_reply_empty(from, m, DBUS_NAME_BUS);
                return;
        }
        dbus_error_reply(from, m, "org.freedesktop.DBus.Error.UnknownMethod", "No such method", DBUS_NAME_BUS);
}

/* ---- the Settings portal ---- */

static const char dbus_introspect_portal[] =
    "<!DOCTYPE node PUBLIC \"-//freedesktop//DTD D-BUS Object Introspection 1.0//EN\" "
    "\"http://www.freedesktop.org/standards/dbus/1.0/introspect.dtd\">\n<node>\n"
    "<interface name=\"org.freedesktop.portal.Settings\">\n"
    "<method name=\"ReadAll\"><arg type=\"as\" direction=\"in\"/><arg type=\"a{sa{sv}}\" direction=\"out\"/></method>\n"
    "<method name=\"Read\"><arg type=\"s\" direction=\"in\"/><arg type=\"s\" direction=\"in\"/><arg type=\"v\" direction=\"out\"/></method>\n"
    "<method name=\"ReadOne\"><arg type=\"s\" direction=\"in\"/><arg type=\"s\" direction=\"in\"/><arg type=\"v\" direction=\"out\"/></method>\n"
    "<signal name=\"SettingChanged\"><arg type=\"s\"/><arg type=\"s\"/><arg type=\"v\"/></signal>\n"
    "<property name=\"version\" type=\"u\" access=\"read\"/>\n"
    "</interface>\n</node>\n";

// The variant of a setting: its signature and its value.
static void dbus_o_setting(struct dbus_out *w, const struct dbus_setting *s)
{
        switch (s->kind)
        {
        case 'u':
        case 'i':
        case 'b':
                dbus_o_sig(w, s->kind == 'u' ? "u" : s->kind == 'i' ? "i" : "b");
                dbus_o_u32(w, (unsigned long)s->number & 0xffffffffUL);
                break;
        case 'd':
                dbus_o_sig(w, "d");
                dbus_o_u64(w, s->bits[0]);
                break;
        case 's':
                dbus_o_sig(w, "s");
                dbus_o_string(w, s->text);
                break;
        default:
                dbus_o_sig(w, "(ddd)");
                dbus_o_pad(w, 8);
                dbus_o_u64(w, s->bits[0]);
                dbus_o_u64(w, s->bits[1]);
                dbus_o_u64(w, s->bits[2]);
                break;
        }
}

static struct dbus_setting *dbus_setting_find(const char *ns, const char *key)
{
        for (long at = 0; at < DBUS_SETTINGS; at++)
                if (dbus.setting[at].used && dbus_equal(dbus.setting[at].ns, ns) && dbus_equal(dbus.setting[at].key, key))
                        return &dbus.setting[at];
        return 0;
}

// Whether a namespace is one a ReadAll filter names: the filter is empty, or the namespace, or a prefix ending in .* of it.
static int dbus_ns_wanted(const char *ns, const char *pattern)
{
        long n = dbus_length(pattern);

        if (!n)
                return 1;
        if (n >= 3 && pattern[n - 1] == '*' && pattern[n - 2] == '.')
        {
                for (long k = 0; k < n - 2; k++)
                        if (ns[k] != pattern[k])
                                return 0;
                return 1;
        }
        return dbus_equal(ns, pattern);
}

static void dbus_portal_read_all(struct dbus_client *from, const struct dbus_message *m)
{
        struct dbus_out w;
        long body, outer_at, outer_first, count = 0;
        char filter[8][96];
        const unsigned char *b = m->bytes + m->body_at;
        long at = 4, limit;

        if (!dbus_equal(m->signature, "as"))
        {
                dbus_error_reply(from, m, "org.freedesktop.DBus.Error.InvalidArgs", "ReadAll takes an array of namespaces", DBUS_NAME_PORTAL);
                return;
        }
        limit = 4 + (long)dbus_get32(b);
        while (at < limit)
        {
                long n;

                at = DBUS_ALIGN(at, 4);
                n = (long)dbus_get32(b + at);
                at += 4;
                if (count < 8 && n < 96)
                {
                        dbus_copy(filter[count], b + at, n);
                        filter[count++][n] = 0;
                }
                at += n + 1;
        }
        dbus_reply_begin(&w, from, m, "a{sa{sv}}", DBUS_NAME_PORTAL, &body);
        dbus_o_u32(&w, 0);
        outer_at = w.length - 4;
        dbus_o_pad(&w, 8);
        outer_first = w.length;
        for (long a = 0; a < DBUS_SETTINGS; a++)
        {
                const struct dbus_setting *s = &dbus.setting[a];
                int wanted = !count, first_of_ns = 1;
                long inner_at, inner_first;

                if (!s->used)
                        continue;
                for (long k = 0; k < count && !wanted; k++)
                        wanted = dbus_ns_wanted(s->ns, filter[k]);
                if (!wanted)
                        continue;
                for (long earlier = 0; earlier < a; earlier++)
                        if (dbus.setting[earlier].used && dbus_equal(dbus.setting[earlier].ns, s->ns))
                                first_of_ns = 0;
                if (!first_of_ns)
                        continue;
                dbus_o_pad(&w, 8);
                dbus_o_string(&w, s->ns);
                dbus_o_u32(&w, 0);
                inner_at = w.length - 4;
                dbus_o_pad(&w, 8);
                inner_first = w.length;
                for (long k = 0; k < DBUS_SETTINGS; k++)
                        if (dbus.setting[k].used && dbus_equal(dbus.setting[k].ns, s->ns))
                        {
                                dbus_o_pad(&w, 8);
                                dbus_o_string(&w, dbus.setting[k].key);
                                dbus_o_setting(&w, &dbus.setting[k]);
                        }
                dbus_o_patch32(&w, inner_at, (unsigned long)(w.length - inner_first));
        }
        dbus_o_patch32(&w, outer_at, (unsigned long)(w.length - outer_first));
        dbus_reply_end(&w, from, m, body);
}

/*
        The portal interfaces the service claims, with the version each answers to Properties.Get. Settings is its own.
        FileChooser is not, and is claimed at version 1 for a reason: Chromium asks for the version of the FileChooser
        portal at startup to learn whether there is a portal, and reads the Settings (the color-scheme, and with it
        prefers-color-scheme) only once that has been answered. It uses the portal for its file dialogs only from
        version 3, so at 1 its dialogs stay the toolkit's, and it never calls a method of this interface; a program
        that does is told the method is not there. (test/wayland_chromium.py holds both: the theme follows, and a click
        on a file input calls no OpenFile.)
*/
static struct
{
        char name[56];
        unsigned char version;
} dbus_interfaces[8] = {{"org.freedesktop.portal.Settings", 2}, {"org.freedesktop.portal.FileChooser", 1}};

static long dbus_interface_version(const char *name)
{
        for (long at = 0; at < 8; at++)
                if (dbus_interfaces[at].version && dbus_equal(dbus_interfaces[at].name, name))
                        return dbus_interfaces[at].version;
        return 0;
}

// An interface the service claims: 0, or -1 when there is no room or the name is too long.
DBUS_API int dbus_interface_claim(const char *name, unsigned version)
{
        for (long at = 0; at < 8; at++)
                if (!dbus_interfaces[at].version)
                {
                        if (dbus_length(name) >= (long)sizeof dbus_interfaces[at].name || !version || version > 255)
                                return -1;
                        dbus_text(dbus_interfaces[at].name, sizeof dbus_interfaces[at].name, name);
                        dbus_interfaces[at].version = (unsigned char)version;
                        return 0;
                }
        return -1;
}

// A call to the portal (the service's own).
static void dbus_service_call(struct dbus_client *from, const struct dbus_message *m)
{
        char ns[96], key[96];
        struct dbus_setting *s;
        struct dbus_out w;
        long body;

        if (dbus_common(from, m, dbus.service.unique, dbus_introspect_portal))
                return;
        if (!dbus_equal(m->path, DBUS_PATH_PORTAL))
        {
                dbus_error_reply(from, m, "org.freedesktop.DBus.Error.UnknownObject", "No such object", dbus.service.unique);
                return;
        }
        if (dbus_equal(m->interface, "org.freedesktop.portal.Settings"))
        {
                if (dbus_equal(m->member, "ReadAll"))
                {
                        dbus_portal_read_all(from, m);
                        return;
                }
                if ((dbus_equal(m->member, "Read") || dbus_equal(m->member, "ReadOne")) && dbus_equal(m->signature, "ss"))
                {
                        long first = dbus_arg_string(m, 0, ns, sizeof ns), second = dbus_arg_string(m, 1, key, sizeof key);

                        if (first < 0 || second < 0)
                        {
                                dbus_error_reply(from, m, "org.freedesktop.DBus.Error.InvalidArgs", "The namespace or the key is too long", dbus.service.unique);
                                return;
                        }
                        s = dbus_setting_find(ns, key);
                        if (!s)
                        {
                                dbus_error_reply(from, m, "org.freedesktop.portal.Error.NotFound", "Requested setting not found", dbus.service.unique);
                                return;
                        }
                        dbus_reply_begin(&w, from, m, "v", dbus.service.unique, &body);
                        if (m->member[4] == 0)
                        {
                                // Read, the old one, answers a variant that holds the variant
                                dbus_o_sig(&w, "v");
                        }
                        dbus_o_setting(&w, s);
                        dbus_reply_end(&w, from, m, body);
                        return;
                }
                dbus_error_reply(from, m, "org.freedesktop.DBus.Error.UnknownMethod", "No such method", dbus.service.unique);
                return;
        }
        if (dbus_equal(m->interface, "org.freedesktop.DBus.Properties"))
        {
                if (dbus_equal(m->member, "Get") && dbus_equal(m->signature, "ss") && dbus_arg_string(m, 0, ns, sizeof ns) >= 0 &&
                    dbus_arg_string(m, 1, key, sizeof key) >= 0)
                {
                        if (dbus_equal(key, "version") && dbus_interface_version(ns) > 0)
                        {
                                dbus_reply_begin(&w, from, m, "v", dbus.service.unique, &body);
                                dbus_o_sig(&w, "u");
                                dbus_o_u32(&w, (unsigned long)dbus_interface_version(ns));
                                dbus_reply_end(&w, from, m, body);
                                return;
                        }
                        dbus_error_reply(from, m, "org.freedesktop.DBus.Error.UnknownProperty", "No such property", dbus.service.unique);
                        return;
                }
                if (dbus_equal(m->member, "GetAll") && dbus_equal(m->signature, "s") && dbus_arg_string(m, 0, ns, sizeof ns) >= 0)
                {
                        long length_at, first;

                        dbus_reply_begin(&w, from, m, "a{sv}", dbus.service.unique, &body);
                        dbus_o_u32(&w, 0);
                        length_at = w.length - 4;
                        dbus_o_pad(&w, 8);
                        first = w.length;
                        if (dbus_interface_version(ns) > 0)
                        {
                                dbus_o_pad(&w, 8);
                                dbus_o_string(&w, "version");
                                dbus_o_sig(&w, "u");
                                dbus_o_u32(&w, (unsigned long)dbus_interface_version(ns));
                        }
                        dbus_o_patch32(&w, length_at, (unsigned long)(w.length - first));
                        dbus_reply_end(&w, from, m, body);
                        return;
                }
        }
        if (dbus_equal(m->interface, "org.freedesktop.host.portal.Registry") && dbus_equal(m->member, "Register"))
        {
                dbus_reply_empty(from, m, dbus.service.unique);
                return;
        }
        dbus_error_reply(from, m, "org.freedesktop.DBus.Error.UnknownMethod", "No such method", dbus.service.unique);
}

/* ---- routing ---- */

// The message with its sender stamped (what the client wrote there is not kept): the new bytes in dbus.stamped, their length.
static long dbus_stamp(const struct dbus_message *m, const char *sender)
{
        struct dbus_out w = {dbus.stamped, 0, DBUS_MESSAGE + 4096, 0};
        struct dbus_fields f = {m->path, m->interface, m->member, m->error, m->destination, sender, m->signature, m->reply,
                                (m->has & DBUS_HAS_REPLY) != 0};

        dbus_o_header(&w, m->type, m->flags, m->serial, &f);
        dbus_o_raw(&w, m->bytes + m->body_at, m->body_length);
        dbus_o_end(&w, DBUS_ALIGN(w.length - m->body_length, 8));
        return w.bad ? -1 : w.length;
}

static struct dbus_pending *dbus_pending_find(long caller, unsigned long serial, long callee)
{
        for (long at = 0; at < DBUS_PENDING; at++)
                if (dbus.pending[at].used && dbus.pending[at].caller == caller && dbus.pending[at].serial == serial &&
                    (callee < 0 || dbus.pending[at].callee == callee))
                        return &dbus.pending[at];
        return 0;
}

// A line for the host's trace of a message: its kind, who sent it, to whom, and what it is.
static void dbus_trace(struct dbus_client *from, const struct dbus_message *m)
{
        char line[400];
        long n = 0;
        static const char *const kinds[] = {"?", "call", "reply", "error", "signal"};
        const char *parts[] = {kinds[m->type], " ", from->unique, " -> ", m->destination[0] ? m->destination : "-", " ", m->path, " ", m->interface,
                               ".", m->member, m->error, "(", m->signature, ")", 0};

        char first[80] = "", second[80] = "";

        for (long k = 0; parts[k]; k++)
                for (long c = 0; parts[k][c] && n < (long)sizeof line - 1; c++)
                        line[n++] = parts[k][c];
        // the first two arguments when they are strings
        if (dbus_arg_string(m, 0, first, sizeof first) >= 0)
        {
                const char *more[] = {" [", first, 0, 0, 0};

                if (dbus_arg_string(m, 1, second, sizeof second) >= 0)
                        more[2] = ", ", more[3] = second;
                for (long k = 0; more[k]; k++)
                        for (long c = 0; more[k][c] && n < (long)sizeof line - 2; c++)
                                line[n++] = more[k][c];
                line[n++] = ']';
        }
        line[n] = 0;
        dbus.host->trace(dbus.host->context, line);
}

// One message from a client that has said Hello, routed.
static void dbus_route(struct dbus_client *from, const struct dbus_message *m)
{
        long to, n;
        struct dbus_client *target;

        if (dbus.host->trace)
                dbus_trace(from, m);

        if (m->type == DBUS_CALL)
        {
                const char *dest = m->destination;

                if (!dest[0] || dbus_equal(dest, DBUS_NAME_BUS))
                {
                        dbus_bus_call(from, m);
                        return;
                }
                to = dbus_resolve(dest);
                if (to == DBUS_SERVICE)
                {
                        dbus_service_call(from, m);
                        return;
                }
                if (to < 0)
                {
                        dbus_error_reply(from, m, "org.freedesktop.DBus.Error.ServiceUnknown",
                                         "The name is not provided by any service and is not owned", DBUS_NAME_BUS);
                        return;
                }
                target = dbus_client_of(to);
                if (!(m->flags & DBUS_FLAG_NO_REPLY))
                {
                        struct dbus_pending *p = 0;

                        for (long at = 0; at < DBUS_PENDING && !p; at++)
                                if (!dbus.pending[at].used)
                                        p = &dbus.pending[at];
                        if (!p || from->pending >= DBUS_PENDING_PER)
                        {
                                dbus_error_reply(from, m, "org.freedesktop.DBus.Error.LimitsExceeded", "Too many calls wait for a reply", DBUS_NAME_BUS);
                                return;
                        }
                        p->used = 1;
                        p->caller = (short)from->slot;
                        p->callee = (short)to;
                        p->serial = m->serial;
                        from->pending++;
                }
                n = dbus_stamp(m, from->unique);
                if (n > 0)
                        dbus_deliver(target, dbus.stamped, n);
                return;
        }
        if (m->type == DBUS_SIGNAL)
        {
                n = dbus_stamp(m, from->unique);
                if (n < 0)
                        return;
                if (m->destination[0])
                {
                        to = dbus_resolve(m->destination);
                        if (to >= 0 && to != DBUS_SERVICE)
                                dbus_deliver(dbus_client_of(to), dbus.stamped, n);
                        return;
                }
                {
                        struct dbus_message stamped;

                        if (dbus_parse(dbus.stamped, n, &stamped) == 0)
                                dbus_broadcast(dbus.stamped, n, from->slot, &stamped);
                }
                return;
        }
        // a reply or an error: to the caller that asked it of this client, once
        to = m->destination[0] ? dbus_resolve(m->destination) : -1;
        if (to < 0 || to == DBUS_SERVICE || !(m->has & DBUS_HAS_REPLY))
                return;
        {
                struct dbus_pending *p = dbus_pending_find(to, m->reply, from->slot);

                if (!p)
                        return;
                p->used = 0;
                if (dbus_client_of(to) && dbus_client_of(to)->pending > 0)
                        dbus_client_of(to)->pending--;
        }
        n = dbus_stamp(m, from->unique);
        if (n > 0)
                dbus_deliver(dbus_client_of(to), dbus.stamped, n);
}

// The first call of a connection, which must be Hello: it gets its unique name.
static int dbus_hello(struct dbus_client *c, const struct dbus_message *m)
{
        struct dbus_out w;
        long body;

        if (m->type != DBUS_CALL || !dbus_equal(m->member, "Hello") || !(dbus_equal(m->destination, DBUS_NAME_BUS) || !m->destination[0]) ||
            m->signature[0] || (m->interface[0] && !dbus_equal(m->interface, DBUS_NAME_BUS)))
                return -1;
        c->id = dbus.next_id++;
        dbus_unique_text(c->unique, c->id);
        c->hello = 1;
        dbus_reply_begin(&w, c, m, "s", DBUS_NAME_BUS, &body);
        dbus_o_string(&w, c->unique);
        dbus_reply_end(&w, c, m, body);
        dbus_bus_signal(c->slot, "NameAcquired", c->unique, 0, 0);
        dbus_name_changed(c->unique, "", c->unique);
        return 0;
}

/* ---- the authentication ---- */

static int dbus_hex_value(char c)
{
        return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}

static void dbus_say(struct dbus_client *c, const char *text)
{
        dbus_deliver(c, (const unsigned char *)text, dbus_length(text));
}

// The decimal text of a uid as hex of its characters, which is what EXTERNAL sends.
static int dbus_external_ok(struct dbus_client *c, const char *hex)
{
        char digits[16];
        long n = 0;

        {
                long id = c->uid;
                char rev[12];
                long k = 0;

                do
                        rev[k++] = (char)('0' + id % 10);
                while ((id /= 10) > 0);
                while (k)
                        digits[n++] = rev[--k];
                digits[n] = 0;
        }
        if (!hex[0])
                return 1;
        for (long k = 0; k < n; k++)
        {
                int hi = dbus_hex_value(hex[2 * k]), lo = hi < 0 ? -1 : dbus_hex_value(hex[2 * k + 1]);

                if (hi < 0 || lo < 0 || (hi << 4 | lo) != (unsigned char)digits[k])
                        return 0;
        }
        return !hex[2 * n];
}

// One line of the authentication (without its CR LF).
static int dbus_auth_line(struct dbus_client *c, char *line, long n)
{
        char *word[4] = {line, 0, 0, 0};
        long count = 1;

        for (long k = 0; k < n; k++)
                if (line[k] == ' ' && count < 4)
                {
                        line[k] = 0;
                        word[count++] = line + k + 1;
                }
        if (dbus_equal(word[0], "AUTH"))
        {
                if (count == 1 || (count >= 2 && !dbus_equal(word[1], "EXTERNAL") && !dbus_equal(word[1], "ANONYMOUS")))
                {
                        dbus_say(c, "REJECTED EXTERNAL ANONYMOUS\r\n");
                        return 0;
                }
                if (dbus_equal(word[1], "ANONYMOUS"))
                {
                        c->uid_ok = 1;
                        dbus_say(c, "OK ");
                        dbus_say(c, dbus.guid);
                        dbus_say(c, "\r\n");
                        return 0;
                }
                if (count == 2)
                {
                        // EXTERNAL with nothing after it: the client answers DATA with its data, or with none (sd-bus sends
                        // that line without waiting) to say that the credentials of the socket are enough.
                        c->want_data = 1;
                        dbus_say(c, "DATA\r\n");
                        return 0;
                }
                if (dbus_external_ok(c, word[2]))
                {
                        c->uid_ok = 1;
                        dbus_say(c, "OK ");
                        dbus_say(c, dbus.guid);
                        dbus_say(c, "\r\n");
                }
                else
                        dbus_say(c, "REJECTED EXTERNAL ANONYMOUS\r\n");
                return 0;
        }
        if (dbus_equal(word[0], "DATA"))
        {
                int asked = c->want_data;

                c->want_data = 0;
                if (asked && dbus_external_ok(c, count >= 2 ? word[1] : ""))
                {
                        c->uid_ok = 1;
                        dbus_say(c, "OK ");
                        dbus_say(c, dbus.guid);
                        dbus_say(c, "\r\n");
                }
                else
                        dbus_say(c, "REJECTED EXTERNAL ANONYMOUS\r\n");
                return 0;
        }
        if (dbus_equal(word[0], "NEGOTIATE_UNIX_FD"))
        {
                dbus_say(c, "ERROR \"file descriptors are not passed\"\r\n");
                return 0;
        }
        if (dbus_equal(word[0], "BEGIN"))
        {
                if (!c->uid_ok)
                        return -1;
                c->state = 2;
                return 1;
        }
        if (dbus_equal(word[0], "CANCEL") || dbus_equal(word[0], "ERROR"))
        {
                c->uid_ok = 0;
                dbus_say(c, "REJECTED EXTERNAL ANONYMOUS\r\n");
                return 0;
        }
        dbus_say(c, "ERROR\r\n");
        return 0;
}

/* ---- the host's side ---- */

DBUS_API int dbus_start(const struct dbus_host *host)
{
        unsigned char random[16];

        if (dbus.host || !host || !host->alloc || !host->release || !host->now || !host->random)
                return -1;
        for (unsigned long k = 0; k < sizeof dbus; k++)
                ((unsigned char *)&dbus)[k] = 0;
        dbus.host = host;
        dbus.next_id = 2;
        for (long at = 0; at < DBUS_NAMES; at++)
                dbus.name[at].owner = -1;
        host->random(host->context, random, 16);
        for (long k = 0; k < 16; k++)
        {
                dbus.guid[2 * k] = "0123456789abcdef"[random[k] >> 4];
                dbus.guid[2 * k + 1] = "0123456789abcdef"[random[k] & 15];
        }
        dbus.service.slot = DBUS_SERVICE;
        dbus.service.hello = 1;
        dbus.service.id = 1;
        dbus_text(dbus.service.unique, sizeof dbus.service.unique, ":1.1");
        dbus.name[0].used = 1;
        dbus.name[0].owner = DBUS_SERVICE;
        dbus_text(dbus.name[0].text, DBUS_NAME_TEXT, DBUS_NAME_PORTAL);
        dbus.service.names = 1;
        return 0;
}

// A connection accepted: its slot, or -1 for none (too many, too many of one uid). The host's alloc gives zeroed memory.
DBUS_API long dbus_connect(long pid, long uid)
{
        struct dbus_client *c;
        long slot = -1, same = 0;

        if (!dbus.host)
                return -1;
        for (long at = 0; at < DBUS_CLIENTS; at++)
        {
                if (!dbus.client[at] && slot < 0)
                        slot = at;
                else if (dbus.client[at] && dbus.client[at]->uid == uid)
                        same++;
        }
        if (slot < 0 || (uid != 0 && same >= DBUS_PER_UID))
                return -1;
        c = (struct dbus_client *)dbus.host->alloc(dbus.host->context, (long)sizeof *c);
        if (!c)
                return -1;
        c->live = 1;
        c->slot = slot;
        c->pid = pid;
        c->uid = uid;
        c->born = dbus.host->now(dbus.host->context);
        dbus.client[slot] = c;
        dbus.clients++;
        return slot;
}

// A client gone: its names are let go, the calls it waited on and the calls waiting on it are answered, its record returned.
DBUS_API void dbus_drop(long slot)
{
        struct dbus_client *c = slot >= 0 && slot < DBUS_CLIENTS ? dbus.client[slot] : 0;

        if (!c)
                return;
        dbus.client[slot] = 0;
        dbus.clients--;
        if (c->hello)
        {
                for (long at = 0; at < DBUS_NAMES; at++)
                        if (dbus.name[at].used && dbus.name[at].owner == slot)
                        {
                                dbus.name[at].used = 0;
                                dbus.name[at].owner = -1;
                                dbus_name_changed(dbus.name[at].text, c->unique, "");
                        }
                for (long at = 0; at < DBUS_PENDING; at++)
                {
                        struct dbus_pending *p = &dbus.pending[at];

                        if (!p->used)
                                continue;
                        if (p->caller == slot)
                                p->used = 0;
                        else if (p->callee == slot)
                        {
                                struct dbus_client *caller = dbus_client_of(p->caller);
                                struct dbus_message fake;

                                p->used = 0;
                                if (caller)
                                {
                                        if (caller->pending > 0)
                                                caller->pending--;
                                        fake.serial = p->serial;
                                        fake.flags = 0;
                                        dbus_error_reply(caller, &fake, "org.freedesktop.DBus.Error.NoReply",
                                                         "Message recipient disconnected from message bus without replying", DBUS_NAME_BUS);
                                }
                        }
                }
                dbus_name_changed(c->unique, c->unique, "");
        }
        dbus.host->release(dbus.host->context, c, (long)sizeof *c);
}

// Where a client's bytes are to be read to, and how many fit.
DBUS_API unsigned char *dbus_in_room(long slot, long *room)
{
        struct dbus_client *c = slot >= 0 && slot < DBUS_CLIENTS ? dbus.client[slot] : 0;

        *room = 0;
        if (!c || c->dead)
                return 0;
        *room = (long)sizeof c->in - c->in_length;
        return c->in + c->in_length;
}

/*
        count bytes were read into the room. The authentication's lines are taken, then every whole message; a message
        that is not valid, a line too long, or a call before Hello that is none ends the connection (dbus_dead says so).
*/
DBUS_API void dbus_in_done(long slot, long count)
{
        struct dbus_client *c = slot >= 0 && slot < DBUS_CLIENTS ? dbus.client[slot] : 0;
        long at = 0;

        if (!c || c->dead || count < 0 || count > (long)sizeof c->in - c->in_length)
                return;
        c->in_length += count;
        while (at < c->in_length && !c->dead)
        {
                if (c->state == 0)
                {
                        if (c->in[at] != 0)
                        {
                                c->dead = 1;
                                c->why = "the first byte was not a NUL";
                                break;
                        }
                        c->state = 1;
                        at++;
                        continue;
                }
                if (c->state == 1)
                {
                        long limit = at;
                        int more;

                        while (limit + 1 < c->in_length && !(c->in[limit] == '\r' && c->in[limit + 1] == '\n'))
                                limit++;
                        if (limit + 1 >= c->in_length)
                        {
                                if (c->in_length - at > DBUS_AUTH_LINE)
                                        c->dead = 1, c->why = "an authentication line was too long";
                                break;
                        }
                        c->auth_total += limit - at + 2;
                        if (limit - at > DBUS_AUTH_LINE || c->auth_total > DBUS_AUTH_TOTAL)
                        {
                                c->dead = 1;
                                c->why = "the authentication was too long";
                                break;
                        }
                        for (long k = at; k < limit; k++)
                                if (c->in[k] < 32 || c->in[k] > 126)
                                        c->dead = 1, c->why = "a byte that is no text in the authentication";
                        if (c->dead)
                                break;
                        {
                                char line[DBUS_AUTH_LINE + 1];

                                dbus_copy(line, c->in + at, limit - at);
                                line[limit - at] = 0;
                                more = dbus_auth_line(c, line, limit - at);
                        }
                        at = limit + 2;
                        if (more < 0)
                                c->dead = 1, c->why = "BEGIN before an authentication";
                        continue;
                }
                {
                        struct dbus_message m;
                        long total;

                        if (c->in_length - at < 16)
                                break;
                        total = 16 + (long)dbus_get32(c->in + at + 12);
                        total = DBUS_ALIGN(total, 8) + (long)dbus_get32(c->in + at + 4);
                        if (total < 16 || total > DBUS_MESSAGE)
                        {
                                c->dead = 1;
                                c->why = "a message of a size past the cap";
                                break;
                        }
                        if (c->in_length - at < total)
                                break;
                        if (dbus_parse(c->in + at, total, &m) < 0)
                        {
                                c->dead = 1;
                                c->why = "a message that is not valid";
                                break;
                        }
                        if (!c->hello)
                        {
                                if (dbus_hello(c, &m) < 0)
                                        c->dead = 1, c->why = "the first call was not Hello";
                        }
                        else
                                dbus_route(c, &m);
                        at += total;
                }
        }
        if (at > 0)
        {
                for (long k = at; k < c->in_length; k++)
                        c->in[k - at] = c->in[k];
                c->in_length -= at;
        }
}

DBUS_API long dbus_out(long slot, const unsigned char **bytes)
{
        struct dbus_client *c = slot >= 0 && slot < DBUS_CLIENTS ? dbus.client[slot] : 0;

        *bytes = c ? c->out : 0;
        return c ? c->out_length : 0;
}

DBUS_API void dbus_sent(long slot, long count)
{
        struct dbus_client *c = slot >= 0 && slot < DBUS_CLIENTS ? dbus.client[slot] : 0;

        if (!c || count <= 0 || count > c->out_length)
                return;
        for (long k = count; k < c->out_length; k++)
                c->out[k - count] = c->out[k];
        c->out_length -= count;
}

// 0 while a client is well, else why not (1 it spoke wrong, 2 it did not read).
DBUS_API long dbus_dead(long slot)
{
        struct dbus_client *c = slot >= 0 && slot < DBUS_CLIENTS ? dbus.client[slot] : 0;

        return c ? c->dead : 1;
}

// Connections that have not said Hello in DBUS_HELLO_MS are ended; called now and then.
DBUS_API void dbus_tick(void)
{
        long now;

        if (!dbus.host)
                return;
        now = dbus.host->now(dbus.host->context);
        for (long at = 0; at < DBUS_CLIENTS; at++)
                if (dbus.client[at] && !dbus.client[at]->hello && !dbus.client[at]->dead && now - dbus.client[at]->born > DBUS_HELLO_MS)
                        dbus.client[at]->dead = 1, dbus.client[at]->why = "it did not say Hello in time";
}

// What ended a client, for the log (or "").
DBUS_API const char *dbus_why(long slot)
{
        struct dbus_client *c = slot >= 0 && slot < DBUS_CLIENTS ? dbus.client[slot] : 0;

        return c && c->why ? c->why : "";
}

/*
        A setting of the portal: its namespace and key, and its value of the kind (u, i, b with number; d with bits[0],
        the double's bits; s with text; c with the three doubles). A value that is not what it was is told to every client
        that listens for SettingChanged. Answers 1 when it changed, 0 when it is as it was, -1 when there is no room.
*/
DBUS_API int dbus_setting_set(const char *ns, const char *key, unsigned kind, long number, const unsigned long *bits, const char *text)
{
        struct dbus_setting *s = dbus_setting_find(ns, key), fresh;
        struct dbus_out w = {dbus.scratch, 0, DBUS_MESSAGE, 0};
        struct dbus_fields f = {DBUS_PATH_PORTAL, "org.freedesktop.portal.Settings", "SettingChanged", "", "", ":1.1", "ssv", 0, 0};
        struct dbus_message m;
        long body;
        int existed = s != 0;

        if (!dbus.host || dbus_length(ns) >= (long)sizeof s->ns || dbus_length(key) >= (long)sizeof s->key)
                return -1;
        if (!s)
        {
                for (long at = 0; at < DBUS_SETTINGS && !s; at++)
                        if (!dbus.setting[at].used)
                                s = &dbus.setting[at];
                if (!s)
                        return -1;
        }
        for (unsigned long k = 0; k < sizeof fresh; k++)
                ((unsigned char *)&fresh)[k] = 0;
        dbus_text(fresh.ns, sizeof fresh.ns, ns);
        dbus_text(fresh.key, sizeof fresh.key, key);
        fresh.used = 1;
        fresh.kind = (unsigned char)kind;
        fresh.number = number;
        if (bits)
                for (long k = 0; k < 3; k++)
                        fresh.bits[k] = bits[k];
        if (text)
                dbus_text(fresh.text, sizeof fresh.text, text);
        if (existed && s->kind == fresh.kind && s->number == fresh.number && s->bits[0] == fresh.bits[0] && s->bits[1] == fresh.bits[1] &&
            s->bits[2] == fresh.bits[2] && dbus_equal(s->text, fresh.text))
                return 0;
        *s = fresh;
        if (!existed)
                return 1;
        dbus_o_header(&w, DBUS_SIGNAL, DBUS_FLAG_NO_REPLY, dbus_next_serial(), &f);
        body = w.length;
        dbus_o_string(&w, ns);
        dbus_o_string(&w, key);
        dbus_o_setting(&w, s);
        dbus_o_end(&w, body);
        if (!w.bad && dbus_parse(w.at, w.length, &m) == 0)
                dbus_broadcast(w.at, w.length, DBUS_SERVICE, &m);
        return 1;
}

DBUS_API void dbus_stop(void)
{
        for (long at = 0; at < DBUS_CLIENTS; at++)
                dbus_drop(at);
        dbus.host = 0;
}

#endif /* DBUS_INCLUDED */
