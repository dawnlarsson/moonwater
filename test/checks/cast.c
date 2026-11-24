/*
        test/checks/cast.c: the capture engine, the PipeWire of a screen cast (src/cast/pipewire.c) and the rings' rule
        (src/cast/cast.h).

        A run of test/checks.c, which includes this file where the run stood: it is compiled only as part of that file, with
        its statics and macros, and never on its own.
*/

#ifdef CHECK_cast
/*
        CHECK_cast: the PipeWire server against a client that speaks its wire, built with the server's own pod writer (a fault
        in it would show in the judge too, which the end-to-end run against libpipewire is for), and a fake host; then the hostile
        messages, and the slots of a ring between a writer and a sink.

            cc -std=gnu11 -O2 -Wall -Wextra -DCHECK_cast test/checks.c
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../src/cast/pipewire.c"

static unsigned cn_checks, cn_failures;

#define CNCHECK(condition, ...)                                                                                       \
        do                                                                                                            \
        {                                                                                                             \
                cn_checks++;                                                                                          \
                if (!(condition))                                                                                     \
                {                                                                                                     \
                        cn_failures++;                                                                                \
                        printf("  FAIL: ");                                                                           \
                        printf(__VA_ARGS__);                                                                          \
                        printf(" (line %d)\n", __LINE__);                                                             \
                }                                                                                                     \
        } while (0)

static long long cn_time = 1000000000ll;
static int cn_fds = 100, cn_signalled = -1, cn_closed;

static void *cn_alloc(void *context, long bytes) { (void)context; return calloc(1, (size_t)bytes); }
static void cn_release(void *context, void *block, long bytes) { (void)context, (void)bytes; free(block); }
static long long cn_now(void *context) { (void)context; return cn_time; }
static int cn_eventfd(void *context) { (void)context; return cn_fds++; }
static void cn_signal(void *context, int fd) { (void)context; cn_signalled = fd; }
static int cn_memfd(void *context, long bytes, void **mapped) { (void)context; *mapped = calloc(1, (size_t)bytes); return cn_fds++; }
static void cn_unmap(void *context, void *mapped, long bytes) { (void)context, (void)bytes; free(mapped); }
static void cn_close(void *context, int fd) { (void)context, (void)fd; cn_closed++; }
static unsigned cn_cookie(void *context) { (void)context; return 7; }
static const struct pwire_host cn_host = {0, cn_alloc, cn_release, cn_now, cn_eventfd, cn_signal, cn_memfd, cn_unmap, cn_close, cn_cookie};

// The client's writer: a connection of its own whose output is what the client sends.
static struct pwire_connection cn_writer;

static long cn_begin(struct pwire_out *o, unsigned id, unsigned opcode)
{
        return pwire_o_message(o, &cn_writer, id, opcode);
}

// What the client wrote, fed to the server; the server's answer, and 0 or -1 as pwire_read says.
static int cn_send(long slot, struct pwire_out *o, long body)
{
        int fed;

        pwire_o_send(o, body);
        fed = pwire_read(slot, cn_writer.out, cn_writer.out_length, 0, 0);
        cn_writer.out_length = cn_writer.out_fds = 0;
        return fed;
}

// The next message the server sent: its object, opcode, body and the descriptors it carries; 0 when there is none.
struct cn_seen
{
        unsigned id, opcode, fds;
        struct pwire_pod pod;
        struct pwire_members m;
        unsigned char bytes[65536];
};

static int cn_next(long slot, struct cn_seen *s)
{
        const unsigned char *bytes;
        const int *fds;
        long nfds, n = pwire_pending(slot, &bytes, &fds, &nfds), size, at = 0;

        if (n < PWIRE_HEADER)
                return 0;
        size = (long)(pwire_get32(bytes + 4) & 0xffffff);
        if (PWIRE_HEADER + size > n || size > (long)sizeof s->bytes)
                return 0;
        s->id = pwire_get32(bytes), s->opcode = pwire_get32(bytes + 4) >> 24, s->fds = pwire_get32(bytes + 12);
        memcpy(s->bytes, bytes + PWIRE_HEADER, (size_t)size);
        pwire_wrote(slot, PWIRE_HEADER + size);
        return pwire_p_next(s->bytes, size, &at, &s->pod) == 0 && pwire_p_struct(&s->pod, &s->m) == 0;
}

// Takes messages until one of object id and opcode, or there are none.
static int cn_find(long slot, struct cn_seen *s, unsigned id, unsigned opcode)
{
        for (int k = 0; k < 64; k++)
        {
                if (!cn_next(slot, s))
                        return 0;
                if (s->id == id && s->opcode == opcode)
                        return 1;
        }
        return 0;
}

static void cn_drain(long slot)
{
        struct cn_seen s;

        while (cn_next(slot, &s))
                ;
}

static const struct pwire_source cn_source = {640, 480, 2560, 60, 4, 2560 * 480, 0, 9, 102, "check"};

static long cn_connected(void)
{
        long slot = pwire_open(&cn_source);
        struct pwire_out o;
        long body = cn_begin(&o, 0, 1);

        pwire_o_int(&o, PWIRE_INT, 4);
        cn_send(slot, &o, body);
        return slot;
}

// An EnumFormat of video/raw in one format, its size a range from 1x1 (or the size given exactly), and a modifier when asked.
static void cn_enum_format(struct pwire_out *o, unsigned video, unsigned width, unsigned height, int modifier)
{
        long object = pwire_o_object(o, PWIRE_OBJECT_FORMAT, PWIRE_PARAM_ENUM_FORMAT), choice;
        unsigned char *p;

        pwire_o_prop(o, PWIRE_FORMAT_MEDIA_TYPE), pwire_o_int(o, PWIRE_ID, PWIRE_MEDIA_VIDEO);
        pwire_o_prop(o, PWIRE_FORMAT_MEDIA_SUBTYPE), pwire_o_int(o, PWIRE_ID, PWIRE_SUBTYPE_RAW);
        pwire_o_prop(o, PWIRE_FORMAT_VIDEO), pwire_o_int(o, PWIRE_ID, video);
        if (modifier)
                pwire_o_prop(o, PWIRE_FORMAT_MODIFIER), pwire_o_long(o, 0);
        pwire_o_prop(o, PWIRE_FORMAT_SIZE);
        if (width)
                pwire_o_pair(o, PWIRE_RECTANGLE, width, height);
        else
        {
                // a Choice Range of rectangles: the default, the least and the most
                choice = pwire_o_begin(o, PWIRE_CHOICE);
                p = pwire_o_room(o, 8 + 24);
                pwire_put32(p, PWIRE_CHOICE_RANGE), pwire_put32(p + 4, 0), pwire_put32(p + 8, 8), pwire_put32(p + 12, PWIRE_RECTANGLE);
                pwire_put32(p + 16, 1), pwire_put32(p + 20, 1), pwire_put32(p + 24, 1), pwire_put32(p + 28, 1);
                p = pwire_o_room(o, 8);
                pwire_put32(p, 0xffffffffu), pwire_put32(p + 4, 0xffffffffu);
                pwire_o_end(o, choice);
        }
        pwire_o_end(o, object);
}

static void cn_port_update(long slot, int formats, int buffers, unsigned data_types)
{
        struct pwire_out o;
        long body = cn_begin(&o, 2, 3), object;

        pwire_o_int(&o, PWIRE_INT, 0), pwire_o_int(&o, PWIRE_INT, 0), pwire_o_int(&o, PWIRE_INT, 1);
        pwire_o_int(&o, PWIRE_INT, (unsigned)(formats ? 2 : 0) + (unsigned)(buffers ? 3 : 0));
        if (formats)
        {
                cn_enum_format(&o, PWIRE_VIDEO_BGRA, 0, 0, 1); // the card's buffer, which is not offered
                cn_enum_format(&o, PWIRE_VIDEO_BGRX, 0, 0, 0);
        }
        if (buffers)
        {
                object = pwire_o_object(&o, PWIRE_OBJECT_BUFFERS, PWIRE_PARAM_BUFFERS);
                pwire_o_prop(&o, PWIRE_BUFFERS_BUFFERS), pwire_o_int(&o, PWIRE_INT, 8);
                pwire_o_prop(&o, PWIRE_BUFFERS_DATA_TYPE), pwire_o_int(&o, PWIRE_INT, data_types);
                pwire_o_end(&o, object);
                object = pwire_o_object(&o, PWIRE_OBJECT_META, PWIRE_PARAM_META);
                pwire_o_prop(&o, PWIRE_META_TYPE), pwire_o_int(&o, PWIRE_ID, PWIRE_META_HEADER);
                pwire_o_prop(&o, PWIRE_META_SIZE), pwire_o_int(&o, PWIRE_INT, 32);
                pwire_o_end(&o, object);
                object = pwire_o_object(&o, PWIRE_OBJECT_META, PWIRE_PARAM_META);
                pwire_o_prop(&o, PWIRE_META_TYPE), pwire_o_int(&o, PWIRE_ID, PWIRE_META_DAMAGE);
                pwire_o_prop(&o, PWIRE_META_SIZE), pwire_o_int(&o, PWIRE_INT, 16 * 4);
                pwire_o_end(&o, object);
        }
        pwire_o_pod(&o, PWIRE_NONE, "", 0);
        cn_send(slot, &o, body);
}

// A connection that is streaming: hello, the node, its port, active, and its buffers.
static long cn_streaming(void)
{
        long slot = cn_connected();
        struct pwire_out o;
        long body = cn_begin(&o, 0, 6);
        static const char *const none[1] = {0};

        pwire_o_string(&o, "client-node"), pwire_o_string(&o, "PipeWire:Interface:ClientNode"), pwire_o_int(&o, PWIRE_INT, 6);
        pwire_o_dict(&o, none, 0);
        pwire_o_int(&o, PWIRE_INT, 2);
        cn_send(slot, &o, body);
        cn_port_update(slot, 1, 0, 0);
        body = cn_begin(&o, 2, 4);
        pwire_o_int(&o, PWIRE_BOOL, 1);
        cn_send(slot, &o, body);
        cn_port_update(slot, 1, 1, 1u << PWIRE_DATA_MEMFD);
        return slot;
}

static void cn_test_core(void)
{
        struct cn_seen s;
        struct pwire_out o;
        long slot = cn_connected(), body, n;
        const char *text;

        CNCHECK(cn_next(slot, &s) && s.id == 0 && s.opcode == 0, "Hello is answered with the core's info");
        CNCHECK(pwire_p_int(&s.m) == 0 && pwire_p_int(&s.m) == 7 && (text = pwire_p_string(&s.m, &n)) && !strcmp(text, "moonwater"),
                "the info's id, cookie and user");
        CNCHECK(cn_find(slot, &s, 0, 5) && pwire_p_int(&s.m) == 1, "the client's own object is bound");
        cn_drain(slot);
        body = cn_begin(&o, 0, 2);
        pwire_o_int(&o, PWIRE_INT, 0), pwire_o_int(&o, PWIRE_INT, 0x40000002u);
        CNCHECK(cn_send(slot, &o, body) == 0, "Sync is taken");
        CNCHECK(cn_next(slot, &s) && s.id == 0 && s.opcode == 1 && pwire_p_int(&s.m) == 0 && pwire_p_int(&s.m) == 0x40000002u,
                "Sync is answered with Done of its seq");
        body = cn_begin(&o, 0, 5);
        pwire_o_int(&o, PWIRE_INT, 3), pwire_o_int(&o, PWIRE_INT, 3);
        cn_send(slot, &o, body);
        CNCHECK(cn_next(slot, &s) && s.id == 3 && s.opcode == 0 && pwire_p_int(&s.m) == 102, "the registry has the source's node, and nothing else");
        CNCHECK(!cn_next(slot, &s), "one global");
        body = cn_begin(&o, 3, 1);
        pwire_o_int(&o, PWIRE_INT, 102), pwire_o_string(&o, "PipeWire:Interface:Node"), pwire_o_int(&o, PWIRE_INT, 3), pwire_o_int(&o, PWIRE_INT, 4);
        cn_send(slot, &o, body);
        CNCHECK(cn_next(slot, &s) && s.id == 0 && s.opcode == 3 && pwire_p_int(&s.m) == 4, "a Bind is an error: nothing is bound");
        body = cn_begin(&o, 0, 6);
        {
                static const char *const none[1] = {0};

                pwire_o_string(&o, "link-factory"), pwire_o_string(&o, "PipeWire:Interface:Link"), pwire_o_int(&o, PWIRE_INT, 3);
                pwire_o_dict(&o, none, 0);
                pwire_o_int(&o, PWIRE_INT, 5);
        }
        cn_send(slot, &o, body);
        CNCHECK(cn_find(slot, &s, 0, 3) && pwire_p_int(&s.m) == 5, "an object of another factory is refused");
        CNCHECK(!pwire_dead(slot), "and the connection lives");
        pwire_close(slot);
}

static void cn_test_stream(void)
{
        struct cn_seen s;
        long slot = cn_streaming();
        unsigned found = 0, video = 0, width = 0, n_buffers = 0, mem = 0, flags = 0, mapoffset[4] = {0}, start = 0, io = 0;
        struct pwire_connection *c;

        while (cn_next(slot, &s))
        {
                if (s.id == 0 && s.opcode == 6) // AddMem
                        found |= 1;
                if (s.id == 2 && s.opcode == 0 && s.fds == 2) // Transport
                        found |= 2;
                if (s.id == 2 && s.opcode == 11)
                        found |= 4;
                if (s.id == 2 && s.opcode == 7) // PortSetParam: the format
                {
                        struct pwire_pod param;
                        struct pwire_props props;
                        struct pwire_pod value;
                        unsigned key;

                        pwire_p_int(&s.m), pwire_p_int(&s.m), pwire_p_int(&s.m), pwire_p_int(&s.m);
                        param = pwire_p_member(&s.m);
                        if (pwire_p_object(&param, &props) == 0 && props.id == PWIRE_PARAM_FORMAT)
                                while (pwire_p_prop(&props, &key, &value) > 0)
                                {
                                        if (key == PWIRE_FORMAT_VIDEO)
                                                video = pwire_get32(value.body);
                                        if (key == PWIRE_FORMAT_SIZE)
                                                width = pwire_get32(value.body);
                                }
                }
                if (s.id == 2 && s.opcode == 8) // PortUseBuffers
                {
                        pwire_p_int(&s.m), pwire_p_int(&s.m), pwire_p_int(&s.m), pwire_p_int(&s.m);
                        n_buffers = pwire_p_int(&s.m);
                        for (unsigned b = 0; b < n_buffers && b < 4; b++)
                        {
                                unsigned metas;

                                mem = pwire_p_int(&s.m), pwire_p_int(&s.m), pwire_p_int(&s.m);
                                metas = pwire_p_int(&s.m);
                                for (unsigned k = 0; k < metas; k++)
                                        pwire_p_int(&s.m), pwire_p_int(&s.m);
                                pwire_p_int(&s.m), pwire_p_int(&s.m), pwire_p_int(&s.m);
                                flags = pwire_p_int(&s.m), mapoffset[b] = pwire_p_int(&s.m), pwire_p_int(&s.m);
                        }
                }
                if (s.id == 2 && s.opcode == 9)
                        io = 1;
                if (s.id == 2 && s.opcode == 4)
                        start = 1;
        }
        CNCHECK((found & 7) == 7, "the node's memory, its transport and its mix are given");
        CNCHECK(video == PWIRE_VIDEO_BGRX && width == 640, "the format is the one EnumFormat the screen can be (no modifier), of the source's size");
        CNCHECK(n_buffers == 4 && mem == 2 && flags == 9, "a buffer a slot, its metadata in the session's memory, its frame readable only");
        CNCHECK(mapoffset[1] == 2560 * 480 && mapoffset[3] == 3 * 2560 * 480, "each buffer its slot of the ring");
        CNCHECK(io && start, "the io area and Start");
        c = pwire_of(slot);
        CNCHECK(pwire_ready(slot, cn_time), "streaming, it is ready for a frame");
        {
                const int damage[1][4] = {{10, 20, 30, 40}};

                CNCHECK(pwire_frame(slot, 0, 5555, 1, damage, 1) == 1 && cn_signalled == c->trigger_fd, "a frame is told and its eventfd written");
                CNCHECK(pwire_get32(c->mem + c->io_at) == PWIRE_STATUS_HAVE_DATA && pwire_get32(c->mem + c->io_at + 4) == 0,
                        "the io area has the buffer");
                CNCHECK(pwire_get32(c->mem + c->act_at) == PWIRE_ACT_TRIGGERED, "the client's activation is triggered");
                CNCHECK(pwire_get32(c->meta + 8 + 8) == 5555 && pwire_get32(c->meta + 8 + 24) == 1, "the header has its stamp and its number (after Busy)");
                CNCHECK(pwire_get32(c->meta + 40) == 10 && pwire_get32(c->meta + 52) == 40, "the damage meta has its rectangle");
                CNCHECK(!pwire_ready(slot, cn_time + 100000000ll), "not ready while the client has not run");
                CNCHECK(pwire_frame(slot, 1, 6666, 2, 0, 0) == 0, "and a frame then is let go");
                // the client runs: its activation finished, and its stream counts the buffer its program has in the Busy meta
                pwire_put32(c->mem + c->act_at, PWIRE_ACT_FINISHED);
                pwire_put32(c->mem + c->io_at, PWIRE_STATUS_NEED_DATA);
                pwire_put32(c->meta + 4, 1);
                CNCHECK(pwire_held(slot) == 1, "it holds the buffer it was told");
                cn_time += 20000000ll;
                CNCHECK(pwire_frame(slot, 1, 7777, 3, 0, 0) == 1, "the next frame is told");
                pwire_put32(c->mem + c->act_at, PWIRE_ACT_FINISHED);
                CNCHECK(pwire_held(slot) == 3, "a buffer its program has not given back stays held");
                pwire_put32(c->meta + 4, 0);
                CNCHECK(pwire_held(slot) == 2, "given back (its count down), it is the source's again; the newest stays held");
                pwire_put32(c->meta + c->meta_size + 4, 0);
                CNCHECK(pwire_held(slot) == 2, "the newest told is held whatever its count says (the client's to write)");
                CNCHECK(pwire_get32(c->meta + c->meta_size + 40) == 0 && pwire_get32(c->meta + c->meta_size + 48) == 640,
                        "a frame told without damage says the whole frame changed");
        }
        CNCHECK(pwire_frame(slot, 4, 1, 4, 0, 0) == -1, "a buffer past the source's slots is not one");
        pwire_close(slot);
}

static void cn_test_refusals(void)
{
        struct cn_seen s;
        long slot = cn_streaming();
        struct pwire_connection *c = pwire_of(slot);

        pwire_close(slot);
        (void)c;
        // buffers only on the card: refused
        slot = cn_connected();
        {
                struct pwire_out o;
                long body = cn_begin(&o, 0, 6);
                static const char *const none[1] = {0};

                pwire_o_string(&o, "client-node"), pwire_o_string(&o, "PipeWire:Interface:ClientNode"), pwire_o_int(&o, PWIRE_INT, 6);
                pwire_o_dict(&o, none, 0);
                pwire_o_int(&o, PWIRE_INT, 2);
                cn_send(slot, &o, body);
                cn_port_update(slot, 1, 0, 0);
                body = cn_begin(&o, 2, 4);
                pwire_o_int(&o, PWIRE_BOOL, 1);
                cn_send(slot, &o, body);
                cn_drain(slot);
                cn_port_update(slot, 1, 1, 1u << 3);
                CNCHECK(cn_find(slot, &s, 0, 3), "buffers of the card's alone (DmaBuf) are an error");
                CNCHECK(!pwire_of(slot)->started, "and nothing streams");
                body = cn_begin(&o, 0, 6);
                pwire_o_string(&o, "client-node"), pwire_o_string(&o, "PipeWire:Interface:ClientNode"), pwire_o_int(&o, PWIRE_INT, 6);
                pwire_o_dict(&o, none, 0);
                pwire_o_int(&o, PWIRE_INT, 4);
                cn_send(slot, &o, body);
                CNCHECK(cn_find(slot, &s, 0, 3) && pwire_p_int(&s.m) == 4, "a second client node is refused");
        }
        pwire_close(slot);
}

// Bytes that are not messages: each ends its connection, and none is read past its bounds.
static void cn_test_hostile(void)
{
        unsigned char m[64];
        long slot;

        slot = pwire_open(&cn_source);
        memset(m, 0, sizeof m);
        pwire_put32(m, 0), pwire_put32(m + 4, 2u << 24 | 16), pwire_put32(m + 16, 8), pwire_put32(m + 20, PWIRE_STRUCT);
        CNCHECK(pwire_read(slot, m, 32, 0, 0) < 0 && pwire_dead(slot), "a message before Hello ends the connection");
        pwire_close(slot);

        slot = cn_connected();
        cn_drain(slot);
        pwire_put32(m, 0), pwire_put32(m + 4, 2u << 24 | 16), pwire_put32(m + 16, 4000), pwire_put32(m + 20, PWIRE_STRUCT);
        CNCHECK(pwire_read(slot, m, 32, 0, 0) < 0, "a struct longer than its message ends the connection");
        pwire_close(slot);

        slot = cn_connected();
        pwire_put32(m, 0), pwire_put32(m + 4, 2u << 24 | 16), pwire_put32(m + 12, 1), pwire_put32(m + 16, 8), pwire_put32(m + 20, PWIRE_STRUCT);
        {
                int fd = 55;

                cn_closed = 0;
                CNCHECK(pwire_read(slot, m, 32, &fd, 1) < 0 && cn_closed == 1, "a message with descriptors ends it, and they are closed");
        }
        pwire_close(slot);

        slot = cn_connected();
        memset(m, 0, sizeof m);
        pwire_put32(m, 9), pwire_put32(m + 4, 2u << 24 | 16), pwire_put32(m + 16, 8), pwire_put32(m + 20, PWIRE_STRUCT);
        CNCHECK(pwire_read(slot, m, 32, 0, 0) < 0, "a message to an object it does not have ends it");
        pwire_close(slot);

        // a message in pieces of a byte: read whole, then answered
        slot = cn_connected();
        cn_drain(slot);
        {
                struct pwire_out o;
                struct cn_seen s;
                long body = cn_begin(&o, 0, 2);
                int fed = 0;

                pwire_o_int(&o, PWIRE_INT, 0), pwire_o_int(&o, PWIRE_INT, 99);
                pwire_o_send(&o, body);
                for (long k = 0; k < cn_writer.out_length; k++)
                        fed |= pwire_read(slot, cn_writer.out + k, 1, 0, 0);
                cn_writer.out_length = 0;
                CNCHECK(fed == 0 && cn_next(slot, &s) && s.opcode == 1 && (pwire_p_int(&s.m), pwire_p_int(&s.m)) == 99, "a message a byte at a time is read whole");
        }
        pwire_close(slot);

        // a source that is not one is refused
        {
                struct pwire_source bad = cn_source;

                bad.slot_bytes = 100;
                CNCHECK(pwire_open(&bad) < 0, "a ring whose slots do not hold a frame is refused");
                bad = cn_source, bad.slots = PWIRE_BUFFERS_MOST + 1;
                CNCHECK(pwire_open(&bad) < 0, "more slots than a port has buffers are refused");
        }

        // random bytes after Hello: never more than the connection's end
        for (int round = 0; round < 2000; round++)
        {
                unsigned char junk[256];
                unsigned seed = (unsigned)round * 2654435761u;
                long n = 16 + (long)(seed % 200);

                slot = cn_connected();
                for (long k = 0; k < n; k++)
                        seed = seed * 1103515245u + 12345u, junk[k] = (unsigned char)(seed >> 16);
                pwire_put32(junk + 4, (pwire_get32(junk + 4) & 0xff000000u) | (unsigned)(n - 16));
                pwire_put32(junk + 12, 0);
                pwire_put32(junk, seed % 3);
                pwire_read(slot, junk, n, 0, 0);
                pwire_close(slot);
        }
        CNCHECK(1, "random messages are read without a fault");
}

/* ---- the rings' rule (src/cast/cast.h), on a row of its own ---- */

