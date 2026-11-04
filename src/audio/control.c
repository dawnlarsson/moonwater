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
#define CTL_COMMAND_SET_SINK_MUTE 39
#define CTL_COMMAND_KILL_SINK_INPUT 49
#define CTL_COMMAND_SET_SINK_INPUT_MUTE 69
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
        for (long i = 0; i < n; i++)
                w->p[w->len + i] = ((const unsigned char *)data)[i];
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
        long n = 0;

        while (s[n])
                n++;
        return n;
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
        long at;

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
        for (at = r->at + 1; at < r->length && r->p[at]; at++)
                ;
        if (at >= r->length)
        {
                r->bad = 1;
                return;
        }
        r->at = at + 1;
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
        long timeout_ms;              // how long an answer is waited for (CTL_TIMEOUT_MS when 0); set before the connection is made
        int open;                     // the handshake is done
        unsigned long tag;
        struct { unsigned long index; long channels; } kept[CTL_CACHE];
        int kept_next;
        unsigned char frame[CTL_FRAME + 20];
};

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
        long wait = c->timeout_ms ? c->timeout_ms : CTL_TIMEOUT_MS;

        ctl_put_be(frame, (unsigned long)length);
        ctl_put_be(frame + 4, CTL_NONE); // a command is on the channel -1
        ctl_put_be(frame + 8, 0), ctl_put_be(frame + 12, 0), ctl_put_be(frame + 16, 0);
        if (c->transport.send(c->transport.context, frame, 20 + length) < 0)
        {
                c->open = 0;
                return CTL_FAILED;
        }
        // The answer is the next command frame with this tag; whatever else comes (a subscription's event, a stray data frame) is read past.
        for (int frames = 0; frames < 64; frames++)
        {
                unsigned long size, channel, command, answered;

                if (c->transport.receive(c->transport.context, frame, 20, wait) < 0)
                        break;
                size = ctl_be(frame), channel = ctl_be(frame + 4);
                if (size > CTL_FRAME)
                        break;
                if (size && c->transport.receive(c->transport.context, frame + 20, (long)size, wait) < 0)
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
        for (int i = 0; i < 256; i++)
                cookie[i] = 0;
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
static long ctl_get_input(struct ctl *c, unsigned long index, struct ctl_input *into)
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

static long ctl_volume_once(struct ctl *c, unsigned long index, unsigned long volume, long channels)
{
        struct ctl_w w;
        unsigned long all[CTL_CHANNELS];

        if (channels < 1 || channels > CTL_CHANNELS)
                return CTL_INVALID;
        for (long i = 0; i < channels; i++)
                all[i] = volume;
        ctl_begin(c, &w, CTL_COMMAND_SET_SINK_INPUT_VOLUME);
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
        struct ctl_w w;
        long got;

        if (!c->open)
                return CTL_FAILED;
        ctl_begin(c, &w, CTL_COMMAND_SET_SINK_INPUT_MUTE);
        ctl_w_u32(&w, index);
        ctl_w_bool(&w, muted != 0);
        if (w.bad)
                return CTL_FAILED;
        got = ctl_exchange(c, w.len);
        return got < 0 ? got : 0;
}

// A stream ended: its program is told its stream is gone (the server only lets the person's own session do it).
static long ctl_kill_input(struct ctl *c, unsigned long index)
{
        struct ctl_w w;
        long got;

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
        The master volume and mute of the sink the server plays on, named as the page names it (the person's session only: the server
        refuses any other uid). A volume is one number on both channels, up to 200%; the same answers as a stream's.
*/
static long ctl_set_sink_volume(struct ctl *c, const char *sink, unsigned long volume)
{
        struct ctl_w w;
        unsigned long both[2];
        long got;

        if (!c->open)
                return CTL_FAILED;
        both[0] = both[1] = volume > CTL_VOLUME_MAX ? CTL_VOLUME_MAX : volume;
        ctl_begin(c, &w, CTL_COMMAND_SET_SINK_VOLUME);
        ctl_w_u32(&w, CTL_NONE);
        ctl_w_string(&w, sink);
        ctl_w_cvolume(&w, both, 2);
        if (w.bad)
                return CTL_FAILED;
        got = ctl_exchange(c, w.len);
        return got < 0 ? got : 0;
}

static long ctl_set_sink_mute(struct ctl *c, const char *sink, int muted)
{
        struct ctl_w w;
        long got;

        if (!c->open)
                return CTL_FAILED;
        ctl_begin(c, &w, CTL_COMMAND_SET_SINK_MUTE);
        ctl_w_u32(&w, CTL_NONE);
        ctl_w_string(&w, sink);
        ctl_w_bool(&w, muted != 0);
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
