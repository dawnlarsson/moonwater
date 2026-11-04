/*
        A client of the sound server for the programs that control it: the mixer, the compositor's volume keys and its tray, and
        whatever else of the person's session changes a program's volume or mute. It speaks the PulseAudio native protocol
        (src/audio/pulse.c is the other end) with the few writers and readers of the tag structure it needs written here, so that
        it stands alone and any file may include it without the server's engine before it, and is synchronous: one request at a
        time, and its answer waited for at most the connection's timeout (CTL_TIMEOUT_MS, which a caller that must not wait
        shortens: the compositor), so a server that stalls costs a window a fraction of a second and not its life.

        The program's volume has one number on the state page (the mean of its channels, 65536 is 100%), and the server wants
        as many numbers as the stream has channels, so a volume is sent as that many equal ones. The channels of a stream
        are asked once (GET_SINK_INPUT_INFO) and kept by the stream's index, which the server never gives to another stream;
        a count that the server refuses (a stream that changed under us can not, but a table that was kept past a restart of
        the server can) is asked again and the volume sent once more.

        What is changed here is the person's own session's: the server lets a client of uid 0 change any stream, and any other
        uid only its own, so a refusal (CTL_ACCESS) is a real answer and is handed back.

        The bytes go through two functions of the caller's (struct ctl_transport), so a check can feed the server's engine
        directly with no socket between; the transport over a Unix socket is below, for the shell and not for a hosted
        check (CTL_NO_SYSTEM).

        Dawn Larsson - Apache-2.0 license
        github.com/dawnlarsson/moonwater
*/
#ifndef AUDIO_CONTROL_INCLUDED
#define AUDIO_CONTROL_INCLUDED

#define CTL_SOCKET "/run/moonwater/pulse/native"
#define CTL_TIMEOUT_MS 300
#define CTL_FRAME 20480         // the most a request or an answer here is: a stream's answer holds its whole property list
#define CTL_CACHE 16            // streams whose channel count is kept
#define CTL_NAME "Moonwater mixer"
#define CTL_VOLUME_MAX 0x20000ul // 200%, the server's own limit
#define CTL_RECORD_KEY 0x40000000ul // added to a record stream's number in the table of kept channel counts: its numbers are its own, not a playback stream's

/* ---- the wire: PulseAudio's command numbers and errors, and the tag structure's writers and readers this needs ---- */

#define CTL_VERSION 35
#define CTL_NONE 0xffffffffu // an index that is none, and the channel a command is on
#define CTL_CHANNELS 32
#define CTL_COMMAND_ERROR 0
#define CTL_COMMAND_REPLY 2
#define CTL_COMMAND_AUTH 8
#define CTL_COMMAND_SET_CLIENT_NAME 9
#define CTL_COMMAND_GET_SINK_INPUT_INFO 29
#define CTL_COMMAND_SET_SINK_VOLUME 36
#define CTL_COMMAND_SET_SINK_INPUT_VOLUME 37
#define CTL_COMMAND_SET_SOURCE_VOLUME 38
#define CTL_COMMAND_SET_SINK_MUTE 39
#define CTL_COMMAND_SET_SOURCE_MUTE 40
#define CTL_COMMAND_KILL_SINK_INPUT 49
#define CTL_COMMAND_SET_SINK_INPUT_MUTE 69
#define CTL_COMMAND_SET_SOURCE_OUTPUT_VOLUME 98
#define CTL_COMMAND_SET_SOURCE_OUTPUT_MUTE 99
#define CTL_ERR_ACCESS 1
#define CTL_ERR_INVALID 3
#define CTL_ERR_NOENTITY 5

struct ctl_w
{
        unsigned char *p;
        long cap, len;
        int bad;
};

struct ctl_r
{
        const unsigned char *p;
        long length, at;
        int bad;
};

static unsigned long ctl_be(const unsigned char *p)
{
        return (unsigned long)p[0] << 24 | (unsigned long)p[1] << 16 | (unsigned long)p[2] << 8 | (unsigned long)p[3];
}

