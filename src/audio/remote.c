/*
        The sound server's machines over Waterlink: the outputs and the sources of the machines this one is linked to, as
        devices of the server's graph (graph.c), and what those machines take of this one's sound. The cast engine's sound
        channel (src/cast/sound.h, src/waterlink/cast.c): the server and the link share rings of 2.5 ms blocks, and the link
        carries them between the machines.

        THE ROWS

        Each linked machine M is three rows, which the link tells the server of ('R'): its speakers (an output of the
        graph: what is wired into it is played there), its microphone and what it plays (sources). A row the far machine has
        not granted is DENIED, with the line that grants it, to be run on M; a machine with no session up now is AWAY. And
        each stream another machine takes of this one, or plays into it, is a row while it runs ('H'), HEARD, which is what
        shows the person that M listens or plays here: M playing on this machine's speakers is a source of the graph wired to
        the main mix by default; M hearing this machine's microphone, or what it plays, is an output wired from them.

                remote.M.speakers       output   M's speakers
                remote.M.mic            source   M's microphone
                remote.M.sound          source   what M plays (MONITOR)
                remote.M.plays          source   M playing here (HEARD)
                remote.M.hears-mic      output   M hearing this microphone (HEARD)
                remote.M.hears-sound    output   M hearing what plays here (HEARD, MONITOR)

        THE SOUND

        An output's sound is pushed by the graph a mix block at a time (remote_push), cut into the engine's blocks of 120
        frames at 48 kHz, 16-bit stereo, stamped on this machine's CLOCK_MONOTONIC on a grid that follows the mix (one
        block's length after the one before, pulled a thirty-second of the way to the time it was mixed), and written to the
        row's out-ring, whose eventfd wakes the link. A source's sound is pulled by the graph (remote_pull), exactly the
        frames it asks for at the mix rate, from the in-ring the link writes as blocks arrive:

          - the jitter buffer: the read position is kept `target` frames behind the newest block that came, as it is when a
            pull begins; the target is what the largest pull of the last two seconds takes, a block more (blocks come whole,
            so the fill is a saw of a block's height), and the 95th percentile of how late blocks arrive (arrived less
            stamp, over the same two seconds, less the least), in whole blocks, starting at four blocks (10 ms), never
            under two nor over 24 (60 ms);
          - the drift: two machines' clocks differ by tens of parts a million, so the fill is followed as a mirror's is
            (service.c): an adapting resampler's step is nudged by the smoothed distance of the fill from its target, at
            most 1000 ppm (4000 when it is a block or more away), and a fill far out (60 ms past the target, or the writer
            overtaken by twice it) is put back at the target at once;
          - the loss: a block that is not in the ring when it is read was lost (the link already took the copy every
            datagram carries of the block before it) and is concealed: the last block heard again at half the level of
            the time before, so a run of losses fades out and ends in silence, and the next block that comes plays as it is.

        The graph calls pull only while the row is wired out and push only while it is wired in: AUDIO_REMOTE_IDLE_NS of
        neither is the row not wanted, and the link is told ('W'), which ends the stream at the far machine.

        WHAT IS TRUSTED

        Nothing the link says, though it is root's (the socket is root's alone, mode 0600, and its peer must be uid 0): a
        message is one fixed-size record (a SOCK_SEQPACKET packet), every field held to its range before it is used, every
        text ended in its array and printable; a descriptor that comes with one is taken only where that message carries
        one, and only when it is a memfd at least a ring long whose size is sealed (it cannot be cut short under the
        mapping); a ring is read through its header once checked and its slots' own bounds. Every descriptor a message
        brings and the message does not keep is closed.
*/
#ifndef AUDIO_REMOTE_INCLUDED
#define AUDIO_REMOTE_INCLUDED

#include "../cast/sound.h"

// graph.c's words for a remote row, until graph.c is in the build (it defines them first when it is).
#ifndef AUDIO_NODE_REMOTE_SOURCE
#define AUDIO_NODE_REMOTE_SOURCE 1u // the graph pulls it
#define AUDIO_NODE_REMOTE_OUTPUT 2u // the graph pushes into it
#define AUDIO_NODE_DENIED 1u        // the far machine answered no: allow is the line that grants it, run there
#define AUDIO_NODE_AWAY 2u          // linked, no session up now
#define AUDIO_NODE_LIVE 4u          // sound flows now
#define AUDIO_NODE_HEARD 8u         // the far machine's stream of this one: the indicator
#define AUDIO_NODE_MONITOR 16u      // what a machine plays, not a microphone
struct audio_remote_row
{
        long dev;
        char name[64], description[96], machine[32], allow[64];
        unsigned way, flags, latency_us, lost;
};
#endif

#ifndef AUDIO_REMOTE_NOW
#define AUDIO_REMOTE_NOW() clock_monotonic_nanoseconds() // the checks run the jitter buffer on a clock of their own
#endif
#define AUDIO_REMOTE_SOCKET "/run/moonwater/audio.link" // the link's way in: root's, and no bowl's (a bowl is given the native socket alone)
#define AUDIO_REMOTE_SPEAKERS 1u
#define AUDIO_REMOTE_MIC 2u
#define AUDIO_REMOTE_SOUND 3u
#define AUDIO_REMOTE_IDLE_NS 100000000ull   // of neither pull nor push: the row is not wanted
#define AUDIO_REMOTE_TARGET_FIRST 4         // blocks the jitter buffer holds before it has measured
#define AUDIO_REMOTE_TARGET_LEAST 2
#define AUDIO_REMOTE_TARGET_MOST 24         // 60 ms
#define AUDIO_REMOTE_JITTER_NS 1000000000ull // a half of the jitter's window (the window is two of them)
#define AUDIO_REMOTE_BUCKET_NS 250000ull    // the jitter histogram's step
#define AUDIO_REMOTE_BUCKETS 256            // 64 ms, the last bucket for anything later
#define AUDIO_REMOTE_SETTLE 4               // seconds the fill is brought back to its target over
#define AUDIO_REMOTE_PPM 1000
#define AUDIO_REMOTE_PPM_FAR 4000
#define AUDIO_REMOTE_FADES 12               // concealed blocks after which a source is silence (2^-12)
#define AUDIO_REMOTE_RAISE_NS 50000000ull   // an underrun raises the target by a block at most this often

