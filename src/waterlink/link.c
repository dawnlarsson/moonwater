/*
        Waterlink's core, as a transform.

        Frames in, datagram bodies out, and the same in reverse. Nothing here
        opens a socket, reads a clock, takes memory or touches a key: the
        caller passes the time it already read and the buffer it already owns,
        and gets bytes back. That is not tidiness. It is what lets the same
        code be the kernel's datapath on Moonwater and a userspace socket loop
        on a machine that is not Moonwater, and it is what lets the scheduler,
        the retransmission and the replay window be tested to the end without
        a network.

        The rules this file implements are stated in waterlink.c and are not
        restated here. What is here is how they are kept.

        Time is microseconds, from whatever clock the caller keeps, as long as
        it never goes backwards. Deadlines on the wire stay milliseconds.

        Dawn Larsson - Apache-2.0 license
        github.com/dawnlarsson/moonwater
*/

#ifndef WATERLINK_LINK_INCLUDED
#define WATERLINK_LINK_INCLUDED

#include "waterlink.c"

// The largest frame that can share a datagram with nothing else.
#define WATERLINK_FRAME_MAX (WATERLINK_PAYLOAD - WATERLINK_HEADER_MOST)

/*
        Ceilings, not guesses at a working set. A fixed count is what buys no
        allocation failure on the datapath and a cost that does not depend on
        what the far side does. If one is ever too low, raise it here -- that
        is the whole intent of naming it.

        SLOTS is what may be queued or unacknowledged at once, so it is also
        the most a link can have in flight: 256 frames of 1149 bytes is 294 KB
        a round trip, 14 MB/s at 20 ms and far past a gigabyte a second over a
        loopback's tens of microseconds. A key may run KEY_WINDOW frames past
        its oldest one not yet taken, and HELD is what a receiver keeps for
        keys that are ahead of what was taken -- out of order, or waiting for
        the application -- so it holds two streams running full at once, which
        is as many as the service ever receives on.
*/
#define WATERLINK_SLOTS 256
#define WATERLINK_HELD 128
#define WATERLINK_KEY_WINDOW 64

#define WATERLINK_NONE 0xffffffffu

/*
        Two queues, because urgency is a band and not a sort. Within a band
        post order is already the order a receiver will want, so a band is a
        list, a frame joins the end of its band's list, and supersession
        replaces a frame where it stands rather than moving it.
*/
#define WATERLINK_BAND_URGENT 0
#define WATERLINK_BAND_NORMAL 1
#define WATERLINK_BANDS 2

#define WATERLINK_SLOT_FREE 0
#define WATERLINK_SLOT_QUEUED 1 // in its band, waiting to be sent
#define WATERLINK_SLOT_FLIGHT 2 // sent, waiting for its acknowledgement
#define WATERLINK_SLOT_HELD 3   // arrived, and the far side holds it

struct waterlink_slot {
        p64 sent;   // the last transmission
        p64 serial; // the last transmission's place in the link's order
        p32 sequence;
        p32 next;  // the next slot in its band or in the flight
        p32 prior; // the one before it in the flight
        p32 chain; // the next slot on the same key, by sequence
        p16 length;
        p8 key;
        p8 flags;
        p8 state;
        p8 tries;
        p8 payload[WATERLINK_FRAME_MAX];
        p8 probed; // times asked about since the far side began to hold it
};

/*
        One key, each way. A key is a number below WATERLINK_KEYS, so these
        are tables indexed by it: no hashing, and nothing to evict.

        Sending: the next sequence, the class its first frame gave it, and
        the chain of its slots from oldest to newest -- the newest is the only
        one supersession ever asks about.

        Receiving: how far the key has been taken, the frames held back for
        it, and whether the application said "not now".
*/
struct waterlink_sending {
        p32 sequence;
        p32 first;
        p32 last;
        p16 flying;
        p8 class;   // WATERLINK_FRAME_REPLACEABLE or _DURABLE, 0 until posted
        p8 closing; // its last frame is posted
};

struct waterlink_receiving {
        p32 delivered;
        p32 first;
        p8 over;   // its last frame was taken
        p8 paused; // the application would not take the next one
};

// A frame that arrived ahead of what was taken, kept until it can be.
struct waterlink_held {
        struct waterlink_frame head;
        p32 next;
        p8 payload[WATERLINK_FRAME_MAX];
};

/*
        The replay window: the highest counter seen, and a bitmap of which of
        the WATERLINK_REPLAY_WINDOW counters below it have already arrived.
        A datagram above the top slides the window; one below it is checked
        and marked; one below the bottom is too old to judge and is refused.
*/
struct waterlink_replay {
        p64 top;
        p64 seen[WATERLINK_REPLAY_WINDOW / 64 + 1];
};

_Static_assert(WATERLINK_KEYS <= 64, "the keys owing an acknowledgement are one word");

struct waterlink_link {
        struct waterlink_slot slot[WATERLINK_SLOTS];
        struct waterlink_held held[WATERLINK_HELD];
        struct waterlink_sending sending[WATERLINK_KEYS];
        struct waterlink_receiving receiving[WATERLINK_KEYS];
        struct waterlink_replay replay;
        p64 acking; // the keys owed an acknowledgement, a bit each

        p32 head[WATERLINK_BANDS];
        p32 tail[WATERLINK_BANDS];
        p32 requeue[WATERLINK_BANDS]; // where the next lost frame goes back in
        p32 free;
        p32 free_count;
        p32 held_free;
        p32 flight_head;
        p32 flight_tail;

        /*      How much the path carries, as far as this side can tell. The
                window and what is in flight are frame bytes; an estimate of
                the round trip in microseconds, zero until the first sample. */
        p64 clock;
        p64 serial;        // transmissions so far
        p64 largest;       // the latest transmission known to have arrived
        p64 recovery;      // transmissions before this were sent pre-loss
        p64 in_flight;
        p64 window;
        p64 threshold;
        p64 smoothed;
        p64 variance;
        p64 pace;          // when the next paced datagram may leave
        p64 recent;        // the last round trip sampled
        p64 owed;          // when the oldest unacknowledged arrival came
        p32 owed_count;    // datagrams with frames since the last ack
        p8 owed_now;       // an arrival that should be answered at once
        p32 backoff;
        p32 probes;        // datagrams the probe timer lets past the window

        // What the caller may want to know without instrumenting the caller.
        p64 posted;
        p64 superseded;
        p64 refused;
        p64 delivered;
        p64 stale;
        p64 sent;
        p64 retransmitted;
        p64 lost;
        p64 timeouts;
        p64 acked;
        p64 kept;    // frames held back
        p64 spilled; // frames dropped because the hold-back was full

        p64 looked;  // when held keys are next looked over

        /*      The normal band's frames by key, and a bit for every key it
                has any of (and perhaps for some it no longer has): a walk
                past streams whose window is out stops once no other key
                is left in the band. */
        p64 banded;
        p16 queued[WATERLINK_KEYS];
};

/*
        How the path is estimated. Not part of the contract, which only says a
        sender must not put more in flight than the path carries: this is
        additive increase and halving on loss, a window that starts at ten
        datagrams and doubles per round trip until the first loss, and a
        pacer that spreads a window over a round trip at a quarter faster
        than the window, so a run of segments leaves as a run and not as the
        whole window at once. The retransmission timer is the usual smoothed
        round trip plus four deviations, doubled on each expiry.
*/
#define WATERLINK_WINDOW_FIRST (10 * WATERLINK_DATAGRAM)
#define WATERLINK_WINDOW_LEAST (2 * WATERLINK_DATAGRAM)
#define WATERLINK_WINDOW_MOST ((p64)WATERLINK_SLOTS * WATERLINK_DATAGRAM)
#define WATERLINK_REORDER 3             // transmissions after, then lost
#define WATERLINK_RTT_FIRST 100000ull   // before any sample
#define WATERLINK_GRANULE 1000ull       // the least a timer means
#define WATERLINK_RTO_MOST 2000000ull
#define WATERLINK_PERSISTENT 3          // probe expiries, then the path is gone
#define WATERLINK_HELD_MOST 60000000ull // the longest a held frame waits to be asked about

/*      Acknowledgements are owed every second datagram that carried frames,
        and never later than this: at once when a frame arrived out of
        order, was a copy, was urgent or ended its key, since each of those
        is something the sender is waiting to hear. Otherwise an
        acknowledgement rides free in whatever datagram goes the other way
        first. */
#define WATERLINK_ACK_EVERY 2
#define WATERLINK_ACK_DELAY 1000ull
#define WATERLINK_PACE_BURST 16         // datagrams the pacer lets go at once

fn waterlink_link_reset(struct waterlink_link address_to link)
{
        memory_zero(link, sizeof(address_to link));

        for (p32 at = 0; at < WATERLINK_SLOTS; at++)
                link->slot[at].next = at + 1 < WATERLINK_SLOTS
                                              ? at + 1
                                              : WATERLINK_NONE;
        for (p32 at = 0; at < WATERLINK_HELD; at++)
                link->held[at].next = at + 1 < WATERLINK_HELD
                                              ? at + 1
                                              : WATERLINK_NONE;
        for (p32 key = 0; key < WATERLINK_KEYS; key++)
        {
                link->sending[key].sequence = 1;
                link->sending[key].first = WATERLINK_NONE;
                link->sending[key].last = WATERLINK_NONE;
                link->receiving[key].first = WATERLINK_NONE;
        }
        for (p32 band = 0; band < WATERLINK_BANDS; band++)
        {
                link->head[band] = WATERLINK_NONE;
                link->tail[band] = WATERLINK_NONE;
                link->requeue[band] = WATERLINK_NONE;
        }

        link->free_count = WATERLINK_SLOTS;
        link->flight_head = WATERLINK_NONE;
        link->flight_tail = WATERLINK_NONE;
        link->window = WATERLINK_WINDOW_FIRST;
        link->threshold = ~0ull;
}

static p32 waterlink_band_of(p8 flags)
{
        return flags & WATERLINK_FRAME_URGENT ? WATERLINK_BAND_URGENT
                                              : WATERLINK_BAND_NORMAL;
}

// What a slot's frame costs the window.
static p64 waterlink_bytes(struct waterlink_slot address_to slot)
{
        return 2 + memory_vli_size(slot->sequence) +
               memory_vli_size(slot->length) + slot->length;
}

/*
        Whether a frame's flags are ones this link will carry at all: one
        class, since a frame that is both replaceable and durable has no
        answer to "may this be dropped", and no bit it does not know. The
        acknowledgement flag is the link's own and no caller may set it.
*/
static bool waterlink_frame_sane(p8 flags)
{
        bool replaceable = (flags & WATERLINK_FRAME_REPLACEABLE) != 0;
        bool durable = (flags & WATERLINK_FRAME_DURABLE) != 0;

        return replaceable != durable && !(flags & ~WATERLINK_FRAME_WIRE);
}

// The flight is a list in the order things were sent.
static fn waterlink_flight_remove(struct waterlink_link address_to link,
                                  p32 at)
{
        struct waterlink_slot address_to slot = link->slot + at;

        if (slot->prior == WATERLINK_NONE)
                link->flight_head = slot->next;
        else
                link->slot[slot->prior].next = slot->next;
        if (slot->next == WATERLINK_NONE)
                link->flight_tail = slot->prior;
        else
                link->slot[slot->next].prior = slot->prior;

        link->in_flight -= waterlink_bytes(slot);
        link->sending[slot->key].flying--;
        slot->next = WATERLINK_NONE;
        slot->prior = WATERLINK_NONE;
}

/*      A frame joins or leaves the normal band. Everything a stream key
        has there past a frame its window holds back is held back as well --
        what went out and came back was inside the window and still is, and
        what follows it was posted later -- so a walk that finds a stream
        blocked need not look at another of its frames. Not a register's: a
        value replaced where it stands takes the newest sequence, and may
        stand ahead of an older frame of its key that the window lets by.

        A key's bit in banded stays set after its last frame leaves -- fill
        would otherwise pay for clearing it on every frame it takes -- and
        is cleared when a walk asks whether it can end. */
static fn waterlink_band_joined(struct waterlink_link address_to link, p32 band,
                                p8 key)
{
        if (band == WATERLINK_BAND_NORMAL)
        {
                link->queued[key]++;
                link->banded |= 1ull << key;
        }
}

static fn waterlink_band_left(struct waterlink_link address_to link, p32 band,
                              p32 at)
{
        if (band == WATERLINK_BAND_NORMAL)
                link->queued[link->slot[at].key]--;
}

//      Whether a walk that found these streams blocked is over: no other
//      key has a frame left in the normal band.
static bool waterlink_band_done(struct waterlink_link address_to link,
                                p64 blocked)
{
        for (p64 left = link->banded & ~blocked; left; left &= left - 1)
        {
                p8 key = (p8)bits_trailing_zeros(left);

                if (link->queued[key])
                        return false;
                link->banded &= ~(1ull << key);
        }
        return true;
}

static fn waterlink_band_append(struct waterlink_link address_to link, p32 at)
{
        p32 band = waterlink_band_of(link->slot[at].flags);

        link->slot[at].state = WATERLINK_SLOT_QUEUED;
        link->slot[at].next = WATERLINK_NONE;
        if (link->tail[band] == WATERLINK_NONE)
                link->head[band] = at;
        else
                link->slot[link->tail[band]].next = at;
        link->tail[band] = at;
        waterlink_band_joined(link, band, link->slot[at].key);
}

// Unlink one slot from its band, given the slot before it or NONE.
static fn waterlink_band_unlink(struct waterlink_link address_to link,
                                p32 band, p32 at, p32 prior)
{
        p32 next = link->slot[at].next;

        if (prior == WATERLINK_NONE)
                link->head[band] = next;
        else
                link->slot[prior].next = next;
        if (link->tail[band] == at)
                link->tail[band] = prior;
        if (link->requeue[band] == at)
                link->requeue[band] = prior;
        link->slot[at].next = WATERLINK_NONE;
        waterlink_band_left(link, band, at);
}

static KEEP fn waterlink_band_remove(struct waterlink_link address_to link, p32 at)
{
        p32 band = waterlink_band_of(link->slot[at].flags);
        p32 prior = WATERLINK_NONE;

        for (p32 look = link->head[band]; look != WATERLINK_NONE;
             look = link->slot[look].next)
        {
                if (look == at)
                {
                        waterlink_band_unlink(link, band, at, prior);
                        return;
                }
                prior = look;
        }
}

// Off the band or the flight, whichever it is on.
static fn waterlink_slot_unqueue(struct waterlink_link address_to link, p32 at)
{
        if (link->slot[at].state == WATERLINK_SLOT_FLIGHT)
                waterlink_flight_remove(link, at);
        else if (link->slot[at].state == WATERLINK_SLOT_QUEUED)
                waterlink_band_remove(link, at);
}

/*
        A lost frame goes back ahead of everything posted since and behind
        every frame lost before it that has not gone out again yet: the front
        of a band is a queue of retransmissions in the order they were found.
        Putting the newest loss first instead starves the oldest -- which is
        the one the far side is holding everything else back for.

        Except the oldest itself: a key's first frame not yet taken goes in
        front of every other retransmission. The far side's hold-back is one
        pool for every key, so frames later on a key can fill it, be refused,
        time out and come back first forever, with the window full of them
        and the one frame that would empty the pool never reaching the front
        -- a bulk transfer on a clean path stalled six seconds that way.
*/
static fn waterlink_band_requeue(struct waterlink_link address_to link, p32 at)
{
        p32 band = waterlink_band_of(link->slot[at].flags);
        p32 after = link->requeue[band];

        link->slot[at].state = WATERLINK_SLOT_QUEUED;
        waterlink_band_joined(link, band, link->slot[at].key);
        if (link->sending[link->slot[at].key].first == at)
        {
                link->slot[at].next = link->head[band];
                link->head[band] = at;
                if (link->tail[band] == WATERLINK_NONE)
                        link->tail[band] = at;
                if (after == WATERLINK_NONE)
                        link->requeue[band] = at;
                return;
        }
        if (after == WATERLINK_NONE)
        {
                link->slot[at].next = link->head[band];
                link->head[band] = at;
                if (link->tail[band] == WATERLINK_NONE)
                        link->tail[band] = at;
        }
        else
        {
                link->slot[at].next = link->slot[after].next;
                link->slot[after].next = at;
                if (link->tail[band] == after)
                        link->tail[band] = at;
        }
        link->requeue[band] = at;
}

// A slot leaves its key's chain, already off the band and the flight.
static KEEP fn waterlink_slot_free(struct waterlink_link address_to link, p32 at)
{
        struct waterlink_slot address_to slot = link->slot + at;
        struct waterlink_sending address_to live = link->sending + slot->key;
        p32 prior = WATERLINK_NONE;

        for (p32 look = live->first; look != WATERLINK_NONE;
             look = link->slot[look].chain)
        {
                if (look == at)
                {
                        if (prior == WATERLINK_NONE)
                                live->first = slot->chain;
                        else
                                link->slot[prior].chain = slot->chain;
                        if (live->last == at)
                                live->last = prior;
                        break;
                }
                prior = look;
        }

        slot->state = WATERLINK_SLOT_FREE;
        slot->chain = WATERLINK_NONE;
        slot->next = link->free;
        link->free = at;
        link->free_count++;
}

/*
        Queue a frame for sending.

        A key's class is the class of its first frame, and a frame of the
        other class on it is refused: a stream key carries every frame once
        and in order, a register key only its newest value. That one rule is
        what lets a frame name no other frame. The supersession rule lives
        here: a frame on a register key whose newest frame is still in the
        same band takes that frame's slot under the next sequence -- replaced
        where it stands if queued, sent again in its place if it had already
        gone. The far side takes any value newer than the one it has.

        Returns false and counts a refusal when the frame is malformed, the
        queue is full, the key ran out of sequence numbers, or the key's last
        frame was already posted. A full queue is the caller's signal to stop
        producing, not this file's to start choosing.
*/
bool waterlink_post(struct waterlink_link address_to link, p8 key, p8 flags,
                    address_any payload, p16 length, p64 now)
{
        struct waterlink_sending address_to live;
        p8 class = flags & (WATERLINK_FRAME_REPLACEABLE | WATERLINK_FRAME_DURABLE);
        struct waterlink_slot address_to slot;
        bool queued;
        p32 at;

        /*      Form no pointer from an application key until its bound is
                proved.  Merely computing sending + key outside the array is
                undefined in C, even when the short-circuit below keeps it
                from being dereferenced. */
        if (key >= WATERLINK_KEYS || !waterlink_frame_sane(flags) ||
            length > WATERLINK_FRAME_MAX)
        {
                link->refused++;
                return false;
        }
        live = link->sending + key;
        if ((live->class && live->class != class) || live->closing ||
            live->sequence == WATERLINK_NONE)
        {
                link->refused++;
                return false;
        }
        if (now > link->clock)
                link->clock = now;
        live->class = class;

        at = live->last;
        if (class == WATERLINK_FRAME_REPLACEABLE && at != WATERLINK_NONE &&
            waterlink_band_of(link->slot[at].flags) == waterlink_band_of(flags))
                link->superseded++;
        else
        {
                at = link->free;
                if (at == WATERLINK_NONE)
                {
                        link->refused++;
                        return false;
                }
                link->free = link->slot[at].next;
                link->free_count--;
                link->slot[at].state = WATERLINK_SLOT_FREE;
                link->slot[at].chain = WATERLINK_NONE;
                if (live->last == WATERLINK_NONE)
                        live->first = at;
                else
                        link->slot[live->last].chain = at;
                live->last = at;
                link->posted++;
        }

        slot = link->slot + at;
        queued = slot->state == WATERLINK_SLOT_QUEUED;
        if (!queued)
                waterlink_slot_unqueue(link, at);
        slot->key = key;
        slot->sequence = live->sequence++;
        slot->sent = 0;
        slot->serial = 0;
        slot->prior = WATERLINK_NONE;
        slot->tries = 0;
        slot->probed = 0;
        slot->length = length;
        slot->flags = flags;
        //      Queued before the copy, while its key and flags are in hand.
        if (!queued)
                waterlink_band_append(link, at);
        if (length)
                memory_copy(slot->payload, payload, length);
        if (flags & WATERLINK_FRAME_LAST)
                live->closing = 1;
        return true;
}