static void ctl_put_be(unsigned char *to, unsigned long v)
{
        to[0] = (unsigned char)(v >> 24), to[1] = (unsigned char)(v >> 16), to[2] = (unsigned char)(v >> 8), to[3] = (unsigned char)v;
}

static void ctl_w_raw(struct ctl_w *w, const void *data, long n)
{
        if (w->len + n > w->cap)
        {
                w->bad = 1;
                return;
        }
        memory_copy(w->p + w->len, data, (unsigned long)n);
        w->len += n;
}

static void ctl_w_byte(struct ctl_w *w, unsigned char c)
{
        ctl_w_raw(w, &c, 1);
}

static void ctl_w_be32(struct ctl_w *w, unsigned long v)
{
        unsigned char b[4];

        ctl_put_be(b, v);
        ctl_w_raw(w, b, 4);
}

static void ctl_w_u32(struct ctl_w *w, unsigned long v)
{
        ctl_w_byte(w, 'L');
        ctl_w_be32(w, v);
}

static void ctl_w_bool(struct ctl_w *w, int v)
{
        ctl_w_byte(w, v ? '1' : '0');
}

static long ctl_length(const char *s)
{
        return (long)string_length(s);
}

static void ctl_w_string(struct ctl_w *w, const char *s)
{
        if (!s)
        {
                ctl_w_byte(w, 'N');
                return;
        }
        ctl_w_byte(w, 't');
        ctl_w_raw(w, s, ctl_length(s) + 1);
}

static void ctl_w_cvolume(struct ctl_w *w, const unsigned long *volume, long channels)
{
        ctl_w_byte(w, 'v');
        ctl_w_byte(w, (unsigned char)channels);
        for (long i = 0; i < channels; i++)
                ctl_w_be32(w, volume[i]);
}

static int ctl_r_tag(struct ctl_r *r, unsigned char tag)
{
        if (r->bad || r->at >= r->length || r->p[r->at] != tag)
        {
                r->bad = 1;
                return 0;
        }
        r->at++;
        return 1;
}

static unsigned long ctl_r_u32(struct ctl_r *r)
{
        unsigned long v;

        if (!ctl_r_tag(r, 'L') || r->length - r->at < 4)
        {
                r->bad = 1;
                return 0;
        }
        v = ctl_be(r->p + r->at);
        r->at += 4;
        return v;
}

static int ctl_r_bool(struct ctl_r *r)
{
        if (r->bad || r->at >= r->length || (r->p[r->at] != '1' && r->p[r->at] != '0'))
        {
                r->bad = 1;
                return 0;
        }
        return r->p[r->at++] == '1';
}

// A string, passed over (the null string 'N' or one that ends in its null inside the answer).
static void ctl_r_string(struct ctl_r *r)
{
        void *nul;

        if (r->bad || r->at >= r->length)
        {
                r->bad = 1;
                return;
        }
        if (r->p[r->at] == 'N')
        {
                r->at++;
                return;
        }
        if (r->p[r->at] != 't')
        {
                r->bad = 1;
                return;
        }
        // The string's null, within the answer: none there is a string that runs past it.
        nul = memory_first_of((void *)(r->p + r->at + 1), 0, (unsigned long)(r->length - r->at - 1));
        if (!nul)
        {
                r->bad = 1;
                return;
        }
        r->at = (long)((const unsigned char *)nul - r->p) + 1;
}

// A field of fixed size behind its tag, passed over: a sample spec ('a', 6), a 64-bit time ('U', 8).
static void ctl_r_fixed(struct ctl_r *r, unsigned char tag, long bytes)
{
        if (!ctl_r_tag(r, tag) || r->length - r->at < bytes)
        {
                r->bad = 1;
                return;
        }
        r->at += bytes;
}