// One record each way on the link's socket (a SOCK_SEQPACKET packet); its descriptors in the same packet.
struct audio_remote_message
{
        unsigned char op;     // the link's: 'R' a row of a machine, 'H' a stream of ours a machine takes ('I' with its in-ring), 'I' an in-ring, 'X' gone
                              // the server's: 'W' wanted (on) or not (off); an output's 'W' on brings its out-ring and eventfd
        unsigned char kind;   // AUDIO_REMOTE_SPEAKERS, _MIC, _SOUND
        unsigned char heard;  // 0 the machine's own device; 1 the machine's stream of this one
        unsigned char on;
        unsigned int flags;   // 'R': AUDIO_NODE_DENIED, AUDIO_NODE_AWAY
        char machine[32];
        char allow[64];
};

_Static_assert(sizeof(struct audio_remote_message) == 104, "a message is 104 bytes on both sides");

struct audio_remote_stream
{
        int dead, heard;                     // dead: gone, its place kept until the graph's next look (audio_remote_rows)
        unsigned int kind;
        const struct cast_sound_ring *in;   // a source's ring, the link's, mapped read-only
        struct cast_sound_ring *out;         // an output's ring, ours
        int out_fd, wake_fd;
        int told;                            // the link was told it is wanted
        unsigned long long pulled_at, pushed_at;
        // The output: blocks cut from what is pushed, stamped on the grid.
        unsigned long long out_block, out_stamp, grid;
        long out_have, push_rate;
        struct audio_table *push_table;
        struct audio_resampler push_rs;
        short out_pcm[CAST_SOUND_SAMPLES];
        float push_out[2][AUDIO_BLOCK];
        // The source: the read position in the in-ring's frames, its resampler, the drift and the jitter.
        long long at;                        // block * CAST_SOUND_FRAMES + frame
        int started;
        long rate;
        struct audio_table *table;
        struct audio_resampler rs;
        unsigned long step;
        float error;
        long target;                         // blocks
        unsigned long long seen;             // blocks of the ring looked at for the jitter
        unsigned long long jitter_at;
        unsigned int histogram[2][AUDIO_REMOTE_BUCKETS];
        unsigned int counted[2];
        long largest[2];                     // the largest pull of each half of the window, in the ring's frames
        unsigned long long current_block;   // the block in cur (~0: none)
        struct cast_sound_slot cur;
        short last[CAST_SOUND_SAMPLES];      // the last block heard, for concealment
        int fades;
        unsigned long long lost, resyncs, underruns;
        unsigned long long now, raised;      // the pull's time, and when the target was last raised by an underrun
        unsigned long long said_at;          // the log's last line of it
        int said_live;
};

static struct
{
        int listen, link;                  // the socket, and the link's connection (-1 none)
        unsigned long long listen_at;      // the last try to listen
        long count, room;                  // rows used (a prefix: a gone row is moved over by the last), and room for
        struct audio_remote_row *rows;
        struct audio_remote_stream *streams;
} audio_remote = {-1, -1, 0, 0, 0, 0, 0};

static const char *const audio_remote_words[2][4] = {{"", "speakers", "mic", "sound"}, {"", "plays", "hears-mic", "hears-sound"}};
static const char *const audio_remote_said[2][4] = {{"", "Speakers", "Microphone", "What it plays"},
                                                   {"", "Playing here", "Hears your microphone", "Hears what plays"}};

static unsigned int audio_remote_way(unsigned int kind, int heard)
{
        return (kind == AUDIO_REMOTE_SPEAKERS) == !heard ? AUDIO_NODE_REMOTE_OUTPUT : AUDIO_NODE_REMOTE_SOURCE;
}

// A text of the link's, made safe: ended inside its array, and a byte that is no printable ASCII is refused (0).
static int audio_remote_text(char *text, unsigned long size, int empty_ok)
{
        unsigned long n = 0;

        while (n < size && text[n])
        {
                if ((unsigned char)text[n] < 0x20 || (unsigned char)text[n] > 0x7e)
                        return 0;
                n++;
        }
        if (n == size)
                return 0;
        return empty_ok || n > 0;
}

static long audio_remote_find(unsigned int kind, int heard, const char *machine)
{
        for (long i = 0; i < audio_remote.count; i++)
                if (!audio_remote.streams[i].dead && audio_remote.streams[i].kind == kind && audio_remote.streams[i].heard == heard &&
                    audio_text_equal(audio_remote.rows[i].machine, machine))
                        return i;
        return -1;
}

static void audio_remote_unmap(struct audio_remote_stream *st)
{
        if (st->in)
                audio_call(syscall(munmap), (positive)st->in, CAST_SOUND_RING_BYTES, 0, 0, 0, 0);
        if (st->out)
                audio_call(syscall(munmap), (positive)st->out, CAST_SOUND_RING_BYTES, 0, 0, 0, 0);
        if (st->out_fd >= 0)
                audio_call(syscall(close), (positive)st->out_fd, 0, 0, 0, 0, 0);
        if (st->wake_fd >= 0)
                audio_call(syscall(close), (positive)st->wake_fd, 0, 0, 0, 0, 0);
        audio_table_give(st->table);
        audio_table_give(st->push_table);
        st->in = 0, st->out = 0, st->out_fd = st->wake_fd = -1, st->table = st->push_table = 0;
        st->started = 0, st->told = 0, st->out_have = 0;
}

