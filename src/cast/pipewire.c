/*
        Moonwater's PipeWire: the server half of PipeWire's native protocol, as much of it as a program that records the
        screen asks for, with one video source behind each connection.

        WHY

        A browser and an Electron program (Chromium's WebRTC, and so Google Meet and Discord) and OBS record the screen
        on Wayland one way only: they ask the ScreenCast portal for a session (src/dbus/screencast.c), the person picks
        what to share, and OpenPipeWireRemote hands them a socket on which they speak PipeWire's native protocol through
        their own libpipewire: a stream of video/raw frames in memory they map. There is no PipeWire on Moonwater, and
        there need not be one: the protocol a client of a screen cast speaks is a small part of PipeWire's (the core,
        the client, an optional registry, and the client node: a node the client makes in its own process with one input
        port, whose format, buffers, io area and activation the server gives it), and every connection here is made by
        the portal for one session, so this server is the session manager too: the client's node is linked to the
        session's source and to nothing else, whatever node it names. The frames are the capture engine's
        (src/cast/cast.h): the compositor composes them into a ring of slots in a memfd that only it can write (sealed),
        and a slot is a buffer here; this file only says to the client which slot holds the newest frame, when, and
        what changed in it.

        WHAT IT IS

        One connection is one client (pwire_open, with the source it may see), driven by its host (the session bus's
        process): bytes and descriptors read from the socket go in (pwire_read), bytes and descriptors to send come out
        (pwire_pending, pwire_wrote), and a frame of the source is told to the client (pwire_frame) when the ring has a
        new one. The wire is the version 3 header (id, opcode and size, seq, the count of descriptors) and SPA pods; the
        graph is the shape the real server makes (a trace of pipewire 1.6.11 with an xdg-desktop-portal-wlr-shaped
        producer was its reference): the client node's activation and the source's (the driver's) in one memfd of the
        session's own with the io area and the buffers' metadata, the mix made and the format set once the node is
        active, the buffers given once the client says what it wants of them, then the activation of the driver, the io
        and Start. A frame is the io area's buffer id and status, the client's activation set triggered, and a write to
        its eventfd; the client's own libpipewire reads the frame, and tells the driver's activation (and its eventfd,
        pwire_wake_fd) when it has, giving the buffer before back in the io area.

        THREAT MODEL

        A connection is any program a person runs in a bowl, and everything it sends is hostile: every length, every pod
        and its nesting, the ids, the order of calls; and so is everything it can write to: the memory of the session
        (both activations, the io area, the metadata) is mapped by the client read-write, so nothing read back from it is
        trusted beyond a buffer id checked against the count. Each pod is bounded by its parent before it is read; a
        message that is not what its method takes ends the connection. The frames are never the client's to write: the
        ring's memfd is sealed against writable mappings by the compositor before it is given, and given here as
        readable only. A client sees one source, its session's, with no registry of anything else; it cannot make any
        object but the one node, cannot bind, and cannot send descriptors (any sent are closed). Its input is held to
        one message of the protocol's largest (its 24-bit size), its output grows only with what it asked for.

        It is freestanding C with no allocator of its own, the host behind struct pwire_host, so that the checks can put
        a fake one behind it (test/checks/cast.c).
*/
#ifndef PWIRE_INCLUDED
#define PWIRE_INCLUDED

#define PWIRE_API static __attribute__((unused))

#define PWIRE_HEADER 16
#define PWIRE_MESSAGE_MOST (16l << 20)  // the header's 24-bit size: the largest message there can be
#define PWIRE_FDS_MOST 28              // descriptors one message may carry (libpipewire's own bound)
#define PWIRE_BUFFERS_MOST 64          // buffers of one port (libpipewire's MAX_BUFFERS)
#define PWIRE_DAMAGE_MOST 64           // damage rectangles told in one frame's VideoDamage
#define PWIRE_DEPTH 16                 // how deep the pods of a message may nest
#define PWIRE_ACT 2312                 // sizeof(struct pw_node_activation), 1.6.11
#define PWIRE_PAGE 4096

// SPA's pod types, and the ids this server reads and writes
enum
{
        PWIRE_NONE = 1, PWIRE_BOOL, PWIRE_ID, PWIRE_INT, PWIRE_LONG, PWIRE_FLOAT, PWIRE_DOUBLE, PWIRE_STRING, PWIRE_BYTES,
        PWIRE_RECTANGLE, PWIRE_FRACTION, PWIRE_BITMAP, PWIRE_ARRAY, PWIRE_STRUCT, PWIRE_OBJECT, PWIRE_SEQUENCE, PWIRE_POINTER,
        PWIRE_FD, PWIRE_CHOICE, PWIRE_POD
};
enum
{
        PWIRE_CHOICE_NONE = 0, PWIRE_CHOICE_RANGE = 1, PWIRE_CHOICE_STEP = 2, PWIRE_CHOICE_ENUM = 3, PWIRE_CHOICE_FLAGS = 4,
        PWIRE_OBJECT_FORMAT = 0x40003, PWIRE_OBJECT_BUFFERS = 0x40004, PWIRE_OBJECT_META = 0x40005, PWIRE_COMMAND_NODE = 0x30002,
        PWIRE_PARAM_ENUM_FORMAT = 3, PWIRE_PARAM_FORMAT = 4, PWIRE_PARAM_BUFFERS = 5, PWIRE_PARAM_META = 6,
        PWIRE_FORMAT_MEDIA_TYPE = 1, PWIRE_FORMAT_MEDIA_SUBTYPE = 2, PWIRE_FORMAT_VIDEO = 0x20001, PWIRE_FORMAT_MODIFIER = 0x20002,
        PWIRE_FORMAT_SIZE = 0x20003, PWIRE_FORMAT_RATE = 0x20004, PWIRE_FORMAT_MAX_RATE = 0x20005,
        PWIRE_MEDIA_VIDEO = 2, PWIRE_SUBTYPE_RAW = 1, PWIRE_VIDEO_BGRX = 8, PWIRE_VIDEO_BGRA = 12,
        PWIRE_BUFFERS_BUFFERS = 1, PWIRE_BUFFERS_DATA_TYPE = 6, PWIRE_META_TYPE = 1, PWIRE_META_SIZE = 2,
        PWIRE_META_HEADER = 1, PWIRE_META_CROP = 2, PWIRE_META_DAMAGE = 3, PWIRE_META_CURSOR = 5,
        PWIRE_DATA_MEMFD = 2, PWIRE_DATA_MEMID = 4, PWIRE_IO_CLOCK = 3, PWIRE_IO_POSITION = 7, PWIRE_IO_BUFFERS = 1,
        PWIRE_COMMAND_START = 2, PWIRE_COMMAND_PAUSE = 1,
        PWIRE_STATUS_NEED_DATA = 1, PWIRE_STATUS_HAVE_DATA = 2,
        PWIRE_ACT_NOT_TRIGGERED = 0, PWIRE_ACT_TRIGGERED = 1, PWIRE_ACT_AWAKE = 2, PWIRE_ACT_FINISHED = 3
};
// struct pw_node_activation's fields (pipewire 1.6.11's private.h, measured), and its position's
enum
{
        PWIRE_ACT_STATUS = 0, PWIRE_ACT_STATE_REQUIRED = 12, PWIRE_ACT_STATE_PENDING = 16, PWIRE_ACT_SIGNAL_TIME = 32,
        PWIRE_ACT_SERVER_VERSION = 544, PWIRE_ACT_ACTIVE_DRIVER = 548, PWIRE_ACT_DRIVER = 552, PWIRE_ACT_POSITION = 560,
        PWIRE_POSITION_SIZE = 1688, PWIRE_CLOCK_SIZE = 160,
        PWIRE_CLOCK_ID = 4, PWIRE_CLOCK_NAME = 8, PWIRE_CLOCK_NSEC = 72, PWIRE_CLOCK_RATE = 80, PWIRE_CLOCK_POSITION = 88,
        PWIRE_CLOCK_DURATION = 96, PWIRE_CLOCK_RATE_DIFF = 112, PWIRE_CLOCK_NEXT = 120, PWIRE_CLOCK_TARGET_RATE = 128,
        PWIRE_CLOCK_TARGET_DURATION = 136, PWIRE_CLOCK_CYCLE = 148, PWIRE_POSITION_VIDEO = 160, PWIRE_POSITION_STATE = 208,
        PWIRE_POSITION_SEGMENTS = 212, PWIRE_SEGMENT_RATE = 24 + 216
};