// A channel map ('m', a count, that many positions), passed over.
static void ctl_r_map(struct ctl_r *r)
{
        long n;

        if (!ctl_r_tag(r, 'm') || r->length - r->at < 1)
        {
                r->bad = 1;
                return;
        }
        n = r->p[r->at++];
        if (n > CTL_CHANNELS || r->length - r->at < n)
        {
                r->bad = 1;
                return;
        }
        r->at += n;
}

// A volume ('v', a count, that many 32-bit numbers): the count, and the first volume.
static long ctl_r_cvolume(struct ctl_r *r, unsigned long *first)
{
        long n;

        if (!ctl_r_tag(r, 'v') || r->length - r->at < 1)
        {
                r->bad = 1;
                return 0;
        }
        n = r->p[r->at++];
        if (n > CTL_CHANNELS || r->length - r->at < 4 * n)
        {
                r->bad = 1;
                return 0;
        }
        *first = n ? ctl_be(r->p + r->at) : 0;
        r->at += 4 * n;
        return n;
}

// What a call answers: 0 or more for success, or one of these.
#define CTL_FAILED (-1)  // no server, a connection lost, no answer in time, or an answer that is none
#define CTL_ACCESS (-2)  // the server refuses: the stream is not ours to change
#define CTL_NOENTITY (-3) // the stream is gone
#define CTL_INVALID (-4) // the server did not understand what was asked

/*
        What a connection is. send puts all count bytes on it, and receive takes exactly count bytes; each has timeout_ms in
        total, not per part (a call is given a budget and every wait of it is for what is left), and either fails (a negative
        number) when it cannot, the budget running out included. now is the time in milliseconds on a clock that does not go back.
*/
struct ctl_transport
{
        void *context;
        long (*send)(void *context, const unsigned char *bytes, long count, long timeout_ms);
        long (*receive)(void *context, unsigned char *bytes, long count, long timeout_ms);
        long (*now)(void *context);
};

struct ctl_input
{
        unsigned long index;
        long channels;
        unsigned long volume;  // the first channel's
        int muted;
};

// A connection's socket: its own, so two connections in one process (the compositor's and the mixer's) never share one.
struct ctl_socket
{
        int fd;
        int linked;                   // the descriptor is this connection's and is closed when it is let go
};

struct ctl
{
        struct ctl_transport transport;
        struct ctl_socket socket;
        long timeout_ms;              // how long a call may take, from its start (CTL_TIMEOUT_MS when 0); set before the connection is made
        long deadline;                // the transport's time the call in hand must be done by
        int open;                     // the handshake is done
        unsigned long tag;
        struct { unsigned long index; long channels; } kept[CTL_CACHE];
        int kept_next;
        unsigned char frame[CTL_FRAME + 20];
};

/*
        A call's budget: the connection's timeout (CTL_TIMEOUT_MS when 0) from the moment the call begins, and what of it is
        left. Every wait of a call is for what is left, so a server that answers a byte at a time, or with stray frames that
        never end, costs the call its budget and no more. Only a call that is made of its own sets the budget: the calls it
        makes in turn (the probe of a record stream's count, the stream's count asked before its volume) share it.
*/
// The transport's time. A transport with no clock (now is null) has the time 0 always, so its budget never runs out: the call is
// given its whole timeout on every wait, as a connection without a clock always had, and no null is ever called.
static long ctl_now(struct ctl *c)
{
        return c->transport.now ? c->transport.now(c->transport.context) : 0;
}

static void ctl_budget(struct ctl *c)
{
        c->deadline = ctl_now(c) + (c->timeout_ms ? c->timeout_ms : CTL_TIMEOUT_MS);
}

static long ctl_left(struct ctl *c)
{
        long left = c->deadline - ctl_now(c);

        return left > 0 ? left : 0;
}

