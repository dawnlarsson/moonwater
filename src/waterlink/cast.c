/*
        The cast engine's channels over a link: sound now, video and input beside it later, in the one session two
        listeners keep with each other (the keeper's: link_keep_turn), on keys of their own beside the pad's
        (campaigns/cast-engine-design: the one session, the one clock, input before sound before video).

                LINK_KEY_CAST_CONTROL   either way, durable: grants, asks and answers, the clock
                LINK_KEY_CAST_SOUND     a register: what a machine plays          (the grant "screen")
                LINK_KEY_CAST_MIC       a register: a machine's microphone        (the grant "mic")
                LINK_KEY_CAST_SPEAKERS  a register: sound played on the far side  (the grant "speakers", the far side's)

        THE SOUND SERVER'S SIDE

        The listener is the sound server's link (src/audio/remote.c): it connects to its socket and tells it, for every
        machine in the peers file, the machine's three devices and whether each can be had ('R': not linked now, AWAY; not
        granted, DENIED, with the line that grants it, to be run there). The server says which it wants ('W', when the graph
        pulls or pushes one): an output brings the server's ring of what is to be played there, a source is answered with a
        ring the listener makes and writes what arrives into ('I'). What a far machine takes of this one is said to the server
        the same way ('H'), and the server's graph wires it by default: what is played here into the main mix, this machine's
        microphone or mix into what the far machine hears.

        ON THE WIRE

        A block is one frame on its kind's key: posted urgent and sent in the wake its ring woke the listener in, so it leaves
        alone and at once, ahead of anything paced (input is urgent too, and is read first in a turn). A register, so a
        frame lost is never sent again late: its slot carries the newest block instead, and every frame carries the block
        before it as well, so one lost datagram costs nothing.

                'B', version 1, count (1 or 2), block (32 bits), stamp (64 bits, ns, the sender's CLOCK_MONOTONIC),
                the block's 480 bytes, then the block before it when count is 2

        little endian. The receiver numbers a stream's blocks from its first and writes each, with its stamp moved onto its
        own clock (the offset the clock asks found, as the pad's are) and the time it came, into the ring the server reads;
        a block far ahead of the ring is the sender started again, and the numbering goes on from the ring's.

        On LINK_KEY_CAST_CONTROL, the type byte then:

                'G' grants (32 bits), name   what this machine grants the far one (screen, mic, speakers), and
                                             the name it has the far one under (for the line that grants the rest)
                'O' kind                     open: the asker's stream of that kind (1 speakers, 2 mic, 3 sound)
                'C' kind                     the asker closes it
                'A' kind                     the giver's yes
                'N' kind, line               the giver's no, and the line that grants it, run on the giver
                'E' kind                     the giver ended it (the grant went, or its sound server)
                'P' asked (64 bits)          the clock asked, as the pad's is; 'Q' asked, told: the answer

        WHAT IS TRUSTED

        Every frame of the far side's is judged before a byte of it is used: its type, its length to the byte, the kind,
        the version and count. A stream exists only when this side asked for it or granted it: a block on a key with no
        stream, or a yes to nothing asked, is dropped. The giver asks the peers file at every ask and every half second
        after, so a grant taken away ends what it granted. The rings the server gives are taken only when they are
        sealed memfds at least a ring long, mapped read-only.

        Dawn Larsson - Apache-2.0 license
        github.com/dawnlarsson/moonwater
*/

#ifndef WATERLINK_CAST_INCLUDED
#define WATERLINK_CAST_INCLUDED

#include "../cast/sound.h"

#define LINK_CAST_SPEAKERS 1
#define LINK_CAST_MIC 2
#define LINK_CAST_SOUND 3
#define LINK_CAST_KINDS 4

#define LINK_CAST_SOCKET "/run/moonwater/audio.link"
#define LINK_CAST_LOOK 500000ull    // how often the rows and the grants are looked at again
#define LINK_CAST_RETRY 2000000ull  // a sound server not there is tried again after this
#define LINK_CAST_PING 1000000ull
#define LINK_CAST_ASK_AGAIN 5000000ull // an ask the far side refused or never answered is made again after this, while it is wanted
#define LINK_CAST_HEAD 14           // version, count, block, stamp
#define LINK_CAST_BLOCK_BYTES (CAST_SOUND_SAMPLES * 2)
#define LINK_CAST_SENDS 8           // blocks one turn sends of a stream at most (the rest are old: the newest go)

#define LINK_CAST_IDLE 0
#define LINK_CAST_ASKED 1
#define LINK_CAST_LIVE 2

_Static_assert(1 + LINK_CAST_HEAD + 2 * LINK_CAST_BLOCK_BYTES <= WATERLINK_FRAME_MAX, "a block and the one before it are one frame");

// One record each way on the sound server's socket: src/audio/remote.c's struct audio_remote_message, byte for byte.
typedef struct
{
        p8 op, kind, heard, on;
        p32 flags;
        p8 machine[32];
        p8 allow[64];
} link_cast_message;

_Static_assert(sizeof(link_cast_message) == 104, "the sound server's message");

#define LINK_CAST_DENIED 1u // AUDIO_NODE_DENIED
#define LINK_CAST_AWAY 2u   // AUDIO_NODE_AWAY

typedef struct
{
        p8 state;          // LINK_CAST_*
        p8 wanted;         // an ask: the server wants it (asked again every LINK_CAST_ASK_AGAIN while it is not had)
        p8 paused;         // a give: the server does not want it now (the person unwired it): kept, nothing moves
        p64 asked_at;
        // Sent: the server's ring, read here.
        const struct cast_sound_ring address_to out;
        bipolar wake;
        p8 wake_quiet;
        p64 next;          // the ring's next block to send
        p8 primed;         // next was set from the ring
        p64 previous_block;
        p8 previous_held;
        short previous[CAST_SOUND_SAMPLES];
        // Received: this side's ring, written here, read by the server.
        struct cast_sound_ring address_to in;
        p32 base;
        p8 based;
        p64 frames, late, jumps, sent, skipped;
} link_cast_stream;

