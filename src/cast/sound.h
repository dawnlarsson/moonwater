/*
        The capture engine's sound ring: blocks of sound between the sound server and the link, one writer and any readers, in
        a memfd the writer made and sealed (it cannot be shrunk, grown or written by anyone the writer handed it to: the
        writer's own mapping is the only writable one, F_SEAL_FUTURE_WRITE).

        A block is CAST_SOUND_FRAMES frames (2.5 ms at 48 kHz) of 16-bit stereo, the engine's one shape on the wire and in
        the rings, whatever rate or format the server's own card runs at. Each block is numbered by its writer from 0 and
        stamped on the clock of the machine whose ring it is (CLOCK_MONOTONIC nanoseconds): the server stamps the start of a
        block it made (when it was mixed, or when its microphone's period came in); the link stamps a block it received with
        the sender's stamp moved onto this machine's clock (the offset its clock asks found), and with the time it arrived.

        A block goes to slot (block % slots). The writer makes the slot's seq odd, writes the block, makes it even and moves
        `written` past it (the newest block + 1); a reader that wants block n reads seq, copies the slot, reads seq again, and
        has the block when the two are the same and even and the slot says n. A slot the writer is lapping, or one never
        written, is no block: the reader conceals it. Everything a reader reads is the writer's word and is held to the
        header the reader checked once (cast_sound_ring_good) and to the slot's own bounds.

        The ring is the interface: the server writes what it sends (an output of another machine's, or what another machine
        hears of this one) and reads what it receives; the link moves blocks between the rings and the network
        (src/waterlink/cast.c). It needs the library's memory_copy and memory_copy_apart, and nothing else before it.
*/
#ifndef CAST_SOUND_INCLUDED
#define CAST_SOUND_INCLUDED

#define CAST_SOUND_MAGIC 0x444e534du // "MSND"
#define CAST_SOUND_VERSION 1u
#define CAST_SOUND_RATE 48000u
#define CAST_SOUND_CHANNELS 2u
#define CAST_SOUND_FRAMES 120u                     // 2.5 ms at 48 kHz
#define CAST_SOUND_BLOCK_NS 2500000ull             // a block's length
#define CAST_SOUND_SLOTS 64u                       // 160 ms of blocks: past the jitter buffer's most (60 ms) and a lap of slack
#define CAST_SOUND_SAMPLES (CAST_SOUND_FRAMES * CAST_SOUND_CHANNELS)

#define CAST_SOUND_FILLED 1u // a block a sender sent (a block the link made up for one that never came is never written)

struct cast_sound_slot
{
        unsigned int seq;            // odd while it is written
        unsigned int flags;          // CAST_SOUND_FILLED
        unsigned long long block;    // the writer's number of the block
        unsigned long long stamp;    // the block's first frame, CLOCK_MONOTONIC ns on this machine
        unsigned long long arrived;  // when it arrived here (a received block), or was written (a sent one)
        short pcm[CAST_SOUND_SAMPLES];
};

struct cast_sound_ring
{
        unsigned int magic, version, bytes, slots;
        unsigned int frames, rate, channels, writer; // writer: its pid
        unsigned long long written;  // the newest block written + 1; 0 for none yet
        unsigned long long lost;     // received rings: blocks the link never had (not even in the next one's copy)
        unsigned long long late;     // received rings: blocks that came after their slot was lapped
        unsigned long long spare;
        struct cast_sound_slot slot[CAST_SOUND_SLOTS];
};

#define CAST_SOUND_RING_BYTES ((unsigned long)sizeof(struct cast_sound_ring))

_Static_assert(sizeof(struct cast_sound_slot) == 512, "a slot is 512 bytes: 32 of head and 480 of sound");
_Static_assert(sizeof(struct cast_sound_ring) % 64 == 0, "the ring is whole cache lines");

// The writer: the header of a ring just made (the memfd's zeros everywhere else).
static inline void cast_sound_ring_init(struct cast_sound_ring *r, unsigned int writer)
{
        r->magic = CAST_SOUND_MAGIC;
        r->version = CAST_SOUND_VERSION;
        r->bytes = (unsigned int)sizeof(*r);
        r->slots = CAST_SOUND_SLOTS;
        r->frames = CAST_SOUND_FRAMES;
        r->rate = CAST_SOUND_RATE;
        r->channels = CAST_SOUND_CHANNELS;
        r->writer = writer;
}

// A reader: a ring of this shape, as the writer said once (1), or not one to read (0).
static inline int cast_sound_ring_good(const struct cast_sound_ring *r, unsigned long mapped)
{
        return mapped >= sizeof(*r) && r->magic == CAST_SOUND_MAGIC && r->version == CAST_SOUND_VERSION && r->bytes == sizeof(*r) &&
               r->slots == CAST_SOUND_SLOTS && r->frames == CAST_SOUND_FRAMES && r->rate == CAST_SOUND_RATE && r->channels == CAST_SOUND_CHANNELS;
}

// The writer: block `block` into its slot, stamped, and published (written moves past it when it is the newest).
static inline void cast_sound_write(struct cast_sound_ring *r, unsigned long long block, unsigned long long stamp, unsigned long long arrived,
                                    const short *pcm)
{
        struct cast_sound_slot *s = &r->slot[block % CAST_SOUND_SLOTS];
        unsigned int seq = __atomic_load_n(&s->seq, __ATOMIC_RELAXED);

        __atomic_store_n(&s->seq, seq | 1u, __ATOMIC_RELAXED);
        __atomic_thread_fence(__ATOMIC_RELEASE);
        s->block = block, s->stamp = stamp, s->arrived = arrived, s->flags = CAST_SOUND_FILLED;
        memory_copy(s->pcm, pcm, sizeof(s->pcm));
        __atomic_store_n(&s->seq, (seq | 1u) + 1u, __ATOMIC_RELEASE);
        if (block + 1 > __atomic_load_n(&r->written, __ATOMIC_RELAXED))
                __atomic_store_n(&r->written, block + 1, __ATOMIC_RELEASE);
}

// The writer: whether block `block` is in its slot already (a received block's second copy is not written twice).
static inline int cast_sound_has(const struct cast_sound_ring *r, unsigned long long block)
{
        const struct cast_sound_slot *s = &r->slot[block % CAST_SOUND_SLOTS];

        return !(__atomic_load_n(&s->seq, __ATOMIC_ACQUIRE) & 1u) && s->flags == CAST_SOUND_FILLED && s->block == block;
}

// A reader: block `block` copied into *out (1), or it is not there: never written, lapped, or being written (0).
static inline int cast_sound_read(const struct cast_sound_ring *r, unsigned long long block, struct cast_sound_slot *out)
{
        const struct cast_sound_slot *s = &r->slot[block % CAST_SOUND_SLOTS];
        unsigned int before = __atomic_load_n(&s->seq, __ATOMIC_ACQUIRE), after;

        if (before & 1u)
                return 0;
        memory_copy_apart(out, s, sizeof(*s));
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
        after = __atomic_load_n(&s->seq, __ATOMIC_ACQUIRE);
        return before == after && out->flags == CAST_SOUND_FILLED && out->block == block;
}

#endif /* CAST_SOUND_INCLUDED */
