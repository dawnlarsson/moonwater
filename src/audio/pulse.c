/*
        Moonwater's sound server's protocol: the PulseAudio native protocol, the part of it that libpulse's programs
        (Chromium, Firefox, mpv, SDL, ffmpeg, pavucontrol and pactl) use to play sound, and what the machine's mixer
        needs: one file that is the whole of what a client can say to the sound server, and what it says back.

        WHAT IT IS

        A client is a connection. This file is given the bytes it sent (pulse_feed) and gives the bytes to send back
        (pulse_out), and turns what it reads into the same things the mixing core (mix.c) works on: a playback stream
        is a voice with a ring the client's data is written to; the sink is a card's worth of mixing, run a block at a
        time by the service (pulse_sink_mix) when the card wants one. It touches no descriptor, no clock and no
        allocator of its own: it asks the host (struct pulse_host, which is also the seam the checks put a fake
        service behind). It runs in the sound server's process, in ring 3, and is not kernel code.

        THREAT MODEL

        Every byte and number a client sends is hostile: a length, an index, a rate, a string, a tag, the order of
        calls. Therefore: fixed tables only (a client's numbers are compared with a cap and are never the size of
        anything); a frame is read whole and its fields checked before anything changes; the first protocol error is
        fatal to that client (it is dropped, and the drop gives back everything it held in one place, pulse_drop); a
        client that does not read its socket never blocks the server (its output queue is bounded, and one that fills it
        is dropped); a stream that never writes only underruns; a client reads and changes its own streams only unless
        it is root (the person's session); and what a client names a stream is shown to the person, so strings are
        cleaned of control bytes and bad UTF-8 before anything displays them.

        WHAT IS SERVED

        Version 35, with no shared memory and no descriptors passed (the AUTH reply sets neither bit, so a client
        sends its data over the socket): the handshake, client names and properties, playback streams (data with
        seeks, REQUEST, UNDERFLOW, STARTED, drain, cork, flush, trigger, prebuf, latency, buffer attributes, rate
        updates, names and properties, moving to a sink), the lists and infos of sinks, sources (the sinks' monitors),
        clients and sink inputs (and empty lists of modules, cards and samples), subscription events, volumes and
        mutes, the default sink. What is not served is answered with the error a client expects (NOTIMPLEMENTED,
        NOENTITY, NOEXTENSION, ACCESS) and never ignored: a client waiting for an answer gets one.
*/
#ifndef AUDIO_PULSE_INCLUDED
#define AUDIO_PULSE_INCLUDED

#ifndef AUDIO_MIX_INCLUDED
#error "pulse.c follows mix.c, whose voices and sinks it drives"
#endif

/* ---- the caps ---- */

#define PULSE_VERSION 35
#define PULSE_CLIENTS 32
#define PULSE_PER_USER 8             // clients of one user who is not root
#define PULSE_STREAMS_PER_CLIENT 16
#define PULSE_STREAMS 64             // all clients together
#define PULSE_SINKS 8
#define PULSE_FRAME_COMMAND 16384    // the most a command frame may carry
#define PULSE_FRAME_DATA 131072      // and a data frame
#define PULSE_IN (PULSE_FRAME_DATA + 20)
#define PULSE_OUT 262144             // a client's output queue
#define PULSE_SCRATCH 131072         // a reply is built here before it is queued
#define PULSE_PROPS 32               // entries of one property list
#define PULSE_KEY 64
#define PULSE_VALUE 256
#define PULSE_STRING 1024            // the longest string a command may carry
#define PULSE_RING_MAX (1u << 20)    // a playback ring
#define PULSE_RING_TOTAL (32ul << 20) // and all of them
#define PULSE_NAME 64
#define PULSE_DESCRIPTION 96
#define PULSE_CHANNEL_MAX 32         // channels a map or a volume may name; only AUDIO_CHANNELS_MAX are played
#define PULSE_AUTH_USEC 5000000l     // a connection that has not said who it is by now is closed
#define PULSE_INVALID 0xffffffffu
#define PULSE_DRAIN_USEC 20000l

/* PulseAudio's numbers. */
#define PULSE_COMMAND_ERROR 0
#define PULSE_COMMAND_REPLY 2
#define PULSE_COMMAND_CREATE_PLAYBACK_STREAM 3
#define PULSE_COMMAND_DELETE_PLAYBACK_STREAM 4
#define PULSE_COMMAND_CREATE_RECORD_STREAM 5
#define PULSE_COMMAND_DELETE_RECORD_STREAM 6
#define PULSE_COMMAND_EXIT 7
#define PULSE_COMMAND_AUTH 8
#define PULSE_COMMAND_SET_CLIENT_NAME 9
#define PULSE_COMMAND_LOOKUP_SINK 10
#define PULSE_COMMAND_LOOKUP_SOURCE 11
#define PULSE_COMMAND_DRAIN_PLAYBACK_STREAM 12
#define PULSE_COMMAND_STAT 13
#define PULSE_COMMAND_GET_PLAYBACK_LATENCY 14
#define PULSE_COMMAND_CREATE_UPLOAD_STREAM 15
#define PULSE_COMMAND_DELETE_UPLOAD_STREAM 16
#define PULSE_COMMAND_FINISH_UPLOAD_STREAM 17
#define PULSE_COMMAND_PLAY_SAMPLE 18
#define PULSE_COMMAND_REMOVE_SAMPLE 19
#define PULSE_COMMAND_GET_SERVER_INFO 20
#define PULSE_COMMAND_GET_SINK_INFO 21
#define PULSE_COMMAND_GET_SINK_INFO_LIST 22
#define PULSE_COMMAND_GET_SOURCE_INFO 23
#define PULSE_COMMAND_GET_SOURCE_INFO_LIST 24
#define PULSE_COMMAND_GET_MODULE_INFO 25
#define PULSE_COMMAND_GET_MODULE_INFO_LIST 26
#define PULSE_COMMAND_GET_CLIENT_INFO 27
#define PULSE_COMMAND_GET_CLIENT_INFO_LIST 28
#define PULSE_COMMAND_GET_SINK_INPUT_INFO 29
#define PULSE_COMMAND_GET_SINK_INPUT_INFO_LIST 30
#define PULSE_COMMAND_GET_SOURCE_OUTPUT_INFO 31
#define PULSE_COMMAND_GET_SOURCE_OUTPUT_INFO_LIST 32
#define PULSE_COMMAND_GET_SAMPLE_INFO 33
#define PULSE_COMMAND_GET_SAMPLE_INFO_LIST 34
#define PULSE_COMMAND_SUBSCRIBE 35
#define PULSE_COMMAND_SET_SINK_VOLUME 36
#define PULSE_COMMAND_SET_SINK_INPUT_VOLUME 37
#define PULSE_COMMAND_SET_SOURCE_VOLUME 38
#define PULSE_COMMAND_SET_SINK_MUTE 39
#define PULSE_COMMAND_SET_SOURCE_MUTE 40
#define PULSE_COMMAND_CORK_PLAYBACK_STREAM 41
#define PULSE_COMMAND_FLUSH_PLAYBACK_STREAM 42
#define PULSE_COMMAND_TRIGGER_PLAYBACK_STREAM 43
#define PULSE_COMMAND_SET_DEFAULT_SINK 44
#define PULSE_COMMAND_SET_DEFAULT_SOURCE 45
#define PULSE_COMMAND_SET_PLAYBACK_STREAM_NAME 46
#define PULSE_COMMAND_SET_RECORD_STREAM_NAME 47
#define PULSE_COMMAND_KILL_CLIENT 48
#define PULSE_COMMAND_KILL_SINK_INPUT 49
#define PULSE_COMMAND_KILL_SOURCE_OUTPUT 50
#define PULSE_COMMAND_LOAD_MODULE 51
#define PULSE_COMMAND_UNLOAD_MODULE 52
#define PULSE_COMMAND_GET_RECORD_LATENCY 57
#define PULSE_COMMAND_CORK_RECORD_STREAM 58
#define PULSE_COMMAND_FLUSH_RECORD_STREAM 59
#define PULSE_COMMAND_PREBUF_PLAYBACK_STREAM 60
#define PULSE_COMMAND_REQUEST 61
#define PULSE_COMMAND_OVERFLOW 62
#define PULSE_COMMAND_UNDERFLOW 63
#define PULSE_COMMAND_PLAYBACK_STREAM_KILLED 64
#define PULSE_COMMAND_SUBSCRIBE_EVENT 66
#define PULSE_COMMAND_MOVE_SINK_INPUT 67
#define PULSE_COMMAND_MOVE_SOURCE_OUTPUT 68
#define PULSE_COMMAND_SET_SINK_INPUT_MUTE 69
#define PULSE_COMMAND_SUSPEND_SINK 70
#define PULSE_COMMAND_SUSPEND_SOURCE 71
#define PULSE_COMMAND_SET_PLAYBACK_STREAM_BUFFER_ATTR 72
#define PULSE_COMMAND_SET_RECORD_STREAM_BUFFER_ATTR 73
#define PULSE_COMMAND_UPDATE_PLAYBACK_STREAM_SAMPLE_RATE 74
#define PULSE_COMMAND_UPDATE_RECORD_STREAM_SAMPLE_RATE 75
#define PULSE_COMMAND_UPDATE_RECORD_STREAM_PROPLIST 80
#define PULSE_COMMAND_UPDATE_PLAYBACK_STREAM_PROPLIST 81
#define PULSE_COMMAND_UPDATE_CLIENT_PROPLIST 82
#define PULSE_COMMAND_REMOVE_RECORD_STREAM_PROPLIST 83
#define PULSE_COMMAND_REMOVE_PLAYBACK_STREAM_PROPLIST 84
#define PULSE_COMMAND_REMOVE_CLIENT_PROPLIST 85
#define PULSE_COMMAND_STARTED 86
#define PULSE_COMMAND_EXTENSION 87
#define PULSE_COMMAND_GET_CARD_INFO 88
#define PULSE_COMMAND_GET_CARD_INFO_LIST 89
#define PULSE_COMMAND_SET_CARD_PROFILE 90
#define PULSE_COMMAND_SET_SINK_PORT 96
#define PULSE_COMMAND_SET_SOURCE_PORT 97
#define PULSE_COMMAND_SET_SOURCE_OUTPUT_VOLUME 98
#define PULSE_COMMAND_SET_SOURCE_OUTPUT_MUTE 99
#define PULSE_COMMAND_SET_PORT_LATENCY_OFFSET 100
#define PULSE_COMMAND_ENABLE_SRBCHANNEL 101
#define PULSE_COMMAND_DISABLE_SRBCHANNEL 102
#define PULSE_COMMAND_REGISTER_MEMFD_SHMID 103
#define PULSE_COMMAND_SEND_OBJECT_MESSAGE 104

#define PULSE_ERR_ACCESS 1
#define PULSE_ERR_COMMAND 2
#define PULSE_ERR_INVALID 3
#define PULSE_ERR_NOENTITY 5
#define PULSE_ERR_VERSION 17
#define PULSE_ERR_NODATA 16
#define PULSE_ERR_TOOLARGE 18
#define PULSE_ERR_NOTSUPPORTED 19
#define PULSE_ERR_NOEXTENSION 21
#define PULSE_ERR_NOTIMPLEMENTED 23
#define PULSE_ERR_BUSY 26

// Subscription: the facilities (bits of the mask, numbers of the events) and the kinds of event.
#define PULSE_FACILITY_SINK 0
#define PULSE_FACILITY_SOURCE 1
#define PULSE_FACILITY_SINK_INPUT 2
#define PULSE_FACILITY_CLIENT 5
#define PULSE_FACILITY_SERVER 7
#define PULSE_EVENT_NEW 0x0000u
#define PULSE_EVENT_CHANGE 0x0010u
#define PULSE_EVENT_REMOVE 0x0020u

#define PULSE_DONE 0
#define PULSE_DEAD (-1)             // the client is to be dropped (the reason is counted)

/* What the engine asks of whoever runs it. */
struct pulse_host
{
        void *context;
        void *(*alloc)(void *context, long bytes);               // zeroed, or 0
        void (*release)(void *context, void *block, long bytes);
        long (*now)(void *context);                              // microseconds on a clock that only goes forward
        void (*real)(void *context, long *seconds, long *microseconds); // the wall clock
        // A stream's volume is remembered by the name of its program: ask on creation (1 if there is a memory and it is in *volume
        // and *mute), tell on change.
        int (*recall)(void *context, const char *name, unsigned long *volume, int *mute);
        void (*remember)(void *context, const char *name, unsigned long volume, int mute);
        // Something the host keeps (the master volume, the mute, the default sink) changed, or a stream began or ended.
        void (*changed)(void *context, int what, long sink);
};

#define PULSE_CHANGED_SINK_VOLUME 1
#define PULSE_CHANGED_DEFAULT 2
#define PULSE_CHANGED_STREAMS 3

struct pulse_props
{
        int count;
        struct
        {
                char key[PULSE_KEY];
                unsigned char value[PULSE_VALUE];
                int length;
        } entry[PULSE_PROPS];
};

struct pulse_stream
{
        int used, dying;
        long client;                    // slot of the client that made it, -1 when it is dying with no client
        unsigned long channel;          // the client's name for it, which data frames carry
        unsigned long index;            // the sink input's, which introspection and volume use
        long sink;
        struct pulse_props props;
        long format, channels, rate;
        signed char map[AUDIO_CHANNELS_MAX];
        long map_channels;              // channels the program named (up to PULSE_CHANNEL_MAX), the first AUDIO_CHANNELS_MAX played
        unsigned long volume[PULSE_CHANNEL_MAX];
        int muted, corked, trigger, underrun, adjust_latency, variable_rate, early_requests, no_move, owner_set;
        unsigned long maxlength, tlength, prebuf, minreq;
        unsigned long outstanding;      // bytes asked of the program and not yet written
        int drain_pending, drained, flushing;
        unsigned long drain_tag;
        long drain_at;
        unsigned long long underrun_for, playing_for;
        unsigned char *ring_memory;
        unsigned long ring_bytes;
        long uid, pid;
        struct audio_voice voice;
};