// The rows and their streams grown by doubling: 0 when there is no memory (the row is refused, nothing else changes).
static int audio_remote_fit(long want)
{
        long room = audio_remote.room ? audio_remote.room : 8;
        struct audio_remote_row *rows;
        struct audio_remote_stream *streams;

        if (want <= audio_remote.room)
                return 1;
        while (room < want)
                room *= 2;
        rows = audio_host_alloc(0, room * (long)sizeof(*rows));
        streams = audio_host_alloc(0, room * (long)sizeof(*streams));
        if (!rows || !streams)
        {
                if (rows)
                        audio_host_release(0, rows, room * (long)sizeof(*rows));
                if (streams)
                        audio_host_release(0, streams, room * (long)sizeof(*streams));
                return 0;
        }
        if (audio_remote.rows)
        {
                memory_copy_apart(rows, audio_remote.rows, (unsigned long)audio_remote.count * sizeof(*rows));
                memory_copy_apart(streams, audio_remote.streams, (unsigned long)audio_remote.count * sizeof(*streams));
                audio_host_release(0, audio_remote.rows, audio_remote.room * (long)sizeof(*rows));
                audio_host_release(0, audio_remote.streams, audio_remote.room * (long)sizeof(*streams));
        }
        audio_remote.rows = rows, audio_remote.streams = streams, audio_remote.room = room;
        return 1;
}

// A row of a machine, made or found: its index, or -1.
static long audio_remote_row_for(unsigned int kind, int heard, const char *machine)
{
        long i = audio_remote_find(kind, heard, machine);
        struct audio_remote_row *row;
        struct audio_remote_stream *st;
        long n = 0;

        if (i >= 0)
                return i;
        if (!audio_remote_fit(audio_remote.count + 1))
                return -1;
        i = audio_remote.count++;
        row = audio_remote.rows + i, st = audio_remote.streams + i;
        memory_zero(row, sizeof(*row));
        memory_zero(st, sizeof(*st));
        st->heard = heard, st->kind = kind, st->out_fd = st->wake_fd = -1, st->current_block = ~0ull;
        st->target = AUDIO_REMOTE_TARGET_FIRST;
        pulse_copy(row->machine, sizeof(row->machine), machine);
        // remote.M.word, cut where it does not fit (a machine's name is at most 31 bytes: it always fits).
        for (const char *part = "remote."; *part && n < 63; part++)
                row->name[n++] = *part;
        for (const char *part = machine; *part && n < 63; part++)
                row->name[n++] = *part;
        if (n < 63)
                row->name[n++] = '.';
        for (const char *part = audio_remote_words[heard][kind]; *part && n < 63; part++)
                row->name[n++] = *part;
        row->name[n] = 0;
        pulse_copy(row->description, sizeof(row->description), audio_remote_said[heard][kind]);
        row->way = audio_remote_way(kind, heard);
        row->flags = (heard ? AUDIO_NODE_HEARD : 0) | (kind == AUDIO_REMOTE_SOUND ? AUDIO_NODE_MONITOR : 0);
        return i;
}

// A row let go: its rings unmapped and its place dead, so a dev the graph holds names no other row until its next look.
static void audio_remote_drop(long i)
{
        if (i < 0 || i >= audio_remote.count)
                return;
        audio_remote_unmap(audio_remote.streams + i);
        audio_remote.streams[i].dead = 1;
}

// The graph's look at the rows: the dead ones are gone from here on (the last moved into each place), and a row's dev is its
// place + 1, as the graph takes it until its next look.
static long audio_remote_rows(const struct audio_remote_row **rows)
{
        long kept = 0;

        for (long i = 0; i < audio_remote.count; i++)
                if (!audio_remote.streams[i].dead)
                {
                        if (kept != i)
                        {
                                audio_remote.rows[kept] = audio_remote.rows[i];
                                audio_remote.streams[kept] = audio_remote.streams[i];
                                audio_remote.streams[kept].current_block = ~0ull; // read again from the ring: cur was copied
                        }
                        audio_remote.rows[kept].dev = kept + 1;
                        kept++;
                }
        audio_remote.count = kept;
        *rows = audio_remote.rows;
        return audio_remote.count;
}

/* ---- the link's socket ---- */

struct audio_remote_iovec
{
        void *base;
        unsigned long length;
};

struct audio_remote_msghdr
{
        void *name;
        unsigned int name_length, hole;
        struct audio_remote_iovec *iov;
        unsigned long iov_length;
        void *control;
        unsigned long control_length;
        int flags, hole2;
};

struct audio_remote_cmsghdr
{
        unsigned long length;
        int level, type;
};

static void audio_remote_listen(unsigned long long now)
{
        struct
        {
                unsigned short family;
                char path[108];
        } where = {1, AUDIO_REMOTE_SOCKET};
        long fd;

        if (audio_remote.listen >= 0 || (audio_remote.listen_at && now - audio_remote.listen_at < 2000000000ull))
                return;
        audio_remote.listen_at = now ? now : 1;
        fd = audio_call(syscall(socket), 1, 5 | 0x80000 | 0x800, 0, 0, 0, 0); // AF_UNIX, SOCK_SEQPACKET | CLOEXEC | NONBLOCK
        if (fd < 0)
                return;
        audio_call(syscall(unlinkat), (positive)AT_FDCWD, (positive)AUDIO_REMOTE_SOCKET, 0, 0, 0, 0);
        if (audio_call(syscall(bind), (positive)fd, (positive)&where, sizeof(where), 0, 0, 0) < 0 ||
            audio_call(syscall(fchmodat), (positive)AT_FDCWD, (positive)AUDIO_REMOTE_SOCKET, 0600, 0, 0, 0) < 0 ||
            audio_call(syscall(listen), (positive)fd, 2, 0, 0, 0, 0) < 0)
        {
                audio_call(syscall(close), (positive)fd, 0, 0, 0, 0, 0);
                return;
        }
        audio_remote.listen = (int)fd;
}

// The link went: every row it made goes with it (it says them again when it comes back), and the rings with them.
static void audio_remote_lost(void)
{
        if (audio_remote.link >= 0)
                audio_call(syscall(close), (positive)audio_remote.link, 0, 0, 0, 0, 0);
        audio_remote.link = -1;
        for (long i = 0; i < audio_remote.count; i++)
                audio_remote_drop(i);
}