struct link_cast
{
        p32 grants_said;   // what this side last said it grants (0x80000000: never said)
        p8 heard;          // the far side said what it grants
        p32 grants;        // and that is
        p8 known_as[WATERLINK_NAME_MAX]; // this machine's name at the far side
        p8 refusal[LINK_CAST_KINDS][64]; // the far side's line for a kind it refused
        p64 looked;
        p64 pinged;
        b64 offset;        // the far clock less this one, ns
        p64 offset_rtt, offset_at;
        link_cast_stream ask[LINK_CAST_KINDS];   // streams this side asked of the far one
        link_cast_stream give[LINK_CAST_KINDS];  // streams the far side asked of this one
};

#if !defined(MACOS)

static struct
{
        bipolar fd;
        p8 quiet;
        p64 tried, looked;
        positive live;     // streams moving, for the listener's priority
        // The rows the server was told, so only a change is said again.
        struct { p8 machine[WATERLINK_NAME_MAX]; p8 kind; p32 flags; p8 allow[64]; p8 used; } said[LINK_PEERS_MAX * 3];
} link_cast_server = {-1};

static const p32 link_cast_bits[LINK_CAST_KINDS] = {0, WATERLINK_MAY_SPEAKERS, WATERLINK_MAY_MIC, WATERLINK_MAY_SCREEN};
static const string_address link_cast_grant_words[LINK_CAST_KINDS] = {"", "speakers", "mic", "screen"};
static const p8 link_cast_keys[LINK_CAST_KINDS] = {0, LINK_KEY_CAST_SPEAKERS, LINK_KEY_CAST_MIC, LINK_KEY_CAST_SOUND};

// Whether this side sends the stream: an ask of the far speakers, or a give of this microphone or this mix.
static p8 link_cast_sends(p8 give, p8 kind)
{
        return give ? kind != LINK_CAST_SPEAKERS : kind == LINK_CAST_SPEAKERS;
}

/* ---- the sound server's socket ---- */

typedef struct
{
        address_any base;
        positive length;
} link_cast_iovec;

typedef struct
{
        address_any name;
        p32 name_length, hole;
        link_cast_iovec address_to iov;
        positive iov_length;
        address_any control;
        positive control_length;
        b32 flags, hole2;
} link_cast_msghdr;

typedef struct
{
        positive length;
        b32 level, type;
} link_cast_cmsghdr;

static p8 link_cast_tell(link_cast_message address_to m, bipolar fd)
{
        p8 control[sizeof(link_cast_cmsghdr) + 8] __attribute__((aligned(8)));
        link_cast_iovec iov = {m, sizeof(address_to m)};
        link_cast_msghdr msg = {null, 0, 0, address_of iov, 1, null, 0, 0, 0};
        bipolar sent;

        if (link_cast_server.fd < 0)
                return false;
        if (fd >= 0)
        {
                link_cast_cmsghdr address_to c = (link_cast_cmsghdr address_to)control;
                b32 one = (b32)fd;

                memory_zero(control, sizeof control);
                c->length = sizeof(link_cast_cmsghdr) + sizeof(b32), c->level = 1, c->type = 1; // SOL_SOCKET, SCM_RIGHTS
                memory_copy(control + sizeof(link_cast_cmsghdr), address_of one, sizeof one);
                msg.control = control, msg.control_length = sizeof control;
        }
        sent = system_call_3(syscall(sendmsg), (positive)link_cast_server.fd, (positive)address_of msg, 0x40 | 0x4000);
        return sent == (bipolar)sizeof(address_to m);
}

static fn link_cast_say(p8 op, p8 kind, p8 heard, string_address machine, p32 flags, string_address allow, bipolar fd)
{
        link_cast_message m;

        memory_zero(address_of m, sizeof m);
        m.op = op, m.kind = kind, m.heard = heard, m.flags = flags;
        string_copy_bounded((string_address)m.machine, machine, sizeof m.machine);
        if (allow)
                string_copy_bounded((string_address)m.allow, allow, sizeof m.allow);
        (void)link_cast_tell(address_of m, fd);
}

// A ring of this side's, for the server to read: made, mapped, sealed so the server's mapping can only read it.
static struct cast_sound_ring address_to link_cast_ring_make(bipolar address_to fd_out)
{
        bipolar fd = system_call_2(syscall(memfd_create), (positive) "moonwater-sound-in", 1 | 2); // CLOEXEC, ALLOW_SEALING
        bipolar mapped;

        *fd_out = -1;
        if (fd < 0)
                return null;
        if (system_call_2(syscall(ftruncate), (positive)fd, CAST_SOUND_RING_BYTES) < 0)
        {
                system_close(fd);
                return null;
        }
        mapped = system_call_6(syscall(mmap), 0, CAST_SOUND_RING_BYTES, FILE_PROTECT_READ | FILE_PROTECT_WRITE, 1, (positive)fd, 0);
        if (mapped < 0 && mapped > -4096)
        {
                system_close(fd);
                return null;
        }
        (void)system_call_3(syscall(fcntl), (positive)fd, 1033, 2 | 4 | 16 | 1); // F_ADD_SEALS: shrink, grow, future write, seal
        cast_sound_ring_init((struct cast_sound_ring address_to)mapped, (p32)system_call_1(syscall(getpid), 0));
        *fd_out = fd;
        return (struct cast_sound_ring address_to)mapped;
}