/* What the server asks of its host. */
struct pwire_host
{
        void *context;
        void *(*alloc)(void *context, long bytes); // zeroed, or null
        void (*release)(void *context, void *block, long bytes);
        long long (*now)(void *context);           // nanoseconds, CLOCK_MONOTONIC: the present clock the frames are stamped on
        int (*eventfd)(void *context);             // a new eventfd (non-blocking, close-on-exec), or -1
        void (*signal)(void *context, int fd);     // one written to an eventfd
        int (*memfd)(void *context, long bytes, void **mapped); // a new memfd of bytes, mapped for reading and writing, or -1
        void (*unmap)(void *context, void *mapped, long bytes);
        void (*close)(void *context, int fd);
        unsigned (*cookie)(void *context);         // a random number for the core's info
};

/* The source a connection may see: the capture engine's ring, as the compositor made it. */
struct pwire_source
{
        unsigned width, height, stride; // the frame: 4 bytes a pixel, XRGB8888 little-endian (BGRx), alpha 255 (so BGRA too)
        unsigned rate;                  // the most frames a second (the output's refresh)
        unsigned slots;                 // the ring's slots, each a buffer here
        unsigned long slot_bytes, first; // slot k at first + k * slot_bytes of the memfd
        int fd;                         // the ring's memfd, readable only (the host's to close, after pwire_close)
        unsigned node;                  // the source's global id (what the portal's Start said)
        const char *name;               // what the client is told the source is
};

struct pwire_format
{
        unsigned video, width, height, rate;
};

struct pwire_connection
{
        int used, dead, hello;
        const char *why;                 // what ended it
        unsigned version, seq, node_global, client_global;
        struct pwire_source source;
        unsigned char *in;               // a message being read: in[0, in_length)
        long in_length, in_room;
        unsigned char *out;              // bytes to send: out[out_head, out_length)
        long out_head, out_length, out_room;
        int *out_fd;                     // descriptors to send with them (the host's own: dup'd by the kernel)
        long out_fds, out_fd_room;
        // the client node
        unsigned proxy, registry;        // the client's ids for its node and its registry (0: none)
        unsigned char active, port, format_sent, buffers_sent, started, want_crop, want_damage, want_cursor;
        struct pwire_format format;
        unsigned buffers, buffers_least, buffers_most, header_size, crop_size, damage_size, cursor_size, meta_size;
        unsigned char meta_order[4];     // the metas the client asked for, in its order (PWIRE_META_*), meta_count of them
        unsigned meta_count;
        // the session's memory: [0, act) the client's activation, [act, 2 act) the driver's, then the io area; the metadata apart
        int mem_fd, meta_fd;
        unsigned char *mem, *meta;
        long mem_bytes, act_at, driver_at, io_at, meta_bytes;
        int trigger_fd, done_fd;         // the client's eventfd (it reads), and the driver's (it writes when done)
        unsigned long long held;         // the buffers the client holds
        unsigned last;                   // the buffer given last
        unsigned cycle;
        long long last_frame;            // when the last frame was given (ns), for the rate
        unsigned long frames, drops;
};

static struct
{
        const struct pwire_host *host;
        struct pwire_connection **connection;
        long connections, room;
        unsigned next_global;
} pwire;

static void pwire_zero(void *to, long n)
{
        unsigned char *p = to;

        while (n-- > 0)
                *p++ = 0;
}

static void pwire_copy(void *to, const void *from, long n)
{
        unsigned char *p = to;
        const unsigned char *q = from;

        while (n-- > 0)
                *p++ = *q++;
}

static long pwire_length(const char *s)
{
        long n = 0;

        while (s && s[n])
                n++;
        return n;
}

static int pwire_equal(const char *a, long n, const char *b)
{
        long k = 0;

        for (; k < n && b[k]; k++)
                if (a[k] != b[k])
                        return 0;
        return k == n && !b[k];
}

static unsigned pwire_get32(const unsigned char *p)
{
        return (unsigned)p[0] | (unsigned)p[1] << 8 | (unsigned)p[2] << 16 | (unsigned)p[3] << 24;
}

static void pwire_put32(unsigned char *p, unsigned v)
{
        p[0] = (unsigned char)v, p[1] = (unsigned char)(v >> 8), p[2] = (unsigned char)(v >> 16), p[3] = (unsigned char)(v >> 24);
}

static void pwire_put64(unsigned char *p, unsigned long long v)
{
        pwire_put32(p, (unsigned)v), pwire_put32(p + 4, (unsigned)(v >> 32));
}

/* ---- room that grows ---- */

// Room for `need` bytes in a block of `*room` (grown to the next power of two past it, never past `most`): 0, or -1.
static int pwire_grow(unsigned char **block, long *room, long need, long most)
{
        long size = *room ? *room : 4096;
        unsigned char *fresh;

        if (need <= *room)
                return 0;
        if (need > most)
                return -1;
        while (size < need)
                size *= 2;
        if (size > most)
                size = most;
        if (!(fresh = pwire.host->alloc(pwire.host->context, size)))
                return -1;
        if (*block)
        {
                pwire_copy(fresh, *block, *room);
                pwire.host->release(pwire.host->context, *block, *room);
        }
        *block = fresh, *room = size;
        return 0;
}

/* ---- pods written ---- */

// A message is written straight into its connection's output; a fault (no room) marks it bad and the connection ends.
struct pwire_out
{
        struct pwire_connection *c;
        long start;                     // the message's header
        int bad;
        int fds;                        // the descriptors the message carries
};

static unsigned char *pwire_o_room(struct pwire_out *o, long n)
{
        struct pwire_connection *c = o->c;

        if (o->bad || pwire_grow(&c->out, &c->out_room, c->out_length + n, PWIRE_MESSAGE_MOST * 4l) < 0)
        {
                o->bad = 1;
                return 0;
        }
        c->out_length += n;
        return c->out + c->out_length - n;
}

static void pwire_o_pad(struct pwire_out *o)
{
        long pad = (8 - (o->c->out_length - o->start) % 8) % 8;
        unsigned char *p = pad ? pwire_o_room(o, pad) : 0;

        if (p)
                pwire_zero(p, pad);
}

// A pod of a fixed body: its header and its body, padded.
static void pwire_o_pod(struct pwire_out *o, unsigned type, const void *body, unsigned size)
{
        unsigned char *p = pwire_o_room(o, 8 + (long)size);

        if (!p)
                return;
        pwire_put32(p, size), pwire_put32(p + 4, type);
        pwire_copy(p + 8, body, size);
        pwire_o_pad(o);
}

static void pwire_o_int(struct pwire_out *o, unsigned type, unsigned v)
{
        unsigned char body[4];

        pwire_put32(body, v);
        pwire_o_pod(o, type, body, 4);
}

static void pwire_o_long(struct pwire_out *o, unsigned long long v)
{
        unsigned char body[8];

        pwire_put64(body, v);
        pwire_o_pod(o, PWIRE_LONG, body, 8);
}

static void pwire_o_pair(struct pwire_out *o, unsigned type, unsigned a, unsigned b)
{
        unsigned char body[8];

        pwire_put32(body, a), pwire_put32(body + 4, b);
        pwire_o_pod(o, type, body, 8);
}

static void pwire_o_string(struct pwire_out *o, const char *s)
{
        pwire_o_pod(o, PWIRE_STRING, s, (unsigned)pwire_length(s) + 1);
}

// A descriptor: the index of it among the message's, the descriptor queued to go with the bytes.
static void pwire_o_fd(struct pwire_out *o, int fd)
{
        struct pwire_connection *c = o->c;

        if (!o->bad && c->out_fds + 1 > c->out_fd_room)
        {
                long room = c->out_fd_room ? c->out_fd_room * 2 : 16;
                int *fresh = pwire.host->alloc(pwire.host->context, room * (long)sizeof(int));

                if (!fresh)
                        o->bad = 1;
                else
                {
                        if (c->out_fd)
                        {
                                pwire_copy(fresh, c->out_fd, c->out_fd_room * (long)sizeof(int));
                                pwire.host->release(pwire.host->context, c->out_fd, c->out_fd_room * (long)sizeof(int));
                        }
                        c->out_fd = fresh, c->out_fd_room = room;
                }
        }
        if (o->bad)
                return;
        c->out_fd[c->out_fds++] = fd;
        {
                unsigned char body[8];

                pwire_put64(body, (unsigned long long)o->fds++);
                pwire_o_pod(o, PWIRE_FD, body, 8);
        }
}