// Slots free now, which is how much a caller may read before it posts.
p32 waterlink_room(struct waterlink_link address_to link)
{
        return link->free_count;
}

/*
        The probe timer, from the estimate: before any sample, the first
        guess; after, the smoothed round trip, four deviations (a millisecond
        at least) and the longest the far side may hold an acknowledgement,
        doubled for each expiry since the last acknowledgement. Leave the
        acknowledgement delay out and a sender that stops -- at the end of a
        burst, or waiting on the far side -- times out on every last datagram
        the far side is still entitled to sit on: 223 expiries in a 100 MB
        stream over loopback, at 71 MB/s.
*/
static p64 waterlink_timeout(struct waterlink_link address_to link)
{
        p64 spread = 4 * link->variance;
        p64 base;
        p32 doubling = link->backoff < 10 ? link->backoff : 10;

        if (spread < WATERLINK_GRANULE)
                spread = WATERLINK_GRANULE;
        base = link->smoothed ? link->smoothed + spread + WATERLINK_ACK_DELAY
                              : 3 * WATERLINK_RTT_FIRST;
        base <<= doubling;
        return base < WATERLINK_RTO_MOST ? base : WATERLINK_RTO_MOST;
}

static fn waterlink_congested(struct waterlink_link address_to link)
{
        p64 half = link->window / 2;

        link->window = half > WATERLINK_WINDOW_LEAST ? half
                                                     : WATERLINK_WINDOW_LEAST;
        link->threshold = link->window;
        link->recovery = link->serial;
}

static fn waterlink_lose(struct waterlink_link address_to link, p32 at)
{
        waterlink_flight_remove(link, at);
        link->lost++;
        waterlink_band_requeue(link, at);
}

/*
        What is in flight and cannot have arrived: sent three transmissions
        or more before one that did, or sent before one that did and older
        than an eighth over the round trip. Lost frames go back to the front
        of their bands. One halving per round of loss, found by the
        transmission number: a loss of something sent before the last halving
        is the same congestion the halving already answered.

        When nothing sent later has arrived there is no evidence either way
        -- the last frames of a burst, or all of them -- and the probe timer
        answers that: it sends the oldest frame again, past the window, to
        make the far side say what it has. Only when it has expired three
        times with no answer is the path taken to be gone, everything in
        flight counted lost and the window brought to its least.
*/
static KEEP fn waterlink_losses(struct waterlink_link address_to link, p64 now)
{
        p64 trip = link->smoothed > link->recent ? link->smoothed
                                                 : link->recent;
        p64 threshold = trip + trip / 8;
        p32 at = link->flight_head;

        if (threshold < WATERLINK_GRANULE)
                threshold = WATERLINK_GRANULE;

        while (at != WATERLINK_NONE)
        {
                struct waterlink_slot address_to slot = link->slot + at;
                p32 next = slot->next;

                if (slot->serial >= link->largest)
                        break;
                if (slot->serial + WATERLINK_REORDER <= link->largest ||
                    (now >= slot->sent && now - slot->sent >= threshold))
                {
                        if (slot->serial > link->recovery)
                                waterlink_congested(link);
                        waterlink_lose(link, at);
                }
                at = next;
        }

        at = link->flight_head;
        if (at == WATERLINK_NONE ||
            now < link->slot[at].sent ||
            now - link->slot[at].sent < waterlink_timeout(link))
                return;

        link->timeouts++;
        if (link->backoff < 16)
                link->backoff++;

        if (link->backoff >= WATERLINK_PERSISTENT)
        {
                while (link->flight_head != WATERLINK_NONE)
                        waterlink_lose(link, link->flight_head);
                link->threshold = link->window / 2 > WATERLINK_WINDOW_LEAST
                                          ? link->window / 2
                                          : WATERLINK_WINDOW_LEAST;
                link->window = WATERLINK_WINDOW_LEAST;
                link->recovery = link->serial;
        }
        else
                waterlink_lose(link, at);

        link->probes = 2;
}

/*
        A key may run no more than its window ahead of its oldest frame the
        far side has not taken -- counting what it holds as well as what is
        on the path, since held frames are what fill its pool, and what an
        application that is not reading keeps held is how it slows the
        sender: the acknowledgement is the credit.
*/
static bool waterlink_key_blocked(struct waterlink_link address_to link,
                                  struct waterlink_slot address_to slot)
{
        p32 first = link->sending[slot->key].first;

        return first != WATERLINK_NONE &&
               slot->sequence - link->slot[first].sequence >=
                       WATERLINK_KEY_WINDOW;
}

/*
        The acknowledgement a receiver owes, as one frame per key: how far
        the key has been taken, and which of the sixty four sequences after
        that it holds. The first frees everything up to it at the sender. The
        second tells the sender those frames arrived and are only waiting --
        out of order, or for an application that said "not now" -- so they
        leave the flight and are not sent again, and what is left in flight
        on that key is what really went missing.
*/
#define WATERLINK_ACK_MASK 64

static fn waterlink_ack_owe(struct waterlink_link address_to link, p8 key)
{
        if (!link->acking)
                link->owed = link->clock;
        link->acking |= 1ull << key;
}

static bool waterlink_ack_due(struct waterlink_link address_to link, p64 now)
{
        return link->acking &&
               (link->owed_now || link->owed_count >= WATERLINK_ACK_EVERY ||
                (now >= link->owed &&
                 now - link->owed >= WATERLINK_ACK_DELAY));
}

/*
        Fill one datagram body: as many whole frames as the path allows and
        the body fits, then what acknowledgements are owed.

        Sets alone when the body holds an urgent frame. An urgent frame cannot
        wait in a segment run for the forty frames behind it -- a run is built
        by waiting, and waiting is the thing this link exists to refuse -- so
        the caller sends that datagram by itself and pays the unbatched price
        on purpose. For the same reason an urgent frame is not held for the
        window or the pacer: it is a few bytes, and the person typing it is
        the one who notices. Frames already queued still ride along behind
        it when the path allows; they were not going to leave sooner.

        Acknowledgements are not held by the window either: they are what
        opens it. Room is kept for a few, so a full run of frames still
        carries them.

        Returns the bytes written, or zero when there is nothing to send now.

        In each machine's registers: a slot's fields are read once, at the
        widths post wrote them, before anything is written, so no read waits
        on a store; a frame's size is spelled once and is what the window is
        charged; a frame's head is built in a register and stored whole; and
        a call that has no frame to send touches only the acknowledgements.
        Nothing is written past the bytes returned.
*/
positive waterlink_fill(struct waterlink_link address_to link,
                        address_any out, p64 now, bool address_to alone);

_Static_assert(sizeof(struct waterlink_slot) == 1200 &&
                       __builtin_offsetof(struct waterlink_slot, serial) == 8 &&
                       __builtin_offsetof(struct waterlink_slot, sequence) == 16 &&
                       __builtin_offsetof(struct waterlink_slot, next) == 20 &&
                       __builtin_offsetof(struct waterlink_slot, prior) == 24 &&
                       __builtin_offsetof(struct waterlink_slot, length) == 32 &&
                       __builtin_offsetof(struct waterlink_slot, key) == 34 &&
                       __builtin_offsetof(struct waterlink_slot, flags) == 35 &&
                       __builtin_offsetof(struct waterlink_slot, state) == 36 &&
                       __builtin_offsetof(struct waterlink_slot, tries) == 37 &&
                       __builtin_offsetof(struct waterlink_slot, payload) == 38 &&
                       __builtin_offsetof(struct waterlink_slot, probed) == 1197,
               "fill reads a slot at these places");
_Static_assert(sizeof(struct waterlink_held) == 1172 &&
                       __builtin_offsetof(struct waterlink_held, next) == 8 &&
                       sizeof(struct waterlink_sending) == 16 &&
                       __builtin_offsetof(struct waterlink_sending, first) == 4 &&
                       __builtin_offsetof(struct waterlink_sending, flying) == 12 &&
                       sizeof(struct waterlink_receiving) == 12 &&
                       __builtin_offsetof(struct waterlink_receiving, first) == 4,
               "fill reads the key tables at these places");
_Static_assert(__builtin_offsetof(struct waterlink_link, held) == 0x4b000 &&
                       __builtin_offsetof(struct waterlink_link, sending) == 0x6fa00 &&
                       __builtin_offsetof(struct waterlink_link, receiving) == 0x6fe00 &&
                       __builtin_offsetof(struct waterlink_link, acking) == 0x70210 &&
                       __builtin_offsetof(struct waterlink_link, head) == 0x70218 &&
                       __builtin_offsetof(struct waterlink_link, tail) == 0x70220 &&
                       __builtin_offsetof(struct waterlink_link, requeue) == 0x70228 &&
                       __builtin_offsetof(struct waterlink_link, flight_head) == 0x7023c &&
                       __builtin_offsetof(struct waterlink_link, flight_tail) == 0x70240 &&
                       __builtin_offsetof(struct waterlink_link, clock) == 0x70248 &&
                       __builtin_offsetof(struct waterlink_link, serial) == 0x70250 &&
                       __builtin_offsetof(struct waterlink_link, in_flight) == 0x70268 &&
                       __builtin_offsetof(struct waterlink_link, window) == 0x70270 &&
                       __builtin_offsetof(struct waterlink_link, smoothed) == 0x70280 &&
                       __builtin_offsetof(struct waterlink_link, pace) == 0x70290 &&
                       __builtin_offsetof(struct waterlink_link, owed) == 0x702a0 &&
                       __builtin_offsetof(struct waterlink_link, owed_count) == 0x702a8 &&
                       __builtin_offsetof(struct waterlink_link, owed_now) == 0x702ac &&
                       __builtin_offsetof(struct waterlink_link, probes) == 0x702b4 &&
                       __builtin_offsetof(struct waterlink_link, sent) == 0x702e0 &&
                       __builtin_offsetof(struct waterlink_link, retransmitted) == 0x702e8 &&
                       __builtin_offsetof(struct waterlink_link, banded) == 0x70320 &&
                       __builtin_offsetof(struct waterlink_link, queued) == 0x70328,
               "fill reads the link at these places");
_Static_assert(WATERLINK_PAYLOAD == 1168 && WATERLINK_ACK_MOST == 17 &&
                       WATERLINK_FRAME_MAX < 16384 && WATERLINK_BANDS == 2 &&
                       WATERLINK_KEY_WINDOW == 64 && WATERLINK_ACK_MASK == 64 &&
                       WATERLINK_ACK_EVERY == 2 && WATERLINK_ACK_DELAY == 1000 &&
                       WATERLINK_DATAGRAM * 4 == 4800 &&
                       WATERLINK_PACE_BURST == 16 && WATERLINK_SLOT_FLIGHT == 2,
               "fill's bounds are these");

#if X64
/*
        A number below 2^32 and over 127 in v, spelled seven bits a byte
        with the top bit saying more follows, and n its bytes. Each add moves
        the bits above a byte's seven up by one; the bytes that say more are
        the ones under the highest, a shift of 0x80808080. t and rcx go.
*/
#define WATERLINK_X64_SPELL(v, t, t32, n, n32)                                 \
    "mov %" v ", %" t "\n   and $-128, %" t "\n   add %" t ", %" v "\n"         \
    "mov %" v ", %" t "\n   and $-32768, %" t "\n   add %" t ", %" v "\n"       \
    "mov %" v ", %" t "\n   and $-8388608, %" t "\n   add %" t ", %" v "\n"     \
    "mov %" v ", %" t "\n   and $-2147483648, %" t "\n   add %" t ", %" v "\n"  \
    "bsr %" v ", %" n "\n   shr $3, %" n32 "\n"                                 \
    "lea 0(,%" n ",8), %ecx\n   neg %ecx\n   add $32, %ecx\n"                   \
    "mov $0x80808080, %" t32 "\n   shr %cl, %" t32 "\n   or %" t ", %" v "\n"   \
    "inc %" n32 "\n"

/*
        One frame out of a band: eax its slot, rsi the slot's address, r11d
        the slot before it in the band. edx the sequence and then the head,
        r8 the length, r9 the key, r10 the flags, rdi the sequence spelled,
        ecx its bytes, r14 the head's bytes, r15 the frame's. Everything the
        slot says is read before the body is written.
*/
#define WATERLINK_X64_FRAME(head, tail, requeue, full, again, carried, taken)  \
    "mov 16(%rsi), %edx\n   movzwl 32(%rsi), %r8d\n"                           \
    "movzbl 34(%rsi), %r9d\n   movzbl 35(%rsi), %r10d\n"                       \
    "mov %edx, %edi\n   mov $1, %ecx\n   cmp $127, %edx\n   ja 50f\n"          \
    "51: mov %r8d, %edx\n   lea 3(%rcx), %r14d\n   cmp $127, %r8d\n   ja 52f\n" \
    "53: lea 16(,%rcx,8), %ecx\n   shl %cl, %rdx\n   shl $16, %rdi\n"          \
    "or %rdi, %rdx\n   mov %r9d, %edi\n   shl $8, %edi\n   or %edi, %r10d\n"   \
    "or %r10, %rdx\n"                                                          \
    "lea (%r14,%r8), %r15\n   lea (%r13,%r15), %rdi\n   cmp 8(%rsp), %rdi\n"   \
    "ja 54f\n"                                                                 \
    "55: lea (%rbp,%r13), %rdi\n   cmp $8, %r15\n   jb 56f\n"                  \
    "mov %rdx, (%rdi)\n   cmp $9, %r14d\n   je 57f\n"                          \
    /*  Off the band, into the flight. */                                      \
    "58: mov 20(%rsi), %ecx\n   cmp $-1, %r11d\n   je 59f\n"                   \
    "imul $1200, %r11, %rdx\n   mov %ecx, 20(%rbx,%rdx)\n   jmp 60f\n"          \
    "59: mov %ecx, " head "(%rbx)\n"                                           \
    "60: cmp %eax, " tail "(%rbx)\n   jne 61f\n   mov %r11d, " tail "(%rbx)\n"  \
    "61: cmp %eax, " requeue "(%rbx)\n   jne 62f\n"                            \
    "mov %r11d, " requeue "(%rbx)\n"                                           \
    "62: mov 0x70250(%rbx), %rcx\n   inc %rcx\n   mov %rcx, 0x70250(%rbx)\n"   \
    "mov %rcx, 8(%rsi)\n   mov %r12, (%rsi)\n   movb $2, 36(%rsi)\n"           \
    "movzbl 37(%rsi), %ecx\n   lea 1(%rcx), %edx\n   mov %dl, 37(%rsi)\n"      \
    "test %ecx, %ecx\n   jnz 63f\n"                                            \
    "64: mov 0x70240(%rbx), %ecx\n   movl $-1, 20(%rsi)\n   mov %ecx, 24(%rsi)\n" \
    "cmp $-1, %ecx\n   je 65f\n"                                               \
    "imul $1200, %rcx, %rcx\n   mov %eax, 20(%rbx,%rcx)\n   jmp 66f\n"          \
    "65: mov %eax, 0x7023c(%rbx)\n"                                            \
    "66: mov %eax, 0x70240(%rbx)\n   add %r15, 0x70268(%rbx)\n" taken         \
    "shl $4, %r9d\n   incw 0x6fa0c(%rbx,%r9)\n   incq 0x702e0(%rbx)\n"         \
    /*  The payload, read in the pieces post wrote it in. */                   \
    "test %r8d, %r8d\n   jz 67f\n"                                             \
    "lea (%rbp,%r13), %rdi\n   add %r14, %rdi\n   add $38, %rsi\n"             \
    "cmp $4, %r8d\n   jae 68f\n   cmp $2, %r8d\n   jae 69f\n"                  \
    "movzbl (%rsi), %eax\n   mov %al, (%rdi)\n   jmp 67f\n"                    \
    "69: movzwl (%rsi), %eax\n   movzwl -2(%rsi,%r8), %ecx\n"                  \
    "mov %ax, (%rdi)\n   mov %cx, -2(%rdi,%r8)\n   jmp 67f\n"                  \
    "68: cmp $8, %r8d\n   jae 70f\n"                                           \
    "mov (%rsi), %eax\n   mov -4(%rsi,%r8), %ecx\n"                            \
    "mov %eax, (%rdi)\n   mov %ecx, -4(%rdi,%r8)\n   jmp 67f\n"                \
    "70: cmp $16, %r8d\n   jae 71f\n"                                          \
    "mov (%rsi), %rax\n   mov -8(%rsi,%r8), %rcx\n"                            \
    "mov %rax, (%rdi)\n   mov %rcx, -8(%rdi,%r8)\n   jmp 67f\n"                \
    "71: cmp $32, %r8d\n   ja 72f\n"                                           \
    "mov (%rsi), %rax\n   mov 8(%rsi), %rcx\n"                                 \
    "mov -16(%rsi,%r8), %rdx\n   mov -8(%rsi,%r8), %r9\n"                      \
    "mov %rax, (%rdi)\n   mov %rcx, 8(%rdi)\n"                                 \
    "mov %rdx, -16(%rdi,%r8)\n   mov %r9, -8(%rdi,%r8)\n   jmp 67f\n"          \
    "72: mov %r11, 16(%rsp)\n   mov %r8, %rdx\n   call memory_copy_apart\n"    \
    "mov 16(%rsp), %r11\n"                                                     \
    "67: add %r15, %r13\n" carried "jmp " again "\n"                           \
    /*  A sequence over seven bits, a length over seven, no room. */           \
    "50: " WATERLINK_X64_SPELL("rdi", "r15", "r15d", "r14", "r14d")            \
    "mov %r14d, %ecx\n   jmp 51b\n"                                            \
    "52: mov %r8d, %edx\n   and $0x7f, %edx\n   or $0x80, %edx\n"              \
    "mov %r8d, %r14d\n   shr $7, %r14d\n   shl $8, %r14d\n   or %r14d, %edx\n" \
    "lea 4(%rcx), %r14d\n   jmp 53b\n"                                         \
    "54: test %r13, %r13\n   jnz " full "\n   cmp $1168, %r15\n   ja " full "\n" \
    "movq $1168, 8(%rsp)\n   jmp 55b\n"                                        \
    /*  A frame under eight bytes goes with its payload in the head's        \
        word, stored as two words that meet. */                                \
    "56: test %r8d, %r8d\n   jz 74f\n   cmp $2, %r8d\n   jae 75f\n"            \
    "movzbl 38(%rsi), %r10d\n   jmp 73f\n"                                     \
    "75: movzwl 38(%rsi), %r10d\n   movzwl 36(%rsi,%r8), %ecx\n"               \
    "cmp $3, %r8d\n   jne 73f\n   shl $8, %ecx\n   or %ecx, %r10d\n"           \
    "73: lea 0(,%r14,8), %ecx\n   shl %cl, %r10\n   or %r10, %rdx\n"           \
    "74: mov %edx, (%rdi)\n   lea -32(,%r15,8), %ecx\n   shr %cl, %rdx\n"      \
    "mov %edx, -4(%rdi,%r15)\n   xor %r8d, %r8d\n   jmp 58b\n"                 \
    "57: mov %r8d, %ecx\n   shr $7, %ecx\n   mov %cl, 8(%rdi)\n   jmp 58b\n"    \
    "63: incq 0x702e8(%rbx)\n   jmp 64b\n"