/*
        One request and its answer. The request's payload is in c->frame at 20 on (its descriptor is made here), length bytes;
        the answer's payload is left in c->frame at 20 on and its length answered: CTL_FAILED when the connection broke or the
        server said nothing in time, or when it was not an answer; CTL_ACCESS, CTL_NOENTITY or CTL_INVALID for the server's
        error of that name (the others are CTL_FAILED); else the length of the answer, which begins after the command and the
        tag (the reader is past them).
*/
static long ctl_exchange(struct ctl *c, long length)
{
        unsigned char *frame = c->frame;
        unsigned long tag = c->tag;

        ctl_put_be(frame, (unsigned long)length);
        ctl_put_be(frame + 4, CTL_NONE); // a command is on the channel -1
        ctl_put_be(frame + 8, 0), ctl_put_be(frame + 12, 0), ctl_put_be(frame + 16, 0);
        if (c->transport.send(c->transport.context, frame, 20 + length, ctl_left(c)) < 0)
        {
                c->open = 0;
                return CTL_FAILED;
        }
        // The answer is the next command frame with this tag; whatever else comes (a subscription's event, a stray data frame) is read past.
        for (int frames = 0; frames < 64; frames++)
        {
                unsigned long size, channel, command, answered;

                if (c->transport.receive(c->transport.context, frame, 20, ctl_left(c)) < 0)
                        break;
                size = ctl_be(frame), channel = ctl_be(frame + 4);
                if (size > CTL_FRAME)
                        break;
                if (size && c->transport.receive(c->transport.context, frame + 20, (long)size, ctl_left(c)) < 0)
                        break;
                if (channel != CTL_NONE || size < 10)
                        continue;
                command = ctl_be(frame + 21), answered = ctl_be(frame + 26);
                if (frame[20] != 'L' || frame[25] != 'L' || answered != tag)
                        continue;
                if (command == CTL_COMMAND_REPLY)
                        return (long)size;
                if (command == CTL_COMMAND_ERROR && size >= 15 && frame[30] == 'L')
                {
                        unsigned long code = ctl_be(frame + 31);

                        return code == CTL_ERR_ACCESS ? CTL_ACCESS : code == CTL_ERR_NOENTITY ? CTL_NOENTITY
                               : code == CTL_ERR_INVALID ? CTL_INVALID : CTL_FAILED;
                }
                break;
        }
        c->open = 0;
        return CTL_FAILED;
}

// A request begun: its writer over the frame's payload, the command and a fresh tag put in.
static void ctl_begin(struct ctl *c, struct ctl_w *w, unsigned long command)
{
        c->tag++;
        w->p = c->frame + 20;
        w->cap = CTL_FRAME;
        w->len = 0;
        w->bad = 0;
        ctl_w_u32(w, command);
        ctl_w_u32(w, c->tag);
}

// The handshake: AUTH at the newest version (the cookie is nothing here), then the client's name. 0, or CTL_FAILED.
static long ctl_start(struct ctl *c)
{
        struct ctl_w w;
        unsigned char cookie[256];
        long got;

        c->open = 0;
        c->tag = 0;
        c->kept_next = 0;
        for (int i = 0; i < CTL_CACHE; i++)
                c->kept[i].index = CTL_NONE;
        memory_zero(cookie, sizeof(cookie));
        ctl_budget(c);
        ctl_begin(c, &w, CTL_COMMAND_AUTH);
        ctl_w_u32(&w, CTL_VERSION);
        ctl_w_byte(&w, 'x');
        ctl_w_be32(&w, 256);
        ctl_w_raw(&w, cookie, 256);
        if (w.bad || (got = ctl_exchange(c, w.len)) < 0)
                return CTL_FAILED;
        c->open = 1;
        ctl_begin(c, &w, CTL_COMMAND_SET_CLIENT_NAME);
        // The property list: application.name, its value a string with its null, and the end.
        ctl_w_byte(&w, 'P');
        ctl_w_string(&w, "application.name");
        ctl_w_u32(&w, (unsigned long)sizeof(CTL_NAME));
        ctl_w_byte(&w, 'x');
        ctl_w_be32(&w, (unsigned long)sizeof(CTL_NAME));
        ctl_w_raw(&w, CTL_NAME, (long)sizeof(CTL_NAME));
        ctl_w_byte(&w, 'N');
        if (w.bad || ctl_exchange(c, w.len) < 0)
        {
                c->open = 0;
                return CTL_FAILED;
        }
        c->open = 1;
        return 0;
}

