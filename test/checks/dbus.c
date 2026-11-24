/*
        test/checks/dbus.c: the session bus.

        A run of test/checks.c, which includes this file where the run stood (test/split.py cut it there): it is
        compiled only as part of that file, with its statics and macros, and never on its own. A section is built as
        test/checks.c is, from the repository root: a freestanding one by gcc -O2 -static -nostdlib -nostartfiles
        -march=x86-64 -w -DCHECK_<name> -fwhole-program -o OUT test/checks.c (sh test/run builds it with its
        freestanding_check), a hosted one as test/run builds it (gcc -std=gnu11 -O2 -D_GNU_SOURCE -DCHECK_<name>
        test/checks.c).
*/

#ifdef CHECK_dbus
/*
        CHECK_dbus: Moonwater's session bus (src/dbus/dbus.c) against clients that speak its wire, and a fake host. The
        messages the clients send are built with the bus's own writer (so a fault in it shows in the judge too, which the
        end-to-end rows against libdbus and GDBus are for), and what it sends back is read by the bus's own parser, which
        holds the grammar of the protocol; the hostile messages are bytes written out by hand.

            cc -std=gnu11 -O2 -Wall -Wextra -DCHECK_dbus test/checks.c
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../../src/dbus/dbus.c"

static unsigned db_checks, db_failures;

#define DBCHECK(condition, ...)                                                                                       \
        do                                                                                                            \
        {                                                                                                             \
                db_checks++;                                                                                          \
                if (!(condition))                                                                                     \
                {                                                                                                     \
                        db_failures++;                                                                                \
                        printf("  FAIL: ");                                                                           \
                        printf(__VA_ARGS__);                                                                          \
                        printf(" (line %d)\n", __LINE__);                                                             \
                }                                                                                                     \
        } while (0)

static long db_allocs, db_releases, db_time;

static void *db_alloc(void *context, long bytes)
{
        (void)context;
        db_allocs++;
        return calloc(1, (size_t)bytes);
}

static void db_release(void *context, void *block, long bytes)
{
        (void)context;
        (void)bytes;
        db_releases++;
        free(block);
}

static long db_now(void *context)
{
        (void)context;
        return db_time;
}

static void db_random(void *context, unsigned char *bytes, long count)
{
        (void)context;
        for (long at = 0; at < count; at++)
                bytes[at] = (unsigned char)(0x10 + at * 7);
}

static const struct dbus_host db_host = {0, db_alloc, db_release, db_now, db_random, 0, 0, 0, 0};

/* ---- a client ---- */

static void db_feed(long slot, const void *bytes, long n)
{
        const unsigned char *p = (const unsigned char *)bytes;

        while (n > 0)
        {
                long room, take;
                unsigned char *to = dbus_in_room(slot, &room);

                if (!to || room <= 0)
                        return;
                take = n < room ? n : room;
                memcpy(to, p, (size_t)take);
                dbus_in_done(slot, take);
                p += take;
                n -= take;
        }
}

static unsigned long db_serial;

// What the bus has queued for a client, as text up to count bytes (the authentication's lines).
static long db_take_text(long slot, char *into, long room)
{
        const unsigned char *bytes;
        long n = dbus_out(slot, &bytes);

        if (n > room - 1)
                n = room - 1;
        memcpy(into, bytes, (size_t)n);
        into[n] = 0;
        dbus_sent(slot, n);
        return n;
}

static long db_connect(long uid)
{
        long slot = dbus_connect(1000 + uid, uid);
        char line[200], hello[64];
        int n;

        if (slot < 0)
                return slot;
        n = snprintf(hello, sizeof hello, "%ld", uid);
        {
                char hex[64];

                for (int k = 0; k < n; k++)
                        snprintf(hex + 2 * k, 3, "%02x", (unsigned)hello[k]);
                snprintf(line, sizeof line, "%cAUTH EXTERNAL %s\r\n", 0, hex);
        }
        db_feed(slot, line, (long)strlen(line + 1) + 1);
        db_take_text(slot, line, sizeof line);
        if (strncmp(line, "OK ", 3))
                return -2;
        db_feed(slot, "BEGIN\r\n", 7);
        return slot;
}

struct db_call
{
        unsigned char bytes[4096];
        struct dbus_out w;
        long body;
};

static void db_begin(struct db_call *c, unsigned type, unsigned flags, const struct dbus_fields *f)
{
        c->w = (struct dbus_out){c->bytes, 0, sizeof c->bytes, 0};
        dbus_o_header(&c->w, type, flags, ++db_serial, f);
        c->body = c->w.length;
}

static long db_end(struct db_call *c)
{
        dbus_o_end(&c->w, c->body);
        return c->w.length;
}

static void db_send(long slot, struct db_call *c)
{
        db_feed(slot, c->bytes, db_end(c));
}

static unsigned long db_last_serial(void)
{
        return db_serial;
}

// A method call with strings (up to three) as its arguments.
static void db_call(long slot, const char *dest, const char *path, const char *iface, const char *member, const char *sig, const char *a,
                    const char *b)
{
        struct db_call c;
        struct dbus_fields f = {path, iface, member, "", dest, "", sig, 0, 0};

        db_begin(&c, DBUS_CALL, 0, &f);
        if (a)
                dbus_o_string(&c.w, a);
        if (b)
                dbus_o_string(&c.w, b);
        db_send(slot, &c);
}

static void db_hello(long slot)
{
        db_call(slot, DBUS_NAME_BUS, "/org/freedesktop/DBus", DBUS_NAME_BUS, "Hello", "", 0, 0);
}

static void db_su(long slot, const char *member, const char *name, unsigned flags)
{
        struct db_call c;
        struct dbus_fields f = {"/org/freedesktop/DBus", DBUS_NAME_BUS, member, "", DBUS_NAME_BUS, "", "su", 0, 0};

        db_begin(&c, DBUS_CALL, 0, &f);
        dbus_o_string(&c.w, name);
        dbus_o_u32(&c.w, flags);
        db_send(slot, &c);
}

static void db_request(long slot, const char *name, unsigned flags)
{
        db_su(slot, "RequestName", name, flags);
}

struct db_seen
{
        struct dbus_message m;
        unsigned char bytes[8192];
        long size;
        int ok;
};

// The next message queued for a client, parsed (and taken off the queue); ok 0 when there is none or it is not one.
static void db_next(long slot, struct db_seen *s)
{
        const unsigned char *bytes;
        long n = dbus_out(slot, &bytes), total;

        s->ok = 0;
        if (n < 16)
                return;
        total = DBUS_ALIGN(16 + (long)dbus_get32(bytes + 12), 8) + (long)dbus_get32(bytes + 4);
        if (total > n || total > (long)sizeof s->bytes)
                return;
        memcpy(s->bytes, bytes, (size_t)total);
        s->size = total;
        dbus_sent(slot, total);
        s->ok = dbus_parse(s->bytes, total, &s->m) == 0;
        if (!s->ok && getenv("DBDEBUG"))
        {
                fprintf(stderr, "unparsable message of %ld bytes:", total);
                for (long k = 0; k < total; k++)
                        fprintf(stderr, "%s%02x", k % 16 ? " " : "\n", s->bytes[k]);
                fprintf(stderr, "\n");
        }
}

static unsigned long db_u32_body(const struct db_seen *s)
{
        return dbus_get32(s->bytes + s->m.body_at);
}

static int db_string_arg(const struct db_seen *s, long index, char *text, long room)
{
        return dbus_arg_string(&s->m, index, text, room) >= 0;
}

// Takes messages off a client's queue until one is of the member (a signal) named, or there are none.
static int db_find(long slot, struct db_seen *s, unsigned type, const char *member)
{
        for (int k = 0; k < 64; k++)
        {
                db_next(slot, s);
                if (!s->ok)
                        return 0;
                if (s->m.type == type && (!member || dbus_equal(s->m.member, member)))
                        return 1;
        }
        return 0;
}

static void db_drain(long slot)
{
        const unsigned char *bytes;
        long n = dbus_out(slot, &bytes);

        dbus_sent(slot, n);
}

// The bytes queued for a client and not yet taken.
static long db_queued(long slot)
{
        const unsigned char *bytes;

        return dbus_out(slot, &bytes);
}

static void db_reset(void)
{
        for (long slot = 0; slot < DBUS_CLIENTS; slot++)
                dbus_drop(slot);
        dbus_stop();
        db_allocs = db_releases = db_time = 0;
        db_serial = 0;
        dbus_start(&db_host);
}

/* ---- the rows ---- */

static void db_test_auth(void)
{
        long slot;
        char text[300];
        const char *external = "\0AUTH EXTERNAL 31303030\r\n";

        db_reset();
        slot = dbus_connect(7, 1000);
        db_feed(slot, external, 25);
        db_take_text(slot, text, sizeof text);
        DBCHECK(!strncmp(text, "OK ", 3) && strlen(text) == 3 + 32 + 2 && !strcmp(text + 35, "\r\n"), "EXTERNAL with the uid of the peer is accepted: %s", text);
        db_feed(slot, "NEGOTIATE_UNIX_FD\r\n", 19);
        db_take_text(slot, text, sizeof text);
        DBCHECK(!strncmp(text, "ERROR", 5), "passing descriptors is refused, not agreed");
        db_feed(slot, "BEGIN\r\n", 7);
        DBCHECK(!dbus_dead(slot), "BEGIN begins");
        dbus_drop(slot);

        slot = dbus_connect(8, 1000);
        db_feed(slot, "\0AUTH EXTERNAL 31303031\r\n", 25);
        db_take_text(slot, text, sizeof text);
        DBCHECK(!strncmp(text, "REJECTED", 8), "the uid of another is rejected");
        db_feed(slot, "BEGIN\r\n", 7);
        DBCHECK(dbus_dead(slot), "and a BEGIN without an authentication ends the connection");
        dbus_drop(slot);

        slot = dbus_connect(8, 1000);
        db_feed(slot, "\0AUTH EXTERNAL 3130303030\r\n", 27);
        db_take_text(slot, text, sizeof text);
        DBCHECK(!strncmp(text, "REJECTED", 8), "the uid with more after it is rejected");
        db_feed(slot, "AUTH EXTERNAL 3130\r\n", 20);
        db_take_text(slot, text, sizeof text);
        DBCHECK(!strncmp(text, "REJECTED", 8), "and so is part of it");
        db_feed(slot, "AUTH EXTERNAL 3100\r\n", 20);
        db_take_text(slot, text, sizeof text);
        DBCHECK(!strncmp(text, "REJECTED", 8), "and so is a byte that is not a digit of it");
        dbus_drop(slot);

        slot = dbus_connect(8, 1000);
        db_feed(slot, "\0AUTH\r\n", 7);
        db_take_text(slot, text, sizeof text);
        DBCHECK(!strncmp(text, "REJECTED EXTERNAL ANONYMOUS", 27), "AUTH alone lists the mechanisms");
        db_feed(slot, "AUTH EXTERNAL\r\n", 15);
        db_take_text(slot, text, sizeof text);
        DBCHECK(!strncmp(text, "DATA", 4), "EXTERNAL with no data asks for it");
        db_feed(slot, "DATA 31303030\r\n", 15);
        db_take_text(slot, text, sizeof text);
        DBCHECK(!strncmp(text, "OK ", 3), "and takes it");
        dbus_drop(slot);

        slot = dbus_connect(8, 1000);
        db_feed(slot, "\0AUTH EXTERNAL\r\nDATA\r\nNEGOTIATE_UNIX_FD\r\nBEGIN\r\n", 1 + 15 + 6 + 19 + 7);
        db_take_text(slot, text, sizeof text);
        DBCHECK(!strncmp(text, "DATA\r\nOK ", 9) && !dbus_dead(slot), "sd-bus's pipeline (EXTERNAL, an empty DATA, NEGOTIATE_UNIX_FD, BEGIN) is accepted: %s", text);
        db_hello(slot);
        {
                struct db_seen s;

                DBCHECK(db_find(slot, &s, DBUS_RETURN, 0), "and Hello after it is answered");
        }
        dbus_drop(slot);

        slot = dbus_connect(8, 1000);
        db_feed(slot, "\0AUTH ANONYMOUS\r\n", 17);
        db_take_text(slot, text, sizeof text);
        DBCHECK(!strncmp(text, "OK ", 3), "ANONYMOUS is accepted");
        db_feed(slot, "NONSENSE\r\n", 10);
        db_take_text(slot, text, sizeof text);
        DBCHECK(!strncmp(text, "ERROR", 5) && !dbus_dead(slot), "a command that is none is an ERROR line");
        dbus_drop(slot);

        slot = dbus_connect(8, 1000);
        db_feed(slot, "x", 1);
        DBCHECK(dbus_dead(slot), "the first byte is a NUL or the connection ends");
        dbus_drop(slot);

        slot = dbus_connect(8, 1000);
        {
                char big[1200];

                memset(big, 'a', sizeof big);
                big[0] = 0;
                db_feed(slot, big, sizeof big);
        }
        DBCHECK(dbus_dead(slot), "an authentication line past its cap ends the connection");
        dbus_drop(slot);

        slot = dbus_connect(8, 1000);
        db_time += DBUS_HELLO_MS + 10;
        dbus_tick();
        DBCHECK(dbus_dead(slot), "a connection that says nothing for a few seconds ends");
        dbus_drop(slot);

        {
                const char *host_map = "         0          0 4294967295\n";
                const char *bowl_map = "      1000          0          1\n";
                const char *nested_map = "      1000       1000          1\n";
                const char *escalate_map = "         0       1000          1\n";
                const char *overflow_map = "4294967290          0         10\n";
                const char *trailing_junk = "      1000          0          1 junk\n";

                DBCHECK(dbus_uid_from_map(host_map, (long)strlen(host_map), 0) == 0 &&
                            dbus_uid_from_map(host_map, (long)strlen(host_map), 1000) == 1000,
                        "host uid_map keeps both root and unprivileged uids");
                DBCHECK(dbus_uid_from_map(bowl_map, (long)strlen(bowl_map), 0) == 1000,
                        "bowl uid_map maps kernel peer uid 0 to inside uid 1000");
                DBCHECK(dbus_uid_from_map(nested_map, (long)strlen(nested_map), 0) == 1000,
                        "nested single-uid uid_map unwraps inside uid 1000 from outer uid 0");
                DBCHECK(dbus_uid_from_map(escalate_map, (long)strlen(escalate_map), 1000) == 1000,
                        "unprivileged userns mapping cannot elevate a non-zero peer uid to 0");
                DBCHECK(dbus_uid_from_map(overflow_map, (long)strlen(overflow_map), 9) == 9 &&
                            dbus_uid_from_map(trailing_junk, (long)strlen(trailing_junk), 0) == 0 &&
                            dbus_uid_from_map("", 0, 0) == 0,
                        "overflowing, malformed, and empty uid_maps fall back safely");

                slot = dbus_connect(9, dbus_uid_from_map(bowl_map, (long)strlen(bowl_map), 0));
                db_feed(slot, "\0AUTH EXTERNAL 31303030\r\n", 25);
                db_take_text(slot, text, sizeof text);
                DBCHECK(!strncmp(text, "OK ", 3), "container client mapped through uid_map authenticates with EXTERNAL");
                dbus_drop(slot);
        }

        DBCHECK(db_allocs == db_releases, "every record is returned (%ld %ld)", db_allocs, db_releases);
}

static void db_test_hello_and_names(void)
{
        long a, b, c;
        struct db_seen s;
        char text[200];

        db_reset();
        a = db_connect(1000);
        db_hello(a);
        DBCHECK(db_find(a, &s, DBUS_RETURN, 0) && dbus_equal(s.m.signature, "s") && s.m.reply == db_last_serial() &&
                    db_string_arg(&s, 0, text, sizeof text) && !strcmp(text, ":1.2") && dbus_equal(s.m.sender, DBUS_NAME_BUS),
                "Hello answers the unique name, from the bus: %s", text);
        DBCHECK(db_find(a, &s, DBUS_SIGNAL, "NameAcquired") && db_string_arg(&s, 0, text, sizeof text) && !strcmp(text, ":1.2"),
                "and NameAcquired follows");
        db_hello(a);
        DBCHECK(db_find(a, &s, DBUS_ERROR, 0), "a second Hello is an error");

        c = db_connect(1000);
        db_call(c, DBUS_NAME_BUS, "/org/freedesktop/DBus", DBUS_NAME_BUS, "ListNames", "", 0, 0);
        DBCHECK(dbus_dead(c), "a call before Hello that is not Hello ends the connection");
        dbus_drop(c);

        b = db_connect(1000);
        db_hello(b);
        db_drain(b);
        db_drain(a);
        db_request(a, "org.example.Alpha", 0);
        DBCHECK(db_find(a, &s, DBUS_RETURN, 0) && db_u32_body(&s) == 1, "RequestName: primary owner");
        db_request(a, "org.example.Alpha", 0);
        DBCHECK(db_find(a, &s, DBUS_RETURN, 0) && db_u32_body(&s) == 4, "again: already the owner");
        db_request(b, "org.example.Alpha", 0);
        DBCHECK(db_find(b, &s, DBUS_RETURN, 0) && db_u32_body(&s) == 3, "another client: the name exists");
        db_request(b, DBUS_NAME_PORTAL, 2);
        DBCHECK(db_find(b, &s, DBUS_RETURN, 0) && db_u32_body(&s) == 3, "the portal's name is not for taking, replacing or not");
        db_request(b, DBUS_NAME_BUS, 0);
        DBCHECK(db_find(b, &s, DBUS_ERROR, 0), "the bus's own name is not for owning");
        db_request(b, ":1.99", 0);
        DBCHECK(db_find(b, &s, DBUS_ERROR, 0), "a unique name is not for owning");
        db_request(b, "no-dots", 0);
        DBCHECK(db_find(b, &s, DBUS_ERROR, 0), "a name that is none is refused");

        db_call(b, DBUS_NAME_BUS, "/org/freedesktop/DBus", DBUS_NAME_BUS, "GetNameOwner", "s", "org.example.Alpha", 0);
        DBCHECK(db_find(b, &s, DBUS_RETURN, 0) && db_string_arg(&s, 0, text, sizeof text) && !strcmp(text, ":1.2"), "GetNameOwner");
        db_call(b, DBUS_NAME_BUS, "/org/freedesktop/DBus", DBUS_NAME_BUS, "GetNameOwner", "s", "org.example.Nobody", 0);
        DBCHECK(db_find(b, &s, DBUS_ERROR, 0) && !strcmp(s.m.error, "org.freedesktop.DBus.Error.NameHasNoOwner"), "and of no one is an error");
        db_call(b, DBUS_NAME_BUS, "/org/freedesktop/DBus", DBUS_NAME_BUS, "GetNameOwner", "s", DBUS_NAME_PORTAL, 0);
        DBCHECK(db_find(b, &s, DBUS_RETURN, 0) && db_string_arg(&s, 0, text, sizeof text) && !strcmp(text, ":1.1"), "the portal is :1.1");
        db_call(b, DBUS_NAME_BUS, "/org/freedesktop/DBus", DBUS_NAME_BUS, "NameHasOwner", "s", "org.example.Alpha", 0);
        DBCHECK(db_find(b, &s, DBUS_RETURN, 0) && db_u32_body(&s) == 1, "NameHasOwner");
        db_su(b, "StartServiceByName", "org.example.Nobody", 0);
        DBCHECK(db_find(b, &s, DBUS_ERROR, 0) && !strcmp(s.m.error, "org.freedesktop.DBus.Error.ServiceUnknown"), "a service nobody provides cannot be started");
        db_su(b, "StartServiceByName", DBUS_NAME_PORTAL, 0);
        DBCHECK(db_find(b, &s, DBUS_RETURN, 0) && db_u32_body(&s) == 2, "and the portal, which runs, is already running");

        // The names are told to those who ask: NameOwnerChanged with arg0 of one name.
        db_call(b, DBUS_NAME_BUS, "/org/freedesktop/DBus", DBUS_NAME_BUS, "AddMatch", "s",
                "type='signal',sender='org.freedesktop.DBus',interface='org.freedesktop.DBus',member='NameOwnerChanged',arg0='org.example.Beta'", 0);
        DBCHECK(db_find(b, &s, DBUS_RETURN, 0), "AddMatch");
        db_drain(b);
        db_request(a, "org.example.Gamma", 0);
        DBCHECK(!db_find(b, &s, DBUS_SIGNAL, "NameOwnerChanged"), "a name it did not ask for is not told");
        db_request(a, "org.example.Beta", 0);
        DBCHECK(db_find(b, &s, DBUS_SIGNAL, "NameOwnerChanged") && db_string_arg(&s, 0, text, sizeof text) && !strcmp(text, "org.example.Beta") &&
                    db_string_arg(&s, 2, text, sizeof text) && !strcmp(text, ":1.2"),
                "a name it asked for is, with the new owner");
        db_call(a, DBUS_NAME_BUS, "/org/freedesktop/DBus", DBUS_NAME_BUS, "ReleaseName", "s", "org.example.Beta", 0);
        db_drain(a);
        DBCHECK(db_find(b, &s, DBUS_SIGNAL, "NameOwnerChanged") && db_string_arg(&s, 2, text, sizeof text) && !text[0], "and when it goes");
        db_call(b, DBUS_NAME_BUS, "/org/freedesktop/DBus", DBUS_NAME_BUS, "RemoveMatch", "s",
                "type='signal',sender='org.freedesktop.DBus',interface='org.freedesktop.DBus',member='NameOwnerChanged',arg0='org.example.Beta'", 0);
        DBCHECK(db_find(b, &s, DBUS_RETURN, 0), "RemoveMatch");
        db_call(b, DBUS_NAME_BUS, "/org/freedesktop/DBus", DBUS_NAME_BUS, "RemoveMatch", "s", "type='signal',member='Nothing'", 0);
        DBCHECK(db_find(b, &s, DBUS_ERROR, 0), "a rule that was never added cannot be removed");
        db_call(b, DBUS_NAME_BUS, "/org/freedesktop/DBus", DBUS_NAME_BUS, "AddMatch", "s", "bogus='1'", 0);
        DBCHECK(db_find(b, &s, DBUS_ERROR, 0) && !strcmp(s.m.error, "org.freedesktop.DBus.Error.MatchRuleInvalid"), "a rule with a key unknown is invalid");
        db_call(b, DBUS_NAME_BUS, "/org/freedesktop/DBus", DBUS_NAME_BUS, "GetConnectionUnixUser", "s", ":1.2", 0);
        DBCHECK(db_find(b, &s, DBUS_RETURN, 0) && db_u32_body(&s) == 1000, "GetConnectionUnixUser");
        db_call(b, DBUS_NAME_BUS, "/org/freedesktop/DBus", DBUS_NAME_BUS, "GetConnectionCredentials", "s", ":1.2", 0);
        DBCHECK(db_find(b, &s, DBUS_RETURN, 0) && dbus_equal(s.m.signature, "a{sv}"), "GetConnectionCredentials");
        db_call(b, DBUS_NAME_BUS, "/org/freedesktop/DBus", DBUS_NAME_BUS, "ListNames", "", 0, 0);
        DBCHECK(db_find(b, &s, DBUS_RETURN, 0) && dbus_equal(s.m.signature, "as"), "ListNames");
        db_call(b, DBUS_NAME_BUS, "/org/freedesktop/DBus", "org.freedesktop.DBus.Peer", "Ping", "", 0, 0);
        DBCHECK(db_find(b, &s, DBUS_RETURN, 0), "Ping");
        db_call(b, DBUS_NAME_BUS, "/org/freedesktop/DBus", DBUS_NAME_BUS, "NoSuchMethod", "", 0, 0);
        DBCHECK(db_find(b, &s, DBUS_ERROR, 0) && !strcmp(s.m.error, "org.freedesktop.DBus.Error.UnknownMethod"), "an unknown method is an error");

        // A name goes with its owner, and is told.
        db_call(b, DBUS_NAME_BUS, "/org/freedesktop/DBus", DBUS_NAME_BUS, "AddMatch", "s", "type='signal',member='NameOwnerChanged'", 0);
        db_drain(b);
        dbus_drop(a);
        DBCHECK(db_find(b, &s, DBUS_SIGNAL, "NameOwnerChanged"), "a client that goes is told to those who watch");
        dbus_drop(b);
        DBCHECK(db_allocs == db_releases, "every record is returned (%ld %ld)", db_allocs, db_releases);
}

