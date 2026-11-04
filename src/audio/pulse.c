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
        updates, names and properties, moving to a sink), record streams (below), the sample cache (upload, play by
        name, remove, info), the lists and infos of sinks, sources, clients, sink inputs, source outputs and samples
        (and empty lists of modules and cards), subscription
        events, volumes and mutes, the default sink and the default source. What is not served is answered with the
        error a client expects (NOTIMPLEMENTED, NOENTITY, NOEXTENSION, ACCESS) and never ignored: a client waiting for
        an answer gets one.

        RECORDING

        A source is where sound comes from: the monitor of each sink (what the sink plays, after its volume and mute) or a
        capture device the host adds (pulse_source_add). A record stream is a program that listens to one: the host feeds
        a source blocks of a stereo float pair (pulse_source_feed, a capture device's from the card, a monitor's from
        pulse_sink_mix), and every stream on it takes the block at the source's volume, at its own gain, resampled to its
        rate, in its format and channels (every format but the two laws), and has it queued to its program at once as data
        frames of its fragment size at most; a program that does not read loses data, never replies. A stream of peaks
        (PEAK_DETECT) is the greatest sample of each window instead, the way a meter wants it. Another user's program
        records a capture device and not what the session plays; the person's switch (pulse_capture_allow) ends every
        record stream and refuses new ones. The host runs a capture device while pulse_source_wanted says a program
        listens to it.
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
#define PULSE_SOURCES (PULSE_SINKS + 8) // the first PULSE_SINKS are the sinks' monitors (a monitor has its sink's number), the rest are capture devices
#define PULSE_RECORDS 32                // record streams, all clients together
#define PULSE_RECORDS_PER_CLIENT 8
#define PULSE_RECORD_CHUNK 16384        // the most of a record stream's data that one frame carries
#define PULSE_RECORD_RESERVE 32768      // of a client's output queue kept for its replies and events: record data is dropped before they are
#define PULSE_SAMPLES 64                // sounds in the cache
#define PULSE_SAMPLE_MAX (4ul << 20)    // bytes of one
#define PULSE_SAMPLE_TOTAL (16ul << 20) // and of all of them, with the uploads begun
#define PULSE_SAMPLE_USER (4ul << 20)   // and of what a user who is not root holds of them
#define PULSE_UPLOADS 8                 // sounds being uploaded, all clients together
#define PULSE_UPLOADS_PER_CLIENT 2
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
#define PULSE_COMMAND_RECORD_STREAM_KILLED 65
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
#define PULSE_FACILITY_SOURCE_OUTPUT 3
#define PULSE_FACILITY_SAMPLE_CACHE 6
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
#define PULSE_CHANGED_SOURCE_VOLUME 4 // a capture source's volume or mute (the slot is the source's)
#define PULSE_CHANGED_DEFAULT_SOURCE 5
#define PULSE_CHANGED_RECORDS 6       // a record stream began or ended

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
        int one_shot;                   // a sample being played: it belongs to no connection, and ends when its frames are done
        long sample_ref;                // the sample it plays, plus one
        unsigned long maker;            // the connection (its index) that asked for it
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

struct pulse_sample
{
        int used, condemned;            // condemned: out of the cache, kept for the streams that are playing it
        unsigned long index;
        char name[PULSE_NAME];
        long format, channels, rate;
        signed char map[AUDIO_CHANNELS_MAX];
        long map_channels;
        unsigned char *data;
        unsigned long bytes;
        struct pulse_props props;
        long uid;
        long playing;
};

struct pulse_upload
{
        int used;
        long client;
        unsigned long channel;
        char name[PULSE_NAME];
        long format, channels, rate;
        signed char map[AUDIO_CHANNELS_MAX];
        long map_channels;
        struct pulse_props props;
        unsigned char *data;
        unsigned long length, got;
        long uid;
};

/*
        A source is where sound comes from: the monitor of a sink (what the sink plays, after its volume; the first
        PULSE_SINKS places, a monitor having its sink's number) or a capture device (the rest). Its bus is the block the
        host fed it, at its own volume, which every record stream on it takes from.
*/
struct pulse_source
{
        int used, capture;
        char name[PULSE_NAME + 16], description[PULSE_DESCRIPTION + 24];
        long rate;
        long sink;                      // a monitor: the slot of its sink
        unsigned long volume[2];
        int muted;
        long delay_frames;              // what the device holds, as the host last read it
        unsigned long long fed;         // frames
        float now;                      // the gain it is at (it ramps to the volume, or to nothing when muted)
        float peak[2];                  // the greatest sample since the page last took it
        float bus[2][AUDIO_BLOCK];
};

/*
        A record stream (a source output): a program that listens. What the source makes is converted to the program's
        format, channels and rate and queued to the program's connection as data frames on its channel; a program that does
        not read costs the sound nothing, its data is dropped before its replies are.
*/
struct pulse_record
{
        int used;
        long client;                    // slot of the client that made it
        unsigned long channel;          // the client's name for it, which data frames carry
        unsigned long index;            // the source output's, which introspection and volume use
        long source;
        struct pulse_props props;
        long format, channels, rate;
        signed char map[AUDIO_CHANNELS_MAX];
        long map_channels;
        unsigned long volume[PULSE_CHANNEL_MAX];
        int muted, corked, peak_detect, adjust_latency, variable_rate, early_requests, no_move, owner_set;
        unsigned long maxlength, fragsize;
        unsigned long long produced, sent; // bytes made, and bytes queued to the client
        unsigned long long dropped;        // bytes made and not queued (the client did not read)
        long uid, pid;
        float now[2], target;           // the stream's own gain
        float peak[2];                  // the greatest sample since the page last took it
        float window[2];                // peak detect: the greatest of the frames of the window so far
        long window_count;
        struct audio_table *table;      // the resampler's, none at equal rates (and none for peak detect)
        struct audio_resampler rs;
};