//      A frame taken out of the normal band, r9 its key.
#define WATERLINK_X64_TAKEN "decw 0x70328(%rbx,%r9,2)\n"

/*
        rdi the link, rsi the body, rdx now, rcx where alone goes. With a
        frame to send: rbx the link, rbp the body, r12 now, r13 the bytes
        used, and on the stack the bytes the normal band carried, the room,
        a band's prior across a copy, alone's address, and the keys the
        normal band's walk has not found blocked. The
        acknowledgements are written with rdi, rsi and rdx as they came and
        r11 the bytes used.
*/
__asm__(
    ASM_FUNC(waterlink_fill)
    "movb $0, (%rcx)\n"
    "cmp %rdx, 0x70248(%rdi)\n   jae 1f\n   mov %rdx, 0x70248(%rdi)\n"
    "1:  cmpl $-1, 0x7023c(%rdi)\n   jne 2f\n"
    "cmpl $-1, 0x70218(%rdi)\n   jne 2f\n   cmpl $-1, 0x7021c(%rdi)\n   jne 2f\n"
    "xor %r11d, %r11d\n   jmp 100f\n"
    "2:  push %rbx\n   push %rbp\n   push %r12\n   push %r13\n   push %r14\n"
    "push %r15\n   sub $40, %rsp\n"
    "mov %rdi, %rbx\n   mov %rsi, %rbp\n   mov %rdx, %r12\n   mov %rcx, 24(%rsp)\n"
    "cmpl $-1, 0x7023c(%rbx)\n   je 5f\n"
    "mov %r12, %rsi\n   call waterlink_losses\n"
    //  Room kept for the acknowledgements owed, up to eight.
    "5:  xor %r13d, %r13d\n   movq $0, (%rsp)\n   mov $1168, %ecx\n"
    "mov 0x70210(%rbx), %rax\n   test %rax, %rax\n   jz 6f\n"
    "popcnt %rax, %rax\n   mov $8, %edx\n   cmp %rdx, %rax\n   cmova %rdx, %rax\n"
    "mov %rax, %rdx\n   shl $4, %rdx\n   add %rdx, %rax\n   sub %rax, %rcx\n"
    "6:  mov %rcx, 8(%rsp)\n"
    //  The urgent band, from its head.
    "10: mov 0x70218(%rbx), %eax\n   cmp $-1, %eax\n   je 20f\n"
    "imul $1200, %rax, %rsi\n   add %rbx, %rsi\n   mov $-1, %r11d\n"
    WATERLINK_X64_FRAME("0x70218", "0x70220", "0x70228", "19f", "10b", "", "")
    "19: mov 24(%rsp), %rax\n   movb $1, (%rax)\n   jmp 40f\n"
    "20: test %r13, %r13\n   jz 21f\n   mov 24(%rsp), %rax\n   movb $1, (%rax)\n"
    "21: mov $-1, %r11d\n   cmpq $0, 0x70320(%rbx)\n   je 40f\n"
    "movq $-1, 32(%rsp)\n"
    //  The normal band, while the window and the pacer allow, passing
    //  over a key with its window out; a frame taken leaves the ones
    //  passed over where they were, so the walk goes on from its prior.
    //  A stream found blocked is a bit cleared on the stack, and the walk
    //  ends when no other key has a frame left in the band: a key's bit
    //  set in banded with none counted is cleared on the way.
    "30: cmpl $0, 0x702b4(%rbx)\n   jne 31f\n"
    "mov 0x70268(%rbx), %rax\n   cmp 0x70270(%rbx), %rax\n   jae 40f\n"
    "cmpq $0, 0x70280(%rbx)\n   je 31f\n   cmp 0x70290(%rbx), %r12\n   jb 40f\n"
    "31: cmp $-1, %r11d\n   je 32f\n"
    "imul $1200, %r11, %rax\n   mov 20(%rbx,%rax), %eax\n   jmp 33f\n"
    "32: mov 0x7021c(%rbx), %eax\n"
    "33: cmp $-1, %eax\n   je 40f\n"
    "imul $1200, %rax, %rsi\n   add %rbx, %rsi\n"
    "movzbl 34(%rsi), %ecx\n   shl $4, %ecx\n   mov 0x6fa04(%rbx,%rcx), %ecx\n"
    "cmp $-1, %ecx\n   je 34f\n"
    "imul $1200, %rcx, %rcx\n   mov 16(%rsi), %edx\n   sub 16(%rbx,%rcx), %edx\n"
    "cmp $63, %edx\n   jbe 34f\n"
    "testb $2, 35(%rsi)\n   jz 36f\n"
    "movzbl 34(%rsi), %ecx\n   mov 32(%rsp), %rdx\n   btr %rcx, %rdx\n"
    "mov %rdx, 32(%rsp)\n   and 0x70320(%rbx), %rdx\n   jz 40f\n"
    "35: bsf %rdx, %rcx\n   cmpw $0, 0x70328(%rbx,%rcx,2)\n   jne 36f\n"
    "btr %rcx, %rdx\n   mov 0x70320(%rbx), %r8\n   btr %rcx, %r8\n"
    "mov %r8, 0x70320(%rbx)\n   test %rdx, %rdx\n   jnz 35b\n   jmp 40f\n"
    "36: mov %eax, %r11d\n   mov 20(%rsi), %eax\n   jmp 33b\n"
    "34:\n"
    WATERLINK_X64_FRAME("0x7021c", "0x70224", "0x7022c", "40f", "30b",
                        "add %r15, (%rsp)\n", WATERLINK_X64_TAKEN)
    //  A probe spends one expiry's worth; otherwise the pacer charges
    //  what the normal band carried.
    "40: mov 0x702b4(%rbx), %eax\n   mov (%rsp), %r15\n   test %eax, %eax\n"
    "jz 41f\n   test %r15, %r15\n   jz 42f\n   dec %eax\n   mov %eax, 0x702b4(%rbx)\n"
    "42: xor %r15d, %r15d\n"
    "41: test %r15, %r15\n   jz 43f\n"
    "mov 0x70280(%rbx), %rcx\n   test %rcx, %rcx\n   jz 43f\n"
    "mov 0x70270(%rbx), %rsi\n   lea (%rsi,%rsi,4), %rsi\n"
    "mov %rcx, %rax\n   imul %r15, %rax\n   shl $2, %rax\n   xor %edx, %edx\n"
    "div %rsi\n   mov %rax, %r8\n"
    "imul $4800, %rcx, %rax\n   xor %edx, %edx\n   div %rsi\n   shl $4, %rax\n"
    "mov %r12, %rcx\n   sub %rax, %rcx\n   xor %edx, %edx\n   cmp %rax, %r12\n"
    "cmovbe %rdx, %rcx\n"
    "mov 0x70290(%rbx), %rax\n   cmp %rcx, %rax\n   cmovb %rcx, %rax\n"
    "add %r8, %rax\n   mov %rax, 0x70290(%rbx)\n"
    "43: mov %rbx, %rdi\n   mov %rbp, %rsi\n   mov %r13, %r11\n   mov %r12, %rdx\n"
    "add $40, %rsp\n   pop %r15\n   pop %r14\n   pop %r13\n   pop %r12\n"
    "pop %rbp\n   pop %rbx\n"
    //  The acknowledgements, when frames go or they are due: per key, how
    //  far it was taken and a bit for each held past that. rdx the head,
    //  r8 the key's taken point, r9d its held chain and then the point's
    //  bytes, r10 the mask.
    "100: mov 0x70210(%rdi), %rax\n   test %rax, %rax\n   jz 119f\n"
    "test %r11, %r11\n   jnz 101f\n"
    "cmpb $0, 0x702ac(%rdi)\n   jne 101f\n   cmpl $2, 0x702a8(%rdi)\n   jae 101f\n"
    "cmp 0x702a0(%rdi), %rdx\n   jb 119f\n"
    "sub 0x702a0(%rdi), %rdx\n   cmp $1000, %rdx\n   jb 119f\n"
    "101: bsf %rax, %rcx\n   lea (%rcx,%rcx,2), %r8\n   lea 0x6fe00(%rdi,%r8,4), %r8\n"
    "mov 4(%r8), %r9d\n   mov (%r8), %r8d\n   xor %r10d, %r10d\n"
    "mov %ecx, %edx\n   shl $8, %edx\n   or $8, %edx\n"
    "cmp $-1, %r9d\n   je 103f\n"
    "102: imul $1172, %r9, %rcx\n   add %rdi, %rcx\n"
    "mov 0x4b000(%rcx), %eax\n   sub %r8d, %eax\n   dec %eax\n"
    "mov 0x4b008(%rcx), %r9d\n   cmp $63, %eax\n   ja 104f\n   bts %rax, %r10\n"
    "104: cmp $-1, %r9d\n   jne 102b\n"
    "103: mov $1, %r9d\n   cmp $127, %r8d\n   ja 105f\n"
    "106: shl $16, %r8\n   or %r8, %rdx\n   cmp $127, %r10\n   ja 107f\n"
    "lea 16(,%r9,8), %ecx\n   shl %cl, %r10\n   or %r10, %rdx\n"
    "lea 3(%r9), %rcx\n   lea (%r11,%rcx), %rax\n   cmp $1168, %rax\n   ja 119f\n"
    "lea (%rsi,%r11), %r8\n   mov %rax, %r11\n   cmp $8, %ecx\n   je 108f\n"
    "mov %edx, (%r8)\n   lea -32(,%rcx,8), %ecx\n   shr %cl, %rdx\n"
    "mov %edx, -4(%rsi,%r11)\n   jmp 109f\n"
    "108: mov %rdx, (%r8)\n"
    "109: mov 0x70210(%rdi), %rax\n   lea -1(%rax), %rcx\n   and %rcx, %rax\n"
    "mov %rax, 0x70210(%rdi)\n   jnz 101b\n"
    "movl $0, 0x702a8(%rdi)\n   movb $0, 0x702ac(%rdi)\n"
    "119: mov %r11, %rax\n"
    ASM_RET
    "105: " WATERLINK_X64_SPELL("r8", "rax", "eax", "r9", "r9d")
    "jmp 106b\n"
    //  A mask over seven bits: the head a byte at a time, then the mask.
    "107: bsr %r10, %rcx\n   lea (%rcx,%rcx,8), %ecx\n   add $73, %ecx\n   shr $6, %ecx\n"
    "lea 2(%r9,%rcx), %rcx\n   lea (%r11,%rcx), %rax\n   cmp $1168, %rax\n   ja 119b\n"
    "lea (%rsi,%r11), %r8\n   mov %rax, %r11\n   lea 2(%r9), %ecx\n"
    "110: mov %dl, (%r8)\n   inc %r8\n   shr $8, %rdx\n   dec %ecx\n   jnz 110b\n"
    "111: cmp $127, %r10\n   jbe 112f\n   mov %r10d, %eax\n   or $0x80, %eax\n"
    "mov %al, (%r8)\n   inc %r8\n   shr $7, %r10\n   jmp 111b\n"
    "112: mov %r10b, (%r8)\n   jmp 109b\n"
    ASM_END(waterlink_fill)
);
#elif ARM64
/*
        The spelling as on x86_64, v a number over 127 below 2^32: t and u
        go, n its bytes.
*/
#define WATERLINK_A64_SPELL(v, t, tw, uw, n, nw)                               \
    "and " t ", " v ", #0xffffffffffffff80\n   add " v ", " v ", " t "\n"       \
    "and " t ", " v ", #0xffffffffffff8000\n   add " v ", " v ", " t "\n"       \
    "and " t ", " v ", #0xffffffffff800000\n   add " v ", " v ", " t "\n"       \
    "and " t ", " v ", #0xffffffff80000000\n   add " v ", " v ", " t "\n"       \
    "clz " n ", " v "\n   eor " n ", " n ", #63\n   lsr " n ", " n ", #3\n"      \
    "mov " uw ", #32\n   sub " uw ", " uw ", " nw ", lsl #3\n"                  \
    "mov " tw ", #0x80808080\n   lsr " tw ", " tw ", " uw "\n"                  \
    "orr " v ", " v ", " t "\n   add " nw ", " nw ", #1\n"

/*
        One frame: w10 its slot, x11 the slot's address, w8 the one before
        it in the band. w12 the sequence, w13 the length, w14 the key, x15
        the flags and then the head, w16 the sequence's bytes and then the
        frame's, w17 the head's. The slot's fields are read first, its
        bookkeeping written next, and the frame last.
*/
#define WATERLINK_A64_FRAME(head, tail, requeue, full, again, carried, taken)  \
    "ldr w12, [x11, #16]\n   ldrh w13, [x11, #32]\n"                           \
    "ldrb w14, [x11, #34]\n   ldrb w15, [x11, #35]\n"                          \
    "mov w16, #1\n   cmp w12, #127\n   b.hi 50f\n"                             \
    "51: mov w9, w13\n   add w17, w16, #3\n   cmp w13, #127\n   b.hi 52f\n"    \
    "53: add w16, w16, #2\n   lsl w16, w16, #3\n   lsl x9, x9, x16\n"          \
    "orr x15, x15, x14, lsl #8\n   orr x15, x15, x12, lsl #16\n"               \
    "orr x15, x15, x9\n"                                                       \
    "add w16, w17, w13\n   add x12, x5, x16\n   cmp x12, x6\n   b.hi 54f\n"    \
    /*  Off the band, into the flight. */                                      \
    "55: ldr w12, [x11, #20]\n   mov w9, #1200\n   cmn w8, #1\n   b.eq 59f\n"  \
    "madd x9, x8, x9, x0\n   str w12, [x9, #20]\n   b 60f\n"                   \
    "59: str w12, [x4, #" head "]\n"                                           \
    "60: ldr w12, [x4, #" tail "]\n   cmp w12, w10\n   b.ne 61f\n"             \
    "str w8, [x4, #" tail "]\n"                                                \
    "61: ldr w12, [x4, #" requeue "]\n   cmp w12, w10\n   b.ne 62f\n"          \
    "str w8, [x4, #" requeue "]\n"                                             \
    "62: ldr x12, [x4, #0xa50]\n   add x12, x12, #1\n   str x12, [x4, #0xa50]\n" \
    "stp x2, x12, [x11]\n   mov w12, #2\n   strb w12, [x11, #36]\n"            \
    "ldrb w12, [x11, #37]\n   add w9, w12, #1\n   strb w9, [x11, #37]\n"       \
    "cbnz w12, 63f\n"                                                          \
    "64: ldr w12, [x4, #0xa40]\n   mov w9, #-1\n   stp w9, w12, [x11, #20]\n"  \
    "cmn w12, #1\n   b.eq 65f\n"                                               \
    "mov w9, #1200\n   madd x9, x12, x9, x0\n   str w10, [x9, #20]\n   b 66f\n" \
    "65: str w10, [x4, #0xa3c]\n"                                              \
    "66: str w10, [x4, #0xa40]\n" taken                                        \
    "ldr x12, [x4, #0xa68]\n   add x12, x12, x16\n   str x12, [x4, #0xa68]\n"   \
    "add x14, x4, x14, lsl #4\n   ldrh w12, [x14, #0x20c]\n"                   \
    "add w12, w12, #1\n   strh w12, [x14, #0x20c]\n"                           \
    "ldr x12, [x4, #0xae0]\n   add x12, x12, #1\n   str x12, [x4, #0xae0]\n"    \
    /*  The frame: the head in one store, the payload read in the pieces     \
        post wrote it in. */                                                   \
    "add x12, x1, x5\n   add x5, x5, x16\n" carried                            \
    "cmp w16, #8\n   b.lo 56f\n   str x15, [x12]\n   cmp w17, #9\n   b.eq 57f\n" \
    "58: cbz w13, 67f\n   add x12, x12, x17\n   add x11, x11, #38\n"           \
    "cmp w13, #4\n   b.hs 68f\n   cmp w13, #2\n   b.hs 69f\n"                  \
    "ldrb w9, [x11]\n   strb w9, [x12]\n   b 67f\n"                            \
    "69: sub x14, x13, #2\n   ldrh w9, [x11]\n   ldrh w10, [x11, x14]\n"       \
    "strh w9, [x12]\n   strh w10, [x12, x14]\n   b 67f\n"                      \
    "68: cmp w13, #8\n   b.hs 70f\n"                                           \
    "sub x14, x13, #4\n   ldr w9, [x11]\n   ldr w10, [x11, x14]\n"             \
    "str w9, [x12]\n   str w10, [x12, x14]\n   b 67f\n"                        \
    "70: cmp w13, #16\n   b.hs 71f\n"                                          \
    "sub x14, x13, #8\n   ldr x9, [x11]\n   ldr x10, [x11, x14]\n"             \
    "str x9, [x12]\n   str x10, [x12, x14]\n   b 67f\n"                        \
    "71: cmp w13, #32\n   b.hi 72f\n"                                          \
    "sub x14, x13, #16\n   ldr q0, [x11]\n   ldr q1, [x11, x14]\n"             \
    "str q0, [x12]\n   str q1, [x12, x14]\n   b 67f\n"                         \
    "72: stp x29, x30, [sp, #-80]!\n   mov x29, sp\n"                          \
    "stp x0, x1, [sp, #16]\n   stp x2, x3, [sp, #32]\n"                        \
    "stp x5, x6, [sp, #48]\n   stp x7, x8, [sp, #64]\n"                        \
    "mov x0, x12\n   mov x1, x11\n   mov x2, x13\n   bl memory_copy_apart\n"   \
    "ldp x0, x1, [sp, #16]\n   ldp x2, x3, [sp, #32]\n"                        \
    "ldp x5, x6, [sp, #48]\n   ldp x7, x8, [sp, #64]\n   ldp x29, x30, [sp], #80\n" \
    "add x4, x0, #0x6f, lsl #12\n   add x4, x4, #0x800\n"                      \
    "67: b " again "\n"                                                        \
    /*  A sequence over seven bits, a length over seven, no room. */           \
    "50: " WATERLINK_A64_SPELL("x12", "x9", "w9", "w17", "x16", "w16")         \
    "b 51b\n"                                                                  \
    "52: and w9, w13, #0x7f\n   orr w9, w9, #0x80\n   lsr w17, w13, #7\n"       \
    "orr w9, w9, w17, lsl #8\n   add w17, w16, #4\n   b 53b\n"                  \
    "54: cbnz x5, " full "\n   cmp w16, #1168\n   b.hi " full "\n"             \
    "mov x6, #1168\n   b 55b\n"                                                \
    /*  A frame under eight bytes goes with its payload in the head's        \
        word, stored as two words that meet. */                                \
    "56: cbz w13, 74f\n   cmp w13, #2\n   b.hs 75f\n   ldrb w9, [x11, #38]\n"  \
    "b 73f\n"                                                                  \
    "75: ldrh w9, [x11, #38]\n   cmp w13, #3\n   b.ne 73f\n"                   \
    "ldrb w10, [x11, #40]\n   orr w9, w9, w10, lsl #16\n"                      \
    "73: lsl w10, w17, #3\n   lsl x9, x9, x10\n   orr x15, x15, x9\n"          \
    "74: str w15, [x12]\n   sub w10, w16, #4\n   lsl w9, w10, #3\n"            \
    "lsr x15, x15, x9\n   str w15, [x12, x10]\n   b 67b\n"                     \
    "57: lsr w9, w13, #7\n   strb w9, [x12, #8]\n   b 58b\n"                    \
    "63: ldr x12, [x4, #0xae8]\n   add x12, x12, #1\n   str x12, [x4, #0xae8]\n" \
    "b 64b\n"