static void db_test_routing(void)
{
        long a, b, c;
        struct db_seen s;
        char text[200];
        unsigned long serial;

        db_reset();
        a = db_connect(1000);
        b = db_connect(1000);
        c = db_connect(1000);
        db_hello(a);
        db_hello(b);
        db_hello(c);
        db_drain(a);
        db_drain(b);
        db_drain(c);
        db_request(b, "org.example.Server", 0);
        db_drain(b);

        // a calls the server by name, claiming to be someone else
        {
                struct db_call call;
                struct dbus_fields f = {"/org/example", "org.example.Interface", "Ask", "", "org.example.Server", ":1.77", "s", 0, 0};

                db_begin(&call, DBUS_CALL, 0, &f);
                dbus_o_string(&call.w, "question");
                db_send(a, &call);
                serial = db_last_serial();
        }
        DBCHECK(db_find(b, &s, DBUS_CALL, "Ask") && dbus_equal(s.m.sender, ":1.2") && s.m.serial == serial && dbus_equal(s.m.destination, "org.example.Server") &&
                    db_string_arg(&s, 0, text, sizeof text) && !strcmp(text, "question"),
                "a call by name reaches its owner with the sender the bus says, not the one the caller wrote (%s)", s.m.sender);
        // c forges the reply
        {
                struct db_call reply;
                struct dbus_fields f = {"", "", "", "", ":1.2", "", "s", serial, 1};

                db_begin(&reply, DBUS_RETURN, 1, &f);
                dbus_o_string(&reply.w, "forged");
                db_send(c, &reply);
        }
        DBCHECK(!db_find(a, &s, DBUS_RETURN, 0), "a reply from a client the call was not asked of is dropped");
        // b replies
        {
                struct db_call reply;
                struct dbus_fields f = {"", "", "", "", ":1.2", "", "s", serial, 1};

                db_begin(&reply, DBUS_RETURN, 1, &f);
                dbus_o_string(&reply.w, "answer");
                db_send(b, &reply);
        }
        DBCHECK(db_find(a, &s, DBUS_RETURN, 0) && s.m.reply == serial && dbus_equal(s.m.sender, ":1.3") && db_string_arg(&s, 0, text, sizeof text) &&
                    !strcmp(text, "answer"),
                "the reply reaches the caller, from the callee");
        {
                struct db_call reply;
                struct dbus_fields f = {"", "", "", "", ":1.2", "", "s", serial, 1};

                db_begin(&reply, DBUS_RETURN, 1, &f);
                dbus_o_string(&reply.w, "again");
                db_send(b, &reply);
        }
        DBCHECK(!db_find(a, &s, DBUS_RETURN, 0), "and a second reply to the same call is dropped");

        // a callee that goes before it answers: the caller is told
        {
                struct db_call call;
                struct dbus_fields f = {"/org/example", "org.example.Interface", "Wait", "", "org.example.Server", "", "", 0, 0};

                db_begin(&call, DBUS_CALL, 0, &f);
                db_send(a, &call);
                serial = db_last_serial();
        }
        db_drain(b);
        dbus_drop(b);
        DBCHECK(db_find(a, &s, DBUS_ERROR, 0) && s.m.reply == serial && !strcmp(s.m.error, "org.freedesktop.DBus.Error.NoReply"),
                "a callee that goes without answering is an error to its caller");
        db_call(a, "org.example.Server", "/org/example", "org.example.Interface", "Ask", "", 0, 0);
        DBCHECK(db_find(a, &s, DBUS_ERROR, 0) && !strcmp(s.m.error, "org.freedesktop.DBus.Error.ServiceUnknown"), "a name nobody owns is ServiceUnknown");
        db_call(a, ":1.77", "/org/example", "org.example.Interface", "Ask", "", 0, 0);
        DBCHECK(db_find(a, &s, DBUS_ERROR, 0), "and so is a unique name nobody has");

        // signals: broadcast to those whose rule they match, not to the others
        b = db_connect(1000);
        db_hello(b);
        db_drain(a);
        db_drain(b);
        db_drain(c);
        db_call(b, DBUS_NAME_BUS, "/org/freedesktop/DBus", DBUS_NAME_BUS, "AddMatch", "s", "type='signal',interface='org.example.Interface',path_namespace='/org/example'", 0);
        db_call(c, DBUS_NAME_BUS, "/org/freedesktop/DBus", DBUS_NAME_BUS, "AddMatch", "s", "type='signal',member='Other'", 0);
        db_drain(b);
        db_drain(c);
        {
                struct db_call sig;
                struct dbus_fields f = {"/org/example/thing", "org.example.Interface", "Changed", "", "", "", "s", 0, 0};

                db_begin(&sig, DBUS_SIGNAL, 1, &f);
                dbus_o_string(&sig.w, "payload");
                db_send(a, &sig);
        }
        DBCHECK(db_find(b, &s, DBUS_SIGNAL, "Changed") && dbus_equal(s.m.sender, ":1.2") && !db_find(c, &s, DBUS_SIGNAL, "Changed"),
                "a signal reaches the client whose rule it matches, and no other");
        {
                struct db_call sig;
                struct dbus_fields f = {"/elsewhere", "org.example.Interface", "Changed", "", "", "", "", 0, 0};

                db_begin(&sig, DBUS_SIGNAL, 1, &f);
                db_send(a, &sig);
        }
        DBCHECK(!db_find(b, &s, DBUS_SIGNAL, "Changed"), "a path outside the namespace does not match");
        {
                struct db_call sig;
                struct dbus_fields f = {"/elsewhere", "org.example.Interface", "Private", "", ":1.4", "", "", 0, 0};

                db_begin(&sig, DBUS_SIGNAL, 1, &f);
                db_send(a, &sig);
        }
        DBCHECK(db_find(c, &s, DBUS_SIGNAL, "Private") && !db_find(b, &s, DBUS_SIGNAL, "Private"), "a signal sent to a client is its alone");

        // path and namespace rules
        db_call(b, DBUS_NAME_BUS, "/org/freedesktop/DBus", DBUS_NAME_BUS, "AddMatch", "s",
                "type='signal',interface='ca.desrt.dconf.Writer',path='/ca/desrt/dconf/Writer/user',arg0path='/org/gnome/desktop/interface/'", 0);
        db_call(c, DBUS_NAME_BUS, "/org/freedesktop/DBus", DBUS_NAME_BUS, "AddMatch", "s", "type='signal',interface='org.example.N',arg0namespace='com.example.app'", 0);
        db_call(b, DBUS_NAME_BUS, "/org/freedesktop/DBus", DBUS_NAME_BUS, "AddMatch", "s", "type='signal',interface='org.example.P',arg0path='/a/b'", 0);
        db_drain(b);
        db_drain(c);
        {
                static const struct
                {
                        const char *interface, *path, *arg;
                        int b_sees, c_sees;
                } cases[] = {
                    {"ca.desrt.dconf.Writer", "/ca/desrt/dconf/Writer/user", "/org/gnome/desktop/interface/font-name", 1, 0},
                    {"ca.desrt.dconf.Writer", "/ca/desrt/dconf/Writer/user", "/org/gnome/desktop/interface/", 1, 0},
                    {"ca.desrt.dconf.Writer", "/ca/desrt/dconf/Writer/user", "/org/gnome/", 1, 0},
                    {"ca.desrt.dconf.Writer", "/ca/desrt/dconf/Writer/user", "/org/gnome/desktop/sound/", 0, 0},
                    {"ca.desrt.dconf.Writer", "/ca/desrt/dconf/Writer/user", "/org/gnome/desktop/interfaces", 0, 0},
                    {"org.example.N", "/o", "com.example.app", 0, 1},
                    {"org.example.N", "/o", "com.example.app.sub", 0, 1},
                    {"org.example.N", "/o", "com.example.apple", 0, 0},
                    {"org.example.P", "/o", "/a/b", 1, 0},
                    {"org.example.P", "/o", "/a/bc", 0, 0},
                    {"org.example.P", "/o", "/a/b/c", 0, 0},
                };

                for (unsigned k = 0; k < sizeof cases / sizeof cases[0]; k++)
                {
                        struct db_call sig;
                        struct dbus_fields f = {cases[k].path, cases[k].interface, "Changed", "", "", "", "s", 0, 0};

                        db_begin(&sig, DBUS_SIGNAL, 1, &f);
                        dbus_o_string(&sig.w, cases[k].arg);
                        db_send(a, &sig);
                        DBCHECK(db_find(b, &s, DBUS_SIGNAL, "Changed") == cases[k].b_sees && db_find(c, &s, DBUS_SIGNAL, "Changed") == cases[k].c_sees,
                                "the rules by path and by namespace on %s: b %d, c %d", cases[k].arg, cases[k].b_sees, cases[k].c_sees);
                        db_drain(b);
                        db_drain(c);
                }
        }

        // the pending table is bounded
        db_request(c, "org.example.Sink", 0);
        db_drain(c);
        for (int k = 0; k < DBUS_PENDING_PER + 4; k++)
                db_call(a, "org.example.Sink", "/o", "org.example.I", "M", "", 0, 0);
        {
                long errors = 0;

                while (db_find(a, &s, DBUS_ERROR, 0))
                        errors += !strcmp(s.m.error, "org.freedesktop.DBus.Error.LimitsExceeded");
                DBCHECK(errors == 4, "calls that wait for a reply are bounded per client (%ld refused)", errors);
        }
        dbus_drop(a);
        dbus_drop(b);
        dbus_drop(c);
        DBCHECK(db_allocs == db_releases, "every record is returned (%ld %ld)", db_allocs, db_releases);
        for (long k = 0; k < DBUS_PENDING; k++)
                DBCHECK(!dbus.pending[k].used, "no call waits for a reply when everyone is gone");
}

// A double's bits, for the settings.
static unsigned long db_bits(double v)
{
        unsigned long bits;

        memcpy(&bits, &v, 8);
        return bits;
}

static void db_test_portal(void)
{
        long a, b;
        struct db_seen s;
        char text[200];
        unsigned long colour[3] = {0, 0, 0};

        db_reset();
        dbus_setting_set("org.freedesktop.appearance", "color-scheme", 'u', 1, 0, 0);
        colour[0] = db_bits(0.5);
        colour[1] = db_bits(0.25);
        colour[2] = db_bits(1.0);
        dbus_setting_set("org.freedesktop.appearance", "accent-color", 'c', 0, colour, 0);
        dbus_setting_set("org.freedesktop.appearance", "contrast", 'u', 0, 0, 0);
        dbus_setting_set("org.gnome.desktop.interface", "text-scaling-factor", 'd', 0, (unsigned long[]){db_bits(1.25), 0, 0}, 0);
        dbus_setting_set("org.gnome.desktop.interface", "gtk-theme", 's', 0, 0, "Adwaita-dark");
        dbus_setting_set("org.gnome.desktop.interface", "cursor-size", 'i', 32, 0, 0);
        dbus_setting_set("org.gnome.desktop.interface", "enable-animations", 'b', 1, 0, 0);
        a = db_connect(1000);
        b = db_connect(1000);
        db_hello(a);
        db_hello(b);
        db_drain(a);
        db_drain(b);

        db_call(a, DBUS_NAME_PORTAL, DBUS_PATH_PORTAL, "org.freedesktop.portal.Settings", "ReadOne", "ss", "org.freedesktop.appearance", "color-scheme");
        DBCHECK(db_find(a, &s, DBUS_RETURN, 0) && dbus_equal(s.m.signature, "v") && s.bytes[s.m.body_at] == 1 && s.bytes[s.m.body_at + 1] == 'u' &&
                    dbus_get32(s.bytes + s.m.body_at + 4) == 1 && dbus_equal(s.m.sender, ":1.1"),
                "ReadOne: the color-scheme is a variant of u, 1 (prefers dark)");
        db_call(a, DBUS_NAME_PORTAL, DBUS_PATH_PORTAL, "org.freedesktop.portal.Settings", "Read", "ss", "org.freedesktop.appearance", "color-scheme");
        DBCHECK(db_find(a, &s, DBUS_RETURN, 0) && dbus_equal(s.m.signature, "v") && s.bytes[s.m.body_at] == 1 && s.bytes[s.m.body_at + 1] == 'v' &&
                    s.bytes[s.m.body_at + 3] == 1 && s.bytes[s.m.body_at + 4] == 'u',
                "Read, the old one, answers a variant that holds the variant");
        db_call(a, DBUS_NAME_PORTAL, DBUS_PATH_PORTAL, "org.freedesktop.portal.Settings", "ReadOne", "ss", "org.freedesktop.appearance", "accent-color");
        DBCHECK(db_find(a, &s, DBUS_RETURN, 0) && !memcmp(s.bytes + s.m.body_at, "\5(ddd)\0", 7) && s.m.body_length == 8 + 24 &&
                    !memcmp(s.bytes + s.m.body_at + 8, &colour[0], 8) && !memcmp(s.bytes + s.m.body_at + 24, &colour[2], 8),
                "the accent is a variant of (ddd)");
        db_call(a, DBUS_NAME_PORTAL, DBUS_PATH_PORTAL, "org.freedesktop.portal.Settings", "ReadOne", "ss", "org.freedesktop.appearance", "nothing");
        DBCHECK(db_find(a, &s, DBUS_ERROR, 0) && !strcmp(s.m.error, "org.freedesktop.portal.Error.NotFound"), "a setting that is none is NotFound");

        {
                struct db_call call;
                struct dbus_fields f = {DBUS_PATH_PORTAL, "org.freedesktop.portal.Settings", "ReadAll", "", DBUS_NAME_PORTAL, "", "as", 0, 0};
                long length_at;

                db_begin(&call, DBUS_CALL, 0, &f);
                dbus_o_u32(&call.w, 0);
                length_at = call.w.length - 4;
                dbus_o_string(&call.w, "org.gnome.*");
                dbus_o_patch32(&call.w, length_at, (unsigned long)(call.w.length - length_at - 4));
                db_send(a, &call);
        }
        DBCHECK(db_find(a, &s, DBUS_RETURN, 0) && dbus_equal(s.m.signature, "a{sa{sv}}") && s.ok, "ReadAll with org.gnome.* answers a{sa{sv}}");
        {
                int has_gnome = 0, has_appearance = 0;

                for (long at = 0; at + 25 < s.size; at++)
                {
                        has_gnome |= !memcmp(s.bytes + at, "org.gnome.desktop.interface", 28);
                        has_appearance |= !memcmp(s.bytes + at, "org.freedesktop.appearance", 27);
                }
                DBCHECK(has_gnome && !has_appearance, "and holds the gnome namespace and not the other");
        }
        {
                struct db_call call;
                struct dbus_fields f = {DBUS_PATH_PORTAL, "org.freedesktop.portal.Settings", "ReadAll", "", DBUS_NAME_PORTAL, "", "as", 0, 0};

                db_begin(&call, DBUS_CALL, 0, &f);
                dbus_o_u32(&call.w, 0);
                db_send(a, &call);
        }
        {
                int has_gnome = 0, has_appearance = 0;

                DBCHECK(db_find(a, &s, DBUS_RETURN, 0), "ReadAll with no filter answers");
                for (long at = 0; at + 25 < s.size; at++)
                {
                        has_gnome |= !memcmp(s.bytes + at, "org.gnome.desktop.interface", 28);
                        has_appearance |= !memcmp(s.bytes + at, "org.freedesktop.appearance", 27);
                }
                DBCHECK(has_gnome && has_appearance, "and holds all of them");
        }
        db_call(a, DBUS_NAME_PORTAL, DBUS_PATH_PORTAL, "org.freedesktop.DBus.Properties", "Get", "ss", "org.freedesktop.portal.Settings", "version");
        DBCHECK(db_find(a, &s, DBUS_RETURN, 0) && s.bytes[s.m.body_at + 1] == 'u' && dbus_get32(s.bytes + s.m.body_at + 4) == 2, "the Settings portal is version 2");
        // (the portal's Notification was this row's interface until the bus served it, 2026-11-15: db_test_notifications holds its version)
        db_call(a, DBUS_NAME_PORTAL, DBUS_PATH_PORTAL, "org.freedesktop.DBus.Properties", "Get", "ss", "org.freedesktop.portal.ScreenCast", "version");
        DBCHECK(db_find(a, &s, DBUS_ERROR, 0) && !strcmp(s.m.error, "org.freedesktop.DBus.Error.UnknownProperty"), "a portal it does not have is an error, as a browser's check for it expects");
        db_call(a, DBUS_NAME_PORTAL, DBUS_PATH_PORTAL, "org.freedesktop.DBus.Properties", "Get", "ss", "org.freedesktop.portal.FileChooser", "version");
        DBCHECK(db_find(a, &s, DBUS_RETURN, 0) && s.bytes[s.m.body_at + 1] == 'u' && dbus_get32(s.bytes + s.m.body_at + 4) == 1,
                "the FileChooser answers its version, 1, which is below what a browser needs to use it (the reason is in dbus.c)");
        db_call(a, DBUS_NAME_PORTAL, DBUS_PATH_PORTAL, "org.freedesktop.DBus.Properties", "Get", "ss", "org.freedesktop.host.portal.Registry", "version");
        DBCHECK(db_find(a, &s, DBUS_RETURN, 0) && s.bytes[s.m.body_at + 1] == 'u' && dbus_get32(s.bytes + s.m.body_at + 4) == 1,
                "the host Registry portal answers version 1");
        db_call(a, DBUS_NAME_PORTAL, DBUS_PATH_PORTAL, "org.freedesktop.DBus.Properties", "Get", "ss", "org.freedesktop.portal.Inhibit", "version");
        DBCHECK(db_find(a, &s, DBUS_RETURN, 0) && s.bytes[s.m.body_at + 1] == 'u' && dbus_get32(s.bytes + s.m.body_at + 4) == 1,
                "the Inhibit portal answers version 1");
        db_call(a, DBUS_NAME_PORTAL, DBUS_PATH_PORTAL, "org.freedesktop.portal.FileChooser", "OpenFile", "", 0, 0);
        DBCHECK(db_find(a, &s, DBUS_ERROR, 0) && !strcmp(s.m.error, "org.freedesktop.DBus.Error.UnknownMethod"), "and its methods are not there");
        db_call(a, DBUS_NAME_PORTAL, DBUS_PATH_PORTAL, "org.freedesktop.portal.Settings", "Write", "", 0, 0);
        DBCHECK(db_find(a, &s, DBUS_ERROR, 0) && !strcmp(s.m.error, "org.freedesktop.DBus.Error.UnknownMethod"), "a method it has not is UnknownMethod");
        db_call(a, DBUS_NAME_PORTAL, "/elsewhere", "org.freedesktop.portal.Settings", "ReadAll", "", 0, 0);
        DBCHECK(db_find(a, &s, DBUS_ERROR, 0), "another object is an error");
        {
                struct db_call call;
                struct dbus_fields f = {DBUS_PATH_PORTAL, "org.freedesktop.host.portal.Registry", "Register", "", DBUS_NAME_PORTAL, "", "sa{sv}", 0, 0};

                db_begin(&call, DBUS_CALL, 0, &f);
                dbus_o_string(&call.w, "org.example.App");
                dbus_o_u32(&call.w, 0);
                dbus_o_pad(&call.w, 8);
                db_send(a, &call);
                DBCHECK(db_find(a, &s, DBUS_RETURN, 0) && s.m.body_length == 0, "the registry's Register is answered with nothing");
        }
        {
                struct db_call call;
                struct dbus_fields f = {DBUS_PATH_PORTAL, "org.freedesktop.portal.Inhibit", "Inhibit", "", DBUS_NAME_PORTAL, "", "sua{sv}", 0, 0};

                db_begin(&call, DBUS_CALL, 0, &f);
                dbus_o_string(&call.w, "");
                dbus_o_u32(&call.w, 8);
                dbus_o_u32(&call.w, 0);
                dbus_o_pad(&call.w, 8);
                db_send(a, &call);
                DBCHECK(db_find(a, &s, DBUS_RETURN, 0) && dbus_equal(s.m.signature, "o") &&
                            db_string_arg(&s, 0, text, sizeof text) &&
                            !strcmp(text, "/org/freedesktop/portal/desktop/request/moonwater/1"),
                        "Inhibit returns a request object path");
                db_call(a, DBUS_NAME_PORTAL, "/org/freedesktop/portal/desktop/request/moonwater/1",
                        "org.freedesktop.portal.Request", "Close", "", 0, 0);
                DBCHECK(db_find(a, &s, DBUS_RETURN, 0) && s.m.body_length == 0, "Request.Close on the inhibit handle succeeds");
        }
        db_call(a, DBUS_NAME_PORTAL, DBUS_PATH_PORTAL, "org.freedesktop.DBus.Introspectable", "Introspect", "", 0, 0);
        {
                int found = 0, found_registry = 0;

                DBCHECK(db_find(a, &s, DBUS_RETURN, 0), "Introspect answers");
                for (long at = 0; at + 31 < s.size; at++)
                        found |= !memcmp(s.bytes + at, "org.freedesktop.portal.Settings", 31);
                for (long at = 0; at + 36 < s.size; at++)
                        found_registry |= !memcmp(s.bytes + at, "org.freedesktop.host.portal.Registry", 36);
                DBCHECK(found && found_registry, "and names the Settings and Registry interfaces");
        }

        // a change is told to those who listen for it, once
        db_call(b, DBUS_NAME_BUS, "/org/freedesktop/DBus", DBUS_NAME_BUS, "AddMatch", "s",
                "type='signal',sender='org.freedesktop.portal.Desktop',interface='org.freedesktop.portal.Settings',member='SettingChanged'", 0);
        db_drain(b);
        db_drain(a);
        DBCHECK(dbus_setting_set("org.freedesktop.appearance", "color-scheme", 'u', 2, 0, 0) == 1, "a setting that changes says so");
        DBCHECK(db_find(b, &s, DBUS_SIGNAL, "SettingChanged") && dbus_equal(s.m.sender, ":1.1") && dbus_equal(s.m.signature, "ssv") &&
                    db_string_arg(&s, 0, text, sizeof text) && !strcmp(text, "org.freedesktop.appearance") && db_string_arg(&s, 1, text, sizeof text) &&
                    !strcmp(text, "color-scheme") && s.bytes[s.m.body_at + s.m.body_length - 7] == 1 && s.bytes[s.m.body_at + s.m.body_length - 6] == 'u' &&
                    dbus_get32(s.bytes + s.m.body_at + s.m.body_length - 4) == 2,
                "SettingChanged reaches the client that asked for it, with the new value");
        DBCHECK(!db_find(a, &s, DBUS_SIGNAL, "SettingChanged"), "and not one that did not");
        DBCHECK(dbus_setting_set("org.freedesktop.appearance", "color-scheme", 'u', 2, 0, 0) == 0 && !db_find(b, &s, DBUS_SIGNAL, "SettingChanged"),
                "a setting set to what it is says nothing");
        db_call(a, DBUS_NAME_PORTAL, DBUS_PATH_PORTAL, "org.freedesktop.portal.Settings", "ReadOne", "ss", "org.freedesktop.appearance", "color-scheme");
        DBCHECK(db_find(a, &s, DBUS_RETURN, 0) && dbus_get32(s.bytes + s.m.body_at + 4) == 2, "and a read after it is the new value");
        {
                int made = 0;
                char key[16];

                for (int k = 0; k < DBUS_SETTINGS + 4; k++)
                {
                        snprintf(key, sizeof key, "k%d", k);
                        made += dbus_setting_set("org.example", key, 'u', k, 0, 0) >= 0;
                }
                DBCHECK(made < DBUS_SETTINGS + 4, "the settings are a table of a size (%d made)", made);
        }
        // What a setting is given goes on the wire as it is, so what would not be valid there is no setting: a text cut
        // in a character (63 bytes of "a" and then a letter of two), a text past the room, one that is not UTF-8, a kind
        // that is none. A boolean is 0 or 1 whatever number it is given.
        db_reset();
        {
                char cut[80];
                struct dbus_setting *setting;

                memset(cut, 'a', 62);
                memcpy(cut + 62, "\xc3\xa5", 3);
                DBCHECK(dbus_setting_set("org.example", "cut", 's', 0, 0, cut) == -1 && !dbus_setting_find("org.example", "cut"),
                        "a text that would be cut in a character is no setting");
                cut[62] = 0;
                DBCHECK(dbus_setting_set("org.example", "fits", 's', 0, 0, cut) == 1, "and one that fits is");
                DBCHECK(dbus_setting_set("org.example", "bad", 's', 0, 0, "\xff") == -1, "a text that is not UTF-8 is no setting");
                DBCHECK(dbus_setting_set("org.example", "none", 's', 0, 0, 0) == -1, "nor is a text that is not there");
                DBCHECK(dbus_setting_set("org.example", "\xc3", 'u', 1, 0, 0) == -1, "nor a key that is not UTF-8");
                DBCHECK(dbus_setting_set("org.example", "kind", 'z', 1, 0, 0) == -1, "nor a kind that is none");
                DBCHECK(dbus_setting_set("org.example", "yes", 'b', 7, 0, 0) == 1 && (setting = dbus_setting_find("org.example", "yes")) && setting->number == 1,
                        "a boolean of 7 is true, 1");
        }
        dbus_drop(a);
        dbus_drop(b);
        DBCHECK(db_allocs == db_releases, "every record is returned (%ld %ld)", db_allocs, db_releases);
}

/* ---- hostile messages ---- */

