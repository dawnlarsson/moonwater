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
        Settings from a table the host fills from Moonwater's settings and keeps current. Every other thing a desktop's
        programs ask of it at their start is answered in the call: a portal's request with its Response, the desktop's
        notifications into the records its host shows (org.freedesktop.Notifications, the portal's Notification), and
        a service that is not here with ServiceUnknown, so no program waits on the bus.

        WHAT IT IS NOT

        There is no activation, no policy files, no monitoring, no eavesdropping, no byte order but the machine's (a
        message in the other one drops the client), no queue of owners of a name. File descriptors are passed only where
        the host gives the bus its descriptor operations (dbus_set_fd_ops; the sessionbus does, SCM_RIGHTS), and the
        negotiation is refused where it does not. A call to a name nobody owns, and StartServiceByName of one, is
        ServiceUnknown in the call, as it is on a bus with no services installed: nothing is started and nothing waits.

        THREAT MODEL

        Everything a connection sends is hostile: the authentication lines, every header field, every length and the
        body, and the order and number of calls. A connection is any program a person runs, in a bowl or not. So:
        1. Fixed tables only: a client's record is one block the host maps when it connects, and nothing a client says
           is the size of anything.
        2. A message is read whole and validated whole (the header's fields by type, every name, path and signature by
           its grammar, the body by its signature down to the padding and the UTF-8 of each string) before anything of it
           is acted on or passed on; a message that is not valid is the end of that connection. Valid is at least as
           strict as every client library a message is passed to: libdbus and GDBus end their own connection on a
           message they cannot read, so one this bus passed on would let any client disconnect any other. So the
           reserved org.freedesktop.DBus.Local interface and path (where a library's own Disconnected comes from), a
           reply serial of 0, and containers nested past libdbus's 64 (dict entries counted) are not valid here.
        3. The sender of a message is the bus's to say: it is stamped on what is passed on and what a client wrote there
           is not kept. A reply is passed only to the caller it answers, from the callee it was asked of.
        4. A client that does not read never blocks the bus, and no client can make the bus drop another: a queue holds
           what its client asked for (answers to its calls, the bus's own words to it) up to DBUS_OUT, and what others
           sent it unasked (calls, signals) only up to DBUS_UNASKED, so a flood is refused (a call so refused is an
           error to its caller) and the client's own answers still have room. A client whose queue refuses a message
           and that then reads nothing for DBUS_STALL_MS is dropped. A connection that has not said Hello in a few
           seconds is dropped. No wait is without its end: a call routed to a client that does not answer in
           DBUS_REPLY_MS is answered NoReply and its caller's row let go, and a portal Response the bus holds for its
           caller's rule is sent after DBUS_RESPONSE_MS whatever the caller has.
        5. A name is a client's only while it is connected; the bus's own names, and the service's, are not for taking, nor
           are the names whose owner every bowl's programs would send keys, passwords or notifications to (dbus_kept).

        It is freestanding C with no allocator, in ring 3, the same way the Wayland shim is, with the host behind a small
        seam (struct dbus_host) so that the checks can put a fake one behind it.
*/
#ifndef DBUS_INCLUDED
#define DBUS_INCLUDED

#define DBUS_API static __attribute__((unused))

#define DBUS_CLIENTS 128         // a desktop's programs: a browser alone holds three or four
#define DBUS_PER_UID 96          // connections of one uid (a bowl's user is every program of the bowls), root's as many as there is room for
#define DBUS_MESSAGE 262144      // the most a message may be, header and body
#define DBUS_STAMPED (DBUS_MESSAGE + 4096)   // the most a message may be once the bus has stamped its sender on it
#define DBUS_OUT (2 * DBUS_MESSAGE + 16384)  // a client's output queue: what it asked for, and what others sent it
#define DBUS_UNASKED (DBUS_STAMPED + 4096)   // the part of it others may fill unasked (their calls, signals): the largest message, stamped
#define DBUS_STALL_MS 15000      // how long a client whose queue refused a message may go without reading
#define DBUS_NAMES_PER 8         // well-known names one client owns
#define DBUS_NAMES (DBUS_CLIENTS * DBUS_NAMES_PER + 1) // all together, the portal's too: room for every client's, so no uid's clients fill it for another's
#define DBUS_NAME_BUCKETS 1024   // the names' hash table (a power of two)
#define DBUS_RULES 64            // match rules one client holds
#define DBUS_RULE_TEXT 1024      // the longest match rule read
#define DBUS_PENDING_PER 64      // method calls of one client waiting for a reply
#define DBUS_PENDING (DBUS_CLIENTS * DBUS_PENDING_PER) // all clients together: slot s's own rows are [s * DBUS_PENDING_PER, (s + 1) * DBUS_PENDING_PER), so none takes another's
#define DBUS_SETTINGS 64
#define DBUS_NAME_TEXT 128       // the longest well-known name kept (the protocol's 255 are refused)
#define DBUS_DEPTH 64            // how deep the containers of a value nest: arrays, dict entries, structs and variants, as libdbus counts
#define DBUS_AUTH_LINE 512
#define DBUS_AUTH_TOTAL 4096
#define DBUS_HELLO_MS 5000       // how long a connection has to authenticate and say Hello
#define DBUS_SERVICE DBUS_CLIENTS // the slot of the portal service, which has no socket
#define DBUS_FDS 32              // most file descriptors queued per direction on a client or per message
#define DBUS_REPLY_MS 25000      // how long a call routed to a client waits for its answer: the client libraries' own default (libdbus, GDBus, sd-bus, Qt)
#define DBUS_RESPONSES 4         // portal Responses one client is owed and not yet sent (one past them is sent at once)
#define DBUS_RESPONSE_MS 1000    // how long a portal Response waits for its caller's rule before it is sent all the same
#define DBUS_REQUEST_PATH 160    // a portal request's path: the prefix, the caller's name, a token of DBUS_TOKEN

#define DBUS_CALL 1
#define DBUS_RETURN 2
#define DBUS_ERROR 3
#define DBUS_SIGNAL 4
#define DBUS_FLAG_NO_REPLY 1
#define DBUS_FLAG_NO_START 2

#define DBUS_NAME_BUS "org.freedesktop.DBus"
#define DBUS_NAME_PORTAL "org.freedesktop.portal.Desktop"
#define DBUS_PATH_PORTAL "/org/freedesktop/portal/desktop"
#define DBUS_NAME_NOTIFY "org.freedesktop.Notifications"
#define DBUS_PATH_NOTIFY "/org/freedesktop/Notifications"

#define DBUS_NOTICES 4                   // notifications kept at once, every program's together: the oldest gives way to a new one
#define DBUS_NOTICE_APP 24               // a program's name kept, sanitized, its NUL with it (the board's key is as long)
#define DBUS_NOTICE_TEXT 192             // the summary and the body kept, sanitized, together, their NUL with them (the board's detail)
#define DBUS_NOTICE_MS (30L * 60 * 1000) // how long a notification that names no time is kept, and the longest one may name
#define DBUS_NOTICE_SHORTEST 1000        // the shortest time one may name
#define DBUS_NOTICE_BURST 8              // notifications one client may post in DBUS_NOTICE_SPAN_MS
#define DBUS_NOTICE_ALL 32               // and every client together
#define DBUS_NOTICE_SPAN_MS 10000

// A notification the bus keeps, as its host is given it (dbus_host.notices): id 0 is none.
struct dbus_notice
{
        unsigned long id;                // the id Notify answered (the portal's have one too, which no client sees)
        unsigned long order;             // newer ones are larger
        long due;                        // when it expires (0: never)
        long owner;                      // the id (:1.N) of the client that posted it
        unsigned char urgency;           // 0 low, 1 normal, 2 critical (the urgency hint, the portal's priority)
        unsigned char portal;            // posted through org.freedesktop.portal.Notification, by `key`
        char key[64];                    // the portal's own id of it
        char app[DBUS_NOTICE_APP];       // the program's name, sanitized: never empty
        char text[DBUS_NOTICE_TEXT];     // the summary, and " - " and the body after it when there is one, sanitized
};

/* What the bus asks of its host. now is milliseconds on a clock that only goes forward; random fills bytes with random ones. */
/* What a FileChooser request asks of the host's chooser (dbus_portal_choose). */
#define DBUS_CHOOSE_OPEN 0
#define DBUS_CHOOSE_SAVE 1
#define DBUS_CHOOSE_PATH 4096
struct dbus_choice
{
        long slot, id;                  // the client asking, and its id (its slot may be another client's by the time it is answered)
        unsigned char kind;             // DBUS_CHOOSE_OPEN or DBUS_CHOOSE_SAVE
        unsigned char multiple, directory;
        char title[256];                // the title the program gives the chooser, or nothing
        char name[256];                 // SaveFile's suggested name, or nothing
        char folder[DBUS_CHOOSE_PATH];  // the folder to start in, or nothing
};

struct dbus_host
{
        void *context;
        void *(*alloc)(void *context, long bytes);
        void (*release)(void *context, void *block, long bytes);
        long (*now)(void *context);
        void (*random)(void *context, unsigned char *bytes, long count);
        void (*trace)(void *context, const char *line); // optional: one line for each message the bus routes, to read what a program asks
        // optional: the notifications the bus keeps, every time they change (DBUS_NOTICES of them, id 0 where there is none)
        void (*notices)(void *context, const struct dbus_notice *notice, long count);
        // optional: a chooser of files shown for a client's FileChooser request (0, or -1 when none can be), its answer given back by
        // dbus_choice_done; and one let go (the request closed, the client gone) before it was answered
        long (*choose)(void *context, const struct dbus_choice *choice);
        void (*unchoose)(void *context, long slot);
};

/* A match rule, as AddMatch gave it. */
struct dbus_rule
{
        unsigned char used, type;       // type 0 for any, else DBUS_CALL..DBUS_SIGNAL
        unsigned char has;              // which of the strings below were given
        char sender[DBUS_NAME_TEXT], interface[DBUS_NAME_TEXT], member[64], path[128], path_namespace[128], destination[DBUS_NAME_TEXT];
        char arg[4][96];
        unsigned char arg_has, arg_mode[4]; // arg_mode: 0 the argument is the string, 1 a path of it (argNpath), 2 a name of its namespace (arg0namespace)
        unsigned sender_hash;           // dbus_hash of sender, made once: a broadcast asks who owns it of every rule that names it
};

// A portal Response the bus owes a client (dbus_portal_request): its request's path and its code, held until due (0: none).
struct dbus_response
{
        long due;
        unsigned long code;
        char path[DBUS_REQUEST_PATH];
};

struct dbus_client
{
        long live, dead, slot, pid, uid, state; // state: 0 awaits the first byte, 1 the authentication, 2 messages
        const char *why;                        // what ended it, for the log
        long in_length, auth_total, born;
        long out_head, out_length;              // the queue is out[out_head, out_length): what is sent moves the head
        long full_since;                        // when its queue first refused a message since it last read (0: it has not)
        long id;                                // the unique name is :1.id
        unsigned char hello, uid_ok, want_data, unix_fds; // want_data: EXTERNAL was named with no data, and DATA is asked for
        long names, pending;
        long rules;                             // rule[0, rules) holds every rule it has (a hole where one was removed): what a broadcast looks through
        char unique[16];
        struct dbus_rule rule[DBUS_RULES];
        long in_fds, out_fds, out_fd_runs;
        int in_fd[DBUS_FDS], out_fd[DBUS_FDS];
        struct
        {
                long at, count;
        } out_fd_run[DBUS_FDS];
        long responses;                         // of response[], the ones held
        struct dbus_response response[DBUS_RESPONSES];
        long notice_span, notice_count;         // when its span of DBUS_NOTICE_SPAN_MS began, and the notifications it posted in it
        char choosing[DBUS_REQUEST_PATH];       // the request a chooser of the host's is open for (dbus_portal_choose), or nothing
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
        unsigned hash;                  // dbus_hash of text
        short owner;                    // a client's slot, DBUS_SERVICE for the service, -1 for none
        short next;                     // the next name of its bucket, -1 for none
        unsigned char used, replaceable;
};

struct dbus_pending
{
        unsigned long serial;           // the caller's serial
        long due;                       // when the caller is told NoReply and the row is let go
        short caller, callee;
        unsigned char used;
};

static struct
{
        const struct dbus_host *host;
        struct dbus_client *client[DBUS_CLIENTS];
        struct dbus_client service;     // the portal's record: no socket, a unique name and a name, its calls answered in here
        struct dbus_name name[DBUS_NAMES];
        short bucket[DBUS_NAME_BUCKETS]; // the first name of each bucket (by its hash), -1 for none
        short recent;                    // the name dbus_name_find found last, -1 for none (asked again only while it is used)
        struct dbus_pending pending[DBUS_PENDING];
        struct dbus_setting setting[DBUS_SETTINGS];
        long next_id, clients;
        unsigned long serial;
        long due;                        // the earliest deadline of a pending call or a held Response (0: none), which dbus_tick acts on
        unsigned long requests;          // portal requests made, for the token of one that names none
        struct dbus_notice notice[DBUS_NOTICES];
        unsigned long notice_ids;        // the last notification id given
        unsigned long notice_order;      // the last notification posted, for which is the oldest
        long notice_span, notice_count;  // when the bus's span of DBUS_NOTICE_SPAN_MS began, and the notifications posted in it
        char guid[33];
        unsigned char scratch[DBUS_STAMPED];
        unsigned char stamped[DBUS_STAMPED];
} dbus;

static struct
{
        int (*dup_fd)(int fd);
        void (*close_fd)(int fd);
} dbus_fd_ops;

DBUS_API void dbus_set_fd_ops(int (*dup_fd)(int fd), void (*close_fd)(int fd))
{
        dbus_fd_ops.dup_fd = dup_fd;
        dbus_fd_ops.close_fd = close_fd;
}

static void dbus_close_fds(const int *fds, long count)
{
        if (!dbus_fd_ops.close_fd)
                return;
        for (long k = 0; k < count; k++)
                if (fds[k] >= 0)
                        dbus_fd_ops.close_fd(fds[k]);
}

/*
        ---- small things ----

        THE LIBRARY'S LOOPS. Where lib.c is in the translation unit (the shell's sessionbus) the scans and the copies are
        its, assembly on the three machines, where BENCH_dbus measured them faster than the C (x86_64, 9950X, ticks a
        call): string_compare at the names a bus has (6.4 against 13.1), the UTF-8 validator from 8 bytes (8.3 against
        17.3 at 8, 11.2 against 80.8 at 40; below 8 its walk loses, 11.1 against 4.6 at one byte, and the C walk stays),
        memory_copy from 300 bytes (17.3 against 21.1 at 300, 32.0 against 64.2 at 1100; below that the compiler's own
        loop is as fast or faster, 2.8 against 8.1 at 16). Each was then taken out of the engine alone and timed in its
        rows: without the validator a ReadOne is 1210 ticks against 870, without string_compare 994 to 1050; string_length,
        3.8 against 12.1 alone, made it 3% slower there (short names, where the inlined loop wins) and is not used. A
        stand-alone compile of this file and the hosted check have the C, which is what they test; BENCH_dbus built with
        -DDBUS_C_ONLY times the engine on its C beside lib.c, the before of each row.
*/
#if defined(STANDARD_MODERN_C) && !defined(DBUS_C_ONLY)
#define DBUS_LIBRARY 1
#else
#define DBUS_LIBRARY 0
#endif
#ifndef offset_of
#define offset_of(type, member) ((unsigned long)&((type *)0)->member)
#endif
// The shortest text lib.c's validator takes, and the shortest copy memory_copy takes. arm64 has x86_64's (BENCH_dbus
// under qemu: the validator 89% of the C's instructions at 8 bytes, the copy 83% at 300); riscv64 without V its own,
// as qemu has its instructions (no hardware timing): the validator 101 to 109% at 8 to 12 bytes and 73% at 16, the
// copy 50% at 100, since its byte loop is not made wide there.
#if defined(__riscv)
#define DBUS_UTF8_LIBRARY 16
#define DBUS_COPY_LIBRARY 100
#else
#define DBUS_UTF8_LIBRARY 8
#define DBUS_COPY_LIBRARY 300
#endif

// The C loop, inlined at its callers, which are short names: string_length in its place made a ReadOne 3% slower in the engine.
static long dbus_length(const char *s)
{
        long n = 0;

        while (s[n])
                n++;
        return n;
}

static int dbus_equal(const char *a, const char *b)
{
#if DBUS_LIBRARY
        return !string_compare((string_address)a, (string_address)b);
#else
        long n = 0;

        while (a[n] && a[n] == b[n])
                n++;
        return a[n] == b[n];
#endif
}

#if DBUS_LIBRARY
#define DBUS_TABLE_LOOKUP(key, table) ((long)string_table_lookup((string_address)(key), (table)))
#else
#define DBUS_TABLE_LOOKUP(key, table)                                                                \
        ({                                                                                           \
                long _dbus_i = 0, _dbus_n = (long)(sizeof(table) / sizeof((table)[0]));              \
                while (_dbus_i < _dbus_n && !dbus_equal((key), (table)[_dbus_i].name))               \
                        _dbus_i++;                                                                   \
                _dbus_i;                                                                             \
        })
#endif

static int dbus_starts(const char *text, const char *prefix)
{
        long n = 0;

        while (prefix[n] && text[n] == prefix[n])
                n++;
        return !prefix[n];
}

// n bytes, forward a byte at a time (so a move down, to a lower address, is safe too): the copy below DBUS_COPY_LIBRARY.
static void dbus_bytes(void *to, const void *from, long n)
{
        unsigned char *d = (unsigned char *)to;
        const unsigned char *s = (const unsigned char *)from;

        for (long at = 0; at < n; at++)
                d[at] = s[at];
}

// n bytes; the two may overlap with `to` below `from` (a queue moved to its front), which memory_copy, a memmove, takes as well.
static void dbus_copy(void *to, const void *from, long n)
{
#if DBUS_LIBRARY
        if (n >= DBUS_COPY_LIBRARY)
        {
                memory_copy(to, from, (positive)n);
                return;
        }
#endif
        dbus_bytes(to, from, n);
}

static void dbus_zero(void *to, unsigned long n)
{
#if DBUS_LIBRARY
        memory_zero(to, (positive)n);
#else
        unsigned char *d = (unsigned char *)to;

        for (unsigned long k = 0; k < n; k++)
                d[k] = 0;
#endif
}

static void dbus_text(char *to, long room, const char *from)
{
#if DBUS_LIBRARY
        if (room > 0)
                string_copy_bounded((p8 *)to, (string_address)from, (positive)room);
#else
        long at = 0;

        if (room <= 0)
                return;
        while (from[at] && at + 1 < room)
        {
                to[at] = from[at];
                at++;
        }
        to[at] = 0;
#endif
}

static unsigned long dbus_get32(const unsigned char *p)
{
        return (unsigned long)p[0] | (unsigned long)p[1] << 8 | (unsigned long)p[2] << 16 | (unsigned long)p[3] << 24;
}

#define DBUS_ALIGN(at, n) (((at) + (n)-1) & ~((long)(n)-1))

/* ---- grammar: UTF-8, names, paths, signatures ---- */

// One valid UTF-8 text with no NUL: the shortest forms, no surrogates, nothing past U+10FFFF; the C walk, a character at a time.
static int dbus_utf8_walk(const unsigned char *s, long n)
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

/*
        The same, for a text with a NUL after it (every caller's: a string of a message is checked for its NUL first, a
        setting's is a C string): from DBUS_UTF8_LIBRARY bytes, lib.c's validator, which holds the same strictness (a lead
        of C2 to F4, no overlong form, no surrogate, nothing past U+10FFFF), and its NUL is the length's, which stops at
        the first.
*/
static int dbus_utf8(const unsigned char *s, long n)
{
#if DBUS_LIBRARY
        if (n >= DBUS_UTF8_LIBRARY)
                return memory_utf8_valid_span((address_any)s, (positive)n).x == (positive)n && string_length((const_string)s) == (positive)n;
#endif
        return dbus_utf8_walk(s, n);
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

// One complete type of a signature, from *at; the depth of arrays, of structs and of dict entries counted apart, each to 32, as libdbus does.
static int dbus_sig_one(const char *s, long n, long *at, int arrays, int structs, int dicts)
{
        char c;

        if (*at >= n || arrays > 32 || structs > 32 || dicts > 32)
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
                        if (!dbus_sig_one(s, n, at, arrays + 1, structs, dicts + 1) || *at >= n || s[(*at)++] != '}')
                                return 0;
                        return 1;
                }
                return dbus_sig_one(s, n, at, arrays + 1, structs, dicts);
        case '(':
        {
                long members = 0;

                while (*at < n && s[*at] != ')')
                {
                        if (!dbus_sig_one(s, n, at, arrays, structs + 1, dicts))
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
                if (!dbus_sig_one(s, n, &at, 0, 0, 0))
                        return 0;
        return 1;
}

// A variant's signature at p[*at] (a byte count, the text, a NUL) that is one complete type; its length, or -1. *at moves past it.
static long dbus_variant_sig(const unsigned char *p, long *at, long limit)
{
        long length, inner = 0, start;

        if (*at + 1 > limit)
                return -1;
        length = p[(*at)++];
        start = *at;
        if (length > limit - *at - 1 || p[*at + length] || !dbus_sig_one((const char *)p + start, length, &inner, 0, 0, 0) || inner != length)
                return -1;
        *at += length + 1;
        return length;
}

/* ---- a body, valid down to its padding ---- */

static int dbus_value(const unsigned char *p, long *at, long limit, const char *sig, long sig_length, long *s, int depth, unsigned long fds);

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
static int dbus_value(const unsigned char *p, long *at, long limit, const char *sig, long sig_length, long *s, int depth, unsigned long fds)
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
                if (c == 'h' && dbus_get32(p + *at) >= fds)
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
                long inner = 0, start = *at + 1;

                length = dbus_variant_sig(p, at, limit);
                return length >= 0 && dbus_value(p, at, limit, (const char *)p + start, length, &inner, depth + 1, fds);
        }
        case 'a':
        {
                long element = *s, after, stop, align, counted, previous = 0, elements = 0;
                char first;

                if (!dbus_pad_zero(p, at, limit, 4) || *at + 4 > limit)
                        return 0;
                length = (long)dbus_get32(p + *at);
                *at += 4;
                counted = *at;
                first = sig[element];
                align = first == '{' || first == '(' ? 8 : dbus_fixed_size(first) ? dbus_fixed_size(first) : first == 's' || first == 'o' || first == 'a' ? 4 : 1;
                if (!dbus_pad_zero(p, at, limit, align) || length < 0 || length > 67108864 || length > limit - *at)
                        return 0;
                stop = *at + length;
                after = element - 1; // the array's own type, from its a, for where the signature goes on
                if (!dbus_sig_one(sig, sig_length, &after, 0, 0, 0))
                        return 0;
                while (*at < stop)
                {
                        long each = element;

                        previous = *at;
                        elements++;
                        if (first == '{')
                        {
                                // a dict entry is a container of its own, a level below the array's (libdbus counts it so)
                                if (!dbus_pad_zero(p, at, stop, 8))
                                        return 0;
                                each++;
                                if (!dbus_value(p, at, stop, sig, sig_length, &each, depth + 2, fds) ||
                                    !dbus_value(p, at, stop, sig, sig_length, &each, depth + 2, fds))
                                        return 0;
                        }
                        else if (!dbus_value(p, at, stop, sig, sig_length, &each, depth + 1, fds))
                                return 0;
                }
                /*
                        GDBus (GLib's gdbusmessage.c, 2.90 at least) counts an array's length from where the length ends, before
                        the padding to its elements' 8: when that padding is 4 bytes, it takes the array to end 4 bytes early,
                        and an element that begins after that point is not read. The last element is lost and what follows is
                        read 4 or more bytes back from where it is: sent (a(ay)y) of [([1,2,3,4],), ([],)] and 9, GDBus reads
                        [([1,2,3,4],)] and 0; where that misreading meets something not valid, GDBus ends its connection. So
                        such an array, valid as it is and read rightly by libdbus, is not taken: that a client would read other
                        values than the bus passed on, or be disconnected by another, is worse than refusing a layout no
                        program has use for (the last of two or more structs or dict entries no more than four bytes).
                        GDBus stops before the last element when the one before it ends where GDBus takes the end to be.
                */
                if (*at != stop || ((first == '(' || first == '{') && counted + 4 == stop - length && elements > 1 && previous >= stop - 4))
                        return 0;
                *s = after;
                return 1;
        }
        case '(':
                if (!dbus_pad_zero(p, at, limit, 8))
                        return 0;
                while (*s < sig_length && sig[*s] != ')')
                        if (!dbus_value(p, at, limit, sig, sig_length, s, depth + 1, fds))
                                return 0;
                (*s)++;
                return 1;
        default:
                return 0;
        }
}

static int dbus_body_valid(const unsigned char *body, long length, const char *sig, unsigned long fds)
{
        long at = 0, s = 0, n = dbus_length(sig);

        while (s < n)
                if (!dbus_value(body, &at, length, sig, n, &s, 0, fds))
                        return 0;
        return at == length;
}

/* ---- a message, parsed ---- */

struct dbus_message
{
        const unsigned char *bytes;
        long size, body_at, body_length;
        unsigned char type, flags;
        unsigned long serial, reply, fds;
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

// 0 when the bytes are one whole message, valid; the message read into m. (A client's own are cut at DBUS_MESSAGE before; one the bus
// stamped its sender on may be up to DBUS_STAMPED.)
static int dbus_parse(const unsigned char *b, long size, struct dbus_message *m)
{
        long fields, limit, at = 16, body, pos;
        unsigned seen = 0;

        m->has = 0;
        m->path = m->interface = m->member = m->error = m->destination = m->sender = m->signature = "";
        m->reply = 0;
        m->fds = 0;
        if (size < 16 || size > DBUS_STAMPED || b[0] != 'l' || b[3] != 1 || b[1] < DBUS_CALL || b[1] > DBUS_SIGNAL)
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
                // a field is a struct of its code and a variant: aligned to 8, the code, the variant's type, its value
                unsigned char code;
                long length, type_at, inner = 0;
                char kind;
                const char *text;

                if (!dbus_pad_zero(b, &at, limit, 8) || at + 1 > limit)
                        return -1;
                code = b[at++];
                type_at = at + 1;
                length = dbus_variant_sig(b, &at, limit);
                // a field this bus does not know is valid as its type says and is not passed on (the stamp writes only the
                // fields below); code 0 is INVALID, which no message carries, and no branch below takes it
                if (length < 0 || !dbus_value(b, &at, limit, (const char *)b + type_at, length, &inner, 3, 0))
                        return -1;
                if (code > 9)
                        continue;
                {
                        static const struct
                        {
                                char kind;
                                signed char name_kind;
                                unsigned short off;
                                unsigned short bit;
                        } hdr_fields[10] = {
                            {0, -1, 0, 0},
                            {'o', -1, (unsigned short)offset_of(struct dbus_message, path), DBUS_HAS_PATH},
                            {'s', 0, (unsigned short)offset_of(struct dbus_message, interface), DBUS_HAS_INTERFACE},
                            {'s', 2, (unsigned short)offset_of(struct dbus_message, member), DBUS_HAS_MEMBER},
                            {'s', 0, (unsigned short)offset_of(struct dbus_message, error), DBUS_HAS_ERROR},
                            {'u', -1, 0, DBUS_HAS_REPLY},
                            {'s', 1, (unsigned short)offset_of(struct dbus_message, destination), DBUS_HAS_DESTINATION},
                            {'s', 1, (unsigned short)offset_of(struct dbus_message, sender), DBUS_HAS_SENDER},
                            {'g', -1, (unsigned short)offset_of(struct dbus_message, signature), DBUS_HAS_SIGNATURE},
                            {'u', -1, 0, DBUS_HAS_FDS},
                        };

                        kind = (char)b[type_at];
                        if (length != 1 || (seen & (1u << code)) || !hdr_fields[code].kind || kind != hdr_fields[code].kind)
                                return -1;
                        seen |= 1u << code;
                        // the value is valid for its type (its UTF-8, its path, its signature), and ends at `at`
                        if (kind == 'u')
                        {
                                unsigned long val = dbus_get32(b + at - 4);

                                if (code == 5)
                                {
                                        if (!val)
                                                return -1; // a reply serial of 0 is none: libdbus ends a connection that is sent one
                                        m->reply = val;
                                }
                                else
                                {
                                        if (val > DBUS_FDS || (val && !dbus_fd_ops.close_fd))
                                                return -1;
                                        m->fds = val;
                                }
                                m->has |= hdr_fields[code].bit;
                        }
                        else
                        {
                                text = kind == 'g' ? (const char *)b + type_at + 3 : (const char *)b + DBUS_ALIGN(type_at + 2, 4) + 4;
                                length = kind == 'g' ? b[type_at + 2] : at - 1 - (text - (const char *)b);
                                if (hdr_fields[code].name_kind >= 0 && !dbus_valid_name(text, length, hdr_fields[code].name_kind))
                                        return -1;
                                *(const char **)((char *)m + hdr_fields[code].off) = text;
                                m->has |= hdr_fields[code].bit;
                        }
                }
        }
        if (at != limit)
                return -1;
        if ((m->type == DBUS_CALL && (!(m->has & DBUS_HAS_PATH) || !(m->has & DBUS_HAS_MEMBER))) ||
            (m->type == DBUS_SIGNAL && (!(m->has & DBUS_HAS_PATH) || !(m->has & DBUS_HAS_INTERFACE) || !(m->has & DBUS_HAS_MEMBER))) ||
            (m->type == DBUS_RETURN && !(m->has & DBUS_HAS_REPLY)) ||
            (m->type == DBUS_ERROR && (!(m->has & DBUS_HAS_REPLY) || !(m->has & DBUS_HAS_ERROR))))
                return -1;
        // the interface and the path a client library's own Disconnected comes from: libdbus and GDBus end a connection sent either
        if (dbus_equal(m->interface, "org.freedesktop.DBus.Local") || dbus_equal(m->path, "/org/freedesktop/DBus/Local"))
                return -1;
        m->body_at = pos;
        m->body_length = body;
        if (body > 0 && !*m->signature)
                return -1;
        if (!dbus_body_valid(b + pos, body, m->signature, m->fds))
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
        long to;

        if (w->bad)
                return;
        to = DBUS_ALIGN(w->length, align);
        if (to > w->room)
        {
                w->bad = 1;
                return;
        }
        for (long k = w->length; k < to; k++)
                w->at[k] = 0;
        w->length = to;
}

static void dbus_o_raw(struct dbus_out *w, const void *bytes, long n)
{
        if (w->bad)
                return;
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

static void dbus_o_field_u32(struct dbus_out *w, unsigned code, unsigned long value)
{
        dbus_o_pad(w, 8);
        dbus_o_u8(w, code);
        dbus_o_u8(w, 1);
        dbus_o_u8(w, 'u');
        dbus_o_u8(w, 0);
        dbus_o_u32(w, value);
}

// A message's header; the body follows, then dbus_o_end.
static void dbus_o_header_fds(struct dbus_out *w, unsigned type, unsigned flags, unsigned long serial, const struct dbus_fields *f, unsigned long fds)
{
        static const struct
        {
                unsigned char code;
                char kind;
                unsigned short off;
        } str_fields[] = {
            {1, 'o', (unsigned short)offset_of(struct dbus_fields, path)},
            {2, 's', (unsigned short)offset_of(struct dbus_fields, interface)},
            {3, 's', (unsigned short)offset_of(struct dbus_fields, member)},
            {4, 's', (unsigned short)offset_of(struct dbus_fields, error)},
            {6, 's', (unsigned short)offset_of(struct dbus_fields, destination)},
            {7, 's', (unsigned short)offset_of(struct dbus_fields, sender)},
            {8, 'g', (unsigned short)offset_of(struct dbus_fields, signature)},
        };
        long fields_at;

        dbus_o_u8(w, 'l');
        dbus_o_u8(w, type);
        dbus_o_u8(w, flags);
        dbus_o_u8(w, 1);
        dbus_o_u32(w, 0);
        dbus_o_u32(w, serial);
        dbus_o_u32(w, 0);
        fields_at = w->length;
        for (long i = 0; i < (long)(sizeof str_fields / sizeof str_fields[0]); i++)
        {
                const char *val = *(const char *const *)((const char *)f + str_fields[i].off);

                if (str_fields[i].code == 6 && f->has_reply)
                        dbus_o_field_u32(w, 5, f->reply);
                if (val && *val)
                        dbus_o_field_string(w, str_fields[i].code, str_fields[i].kind, val);
        }
        if (fds)
                dbus_o_field_u32(w, 9, fds);
        dbus_o_patch32(w, 12, (unsigned long)(w->length - fields_at));
        dbus_o_pad(w, 8);
}

static void dbus_o_header(struct dbus_out *w, unsigned type, unsigned flags, unsigned long serial, const struct dbus_fields *f)
{
        dbus_o_header_fds(w, type, flags, serial, f, 0);
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

// The slot a unique name (:1.N) is, or -1: N read as the number it is (no leading zero), then the client that has it.
static long dbus_unique_slot(const char *name)
{
        long id = 0, at = 3;

        if (name[0] != ':' || name[1] != '1' || name[2] != '.' || name[3] < '1' || name[3] > '9')
                return -1;
        while (at < 15 && name[at] >= '0' && name[at] <= '9')
                id = id * 10 + (name[at++] - '0');
        if (name[at])
                return -1;
        if (id == dbus.service.id)
                return DBUS_SERVICE;
        for (long slot = 0; slot < DBUS_CLIENTS; slot++)
                if (dbus.client[slot] && dbus.client[slot]->hello && dbus.client[slot]->id == id)
                        return slot;
        return -1;
}

/*
        The names: DBUS_NAMES of them, found by their DJB2 hash in DBUS_NAME_BUCKETS buckets, each a list through `next`.
        A call to a well-known name, and a broadcast to every rule that names its sender, asks who owns a name; the table
        was a scan of all of it, which at room for every client's names (1025) made a name not there, or late in it, a
        walk of 1025 entries, a cost any client could put on every lookup by owning names. The hash is lib.c's
        string_hash_33_length where lib.c is (a byte a shift and an add, a word at a time past eight; FNV-1a's chain of
        multiplies made a ReadOne 12% slower), the same DJB2 in C where it is not; a rule's sender has its hash made
        once, and a call to the portal or the bus goes to them by its name before any lookup.
*/
static unsigned dbus_hash(const char *s)
{
#if DBUS_LIBRARY
        return (unsigned)string_hash_33_length((string_address)s).x;
#else
        unsigned long h = 5381;

        while (*s)
                h = (h << 5) + h + (unsigned char)*s++;
        return (unsigned)h;
#endif
}

static struct dbus_name *dbus_name_lookup(const char *name, unsigned hash)
{
        for (long at = dbus.bucket[hash & (DBUS_NAME_BUCKETS - 1)]; at >= 0; at = dbus.name[at].next)
                if (dbus.name[at].hash == hash && dbus_equal(dbus.name[at].text, name))
                        return &dbus.name[at];
        return 0;
}

// A name by its text: the one found last is asked first (a program calls the same service again and again), then its bucket.
static struct dbus_name *dbus_name_find(const char *name)
{
        struct dbus_name *n = dbus.recent >= 0 ? &dbus.name[dbus.recent] : 0;

        if (n && n->used && dbus_equal(n->text, name))
                return n;
        n = dbus_name_lookup(name, dbus_hash(name));
        if (n)
                dbus.recent = (short)(n - dbus.name);
        return n;
}

// A name made, owned by `owner`, and put in its bucket: the entry, or 0 when the table has no room.
static struct dbus_name *dbus_name_add(const char *name, long owner, int replaceable)
{
        struct dbus_name *n = 0;
        unsigned hash = dbus_hash(name);

        for (long at = 0; at < DBUS_NAMES && !n; at++)
                if (!dbus.name[at].used)
                        n = &dbus.name[at];
        if (!n)
                return 0;
        dbus_text(n->text, DBUS_NAME_TEXT, name);
        n->hash = hash;
        n->used = 1;
        n->owner = (short)owner;
        n->replaceable = (unsigned char)(replaceable != 0);
        n->next = dbus.bucket[hash & (DBUS_NAME_BUCKETS - 1)];
        dbus.bucket[hash & (DBUS_NAME_BUCKETS - 1)] = (short)(n - dbus.name);
        return n;
}

// A name taken out of its bucket and freed (its text stays until the entry is made again, for the NameOwnerChanged after).
static void dbus_name_remove(struct dbus_name *n)
{
        short *link = &dbus.bucket[n->hash & (DBUS_NAME_BUCKETS - 1)];

        while (*link >= 0 && &dbus.name[*link] != n)
                link = &dbus.name[*link].next;
        if (*link >= 0)
                *link = n->next;
        n->used = 0;
        n->owner = -1;
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

static long dbus_u64_text(char *into, unsigned long u)
{
#if DBUS_LIBRARY
        long n = (long)positive_into_string((p8 *)into, (positive)u);

        into[n] = 0;
        return n;
#else
        char digits[24];
        long n = 0, at = 0;

        do
                digits[n++] = (char)('0' + u % 10);
        while ((u /= 10) > 0);
        while (n)
                into[at++] = digits[--n];
        into[at] = 0;
        return at;
#endif
}

static void dbus_unique_text(char *into, long id)
{
        into[0] = ':';
        into[1] = '1';
        into[2] = '.';
        dbus_u64_text(into + 3, id > 0 ? (unsigned long)id : 0);
}

#define DBUS_UNASKED_FOR 0 // a call or a signal of another, or the bus's NameOwnerChanged: up to DBUS_UNASKED of the queue
#define DBUS_ASKED_FOR 1   // an answer to the client's own call, or the bus's own words to it: up to DBUS_OUT

/*
        Queued to a client: 1, or 0 when it is not (no client, the service, one that has ended, or no room for it under
        what it is). A message there is no room for is refused, never the client: what it asked for has the room above
        DBUS_UNASKED that nothing unasked can take, so another client's flood cannot crowd out its answers or have it
        dropped. The first refusal since it last read starts its stall clock, which dbus_tick ends it by.
*/
static int dbus_deliver_fds(struct dbus_client *c, const unsigned char *bytes, long n, int asked, const int *fds, long nfds)
{
        if (!c || c->slot == DBUS_SERVICE || c->dead)
                return 0;
        if (nfds > 0 && (!c->unix_fds || c->out_fds + nfds > DBUS_FDS || c->out_fd_runs >= DBUS_FDS))
                return 0;
        if (n < 0 || n > DBUS_OUT || c->out_length - c->out_head + n > (asked ? DBUS_OUT : DBUS_UNASKED))
        {
                if (!c->full_since)
                        c->full_since = dbus.host->now(dbus.host->context) | 1;
                return 0;
        }
        if (c->out_length + n > DBUS_OUT)
        {
                long shift = c->out_head;

                // what is not sent yet goes to the front, once, so that there is room behind it
                dbus_copy(c->out, c->out + c->out_head, c->out_length - c->out_head);
                c->out_length -= c->out_head;
                c->out_head = 0;
                for (long k = 0; k < c->out_fd_runs; k++)
                        c->out_fd_run[k].at -= shift;
        }
        if (nfds > 0)
        {
                c->out_fd_run[c->out_fd_runs].at = c->out_length;
                c->out_fd_run[c->out_fd_runs].count = nfds;
                c->out_fd_runs++;
                for (long k = 0; k < nfds; k++)
                        c->out_fd[c->out_fds++] = fds[k];
        }
        dbus_copy(c->out + c->out_length, bytes, n);
        c->out_length += n;
        return 1;
}

static int dbus_deliver(struct dbus_client *c, const unsigned char *bytes, long n, int asked)
{
        return dbus_deliver_fds(c, bytes, n, asked, 0, 0);
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
                        if (at > limit)
                                return -1;
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
                        if (at >= limit)
                                return -1;
                        length = body[at];
                        if (at + 1 + length + 1 > limit)
                                return -1;
                        at += 1 + length + 1;
                        continue;
                }
                return -1;
        }
        return -1;
}

// Whether the sender a rule names is `from` (-1 for the bus): its unique name, or a well-known name it owns (by the rule's own hash of it).
static int dbus_rule_sender_is(const struct dbus_rule *r, long from)
{
        struct dbus_client *c = dbus_client_of(from);
        struct dbus_name *n;

        if (from < 0 || !c)
                return dbus_equal(r->sender, DBUS_NAME_BUS);
        if (r->sender[0] == ':')
                return dbus_equal(r->sender, c->unique);
        n = dbus_name_lookup(r->sender, r->sender_hash);
        return n && n->owner == from;
}

// Whether a message (sent by `from`, -1 for the bus) is what a rule asks for.
static const struct
{
        const char *name;
        unsigned short offset, room, msg_off;
        unsigned char bit;
} dbus_rule_fields[] = {
    {"sender", (unsigned short)offset_of(struct dbus_rule, sender), DBUS_NAME_TEXT, 0xffff, 1},
    {"interface", (unsigned short)offset_of(struct dbus_rule, interface), DBUS_NAME_TEXT, (unsigned short)offset_of(struct dbus_message, interface), 2},
    {"member", (unsigned short)offset_of(struct dbus_rule, member), 64, (unsigned short)offset_of(struct dbus_message, member), 4},
    {"path", (unsigned short)offset_of(struct dbus_rule, path), 128, (unsigned short)offset_of(struct dbus_message, path), 8},
    {"path_namespace", (unsigned short)offset_of(struct dbus_rule, path_namespace), 128, 0xffff, 16},
    {"destination", (unsigned short)offset_of(struct dbus_rule, destination), DBUS_NAME_TEXT, (unsigned short)offset_of(struct dbus_message, destination), 32},
};

static int dbus_rule_matches(const struct dbus_rule *r, long from, const struct dbus_message *m)
{
        if (r->type && r->type != m->type)
                return 0;
        if ((r->has & 1) && !dbus_rule_sender_is(r, from))
                return 0;
        for (long i = 0; i < (long)(sizeof dbus_rule_fields / sizeof dbus_rule_fields[0]); i++)
                if (dbus_rule_fields[i].msg_off != 0xffff && (r->has & dbus_rule_fields[i].bit) &&
                    !dbus_equal((const char *)r + dbus_rule_fields[i].offset, *(const char *const *)((const char *)m + dbus_rule_fields[i].msg_off)))
                        return 0;
        if (r->has & 16)
        {
                long n = dbus_length(r->path_namespace);

                if (!dbus_starts(m->path, r->path_namespace) || (m->path[n] && m->path[n] != '/' && n > 1))
                        return 0;
        }
        for (long k = 0; k < 4; k++)
                if (r->arg_has & (1u << k))
                {
                        char value[96];
                        long want = dbus_length(r->arg[k]), got = dbus_arg_string(m, k, value, sizeof value);

                        // argN and arg0namespace match a string, argNpath a string or an object path (the argument's type is
                        // signature[k] when there is one: every argument before it is of one character)
                        if (got < 0 || (r->arg_mode[k] != 1 && m->signature[k] != 's'))
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

        dbus_zero(r, sizeof *r);
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
                {
                        static const struct
                        {
                                const char *name;
                                unsigned char code;
                        } rule_types[] = {{"signal", DBUS_SIGNAL}, {"method_call", DBUS_CALL}, {"method_return", DBUS_RETURN}, {"error", DBUS_ERROR}};
                        long fi = DBUS_TABLE_LOOKUP(key, dbus_rule_fields);

                        if (fi < (long)(sizeof(dbus_rule_fields) / sizeof(dbus_rule_fields[0])))
                        {
                                if (vlen >= dbus_rule_fields[fi].room)
                                        return -1;
                                dbus_text((char *)r + dbus_rule_fields[fi].offset, dbus_rule_fields[fi].room, value);
                                r->has |= dbus_rule_fields[fi].bit;
                        }
                        else if (dbus_equal(key, "type"))
                        {
                                long ti = DBUS_TABLE_LOOKUP(value, rule_types);

                                if (ti >= (long)(sizeof(rule_types) / sizeof(rule_types[0])))
                                        return -1;
                                r->type = rule_types[ti].code;
                        }
                        else if (key[0] == 'a' && key[1] == 'r' && key[2] == 'g' && key[3] >= '0' && key[3] <= '3' && vlen < 96 &&
                                 (!key[4] || dbus_equal(key + 4, "path") || (key[3] == '0' && dbus_equal(key + 4, "namespace"))))
                        {
                                r->arg_mode[key[3] - '0'] = !key[4] ? 0 : key[4] == 'p' ? 1 : 2;
                                dbus_text(r->arg[key[3] - '0'], sizeof r->arg[0], value);
                                r->arg_has |= (unsigned char)(1u << (key[3] - '0'));
                        }
                        else if (!dbus_equal(key, "eavesdrop"))
                                return -1;
                }
        }
        r->sender_hash = dbus_hash(r->sender);
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
        for (long i = 0; i < (long)(sizeof dbus_rule_fields / sizeof dbus_rule_fields[0]); i++)
                if (!dbus_equal((const char *)a + dbus_rule_fields[i].offset, (const char *)b + dbus_rule_fields[i].offset))
                        return 0;
        for (long k = 0; k < 4; k++)
                if (!dbus_equal(a->arg[k], b->arg[k]))
                        return 0;
        return 1;
}

/* ---- delivery ---- */

// A message to every client with a rule it matches; from is the sender's slot, -1 for the bus.
static void dbus_broadcast_fds(const unsigned char *bytes, long n, long from, const struct dbus_message *m, const int *fds, long nfds)
{
        for (long at = 0; at < DBUS_CLIENTS; at++)
        {
                struct dbus_client *c = dbus.client[at];

                if (!c || !c->hello || c->dead)
                        continue;
                for (long k = 0; k < c->rules; k++)
                        if (c->rule[k].used && dbus_rule_matches(&c->rule[k], from, m))
                        {
                                if (nfds > 0)
                                {
                                        int duped[DBUS_FDS];
                                        long ok = 1;

                                        if (!c->unix_fds || !dbus_fd_ops.dup_fd)
                                                break;
                                        for (long j = 0; j < nfds; j++)
                                        {
                                                duped[j] = dbus_fd_ops.dup_fd(fds[j]);
                                                if (duped[j] < 0)
                                                {
                                                        dbus_close_fds(duped, j);
                                                        ok = 0;
                                                        break;
                                                }
                                        }
                                        if (ok && !dbus_deliver_fds(c, bytes, n, DBUS_UNASKED_FOR, duped, nfds))
                                                dbus_close_fds(duped, nfds);
                                }
                                else
                                        dbus_deliver(c, bytes, n, DBUS_UNASKED_FOR);
                                break;
                        }
        }
}

static void dbus_broadcast(const unsigned char *bytes, long n, long from, const struct dbus_message *m)
{
        dbus_broadcast_fds(bytes, n, from, m, 0, 0);
}

// A signal of the bus: NameOwnerChanged to those who ask for it, or a unicast one to a client: NameAcquired (what it asked
// for, asked 1) or NameLost (a name another took from it, asked 0).
static void dbus_bus_signal(long to, const char *member, const char *a, const char *b, const char *c, int asked)
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
                dbus_deliver(dbus_client_of(to), w.at, w.length, asked);
                return;
        }
        if (dbus_parse(w.at, w.length, &m) == 0)
                dbus_broadcast(w.at, w.length, -1, &m);
}