// A container begun: where its header is, for pwire_o_end.
static long pwire_o_begin(struct pwire_out *o, unsigned type)
{
        unsigned char *p = pwire_o_room(o, 8);

        if (!p)
                return -1;
        pwire_put32(p + 4, type);
        return o->c->out_length - 8;
}

static void pwire_o_end(struct pwire_out *o, long at)
{
        if (o->bad || at < 0)
                return;
        pwire_put32(o->c->out + at, (unsigned)(o->c->out_length - at - 8));
        pwire_o_pad(o);
}

static long pwire_o_object(struct pwire_out *o, unsigned type, unsigned id)
{
        long at = pwire_o_begin(o, PWIRE_OBJECT);
        unsigned char *p = pwire_o_room(o, 8);

        if (p)
                pwire_put32(p, type), pwire_put32(p + 4, id);
        return at;
}

static void pwire_o_prop(struct pwire_out *o, unsigned key)
{
        unsigned char *p = pwire_o_room(o, 8);

        if (p)
                pwire_put32(p, key), pwire_put32(p + 4, 0);
}

// A dict, as the protocol writes one: a struct of its count and its keys and values.
static void pwire_o_dict(struct pwire_out *o, const char *const *pairs, unsigned count)
{
        long at = pwire_o_begin(o, PWIRE_STRUCT);

        pwire_o_int(o, PWIRE_INT, count);
        for (unsigned k = 0; k < 2 * count; k++)
                pwire_o_string(o, pairs[k]);
        pwire_o_end(o, at);
}

// A message to the client begun: the object it is an event of and the event's opcode. Its body is one struct.
static long pwire_o_message(struct pwire_out *o, struct pwire_connection *c, unsigned id, unsigned opcode)
{
        unsigned char *p;

        o->c = c, o->bad = 0, o->fds = 0, o->start = c->out_length;
        p = pwire_o_room(o, PWIRE_HEADER);
        if (p)
        {
                pwire_put32(p, id), pwire_put32(p + 4, opcode << 24), pwire_put32(p + 8, c->seq++), pwire_put32(p + 12, 0);
                o->start = c->out_length;
        }
        return pwire_o_begin(o, PWIRE_STRUCT);
}

static void pwire_o_send(struct pwire_out *o, long body)
{
        struct pwire_connection *c = o->c;
        long header = o->start - PWIRE_HEADER, size;

        pwire_o_end(o, body);
        size = c->out_length - o->start;
        if (o->bad || size >= PWIRE_MESSAGE_MOST)
        {
                c->dead = 1, c->why = "no room for a message to it";
                return;
        }
        pwire_put32(c->out + header + 4, pwire_get32(c->out + header + 4) | (unsigned)size);
        pwire_put32(c->out + header + 12, (unsigned)o->fds);
}

/* ---- pods read ---- */

// A pod in a message: its type, its body and the body's size, all inside the message (pwire_p_next made sure).
struct pwire_pod
{
        unsigned type, size;
        const unsigned char *body;
};

// The pod at *at of [p, p + n): 0 and *at past it (and its padding), or -1 when it does not fit.
static int pwire_p_next(const unsigned char *p, long n, long *at, struct pwire_pod *pod)
{
        long size;

        if (*at < 0 || n - *at < 8)
                return -1;
        size = (long)pwire_get32(p + *at);
        pod->type = pwire_get32(p + *at + 4);
        if (size > n - *at - 8)
                return -1;
        pod->size = (unsigned)size, pod->body = p + *at + 8;
        *at += 8 + ((size + 7) & ~7l);
        if (*at > n)
                *at = n;
        return 0;
}

// The members of a struct, read one after another.
struct pwire_members
{
        const unsigned char *p;
        long n, at;
        int bad;
};

static int pwire_p_struct(const struct pwire_pod *pod, struct pwire_members *m)
{
        m->p = pod->body, m->n = pod->size, m->at = 0, m->bad = pod->type != PWIRE_STRUCT;
        return m->bad ? -1 : 0;
}

static struct pwire_pod pwire_p_member(struct pwire_members *m)
{
        struct pwire_pod pod = {0, 0, 0};

        if (m->bad || pwire_p_next(m->p, m->n, &m->at, &pod) < 0)
                m->bad = 1, pod.type = 0;
        return pod;
}

// An Int (or an Id, or a Bool: four bytes) member, or 0 with the members marked bad.
static unsigned pwire_p_int(struct pwire_members *m)
{
        struct pwire_pod pod = pwire_p_member(m);

        if ((pod.type != PWIRE_INT && pod.type != PWIRE_ID && pod.type != PWIRE_BOOL) || pod.size < 4)
        {
                m->bad = 1;
                return 0;
        }
        return pwire_get32(pod.body);
}

// A String member: its bytes (NUL-ended inside its pod) and length, or null with the members marked bad. A None is a null string.
static const char *pwire_p_string(struct pwire_members *m, long *length)
{
        struct pwire_pod pod = pwire_p_member(m);
        long n = 0;

        *length = 0;
        if (pod.type == PWIRE_NONE)
                return "";
        if (pod.type != PWIRE_STRING || !pod.size || pod.body[pod.size - 1])
        {
                m->bad = 1;
                return 0;
        }
        while (pod.body[n])
                n++;
        *length = n;
        return (const char *)pod.body;
}

// The values of a property: a plain value is one of them; a Choice's are its child pods (the default first), each `size` bytes.
struct pwire_values
{
        unsigned type, choice, size, count;
        const unsigned char *at;
};

static int pwire_p_values(const struct pwire_pod *pod, struct pwire_values *v)
{
        if (pod->type != PWIRE_CHOICE)
        {
                v->type = pod->type, v->choice = PWIRE_CHOICE_NONE, v->size = pod->size, v->count = 1, v->at = pod->body;
                return 0;
        }
        if (pod->size < 16)
                return -1;
        v->choice = pwire_get32(pod->body);
        v->size = pwire_get32(pod->body + 8);
        v->type = pwire_get32(pod->body + 12);
        if (!v->size || v->size > pod->size - 16)
                return -1;
        v->count = (pod->size - 16) / v->size;
        v->at = pod->body + 16;
        return v->count ? 0 : -1;
}

static unsigned pwire_value32(const struct pwire_values *v, unsigned k, unsigned word)
{
        return k < v->count && v->size >= 4 * (word + 1) ? pwire_get32(v->at + (long)k * v->size + 4 * word) : 0;
}

// Whether the values allow x (an Id or an Int): one of them for none or an enum, between the bounds for a range, a bit of the flags.
static int pwire_allows(const struct pwire_values *v, unsigned x)
{
        if (v->choice == PWIRE_CHOICE_NONE)
                return pwire_value32(v, 0, 0) == x;
        if (v->choice == PWIRE_CHOICE_ENUM)
        {
                for (unsigned k = v->count > 1 ? 1 : 0; k < v->count; k++)
                        if (pwire_value32(v, k, 0) == x)
                                return 1;
                return 0;
        }
        if (v->choice == PWIRE_CHOICE_RANGE || v->choice == PWIRE_CHOICE_STEP)
                return v->count >= 3 && pwire_value32(v, 1, 0) <= x && x <= pwire_value32(v, 2, 0);
        if (v->choice == PWIRE_CHOICE_FLAGS)
                return (pwire_value32(v, 0, 0) & x) == x;
        return 0;
}

// The properties of an object pod: its type and id, then each key and value in turn (pwire_p_prop).
struct pwire_props
{
        unsigned type, id;
        const unsigned char *p;
        long n, at;
};