#define WINDOWS_CAST_SLOTS 4
#define WINDOWS_CAST_DAMAGE 16
struct windows_cast
{
        unsigned int slots, latest, frames;
        unsigned int seq[WINDOWS_CAST_SLOTS], held[WINDOWS_CAST_SLOTS], damages[WINDOWS_CAST_SLOTS];
        unsigned long long stamp[WINDOWS_CAST_SLOTS];
        int damage[WINDOWS_CAST_SLOTS][WINDOWS_CAST_DAMAGE][4];
};
#include "../../src/cast/cast.h"

static void cn_test_ring(void)
{
        struct windows_cast c;
        unsigned seq;
        int k, taken;
        const int rect[1][4] = {{1, 2, 3, 4}};

        memset(&c, 0, sizeof c);
        c.slots = 4, c.latest = ~0u;
        CNCHECK(cast_take(&c, &seq) < 0, "nothing to take before a frame");
        k = cast_write_begin(&c);
        CNCHECK(k == 0 && (c.seq[0] & 1), "the first slot is written, its seq odd");
        CNCHECK(cast_take(&c, &seq) < 0, "a slot being written is not the newest yet");
        cast_write_end(&c, (unsigned)k, 42, rect, 1);
        CNCHECK(c.latest == 0 && !(c.seq[0] & 1) && c.frames == 1 && c.damages[0] == 1 && c.stamp[0] == 42, "published: even, newest, stamped");
        taken = cast_take(&c, &seq);
        CNCHECK(taken == 0 && c.held[0] == 1, "a sink takes the newest and holds it");
        k = cast_write_begin(&c);
        CNCHECK(k == 1, "the writer skips the held and the newest");
        cast_write_end(&c, (unsigned)k, 43, 0, WINDOWS_CAST_DAMAGE + 1);
        CNCHECK(c.damages[1] == 0, "more damage than a slot keeps is the whole frame");
        k = cast_write_begin(&c);
        CNCHECK(k == 2, "the newest is not written while a sink may take it");
        cast_write_end(&c, (unsigned)k, 44, 0, 0);
        taken = cast_take(&c, &seq);
        k = cast_write_begin(&c);
        CNCHECK(taken == 2 && k == 1, "slot 0 and 2 held, 2 the newest: the writer has 1");
        cast_write_end(&c, (unsigned)k, 45, 0, 0);
        k = cast_write_begin(&c);
        CNCHECK(k == 3, "and then 3");
        cast_write_end(&c, (unsigned)k, 46, 0, 0);
        CNCHECK(cast_take(&c, &seq) == 3 && cast_write_begin(&c) == 1, "3 taken: 1 is the one left");
        cast_write_end(&c, 1, 47, 0, 0);
        CNCHECK(cast_take(&c, &seq) == 1 && cast_write_begin(&c) < 0, "with every slot held there is none to write");
        cast_give(&c, 0);
        cast_give(&c, 0);
        CNCHECK(c.held[0] == 0, "a slot let go more often than held stays let go");
}

int main(void)
{
        pwire_start(&cn_host);
        cn_test_core();
        cn_test_stream();
        cn_test_refusals();
        cn_test_hostile();
        cn_test_ring();
        printf("CHECK_cast: %u checks, %u failures\n", cn_checks, cn_failures);
        return cn_failures != 0;
}
#endif /* CHECK_cast */