static fn link_cast_unmap(const address_any ring)
{
        if (ring)
                (void)system_call_2(syscall(munmap), (positive)ring, CAST_SOUND_RING_BYTES);
}

static fn link_cast_stream_stop(link_cast_stream address_to st)
{
        link_cast_unmap(st->out);
        link_cast_unmap(st->in);
        if (st->wake >= 0)
                system_close(st->wake);
        memory_zero(st, sizeof(address_to st));
        st->wake = -1;
}

// The server's ring of what this side sends, taken when it is a sealed memfd a ring long; its eventfd made non-blocking.
static p8 link_cast_take_out(link_cast_stream address_to st, bipolar ring_fd, bipolar wake_fd)
{
        bipolar seals = system_call_3(syscall(fcntl), (positive)ring_fd, 1034, 0);
        bipolar size = system_call_3(syscall(lseek), (positive)ring_fd, 0, 2);
        bipolar mapped = -1;

        if (seals >= 0 && (seals & 2) && (seals & 4) && size >= (bipolar)CAST_SOUND_RING_BYTES)
                mapped = system_call_6(syscall(mmap), 0, CAST_SOUND_RING_BYTES, FILE_PROTECT_READ, 1, (positive)ring_fd, 0);
        system_close(ring_fd);
        if ((mapped < 0 && mapped > -4096) || !cast_sound_ring_good((const struct cast_sound_ring address_to)mapped, CAST_SOUND_RING_BYTES) ||
            system_call_3(syscall(fcntl), (positive)wake_fd, 4, O_NONBLOCK) < 0) // F_SETFL
        {
                if (!(mapped < 0 && mapped > -4096))
                        link_cast_unmap((address_any)mapped);
                system_close(wake_fd);
                return false;
        }
        link_cast_unmap(st->out);
        if (st->wake >= 0)
                system_close(st->wake);
        st->out = (const struct cast_sound_ring address_to)mapped;
        st->wake = wake_fd, st->wake_quiet = false, st->primed = false, st->previous_held = false;
        return true;
}

/* ---- the sessions ---- */

/*      A session's cast state, made the first time: by the side that dialled it (the keeper's, so the far side is a
        listener too), and by the other when the first cast frame comes. A command's session never has one. */
static struct link_cast address_to link_cast_state(struct link_session address_to s)
{
        bipolar mapped;

        if (s->cast)
                return s->cast;
        mapped = system_call_6(syscall(mmap), 0, sizeof(struct link_cast), FILE_PROTECT_READ | FILE_PROTECT_WRITE,
                               FILE_MAP_PRIVATE | FILE_MAP_ANONYMOUS, (positive)(bipolar)-1, 0);
        if (mapped < 0 && mapped > -4096)
                return null;
        s->cast = (struct link_cast address_to)mapped;
        s->cast->grants_said = 0x80000000u;
        for (positive k = 0; k < LINK_CAST_KINDS; k++)
                s->cast->ask[k].wake = s->cast->give[k].wake = -1;
        link_cast_server.looked = 0;
        return s->cast;
}

// A session this listener keeps with another listener, up: the one the channels ride.
static p8 link_cast_session_ok(struct link_session address_to s)
{
        return link_self.server && s->used && s->now.live && s->kind == LINK_KIND_NONE && !s->exit_sent;
}

/*      The session the channels with a peer ride: for a stream the far side asked of this one, the session it asked on (two
        listeners that dialled each other at once keep two sessions, and each side may have asked on either); else the first. */
static struct link_session address_to link_cast_session_for(p8 address_to key, p8 give, p8 kind)
{
        struct link_session address_to first = null;

        for (positive at = 0; at < LINK_SESSIONS; at++)
        {
                struct link_session address_to s = link_self.session + at;

                if (!link_cast_session_ok(s) || !s->cast || !crypto_same(s->peer, key, 32))
                        continue;
                if (give && kind && s->cast->give[kind].state != LINK_CAST_IDLE)
                        return s;
                if (!first)
                        first = s;
        }
        return first;
}

static struct link_session address_to link_cast_session_of(p8 address_to key)
{
        return link_cast_session_for(key, false, 0);
}

// The peer's record now, and its name: null when it is gone or switched off.
static struct waterlink_peer address_to link_cast_peer(link_peers address_to peers, p8 address_to key)
{
        struct waterlink_peer address_to peer;

        link_peers_load(peers);
        peer = link_peer_keyed(peers, key);
        return peer && !(peer->flags & WATERLINK_PEER_SHUT) ? peer : null;
}

// What this side grants the far one, as the peers file and the policy say now.
static p32 link_cast_grants_now(struct waterlink_peer address_to peer)
{
        return peer ? peer->may & link_policy_load() & (WATERLINK_MAY_SCREEN | WATERLINK_MAY_MIC | WATERLINK_MAY_SPEAKERS) : 0;
}

static fn link_cast_post(struct link_session address_to s, p8 type, p8 address_to data, positive length)
{
        link_self.now = link_now();
        (void)link_post(s, LINK_KEY_CAST_CONTROL, WATERLINK_FRAME_DURABLE, type, data, length);
}

static fn link_cast_post_kind(struct link_session address_to s, p8 type, p8 kind)
{
        link_cast_post(s, type, address_of kind, 1);
}

static fn link_cast_priority(void)
{
        positive live = 0;

        for (positive at = 0; at < LINK_SESSIONS; at++)
                if (link_self.session[at].used && link_self.session[at].cast)
                        for (positive k = 1; k < LINK_CAST_KINDS; k++)
                                live += link_self.session[at].cast->ask[k].state == LINK_CAST_LIVE ||
                                        link_self.session[at].cast->give[k].state == LINK_CAST_LIVE;
        if (!live != !link_cast_server.live && !link_pad_made)
                link_pad_priority(live ? 1 : 0, live ? LINK_PAD_PRIORITY_MACHINE : 0);
        link_cast_server.live = live;
}