// One message to the link, with descriptors (count of them): 1 when it went.
static int audio_remote_send(const struct audio_remote_message *m, const int *fds, int count)
{
        unsigned char control[sizeof(struct audio_remote_cmsghdr) + 2 * sizeof(int) + 8] __attribute__((aligned(8)));
        struct audio_remote_iovec iov = {(void *)m, sizeof(*m)};
        struct audio_remote_msghdr msg = {0, 0, 0, &iov, 1, 0, 0, 0, 0};

        if (audio_remote.link < 0)
                return 0;
        if (count > 0)
        {
                struct audio_remote_cmsghdr *c = (struct audio_remote_cmsghdr *)control;

                memory_zero(control, sizeof(control));
                c->length = sizeof(*c) + (unsigned long)count * sizeof(int), c->level = 1, c->type = 1; // SOL_SOCKET, SCM_RIGHTS
                memory_copy(control + sizeof(*c), fds, (unsigned long)count * sizeof(int));
                msg.control = control, msg.control_length = (c->length + 7ul) & ~7ul;
        }
        return audio_call(syscall(sendmsg), (positive)audio_remote.link, (positive)&msg, 0x40 | 0x4000, 0, 0, 0) == (long)sizeof(*m);
}

// A descriptor the link gave as a ring: mapped read-only when it is a sealed memfd at least a ring long, else nothing (and closed).
static const struct cast_sound_ring *audio_remote_ring_take(int fd)
{
        long seals = audio_call(syscall(fcntl), (positive)fd, 1034, 0, 0, 0, 0), size, got; // F_GET_SEALS: a memfd's, EINVAL for anything else
        const struct cast_sound_ring *ring = 0;

        size = audio_call(syscall(lseek), (positive)fd, 0, 2, 0, 0, 0); // SEEK_END
        if (seals >= 0 && (seals & 2) && (seals & 4) && size >= (long)CAST_SOUND_RING_BYTES) // F_SEAL_SHRINK and _GROW: it stays this long
        {
                got = audio_call(syscall(mmap), 0, CAST_SOUND_RING_BYTES, 1, 1, (positive)fd, 0); // PROT_READ, MAP_SHARED
                if (!(got < 0 && got > -4096))
                        ring = (const struct cast_sound_ring *)got;
        }
        audio_call(syscall(close), (positive)fd, 0, 0, 0, 0, 0);
        if (ring && !cast_sound_ring_good(ring, CAST_SOUND_RING_BYTES))
        {
                audio_call(syscall(munmap), (positive)ring, CAST_SOUND_RING_BYTES, 0, 0, 0, 0);
                ring = 0;
        }
        return ring;
}

/*
        One message of the link's, whole and judged (the descriptors that came with it are closed unless kept): 1, or 0 when it
        is malformed, which ends the connection (the link says everything again when it comes back).
*/
static int audio_remote_take(struct audio_remote_message *m, int *fds, int count)
{
        long i;
        int ok = 0;

        if (m->kind < AUDIO_REMOTE_SPEAKERS || m->kind > AUDIO_REMOTE_SOUND || m->heard > 1 || m->on > 1 ||
            !audio_remote_text(m->machine, sizeof(m->machine), 0) || !audio_remote_text(m->allow, sizeof(m->allow), 1) ||
            (m->flags & ~(AUDIO_NODE_DENIED | AUDIO_NODE_AWAY)))
                goto done;
        switch (m->op)
        {
        case 'R': // a machine's own device, its state: no descriptor, and never a heard row
                if (count || m->heard)
                        goto done;
                if ((i = audio_remote_row_for(m->kind, 0, m->machine)) < 0)
                        break;
                // A row that can be had again (linked, or granted) is told wanted again at the pass if the graph wants it: the
                // link let go of what it was told while it could not be had.
                if ((audio_remote.rows[i].flags & (AUDIO_NODE_DENIED | AUDIO_NODE_AWAY)) != m->flags)
                        audio_remote.streams[i].told = 0;
                audio_remote.rows[i].flags = (audio_remote.rows[i].flags & (AUDIO_NODE_LIVE | AUDIO_NODE_MONITOR)) | m->flags;
                pulse_copy(audio_remote.rows[i].allow, sizeof(audio_remote.rows[i].allow), m->flags & AUDIO_NODE_DENIED ? m->allow : "");
                if (m->flags)
                {
                        // Not to be had now: what was coming is let go (the ring is the link's, which ended the stream).
                        struct audio_remote_stream *st = audio_remote.streams + i;

                        if (st->in)
                                audio_call(syscall(munmap), (positive)st->in, CAST_SOUND_RING_BYTES, 0, 0, 0, 0), st->in = 0, st->started = 0;
                }
                break;
        case 'H': // a machine takes a stream of ours: a heard row, with its in-ring when it plays here
        case 'I': // an in-ring for a source row of a machine's own
                if (m->op == 'H' ? !m->heard : m->heard || audio_remote_way(m->kind, 0) != AUDIO_NODE_REMOTE_SOURCE)
                        goto done;
                if (audio_remote_way(m->kind, m->heard) == AUDIO_NODE_REMOTE_SOURCE ? count != 1 : count != 0)
                        goto done;
                if ((i = audio_remote_row_for(m->kind, m->heard, m->machine)) < 0)
                        break;
                if (count)
                {
                        struct audio_remote_stream *st = audio_remote.streams + i;
                        const struct cast_sound_ring *ring = audio_remote_ring_take(fds[0]);

                        fds[0] = -1;
                        if (!ring)
                                goto done;
                        if (st->in)
                                audio_call(syscall(munmap), (positive)st->in, CAST_SOUND_RING_BYTES, 0, 0, 0, 0);
                        st->in = ring, st->started = 0, st->current_block = ~0ull, st->seen = 0, st->fades = AUDIO_REMOTE_FADES;
                        st->target = AUDIO_REMOTE_TARGET_FIRST, st->counted[0] = st->counted[1] = 0, st->jitter_at = 0;
                        memory_zero(st->histogram, sizeof(st->histogram));
                }
                audio_remote.rows[i].flags &= ~(AUDIO_NODE_DENIED | AUDIO_NODE_AWAY);
                audio_remote.rows[i].allow[0] = 0;
                break;
        case 'X':
                if (count)
                        goto done;
                audio_remote_drop(audio_remote_find(m->kind, m->heard, m->machine));
                break;
        default:
                goto done;
        }
        ok = 1;
done:
        for (int k = 0; k < count; k++)
                if (fds[k] >= 0)
                        audio_call(syscall(close), (positive)fds[k], 0, 0, 0, 0, 0);
        return ok;
}

