/*
        The capture engine: Moonwater's one way to share what is on the screen and what is heard, from the sources that make
        them to the sinks that carry them away.

        Sources: the compositor makes the video (a screen as it is shown, a window, a Scene composed apart), at its presents and
        stamped on its present clock (CLOCK_MONOTONIC nanoseconds); the sound server makes the audio. Sinks: the session bus's
        PipeWire (src/cast/pipewire.c), which a program reaches only through the ScreenCast portal (src/dbus/screencast.c) and
        the person's pick in the share app; and any other program of the desktop's that reads a ring the same way.

        The registry is the window table's casts (window.c's struct windows_cast): a row a share, from the sink's ask to the
        person's pick to the compositor's ring and its end. A ring is a memfd of the compositor's of `slots` frames, sealed
        before any sink sees it so that nothing but the compositor can ever write a frame; a sink opens it read-only through
        the compositor's /proc (it is root's, as the table is) and hands that to its consumer. Its slots go round under one rule,
        seqlock-like, with no lock either side waits on:

          - the compositor writes a slot that is neither held nor the newest: its seq made odd first, then held looked at again
            (a sink that took it in between wins, and the compositor goes to another), the frame composed, seq made even, the
            slot made the newest, the wake door rung (WINDOWS_CAST_WAKE);
          - a sink takes the newest: held first, then seq looked at again (odd, or moved, and the sink lets it go), and holds it
            while its consumer may read it, which the compositor never overwrites.

        Each published slot carries its frame's stamp and the damage since the frame before it (the whole frame when there were
        more rectangles than WINDOWS_CAST_DAMAGE).

        Included after window.c by the compositor and by the session bus.
*/
#ifndef CAST_INCLUDED
#define CAST_INCLUDED

#define WINDOWS_CAST_WAKE "/run/moonwater/cast.wake" // a FIFO of the session bus's: a byte for each frame the compositor publishes

// The compositor: the slot to write the next frame into, its seq made odd; -1 when every slot is held or the newest.
static inline int cast_write_begin(struct windows_cast *c)
{
        unsigned int latest = __atomic_load_n(&c->latest, __ATOMIC_ACQUIRE), slots = c->slots < WINDOWS_CAST_SLOTS ? c->slots : WINDOWS_CAST_SLOTS;

        for (unsigned int k = 0; k < slots; k++)
        {
                unsigned int seq;

                if (k == latest || __atomic_load_n(&c->held[k], __ATOMIC_SEQ_CST))
                        continue;
                seq = __atomic_load_n(&c->seq[k], __ATOMIC_RELAXED);
                __atomic_store_n(&c->seq[k], seq | 1u, __ATOMIC_SEQ_CST);
                if (!__atomic_load_n(&c->held[k], __ATOMIC_SEQ_CST))
                        return (int)k;
                __atomic_store_n(&c->seq[k], seq, __ATOMIC_SEQ_CST);
        }
        return -1;
}

// The compositor: slot k written, stamped and published as the newest, with its damage (x, y, w, h in the frame; none, the whole).
static inline void cast_write_end(struct windows_cast *c, unsigned int k, unsigned long long stamp, const int (*damage)[4], unsigned int count)
{
        unsigned int n = count <= WINDOWS_CAST_DAMAGE ? count : 0;

        for (unsigned int r = 0; r < n; r++)
                for (int side = 0; side < 4; side++)
                        c->damage[k][r][side] = damage[r][side];
        c->damages[k] = n;
        c->stamp[k] = stamp;
        __atomic_store_n(&c->seq[k], (__atomic_load_n(&c->seq[k], __ATOMIC_RELAXED) | 1u) + 1u, __ATOMIC_RELEASE);
        __atomic_store_n(&c->latest, k, __ATOMIC_RELEASE);
        __atomic_add_fetch(&c->frames, 1u, __ATOMIC_RELEASE);
}

// A sink: the newest slot taken and held (*seq its seq then), or -1 when there is none yet or it is being written.
static inline int cast_take(struct windows_cast *c, unsigned int *seq_out)
{
        unsigned int k = __atomic_load_n(&c->latest, __ATOMIC_ACQUIRE), seq;

        if (k >= WINDOWS_CAST_SLOTS || k >= c->slots)
                return -1;
        seq = __atomic_load_n(&c->seq[k], __ATOMIC_ACQUIRE);
        __atomic_add_fetch(&c->held[k], 1u, __ATOMIC_SEQ_CST);
        if ((seq & 1u) || !seq || __atomic_load_n(&c->seq[k], __ATOMIC_SEQ_CST) != seq)
        {
                __atomic_sub_fetch(&c->held[k], 1u, __ATOMIC_SEQ_CST);
                return -1;
        }
        *seq_out = seq;
        return (int)k;
}

// A sink: slot k let go.
static inline void cast_give(struct windows_cast *c, unsigned int k)
{
        if (k < WINDOWS_CAST_SLOTS && __atomic_load_n(&c->held[k], __ATOMIC_ACQUIRE))
                __atomic_sub_fetch(&c->held[k], 1u, __ATOMIC_SEQ_CST);
}

#endif /* CAST_INCLUDED */