static int pwire_p_object(const struct pwire_pod *pod, struct pwire_props *o)
{
        if (pod->type != PWIRE_OBJECT || pod->size < 8)
                return -1;
        o->type = pwire_get32(pod->body), o->id = pwire_get32(pod->body + 4);
        o->p = pod->body + 8, o->n = pod->size - 8, o->at = 0;
        return 0;
}

// The next property: 1 with its key and value, 0 at the end, -1 when it does not fit.
static int pwire_p_prop(struct pwire_props *o, unsigned *key, struct pwire_pod *value)
{
        if (o->at >= o->n)
                return 0;
        if (o->n - o->at < 8)
                return -1;
        *key = pwire_get32(o->p + o->at);
        o->at += 8;
        return pwire_p_next(o->p, o->n, &o->at, value) < 0 ? -1 : 1;
}

/* ---- the connections ---- */

static struct pwire_connection *pwire_of(long slot)
{
        return slot >= 0 && slot < pwire.connections && pwire.connection[slot] && pwire.connection[slot]->used ? pwire.connection[slot] : 0;
}

PWIRE_API void pwire_start(const struct pwire_host *host)
{
        pwire.host = host;
        pwire.next_global = 32;
}

// The source's memory and the session's descriptors let go; the connection's queues too.
static void pwire_free(struct pwire_connection *c)
{
        const struct pwire_host *h = pwire.host;

        if (c->mem)
                h->unmap(h->context, c->mem, c->mem_bytes);
        if (c->mem_fd >= 0)
                h->close(h->context, c->mem_fd);
        if (c->meta)
                h->unmap(h->context, c->meta, c->meta_bytes);
        if (c->meta_fd >= 0)
                h->close(h->context, c->meta_fd);
        if (c->trigger_fd >= 0)
                h->close(h->context, c->trigger_fd);
        if (c->done_fd >= 0)
                h->close(h->context, c->done_fd);
        if (c->in)
                h->release(h->context, c->in, c->in_room);
        if (c->out)
                h->release(h->context, c->out, c->out_room);
        if (c->out_fd)
                h->release(h->context, c->out_fd, c->out_fd_room * (long)sizeof(int));
}

// A connection for a client that may see `source`: its slot, or -1. The client says Hello first.
PWIRE_API long pwire_open(const struct pwire_source *source)
{
        const struct pwire_host *h = pwire.host;
        struct pwire_connection *c;
        long slot = -1;

        if (!h || !source || !source->width || !source->height || source->stride < source->width * 4 || !source->slots ||
            source->slots > PWIRE_BUFFERS_MOST || source->slot_bytes < (unsigned long)source->stride * source->height || source->fd < 0)
                return -1;
        for (long k = 0; k < pwire.connections && slot < 0; k++)
                if (!pwire.connection[k])
                        slot = k;
        if (slot < 0)
        {
                if (pwire.connections == pwire.room)
                {
                        long room = pwire.room ? pwire.room * 2 : 8;
                        struct pwire_connection **fresh = h->alloc(h->context, room * (long)sizeof(*fresh));

                        if (!fresh)
                                return -1;
                        if (pwire.connection)
                        {
                                pwire_copy(fresh, pwire.connection, pwire.room * (long)sizeof(*fresh));
                                h->release(h->context, pwire.connection, pwire.room * (long)sizeof(*fresh));
                        }
                        pwire.connection = fresh, pwire.room = room;
                }
                slot = pwire.connections++;
        }
        if (!(c = h->alloc(h->context, sizeof(*c))))
                return -1;
        pwire_zero(c, sizeof(*c));
        c->used = 1, c->source = *source, c->mem_fd = c->meta_fd = c->trigger_fd = c->done_fd = -1, c->last = ~0u;
        c->client_global = pwire.next_global++;
        c->node_global = pwire.next_global++;
        pwire.connection[slot] = c;
        return slot;
}

PWIRE_API void pwire_close(long slot)
{
        struct pwire_connection *c = pwire_of(slot);

        if (!c)
                return;
        pwire_free(c);
        pwire.host->release(pwire.host->context, c, sizeof(*c));
        pwire.connection[slot] = 0;
}

// Whether a connection is to be closed (it sent what it must not, or there was no room for it), and why.
PWIRE_API const char *pwire_dead(long slot)
{
        struct pwire_connection *c = pwire_of(slot);

        return !c ? "no such connection" : c->dead ? (c->why ? c->why : "ended") : 0;
}

/* ---- events ---- */

static void pwire_error(struct pwire_connection *c, unsigned id, int seq, int result, const char *text)
{
        struct pwire_out o;
        long body = pwire_o_message(&o, c, 0, 3);

        pwire_o_int(&o, PWIRE_INT, id), pwire_o_int(&o, PWIRE_INT, (unsigned)seq), pwire_o_int(&o, PWIRE_INT, (unsigned)result);
        pwire_o_string(&o, text);
        pwire_o_send(&o, body);
}

static void pwire_remove_id(struct pwire_connection *c, unsigned id)
{
        struct pwire_out o;
        long body = pwire_o_message(&o, c, 0, 4);

        pwire_o_int(&o, PWIRE_INT, id);
        pwire_o_send(&o, body);
}

static void pwire_bound(struct pwire_connection *c, unsigned id, unsigned global, const char *const *pairs, unsigned count)
{
        struct pwire_out o;
        long body = pwire_o_message(&o, c, 0, 5);

        pwire_o_int(&o, PWIRE_INT, id), pwire_o_int(&o, PWIRE_INT, global);
        pwire_o_send(&o, body);
        if (c->version < 4)
                return;
        body = pwire_o_message(&o, c, 0, 8);
        pwire_o_int(&o, PWIRE_INT, id), pwire_o_int(&o, PWIRE_INT, global);
        pwire_o_dict(&o, pairs, count);
        pwire_o_send(&o, body);
}

static void pwire_add_mem(struct pwire_connection *c, unsigned id, int fd, unsigned flags)
{
        struct pwire_out o;
        long body = pwire_o_message(&o, c, 0, 6);

        pwire_o_int(&o, PWIRE_INT, id), pwire_o_int(&o, PWIRE_ID, PWIRE_DATA_MEMFD);
        pwire_o_fd(&o, fd);
        pwire_o_int(&o, PWIRE_INT, flags);
        pwire_o_send(&o, body);
}

static void pwire_hello(struct pwire_connection *c, unsigned version)
{
        static const char *const core[] = {"core.name", "pipewire-0", "core.version", "1.6.11", "default.clock.rate", "48000",
                                           "moonwater.cast", "1"};
        static const char *const client[] = {"core.name", "pipewire-0", "pipewire.protocol", "protocol-native"};
        struct pwire_out o;
        long body;

        c->hello = 1, c->version = version;
        body = pwire_o_message(&o, c, 0, 0);
        pwire_o_int(&o, PWIRE_INT, 0);
        pwire_o_int(&o, PWIRE_INT, pwire.host->cookie ? pwire.host->cookie(pwire.host->context) : 0);
        pwire_o_string(&o, "moonwater"), pwire_o_string(&o, "moonwater"), pwire_o_string(&o, "1.6.11"), pwire_o_string(&o, "pipewire-0");
        pwire_o_long(&o, 1);
        pwire_o_dict(&o, core, 4);
        pwire_o_send(&o, body);
        pwire_bound(c, 1, c->client_global, client, 2);
}

static void pwire_client_info(struct pwire_connection *c)
{
        static const char *const client[] = {"pipewire.protocol", "protocol-native", "moonwater.cast", "1"};
        struct pwire_out o;
        long body = pwire_o_message(&o, c, 1, 0);

        pwire_o_int(&o, PWIRE_INT, c->client_global);
        pwire_o_long(&o, 1);
        pwire_o_dict(&o, client, 2);
        pwire_o_send(&o, body);
}

// The registry's one global: the source's node.
static void pwire_globals(struct pwire_connection *c)
{
        const char *pairs[] = {"node.name", c->source.name ? c->source.name : "moonwater-cast", "media.class", "Video/Source",
                               "object.serial", "0"};
        struct pwire_out o;
        long body = pwire_o_message(&o, c, c->registry, 0);

        pwire_o_int(&o, PWIRE_INT, c->source.node);
        pwire_o_int(&o, PWIRE_INT, 4); // read: the client may see it, and nothing more
        pwire_o_string(&o, "PipeWire:Interface:Node");
        pwire_o_int(&o, PWIRE_INT, 3);
        pwire_o_dict(&o, pairs, 2);
        pwire_o_send(&o, body);
}