// What the link sent, a packet at a time, with its descriptors (MSG_CMSG_CLOEXEC); a malformed one ends it.
static void audio_remote_read(void)
{
        for (int rounds = 0; rounds < 64 && audio_remote.link >= 0; rounds++)
        {
                struct audio_remote_message m;
                unsigned char control[256] __attribute__((aligned(8)));
                struct audio_remote_iovec iov = {&m, sizeof(m)};
                struct audio_remote_msghdr msg = {0, 0, 0, &iov, 1, control, sizeof(control), 0, 0};
                int fds[2], count = 0, extra = 0;
                long got = audio_call(syscall(recvmsg), (positive)audio_remote.link, (positive)&msg, 0x40 | 0x40000000, 0, 0, 0); // DONTWAIT, CMSG_CLOEXEC

                if (got == -11 || got == -4) // EAGAIN, EINTR
                        return;
                // Every descriptor that came is counted and closed unless the message keeps it, however the packet is (an empty
                // record can carry them too).
                for (unsigned long at = 0; got >= 0 && at + sizeof(struct audio_remote_cmsghdr) <= msg.control_length;)
                {
                        struct audio_remote_cmsghdr *c = (struct audio_remote_cmsghdr *)(control + at);
                        unsigned long length = c->length;

                        if (length < sizeof(*c) || at + length > msg.control_length)
                                break;
                        if (c->level == 1 && c->type == 1)
                                for (unsigned long k = 0; k + sizeof(int) <= length - sizeof(*c); k += sizeof(int))
                                {
                                        int fd;

                                        memory_copy(&fd, control + at + sizeof(*c) + k, sizeof(fd));
                                        if (count < 2)
                                                fds[count++] = fd;
                                        else
                                                audio_call(syscall(close), (positive)fd, 0, 0, 0, 0, 0), extra = 1;
                                }
                        at += (length + 7ul) & ~7ul;
                }
                if (got <= 0) // the link closed its end, or the socket failed
                {
                        for (int k = 0; k < count; k++)
                                audio_call(syscall(close), (positive)fds[k], 0, 0, 0, 0, 0);
                        audio_remote_lost();
                        return;
                }
                if (got != (long)sizeof(m) || extra || (msg.flags & (0x20 | 0x8))) // MSG_TRUNC, MSG_CTRUNC
                {
                        for (int k = 0; k < count; k++)
                                audio_call(syscall(close), (positive)fds[k], 0, 0, 0, 0, 0);
                        count = -1;
                }
                if (count < 0 || !audio_remote_take(&m, fds, count)) // the take closes what it does not keep
                {
                        audio_say_text("the link's sound socket said something malformed; it is let go", "");
                        audio_remote_lost();
                        return;
                }
        }
}

static void audio_remote_accept(void)
{
        struct
        {
                int pid;
                unsigned int uid, gid;
        } who = {0, ~0u, ~0u};
        unsigned int size = sizeof(who);
        long fd = audio_call(syscall(accept4), (positive)audio_remote.listen, 0, 0, 0x80000 | 0x800, 0, 0);

        if (fd < 0)
                return;
        if (audio_call(syscall(getsockopt), (positive)fd, 1, 17, (positive)&who, (positive)&size, 0) < 0 || who.uid != 0) // SO_PEERCRED
        {
                audio_call(syscall(close), (positive)fd, 0, 0, 0, 0, 0);
                return;
        }
        audio_remote_lost(); // one link at a time: a new one is the listener started again
        audio_remote.link = (int)fd;
}

/* ---- the hooks the loop calls ---- */

// The descriptors the loop waits on for the link, put in `into` (room of them); with no `into`, how many there are.
static int audio_remote_poll(struct audio_poll *into, int room)
{
        int n = 0;

        audio_remote_listen(AUDIO_REMOTE_NOW());
        if (!into)
                return (audio_remote.listen >= 0) + (audio_remote.link >= 0);
        if (audio_remote.listen >= 0 && n < room)
                into[n++] = (struct audio_poll){audio_remote.listen, AUDIO_POLLIN, 0};
        if (audio_remote.link >= 0 && n < room)
                into[n++] = (struct audio_poll){audio_remote.link, AUDIO_POLLIN, 0};
        return n;
}

static void audio_remote_ready(const struct audio_poll *polled, int count, unsigned long long now)
{
        (void)now;
        for (int k = 0; k < count; k++)
                if (polled[k].returned)
                {
                        if (polled[k].handle == audio_remote.listen)
                                audio_remote_accept();
                        else if (polled[k].handle == audio_remote.link)
                        {
                                if (polled[k].returned & (AUDIO_POLLHUP | AUDIO_POLLERR))
                                        audio_remote_lost();
                                else
                                        audio_remote_read();
                        }
                }
}

// An output's out-ring and its eventfd, made and sealed so the link can only read it: 1, or 0 (nothing made).
static int audio_remote_ring_make(struct audio_remote_stream *st)
{
        long fd = audio_call(syscall(memfd_create), (positive) "moonwater-sound", 1 | 2, 0, 0, 0, 0), got; // MFD_CLOEXEC | MFD_ALLOW_SEALING
        long wake;

        if (fd < 0)
                return 0;
        if (audio_call(syscall(ftruncate), (positive)fd, CAST_SOUND_RING_BYTES, 0, 0, 0, 0) < 0 ||
            ((got = audio_call(syscall(mmap), 0, CAST_SOUND_RING_BYTES, 3, 1, (positive)fd, 0)) < 0 && got > -4096))
        {
                audio_call(syscall(close), (positive)fd, 0, 0, 0, 0, 0);
                return 0;
        }
        // Shrink, grow, future writes and the seals themselves: the link's mapping is read-only, and the ring stays as long as it is.
        audio_call(syscall(fcntl), (positive)fd, 1033, 2 | 4 | 16 | 1, 0, 0, 0);
        wake = audio_call(syscall(eventfd2), 0, 0x80000 | 0x800, 0, 0, 0, 0);
        if (wake < 0)
        {
                audio_call(syscall(munmap), (positive)got, CAST_SOUND_RING_BYTES, 0, 0, 0, 0);
                audio_call(syscall(close), (positive)fd, 0, 0, 0, 0, 0);
                return 0;
        }
        st->out = (struct cast_sound_ring *)got;
        cast_sound_ring_init(st->out, (unsigned int)audio_call(syscall(getpid), 0, 0, 0, 0, 0, 0));
        st->out_fd = (int)fd, st->wake_fd = (int)wake, st->out_block = 0, st->out_have = 0, st->grid = 0;
        return 1;
}

