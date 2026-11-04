/*
        A client of the sound server for the programs that control it: the mixer, and whatever else of the person's session
        changes a program's volume or mute. It speaks the PulseAudio native protocol (src/audio/pulse.c is the other end and
        lends it the writers and readers of the tag structure) and is synchronous: one request at a time, and its answer
        waited for at most CTL_TIMEOUT_MS, so a server that stalls costs a window a fraction of a second and not its life.

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

#ifndef AUDIO_PULSE_INCLUDED
#error "control.c follows pulse.c, whose writers and readers it uses"
#endif

#define CTL_TIMEOUT_MS 300
#define CTL_FRAME 20480         // the most a request or an answer here is: a stream's answer holds its whole property list
#define CTL_CACHE 16            // streams whose channel count is kept
#define CTL_NAME "Moonwater mixer"
#define CTL_VOLUME_MAX 0x20000ul // 200%, the server's own limit

// What a call answers: 0 or more for success, or one of these.
#define CTL_FAILED (-1)  // no server, a connection lost, no answer in time, or an answer that is none
#define CTL_ACCESS (-2)  // the server refuses: the stream is not ours to change
#define CTL_NOENTITY (-3) // the stream is gone
#define CTL_INVALID (-4) // the server did not understand what was asked

/*
        The two things a connection is. send puts all count bytes on it or fails (a negative number); receive takes exactly
        count bytes, waiting at most timeout_ms for each part of them, and fails (a negative number) when it cannot.
*/
struct ctl_transport
{
        void *context;
        long (*send)(void *context, const unsigned char *bytes, long count);
        long (*receive)(void *context, unsigned char *bytes, long count, long timeout_ms);
};

struct ctl_input
{
        unsigned long index;
        long channels;
        unsigned long volume;  // the first channel's
        int muted;
};

struct ctl
{
        struct ctl_transport transport;
        int open;                     // the handshake is done
        unsigned long tag;
        struct { unsigned long index; long channels; } kept[CTL_CACHE];
        int kept_next;
        unsigned char frame[CTL_FRAME + 20];
};

static void ctl_be32(unsigned char *to, unsigned long v)
{
        to[0] = (unsigned char)(v >> 24), to[1] = (unsigned char)(v >> 16), to[2] = (unsigned char)(v >> 8), to[3] = (unsigned char)v;
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

        ctl_be32(frame, (unsigned long)length);
        ctl_be32(frame + 4, PULSE_INVALID); // a command is on the channel -1
        ctl_be32(frame + 8, 0), ctl_be32(frame + 12, 0), ctl_be32(frame + 16, 0);
        if (c->transport.send(c->transport.context, frame, 20 + length) < 0)
        {
                c->open = 0;
                return CTL_FAILED;
        }
        // The answer is the next command frame with this tag; whatever else comes (a subscription's event, a stray data frame) is read past.
        for (int frames = 0; frames < 64; frames++)
        {
                unsigned long size, channel, command, answered;

                if (c->transport.receive(c->transport.context, frame, 20, CTL_TIMEOUT_MS) < 0)
                        break;
                size = pulse_be32(frame), channel = pulse_be32(frame + 4);
                if (size > CTL_FRAME)
                        break;
                if (size && c->transport.receive(c->transport.context, frame + 20, (long)size, CTL_TIMEOUT_MS) < 0)
                        break;
                if (channel != PULSE_INVALID || size < 10)
                        continue;
                command = pulse_be32(frame + 21), answered = pulse_be32(frame + 26);
                if (frame[20] != 'L' || frame[25] != 'L' || answered != tag)
                        continue;
                if (command == PULSE_COMMAND_REPLY)
                        return (long)size;
                if (command == PULSE_COMMAND_ERROR && size >= 15 && frame[30] == 'L')
                {
                        unsigned long code = pulse_be32(frame + 31);

                        return code == PULSE_ERR_ACCESS ? CTL_ACCESS : code == PULSE_ERR_NOENTITY ? CTL_NOENTITY
                               : code == PULSE_ERR_INVALID ? CTL_INVALID : CTL_FAILED;
                }
                break;
        }
        c->open = 0;
        return CTL_FAILED;
}

// A request begun: its writer over the frame's payload, the command and a fresh tag put in.
static void ctl_begin(struct ctl *c, struct pulse_writer *w, unsigned long command)
{
        c->tag++;
        w->p = c->frame + 20;
        w->cap = CTL_FRAME;
        w->len = 0;
        w->bad = 0;
        pulse_w_u32(w, command);
        pulse_w_u32(w, c->tag);
}