// Whether a connection is there to be used.
static int ctl_ready(const struct ctl *c)
{
        return c->open;
}

// A stream's channels as they were kept, or 0.
static long ctl_kept_channels(const struct ctl *c, unsigned long index)
{
        for (int i = 0; i < CTL_CACHE; i++)
                if (c->kept[i].index == index)
                        return c->kept[i].channels;
        return 0;
}

static void ctl_keep_channels(struct ctl *c, unsigned long index, long channels)
{
        for (int i = 0; i < CTL_CACHE; i++)
                if (c->kept[i].index == index)
                {
                        c->kept[i].channels = channels;
                        return;
                }
        c->kept[c->kept_next].index = index;
        c->kept[c->kept_next].channels = channels;
        c->kept_next = (c->kept_next + 1) % CTL_CACHE;
}

// A 64-bit time of the tag structure ('U'), passed over.
/*
        What the server says of a stream: its channel count (kept for the volume calls), the first channel's volume and the mute.
        0, or CTL_NOENTITY for a stream that is gone, CTL_ACCESS for one that is not ours to see, CTL_FAILED.
*/
static long ctl_get_input_now(struct ctl *c, unsigned long index, struct ctl_input *into)
{
        struct ctl_w w;
        struct ctl_r r;
        long size = 0, count;
        unsigned long volume;

        if (!c->open)
                return CTL_FAILED;
        ctl_begin(c, &w, CTL_COMMAND_GET_SINK_INPUT_INFO);
        ctl_w_u32(&w, index);
        if (w.bad || (size = ctl_exchange(c, w.len)) < 0)
                return size < 0 ? size : CTL_FAILED;
        r = (struct ctl_r){c->frame + 20, size, 10, 0};
        into->index = ctl_r_u32(&r);
        ctl_r_string(&r);                        // media.name
        (void)ctl_r_u32(&r);                     // the module
        (void)ctl_r_u32(&r);                     // the client
        (void)ctl_r_u32(&r);                     // the sink
        ctl_r_fixed(&r, 'a', 6);                 // the sample spec
        ctl_r_map(&r);
        count = ctl_r_cvolume(&r, &volume);
        if (r.bad || count < 1 || into->index != index)
                return CTL_FAILED;
        // The tail: the two latencies, the resampler's name and the driver's, then the mute (version 11 on, which every client here is).
        for (int latency = 0; latency < 2 && !r.bad; latency++)
                ctl_r_fixed(&r, 'U', 8);
        ctl_r_string(&r);
        ctl_r_string(&r);
        into->muted = ctl_r_bool(&r);
        if (r.bad)
                return CTL_FAILED;
        into->channels = count;
        into->volume = volume;
        ctl_keep_channels(c, index, count);
        return 0;
}

static long ctl_get_input(struct ctl *c, unsigned long index, struct ctl_input *into)
{
        ctl_budget(c);
        return ctl_get_input_now(c, index, into);
}

static long ctl_volume_once(struct ctl *c, unsigned long command, unsigned long index, unsigned long volume, long channels)
{
        struct ctl_w w;
        unsigned long all[CTL_CHANNELS];

        if (channels < 1 || channels > CTL_CHANNELS)
                return CTL_INVALID;
        for (long i = 0; i < channels; i++)
                all[i] = volume;
        ctl_begin(c, &w, command);
        ctl_w_u32(&w, index);
        ctl_w_cvolume(&w, all, channels);
        if (w.bad)
                return CTL_FAILED;
        return ctl_exchange(c, w.len);
}