// The link told whether a row is wanted; an output's yes brings its ring.
static void audio_remote_tell(long i, int on)
{
        struct audio_remote_stream *st = audio_remote.streams + i;
        struct audio_remote_message m;
        int fds[2];
        int pushing = audio_remote.rows[i].way == AUDIO_NODE_REMOTE_OUTPUT;

        memory_zero(&m, sizeof(m));
        m.op = 'W', m.kind = (unsigned char)st->kind, m.heard = (unsigned char)st->heard, m.on = (unsigned char)on;
        pulse_copy(m.machine, sizeof(m.machine), audio_remote.rows[i].machine);
        if (on && pushing && !st->out && !audio_remote_ring_make(st))
                return;
        fds[0] = st->out_fd, fds[1] = st->wake_fd;
        if (audio_remote_send(&m, fds, on && pushing ? 2 : 0))
                st->told = on;
        if (!on && pushing && st->out)
        {
                // The ring goes with the stream: the next push makes another, which the link is given with its next yes.
                audio_call(syscall(munmap), (positive)st->out, CAST_SOUND_RING_BYTES, 0, 0, 0, 0);
                audio_call(syscall(close), (positive)st->out_fd, 0, 0, 0, 0, 0);
                audio_call(syscall(close), (positive)st->wake_fd, 0, 0, 0, 0, 0);
                st->out = 0, st->out_fd = st->wake_fd = -1;
                audio_table_give(st->push_table), st->push_table = 0;
        }
}

// Each pass: a row the graph started to pull or push is told wanted, one it stopped for AUDIO_REMOTE_IDLE_NS is told not.
static void audio_remote_pass(unsigned long long now)
{
        for (long i = 0; i < audio_remote.count; i++)
        {
                struct audio_remote_stream *st = audio_remote.streams + i;
                struct audio_remote_row *row = audio_remote.rows + i;
                unsigned long long last = st->pulled_at > st->pushed_at ? st->pulled_at : st->pushed_at;
                // AWAY is wanted all the same: the link dials the machine for it. DENIED is not: only that machine can change it.
                // (A pull or push of this pass came after the loop read its clock: that is now, not 2^64 ns ago.)
                int wanted = last && (last >= now || now - last < AUDIO_REMOTE_IDLE_NS) && !(row->flags & AUDIO_NODE_DENIED);
                unsigned long long written = st->in ? __atomic_load_n(&st->in->written, __ATOMIC_ACQUIRE) : 0;

                if (st->dead)
                        continue;
                if (wanted != st->told)
                        audio_remote_tell(i, wanted);
                row->flags = (row->flags & ~AUDIO_NODE_LIVE) |
                             (wanted && (row->way == AUDIO_NODE_REMOTE_OUTPUT ? st->out != 0 : st->in && written && st->fades < AUDIO_REMOTE_FADES)
                                  ? AUDIO_NODE_LIVE : 0);
                row->lost = (unsigned int)st->lost;
                // The log says when sound starts and stops flowing, and every ten seconds while it does, in numbers.
                if (!!(row->flags & AUDIO_NODE_LIVE) != st->said_live || ((row->flags & AUDIO_NODE_LIVE) && now - st->said_at >= 10000000000ull))
                {
                        st->said_live = !!(row->flags & AUDIO_NODE_LIVE), st->said_at = now;
                        if (row->way == AUDIO_NODE_REMOTE_SOURCE)
                                string_format(log, "audio: %s %s: %p us behind its mix there, a buffer of %p blocks, %p concealed, %p underruns, %p resyncs\n",
                                              row->name, st->said_live ? "plays" : "stopped", (positive)row->latency_us, (positive)st->target,
                                              (positive)st->lost, (positive)st->underruns, (positive)st->resyncs);
                        else
                                string_format(log, "audio: %s %s: %p blocks sent\n", row->name, st->said_live ? "is sent to" : "stopped",
                                              (positive)st->out_block);
                        log_flush();
                }
        }
}

static struct audio_remote_stream *audio_remote_dev(long dev, unsigned int way)
{
        if (dev < 1 || dev > audio_remote.count || audio_remote.streams[dev - 1].dead || audio_remote.rows[dev - 1].way != way)
                return 0;
        return audio_remote.streams + (dev - 1);
}

/* ---- an output: pushed, cut into blocks, written ---- */

// A whole block written to the out-ring, stamped on the grid, and the link woken.
static void audio_remote_block_out(struct audio_remote_stream *st, unsigned long long now)
{
        unsigned long long one = 1;

        if (!st->grid || (now > st->grid ? now - st->grid : st->grid - now) > 50000000ull)
                st->grid = now; // first, or 50 ms off (the graph stopped for a while): from now
        else
        {
                long long off = (long long)(now - st->grid);

                st->grid += (unsigned long long)(off / 32);
        }
        cast_sound_write(st->out, st->out_block, st->grid, now, st->out_pcm);
        st->out_block++;
        st->grid += CAST_SOUND_BLOCK_NS;
        audio_call(syscall(write), (positive)st->wake_fd, (positive)&one, 8, 0, 0, 0);
}