struct pulse_client
{
        int used;
        unsigned long index;
        long uid, pid;
        unsigned version;
        int authorized, root;
        long born;
        unsigned long subscribe;
        unsigned long next_channel;
        struct pulse_props props;
        long in_have;
        long out_head, out_tail;        // bytes in the queue are [out_head, out_tail) modulo nothing: a ring by counts
        unsigned char in[PULSE_IN];
        unsigned char out[PULSE_OUT];
};

struct pulse_sink
{
        int used, suspended;
        char name[PULSE_NAME], description[PULSE_DESCRIPTION];
        long rate;
        unsigned long volume[2];
        int muted;
        long delay_frames;              // what the card holds, as the host last read it
        unsigned long long played;      // frames mixed
        struct audio_sink mix;
};

struct pulse
{
        struct pulse_host host;
        struct pulse_client *client[PULSE_CLIENTS];
        struct pulse_stream stream[PULSE_STREAMS];
        struct pulse_sink sink[PULSE_SINKS];
        long default_sink;
        unsigned long next_client, next_input;
        unsigned long ring_total;
        unsigned char scratch[PULSE_SCRATCH];
        // What the checks count back to nothing.
        unsigned long clients_made, clients_dropped, streams_made, streams_freed, protocol_errors;
};

/* ---- reading a tag structure ---- */

struct pulse_reader
{
        const unsigned char *p;
        long length, at;
        int bad;
};

static unsigned long pulse_be32(const unsigned char *p)
{
        return (unsigned long)p[0] << 24 | (unsigned long)p[1] << 16 | (unsigned long)p[2] << 8 | (unsigned long)p[3];
}

static int pulse_tag(struct pulse_reader *r, unsigned char tag)
{
        if (r->bad || r->at >= r->length || r->p[r->at] != tag)
        {
                r->bad = 1;
                return 0;
        }
        r->at++;
        return 1;
}

static unsigned long pulse_r_u32(struct pulse_reader *r)
{
        unsigned long v;

        if (!pulse_tag(r, 'L') || r->length - r->at < 4)
        {
                r->bad = 1;
                return 0;
        }
        v = pulse_be32(r->p + r->at);
        r->at += 4;
        return v;
}

static unsigned long pulse_r_u8(struct pulse_reader *r)
{
        if (!pulse_tag(r, 'B') || r->length - r->at < 1)
        {
                r->bad = 1;
                return 0;
        }
        return r->p[r->at++];
}

static int pulse_r_bool(struct pulse_reader *r)
{
        if (r->bad || r->at >= r->length || (r->p[r->at] != '1' && r->p[r->at] != '0'))
        {
                r->bad = 1;
                return 0;
        }
        return r->p[r->at++] == '1';
}

// A string of the tag structure: NULL for the null string (and *isnull set), else a pointer into the frame, whose limit is checked.
static const char *pulse_r_string(struct pulse_reader *r, int *isnull)
{
        long start, limit;

        if (isnull)
                *isnull = 0;
        if (r->bad || r->at >= r->length)
        {
                r->bad = 1;
                return 0;
        }
        if (r->p[r->at] == 'N')
        {
                r->at++;
                if (isnull)
                        *isnull = 1;
                return 0;
        }
        if (r->p[r->at] != 't')
        {
                r->bad = 1;
                return 0;
        }
        start = r->at + 1;
        for (limit = start; limit < r->length && limit - start <= PULSE_STRING && r->p[limit]; limit++)
                ;
        if (limit >= r->length || limit - start > PULSE_STRING)
        {
                r->bad = 1;
                return 0;
        }
        r->at = limit + 1;
        return (const char *)(r->p + start);
}

static void pulse_r_sample_spec(struct pulse_reader *r, long *format, long *channels, long *rate)
{
        if (!pulse_tag(r, 'a') || r->length - r->at < 6)
        {
                r->bad = 1;
                return;
        }
        *format = r->p[r->at];
        *channels = r->p[r->at + 1];
        *rate = (long)pulse_be32(r->p + r->at + 2);
        r->at += 6;
}

static long pulse_r_channel_map(struct pulse_reader *r, signed char *map)
{
        long n;

        if (!pulse_tag(r, 'm') || r->length - r->at < 1)
        {
                r->bad = 1;
                return 0;
        }
        n = r->p[r->at++];
        if (n > PULSE_CHANNEL_MAX || r->length - r->at < n)
        {
                r->bad = 1;
                return 0;
        }
        for (long i = 0; i < n; i++)
                if (i < AUDIO_CHANNELS_MAX)
                        map[i] = (signed char)(r->p[r->at + i] < 64 ? r->p[r->at + i] : 63);
        r->at += n;
        return n;
}

static long pulse_r_cvolume(struct pulse_reader *r, unsigned long *volume)
{
        long n;

        if (!pulse_tag(r, 'v') || r->length - r->at < 1)
        {
                r->bad = 1;
                return 0;
        }
        n = r->p[r->at++];
        if (n > PULSE_CHANNEL_MAX || r->length - r->at < 4 * n)
        {
                r->bad = 1;
                return 0;
        }
        for (long i = 0; i < n; i++)
        {
                volume[i] = pulse_be32(r->p + r->at + 4 * i);
                if (volume[i] > AUDIO_VOLUME_MAX)
                        volume[i] = AUDIO_VOLUME_MAX;
        }
        r->at += 4 * n;
        return n;
}

static void pulse_r_timeval(struct pulse_reader *r, unsigned long *seconds, unsigned long *microseconds)
{
        if (!pulse_tag(r, 'T') || r->length - r->at < 8)
        {
                r->bad = 1;
                return;
        }
        *seconds = pulse_be32(r->p + r->at);
        *microseconds = pulse_be32(r->p + r->at + 4);
        r->at += 8;
}

// An arbitrary block of exactly n bytes.
static const unsigned char *pulse_r_arbitrary(struct pulse_reader *r, unsigned long n)
{
        const unsigned char *at;

        if (!pulse_tag(r, 'x') || r->length - r->at < 4 || pulse_be32(r->p + r->at) != n || (unsigned long)(r->length - r->at - 4) < n)
        {
                r->bad = 1;
                return 0;
        }
        at = r->p + r->at + 4;
        r->at += 4 + (long)n;
        return at;
}

static int pulse_r_eof(const struct pulse_reader *r)
{
        return !r->bad && r->at == r->length;
}

// A property key: printable ASCII, no space or control byte, and not empty.
static int pulse_key_ok(const char *key, long length)
{
        if (length < 1 || length >= PULSE_KEY)
                return 0;
        for (long i = 0; i < length; i++)
                if ((unsigned char)key[i] <= ' ' || (unsigned char)key[i] >= 127)
                        return 0;
        return 1;
}

static long pulse_len(const char *s)
{
        long n = 0;

        while (s[n])
                n++;
        return n;
}

static int pulse_equal(const char *a, const char *b)
{
        while (*a && *a == *b)
                a++, b++;
        return *a == *b;
}

static void pulse_copy(char *to, long room, const char *from)
{
        long i = 0;

        for (; i < room - 1 && from[i]; i++)
                to[i] = from[i];
        to[i] = 0;
}

static int pulse_props_set(struct pulse_props *props, const char *key, const unsigned char *value, long length)
{
        long k = pulse_len(key);

        if (!pulse_key_ok(key, k) || length < 0 || length > PULSE_VALUE)
                return 0;
        for (int i = 0; i < props->count; i++)
                if (pulse_equal(props->entry[i].key, key))
                {
                        __builtin_memcpy(props->entry[i].value, value, (unsigned long)length);
                        props->entry[i].length = (int)length;
                        return 1;
                }
        if (props->count >= PULSE_PROPS)
                return 0;
        pulse_copy(props->entry[props->count].key, PULSE_KEY, key);
        __builtin_memcpy(props->entry[props->count].value, value, (unsigned long)length);
        props->entry[props->count].length = (int)length;
        props->count++;
        return 1;
}

static void pulse_props_remove(struct pulse_props *props, const char *key)
{
        for (int i = 0; i < props->count; i++)
                if (pulse_equal(props->entry[i].key, key))
                {
                        props->entry[i] = props->entry[props->count - 1];
                        props->count--;
                        return;
                }
}

// PulseAudio's three ways to update a list: 0 replaces it, 1 adds what is not there, 2 adds and replaces.
static void pulse_props_update(struct pulse_props *to, const struct pulse_props *from, unsigned long mode)
{
        if (mode == 0)
                to->count = 0;
        for (int i = 0; i < from->count; i++)
        {
                int there = 0;

                if (mode == 1)
                        for (int k = 0; k < to->count; k++)
                                there |= pulse_equal(to->entry[k].key, from->entry[i].key);
                if (!there)
                        pulse_props_set(to, from->entry[i].key, from->entry[i].value, from->entry[i].length);
        }
}

// The text of a property, cleaned for showing: a string of its length, control bytes and bad UTF-8 turned into '?'. Answers its length.
static long pulse_props_text(const struct pulse_props *props, const char *key, char *to, long room)
{
        long n = 0;

        to[0] = 0;
        for (int i = 0; i < props->count; i++)
                if (pulse_equal(props->entry[i].key, key))
                {
                        const unsigned char *v = props->entry[i].value;
                        long length = props->entry[i].length;

                        for (long at = 0; at < length && v[at] && n < room - 4; at++)
                        {
                                unsigned char c = v[at];
                                long extra = c >= 0xf0 && c < 0xf5 ? 3 : c >= 0xe0 ? 2 : c >= 0xc2 ? 1 : 0;

                                if (c >= 0x20 && c < 0x7f)
                                        to[n++] = (char)c;
                                else if (c >= 0xc2 && c < 0xf5 && at + extra < length)
                                {
                                        int good = 1;

                                        for (long k = 1; k <= extra; k++)
                                                good &= (v[at + k] & 0xc0) == 0x80;
                                        // C1 controls and surrogates are not text.
                                        if (good && extra == 1 && c == 0xc2 && v[at + 1] < 0xa0)
                                                good = 0;
                                        if (good && extra == 2 && c == 0xed && v[at + 1] >= 0xa0)
                                                good = 0;
                                        if (good && extra == 2 && c == 0xe0 && v[at + 1] < 0xa0)
                                                good = 0;
                                        if (good)
                                        {
                                                for (long k = 0; k <= extra; k++)
                                                        to[n++] = (char)v[at + k];
                                                at += extra;
                                        }
                                        else
                                                to[n++] = '?';
                                }
                                else
                                        to[n++] = '?';
                        }
                        to[n] = 0;
                        return n;
                }
        return 0;
}

static void pulse_r_proplist(struct pulse_reader *r, struct pulse_props *props)
{
        if (!pulse_tag(r, 'P'))
                return;
        for (;;)
        {
                int isnull;
                const char *key = pulse_r_string(r, &isnull);
                unsigned long length;
                const unsigned char *value;

                if (r->bad || isnull)
                        return;
                length = pulse_r_u32(r);
                if (r->bad || length > 65536)
                {
                        r->bad = 1;
                        return;
                }
                value = pulse_r_arbitrary(r, length);
                if (r->bad)
                        return;
                // Whatever does not fit the caps is left out, and the program is not told: it is not an error to have too much to say.
                if (length <= PULSE_VALUE)
                        pulse_props_set(props, key, value, (long)length);
        }
}

/* ---- writing one ---- */

struct pulse_writer
{
        unsigned char *p;
        long cap, len;
        int bad;
};

static void pulse_w_raw(struct pulse_writer *w, const void *data, long n)
{
        if (w->len + n > w->cap)
        {
                w->bad = 1;
                return;
        }
        __builtin_memcpy(w->p + w->len, data, (unsigned long)n);
        w->len += n;
}

static void pulse_w_byte(struct pulse_writer *w, unsigned char c)
{
        pulse_w_raw(w, &c, 1);
}

static void pulse_w_be32(struct pulse_writer *w, unsigned long v)
{
        unsigned char b[4] = {(unsigned char)(v >> 24), (unsigned char)(v >> 16), (unsigned char)(v >> 8), (unsigned char)v};

        pulse_w_raw(w, b, 4);
}

static void pulse_w_u32(struct pulse_writer *w, unsigned long v)
{
        pulse_w_byte(w, 'L');
        pulse_w_be32(w, v);
}

// A volume on its own (the base volume of a device) has a tag of its own.
static void pulse_w_volume(struct pulse_writer *w, unsigned long v)
{
        pulse_w_byte(w, 'V');
        pulse_w_be32(w, v);
}

static void pulse_w_u8(struct pulse_writer *w, unsigned long v)
{
        pulse_w_byte(w, 'B');
        pulse_w_byte(w, (unsigned char)v);
}

static void pulse_w_bool(struct pulse_writer *w, int v)
{
        pulse_w_byte(w, v ? '1' : '0');
}

static void pulse_w_64(struct pulse_writer *w, unsigned char tag, long long v)
{
        pulse_w_byte(w, tag);
        pulse_w_be32(w, (unsigned long)((unsigned long long)v >> 32));
        pulse_w_be32(w, (unsigned long)((unsigned long long)v & 0xffffffffu));
}

static void pulse_w_string(struct pulse_writer *w, const char *s)
{
        if (!s)
        {
                pulse_w_byte(w, 'N');
                return;
        }
        pulse_w_byte(w, 't');
        pulse_w_raw(w, s, pulse_len(s) + 1);
}

static void pulse_w_sample_spec(struct pulse_writer *w, long format, long channels, long rate)
{
        pulse_w_byte(w, 'a');
        pulse_w_byte(w, (unsigned char)format);
        pulse_w_byte(w, (unsigned char)channels);
        pulse_w_be32(w, (unsigned long)rate);
}

static void pulse_w_channel_map(struct pulse_writer *w, const signed char *map, long channels)
{
        pulse_w_byte(w, 'm');
        pulse_w_byte(w, (unsigned char)channels);
        for (long i = 0; i < channels; i++)
                pulse_w_byte(w, (unsigned char)(i < AUDIO_CHANNELS_MAX ? map[i] : AUDIO_POSITION_AUX0 + i));
}