//      A frame taken out of the normal band, w14 its key. x9 and w12 go.
#define WATERLINK_A64_TAKEN                                                    \
    "add x9, x4, x14, lsl #1\n   ldrh w12, [x9, #0xb28]\n"                     \
    "sub w12, w12, #1\n   strh w12, [x9, #0xb28]\n"

/*
        x0 the link, x1 the body, x2 now, x3 where alone goes and then, in
        the normal band's walk, the keys not found blocked, x4 the link plus
        0x6f800 so every field past the slots is an offset from it, x5 the
        bytes used, x6 the room, x7 the bytes the normal band carried, w8 a
        band's prior. Nothing is kept on the stack but across the two calls,
        the losses walk and a payload past thirty two bytes.
*/
__asm__(
    ASM_FUNC(waterlink_fill)
    "strb wzr, [x3]\n   add x4, x0, #0x6f, lsl #12\n   add x4, x4, #0x800\n"
    "ldr x10, [x4, #0xa48]\n   cmp x2, x10\n   b.ls 1f\n   str x2, [x4, #0xa48]\n"
    //  Nothing in flight and nothing queued: the acknowledgements alone.
    "1:  ldr w10, [x4, #0xa3c]\n   ldr w11, [x4, #0xa18]\n   ldr w12, [x4, #0xa1c]\n"
    "and w11, w11, w12\n   and w11, w11, w10\n   mov x5, #0\n   cmn w11, #1\n   b.eq 100f\n"
    "cmn w10, #1\n   b.eq 5f\n"
    "stp x29, x30, [sp, #-48]!\n   mov x29, sp\n   stp x0, x1, [sp, #16]\n"
    "stp x2, x3, [sp, #32]\n   mov x1, x2\n   bl waterlink_losses\n"
    "ldp x0, x1, [sp, #16]\n   ldp x2, x3, [sp, #32]\n   ldp x29, x30, [sp], #48\n"
    "add x4, x0, #0x6f, lsl #12\n   add x4, x4, #0x800\n"
    //  Room kept for the acknowledgements owed, up to eight.
    "5:  mov x5, #0\n   mov x6, #1168\n   mov x7, #0\n   ldr x10, [x4, #0xa10]\n"
    "cbz x10, 10f\n"
    "fmov d0, x10\n   cnt v0.8b, v0.8b\n   addv b0, v0.8b\n   fmov w10, s0\n"
    "mov w11, #8\n   cmp w10, #8\n   csel w10, w10, w11, lo\n"
    "add w10, w10, w10, lsl #4\n   sub x6, x6, x10\n"
    //  The urgent band, from its head.
    "10: ldr w10, [x4, #0xa18]\n   cmn w10, #1\n   b.eq 20f\n"
    "mov w9, #1200\n   madd x11, x10, x9, x0\n   mov w8, #-1\n"
    WATERLINK_A64_FRAME("0xa18", "0xa20", "0xa28", "19f", "10b", "", "")
    "19: mov w10, #1\n   strb w10, [x3]\n   b 40f\n"
    "20: cbz x5, 21f\n   mov w10, #1\n   strb w10, [x3]\n"
    "21: mov w8, #-1\n   ldr x10, [x4, #0xb20]\n   cbz x10, 40f\n   mov x3, #-1\n"
    //  The normal band, while the window and the pacer allow, passing over
    //  a key with its window out and going on from the prior. A stream
    //  found blocked is a bit cleared in x3, and the walk ends when no
    //  other key has a frame left in the band: a key's bit set in banded
    //  with none counted is cleared on the way.
    "30: ldr w10, [x4, #0xab4]\n   cbnz w10, 31f\n"
    "ldr x10, [x4, #0xa68]\n   ldr x11, [x4, #0xa70]\n   cmp x10, x11\n   b.hs 40f\n"
    "ldr x10, [x4, #0xa80]\n   cbz x10, 31f\n   ldr x10, [x4, #0xa90]\n"
    "cmp x2, x10\n   b.lo 40f\n"
    "31: mov w9, #1200\n   cmn w8, #1\n   b.eq 32f\n"
    "madd x10, x8, x9, x0\n   ldr w10, [x10, #20]\n   b 33f\n"
    "32: ldr w10, [x4, #0xa1c]\n"
    "33: cmn w10, #1\n   b.eq 40f\n   madd x11, x10, x9, x0\n"
    "ldrb w12, [x11, #34]\n   add x12, x4, x12, lsl #4\n   ldr w12, [x12, #0x204]\n"
    "cmn w12, #1\n   b.eq 34f\n"
    "madd x12, x12, x9, x0\n   ldr w13, [x11, #16]\n   ldr w12, [x12, #16]\n"
    "sub w13, w13, w12\n   cmp w13, #63\n   b.ls 34f\n"
    "ldrb w13, [x11, #35]\n   tbz w13, #1, 36f\n"
    "ldrb w12, [x11, #34]\n   mov x13, #1\n   lsl x13, x13, x12\n   bic x3, x3, x13\n"
    "ldr x12, [x4, #0xb20]\n   and x12, x12, x3\n   cbz x12, 40f\n"
    "35: rbit x13, x12\n   clz x13, x13\n   add x14, x4, x13, lsl #1\n"
    "ldrh w14, [x14, #0xb28]\n   cbnz w14, 36f\n"
    "mov x14, #1\n   lsl x14, x14, x13\n   bic x12, x12, x14\n"
    "ldr x15, [x4, #0xb20]\n   bic x15, x15, x14\n   str x15, [x4, #0xb20]\n"
    "cbnz x12, 35b\n   b 40f\n"
    "36: mov w8, w10\n   ldr w10, [x11, #20]\n   b 33b\n"
    "34:\n"
    WATERLINK_A64_FRAME("0xa1c", "0xa24", "0xa2c", "40f", "30b",
                        "add x7, x7, x16\n", WATERLINK_A64_TAKEN)
    //  A probe spends one expiry's worth; otherwise the pacer charges what
    //  the normal band carried.
    "40: ldr w10, [x4, #0xab4]\n   cbz w10, 41f\n   cbz x7, 42f\n"
    "sub w10, w10, #1\n   str w10, [x4, #0xab4]\n"
    "42: mov x7, #0\n"
    "41: cbz x7, 100f\n   ldr x10, [x4, #0xa80]\n   cbz x10, 100f\n"
    "ldr x11, [x4, #0xa70]\n   add x11, x11, x11, lsl #2\n"
    "mul x12, x10, x7\n   lsl x12, x12, #2\n   udiv x12, x12, x11\n"
    "mov x13, #4800\n   mul x13, x10, x13\n   udiv x13, x13, x11\n   lsl x13, x13, #4\n"
    "subs x14, x2, x13\n   csel x14, x14, xzr, hi\n"
    "ldr x15, [x4, #0xa90]\n   cmp x15, x14\n   csel x15, x14, x15, lo\n"
    "add x15, x15, x12\n   str x15, [x4, #0xa90]\n"
    //  The acknowledgements, when frames go or they are due: x10 the keys
    //  owed, x16 and w17 the held pool, w11 the key, w12 how far it was
    //  taken and then its spelling, w13 its held chain, x14 the mask, x15
    //  the head, w9 the spelling's bytes.
    "100: ldr x10, [x4, #0xa10]\n   cbz x10, 119f\n   cbnz x5, 101f\n"
    "ldrb w11, [x4, #0xaac]\n   cbnz w11, 101f\n"
    "ldr w11, [x4, #0xaa8]\n   cmp w11, #2\n   b.hs 101f\n"
    "ldr x11, [x4, #0xaa0]\n   cmp x2, x11\n   b.lo 119f\n"
    "sub x11, x2, x11\n   cmp x11, #1000\n   b.lo 119f\n"
    "101: add x16, x0, #0x4b, lsl #12\n   mov w17, #1172\n"
    "102: rbit x11, x10\n   clz x11, x11\n   add x12, x11, x11, lsl #1\n"
    "add x12, x4, x12, lsl #2\n   ldr w13, [x12, #0x604]\n   ldr w12, [x12, #0x600]\n"
    "mov x14, #0\n   lsl w15, w11, #8\n   orr w15, w15, #8\n"
    "cmn w13, #1\n   b.eq 103f\n"
    "104: madd x6, x13, x17, x16\n   ldr w7, [x6]\n   ldr w13, [x6, #8]\n"
    "sub w7, w7, w12\n   sub w7, w7, #1\n   cmp w7, #63\n   b.hi 105f\n"
    "mov x8, #1\n   lsl x8, x8, x7\n   orr x14, x14, x8\n"
    "105: cmn w13, #1\n   b.ne 104b\n"
    "103: mov w9, #1\n   cmp w12, #127\n   b.hi 106f\n"
    "107: orr x15, x15, x12, lsl #16\n   cmp x14, #127\n   b.hi 108f\n"
    "add w6, w9, #2\n   lsl w6, w6, #3\n   lsl x14, x14, x6\n   orr x15, x15, x14\n"
    "add x6, x9, #3\n   add x7, x5, x6\n   cmp x7, #1168\n   b.hi 119f\n"
    "add x8, x1, x5\n   mov x5, x7\n   sub x7, x10, #1\n   and x10, x10, x7\n"
    "str x10, [x4, #0xa10]\n   cmp x6, #8\n   b.eq 109f\n"
    "str w15, [x8]\n   sub x6, x6, #4\n   lsl x7, x6, #3\n   lsr x15, x15, x7\n"
    "str w15, [x8, x6]\n   b 110f\n"
    "109: str x15, [x8]\n"
    "110: cbnz x10, 102b\n"
    "str wzr, [x4, #0xaa8]\n   strb wzr, [x4, #0xaac]\n"
    "119: mov x0, x5\n"
    ASM_RET
    "106: " WATERLINK_A64_SPELL("x12", "x6", "w6", "w7", "x9", "w9")
    "b 107b\n"
    //  A mask over seven bits: the head a byte at a time, then the mask.
    "108: clz x6, x14\n   eor x6, x6, #63\n   add x6, x6, x6, lsl #3\n"
    "add x6, x6, #73\n   lsr x6, x6, #6\n   add x6, x6, x9\n   add x6, x6, #2\n"
    "add x7, x5, x6\n   cmp x7, #1168\n   b.hi 119b\n"
    "add x8, x1, x5\n   mov x5, x7\n   sub x7, x10, #1\n   and x10, x10, x7\n"
    "str x10, [x4, #0xa10]\n   add w6, w9, #2\n"
    "111: strb w15, [x8], #1\n   lsr x15, x15, #8\n   subs w6, w6, #1\n   b.ne 111b\n"
    "112: cmp x14, #127\n   b.ls 113f\n   orr w6, w14, #0x80\n   strb w6, [x8], #1\n"
    "lsr x14, x14, #7\n   b 112b\n"
    "113: strb w14, [x8]\n   b 110b\n"
    ASM_END(waterlink_fill)
);
#elif RISCV64
/*
        One frame: t0 its slot, t1 the slot's address, t6 the one before it
        in the band. t2 the sequence, t3 the length, t4 the flags with the
        key over them, t5 the frame's bytes; s0, s1 and a3 go. The slot's
        fields are read first, its bookkeeping written next, and the frame
        last, a byte at a time: a body's bytes are wherever the frames
        before put them, and baseline riscv64 asks for aligned words.
*/
#define WATERLINK_RV_FRAME(head, tail, requeue, full, again, carried, taken)   \
    "lwu t2, 16(t1)\n   lhu t3, 32(t1)\n   lbu t4, 34(t1)\n   lbu t5, 35(t1)\n" \
    "slli t4, t4, 8\n   or t4, t4, t5\n"                                       \
    "li s0, 128\n   addi t5, t3, 5\n   bgeu t3, s0, 51f\n   addi t5, t3, 4\n"   \
    "51: srli s1, t2, 7\n   beqz s1, 53f\n"                                    \
    "52: addi t5, t5, 1\n   srli s1, s1, 7\n   bnez s1, 52b\n"                  \
    "53: add s0, a5, t5\n   bltu a6, s0, 54f\n"                                \
    /*  Off the band, into the flight. */                                      \
    "55: lw s0, 20(t1)\n   bltz t6, 59f\n"                                     \
    "li s1, 1200\n   mul s1, t6, s1\n   add s1, a0, s1\n   sw s0, 20(s1)\n"    \
    "j 60f\n"                                                                  \
    "59: sw s0, " head "(a4)\n"                                                \
    "60: lw s0, " tail "(a4)\n   bne s0, t0, 61f\n   sw t6, " tail "(a4)\n"     \
    "61: lw s0, " requeue "(a4)\n   bne s0, t0, 62f\n"                         \
    "sw t6, " requeue "(a4)\n"                                                 \
    "62: ld s0, 848(a4)\n   addi s0, s0, 1\n   sd s0, 848(a4)\n"               \
    "sd s0, 8(t1)\n   sd a2, 0(t1)\n   li s0, 2\n   sb s0, 36(t1)\n"           \
    "lbu s0, 37(t1)\n   addi s1, s0, 1\n   sb s1, 37(t1)\n   bnez s0, 63f\n"   \
    "64: lw s0, 832(a4)\n   li s1, -1\n   sw s1, 20(t1)\n   sw s0, 24(t1)\n"   \
    "bltz s0, 65f\n"                                                           \
    "li s1, 1200\n   mul s1, s0, s1\n   add s1, a0, s1\n   sw t0, 20(s1)\n"    \
    "j 66f\n"                                                                  \
    "65: sw t0, 828(a4)\n"                                                     \
    "66: sw t0, 832(a4)\n   ld s0, 872(a4)\n   add s0, s0, t5\n   sd s0, 872(a4)\n" \
    taken                                                                      \
    "srli s0, t4, 8\n   slli s0, s0, 4\n   add s0, a4, s0\n"                   \
    "lhu s1, -1268(s0)\n   addi s1, s1, 1\n   sh s1, -1268(s0)\n"              \
    "ld s0, 992(a4)\n   addi s0, s0, 1\n   sd s0, 992(a4)\n"                   \
    /*  The frame. */                                                          \
    "add a3, a1, a5\n   add a5, a5, t5\n" carried                              \
    "sb t4, 0(a3)\n   srli s0, t4, 8\n   sb s0, 1(a3)\n   addi a3, a3, 2\n"     \
    "li s1, 128\n"                                                             \
    "70: bltu t2, s1, 71f\n   ori s0, t2, 128\n   sb s0, 0(a3)\n"              \
    "addi a3, a3, 1\n   srli t2, t2, 7\n   j 70b\n"                            \
    "71: sb t2, 0(a3)\n   addi a3, a3, 1\n   bltu t3, s1, 72f\n"                \
    "ori s0, t3, 128\n   sb s0, 0(a3)\n   srli s0, t3, 7\n   sb s0, 1(a3)\n"   \
    "addi a3, a3, 2\n   j 73f\n"                                               \
    "72: sb t3, 0(a3)\n   addi a3, a3, 1\n"                                    \
    "73: beqz t3, 67f\n   addi t1, t1, 38\n   li s0, 16\n   bltu s0, t3, 75f\n" \
    "add s0, t1, t3\n"                                                         \
    "74: lbu s1, 0(t1)\n   sb s1, 0(a3)\n   addi t1, t1, 1\n   addi a3, a3, 1\n" \
    "bne t1, s0, 74b\n"                                                        \
    "67: j " again "\n"                                                        \
    "75: addi sp, sp, -64\n   sd a0, 0(sp)\n   sd a1, 8(sp)\n   sd a2, 16(sp)\n" \
    "sd a4, 24(sp)\n   sd a5, 32(sp)\n   sd a6, 40(sp)\n   sd a7, 48(sp)\n"     \
    "sd t6, 56(sp)\n   mv a0, a3\n   mv a1, t1\n   mv a2, t3\n"                \
    "call memory_copy_apart\n"                                                 \
    "ld a0, 0(sp)\n   ld a1, 8(sp)\n   ld a2, 16(sp)\n   ld a4, 24(sp)\n"       \
    "ld a5, 32(sp)\n   ld a6, 40(sp)\n   ld a7, 48(sp)\n   ld t6, 56(sp)\n"     \
    "addi sp, sp, 64\n   j 67b\n"                                              \
    "54: bnez a5, " full "\n   li s0, 1168\n   bltu s0, t5, " full "\n"        \
    "li a6, 1168\n   j 55b\n"                                                  \
    "63: ld s0, 1000(a4)\n   addi s0, s0, 1\n   sd s0, 1000(a4)\n   j 64b\n"

//      A frame taken out of the normal band, its key over t4's flags. s0
//      and s1 go.
#define WATERLINK_RV_TAKEN                                                     \
    "srli s0, t4, 8\n   slli s0, s0, 1\n   add s0, a4, s0\n"                    \
    "lhu s1, 1064(s0)\n   addi s1, s1, -1\n   sh s1, 1064(s0)\n"