// The handshake: AUTH at the newest version (the cookie is nothing here), then the client's name. 0, or CTL_FAILED.
static long ctl_start(struct ctl *c)
{
        struct pulse_writer w;
        unsigned char cookie[256];
        struct pulse_props props = {0};
        long got;

        c->open = 0;
        c->tag = 0;
        c->kept_next = 0;
        for (int i = 0; i < CTL_CACHE; i++)
                c->kept[i].index = PULSE_INVALID;
        for (int i = 0; i < 256; i++)
                cookie[i] = 0;
        ctl_begin(c, &w, PULSE_COMMAND_AUTH);
        pulse_w_u32(&w, PULSE_VERSION);
        pulse_w_byte(&w, 'x');
        pulse_w_be32(&w, 256);
        pulse_w_raw(&w, cookie, 256);
        if (w.bad || (got = ctl_exchange(c, w.len)) < 0)
                return CTL_FAILED;
        c->open = 1;
        ctl_begin(c, &w, PULSE_COMMAND_SET_CLIENT_NAME);
        pulse_props_set(&props, "application.name", (const unsigned char *)CTL_NAME, (long)sizeof(CTL_NAME));
        pulse_w_proplist(&w, &props);
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
static void ctl_skip_usec(struct pulse_reader *r)
{
        if (r->bad || r->length - r->at < 9 || r->p[r->at] != 'U')
        {
                r->bad = 1;
                return;
        }
        r->at += 9;
}

/*
        What the server says of a stream: its channel count (kept for the volume calls), the first channel's volume and the mute.
        0, or CTL_NOENTITY for a stream that is gone, CTL_ACCESS for one that is not ours to see, CTL_FAILED.
*/
static long ctl_get_input(struct ctl *c, unsigned long index, struct ctl_input *into)
{
        struct pulse_writer w;
        struct pulse_reader r;
        long size = 0, format, channels, rate, count;
        unsigned long volume[PULSE_CHANNEL_MAX];
        signed char map[AUDIO_CHANNELS_MAX];
        int isnull;

        if (!c->open)
                return CTL_FAILED;
        ctl_begin(c, &w, PULSE_COMMAND_GET_SINK_INPUT_INFO);
        pulse_w_u32(&w, index);
        if (w.bad || (size = ctl_exchange(c, w.len)) < 0)
                return size < 0 ? size : CTL_FAILED;
        r = (struct pulse_reader){c->frame + 20, size, 10, 0};
        into->index = pulse_r_u32(&r);
        (void)pulse_r_string(&r, &isnull);       // media.name
        (void)pulse_r_u32(&r);                   // the module
        (void)pulse_r_u32(&r);                   // the client
        (void)pulse_r_u32(&r);                   // the sink
        pulse_r_sample_spec(&r, &format, &channels, &rate);
        (void)pulse_r_channel_map(&r, map);
        count = pulse_r_cvolume(&r, volume);
        if (r.bad || count < 1 || into->index != index)
                return CTL_FAILED;
        // The tail: the two latencies, the resampler's name and the driver's, then the mute (version 11 on, which every client here is).
        for (int latency = 0; latency < 2 && !r.bad; latency++)
                ctl_skip_usec(&r);
        (void)pulse_r_string(&r, &isnull);
        (void)pulse_r_string(&r, &isnull);
        into->muted = pulse_r_bool(&r);
        if (r.bad)
                return CTL_FAILED;
        into->channels = count;
        into->volume = volume[0];
        ctl_keep_channels(c, index, count);
        return 0;
}

static long ctl_volume_once(struct ctl *c, unsigned long index, unsigned long volume, long channels)
{
        struct pulse_writer w;
        unsigned long all[PULSE_CHANNEL_MAX];

        if (channels < 1 || channels > PULSE_CHANNEL_MAX)
                return CTL_INVALID;
        for (long i = 0; i < channels; i++)
                all[i] = volume;
        ctl_begin(c, &w, PULSE_COMMAND_SET_SINK_INPUT_VOLUME);
        pulse_w_u32(&w, index);
        pulse_w_cvolume(&w, all, channels);
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

        if (!c->open)
                return CTL_FAILED;
        volume = volume > CTL_VOLUME_MAX ? CTL_VOLUME_MAX : volume;
        if (channels)
        {
                got = ctl_volume_once(c, index, volume, channels);
                if (got != CTL_INVALID)
                        return got < 0 ? got : 0;
        }
        // Not known, or refused for its count: asked again, and sent once more.
        if ((got = ctl_get_input(c, index, &info)) < 0)
                return got;
        got = ctl_volume_once(c, index, volume, info.channels);
        return got < 0 ? got : 0;
}

// A stream muted or not. The same answers as the volume's.
static long ctl_set_input_mute(struct ctl *c, unsigned long index, int muted)
{
        struct pulse_writer w;
        long got;

        if (!c->open)
                return CTL_FAILED;
        ctl_begin(c, &w, PULSE_COMMAND_SET_SINK_INPUT_MUTE);
        pulse_w_u32(&w, index);
        pulse_w_bool(&w, muted != 0);
        if (w.bad)
                return CTL_FAILED;
        got = ctl_exchange(c, w.len);
        return got < 0 ? got : 0;
}

// A stream ended: its program is told its stream is gone (the server only lets the person's own session do it).
static long ctl_kill_input(struct ctl *c, unsigned long index)
{
        struct pulse_writer w;
        long got;

        if (!c->open)
                return CTL_FAILED;
        ctl_begin(c, &w, PULSE_COMMAND_KILL_SINK_INPUT);
        pulse_w_u32(&w, index);
        if (w.bad)
                return CTL_FAILED;
        got = ctl_exchange(c, w.len);
        return got < 0 ? got : 0;
}

/*
        The master volume and mute of the sink the server plays on, named as the page names it (the person's session only: the server
        refuses any other uid). A volume is one number on both channels, up to 200%; the same answers as a stream's.
*/
static long ctl_set_sink_volume(struct ctl *c, const char *sink, unsigned long volume)
{
        struct pulse_writer w;
        unsigned long both[2];
        long got;

        if (!c->open)
                return CTL_FAILED;
        both[0] = both[1] = volume > CTL_VOLUME_MAX ? CTL_VOLUME_MAX : volume;
        ctl_begin(c, &w, PULSE_COMMAND_SET_SINK_VOLUME);
        pulse_w_u32(&w, PULSE_INVALID);
        pulse_w_string(&w, sink);
        pulse_w_cvolume(&w, both, 2);
        if (w.bad)
                return CTL_FAILED;
        got = ctl_exchange(c, w.len);
        return got < 0 ? got : 0;
}

static long ctl_set_sink_mute(struct ctl *c, const char *sink, int muted)
{
        struct pulse_writer w;
        long got;

        if (!c->open)
                return CTL_FAILED;
        ctl_begin(c, &w, PULSE_COMMAND_SET_SINK_MUTE);
        pulse_w_u32(&w, PULSE_INVALID);
        pulse_w_string(&w, sink);
        pulse_w_bool(&w, muted != 0);
        if (w.bad)
                return CTL_FAILED;
        got = ctl_exchange(c, w.len);
        return got < 0 ? got : 0;
}

#ifndef CTL_NO_SYSTEM
/* ---- the transport over a Unix socket ---- */

struct ctl_pollfd
{
        int handle;
        short events, returned;
};

struct ctl_socket
{
        int fd;
};

static long ctl_call(positive number, positive a, positive b, positive c, positive d, positive e, positive f)
{
        return (long)system_call_6(number, a, b, c, d, e, f);
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

static long ctl_socket_send(void *context, const unsigned char *bytes, long count)
{
        struct ctl_socket *s = context;
        long done = 0;

        while (done < count)
        {
                long got = ctl_call(syscall(sendto), (positive)s->fd, (positive)(bytes + done), (positive)(count - done), 0x4000, 0, 0); // MSG_NOSIGNAL

                if (got == -11) // EAGAIN: the server's socket is full; waited for a moment
                {
                        if (ctl_wait(s->fd, 0, CTL_TIMEOUT_MS) <= 0)
                                return -1;
                        continue;
                }
                if (got <= 0)
                        return -1;
                done += got;
        }
        return 0;
}

static long ctl_socket_receive(void *context, unsigned char *bytes, long count, long timeout_ms)
{
        struct ctl_socket *s = context;
        long done = 0;

        while (done < count)
        {
                long got = ctl_call(syscall(read), (positive)s->fd, (positive)(bytes + done), (positive)(count - done), 0, 0, 0);

                if (got == -11)
                {
                        if (ctl_wait(s->fd, 1, timeout_ms) <= 0)
                                return -1;
                        continue;
                }
                if (got <= 0)
                        return -1;
                done += got;
        }
        return 0;
}

static struct ctl_socket ctl_the_socket = {-1};

// A connection to the server at path, the handshake done. 0, or CTL_FAILED (no socket there, nobody listening, no answer).
static long ctl_connect(struct ctl *c, const char *path)
{
        unsigned char address[110];
        long length = 0, fd;

        for (int i = 0; i < (int)sizeof(address); i++)
                address[i] = 0;
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
        ctl_the_socket.fd = (int)fd;
        c->transport = (struct ctl_transport){&ctl_the_socket, ctl_socket_send, ctl_socket_receive};
        if (ctl_start(c) < 0)
        {
                ctl_call(syscall(close), (positive)fd, 0, 0, 0, 0, 0);
                ctl_the_socket.fd = -1;
                return CTL_FAILED;
        }
        return 0;
}

static void ctl_disconnect(struct ctl *c)
{
        if (ctl_the_socket.fd >= 0)
                ctl_call(syscall(close), (positive)ctl_the_socket.fd, 0, 0, 0, 0, 0);
        ctl_the_socket.fd = -1;
        c->open = 0;
}
#endif // CTL_NO_SYSTEM

#endif // AUDIO_CONTROL_INCLUDED