// The line that grants a kind on the far machine, run there.
static fn link_cast_allow_line(struct link_cast address_to c, p8 kind, p8 address_to line, positive room)
{
        p8 own[WATERLINK_NAME_MAX];

        if (c && c->refusal[kind][0])
        {
                string_copy_bounded((string_address)line, (string_address)c->refusal[kind], room);
                return;
        }
        if (c && c->known_as[0])
                string_copy_bounded((string_address)own, (string_address)c->known_as, sizeof own);
        else
                link_machine_name(own);
        string_copy_bounded((string_address)line, "moonwater link allow ", room);
        string_append_bounded((string_address)line, (string_address)own, room);
        string_append_bounded((string_address)line, " ", room);
        string_append_bounded((string_address)line, link_cast_grant_words[kind], room);
}

// A stream that ends: its rings let go, and the server told (a heard row goes; a source of the far machine's is left to its 'R').
static fn link_cast_end(struct link_session address_to s, p8 give, p8 kind, string_address machine)
{
        link_cast_stream address_to st = give ? s->cast->give + kind : s->cast->ask + kind;

        if (st->state == LINK_CAST_IDLE && !st->out && !st->in)
                return;
        link_cast_stream_stop(st);
        if (give)
                link_cast_say('X', kind, 1, machine, 0, null, -1);
        link_cast_priority();
}

// The far side asks a stream of this one: granted, it is a heard row of the server's (with a ring when it plays here).
static fn link_cast_opened(struct link_session address_to s, p8 kind, string_address machine, struct waterlink_peer address_to peer)
{
        link_cast_stream address_to st = s->cast->give + kind;
        p8 line[96];

        if (!(link_cast_grants_now(peer) & link_cast_bits[kind]))
        {
                // The line the asker shows: run here, with the name this machine has the asker under.
                line[0] = 'N', line[1] = kind;
                string_copy_bounded((string_address)line + 2, "moonwater link allow ", sizeof line - 2);
                string_append_bounded((string_address)line + 2, machine, sizeof line - 2);
                string_append_bounded((string_address)line + 2, " ", sizeof line - 2);
                string_append_bounded((string_address)line + 2, link_cast_grant_words[kind], sizeof line - 2);
                link_cast_post(s, line[0], line + 1, 1 + string_length((string_address)line + 2));
                return;
        }
        if (link_cast_server.fd < 0)
        {
                link_cast_post(s, 'N', (p8[]){kind}, 1); // no sound server here now: no line grants that
                return;
        }
        link_cast_stream_stop(st);
        if (kind == LINK_CAST_SPEAKERS)
        {
                bipolar fd;

                st->in = link_cast_ring_make(address_of fd);
                if (!st->in)
                {
                        link_cast_post(s, 'N', (p8[]){kind}, 1);
                        return;
                }
                link_cast_say('H', kind, 1, machine, 0, null, fd);
                system_close(fd);
        }
        else
                link_cast_say('H', kind, 1, machine, 0, null, -1);
        st->state = LINK_CAST_LIVE;
        link_cast_post_kind(s, 'A', kind);
        link_cast_priority();
        string_format(log, host_label "%s %s this machine's %s\n", machine,
                      kind == LINK_CAST_SPEAKERS ? "plays on" : "hears", kind == LINK_CAST_SPEAKERS ? "speakers" : kind == LINK_CAST_MIC ? "microphone" : "sound");
        log_flush();
}

// The giver said yes to an ask: a source's ring is made and given to the server; an output's ring is the server's already.
static fn link_cast_accepted(struct link_session address_to s, p8 kind, string_address machine)
{
        link_cast_stream address_to st = s->cast->ask + kind;

        if (st->state != LINK_CAST_ASKED)
                return;
        st->state = LINK_CAST_LIVE;
        s->cast->refusal[kind][0] = 0;
        if (kind != LINK_CAST_SPEAKERS)
        {
                bipolar fd;

                link_cast_unmap(st->in);
                st->in = link_cast_ring_make(address_of fd);
                st->based = false;
                if (st->in)
                {
                        link_cast_say('I', kind, 0, machine, 0, null, fd);
                        system_close(fd);
                }
        }
        link_cast_priority();
}