/*
        a0 the link, a1 the body, a2 now, a3 where alone goes, a4 the link
        plus 0x6ff00 so every field past the slots is an offset from it, a5
        the bytes used, a6 the room, a7 the bytes the normal band carried,
        t6 a band's prior. With a frame to send, ra, s0, s1, where alone
        goes and the keys the normal band's walk has not found blocked are
        kept on the stack, and the losses walk's arguments across it. The key of an
        acknowledgement is its bit's place, which the double its power of
        two converts to says in its exponent.
*/
__asm__(
    ASM_FUNC(waterlink_fill)
    "sb zero, 0(a3)\n   lui a4, 0x70\n   addi a4, a4, -256\n   add a4, a0, a4\n"
    "ld t0, 840(a4)\n   bgeu t0, a2, 1f\n   sd a2, 840(a4)\n"
    //  Nothing in flight and nothing queued: the acknowledgements alone.
    "1:  lw t0, 828(a4)\n   lw t1, 792(a4)\n   lw t2, 796(a4)\n   and t1, t1, t2\n"
    "and t1, t1, t0\n   li a5, 0\n   addi t1, t1, 1\n   beqz t1, 100f\n"
    "addi sp, sp, -64\n   sd ra, 24(sp)\n   sd s0, 16(sp)\n   sd s1, 8(sp)\n"
    "sd a3, 0(sp)\n   bltz t0, 5f\n"
    "sd a0, 48(sp)\n   sd a1, 32(sp)\n   sd a2, 40(sp)\n   mv a1, a2\n"
    "call waterlink_losses\n"
    "ld a0, 48(sp)\n   ld a1, 32(sp)\n   ld a2, 40(sp)\n"
    "lui a4, 0x70\n   addi a4, a4, -256\n   add a4, a0, a4\n"
    //  Room kept for the acknowledgements owed, up to eight.
    "5:  li a5, 0\n   li a6, 1168\n   li a7, 0\n   ld t0, 784(a4)\n   beqz t0, 10f\n"
    "li t1, 0\n   li t2, 8\n"
    "6:  addi t1, t1, 1\n   addi t3, t0, -1\n   and t0, t0, t3\n   beqz t0, 7f\n"
    "bne t1, t2, 6b\n"
    "7:  slli t2, t1, 4\n   add t1, t1, t2\n   sub a6, a6, t1\n"
    //  The urgent band, from its head.
    "10: lw t0, 792(a4)\n   bltz t0, 20f\n"
    "li s0, 1200\n   mul t1, t0, s0\n   add t1, a0, t1\n   li t6, -1\n"
    WATERLINK_RV_FRAME("792", "800", "808", "19f", "10b", "", "")
    "19: ld t0, 0(sp)\n   li t1, 1\n   sb t1, 0(t0)\n   j 40f\n"
    "20: beqz a5, 21f\n   ld t0, 0(sp)\n   li t1, 1\n   sb t1, 0(t0)\n"
    "21: li t6, -1\n   ld t0, 1056(a4)\n   beqz t0, 40f\n   li t0, -1\n"
    "sd t0, 56(sp)\n"
    //  The normal band, while the window and the pacer allow, passing over
    //  a key with its window out and going on from the prior. A stream
    //  found blocked is a bit cleared on the stack, and the walk ends when
    //  no other key has a frame left in the band: a key's bit set in
    //  banded with none counted is cleared on the way.
    "30: lw t0, 948(a4)\n   bnez t0, 31f\n"
    "ld t0, 872(a4)\n   ld t1, 880(a4)\n   bgeu t0, t1, 40f\n"
    "ld t0, 896(a4)\n   beqz t0, 31f\n   ld t0, 912(a4)\n   bltu a2, t0, 40f\n"
    "31: li s0, 1200\n   bltz t6, 32f\n"
    "mul t0, t6, s0\n   add t0, a0, t0\n   lw t0, 20(t0)\n   j 33f\n"
    "32: lw t0, 796(a4)\n"
    "33: bltz t0, 40f\n   mul t1, t0, s0\n   add t1, a0, t1\n"
    "lbu t2, 34(t1)\n   slli t2, t2, 4\n   add t2, a4, t2\n   lw t2, -1276(t2)\n"
    "bltz t2, 34f\n"
    "mul t2, t2, s0\n   add t2, a0, t2\n   lwu t3, 16(t1)\n   lwu t2, 16(t2)\n"
    "subw t3, t3, t2\n   li t2, 64\n   bltu t3, t2, 34f\n"
    "lbu t2, 35(t1)\n   andi t2, t2, 2\n   beqz t2, 36f\n"
    "lbu t2, 34(t1)\n   li t3, 1\n   sll t3, t3, t2\n   not t3, t3\n"
    "ld t2, 56(sp)\n   and t2, t2, t3\n   sd t2, 56(sp)\n"
    "ld t3, 1056(a4)\n   and t3, t3, t2\n   beqz t3, 40f\n"
    "35: neg t4, t3\n   and t4, t3, t4\n   fcvt.d.lu ft0, t4\n   fmv.x.d t5, ft0\n"
    "srli t5, t5, 52\n   addi t5, t5, -1023\n   slli t5, t5, 1\n   add t5, a4, t5\n"
    "lhu t5, 1064(t5)\n   bnez t5, 36f\n   xor t3, t3, t4\n   not t4, t4\n"
    "ld t5, 1056(a4)\n   and t5, t5, t4\n   sd t5, 1056(a4)\n   bnez t3, 35b\n"
    "j 40f\n"
    "36: mv t6, t0\n   lw t0, 20(t1)\n   j 33b\n"
    "34:\n"
    WATERLINK_RV_FRAME("796", "804", "812", "40f", "30b", "add a7, a7, t5\n",
                       WATERLINK_RV_TAKEN)
    //  A probe spends one expiry's worth; otherwise the pacer charges what
    //  the normal band carried.
    "40: lw t0, 948(a4)\n   beqz t0, 41f\n   beqz a7, 42f\n"
    "addi t0, t0, -1\n   sw t0, 948(a4)\n"
    "42: li a7, 0\n"
    "41: beqz a7, 43f\n   ld t0, 896(a4)\n   beqz t0, 43f\n"
    "ld t1, 880(a4)\n   slli t2, t1, 2\n   add t1, t1, t2\n"
    "mul t2, t0, a7\n   slli t2, t2, 2\n   divu t2, t2, t1\n"
    "li t3, 4800\n   mul t3, t0, t3\n   divu t3, t3, t1\n   slli t3, t3, 4\n"
    "li t4, 0\n   bgeu t3, a2, 44f\n   sub t4, a2, t3\n"
    "44: ld t5, 912(a4)\n   bgeu t5, t4, 45f\n   mv t5, t4\n"
    "45: add t5, t5, t2\n   sd t5, 912(a4)\n"
    "43: ld ra, 24(sp)\n   ld s0, 16(sp)\n   ld s1, 8(sp)\n   addi sp, sp, 64\n"
    //  The acknowledgements, when frames go or they are due: t0 the keys
    //  owed, t5 and t6 the held pool, t1 the key, t3 how far it was taken,
    //  t2 its held chain, t4 the mask, a6 the bytes.
    "100: ld t0, 784(a4)\n   beqz t0, 119f\n   bnez a5, 101f\n"
    "lbu t1, 940(a4)\n   bnez t1, 101f\n"
    "lwu t1, 936(a4)\n   li t2, 2\n   bgeu t1, t2, 101f\n"
    "ld t1, 928(a4)\n   bltu a2, t1, 119f\n"
    "sub t1, a2, t1\n   li t2, 1000\n   bltu t1, t2, 119f\n"
    "101: li t5, 1172\n   lui t6, 0x4b\n   add t6, a0, t6\n"
    "102: neg t1, t0\n   and t1, t0, t1\n   fcvt.d.lu ft0, t1\n   fmv.x.d t1, ft0\n"
    "srli t1, t1, 52\n   addi t1, t1, -1023\n"
    "slli t2, t1, 1\n   add t2, t2, t1\n   slli t2, t2, 2\n   add t2, a4, t2\n"
    "lwu t3, -256(t2)\n   lw t2, -252(t2)\n   li t4, 0\n   bltz t2, 103f\n"
    "104: mul a3, t2, t5\n   add a3, t6, a3\n   lwu a6, 0(a3)\n   lw t2, 8(a3)\n"
    "subw a6, a6, t3\n   addiw a6, a6, -1\n   li a7, 64\n   bgeu a6, a7, 105f\n"
    "li a7, 1\n   sll a7, a7, a6\n   or t4, t4, a7\n"
    "105: bgez t2, 104b\n"
    "103: li a7, 128\n   li a6, 4\n   srli a3, t3, 7\n   beqz a3, 106f\n"
    "107: addi a6, a6, 1\n   srli a3, a3, 7\n   bnez a3, 107b\n"
    "106: srli a3, t4, 7\n   beqz a3, 108f\n"
    "109: addi a6, a6, 1\n   srli a3, a3, 7\n   bnez a3, 109b\n"
    "108: add a6, a5, a6\n   li a3, 1168\n   bltu a3, a6, 119f\n"
    "add a3, a1, a5\n   mv a5, a6\n   addi a6, t0, -1\n   and t0, t0, a6\n"
    "sd t0, 784(a4)\n   li a6, 8\n   sb a6, 0(a3)\n   sb t1, 1(a3)\n   addi a3, a3, 2\n"
    "110: bltu t3, a7, 111f\n   ori a6, t3, 128\n   sb a6, 0(a3)\n   addi a3, a3, 1\n"
    "srli t3, t3, 7\n   j 110b\n"
    "111: sb t3, 0(a3)\n   addi a3, a3, 1\n"
    "112: bltu t4, a7, 113f\n   ori a6, t4, 128\n   sb a6, 0(a3)\n   addi a3, a3, 1\n"
    "srli t4, t4, 7\n   j 112b\n"
    "113: sb t4, 0(a3)\n   bnez t0, 102b\n"
    "sw zero, 936(a4)\n   sb zero, 940(a4)\n"
    "119: mv a0, a5\n"
    ASM_RET
    ASM_END(waterlink_fill)
);
#endif

/*
        A key whose frames the far side holds, with none of its own in
        flight, has no timer to ask again: the acknowledgement that frees
        them -- sent when the far side took them, or when its reader came
        back -- may be the one the network lost, and then the key waits for
        ever, with everything queued behind it on its window. So its oldest
        held frame goes back to its band once the probe timer, doubled for
        each time it was asked about already and at most a minute, runs out,
        and the far side answers the copy with what it has taken: a reader
        that is only slow costs a copy now and then, as TCP's persist timer
        does. Doubled per question and not per transmission: a frame sent
        eight times on a lossy path before it was held would otherwise wait
        the whole minute for its first.
        Looking over the keys is sixty four steps, so it is done when one of
        them is due, and at least every RTO_MOST while any slot is taken,
        never on every wake. Answers when to look next.
*/
static p64 waterlink_held_probes(struct waterlink_link address_to link,
                                 p64 now)
{
        p64 due = now + WATERLINK_RTO_MOST;

        if (link->free_count == WATERLINK_SLOTS)
                return ~0ull;
        if (now < link->looked)
                return link->looked;
        for (p32 key = 0; key < WATERLINK_KEYS; key++)
        {
                p32 at = link->sending[key].first;
                struct waterlink_slot address_to slot;
                p64 wait;

                if (at == WATERLINK_NONE || link->sending[key].flying ||
                    link->slot[at].state != WATERLINK_SLOT_HELD)
                        continue;
                slot = link->slot + at;
                wait = waterlink_timeout(link)
                       << (slot->probed < 15 ? slot->probed : 15);
                if (wait > WATERLINK_HELD_MOST)
                        wait = WATERLINK_HELD_MOST;
                if (now >= slot->sent && now - slot->sent >= wait)
                {
                        slot->probed += slot->probed < 255;
                        waterlink_band_requeue(link, at);
                        due = now;
                }
                else if (slot->sent + wait < due)
                        due = slot->sent + wait;
        }
        link->looked = due;
        return due;
}

/*
        Whether the normal band has a frame its key's window lets go. The
        walk ends at the first, or when every key left in the band is a
        stream it found blocked.
*/
static bool waterlink_sendable(struct waterlink_link address_to link)
{
        p64 blocked = 0;

        for (p32 at = link->head[WATERLINK_BAND_NORMAL]; at != WATERLINK_NONE;
             at = link->slot[at].next)
        {
                if (!waterlink_key_blocked(link, link->slot + at))
                        return true;
                if (!(link->slot[at].flags & WATERLINK_FRAME_DURABLE))
                        continue;
                blocked |= 1ull << link->slot[at].key;
                if (waterlink_band_done(link, blocked))
                        break;
        }
        return false;
}

/*
        When the caller should next call fill if nothing arrives first: now,
        when acknowledgements are owed or something may be sent; when the
        pacer next lets a datagram go; when the oldest frame in flight would
        time out, or a held one is due to be asked about. ~0 when there is
        nothing to wait for.
*/
p64 waterlink_wake(struct waterlink_link address_to link, p64 now)
{
        p64 wake = waterlink_held_probes(link, now);

        if (wake <= now || waterlink_ack_due(link, now) ||
            link->head[WATERLINK_BAND_URGENT] != WATERLINK_NONE)
                return now;

        if (link->acking && link->owed + WATERLINK_ACK_DELAY < wake)
                wake = link->owed + WATERLINK_ACK_DELAY;

        //      A frame waiting on its key's window is waiting for an
        //      acknowledgement or the timer, and both of those wake the
        //      caller already; answering now for it would spin.
        if ((link->probes || link->in_flight < link->window) &&
            waterlink_sendable(link))
        {
                if (link->probes || !link->smoothed || link->pace <= now)
                        return now;
                if (link->pace < wake)
                        wake = link->pace;
        }

        if (link->flight_head != WATERLINK_NONE)
        {
                p64 due = link->slot[link->flight_head].sent +
                          waterlink_timeout(link);

                if (due < wake)
                        wake = due;
        }
        return wake;
}

/*
        Every frame taken by the far side's application and nothing owed: the
        link can close. Not only nothing queued or in flight -- a frame the
        far side holds for a reader that is behind is not delivered yet, and
        closing then would drop it there.
*/
bool waterlink_idle(struct waterlink_link address_to link)
{
        return link->free_count == WATERLINK_SLOTS && !link->acking;
}

/*
        Hold a frame for its key: in the key's chain by sequence, once. With
        the pool full it is dropped unheld, and the sender, which never heard
        of it arriving, sends it again.
*/
static KEEP fn waterlink_hold(struct waterlink_link address_to link,
                              struct waterlink_receiving address_to live,
                              struct waterlink_frame address_to head,
                              p8 address_to payload)
{
        p32 prior = WATERLINK_NONE;
        p32 look = live->first;
        p32 at;

        while (look != WATERLINK_NONE &&
               link->held[look].head.sequence < head->sequence)
        {
                prior = look;
                look = link->held[look].next;
        }
        if (look != WATERLINK_NONE &&
            link->held[look].head.sequence == head->sequence)
                return;

        at = link->held_free;
        if (at == WATERLINK_NONE)
        {
                link->spilled++;
                return;
        }
        link->held_free = link->held[at].next;
        link->held[at].head = address_to head;
        if (head->length)
                memory_copy(link->held[at].payload, payload, head->length);
        link->held[at].next = look;
        if (prior == WATERLINK_NONE)
                live->first = at;
        else
                link->held[prior].next = at;
        link->kept++;
}

typedef bool (address_to waterlink_sink)(address_any context,
                                         struct waterlink_frame address_to head,
                                         p8 address_to payload);

// Hand one frame to the application; false when it would not take it.
static bool waterlink_hand(struct waterlink_link address_to link,
                           struct waterlink_receiving address_to live,
                           struct waterlink_frame address_to head,
                           p8 address_to payload, waterlink_sink sink,
                           address_any context)
{
        if (sink && !sink(context, head, payload))
                return false;
        live->delivered = head->sequence;
        link->delivered++;
        if (head->flags & WATERLINK_FRAME_LAST)
                live->over = 1;
        return true;
}

static fn waterlink_held_drop(struct waterlink_link address_to link,
                              struct waterlink_receiving address_to live)
{
        p32 at = live->first;

        live->first = link->held[at].next;
        link->held[at].next = link->held_free;
        link->held_free = at;
}

/*
        What a key holds, handed on while it can be: a stream's frames while
        each is the next, a register's newest value only. When the
        application will not take one the key pauses there, held, until
        waterlink_resume. An ended key keeps nothing.
*/
static KEEP fn waterlink_release(struct waterlink_link address_to link, p8 key,
                                 waterlink_sink sink, address_any context)
{
        struct waterlink_receiving address_to live = link->receiving + key;

        while (live->first != WATERLINK_NONE && !live->over)
        {
                struct waterlink_held address_to held = link->held + live->first;
                bool newer = held->next != WATERLINK_NONE;

                if (held->head.sequence <= live->delivered ||
                    ((held->head.flags & WATERLINK_FRAME_REPLACEABLE) && newer))
                {
                        link->stale++;
                        waterlink_held_drop(link, live);
                        continue;
                }
                if ((held->head.flags & WATERLINK_FRAME_DURABLE) &&
                    held->head.sequence != live->delivered + 1)
                        break;
                if (!waterlink_hand(link, live, address_of held->head,
                                    held->payload, sink, context))
                {
                        live->paused = 1;
                        return;
                }
                waterlink_held_drop(link, live);
        }
        live->paused = 0;
        while (live->over && live->first != WATERLINK_NONE)
                waterlink_held_drop(link, live);
}

/*
        The application will take a paused key's frames again: what it holds
        goes on now, and the sender hears at once, since what it hears is
        what lets it send more.
*/
fn waterlink_resume(struct waterlink_link address_to link, p8 key,
                    waterlink_sink sink, address_any context)
{
        /*      Keys also arrive from the application side of the link.  The
                wire judge confines its byte before it reaches this API, but
                a confused or compromised service must not turn an invalid
                channel number into an out-of-bounds receiving-table access. */
        if (key >= WATERLINK_KEYS)
                return;

        p32 before = link->receiving[key].delivered;

        waterlink_release(link, key, sink, context);
        if (link->receiving[key].delivered != before)
        {
                waterlink_ack_owe(link, key);
                link->owed_now = 1;
        }
}

bool waterlink_paused(struct waterlink_link address_to link, p8 key)
{
        return key < WATERLINK_KEYS && link->receiving[key].paused;
}

/*
        What a judged body holds, one part to each frame or acknowledgement
        in the order they came: its flags and key, a frame's sequence or
        what an acknowledgement says was delivered, a frame's length or an
        acknowledgement's mask, and where a frame's payload starts. The
        smallest part is four bytes, so a body holds at most a quarter of
        its bytes of them.
*/
struct waterlink_part
{
        p8 flags;
        p8 key;
        p16 at;
        p32 number;
        p64 more;
};

#define WATERLINK_PARTS (WATERLINK_PAYLOAD / 4)

/*
        Judge an authenticated body whole, and say what it holds: the number
        of parts written, or -1 when the link would not take it.

        A tag proves who sent the bytes, not that they are a body, and a body
        is taken whole or not at all -- a request followed by a malformed
        frame must not reach the application while the datagram as a whole
        is refused. So every frame and acknowledgement is read and checked
        here, and nothing but zeros may follow the last: a zero flags byte is
        where the frames stop, and the padding is inside the tag, so this is
        the sender's statement and not a guess. A key is acknowledged once a
        body, as fill writes it: each acknowledgement walks its key's slots,
        and a body of one key's, over and over, was a peer buying the
        listener's time at hundreds of times what it spent. What is read is
        kept, so the body is read once and applied from the parts.

        One pass, in each machine's registers: a part is two stores, a number
        of one byte never leaves the line, and the zeros after the last part
        are lib.c's memory_span_byte.
*/
bipolar waterlink_judge(address_any body, positive length,
                        struct waterlink_part address_to parts);

_Static_assert(sizeof(struct waterlink_part) == 16 &&
                       __builtin_offsetof(struct waterlink_part, at) == 2 &&
                       __builtin_offsetof(struct waterlink_part, number) == 4 &&
                       __builtin_offsetof(struct waterlink_part, more) == 8,
               "the judge writes a part as two words");
_Static_assert(WATERLINK_PAYLOAD == 1168 && WATERLINK_FRAME_MAX == 1159 &&
                       WATERLINK_KEYS == 64 && WATERLINK_FRAME_ACK == 8 &&
                       WATERLINK_FRAME_WIRE == 0x17 &&
                       WATERLINK_NONE == 0xffffffffu,
               "the judge's bounds are these");