/*
        A stream's volume (65536 is 100%, up to 200%), the same on every channel. 0 when the server took it; CTL_NOENTITY for
        a stream that is gone, CTL_ACCESS for one that is not ours, CTL_FAILED when there is no server.
*/
static long ctl_set_input_volume(struct ctl *c, unsigned long index, unsigned long volume)
{
        long channels = ctl_kept_channels(c, index), got;
        struct ctl_input info;

        ctl_budget(c);
        if (!c->open)
                return CTL_FAILED;
        volume = volume > CTL_VOLUME_MAX ? CTL_VOLUME_MAX : volume;
        if (channels)
        {
                got = ctl_volume_once(c, CTL_COMMAND_SET_SINK_INPUT_VOLUME, index, volume, channels);
                if (got != CTL_INVALID)
                        return got < 0 ? got : 0;
        }
        // Not known, or refused for its count: asked again, and sent once more.
        if ((got = ctl_get_input_now(c, index, &info)) < 0)
                return got;
        got = ctl_volume_once(c, CTL_COMMAND_SET_SINK_INPUT_VOLUME, index, volume, info.channels);
        return got < 0 ? got : 0;
}

/*
        A record stream's volume, by the number the page gives it (record[].index): the same on every channel, and 200% the most. The
        count of its channels is not on the page, and the server's answer about a record stream is long (the list of its properties
        comes before its volume), so it is found by sending: the count that was kept for it, else the counts a capture has (two, one, four,
        six, eight) in turn until the server takes one. 0 when it did; CTL_NOENTITY for a stream that is gone, CTL_ACCESS for one that is not
        ours, CTL_INVALID when no count was taken, CTL_FAILED when there is no server.
*/
static long ctl_set_output_volume(struct ctl *c, unsigned long index, unsigned long volume)
{
        static const signed char guess[] = {2, 1, 4, 6, 8};
        long channels = ctl_kept_channels(c, index + CTL_RECORD_KEY), got = CTL_INVALID;

        ctl_budget(c);
        if (!c->open)
                return CTL_FAILED;
        volume = volume > CTL_VOLUME_MAX ? CTL_VOLUME_MAX : volume;
        if (channels && (got = ctl_volume_once(c, CTL_COMMAND_SET_SOURCE_OUTPUT_VOLUME, index, volume, channels)) != CTL_INVALID)
                return got < 0 ? got : 0;
        for (unsigned int k = 0; k < sizeof(guess); k++)
        {
                if (guess[k] == channels)
                        continue;
                got = ctl_volume_once(c, CTL_COMMAND_SET_SOURCE_OUTPUT_VOLUME, index, volume, guess[k]);
                if (got == CTL_INVALID)
                        continue;
                if (got >= 0)
                        ctl_keep_channels(c, index + CTL_RECORD_KEY, guess[k]);
                return got < 0 ? got : 0;
        }
        return CTL_INVALID;
}

// A stream muted or not, by the command that names its kind. The same answers as the volume's.
static long ctl_stream_mute(struct ctl *c, unsigned long command, unsigned long index, int muted)
{
        struct ctl_w w;
        long got;

        ctl_budget(c);
        if (!c->open)
                return CTL_FAILED;
        ctl_begin(c, &w, command);
        ctl_w_u32(&w, index);
        ctl_w_bool(&w, muted != 0);
        if (w.bad)
                return CTL_FAILED;
        got = ctl_exchange(c, w.len);
        return got < 0 ? got : 0;
}

static long ctl_set_input_mute(struct ctl *c, unsigned long index, int muted)
{
        return ctl_stream_mute(c, CTL_COMMAND_SET_SINK_INPUT_MUTE, index, muted);
}

// A record stream muted or not: the one program's microphone, which is the person's (root's) or the program's own.
static long ctl_set_output_mute(struct ctl *c, unsigned long index, int muted)
{
        return ctl_stream_mute(c, CTL_COMMAND_SET_SOURCE_OUTPUT_MUTE, index, muted);
}