static void pulse_w_cvolume(struct pulse_writer *w, const unsigned long *volume, long channels)
{
        pulse_w_byte(w, 'v');
        pulse_w_byte(w, (unsigned char)channels);
        for (long i = 0; i < channels; i++)
                pulse_w_be32(w, volume[i]);
}

static void pulse_w_proplist(struct pulse_writer *w, const struct pulse_props *props)
{
        pulse_w_byte(w, 'P');
        for (int i = 0; i < props->count; i++)
        {
                pulse_w_string(w, props->entry[i].key);
                pulse_w_u32(w, (unsigned long)props->entry[i].length);
                pulse_w_byte(w, 'x');
                pulse_w_be32(w, (unsigned long)props->entry[i].length);
                pulse_w_raw(w, props->entry[i].value, props->entry[i].length);
        }
        pulse_w_byte(w, 'N');
}

static void pulse_w_format_info(struct pulse_writer *w)
{
        // PCM, with no properties: the only encoding served.
        pulse_w_byte(w, 'f');
        pulse_w_u8(w, 1);
        pulse_w_byte(w, 'P');
        pulse_w_byte(w, 'N');
}

/* ---- the queue to a client ---- */

static long pulse_out_fill(const struct pulse_client *c)
{
        return c->out_tail - c->out_head;
}