/*
        A frame on one of the cast keys, from a session this listener keeps: judged whole before it is believed. Answers
        nothing: a frame that does not belong is dropped.
*/
static fn link_cast_hear(struct link_session address_to s, p8 key, p8 address_to payload, positive length)
{
        struct link_cast address_to c;
        link_peers peers;
        struct waterlink_peer address_to peer;
        p64 now = link_now();

        if (!length || !link_cast_session_ok(s) || (!s->cast && key != LINK_KEY_CAST_CONTROL) || !(c = link_cast_state(s)))
                return;
        if (key == LINK_KEY_CAST_CONTROL)
        {
                p8 type = payload[0];
                p8 kind = length >= 2 ? payload[1] : 0;
                p8 known = kind >= LINK_CAST_SPEAKERS && kind <= LINK_CAST_SOUND;

                if (type == 'G' && length >= 5 && length <= 5 + WATERLINK_NAME_MAX - 1)
                {
                        c->grants = memory_load_unaligned(p32, payload + 1) & (WATERLINK_MAY_SCREEN | WATERLINK_MAY_MIC | WATERLINK_MAY_SPEAKERS);
                        c->heard = true;
                        memory_zero(c->known_as, sizeof c->known_as);
                        for (positive at = 0; at < length - 5; at++)
                                c->known_as[at] = byte_is_printable(payload[5 + at]) && payload[5 + at] != ' ' ? payload[5 + at] : '?';
                        link_cast_server.looked = 0; // the rows again, now
                }
                else if (type == 'P' && length == 9)
                {
                        p8 answer[16];

                        memory_copy(answer, payload + 1, 8);
                        memory_store_unaligned(p64, answer + 8, clock_monotonic_nanoseconds());
                        link_cast_post(s, 'Q', answer, sizeof answer);
                }
                else if (type == 'Q' && length == 17)
                {
                        p64 asked = memory_load_unaligned(p64, payload + 1), told = memory_load_unaligned(p64, payload + 9);
                        p64 here = clock_monotonic_nanoseconds(), rtt;

                        if (asked > here || here - asked > 4 * LINK_CAST_PING * 1000)
                                return;
                        rtt = here - asked;
                        if (c->offset_at && rtt > c->offset_rtt && link_age(now, c->offset_at) < 30000000ull)
                                return;
                        c->offset = (b64)told - (b64)(asked + rtt / 2);
                        c->offset_rtt = rtt, c->offset_at = now;
                }
                else if (known && length == 2 && (type == 'O' || type == 'C'))
                {
                        if (!(peer = link_cast_peer(address_of peers, s->peer)))
                                return;
                        if (type == 'O')
                                link_cast_opened(s, kind, (string_address)peer->name, peer);
                        else
                                link_cast_end(s, true, kind, (string_address)peer->name);
                }
                else if (known && length == 2 && type == 'A')
                {
                        if ((peer = link_cast_peer(address_of peers, s->peer)))
                                link_cast_accepted(s, kind, (string_address)peer->name);
                }
                else if (known && (type == 'N' || (type == 'E' && length == 2)) && length <= 2 + 63)
                {
                        link_cast_stream address_to st = c->ask + kind;

                        if (type == 'N' && st->state != LINK_CAST_ASKED)
                                return;
                        if (type == 'N')
                        {
                                for (positive at = 0; at < length - 2; at++)
                                        c->refusal[kind][at] = byte_is_printable(payload[2 + at]) ? payload[2 + at] : '?';
                                c->refusal[kind][length - 2] = 0;
                        }
                        // The stream is over; the server's ring of an output is kept for the next ask (it is still its stream),
                        // which is made again in a while if the server still wants it.
                        link_cast_unmap(st->in);
                        st->in = null;
                        st->state = LINK_CAST_IDLE, st->asked_at = now;
                        link_cast_server.looked = 0;
                        link_cast_priority();
                }
                return;
        }
        // A block, on its kind's key: the far side's mix or microphone this side asked for, or what it plays here.
        {
                p8 kind = key == LINK_KEY_CAST_SPEAKERS ? LINK_CAST_SPEAKERS : key == LINK_KEY_CAST_MIC ? LINK_CAST_MIC : LINK_CAST_SOUND;
                link_cast_stream address_to st = kind == LINK_CAST_SPEAKERS ? c->give + kind : c->ask + kind;
                p8 count;
                p32 block;
                b32 ahead;
                p64 stamp, in, written, here;

                if (key < LINK_KEY_CAST_SOUND || key > LINK_KEY_CAST_SPEAKERS || st->state != LINK_CAST_LIVE || !st->in ||
                    payload[0] != 'B' || length < 1 + LINK_CAST_HEAD || payload[1] != 1)
                        return;
                count = payload[2];
                if ((count != 1 && count != 2) || length != 1 + LINK_CAST_HEAD + (positive)count * LINK_CAST_BLOCK_BYTES)
                        return;
                block = memory_load_unaligned(p32, payload + 3);
                stamp = memory_load_unaligned(p64, payload + 7);
                here = clock_monotonic_nanoseconds();
                // On this clock: the far stamp less the far clock's offset; before the first answer, the time it came.
                stamp = c->offset_at ? (p64)((b64)stamp - c->offset) : here;
                written = __atomic_load_n(&st->in->written, __ATOMIC_RELAXED);
                if (!st->based)
                        st->base = block - (count - 1), st->based = true, written = 0;
                //      How far past the stream's first the block is, as a difference of the sender's 32-bit numbers: one behind
                //      the first, or a lap behind the ring, is late; one far ahead of the ring is the sender begun again, and its
                //      blocks go on from the ring's.
                ahead = (b32)(block - st->base);
                if (ahead < 0)
                {
                        st->late++;
                        return;
                }
                in = (p64)ahead;
                if (in > written + 256)
                {
                        st->base = block - (p32)written;
                        in = written;
                        st->jumps++;
                }
                if (in + CAST_SOUND_SLOTS <= written)
                {
                        st->late++;
                        return;
                }
                if (!cast_sound_has(st->in, in))
                        cast_sound_write(st->in, in, stamp, here, (const short *)(payload + 1 + LINK_CAST_HEAD));
                if (count == 2 && in > 0 && in - 1 + CAST_SOUND_SLOTS > written && !cast_sound_has(st->in, in - 1))
                        cast_sound_write(st->in, in - 1, stamp - CAST_SOUND_BLOCK_NS, here,
                                         (const short *)(payload + 1 + LINK_CAST_HEAD + LINK_CAST_BLOCK_BYTES));
                st->frames++;
        }
}