// A stream ended: its program is told its stream is gone (the server only lets the person's own session do it).
static long ctl_kill_input(struct ctl *c, unsigned long index)
{
        struct ctl_w w;
        long got;

        ctl_budget(c);
        if (!c->open)
                return CTL_FAILED;
        ctl_begin(c, &w, CTL_COMMAND_KILL_SINK_INPUT);
        ctl_w_u32(&w, index);
        if (w.bad)
                return CTL_FAILED;
        got = ctl_exchange(c, w.len);
        return got < 0 ? got : 0;
}

/*
        The master volume and mute of the sink the server plays on and of the source programs record from (the microphone), each
        named as the page names it (the person's session only: the server refuses any other uid). A volume is one number on both
        channels, up to 200%; the same answers as a stream's.
*/
static long ctl_device_volume(struct ctl *c, unsigned long command, const char *name, unsigned long volume)
{
        struct ctl_w w;
        unsigned long both[2];
        long got;

        ctl_budget(c);
        if (!c->open)
                return CTL_FAILED;
        both[0] = both[1] = volume > CTL_VOLUME_MAX ? CTL_VOLUME_MAX : volume;
        ctl_begin(c, &w, command);
        ctl_w_u32(&w, CTL_NONE);
        ctl_w_string(&w, name);
        ctl_w_cvolume(&w, both, 2);
        if (w.bad)
                return CTL_FAILED;
        got = ctl_exchange(c, w.len);
        return got < 0 ? got : 0;
}

static long ctl_device_mute(struct ctl *c, unsigned long command, const char *name, int muted)
{
        struct ctl_w w;
        long got;

        ctl_budget(c);
        if (!c->open)
                return CTL_FAILED;
        ctl_begin(c, &w, command);
        ctl_w_u32(&w, CTL_NONE);
        ctl_w_string(&w, name);
        ctl_w_bool(&w, muted != 0);
        if (w.bad)
                return CTL_FAILED;
        got = ctl_exchange(c, w.len);
        return got < 0 ? got : 0;
}

static long ctl_set_sink_volume(struct ctl *c, const char *sink, unsigned long volume)
{
        return ctl_device_volume(c, CTL_COMMAND_SET_SINK_VOLUME, sink, volume);
}

static long ctl_set_sink_mute(struct ctl *c, const char *sink, int muted)
{
        return ctl_device_mute(c, CTL_COMMAND_SET_SINK_MUTE, sink, muted);
}

static long ctl_set_source_volume(struct ctl *c, const char *source, unsigned long volume)
{
        return ctl_device_volume(c, CTL_COMMAND_SET_SOURCE_VOLUME, source, volume);
}

static long ctl_set_source_mute(struct ctl *c, const char *source, int muted)
{
        return ctl_device_mute(c, CTL_COMMAND_SET_SOURCE_MUTE, source, muted);
}

#ifndef CTL_NO_SYSTEM
/* ---- the transport over a Unix socket ---- */

struct ctl_pollfd
{
        int handle;
        short events, returned;
};

static long ctl_call(positive number, positive a, positive b, positive c, positive d, positive e, positive f)
{
        return (long)system_call_6(number, a, b, c, d, e, f);
}

// The clock of the transport: CLOCK_MONOTONIC in milliseconds.
static long ctl_socket_now(void *context)
{
        long at[2] = {0, 0};

        (void)context;
        ctl_call(syscall(clock_gettime), 1, (positive)at, 0, 0, 0, 0);
        return at[0] * 1000 + at[1] / 1000000;
}