// A frame to the client: the descriptor (length, channel, offset, flags), then the payload. The client is dead if there is no room.
static int pulse_enqueue(struct pulse_client *c, unsigned long channel, const unsigned char *payload, long length)
{
        unsigned char header[20] = {(unsigned char)(length >> 24), (unsigned char)(length >> 16), (unsigned char)(length >> 8), (unsigned char)length,
                                    (unsigned char)(channel >> 24), (unsigned char)(channel >> 16), (unsigned char)(channel >> 8), (unsigned char)channel,
                                    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

        if (pulse_out_fill(c) + 20 + length > PULSE_OUT)
                return PULSE_DEAD;
        for (int part = 0; part < 2; part++)
        {
                const unsigned char *from = part ? payload : header;
                long n = part ? length : 20;

                for (long i = 0; i < n; i++)
                        c->out[(unsigned long)(c->out_tail + i) % PULSE_OUT] = from[i];
                c->out_tail += n;
        }
        return PULSE_DONE;
}

/* ---- the objects ---- */

static struct pulse_stream *pulse_stream_of(struct pulse *p, long client, unsigned long channel)
{
        for (int i = 0; i < PULSE_STREAMS; i++)
                if (p->stream[i].used && !p->stream[i].dying && p->stream[i].client == client && p->stream[i].channel == channel)
                        return p->stream + i;
        return 0;
}

static struct pulse_stream *pulse_input_of(struct pulse *p, unsigned long index)
{
        for (int i = 0; i < PULSE_STREAMS; i++)
                if (p->stream[i].used && !p->stream[i].dying && p->stream[i].index == index)
                        return p->stream + i;
        return 0;
}

// A client may read and change what is its own; root may change anything.
static int pulse_may(const struct pulse_client *c, long uid)
{
        return c->root || c->uid == uid;
}

static void pulse_changed(struct pulse *p, int what, long sink)
{
        if (p->host.changed)
                p->host.changed(p->host.context, what, sink);
}

// The event to every client that asked for it: the facility's bit in its mask, the kind and the index.
static void pulse_event(struct pulse *p, unsigned long facility, unsigned long kind, unsigned long index)
{
        for (int i = 0; i < PULSE_CLIENTS; i++)
        {
                struct pulse_client *c = p->client[i];
                struct pulse_writer w = {p->scratch, PULSE_SCRATCH, 0, 0};

                if (!c || !c->authorized || !(c->subscribe & (1ul << facility)))
                        continue;
                pulse_w_u32(&w, PULSE_COMMAND_SUBSCRIBE_EVENT);
                pulse_w_u32(&w, PULSE_INVALID);
                pulse_w_u32(&w, facility | kind);
                pulse_w_u32(&w, index);
                // A client that cannot take an event is dropped by the next call that finds its queue full; here it is only skipped.
                pulse_enqueue(c, PULSE_INVALID, w.p, w.len);
        }
}

static int pulse_error(struct pulse *p, struct pulse_client *c, unsigned long tag, unsigned long code)
{
        struct pulse_writer w = {p->scratch, PULSE_SCRATCH, 0, 0};

        pulse_w_u32(&w, PULSE_COMMAND_ERROR);
        pulse_w_u32(&w, tag);
        pulse_w_u32(&w, code);
        return pulse_enqueue(c, PULSE_INVALID, w.p, w.len);
}

static void pulse_reply_begin(struct pulse *p, struct pulse_writer *w, unsigned long tag)
{
        w->p = p->scratch;
        w->cap = PULSE_SCRATCH;
        w->len = 0;
        w->bad = 0;
        pulse_w_u32(w, PULSE_COMMAND_REPLY);
        pulse_w_u32(w, tag);
}

static int pulse_reply_end(struct pulse_client *c, struct pulse_writer *w)
{
        // A reply that did not fit is a bug in the caps, and the client is dropped rather than sent half of it.
        if (w->bad)
                return PULSE_DEAD;
        return pulse_enqueue(c, PULSE_INVALID, w->p, w->len);
}

static int pulse_ack(struct pulse *p, struct pulse_client *c, unsigned long tag)
{
        struct pulse_writer w;

        pulse_reply_begin(p, &w, tag);
        return pulse_reply_end(c, &w);
}

/* ---- the sinks ---- */

/*
        A sink for a card (or a clock): its name as clients see it, the description the person sees, and the rate the
        card is run at. Answers its slot or a negative number. The first becomes the default.
*/
AUDIO_API long pulse_sink_add(struct pulse *p, const char *name, const char *description, long rate)
{
        for (long i = 0; i < PULSE_SINKS; i++)
                if (!p->sink[i].used)
                {
                        struct pulse_sink *s = p->sink + i;

                        __builtin_memset(s, 0, sizeof(*s));
                        s->used = 1;
                        pulse_copy(s->name, PULSE_NAME, name);
                        pulse_copy(s->description, PULSE_DESCRIPTION, description);
                        s->rate = rate;
                        s->volume[0] = s->volume[1] = AUDIO_VOLUME_NORM;
                        audio_sink_open(&s->mix, rate, 2);
                        if (p->default_sink < 0)
                                p->default_sink = i;
                        pulse_event(p, PULSE_FACILITY_SINK, PULSE_EVENT_NEW, (unsigned long)i);
                        pulse_event(p, PULSE_FACILITY_SOURCE, PULSE_EVENT_NEW, (unsigned long)i);
                        return i;
                }
        return -1;
}

/*
        The sink is another card now (the person chose another device, or the card went and the clock took over): its name,
        its description and its rate. The streams on it are played at the new rate, from where they were; a stream that
        cannot be (no table for its ratio) is ended. Clients are told that the sink and the server changed.
*/
static void pulse_stream_end(struct pulse *p, struct pulse_stream *s);

AUDIO_API void pulse_sink_reconfigure(struct pulse *p, long slot, const char *name, const char *description, long rate)
{
        struct pulse_sink *sink = p->sink + slot;
        float master = sink->mix.master_target;

        pulse_copy(sink->name, PULSE_NAME, name);
        pulse_copy(sink->description, PULSE_DESCRIPTION, description);
        if (rate != sink->rate)
        {
                sink->rate = rate;
                audio_sink_open(&sink->mix, rate, 2);
                sink->mix.master_target = sink->mix.master_now = master;
                for (int i = 0; i < PULSE_STREAMS; i++)
                        if (p->stream[i].used && !p->stream[i].dying && p->stream[i].sink == slot && audio_voice_retarget(&p->stream[i].voice, rate) < 0)
                                pulse_stream_end(p, p->stream + i);
        }
        pulse_event(p, PULSE_FACILITY_SINK, PULSE_EVENT_CHANGE, (unsigned long)slot);
        pulse_event(p, PULSE_FACILITY_SOURCE, PULSE_EVENT_CHANGE, (unsigned long)slot);
        pulse_event(p, PULSE_FACILITY_SERVER, PULSE_EVENT_CHANGE, 0);
}

// A name that is all digits is a number, as PulseAudio reads it: `pactl set-sink-volume 0 30%` names its sink 0. -1 when it is not one.
static long pulse_number(const char *name)
{
        long value = 0;

        if (!name || !name[0])
                return -1;
        for (; *name; name++)
        {
                if (*name < '0' || *name > '9' || value > 100000)
                        return -1;
                value = value * 10 + (*name - '0');
        }
        return value;
}

static long pulse_sink_named(struct pulse *p, const char *name)
{
        long number;

        for (long i = 0; i < PULSE_SINKS; i++)
                if (p->sink[i].used && pulse_equal(p->sink[i].name, name))
                        return i;
        number = pulse_number(name);
        return number >= 0 && number < PULSE_SINKS && p->sink[number].used ? number : -1;
}

static float pulse_mean_gain(const unsigned long *volume, long n)
{
        unsigned long sum = 0;

        for (long i = 0; i < n; i++)
                sum += volume[i];
        return audio_volume_gain(n ? sum / (unsigned long)n : 0);
}

// The gain of each of the pair on the bus for a stream: its map's left channels, its right, and the middle on both.
static void pulse_stream_gains(const struct pulse_stream *s, float *left, float *right)
{
        unsigned long l = 0, r = 0, m = 0;
        long nl = 0, nr = 0, nm = 0;

        for (long c = 0; c < s->map_channels && c < AUDIO_CHANNELS_MAX; c++)
        {
                int side = 2, pos = s->map[c];

                if (pos == AUDIO_POSITION_FRONT_LEFT || pos == AUDIO_POSITION_REAR_LEFT || pos == AUDIO_POSITION_SIDE_LEFT || pos == AUDIO_POSITION_FRONT_LEFT_OF_CENTER ||
                    (pos >= AUDIO_POSITION_AUX0 && pos < AUDIO_POSITION_AUX0 + 32 && !((pos - AUDIO_POSITION_AUX0) & 1)))
                        side = 0;
                else if (pos == AUDIO_POSITION_FRONT_RIGHT || pos == AUDIO_POSITION_REAR_RIGHT || pos == AUDIO_POSITION_SIDE_RIGHT || pos == AUDIO_POSITION_FRONT_RIGHT_OF_CENTER ||
                         pos >= AUDIO_POSITION_AUX0)
                        side = 1;
                if (side == 0)
                        l += s->volume[c], nl++;
                else if (side == 1)
                        r += s->volume[c], nr++;
                else
                        m += s->volume[c], nm++;
        }
        // A side with no channel of its own takes the middle's, or the other side's.
        if (!nl && !nr)
        {
                unsigned long v = nm ? m / (unsigned long)nm : AUDIO_VOLUME_NORM;

                *left = *right = audio_volume_gain(v);
                return;
        }
        *left = audio_volume_gain(nl ? l / (unsigned long)nl : r / (unsigned long)nr);
        *right = audio_volume_gain(nr ? r / (unsigned long)nr : l / (unsigned long)nl);
        if (nm)
        {
                float centre = audio_volume_gain(m / (unsigned long)nm);

                // The middle channels go to both sides: the stream's volume there is the mean of the two it has.
                *left = (*left * (float)nl + centre * (float)nm) / (float)(nl + nm);
                *right = (*right * (float)nr + centre * (float)nm) / (float)(nr + nm);
        }
}

static void pulse_stream_apply(struct pulse_stream *s)
{
        float left, right;

        pulse_stream_gains(s, &left, &right);
        if (s->muted || s->corked || s->dying)
                left = right = 0.0f;
        audio_voice_gain(&s->voice, left, right);
        // A muted stream's time goes on, and a stream at no volume; a corked one's stands still.
        s->voice.flows = !s->corked;
}

static void pulse_stream_free(struct pulse *p, struct pulse_stream *s)
{
        audio_voice_close(&s->voice);
        if (s->ring_memory)
        {
                p->host.release(p->host.context, s->ring_memory, (long)s->ring_bytes);
                p->ring_total -= s->ring_bytes;
        }
        s->ring_memory = 0;
        s->used = 0;
        p->streams_freed++;
}

// A stream ends: its voice fades out first (a client that dies mid-stream does not click), and then it is freed.
static void pulse_stream_end(struct pulse *p, struct pulse_stream *s)
{
        pulse_event(p, PULSE_FACILITY_SINK_INPUT, PULSE_EVENT_REMOVE, s->index);
        pulse_changed(p, PULSE_CHANGED_STREAMS, s->sink);
        s->dying = 1;
        s->client = -1;
        s->drain_pending = 0;
        s->corked = 0;
        pulse_stream_apply(s);
        if (s->voice.now[0] == 0 && s->voice.now[1] == 0)
                pulse_stream_free(p, s);
}

/* ---- a client's departure ---- */

static void pulse_drop(struct pulse *p, long slot)
{
        struct pulse_client *c = p->client[slot];

        if (!c)
                return;
        for (int i = 0; i < PULSE_STREAMS; i++)
                if (p->stream[i].used && !p->stream[i].dying && p->stream[i].client == slot)
                        pulse_stream_end(p, p->stream + i);
        pulse_event(p, PULSE_FACILITY_CLIENT, PULSE_EVENT_REMOVE, c->index);
        p->client[slot] = 0;
        p->clients_dropped++;
        p->host.release(p->host.context, c, (long)sizeof(*c));
}

/* ---- introspection ---- */

static void pulse_fill_sink(struct pulse *p, struct pulse_writer *w, struct pulse_client *c, long slot)
{
        struct pulse_sink *s = p->sink + slot;
        static const signed char stereo[2] = {AUDIO_POSITION_FRONT_LEFT, AUDIO_POSITION_FRONT_RIGHT};
        char monitor[PULSE_NAME + 16];
        struct pulse_props props = {0};
        long latency = s->delay_frames * 1000000l / (s->rate ? s->rate : 1);

        pulse_copy(monitor, sizeof(monitor), s->name);
        pulse_copy(monitor + pulse_len(monitor), (long)sizeof(monitor) - pulse_len(monitor), ".monitor");
        pulse_w_u32(w, (unsigned long)slot);
        pulse_w_string(w, s->name);
        pulse_w_string(w, s->description);
        pulse_w_sample_spec(w, AUDIO_FORMAT_F32LE, 2, s->rate);
        pulse_w_channel_map(w, stereo, 2);
        pulse_w_u32(w, PULSE_INVALID);
        pulse_w_cvolume(w, s->volume, 2);
        pulse_w_bool(w, s->muted);
        pulse_w_u32(w, (unsigned long)slot);
        pulse_w_string(w, monitor);
        pulse_w_64(w, 'U', latency);
        pulse_w_string(w, "moonwater audio");
        pulse_w_u32(w, 0x1 | 0x2 | 0x4 | 0x8 | 0x10 | 0x20 | 0x40); // HW_VOLUME_CTRL is not set: the volume is the mixer's, in software
        if (c->version >= 13)
        {
                pulse_props_set(&props, "device.description", (const unsigned char *)s->description, pulse_len(s->description) + 1);
                pulse_props_set(&props, "device.class", (const unsigned char *)"sound", 6);
                pulse_w_proplist(w, &props);
                pulse_w_64(w, 'U', 0);
        }
        if (c->version >= 15)
        {
                int busy = 0;

                for (int i = 0; i < PULSE_STREAMS; i++)
                        busy |= p->stream[i].used && !p->stream[i].dying && p->stream[i].sink == slot;
                pulse_w_volume(w, AUDIO_VOLUME_NORM);              // base volume
                pulse_w_u32(w, s->suspended ? 2 : busy ? 0 : 1);   // running, idle or suspended
                pulse_w_u32(w, 256);                               // volume steps
                pulse_w_u32(w, PULSE_INVALID);                     // card
        }
        if (c->version >= 16)
        {
                pulse_w_u32(w, 0); // no ports
                pulse_w_string(w, 0);
        }
        if (c->version >= 21)
        {
                pulse_w_u8(w, 1);
                pulse_w_format_info(w);
        }
}

static void pulse_fill_source(struct pulse *p, struct pulse_writer *w, struct pulse_client *c, long slot)
{
        struct pulse_sink *s = p->sink + slot;
        static const signed char stereo[2] = {AUDIO_POSITION_FRONT_LEFT, AUDIO_POSITION_FRONT_RIGHT};
        char monitor[PULSE_NAME + 16], description[PULSE_DESCRIPTION + 24];
        struct pulse_props props = {0};
        unsigned long volume[2] = {AUDIO_VOLUME_NORM, AUDIO_VOLUME_NORM};

        pulse_copy(monitor, sizeof(monitor), s->name);
        pulse_copy(monitor + pulse_len(monitor), (long)sizeof(monitor) - pulse_len(monitor), ".monitor");
        pulse_copy(description, 12, "Monitor of ");
        pulse_copy(description + 11, (long)sizeof(description) - 11, s->description);
        pulse_w_u32(w, (unsigned long)slot);
        pulse_w_string(w, monitor);
        pulse_w_string(w, description);
        pulse_w_sample_spec(w, AUDIO_FORMAT_F32LE, 2, s->rate);
        pulse_w_channel_map(w, stereo, 2);
        pulse_w_u32(w, PULSE_INVALID);
        pulse_w_cvolume(w, volume, 2);
        pulse_w_bool(w, 0);
        pulse_w_u32(w, (unsigned long)slot);
        pulse_w_string(w, s->name);
        pulse_w_64(w, 'U', 0);
        pulse_w_string(w, "moonwater audio");
        pulse_w_u32(w, 0x1 | 0x2 | 0x4 | 0x8 | 0x10);
        if (c->version >= 13)
        {
                pulse_props_set(&props, "device.description", (const unsigned char *)description, pulse_len(description) + 1);
                pulse_props_set(&props, "device.class", (const unsigned char *)"monitor", 8);
                pulse_w_proplist(w, &props);
                pulse_w_64(w, 'U', 0);
        }
        if (c->version >= 15)
        {
                pulse_w_volume(w, AUDIO_VOLUME_NORM);
                pulse_w_u32(w, 0);
                pulse_w_u32(w, 256);
                pulse_w_u32(w, PULSE_INVALID);
        }
        if (c->version >= 16)
        {
                pulse_w_u32(w, 0);
                pulse_w_string(w, 0);
        }
        if (c->version >= 22)
        {
                pulse_w_u8(w, 1);
                pulse_w_format_info(w);
        }
}

static void pulse_fill_client(struct pulse *p, struct pulse_writer *w, struct pulse_client *c, struct pulse_client *of)
{
        char name[PULSE_VALUE + 1];

        (void)p;
        pulse_props_text(&of->props, "application.name", name, sizeof(name));
        pulse_w_u32(w, of->index);
        pulse_w_string(w, name);
        pulse_w_u32(w, PULSE_INVALID);
        pulse_w_string(w, "protocol-native");
        if (c->version >= 13)
                pulse_w_proplist(w, &of->props);
}

static void pulse_fill_input(struct pulse *p, struct pulse_writer *w, struct pulse_client *c, struct pulse_stream *s)
{
        char name[PULSE_VALUE + 1];
        long latency = p->sink[s->sink].delay_frames * 1000000l / (p->sink[s->sink].rate ? p->sink[s->sink].rate : 1);
        unsigned long volume[PULSE_CHANNEL_MAX];
        long n = s->map_channels;

        pulse_props_text(&s->props, "media.name", name, sizeof(name));
        for (long i = 0; i < n; i++)
                volume[i] = s->volume[i];
        pulse_w_u32(w, s->index);
        pulse_w_string(w, name);
        pulse_w_u32(w, PULSE_INVALID);
        pulse_w_u32(w, s->client >= 0 && p->client[s->client] ? p->client[s->client]->index : PULSE_INVALID);
        pulse_w_u32(w, (unsigned long)s->sink);
        pulse_w_sample_spec(w, s->format, s->channels, s->rate);
        pulse_w_channel_map(w, s->map, s->map_channels);
        pulse_w_cvolume(w, volume, n);
        pulse_w_64(w, 'U', (long long)(s->voice.ring.limit - s->voice.ring.read) * 1000000 / (long long)(s->voice.frame * (unsigned long)s->rate));
        pulse_w_64(w, 'U', latency);
        pulse_w_string(w, "copy");
        pulse_w_string(w, "protocol-native");
        if (c->version >= 11)
                pulse_w_bool(w, s->muted);
        if (c->version >= 13)
                pulse_w_proplist(w, &s->props);
        if (c->version >= 19)
                pulse_w_bool(w, s->corked);
        if (c->version >= 20)
        {
                pulse_w_bool(w, 1);
                pulse_w_bool(w, 1);
        }
        if (c->version >= 21)
                pulse_w_format_info(w);
}

/* ---- the streams ---- */

// Bytes of the ring a program may be asked for, so that its buffered data and what is asked reach the target length.
static void pulse_stream_request(struct pulse *p, struct pulse_stream *s)
{
        struct pulse_client *c;
        unsigned long queued, target, missing;
        struct pulse_writer w = {p->scratch, PULSE_SCRATCH, 0, 0};

        if (!s->used || s->dying || s->client < 0 || !(c = p->client[s->client]))
                return;
        queued = audio_ring_fill(&s->voice.ring);
        target = s->tlength;
        if (queued + s->outstanding >= target)
                return;
        missing = target - queued - s->outstanding;
        missing -= missing % s->voice.frame;
        if (!missing || (missing < s->minreq && (queued + s->outstanding) != 0))
                return;
        pulse_w_u32(&w, PULSE_COMMAND_REQUEST);
        pulse_w_u32(&w, PULSE_INVALID);
        pulse_w_u32(&w, s->channel);
        pulse_w_u32(&w, missing);
        if (pulse_enqueue(c, PULSE_INVALID, w.p, w.len) == PULSE_DONE)
                s->outstanding += missing;
}

static void pulse_stream_signal(struct pulse *p, struct pulse_stream *s, unsigned long command, int with_offset)
{
        struct pulse_client *c = s->client >= 0 ? p->client[s->client] : 0;
        struct pulse_writer w = {p->scratch, PULSE_SCRATCH, 0, 0};

        if (!c || (command == PULSE_COMMAND_STARTED && c->version < 13))
                return;
        pulse_w_u32(&w, command);
        pulse_w_u32(&w, PULSE_INVALID);
        pulse_w_u32(&w, s->channel);
        if (with_offset && c->version >= 23)
                pulse_w_64(&w, 'r', (long long)s->voice.ring.read);
        pulse_enqueue(c, PULSE_INVALID, w.p, w.len);
}

/*
        Makes a playback stream for a client, from CREATE_PLAYBACK_STREAM's fields. Answers the stream or 0 with an error
        code in *error. The buffer attributes are the program's where it gave them, else the server's: a target of 200 ms,
        a request for a quarter of it, start playing when the target is there.
*/
static struct pulse_stream *pulse_stream_make(struct pulse *p, long slot, long format, long channels, long rate, const signed char *map, long map_channels, long sink,
                                              unsigned long maxlength, unsigned long tlength, unsigned long prebuf, unsigned long minreq, int *error)
{
        struct pulse_client *c = p->client[slot];
        struct pulse_stream *s = 0;
        unsigned long frame, ring;
        long count = 0;

        for (int i = 0; i < PULSE_STREAMS; i++)
        {
                if (p->stream[i].used && !p->stream[i].dying && p->stream[i].client == slot)
                        count++;
                if (!p->stream[i].used && !s)
                        s = p->stream + i;
        }
        if (count >= PULSE_STREAMS_PER_CLIENT || !s)
        {
                *error = PULSE_ERR_TOOLARGE;
                return 0;
        }
        if (!audio_format_bytes(format) || channels < 1 || channels > AUDIO_CHANNELS_MAX || rate < 1 || rate > AUDIO_RATE_MAX || map_channels != channels)
        {
                *error = PULSE_ERR_INVALID;
                return 0;
        }
        frame = (unsigned long)(audio_format_bytes(format) * channels);
        // Bytes of 200 ms by default, and never more than a ring.
        if (tlength == PULSE_INVALID || tlength == 0)
                tlength = (unsigned long)rate / 5 * frame;
        if (tlength > PULSE_RING_MAX / 2)
                tlength = PULSE_RING_MAX / 2;
        if (tlength < frame * 64)
                tlength = frame * 64;
        tlength -= tlength % frame;
        if (maxlength == PULSE_INVALID || maxlength < tlength * 2)
                maxlength = tlength * 2;
        if (maxlength > PULSE_RING_MAX)
                maxlength = PULSE_RING_MAX;
        maxlength -= maxlength % frame;
        if (tlength > maxlength)
                tlength = maxlength;
        if (prebuf == PULSE_INVALID)
                prebuf = tlength;
        if (prebuf > tlength)
                prebuf = tlength;
        prebuf -= prebuf % frame;
        if (minreq == PULSE_INVALID || minreq == 0)
                minreq = tlength / 4;
        if (minreq > tlength)
                minreq = tlength;
        minreq -= minreq % frame;
        if (minreq < frame)
                minreq = frame;
        ring = maxlength;
        if (p->ring_total + ring > PULSE_RING_TOTAL)
        {
                *error = PULSE_ERR_TOOLARGE;
                return 0;
        }
        __builtin_memset(s, 0, __builtin_offsetof(struct pulse_stream, voice));
        s->ring_memory = p->host.alloc(p->host.context, (long)ring);
        if (!s->ring_memory)
        {
                *error = PULSE_ERR_TOOLARGE;
                return 0;
        }
        s->ring_bytes = ring;
        if (audio_voice_open(&s->voice, format, channels, rate, map, p->sink[sink].rate, s->ring_memory, ring) < 0)
        {
                p->host.release(p->host.context, s->ring_memory, (long)ring);
                s->ring_memory = 0;
                *error = PULSE_ERR_NOTSUPPORTED;
                return 0;
        }
        p->ring_total += ring;
        s->used = 1;
        s->client = slot;
        s->channel = c->next_channel++;
        s->index = p->next_input++;
        s->sink = sink;
        s->format = format;
        s->channels = channels;
        s->rate = rate;
        s->map_channels = map_channels;
        for (long i = 0; i < channels; i++)
                s->map[i] = map[i];
        s->maxlength = maxlength;
        s->tlength = tlength;
        s->prebuf = prebuf;
        s->minreq = minreq;
        s->underrun = 1;
        s->uid = c->uid;
        s->pid = c->pid;
        for (long i = 0; i < PULSE_CHANNEL_MAX; i++)
                s->volume[i] = AUDIO_VOLUME_NORM;
        p->streams_made++;
        return s;
}

/* ---- the commands ---- */

#define PULSE_NEED_AUTH() \
        do \
        { \
                if (!c->authorized) \
                        return pulse_error(p, c, tag, PULSE_ERR_ACCESS); \
        } while (0)

// The sink a command names by index or by name (a name given with an index that is not invalid is a mistake).
static long pulse_find_sink(struct pulse *p, unsigned long index, const char *name)
{
        if (name && name[0])
                return pulse_sink_named(p, name);
        if (index == PULSE_INVALID)
                return -1;
        return index < PULSE_SINKS && p->sink[index].used ? (long)index : -1;
}

static int pulse_command_create_playback(struct pulse *p, long slot, struct pulse_reader *r, unsigned long tag)
{
        struct pulse_client *c = p->client[slot];
        struct pulse_props props = {0};
        long format = 0, channels = 0, rate = 0, map_channels, sink;
        int error = PULSE_ERR_INVALID;
        signed char map[PULSE_CHANNEL_MAX];
        unsigned long sink_index, maxlength, tlength, prebuf, minreq, volume[PULSE_CHANNEL_MAX];
        const char *sink_name;
        int corked, muted = 0, adjust = 0, early = 0, volume_set = 1, muted_set = 0, variable = 0, no_move = 0, isnull;
        long volume_channels;
        struct pulse_stream *s;
        struct pulse_writer w;
        int err;

        PULSE_NEED_AUTH();
        pulse_r_sample_spec(r, &format, &channels, &rate);
        map_channels = pulse_r_channel_map(r, map);
        sink_index = pulse_r_u32(r);
        sink_name = pulse_r_string(r, &isnull);
        maxlength = pulse_r_u32(r);
        corked = pulse_r_bool(r);
        tlength = pulse_r_u32(r);
        prebuf = pulse_r_u32(r);
        minreq = pulse_r_u32(r);
        pulse_r_u32(r); // sync id
        volume_channels = pulse_r_cvolume(r, volume);
        if (c->version >= 12)
        {
                pulse_r_bool(r); // no remap
                pulse_r_bool(r); // no remix
                pulse_r_bool(r); // fix format
                pulse_r_bool(r); // fix rate
                pulse_r_bool(r); // fix channels
                no_move = pulse_r_bool(r);
                variable = pulse_r_bool(r);
        }
        if (c->version >= 13)
        {
                muted = pulse_r_bool(r);
                adjust = pulse_r_bool(r);
                pulse_r_proplist(r, &props);
        }
        if (c->version >= 14)
        {
                volume_set = pulse_r_bool(r);
                early = pulse_r_bool(r);
        }
        if (c->version >= 15)
        {
                muted_set = pulse_r_bool(r);
                pulse_r_bool(r); // don't inhibit auto suspend
                pulse_r_bool(r); // fail on suspend
        }
        if (c->version >= 17)
                pulse_r_bool(r); // relative volume
        if (c->version >= 18)
                pulse_r_bool(r); // passthrough
        if (c->version >= 21)
        {
                unsigned long formats = pulse_r_u8(r);

                // Formats the program offers are PCM or nothing: any of them is read and none is used (the sample spec stands).
                for (unsigned long i = 0; i < formats && !r->bad; i++)
                {
                        pulse_tag(r, 'f');
                        pulse_r_u8(r);
                        {
                                struct pulse_props ignored = {0};

                                pulse_r_proplist(r, &ignored);
                        }
                }
        }
        if (!pulse_r_eof(r))
                return PULSE_DEAD;
        (void)muted_set;
        if (volume_channels != channels)
                return pulse_error(p, c, tag, PULSE_ERR_INVALID);
        if (sink_name && sink_name[0] && sink_index != PULSE_INVALID)
                return pulse_error(p, c, tag, PULSE_ERR_INVALID);
        if (sink_name && sink_name[0])
                sink = pulse_sink_named(p, sink_name);
        else if (sink_index != PULSE_INVALID)
                sink = pulse_find_sink(p, sink_index, 0);
        else
                sink = p->default_sink;
        if (sink < 0)
                return pulse_error(p, c, tag, PULSE_ERR_NOENTITY);
        s = pulse_stream_make(p, slot, format, channels, rate, map, map_channels, sink, maxlength, tlength, prebuf, minreq, &error);
        if (!s)
                return pulse_error(p, c, tag, (unsigned long)error);
        s->props = props;
        s->corked = corked;
        s->muted = muted;
        s->adjust_latency = adjust;
        s->early_requests = early;
        s->variable_rate = variable;
        s->no_move = no_move;
        s->owner_set = sink_name && sink_name[0] ? 1 : sink_index != PULSE_INVALID;
        if (volume_set)
                for (long i = 0; i < volume_channels; i++)
                        s->volume[i] = volume[i];
        else
        {
                // A program that did not say remembers its volume by its name.
                char app[PULSE_VALUE + 1];
                unsigned long remembered;
                int mute_remembered;

                pulse_props_text(&c->props, "application.name", app, sizeof(app));
                if (p->host.recall && app[0] && p->host.recall(p->host.context, app, &remembered, &mute_remembered))
                {
                        for (long i = 0; i < channels; i++)
                                s->volume[i] = remembered;
                        s->muted = mute_remembered;
                }
        }
        // The program's own application properties (its client's) go with its stream when it gave none of its own.
        {
                char value[PULSE_VALUE + 1];

                if (!pulse_props_text(&s->props, "application.name", value, sizeof(value)) && pulse_props_text(&c->props, "application.name", value, sizeof(value)))
                        pulse_props_set(&s->props, "application.name", (const unsigned char *)value, pulse_len(value) + 1);
        }
        pulse_stream_apply(s);
        // The program is asked for its first data before the answer, as the answer's "missing" says.
        pulse_reply_begin(p, &w, tag);
        pulse_w_u32(&w, s->channel);
        pulse_w_u32(&w, s->index);
        pulse_w_u32(&w, s->tlength);
        pulse_w_u32(&w, s->maxlength);
        pulse_w_u32(&w, s->tlength);
        pulse_w_u32(&w, s->prebuf);
        pulse_w_u32(&w, s->minreq);
        if (c->version >= 12)
        {
                pulse_w_sample_spec(&w, s->format, s->channels, s->rate);
                pulse_w_channel_map(&w, s->map, s->map_channels);
                pulse_w_u32(&w, (unsigned long)sink);
                pulse_w_string(&w, p->sink[sink].name);
                pulse_w_bool(&w, 0);
        }
        if (c->version >= 13)
                pulse_w_64(&w, 'U', 20000);
        if (c->version >= 21)
                pulse_w_format_info(&w);
        s->outstanding = s->tlength;
        err = pulse_reply_end(c, &w);
        pulse_event(p, PULSE_FACILITY_SINK_INPUT, PULSE_EVENT_NEW, s->index);
        pulse_changed(p, PULSE_CHANGED_STREAMS, sink);
        return err;
}

static int pulse_command_set_volume(struct pulse *p, struct pulse_client *c, unsigned long command, unsigned long tag, struct pulse_reader *r)
{
        unsigned long index = pulse_r_u32(r), volume[PULSE_CHANNEL_MAX];
        const char *name = 0;
        long n;
        int isnull;

        if (command == PULSE_COMMAND_SET_SINK_VOLUME || command == PULSE_COMMAND_SET_SOURCE_VOLUME)
                name = pulse_r_string(r, &isnull);
        n = pulse_r_cvolume(r, volume);
        if (!pulse_r_eof(r))
                return PULSE_DEAD;
        PULSE_NEED_AUTH();
        if (n < 1)
                return pulse_error(p, c, tag, PULSE_ERR_INVALID);
        if (command == PULSE_COMMAND_SET_SINK_INPUT_VOLUME)
        {
                struct pulse_stream *s = pulse_input_of(p, index);

                if (!s)
                        return pulse_error(p, c, tag, PULSE_ERR_NOENTITY);
                if (!pulse_may(c, s->uid))
                        return pulse_error(p, c, tag, PULSE_ERR_ACCESS);
                if (n != s->map_channels)
                        return pulse_error(p, c, tag, PULSE_ERR_INVALID);
                for (long i = 0; i < n; i++)
                        s->volume[i] = volume[i];
                pulse_stream_apply(s);
                if (p->host.remember)
                {
                        char app[PULSE_VALUE + 1];
                        struct pulse_client *owner = s->client >= 0 ? p->client[s->client] : 0;

                        if (owner && pulse_props_text(&owner->props, "application.name", app, sizeof(app)))
                                p->host.remember(p->host.context, app, volume[0], s->muted);
                }
                pulse_event(p, PULSE_FACILITY_SINK_INPUT, PULSE_EVENT_CHANGE, s->index);
        }
        else if (command == PULSE_COMMAND_SET_SINK_VOLUME)
        {
                long sink = pulse_find_sink(p, index, name);

                if (sink < 0)
                        return pulse_error(p, c, tag, PULSE_ERR_NOENTITY);
                if (!c->root)
                        return pulse_error(p, c, tag, PULSE_ERR_ACCESS);
                if (n != 2)
                        return pulse_error(p, c, tag, PULSE_ERR_INVALID);
                p->sink[sink].volume[0] = volume[0];
                p->sink[sink].volume[1] = volume[1];
                pulse_event(p, PULSE_FACILITY_SINK, PULSE_EVENT_CHANGE, (unsigned long)sink);
                pulse_changed(p, PULSE_CHANGED_SINK_VOLUME, sink);
        }
        else if (command == PULSE_COMMAND_SET_SOURCE_VOLUME)
        {
                if (pulse_find_sink(p, index, name) < 0)
                        return pulse_error(p, c, tag, PULSE_ERR_NOENTITY);
        }
        else
                return pulse_error(p, c, tag, PULSE_ERR_NOENTITY);
        return pulse_ack(p, c, tag);
}

static int pulse_command_set_mute(struct pulse *p, struct pulse_client *c, unsigned long command, unsigned long tag, struct pulse_reader *r)
{
        unsigned long index = pulse_r_u32(r);
        const char *name = 0;
        int mute, isnull;

        if (command == PULSE_COMMAND_SET_SINK_MUTE || command == PULSE_COMMAND_SET_SOURCE_MUTE)
                name = pulse_r_string(r, &isnull);
        mute = pulse_r_bool(r);
        if (!pulse_r_eof(r))
                return PULSE_DEAD;
        PULSE_NEED_AUTH();
        if (command == PULSE_COMMAND_SET_SINK_INPUT_MUTE)
        {
                struct pulse_stream *s = pulse_input_of(p, index);

                if (!s)
                        return pulse_error(p, c, tag, PULSE_ERR_NOENTITY);
                if (!pulse_may(c, s->uid))
                        return pulse_error(p, c, tag, PULSE_ERR_ACCESS);
                s->muted = mute;
                pulse_stream_apply(s);
                if (p->host.remember)
                {
                        char app[PULSE_VALUE + 1];
                        struct pulse_client *owner = s->client >= 0 ? p->client[s->client] : 0;

                        if (owner && pulse_props_text(&owner->props, "application.name", app, sizeof(app)))
                                p->host.remember(p->host.context, app, s->volume[0], mute);
                }
                pulse_event(p, PULSE_FACILITY_SINK_INPUT, PULSE_EVENT_CHANGE, s->index);
        }
        else if (command == PULSE_COMMAND_SET_SINK_MUTE)
        {
                long sink = pulse_find_sink(p, index, name);

                if (sink < 0)
                        return pulse_error(p, c, tag, PULSE_ERR_NOENTITY);
                if (!c->root)
                        return pulse_error(p, c, tag, PULSE_ERR_ACCESS);
                p->sink[sink].muted = mute;
                pulse_event(p, PULSE_FACILITY_SINK, PULSE_EVENT_CHANGE, (unsigned long)sink);
                pulse_changed(p, PULSE_CHANGED_SINK_VOLUME, sink);
        }
        else if (command == PULSE_COMMAND_SET_SOURCE_MUTE)
        {
                if (pulse_find_sink(p, index, name) < 0)
                        return pulse_error(p, c, tag, PULSE_ERR_NOENTITY);
        }
        else
                return pulse_error(p, c, tag, PULSE_ERR_NOENTITY);
        return pulse_ack(p, c, tag);
}

// The commands on one stream by its channel: delete, cork, flush, trigger, prebuf, drain, latency, name, rate, attributes, properties.
static int pulse_command_stream(struct pulse *p, long slot, unsigned long command, unsigned long tag, struct pulse_reader *r)
{
        struct pulse_client *c = p->client[slot];
        unsigned long channel = pulse_r_u32(r);
        struct pulse_stream *s;
        struct pulse_writer w;

        PULSE_NEED_AUTH();
        switch (command)
        {
        case PULSE_COMMAND_DELETE_PLAYBACK_STREAM:
        case PULSE_COMMAND_FLUSH_PLAYBACK_STREAM:
        case PULSE_COMMAND_TRIGGER_PLAYBACK_STREAM:
        case PULSE_COMMAND_PREBUF_PLAYBACK_STREAM:
        case PULSE_COMMAND_DRAIN_PLAYBACK_STREAM:
                if (!pulse_r_eof(r))
                        return PULSE_DEAD;
                break;
        default:
                break;
        }
        s = r->bad ? 0 : pulse_stream_of(p, slot, channel);
        switch (command)
        {
        case PULSE_COMMAND_DELETE_PLAYBACK_STREAM:
                if (!s)
                        return pulse_error(p, c, tag, PULSE_ERR_NOENTITY);
                pulse_stream_end(p, s);
                return pulse_ack(p, c, tag);
        case PULSE_COMMAND_CORK_PLAYBACK_STREAM:
        {
                int cork = pulse_r_bool(r);

                if (!pulse_r_eof(r))
                        return PULSE_DEAD;
                if (!s)
                        return pulse_error(p, c, tag, PULSE_ERR_NOENTITY);
                s->corked = cork;
                if (cork)
                        s->underrun = 1;
                pulse_stream_apply(s);
                pulse_event(p, PULSE_FACILITY_SINK_INPUT, PULSE_EVENT_CHANGE, s->index);
                return pulse_ack(p, c, tag);
        }
        case PULSE_COMMAND_FLUSH_PLAYBACK_STREAM:
                if (!s)
                        return pulse_error(p, c, tag, PULSE_ERR_NOENTITY);
                // What was queued is thrown away, but the last frames of it play out, in the fade that an end of data has.
                {
                        unsigned long keep = (unsigned long)AUDIO_RAMP * s->voice.frame, fill = audio_ring_fill(&s->voice.ring);

                        if (s->underrun || s->corked)
                        {
                                // Not playing: everything is dropped.
                                s->voice.ring.limit = s->voice.ring.write = s->voice.ring.read;
                        }
                        else
                        {
                                // Playing: the last frames of it play out, in the fade that the end of data has, and then it waits for its start again.
                                if (fill > keep)
                                        s->voice.ring.limit = s->voice.ring.read + keep;
                                s->voice.ring.write = s->voice.ring.limit;
                                s->flushing = 1;
                        }
                        s->outstanding = 0;
                        s->drain_pending = 0;
                        if (s->prebuf)
                                s->underrun = s->underrun || !s->flushing;
                        s->trigger = 0;
                }
                pulse_stream_apply(s);
                pulse_stream_request(p, s);
                return pulse_ack(p, c, tag);
        case PULSE_COMMAND_TRIGGER_PLAYBACK_STREAM:
                if (!s)
                        return pulse_error(p, c, tag, PULSE_ERR_NOENTITY);
                s->trigger = 1;
                return pulse_ack(p, c, tag);
        case PULSE_COMMAND_PREBUF_PLAYBACK_STREAM:
                if (!s)
                        return pulse_error(p, c, tag, PULSE_ERR_NOENTITY);
                if (s->prebuf)
                        s->underrun = 1;
                s->trigger = 0;
                return pulse_ack(p, c, tag);
        case PULSE_COMMAND_DRAIN_PLAYBACK_STREAM:
                if (!s)
                        return pulse_error(p, c, tag, PULSE_ERR_NOENTITY);
                if (s->drain_pending)
                        return pulse_error(p, c, tag, PULSE_ERR_BUSY);
                s->drain_pending = 1;
                s->drain_tag = tag;
                s->drained = 0;
                s->trigger = 1; // a stream below its start is started to be drained
                return PULSE_DONE;
        case PULSE_COMMAND_GET_PLAYBACK_LATENCY:
        {
                unsigned long sec = 0, usec = 0;
                long now_sec = 0, now_usec = 0;
                long latency;

                pulse_r_timeval(r, &sec, &usec);
                if (!pulse_r_eof(r))
                        return PULSE_DEAD;
                if (!s)
                        return pulse_error(p, c, tag, PULSE_ERR_NOENTITY);
                if (p->host.real)
                        p->host.real(p->host.context, &now_sec, &now_usec);
                latency = p->sink[s->sink].delay_frames * 1000000l / (p->sink[s->sink].rate ? p->sink[s->sink].rate : 1) +
                          (long)((unsigned long)audio_voice_delay(&s->voice) * 1000000ul / (unsigned long)s->rate);
                pulse_reply_begin(p, &w, tag);
                pulse_w_64(&w, 'U', latency);
                pulse_w_64(&w, 'U', 0);
                pulse_w_bool(&w, !s->underrun && !s->corked);
                pulse_w_byte(&w, 'T');
                pulse_w_be32(&w, sec);
                pulse_w_be32(&w, usec);
                pulse_w_byte(&w, 'T');
                pulse_w_be32(&w, (unsigned long)now_sec);
                pulse_w_be32(&w, (unsigned long)now_usec);
                pulse_w_64(&w, 'r', (long long)s->voice.ring.write);
                pulse_w_64(&w, 'r', (long long)s->voice.ring.read);
                if (c->version >= 13)
                {
                        pulse_w_64(&w, 'R', (long long)s->underrun_for);
                        pulse_w_64(&w, 'R', (long long)s->playing_for);
                }
                return pulse_reply_end(c, &w);
        }
        case PULSE_COMMAND_SET_PLAYBACK_STREAM_NAME:
        {
                int isnull;
                const char *name = pulse_r_string(r, &isnull);

                if (!pulse_r_eof(r) || !name)
                        return PULSE_DEAD;
                if (!s)
                        return pulse_error(p, c, tag, PULSE_ERR_NOENTITY);
                pulse_props_set(&s->props, "media.name", (const unsigned char *)name, pulse_len(name) + 1);
                pulse_event(p, PULSE_FACILITY_SINK_INPUT, PULSE_EVENT_CHANGE, s->index);
                return pulse_ack(p, c, tag);
        }
        case PULSE_COMMAND_UPDATE_PLAYBACK_STREAM_SAMPLE_RATE:
        {
                unsigned long rate = pulse_r_u32(r);

                if (!pulse_r_eof(r))
                        return PULSE_DEAD;
                if (!s)
                        return pulse_error(p, c, tag, PULSE_ERR_NOENTITY);
                if (!s->variable_rate || rate < 1 || rate > AUDIO_RATE_MAX || audio_voice_rate(&s->voice, (long)rate) < 0)
                        return pulse_error(p, c, tag, PULSE_ERR_INVALID);
                s->rate = (long)rate;
                return pulse_ack(p, c, tag);
        }
        case PULSE_COMMAND_SET_PLAYBACK_STREAM_BUFFER_ATTR:
        {
                unsigned long maxlength = pulse_r_u32(r), tlength = pulse_r_u32(r), prebuf = pulse_r_u32(r), minreq = pulse_r_u32(r);
                int adjust = 0, early = 0;

                if (c->version >= 13)
                        adjust = pulse_r_bool(r);
                if (c->version >= 14)
                        early = pulse_r_bool(r);
                if (!pulse_r_eof(r))
                        return PULSE_DEAD;
                if (!s)
                        return pulse_error(p, c, tag, PULSE_ERR_NOENTITY);
                {
                        unsigned long frame = s->voice.frame;

                        (void)maxlength;
                        if (tlength == PULSE_INVALID || tlength == 0)
                                tlength = s->tlength;
                        if (tlength > s->maxlength / 2 + s->maxlength / 2)
                                tlength = s->maxlength;
                        if (tlength < frame * 64)
                                tlength = frame * 64;
                        tlength -= tlength % frame;
                        if (prebuf == PULSE_INVALID)
                                prebuf = tlength;
                        if (prebuf > tlength)
                                prebuf = tlength;
                        prebuf -= prebuf % frame;
                        if (minreq == PULSE_INVALID || minreq == 0)
                                minreq = tlength / 4;
                        if (minreq > tlength)
                                minreq = tlength;
                        minreq -= minreq % frame;
                        if (minreq < frame)
                                minreq = frame;
                        s->tlength = tlength;
                        s->prebuf = prebuf;
                        s->minreq = minreq;
                        s->adjust_latency = adjust;
                        s->early_requests = early;
                }
                pulse_reply_begin(p, &w, tag);
                pulse_w_u32(&w, s->maxlength);
                pulse_w_u32(&w, s->tlength);
                pulse_w_u32(&w, s->prebuf);
                pulse_w_u32(&w, s->minreq);
                if (c->version >= 13)
                        pulse_w_64(&w, 'U', 20000);
                return pulse_reply_end(c, &w);
        }
        case PULSE_COMMAND_UPDATE_PLAYBACK_STREAM_PROPLIST:
        {
                unsigned long mode = pulse_r_u32(r);
                struct pulse_props props = {0};

                pulse_r_proplist(r, &props);
                if (!pulse_r_eof(r))
                        return PULSE_DEAD;
                if (!s)
                        return pulse_error(p, c, tag, PULSE_ERR_NOENTITY);
                pulse_props_update(&s->props, &props, mode);
                pulse_event(p, PULSE_FACILITY_SINK_INPUT, PULSE_EVENT_CHANGE, s->index);
                return pulse_ack(p, c, tag);
        }
        case PULSE_COMMAND_REMOVE_PLAYBACK_STREAM_PROPLIST:
        {
                for (;;)
                {
                        int isnull;
                        const char *key = pulse_r_string(r, &isnull);

                        if (r->bad)
                                return PULSE_DEAD;
                        if (isnull)
                                break;
                        if (s)
                                pulse_props_remove(&s->props, key);
                }
                if (!pulse_r_eof(r))
                        return PULSE_DEAD;
                if (!s)
                        return pulse_error(p, c, tag, PULSE_ERR_NOENTITY);
                pulse_event(p, PULSE_FACILITY_SINK_INPUT, PULSE_EVENT_CHANGE, s->index);
                return pulse_ack(p, c, tag);
        }
        default:
                break;
        }
        return pulse_error(p, c, tag, PULSE_ERR_COMMAND);
}

static int pulse_command_info(struct pulse *p, long slot, unsigned long command, unsigned long tag, struct pulse_reader *r)
{
        struct pulse_client *c = p->client[slot];
        struct pulse_writer w;
        unsigned long index = PULSE_INVALID;
        const char *name = 0;
        int list = 0, isnull;

        switch (command)
        {
        case PULSE_COMMAND_GET_SINK_INFO_LIST:
        case PULSE_COMMAND_GET_SOURCE_INFO_LIST:
        case PULSE_COMMAND_GET_CLIENT_INFO_LIST:
        case PULSE_COMMAND_GET_SINK_INPUT_INFO_LIST:
        case PULSE_COMMAND_GET_SOURCE_OUTPUT_INFO_LIST:
        case PULSE_COMMAND_GET_MODULE_INFO_LIST:
        case PULSE_COMMAND_GET_CARD_INFO_LIST:
        case PULSE_COMMAND_GET_SAMPLE_INFO_LIST:
                list = 1;
                break;
        default:
                index = pulse_r_u32(r);
                if (command == PULSE_COMMAND_GET_SINK_INFO || command == PULSE_COMMAND_GET_SOURCE_INFO || command == PULSE_COMMAND_GET_CARD_INFO || command == PULSE_COMMAND_GET_SAMPLE_INFO)
                        name = pulse_r_string(r, &isnull);
                break;
        }
        if (!pulse_r_eof(r))
                return PULSE_DEAD;
        PULSE_NEED_AUTH();
        // A sink or a source named by a number is that index.
        if (name && pulse_number(name) >= 0 && (command == PULSE_COMMAND_GET_SINK_INFO || command == PULSE_COMMAND_GET_SOURCE_INFO))
        {
                index = (unsigned long)pulse_number(name);
                name = 0;
        }
        pulse_reply_begin(p, &w, tag);
        switch (command)
        {
        case PULSE_COMMAND_GET_SINK_INFO:
        case PULSE_COMMAND_GET_SINK_INFO_LIST:
        case PULSE_COMMAND_GET_SOURCE_INFO:
        case PULSE_COMMAND_GET_SOURCE_INFO_LIST:
        {
                int sources = command == PULSE_COMMAND_GET_SOURCE_INFO || command == PULSE_COMMAND_GET_SOURCE_INFO_LIST;

                for (long i = 0; i < PULSE_SINKS; i++)
                {
                        char monitor[PULSE_NAME + 16];

                        if (!p->sink[i].used)
                                continue;
                        pulse_copy(monitor, sizeof(monitor), p->sink[i].name);
                        pulse_copy(monitor + pulse_len(monitor), (long)sizeof(monitor) - pulse_len(monitor), ".monitor");
                        if (!list && !((name && name[0] && pulse_equal(sources ? monitor : p->sink[i].name, name)) || (!(name && name[0]) && index == (unsigned long)i)))
                                continue;
                        if (sources)
                                pulse_fill_source(p, &w, c, i);
                        else
                                pulse_fill_sink(p, &w, c, i);
                        if (!list)
                                return pulse_reply_end(c, &w);
                }
                if (!list)
                        return pulse_error(p, c, tag, PULSE_ERR_NOENTITY);
                return pulse_reply_end(c, &w);
        }
        case PULSE_COMMAND_GET_CLIENT_INFO:
        case PULSE_COMMAND_GET_CLIENT_INFO_LIST:
                for (long i = 0; i < PULSE_CLIENTS; i++)
                        if (p->client[i] && p->client[i]->authorized && (list || p->client[i]->index == index) && (c->root || p->client[i]->uid == c->uid))
                        {
                                pulse_fill_client(p, &w, c, p->client[i]);
                                if (!list)
                                        return pulse_reply_end(c, &w);
                        }
                if (!list)
                        return pulse_error(p, c, tag, PULSE_ERR_NOENTITY);
                return pulse_reply_end(c, &w);
        case PULSE_COMMAND_GET_SINK_INPUT_INFO:
        case PULSE_COMMAND_GET_SINK_INPUT_INFO_LIST:
                for (int i = 0; i < PULSE_STREAMS; i++)
                        if (p->stream[i].used && !p->stream[i].dying && (list || p->stream[i].index == index) && pulse_may(c, p->stream[i].uid))
                        {
                                pulse_fill_input(p, &w, c, p->stream + i);
                                if (!list)
                                        return pulse_reply_end(c, &w);
                        }
                if (!list)
                        return pulse_error(p, c, tag, PULSE_ERR_NOENTITY);
                return pulse_reply_end(c, &w);
        case PULSE_COMMAND_GET_SOURCE_OUTPUT_INFO_LIST:
        case PULSE_COMMAND_GET_MODULE_INFO_LIST:
        case PULSE_COMMAND_GET_CARD_INFO_LIST:
        case PULSE_COMMAND_GET_SAMPLE_INFO_LIST:
                return pulse_reply_end(c, &w);
        default:
                return pulse_error(p, c, tag, PULSE_ERR_NOENTITY);
        }
}

static int pulse_command_server_info(struct pulse *p, struct pulse_client *c, unsigned long tag, struct pulse_reader *r)
{
        struct pulse_writer w;
        static const signed char stereo[2] = {AUDIO_POSITION_FRONT_LEFT, AUDIO_POSITION_FRONT_RIGHT};
        char monitor[PULSE_NAME + 16];

        if (!pulse_r_eof(r))
                return PULSE_DEAD;
        PULSE_NEED_AUTH();
        pulse_reply_begin(p, &w, tag);
        pulse_w_string(&w, "Moonwater audio");
        pulse_w_string(&w, "1.0");
        pulse_w_string(&w, "moonwater");
        pulse_w_string(&w, "moonwater");
        pulse_w_sample_spec(&w, AUDIO_FORMAT_S16LE, 2, p->default_sink >= 0 ? p->sink[p->default_sink].rate : 48000);
        pulse_w_string(&w, p->default_sink >= 0 ? p->sink[p->default_sink].name : 0);
        if (p->default_sink >= 0)
        {
                pulse_copy(monitor, sizeof(monitor), p->sink[p->default_sink].name);
                pulse_copy(monitor + pulse_len(monitor), (long)sizeof(monitor) - pulse_len(monitor), ".monitor");
                pulse_w_string(&w, monitor);
        }
        else
                pulse_w_string(&w, 0);
        pulse_w_u32(&w, 0);
        if (c->version >= 15)
                pulse_w_channel_map(&w, stereo, 2);
        return pulse_reply_end(c, &w);
}

// One command frame: the tag structure of it, in payload[0, length).
static int pulse_command(struct pulse *p, long slot, const unsigned char *payload, long length)
{
        struct pulse_client *c = p->client[slot];
        struct pulse_reader r = {payload, length, 0, 0};
        unsigned long command = pulse_r_u32(&r), tag = pulse_r_u32(&r);

        if (r.bad)
                return PULSE_DEAD;
        // Before the handshake only the handshake.
        if (!c->authorized && command != PULSE_COMMAND_AUTH && command != PULSE_COMMAND_SET_CLIENT_NAME)
                return pulse_error(p, c, tag, PULSE_ERR_ACCESS);
        switch (command)
        {
        case PULSE_COMMAND_AUTH:
        {
                unsigned long version = pulse_r_u32(&r);
                struct pulse_writer w;

                pulse_r_arbitrary(&r, 256);
                if (!pulse_r_eof(&r))
                        return PULSE_DEAD;
                version &= 0xffff;
                if (version < 8)
                        return pulse_error(p, c, tag, PULSE_ERR_VERSION);
                c->version = (unsigned)(version < PULSE_VERSION ? version : PULSE_VERSION);
                c->authorized = 1;
                pulse_reply_begin(p, &w, tag);
                pulse_w_u32(&w, PULSE_VERSION); // no shared memory, no memfd: neither flag
                return pulse_reply_end(c, &w);
        }
        case PULSE_COMMAND_SET_CLIENT_NAME:
        {
                struct pulse_props props = {0};
                struct pulse_writer w;

                if (c->version >= 13)
                        pulse_r_proplist(&r, &props);
                else
                {
                        int isnull;
                        const char *name = pulse_r_string(&r, &isnull);

                        if (name)
                                pulse_props_set(&props, "application.name", (const unsigned char *)name, pulse_len(name) + 1);
                }
                if (!pulse_r_eof(&r))
                        return PULSE_DEAD;
                c->props = props;
                {
                        char pid[24];
                        long n = 0, v = c->pid;
                        char digits[24];
                        int count = 0;

                        do
                                digits[count++] = (char)('0' + v % 10);
                        while ((v /= 10) > 0);
                        while (count)
                                pid[n++] = digits[--count];
                        pid[n] = 0;
                        // The pid the system says it is, over whatever the program says.
                        pulse_props_set(&c->props, "application.process.id", (const unsigned char *)pid, n + 1);
                }
                pulse_event(p, PULSE_FACILITY_CLIENT, PULSE_EVENT_NEW, c->index);
                pulse_reply_begin(p, &w, tag);
                if (c->version >= 13)
                        pulse_w_u32(&w, c->index);
                return pulse_reply_end(c, &w);
        }
        case PULSE_COMMAND_UPDATE_CLIENT_PROPLIST:
        {
                unsigned long mode = pulse_r_u32(&r);
                struct pulse_props props = {0};

                pulse_r_proplist(&r, &props);
                if (!pulse_r_eof(&r))
                        return PULSE_DEAD;
                PULSE_NEED_AUTH();
                {
                        struct pulse_props pid = {0};
                        char id[24];

                        // The process id is the system's, whatever the program says of it.
                        pulse_props_text(&c->props, "application.process.id", id, sizeof(id));
                        for (int i = 0; i < props.count; i++)
                                if (pulse_equal(props.entry[i].key, "application.process.id"))
                                        props.entry[i] = props.entry[--props.count], i--;
                        pulse_props_update(&c->props, &props, mode);
                        if (id[0])
                        {
                                pulse_props_set(&pid, "application.process.id", (const unsigned char *)id, pulse_len(id) + 1);
                                pulse_props_update(&c->props, &pid, 2);
                        }
                }
                pulse_event(p, PULSE_FACILITY_CLIENT, PULSE_EVENT_CHANGE, c->index);
                return pulse_ack(p, c, tag);
        }
        case PULSE_COMMAND_REMOVE_CLIENT_PROPLIST:
                PULSE_NEED_AUTH();
                for (;;)
                {
                        int isnull;
                        const char *key = pulse_r_string(&r, &isnull);

                        if (r.bad)
                                return PULSE_DEAD;
                        if (isnull)
                                break;
                        if (!pulse_equal(key, "application.process.id"))
                                pulse_props_remove(&c->props, key);
                }
                if (!pulse_r_eof(&r))
                        return PULSE_DEAD;
                pulse_event(p, PULSE_FACILITY_CLIENT, PULSE_EVENT_CHANGE, c->index);
                return pulse_ack(p, c, tag);
        case PULSE_COMMAND_EXIT:
                if (!pulse_r_eof(&r))
                        return PULSE_DEAD;
                return pulse_error(p, c, tag, PULSE_ERR_ACCESS);
        case PULSE_COMMAND_GET_SERVER_INFO:
                return pulse_command_server_info(p, c, tag, &r);
        case PULSE_COMMAND_STAT:
        {
                struct pulse_writer w;

                if (!pulse_r_eof(&r))
                        return PULSE_DEAD;
                pulse_reply_begin(p, &w, tag);
                for (int i = 0; i < 5; i++)
                        pulse_w_u32(&w, 0);
                return pulse_reply_end(c, &w);
        }
        case PULSE_COMMAND_LOOKUP_SINK:
        case PULSE_COMMAND_LOOKUP_SOURCE:
        {
                int isnull;
                const char *name = pulse_r_string(&r, &isnull);
                struct pulse_writer w;
                long found = -1;

                if (!pulse_r_eof(&r) || !name)
                        return PULSE_DEAD;
                for (long i = 0; i < PULSE_SINKS && found < 0; i++)
                {
                        char monitor[PULSE_NAME + 16];

                        if (!p->sink[i].used)
                                continue;
                        pulse_copy(monitor, sizeof(monitor), p->sink[i].name);
                        pulse_copy(monitor + pulse_len(monitor), (long)sizeof(monitor) - pulse_len(monitor), ".monitor");
                        if (pulse_equal(command == PULSE_COMMAND_LOOKUP_SINK ? p->sink[i].name : monitor, name))
                                found = i;
                }
                if (found < 0)
                        return pulse_error(p, c, tag, PULSE_ERR_NOENTITY);
                pulse_reply_begin(p, &w, tag);
                pulse_w_u32(&w, (unsigned long)found);
                return pulse_reply_end(c, &w);
        }
        case PULSE_COMMAND_SUBSCRIBE:
        {
                unsigned long mask = pulse_r_u32(&r);

                if (!pulse_r_eof(&r))
                        return PULSE_DEAD;
                PULSE_NEED_AUTH();
                c->subscribe = mask & 0x2ff;
                return pulse_ack(p, c, tag);
        }
        case PULSE_COMMAND_CREATE_PLAYBACK_STREAM:
                return pulse_command_create_playback(p, slot, &r, tag);
        case PULSE_COMMAND_DELETE_PLAYBACK_STREAM:
        case PULSE_COMMAND_CORK_PLAYBACK_STREAM:
        case PULSE_COMMAND_FLUSH_PLAYBACK_STREAM:
        case PULSE_COMMAND_TRIGGER_PLAYBACK_STREAM:
        case PULSE_COMMAND_PREBUF_PLAYBACK_STREAM:
        case PULSE_COMMAND_DRAIN_PLAYBACK_STREAM:
        case PULSE_COMMAND_GET_PLAYBACK_LATENCY:
        case PULSE_COMMAND_SET_PLAYBACK_STREAM_NAME:
        case PULSE_COMMAND_UPDATE_PLAYBACK_STREAM_SAMPLE_RATE:
        case PULSE_COMMAND_SET_PLAYBACK_STREAM_BUFFER_ATTR:
        case PULSE_COMMAND_UPDATE_PLAYBACK_STREAM_PROPLIST:
        case PULSE_COMMAND_REMOVE_PLAYBACK_STREAM_PROPLIST:
                return pulse_command_stream(p, slot, command, tag, &r);
        case PULSE_COMMAND_GET_SINK_INFO:
        case PULSE_COMMAND_GET_SINK_INFO_LIST:
        case PULSE_COMMAND_GET_SOURCE_INFO:
        case PULSE_COMMAND_GET_SOURCE_INFO_LIST:
        case PULSE_COMMAND_GET_MODULE_INFO:
        case PULSE_COMMAND_GET_MODULE_INFO_LIST:
        case PULSE_COMMAND_GET_CLIENT_INFO:
        case PULSE_COMMAND_GET_CLIENT_INFO_LIST:
        case PULSE_COMMAND_GET_SINK_INPUT_INFO:
        case PULSE_COMMAND_GET_SINK_INPUT_INFO_LIST:
        case PULSE_COMMAND_GET_SOURCE_OUTPUT_INFO:
        case PULSE_COMMAND_GET_SOURCE_OUTPUT_INFO_LIST:
        case PULSE_COMMAND_GET_SAMPLE_INFO:
        case PULSE_COMMAND_GET_SAMPLE_INFO_LIST:
        case PULSE_COMMAND_GET_CARD_INFO:
        case PULSE_COMMAND_GET_CARD_INFO_LIST:
                return pulse_command_info(p, slot, command, tag, &r);
        case PULSE_COMMAND_SET_SINK_VOLUME:
        case PULSE_COMMAND_SET_SINK_INPUT_VOLUME:
        case PULSE_COMMAND_SET_SOURCE_VOLUME:
        case PULSE_COMMAND_SET_SOURCE_OUTPUT_VOLUME:
                return pulse_command_set_volume(p, c, command, tag, &r);
        case PULSE_COMMAND_SET_SINK_MUTE:
        case PULSE_COMMAND_SET_SINK_INPUT_MUTE:
        case PULSE_COMMAND_SET_SOURCE_MUTE:
        case PULSE_COMMAND_SET_SOURCE_OUTPUT_MUTE:
                return pulse_command_set_mute(p, c, command, tag, &r);
        case PULSE_COMMAND_SET_DEFAULT_SINK:
        case PULSE_COMMAND_SET_DEFAULT_SOURCE:
        {
                int isnull;
                const char *name = pulse_r_string(&r, &isnull);
                long sink = -1;

                if (!pulse_r_eof(&r))
                        return PULSE_DEAD;
                PULSE_NEED_AUTH();
                if (!c->root)
                        return pulse_error(p, c, tag, PULSE_ERR_ACCESS);
                if (name)
                {
                        char base[PULSE_NAME + 16];

                        pulse_copy(base, sizeof(base), name);
                        // A source is a sink's monitor: the default source follows the sink it monitors.
                        if (command == PULSE_COMMAND_SET_DEFAULT_SOURCE)
                        {
                                long n = pulse_len(base);

                                if (n > 8 && pulse_equal(base + n - 8, ".monitor"))
                                        base[n - 8] = 0;
                        }
                        sink = pulse_sink_named(p, base);
                }
                if (sink < 0)
                        return pulse_error(p, c, tag, PULSE_ERR_NOENTITY);
                if (command == PULSE_COMMAND_SET_DEFAULT_SINK && sink != p->default_sink)
                {
                        p->default_sink = sink;
                        // A stream that did not name its sink follows the default.
                        for (int i = 0; i < PULSE_STREAMS; i++)
                                if (p->stream[i].used && !p->stream[i].dying && !p->stream[i].owner_set && !p->stream[i].no_move && p->stream[i].sink != sink)
                                {
                                        struct pulse_stream *s = p->stream + i;

                                        if (audio_voice_retarget(&s->voice, p->sink[sink].rate) == 0)
                                        {
                                                s->sink = sink;
                                                pulse_event(p, PULSE_FACILITY_SINK_INPUT, PULSE_EVENT_CHANGE, s->index);
                                        }
                                }
                        pulse_event(p, PULSE_FACILITY_SERVER, PULSE_EVENT_CHANGE, 0);
                        pulse_changed(p, PULSE_CHANGED_DEFAULT, sink);
                }
                return pulse_ack(p, c, tag);
        }
        case PULSE_COMMAND_MOVE_SINK_INPUT:
        {
                unsigned long index = pulse_r_u32(&r), device = pulse_r_u32(&r);
                int isnull;
                const char *name = pulse_r_string(&r, &isnull);
                struct pulse_stream *s;
                long sink;

                if (!pulse_r_eof(&r))
                        return PULSE_DEAD;
                PULSE_NEED_AUTH();
                s = pulse_input_of(p, index);
                sink = pulse_find_sink(p, device, name);
                if (!s || sink < 0)
                        return pulse_error(p, c, tag, PULSE_ERR_NOENTITY);
                if (!pulse_may(c, s->uid))
                        return pulse_error(p, c, tag, PULSE_ERR_ACCESS);
                if (s->sink != sink)
                {
                        if (audio_voice_retarget(&s->voice, p->sink[sink].rate) < 0)
                                return pulse_error(p, c, tag, PULSE_ERR_NOTSUPPORTED);
                        s->sink = sink;
                        s->owner_set = 1;
                        pulse_event(p, PULSE_FACILITY_SINK_INPUT, PULSE_EVENT_CHANGE, s->index);
                }
                return pulse_ack(p, c, tag);
        }
        case PULSE_COMMAND_KILL_CLIENT:
        case PULSE_COMMAND_KILL_SINK_INPUT:
        {
                unsigned long index = pulse_r_u32(&r);

                if (!pulse_r_eof(&r))
                        return PULSE_DEAD;
                PULSE_NEED_AUTH();
                if (!c->root)
                        return pulse_error(p, c, tag, PULSE_ERR_ACCESS);
                if (command == PULSE_COMMAND_KILL_SINK_INPUT)
                {
                        struct pulse_stream *s = pulse_input_of(p, index);
                        struct pulse_client *owner;
                        struct pulse_writer w = {p->scratch, PULSE_SCRATCH, 0, 0};

                        if (!s)
                                return pulse_error(p, c, tag, PULSE_ERR_NOENTITY);
                        owner = s->client >= 0 ? p->client[s->client] : 0;
                        if (owner)
                        {
                                pulse_w_u32(&w, PULSE_COMMAND_PLAYBACK_STREAM_KILLED);
                                pulse_w_u32(&w, PULSE_INVALID);
                                pulse_w_u32(&w, s->channel);
                                pulse_enqueue(owner, PULSE_INVALID, w.p, w.len);
                        }
                        pulse_stream_end(p, s);
                }
                else
                {
                        for (long i = 0; i < PULSE_CLIENTS; i++)
                                if (p->client[i] && p->client[i]->index == index && i != slot)
                                {
                                        pulse_drop(p, i);
                                        return pulse_ack(p, c, tag);
                                }
                        return pulse_error(p, c, tag, PULSE_ERR_NOENTITY);
                }
                return pulse_ack(p, c, tag);
        }
        case PULSE_COMMAND_SUSPEND_SINK:
        case PULSE_COMMAND_SUSPEND_SOURCE:
        {
                unsigned long index = pulse_r_u32(&r);
                int isnull;
                const char *name = pulse_r_string(&r, &isnull);

                pulse_r_bool(&r);
                if (!pulse_r_eof(&r))
                        return PULSE_DEAD;
                PULSE_NEED_AUTH();
                if (index != PULSE_INVALID && name && name[0])
                        return pulse_error(p, c, tag, PULSE_ERR_INVALID);
                return pulse_ack(p, c, tag); // accepted and not acted on: the card is opened when it is wanted
        }
        case PULSE_COMMAND_SET_SINK_PORT:
        case PULSE_COMMAND_SET_SOURCE_PORT:
        case PULSE_COMMAND_SET_CARD_PROFILE:
        case PULSE_COMMAND_SET_PORT_LATENCY_OFFSET:
        case PULSE_COMMAND_MOVE_SOURCE_OUTPUT:
        case PULSE_COMMAND_KILL_SOURCE_OUTPUT:
        case PULSE_COMMAND_SET_RECORD_STREAM_NAME:
        case PULSE_COMMAND_DELETE_RECORD_STREAM:
        case PULSE_COMMAND_GET_RECORD_LATENCY:
        case PULSE_COMMAND_CORK_RECORD_STREAM:
        case PULSE_COMMAND_FLUSH_RECORD_STREAM:
        case PULSE_COMMAND_SET_RECORD_STREAM_BUFFER_ATTR:
        case PULSE_COMMAND_UPDATE_RECORD_STREAM_SAMPLE_RATE:
        case PULSE_COMMAND_UPDATE_RECORD_STREAM_PROPLIST:
        case PULSE_COMMAND_REMOVE_RECORD_STREAM_PROPLIST:
        case PULSE_COMMAND_DELETE_UPLOAD_STREAM:
        case PULSE_COMMAND_FINISH_UPLOAD_STREAM:
        case PULSE_COMMAND_REMOVE_SAMPLE:
                PULSE_NEED_AUTH();
                return pulse_error(p, c, tag, PULSE_ERR_NOENTITY);
        case PULSE_COMMAND_CREATE_RECORD_STREAM:
        case PULSE_COMMAND_CREATE_UPLOAD_STREAM:
        case PULSE_COMMAND_PLAY_SAMPLE:
        case PULSE_COMMAND_SEND_OBJECT_MESSAGE:
                PULSE_NEED_AUTH();
                return pulse_error(p, c, tag, PULSE_ERR_NOTIMPLEMENTED);
        case PULSE_COMMAND_LOAD_MODULE:
        case PULSE_COMMAND_UNLOAD_MODULE:
                PULSE_NEED_AUTH();
                return pulse_error(p, c, tag, PULSE_ERR_ACCESS);
        case PULSE_COMMAND_EXTENSION:
                PULSE_NEED_AUTH();
                return pulse_error(p, c, tag, PULSE_ERR_NOEXTENSION);
        case PULSE_COMMAND_ENABLE_SRBCHANNEL:
        case PULSE_COMMAND_DISABLE_SRBCHANNEL:
        case PULSE_COMMAND_REGISTER_MEMFD_SHMID:
                // Neither side was told it could: a program that asks anyway is told no.
                return pulse_error(p, c, tag, PULSE_ERR_NOTSUPPORTED);
        default:
                return pulse_error(p, c, tag, PULSE_ERR_COMMAND);
        }
}

/* ---- data ---- */

static int pulse_data(struct pulse *p, long slot, unsigned long channel, long long offset, unsigned long flags, const unsigned char *data, long length)
{
        struct pulse_client *c = p->client[slot];
        struct pulse_stream *s = pulse_stream_of(p, slot, channel);
        unsigned long took;

        if (!c->authorized)
                return PULSE_DEAD;
        if (!s) // data for a stream that is gone: dropped, as a program that deleted it just before may have sent some
                return PULSE_DONE;
        if (length % (long)s->voice.frame)
                return PULSE_DEAD;
        if ((flags & 0xff) != 0 || offset != 0)
        {
                if (audio_ring_seek(&s->voice.ring, (long)(flags & 0xff), offset) < 0)
                        return PULSE_DONE; // a seek out of the ring is dropped with its data
        }
        took = audio_ring_put(&s->voice.ring, data, (unsigned long)length);
        if (took < (unsigned long)length)
        {
                struct pulse_writer w = {p->scratch, PULSE_SCRATCH, 0, 0};

                pulse_w_u32(&w, PULSE_COMMAND_OVERFLOW);
                pulse_w_u32(&w, PULSE_INVALID);
                pulse_w_u32(&w, s->channel);
                pulse_enqueue(c, PULSE_INVALID, w.p, w.len);
        }
        s->outstanding = s->outstanding > took ? s->outstanding - took : 0;
        return PULSE_DONE;
}

/* ---- the connection ---- */

/*
        A client has connected: its user and process as the system says (a socket's credentials), and a block for its
        buffers from the host. Answers its slot, or a negative number when it is refused: the table is full, or its
        user already has PULSE_PER_USER.
*/
AUDIO_API long pulse_client_open(struct pulse *p, long uid, long pid)
{
        long used_by_user = 0;

        for (long i = 0; i < PULSE_CLIENTS; i++)
                if (p->client[i] && p->client[i]->uid == uid)
                        used_by_user++;
        if (uid != 0 && used_by_user >= PULSE_PER_USER)
                return -1;
        for (long i = 0; i < PULSE_CLIENTS; i++)
                if (!p->client[i])
                {
                        struct pulse_client *c = p->host.alloc(p->host.context, (long)sizeof(struct pulse_client));

                        if (!c)
                                return -1;
                        c->used = 1;
                        c->index = p->next_client++;
                        c->uid = uid;
                        c->pid = pid;
                        c->root = uid == 0;
                        c->born = p->host.now(p->host.context);
                        p->client[i] = c;
                        p->clients_made++;
                        return i;
                }
        return -1;
}

/*
        Bytes a client sent. Frames are read whole and answered; the bytes of one that has not all come are kept. Answers
        0, or PULSE_DEAD when the client is to be dropped (the caller then closes its socket and calls pulse_client_close).
*/
AUDIO_API int pulse_client_feed(struct pulse *p, long slot, const unsigned char *bytes, long count)
{
        struct pulse_client *c = p->client[slot];

        while (count > 0)
        {
                long take = PULSE_IN - c->in_have < count ? PULSE_IN - c->in_have : count;

                __builtin_memcpy(c->in + c->in_have, bytes, (unsigned long)take);
                c->in_have += take;
                bytes += take;
                count -= take;
                for (;;)
                {
                        unsigned long length, channel, flags;
                        long long offset;
                        int result;

                        if (c->in_have < 20)
                                break;
                        length = pulse_be32(c->in);
                        channel = pulse_be32(c->in + 4);
                        offset = (long long)((unsigned long long)pulse_be32(c->in + 8) << 32 | pulse_be32(c->in + 12));
                        flags = pulse_be32(c->in + 16);
                        if (channel == PULSE_INVALID ? length > PULSE_FRAME_COMMAND : length > PULSE_FRAME_DATA)
                        {
                                p->protocol_errors++;
                                return PULSE_DEAD;
                        }
                        // Shared memory and descriptors were never offered: a frame that says it carries them is a lie.
                        if (flags & 0xff000000u)
                        {
                                p->protocol_errors++;
                                return PULSE_DEAD;
                        }
                        if (c->in_have < 20 + (long)length)
                                break;
                        if (channel == PULSE_INVALID)
                                result = length ? pulse_command(p, slot, c->in + 20, (long)length) : PULSE_DEAD;
                        else
                                result = pulse_data(p, slot, channel, offset, flags, c->in + 20, (long)length);
                        if (result == PULSE_DEAD)
                        {
                                p->protocol_errors++;
                                return PULSE_DEAD;
                        }
                        c->in_have -= 20 + (long)length;
                        __builtin_memmove(c->in, c->in + 20 + length, (unsigned long)c->in_have);
                }
        }
        return PULSE_DONE;
}

// What the client is to be sent: the contiguous part of its queue at the head, and how many bytes; pulse_client_sent takes what was written.
AUDIO_API long pulse_client_out(struct pulse *p, long slot, const unsigned char **bytes)
{
        struct pulse_client *c = p->client[slot];
        unsigned long head = (unsigned long)c->out_head % PULSE_OUT;
        long fill = pulse_out_fill(c), run = (long)(PULSE_OUT - head);

        *bytes = c->out + head;
        return fill < run ? fill : run;
}

AUDIO_API void pulse_client_sent(struct pulse *p, long slot, long count)
{
        p->client[slot]->out_head += count;
}

AUDIO_API long pulse_client_pending(struct pulse *p, long slot)
{
        return pulse_out_fill(p->client[slot]);
}

AUDIO_API void pulse_client_close(struct pulse *p, long slot)
{
        pulse_drop(p, slot);
}

// A client that connected and has not said who it is, by the time it should have: it is dropped.
AUDIO_API int pulse_client_late(struct pulse *p, long slot, long now)
{
        struct pulse_client *c = p->client[slot];

        return !c->authorized && now - c->born > PULSE_AUTH_USEC;
}

AUDIO_API void pulse_open(struct pulse *p, const struct pulse_host *host)
{
        __builtin_memset(p, 0, sizeof(*p));
        p->host = *host;
        p->default_sink = -1;
}

/* ---- running the sink ---- */

static void pulse_stream_play(struct pulse *p, struct pulse_stream *s, struct pulse_sink *sink, long frames)
{
        unsigned long fill = audio_ring_fill(&s->voice.ring);
        long played, now = p->host.now(p->host.context);

        if (s->dying)
        {
                // Fading out: it plays its ring until its gain has reached nothing, and is then freed.
                audio_voice_mix(&sink->mix, &s->voice, frames, 0);
                if (s->voice.now[0] == 0 && s->voice.now[1] == 0)
                        pulse_stream_free(p, s);
                return;
        }
        if (s->corked)
        {
                // Its gain goes to nothing over a ramp, taking what the ramp plays from the ring; then its time stands still.
                if (s->voice.now[0] != 0 || s->voice.now[1] != 0)
                        audio_voice_mix(&sink->mix, &s->voice, frames, 0);
                return;
        }
        if (s->underrun)
        {
                // Waiting for the start: the target of data is there, or the program triggered it, or a drain asked for the rest.
                if (fill > 0 && (fill >= s->prebuf || s->trigger))
                {
                        s->underrun = 0;
                        pulse_stream_signal(p, s, PULSE_COMMAND_STARTED, 0);
                }
                else
                {
                        s->underrun_for += (unsigned long long)frames * s->voice.frame;
                        if (s->drain_pending && fill == 0 && !s->drained)
                        {
                                s->drained = 1;
                                s->drain_at = now + PULSE_DRAIN_USEC;
                        }
                        return;
                }
        }
        played = audio_voice_mix(&sink->mix, &s->voice, frames, s->drain_pending);
        s->playing_for += (unsigned long long)played * s->voice.frame;
        if (played < frames)
        {
                // Out of data: it faded to nothing, it waits for its start again (a drain is answered once the card has played it).
                s->underrun = 1;
                s->trigger = 0;
                if (s->drain_pending)
                {
                        if (!s->drained)
                        {
                                s->drained = 1;
                                s->drain_at = now + PULSE_DRAIN_USEC;
                        }
                }
                else if (!s->flushing)
                        pulse_stream_signal(p, s, PULSE_COMMAND_UNDERFLOW, 1);
                s->flushing = 0;
        }
}

/*
        One block of the sink to the card: frames (at most AUDIO_BLOCK) of every stream on it, mixed, at the master volume,
        encoded to the card's format and channels into dst. Answers the bytes. The programs of the streams are asked for
        what they have left room for, and a stream that was ending is finished.
*/
AUDIO_API long pulse_sink_mix(struct pulse *p, long slot, unsigned char *dst, long format, long channels, long frames)
{
        struct pulse_sink *sink = p->sink + slot;
        long bytes;

        if (frames > AUDIO_BLOCK)
                frames = AUDIO_BLOCK;
        audio_sink_begin(&sink->mix, frames);
        audio_sink_gain(&sink->mix, sink->muted ? 0.0f : pulse_mean_gain(sink->volume, 2));
        for (int i = 0; i < PULSE_STREAMS; i++)
                if (p->stream[i].used && p->stream[i].sink == slot)
                        pulse_stream_play(p, p->stream + i, sink, frames);
        bytes = audio_sink_end(&sink->mix, dst, format, channels, frames);
        sink->played += (unsigned long long)frames;
        for (int i = 0; i < PULSE_STREAMS; i++)
                if (p->stream[i].used && p->stream[i].sink == slot)
                        pulse_stream_request(p, p->stream + i);
        return bytes;
}

// Time has passed: a drain whose last frames have left the card is answered.
AUDIO_API void pulse_tick(struct pulse *p)
{
        long now = p->host.now(p->host.context);

        for (int i = 0; i < PULSE_STREAMS; i++)
        {
                struct pulse_stream *s = p->stream + i;
                struct pulse_client *c;

                if (s->used && !s->dying && s->drain_pending && s->drained && now >= s->drain_at && s->client >= 0 && (c = p->client[s->client]))
                {
                        s->drain_pending = 0;
                        s->drained = 0;
                        pulse_ack(p, c, s->drain_tag);
                }
        }
}

AUDIO_API void pulse_sink_delay(struct pulse *p, long slot, long frames)
{
        p->sink[slot].delay_frames = frames;
}

#endif // AUDIO_PULSE_INCLUDED