struct pulse
{
        struct pulse_host host;
        struct pulse_client *client[PULSE_CLIENTS];
        struct pulse_stream stream[PULSE_STREAMS];
        struct pulse_sink sink[PULSE_SINKS];
        struct pulse_source source[PULSE_SOURCES];
        struct pulse_record record[PULSE_RECORDS];
        struct pulse_sample sample[PULSE_SAMPLES];
        struct pulse_upload upload[PULSE_UPLOADS];
        unsigned long sample_total, upload_total, next_sample;
        long default_sink, default_source;
        int capture_off;                // the person has turned capture off: no program records
        unsigned long next_client, next_input, next_output;
        unsigned long ring_total;
        unsigned char scratch[PULSE_SCRATCH];
        float record_a[2][AUDIO_BLOCK], record_b[2][AUDIO_BLOCK];
        unsigned char record_bytes[AUDIO_BLOCK * AUDIO_CHANNELS_MAX * 4 * 2];
        // What the checks count back to nothing.
        unsigned long clients_made, clients_dropped, streams_made, streams_freed, protocol_errors;
        unsigned long records_made, records_freed;
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
        long start, avail;
        const unsigned char *nul;

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
        // The terminator is searched for within the frame and within PULSE_STRING + 1 bytes (one more than a string may be): found,
        // the string is the bytes before it; not found, the frame or the string ran out first.
        start = r->at + 1;
        avail = r->length - start < PULSE_STRING + 1 ? r->length - start : PULSE_STRING + 1;
        nul = avail > 0 ? __builtin_memchr(r->p + start, 0, (unsigned long)avail) : 0;
        if (!nul)
        {
                r->bad = 1;
                return 0;
        }
        r->at = (long)(nul - r->p) + 1;
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

// A string into a buffer of room bytes, cut to fit and always terminated. (Measured on an M2 Pro: a byte loop 8.9 ns for 24 bytes and
// 26 ns for 64; the length and the copy 5.2 and 11.2 ns; the same at 8 bytes.)
static void pulse_copy(char *to, long room, const char *from)
{
        long n;

        if (room < 1)
                return;
        n = (long)__builtin_strlen(from);
        if (n > room - 1)
                n = room - 1;
        __builtin_memcpy(to, from, (unsigned long)n);
        to[n] = 0;
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
                                        // Four bytes are a code point from U+10000 to U+10FFFF: F0 below 90 is overlong, F4 from 90 is past the end.
                                        if (good && extra == 3 && c == 0xf0 && v[at + 1] < 0x90)
                                                good = 0;
                                        if (good && extra == 3 && c == 0xf4 && v[at + 1] >= 0x90)
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

// Bytes onto the client's output ring at its tail, the ring wrapping at most once (n is less than PULSE_OUT). Up to 64 bytes
// they go by a byte loop: an out-of-line copy costs more than that (measured on an M2 Pro: 6.9 ns a 64-byte frame by the loop,
// 8 to 9 ns with a call); longer, by one copy, or two when the ring wraps.
static void pulse_out_put(struct pulse_client *c, const unsigned char *from, long n)
{
        unsigned long at;
        long first;

        if (n <= 0)
                return;
        if (n <= 64)
        {
                for (long i = 0; i < n; i++)
                        c->out[(unsigned long)(c->out_tail + i) % PULSE_OUT] = from[i];
                c->out_tail += n;
                return;
        }
        at = (unsigned long)c->out_tail % PULSE_OUT;
        first = (long)(PULSE_OUT - at) < n ? (long)(PULSE_OUT - at) : n;
        __builtin_memcpy(c->out + at, from, (unsigned long)first);
        if (first < n)
                __builtin_memcpy(c->out, from + first, (unsigned long)(n - first));
        c->out_tail += n;
}

// A frame to the client: the descriptor (length, channel, offset, flags), then the payload. The client is dead if there is no room.
static int pulse_enqueue(struct pulse_client *c, unsigned long channel, const unsigned char *payload, long length)
{
        unsigned char header[20] = {(unsigned char)(length >> 24), (unsigned char)(length >> 16), (unsigned char)(length >> 8), (unsigned char)length,
                                    (unsigned char)(channel >> 24), (unsigned char)(channel >> 16), (unsigned char)(channel >> 8), (unsigned char)channel,
                                    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
        unsigned long at;

        if (pulse_out_fill(c) + 20 + length > PULSE_OUT)
                return PULSE_DEAD;
        at = (unsigned long)c->out_tail % PULSE_OUT;
        // The descriptor is 20 bytes: a constant-size copy the compiler inlines, unless it straddles the end of the ring.
        if (at + 20 <= PULSE_OUT)
        {
                __builtin_memcpy(c->out + at, header, 20);
                c->out_tail += 20;
        }
        else
                pulse_out_put(c, header, 20);
        pulse_out_put(c, payload, length);
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

static void pulse_monitor_set(struct pulse *p, long slot);
static void pulse_source_retable(struct pulse *p, long slot);
static int pulse_record_move(struct pulse *p, struct pulse_record *r, long slot);

/*
        A sink for a card (or a clock): its name as clients see it, the description the person sees, and the rate the
        card is run at. Answers its slot or a negative number. The first becomes the default. Its monitor, the source that
        is what it plays, is made with it.
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
                        __builtin_memset(p->source + i, 0, sizeof(p->source[i]));
                        pulse_monitor_set(p, i);
                        if (p->default_source < 0)
                                p->default_source = i;
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
        int changed = 0;

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
                changed = 1;
        }
        pulse_monitor_set(p, slot);
        if (changed)
                pulse_source_retable(p, slot);
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

static void pulse_sample_release(struct pulse *p, struct pulse_sample *s);

static void pulse_stream_free(struct pulse *p, struct pulse_stream *s)
{
        audio_voice_close(&s->voice);
        if (s->sample_ref)
        {
                struct pulse_sample *sample = p->sample + s->sample_ref - 1;

                sample->playing--;
                pulse_sample_release(p, sample);
        }
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

/* ---- the sources ---- */

static float pulse_source_target(const struct pulse_source *s)
{
        return s->muted ? 0.0f : pulse_mean_gain(s->volume, 2);
}

// A sink's monitor: the source that is what the sink plays. It has its sink's number, and its name and description follow the sink's.
static void pulse_monitor_set(struct pulse *p, long slot)
{
        struct pulse_source *m = p->source + slot;
        const struct pulse_sink *s = p->sink + slot;
        long n;

        if (!m->used)
        {
                __builtin_memset(m, 0, sizeof(*m));
                m->used = 1;
                m->volume[0] = m->volume[1] = AUDIO_VOLUME_NORM;
                m->now = 1.0f;
        }
        m->sink = slot;
        m->rate = s->rate;
        pulse_copy(m->name, (long)sizeof(m->name), s->name);
        n = pulse_len(m->name);
        pulse_copy(m->name + n, (long)sizeof(m->name) - n, ".monitor");
        pulse_copy(m->description, (long)sizeof(m->description), "Monitor of ");
        n = pulse_len(m->description);
        pulse_copy(m->description + n, (long)sizeof(m->description) - n, s->description);
}

static long pulse_source_named(struct pulse *p, const char *name)
{
        long number;

        for (long i = 0; i < PULSE_SOURCES; i++)
                if (p->source[i].used && pulse_equal(p->source[i].name, name))
                        return i;
        number = pulse_number(name);
        return number >= 0 && number < PULSE_SOURCES && p->source[number].used ? number : -1;
}

static long pulse_find_source(struct pulse *p, unsigned long index, const char *name)
{
        if (name && name[0])
                return pulse_source_named(p, name);
        if (index == PULSE_INVALID)
                return -1;
        return index < PULSE_SOURCES && p->source[index].used ? (long)index : -1;
}

/* ---- the record streams ---- */

static struct pulse_record *pulse_record_of(struct pulse *p, long client, unsigned long channel)
{
        for (int i = 0; i < PULSE_RECORDS; i++)
                if (p->record[i].used && p->record[i].client == client && p->record[i].channel == channel)
                        return p->record + i;
        return 0;
}

static struct pulse_record *pulse_output_of(struct pulse *p, unsigned long index)
{
        for (int i = 0; i < PULSE_RECORDS; i++)
                if (p->record[i].used && p->record[i].index == index)
                        return p->record + i;
        return 0;
}

static void pulse_record_apply(struct pulse_record *r)
{
        r->target = r->muted ? 0.0f : pulse_mean_gain(r->volume, r->map_channels);
}

static void pulse_record_free(struct pulse *p, struct pulse_record *r)
{
        audio_table_give(r->table);
        r->table = 0;
        r->used = 0;
        p->records_freed++;
}

// A record stream ends: the program is told (unless it is the one that went), and everyone who asked is told it is gone.
static void pulse_record_end(struct pulse *p, struct pulse_record *r, int tell)
{
        struct pulse_client *c = r->client >= 0 ? p->client[r->client] : 0;

        if (tell && c)
        {
                struct pulse_writer w = {p->scratch, PULSE_SCRATCH, 0, 0};

                pulse_w_u32(&w, PULSE_COMMAND_RECORD_STREAM_KILLED);
                pulse_w_u32(&w, PULSE_INVALID);
                pulse_w_u32(&w, r->channel);
                pulse_enqueue(c, PULSE_INVALID, w.p, w.len);
        }
        pulse_event(p, PULSE_FACILITY_SOURCE_OUTPUT, PULSE_EVENT_REMOVE, r->index);
        pulse_changed(p, PULSE_CHANGED_RECORDS, r->source);
        pulse_record_free(p, r);
}

// The source runs at another rate now: the streams on it start their filters over, and one with no table for the ratio is ended.
static void pulse_source_retable(struct pulse *p, long slot)
{
        for (int i = 0; i < PULSE_RECORDS; i++)
        {
                struct pulse_record *r = p->record + i;
                struct audio_table *table = 0;

                if (!r->used || r->source != slot || r->peak_detect)
                        continue;
                if (r->rate != p->source[slot].rate)
                {
                        table = audio_table_take(p->source[slot].rate, r->rate);
                        if (!table)
                        {
                                pulse_record_end(p, r, 1);
                                continue;
                        }
                }
                audio_table_give(r->table);
                r->table = table;
                if (table)
                        audio_rs_open(&r->rs, table);
        }
}

static int pulse_record_format_ok(long format)
{
        return format == AUDIO_FORMAT_U8 || format == AUDIO_FORMAT_S16LE || format == AUDIO_FORMAT_S16BE || format == AUDIO_FORMAT_F32LE ||
               format == AUDIO_FORMAT_F32BE || format == AUDIO_FORMAT_S32LE || format == AUDIO_FORMAT_S32BE || format == AUDIO_FORMAT_S24LE ||
               format == AUDIO_FORMAT_S24BE || format == AUDIO_FORMAT_S24_32LE || format == AUDIO_FORMAT_S24_32BE;
}

/*
        The pair as the program's format and channels: the library's little-endian encoders, the bytes of a sample turned over
        for a big-endian format, and the high byte (offset) of 16 bits for U8. Answers the bytes.
*/
static long pulse_record_encode(unsigned char *dst, long format, long frames, long channels, const float *left, const float *right)
{
        long samples = frames * channels, base = format, width, bytes;
        int swap = 0;

        switch (format)
        {
        case AUDIO_FORMAT_S16BE:
                base = AUDIO_FORMAT_S16LE;
                swap = 1;
                break;
        case AUDIO_FORMAT_F32BE:
                base = AUDIO_FORMAT_F32LE;
                swap = 1;
                break;
        case AUDIO_FORMAT_S32BE:
                base = AUDIO_FORMAT_S32LE;
                swap = 1;
                break;
        case AUDIO_FORMAT_S24BE:
                base = AUDIO_FORMAT_S24LE;
                swap = 1;
                break;
        case AUDIO_FORMAT_S24_32BE:
                base = AUDIO_FORMAT_S24_32LE;
                swap = 1;
                break;
        default:
                break;
        }
        if (format == AUDIO_FORMAT_U8)
        {
                // The 16 bits go behind the bytes made, each of which is read from before it is written over.
                unsigned char *wide = dst + samples;

                audio_encode(AUDIO_FORMAT_S16LE, wide, frames, channels, left, right);
                for (long i = 0; i < samples; i++)
                {
                        int v = (short)(wide[2 * i] | wide[2 * i + 1] << 8);

                        v = (v + 128) >> 8;
                        dst[i] = (unsigned char)((v > 127 ? 127 : v) + 128);
                }
                return samples;
        }
        bytes = audio_encode(base, dst, frames, channels, left, right);
        if (swap)
        {
                width = audio_format_bytes(base);
                for (long i = 0; i < samples; i++)
                        for (long k = 0; k < width / 2; k++)
                        {
                                unsigned char t = dst[i * width + k];

                                dst[i * width + k] = dst[i * width + width - 1 - k];
                                dst[i * width + width - 1 - k] = t;
                        }
        }
        return bytes;
}

// Bytes made for a program: queued to it in frames of its fragment size at most, or dropped (and counted) when it has not read what it was sent.
static void pulse_record_post(struct pulse *p, struct pulse_record *r, const unsigned char *data, long count)
{
        struct pulse_client *c = r->client >= 0 ? p->client[r->client] : 0;
        long frame = audio_format_bytes(r->format) * r->channels, chunk = PULSE_RECORD_CHUNK;

        r->produced += (unsigned long long)count;
        if (!c)
                return;
        if (r->fragsize >= (unsigned long)frame && r->fragsize < (unsigned long)chunk)
                chunk = (long)r->fragsize;
        chunk -= chunk % frame;
        while (count > 0)
        {
                long n = count < chunk ? count : chunk;

                if (PULSE_OUT - pulse_out_fill(c) - 20 - n < PULSE_RECORD_RESERVE)
                {
                        r->dropped += (unsigned long long)count;
                        return;
                }
                pulse_enqueue(c, r->channel, data, n);
                r->sent += (unsigned long long)n;
                data += n;
                count -= n;
        }
}

// n frames of the stream's own pair, at the stream's rate: at the stream's gain, in the program's format, to the program.
static void pulse_record_emit(struct pulse *p, struct pulse_record *r, const float *a0, const float *a1, long n)
{
        float *g0 = p->record_b[0], *g1 = p->record_b[1], now0 = r->now[0], now1 = r->now[1];
        long bytes;

        audio_gain_run(g0, a0, n, &now0, r->target, audio_slope(r->now[0], r->target), r->peak, 0);
        audio_gain_run(g1, a1, n, &now1, r->target, audio_slope(r->now[1], r->target), r->peak + 1, 0);
        r->now[0] = now0;
        r->now[1] = now1;
        bytes = pulse_record_encode(p->record_bytes, r->format, n, r->channels, g0, g1);
        pulse_record_post(p, r, p->record_bytes, bytes);
}

/*
        Peak detect: the stream's frames are the greatest sample of each window of the source's frames (a window is the
        source's rate over the stream's, in whole frames), the way PulseAudio's "peaks" resampler makes them, so that a
        program draws a meter from a few frames a second. The stream's own volume does not take part: it measures the source.
*/
static void pulse_record_peaks(struct pulse *p, struct pulse_record *r, const float *left, const float *right, long frames, long source_rate)
{
        float *o0 = p->record_a[0], *o1 = p->record_a[1];
        long window = source_rate / r->rate, made = 0;

        if (window < 1)
                window = 1;
        for (long i = 0; i < frames; i++)
        {
                float a = left[i] < 0 ? -left[i] : left[i], b = right[i] < 0 ? -right[i] : right[i];

                r->window[0] = a > r->window[0] ? a : r->window[0];
                r->window[1] = b > r->window[1] ? b : r->window[1];
                if (++r->window_count >= window)
                {
                        o0[made] = r->window[0];
                        o1[made] = r->window[1];
                        if (r->channels == 1)
                                o0[made] = o1[made] = r->window[0] > r->window[1] ? r->window[0] : r->window[1];
                        made++;
                        r->window[0] = r->window[1] = 0;
                        r->window_count = 0;
                }
        }
        if (made)
                pulse_record_post(p, r, p->record_bytes, pulse_record_encode(p->record_bytes, r->format, made, r->channels, o0, o1));
}

// A block of the source (its bus, at its volume): into the stream, resampled to its rate when it has a table.
static void pulse_record_take(struct pulse *p, struct pulse_record *r, const float *left, const float *right, long frames, long source_rate)
{
        if (r->peak_detect)
        {
                pulse_record_peaks(p, r, left, right, frames, source_rate);
                return;
        }
        if (!r->table)
        {
                pulse_record_emit(p, r, left, right, frames);
                return;
        }
        {
                struct audio_resampler *rs = &r->rs;
                long room = audio_rs_room(rs), take = frames < room ? frames : room;

                __builtin_memcpy(rs->buf[0] + rs->have, left, (unsigned long)take * sizeof(float));
                __builtin_memcpy(rs->buf[1] + rs->have, right, (unsigned long)take * sizeof(float));
                rs->have += take;
                for (;;)
                {
                        long ready = audio_rs_ready(rs), m = ready < AUDIO_BLOCK ? ready : AUDIO_BLOCK, made;

                        if (m < 1)
                                break;
                        made = audio_rs_run(rs, p->record_a[0], p->record_a[1], m);
                        pulse_record_emit(p, r, p->record_a[0], p->record_a[1], made);
                }
        }
}

/*
        A block (at most AUDIO_BLOCK frames, as a planar pair at the source's rate) of what a source makes: at the source's
        volume, to every record stream on it that is not corked. A source nobody records from costs a scan of the table.
*/
AUDIO_API void pulse_source_feed(struct pulse *p, long slot, const float *left, const float *right, long frames)
{
        struct pulse_source *s = p->source + slot;
        float target, a, b;
        int any = 0;

        if (!s->used || frames < 1)
                return;
        for (int i = 0; i < PULSE_RECORDS && !any; i++)
                any = p->record[i].used && p->record[i].source == slot && !p->record[i].corked;
        if (!any)
                return;
        if (frames > AUDIO_BLOCK)
                frames = AUDIO_BLOCK;
        target = pulse_source_target(s);
        a = b = s->now;
        audio_gain_run(s->bus[0], left, frames, &a, target, audio_slope(s->now, target), s->peak, 0);
        audio_gain_run(s->bus[1], right, frames, &b, target, audio_slope(s->now, target), s->peak + 1, 0);
        s->now = b;
        s->fed += (unsigned long long)frames;
        for (int i = 0; i < PULSE_RECORDS; i++)
                if (p->record[i].used && p->record[i].source == slot && !p->record[i].corked)
                        pulse_record_take(p, p->record + i, s->bus[0], s->bus[1], frames, s->rate);
}

// The record streams that are listening on a source: the host runs a capture device while there are any.
AUDIO_API long pulse_source_wanted(struct pulse *p, long slot)
{
        long n = 0;

        for (int i = 0; i < PULSE_RECORDS; i++)
                n += p->record[i].used && p->record[i].source == slot && !p->record[i].corked;
        return n;
}

AUDIO_API void pulse_source_delay(struct pulse *p, long slot, long frames)
{
        p->source[slot].delay_frames = frames;
}

// The person's capture switch: off ends every record stream there is, and none is made while it is off.
AUDIO_API void pulse_capture_allow(struct pulse *p, int allowed)
{
        if (!p->capture_off == !!allowed)
                return;
        p->capture_off = !allowed;
        if (!allowed)
                for (int i = 0; i < PULSE_RECORDS; i++)
                        if (p->record[i].used)
                                pulse_record_end(p, p->record + i, 1);
        pulse_event(p, PULSE_FACILITY_SERVER, PULSE_EVENT_CHANGE, 0);
}

// The source a program that does not name one is given: the first capture device, else the monitor of the default sink.
static long pulse_source_fallback(struct pulse *p)
{
        for (long i = PULSE_SINKS; i < PULSE_SOURCES; i++)
                if (p->source[i].used)
                        return i;
        return p->default_sink >= 0 && p->source[p->default_sink].used ? p->default_sink : -1;
}

/*
        A capture device: its name as clients see it, the description the person sees, and the rate it is run at. Answers its
        slot (a number from PULSE_SINKS up) or a negative number. A capture device is the default source over a monitor.
*/
AUDIO_API long pulse_source_add(struct pulse *p, const char *name, const char *description, long rate)
{
        for (long i = PULSE_SINKS; i < PULSE_SOURCES; i++)
                if (!p->source[i].used)
                {
                        struct pulse_source *s = p->source + i;

                        __builtin_memset(s, 0, sizeof(*s));
                        s->used = 1;
                        s->capture = 1;
                        s->sink = -1;
                        pulse_copy(s->name, (long)sizeof(s->name), name);
                        pulse_copy(s->description, (long)sizeof(s->description), description);
                        s->rate = rate;
                        s->volume[0] = s->volume[1] = AUDIO_VOLUME_NORM;
                        s->now = 1.0f;
                        pulse_event(p, PULSE_FACILITY_SOURCE, PULSE_EVENT_NEW, (unsigned long)i);
                        if (p->default_source < 0 || p->default_source < PULSE_SINKS)
                        {
                                p->default_source = i;
                                pulse_event(p, PULSE_FACILITY_SERVER, PULSE_EVENT_CHANGE, 0);
                                pulse_changed(p, PULSE_CHANGED_DEFAULT_SOURCE, i);
                        }
                        return i;
                }
        return -1;
}

// The capture device is another now (the person chose another): its streams go on from it, at the new rate.
AUDIO_API void pulse_source_reconfigure(struct pulse *p, long slot, const char *name, const char *description, long rate)
{
        struct pulse_source *s = p->source + slot;
        int changed = rate != s->rate;

        pulse_copy(s->name, (long)sizeof(s->name), name);
        pulse_copy(s->description, (long)sizeof(s->description), description);
        s->rate = rate;
        if (changed)
                pulse_source_retable(p, slot);
        pulse_event(p, PULSE_FACILITY_SOURCE, PULSE_EVENT_CHANGE, (unsigned long)slot);
        pulse_event(p, PULSE_FACILITY_SERVER, PULSE_EVENT_CHANGE, 0);
}

// The capture device is gone: its streams are ended (the programs are told), and the default source is another.
AUDIO_API void pulse_source_remove(struct pulse *p, long slot)
{
        for (int i = 0; i < PULSE_RECORDS; i++)
                if (p->record[i].used && p->record[i].source == slot)
                        pulse_record_end(p, p->record + i, 1);
        p->source[slot].used = 0;
        pulse_event(p, PULSE_FACILITY_SOURCE, PULSE_EVENT_REMOVE, (unsigned long)slot);
        if (p->default_source == slot)
        {
                p->default_source = pulse_source_fallback(p);
                pulse_event(p, PULSE_FACILITY_SERVER, PULSE_EVENT_CHANGE, 0);
                pulse_changed(p, PULSE_CHANGED_DEFAULT_SOURCE, p->default_source);
        }
}

/*
        The default source is this one (the person's choice, or a program's with the right to). A record stream that did not
        name its source follows it. Answers 0 for a place that is no source.
*/
AUDIO_API int pulse_source_default(struct pulse *p, long slot)
{
        if (slot < 0 || slot >= PULSE_SOURCES || !p->source[slot].used)
                return 0;
        if (slot == p->default_source)
                return 1;
        p->default_source = slot;
        for (int i = 0; i < PULSE_RECORDS; i++)
                if (p->record[i].used && !p->record[i].owner_set && !p->record[i].no_move)
                        pulse_record_move(p, p->record + i, slot);
        pulse_event(p, PULSE_FACILITY_SERVER, PULSE_EVENT_CHANGE, 0);
        pulse_changed(p, PULSE_CHANGED_DEFAULT_SOURCE, slot);
        return 1;
}

static void pulse_upload_free(struct pulse *p, struct pulse_upload *u);

/* ---- a client's departure ---- */

static void pulse_drop(struct pulse *p, long slot)
{
        struct pulse_client *c = p->client[slot];

        if (!c)
                return;
        for (int i = 0; i < PULSE_STREAMS; i++)
                if (p->stream[i].used && !p->stream[i].dying && p->stream[i].client == slot)
                        pulse_stream_end(p, p->stream + i);
        for (int i = 0; i < PULSE_RECORDS; i++)
                if (p->record[i].used && p->record[i].client == slot)
                        pulse_record_end(p, p->record + i, 0);
        for (int i = 0; i < PULSE_UPLOADS; i++)
                if (p->upload[i].used && p->upload[i].client == slot)
                        pulse_upload_free(p, p->upload + i);
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
        const struct pulse_source *s = p->source + slot;
        static const signed char stereo[2] = {AUDIO_POSITION_FRONT_LEFT, AUDIO_POSITION_FRONT_RIGHT};
        struct pulse_props props = {0};
        long latency = s->delay_frames * 1000000l / (s->rate ? s->rate : 1);
        int busy = 0;

        for (int i = 0; i < PULSE_RECORDS; i++)
                busy |= p->record[i].used && p->record[i].source == slot && !p->record[i].corked;
        pulse_w_u32(w, (unsigned long)slot);
        pulse_w_string(w, s->name);
        pulse_w_string(w, s->description);
        pulse_w_sample_spec(w, AUDIO_FORMAT_F32LE, 2, s->rate);
        pulse_w_channel_map(w, stereo, 2);
        pulse_w_u32(w, PULSE_INVALID);
        pulse_w_cvolume(w, s->volume, 2);
        pulse_w_bool(w, s->muted);
        pulse_w_u32(w, s->capture ? PULSE_INVALID : (unsigned long)s->sink);
        pulse_w_string(w, s->capture ? 0 : p->sink[s->sink].name);
        pulse_w_64(w, 'U', latency);
        pulse_w_string(w, "moonwater audio");
        pulse_w_u32(w, s->capture ? 0x2 | 0x4 | 0x10 : 0x1 | 0x2 | 0x4 | 0x8 | 0x10);
        if (c->version >= 13)
        {
                pulse_props_set(&props, "device.description", (const unsigned char *)s->description, pulse_len(s->description) + 1);
                pulse_props_set(&props, "device.class", (const unsigned char *)(s->capture ? "sound" : "monitor"), s->capture ? 6 : 8);
                pulse_w_proplist(w, &props);
                pulse_w_64(w, 'U', 0);
        }
        if (c->version >= 15)
        {
                pulse_w_volume(w, AUDIO_VOLUME_NORM);
                pulse_w_u32(w, busy ? 0 : 1); // running, or idle
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

// A record stream as introspection shows it: the source output.
static void pulse_fill_output(struct pulse *p, struct pulse_writer *w, struct pulse_client *c, struct pulse_record *r)
{
        char name[PULSE_VALUE + 1];
        const struct pulse_source *s = p->source + r->source;
        long source_latency = s->delay_frames * 1000000l / (s->rate ? s->rate : 1), queued = 0;
        long frame = audio_format_bytes(r->format) * r->channels;
        unsigned long volume[PULSE_CHANNEL_MAX];

        if (r->client >= 0 && p->client[r->client] && frame && r->rate)
                queued = pulse_out_fill(p->client[r->client]) / frame * 1000000l / r->rate;
        pulse_props_text(&r->props, "media.name", name, sizeof(name));
        for (long i = 0; i < r->map_channels; i++)
                volume[i] = r->volume[i];
        pulse_w_u32(w, r->index);
        pulse_w_string(w, name);
        pulse_w_u32(w, PULSE_INVALID);
        pulse_w_u32(w, r->client >= 0 && p->client[r->client] ? p->client[r->client]->index : PULSE_INVALID);
        pulse_w_u32(w, (unsigned long)r->source);
        pulse_w_sample_spec(w, r->format, r->channels, r->rate);
        pulse_w_channel_map(w, r->map, r->map_channels);
        pulse_w_64(w, 'U', queued);
        pulse_w_64(w, 'U', source_latency);
        pulse_w_string(w, "copy");
        pulse_w_string(w, "protocol-native");
        if (c->version >= 13)
                pulse_w_proplist(w, &r->props);
        if (c->version >= 19)
                pulse_w_bool(w, r->corked);
        if (c->version >= 22)
        {
                pulse_w_cvolume(w, volume, r->map_channels);
                pulse_w_bool(w, r->muted);
                pulse_w_bool(w, 1);
                pulse_w_bool(w, 1);
                pulse_w_format_info(w);
        }
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
                long source = pulse_find_source(p, index, name);

                if (source < 0)
                        return pulse_error(p, c, tag, PULSE_ERR_NOENTITY);
                if (!c->root)
                        return pulse_error(p, c, tag, PULSE_ERR_ACCESS);
                if (n != 2)
                        return pulse_error(p, c, tag, PULSE_ERR_INVALID);
                p->source[source].volume[0] = volume[0];
                p->source[source].volume[1] = volume[1];
                pulse_event(p, PULSE_FACILITY_SOURCE, PULSE_EVENT_CHANGE, (unsigned long)source);
                pulse_changed(p, PULSE_CHANGED_SOURCE_VOLUME, source);
        }
        else if (command == PULSE_COMMAND_SET_SOURCE_OUTPUT_VOLUME)
        {
                struct pulse_record *rec = pulse_output_of(p, index);

                if (!rec)
                        return pulse_error(p, c, tag, PULSE_ERR_NOENTITY);
                if (!pulse_may(c, rec->uid))
                        return pulse_error(p, c, tag, PULSE_ERR_ACCESS);
                if (n != rec->map_channels)
                        return pulse_error(p, c, tag, PULSE_ERR_INVALID);
                for (long i = 0; i < n; i++)
                        rec->volume[i] = volume[i];
                pulse_record_apply(rec);
                pulse_event(p, PULSE_FACILITY_SOURCE_OUTPUT, PULSE_EVENT_CHANGE, rec->index);
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
                long source = pulse_find_source(p, index, name);

                if (source < 0)
                        return pulse_error(p, c, tag, PULSE_ERR_NOENTITY);
                if (!c->root)
                        return pulse_error(p, c, tag, PULSE_ERR_ACCESS);
                p->source[source].muted = mute;
                pulse_event(p, PULSE_FACILITY_SOURCE, PULSE_EVENT_CHANGE, (unsigned long)source);
                pulse_changed(p, PULSE_CHANGED_SOURCE_VOLUME, source);
        }
        else if (command == PULSE_COMMAND_SET_SOURCE_OUTPUT_MUTE)
        {
                struct pulse_record *rec = pulse_output_of(p, index);

                if (!rec)
                        return pulse_error(p, c, tag, PULSE_ERR_NOENTITY);
                if (!pulse_may(c, rec->uid))
                        return pulse_error(p, c, tag, PULSE_ERR_ACCESS);
                rec->muted = mute;
                pulse_record_apply(rec);
                pulse_event(p, PULSE_FACILITY_SOURCE_OUTPUT, PULSE_EVENT_CHANGE, rec->index);
        }
        else
                return pulse_error(p, c, tag, PULSE_ERR_NOENTITY);
        return pulse_ack(p, c, tag);
}

/* ---- the commands on record streams ---- */

// A record stream is listened to from another source: its table for the new ratio, or an error and nothing changed.
static int pulse_record_move(struct pulse *p, struct pulse_record *r, long slot)
{
        struct audio_table *table = 0;

        if (r->source == slot)
                return 0;
        if (!r->peak_detect && r->rate != p->source[slot].rate)
        {
                table = audio_table_take(p->source[slot].rate, r->rate);
                if (!table)
                        return -1;
        }
        if (!r->peak_detect)
        {
                audio_table_give(r->table);
                r->table = table;
                if (table)
                        audio_rs_open(&r->rs, table);
        }
        pulse_changed(p, PULSE_CHANGED_RECORDS, r->source);
        r->source = slot;
        pulse_changed(p, PULSE_CHANGED_RECORDS, slot);
        pulse_event(p, PULSE_FACILITY_SOURCE_OUTPUT, PULSE_EVENT_CHANGE, r->index);
        return 0;
}

static unsigned long pulse_record_frame(const struct pulse_record *r)
{
        return (unsigned long)(audio_format_bytes(r->format) * r->channels);
}

// The fragment size and the longest buffer a program asked for, made what the stream can have.
static void pulse_record_attributes(struct pulse_record *r, unsigned long maxlength, unsigned long fragsize)
{
        unsigned long frame = pulse_record_frame(r);

        if (maxlength == PULSE_INVALID || maxlength == 0 || maxlength > PULSE_RING_MAX)
                maxlength = PULSE_RING_MAX;
        maxlength -= maxlength % frame;
        if (maxlength < frame)
                maxlength = frame;
        if (fragsize == PULSE_INVALID || fragsize == 0)
                fragsize = (unsigned long)r->rate / 50 * frame;
        if (fragsize > maxlength)
                fragsize = maxlength;
        fragsize -= fragsize % frame;
        if (fragsize < frame)
                fragsize = frame;
        r->maxlength = maxlength;
        r->fragsize = fragsize;
}

static int pulse_command_create_record(struct pulse *p, long slot, struct pulse_reader *r, unsigned long tag)
{
        struct pulse_client *c = p->client[slot];
        struct pulse_props props = {0};
        long format = 0, channels = 0, rate = 0, map_channels, source, volume_channels = 0, count = 0;
        signed char map[PULSE_CHANNEL_MAX];
        unsigned long source_index, maxlength, fragsize, direct = PULSE_INVALID, volume[PULSE_CHANNEL_MAX];
        const char *source_name;
        int corked, no_move = 0, variable = 0, peak = 0, adjust = 0, early = 0, muted = 0, volume_set = 0, muted_set = 0, isnull;
        struct pulse_record *rec = 0;
        struct audio_table *table = 0;
        struct pulse_writer w;
        int err;

        PULSE_NEED_AUTH();
        if (c->version < 13)
                pulse_r_string(r, &isnull); // the name of the stream, which version 13 put in the properties
        pulse_r_sample_spec(r, &format, &channels, &rate);
        map_channels = pulse_r_channel_map(r, map);
        source_index = pulse_r_u32(r);
        source_name = pulse_r_string(r, &isnull);
        maxlength = pulse_r_u32(r);
        corked = pulse_r_bool(r);
        fragsize = pulse_r_u32(r);
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
                peak = pulse_r_bool(r);
                adjust = pulse_r_bool(r);
                pulse_r_proplist(r, &props);
                direct = pulse_r_u32(r);
        }
        if (c->version >= 14)
                early = pulse_r_bool(r);
        if (c->version >= 15)
        {
                pulse_r_bool(r); // don't inhibit auto suspend
                pulse_r_bool(r); // fail on suspend
        }
        if (c->version >= 22)
        {
                unsigned long formats = pulse_r_u8(r);

                for (unsigned long i = 0; i < formats && !r->bad; i++)
                {
                        struct pulse_props ignored = {0};

                        pulse_tag(r, 'f');
                        pulse_r_u8(r);
                        pulse_r_proplist(r, &ignored);
                }
                volume_channels = pulse_r_cvolume(r, volume);
                muted = pulse_r_bool(r);
                volume_set = pulse_r_bool(r);
                muted_set = pulse_r_bool(r);
                pulse_r_bool(r); // relative volume
                pulse_r_bool(r); // passthrough
        }
        if (!pulse_r_eof(r))
                return PULSE_DEAD;
        // The person's switch comes before anything else is looked at: a program told no is told it whatever it asked.
        if (p->capture_off)
                return pulse_error(p, c, tag, PULSE_ERR_ACCESS);
        if (direct != PULSE_INVALID)
                return pulse_error(p, c, tag, PULSE_ERR_NOTSUPPORTED); // a program's own meter on another's stream: not served
        if (!audio_format_bytes(format) || channels < 1 || channels > AUDIO_CHANNELS_MAX || rate < 1 || rate > AUDIO_RATE_MAX || map_channels != channels)
                return pulse_error(p, c, tag, PULSE_ERR_INVALID);
        if (!pulse_record_format_ok(format))
                return pulse_error(p, c, tag, PULSE_ERR_NOTSUPPORTED);
        if (source_name && source_name[0] && source_index != PULSE_INVALID)
                return pulse_error(p, c, tag, PULSE_ERR_INVALID);
        if (source_name && source_name[0])
                source = pulse_source_named(p, source_name);
        else if (source_index != PULSE_INVALID)
                source = pulse_find_source(p, source_index, 0);
        else
                source = p->default_source;
        if (source < 0)
                return pulse_error(p, c, tag, PULSE_ERR_NOENTITY);
        // What a sink plays is what everyone in the session plays: another user's program does not hear it.
        if (!p->source[source].capture && !c->root)
                return pulse_error(p, c, tag, PULSE_ERR_ACCESS);
        for (int i = 0; i < PULSE_RECORDS; i++)
        {
                if (p->record[i].used && p->record[i].client == slot)
                        count++;
                if (!p->record[i].used && !rec)
                        rec = p->record + i;
        }
        if (count >= PULSE_RECORDS_PER_CLIENT || !rec)
                return pulse_error(p, c, tag, PULSE_ERR_TOOLARGE);
        audio_init();
        if (!peak && rate != p->source[source].rate)
        {
                table = audio_table_take(p->source[source].rate, rate);
                if (!table)
                        return pulse_error(p, c, tag, PULSE_ERR_NOTSUPPORTED);
        }
        __builtin_memset(rec, 0, __builtin_offsetof(struct pulse_record, rs));
        rec->used = 1;
        rec->table = table;
        if (table)
                audio_rs_open(&rec->rs, table);
        rec->client = slot;
        rec->channel = c->next_channel++;
        rec->index = p->next_output++;
        rec->source = source;
        rec->props = props;
        rec->format = format;
        rec->channels = channels;
        rec->rate = rate;
        rec->map_channels = map_channels;
        for (long i = 0; i < channels; i++)
                rec->map[i] = map[i];
        for (long i = 0; i < PULSE_CHANNEL_MAX; i++)
                rec->volume[i] = AUDIO_VOLUME_NORM;
        if (volume_set && volume_channels == map_channels)
                for (long i = 0; i < volume_channels; i++)
                        rec->volume[i] = volume[i];
        rec->muted = muted_set ? muted : 0;
        rec->corked = corked;
        rec->peak_detect = peak;
        rec->adjust_latency = adjust;
        rec->variable_rate = variable;
        rec->early_requests = early;
        rec->no_move = no_move;
        rec->owner_set = (source_name && source_name[0]) || source_index != PULSE_INVALID;
        rec->uid = c->uid;
        rec->pid = c->pid;
        rec->now[0] = rec->now[1] = 1.0f;
        pulse_record_attributes(rec, maxlength, fragsize);
        pulse_record_apply(rec);
        rec->now[0] = rec->now[1] = rec->target;
        {
                char value[PULSE_VALUE + 1];

                if (!pulse_props_text(&rec->props, "application.name", value, sizeof(value)) && pulse_props_text(&c->props, "application.name", value, sizeof(value)))
                        pulse_props_set(&rec->props, "application.name", (const unsigned char *)value, pulse_len(value) + 1);
        }
        p->records_made++;
        pulse_reply_begin(p, &w, tag);
        pulse_w_u32(&w, rec->channel);
        pulse_w_u32(&w, rec->index);
        pulse_w_u32(&w, rec->maxlength);
        pulse_w_u32(&w, rec->fragsize);
        if (c->version >= 12)
        {
                pulse_w_sample_spec(&w, rec->format, rec->channels, rec->rate);
                pulse_w_channel_map(&w, rec->map, rec->map_channels);
                pulse_w_u32(&w, (unsigned long)source);
                pulse_w_string(&w, p->source[source].name);
                pulse_w_bool(&w, 0);
        }
        if (c->version >= 13)
                pulse_w_64(&w, 'U', p->source[source].delay_frames * 1000000l / (p->source[source].rate ? p->source[source].rate : 1));
        if (c->version >= 22)
                pulse_w_format_info(&w);
        err = pulse_reply_end(c, &w);
        pulse_event(p, PULSE_FACILITY_SOURCE_OUTPUT, PULSE_EVENT_NEW, rec->index);
        pulse_changed(p, PULSE_CHANGED_RECORDS, source);
        return err;
}

// The commands on one record stream by its channel: delete, cork, flush, latency, name, rate, attributes, properties.
static int pulse_command_record(struct pulse *p, long slot, unsigned long command, unsigned long tag, struct pulse_reader *r)
{
        struct pulse_client *c = p->client[slot];
        unsigned long channel = pulse_r_u32(r);
        struct pulse_record *rec;
        struct pulse_writer w;

        PULSE_NEED_AUTH();
        if (command == PULSE_COMMAND_DELETE_RECORD_STREAM || command == PULSE_COMMAND_FLUSH_RECORD_STREAM)
                if (!pulse_r_eof(r))
                        return PULSE_DEAD;
        rec = r->bad ? 0 : pulse_record_of(p, slot, channel);
        switch (command)
        {
        case PULSE_COMMAND_DELETE_RECORD_STREAM:
                if (!rec)
                        return pulse_error(p, c, tag, PULSE_ERR_NOENTITY);
                pulse_record_end(p, rec, 0);
                return pulse_ack(p, c, tag);
        case PULSE_COMMAND_CORK_RECORD_STREAM:
        {
                int cork = pulse_r_bool(r);

                if (!pulse_r_eof(r))
                        return PULSE_DEAD;
                if (!rec)
                        return pulse_error(p, c, tag, PULSE_ERR_NOENTITY);
                if (rec->corked && !cork && rec->table)
                        audio_rs_reset(&rec->rs); // what it heard before is not what comes next
                rec->corked = cork;
                rec->window[0] = rec->window[1] = 0;
                rec->window_count = 0;
                pulse_event(p, PULSE_FACILITY_SOURCE_OUTPUT, PULSE_EVENT_CHANGE, rec->index);
                pulse_changed(p, PULSE_CHANGED_RECORDS, rec->source);
                return pulse_ack(p, c, tag);
        }
        case PULSE_COMMAND_FLUSH_RECORD_STREAM:
                // Nothing waits in the server: what a stream makes is in its program's queue at once. What is flushed is the filter's tail.
                if (!rec)
                        return pulse_error(p, c, tag, PULSE_ERR_NOENTITY);
                if (rec->table)
                        audio_rs_reset(&rec->rs);
                return pulse_ack(p, c, tag);
        case PULSE_COMMAND_GET_RECORD_LATENCY:
        {
                unsigned long sec = 0, usec = 0;
                long now_sec = 0, now_usec = 0, source_latency, latency;
                const struct pulse_source *s;

                pulse_r_timeval(r, &sec, &usec);
                if (!pulse_r_eof(r))
                        return PULSE_DEAD;
                if (!rec)
                        return pulse_error(p, c, tag, PULSE_ERR_NOENTITY);
                s = p->source + rec->source;
                if (p->host.real)
                        p->host.real(p->host.context, &now_sec, &now_usec);
                source_latency = s->delay_frames * 1000000l / (s->rate ? s->rate : 1);
                latency = source_latency + (long)((unsigned long)pulse_out_fill(c) / pulse_record_frame(rec) * 1000000ul / (unsigned long)rec->rate);
                pulse_reply_begin(p, &w, tag);
                pulse_w_64(&w, 'U', latency);
                pulse_w_64(&w, 'U', source_latency);
                pulse_w_bool(&w, !rec->corked);
                pulse_w_byte(&w, 'T');
                pulse_w_be32(&w, sec);
                pulse_w_be32(&w, usec);
                pulse_w_byte(&w, 'T');
                pulse_w_be32(&w, (unsigned long)now_sec);
                pulse_w_be32(&w, (unsigned long)now_usec);
                pulse_w_64(&w, 'r', (long long)rec->produced);
                pulse_w_64(&w, 'r', (long long)rec->sent);
                return pulse_reply_end(c, &w);
        }
        case PULSE_COMMAND_SET_RECORD_STREAM_NAME:
        {
                int isnull;
                const char *name = pulse_r_string(r, &isnull);

                if (!pulse_r_eof(r) || !name)
                        return PULSE_DEAD;
                if (!rec)
                        return pulse_error(p, c, tag, PULSE_ERR_NOENTITY);
                pulse_props_set(&rec->props, "media.name", (const unsigned char *)name, pulse_len(name) + 1);
                pulse_event(p, PULSE_FACILITY_SOURCE_OUTPUT, PULSE_EVENT_CHANGE, rec->index);
                return pulse_ack(p, c, tag);
        }
        case PULSE_COMMAND_UPDATE_RECORD_STREAM_SAMPLE_RATE:
        {
                unsigned long rate = pulse_r_u32(r);
                struct audio_table *table = 0;

                if (!pulse_r_eof(r))
                        return PULSE_DEAD;
                if (!rec)
                        return pulse_error(p, c, tag, PULSE_ERR_NOENTITY);
                if (!rec->variable_rate || rate < 1 || rate > AUDIO_RATE_MAX)
                        return pulse_error(p, c, tag, PULSE_ERR_INVALID);
                if (!rec->peak_detect && (long)rate != p->source[rec->source].rate)
                {
                        table = audio_table_take(p->source[rec->source].rate, (long)rate);
                        if (!table)
                                return pulse_error(p, c, tag, PULSE_ERR_INVALID);
                }
                if (!rec->peak_detect)
                {
                        audio_table_give(rec->table);
                        rec->table = table;
                        if (table)
                                audio_rs_open(&rec->rs, table);
                }
                rec->rate = (long)rate;
                return pulse_ack(p, c, tag);
        }
        case PULSE_COMMAND_SET_RECORD_STREAM_BUFFER_ATTR:
        {
                unsigned long maxlength = pulse_r_u32(r), fragsize = pulse_r_u32(r);
                int adjust = 0, early = 0;

                if (c->version >= 13)
                        adjust = pulse_r_bool(r);
                if (c->version >= 14)
                        early = pulse_r_bool(r);
                if (!pulse_r_eof(r))
                        return PULSE_DEAD;
                if (!rec)
                        return pulse_error(p, c, tag, PULSE_ERR_NOENTITY);
                pulse_record_attributes(rec, maxlength, fragsize);
                rec->adjust_latency = adjust;
                rec->early_requests = early;
                pulse_reply_begin(p, &w, tag);
                pulse_w_u32(&w, rec->maxlength);
                pulse_w_u32(&w, rec->fragsize);
                if (c->version >= 13)
                        pulse_w_64(&w, 'U', p->source[rec->source].delay_frames * 1000000l / (p->source[rec->source].rate ? p->source[rec->source].rate : 1));
                return pulse_reply_end(c, &w);
        }
        case PULSE_COMMAND_UPDATE_RECORD_STREAM_PROPLIST:
        {
                unsigned long mode = pulse_r_u32(r);
                struct pulse_props props = {0};

                pulse_r_proplist(r, &props);
                if (!pulse_r_eof(r))
                        return PULSE_DEAD;
                if (!rec)
                        return pulse_error(p, c, tag, PULSE_ERR_NOENTITY);
                pulse_props_update(&rec->props, &props, mode);
                pulse_event(p, PULSE_FACILITY_SOURCE_OUTPUT, PULSE_EVENT_CHANGE, rec->index);
                return pulse_ack(p, c, tag);
        }
        case PULSE_COMMAND_REMOVE_RECORD_STREAM_PROPLIST:
        {
                for (;;)
                {
                        int isnull;
                        const char *key = pulse_r_string(r, &isnull);

                        if (r->bad)
                                return PULSE_DEAD;
                        if (isnull)
                                break;
                        if (rec)
                                pulse_props_remove(&rec->props, key);
                }
                if (!pulse_r_eof(r))
                        return PULSE_DEAD;
                if (!rec)
                        return pulse_error(p, c, tag, PULSE_ERR_NOENTITY);
                pulse_event(p, PULSE_FACILITY_SOURCE_OUTPUT, PULSE_EVENT_CHANGE, rec->index);
                return pulse_ack(p, c, tag);
        }
        default:
                break;
        }
        return pulse_error(p, c, tag, PULSE_ERR_COMMAND);
}

/* ---- the sample cache ---- */

/*
        Sounds a program keeps in the server to play again by name (libcanberra's event sounds: a click, a bell, a message
        arrived). A program uploads one (CREATE_UPLOAD_STREAM, its data as frames on the channel it was given, then
        FINISH_UPLOAD_STREAM), plays it (PLAY_SAMPLE: a stream of its own that is the sample's frames, played from the
        cache's memory without copying them, that ends by itself when they are done), and takes it out (REMOVE_SAMPLE). A
        sample that is taken out, or replaced by another of its name, goes on for the streams that are playing it and
        is gone from the cache at once. The cache is bounded as everything else is: a sample, all of them, a user's.
*/

static int pulse_sample_name_ok(const char *name)
{
        long n;

        if (!name)
                return 0;
        n = pulse_len(name);
        if (n < 1 || n >= PULSE_NAME)
                return 0;
        for (long i = 0; i < n; i++)
                if ((unsigned char)name[i] < ' ' || (unsigned char)name[i] == 127)
                        return 0;
        return 1;
}

static struct pulse_sample *pulse_sample_named(struct pulse *p, const char *name)
{
        for (int i = 0; i < PULSE_SAMPLES; i++)
                if (p->sample[i].used && !p->sample[i].condemned && pulse_equal(p->sample[i].name, name))
                        return p->sample + i;
        return 0;
}

// A sample taken out of the cache gives its memory back when nothing plays it any more.
static void pulse_sample_release(struct pulse *p, struct pulse_sample *s)
{
        if (!s->used || !s->condemned || s->playing > 0)
                return;
        p->host.release(p->host.context, s->data, (long)s->bytes);
        p->sample_total -= s->bytes;
        s->used = 0;
        s->data = 0;
}

// What a user holds of the cache, in samples and in uploads not finished.
static unsigned long pulse_sample_held(struct pulse *p, long uid)
{
        unsigned long n = 0;

        for (int i = 0; i < PULSE_SAMPLES; i++)
                if (p->sample[i].used && p->sample[i].uid == uid)
                        n += p->sample[i].bytes;
        for (int i = 0; i < PULSE_UPLOADS; i++)
                if (p->upload[i].used && p->upload[i].uid == uid)
                        n += p->upload[i].length;
        return n;
}

static struct pulse_upload *pulse_upload_of(struct pulse *p, long client, unsigned long channel)
{
        for (int i = 0; i < PULSE_UPLOADS; i++)
                if (p->upload[i].used && p->upload[i].client == client && p->upload[i].channel == channel)
                        return p->upload + i;
        return 0;
}

static void pulse_upload_free(struct pulse *p, struct pulse_upload *u)
{
        if (u->data)
                p->host.release(p->host.context, u->data, (long)u->length);
        p->upload_total -= u->length;
        u->used = 0;
        u->data = 0;
}

static void pulse_fill_sample(struct pulse_writer *w, struct pulse_client *c, const struct pulse_sample *s)
{
        unsigned long volume[AUDIO_CHANNELS_MAX];
        unsigned long frames = s->bytes / (unsigned long)(audio_format_bytes(s->format) * s->channels);

        for (long i = 0; i < s->map_channels; i++)
                volume[i] = AUDIO_VOLUME_NORM;
        pulse_w_u32(w, s->index);
        pulse_w_string(w, s->name);
        pulse_w_cvolume(w, volume, s->map_channels);
        pulse_w_64(w, 'U', (long long)frames * 1000000 / (long long)s->rate);
        pulse_w_sample_spec(w, s->format, s->channels, s->rate);
        pulse_w_channel_map(w, s->map, s->map_channels);
        pulse_w_u32(w, s->bytes);
        pulse_w_bool(w, 0); // not lazy: the data is in the cache
        pulse_w_string(w, 0);
        if (c->version >= 13)
                pulse_w_proplist(w, &s->props);
}

static int pulse_command_create_upload(struct pulse *p, long slot, struct pulse_reader *r, unsigned long tag)
{
        struct pulse_client *c = p->client[slot];
        struct pulse_props props = {0};
        long format = 0, channels = 0, rate = 0, map_channels, mine = 0;
        signed char map[PULSE_CHANNEL_MAX];
        unsigned long length;
        const char *name;
        struct pulse_upload *u = 0;
        unsigned char *memory;
        struct pulse_writer w;
        int isnull;

        PULSE_NEED_AUTH();
        name = pulse_r_string(r, &isnull);
        pulse_r_sample_spec(r, &format, &channels, &rate);
        map_channels = pulse_r_channel_map(r, map);
        length = pulse_r_u32(r);
        if (c->version >= 13)
                pulse_r_proplist(r, &props);
        if (!pulse_r_eof(r))
                return PULSE_DEAD;
        if (!pulse_sample_name_ok(name) || !audio_format_bytes(format) || channels < 1 || channels > AUDIO_CHANNELS_MAX || rate < 1 || rate > AUDIO_RATE_MAX || map_channels != channels ||
            length == 0 || length % (unsigned long)(audio_format_bytes(format) * channels))
                return pulse_error(p, c, tag, PULSE_ERR_INVALID);
        for (int i = 0; i < PULSE_UPLOADS; i++)
        {
                if (p->upload[i].used && p->upload[i].client == slot)
                        mine++;
                if (!p->upload[i].used && !u)
                        u = p->upload + i;
        }
        if (length > PULSE_SAMPLE_MAX || mine >= PULSE_UPLOADS_PER_CLIENT || !u || p->sample_total + p->upload_total + length > PULSE_SAMPLE_TOTAL ||
            (!c->root && pulse_sample_held(p, c->uid) + length > PULSE_SAMPLE_USER))
                return pulse_error(p, c, tag, PULSE_ERR_TOOLARGE);
        memory = p->host.alloc(p->host.context, (long)length);
        if (!memory)
                return pulse_error(p, c, tag, PULSE_ERR_TOOLARGE);
        __builtin_memset(u, 0, sizeof(*u));
        u->used = 1;
        u->client = slot;
        u->channel = c->next_channel++;
        pulse_copy(u->name, PULSE_NAME, name);
        u->format = format;
        u->channels = channels;
        u->rate = rate;
        u->map_channels = map_channels;
        for (long i = 0; i < channels; i++)
                u->map[i] = map[i];
        u->props = props;
        u->data = memory;
        u->length = length;
        u->uid = c->uid;
        p->upload_total += length;
        pulse_reply_begin(p, &w, tag);
        pulse_w_u32(&w, u->channel);
        pulse_w_u32(&w, length);
        return pulse_reply_end(c, &w);
}

// DELETE_UPLOAD_STREAM and FINISH_UPLOAD_STREAM: the upload is given up, or its sample is put in the cache (the one of its name replaced).
static int pulse_command_upload(struct pulse *p, long slot, unsigned long command, unsigned long tag, struct pulse_reader *r)
{
        struct pulse_client *c = p->client[slot];
        unsigned long channel = pulse_r_u32(r);
        struct pulse_upload *u;
        struct pulse_sample *old, *s = 0;
        unsigned long index = 0;

        if (!pulse_r_eof(r))
                return PULSE_DEAD;
        PULSE_NEED_AUTH();
        u = pulse_upload_of(p, slot, channel);
        if (!u)
                return pulse_error(p, c, tag, PULSE_ERR_NOENTITY);
        if (command == PULSE_COMMAND_DELETE_UPLOAD_STREAM)
        {
                pulse_upload_free(p, u);
                return pulse_ack(p, c, tag);
        }
        if (u->got != u->length)
        {
                pulse_upload_free(p, u);
                return pulse_error(p, c, tag, PULSE_ERR_INVALID);
        }
        old = pulse_sample_named(p, u->name);
        if (old && !pulse_may(c, old->uid))
        {
                pulse_upload_free(p, u);
                return pulse_error(p, c, tag, PULSE_ERR_ACCESS);
        }
        // A replacement takes the place of the sound it replaces when nothing plays that one, which gives its memory back now,
        // so a full cache takes it. A sound that is still played keeps its place until it is done, and needs a free one.
        if (old)
                index = old->index;
        if (old && old->playing == 0)
        {
                old->condemned = 1;
                pulse_sample_release(p, old);
        }
        for (int i = 0; i < PULSE_SAMPLES && !s; i++)
                if (!p->sample[i].used)
                        s = p->sample + i;
        if (!s)
        {
                pulse_upload_free(p, u);
                return pulse_error(p, c, tag, PULSE_ERR_TOOLARGE);
        }
        __builtin_memset(s, 0, sizeof(*s));
        s->used = 1;
        s->index = old ? index : p->next_sample++;
        pulse_copy(s->name, PULSE_NAME, u->name);
        s->format = u->format;
        s->channels = u->channels;
        s->rate = u->rate;
        s->map_channels = u->map_channels;
        for (long i = 0; i < u->map_channels; i++)
                s->map[i] = u->map[i];
        s->props = u->props;
        s->data = u->data;
        s->bytes = u->length;
        s->uid = c->uid;
        p->sample_total += u->length;
        p->upload_total -= u->length;
        u->used = 0;
        u->data = 0;
        // The old one, if it was not released above and did not give its place to the new one, is condemned: its memory goes when
        // the last stream that plays it has ended.
        if (old && old != s)
        {
                old->condemned = 1;
                pulse_sample_release(p, old);
        }
        pulse_event(p, PULSE_FACILITY_SAMPLE_CACHE, old ? PULSE_EVENT_CHANGE : PULSE_EVENT_NEW, s->index);
        return pulse_ack(p, c, tag);
}

static int pulse_command_remove_sample(struct pulse *p, long slot, struct pulse_reader *r, unsigned long tag)
{
        struct pulse_client *c = p->client[slot];
        int isnull;
        const char *name = pulse_r_string(r, &isnull);
        struct pulse_sample *s;

        if (!pulse_r_eof(r) || !name)
                return PULSE_DEAD;
        PULSE_NEED_AUTH();
        s = pulse_sample_named(p, name);
        if (!s)
                return pulse_error(p, c, tag, PULSE_ERR_NOENTITY);
        if (!pulse_may(c, s->uid))
                return pulse_error(p, c, tag, PULSE_ERR_ACCESS);
        s->condemned = 1;
        pulse_event(p, PULSE_FACILITY_SAMPLE_CACHE, PULSE_EVENT_REMOVE, s->index);
        pulse_sample_release(p, s);
        return pulse_ack(p, c, tag);
}

/*
        PLAY_SAMPLE: a stream of the sample's frames on a sink, for no program (it is not the requester's: it plays on if the
        requester goes), that ends when its frames have been played. The ring of its voice is the sample's own memory, with
        everything in it to be read. The volume is the program's if it gave one, else what is remembered of its name.
*/
static int pulse_command_play_sample(struct pulse *p, long slot, struct pulse_reader *r, unsigned long tag)
{
        struct pulse_client *c = p->client[slot];
        struct pulse_props props = {0}, extra = {0};
        unsigned long sink_index, volume;
        const char *sink_name, *name;
        int isnull;
        struct pulse_sample *sample;
        struct pulse_stream *s = 0;
        long sink, mine = 0;
        struct pulse_writer w;
        char value[PULSE_VALUE + 1];

        PULSE_NEED_AUTH();
        sink_index = pulse_r_u32(r);
        sink_name = pulse_r_string(r, &isnull);
        volume = pulse_r_u32(r);
        name = pulse_r_string(r, &isnull);
        if (c->version >= 13)
                pulse_r_proplist(r, &props);
        if (!pulse_r_eof(r) || !name)
                return PULSE_DEAD;
        if (sink_name && sink_name[0] && sink_index != PULSE_INVALID)
                return pulse_error(p, c, tag, PULSE_ERR_INVALID);
        if (sink_name && sink_name[0])
                sink = pulse_sink_named(p, sink_name);
        else if (sink_index != PULSE_INVALID)
                sink = pulse_find_sink(p, sink_index, 0);
        else
                sink = p->default_sink;
        sample = pulse_sample_named(p, name);
        if (!sample || sink < 0)
                return pulse_error(p, c, tag, PULSE_ERR_NOENTITY);
        for (int i = 0; i < PULSE_STREAMS; i++)
        {
                if (p->stream[i].used && !p->stream[i].dying && p->stream[i].one_shot && p->stream[i].maker == c->index)
                        mine++;
                if (!p->stream[i].used && !s)
                        s = p->stream + i;
        }
        if (mine >= PULSE_STREAMS_PER_CLIENT || !s)
                return pulse_error(p, c, tag, PULSE_ERR_TOOLARGE);
        __builtin_memset(s, 0, __builtin_offsetof(struct pulse_stream, voice));
        if (audio_voice_open(&s->voice, sample->format, sample->channels, sample->rate, sample->map, p->sink[sink].rate, sample->data, sample->bytes) < 0)
                return pulse_error(p, c, tag, PULSE_ERR_NOTSUPPORTED);
        s->voice.ring.write = s->voice.ring.limit = s->voice.ring.size;
        s->used = 1;
        s->client = -1;
        s->index = p->next_input++;
        s->sink = sink;
        s->format = sample->format;
        s->channels = sample->channels;
        s->rate = sample->rate;
        s->map_channels = sample->map_channels;
        for (long i = 0; i < sample->channels; i++)
                s->map[i] = sample->map[i];
        s->one_shot = 1;
        s->maker = c->index;
        s->sample_ref = (sample - p->sample) + 1;
        s->owner_set = (sink_name && sink_name[0]) || sink_index != PULSE_INVALID;
        s->uid = c->uid;
        s->pid = c->pid;
        sample->playing++;
        for (long i = 0; i < PULSE_CHANNEL_MAX; i++)
                s->volume[i] = volume == PULSE_INVALID ? AUDIO_VOLUME_NORM : volume > AUDIO_VOLUME_MAX ? AUDIO_VOLUME_MAX : volume;
        if (volume == PULSE_INVALID && p->host.recall && pulse_props_text(&c->props, "application.name", value, sizeof(value)) && value[0])
        {
                unsigned long remembered;
                int mute_remembered;

                if (p->host.recall(p->host.context, value, &remembered, &mute_remembered))
                {
                        for (long i = 0; i < PULSE_CHANNEL_MAX; i++)
                                s->volume[i] = remembered;
                        s->muted = mute_remembered;
                }
        }
        // What the program said of it, then what the sample was uploaded with; its program's name, and the sample's for the stream's.
        s->props = props;
        pulse_props_update(&s->props, &sample->props, 1);
        if (!pulse_props_text(&s->props, "application.name", value, sizeof(value)) && pulse_props_text(&c->props, "application.name", value, sizeof(value)))
                pulse_props_set(&s->props, "application.name", (const unsigned char *)value, pulse_len(value) + 1);
        if (!pulse_props_text(&s->props, "media.name", value, sizeof(value)))
        {
                pulse_props_set(&extra, "media.name", (const unsigned char *)sample->name, pulse_len(sample->name) + 1);
                pulse_props_update(&s->props, &extra, 1);
        }
        pulse_stream_apply(s);
        p->streams_made++;
        pulse_reply_begin(p, &w, tag);
        if (c->version >= 13)
                pulse_w_u32(&w, s->index);
        {
                int err = pulse_reply_end(c, &w);

                pulse_event(p, PULSE_FACILITY_SINK_INPUT, PULSE_EVENT_NEW, s->index);
                pulse_changed(p, PULSE_CHANGED_STREAMS, sink);
                return err;
        }
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

// A list is built in the scratch. One that did not fit is refused as too large, rather than dropping the client that asked.
static int pulse_list_end(struct pulse *p, struct pulse_client *c, unsigned long tag, struct pulse_writer *w)
{
        if (w->bad)
                return pulse_error(p, c, tag, PULSE_ERR_TOOLARGE);
        return pulse_reply_end(c, w);
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
                for (long i = 0; i < PULSE_SINKS; i++)
                {
                        if (!p->sink[i].used)
                                continue;
                        if (!list && !((name && name[0] && pulse_equal(p->sink[i].name, name)) || (!(name && name[0]) && index == (unsigned long)i)))
                                continue;
                        pulse_fill_sink(p, &w, c, i);
                        if (!list)
                                return pulse_list_end(p, c, tag, &w);
                }
                if (!list)
                        return pulse_error(p, c, tag, PULSE_ERR_NOENTITY);
                return pulse_list_end(p, c, tag, &w);
        case PULSE_COMMAND_GET_SOURCE_INFO:
        case PULSE_COMMAND_GET_SOURCE_INFO_LIST:
                for (long i = 0; i < PULSE_SOURCES; i++)
                {
                        if (!p->source[i].used)
                                continue;
                        if (!list && !((name && name[0] && pulse_equal(p->source[i].name, name)) || (!(name && name[0]) && index == (unsigned long)i)))
                                continue;
                        pulse_fill_source(p, &w, c, i);
                        if (!list)
                                return pulse_list_end(p, c, tag, &w);
                }
                if (!list)
                        return pulse_error(p, c, tag, PULSE_ERR_NOENTITY);
                return pulse_list_end(p, c, tag, &w);
        case PULSE_COMMAND_GET_SOURCE_OUTPUT_INFO:
        case PULSE_COMMAND_GET_SOURCE_OUTPUT_INFO_LIST:
                for (int i = 0; i < PULSE_RECORDS; i++)
                        if (p->record[i].used && (list || p->record[i].index == index) && pulse_may(c, p->record[i].uid))
                        {
                                pulse_fill_output(p, &w, c, p->record + i);
                                if (!list)
                                        return pulse_list_end(p, c, tag, &w);
                        }
                if (!list)
                        return pulse_error(p, c, tag, PULSE_ERR_NOENTITY);
                return pulse_list_end(p, c, tag, &w);
        case PULSE_COMMAND_GET_CLIENT_INFO:
        case PULSE_COMMAND_GET_CLIENT_INFO_LIST:
                for (long i = 0; i < PULSE_CLIENTS; i++)
                        if (p->client[i] && p->client[i]->authorized && (list || p->client[i]->index == index) && (c->root || p->client[i]->uid == c->uid))
                        {
                                pulse_fill_client(p, &w, c, p->client[i]);
                                if (!list)
                                        return pulse_list_end(p, c, tag, &w);
                        }
                if (!list)
                        return pulse_error(p, c, tag, PULSE_ERR_NOENTITY);
                return pulse_list_end(p, c, tag, &w);
        case PULSE_COMMAND_GET_SINK_INPUT_INFO:
        case PULSE_COMMAND_GET_SINK_INPUT_INFO_LIST:
                for (int i = 0; i < PULSE_STREAMS; i++)
                        if (p->stream[i].used && !p->stream[i].dying && (list || p->stream[i].index == index) && pulse_may(c, p->stream[i].uid))
                        {
                                pulse_fill_input(p, &w, c, p->stream + i);
                                if (!list)
                                        return pulse_list_end(p, c, tag, &w);
                        }
                if (!list)
                        return pulse_error(p, c, tag, PULSE_ERR_NOENTITY);
                return pulse_list_end(p, c, tag, &w);
        case PULSE_COMMAND_GET_SAMPLE_INFO:
        case PULSE_COMMAND_GET_SAMPLE_INFO_LIST:
                for (int i = 0; i < PULSE_SAMPLES; i++)
                        if (p->sample[i].used && !p->sample[i].condemned && (list || (name && name[0] ? pulse_equal(p->sample[i].name, name) : p->sample[i].index == index)))
                        {
                                pulse_fill_sample(&w, c, p->sample + i);
                                if (!list)
                                        return pulse_list_end(p, c, tag, &w);
                        }
                if (!list)
                        return pulse_error(p, c, tag, PULSE_ERR_NOENTITY);
                return pulse_list_end(p, c, tag, &w);
        case PULSE_COMMAND_GET_MODULE_INFO_LIST:
        case PULSE_COMMAND_GET_CARD_INFO_LIST:
                return pulse_list_end(p, c, tag, &w);
        default:
                return pulse_error(p, c, tag, PULSE_ERR_NOENTITY);
        }
}

static int pulse_command_server_info(struct pulse *p, struct pulse_client *c, unsigned long tag, struct pulse_reader *r)
{
        struct pulse_writer w;
        static const signed char stereo[2] = {AUDIO_POSITION_FRONT_LEFT, AUDIO_POSITION_FRONT_RIGHT};

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
        pulse_w_string(&w, p->default_source >= 0 ? p->source[p->default_source].name : 0);
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
                for (int i = 0; i < 4; i++)
                        pulse_w_u32(&w, 0);
                pulse_w_u32(&w, p->sample_total);
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
                if (command == PULSE_COMMAND_LOOKUP_SINK)
                {
                        for (long i = 0; i < PULSE_SINKS && found < 0; i++)
                                if (p->sink[i].used && pulse_equal(p->sink[i].name, name))
                                        found = i;
                }
                else
                {
                        for (long i = 0; i < PULSE_SOURCES && found < 0; i++)
                                if (p->source[i].used && pulse_equal(p->source[i].name, name))
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
                        sink = pulse_sink_named(p, name);
                if (sink < 0)
                        return pulse_error(p, c, tag, PULSE_ERR_NOENTITY);
                if (sink != p->default_sink)
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
                        // The default source is the monitor of the default sink while it is a monitor.
                        if (p->default_source >= 0 && p->default_source < PULSE_SINKS)
                                pulse_source_default(p, sink);
                        pulse_event(p, PULSE_FACILITY_SERVER, PULSE_EVENT_CHANGE, 0);
                        pulse_changed(p, PULSE_CHANGED_DEFAULT, sink);
                }
                return pulse_ack(p, c, tag);
        }
        case PULSE_COMMAND_SET_DEFAULT_SOURCE:
        {
                int isnull;
                const char *name = pulse_r_string(&r, &isnull);
                long source = -1;

                if (!pulse_r_eof(&r))
                        return PULSE_DEAD;
                PULSE_NEED_AUTH();
                if (!c->root)
                        return pulse_error(p, c, tag, PULSE_ERR_ACCESS);
                if (name)
                        source = pulse_source_named(p, name);
                if (source < 0)
                        return pulse_error(p, c, tag, PULSE_ERR_NOENTITY);
                pulse_source_default(p, source);
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
                        // A stream that asked not to be moved is not moved by its program: the person (root) still may.
                        if (s->no_move && !c->root)
                                return pulse_error(p, c, tag, PULSE_ERR_ACCESS);
                        if (audio_voice_retarget(&s->voice, p->sink[sink].rate) < 0)
                                return pulse_error(p, c, tag, PULSE_ERR_NOTSUPPORTED);
                        s->sink = sink;
                        s->owner_set = 1;
                        pulse_event(p, PULSE_FACILITY_SINK_INPUT, PULSE_EVENT_CHANGE, s->index);
                }
                return pulse_ack(p, c, tag);
        }
        case PULSE_COMMAND_MOVE_SOURCE_OUTPUT:
        {
                unsigned long index = pulse_r_u32(&r), device = pulse_r_u32(&r);
                int isnull;
                const char *name = pulse_r_string(&r, &isnull);
                struct pulse_record *rec;
                long source;

                if (!pulse_r_eof(&r))
                        return PULSE_DEAD;
                PULSE_NEED_AUTH();
                rec = pulse_output_of(p, index);
                source = pulse_find_source(p, device, name);
                if (!rec || source < 0)
                        return pulse_error(p, c, tag, PULSE_ERR_NOENTITY);
                if (!pulse_may(c, rec->uid) || (!p->source[source].capture && !c->root))
                        return pulse_error(p, c, tag, PULSE_ERR_ACCESS);
                if (pulse_record_move(p, rec, source) < 0)
                        return pulse_error(p, c, tag, PULSE_ERR_NOTSUPPORTED);
                rec->owner_set = 1;
                return pulse_ack(p, c, tag);
        }
        case PULSE_COMMAND_CREATE_RECORD_STREAM:
                return pulse_command_create_record(p, slot, &r, tag);
        case PULSE_COMMAND_DELETE_RECORD_STREAM:
        case PULSE_COMMAND_CORK_RECORD_STREAM:
        case PULSE_COMMAND_FLUSH_RECORD_STREAM:
        case PULSE_COMMAND_GET_RECORD_LATENCY:
        case PULSE_COMMAND_SET_RECORD_STREAM_NAME:
        case PULSE_COMMAND_UPDATE_RECORD_STREAM_SAMPLE_RATE:
        case PULSE_COMMAND_SET_RECORD_STREAM_BUFFER_ATTR:
        case PULSE_COMMAND_UPDATE_RECORD_STREAM_PROPLIST:
        case PULSE_COMMAND_REMOVE_RECORD_STREAM_PROPLIST:
                return pulse_command_record(p, slot, command, tag, &r);
        case PULSE_COMMAND_KILL_CLIENT:
        case PULSE_COMMAND_KILL_SINK_INPUT:
        case PULSE_COMMAND_KILL_SOURCE_OUTPUT:
        {
                unsigned long index = pulse_r_u32(&r);

                if (!pulse_r_eof(&r))
                        return PULSE_DEAD;
                PULSE_NEED_AUTH();
                if (!c->root)
                        return pulse_error(p, c, tag, PULSE_ERR_ACCESS);
                if (command == PULSE_COMMAND_KILL_SOURCE_OUTPUT)
                {
                        struct pulse_record *rec = pulse_output_of(p, index);

                        if (!rec)
                                return pulse_error(p, c, tag, PULSE_ERR_NOENTITY);
                        pulse_record_end(p, rec, 1);
                }
                else if (command == PULSE_COMMAND_KILL_SINK_INPUT)
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
                PULSE_NEED_AUTH();
                return pulse_error(p, c, tag, PULSE_ERR_NOENTITY);
        case PULSE_COMMAND_CREATE_UPLOAD_STREAM:
                return pulse_command_create_upload(p, slot, &r, tag);
        case PULSE_COMMAND_DELETE_UPLOAD_STREAM:
        case PULSE_COMMAND_FINISH_UPLOAD_STREAM:
                return pulse_command_upload(p, slot, command, tag, &r);
        case PULSE_COMMAND_PLAY_SAMPLE:
                return pulse_command_play_sample(p, slot, &r, tag);
        case PULSE_COMMAND_REMOVE_SAMPLE:
                return pulse_command_remove_sample(p, slot, &r, tag);
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
        if (!s)
        {
                struct pulse_upload *u = pulse_upload_of(p, slot, channel);

                // An upload's data is appended, in order, and never more than was said; data for a stream that is gone is dropped,
                // as a program that deleted it just before may have sent some.
                if (u)
                {
                        if ((flags & 0xff) != 0 || offset != 0 || (unsigned long)length > u->length - u->got)
                                return PULSE_DEAD;
                        __builtin_memcpy(u->data + u->got, data, (unsigned long)length);
                        u->got += (unsigned long)length;
                }
                return PULSE_DONE;
        }
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
                // The frames that are whole are read from where the last one ended; what is left of the buffer is moved to its
                // start once, after them, not after each (a feed of small frames would move its rest once per frame).
                long at = 0;

                for (;;)
                {
                        const unsigned char *frame = c->in + at;
                        unsigned long length, channel, flags;
                        long long offset;
                        int result;

                        if (c->in_have - at < 20)
                                break;
                        length = pulse_be32(frame);
                        channel = pulse_be32(frame + 4);
                        offset = (long long)((unsigned long long)pulse_be32(frame + 8) << 32 | pulse_be32(frame + 12));
                        flags = pulse_be32(frame + 16);
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
                        if (c->in_have - at < 20 + (long)length)
                                break;
                        if (channel == PULSE_INVALID)
                                result = length ? pulse_command(p, slot, frame + 20, (long)length) : PULSE_DEAD;
                        else
                                result = pulse_data(p, slot, channel, offset, flags, frame + 20, (long)length);
                        if (result == PULSE_DEAD)
                        {
                                p->protocol_errors++;
                                return PULSE_DEAD;
                        }
                        at += 20 + (long)length;
                }
                if (at)
                {
                        __builtin_memmove(c->in, c->in + at, (unsigned long)(c->in_have - at));
                        c->in_have -= at;
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
        p->default_source = -1;
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
        if (played < frames && s->one_shot)
        {
                // A sample's frames are done (the last of them were faded to nothing): it ends, and its end is announced.
                pulse_stream_end(p, s);
                return;
        }
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
        pulse_source_feed(p, slot, sink->mix.mix[0], sink->mix.mix[1], frames);
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