static void remote_push(long dev, const float *left, const float *right, long frames, long rate)
{
        struct audio_remote_stream *st = audio_remote_dev(dev, AUDIO_NODE_REMOTE_OUTPUT);
        unsigned long long now = AUDIO_REMOTE_NOW();
        long at = 0;

        if (!st || frames < 1 || frames > AUDIO_BLOCK || rate < 1 || rate > AUDIO_RATE_MAX)
                return;
        st->pushed_at = now;
        if (!st->out || !st->told)
                return; // made and given to the link at the pass (audio_remote_tell)
        // A mix rate that is not the engine's is resampled to it first (a table of its own; the far side follows the drift).
        if ((unsigned long)rate != CAST_SOUND_RATE)
        {
                if (!st->push_table || st->push_rate != rate)
                {
                        audio_table_give(st->push_table);
                        if (!(st->push_table = audio_table_take(rate, CAST_SOUND_RATE)))
                                return;
                        audio_rs_open(&st->push_rs, st->push_table), st->push_rate = rate;
                }
                if (audio_rs_room(&st->push_rs) < frames)
                        audio_rs_reset(&st->push_rs);
                memory_copy(st->push_rs.buf[0] + st->push_rs.have, left, (unsigned long)frames * sizeof(float));
                memory_copy(st->push_rs.buf[1] + st->push_rs.have, right, (unsigned long)frames * sizeof(float));
                st->push_rs.have += frames;
                frames = audio_rs_run(&st->push_rs, st->push_out[0], st->push_out[1], AUDIO_BLOCK);
                left = st->push_out[0], right = st->push_out[1];
        }
        while (at < frames)
        {
                long n = frames - at < (long)CAST_SOUND_FRAMES - st->out_have ? frames - at : (long)CAST_SOUND_FRAMES - st->out_have;

                audio_encode(AUDIO_FORMAT_S16LE, (unsigned char *)(st->out_pcm + st->out_have * 2), n, 2, left + at, right + at);
                st->out_have += n, at += n;
                if (st->out_have == (long)CAST_SOUND_FRAMES)
                {
                        // The push's first frame is mixed now; the block's first is (at - a block) frames from it, maybe in a push before.
                        long long from = ((long long)at - (long long)CAST_SOUND_FRAMES) * 1000000000ll / (long long)CAST_SOUND_RATE;

                        audio_remote_block_out(st, (unsigned long long)((long long)now + from));
                        st->out_have = 0;
                }
        }
}

/* ---- a source: the jitter buffer, the drift, the loss ---- */

// The blocks that came since the last look, counted in the jitter's histogram (how late each was, from its stamp), and the
// target made again from the two halves of the window: the 95th percentile less the least, in whole blocks, and one more.
static void audio_remote_jitter(struct audio_remote_stream *st, unsigned long long now, unsigned long long written)
{
        struct cast_sound_slot slot;

        if (written > st->seen + CAST_SOUND_SLOTS)
                st->seen = written - CAST_SOUND_SLOTS;
        for (; st->seen < written; st->seen++)
        {
                unsigned long long late;
                unsigned long bucket;

                if (!cast_sound_read(st->in, st->seen, &slot))
                        continue;
                late = slot.arrived > slot.stamp ? slot.arrived - slot.stamp : 0;
                bucket = (unsigned long)(late / AUDIO_REMOTE_BUCKET_NS);
                st->histogram[1][bucket < AUDIO_REMOTE_BUCKETS ? bucket : AUDIO_REMOTE_BUCKETS - 1]++;
                st->counted[1]++;
        }
        if (!st->jitter_at)
                st->jitter_at = now;
        if (now - st->jitter_at < AUDIO_REMOTE_JITTER_NS)
                return;
        st->jitter_at = now;
        if (st->counted[0] + st->counted[1] >= 100)
        {
                unsigned int total = st->counted[0] + st->counted[1], sum = 0, least = AUDIO_REMOTE_BUCKETS, high = 0;
                long blocks;

                for (unsigned int b = 0; b < AUDIO_REMOTE_BUCKETS; b++)
                {
                        unsigned int here = st->histogram[0][b] + st->histogram[1][b];

                        if (here && least == AUDIO_REMOTE_BUCKETS)
                                least = b;
                        sum += here;
                        if (sum * 20 >= total * 19)
                        {
                                high = b;
                                break;
                        }
                }
                // The spread (to its bucket's top) as frames, with the largest pull and the block's saw, in whole blocks.
                long spread = (long)(((unsigned long long)(high - least) + 1) * AUDIO_REMOTE_BUCKET_NS * CAST_SOUND_RATE / 1000000000ull);
                long pull = st->largest[0] > st->largest[1] ? st->largest[0] : st->largest[1];

                blocks = (pull + (long)CAST_SOUND_FRAMES + spread + (long)CAST_SOUND_FRAMES - 1) / (long)CAST_SOUND_FRAMES;
                blocks = blocks < AUDIO_REMOTE_TARGET_LEAST ? AUDIO_REMOTE_TARGET_LEAST : blocks > AUDIO_REMOTE_TARGET_MOST ? AUDIO_REMOTE_TARGET_MOST : blocks;
                // Up at once, down a block a look: a burst of late arrivals is not forgotten the second after it.
                st->target = blocks >= st->target ? blocks : st->target - 1;
        }
        memory_copy(st->histogram[0], st->histogram[1], sizeof(st->histogram[0]));
        memory_zero(st->histogram[1], sizeof(st->histogram[1]));
        st->counted[0] = st->counted[1], st->counted[1] = 0;
        st->largest[0] = st->largest[1], st->largest[1] = 0;
}