// Waits until the socket may be read (readable) or written, at most timeout_ms: 1 when it may, 0 when not in time, -1 when it is broken.
static long ctl_wait(int fd, int readable, long timeout_ms)
{
        short want = (short)(readable ? 1 : 4); // POLLIN, POLLOUT
        struct ctl_pollfd watch = {fd, want, 0};
        long span[2] = {timeout_ms / 1000, (timeout_ms % 1000) * 1000000};
        long got = ctl_call(syscall(ppoll), (positive)&watch, 1, (positive)span, 0, 0, 0);

        if (got < 0)
                return got == -4 ? 0 : -1; // EINTR is a wait that did not come to anything
        if (got == 0)
                return 0;
        return (watch.returned & want) ? 1 : -1;
}

// Waits until the socket may be read (readable) or written, for what is left of the budget that ends at until: 1 when it may, else 0 or -1.
static long ctl_wait_until(struct ctl_socket *s, int readable, long until)
{
        long left = until - ctl_socket_now(s);

        return left > 0 ? ctl_wait(s->fd, readable, left) : 0;
}

// Puts all count bytes on the connection within timeout_ms in total, or fails (a negative number).
static long ctl_socket_send(void *context, const unsigned char *bytes, long count, long timeout_ms)
{
        struct ctl_socket *s = context;
        long done = 0, until = ctl_socket_now(s) + timeout_ms;

        while (done < count)
        {
                long got = ctl_call(syscall(sendto), (positive)s->fd, (positive)(bytes + done), (positive)(count - done), 0x4000, 0, 0); // MSG_NOSIGNAL

                if (got == -11) // EAGAIN: the server's socket is full; waited for, within the budget
                {
                        if (ctl_wait_until(s, 0, until) <= 0)
                                return -1;
                        continue;
                }
                if (got <= 0)
                        return -1;
                done += got;
        }
        return 0;
}

// Takes exactly count bytes from the connection within timeout_ms in total, or fails (a negative number).
static long ctl_socket_receive(void *context, unsigned char *bytes, long count, long timeout_ms)
{
        struct ctl_socket *s = context;
        long done = 0, until = ctl_socket_now(s) + timeout_ms;

        while (done < count)
        {
                long got = ctl_call(syscall(read), (positive)s->fd, (positive)(bytes + done), (positive)(count - done), 0, 0, 0);

                if (got == -11)
                {
                        if (ctl_wait_until(s, 1, until) <= 0)
                                return -1;
                        continue;
                }
                if (got <= 0)
                        return -1;
                done += got;
        }
        return 0;
}

static void ctl_disconnect(struct ctl *c)
{
        if (c->socket.linked)
                ctl_call(syscall(close), (positive)c->socket.fd, 0, 0, 0, 0, 0);
        c->socket.linked = 0;
        c->socket.fd = -1;
        c->open = 0;
}

// A connection to the server at path, the handshake done. 0, or CTL_FAILED (no socket there, nobody listening, no answer).
static long ctl_connect(struct ctl *c, const char *path)
{
        unsigned char address[110];
        long length = 0, fd;

        memory_zero(address, sizeof(address));
        address[0] = 1; // AF_UNIX
        while (path[length] && length < 100)
        {
                address[2 + length] = (unsigned char)path[length];
                length++;
        }
        fd = ctl_call(syscall(socket), 1, 1 | 0x800 | 0x80000, 0, 0, 0, 0); // SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC
        if (fd < 0)
                return CTL_FAILED;
        if (ctl_call(syscall(connect), (positive)fd, (positive)address, (positive)(2 + length + 1), 0, 0, 0) < 0)
        {
                ctl_call(syscall(close), (positive)fd, 0, 0, 0, 0, 0);
                return CTL_FAILED;
        }
        ctl_disconnect(c); // a connection made again replaces the one it had, and does not leak its socket
        c->socket.fd = (int)fd;
        c->socket.linked = 1;
        c->transport = (struct ctl_transport){&c->socket, ctl_socket_send, ctl_socket_receive, ctl_socket_now};
        if (ctl_start(c) < 0)
        {
                ctl_disconnect(c);
                return CTL_FAILED;
        }
        return 0;
}
#endif // CTL_NO_SYSTEM

#endif // AUDIO_CONTROL_INCLUDED