static void dbus_name_changed(const char *name, const char *old_owner, const char *new_owner)
{
        dbus_bus_signal(-1, "NameOwnerChanged", name, old_owner, new_owner, DBUS_UNASKED_FOR);
}

/* ---- the bus's own methods ---- */

static void dbus_responses_send(struct dbus_client *c, long now, const struct dbus_rule *only);

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
                dbus_deliver(to, w.at, w.length, DBUS_ASKED_FOR);
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
                dbus_deliver(to, w->at, w->length, DBUS_ASKED_FOR);
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

/*
        The names no client may own (MW-2026-197). The bus is every bowl's and the host's, and whoever owns a name is sent
        what every program of every bowl sends to it. org.a11y.Bus answers GetAddress, and a GTK 4 or Qt program connects to
        the address it is given and sends there every text it shows and every key it is given (AT-SPI); org.freedesktop.secrets
        is where libsecret, and so Chromium, Electron and VS Code, keep a profile's passwords; org.freedesktop.Notifications is
        sent the text of every notification (a sign-in code among them), and is the bus's own to serve. Nobody serves the first
        two here, and a program is told so at once (ServiceUnknown): GTK 4 asks for the accessibility bus with a call it waits on.
*/
static const char *const dbus_kept[] = {"org.a11y.Bus", "org.freedesktop.secrets", "org.freedesktop.Notifications", 0};