#if X64
/*
        rdi the body, rsi its length, rdx the parts; r8 the offset, r9 sixteen
        times the parts written, ebx the flags and key as one word, r10 and
        r11 the two numbers, r12 the keys acknowledged so far.
*/
__asm__(
    ASM_FUNC(waterlink_judge)
    "cmp $1168, %rsi\n   ja 8f\n"
    "push %rbx\n   push %r12\n   xor %r8d, %r8d\n   xor %r9d, %r9d\n   xor %r12d, %r12d\n"
    //  A part starts at a flags byte that is not zero, with its key after.
    "1:  lea 1(%r8), %rax\n   cmp %rsi, %rax\n   jae 6f\n"
    "movzwl (%rdi,%r8), %ebx\n   test %bl, %bl\n   jz 6f\n"
    "cmp $0x3fff, %ebx\n   ja 9f\n"
    "add $2, %r8\n"
    "cmp %rsi, %r8\n   jae 9f\n"
    "movzbl (%rdi,%r8), %r10d\n   inc %r8\n   test %r10b, %r10b\n   js 20f\n"
    "2:  cmp %rsi, %r8\n   jae 9f\n"
    "movzbl (%rdi,%r8), %r11d\n   inc %r8\n   test %r11b, %r11b\n   js 30f\n"
    //  The part: flags, key, where a payload would start and the first
    //  number in one word, the second in the other.
    "3:  mov %r8, %rax\n   shl $16, %rax\n   or %rbx, %rax\n"
    "mov %r10, %rcx\n   shl $32, %rcx\n   or %rcx, %rax\n"
    "mov %rax, (%rdx,%r9)\n   mov %r11, 8(%rdx,%r9)\n   add $16, %r9\n"
    //  An acknowledgement names a sequence, once a key; its mask is any word.
    "cmp $8, %bl\n   jne 4f\n"
    "movzbl %bh, %ecx\n   bts %rcx, %r12\n   jc 9f\n"
    "mov $0xfffffffe, %eax\n   cmp %rax, %r10\n   jbe 1b\n   jmp 9f\n"
    //  A frame: a sequence, one class, no bit the wire does not know, and a
    //  length that is a frame's and is there.
    "4:  lea -1(%r10), %rax\n   mov $0xfffffffd, %ecx\n   cmp %rcx, %rax\n   ja 9f\n"
    "mov %ebx, %eax\n   shr $1, %eax\n   xor %ebx, %eax\n   test $1, %al\n   jz 9f\n"
    "test $0xe8, %bl\n   jnz 9f\n"
    "cmp $1159, %r11\n   ja 9f\n"
    "mov %rsi, %rax\n   sub %r8, %rax\n   cmp %rax, %r11\n   ja 9f\n"
    "add %r11, %r8\n   jmp 1b\n"
    //  Zeros to the end. rbx keeps sixteen times the count over the bytes
    //  left, both under 4096 times it, across the call.
    "6:  pop %r12\n   sub %r8, %rsi\n   add %r8, %rdi\n   shl $12, %r9\n   lea (%r9,%rsi), %rbx\n"
    "mov %rsi, %rdx\n   xor %esi, %esi\n   call memory_span_byte\n"
    "mov %ebx, %ecx\n   and $4095, %ecx\n   cmp %rcx, %rax\n   jne 7f\n"
    "mov %rbx, %rax\n   shr $16, %rax\n   pop %rbx\n"
    ASM_RET
    "9:  pop %r12\n"
    "7:  pop %rbx\n"
    "8:  mov $-1, %rax\n"
    ASM_RET
    //  The rest of a number that said more follows, seven bits a byte; a
    //  last byte of zero is a longer spelling of a shorter number, and the
    //  tenth byte holds bit 63 alone.
    "20: and $0x7f, %r10d\n   mov $7, %ecx\n"
    "21: cmp %rsi, %r8\n   jae 9b\n"
    "movzbl (%rdi,%r8), %eax\n   inc %r8\n"
    "cmp $63, %ecx\n   je 24f\n"
    "test %al, %al\n   js 23f\n"
    "test %eax, %eax\n   jz 9b\n"
    "shl %cl, %rax\n   or %rax, %r10\n   jmp 2b\n"
    "23: and $0x7f, %eax\n   shl %cl, %rax\n   or %rax, %r10\n   add $7, %ecx\n   jmp 21b\n"
    "24: cmp $1, %eax\n   jne 9b\n   bts $63, %r10\n   jmp 2b\n"
    "30: and $0x7f, %r11d\n   mov $7, %ecx\n"
    "31: cmp %rsi, %r8\n   jae 9b\n"
    "movzbl (%rdi,%r8), %eax\n   inc %r8\n"
    "cmp $63, %ecx\n   je 34f\n"
    "test %al, %al\n   js 33f\n"
    "test %eax, %eax\n   jz 9b\n"
    "shl %cl, %rax\n   or %rax, %r11\n   jmp 3b\n"
    "33: and $0x7f, %eax\n   shl %cl, %rax\n   or %rax, %r11\n   add $7, %ecx\n   jmp 31b\n"
    "34: cmp $1, %eax\n   jne 9b\n   bts $63, %r11\n   jmp 3b\n"
    ASM_END(waterlink_judge)
);
#elif ARM64
/*
        x0 the body, x1 its length, x2 the parts; x8 the offset, x9 the parts
        written, w11 the flags, w12 the key, x13 and x14 the two numbers, x3
        the keys acknowledged so far.
*/
__asm__(
    ASM_FUNC(waterlink_judge)
    "cmp x1, #1168\n   b.hi 8f\n"
    "mov x8, #0\n   mov x9, #0\n   mov x3, #0\n"
    "1:  add x10, x8, #1\n   cmp x10, x1\n   b.hs 6f\n"
    "ldrb w11, [x0, x8]\n   cbz w11, 6f\n"
    "ldrb w12, [x0, x10]\n   cmp w12, #63\n   b.hi 8f\n"
    "add x8, x8, #2\n"
    "cmp x8, x1\n   b.hs 8f\n"
    "ldrb w13, [x0, x8]\n   add x8, x8, #1\n   tbnz w13, #7, 20f\n"
    "2:  cmp x8, x1\n   b.hs 8f\n"
    "ldrb w14, [x0, x8]\n   add x8, x8, #1\n   tbnz w14, #7, 30f\n"
    "3:  orr w15, w11, w12, lsl #8\n   orr x15, x15, x8, lsl #16\n"
    "orr x15, x15, x13, lsl #32\n"
    "add x16, x2, x9, lsl #4\n   stp x15, x14, [x16]\n   add x9, x9, #1\n"
    "cmp w11, #8\n   b.ne 4f\n"
    "lsr x16, x3, x12\n   tbnz x16, #0, 8f\n   mov x16, #1\n   lsl x16, x16, x12\n"
    "orr x3, x3, x16\n"
    "mov w16, #0xfffffffe\n   cmp x13, x16\n   b.ls 1b\n   b 8f\n"
    "4:  sub x16, x13, #1\n   mov w17, #0xfffffffd\n   cmp x16, x17\n   b.hi 8f\n"
    "eor w16, w11, w11, lsr #1\n   tbz w16, #0, 8f\n"
    "mov w17, #0x17\n   bics wzr, w11, w17\n   b.ne 8f\n"
    "cmp x14, #1159\n   b.hi 8f\n"
    "sub x16, x1, x8\n   cmp x14, x16\n   b.hi 8f\n"
    "add x8, x8, x14\n   b 1b\n"
    "6:  stp x29, x30, [sp, #-32]!\n   mov x29, sp\n   stp x19, x20, [sp, #16]\n"
    "sub x19, x1, x8\n   mov x20, x9\n"
    "add x0, x0, x8\n   mov w1, #0\n   mov x2, x19\n   bl memory_span_byte\n"
    "cmp x0, x19\n   csinv x0, x20, xzr, eq\n"
    "ldp x19, x20, [sp, #16]\n   ldp x29, x30, [sp], #32\n"
    ASM_RET
    "8:  mov x0, #-1\n"
    ASM_RET
    "20: and w13, w13, #0x7f\n   mov w17, #7\n"
    "21: cmp x8, x1\n   b.hs 8b\n"
    "ldrb w16, [x0, x8]\n   add x8, x8, #1\n"
    "cmp w17, #63\n   b.eq 24f\n"
    "tbnz w16, #7, 23f\n"
    "cbz w16, 8b\n"
    "lsl x16, x16, x17\n   orr x13, x13, x16\n   b 2b\n"
    "23: and w16, w16, #0x7f\n   lsl x16, x16, x17\n   orr x13, x13, x16\n"
    "add w17, w17, #7\n   b 21b\n"
    "24: cmp w16, #1\n   b.ne 8b\n   orr x13, x13, #0x8000000000000000\n   b 2b\n"
    "30: and w14, w14, #0x7f\n   mov w17, #7\n"
    "31: cmp x8, x1\n   b.hs 8b\n"
    "ldrb w16, [x0, x8]\n   add x8, x8, #1\n"
    "cmp w17, #63\n   b.eq 34f\n"
    "tbnz w16, #7, 33f\n"
    "cbz w16, 8b\n"
    "lsl x16, x16, x17\n   orr x14, x14, x16\n   b 3b\n"
    "33: and w16, w16, #0x7f\n   lsl x16, x16, x17\n   orr x14, x14, x16\n"
    "add w17, w17, #7\n   b 31b\n"
    "34: cmp w16, #1\n   b.ne 8b\n   orr x14, x14, #0x8000000000000000\n   b 3b\n"
    ASM_END(waterlink_judge)
);
#elif RISCV64
/*
        a0 the body, a1 its length, a2 the parts; t1 the offset, t2 the parts
        written, t4 the flags, t5 the key, a3 and a4 the two numbers, a6 the
        keys acknowledged so far.
*/
__asm__(
    ASM_FUNC(waterlink_judge)
    "li t0, 1168\n   bgtu a1, t0, 8f\n"
    "li t1, 0\n   li t2, 0\n   li a6, 0\n"
    "1:  addi t0, t1, 1\n   bgeu t0, a1, 6f\n"
    "add t3, a0, t1\n   lbu t4, 0(t3)\n   beqz t4, 6f\n"
    "lbu t5, 1(t3)\n   li t0, 63\n   bgtu t5, t0, 8f\n"
    "addi t1, t1, 2\n"
    "bgeu t1, a1, 8f\n"
    "add t3, a0, t1\n   lbu a3, 0(t3)\n   addi t1, t1, 1\n"
    "andi t0, a3, 0x80\n   bnez t0, 20f\n"
    "2:  bgeu t1, a1, 8f\n"
    "add t3, a0, t1\n   lbu a4, 0(t3)\n   addi t1, t1, 1\n"
    "andi t0, a4, 0x80\n   bnez t0, 30f\n"
    "3:  slli t0, t5, 8\n   or t0, t0, t4\n   slli t3, t1, 16\n   or t0, t0, t3\n"
    "slli t3, a3, 32\n   or t0, t0, t3\n"
    "slli t3, t2, 4\n   add t3, a2, t3\n   sd t0, 0(t3)\n   sd a4, 8(t3)\n"
    "addi t2, t2, 1\n"
    "li t0, 8\n   bne t4, t0, 4f\n"
    "srl t0, a6, t5\n   andi t0, t0, 1\n   bnez t0, 8f\n"
    "li t0, 1\n   sll t0, t0, t5\n   or a6, a6, t0\n"
    "li t0, 0xfffffffe\n   bleu a3, t0, 1b\n   j 8f\n"
    "4:  addi t0, a3, -1\n   li t3, 0xfffffffd\n   bgtu t0, t3, 8f\n"
    "srli t0, t4, 1\n   xor t0, t0, t4\n   andi t0, t0, 1\n   beqz t0, 8f\n"
    "andi t0, t4, 0xe8\n   bnez t0, 8f\n"
    "li t0, 1159\n   bgtu a4, t0, 8f\n"
    "sub t0, a1, t1\n   bgtu a4, t0, 8f\n"
    "add t1, t1, a4\n   j 1b\n"
    "6:  addi sp, sp, -32\n   sd ra, 24(sp)\n   sd s0, 16(sp)\n   sd s1, 8(sp)\n"
    "sub s0, a1, t1\n   mv s1, t2\n"
    "add a0, a0, t1\n   li a1, 0\n   mv a2, s0\n   call memory_span_byte\n"
    "li t0, -1\n   bne a0, s0, 7f\n   mv t0, s1\n"
    "7:  mv a0, t0\n"
    "ld ra, 24(sp)\n   ld s0, 16(sp)\n   ld s1, 8(sp)\n   addi sp, sp, 32\n"
    ASM_RET
    "8:  li a0, -1\n"
    ASM_RET
    "20: andi a3, a3, 0x7f\n   li a5, 7\n"
    "21: bgeu t1, a1, 8b\n"
    "add t3, a0, t1\n   lbu t0, 0(t3)\n   addi t1, t1, 1\n"
    "li t3, 63\n   beq a5, t3, 24f\n"
    "andi t3, t0, 0x80\n   bnez t3, 23f\n"
    "beqz t0, 8b\n"
    "sll t0, t0, a5\n   or a3, a3, t0\n   j 2b\n"
    "23: andi t0, t0, 0x7f\n   sll t0, t0, a5\n   or a3, a3, t0\n"
    "addi a5, a5, 7\n   j 21b\n"
    "24: li t3, 1\n   bne t0, t3, 8b\n   slli t0, t0, 63\n   or a3, a3, t0\n   j 2b\n"
    "30: andi a4, a4, 0x7f\n   li a5, 7\n"
    "31: bgeu t1, a1, 8b\n"
    "add t3, a0, t1\n   lbu t0, 0(t3)\n   addi t1, t1, 1\n"
    "li t3, 63\n   beq a5, t3, 34f\n"
    "andi t3, t0, 0x80\n   bnez t3, 33f\n"
    "beqz t0, 8b\n"
    "sll t0, t0, a5\n   or a4, a4, t0\n   j 3b\n"
    "33: andi t0, t0, 0x7f\n   sll t0, t0, a5\n   or a4, a4, t0\n"
    "addi a5, a5, 7\n   j 31b\n"
    "34: li t3, 1\n   bne t0, t3, 8b\n   slli t0, t0, 63\n   or a4, a4, t0\n   j 3b\n"
    ASM_END(waterlink_judge)
);
#endif

/*
        Apply a judged body: acknowledgements to the sending half, frames to
        the application.

        A stream frame is taken when it is the next on its key and held when
        it is ahead; a register frame is taken when it is newer than what the
        key has. A frame at or behind what the key has taken was already
        taken, or is an older value, and is dropped -- what makes a copy the
        sender sent again arrive only once. Every frame that arrives, new or
        not, is owed an acknowledgement, because a copy arriving is the
        sender saying it never heard the first one was acknowledged.

        Every part comes from waterlink_judge, which has checked each bound
        against what the body says it is; nothing here reads the body but a
        frame's payload where the judge found it.

        An acknowledgement is taken at the sender here: everything on the
        key up to what was taken is done and freed, and a frame the mask
        says is held has arrived and is waiting, which moves the latest
        known arrival forward without freeing anything -- the far side still
        has to take it, and the slot stays until it does. What a body's
        acknowledgements add up to is applied once, after its last part: the
        latest arrival and a round trip from it, the window grown by what was
        newly delivered, and the losses that makes visible.

        In each machine's registers: a part is read once, a frame taken in
        order with nothing held behind it touches no function, and nothing
        of the link is kept in a register across a call, since the sink may
        post into it. The held pool, a key's release once it holds
        something, a slot freed from the middle of its key's chain and a
        queued slot's unlinking are the C beside this.
*/
fn waterlink_apply(struct waterlink_link address_to link, address_any body,
                   struct waterlink_part address_to parts, positive count,
                   p64 now, waterlink_sink sink, address_any context);

_Static_assert(__builtin_offsetof(struct waterlink_slot, chain) == 28 &&
                       __builtin_offsetof(struct waterlink_receiving, over) == 8 &&
                       __builtin_offsetof(struct waterlink_receiving, paused) == 9 &&
                       __builtin_offsetof(struct waterlink_sending, last) == 8 &&
                       sizeof(struct waterlink_frame) == 8 &&
                       __builtin_offsetof(struct waterlink_frame, length) == 4 &&
                       __builtin_offsetof(struct waterlink_frame, key) == 6 &&
                       __builtin_offsetof(struct waterlink_frame, flags) == 7,
               "apply reads these at these places");
_Static_assert(__builtin_offsetof(struct waterlink_link, free) == 0x70230 &&
                       __builtin_offsetof(struct waterlink_link, free_count) == 0x70234 &&
                       __builtin_offsetof(struct waterlink_link, largest) == 0x70258 &&
                       __builtin_offsetof(struct waterlink_link, recovery) == 0x70260 &&
                       __builtin_offsetof(struct waterlink_link, threshold) == 0x70278 &&
                       __builtin_offsetof(struct waterlink_link, variance) == 0x70288 &&
                       __builtin_offsetof(struct waterlink_link, recent) == 0x70298 &&
                       __builtin_offsetof(struct waterlink_link, backoff) == 0x702b0 &&
                       __builtin_offsetof(struct waterlink_link, delivered) == 0x702d0 &&
                       __builtin_offsetof(struct waterlink_link, stale) == 0x702d8 &&
                       __builtin_offsetof(struct waterlink_link, acked) == 0x70300 &&
                       WATERLINK_SLOT_QUEUED == 1 && WATERLINK_SLOT_HELD == 3 &&
                       WATERLINK_SLOT_FREE == 0 && WATERLINK_FRAME_LAST == 4 &&
                       WATERLINK_FRAME_URGENT == 0x10 &&
                       WATERLINK_WINDOW_MOST == 307200,
               "apply's link is laid out so");