/*
        frames of the stream from the read position, as floats, the position moved on: a block not in the ring is concealed. A block
        the ring has not come to yet (the reader at the writer's edge: the arrivals are later than the target allows) is not
        skipped: what is left of the pull is concealment and the position stays, so the fill grows by what was made up, and the
        target is a block more at once (the window brings it down again, a block a second, when the arrivals are early again).
*/
static void audio_remote_frames(struct audio_remote_stream *st, float *left, float *right, long frames, unsigned long long written)
{
        long done = 0;

        while (done < frames)
        {
                unsigned long long block = (unsigned long long)(st->at / (long long)CAST_SOUND_FRAMES);
                long offset = (long)(st->at % (long long)CAST_SOUND_FRAMES), n = (long)CAST_SOUND_FRAMES - offset;
                float *planes[2] = {left + done, right + done};

                if (n > frames - done)
                        n = frames - done;
                if (st->current_block != block && block >= written)
                {
                        short held[CAST_SOUND_SAMPLES];
                        long rest = frames - done;

                        st->lost++, st->underruns++;
                        // A block more, once in a while: the pulls of one dip in the arrivals each find the edge, and each is not a block.
                        if (st->target < AUDIO_REMOTE_TARGET_MOST && st->now - st->raised >= AUDIO_REMOTE_RAISE_NS)
                                st->target++, st->raised = st->now;
                        st->fades += st->fades < AUDIO_REMOTE_FADES;
                        for (unsigned long k = 0; k < CAST_SOUND_SAMPLES; k++)
                                held[k] = st->fades >= AUDIO_REMOTE_FADES ? 0 : (short)(st->last[k] / 2);
                        memory_copy(st->last, held, sizeof(st->last));
                        while (rest > 0)
                        {
                                long chunk = rest < (long)CAST_SOUND_FRAMES ? rest : (long)CAST_SOUND_FRAMES;
                                float *made[2] = {left + done, right + done};

                                audio_decode(AUDIO_FORMAT_S16LE, (const unsigned char *)held, chunk, 2, made);
                                done += chunk, rest -= chunk;
                        }
                        return;
                }
                if (st->current_block != block)
                {
                        st->current_block = block;
                        if (cast_sound_read(st->in, block, &st->cur))
                        {
                                memory_copy(st->last, st->cur.pcm, sizeof(st->last));
                                st->fades = 0;
                        }
                        else
                        {
                                // Lost: the last block heard again, at half the level of the last time, until it is nothing.
                                st->lost++;
                                st->fades += st->fades < AUDIO_REMOTE_FADES;
                                for (unsigned long k = 0; k < CAST_SOUND_SAMPLES; k++)
                                        st->cur.pcm[k] = st->fades >= AUDIO_REMOTE_FADES ? 0 : (short)(st->last[k] / 2);
                                memory_copy(st->last, st->cur.pcm, sizeof(st->last));
                        }
                }
                audio_decode(AUDIO_FORMAT_S16LE, (const unsigned char *)(st->cur.pcm + offset * 2), n, 2, planes);
                st->at += n, done += n;
        }
}

static long remote_pull(long dev, float *left, float *right, long frames, long rate)
{
        struct audio_remote_stream *st = audio_remote_dev(dev, AUDIO_NODE_REMOTE_SOURCE);
        unsigned long long now = AUDIO_REMOTE_NOW(), written;
        long long fill, target;
        long need, made, ppm, most;

        if (frames < 1)
                return 0;
        if (frames > AUDIO_BLOCK)
                frames = AUDIO_BLOCK;
        memory_zero(left, (unsigned long)frames * sizeof(float));
        memory_zero(right, (unsigned long)frames * sizeof(float));
        if (!st)
                return frames;
        st->pulled_at = st->now = now;
        if (!st->in || rate < 1 || rate > AUDIO_RATE_MAX)
                return frames;
        if (!st->table || st->rate != rate)
        {
                audio_table_give(st->table);
                if (!(st->table = audio_table_take_adapting(CAST_SOUND_RATE, rate)))
                        return frames;
                audio_rs_open(&st->rs, st->table);
                st->step = st->rs.step, st->rate = rate, st->started = 0;
        }
        written = __atomic_load_n(&st->in->written, __ATOMIC_ACQUIRE);
        if (!written || written > (1ull << 40)) // nothing yet; or a count no stream reaches (2^40 blocks is 87 years), which no arithmetic here is held to
                return frames;
        audio_remote_jitter(st, now, written);
        target = (long long)st->target * CAST_SOUND_FRAMES;
        fill = (long long)(written * CAST_SOUND_FRAMES) - st->at;
        // Not begun: begun once the target is in the ring (10 ms at first). So far from the target that following it would be
        // heard for seconds: put back at the target now.
        if (!st->started && (long long)(written * CAST_SOUND_FRAMES) < target)
                return frames;
        if (!st->started || fill > target + 60 * (long long)CAST_SOUND_RATE / 1000 || fill < -2 * target)
        {
                if (st->started)
                        st->resyncs++;
                st->at = (long long)(written * CAST_SOUND_FRAMES) - target;
                st->at = st->at < 0 ? 0 : st->at; // the position is never before the stream: a block and its frame are read from it
                st->current_block = ~0ull;
                st->error = 0, st->started = 1;
                audio_rs_reset(&st->rs);
                fill = (long long)(written * CAST_SOUND_FRAMES) - st->at;
        }
        // The fill as the pull begins, smoothed over 32 pulls, moves the step (as a mirror's does: fuller, faster).
        st->error += ((float)(fill - target) - st->error) / 32.0f;
        most = st->error > (float)CAST_SOUND_FRAMES || st->error < -(float)CAST_SOUND_FRAMES ? AUDIO_REMOTE_PPM_FAR : AUDIO_REMOTE_PPM;
        ppm = (long)(st->error * 1000000.0f / (float)(CAST_SOUND_RATE * AUDIO_REMOTE_SETTLE));
        ppm = ppm > most ? most : ppm < -most ? -most : ppm;
        st->rs.step = (unsigned long)((long long)st->step + (long long)st->step / 1000000 * ppm);
        need = audio_rs_need(&st->rs, frames);
        st->largest[1] = need > st->largest[1] ? need : st->largest[1];
        if (audio_rs_room(&st->rs) < need)
        {
                audio_rs_reset(&st->rs);
                if (audio_rs_room(&st->rs) < (need = audio_rs_need(&st->rs, frames)))
                        return frames;
        }
        audio_remote_frames(st, st->rs.buf[0] + st->rs.have, st->rs.buf[1] + st->rs.have, need, written);
        st->rs.have += need;
        made = audio_rs_run(&st->rs, left, right, frames);
        (void)made; // need was what frames outputs take: made is frames
        // How old what is mixed now is: from its stamp (the far machine's mix, on this clock) to this pull; a block made up, or
        // one kept from before a pause, says nothing of it.
        if (!st->fades && st->cur.stamp && now > st->cur.stamp && now - st->cur.stamp < 1000000000ull)
                audio_remote.rows[dev - 1].latency_us = (unsigned int)((now - st->cur.stamp) / 1000);
        return frames;
}

#endif // AUDIO_REMOTE_INCLUDED