static int dbus_name_kept(const char *name)
{
        for (long k = 0; dbus_kept[k]; k++)
                if (dbus_equal(dbus_kept[k], name))
                        return 1;
        return 0;
}

static void dbus_name_release(long slot, struct dbus_name *n)
{
        struct dbus_client *c = dbus_client_of(slot);

        if (!n || !n->used || n->owner != slot)
                return;
        dbus_name_remove(n);
        if (c && c->names > 0)
                c->names--;
        dbus_name_changed(n->text, c ? c->unique : "", "");
}

/*
        RequestName: 1 primary owner, 3 exists, 4 already owner, or 0 when the client may own no more names (or the table
        has no room), which its caller answers as LimitsExceeded. The flags: 1 allow replacement, 2 replace existing, 4 do
        not queue; there is no queue, so a name that is taken is 3 whatever the flags. An owner that asks again has its
        allow-replacement set as it asks now, as dbus-daemon does.
*/
static unsigned long dbus_request_name(struct dbus_client *c, const char *name, unsigned long flags)
{
        struct dbus_name *n = dbus_name_find(name);

        if (n)
        {
                struct dbus_client *old;

                if (n->owner == c->slot)
                {
                        n->replaceable = (flags & 1) != 0;
                        return 4;
                }
                if (n->owner == DBUS_SERVICE || !(flags & 2) || !n->replaceable)
                        return 3;
                if (c->names >= DBUS_NAMES_PER)
                        return 0;
                old = dbus_client_of(n->owner);
                dbus_bus_signal(n->owner, "NameLost", name, 0, 0, DBUS_UNASKED_FOR);
                if (old && old->names > 0)
                        old->names--;
                n->owner = (short)c->slot;
                n->replaceable = (flags & 1) != 0;
                c->names++;
                dbus_name_changed(name, old ? old->unique : "", c->unique);
                dbus_bus_signal(c->slot, "NameAcquired", name, 0, 0, DBUS_ASKED_FOR);
                return 1;
        }
        if (c->names >= DBUS_NAMES_PER || !dbus_name_add(name, c->slot, (flags & 1) != 0))
                return 0;
        c->names++;
        dbus_name_changed(name, "", c->unique);
        dbus_bus_signal(c->slot, "NameAcquired", name, 0, 0, DBUS_ASKED_FOR);
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
        // ListActivatableNames is the bus and the portal (the one service there is, always running); ListNames every name
        if (!all)
                dbus_o_string(&w, DBUS_NAME_PORTAL);
        if (all)
        {
                for (long at = 0; at < DBUS_NAMES; at++)
                        if (dbus.name[at].used)
                                dbus_o_string(&w, dbus.name[at].text);
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
        enum
        {
                DBUS_M_HELLO, DBUS_M_REQUEST_NAME, DBUS_M_RELEASE_NAME, DBUS_M_LIST_NAMES, DBUS_M_LIST_ACTIVATABLE,
                DBUS_M_NAME_HAS_OWNER, DBUS_M_GET_NAME_OWNER, DBUS_M_GET_UID, DBUS_M_GET_PID, DBUS_M_GET_CREDS,
                DBUS_M_START_SERVICE, DBUS_M_ADD_MATCH, DBUS_M_REMOVE_MATCH, DBUS_M_GET_ID, DBUS_M_NOOP,
        };
        static const struct
        {
                const char *name;
                unsigned char op;
        } methods[] = {
            {"Hello", DBUS_M_HELLO},
            {"RequestName", DBUS_M_REQUEST_NAME},
            {"ReleaseName", DBUS_M_RELEASE_NAME},
            {"ListNames", DBUS_M_LIST_NAMES},
            {"ListActivatableNames", DBUS_M_LIST_ACTIVATABLE},
            {"NameHasOwner", DBUS_M_NAME_HAS_OWNER},
            {"GetNameOwner", DBUS_M_GET_NAME_OWNER},
            {"GetConnectionUnixUser", DBUS_M_GET_UID},
            {"GetConnectionUnixProcessID", DBUS_M_GET_PID},
            {"GetConnectionCredentials", DBUS_M_GET_CREDS},
            {"StartServiceByName", DBUS_M_START_SERVICE},
            {"AddMatch", DBUS_M_ADD_MATCH},
            {"RemoveMatch", DBUS_M_REMOVE_MATCH},
            {"GetId", DBUS_M_GET_ID},
            {"UpdateActivationEnvironment", DBUS_M_NOOP},
            {"ReloadConfig", DBUS_M_NOOP},
        };
        char text[DBUS_NAME_TEXT + 160];
        long length, mi;

        if (dbus_common(from, m, DBUS_NAME_BUS, dbus_introspect_bus))
                return;
        if (m->interface[0] && !dbus_equal(m->interface, DBUS_NAME_BUS))
        {
                dbus_error_reply(from, m, "org.freedesktop.DBus.Error.UnknownMethod", "No such interface", DBUS_NAME_BUS);
                return;
        }
        mi = DBUS_TABLE_LOOKUP(m->member, methods);
        if (mi >= (long)(sizeof(methods) / sizeof(methods[0])))
        {
                dbus_error_reply(from, m, "org.freedesktop.DBus.Error.UnknownMethod", "No such method", DBUS_NAME_BUS);
                return;
        }
        switch (methods[mi].op)
        {
        case DBUS_M_HELLO:
                dbus_error_reply(from, m, "org.freedesktop.DBus.Error.Failed", "Already handled an Hello message", DBUS_NAME_BUS);
                return;
        case DBUS_M_REQUEST_NAME:
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
                if (dbus_name_kept(text))
                {
                        dbus_error_reply(from, m, "org.freedesktop.DBus.Error.AccessDenied",
                                         "That name is the bus's to keep: its owner would be sent what every program sends it", DBUS_NAME_BUS);
                        return;
                }
                flags = dbus_request_name(from, text, flags);
                if (!flags)
                        dbus_error_reply(from, m, "org.freedesktop.DBus.Error.LimitsExceeded", "No more names can be owned", DBUS_NAME_BUS);
                else
                        dbus_reply_u32(from, m, flags, DBUS_NAME_BUS);
                return;
        }
        case DBUS_M_RELEASE_NAME:
        {
                struct dbus_name *n;

                if (!dbus_equal(m->signature, "s") || (length = dbus_call_string(m, text, sizeof text)) < 0 || text[0] == ':' ||
                    !dbus_name_ok(text) || dbus_equal(text, DBUS_NAME_BUS))
                {
                        dbus_error_reply(from, m, "org.freedesktop.DBus.Error.InvalidArgs", "ReleaseName takes a well-known name", DBUS_NAME_BUS);
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
        case DBUS_M_LIST_NAMES:
        case DBUS_M_LIST_ACTIVATABLE:
                dbus_reply_names(from, m, methods[mi].op == DBUS_M_LIST_NAMES);
                return;
        case DBUS_M_NAME_HAS_OWNER:
        case DBUS_M_GET_NAME_OWNER:
        case DBUS_M_GET_UID:
        case DBUS_M_GET_PID:
        case DBUS_M_GET_CREDS:
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
                if (methods[mi].op == DBUS_M_NAME_HAS_OWNER)
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
                if (methods[mi].op == DBUS_M_GET_NAME_OWNER)
                {
                        dbus_reply_string(from, m, slot == -2 ? DBUS_NAME_BUS : of->unique, DBUS_NAME_BUS);
                        return;
                }
                if (slot == -2)
                {
                        dbus_error_reply(from, m, "org.freedesktop.DBus.Error.Failed", "The bus has no credentials", DBUS_NAME_BUS);
                        return;
                }
                if (methods[mi].op == DBUS_M_GET_UID)
                        dbus_reply_u32(from, m, (unsigned long)of->uid, DBUS_NAME_BUS);
                else if (methods[mi].op == DBUS_M_GET_PID)
                        dbus_reply_u32(from, m, (unsigned long)of->pid, DBUS_NAME_BUS);
                else
                        dbus_reply_credentials(from, m, of);
                return;
        }
        case DBUS_M_START_SERVICE:
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
        case DBUS_M_ADD_MATCH:
        case DBUS_M_REMOVE_MATCH:
        {
                struct dbus_rule rule;
                long free_at = -1, found = -1;
                char rule_text[DBUS_RULE_TEXT];

                if (!dbus_equal(m->signature, "s") || dbus_call_string(m, rule_text, sizeof rule_text) < 0)
                {
                        dbus_error_reply(from, m, "org.freedesktop.DBus.Error.InvalidArgs", "A match rule is a string", DBUS_NAME_BUS);
                        return;
                }
                if (dbus_rule_parse(rule_text, &rule) < 0)
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
                if (methods[mi].op == DBUS_M_ADD_MATCH)
                {
                        if (free_at < 0)
                        {
                                dbus_error_reply(from, m, "org.freedesktop.DBus.Error.LimitsExceeded", "Too many match rules", DBUS_NAME_BUS);
                                return;
                        }
                        from->rule[free_at] = rule;
                        if (from->rules < free_at + 1)
                                from->rules = free_at + 1;
                }
                else if (found < 0)
                {
                        dbus_error_reply(from, m, "org.freedesktop.DBus.Error.MatchRuleNotFound", "No such match rule", DBUS_NAME_BUS);
                        return;
                }
                else
                {
                        from->rule[found].used = 0;
                        while (from->rules > 0 && !from->rule[from->rules - 1].used)
                                from->rules--;
                }
                dbus_reply_empty(from, m, DBUS_NAME_BUS);
                // a portal Response held for want of a rule goes once there is one: the client subscribed after its call (Qt)
                if (methods[mi].op == DBUS_M_ADD_MATCH && from->responses)
                        dbus_responses_send(from, dbus.host->now(dbus.host->context), &from->rule[free_at]);
                return;
        }
        case DBUS_M_GET_ID:
                dbus_reply_string(from, m, dbus.guid, DBUS_NAME_BUS);
                return;
        case DBUS_M_NOOP:
                dbus_reply_empty(from, m, DBUS_NAME_BUS);
                return;
        }
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
    "</interface>\n"
    "<interface name=\"org.freedesktop.host.portal.Registry\">\n"
    "<method name=\"Register\"><arg type=\"s\" direction=\"in\"/><arg type=\"a{sv}\" direction=\"in\"/></method>\n"
    "<property name=\"version\" type=\"u\" access=\"read\"/>\n"
    "</interface>\n"
    "<interface name=\"org.freedesktop.portal.Inhibit\">\n"
    "<method name=\"Inhibit\"><arg type=\"s\" direction=\"in\"/><arg type=\"u\" direction=\"in\"/><arg type=\"a{sv}\" direction=\"in\"/><arg type=\"o\" direction=\"out\"/></method>\n"
    "<method name=\"CreateMonitor\"><arg type=\"s\" direction=\"in\"/><arg type=\"a{sv}\" direction=\"in\"/><arg type=\"o\" direction=\"out\"/></method>\n"
    "<property name=\"version\" type=\"u\" access=\"read\"/>\n"
    "</interface>\n"
    "<interface name=\"org.freedesktop.portal.FileChooser\">\n"
    "<method name=\"OpenFile\"><arg type=\"s\" direction=\"in\"/><arg type=\"s\" direction=\"in\"/><arg type=\"a{sv}\" direction=\"in\"/><arg type=\"o\" direction=\"out\"/></method>\n"
    "<method name=\"SaveFile\"><arg type=\"s\" direction=\"in\"/><arg type=\"s\" direction=\"in\"/><arg type=\"a{sv}\" direction=\"in\"/><arg type=\"o\" direction=\"out\"/></method>\n"
    "<method name=\"SaveFiles\"><arg type=\"s\" direction=\"in\"/><arg type=\"s\" direction=\"in\"/><arg type=\"a{sv}\" direction=\"in\"/><arg type=\"o\" direction=\"out\"/></method>\n"
    "<property name=\"version\" type=\"u\" access=\"read\"/>\n"
    "</interface>\n"
    "<interface name=\"org.freedesktop.portal.Notification\">\n"
    "<method name=\"AddNotification\"><arg type=\"s\" direction=\"in\"/><arg type=\"a{sv}\" direction=\"in\"/></method>\n"
    "<method name=\"RemoveNotification\"><arg type=\"s\" direction=\"in\"/></method>\n"
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

/*
        Whether a namespace is one a ReadAll asks for, as xdg-desktop-portal reads its patterns: there are none, or one is
        empty, or one is the namespace, or one ends in * and the namespace starts with what is before it (org.gnome.*
        is org.gnome.desktop.interface, and not org.gnomeish). The patterns are the call's body, an array of strings
        the parser has held valid, read where they are: any number of them, of any length.
*/
static int dbus_ns_wanted(const char *ns, const unsigned char *body)
{
        long limit = 4 + (long)dbus_get32(body), at = 4;

        if (limit == 4)
                return 1;
        while (at < limit)
        {
                long n, k = 0;
                const char *pattern;

                at = DBUS_ALIGN(at, 4);
                n = (long)dbus_get32(body + at);
                pattern = (const char *)body + at + 4;
                at += 4 + n + 1;
                if (!n || dbus_equal(ns, pattern))
                        return 1;
                if (pattern[n - 1] != '*')
                        continue;
                while (k < n - 1 && ns[k] == pattern[k])
                        k++;
                if (k == n - 1)
                        return 1;
        }
        return 0;
}

static void dbus_portal_read_all(struct dbus_client *from, const struct dbus_message *m)
{
        struct dbus_out w;
        long body, outer_at, outer_first;
        const unsigned char *b = m->bytes + m->body_at;

        if (!dbus_equal(m->signature, "as"))
        {
                dbus_error_reply(from, m, "org.freedesktop.DBus.Error.InvalidArgs", "ReadAll takes an array of namespaces", DBUS_NAME_PORTAL);
                return;
        }
        dbus_reply_begin(&w, from, m, "a{sa{sv}}", dbus.service.unique, &body);
        dbus_o_u32(&w, 0);
        outer_at = w.length - 4;
        dbus_o_pad(&w, 8);
        outer_first = w.length;
        for (long a = 0; a < DBUS_SETTINGS; a++)
        {
                const struct dbus_setting *s = &dbus.setting[a];
                int first_of_ns = 1;
                long inner_at, inner_first;

                if (!s->used || !dbus_ns_wanted(s->ns, b))
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
        FileChooser is claimed at version 1 for a reason, where the host has no chooser: Chromium asks for the version of
        the FileChooser portal at startup to learn whether there is a portal, and reads the Settings (the color-scheme, and
        with it prefers-color-scheme) only once that has been answered. It uses the portal for its file dialogs only from
        version 3, so at 1 its dialogs stay the toolkit's, and it never calls a method of this interface; a program
        that does (GTK 4 asked to use the portal, Qt's portal theme) is answered a request that ends at once as
        cancelled (dbus_portal_request). A host with a chooser (the sessionbus's: Files, dbus_portal_choose) claims 4, and
        then Chromium's and Electron's file dialogs are Files. (test/wayland_chromium.py holds Chromium's side with no
        chooser: the theme follows, and a click on a file input calls no OpenFile.)
*/
static struct
{
        char name[56];
        unsigned char version;
} dbus_interfaces[8] = {{"org.freedesktop.portal.Settings", 2},
                        {"org.freedesktop.portal.FileChooser", 1},
                        {"org.freedesktop.host.portal.Registry", 1},
                        {"org.freedesktop.portal.Inhibit", 1},
                        {"org.freedesktop.portal.Notification", 1}};

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
        // one the table has already answers the version claimed now (a host with a chooser claims FileChooser's)
        for (long at = 0; at < 8; at++)
                if (dbus_interfaces[at].version && dbus_equal(dbus_interfaces[at].name, name))
                {
                        if (!version || version > 255)
                                return -1;
                        dbus_interfaces[at].version = (unsigned char)version;
                        return 0;
                }
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

/*
        The portal's requests (org.freedesktop.portal.Request). A portal method that would ask the person something (a
        file to open, a monitor of the session) answers at once with the path of a request object, and its outcome comes
        after, as that object's Response signal: (u response, a{sv} results), 0 done, 1 cancelled by the person, 2 ended
        another way. A client that is answered a path and never sent the Response waits on it for ever: an upload dialog
        that never opens and never closes. So every request this portal answers has its Response, and the request's path
        is the one the specification makes, /org/freedesktop/portal/desktop/request/SENDER/TOKEN (SENDER the caller's
        unique name, its colon taken off and its dots made underscores; TOKEN the handle_token of the call's options),
        because a client subscribes to that path before it calls (GDBus, libportal, Chromium) or once the path is back to
        it (Qt): a Response sent before the client has a rule for it is one it never reads. So a Response is sent to its
        caller alone (a signal with a destination: no other client learns what it asked) when the caller has a rule it
        matches: at once when it has one already, at its AddMatch when it subscribes after, and after DBUS_RESPONSE_MS
        whatever it has. A client holds DBUS_RESPONSES of them; one past that is sent at once.
*/
#define DBUS_REQUEST_PREFIX "/org/freedesktop/portal/desktop/request/"
#define DBUS_TOKEN 64 // the longest handle_token taken (one longer, or not a path's element, is replaced by the bus's own)

// When the bus has something to do at `due`: dbus_tick acts at the earliest.
static void dbus_due_at(long due)
{
        if (!dbus.due || due < dbus.due)
                dbus.due = due;
}

// Where argument `index` of a message begins in its body (before its own padding; the arguments before it skipped by their types, the
// body valid as the parser held it), with *s at its type in the signature: the offset, or -1 when there are not that many.
static long dbus_arg_at(const struct dbus_message *m, long index, long *s)
{
        const unsigned char *p = m->bytes + m->body_at;
        long at = 0, n = dbus_length(m->signature);

        *s = 0;
        for (long k = 0; k < index; k++)
                if (*s >= n || !dbus_value(p, &at, m->body_length, m->signature, n, s, 0, m->fds))
                        return -1;
        return *s < n ? at : -1;
}

// A string argument where it is (its bytes in the body, not copied): its length, or -1 when argument `index` is not a string.
static long dbus_arg_text(const struct dbus_message *m, long index, const unsigned char **text)
{
        long s, at = dbus_arg_at(m, index, &s);

        if (at < 0 || m->signature[s] != 's')
                return -1;
        at = DBUS_ALIGN(at, 4);
        *text = m->bytes + m->body_at + at + 4;
        return (long)dbus_get32(m->bytes + m->body_at + at);
}

// Where the value of `key` is in the a{sv} that is argument `index`, when the variant there holds one value of type `type`: its offset in
// the body (aligned for the type), or -1.
static long dbus_option_at(const struct dbus_message *m, long index, const char *key, char type)
{
        const unsigned char *p = m->bytes + m->body_at;
        const char *sig = m->signature;
        long s, at = dbus_arg_at(m, index, &s), n = dbus_length(sig), limit = m->body_length, stop;

        if (at < 0 || s + 4 > n || sig[s] != 'a' || sig[s + 1] != '{' || sig[s + 2] != 's' || sig[s + 3] != 'v')
                return -1;
        at = DBUS_ALIGN(at, 4);
        if (at + 4 > limit)
                return -1;
        stop = (long)dbus_get32(p + at); // the elements' bytes, from the first one's start (after the padding to 8)
        at = DBUS_ALIGN(at + 4, 8);
        stop += at;
        if (stop > limit)
                return -1;
        while (at < stop)
        {
                long key_at, sig_at, ks = 0, vs = 0, size;

                at = DBUS_ALIGN(at, 8);
                key_at = at;
                if (!dbus_value(p, &at, stop, "s", 1, &ks, 0, 0))
                        return -1;
                sig_at = at;
                if (!dbus_value(p, &at, stop, "v", 1, &vs, 0, m->fds))
                        return -1;
                if (p[sig_at] != 1 || p[sig_at + 1] != (unsigned char)type || !dbus_equal((const char *)p + key_at + 4, key))
                        continue;
                size = dbus_fixed_size(type);
                return DBUS_ALIGN(sig_at + 3, size ? size : type == 's' || type == 'o' ? 4 : 1);
        }
        return -1;
}

// The string value of `key` in the a{sv} that is argument `index` of a message: its length, or -1 when there is no such key holding
// a string, or it does not fit room.
static long dbus_option_string(const struct dbus_message *m, long index, const char *key, char *into, long room)
{
        long at = dbus_option_at(m, index, key, 's'), length;

        if (at < 0)
                return -1;
        length = (long)dbus_get32(m->bytes + m->body_at + at);
        if (length + 1 > room)
                return -1;
        dbus_copy(into, m->bytes + m->body_at + at + 4, length);
        into[length] = 0;
        return length;
}

// A request's path for a caller: the specification's, with the caller's token when it is one element of a path (letters, digits
// and underscores, at most DBUS_TOKEN), else the bus's own (moonwater and a number).
static void dbus_request_path(const struct dbus_client *c, const char *token, char *into)
{
        long n = 0, length = dbus_length(token);
        int ok = length > 0 && length <= DBUS_TOKEN;

        for (long k = 0; ok && k < length; k++)
                ok = dbus_letter(token[k]) || dbus_digit(token[k]);
        for (long k = 0; DBUS_REQUEST_PREFIX[k]; k++)
                into[n++] = DBUS_REQUEST_PREFIX[k];
        for (long k = 1; c->unique[k]; k++)
                into[n++] = c->unique[k] == '.' ? '_' : c->unique[k];
        into[n++] = '/';
        if (ok)
                for (long k = 0; k < length; k++)
                        into[n++] = token[k];
        else
        {
                for (long k = 0; "moonwater"[k]; k++)
                        into[n++] = "moonwater"[k];
                n += dbus_u64_text(into + n, ++dbus.requests);
        }
        into[n] = 0;
}

// The Response of a request to its caller, written to dbus.scratch and parsed into m: its length, or -1.
static long dbus_response_write(const struct dbus_client *c, const struct dbus_response *r, struct dbus_message *m)
{
        struct dbus_out w = {dbus.scratch, 0, DBUS_MESSAGE, 0};
        struct dbus_fields f = {r->path, "org.freedesktop.portal.Request", "Response", "", c->unique, dbus.service.unique, "ua{sv}", 0, 0};
        long body;

        dbus_o_header(&w, DBUS_SIGNAL, DBUS_FLAG_NO_REPLY, dbus_next_serial(), &f);
        body = w.length;
        dbus_o_u32(&w, r->code);
        dbus_o_u32(&w, 0); // the results: none
        dbus_o_pad(&w, 8);
        dbus_o_end(&w, body);
        return w.bad || dbus_parse(w.at, w.length, m) < 0 ? -1 : w.length;
}

// Whether one of a client's rules (or the one rule given) is a rule the Response matches.
static int dbus_response_ruled(const struct dbus_client *c, const struct dbus_message *m, const struct dbus_rule *only)
{
        if (only)
                return dbus_rule_matches(only, DBUS_SERVICE, m);
        for (long k = 0; k < c->rules; k++)
                if (c->rule[k].used && dbus_rule_matches(&c->rule[k], DBUS_SERVICE, m))
                        return 1;
        return 0;
}

// The held Responses of a client sent where they are due (now >= due), or where a rule matches them (all of them, or `only` given).
static void dbus_responses_send(struct dbus_client *c, long now, const struct dbus_rule *only)
{
        for (long k = 0; c->responses && k < DBUS_RESPONSES; k++)
        {
                struct dbus_response *r = &c->response[k];
                struct dbus_message m;
                long n;

                if (!r->due)
                        continue;
                n = dbus_response_write(c, r, &m);
                if (n > 0 && now < r->due && !dbus_response_ruled(c, &m, only))
                {
                        dbus_due_at(r->due);
                        continue;
                }
                if (n > 0)
                        dbus_deliver(c, dbus.scratch, n, DBUS_ASKED_FOR);
                r->due = 0;
                c->responses--;
        }
}

// A request's Response owed to its caller: sent now when a rule of the caller matches it or there is no room to hold it, else held.
static void dbus_response_owe(struct dbus_client *c, const char *path, unsigned long code)
{
        struct dbus_response fresh, *r = 0;
        struct dbus_message m;
        long n;

        fresh.due = dbus.host->now(dbus.host->context) + DBUS_RESPONSE_MS;
        fresh.due += !fresh.due;
        fresh.code = code;
        dbus_text(fresh.path, sizeof fresh.path, path);
        for (long k = 0; k < DBUS_RESPONSES && !r; k++)
                if (!c->response[k].due)
                        r = &c->response[k];
        n = dbus_response_write(c, &fresh, &m);
        if (n <= 0)
                return;
        if (!r || dbus_response_ruled(c, &m, 0))
        {
                dbus_deliver(c, dbus.scratch, n, DBUS_ASKED_FOR);
                return;
        }
        *r = fresh;
        c->responses++;
        dbus_due_at(fresh.due);
}

// A portal method that is a request: answered with its request's path (from the handle_token of its options, argument `options`), and
// the request's Response with `code` after it, when the caller can read it.
static void dbus_portal_request(struct dbus_client *from, const struct dbus_message *m, long options, unsigned long code)
{
        char token[DBUS_TOKEN + 8], path[DBUS_REQUEST_PATH];
        struct dbus_out w;
        long body;

        if (dbus_option_string(m, options, "handle_token", token, sizeof token) < 0)
                token[0] = 0;
        dbus_request_path(from, token, path);
        dbus_reply_begin(&w, from, m, "o", dbus.service.unique, &body);
        dbus_o_string(&w, path);
        dbus_reply_end(&w, from, m, body);
        dbus_response_owe(from, path, code);
}

// Request.Close of a request: its Response, held, is not sent (the specification: a closed request emits none).
static void dbus_choice_close(struct dbus_client *c, const char *path);

static void dbus_request_close(struct dbus_client *from, const char *path)
{
        dbus_choice_close(from, path);
        for (long k = 0; from->responses && k < DBUS_RESPONSES; k++)
                if (from->response[k].due && dbus_equal(from->response[k].path, path))
                {
                        from->response[k].due = 0;
                        from->responses--;
                }
}

/*
        The desktop's notifications: org.freedesktop.Notifications (Chromium, Electron, libnotify, GLib's GNotification outside a
        sandbox) and the portal's org.freedesktop.portal.Notification (GLib's inside one), served by the bus into DBUS_NOTICES
        records its host is given (dbus_host.notices: the sessionbus puts them on the desktop's board, which the notices popup
        reads). Any client may post one, so (MW-2026-198) everything of it is bounded: a program's name to DBUS_NOTICE_APP and its
        summary and body together to DBUS_NOTICE_TEXT, each cut at a character's end; a client posts DBUS_NOTICE_BURST and the bus
        DBUS_NOTICE_ALL in DBUS_NOTICE_SPAN_MS, and one past that is LimitsExceeded; the oldest of DBUS_NOTICES gives way to a new one;
        a notification lives the time it names, from DBUS_NOTICE_SHORTEST to DBUS_NOTICE_MS (no time named is DBUS_NOTICE_MS), and
        one that names 0 stays until it is closed or gives way. A notification is replaced and closed only by the client that
        posted it, and NotificationClosed goes to that client alone: no other learns its id. The text is put down as the kit puts
        down a stranger's words (ui_label_foreign): a control byte is its picture (U+2400 on, U+2421 for delete), so a newline or an
        escape moves nothing; a C1 control, and every mark that reorders or hides text (the embeddings, overrides and isolates,
        U+202A to U+202E and U+2066 to U+2069; U+200E, U+200F, U+061C; the line and paragraph separators; the zero width space, the
        word joiner and the byte order mark) is U+FFFD, seen where it stands and without its effect. Only "body" is said among the
        capabilities: no markup, no actions, no icons.
*/

// A stranger's text, whole and valid UTF-8 (a string the parser held so), written into room bytes with its NUL as the kit would draw
// it, stopping at the last whole character that fits; its length.
static long dbus_foreign(char *into, long room, const unsigned char *s, long n)
{
        long used = 0;

        for (long at = 0; at < n && room > 0;)
        {
                unsigned long code = s[at];
                long more = code < 0x80 ? 0 : code < 0xe0 ? 1 : code < 0xf0 ? 2 : 3, size;

                code = more == 0 ? code : more == 1 ? code & 0x1f : more == 2 ? code & 0x0f : code & 0x07;
                for (long k = 1; k <= more; k++)
                        code = code << 6 | (s[at + k] & 0x3fu);
                at += more + 1;
                if (code < 0x20)
                        code += 0x2400;
                else if (code == 0x7f)
                        code = 0x2421;
                else if ((code >= 0x80 && code < 0xa0) || (code >= 0x202a && code <= 0x202e) || (code >= 0x2066 && code <= 0x2069) || code == 0x200e ||
                         code == 0x200f || code == 0x061c || code == 0x2028 || code == 0x2029 || code == 0x200b || code == 0x2060 || code == 0xfeff)
                        code = 0xfffd;
                size = code < 0x80 ? 1 : code < 0x800 ? 2 : code < 0x10000 ? 3 : 4;
                if (used + size + 1 > room)
                        break;
                if (size == 1)
                        into[used] = (char)code;
                else
                {
                        into[used] = (char)((size == 2 ? 0xc0 : size == 3 ? 0xe0 : 0xf0) | code >> (6 * (size - 1)));
                        for (long k = 1; k < size; k++)
                                into[used + k] = (char)(0x80 | ((code >> (6 * (size - 1 - k))) & 0x3f));
                }
                used += size;
        }
        if (room > 0)
                into[used] = 0;
        return used;
}

static void dbus_notices_told(void)
{
        if (dbus.host->notices)
                dbus.host->notices(dbus.host->context, dbus.notice, DBUS_NOTICES);
}

// A notification ended: NotificationClosed(id, reason) to the client that posted it when it is still here (1 expired or gave way, 3 closed).
static void dbus_notice_end(struct dbus_notice *n, unsigned long reason)
{
        long slot = -1;

        for (long at = 0; at < DBUS_CLIENTS && slot < 0; at++)
                if (dbus.client[at] && dbus.client[at]->hello && dbus.client[at]->id == n->owner)
                        slot = at;
        if (slot >= 0 && !n->portal)
        {
                struct dbus_out w = {dbus.scratch, 0, DBUS_MESSAGE, 0};
                struct dbus_fields f = {DBUS_PATH_NOTIFY, DBUS_NAME_NOTIFY, "NotificationClosed", "", dbus.client[slot]->unique, dbus.service.unique, "uu", 0, 0};
                long body;

                dbus_o_header(&w, DBUS_SIGNAL, DBUS_FLAG_NO_REPLY, dbus_next_serial(), &f);
                body = w.length;
                dbus_o_u32(&w, n->id);
                dbus_o_u32(&w, reason);
                dbus_o_end(&w, body);
                if (!w.bad)
                        dbus_deliver(dbus.client[slot], w.at, w.length, DBUS_ASKED_FOR);
        }
        dbus_zero(n, sizeof *n);
}

// Whether a client may post one more now: its span's count and the bus's, each started again when its span is over.
static int dbus_notice_room(struct dbus_client *c, long now)
{
        if (!c->notice_count || now - c->notice_span >= DBUS_NOTICE_SPAN_MS)
                c->notice_span = now, c->notice_count = 0;
        if (!dbus.notice_count || now - dbus.notice_span >= DBUS_NOTICE_SPAN_MS)
                dbus.notice_span = now, dbus.notice_count = 0;
        if (c->notice_count >= DBUS_NOTICE_BURST || dbus.notice_count >= DBUS_NOTICE_ALL)
                return 0;
        c->notice_count++;
        dbus.notice_count++;
        return 1;
}

/*
        A notification posted: in the place of the poster's own `replaces` (a Notify's replaces_id) or `key` (a portal's id) when it has
        one by that, else in a free record, else in the oldest's (which is ended, 1). Its id, or 0 when the client may post no more now.
*/
static unsigned long dbus_notice_post(struct dbus_client *c, unsigned long replaces, const char *key, const unsigned char *app, long app_n,
                                      const unsigned char *summary, long summary_n, const unsigned char *body, long body_n, unsigned urgency, long expire)
{
        long now = dbus.host->now(dbus.host->context), used;
        struct dbus_notice *n = 0, *oldest = 0;

        if (!dbus_notice_room(c, now))
                return 0;
        for (long k = 0; k < DBUS_NOTICES && !n; k++)
        {
                struct dbus_notice *e = &dbus.notice[k];

                if (e->id && e->owner == c->id && ((key && e->portal && dbus_equal(e->key, key)) || (!key && !e->portal && replaces && e->id == replaces)))
                        n = e;
        }
        for (long k = 0; k < DBUS_NOTICES && !n; k++)
                if (!dbus.notice[k].id)
                        n = &dbus.notice[k];
                else if (!oldest || dbus.notice[k].order < oldest->order)
                        oldest = &dbus.notice[k];
        if (!n)
        {
                n = oldest;
                dbus_notice_end(n, 1);
        }
        if (!n->id)
        {
                n->id = ++dbus.notice_ids;
                n->id += !n->id;
        }
        n->order = ++dbus.notice_order;
        n->owner = c->id;
        n->urgency = (unsigned char)(urgency > 2 ? 1 : urgency);
        n->portal = key != 0;
        dbus_text(n->key, sizeof n->key, key ? key : "");
        if (!dbus_foreign(n->app, sizeof n->app, app, app_n))
                dbus_text(n->app, sizeof n->app, "a program");
        used = dbus_foreign(n->text, sizeof n->text, summary, summary_n);
        if (body_n > 0 && used + 4 < (long)sizeof n->text)
        {
                dbus_text(n->text + used, (long)sizeof n->text - used, " - ");
                used += 3;
                dbus_foreign(n->text + used, (long)sizeof n->text - used, body, body_n);
        }
        expire = expire < 0 ? DBUS_NOTICE_MS : expire > DBUS_NOTICE_MS ? DBUS_NOTICE_MS : expire > 0 && expire < DBUS_NOTICE_SHORTEST ? DBUS_NOTICE_SHORTEST : expire;
        n->due = expire ? now + expire : 0;
        n->due += expire && !n->due;
        if (n->due)
                dbus_due_at(n->due);
        dbus_notices_told();
        return n->id;
}

// The notifications whose time is over are ended (1), and the earliest left is the bus's next deadline.
static void dbus_notices_due(long now)
{
        int ended = 0;

        for (long k = 0; k < DBUS_NOTICES; k++)
        {
                struct dbus_notice *n = &dbus.notice[k];

                if (!n->id || !n->due)
                        continue;
                if (now >= n->due)
                        dbus_notice_end(n, 1), ended = 1;
                else
                        dbus_due_at(n->due);
        }
        if (ended)
                dbus_notices_told();
}

// A text argument read for a notification: where it is and how long, or 0 bytes when it is none (dbus_foreign reads only what it keeps).
static long dbus_notice_arg(const struct dbus_message *m, long index, const unsigned char **text)
{
        long n = dbus_arg_text(m, index, text);

        return n < 0 ? 0 : n;
}

static const char dbus_introspect_notify[] =
    "<!DOCTYPE node PUBLIC \"-//freedesktop//DTD D-BUS Object Introspection 1.0//EN\" "
    "\"http://www.freedesktop.org/standards/dbus/1.0/introspect.dtd\">\n<node>\n"
    "<interface name=\"org.freedesktop.Notifications\">\n"
    "<method name=\"Notify\"><arg type=\"s\" direction=\"in\"/><arg type=\"u\" direction=\"in\"/><arg type=\"s\" direction=\"in\"/>"
    "<arg type=\"s\" direction=\"in\"/><arg type=\"s\" direction=\"in\"/><arg type=\"as\" direction=\"in\"/><arg type=\"a{sv}\" direction=\"in\"/>"
    "<arg type=\"i\" direction=\"in\"/><arg type=\"u\" direction=\"out\"/></method>\n"
    "<method name=\"CloseNotification\"><arg type=\"u\" direction=\"in\"/></method>\n"
    "<method name=\"GetCapabilities\"><arg type=\"as\" direction=\"out\"/></method>\n"
    "<method name=\"GetServerInformation\"><arg type=\"s\" direction=\"out\"/><arg type=\"s\" direction=\"out\"/><arg type=\"s\" direction=\"out\"/>"
    "<arg type=\"s\" direction=\"out\"/></method>\n"
    "<signal name=\"NotificationClosed\"><arg type=\"u\"/><arg type=\"u\"/></signal>\n"
    "</interface>\n</node>\n";

// A call to org.freedesktop.Notifications, at its path.
static void dbus_notify_call(struct dbus_client *from, const struct dbus_message *m)
{
        struct dbus_out w;
        long body;

        if (dbus_common(from, m, dbus.service.unique, dbus_introspect_notify))
                return;
        if (m->interface[0] && !dbus_equal(m->interface, DBUS_NAME_NOTIFY))
        {
                dbus_error_reply(from, m, "org.freedesktop.DBus.Error.UnknownMethod", "No such interface", dbus.service.unique);
                return;
        }
        if (dbus_equal(m->member, "Notify") && dbus_equal(m->signature, "susssasa{sv}i"))
        {
                const unsigned char *app, *summary, *text;
                long app_n = dbus_notice_arg(m, 0, &app), summary_n = dbus_notice_arg(m, 3, &summary), text_n = dbus_notice_arg(m, 4, &text), s;
                long urgency_at = dbus_option_at(m, 6, "urgency", 'y'), expire_at = dbus_arg_at(m, 7, &s), replaces_at = dbus_arg_at(m, 1, &s);
                unsigned long id;

                if (replaces_at < 0 || expire_at < 0)
                        return;
                id = dbus_notice_post(from, dbus_get32(m->bytes + m->body_at + DBUS_ALIGN(replaces_at, 4)), 0, app, app_n, summary, summary_n, text, text_n,
                                      urgency_at < 0 ? 1 : m->bytes[m->body_at + urgency_at],
                                      (long)(int)dbus_get32(m->bytes + m->body_at + DBUS_ALIGN(expire_at, 4)));
                if (!id)
                {
                        dbus_error_reply(from, m, "org.freedesktop.DBus.Error.LimitsExceeded", "Too many notifications at once", dbus.service.unique);
                        return;
                }
                dbus_reply_u32(from, m, id, dbus.service.unique);
                return;
        }
        if (dbus_equal(m->member, "CloseNotification") && dbus_equal(m->signature, "u"))
        {
                unsigned long id = dbus_get32(m->bytes + m->body_at);

                // answered alike whether there is such a notification of the caller's or not: another's ids are not told
                dbus_reply_empty(from, m, dbus.service.unique);
                for (long k = 0; k < DBUS_NOTICES; k++)
                        if (id && dbus.notice[k].id == id && dbus.notice[k].owner == from->id && !dbus.notice[k].portal)
                        {
                                dbus_notice_end(&dbus.notice[k], 3);
                                dbus_notices_told();
                        }
                return;
        }
        if (dbus_equal(m->member, "GetCapabilities") && !m->signature[0])
        {
                dbus_reply_begin(&w, from, m, "as", dbus.service.unique, &body);
                dbus_o_u32(&w, 9); // the array's bytes: "body", one string of 4 + 4 + 1
                dbus_o_string(&w, "body");
                dbus_reply_end(&w, from, m, body);
                return;
        }
        if (dbus_equal(m->member, "GetServerInformation") && !m->signature[0])
        {
                dbus_reply_begin(&w, from, m, "ssss", dbus.service.unique, &body);
                dbus_o_string(&w, "Moonwater");
                dbus_o_string(&w, "Moonwater");
                dbus_o_string(&w, "1");
                dbus_o_string(&w, "1.2");
                dbus_reply_end(&w, from, m, body);
                return;
        }
        dbus_error_reply(from, m, "org.freedesktop.DBus.Error.UnknownMethod", "No such method", dbus.service.unique);
}

// The portal's org.freedesktop.portal.Notification: AddNotification(s id, a{sv} notification) and RemoveNotification(s id); 1 when the call was one.
static int dbus_portal_notification(struct dbus_client *from, const struct dbus_message *m)
{
        char key[64];

        if (!dbus_equal(m->interface, "org.freedesktop.portal.Notification"))
                return 0;
        if ((dbus_equal(m->member, "AddNotification") && dbus_equal(m->signature, "sa{sv}")) ||
            (dbus_equal(m->member, "RemoveNotification") && dbus_equal(m->signature, "s")))
        {
                if (dbus_arg_string(m, 0, key, sizeof key) < 0)
                {
                        dbus_error_reply(from, m, "org.freedesktop.DBus.Error.InvalidArgs", "A notification's id is at most 63 bytes", dbus.service.unique);
                        return 1;
                }
                if (m->member[0] == 'R')
                {
                        dbus_reply_empty(from, m, dbus.service.unique);
                        for (long k = 0; k < DBUS_NOTICES; k++)
                                if (dbus.notice[k].id && dbus.notice[k].portal && dbus.notice[k].owner == from->id && dbus_equal(dbus.notice[k].key, key))
                                {
                                        dbus_notice_end(&dbus.notice[k], 3);
                                        dbus_notices_told();
                                }
                        return 1;
                }
                {
                        long title_at = dbus_option_at(m, 1, "title", 's'), body_at = dbus_option_at(m, 1, "body", 's');
                        long priority_at = dbus_option_at(m, 1, "priority", 's');
                        const unsigned char *b = m->bytes + m->body_at;
                        long title_n = title_at < 0 ? 0 : (long)dbus_get32(b + title_at), body_n = body_at < 0 ? 0 : (long)dbus_get32(b + body_at);
                        const char *priority = priority_at < 0 ? "normal" : (const char *)b + priority_at + 4;
                        unsigned urgency = dbus_equal(priority, "low") ? 0 : dbus_equal(priority, "urgent") || dbus_equal(priority, "high") ? 2 : 1;

                        if (!dbus_notice_post(from, 0, key, (const unsigned char *)"", 0, title_at < 0 ? b : b + title_at + 4, title_n,
                                              body_at < 0 ? b : b + body_at + 4, body_n, urgency, -1))
                                dbus_error_reply(from, m, "org.freedesktop.DBus.Error.LimitsExceeded", "Too many notifications at once", dbus.service.unique);
                        else
                                dbus_reply_empty(from, m, dbus.service.unique);
                }
                return 1;
        }
        dbus_error_reply(from, m, "org.freedesktop.DBus.Error.UnknownMethod", "No such method", dbus.service.unique);
        return 1;
}

/* ---- the FileChooser: Files' pick mode ---- */

/*
        FileChooser's OpenFile and SaveFile, answered by a chooser the host shows (the sessionbus's: Files in pick mode),
        when the host has one: the request's path is the reply at once, as the specification has it, and its Response comes when
        the person has picked (0, the picked files' file: URIs in results' uris) or shut the chooser (1). One chooser a client: a
        request while one is open for it, or one the host cannot show, ends at once as before (2, which a program takes for no
        dialog), and so does SaveFiles (a folder for files the program names, which Files does not pick yet). Request.Close of the
        request, or the client gone, lets the chooser go (the host's unchoose) and nothing is sent.
        What a program asks is held to what it is: the title, the name and the folder each fit their room or are not used, and a
        folder is an absolute path or nothing.
*/

// Where the value of `key` is in the a{sv} that is argument `index`, when its variant holds signature `sig` exactly: the offset of the
// value in the body (aligned for its first type), or -1. (dbus_option_at is this for one basic type.)
static long dbus_option_typed(const struct dbus_message *m, long index, const char *key, const char *sig)
{
        const unsigned char *p = m->bytes + m->body_at;
        long s, at = dbus_arg_at(m, index, &s), n = dbus_length(m->signature), limit = m->body_length, stop, length = dbus_length(sig);

        if (at < 0 || s + 4 > n || m->signature[s] != 'a' || m->signature[s + 1] != '{' || m->signature[s + 2] != 's' || m->signature[s + 3] != 'v')
                return -1;
        at = DBUS_ALIGN(at, 4);
        if (at + 4 > limit)
                return -1;
        stop = (long)dbus_get32(p + at);
        at = DBUS_ALIGN(at + 4, 8);
        stop += at;
        if (stop > limit)
                return -1;
        while (at < stop)
        {
                long key_at, sig_at, ks = 0, vs = 0, first;

                at = DBUS_ALIGN(at, 8);
                key_at = at;
                if (!dbus_value(p, &at, stop, "s", 1, &ks, 0, 0))
                        return -1;
                sig_at = at;
                if (!dbus_value(p, &at, stop, "v", 1, &vs, 0, m->fds))
                        return -1;
                if (p[sig_at] != length || !dbus_equal((const char *)p + key_at + 4, key))
                        continue;
                for (long k = 0; k < length; k++)
                        if (p[sig_at + 1 + k] != (unsigned char)sig[k])
                                length = -1;
                if (length < 0)
                        return -1;
                first = dbus_fixed_size(sig[0]);
                return DBUS_ALIGN(sig_at + 2 + length, sig[0] == 'a' || sig[0] == 's' || sig[0] == 'o' ? 4 : first ? first : 1);
        }
        return -1;
}

// A boolean option of argument `index`: 1 or 0, and 0 when there is none.
static int dbus_option_true(const struct dbus_message *m, long index, const char *key)
{
        long at = dbus_option_typed(m, index, key, "b");

        return at >= 0 && dbus_get32(m->bytes + m->body_at + at) != 0;
}

// A path option (ay, its bytes ended by a NUL as the portal sends them) into `into`: its length, or -1 when there is none, it does not
// fit, it is not absolute, or a NUL comes before its end.
static long dbus_option_path(const struct dbus_message *m, long index, const char *key, char *into, long room)
{
        long at = dbus_option_typed(m, index, key, "ay"), length;
        const unsigned char *bytes;

        if (at < 0)
                return -1;
        length = (long)dbus_get32(m->bytes + m->body_at + at);
        bytes = m->bytes + m->body_at + at + 4;
        while (length > 0 && !bytes[length - 1])
                length--;
        if (length <= 0 || length + 1 > room || bytes[0] != '/')
                return -1;
        for (long k = 0; k < length; k++)
                if (!bytes[k])
                        return -1;
        dbus_copy(into, bytes, length);
        into[length] = 0;
        return length;
}

// The Response of a chooser's request: `code`, and for 0 the paths (each ended by a NUL, `length` bytes in all) as file: URIs.
static long dbus_choice_response(const struct dbus_client *c, const char *path, unsigned long code, const char *paths, long length)
{
        struct dbus_out w = {dbus.scratch, 0, DBUS_MESSAGE, 0};
        struct dbus_fields f = {path, "org.freedesktop.portal.Request", "Response", "", c->unique, dbus.service.unique, "ua{sv}", 0, 0};
        static char uri[3 * DBUS_CHOOSE_PATH + 8];
        long body, outer, first, inner, items;
        struct dbus_message parsed;

        dbus_o_header(&w, DBUS_SIGNAL, DBUS_FLAG_NO_REPLY, dbus_next_serial(), &f);
        body = w.length;
        dbus_o_u32(&w, code);
        dbus_o_u32(&w, 0);
        outer = w.length - 4;
        dbus_o_pad(&w, 8);
        first = w.length;
        if (!code && length > 0)
        {
                dbus_o_pad(&w, 8);
                dbus_o_string(&w, "uris");
                dbus_o_sig(&w, "as");
                dbus_o_u32(&w, 0);
                inner = w.length - 4;
                items = w.length;
                for (long at = 0; at < length;)
                {
                        long n = 0, stop = at;

                        while (stop < length && paths[stop])
                                stop++;
                        for (long k = 0; "file://"[k]; k++)
                                uri[n++] = "file://"[k];
                        // A byte of the path that is not unreserved (letters, digits, - . _ ~) or a slash is written %XX.
                        for (long k = at; k < stop && n + 4 < (long)sizeof uri; k++)
                        {
                                unsigned char b = (unsigned char)paths[k];

                                if (dbus_letter((char)b) || dbus_digit((char)b) || b == '-' || b == '.' || b == '_' || b == '~' || b == '/')
                                        uri[n++] = (char)b;
                                else
                                        uri[n++] = '%', uri[n++] = "0123456789ABCDEF"[b >> 4], uri[n++] = "0123456789ABCDEF"[b & 15];
                        }
                        uri[n] = 0;
                        if (stop > at && paths[at] == '/')
                                dbus_o_string(&w, uri);
                        at = stop + 1;
                }
                dbus_o_patch32(&w, inner, (unsigned long)(w.length - items));
        }
        dbus_o_patch32(&w, outer, (unsigned long)(w.length - first));
        dbus_o_end(&w, body);
        return w.bad || dbus_parse(w.at, w.length, &parsed) < 0 ? -1 : w.length;
}

// FileChooser's methods, when the host shows a chooser: 1 when the call was one and is answered, 0 to leave it to the request that ends.
static int dbus_portal_choose(struct dbus_client *from, const struct dbus_message *m)
{
        static struct dbus_choice choice;
        char token[DBUS_TOKEN + 8], path[DBUS_REQUEST_PATH];
        const unsigned char *title;
        struct dbus_out w;
        long body, length;

        if (!dbus.host->choose || !dbus_equal(m->interface, "org.freedesktop.portal.FileChooser") || !dbus_equal(m->signature, "ssa{sv}") ||
            !(dbus_equal(m->member, "OpenFile") || dbus_equal(m->member, "SaveFile")))
                return 0;
        dbus_zero(&choice, sizeof choice);
        choice.slot = from->slot, choice.id = from->id;
        choice.kind = dbus_equal(m->member, "OpenFile") ? DBUS_CHOOSE_OPEN : DBUS_CHOOSE_SAVE;
        choice.multiple = choice.kind == DBUS_CHOOSE_OPEN && dbus_option_true(m, 2, "multiple");
        choice.directory = choice.kind == DBUS_CHOOSE_OPEN && dbus_option_true(m, 2, "directory");
        if ((length = dbus_arg_text(m, 1, &title)) > 0 && length < (long)sizeof choice.title)
                dbus_copy(choice.title, title, length), choice.title[length] = 0;
        if (dbus_option_string(m, 2, "current_name", choice.name, sizeof choice.name) < 0)
                choice.name[0] = 0;
        for (long k = 0; choice.name[k]; k++)
                if (choice.name[k] == '/')
                        choice.name[0] = 0;
        if (dbus_option_path(m, 2, "current_folder", choice.folder, sizeof choice.folder) < 0)
        {
                // SaveFile's current_file: the file being saved again, its folder the chooser's and its name the one suggested
                choice.folder[0] = 0;
                if (choice.kind == DBUS_CHOOSE_SAVE && (length = dbus_option_path(m, 2, "current_file", choice.folder, sizeof choice.folder)) > 1)
                {
                        long cut = length;

                        while (cut > 0 && choice.folder[cut - 1] != '/')
                                cut--;
                        if (cut > 0 && length - cut < (long)sizeof choice.name)
                                dbus_text(choice.name, sizeof choice.name, choice.folder + cut);
                        choice.folder[cut > 1 ? cut - 1 : 1] = 0;
                }
                else
                        choice.folder[0] = 0;
        }
        if (dbus_option_string(m, 2, "handle_token", token, sizeof token) < 0)
                token[0] = 0;
        dbus_request_path(from, token, path);
        dbus_reply_begin(&w, from, m, "o", dbus.service.unique, &body);
        dbus_o_string(&w, path);
        dbus_reply_end(&w, from, m, body);
        if (from->choosing[0] || dbus.host->choose(dbus.host->context, &choice) < 0)
                dbus_response_owe(from, path, 2);
        else
                dbus_text(from->choosing, sizeof from->choosing, path);
        return 1;
}

// The chooser open for a client let go: the one of the request at `path`, or any (path null: the client is gone).
static void dbus_choice_close(struct dbus_client *c, const char *path)
{
        if (!c->choosing[0] || (path && !dbus_equal(c->choosing, path)))
                return;
        c->choosing[0] = 0;
        if (dbus.host->unchoose)
                dbus.host->unchoose(dbus.host->context, c->slot);
}

/*
        The host's chooser for client `id` at `slot` is done: the paths picked, each ended by a NUL (`length` bytes in all; none is
        the chooser shut, cancelled). Sent as its request's Response to the client that asked, when it is still connected there with
        that request open; a path that is not absolute is not sent.
*/
DBUS_API void dbus_choice_done(long slot, long id, const char *paths, long length)
{
        struct dbus_client *c = slot >= 0 && slot < DBUS_CLIENTS ? dbus.client[slot] : 0;
        long n;

        if (!c || c->id != id || !c->choosing[0] || length < 0)
                return;
        n = dbus_choice_response(c, c->choosing, length > 0 ? 0 : 1, paths, length);
        if (n < 0)
                n = dbus_choice_response(c, c->choosing, 2, 0, 0);
        c->choosing[0] = 0;
        if (n > 0)
                dbus_deliver(c, dbus.scratch, n, DBUS_ASKED_FOR);
}

// A call to the portal (the service's own).
static void dbus_service_call(struct dbus_client *from, const struct dbus_message *m)
{
        char ns[96], key[96];
        struct dbus_setting *s;
        struct dbus_out w;
        long body;

        if (dbus_equal(m->path, DBUS_PATH_NOTIFY))
        {
                dbus_notify_call(from, m);
                return;
        }
        if (dbus_common(from, m, dbus.service.unique, dbus_introspect_portal))
                return;
        if (!dbus_equal(m->path, DBUS_PATH_PORTAL))
        {
                if (dbus_starts(m->path, DBUS_REQUEST_PREFIX) && dbus_equal(m->interface, "org.freedesktop.portal.Request") && dbus_equal(m->member, "Close"))
                {
                        dbus_reply_empty(from, m, dbus.service.unique);
                        dbus_request_close(from, m->path);
                        return;
                }
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
        if (dbus_equal(m->interface, "org.freedesktop.portal.Inhibit") && dbus_equal(m->member, "Inhibit"))
        {
                // the request is the inhibition itself, which the caller ends with Close: no Response while it holds
                dbus_reply_begin(&w, from, m, "o", dbus.service.unique, &body);
                dbus_o_string(&w, "/org/freedesktop/portal/desktop/request/moonwater/1");
                dbus_reply_end(&w, from, m, body);
                return;
        }
        if (dbus_portal_choose(from, m))
                return;
        /*
                A monitor of the session would be told when it ends and asked whether it may (StateChanged, QueryEndResponse),
                and the desktop asks no program that: so the request ends at once, 2 (another way), and the program goes on with
                no monitor, as it does under a portal that has none. A file chooser's requests, where the host shows none (or
                SaveFiles, or one asked while the caller's is open), end at once as cancelled (1 is the person's; 2 is what a
                program takes for "no dialog"), so an upload is never left open.
        */
        if ((dbus_equal(m->interface, "org.freedesktop.portal.Inhibit") && dbus_equal(m->member, "CreateMonitor") && dbus_equal(m->signature, "sa{sv}")) ||
            (dbus_equal(m->interface, "org.freedesktop.portal.FileChooser") &&
             (dbus_equal(m->member, "OpenFile") || dbus_equal(m->member, "SaveFile") || dbus_equal(m->member, "SaveFiles")) &&
             dbus_equal(m->signature, "ssa{sv}")))
        {
                dbus_portal_request(from, m, m->signature[1] == 'a' ? 1 : 2, 2);
                return;
        }
        if (dbus_portal_notification(from, m))
                return;
        dbus_error_reply(from, m, "org.freedesktop.DBus.Error.UnknownMethod", "No such method", dbus.service.unique);
}

/* ---- routing ---- */

// The message with its sender stamped (what the client wrote there is not kept): the new bytes in dbus.stamped, their length.
static long dbus_stamp(const struct dbus_message *m, const char *sender)
{
        struct dbus_out w = {dbus.stamped, 0, DBUS_STAMPED, 0};
        struct dbus_fields f = {m->path, m->interface, m->member, m->error, m->destination, sender, m->signature, m->reply,
                                (m->has & DBUS_HAS_REPLY) != 0};

        dbus_o_header_fds(&w, m->type, m->flags, m->serial, &f, m->fds);
        dbus_o_raw(&w, m->bytes + m->body_at, m->body_length);
        dbus_o_end(&w, DBUS_ALIGN(w.length - m->body_length, 8));
        return w.bad ? -1 : w.length;
}

// A call of `caller` waiting for a reply: among its own rows only (a caller is a client's slot, never the service).
static struct dbus_pending *dbus_pending_find(long caller, unsigned long serial, long callee)
{
        struct dbus_pending *row = &dbus.pending[caller * DBUS_PENDING_PER];

        for (long at = 0; at < DBUS_PENDING_PER; at++)
                if (row[at].used && row[at].serial == serial && (callee < 0 || row[at].callee == callee))
                        return &row[at];
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
                // the bracket that closes them and the line's end are kept room for, whatever filled the line before
                for (long k = 0; more[k]; k++)
                        for (long c = 0; more[k][c] && n < (long)sizeof line - 2; c++)
                                line[n++] = more[k][c];
                if (n < (long)sizeof line - 1)
                        line[n++] = ']';
        }
        line[n] = 0;
        // what a client wrote goes to the log, and so to a terminal: a byte a terminal acts on is shown as ?
        for (long k = 0; k < n; k++)
                if ((unsigned char)line[k] < 32 || line[k] == 127)
                        line[k] = '?';
        dbus.host->trace(dbus.host->context, line);
}

// One message from a client that has said Hello, routed.
static void dbus_route(struct dbus_client *from, const struct dbus_message *m, const int *fds)
{
        long to, n;
        long nfds = (long)m->fds;
        struct dbus_client *target;

        if (dbus.host->trace)
                dbus_trace(from, m);

        if (m->type == DBUS_CALL)
        {
                const char *dest = m->destination;

                if (!dest[0] || dbus_equal(dest, DBUS_NAME_BUS))
                {
                        dbus_close_fds(fds, nfds);
                        dbus_bus_call(from, m);
                        return;
                }
                // the portal's name is the service's for as long as the bus runs: no lookup for the call most made
                to = dbus_equal(dest, DBUS_NAME_PORTAL) ? DBUS_SERVICE : dbus_resolve(dest);
                if (to == DBUS_SERVICE)
                {
                        dbus_close_fds(fds, nfds);
                        dbus_service_call(from, m);
                        return;
                }
                if (to < 0)
                {
                        dbus_close_fds(fds, nfds);
                        dbus_error_reply(from, m, "org.freedesktop.DBus.Error.ServiceUnknown",
                                         "The name is not provided by any service and is not owned", DBUS_NAME_BUS);
                        return;
                }
                target = dbus_client_of(to);
                if (nfds > 0 && (!target || !target->unix_fds))
                {
                        dbus_close_fds(fds, nfds);
                        dbus_error_reply(from, m, "org.freedesktop.DBus.Error.InvalidArgs",
                                         "Message recipient does not support file descriptor passing", DBUS_NAME_BUS);
                        return;
                }
                {
                        struct dbus_pending *p = 0;
                        int answered = !(m->flags & DBUS_FLAG_NO_REPLY);

                        for (long at = from->slot * DBUS_PENDING_PER; answered && at < (from->slot + 1) * DBUS_PENDING_PER && !p; at++)
                                if (!dbus.pending[at].used)
                                        p = &dbus.pending[at];
                        if (answered && (!p || from->pending >= DBUS_PENDING_PER))
                        {
                                dbus_close_fds(fds, nfds);
                                dbus_error_reply(from, m, "org.freedesktop.DBus.Error.LimitsExceeded", "Too many calls wait for a reply", DBUS_NAME_BUS);
                                return;
                        }
                        n = dbus_stamp(m, from->unique);
                        if (n <= 0 || !dbus_deliver_fds(target, dbus.stamped, n, DBUS_UNASKED_FOR, fds, nfds))
                        {
                                dbus_close_fds(fds, nfds);
                                // the callee's queue holds what others sent it and it has not read: the call is refused, not the callee
                                dbus_error_reply(from, m, "org.freedesktop.DBus.Error.LimitsExceeded",
                                                 "The recipient has not read what it was sent", DBUS_NAME_BUS);
                                return;
                        }
                        if (answered)
                        {
                                p->used = 1;
                                p->caller = (short)from->slot;
                                p->callee = (short)to;
                                p->serial = m->serial;
                                p->due = dbus.host->now(dbus.host->context) + DBUS_REPLY_MS;
                                p->due += !p->due;
                                dbus_due_at(p->due);
                                from->pending++;
                        }
                }
                return;
        }
        if (m->type == DBUS_SIGNAL)
        {
                n = dbus_stamp(m, from->unique);
                if (n < 0)
                {
                        dbus_close_fds(fds, nfds);
                        return;
                }
                if (m->destination[0])
                {
                        to = dbus_resolve(m->destination);
                        target = to >= 0 && to != DBUS_SERVICE ? dbus_client_of(to) : 0;
                        if (!target || !dbus_deliver_fds(target, dbus.stamped, n, DBUS_UNASKED_FOR, fds, nfds))
                                dbus_close_fds(fds, nfds);
                        return;
                }
                // the rules are matched against the message as it came (the sender a rule names is `from`, never what it wrote),
                // which is the stamped one but for the sender: it is not parsed again
                dbus_broadcast_fds(dbus.stamped, n, from->slot, m, fds, nfds);
                dbus_close_fds(fds, nfds);
                return;
        }
        // a reply or an error: to the caller that asked it of this client, once
        to = m->destination[0] ? dbus_resolve(m->destination) : -1;
        if (to < 0 || to == DBUS_SERVICE || !(m->has & DBUS_HAS_REPLY))
        {
                dbus_close_fds(fds, nfds);
                return;
        }
        {
                struct dbus_pending *p = dbus_pending_find(to, m->reply, from->slot);

                if (!p)
                {
                        dbus_close_fds(fds, nfds);
                        return;
                }
                p->used = 0;
                if (dbus_client_of(to) && dbus_client_of(to)->pending > 0)
                        dbus_client_of(to)->pending--;
        }
        n = dbus_stamp(m, from->unique);
        if (n <= 0 || !dbus_deliver_fds(dbus_client_of(to), dbus.stamped, n, DBUS_ASKED_FOR, fds, nfds))
                dbus_close_fds(fds, nfds);
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
        dbus_bus_signal(c->slot, "NameAcquired", c->unique, 0, 0, DBUS_ASKED_FOR);
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
        dbus_deliver(c, (const unsigned char *)text, dbus_length(text), DBUS_ASKED_FOR);
}

// The decimal text of a uid as hex of its characters, which is what EXTERNAL sends.
static int dbus_external_ok(struct dbus_client *c, const char *hex)
{
        char digits[24];
        long n = dbus_u64_text(digits, (unsigned long)(unsigned int)c->uid);
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
        static const struct
        {
                const char *name;
                unsigned char op;
        } auth_cmds[] = {{"AUTH", 1}, {"DATA", 2}, {"NEGOTIATE_UNIX_FD", 3}, {"BEGIN", 4}, {"CANCEL", 5}, {"ERROR", 5}};
        char *word[4] = {line, 0, 0, 0};
        long count = 1, ci;

        for (long k = 0; k < n; k++)
                if (line[k] == ' ' && count < 4)
                {
                        line[k] = 0;
                        word[count++] = line + k + 1;
                }
        ci = DBUS_TABLE_LOOKUP(word[0], auth_cmds);
        switch (ci < (long)(sizeof(auth_cmds) / sizeof(auth_cmds[0])) ? auth_cmds[ci].op : 0)
        {
        case 1: // AUTH
                c->want_data = 0;
                if (count == 1 || (count >= 2 && !dbus_equal(word[1], "EXTERNAL") && !dbus_equal(word[1], "ANONYMOUS")))
                {
                        c->uid_ok = 0;
                        c->unix_fds = 0;
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
                        c->uid_ok = 0;
                        c->unix_fds = 0;
                        c->want_data = 1;
                        dbus_say(c, "DATA\r\n");
                        return 0;
                }
                if (count == 3 && dbus_external_ok(c, word[2]))
                {
                        c->uid_ok = 1;
                        dbus_say(c, "OK ");
                        dbus_say(c, dbus.guid);
                        dbus_say(c, "\r\n");
                }
                else
                {
                        c->uid_ok = 0;
                        c->unix_fds = 0;
                        dbus_say(c, "REJECTED EXTERNAL ANONYMOUS\r\n");
                }
                return 0;
        case 2: // DATA
        {
                int asked = c->want_data;

                c->want_data = 0;
                if (asked && count <= 2 && dbus_external_ok(c, count == 2 ? word[1] : ""))
                {
                        c->uid_ok = 1;
                        dbus_say(c, "OK ");
                        dbus_say(c, dbus.guid);
                        dbus_say(c, "\r\n");
                }
                else
                {
                        c->uid_ok = 0;
                        c->unix_fds = 0;
                        dbus_say(c, "REJECTED EXTERNAL ANONYMOUS\r\n");
                }
                return 0;
        }
        case 3: // NEGOTIATE_UNIX_FD
                if (dbus_fd_ops.close_fd && c->uid_ok)
                {
                        c->unix_fds = 1;
                        dbus_say(c, "AGREE_UNIX_FD\r\n");
                }
                else
                        dbus_say(c, "ERROR \"file descriptors are not passed\"\r\n");
                return 0;
        case 4: // BEGIN
                if (!c->uid_ok)
                        return -1;
                c->state = 2;
                return 1;
        case 5: // CANCEL, ERROR
                c->uid_ok = 0;
                c->want_data = 0;
                c->unix_fds = 0;
                dbus_say(c, "REJECTED EXTERNAL ANONYMOUS\r\n");
                return 0;
        default:
                dbus_say(c, "ERROR\r\n");
                return 0;
        }
}

/* ---- the host's side ---- */

DBUS_API int dbus_start(const struct dbus_host *host)
{
        unsigned char random[16];

        if (dbus.host || !host || !host->alloc || !host->release || !host->now || !host->random)
                return -1;
        dbus_zero(&dbus, sizeof dbus);
        dbus.host = host;
        dbus.next_id = 2;
        for (long at = 0; at < DBUS_NAMES; at++)
                dbus.name[at].owner = dbus.name[at].next = -1;
        for (long at = 0; at < DBUS_NAME_BUCKETS; at++)
                dbus.bucket[at] = -1;
        dbus.recent = -1;
        host->random(host->context, random, 16);
#if DBUS_LIBRARY
        memory_into_hex((p8 *)dbus.guid, random, 16);
#else
        for (long k = 0; k < 16; k++)
        {
                dbus.guid[2 * k] = "0123456789abcdef"[random[k] >> 4];
                dbus.guid[2 * k + 1] = "0123456789abcdef"[random[k] & 15];
        }
#endif
        dbus.service.slot = DBUS_SERVICE;
        dbus.service.hello = 1;
        dbus.service.id = 1;
        dbus_text(dbus.service.unique, sizeof dbus.service.unique, ":1.1");
        dbus_name_add(DBUS_NAME_PORTAL, DBUS_SERVICE, 0);
        dbus_name_add(DBUS_NAME_NOTIFY, DBUS_SERVICE, 0);
        dbus.service.names = 2;
        return 0;
}

// A line in /proc/<pid>/uid_map is three decimals: <inside> <outside> <count>. A peer inside bowl's user namespace (mapping
// container uid 1000 to host uid 0) carries kernel SO_PEERCRED uid 0 yet authenticates with EXTERNAL "1000"; map its outer
// uid to the inside uid here, while refusing to map a non-zero outer uid down to 0.
DBUS_API long dbus_uid_from_map(const char *text, long length, long uid)
{
        unsigned long want = (unsigned long)(unsigned int)uid;
        long at = 0, lines = 0;
        unsigned long single_inside = 0, single_count = 0;

        if (!text || length <= 0 || uid < 0)
                return uid;
        while (at < length)
        {
                unsigned long inside = 0, outside = 0, count = 0;
                int fields = 0;

                while (at < length && (text[at] == ' ' || text[at] == '\t' || text[at] == '\r'))
                        at++;
                if (at >= length || text[at] == '\n')
                {
                        if (at < length)
                                at++;
                        continue;
                }
                for (int f = 0; f < 3; f++)
                {
                        unsigned long value = 0;
                        int any = 0;

                        while (at < length && (text[at] == ' ' || text[at] == '\t'))
                                at++;
                        while (at < length && text[at] >= '0' && text[at] <= '9')
                        {
                                value = value * 10u + (unsigned long)(unsigned char)(text[at] - '0');
                                at++;
                                any = 1;
                                if (value > 0xffffffffUL)
                                        break;
                        }
                        if (!any || value > 0xffffffffUL ||
                            (at < length && text[at] != ' ' && text[at] != '\t' && text[at] != '\n' && text[at] != '\r'))
                                break;
                        if (f == 0)
                                inside = value;
                        else if (f == 1)
                                outside = value;
                        else
                                count = value;
                        fields++;
                }
                if (fields == 3)
                {
                        while (at < length && (text[at] == ' ' || text[at] == '\t' || text[at] == '\r'))
                                at++;
                        if (at < length && text[at] != '\n')
                                fields = 0;
                }
                while (at < length && text[at] != '\n')
                        at++;
                if (at < length && text[at] == '\n')
                        at++;
                if (fields != 3 || count == 0)
                        continue;
                lines++;
                single_inside = inside;
                single_count = count;
                if (want >= outside && want - outside < count)
                {
                        unsigned long mapped = inside + (want - outside);

                        if (mapped > 0xffffffffUL || (uid != 0 && mapped == 0))
                                return uid;
                        return (long)mapped;
                }
        }
        // A nested single-uid child userns (e.g. bwrap/pressure-vessel inside bowl) started from outer uid 0 may expose only
        // its one uid; unwrap directly when there is one extent of length 1 and the host credential was uid 0.
        if (uid == 0 && lines == 1 && single_count == 1)
                return (long)single_inside;
        return uid;
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
        dbus_choice_close(c, 0);
        dbus_close_fds(c->in_fd, c->in_fds);
        dbus_close_fds(c->out_fd, c->out_fds);
        c->in_fds = c->out_fds = c->out_fd_runs = 0;
        dbus.client[slot] = 0;
        dbus.clients--;
        if (c->hello)
        {
                for (long at = 0; at < DBUS_NAMES; at++)
                        if (dbus.name[at].used && dbus.name[at].owner == slot)
                        {
                                dbus_name_remove(&dbus.name[at]);
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
                                struct dbus_message fake = {0};

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
                        if (c->in[at] == 'l' && c->in[at + 3] == 1 && c->in[at + 1] > DBUS_SIGNAL)
                        {
                                // An unknown message type cannot safely be skipped while queued FDs are pending: it could
                                // shift those FDs onto a following message.
                                if (c->in_fds > 0)
                                {
                                        c->dead = 1;
                                        c->why = "a message that is not valid";
                                        break;
                                }
                                // a type of message this bus does not know is ignored, as the specification asks; it goes nowhere
                                at += total;
                                continue;
                        }
                        if (dbus_parse(c->in + at, total, &m) < 0)
                        {
                                c->dead = 1;
                                c->why = "a message that is not valid";
                                break;
                        }
                        {
                                int msg_fds[DBUS_FDS];
                                long nfds = (long)m.fds;

                                if (nfds > 0 && (!c->unix_fds || nfds > c->in_fds))
                                {
                                        c->dead = 1;
                                        c->why = "a message that is not valid";
                                        break;
                                }
                                for (long k = 0; k < nfds; k++)
                                        msg_fds[k] = c->in_fd[k];
                                if (nfds > 0)
                                {
                                        for (long k = nfds; k < c->in_fds; k++)
                                                c->in_fd[k - nfds] = c->in_fd[k];
                                        c->in_fds -= nfds;
                                }
                                if (!c->hello)
                                {
                                        dbus_close_fds(msg_fds, nfds);
                                        if (dbus_hello(c, &m) < 0)
                                                c->dead = 1, c->why = "the first call was not Hello";
                                }
                                else
                                        dbus_route(c, &m, msg_fds);
                        }
                        at += total;
                }
        }
        if (at > 0)
        {
                // what is left is less than a message: it goes to the front once a read, after every whole one is taken
                dbus_copy(c->in, c->in + at, c->in_length - at);
                c->in_length -= at;
        }
}

DBUS_API void dbus_in_fds(long slot, const int *fds, long count)
{
        struct dbus_client *c = slot >= 0 && slot < DBUS_CLIENTS ? dbus.client[slot] : 0;

        if (!c || c->dead || count <= 0)
        {
                dbus_close_fds(fds, count);
                return;
        }
        if (!c->unix_fds || c->in_fds + count > DBUS_FDS)
        {
                dbus_close_fds(fds, count);
                c->dead = 1;
                c->why = "too many file descriptors";
                return;
        }
        for (long k = 0; k < count; k++)
                c->in_fd[c->in_fds++] = fds[k];
}

DBUS_API long dbus_out(long slot, const unsigned char **bytes)
{
        struct dbus_client *c = slot >= 0 && slot < DBUS_CLIENTS ? dbus.client[slot] : 0;

        *bytes = c ? c->out + c->out_head : 0;
        return c ? c->out_length - c->out_head : 0;
}

DBUS_API long dbus_out_fds(long slot, const unsigned char **bytes, const int **fds, long *nfds)
{
        struct dbus_client *c = slot >= 0 && slot < DBUS_CLIENTS ? dbus.client[slot] : 0;
        long len;

        *fds = 0;
        *nfds = 0;
        if (!c)
        {
                *bytes = 0;
                return 0;
        }
        *bytes = c->out + c->out_head;
        len = c->out_length - c->out_head;
        if (c->out_fd_runs > 0)
        {
                if (c->out_fd_run[0].at == c->out_head)
                {
                        *fds = c->out_fd;
                        *nfds = c->out_fd_run[0].count;
                        if (c->out_fd_runs > 1 && c->out_fd_run[1].at - c->out_head < len)
                                len = c->out_fd_run[1].at - c->out_head;
                }
                else if (c->out_fd_run[0].at > c->out_head && c->out_fd_run[0].at - c->out_head < len)
                        len = c->out_fd_run[0].at - c->out_head;
        }
        return len;
}

DBUS_API void dbus_sent_fds(long slot)
{
        struct dbus_client *c = slot >= 0 && slot < DBUS_CLIENTS ? dbus.client[slot] : 0;
        long count;

        if (!c || c->out_fd_runs <= 0 || c->out_fd_run[0].at != c->out_head)
                return;
        count = c->out_fd_run[0].count;
        dbus_close_fds(c->out_fd, count);
        for (long k = count; k < c->out_fds; k++)
                c->out_fd[k - count] = c->out_fd[k];
        c->out_fds -= count;
        for (long k = 1; k < c->out_fd_runs; k++)
                c->out_fd_run[k - 1] = c->out_fd_run[k];
        c->out_fd_runs--;
}

// count bytes of what dbus_out gave were written: they leave the queue, and a client that read anything is not stalled.
DBUS_API void dbus_sent(long slot, long count)
{
        struct dbus_client *c = slot >= 0 && slot < DBUS_CLIENTS ? dbus.client[slot] : 0;

        if (!c || count <= 0 || count > c->out_length - c->out_head)
                return;
        c->out_head += count;
        if (c->out_head == c->out_length)
                c->out_head = c->out_length = 0;
        c->full_since = 0;
}

// 0 while a client is well, else why not (1 it spoke wrong, 2 it did not read).
DBUS_API long dbus_dead(long slot)
{
        struct dbus_client *c = slot >= 0 && slot < DBUS_CLIENTS ? dbus.client[slot] : 0;

        return c ? c->dead : 1;
}

/*
        Connections that have not said Hello in DBUS_HELLO_MS are ended, and those whose queue refused a message and that have read
        nothing for DBUS_STALL_MS since; a call that has waited DBUS_REPLY_MS for its callee is answered NoReply and its row let go
        (a late answer then goes nowhere), and a portal Response held DBUS_RESPONSE_MS is sent. Called now and then, and by
        dbus_wait's time at the latest.
*/
DBUS_API void dbus_tick(void)
{
        long now;

        if (!dbus.host)
                return;
        now = dbus.host->now(dbus.host->context);
        for (long at = 0; at < DBUS_CLIENTS; at++)
        {
                struct dbus_client *c = dbus.client[at];

                if (!c || c->dead)
                        continue;
                if (!c->hello && now - c->born > DBUS_HELLO_MS)
                        c->dead = 1, c->why = "it did not say Hello in time";
                else if (c->full_since && now - c->full_since > DBUS_STALL_MS)
                        c->dead = 2, c->why = "it did not read what it was sent";
        }
        if (!dbus.due || now < dbus.due)
                return;
        dbus.due = 0;
        dbus_notices_due(now);
        for (long at = 0; at < DBUS_CLIENTS; at++)
        {
                struct dbus_client *c = dbus.client[at];
                struct dbus_pending *row = &dbus.pending[at * DBUS_PENDING_PER];

                if (!c || c->dead)
                        continue;
                for (long k = 0; c->pending && k < DBUS_PENDING_PER; k++)
                {
                        struct dbus_message fake = {0};

                        if (!row[k].used)
                                continue;
                        if (now < row[k].due)
                        {
                                dbus_due_at(row[k].due);
                                continue;
                        }
                        row[k].used = 0;
                        c->pending--;
                        fake.serial = row[k].serial;
                        fake.flags = 0;
                        dbus_error_reply(c, &fake, "org.freedesktop.DBus.Error.NoReply", "The recipient did not answer in time", DBUS_NAME_BUS);
                }
                if (c->responses)
                        dbus_responses_send(c, now, 0);
        }
}

// Milliseconds until dbus_tick has a deadline to act on (a held Response, a call's answer), at most `most`: a host's longest sleep.
DBUS_API long dbus_wait(long most)
{
        long left;

        if (!dbus.host || !dbus.due)
                return most;
        left = dbus.due - dbus.host->now(dbus.host->context);
        return left < 0 ? 0 : left < most ? left : most;
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

        // What is given is put on the wire as it is: a text that does not fit, or is not UTF-8, is no setting (cut, it would be
        // a character cut in two, which a client takes for a message that is not valid and leaves the bus for), nor is a kind
        // that is none, and a boolean is 0 or 1.
        if (!dbus.host || dbus_length(ns) >= (long)sizeof s->ns || dbus_length(key) >= (long)sizeof s->key || !dbus_utf8((const unsigned char *)ns, dbus_length(ns)) ||
            !dbus_utf8((const unsigned char *)key, dbus_length(key)) || (kind != 'u' && kind != 'i' && kind != 'b' && kind != 'd' && kind != 's' && kind != 'c') ||
            (kind == 's' && (!text || dbus_length(text) >= (long)sizeof s->text || !dbus_utf8((const unsigned char *)text, dbus_length(text)))))
                return -1;
        if (kind == 'b')
                number = number != 0;
        if (!s)
        {
                for (long at = 0; at < DBUS_SETTINGS && !s; at++)
                        if (!dbus.setting[at].used)
                                s = &dbus.setting[at];
                if (!s)
                        return -1;
        }
        dbus_zero(&fresh, sizeof fresh);
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