/* ---- the client node ---- */

// The session's memory: both activations and the io area.
static int pwire_memory(struct pwire_connection *c)
{
        const struct pwire_host *h = pwire.host;
        long act = (PWIRE_ACT + PWIRE_PAGE - 1) / PWIRE_PAGE * PWIRE_PAGE;
        void *mapped = 0;

        c->act_at = 0, c->driver_at = act, c->io_at = 2 * act;
        c->mem_bytes = 2 * act + PWIRE_PAGE;
        if ((c->mem_fd = h->memfd(h->context, c->mem_bytes, &mapped)) < 0 || !mapped)
                return -1;
        c->mem = mapped;
        pwire_zero(c->mem, c->mem_bytes);
        if ((c->trigger_fd = h->eventfd(h->context)) < 0 || (c->done_fd = h->eventfd(h->context)) < 0)
                return -1;
        pwire_put32(c->mem + c->act_at + PWIRE_ACT_SERVER_VERSION, 1);
        pwire_put32(c->mem + c->driver_at + PWIRE_ACT_SERVER_VERSION, 1);
        pwire_put32(c->mem + c->act_at + PWIRE_ACT_DRIVER, c->source.node);
        pwire_put32(c->mem + c->act_at + PWIRE_ACT_ACTIVE_DRIVER, c->source.node);
        pwire_put32(c->mem + c->act_at + PWIRE_ACT_POSITION + PWIRE_CLOCK_ID, c->node_global);
        pwire_put32(c->mem + c->driver_at + PWIRE_ACT_POSITION + PWIRE_CLOCK_ID, c->source.node);
        pwire_put32(c->mem + c->io_at, PWIRE_STATUS_NEED_DATA);
        pwire_put32(c->mem + c->io_at + 4, ~0u);
        return 0;
}

// CreateObject of the one client node: its memory, its transport and its id.
static void pwire_node_make(struct pwire_connection *c, unsigned new_id)
{
        static const char *const node[] = {"object.serial", "1", "node.name", "moonwater-cast-client"};
        struct pwire_out o;
        long body;

        if (pwire_memory(c) < 0)
        {
                c->dead = 1, c->why = "no memory for the session";
                return;
        }
        c->proxy = new_id;
        pwire_add_mem(c, 0, c->mem_fd, 3); // readable and writable
        pwire_bound(c, new_id, c->node_global, node, 2);
        body = pwire_o_message(&o, c, new_id, 0); // Transport
        pwire_o_fd(&o, c->trigger_fd), pwire_o_fd(&o, c->done_fd);
        pwire_o_int(&o, PWIRE_INT, 0), pwire_o_int(&o, PWIRE_INT, (unsigned)c->act_at), pwire_o_int(&o, PWIRE_INT, PWIRE_ACT);
        pwire_o_send(&o, body);
}

// The first of the client's EnumFormats the source can be: video/raw, BGRx or BGRA, of the source's size, and no modifier asked.
static int pwire_format_pick(struct pwire_connection *c, const struct pwire_pod *param)
{
        struct pwire_props props;
        struct pwire_pod value;
        struct pwire_values v;
        unsigned key, ok = 0, video = 0;
        int more;

        if (pwire_p_object(param, &props) < 0 || props.type != PWIRE_OBJECT_FORMAT)
                return 0;
        while ((more = pwire_p_prop(&props, &key, &value)) > 0)
        {
                if (pwire_p_values(&value, &v) < 0)
                        return 0;
                if (key == PWIRE_FORMAT_MEDIA_TYPE)
                        ok |= pwire_allows(&v, PWIRE_MEDIA_VIDEO) ? 1 : 0;
                else if (key == PWIRE_FORMAT_MEDIA_SUBTYPE)
                        ok |= pwire_allows(&v, PWIRE_SUBTYPE_RAW) ? 2 : 0;
                else if (key == PWIRE_FORMAT_VIDEO)
                        video = pwire_allows(&v, PWIRE_VIDEO_BGRX) ? PWIRE_VIDEO_BGRX : pwire_allows(&v, PWIRE_VIDEO_BGRA) ? PWIRE_VIDEO_BGRA : 0;
                else if (key == PWIRE_FORMAT_MODIFIER)
                        return 0; // a buffer of the card's, which this source does not make
                else if (key == PWIRE_FORMAT_SIZE && v.type == PWIRE_RECTANGLE && v.size >= 8)
                {
                        unsigned w = c->source.width, h = c->source.height;

                        if (v.choice == PWIRE_CHOICE_NONE ? pwire_value32(&v, 0, 0) != w || pwire_value32(&v, 0, 1) != h
                                                          : v.count < 3 || w < pwire_value32(&v, 1, 0) || h < pwire_value32(&v, 1, 1) ||
                                                                w > pwire_value32(&v, 2, 0) || h > pwire_value32(&v, 2, 1))
                                return 0;
                }
        }
        if (more < 0 || ok != 3 || !video)
                return 0;
        c->format.video = video, c->format.width = c->source.width, c->format.height = c->source.height, c->format.rate = c->source.rate;
        return 1;
}

// What the client asks of its buffers (a Buffers param) and the metas it wants (Meta params).
static void pwire_buffers_asked(struct pwire_connection *c, const struct pwire_pod *param, int *types_ok)
{
        struct pwire_props props;
        struct pwire_pod value;
        struct pwire_values v;
        unsigned key;

        if (pwire_p_object(param, &props) < 0)
                return;
        if (props.type == PWIRE_OBJECT_BUFFERS)
        {
                while (pwire_p_prop(&props, &key, &value) > 0)
                        if (pwire_p_values(&value, &v) == 0 && v.type == PWIRE_INT)
                        {
                                if (key == PWIRE_BUFFERS_BUFFERS)
                                {
                                        c->buffers_least = v.choice == PWIRE_CHOICE_RANGE && v.count >= 3 ? pwire_value32(&v, 1, 0) : 1;
                                        c->buffers_most = v.choice == PWIRE_CHOICE_RANGE && v.count >= 3 ? pwire_value32(&v, 2, 0) : PWIRE_BUFFERS_MOST;
                                }
                                else if (key == PWIRE_BUFFERS_DATA_TYPE)
                                        *types_ok &= (pwire_value32(&v, 0, 0) & (1u << PWIRE_DATA_MEMFD)) != 0;
                        }
                return;
        }
        if (props.type != PWIRE_OBJECT_META)
                return;
        unsigned type = 0, size = 0;

        while (pwire_p_prop(&props, &key, &value) > 0)
                if (pwire_p_values(&value, &v) == 0)
                {
                        if (key == PWIRE_META_TYPE)
                                type = pwire_value32(&v, 0, 0);
                        else if (key == PWIRE_META_SIZE)
                                size = pwire_value32(&v, 0, 0); // a range's default
                }
        // the metas the source fills: their sizes are its own, but for the damage, of which the client says how many rectangles
        if (c->meta_count >= 4)
                return;
        for (unsigned k = 0; k < c->meta_count; k++)
                if (c->meta_order[k] == type)
                        return;
        if (type == PWIRE_META_HEADER)
                c->header_size = 32;
        else if (type == PWIRE_META_CROP)
                c->crop_size = 16;
        else if (type == PWIRE_META_DAMAGE && size >= 16)
                c->damage_size = (size > 16 * PWIRE_DAMAGE_MOST ? 16 * PWIRE_DAMAGE_MOST : size) / 16 * 16;
        else if (type == PWIRE_META_CURSOR && size >= 28 && size <= (4u << 20))
                c->cursor_size = size;
        else
                return;
        c->meta_order[c->meta_count++] = (unsigned char)type;
}

static unsigned pwire_meta_size(const struct pwire_connection *c, unsigned type)
{
        return type == PWIRE_META_HEADER ? c->header_size : type == PWIRE_META_CROP ? c->crop_size : type == PWIRE_META_DAMAGE ? c->damage_size
                                                                                     : type == PWIRE_META_CURSOR ? c->cursor_size : 0;
}