// The blocks the server's ring has that were not sent yet, sent: each with the one before it, urgent and flushed alone.
static fn link_cast_send(struct link_session address_to s, link_cast_stream address_to st, p8 key)
{
        p8 frame[LINK_CAST_HEAD + 2 * LINK_CAST_BLOCK_BYTES];
        struct cast_sound_slot slot;
        p64 written;
        p64 eight = 0;

        if (!st->wake_quiet)
        {
                while (system_read_once(st->wake, address_of eight, 8) == 8)
                        ;
                st->wake_quiet = true;
        }
        written = __atomic_load_n(&st->out->written, __ATOMIC_ACQUIRE);
        if (!st->primed || written > st->next + LINK_CAST_SENDS || written < st->next)
        {
                // The newest only: what is older is late already, and a ring begun again starts over.
                st->skipped += st->primed && written > st->next ? written - st->next - 1 : 0;
                st->next = written ? written - 1 : 0;
                st->primed = true;
        }
        for (; st->next < written; st->next++)
        {
                p8 count = 1;

                if (!cast_sound_read(st->out, st->next, address_of slot))
                        continue;
                frame[0] = 1;
                memory_store_unaligned(p32, frame + 2, (p32)st->next);
                memory_store_unaligned(p64, frame + 6, slot.stamp);
                memory_copy(frame + LINK_CAST_HEAD, slot.pcm, LINK_CAST_BLOCK_BYTES);
                if (st->previous_held && st->previous_block + 1 == st->next)
                {
                        memory_copy(frame + LINK_CAST_HEAD + LINK_CAST_BLOCK_BYTES, st->previous, LINK_CAST_BLOCK_BYTES);
                        count = 2;
                }
                frame[1] = count;
                memory_copy(st->previous, slot.pcm, LINK_CAST_BLOCK_BYTES);
                st->previous_block = st->next, st->previous_held = true;
                link_self.now = link_now();
                if (link_post(s, key, WATERLINK_FRAME_REPLACEABLE | WATERLINK_FRAME_URGENT, 'B', frame,
                              LINK_CAST_HEAD + (positive)count * LINK_CAST_BLOCK_BYTES))
                {
                        st->sent++;
                        link_session_flush(s, link_self.now);
                }
        }
}

// The server wants a row (on), or not: what the far machine is asked, or what this side sends of its own.
static fn link_cast_wanted(link_cast_message address_to m, bipolar ring_fd, bipolar wake_fd)
{
        link_peers peers;
        struct waterlink_peer address_to peer;
        struct link_session address_to s;
        link_cast_stream address_to st;
        p8 sends = link_cast_sends(m->heard, m->kind);

        link_peers_load(address_of peers);
        peer = link_peer_named(address_of peers, (string_address)m->machine);
        s = peer && !(peer->flags & WATERLINK_PEER_SHUT) ? link_cast_session_for(peer->key, m->heard, m->kind) : null;
        if (!s)
        {
                if (ring_fd >= 0)
                        system_close(ring_fd), system_close(wake_fd);
                //      Not up: the keeper dials it, and the row stays AWAY until it is.
                if (peer && !(peer->flags & WATERLINK_PEER_SHUT) && m->on)
                {
                        struct link_keep_entry address_to entry = link_keep_for(peer->key, true);

                        if (entry)
                                entry->linked = true, entry->next = 0;
                }
                return;
        }
        st = m->heard ? s->cast->give + m->kind : s->cast->ask + m->kind;
        if (sends && m->on)
        {
                if (ring_fd < 0 || !link_cast_take_out(st, ring_fd, wake_fd))
                        return;
        }
        else if (ring_fd >= 0)
                system_close(ring_fd), system_close(wake_fd);
        if (m->heard)
        {
                // A stream the far side asked of this one: the person's unwire is a pause, never a no.
                st->paused = !m->on;
                return;
        }
        if (!m->on)
        {
                if (st->state != LINK_CAST_IDLE)
                        link_cast_post_kind(s, 'C', m->kind);
                link_cast_stream_stop(st);
                link_cast_priority();
                return;
        }
        st->wanted = true;
        if (st->state == LINK_CAST_IDLE)
        {
                st->state = LINK_CAST_ASKED, st->asked_at = link_now();
                link_cast_post_kind(s, 'O', m->kind);
        }
}

// What the server said: whole records, each with at most two descriptors; anything else ends the connection.
static fn link_cast_read(void)
{
        for (positive rounds = 0; rounds < 64 && link_cast_server.fd >= 0; rounds++)
        {
                link_cast_message m;
                p8 control[128] __attribute__((aligned(8)));
                link_cast_iovec iov = {address_of m, sizeof m};
                link_cast_msghdr msg = {null, 0, 0, address_of iov, 1, control, sizeof control, 0, 0};
                bipolar fds[2] = {-1, -1};
                positive count = 0;
                p8 extra = false;
                bipolar got = system_call_3(syscall(recvmsg), (positive)link_cast_server.fd, (positive)address_of msg, 0x40 | 0x40000000);

                if (got == -EAGAIN || got == -EINTR)
                {
                        link_cast_server.quiet = true;
                        return;
                }
                for (positive at = 0; got > 0 && at + sizeof(link_cast_cmsghdr) <= msg.control_length;)
                {
                        link_cast_cmsghdr address_to c = (link_cast_cmsghdr address_to)(control + at);

                        if (c->length < sizeof(link_cast_cmsghdr) || at + c->length > msg.control_length)
                                break;
                        if (c->level == 1 && c->type == 1)
                                for (positive k = 0; k + 4 <= c->length - sizeof(link_cast_cmsghdr); k += 4)
                                {
                                        b32 fd;

                                        memory_copy(address_of fd, control + at + sizeof(link_cast_cmsghdr) + k, 4);
                                        if (count < 2)
                                                fds[count++] = fd;
                                        else
                                                system_close(fd), extra = true;
                                }
                        at += (c->length + 7) & ~(positive)7;
                }
                if (got != (bipolar)sizeof m || extra || (msg.flags & (0x20 | 0x8)) || m.op != 'W' || m.kind < LINK_CAST_SPEAKERS ||
                    m.kind > LINK_CAST_SOUND || m.heard > 1 || m.on > 1 || !memory_first_of(m.machine, 0, sizeof m.machine) ||
                    count != (m.on && link_cast_sends(m.heard, m.kind) ? 2u : 0u))
                {
                        for (positive k = 0; k < count; k++)
                                system_close(fds[k]);
                        system_close(link_cast_server.fd);
                        link_cast_server.fd = -1;
                        return;
                }
                link_cast_wanted(address_of m, count ? fds[0] : -1, count ? fds[1] : -1);
        }
}