#if X64
/*
        rbx the link, rbp the part, r12 past the last, r13 the bytes newly
        delivered, r14 the latest transmission known to have arrived, r15 its
        round trip. On the stack: a frame's head for the sink and the held
        pool, the body, the sink, its context, now, whether frames came and
        whether acknowledgements did, and an acknowledgement's walk across
        the C it calls.
*/
__asm__(
    ASM_FUNC(waterlink_apply)
    "push %rbx\n   push %rbp\n   push %r12\n   push %r13\n   push %r14\n   push %r15\n"
    "sub $88, %rsp\n"
    "mov %rdi, %rbx\n   mov %rdx, %rbp\n   shl $4, %rcx\n   lea (%rdx,%rcx), %r12\n"
    "mov %rsi, 8(%rsp)\n   mov %r9, 16(%rsp)\n   mov 144(%rsp), %rax\n   mov %rax, 24(%rsp)\n"
    "cmp %r8, 0x70248(%rbx)\n   jae 1f\n   mov %r8, 0x70248(%rbx)\n"
    "1:  mov 0x70248(%rbx), %rax\n   mov %rax, 32(%rsp)\n   movq $0, 40(%rsp)\n"
    "xor %r13d, %r13d\n   xor %r14d, %r14d\n   xor %r15d, %r15d\n"
    "cmp %r12, %rbp\n   jae 90f\n"
    //  A part: eax its flags, ecx its key, edx its first number.
    "10: movzbl (%rbp), %eax\n   movzbl 1(%rbp), %ecx\n   mov 4(%rbp), %edx\n"
    "cmp $8, %eax\n   je 40f\n"
    //  A frame is owed an acknowledgement whatever becomes of it.
    "lea (%rcx,%rcx,2), %rsi\n   lea 0x6fe00(%rbx,%rsi,4), %rsi\n"
    "mov 0x70210(%rbx), %rdi\n   test %rdi, %rdi\n   jnz 11f\n"
    "mov 0x70248(%rbx), %r8\n   mov %r8, 0x702a0(%rbx)\n"
    "11: bts %rcx, %rdi\n   mov %rdi, 0x70210(%rbx)\n   orb $1, 40(%rsp)\n"
    "test $0x14, %al\n   jz 12f\n   movb $1, 0x702ac(%rbx)\n"
    "12: mov (%rsi), %edi\n   cmp %edi, %edx\n   jbe 13f\n   cmpb $0, 8(%rsi)\n   jne 13f\n"
    "cmpb $0, 9(%rsi)\n   jne 14f\n   test $2, %al\n   jz 15f\n"
    "lea 1(%rdi), %r8d\n   cmp %r8d, %edx\n   jne 14f\n"
    "15: mov 16(%rsp), %r8\n   test %r8, %r8\n   jnz 16f\n"
    //  Taken: the key moves on, and whatever it holds follows.
    "17: mov %edx, (%rsi)\n   incq 0x702d0(%rbx)\n   test $4, %al\n   jz 18f\n"
    "movb $1, 8(%rsi)\n"
    "18: cmpl $-1, 4(%rsi)\n   jne 19f\n   movb $0, 9(%rsi)\n"
    "20: add $16, %rbp\n   cmp %r12, %rbp\n   jb 10b\n   jmp 90f\n"
    "19: mov %rbx, %rdi\n   mov %ecx, %esi\n   mov 16(%rsp), %rdx\n   mov 24(%rsp), %rcx\n"
    "call waterlink_release\n   jmp 20b\n"
    //  At or behind what was taken, or after its last: a copy, answered.
    "13: incq 0x702d8(%rbx)\n   movb $1, 0x702ac(%rbx)\n   jmp 20b\n"
    //  Ahead, or its key paused: held.
    "14: movb $1, 0x702ac(%rbx)\n"
    "21: movzwl 8(%rbp), %r8d\n   shl $32, %r8\n   or %rdx, %r8\n   shl $48, %rcx\n"
    "or %rcx, %r8\n   shl $56, %rax\n   or %rax, %r8\n   mov %r8, (%rsp)\n"
    "movzwl 2(%rbp), %ecx\n   add 8(%rsp), %rcx\n   mov %rbx, %rdi\n   mov %rsp, %rdx\n"
    "call waterlink_hold\n   jmp 20b\n"
    //  The application's say: the head goes by address, and nothing held
    //  in a register survives the call.
    "16: movzwl 8(%rbp), %r9d\n   shl $32, %r9\n   or %rdx, %r9\n   mov %rcx, %r10\n"
    "shl $48, %r10\n   or %r10, %r9\n   mov %rax, %r10\n   shl $56, %r10\n   or %r10, %r9\n"
    "mov %r9, (%rsp)\n   movzwl 2(%rbp), %edx\n   add 8(%rsp), %rdx\n   mov 24(%rsp), %rdi\n"
    "mov %rsp, %rsi\n   call *%r8\n"
    "movzbl 7(%rsp), %r8d\n   movzbl 1(%rbp), %ecx\n   mov (%rsp), %edx\n"
    "lea (%rcx,%rcx,2), %rsi\n   lea 0x6fe00(%rbx,%rsi,4), %rsi\n"
    "test %al, %al\n   mov %r8d, %eax\n   jnz 17b\n"
    "movzwl 2(%rbp), %ecx\n   add 8(%rsp), %rcx\n   mov %rbx, %rdi\n   mov %rsp, %rdx\n"
    "mov %rsi, 80(%rsp)\n   call waterlink_hold\n   mov 80(%rsp), %rsi\n"
    "movb $1, 9(%rsi)\n   jmp 20b\n"
    //  An acknowledgement: edx what was taken, r8 the mask, r9 the key's
    //  sending entry, eax the slot, rsi its address, r10d the next on the
    //  key, r11d its sequence.
    "40: orb $2, 40(%rsp)\n   mov 8(%rbp), %r8\n   shl $4, %ecx\n"
    "lea 0x6fa00(%rbx,%rcx), %r9\n   mov 4(%r9), %eax\n"
    "41: cmp $-1, %eax\n   je 20b\n"
    "imul $1200, %rax, %rsi\n   add %rbx, %rsi\n   mov 28(%rsi), %r10d\n   mov 16(%rsi), %r11d\n"
    "cmp %edx, %r11d\n   jbe 42f\n"
    //  Past the mask: a key's slots are in sequence order, so the rest are.
    "mov %r11d, %ecx\n   sub %edx, %ecx\n   dec %ecx\n   cmp $63, %ecx\n   ja 20b\n"
    "bt %rcx, %r8\n   jnc 49f\n"
    "42: movzbl 36(%rsi), %ecx\n   cmp $2, %ecx\n   jne 43f\n"
    //  In flight: what it carried is delivered and leaves the flight, and
    //  the newest transmission among them is the round trip's sample.
    "movzwl 32(%rsi), %ecx\n   lea 4(%rcx), %edi\n   cmp $127, %ecx\n   jbe 44f\n   inc %edi\n"
    "44: mov %edi, %ecx\n   cmp $127, %r11d\n   jbe 45f\n"
    "bsr %r11d, %edi\n   lea (%rdi,%rdi,8), %edi\n   add $9, %edi\n   shr $6, %edi\n"
    "add %rdi, %rcx\n"
    "45: add %rcx, %r13\n   sub %rcx, 0x70268(%rbx)\n"
    "mov 8(%rsi), %r11\n   cmp %r14, %r11\n   jbe 46f\n   mov %r11, %r14\n   xor %r15d, %r15d\n"
    "cmpb $1, 37(%rsi)\n   jne 46f\n   mov 0x70248(%rbx), %r15\n   sub (%rsi), %r15\n"
    "46: mov 24(%rsi), %ecx\n   mov 20(%rsi), %edi\n   cmp $-1, %ecx\n   je 47f\n"
    "imul $1200, %rcx, %r11\n   mov %edi, 20(%rbx,%r11)\n   jmp 48f\n"
    "47: mov %edi, 0x7023c(%rbx)\n"
    "48: cmp $-1, %edi\n   je 50f\n   imul $1200, %rdi, %r11\n   mov %ecx, 24(%rbx,%r11)\n"
    "jmp 51f\n"
    "50: mov %ecx, 0x70240(%rbx)\n"
    "51: movzbl 34(%rsi), %ecx\n   shl $4, %ecx\n   decw 0x6fa0c(%rbx,%rcx)\n"
    "movq $-1, 20(%rsi)\n"
    //  Taken is freed; past what was taken, held.
    "52: cmp %edx, 16(%rsi)\n   ja 53f\n   incq 0x70300(%rbx)\n"
    "cmp %eax, 4(%r9)\n   jne 54f\n   mov %r10d, 4(%r9)\n   cmp %eax, 8(%r9)\n   jne 55f\n"
    "movl $-1, 8(%r9)\n"
    "55: movb $0, 36(%rsi)\n   movl $-1, 28(%rsi)\n   mov 0x70230(%rbx), %ecx\n"
    "mov %ecx, 20(%rsi)\n   mov %eax, 0x70230(%rbx)\n   incl 0x70234(%rbx)\n   jmp 49f\n"
    "53: movb $3, 36(%rsi)\n"
    "49: mov %r10d, %eax\n   jmp 41b\n"
    //  Queued, lost and not sent again yet: off its band. A slot freed from
    //  the middle of its chain: the C walks to it.
    "43: cmp $1, %ecx\n   jne 52b\n   mov %eax, 64(%rsp)\n   mov %r10d, 72(%rsp)\n"
    "mov %edx, 48(%rsp)\n   mov %r8, 56(%rsp)\n   mov %rbx, %rdi\n   mov %eax, %esi\n"
    "call waterlink_band_remove\n   mov 64(%rsp), %eax\n   imul $1200, %rax, %rsi\n"
    "add %rbx, %rsi\n   mov 48(%rsp), %edx\n   mov 56(%rsp), %r8\n   mov 72(%rsp), %r10d\n"
    "movzbl 1(%rbp), %ecx\n   shl $4, %ecx\n   lea 0x6fa00(%rbx,%rcx), %r9\n   jmp 52b\n"
    "54: mov %r10d, 72(%rsp)\n   mov %edx, 48(%rsp)\n   mov %r8, 56(%rsp)\n"
    "mov %rbx, %rdi\n   mov %eax, %esi\n   call waterlink_slot_free\n"
    "mov 48(%rsp), %edx\n   mov 56(%rsp), %r8\n   mov 72(%rsp), %r10d\n"
    "movzbl 1(%rbp), %ecx\n   shl $4, %ecx\n   lea 0x6fa00(%rbx,%rcx), %r9\n   jmp 49b\n"
    //  Once for the body: the latest arrival, its round trip into the
    //  estimate, the window grown by what was delivered after the last
    //  halving, and the losses that makes visible.
    "90: testb $2, 40(%rsp)\n   jz 95f\n   test %r14, %r14\n   jz 91f\n"
    "cmp 0x70258(%rbx), %r14\n   jbe 92f\n   mov %r14, 0x70258(%rbx)\n"
    "92: test %r15, %r15\n   jz 93f\n   mov %r15, 0x70298(%rbx)\n"
    "mov 0x70280(%rbx), %rax\n   test %rax, %rax\n   jnz 94f\n"
    "mov %r15, 0x70280(%rbx)\n   mov %r15, %rcx\n   shr $1, %rcx\n   mov %rcx, 0x70288(%rbx)\n"
    "jmp 93f\n"
    "94: mov %r15, %rcx\n   sub %rax, %rcx\n   mov %rax, %rdx\n   sub %r15, %rdx\n"
    "cmp %rax, %r15\n   cmovbe %rdx, %rcx\n"
    "mov 0x70288(%rbx), %rdx\n   lea (%rdx,%rdx,2), %rdx\n   add %rcx, %rdx\n   shr $2, %rdx\n"
    "mov %rdx, 0x70288(%rbx)\n   lea 0(,%rax,8), %rdx\n   sub %rax, %rdx\n   add %r15, %rdx\n"
    "shr $3, %rdx\n   mov %rdx, 0x70280(%rbx)\n"
    "93: movl $0, 0x702b0(%rbx)\n   movl $0, 0x702b4(%rbx)\n"
    "91: test %r13, %r13\n   jz 96f\n   cmp 0x70260(%rbx), %r14\n   jbe 96f\n"
    "mov 0x70270(%rbx), %rax\n   cmp 0x70278(%rbx), %rax\n   jae 97f\n   add %r13, %rax\n"
    "jmp 98f\n"
    "97: mov %rax, %rcx\n   imul $1200, %r13, %rax\n   xor %edx, %edx\n   div %rcx\n"
    "lea 1(%rcx,%rax), %rax\n"
    "98: mov $307200, %ecx\n   cmp %rcx, %rax\n   cmova %rcx, %rax\n   mov %rax, 0x70270(%rbx)\n"
    "96: cmpl $-1, 0x7023c(%rbx)\n   je 95f\n   mov %rbx, %rdi\n   mov 32(%rsp), %rsi\n"
    "call waterlink_losses\n"
    "95: testb $1, 40(%rsp)\n   jz 99f\n   incl 0x702a8(%rbx)\n"
    "99: add $88, %rsp\n   pop %r15\n   pop %r14\n   pop %r13\n   pop %r12\n   pop %rbp\n"
    "pop %rbx\n"
    ASM_RET
    ASM_END(waterlink_apply)
);
#elif ARM64
/*
        x19 the link, x20 the part, x21 past the last, x22 the bytes newly
        delivered, x23 the latest transmission known to have arrived, x24
        its round trip, x25 the link plus 0x6f800, x26 whether frames came
        (bit 0) and acknowledgements did (bit 1), x27 the sink, x28 its
        context. On the stack past the saved registers: the body, now, a
        frame's head, and an acknowledgement's walk across the C it calls.
        A part: w9 its flags, w10 its key, w11 its first number, x12 its
        key's receiving entry less 0x600.
*/
__asm__(
    ASM_FUNC(waterlink_apply)
    "stp x29, x30, [sp, #-144]!\n   mov x29, sp\n   stp x19, x20, [sp, #16]\n"
    "stp x21, x22, [sp, #32]\n   stp x23, x24, [sp, #48]\n   stp x25, x26, [sp, #64]\n"
    "stp x27, x28, [sp, #80]\n"
    "mov x19, x0\n   mov x20, x2\n   add x21, x2, x3, lsl #4\n"
    "add x25, x0, #0x6f, lsl #12\n   add x25, x25, #0x800\n"
    "str x1, [sp, #96]\n   mov x26, #0\n   mov x27, x5\n   mov x28, x6\n"
    "ldr x9, [x25, #0xa48]\n   cmp x4, x9\n   b.ls 1f\n   str x4, [x25, #0xa48]\n"
    "1:  ldr x9, [x25, #0xa48]\n   str x9, [sp, #104]\n"
    "mov x22, #0\n   mov x23, #0\n   mov x24, #0\n   cmp x20, x21\n   b.hs 90f\n"
    "10: ldrb w9, [x20]\n   ldrb w10, [x20, #1]\n   ldr w11, [x20, #4]\n"
    "cmp w9, #8\n   b.eq 40f\n"
    //  A frame is owed an acknowledgement whatever becomes of it.
    "add x12, x10, x10, lsl #1\n   add x12, x25, x12, lsl #2\n"
    "ldr x13, [x25, #0xa10]\n   cbnz x13, 11f\n   ldr x14, [x25, #0xa48]\n"
    "str x14, [x25, #0xaa0]\n"
    "11: mov x14, #1\n   lsl x14, x14, x10\n   orr x13, x13, x14\n   str x13, [x25, #0xa10]\n"
    "orr x26, x26, #1\n   mov w14, #0x14\n   tst w9, w14\n   b.eq 12f\n   mov w14, #1\n"
    "strb w14, [x25, #0xaac]\n"
    "12: ldr w13, [x12, #0x600]\n   cmp w11, w13\n   b.ls 13f\n"
    "ldrb w14, [x12, #0x608]\n   cbnz w14, 13f\n   ldrb w14, [x12, #0x609]\n   cbnz w14, 14f\n"
    "tbz w9, #1, 15f\n   add w14, w13, #1\n   cmp w11, w14\n   b.ne 14f\n"
    "15: cbnz x27, 16f\n"
    //  Taken: the key moves on, and whatever it holds follows.
    "17: str w11, [x12, #0x600]\n   ldr x14, [x25, #0xad0]\n   add x14, x14, #1\n"
    "str x14, [x25, #0xad0]\n   tbz w9, #2, 18f\n   mov w14, #1\n   strb w14, [x12, #0x608]\n"
    "18: ldr w14, [x12, #0x604]\n   cmn w14, #1\n   b.ne 19f\n   strb wzr, [x12, #0x609]\n"
    "20: add x20, x20, #16\n   cmp x20, x21\n   b.lo 10b\n   b 90f\n"
    "19: mov x0, x19\n   mov w1, w10\n   mov x2, x27\n   mov x3, x28\n"
    "bl waterlink_release\n   b 20b\n"
    //  At or behind what was taken, or after its last: a copy, answered.
    "13: ldr x14, [x25, #0xad8]\n   add x14, x14, #1\n   str x14, [x25, #0xad8]\n"
    "mov w14, #1\n   strb w14, [x25, #0xaac]\n   b 20b\n"
    //  Ahead, or its key paused: held.
    "14: mov w14, #1\n   strb w14, [x25, #0xaac]\n"
    "ldrh w14, [x20, #8]\n   lsl x14, x14, #32\n   orr x14, x14, x11\n"
    "orr x14, x14, x10, lsl #48\n   orr x14, x14, x9, lsl #56\n   str x14, [sp, #112]\n"
    "ldrh w3, [x20, #2]\n   ldr x15, [sp, #96]\n   add x3, x15, x3\n   mov x0, x19\n"
    "add x1, x12, #0x600\n   add x2, sp, #112\n   bl waterlink_hold\n   b 20b\n"
    //  The application's say: the head goes by address, and nothing held
    //  in a caller's register survives the call.
    "16: ldrh w14, [x20, #8]\n   lsl x14, x14, #32\n   orr x14, x14, x11\n"
    "orr x14, x14, x10, lsl #48\n   orr x14, x14, x9, lsl #56\n   str x14, [sp, #112]\n"
    "ldrh w2, [x20, #2]\n   ldr x15, [sp, #96]\n   add x2, x15, x2\n   mov x0, x28\n"
    "add x1, sp, #112\n   blr x27\n"
    "ldrb w9, [sp, #119]\n   ldrb w10, [x20, #1]\n   ldr w11, [sp, #112]\n"
    "add x12, x10, x10, lsl #1\n   add x12, x25, x12, lsl #2\n   tst w0, #0xff\n   b.ne 17b\n"
    "ldrh w3, [x20, #2]\n   ldr x15, [sp, #96]\n   add x3, x15, x3\n   mov x0, x19\n"
    "add x1, x12, #0x600\n   add x2, sp, #112\n   bl waterlink_hold\n"
    "ldrb w10, [x20, #1]\n   add x12, x10, x10, lsl #1\n   add x12, x25, x12, lsl #2\n"
    "mov w14, #1\n   strb w14, [x12, #0x609]\n   b 20b\n"
    //  An acknowledgement: w11 what was taken, x13 the mask, x15 the key's
    //  sending entry less 0x200, w9 the slot, x12 its address, w10 the next
    //  on the key, w14 its sequence.
    "40: orr x26, x26, #2\n   ldr x13, [x20, #8]\n   add x15, x25, x10, lsl #4\n"
    "ldr w9, [x15, #0x204]\n"
    "41: cmn w9, #1\n   b.eq 20b\n"
    "mov w16, #1200\n   madd x12, x9, x16, x19\n   ldr w10, [x12, #28]\n   ldr w14, [x12, #16]\n"
    "cmp w14, w11\n   b.ls 42f\n"
    //  Past the mask: a key's slots are in sequence order, so the rest are.
    "sub w16, w14, w11\n   sub w16, w16, #1\n   cmp w16, #63\n   b.hi 20b\n"
    "lsr x17, x13, x16\n   tbz x17, #0, 49f\n"
    "42: ldrb w16, [x12, #36]\n   cmp w16, #2\n   b.ne 43f\n"
    //  In flight: what it carried is delivered and leaves the flight, and
    //  the newest transmission among them is the round trip's sample.
    "ldrh w16, [x12, #32]\n   add w17, w16, #4\n   cmp w16, #127\n   cinc w17, w17, hi\n"
    "cmp w14, #127\n   b.ls 45f\n"
    "clz w16, w14\n   eor w16, w16, #31\n   add w16, w16, w16, lsl #3\n   add w16, w16, #9\n"
    "lsr w16, w16, #6\n   add w17, w17, w16\n"
    "45: add x22, x22, x17\n   ldr x16, [x25, #0xa68]\n   sub x16, x16, x17\n"
    "str x16, [x25, #0xa68]\n"
    "ldr x16, [x12, #8]\n   cmp x16, x23\n   b.ls 46f\n   mov x23, x16\n   mov x24, #0\n"
    "ldrb w16, [x12, #37]\n   cmp w16, #1\n   b.ne 46f\n   ldr x24, [x25, #0xa48]\n"
    "ldr x16, [x12]\n   sub x24, x24, x16\n"
    "46: ldp w16, w17, [x12, #20]\n   mov w0, #1200\n   cmn w17, #1\n   b.eq 47f\n"
    "madd x1, x17, x0, x19\n   str w16, [x1, #20]\n   b 48f\n"
    "47: str w16, [x25, #0xa3c]\n"
    "48: cmn w16, #1\n   b.eq 50f\n   madd x1, x16, x0, x19\n   str w17, [x1, #24]\n   b 51f\n"
    "50: str w17, [x25, #0xa40]\n"
    "51: ldrb w16, [x12, #34]\n   add x16, x25, x16, lsl #4\n   ldrh w17, [x16, #0x20c]\n"
    "sub w17, w17, #1\n   strh w17, [x16, #0x20c]\n   mov w16, #-1\n   stp w16, w16, [x12, #20]\n"
    //  Taken is freed; past what was taken, held.
    "52: ldr w16, [x12, #16]\n   cmp w16, w11\n   b.hi 53f\n"
    "ldr x16, [x25, #0xb00]\n   add x16, x16, #1\n   str x16, [x25, #0xb00]\n"
    "ldr w16, [x15, #0x204]\n   cmp w16, w9\n   b.ne 54f\n   str w10, [x15, #0x204]\n"
    "ldr w16, [x15, #0x208]\n   cmp w16, w9\n   b.ne 55f\n   mov w16, #-1\n   str w16, [x15, #0x208]\n"
    "55: strb wzr, [x12, #36]\n   mov w16, #-1\n   str w16, [x12, #28]\n"
    "ldr w16, [x25, #0xa30]\n   str w16, [x12, #20]\n   str w9, [x25, #0xa30]\n"
    "ldr w16, [x25, #0xa34]\n   add w16, w16, #1\n   str w16, [x25, #0xa34]\n   b 49f\n"
    "53: mov w16, #3\n   strb w16, [x12, #36]\n"
    "49: mov w9, w10\n   b 41b\n"
    //  Queued, lost and not sent again yet: off its band. A slot freed from
    //  the middle of its chain: the C walks to it.
    "43: cmp w16, #1\n   b.ne 52b\n   stp w9, w10, [sp, #120]\n   str x11, [sp, #128]\n"
    "str x13, [sp, #136]\n   mov x0, x19\n   mov w1, w9\n   bl waterlink_band_remove\n"
    "ldp w9, w10, [sp, #120]\n   ldr x11, [sp, #128]\n   ldr x13, [sp, #136]\n"
    "mov w16, #1200\n   madd x12, x9, x16, x19\n"
    "ldrb w16, [x20, #1]\n   add x15, x25, x16, lsl #4\n   b 52b\n"
    "54: stp w9, w10, [sp, #120]\n   str x11, [sp, #128]\n   str x13, [sp, #136]\n"
    "mov x0, x19\n   mov w1, w9\n   bl waterlink_slot_free\n"
    "ldp w9, w10, [sp, #120]\n   ldr x11, [sp, #128]\n   ldr x13, [sp, #136]\n"
    "ldrb w16, [x20, #1]\n   add x15, x25, x16, lsl #4\n   b 49b\n"
    //  Once for the body: the latest arrival, its round trip into the
    //  estimate, the window grown by what was delivered after the last
    //  halving, and the losses that makes visible.
    "90: tbz x26, #1, 95f\n   cbz x23, 91f\n"
    "ldr x10, [x25, #0xa58]\n   cmp x23, x10\n   b.ls 92f\n   str x23, [x25, #0xa58]\n"
    "92: cbz x24, 93f\n   str x24, [x25, #0xa98]\n   ldr x10, [x25, #0xa80]\n   cbnz x10, 94f\n"
    "str x24, [x25, #0xa80]\n   lsr x11, x24, #1\n   str x11, [x25, #0xa88]\n   b 93f\n"
    "94: subs x11, x24, x10\n   sub x12, x10, x24\n   csel x11, x11, x12, hi\n"
    "ldr x12, [x25, #0xa88]\n   add x12, x12, x12, lsl #1\n   add x12, x12, x11\n"
    "lsr x12, x12, #2\n   str x12, [x25, #0xa88]\n"
    "lsl x12, x10, #3\n   sub x12, x12, x10\n   add x12, x12, x24\n   lsr x12, x12, #3\n"
    "str x12, [x25, #0xa80]\n"
    "93: str wzr, [x25, #0xab0]\n   str wzr, [x25, #0xab4]\n"
    "91: cbz x22, 96f\n   ldr x10, [x25, #0xa60]\n   cmp x23, x10\n   b.ls 96f\n"
    "ldr x10, [x25, #0xa70]\n   ldr x11, [x25, #0xa78]\n   cmp x10, x11\n   b.hs 97f\n"
    "add x10, x10, x22\n   b 98f\n"
    "97: mov x11, #1200\n   mul x11, x22, x11\n   udiv x11, x11, x10\n   add x10, x10, x11\n"
    "add x10, x10, #1\n"
    "98: cmp x10, #0x4b, lsl #12\n   b.ls 89f\n   mov x10, #0xb000\n   movk x10, #0x4, lsl #16\n"
    "89: str x10, [x25, #0xa70]\n"
    "96: ldr w10, [x25, #0xa3c]\n   cmn w10, #1\n   b.eq 95f\n   mov x0, x19\n"
    "ldr x1, [sp, #104]\n   bl waterlink_losses\n"
    "95: tbz x26, #0, 99f\n   ldr w10, [x25, #0xaa8]\n   add w10, w10, #1\n"
    "str w10, [x25, #0xaa8]\n"
    "99: ldp x19, x20, [sp, #16]\n   ldp x21, x22, [sp, #32]\n   ldp x23, x24, [sp, #48]\n"
    "ldp x25, x26, [sp, #64]\n   ldp x27, x28, [sp, #80]\n   ldp x29, x30, [sp], #144\n"
    ASM_RET
    ASM_END(waterlink_apply)
);
#elif RISCV64
/*
        s0 the link, s1 the part, s2 past the last, s3 the bytes newly
        delivered, s4 the latest transmission known to have arrived, s5 its
        round trip, s6 the link plus 0x6ff00, s7 whether frames came (bit 0)
        and acknowledgements did (bit 1), s8 the sink, s9 its context, s10
        the body, s11 now. On the stack: a frame's head, and an
        acknowledgement's walk across the C it calls. A part: t0 its flags,
        t1 its key, t2 its first number, t3 its key's receiving entry plus
        256.
*/
__asm__(
    ASM_FUNC(waterlink_apply)
    "addi sp, sp, -144\n   sd ra, 136(sp)\n   sd s0, 128(sp)\n   sd s1, 120(sp)\n"
    "sd s2, 112(sp)\n   sd s3, 104(sp)\n   sd s4, 96(sp)\n   sd s5, 88(sp)\n   sd s6, 80(sp)\n"
    "sd s7, 72(sp)\n   sd s8, 64(sp)\n   sd s9, 56(sp)\n   sd s10, 48(sp)\n   sd s11, 40(sp)\n"
    "mv s0, a0\n   mv s1, a2\n   slli t0, a3, 4\n   add s2, a2, t0\n"
    "lui s6, 0x70\n   addi s6, s6, -256\n   add s6, a0, s6\n"
    "mv s8, a5\n   mv s9, a6\n   mv s10, a1\n   li s7, 0\n"
    "ld t0, 840(s6)\n   bgeu t0, a4, 1f\n   sd a4, 840(s6)\n"
    "1:  ld s11, 840(s6)\n   li s3, 0\n   li s4, 0\n   li s5, 0\n   bgeu s1, s2, 90f\n"
    "10: lbu t0, 0(s1)\n   lbu t1, 1(s1)\n   lwu t2, 4(s1)\n   li t3, 8\n   beq t0, t3, 40f\n"
    //  A frame is owed an acknowledgement whatever becomes of it.
    "slli t3, t1, 1\n   add t3, t3, t1\n   slli t3, t3, 2\n   add t3, s6, t3\n"
    "ld t4, 784(s6)\n   bnez t4, 11f\n   ld t5, 840(s6)\n   sd t5, 928(s6)\n"
    "11: li t5, 1\n   sll t5, t5, t1\n   or t4, t4, t5\n   sd t4, 784(s6)\n   ori s7, s7, 1\n"
    "andi t4, t0, 0x14\n   beqz t4, 12f\n   li t4, 1\n   sb t4, 940(s6)\n"
    "12: lwu t4, -256(t3)\n   bgeu t4, t2, 13f\n   lbu t5, -248(t3)\n   bnez t5, 13f\n"
    "lbu t5, -247(t3)\n   bnez t5, 14f\n   andi t5, t0, 2\n   beqz t5, 15f\n"
    "addi t5, t4, 1\n   bne t2, t5, 14f\n"
    "15: bnez s8, 16f\n"
    //  Taken: the key moves on, and whatever it holds follows.
    "17: sw t2, -256(t3)\n   ld t5, 976(s6)\n   addi t5, t5, 1\n   sd t5, 976(s6)\n"
    "andi t5, t0, 4\n   beqz t5, 18f\n   li t5, 1\n   sb t5, -248(t3)\n"
    "18: lw t5, -252(t3)\n   bgez t5, 19f\n   sb zero, -247(t3)\n"
    "20: addi s1, s1, 16\n   bltu s1, s2, 10b\n   j 90f\n"
    "19: mv a0, s0\n   mv a1, t1\n   mv a2, s8\n   mv a3, s9\n   call waterlink_release\n"
    "j 20b\n"
    //  At or behind what was taken, or after its last: a copy, answered.
    "13: ld t5, 984(s6)\n   addi t5, t5, 1\n   sd t5, 984(s6)\n   li t5, 1\n   sb t5, 940(s6)\n"
    "j 20b\n"
    //  Ahead, or its key paused: held.
    "14: li t5, 1\n   sb t5, 940(s6)\n"
    "lhu t5, 8(s1)\n   slli t5, t5, 32\n   or t5, t5, t2\n   slli t6, t1, 48\n   or t5, t5, t6\n"
    "slli t6, t0, 56\n   or t5, t5, t6\n   sd t5, 0(sp)\n"
    "lhu a3, 2(s1)\n   add a3, s10, a3\n   mv a0, s0\n   addi a1, t3, -256\n   mv a2, sp\n"
    "call waterlink_hold\n   j 20b\n"
    //  The application's say: the head goes by address, and nothing held
    //  in a caller's register survives the call.
    "16: lhu t5, 8(s1)\n   slli t5, t5, 32\n   or t5, t5, t2\n   slli t6, t1, 48\n   or t5, t5, t6\n"
    "slli t6, t0, 56\n   or t5, t5, t6\n   sd t5, 0(sp)\n"
    "lhu a2, 2(s1)\n   add a2, s10, a2\n   mv a0, s9\n   mv a1, sp\n   jalr s8\n"
    "lbu t0, 7(sp)\n   lbu t1, 1(s1)\n   lwu t2, 0(sp)\n"
    "slli t3, t1, 1\n   add t3, t3, t1\n   slli t3, t3, 2\n   add t3, s6, t3\n"
    "andi a0, a0, 0xff\n   bnez a0, 17b\n"
    "lhu a3, 2(s1)\n   add a3, s10, a3\n   mv a0, s0\n   addi a1, t3, -256\n   mv a2, sp\n"
    "call waterlink_hold\n"
    "lbu t1, 1(s1)\n   slli t3, t1, 1\n   add t3, t3, t1\n   slli t3, t3, 2\n   add t3, s6, t3\n"
    "li t5, 1\n   sb t5, -247(t3)\n   j 20b\n"
    //  An acknowledgement: t2 what was taken, t3 the mask, t4 the key's
    //  sending entry plus 1280, t0 the slot, a0 its address, t1 the next on
    //  the key, t5 its sequence.
    "40: ori s7, s7, 2\n   ld t3, 8(s1)\n   slli t4, t1, 4\n   add t4, s6, t4\n"
    "lw t0, -1276(t4)\n"
    "41: bltz t0, 20b\n"
    "li t5, 1200\n   mul a0, t0, t5\n   add a0, s0, a0\n   lw t1, 28(a0)\n   lwu t5, 16(a0)\n"
    "bgeu t2, t5, 42f\n"
    //  Past the mask: a key's slots are in sequence order, so the rest are.
    "sub t6, t5, t2\n   addi t6, t6, -1\n   li a1, 64\n   bgeu t6, a1, 20b\n"
    "srl a1, t3, t6\n   andi a1, a1, 1\n   beqz a1, 49f\n"
    "42: lbu t6, 36(a0)\n   li a1, 2\n   bne t6, a1, 43f\n"
    //  In flight: what it carried is delivered and leaves the flight, and
    //  the newest transmission among them is the round trip's sample.
    "lhu a1, 32(a0)\n   addi a2, a1, 4\n   li a3, 128\n   bltu a1, a3, 44f\n   addi a2, a2, 1\n"
    "44: srli a1, t5, 7\n   beqz a1, 45f\n"
    "56: addi a2, a2, 1\n   srli a1, a1, 7\n   bnez a1, 56b\n"
    "45: add s3, s3, a2\n   ld a1, 872(s6)\n   sub a1, a1, a2\n   sd a1, 872(s6)\n"
    "ld a1, 8(a0)\n   bgeu s4, a1, 46f\n   mv s4, a1\n   li s5, 0\n   lbu a1, 37(a0)\n"
    "li a2, 1\n   bne a1, a2, 46f\n   ld s5, 840(s6)\n   ld a1, 0(a0)\n   sub s5, s5, a1\n"
    "46: lw a1, 20(a0)\n   lw a2, 24(a0)\n   bltz a2, 47f\n"
    "li a3, 1200\n   mul a3, a2, a3\n   add a3, s0, a3\n   sw a1, 20(a3)\n   j 48f\n"
    "47: sw a1, 828(s6)\n"
    "48: bltz a1, 50f\n   li a3, 1200\n   mul a3, a1, a3\n   add a3, s0, a3\n   sw a2, 24(a3)\n"
    "j 51f\n"
    "50: sw a2, 832(s6)\n"
    "51: lbu a1, 34(a0)\n   slli a1, a1, 4\n   add a1, s6, a1\n   lhu a2, -1268(a1)\n"
    "addi a2, a2, -1\n   sh a2, -1268(a1)\n   li a1, -1\n   sw a1, 20(a0)\n   sw a1, 24(a0)\n"
    //  Taken is freed; past what was taken, held.
    "52: lwu a1, 16(a0)\n   bltu t2, a1, 53f\n"
    "ld a1, 1024(s6)\n   addi a1, a1, 1\n   sd a1, 1024(s6)\n"
    "lw a1, -1276(t4)\n   bne a1, t0, 54f\n   sw t1, -1276(t4)\n"
    "lw a1, -1272(t4)\n   bne a1, t0, 55f\n   li a1, -1\n   sw a1, -1272(t4)\n"
    "55: sb zero, 36(a0)\n   li a1, -1\n   sw a1, 28(a0)\n   lw a1, 816(s6)\n   sw a1, 20(a0)\n"
    "sw t0, 816(s6)\n   lw a1, 820(s6)\n   addi a1, a1, 1\n   sw a1, 820(s6)\n   j 49f\n"
    "53: li a1, 3\n   sb a1, 36(a0)\n"
    "49: mv t0, t1\n   j 41b\n"
    //  Queued, lost and not sent again yet: off its band. A slot freed from
    //  the middle of its chain: the C walks to it.
    "43: li a1, 1\n   bne t6, a1, 52b\n"
    "sd t0, 8(sp)\n   sd t1, 16(sp)\n   sd t2, 24(sp)\n   sd t3, 32(sp)\n"
    "mv a0, s0\n   mv a1, t0\n   call waterlink_band_remove\n"
    "ld t0, 8(sp)\n   ld t1, 16(sp)\n   ld t2, 24(sp)\n   ld t3, 32(sp)\n"
    "li a1, 1200\n   mul a0, t0, a1\n   add a0, s0, a0\n"
    "lbu t4, 1(s1)\n   slli t4, t4, 4\n   add t4, s6, t4\n   j 52b\n"
    "54: sd t1, 16(sp)\n   sd t2, 24(sp)\n   sd t3, 32(sp)\n"
    "mv a0, s0\n   mv a1, t0\n   call waterlink_slot_free\n"
    "ld t1, 16(sp)\n   ld t2, 24(sp)\n   ld t3, 32(sp)\n"
    "lbu t4, 1(s1)\n   slli t4, t4, 4\n   add t4, s6, t4\n   j 49b\n"
    //  Once for the body: the latest arrival, its round trip into the
    //  estimate, the window grown by what was delivered after the last
    //  halving, and the losses that makes visible.
    "90: andi t0, s7, 2\n   beqz t0, 95f\n   beqz s4, 91f\n"
    "ld t0, 856(s6)\n   bgeu t0, s4, 92f\n   sd s4, 856(s6)\n"
    "92: beqz s5, 93f\n   sd s5, 920(s6)\n   ld t0, 896(s6)\n   bnez t0, 94f\n"
    "sd s5, 896(s6)\n   srli t1, s5, 1\n   sd t1, 904(s6)\n   j 93f\n"
    "94: sub t1, s5, t0\n   bltu t0, s5, 57f\n   sub t1, t0, s5\n"
    "57: ld t2, 904(s6)\n   slli t3, t2, 1\n   add t2, t2, t3\n   add t2, t2, t1\n"
    "srli t2, t2, 2\n   sd t2, 904(s6)\n"
    "slli t2, t0, 3\n   sub t2, t2, t0\n   add t2, t2, s5\n   srli t2, t2, 3\n   sd t2, 896(s6)\n"
    "93: sw zero, 944(s6)\n   sw zero, 948(s6)\n"
    "91: beqz s3, 96f\n   ld t0, 864(s6)\n   bgeu t0, s4, 96f\n"
    "ld t0, 880(s6)\n   ld t1, 888(s6)\n   bgeu t0, t1, 97f\n   add t0, t0, s3\n   j 98f\n"
    "97: li t1, 1200\n   mul t1, s3, t1\n   divu t1, t1, t0\n   add t0, t0, t1\n   addi t0, t0, 1\n"
    "98: li t1, 307200\n   bgeu t1, t0, 89f\n   mv t0, t1\n"
    "89: sd t0, 880(s6)\n"
    "96: lw t0, 828(s6)\n   bltz t0, 95f\n   mv a0, s0\n   mv a1, s11\n   call waterlink_losses\n"
    "95: andi t0, s7, 1\n   beqz t0, 99f\n   lw t0, 936(s6)\n   addi t0, t0, 1\n   sw t0, 936(s6)\n"
    "99: ld ra, 136(sp)\n   ld s0, 128(sp)\n   ld s1, 120(sp)\n   ld s2, 112(sp)\n"
    "ld s3, 104(sp)\n   ld s4, 96(sp)\n   ld s5, 88(sp)\n   ld s6, 80(sp)\n   ld s7, 72(sp)\n"
    "ld s8, 64(sp)\n   ld s9, 56(sp)\n   ld s10, 48(sp)\n   ld s11, 40(sp)\n   addi sp, sp, 144\n"
    ASM_RET
    ASM_END(waterlink_apply)
);
#endif