static void db_test_hostile(void)
{
        long slot;

        db_reset();
        slot = db_connect(1000);
        db_hello(slot);
        db_drain(slot);
        db_call(slot, DBUS_NAME_BUS, "/org/freedesktop/DBus", DBUS_NAME_BUS, "ListNames", "", 0, 0);
        {
                struct db_seen s;

                DBCHECK(db_find(slot, &s, DBUS_RETURN, 0), "a client that is well gets its answer");
        }
        dbus_drop(slot);

        // each of these, sent whole after a Hello, ends the connection (and does nothing else)
        {
                static const struct
                {
                        const char *name;
                        unsigned char bytes[160];
                        long size;
                } cases[] = {
                    {"the other byte order", {'B', 1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0}, 16},
                    {"a version that is not 1", {'l', 1, 0, 2, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0}, 16},
                    {"a type of 0, INVALID", {'l', 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0}, 16},
                    {"a serial of 0", {'l', 1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, 16},
                    {"a call with no path or member", {'l', 1, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0}, 16},
                    {"fields longer than the cap", {'l', 1, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0xff, 0xff, 0xff, 0xff}, 16},
                    {"a call that has a body and no fields", {'l', 1, 0, 1, 9, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9}, 25},
                    {"a field of a type that is none", {'l', 1, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 8, 0, 0, 0, 1, 1, 'z', 0, 0, 0, 0, 0}, 24},
                    {"a path that is not one", {'l', 1, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 16, 0, 0, 0, 1, 1, 'o', 0, 3, 0, 0, 0, 'a', 'b', 0, 0, 0, 0, 0, 0}, 32},
                    {"a string with a byte that is not UTF-8", {'l', 1, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 16, 0, 0, 0, 3, 1, 's', 0, 3, 0, 0, 0, 0xc0, 0x80, 0, 0, 0, 0, 0, 0}, 32},
                    {"a string that is not ended", {'l', 1, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 16, 0, 0, 0, 3, 1, 's', 0, 3, 0, 0, 0, 'a', 'b', 'c', 'd', 0, 0, 0, 0}, 32},
                    {"a name with no dot", {'l', 1, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 16, 0, 0, 0, 2, 1, 's', 0, 3, 0, 0, 0, 'a', 'b', 0, 0, 0, 0, 0, 0}, 32},
                    {"padding that is not zero", {'l', 1, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 3, 0, 0, 0, 1, 2, 3, 9, 0, 0, 0, 0}, 24},
                };
                for (unsigned k = 0; k < sizeof cases / sizeof cases[0]; k++)
                {
                        slot = db_connect(1000);
                        db_hello(slot);
                        db_drain(slot);
                        db_feed(slot, cases[k].bytes, cases[k].size);
                        DBCHECK(dbus_dead(slot), "%s ends the connection", cases[k].name);
                        dbus_drop(slot);
                }
        }
        // bodies against their signatures
        {
                struct
                {
                        const char *name;
                        const char *sig;
                        unsigned char body[64];
                        long size;
                } bodies[] = {
                    {"a string whose length passes the body", "s", {0xff, 0, 0, 0, 'a', 0}, 6},
                    {"a boolean of 2", "b", {2, 0, 0, 0}, 4},
                    {"an array whose length passes the body", "ay", {9, 0, 0, 0, 1, 2}, 6},
                    {"an array of dict entries with a half entry", "a{sy}", {5, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 'a'}, 13},
                    {"a variant whose signature is two types", "v", {2, 'y', 'y', 0, 1, 1}, 6},
                    {"a signature that is none", "g", {2, 'a', 'a', 0}, 4},
                    {"an object path that is not one", "o", {2, 0, 0, 0, 'x', 'y', 0}, 7},
                    {"a body shorter than its signature", "ss", {0, 0, 0, 0, 0}, 5},
                    {"a body longer than its signature", "y", {1, 2}, 2},
                    {"padding that is not zero in a body", "su", {1, 0, 0, 0, 'a', 0, 7, 7, 0, 0, 0, 0}, 12},
                    {"a string that is not UTF-8", "s", {1, 0, 0, 0, 0xff, 0}, 6},
                    {"a string with a character cut short", "s", {2, 0, 0, 0, 0xe2, 0x82, 0}, 7},
                    {"a string with a surrogate", "s", {3, 0, 0, 0, 0xed, 0xa0, 0x80, 0}, 8},
                    {"a dictionary keyed by a variant", "a{vy}", {0, 0, 0, 0, 0, 0, 0, 0}, 8},
                    {"a dictionary keyed by an array", "a{asy}", {0, 0, 0, 0, 0, 0, 0, 0}, 8},
                };
                for (unsigned k = 0; k < sizeof bodies / sizeof bodies[0]; k++)
                {
                        struct db_call c;
                        struct dbus_fields f = {"/org/freedesktop/DBus", DBUS_NAME_BUS, "ListNames", "", DBUS_NAME_BUS, "", bodies[k].sig, 0, 0};

                        slot = db_connect(1000);
                        db_hello(slot);
                        db_drain(slot);
                        db_begin(&c, DBUS_CALL, 0, &f);
                        dbus_o_raw(&c.w, bodies[k].body, bodies[k].size);
                        db_send(slot, &c);
                        DBCHECK(dbus_dead(slot), "%s ends the connection", bodies[k].name);
                        dbus_drop(slot);
                }
        }
        // a call whole in everything but a serial of 0
        {
                struct db_call c;
                struct dbus_fields f = {"/org/freedesktop/DBus", DBUS_NAME_BUS, "ListNames", "", DBUS_NAME_BUS, "", "", 0, 0};

                slot = db_connect(1000);
                db_hello(slot);
                db_drain(slot);
                db_begin(&c, DBUS_CALL, 0, &f);
                memset(c.bytes + 8, 0, 4);
                db_send(slot, &c);
                DBCHECK(dbus_dead(slot), "a call with a serial of 0 ends the connection");
                dbus_drop(slot);
        }
        // nesting
        {
                char sig[80];
                struct db_call c;
                struct dbus_fields f = {"/org/freedesktop/DBus", DBUS_NAME_BUS, "ListNames", "", DBUS_NAME_BUS, "", sig, 0, 0};
                int deep;

                for (deep = 0; deep < 40; deep++)
                        sig[deep] = 'a';
                sig[deep] = 'y';
                sig[deep + 1] = 0;
                slot = db_connect(1000);
                db_hello(slot);
                db_drain(slot);
                db_begin(&c, DBUS_CALL, 0, &f);
                db_send(slot, &c);
                DBCHECK(dbus_dead(slot), "a signature nested past 32 arrays ends the connection");
                dbus_drop(slot);
        }
        // a message past the cap, announced by its header
        slot = db_connect(1000);
        db_hello(slot);
        db_drain(slot);
        {
                unsigned char header[16] = {'l', 1, 0, 1, 0xff, 0xff, 0xff, 0x7f, 1, 0, 0, 0, 0, 0, 0, 0};

                db_feed(slot, header, 16);
        }
        DBCHECK(dbus_dead(slot), "a message that announces more than the cap ends the connection");
        dbus_drop(slot);
        // a client that does not read is dropped, not waited for: its queue refuses what does not fit, and once it has read
        // nothing for DBUS_STALL_MS after that, it goes
        {
                long a = db_connect(1000), b = db_connect(1000);
                int dropped = 0;

                db_hello(a);
                db_hello(b);
                db_drain(a);
                db_drain(b);
                db_request(a, "org.example.Flood", 0);
                db_drain(a);
                for (int k = 0; k < 20000 && !dbus_dead(a); k++)
                {
                        struct db_call spam;
                        struct dbus_fields f = {"/o", "org.example.I", "Spam", "", "org.example.Flood", "", "s", 0, 0};

                        db_begin(&spam, DBUS_CALL, DBUS_FLAG_NO_REPLY, &f);
                        dbus_o_string(&spam.w, "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx");
                        db_send(b, &spam);
                        db_drain(b);
                }
                DBCHECK(!dbus_dead(a), "a client whose queue is full is not dropped at once (%ld)", dbus_dead(a));
                db_time += DBUS_STALL_MS / 2;
                dbus_tick();
                DBCHECK(!dbus_dead(a), "nor before it has had DBUS_STALL_MS to read");
                db_time += DBUS_STALL_MS / 2 + 10;
                dbus_tick();
                dropped = dbus_dead(a) == 2;
                DBCHECK(dropped, "a client that does not read fills its queue and is dropped (%ld)", dbus_dead(a));
                DBCHECK(!dbus_dead(b), "and the one that called it is not");
                dbus_drop(a);
                dbus_drop(b);
        }
        DBCHECK(db_allocs == db_releases, "every record is returned (%ld %ld)", db_allocs, db_releases);
}

// A signal of an array of payload bytes from a client to destination (a unique name, or "" for everyone): its length.
static long db_big_signal(unsigned char *into, long room, const char *destination, long payload)
{
        struct dbus_out w = {into, 0, room, 0};
        struct dbus_fields f = {"/o", "org.example.I", "Big", "", destination, "", "ay", 0, 0};
        long body;

        dbus_o_header(&w, DBUS_SIGNAL, DBUS_FLAG_NO_REPLY, ++db_serial, &f);
        body = w.length;
        dbus_o_u32(&w, (unsigned long)payload);
        if (payload >= 0 && w.length + payload <= w.room)
        {
                memset(w.at + w.length, 'x', (size_t)payload);
                w.length += payload;
        }
        else
                w.bad = 1;
        dbus_o_end(&w, body);
        return w.bad ? -1 : w.length;
}

// The messages queued for a client, walked: how many, and the type and error name of the last (an error's, else "").
static long db_queue_last(long slot, unsigned *type, char *error, long room)
{
        const unsigned char *bytes;
        long n = dbus_out(slot, &bytes), at = 0, count = 0;

        *type = 0;
        error[0] = 0;
        while (at + 16 <= n)
        {
                struct dbus_message m;
                long total = DBUS_ALIGN(16 + (long)dbus_get32(bytes + at + 12), 8) + (long)dbus_get32(bytes + at + 4);

                if (at + total > n || dbus_parse(bytes + at, total, &m) != 0)
                        return -1;
                *type = m.type;
                snprintf(error, (size_t)room, "%s", m.error);
                count++;
                at += total;
        }
        return count;
}

/*
        One client cannot have another ended by what it sends it. A client's queue holds what it is sent until it reads
        it; what other clients send it (calls, signals, the bus's own broadcasts) is refused once it fills the most of it
        they may have, and a sender that waits for a reply is told (LimitsExceeded), while the rest of the queue is kept
        for what the bus owes the client, the answers to its own calls. Only a client that does not read what it asked
        for itself is ended. (Before, a message of any client that did not fit ended the client it was sent to, and the
        largest message the bus takes did not fit an empty queue: any program could end any other, a browser or a
        desktop's own, with one signal sent to its unique name.)
*/
static void db_test_queue(void)
{
        static unsigned char big[DBUS_MESSAGE + 64];
        long victim, attacker, header, n, refused = 0, count;
        unsigned type;
        char error[128];
        struct db_seen s;

        db_reset();
        victim = db_connect(1000);
        attacker = db_connect(1001);
        db_hello(victim);
        db_hello(attacker);
        db_drain(victim);
        db_drain(attacker);
        header = db_big_signal(big, sizeof big, ":1.2", 0) - 4;
        n = db_big_signal(big, sizeof big, ":1.2", DBUS_MESSAGE - header - 4);
        DBCHECK(n == DBUS_MESSAGE, "the signal is as large as the bus takes (%ld)", n);
        db_feed(attacker, big, n);
        DBCHECK(!dbus_dead(attacker), "the bus takes it");
        DBCHECK(!dbus_dead(victim) && db_queue_last(victim, &type, error, sizeof error) == 1 && type == DBUS_SIGNAL,
                "and gives it to the client it was sent to, which it does not end (dead %ld)", dbus_dead(victim));

        // a client that does not read, sent more and more: what does not fit is refused, and neither the sender nor it is ended
        n = db_big_signal(big, sizeof big, ":1.2", 200000);
        for (int k = 0; k < 40 && !dbus_dead(victim) && !dbus_dead(attacker); k++)
        {
                long before = db_queued(victim);

                db_feed(attacker, big, n);
                refused += db_queued(victim) == before;
        }
        DBCHECK(!dbus_dead(victim) && !dbus_dead(attacker) && refused > 0, "signals past the room a client has are refused, and end no one (%ld refused, dead %ld %ld)",
                refused, dbus_dead(victim), dbus_dead(attacker));
        DBCHECK(dbus.client[victim]->out_length - dbus.client[victim]->out_head <= DBUS_UNASKED, "and what others sent it is within the most they may have (%ld)",
                dbus.client[victim]->out_length);
        // a broadcast too
        db_call(victim, DBUS_NAME_BUS, "/org/freedesktop/DBus", DBUS_NAME_BUS, "AddMatch", "s", "type='signal',member='Big'", 0);
        n = db_big_signal(big, sizeof big, "", 200000);
        for (int k = 0; k < 10 && !dbus_dead(victim); k++)
                db_feed(attacker, big, n);
        DBCHECK(!dbus_dead(victim) && db_queued(victim) <= DBUS_OUT, "broadcasts to a client whose queue is full end it not (dead %ld)",
                dbus_dead(victim));

        // the client's own call is still answered: the room the bus owes it was kept
        db_call(victim, DBUS_NAME_BUS, "/org/freedesktop/DBus", DBUS_NAME_BUS, "ListNames", "", 0, 0);
        count = db_queue_last(victim, &type, error, sizeof error);
        DBCHECK(!dbus_dead(victim) && count > 0 && type == DBUS_RETURN, "a client whose queue others filled still gets the answer to its own call (type %u)", type);

        // a call that waits for a reply, to a client with no room: the caller is told, and nothing is left waiting
        n = db_big_signal(big, sizeof big, ":1.2", 0);
        for (int k = 0; k < 20000 && dbus.client[victim]->out_length - dbus.client[victim]->out_head + n <= DBUS_UNASKED; k++)
                db_feed(attacker, big, n);
        db_drain(attacker);
        db_call(attacker, ":1.2", "/o", "org.example.I", "Ask", "s", "a question longer than the signals that filled the queue, which are the smallest", 0);
        DBCHECK(db_find(attacker, &s, DBUS_ERROR, 0) && !strcmp(s.m.error, "org.freedesktop.DBus.Error.LimitsExceeded") && !dbus_dead(attacker) &&
                    dbus.client[attacker]->pending == 0,
                "a call to a client with a full queue is an error to its caller (LimitsExceeded), and waits on nothing (%ld)", dbus.client[attacker]->pending);

        // what it was given, read, makes room again
        db_drain(victim);
        n = db_big_signal(big, sizeof big, ":1.2", 1000);
        db_feed(attacker, big, n);
        DBCHECK(db_queue_last(victim, &type, error, sizeof error) == 1 && type == DBUS_SIGNAL, "a client that read is sent to again");

        // a client that does not read the answers to its own calls is ended: it is the one that does not read
        db_drain(victim);
        for (int k = 0; k < 4000 && !dbus_dead(victim); k++)
                db_call(victim, DBUS_NAME_BUS, "/org/freedesktop/DBus", "org.freedesktop.DBus.Introspectable", "Introspect", "", 0, 0);
        DBCHECK(!dbus_dead(victim), "a client whose own answers find no room is not ended at once (%ld)", dbus_dead(victim));
        db_time += DBUS_STALL_MS + 10;
        dbus_tick();
        DBCHECK(dbus_dead(victim) == 2, "a client that does not read what it asked for is ended (%ld)", dbus_dead(victim));
        DBCHECK(!dbus_dead(attacker), "and that ends no other");
        dbus_drop(victim);
        dbus_drop(attacker);
        DBCHECK(db_allocs == db_releases, "every record is returned (%ld %ld)", db_allocs, db_releases);
}

/*
        No client can have another's calls refused by the calls it has waiting. Each client may have DBUS_PENDING_PER
        calls wait for a reply, and the bus's table has room for every client's. (Before, it held 512, eight clients'
        worth: nine clients calling a callee that never answers took all of it, and every call of every other client to
        anyone but the bus was LimitsExceeded for as long as they stayed.)
*/
static void db_test_pending(void)
{
        long sink, caller, made = 0, refused = 0, delivered = 0, waiting = 0;
        struct db_seen s;

        db_reset();
        sink = db_connect(0);
        db_hello(sink);
        db_request(sink, "org.example.Sink", 0);
        db_drain(sink);
        // every client the bus has room for, the sink too (it calls itself), each with all the calls it may have waiting
        for (int k = 0; k < DBUS_CLIENTS; k++)
        {
                caller = k ? db_connect(0) : sink;
                if (caller < 0)
                        break;
                if (k)
                {
                        db_hello(caller);
                        db_drain(caller);
                }
                made++;
                for (int c = 0; c < DBUS_PENDING_PER; c++)
                        db_call(caller, "org.example.Sink", "/o", "org.example.I", "Hang", "", 0, 0);
                if (caller != sink)
                        while (db_find(caller, &s, DBUS_ERROR, 0))
                                refused++;
                while (db_find(sink, &s, DBUS_CALL, "Hang")) // the sink reads every call and answers none
                        delivered++;
                for (; db_find(sink, &s, DBUS_ERROR, 0);)
                        refused++;
                waiting += dbus.client[caller]->pending;
        }
        DBCHECK(made == DBUS_CLIENTS && !refused && delivered == (long)DBUS_CLIENTS * DBUS_PENDING_PER && waiting == delivered,
                "every client may have its %d calls wait for a reply, all of them at once (%ld clients, %ld refused, %ld delivered, %ld waiting)", DBUS_PENDING_PER,
                made, refused, delivered, waiting);
        for (long slot = 0; slot < DBUS_CLIENTS; slot++)
                dbus_drop(slot);
        DBCHECK(db_allocs == db_releases, "every record is returned (%ld %ld)", db_allocs, db_releases);
        for (long k = 0; k < DBUS_PENDING; k++)
                DBCHECK(!dbus.pending[k].used, "no call waits when everyone is gone");
}

static long db_room_lines, db_room_longest;

static void db_room_trace(void *context, const char *line)
{
        long n = (long)strlen(line);

        (void)context;
        db_room_lines++;
        db_room_longest = n > db_room_longest ? n : db_room_longest;
}

/*
        The trace (sessionbus -t) puts a line for each message routed into 400 bytes, the parts of the message and its
        first two string arguments: what a client sends that fills it is cut, and the line ends inside them. (Before, a
        message whose path, interface, member and destination filled the line had the bracket of its arguments and the
        line's end written after it, one byte past the buffer on the stack, two in all.)
*/
static void db_test_trace_room(void)
{
        static const struct dbus_host traced = {0, db_alloc, db_release, db_now, db_random, db_room_trace, 0, 0, 0};
        char path[260], member[256], interface[256], argument[200];
        long slot;

        db_reset();
        dbus_stop();
        dbus_start(&traced);
        db_room_lines = db_room_longest = 0;
        slot = db_connect(1000);
        db_hello(slot);
        db_drain(slot);
        path[0] = '/';
        memset(path + 1, 'p', 250);
        path[251] = 0;
        memset(member, 'm', 250);
        member[250] = 0;
        memcpy(interface, "org.", 4);
        memset(interface + 4, 'i', 240);
        interface[244] = 0;
        memset(argument, 'a', 50); // short enough to be shown
        argument[50] = 0;
        db_call(slot, "org.example.Nobody", path, interface, member, "ss", argument, argument);
        DBCHECK(db_room_lines == 1 && db_room_longest <= 399, "the trace's line is cut inside its 400 bytes, the bracket and the end in them (%ld long, %ld lines)",
                db_room_longest, db_room_lines);
        DBCHECK(!dbus_dead(slot), "and the call is answered as any is");
        dbus_drop(slot);
        db_reset();
}

static void db_test_caps(void)
{
        long slots[DBUS_CLIENTS + 2], made = 0, per_uid = 0;
        struct db_seen s;
        long a;

        db_reset();
        for (long k = 0; k < DBUS_CLIENTS + 2; k++)
        {
                slots[k] = dbus_connect(2000 + k, 0);
                made += slots[k] >= 0;
        }
        DBCHECK(made == DBUS_CLIENTS, "no more clients than the table has (%ld)", made);
        for (long k = 0; k < DBUS_CLIENTS + 2; k++)
                if (slots[k] >= 0)
                        dbus_drop(slots[k]);
        for (long k = 0; k < DBUS_PER_UID + 3; k++)
        {
                slots[k] = dbus_connect(3000 + k, 1234);
                per_uid += slots[k] >= 0;
        }
        DBCHECK(per_uid == DBUS_PER_UID, "and no more than DBUS_PER_UID of one user (%ld)", per_uid);
        for (long k = 0; k < DBUS_PER_UID + 3; k++)
                if (slots[k] >= 0)
                        dbus_drop(slots[k]);

        a = db_connect(1000);
        db_hello(a);
        db_drain(a);
        {
                char name[40];

                for (int k = 0; k < DBUS_NAMES_PER + 3; k++)
                {
                        snprintf(name, sizeof name, "org.example.N%d", k);
                        db_request(a, name, 0);
                        db_drain(a);
                }
                DBCHECK(dbus.client[a]->names <= DBUS_NAMES_PER, "a client owns no more names than the cap (%ld)", dbus.client[a]->names);
        }
        {
                int added = 0;
                char rule[60];

                for (int k = 0; k < DBUS_RULES + 5; k++)
                {
                        snprintf(rule, sizeof rule, "type='signal',member='M%d'", k);
                        db_call(a, DBUS_NAME_BUS, "/org/freedesktop/DBus", DBUS_NAME_BUS, "AddMatch", "s", rule, 0);
                        if (db_find(a, &s, DBUS_RETURN, 0))
                                added++;
                        db_drain(a);
                }
                DBCHECK(added == DBUS_RULES, "and no more rules than the cap (%d)", added);
        }
        dbus_drop(a);
        DBCHECK(db_allocs == db_releases, "every record is returned (%ld %ld)", db_allocs, db_releases);
}

/* ---- what one client can do to another (2026-11-04 review) ---- */

// A message written field by field, for the fields the bus's writer has no word for (a REPLY_SERIAL of 0, code 0, code 10).
struct db_raw
{
        unsigned char *bytes;
        struct dbus_out w;
        long body;
};

static void db_raw_begin(struct db_raw *r, unsigned char *bytes, long room, unsigned type, unsigned flags)
{
        r->bytes = bytes;
        r->w = (struct dbus_out){bytes, 0, room, 0};
        dbus_o_u8(&r->w, 'l');
        dbus_o_u8(&r->w, type);
        dbus_o_u8(&r->w, flags);
        dbus_o_u8(&r->w, 1);
        dbus_o_u32(&r->w, 0);
        dbus_o_u32(&r->w, ++db_serial);
        dbus_o_u32(&r->w, 0);
}

// A field's code and its variant's type; the value is the caller's to write.
static void db_raw_field(struct db_raw *r, unsigned code, const char *type)
{
        dbus_o_pad(&r->w, 8);
        dbus_o_u8(&r->w, code);
        dbus_o_sig(&r->w, type);
}

static void db_raw_string(struct db_raw *r, unsigned code, char kind, const char *value)
{
        char type[2] = {kind, 0};

        db_raw_field(r, code, type);
        if (kind == 'g')
                dbus_o_sig(&r->w, value);
        else
                dbus_o_string(&r->w, value);
}

static void db_raw_body(struct db_raw *r)
{
        dbus_o_patch32(&r->w, 12, (unsigned long)(r->w.length - 16));
        dbus_o_pad(&r->w, 8);
        r->body = r->w.length;
}

static long db_raw_end(struct db_raw *r)
{
        dbus_o_patch32(&r->w, 4, (unsigned long)(r->w.length - r->body));
        return r->w.bad ? -1 : r->w.length;
}

// db_find with more patience: a reply behind a flood.
static int db_find_far(long slot, struct db_seen *s, unsigned type, const char *member)
{
        for (int k = 0; k < 4000; k++)
        {
                db_next(slot, s);
                if (!s->ok)
                        return 0;
                if (s->m.type == type && (!member || dbus_equal(s->m.member, member)))
                        return 1;
        }
        return 0;
}

static void db_rule(long slot, const char *rule)
{
        db_call(slot, DBUS_NAME_BUS, "/org/freedesktop/DBus", DBUS_NAME_BUS, "AddMatch", "s", rule, 0);
}

static const char *db_unique(long slot)
{
        return dbus.client[slot]->unique;
}

/*
        A flood: before 2026-11-04 a client's queue that overflowed dropped the client, so any client could disconnect any
        other by writing it more than its queue in one go (unicast signals need no rule), and take its names after it.
*/
static void db_test_flood(void)
{
        long victim, attacker, third;
        struct db_seen s;
        static unsigned char batch[900000];
        long n = 0;

        db_reset();
        victim = db_connect(1000);
        attacker = db_connect(1000);
        third = db_connect(1000);
        db_hello(victim);
        db_hello(attacker);
        db_hello(third);
        db_request(victim, "org.example.Victim", 0);
        db_drain(victim);
        db_drain(attacker);
        db_drain(third);
        for (int k = 0; k < 800; k++)
        {
                struct db_call c;
                struct dbus_fields f = {"/a", "org.example.Flood", "M", "", db_unique(victim), "", "s", 0, 0};
                char text[1001];

                memset(text, 'x', 1000);
                text[1000] = 0;
                db_begin(&c, DBUS_SIGNAL, 0, &f);
                dbus_o_string(&c.w, text);
                memcpy(batch + n, c.bytes, (size_t)db_end(&c));
                n += c.w.length;
        }
        db_feed(attacker, batch, n);
        DBCHECK(!dbus_dead(victim), "a client written more than its queue holds in one go is not dropped for it (%ld)", dbus_dead(victim));
        DBCHECK(!dbus_dead(attacker), "and the client that wrote it is not either (it broke no rule)");
        DBCHECK(db_queued(victim) <= DBUS_UNASKED && db_queued(victim) > DBUS_UNASKED - 2048,
                "what it did not ask for fills its queue to DBUS_UNASKED and no further (%ld of %d)", db_queued(victim), DBUS_UNASKED);
        {
                char text[1501];

                // larger than the room the flood's last signal left
                memset(text, 'q', 1500);
                text[1500] = 0;
                db_call(third, "org.example.Victim", "/o", "org.example.I", "M", "s", text, 0);
        }
        DBCHECK(db_find(third, &s, DBUS_ERROR, 0) && !strcmp(s.m.error, "org.freedesktop.DBus.Error.LimitsExceeded"),
                "a call to it while its queue is full of what it did not read is an error to the caller");
        {
                long waiting = 0;

                for (long k = 0; k < DBUS_PENDING; k++)
                        waiting += dbus.pending[k].used;
                DBCHECK(!waiting && dbus.client[third]->pending == 0, "and waits for no reply (%ld)", waiting);
        }
        db_call(victim, DBUS_NAME_BUS, "/org/freedesktop/DBus", DBUS_NAME_BUS, "ListNames", "", 0, 0);
        DBCHECK(db_find_far(victim, &s, DBUS_RETURN, 0) && dbus_equal(s.m.signature, "as"),
                "its own call is answered behind the flood: what it asks for has room the others cannot take");
        db_drain(victim);
        db_feed(attacker, batch, n);
        db_drain(victim);
        db_time += DBUS_STALL_MS + 10;
        dbus_tick();
        DBCHECK(!dbus_dead(victim), "a client that is flooded and reads is never dropped (%ld)", dbus_dead(victim));
        db_feed(attacker, batch, n);
        db_time += DBUS_STALL_MS + 10;
        dbus_tick();
        DBCHECK(dbus_dead(victim) == 2, "a client that is flooded and reads nothing is dropped after DBUS_STALL_MS (%ld)", dbus_dead(victim));
        dbus_drop(victim);
        db_drain(attacker);

        // a queue read a little at a time while more comes keeps every message whole and in order, through its moves to the front
        victim = db_connect(1000);
        db_hello(victim);
        db_drain(victim);
        db_drain(attacker);
        {
                long sent = 0, taken = 0, in_order = 1;

                // a backlog of a hundred, then seven in and seven out a turn: the queue never empties, its end reaches the end of
                // the buffer every sixty turns or so and what is unread moves to the front
                for (int turn = 0; turn < 400; turn++)
                {
                        for (int k = 0; k < (turn ? 7 : 100); k++)
                        {
                                struct db_call c;
                                struct dbus_fields f = {"/a", "org.example.Order", "M", "", db_unique(victim), "", "suy", 0, 0};
                                char text[901];

                                memset(text, 'a' + (int)(sent % 26), 900);
                                text[900] = 0;
                                db_begin(&c, DBUS_SIGNAL, 0, &f);
                                dbus_o_string(&c.w, text);
                                dbus_o_u32(&c.w, (unsigned long)sent);
                                dbus_o_u8(&c.w, (unsigned)(sent * 7 + 1) & 255); // the last byte of each, never the same twice running
                                db_send(attacker, &c);
                                sent++;
                        }
                        for (int k = 0; k < (turn ? 7 : 0) || (turn == 399 && db_queued(victim)); k++)
                        {
                                db_next(victim, &s);
                                if (!s.ok)
                                        break;
                                in_order &= dbus_get32(s.bytes + s.m.body_at + DBUS_ALIGN(4 + 900 + 1, 4)) == (unsigned long)taken &&
                                            s.bytes[s.m.body_at + 4] == 'a' + taken % 26 && s.m.body_length == DBUS_ALIGN(4 + 900 + 1, 4) + 5 &&
                                            s.bytes[s.m.body_at + s.m.body_length - 1] == ((taken * 7 + 1) & 255);
                                taken++;
                        }
                }
                DBCHECK(sent == 100 + 399 * 7 && taken == sent && in_order && !dbus_dead(victim),
                        "a queue read part by part as more comes gives every message whole and in order (%ld of %ld)", taken, sent);
        }
        dbus_drop(victim);

        // the largest message there is reaches a client that is reading: stamped with its sender it is past DBUS_MESSAGE
        victim = db_connect(1000);
        db_hello(victim);
        db_drain(victim);
        {
                unsigned char *big = malloc(DBUS_MESSAGE);
                struct db_raw r;
                long body_room, size;

                db_raw_begin(&r, big, DBUS_MESSAGE, DBUS_SIGNAL, 0);
                db_raw_string(&r, 1, 'o', "/a");
                db_raw_string(&r, 2, 's', "org.example.Big");
                db_raw_string(&r, 3, 's', "M");
                db_raw_string(&r, 6, 's', db_unique(victim));
                db_raw_string(&r, 8, 'g', "ay");
                db_raw_body(&r);
                body_room = DBUS_MESSAGE - r.w.length - 4;
                dbus_o_u32(&r.w, (unsigned long)body_room);
                memset(big + r.w.length, 7, (size_t)body_room);
                r.w.length += body_room;
                size = db_raw_end(&r);
                db_drain(attacker);
                db_feed(attacker, big, size);
                DBCHECK(size == DBUS_MESSAGE && !dbus_dead(attacker) && db_queued(victim) > DBUS_MESSAGE,
                        "a message of DBUS_MESSAGE bytes reaches its client with the sender stamped on it (%ld queued)", db_queued(victim));
                db_drain(victim);
                db_feed(attacker, big, size);
                db_feed(attacker, big, size);
                DBCHECK(!dbus_dead(victim) && db_queued(victim) > DBUS_MESSAGE && db_queued(victim) <= DBUS_UNASKED,
                        "and a second one before it read is refused, not the client");
                free(big);
        }
        dbus_drop(victim);
        dbus_drop(attacker);
        dbus_drop(third);
        DBCHECK(db_allocs == db_releases, "every record is returned (%ld %ld)", db_allocs, db_releases);
}

/*
        Messages no client library takes. libdbus and GDBus end their own connection when they are sent a message they
        cannot read (libdbus: "Message is corrupted"; GDBus: "Cannot deserialize message" and the connection closes), so
        each of these, had the bus passed it on, would have let any client disconnect any other: the reserved Local
        interface and path (libdbus and GDBus both), and a reply serial of 0 (libdbus). Each now ends the connection
        that sent it, and reaches nobody.
*/
static void db_test_reserved(void)
{
        static const struct
        {
                const char *name;
                unsigned type;
                const char *path, *interface;
                int reply_zero, code_zero, unicast;
        } cases[] = {
            {"a signal on org.freedesktop.DBus.Local sent to a client", DBUS_SIGNAL, "/a", "org.freedesktop.DBus.Local", 0, 0, 1},
            {"a signal on org.freedesktop.DBus.Local to everyone", DBUS_SIGNAL, "/a", "org.freedesktop.DBus.Local", 0, 0, 0},
            {"a signal from /org/freedesktop/DBus/Local", DBUS_SIGNAL, "/org/freedesktop/DBus/Local", "org.example.I", 0, 0, 1},
            {"a call of org.freedesktop.DBus.Local", DBUS_CALL, "/a", "org.freedesktop.DBus.Local", 0, 0, 1},
            {"a call on /org/freedesktop/DBus/Local", DBUS_CALL, "/org/freedesktop/DBus/Local", "org.example.I", 0, 0, 1},
            {"a call carrying a reply serial of 0", DBUS_CALL, "/a", "org.example.I", 1, 0, 1},
            {"a signal carrying a reply serial of 0", DBUS_SIGNAL, "/a", "org.example.I", 1, 0, 1},
            {"a signal with a header field of code 0", DBUS_SIGNAL, "/a", "org.example.I", 0, 1, 0},
        };

        for (unsigned k = 0; k < sizeof cases / sizeof cases[0]; k++)
        {
                long victim, attacker, size;
                unsigned char bytes[512];
                struct db_raw r;

                db_reset();
                victim = db_connect(1000);
                attacker = db_connect(1000);
                db_hello(victim);
                db_hello(attacker);
                db_request(victim, "org.example.Victim", 0);
                db_rule(victim, "type='signal'");
                db_drain(victim);
                db_drain(attacker);
                db_raw_begin(&r, bytes, sizeof bytes, cases[k].type, 0);
                db_raw_string(&r, 1, 'o', cases[k].path);
                db_raw_string(&r, 2, 's', cases[k].interface);
                db_raw_string(&r, 3, 's', "Disconnected");
                if (cases[k].reply_zero)
                {
                        db_raw_field(&r, 5, "u");
                        dbus_o_u32(&r.w, 0);
                }
                if (cases[k].code_zero)
                {
                        db_raw_field(&r, 0, "u");
                        dbus_o_u32(&r.w, 1);
                }
                if (cases[k].unicast)
                        db_raw_string(&r, 6, 's', cases[k].type == DBUS_CALL ? "org.example.Victim" : db_unique(victim));
                db_raw_body(&r);
                size = db_raw_end(&r);
                db_feed(attacker, bytes, size);
                DBCHECK(dbus_dead(attacker) == 1 && db_queued(victim) == 0, "%s ends the connection that sent it and reaches nobody (%ld, %ld queued)",
                        cases[k].name, dbus_dead(attacker), db_queued(victim));
        }
        db_reset();
        {
                // a reply of serial 0 to a call that waits: libdbus would end the caller for it
                long caller = db_connect(1000), callee = db_connect(1000), size;
                unsigned char bytes[256];
                struct db_raw r;
                struct db_seen s;

                db_hello(caller);
                db_hello(callee);
                db_request(callee, "org.example.Callee", 0);
                db_drain(caller);
                db_drain(callee);
                db_call(caller, "org.example.Callee", "/o", "org.example.I", "M", "", 0, 0);
                DBCHECK(db_find(callee, &s, DBUS_CALL, "M"), "the call reaches its callee");
                db_raw_begin(&r, bytes, sizeof bytes, DBUS_RETURN, 1);
                db_raw_field(&r, 5, "u");
                dbus_o_u32(&r.w, 0);
                db_raw_string(&r, 6, 's', db_unique(caller));
                db_raw_body(&r);
                size = db_raw_end(&r);
                db_feed(callee, bytes, size);
                DBCHECK(dbus_dead(callee) == 1 && db_queued(caller) == 0, "a reply with a reply serial of 0 ends its sender and reaches nobody");
        }
        /*
                An array GDBus misreads (found by test/dbus_libraries.py on the grammar fuzz's output): of structs or dict entries,
                its length 8-aligned so that 4 bytes of padding follow it, and its last element beginning where the one before
                ended, no more than 4 bytes before the end. GLib 2.90.1 reads (a(ay)y) of [([1,2,3,4],), ([],)] and 9 as
                [([1,2,3,4],)] and 0, and ends its connection where the misreading meets bytes that are not valid.
        */
        {
                static const struct
                {
                        const char *name;
                        long last, ok;
                } layouts[] = {
                    {"an array GDBus would read short (its last struct an empty ay after one that ends on 8)", 0, 0},
                    {"and the same with a byte in its last element, which GDBus reads whole", 1, 1},
                };

                for (unsigned k = 0; k < 2; k++)
                {
                        long victim, attacker, size, length_at, first;
                        unsigned char bytes[512];
                        struct db_raw r;
                        struct db_seen s;

                        db_reset();
                        victim = db_connect(1000);
                        attacker = db_connect(1000);
                        db_hello(victim);
                        db_hello(attacker);
                        db_rule(victim, "type='signal'");
                        db_drain(victim);
                        db_drain(attacker);
                        db_raw_begin(&r, bytes, sizeof bytes, DBUS_SIGNAL, 0);
                        db_raw_string(&r, 1, 'o', "/a");
                        db_raw_string(&r, 2, 's', "org.example.I");
                        db_raw_string(&r, 3, 's', "Short");
                        db_raw_string(&r, 8, 'g', "a(ay)y");
                        db_raw_body(&r);
                        dbus_o_u32(&r.w, 0);
                        length_at = r.w.length - 4;
                        dbus_o_pad(&r.w, 8);
                        first = r.w.length;
                        dbus_o_u32(&r.w, 4);
                        dbus_o_raw(&r.w, "\1\2\3\4", 4);
                        dbus_o_pad(&r.w, 8);
                        dbus_o_u32(&r.w, (unsigned long)layouts[k].last);
                        if (layouts[k].last)
                                dbus_o_u8(&r.w, 5);
                        dbus_o_patch32(&r.w, length_at, (unsigned long)(r.w.length - first));
                        dbus_o_u8(&r.w, 9);
                        size = db_raw_end(&r);
                        db_feed(attacker, bytes, size);
                        if (layouts[k].ok)
                                DBCHECK(!dbus_dead(attacker) && db_find(victim, &s, DBUS_SIGNAL, "Short"), "%s is passed on", layouts[k].name);
                        else
                                DBCHECK(dbus_dead(attacker) == 1 && db_queued(victim) == 0, "%s ends its sender and reaches nobody", layouts[k].name);
                }
        }
        db_reset();
}

// What the specification allows that the bus refused before 2026-11-04 (each ended a client that did nothing wrong).
static void db_test_allowed(void)
{
        long a, b;
        struct db_seen s;

        db_reset();
        a = db_connect(1000);
        b = db_connect(1000);
        db_hello(a);
        db_hello(b);
        db_rule(b, "type='signal'");
        db_drain(a);
        db_drain(b);
        {
                // a header field of a code the bus does not know, of any type: kept by nobody, the message passed on without it
                unsigned char bytes[512];
                struct db_raw r;
                long size;

                db_raw_begin(&r, bytes, sizeof bytes, DBUS_SIGNAL, 0);
                db_raw_string(&r, 1, 'o', "/a");
                db_raw_string(&r, 2, 's', "org.example.I");
                db_raw_string(&r, 3, 's', "Unknown");
                db_raw_field(&r, 11, "y");
                dbus_o_u8(&r.w, 5);
                db_raw_field(&r, 200, "a{sv}");
                dbus_o_u32(&r.w, 0);
                {
                        long length_at = r.w.length - 4, first;

                        dbus_o_pad(&r.w, 8);
                        first = r.w.length;
                        dbus_o_string(&r.w, "key");
                        dbus_o_sig(&r.w, "u");
                        dbus_o_u32(&r.w, 9);
                        dbus_o_patch32(&r.w, length_at, (unsigned long)(r.w.length - first));
                }
                db_raw_body(&r);
                size = db_raw_end(&r);
                db_feed(a, bytes, size);
                DBCHECK(!dbus_dead(a) && db_find(b, &s, DBUS_SIGNAL, "Unknown") && s.size < size,
                        "header fields of codes the bus does not know (a byte, a dictionary) are taken, and not passed on");
                // a type of message the bus does not know is ignored
                bytes[1] = 7;
                db_feed(a, bytes, size);
                DBCHECK(!dbus_dead(a) && db_queued(b) == 0, "a message of a type the bus does not know is ignored, not the end of its sender");
                // the same with a field of code 10 that is not valid for its type
                db_raw_begin(&r, bytes, sizeof bytes, DBUS_SIGNAL, 0);
                db_raw_string(&r, 1, 'o', "/a");
                db_raw_string(&r, 2, 's', "org.example.I");
                db_raw_string(&r, 3, 's', "Unknown");
                db_raw_field(&r, 10, "b");
                dbus_o_u32(&r.w, 2);
                db_raw_body(&r);
                size = db_raw_end(&r);
                db_feed(a, bytes, size);
                DBCHECK(dbus_dead(a) == 1 && db_queued(b) == 0, "but a field of a code it does not know is still valid for its type (a boolean of 2 is not)");
                dbus_drop(a);
                a = db_connect(1000);
                db_hello(a);
                db_drain(a);
        }
        // nesting as libdbus counts it: arrays, dict entries, structs and variants, a value no deeper than 64
        {
                static const struct
                {
                        const char *name;
                        int variants, dicts, ok;
                } nests[] = {
                    {"41 variants, one in another, the byte at 41", 40, 0, 1},
                    {"64 variants in a variant, the last at depth 63, its byte at 64", 63, 0, 1},
                    {"65 variants, the byte at 65", 64, 0, 0},
                    {"21 levels of v(a{sv}), the byte at 64", 21, 1, 1},
                    {"22 levels of v(a{sv}), the byte at 67", 22, 1, 0},
                };

                for (unsigned k = 0; k < sizeof nests / sizeof nests[0]; k++)
                {
                        static unsigned char bytes[8192];
                        struct db_raw r;
                        long size;

                        db_raw_begin(&r, bytes, sizeof bytes, DBUS_SIGNAL, 0);
                        db_raw_string(&r, 1, 'o', "/a");
                        db_raw_string(&r, 2, 's', "org.example.I");
                        db_raw_string(&r, 3, 's', "Deep");
                        db_raw_string(&r, 8, 'g', "v");
                        db_raw_body(&r);
                        if (!nests[k].dicts)
                        {
                                for (int v = 0; v < nests[k].variants; v++)
                                        dbus_o_sig(&r.w, "v");
                                dbus_o_sig(&r.w, "y");
                                dbus_o_u8(&r.w, 1);
                        }
                        else
                        {
                                long length_at[32], first[32];

                                for (int v = 0; v < nests[k].variants; v++)
                                {
                                        dbus_o_sig(&r.w, "a{sv}");
                                        dbus_o_u32(&r.w, 0);
                                        length_at[v] = r.w.length - 4;
                                        dbus_o_pad(&r.w, 8);
                                        first[v] = r.w.length;
                                        dbus_o_string(&r.w, "k");
                                }
                                dbus_o_sig(&r.w, "y");
                                dbus_o_u8(&r.w, 7);
                                for (int v = nests[k].variants - 1; v >= 0; v--)
                                        dbus_o_patch32(&r.w, length_at[v], (unsigned long)(r.w.length - first[v]));
                        }
                        size = db_raw_end(&r);
                        db_feed(a, bytes, size);
                        if (nests[k].ok)
                                DBCHECK(!dbus_dead(a) && db_find(b, &s, DBUS_SIGNAL, "Deep"), "%s is taken and passed on", nests[k].name);
                        else
                                DBCHECK(dbus_dead(a) == 1 && db_queued(b) == 0, "%s ends its sender and reaches nobody", nests[k].name);
                        db_drain(b);
                        if (dbus_dead(a))
                        {
                                dbus_drop(a);
                                a = db_connect(1000);
                                db_hello(a);
                                db_drain(a);
                        }
                }
        }
        {
                // libdbus counts arrays, structs and dict entries apart in a signature, each to 32: twenty structs in thirteen
                // dict entries is a signature, which the bus refused counting dict entries as structs
                char sig[128];
                long n = 0;
                struct db_call c;
                struct dbus_fields f = {"/a", "org.example.I", "Sig", "", "", "", sig, 0, 0};

                for (int k = 0; k < 13; k++)
                        sig[n++] = 'a', sig[n++] = '{', sig[n++] = 's';
                for (int k = 0; k < 20; k++)
                        sig[n++] = '(';
                sig[n++] = 'y';
                for (int k = 0; k < 20; k++)
                        sig[n++] = ')';
                for (int k = 0; k < 13; k++)
                        sig[n++] = '}';
                sig[n] = 0;
                db_begin(&c, DBUS_SIGNAL, 0, &f);
                dbus_o_u32(&c.w, 0);
                dbus_o_pad(&c.w, 8);
                db_send(a, &c);
                DBCHECK(!dbus_dead(a) && db_find(b, &s, DBUS_SIGNAL, "Sig"), "a signature of twenty structs in thirteen dict entries is taken");
                for (int k = 0; k < 13; k++)
                        sig[3 * k + 1] = '(';
                for (long k = n - 13; k < n; k++)
                        sig[k] = ')';
                db_begin(&c, DBUS_SIGNAL, 0, &f);
                dbus_o_u32(&c.w, 0);
                dbus_o_pad(&c.w, 8);
                db_send(a, &c);
                DBCHECK(dbus_dead(a) == 1, "and thirty-three structs are not (%s)", sig);
        }
        dbus_drop(a);
        dbus_drop(b);
        DBCHECK(db_allocs == db_releases, "every record is returned (%ld %ld)", db_allocs, db_releases);
}

// The bus's own methods, where they said what the specification does not.
static void db_test_methods(void)
{
        long a, b;
        struct db_seen s;
        char text[200], name[40];

        db_reset();
        a = db_connect(1000);
        b = db_connect(1000);
        db_hello(a);
        db_hello(b);
        db_drain(a);
        db_drain(b);
        for (int k = 0; k < DBUS_NAMES_PER; k++)
        {
                snprintf(name, sizeof name, "org.example.Many%d", k);
                db_request(a, name, 0);
        }
        db_drain(a);
        db_request(a, "org.example.OneTooMany", 0);
        DBCHECK(db_find(a, &s, DBUS_ERROR, 0) && !strcmp(s.m.error, "org.freedesktop.DBus.Error.LimitsExceeded"),
                "a name past the cap is LimitsExceeded, not a reply of 0 (which is no answer RequestName has)");
        db_request(b, "org.example.Replace", 0);
        db_request(b, "org.example.Replace", 1);
        db_drain(b);
        db_call(a, DBUS_NAME_BUS, "/org/freedesktop/DBus", DBUS_NAME_BUS, "ReleaseName", "s", "org.example.Many0", 0);
        db_drain(a);
        db_request(a, "org.example.Replace", 2);
        DBCHECK(db_find(a, &s, DBUS_RETURN, 0) && db_u32_body(&s) == 1,
                "an owner that asks again with allow-replacement may then be replaced, as dbus-daemon has it");
        DBCHECK(db_find(b, &s, DBUS_SIGNAL, "NameLost"), "and is told it lost the name");
        db_call(a, DBUS_NAME_BUS, "/org/freedesktop/DBus", DBUS_NAME_BUS, "ReleaseName", "s", db_unique(b), 0);
        DBCHECK(db_find(a, &s, DBUS_ERROR, 0) && !strcmp(s.m.error, "org.freedesktop.DBus.Error.InvalidArgs"), "ReleaseName of a unique name is InvalidArgs");
        db_call(a, DBUS_NAME_BUS, "/org/freedesktop/DBus", DBUS_NAME_BUS, "ReleaseName", "s", "not a name", 0);
        DBCHECK(db_find(a, &s, DBUS_ERROR, 0) && !strcmp(s.m.error, "org.freedesktop.DBus.Error.InvalidArgs"), "and of a text that is no name");
        db_call(a, DBUS_NAME_BUS, "/org/freedesktop/DBus", DBUS_NAME_BUS, "ListActivatableNames", "", 0, 0);
        DBCHECK(db_find(a, &s, DBUS_RETURN, 0) && dbus_equal(s.m.signature, "as") && s.m.body_length > 4, "ListActivatableNames answers");
        {
                long count = 0, at = 4, limit = 4 + (long)dbus_get32(s.bytes + s.m.body_at);
                int bus = 0, portal = 0, other = 0;

                while (at < limit)
                {
                        const char *t = (const char *)s.bytes + s.m.body_at + at + 4;

                        bus += !strcmp(t, DBUS_NAME_BUS);
                        portal += !strcmp(t, DBUS_NAME_PORTAL);
                        other += strcmp(t, DBUS_NAME_BUS) && strcmp(t, DBUS_NAME_PORTAL);
                        at = DBUS_ALIGN(at + 4 + (long)dbus_get32(s.bytes + s.m.body_at + at) + 1, 4);
                        count++;
                }
                DBCHECK(count == 2 && bus == 1 && portal == 1 && !other, "and it is the bus and the portal once each, no client's name (%ld names)", count);
        }
        {
                // a broadcast looks through a client's rules up to its last: added three, removed the last two, it looks at one
                db_rule(a, "type='signal',member='R1'");
                db_rule(a, "type='signal',member='R2'");
                db_rule(a, "type='signal',member='R3'");
                db_call(a, DBUS_NAME_BUS, "/org/freedesktop/DBus", DBUS_NAME_BUS, "RemoveMatch", "s", "type='signal',member='R3'", 0);
                db_call(a, DBUS_NAME_BUS, "/org/freedesktop/DBus", DBUS_NAME_BUS, "RemoveMatch", "s", "type='signal',member='R2'", 0);
                db_drain(a);
                DBCHECK(dbus.client[a]->rules == 1 && dbus.client[a]->rule[0].used, "the rules a broadcast looks through end at the last one kept (%ld)",
                        dbus.client[a]->rules);
                db_call(a, DBUS_NAME_BUS, "/org/freedesktop/DBus", DBUS_NAME_BUS, "RemoveMatch", "s", "type='signal',member='R1'", 0);
                db_drain(a);
        }
        db_call(a, DBUS_NAME_BUS, "/org/freedesktop/DBus", DBUS_NAME_BUS, "GetNameOwner", "s", ":1.02", 0);
        DBCHECK(db_find(a, &s, DBUS_ERROR, 0), "a unique name with a leading zero is no client's");
        db_call(a, DBUS_NAME_BUS, "/org/freedesktop/DBus", DBUS_NAME_BUS, "GetNameOwner", "s", db_unique(b), 0);
        DBCHECK(db_find(a, &s, DBUS_RETURN, 0) && db_string_arg(&s, 0, text, sizeof text) && !strcmp(text, db_unique(b)), "and a client's own is its");
        {
                // a match rule longer than the 288 bytes once read, well under the limit
                char rule[600], part[111];

                memset(part, 'a', 110);
                part[110] = 0;
                snprintf(rule, sizeof rule, "type='signal',sender='org.example.%s',interface='org.example.%s',member='M%.60s'", part, part, part);
                db_rule(a, rule);
                DBCHECK(strlen(rule) > 300 && db_find(a, &s, DBUS_RETURN, 0), "a match rule of %zu bytes is taken", strlen(rule));
        }
        {
                // argN matches a string, argNpath a string or an object path
                struct db_call c;
                struct dbus_fields f = {"/a", "org.example.Args", "M", "", "", "", "o", 0, 0};

                db_rule(b, "type='signal',interface='org.example.Args',arg0='/a/b'");
                db_drain(b);
                db_begin(&c, DBUS_SIGNAL, 0, &f);
                dbus_o_string(&c.w, "/a/b");
                db_send(a, &c);
                DBCHECK(!db_find(b, &s, DBUS_SIGNAL, "M"), "arg0 does not match an object path, which is not a string");
                db_rule(b, "type='signal',interface='org.example.Args',arg0path='/a/b'");
                db_drain(b);
                db_begin(&c, DBUS_SIGNAL, 0, &f);
                dbus_o_string(&c.w, "/a/b");
                db_send(a, &c);
                DBCHECK(db_find(b, &s, DBUS_SIGNAL, "M"), "and arg0path does");
        }
        {
                // a bowl's user is every program of every bowl: more connections than one program needs
                long many[DBUS_PER_UID], made = 0;

                for (long k = 0; k < 48; k++)
                        made += (many[k] = db_connect(1000)) >= 0;
                DBCHECK(made == 48, "one user has 48 connections and more (%ld)", made);
                for (long k = 0; k < 48; k++)
                        if (many[k] >= 0)
                                dbus_drop(many[k]);
        }
        dbus_drop(a);
        dbus_drop(b);
        DBCHECK(db_allocs == db_releases, "every record is returned (%ld %ld)", db_allocs, db_releases);
}

/*
        One uid's clients, each owning all the names it may, leave room for another's (found by the sec-apps review: the
        table held 256 names for 32 clients of 8 each, and after the clients went to 128 one uid's 96 could own all of
        them, and RequestName was then LimitsExceeded for everyone else). The names are found by their hash: every one is
        found again, released and taken again in its bucket.
*/
static void db_test_names_room(void)
{
        long many[DBUS_PER_UID], other, made = 0, owned = 0, found = 0;
        struct db_seen s;
        char name[48];

        db_reset();
        for (long k = 0; k < DBUS_PER_UID; k++)
        {
                many[k] = db_connect(1234);
                if (many[k] < 0)
                        continue;
                made++;
                db_hello(many[k]);
                for (int n = 0; n < DBUS_NAMES_PER; n++)
                {
                        snprintf(name, sizeof name, "org.example.U%ld.N%d", k, n);
                        db_request(many[k], name, 0);
                }
                while (db_find(many[k], &s, DBUS_RETURN, 0))
                        owned += db_u32_body(&s) == 1;
                db_drain(many[k]);
        }
        DBCHECK(made == DBUS_PER_UID && owned == (long)DBUS_PER_UID * DBUS_NAMES_PER, "one uid's %ld clients own %ld names, all they may", made, owned);
        other = db_connect(1000);
        db_hello(other);
        db_drain(other);
        db_request(other, "org.example.Mine", 0);
        DBCHECK(db_find(other, &s, DBUS_RETURN, 0) && db_u32_body(&s) == 1, "and another uid's client still owns a name of its own");
        for (long k = 0; k < DBUS_PER_UID; k += 7)
        {
                snprintf(name, sizeof name, "org.example.U%ld.N%d", k, (int)(k % DBUS_NAMES_PER));
                found += dbus_name_find(name) && dbus_name_find(name)->owner == many[k];
        }
        DBCHECK(found == (DBUS_PER_UID + 6) / 7, "each name is found again by its hash, owned by its client (%ld)", found);
        db_call(many[3], DBUS_NAME_BUS, "/org/freedesktop/DBus", DBUS_NAME_BUS, "ReleaseName", "s", "org.example.U3.N3", 0);
        db_drain(many[3]);
        DBCHECK(!dbus_name_find("org.example.U3.N3") && dbus_name_find("org.example.U3.N2"), "a name released is gone from its bucket, and no other with it");
        db_request(other, "org.example.U3.N3", 0);
        DBCHECK(db_find(other, &s, DBUS_RETURN, 0) && db_u32_body(&s) == 1 && dbus_name_find("org.example.U3.N3")->owner == other,
                "and is taken again by another");
        for (long k = 0; k < DBUS_PER_UID; k++)
                if (many[k] >= 0)
                        dbus_drop(many[k]);
        DBCHECK(dbus_name_find("org.example.Mine") && !dbus_name_find("org.example.U5.N1"), "the names of the clients gone are gone, the others' kept");
        dbus_drop(other);
        DBCHECK(db_allocs == db_releases, "every record is returned (%ld %ld)", db_allocs, db_releases);
}

static void db_read_all(long slot, const char *const *patterns, int count)
{
        struct db_call call;
        struct dbus_fields f = {DBUS_PATH_PORTAL, "org.freedesktop.portal.Settings", "ReadAll", "", DBUS_NAME_PORTAL, "", "as", 0, 0};
        long length_at;

        db_begin(&call, DBUS_CALL, 0, &f);
        dbus_o_u32(&call.w, 0);
        length_at = call.w.length - 4;
        for (int k = 0; k < count; k++)
                dbus_o_string(&call.w, patterns[k]);
        dbus_o_patch32(&call.w, length_at, (unsigned long)(call.w.length - length_at - 4));
        db_send(slot, &call);
}

static int db_holds(const struct db_seen *s, const char *text)
{
        long n = (long)strlen(text) + 1;

        for (long at = 0; at + n <= s->size; at++)
                if (!memcmp(s->bytes + at, text, (size_t)n))
                        return 1;
        return 0;
}

// ReadAll's patterns, as xdg-desktop-portal reads them.
static void db_test_read_all(void)
{
        long a;
        struct db_seen s;
        static const char *const nine[] = {"a.b", "c.d", "e.f", "g.h", "i.j", "k.l", "m.n", "o.p", "org.freedesktop.appearance"};
        char long_pattern[121];
        const char *one[1];

        db_reset();
        dbus_setting_set("org.freedesktop.appearance", "color-scheme", 'u', 1, 0, 0);
        dbus_setting_set("org.gnome.desktop.interface", "gtk-theme", 's', 0, 0, "Adwaita");
        dbus_setting_set("org.gnomeish", "other", 'u', 3, 0, 0);
        a = db_connect(1000);
        db_hello(a);
        db_drain(a);
        db_read_all(a, nine, 9);
        DBCHECK(db_find(a, &s, DBUS_RETURN, 0) && db_holds(&s, "org.freedesktop.appearance") && !db_holds(&s, "org.gnome.desktop.interface"),
                "the ninth of nine patterns is read like the first");
        memset(long_pattern, 'x', 120);
        long_pattern[120] = 0;
        one[0] = long_pattern;
        db_read_all(a, one, 1);
        DBCHECK(db_find(a, &s, DBUS_RETURN, 0) && !db_holds(&s, "org.freedesktop.appearance") && !db_holds(&s, "org.gnome.desktop.interface"),
                "a pattern of 120 bytes that names nothing reads nothing, not everything");
        one[0] = "org.gnome*";
        db_read_all(a, one, 1);
        DBCHECK(db_find(a, &s, DBUS_RETURN, 0) && db_holds(&s, "org.gnome.desktop.interface") && db_holds(&s, "org.gnomeish"),
                "org.gnome* is every namespace that starts org.gnome");
        one[0] = "org.gnome.*";
        db_read_all(a, one, 1);
        DBCHECK(db_find(a, &s, DBUS_RETURN, 0) && db_holds(&s, "org.gnome.desktop.interface") && !db_holds(&s, "org.gnomeish"),
                "and org.gnome.* every one that starts org.gnome. (not org.gnomeish)");
        one[0] = "";
        db_read_all(a, one, 1);
        DBCHECK(db_find(a, &s, DBUS_RETURN, 0) && db_holds(&s, "org.gnome.desktop.interface") && db_holds(&s, "org.freedesktop.appearance"),
                "an empty pattern is every namespace");
        dbus_drop(a);
}

static char db_traced[1024];

static void db_trace_take(void *context, const char *line)
{
        (void)context;
        snprintf(db_traced, sizeof db_traced, "%s", line);
}

// The trace (sessionbus -t) puts what clients wrote on standard error, which is a terminal: no byte of theirs a terminal acts on.
static void db_test_trace(void)
{
        static const struct dbus_host traced = {0, db_alloc, db_release, db_now, db_random, db_trace_take, 0, 0, 0};
        long a;
        int clean = 1;

        for (long slot = 0; slot < DBUS_CLIENTS; slot++)
                dbus_drop(slot);
        dbus_stop();
        dbus_start(&traced);
        a = db_connect(1000);
        db_hello(a);
        db_drain(a);
        db_call(a, DBUS_NAME_BUS, "/org/freedesktop/DBus", DBUS_NAME_BUS, "GetNameOwner", "s", "\033]0;owned\007\033[2J\r\n", 0);
        for (long k = 0; db_traced[k]; k++)
                clean &= (unsigned char)db_traced[k] >= 32 && db_traced[k] != 127;
        DBCHECK(db_traced[0] && clean, "a client's escape and control bytes are not traced as they are: %s", db_traced);
        dbus_drop(a);
        db_reset();
}

/* ---- noise ---- */

static unsigned long db_seed = 88172645463325252UL;

static unsigned long db_random_next(void)
{
        db_seed ^= db_seed << 13;
        db_seed ^= db_seed >> 7;
        db_seed ^= db_seed << 17;
        return db_seed;
}

// Random bytes, random cuts of valid messages, valid messages with a byte changed: nothing ends the bus, nothing is left.
static void db_test_fuzz(long rounds)
{
        long a, b, accepted = 0, ended = 0;
        struct db_call good;
        struct dbus_fields f = {DBUS_PATH_PORTAL, "org.freedesktop.portal.Settings", "ReadOne", "", DBUS_NAME_PORTAL, "", "ss", 0, 0};

        db_reset();
        dbus_setting_set("org.freedesktop.appearance", "color-scheme", 'u', 1, 0, 0);
        db_begin(&good, DBUS_CALL, 0, &f);
        dbus_o_string(&good.w, "org.freedesktop.appearance");
        dbus_o_string(&good.w, "color-scheme");
        db_end(&good);
        a = db_connect(1000);
        db_hello(a);
        db_drain(a);
        for (long round = 0; round < rounds; round++)
        {
                unsigned char noise[600];
                long size = good.w.length;

                if (dbus_dead(a))
                {
                        ended++;
                        dbus_drop(a);
                        a = db_connect(1000);
                        db_hello(a);
                        db_drain(a);
                }
                b = db_random_next() % 4;
                if (b == 0)
                {
                        long n = (long)(db_random_next() % sizeof noise);

                        for (long k = 0; k < n; k++)
                                noise[k] = (unsigned char)db_random_next();
                        db_feed(a, noise, n);
                }
                else
                {
                        memcpy(noise, good.bytes, (size_t)size);
                        for (long k = 0, changes = 1 + (long)(db_random_next() % 3); k < changes; k++)
                                noise[db_random_next() % (unsigned long)size] = (unsigned char)db_random_next();
                        db_feed(a, noise, b == 1 ? size : (long)(db_random_next() % (unsigned long)size));
                        if (b == 3 && !dbus_dead(a))
                                db_feed(a, good.bytes, size);
                }
                accepted += !dbus_dead(a);
                db_drain(a);
        }
        dbus_drop(a);
        DBCHECK(db_allocs == db_releases, "every record is returned after the noise (%ld %ld)", db_allocs, db_releases);
        for (long k = 0; k < DBUS_PENDING; k++)
                DBCHECK(!dbus.pending[k].used, "no call waits for a reply after the noise");
        printf("CHECK_dbus fuzz: %ld rounds, %ld left the connection well, %ld ended it\n", rounds, accepted, ended);
}

/*
        A fuzz that knows the grammar. Messages are made from it: a signature of random types (arrays, dictionaries,
        structs and variants nested, every basic type) and a body of random values of them, random header fields, from a
        random client to the bus, the portal, a client by its unique or a well-known name, or to nobody; then, a third of
        the time, changed where a parser is weakest (a length, a count, a byte of a signature or of a pad, a cut). Among
        them are the bus's own calls with random arguments (names taken, let go and replaced; rules added and removed in
        the text AddMatch reads, quotes and escapes and lengths at their caps; the portal's reads with random namespaces
        and keys), replies and errors to the calls a client was sent, forged ones to calls it was not, signatures nested
        past the cap, clients that read and clients that do not, clients that go, settings that change, and the clock.
        Bytes go in pieces of random sizes. After every step, every queue is within its cap, every name and every pending
        call belongs to a client that is there, and the calls each client waits on are counted right; every message a
        client is given, when it reads, is one the parser takes whole; at the end every record is returned.
*/
#define DBZ_CLIENTS 6

static long dbz_slot[DBZ_CLIENTS];
static char dbz_unique[DBZ_CLIENTS][24];
static unsigned long dbz_asked[DBZ_CLIENTS][8];  // serials of calls a client was sent, which it may answer
static char dbz_asker[DBZ_CLIENTS][8][24];       // and who sent them
static long dbz_checked, dbz_messages_read, dbz_bad_out, dbz_mutated, dbz_whole_refused;

static unsigned long dbz_pick(unsigned long n)
{
        return n ? db_random_next() % n : 0;
}

static const char *const dbz_names[] = {"org.example.A",  "org.example.B",  "org.freedesktop.Notifications", DBUS_NAME_PORTAL, DBUS_NAME_BUS,
                                        "org.example.A.b-c", "a.b", ":1.2", ":1.3", ":1.4", ":1.1", ":1.999", "org.example.A.B.C.D"};
static const char *const dbz_paths[] = {"/", "/org/example", DBUS_PATH_PORTAL, "/org/freedesktop/DBus", "/a/b/c", "/org/freedesktop/portal"};
static const char *const dbz_ifaces[] = {"org.example.I", "org.freedesktop.portal.Settings", DBUS_NAME_BUS, "org.freedesktop.DBus.Properties",
                                         "org.freedesktop.DBus.Peer", "org.freedesktop.DBus.Introspectable", "org.freedesktop.host.portal.Registry",
                                         "org.freedesktop.portal.FileChooser"};
static const char *const dbz_members[] = {"Hello", "RequestName", "ReleaseName", "ListNames", "ListActivatableNames", "NameHasOwner", "GetNameOwner",
                                          "StartServiceByName", "AddMatch", "RemoveMatch", "GetConnectionUnixUser", "GetConnectionUnixProcessID",
                                          "GetConnectionCredentials", "GetId", "Ping", "GetMachineId", "Introspect", "ReadAll", "Read", "ReadOne", "Get",
                                          "GetAll", "Register", "M", "Changed", "SettingChanged", "NameOwnerChanged", "UpdateActivationEnvironment"};
static const char *const dbz_namespaces[] = {"org.freedesktop.appearance", "org.gnome.desktop.interface", "org.gnome.*", "org.*", "", "o.*", ".*", "org.freedesktop.appearance.*",
                                             "org.freedesktop.portal.Settings", "org.freedesktop.portal.FileChooser", "org.example"};
static const char *const dbz_keys[] = {"color-scheme", "accent-color", "contrast", "version", "gtk-theme", "cursor-size", "nothing", "", "k1"};

#define DBZ_COUNT(table) ((unsigned long)(sizeof(table) / sizeof((table)[0])))

static unsigned long dbz_edge32(void)
{
        static const unsigned long edges[] = {0, 1, 2, 3, 4, 7, 8, 255, 256, 0x7fffffffUL, 0x80000000UL, 0xffffffffUL, 0xfffffffeUL, 262144, 262145, 67108864, 67108865};

        return dbz_pick(3) ? db_random_next() & (dbz_pick(2) ? 0xffUL : 0xffffffffUL) : edges[dbz_pick(DBZ_COUNT(edges))];
}

// A random complete type at sig[*n], nested no more than depth containers deep.
static void dbz_type(char *sig, long *n, int depth)
{
        static const char basic[] = "ybnqiuxtdsogh";
        unsigned long k = dbz_pick(depth > 0 ? 19 : 13);

        if (k < 13)
                sig[(*n)++] = basic[k];
        else if (k < 15)
        {
                sig[(*n)++] = 'a';
                dbz_type(sig, n, depth - 1);
        }
        else if (k < 17)
                sig[(*n)++] = 'v';
        else if (k == 17)
        {
                sig[(*n)++] = 'a';
                sig[(*n)++] = '{';
                sig[(*n)++] = basic[dbz_pick(13)];
                dbz_type(sig, n, depth - 1);
                sig[(*n)++] = '}';
        }
        else
        {
                sig[(*n)++] = '(';
                for (long m = 1 + (long)dbz_pick(3); m > 0; m--)
                        dbz_type(sig, n, depth - 1);
                sig[(*n)++] = ')';
        }
}

// A random signature of up to four complete types, of at most 255 characters (a few times in a hundred, one nested far past the cap).
static void dbz_signature(char *sig, int depth)
{
        long n = 0;

        if (!dbz_pick(40))
        {
                // deep: arrays, structs or variants of one another past DBUS_DEPTH
                long deep = 20 + (long)dbz_pick(30), kind = (long)dbz_pick(3);

                // past the cap, no message to take: more than 32 arrays or structs in one signature, or (half each) a value past DBUS_DEPTH
                dbz_mutated |= kind == 2 ? deep > DBUS_DEPTH || (deep + 1) / 2 > 32 : deep > 32;

                for (long k = 0; k < deep; k++)
                        sig[n++] = kind == 0 ? 'a' : kind == 1 ? '(' : (k & 1) ? '(' : 'a';
                sig[n++] = 'y';
                for (long k = deep - 1; k >= 0; k--)
                        if (sig[k] == '(')
                                sig[n++] = ')';
                sig[n] = 0;
                return;
        }
        for (long types = (long)dbz_pick(5); types > 0; types--)
        {
                char one[600];
                long m = 0;

                dbz_type(one, &m, depth);
                if (n + m > 255)
                        break;
                memcpy(sig + n, one, (size_t)m);
                n += m;
        }
        sig[n] = 0;
}

// A random text of valid UTF-8 with no NUL: mostly ASCII, some letters of two, three and four bytes, now and then long.
static void dbz_text(char *into, long room)
{
        static const char *const pieces[] = {"a", "Z", "_", ".", "/", " ", "'", ",", "=", "\\", "*", "\xc3\xa5", "\xe2\x82\xac", "\xf0\x9f\x98\x80", "\x7f", "\x01"};
        long most = dbz_pick(8) ? 24 : room - 1, n = 0, length = (long)dbz_pick((unsigned long)most + 1);

        while (n < length)
        {
                const char *piece = pieces[dbz_pick(DBZ_COUNT(pieces))];
                long size = (long)strlen(piece);

                if (n + size > room - 1)
                        break;
                memcpy(into + n, piece, (size_t)size);
                n += size;
        }
        into[n] = 0;
}

static void dbz_value(struct dbus_out *w, const char *sig, long *s, int budget)
{
        char c = sig[(*s)++];

        switch (c)
        {
        case 'y':
                dbus_o_u8(w, (unsigned)db_random_next());
                break;
        case 'b':
                dbus_o_u32(w, dbz_pick(2));
                break;
        case 'n':
        case 'q':
        {
                unsigned char two[2] = {(unsigned char)db_random_next(), (unsigned char)db_random_next()};

                dbus_o_pad(w, 2);
                dbus_o_raw(w, two, 2);
                break;
        }
        case 'i':
        case 'u':
        case 'h':
                dbus_o_u32(w, dbz_edge32());
                break;
        case 'x':
        case 't':
        case 'd':
                dbus_o_u64(w, db_random_next());
                break;
        case 's':
        {
                char text[700];

                dbz_text(text, budget > 0 ? (long)sizeof text : 32);
                dbus_o_string(w, text);
                break;
        }
        case 'o':
                dbus_o_string(w, dbz_paths[dbz_pick(DBZ_COUNT(dbz_paths))]);
                break;
        case 'g':
        {
                char g[300];

                dbz_signature(g, 2);
                dbus_o_sig(w, g);
                break;
        }
        case 'v':
        {
                char g[300];
                long n = 0, k = 0;

                dbz_type(g, &n, budget > 0 ? 3 : 0);
                g[n] = 0;
                dbus_o_sig(w, g);
                dbz_value(w, g, &k, budget - 1);
                break;
        }
        case 'a':
        {
                long element = *s, after = *s - 1, length_at, first, count, previous = 0, made = 0;
                char e = sig[element];
                long align = e == '{' || e == '(' ? 8 : dbus_fixed_size(e) ? dbus_fixed_size(e) : e == 's' || e == 'o' || e == 'a' ? 4 : 1;

                dbus_sig_one(sig, (long)strlen(sig), &after, 0, 0, 0);
                dbus_o_u32(w, 0);
                length_at = w->length - 4;
                dbus_o_pad(w, align);
                first = w->length;
                count = budget > 0 ? (long)dbz_pick(dbz_pick(6) ? 4 : 200) : 0;
                for (long k = 0; k < count && !w->bad && w->length < 200000; k++)
                {
                        long each = element;

                        previous = w->length;
                        made++;
                        if (e == '{')
                        {
                                dbus_o_pad(w, 8);
                                each++;
                                dbz_value(w, sig, &each, budget - 1);
                                dbz_value(w, sig, &each, budget - 1);
                        }
                        else
                                dbz_value(w, sig, &each, budget - 1);
                }
                dbus_o_patch32(w, length_at, (unsigned long)(w->length - first));
                // an array GDBus reads short (see dbus_value): valid, and not taken by the bus, so no message to take
                dbz_mutated |= (e == '{' || e == '(') && first == length_at + 8 && made > 1 && previous >= w->length - 4;
                *s = after;
                break;
        }
        case '(':
                dbus_o_pad(w, 8);
                while (sig[*s] && sig[*s] != ')')
                        dbz_value(w, sig, s, budget - 1);
                (*s)++;
                break;
        default:
                break;
        }
}

// A rule in AddMatch's text: random keys, values quoted, escaped, bare, long and empty.
static void dbz_rule(char *rule, long room)
{
        static const char *const keys[] = {"type", "sender", "interface", "member", "path", "path_namespace", "destination", "arg0", "arg1", "arg3", "arg4",
                                           "arg0path", "arg2path", "arg0namespace", "arg1namespace", "eavesdrop", "bogus", "", "type"};
        static const char *const values[] = {"signal", "method_call", "error", "org.example.I", "org.freedesktop.DBus", ":1.2", "/", "/org/example",
                                             "/org/example/", "Changed", "NameOwnerChanged", "org.example", "true", "", "it\\'s", "a,b"};
        long n = 0;

        rule[0] = 0;
        for (long k = (long)dbz_pick(6); k > 0 && n < room - 200; k--)
        {
                const char *key = keys[dbz_pick(DBZ_COUNT(keys))];
                char value[400];
                long quote = (long)dbz_pick(3);

                if (dbz_pick(5))
                        snprintf(value, sizeof value, "%s", values[dbz_pick(DBZ_COUNT(values))]);
                else
                {
                        long length = dbz_pick(2) ? 95 + (long)dbz_pick(4) : 120 + (long)dbz_pick(50);

                        memset(value, dbz_pick(2) ? 'x' : '.', (size_t)length);
                        value[length] = 0;
                }
                n += snprintf(rule + n, (size_t)(room - n), "%s%s=%s%s%s", n ? "," : "", key, quote ? "'" : "", value, quote == 1 ? "'" : "");
        }
        if (n >= room)
                rule[room - 1] = 0;
}

// What a fuzz client is sent: a random message, or a call of the bus or the portal with random arguments; its length, or -1.
static long dbz_message(unsigned char *into, long room, long who)
{
        struct dbus_out w = {into, 0, room, 0};
        char sig[300], text[700], other[700], error[64];
        struct dbus_fields f = {"", "", "", "", "", "", "", 0, 0};
        unsigned type = DBUS_CALL, flags = dbz_pick(4) ? 0 : (unsigned)dbz_pick(4);
        long body, kind = (long)dbz_pick(10);

        dbz_mutated = 0;
        sig[0] = 0;
        if (kind < 4)
        {
                // a call of the bus's or the portal's, with arguments of the right signature (or near it)
                const char *member = dbz_members[dbz_pick(DBZ_COUNT(dbz_members))];

                f.path = dbz_paths[dbz_pick(DBZ_COUNT(dbz_paths))];
                f.interface = dbz_pick(4) ? dbz_ifaces[dbz_pick(DBZ_COUNT(dbz_ifaces))] : "";
                f.member = member;
                f.destination = dbz_pick(3) ? DBUS_NAME_BUS : dbz_pick(2) ? DBUS_NAME_PORTAL : dbz_names[dbz_pick(DBZ_COUNT(dbz_names))];
                if (!strcmp(member, "AddMatch") || !strcmp(member, "RemoveMatch"))
                {
                        dbz_rule(text, dbz_pick(4) ? 300 : sizeof text);
                        f.signature = "s";
                        dbus_o_header(&w, type, flags, ++db_serial, &f);
                        body = w.length;
                        dbus_o_string(&w, text);
                }
                else if (!strcmp(member, "RequestName") || !strcmp(member, "StartServiceByName"))
                {
                        if (dbz_pick(3))
                                snprintf(text, sizeof text, "%s", dbz_names[dbz_pick(DBZ_COUNT(dbz_names))]);
                        else
                                dbz_text(text, 300);
                        f.signature = "su";
                        dbus_o_header(&w, type, flags, ++db_serial, &f);
                        body = w.length;
                        dbus_o_string(&w, text);
                        dbus_o_u32(&w, dbz_pick(3) ? dbz_pick(8) : dbz_edge32());
                }
                else if (!strcmp(member, "ReadAll"))
                {
                        long length_at, first;

                        f.signature = "as";
                        dbus_o_header(&w, type, flags, ++db_serial, &f);
                        body = w.length;
                        dbus_o_u32(&w, 0);
                        length_at = w.length - 4;
                        first = w.length;
                        for (long k = (long)dbz_pick(dbz_pick(4) ? 3 : 20); k > 0; k--)
                        {
                                if (dbz_pick(4))
                                        dbus_o_string(&w, dbz_namespaces[dbz_pick(DBZ_COUNT(dbz_namespaces))]);
                                else
                                {
                                        dbz_text(other, 200);
                                        dbus_o_string(&w, other);
                                }
                        }
                        dbus_o_patch32(&w, length_at, (unsigned long)(w.length - first));
                }
                else if (!strcmp(member, "Read") || !strcmp(member, "ReadOne") || !strcmp(member, "Get"))
                {
                        f.signature = "ss";
                        dbus_o_header(&w, type, flags, ++db_serial, &f);
                        body = w.length;
                        if (dbz_pick(4))
                                dbus_o_string(&w, dbz_namespaces[dbz_pick(DBZ_COUNT(dbz_namespaces))]);
                        else
                        {
                                dbz_text(other, sizeof other);
                                dbus_o_string(&w, other);
                        }
                        if (dbz_pick(4))
                                dbus_o_string(&w, dbz_keys[dbz_pick(DBZ_COUNT(dbz_keys))]);
                        else
                        {
                                dbz_text(other, sizeof other);
                                dbus_o_string(&w, other);
                        }
                }
                else
                {
                        // the one string most of the bus's methods take: a name, or a text
                        if (dbz_pick(3))
                                snprintf(text, sizeof text, "%s", dbz_pick(2) ? dbz_names[dbz_pick(DBZ_COUNT(dbz_names))] : dbz_unique[dbz_pick(DBZ_CLIENTS)]);
                        else
                                dbz_text(text, sizeof text);
                        f.signature = dbz_pick(4) ? "s" : "";
                        dbus_o_header(&w, type, flags, ++db_serial, &f);
                        body = w.length;
                        if (f.signature[0])
                                dbus_o_string(&w, text);
                }
        }
        else
        {
                // a message of any kind, of a random signature and body, to anyone
                type = 1 + (unsigned)dbz_pick(4);
                dbz_signature(sig, 1 + (int)dbz_pick(4));
                f.signature = sig;
                if (type == DBUS_CALL || type == DBUS_SIGNAL || dbz_pick(4) == 0)
                {
                        f.path = dbz_paths[dbz_pick(DBZ_COUNT(dbz_paths))];
                        f.member = dbz_members[dbz_pick(DBZ_COUNT(dbz_members))];
                        f.interface = type == DBUS_SIGNAL || dbz_pick(2) ? dbz_ifaces[dbz_pick(DBZ_COUNT(dbz_ifaces))] : "";
                }
                if (type == DBUS_ERROR || !dbz_pick(8))
                {
                        snprintf(error, sizeof error, "org.example.Error.E%lu", dbz_pick(3));
                        f.error = error;
                }
                if (type == DBUS_RETURN || type == DBUS_ERROR)
                {
                        long slot = (long)dbz_pick(8);

                        // an answer to a call this client was sent (from whom it came), or a forged one
                        f.has_reply = 1;
                        f.reply = dbz_pick(4) ? dbz_asked[who][slot] : dbz_edge32();
                        dbz_mutated |= !f.reply; // a reply serial of 0 is none (libdbus ends a connection sent one): no message to take
                        f.destination = dbz_pick(4) ? dbz_asker[who][slot] : dbz_unique[dbz_pick(DBZ_CLIENTS)];
                }
                else
                        f.destination = dbz_pick(5) == 0 ? "" : dbz_pick(2) ? dbz_unique[dbz_pick(DBZ_CLIENTS)] : dbz_names[dbz_pick(DBZ_COUNT(dbz_names))];
                if (!dbz_pick(10))
                        f.sender = ":1.77"; // a sender the bus must not keep
                dbus_o_header(&w, type, flags, ++db_serial, &f);
                body = w.length;
                for (long s = 0; sig[s] && !w.bad;)
                        dbz_value(&w, sig, &s, 2 + (int)dbz_pick(3));
        }
        dbus_o_end(&w, body);
        if (w.bad)
                return -1;
        if (!dbz_pick(3))
        {
                // changed where a parser is weakest
                dbz_mutated = 1;
                for (long k = 1 + (long)dbz_pick(3); k > 0 && w.length > 0; k--)
                {
                        long how = (long)dbz_pick(6), at = (long)dbz_pick((unsigned long)w.length);

                        if (how == 0)
                                into[at] = (unsigned char)db_random_next();
                        else if (how == 1 && w.length >= 16)
                                dbus_o_patch32(&w, dbz_pick(2) ? 4 : 12, dbz_pick(2) ? dbz_edge32() : dbus_get32(into + (dbz_pick(2) ? 4 : 12)) + dbz_pick(9) - 4);
                        else if (how == 2 && w.length >= 8)
                                dbus_o_patch32(&w, at & ~3L, dbz_edge32());
                        else if (how == 3)
                                w.length = at;
                        else if (how == 4)
                                into[at] = dbz_pick(2) ? 0 : (unsigned char)"ybnqiuxtdsoghva({)}"[dbz_pick(19)];
                        else
                                into[at] ^= (unsigned char)(1u << dbz_pick(8));
                }
        }
        return w.length;
}

// Bytes to a client in pieces of random size.
static void dbz_feed(long slot, const unsigned char *bytes, long n)
{
        while (n > 0 && !dbus_dead(slot))
        {
                long take = dbz_pick(3) ? n : 1 + (long)dbz_pick((unsigned long)n);

                db_feed(slot, bytes, take);
                bytes += take;
                n -= take;
        }
}

// What a client is given, read whole: every message must be one the parser takes; calls it is sent are kept, to answer.
static void dbz_read(long k, long most)
{
        long slot = dbz_slot[k];
        const unsigned char *bytes;
        long n = dbus_out(slot, &bytes), at = 0;

        if (slot < 0)
                return;
        // A client still in the authentication is given its lines, which are text.
        if (!dbus.client[slot] || dbus.client[slot]->state != 2 || !dbus.client[slot]->hello)
        {
                db_drain(slot);
                return;
        }
        while (at + 16 <= n && (most < 0 || at < most))
        {
                struct dbus_message m;
                long total = DBUS_ALIGN(16 + (long)dbus_get32(bytes + at + 12), 8) + (long)dbus_get32(bytes + at + 4);

                if (total < 16 || at + total > n || dbus_parse(bytes + at, total, &m) != 0)
                {
                        if (getenv("DBDEBUG"))
                        {
                                fprintf(stderr, "unparsable in queue of slot %ld (state %ld hello %d dead %ld):", slot, dbus.client[slot]->state, dbus.client[slot]->hello,
                                        dbus.client[slot]->dead);
                                for (long b = at; b < n && b < at + 96; b++)
                                        fprintf(stderr, "%02x", bytes[b]);
                                fprintf(stderr, "\n");
                        }
                        dbz_bad_out++;
                        DBCHECK(0, "fuzz: a client was given a message the parser does not take, at %ld of %ld", at, n);
                        break;
                }
                dbz_messages_read++;
                if (m.type == DBUS_CALL && !(m.flags & DBUS_FLAG_NO_REPLY))
                {
                        long place = (long)dbz_pick(8);

                        dbz_asked[k][place] = m.serial;
                        snprintf(dbz_asker[k][place], sizeof dbz_asker[k][place], "%s", m.sender);
                }
                if (m.type == DBUS_RETURN && !dbz_unique[k][0] && dbus_equal(m.sender, DBUS_NAME_BUS) && dbus_equal(m.signature, "s"))
                        dbus_arg_string(&m, 0, dbz_unique[k], sizeof dbz_unique[k]);
                at += total;
        }
        if (at > n)
                at = n;
        dbus_sent(slot, at);
}

static void dbz_connect(long k)
{
        static const long uids[] = {1000, 1000, 1000, 0, 1001};
        long uid = uids[dbz_pick(DBZ_COUNT(uids))];

        dbz_unique[k][0] = 0;
        memset(dbz_asked[k], 0, sizeof dbz_asked[k]);
        memset(dbz_asker[k], 0, sizeof dbz_asker[k]);
        if (dbz_pick(10))
                dbz_slot[k] = db_connect(uid);
        else
        {
                // the authentication's other ways in, some of which are no way in
                static const char *const ways[] = {"\0AUTH ANONYMOUS\r\nBEGIN\r\n", "\0AUTH EXTERNAL\r\nDATA\r\nNEGOTIATE_UNIX_FD\r\nBEGIN\r\n",
                                                   "\0AUTH\r\nCANCEL\r\nAUTH ANONYMOUS 00\r\nERROR x\r\nBEGIN\r\n", "\0BEGIN\r\n", "\0DATA 31\r\nAUTH EXTERNAL 3x\r\n",
                                                   "\0AUTH EXTERNAL 313030\r\nDATA\r\nBEGIN\r\n"};
                const char *way = ways[dbz_pick(DBZ_COUNT(ways))];
                long length = 1 + (long)strlen(way + 1);

                dbz_slot[k] = dbus_connect(4000 + k, uid);
                if (dbz_slot[k] >= 0)
                        dbz_feed(dbz_slot[k], (const unsigned char *)way, length);
        }
        if (dbz_slot[k] >= 0 && !dbus_dead(dbz_slot[k]))
        {
                db_drain(dbz_slot[k]);
                if (dbz_pick(12))
                        db_hello(dbz_slot[k]);
                dbz_read(k, -1);
        }
}

// What must hold after every step.
static void dbz_invariants(void)
{
        long pending[DBUS_CLIENTS] = {0}, names[DBUS_CLIENTS] = {0};

        dbz_checked++;
        for (long at = 0; at < DBUS_PENDING; at++)
                if (dbus.pending[at].used)
                {
                        struct dbus_pending *p = &dbus.pending[at];

                        DBCHECK(p->caller >= 0 && p->caller < DBUS_CLIENTS && dbus.client[p->caller] && p->callee >= 0 && p->callee < DBUS_CLIENTS &&
                                    dbus.client[p->callee],
                                "fuzz: a pending call is between clients that are there (%d %d)", p->caller, p->callee);
                        if (p->caller >= 0 && p->caller < DBUS_CLIENTS)
                                pending[p->caller]++;
                }
        for (long at = 0; at < DBUS_NAMES; at++)
                if (dbus.name[at].used)
                {
                        short owner = dbus.name[at].owner;

                        DBCHECK(owner == DBUS_SERVICE || (owner >= 0 && owner < DBUS_CLIENTS && dbus.client[owner] && dbus.client[owner]->hello),
                                "fuzz: a name is owned by a client that is there and said Hello (%s, %d)", dbus.name[at].text, owner);
                        DBCHECK(dbus.name[at].text[0] && dbus_valid_name(dbus.name[at].text, dbus_length(dbus.name[at].text), 1), "fuzz: a name owned is a name");
                        if (owner >= 0 && owner < DBUS_CLIENTS)
                                names[owner]++;
                }
        for (long slot = 0; slot < DBUS_CLIENTS; slot++)
        {
                struct dbus_client *c = dbus.client[slot];

                if (!c)
                        continue;
                DBCHECK(c->out_length >= 0 && c->out_length <= DBUS_OUT && c->in_length >= 0 && c->in_length <= (long)sizeof c->in,
                        "fuzz: the queues are within their caps (%ld %ld)", c->out_length, c->in_length);
                DBCHECK(c->pending == pending[slot] && c->pending <= DBUS_PENDING_PER, "fuzz: the calls a client waits on are counted (%ld %ld)", c->pending,
                        pending[slot]);
                DBCHECK(c->names == names[slot] && c->names <= DBUS_NAMES_PER, "fuzz: the names a client owns are counted (%ld %ld)", c->names, names[slot]);
        }
}

static void db_test_structured(long rounds)
{
        static unsigned char message[DBUS_MESSAGE + 64];
        long fed = 0, dropped = 0, failures = db_failures;

        db_reset();
        dbz_checked = dbz_messages_read = dbz_bad_out = 0;
        dbus_setting_set("org.freedesktop.appearance", "color-scheme", 'u', 1, 0, 0);
        dbus_setting_set("org.gnome.desktop.interface", "gtk-theme", 's', 0, 0, "Adwaita");
        for (long k = 0; k < DBZ_CLIENTS; k++)
                dbz_connect(k);
        for (long round = 0; round < rounds && db_failures - failures < 20; round++)
        {
                long k = (long)dbz_pick(DBZ_CLIENTS), what = (long)dbz_pick(100);

                if (dbz_slot[k] < 0 || dbus_dead(dbz_slot[k]) || what < 2)
                {
                        // a client that went, or one that goes now: what it was given is read first, then it is gone
                        if (dbz_slot[k] >= 0)
                        {
                                dbz_read(k, -1);
                                dbus_drop(dbz_slot[k]);
                                dropped++;
                        }
                        dbz_connect(k);
                }
                else if (what < 70)
                {
                        long n = dbz_message(message, sizeof message, k);

                        if (n > 0)
                        {
                                long was = dbus.client[dbz_slot[k]] && dbus.client[dbz_slot[k]]->hello && !dbus.client[dbz_slot[k]]->in_length;

                                dbz_feed(dbz_slot[k], message, n);
                                fed++;
                                // a whole message, valid by the grammar, from a client that is well: the bus takes it
                                if (!dbz_mutated && was && dbus_dead(dbz_slot[k]) == 1)
                                {
                                        struct dbus_message m;

                                        dbz_whole_refused++;
                                        if (getenv("DBDEBUG") && dbz_whole_refused < 6)
                                        {
                                                fprintf(stderr, "refused (%s, parse %d):", dbus_why(dbz_slot[k]), dbus_parse(message, n, &m));
                                                for (long b = 0; b < n; b++)
                                                        fprintf(stderr, "%02x", message[b]);
                                                fprintf(stderr, "\n");
                                        }
                                }
                        }
                }
                else if (what < 90)
                        dbz_read((long)dbz_pick(DBZ_CLIENTS), dbz_pick(3) ? -1 : (long)dbz_pick(4096)); // some read part, and some never read
                else if (what < 93)
                {
                        static const char *const keys[] = {"color-scheme", "accent-color", "contrast", "k2"};
                        unsigned long colour[3] = {db_random_next(), db_random_next(), db_random_next()};
                        char text[80];

                        dbz_text(text, sizeof text);
                        dbus_setting_set(dbz_pick(2) ? "org.freedesktop.appearance" : "org.gnome.desktop.interface", keys[dbz_pick(4)], "uibdsc"[dbz_pick(6)],
                                         (long)db_random_next(), colour, text);
                }
                else if (what < 96)
                {
                        db_time += (long)dbz_pick(dbz_pick(4) ? 100 : 8000);
                        dbus_tick();
                }
                else
                {
                        // bytes that are no message at all
                        long n = (long)dbz_pick(64);

                        for (long b = 0; b < n; b++)
                                message[b] = (unsigned char)db_random_next();
                        dbz_feed(dbz_slot[k], message, n);
                }
                dbz_invariants();
        }
        for (long k = 0; k < DBZ_CLIENTS; k++)
                if (dbz_slot[k] >= 0)
                {
                        dbz_read(k, -1);
                        dbus_drop(dbz_slot[k]);
                        dbz_slot[k] = -1;
                }
        DBCHECK(!dbz_whole_refused, "every message the grammar makes whole is taken by the bus (%ld refused)", dbz_whole_refused);
        DBCHECK(db_allocs == db_releases, "every record is returned after the grammar's fuzz (%ld %ld)", db_allocs, db_releases);
        for (long k = 0; k < DBUS_PENDING; k++)
                DBCHECK(!dbus.pending[k].used, "no call waits for a reply after the grammar's fuzz");
        printf("CHECK_dbus grammar fuzz: %ld rounds, %ld messages fed, %ld read back whole, %ld clients gone, %ld whole ones refused, %ld invariant passes, "
               "seed %lu\n", rounds, fed, dbz_messages_read, dropped, dbz_whole_refused, dbz_checked, db_seed);
}

/* ---- a fuzz that knows the grammar ---- */

/*
        Random messages of every kind, built from random signatures and valid as libdbus has them (arrays, structs and
        dict entries to 32 each in a signature; a value no deeper than 64, dict entries counted; strings from every plane
        of UTF-8, control bytes and noncharacters included, never a NUL; paths; signatures; booleans of 0 and 1), sent by
        six clients that own names and hold match rules: calls to the bus's methods, to the portal's, to each other's
        names and to nobody, answers to the calls each was given, errors, and signals to all and to one. Half are sent as
        they are, and each of those must leave its sender connected. The other half have one thing changed: a length, a
        signature's byte, a padding byte, a byte of the fixed header, any byte, or a cut. After every message, what each
        client was given is one whole valid message after another, each passed on from a client is stamped with that
        client's name, every call that waits is counted once by its caller, and every name has an owner that said Hello.
        DBFUZZ_DUMP=FILE writes each message a client was given (four bytes of its length, then its bytes), for
        test/dbus_libraries.py to hand to libdbus and to GDBus, which must take every one.
*/
struct db_gen
{
        struct dbus_out w;
        long mark[512], marks; // where a length, a signature's byte or a padding byte is: what a mutation picks from
};

// The message made has what a library does not take or misreads (1 the Local interface, 2 the Local path, 3 a reply serial
// of 0, 4 an array GDBus reads short): it is a change, not a valid message.
static int db_gen_reserved;

static void db_mark(struct db_gen *g, long at)
{
        if (g->marks < 512)
                g->mark[g->marks++] = at;
}

static void db_gpad(struct db_gen *g, long align)
{
        while (g->w.length % align && !g->w.bad)
        {
                db_mark(g, g->w.length);
                dbus_o_u8(&g->w, 0);
        }
}

static const char db_basic[] = "ybnqiuxtdsogh";

// A complete type into sig at *n, within libdbus's counts; a value of it reaches no deeper than `left` more levels.
static void db_gen_type(char *sig, long *n, int arrays, int structs, int dicts, int left)
{
        unsigned long r = db_random_next() % 20;

        if (*n > 120 || left <= 0 || r < 9)
                sig[(*n)++] = db_basic[db_random_next() % 13];
        else if (r < 12 && arrays < 32)
        {
                sig[(*n)++] = 'a';
                db_gen_type(sig, n, arrays + 1, structs, dicts, left - 1);
        }
        else if (r < 15 && arrays < 32 && dicts < 32 && left >= 2)
        {
                sig[(*n)++] = 'a';
                sig[(*n)++] = '{';
                sig[(*n)++] = db_basic[db_random_next() % 13];
                db_gen_type(sig, n, arrays + 1, structs, dicts + 1, left - 2);
                sig[(*n)++] = '}';
        }
        else if (r < 18 && structs < 32)
        {
                long members = 1 + (long)(db_random_next() % 3);

                sig[(*n)++] = '(';
                for (long k = 0; k < members; k++)
                        db_gen_type(sig, n, arrays, structs + 1, dicts, left - 1);
                sig[(*n)++] = ')';
        }
        else
                sig[(*n)++] = 'v';
}

// Complete types into sig (up to `types` of them), 255 characters at most; its length.
static long db_gen_signature(char *sig, int types, int left)
{
        long n;

        do
        {
                n = 0;
                for (int k = 0, count = (int)(db_random_next() % (unsigned long)(types + 1)); k < count; k++)
                        db_gen_type(sig, &n, 0, 0, 0, left);
        } while (n > 255);
        sig[n] = 0;
        return n;
}

static long db_gen_text(char *into, long room)
{
        static const unsigned long edges[] = {0xfffe, 0xffff, 0xfdd0, 0x10ffff, 0x7f, 0x80, 0x7ff, 0x800, 0xe000, 0x10000};
        long n = 0;

        for (long want = (long)(db_random_next() % 24); want > 0 && n + 5 < room; want--)
        {
                unsigned long r = db_random_next() % 10, code;

                code = r < 5 ? 32 + db_random_next() % 95 : r < 6 ? 1 + db_random_next() % 31 : r < 7 ? 0x80 + db_random_next() % 0x780
                       : r < 8 ? 0x800 + db_random_next() % 0xf800 : r < 9 ? 0x10000 + db_random_next() % 0x100000 : edges[db_random_next() % 10];
                if (code >= 0xd800 && code < 0xe000)
                        code = 0xfffd;
                if (code < 0x80)
                        into[n++] = (char)code;
                else if (code < 0x800)
                        into[n++] = (char)(0xc0 | code >> 6), into[n++] = (char)(0x80 | (code & 63));
                else if (code < 0x10000)
                        into[n++] = (char)(0xe0 | code >> 12), into[n++] = (char)(0x80 | ((code >> 6) & 63)), into[n++] = (char)(0x80 | (code & 63));
                else
                        into[n++] = (char)(0xf0 | code >> 18), into[n++] = (char)(0x80 | ((code >> 12) & 63)), into[n++] = (char)(0x80 | ((code >> 6) & 63)),
                        into[n++] = (char)(0x80 | (code & 63));
        }
        into[n] = 0;
        return n;
}

static void db_gen_path(char *into)
{
        static const char letters[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_";
        long n = 0;
        int elements = (int)(db_random_next() % 4);

        if (!elements)
                into[n++] = '/';
        for (int e = 0; e < elements; e++)
        {
                into[n++] = '/';
                for (int k = 0, length = 1 + (int)(db_random_next() % 6); k < length; k++)
                        into[n++] = letters[db_random_next() % 63];
        }
        into[n] = 0;
}

static void db_gen_string(struct db_gen *g, const char *text)
{
        db_gpad(g, 4);
        db_mark(g, g->w.length);
        dbus_o_string(&g->w, text);
}

static void db_gen_value(struct db_gen *g, const char *sig, long *s, int left)
{
        char c = sig[(*s)++], text[128];

        switch (c)
        {
        case 'y':
                dbus_o_u8(&g->w, (unsigned)db_random_next());
                return;
        case 'b':
                db_gpad(g, 4);
                db_mark(g, g->w.length);
                dbus_o_u32(&g->w, db_random_next() & 1);
                return;
        case 'n':
        case 'q':
        {
                unsigned char two[2] = {(unsigned char)db_random_next(), (unsigned char)db_random_next()};

                db_gpad(g, 2);
                dbus_o_raw(&g->w, two, 2);
                return;
        }
        case 'i':
        case 'u':
        case 'h':
                db_gpad(g, 4);
                dbus_o_u32(&g->w, db_random_next() & 0xffffffffUL);
                return;
        case 'x':
        case 't':
        case 'd':
                db_gpad(g, 8);
                dbus_o_u64(&g->w, db_random_next());
                return;
        case 's':
                db_gen_text(text, sizeof text);
                db_gen_string(g, text);
                return;
        case 'o':
                db_gen_path(text);
                db_gen_string(g, text);
                return;
        case 'g':
        {
                char inner[512];

                db_gen_signature(inner, 2, 8);
                db_mark(g, g->w.length);
                dbus_o_sig(&g->w, inner);
                return;
        }
        case 'v':
        {
                char inner[512];
                long n = 0, t = 0;

                do
                {
                        n = 0;
                        db_gen_type(inner, &n, 0, 0, 0, left - 1);
                } while (n > 255);
                inner[n] = 0;
                db_mark(g, g->w.length);
                dbus_o_sig(&g->w, inner);
                db_gen_value(g, inner, &t, left - 1);
                return;
        }
        case 'a':
        {
                long element = *s, after = *s - 1, length_at, first, count = (long)(db_random_next() % 4), previous = 0, made = 0;
                char e = sig[element];

                db_gpad(g, 4);
                db_mark(g, g->w.length);
                dbus_o_u32(&g->w, 0);
                length_at = g->w.length - 4;
                db_gpad(g, e == '{' || e == '(' ? 8 : dbus_fixed_size(e) ? dbus_fixed_size(e) : e == 's' || e == 'o' || e == 'a' ? 4 : 1);
                first = g->w.length;
                dbus_sig_one(sig, (long)strlen(sig), &after, 0, 0, 0);
                for (long k = 0; k < count && g->w.length < 3000; k++)
                {
                        long each = element;

                        previous = g->w.length;
                        made++;
                        if (e == '{')
                        {
                                db_gpad(g, 8);
                                each++;
                                db_gen_value(g, sig, &each, left - 2);
                                db_gen_value(g, sig, &each, left - 2);
                        }
                        else
                                db_gen_value(g, sig, &each, left - 1);
                }
                dbus_o_patch32(&g->w, length_at, (unsigned long)(g->w.length - first));
                // GDBus takes such an array to end 4 bytes early (4 bytes of padding after its length, and the element before
                // its last ending in those 4): valid, but not taken by the bus, so the message is a change and not a valid one
                if ((e == '{' || e == '(') && first == length_at + 8 && made > 1 && previous >= g->w.length - 4)
                        db_gen_reserved = 4;
                *s = after;
                return;
        }
        case '(':
                db_gpad(g, 8);
                while (sig[*s] != ')')
                        db_gen_value(g, sig, s, left - 1);
                (*s)++;
                return;
        }
}

#define DB_PEERS 6

static struct
{
        long slot;
        char name[40];                  // the well-known name it owns, or ""
        unsigned long call[8];          // calls it was given and has not answered: their serials
        char caller[8][20];             // and who made them
        long calls;
        long checked;                   // the bytes at the front of its queue already looked at (a queue left unread)
} db_peer[DB_PEERS];

static FILE *db_dump;
static long db_fuzz_failures;

static void db_peer_up(int p)
{
        static const char *const rules[] = {
            "type='signal'", "type='signal',sender='org.freedesktop.DBus',member='NameOwnerChanged'",
            "type='signal',interface='org.example.I',arg0='a'", "type='signal',path_namespace='/a'", "type='signal',arg0path='/a/'",
            "type='signal',sender='org.freedesktop.portal.Desktop',interface='org.freedesktop.portal.Settings'",
            "type='signal',arg0namespace='org.example'", "type='method_call'", "type='signal',member='M',arg1='x'",
        };

        db_peer[p].slot = db_connect(1000);
        db_hello(db_peer[p].slot);
        db_peer[p].calls = 0;
        db_peer[p].checked = 0;
        db_peer[p].name[0] = 0;
        if (p % 2 == 0)
        {
                snprintf(db_peer[p].name, sizeof db_peer[p].name, "org.example.Peer%d", p);
                db_request(db_peer[p].slot, db_peer[p].name, (unsigned)(db_random_next() % 8));
        }
        for (int k = 0, count = (int)(db_random_next() % 3); k < count; k++)
                db_rule(db_peer[p].slot, rules[db_random_next() % (sizeof rules / sizeof rules[0])]);
}

// What a client was given since it last looked: each must be a whole valid message; those passed on from `sender` carry its name.
static void db_peer_take(int p, const char *sender, int keep)
{
        const unsigned char *bytes;
        long n = dbus_out(db_peer[p].slot, &bytes), at = db_peer[p].checked;

        while (at < n)
        {
                struct dbus_message m;
                long total = n - at < 16 ? -1 : DBUS_ALIGN(16 + (long)dbus_get32(bytes + at + 12), 8) + (long)dbus_get32(bytes + at + 4);

                if (total < 16 || total > n - at || total > DBUS_MESSAGE + 4096 ||
                    dbus_parse(bytes + at, total > DBUS_MESSAGE ? DBUS_MESSAGE : total, &m) != 0)
                {
                        // a message past DBUS_MESSAGE (the largest, stamped) is only framed here: the parser takes DBUS_MESSAGE
                        if (total > DBUS_MESSAGE && total <= n - at && total <= DBUS_MESSAGE + 4096)
                        {
                                at += total;
                                continue;
                        }
                        if (db_fuzz_failures++ < 5)
                                DBCHECK(0, "fuzz: the bus gave a client %ld bytes that are not a valid message (%ld of %ld)", total, at, n);
                        break;
                }
                if (db_dump)
                {
                        unsigned char size[4] = {(unsigned char)total, (unsigned char)(total >> 8), (unsigned char)(total >> 16), (unsigned char)(total >> 24)};

                        fwrite(size, 1, 4, db_dump);
                        fwrite(bytes + at, 1, (size_t)total, db_dump);
                }
                if (*m.sender && m.sender[0] == ':' && strcmp(m.sender, ":1.1") && sender && strcmp(m.sender, sender) && db_fuzz_failures++ < 5)
                        DBCHECK(0, "fuzz: a message passed on carries %s, not its sender %s", m.sender, sender);
                if (m.type == DBUS_CALL && !(m.flags & DBUS_FLAG_NO_REPLY) && db_peer[p].calls < 8 && *m.sender)
                {
                        db_peer[p].call[db_peer[p].calls] = m.serial;
                        snprintf(db_peer[p].caller[db_peer[p].calls++], 20, "%s", m.sender);
                }
                at += total;
        }
        db_peer[p].checked = at;
        if (!keep)
        {
                dbus_sent(db_peer[p].slot, n);
                db_peer[p].checked = 0;
        }
}

static void db_fuzz_books(void)
{
        for (long slot = 0; slot < DBUS_CLIENTS; slot++)
        {
                struct dbus_client *c = dbus.client[slot];
                long waiting = 0;

                if (!c)
                        continue;
                for (long k = 0; k < DBUS_PENDING; k++)
                        waiting += dbus.pending[k].used && dbus.pending[k].caller == slot;
                if (waiting != c->pending && db_fuzz_failures++ < 5)
                        DBCHECK(0, "fuzz: client %ld counts %ld calls waiting, the table %ld", slot, c->pending, waiting);
                if ((c->out_head > c->out_length || c->out_length > DBUS_OUT) && db_fuzz_failures++ < 5)
                        DBCHECK(0, "fuzz: client %ld's queue is [%ld, %ld)", slot, c->out_head, c->out_length);
                {
                        long top = 0;

                        for (long k = 0; k < DBUS_RULES; k++)
                                if (c->rule[k].used)
                                        top = k + 1;
                        if (c->rules != top && db_fuzz_failures++ < 5)
                                DBCHECK(0, "fuzz: client %ld looks through %ld rules, its last is %ld", slot, c->rules, top);
                }
        }
        for (long k = 0; k < DBUS_NAMES; k++)
                if (dbus.name[k].used && dbus.name[k].owner != DBUS_SERVICE &&
                    (!dbus_client_of(dbus.name[k].owner) || !dbus_client_of(dbus.name[k].owner)->hello) && db_fuzz_failures++ < 5)
                        DBCHECK(0, "fuzz: %s is owned by slot %d, which has no client", dbus.name[k].text, dbus.name[k].owner);
}

// One random message from peer p into g: valid, as libdbus has it.
static void db_gen_message(struct db_gen *g, int p)
{
        static const char *const interfaces[] = {"org.example.I", DBUS_NAME_BUS, "org.freedesktop.portal.Settings", "org.freedesktop.DBus.Properties",
                                                 "org.freedesktop.DBus.Peer", "org.freedesktop.DBus.Introspectable", "org.freedesktop.portal.FileChooser"};
        static const char *const members[] = {"M", "Hello", "RequestName", "ReleaseName", "ListNames", "ListActivatableNames", "NameHasOwner",
                                              "GetNameOwner", "GetConnectionUnixUser", "GetConnectionUnixProcessID", "GetConnectionCredentials",
                                              "StartServiceByName", "AddMatch", "RemoveMatch", "GetId", "Read", "ReadOne", "ReadAll", "Get", "GetAll",
                                              "Ping", "GetMachineId", "Introspect", "Register", "OpenFile", "Changed"};
        static const char *const typed[] = {"s", "su", "ss", "as", "", "sa{sv}", "u", "v"};
        unsigned long r = db_random_next() % 20;
        unsigned type = r < 9 ? DBUS_CALL : r < 14 ? DBUS_SIGNAL : r < 18 ? DBUS_RETURN : DBUS_ERROR;
        int other = (int)(db_random_next() % DB_PEERS), answer = -1;
        char sig[512], path[64], text[128];
        const char *destination = "";
        long body_at, sig_field = -1;

        if ((type == DBUS_RETURN || type == DBUS_ERROR) && db_peer[p].calls && db_random_next() % 4)
                answer = (int)(db_random_next() % (unsigned long)db_peer[p].calls);
        if (type == DBUS_CALL)
        {
                unsigned long d = db_random_next() % 10;

                destination = d < 3 ? DBUS_NAME_BUS : d < 6 ? DBUS_NAME_PORTAL : d < 7 && db_peer[other].name[0] ? db_peer[other].name
                              : d < 9 ? db_unique(db_peer[other].slot) : d < 10 ? "org.example.Nobody" : "";
        }
        else if (type == DBUS_SIGNAL && db_random_next() % 3 == 0)
                destination = db_unique(db_peer[other].slot);
        else if (answer >= 0)
                destination = db_peer[p].caller[answer];
        else if (type != DBUS_SIGNAL)
                destination = db_unique(db_peer[other].slot);
        db_gen_reserved = (int)(db_random_next() % 48);
        db_gen_reserved = db_gen_reserved < 3 ? db_gen_reserved + 1 : 0; // 1 the Local interface, 2 the Local path, 3 a reply serial of 0
        g->w = (struct dbus_out){g->w.at, 0, 4096, 0};
        g->marks = 0;
        dbus_o_u8(&g->w, 'l');
        dbus_o_u8(&g->w, type);
        dbus_o_u8(&g->w, (unsigned)(db_random_next() % 8) & (type == DBUS_CALL ? 7u : 1u));
        dbus_o_u8(&g->w, 1);
        db_mark(g, 4);
        dbus_o_u32(&g->w, 0);
        dbus_o_u32(&g->w, ++db_serial);
        db_mark(g, 12);
        dbus_o_u32(&g->w, 0);
        if (type == DBUS_CALL || type == DBUS_SIGNAL)
        {
                db_gpad(g, 8);
                dbus_o_u8(&g->w, 1);
                db_mark(g, g->w.length);
                dbus_o_sig(&g->w, "o");
                if (db_gen_reserved == 2)
                        snprintf(path, sizeof path, "/org/freedesktop/DBus/Local");
                else if (db_random_next() % 3)
                        snprintf(path, sizeof path, "%s", db_random_next() % 2 ? DBUS_PATH_PORTAL : "/org/freedesktop/DBus");
                else
                        db_gen_path(path);
                db_gen_string(g, path);
        }
        if (type == DBUS_SIGNAL || (type == DBUS_CALL && (db_random_next() % 4 || db_gen_reserved == 1)))
        {
                db_gpad(g, 8);
                dbus_o_u8(&g->w, 2);
                dbus_o_sig(&g->w, "s");
                db_gen_string(g, db_gen_reserved == 1 ? "org.freedesktop.DBus.Local" : interfaces[db_random_next() % (sizeof interfaces / sizeof interfaces[0])]);
        }
        if ((type == DBUS_CALL || type == DBUS_SIGNAL) && db_gen_reserved == 3)
        {
                db_gpad(g, 8);
                dbus_o_u8(&g->w, 5);
                dbus_o_sig(&g->w, "u");
                dbus_o_u32(&g->w, 0);
        }
        if (type == DBUS_CALL || type == DBUS_SIGNAL)
        {
                db_gpad(g, 8);
                dbus_o_u8(&g->w, 3);
                dbus_o_sig(&g->w, "s");
                db_gen_string(g, members[db_random_next() % (sizeof members / sizeof members[0])]);
        }
        if (type == DBUS_ERROR)
        {
                db_gpad(g, 8);
                dbus_o_u8(&g->w, 4);
                dbus_o_sig(&g->w, "s");
                db_gen_string(g, "org.example.Error.Failed");
        }
        if (type == DBUS_RETURN || type == DBUS_ERROR)
        {
                db_gpad(g, 8);
                dbus_o_u8(&g->w, 5);
                dbus_o_sig(&g->w, "u");
                db_mark(g, g->w.length);
                dbus_o_u32(&g->w, db_gen_reserved == 3 ? 0 : answer >= 0 ? db_peer[p].call[answer] : 1 + db_random_next() % 64);
        }
        if (destination[0])
        {
                db_gpad(g, 8);
                dbus_o_u8(&g->w, 6);
                dbus_o_sig(&g->w, "s");
                db_gen_string(g, destination);
        }
        if (db_random_next() % 8 == 0)
        {
                // a field the bus does not know: it is to be taken, and not passed on
                char inner[512];
                long n = 0, t = 0;

                db_gpad(g, 8);
                dbus_o_u8(&g->w, 10 + (unsigned)(db_random_next() % 246));
                do
                {
                        n = 0;
                        db_gen_type(inner, &n, 0, 0, 0, 3);
                } while (n > 255);
                inner[n] = 0;
                dbus_o_sig(&g->w, inner);
                db_gen_value(g, inner, &t, 3);
        }
        if (db_random_next() % 3 == 0)
                snprintf(sig, sizeof sig, "%s", typed[db_random_next() % (sizeof typed / sizeof typed[0])]);
        else
                db_gen_signature(sig, 4, 64);
        if (sig[0])
        {
                db_gpad(g, 8);
                dbus_o_u8(&g->w, 8);
                dbus_o_sig(&g->w, "g");
                sig_field = g->w.length;
                dbus_o_sig(&g->w, sig);
                for (long k = 0; sig[k]; k++)
                        db_mark(g, sig_field + 1 + k);
        }
        dbus_o_patch32(&g->w, 12, (unsigned long)(g->w.length - 16));
        db_gpad(g, 8);
        body_at = g->w.length;
        for (long s = 0, n = (long)strlen(sig); s < n && !g->w.bad;)
        {
                if (sig[s] == 's' && db_random_next() % 2)
                {
                        // strings a method looks for: names, rules, namespaces
                        static const char *const words[] = {"org.example.Peer0", "org.example.Peer2", "org.freedesktop.appearance", "color-scheme",
                                                            "org.freedesktop.portal.Settings", "version", "type='signal'", ":1.2", "org.gnome.*", ""};

                        snprintf(text, sizeof text, "%s", words[db_random_next() % 10]);
                        db_gen_string(g, text);
                        s++;
                }
                else
                        db_gen_value(g, sig, &s, 64);
        }
        dbus_o_patch32(&g->w, 4, (unsigned long)(g->w.length - body_at));
        if (answer >= 0)
        {
                // answered once: the call leaves the peer's list
                db_peer[p].call[answer] = db_peer[p].call[db_peer[p].calls - 1];
                memcpy(db_peer[p].caller[answer], db_peer[p].caller[db_peer[p].calls - 1], 20);
                db_peer[p].calls--;
        }
}

static void db_test_grammar_fuzz(long rounds)
{
        static unsigned char bytes[4096 + 64];
        struct db_gen g;
        long valid = 0, valid_kept = 0, mutated = 0, mutated_ended = 0;
        const char *dump = getenv("DBFUZZ_DUMP");

        db_reset();
        db_fuzz_failures = 0;
        db_dump = dump ? fopen(dump, "wb") : 0;
        dbus_setting_set("org.freedesktop.appearance", "color-scheme", 'u', 1, 0, 0);
        dbus_setting_set("org.gnome.desktop.interface", "gtk-theme", 's', 0, 0, "Adwaita-dark");
        g.w.at = bytes;
        for (int p = 0; p < DB_PEERS; p++)
                db_peer_up(p);
        for (int p = 0; p < DB_PEERS; p++)
                db_peer_take(p, 0, 0);
        for (long round = 0; round < rounds; round++)
        {
                int p = (int)(db_random_next() % DB_PEERS), mutate = db_random_next() % 2 == 0, change;
                long size;
                char sender[20];

                db_gen_message(&g, p);
                if (g.w.bad)
                        continue;
                // what no library takes is sent as it was made as often as with a change, so that a bus that passes it on is seen
                change = mutate | db_gen_reserved;
                size = g.w.length;
                snprintf(sender, sizeof sender, "%s", db_unique(db_peer[p].slot));
                if (mutate)
                {
                        unsigned long how = db_random_next() % 8;
                        long at = g.marks ? g.mark[db_random_next() % (unsigned long)g.marks] : 0;

                        if (how < 3 && g.marks)
                                bytes[at] = (unsigned char)(how == 0 ? bytes[at] + 1u : how == 1 ? bytes[at] - 1u : (unsigned)db_random_next());
                        else if (how == 3)
                                bytes[db_random_next() % 16] = (unsigned char)db_random_next();
                        else if (how == 4)
                                bytes[db_random_next() % (unsigned long)size] ^= (unsigned char)(1u << (db_random_next() % 8));
                        else if (how == 5)
                                size = (long)(db_random_next() % (unsigned long)size);
                        else if (how == 6 && g.marks)
                                bytes[at + 1 < size ? at + 1 : at] = (unsigned char)db_random_next();
                        else
                                bytes[db_random_next() % (unsigned long)size] = (unsigned char)db_random_next();
                }
                if (db_random_next() % 4 == 0 && size > 1)
                {
                        // in two reads, cut anywhere
                        long cut = 1 + (long)(db_random_next() % (unsigned long)(size - 1));

                        db_feed(db_peer[p].slot, bytes, cut);
                        db_feed(db_peer[p].slot, bytes + cut, size - cut);
                }
                else
                        db_feed(db_peer[p].slot, bytes, size);
                if (!change)
                {
                        valid++;
                        valid_kept += !dbus_dead(db_peer[p].slot);
                        if (dbus_dead(db_peer[p].slot) && db_fuzz_failures++ < 5)
                        {
                                DBCHECK(0, "fuzz: a valid message of %ld bytes ended its sender (%s)", size, dbus_why(db_peer[p].slot));
                                for (long k = 0; k < size && getenv("DBDEBUG"); k++)
                                        fprintf(stderr, "%s%02x", k % 16 ? " " : "\n", bytes[k]);
                        }
                }
                else
                {
                        mutated++;
                        mutated_ended += dbus_dead(db_peer[p].slot) != 0;
                }
                for (int q = 0; q < DB_PEERS; q++)
                        db_peer_take(q, sender, db_random_next() % 64 == 0);
                if (round % 64 == 0)
                {
                        db_time += 1000;
                        dbus_tick();
                }
                db_fuzz_books();
                for (int q = 0; q < DB_PEERS; q++)
                        // a peer that was ended, or left with part of a message in (a cut, a length made longer), starts again
                        if (dbus_dead(db_peer[q].slot) || dbus.client[db_peer[q].slot]->in_length || (db_random_next() % 4096) == 0)
                        {
                                dbus_drop(db_peer[q].slot);
                                db_peer_up(q);
                                db_peer_take(q, 0, 0);
                        }
                if (db_random_next() % 512 == 0)
                        dbus_setting_set("org.freedesktop.appearance", "color-scheme", 'u', (long)(1 + db_random_next() % 2), 0, 0);
        }
        for (int p = 0; p < DB_PEERS; p++)
                dbus_drop(db_peer[p].slot);
        if (db_dump)
                fclose(db_dump);
        DBCHECK(valid == valid_kept, "fuzz: every valid message left its sender connected (%ld of %ld)", valid_kept, valid);
        DBCHECK(db_allocs == db_releases, "every record is returned after the grammar's fuzz (%ld %ld)", db_allocs, db_releases);
        for (long k = 0; k < DBUS_PENDING; k++)
                DBCHECK(!dbus.pending[k].used, "no call waits for a reply after the grammar's fuzz");
        printf("CHECK_dbus grammar fuzz: %ld rounds, %ld valid (%ld kept their sender), %ld changed (%ld ended theirs)\n", rounds, valid, valid_kept,
               mutated, mutated_ended);
}

/*
        The portal's requests (2026-11-15): a method that answers a request's path owes its caller the request's Response, and
        before this none was sent for CreateMonitor (GTK 4 in a sandbox waited on it for ever), and OpenFile, SaveFile and
        SaveFiles of the FileChooser the portal claims were UnknownMethod. Each now answers the specification's path and a
        Response(2, {}) the caller can read: at once when it has a rule for it, at its AddMatch when it subscribes after (as Qt
        does), at DBUS_RESPONSE_MS whatever it has, and to it alone.
*/
static void db_portal_call(long slot, const char *iface, const char *member, const char *token, unsigned flags)
{
        struct db_call c;
        int two = !strcmp(member, "CreateMonitor");
        struct dbus_fields f = {DBUS_PATH_PORTAL, iface, member, "", DBUS_NAME_PORTAL, "", two ? "sa{sv}" : "ssa{sv}", 0, 0};
        long length_at, first;

        db_begin(&c, DBUS_CALL, flags, &f);
        dbus_o_string(&c.w, "");
        if (!two)
                dbus_o_string(&c.w, "Open a file");
        dbus_o_u32(&c.w, 0);
        length_at = c.w.length - 4;
        dbus_o_pad(&c.w, 8);
        first = c.w.length;
        // an option before the token, of another type, so the token is found past it
        dbus_o_pad(&c.w, 8);
        dbus_o_string(&c.w, "modal");
        dbus_o_sig(&c.w, "b");
        dbus_o_u32(&c.w, 1);
        if (token)
        {
                dbus_o_pad(&c.w, 8);
                dbus_o_string(&c.w, "handle_token");
                dbus_o_sig(&c.w, "s");
                dbus_o_string(&c.w, token);
        }
        dbus_o_patch32(&c.w, length_at, (unsigned long)(c.w.length - first));
        db_send(slot, &c);
}

// The path the specification makes for a caller's request: the prefix, its unique name without the colon and with underscores, the token.
static void db_request_path(long slot, const char *token, char *into, long room)
{
        char sender[32];
        long n = 0;

        for (const char *u = db_unique(slot) + 1; *u; u++)
                sender[n++] = *u == '.' ? '_' : *u;
        sender[n] = 0;
        snprintf(into, (size_t)room, "/org/freedesktop/portal/desktop/request/%s/%s", sender, token);
}

// Whether s is the Response of path with code to the client slot from the portal: destination, sender, signature and body.
static int db_is_response(const struct db_seen *s, long slot, const char *path, unsigned long code)
{
        return s->ok && s->m.type == DBUS_SIGNAL && !strcmp(s->m.path, path) && !strcmp(s->m.interface, "org.freedesktop.portal.Request") &&
               !strcmp(s->m.member, "Response") && !strcmp(s->m.destination, db_unique(slot)) && !strcmp(s->m.sender, ":1.1") &&
               !strcmp(s->m.signature, "ua{sv}") && s->m.body_length == 8 && db_u32_body(s) == code && !dbus_get32(s->bytes + s->m.body_at + 4);
}

static void db_test_requests(void)
{
        long a, b;
        struct db_seen s;
        char path[200], rule[400], text[200];

        db_reset();
        a = db_connect(1000);
        b = db_connect(1000);
        db_hello(a);
        db_hello(b);
        db_drain(a);
        db_drain(b);
        db_rule(b, "type='signal'"); // b listens for every signal it may have: it must see nobody's Response
        db_drain(b);

        // subscribed first, as GDBus, libportal and Chromium do: the path back is the one subscribed to, and the Response follows at once
        db_request_path(a, "upload1", path, sizeof path);
        snprintf(rule, sizeof rule, "type='signal',sender='org.freedesktop.portal.Desktop',interface='org.freedesktop.portal.Request',member='Response',path='%s'", path);
        db_rule(a, rule);
        db_drain(a);
        db_portal_call(a, "org.freedesktop.portal.FileChooser", "OpenFile", "upload1", 0);
        db_next(a, &s);
        DBCHECK(s.ok && s.m.type == DBUS_RETURN && s.m.reply == db_last_serial() && !strcmp(s.m.signature, "o") && db_string_arg(&s, 0, text, sizeof text) &&
                    !strcmp(text, path),
                "FileChooser.OpenFile answers the path the specification makes of the caller's name and its handle_token: %s", s.ok ? text : "nothing");
        db_next(a, &s);
        DBCHECK(db_is_response(&s, a, path, 2), "and the request's Response(2, {}) follows it at once, to the caller, from the portal, with no tick and no time gone");
        DBCHECK(!db_queued(b), "a client listening for every signal is sent no other's Response (%ld bytes queued)", db_queued(b));

        // subscribed after the answer, as Qt does: held until its AddMatch, then sent after the AddMatch's own answer
        db_request_path(a, "save1", path, sizeof path);
        db_portal_call(a, "org.freedesktop.portal.FileChooser", "SaveFile", "save1", 0);
        db_next(a, &s);
        DBCHECK(s.ok && s.m.type == DBUS_RETURN && db_string_arg(&s, 0, text, sizeof text) && !strcmp(text, path), "SaveFile answers its path");
        DBCHECK(!db_queued(a), "and a caller with no rule for its Response is not sent it yet, where it would not read it (%ld bytes)", db_queued(a));
        DBCHECK(dbus_wait(60000) <= DBUS_RESPONSE_MS && dbus_wait(60000) > 0, "the bus asks to be woken by the time the Response is due (%ld ms)", dbus_wait(60000));
        snprintf(rule, sizeof rule, "type='signal',interface='org.freedesktop.portal.Request',member='Response',path='%s'", path);
        db_rule(a, rule);
        db_next(a, &s);
        DBCHECK(s.ok && s.m.type == DBUS_RETURN, "a caller that subscribes after is answered its AddMatch");
        db_next(a, &s);
        DBCHECK(db_is_response(&s, a, path, 2), "and then sent the Response its new rule matches");

        // no rule at all: sent at the deadline, not before
        db_request_path(a, "save2", path, sizeof path);
        db_portal_call(a, "org.freedesktop.portal.FileChooser", "SaveFiles", "save2", 0);
        db_next(a, &s);
        db_time += DBUS_RESPONSE_MS - 1;
        dbus_tick();
        DBCHECK(!db_queued(a), "a Response no rule matches waits out its time (%ld bytes after %d ms)", db_queued(a), DBUS_RESPONSE_MS - 1);
        db_time += 2;
        dbus_tick();
        db_next(a, &s);
        DBCHECK(db_is_response(&s, a, path, 2), "and is sent at DBUS_RESPONSE_MS all the same: SaveFiles' too");
        DBCHECK(dbus_wait(60000) == 60000, "and nothing is due after it (%ld)", dbus_wait(60000));

        // a handle_token that is not one element of a path is the bus's own
        db_portal_call(a, "org.freedesktop.portal.FileChooser", "OpenFile", "a-b/c", 0);
        db_next(a, &s);
        DBCHECK(s.ok && db_string_arg(&s, 0, text, sizeof text) && strstr(text, "/moonwater") && !strchr(strrchr(text, '/'), '-'),
                "a handle_token with a dash or a slash is not put in a path: %s", s.ok ? text : "nothing");
        db_portal_call(a, "org.freedesktop.portal.FileChooser", "OpenFile", 0, 0);
        db_next(a, &s);
        DBCHECK(s.ok && db_string_arg(&s, 0, path, sizeof path) && dbus_valid_path(path, (long)strlen(path)) && strcmp(path, text),
                "and a call with none has a path of its own: %s", s.ok ? path : "nothing");
        {
                char token[80];

                memset(token, 'x', 70);
                token[70] = 0;
                db_portal_call(a, "org.freedesktop.portal.FileChooser", "OpenFile", token, 0);
                db_next(a, &s);
                DBCHECK(s.ok && db_string_arg(&s, 0, text, sizeof text) && !strstr(text, "xxxxxxxx"), "nor one past %d bytes: %s", DBUS_TOKEN, s.ok ? text : "");
        }
        db_time += DBUS_RESPONSE_MS + 1;
        dbus_tick();
        for (int k = 0; k < 3; k++)
        {
                db_next(a, &s);
                DBCHECK(s.ok && s.m.type == DBUS_SIGNAL && !strcmp(s.m.member, "Response") && db_u32_body(&s) == 2, "each of them has its Response");
        }

        // Close before the Response: none is sent (a closed request emits none)
        db_request_path(a, "closed", path, sizeof path);
        db_portal_call(a, "org.freedesktop.portal.FileChooser", "OpenFile", "closed", 0);
        db_next(a, &s);
        db_call(a, DBUS_NAME_PORTAL, path, "org.freedesktop.portal.Request", "Close", "", 0, 0);
        db_next(a, &s);
        DBCHECK(s.ok && s.m.type == DBUS_RETURN, "Request.Close is answered");
        db_time += DBUS_RESPONSE_MS + 1;
        dbus_tick();
        DBCHECK(!db_queued(a), "and the request it closed sends no Response at its time (%ld bytes)", db_queued(a));

        // more than a client may be owed: the one past them is sent at once, the others at their time
        for (int k = 0; k <= DBUS_RESPONSES; k++)
        {
                char token[16];

                snprintf(token, sizeof token, "many%d", k);
                db_portal_call(a, "org.freedesktop.portal.FileChooser", "OpenFile", token, 0);
        }
        {
                int answers = 0, responses = 0;

                while (db_next(a, &s), s.ok)
                        answers += s.m.type == DBUS_RETURN, responses += s.m.type == DBUS_SIGNAL;
                DBCHECK(answers == DBUS_RESPONSES + 1 && responses == 1, "%d requests at once: each answered, the one past the %d held sent at once (%d, %d)",
                        DBUS_RESPONSES + 1, DBUS_RESPONSES, answers, responses);
                db_time += DBUS_RESPONSE_MS + 1;
                dbus_tick();
                responses = 0;
                while (db_next(a, &s), s.ok)
                        responses += s.m.type == DBUS_SIGNAL && !strcmp(s.m.member, "Response");
                DBCHECK(responses == DBUS_RESPONSES, "and the %d held at their time (%d)", DBUS_RESPONSES, responses);
        }

        // Inhibit.CreateMonitor: its request is answered and ends at once, as there is no session to monitor
        db_request_path(a, "monitor", path, sizeof path);
        snprintf(rule, sizeof rule, "type='signal',interface='org.freedesktop.portal.Request',path='%s'", path);
        db_rule(a, rule);
        db_drain(a);
        db_portal_call(a, "org.freedesktop.portal.Inhibit", "CreateMonitor", "monitor", 0);
        db_next(a, &s);
        DBCHECK(s.ok && s.m.type == DBUS_RETURN && db_string_arg(&s, 0, text, sizeof text) && !strcmp(text, path), "CreateMonitor answers its request's path");
        db_next(a, &s);
        DBCHECK(db_is_response(&s, a, path, 2), "and the Response GTK 4 waits for, at once (before, it was never sent)");

        // a call that asks for no reply still has its Response: the caller made the path of its token itself
        db_request_path(a, "quiet", path, sizeof path);
        db_portal_call(a, "org.freedesktop.portal.FileChooser", "OpenFile", "quiet", DBUS_FLAG_NO_REPLY);
        DBCHECK(!db_queued(a), "a request with NO_REPLY_EXPECTED is not answered");
        db_time += DBUS_RESPONSE_MS + 1;
        dbus_tick();
        db_next(a, &s);
        DBCHECK(db_is_response(&s, a, path, 2), "and its Response is sent all the same");

        // a request's Response goes with its caller: nothing is left of it when the caller goes
        db_portal_call(b, "org.freedesktop.portal.FileChooser", "OpenFile", "gone", 0);
        dbus_drop(b);
        db_time += DBUS_RESPONSE_MS + 1;
        dbus_tick();
        DBCHECK(!db_queued(a), "a caller that goes takes its held Response with it");
        dbus_drop(a);
        DBCHECK(db_allocs == db_releases, "every record is returned (%ld %ld)", db_allocs, db_releases);
}

/*
        The FileChooser answered by the host's chooser (Files, in the sessionbus): OpenFile and SaveFile are the request's path at
        once, the host is asked to show a chooser with what the program said (the title, multiple, directory, the name, the folder),
        and the Response comes when the host says what was picked: 0 and the file: URIs, or 1 for none. One chooser a client; a
        Close, or the client gone, lets it go and sends nothing; what was picked for a client that is gone goes nowhere.
*/
static struct dbus_choice db_chosen;
static long db_choices, db_unchosen = -1, db_choose_refuses;

static long db_choose_seen(void *context, const struct dbus_choice *choice)
{
        (void)context;
        db_chosen = *choice, db_choices++;
        return db_choose_refuses ? -1 : 0;
}

static void db_unchoose_seen(void *context, long slot)
{
        (void)context;
        db_unchosen = slot;
}

static const struct dbus_host db_chooser_host = {0, db_alloc, db_release, db_now, db_random, 0, 0, db_choose_seen, db_unchoose_seen};

// OpenFile or SaveFile with the options a chooser reads: multiple and directory (b), current_name (s), current_folder or current_file (ay).
static void db_choose_call(long slot, const char *member, const char *token, int multiple, int directory, const char *name, const char *folder_key,
                           const char *folder)
{
        struct db_call c;
        struct dbus_fields f = {DBUS_PATH_PORTAL, "org.freedesktop.portal.FileChooser", member, "", DBUS_NAME_PORTAL, "", "ssa{sv}", 0, 0};
        long length_at, first;

        db_begin(&c, DBUS_CALL, 0, &f);
        dbus_o_string(&c.w, "x11:1a");
        dbus_o_string(&c.w, "Open Image");
        dbus_o_u32(&c.w, 0);
        length_at = c.w.length - 4;
        dbus_o_pad(&c.w, 8);
        first = c.w.length;
        dbus_o_pad(&c.w, 8);
        dbus_o_string(&c.w, "handle_token");
        dbus_o_sig(&c.w, "s");
        dbus_o_string(&c.w, token);
        dbus_o_pad(&c.w, 8);
        dbus_o_string(&c.w, "multiple");
        dbus_o_sig(&c.w, "b");
        dbus_o_u32(&c.w, (unsigned long)multiple);
        dbus_o_pad(&c.w, 8);
        dbus_o_string(&c.w, "directory");
        dbus_o_sig(&c.w, "b");
        dbus_o_u32(&c.w, (unsigned long)directory);
        if (name)
        {
                dbus_o_pad(&c.w, 8);
                dbus_o_string(&c.w, "current_name");
                dbus_o_sig(&c.w, "s");
                dbus_o_string(&c.w, name);
        }
        if (folder)
        {
                dbus_o_pad(&c.w, 8);
                dbus_o_string(&c.w, folder_key);
                dbus_o_sig(&c.w, "ay");
                dbus_o_u32(&c.w, (unsigned long)strlen(folder) + 1);
                dbus_o_raw(&c.w, folder, (long)strlen(folder) + 1);
        }
        dbus_o_patch32(&c.w, length_at, (unsigned long)(c.w.length - first));
        db_send(slot, &c);
}

// The uris of a Response(0, {uris: as}), joined by newlines into `into`: how many there were, -1 when it is not that.
static int db_response_uris(const struct db_seen *s, char *into, long room)
{
        const unsigned char *p = s->bytes + s->m.body_at;
        long at = 8, stop, n = 0;
        int count = 0;

        into[0] = 0;
        if (!s->ok || s->m.type != DBUS_SIGNAL || strcmp(s->m.signature, "ua{sv}") || dbus_get32(p) != 0)
                return -1;
        stop = 8 + (long)dbus_get32(p + 4);
        if (stop > s->m.body_length || at + 4 + 4 > stop || dbus_get32(p + at) != 4 || memcmp(p + at + 4, "uris", 5) || p[at + 9] != 2 ||
            memcmp(p + at + 10, "as", 3))
                return -1;
        at = (at + 13 + 3) & ~3L;
        stop = at + 4 + (long)dbus_get32(p + at);
        for (at += 4; at < stop; at = (at + 4 + (long)dbus_get32(p + at) + 1 + 3) & ~3L, count++)
        {
                long length = (long)dbus_get32(p + at);

                if (n + length + 2 > room)
                        return -1;
                if (n)
                        into[n++] = '\n';
                memcpy(into + n, p + at + 4, (size_t)length), n += length, into[n] = 0;
        }
        return count;
}

static void db_test_chooser(void)
{
        long a, b;
        struct db_seen s;
        char path[200], text[600];

        db_reset();
        dbus_stop();
        dbus_start(&db_chooser_host);
        dbus_interface_claim("org.freedesktop.portal.FileChooser", 4);
        db_choices = db_choose_refuses = 0, db_unchosen = -1;
        a = db_connect(1000);
        b = db_connect(1000);
        db_hello(a);
        db_hello(b);
        db_drain(a);
        db_drain(b);
        db_call(a, DBUS_NAME_PORTAL, DBUS_PATH_PORTAL, "org.freedesktop.DBus.Properties", "Get", "ss", "org.freedesktop.portal.FileChooser", "version");
        db_next(a, &s);
        DBCHECK(s.ok && s.m.type == DBUS_RETURN && dbus_get32(s.bytes + s.m.body_at + 4) == 4,
                "a host with a chooser claims FileChooser 4, the version a browser opens its file dialogs through");

        db_request_path(a, "pick1", path, sizeof path);
        db_rule(a, "type='signal',interface='org.freedesktop.portal.Request'");
        db_drain(a);
        db_choose_call(a, "OpenFile", "pick1", 1, 0, 0, "current_folder", "/root/Pictures");
        db_next(a, &s);
        DBCHECK(s.ok && s.m.type == DBUS_RETURN && db_string_arg(&s, 0, text, sizeof text) && !strcmp(text, path), "OpenFile answers its request's path");
        DBCHECK(!db_queued(a), "and no Response yet: the person is picking (%ld bytes)", db_queued(a));
        DBCHECK(db_choices == 1 && db_chosen.slot == a && db_chosen.kind == DBUS_CHOOSE_OPEN && db_chosen.multiple && !db_chosen.directory &&
                    !strcmp(db_chosen.title, "Open Image") && !strcmp(db_chosen.folder, "/root/Pictures") && !db_chosen.name[0],
                "the host is asked for a chooser with the program's title, multiple and its folder");
        db_time += DBUS_RESPONSE_MS * 10;
        dbus_tick();
        DBCHECK(!db_queued(a), "a chooser's request waits for the person, however long (%ld bytes)", db_queued(a));
        dbus_choice_done(a, db_chosen.id + 1, "/root/x\0", 8);
        DBCHECK(!db_queued(a), "what another client's chooser picked is not this one's (%ld bytes)", db_queued(a));
        dbus_choice_done(a, db_chosen.id, "/root/Pictures/a b.png\0/root/Pictures/\xc3\xa4%.png\0relative\0", 55);
        db_next(a, &s);
        DBCHECK(db_response_uris(&s, text, sizeof text) == 2 && !strcmp(s.m.path, path) && !strcmp(s.m.destination, db_unique(a)) &&
                    !strcmp(text, "file:///root/Pictures/a%20b.png\nfile:///root/Pictures/%C3%A4%25.png"),
                "the picked paths are the Response's uris, each a file: URI with what is not unreserved escaped, a relative one left out: %s", text);

        db_request_path(a, "save1", path, sizeof path);
        db_choose_call(a, "SaveFile", "save1", 0, 0, "Untitled.xcf", "current_file", "/root/Documents/drawing.xcf");
        db_next(a, &s);
        DBCHECK(db_choices == 2 && db_chosen.kind == DBUS_CHOOSE_SAVE && !strcmp(db_chosen.folder, "/root/Documents") &&
                    !strcmp(db_chosen.name, "drawing.xcf"),
                "SaveFile of a file there already (current_file) asks to save it again where it is, under its own name, whatever current_name says: %s %s",
                db_chosen.folder, db_chosen.name);
        db_choose_call(a, "OpenFile", "pick2", 0, 1, 0, 0, 0);
        db_next(a, &s);
        db_next(a, &s);
        DBCHECK(db_choices == 2 && s.ok && s.m.type == DBUS_SIGNAL && db_u32_body(&s) == 2, "a second request while a chooser is open ends at once (2)");
        dbus_choice_done(a, db_chosen.id, "", 0);
        db_next(a, &s);
        DBCHECK(db_is_response(&s, a, path, 1), "a chooser shut with nothing picked is the person's cancel (1)");

        db_request_path(a, "closed", path, sizeof path);
        db_choose_call(a, "OpenFile", "closed", 0, 1, "../x", "current_folder", "relative/folder");
        db_next(a, &s);
        DBCHECK(db_chosen.directory && !db_chosen.folder[0] && !db_chosen.name[0], "a folder that is not absolute is no folder to start in, nor is a name a path");
        db_call(a, DBUS_NAME_PORTAL, path, "org.freedesktop.portal.Request", "Close", "", 0, 0);
        db_next(a, &s);
        DBCHECK(db_unchosen == a, "Request.Close lets the chooser go");
        dbus_choice_done(a, db_chosen.id, "/root/x\0", 8);
        DBCHECK(!db_queued(a), "and what it picked after is sent nowhere (%ld bytes)", db_queued(a));

        db_unchosen = -1;
        db_choose_call(b, "OpenFile", "gone", 0, 0, 0, 0, 0);
        db_next(b, &s);
        dbus_drop(b);
        DBCHECK(db_unchosen == b, "a client that goes takes its chooser with it");
        dbus_choice_done(b, db_chosen.id, "/root/x\0", 8);

        db_choose_refuses = 1;
        db_request_path(a, "refused", path, sizeof path);
        db_choose_call(a, "OpenFile", "refused", 0, 0, 0, 0, 0);
        db_next(a, &s);
        db_next(a, &s);
        DBCHECK(db_is_response(&s, a, path, 2), "a chooser the host cannot show ends the request at once (2), as with no chooser");
        dbus_drop(a);
        dbus_interface_claim("org.freedesktop.portal.FileChooser", 1);
        DBCHECK(db_allocs == db_releases, "every record is returned (%ld %ld)", db_allocs, db_releases);
        dbus_stop();
        dbus_start(&db_host);
}

/*
        Deadlines on the bus's waits (2026-11-15): a call routed to a client that never answers held its caller's row for ever,
        so a callee could take every row of a caller (DBUS_PENDING_PER) and have each later call of it refused LimitsExceeded.
        Now the caller is told NoReply at DBUS_REPLY_MS and its row let go; a late answer goes nowhere. And a call to a name
        nobody owns, or StartServiceByName of one, is ServiceUnknown in the call: there is nothing to start and nothing waits.
*/
static void db_test_deadlines(void)
{
        long callee, caller;
        struct db_seen s;
        unsigned long serial;
        int refused = 0;

        db_reset();
        callee = db_connect(1000);
        caller = db_connect(1000);
        db_hello(callee);
        db_hello(caller);
        db_request(callee, "org.example.Silent", 0);
        db_drain(callee);
        db_drain(caller);
        for (int k = 0; k < DBUS_PENDING_PER; k++)
                db_call(caller, "org.example.Silent", "/o", "org.example.I", "Hang", "", 0, 0);
        serial = db_last_serial();
        db_call(caller, "org.example.Silent", "/o", "org.example.I", "Hang", "", 0, 0);
        db_next(caller, &s);
        DBCHECK(s.ok && s.m.type == DBUS_ERROR && !strcmp(s.m.error, "org.freedesktop.DBus.Error.LimitsExceeded"),
                "a callee that answers none of %d calls fills the caller's rows: the next is refused", DBUS_PENDING_PER);
        db_time += DBUS_REPLY_MS - 1;
        dbus_tick();
        DBCHECK(!db_queued(caller) && dbus.client[caller]->pending == DBUS_PENDING_PER, "and the calls wait out DBUS_REPLY_MS (%ld bytes, %ld waiting)",
                db_queued(caller), dbus.client[caller]->pending);
        DBCHECK(dbus_wait(60000) == 1, "the bus asks to be woken when the first is due (%ld ms)", dbus_wait(60000));
        db_time += 2;
        dbus_tick();
        while (db_next(caller, &s), s.ok)
                refused += s.m.type == DBUS_ERROR && !strcmp(s.m.error, "org.freedesktop.DBus.Error.NoReply") && !strcmp(s.m.sender, DBUS_NAME_BUS);
        DBCHECK(refused == DBUS_PENDING_PER && dbus.client[caller]->pending == 0, "at DBUS_REPLY_MS each is answered NoReply and its row let go (%d, %ld waiting)",
                refused, dbus.client[caller]->pending);
        {
                struct db_call reply;
                struct dbus_fields f = {"", "", "", "", db_unique(caller), "", "", serial, 1};

                db_begin(&reply, DBUS_RETURN, 0, &f);
                db_send(callee, &reply);
                DBCHECK(!db_queued(caller) && !dbus_dead(callee), "a late answer goes nowhere, and is no fault of its sender");
        }
        db_drain(callee);
        db_call(caller, "org.example.Silent", "/o", "org.example.I", "Hang", "", 0, 0);
        DBCHECK(db_find(callee, &s, DBUS_CALL, "Hang") && dbus.client[caller]->pending == 1, "and the caller's next call is routed again");

        // a name nobody owns: the error is in the queue when the call is read, with no tick and no time gone, and in well under a millisecond
        {
                struct timespec t0, t1;
                long worst = 0;

                db_drain(caller);
                for (int k = 0; k < 64; k++)
                {
                        long ns;

                        clock_gettime(CLOCK_MONOTONIC, &t0);
                        db_call(caller, "org.freedesktop.secrets", "/org/freedesktop/secrets", "org.freedesktop.Secret.Service", "OpenSession", "", 0, 0);
                        clock_gettime(CLOCK_MONOTONIC, &t1);
                        ns = (t1.tv_sec - t0.tv_sec) * 1000000000L + t1.tv_nsec - t0.tv_nsec;
                        worst = ns > worst ? ns : worst;
                        db_next(caller, &s);
                        if (!(s.ok && s.m.type == DBUS_ERROR && !strcmp(s.m.error, "org.freedesktop.DBus.Error.ServiceUnknown") && s.m.reply == db_last_serial()))
                                worst = 1L << 40;
                }
                DBCHECK(worst < 1000000, "a call to a name nobody owns, auto-start allowed, is ServiceUnknown within the call: the slowest of 64 in %ld ns", worst);
        }
        db_su(caller, "StartServiceByName", "org.a11y.Bus", 0);
        db_next(caller, &s);
        DBCHECK(s.ok && s.m.type == DBUS_ERROR && !strcmp(s.m.error, "org.freedesktop.DBus.Error.ServiceUnknown"), "StartServiceByName of a name nobody owns is ServiceUnknown at once");
        db_su(caller, "StartServiceByName", DBUS_NAME_PORTAL, 0);
        db_next(caller, &s);
        DBCHECK(s.ok && s.m.type == DBUS_RETURN && db_u32_body(&s) == 2, "and of the portal, 2: it is running");
        // a callee that goes answers its callers NoReply at once, and leaves no deadline behind
        dbus_drop(callee);
        db_next(caller, &s);
        DBCHECK(s.ok && s.m.type == DBUS_ERROR && !strcmp(s.m.error, "org.freedesktop.DBus.Error.NoReply"), "a callee that goes is NoReply to its caller at once");
        dbus_drop(caller);
        DBCHECK(db_allocs == db_releases, "every record is returned (%ld %ld)", db_allocs, db_releases);
}

/*
        Clean refusals (MW-2026-197): the names whose owner every bowl's programs would send keys, passwords or notifications to
        are no client's, and a call to them is ServiceUnknown; the portal interfaces that need PipeWire or a person's choice that
        Moonwater has no way to ask (GlobalShortcuts, ScreenCast, Screenshot, RemoteDesktop) are not claimed: their version is
        UnknownProperty and their methods UnknownMethod, at once, so a browser hides the feature.
*/
static void db_test_kept(void)
{
        static const char *const kept[] = {"org.a11y.Bus", "org.freedesktop.secrets", "org.freedesktop.Notifications"};
        // OpenURI too: a browser started by the bus would be root's on the host, started for any client (the reason is in dbus.c)
        static const char *const unclaimed[] = {"org.freedesktop.portal.GlobalShortcuts", "org.freedesktop.portal.ScreenCast",
                                                "org.freedesktop.portal.Screenshot", "org.freedesktop.portal.RemoteDesktop",
                                                "org.freedesktop.portal.OpenURI"};
        static const char *const methods[] = {"CreateSession", "CreateSession", "Screenshot", "CreateSession", "OpenURI"};
        long a, b;
        struct db_seen s;

        db_reset();
        a = db_connect(1000);
        b = db_connect(1000);
        db_hello(a);
        db_hello(b);
        db_drain(a);
        db_drain(b);
        for (int k = 0; k < 3; k++)
        {
                db_request(a, kept[k], 0);
                db_next(a, &s);
                DBCHECK(s.ok && s.m.type == DBUS_ERROR && !strcmp(s.m.error, "org.freedesktop.DBus.Error.AccessDenied"), "%s is no client's to own: AccessDenied",
                        kept[k]);
                db_request(a, kept[k], 2 | 4);
                db_next(a, &s);
                DBCHECK(s.ok && s.m.type == DBUS_ERROR, "and asking to replace its owner does not make it so");
                db_call(b, DBUS_NAME_BUS, "/org/freedesktop/DBus", DBUS_NAME_BUS, "NameHasOwner", "s", kept[k], 0);
                db_find(b, &s, DBUS_RETURN, 0);
                // the notification server is the bus's own service (db_test_notifications); the others nobody's
                DBCHECK(s.ok && s.m.type == DBUS_RETURN && db_u32_body(&s) == (k == 2), "%s %s", k == 2 ? "the bus's service owns" : "nobody owns", kept[k]);
                db_call(b, kept[k], "/", "org.example.Any", "Any", "", 0, 0);
                db_next(b, &s);
                DBCHECK(s.ok && s.m.type == DBUS_ERROR && !strcmp(s.m.error, k == 2 ? "org.freedesktop.DBus.Error.UnknownObject" : "org.freedesktop.DBus.Error.ServiceUnknown") &&
                            s.m.reply == db_last_serial(),
                        "a call to %s is %s, in the call", kept[k], k == 2 ? "the service's own error" : "ServiceUnknown");
                db_drain(b);
        }
        db_request(a, "org.a11y.Bus2", 0);
        db_find(a, &s, DBUS_RETURN, 0);
        DBCHECK(s.ok && s.m.type == DBUS_RETURN && db_u32_body(&s) == 1, "a name that only starts like one kept is a client's as any other");
        db_drain(a);
        db_call(b, DBUS_NAME_BUS, "/org/freedesktop/DBus", DBUS_NAME_BUS, "ListActivatableNames", "", 0, 0);
        db_find(b, &s, DBUS_RETURN, 0);
        DBCHECK(s.ok && !db_holds(&s, "org.a11y.Bus\0") && !db_holds(&s, "org.freedesktop.secrets") && !db_holds(&s, "org.freedesktop.Notifications"),
                "and no name kept is said to be one that can be started");
        for (int k = 0; k < 5; k++)
        {
                db_call(b, DBUS_NAME_PORTAL, DBUS_PATH_PORTAL, "org.freedesktop.DBus.Properties", "Get", "ss", unclaimed[k], "version");
                db_next(b, &s);
                DBCHECK(s.ok && s.m.type == DBUS_ERROR && !strcmp(s.m.error, "org.freedesktop.DBus.Error.UnknownProperty"), "%s is not claimed: its version is UnknownProperty",
                        unclaimed[k]);
                db_call(b, DBUS_NAME_PORTAL, DBUS_PATH_PORTAL, "org.freedesktop.DBus.Properties", "GetAll", "s", unclaimed[k], 0);
                db_next(b, &s);
                DBCHECK(s.ok && s.m.type == DBUS_RETURN && !strcmp(s.m.signature, "a{sv}") && dbus_get32(s.bytes + s.m.body_at) == 0, "and GetAll of it is empty");
                db_call(b, DBUS_NAME_PORTAL, DBUS_PATH_PORTAL, unclaimed[k], methods[k], "", 0, 0);
                db_next(b, &s);
                DBCHECK(s.ok && s.m.type == DBUS_ERROR && !strcmp(s.m.error, "org.freedesktop.DBus.Error.UnknownMethod") && !db_queued(b),
                        "and %s.%s is UnknownMethod at once, with no request to wait on", unclaimed[k], methods[k]);
        }
        db_call(b, DBUS_NAME_PORTAL, DBUS_PATH_PORTAL, "org.freedesktop.DBus.Introspectable", "Introspect", "", 0, 0);
        db_next(b, &s);
        DBCHECK(s.ok && memmem(s.bytes, (size_t)s.size, "portal.FileChooser", 18) && !memmem(s.bytes, (size_t)s.size, "ScreenCast", 10) &&
                    !memmem(s.bytes, (size_t)s.size, "GlobalShortcuts", 15) && !memmem(s.bytes, (size_t)s.size, "Screenshot", 10) &&
                    !memmem(s.bytes, (size_t)s.size, "RemoteDesktop", 13),
                "and the portal's introspection names none of them");
        dbus_drop(a);
        dbus_drop(b);
        DBCHECK(db_allocs == db_releases, "every record is returned (%ld %ld)", db_allocs, db_releases);
}

/*
        The desktop's notifications (MW-2026-198): org.freedesktop.Notifications and the portal's Notification, served by the bus into
        the records its host is given, every cap with a row: the text's size and its foreign bytes, the records, the time, the rate of
        a client and of the bus, and who may replace and close one and hear it close.
*/
static struct dbus_notice db_notices[DBUS_NOTICES];
static long db_notices_told;

static void db_notices_seen(void *context, const struct dbus_notice *notice, long count)
{
        (void)context;
        memcpy(db_notices, notice, sizeof db_notices);
        db_notices_told += count == DBUS_NOTICES;
}

static const struct dbus_host db_notice_host = {0, db_alloc, db_release, db_now, db_random, 0, db_notices_seen, 0, 0};

static const struct dbus_notice *db_notice_of(unsigned long id)
{
        for (int k = 0; k < DBUS_NOTICES; k++)
                if (id && db_notices[k].id == id)
                        return &db_notices[k];
        return 0;
}

static int db_notices_kept(void)
{
        int n = 0;

        for (int k = 0; k < DBUS_NOTICES; k++)
                n += db_notices[k].id != 0;
        return n;
}

// Notify(app, replaces, "", summary, body, [], {urgency when it is 0 to 2}, expire) from a client: the id it is answered, 0 for an error.
static unsigned long db_notify(long slot, const char *app, unsigned long replaces, const char *summary, const char *body, long expire, int urgency)
{
        static unsigned char bytes[16384]; // a summary may be long: more than a struct db_call's 4096
        struct dbus_out w = {bytes, 0, sizeof bytes, 0};
        struct dbus_fields f = {DBUS_PATH_NOTIFY, DBUS_NAME_NOTIFY, "Notify", "", DBUS_NAME_NOTIFY, "", "susssasa{sv}i", 0, 0};
        long length_at, first, body_at;
        struct db_seen s;
        unsigned long id = 0;

        dbus_o_header(&w, DBUS_CALL, 0, ++db_serial, &f);
        body_at = w.length;
        dbus_o_string(&w, app);
        dbus_o_u32(&w, replaces);
        dbus_o_string(&w, "");
        dbus_o_string(&w, summary);
        dbus_o_string(&w, body);
        dbus_o_u32(&w, 0); // no actions
        dbus_o_u32(&w, 0);
        length_at = w.length - 4;
        dbus_o_pad(&w, 8);
        first = w.length;
        if (urgency >= 0)
        {
                dbus_o_pad(&w, 8);
                dbus_o_string(&w, "urgency");
                dbus_o_sig(&w, "y");
                dbus_o_u8(&w, (unsigned)urgency);
        }
        dbus_o_patch32(&w, length_at, (unsigned long)(w.length - first));
        dbus_o_u32(&w, (unsigned long)expire & 0xffffffffUL);
        dbus_o_end(&w, body_at);
        db_feed(slot, bytes, w.length);
        if (db_find(slot, &s, DBUS_RETURN, 0) && !strcmp(s.m.signature, "u"))
                id = db_u32_body(&s);
        return id;
}

// The next NotificationClosed queued for a client: its id and reason, or 0 when there is none.
static unsigned long db_closed(long slot, unsigned long *reason)
{
        struct db_seen s;

        *reason = 0;
        while (db_next(slot, &s), s.ok)
                if (s.m.type == DBUS_SIGNAL && !strcmp(s.m.member, "NotificationClosed") && !strcmp(s.m.destination, db_unique(slot)) &&
                    !strcmp(s.m.sender, ":1.1") && !strcmp(s.m.path, DBUS_PATH_NOTIFY))
                {
                        *reason = dbus_get32(s.bytes + s.m.body_at + 4);
                        return db_u32_body(&s);
                }
        return 0;
}

static void db_test_notifications(void)
{
        long a, b, c;
        struct db_seen s;
        unsigned long id, other, reason;
        const struct dbus_notice *n;
        char text[300];

        db_reset();
        dbus_stop();
        dbus_start(&db_notice_host);
        memset(db_notices, 0, sizeof db_notices);
        db_notices_told = 0;
        a = db_connect(1000);
        b = db_connect(1000);
        c = db_connect(1000);
        db_hello(a);
        db_hello(b);
        db_hello(c);
        db_rule(b, "type='signal'");
        db_drain(a);
        db_drain(b);
        db_drain(c);

        db_call(a, DBUS_NAME_BUS, "/org/freedesktop/DBus", DBUS_NAME_BUS, "GetNameOwner", "s", DBUS_NAME_NOTIFY, 0);
        db_next(a, &s);
        DBCHECK(s.ok && s.m.type == DBUS_RETURN && db_string_arg(&s, 0, text, sizeof text) && !strcmp(text, ":1.1"), "org.freedesktop.Notifications is the bus's own: :1.1");
        db_call(a, DBUS_NAME_NOTIFY, DBUS_PATH_NOTIFY, DBUS_NAME_NOTIFY, "GetCapabilities", "", 0, 0);
        db_next(a, &s);
        DBCHECK(s.ok && s.m.type == DBUS_RETURN && !strcmp(s.m.signature, "as") && dbus_get32(s.bytes + s.m.body_at) == 9 && db_holds(&s, "body"),
                "GetCapabilities is body alone: no markup, no actions, no icons");
        db_call(a, DBUS_NAME_NOTIFY, DBUS_PATH_NOTIFY, DBUS_NAME_NOTIFY, "GetServerInformation", "", 0, 0);
        db_next(a, &s);
        DBCHECK(s.ok && s.m.type == DBUS_RETURN && !strcmp(s.m.signature, "ssss") && db_string_arg(&s, 3, text, sizeof text) && !strcmp(text, "1.2"),
                "GetServerInformation is four strings, the specification's version last");
        db_call(a, DBUS_NAME_NOTIFY, DBUS_PATH_NOTIFY, "org.freedesktop.DBus.Introspectable", "Introspect", "", 0, 0);
        db_next(a, &s);
        DBCHECK(s.ok && memmem(s.bytes, (size_t)s.size, "NotificationClosed", 18), "and its path introspects as the notification server");

        id = db_notify(a, "Chromium", 0, "Hello", "World", -1, -1);
        n = db_notice_of(id);
        DBCHECK(id && n && !strcmp(n->app, "Chromium") && !strcmp(n->text, "Hello - World") && n->urgency == 1 && n->due == db_time + DBUS_NOTICE_MS && db_notices_told,
                "Notify answers an id and the host is given the notice: its program, summary - body, normal, kept DBUS_NOTICE_MS");
        DBCHECK(!db_queued(b), "a client listening for every signal hears nothing of another's notification");

        // a stranger's bytes are put down as the kit draws them: control bytes as their pictures, reordering marks as U+FFFD
        id = db_notify(a, "Evil\xe2\x80\xae" "app", 0, "a\x1b[2Jb\nc\x7f\xe2\x80\xae\xc2\x85\xe2\x81\xa6\xe2\x80\x8f" "d", "\xef\xbb\xbf", -1, 2);
        n = db_notice_of(id);
        DBCHECK(n && !strcmp(n->text, "a\xe2\x90\x9b[2Jb\xe2\x90\x8a" "c\xe2\x90\xa1\xef\xbf\xbd\xef\xbf\xbd\xef\xbf\xbd\xef\xbf\xbd" "d - \xef\xbf\xbd") &&
                    !strcmp(n->app, "Evil\xef\xbf\xbd" "app") && n->urgency == 2,
                "an escape, a newline and a delete are their pictures, and an override, a C1, an isolate, a mark and a BOM are U+FFFD: %s", n ? n->text : "none");
        {
                char big[3200];

                memset(big, 'x', 300);
                big[300] = 0;
                for (int k = 0; k < 1400; k++)
                        big[300 + 2 * k] = (char)0xc3, big[301 + 2 * k] = (char)0xa9;
                big[300 + 2800] = 0;
                big[0] = 'x';
                id = db_notify(c, big + 2000, 0, big + 300, big + 300, -1, -1);
                n = db_notice_of(id);
                DBCHECK(n && strlen(n->text) <= DBUS_NOTICE_TEXT - 1 && strlen(n->text) >= DBUS_NOTICE_TEXT - 2 && dbus_utf8_walk((const unsigned char *)n->text, (long)strlen(n->text)) &&
                            strlen(n->app) <= DBUS_NOTICE_APP - 1 && dbus_utf8_walk((const unsigned char *)n->app, (long)strlen(n->app)),
                        "a summary and a body of 1400 two-byte letters each are cut to %d bytes and a program's name to %d, each at a letter's end (%zu, %zu)",
                        DBUS_NOTICE_TEXT - 1, DBUS_NOTICE_APP - 1, n ? strlen(n->text) : 0, n ? strlen(n->app) : 0);
                id = db_notify(c, "", 0, "", "", -1, -1);
                n = db_notice_of(id);
                DBCHECK(n && !strcmp(n->app, "a program") && !n->text[0], "a notification with no name and no words is a program's, with nothing said");
        }

        // replaced and closed by its poster alone, and its NotificationClosed to it alone
        db_time += DBUS_NOTICE_SPAN_MS;
        id = db_notify(a, "Chromium", 0, "One", "", -1, -1);
        other = db_notify(a, "Chromium", id, "Two", "", -1, -1);
        DBCHECK(other == id && db_notice_of(id) && !strcmp(db_notice_of(id)->text, "Two"), "replaces_id of the poster's own replaces it in place, with its id");
        other = db_notify(b, "Other", id, "Three", "", -1, -1);
        DBCHECK(other && other != id && !strcmp(db_notice_of(id)->text, "Two"), "and another client's replaces_id of it is a notification of its own");
        db_drain(a); // (the records were full: a's oldest gave way to b's, and a was told)
        db_drain(b);
        {
                struct db_call call;
                struct dbus_fields f = {DBUS_PATH_NOTIFY, DBUS_NAME_NOTIFY, "CloseNotification", "", DBUS_NAME_NOTIFY, "", "u", 0, 0};

                db_drain(b);
                db_begin(&call, DBUS_CALL, 0, &f);
                dbus_o_u32(&call.w, id);
                db_send(b, &call);
                db_next(b, &s);
                DBCHECK(s.ok && s.m.type == DBUS_RETURN && db_notice_of(id) && !db_queued(a), "another client's CloseNotification of it is answered and closes nothing");
                db_begin(&call, DBUS_CALL, 0, &f);
                dbus_o_u32(&call.w, id);
                db_send(a, &call);
                db_next(a, &s);
                DBCHECK(s.ok && s.m.type == DBUS_RETURN && !db_notice_of(id), "the poster's closes it");
                DBCHECK(db_closed(a, &reason) == id && reason == 3, "and NotificationClosed(id, 3) is sent to the poster (%lu)", reason);
                DBCHECK(!db_queued(b), "and to no one else");
        }

        // time: what it names, from DBUS_NOTICE_SHORTEST to DBUS_NOTICE_MS; 0 is until it is closed
        db_time += DBUS_NOTICE_SPAN_MS;
        id = db_notify(a, "Clock", 0, "Two seconds", "", 2000, -1);
        DBCHECK(dbus_wait(60000) <= 2000, "the bus asks to be woken when a notification is due (%ld ms)", dbus_wait(60000));
        db_time += 1999;
        dbus_tick();
        DBCHECK(db_notice_of(id) && !db_closed(a, &reason), "a notification of 2000 ms is kept 1999");
        db_time += 2;
        dbus_tick();
        DBCHECK(!db_notice_of(id) && db_closed(a, &reason) == id && reason == 1, "and expires at 2000: NotificationClosed(id, 1) (%lu)", reason);
        id = db_notify(a, "Clock", 0, "A moment", "", 10, -1);
        DBCHECK(db_notice_of(id) && db_notice_of(id)->due == db_time + DBUS_NOTICE_SHORTEST, "10 ms is DBUS_NOTICE_SHORTEST");
        other = db_notify(a, "Clock", 0, "For ever", "", 0, -1);
        DBCHECK(db_notice_of(other) && !db_notice_of(other)->due, "and 0 is no time");
        id = db_notify(a, "Clock", 0, "A week", "", 604800000, -1);
        DBCHECK(db_notice_of(id) && db_notice_of(id)->due == db_time + DBUS_NOTICE_MS, "and a week is DBUS_NOTICE_MS");
        db_time += DBUS_NOTICE_MS + 1;
        dbus_tick();
        DBCHECK(db_notice_of(other) && !db_notice_of(id), "after DBUS_NOTICE_MS the one of no time is kept and the one of a week is gone");

        // the records: the oldest gives way, told 1
        db_drain(a);
        db_drain(b);
        db_drain(c);
        {
                unsigned long ids[DBUS_NOTICES + 1];
                int kept = 1;

                for (int k = 0; k <= DBUS_NOTICES; k++)
                        ids[k] = db_notify(k & 1 ? b : c, "Many", 0, "One of many", "", -1, -1);
                for (int k = 1; k <= DBUS_NOTICES; k++)
                        kept &= db_notice_of(ids[k]) != 0;
                DBCHECK(kept && db_notices_kept() == DBUS_NOTICES && !db_notice_of(other), "%d notifications kept, every program's together: the oldest gives way",
                        DBUS_NOTICES);
                DBCHECK(db_closed(a, &reason) == other && reason == 1, "and its poster is told NotificationClosed(id, 1)");
        }

        // the rate: DBUS_NOTICE_BURST a client and DBUS_NOTICE_ALL the bus in DBUS_NOTICE_SPAN_MS
        db_time += DBUS_NOTICE_SPAN_MS;
        {
                int posted = 0, all = 0;
                long more[8];

                for (int k = 0; k < DBUS_NOTICE_BURST + 1; k++)
                        posted += db_notify(a, "Burst", 0, "Again", "", -1, -1) != 0;
                db_drain(a);
                DBCHECK(posted == DBUS_NOTICE_BURST, "a client posts %d in %d ms and is refused the next: LimitsExceeded (%d)", DBUS_NOTICE_BURST, DBUS_NOTICE_SPAN_MS, posted);
                db_time += DBUS_NOTICE_SPAN_MS;
                DBCHECK(db_notify(a, "Burst", 0, "Later", "", -1, -1) != 0, "and posts again once its span is over");
                db_time += DBUS_NOTICE_SPAN_MS;
                for (int k = 0; k < 8; k++)
                {
                        more[k] = db_connect(1000);
                        db_hello(more[k]);
                        db_drain(more[k]);
                }
                for (int k = 0; k < 8 * DBUS_NOTICE_BURST; k++)
                        all += db_notify(more[k % 8], "Many", 0, "From many", "", -1, -1) != 0;
                DBCHECK(all == DBUS_NOTICE_ALL, "and %d clients together post %d in a span, the bus's most (%d)", 8, DBUS_NOTICE_ALL, all);
                for (int k = 0; k < 8; k++)
                        dbus_drop(more[k]);
        }

        // the portal's
        db_time += DBUS_NOTICE_SPAN_MS;
        {
                struct db_call call;
                struct dbus_fields f = {DBUS_PATH_PORTAL, "org.freedesktop.portal.Notification", "AddNotification", "", DBUS_NAME_PORTAL, "", "sa{sv}", 0, 0};
                long length_at, first;
                const struct dbus_notice *p = 0;
                int same = 0;

                db_drain(a);
                for (int round = 0; round < 2; round++)
                {
                        db_begin(&call, DBUS_CALL, 0, &f);
                        dbus_o_string(&call.w, "upload-done");
                        dbus_o_u32(&call.w, 0);
                        length_at = call.w.length - 4;
                        dbus_o_pad(&call.w, 8);
                        first = call.w.length;
                        dbus_o_pad(&call.w, 8);
                        dbus_o_string(&call.w, "title");
                        dbus_o_sig(&call.w, "s");
                        dbus_o_string(&call.w, round ? "Done again" : "Done");
                        dbus_o_pad(&call.w, 8);
                        dbus_o_string(&call.w, "body");
                        dbus_o_sig(&call.w, "s");
                        dbus_o_string(&call.w, "a.txt");
                        dbus_o_pad(&call.w, 8);
                        dbus_o_string(&call.w, "priority");
                        dbus_o_sig(&call.w, "s");
                        dbus_o_string(&call.w, "urgent");
                        dbus_o_patch32(&call.w, length_at, (unsigned long)(call.w.length - first));
                        db_send(a, &call);
                        db_next(a, &s);
                }
                for (int k = 0; k < DBUS_NOTICES; k++)
                        if (db_notices[k].portal && !strcmp(db_notices[k].key, "upload-done"))
                                p = &db_notices[k], same++;
                DBCHECK(s.ok && s.m.type == DBUS_RETURN && same == 1 && !strcmp(p->text, "Done again - a.txt") && p->urgency == 2,
                        "the portal's AddNotification is a notice, the same id again replaces it, and urgent is critical");
                db_call(a, DBUS_NAME_PORTAL, DBUS_PATH_PORTAL, "org.freedesktop.portal.Notification", "RemoveNotification", "s", "upload-done", 0);
                db_next(a, &s);
                p = 0;
                for (int k = 0; k < DBUS_NOTICES; k++)
                        if (db_notices[k].portal)
                                p = &db_notices[k];
                DBCHECK(s.ok && s.m.type == DBUS_RETURN && !p && !db_closed(a, &reason), "and RemoveNotification takes it away, with no NotificationClosed");
                db_call(a, DBUS_NAME_PORTAL, DBUS_PATH_PORTAL, "org.freedesktop.DBus.Properties", "Get", "ss", "org.freedesktop.portal.Notification", "version");
                db_next(a, &s);
                DBCHECK(s.ok && s.m.type == DBUS_RETURN && dbus_get32(s.bytes + s.m.body_at + 4) == 1, "and the portal's Notification is of version 1");
        }

        // a client that goes leaves its notices to their time, and their end goes to nobody
        db_time += DBUS_NOTICE_SPAN_MS;
        id = db_notify(c, "Gone", 0, "Left behind", "", 2000, -1);
        dbus_drop(c);
        DBCHECK(db_notice_of(id) != 0, "a poster that goes leaves its notification");
        db_time += 2001;
        dbus_tick();
        DBCHECK(!db_notice_of(id), "and it expires all the same");
        dbus_drop(a);
        dbus_drop(b);
        DBCHECK(db_allocs == db_releases, "every record is returned (%ld %ld)", db_allocs, db_releases);
        dbus_stop();
        dbus_start(&db_host);
}

/*
        File descriptors passed (SCM_RIGHTS, 21dcc3d), every cap with a row: the descriptors a client may have waiting (DBUS_FDS), a
        message that names more than came with it, a callee that did not ask for them, a listener whose queue of them is full, and a
        client that goes with descriptors queued both ways. The bus is root's and holds what every client sends it, so each of them
        is closed exactly once, whatever path it took: the fake operations count every descriptor made and closed.
*/
#define DB_FDS 4096
static unsigned char db_fd_open[DB_FDS];
static int db_fd_next, db_fd_twice;

static int db_fd_new(void)
{
        int fd = db_fd_next++;

        db_fd_open[fd] = 1;
        return fd;
}

static int db_fd_dup(int fd)
{
        if (fd < 0 || fd >= DB_FDS || !db_fd_open[fd])
                return -1;
        return db_fd_new();
}

static void db_fd_close(int fd)
{
        if (fd < 0 || fd >= DB_FDS || !db_fd_open[fd])
                db_fd_twice++;
        else
                db_fd_open[fd] = 0;
}

static int db_fds_open(void)
{
        int n = 0;

        for (int k = 0; k < DB_FDS; k++)
                n += db_fd_open[k];
        return n;
}

// A client that agreed to descriptors: authenticated, NEGOTIATE_UNIX_FD, BEGIN, Hello.
static long db_connect_fds(void)
{
        long slot = dbus_connect(1000, 1000);
        char text[200];

        db_feed(slot, "\0AUTH EXTERNAL 31303030\r\nNEGOTIATE_UNIX_FD\r\nBEGIN\r\n", 1 + 24 + 19 + 7);
        db_take_text(slot, text, sizeof text);
        db_hello(slot);
        db_drain(slot);
        return strstr(text, "AGREE_UNIX_FD") ? slot : -1;
}

// A message carrying count descriptors (its UNIX_FDS field says `says`), the descriptors given to the bus first, as a read gives them.
static void db_send_fds(long slot, unsigned type, const char *dest, const char *member, int count, unsigned long says)
{
        struct db_call c;
        struct dbus_fields f = {"/o", "org.example.Fd", member, "", dest, "", says ? "h" : "", 0, 0};
        int fds[DBUS_FDS + 8];

        for (int k = 0; k < count; k++)
                fds[k] = db_fd_new();
        if (count)
                dbus_in_fds(slot, fds, count);
        c.w = (struct dbus_out){c.bytes, 0, sizeof c.bytes, 0};
        dbus_o_header_fds(&c.w, type, 0, ++db_serial, &f, says);
        c.body = c.w.length;
        if (says)
                dbus_o_u32(&c.w, 0);
        db_send(slot, &c);
}

static void db_test_fds(void)
{
        long callee, caller, plain, listener[3];
        const unsigned char *bytes;
        const int *fds;
        long n, nfds;
        struct db_seen s;

        db_reset();
        memset(db_fd_open, 0, sizeof db_fd_open);
        db_fd_next = 3, db_fd_twice = 0;
        dbus_set_fd_ops(db_fd_dup, db_fd_close);
        callee = db_connect_fds();
        caller = db_connect_fds();
        DBCHECK(callee >= 0 && caller >= 0, "with descriptor operations the bus agrees to NEGOTIATE_UNIX_FD");
        db_request(callee, "org.example.Fds", 0);
        db_drain(callee);

        // a call with two, to a callee that asked for them: queued with the call, given with it, closed once sent
        db_send_fds(caller, DBUS_CALL, "org.example.Fds", "Take", 2, 2);
        n = dbus_out_fds(callee, &bytes, &fds, &nfds);
        DBCHECK(n > 0 && nfds == 2 && db_fds_open() == 2, "a call with two descriptors reaches its callee with its two (%ld, %d open)", nfds, db_fds_open());
        dbus_sent_fds(callee);
        dbus_sent(callee, n);
        DBCHECK(db_fds_open() == 0 && !db_fd_twice, "and the bus closes its own once they are sent (%d open)", db_fds_open());

        // more than DBUS_FDS waiting: the client is ended and every one closed
        {
                int many[DBUS_FDS + 1];
                long greedy = db_connect_fds();

                for (int k = 0; k <= DBUS_FDS; k++)
                        many[k] = db_fd_new();
                dbus_in_fds(greedy, many, DBUS_FDS + 1);
                DBCHECK(dbus_dead(greedy) && db_fds_open() == 0, "a client that sends %d descriptors at once is ended and they are closed (%d open)", DBUS_FDS + 1,
                        db_fds_open());
                dbus_drop(greedy);
        }

        // a message that says more than came with it ends its sender; the ones that came are closed with it
        {
                long liar = db_connect_fds();

                db_send_fds(liar, DBUS_CALL, "org.example.Fds", "Take", 1, 2);
                DBCHECK(dbus_dead(liar), "a message that names two descriptors with one sent ends its sender");
                dbus_drop(liar);
                DBCHECK(db_fds_open() == 0, "and the one is closed (%d open)", db_fds_open());
        }

        // a callee that did not ask for descriptors: the call is an error to its caller and they are closed
        plain = db_connect(1000);
        db_hello(plain);
        db_request(plain, "org.example.Plain", 0);
        db_drain(plain);
        db_drain(caller);
        db_send_fds(caller, DBUS_CALL, "org.example.Plain", "Take", 1, 1);
        db_next(caller, &s);
        DBCHECK(s.ok && s.m.type == DBUS_ERROR && !strcmp(s.m.error, "org.freedesktop.DBus.Error.InvalidArgs") && !db_queued(plain) && db_fds_open() == 0,
                "a call with a descriptor to a callee that did not ask for them is InvalidArgs to the caller (not a full queue's error), and it is closed (%d open)",
                db_fds_open());

        // a signal with one to three listeners: a copy each, the one sent closed; a listener whose queue of them is full is passed by
        for (int k = 0; k < 3; k++)
        {
                listener[k] = db_connect_fds();
                db_rule(listener[k], "type='signal',interface='org.example.Fd'");
                db_drain(listener[k]);
        }
        db_send_fds(caller, DBUS_SIGNAL, "", "Shared", 1, 1);
        DBCHECK(db_fds_open() == 3, "a signal with a descriptor to three listeners is three copies, the one sent closed (%d open)", db_fds_open());
        for (int k = 0; k < DBUS_FDS; k++)
                db_send_fds(caller, DBUS_SIGNAL, "", "Shared", 1, 1);
        DBCHECK(db_fds_open() == 3 * DBUS_FDS && !dbus_dead(listener[0]),
                "past %d waiting on a listener the next copy is not queued and is closed; no listener is ended for it (%d open)", DBUS_FDS, db_fds_open());

        // clients that go with descriptors queued: every one closed, none twice
        for (int k = 0; k < 3; k++)
                dbus_drop(listener[k]);
        dbus_drop(callee);
        dbus_drop(caller);
        dbus_drop(plain);
        DBCHECK(db_fds_open() == 0 && !db_fd_twice, "clients that go take their queued descriptors with them: %d open, %d closed twice", db_fds_open(), db_fd_twice);
        DBCHECK(db_allocs == db_releases, "every record is returned (%ld %ld)", db_allocs, db_releases);
        dbus_set_fd_ops(0, 0);
}

int main(int argc, char **argv)
{
        long rounds = argc > 1 ? atol(argv[1]) : 20000, grammar = argc > 2 ? atol(argv[2]) : 20000;

        if (argc > 3)
                db_seed = strtoul(argv[3], 0, 0) | 1;
        db_test_requests();
        db_test_chooser();
        db_test_deadlines();
        db_test_kept();
        db_test_notifications();
        db_test_fds();
        db_test_auth();
        db_test_hello_and_names();
        db_test_routing();
        db_test_portal();
        db_test_hostile();
        db_test_caps();
        db_test_queue();
        db_test_pending();
        db_test_flood();
        db_test_reserved();
        db_test_allowed();
        db_test_methods();
        db_test_names_room();
        db_test_read_all();
        db_test_trace();
        db_test_trace_room();
        db_test_fuzz(rounds);
        db_test_structured(grammar);
        db_test_grammar_fuzz(grammar);
        printf("CHECK_dbus: %u checks, %u failures\n", db_checks, db_failures);
        return db_failures != 0;
}
#endif /* CHECK_dbus */