/*
        The rows the server is told, from the peers file and what each session's far side said it grants: every machine's
        three, AWAY with no session up, DENIED when the far side said no, with the line. Only what changed is said; a row of
        a machine that went is said gone.
*/
static fn link_cast_rows(void)
{
        link_peers peers;
        p8 still[LINK_PEERS_MAX * 3];

        memory_zero(still, sizeof still);
        link_peers_load(address_of peers);
        for (positive at = 0; at < peers.count; at++)
        {
                struct waterlink_peer address_to peer = peers.peer + at;
                struct link_session address_to s = link_cast_session_of(peer->key);

                if (peer->flags & WATERLINK_PEER_SHUT)
                        continue;
                for (p8 kind = LINK_CAST_SPEAKERS; kind <= LINK_CAST_SOUND; kind++)
                {
                        p32 flags = !s || !s->cast->heard ? LINK_CAST_AWAY : (s->cast->grants & link_cast_bits[kind]) ? 0 : LINK_CAST_DENIED;
                        p8 allow[64];
                        positive slot = LINK_PEERS_MAX * 3, free = LINK_PEERS_MAX * 3;

                        allow[0] = 0;
                        if (flags & LINK_CAST_DENIED)
                                link_cast_allow_line(s->cast, kind, allow, sizeof allow);
                        for (positive k = 0; k < LINK_PEERS_MAX * 3; k++)
                        {
                                if (link_cast_server.said[k].used && link_cast_server.said[k].kind == kind &&
                                    string_equals((string_address)link_cast_server.said[k].machine, (string_address)peer->name))
                                        slot = k;
                                if (!link_cast_server.said[k].used && free == LINK_PEERS_MAX * 3)
                                        free = k;
                        }
                        if (slot == LINK_PEERS_MAX * 3)
                                slot = free;
                        if (slot == LINK_PEERS_MAX * 3)
                                continue;
                        still[slot] = true;
                        if (link_cast_server.said[slot].used && link_cast_server.said[slot].flags == flags &&
                            string_equals((string_address)link_cast_server.said[slot].allow, (string_address)allow))
                                continue;
                        link_cast_say('R', kind, 0, (string_address)peer->name, flags, (string_address)allow, -1);
                        link_cast_server.said[slot].used = true, link_cast_server.said[slot].kind = kind, link_cast_server.said[slot].flags = flags;
                        string_copy_bounded((string_address)link_cast_server.said[slot].machine, (string_address)peer->name, WATERLINK_NAME_MAX);
                        string_copy_bounded((string_address)link_cast_server.said[slot].allow, (string_address)allow, 64);
                }
        }
        for (positive k = 0; k < LINK_PEERS_MAX * 3; k++)
                if (link_cast_server.said[k].used && !still[k])
                {
                        link_cast_say('X', link_cast_server.said[k].kind, 0, (string_address)link_cast_server.said[k].machine, 0, null, -1);
                        link_cast_server.said[k].used = false;
                }
}

// The listener's turn: the server's socket connected (or tried again), what it said read, and the rows looked at.
static fn link_cast_turn(p64 now)
{
        if (link_cast_server.fd < 0 && link_age(now, link_cast_server.tried) >= LINK_CAST_RETRY)
        {
                struct { p16 family; p8 path[108]; } where;
                bipolar fd = system_call_3(syscall(socket), 1, 5 | 0x80000 | 0x800, 0); // AF_UNIX, SOCK_SEQPACKET, CLOEXEC, NONBLOCK

                link_cast_server.tried = now ? now : 1;
                memory_zero(address_of where, sizeof where);
                where.family = 1;
                string_copy_bounded((string_address)where.path, LINK_CAST_SOCKET, sizeof where.path);
                if (fd >= 0 && system_call_3(syscall(connect), (positive)fd, (positive)address_of where, sizeof where) == 0)
                {
                        link_cast_server.fd = fd, link_cast_server.quiet = false, link_cast_server.looked = 0;
                        memory_zero(link_cast_server.said, sizeof link_cast_server.said); // a new server knows nothing: every row again
                }
                else if (fd >= 0)
                        system_close(fd);
        }
        if (link_cast_server.fd >= 0 && !link_cast_server.quiet)
                link_cast_read();
        if (link_cast_server.fd >= 0 && (!link_cast_server.looked || link_age(now, link_cast_server.looked) >= LINK_CAST_LOOK))
        {
                link_cast_server.looked = now ? now : 1;
                link_cast_rows();
        }
}

static fn link_cast_watch(system_poll_descriptor address_to watch, p8 address_to address_to quiet, positive address_to count)
{
        link_watch(watch, quiet, count, link_cast_server.fd, SYSTEM_POLL_READ, address_of link_cast_server.quiet);
}