// The mix and the format, once the node is active and its port is there.
static void pwire_link(struct pwire_connection *c)
{
        static const char *const none[1] = {0};
        struct pwire_out o;
        long body, object;

        if (c->format_sent || !c->active || !c->port || !c->format.video)
                return;
        body = pwire_o_message(&o, c, c->proxy, 11); // PortSetMixInfo: input 0, mix 0, from the source's port
        pwire_o_int(&o, PWIRE_INT, 0), pwire_o_int(&o, PWIRE_INT, 0), pwire_o_int(&o, PWIRE_INT, 0);
        pwire_o_int(&o, PWIRE_INT, c->source.node + 1);
        pwire_o_dict(&o, none, 0);
        pwire_o_send(&o, body);
        body = pwire_o_message(&o, c, c->proxy, 7); // PortSetParam Format
        pwire_o_int(&o, PWIRE_INT, 0), pwire_o_int(&o, PWIRE_INT, 0), pwire_o_int(&o, PWIRE_ID, PWIRE_PARAM_FORMAT), pwire_o_int(&o, PWIRE_INT, 0);
        object = pwire_o_object(&o, PWIRE_OBJECT_FORMAT, PWIRE_PARAM_FORMAT);
        pwire_o_prop(&o, PWIRE_FORMAT_MEDIA_TYPE), pwire_o_int(&o, PWIRE_ID, PWIRE_MEDIA_VIDEO);
        pwire_o_prop(&o, PWIRE_FORMAT_MEDIA_SUBTYPE), pwire_o_int(&o, PWIRE_ID, PWIRE_SUBTYPE_RAW);
        pwire_o_prop(&o, PWIRE_FORMAT_VIDEO), pwire_o_int(&o, PWIRE_ID, c->format.video);
        pwire_o_prop(&o, PWIRE_FORMAT_SIZE), pwire_o_pair(&o, PWIRE_RECTANGLE, c->format.width, c->format.height);
        pwire_o_prop(&o, PWIRE_FORMAT_RATE), pwire_o_pair(&o, PWIRE_FRACTION, 0, 1);
        pwire_o_prop(&o, PWIRE_FORMAT_MAX_RATE), pwire_o_pair(&o, PWIRE_FRACTION, c->format.rate ? c->format.rate : 60, 1);
        pwire_o_end(&o, object);
        pwire_o_send(&o, body);
        c->format_sent = 1;
}

// The buffers (the ring's slots, with their metadata here), then the driver's activation, the io area and Start.
static void pwire_buffers_give(struct pwire_connection *c)
{
        struct pwire_out o;
        long body, object;
        unsigned per = 0, n = c->source.slots;

        if (c->buffers_sent || !c->format_sent)
                return;
        if (n < c->buffers_least)
        {
                c->dead = 1, c->why = "it asks more buffers than the source has";
                return;
        }
        if (n > c->buffers_most)
                n = c->buffers_most;
        for (unsigned k = 0; k < c->meta_count; k++)
                per += (pwire_meta_size(c, c->meta_order[k]) + 7) & ~7u;
        per += 16; // the chunk
        {
                const struct pwire_host *h = pwire.host;
                void *mapped = 0;

                c->meta_bytes = ((long)per * n + PWIRE_PAGE - 1) / PWIRE_PAGE * PWIRE_PAGE;
                if ((c->meta_fd = h->memfd(h->context, c->meta_bytes, &mapped)) < 0 || !mapped)
                {
                        c->dead = 1, c->why = "no memory for its buffers' metadata";
                        return;
                }
                c->meta = mapped;
                pwire_zero(c->meta, c->meta_bytes);
        }
        c->buffers = n, c->meta_size = per;
        pwire_add_mem(c, 1, c->source.fd, 1); // readable only
        pwire_add_mem(c, 2, c->meta_fd, 3);
        body = pwire_o_message(&o, c, c->proxy, 8); // PortUseBuffers: input 0, mix 0
        pwire_o_int(&o, PWIRE_INT, 0), pwire_o_int(&o, PWIRE_INT, 0), pwire_o_int(&o, PWIRE_INT, 0), pwire_o_int(&o, PWIRE_INT, 0);
        pwire_o_int(&o, PWIRE_INT, n);
        for (unsigned b = 0; b < n; b++)
        {
                pwire_o_int(&o, PWIRE_INT, 2), pwire_o_int(&o, PWIRE_INT, b * per), pwire_o_int(&o, PWIRE_INT, per);
                pwire_o_int(&o, PWIRE_INT, c->meta_count);
                for (unsigned k = 0; k < c->meta_count; k++)
                        pwire_o_int(&o, PWIRE_ID, c->meta_order[k]), pwire_o_int(&o, PWIRE_INT, pwire_meta_size(c, c->meta_order[k]));
                pwire_o_int(&o, PWIRE_INT, 1);
                pwire_o_int(&o, PWIRE_ID, PWIRE_DATA_MEMID), pwire_o_int(&o, PWIRE_INT, 1);
                pwire_o_int(&o, PWIRE_INT, 1 | 8); // readable, mappable
                pwire_o_int(&o, PWIRE_INT, (unsigned)(c->source.first + b * c->source.slot_bytes));
                pwire_o_int(&o, PWIRE_INT, (unsigned)c->source.slot_bytes);
        }
        pwire_o_send(&o, body);
        body = pwire_o_message(&o, c, c->proxy, 10); // SetActivation of the driver, which the client tells when it is done
        pwire_o_int(&o, PWIRE_INT, c->source.node);
        pwire_o_fd(&o, c->done_fd);
        pwire_o_int(&o, PWIRE_INT, 0), pwire_o_int(&o, PWIRE_INT, (unsigned)c->driver_at), pwire_o_int(&o, PWIRE_INT, PWIRE_ACT);
        pwire_o_send(&o, body);
        body = pwire_o_message(&o, c, c->proxy, 2); // SetIO Position: the driver's
        pwire_o_int(&o, PWIRE_ID, PWIRE_IO_POSITION), pwire_o_int(&o, PWIRE_INT, 0);
        pwire_o_int(&o, PWIRE_INT, (unsigned)(c->driver_at + PWIRE_ACT_POSITION)), pwire_o_int(&o, PWIRE_INT, PWIRE_POSITION_SIZE);
        pwire_o_send(&o, body);
        body = pwire_o_message(&o, c, c->proxy, 9); // PortSetIO Buffers: input 0, mix 0
        pwire_o_int(&o, PWIRE_INT, 0), pwire_o_int(&o, PWIRE_INT, 0), pwire_o_int(&o, PWIRE_INT, 0);
        pwire_o_int(&o, PWIRE_ID, PWIRE_IO_BUFFERS), pwire_o_int(&o, PWIRE_INT, 0), pwire_o_int(&o, PWIRE_INT, (unsigned)c->io_at);
        pwire_o_int(&o, PWIRE_INT, 8);
        pwire_o_send(&o, body);
        body = pwire_o_message(&o, c, c->proxy, 4); // Command Start
        object = pwire_o_object(&o, PWIRE_COMMAND_NODE, PWIRE_COMMAND_START);
        pwire_o_end(&o, object);
        pwire_o_send(&o, body);
        c->buffers_sent = c->started = 1;
}

// PortUpdate: the port (input 0 only), the formats it takes, and once the format is set, what it wants of the buffers.
static void pwire_port_update(struct pwire_connection *c, struct pwire_members *m)
{
        unsigned direction = pwire_p_int(m), port = pwire_p_int(m), mask = pwire_p_int(m), count = pwire_p_int(m);
        int types_ok = 1, buffers = 0;

        (void)mask;
        if (m->bad || direction != 0 || port != 0)
        {
                c->dead = 1, c->why = "a port that is not one input";
                return;
        }
        c->port = 1;
        if (!c->format_sent)
                c->format.video = 0;
        c->meta_count = 0, c->header_size = c->crop_size = c->damage_size = c->cursor_size = 0;
        for (unsigned k = 0; k < count && !m->bad; k++)
        {
                struct pwire_pod param = pwire_p_member(m);
                struct pwire_props props;

                if (m->bad || pwire_p_object(&param, &props) < 0)
                        continue;
                if (props.id == PWIRE_PARAM_ENUM_FORMAT && !c->format_sent && !c->format.video)
                        pwire_format_pick(c, &param);
                else if (props.id == PWIRE_PARAM_BUFFERS || props.id == PWIRE_PARAM_META)
                {
                        buffers |= props.id == PWIRE_PARAM_BUFFERS;
                        pwire_buffers_asked(c, &param, &types_ok);
                }
        }
        if (m->bad)
        {
                c->dead = 1, c->why = "a port update that does not parse";
                return;
        }
        if (!c->format_sent && !c->format.video && count)
                pwire_error(c, c->proxy, 0, -22, "no format of the screen's: it is BGRx or BGRA in memory, of its own size");
        pwire_link(c);
        if (buffers && !types_ok)
                pwire_error(c, c->proxy, 0, -22, "the screen's frames are in memory (MemFd), not in the card's buffers");
        else if (buffers)
                pwire_buffers_give(c);
}