// A body judged and then applied: what every caller without a judge wants.
bool waterlink_deliver(struct waterlink_link address_to link,
                          address_any body, positive length, p64 now,
                          waterlink_sink sink, address_any context)
{
        struct waterlink_part parts[WATERLINK_PARTS];
        bipolar count = waterlink_judge(body, length, parts);

        if (count < 0)
                return false;
        waterlink_apply(link, body, parts, (positive)count, now, sink, context);
        return true;
}

/*
        Whether a datagram's counter is one this session has not seen.

        Accepting marks it, so this is asked once per datagram and only after
        the tag has verified -- a counter taken from an unauthenticated header
        would let anyone at all slide the window forward and lock the session
        out of its own traffic.

        RFC 6479's window: a ring of 64-bit blocks, each counter's bit in the
        block its high bits name. The top moving forward clears whole blocks
        between the old top's and the new one's -- one spare block past the
        window's width is what lets clearing be whole blocks and still keep
        every counter the window promises. Nothing shifts, so an arrival in
        order costs one word.
*/
#define WATERLINK_REPLAY_BLOCKS (WATERLINK_REPLAY_WINDOW / 64 + 1)

_Static_assert(WATERLINK_REPLAY_WINDOW % 64 == 0,
               "the replay window is whole blocks");

bool waterlink_replay_new(struct waterlink_replay address_to window, p64 counter)
{
        p64 block = counter >> 6;
        p64 bit = 1ull << (counter & 63);
        p64 address_to word = window->seen + block % WATERLINK_REPLAY_BLOCKS;

        if (counter > window->top)
        {
                p64 top = window->top >> 6;
                p64 steps = block - top < WATERLINK_REPLAY_BLOCKS
                                    ? block - top
                                    : WATERLINK_REPLAY_BLOCKS;

                for (p64 step = 1; step <= steps; step++)
                        window->seen[(top + step) % WATERLINK_REPLAY_BLOCKS] = 0;
                window->top = counter;
        }
        else if (window->top - counter >= WATERLINK_REPLAY_WINDOW ||
                 (address_to word & bit))
                return false;

        address_to word |= bit;
        return true;
}

#endif // WATERLINK_LINK_INCLUDED