// A session's turn: its cast state made, its grants said when they changed, its clock asked, its streams sent, and a
// grant that went ending what it gave.
static fn link_cast_session_turn(struct link_session address_to s, p64 now)
{
        struct link_cast address_to c;
        link_peers peers;
        struct waterlink_peer address_to peer;
        p8 live = false;

        if (!link_cast_session_ok(s) || (!s->cast && !s->dialed) || !(c = link_cast_state(s)))
                return;
        if (!c->looked || link_age(now, c->looked) >= LINK_CAST_LOOK)
        {
                p32 grants;
                p8 said[4 + WATERLINK_NAME_MAX];

                c->looked = now ? now : 1;
                peer = link_cast_peer(address_of peers, s->peer);
                grants = link_cast_grants_now(peer);
                if (grants != c->grants_said && peer)
                {
                        positive named = string_length((string_address)peer->name);

                        memory_store_unaligned(p32, said, grants);
                        memory_copy(said + 4, peer->name, named);
                        link_cast_post(s, 'G', said, 4 + named);
                        c->grants_said = grants;
                }
                for (p8 kind = LINK_CAST_SPEAKERS; kind <= LINK_CAST_SOUND; kind++)
                        if (c->give[kind].state != LINK_CAST_IDLE && !(grants & link_cast_bits[kind]))
                        {
                                link_cast_post_kind(s, 'E', kind);
                                link_cast_end(s, true, kind, peer ? (string_address)peer->name : (string_address)s->name);
                        }
        }
        for (p8 kind = LINK_CAST_SPEAKERS; kind <= LINK_CAST_SOUND; kind++)
        {
                link_cast_stream address_to ask = c->ask + kind, address_to give = c->give + kind;

                live |= ask->state != LINK_CAST_IDLE || give->state != LINK_CAST_IDLE;
                if (ask->wanted && ask->state == LINK_CAST_IDLE && link_age(now, ask->asked_at) >= LINK_CAST_ASK_AGAIN &&
                    !(c->heard && !(c->grants & link_cast_bits[kind])))
                {
                        ask->state = LINK_CAST_ASKED, ask->asked_at = now;
                        link_cast_post_kind(s, 'O', kind);
                }
                if (link_cast_sends(false, kind) && ask->state == LINK_CAST_LIVE && ask->out && ask->wake >= 0)
                        link_cast_send(s, ask, link_cast_keys[kind]);
                if (link_cast_sends(true, kind) && give->state == LINK_CAST_LIVE && !give->paused && give->out && give->wake >= 0)
                        link_cast_send(s, give, link_cast_keys[kind]);
        }
        if (live && link_age(now, c->pinged) >= LINK_CAST_PING)
        {
                p8 asked[8];

                c->pinged = now;
                memory_store_unaligned(p64, asked, clock_monotonic_nanoseconds());
                link_cast_post(s, 'P', asked, sizeof asked);
        }
}

static fn link_cast_session_watch(struct link_session address_to s, system_poll_descriptor address_to watch, p8 address_to address_to quiet,
                                  positive address_to count)
{
        if (!s->cast)
                return;
        for (positive kind = 1; kind < LINK_CAST_KINDS; kind++)
        {
                link_cast_stream address_to ask = s->cast->ask + kind, address_to give = s->cast->give + kind;

                if (ask->state == LINK_CAST_LIVE && ask->wake >= 0)
                        link_watch(watch, quiet, count, ask->wake, SYSTEM_POLL_READ, address_of ask->wake_quiet);
                if (give->state == LINK_CAST_LIVE && give->wake >= 0)
                        link_watch(watch, quiet, count, give->wake, SYSTEM_POLL_READ, address_of give->wake_quiet);
        }
}

/*      When the session's channels next want a turn: the clock's next ask, while a stream is asked or given (the turn only
        asks then). A session with cast state and no stream wants none: its pinged is 0, and a due of 0 + a second was a time
        long past, so the listener's wait was zero and it spun a core (cloudless-lamp, 2026-11-25). */
static p64 link_cast_due(struct link_session address_to s, p64 due)
{
        p8 live = false;
        p64 next;

        if (!s->cast)
                return due;
        for (positive kind = 1; kind < LINK_CAST_KINDS; kind++)
                live |= s->cast->ask[kind].state != LINK_CAST_IDLE || s->cast->give[kind].state != LINK_CAST_IDLE;
        if (!live)
                return due;
        next = s->cast->pinged + LINK_CAST_PING;
        return next < due ? next : due;
}

// A session that ends: every stream of it ends, and the server is told the rows of the far machine again.
static fn link_cast_close(struct link_session address_to s)
{
        link_peers peers;
        struct waterlink_peer address_to peer;

        if (!s->cast)
                return;
        peer = link_cast_peer(address_of peers, s->peer);
        for (p8 kind = LINK_CAST_SPEAKERS; kind <= LINK_CAST_SOUND; kind++)
        {
                link_cast_stream_stop(s->cast->ask + kind);
                link_cast_end(s, true, kind, peer ? (string_address)peer->name : (string_address)s->name);
        }
        system_call_2(syscall(munmap), (positive)s->cast, sizeof(struct link_cast));
        s->cast = null;
        link_cast_server.looked = 0;
        link_cast_priority();
}

#else
//      A Mac's listener has no sound server of the engine's (CoreAudio would be its own backend): the channels are not
//      offered, and a frame on their keys is dropped.
static fn link_cast_hear(struct link_session address_to s, p8 key, p8 address_to payload, positive length)
{
        (void)s, (void)key, (void)payload, (void)length;
}
static fn link_cast_turn(p64 now)
{
        (void)now;
}
static fn link_cast_watch(system_poll_descriptor address_to watch, p8 address_to address_to quiet, positive address_to count)
{
        (void)watch, (void)quiet, (void)count;
}
static fn link_cast_session_turn(struct link_session address_to s, p64 now)
{
        (void)s, (void)now;
}
static fn link_cast_session_watch(struct link_session address_to s, system_poll_descriptor address_to watch, p8 address_to address_to quiet,
                                  positive address_to count)
{
        (void)s, (void)watch, (void)quiet, (void)count;
}
static p64 link_cast_due(struct link_session address_to s, p64 due)
{
        (void)s;
        return due;
}
static fn link_cast_close(struct link_session address_to s)
{
        (void)s;
}
#endif

#endif // WATERLINK_CAST_INCLUDED