/* ---- messages ---- */

// One message whole: its object, opcode and body.
static void pwire_message(struct pwire_connection *c, unsigned id, unsigned opcode, const unsigned char *body, long size)
{
        struct pwire_pod pod;
        struct pwire_members m;
        long at = 0;

        if (pwire_p_next(body, size, &at, &pod) < 0 || pwire_p_struct(&pod, &m) < 0)
        {
                c->dead = 1, c->why = "a message that is not a struct";
                return;
        }
        if (!c->hello && !(id == 0 && opcode == 1))
        {
                c->dead = 1, c->why = "a message before Hello";
                return;
        }
        if (id == 0) // the core
        {
                if (opcode == 1) // Hello
                {
                        unsigned version = pwire_p_int(&m);

                        if (!m.bad && !c->hello)
                                pwire_hello(c, version);
                }
                else if (opcode == 2 || opcode == 3) // Sync: Done of the same; Pong: nothing
                {
                        unsigned of = pwire_p_int(&m), seq = pwire_p_int(&m);
                        struct pwire_out o;
                        long b;

                        if (!m.bad && opcode == 2)
                        {
                                b = pwire_o_message(&o, c, 0, 1);
                                pwire_o_int(&o, PWIRE_INT, of), pwire_o_int(&o, PWIRE_INT, seq);
                                pwire_o_send(&o, b);
                        }
                }
                else if (opcode == 4) // Error: the client's own, said
                        ;
                else if (opcode == 5) // GetRegistry
                {
                        unsigned version = pwire_p_int(&m), new_id = pwire_p_int(&m);

                        (void)version;
                        if (!m.bad && !c->registry && new_id > 1 && new_id != c->proxy)
                                c->registry = new_id, pwire_globals(c);
                        else if (!m.bad)
                                pwire_error(c, new_id, 0, -17, "one registry");
                }
                else if (opcode == 6) // CreateObject: the client node, once
                {
                        long n, t;
                        const char *factory = pwire_p_string(&m, &n);
                        const char *type = pwire_p_string(&m, &t);
                        unsigned version = pwire_p_int(&m);
                        struct pwire_pod props = pwire_p_member(&m);
                        unsigned new_id = pwire_p_int(&m);

                        (void)version, (void)props, (void)type;
                        if (m.bad)
                                ;
                        else if (pwire_equal(factory, n, "client-node") && !c->proxy && new_id > 1 && new_id != c->registry)
                                pwire_node_make(c, new_id);
                        else
                        {
                                pwire_error(c, new_id, 0, -1, "a screen cast's connection makes one client node and nothing else");
                                pwire_remove_id(c, new_id);
                        }
                }
                else if (opcode == 7) // Destroy
                {
                        unsigned of = pwire_p_int(&m);

                        if (!m.bad && of == c->proxy && of)
                                c->dead = 1, c->why = "its node destroyed";
                        else if (!m.bad && of == c->registry && of)
                                c->registry = 0, pwire_remove_id(c, of);
                }
                else
                        m.bad = 1;
        }
        else if (id == 1) // the client: its properties are said back, nothing else is done
        {
                if (opcode == 2)
                        pwire_client_info(c);
                else if (opcode > 4)
                        m.bad = 1;
        }
        else if (id == c->registry)
        {
                if (opcode == 1) // Bind: nothing can be bound
                {
                        unsigned of = pwire_p_int(&m);
                        long t;
                        const char *type = pwire_p_string(&m, &t);
                        unsigned version = pwire_p_int(&m), new_id = pwire_p_int(&m);

                        (void)of, (void)type, (void)version;
                        if (!m.bad)
                                pwire_error(c, new_id, 0, -1, "nothing of a screen cast is bound"), pwire_remove_id(c, new_id);
                }
                else if (opcode > 2)
                        m.bad = 1;
        }
        else if (id == c->proxy && id)
        {
                if (opcode == 2) // Update: the node's own params and info, which this server takes as they are
                        ;
                else if (opcode == 3)
                        pwire_port_update(c, &m);
                else if (opcode == 4) // SetActive
                {
                        c->active = (unsigned char)(pwire_p_int(&m) != 0);
                        pwire_link(c);
                }
                else if (opcode == 1 || opcode == 6) // GetNode, PortBuffers: not for this node
                {
                        c->dead = 1, c->why = "a node method a consumer does not use";
                        return;
                }
                else if (opcode > 6)
                        m.bad = 1;
        }
        else
        {
                c->dead = 1, c->why = "a message to an object it does not have";
                return;
        }
        if (m.bad && !c->dead)
                c->dead = 1, c->why = "a message that does not parse";
}

/*
        Bytes read from the connection, and the descriptors that came with them (which are the server's to close: a
        consumer sends none, and any are closed at once). 0, or -1 when the connection is to be closed (pwire_dead says why).
*/
PWIRE_API int pwire_read(long slot, const unsigned char *bytes, long n, const int *fds, long nfds)
{
        struct pwire_connection *c = pwire_of(slot);

        for (long k = 0; k < nfds; k++)
                if (fds[k] >= 0)
                        pwire.host->close(pwire.host->context, fds[k]);
        if (!c || c->dead)
                return -1;
        while (n > 0 && !c->dead)
        {
                long want = PWIRE_HEADER, take;

                if (c->in_length >= PWIRE_HEADER)
                        want += (long)(pwire_get32(c->in + 4) & 0xffffff);
                take = want - c->in_length < n ? want - c->in_length : n;
                if (pwire_grow(&c->in, &c->in_room, want, PWIRE_MESSAGE_MOST + PWIRE_HEADER) < 0)
                {
                        c->dead = 1, c->why = "no room for its message";
                        break;
                }
                pwire_copy(c->in + c->in_length, bytes, take);
                c->in_length += take, bytes += take, n -= take;
                if (c->in_length == PWIRE_HEADER && (pwire_get32(c->in + 4) & 0xffffff) == 0)
                        want = PWIRE_HEADER;
                if (c->in_length >= PWIRE_HEADER && c->in_length == PWIRE_HEADER + (long)(pwire_get32(c->in + 4) & 0xffffff))
                {
                        if (pwire_get32(c->in + 12))
                        {
                                c->dead = 1, c->why = "a message with descriptors";
                                break;
                        }
                        pwire_message(c, pwire_get32(c->in), pwire_get32(c->in + 4) >> 24, c->in + PWIRE_HEADER, c->in_length - PWIRE_HEADER);
                        c->in_length = 0;
                }
        }
        return c->dead ? -1 : 0;
}

// What is to be sent: the bytes, and the descriptors to send with the first of them (*fds, *nfds). 0 when nothing is.
PWIRE_API long pwire_pending(long slot, const unsigned char **bytes, const int **fds, long *nfds)
{
        struct pwire_connection *c = pwire_of(slot);

        *nfds = 0, *fds = 0, *bytes = 0;
        if (!c)
                return 0;
        *bytes = c->out + c->out_head;
        *fds = c->out_fd, *nfds = c->out_fds;
        return c->out_length - c->out_head;
}

// So many of the bytes sent, and the descriptors with them when sent is more than none.
PWIRE_API void pwire_wrote(long slot, long sent)
{
        struct pwire_connection *c = pwire_of(slot);

        if (!c || sent <= 0)
                return;
        c->out_fds = 0;
        c->out_head += sent;
        if (c->out_head >= c->out_length)
                c->out_head = c->out_length = 0;
}

// The eventfd the client writes when it has read a frame (the driver's): for the host to read and empty, then pwire_returned.
PWIRE_API int pwire_wake_fd(long slot)
{
        struct pwire_connection *c = pwire_of(slot);

        return c && c->started ? c->done_fd : -1;
}

// The buffers the client holds now, by bit: the host's ring keeps them from being written.
PWIRE_API unsigned long long pwire_held(long slot)
{
        struct pwire_connection *c = pwire_of(slot);
        unsigned status, id;

        if (!c || !c->started)
                return 0;
        // what the client gave back since: the io area's buffer when it asks for more (it is the client's to write: only an id in range counts)
        status = __atomic_load_n((unsigned *)(c->mem + c->io_at), __ATOMIC_ACQUIRE);
        id = __atomic_load_n((unsigned *)(c->mem + c->io_at + 4), __ATOMIC_ACQUIRE);
        if (status == PWIRE_STATUS_NEED_DATA && id < c->buffers && id != c->last)
                c->held &= ~(1ull << id);
        return c->held;
}

// Whether a frame may be told now: the client is streaming, done with the last one, and the rate allows (ns since the last).
PWIRE_API int pwire_ready(long slot, long long now)
{
        struct pwire_connection *c = pwire_of(slot);
        unsigned status;

        if (!c || !c->started || c->dead)
                return 0;
        status = __atomic_load_n((unsigned *)(c->mem + c->act_at), __ATOMIC_ACQUIRE);
        if (status == PWIRE_ACT_TRIGGERED || status == PWIRE_ACT_AWAKE)
                return 0;
        return !c->format.rate || !c->frames || now - c->last_frame >= 1000000000ll / c->format.rate - 1000000ll;
}

/*
        A frame of the source told to the client: slot `buffer` of the ring, stamped `pts` (ns on the present clock), `seq`
        its number, with its damage (rectangles x, y, w, h; none is the whole frame). 1 when it was told, 0 when the client
        is not ready for it (pwire_ready) and it is let go, -1 for a buffer that is not one.
*/
PWIRE_API int pwire_frame(long slot, unsigned buffer, long long pts, unsigned long long seq, const int (*damage)[4], unsigned rects)
{
        struct pwire_connection *c = pwire_of(slot);
        unsigned char *meta, *act, *driver, *pos;
        long long now;
        unsigned status;

        if (!c || buffer >= c->buffers)
                return -1;
        now = pwire.host->now(pwire.host->context);
        if (!pwire_ready(slot, now))
        {
                c->drops++;
                return 0;
        }
        // the frame before, never taken (the io area still has it): the source's again
        status = __atomic_load_n((unsigned *)(c->mem + c->io_at), __ATOMIC_ACQUIRE);
        if (status == PWIRE_STATUS_HAVE_DATA && c->last < c->buffers)
                c->held &= ~(1ull << c->last);
        pwire_held(slot);
        meta = c->meta + (long)buffer * c->meta_size;
        for (unsigned k = 0; k < c->meta_count; k++)
        {
                unsigned type = c->meta_order[k], size = pwire_meta_size(c, type);

                pwire_zero(meta, size);
                if (type == PWIRE_META_HEADER)
                        pwire_put64(meta + 8, (unsigned long long)pts), pwire_put64(meta + 24, seq);
                else if (type == PWIRE_META_CROP)
                        pwire_put32(meta + 8, c->format.width), pwire_put32(meta + 12, c->format.height);
                else if (type == PWIRE_META_DAMAGE)
                {
                        unsigned room = size / 16, n = rects < room ? rects : 0; // too many to tell: the whole frame
                        static const int whole[1][4] = {{0, 0, 0, 0}};

                        if (!n)
                                damage = whole, n = 1;
                        for (unsigned r = 0; r < n; r++)
                        {
                                int x = damage[r][0], y = damage[r][1], w = damage[r][2], h = damage[r][3];

                                if (damage == whole)
                                        w = (int)c->format.width, h = (int)c->format.height;
                                pwire_put32(meta + 16 * r, (unsigned)x), pwire_put32(meta + 16 * r + 4, (unsigned)y);
                                pwire_put32(meta + 16 * r + 8, (unsigned)w), pwire_put32(meta + 16 * r + 12, (unsigned)h);
                        }
                }
                meta += (size + 7) & ~7u;
        }
        // the chunk: the whole slot, the source's stride
        pwire_put32(meta, 0), pwire_put32(meta + 4, c->source.stride * c->source.height), pwire_put32(meta + 8, c->source.stride), pwire_put32(meta + 12, 0);
        c->held |= 1ull << buffer, c->last = buffer;
        // the driver's activation, for the client's word that it is done; its position, which the client's node reads
        act = c->mem + c->act_at, driver = c->mem + c->driver_at, pos = driver + PWIRE_ACT_POSITION;
        pwire_put32(driver + PWIRE_ACT_STATE_REQUIRED, 1), pwire_put32(driver + PWIRE_ACT_STATE_PENDING, 1);
        __atomic_store_n((unsigned *)(driver + PWIRE_ACT_STATUS), PWIRE_ACT_NOT_TRIGGERED, __ATOMIC_RELEASE);
        {
                long long period = 1000000000ll / (c->format.rate ? c->format.rate : 60);
                double one = 1.0;
                unsigned long long bits;

                pwire_copy(&bits, &one, 8);
                pwire_copy(pos + PWIRE_CLOCK_NAME, "moonwater.cast", 15);
                pwire_put64(pos + PWIRE_CLOCK_NSEC, (unsigned long long)now);
                pwire_put32(pos + PWIRE_CLOCK_RATE, 1), pwire_put32(pos + PWIRE_CLOCK_RATE + 4, 1000000000u);
                pwire_put64(pos + PWIRE_CLOCK_POSITION, (unsigned long long)now);
                pwire_put64(pos + PWIRE_CLOCK_DURATION, (unsigned long long)period);
                pwire_put64(pos + PWIRE_CLOCK_RATE_DIFF, bits);
                pwire_put64(pos + PWIRE_CLOCK_NEXT, (unsigned long long)(now + period));
                pwire_put32(pos + PWIRE_CLOCK_TARGET_RATE, 1), pwire_put32(pos + PWIRE_CLOCK_TARGET_RATE + 4, 1000000000u);
                pwire_put64(pos + PWIRE_CLOCK_TARGET_DURATION, (unsigned long long)period);
                pwire_put32(pos + PWIRE_CLOCK_CYCLE, ++c->cycle);
                pwire_put32(pos + PWIRE_POSITION_VIDEO + 4, c->source.stride);
                pwire_put32(pos + PWIRE_POSITION_VIDEO + 8, c->format.width), pwire_put32(pos + PWIRE_POSITION_VIDEO + 12, c->format.height);
                pwire_put32(pos + PWIRE_POSITION_VIDEO + 16, c->format.rate ? c->format.rate : 60), pwire_put32(pos + PWIRE_POSITION_VIDEO + 20, 1);
                pwire_put32(pos + PWIRE_POSITION_STATE, 2); // running
                pwire_put32(pos + PWIRE_POSITION_SEGMENTS, 1);
                pwire_put64(pos + PWIRE_SEGMENT_RATE, bits);
        }
        // the io area, then the client's activation, then its eventfd
        pwire_put32(c->mem + c->io_at + 4, buffer);
        __atomic_store_n((unsigned *)(c->mem + c->io_at), PWIRE_STATUS_HAVE_DATA, __ATOMIC_RELEASE);
        pwire_put64(act + PWIRE_ACT_SIGNAL_TIME, (unsigned long long)now);
        __atomic_store_n((unsigned *)(act + PWIRE_ACT_STATUS), PWIRE_ACT_TRIGGERED, __ATOMIC_RELEASE);
        pwire.host->signal(pwire.host->context, c->trigger_fd);
        c->last_frame = now, c->frames++;
        return 1;
}

// The frames told and let go.
PWIRE_API void pwire_counts(long slot, unsigned long *frames, unsigned long *drops)
{
        struct pwire_connection *c = pwire_of(slot);

        *frames = c ? c->frames : 0, *drops = c ? c->drops : 0;
}

#endif /* PWIRE_INCLUDED */
