/*
        test/checks/waterlink.c: the waterlink and its service.

        A run of test/checks.c, which includes this file where the run stood (test/split.py cut it there): it is
        compiled only as part of that file, with its statics and macros, and never on its own. A section is built as
        test/checks.c is, from the repository root: a freestanding one by gcc -O2 -static -nostdlib -nostartfiles
        -march=x86-64 -w -DCHECK_<name> -fwhole-program -o OUT test/checks.c (sh test/run builds it with its
        freestanding_check), a hosted one as test/run builds it (gcc -std=gnu11 -O2 -D_GNU_SOURCE -DCHECK_<name>
        test/checks.c).
*/

#ifdef CHECK_waterlink
#include "../../src/lib.util.c"
#include "../../src/net/net.c"
#include "../../src/waterlink/link.c"
#include "../../src/waterlink/seal.c"
#include "../../src/waterlink/handshake.c"
#include "../../src/waterlink/discover.c"
#include "../../src/net/discovery.c"
#define SHARED_counted
#include "../checks.c"
#undef SHARED_counted
/*
        Waterlink's core, which is a pure transform and so can be tested to
        the end with no socket, no clock and no key.

        The three rules that are bugs if they are wrong get their own cases:
        supersession keeps a frame's place, a durable frame is never jumped or
        dropped, and a frame that arrives behind the one that replaced it is
        refused rather than applied backwards. The rest is bounds.
*/

static struct waterlink_link one;

static p64 heard_key[64];
static p32 heard_sequence[64];
static p16 heard_length[64];
static p8 heard_first[64];
static positive heard;

static p8 hear(address_any context, struct waterlink_frame address_to head,
                 p8 address_to payload)
{
        (void)context;

        if (heard >= 64)
                return true;

        heard_key[heard] = head->key;
        heard_sequence[heard] = head->sequence;
        heard_length[heard] = head->length;
        heard_first[heard] = head->length ? payload[0] : 0;
        heard++;
        return true;
}

//      One datagram out of the sender and straight into the receiver, which
//      is the whole link minus the two things this file does not do.
static positive carry(p64 now, p8 address_to alone)
{
        p8 body[WATERLINK_PAYLOAD];
        positive used = waterlink_fill(address_of one, body, now, alone);

        heard = 0;
        if (used)
                check("a filled body walks back out",
                      waterlink_deliver(address_of one, body, used, one.clock, hear,
                                        null));
        return used;
}

static fn post_one(p8 key, p8 flags, p8 mark, p64 now)
{
        p8 payload[4];

        payload[0] = mark;
        payload[1] = mark;
        payload[2] = mark;
        payload[3] = mark;
        waterlink_post(address_of one, key, flags, payload, 4, now);
}

static fn refusals(void)
{
        p8 payload[4] = { 1, 1, 1, 1 };
        p64 before;

        waterlink_link_reset(address_of one);
        before = one.refused;

        //      Neither bit says nothing about whether it may be dropped, and
        //      both bits say two things at once.
        check("a frame with no class is refused",
              !waterlink_post(address_of one, 1, 0, payload, 4, 0));
        check("a frame with both classes is refused",
              !waterlink_post(address_of one, 1,
                              WATERLINK_FRAME_REPLACEABLE |
                                      WATERLINK_FRAME_DURABLE,
                              payload, 4, 0));

        //      A bit the link does not know is a meaning the far side would
        //      have to guess, and the acknowledgement bit is the link's own.
        check("a frame with a flag this link does not know is refused",
              !waterlink_post(address_of one, 1, WATERLINK_FRAME_DURABLE | 0x40,
                              payload, 4, 0));
        check("a caller's frame marked as an acknowledgement is refused",
              !waterlink_post(address_of one, 1,
                              WATERLINK_FRAME_DURABLE | WATERLINK_FRAME_ACK,
                              payload, 4, 0));
        check("a frame larger than a datagram is refused",
              !waterlink_post(address_of one, 2, WATERLINK_FRAME_DURABLE,
                              payload, WATERLINK_FRAME_MAX + 1, 0));
        check("a key past the last is refused",
              !waterlink_post(address_of one, WATERLINK_KEYS,
                              WATERLINK_FRAME_DURABLE, payload, 4, 0));

        check("every refusal was counted", one.refused - before == 6);
}

static fn supersession(void)
{
        p8 alone = false;

        waterlink_link_reset(address_of one);

        //      Three states for one key. Only the last is worth sending, and
        //      it must arrive where the first one stood.
        post_one(7, WATERLINK_FRAME_REPLACEABLE, 'a', 0);
        post_one(9, WATERLINK_FRAME_REPLACEABLE, 'x', 0);
        post_one(7, WATERLINK_FRAME_REPLACEABLE, 'b', 0);
        post_one(7, WATERLINK_FRAME_REPLACEABLE, 'c', 0);

        check("two supersessions happened", one.superseded == 2);
        check("only two slots were ever posted", one.posted == 2);

        carry(0, address_of alone);
        check("both keys arrived", heard == 2);
        check("the superseded key kept its place", heard_key[0] == 7);
        check("and carries the newest value", heard_first[0] == 'c');
        check("the untouched key is behind it", heard_key[1] == 9);
        check("a replaceable run needs no urgency", !alone);
}

static fn class_is_the_keys(void)
{
        p8 alone = false;

        waterlink_link_reset(address_of one);

        //      A key is a stream or a register, and its first frame says
        //      which: a frame of the other class on it is refused, so no
        //      frame ever has to name the one it comes after.
        post_one(7, WATERLINK_FRAME_REPLACEABLE, 'a', 0);
        check("a stream frame on a register key is refused",
              !waterlink_post(address_of one, 7, WATERLINK_FRAME_DURABLE,
                              (p8 address_to) "S", 1, 0));
        post_one(8, WATERLINK_FRAME_DURABLE, 'a', 0);
        post_one(8, WATERLINK_FRAME_DURABLE, 'b', 0);
        check("a register frame on a stream key is refused",
              !waterlink_post(address_of one, 8, WATERLINK_FRAME_REPLACEABLE,
                              (p8 address_to) "r", 1, 0));
        check("and a stream is never superseded", one.superseded == 0);

        carry(0, address_of alone);
        check("all three went out", heard == 3);
        check("the stream in the order it was posted, from sequence one",
              heard_key[1] == 8 && heard_first[1] == 'a' &&
                      heard_sequence[1] == 1 && heard_first[2] == 'b' &&
                      heard_sequence[2] == 2);
}


static fn urgency(void)
{
        p8 alone = false;

        waterlink_link_reset(address_of one);

        post_one(1, WATERLINK_FRAME_DURABLE, 'B', 0);
        post_one(2, WATERLINK_FRAME_URGENT | WATERLINK_FRAME_DURABLE, 'U', 0);

        //      An urgent frame cannot wait in a segment run for the frames
        //      behind it, so the datagram carrying it says so.
        carry(0, address_of alone);
        check("the urgent frame went first", heard_first[0] == 'U');
        check("its datagram must travel alone", alone);

        //      The other frame rides along in the same datagram rather than
        //      waiting for the next one. It was already queued and the urgent
        //      frame is in front of it, so nothing is delayed.
        check("the other frame rode along behind it", heard == 2);
        check("and it is the other one", heard_first[1] == 'B');

        //      What comes back is the acknowledgement of both, which frees
        //      them, and then there is nothing.
        check("the acknowledgements go out", carry(0, address_of alone) > 0);
        check("and deliver nothing to the application", heard == 0);
        check("nothing is left to send", carry(0, address_of alone) == 0);
        check("and an empty fill claims no urgency", !alone);
        check("the acknowledged link is idle", waterlink_idle(address_of one));
}

static fn stale_arrivals(void)
{
        p8 body[WATERLINK_PAYLOAD];
        p8 first[WATERLINK_PAYLOAD];
        positive used;
        positive kept;
        p8 alone = false;

        waterlink_link_reset(address_of one);

        post_one(7, WATERLINK_FRAME_REPLACEABLE, 'a', 0);
        kept = waterlink_fill(address_of one, first, 0, address_of alone);

        post_one(7, WATERLINK_FRAME_REPLACEABLE, 'b', 0);
        used = waterlink_fill(address_of one, body, 0, address_of alone);

        heard = 0;
        waterlink_deliver(address_of one, body, used, one.clock, hear, null);
        check("the newer frame was delivered", heard == 1);

        //      The older frame arrives after the one that replaced it. This
        //      is the receiving half of supersession: it is dropped, not
        //      applied, or a window's damage lands after the window closed.
        heard = 0;
        check("a stale frame is still well formed",
              waterlink_deliver(address_of one, first, kept, one.clock, hear, null));
        check("but nothing was delivered from it", heard == 0);
        check("and it was counted stale", one.stale == 1);
}

/*
        Two keystrokes on one key, in two datagrams the network swaps. Both
        are durable, so both must arrive and in the order they were typed:
        the later one waits for the earlier rather than being taken as the
        newer state and the earlier then dropped as stale -- which is right
        for a replaceable frame and loses a keystroke for a durable one.
*/
static fn durable_reordered(void)
{
        p8 early[WATERLINK_PAYLOAD];
        p8 late[WATERLINK_PAYLOAD];
        positive early_used;
        positive late_used;
        positive before_early;
        p8 alone = false;

        waterlink_link_reset(address_of one);
        post_one(7, WATERLINK_FRAME_DURABLE, 'a', 0);
        early_used = waterlink_fill(address_of one, early, 0, address_of alone);
        post_one(7, WATERLINK_FRAME_DURABLE, 'b', 0);
        late_used = waterlink_fill(address_of one, late, 0, address_of alone);

        heard = 0;
        check("the later datagram is well formed",
              waterlink_deliver(address_of one, late, late_used, one.clock, hear, null));
        before_early = heard;
        check("the earlier datagram is well formed",
              waterlink_deliver(address_of one, early, early_used, one.clock, hear,
                                null));

        check("the later keystroke waits for the earlier one",
              before_early == 0);
        check("both keystrokes arrive though the network swapped them",
              heard == 2);
        check("in the order they were typed",
              heard == 2 && heard_first[0] == 'a' && heard_first[1] == 'b');
}

static fn malformed_bodies(void)
{
        //      Bodies by hand, each one thing wrong, the rest of it a good
        //      stream frame on key 7: flags 2, key 7, sequence 1, length 1,
        //      then the payload.
        static const struct {
                p8 bytes[16];
                positive length;
        } wrong[] = {
                {{2, 7, 0x81, 0x00, 1, 'a'}, 6},             // sequence in two bytes
                {{2, 7, 0, 1, 'a'}, 5},                      // sequence zero
                {{2, 7, 0xff, 0xff, 0xff, 0xff, 0x0f, 1, 'a'}, 9}, // 2^32-1
                {{2, 7, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                  0x02, 1, 'a'}, 14},                        // past 64 bits
                {{0x42, 7, 1, 1, 'a'}, 5},                   // a flag it does not know
                {{3, 7, 1, 1, 'a'}, 5},                      // both classes
                {{0x0a, 7, 1, 1, 'a'}, 5},                   // a class on an ack
                {{2, WATERLINK_KEYS, 1, 1, 'a'}, 5},         // a key past the last
                {{8, 7, 1, 0x80, 0x00}, 5},                  // an ack's mask in two
                {{8, 7, 0xff, 0xff, 0xff, 0xff, 0x0f, 0}, 8}, // taken 2^32-1
                {{2, 7, 1, 1, 'a', 0, 5}, 7},                // bytes after the end
                {{2, 7, 1, 9, 'a'}, 5},                      // a length past the body
                {{2, 7, 1}, 3},                              // cut inside its header
                {{2, 7, 1, 0x80}, 4},                        // cut inside a number
                {{2}, 1},                                    // a flags byte alone
                {{8, 7, 1, 0, 8, 7, 2, 0}, 8},               // one key acknowledged twice
        };
        static const p8 widest[] = {1, WATERLINK_KEYS - 1, 0xfe, 0xff, 0xff,
                                    0xff, 0x0f, 1, 'z'};
        p8 body[WATERLINK_PAYLOAD];
        positive used;
        positive refused = 0;
        p8 alone = false;

        waterlink_link_reset(address_of one);
        post_one(7, WATERLINK_FRAME_DURABLE, 'a', 0);
        used = waterlink_fill(address_of one, body, 0, address_of alone);

        check("a stream frame's header is four bytes on the wire",
              used == 4 + 4 && body[0] == WATERLINK_FRAME_DURABLE &&
                      body[1] == 7 && body[2] == 1 && body[3] == 4);
        check("a body longer than a datagram is refused",
              !waterlink_deliver(address_of one, body, WATERLINK_PAYLOAD + 1, one.clock,
                                 null, null));
        check("a body cut inside its payload is refused",
              !waterlink_deliver(address_of one, body, used - 1, one.clock, null, null));

        for (positive at = 0; at < sizeof wrong / sizeof wrong[0]; at++)
        {
                waterlink_link_reset(address_of one);
                refused += !waterlink_deliver(address_of one,
                                              (p8 address_to)wrong[at].bytes,
                                              wrong[at].length, one.clock, null, null);
        }
        check("a second spelling, a sequence out of range, an unknown or mixed "
              "flag, a key past the last, a number past 64 bits, bytes after "
              "the end, a length past the body, a cut header and a key "
              "acknowledged twice are each refused",
              refused == sizeof wrong / sizeof wrong[0]);
        {
                static const p8 two[] = {8, 7, 1, 0, 8, 9, 1, 0};

                waterlink_link_reset(address_of one);
                check("sec: two keys acknowledged in one body are taken",
                      waterlink_deliver(address_of one, (p8 address_to)two,
                                        sizeof two, one.clock, null, null));
        }

        waterlink_link_reset(address_of one);
        heard = 0;
        check("and the last key and the widest sequence read back",
              waterlink_deliver(address_of one, (p8 address_to)widest,
                                sizeof widest, one.clock, hear, null) &&
                      heard == 1 && heard_key[0] == WATERLINK_KEYS - 1 &&
                      heard_sequence[0] == 0xfffffffeu && heard_first[0] == 'z');

        /* A malformed suffix must make the entire authenticated body inert,
           not deliver its valid prefix and only then report failure. */
        {
                static const p8 mixed[] = {
                    WATERLINK_FRAME_DURABLE, 7, 1, 1, 'x',
                    WATERLINK_FRAME_DURABLE, 7, 2, 9, 'y'};
                struct waterlink_link before;

                waterlink_link_reset(address_of one);
                before = one;
                heard = 0;
                check("sec: a malformed body with a valid prefix is refused",
                      !waterlink_deliver(address_of one,
                                         (p8 address_to)mixed, sizeof mixed,
                                         100, hear, null));
                check("sec: refusing the malformed whole delivers no prefix",
                      heard == 0);
                check("sec: and changes none of the transport state",
                      !memory_compare(address_of one, address_of before,
                                      sizeof one));
        }
}

/*
        The judge's shortest inputs, every one: no frame or acknowledgement
        fits in two bytes, and three hold a flags byte, a key and one number
        of the two a part needs, so exactly the strings of zeros are bodies
        and they hold nothing.
*/
static fn judge_short_exhaustive(void)
{
        struct waterlink_part parts[WATERLINK_PARTS];
        static const p8 keys[] = {0, 63, 64};
        p8 body[3];
        positive cases = 0;
        positive wrong = 0;

        wrong += waterlink_judge(body, 0, parts) != 0;
        cases++;
        for (positive first = 0; first < 256; first++)
        {
                body[0] = (p8)first;
                wrong += waterlink_judge(body, 1, parts) != (first ? -1 : 0);
                cases++;
                for (positive second = 0; second < 256; second++)
                {
                        body[1] = (p8)second;
                        wrong += waterlink_judge(body, 2, parts) !=
                                 (first || second ? -1 : 0);
                        cases++;
                }
                for (positive key = 0; key < array_count(keys); key++)
                        for (positive third = 0; third < 256; third++)
                        {
                                body[1] = keys[key];
                                body[2] = (p8)third;
                                wrong += waterlink_judge(body, 3, parts) !=
                                         (first || keys[key] || third ? -1 : 0);
                                cases++;
                        }
        }
        check("the judge agrees on every body of two bytes and every "
              "three-byte one around the key's bound",
              cases == 1 + 256 + 256 * 256 + 256 * 3 * 256 && wrong == 0);
}

/* Every cut through a compound frame/ack body. The only accepted prefixes
   are the empty body, the exact end of the frame, the exact end of the ack,
   and authenticated zero padding after it. Every other cut is inside a
   header, varint or payload and must be both refused and state-atomic. */
static fn body_prefix_boundaries(void)
{
        p8 body[26] = {WATERLINK_FRAME_DURABLE, 7, 1, 3, 'a', 'b', 'c',
                       WATERLINK_FRAME_ACK, 7, 4, 0};
        positive wrong = 0;
        positive changed = 0;
        positive delivered = 0;

        for (positive length = 0; length <= sizeof body; length++)
        {
                p8 want = length == 0 || length == 7 || length >= 11;
                struct waterlink_link before;
                p8 got;

                waterlink_link_reset(address_of one);
                before = one;
                heard = 0;
                got = waterlink_deliver(address_of one, body, length, 100,
                                        hear, null);
                wrong += got != want;
                if (!want)
                        changed += heard || memory_compare(address_of one,
                                                          address_of before,
                                                          sizeof one);
                else if (length >= 7)
                        delivered += heard == 1 && heard_first[0] == 'a';
        }

        check("every cut through a frame, ack and padding has one answer",
              wrong == 0);
        check("every malformed cut is transport- and application-atomic",
              changed == 0);
        check("every complete-frame prefix delivers exactly that frame",
              delivered == 1 + sizeof body - 11 + 1);
}

/*
        An independent reading of the wire for the fuzzer below. It never
        calls the production reader and spells its bounds as numbers, so
        agreement is two readings of the rules, not the code certifying
        itself; and it says what a body holds, part for part, so the judge
        must agree on every word it hands the application, not only on yes.
*/
static p8 judge_model_number(p8 address_to body, positive length,
                               positive address_to at, p64 address_to value)
{
        p64 got = 0;

        for (positive byte = 0; byte < 10 && address_to at < length; byte++)
        {
                p8 one = body[(address_to at)++];

                if (byte == 9 && one > 1)
                        return false;
                got |= (p64)(one & 0x7f) << (7 * byte);
                if (!(one & 0x80))
                {
                        if (byte && !one)
                                return false;
                        address_to value = got;
                        return true;
                }
        }
        return false;
}

static bipolar judge_model(p8 address_to body, positive length,
                           struct waterlink_part address_to parts)
{
        positive at = 0;
        positive count = 0;
        p64 acknowledged = 0;

        if (length > 1168)
                return -1;
        while (at + 1 < length && body[at])
        {
                p8 flags = body[at++];
                p8 key = body[at++];
                p64 first, second;
                p8 replaceable = (flags & 1) != 0;
                p8 durable = (flags & 2) != 0;

                if (key >= 64 ||
                    !judge_model_number(body, length, address_of at,
                                        address_of first) ||
                    !judge_model_number(body, length, address_of at,
                                        address_of second))
                        return -1;
                parts[count].flags = flags;
                parts[count].key = key;
                parts[count].at = (p16)at;
                parts[count].number = (p32)first;
                parts[count].more = second;
                count++;
                if (flags == 8)
                {
                        if (first >= 0xffffffffu || (acknowledged >> key & 1))
                                return -1;
                        acknowledged |= 1ull << key;
                        continue;
                }
                if (replaceable == durable || (flags & ~0x17u) || !first ||
                    first >= 0xffffffffu || second > 1159 || second > length - at)
                        return -1;
                at += (positive)second;
        }
        for (; at < length; at++)
                if (body[at])
                        return -1;
        return (bipolar)count;
}

/*
        Applying as it was in C, before waterlink_apply was each machine's
        assembly: the reference it must match, and the acknowledging and
        settling judge_walk below applies through as well. apply_seen counts
        the paths each call took.
*/
enum {
        APPLY_ACK, APPLY_ACK_TAKEN, APPLY_ACK_HELD, APPLY_ACK_PASSED,
        APPLY_ACK_BEYOND, APPLY_ACK_FLIGHT, APPLY_ACK_QUEUED, APPLY_ACK_LATEST, APPLY_ACK_SAMPLE,
        APPLY_ACK_UNSAMPLED, APPLY_FREE_HEAD, APPLY_FREE_WALK, APPLY_FREE_LAST,
        APPLY_LARGEST, APPLY_ESTIMATE_FIRST, APPLY_ESTIMATE, APPLY_GROW_SLOW,
        APPLY_GROW_AVOID, APPLY_GROW_CAP, APPLY_SETTLE_LOSSES, APPLY_FRAME,
        APPLY_STALE, APPLY_OVER, APPLY_HOLD_PAUSED, APPLY_HOLD_AHEAD,
        APPLY_REFUSED, APPLY_HANDED, APPLY_LAST, APPLY_RELEASE_HELD,
        APPLY_OWED_FIRST, APPLY_OWED_NOW, APPLY_CLOCK_BEHIND, APPLY_NO_SINK,
        APPLY_SEEN
};

static positive apply_seen[APPLY_SEEN];

static fn apply_walk_acknowledge(struct waterlink_link address_to link, p8 key,
                                 p32 delivered, p64 mask, p64 address_to newly,
                                 p64 address_to latest, p64 address_to sample)
{
        p32 at = link->sending[key].first;

        while (at != WATERLINK_NONE)
        {
                struct waterlink_slot address_to slot = link->slot + at;
                p32 next = slot->chain;
                p32 gap = slot->sequence - delivered - 1;
                p8 taken = slot->sequence <= delivered;

                //      A key's slots are in sequence order: past the mask,
                //      the rest are too.
                if (!taken && gap >= WATERLINK_ACK_MASK)
                {
                        apply_seen[APPLY_ACK_BEYOND]++;
                        break;
                }
                if (!taken && !(mask & (1ull << gap)))
                {
                        apply_seen[APPLY_ACK_PASSED]++;
                        at = next;
                        continue;
                }
                if (slot->state == WATERLINK_SLOT_FLIGHT)
                {
                        apply_seen[APPLY_ACK_FLIGHT]++;
                        address_to newly += waterlink_bytes(slot);
                        if (slot->serial > address_to latest)
                        {
                                apply_seen[APPLY_ACK_LATEST]++;
                                apply_seen[slot->tries == 1 ? APPLY_ACK_SAMPLE
                                                            : APPLY_ACK_UNSAMPLED]++;
                                address_to latest = slot->serial;
                                address_to sample = slot->tries == 1
                                                            ? link->clock - slot->sent
                                                            : 0;
                        }
                }
                else if (slot->state == WATERLINK_SLOT_QUEUED)
                        apply_seen[APPLY_ACK_QUEUED]++;
                waterlink_slot_unqueue(link, at);
                if (taken)
                {
                        apply_seen[APPLY_ACK_TAKEN]++;
                        apply_seen[link->sending[key].first == at ? APPLY_FREE_HEAD
                                                                  : APPLY_FREE_WALK]++;
                        apply_seen[APPLY_FREE_LAST] += link->sending[key].last == at;
                        link->acked++;
                        waterlink_slot_free(link, at);
                }
                else
                {
                        apply_seen[APPLY_ACK_HELD]++;
                        slot->state = WATERLINK_SLOT_HELD;
                }
                at = next;
        }
}

static fn apply_walk_estimate(struct waterlink_link address_to link, p64 sample)
{
        if (!sample)
                sample = 1;
        link->recent = sample;

        if (!link->smoothed)
        {
                apply_seen[APPLY_ESTIMATE_FIRST]++;
                link->smoothed = sample;
                link->variance = sample / 2;
                return;
        }

        {
                p64 gap = sample > link->smoothed ? sample - link->smoothed
                                                  : link->smoothed - sample;

                apply_seen[APPLY_ESTIMATE]++;
                link->variance = (3 * link->variance + gap) / 4;
                link->smoothed = (7 * link->smoothed + sample) / 8;
        }
}

static fn apply_walk_settle(struct waterlink_link address_to link, p64 newly,
                            p64 latest, p64 sample, p64 now)
{
        if (latest)
        {
                if (latest > link->largest)
                {
                        apply_seen[APPLY_LARGEST]++;
                        link->largest = latest;
                }
                if (sample)
                        apply_walk_estimate(link, sample);
                link->backoff = 0;
                link->probes = 0;
        }

        if (newly && latest > link->recovery)
        {
                if (link->window < link->threshold)
                {
                        apply_seen[APPLY_GROW_SLOW]++;
                        link->window += newly;
                }
                else
                {
                        apply_seen[APPLY_GROW_AVOID]++;
                        link->window += WATERLINK_DATAGRAM * newly /
                                        link->window + 1;
                }
                if (link->window > WATERLINK_WINDOW_MOST)
                {
                        apply_seen[APPLY_GROW_CAP]++;
                        link->window = WATERLINK_WINDOW_MOST;
                }
        }

        if (link->flight_head != WATERLINK_NONE)
        {
                apply_seen[APPLY_SETTLE_LOSSES]++;
                waterlink_losses(link, now);
        }
}

static fn apply_walk(struct waterlink_link address_to link, address_any body,
                     struct waterlink_part address_to parts, positive count,
                     p64 now, waterlink_sink sink, address_any context)
{
        p8 address_to bytes = (p8 address_to)body;
        p8 framed = false;
        p8 acked = false;
        p64 newly = 0, latest = 0, sample = 0;

        if (now > link->clock)
                link->clock = now;
        else if (now < link->clock)
                apply_seen[APPLY_CLOCK_BEHIND]++;
        now = link->clock;
        apply_seen[APPLY_NO_SINK] += !sink;

        for (struct waterlink_part address_to part = parts; part < parts + count;
             part++)
        {
                p8 flags = part->flags;
                p8 key = part->key;
                p8 address_to payload = bytes + part->at;
                struct waterlink_frame head;
                struct waterlink_receiving address_to live;

                if (flags == WATERLINK_FRAME_ACK)
                {
                        apply_seen[APPLY_ACK]++;
                        apply_walk_acknowledge(link, key, part->number, part->more,
                                               address_of newly,
                                               address_of latest,
                                               address_of sample);
                        acked = true;
                        continue;
                }

                head.key = key;
                head.sequence = part->number;
                head.length = (p16)part->more;
                head.flags = flags;
                live = link->receiving + key;

                apply_seen[APPLY_FRAME]++;
                apply_seen[APPLY_OWED_FIRST] += !link->acking;
                waterlink_ack_owe(link, key);
                framed = true;
                if (flags & (WATERLINK_FRAME_URGENT | WATERLINK_FRAME_LAST))
                {
                        apply_seen[APPLY_OWED_NOW]++;
                        link->owed_now = 1;
                }

                if (head.sequence <= live->delivered || live->over)
                {
                        apply_seen[live->over ? APPLY_OVER : APPLY_STALE]++;
                        link->stale++;
                        link->owed_now = 1;
                }
                else if (live->paused ||
                         ((flags & WATERLINK_FRAME_DURABLE) &&
                          head.sequence != live->delivered + 1))
                {
                        apply_seen[live->paused ? APPLY_HOLD_PAUSED
                                                : APPLY_HOLD_AHEAD]++;
                        link->owed_now = 1;
                        waterlink_hold(link, live, address_of head, payload);
                }
                else if (!waterlink_hand(link, live, address_of head, payload,
                                         sink, context))
                {
                        apply_seen[APPLY_REFUSED]++;
                        waterlink_hold(link, live, address_of head, payload);
                        live->paused = 1;
                }
                else
                {
                        apply_seen[APPLY_HANDED]++;
                        apply_seen[APPLY_LAST] += (flags & WATERLINK_FRAME_LAST) != 0;
                        apply_seen[APPLY_RELEASE_HELD] +=
                                live->first != WATERLINK_NONE;
                        waterlink_release(link, key, sink, context);
                }
        }

        if (acked)
                apply_walk_settle(link, newly, latest, sample, now);

        if (framed)
                link->owed_count++;
}

/*
        Delivery as it was before the judge: one walk applying each part as
        it read it. For a body the judge takes, applying its parts must leave
        a link exactly where this walk leaves it, having handed on the same
        frames in the same order.
*/
static p8 judge_walk_number(p8 address_to bytes, positive length,
                              positive address_to at, p64 address_to value)
{
        positive used = memory_vli_get(bytes + address_to at,
                                       length - address_to at, 10, value);

        address_to at += used;
        return used != 0;
}

static p8 judge_walk(struct waterlink_link address_to link,
                       p8 address_to bytes, positive length, p64 now,
                       waterlink_sink sink, address_any context)
{
        positive at = 0;
        p8 framed = false;
        p8 acked = false;
        p64 newly = 0, latest = 0, sample = 0;
        p8 good;

        if (length > WATERLINK_PAYLOAD)
                return false;
        if (now > link->clock)
                link->clock = now;
        now = link->clock;
        while (at + 1 < length && bytes[at])
        {
                p8 flags = bytes[at];
                p8 key = bytes[at + 1];
                struct waterlink_frame head;
                struct waterlink_receiving address_to live;
                p64 sequence, size;

                at += 2;
                if (key >= WATERLINK_KEYS)
                        return false;
                if (flags == WATERLINK_FRAME_ACK)
                {
                        p64 delivered, mask;

                        if (!judge_walk_number(bytes, length, address_of at,
                                               address_of delivered) ||
                            !judge_walk_number(bytes, length, address_of at,
                                               address_of mask) ||
                            delivered >= WATERLINK_NONE)
                                return false;
                        apply_walk_acknowledge(link, key, (p32)delivered, mask,
                                              address_of newly,
                                              address_of latest,
                                              address_of sample);
                        acked = true;
                        continue;
                }
                if (!waterlink_frame_sane(flags) ||
                    !judge_walk_number(bytes, length, address_of at,
                                       address_of sequence) ||
                    !judge_walk_number(bytes, length, address_of at,
                                       address_of size) ||
                    !sequence || sequence >= WATERLINK_NONE ||
                    size > WATERLINK_FRAME_MAX || size > length - at)
                        return false;
                head.key = key;
                head.sequence = (p32)sequence;
                head.length = (p16)size;
                head.flags = flags;
                live = link->receiving + key;
                waterlink_ack_owe(link, key);
                framed = true;
                if (flags & (WATERLINK_FRAME_URGENT | WATERLINK_FRAME_LAST))
                        link->owed_now = 1;
                if (head.sequence <= live->delivered || live->over)
                {
                        link->stale++;
                        link->owed_now = 1;
                }
                else if (live->paused ||
                         ((flags & WATERLINK_FRAME_DURABLE) &&
                          head.sequence != live->delivered + 1))
                {
                        link->owed_now = 1;
                        waterlink_hold(link, live, address_of head, bytes + at);
                }
                else if (!waterlink_hand(link, live, address_of head, bytes + at,
                                         sink, context))
                {
                        waterlink_hold(link, live, address_of head, bytes + at);
                        live->paused = 1;
                }
                else
                        waterlink_release(link, key, sink, context);
                at += head.length;
        }
        if (acked)
                apply_walk_settle(link, newly, latest, sample, now);
        good = memory_span_byte(bytes + at, 0, length - at) == length - at;
        if (good && framed)
                link->owed_count++;
        return good;
}

//      What each side handed on, and a sink that turns some frames away --
//      by what they are, so both sides turn the same ones away.
static p64 judge_heard[2][512];
static positive judge_heard_count[2];

static p8 judge_hear(address_any context, struct waterlink_frame address_to head,
                       p8 address_to payload)
{
        positive side = (positive)context - 1;
        p64 mark = hash_xxh64(payload, head->length, head->sequence);

        if ((head->sequence * 7 + head->key) % 5 == 0 && head->length & 1)
                return false;
        if (judge_heard_count[side] < array_count(judge_heard[side]))
                judge_heard[side][judge_heard_count[side]++] =
                        mark ^ ((p64)head->key << 56) ^ head->flags;
        return true;
}

static p64 judge_state = 0x6a09e667f3bcc909ull;

static p64 judge_next(void)
{
        judge_state ^= judge_state << 13;
        judge_state ^= judge_state >> 7;
        judge_state ^= judge_state << 17;
        return judge_state;
}

//      A number, sometimes spelled longer than it needs: the longer
//      spellings are refused, and are where a reader that stops early goes
//      wrong.
static positive judge_put(p8 address_to into, p64 value, positive longer)
{
        positive used = memory_vli_put(into, value);

        if (longer && used < 10)
        {
                into[used - 1] |= 0x80;
                while (--longer && used < 9)
                        into[used++] = 0x80;
                into[used++] = 0;
        }
        return used;
}

static const p64 judge_edges[] = {
    0, 1, 2, 63, 64, 126, 127, 128, 129, 255, 16383, 16384, 1158, 1159, 1160,
    1167, 1168, 1169, 0xfffffffdull, 0xfffffffeull, 0xffffffffull,
    0x100000000ull, (1ull << 56) - 1, 1ull << 56, (1ull << 63) - 1, 1ull << 63,
    ~0ull};

static p64 judge_pick(void)
{
        switch (judge_next() % 4)
        {
        case 0:
                return judge_edges[judge_next() % array_count(judge_edges)];
        case 1:
                return judge_next() % 300;
        case 2:
                return judge_next() >> (judge_next() % 64);
        default:
                return 1 + judge_next() % 0xfffffffeull;
        }
}

/*
        A body as a fuzzer makes one: noise a fifth of the time, otherwise
        parts with numbers from the edges and spellings of every length, cut
        anywhere, padded with zeros or not, and a bit flipped a third of the
        time. `plausible` keeps to what a live link sends: small keys,
        sequences near the start and payloads that fit.
*/
static positive judge_body(p8 address_to body, p8 plausible)
{
        positive used = 0;
        positive frames = 1 + (positive)(judge_next() % 7);
        positive length;

        memory_zero(body, WATERLINK_PAYLOAD + 2);
        if (!plausible && judge_next() % 5 == 0)
        {
                length = (positive)(judge_next() % (WATERLINK_PAYLOAD + 2));
                for (positive at = 0; at < length; at++)
                        body[at] = (p8)(judge_next() % 3 ? judge_next()
                                                         : judge_next() % 4);
                return length;
        }
        for (positive frame = 0; frame < frames; frame++)
        {
                p8 number[24];
                positive n = 0;
                p8 ack = judge_next() % 4 == 0;
                p8 flags = ack ? 8
                               : (p8)((judge_next() & 1 ? 2 : 1) |
                                      (judge_next() & 3 ? 0 : 0x10) |
                                      (judge_next() & 7 ? 0 : 4));
                p64 first, second;
                positive payload;

                if (plausible)
                {
                        first = ack ? judge_next() % 12 : 1 + judge_next() % 12;
                        second = ack ? judge_next() & 0xfff
                                     : judge_next() % 48;
                }
                else
                {
                        first = judge_next() % 3
                                        ? (ack ? judge_next() % 0xffffffffull
                                               : 1 + judge_next() % 0xfffffffeull)
                                        : judge_pick();
                        second = ack ? judge_next()
                                     : judge_next() % 3 ? judge_next() % 200
                                                        : judge_pick();
                        if (judge_next() % 16 == 0)
                                flags = (p8)judge_next();
                }
                n += judge_put(number, first,
                               !plausible && judge_next() % 8 == 0
                                       ? (positive)(judge_next() % 10)
                                       : 0);
                n += judge_put(number + n, second,
                               !plausible && judge_next() % 8 == 0
                                       ? (positive)(judge_next() % 10)
                                       : 0);
                payload = ack || second > 1300 ? 0 : (positive)second;
                if (used + 2 + n + payload > WATERLINK_PAYLOAD)
                        break;
                body[used++] = flags;
                body[used++] = (p8)(plausible ? judge_next() % 6
                                    : judge_next() % 8 ? judge_next() % 64
                                                       : judge_next());
                memory_copy(body + used, number, n);
                used += n;
                for (positive at = 0; at < payload; at++)
                        body[used++] = (p8)judge_next();
        }
        if (plausible)
                return used + (positive)(judge_next() % 16) <= WATERLINK_PAYLOAD
                               ? used + (positive)(judge_next() % 16)
                               : used;
        switch (judge_next() % 4)
        {
        case 0:
                length = used;
                break;
        case 1:
                length = used + (positive)(judge_next() % 20);
                break;
        case 2:
                length = used ? (positive)(judge_next() % used) : 0;
                break;
        default:
                length = used + (positive)(judge_next() %
                                           (WATERLINK_PAYLOAD + 2 - used));
        }
        if (length > WATERLINK_PAYLOAD + 1)
                length = WATERLINK_PAYLOAD + 1;
        if (judge_next() % 3 == 0 && length)
                body[judge_next() % length] ^= (p8)(1u << (judge_next() % 8));
        return length;
}

static struct waterlink_link judge_one, judge_two;

static fn judge_procedural(void)
{
        struct waterlink_part parts[WATERLINK_PARTS];
        struct waterlink_part model[WATERLINK_PARTS];
        p8 body[WATERLINK_PAYLOAD + 2];
        positive cases = 0, wrong = 0, valid = 0, parted = 0, atomic = 0;
        positive runs = 0, bodies = 0, apart = 0;

        for (positive round = 0; round < 300000; round++)
        {
                positive length = judge_body(body, false);
                bipolar want = judge_model(body, length, model);
                bipolar got = waterlink_judge(body, length, parts);

                cases++;
                wrong += want != got;
                if (want >= 0 && want == got)
                {
                        valid++;
                        parted += (positive)want;
                        wrong += memory_compare(parts, model,
                                                (positive)want * sizeof *parts) != 0;
                }
                if (want < 0 && atomic < 1024)
                {
                        struct waterlink_link before;

                        waterlink_link_reset(address_of one);
                        before = one;
                        heard = 0;
                        if (!waterlink_deliver(address_of one, body, length, round,
                                               hear, null) &&
                            !heard &&
                            !memory_compare(address_of one, address_of before,
                                            sizeof one))
                                atomic++;
                }
        }

        //      Runs of bodies a live link could hear, into two links: one
        //      through the judge's parts, one through the walk it replaced.
        for (positive run = 0; run < 400; run++)
        {
                p8 payload[64];
                p8 alone = false;

                waterlink_link_reset(address_of judge_one);
                waterlink_link_reset(address_of judge_two);
                judge_heard_count[0] = judge_heard_count[1] = 0;
                for (positive at = 0; at < sizeof payload; at++)
                        payload[at] = (p8)judge_next();
                for (positive post = 0; post < 9; post++)
                {
                        p8 key = (p8)(1 + post % 3);
                        p16 size = (p16)(1 + judge_next() % 60);
                        p8 flags = post & 1 ? WATERLINK_FRAME_DURABLE
                                            : WATERLINK_FRAME_REPLACEABLE;

                        (void)waterlink_post(address_of judge_one, key, flags,
                                             payload, size, 1000 + post);
                        (void)waterlink_post(address_of judge_two, key, flags,
                                             payload, size, 1000 + post);
                }
                (void)waterlink_fill(address_of judge_one, body, 2000,
                                     address_of alone);
                (void)waterlink_fill(address_of judge_two, body, 2000,
                                     address_of alone);
                for (positive step = 0; step < 40; step++)
                {
                        positive length = judge_body(body, true);
                        p8 first, second;

                        if (waterlink_judge(body, length, parts) < 0)
                                continue;
                        first = waterlink_deliver(address_of judge_one, body,
                                                  length, 3000 + step * 50,
                                                  judge_hear, (address_any)1);
                        second = judge_walk(address_of judge_two, body, length,
                                            3000 + step * 50, judge_hear,
                                            (address_any)2);
                        apart += first != second || !first;
                        bodies++;
                }
                apart += judge_heard_count[0] != judge_heard_count[1] ||
                         memory_compare(judge_heard[0], judge_heard[1],
                                        judge_heard_count[0] * sizeof(p64)) ||
                         memory_compare(address_of judge_one, address_of judge_two,
                                        sizeof judge_one);
                runs++;
        }

        string_format(log, "  judge: %p bodies, %p taken with %p parts, "
                           "%p refusals snapshotted; %p runs of %p live "
                           "bodies\n",
                      cases, valid, parted, atomic, runs, bodies);
        check("the judge agrees with an independent reading, part for part, "
              "on every generated body",
              cases == 300000 && wrong == 0 && valid > 10000 &&
                      cases - valid > 10000);
        check("a refused body changes nothing and hands nothing on",
              atomic == 1024);
        check("applying the judge's parts is the walk it replaced, frame for "
              "frame and word for word",
              runs == 400 && bodies > 8000 && apart == 0);
}

/*
        Filling as it was in C, before waterlink_fill was each machine's
        assembly: the reference the assembly must match, state for state and
        byte for byte. fill_seen counts the paths each call took, so the
        check can say its states reached every one of them.
*/
enum {
        FILL_SEQUENCE_1, FILL_SEQUENCE_2, FILL_SEQUENCE_3, FILL_SEQUENCE_4,
        FILL_SEQUENCE_5, FILL_LENGTH_2, FILL_HEAD_9, FILL_TINY,
        FILL_PAYLOAD_0, FILL_PAYLOAD_1, FILL_PAYLOAD_2, FILL_PAYLOAD_4,
        FILL_PAYLOAD_8, FILL_PAYLOAD_16, FILL_PAYLOAD_33, FILL_ROOM_RESET,
        FILL_FULL, FILL_REQUEUE_AT, FILL_TAIL_AT, FILL_PRIOR, FILL_BLOCKED,
        FILL_PROBED, FILL_PROBES_IDLE, FILL_PACED, FILL_WINDOW_FULL,
        FILL_PACE_HOLD, FILL_URGENT, FILL_RETRANSMIT, FILL_FLIGHT_EMPTY,
        FILL_FLIGHT_TAIL, FILL_ACKS_OVER_8, FILL_ACK_MASK_LONG, FILL_ACK_ROOM,
        FILL_ACKS_DONE, FILL_ACK_NOW, FILL_ACK_COUNT, FILL_ACK_DELAY,
        FILL_ACK_NOT_DUE, FILL_ACK_DELIVERED_5, FILL_CLOCK_BEHIND,
        FILL_LOSSES, FILL_BOTH_BANDS, FILL_SEEN
};

static positive fill_seen[FILL_SEEN];

static positive fill_walk_head_put(p8 address_to at,
                                   struct waterlink_slot address_to slot)
{
        positive used = 2;

        at[0] = slot->flags;
        at[1] = slot->key;
        used += memory_vli_put(at + used, slot->sequence);
        used += memory_vli_put(at + used, slot->length);
        return used;
}

static fn fill_walk_flight_append(struct waterlink_link address_to link, p32 at)
{
        struct waterlink_slot address_to slot = link->slot + at;

        fill_seen[link->flight_tail == WATERLINK_NONE ? FILL_FLIGHT_EMPTY
                                                      : FILL_FLIGHT_TAIL]++;
        slot->next = WATERLINK_NONE;
        slot->prior = link->flight_tail;
        if (link->flight_tail == WATERLINK_NONE)
                link->flight_head = at;
        else
                link->slot[link->flight_tail].next = at;
        link->flight_tail = at;
        link->in_flight += waterlink_bytes(slot);
        link->sending[slot->key].flying++;
}

static p32 fill_walk_band_take(struct waterlink_link address_to link, p32 band,
                               p32 address_to prior_out)
{
        p32 prior = WATERLINK_NONE;

        for (p32 at = link->head[band]; at != WATERLINK_NONE;
             at = link->slot[at].next)
        {
                if (band == WATERLINK_BAND_URGENT ||
                    !waterlink_key_blocked(link, link->slot + at))
                {
                        address_to prior_out = prior;
                        return at;
                }
                fill_seen[FILL_BLOCKED]++;
                prior = at;
        }
        return WATERLINK_NONE;
}

static positive fill_walk_ack_write(struct waterlink_link address_to link,
                                    p8 address_to bytes, positive room)
{
        positive used = 0;

        if (bits_counted(link->acking) > 8)
                fill_seen[FILL_ACKS_OVER_8]++;
        while (link->acking)
        {
                p8 key = (p8)bits_trailing_zeros(link->acking);
                struct waterlink_receiving address_to live =
                        link->receiving + key;
                p64 mask = 0;

                for (p32 held = live->first; held != WATERLINK_NONE;
                     held = link->held[held].next)
                {
                        p32 gap = link->held[held].head.sequence -
                                  live->delivered - 1;

                        if (gap < WATERLINK_ACK_MASK)
                                mask |= 1ull << gap;
                }

                if (used + 2 + memory_vli_size(live->delivered) +
                            memory_vli_size(mask) >
                    room)
                {
                        fill_seen[FILL_ACK_ROOM]++;
                        break;
                }
                fill_seen[FILL_ACK_MASK_LONG] += mask >= 0x80;
                fill_seen[FILL_ACK_DELIVERED_5] +=
                        memory_vli_size(live->delivered) == 5;
                bytes[used++] = WATERLINK_FRAME_ACK;
                bytes[used++] = key;
                used += memory_vli_put(bytes + used, live->delivered);
                used += memory_vli_put(bytes + used, mask);
                link->acking &= link->acking - 1;
        }
        return used;
}

static positive fill_walk(struct waterlink_link address_to link,
                          address_any out, p64 now, p8 address_to alone)
{
        p8 address_to bytes = (p8 address_to)out;
        positive used = 0;
        positive room = WATERLINK_PAYLOAD;
        positive paced = 0;
        p8 probed = false;
        p8 full = false;
        positive urgent = 0;

        address_to alone = false;
        if (now > link->clock)
                link->clock = now;
        else if (now < link->clock)
                fill_seen[FILL_CLOCK_BEHIND]++;

        if (link->flight_head != WATERLINK_NONE)
        {
                fill_seen[FILL_LOSSES]++;
                waterlink_losses(link, now);
        }

        if (link->acking)
        {
                positive owed = (positive)bits_counted(link->acking);

                room -= WATERLINK_ACK_MOST * (owed < 8 ? owed : 8);
        }

        for (p32 band = 0; band < WATERLINK_BANDS && !full; band++)
        {
                for (;;)
                {
                        struct waterlink_slot address_to slot;
                        p32 prior = WATERLINK_NONE;
                        p32 at;
                        positive size;
                        positive spelled;

                        if (band != WATERLINK_BAND_URGENT && !link->probes &&
                            (link->in_flight >= link->window ||
                             (link->smoothed && now < link->pace)))
                        {
                                if (link->head[band] != WATERLINK_NONE)
                                        fill_seen[link->in_flight >= link->window
                                                          ? FILL_WINDOW_FULL
                                                          : FILL_PACE_HOLD]++;
                                break;
                        }

                        at = fill_walk_band_take(link, band, address_of prior);
                        if (at == WATERLINK_NONE)
                                break;

                        slot = link->slot + at;
                        size = (positive)waterlink_bytes(slot);
                        if (used + size > room)
                        {
                                if (!used && size <= WATERLINK_PAYLOAD)
                                {
                                        fill_seen[FILL_ROOM_RESET]++;
                                        room = WATERLINK_PAYLOAD;
                                }
                                else
                                {
                                        fill_seen[FILL_FULL]++;
                                        full = true;
                                        break;
                                }
                        }

                        spelled = 2 + memory_vli_size(slot->sequence) +
                                  memory_vli_size(slot->length);
                        fill_seen[FILL_SEQUENCE_1 - 1 +
                                  memory_vli_size(slot->sequence)]++;
                        fill_seen[FILL_LENGTH_2] += slot->length >= 0x80;
                        fill_seen[FILL_HEAD_9] += spelled == 9;
                        fill_seen[FILL_TINY] += size < 8;
                        fill_seen[!slot->length        ? FILL_PAYLOAD_0
                                  : slot->length < 2  ? FILL_PAYLOAD_1
                                  : slot->length < 4  ? FILL_PAYLOAD_2
                                  : slot->length < 8  ? FILL_PAYLOAD_4
                                  : slot->length < 16 ? FILL_PAYLOAD_8
                                  : slot->length < 33 ? FILL_PAYLOAD_16
                                                      : FILL_PAYLOAD_33]++;
                        fill_seen[FILL_PRIOR] += prior != WATERLINK_NONE;
                        fill_seen[FILL_TAIL_AT] += link->tail[band] == at &&
                                                   prior != WATERLINK_NONE;
                        fill_seen[FILL_REQUEUE_AT] += link->requeue[band] == at;

                        used += fill_walk_head_put(bytes + used, slot);
                        if (slot->length)
                                memory_copy(bytes + used, slot->payload,
                                            slot->length);
                        used += slot->length;

                        if (band == WATERLINK_BAND_URGENT)
                        {
                                address_to alone = true;
                                urgent++;
                        }
                        else if (link->probes)
                                probed = true;
                        else
                                paced += size;

                        waterlink_band_unlink(link, band, at, prior);
                        slot->state = WATERLINK_SLOT_FLIGHT;
                        slot->serial = ++link->serial;
                        slot->sent = now;
                        if (slot->tries++)
                        {
                                fill_seen[FILL_RETRANSMIT]++;
                                link->retransmitted++;
                        }
                        fill_walk_flight_append(link, at);
                        link->sent++;
                        if (band != WATERLINK_BAND_URGENT && urgent)
                                fill_seen[FILL_BOTH_BANDS]++;
                }
        }

        fill_seen[FILL_URGENT] += urgent != 0;
        if (probed)
                fill_seen[FILL_PROBED]++;
        else if (link->probes)
                fill_seen[FILL_PROBES_IDLE]++;
        if (probed)
                link->probes--;

        if (link->acking && !used)
                fill_seen[link->owed_now ? FILL_ACK_NOW
                          : link->owed_count >= WATERLINK_ACK_EVERY
                                  ? FILL_ACK_COUNT
                          : link_age(now, link->owed) >= WATERLINK_ACK_DELAY
                                  ? FILL_ACK_DELAY
                                  : FILL_ACK_NOT_DUE]++;
        if (link->acking && (used || waterlink_ack_due(link, now)))
        {
                used += fill_walk_ack_write(link, bytes + used,
                                            WATERLINK_PAYLOAD - used);
                if (!link->acking)
                {
                        fill_seen[FILL_ACKS_DONE]++;
                        link->owed_count = 0;
                        link->owed_now = 0;
                }
        }

        if (paced && link->smoothed)
        {
                p64 gap = link->smoothed * paced * 4 / (link->window * 5);
                p64 whole = link->smoothed * WATERLINK_DATAGRAM * 4 /
                            (link->window * 5);
                p64 floor = now > whole * WATERLINK_PACE_BURST
                                    ? now - whole * WATERLINK_PACE_BURST
                                    : 0;

                fill_seen[FILL_PACED]++;
                if (link->pace < floor)
                        link->pace = floor;
                link->pace += gap;
        }

        return used;
}

/*
        States for filling, made the way a link makes them and then pushed
        to the edges: posts on streams and registers of every length class,
        sequences started at every spelling's edge, a key run past its
        window, frames lost back to the front of their bands, keys taken and
        held at the far side's every distance, more keys owed than a body
        reserves room for, and the window, the pacer, the probes and the
        clock set anywhere a live link can put them. Every change is made the
        same way to two links; one fills through the reference, the other
        through waterlink_fill, and after every call the two must have
        written the same bytes -- and nothing past them -- and be in the same
        state, which the whole struct confirms at the end of each run.
*/
static struct waterlink_link fill_one, fill_two;

static p64 fill_next(p64 address_to state)
{
        XORSHIFT64(address_to state);
        return address_to state;
}

static p32 fill_sequence_pick(p64 address_to state)
{
        static const p32 edges[] = {
            1,         2,         126,         127,         128,
            129,       16382,     16383,       16384,       16385,
            2097150,   2097151,   2097152,     268435454,   268435455,
            268435456, 268435457, 0xfffffff0u, 0xfffffffdu};

        if (fill_next(state) % 3)
                return edges[fill_next(state) % array_count(edges)];
        return 1 + (p32)(fill_next(state) % 0xfffffff0u);
}

static p16 fill_length_pick(p64 address_to state)
{
        switch (fill_next(state) % 11)
        {
        case 0: return 0;
        case 1: return 1;
        case 2: return (p16)(2 + fill_next(state) % 2);
        case 3: return (p16)(4 + fill_next(state) % 4);
        case 4: return (p16)(8 + fill_next(state) % 8);
        case 5: return (p16)(16 + fill_next(state) % 17);
        case 6: return (p16)(33 + fill_next(state) % 100);
        case 7: return (p16)(126 + fill_next(state) % 4);
        case 8: return (p16)(1100 + fill_next(state) % 60);
        default: return (p16)(fill_next(state) % 64);
        }
}

//      One change, made from a copy of the generator's state so that the
//      two links see the same one.
static p64 fill_change(struct waterlink_link address_to link, p64 state,
                       p64 now)
{
        static p8 payload[WATERLINK_FRAME_MAX];
        p64 op = fill_next(address_of state) % 18;

        for (positive at = 0; at < 64; at++)
                payload[at] = (p8)(at * 29 + op);
        if (op < 7 || op == 16)
        {
                positive posts = op == 16 ? 70 + fill_next(address_of state) % 20
                                          : 1 + fill_next(address_of state) % 12;
                p8 key = (p8)(fill_next(address_of state) % 8 ? fill_next(address_of state) % 8
                                                               : fill_next(address_of state) % 64);

                for (positive post = 0; post < posts; post++)
                {
                        p8 flags;
                        p16 length = fill_length_pick(address_of state);

                        if (op != 16 && fill_next(address_of state) % 3 == 0)
                                key = (p8)(fill_next(address_of state) % 8);
                        flags = key & 1 ? WATERLINK_FRAME_REPLACEABLE
                                        : WATERLINK_FRAME_DURABLE;
                        if (fill_next(address_of state) % 3 == 0)
                                flags |= WATERLINK_FRAME_URGENT;
                        if (fill_next(address_of state) % 97 == 0)
                                flags |= WATERLINK_FRAME_LAST;
                        if (link->sending[key].first == WATERLINK_NONE &&
                            fill_next(address_of state) % 3 == 0)
                                link->sending[key].sequence =
                                        fill_sequence_pick(address_of state);
                        (void)waterlink_post(link, key, flags, payload, length,
                                             now);
                }
        }
        else if (op == 7 || op == 8)
        {
                //      The far side takes some of a key and holds some past it.
                p8 key = (p8)(fill_next(address_of state) % 8);
                p32 first = link->sending[key].first;
                p64 newly = 0, latest = 0, sample = 0;
                p32 taken = (p32)(fill_next(address_of state) % 6);
                p64 mask = fill_next(address_of state);

                mask &= fill_next(address_of state);
                if (first != WATERLINK_NONE)
                        apply_walk_acknowledge(
                                link, key,
                                link->slot[first].sequence - 1 + taken, mask,
                                address_of newly, address_of latest,
                                address_of sample);
                apply_walk_settle(link, newly, latest, sample, now);
        }
        else if (op == 9)
        {
                for (p32 at = link->flight_head; at != WATERLINK_NONE;)
                {
                        p32 next = link->slot[at].next;

                        if (fill_next(address_of state) & 1)
                                waterlink_lose(link, at);
                        at = next;
                }
        }
        else if (op == 10 || op == 11)
        {
                //      Arrivals owed an answer, some held ahead of what was
                //      taken, the most keys a body can answer or more.
                positive keys = op == 11 ? 9 + fill_next(address_of state) % 40
                                         : 1 + fill_next(address_of state) % 3;

                for (positive one_key = 0; one_key < keys; one_key++)
                {
                        p8 key = (p8)(fill_next(address_of state) % 64);
                        struct waterlink_receiving address_to live =
                                link->receiving + key;
                        positive holds = fill_next(address_of state) % 5;

                        if (live->first == WATERLINK_NONE)
                                live->delivered =
                                        fill_next(address_of state) % 4
                                                ? (p32)(fill_next(address_of state) % 300)
                                                : fill_sequence_pick(address_of state);
                        for (positive hold = 0; hold < holds; hold++)
                        {
                                struct waterlink_frame head;

                                head.key = key;
                                head.flags = WATERLINK_FRAME_DURABLE;
                                head.length = (p16)(fill_next(address_of state) % 5);
                                head.sequence = live->delivered + 1 +
                                                (p32)(fill_next(address_of state) % 70);
                                waterlink_hold(link, live, address_of head,
                                               payload);
                        }
                        waterlink_ack_owe(link, key);
                }
        }
        else if (op == 12)
        {
                p64 span = WATERLINK_WINDOW_MOST - WATERLINK_WINDOW_LEAST;

                link->window = fill_next(address_of state) % 2
                                       ? link->in_flight + fill_next(address_of state) % 4000
                                       : WATERLINK_WINDOW_LEAST +
                                                 fill_next(address_of state) % span;
                if (link->window < WATERLINK_WINDOW_LEAST)
                        link->window = WATERLINK_WINDOW_LEAST;
                if (link->window > WATERLINK_WINDOW_MOST)
                        link->window = WATERLINK_WINDOW_MOST;
                link->smoothed = fill_next(address_of state) % 3
                                         ? fill_next(address_of state) % 60000
                                         : 0;
                link->pace = fill_next(address_of state) % 2
                                     ? now + fill_next(address_of state) % 5000
                                     : now - fill_next(address_of state) % 5000;
        }
        else if (op == 13)
                link->probes = (p32)(fill_next(address_of state) % 3);
        else if (op == 14)
        {
                link->owed_now = (p8)(fill_next(address_of state) & 1);
                link->owed_count = (p32)(fill_next(address_of state) % 4);
                link->owed = now - (fill_next(address_of state) % 2
                                            ? fill_next(address_of state) % 2500
                                            : WATERLINK_ACK_DELAY - 1 +
                                                      fill_next(address_of state) % 3);
        }
        else if (op == 15)
        {
                link->window = WATERLINK_WINDOW_MOST;
                link->pace = 0;
                if (fill_next(address_of state) % 4 == 0)
                        link->acking = 0;
        }
        return state;
}

/*      The normal band counted again by walking it: the tally the bounded
        walks trust has to be what is there, whoever moved the band, and
        every key with a frame there has its bit (a bit may outlive its
        frames; a walk clears it). */
static p8 band_counts_hold(struct waterlink_link address_to link)
{
        p16 counted[WATERLINK_KEYS];
        p64 keys = 0;

        memory_zero(counted, sizeof counted);
        for (p32 at = link->head[WATERLINK_BAND_NORMAL]; at != WATERLINK_NONE;
             at = link->slot[at].next)
        {
                counted[link->slot[at].key]++;
                keys |= 1ull << link->slot[at].key;
        }
        return !(keys & ~link->banded) &&
               !memory_compare(counted, link->queued, sizeof counted);
}

/*      Two links the same from a place on, but for which keys' bits are
        set in banded past their last frame: the reference fill never clears
        one, and waterlink_fill clears those its walk comes across. */
static p8 fill_links_apart(struct waterlink_link address_to one,
                             struct waterlink_link address_to two,
                             positive from)
{
        positive mark = __builtin_offsetof(struct waterlink_link, banded);

        return memory_compare((p8 address_to)one + from,
                              (p8 address_to)two + from, mark - from) ||
               memory_compare(one->queued, two->queued, sizeof one->queued) ||
               !band_counts_hold(one) || !band_counts_hold(two);
}

//      The walk wake did before it could end early: any frame the window lets by.
static p8 sendable_walk(struct waterlink_link address_to link)
{
        for (p32 at = link->head[WATERLINK_BAND_NORMAL]; at != WATERLINK_NONE;
             at = link->slot[at].next)
                if (!waterlink_key_blocked(link, link->slot + at))
                        return true;
        return false;
}

//      Whether a walk now ends early: a stream blocked with more behind it.
static p8 band_walk_bounded(struct waterlink_link address_to link)
{
        for (p32 at = link->head[WATERLINK_BAND_NORMAL]; at != WATERLINK_NONE;
             at = link->slot[at].next)
                if ((link->slot[at].flags & WATERLINK_FRAME_DURABLE) &&
                    waterlink_key_blocked(link, link->slot + at) &&
                    link->queued[link->slot[at].key] > 1)
                        return true;
        return false;
}

static fn fill_procedural(void)
{
        static p8 body_one[WATERLINK_PAYLOAD + 64];
        static p8 body_two[WATERLINK_PAYLOAD + 64];
        p64 state = 0x243f6a8885a308d3ull;
        positive runs = 0, calls = 0, written = 0, wrong = 0, apart = 0;
        positive missed = 0, bounded = 0, miscounted = 0;

        memory_zero(fill_seen, sizeof fill_seen);
        for (positive run = 0; run < 300; run++)
        {
                p64 now = 1000 + fill_next(address_of state) % 100000;

                waterlink_link_reset(address_of fill_one);
                waterlink_link_reset(address_of fill_two);
                for (positive step = 0; step < 160; step++)
                {
                        positive fills = 1 + fill_next(address_of state) % 3;

                        (void)fill_change(address_of fill_one, state, now);
                        state = fill_change(address_of fill_two, state, now);
                        for (positive fill = 0; fill < fills; fill++)
                        {
                                p8 alone_one = (fill_next(address_of state) & 1) != 0;
                                p8 alone_two = alone_one;
                                p64 at = fill_next(address_of state) % 16 == 0
                                                 ? now - fill_next(address_of state) % 200
                                                 : now;
                                positive used_one, used_two;

                                for (positive byte = 0; byte < sizeof body_one; byte++)
                                        body_one[byte] = body_two[byte] =
                                                (p8)(byte * 13 + step + fill);
                                bounded += band_walk_bounded(address_of fill_two);
                                miscounted += waterlink_sendable(address_of fill_two) !=
                                              sendable_walk(address_of fill_two);
                                used_one = fill_walk(address_of fill_one, body_one,
                                                     at, address_of alone_one);
                                used_two = waterlink_fill(address_of fill_two,
                                                          body_two, at,
                                                          address_of alone_two);
                                calls++;
                                written += used_one;
                                miscounted += !band_counts_hold(address_of fill_one) ||
                                              !band_counts_hold(address_of fill_two);
                                wrong += used_one != used_two ||
                                         alone_one != alone_two ||
                                         memory_compare(body_one, body_two,
                                                        sizeof body_one) != 0;
                                apart += fill_links_apart(address_of fill_one,
                                                          address_of fill_two,
                                                          __builtin_offsetof(
                                                                  struct waterlink_link,
                                                                  sending));
                                for (positive slot = 0; slot < WATERLINK_SLOTS; slot++)
                                        apart += memory_compare(
                                                         fill_one.slot + slot,
                                                         fill_two.slot + slot,
                                                         __builtin_offsetof(
                                                                 struct waterlink_slot,
                                                                 payload)) != 0;
                        }
                        now += fill_next(address_of state) % 8 ? fill_next(address_of state) % 3000
                                                               : fill_next(address_of state) % 300000;
                }
                apart += fill_links_apart(address_of fill_one,
                                          address_of fill_two, 0);
                runs++;
        }

        for (positive seen = 0; seen < FILL_SEEN; seen++)
                missed += fill_seen[seen] < 20;
        string_format(log, "  fill: %p calls in %p runs wrote %p bytes; "
                           "%p paths taken under twenty times; %p calls "
                           "past a blocked stream\n",
                      calls, runs, written, missed, bounded);
        for (positive seen = 0; seen < FILL_SEEN; seen++)
                if (fill_seen[seen] < 20)
                        string_format(log, "    fill path %p taken %p times\n",
                                      seen, fill_seen[seen]);
        check("waterlink_fill is the C it replaced, byte for byte and state "
              "for state, on every generated link",
              runs == 300 && calls > 50000 && wrong == 0 && apart == 0);
        check("and the generated links took every path of it",
              missed == 0);
        check("and the normal band's tally by key is the band after every "
              "fill, and wake's bounded walk finds what the whole walk "
              "does, many of them past a blocked stream",
              miscounted == 0 && bounded > 1000);
}

/*
        What the hold-back says of itself: each key's chain in order, ending
        where held_last says, its stream frames the bits of held_mask and none
        of them further past what was taken than a window, and the pool's free
        list and the chains the whole of it. The count it returns is how many
        frames the link holds.
*/
static p8 held_consistent(struct waterlink_link address_to link,
                            positive address_to total)
{
        positive free = 0;
        positive used = 0;

        for (p32 at = link->held_free; at != WATERLINK_NONE;
             at = link->held[at].next)
                if (at >= WATERLINK_HELD || ++free > WATERLINK_HELD)
                        return false;
        for (p32 key = 0; key < WATERLINK_KEYS; key++)
        {
                struct waterlink_receiving address_to live = link->receiving + key;
                p64 mask = 0;
                p32 last = WATERLINK_NONE;

                for (p32 at = live->first; at != WATERLINK_NONE;
                     at = link->held[at].next)
                {
                        struct waterlink_frame address_to head =
                                address_of link->held[at].head;

                        if (at >= WATERLINK_HELD || ++used > WATERLINK_HELD ||
                            head->key != key ||
                            (last != WATERLINK_NONE &&
                             head->sequence <= link->held[last].head.sequence))
                                return false;
                        if (head->flags & WATERLINK_FRAME_DURABLE)
                        {
                                if (head->sequence - live->delivered >
                                    WATERLINK_KEY_WINDOW)
                                        return false;
                                mask |= 1ull << (head->sequence & 63);
                        }
                        last = at;
                }
                if (mask != link->held_mask[key] ||
                    (last != WATERLINK_NONE && last != link->held_last[key]))
                        return false;
        }
        address_to total = used;
        return free + used == WATERLINK_HELD;
}

/*
        Applying against the C it replaced, on the links fill's changes make
        and bodies a judge could have passed: acknowledgements at and around
        each key's chain with every kind of mask, frames stale, next, ahead
        and far ahead, on keys paused and ended, of every length, and four
        sinks -- none, one that takes everything, one that turns some away,
        and one that posts back into the link it is hearing from, which is
        what a service answering a request does. Now and then a key's chain
        is left out of order, so a slot taken is not always the first. What
        each side handed on, and the two links, must be the same.
*/
static p64 apply_heard[2][2048];
static positive apply_heard_count[2];
static positive apply_sink_mode;

static p8 apply_hear(address_any context, struct waterlink_frame address_to head,
                       p8 address_to payload)
{
        positive side = (positive)context - 1;
        struct waterlink_link address_to link = side ? &fill_two : &fill_one;
        p64 mark = hash_xxh64(payload, head->length, head->sequence) ^
                   ((p64)head->key << 56) ^ ((p64)head->flags << 48);

        if (apply_sink_mode == 2 && (head->sequence * 7 + head->key) % 5 == 0 &&
            head->length & 1)
                return false;
        if (apply_heard_count[side] < array_count(apply_heard[side]))
                apply_heard[side][apply_heard_count[side]++] = mark;
        if (apply_sink_mode == 3 && head->length % 3 == 0)
        {
                p8 key = (p8)((head->key + 1) % 8);

                (void)waterlink_post(link, key,
                                     key & 1 ? WATERLINK_FRAME_REPLACEABLE
                                             : WATERLINK_FRAME_DURABLE,
                                     payload, head->length < 8 ? head->length : 8,
                                     link->clock);
        }
        return true;
}

//      A body of parts, made from a copy of the generator's state.
static positive apply_parts(struct waterlink_link address_to link, p64 state,
                            struct waterlink_part address_to parts,
                            p8 address_to body)
{
        positive count = 1 + fill_next(address_of state) % (fill_next(address_of state) % 4 ? 4 : 12);

        for (positive at = 0; at < WATERLINK_PAYLOAD; at++)
                body[at] = (p8)fill_next(address_of state);
        for (positive part = 0; part < count; part++)
        {
                p8 key = (p8)(fill_next(address_of state) % 8 ? fill_next(address_of state) % 8
                                                               : fill_next(address_of state) % 64);
                struct waterlink_part address_to made = parts + part;

                memory_zero(made, sizeof *made);
                made->key = key;
                if (fill_next(address_of state) % 3 == 0)
                {
                        p32 at = link->sending[key].first;
                        positive walk = fill_next(address_of state) % 4;
                        p32 base;

                        while (walk-- && at != WATERLINK_NONE &&
                               link->slot[at].chain != WATERLINK_NONE)
                                at = link->slot[at].chain;
                        base = at == WATERLINK_NONE ? link->sending[key].sequence
                                                    : link->slot[at].sequence;
                        made->flags = WATERLINK_FRAME_ACK;
                        made->number = fill_next(address_of state) % 5
                                               ? base - 2 + (p32)(fill_next(address_of state) % 4)
                                               : fill_sequence_pick(address_of state) - 1;
                        if (made->number == WATERLINK_NONE)
                                made->number = 0;
                        switch (fill_next(address_of state) % 5)
                        {
                        case 0: made->more = 0; break;
                        case 1: made->more = ~0ull; break;
                        case 2: made->more = 1ull << (fill_next(address_of state) % 64); break;
                        default: made->more = fill_next(address_of state) &
                                              fill_next(address_of state);
                        }
                }
                else
                {
                        p32 taken = link->receiving[key].delivered;
                        positive length = fill_next(address_of state) % 4 ? fill_next(address_of state) % 16
                                                                          : fill_next(address_of state) % 200;
                        p32 number;

                        switch (fill_next(address_of state) % 8)
                        {
                        case 0: number = taken; break;
                        case 1: number = taken - (p32)(fill_next(address_of state) % 3); break;
                        case 2: number = taken + 2 + (p32)(fill_next(address_of state) % 70); break;
                        case 3: number = fill_sequence_pick(address_of state); break;
                        default: number = taken + 1;
                        }
                        if (!number || number == WATERLINK_NONE)
                                number = 1;
                        made->flags = (p8)((key & 1 ? WATERLINK_FRAME_REPLACEABLE
                                                    : WATERLINK_FRAME_DURABLE) |
                                           (fill_next(address_of state) % 4 ? 0 : WATERLINK_FRAME_URGENT) |
                                           (fill_next(address_of state) % 29 ? 0 : WATERLINK_FRAME_LAST));
                        if (fill_next(address_of state) % 16 == 0)
                                made->flags ^= WATERLINK_FRAME_REPLACEABLE |
                                               WATERLINK_FRAME_DURABLE;
                        made->number = number;
                        made->more = length;
                        made->at = (p16)(fill_next(address_of state) %
                                         (WATERLINK_PAYLOAD - length));
                }
        }
        return count;
}

//      A key's first two slots trade sequences, so an acknowledgement can
//      take the second and not the first.
static fn apply_shuffle(struct waterlink_link address_to link, p8 key)
{
        p32 first = link->sending[key].first;
        p32 second = first == WATERLINK_NONE ? WATERLINK_NONE
                                             : link->slot[first].chain;
        p32 swap;

        if (second == WATERLINK_NONE)
                return;
        swap = link->slot[first].sequence;
        link->slot[first].sequence = link->slot[second].sequence;
        link->slot[second].sequence = swap;
}

static fn apply_procedural(void)
{
        static struct waterlink_part parts[WATERLINK_PARTS];
        static p8 body[WATERLINK_PAYLOAD];
        p64 state = 0x13198a2e03707344ull;
        positive runs = 0, calls = 0, apart = 0, heard = 0, missed = 0;
        positive miscounted = 0;
        positive inconsistent = 0;
        positive total;

        memory_zero(apply_seen, sizeof apply_seen);
        for (positive run = 0; run < 240; run++)
        {
                p64 now = 1000 + fill_next(address_of state) % 100000;

                waterlink_link_reset(address_of fill_one);
                waterlink_link_reset(address_of fill_two);
                apply_heard_count[0] = apply_heard_count[1] = 0;
                for (positive step = 0; step < 120; step++)
                {
                        positive count;
                        waterlink_sink sink;
                        p64 at;

                        (void)fill_change(address_of fill_one, state, now);
                        state = fill_change(address_of fill_two, state, now);
                        if (fill_next(address_of state) % 3 == 0)
                        {
                                p8 alone = false;
                                static p8 out[WATERLINK_PAYLOAD];

                                (void)fill_walk(address_of fill_one, out, now,
                                                address_of alone);
                                (void)fill_walk(address_of fill_two, out, now,
                                                address_of alone);
                        }
                        if (fill_next(address_of state) % 23 == 0)
                        {
                                p8 key = (p8)(fill_next(address_of state) % 8);

                                apply_shuffle(address_of fill_one, key);
                                apply_shuffle(address_of fill_two, key);
                        }
                        if (fill_next(address_of state) % 11 == 0)
                        {
                                p8 key = (p8)(fill_next(address_of state) % 8);

                                fill_one.receiving[key].paused ^= 1;
                                fill_two.receiving[key].paused ^= 1;
                        }
                        count = apply_parts(address_of fill_one, state, parts, body);
                        (void)fill_next(address_of state);
                        apply_sink_mode = fill_next(address_of state) % 4;
                        sink = apply_sink_mode ? apply_hear : null;
                        at = fill_next(address_of state) % 16 == 0
                                     ? now - fill_next(address_of state) % 200
                                     : now;
                        apply_walk(address_of fill_one, body, parts, count, at,
                                   sink, (address_any)1);
                        waterlink_apply(address_of fill_two, body, parts, count, at,
                                        sink, (address_any)2);
                        calls++;
                        miscounted += !band_counts_hold(address_of fill_two);
                        inconsistent +=
                                !held_consistent(address_of fill_two, address_of total);
                        apart += memory_compare(address_of fill_one.sending,
                                                address_of fill_two.sending,
                                                sizeof fill_one -
                                                        __builtin_offsetof(
                                                                struct waterlink_link,
                                                                sending)) != 0;
                        for (positive slot = 0; slot < WATERLINK_SLOTS; slot++)
                                apart += memory_compare(
                                                 fill_one.slot + slot,
                                                 fill_two.slot + slot,
                                                 __builtin_offsetof(
                                                         struct waterlink_slot,
                                                         payload)) != 0;
                        for (positive held = 0; held < WATERLINK_HELD; held++)
                                apart += memory_compare(
                                                 fill_one.held + held,
                                                 fill_two.held + held,
                                                 __builtin_offsetof(
                                                         struct waterlink_held,
                                                         payload)) != 0;
                        now += fill_next(address_of state) % 8 ? fill_next(address_of state) % 3000
                                                               : fill_next(address_of state) % 300000;
                }
                apart += apply_heard_count[0] != apply_heard_count[1] ||
                         memory_compare(apply_heard[0], apply_heard[1],
                                        apply_heard_count[0] * sizeof(p64)) != 0;
                apart += memory_compare(address_of fill_one, address_of fill_two,
                                        sizeof fill_one) != 0;
                heard += apply_heard_count[0];
                runs++;
        }

        for (positive seen = 0; seen < APPLY_SEEN; seen++)
                missed += apply_seen[seen] < 20;
        string_format(log, "  apply: %p calls in %p runs handed on %p frames; "
                           "%p paths taken under twenty times\n",
                      calls, runs, heard, missed);
        for (positive seen = 0; seen < APPLY_SEEN; seen++)
                if (apply_seen[seen] < 20)
                        string_format(log, "    apply path %p taken %p times\n",
                                      seen, apply_seen[seen]);
        check("waterlink_apply is the C it replaced, frame for frame and state "
              "for state, on every generated link and body",
              runs == 240 && calls > 25000 && heard > 10000 && apart == 0);
        check("and the generated links and bodies took every path of it",
              missed == 0);
        check("and the normal band's tally by key is the band after every "
              "apply",
              miscounted == 0);
        check("and every key's held frames are in order, within a window of "
              "what was taken, and the ones its chain's end and bits say",
              inconsistent == 0);
}

static fn replay(void)
{
        struct waterlink_replay window;

        memory_zero(address_of window, sizeof window);

        check("the first counter is new",
              waterlink_replay_new(address_of window, 1));
        check("the same counter is not",
              !waterlink_replay_new(address_of window, 1));
        check("a later counter is new",
              waterlink_replay_new(address_of window, 5));
        check("a counter behind it, unseen, is new",
              waterlink_replay_new(address_of window, 3));
        check("and is not new twice",
              !waterlink_replay_new(address_of window, 3));

        //      A jump past the whole window clears it rather than shifting
        //      bits that can no longer mean anything.
        check("a far jump is new",
              waterlink_replay_new(address_of window,
                                   5 + WATERLINK_REPLAY_WINDOW * 2));
        check("what the jump left behind is too old to judge",
              !waterlink_replay_new(address_of window, 5));
        check("the top of the window is still itself",
              !waterlink_replay_new(address_of window,
                                    5 + WATERLINK_REPLAY_WINDOW * 2));

        //      The edge the shift has to get right: exactly one below the
        //      window is out, exactly at the edge is in.
        memory_zero(address_of window, sizeof window);
        check("a fresh window takes a high counter",
              waterlink_replay_new(address_of window,
                                   WATERLINK_REPLAY_WINDOW * 4));
        check("the oldest counter still inside is new",
              waterlink_replay_new(address_of window,
                                   WATERLINK_REPLAY_WINDOW * 4 -
                                           (WATERLINK_REPLAY_WINDOW - 1)));
        check("one below the window is refused",
              !waterlink_replay_new(address_of window,
                                    WATERLINK_REPLAY_WINDOW * 4 -
                                            WATERLINK_REPLAY_WINDOW));

        //      The bit a counter owns is owned again exactly one window
        //      later. If advancing the top does not clear what it passes
        //      over, the older counter's mark is still standing in the way
        //      and the newer one is refused as a replay -- a session that
        //      runs long enough then rejects its own traffic, once per
        //      window, forever. This is that arrival, from both sides.
        memory_zero(address_of window, sizeof window);
        check("a counter arrives", waterlink_replay_new(address_of window, 1));
        check("the counter one window later is new, not a replay",
              waterlink_replay_new(address_of window,
                                   1 + WATERLINK_REPLAY_WINDOW));
        check("and neither is the one after that",
              waterlink_replay_new(address_of window,
                                   2 + WATERLINK_REPLAY_WINDOW));

        //      The same collision, reached one counter at a time rather than
        //      in one jump, which is the path a real session takes.
        memory_zero(address_of window, sizeof window);
        for (p64 step = 1; step <= WATERLINK_REPLAY_WINDOW; step++)
                if (!waterlink_replay_new(address_of window, step))
                {
                        check("a counter walked to in order is new", false);
                        break;
                }
        check("walking one window in order refused nothing",
              window.top == WATERLINK_REPLAY_WINDOW);
        check("and the next counter after a full window is still new",
              waterlink_replay_new(address_of window,
                                   WATERLINK_REPLAY_WINDOW + 1));

        //      The top at the largest counter there is. Every test of a
        //      counter is on the counter and its block, never on top + 1, so
        //      nothing wraps round to the bottom of the window: the last
        //      counter is new once, the window below it still counts, and a
        //      counter a window below the top is too old, not the next one up.
        memory_zero(address_of window, sizeof window);
        check("the largest counter is new",
              waterlink_replay_new(address_of window, ~0ull));
        check("and is not new twice",
              !waterlink_replay_new(address_of window, ~0ull));
        check("the counter below it is new",
              waterlink_replay_new(address_of window, ~0ull - 1));
        check("the oldest counter still in the window is new",
              waterlink_replay_new(address_of window,
                                   ~0ull - (WATERLINK_REPLAY_WINDOW - 1)));
        check("one below the window at the top is refused, not wrapped in",
              !waterlink_replay_new(address_of window,
                                    ~0ull - WATERLINK_REPLAY_WINDOW));
        check("counter zero is too old at the top",
              !waterlink_replay_new(address_of window, 0));
}

static fn saturation(void)
{
        p8 payload[4] = { 1, 1, 1, 1 };
        positive taken = 0;

        waterlink_link_reset(address_of one);

        //      A full queue refuses rather than choosing what to throw away.
        //      Choosing is the caller's, which is why this returns false
        //      instead of quietly dropping the oldest durable frame.
        for (positive at = 0; at < WATERLINK_SLOTS + 8; at++)
                if (waterlink_post(address_of one,
                                   (p8)(at % WATERLINK_KEYS),
                                   WATERLINK_FRAME_DURABLE, payload, 4, 0))
                        taken++;

        check("the queue filled to its ceiling", taken == WATERLINK_SLOTS);
        check("and refused everything past it", one.refused == 8);
}

/*
        The seal against net.c's general AES-128-GCM over the same header,
        nonce and box. They share the two assembly primitives and nothing
        else -- not the counter layout, not the tag's mask, not the lengths
        block -- and the framing is the part written here, so the framing is
        what this compares.
*/
static crypto_aesgcm_key sealing;

static fn datagram_make(p8 address_to datagram, p64 counter, p8 fill)
{
        struct waterlink_datagram head;
        positive used;
        p8 alone = false;

        waterlink_link_reset(address_of one);
        post_one(7, WATERLINK_FRAME_DURABLE, fill, 0);
        post_one(8, WATERLINK_FRAME_REPLACEABLE, (p8)(fill + 1), 0);

        head.kind = WATERLINK_KIND_CARRY;
        head.receiver = 0x01020304;
        head.counter = counter;
        memory_copy(datagram, address_of head, 16);

        used = waterlink_fill(address_of one, datagram + 16, 0,
                              address_of alone);
        //      At the full size, the box every run of segments is made of.
        waterlink_seal_box(address_of sealing, datagram, used,
                           WATERLINK_PAYLOAD);
}

static fn sealed(void)
{
        p8 raw[WATERLINK_AEAD_BYTES];
        p8 datagram[WATERLINK_DATAGRAM];
        p8 plain[WATERLINK_PAYLOAD];
        p8 reference[WATERLINK_PAYLOAD];
        p8 tag[16];
        p8 iv[12];
        positive used;
        p8 alone = false;
        p8 agreed;

        for (positive i = 0; i < sizeof raw; i++)
                raw[i] = (p8)(0x5a ^ (i * 29));
        crypto_aesgcm_prepare(address_of sealing, raw);

        //      The plaintext as it went in, taken before sealing so the
        //      reference encrypts the same padded box.
        waterlink_link_reset(address_of one);
        post_one(7, WATERLINK_FRAME_DURABLE, 'q', 0);
        post_one(8, WATERLINK_FRAME_REPLACEABLE, 'r', 0);
        memory_zero(plain, sizeof plain);
        used = waterlink_fill(address_of one, plain, 0, address_of alone);

        datagram_make(datagram, 0x1122334455667788ull, 'q');

        memory_copy(reference, plain, sizeof reference);
        memory_zero(iv, 4);
        for (positive i = 0; i < 8; i++)
                iv[4 + i] = (p8)(0x1122334455667788ull >> (8 * i));
        crypto_aesgcm_seal(address_of sealing, iv, datagram, 16, reference,
                           WATERLINK_PAYLOAD, tag);

        agreed = !memory_compare(datagram + 16, reference, WATERLINK_PAYLOAD);
        check("the box is net.c's AES-GCM ciphertext", agreed);
        check("the tag is net.c's AES-GCM tag",
              !memory_compare(datagram + 16 + WATERLINK_PAYLOAD, tag, 16));
        check("the header stays in the clear",
              ((struct waterlink_datagram address_to)datagram)->counter ==
                      0x1122334455667788ull);

        check("a sealed datagram opens",
              waterlink_open(address_of sealing, datagram, WATERLINK_DATAGRAM));
        check("and gives back the frames that went in",
              !memory_compare(datagram + 16, plain, WATERLINK_PAYLOAD));

        //      One flipped bit anywhere -- header, box, tag -- and it does
        //      not open. The header is the case that matters most: it is not
        //      encrypted, so only the tag stands between an attacker and a
        //      rewritten counter.
        {
                static const positive where[] = {
                        0, 8, 15, 16, 16 + 24, 16 + WATERLINK_PAYLOAD - 1,
                        16 + WATERLINK_PAYLOAD, WATERLINK_DATAGRAM - 1};
                positive refused = 0;
                p8 wiped = true;

                for (positive at = 0; at < sizeof where / sizeof where[0];
                     at++)
                {
                        datagram_make(datagram, 42, 'q');
                        datagram[where[at]] ^= 0x10;
                        if (!waterlink_open(address_of sealing, datagram, WATERLINK_DATAGRAM))
                        {
                                refused++;
                                for (positive i = 0; i < WATERLINK_PAYLOAD; i++)
                                        if (datagram[16 + i])
                                                wiped = false;
                        }
                }

                check("a flipped bit anywhere stops it opening",
                      refused == sizeof where / sizeof where[0]);
                check("and a refused box is wiped, not left decrypted", wiped);
        }

        //      A datagram sealed under another counter is a different
        //      datagram: moving a header onto someone else's box fails.
        {
                p8 other[WATERLINK_DATAGRAM];

                datagram_make(datagram, 100, 'q');
                datagram_make(other, 101, 'q');
                memory_copy(other, datagram, 16);
                check("a header moved onto another box does not open",
                      !waterlink_open(address_of sealing, other, WATERLINK_DATAGRAM));
        }
}

/*
        A datagram with room left for a frame is sealed at whole blocks, and
        is still net.c's AES-GCM over the same header and the shorter box; one
        without is padded to the full size. The box a full frame leaves is
        always the full one, whatever else rides with it, so a run of them is
        one size for segment offload.
*/
static fn sealed_short(void)
{
        static const positive sizes[] = {
                1, 16, 17, 76, 100, 400, WATERLINK_PAYLOAD - WATERLINK_HEADER_MOST,
                WATERLINK_PAYLOAD - WATERLINK_HEADER_MOST + 1, WATERLINK_PAYLOAD};
        positive agreed = 0;
        positive opened = 0;
        positive refused = 0;

        for (positive at = 0; at < sizeof sizes / sizeof sizes[0]; at++)
        {
                p8 datagram[WATERLINK_DATAGRAM];
                p8 reference[WATERLINK_PAYLOAD];
                p8 tag[16];
                p8 iv[12];
                struct waterlink_datagram head;
                positive box = waterlink_box(sizes[at]);
                positive length;

                head.kind = WATERLINK_KIND_CARRY;
                head.receiver = 7;
                head.counter = 1000 + at;
                memory_copy(datagram, address_of head, 16);
                for (positive i = 0; i < WATERLINK_PAYLOAD; i++)
                        datagram[16 + i] = (p8)(i * 13 + at);
                memory_zero(reference, sizeof reference);
                memory_copy(reference, datagram + 16, sizes[at]);

                length = waterlink_seal(address_of sealing, datagram, sizes[at]);

                memory_zero(iv, 4);
                for (positive i = 0; i < 8; i++)
                        iv[4 + i] = (p8)(head.counter >> (8 * i));
                crypto_aesgcm_seal(address_of sealing, iv, datagram, 16,
                                   reference, box, tag);

                if (length == 32 + box &&
                    !memory_compare(datagram + 16, reference, box) &&
                    !memory_compare(datagram + 16 + box, tag, 16))
                        agreed++;

                if (!waterlink_open(address_of sealing, datagram, length - 16))
                        refused++;
                length = waterlink_seal(address_of sealing, datagram, sizes[at]);
                if (waterlink_open(address_of sealing, datagram, length))
                        opened++;
        }

        check("a box with room for another frame is cut to whole blocks, "
              "and one without is the full payload",
              waterlink_box(0) == 16 && waterlink_box(1) == 16 &&
                      waterlink_box(17) == 32 &&
                      waterlink_box(WATERLINK_PAYLOAD - WATERLINK_HEADER_MOST) ==
                              ((WATERLINK_PAYLOAD - WATERLINK_HEADER_MOST + 15) & ~15u) &&
                      waterlink_box(WATERLINK_PAYLOAD - WATERLINK_HEADER_MOST + 1) ==
                              WATERLINK_PAYLOAD &&
                      waterlink_box(WATERLINK_PAYLOAD) == WATERLINK_PAYLOAD);
        check("a short datagram is net.c's AES-GCM over the shorter box",
              agreed == sizeof sizes / sizeof sizes[0]);
        check("and opens at its own length", opened == agreed);
        check("and not at a length it was not sealed at",
              refused == sizeof sizes / sizeof sizes[0]);

        /*      Length overflow is an old network-parser fault line. Exercise
                both sides of the payload bound and the values whose padding
                additions would wrap, with guards proving a rejected seal
                writes neither the datagram nor its neighbours. */
        {
                static const positive hostile[] = {
                    WATERLINK_PAYLOAD + 1, ~(positive)0,
                    ~(positive)0 - 7, ~(positive)0 - 15};
                struct
                {
                        p64 before[2];
                        p8 datagram[WATERLINK_DATAGRAM];
                        p64 after[2];
                } guarded;
                p8 original[sizeof guarded];
                positive refused_huge = 0;

                memory_fill(address_of guarded, 0xa5, sizeof guarded);
                memory_copy(original, address_of guarded, sizeof guarded);
                for (positive at = 0; at < array_count(hostile); at++)
                        refused_huge +=
                                waterlink_seal(address_of sealing,
                                               guarded.datagram,
                                               hostile[at]) == 0;
                check("sec: oversized and wrapping seal spans are inert",
                      refused_huge == array_count(hostile) &&
                              !memory_compare(original, address_of guarded,
                                              sizeof guarded));
                check("sec: box sizing saturates without integer wrap",
                      waterlink_box(~(positive)0) == WATERLINK_PAYLOAD &&
                              waterlink_box(~(positive)0 - 15) ==
                                      WATERLINK_PAYLOAD);
        }
}

/*
        Every length a box can have, sixteen bytes to the full payload, three
        times over under seeded keys, counters and text, sealed here and by
        net.c's general AES-GCM: the same ciphertext and tag, an open that
        gives the text back, and one flipped bit anywhere in header, box or
        tag that stops it opening and leaves the box wiped.
*/
static crypto_aesgcm_key sealing_generated;

static fn sealed_generated(void)
{
        positive cases = 0, agreed = 0, opened = 0, refused = 0, wiped = 0;
        p64 seed = 0x243f6a8885a308d3ull;
        p8 vaes = cpu_has_vaes;

        //      A whole datagram has a body of its own where the machine has
        //      the wide instructions, so it gets thirty two draws a round,
        //      and the second pass takes them away to hold the other path to
        //      the same answers.
        for (positive pass = 0; pass < 2; pass++)
        for (positive round = 0; round < 3; round++)
                for (positive box = 16; box <= WATERLINK_PAYLOAD; box += 16)
                for (positive draw = 0;
                     draw < (box == WATERLINK_PAYLOAD ? 32u : 1u); draw++)
                {
                        p8 raw[16];
                        p8 datagram[WATERLINK_DATAGRAM];
                        p8 text[WATERLINK_PAYLOAD];
                        p8 tag[16];
                        p8 iv[12];
                        struct waterlink_datagram head;
                        positive used;
                        positive length;
                        positive flip;
                        p8 clean = true;

                        for (positive i = 0; i < 16; i++)
                        {
                                XORSHIFT64(seed);
                                raw[i] = (p8)seed;
                        }
                        cpu_has_vaes = pass ? 0 : vaes;
                        crypto_aesgcm_prepare(address_of sealing_generated, raw);
                        head.kind = WATERLINK_KIND_CARRY;
                        head.receiver = (unsigned int)(seed >> 32);
                        head.counter = seed * 0x9e3779b97f4a7c15ull;
                        used = box - (positive)(seed % 16);
                        memory_zero(text, sizeof text);
                        for (positive i = 0; i < used; i++)
                                text[i] = (p8)(seed >> (i % 56) ^ i);
                        //      Whatever was in the box past used, sealing
                        //      zeroes it.
                        memory_copy(datagram, address_of head, 16);
                        for (positive i = 16; i < sizeof datagram; i++)
                                datagram[i] = (p8)(i * 31 + round);
                        memory_copy(datagram + 16, text, used);

                        waterlink_seal_box(address_of sealing_generated,
                                           datagram, used, box);
                        length = 32 + box;

                        memory_zero(iv, 4);
                        for (positive i = 0; i < 8; i++)
                                iv[4 + i] = (p8)(head.counter >> (8 * i));
                        crypto_aesgcm_seal(address_of sealing_generated, iv,
                                           datagram, 16, text, box, tag);
                        cases++;
                        if (length == 32 + box &&
                            !memory_compare(datagram + 16, text, box) &&
                            !memory_compare(datagram + 16 + box, tag, 16) &&
                            ((struct waterlink_datagram address_to)datagram)->counter ==
                                    head.counter)
                                agreed++;

                        {
                                p8 copy[WATERLINK_DATAGRAM];

                                memory_copy(copy, datagram, sizeof copy);
                                flip = (positive)(seed % length);
                                copy[flip] ^= (p8)(1u << (seed >> 8) % 8);
                                if (!waterlink_open(address_of sealing_generated,
                                                           copy, length))
                                        refused++;
                                for (positive i = 0; i < box; i++)
                                        if (copy[16 + i])
                                                clean = false;
                                if (clean)
                                        wiped++;
                        }

                        memory_zero(text, sizeof text);
                        for (positive i = 0; i < used; i++)
                                text[i] = (p8)(seed >> (i % 56) ^ i);
                        if (waterlink_open(address_of sealing_generated,
                                                  datagram, length) &&
                            !memory_compare(datagram + 16, text, box))
                                opened++;
                }

        cpu_has_vaes = vaes;
        check("every box length seals to net.c's AES-GCM", agreed == cases);
        check("and opens to the text that went in", opened == cases);
        check("and a flipped bit anywhere refuses it", refused == cases);
        check("and wipes the refused box", wiped == cases);
        crypto_forget(address_of sealing_generated, sizeof sealing_generated);
}

/*
        The short boxes a keystroke seals -- one to four blocks, which all
        three machines seal and open in registers, and the first box past
        them -- under every length of text each can hold, with the short
        body and without it (it asks for carry-less multiply beside AES on
        each, so clearing the one byte turns it off everywhere): the
        datagram net.c's AES-GCM makes, nothing written past the tag,
        an open that gives the text back and leaves header and tag as they
        were, and every one-bit change anywhere in header, box or tag
        refused, the box wiped and nothing else touched.
*/
static crypto_aesgcm_key sealing_small;

static fn sealed_small(void)
{
        positive cases = 0, agreed = 0, opened = 0, kept = 0;
        positive flips = 0, refused = 0, wiped = 0, untouched = 0;
        p64 seed = 0x13198a2e03707344ull;
        p8 pclmul = cpu_has_pclmul;

        for (positive pass = 0; pass < 2; pass++)
        for (positive box = 16; box <= 80; box += 16)
        for (positive used = box == 16 ? 0 : box - 15; used <= box; used++)
        for (positive draw = 0; draw < 2; draw++)
        {
                p8 raw[16], text[80], plain[80], tag[16], iv[12];
                p8 datagram[WATERLINK_DATAGRAM], copy[WATERLINK_DATAGRAM];
                p8 before[WATERLINK_DATAGRAM];
                struct waterlink_datagram head;
                positive length = 32 + box;
                p8 clean;

                for (positive i = 0; i < 16; i++)
                {
                        XORSHIFT64(seed);
                        raw[i] = (p8)seed;
                }
                cpu_has_pclmul = pass ? 0 : pclmul;
                crypto_aesgcm_prepare(address_of sealing_small, raw);
                head.kind = WATERLINK_KIND_CARRY;
                head.receiver = (unsigned int)(seed >> 32);
                head.counter = seed * 0x9e3779b97f4a7c15ull;
                memory_zero(plain, sizeof plain);
                for (positive i = 0; i < used; i++)
                        plain[i] = (p8)(seed >> (i % 56) ^ (i * 7));
                memory_copy(text, plain, sizeof text);
                for (positive i = 16; i < sizeof datagram; i++)
                        datagram[i] = (p8)(i * 29 + used);
                memory_copy(datagram, address_of head, 16);
                memory_copy(datagram + 16, plain, used);
                memory_copy(before, datagram, sizeof before);

                waterlink_seal_box(address_of sealing_small, datagram, used,
                                   box);
                memory_zero(iv, 4);
                for (positive i = 0; i < 8; i++)
                        iv[4 + i] = (p8)(head.counter >> (8 * i));
                crypto_aesgcm_seal(address_of sealing_small, iv, datagram, 16,
                                   text, box, tag);
                cases++;
                agreed += !memory_compare(datagram, before, 16) &&
                          !memory_compare(datagram + 16, text, box) &&
                          !memory_compare(datagram + 16 + box, tag, 16) &&
                          !memory_compare(datagram + length, before + length,
                                          sizeof datagram - length);

                for (positive bit = 0; bit < length * 8; bit++)
                {
                        memory_copy(copy, datagram, sizeof copy);
                        copy[bit / 8] ^= (p8)(1u << (bit % 8));
                        memory_copy(before, copy, sizeof before);
                        flips++;
                        refused += !waterlink_open(address_of sealing_small,
                                                   copy, length);
                        clean = true;
                        for (positive i = 0; i < box; i++)
                                if (copy[16 + i])
                                        clean = false;
                        wiped += clean;
                        untouched += !memory_compare(copy, before, 16) &&
                                     !memory_compare(copy + 16 + box,
                                                     before + 16 + box,
                                                     sizeof copy - 16 - box);
                }

                memory_copy(before, datagram, sizeof before);
                opened += waterlink_open(address_of sealing_small, datagram,
                                         length) &&
                          !memory_compare(datagram + 16, plain, box);
                kept += !memory_compare(datagram, before, 16) &&
                        !memory_compare(datagram + 16 + box, before + 16 + box,
                                        sizeof datagram - 16 - box);
        }

        cpu_has_pclmul = pclmul;
        string_format(log, "  short boxes: %p sealed, %p one-bit changes\n",
                      cases, flips);
        check("every short box, at every length of text, seals to net.c's "
              "AES-GCM and writes nothing past its tag",
              cases == 2 * (17 + 16 * 4) * 2 && agreed == cases);
        check("and opens to its text, header and tag as they were",
              opened == cases && kept == cases);
        check("and every one-bit change is refused, the box wiped, nothing "
              "else touched",
              flips > 0 && refused == flips && wiped == flips &&
                      untouched == flips);
        crypto_forget(address_of sealing_small, sizeof sealing_small);
}

//      The whole path with no network: post, fill, seal, open, replay,
//      deliver. This is the datapath the shim will drive, minus the socket.
static fn whole_path(void)
{
        p8 datagram[WATERLINK_DATAGRAM];
        struct waterlink_replay window;
        struct waterlink_datagram head;

        memory_zero(address_of window, sizeof window);

        datagram_make(datagram, 9, 'w');
        check("the datagram opens", waterlink_open(address_of sealing, datagram, WATERLINK_DATAGRAM));

        memory_copy(address_of head, datagram, 16);
        check("its counter is new", waterlink_replay_new(address_of window,
                                                         head.counter));

        waterlink_link_reset(address_of one);
        heard = 0;
        check("its padded box delivers",
              waterlink_deliver(address_of one, datagram + 16,
                                WATERLINK_PAYLOAD, one.clock, hear, null));
        check("both frames arrived, and the padding was not one", heard == 2);
        check("in order", heard_first[0] == 'w' && heard_first[1] == 'x');

        check("the same datagram again is a replay",
              !waterlink_replay_new(address_of window, head.counter));
}

//      The seeded generator both generated checks below draw from.
static p64 traffic_state;

static p64 traffic_next(void)
{
        XORSHIFT64(traffic_state);
        return traffic_state;
}

/*
        The replay window against the set it stands for. A counter is new
        when it is above the highest seen, or within the window below it and
        not seen before; anything further back is refused. The model keeps
        exactly that set, over a region four windows wide that starts at
        zero, somewhere in the middle, or ends at the last counter there is
        -- where clearing the slots the top passes over used to wrap its
        own cursor and never finish.
*/
#define REPLAY_REGION (4 * WATERLINK_REPLAY_WINDOW)

static p8 replay_model[REPLAY_REGION + 1];

static fn replay_generated(void)
{
        positive asked = 0, wrong = 0, topped = 0;

        for (positive seed = 1; seed <= 48; seed++)
        {
                struct waterlink_replay window;
                p64 base;
                p64 top = 0;
                p8 any = false;

                traffic_state = 0xd1b54a32d192ed03ull * seed;
                base = seed % 3 == 0 ? ~0ull - REPLAY_REGION
                     : seed % 3 == 1 ? 0
                                     : traffic_next() >> 2;
                memory_zero(address_of window, sizeof window);
                memory_zero(replay_model, sizeof replay_model);

                for (positive op = 0; op < 3000; op++)
                {
                        p64 roll = traffic_next();
                        p64 offset;
                        p64 counter;
                        p8 want;
                        p8 got;

                        if (roll % 8 == 0)
                                offset = REPLAY_REGION;
                        else if (any && roll % 8 < 5)
                        {
                                p64 at = top - base;
                                p64 back = traffic_next() % (WATERLINK_REPLAY_WINDOW + 64);

                                offset = roll % 2 ? at + traffic_next() % 70
                                                  : (back > at ? 0 : at - back);
                                if (offset > REPLAY_REGION)
                                        offset = REPLAY_REGION;
                        }
                        else
                                offset = traffic_next() % (REPLAY_REGION + 1);
                        counter = base + offset;

                        //      The window starts at top zero with nothing
                        //      seen, so counter zero is new once.
                        if (counter > top)
                                want = true;
                        else
                                want = top - counter < WATERLINK_REPLAY_WINDOW &&
                                       !replay_model[offset];

                        got = waterlink_replay_new(address_of window, counter);
                        asked++;
                        if (got != want)
                                wrong++;
                        if (want)
                        {
                                replay_model[offset] = 1;
                                if (counter > top)
                                        top = counter;
                        }
                        any = true;
                        topped += counter == ~0ull;
                }
        }

        check("the replay window answers as the set it stands for", wrong == 0);
        check("and was asked about the last counter there is", topped > 0);
        check("and asked enough to mean it", asked == 48 * 3000);
}

/*
        The shapes the generated network below reaches only by luck, made on
        purpose. A key's last frame arrives once however often it is sent. A
        register frame superseded while it was in flight goes out as the new
        value, and a late copy of the old one is not applied over it. A
        reader that says "not now" holds the sender back without a frame
        being sent twice.
*/

static fn last_arrives_once(void)
{
        p8 body[WATERLINK_PAYLOAD];
        positive used;
        p8 alone = false;

        waterlink_link_reset(address_of one);
        post_one(40, WATERLINK_FRAME_DURABLE, 'x', 0);
        post_one(40, WATERLINK_FRAME_DURABLE | WATERLINK_FRAME_LAST, 'z', 0);
        check("nothing may follow a key's last frame",
              !waterlink_post(address_of one, 40, WATERLINK_FRAME_DURABLE,
                              body, 1, 0));

        used = waterlink_fill(address_of one, body, 0, address_of alone);
        heard = 0;
        waterlink_deliver(address_of one, body, used, 0, hear, null);
        check("the key's frames arrive", heard == 2);

        //      The acknowledgement is lost, so the probe timer sends the
        //      oldest frame again, and the far side has to say once more that
        //      it has both.
        (void)waterlink_fill(address_of one, body, 0, address_of alone);
        used = waterlink_fill(address_of one, body, 10000000,
                              address_of alone);
        check("an unacknowledged last frame is sent again", used > 0 &&
                                                            one.lost >= 1);
        heard = 0;
        waterlink_deliver(address_of one, body, used, 10000000, hear, null);
        check("and is not delivered twice", heard == 0);

        //      This time the acknowledgement arrives, and the key is gone.
        for (p64 now = 10000001; now < 10000010; now++)
        {
                used = waterlink_fill(address_of one, body, now,
                                      address_of alone);
                if (used)
                        waterlink_deliver(address_of one, body, used, now,
                                             hear, null);
        }
        check("and nothing on the key is delivered again", heard == 0);
        check("an acknowledged last frame leaves the key empty",
              one.sending[40].first == WATERLINK_NONE &&
                      waterlink_idle(address_of one));
}

/*
        An acknowledgement owed but not yet due must not keep a full frame
        back: the frame goes now and the acknowledgement rides the next
        datagram. The room kept for it stopped a full frame fitting, fill
        sent nothing, and the stream stood still until the acknowledgement
        fell due a millisecond later -- stdin through link run ran at 71 MB/s
        over loopback against 890 once fixed.
*/
static fn full_frame_beside_owed_ack(void)
{
        p8 body[WATERLINK_PAYLOAD];
        p8 big[WATERLINK_FRAME_MAX];
        positive used;
        p8 alone = false;

        waterlink_link_reset(address_of one);
        post_one(3, WATERLINK_FRAME_REPLACEABLE, 'c', 0);
        used = waterlink_fill(address_of one, body, 0, address_of alone);
        waterlink_deliver(address_of one, body, used, 0, null, null);
        check("one frame in is an acknowledgement owed, not yet due",
              one.acking == 1ull << 3 && waterlink_fill(address_of one, body, 10,
                                               address_of alone) == 0);

        memory_zero(big, sizeof big);
        big[0] = 'F';
        waterlink_post(address_of one, 4, WATERLINK_FRAME_DURABLE, big,
                       WATERLINK_FRAME_MAX, 20);
        used = waterlink_fill(address_of one, body, 20, address_of alone);
        check("a full frame goes at once beside an acknowledgement owed",
              used > WATERLINK_FRAME_MAX &&
                      waterlink_box(used) == WATERLINK_PAYLOAD);
}

/*
        The acknowledgement is the credit. A reader that will not take a
        frame keeps it held, the sender hears it is held and its loss timer
        does not send it again however long it waits, and a key runs no further than its
        window past what was taken -- then, taken, everything flows.
*/
static p8 refusing;

static p8 hear_or_refuse(address_any context,
                           struct waterlink_frame address_to head,
                           p8 address_to payload)
{
        return !refusing && hear(context, head, payload);
}

static fn pump(p64 from, p64 to)
{
        p8 body[WATERLINK_PAYLOAD];
        p8 alone = false;

        for (p64 now = from; now < to; now++)
        {
                positive used = waterlink_fill(address_of one, body, now,
                                               address_of alone);

                if (used)
                        waterlink_deliver(address_of one, body, used, now,
                                             hear_or_refuse, null);
        }
}

static fn reader_credit(void)
{
        positive posted = 0;
        positive held = 0;

        waterlink_link_reset(address_of one);
        heard = 0;
        refusing = true;
        post_one(5, WATERLINK_FRAME_DURABLE, 'a', 0);
        pump(0, 3000);
        check("a frame the reader will not take is held, not handed on",
              heard == 0 && waterlink_paused(address_of one, 5));
        check("and the sender hears it is held, so nothing is in flight",
              one.flight_head == WATERLINK_NONE &&
                      one.slot[one.sending[5].first].state ==
                              WATERLINK_SLOT_HELD);
        pump(3000, 3001);
        pump(10000000, 10000001);
        check("so the timer sends nothing again", one.timeouts == 0 &&
                                                  one.retransmitted == 0);

        for (positive at = 0; at < 100; at++)
                posted += waterlink_post(address_of one, 5,
                                         WATERLINK_FRAME_DURABLE,
                                         (p8 address_to) "b", 1, 10000001);
        pump(10000001, 10005000);
        for (p32 at = one.receiving[5].first; at != WATERLINK_NONE;
             at = one.held[at].next)
                held++;
        check("a key the reader holds runs no further than its window",
              posted == 100 && held == WATERLINK_KEY_WINDOW && heard == 0);

        refusing = false;
        waterlink_resume(address_of one, 5, hear_or_refuse, null);
        check("taken again, what was held is handed on in order",
              heard == WATERLINK_KEY_WINDOW && heard_first[0] == 'a' &&
                      heard_first[1] == 'b' &&
                      !waterlink_paused(address_of one, 5));
        heard = 0;
        pump(10005000, 10010000);
        check("and the rest follows, with the link left idle",
              one.delivered == 101 && waterlink_idle(address_of one) &&
                      one.retransmitted == 0);
}

/*      A body of frames with no payload on one key, as many as it holds: the
        sequences first, first + 1, and so on round to first again after span
        of them. Nothing a peer sends costs it less. */
static positive flood_body(p8 address_to body, p8 key, p8 flags, p32 first,
                           p32 span, positive most)
{
        positive used = 0;

        memory_zero(body, WATERLINK_PAYLOAD);
        for (positive frame = 0; frame < most; frame++)
        {
                p32 sequence = first + frame % span;

                if (used + 3 + memory_vli_size(sequence) > WATERLINK_PAYLOAD)
                        break;
                body[used++] = flags;
                body[used++] = key;
                used += memory_vli_put(body + used, sequence);
                body[used++] = 0;
        }
        return used;
}

/*
        An authenticated peer that may do nothing still has its frames held:
        the listener that serves it is one thread, so a datagram of frames
        that are of no use to anyone must cost about what a datagram in order
        does. Held are the stream frames ahead of what was taken -- none past
        the window the sender would keep to -- and the registers newer than
        any held; a copy is a bit and an older register is dropped on arrival.
        The cost is read as a ratio to a body of the same size in order, the
        least of many in the same process, so a loaded machine or an emulator
        moves both alike: it was 7 to 17 times when the hold-back walked a
        chain of up to 128 frames to find where a frame went, and a copy
        walked it to the copy.
*/
static positive flood_ticks(positive shape)
{
        p8 body[WATERLINK_PAYLOAD];
        p8 first[WATERLINK_PAYLOAD];
        positive used;
        positive least = ~0ull;

        for (positive round = 0; round < 64; round++)
        {
                p64 start, took;

                waterlink_link_reset(address_of one);
                refusing = shape == 3;
                switch (shape)
                {
                case 1: // sixty four held, behind the one that is missing
                        (void)waterlink_deliver(
                                address_of one, first,
                                flood_body(first, 5, WATERLINK_FRAME_DURABLE, 2, 64, 64),
                                1000, hear, null);
                        used = flood_body(body, 5, WATERLINK_FRAME_DURABLE, 2, 64, 1000);
                        break;
                case 2: // far ahead of anything taken
                        used = flood_body(body, 5, WATERLINK_FRAME_DURABLE, 200, ~0u - 400, 1000);
                        break;
                case 3: // a register's reader that will not take it, then newer values
                        (void)waterlink_deliver(
                                address_of one, first,
                                flood_body(first, 6, WATERLINK_FRAME_REPLACEABLE, 1, 1, 1),
                                1000, hear_or_refuse, null);
                        used = flood_body(body, 6, WATERLINK_FRAME_REPLACEABLE, 1000, ~0u - 2000, 1000);
                        break;
                default: // in order, every frame taken
                        used = flood_body(body, 5, WATERLINK_FRAME_DURABLE, 1, ~0u - 2, 1000);
                }
                start = get_cpu_time();
                (void)waterlink_deliver(address_of one, body, used, 1001 + round,
                                        hear_or_refuse, null);
                took = get_cpu_time() - start;
                if (took < least)
                        least = took;
        }
        refusing = false;
        return least;
}

//      One frame of that, delivered to the link under test.
static p8 flood_one(p8 key, p8 flags, p32 sequence, positive copies)
{
        p8 body[WATERLINK_PAYLOAD];

        return waterlink_deliver(address_of one, body,
                                 flood_body(body, key, flags, sequence, 1,
                                            copies),
                                 1000, hear_or_refuse, null);
}

static fn held_floods(void)
{
        positive total;
        positive in_order = flood_ticks(0);
        positive copies = flood_ticks(1);
        positive ahead = flood_ticks(2);
        positive registers = flood_ticks(3);

        string_format(log, "  held floods, ticks a body: in order %p, copies %p, "
                           "ahead %p, registers %p\n",
                      in_order, copies, ahead, registers);
        check("sec: a body of copies of what a key holds costs about what one "
              "in order does",
              copies < 3 * in_order);
        check("sec: a body of stream frames far ahead of what was taken costs "
              "about what one in order does",
              ahead < 3 * in_order);
        check("sec: a body of registers newer than what is held costs about "
              "what one in order does",
              registers < 3 * in_order);

        waterlink_link_reset(address_of one);
        heard = 0;
        check("a stream frame a window past what was taken is held",
              flood_one(5, WATERLINK_FRAME_DURABLE, WATERLINK_KEY_WINDOW, 1) &&
                      held_consistent(address_of one, address_of total) &&
                      total == 1 && one.kept == 1 && heard == 0);
        check("sec: one past that is dropped, and counted with what the pool "
              "could not take",
              flood_one(5, WATERLINK_FRAME_DURABLE, WATERLINK_KEY_WINDOW + 1, 1) &&
                      held_consistent(address_of one, address_of total) &&
                      total == 1 && one.kept == 1 && one.spilled == 1);
        check("sec: copies of a frame held take nothing more of the pool",
              flood_one(5, WATERLINK_FRAME_DURABLE, WATERLINK_KEY_WINDOW, 3) &&
                      held_consistent(address_of one, address_of total) &&
                      total == 1 && one.kept == 1 && one.spilled == 1);
        {
                p8 body[WATERLINK_PAYLOAD];

                check("and the frames before it, arriving, are handed on in "
                      "order with it, and the pool is empty",
                      waterlink_deliver(address_of one, body,
                                        flood_body(body, 5,
                                                   WATERLINK_FRAME_DURABLE, 1,
                                                   0x7fffffffu,
                                                   WATERLINK_KEY_WINDOW - 1),
                                        1000, hear, null) &&
                              held_consistent(address_of one,
                                              address_of total) &&
                              total == 0 && heard == WATERLINK_KEY_WINDOW &&
                              heard_sequence[WATERLINK_KEY_WINDOW - 1] ==
                                      WATERLINK_KEY_WINDOW);
        }
        check("sec: a window past what was taken is held again once more is "
              "taken, though its low bits are those of a frame that left",
              flood_one(5, WATERLINK_FRAME_DURABLE, 2 * WATERLINK_KEY_WINDOW, 1) &&
                      held_consistent(address_of one, address_of total) &&
                      total == 1 && one.held_mask[5] == 1);

        //      A register the reader would not take, and newer ones: only the
        //      newest is ever handed on, so an older one is not kept.
        waterlink_link_reset(address_of one);
        heard = 0;
        refusing = true;
        (void)flood_one(6, WATERLINK_FRAME_REPLACEABLE, 5, 1);
        (void)flood_one(6, WATERLINK_FRAME_REPLACEABLE, 9, 1);
        (void)flood_one(6, WATERLINK_FRAME_REPLACEABLE, 7, 1);
        check("sec: a register older than one held is not kept",
              held_consistent(address_of one, address_of total) &&
                      total == 2 && one.stale == 1 && heard == 0);
        refusing = false;
        waterlink_resume(address_of one, 6, hear_or_refuse, null);
        check("and the newest is the only one handed on once the reader "
              "comes back",
              heard == 1 && heard_sequence[0] == 9 &&
                      held_consistent(address_of one, address_of total) &&
                      total == 0);
}

/*      Two ends, and the one acknowledgement that would free what the far
        side held lost on the way: the reader refuses three frames, the
        sender hears they are held, the reader comes back and takes them,
        and the network loses the answer. Nothing of the key is in flight,
        so no loss timer runs; waterlink_wake has to be the one that asks. */
static struct waterlink_link held_sender, held_reader;

static fn held_answer_lost(void)
{
        p8 body[WATERLINK_PAYLOAD];
        p8 alone = false;
        positive used;
        p64 now = 1000;
        p64 wake;

        waterlink_link_reset(address_of held_sender);
        waterlink_link_reset(address_of held_reader);
        heard = 0;
        refusing = true;
        for (positive at = 0; at < 3; at++)
                (void)waterlink_post(address_of held_sender, 5,
                                     WATERLINK_FRAME_DURABLE,
                                     (p8 address_to) "abc" + at, 1, now);
        used = waterlink_fill(address_of held_sender, body, now, address_of alone);
        (void)waterlink_deliver(address_of held_reader, body, used, now,
                                hear_or_refuse, null);
        used = waterlink_fill(address_of held_reader, body, now + 1, address_of alone);
        (void)waterlink_deliver(address_of held_sender, body, used, now + 2,
                                null, null);
        refusing = false;
        waterlink_resume(address_of held_reader, 5, hear_or_refuse, null);
        (void)waterlink_fill(address_of held_reader, body, now + 3, address_of alone);
        check("sec: a link whose frames the far side still holds is not idle",
              !waterlink_idle(address_of held_sender));
        check("sec: a held key whose freeing acknowledgement was lost is due "
              "to be asked about",
              heard == 3 && held_sender.flight_head == WATERLINK_NONE &&
                      (wake = waterlink_wake(address_of held_sender, now + 3)) >
                              now + 3 &&
                      wake != ~0ull);
        now += 3000000;
        used = waterlink_wake(address_of held_sender, now) == now
                       ? waterlink_fill(address_of held_sender, body, now,
                                        address_of alone)
                       : 0;
        if (used)
                (void)waterlink_deliver(address_of held_reader, body, used, now,
                                        hear_or_refuse, null);
        used = waterlink_fill(address_of held_reader, body, now + 1, address_of alone);
        if (used)
                (void)waterlink_deliver(address_of held_sender, body, used,
                                        now + 2, null, null);
        check("sec: and the copy it sends is answered, freeing the key",
              heard == 3 && waterlink_idle(address_of held_sender) &&
                      held_sender.free_count == WATERLINK_SLOTS &&
                      held_sender.retransmitted == 1);
}

/*      A register's value replaced where it stands at the front of the
        normal band takes the newest sequence and can be past its key's
        window, with an older frame of the same key behind it that is not:
        a walk that took the first as the key blocked would stop there, and
        the older frame would wait for the window with nothing to open it. */
static struct waterlink_link register_link;

static fn register_behind_its_newest(void)
{
        struct waterlink_link address_to l = address_of register_link;
        p8 body[WATERLINK_PAYLOAD];
        p8 alone = false;
        p8 value = 'v';
        p32 slot[5];
        p64 now = 1000;
        p64 wake;

        waterlink_link_reset(l);
        //      Normal, urgent, normal, urgent, normal: five slots of key 3.
        for (positive at = 0; at < 5; at++)
        {
                (void)waterlink_post(l, 3,
                                     WATERLINK_FRAME_REPLACEABLE |
                                             (at & 1 ? WATERLINK_FRAME_URGENT : 0),
                                     address_of value, 1, now);
                slot[at] = l->sending[3].last;
                while (waterlink_fill(l, body, now, address_of alone))
                        ;
        }
        //      The newest lost first, then the older normal one behind it.
        waterlink_lose(l, slot[4]);
        waterlink_lose(l, slot[2]);
        for (positive at = 0; at < WATERLINK_KEY_WINDOW; at++)
                (void)waterlink_post(l, 3, WATERLINK_FRAME_REPLACEABLE,
                                     address_of value, 1, now);
        check("a register replaced where it stands, past its window, stands "
              "ahead of an older frame of its key",
              l->head[WATERLINK_BAND_NORMAL] == slot[4] &&
                      l->slot[slot[4]].next == slot[2] &&
                      waterlink_key_blocked(l, l->slot + slot[4]) &&
                      !waterlink_key_blocked(l, l->slot + slot[2]));
        wake = waterlink_wake(l, now);
        (void)waterlink_fill(l, body, now, address_of alone);
        check("sec: the older frame is not taken for blocked with it: wake "
              "answers now, and fill sends it",
              wake == now && l->slot[slot[2]].state == WATERLINK_SLOT_FLIGHT &&
                      l->slot[slot[4]].state == WATERLINK_SLOT_QUEUED);
}

/*      A frame the path lost several times before it arrived and was held:
        its first question is one probe timer away, not one doubled for
        every time it went out. */
static fn held_after_losses(void)
{
        p8 body[WATERLINK_PAYLOAD];
        p8 alone = false;
        positive used = 0;
        p64 now = 1000;
        p64 wake;
        p32 at;

        waterlink_link_reset(address_of held_sender);
        waterlink_link_reset(address_of held_reader);
        heard = 0;
        refusing = true;
        for (positive at = 0; at < 2; at++)
                (void)waterlink_post(address_of held_sender, 5,
                                     WATERLINK_FRAME_DURABLE,
                                     (p8 address_to) "ab" + at, 1, now);
        //      Sent, lost, and sent again at each expiry.
        for (positive round = 0; round < 6; round++)
        {
                used = waterlink_fill(address_of held_sender, body, now,
                                      address_of alone);
                now += 3000000;
        }
        now -= 3000000 - 10;
        at = held_sender.sending[5].first;
        (void)waterlink_deliver(address_of held_reader, body, used, now,
                                hear_or_refuse, null);
        now += WATERLINK_ACK_DELAY;
        used = waterlink_fill(address_of held_reader, body, now, address_of alone);
        (void)waterlink_deliver(address_of held_sender, body, used, now, null,
                                null);
        check("frames lost again and again, then held, went out five times",
              held_sender.slot[at].tries == 5 &&
                      held_sender.slot[at].state == WATERLINK_SLOT_HELD);
        wake = waterlink_wake(address_of held_sender, now);
        check("sec: its first question is one probe timer after it was sent",
              wake > now &&
                      wake <= held_sender.slot[at].sent +
                                      waterlink_timeout(address_of held_sender));
        check("and at it the frame goes back to be sent, its question counted",
              waterlink_wake(address_of held_sender, wake) == wake &&
                      held_sender.slot[at].state == WATERLINK_SLOT_QUEUED &&
                      held_sender.slot[at].probed == 1);
}

/*      The wire judge already refuses channel bytes above 63.  The application
        entry points are also called by the service, though, so their own
        boundary is a fuse: a bad channel cannot index past a table, and a
        refused post cannot poison the link's monotonic clock. Put sentinels
        around a link and try every invalid key, flag byte and frame length,
        rather than checking only the first invalid value. */
static fn invalid_application_keys(void)
{
        struct
        {
                p64 before[2];
                struct waterlink_link link;
                p64 after[2];
        } guarded;
        struct waterlink_link original;
        positive wrong = 0;
        positive rejected = 0;
        p64 refused;

        memory_fill(address_of guarded, 0xa5, sizeof guarded);
        waterlink_link_reset(address_of guarded.link);
        refused = guarded.link.refused;
        original = guarded.link;
        for (positive key = WATERLINK_KEYS; key < 256; key++)
        {
                wrong += waterlink_paused(address_of guarded.link, (p8)key);
                waterlink_resume(address_of guarded.link, (p8)key,
                                 hear_or_refuse, null);
                wrong += waterlink_post(address_of guarded.link, (p8)key,
                                        WATERLINK_FRAME_DURABLE, null,
                                        WATERLINK_FRAME_MAX, ~0ull);
                rejected++;
        }
        for (positive flags = 0; flags < 256; flags++)
                if (!waterlink_frame_sane((p8)flags))
                {
                        wrong += waterlink_post(address_of guarded.link, 0,
                                                (p8)flags, null, 0, ~0ull);
                        rejected++;
                }
        for (positive length = WATERLINK_FRAME_MAX + 1;
             length <= (positive)(p16)~0; length++)
        {
                wrong += waterlink_post(address_of guarded.link, 0,
                                        WATERLINK_FRAME_DURABLE, null,
                                        (p16)length, ~0ull);
                rejected++;
        }
        original.refused += rejected;
        check("sec: invalid application posts are inert", wrong == 0 &&
                      guarded.link.refused == refused + rejected &&
                      !memory_compare(address_of original,
                                      address_of guarded.link,
                                      sizeof original) &&
                      guarded.before[0] == 0xa5a5a5a5a5a5a5a5ull &&
                      guarded.before[1] == 0xa5a5a5a5a5a5a5a5ull &&
                      guarded.after[0] == 0xa5a5a5a5a5a5a5a5ull &&
                      guarded.after[1] == 0xa5a5a5a5a5a5a5a5ull);
}

static fn superseded_in_flight(void)
{
        p8 old_body[WATERLINK_PAYLOAD];
        p8 body[WATERLINK_PAYLOAD];
        positive old_used;
        positive used;
        p8 alone = false;

        waterlink_link_reset(address_of one);
        post_one(9, WATERLINK_FRAME_REPLACEABLE, '1', 0);
        old_used = waterlink_fill(address_of one, old_body, 0,
                                  address_of alone);
        post_one(9, WATERLINK_FRAME_REPLACEABLE, '2', 0);
        check("a frame in flight is superseded, not queued behind",
              one.superseded == 1 && one.posted == 1);
        used = waterlink_fill(address_of one, body, 0, address_of alone);

        heard = 0;
        waterlink_deliver(address_of one, body, used, 0, hear, null);
        waterlink_deliver(address_of one, old_body, old_used, 0, hear,
                             null);
        check("the new value arrives and the old one after it is dropped",
              heard == 1 && heard_first[0] == '2');
}

/*
        The link over a generated network, both ways at once.

        Each seed builds a path -- a bottleneck with a rate and a queue that
        drops when full, a propagation delay, jitter that reorders, random
        loss and duplication -- and a workload on both ends: streams, urgent
        keystrokes, bulk runs, registers, keys that end, and streams whose
        reader keeps saying "not now" and comes back later. The receiving
        application is the oracle: on every key, what it is handed must move
        forward, skip nothing on a stream, and carry exactly the bytes
        posted; when the workload is done and the network drained, every key
        must have arrived through its last post. Half the seeds put the datagram replay
        window in front of the link and half hand it the network's copies
        raw, so the link's own answer to a repeated frame is tested too.

        Seeds of the last kind carry bulk only over a clean bottleneck, and
        answer for the path: what arrived against what the bottleneck could
        carry, and how much the queue dropped.
*/
#define SIM_KEYS 12
#define SIM_POSTS 512
#define SIM_IN_FLIGHT 1024
#define SIM_HEAP (2 * SIM_IN_FLIGHT)

typedef struct {
        p64 arrive;
        p64 counter;
        positive used;
        p8 to;
        p8 body[WATERLINK_PAYLOAD];
} sim_datagram;

static sim_datagram sim_pool[SIM_IN_FLIGHT];
static p32 sim_free_list[SIM_IN_FLIGHT];
static positive sim_free_count;
static p32 sim_heap[SIM_IN_FLIGHT];
static positive sim_heap_count;

static struct waterlink_link sim_link[2];
static struct waterlink_replay sim_window[2];

typedef struct {
        p8 flags;
        p8 slow;       // its reader refuses a third of what it is handed
        p8 ends;       // the key's last post is flagged LAST
        p8 size;       // 0 small, 1 medium, 2 bulk
        positive posts;       // how many this key will get
        positive posted;      // how many it has had
        bipolar delivered;    // the index last handed over, -1 for none
        positive handed;
} sim_key;

typedef struct {
        sim_key key[SIM_KEYS];
        positive keys;
        p64 next_post;
        positive remaining;
        p64 counter;
        //      The path out of this end.
        p64 spacing;  // microseconds a datagram holds the bottleneck
        p64 free_at;
        positive queue;
        p64 delay;
        p64 jitter;
        positive loss;   // per 10,000
        positive repeat; // per 10,000
        //      What happened.
        positive sent;
        positive dropped;
        positive lost;
        p64 bytes;
        p64 first_post;
        p64 last_arrival;
        p64 resume_at; // when a slow reader at this end comes back
        positive refusals;
        p8 wrong;
        p8 bulk;
} sim_end;

static sim_end sim_ends[2];
static positive sim_wrong;
static p64 sim_rng;

static p64 sim_next(void)
{
        XORSHIFT64(sim_rng);
        return sim_rng;
}

static positive sim_below(positive bound)
{
        return bound ? (positive)(sim_next() % bound) : 0;
}

static fn sim_heap_push(p32 at)
{
        positive i = sim_heap_count++;

        sim_heap[i] = at;
        while (i && sim_pool[sim_heap[(i - 1) / 2]].arrive >
                            sim_pool[sim_heap[i]].arrive)
        {
                p32 swap = sim_heap[i];

                sim_heap[i] = sim_heap[(i - 1) / 2];
                sim_heap[(i - 1) / 2] = swap;
                i = (i - 1) / 2;
        }
}

static p32 sim_heap_pop(void)
{
        p32 top = sim_heap[0];
        positive i = 0;

        sim_heap[0] = sim_heap[--sim_heap_count];
        for (;;)
        {
                positive l = 2 * i + 1;
                positive r = l + 1;
                positive m = i;
                p32 swap;

                if (l < sim_heap_count &&
                    sim_pool[sim_heap[l]].arrive < sim_pool[sim_heap[m]].arrive)
                        m = l;
                if (r < sim_heap_count &&
                    sim_pool[sim_heap[r]].arrive < sim_pool[sim_heap[m]].arrive)
                        m = r;
                if (m == i)
                        break;
                swap = sim_heap[i];
                sim_heap[i] = sim_heap[m];
                sim_heap[m] = swap;
                i = m;
        }
        return top;
}

//      One datagram onto the path from end `from`: the bottleneck, then the
//      wire's loss, jitter and duplication.
static fn sim_send(positive from, p8 address_to body, positive used, p64 now)
{
        sim_end address_to edge = sim_ends + from;
        p64 depart;
        //      A datagram holds the bottleneck for its share of a full one.
        p64 cost = edge->spacing * (32 + waterlink_box(used)) /
                   WATERLINK_DATAGRAM;
        positive copies;

        if (!cost)
                cost = 1;

        edge->sent++;
        if (edge->free_at > now &&
            (edge->free_at - now) / edge->spacing >= edge->queue)
        {
                edge->dropped++;
                return;
        }

        depart = (edge->free_at > now ? edge->free_at : now) + cost;
        edge->free_at = depart;

        if (sim_below(10000) < edge->loss)
        {
                edge->lost++;
                return;
        }

        copies = sim_below(10000) < edge->repeat ? 2 : 1;
        edge->counter++;

        while (copies--)
        {
                sim_datagram address_to d;
                p32 at;

                if (!sim_free_count)
                {
                        edge->lost++;
                        return;
                }

                at = sim_free_list[--sim_free_count];
                d = sim_pool + at;
                d->arrive = depart + edge->delay +
                            (edge->jitter ? sim_next() % edge->jitter : 0);
                d->counter = edge->counter;
                //      The box as the seal leaves it, zeros to its end.
                d->used = waterlink_box(used);
                d->to = (p8)(1 - from);
                memory_copy(d->body, body, used);
                memory_zero(d->body + used, d->used - used);
                sim_heap_push(at);
        }
}

//      A frame's payload says which key and which post it is, and the rest
//      is a pattern of both, so a wrong byte anywhere is seen.
static p16 sim_length(sim_key address_to key, positive index)
{
        positive base = key->size == 2 ? 900 : key->size == 1 ? 120 : 6;

        return (p16)(base + (index * 7 + key->posts) % 97);
}

static fn sim_fill(p8 address_to into, positive k, positive index, p16 length)
{
        into[0] = (p8)k;
        into[1] = (p8)index;
        into[2] = (p8)(index >> 8);
        for (positive i = 3; i < length; i++)
                into[i] = (p8)(k * 31 + index * 7 + i);
}

static fn sim_wrong_at(sim_end address_to edge)
{
        if (!edge->wrong)
                sim_wrong++;
        edge->wrong = true;
}

static p64 sim_now;

static p8 sim_hear(address_any context, struct waterlink_frame address_to head,
                     p8 address_to payload)
{
        sim_end address_to edge = sim_ends + (positive)context;
        positive k;
        positive index;
        sim_key address_to key;
        p8 want[WATERLINK_FRAME_MAX];

        if (head->length < 3)
        {
                sim_wrong_at(edge);
                return true;
        }

        k = payload[0];
        index = payload[1] | (positive)payload[2] << 8;
        if (k >= edge->keys || index >= edge->key[k].posted)
        {
                sim_wrong_at(edge);
                return true;
        }

        key = edge->key + k;

        //      A slow reader: the frame stays with the link until it comes
        //      back for it.
        if (key->slow && !sim_below(3))
        {
                sim_ends[1 - (positive)context].resume_at =
                        sim_now + 1 + sim_below(3000);
                edge->refusals++;
                return false;
        }

        //      Forward only, and nothing skipped on a stream.
        if ((bipolar)index <= key->delivered ||
            ((key->flags & WATERLINK_FRAME_DURABLE) &&
             (bipolar)index != key->delivered + 1))
        {
                sim_wrong_at(edge);
                return true;
        }

        if (head->length != sim_length(key, index) || head->key != k)
                sim_wrong_at(edge);
        else
        {
                sim_fill(want, k, index, head->length);
                if (memory_compare(want, payload, head->length))
                        sim_wrong_at(edge);
        }

        key->delivered = (bipolar)index;
        key->handed++;
        edge->bytes += head->length;
        return true;
}

static fn sim_workload(sim_end address_to edge, p8 bulk_only)
{
        memory_zero(edge, sizeof(address_to edge));
        edge->bulk = bulk_only;
        edge->keys = bulk_only ? 2 + sim_below(5) : 4 + sim_below(SIM_KEYS - 3);

        for (positive k = 0; k < edge->keys; k++)
        {
                sim_key address_to key = edge->key + k;
                positive shape = bulk_only ? 0 : sim_below(6);

                key->delivered = -1;
                key->posts = bulk_only ? 200 + sim_below(300)
                                       : 5 + sim_below(80);

                switch (shape)
                {
                case 0: // a stream, bulk or not
                        key->flags = WATERLINK_FRAME_DURABLE;
                        key->size = bulk_only ? 2 : (p8)sim_below(3);
                        break;
                case 1: // keystrokes
                        key->flags = WATERLINK_FRAME_DURABLE |
                                     WATERLINK_FRAME_URGENT;
                        break;
                case 2: // a register, small
                        key->flags = WATERLINK_FRAME_REPLACEABLE;
                        key->size = (p8)sim_below(2);
                        break;
                case 3: // a register, any size
                        key->flags = WATERLINK_FRAME_REPLACEABLE;
                        key->size = (p8)sim_below(3);
                        break;
                case 4: // a stream whose reader is slow
                        key->flags = WATERLINK_FRAME_DURABLE;
                        key->slow = 1;
                        key->size = (p8)sim_below(3);
                        break;
                default: // a key that ends
                        key->flags = WATERLINK_FRAME_DURABLE;
                        key->ends = 1;
                        key->posts = 1 + sim_below(6);
                        break;
                }

                edge->remaining += key->posts;
        }
}

//      The next post from this end, if there is one due: a key chosen at
//      random among those with posts left. A refusal keeps it for later.
static fn sim_post(sim_end address_to edge, positive side, p64 now)
{
        while (edge->remaining && edge->next_post <= now)
        {
                positive k = sim_below(edge->keys);
                sim_key address_to key;
                p8 payload[WATERLINK_FRAME_MAX];
                p8 flags;
                p16 length;
                positive index;
                p8 last_post;

                for (positive tries = 0;
                     edge->key[k].posted >= edge->key[k].posts &&
                     tries < SIM_KEYS;
                     tries++)
                        k = (k + 1) % edge->keys;

                key = edge->key + k;
                if (key->posted >= key->posts)
                        return;

                index = key->posted;
                last_post = index + 1 == key->posts;
                flags = key->flags;
                if (key->ends && last_post)
                        flags |= WATERLINK_FRAME_LAST;

                length = sim_length(key, index);
                sim_fill(payload, k, index, length);

                if (!waterlink_post(sim_link + side, (p8)k, flags, payload,
                                    length, now))
                {
                        //      The queue is full: the application waits for
                        //      an acknowledgement to free a slot.
                        edge->next_post = ~0ull;
                        return;
                }

                if (!edge->first_post)
                        edge->first_post = now ? now : 1;
                key->posted++;
                edge->remaining--;
                edge->next_post = edge->bulk ? now : now + sim_below(1500);
        }
}

static p8 sim_scratch[WATERLINK_PAYLOAD];


static fn sim_drain_sends(positive side, p64 now)
{
        for (positive guard = 0; guard < 4096; guard++)
        {
                p8 alone = false;
                positive used = waterlink_fill(sim_link + side, sim_scratch,
                                               now, address_of alone);

                if (!used)
                        return;
                sim_send(side, sim_scratch, used, now);
        }
}

typedef struct {
        positive seeds;
        positive schedules_bulk;
        p64 posts;
        p64 delivered;
        p64 lost;
        p64 retransmitted;
        p64 spilled;
        p64 kept;
        p64 refusals;
        p64 superseded;
        p64 timeouts;
        p64 unfinished;
        //      The clean-bottleneck seeds, summed.
        p64 path_bytes;
        p64 path_capacity;
        //      The bulk schedule slowest against what its path allows.
        p64 slow_span;
        p64 slow_ideal;
        p64 path_sent;
        p64 path_dropped;
} sim_tally;

static sim_tally sim_total;

static fn sim_seed(positive seed)
{
        p8 bulk_path = seed % 5 == 0;
        p8 filtered = seed % 2 == 0;
        p64 now = 0;
        p64 limit;

        sim_rng = 0x9e3779b97f4a7c15ull * (seed + 1) ^ 0x2545f4914f6cdd1dull;
        for (positive i = 0; i < 8; i++)
                (void)sim_next();

        waterlink_link_reset(sim_link + 0);
        waterlink_link_reset(sim_link + 1);
        memory_zero(sim_window, sizeof sim_window);
        sim_heap_count = 0;
        sim_free_count = SIM_IN_FLIGHT;
        for (positive i = 0; i < SIM_IN_FLIGHT; i++)
                sim_free_list[i] = (p32)(SIM_IN_FLIGHT - 1 - i);

        for (positive side = 0; side < 2; side++)
        {
                sim_end address_to edge = sim_ends + side;

                //      Bulk seeds send one way over a clean bottleneck.
                sim_workload(edge, bulk_path && side == 0);
                if (bulk_path && side == 1)
                        edge->remaining = 0;

                edge->spacing = 8 + sim_below(120);
                edge->queue = 8 + sim_below(96);
                edge->delay = bulk_path ? 100 + sim_below(1500)
                                       : 200 + sim_below(20000);
                edge->jitter = bulk_path ? 0 : sim_below(8000);
                edge->loss = bulk_path ? 0 : sim_below(4) ? sim_below(1500) : 0;
                edge->repeat = bulk_path ? 0 : sim_below(800);
                edge->next_post = sim_below(1000);
        }

        limit = 600000000ull; // ten minutes of simulated time
        for (positive events = 0; now < limit && events < 4000000; events++)
        {
                p64 next = ~0ull;

                sim_now = now;
                while (sim_heap_count && sim_pool[sim_heap[0]].arrive <= now)
                {
                        p32 at = sim_heap_pop();
                        sim_datagram address_to d = sim_pool + at;
                        positive to = d->to;

                        sim_free_list[sim_free_count++] = at;
                        if (filtered &&
                            !waterlink_replay_new(sim_window + to, d->counter))
                                continue;
                        //      The oracle is the end that posted.
                        if (!waterlink_deliver(sim_link + to, d->body,
                                                  d->used, now, sim_hear,
                                                  (address_any)(1 - to)))
                                sim_wrong_at(sim_ends + 1 - to);
                        sim_ends[1 - to].last_arrival = now;
                        //      An acknowledgement may have freed slots.
                        if (sim_ends[to].next_post == ~0ull)
                                sim_ends[to].next_post = now;
                }

                //      A slow reader comes back for what it left.
                for (positive to = 0; to < 2; to++)
                        if (sim_ends[to].resume_at &&
                            sim_ends[to].resume_at <= now)
                        {
                                sim_ends[to].resume_at = 0;
                                for (positive k = 0; k < sim_ends[1 - to].keys;
                                     k++)
                                        if (waterlink_paused(sim_link + to,
                                                             (p8)k))
                                                waterlink_resume(
                                                        sim_link + to, (p8)k,
                                                        sim_hear,
                                                        (address_any)(1 - to));
                        }

                for (positive side = 0; side < 2; side++)
                        sim_post(sim_ends + side, side, now);

                for (positive side = 0; side < 2; side++)
                {
                        sim_drain_sends(side, now);
                        //      So may a loss the fill found expired.
                        if (sim_ends[side].next_post == ~0ull &&
                            sim_link[side].free != WATERLINK_NONE)
                                sim_ends[side].next_post = now + 1;
                }

                for (positive side = 0; side < 2; side++)
                {
                        p64 wake = waterlink_wake(sim_link + side, now);

                        if (wake <= now)
                                wake = now + 1;
                        if (wake < next)
                                next = wake;
                        if (sim_ends[side].remaining &&
                            sim_ends[side].next_post < next)
                                next = sim_ends[side].next_post > now
                                               ? sim_ends[side].next_post
                                               : now + 1;
                }

                if (sim_heap_count && sim_pool[sim_heap[0]].arrive < next)
                        next = sim_pool[sim_heap[0]].arrive;
                for (positive side = 0; side < 2; side++)
                        if (sim_ends[side].resume_at &&
                            sim_ends[side].resume_at < next)
                                next = sim_ends[side].resume_at > now
                                               ? sim_ends[side].resume_at
                                               : now + 1;

                if (next == ~0ull)
                        break;
                now = next > now ? next : now + 1;
        }

        for (positive side = 0; side < 2; side++)
        {
                sim_end address_to edge = sim_ends + side;
                struct waterlink_link address_to link = sim_link + side;
                p8 finished = !edge->remaining &&
                                waterlink_idle(sim_link + 0) &&
                                waterlink_idle(sim_link + 1);

                for (positive k = 0; k < edge->keys; k++)
                        if (edge->key[k].delivered + 1 !=
                            (bipolar)edge->key[k].posted)
                                finished = false;

                if (!finished)
                {
                        sim_total.unfinished++;
                        sim_wrong_at(edge);
                }

                sim_total.posts += link->posted + link->superseded;
                sim_total.lost += link->lost;
                sim_total.retransmitted += link->retransmitted;
                sim_total.spilled += sim_link[1 - side].spilled;
                sim_total.kept += sim_link[1 - side].kept;
                sim_total.refusals += edge->refusals;
                sim_total.superseded += link->superseded;
                sim_total.timeouts += link->timeouts;
                for (positive k = 0; k < edge->keys; k++)
                        sim_total.delivered += edge->key[k].handed;
        }

        if (bulk_path)
        {
                sim_end address_to edge = sim_ends + 0;
                p64 span = edge->last_arrival - edge->first_post;

                sim_total.schedules_bulk++;
                sim_total.path_bytes += edge->bytes;
                //      What the bottleneck carries, or what the link's own
                //      ceilings let be in flight over the round trip if
                //      that is less -- the check is of the window, not of
                //      the slot count.
                {
                        //      There and back: each way has its own delay.
                        p64 trip = edge->delay + sim_ends[1].delay +
                                   2 * edge->spacing;
                        p64 flying = edge->keys * WATERLINK_KEY_WINDOW;
                        p64 by_rate = span / edge->spacing;
                        p64 by_window;

                        p64 capacity;

                        if (flying > WATERLINK_SLOTS)
                                flying = WATERLINK_SLOTS;
                        by_window = span * flying / trip;
                        capacity = (by_rate < by_window ? by_rate : by_window) *
                                   WATERLINK_FRAME_MAX;
                        //      What the path allows: every frame through the
                        //      bottleneck, and a few round trips to start.
                        p64 ideal = edge->bytes / WATERLINK_FRAME_MAX *
                                            edge->spacing +
                                    4 * trip;

                        sim_total.path_capacity += capacity;
                        if (!sim_total.slow_ideal ||
                            span * sim_total.slow_ideal >
                                    sim_total.slow_span * ideal)
                        {
                                sim_total.slow_span = span;
                                sim_total.slow_ideal = ideal;
                        }
                }
                sim_total.path_sent += edge->sent;
                sim_total.path_dropped += edge->dropped;
        }

        sim_total.seeds++;
}

static fn network_generated(void)
{
        positive seeds = 2000;

        memory_zero(address_of sim_total, sizeof sim_total);
        sim_wrong = 0;

        for (positive seed = 0; seed < seeds; seed++)
                sim_seed(seed);

        string_format(log, "  waterlink network: %p schedules (%p bulk), "
                           "%p frames posted, %p delivered, %p lost, %p sent "
                           "again, %p held back, %p spilled, %p refused by a "
                           "slow reader, "
                           "%p timeouts\n",
                      sim_total.seeds, sim_total.schedules_bulk,
                      (positive)sim_total.posts, (positive)sim_total.delivered,
                      (positive)sim_total.lost,
                      (positive)sim_total.retransmitted,
                      (positive)sim_total.kept, (positive)sim_total.spilled,
                      (positive)sim_total.refusals,
                      (positive)sim_total.timeouts);
        string_format(log, "  waterlink link: %p bytes a session\n",
                      (positive)sizeof(struct waterlink_link));
        string_format(log, "  waterlink path: %p of %p bytes the bottleneck "
                           "could carry, %p of %p datagrams dropped at it\n",
                      (positive)sim_total.path_bytes,
                      (positive)sim_total.path_capacity,
                      (positive)sim_total.path_dropped,
                      (positive)sim_total.path_sent);

        check("every schedule delivered every key exactly as the rules say",
              sim_wrong == 0);
        check("and finished", sim_total.unfinished == 0);
        check("the network lost frames and the link sent them again",
              sim_total.lost > 0 && sim_total.retransmitted > 0);
        check("frames were held back for the frame they follow",
              sim_total.kept > 0);
        check("and the hold-back overflowed, and that was recovered",
              sim_total.spilled > 0);
        check("register frames were superseded", sim_total.superseded > 0);
        check("slow readers held frames back and every one was taken later",
              sim_total.refusals > 0);
        check("the timer fired", sim_total.timeouts > 0);
        check("a bulk path is kept at least half full",
              sim_total.path_bytes * 2 >= sim_total.path_capacity);
        string_format(log, "  waterlink slowest bulk: %p us where its path "
                           "allows %p\n",
                      (positive)sim_total.slow_span,
                      (positive)sim_total.slow_ideal);
        check("and no bulk path stalls: each takes under eight times what "
              "its bottleneck and round trip allow",
              sim_total.slow_span < 8 * sim_total.slow_ideal);
        check("and its queue drops fewer than one datagram in ten",
              sim_total.path_dropped * 10 <= sim_total.path_sent);
}

static fn clock_ordering(void)
{
        struct waterlink_link link;

        waterlink_link_reset(address_of link);
        link.acking = 1;
        link.owed = 2000000;
        check("sec: a clock sample before an owed acknowledgement cannot make it due",
              !waterlink_ack_due(address_of link, 1000000));
        {
                //      The same through waterlink_fill, which is assembly on
                //      every machine: its own compare of now with owed.
                p8 body[WATERLINK_PAYLOAD];
                p8 alone = false;

                check("sec: a body filled before an owed acknowledgement carries none",
                      waterlink_fill(address_of link, body, 1000000,
                                     address_of alone) == 0);
                check("the same acknowledgement is written once it is due",
                      waterlink_fill(address_of link, body,
                                     2000000 + WATERLINK_ACK_DELAY,
                                     address_of alone) > 0);
        }

        waterlink_link_reset(address_of link);
        link.free_count--;
        link.free = 1;
        link.flight_head = link.flight_tail = 0;
        link.slot[0].next = WATERLINK_NONE;
        link.slot[0].state = WATERLINK_SLOT_FLIGHT;
        link.slot[0].serial = 1;
        link.slot[0].sent = 2000000;
        link.largest = 2;
        waterlink_losses(address_of link, 1000000);
        check("sec: a clock sample before a flight cannot declare that frame lost",
              link.flight_head == 0 && link.slot[0].state == WATERLINK_SLOT_FLIGHT &&
                  !link.timeouts);
}

/*
        The handshake, both ends in one process: the keys agree, a wrong
        mac1, wrong padding or a message to the wrong key is refused at the
        gate before any curve work, a tampered static or hello is refused
        after it, an initiation made again is not newer, and the admission
        bucket holds a flood to its burst. Byte-for-byte agreement with an
        independent Noise is the harness's (differential.py --harness
        waterlink_noise), fed by this binary's `noise` mode.
*/
static fn identity_seeded(struct waterlink_identity address_to identity,
                          p8 seed)
{
        p8 secret[32];

        for (positive i = 0; i < 32; i++)
                secret[i] = (p8)(seed * 37 + i * 11 + 1);
        waterlink_identity_from(identity, secret);
}

static fn random_seeded(p8 address_to into, positive length, p8 seed)
{
        for (positive i = 0; i < length; i++)
                into[i] = (p8)(seed * 91 + i * 17 + 5);
}

static fn handshake(void)
{
        struct waterlink_identity alice, bob, eve;
        struct waterlink_noise starting, answering, starting_kept;
        p8 first[WATERLINK_DATAGRAM];
        p8 second[WATERLINK_DATAGRAM];
        p8 kept[WATERLINK_DATAGRAM];
        p8 hello[WATERLINK_HELLO_BYTES];
        p8 heard_hello[WATERLINK_HELLO_BYTES];
        p8 who[32];
        p8 e1[32], e2[32];
        p8 a_send[16], a_receive[16], b_send[16], b_receive[16];
        p32 index = 0;
        struct waterlink_admission admission;
        p8 source[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff, 10, 0, 0, 2};
        positive admitted = 0;

        identity_seeded(address_of alice, 1);
        identity_seeded(address_of bob, 2);
        identity_seeded(address_of eve, 3);
        random_seeded(e1, 32, 4);
        random_seeded(e2, 32, 5);
        random_seeded(hello, sizeof hello, 6);

        {
                p8 low_order[32] = {0};
                p8 one = 0;
                struct waterlink_noise bounded = {0};

                check("sec: an initiator refuses a low-order peer key",
                      !waterlink_initiate(address_of starting,
                                          address_of alice, low_order, null, e1,
                                          hello, first));
                check("sec: a wrapped Noise ciphertext length is refused before "
                      "the input or transcript is touched",
                      !waterlink_open_hash(address_of bounded, address_of one,
                                           positive_max, address_of one) &&
                              !bounded.nonce);
        }

        check("an initiation is made for a valid peer key",
              waterlink_initiate(address_of starting, address_of alice,
                                 bob.public, null, e1, hello, first));
        memory_copy(kept, first, sizeof kept);
        check("an initiation passes the responder's gate",
              waterlink_gate_passes(address_of bob, first, WATERLINK_DATAGRAM));
        check("and nobody else's", !waterlink_gate_passes(address_of eve, first,
                                                          WATERLINK_DATAGRAM));
        check("the responder learns who and what",
              waterlink_accept(address_of answering, address_of bob, null, first,
                               who, heard_hello) &&
                      !memory_compare(who, alice.public, 32) &&
                      !memory_compare(heard_hello, hello, sizeof hello));

        check("the responder makes an answer",
              waterlink_respond(address_of answering, e2, 0x11223344,
                                0x55667788, second));
        check("the answer passes the initiator's gate",
              waterlink_gate_passes(address_of alice, second,
                                    WATERLINK_DATAGRAM));
        check("and names the initiator's index",
              ((struct waterlink_datagram address_to)second)->receiver ==
                      0x11223344);
        starting_kept = starting;

        /* Every individual bit is authenticated by mac1, including header,
           ciphertext, tag and padding -- all but an initiation's mac2, which
           the admission asks about, and only under load. This is exhaustive
           over both full handshake datagrams rather than a sample of
           interesting offsets. */
        {
                p8 changed[WATERLINK_DATAGRAM];
                positive first_refused = 0;
                positive second_refused = 0;

                for (positive bit = 0; bit < WATERLINK_DATAGRAM * 8; bit++)
                {
                        memory_copy(changed, kept, sizeof changed);
                        changed[bit / 8] ^= (p8)(1u << (bit % 8));
                        first_refused +=
                                !waterlink_gate_passes(address_of bob, changed,
                                                       sizeof changed);

                        memory_copy(changed, second, sizeof changed);
                        changed[bit / 8] ^= (p8)(1u << (bit % 8));
                        second_refused +=
                                !waterlink_gate_passes(address_of alice, changed,
                                                       sizeof changed);
                }
                check("sec: every one-bit initiation mutation fails its gate "
                      "but in mac2",
                      first_refused == (WATERLINK_DATAGRAM - 16) * 8);
                check("sec: every one-bit answer mutation fails its gate",
                      second_refused == WATERLINK_DATAGRAM * 8);
        }

        /* mac1 is keyed only by a public identity and is deliberately not
           authentication. Give each encrypted-field mutation a freshly
           correct mac1 and require Noise's DH/transcript tag to reject it. */
        {
                p8 changed[WATERLINK_DATAGRAM];
                p8 gate[32];
                positive first_refused = 0;
                positive second_refused = 0;
                positive first_bits =
                        (WATERLINK_INITIATE_BYTES - 16) * 8;
                positive second_bits =
                        (WATERLINK_RESPOND_BYTES - 16) * 8;

                waterlink_gate_of(bob.public, gate);
                for (positive bit = 0; bit < first_bits; bit++)
                {
                        struct waterlink_noise candidate;

                        memory_copy(changed, kept, sizeof changed);
                        changed[16 + bit / 8] ^= (p8)(1u << (bit % 8));
                        waterlink_mac1(gate, changed,
                                       16 + WATERLINK_INITIATE_BYTES - 16,
                                       changed + 16 +
                                               WATERLINK_INITIATE_BYTES - 16);
                        first_refused +=
                                waterlink_gate_passes(address_of bob, changed,
                                                      sizeof changed) &&
                                !waterlink_accept(address_of candidate,
                                                  address_of bob, null, changed,
                                                  who, heard_hello);
                        crypto_forget(address_of candidate, sizeof candidate);
                }

                waterlink_gate_of(alice.public, gate);
                for (positive bit = 0; bit < second_bits; bit++)
                {
                        struct waterlink_noise candidate = starting_kept;

                        memory_copy(changed, second, sizeof changed);
                        changed[16 + bit / 8] ^= (p8)(1u << (bit % 8));
                        waterlink_mac1(gate, changed,
                                       16 + WATERLINK_RESPOND_BYTES - 16,
                                       changed + 16 +
                                               WATERLINK_RESPOND_BYTES - 16);
                        second_refused +=
                                waterlink_gate_passes(address_of alice, changed,
                                                      sizeof changed) &&
                                !waterlink_answered(address_of candidate,
                                                    address_of alice, changed,
                                                    address_of index);
                        crypto_forget(address_of candidate, sizeof candidate);
                }
                check("sec: Noise rejects every one-bit initiation mutation "
                      "behind a repaired gate",
                      first_refused == first_bits);
                check("sec: Noise rejects every one-bit answer mutation behind "
                      "a repaired gate",
                      second_refused == second_bits);
        }

        check("the initiator reads the answer",
              waterlink_answered(address_of starting, address_of alice, second,
                                 address_of index) &&
                      index == 0x55667788);

        waterlink_split(address_of starting, true, a_send, a_receive);
        waterlink_split(address_of answering, false, b_send, b_receive);
        check("the two ends agree on both keys",
              !memory_compare(a_send, b_receive, 16) &&
                      !memory_compare(a_receive, b_send, 16) &&
                      memory_compare(a_send, a_receive, 16));

        //      The gate: one bit anywhere before it, in it, or in the padding.
        {
                static const positive where[] = {0, 5, 16, 16 + 40,
                                                 16 + WATERLINK_INITIATE_BYTES - 1,
                                                 16 + WATERLINK_INITIATE_BYTES + 16,
                                                 WATERLINK_DATAGRAM - 1};
                positive refused = 0;

                for (positive at = 0; at < sizeof where / sizeof where[0]; at++)
                {
                        memory_copy(first, kept, sizeof first);
                        first[where[at]] ^= 0x20;
                        if (!waterlink_gate_passes(address_of bob, first,
                                                   WATERLINK_DATAGRAM))
                                refused++;
                }
                check("a wrong mac1, message or padding is refused at the gate",
                      refused == sizeof where / sizeof where[0]);
                check("and so is a datagram of the wrong length",
                      !waterlink_gate_passes(address_of bob, kept,
                                             WATERLINK_DATAGRAM - 16));
        }

        //      Past the gate, the sealed parts: made for bob, sent to eve with
        //      eve's own mac1 on it, it does not open.
        {
                struct waterlink_noise other;
                p8 gate[32];

                memory_copy(first, kept, sizeof first);
                waterlink_gate_of(eve.public, gate);
                waterlink_mac1(gate, first, 16 + WATERLINK_INITIATE_BYTES - 16,
                               first + 16 + WATERLINK_INITIATE_BYTES - 16);
                check("an initiation for another key does not open",
                      waterlink_gate_passes(address_of eve, first,
                                            WATERLINK_DATAGRAM) &&
                              !waterlink_accept(address_of other,
                                                address_of eve, null, first, who,
                                                heard_hello));

                memory_copy(first, kept, sizeof first);
                first[16 + 32 + 3] ^= 1;
                waterlink_gate_of(bob.public, gate);
                waterlink_mac1(gate, first, 16 + WATERLINK_INITIATE_BYTES - 16,
                               first + 16 + WATERLINK_INITIATE_BYTES - 16);
                check("a tampered static does not open",
                      !waterlink_accept(address_of other, address_of bob, null,
                                        first, who, heard_hello));
        }

        //      An answer the initiator did not ask for, or from the wrong
        //      responder, does not open.
        {
                struct waterlink_noise stranger;

                waterlink_initiate(address_of starting, address_of alice,
                                   bob.public, null, e1, hello, first);
                waterlink_accept(address_of answering, address_of bob, null, first,
                                 who, heard_hello);
                waterlink_initiate(address_of stranger, address_of alice,
                                   eve.public, null, e2, hello, first);
                waterlink_respond(address_of answering, e2, 1, 2, second);
                check("an answer to another initiation does not open",
                      !waterlink_answered(address_of stranger, address_of alice,
                                          second, address_of index));
        }

        //      A replayed initiation carries the stamp already seen.
        {
                p8 last[WATERLINK_STAMP_BYTES];
                p8 now[WATERLINK_STAMP_BYTES];

                waterlink_stamp(last, 1790000000, 5);
                waterlink_stamp(now, 1790000000, 5);
                check("the same stamp is not newer",
                      !waterlink_stamp_newer(now, last));
                waterlink_stamp(now, 1790000000, 6);
                check("a nanosecond later is", waterlink_stamp_newer(now, last));
                waterlink_stamp(now, 1789999999, 999999999);
                check("a second earlier is not",
                      !waterlink_stamp_newer(now, last));
        }

        static p8 unproven[WATERLINK_DATAGRAM];

        {
                struct waterlink_bucket bucket = {0};
                struct waterlink_bucket before;

                check("an admission bucket starts with one bounded burst",
                      waterlink_bucket_take(address_of bucket, 5, 5, 100));
                before = bucket;
                check("sec: an admission clock moving backward fails closed",
                      !waterlink_bucket_take(address_of bucket, 5, 5, 99) &&
                              !memory_compare(address_of bucket,
                                              address_of before, sizeof bucket));
                bucket.tokens = 0;
                check("sec: an enormous forward jump refills without arithmetic wrap",
                      waterlink_bucket_take(address_of bucket, 5, 5, ~0ull) &&
                              bucket.tokens == 4 && bucket.at == ~0ull);
        }

        memory_zero(address_of admission, sizeof admission);
        for (positive at = 0; at < 6; at++)
                admitted += waterlink_admit(address_of admission, unproven,
                                            source, 7, 1000000 + at * 1000) == 1;
        check("sec: an unproven source cannot spend the per-source bucket",
              admitted == 6 && !admission.source[0].at);
        source[15] = 3;
        check("and another source remains admitted below global load",
              waterlink_admit(address_of admission, unproven, source, 7,
                              1000000 + 50000) == 1);

        memory_zero(address_of admission, sizeof admission);
        admitted = 0;
        for (positive at = 0; at < 256; at++)
        {
                memory_zero(source, sizeof source);
                source[14] = (p8)(at >> 8);
                source[15] = (p8)at;
                admitted += waterlink_admit(address_of admission, unproven,
                                            source, 7, 2000000) == 1;
        }
        check("sec: rotating source addresses reach the curve only up to the "
              "load, past which they need mac2 and a secret to have it by",
              admitted == WATERLINK_ADMIT_LOADED &&
                      admission.all.tokens ==
                              WATERLINK_ADMIT_GLOBAL_BURST -
                                      WATERLINK_ADMIT_LOADED);
        check("sec: the load passes as its bucket refills",
              waterlink_admit(address_of admission, unproven, source, 7,
                              3000000) == 1);

        check("a session is refused at three minutes, or past its counter by "
              "more than a rekey's room",
              !waterlink_session_spent(179999999, 5) &&
                      waterlink_session_spent(180000000, 5) &&
                      !waterlink_session_spent(1, WATERLINK_REKEY_MESSAGES) &&
                      waterlink_session_spent(1, WATERLINK_REKEY_MESSAGES +
                                                         (1ull << 20)));
}

/*
        Groups: the derivation against hashlib's PBKDF2, the blinded tags,
        the pairing handshake, and the mDNS parser against hostile packets.
*/
static fn group_keys(struct waterlink_group_keys address_to keys,
                     string_address namespace, string_address secret)
{
        p8 derived[32];

        waterlink_group_derive(namespace, (p8 address_to)secret,
                               string_length(secret), 1000, derived);
        waterlink_group_keys_from(keys, derived, namespace);
}

/*
        PBKDF2 against RFC 6070 (HMAC-SHA1) and RFC 7914 (HMAC-SHA256, two
        blocks of output): the derivation waterlink's groups and WPA's
        pre-shared key are both made with.
*/
static p8 pbkdf2_is(positive algorithm, positive size, string_address password,
                      string_address salt, positive rounds, string_address hex)
{
        p8 out[64];
        p8 want[64];
        positive length = string_length(hex) / 2;

        for (positive at = 0; at < length; at++)
                want[at] = (p8)(digit_known(hex[2 * at], 16) << 4 |
                                digit_known(hex[2 * at + 1], 16));
        crypto_pbkdf2(algorithm, size, (p8 address_to)password,
                      string_length(password), (p8 address_to)salt,
                      string_length(salt), rounds, out, length);
        return !memory_compare(out, want, length);
}

static fn pbkdf2_vectors(void)
{
        check("PBKDF2-HMAC-SHA1, one round (RFC 6070)",
              pbkdf2_is(DIGEST_SHA1, 20, "password", "salt", 1,
                        "0c60c80f961f0e71f3a9b524af6012062fe037a6"));
        check("PBKDF2-HMAC-SHA1, 4096 rounds (RFC 6070)",
              pbkdf2_is(DIGEST_SHA1, 20, "password", "salt", 4096,
                        "4b007901b765489abead49d926f721d065a429c1"));
        check("PBKDF2-HMAC-SHA256, two blocks out (RFC 7914)",
              pbkdf2_is(DIGEST_SHA256, 32, "passwd", "salt", 1,
                        "55ac046e56e3089fec1691c22544b605f94185216dde0465e6"
                        "8b9d57c20dacbc49ca9cccf179b645991664b39d77ef317c71"
                        "b845b1e30bd509112041d3a19783"));
}

static fn group_derivation(void)
{
        static const p8 one[32] = {
                0xf3, 0x86, 0x42, 0xce, 0x4e, 0x30, 0x42, 0x0f, 0x7a, 0xfc, 0xe6,
                0xab, 0x60, 0xb7, 0x2d, 0x73, 0x9c, 0x7b, 0x66, 0x48, 0x7a, 0xdd,
                0xf8, 0x51, 0x41, 0x18, 0xb3, 0xe9, 0xd8, 0x29, 0x7f, 0x51};
        static const p8 thousand[32] = {
                0x7f, 0xf8, 0x8f, 0xc4, 0x56, 0x4d, 0x85, 0xb4, 0x0c, 0x71, 0x69,
                0xd4, 0x20, 0x1c, 0x7c, 0xd6, 0xe1, 0x8a, 0x2e, 0x55, 0x52, 0xa2,
                0xfc, 0x53, 0x8f, 0x57, 0x7c, 0xb5, 0x2d, 0x6d, 0x4a, 0xaa};
        static const p8 long_secret[32] = {
                0x53, 0xfa, 0x6c, 0xe9, 0x94, 0x59, 0xe7, 0x73, 0xd8, 0x58, 0x5c,
                0xdb, 0x95, 0x4c, 0x45, 0xdd, 0xd2, 0xa9, 0x91, 0x00, 0xe3, 0xe5,
                0x8d, 0x71, 0xba, 0x21, 0x7e, 0x2b, 0x8b, 0x2b, 0xec, 0xd2};
        static const char longer[] = "a much longer secret that exceeds sixty "
                                     "four bytes in length, for the pad rule";
        p8 out[32];

        waterlink_group_derive("office", (p8 address_to) "sesame", 6, 1, out);
        check("PBKDF2 of one round is hashlib's", !memory_compare(out, one, 32));
        waterlink_group_derive("office", (p8 address_to) "sesame", 6, 1000, out);
        check("PBKDF2 of a thousand rounds is hashlib's",
              !memory_compare(out, thousand, 32));
        waterlink_group_derive("lab-2", (p8 address_to)longer,
                               sizeof longer - 1, 1000, out);
        check("and a secret past the HMAC block is hashed first, as hashlib's",
              !memory_compare(out, long_secret, 32));
}

static fn group_identity(void)
{
        struct waterlink_group_keys office, again, other, elsewhere;

        group_keys(address_of office, "office", "sesame");
        group_keys(address_of again, "office", "sesame");
        group_keys(address_of other, "office", "sesamf");
        group_keys(address_of elsewhere, "office2", "sesame");
        check("members of a group derive one identity and one pre-shared key",
              !memory_compare(office.identity.public, again.identity.public,
                              32) &&
                      !memory_compare(office.psk, again.psk, 32));
        check("and another secret or another namespace derives another",
              memory_compare(office.identity.public, other.identity.public,
                             32) &&
                      memory_compare(office.identity.public,
                                     elsewhere.identity.public, 32) &&
                      memory_compare(office.psk, other.psk, 32));
        check("groups mark their peers, never zero, one mark a namespace",
              office.mark && office.mark == again.mark &&
                      office.mark != elsewhere.mark);
}

/*
        A greeting: the ordinary first message, to the group's key, with the
        group's pre-shared key. A member reads the sender's key and name out
        of it; a machine with another secret cannot pass its gate; the right
        identity with a wrong pre-shared key, or one changed bit, reads
        nothing.
*/
static fn group_pairing(void)
{
        struct waterlink_group_keys office, other;
        struct waterlink_identity a;
        struct waterlink_noise starting, answering;
        p8 greeting[WATERLINK_DATAGRAM], hello[WATERLINK_HELLO_BYTES];
        p8 heard[WATERLINK_HELLO_BYTES], who[32], e1[32], wrong[32];

        group_keys(address_of office, "office", "sesame");
        group_keys(address_of other, "office", "wrong");
        identity_seeded(address_of a, 21);
        random_seeded(e1, 32, 23);
        memory_zero(hello, sizeof hello);
        string_copy((string_address)hello + WATERLINK_STAMP_BYTES, "machine-a");

        check("a greeting is written",
              waterlink_initiate(address_of starting, address_of a,
                                 office.identity.public, office.psk, e1, hello,
                                 greeting));
        check("a machine with another secret cannot pass its gate",
              !waterlink_gate_passes(address_of other.identity, greeting,
                                     WATERLINK_DATAGRAM));
        check("a member passes it and reads the sender's key and name",
              waterlink_gate_passes(address_of office.identity, greeting,
                                    WATERLINK_DATAGRAM) &&
                      waterlink_accept(address_of answering,
                                       address_of office.identity, office.psk,
                                       greeting, who, heard) &&
                      !memory_compare(who, a.public, 32) &&
                      !memory_compare(heard, hello, sizeof hello));
        memory_copy(wrong, office.psk, 32);
        wrong[5] ^= 1;
        check("the group's key with another pre-shared key reads nothing",
              !waterlink_accept(address_of answering, address_of office.identity,
                                wrong, greeting, who, heard));
        check("and neither does an ordinary handshake to the group's key",
              !waterlink_accept(address_of answering, address_of office.identity,
                                null, greeting, who, heard));
        greeting[16 + 32 + 5] ^= 4;
        check("a changed greeting reads nothing",
              !waterlink_accept(address_of answering, address_of office.identity,
                                office.psk, greeting, who, heard));
}

/*
        The mDNS parser against what a hostile network sends. Every packet
        is placed so its last byte is the last of a readable page with an
        unmapped one after it: a read past the end is a fault here, on every
        machine. Built packets must read back as built; broken ones must be
        refused or read as nothing; and the shapes every DNS parser gets wrong
        once are made on purpose.
*/
static p8 address_to mdns_page;

static fn mdns_page_make(void)
{
        bipolar mapped = system_call_6(syscall(mmap), 0, 2 * 4096, 3, 0x22,
                                       (positive)(bipolar)-1, 0);

        mdns_page = (p8 address_to)mapped;
        system_call_3(syscall(mprotect), (positive)(mdns_page + 4096), 4096, 0);
}

static p8 mdns_read_edge(const p8 address_to packet, positive length,
                           struct waterlink_found address_to found)
{
        p8 address_to at = mdns_page + 4096 - length;

        memory_copy(at, packet, length);
        return waterlink_mdns_read(at, length, found);
}

/*
        What this machine puts on the link, spelled a second time.

        The reader above reads only the PTR and the SRV, so a wrong TTL on
        the TXT, a TXT whose one byte claims a 255-byte string, a header
        whose authority count is left as whatever the stack held, or a
        label one byte out are all invisible to it -- and to every peer
        check that reads with the same code. Here the packet is built again
        from RFC 6762/6763 by hand, byte for byte, for every combination of
        a live announcement and a goodbye, with and without an address, and
        with and without an echoed question, and the two must be equal.
*/
static positive mdns_expected(p8 address_to out, const p8 address_to instance,
                              const p8 address_to host, p16 port, p32 address,
                              p32 ttl, p16 id, const p8 address_to question,
                              positive question_length)
{
        static const p8 hex[] = "0123456789abcdef";
        positive at = 0;
        positive service, named, machine;

#define PUT16(v) (out[at] = (p8)((v) >> 8), out[at + 1] = (p8)(v), at += 2)
#define PUT32(v) (PUT16((p32)(v) >> 16), PUT16((p32)(v) & 0xffff))
        PUT16(id);
        PUT16(0x8400);
        PUT16(question ? 1 : 0);
        PUT16(address ? 4 : 3);
        PUT16(0);
        PUT16(0);
        for (positive i = 0; i < question_length; i++)
                out[at++] = question[i];
        service = at;
        for (positive i = 0; i < WATERLINK_SERVICE_BYTES; i++)
                out[at++] = waterlink_service_name[i];
        PUT16(12); PUT16(1); PUT32(ttl ? 4500 : 0); PUT16(26);
        named = at;
        out[at++] = 23; out[at++] = 'w'; out[at++] = 'l'; out[at++] = '-';
        for (positive i = 0; i < 10; i++)
        {
                out[at++] = hex[instance[i] >> 4];
                out[at++] = hex[instance[i] & 15];
        }
        PUT16(0xc000 | service);
        PUT16(0xc000 | named);
        PUT16(33); PUT16(0x8001); PUT32(ttl); PUT16(29);
        PUT16(0); PUT16(0); PUT16(port);
        machine = at;
        out[at++] = 15; out[at++] = 'w'; out[at++] = 'l'; out[at++] = '-';
        for (positive i = 0; i < 6; i++)
        {
                out[at++] = hex[host[i] >> 4];
                out[at++] = hex[host[i] & 15];
        }
        out[at++] = 5;
        for (positive i = 0; i < 5; i++)
                out[at++] = (p8) "local"[i];
        out[at++] = 0;
        PUT16(0xc000 | named);
        PUT16(16); PUT16(0x8001); PUT32(ttl ? 4500 : 0); PUT16(1);
        out[at++] = 0;
        if (address)
        {
                PUT16(0xc000 | machine);
                PUT16(1); PUT16(0x8001); PUT32(ttl); PUT16(4);
                PUT32(address);
        }
#undef PUT32
#undef PUT16
        return at;
}

/*
        Discovery's settings, read from a directory of the check's own as
        they are read from /root: each lookup on when nothing is kept, one
        switch that lookup alone, discovery off all of them, the lookups
        under off sending nothing (discovery_sent counts every datagram and
        connection discovery.c opens), and a word `link discovery` kept,
        before the switches were the network's, read once and moved.
*/
static p8 discovery_kept_word(string_address directory, string_address leaf,
                              string_address word)
{
        p8 path[DISCOVERY_PATH_ROOM];
        bipolar handle;
        p8 whole;

        discovery_kept_path(directory, leaf, path);
        (void)system_remove_at(AT_FDCWD, (string_address)path, 0);
        if (!word)
                return true;
        handle = system_open_output_at(AT_FDCWD, (string_address)path, true, 0600);
        if (handle < 0)
                return false;
        whole = system_write_all((positive)handle, word, string_length(word)) ==
                string_length(word);
        system_close((b32)handle);
        return whole;
}

static fn discovery_kept_clear(string_address directory)
{
        for (positive at = 0; at < array_count(discovery_switches); at++)
        {
                (void)discovery_kept_word(directory, discovery_switches[at].leaf, null);
                (void)discovery_kept_word(directory, discovery_switches[at].was, null);
        }
}

static fn discovery_setting_rows(void)
{
        p8 directory[48] = "/tmp/moonwater-discovery-";
        discovery_settings none, off, stun_off, moved, kept;
        discovery_found found;
        discovery_here here;
        positive before = discovery_sent;
        p8 path[DISCOVERY_PATH_ROOM];
        p8 word[8];
        p8 made, went;

        directory[25 + positive_into(directory + 25, (positive)system_call(syscall(getpid)))] = end;
        made = system_make_directory_at(AT_FDCWD, (string_address)directory, 0700) >= 0;
        discovery_kept_clear((string_address)directory);
        discovery_settings_read_at((string_address)directory, address_of none);
        made &= discovery_kept_word((string_address)directory, "network.discovery", "off\n");
        discovery_settings_read_at((string_address)directory, address_of off);
        memory_zero(address_of here, sizeof here);
        discovery_look(address_of off, address_of found, address_of here);
        made &= discovery_kept_word((string_address)directory, "network.discovery", "on\n");
        made &= discovery_kept_word((string_address)directory, "network.discovery.stun", "off\n");
        discovery_settings_read_at((string_address)directory, address_of stun_off);
        check("discovery: every lookup is on when nothing is kept",
              made && none.ask == (DISCOVERY_ASK_STUN | DISCOVERY_ASK_HTTPS) &&
                  none.switched[0] && none.switched[1] && none.switched[2]);
        check("sec: discovery off is every lookup off, and the lookups then send "
              "nothing and say they asked nothing",
              made && off.ask == 0 && !off.switched[0] && off.switched[1] &&
                  discovery_sent == before && found.asked &&
                  found.udp == DISCOVERY_UDP_NOT_ASKED &&
                  found.https == DISCOVERY_HTTPS_NOT_ASKED && !found.answered);
        check("discovery: one lookup's switch is that lookup alone",
              made && stun_off.ask == DISCOVERY_ASK_HTTPS && stun_off.switched[0] &&
                  !stun_off.switched[1]);

        //      What `link discovery https off` kept is the network's word
        //      now: read once, moved, and never put over a word kept since.
        discovery_kept_clear((string_address)directory);
        made &= discovery_kept_word((string_address)directory, "link.discovery.https", "off\n");
        discovery_settings_read_at((string_address)directory, address_of moved);
        discovery_kept_path((string_address)directory, "network.discovery.https", path);
        went = host_read_word((string_address)path, word, sizeof word) == 3 &&
               string_equals((string_address)word, "off");
        discovery_kept_path((string_address)directory, "link.discovery.https", path);
        went &= host_read_word((string_address)path, word, sizeof word) < 0;
        made &= discovery_kept_word((string_address)directory, "link.discovery.https", "on\n");
        discovery_settings_read_at((string_address)directory, address_of kept);
        check("discovery: a switch link discovery kept is read once and moved to "
              "the network's name, and never over a word kept since",
              made && went && moved.ask == DISCOVERY_ASK_STUN &&
                  kept.ask == DISCOVERY_ASK_STUN);
        discovery_kept_clear((string_address)directory);
        (void)system_remove_at(AT_FDCWD, (string_address)directory, 0x200 /* AT_REMOVEDIR */);
}

/*
        discovery.c's pure parts: STUN against RFC 5769's vectors (the test
        vectors RFC 8489 names), the request it sends, the trace's line, the
        NAT's reading, an address's scope and text and an interface's kind.
*/
static fn discovery_vectors(void)
{
        //      RFC 5769 2.2 and 2.3: the IPv4 and IPv6 responses, whole.
        static const p8 response4[80] = {
            0x01, 0x01, 0x00, 0x3c, 0x21, 0x12, 0xa4, 0x42, 0xb7, 0xe7, 0xa7, 0x01,
            0xbc, 0x34, 0xd6, 0x86, 0xfa, 0x87, 0xdf, 0xae, 0x80, 0x22, 0x00, 0x0b,
            0x74, 0x65, 0x73, 0x74, 0x20, 0x76, 0x65, 0x63, 0x74, 0x6f, 0x72, 0x20,
            0x00, 0x20, 0x00, 0x08, 0x00, 0x01, 0xa1, 0x47, 0xe1, 0x12, 0xa6, 0x43,
            0x00, 0x08, 0x00, 0x14, 0x2b, 0x91, 0xf5, 0x99, 0xfd, 0x9e, 0x90, 0xc3,
            0x8c, 0x74, 0x89, 0xf9, 0x2a, 0xf9, 0xba, 0x53, 0xf0, 0x6b, 0xe7, 0xd7,
            0x80, 0x28, 0x00, 0x04, 0xc0, 0x7d, 0x4c, 0x96};
        static const p8 response6[92] = {
            0x01, 0x01, 0x00, 0x48, 0x21, 0x12, 0xa4, 0x42, 0xb7, 0xe7, 0xa7, 0x01,
            0xbc, 0x34, 0xd6, 0x86, 0xfa, 0x87, 0xdf, 0xae, 0x80, 0x22, 0x00, 0x0b,
            0x74, 0x65, 0x73, 0x74, 0x20, 0x76, 0x65, 0x63, 0x74, 0x6f, 0x72, 0x20,
            0x00, 0x20, 0x00, 0x14, 0x00, 0x02, 0xa1, 0x47, 0x01, 0x13, 0xa9, 0xfa,
            0xa5, 0xd3, 0xf1, 0x79, 0xbc, 0x25, 0xf4, 0xb5, 0xbe, 0xd2, 0xb9, 0xd9,
            0x00, 0x08, 0x00, 0x14, 0xa3, 0x82, 0x95, 0x4e, 0x4b, 0xe6, 0x7b, 0xf1,
            0x17, 0x84, 0xc9, 0x7c, 0x82, 0x92, 0xc2, 0x75, 0xbf, 0xe3, 0xed, 0x41,
            0x80, 0x28, 0x00, 0x04, 0xc8, 0xfb, 0x0b, 0x4c};
        static const p8 transaction[12] = {0xb7, 0xe7, 0xa7, 0x01, 0xbc, 0x34,
                                           0xd6, 0x86, 0xfa, 0x87, 0xdf, 0xae};
        static const p8 other[12] = {0xb7, 0xe7, 0xa7, 0x01, 0xbc, 0x34,
                                     0xd6, 0x86, 0xfa, 0x87, 0xdf, 0xaf};
        static const struct
        {
                string_address name;
                p16 type;
                p8 wireless;
                string_address driver;
                positive kind;
        } kinds[] = {
            {"wg0", 65534, false, "wireguard", DISCOVERY_KIND_VPN},
            {"tailscale0", 65534, false, "tun", DISCOVERY_KIND_VPN},
            {"ztks57abcd", 1, false, "tun", DISCOVERY_KIND_VPN},
            {"ztks57abcd", 1, false, null, DISCOVERY_KIND_VPN},
            {"wlan0", 1, true, null, DISCOVERY_KIND_WIFI},
            {"wlp2s0", 1, false, null, DISCOVERY_KIND_WIFI},
            {"radio0", 1, true, null, DISCOVERY_KIND_WIFI},
            {"enp3s0", 1, false, null, DISCOVERY_KIND_ETHERNET},
            {"eth0", 1, false, "veth", DISCOVERY_KIND_VIRTUAL},
            {"docker0", 1, false, "bridge", DISCOVERY_KIND_VIRTUAL},
            {"lo", 772, false, null, DISCOVERY_KIND_LOOPBACK},
            {"can0", 280, false, null, DISCOVERY_KIND_OTHER},
        };
        static const struct
        {
                string_address text;
                positive scope;
        } scopes[] = {
            {"10.1.2.3", DISCOVERY_SCOPE_PRIVATE},
            {"172.31.0.1", DISCOVERY_SCOPE_PRIVATE},
            {"172.32.0.1", DISCOVERY_SCOPE_GLOBAL},
            {"192.168.7.1", DISCOVERY_SCOPE_PRIVATE},
            {"100.64.0.1", DISCOVERY_SCOPE_SHARED},
            {"100.128.0.1", DISCOVERY_SCOPE_GLOBAL},
            {"169.254.9.9", DISCOVERY_SCOPE_LINK},
            {"127.0.0.1", DISCOVERY_SCOPE_LOOPBACK},
            {"203.0.113.9", DISCOVERY_SCOPE_GLOBAL},
            {"fe80::1", DISCOVERY_SCOPE_LINK},
            {"fd12:3456::1", DISCOVERY_SCOPE_UNIQUE},
            {"2001:db8::1", DISCOVERY_SCOPE_GLOBAL},
            {"::1", DISCOVERY_SCOPE_LOOPBACK},
            {"ff02::1", DISCOVERY_SCOPE_OTHER},
        };
        static const string_address texts[] = {
            "::1", "2001:db8::1", "fe80::1:0:0:1", "1:0:0:2::3", "1::",
            "2001:db8:1234:5678:11:2233:4455:6677", "::", "192.0.2.1"};
        static const string_address refused[] = {
            "999.1.1.1", "1.2.3", "1.2.3.4.5", "1..2.3", "1:2:3:4:5:6:7:8:9",
            "1::2::3", ":1::2", "1:2:3:4:5:6:7:", "12345::1", "g::1", "",
            "1:2:3:4:5:6:7:8::"};
        p8 copy[92];
        p8 address[16];
        p8 text[48];
        p8 request[DISCOVERY_STUN_REQUEST];
        p16 port = 0;
        positive wrong = 0;
        p8 read;

        read = discovery_stun_read(response4, sizeof response4, transaction,
                                    address, address_of port);
        discovery_address_text(address, text);
        check("discovery: RFC 5769's IPv4 response is 192.0.2.1 port 32853, "
              "its FINGERPRINT checked",
              read && port == 32853 && string_equals((string_address)text, "192.0.2.1") &&
                  discovery_stun_fingerprint(response4, 72) == 0xc07d4c96u);
        read = discovery_stun_read(response6, sizeof response6, transaction,
                                    address, address_of port);
        discovery_address_text(address, text);
        check("discovery: RFC 5769's IPv6 response is "
              "2001:db8:1234:5678:11:2233:4455:6677 port 32853",
              read && port == 32853 &&
                  string_equals((string_address)text,
                                "2001:db8:1234:5678:11:2233:4455:6677") &&
                  discovery_stun_fingerprint(response6, 84) == 0xc8fb0b4cu);

        //      Every byte the FINGERPRINT covers, and the fingerprint itself,
        //      changed is refused. Its attribute's type changed is an unknown
        //      optional attribute (0x8000 and up), which RFC 8489 says to
        //      ignore, and an answer with no fingerprint is still taken: a
        //      server need not send one, and the transaction's 96 random bits
        //      are what keep a stranger's answer out.
        for (positive at = 0; at < sizeof response4; at++)
        {
                if (at >= 72 && at < 74)
                        continue;
                memory_copy(copy, response4, sizeof response4);
                copy[at] ^= 0x01;
                wrong += discovery_stun_read(copy, sizeof response4, transaction,
                                              address, address_of port);
        }
        check("sec: discovery: a STUN answer with any byte its FINGERPRINT "
              "covers changed is refused",
              wrong == 0);
        wrong = 0;
        for (positive length = 0; length < sizeof response4; length++)
                wrong += discovery_stun_read(response4, length, transaction,
                                              address, address_of port);
        wrong += discovery_stun_read(response4, sizeof response4, other, address,
                                      address_of port);
        check("sec: discovery: a cut answer, or another transaction's, is refused",
              wrong == 0);

        (void)discovery_stun_request(request, transaction);
        check("discovery: a Binding request is its header and a FINGERPRINT, "
              "28 bytes",
              request[0] == 0 && request[1] == 1 && request[2] == 0 && request[3] == 8 &&
                  discovery_get32(request + 4) == DISCOVERY_STUN_MAGIC &&
                  !memory_compare(request + 8, transaction, 12) &&
                  discovery_get32(request + 20) == 0x80280004u &&
                  discovery_get32(request + 24) ==
                      discovery_stun_fingerprint(request, 20));

        wrong = !discovery_trace_address(
                    (p8 address_to) "fl=1f1\nh=cloudflare.com\nip=203.0.113.9\nts=1\n",
                    44, address) ||
                (discovery_address_text(address, text),
                 !string_equals((string_address)text, "203.0.113.9"));
        wrong += !discovery_trace_address((p8 address_to) "ip=2001:db8::5\r\nts=2\n",
                                           21, address) ||
                 (discovery_address_text(address, text),
                  !string_equals((string_address)text, "2001:db8::5"));
        wrong += discovery_trace_address((p8 address_to) "h=x\nts=1\n", 9, address);
        wrong += discovery_trace_address((p8 address_to) "ip=999.0.0.1\n", 13, address);
        wrong += discovery_trace_address((p8 address_to) "xip=1.2.3.4\n", 12, address);
        wrong += discovery_trace_address((p8 address_to) "ip=\n", 4, address);
        check("discovery: the trace's ip= line is the address, and nothing "
              "else is", wrong == 0);

        {
                p8 a[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff, 198, 51, 100, 7};
                p8 b[16];

                memory_copy(b, a, 16);
                b[15] = 8;
                check("discovery: the NAT is told by what two servers saw",
                      discovery_nat_classify(0, a, 1, a, 1, false) == DISCOVERY_NAT_UNKNOWN &&
                          discovery_nat_classify(1, a, 1, a, 1, false) == DISCOVERY_NAT_ONE &&
                          discovery_nat_classify(2, a, 1, a, 1, false) == DISCOVERY_NAT_ONE &&
                          discovery_nat_classify(3, a, 1, a, 1, true) == DISCOVERY_NAT_NONE &&
                          discovery_nat_classify(3, a, 4000, a, 4000, false) ==
                              DISCOVERY_NAT_INDEPENDENT &&
                          discovery_nat_classify(3, a, 4000, a, 4001, false) ==
                              DISCOVERY_NAT_DEPENDENT &&
                          discovery_nat_classify(3, a, 4000, b, 4000, false) ==
                              DISCOVERY_NAT_DEPENDENT);
                //      The caller's first mapping is the second server's when
                //      only it answered, and its own is no NAT all the same.
                check("discovery: an address of this machine's own seen by "
                      "the second server alone is no NAT",
                      discovery_nat_classify(2, a, 1, a, 1, true) == DISCOVERY_NAT_NONE);
        }

        {
                discovery_found before, found;

                memory_zero(address_of before, sizeof before);
                before.answered = 1;
                before.nat = DISCOVERY_NAT_ONE;
                before.mapped[0][15] = 7;
                before.mapped_port[0] = 4000;
                before.stun_when = 100;
                before.https = DISCOVERY_HTTPS_FAILED;
                before.trace[15] = 9;
                before.https_when = 50;
                memory_zero(address_of found, sizeof found);
                found.udp = DISCOVERY_UDP_BLOCKED;
                found.https = DISCOVERY_HTTPS_FAILED;
                discovery_found_keep(address_of found, address_of before);
                check("discovery: a gather that learned nothing keeps the last "
                      "STUN answer whole and a trace from gathers before",
                      found.answered == 1 && found.nat == DISCOVERY_NAT_ONE &&
                          found.mapped[0][15] == 7 && found.mapped_port[0] == 4000 &&
                          found.stun_when == 100 && found.udp == DISCOVERY_UDP_BLOCKED &&
                          found.trace[15] == 9 && found.https_when == 50);
        }

        wrong = 0;
        for (positive at = 0; at < array_count(kinds); at++)
                wrong += discovery_interface_kind(kinds[at].name, kinds[at].type,
                                                   kinds[at].wireless,
                                                   kinds[at].driver) != kinds[at].kind;
        check("discovery: an interface's kind, by its driver, its name and its "
              "hardware", wrong == 0);

        wrong = 0;
        for (positive at = 0; at < array_count(scopes); at++)
                wrong += !discovery_parse_address((p8 address_to)scopes[at].text,
                                                   string_length(scopes[at].text),
                                                   address) ||
                         discovery_address_scope(address) != scopes[at].scope;
        check("discovery: an address's scope", wrong == 0);

        wrong = 0;
        for (positive at = 0; at < array_count(texts); at++)
                wrong += !discovery_parse_address((p8 address_to)texts[at],
                                                   string_length(texts[at]), address) ||
                         (discovery_address_text(address, text),
                          !string_equals((string_address)text, texts[at]));
        for (positive at = 0; at < array_count(refused); at++)
                wrong += discovery_parse_address((p8 address_to)refused[at],
                                                  string_length(refused[at]), address);
        check("discovery: an address read and written back is itself (RFC 5952), "
              "and a malformed one is refused", wrong == 0);
}

static fn mdns_wire_exact(void)
{
        p8 instance[10];
        p8 host[6] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xab};
        p8 packet[WATERLINK_MDNS_MAX];
        p8 expected[WATERLINK_MDNS_MAX];
        p8 question[WATERLINK_SERVICE_BYTES + 4];
        positive question_length;
        positive wrong = 0;
        positive rows = 0;
        struct waterlink_found found;

        random_seeded(instance, 10, 7);
        question_length = waterlink_mdns_query(packet, sizeof packet) - 12;
        memory_copy(question, packet + 12, question_length);
        /* Invalid writer contracts are refused before touching the output,
           including lengths that would wrap an additive capacity check. */
        const positive invalid_lengths[] = {
            sizeof(packet), positive_max, positive_max - 171,
        };
        for (positive i = 0; i < array_count(invalid_lengths); i++)
        {
                memory_fill(packet, 0xa5, sizeof(packet));
                check("mDNS writer refuses oversized question before writing",
                      !waterlink_mdns_announce(packet, sizeof(packet), instance,
                          host, 22348, 0, 120, 0, question, invalid_lengths[i]) &&
                      memory_span_byte(packet, 0xa5, sizeof(packet)) == sizeof(packet));
        }
        memory_fill(packet, 0xa5, sizeof(packet));
        check("mDNS writer refuses missing question bytes",
              !waterlink_mdns_announce(packet, sizeof(packet), instance, host,
                  22348, 0, 120, 0, null, question_length) &&
              memory_span_byte(packet, 0xa5, sizeof(packet)) == sizeof(packet));
        check("mDNS writer refuses empty declared question",
              !waterlink_mdns_announce(packet, sizeof(packet), instance, host,
                  22348, 0, 120, 0, question, 0) &&
              memory_span_byte(packet, 0xa5, sizeof(packet)) == sizeof(packet));
        check("mDNS writer refuses undersized fixed reserve",
              !waterlink_mdns_announce(packet, 171, instance, host, 22348,
                  0, 120, 0, null, 0) &&
              memory_span_byte(packet, 0xa5, sizeof(packet)) == sizeof(packet));
        check("mDNS writer accepts its fixed-reserve boundary",
              waterlink_mdns_announce(packet, 172, instance, host, 22348,
                  0, 120, 0, null, 0) > 0);
        check("mDNS writer accepts its question-reserve boundary",
              waterlink_mdns_announce(packet, 172 + question_length, instance,
                  host, 22348, 0, 120, 0, question, question_length) > 0);
        for (positive shape = 0; shape < 8; shape++)
        {
                p32 ttl = shape & 1 ? 0 : 120;
                p32 address = shape & 2 ? 0 : 0x0a000105;
                p8 asked = shape & 4;
                positive want, got;

                //      Poisoned first, so a field the builder leaves alone
                //      shows as the poison rather than as a lucky zero.
                memory_fill(packet, 0xa5, sizeof packet);
                got = waterlink_mdns_announce(packet, sizeof packet, instance,
                                              host, 22348, address, ttl,
                                              asked ? 0x1234 : 0,
                                              asked ? question : null,
                                              asked ? question_length : 0);
                want = mdns_expected(expected, instance, host, 22348, address,
                                     ttl, asked ? 0x1234 : 0,
                                     asked ? question : null,
                                     asked ? question_length : 0);
                rows++;
                wrong += got != want || memory_compare(packet, expected, want);
        }
        check("every announcement and goodbye is byte for byte the DNS-SD records it means",
              rows == 8 && !wrong);

        //      The question a one-shot asker sent is kept whole, to be echoed:
        //      its name, its type and its class, and not a byte more.
        question_length = waterlink_mdns_query(packet, sizeof packet);
        check("a question is kept exactly as it was asked",
              waterlink_mdns_read(packet, question_length, address_of found) &&
                  found.asked &&
                  found.question_length == question_length - 12 &&
                  !memory_compare(found.question, packet + 12,
                                  question_length - 12));

        //      The smallest question there is, the root name: exactly the five
        //      bytes the count bound allows for one, which it must not refuse.
        {
                static const p8 smallest[] = {0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0,
                                              0, 0, 12, 0, 1};

                check("a question of the root name, at the count bound exactly, is read",
                      waterlink_mdns_read(smallest, sizeof smallest,
                                          address_of found) &&
                          !found.asked);
        }

        //      An SRV whose target is the root, framed exactly (RFC 2782:
        //      the service is decidedly not available), beside the same
        //      record with a real one-label target.
        for (positive root = 0; root < 2; root++)
        {
                positive at = 0;
                positive named;

                memory_fill(packet, 0, sizeof packet);
                packet[2] = 0x84;
                packet[7] = 2;
                at = 12;
                memory_copy(packet + at, waterlink_service_name,
                            WATERLINK_SERVICE_BYTES);
                at += WATERLINK_SERVICE_BYTES;
                network_store_16(packet + at, 12);
                network_store_16(packet + at + 2, 1);
                network_store_32(packet + at + 4, 120);
                network_store_16(packet + at + 8, 6);
                at += 10;
                named = at;
                packet[at++] = 3;
                memory_copy(packet + at, "abc", 3);
                at += 3;
                network_store_16(packet + at, 0xc00c);
                at += 2;
                network_store_16(packet + at, 0xc000 | named);
                network_store_16(packet + at + 2, 33);
                network_store_16(packet + at + 4, 0x8001);
                network_store_32(packet + at + 6, 120);
                network_store_16(packet + at + 10, root ? 7 : 9);
                at += 12;
                network_store_16(packet + at + 4, 22348);
                at += 6;
                if (!root)
                {
                        packet[at++] = 1;
                        packet[at++] = 'h';
                }
                packet[at++] = 0;
                if (root)
                        check("an SRV to the root target is read but is no endpoint",
                              waterlink_mdns_read(packet, at, address_of found) &&
                                  !found.count);
                else
                        check("the same SRV to a real target is an endpoint",
                              waterlink_mdns_read(packet, at, address_of found) &&
                                  found.count == 1 &&
                                  found.instance[0].port == 22348);
        }
}

static fn mdns_parse(void)
{
        p8 instance[10];
        struct waterlink_found found;
        p8 packet[WATERLINK_MDNS_MAX];
        p8 broken[WATERLINK_MDNS_MAX];
        p8 host[6] = {1, 2, 3, 4, 5, 6};
        positive length;
        positive survived = 0;
        positive tries = 0;

        mdns_page_make();
        random_seeded(instance, 10, 1);

        length = waterlink_mdns_announce(packet, sizeof packet, instance, host,
                                         22348, 0x0a000001, 4500, 0, null, 0);
        check("an announcement reads back: one instance, on its port",
              length && mdns_read_edge(packet, length, address_of found) &&
                      found.response && found.count == 1 &&
                      found.instance[0].has_port &&
                      found.instance[0].port == 22348);
        memory_copy(broken, packet, length);
        network_store_16(broken + 12 + WATERLINK_SERVICE_BYTES, 16);
        check("an orphan SRV without the service PTR is not discovery",
              mdns_read_edge(broken, length, address_of found) &&
                  found.response && !found.count);
        {
                positive target = 0;

                for (positive at = 0; at + 4 < length; at++)
                        if (packet[at] == 15 && packet[at + 1] == 'w' &&
                            packet[at + 2] == 'l' && packet[at + 3] == '-')
                                target = at;
                check("the production announcement exposes one SRV target",
                      target != 0);
                if (target)
                {
                        memory_copy(broken, packet, length);
                        broken[target] = 0;
                        check("an unavailable root SRV target with trailing bytes is not live",
                              mdns_read_edge(broken, length, address_of found) &&
                                  !found.count);
                        memory_copy(broken, packet, length);
                        broken[target - 2] = broken[target - 1] = 0;
                        check("an SRV destination on port zero is not live",
                              mdns_read_edge(broken, length, address_of found) &&
                                  !found.count);
                }
        }
        length = waterlink_mdns_announce(packet, sizeof packet, instance, host,
                                         22348, 0x0a000001, 0, 0, null, 0);
        check("an mDNS goodbye cannot be rediscovered as a live endpoint",
              length && mdns_read_edge(packet, length, address_of found) &&
                  found.response && !found.count);

        //      Letter case: the service's name reads the same in capitals.
        {
                positive names = 0;

                length = waterlink_mdns_announce(packet, sizeof packet, instance,
                                                 host, 22348, 0x0a000001, 4500,
                                                 0, null, 0);
                memory_copy(broken, packet, length);
                for (positive at = 0; at + WATERLINK_SERVICE_BYTES <= length; at++)
                        if (!memory_compare(broken + at, waterlink_service_name,
                                            WATERLINK_SERVICE_BYTES))
                        {
                                names++;
                                for (positive i = at; i < at + WATERLINK_SERVICE_BYTES; i++)
                                        if (broken[i] >= 'a' && broken[i] <= 'z')
                                                broken[i] -= 32;
                        }
                check("the service's name reads the same in capitals",
                      names &&
                              mdns_read_edge(broken, length, address_of found) &&
                              found.count == 1 &&
                              found.instance[0].port == 22348);
        }

        length = waterlink_mdns_query(packet, sizeof packet);
        check("a question for the service is one",
              mdns_read_edge(packet, length, address_of found) && found.asked &&
                      !found.response && found.question_length);
        packet[length - 1] = 3; // CHAOS, not Internet class
        check("a non-Internet question cannot make this responder an oracle",
              mdns_read_edge(packet, length, address_of found) && !found.asked);
        packet[length - 2] = 0x80; // QU is the one legal high class bit
        packet[length - 1] = 1;
        check("an Internet-class question may request a unicast response",
              mdns_read_edge(packet, length, address_of found) && found.asked);
        packet[length - 2] = 0;
        packet[3] = 3;
        check("a query cannot smuggle a response error code into the responder",
              !mdns_read_edge(packet, length, address_of found));
        packet[3] = 0;

        //      The shapes, by hand: each must be refused.
        {
                static const p8 loop[] = {0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0,
                                          0xc0, 12, 0, 12, 0, 1};
                static const p8 ahead[] = {0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0,
                                           0xc0, 16, 0, 12, 0, 1, 0};
                static const p8 kind40[] = {0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0,
                                            0x41, 'a', 0, 0, 12, 0, 1};
                static const p8 kind80[] = {0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0,
                                            0x81, 'a', 0, 0, 12, 0, 1};
                static const p8 counts[] = {0, 0, 0x84, 0, 0xff, 0xff, 0xff,
                                            0xff, 0, 0, 0, 0};
                static const p8 short_head[] = {0, 0, 0x84, 0, 0};
                p8 long_label[12 + 1 + 64 + 1 + 4];
                p8 long_name[12 + 5 * 64 + 1 + 4];
                p8 rdlength[64];
                positive refused = 0;
                positive at;

                refused += !mdns_read_edge(loop, sizeof loop, address_of found);
                refused += !mdns_read_edge(ahead, sizeof ahead, address_of found);
                refused += !mdns_read_edge(kind40, sizeof kind40,
                                           address_of found);
                refused += !mdns_read_edge(kind80, sizeof kind80,
                                           address_of found);
                refused += !mdns_read_edge(counts, sizeof counts,
                                           address_of found);
                refused += !mdns_read_edge(short_head, sizeof short_head,
                                           address_of found);
                refused += !mdns_read_edge(packet, 0, address_of found);

                //      A label of 64 bytes: the length byte is 0x40, a type.
                memory_zero(long_label, sizeof long_label);
                long_label[5] = 1;
                long_label[12] = 64;
                memory_fill(long_label + 13, 'x', 64);
                long_label[12 + 1 + 64 + 2] = 12;
                refused += !mdns_read_edge(long_label, sizeof long_label,
                                           address_of found);

                //      A name of five 63-byte labels: past 255.
                memory_zero(long_name, sizeof long_name);
                long_name[5] = 1;
                at = 12;
                for (positive label = 0; label < 5; label++)
                {
                        long_name[at++] = 63;
                        memory_fill(long_name + at, 'y', 63);
                        at += 63;
                }
                long_name[at++] = 0;
                long_name[at + 1] = 12;
                refused += !mdns_read_edge(long_name, at + 4, address_of found);

                //      A record whose rdata runs past the packet.
                memory_zero(rdlength, sizeof rdlength);
                rdlength[2] = 0x84;
                rdlength[7] = 1;
                memory_copy(rdlength + 12, waterlink_service_name,
                            WATERLINK_SERVICE_BYTES);
                at = 12 + WATERLINK_SERVICE_BYTES;
                rdlength[at + 1] = 12;
                rdlength[at + 3] = 1;
                rdlength[at + 8] = 0x01; // rdlength 256
                refused += !mdns_read_edge(rdlength, at + 10 + 4,
                                           address_of found);

                check("pointer loops and forward pointers, label types, long "
                      "labels and names, impossible counts, short headers and "
                      "rdata past the end are all refused",
                      refused == 10);
        }

        //      Generated: every cut of a real announcement, and a thousand
        //      random edits of it, each read at the page's edge. Surviving is
        //      the claim; what they read must never be more than was there.
        length = waterlink_mdns_announce(packet, sizeof packet, instance, host,
                                         22348, 0x0a000001, 4500, 0, null, 0);
        for (positive cut = 0; cut < length; cut++)
        {
                tries++;
                survived += !mdns_read_edge(packet, cut, address_of found) &&
                            !found.count && !found.asked && !found.id;
        }
        traffic_state = 0x51a7e5ull;
        for (positive round = 0; round < 20000; round++)
        {
                positive edits = 1 + traffic_next() % 6;
                positive size = length;

                memory_copy(broken, packet, length);
                for (positive e = 0; e < edits; e++)
                {
                        p64 roll = traffic_next();
                        positive where = (positive)(roll >> 8) % size;

                        switch (roll % 5)
                        {
                        case 0: broken[where] ^= (p8)(1u << (roll >> 40) % 8); break;
                        case 1: broken[where] = (p8)(roll >> 16); break;
                        //      A pointer, anywhere, to anywhere.
                        case 2:
                                if (where + 1 < size)
                                {
                                        broken[where] = (p8)(0xc0 | ((roll >> 24) & 0x3f));
                                        broken[where + 1] = (p8)(roll >> 32);
                                }
                                break;
                        case 3: size = where ? where : 1; break;
                        default: broken[where] = (p8)((roll >> 20) % 70); break;
                        }
                }
                tries++;
                survived += mdns_read_edge(broken, size, address_of found)
                                ? found.count <= WATERLINK_FOUND_MAX
                                : !found.count && !found.asked && !found.id;
        }
        check("every cut and 20,000 random edits of an announcement are read "
              "at a page's edge without reading past it, and every one "
              "refused says nothing was found",
              survived == tries);
}

/*
        For the harness: seeded inputs and what both messages and both keys
        came out as, one line a seed, in hex.
*/
static fn hex_out(p8 address_to bytes, positive length)
{
        p8 text[2 * 256 + 2];
        positive used = memory_into_hex(text, bytes, length < 256 ? length : 256);

        text[used++] = ' ';
        text[used] = 0;
        string_format(log, "%s", (string_address)text);
}

static b32 noise_vectors(positive count)
{
        for (positive seed = 0; seed < count; seed++)
        {
                struct waterlink_identity i, r;
                struct waterlink_noise a, b;
                p8 si[32], sr[32], ei[32], er[32];
                p8 hello[WATERLINK_HELLO_BYTES];
                p8 first[WATERLINK_DATAGRAM], second[WATERLINK_DATAGRAM];
                p8 heard_hello[WATERLINK_HELLO_BYTES], who[32];
                p8 k1[16], k2[16], k3[16], k4[16];
                p32 index = 0;

                sim_rng = 0x51ed2701ull * (seed + 1);
                for (positive n = 0; n < 32; n++)
                {
                        si[n] = (p8)sim_next();
                        sr[n] = (p8)sim_next();
                        ei[n] = (p8)sim_next();
                        er[n] = (p8)sim_next();
                }
                for (positive n = 0; n < sizeof hello; n++)
                        hello[n] = (p8)sim_next();

                waterlink_identity_from(address_of i, si);
                waterlink_identity_from(address_of r, sr);
                waterlink_initiate(address_of a, address_of i, r.public, null, ei,
                                   hello, first);
                if (!waterlink_accept(address_of b, address_of r, null, first, who,
                                      heard_hello))
                        return 1;
                waterlink_respond(address_of b, er, 7, (p32)seed, second);
                if (!waterlink_answered(address_of a, address_of i, second,
                                        address_of index))
                        return 1;
                waterlink_split(address_of a, true, k1, k2);
                waterlink_split(address_of b, false, k3, k4);

                hex_out(si, 32);
                hex_out(sr, 32);
                hex_out(ei, 32);
                hex_out(er, 32);
                hex_out(hello, sizeof hello);
                hex_out(first + 16, WATERLINK_INITIATE_BYTES - 16);
                hex_out(second + 16, WATERLINK_RESPOND_BYTES - 16);
                hex_out(k1, 16);
                hex_out(k2, 16);
                string_format(log, "\n");
        }
        log_flush();
        return 0;
}

/*
        Greetings for the harness: seeded initiator and group keys, ephemeral,
        pre-shared key and hello, and the first message's body, one line a
        seed.
*/
static b32 pair_vectors(positive count)
{
        for (positive seed = 0; seed < count; seed++)
        {
                struct waterlink_identity i, group;
                struct waterlink_noise a, b;
                p8 si[32], sg[32], ei[32], psk[32], hello[WATERLINK_HELLO_BYTES];
                p8 greeting[WATERLINK_DATAGRAM], who[32],
                        heard[WATERLINK_HELLO_BYTES];

                sim_rng = 0x9a1b2c3dull * (seed + 1);
                for (positive n = 0; n < 32; n++)
                {
                        si[n] = (p8)sim_next();
                        sg[n] = (p8)sim_next();
                        ei[n] = (p8)sim_next();
                        psk[n] = (p8)sim_next();
                }
                for (positive n = 0; n < WATERLINK_HELLO_BYTES; n++)
                        hello[n] = (p8)sim_next();
                waterlink_identity_from(address_of i, si);
                waterlink_identity_from(address_of group, sg);
                if (!waterlink_initiate(address_of a, address_of i,
                                        group.public, psk, ei, hello,
                                        greeting) ||
                    !waterlink_accept(address_of b, address_of group, psk,
                                      greeting, who, heard))
                        return 1;

                hex_out(si, 32);
                hex_out(sg, 32);
                hex_out(ei, 32);
                hex_out(psk, 32);
                hex_out(hello, WATERLINK_HELLO_BYTES);
                hex_out(greeting + 16, WATERLINK_INITIATE_BYTES - 16);
                string_format(log, "\n");
        }
        log_flush();
        return 0;
}

static fn hex_line(p8 address_to bytes, positive length)
{
        static p8 text[2 * WATERLINK_MDNS_MAX + 2];
        positive used = memory_into_hex(text, bytes, length);

        text[used++] = '\n';
        text[used] = 0;
        string_format(log, "%s", (string_address)text);
}

/*
        mDNS for the harness: an announcement for dnspython to
        read, and a reader for packets dnspython builds -- hex in on standard
        input, one packet a line, what was found out.
*/
//      Hex in either case, for lines of packets.
static p8 address_to mdns_hex_values(void)
{
        static p8 values[256];

        memory_fill(values, 255, sizeof values);
        for (positive at = 0; at < 16; at++)
        {
                values["0123456789abcdef"[at]] = (p8)at;
                values["0123456789ABCDEF"[at]] = (p8)at;
        }
        return values;
}

static b32 mdns_modes(string_address mode)
{
        if (string_equals(mode, "mdns-announce"))
        {
                p8 instance[10];
                p8 host[6] = {0xa1, 0xb2, 0xc3, 0xd4, 0xe5, 0xf6};
                p8 packet[WATERLINK_MDNS_MAX];
                positive length;

                random_seeded(instance, 10, 1);
                length = waterlink_mdns_announce(packet, sizeof packet, instance,
                                                 host, 22348, 0x0a4d0002, 4500,
                                                 0, null, 0);
                hex_line(packet, length);
                length = waterlink_mdns_query(packet, sizeof packet);
                hex_line(packet, length);
                log_flush();
                return 0;
        }

        //      mdns-read
        {
                static p8 line[2 * WATERLINK_MDNS_MAX + 4];

                for (;;)
                {
                        positive used = 0;
                        p8 packet[WATERLINK_MDNS_MAX];
                        struct waterlink_found found;
                        p8 good;

                        for (;;)
                        {
                                bipolar got = system_read_once(0, line + used, 1);

                                if (got <= 0)
                                {
                                        log_flush();
                                        return 0;
                                }
                                if (line[used] == '\n')
                                        break;
                                if (used < sizeof line - 1)
                                        used++;
                        }
                        if (used % 2 || used / 2 > WATERLINK_MDNS_MAX ||
                            memory_decode_power2(packet, line, used / 2,
                                                 mdns_hex_values(), 4) !=
                                    used / 2)
                        {
                                string_format(log, "bad\n");
                                continue;
                        }
                        good = waterlink_mdns_read(packet, used / 2,
                                                   address_of found);
                        string_format(log, "%s %s %p", good ? "ok" : "refused",
                                      found.asked ? "asked" : "-",
                                      found.count);
                        for (positive at = 0; good && at < found.count; at++)
                        {
                                struct waterlink_found_instance address_to one =
                                        found.instance + at;

                                if (one->has_port)
                                        string_format(log, " %p",
                                                      (positive)one->port);
                        }
                        string_format(log, "\n");
                        log_flush();
                }
        }
}

b32 main(void)
{
        if (program_argument_count() > 1)
        {
                string_address mode = program_argument_list()[1];

                if (string_equals(mode, "pair"))
                        return pair_vectors(64);
                if (string_equals(mode, "mdns-announce") ||
                    string_equals(mode, "mdns-read"))
                        return mdns_modes(mode);
                return noise_vectors(64);
        }
        sealed();
        sealed_short();
        sealed_generated();
        sealed_small();
        whole_path();
        refusals();
        supersession();
        class_is_the_keys();
        urgency();
        stale_arrivals();
        durable_reordered();
        malformed_bodies();
        judge_short_exhaustive();
        body_prefix_boundaries();
        judge_procedural();
        fill_procedural();
        apply_procedural();
        replay();
        saturation();
        replay_generated();
        last_arrives_once();
        superseded_in_flight();
        full_frame_beside_owed_ack();
        reader_credit();
        held_floods();
        held_answer_lost();
        held_after_losses();
        register_behind_its_newest();
        invalid_application_keys();
        network_generated();
        clock_ordering();
        handshake();
        pbkdf2_vectors();
        group_derivation();
        group_identity();
        group_pairing();
        mdns_parse();
        mdns_wire_exact();
        discovery_vectors();
        discovery_setting_rows();
        return test_report(null);
}
#endif /* CHECK_waterlink */

#ifdef CHECK_waterlink_service
/*
        Waterlink's service half, driven directly: the admission, pairing,
        authorization and transfer code in service.c, nearby.c and command.c
        that CHECK_waterlink's pure transforms never reach, and the link lane
        only reaches the way a well-behaved peer does.

        The whole shell is compiled in, so the functions under test are the
        ones that ship. Two things are arranged around them. The entropy
        source is wrapped, so a failing getrandom can be switched on as the
        kernel would fail it. And the run happens in a user and mount
        namespace of its own, with tmpfs over /root and /run, so the
        authorization files are root's and the test owns them. Datagrams go
        over loopback to a socket the test reads, so "was answered" and "was
        not answered" are what the kernel delivered.
*/
#define system_random_fill system_random_fill_kernel
#include "../../src/lib.util.c"
#undef system_random_fill

static p8 entropy_down;
static positive entropy_draws;
static positive entropy_fail_length;
static fn (*entropy_before_fill)(void);

static bipolar system_random_fill(address_any into, positive length,
                                  positive flags)
{
        entropy_draws++;
        if (entropy_down ||
            (entropy_fail_length && length == entropy_fail_length))
                return -5;
        if (entropy_before_fill)
        {
                fn (*before)(void) = entropy_before_fill;

                entropy_before_fill = null;
                before();
        }
        return system_random_fill_kernel(into, length, flags);
}

static p8 wls_open_capture;
static positive wls_open_calls;
static bipolar wls_reopen_error;

static bipolar wls_call3(positive number, positive one, positive two,
                         positive three)
{
        if (wls_open_capture && number == syscall(openat))
                wls_open_calls++;
        if (wls_reopen_error && number == syscall(openat) && two &&
            !string_compare_max((string_address)two, "/proc/self/fd/", 14))
                return wls_reopen_error;
        return system_call_3(number, one, two, three);
}

static p8 wls_wait_capture;
static timespec wls_wait_limit;
static positive wls_wait_calls;

static bipolar wls_poll_wait(system_poll_descriptor address_to descriptors,
                             positive count, timespec address_to limit,
                             positive address_to signal_mask)
{
        if (wls_wait_capture)
        {
                wls_wait_limit = *limit;
                wls_wait_calls++;
                return 0;
        }
        return system_poll_wait(descriptors, count, limit, signal_mask);
}

#define system_poll_wait wls_poll_wait
#define system_call_3(...) wls_call3(__VA_ARGS__)
#include "../../src/moonwater/spark.c"
#define SHELL_UTILITY_PROGRAM
#include "../../src/sh/shell.c"
#undef system_poll_wait
#undef system_call_3

/*      The audio modem's half of the link (the send section of modem.c) is
        built with the sound server, which the utilities alone have not: here
        the transport is the network, as it is on a machine whose
        /root/link.transport says nothing. */
static p8 link_modem_carrying(void) { return false; }
static p8 link_modem_put(p8 address_to bytes, positive length, p64 when)
{
        (void)bytes; (void)length; (void)when;
        return false;
}
static p64 link_modem_turn(p64 now) { (void)now; return ~0ull; }
static void link_modem_stop(void) {}
static p64 link_modem_floor(void) { return 0; }
static p64 link_modem_ceiling(void) { return 0; }
static p64 link_modem_queued_from(void) { return ~0ull; }
static positive link_modem_free(void) { return 0; }
#define SHARED_counted
#include "../checks.c"
#undef SHARED_counted

static p8 wls_write(string_address path, address_any data, positive length,
                      positive mode)
{
        bipolar handle;
        p8 good;

        (void)system_remove_at(AT_FDCWD, path, 0);
        handle = system_open_at_mode(AT_FDCWD, path,
                                     FILE_WRITE | FILE_EXCLUSIVE | O_CLOEXEC,
                                     mode);
        if (handle < 0)
                return false;
        good = system_write_all((positive)handle, data, length) == length;
        (void)system_call_2(syscall(fchmod), (positive)handle, mode);
        system_close(handle);
        return good;
}

static p8 wls_text(string_address path, string_address text)
{
        bipolar handle = system_open_at(AT_FDCWD, path, FILE_WRITE | O_CLOEXEC);
        positive length = string_length(text);
        p8 good;

        if (handle < 0)
                return false;
        good = system_write_all((positive)handle, text, length) == length;
        system_close(handle);
        return good;
}

//      A namespace where this process is root and /root and /run are its own.
static p8 wls_sandbox(void)
{
        p8 map[64];
        bipolar uid = system_call_1(syscall(getuid), 0);
        bipolar gid = system_call_1(syscall(getgid), 0);
        positive at;

        if (system_call_1(syscall(unshare), 0x10000000 | 0x00020000 | 0x04000000) < 0)
                return false;
        map[0] = '0';
        map[1] = ' ';
        at = 2 + positive_into(map + 2, (positive)uid);
        memory_copy(map + at, " 1\n", 4);
        if (!wls_text("/proc/self/uid_map", (string_address)map))
                return false;
        (void)wls_text("/proc/self/setgroups", "deny");
        at = 2 + positive_into(map + 2, (positive)gid);
        memory_copy(map + at, " 1\n", 4);
        if (!wls_text("/proc/self/gid_map", (string_address)map))
                return false;
        if (system_call_5(syscall(mount), 0, (positive) "/", 0,
                          0x4000 | (1 << 18), 0) < 0)
                return false;
        if (system_call_5(syscall(mount), (positive) "tmpfs", (positive) "/root",
                          (positive) "tmpfs", 0, (positive) "mode=0700") < 0 ||
            system_call_5(syscall(mount), (positive) "tmpfs", (positive) "/run",
                          (positive) "tmpfs", 0, (positive) "mode=0755") < 0)
                return false;
        return system_make_directory_at(AT_FDCWD, HOST_STATE, 0755) >= 0;
}

//      A loopback socket the test reads what the service sent from.
static bipolar wls_listener(p16 address_to port)
{
        socket_address_internet6 self;
        b32 size = sizeof self;
        bipolar handle = socket_new(AF_INET6, SOCK_DGRAM | SOCK_CLOEXEC |
                                                  SOCK_NONBLOCK, 0);

        if (handle < 0)
                return handle;
        memory_zero(address_of self, sizeof self);
        self.family = AF_INET6;
        self.host[15] = 1;
        if (socket_bind((b32)handle, address_of self, sizeof self) < 0 ||
            system_call_3(syscall(getsockname), (positive)handle,
                          (positive)address_of self,
                          (positive)address_of size) < 0)
                return -1;
        address_to port = network_order_16(self.port);
        return handle;
}

//      What the service sent to that socket since the last look, if anything.
static bipolar wls_heard(bipolar handle, p8 address_to datagram)
{
        socket_address_internet6 from;
        b32 size = sizeof from;
        timespec pause = {0, 20000000};
        bipolar got;

        (void)system_call_2(syscall(nanosleep), (positive)address_of pause, 0);
        got = socket_receive((b32)handle, datagram, WATERLINK_DATAGRAM + 16,
                             MSG_DONTWAIT, address_of from, address_of size);
        return got;
}

static fn wls_drain(bipolar handle)
{
        p8 junk[WATERLINK_DATAGRAM + 16];

        while (wls_heard(handle, junk) > 0)
                ;
}

static fn wls_identity(struct waterlink_identity address_to identity, p8 seed)
{
        p8 secret[32];

        for (positive i = 0; i < 32; i++)
                secret[i] = (p8)(seed * 37 + i * 11 + 1);
        waterlink_identity_from(identity, secret);
}

static fn wls_seeded(p8 address_to into, positive length, p8 seed)
{
        for (positive i = 0; i < length; i++)
                into[i] = (p8)(seed * 91 + i * 17 + 5);
}

static fn wls_peers_with(p8 address_to key, p32 may)
{
        struct waterlink_peer peer;

        memory_zero(address_of peer, sizeof peer);
        memory_copy(peer.key, key, 32);
        string_copy(peer.name, "client");
        peer.may = may;
        (void)wls_write(LINK_PEERS_PATH, address_of peer, sizeof peer, 0600);
}

static positive wls_peers_count(void)
{
        link_peers peers;

        link_peers_load(address_of peers);
        return peers.count;
}

static positive wls_sessions_used(void)
{
        positive used = 0;

        for (positive at = 0; at < LINK_SESSIONS; at++)
                used += link_self.session[at].used;
        return used;
}

static p8 wls_loopback[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};

/*
        The authorization files are read only as root's own, private, whole
        records -- and anything else grants nothing.
*/
static fn authorization_files(void)
{
        struct waterlink_peer two[2];
        p8 key[32];

        wls_seeded(key, 32, 3);
        wls_peers_with(key, WATERLINK_MAY_DEFAULT);
        check("sec: a private, whole peer file grants", wls_peers_count() == 1);

        (void)system_call_4(syscall(fchmodat), (positive)(bipolar)AT_FDCWD,
                            (positive)LINK_PEERS_PATH, 0644, 0);
        check("sec: a peer file others can read grants nothing",
              wls_peers_count() == 0);
        (void)system_call_4(syscall(fchmodat), (positive)(bipolar)AT_FDCWD,
                            (positive)LINK_PEERS_PATH, 0620, 0);
        check("sec: a peer file its group can write grants nothing",
              wls_peers_count() == 0);
        (void)system_call_4(syscall(fchmodat), (positive)(bipolar)AT_FDCWD,
                            (positive)LINK_PEERS_PATH, 0600, 0);
        check("sec: made private again it grants again",
              wls_peers_count() == 1);

        memory_zero(two, sizeof two);
        memory_copy(two[0].key, key, 32);
        (void)wls_write(LINK_PEERS_PATH, two, sizeof(struct waterlink_peer) + 40,
                        0600);
        check("sec: a peer file with a partial record grants nothing",
              wls_peers_count() == 0);

        {
                positive room = (LINK_PEERS_MAX + 1) *
                                sizeof(struct waterlink_peer);
                p8 address_to big = (p8 address_to)system_call_6(
                        syscall(mmap), 0, room, 3, 0x22, (positive)(bipolar)-1, 0);

                memory_zero(big, room);
                memory_copy(big, key, 32);
                (void)wls_write(LINK_PEERS_PATH, big, room, 0600);
                check("sec: a peer file larger than the table grants nothing",
                      wls_peers_count() == 0);
                (void)system_call_2(syscall(munmap), (positive)big, room);
        }

        wls_peers_with(key, WATERLINK_MAY_DEFAULT);
        (void)system_rename_at(AT_FDCWD, LINK_PEERS_PATH, AT_FDCWD,
                               "/root/peers.elsewhere", 0);
        (void)system_symbolic_link_at("/root/peers.elsewhere", AT_FDCWD,
                                      LINK_PEERS_PATH);
        check("sec: a peer file that is a symbolic link grants nothing",
              wls_peers_count() == 0);
        (void)system_remove_at(AT_FDCWD, LINK_PEERS_PATH, 0);
        (void)system_make_directory_at(AT_FDCWD, LINK_PEERS_PATH, 0700);
        check("sec: a peer file that is a directory grants nothing",
              wls_peers_count() == 0);
        (void)system_remove_at(AT_FDCWD, LINK_PEERS_PATH, AT_REMOVEDIR);

        {
                struct link_group_record group;
                link_groups groups;

                memory_zero(address_of group, sizeof group);
                string_copy(group.namespace, "office");
                (void)wls_write(LINK_GROUPS_PATH, address_of group,
                                sizeof group, 0644);
                link_groups_load(address_of groups);
                check("sec: a group file others can read grants nothing",
                      groups.count == 0);
                (void)system_call_4(syscall(fchmodat),
                                    (positive)(bipolar)AT_FDCWD,
                                    (positive)LINK_GROUPS_PATH, 0600, 0);
                link_groups_load(address_of groups);
                check("sec: and made private it is read", groups.count == 1);
                (void)system_remove_at(AT_FDCWD, LINK_GROUPS_PATH, 0);
        }

        /*      A record from before 2026-09-30 has a SHA-256 of the secret in
                its check field. Reading the file takes it out of the file,
                not only out of memory: a listener never writes the file, so
                it stayed on the disk until somebody joined or left. */
        {
                struct link_group_record group;
                struct link_group_record stored;
                link_groups groups;
                bipolar handle;
                bipolar read = -1;

                memory_zero(address_of group, sizeof group);
                string_copy(group.namespace, "office");
                memory_fill(group.key, 0x5a, sizeof group.key);
                memory_fill(group.check, 0xa5, sizeof group.check);
                group.may = 6;
                (void)wls_write(LINK_GROUPS_PATH, address_of group,
                                sizeof group, 0600);
                link_groups_load(address_of groups);
                memory_fill(address_of stored, 0xee, sizeof stored);
                handle = system_open_at(AT_FDCWD, LINK_GROUPS_PATH, FILE_READ);
                if (handle >= 0)
                {
                        read = system_read_retry((positive)handle,
                                                 address_of stored,
                                                 sizeof stored);
                        system_close((positive)handle);
                }
                check("sec: reading a group file with an old check reads the group",
                      groups.count == 1 && groups.record[0].may == 6 &&
                          string_equals(groups.record[0].namespace, "office"));
                check("sec: and takes the old check out of the file as well, "
                      "the rest of the record as it was",
                      read == (bipolar)sizeof stored &&
                          memory_span_byte(stored.check, 0,
                                           sizeof stored.check) ==
                              sizeof stored.check &&
                          !memory_compare(stored.key, group.key,
                                          sizeof group.key) &&
                          stored.may == 6 &&
                          string_equals(stored.namespace, "office"));
                (void)system_remove_at(AT_FDCWD, LINK_GROUPS_PATH, 0);
        }
        {
                static const p8 zeros[32];
                bipolar full = system_open_at(AT_FDCWD,
                                               (string_address)"/dev/full",
                                               FILE_WRITE | O_CLOEXEC);

                check("the legacy group scrub's fault sink opens", full >= 0);
                if (full >= 0)
                {
                        check("sec: a refused positional scrub is reported, not mistaken for migration",
                              file_transfer_exact(syscall(pwrite64), full,
                                                  (p8 address_to)zeros,
                                                  sizeof zeros, 0) !=
                                  (bipolar)sizeof zeros);
                        system_close(full);
                }
        }
}

/*
        Staging: a fresh exclusive name beside the target, published only
        while the name is still the inode the transfer holds open.
*/
static positive key_claim_kind;
static p8 key_claim[33];
static file_facts key_claim_facts;
static p8 key_claim_ready;

/* Runs after link_secret has observed ENOENT and before its publication.
   The competing initializer controls a real pathname, not a mocked stat. */
static fn identity_key_claim(void)
{
        key_claim_ready = false;
        if (key_claim_kind == 4)
        {
                key_claim_ready = wls_write("/root/other-key", key_claim, 32, 0600) &&
                    system_symbolic_link_at("/root/other-key", AT_FDCWD,
                                            LINK_KEY_PATH) >= 0;
        }
        else if (key_claim_kind == 5)
                key_claim_ready = system_make_directory_at(AT_FDCWD,
                                                           LINK_KEY_PATH, 0700) >= 0;
        else
        {
                positive length = key_claim_kind == 2 ? 31
                                  : key_claim_kind == 3 ? 0
                                  : key_claim_kind == 6 ? 33 : 32;

                key_claim_ready = wls_write(LINK_KEY_PATH, key_claim, length,
                                            key_claim_kind == 1 ? 0644 : 0600);
        }
        key_claim_ready &= file_look(AT_FDCWD, LINK_KEY_PATH, AT_SYMLINK_NOFOLLOW,
                                    address_of key_claim_facts);
}

static fn identity_creation_races(void)
{
        for (positive row = 0; row < 7; row++)
        {
                p8 secret[32], read[33];
                file_facts after;

                (void)system_remove_at(AT_FDCWD, LINK_KEY_PATH, 0);
                key_claim_kind = row;
                wls_seeded(key_claim, sizeof key_claim, (p8)(20 + row));
                memory_fill(secret, 0xa5, sizeof secret);
                entropy_before_fill = identity_key_claim;
                bipolar status = link_secret(secret, true);

                entropy_before_fill = null;
                check("concurrent key claimant is installed after the initial lookup",
                      key_claim_ready);
                check("only a private whole concurrently created key is accepted",
                      row ? status < 0
                          : status == 0 && !memory_compare(secret, key_claim, 32));
                check("first key creation never replaces a concurrent pathname",
                      file_look(AT_FDCWD, LINK_KEY_PATH, AT_SYMLINK_NOFOLLOW,
                                address_of after) &&
                          file_same_identity(address_of after,
                                              address_of key_claim_facts) &&
                          after.mode == key_claim_facts.mode &&
                          after.size == key_claim_facts.size);
                if (row != 4 && row != 5)
                {
                        bipolar handle = system_open_at(AT_FDCWD, LINK_KEY_PATH,
                                                         FILE_READ | O_CLOEXEC);
                        bipolar got = handle < 0 ? handle
                            : system_read_once(handle, read, sizeof read);

                        if (handle >= 0) system_close(handle);
                        check("a concurrent key's bytes never become the losing initializer's secret",
                              got == (bipolar)key_claim_facts.size &&
                                  !memory_compare(read, key_claim,
                                                  key_claim_facts.size));
                }
                crypto_forget(secret, sizeof secret);
                (void)system_remove_at(AT_FDCWD, LINK_KEY_PATH,
                                       row == 5 ? AT_REMOVEDIR : 0);
                (void)system_remove_at(AT_FDCWD, "/root/other-key", 0);
        }
        {
                struct waterlink_identity first, again;

                check("an uncontended first identity is created and reused",
                      link_identity(address_of first, true) == 0 &&
                          link_identity(address_of again, true) == 0 &&
                          !memory_compare(first.public, again.public, 32));
                crypto_forget(address_of first, sizeof first);
                crypto_forget(address_of again, sizeof again);
        }
        (void)system_remove_at(AT_FDCWD, LINK_KEY_PATH, 0);
        entropy_down = true;
        p8 secret[32];
        check("identity creation without entropy publishes no key",
              link_secret(secret, true) == -EIO &&
                  !file_look_at(LINK_KEY_PATH, address_of key_claim_facts));
        entropy_down = false;
        crypto_forget(secret, sizeof secret);
        crypto_forget(key_claim, sizeof key_claim);
}

static fn staging(void)
{
        p8 part[LINK_REQUEST_MAX + 32];
        p8 other[LINK_REQUEST_MAX + 32];
        p8 planted[] = "somebody else's";
        p8 read[64];
        bipolar handle;
        bipolar second;

        (void)wls_write("/root/target.link-part", planted, sizeof planted - 1,
                        0600);
        handle = link_part_open("/root/target", 12, part, sizeof part, 0640);
        check("sec: a staging file opens beside a planted predictable one",
              handle >= 0 &&
                      !memory_compare(part, "/root/target.link-part.", 23) &&
                      string_length((string_address)part) == 12 + 11 + 16);
        {
                bipolar planted_handle = system_open_at(
                        AT_FDCWD, "/root/target.link-part", FILE_READ);
                bipolar got = system_read_once(planted_handle, read, sizeof read);

                system_close(planted_handle);
                check("sec: and the planted file is left as it was",
                      got == (bipolar)(sizeof planted - 1) &&
                              !memory_compare(read, planted, sizeof planted - 1));
        }
        second = link_part_open("/root/target", 12, other, sizeof other, 0640);
        check("sec: two transfers to one target stage under different names",
              second >= 0 && !string_equals((string_address)part,
                                            (string_address)other));
        check("sec: a staging file is owned while its name is its inode",
              link_part_owned(handle, part));

        (void)system_rename_at(AT_FDCWD, part, AT_FDCWD, "/root/moved", 0);
        (void)wls_write((string_address)part, planted, 4, 0600);
        check("sec: a staging name replaced by another file is not owned",
              !link_part_owned(handle, part));
        (void)system_remove_at(AT_FDCWD, part, 0);
        (void)system_symbolic_link_at("/root/moved", AT_FDCWD, part);
        check("sec: nor one replaced by a link to the staging inode itself",
              !link_part_owned(handle, part));
        (void)system_remove_at(AT_FDCWD, part, 0);
        check("sec: nor a name removed", !link_part_owned(handle, part));
        system_close(handle);
        system_close(second);
        (void)system_remove_at(AT_FDCWD, (string_address)other, 0);
        (void)system_remove_at(AT_FDCWD, "/root/moved", 0);

        {
                p8 small[24];

                check("sec: a target too long to stage is refused",
                      link_part_open("/root/target", 12, small, sizeof small,
                                     0640) == -ERROR_NAME_TOO_LONG);
        }

        entropy_down = true;
        check("sec: with no entropy nothing is staged",
              link_part_open("/root/target", 12, part, sizeof part, 0640) ==
                      -EIO);
        entropy_down = false;
}

//      A push session whose part file is opened and filled as the server does.
static p8 wls_push(struct link_session address_to s, string_address target)
{
        if (!link_session_open(s))
                return false;
        s->kind = LINK_KIND_PUSH;
        if (!link_start_push(s, (p8 address_to)target, string_length(target),
                             0640))
                return false;
        return system_write_all((positive)s->writes[0].fd, "whole", 5) == 5;
}

static fn publication(void)
{
        struct link_session address_to s = link_self.session;
        p8 read[16];
        p8 kept[LINK_REQUEST_MAX + 32];

        (void)wls_write("/root/dest", "before", 6, 0644);
        check("a push opens", wls_push(s, "/root/dest"));
        link_push_done(s, 1);
        {
                bipolar handle = system_open_at(AT_FDCWD, "/root/dest", FILE_READ);
                bipolar got = system_read_once(handle, read, sizeof read);

                system_close(handle);
                check("sec: a whole, still-owned push is published",
                      !s->failed && got == 5 && !memory_compare(read, "whole", 5));
        }
        system_close(s->writes[0].fd);
        s->writes[0].fd = -1;
        link_session_close(s);

        (void)wls_write("/root/dest", "before", 6, 0644);
        check("a second push opens", wls_push(s, "/root/dest"));
        string_copy((string_address)kept, (string_address)s->part);
        (void)system_remove_at(AT_FDCWD, (string_address)kept, 0);
        (void)wls_write((string_address)kept, "planted", 7, 0600);
        link_push_done(s, 1);
        {
                bipolar handle = system_open_at(AT_FDCWD, "/root/dest", FILE_READ);
                bipolar got = system_read_once(handle, read, sizeof read);
                bipolar planted = system_open_at(AT_FDCWD, (string_address)kept,
                                                 FILE_READ);

                system_close(handle);
                check("sec: a push whose staging name was replaced is not published",
                      s->failed && got == 6 && !memory_compare(read, "before", 6));
                check("sec: and the file put in its place is not removed",
                      planted >= 0);
                if (planted >= 0)
                        system_close(planted);
        }
        system_close(s->writes[0].fd);
        s->writes[0].fd = -1;
        link_session_close(s);
        (void)system_remove_at(AT_FDCWD, (string_address)kept, 0);

        check("a third push opens", wls_push(s, "/root/dest"));
        string_copy((string_address)kept, (string_address)s->part);
        link_session_close(s);
        check("sec: an interrupted push removes its own staging file",
              system_open_at(AT_FDCWD, (string_address)kept, FILE_READ) < 0);

        check("a fourth push opens", wls_push(s, "/root/dest"));
        string_copy((string_address)kept, (string_address)s->part);
        (void)system_remove_at(AT_FDCWD, (string_address)kept, 0);
        (void)wls_write((string_address)kept, "planted", 7, 0600);
        link_session_close(s);
        {
                bipolar planted = system_open_at(AT_FDCWD, (string_address)kept,
                                                 FILE_READ);

                check("sec: an interrupted push leaves a name that is no longer its own",
                      planted >= 0);
                if (planted >= 0)
                        system_close(planted);
        }
        (void)system_remove_at(AT_FDCWD, (string_address)kept, 0);
}

/*
        A command for a session to hang up on: one that hears the hangup and
        ends with it, or one that ignores it and ends on its own a moment
        later. Answers its pidfd, which the session closes with.
*/
static bipolar wls_command(struct link_session address_to s, p8 deaf)
{
        b32 ends[2];
        bipolar child;
        p8 told = 0;

        if (system_pipe(ends, 0) < 0)
                return -1;
        child = system_fork();
        if (!child)
        {
                timespec pause = {deaf ? 0 : 30, deaf ? 300000000 : 0};
                p64 ignore[4] = {deaf ? 1 : 0, 0, 0, 0};

                //      The hangup is what one hears or one does not, and not
                //      what the runner started this with: a run under nohup
                //      hands every child a SIGHUP that is ignored.
                (void)system_call_4(syscall(rt_sigaction), 1, (positive)ignore,
                                    0, 8);
                //      And it is said once that it is so, since a hangup that
                //      came before it was a hangup to an ignoring process.
                system_close((bipolar)ends[0]);
                (void)system_write_all((positive)ends[1], address_of told, 1);
                system_close((bipolar)ends[1]);
                (void)system_call_2(syscall(nanosleep),
                                    (positive)address_of pause, 0);
                exit(0);
        }
        system_close((bipolar)ends[1]);
        if (child > 0)
                (void)system_read_retry((positive)ends[0], address_of told, 1);
        system_close((bipolar)ends[0]);
        if (child < 0 || !link_session_open(s))
                return -1;
        s->kind = LINK_KIND_RUN;
        s->pid = child;
        s->pidfd = system_call_2(syscall(pidfd_open), (positive)child, 0);
        return child;
}

//      Whether a process is still in the table, a zombie or not.
static p8 wls_present(bipolar pid)
{
        return system_call_2(syscall(kill), (positive)pid, 0) == 0;
}

//      Until nothing waits to be waited for, or a few seconds.
static fn wls_reaped(void)
{
        timespec pause = {0, 20000000};

        for (positive tick = 0; tick < 250 && link_reaping_count; tick++)
        {
                link_reap();
                (void)system_call_2(syscall(nanosleep),
                                    (positive)address_of pause, 0);
        }
}

/*
        A session that ends hangs up on its command, and the command is
        waited for however it ends: not left a zombie in the listener's table
        of processes, as every one was.
*/
static fn reaped_commands(void)
{
        struct link_session address_to s = link_self.session;
        bipolar child;
        bipolar deaf;
        bipolar group;
        bipolar many[LINK_REAPING + 4];
        positive before;
        p8 all = true;

        //      Whatever the checks before this closed on a command that was
        //      still running -- a pull's cat -- is waited for first, so the
        //      table starts empty and what it holds after is these.
        wls_reaped();
        before = link_reaping_count;
        child = wls_command(s, false);
        link_session_close(s);
        wls_reaped();
        check("sec: a command hung up on at the close of its session is "
              "waited for, and gone",
              child > 0 && link_reaping_count == before && !wls_present(child));

        deaf = wls_command(s, true);
        link_session_close(s);
        check("sec: one that ignores the hangup is kept to be waited for",
              deaf > 0 && link_reaping_count == before + 1 &&
                      wls_present(deaf));
        wls_reaped();
        check("sec: and is waited for when it ends",
              link_reaping_count == before && !wls_present(deaf));

        //      A session the listener gave up on: nothing heard in 45
        //      seconds, as when its client was killed and said nothing.
        link_self.server = true;
        child = wls_command(s, false);
        s->heard = link_now() - LINK_DEAD - 1;
        (void)link_sessions_turn(link_now());
        link_self.server = false;
        wls_reaped();
        check("sec: so is the command of a session that went silent",
              child > 0 && !s->used && link_reaping_count == before &&
                      !wls_present(child));

        //      A command already waited for is not signalled again by the
        //      number it had: it may be somebody else's by now.
        group = system_fork();
        if (!group)
        {
                timespec pause = {30, 0};

                (void)system_call(syscall(setsid));
                (void)system_call_2(syscall(nanosleep),
                                    (positive)address_of pause, 0);
                exit(0);
        }
        if (link_session_open(s))
        {
                s->kind = LINK_KIND_RUN;
                s->pid = group;
                s->exited = true;
                link_session_close(s);
        }
        check("sec: and the group of one that has exited is not hung up on",
              group > 0 && wls_present(group));
        (void)system_call_2(syscall(kill), (positive)group, 9);
        (void)system_wait4_retry((b32)group, null, 0, null);

        //      More hung up on than there are places to wait in: the table
        //      does not grow past its end, and every command is still hung
        //      up on.
        for (positive at = 0; at < array_count(many); at++)
        {
                many[at] = wls_command(s, true);
                all &= many[at] > 0;
                link_session_close(s);
        }
        check("sec: the commands waiting to be waited for are bounded",
              all && link_reaping_count == LINK_REAPING);
        wls_reaped();
        check("sec: and those that stayed in it are all waited for",
              link_reaping_count == 0);
        for (positive at = 0; at < array_count(many); at++)
                if (many[at] > 0)
                        (void)system_wait4_retry((b32)many[at], null, 0, null);
}

/*
        A request for a file, as a peer holding `files` and nothing else would
        send it: the answer is whether the machine took it up.
*/
static p8 wls_file_over(struct link_session address_to s, p8 ask,
                          string_address path, p8 over)
{
        p8 request[LINK_REQUEST_MAX + 8];
        p8 key[32];
        positive length = string_length(path);
        positive at = 1;

        wls_seeded(key, 32, 9);
        if (!link_session_open(s))
                return false;
        memory_copy(s->peer, key, 32);
        string_copy((string_address)s->name, "client");
        //      A session that has said it is over, by a refusal or an end.
        s->exit_sent = over;
        request[0] = ask;
        if (ask == LINK_ASK_PUSH)
        {
                //      The mode, then the format (raw) and the dictionary's id.
                memory_zero(request + 1, LINK_PUSH_HEAD - 1);
                at += LINK_PUSH_HEAD - 1;
        }
        memory_copy(request + at, path, length);
        link_request(s, request, at + length);
        return !s->exit_sent && s->kind != LINK_KIND_NONE;
}

static p8 wls_file_asked(struct link_session address_to s, p8 ask,
                           string_address path)
{
        return wls_file_over(s, ask, path, false);
}

/*
        The link's own state is not a file a grant for files reaches, however
        it is named; any other file is.
*/
static fn own_files(void)
{
        static const string_address own[] = {
            LINK_KEY_PATH, LINK_PEERS_PATH, LINK_GROUPS_PATH, LINK_STAMPS_PATH,
            LINK_STATE_PATH, LINK_LOCK_PATH, HOST_MACHINE_SCRIPT,
            LINK_PLACES_PATH, LINK_SWITCH_PATH, LINK_PORT_PATH,
            LINK_TRANSPORT_PATH};
        struct link_session address_to s = link_self.session;
        file_facts before[array_count(own)];
        p8 named = true, linked = true, doubled = true, spelled = true;
        p8 kept = true, pushed = false, pulled = false;
        p8 made[array_count(own)];
        string_address hard;
        p8 round[64];
        p8 key[32];

        wls_seeded(key, 32, 9);
        wls_peers_with(key, WATERLINK_MAY_FILES);
        host_state_ready();
        for (positive at = 0; at < array_count(own); at++)
        {
                made[at] = !file_look_at(own[at], address_of before[at]);
                if (made[at])
                        (void)wls_write(own[at], "x", 1, 0600);
                (void)file_look_at(own[at], address_of before[at]);
        }
        for (positive at = 0; at < array_count(own); at++)
        {
                file_facts after;

                (void)system_remove_at(AT_FDCWD, "/root/own-link", 0);
                hard = !string_compare_max(own[at], "/run/", 5)
                               ? "/run/own-hard"
                               : "/root/own-hard";
                (void)system_remove_at(AT_FDCWD, hard, 0);
                (void)system_remove_at(AT_FDCWD, "/root/own-hard", 0);
                (void)system_symbolic_link_at(own[at], AT_FDCWD,
                                              "/root/own-link");
                (void)system_call_5(syscall(linkat), (positive)(bipolar)AT_FDCWD,
                                    (positive)own[at],
                                    (positive)(bipolar)AT_FDCWD,
                                    (positive)hard, 0);
                if (!string_compare_max(own[at], "/run/", 5))
                {
                        string_copy((string_address)round, "/run/../run/");
                        string_append_bounded((string_address)round,
                                              own[at] + 5, sizeof round);
                }
                else
                {
                        string_copy((string_address)round, "/root/../root/");
                        string_append_bounded((string_address)round,
                                              own[at] + 6, sizeof round);
                }
                named &= link_path_is_own(own[at]);
                linked &= link_path_is_own("/root/own-link");
                doubled &= link_path_is_own(hard);
                spelled &= link_path_is_own((string_address)round);
                //      Neither verb takes it up, and nothing is staged.
                pushed |= wls_file_asked(s, LINK_ASK_PUSH, own[at]) ||
                          s->writes[0].fd >= 0;
                link_session_close(s);
                pulled |= wls_file_asked(s, LINK_ASK_PULL, own[at]) ||
                          s->pid > 0;
                link_session_close(s);
                pulled |= wls_file_asked(s, LINK_ASK_PULL, "/root/own-link") ||
                          s->pid > 0;
                link_session_close(s);
                kept &= file_look_at(own[at], address_of after) &&
                        file_same_identity(address_of before[at],
                                           address_of after) &&
                        before[at].size == after.size &&
                        before[at].modified.seconds == after.modified.seconds &&
                        before[at].modified.nanoseconds ==
                                after.modified.nanoseconds;
        }
        check("sec: the key, the machines, the groups, the stamps and the "
              "machine script are the link's own files",
              named);
        check("sec: and so is a link to one, and a second name for it, and "
              "a path spelled round it",
              linked && doubled && spelled);
        check("sec: a push or a pull of any of them is refused, nothing is "
              "staged or started, and the file is as it was",
              !pushed && !pulled && kept);
        check("a path that is not there, or any other file, is not",
              !link_path_is_own("/root/no-such-file") &&
                      wls_write("/root/ordinary", "y", 1, 0600) &&
                      !link_path_is_own("/root/ordinary"));
        check("and a push of another file is taken up",
              wls_file_asked(s, LINK_ASK_PUSH, "/root/ordinary") &&
                      s->writes[0].fd >= 0);
        link_session_close(s);
        (void)system_remove_at(AT_FDCWD, "/root/own-link", 0);
        (void)system_remove_at(AT_FDCWD, "/root/own-hard", 0);
        (void)system_remove_at(AT_FDCWD, "/run/own-hard", 0);
        (void)system_remove_at(AT_FDCWD, "/root/ordinary", 0);
        for (positive at = 0; at < array_count(own); at++)
                if (made[at])
                        (void)system_remove_at(AT_FDCWD, own[at], 0);
}

//      The answer a session posted, as the link holds it for sending: its
//      length and what the first bytes are.
static positive wls_answer(struct link_session address_to s, p8 address_to into)
{
        struct waterlink_sending address_to live =
                s->link->sending + LINK_KEY_ANSWER;
        struct waterlink_slot address_to slot;

        if (live->first == WATERLINK_NONE)
                return 0;
        slot = s->link->slot + live->first;
        memory_copy(into, slot->payload, slot->length < 8 ? slot->length : 8);
        return slot->length;
}

/*
        What the streams say when they fail. A read that cannot be done is
        not the end of an input -- a directory pushed, a file that refuses --
        and says why and ends nothing; a write that cannot be done is said to
        the loop that stops on it, which is how a reader that went away
        (`link run b yes | head -c 10`) stops what fills it; and a push that
        failed leaves no staging file beside its target.
*/
static fn missing_own_files(void)
{
        static const string_address own[] = {
            LINK_KEY_PATH, LINK_GROUPS_PATH, LINK_STAMPS_PATH, LINK_STATE_PATH,
            LINK_LOCK_PATH, HOST_MACHINE_SCRIPT, LINK_PLACES_PATH,
            LINK_SWITCH_PATH, LINK_PORT_PATH, LINK_TRANSPORT_PATH};
        p8 key[32];
        struct link_session address_to s = link_self.session;

        wls_seeded(key, sizeof key, 9);
        wls_peers_with(key, WATERLINK_MAY_FILES);
        host_state_ready();
        (void)system_make_directory_at(AT_FDCWD, "/root/unrelated", 0700);
        (void)system_symbolic_link_at("/root", AT_FDCWD, "/root/root-alias");
        for (positive row = 0; row < array_count(own); row++)
        {
                p8 around[128], ordinary[128];
                string_address leaf = own[row] + memory_after_last(
                    own[row], '/', string_length(own[row]));
                string_address parent = !string_compare_max(own[row], "/root/", 6)
                    ? "/root/../root/" : HOST_STATE "/./";

                (void)system_remove_at(AT_FDCWD, own[row], 0);
                string_copy(around, parent);
                string_append_bounded(around, leaf, sizeof around);
                check("missing protected control paths remain reserved",
                      link_path_is_own(own[row]) && link_path_is_own(around));
                if (!string_compare_max(own[row], "/root/", 6))
                {
                        string_copy(around, "/root/root-alias/");
                        string_append_bounded(around, leaf, sizeof around);
                        check("directory symlinks cannot hide a missing protected path",
                              link_path_is_own(around));
                }
                check("files-only peers cannot stage a missing control file",
                      !wls_file_asked(s, LINK_ASK_PUSH, own[row]) &&
                          s->exit_sent && s->writes[0].fd < 0);
                link_session_close(s);
                check("aliases of absent control paths cannot be staged",
                      !wls_file_asked(s, LINK_ASK_PUSH, around) &&
                          s->exit_sent && s->writes[0].fd < 0);
                link_session_close(s);
                string_copy(ordinary, "/root/unrelated/");
                string_append_bounded(ordinary, leaf, sizeof ordinary);
                check("a reserved basename in another directory is an ordinary file",
                      !link_path_is_own(ordinary) &&
                          wls_file_asked(s, LINK_ASK_PUSH, ordinary) &&
                          s->writes[0].fd >= 0);
                link_session_close(s);
        }
        (void)system_remove_at(AT_FDCWD, "/root/root-alias", 0);
        (void)system_remove_at(AT_FDCWD, "/root/unrelated", 0x200);
}

static fn standard_stream_prepare_failures(void)
{
        const bipolar errors[] = {-EMFILE, -EACCES, -ENOENT, -ELOOP};

        for (positive fd = 0; fd < 3; fd++)
        {
                b32 ends[2];
                bipolar saved = system_call_3(syscall(fcntl), fd, 1030, 10);
                bipolar opened = system_pipe(ends, O_CLOEXEC);

                check("standard stream failure fixture opens", saved >= 0 && opened >= 0);
                if (saved < 0 || opened < 0)
                {
                        if (saved >= 0) system_close(saved);
                        if (opened >= 0) { system_close(ends[0]); system_close(ends[1]); }
                        continue;
                }
                {
                        struct link_stream stream;

                        system_close(fd);
                        link_stream_own(address_of stream, fd,
                            fd ? LINK_KEY_OUTPUT : LINK_KEY_INPUT, 0,
                            fd ? FILE_WRITE : FILE_READ);
                        check("failed standard stream metadata disables the stream",
                              stream.fd == -1 && stream.done && stream.error == -EIO);
                }
                bipolar installed = system_descriptor_install(ends[fd ? 1 : 0], fd);
                check("standard stream pipe installs", installed >= 0);
                if (installed >= 0)
                {
                        for (positive row = 0; row < array_count(errors); row++)
                        {
                                struct link_stream stream;

                                wls_reopen_error = errors[row];
                                link_stream_own(address_of stream, fd,
                                    fd ? LINK_KEY_OUTPUT : LINK_KEY_INPUT, 0,
                                    fd ? FILE_WRITE : FILE_READ);
                                wls_reopen_error = 0;
                                check("nonblocking preparation failures disable the stream",
                                      stream.fd == -1 && stream.done &&
                                          stream.error == errors[row]);
                        }
                        {
                                struct link_stream stream;

                                link_stream_own(address_of stream, fd,
                                    fd ? LINK_KEY_OUTPUT : LINK_KEY_INPUT, 0,
                                    fd ? FILE_WRITE : FILE_READ);
                                check("successful preparation owns a nonblocking stream",
                                      stream.fd > 2 && !stream.error &&
                                          (system_call_3(syscall(fcntl), stream.fd,
                                                         3, 0) & O_NONBLOCK));
                                check("stream preparation preserves shared descriptor flags",
                                      !(system_call_3(syscall(fcntl), fd, 3, 0) & O_NONBLOCK));
                                if (stream.fd > 2) system_close(stream.fd);
                        }
                }
                (void)system_descriptor_install(saved, fd);
                system_close(saved);
                system_close(ends[0]);
                system_close(ends[1]);
        }
}

static fn stream_failures(void)
{
        struct link_session address_to s = link_self.session;
        p8 frame[2] = {LINK_DATA, 'x'};
        p8 last[1] = {LINK_END};
        p8 kept[LINK_REQUEST_MAX + 32];
        p8 read[16];
        file_facts gone;
        bipolar handle;
        positive room;
        b32 pair[2];

        check("a session opens for the streams", link_session_open(s));
        room = waterlink_room(s->link);
        handle = system_open_at(AT_FDCWD, "/root", FILE_READ | O_DIRECTORY | O_CLOEXEC);
        link_stream_set(s->reads, handle, LINK_KEY_INPUT, WATERLINK_FRAME_DURABLE);
        link_stream_read(s, s->reads);
        check("sec: an input that cannot be read is not an input that ended: "
              "it says why, and posts no end",
              s->reads[0].done && s->reads[0].error == -EISDIR &&
                      waterlink_room(s->link) == room);
        link_session_close(s);

        check("a session opens for an empty input", link_session_open(s));
        room = waterlink_room(s->link);
        handle = system_open_at(AT_FDCWD, "/dev/null", FILE_READ | O_CLOEXEC);
        link_stream_set(s->reads, handle, LINK_KEY_INPUT, WATERLINK_FRAME_DURABLE);
        link_stream_read(s, s->reads);
        check("and an input at its end posts its end, and no error",
              s->reads[0].done && !s->reads[0].error &&
                      waterlink_room(s->link) == room - 1);
        link_session_close(s);

        check("a session opens for an output", link_session_open(s));
        room = waterlink_room(s->link);
        handle = system_open_at(AT_FDCWD, "/root", FILE_READ | O_DIRECTORY | O_CLOEXEC);
        link_stream_set(s->reads, handle, LINK_KEY_OUTPUT, WATERLINK_FRAME_DURABLE);
        link_stream_read(s, s->reads);
        check("a machine's output that reads as an error is the end of it, as "
              "a terminal's last holder closing it is, with nothing said",
              s->reads[0].done && !s->reads[0].error &&
                      waterlink_room(s->link) == room);
        link_session_close(s);

        //      A write to a pipe nobody reads: the error, with SIGPIPE
        //      ignored as the loop's signalfd leaves it.
        {
                p64 ignore[4] = {1, 0, 0, 0};
                p64 old[4] = {0, 0, 0, 0};

                check("a session opens for a broken pipe", link_session_open(s));
                check("a pipe opens", system_pipe(pair, O_CLOEXEC) >= 0);
                system_close(pair[0]);
                (void)system_call_4(syscall(rt_sigaction), 13, (positive)ignore,
                                    (positive)old, 8);
                link_stream_set(s->writes, pair[1], LINK_KEY_OUTPUT, 0);
                check("a frame for a descriptor that is gone is taken",
                      link_stream_take(s, s->writes, frame, 2));
                (void)system_call_4(syscall(rt_sigaction), 13, (positive)old, 0,
                                    8);
                check("sec: and the stream says why it stopped, which is how "
                      "the client knows the reader went away",
                      s->failed && s->writes[0].error == -EPIPE);
                link_session_close(s);
        }

        //      A push whose disk refuses: a descriptor that cannot be
        //      written, on the staging file's inode.
        (void)wls_write("/root/refused", "before", 6, 0644);
        check("a push opens to be refused", wls_push(s, "/root/refused"));
        string_copy((string_address)kept, (string_address)s->part);
        handle = system_open_at(AT_FDCWD, (string_address)kept, FILE_READ | O_CLOEXEC);
        system_close(s->writes[0].fd);
        s->writes[0].fd = handle;
        (void)link_stream_take(s, s->writes, frame, 2);
        check("a write the disk refuses fails the push", s->failed &&
                                                            s->writes[0].error < 0);
        (void)link_stream_take(s, s->writes, last, 1);
        {
                bipolar target = system_open_at(AT_FDCWD, "/root/refused", FILE_READ);
                bipolar got = system_read_once(target, read, sizeof read);

                system_close(target);
                check("sec: a push that failed leaves no staging file, and "
                      "its target is as it was",
                      s->exited && s->status == 1 && !s->part[0] &&
                              !file_look_at((string_address)kept,
                                            address_of gone) &&
                              got == 6 && !memory_compare(read, "before", 6));
        }
        link_session_close(s);
        (void)system_remove_at(AT_FDCWD, "/root/refused", 0);

        //      A pushed file's disk is asked to begin on what has been
        //      written every eight megabytes, so the fsync that ends the push
        //      is not the one place the loop waits for all of it.
        {
                static p8 body[LINK_CHUNK + 1];
                positive frames = (positive)((8ull << 20) / LINK_CHUNK);
                p8 early = true;

                memory_fill(body, 'z', sizeof body);
                body[0] = LINK_DATA;
                check("a push opens to be flushed", wls_push(s, "/root/flushed"));
                for (positive at = 0; at < frames / 2; at++)
                        (void)link_stream_take(s, s->writes, body, sizeof body);
                early = !s->writes[0].started;
                for (positive at = frames / 2; at < frames + 1; at++)
                        (void)link_stream_take(s, s->writes, body, sizeof body);
                check("a transfer's file is flushed by the eight megabytes, "
                      "and not before",
                      early && s->writes[0].started >= (8ull << 20) &&
                              !s->failed);
                link_session_close(s);
        }
}

/*
        What the machine says of a request. A session that has answered, or
        refused, takes no second request. A pulled file's mode goes with the
        yes, to the bits its owner sets, and the client keeps that and no
        more.
*/
static fn request_answers(void)
{
        struct link_session address_to s = link_self.session;
        struct waterlink_frame head = {0};
        p8 answer[5];
        p8 got[8];
        file_facts facts;
        p32 mode = 0;
        p8 key[32];

        wls_seeded(key, 32, 9);
        wls_peers_with(key, WATERLINK_MAY_FILES);
        (void)wls_write("/root/ordinary", "y", 1, 0600);
        check("sec: a request after the session said it is over is not taken",
              !wls_file_over(s, LINK_ASK_PUSH, "/root/ordinary", true) &&
                      s->kind == LINK_KIND_NONE && s->writes[0].fd < 0);
        link_session_close(s);
        check("and the same request in a session that has said nothing is",
              wls_file_over(s, LINK_ASK_PUSH, "/root/ordinary", false) &&
                      s->writes[0].fd >= 0);
        link_session_close(s);

        (void)wls_write("/root/private", "p", 1, 0640);
        if (file_look_at("/root/private", address_of facts))
                mode = facts.mode & 0777;
        check("a pull of a file is taken up", wls_file_asked(s, LINK_ASK_PULL,
                                                             "/root/private"));
        memory_zero(answer, sizeof answer);
        check("sec: and its yes carries the file's mode",
              wls_answer(s, got) == 5 && got[0] == 'O' &&
                      memory_load_unaligned(p32, got + 1) == mode && mode);
        link_session_close(s);
        check("a pull of a file that is not there is taken up as it always "
              "was",
              wls_file_asked(s, LINK_ASK_PULL, "/root/no-such-file"));
        check("and its yes carries no mode: there is none",
              wls_answer(s, got) == 1 && got[0] == 'O');
        link_session_close(s);

        //      The client's side of the yes.
        head.key = LINK_KEY_ANSWER;
        head.length = 5;
        answer[0] = 'O';
        answer[1] = 0xed; // 04755: the setuid bit is not taken
        answer[2] = 0x09;
        answer[3] = answer[4] = 0;
        check("a client's session opens", link_session_open(s));
        s->kind = LINK_KIND_PULL;
        s->mode = 0644;
        (void)link_hear(s, &head, answer);
        check("sec: a pull's yes sets the mode of what is pulled, without "
              "the bits that make a file run as somebody",
              s->answered && s->mode == 0755);
        link_session_close(s);
        check("a client's session opens again", link_session_open(s));
        s->kind = LINK_KIND_RUN;
        s->mode = 0644;
        (void)link_hear(s, &head, answer);
        check("and a yes with a mode to a command that asked for none is "
              "not an answer",
              !s->answered && s->mode == 0644);
        link_session_close(s);
        (void)system_remove_at(AT_FDCWD, "/root/ordinary", 0);
        (void)system_remove_at(AT_FDCWD, "/root/private", 0);
}

/*
        The kernel says how many events it binds, and moonwater bind asks that
        many: a count past the names the shell knows is cut to them, and one
        under them is kept, so that a kernel that offers more can never have
        the walk go past its own table. (Here, with the rest of the shell
        compiled in; the storage_format section, which holds the other host
        checks, calls host_settings_apply with three arguments and does not
        build on this tree.)
*/
static fn bind_events(void)
{
        check("the bind count the kernel offers is kept where it is under the "
              "events this knows",
              host_bind_known(1) == 1 &&
                      host_bind_known(SPARK_BIND_EVENTS) == SPARK_BIND_EVENTS &&
                      host_bind_known(SPARK_BIND_EVENTS - 1) ==
                              SPARK_BIND_EVENTS - 1);
        check("and is cut to them where it is over",
              host_bind_known(SPARK_BIND_EVENTS + 1) == SPARK_BIND_EVENTS &&
                      host_bind_known(0xffffffffu) == SPARK_BIND_EVENTS);
}

//      The words that name a machine, and the terminal a client names.
static fn names_and_words(void)
{
        p8 into[48];

        check("a name is letters, digits, dot, dash and underscore, starting "
              "with a letter or digit",
              link_name_good("box-1") && link_name_good("a.b_c") &&
                      link_name_good("9lives") && !link_name_good("") &&
                      !link_name_good("-a") && !link_name_good(".a") &&
                      !link_name_good("_a") && !link_name_good("a b") &&
                      !link_name_good("a\x01") && !link_name_good("a/b") &&
                      !link_name_good("caf\xc3\xa9"));
        check("and is not one of the words link takes in its place",
              !link_name_good("run") && !link_name_good("on") &&
                      link_name_good("running"));
        {
                p8 refused = !link_name_good("group") &&
                               !link_name_good("leave") &&
                               !link_name_good("help");

                //      Every verb that takes a fixed number of words is a row
                //      of link_shapes: a verb added there and not to
                //      link_words is a machine nobody can reach by its name.
                for (positive at = 0; at < array_count(link_shapes); at++)
                        refused &= !link_name_good(link_shapes[at].word);
                check("and none of the verbs link has, port among them",
                      refused);
                check("nor discovery or network, which were the link's verbs and "
                      "are the network's",
                      !link_name_good("discovery") && !link_name_good("network") &&
                              link_name_good("networks"));
        }
        check("a name of 31 is one and of 32 is not",
              link_name_good("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa") &&
                      !link_name_good("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"));
        link_term_word((p8 address_to) "xterm-256color", 14, into);
        check("a terminal type is its name", !string_compare((string_address)into,
                                                              "xterm-256color"));
        link_term_word((p8 address_to) "vt+x_y.z", 8, into);
        check("with a plus, a dot and an underscore",
              !string_compare((string_address)into, "vt+x_y.z"));
        link_term_word((p8 address_to) "a b", 3, into);
        check("sec: one with anything a terminfo name is not is xterm",
              !string_compare((string_address)into, "xterm"));
        link_term_word((p8 address_to) "ab\0cd", 5, into);
        check("sec: and an end inside it is not an end of it",
              !string_compare((string_address)into, "xterm"));
        link_term_word((p8 address_to) "../x", 4, into);
        check("sec: nor is a path", !string_compare((string_address)into, "xterm"));
        link_term_word((p8 address_to) "", 0, into);
        check("an empty one is xterm", !string_compare((string_address)into, "xterm"));
}

/*
        The control messages a receive reports: the data of the one asked for,
        among the bytes the kernel filled and not the room the buffer has.
*/
static fn control_messages(void)
{
        p8 control[64];
        p64 size = 20;
        b32 has[2] = {SOL_UDP, UDP_GRO};
        b32 value = 1200;

        memory_fill(control, 0, sizeof control);
        memory_copy(control, address_of size, 8);
        memory_copy(control + 8, has, 8);
        memory_copy(control + 16, address_of value, 4);
        check("a message that was filled whole gives its int",
              link_control_int(control, 24, sizeof control, SOL_UDP,
                               UDP_GRO) == 1200 &&
                      link_control_int(control, 20, sizeof control, SOL_UDP,
                                       UDP_GRO) == 1200);
        check("sec: one that claims more than the kernel filled is not read, "
              "though the buffer has room for it",
              link_control_int(control, 16, sizeof control, SOL_UDP,
                               UDP_GRO) == -1 &&
                      link_control_int(control, 19, sizeof control, SOL_UDP,
                                       UDP_GRO) == -1);
        check("sec: nor is what the buffer holds past its room",
              link_control_int(control, 24, 18, SOL_UDP, UDP_GRO) == -1);
        check("and a level or a type that is not asked for is not read",
              link_control_int(control, 24, sizeof control, SOL_UDP,
                               UDP_SEGMENT) == -1 &&
                      link_control_int(control, 24, sizeof control, IPPROTO_IP,
                                       UDP_GRO) == -1);

        //      After another message, padded to its eight.
        memory_fill(control, 0, sizeof control);
        size = 20;
        has[0] = IPPROTO_IP;
        has[1] = IP_TTL;
        value = 255;
        memory_copy(control, address_of size, 8);
        memory_copy(control + 8, has, 8);
        memory_copy(control + 16, address_of value, 4);
        size = 20;
        has[0] = SOL_UDP;
        has[1] = UDP_GRO;
        value = 1200;
        memory_copy(control + 24, address_of size, 8);
        memory_copy(control + 32, has, 8);
        memory_copy(control + 40, address_of value, 4);
        check("the one asked for is found after another, past its padding",
              link_control_int(control, 48, sizeof control, SOL_UDP,
                               UDP_GRO) == 1200 &&
                      link_control_int(control, 48, sizeof control, IPPROTO_IP,
                                       IP_TTL) == 255);
        check("sec: and a second message the filled length cuts short is not",
              link_control_int(control, 40, sizeof control, SOL_UDP,
                               UDP_GRO) == -1);
        size = 8; // smaller than its own header: nothing after it is a message
        memory_copy(control, address_of size, 8);
        check("sec: a message too small to be one ends the walk",
              link_control_int(control, 48, sizeof control, SOL_UDP,
                               UDP_GRO) == -1);
}

/*
        State files are read nonblocking and taken only when they are plain
        files, so a FIFO at the name is refused where it was waited on: each
        is read in a child, which must come back, and refused.
*/
static fn private_records_never_wait(void)
{
        static const string_address names[] = {
            LINK_KEY_PATH, LINK_PEERS_PATH, LINK_GROUPS_PATH, LINK_STAMPS_PATH,
            LINK_STATE_PATH};
        p8 refused = true;
        p8 quick = true;

        for (positive at = 0; at < array_count(names); at++)
        {
                bipolar child;
                positive status = 0;
                p8 done = false;

                (void)system_remove_at(AT_FDCWD, names[at], 0);
                if (system_call_4(syscall(mknodat), (positive)(bipolar)AT_FDCWD,
                                  (positive)names[at], 0010600, 0) < 0)
                {
                        refused = false;
                        continue;
                }
                child = system_fork();
                if (!child)
                {
                        p8 into[32];
                        positive got = 0;

                        exit(link_read_private_records(names[at], into,
                                                       sizeof into, 32,
                                                       address_of got) == -EPERM
                                     ? 0
                                     : 1);
                }
                for (positive tick = 0; child > 0 && tick < 100; tick++)
                {
                        timespec pause = {0, 20000000};

                        if (system_wait4_retry(child, address_of status, 1,
                                               null) == child)
                        {
                                done = true;
                                refused &= wait_status_code(status) == 0;
                                break;
                        }
                        (void)system_call_2(syscall(nanosleep),
                                            (positive)address_of pause, 0);
                }
                if (!done && child > 0)
                {
                        (void)system_call_2(syscall(kill), (positive)child, 9);
                        (void)system_wait4_retry(child, address_of status, 0,
                                                 null);
                }
                quick &= done;
                (void)system_remove_at(AT_FDCWD, names[at], 0);
        }
        check("sec: a FIFO at the key, the machines, the groups, the stamps "
              "or the state is read as nothing, and not waited on",
              quick);
        check("and refused as not a private file", refused);
}

/*
        The responder: an initiation is answered, and one it cannot answer
        does not hold a session.
*/
static struct waterlink_identity wls_server, wls_client, wls_b;

static fn wls_initiation_as(struct waterlink_identity address_to who,
                            p8 address_to datagram, p64 conversation,
                            p64 stamp_seconds, p32 index)
{
        struct waterlink_noise noise;
        p8 hello[WATERLINK_HELLO_BYTES];
        p8 ephemeral[32];

        waterlink_stamp(hello, stamp_seconds, 0);
        memory_copy(hello + WATERLINK_STAMP_BYTES, address_of conversation, 8);
        memory_copy(hello + WATERLINK_STAMP_BYTES + 8, address_of index, 4);
        wls_seeded(ephemeral, 32, (p8)conversation);
        (void)waterlink_initiate(address_of noise, who, wls_server.public, null,
                                 ephemeral, hello, datagram);
}

static fn wls_initiation(p8 address_to datagram, p64 conversation,
                         p64 stamp_seconds, p32 index)
{
        wls_initiation_as(address_of wls_client, datagram, conversation,
                          stamp_seconds, index);
}

static fn responder(bipolar listener, p16 port)
{
        p8 datagram[WATERLINK_DATAGRAM];
        p8 answer[WATERLINK_DATAGRAM + 16];

        link_self.me = wls_server;
        wls_peers_with(wls_client.public, WATERLINK_MAY_DEFAULT);
        memory_zero(address_of link_self.admission, sizeof link_self.admission);
        wls_drain(listener);

        {
                positive stamps = link_self.stamps;

                wls_initiation(datagram, 10, 999, 0);
                link_server_initiation(datagram, WATERLINK_DATAGRAM,
                                       wls_loopback, port, 900000);
                check("sec: an initiation with the reserved zero index holds "
                      "no session and is not answered",
                      wls_sessions_used() == 0 &&
                              wls_heard(listener, answer) <= 0);
                check("sec: a zero-index initiation spends no replay stamp",
                      link_self.stamps == stamps);
        }

        entropy_down = true;
        wls_initiation(datagram, 11, 1000, 0x01020304);
        link_server_initiation(datagram, WATERLINK_DATAGRAM, wls_loopback, port,
                               1000000);
        entropy_down = false;
        check("sec: with no entropy an initiation holds no session",
              wls_sessions_used() == 0);
        check("sec: and is not answered", wls_heard(listener, answer) <= 0);

        /* The same authenticated datagram is what a client retransmits when
           no answer arrived. A local resource failure must not consume its
           replay stamp. */
        link_server_initiation(datagram, WATERLINK_DATAGRAM, wls_loopback, port,
                               2000000);
        check("sec: after entropy returns the same initiation is answered",
              wls_heard(listener, answer) == WATERLINK_DATAGRAM &&
                      wls_sessions_used() == 1);

        /* `leave --forget` removes the authorization record while a listener
           and its sessions may still be live.  The peer's next authenticated
           rekey must revoke the old keys, not merely refuse to replace them. */
        (void)system_remove_at(AT_FDCWD, LINK_PEERS_PATH, 0);
        wls_initiation(datagram, 12, 1001, 0x01020305);
        link_server_initiation(datagram, WATERLINK_DATAGRAM, wls_loopback, port,
                               3000000);
        {
                struct link_session address_to s = link_self.session;
                struct link_session address_to found;

                check("sec: forgetting makes every traffic-key epoch unaddressable "
                      "before queued carry datagrams are dispatched",
                      s->finished && !link_keys_for(s->now.ours, address_of found) &&
                              !link_keys_for(s->next.ours, address_of found) &&
                              !link_keys_for(s->before.ours, address_of found));
        }
        link_self.server = true;
        (void)link_sessions_turn(3000001);
        link_self.server = false;
        {
                bipolar heard;
                p8 answered = false;

                //      Told the session is closed, and nothing else.
                while ((heard = wls_heard(listener, answer)) > 0)
                        answered |= heard == WATERLINK_DATAGRAM;
                check("sec: a forgotten peer's authenticated rekey closes its "
                      "live session and is not answered",
                      wls_sessions_used() == 0 && !answered);
        }

        for (positive at = 0; at < LINK_SESSIONS; at++)
                if (link_self.session[at].used)
                        link_session_close(link_self.session + at);
}

/*
        The table of sessions has places for sixteen, and one peer may hold
        eight of them: a peer that opens a session for every initiation it
        can make fills what it may and no more, a second fills the rest, and
        a third that is paired finds the table full -- nothing is opened past
        its end.
*/
static fn session_caps(bipolar listener, p16 port)
{
        struct waterlink_identity third;
        struct waterlink_identity address_to who[3] = {
            address_of wls_client, address_of wls_b, address_of third};
        struct waterlink_peer peers[3];
        p8 datagram[WATERLINK_DATAGRAM];
        positive kept[3] = {0, 0, 0};

        wls_identity(address_of third, 84);
        memory_zero(peers, sizeof peers);
        for (positive at = 0; at < 3; at++)
        {
                memory_copy(peers[at].key, who[at]->public, 32);
                peers[at].name[0] = (p8)('x' + at);
                peers[at].may = WATERLINK_MAY_DEFAULT;
        }
        (void)wls_write(LINK_PEERS_PATH, peers, sizeof peers, 0600);
        link_self.me = wls_server;
        memory_zero(address_of link_self.admission, sizeof link_self.admission);
        link_self.stamps = 0;
        wls_drain(listener);

        for (positive at = 0; at < 10; at++)
        {
                wls_initiation_as(who[0], datagram, 300 + at, 40000 + at,
                                  0x30000 + (p32)at);
                link_server_initiation(datagram, WATERLINK_DATAGRAM,
                                       wls_loopback, port, 70000000 + at);
        }
        check("sec: one peer holds at most eight sessions, however many "
              "initiations it makes",
              wls_sessions_used() == LINK_SESSIONS_A_PEER);
        for (positive at = 0; at < 10; at++)
        {
                wls_initiation_as(who[1], datagram, 400 + at, 41000 + at,
                                  0x40000 + (p32)at);
                link_server_initiation(datagram, WATERLINK_DATAGRAM,
                                       wls_loopback, port, 70000100 + at);
        }
        check("sec: a second peer takes the rest", wls_sessions_used() ==
                                                       LINK_SESSIONS);
        wls_initiation_as(who[2], datagram, 500, 42000, 0x50000);
        link_server_initiation(datagram, WATERLINK_DATAGRAM, wls_loopback, port,
                               70000200);
        for (positive at = 0; at < LINK_SESSIONS; at++)
                for (positive look = 0; look < 3; look++)
                        if (link_self.session[at].used &&
                            crypto_same(link_self.session[at].peer,
                                        who[look]->public, 32))
                                kept[look]++;
        check("sec: and a third, paired, finds the table full and holds "
              "nothing",
              wls_sessions_used() == LINK_SESSIONS && kept[0] == 8 &&
                      kept[1] == 8 && !kept[2]);
        for (positive at = 0; at < LINK_SESSIONS; at++)
                if (link_self.session[at].used)
                        link_session_close(link_self.session + at);
        wls_drain(listener);
}

/*
        Admission under a flood. A sender that knows this machine's key makes
        good mac1s from addresses it makes up; past WATERLINK_ADMIT_LOADED a
        second the listener asks for mac2, answers an initiation without it
        with a cookie reply, and spends none of the curve's bucket on it --
        so a paired client, told its cookie, still gets in. The cookie is
        bound to the address and port it was handed to, and is good for two
        minutes of the listener's secret.
*/
static fn wls_flood(positive count, p64 now)
{
        p8 datagram[WATERLINK_DATAGRAM];
        p8 hello[WATERLINK_HELLO_BYTES];
        p8 ephemeral[32];
        p8 somewhere[16] = {0x20, 0x01, 0x0d, 0xb8};
        struct waterlink_noise noise;

        for (positive at = 0; at < count; at++)
        {
                memory_zero(hello, sizeof hello);
                waterlink_stamp(hello, 7000 + at, 0);
                wls_seeded(ephemeral, 32, (p8)(at + 7));
                (void)waterlink_initiate(address_of noise, address_of wls_b,
                                         wls_server.public, null, ephemeral,
                                         hello, datagram);
                somewhere[14] = (p8)(at >> 8);
                somewhere[15] = (p8)at;
                link_server_initiation(datagram, WATERLINK_DATAGRAM, somewhere,
                                       (p16)(1000 + at), now);
        }
}

static fn cookies(bipolar listener, p16 port)
{
        p8 datagram[WATERLINK_DATAGRAM];
        p8 heard[WATERLINK_DATAGRAM + 16];
        p8 mac1[16], cookie[16], again[16];
        p64 now = 50000000;
        p32 tokens;
        p64 handed;
        positive refused = 0;

        link_self.me = wls_server;
        wls_peers_with(wls_client.public, WATERLINK_MAY_DEFAULT);
        memory_zero(address_of link_self.admission, sizeof link_self.admission);
        link_self.stamps = 0;
        wls_drain(listener);

        wls_flood(WATERLINK_ADMIT_LOADED, now);
        tokens = link_self.admission.all.tokens;
        wls_flood(200, now);
        check("sec: a flood of initiations past the load spends none of the "
              "curve's bucket",
              link_self.admission.all.tokens == tokens && tokens &&
                      link_self.admission.cookies == 200);

        wls_initiation(datagram, 21, 8000, 0x0badcafe);
        memory_copy(mac1, datagram + 16 + WATERLINK_INITIATE_BYTES - 16, 16);
        link_server_initiation(datagram, WATERLINK_DATAGRAM, wls_loopback, port,
                               now);
        check("sec: under it a paired client's initiation is answered with a "
              "cookie reply, not a session",
              wls_heard(listener, heard) == WATERLINK_COOKIE_DATAGRAM &&
                      heard[0] == WATERLINK_KIND_COOKIE &&
                      wls_sessions_used() == 0);
        check("and the client opens the cookie, sealed to its mac1",
              waterlink_cookie_take(wls_server.public, mac1, heard,
                                    WATERLINK_COOKIE_DATAGRAM, cookie));
        mac1[3] ^= 1;
        check("sec: but not as the answer to another initiation",
              !waterlink_cookie_take(wls_server.public, mac1, heard,
                                     WATERLINK_COOKIE_DATAGRAM, again));
        mac1[3] ^= 1;
        for (positive length = 0; length <= WATERLINK_DATAGRAM; length++)
                refused += length != WATERLINK_COOKIE_DATAGRAM &&
                           !waterlink_cookie_take(wls_server.public, mac1, heard,
                                                  length, again);
        check("sec: nor at any other length",
              refused == WATERLINK_DATAGRAM);

        //      From another port the cookie is not the one it was given.
        wls_initiation(datagram, 22, 8001, 0x0badcaff);
        waterlink_mac2(cookie, datagram);
        handed = link_self.admission.cookies;
        link_server_initiation(datagram, WATERLINK_DATAGRAM, wls_loopback,
                               (p16)(port + 1), now);
        check("sec: a cookie from another port is asked for again",
              link_self.admission.cookies == handed + 1 &&
                      wls_sessions_used() == 0);

        wls_initiation(datagram, 21, 8002, 0x0badcafe);
        waterlink_mac2(cookie, datagram);
        link_server_initiation(datagram, WATERLINK_DATAGRAM, wls_loopback, port,
                               now);
        check("sec: the client asking again under its cookie is answered and "
              "keyed, the flood notwithstanding",
              wls_heard(listener, heard) == WATERLINK_DATAGRAM &&
                      heard[0] == WATERLINK_KIND_RESPOND &&
                      wls_sessions_used() == 1);

        //      Two minutes on, the secret is another, and so is the cookie.
        now += (p64)WATERLINK_COOKIE_SECONDS * 1000000 + 1000000;
        wls_flood(WATERLINK_ADMIT_LOADED + 8, now);
        wls_drain(listener);
        wls_initiation(datagram, 23, 8003, 0x0badcb00);
        memory_copy(mac1, datagram + 16 + WATERLINK_INITIATE_BYTES - 16, 16);
        waterlink_mac2(cookie, datagram);
        link_server_initiation(datagram, WATERLINK_DATAGRAM, wls_loopback, port,
                               now);
        check("sec: a cookie older than the secret's two minutes is asked for "
              "again, and the new one differs",
              wls_heard(listener, heard) == WATERLINK_COOKIE_DATAGRAM &&
                      waterlink_cookie_take(wls_server.public, mac1, heard,
                                            WATERLINK_COOKIE_DATAGRAM, again) &&
                      memory_compare(again, cookie, 16) &&
                      wls_sessions_used() == 1);

        entropy_down = true;
        now += (p64)WATERLINK_COOKIE_SECONDS * 1000000 + 1000000;
        wls_flood(WATERLINK_ADMIT_LOADED + 8, now);
        handed = link_self.admission.cookies;
        wls_initiation(datagram, 24, 8004, 0x0badcb01);
        waterlink_mac2(again, datagram);
        link_server_initiation(datagram, WATERLINK_DATAGRAM, wls_loopback, port,
                               now);
        entropy_down = false;
        check("sec: a secret that cannot be made again is not stretched: "
              "under load nothing is admitted or handed out",
              link_self.admission.cookies == handed && wls_sessions_used() == 1);

        for (positive at = 0; at < LINK_SESSIONS; at++)
                if (link_self.session[at].used)
                        link_session_close(link_self.session + at);
}

/*
        The replay markers across a restart: an initiation taken, the
        listener stopped and started again, and the same datagram played
        back holds no session and draws no answer; a newer one from the
        same peer is answered.
*/
static fn stamps_outlive(bipolar listener, p16 port)
{
        p8 datagram[WATERLINK_DATAGRAM];
        p8 newer[WATERLINK_DATAGRAM];
        p8 heard[WATERLINK_DATAGRAM + 16];
        p64 now = 400000000;

        link_self.me = wls_server;
        link_self.server = true;
        wls_peers_with(wls_client.public, WATERLINK_MAY_DEFAULT);
        memory_zero(address_of link_self.admission, sizeof link_self.admission);
        link_self.stamps = 0;
        (void)system_remove_at(AT_FDCWD, LINK_STAMPS_PATH, 0);
        wls_drain(listener);

        wls_initiation(datagram, 31, 9000, 0x31313131);
        wls_initiation(newer, 32, 9001, 0x32323232);
        link_server_initiation(datagram, WATERLINK_DATAGRAM, wls_loopback, port,
                               now);
        link_stamps_save();
        check("an initiation is answered, and its marker written",
              wls_heard(listener, heard) == WATERLINK_DATAGRAM &&
                      wls_sessions_used() == 1 && !link_self.stamps_dirty);

        //      A restart: nothing of the listener is left but its files.
        for (positive at = 0; at < LINK_SESSIONS; at++)
                if (link_self.session[at].used)
                        link_session_close(link_self.session + at);
        crypto_forget(link_self.stamp, sizeof link_self.stamp);
        link_self.stamps = 0;
        link_stamps_load();

        link_server_initiation(datagram, WATERLINK_DATAGRAM, wls_loopback, port,
                               now + 1000000);
        check("sec: after a restart a recorded initiation played back holds "
              "no session and draws no answer",
              wls_heard(listener, heard) <= 0 && wls_sessions_used() == 0);
        link_server_initiation(newer, WATERLINK_DATAGRAM, wls_loopback, port,
                               now + 2000000);
        check("and a newer one from the same peer is answered",
              wls_heard(listener, heard) == WATERLINK_DATAGRAM &&
                      wls_sessions_used() == 1);
        for (positive at = 0; at < LINK_SESSIONS; at++)
                if (link_self.session[at].used)
                        link_session_close(link_self.session + at);

        //      A peer whose clock once ran a century ahead, now put right.
        {
                p8 future[WATERLINK_STAMP_BYTES];
                p64 wall = system_clock_ns(0) / 1000000000ull;

                waterlink_stamp(future, wall + 100ull * 365 * 86400, 0);
                link_stamp_keep(wls_client.public, future);
                link_stamps_save();
                crypto_forget(link_self.stamp, sizeof link_self.stamp);
                link_self.stamps = 0;
                link_stamps_load();
                wls_initiation(datagram, 33, wall, 0x33333333);
                link_server_initiation(datagram, WATERLINK_DATAGRAM,
                                       wls_loopback, port, now + 3000000);
                check("sec: a marker dated far ahead of this machine's clock "
                      "is not read back to lock its peer out",
                      wls_heard(listener, heard) == WATERLINK_DATAGRAM &&
                              wls_sessions_used() == 1);
                link_server_initiation(newer, WATERLINK_DATAGRAM, wls_loopback,
                                       port, now + 4000000);
                check("while a replay from the past stays refused",
                      wls_heard(listener, heard) <= 0);
        }
        link_stamps_save();
        for (positive at = 0; at < LINK_SESSIONS; at++)
                if (link_self.session[at].used)
                        link_session_close(link_self.session + at);
}

/*
        The client's half: a cookie reply to the initiation it has out is
        kept and asked again under at once, and the next initiation carries
        mac2 by it; a second reply, to an initiation that carried one, does
        not make it ask again before its time.
*/
static fn client_cookie(bipolar listener, p16 port)
{
        struct link_session address_to s = link_self.session;
        struct waterlink_admission table;
        p8 heard[WATERLINK_DATAGRAM + 16];
        p8 reply[WATERLINK_COOKIE_DATAGRAM];
        p8 nonce[24];
        p8 cookie[16];
        p64 now = 90000000;

        memory_zero(address_of table, sizeof table);
        wls_seeded(table.secret, 32, 91);
        table.secret_made = now;
        wls_seeded(nonce, 24, 92);
        link_self.me = wls_client;
        link_self.server = false;
        memory_zero(address_of link_client, sizeof link_client);
        wls_drain(listener);
        check("a client session opens for the cookie", link_session_open(s));
        memory_copy(s->peer, wls_server.public, 32);
        memory_copy(s->address, wls_loopback, 16);
        s->port = port;

        /* Pin every construction boundary separately.  These calls used to
           copy mac1 from the stack even when no datagram had been built. */
        entropy_fail_length = 32;
        memory_fill(link_client.mac1, 0xa5, sizeof link_client.mac1);
        check("sec: client initiation fails closed when ephemeral entropy "
              "fails after its index was made",
              !link_client_initiate(s, now - 4) && !link_client.ours &&
                      memory_span_byte(link_client.mac1, 0,
                                       sizeof link_client.mac1) ==
                              sizeof link_client.mac1 &&
                      wls_heard(listener, heard) <= 0);
        entropy_fail_length = 0;
        memory_zero(s->peer, 32);
        memory_fill(link_client.mac1, 0xa5, sizeof link_client.mac1);
        check("sec: client initiation retains no MAC when the peer key is "
              "refused before a datagram is built",
              !link_client_initiate(s, now - 3) && !link_client.ours &&
                      memory_span_byte(link_client.mac1, 0,
                                       sizeof link_client.mac1) ==
                              sizeof link_client.mac1 &&
                      wls_heard(listener, heard) <= 0);
        memory_copy(s->peer, wls_server.public, 32);
        {
                p8 failed[WATERLINK_DATAGRAM] = {0};
                p16 working_port = s->port;

                s->port = 0;
                check("sec: a locally refused initiation leaves no active "
                      "index",
                      !link_client_initiate(s, now - 2) && !link_client.ours);
                memory_copy(failed + 16 + WATERLINK_INITIATE_BYTES - 16,
                            link_client.mac1, sizeof link_client.mac1);
                waterlink_cookie_reply(address_of wls_server, address_of table,
                                       failed, wls_loopback, 0, nonce,
                                       reply);
                link_client.cookie_at = 0;
                link_client_cookie(reply, sizeof reply, now - 1);
                check("sec: a cookie after a locally refused initiation is "
                      "ignored",
                      !link_client.cookie_at);
                s->port = working_port;
        }

        check("the client's first initiation goes out without mac2",
              link_client_initiate(s, now) &&
                      wls_heard(listener, heard) == WATERLINK_DATAGRAM &&
                      memory_span_byte(heard + 16 + WATERLINK_INITIATE_BYTES, 0,
                                       16) == 16);
        waterlink_cookie_reply(address_of wls_server, address_of table, heard,
                               wls_loopback, port, nonce, reply);
        link_datagram(reply, sizeof reply, wls_loopback, port, now + 10);
        check("sec: a cookie reply to it is kept, and asks again at once",
              link_client.cookie_at == now + 10 &&
                      link_age(now + 10, link_client.initiated) > LINK_ATTEMPT);
        check("and the next initiation carries mac2 by that cookie",
              link_client_initiate(s, now + 20) &&
                      wls_heard(listener, heard) == WATERLINK_DATAGRAM &&
                      link_client.cookied &&
                      waterlink_cookie_take(wls_server.public,
                                            link_client.mac1, reply, sizeof reply,
                                            cookie) == false);
        waterlink_cookie_of(address_of table, wls_loopback, port, cookie);
        {
                p8 copy[WATERLINK_DATAGRAM];

                memory_copy(copy, heard, WATERLINK_DATAGRAM);
                memory_zero(copy + 16 + WATERLINK_INITIATE_BYTES, 16);
                waterlink_mac2(cookie, copy);
                check("sec: which the listener's cookie for this place makes",
                      !memory_compare(copy, heard, WATERLINK_DATAGRAM));
        }
        waterlink_cookie_reply(address_of wls_server, address_of table, heard,
                               wls_loopback, port, nonce, reply);
        link_datagram(reply, sizeof reply, wls_loopback, port, now + 30);
        check("sec: a reply to an initiation that carried a cookie does not "
              "hurry the next",
              link_client.initiated == now + 20);
        link_session_close(s);
        crypto_forget(address_of link_client, sizeof link_client);
}

/*
        The initiator: a forged answer is judged on a copy and leaves the
        transcript for the real one.
*/
static fn initiator_answer(void)
{
        struct link_session address_to s = link_self.session;
        struct waterlink_noise live, before, answering;
        p8 first[WATERLINK_DATAGRAM], second[WATERLINK_DATAGRAM],
                forged[WATERLINK_DATAGRAM];
        p8 who[32], hello[WATERLINK_HELLO_BYTES], heard[WATERLINK_HELLO_BYTES];
        p8 e1[32], e2[32], gate[32];
        p32 ours = 0x0a0b0c0d;

        link_self.me = wls_client;
        check("a client session opens", link_session_open(s));
        memory_copy(s->peer, wls_server.public, 32);
        wls_seeded(e1, 32, 41);
        wls_seeded(e2, 32, 42);
        waterlink_stamp(hello, 5000, 0);
        memory_copy(hello + WATERLINK_STAMP_BYTES + 8, address_of ours, 4);
        (void)waterlink_initiate(address_of live, address_of wls_client,
                                 wls_server.public, null, e1, hello, first);
        (void)waterlink_accept(address_of answering, address_of wls_server, null, first,
                               who, heard);
        (void)waterlink_respond(address_of answering, e2, ours, 0x55667788,
                                second);

        {
                struct link_session session_before = *s;
                positive refused = 0;
                positive unchanged = 0;

                for (positive length = 0; length < WATERLINK_DATAGRAM; length++)
                {
                        before = live;
                        refused += !link_client_answer(s, address_of live, ours,
                                                       second, length);
                        unchanged +=
                                !memory_compare(address_of live,
                                                address_of before,
                                                sizeof live) &&
                                !memory_compare(s, address_of session_before,
                                                sizeof session_before);
                }
                before = live;
                refused += !link_client_answer(s, address_of live, ours,
                                               second,
                                               WATERLINK_DATAGRAM + 1);
                unchanged += !memory_compare(address_of live,
                                             address_of before, sizeof live) &&
                             !memory_compare(s, address_of session_before,
                                             sizeof session_before);
                check("sec: every non-exact handshake-answer length is "
                      "refused",
                      refused == WATERLINK_DATAGRAM + 1);
                check("sec: all handshake-answer length refusals are "
                      "state-atomic",
                      unchanged == refused);
        }

        //      The forgery: a different ephemeral, gated correctly, so it
        //      reaches the curve and fails there.
        memory_copy(forged, second, WATERLINK_DATAGRAM);
        forged[16] ^= 0x40;
        waterlink_gate_of(wls_client.public, gate);
        waterlink_mac1(gate, forged, 16 + WATERLINK_RESPOND_BYTES - 16,
                       forged + 16 + WATERLINK_RESPOND_BYTES - 16);
        check("the forged answer still passes the gate",
              waterlink_gate_passes(address_of wls_client, forged,
                                    WATERLINK_DATAGRAM));

        before = live;
        check("sec: a forged answer is refused",
              !link_client_answer(s, address_of live, ours, forged,
                                  WATERLINK_DATAGRAM));
        check("sec: and leaves the live transcript as it was",
              !memory_compare(address_of live, address_of before, sizeof live));
        check("sec: so the real answer after it still keys the session",
              link_client_answer(s, address_of live, ours, second,
                                 WATERLINK_DATAGRAM) &&
                      s->now.live);
        link_session_close(s);
}

/* A tag proves who sent bytes, not that those bytes are a Waterlink body.
   Malformed authenticated traffic must spend neither its counter nor its
   claimed roaming address before the body grammar accepts it. */
static fn carried_is_atomic(void)
{
        struct link_session address_to s = link_self.session;
        struct link_session before;
        struct waterlink_link link_before;
        struct waterlink_datagram head = {WATERLINK_KIND_CARRY, 0x10203040, 1};
        crypto_aesgcm_key sealing_key;
        p8 raw[16];
        p8 datagram[WATERLINK_DATAGRAM];
        p8 from[16];
        static const p8 malformed[] = {
            WATERLINK_FRAME_DURABLE, 7, 1, 1, 'x',
            WATERLINK_FRAME_DURABLE, 7, 2, 9, 'y'};
        positive length;
        p64 now;

        check("a session for authenticated-body atomicity opens",
              link_session_open(s));
        wls_seeded(raw, sizeof raw, 91);
        link_keys_install(address_of s->now, raw, raw, head.receiver,
                          0x50607080);
        now = link_now();
        crypto_aesgcm_prepare(address_of sealing_key, raw);
        memory_copy(from, wls_loopback, sizeof from);
        from[15] = 2;

        {
                p8 sealed[64];
                positive sealed_length;
                positive refused = 0;
                positive unchanged = 0;

                memory_copy(sealed, address_of head, sizeof head);
                sealed_length = waterlink_seal(address_of sealing_key, sealed,
                                                0);
                for (positive cut = 0; cut < 48; cut++)
                {
                        before = *s;
                        link_before = *s->link;
                        refused += !link_carried(sealed, cut, from, 1234, now,
                                                 null);
                        unchanged +=
                                !memory_compare(s, address_of before,
                                                sizeof before) &&
                                !memory_compare(s->link,
                                                address_of link_before,
                                                sizeof link_before);
                }
                for (positive bit = 0; bit < sealed_length * 8; bit++)
                {
                        memory_copy(datagram, sealed, sealed_length);
                        datagram[bit / 8] ^= (p8)(1u << (bit % 8));
                        before = *s;
                        link_before = *s->link;
                        refused += !link_carried(datagram, sealed_length, from,
                                                 1234, now, null);
                        unchanged +=
                                !memory_compare(s, address_of before,
                                                sizeof before) &&
                                !memory_compare(s->link,
                                                address_of link_before,
                                                sizeof link_before);
                }
                check("sec: every short length and every one-bit authenticated "
                      "datagram mutation is refused",
                      refused == 48 + sealed_length * 8);
                check("sec: all of those carriage refusals are state-atomic",
                      unchanged == refused);
        }

        memory_copy(datagram, address_of head, sizeof head);
        memory_copy(datagram + 16, malformed, sizeof malformed);
        length = waterlink_seal(address_of sealing_key, datagram,
                                sizeof malformed);
        link_self.state_dirty = false;
        before = *s;
        link_before = *s->link;

        check("sec: an authenticated malformed carried body is refused",
              !link_carried(datagram, length, from, 1234, now, null));
        check("sec: it changes no session, replay, roaming or link state",
              !memory_compare(s, address_of before, sizeof before) &&
                      !memory_compare(s->link, address_of link_before,
                                      sizeof link_before) &&
                      !link_self.state_dirty);

        /* Seal a grammatical keepalive under the very same counter. If the
           malformed datagram spent it, this valid datagram is a replay. */
        memory_copy(datagram, address_of head, sizeof head);
        length = waterlink_seal(address_of sealing_key, datagram, 0);
        check("sec: the malformed datagram did not spend its counter",
              link_carried(datagram, length, from, 1234, now + 1, null));

        {
                struct waterlink_replay replay_before = s->now.replay;
                p64 expired = s->now.made +
                              (p64)WATERLINK_REJECT_SECONDS * 1000000;

                head.counter = 2;
                memory_copy(datagram, address_of head, sizeof head);
                length = waterlink_seal(address_of sealing_key, datagram, 0);
                check("sec: an authenticated datagram under expired keys is "
                      "refused",
                      !link_carried(datagram, length, from, 1234, expired,
                                    null));
                check("sec: expired keys spend no replay counter",
                      !memory_compare(address_of s->now.replay,
                                      address_of replay_before,
                                      sizeof replay_before));
        }
        {
                struct waterlink_replay replay_before = s->now.replay;

                /*      A one-way peer may reach the receiving key's message
                        limit while this machine has sent almost nothing.
                        The peer's authenticated counter, not our unrelated
                        sending counter, must retire the key. */
                s->now.counter = 0;
                head.counter = WATERLINK_REKEY_MESSAGES + (1ull << 20);
                memory_copy(datagram, address_of head, sizeof head);
                length = waterlink_seal(address_of sealing_key, datagram, 0);
                check("sec: a peer cannot exceed a receiving key's message "
                      "limit while our sending counter is low",
                      !link_carried(datagram, length, from, 1234, now + 2,
                                    null));
                check("sec: a message-limit refusal spends no replay counter",
                      !memory_compare(address_of s->now.replay,
                                      address_of replay_before,
                                      sizeof replay_before));
        }
        {
                /*      A receive drain is handed a clock snapshot, while a
                        handshake inside it records a fresh timestamp. Even
                        though the drain now refreshes each turn, keep the age
                        boundary fused too: a carry queued immediately behind
                        the handshake is not 2^64 microseconds old merely
                        because its snapshot is a microsecond earlier. */
                struct waterlink_replay replay_before = s->now.replay;

                s->now.made = now + 100;
                head.counter = 3;
                memory_copy(datagram, address_of head, sizeof head);
                length = waterlink_seal(address_of sealing_key, datagram, 0);
                check("sec: a carry behind its handshake survives a stale "
                      "batch clock snapshot",
                      link_carried(datagram, length, from, 1234, now, null));
                check("and that accepted carry spends exactly its replay slot",
                      memory_compare(address_of s->now.replay,
                                     address_of replay_before,
                                     sizeof replay_before));
        }
        link_session_close(s);
        crypto_forget(address_of sealing_key, sizeof sealing_key);
}

/*
        A carried datagram's length is judged before its tag is read, and a
        length that is not one of this link's is refused whatever the bytes
        say. The oversized one is sealed properly: a close one block past a
        whole datagram, under a tag that verifies, so only its length is
        wrong, and a close would end the session if the length were not
        checked. Every length that is not whole blocks is refused and changes
        nothing, and the same keys still take a whole datagram afterwards.
*/
static fn carried_lengths(void)
{
        struct link_session address_to s = link_self.session;
        struct link_session before;
        struct waterlink_link link_before;
        struct waterlink_datagram head = {WATERLINK_KIND_CLOSE, 0x10203040, 7};
        crypto_aesgcm_key sealing_key;
        p8 raw[16];
        p8 datagram[WATERLINK_DATAGRAM + 64];
        p8 copy[WATERLINK_DATAGRAM + 64];
        p8 from[16];
        positive oversized = WATERLINK_DATAGRAM + 16;
        positive refused = 0;
        positive unchanged = 0;
        positive tried = 0;
        positive length;
        p64 now;

        check("a session for length checks opens", link_session_open(s));
        wls_seeded(raw, sizeof raw, 92);
        link_keys_install(address_of s->now, raw, raw, head.receiver,
                          0x50607080);
        now = link_now();
        crypto_aesgcm_prepare(address_of sealing_key, raw);
        memory_copy(from, wls_loopback, sizeof from);
        from[15] = 2;

        memory_zero(datagram, sizeof datagram);
        memory_copy(datagram, address_of head, sizeof head);
        waterlink_seal_box(address_of sealing_key, datagram, 0, oversized - 32);
        memory_copy(copy, datagram, oversized);
        check("sec: the oversized close's tag verifies, so only its length is wrong",
              waterlink_open_box(address_of sealing_key, copy, oversized - 32));

        before = *s;
        link_before = *s->link;
        check("sec: an authenticated close longer than a datagram is refused",
              !link_carried(datagram, oversized, from, 1234, now, null));
        check("sec: refusing it changes no session, replay or link state",
              !memory_compare(s, address_of before, sizeof before) &&
                      !memory_compare(s->link, address_of link_before,
                                      sizeof link_before));

        for (length = 49; length < sizeof datagram; length++)
        {
                if (length % 16 == 0)
                        continue;
                tried++;
                before = *s;
                link_before = *s->link;
                refused += !link_carried(datagram, length, from, 1234, now, null);
                unchanged += !memory_compare(s, address_of before, sizeof before) &&
                             !memory_compare(s->link, address_of link_before,
                                             sizeof link_before);
        }
        check("sec: every carried length that is not whole blocks is refused",
              tried > 0 && refused == tried);
        check("sec: and every one of those refusals is state-atomic",
              unchanged == tried);

        head.kind = WATERLINK_KIND_CARRY;
        head.counter = 1;
        memory_copy(datagram, address_of head, sizeof head);
        length = waterlink_seal(address_of sealing_key, datagram, 0);
        check("sec: after all of them the same keys still take a whole datagram",
              link_carried(datagram, length, from, 1234, now, null));

        link_session_close(s);
        crypto_forget(address_of sealing_key, sizeof sealing_key);
}

/*
        A carried datagram that was accepted is a replay the second time,
        whatever its tag says: its counter is spent once, and the refused copy
        changes no state. An older counter not yet seen is still new, since
        the window is not a strict count. A rekey gives the new keys a window
        of their own, so the counter the old keys spent is new under the new
        ones, and the old keys still refuse it.
*/
static fn carried_replay(void)
{
        struct link_session address_to s = link_self.session;
        struct link_session before;
        struct waterlink_link link_before;
        struct waterlink_datagram head = {WATERLINK_KIND_CARRY, 0x10203040, 40};
        struct waterlink_datagram next_head = {WATERLINK_KIND_CARRY, 0x11223344, 40};
        crypto_aesgcm_key sealing_key;
        crypto_aesgcm_key next_key;
        p8 raw[16];
        p8 raw_next[16];
        p8 sealed[WATERLINK_DATAGRAM];
        p8 datagram[WATERLINK_DATAGRAM];
        p8 from[16];
        positive length;
        p64 now;

        check("a session for replays opens", link_session_open(s));
        wls_seeded(raw, sizeof raw, 93);
        wls_seeded(raw_next, sizeof raw_next, 94);
        link_keys_install(address_of s->now, raw, raw, head.receiver,
                          0x50607080);
        now = link_now();
        crypto_aesgcm_prepare(address_of sealing_key, raw);
        crypto_aesgcm_prepare(address_of next_key, raw_next);
        memory_copy(from, wls_loopback, sizeof from);
        from[15] = 2;

        memory_copy(datagram, address_of head, sizeof head);
        length = waterlink_seal(address_of sealing_key, datagram, 0);
        memory_copy(sealed, datagram, length);
        check("sec: a carried datagram is new the first time",
              link_carried(datagram, length, from, 1234, now, null));

        memory_copy(datagram, sealed, length);
        before = *s;
        link_before = *s->link;
        check("sec: the same bytes again are a replay and are refused",
              !link_carried(datagram, length, from, 1234, now, null));
        check("sec: refusing the replay changes no session, replay or link state",
              !memory_compare(s, address_of before, sizeof before) &&
                      !memory_compare(s->link, address_of link_before,
                                      sizeof link_before));

        head.counter = 39;
        memory_copy(datagram, address_of head, sizeof head);
        length = waterlink_seal(address_of sealing_key, datagram, 0);
        check("sec: an older counter not yet seen is still new",
              link_carried(datagram, length, from, 1234, now, null));

        memory_copy(datagram, address_of next_head, sizeof next_head);
        link_keys_install(address_of s->next, raw_next, raw_next,
                          next_head.receiver, 0x90a0b0c0);
        length = waterlink_seal(address_of next_key, datagram, 0);
        check("sec: the counter the old keys spent is new under the rekeyed keys",
              link_carried(datagram, length, from, 1234, now, null));

        head.counter = 40;
        memory_copy(datagram, address_of head, sizeof head);
        length = waterlink_seal(address_of sealing_key, datagram, 0);
        check("sec: and the old keys still refuse the counter they spent",
              !link_carried(datagram, length, from, 1234, now, null));

        /*      The keys that rekey promoted leave their window behind in the
                next slot. A second rekey must start that window empty, or
                the counter the first next keys spent would still be marked
                and this one refused. */
        {
                struct waterlink_datagram third = {WATERLINK_KIND_CARRY,
                                                   0x55667788, 40};

                link_keys_install(address_of s->next, raw_next, raw_next,
                                  third.receiver, 0x90a0b0c0);
                memory_copy(datagram, address_of third, sizeof third);
                length = waterlink_seal(address_of next_key, datagram, 0);
                check("sec: a second rekey starts an empty window, so the same "
                      "counter is new under it",
                      link_carried(datagram, length, from, 1234, now, null));
        }

        link_session_close(s);
        crypto_forget(address_of sealing_key, sizeof sealing_key);
        crypto_forget(address_of next_key, sizeof next_key);
}

/*
        The state file hears of a session once a second, and at once when it
        moves -- not at every datagram, which walked every peer seen and
        rewrote the file five times a second under any traffic.
*/
static fn seen_once_a_second(void)
{
        struct link_session address_to s = link_self.session;
        struct waterlink_datagram head = {WATERLINK_KIND_CARRY, 0x10203041, 0};
        crypto_aesgcm_key sealing_key;
        p8 raw[16], datagram[64], from[16];
        link_state address_to state = address_of link_self.state;
        positive dirtied = 0;
        p8 noted;
        p64 now;

        link_self.server = true;
        memory_zero(state, sizeof(address_to state));
        check("a session for the seen table opens", link_session_open(s));
        wls_seeded(raw, sizeof raw, 92);
        link_keys_install(address_of s->now, raw, raw, head.receiver, 5);
        crypto_aesgcm_prepare(address_of sealing_key, raw);
        wls_seeded(s->peer, 32, 93);
        memory_copy(from, wls_loopback, 16);
        now = link_now();

        for (positive n = 0; n < 8; n++)
        {
                head.counter = n;
                memory_copy(datagram, address_of head, 16);
                (void)waterlink_seal(address_of sealing_key, datagram, 0);
                if (n == 5)
                        from[15] = 7;
                link_self.state_dirty = false;
                noted = link_carried(datagram, 48, from, 4321, now + n, null);
                dirtied += noted && link_self.state_dirty;
        }
        check("a session is seen where it is, once a second and again when it "
              "moves",
              state->seen_count == 1 &&
                      !memory_compare(state->seen[0].key, s->peer, 32) &&
                      !memory_compare(state->seen[0].address, from, 16) &&
                      state->seen[0].port == 4321 && dirtied == 2);
        link_session_close(s);
        memory_zero(state, sizeof(address_to state));
        link_self.server = false;
}

/*      The encrypted frame key and its first payload byte are both format
        discriminators.  Exercise the cross-channel confusion cases directly:
        valid authentication must not let an input record impersonate a local
        process exit, or an unknown/overlong answer release client input. */
static fn control_records_are_canonical(void)
{
        struct link_session s;
        struct waterlink_frame head;
        p8 payload[8];
        positive stream_wrong = 0;
        positive answer_wrong = 0;

        memory_zero(address_of s, sizeof s);
        s.pid = 1;
        s.terminal = -1;
        s.writes[0].fd = -1;
        s.writes[0].key = LINK_KEY_INPUT;
        s.writes[0].done = false;
        head.key = LINK_KEY_INPUT;
        head.length = 5;
        payload[0] = LINK_EXIT;
        memory_zero(payload + 1, 4);
        check("sec: an input record cannot impersonate process exit",
              link_hear(address_of s, address_of head, payload) &&
                      !s.exited && !s.writes[0].done);

        payload[0] = LINK_END;
        head.length = 2;
        check("sec: an overlong stream end is inert",
              link_hear(address_of s, address_of head, payload) &&
                      !s.writes[0].done);

        link_self.server = false;
        head.key = LINK_KEY_ANSWER;
        head.length = 1;
        payload[0] = '?';
        check("sec: an unknown answer does not release client input",
              link_hear(address_of s, address_of head, payload) && !s.answered);
        payload[0] = 'O';
        head.length = 2;
        check("sec: an overlong acceptance is not an acceptance",
              link_hear(address_of s, address_of head, payload) && !s.answered);
        head.length = 1;
        check("an exact acceptance is accepted",
              link_hear(address_of s, address_of head, payload) && s.answered);

        /* Every short spelling, not merely the examples above. On an input
           stream only data and the exact one-byte end record have meaning;
           an answer is N plus optional text, or an exact one-byte O. */
        for (positive type = 0; type < 256; type++)
                for (positive length = 0; length <= sizeof payload; length++)
                {
                        p8 ended = type == LINK_END && length == 1;
                        p8 answer = (type == 'N' && length >= 1) ||
                                      (type == 'O' && length == 1);

                        memory_zero(address_of s, sizeof s);
                        s.writes[0].fd = -1;
                        s.writes[0].key = LINK_KEY_INPUT;
                        memory_fill(payload, 'x', sizeof payload);
                        payload[0] = (p8)type;
                        head.key = LINK_KEY_INPUT;
                        head.length = (p16)length;
                        (void)link_hear(address_of s, address_of head, payload);
                        stream_wrong += s.exited || s.writes[0].done != ended;

                        memory_zero(address_of s, sizeof s);
                        head.key = LINK_KEY_ANSWER;
                        (void)link_hear(address_of s, address_of head, payload);
                        answer_wrong += s.answered != answer;
                }
        check("sec: every short stream control spelling is canonical",
              stream_wrong == 0);
        check("sec: every short answer spelling is canonical",
              answer_wrong == 0);
}

/*
        A run of full datagrams longer than one send carries: sent as
        segments in sends the kernel takes, every datagram arriving whole.
        A run of sixty four was refused as too long for one UDP datagram,
        and the refusal turned segments off for good.
*/
static fn segment_runs(bipolar listener, p16 port)
{
        struct link_session address_to s = link_self.session;
        p8 raw[16];
        p8 frame[LINK_CHUNK];
        p8 datagram[WATERLINK_DATAGRAM + 16];
        positive runs = LINK_SEGMENTS + 6;
        positive heard = 0, whole = 0;
        timespec pause = {0, 20000000};
        bipolar got;

        check("a session for segment runs opens", link_session_open(s));
        wls_seeded(raw, sizeof raw, 93);
        link_keys_install(address_of s->now, raw, raw, 0x11223344, 0x55667788);
        memory_copy(s->address, wls_loopback, 16);
        s->port = port;
        s->link->window = WATERLINK_WINDOW_MOST;
        link_self.gso = true;
        wls_drain(listener);
        wls_seeded(frame, sizeof frame, 5);
        for (positive at = 0; at < runs; at++)
                (void)link_post(s, 9, WATERLINK_FRAME_DURABLE, LINK_DATA, frame,
                                sizeof frame);
        link_session_flush(s, link_now());
        (void)system_call_2(syscall(nanosleep), (positive)address_of pause, 0);
        while ((got = socket_receive((b32)listener, datagram, sizeof datagram,
                                     MSG_DONTWAIT, null, null)) > 0)
        {
                heard++;
                whole += got == WATERLINK_DATAGRAM;
        }
        check("a run longer than one send's segments leaves segments on",
              link_self.gso);
        check("and every datagram of it arrives whole",
              heard == runs && whole == runs);
        link_session_close(s);
}

/*
        Datagrams leave in the order they are numbered: full ones wait in the
        batch for a run, and a short one after them waited for nothing, so it
        reached the far side first and its acknowledgement named the run's
        frames lost -- some thousand spurious resends a gigabyte sent.
*/
static fn in_order_sends(bipolar listener, p16 port)
{
        struct link_session address_to s = link_self.session;
        p8 raw[16];
        p8 frame[LINK_CHUNK];
        p8 datagram[WATERLINK_DATAGRAM + 16];
        positive heard = 0, ordered = 0, short_ones = 0;
        p64 last = 0;
        timespec pause = {0, 20000000};
        bipolar got;

        check("a session for in-order sends opens", link_session_open(s));
        wls_seeded(raw, sizeof raw, 94);
        link_keys_install(address_of s->now, raw, raw, 0x11223344, 0x55667788);
        memory_copy(s->address, wls_loopback, 16);
        s->port = port;
        s->link->window = WATERLINK_WINDOW_MOST;
        link_self.gso = true;
        wls_drain(listener);
        wls_seeded(frame, sizeof frame, 6);
        for (positive at = 0; at < 10; at++)
                (void)link_post(s, 9, WATERLINK_FRAME_DURABLE, LINK_DATA, frame,
                                sizeof frame);
        (void)link_post(s, 9, WATERLINK_FRAME_DURABLE, LINK_DATA, frame, 40);
        link_session_flush(s, link_now());
        (void)system_call_2(syscall(nanosleep), (positive)address_of pause, 0);
        while ((got = socket_receive((b32)listener, datagram, sizeof datagram,
                                     MSG_DONTWAIT, null, null)) > 0)
        {
                struct waterlink_datagram head;

                memory_copy(address_of head, datagram, 16);
                ordered += !heard || head.counter > last;
                short_ones += got < WATERLINK_DATAGRAM;
                last = head.counter;
                heard++;
        }
        check("sec: datagrams arrive in the order they are numbered, a short "
              "one after a run as well",
              heard >= 11 && short_ones >= 1 && ordered == heard);
        link_session_close(s);
}

/*
        Runs read coalesced: the link's own socket takes a run of full
        datagrams with a short one last as one read and the size of each,
        and a run the link sends itself is taken frame by frame, in order.
*/
static fn coalesced_runs(void)
{
        struct link_session address_to s = link_self.session;
        bipolar outer = link_self.socket;
        socket_address_internet6 bound, to;
        b32 bound_size = sizeof bound;
        p8 run[3 * WATERLINK_DATAGRAM + 48];
        p8 raw[16], frame[LINK_CHUNK];
        link_run ran[2];
        positive runs = LINK_SEGMENTS + 6;
        p16 port;
        timespec pause = {0, 20000000};
        link_iovec part = {run, sizeof run};
        p64 control[3];
        link_message message;
        bipolar got;

        link_self.socket = link_socket_open(0, true);
        if (link_self.socket < 0 || link_self.v4 ||
            socket_name((b32)link_self.socket, address_of bound,
                        address_of bound_size) < 0)
        {
                check("the link's own socket opens for coalesced runs", false);
                link_self.socket = outer;
                return;
        }
        port = network_order_16(bound.port);

        //      Three full datagrams and a short one, sent as segments.
        wls_seeded(run, sizeof run, 7);
        link_socket_address(address_of to, wls_loopback, port);
        memory_zero(control, sizeof control);
        control[0] = 18;                        // cmsg_len
        ((b32 address_to)control)[2] = 17;      // SOL_UDP
        ((b32 address_to)control)[3] = 103;     // UDP_SEGMENT
        ((p16 address_to)control)[8] = WATERLINK_DATAGRAM;
        memory_zero(address_of message, sizeof message);
        message.name = address_of to;
        message.name_length = sizeof to;
        message.parts = address_of part;
        message.part_count = 1;
        message.control = control;
        message.control_length = 24;
        got = system_call_3(syscall(sendmsg), (positive)outer,
                            (positive)address_of message, MSG_NOSIGNAL);
        (void)system_call_2(syscall(nanosleep), (positive)address_of pause, 0);
        check("a run of segments goes out as one send", got == sizeof run);
        got = link_receive(ran);
        check("and is read as one run, told the size of each datagram, in a "
              "call that says the socket had no more",
              got == 1 && ran[0].length == sizeof run &&
                      ran[0].size == WATERLINK_DATAGRAM &&
                      !memory_compare(link_inbound[0], run, sizeof run) &&
                      !memory_compare(ran[0].address, wls_loopback, 16));

        //      Two runs waiting: one call brings both, the next says "not now".
        run[0] ^= 1;
        (void)system_call_3(syscall(sendmsg), (positive)outer,
                            (positive)address_of message, MSG_NOSIGNAL);
        run[0] ^= 1;
        (void)system_call_3(syscall(sendmsg), (positive)outer,
                            (positive)address_of message, MSG_NOSIGNAL);
        (void)system_call_2(syscall(nanosleep), (positive)address_of pause, 0);
        got = link_receive(ran);
        check("two runs waiting are both read in one call, each whole",
              got == 2 && ran[0].length == sizeof run &&
                      ran[1].length == sizeof run &&
                      ran[1].size == WATERLINK_DATAGRAM &&
                      link_inbound[0][0] == (p8)(run[0] ^ 1) &&
                      !memory_compare(link_inbound[1], run, sizeof run) &&
                      !memory_compare(ran[1].address, wls_loopback, 16));
        check("and the call after them says the socket is empty",
              link_receive(ran) == -EAGAIN);

        check("a session that talks to itself opens", link_session_open(s));
        wls_seeded(raw, sizeof raw, 95);
        link_keys_install(address_of s->now, raw, raw, 0x10111213, 0x10111213);
        memory_copy(s->address, wls_loopback, 16);
        s->port = port;
        s->link->window = WATERLINK_WINDOW_MOST;
        link_self.gso = true;
        wls_seeded(frame, sizeof frame, 9);
        for (positive at = 0; at < runs; at++)
                (void)link_post(s, 9, WATERLINK_FRAME_DURABLE, LINK_DATA, frame,
                                sizeof frame);
        link_session_flush(s, link_now());
        (void)system_call_2(syscall(nanosleep), (positive)address_of pause, 0);
        link_self.socket_quiet = false;
        link_receive_all(link_now());
        check("a run it sent itself is taken, every frame in order",
              s->link->delivered == runs &&
                      s->link->receiving[9].delivered == runs);
        link_session_close(s);
        socket_close((b32)link_self.socket);
        link_self.socket = outer;
}

/* Capture the actual wait budget after procedural amounts of turn work. */
static fn wait_processing_latency(void)
{
        const p64 work[] = {2000, 20000, 200000, 1000000};
        const p64 remaining[] = {0, 500, 2000, 20000};

        wls_wait_capture = true;
        for (positive row = 0; row < array_count(work); row++)
                for (positive column = 0; column < array_count(remaining); column++)
                {
                        p64 current = link_now();
                        p64 started = current > work[row] ? current - work[row] : 0;
                        p64 wake = current + remaining[column];

                        wls_wait_calls = 0;
                        link_wait(null, null, 0, wake, started);
                        p64 wait = (p64)wls_wait_limit.tv_sec * 1000000 +
                                   (p64)wls_wait_limit.tv_nsec / 1000;
                        check("processing time never renews a Waterlink timer",
                              wls_wait_calls == 1 &&
                                  wait <= remaining[column]);
                }
        {
                p64 current = link_now();

                link_wait(null, null, 0, current - 1, current);
                check("already due Waterlink work polls without sleeping",
                      !wls_wait_limit.tv_sec && !wls_wait_limit.tv_nsec);
        }
        wls_wait_capture = false;
}

/* File-open counts cover idle/finished slots and timely active revocation. */
static fn idle_grant_refresh(void)
{
        const positive slots[] = {0, LINK_SESSIONS / 2, LINK_SESSIONS - 1};

        for (positive row = 0; row < array_count(slots); row++)
        {
                struct link_session address_to s = link_self.session + slots[row];
                p64 now = 2000000 + row * 10000000;
                p64 outer_granted = link_self.granted;

                link_self.granted = 0;
                wls_open_capture = true;
                wls_open_calls = 0;
                link_sessions_granted(now);
                check("idle grant refresh opens no authorization file",
                      !wls_open_calls && !link_self.granted);
                p8 opened = link_session_open(s);

                check("idle grant refresh session fixture opens", opened);
                if (opened)
                {
                        s->finished = true;
                        wls_open_calls = 0;
                        link_sessions_granted(now + 1);
                        check("finished sessions need no authorization file refresh",
                              !wls_open_calls && !link_self.granted);
                        s->finished = false;
                        s->kind = LINK_KIND_RUN;
                        wls_seeded(s->peer, sizeof s->peer, (p8)row);
                        wls_peers_with(s->peer, WATERLINK_MAY_RUN);
                        wls_open_calls = 0;
                        link_sessions_granted(now + 2);
                        check("first active session refreshes grants without an idle delay",
                              wls_open_calls == 1 && !s->finished &&
                                  link_self.granted == now + 2);
                        wls_peers_with(s->peer, 0);
                        wls_open_calls = 0;
                        link_sessions_granted(now + 999999);
                        check("active grant refresh retains its one-second rate limit",
                              !wls_open_calls && !s->finished);
                        link_sessions_granted(now + 1000002);
                        check("revoked active grants still end their session on time",
                              wls_open_calls == 1 && s->finished);
                        link_session_close(s);
                }
                wls_open_capture = false;
                link_self.granted = outer_granted;
        }
}

static p8 terminal_hold(address_any context,
                           struct waterlink_frame address_to head,
                           p8 address_to payload)
{
        (void)context; (void)head; (void)payload;
        return false;
}

/* Real stream descriptors and a held DATA/END pair cover the last turn
   after peer removal, grant revocation, close, silence and key exhaustion. */
static fn terminal_sessions_never_stream(void)
{
        const positive slots[] = {0, LINK_SESSIONS / 2, LINK_SESSIONS - 1};
        p8 send[16] = {0}, receive[16] = {0};
        p8 body[] = {WATERLINK_FRAME_DURABLE, LINK_KEY_INPUT, 1, 2,
                     LINK_DATA, '!',
                     WATERLINK_FRAME_DURABLE | WATERLINK_FRAME_LAST,
                     LINK_KEY_INPUT, 2, 1, LINK_END};
        p8 outer_server = link_self.server;
        p64 outer_granted = link_self.granted;

        for (positive slot = 0; slot < array_count(slots); slot++)
        for (positive cause = 0; cause < 7; cause++)
        {
                struct link_session address_to s = link_self.session + slots[slot];
                b32 ends[2];
                p8 read[16], part[LINK_REQUEST_MAX + 32];
                file_facts gone;
                p64 now = 1000000000 + slot * 10000000 + cause * 1000000;
                p8 pushed = wls_write("/root/terminal-target", "before", 6, 0600) &&
                              wls_push(s, "/root/terminal-target");

                check("terminal-session staged upload fixture opens", pushed);
                if (!pushed)
                {
                        if (s->used) link_session_close(s);
                        continue;
                }
                wls_seeded(s->peer, 32, 17);
                wls_peers_with(s->peer, WATERLINK_MAY_FILES);
                link_keys_install(address_of s->now, send, receive, 123, 124);
                s->now.made = s->heard = s->spoke = now;
                string_copy(part, s->part);
                p8 piped = system_pipe(ends, O_CLOEXEC | O_NONBLOCK) >= 0;
                bipolar observer = piped
                    ? system_call_3(syscall(fcntl), ends[0], 1030, 10) : -1;

                check("terminal-session queued output fixture opens", piped && observer >= 0);
                if (!piped || observer < 0)
                {
                        if (piped) { system_close(ends[0]); system_close(ends[1]); }
                        link_session_close(s);
                        continue;
                }
                link_stream_set(s->reads, ends[0], LINK_KEY_OUTPUT,
                                 WATERLINK_FRAME_DURABLE);
                check("terminal-session output waits on its descriptor",
                      system_write_once(ends[1], "pending", 7) == 7);
                check("terminal-session input and end wait behind backpressure",
                      waterlink_deliver(s->link, body, sizeof body, now,
                                         terminal_hold, s) &&
                          waterlink_paused(s->link, LINK_KEY_INPUT));
                if (cause == 1)
                        (void)system_remove_at(AT_FDCWD, LINK_PEERS_PATH, 0);
                else if (cause == 2)
                        wls_peers_with(s->peer, 0);
                else if (cause == 3)
                        s->finished = true;
                else if (cause == 4)
                        s->heard = now - LINK_DEAD - 1;
                else if (cause == 5)
                        s->now.made = now - (p64)WATERLINK_REJECT_SECONDS * 1000000;
                else if (cause == 6)
                        s->now.counter = WATERLINK_REKEY_MESSAGES + (1ull << 20);
                link_self.granted = 0;
                link_self.server = true;
                (void)link_sessions_turn(now);

                check("terminal sessions close before any streams are resumed",
                      cause ? !s->used : s->used);
                bipolar got = system_read_once(observer, read, sizeof read);

                check("terminal sessions leave unread descriptor bytes untouched",
                      cause ? got == 7 && !memory_compare(read, "pending", 7)
                            : got == -EAGAIN);
                bipolar target = system_open_at(AT_FDCWD, "/root/terminal-target",
                                                 FILE_READ | O_CLOEXEC);
                got = target < 0 ? target : system_read_once(target, read, sizeof read);
                if (target >= 0) system_close(target);
                check("terminal sessions discard held upload ends without publishing",
                      got == 6 && !memory_compare(read, cause ? "before" : "whole!", 6) &&
                          !file_look_at(part, address_of gone));
                if (s->used) link_session_close(s);
                system_close(observer);
                system_close(ends[1]);
        }
        link_self.server = outer_server;
        link_self.granted = outer_granted;
        (void)system_remove_at(AT_FDCWD, "/root/terminal-target", 0);
}

/* A short read stays quiet until readiness wakes it again. */
static fn quiet_streams(void)
{
        struct link_session address_to s = link_self.session;
        system_poll_descriptor watch[5];
        p8 address_to quiet[5];
        positive count = 0;
        b32 pipe[2];
        p64 now = link_now();

        if (system_pipe(pipe, O_CLOEXEC | O_NONBLOCK) < 0 ||
            !link_session_open(s))
        {
                check("a session over a pipe opens", false);
                return;
        }
        link_stream_set(s->reads, pipe[0], LINK_KEY_OUTPUT,
                        WATERLINK_FRAME_DURABLE);
        (void)system_write_once(pipe[1], "abc", 3);
        link_stream_read(s, s->reads);
        check("a read that comes back short posts it and leaves the stream "
              "quiet",
              s->link->posted == 1 && s->reads[0].quiet);
        (void)system_write_once(pipe[1], "def", 3);
        link_stream_read(s, s->reads);
        check("a quiet stream is not read again before a wait hears it",
              s->link->posted == 1);
        link_session_watch(s, watch, quiet, address_of count);
        link_wait(watch, quiet, count, now + 5000000, now);
        check("the wait hears what came and wakes at once",
              link_now() - now < 1000000 && !s->reads[0].quiet);
        link_stream_read(s, s->reads);
        check("so what was written after is read and posted",
              s->link->posted == 2 && s->reads[0].quiet);
        link_session_close(s); // and the read end with it
        system_close(pipe[1]);
}

/*
        Group pairing: the replay marker is spent last, follow-ups come only
        from where the pairing began, and replies are judged on a copy.
*/
static struct waterlink_group_keys wls_office;

static fn wls_group(void)
{
        p8 derived[32];

        waterlink_group_derive("office", (p8 address_to) "sesame", 6, 1000,
                               derived);
        waterlink_group_keys_from(address_of wls_office, derived, "office");
        memory_zero(address_of link_nearby, sizeof link_nearby);
        link_nearby.socket = -1;
        link_nearby.groups.count = 1;
        string_copy(link_nearby.groups.record[0].namespace, "office");
        memory_copy(link_nearby.groups.record[0].key, derived, 32);
        link_nearby.keys[0] = wls_office;
        string_copy((string_address)link_nearby.name, "machine");
}

//      A group the greeted machine has, in memory and as the file: one that
//      stays (flags and expect zero) or a one-use code. A greeting is kept
//      by the group as the file holds it (link_group_held), so every check
//      that pairs a member writes the file as well as the listener's copy.
static fn wls_group_file(p32 flags, p64 expires, p32 expect)
{
        wls_group();
        link_nearby.groups.record[0].flags = flags;
        link_nearby.groups.record[0].expires = expires;
        link_nearby.groups.record[0].expect = expect;
        (void)wls_write(LINK_GROUPS_PATH,
                        address_of link_nearby.groups.record[0],
                        sizeof(struct link_group_record), 0600);
}

//      A greeting from one identity to a group, stamped seconds past now.
static fn wls_greeting(struct waterlink_identity address_to from,
                       struct waterlink_group_keys address_to to,
                       string_address name, p64 ahead, p8 address_to datagram)
{
        struct waterlink_noise noise;
        p8 hello[WATERLINK_HELLO_BYTES];
        p8 ephemeral[32];

        memory_zero(hello, sizeof hello);
        waterlink_stamp(hello, system_clock_ns(0) / 1000000000ull + ahead, 0);
        string_copy((string_address)hello + WATERLINK_STAMP_BYTES, name);
        wls_seeded(ephemeral, 32, (p8)ahead);
        (void)waterlink_initiate(address_of noise, from, to->identity.public,
                                 to->psk, ephemeral, hello, datagram);
}

/*
        Greetings, at the machine greeted: a member is kept by its key with
        the group's grants where it came from, and greeted back; a greeting
        under another secret, changed, replayed or from this machine's own
        key keeps nobody; a record paired by hand is never replaced or
        widened; and a member greeting from somewhere new is followed there.
*/
static fn greetings(bipolar listener, p16 port)
{
        struct waterlink_group_keys other;
        struct waterlink_noise noise;
        p8 greeting[WATERLINK_DATAGRAM], back[WATERLINK_DATAGRAM + 16];
        p8 who[32], hello[WATERLINK_HELLO_BYTES], derived[32];
        link_peers peers;

        link_self.me = wls_b;
        wls_group_file(0, 0, 0);
        wls_drain(listener);
        memory_zero(address_of link_self.admission, sizeof link_self.admission);
        (void)system_remove_at(AT_FDCWD, LINK_PEERS_PATH, 0);
        waterlink_group_derive("office", (p8 address_to) "wrong", 5, 1000,
                               derived);
        waterlink_group_keys_from(address_of other, derived, "office");

        wls_greeting(address_of wls_client, address_of other, "outsider", 10,
                     greeting);
        link_server_initiation(greeting, WATERLINK_DATAGRAM, wls_loopback, port,
                               1000000);
        check("sec: a greeting under another secret keeps nobody and is not "
              "answered",
              wls_peers_count() == 0 && wls_heard(listener, back) <= 0);

        wls_greeting(address_of wls_client, address_of wls_office, "machine-a",
                     11, greeting);
        greeting[16 + 40] ^= 1;
        link_server_initiation(greeting, WATERLINK_DATAGRAM, wls_loopback, port,
                               1100000);
        check("sec: a changed greeting keeps nobody", wls_peers_count() == 0);

        wls_greeting(address_of wls_client, address_of wls_office, "machine-a",
                     12, greeting);
        link_server_initiation(greeting, WATERLINK_DATAGRAM, wls_loopback, port,
                               1200000);
        link_peers_load(address_of peers);
        check("a member's greeting keeps it by its key, named as it asked, with "
              "the group's grants and where it came from",
              peers.count == 1 &&
                      !memory_compare(peers.peer[0].key, wls_client.public, 32) &&
                      string_equals(peers.peer[0].name, "machine-a") &&
                      peers.peer[0].group == wls_office.mark &&
                      peers.peer[0].may == WATERLINK_MAY_DEFAULT &&
                      !memory_compare(peers.peer[0].address, wls_loopback, 16) &&
                      peers.peer[0].port == port);
        check("and greets it back, from this machine's key, as a member",
              wls_heard(listener, back) == WATERLINK_DATAGRAM &&
                      waterlink_gate_passes(address_of wls_office.identity, back,
                                            WATERLINK_DATAGRAM) &&
                      waterlink_accept(address_of noise,
                                       address_of wls_office.identity,
                                       wls_office.psk, back, who, hello) &&
                      !memory_compare(who, wls_b.public, 32));

        link_server_initiation(greeting, WATERLINK_DATAGRAM, wls_loopback,
                               (p16)(port + 1), 1300000);
        link_peers_load(address_of peers);
        check("sec: the same greeting again is not taken",
              peers.count == 1 && peers.peer[0].port == port);

        wls_greeting(address_of wls_client, address_of wls_office, "machine-a",
                     13, greeting);
        link_server_initiation(greeting, WATERLINK_DATAGRAM, wls_loopback,
                               (p16)(port + 1), 1400000);
        link_peers_load(address_of peers);
        check("a member greeting from somewhere new is followed there",
              peers.count == 1 && peers.peer[0].port == port + 1);

        //      A restart forgets the replay markers in memory, and with them
        //      the admission buckets; the record's own marker is what still
        //      refuses the greeting played back from somewhere else.
        link_self.stamps = 0;
        memory_zero(address_of link_self.admission, sizeof link_self.admission);
        link_server_initiation(greeting, WATERLINK_DATAGRAM, wls_loopback,
                               (p16)(port + 3), 1420000);
        link_peers_load(address_of peers);
        check("sec: after a restart, a greeting played back does not move the "
              "member, and spends no replay slot",
              peers.count == 1 && peers.peer[0].port == port + 1 &&
                      link_self.stamps == 0);

        //      A peers file that is there and cannot be read is not an empty
        //      list: a new member's greeting must not save itself over it.
        {
                positive stamps = link_self.stamps;

                (void)system_call_4(syscall(fchmodat),
                                    (positive)(bipolar)AT_FDCWD,
                                    (positive)LINK_PEERS_PATH, 0644, 0);
                wls_greeting(address_of wls_server, address_of wls_office,
                             "machine-c", 15, greeting);
                link_server_initiation(greeting, WATERLINK_DATAGRAM,
                                       wls_loopback, port, 1450000);
                (void)system_call_4(syscall(fchmodat),
                                    (positive)(bipolar)AT_FDCWD,
                                    (positive)LINK_PEERS_PATH, 0600, 0);
                link_peers_load(address_of peers);
                check("sec: a greeting over a peers file that cannot be read "
                      "keeps the file and spends no replay slot",
                      peers.count == 1 &&
                              string_equals(peers.peer[0].name, "machine-a") &&
                              link_self.stamps == stamps &&
                              wls_heard(listener, back) <= 0);
        }

        {
                positive stamps = link_self.stamps;

                wls_greeting(address_of wls_b, address_of wls_office, "itself",
                             14, greeting);
                link_server_initiation(greeting, WATERLINK_DATAGRAM,
                                       wls_loopback, port, 1500000);
                check("sec: this machine's own greeting, back through the "
                      "loop, keeps nothing",
                      wls_peers_count() == 1);
                check("sec: a greeting refused by pairing spends no replay "
                      "slot",
                      link_self.stamps == stamps);
        }

        wls_peers_with(wls_client.public, WATERLINK_MAY_LOG);
        link_nearby.groups.record[0].may = WATERLINK_MAY_RUN | WATERLINK_MAY_SHELL;
        wls_greeting(address_of wls_client, address_of wls_office, "machine-a",
                     15, greeting);
        link_server_initiation(greeting, WATERLINK_DATAGRAM, wls_loopback,
                               (p16)(port + 2), 1600000);
        link_peers_load(address_of peers);
        check("sec: a record paired by hand is never replaced, moved or widened",
              peers.count == 1 && peers.peer[0].group == 0 &&
                      peers.peer[0].may == WATERLINK_MAY_LOG &&
                      string_equals(peers.peer[0].name, "client") &&
                      peers.peer[0].port != port + 2);

        (void)system_remove_at(AT_FDCWD, LINK_PEERS_PATH, 0);
        wls_group_file(0, 0, 0);
        wls_drain(listener);
        entropy_down = true;
        wls_greeting(address_of wls_client, address_of wls_office, "machine-a",
                     16, greeting);
        link_server_initiation(greeting, WATERLINK_DATAGRAM, wls_loopback, port,
                               1700000);
        entropy_down = false;
        check("sec: with no entropy a member is still kept, but not greeted "
              "back, and not marked as greeted",
              wls_peers_count() == 1 && wls_heard(listener, back) <= 0 &&
                      !link_greeted_recent(wls_loopback, wls_office.mark, port,
                                           true, 1700001));
        (void)system_remove_at(AT_FDCWD, LINK_PEERS_PATH, 0);
        (void)system_remove_at(AT_FDCWD, LINK_GROUPS_PATH, 0);
}

/*
        mDNS is anyone's to send and names any place. Announcements made up
        by one sender, each naming a new port, are greeted no more than
        LINK_GREETED times in LINK_GREET_AGAIN; and a one-shot question,
        answered to wherever it says it came from, is answered a few times a
        second at most -- neither may turn this machine into an amplifier.
*/
static fn mdns_amplification(void)
{
        bipolar socket[LINK_GREETED + 8];
        p16 port[LINK_GREETED + 8];
        positive greeted = 0;
        positive answers = 0;
        p8 packet[WATERLINK_MDNS_MAX], back[WATERLINK_DATAGRAM + 16];
        p8 loopback4[16];
        socket_address_internet where;
        p32 size = sizeof where;
        bipolar asker;
        positive length;

        wls_group();
        {
                /* IP_PKTINFO's control message: the length the kernel filled
                   bounds the read, not the room the buffer has. */
                p8 control[64];
                p64 size = 28;
                b32 level_and_type[2] = {0, 8};

                memory_fill(control, 0, sizeof control);
                memory_copy(control, address_of size, 8);
                memory_copy(control + 8, level_and_type, 8);
                network_store_32(control + 20, 0x7f000001);
                check("sec: a whole IP_PKTINFO message gives the address the "
                      "kernel says received the packet",
                      link_nearby_local(control, 28, sizeof control) ==
                          0x7f000001);
                check("sec: a message the filled length cuts short is not "
                      "read, though the buffer has room for it",
                      link_nearby_local(control, 24, sizeof control) == 0);
        }
        {
                struct waterlink_group_keys second;
                p8 derived[32];
                positive before = entropy_draws;

                waterlink_group_derive("second", (p8 address_to)"sesame", 6,
                                       1000, derived);
                waterlink_group_keys_from(address_of second, derived, "second");
                link_nearby.groups.count = 2;
                link_nearby.keys[1] = second;
                link_pair_begin(0, wls_loopback, 9, 1000000, false);
                link_pair_begin(1, wls_loopback, 9, 1000001, false);
                check("sec: one place is greeted immediately for each group, "
                      "not throttled as if groups shared an identity",
                      entropy_draws - before == 2 &&
                          link_greeted_recent(wls_loopback, wls_office.mark, 9,
                                              true, 1000002) &&
                          link_greeted_recent(wls_loopback, second.mark, 9,
                                              true, 1000002));
                wls_group();
        }
        (void)system_remove_at(AT_FDCWD, LINK_PEERS_PATH, 0);
        for (positive at = 0; at < array_count(socket); at++)
        {
                p8 instance[10], host[6];

                socket[at] = wls_listener(port + at);
                wls_seeded(instance, 10, (p8)(at + 1));
                wls_seeded(host, 6, (p8)at);
                length = waterlink_mdns_announce(packet, sizeof packet, instance,
                                                 host, port[at], 0, 4500, 0,
                                                 null, 0);
                link_nearby_heard(packet, length, wls_loopback,
                                  WATERLINK_MDNS_PORT, 0, 2000000 + at);
        }
        for (positive at = 0; at < array_count(socket); at++)
        {
                greeted += socket[at] >= 0 &&
                           wls_heard(socket[at], back) == WATERLINK_DATAGRAM;
                if (socket[at] >= 0)
                        system_close(socket[at]);
        }
        check("sec: one source cannot spend the whole global greeting ring",
              greeted == LINK_GREET_SOURCE &&
                  LINK_GREET_SOURCE < LINK_GREETED);
        {
                p8 second_source[16];
                positive before = entropy_draws;

                memory_copy(second_source, wls_loopback, 16);
                second_source[15] = 2;
                for (positive at = 0;
                     at < LINK_GREETED - LINK_GREET_SOURCE; at++)
                {
                        p8 instance[10], host[6];

                        wls_seeded(instance, 10, (p8)(at + 90));
                        wls_seeded(host, 6, (p8)(at + 90));
                        length = waterlink_mdns_announce(
                            packet, sizeof packet, instance, host,
                            (p16)(30000 + at), 0, 4500, 0, null, 0);
                        link_nearby_heard(packet, length, second_source,
                                          WATERLINK_MDNS_PORT, 0,
                                          3000000 + at);
                }
                check("sec: the source ceiling leaves the other half for another address",
                      entropy_draws - before ==
                          LINK_GREETED - LINK_GREET_SOURCE);
        }

        //      A place that refuses the greeting (port zero, from any
        //      address) is nowhere to greet: no curve work is spent on it and
        //      none of the budget is taken.
        wls_group();
        greeted = entropy_draws;
        for (positive at = 0; at < LINK_GREETED + 8; at++)
        {
                p8 instance[10], host[6], place[16];

                wls_seeded(instance, 10, (p8)(at + 60));
                wls_seeded(host, 6, (p8)at);
                memory_copy(place, wls_loopback, 16);
                place[14] = (p8)(at + 1);
                length = waterlink_mdns_announce(packet, sizeof packet, instance,
                                                 host, 0, 0, 4500, 0, null, 0);
                link_nearby_heard(packet, length, place, WATERLINK_MDNS_PORT,
                                  0, 5000000 + at);
        }
        check("sec: announcements naming a place that refuses a greeting "
              "spend none of the budget",
              entropy_draws == greeted && link_nearby.greeted_next == 0);

        link_nearby.socket = socket_new(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC |
                                                         SOCK_NONBLOCK, 0);
        asker = socket_new(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC | SOCK_NONBLOCK,
                           0);
        memory_zero(address_of where, sizeof where);
        where.family = AF_INET;
        where.host = network_order_32(HOST_LOOPBACK);
        if (link_nearby.socket < 0 || asker < 0 ||
            socket_bind((b32)asker, address_of where, sizeof where) < 0 ||
            socket_name((b32)asker, address_of where, address_of size) < 0)
        {
                check("an IPv4 loopback socket for the one-shot asker", false);
                return;
        }
        link_address_v4(loopback4, HOST_LOOPBACK);
        link_nearby.interfaces = 2;
        link_nearby.interface_address[0] = 0x0a000001;
        link_nearby.interface_address[1] = HOST_LOOPBACK;
        length = waterlink_mdns_query(packet, sizeof packet);
        link_nearby_heard(packet, length, loopback4, 0, HOST_LOOPBACK,
                          2999999);
        check("sec: a forged source port the kernel cannot answer does not "
              "spend the honest asker's reply budget",
              link_nearby.last_reply != 2999999);
        for (positive at = 0; at < 8; at++)
                link_nearby_heard(packet, length, loopback4,
                                  network_order_16(where.port), HOST_LOOPBACK,
                                  3000000 + at);
        link_nearby_heard(packet, length, loopback4, network_order_16(where.port),
                          HOST_LOOPBACK, 3000000 + LINK_ANSWER_AGAIN);
        p8 right_address = true;
        bipolar heard;
        while ((heard = wls_heard(asker, back)) > 0)
        {
                answers++;
                right_address &= heard >= 4 &&
                                 network_load_32(back + heard - 4) == HOST_LOOPBACK;
        }
        check("sec: a one-shot question is answered unicast, a few times a "
              "second at most",
              answers == 2);
        check("sec: a one-shot mDNS answer names the address that received "
              "the question, not the first interface", right_address);
        system_close(asker);
        socket_close((b32)link_nearby.socket);
        link_nearby.socket = -1;

        /* The multicast-answer side used to charge before attempting its
           send, unlike the repaired legacy-unicast side. An invalid socket
           is the deterministic kernel refusal: it must neither suppress a
           later honest answer nor let a backward time sample wrap the age. */
        link_nearby.last_answer = 7000000;
        link_nearby_heard(packet, length, loopback4, WATERLINK_MDNS_PORT,
                          HOST_LOOPBACK, 8000001);
        check("sec: a refused multicast answer spends no shared answer budget",
              link_nearby.last_answer == 7000000);
        link_nearby_heard(packet, length, loopback4, WATERLINK_MDNS_PORT,
                          HOST_LOOPBACK, 6000000);
        check("sec: a backward clock sample cannot bypass the multicast answer interval",
              link_nearby.last_answer == 7000000);
}

static fn mdns_interface_selection(void)
{
        netlink_search loopback = {.wanted = (string_address)"lo"};
        bipolar netlink = netlink_open_groups(0);
        bipolar sender = socket_new(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
        p8 packet[64];
        positive length = waterlink_mdns_query(packet, sizeof packet);

        check("mDNS interface-selection sockets open", netlink >= 0 && sender >= 0);
        if (netlink < 0 || sender < 0)
        {
                if (netlink >= 0)
                        system_close((b32)netlink);
                if (sender >= 0)
                        socket_close((b32)sender);
                return;
        }
        check("the loopback interface is found for mDNS selection",
              netlink_link_find((b32)netlink, address_of loopback) == 0);
        system_close((b32)netlink);
        if (!loopback.found)
        {
                socket_close((b32)sender);
                return;
        }

        link_nearby.socket = sender;
        link_nearby.interface[0] = (b32)loopback.index;
        check("an mDNS datagram selects and leaves on the requested interface",
              link_nearby_send(packet, length, 0));

        /* The failed option leaves the socket's previous (loopback) choice in
           the kernel. The old helper sent anyway, leaking this interface's
           packet over that stale choice. */
        link_nearby.interface[0] = 0x7fffffff;
        check("sec: a refused multicast interface cannot fall through to the stale one",
              !link_nearby_send(packet, length, 0));
        socket_close((b32)sender);
        link_nearby.socket = -1;
}

/*
        mDNS is believed only as sent on this link: a question that arrived
        with a TTL a router would have lowered is not answered, the same
        question at 255 is. Real sockets, so the TTL is the kernel's
        control message and not a value the test makes up.
*/
static fn mdns_hop_limit(void)
{
        socket_address_internet where, to;
        p32 size = sizeof where;
        b32 one = 1;
        p8 packet[64], back[WATERLINK_DATAGRAM + 16];
        positive length = waterlink_mdns_query(packet, sizeof packet);
        positive answered[2] = {0, 0};
        bipolar asker = socket_new(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC |
                                                    SOCK_NONBLOCK, 0);

        wls_group();
        link_nearby.socket = socket_new(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC |
                                                         SOCK_NONBLOCK, 0);
        memory_zero(address_of where, sizeof where);
        where.family = AF_INET;
        where.host = network_order_32(HOST_LOOPBACK);
        if (asker < 0 || link_nearby.socket < 0 ||
            socket_option_set((b32)link_nearby.socket, 0, 12, // IP_RECVTTL
                              address_of one, sizeof one) < 0 ||
            socket_bind((b32)link_nearby.socket, address_of where,
                        sizeof where) < 0 ||
            socket_name((b32)link_nearby.socket, address_of to,
                        address_of size) < 0)
        {
                check("IPv4 loopback sockets for the hop limit", false);
                return;
        }
        for (positive at = 0; at < 2; at++)
        {
                b32 ttl = at ? 255 : 64;

                (void)socket_option_set((b32)asker, 0, 2, address_of ttl,
                                        sizeof ttl); // IP_TTL
                (void)socket_send((b32)asker, packet, length, MSG_NOSIGNAL,
                                  address_of to, sizeof to);
                link_nearby_receive(4000000 + at * LINK_ANSWER_AGAIN);
                while (wls_heard(asker, back) > 0)
                        answered[at]++;
        }
        check("sec: mDNS that crossed a router is not answered; from this "
              "link, it is",
              answered[0] == 0 && answered[1] == 1);
        system_close(asker);
        socket_close((b32)link_nearby.socket);
        link_nearby.socket = -1;
}

/*
        An announcement as a stranger could make one: the instances it names,
        each with the port it says, whatever they are. A machine's own
        names one.
*/
static positive wls_mdns_naming(p8 address_to packet, const p16 address_to ports,
                                positive count)
{
        positive at = 12;

        memory_zero(packet, 12);
        packet[2] = 0x84;
        packet[7] = (p8)(2 * count);
        for (positive one = 0; one < count; one++)
        {
                //      The service's PTR to the instance: an SRV nobody
                //      advertises is not discovery.
                memory_copy(packet + at, waterlink_service_name,
                            WATERLINK_SERVICE_BYTES);
                at += WATERLINK_SERVICE_BYTES;
                memory_copy(packet + at, "\x00\x0c\x80\x01\x00\x00\x11\x94", 8);
                at += 8;
                network_store_16(packet + at, 6 + WATERLINK_SERVICE_BYTES);
                at += 2;
                packet[at++] = 5;
                memory_copy(packet + at, "wl-x", 4);
                packet[at + 4] = (p8)('a' + one);
                at += 5;
                memory_copy(packet + at, waterlink_service_name,
                            WATERLINK_SERVICE_BYTES);
                at += WATERLINK_SERVICE_BYTES;

                packet[at++] = 5;
                memory_copy(packet + at, "wl-x", 4);
                packet[at + 4] = (p8)('a' + one);
                at += 5;
                memory_copy(packet + at, waterlink_service_name,
                            WATERLINK_SERVICE_BYTES);
                at += WATERLINK_SERVICE_BYTES;
                memory_copy(packet + at,
                            "\x00\x21\x80\x01\x00\x00\x11\x94\x00\x09\x00\x00\x00\x00",
                            14);
                at += 14;
                network_store_16(packet + at, ports[one]);
                memory_copy(packet + at + 2, "\x01h", 3);
                at += 5;
        }
        return at;
}

/*
        What one announcement is worth to whoever sent it. A machine says one
        instance, so a packet that names eight is eight handshakes of 1200
        bytes to the address it came from, which a spoofed one made somebody
        else's: the first instance it names that has a port is greeted, and
        the rest are not. A port of zero is nowhere to greet and costs no
        curve work and no place in the ring that budgets them.
*/
static fn mdns_names_one(void)
{
        static const p16 many[] = {41000, 41001, 41002, 41003,
                                   41004, 41005, 41006, 41007};
        static const p16 zeros[] = {0, 0, 0, 0, 0, 0, 0, 0};
        static const p16 mixed[] = {0, 0, 41100, 41101};
        p8 packet[WATERLINK_MDNS_MAX];
        positive length;
        positive before;

        wls_group();
        before = entropy_draws;
        length = wls_mdns_naming(packet, many, 8);
        link_nearby_heard(packet, length, wls_loopback, WATERLINK_MDNS_PORT, 0,
                          6000000);
        check("sec: an announcement naming eight instances is one greeting, "
              "to the first",
              entropy_draws - before == 1 &&
                      link_greeted_recent(wls_loopback, wls_office.mark, 41000,
                                          true, 6000001) &&
                      !link_greeted_recent(wls_loopback, wls_office.mark, 41001,
                                           true, 6000001));

        wls_group();
        before = entropy_draws;
        length = wls_mdns_naming(packet, zeros, 8);
        link_nearby_heard(packet, length, wls_loopback, WATERLINK_MDNS_PORT, 0,
                          6100000);
        check("sec: eight instances at port zero are greeted not at all, and "
              "take nothing of the ring",
              entropy_draws == before && link_nearby.greeted_next == 0);

        wls_group();
        before = entropy_draws;
        length = wls_mdns_naming(packet, mixed, 4);
        link_nearby_heard(packet, length, wls_loopback, WATERLINK_MDNS_PORT, 0,
                          6200000);
        check("sec: the first instance with a port is the one greeted, past "
              "those without",
              entropy_draws - before == 1 &&
                      link_greeted_recent(wls_loopback, wls_office.mark, 41100,
                                          true, 6200001) &&
                      !link_greeted_recent(wls_loopback, wls_office.mark, 41101,
                                           true, 6200001));

        //      Only mDNS is greeted: the same announcement from a source port
        //      other than 5353 draws no curve work (RFC 6762 section 6).
        wls_group();
        before = entropy_draws;
        length = wls_mdns_naming(packet, many, 1);
        link_nearby_heard(packet, length, wls_loopback, 40353, 0, 6300000);
        check("sec: an announcement from a port other than 5353 is not greeted",
              entropy_draws == before);
        link_nearby_heard(packet, length, wls_loopback, WATERLINK_MDNS_PORT, 0,
                          6300001);
        check("and the same announcement from 5353 is",
              entropy_draws - before == 1);
}

//      A greeting that arrives, from this machine's loopback: the peers file
//      as it leaves it is the answer.
static fn wls_greeted(struct waterlink_identity address_to from,
                      string_address name, p64 ahead, p16 port, p64 now)
{
        p8 greeting[WATERLINK_DATAGRAM];

        memory_zero(address_of link_self.admission, sizeof link_self.admission);
        wls_greeting(from, address_of wls_office, name, ahead, greeting);
        link_server_initiation(greeting, WATERLINK_DATAGRAM, wls_loopback, port,
                               now);
}

/*
        A greeting from a member kept already is answered too: it greets
        because it has no record of this machine, the first greeting back
        having been lost, and nothing else would send it another, since its
        announcements are a known machine's here. Not sooner than
        LINK_GREET_AGAIN after the last greeting to its place, so two members
        do not answer each other for ever, and never for a greeting played
        back.
*/
static fn greeting_answers(bipolar listener, p16 port)
{
        p8 back[WATERLINK_DATAGRAM + 16];
        p64 first = 50000000;

        link_self.me = wls_b;
        wls_group_file(0, 0, 0);
        (void)system_remove_at(AT_FDCWD, LINK_PEERS_PATH, 0);
        link_self.stamps = 0;
        wls_drain(listener);
        wls_greeted(address_of wls_client, "machine-a", 40, port, first);
        check("a new member is greeted back",
              wls_peers_count() == 1 &&
                      wls_heard(listener, back) == WATERLINK_DATAGRAM);
        wls_greeted(address_of wls_client, "machine-a", 41, port,
                    first + 1000000);
        check("sec: one kept already that greets again is not answered inside "
              "its place's interval",
              wls_peers_count() == 1 && wls_heard(listener, back) <= 0);
        wls_greeted(address_of wls_client, "machine-a", 42, port,
                    first + LINK_GREET_AGAIN + 1);
        check("and is answered once that has passed: its greeting back was "
              "lost, and it has no other way to this machine",
              wls_heard(listener, back) == WATERLINK_DATAGRAM);
        wls_greeted(address_of wls_client, "machine-a", 42, port,
                    first + 2 * LINK_GREET_AGAIN + 2);
        check("sec: and a greeting played back is answered by nothing",
              wls_heard(listener, back) <= 0);
        (void)system_remove_at(AT_FDCWD, LINK_PEERS_PATH, 0);
}

/*
        A greeting is its sender's clock when it was sent, and one that is
        not about now is a recording: from a member that was forgotten and
        whose marker was dropped since, it brought that member back at the
        address of whoever played it, with the group's grants. Where this
        machine's clock reads a time, a greeting to a group that stays must
        be dated within an hour of it, either way; a code is single use and
        ends on its own.
*/
static fn greeting_freshness(bipolar listener, p16 port)
{
        static const struct
        {
                long ahead;
                p8 kept;
        } dated[] = {
            {0, true},         {-60, true},        {-3500, true},
            {3500, true},      {-3700, false},     {3700, false},
            {-7200, false},    {7200, false},      {-86400 * 30, false},
            {86400 * 365, false}, {-1700000000, false},
        };
        p64 wall = system_clock_ns(0) / 1000000000ull;
        p8 back[WATERLINK_DATAGRAM + 16];
        positive kept = 0, refused = 0, mistaken = 0, answered = 0;

        if (wall < LINK_CLOCK_FLOOR)
                return;
        link_self.me = wls_b;
        for (positive at = 0; at < array_count(dated); at++)
        {
                wls_group_file(0, 0, 0);
                (void)system_remove_at(AT_FDCWD, LINK_PEERS_PATH, 0);
                link_self.stamps = 0;
                wls_drain(listener);
                wls_greeted(address_of wls_client, "machine-a",
                            (p64)(bipolar)dated[at].ahead, port, 7000000 + at);
                kept += dated[at].kept;
                refused += !dated[at].kept;
                mistaken += (wls_peers_count() == 1) != dated[at].kept;
                answered += dated[at].kept &&
                            wls_heard(listener, back) == WATERLINK_DATAGRAM;
        }
        check("sec: a greeting to a group that stays is kept when it is dated "
              "within an hour of this machine's clock, and not otherwise",
              mistaken == 0 && kept == 4 && refused == 7);
        check("and one that is kept is greeted back", answered == kept);

        //      The member that was forgotten: its marker is gone, a recording
        //      of its greeting from two hours ago is played, and it is not
        //      back; it greets now and is.
        wls_group_file(0, 0, 0);
        (void)system_remove_at(AT_FDCWD, LINK_PEERS_PATH, 0);
        link_self.stamps = 0;
        wls_greeted(address_of wls_client, "machine-a", (p64)(bipolar)-7200,
                    port, 7100000);
        check("sec: a recorded greeting of a forgotten member, its marker "
              "dropped, does not bring it back",
              wls_peers_count() == 0 && link_self.stamps == 0);
        wls_greeted(address_of wls_client, "machine-a", 5, port, 7200000);
        check("while the member's own greeting does", wls_peers_count() == 1);

        //      A code ends by itself and its own greeting may come from a
        //      machine that has not been told the time.
        wls_group_file(LINK_GROUP_ONCE, link_boot_seconds() + 300, 0);
        (void)system_remove_at(AT_FDCWD, LINK_PEERS_PATH, 0);
        link_self.stamps = 0;
        wls_greeted(address_of wls_client, "machine-a", (p64)(bipolar)-1700000000,
                    port, 7300000);
        check("sec: a greeting to a code is not held to the clock: the "
              "machine pairing may have none",
              wls_peers_count() == 1);
        (void)system_remove_at(AT_FDCWD, LINK_PEERS_PATH, 0);
        (void)system_remove_at(AT_FDCWD, LINK_GROUPS_PATH, 0);
}

/*      A socket for the listener's mDNS that sends nothing: nothing in this
        test may reach the link it runs on. A send on it is refused (the
        write side is shut), and what joins a group on it is only the
        kernel's own book. */
static bipolar wls_quiet_socket(void)
{
        bipolar handle = socket_new(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC |
                                                     SOCK_NONBLOCK, 0);

        if (handle >= 0)
                (void)system_call_2(syscall(shutdown), (positive)handle, 1);
        return handle;
}

/*
        A code, and what keeps a member: it lets one machine in and is closed
        to every other at once; it lets in no machine after its minutes, nor
        one that is not the name it waits for; the member that came in is
        still followed while it is closed, so a greeting back that was lost
        can be answered; and what ran out is gone from the file whether or not
        anything else changed it.
*/
static fn pairing_codes(bipolar listener, p16 port)
{
        link_groups groups;
        p64 boot = link_boot_seconds();
        p8 back[WATERLINK_DATAGRAM + 16];

        link_self.me = wls_b;
        wls_group_file(LINK_GROUP_ONCE, boot + 300, 0);
        (void)system_remove_at(AT_FDCWD, LINK_PEERS_PATH, 0);
        link_self.stamps = 0;
        wls_drain(listener);
        wls_greeted(address_of wls_client, "machine-a", 20, port, 8000000);
        link_groups_load(address_of groups);
        check("a code lets the first machine in, and greets it back",
              wls_peers_count() == 1 &&
                      wls_heard(listener, back) == WATERLINK_DATAGRAM);
        check("and is closed at once, in memory and in the file, for half a "
              "minute more",
              (link_nearby.groups.record[0].flags & LINK_GROUP_DONE) &&
                      link_nearby.groups.record[0].expires <=
                              link_boot_seconds() + LINK_PAIR_GRACE &&
                      groups.count == 1 &&
                      (groups.record[0].flags & LINK_GROUP_DONE) &&
                      (groups.record[0].flags & LINK_GROUP_ONCE) &&
                      groups.record[0].expires > boot &&
                      groups.record[0].expires <=
                              link_boot_seconds() + LINK_PAIR_GRACE);
        wls_greeted(address_of wls_server, "machine-b", 21, port, 8100000);
        check("sec: a second machine is not let in by a code that took one",
              wls_peers_count() == 1);
        wls_greeted(address_of wls_client, "machine-a", 22, port + 1, 8200000);
        {
                link_peers peers;

                link_peers_load(address_of peers);
                check("sec: the machine that came in is still followed, and "
                      "is not greeted again as if it were new",
                      peers.count == 1 && peers.peer[0].port == port + 1 &&
                              wls_heard(listener, back) <= 0);
        }

        wls_group_file(LINK_GROUP_ONCE, boot, 0);
        (void)system_remove_at(AT_FDCWD, LINK_PEERS_PATH, 0);
        link_self.stamps = 0;
        wls_greeted(address_of wls_client, "machine-a", 23, port, 8300000);
        check("sec: a code past its minutes lets nobody in", wls_peers_count() == 0);

        wls_group_file(LINK_GROUP_ONCE, boot + 300,
                       link_name_check("machine-a", WATERLINK_NAME_MAX));
        link_self.stamps = 0;
        wls_greeted(address_of wls_client, "machine-b", 24, port, 8400000);
        check("sec: a code for one name lets in no other", wls_peers_count() == 0);
        wls_greeted(address_of wls_client, "machine-a", 25, port, 8500000);
        check("while the name it waits for is let in", wls_peers_count() == 1);

        //      A code left in the file by the boot before this one: its time
        //      is seconds since that boot, and reads here as hours ahead. No
        //      code is given more than its five minutes, so it is spent.
        wls_group_file(LINK_GROUP_ONCE, boot + LINK_PAIR_SECONDS + 1, 0);
        link_self.stamps = 0;
        wls_greeted(address_of wls_b, "machine-c", 26, port, 8600000);
        check("sec: a code from the boot before, with more time left than a "
              "code is given, lets nobody in",
              wls_peers_count() == 1 &&
                      link_group_spent(link_nearby.groups.record, boot) &&
                      !link_pair_waiting(link_nearby.groups.record, "office",
                                         boot));
        link_nearby.groups.record[0].expires = boot + LINK_PAIR_SECONDS;
        check("while one with exactly its five minutes left is open",
              !link_group_spent(link_nearby.groups.record, boot));

        //      Runs out in the file as well, as the listener's turn finds it.
        wls_group_file(LINK_GROUP_ONCE, boot, 0);
        link_nearby.socket = wls_quiet_socket();
        link_nearby_reload(20000000);
        link_groups_load(address_of groups);
        check("what ran out is dropped from the file, and the listener has "
              "no group left",
              groups.count == 0 && link_nearby.groups.count == 0 &&
                      link_nearby.socket < 0);
        wls_group_file(LINK_GROUP_ONCE, boot + 300, 0);
        link_nearby.socket = wls_quiet_socket();
        link_nearby_reload(30000000);
        link_groups_load(address_of groups);
        check("and what has not, is kept", groups.count == 1 &&
                                               link_nearby.groups.count == 1);
        link_nearby_close();
        (void)system_remove_at(AT_FDCWD, LINK_PEERS_PATH, 0);
        (void)system_remove_at(AT_FDCWD, LINK_GROUPS_PATH, 0);
}

/*
        What a greeting may make of its sender is the group as the groups
        file holds it, under the lock each change to the file is made under,
        and not as the listener last read it: the listener reads the file
        again at most twice a second, and a command that changed it has
        returned by then. Each row changes the file the way a command does
        and greets before the listener has looked again (MW-2026-070).
*/
static fn group_revocations(bipolar listener, p16 port)
{
        struct link_group_record changed;
        link_peers peers;
        p8 back[WATERLINK_DATAGRAM + 16];

        link_self.me = wls_b;

        //      `link group leave office forget`: the member it paired is
        //      removed, and greets again at once, as one being put out would.
        wls_group_file(0, 0, 0);
        (void)system_remove_at(AT_FDCWD, LINK_PEERS_PATH, 0);
        link_self.stamps = 0;
        wls_drain(listener);
        wls_greeted(address_of wls_client, "machine-a", 30, port, 9000000);
        wls_drain(listener);
        check("a member of a group is kept", wls_peers_count() == 1);
        (void)link_group_leave("office", true);
        check("and is removed when the group is left with forget, while the "
              "listener has not read the groups file since",
              wls_peers_count() == 0 && link_nearby.groups.count == 1);
        wls_greeted(address_of wls_client, "machine-a", 31, port, 9100000);
        check("sec: a group left with its machines forgotten takes none of "
              "them back before the listener reads the file again",
              wls_peers_count() == 0 && wls_heard(listener, back) <= 0);

        //      `link group office allow shell`, where it gave run and shell:
        //      a member new since gets what the group gives now.
        wls_group_file(0, 0, 0);
        changed = link_nearby.groups.record[0];
        changed.may = WATERLINK_MAY_SHELL;
        link_nearby.groups.record[0].may = WATERLINK_MAY_RUN | WATERLINK_MAY_SHELL;
        (void)wls_write(LINK_GROUPS_PATH, address_of changed, sizeof changed,
                        0600);
        (void)system_remove_at(AT_FDCWD, LINK_PEERS_PATH, 0);
        link_self.stamps = 0;
        wls_drain(listener);
        wls_greeted(address_of wls_client, "machine-a", 32, port, 9200000);
        link_peers_load(address_of peers);
        check("sec: a member new to a group narrowed since the listener read "
              "it is given what the group gives now, not what it took back",
              peers.count == 1 && peers.peer[0].may == WATERLINK_MAY_SHELL);

        //      `link group office NEWSECRET`: the old secret opens nothing.
        wls_group_file(0, 0, 0);
        changed = link_nearby.groups.record[0];
        waterlink_group_derive("office", (p8 address_to) "another-sesame", 14,
                               1000, changed.key);
        (void)wls_write(LINK_GROUPS_PATH, address_of changed, sizeof changed,
                        0600);
        (void)system_remove_at(AT_FDCWD, LINK_PEERS_PATH, 0);
        link_self.stamps = 0;
        wls_drain(listener);
        wls_greeted(address_of wls_client, "machine-a", 33, port, 9300000);
        check("sec: a group given another secret is opened by the old one no "
              "more, before the listener reads the file again",
              wls_peers_count() == 0 && wls_heard(listener, back) <= 0);

        //      A code the waiting machine took back (link_pair_close).
        wls_group_file(LINK_GROUP_ONCE, link_boot_seconds() + 300, 0);
        (void)system_remove_at(AT_FDCWD, LINK_PEERS_PATH, 0);
        link_self.stamps = 0;
        link_pair_close(wls_office.mark);
        wls_greeted(address_of wls_client, "machine-a", 34, port, 9400000);
        check("sec: a code taken back lets nobody in, before the listener "
              "reads the file again",
              wls_peers_count() == 0 && wls_heard(listener, back) <= 0);

        //      A groups file that is not root's private file is no groups,
        //      as the listener's next look will say.
        wls_group_file(0, 0, 0);
        (void)system_call_4(syscall(fchmodat), (positive)(bipolar)AT_FDCWD,
                            (positive)LINK_GROUPS_PATH, 0644, 0);
        (void)system_remove_at(AT_FDCWD, LINK_PEERS_PATH, 0);
        link_self.stamps = 0;
        wls_greeted(address_of wls_client, "machine-a", 35, port, 9500000);
        check("sec: a groups file that cannot be read pairs nobody",
              wls_peers_count() == 0 && wls_heard(listener, back) <= 0);

        wls_group_file(0, 0, 0);
        link_self.stamps = 0;
        wls_greeted(address_of wls_client, "machine-a", 36, port, 9600000);
        check("and the group the file still holds pairs as before",
              wls_peers_count() == 1 &&
                      wls_heard(listener, back) == WATERLINK_DATAGRAM);
        wls_drain(listener);
        (void)system_remove_at(AT_FDCWD, LINK_PEERS_PATH, 0);
        (void)system_remove_at(AT_FDCWD, LINK_GROUPS_PATH, 0);
}

/*
        The listener reads its groups again when the file has changed, at
        most twice a second. A save is a rename, so the inode says which file
        it is, and an inode that is freed and given to the next save, with the
        same records the same size, would say nothing changed: what was
        changed and written, to the nanosecond, says more. A file that is not
        there is no groups, and takes the socket with it.
*/
static fn nearby_reload(void)
{
        struct link_group_record record;
        struct waterlink_group_keys keys;
        struct
        {
                p64 seconds;
                p64 nanoseconds;
        } times[2] = {{0, UTIME_OMIT}, {1000000000, 5}};
        bipolar handle;
        positive got;

        memory_zero(address_of record, sizeof record);
        string_copy(record.namespace, "office");
        memory_fill(record.key, 0x5a, sizeof record.key);
        record.may = WATERLINK_MAY_RUN;
        waterlink_group_keys_from(address_of keys, record.key, "office");

        memory_zero(address_of link_nearby, sizeof link_nearby);
        (void)system_remove_at(AT_FDCWD, LINK_GROUPS_PATH, 0);
        link_nearby.socket = wls_quiet_socket();
        link_nearby_reload(10000000);
        check("no file is no group, and nothing is opened or closed",
              link_nearby.groups.count == 0 && link_nearby.socket >= 0 &&
                      !link_nearby.labels_ready);

        (void)wls_write(LINK_GROUPS_PATH, address_of record, sizeof record, 0600);
        link_nearby_reload(10600000);
        check("a file that is there is read: the group, its keys derived and "
              "its labels drawn",
              link_nearby.groups.count == 1 &&
                      link_nearby.groups.record[0].may == WATERLINK_MAY_RUN &&
                      link_nearby.keys[0].mark == keys.mark &&
                      !memory_compare(link_nearby.keys[0].psk, keys.psk, 32) &&
                      link_nearby.labels_ready && link_nearby.socket >= 0 &&
                      link_nearby.next_announce == 10600000);

        link_nearby.groups.record[0].may = 77;
        link_nearby_reload(11200000);
        check("sec: a file that has not changed is not read again",
              link_nearby.groups.record[0].may == 77);

        //      The same file, the same size, the same inode: a record's
        //      grant changed in place, written at another time.
        record.may = WATERLINK_MAY_FILES;
        handle = system_open_at(AT_FDCWD, LINK_GROUPS_PATH, FILE_READ_WRITE);
        got = handle >= 0 ? system_write_all((positive)handle, address_of record,
                                             sizeof record)
                          : 0;
        if (handle >= 0)
                system_close(handle);
        (void)system_call_4(syscall(utimensat), (positive)(bipolar)AT_FDCWD,
                            (positive)LINK_GROUPS_PATH,
                            (positive)address_of times, 0);
        link_nearby_reload(11500000);
        check("sec: it is looked at twice a second at most",
              got == sizeof record && link_nearby.groups.record[0].may == 77);
        link_nearby_reload(12000000);
        check("sec: and a file changed in place, with the inode and size it "
              "had, is read again",
              link_nearby.groups.record[0].may == WATERLINK_MAY_FILES);

        //      A clock that was stepped back is not a reason to look at every
        //      turn: a turn dated before the last look is the same turn.
        record.may = WATERLINK_MAY_LOG;
        times[1].nanoseconds = 7;
        handle = system_open_at(AT_FDCWD, LINK_GROUPS_PATH, FILE_READ_WRITE);
        if (handle >= 0)
        {
                (void)system_write_all((positive)handle, address_of record,
                                       sizeof record);
                system_close(handle);
        }
        (void)system_call_4(syscall(utimensat), (positive)(bipolar)AT_FDCWD,
                            (positive)LINK_GROUPS_PATH,
                            (positive)address_of times, 0);
        link_nearby_reload(5000000);
        check("sec: a turn dated before the last look neither looks nor "
              "wraps into an age of years",
              link_nearby.groups.record[0].may == WATERLINK_MAY_FILES);
        link_nearby_reload(13000000);
        check("and the next that is due looks",
              link_nearby.groups.record[0].may == WATERLINK_MAY_LOG);

        (void)system_remove_at(AT_FDCWD, LINK_GROUPS_PATH, 0);
        link_nearby_reload(14000000);
        check("a file that is gone is no group, and closes the socket",
              link_nearby.groups.count == 0 && link_nearby.socket < 0);

        (void)wls_write(LINK_GROUPS_PATH, address_of record, sizeof record, 0644);
        link_nearby.socket = wls_quiet_socket();
        link_nearby_reload(15000000);
        check("sec: a file others can read is no group",
              link_nearby.groups.count == 0 && link_nearby.socket < 0);
        (void)system_remove_at(AT_FDCWD, LINK_GROUPS_PATH, 0);
}

/*
        An address that comes to an interface after the listener started is
        the ordinary case on a machine that boots straight into its lease:
        the memberships are asked for again every five seconds, and a table
        that changed starts the quick announcements over. The messages are the
        kernel's, made here by hand, so a hostile or short one is as likely
        as a good one.
*/
static fn nearby_address(void)
{
        p8 message[64];
        netlink_header address_to header = (netlink_header address_to)message;
        netlink_address address_to body = (netlink_address address_to)(message + 16);
        p8 refused_all = true;
        link_interface_visitor joins = link_nearby_join;

        wls_group();
        link_nearby.socket = wls_quiet_socket();
        memory_zero(message, sizeof message);
        header->length = 16 + 8 + 8;
        header->type = RTM_NEWADDR;
        body->family = AF_INET;
        body->index = 1;
        memory_copy(message + 24, "\x08\x00\x02\x00\x7f\x00\x00\x01", 8);

        check("an address of an interface is joined and kept, with its address",
              link_interface_address(header, address_of joins) &&
                      link_nearby.interfaces == 1 &&
                      link_nearby.interface[0] == 1 &&
                      link_nearby.interface_address[0] == 0x7f000001);
        (void)link_interface_address(header, address_of joins);
        check("sec: the same interface again is kept once",
              link_nearby.interfaces == 1);

        //      The refresh starts from nothing, and the group is joined on the
        //      socket already: the kernel says so, and it is as good as new.
        link_nearby.interfaces = 0;
        (void)link_interface_address(header, address_of joins);
        check("an interface joined already is joined",
              link_nearby.interfaces == 1);

        link_nearby.interfaces = 0;
        body->family = AF_INET6;
        (void)link_interface_address(header, address_of joins);
        refused_all &= link_nearby.interfaces == 0;
        body->family = AF_INET;
        header->type = RTM_NEWADDR + 1;
        (void)link_interface_address(header, address_of joins);
        refused_all &= link_nearby.interfaces == 0;
        header->type = RTM_NEWADDR;
        header->length = 16 + 8;
        (void)link_interface_address(header, address_of joins);
        refused_all &= link_nearby.interfaces == 0;
        header->length = 16 + 8 + 8;
        message[26] = 3; // an attribute that is not the local address
        (void)link_interface_address(header, address_of joins);
        refused_all &= link_nearby.interfaces == 0;
        message[26] = 2;
        message[24] = 4; // an address of two bytes
        (void)link_interface_address(header, address_of joins);
        refused_all &= link_nearby.interfaces == 0;
        message[24] = 8;
        body->index = 0x7ffffff0; // an interface nobody has
        (void)link_interface_address(header, address_of joins);
        refused_all &= link_nearby.interfaces == 0;
        check("sec: another family, another message, a short one, another "
              "attribute, a short address and an interface that is not there "
              "are none of them kept",
              refused_all);

        body->index = 1;
        link_nearby.interfaces = LINK_INTERFACES;
        (void)link_interface_address(header, address_of joins);
        check("sec: a table that is full takes no more",
              link_nearby.interfaces == LINK_INTERFACES);
        link_nearby_close();
}

/*
        The listener's turn: the groups first, then what is due -- three
        announcements and questions a second apart, as RFC 6762 asks of a
        responder, then every minute and every half a minute -- and when it
        wants to be asked again. Nothing is sent: the interfaces it has are
        none, or the quiet socket's, which refuses.
*/
static fn nearby_tick(void)
{
        struct link_group_record record;
        p64 wake;

        memory_zero(address_of record, sizeof record);
        string_copy(record.namespace, "office");
        memory_fill(record.key, 0x5a, sizeof record.key);

        memory_zero(address_of link_nearby, sizeof link_nearby);
        (void)system_remove_at(AT_FDCWD, LINK_GROUPS_PATH, 0);
        link_nearby.socket = wls_quiet_socket();
        wake = link_nearby_tick(20000000);
        check("with no group there is nothing to send, and a second to wait",
              wake == 21000000 && link_nearby.announced == 0);

        (void)wls_write(LINK_GROUPS_PATH, address_of record, sizeof record, 0600);
        link_nearby.interfaces_looked = 20000000; // not due
        wake = link_nearby_tick(20600000);
        check("a group is announced and asked about, once, and the next is a "
              "second on",
              link_nearby.groups.count == 1 && link_nearby.labels_ready &&
                      link_nearby.announced == 1 && link_nearby.asked == 1 &&
                      wake == 21600000);
        wake = link_nearby_tick(21000000);
        check("sec: asked again before it is due, it sends nothing",
              link_nearby.announced == 1 && link_nearby.asked == 1 &&
                      wake == 21600000);
        (void)link_nearby_tick(21600000);
        wake = link_nearby_tick(22600000);
        check("the third is the last quick one; then a minute for announcing "
              "and half of one for asking",
              link_nearby.announced == 3 && link_nearby.asked == 3 &&
                      link_nearby.next_announce == 22600000 + LINK_ANNOUNCE_EVERY &&
                      link_nearby.next_ask == 22600000 + LINK_ASK_EVERY &&
                      wake == 23600000);

        //      The memberships are asked for again every five seconds, and a
        //      table that is not the one it was starts the quick announcements
        //      over: here the table it had names an interface nobody has.
        link_nearby.interfaces = 1;
        link_nearby.interface[0] = 0x7ffffff0;
        link_nearby.interface_address[0] = 0x0a000001;
        link_nearby.interfaces_looked = 22600000;
        (void)link_nearby_tick(23700000);
        check("sec: the table is asked for again every five seconds, and not "
              "sooner",
              link_nearby.interface[0] == 0x7ffffff0 &&
                      link_nearby.announced == 3);
        (void)link_nearby_tick(27700000);
        check("and when it is not the table it was, the announcements start "
              "again",
              (link_nearby.interfaces != 1 ||
               link_nearby.interface[0] != 0x7ffffff0) &&
                      link_nearby.announced == 1 && link_nearby.asked == 1);

        link_nearby_close();
        (void)system_remove_at(AT_FDCWD, LINK_GROUPS_PATH, 0);
}

/*
        A groups file is read as root's own, private, whole records. The old
        check in each -- a hash of the secret, a guessing oracle at full speed
        -- is taken out of the file as well as out of memory, and only from a
        file that is what it was read as; the records it read are all it
        changes.
*/
static fn groups_scrub_edges(void)
{
        struct link_group_record three[3];
        struct link_group_record back[3];
        link_groups groups;
        bipolar handle;
        positive got;
        p8 kept = true;

        memory_zero(three, sizeof three);
        for (positive at = 0; at < 3; at++)
        {
                string_copy(three[at].namespace, at == 1 ? "bad name" : "office");
                memory_fill(three[at].key, 0x40 + at, sizeof three[at].key);
                memory_fill(three[at].check, 0xa5, sizeof three[at].check);
                three[at].may = 2 + 2 * (p32)at;
        }

        //      A record with a name that cannot be one is dropped, and its
        //      old check is still taken out of the file.
        (void)wls_write(LINK_GROUPS_PATH, three, sizeof three, 0600);
        link_groups_load(address_of groups);
        handle = system_open_at(AT_FDCWD, LINK_GROUPS_PATH, FILE_READ);
        got = handle >= 0 ? (positive)system_read_retry((positive)handle, back,
                                                        sizeof back)
                          : 0;
        if (handle >= 0)
                system_close(handle);
        for (positive at = 0; at < 3; at++)
                kept &= memory_span_byte(back[at].check, 0,
                                         sizeof back[at].check) ==
                        sizeof back[at].check;
        check("sec: a record with a name that is not one is dropped, the last "
              "in its place, and every record's old check leaves the file",
              groups.count == 2 && got == sizeof back && kept &&
                      string_equals(groups.record[0].namespace, "office") &&
                      string_equals(groups.record[1].namespace, "office") &&
                      groups.record[1].may == 6 &&
                      !memory_compare(groups.record[1].key, three[2].key, 32) &&
                      memory_span_byte(groups.record[0].check, 0, 32) == 32 &&
                      memory_span_byte(groups.record[1].check, 0, 32) == 32);

        //      Not a file to write: others can read it, or it is not whole,
        //      or it is a link to one.
        (void)wls_write(LINK_GROUPS_PATH, three, sizeof three, 0644);
        link_groups_scrub(3);
        handle = system_open_at(AT_FDCWD, LINK_GROUPS_PATH, FILE_READ);
        got = handle >= 0 ? (positive)system_read_retry((positive)handle, back,
                                                        sizeof back)
                          : 0;
        if (handle >= 0)
                system_close(handle);
        check("sec: a file others can read is not written to",
              got == sizeof back && back[0].check[0] == 0xa5);

        (void)wls_write(LINK_GROUPS_PATH, three, sizeof three - 40, 0600);
        link_groups_scrub(3);
        handle = system_open_at(AT_FDCWD, LINK_GROUPS_PATH, FILE_READ);
        got = handle >= 0 ? (positive)system_read_retry((positive)handle, back,
                                                        sizeof three - 40)
                          : 0;
        if (handle >= 0)
                system_close(handle);
        check("sec: nor one that is not whole records",
              got == sizeof three - 40 && back[0].check[0] == 0xa5);

        (void)wls_write("/root/groups.elsewhere", three, sizeof three, 0600);
        (void)system_remove_at(AT_FDCWD, LINK_GROUPS_PATH, 0);
        (void)system_symbolic_link_at("/root/groups.elsewhere", AT_FDCWD,
                                      LINK_GROUPS_PATH);
        link_groups_scrub(3);
        handle = system_open_at(AT_FDCWD, "/root/groups.elsewhere", FILE_READ);
        got = handle >= 0 ? (positive)system_read_retry((positive)handle, back,
                                                        sizeof back)
                          : 0;
        if (handle >= 0)
                system_close(handle);
        check("sec: nor one that is a link to another file",
              got == sizeof back && back[2].check[0] == 0xa5);
        (void)system_remove_at(AT_FDCWD, LINK_GROUPS_PATH, 0);
        (void)system_remove_at(AT_FDCWD, "/root/groups.elsewhere", 0);

        //      As many records as were read, and no more.
        (void)wls_write(LINK_GROUPS_PATH, three, sizeof three, 0600);
        link_groups_scrub(2);
        handle = system_open_at(AT_FDCWD, LINK_GROUPS_PATH, FILE_READ);
        got = handle >= 0 ? (positive)system_read_retry((positive)handle, back,
                                                        sizeof back)
                          : 0;
        if (handle >= 0)
                system_close(handle);
        check("sec: the records the caller read are all it changes",
              got == sizeof back && back[0].check[0] == 0 &&
                      back[1].check[31] == 0 && back[2].check[0] == 0xa5 &&
                      back[2].may == 6);
        (void)system_remove_at(AT_FDCWD, LINK_GROUPS_PATH, 0);
}

/*
        A groups file that is there and cannot be read is not an empty list:
        every change to the groups reads the file first, and a change that
        saves over what it could not read forgets every group in it. The
        file is cut short, readable by others, too large, a FIFO or a link,
        and each way that groups are changed leaves it as it was: joining,
        leaving, a code made and taken back, one closed after it took its
        machine, and the listener's drop of codes that ran out.
*/
struct wls_groups_look
{
        p32 mode;
        positive length;
        p8 bytes[(LINK_GROUPS_MAX + 1) * sizeof(struct link_group_record)];
};

//      What is at the groups file, a link not followed: its kind and mode,
//      and the bytes of a plain file.
static fn wls_groups_look(struct wls_groups_look address_to look)
{
        file_facts facts;
        bipolar handle;

        memory_zero(look, sizeof(address_to look));
        if (!file_look_link(LINK_GROUPS_PATH, address_of facts))
                return;
        look->mode = (p32)facts.mode;
        look->length = (positive)facts.size;
        if ((facts.mode & MODE_FORMAT) != MODE_FILE)
                return;
        handle = system_open_at(AT_FDCWD, LINK_GROUPS_PATH,
                                FILE_READ | O_NOFOLLOW | O_CLOEXEC);
        if (handle >= 0)
        {
                (void)system_read_retry((positive)handle, look->bytes,
                                        sizeof look->bytes);
                system_close(handle);
        }
}

//      Three groups: two that stay and a code that has not run out.
static fn wls_groups_three(struct link_group_record address_to three)
{
        static const string_address names[3] = {"office", "lab", "kiosk"};

        memory_zero(three, 3 * sizeof(struct link_group_record));
        for (positive at = 0; at < 3; at++)
        {
                string_copy(three[at].namespace, names[at]);
                memory_fill(three[at].key, 0x41 + at, sizeof three[at].key);
                three[at].may = 2 + 2 * (p32)at;
        }
        three[2].flags = LINK_GROUP_ONCE;
        three[2].expires = link_boot_seconds() + 300;
}

enum { WLS_HOSTILE = 5 };

//      The groups file as it is when something has gone wrong with it.
static fn wls_groups_hostile(positive kind)
{
        struct link_group_record many[LINK_GROUPS_MAX + 1];

        wls_groups_three(many);
        memory_copy(many + 3, many, 3 * sizeof many[0]);
        memory_zero(many + 6, 3 * sizeof many[0]);
        (void)system_remove_at(AT_FDCWD, LINK_GROUPS_PATH, 0);
        (void)system_remove_at(AT_FDCWD, "/root/groups.elsewhere", 0);
        if (kind == 0)
                (void)wls_write(LINK_GROUPS_PATH, many,
                                3 * sizeof many[0] + 40, 0600);
        else if (kind == 1)
                (void)wls_write(LINK_GROUPS_PATH, many, 3 * sizeof many[0],
                                0644);
        else if (kind == 2)
                (void)wls_write(LINK_GROUPS_PATH, many, sizeof many, 0600);
        else if (kind == 3)
                (void)system_call_4(syscall(mknodat), (positive)(bipolar)AT_FDCWD,
                                    (positive)LINK_GROUPS_PATH, 0010600, 0);
        else
        {
                (void)wls_write("/root/groups.elsewhere", many,
                                3 * sizeof many[0], 0600);
                (void)system_symbolic_link_at("/root/groups.elsewhere",
                                              AT_FDCWD, LINK_GROUPS_PATH);
        }
}

//      Each is true when the change was refused, or has no answer to give.
static p8 wls_change_join(void)
{
        //      The secret is wiped where it was typed: it must be writable.
        p8 secret[] = "sesame-sesame";
        string_address words[] = {"office", (string_address)secret};

        return link_group_join(words, 2) != 0;
}

static p8 wls_change_leave(void)
{
        return link_group_leave("lab", false) != 0;
}

static p8 wls_change_open(void)
{
        p8 code[LINK_CODE_LENGTH] = {'a', 'b', 'c', 'd', 'e', 'f'};
        p32 mark = 0;

        return link_pair_open("gadget", code, null, address_of mark) < 0;
}

static p8 wls_change_taken_back(void)
{
        link_pair_close(0x12345678);
        return true;
}

static p8 wls_change_closed(void)
{
        wls_group();
        link_group_close(0);
        return true;
}

static p8 wls_change_expired(void)
{
        wls_group();
        link_nearby.groups.record[0].flags = LINK_GROUP_ONCE;
        link_nearby.groups.record[0].expires = 0;
        link_groups_expire();
        return true;
}

static fn groups_unreadable(void)
{
        static p8 (*const changes[])(void) = {
            wls_change_join,     wls_change_leave,  wls_change_open,
            wls_change_taken_back, wls_change_closed, wls_change_expired,
        };
        p8 held[array_count(changes)];
        struct wls_groups_look before, after;
        struct link_group_record three[3];
        link_groups groups;
        p8 want[2 * sizeof(struct link_group_record)];
        p8 code[LINK_CODE_LENGTH] = {'a', 'b', 'c', 'd', 'e', 'f'};
        p32 mark = 0;

        //      A directory where the switch's word goes and one where the
        //      key does: a join that went on would start a listener, and is
        //      stopped there instead.
        (void)system_make_directory_at(AT_FDCWD, LINK_SWITCH_PATH, 0700);
        (void)system_make_directory_at(AT_FDCWD, LINK_KEY_PATH, 0700);
        for (positive change = 0; change < array_count(changes); change++)
        {
                held[change] = true;
                for (positive kind = 0; kind < WLS_HOSTILE; kind++)
                {
                        p8 refused;

                        wls_groups_hostile(kind);
                        wls_groups_look(address_of before);
                        refused = changes[change]();
                        wls_groups_look(address_of after);
                        held[change] &= refused &&
                                        !memory_compare(address_of before,
                                                        address_of after,
                                                        sizeof before);
                }
        }
        check("sec: a group joined to a groups file that is cut short, "
              "readable by others, too large, a FIFO or a link is refused, "
              "and the file is as it was",
              held[0]);
        check("sec: and one left", held[1]);
        check("sec: and a pairing code made", held[2]);
        check("sec: and a pairing code taken back", held[3]);
        check("sec: and one closed after it took its machine", held[4]);
        check("sec: and the codes that ran out dropped by the listener",
              held[5]);

        //      The same changes where the file reads, and where it was never
        //      written, which is an empty list and no trouble.
        wls_groups_three(three);
        (void)system_remove_at(AT_FDCWD, LINK_GROUPS_PATH, 0);
        (void)system_remove_at(AT_FDCWD, "/root/groups.elsewhere", 0);
        check("a code made where no file was written is the one group",
              link_pair_open("gadget", code, null, address_of mark) == 0 &&
                      link_groups_load(address_of groups) && groups.count == 1 &&
                      string_equals(groups.record[0].namespace, "gadget") &&
                      (groups.record[0].flags & LINK_GROUP_ONCE));
        (void)wls_write(LINK_GROUPS_PATH, three, sizeof three, 0600);
        check("a group left is the one record gone",
              link_group_leave("lab", false) == 0 &&
                      link_groups_load(address_of groups) && groups.count == 2 &&
                      string_equals(groups.record[0].namespace, "office") &&
                      string_equals(groups.record[1].namespace, "kiosk"));
        check("a code made where the file reads is the third",
              link_pair_open("gadget", code, null, address_of mark) == 0 &&
                      mark != 0 && link_groups_load(address_of groups) &&
                      groups.count == 3 &&
                      string_equals(groups.record[2].namespace, "gadget"));
        link_pair_close(mark);
        check("and taken back it is gone, and nothing else",
              link_groups_load(address_of groups) && groups.count == 2 &&
                      string_equals(groups.record[0].namespace, "office") &&
                      string_equals(groups.record[1].namespace, "kiosk"));

        //      A record whose flags are not any the code sets is read as the
        //      record it is, and a change to another group keeps it whole.
        wls_groups_three(three);
        three[0].flags = 0xffffffffu;
        three[0].expires = ~(p64)0;
        (void)wls_write(LINK_GROUPS_PATH, three, sizeof three, 0600);
        memory_copy(want, three, sizeof three[0]);
        memory_copy(want + sizeof three[0], three + 2, sizeof three[2]);
        check("sec: a record with flags that are none of the code's is read",
              link_groups_load(address_of groups) && groups.count == 3 &&
                      groups.record[0].flags == 0xffffffffu);
        check("sec: and leaving another group keeps it byte for byte",
              link_group_leave("lab", false) == 0);
        wls_groups_look(address_of after);
        check("sec: with the group after it in its place",
              after.length == sizeof want &&
                      !memory_compare(after.bytes, want, sizeof want));

        (void)system_remove_at(AT_FDCWD, LINK_GROUPS_PATH, 0);
        (void)system_remove_at(AT_FDCWD, LINK_SWITCH_PATH, AT_REMOVEDIR);
        (void)system_remove_at(AT_FDCWD, LINK_KEY_PATH, AT_REMOVEDIR);
}

/*
        A listener that has groups keeps them when a look at the file fails
        for its own reasons -- here, no descriptor to open it with -- where
        it took none and closed its socket for the rest of its life, since a
        look is made once for each change of the file: the groups last read
        stay, and the next look asks again, even when the file is gone by
        then.
*/
static fn nearby_reload_failures(void)
{
        struct link_group_record record;
        positive limits[2] = {0, 0};
        positive tight[2];
        bipolar probe;

        memory_zero(address_of record, sizeof record);
        string_copy(record.namespace, "office");
        memory_fill(record.key, 0x5a, sizeof record.key);
        record.may = WATERLINK_MAY_RUN;

        memory_zero(address_of link_nearby, sizeof link_nearby);
        (void)system_remove_at(AT_FDCWD, LINK_GROUPS_PATH, 0);
        (void)wls_write(LINK_GROUPS_PATH, address_of record, sizeof record, 0600);
        link_nearby.socket = wls_quiet_socket();
        link_nearby_reload(10000000);

        //      The lowest descriptor there is, and no soft limit above it.
        probe = system_open_at(AT_FDCWD, "/dev/null", FILE_READ | O_CLOEXEC);
        if (probe >= 0)
                system_close(probe);
        (void)system_call_4(syscall(prlimit64), 0, 7, 0, (positive)address_of limits);
        tight[0] = probe >= 0 ? (positive)probe : limits[0];
        tight[1] = limits[1];

        record.may = WATERLINK_MAY_LOG;
        (void)wls_write(LINK_GROUPS_PATH, address_of record, sizeof record, 0600);
        (void)system_call_4(syscall(prlimit64), 0, 7, (positive)address_of tight, 0);
        link_nearby_reload(11000000);
        (void)system_call_4(syscall(prlimit64), 0, 7, (positive)address_of limits, 0);
        check("sec: a groups file that could not be opened leaves the groups "
              "last read, and the socket",
              link_nearby.groups.count == 1 &&
                      link_nearby.groups.record[0].may == WATERLINK_MAY_RUN &&
                      link_nearby.socket >= 0);
        link_nearby_reload(11600000);
        check("and the next look reads it",
              link_nearby.groups.count == 1 &&
                      link_nearby.groups.record[0].may == WATERLINK_MAY_LOG);

        record.may = WATERLINK_MAY_FILES;
        (void)wls_write(LINK_GROUPS_PATH, address_of record, sizeof record, 0600);
        (void)system_call_4(syscall(prlimit64), 0, 7, (positive)address_of tight, 0);
        link_nearby_reload(12200000);
        (void)system_call_4(syscall(prlimit64), 0, 7, (positive)address_of limits, 0);
        (void)system_remove_at(AT_FDCWD, LINK_GROUPS_PATH, 0);
        link_nearby_reload(12800000);
        check("a file that is gone after a look that failed is no groups, "
              "and closes the socket",
              link_nearby.groups.count == 0 && link_nearby.socket < 0);
        link_nearby_close();
}

/*
        The name this machine offers is its host name in lowercase, as far as
        it reads as one a member would keep, and a plain "machine" where it
        does not start as one.
*/
static fn machine_names(void)
{
        static const struct
        {
                string_address host;
                string_address name;
        } cases[] = {
            {"Box", "box"},
            {"MY-Box.local", "my-box"},
            {"a_b", "a_b"},
            {"9lives", "9lives"},
            {"-x", "machine"},
            {"_x", "machine"},
            {".local", "machine"},
            {"UPPER_Case9", "upper_case9"},
            {"abcdefghijklmnopqrstuvwxyz0123456789ABCDEF",
             "abcdefghijklmnopqrstuvwxyz01234"},
            {"x y", "x"},
            {"\xc3\xbcx", "machine"},
        };
        p8 name[WATERLINK_NAME_MAX];
        positive wrong = 0;

        if (system_call_2(syscall(sethostname), (positive) "wls-host", 8) < 0)
                return;
        link_machine_name(name);
        check("a host name is the name", string_equals((string_address)name,
                                                       "wls-host"));
        for (positive at = 0; at < array_count(cases); at++)
        {
                memory_fill(name, 0xa5, sizeof name);
                (void)system_call_2(syscall(sethostname),
                                    (positive)cases[at].host,
                                    string_length(cases[at].host) < 64
                                            ? string_length(cases[at].host)
                                            : 64);
                link_machine_name(name);
                wrong += !string_equals((string_address)name, cases[at].name) ||
                         memory_span_byte(name +
                                                  string_length((string_address)name),
                                          0,
                                          WATERLINK_NAME_MAX -
                                                  string_length((string_address)name)) !=
                                 WATERLINK_NAME_MAX -
                                         string_length((string_address)name);
        }
        check("sec: it is the host name lowercased as far as it reads as one, "
              "whole to the end of its room, and machine where it does not",
              wrong == 0);
}

//      The records of a file keep the names that could have been paired, and
//      the last record takes the place of one that could not (the peers and
//      the groups read their files through the same walk).
static fn records_named(void)
{
        struct waterlink_peer peer[4];
        positive count = 4;
        positive at = __builtin_offsetof(struct waterlink_peer, name);
        static const string_address names[4] = {"alice", "pair", "bob", "-x"};

        memory_zero(peer, sizeof peer);
        for (positive i = 0; i < 4; i++)
                string_copy((string_address)peer[i].name, names[i]);
        peer[2].may = 6;
        link_records_named((p8 address_to)peer, address_of count,
                           sizeof peer[0], at, WATERLINK_NAME_MAX);
        check("sec: a record whose name is a word of the command or starts with a "
              "dash is dropped and the last in its place, the one that takes "
              "its place judged too",
              count == 2 && string_equals((string_address)peer[0].name, "alice") &&
                      string_equals((string_address)peer[1].name, "bob") &&
                      peer[1].may == 6);

        count = 2;
        string_copy((string_address)peer[1].name, "pair");
        link_records_named((p8 address_to)peer, address_of count,
                           sizeof peer[0], at, WATERLINK_NAME_MAX);
        check("sec: a last record that is no name is dropped and nothing is "
              "moved over itself",
              count == 1 && string_equals((string_address)peer[0].name, "alice"));

        count = 3;
        string_copy((string_address)peer[0].name, "a b");
        string_copy((string_address)peer[1].name, "");
        string_copy((string_address)peer[2].name, "x\ny");
        link_records_named((p8 address_to)peer, address_of count,
                           sizeof peer[0], at, WATERLINK_NAME_MAX);
        check("sec: records that are all no name leave none", count == 0);

        memory_zero(peer, sizeof peer);
        memory_fill(peer[0].name, 'a', sizeof peer[0].name);
        string_copy((string_address)peer[1].name, "next");
        count = 2;
        link_records_named((p8 address_to)peer, address_of count,
                           sizeof peer[0], at, WATERLINK_NAME_MAX);
        check("sec: a name with no end is cut at its room, kept, and the "
              "record beside it is not touched",
              count == 2 && peer[0].name[WATERLINK_NAME_MAX - 1] == 0 &&
                      string_length((string_address)peer[0].name) ==
                              WATERLINK_NAME_MAX - 1 &&
                      string_equals((string_address)peer[1].name, "next"));

        count = 0;
        link_records_named((p8 address_to)peer, address_of count,
                           sizeof peer[0], at, WATERLINK_NAME_MAX);
        check("sec: no records, none made", count == 0);
}

//      Discovery labels are published only when every one was drawn.
static fn labels(void)
{
        wls_group();
        entropy_down = true;
        check("sec: with no entropy the labels are not made",
              !link_nearby_labels() && !link_nearby.labels_ready);
        entropy_down = false;
        check("sec: and with it back they are",
              link_nearby_labels() && link_nearby.labels_ready);
}

//      WPA's pre-shared key, made through the shared PBKDF2 (IEEE 802.11i).
/* The access point's frames are the ones answered: a frame on the EAPOL
   socket from any other address is dropped. */
static fn wifi_source_checks(void)
{
        wifi_link link;
        socket_address_packet from;

        memory_fill(address_of link, 0, sizeof(link));
        memory_fill(address_of from, 0, sizeof(from));
        memory_copy(link.bssid, "\x02\x00\x00\x00\x01\x00", 6);
        from.family = AF_PACKET;
        from.halen = 6;
        memory_copy(from.addr, "\x02\x00\x00\x00\x01\x00", 6);
        check("an EAPOL frame from the access point's address is taken",
              wifi_eapol_from(address_of link, address_of from, sizeof(from)));
        from.addr[5] = 1;
        check("one from another address is not", !wifi_eapol_from(address_of link, address_of from, sizeof(from)));
        from.addr[5] = 0;
        from.halen = 4;
        check("one whose hardware address is not six bytes is not",
              !wifi_eapol_from(address_of link, address_of from, sizeof(from)));
        from.halen = 6;
        check("the kernel's 18 bytes of it are enough", wifi_eapol_from(address_of link, address_of from, 18));
        check("one the kernel gave no address for is not", !wifi_eapol_from(address_of link, address_of from, 4));
        from.halen = 6;
        for (p8 kind = 1; kind < 5; kind++)
        {
                from.pkttype = kind;
                check("one to everyone, to a group, to another station or from this one is not taken",
                      !wifi_eapol_from(address_of link, address_of from, sizeof(from)));
        }
}

static fn wifi_air_capacity(void)
{
        radio_air air;
        radio_heard one;
        p8 real = false;
        p8 weak = false;
        p8 joined = false;

        memory_zero(address_of air, sizeof air);
        for (positive at = 0; at < RADIO_AIR_MOST; at++)
        {
                memory_zero(address_of one, sizeof one);
                one.ssid[0] = 'x';
                one.ssid[1] = (p8)(at + 1);
                one.ssid_length = 2;
                one.mbm = -9000 + (b32)at;
                radio_air_keep(address_of air, address_of one);
        }
        memory_zero(address_of one, sizeof one);
        memory_copy(one.ssid, "real", 4);
        one.ssid_length = 4;
        one.mbm = -3000;
        radio_air_keep(address_of air, address_of one);
        memory_zero(address_of one, sizeof one);
        memory_copy(one.ssid, "weak", 4);
        one.ssid_length = 4;
        one.mbm = -10000;
        radio_air_keep(address_of air, address_of one);
        memory_zero(address_of one, sizeof one);
        memory_copy(one.ssid, "joined", 6);
        one.ssid_length = 6;
        one.mbm = -11000;
        one.joined = true;
        radio_air_keep(address_of air, address_of one);
        for (positive at = 0; at < air.count; at++)
        {
                real |= air.heard[at].ssid_length == 4 &&
                        !memory_compare(air.heard[at].ssid, "real", 4);
                weak |= air.heard[at].ssid_length == 4 &&
                        !memory_compare(air.heard[at].ssid, "weak", 4);
                joined |= air.heard[at].ssid_length == 6 &&
                          !memory_compare(air.heard[at].ssid, "joined", 6);
        }
        check("sec: weak forged SSIDs cannot make a full scan hide a stronger network",
              air.count == RADIO_AIR_MOST && real && !weak);
        check("sec: a joined network survives the bounded scan even when weakest",
              joined);

        /* The joined bit and the metadata must come from the same BSSID. A
           louder twin by the same name used to lend its OPEN classification,
           frequency and address to the weaker associated row. */
        memory_zero(address_of air, sizeof air);
        memory_zero(address_of one, sizeof one);
        memory_copy(one.ssid, "same", 4);
        one.ssid_length = 4;
        one.mbm = -2000;
        one.security = RADIO_OPEN;
        one.frequency = 2412;
        memory_copy(one.bssid, "\x02\x00\x00\x00\x00\x01", 6);
        radio_air_keep(address_of air, address_of one);
        one.mbm = -7000;
        one.security = RADIO_WPA2;
        one.frequency = 5180;
        one.joined = true;
        memory_copy(one.bssid, "\x02\x00\x00\x00\x00\x02", 6);
        radio_air_keep(address_of air, address_of one);
        check("sec: association and security metadata stay on the same BSSID",
              air.count == 1 && air.heard[0].joined &&
                  air.heard[0].security == RADIO_WPA2 &&
                  air.heard[0].frequency == 5180 &&
                  !memory_compare(air.heard[0].bssid,
                                  "\x02\x00\x00\x00\x00\x02", 6));
        one.mbm = -1000;
        one.security = RADIO_OPEN;
        one.frequency = 2462;
        one.joined = false;
        memory_copy(one.bssid, "\x02\x00\x00\x00\x00\x03", 6);
        radio_air_keep(address_of air, address_of one);
        check("sec: a louder unassociated twin cannot rewrite the joined row",
              air.heard[0].joined && air.heard[0].security == RADIO_WPA2 &&
                  air.heard[0].frequency == 5180 &&
                  !memory_compare(air.heard[0].bssid,
                                  "\x02\x00\x00\x00\x00\x02", 6));
}

static fn wpa_key(void)
{
        p8 pmk[32];
        static const p8 ieee[32] = {
                0xf4, 0x2c, 0x6f, 0xc5, 0x2d, 0xf0, 0xeb, 0xef, 0x9e, 0xbb, 0x4b,
                0x90, 0xb3, 0x8a, 0x5f, 0x90, 0x2e, 0x83, 0xfe, 0x1b, 0x13, 0x5a,
                0x70, 0xe2, 0x3a, 0xed, 0x76, 0x2e, 0x97, 0x10, 0xa1, 0x2e};
        static const p8 this_is[32] = {
                0x0d, 0xc0, 0xd6, 0xeb, 0x90, 0x55, 0x5e, 0xd6, 0x41, 0x97, 0x56,
                0xb9, 0xa1, 0x5e, 0xc3, 0xe3, 0x20, 0x9b, 0x63, 0xdf, 0x70, 0x7d,
                0xd5, 0x08, 0xd1, 0x45, 0x81, 0xf8, 0x98, 0x27, 0x21, 0xaf};

        check("the WPA key of \"password\" on IEEE (802.11i)",
              wifi_psk((p8 address_to) "IEEE", 4, (p8 address_to) "password", 8,
                       pmk) &&
                      !memory_compare(pmk, ieee, 32));
        check("the WPA key of \"ThisIsAPassword\" on ThisIsASSID (802.11i)",
              wifi_psk((p8 address_to) "ThisIsASSID", 11,
                       (p8 address_to) "ThisIsAPassword", 15, pmk) &&
                      !memory_compare(pmk, this_is, 32));
}

/*
        WPA3 and the key frames of descriptor version 3, against values made
        by a reference written from the standard in Python (every SAE
        step, from the hunt for the password element to the confirm) and, for
        the CMAC, against RFC 4493's own. The password element is found for
        each password, the commit taken in gives the KCK, the PMK, the PMKID
        and the confirms, and a commit built here must satisfy
        scalar * PWE + element = rand * PWE. The malformed commits, out of
        range or off the curve or our own sent back, are refused.
*/
static const p8 sae0_pwe[64] = {0x6b, 0x79, 0x1a, 0x30, 0xa9, 0xdc, 0xb6, 0xc3, 0x98, 0x79, 0x52, 0xf8, 0xfc, 0xdb, 0xd6, 0xcc, 0xf8, 0xfe, 0x5d, 0xf5, 0xe3, 0x70, 0xd7, 0x0d, 0x65, 0xfb, 0x0d, 0x98, 0xf9, 0x7f, 0xe8, 0x18, 0x63, 0x1b, 0xac, 0xff, 0x14, 0xf7, 0x0e, 0x78, 0x74, 0x99, 0xc2, 0x7c, 0x0a, 0x74, 0x1e, 0x36, 0x58, 0xf3, 0xf6, 0xb2, 0xf2, 0x74, 0x5a, 0xd1, 0xc1, 0xac, 0x14, 0xa1, 0xbf, 0xf9, 0x10, 0x6c};
static const p8 sae0_rand[32] = {0xf7, 0xc9, 0xe2, 0xb3, 0x4f, 0xb9, 0x24, 0x28, 0xd4, 0x94, 0xae, 0xb1, 0x51, 0x86, 0xe1, 0xf4, 0x58, 0xa9, 0x2d, 0xa4, 0x8c, 0xf7, 0x10, 0x60, 0x27, 0xda, 0x8b, 0x52, 0x8c, 0x74, 0x09, 0x90};
static const p8 sae0_scalar[32] = {0xb7, 0xff, 0x4e, 0xa3, 0xfa, 0xdb, 0x69, 0x40, 0x2e, 0x73, 0x44, 0x50, 0xfb, 0x1f, 0x0d, 0xcb, 0x7a, 0x0a, 0xfe, 0x67, 0xcc, 0x78, 0x27, 0x01, 0x33, 0xee, 0xb1, 0x58, 0xbf, 0x68, 0x1a, 0xaf};
static const p8 sae0_element[64] = {0xb5, 0xeb, 0x84, 0x93, 0x2b, 0x12, 0x5b, 0xb1, 0x73, 0x91, 0x68, 0x42, 0xa3, 0x28, 0xe5, 0xe6, 0x78, 0x5f, 0x8b, 0xc6, 0xd4, 0x4b, 0x20, 0x2d, 0xfb, 0x42, 0xa8, 0x71, 0xca, 0xbd, 0xb0, 0x28, 0x69, 0x53, 0xa6, 0x8e, 0x29, 0x54, 0x70, 0xde, 0xa2, 0x4a, 0xe1, 0x1e, 0x15, 0x35, 0x00, 0x1f, 0xe4, 0x5b, 0x99, 0x98, 0xd3, 0xab, 0x78, 0x3e, 0x96, 0x19, 0xd6, 0x6c, 0xd6, 0x2a, 0xde, 0x73};
static const p8 sae0_peer_scalar[32] = {0xb8, 0x30, 0xa1, 0x9e, 0x54, 0x8e, 0x1d, 0x28, 0xfc, 0x5b, 0xe0, 0xf6, 0xce, 0x53, 0x70, 0xe0, 0x5b, 0xba, 0x84, 0x1d, 0x95, 0xd7, 0xd0, 0x51, 0xd0, 0xfd, 0xbc, 0x3d, 0xb0, 0xc7, 0x2f, 0x11};
static const p8 sae0_peer_element[64] = {0x30, 0x6f, 0xd9, 0x47, 0xa5, 0x80, 0xca, 0x60, 0x7c, 0xf9, 0x47, 0x85, 0xc5, 0xe7, 0x95, 0x90, 0xbe, 0x6f, 0x75, 0x1d, 0x53, 0x37, 0xbb, 0x38, 0xeb, 0xc1, 0xbf, 0x56, 0xf4, 0xc2, 0x38, 0x5e, 0x19, 0x62, 0x13, 0x85, 0xa5, 0x84, 0xb0, 0x20, 0xfa, 0x97, 0x87, 0x9c, 0x00, 0xe1, 0x82, 0xb9, 0x9b, 0xe2, 0xea, 0x02, 0x32, 0xb9, 0x25, 0x4a, 0x9d, 0x27, 0x97, 0xf3, 0x4e, 0x8b, 0x73, 0x44};
static const p8 sae0_kck[32] = {0x8a, 0x72, 0x79, 0xfd, 0x98, 0x2d, 0xcc, 0x65, 0xdc, 0xca, 0xb4, 0x60, 0x52, 0xc3, 0x0b, 0xb4, 0xe9, 0xb0, 0xd6, 0x09, 0x60, 0x56, 0x09, 0x45, 0x31, 0x86, 0x5b, 0x01, 0xf3, 0xb4, 0x95, 0x86};
static const p8 sae0_pmk[32] = {0xff, 0xe0, 0x94, 0x47, 0xc7, 0x2c, 0x92, 0x39, 0xba, 0xa7, 0x88, 0x2b, 0x82, 0xef, 0x5d, 0x1f, 0x7b, 0x52, 0xc2, 0xfd, 0x93, 0x8b, 0x27, 0xa4, 0x4e, 0x2b, 0x62, 0x8c, 0xb5, 0x93, 0xa8, 0x3a};
static const p8 sae0_pmkid[16] = {0x70, 0x2f, 0xf0, 0x43, 0x4f, 0x69, 0x86, 0x68, 0x2a, 0xcf, 0x25, 0x47, 0xc9, 0x72, 0x7e, 0xac};
static const p8 sae0_confirm[32] = {0x5b, 0x7b, 0x31, 0x79, 0x99, 0x3f, 0xe3, 0x8e, 0x01, 0x87, 0x46, 0x99, 0xef, 0x13, 0x66, 0x7e, 0x46, 0xff, 0x60, 0xde, 0xd0, 0x33, 0x99, 0x53, 0xaf, 0xde, 0x07, 0xd5, 0x0a, 0x35, 0x03, 0x9b};
static const p8 sae0_peer_confirm[32] = {0x7c, 0x3b, 0xea, 0x76, 0x1a, 0xb1, 0x1b, 0xa2, 0x5a, 0x14, 0xd9, 0x1f, 0xa8, 0x06, 0x2b, 0x80, 0xf0, 0xc2, 0x18, 0x71, 0x09, 0xce, 0x69, 0x74, 0xf5, 0xa5, 0xa3, 0x7c, 0x87, 0x62, 0x05, 0x11};
static const p8 sae1_pwe[64] = {0x33, 0xcb, 0x00, 0x39, 0x98, 0x8a, 0xf7, 0xb2, 0xc4, 0x51, 0xd5, 0x83, 0x03, 0x12, 0x09, 0xfa, 0xa4, 0x2f, 0x3d, 0xc8, 0x54, 0xd0, 0xb0, 0x7d, 0x19, 0x38, 0xe8, 0x0a, 0xeb, 0x45, 0x08, 0x49, 0xa9, 0x36, 0x11, 0x26, 0x28, 0x36, 0xa9, 0xea, 0xe0, 0xee, 0xca, 0xe6, 0x1b, 0x76, 0xb5, 0x9b, 0xfd, 0x5f, 0xa6, 0xaf, 0xda, 0xe1, 0x8e, 0xdd, 0xd7, 0x1f, 0x87, 0x55, 0x7f, 0xae, 0x61, 0x07};
static const p8 sae1_rand[32] = {0xe9, 0x7b, 0xda, 0x0b, 0x84, 0x04, 0x5d, 0x1f, 0x80, 0xa7, 0x24, 0x5c, 0x28, 0x59, 0x2a, 0xb6, 0x82, 0x64, 0xe4, 0x6b, 0x8b, 0x13, 0xc6, 0x0a, 0x7c, 0x28, 0x63, 0x81, 0xfc, 0xdc, 0x1f, 0x54};
static const p8 sae1_scalar[32] = {0xde, 0x19, 0xc2, 0x8c, 0x0b, 0x03, 0x30, 0x93, 0x08, 0x47, 0x44, 0x8b, 0x79, 0xb3, 0x0c, 0x75, 0x19, 0x5d, 0xb9, 0xd1, 0xd1, 0x01, 0xcd, 0xdd, 0x4e, 0x7f, 0x30, 0x4e, 0x0d, 0x5b, 0x0b, 0x62};
static const p8 sae1_element[64] = {0x6a, 0x80, 0x5a, 0xee, 0x75, 0xed, 0x03, 0x66, 0x28, 0xdd, 0x4a, 0x2b, 0x1a, 0xc1, 0x5c, 0xf8, 0x68, 0xeb, 0xb8, 0x3d, 0x0c, 0x15, 0xa0, 0x11, 0xa1, 0x7c, 0x6e, 0xd6, 0xc7, 0x1b, 0x31, 0xc5, 0xe7, 0xb1, 0x87, 0x8f, 0x12, 0x60, 0xf8, 0xf6, 0x47, 0x20, 0x99, 0xc6, 0xbf, 0x85, 0x8c, 0x74, 0xb8, 0x25, 0x52, 0x26, 0xdd, 0xc6, 0xfc, 0xe9, 0x7b, 0x5c, 0x61, 0x35, 0x2e, 0x78, 0xf1, 0xb0};
static const p8 sae1_peer_scalar[32] = {0x55, 0xb6, 0x00, 0x3e, 0x5e, 0x8f, 0x8d, 0x3f, 0xdf, 0xa5, 0xd8, 0x48, 0x04, 0x26, 0x97, 0x46, 0x95, 0xbf, 0x02, 0xf3, 0xea, 0xa1, 0xe3, 0x81, 0xf1, 0x92, 0xb3, 0x62, 0x53, 0x0a, 0xae, 0xb4};
static const p8 sae1_peer_element[64] = {0xb2, 0xcf, 0x72, 0xb0, 0xbc, 0x05, 0xdb, 0xf9, 0xea, 0x28, 0x92, 0xdc, 0xff, 0x40, 0xcc, 0xc9, 0xba, 0x3b, 0x2b, 0x20, 0x32, 0xd7, 0xab, 0x57, 0x1a, 0x8b, 0xa8, 0xac, 0x2a, 0x54, 0xff, 0x1a, 0xa9, 0x3d, 0xcd, 0x3f, 0xba, 0x73, 0x51, 0xdf, 0x9c, 0x31, 0xd5, 0xf2, 0xbe, 0xe1, 0xb7, 0xaa, 0xd3, 0xf1, 0x22, 0xa3, 0xb6, 0xa2, 0xd3, 0x2c, 0x2d, 0x4f, 0x55, 0xf8, 0x01, 0x65, 0x62, 0x8e};
static const p8 sae1_kck[32] = {0x14, 0x55, 0xfa, 0x30, 0xd0, 0xfa, 0xfc, 0x3f, 0xca, 0x12, 0xa2, 0xa6, 0x76, 0xc6, 0xf5, 0x1f, 0xc1, 0x4e, 0x07, 0x48, 0x6d, 0xad, 0xd4, 0xf0, 0x8f, 0xbf, 0x80, 0x37, 0x19, 0xd7, 0xc4, 0x3a};
static const p8 sae1_pmk[32] = {0xce, 0x92, 0x8d, 0x71, 0x70, 0x50, 0x32, 0x23, 0xa9, 0x7a, 0x2d, 0xa1, 0xb5, 0x20, 0x43, 0xcb, 0xb7, 0x16, 0xd3, 0x6a, 0x3a, 0x6f, 0x15, 0x7f, 0x3d, 0xfb, 0x77, 0x2e, 0x7e, 0xa3, 0x96, 0x06};
static const p8 sae1_pmkid[16] = {0x33, 0xcf, 0xc2, 0xcb, 0x69, 0x92, 0xbd, 0xd1, 0xe7, 0xed, 0x1c, 0xd3, 0x7d, 0xd9, 0xa3, 0xbb};
static const p8 sae1_confirm[32] = {0x8c, 0xd0, 0x01, 0xc6, 0xf7, 0x7c, 0x88, 0xab, 0xf2, 0x4f, 0x11, 0x34, 0x37, 0xdf, 0x81, 0xec, 0x5d, 0x71, 0x60, 0x48, 0xc0, 0x85, 0x53, 0xb2, 0x1f, 0x94, 0x6d, 0xa4, 0x49, 0x7d, 0xfc, 0xec};
static const p8 sae1_peer_confirm[32] = {0xc4, 0x4d, 0xc5, 0xfe, 0x7f, 0xc8, 0xf1, 0xab, 0x56, 0xa1, 0xf4, 0xe3, 0x6e, 0x1c, 0x44, 0x93, 0x99, 0x65, 0x4f, 0x36, 0x86, 0x9d, 0x38, 0x0c, 0x97, 0xa2, 0x76, 0x98, 0x4f, 0x5e, 0xdb, 0xe9};
static const p8 sae2_pwe[64] = {0xc2, 0x8f, 0xac, 0xcf, 0x97, 0x5a, 0x8e, 0x0c, 0x91, 0x11, 0x23, 0xf1, 0x4d, 0x3f, 0x65, 0xb6, 0x58, 0xec, 0x84, 0x7a, 0x57, 0x3e, 0xb0, 0xce, 0x39, 0x7c, 0xf3, 0x75, 0x0c, 0x33, 0x95, 0x19, 0x42, 0x54, 0x71, 0x97, 0xe6, 0x98, 0xe5, 0xa0, 0xce, 0x5d, 0x5b, 0x2f, 0xfe, 0x31, 0xa9, 0xa8, 0x31, 0xea, 0xd2, 0x88, 0x8f, 0xbc, 0xb6, 0xec, 0xbc, 0x6e, 0x48, 0xd6, 0x96, 0x30, 0x66, 0x4c};
static const p8 sae2_rand[32] = {0x79, 0x4a, 0xb1, 0xa1, 0xb0, 0xf3, 0xdd, 0x97, 0xcf, 0x4d, 0x81, 0x41, 0x06, 0x0a, 0x1b, 0x5b, 0x5f, 0x70, 0xb9, 0xe3, 0x8b, 0x45, 0x62, 0xe8, 0x0d, 0x20, 0x39, 0x97, 0x70, 0xf7, 0xd3, 0x3b};
static const p8 sae2_scalar[32] = {0x7c, 0x93, 0x46, 0xa2, 0x3a, 0xdb, 0x06, 0xa9, 0x3f, 0x27, 0x00, 0x49, 0x62, 0xd9, 0xfa, 0xf1, 0xa8, 0x65, 0x8b, 0x24, 0xf8, 0x21, 0xdf, 0x50, 0x21, 0x0a, 0xd0, 0x57, 0x7e, 0x67, 0x24, 0x52};
static const p8 sae2_element[64] = {0x9a, 0xee, 0x1f, 0xea, 0x01, 0x2d, 0x8a, 0x5e, 0x9a, 0xf8, 0x89, 0x04, 0xc8, 0x12, 0x6d, 0x76, 0x88, 0xbb, 0xf0, 0x11, 0x9d, 0xa9, 0x20, 0x9b, 0xbe, 0xf3, 0xf5, 0xae, 0xbb, 0x03, 0x14, 0xf0, 0x86, 0xac, 0xeb, 0x86, 0xf4, 0x7e, 0x8e, 0x3b, 0xe6, 0x72, 0xdc, 0xb4, 0x51, 0x76, 0x83, 0xe6, 0xb5, 0x83, 0xfc, 0xf4, 0x96, 0x18, 0x9e, 0x9f, 0x06, 0xc2, 0xee, 0x1f, 0x2a, 0x87, 0x3e, 0x4c};
static const p8 sae2_peer_scalar[32] = {0x80, 0x90, 0x5d, 0x81, 0x94, 0x9d, 0x9b, 0x8a, 0x8f, 0x3e, 0xc3, 0x30, 0x1a, 0x83, 0x2b, 0xa5, 0xaf, 0x88, 0xa7, 0x09, 0xe9, 0xde, 0xfa, 0x85, 0x98, 0x2b, 0x11, 0x2a, 0x0a, 0x20, 0xd1, 0xbc};
static const p8 sae2_peer_element[64] = {0x53, 0xba, 0xe1, 0xc2, 0xb8, 0xf7, 0x20, 0xf2, 0xe0, 0x71, 0x49, 0xb3, 0xa1, 0x02, 0xf0, 0x90, 0x80, 0xe7, 0xbd, 0xf2, 0x3e, 0xc3, 0x02, 0x4f, 0x9a, 0x8c, 0xb2, 0xa0, 0x0e, 0x16, 0x62, 0x3b, 0x2f, 0x15, 0xd9, 0x77, 0x1c, 0x0b, 0xd7, 0x5e, 0x3e, 0xe0, 0xac, 0x12, 0x23, 0x75, 0xbd, 0x86, 0x83, 0x23, 0xec, 0x48, 0x35, 0x30, 0x5e, 0x8d, 0x7e, 0x80, 0x94, 0x78, 0xf8, 0x66, 0x0a, 0x55};
static const p8 sae2_kck[32] = {0x05, 0xbf, 0xef, 0xbb, 0xa9, 0xf1, 0xb1, 0x51, 0x1a, 0x6c, 0xc7, 0x17, 0x97, 0xa1, 0x92, 0x11, 0x5a, 0xba, 0x05, 0x78, 0x73, 0x5b, 0xbb, 0xb5, 0xb1, 0xcf, 0x99, 0x40, 0x5e, 0xf4, 0xf9, 0x94};
static const p8 sae2_pmk[32] = {0xfd, 0xe3, 0x80, 0x5c, 0x91, 0xd9, 0x1c, 0xc7, 0xcb, 0x06, 0x58, 0x9a, 0x9c, 0x41, 0x5a, 0x45, 0x64, 0x87, 0x8b, 0x86, 0x66, 0x58, 0x81, 0x7b, 0xd4, 0x1e, 0x4a, 0xd7, 0xd3, 0xf3, 0x06, 0x88};
static const p8 sae2_pmkid[16] = {0xfd, 0x23, 0xa4, 0x23, 0xcf, 0x78, 0xa2, 0x33, 0xce, 0x65, 0xc3, 0x79, 0x7d, 0x5d, 0x26, 0x97};
static const p8 sae2_confirm[32] = {0x78, 0x43, 0x83, 0x70, 0x28, 0xff, 0x91, 0x36, 0xe6, 0x58, 0xdf, 0xcb, 0x9b, 0x03, 0x39, 0x8c, 0x1c, 0x59, 0x69, 0x85, 0xe4, 0x6a, 0x6b, 0x4a, 0xd9, 0x80, 0xc8, 0x44, 0x1e, 0xec, 0x04, 0xbe};
static const p8 sae2_peer_confirm[32] = {0x05, 0x48, 0x9b, 0x4f, 0x43, 0xd7, 0x20, 0xf4, 0x1f, 0xb0, 0x0e, 0xe3, 0x06, 0xe8, 0xf1, 0x5d, 0x01, 0x9e, 0x27, 0x6b, 0xff, 0x67, 0x13, 0x9e, 0xc1, 0xd7, 0x00, 0x95, 0x72, 0x19, 0xbd, 0x42};
static const p8 sae3_pwe[64] = {0x66, 0x72, 0x57, 0xe7, 0x41, 0x35, 0xd8, 0xf7, 0x91, 0x57, 0xf8, 0x15, 0x79, 0x69, 0x4d, 0xc2, 0x23, 0xb6, 0xd8, 0x1d, 0x23, 0xfb, 0xf8, 0x52, 0xcd, 0xf9, 0xb4, 0x15, 0xb9, 0x21, 0x41, 0xc6, 0x3e, 0xf6, 0x35, 0x04, 0x13, 0x32, 0xef, 0xd0, 0xc8, 0xf1, 0xb1, 0xa9, 0x61, 0x53, 0xf9, 0x8c, 0x7b, 0xf2, 0x9c, 0xf3, 0xde, 0xb8, 0x41, 0xa4, 0x5e, 0x4a, 0xd9, 0x68, 0x0e, 0x3c, 0xb4, 0x80};
static const p8 sae3_rand[32] = {0x11, 0xc0, 0xac, 0xa2, 0xb8, 0x48, 0x6b, 0x24, 0xbf, 0x53, 0x34, 0x80, 0xc3, 0x31, 0x22, 0xf4, 0x7b, 0x5f, 0x25, 0x46, 0xe8, 0x40, 0x29, 0x92, 0x17, 0x88, 0xac, 0xd2, 0xe3, 0x90, 0xb6, 0xa2};
static const p8 sae3_scalar[32] = {0xc6, 0x51, 0xf1, 0x64, 0x70, 0x4c, 0x0b, 0x8f, 0xfc, 0x04, 0x7c, 0xb7, 0x1b, 0xda, 0x66, 0x6a, 0xb3, 0xb7, 0x30, 0xd9, 0x5f, 0x27, 0x63, 0x3d, 0x85, 0x70, 0x97, 0x29, 0x2f, 0x3f, 0xf1, 0x1e};
static const p8 sae3_element[64] = {0x7e, 0x2c, 0x21, 0x6d, 0x0a, 0xae, 0x76, 0x07, 0x25, 0x35, 0xb7, 0x66, 0xc2, 0x7a, 0x50, 0xf6, 0x79, 0x6c, 0x3e, 0x4f, 0x3c, 0x93, 0x35, 0x3c, 0xaa, 0x0a, 0x34, 0x0c, 0x59, 0xce, 0x3e, 0x19, 0x46, 0xbc, 0xd9, 0x60, 0x21, 0x61, 0xdd, 0x2f, 0xc4, 0x4f, 0xab, 0xa5, 0x4e, 0x7a, 0x61, 0xb6, 0x7a, 0x00, 0x99, 0x7c, 0x7d, 0x90, 0x6a, 0x42, 0xfb, 0x50, 0xef, 0x09, 0x19, 0x90, 0xd3, 0xbe};
static const p8 sae3_peer_scalar[32] = {0x16, 0x91, 0x16, 0xf8, 0xc9, 0xd5, 0x05, 0x8a, 0x53, 0xda, 0x85, 0x80, 0xa0, 0x5f, 0xd4, 0x0e, 0x0c, 0xa0, 0x63, 0xf5, 0x5c, 0x04, 0x29, 0xb2, 0x5b, 0x0d, 0xa0, 0x5a, 0x3a, 0x95, 0x17, 0x90};
static const p8 sae3_peer_element[64] = {0x21, 0x28, 0x3c, 0x73, 0xc2, 0xf7, 0xdd, 0xfb, 0xda, 0x7a, 0x4e, 0xf0, 0x14, 0xff, 0xbf, 0x80, 0xc0, 0x9b, 0x15, 0x27, 0x22, 0x03, 0xba, 0xf0, 0xbd, 0x65, 0x79, 0x26, 0xd5, 0x7e, 0x34, 0xef, 0xec, 0x09, 0xee, 0x02, 0x77, 0x69, 0xc9, 0x7d, 0x0f, 0xb9, 0x5a, 0x30, 0x3f, 0x58, 0x8d, 0x02, 0xde, 0x95, 0x7a, 0xfa, 0xfc, 0x81, 0xad, 0x00, 0xd3, 0xfb, 0xb4, 0xf9, 0x9a, 0x07, 0x08, 0xdc};
static const p8 sae3_kck[32] = {0xad, 0xf1, 0x4e, 0xfd, 0xe4, 0x4c, 0xd8, 0xb7, 0x3b, 0xa8, 0x2c, 0xfa, 0x30, 0xfa, 0x6d, 0x2c, 0x8a, 0x41, 0x33, 0x51, 0x4b, 0x1e, 0x5a, 0xc5, 0xf6, 0x57, 0x0f, 0xf9, 0x22, 0x4f, 0x80, 0x98};
static const p8 sae3_pmk[32] = {0x2e, 0x95, 0x53, 0xca, 0xc7, 0x33, 0x28, 0x1e, 0x0c, 0x78, 0x37, 0xca, 0xa3, 0x22, 0x5f, 0x40, 0x38, 0xa7, 0x35, 0x6c, 0x61, 0x34, 0xea, 0x00, 0x95, 0x0a, 0x88, 0xfb, 0xcc, 0x66, 0xe6, 0x7c};
static const p8 sae3_pmkid[16] = {0xdc, 0xe3, 0x08, 0x5d, 0x3a, 0x21, 0x11, 0x1a, 0x4f, 0xdf, 0x02, 0x37, 0xbc, 0x3a, 0x3a, 0x78};
static const p8 sae3_confirm[32] = {0x6b, 0x28, 0x79, 0x77, 0x8c, 0x3c, 0xbf, 0xc4, 0x94, 0x86, 0xf4, 0x35, 0x9d, 0x87, 0xc7, 0x50, 0x2c, 0x47, 0x47, 0x80, 0xbd, 0x02, 0x43, 0x97, 0xe5, 0xb7, 0x30, 0x3b, 0x30, 0x97, 0x67, 0x43};
static const p8 sae3_peer_confirm[32] = {0x89, 0x1e, 0x90, 0x02, 0x2b, 0xff, 0xce, 0xee, 0x6a, 0x29, 0x7a, 0xb4, 0x97, 0xe2, 0x3f, 0x53, 0x79, 0xe0, 0xde, 0xfb, 0x80, 0xce, 0x15, 0x98, 0xf5, 0x01, 0xa1, 0xd2, 0xc7, 0x5a, 0x13, 0x9a};
static const p8 kdf_pmk[32] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f};
static const p8 kdf_anonce[32] = {0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6a, 0x6b, 0x6c, 0x6d, 0x6e, 0x6f, 0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x7b, 0x7c, 0x7d, 0x7e, 0x7f, 0x80, 0x81, 0x82, 0x83};
static const p8 kdf_snonce[32] = {0xc8, 0xc9, 0xca, 0xcb, 0xcc, 0xcd, 0xce, 0xcf, 0xd0, 0xd1, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xdb, 0xdc, 0xdd, 0xde, 0xdf, 0xe0, 0xe1, 0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7};
static const p8 kdf_ptk[48] = {0xf9, 0xf1, 0xe7, 0x2f, 0x64, 0x83, 0xe3, 0x04, 0xe1, 0x07, 0x17, 0xe1, 0x85, 0xad, 0x39, 0xbe, 0x33, 0x50, 0xf0, 0xc0, 0x45, 0xa1, 0xca, 0x99, 0xd4, 0xfc, 0x43, 0x3f, 0xc6, 0xfb, 0x84, 0x98, 0x5c, 0x8c, 0x5e, 0x99, 0x96, 0x0f, 0xde, 0x49, 0x50, 0x01, 0xbd, 0x5a, 0x87, 0xfa, 0x58, 0xb0};

static p8 wls_same(const p8 address_to a, const p8 address_to b, positive n)
{
        return !memory_compare(a, b, n);
}

static fn wifi_key_checks(void)
{
        static const p8 key[16] = {0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6,
                                   0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f, 0x3c};
        static const p8 message[64] = {
            0x6b, 0xc1, 0xbe, 0xe2, 0x2e, 0x40, 0x9f, 0x96, 0xe9, 0x3d, 0x7e, 0x11, 0x73, 0x93, 0x17, 0x2a,
            0xae, 0x2d, 0x8a, 0x57, 0x1e, 0x03, 0xac, 0x9c, 0x9e, 0xb7, 0x6f, 0xac, 0x45, 0xaf, 0x8e, 0x51,
            0x30, 0xc8, 0x1c, 0x46, 0xa3, 0x5c, 0xe4, 0x11, 0xe5, 0xfb, 0xc1, 0x19, 0x1a, 0x0a, 0x52, 0xef,
            0xf6, 0x9f, 0x24, 0x45, 0xdf, 0x4f, 0x9b, 0x17, 0xad, 0x2b, 0x41, 0x7b, 0xe6, 0x6c, 0x37, 0x10};
        static const p8 tag0[16] = {0xbb, 0x1d, 0x69, 0x29, 0xe9, 0x59, 0x37, 0x28,
                                    0x7f, 0xa3, 0x7d, 0x12, 0x9b, 0x75, 0x67, 0x46};
        static const p8 tag16[16] = {0x07, 0x0a, 0x16, 0xb4, 0x6b, 0x4d, 0x41, 0x44,
                                     0xf7, 0x9b, 0xdd, 0x9d, 0xd0, 0x4a, 0x28, 0x7c};
        static const p8 tag40[16] = {0xdf, 0xa6, 0x67, 0x47, 0xde, 0x9a, 0xe6, 0x30,
                                     0x30, 0xca, 0x32, 0x61, 0x14, 0x97, 0xc8, 0x27};
        static const p8 tag64[16] = {0x51, 0xf0, 0xbe, 0xbf, 0x7e, 0x3b, 0x9d, 0x92,
                                     0xfc, 0x49, 0x74, 0x17, 0x79, 0x36, 0x3c, 0xfe};
        static const p8 access[6] = {2, 0, 0, 0, 1, 0};
        static const p8 station[6] = {2, 0, 0, 0, 2, 0};
        p8 mac[16];
        p8 ptk[64];
        p8 rsn[48];
        static const p8 rsn_pmf[28] = {0x30, 0x1a, 0x01, 0x00, 0x00, 0x0f, 0xac, 0x04, 0x01, 0x00,
                                       0x00, 0x0f, 0xac, 0x04, 0x01, 0x00, 0x00, 0x0f, 0xac, 0x02,
                                       0xc0, 0x00, 0x00, 0x00, 0x00, 0x0f, 0xac, 0x06};
        static const p8 rsn_plain[22] = {0x30, 0x14, 0x01, 0x00, 0x00, 0x0f, 0xac, 0x04,
                                         0x01, 0x00, 0x00, 0x0f, 0xac, 0x04, 0x01, 0x00,
                                         0x00, 0x0f, 0xac, 0x02, 0x00, 0x00};

        crypto_aes_cmac((p8 address_to)key, (p8 address_to)message, 0, mac);
        check("AES-CMAC of nothing (RFC 4493)", wls_same(mac, tag0, 16));
        crypto_aes_cmac((p8 address_to)key, (p8 address_to)message, 16, mac);
        check("AES-CMAC of one block (RFC 4493)", wls_same(mac, tag16, 16));
        crypto_aes_cmac((p8 address_to)key, (p8 address_to)message, 40, mac);
        check("AES-CMAC of forty bytes (RFC 4493)", wls_same(mac, tag40, 16));
        crypto_aes_cmac((p8 address_to)key, (p8 address_to)message, 64, mac);
        check("AES-CMAC of four blocks (RFC 4493)", wls_same(mac, tag64, 16));

        memory_fill(ptk, 0, sizeof(ptk));
        wifi_ptk((p8 address_to)kdf_pmk, (p8 address_to)access, (p8 address_to)station,
                 (p8 address_to)kdf_anonce, (p8 address_to)kdf_snonce, ptk, 6);
        check("the PTK of PSK-SHA256 by the KDF of 802.11-2016", wls_same(ptk, kdf_ptk, 48));
        //      SAE's PTK is the same KDF, which a key descriptor version of 0 must not turn
        //      back into the HMAC-SHA1 of PSK.
        memory_fill(ptk, 0, sizeof(ptk));
        wifi_ptk((p8 address_to)kdf_pmk, (p8 address_to)access, (p8 address_to)station,
                 (p8 address_to)kdf_anonce, (p8 address_to)kdf_snonce, ptk, 8);
        check("and the PTK of SAE is that one too", wls_same(ptk, kdf_ptk, 48));
        check("the key descriptor version is 2 for PSK, 3 for PSK-SHA256 and 0 (as the suite defines) for SAE",
              wifi_key_version(2) == 2 && wifi_key_version(6) == 3 && wifi_key_version(8) == 0);

        check("the RSN element without protection is what it always was",
              wifi_rsn_build(rsn, 2, 0, null) == 22 && wls_same(rsn, rsn_plain, 22));
        check("the RSN element with protection capable and required is wpa_supplicant's",
              wifi_rsn_build(rsn, 2, 3, null) == 28 && wls_same(rsn, rsn_pmf, 28));
}

/*
        Hash-to-element against the same Python reference: the simplified SWU map
        of six values of u (zero, where m is zero, among them), and PT and the
        element of two stations for four names and passwords.
*/
static const p8 h2e0_pt[64] = {0x14, 0x15, 0x5e, 0x65, 0x7b, 0xd5, 0xa6, 0x00, 0xa5, 0x21, 0x64, 0xe2, 0xe5, 0xd7, 0x39, 0xde, 0xd8, 0x17, 0xee, 0x75, 0xc5, 0x29, 0x23, 0xd9, 0x82, 0x8d, 0x50, 0x7c, 0xa7, 0x32, 0xa9, 0xe2, 0xcc, 0xee, 0x48, 0x04, 0xfa, 0x6e, 0x05, 0x1e, 0x21, 0x38, 0x3a, 0xb8, 0xb0, 0xb8, 0x8d, 0x61, 0x4a, 0x60, 0x04, 0xc5, 0x67, 0xde, 0xd4, 0xef, 0x68, 0xca, 0xf4, 0xed, 0x53, 0xba, 0x50, 0x5c};
static const p8 h2e0_pwe[64] = {0x65, 0x23, 0x5c, 0x01, 0x83, 0xc1, 0xf9, 0x44, 0xb0, 0x5d, 0xae, 0x89, 0x99, 0x3f, 0x38, 0x9e, 0x70, 0x16, 0x8c, 0x0d, 0xa5, 0x97, 0xe7, 0x8c, 0xc1, 0x42, 0x95, 0xd6, 0x15, 0x6c, 0x00, 0x11, 0xf9, 0x6b, 0x6b, 0x7c, 0x1b, 0x5a, 0x2d, 0x09, 0x47, 0x17, 0x66, 0x2e, 0x88, 0x52, 0x7c, 0x96, 0x69, 0xca, 0xf4, 0x42, 0x92, 0x80, 0x4c, 0x25, 0x4e, 0x7f, 0xed, 0x05, 0x38, 0x38, 0x59, 0x0e};
static const p8 h2e1_pt[64] = {0xf7, 0x56, 0x1a, 0xfa, 0x28, 0x1c, 0x05, 0xbe, 0x91, 0x02, 0x4b, 0x77, 0x86, 0xa8, 0xae, 0x72, 0xca, 0x02, 0xc0, 0x54, 0x7e, 0x72, 0xb2, 0x91, 0x10, 0x49, 0x76, 0x3f, 0x6d, 0x20, 0x5f, 0x0b, 0x32, 0x6c, 0x75, 0xde, 0x96, 0xf3, 0xce, 0xb3, 0xd1, 0x96, 0xd8, 0x91, 0x50, 0x53, 0x5b, 0x28, 0x4e, 0x5f, 0xbe, 0xe3, 0x47, 0xe1, 0x7a, 0x46, 0xa6, 0xe6, 0xef, 0x59, 0x85, 0x0e, 0x8a, 0x64};
static const p8 h2e1_pwe[64] = {0xb9, 0x5b, 0x64, 0x57, 0x14, 0xf1, 0x93, 0xa7, 0xca, 0x61, 0x25, 0x39, 0x9f, 0xed, 0x3e, 0xb0, 0xed, 0xce, 0xa3, 0xc9, 0xbb, 0x86, 0x3d, 0x64, 0x09, 0xbb, 0x56, 0x1a, 0x53, 0x3f, 0xea, 0xed, 0x83, 0x6c, 0x46, 0x93, 0xc2, 0x97, 0x87, 0x12, 0xfd, 0xaa, 0x21, 0xee, 0x31, 0x10, 0x8a, 0x7a, 0x18, 0x73, 0x43, 0xe7, 0x80, 0xa3, 0x6d, 0xae, 0xe1, 0x9f, 0x96, 0xb4, 0xcf, 0x7f, 0x94, 0x1e};
static const p8 h2e2_pt[64] = {0xac, 0x0d, 0xa4, 0x15, 0x7e, 0xa3, 0xe1, 0xd8, 0x5d, 0x4a, 0xce, 0xcb, 0x43, 0x58, 0x84, 0x63, 0xa1, 0x90, 0x9d, 0x43, 0x98, 0xac, 0x65, 0x53, 0xa4, 0xbe, 0x7c, 0x8f, 0x51, 0xdd, 0x02, 0x96, 0xb1, 0xab, 0x62, 0x38, 0x8a, 0x35, 0x6c, 0x22, 0xf1, 0x91, 0x72, 0xfc, 0x64, 0xb5, 0x84, 0xe3, 0xfa, 0x51, 0x52, 0x27, 0x1f, 0x16, 0x7c, 0x60, 0xf2, 0x1e, 0xd9, 0x72, 0x16, 0x63, 0xdf, 0x9f};
static const p8 h2e2_pwe[64] = {0x29, 0x5e, 0x14, 0x2b, 0xa3, 0x10, 0xdd, 0x00, 0x47, 0xda, 0xf7, 0x8c, 0xbf, 0xaa, 0x64, 0x19, 0xd2, 0x2a, 0x48, 0xd2, 0x35, 0xda, 0x93, 0x27, 0x57, 0x90, 0x6e, 0xb5, 0x02, 0x79, 0x6d, 0x51, 0x75, 0x9d, 0x9d, 0x73, 0x88, 0x32, 0x0c, 0xcb, 0xa2, 0xf1, 0x72, 0xea, 0x74, 0x33, 0xc6, 0xf5, 0x83, 0x47, 0xe3, 0xd5, 0x4b, 0xaf, 0x3c, 0x7a, 0x49, 0x3f, 0x5b, 0x15, 0x50, 0x55, 0x69, 0xa6};
static const p8 h2e3_pt[64] = {0xdf, 0xaa, 0xaa, 0xca, 0xed, 0x6b, 0xad, 0x8f, 0x02, 0x16, 0x28, 0x05, 0x8b, 0xaa, 0x46, 0x84, 0x64, 0x23, 0x6b, 0x08, 0xb1, 0xcf, 0x3c, 0xfc, 0x86, 0x3e, 0xd6, 0xeb, 0x25, 0x71, 0xd8, 0x12, 0xd6, 0xd1, 0xc1, 0x4b, 0x78, 0x68, 0x1e, 0x78, 0x65, 0x05, 0x2d, 0xbf, 0xe9, 0xab, 0x15, 0xaa, 0x74, 0xb1, 0xad, 0x30, 0xb0, 0x25, 0x20, 0xaa, 0x97, 0x7b, 0xa9, 0xd5, 0x91, 0x0a, 0x71, 0x81};
static const p8 h2e3_pwe[64] = {0x30, 0x44, 0x18, 0x71, 0x65, 0xaa, 0x8b, 0x75, 0xd9, 0x82, 0x4d, 0x4a, 0x5f, 0xd6, 0xc2, 0x31, 0x4f, 0xab, 0xa3, 0x75, 0x20, 0xc0, 0x43, 0xed, 0xa4, 0x08, 0x47, 0x8c, 0x89, 0x17, 0xe6, 0x46, 0x68, 0x6c, 0x91, 0x30, 0xdb, 0x81, 0x2c, 0x69, 0x2e, 0xe1, 0x37, 0xf3, 0xb9, 0x72, 0x30, 0x96, 0xa6, 0x14, 0xde, 0x33, 0x27, 0x55, 0xd6, 0x4a, 0x97, 0xc4, 0x83, 0x5e, 0xf1, 0x79, 0xbf, 0x06};
static const p8 sswu0_u[32] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
static const p8 sswu0_p[64] = {0xa5, 0x28, 0xbd, 0x86, 0x96, 0xbd, 0xaf, 0x99, 0x6c, 0x65, 0xb9, 0x82, 0xd9, 0x49, 0x59, 0xd3, 0x14, 0x6f, 0xe6, 0xa0, 0x20, 0x69, 0x30, 0x90, 0xbd, 0xba, 0x13, 0x13, 0x23, 0x75, 0xf2, 0x24, 0x0e, 0x5f, 0xb7, 0x3d, 0x16, 0x79, 0x1c, 0xe3, 0x58, 0xfb, 0x5a, 0xdb, 0x2d, 0x33, 0x66, 0x8a, 0x3b, 0x24, 0x09, 0x9f, 0xd8, 0xd4, 0x01, 0xf6, 0x68, 0x5e, 0x0e, 0x99, 0x4f, 0xb4, 0xd7, 0x56};
static const p8 sswu1_u[32] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01};
static const p8 sswu1_p[64] = {0xdb, 0x46, 0x98, 0xc8, 0x49, 0x7d, 0xef, 0x7b, 0x64, 0x76, 0x53, 0xb9, 0x3f, 0xac, 0xc5, 0x1d, 0x5c, 0xdd, 0x38, 0x4d, 0x64, 0x27, 0x95, 0xb7, 0x7e, 0x59, 0x6b, 0x88, 0x9f, 0x6f, 0xac, 0xc7, 0x27, 0xe8, 0x6f, 0x68, 0x7c, 0xa9, 0x4e, 0x26, 0xb6, 0x55, 0x50, 0x8b, 0x3b, 0xfe, 0xc3, 0x6e, 0x3e, 0x73, 0xa4, 0x74, 0xc9, 0xf7, 0x91, 0x49, 0x31, 0xf0, 0x9c, 0x6e, 0x91, 0xd3, 0xfa, 0x5b};
static const p8 sswu2_u[32] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02};
static const p8 sswu2_p[64] = {0xea, 0xcd, 0xd1, 0x85, 0x01, 0xea, 0x13, 0x40, 0x3e, 0x4f, 0x3b, 0x60, 0x9b, 0xcb, 0xed, 0x7c, 0xde, 0x8d, 0xac, 0xbc, 0xc1, 0x08, 0xe1, 0x0a, 0xcc, 0xf2, 0x3e, 0xde, 0xaf, 0xf6, 0x78, 0x9f, 0x99, 0x9c, 0xba, 0xc0, 0xbc, 0x3d, 0x06, 0x75, 0x57, 0x49, 0x3e, 0xdb, 0x9b, 0x22, 0xfe, 0xb1, 0x62, 0x3c, 0x7a, 0x64, 0xcb, 0x69, 0x25, 0x58, 0x01, 0xba, 0x63, 0x4d, 0xbb, 0xe7, 0x95, 0x5a};
static const p8 sswu3_u[32] = {0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfe};
static const p8 sswu3_p[64] = {0xdb, 0x46, 0x98, 0xc8, 0x49, 0x7d, 0xef, 0x7b, 0x64, 0x76, 0x53, 0xb9, 0x3f, 0xac, 0xc5, 0x1d, 0x5c, 0xdd, 0x38, 0x4d, 0x64, 0x27, 0x95, 0xb7, 0x7e, 0x59, 0x6b, 0x88, 0x9f, 0x6f, 0xac, 0xc7, 0xd8, 0x17, 0x90, 0x96, 0x83, 0x56, 0xb1, 0xda, 0x49, 0xaa, 0xaf, 0x74, 0xc4, 0x01, 0x3c, 0x91, 0xc1, 0x8c, 0x5b, 0x8c, 0x36, 0x08, 0x6e, 0xb6, 0xce, 0x0f, 0x63, 0x91, 0x6e, 0x2c, 0x05, 0xa4};
static const p8 sswu4_u[32] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef, 0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef, 0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef, 0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef};
static const p8 sswu4_p[64] = {0xc9, 0x8a, 0x04, 0x68, 0xfd, 0x11, 0x62, 0xee, 0xad, 0xef, 0x0d, 0x18, 0xe5, 0x04, 0x71, 0xf1, 0xe6, 0xfc, 0xd0, 0xb8, 0x1b, 0x5f, 0x09, 0xe2, 0xa7, 0xef, 0x8b, 0x79, 0xc1, 0xb5, 0xf2, 0xe3, 0x02, 0x52, 0x2e, 0x18, 0x42, 0xcd, 0x0e, 0xb8, 0xef, 0xce, 0x62, 0x9e, 0x89, 0x9c, 0x9e, 0x81, 0xbc, 0x8b, 0xd5, 0x45, 0x05, 0xdc, 0x35, 0xdf, 0x41, 0x30, 0x2f, 0x03, 0x92, 0x1b, 0xcf, 0x09};
static const p8 sswu5_u[32] = {0x7f, 0xff, 0xff, 0xff, 0x80, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
static const p8 sswu5_p[64] = {0x91, 0xf5, 0xe3, 0x9a, 0x5a, 0x89, 0x47, 0x6f, 0xec, 0xc0, 0x0d, 0x7b, 0x1f, 0x42, 0x8e, 0x39, 0x23, 0x7d, 0xce, 0x46, 0x52, 0xfc, 0xfe, 0x18, 0x87, 0xfb, 0xa3, 0xf7, 0x47, 0xbe, 0x61, 0x77, 0x09, 0x2b, 0xaf, 0x09, 0x59, 0x63, 0x30, 0x89, 0xb1, 0xfb, 0xa8, 0x12, 0x3f, 0xb5, 0x3d, 0x0e, 0x12, 0x94, 0x17, 0xa3, 0x89, 0x9a, 0x29, 0x03, 0x3e, 0xd6, 0x18, 0x4c, 0x91, 0x95, 0xc3, 0x3f};

static fn wifi_h2e_checks(void)
{
        static const p8 access[6] = {2, 0, 0, 0, 1, 0};
        static const p8 station[6] = {2, 0, 0, 0, 2, 0};
        static const struct
        {
                string_address ssid;
                positive ssid_length;
                string_address password;
                const p8 *pt, *pwe;
        } h2e[] = {
            {"saeh2", 5, "hostapd-sae-2", h2e0_pt, h2e0_pwe},
            {"Example", 7, "password1", h2e1_pt, h2e1_pwe},
            {"a", 1, "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx", h2e2_pt, h2e2_pwe},
            {"\xe6\x97\xa5\xe6\x9c\xac-", 7, "p\xc3\xa4ssw\xc3\xb6rd", h2e3_pt, h2e3_pwe},
        };
        static const p8 *const sswu_u[6] = {sswu0_u, sswu1_u, sswu2_u, sswu3_u, sswu4_u, sswu5_u};
        static const p8 *const sswu_p[6] = {sswu0_p, sswu1_p, sswu2_p, sswu3_p, sswu4_p, sswu5_p};
        p64 u[CRYPTO_FE_MAX], x[CRYPTO_FE_MAX], y[CRYPTO_FE_MAX];
        p8 point[64];
        p8 element[64];
        p8 got[64];

        for (positive at = 0; at < 6; at++)
        {
                crypto_fe_load_be(u, (p8 address_to)sswu_u[at], 4);
                wifi_sae_sswu(x, y, u);
                crypto_fe_store_be(got, x, 4);
                crypto_fe_store_be(got + 32, y, 4);
                check("the simplified SWU map of a value of u", wls_same(got, sswu_p[at], 64));
        }
        for (positive at = 0; at < sizeof(h2e) / sizeof(h2e[0]); at++)
        {
                check("PT of a network's name and password",
                      wifi_sae_pt((p8 address_to)h2e[at].ssid, h2e[at].ssid_length,
                                  (p8 address_to)h2e[at].password, string_length(h2e[at].password),
                                  point) &&
                              wls_same(point, h2e[at].pt, 64));
                check("the element of two stations from PT",
                      wifi_sae_pwe_from_pt((p8 address_to)h2e[at].pt, (p8 address_to)station,
                                           (p8 address_to)access, element) &&
                              wls_same(element, h2e[at].pwe, 64));
                check("and it does not depend on which address is ours",
                      wifi_sae_pwe_from_pt((p8 address_to)h2e[at].pt, (p8 address_to)access,
                                           (p8 address_to)station, element) &&
                              wls_same(element, h2e[at].pwe, 64));
        }
}

static fn wifi_sae_checks(void)
{
        static const struct
        {
                string_address password;
                const p8 *pwe, *rand, *scalar, *element, *peer_scalar, *peer_element, *kck, *pmk,
                    *pmkid, *confirm, *peer_confirm;
        } sae[] = {
            {"abcdefgh12345678", sae0_pwe, sae0_rand, sae0_scalar, sae0_element, sae0_peer_scalar,
             sae0_peer_element, sae0_kck, sae0_pmk, sae0_pmkid, sae0_confirm, sae0_peer_confirm},
            {"mekmitasdigoat", sae1_pwe, sae1_rand, sae1_scalar, sae1_element, sae1_peer_scalar,
             sae1_peer_element, sae1_kck, sae1_pmk, sae1_pmkid, sae1_confirm, sae1_peer_confirm},
            {"password1", sae2_pwe, sae2_rand, sae2_scalar, sae2_element, sae2_peer_scalar,
             sae2_peer_element, sae2_kck, sae2_pmk, sae2_pmkid, sae2_confirm, sae2_peer_confirm},
            {"xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx", sae3_pwe, sae3_rand, sae3_scalar,
             sae3_element, sae3_peer_scalar, sae3_peer_element, sae3_kck, sae3_pmk, sae3_pmkid,
             sae3_confirm, sae3_peer_confirm},
        };
        static const p8 access[6] = {2, 0, 0, 0, 1, 0};
        static const p8 station[6] = {2, 0, 0, 0, 2, 0};
        p8 rsn[48];
        p8 parity = true;

        check("the RSN element of SAE carries the PMKID and BIP-CMAC-128",
              wifi_rsn_build(rsn, 8, 3, (p8 address_to)sae0_pmkid) == 44 && rsn[1] == 42 &&
                      rsn[19] == 8 && rsn[20] == 0xc0 && rsn[22] == 1 &&
                      wls_same(rsn + 24, sae0_pmkid, 16) && rsn[43] == 6);

        for (positive at = 0; at < sizeof(sae) / sizeof(sae[0]); at++)
        {
                wifi_sae side;
                wifi_sae other;
                p8 sum[64];
                p8 want[32];
                p8 bad[64];
                p8 zeros[32];
                p8 one[32];

                memory_fill(address_of side, 0, sizeof(side));
                memory_fill(zeros, 0, 32);
                memory_fill(one, 0, 32);
                one[31] = 1;
                check("the password element of a password, by hunting and pecking",
                      wifi_sae_pwe((p8 address_to)sae[at].password,
                                   string_length(sae[at].password), (p8 address_to)station,
                                   (p8 address_to)access, side.pwe) &&
                              wls_same(side.pwe, sae[at].pwe, 64));
                //      The same element whichever address is named first.
                memory_fill(address_of other, 0, sizeof(other));
                check("and it does not depend on which address is ours",
                      wifi_sae_pwe((p8 address_to)sae[at].password,
                                   string_length(sae[at].password), (p8 address_to)access,
                                   (p8 address_to)station, other.pwe) &&
                              wls_same(other.pwe, sae[at].pwe, 64));
                memory_copy(side.rand, sae[at].rand, 32);
                memory_copy(side.scalar, sae[at].scalar, 32);
                memory_copy(side.element, sae[at].element, 64);
                check("the commit of the access point is taken in",
                      wifi_sae_commit_good(address_of side, sae[at].peer_scalar,
                                           sae[at].peer_element) &&
                          wifi_sae_take(address_of side, (p8 address_to)sae[at].peer_scalar,
                                        (p8 address_to)sae[at].peer_element));
                check("and it gives the KCK, the PMK and the PMKID",
                      wls_same(side.kck, sae[at].kck, 32) && wls_same(side.pmk, sae[at].pmk, 32) &&
                              wls_same(side.pmkid, sae[at].pmkid, 16));
                sum[0] = 1, sum[1] = 0;
                wifi_sae_confirm(side.kck, sum, side.scalar, side.element, side.peer_scalar,
                                 side.peer_element, want);
                check("our confirm", wls_same(want, sae[at].confirm, 32));
                sum[0] = 7;
                wifi_sae_confirm(side.kck, sum, side.peer_scalar, side.peer_element, side.scalar,
                                 side.element, want);
                check("and the one the access point sends, with its own counter",
                      wls_same(want, sae[at].peer_confirm, 32));

                //      A commit made here: scalar * PWE + element = rand * PWE.
                memory_fill(address_of other, 0, sizeof(other));
                memory_copy(other.pwe, sae[at].pwe, 64);
                parity = wifi_sae_commit(address_of other) &&
                         wifi_sae_multiply(sum, other.scalar, other.pwe) &&
                         wifi_sae_multiply(bad, other.rand, other.pwe);
                if (parity)
                {
                        crypto_point one_point, two_point, total;
                        p64 x[CRYPTO_FE_MAX], y[CRYPTO_FE_MAX];

                        crypto_fe_load_be(x, sum, 4);
                        crypto_fe_load_be(y, sum + 32, 4);
                        crypto_point_set_xy(address_of one_point, x, y, address_of crypto_p256_field);
                        crypto_fe_load_be(x, other.element, 4);
                        crypto_fe_load_be(y, other.element + 32, 4);
                        crypto_point_set_xy(address_of two_point, x, y, address_of crypto_p256_field);
                        crypto_point_add(address_of total, address_of one_point, address_of two_point);
                        crypto_point_affine(address_of total);
                        crypto_fe_store_be(want, total.x, 4);
                        parity = wls_same(want, bad, 32);
                }
                check("a commit made here is scalar * PWE + element = rand * PWE", parity);

                //      Malformed commits are refused, before any of the
                //      multiplications a commit taken in costs.
                check("a commit whose scalar is zero is refused",
                      !wifi_sae_commit_good(address_of side, zeros, sae[at].peer_element));
                check("one whose scalar is one is refused",
                      !wifi_sae_commit_good(address_of side, one, sae[at].peer_element));
                memory_copy(bad, sae[at].peer_element, 64);
                bad[63] ^= 1;
                check("one whose element is off the curve is refused",
                      !wifi_sae_commit_good(address_of side, sae[at].peer_scalar, bad));
                memory_copy(bad, sae[at].peer_element, 64);
                memory_fill(bad, 0xff, 5);
                check("one whose element's x is not under p is refused",
                      !wifi_sae_commit_good(address_of side, sae[at].peer_scalar, bad));
                check("one that is our own sent back is refused",
                      !wifi_sae_commit_good(address_of side, side.scalar, side.element));
                check("our scalar with another element, or our element with another scalar, "
                      "is no reflection",
                      wifi_sae_commit_good(address_of side, side.scalar, sae[at].peer_element) &&
                          wifi_sae_commit_good(address_of side, sae[at].peer_scalar, side.element));
        }
        {
                //      The order itself, and above it, are not scalars.
                static const p8 order[32] = {
                    0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff,
                    0xff, 0xff, 0xff, 0xff, 0xff, 0xbc, 0xe6, 0xfa, 0xad, 0xa7, 0x17,
                    0x9e, 0x84, 0xf3, 0xb9, 0xca, 0xc2, 0xfc, 0x63, 0x25, 0x51};
                wifi_sae side;

                memory_fill(address_of side, 0, sizeof(side));
                memory_copy(side.pwe, sae0_pwe, 64);
                memory_copy(side.rand, sae0_rand, 32);
                memory_copy(side.scalar, sae0_scalar, 32);
                memory_copy(side.element, sae0_element, 64);
                check("a commit whose scalar is the group order is refused",
                      !wifi_sae_commit_good(address_of side, order, sae0_peer_element));
        }
}

/*
        The access point's authentication frames as the exchange reads them
        (wifi_sae_frame_read), to SECURITY.md's list for a new parser: empty
        and shortest, every byte cut, every transaction and the statuses
        that mean something, another sender or network, a protected frame,
        another algorithm or group, a token of one byte, of the most and one
        over, in a container and not, and what may and may not follow; and
        the association response's status (wifi_assoc_status_read).
*/
static positive wifi_auth_frame(p8 address_to into, const p8 address_to from, p16 transaction,
                                p16 status, const p8 address_to body, positive length)
{
        memory_fill(into, 0, 30);
        into[0] = 0xb0;
        memory_copy(into + 4, "\x02\x00\x00\x00\x02\x00", 6);
        memory_copy(into + 10, from, 6);
        memory_copy(into + 16, from, 6);
        into[24] = 3;
        into[26] = (p8)transaction;
        into[27] = (p8)(transaction >> 8);
        into[28] = (p8)status;
        into[29] = (p8)(status >> 8);
        if (length)
                memory_copy(into + 30, body, length);
        return 30 + length;
}

static fn wifi_sae_frame_checks(void)
{
        static const p8 access[6] = {2, 0, 0, 0, 1, 0};
        static const p8 other[6] = {2, 0, 0, 0, 3, 0};
        p8 frame[30 + 2 + WIFI_SAE_TOKEN_MOST + 96 + 8];
        p8 body[2 + WIFI_SAE_TOKEN_MOST + 96 + 8];
        wifi_sae_frame read;
        positive size;
        positive at;
        p8 all;
        p16 status;

        check("nothing at all is no frame", !wifi_sae_frame_read(frame, 0, access, false, address_of read));
        //      A commit: group 19, scalar, element.
        body[0] = WIFI_SAE_GROUP, body[1] = 0;
        memory_copy(body + 2, sae0_peer_scalar, 32);
        memory_copy(body + 34, sae0_peer_element, 64);
        size = wifi_auth_frame(frame, access, 1, 0, body, 98);
        check("a commit is read: group, scalar and element, where they are in the frame",
              wifi_sae_frame_read(frame, size, access, false, address_of read) &&
                  read.transaction == 1 && !read.status && read.group == WIFI_SAE_GROUP &&
                  read.scalar == frame + 32 && read.element == frame + 64);
        all = true;
        for (at = 0; at < size; at++)
                all &= !wifi_sae_frame_read(frame, at, access, false, address_of read);
        check("a commit cut at any byte is refused", all);
        frame[size] = 221, frame[size + 1] = 2, frame[size + 2] = 1, frame[size + 3] = 2;
        check("with a whole element after it, it is still read",
              wifi_sae_frame_read(frame, size + 4, access, false, address_of read) &&
                  read.scalar == frame + 32);
        check("with an element cut short after it, it is not",
              !wifi_sae_frame_read(frame, size + 3, access, false, address_of read) &&
                  !wifi_sae_frame_read(frame, size + 1, access, false, address_of read));
        size = wifi_auth_frame(frame, access, 1, WIFI_SAE_H2E_STATUS, body, 98);
        check("a hash-to-element commit is read the same",
              wifi_sae_frame_read(frame, size, access, true, address_of read) &&
                  read.status == WIFI_SAE_H2E_STATUS && read.scalar == frame + 32);
        body[0] = 20;
        size = wifi_auth_frame(frame, access, 1, 0, body, 2 + 48 + 96);
        check("a commit of another group is read for its group alone",
              wifi_sae_frame_read(frame, size, access, false, address_of read) &&
                  read.group == 20 && !read.scalar && !read.element);
        body[0] = WIFI_SAE_GROUP;
        size = wifi_auth_frame(frame, other, 1, 0, body, 98);
        check("one from another address is no frame of the access point's",
              !wifi_sae_frame_read(frame, size, access, false, address_of read));
        size = wifi_auth_frame(frame, access, 1, 0, body, 98);
        memory_copy(frame + 16, other, 6);
        check("nor is one in another network",
              !wifi_sae_frame_read(frame, size, access, false, address_of read));
        size = wifi_auth_frame(frame, access, 1, 0, body, 98);
        frame[1] = 0x40;
        check("nor a protected one", !wifi_sae_frame_read(frame, size, access, false, address_of read));
        frame[1] = 0;
        frame[0] = 0x10;
        check("nor one that is no authentication frame",
              !wifi_sae_frame_read(frame, size, access, false, address_of read));
        frame[0] = 0xb0;
        frame[24] = 0;
        check("nor one of another algorithm", !wifi_sae_frame_read(frame, size, access, false, address_of read));
        frame[24] = 3;
        for (p16 transaction = 0; transaction < 5; transaction++)
        {
                size = wifi_auth_frame(frame, access, transaction, 1, body, 0);
                check("a refusal is read for its status in transactions 1 and 2, and nothing else is",
                      wifi_sae_frame_read(frame, size, access, false, address_of read) ==
                          (transaction == 1 || transaction == 2) &&
                          (!(transaction == 1 || transaction == 2) || read.status == 1));
        }

        //      A confirm: send-confirm and 32 bytes, and nothing after them.
        body[0] = 7, body[1] = 0;
        memory_copy(body + 2, sae0_peer_confirm, 32);
        size = wifi_auth_frame(frame, access, 2, 0, body, 34);
        check("a confirm is read",
              wifi_sae_frame_read(frame, size, access, false, address_of read) &&
                  read.transaction == 2 && read.send_confirm == 7 && read.confirm == frame + 32);
        all = true;
        for (at = 0; at < size; at++)
                all &= !wifi_sae_frame_read(frame, at, access, false, address_of read);
        check("a confirm cut at any byte is refused", all);
        frame[size] = 0;
        check("and one with a byte after it", !wifi_sae_frame_read(frame, size + 1, access, false, address_of read));

        //      A token asked for: the rest of the frame, or a container.
        body[0] = WIFI_SAE_GROUP, body[1] = 0;
        for (positive length = 0; length <= WIFI_SAE_TOKEN_MOST + 1; length++)
        {
                memory_fill(body + 2, (p8)length, length);
                size = wifi_auth_frame(frame, access, 1, WIFI_SAE_TOKEN_STATUS, body, 2 + length);
                check("a token of one byte to the most is taken, and none and one over are not",
                      wifi_sae_frame_read(frame, size, access, false, address_of read) ==
                          (length && length <= WIFI_SAE_TOKEN_MOST) &&
                          (!length || length > WIFI_SAE_TOKEN_MOST ||
                           (read.token == frame + 32 && read.token_length == length)));
                body[2] = 255, body[3] = (p8)(1 + length), body[4] = WIFI_SAE_TOKEN_EXTENSION;
                memory_fill(body + 5, (p8)length, length);
                size = wifi_auth_frame(frame, access, 1, WIFI_SAE_TOKEN_STATUS, body, 5 + length);
                check("and for hash-to-element in its container the same",
                      wifi_sae_frame_read(frame, size, access, true, address_of read) ==
                          (length && length <= WIFI_SAE_TOKEN_MOST) &&
                          (!length || length > WIFI_SAE_TOKEN_MOST ||
                           (read.token == frame + 35 && read.token_length == length)));
        }
        body[2] = 255, body[3] = 9, body[4] = WIFI_SAE_TOKEN_EXTENSION;
        size = wifi_auth_frame(frame, access, 1, WIFI_SAE_TOKEN_STATUS, body, 5 + 8);
        check("a container cut short, or with a byte after it, or of another kind, is refused",
              !wifi_sae_frame_read(frame, size - 1, access, true, address_of read) &&
                  !wifi_sae_frame_read(frame, size + 1, access, true, address_of read) &&
                  (frame[34] = 92, !wifi_sae_frame_read(frame, size, access, true, address_of read)));
        size = wifi_auth_frame(frame, access, 1, WIFI_SAE_TOKEN_STATUS, body, 2 + 8);
        frame[32] = 1;
        check("and a hash-to-element exchange takes no token out of its container",
              !wifi_sae_frame_read(frame, size, access, true, address_of read));

        //      The association response: capabilities, then the status.
        memory_fill(frame, 0, sizeof(frame));
        frame[0] = 0x10;
        memory_copy(frame + 10, access, 6);
        frame[26] = 17;
        check("an association response's status is read",
              wifi_assoc_status_read(frame, 30, access, address_of status) && status == 17);
        frame[0] = 0x30;
        check("and a reassociation response's", wifi_assoc_status_read(frame, 28, access, address_of status) &&
                                                    status == 17);
        all = true;
        for (at = 0; at < 28; at++)
                all &= !wifi_assoc_status_read(frame, at, access, address_of status);
        check("one cut before its status is refused", all);
        frame[0] = 0xb0;
        check("and an authentication frame is none",
              !wifi_assoc_status_read(frame, 30, access, address_of status));
        frame[0] = 0x10;
        check("and one from another address is none",
              !wifi_assoc_status_read(frame, 30, other, address_of status));
}

//      AES-128's inverse and RFC 3394's unwrap, which hold the key data of
//      every group key and every rekey. FIPS-197 C.1 is the cipher's own
//      vector; RFC 3394 4.1 is the wrap's, 128 bits of key data under a
//      128-bit KEK.
static fn wifi_unwrap_checks(void)
{
        static const p8 kek[16] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                                   0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f};
        static const p8 plain[16] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
                                     0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff};
        static const p8 wrap[24] = {0x1f, 0xa6, 0x8b, 0x0a, 0x81, 0x12, 0xb4, 0x47,
                                    0xae, 0xf3, 0x4b, 0xd8, 0xfb, 0x5a, 0x7b, 0x82,
                                    0x9d, 0x3e, 0x86, 0x23, 0x71, 0xd2, 0xcf, 0xe5};
        static const p8 cipher[16] = {0x69, 0xc4, 0xe0, 0xd8, 0x6a, 0x7b, 0x04, 0x30,
                                      0xd8, 0xcd, 0xb7, 0x80, 0x70, 0xb4, 0xc5, 0x5a};
        p8 round[176];
        p8 out[16];
        p8 got[CRYPTO_WRAP_MOST];
        p8 bent[CRYPTO_WRAP_MOST + 8];
        positive size = 99;
        p8 inverts = true;

        for (positive value = 0; value < 256; value++)
                inverts &= crypto_aes_unsubstitute(crypto_aes_substitute((p8)value)) ==
                               (p8)value &&
                           crypto_aes_substitute(crypto_aes_unsubstitute((p8)value)) ==
                               (p8)value;
        check("the inverse S-box undoes the S-box for every byte", inverts);
        check("and is FIPS-197's: 0x63 gives 0x00, 0xed gives 0x53",
              crypto_aes_unsubstitute(0x63) == 0x00 &&
                  crypto_aes_unsubstitute(0xed) == 0x53);

        crypto_aes128_expand((p8 address_to)kek, round);
        crypto_aes128_decrypt(round, cipher, out);
        check("AES-128 decrypts FIPS-197 C.1", !memory_compare(out, plain, 16));
        memory_copy(out, cipher, 16);
        crypto_aes128_decrypt(round, out, out);
        check("in place too", !memory_compare(out, plain, 16));

        check("RFC 3394 4.1 unwraps",
              crypto_aes_key_unwrap((p8 address_to)kek, (p8 address_to)wrap, 24, got,
                                    address_of size) &&
                  size == 16 && !memory_compare(got, plain, 16));

        memory_copy(bent, wrap, 24);
        bent[23] ^= 1;
        size = 99;
        check("a wrap with one bit bent is refused and gives no length",
              !crypto_aes_key_unwrap((p8 address_to)kek, bent, 24, got, address_of size) &&
                  size == 99);
        check("under another KEK as well",
              !crypto_aes_key_unwrap((p8 address_to)plain, (p8 address_to)wrap, 24, got,
                                     address_of size) &&
                  size == 99);
        memory_fill(bent, 0, sizeof(bent));
        check("a wrap of two blocks, of 25 bytes and of one block past the most are refused",
              !crypto_aes_key_unwrap((p8 address_to)kek, (p8 address_to)wrap, 16, got,
                                     address_of size) &&
                  !crypto_aes_key_unwrap((p8 address_to)kek, bent, 25, got,
                                         address_of size) &&
                  !crypto_aes_key_unwrap((p8 address_to)kek, bent, CRYPTO_WRAP_MOST + 8,
                                         got, address_of size));
}

//      IEEE 802.11i's pairwise key expansion, PRF-512 over the PMK with the
//      two addresses and the two nonces each in order, smaller first. The
//      vector is one an independent PRF (Python's hmac) gave, with the
//      addresses and the first nonce bytes past 0x7f: an order taken on
//      signed bytes would put the station's address first, and a key the
//      access point does not derive.
static fn wifi_ptk_checks(void)
{
        static const p8 pmk[32] = {
                0x0d, 0xc0, 0xd6, 0xeb, 0x90, 0x55, 0x5e, 0xd6, 0x41, 0x97, 0x56,
                0xb9, 0xa1, 0x5e, 0xc3, 0xe3, 0x20, 0x9b, 0x63, 0xdf, 0x70, 0x7d,
                0xd5, 0x08, 0xd1, 0x45, 0x81, 0xf8, 0x98, 0x27, 0x21, 0xaf};
        static const p8 ap[6] = {0x80, 0xa1, 0xb2, 0xc3, 0xd4, 0xe5};
        static const p8 sta[6] = {0x7f, 0x01, 0x02, 0x03, 0x04, 0x05};
        static const p8 anonce[32] = {
                0xc0, 0x0a, 0x11, 0x18, 0x1f, 0x26, 0x2d, 0x34, 0x3b, 0x42, 0x49,
                0x50, 0x57, 0x5e, 0x65, 0x6c, 0x73, 0x7a, 0x81, 0x88, 0x8f, 0x96,
                0x9d, 0xa4, 0xab, 0xb2, 0xb9, 0xc0, 0xc7, 0xce, 0xd5, 0xdc};
        static const p8 snonce[32] = {
                0x3e, 0x10, 0x1b, 0x26, 0x31, 0x3c, 0x47, 0x52, 0x5d, 0x68, 0x73,
                0x7e, 0x89, 0x94, 0x9f, 0xaa, 0xb5, 0xc0, 0xcb, 0xd6, 0xe1, 0xec,
                0xf7, 0x02, 0x0d, 0x18, 0x23, 0x2e, 0x39, 0x44, 0x4f, 0x5a};
        static const p8 expect[64] = {
                0x88, 0xe0, 0xed, 0x01, 0xaa, 0x50, 0x00, 0x38, 0x70, 0x23, 0xf0,
                0xef, 0x4c, 0xa2, 0xe0, 0x58, 0x5d, 0xdd, 0x2c, 0x56, 0xc8, 0x6d,
                0xa9, 0xda, 0x9a, 0x28, 0x6b, 0xc2, 0x79, 0x30, 0x2d, 0xbb, 0xfc,
                0xda, 0xb7, 0x22, 0xe5, 0x99, 0x72, 0xb7, 0xae, 0x8e, 0xe9, 0xdc,
                0xc5, 0x6d, 0x28, 0xcf, 0x21, 0x14, 0x18, 0x7e, 0xe1, 0xbc, 0x89,
                0xa8, 0x03, 0x7d, 0x96, 0xba, 0xbf, 0x8c, 0x98, 0x6f};
        p8 ptk[64];
        p8 other[64];

        wifi_ptk((p8 address_to)pmk, (p8 address_to)ap, (p8 address_to)sta,
                 (p8 address_to)anonce, (p8 address_to)snonce, ptk, 2);
        check("the pairwise key of an access point and a station past 0x7f is 802.11i's",
              !memory_compare(ptk, expect, 64));
        wifi_ptk((p8 address_to)pmk, (p8 address_to)sta, (p8 address_to)ap,
                 (p8 address_to)snonce, (p8 address_to)anonce, other, 2);
        check("and the same from either side of the handshake",
              !memory_compare(other, expect, 64));
}

//      What an access point's RSN element asks of a station, and whether the
//      join can answer it: key management 2, 6 or 8, CCMP both ways, with
//      protected management frames or without. Anything else is a class of
//      its own, named for what it asks, and ranks behind a twin of the same
//      name that the join can answer however loud. And what it offers a join,
//      counted here the long way.
static positive wifi_rsn_element(p8 address_to into, p32 group, const p32 address_to pairwise,
                                 positive pairs, const p32 address_to keys, positive count,
                                 b32 capabilities)
{
        positive at = 0;

        into[at++] = 1;
        into[at++] = 0;
        network_store_32(into + at, group);
        at += 4;
        into[at++] = (p8)pairs;
        into[at++] = 0;
        for (positive one = 0; one < pairs; one++, at += 4)
                network_store_32(into + at, pairwise[one]);
        into[at++] = (p8)count;
        into[at++] = 0;
        for (positive one = 0; one < count; one++, at += 4)
                network_store_32(into + at, keys[one]);
        if (capabilities != (b32)-1)
        {
                into[at++] = (p8)capabilities;
                into[at++] = (p8)(capabilities >> 8);
        }
        return at;
}

static fn wifi_rsn_checks(void)
{
        static const p32 ccmp[] = {0x000fac04};
        static const p32 tkip[] = {0x000fac02};
        static const p32 both[] = {0x000fac02, 0x000fac04};
        static const p32 psk[] = {0x000fac02};
        static const p32 ft[] = {0x000fac04};
        static const p32 sha[] = {0x000fac06};
        static const p32 psk_ft[] = {0x000fac02, 0x000fac04};
        static const p32 psk_sha[] = {0x000fac06, 0x000fac02};
        static const p32 sae[] = {0x000fac08};
        static const p32 psk_sae[] = {0x000fac02, 0x000fac08};
        static const p32 ft_sae[] = {0x000fac04, 0x000fac08};
        static const p32 owe[] = {0x000fac12};
        static const p32 eap[] = {0x000fac01};
        static const p32 sae_ext[] = {0x000fac18};
        static const p32 vendor[] = {0x00506f01};
        p8 element[128];
        positive size;
        wifi_rsn_caps caps;

#define RSN_A_PSK WIFI_AKM_PSK
#define RSN_A_256 WIFI_AKM_PSK_SHA256
#define RSN_A_SAE WIFI_AKM_SAE
#define RSN_A_ANY WIFI_AKM_OTHER
#define RSN_CASE(name, group_suite, pairwise_suites, pairs, keys, count, capabilities, class, \
                 want_akms, want_pmf, want_group, want_pairwise)                          \
        size = wifi_rsn_element(element, group_suite, pairwise_suites, pairs, keys, count, \
                                capabilities);                                           \
        check(name, radio_rsn_security(element, size, address_of caps) == (class) &&        \
                        caps.akms == (want_akms) && caps.pmf == (want_pmf) &&               \
                        caps.group == (want_group) && caps.pairwise == (want_pairwise) &&   \
                        !caps.h2e)

        RSN_CASE("WPA2 as an ordinary network has it", 0x000fac04, ccmp, 1, psk, 1, 0x000c, RADIO_WPA2, RSN_A_PSK, 0, 1, 1);
        RSN_CASE("WPA2 with no capabilities field", 0x000fac04, ccmp, 1, psk, 1, (b32)-1, RADIO_WPA2, RSN_A_PSK, 0, 1, 1);
        RSN_CASE("WPA2 that is capable of PMF", 0x000fac04, ccmp, 1, psk, 1, 0x0080, RADIO_WPA2, RSN_A_PSK, WIFI_PMF_CAPABLE, 1, 1);
        RSN_CASE("WPA2 that requires PMF", 0x000fac04, ccmp, 1, psk, 1, 0x00c0, RADIO_PMF, RSN_A_PSK, WIFI_PMF_CAPABLE | WIFI_PMF_REQUIRED, 1, 1);
        RSN_CASE("WPA2 that requires PMF and is not capable", 0x000fac04, ccmp, 1, psk, 1, 0x0040, RADIO_PMF, RSN_A_PSK, WIFI_PMF_REQUIRED, 1, 1);
        RSN_CASE("WPA2 with a TKIP group", 0x000fac02, ccmp, 1, psk, 1, 0x000c, RADIO_TKIP, RSN_A_PSK, 0, 2, 1);
        RSN_CASE("WPA2 with TKIP alone pairwise", 0x000fac04, tkip, 1, psk, 1, 0x000c, RADIO_TKIP, RSN_A_PSK, 0, 1, 2);
        RSN_CASE("WPA2 with TKIP and CCMP pairwise and a CCMP group", 0x000fac04, both, 2, psk, 1, 0x000c, RADIO_WPA2, RSN_A_PSK, 0, 1, 3);
        RSN_CASE("FT-PSK alone", 0x000fac04, ccmp, 1, ft, 1, 0x000c, RADIO_FT, RSN_A_ANY, 0, 1, 1);
        RSN_CASE("PSK-SHA256 alone", 0x000fac04, ccmp, 1, sha, 1, 0x000c, RADIO_WPA2, RSN_A_256, 0, 1, 1);
        RSN_CASE("PSK-SHA256 that requires PMF", 0x000fac04, ccmp, 1, sha, 1, 0x00c0, RADIO_PMF, RSN_A_256, WIFI_PMF_CAPABLE | WIFI_PMF_REQUIRED, 1, 1);
        RSN_CASE("PSK and FT-PSK", 0x000fac04, ccmp, 1, psk_ft, 2, 0x000c, RADIO_WPA2, RSN_A_PSK | RSN_A_ANY, 0, 1, 1);
        RSN_CASE("PSK-SHA256 and PSK", 0x000fac04, ccmp, 1, psk_sha, 2, 0x000c, RADIO_WPA2, RSN_A_PSK | RSN_A_256, 0, 1, 1);
        RSN_CASE("PSK and SAE", 0x000fac04, ccmp, 1, psk_sae, 2, 0x0080, RADIO_WPA23, RSN_A_PSK | RSN_A_SAE, WIFI_PMF_CAPABLE, 1, 1);
        RSN_CASE("PSK and SAE that requires PMF", 0x000fac04, ccmp, 1, psk_sae, 2, 0x00c0, RADIO_PMF, RSN_A_PSK | RSN_A_SAE, WIFI_PMF_CAPABLE | WIFI_PMF_REQUIRED, 1, 1);
        RSN_CASE("PSK and SAE with a TKIP group", 0x000fac02, ccmp, 1, psk_sae, 2, 0x0080, RADIO_TKIP, RSN_A_PSK | RSN_A_SAE, WIFI_PMF_CAPABLE, 2, 1);
        RSN_CASE("SAE alone", 0x000fac04, ccmp, 1, sae, 1, 0x00c0, RADIO_WPA3, RSN_A_SAE, WIFI_PMF_CAPABLE | WIFI_PMF_REQUIRED, 1, 1);
        RSN_CASE("SAE alone with a TKIP group", 0x000fac02, ccmp, 1, sae, 1, 0x00c0, RADIO_TKIP, RSN_A_SAE, WIFI_PMF_CAPABLE | WIFI_PMF_REQUIRED, 2, 1);
        RSN_CASE("FT-PSK and SAE", 0x000fac04, ccmp, 1, ft_sae, 2, 0x00c0, RADIO_WPA3, RSN_A_SAE | RSN_A_ANY, WIFI_PMF_CAPABLE | WIFI_PMF_REQUIRED, 1, 1);
        RSN_CASE("SAE's extended-key suite alone", 0x000fac04, ccmp, 1, sae_ext, 1, 0x00c0, RADIO_FT, RSN_A_ANY, WIFI_PMF_CAPABLE | WIFI_PMF_REQUIRED, 1, 1);
        RSN_CASE("OWE", 0x000fac04, ccmp, 1, owe, 1, 0x000c, RADIO_OWE, RSN_A_ANY, 0, 1, 1);
        RSN_CASE("802.1X", 0x000fac04, ccmp, 1, eap, 1, 0x000c, RADIO_EAP, RSN_A_ANY, 0, 1, 1);
        RSN_CASE("a vendor's key management alone, taken as WPA2", 0x000fac04, ccmp, 1, vendor, 1, 0x000c, RADIO_WPA2, RSN_A_PSK | RSN_A_ANY, 0, 1, 1);
        RSN_CASE("no key management at all", 0x000fac04, ccmp, 1, psk, 0, 0x000c, RADIO_WPA2, RSN_A_PSK, 0, 1, 1);
        RSN_CASE("no key management and a TKIP group", 0x000fac02, ccmp, 1, psk, 0, 0x000c, RADIO_TKIP, RSN_A_PSK, 0, 2, 1);
#undef RSN_CASE

        size = wifi_rsn_element(element, 0x000fac04, ccmp, 1, psk, 1, 0x00c0);
        check("a capabilities field cut to one byte says nothing: WPA2 with no protection",
              radio_rsn_security(element, size - 1, address_of caps) == RADIO_WPA2 &&
                  caps.pmf == 0 && caps.akms == RSN_A_PSK);
        size = wifi_rsn_element(element, 0x000fac02, tkip, 1, psk, 1, 0x00c0);
        check("an element cut inside its suites is WPA2 with CCMP and PSK, as one with none, "
              "whatever cipher it had begun to name",
              radio_rsn_security(element, 9, address_of caps) == RADIO_WPA2 &&
                  caps.akms == RSN_A_PSK && caps.group == WIFI_CIPHER_CCMP &&
                  caps.pairwise == WIFI_CIPHER_CCMP && !caps.pmf &&
                  radio_rsn_security(element, 3, address_of caps) == RADIO_WPA2 &&
                  caps.akms == RSN_A_PSK && caps.group == WIFI_CIPHER_CCMP);
#undef RSN_A_PSK
#undef RSN_A_256
#undef RSN_A_SAE
#undef RSN_A_ANY
        check("what the join can answer is open, WPA2, WPA3, both, and any of them with "
              "protection required, and no class that asks for more",
              radio_security_joinable(RADIO_OPEN) && radio_security_joinable(RADIO_WPA2) &&
                  radio_security_joinable(RADIO_WPA23) && !radio_security_joinable(RADIO_TKIP) &&
                  radio_security_joinable(RADIO_PMF) && !radio_security_joinable(RADIO_FT) &&
                  radio_security_joinable(RADIO_WPA3) && !radio_security_joinable(RADIO_EAP) &&
                  !radio_security_joinable(RADIO_OWE) && !radio_security_joinable(RADIO_WPA) &&
                  !radio_security_joinable(RADIO_WEP));
        //      The row of a name is its strongest access point, and says a
        //      name can be joined when any by that name can: a louder twin
        //      that asks for more must not make `wifi add` refuse the real
        //      one, which the join would have picked ahead of it.
        {
                radio_air air;
                radio_heard one;

                memory_zero(address_of air, sizeof air);
                memory_zero(address_of one, sizeof one);
                memory_copy(one.ssid, "net", 3);
                one.ssid_length = 3;
                one.mbm = -3000;
                one.security = RADIO_FT;
                one.beacon_security = RADIO_FT;
                radio_air_keep(address_of air, address_of one);
                check("a name of one access point that asks for FT alone is not joinable",
                      air.count == 1 && !air.heard[0].joinable);
                one.mbm = -7000;
                one.security = RADIO_WPA2;
                one.beacon_security = RADIO_WPA2;
                one.joinable = true;
                radio_air_keep(address_of air, address_of one);
                check("a quieter twin of plain WPA2 makes it joinable, the row staying the louder",
                      air.count == 1 && air.heard[0].joinable &&
                          air.heard[0].security == RADIO_FT && air.heard[0].mbm == -3000);
                memory_zero(address_of air, sizeof air);
                radio_air_keep(address_of air, address_of one);
                one.mbm = -1000;
                one.security = RADIO_TKIP;
                one.beacon_security = RADIO_TKIP;
                one.joinable = false;
                radio_air_keep(address_of air, address_of one);
                check("and a louder one that does not changes the row and not that",
                      air.count == 1 && air.heard[0].joinable &&
                          air.heard[0].security == RADIO_TKIP);
        }
        check("a saved network with a password is satisfied by WPA2, WPA3, both and either "
              "with protection required, and nothing else",
              radio_security_fits(true, RADIO_WPA2) && radio_security_fits(true, RADIO_WPA23) &&
                  !radio_security_fits(true, RADIO_TKIP) && radio_security_fits(true, RADIO_PMF) &&
                  !radio_security_fits(true, RADIO_FT) && radio_security_fits(true, RADIO_WPA3) &&
                  !radio_security_fits(true, RADIO_OPEN) && !radio_security_fits(true, RADIO_WPA) &&
                  !radio_security_fits(false, RADIO_WPA2) && radio_security_fits(false, RADIO_OPEN));
}

//      What the nl80211 visitors read of messages made to be hostile: the
//      family's groups cut, interface names of every length with no
//      terminator, more interfaces than there is room for, extended features
//      of no, one and two bytes, a deauthentication frame a byte short and a
//      reason of zero. A message is built the way a request is, so what is
//      cut is cut where an attribute ends or in the middle of one.
#define WIFI_MESSAGE(buffer, family, command)                                  \
        (memory_zero(address_of(buffer), sizeof(buffer)),                       \
         nl80211_begin(address_of(buffer), (family), (command), 0, 1))
#define WIFI_HEADER(buffer) ((netlink_header address_to)(buffer).bytes)

static fn wifi_group_add(netlink_buffer address_to buffer, p16 index, string_address name,
                         positive name_size, p32 id, positive id_size)
{
        positive nest = netlink_nested_begin(buffer, index);

        netlink_attribute_add(buffer, CTRL_ATTR_MCAST_GRP_NAME, (address_any)name, name_size);
        netlink_attribute_add(buffer, CTRL_ATTR_MCAST_GRP_ID, address_of id, id_size);
        netlink_nested_end(buffer, nest);
}

static fn wifi_family_checks(void)
{
        netlink_buffer buffer;
        nl80211_family_info info = {0};
        p16 family = 0x1c;
        positive nest;
        positive whole;

        WIFI_MESSAGE(buffer, GENL_ID_CTRL, 1);
        netlink_attribute_add(address_of buffer, CTRL_ATTR_FAMILY_ID, address_of family, 2);
        nest = netlink_nested_begin(address_of buffer, CTRL_ATTR_MCAST_GROUPS);
        wifi_group_add(address_of buffer, 1, "config", 7, 6, 4);
        wifi_group_add(address_of buffer, 2, "scan", 5, 8, 4);
        wifi_group_add(address_of buffer, 3, "regulatory", 11, 9, 4);
        wifi_group_add(address_of buffer, 4, "mlme", 5, 7, 4);
        netlink_nested_end(address_of buffer, nest);
        whole = WIFI_HEADER(buffer)->length;
        nl80211_family_seen(WIFI_HEADER(buffer), address_of info);
        check("the family's id and its mlme and scan groups are read",
              info.family == 0x1c && info.mlme == 7 && info.scan == 8);

        //      Cut anywhere: what is read is what was whole before the cut, and
        //      nothing is read past it.
        {
                p8 sane = true;
                p8 never_more = true;
                p32 seen_mlme = 7;

                for (positive cut = 1; cut < whole - 20; cut++)
                {
                        nl80211_family_info part = {0};

                        WIFI_HEADER(buffer)->length = (p32)(whole - cut);
                        nl80211_family_seen(WIFI_HEADER(buffer), address_of part);
                        sane &= part.family == 0x1c || part.family == 0;
                        never_more &= (!part.mlme || part.mlme == seen_mlme) &&
                                      (!part.scan || part.scan == 8);
                }
                check("a message cut anywhere gives the family and groups as they were or none",
                      sane && never_more);
                WIFI_HEADER(buffer)->length = (p32)whole;
        }
        netlink_forget(address_of buffer);

        //      A name with no terminator is not "mlme", and an id of three bytes
        //      is none.
        WIFI_MESSAGE(buffer, GENL_ID_CTRL, 1);
        netlink_attribute_add(address_of buffer, CTRL_ATTR_FAMILY_ID, address_of family, 2);
        nest = netlink_nested_begin(address_of buffer, CTRL_ATTR_MCAST_GROUPS);
        wifi_group_add(address_of buffer, 1, "mlme", 4, 7, 4);
        wifi_group_add(address_of buffer, 2, "scan", 5, 8, 3);
        netlink_nested_end(address_of buffer, nest);
        info = (nl80211_family_info){0};
        nl80211_family_seen(WIFI_HEADER(buffer), address_of info);
        check("a group name with no terminator and an id of three bytes are not taken",
              info.family == 0x1c && !info.mlme && !info.scan);
        netlink_forget(address_of buffer);
}

static fn wifi_interface_add(netlink_buffer address_to buffer, p32 index, p32 type,
                             positive name_size)
{
        p8 name[256];
        p32 wiphy = 4;

        memory_fill(name, 'w', sizeof(name));
        WIFI_MESSAGE(*buffer, 0x1c, NL80211_CMD_NEW_INTERFACE);
        nl80211_attribute_u32(buffer, NL80211_ATTR_IFINDEX, index);
        nl80211_attribute_u32(buffer, NL80211_ATTR_IFTYPE, type);
        nl80211_attribute_u32(buffer, NL80211_ATTR_WIPHY, wiphy);
        if (name_size)
                netlink_attribute_add(buffer, NL80211_ATTR_IFNAME, name, name_size);
}

static fn wifi_interface_checks(void)
{
        static const positive sizes[] = {1, 6, 15, 16, 17, 200};
        netlink_buffer buffer = {0};
        nl80211_iface found;
        p8 names = true;

        for (positive at = 0; at < sizeof(sizes) / sizeof(sizes[0]); at++)
        {
                positive kept = sizes[at] < IFNAME_SIZE ? sizes[at] : IFNAME_SIZE - 1;

                memory_zero(address_of found, sizeof(found));
                wifi_interface_add(address_of buffer, 3, NL80211_IFTYPE_STATION, sizes[at]);
                nl80211_iface_seen(WIFI_HEADER(buffer), address_of found);
                names &= found.cards == 1 && found.card[0].index == 3 &&
                         string_length((string_address)found.card[0].name) == kept &&
                         found.card[0].name[kept] == end;
                netlink_forget(address_of buffer);
        }
        check("a name of any length with no terminator is cut to the room and ended", names);

        memory_zero(address_of found, sizeof(found));
        for (p32 index = 10; index < 22; index++)
        {
                wifi_interface_add(address_of buffer, index, 3 + (index & 1) * 3, 5);
                nl80211_iface_seen(WIFI_HEADER(buffer), address_of found);
                netlink_forget(address_of buffer);
        }
        wifi_interface_add(address_of buffer, 40, NL80211_IFTYPE_STATION, 5);
        nl80211_iface_seen(WIFI_HEADER(buffer), address_of found);
        netlink_forget(address_of buffer);
        check("twelve interfaces that are not stations are counted to eight, a station "
              "after them is still found",
              found.others == 8 && found.other[0] == 10 && found.other[7] == 17 &&
                  found.cards == 1 && found.card[0].index == 40);

        memory_zero(address_of found, sizeof(found));
        for (p32 index = 50; index < 57; index++)
        {
                wifi_interface_add(address_of buffer, index, NL80211_IFTYPE_STATION, 5);
                nl80211_iface_seen(WIFI_HEADER(buffer), address_of found);
                netlink_forget(address_of buffer);
        }
        check("stations are kept to the first four, in the order listed",
              found.cards == NL80211_CARDS && found.card[0].index == 50 &&
                  found.card[3].index == 53);

        memory_zero(address_of found, sizeof(found));
        WIFI_MESSAGE(buffer, 0x1c, NL80211_CMD_GET_WIPHY);
        nl80211_attribute_u32(address_of buffer, NL80211_ATTR_IFINDEX, 3);
        nl80211_attribute_u32(address_of buffer, NL80211_ATTR_IFTYPE, NL80211_IFTYPE_STATION);
        nl80211_iface_seen(WIFI_HEADER(buffer), address_of found);
        netlink_forget(address_of buffer);
        WIFI_MESSAGE(buffer, 0x1c, NL80211_CMD_NEW_INTERFACE);
        WIFI_HEADER(buffer)->length = NETLINK_HEADER + 2;
        nl80211_iface_seen(WIFI_HEADER(buffer), address_of found);
        netlink_forget(address_of buffer);
        check("another command's message and one too short for its header are no interface",
              !found.cards && !found.others);
}

static fn wifi_features_checks(void)
{
        netlink_buffer buffer;
        nl80211_wiphy_query query;
        p8 bits[3];
        p32 wiphy = 5;
        positive size;
        p8 took[4];

        for (size = 0; size < 4; size++)
        {
                memory_zero(bits, sizeof(bits));
                bits[1] = 0x80;
                WIFI_MESSAGE(buffer, 0x1c, NL80211_CMD_NEW_WIPHY);
                nl80211_attribute_u32(address_of buffer, NL80211_ATTR_WIPHY, wiphy);
                netlink_attribute_add(address_of buffer, NL80211_ATTR_EXT_FEATURES, bits,
                                      size);
                query = (nl80211_wiphy_query){.wiphy = 5, .seen = ~0u};
                nl80211_wiphy_seen(WIFI_HEADER(buffer), address_of query);
                took[size] = query.offload;
                netlink_forget(address_of buffer);
        }
        check("the offload bit is in the second byte of the features: none, one byte "
              "are not, two and three are",
              !took[0] && !took[1] && took[2] && took[3]);

        memory_zero(bits, sizeof(bits));
        bits[1] = 0x40;
        WIFI_MESSAGE(buffer, 0x1c, NL80211_CMD_NEW_WIPHY);
        nl80211_attribute_u32(address_of buffer, NL80211_ATTR_WIPHY, wiphy);
        netlink_attribute_add(address_of buffer, NL80211_ATTR_EXT_FEATURES, bits, 2);
        query = (nl80211_wiphy_query){.wiphy = 5, .seen = ~0u};
        nl80211_wiphy_seen(WIFI_HEADER(buffer), address_of query);
        netlink_forget(address_of buffer);
        check("the bit next to it is not it", !query.offload);

        //      Another wiphy's features are not this one's, and a chunk of a
        //      split dump with no wiphy of its own belongs to the one before.
        memory_zero(bits, sizeof(bits));
        bits[1] = 0x80;
        query = (nl80211_wiphy_query){.wiphy = 5, .seen = ~0u};
        WIFI_MESSAGE(buffer, 0x1c, NL80211_CMD_NEW_WIPHY);
        wiphy = 6;
        nl80211_attribute_u32(address_of buffer, NL80211_ATTR_WIPHY, wiphy);
        netlink_attribute_add(address_of buffer, NL80211_ATTR_EXT_FEATURES, bits, 2);
        nl80211_wiphy_seen(WIFI_HEADER(buffer), address_of query);
        netlink_forget(address_of buffer);
        check("another wiphy's features are not read", !query.offload);
        WIFI_MESSAGE(buffer, 0x1c, NL80211_CMD_NEW_WIPHY);
        wiphy = 5;
        nl80211_attribute_u32(address_of buffer, NL80211_ATTR_WIPHY, wiphy);
        nl80211_wiphy_seen(WIFI_HEADER(buffer), address_of query);
        netlink_forget(address_of buffer);
        WIFI_MESSAGE(buffer, 0x1c, NL80211_CMD_NEW_WIPHY);
        netlink_attribute_add(address_of buffer, NL80211_ATTR_EXT_FEATURES, bits, 2);
        nl80211_wiphy_seen(WIFI_HEADER(buffer), address_of query);
        netlink_forget(address_of buffer);
        check("a chunk with no wiphy of its own is the one before it's", query.offload);
}

static positive wifi_news(nl80211 address_to session, p8 command, p32 index, p32 type,
                          const p8 address_to frame, positive frame_size, p8 reason_given,
                          p16 reason)
{
        netlink_buffer buffer;
        nl80211_news_turn turn = {.session = session, .index = 9};

        WIFI_MESSAGE(buffer, (p16)type, command);
        nl80211_attribute_u32(address_of buffer, NL80211_ATTR_IFINDEX, index);
        if (frame)
                netlink_attribute_add(address_of buffer, NL80211_ATTR_FRAME,
                                      (address_any)frame, frame_size);
        if (reason_given)
                netlink_attribute_add(address_of buffer, NL80211_ATTR_REASON_CODE,
                                      address_of reason, 2);
        nl80211_link_news_take(WIFI_HEADER(buffer), address_of turn);
        netlink_forget(address_of buffer);
        return turn.reason;
}

static fn wifi_news_checks(void)
{
        nl80211 session = {.family = 0x1c};
        p8 frame[26] = {0};
        netlink_buffer buffer;
        nl80211_news_turn turn = {.session = address_of session, .index = 9};

        check("a disconnection says its reason",
              wifi_news(address_of session, NL80211_CMD_DISCONNECT, 9, 0x1c, null, 0, true, 3) == 3);
        check("one that names none is reason 1",
              wifi_news(address_of session, NL80211_CMD_DISCONNECT, 9, 0x1c, null, 0, false, 0) == 1);
        check("one that names zero is a link gone too, reason 1",
              wifi_news(address_of session, NL80211_CMD_DISCONNECT, 9, 0x1c, null, 0, true, 0) == 1);
        frame[24] = 7;
        check("a deauthentication frame's reason follows its 24 bytes",
              wifi_news(address_of session, NL80211_CMD_DEAUTHENTICATE, 9, 0x1c, frame, 26,
                        false, 0) == 7 &&
                  wifi_news(address_of session, NL80211_CMD_DISASSOCIATE, 9, 0x1c, frame, 26,
                            false, 0) == 7);
        frame[25] = 0x12;
        check("both bytes of it", wifi_news(address_of session, NL80211_CMD_DEAUTHENTICATE, 9,
                                            0x1c, frame, 26, false, 0) == 0x1207);
        check("a frame of 25 bytes, of 24 and none is reason 1: the link is gone all the same",
              wifi_news(address_of session, NL80211_CMD_DEAUTHENTICATE, 9, 0x1c, frame, 25,
                        false, 0) == 1 &&
                  wifi_news(address_of session, NL80211_CMD_DEAUTHENTICATE, 9, 0x1c, frame, 24,
                            false, 0) == 1 &&
                  wifi_news(address_of session, NL80211_CMD_DEAUTHENTICATE, 9, 0x1c, null, 0,
                            false, 0) == 1);
        frame[24] = 0;
        frame[25] = 0;
        check("a frame with reason zero is reason 1",
              wifi_news(address_of session, NL80211_CMD_DEAUTHENTICATE, 9, 0x1c, frame, 26,
                        false, 0) == 1);
        check("news of another interface, another family or another command is none",
              !wifi_news(address_of session, NL80211_CMD_DISCONNECT, 8, 0x1c, null, 0, true, 3) &&
                  !wifi_news(address_of session, NL80211_CMD_DISCONNECT, 9, 0x1d, null, 0, true, 3) &&
                  !wifi_news(address_of session, NL80211_CMD_CONNECT, 9, 0x1c, null, 0, true, 3));
        WIFI_MESSAGE(buffer, 0x1c, NL80211_CMD_DISCONNECT);
        WIFI_HEADER(buffer)->length = NETLINK_HEADER + 1;
        nl80211_link_news_take(WIFI_HEADER(buffer), address_of turn);
        netlink_forget(address_of buffer);
        check("a message too short for its header is none", !turn.reason);
}

//      The pieces a request and a reply are made and walked by: nested
//      attributes built around what they hold, messages visited in a
//      datagram, attributes in a chain.
typedef struct
{
        positive seen;
        positive stop_at;
        positive lengths[8];
} wifi_walk;

static p8 wifi_walk_take(netlink_header address_to header, address_any context)
{
        wifi_walk address_to walk = (wifi_walk address_to)context;

        if (walk->seen < 8)
                walk->lengths[walk->seen] = header->length;
        walk->seen++;
        return walk->seen != walk->stop_at;
}

static p8 wifi_chain_take(p16 type, p8 address_to value, positive size, address_any context)
{
        wifi_walk address_to walk = (wifi_walk address_to)context;

        (void)value;
        if (walk->seen < 8)
                walk->lengths[walk->seen] = (positive)type << 8 | size;
        walk->seen++;
        return walk->seen != walk->stop_at;
}

static fn wifi_netlink_checks(void)
{
        netlink_buffer buffer = {0};
        netlink_buffer datagram = {0};
        netlink_buffer big = {0};
        wifi_walk walk;
        positive nest;
        p8 key[16];
        p8 big_value[40000];
        p8 expect[4 + 52];
        netlink_attribute address_to attribute;

        //      A nested attribute made the way rekey offload makes it: its
        //      length is the sum of what it holds, padded, and the message's
        //      follows.
        memory_fill(key, 0x5a, sizeof(key));
        nl80211_begin(address_of buffer, 0x1c, NL80211_CMD_SET_REKEY_OFFLOAD, 0, 1);
        nest = netlink_nested_begin(address_of buffer, NL80211_ATTR_REKEY_DATA);
        netlink_attribute_add(address_of buffer, NL80211_REKEY_DATA_KEK, key, 16);
        netlink_attribute_add(address_of buffer, NL80211_REKEY_DATA_KCK, key, 16);
        netlink_attribute_add(address_of buffer, NL80211_REKEY_DATA_REPLAY_CTR, key, 8);
        netlink_nested_end(address_of buffer, nest);
        attribute = (netlink_attribute address_to)(buffer.bytes + nest);
        memory_zero(expect, sizeof(expect));
        ((netlink_attribute address_to)expect)->length = 56;
        ((netlink_attribute address_to)expect)->type = NL80211_ATTR_REKEY_DATA | NLA_F_NESTED;
        ((netlink_attribute address_to)(expect + 4))->length = 20;
        ((netlink_attribute address_to)(expect + 4))->type = NL80211_REKEY_DATA_KEK;
        memory_copy(expect + 8, key, 16);
        ((netlink_attribute address_to)(expect + 24))->length = 20;
        ((netlink_attribute address_to)(expect + 24))->type = NL80211_REKEY_DATA_KCK;
        memory_copy(expect + 28, key, 16);
        ((netlink_attribute address_to)(expect + 44))->length = 12;
        ((netlink_attribute address_to)(expect + 44))->type = NL80211_REKEY_DATA_REPLAY_CTR;
        memory_copy(expect + 48, key, 8);
        check("a nest is the length of what it holds, padded, and the message's length follows",
              attribute->length == 56 && attribute->type == (NL80211_ATTR_REKEY_DATA | NLA_F_NESTED) &&
                  !memory_compare(buffer.bytes + nest, expect, 56) &&
                  WIFI_HEADER(buffer)->length == buffer.used &&
                  buffer.used == nest + 56);
        netlink_forget(address_of buffer);

        //      Two attributes that cannot be one nest's 16 bits.
        memory_fill(big_value, 1, sizeof(big_value));
        nl80211_begin(address_of big, 0x1c, NL80211_CMD_SET_REKEY_OFFLOAD, 0, 1);
        nest = netlink_nested_begin(address_of big, NL80211_ATTR_REKEY_DATA);
        netlink_attribute_add(address_of big, 1, big_value, sizeof(big_value));
        check("a nest of 40000 bytes is whole", !big.failed);
        netlink_attribute_add(address_of big, 2, big_value, sizeof(big_value));
        netlink_nested_end(address_of big, nest);
        check("one that is over its sixteen bits fails the request", big.failed);
        netlink_forget(address_of big);

        //      The messages of one datagram: each is visited, a stop is a stop and no
        //      failure, and one cut short stops what follows without being visited.
        nl80211_begin(address_of datagram, 0x1c, 1, 0, 1);
        nl80211_attribute_u32(address_of datagram, 3, 7);
        {
                positive first = datagram.used;
                netlink_buffer second = {0};

                nl80211_begin(address_of second, 0x1c, 2, 0, 2);
                if (net_room(address_of datagram, datagram.used + second.used))
                {
                        memory_copy(datagram.bytes + datagram.used, second.bytes, second.used);
                        datagram.used += second.used;
                }
                netlink_forget(address_of second);
                memory_zero(address_of walk, sizeof(walk));
                check("every message of a datagram is visited, each at its own length",
                      netlink_each(address_of datagram, wifi_walk_take, address_of walk) &&
                          walk.seen == 2 && walk.lengths[0] == first &&
                          walk.lengths[1] == NETLINK_HEADER + GENL_HEADER);
                memory_zero(address_of walk, sizeof(walk));
                walk.stop_at = 1;
                check("a visitor that says stop is no failure, and is not asked again",
                      netlink_each(address_of datagram, wifi_walk_take, address_of walk) &&
                          walk.seen == 1);
                //      The second claims a length that is not in the datagram.
                ((netlink_header address_to)(datagram.bytes + first))->length = 4000;
                memory_zero(address_of walk, sizeof(walk));
                check("a message longer than what arrived is refused, after the whole one",
                      !netlink_each(address_of datagram, wifi_walk_take, address_of walk) &&
                          walk.seen == 1);
                ((netlink_header address_to)(datagram.bytes + first))->length = NETLINK_HEADER - 1;
                memory_zero(address_of walk, sizeof(walk));
                check("one shorter than its own header is refused too",
                      !netlink_each(address_of datagram, wifi_walk_take, address_of walk) &&
                          walk.seen == 1);
                datagram.used = first + 8;
                memory_zero(address_of walk, sizeof(walk));
                check("and a tail of less than a header is not a message",
                      netlink_each(address_of datagram, wifi_walk_take, address_of walk) &&
                          walk.seen == 1);
        }
        netlink_forget(address_of datagram);

        //      An attribute chain: the types without their flags, what a stop does,
        //      what a cut chain gives.
        nl80211_begin(address_of buffer, 0x1c, 1, 0, 1);
        netlink_attribute_add(address_of buffer, 5 | NLA_F_NESTED, key, 3);
        netlink_attribute_add(address_of buffer, 6, key, 0);
        netlink_attribute_add(address_of buffer, 7, key, 8);
        memory_zero(address_of walk, sizeof(walk));
        check("a chain is walked with each type, flags off, and each size, padding off",
              netlink_attributes(buffer.bytes + NETLINK_HEADER + GENL_HEADER,
                                 buffer.used - NETLINK_HEADER - GENL_HEADER, wifi_chain_take,
                                 address_of walk) &&
                  walk.seen == 3 && walk.lengths[0] == (5u << 8 | 3) &&
                  walk.lengths[1] == (6u << 8 | 0) && walk.lengths[2] == (7u << 8 | 8));
        memory_zero(address_of walk, sizeof(walk));
        check("a cut chain gives what was whole before it, and says it was cut",
              !netlink_attributes(buffer.bytes + NETLINK_HEADER + GENL_HEADER,
                                  buffer.used - NETLINK_HEADER - GENL_HEADER - 4,
                                  wifi_chain_take, address_of walk) &&
                  walk.seen == 2);
        netlink_forget(address_of buffer);
}

//      A password is eight to sixty-three characters or sixty-four digits of
//      hex, the one rule for the verb that takes it and the key it makes.
static fn wifi_password_checks(void)
{
        static const p8 key[32] = {0x00, 0xff, 0x10, 0xab, 0xcd, 0xef, 0x01, 0x23,
                                   0x45, 0x67, 0x89, 0xab, 0xcd, 0xef, 0x12, 0x34,
                                   0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf0, 0x11, 0x22,
                                   0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xaa};
        p8 text[96];
        p8 pmk[32];
        p8 into[4];

        memory_fill(text, 'p', sizeof(text));
        check("a password of seven characters is not one, of eight to sixty-three it is",
              !wifi_pass_fits(text, 7) && wifi_pass_fits(text, 8) && wifi_pass_fits(text, 63) &&
                  !wifi_pass_fits(text, 0) && !wifi_pass_fits(text, 65) &&
                  !wifi_pass_fits(text, 64));
        memory_into_hex(text, (address_any)key, 32);
        check("of sixty-four hex digits it is the key", wifi_pass_fits(text, 64));
        for (positive at = 0; at < 64; at++)
                text[at] = byte_to_upper(text[at]);
        check("in either case", wifi_pass_fits(text, 64));
        text[63] = 'g';
        check("a digit that is none makes sixty-four characters no password",
              !wifi_pass_fits(text, 64) &&
                  !wifi_psk((p8 address_to) "net", 3, text, 64, pmk));
        memory_into_hex(text, (address_any)key, 32);
        check("a key of hex is the key itself, and the name is not asked of it",
              wifi_psk((p8 address_to) "net", 3, text, 64, pmk) &&
                  !memory_compare(pmk, key, 32) &&
                  wifi_psk((p8 address_to) "", 0, text, 64, pmk));
        check("a password of eight to sixty-three is asked of a name of one to thirty-two",
              !wifi_psk((p8 address_to) "", 0, (p8 address_to) "password", 8, pmk) &&
                  !wifi_psk((p8 address_to) "0123456789012345678901234567890123", 34,
                            (p8 address_to) "password", 8, pmk));

        memory_fill(into, 0x77, 4);
        check("hex text is bytes, either case", memory_from_hex(into, "00fF10aB", 4) &&
              into[0] == 0x00 && into[1] == 0xff && into[2] == 0x10 && into[3] == 0xab);
        memory_fill(into, 0x77, 4);
        check("text that is no hex stops at the byte it is in, the ones before it written",
              !memory_from_hex(into, "00zz1122", 4) && into[0] == 0 && into[1] == 0x77);
        memory_fill(into, 0x77, 4);
        check("text that ends is read no further than its end",
              !memory_from_hex(into, "ab\0cd", 2) && into[0] == 0xab && into[1] == 0x77 &&
                  !memory_from_hex(into, "abc", 2));

        {
                //      RFC 4648 6: every length to the padding it takes.
                static const string_address vector[] = {"", "Zg==", "Zm8=", "Zm9v", "Zm9vYg==",
                                                        "Zm9vYmE=", "Zm9vYmFy"};
                p8 text[16];
                p8 back[8];
                p8 loose;
                p8 good = true;

                for (positive count = 0; count < array_count(vector); count++)
                {
                        memory_fill(text, 0x55, sizeof(text));
                        memory_into_base64(text, (const p8 address_to) "foobar", count);
                        good = good && !string_compare(text, vector[count]) &&
                               text[base64_length(count) + 1] == 0x55 &&
                               memory_from_base64(back, vector[count], base64_length(count), count,
                                                  address_of loose) &&
                               !loose && !memory_compare(back, "foobar", count);
                }
                check("base64 is RFC 4648's, to the padding, and reads back", good);
                check("a last character with bits past the bytes is read, and said",
                      memory_from_base64(back, "Zh==", 4, 1, address_of loose) && loose &&
                          back[0] == 'f' &&
                          memory_from_base64(back, "Zm9=", 4, 2, address_of loose) && loose);
                check("text that is not the base64 of that many bytes is not",
                      !memory_from_base64(back, "Zg=", 3, 1, address_of loose) &&
                          !memory_from_base64(back, "Zg==", 4, 2, address_of loose) &&
                          !memory_from_base64(back, "Zm9v", 4, 2, address_of loose) &&
                          !memory_from_base64(back, "Z=g=", 4, 1, address_of loose) &&
                          !memory_from_base64(back, "Zg=A", 4, 1, address_of loose) &&
                          !memory_from_base64(back, "Z-9v", 4, 3, address_of loose) &&
                          !memory_from_base64(back, "Z\0" "9v", 4, 3, address_of loose));
        }

        {
                p8 values[256];
                p8 word[8];

                alphabet_values(values, (string_address)BASE64_ALPHABET, 64);
                check("an alphabet's bytes are their places, and the others none",
                      values['A'] == 0 && values['Z'] == 25 && values['a'] == 26 &&
                          values['9'] == 61 && values['+'] == 62 && values['/'] == 63 &&
                          values['-'] == 255 && values['='] == 255 && values[0] == 255 &&
                          values[255] == 255);
                check("pieces that fit are one string, the first may be the place it goes",
                      string_join(word, sizeof(word), "ab", "", "cde") &&
                          !string_compare(word, "abcde") &&
                          string_join(word, sizeof(word), word, "f") &&
                          !string_compare(word, "abcdef") &&
                          string_join(word, 7, "abc", "def") && !string_compare(word, "abcdef"));
                check("a piece that does not fit is told, and what did is there",
                      !string_join(word, 6, "abc", "def") && !string_compare(word, "abcde") &&
                          !string_join(word, 3, "abc", "d") && !string_join(word, 4, "abcd", ""));
        }
}

//      What the machine's own pass waits after a join that failed for the
//      network not being there or for the password, and the record it reads
//      it from.
static fn wifi_last_checks(void)
{
        radio_last last;
        bipolar kinds[] = {-110, -111, -62, -121, -1015, -19, -22, -1};
        p8 quiet = true;
        static const p64 waits[] = {3000, 6000, 12000, 24000, 48000, 60000, 60000, 60000};

        for (positive count = 1; count <= 8; count++)
        {
                last.failed = -113;
                last.at = 1000;
                last.count = count;
                last.failed = count & 1 ? -113 : -13;
                quiet &= radio_join_wait(address_of last) == waits[count - 1];
        }
        check("a network not there or refusing the password is left alone 3 s, then twice as "
              "long, to a minute",
              quiet);
        quiet = true;
        for (positive at = 0; at < sizeof(kinds) / sizeof(kinds[0]); at++)
        {
                last.failed = kinds[at];
                last.at = 1000;
                last.count = 5;
                quiet &= !radio_join_wait(address_of last);
        }
        check("every other failure is joined again at the next pass", quiet);
        last.failed = -113;
        last.at = 0;
        last.count = 0;
        check("a record from before it carried a time is no wait", !radio_join_wait(address_of last));

        radio_last_set("home", -113);
        check("a failure is kept with its name, reason, a time and a count of one",
              radio_last_get(address_of last) && !string_compare((string_address)last.ssid, "home") &&
                  last.failed == -113 && last.at && last.count == 1);
        radio_last_set("home", -113);
        check("again for the same reason, the count is two",
              radio_last_get(address_of last) && last.count == 2);
        radio_scan_declined = true;
        radio_last_set("home", -113);
        radio_last_set("home", -113);
        check("a pass that was told by the last scan and asked nothing is not another time",
              radio_last_get(address_of last) && last.failed == -113 && last.count == 2);
        radio_scan_declined = false;
        radio_last_set("home", -13);
        check("for another reason it is one", radio_last_get(address_of last) &&
                  last.failed == -13 && last.count == 1);
        radio_last_set("away", -13);
        radio_last_set("away", -13);
        radio_last_set("away", -13);
        check("for another name it is one, and counts on", radio_last_get(address_of last) &&
                  !string_compare((string_address)last.ssid, "away") && last.count == 3);
        radio_last_set("away", -19);
        check("no interface is nothing to remember", !radio_last_get(address_of last));
        radio_last_set("away", -113);
        radio_last_set("away", 0);
        check("a join that worked leaves no record", !radio_last_get(address_of last));
        {
                p8 old[] = "home\n113\n";

                host_state_ready();
                host_write_file(RADIO_LAST_PATH, old, sizeof(old) - 1, 0644, false);
                check("a record of the name and reason alone is read, with no time and no count",
                      radio_last_get(address_of last) && last.failed == -113 && !last.at &&
                          !last.count && !radio_join_wait(address_of last));
        }
        radio_last_set("home", -113);
        check("a join that failed a moment ago is waited out, and so is the restore of one",
              radio_join_waiting() && radio_wifi_restored(1) && radio_wifi_restored(-1));
        radio_last_set("home", 0);
        check("with no failure kept there is nothing to wait for and the restore joins",
              !radio_join_waiting() && !radio_wifi_restored(1) && !radio_wifi_restored(-1));
}

//      The whole scans asked for here, by name: what a join may leave to the
//      scan of a moment ago, and what a status goes by.
static fn wifi_scan_checks(void)
{
        p8 text[RADIO_SCAN_ROOM];
        positive lines = 0;
        p8 names = true;

        system_remove_at(AT_FDCWD, RADIO_SCAN_PATH, 0);
        check("with no scan on record the age is the most there is",
              radio_scan_age(null, 0) == ~(p64)0 && radio_scan_age((p8 address_to) "home", 4) == ~(p64)0);
        radio_scan_note((p8 address_to) "home", 4);
        radio_scan_note((p8 address_to) "work", 4);
        radio_scan_note(null, 0);
        check("each name asked for has its own age, and the air alone is none of them",
              radio_scan_age((p8 address_to) "home", 4) < 5000 &&
                  radio_scan_age((p8 address_to) "work", 4) < 5000 &&
                  radio_scan_age((p8 address_to) "else", 4) == ~(p64)0 &&
                  radio_scan_age((p8 address_to) "hom", 3) == ~(p64)0 &&
                  radio_scan_age((p8 address_to) "home!", 5) == ~(p64)0);
        check("and without a name the age is the newest of them", radio_scan_age(null, 0) < 5000);

        host_write_file(RADIO_SCAN_PATH, (p8 address_to) "1 stale\n", 8, 0644, false);
        check("a scan that is long ago is that long ago",
              radio_scan_age((p8 address_to) "stale", 5) > 30000 &&
                  radio_scan_age((p8 address_to) "stale", 5) != ~(p64)0);
        radio_scan_note((p8 address_to) "home", 4);
        check("and is dropped by the next, which is first, with nothing else",
              radio_scan_age((p8 address_to) "stale", 5) == ~(p64)0 &&
                  radio_scan_age((p8 address_to) "home", 4) < 5000);

        host_write_file(RADIO_SCAN_PATH, (p8 address_to) "123", 3, 0644, false);
        check("a record of the time alone, as before names, is a scan of the air",
              radio_scan_age(null, 0) < ~(p64)0 && radio_scan_age((p8 address_to) "x", 1) == ~(p64)0);

        system_remove_at(AT_FDCWD, RADIO_SCAN_PATH, 0);
        for (positive at = 0; at < 24; at++)
        {
                p8 name[3] = {'n', (p8)('a' + at), 0};

                radio_scan_note(name, 2);
        }
        //      The text is read without its last newline.
        host_read_word(RADIO_SCAN_PATH, text, sizeof(text));
        for (string_address at = (string_address)text; *at; at++)
                lines += *at == '\n';
        lines++;
        for (positive at = 8; at < 24; at++)
        {
                p8 name[3] = {'n', (p8)('a' + at), 0};

                names &= radio_scan_age(name, 2) < 5000;
        }
        check("sixteen scans are kept, the newest, however many were made",
              lines == 16 && names && radio_scan_age((p8 address_to) "na", 2) == ~(p64)0);
        system_remove_at(AT_FDCWD, RADIO_SCAN_PATH, 0);
}

//      What a join waits on: the deadline, or, where the asking is what tells
//      of the association, the next time to ask when that comes first.
static fn wifi_wait_checks(void)
{
        b32 pair[2];
        nl80211 session = {.handle = -1};
        network_deadline deadline;
        network_deadline brief;
        p8 byte = 1;
        p64 began;
        p64 took;
        bipolar got;
        bipolar opened = system_call_4(syscall(socketpair), AF_UNIX, SOCK_DGRAM, 0,
                                       (positive)pair);

        check("the socket pair for a join's wait is made", opened == 0);
        if (opened)
                return;
        session.handle = pair[0];

        network_deadline_begin(address_of deadline, 3, 0);
        began = clock_monotonic_nanoseconds();
        got = nl80211_join_wait(address_of session, address_of deadline, true);
        took = clock_monotonic_nanoseconds() - began;
        check("where the asking tells of it, nothing in a fifth of a second is time to ask again",
              got == NETWORK_TRY_AGAIN && took >= 150000000 && took < 1000000000);
        socket_send(pair[1], address_of byte, 1, 0, null, 0);
        check("what comes is what comes, asking or not",
              nl80211_join_wait(address_of session, address_of deadline, true) > 0 &&
                  nl80211_join_wait(address_of session, address_of deadline, false) > 0);
        socket_receive(pair[0], address_of byte, 1, MSG_DONTWAIT, null, 0);

        network_deadline_begin(address_of brief, 0, 300000000);
        began = clock_monotonic_nanoseconds();
        got = nl80211_join_wait(address_of session, address_of brief, false);
        took = clock_monotonic_nanoseconds() - began;
        check("without asking it waits for the deadline and says so",
              got == 0 && took >= 250000000 && took < 1500000000);

        network_deadline_begin(address_of brief, 0, 50000000);
        began = clock_monotonic_nanoseconds();
        got = nl80211_join_wait(address_of session, address_of brief, true);
        took = clock_monotonic_nanoseconds() - began;
        check("a slice that ends at the deadline or after it is the deadline, not time to ask",
              got == 0);
        check("an asking slice cannot overshoot a shorter join deadline",
              took < 150000000);
        {
                const positive budgets[] = {1, 1000000, 5000000};

                for (positive row = 0; row < array_count(budgets); row++)
                {
                        check("join deadline storm queues unrelated news",
                              socket_send(pair[1], address_of byte, 1, 0,
                                          null, 0) == 1);
                        check("join deadline storm starts its budget",
                              network_deadline_begin(address_of brief, 0,
                                                     budgets[row]));
                        /* Leave the event unread: every slice remains ready.
                           Bound the test itself if the old loop survives. */
                        positive turns = 0;
                        while (turns < 1000000 &&
                               nl80211_join_wait(address_of session,
                                                address_of brief, true) > 0)
                                turns++;
                        check("unrelated readable news cannot renew a join",
                              turns < 1000000);
                        for (positive again = 0; again < 8; again++)
                                check("an expired join stays expired with news queued",
                                      nl80211_join_wait(address_of session,
                                                       address_of brief, true) == 0);
                        check("expired join preserves news for the next operation",
                              socket_receive(pair[0], address_of byte, 1,
                                             MSG_DONTWAIT, null, 0) == 1);
                }
        }
        socket_close(pair[0]);
        socket_close(pair[1]);
}

//      The process that keeps a link: named by its lock, ended by it without
//      a pid that was somebody else's by then, and a daemon made of a fork
//      with nothing of its maker's open.
static p8 wifi_descriptor_count(string_address directory, string_address name, address_any context)
{
        (void)directory, (void)name;
        (*(positive address_to)context)++;
        return true;
}

static fn wifi_keeper_checks(void)
{
        b32 ready[2];
        positive status = 0;
        p8 text[96];
        p8 number[24];
        bipolar child;
        bipolar got;

        host_state_ready();
        system_pipe(ready, O_CLOEXEC);
        child = system_fork();
        if (!child)
        {
                bipolar handle = host_lock_file(RADIO_KEEPER_PATH, HOST_FILE_SETLK, null);
                p8 one = handle >= 0 ? 'k' : 'f';

                system_write_all(ready[1], address_of one, 1);
                for (;;)
                        system_call_4(syscall(ppoll), 0, 0, 0, 0);
        }
        got = system_read_once(ready[0], text, 1);
        check("a process takes the keeper's lock", got == 1 && text[0] == 'k');
        check("whose it is is named", host_lock_owner(RADIO_KEEPER_PATH) == (b32)child);
        host_write_file(RADIO_KEPT_PATH, (p8 address_to) "1\n", 2, 0644, false);
        radio_keeper_stop();
        check("the keeper is ended, and its lock free when stop returns",
              !host_lock_owner(RADIO_KEEPER_PATH));
        got = system_call_4(syscall(wait4), (positive)child, (positive)address_of status, 0, 0);
        check("by a kill, which is what it was ended by", got == child && (status & 0x7f) == SIGKILL);
        check("and its marker with it", system_access_at(AT_FDCWD, RADIO_KEPT_PATH, 0) < 0);
        check("nobody holding it is nobody to signal", !host_lock_signal(RADIO_KEEPER_PATH, SIGKILL));
        system_close(ready[0]);
        system_close(ready[1]);

        //      A daemon: not its maker's child, a session of its own, and the
        //      descriptors of the three that are standard and the one kept.
        system_pipe(ready, O_CLOEXEC);
        if (host_detach(ready[1], descriptors_none))
        {
                positive open = 0;
                positive at = 0;

                host_each_entry("/proc/self/fd", wifi_descriptor_count, address_of open);
                positive_into_string(number, system_call(syscall(getppid)));
                string_copy_bounded(text, number, sizeof(text));
                string_append_bounded(text, " ", sizeof(text));
                positive_into_string(number, system_call(syscall(getpid)));
                string_append_bounded(text, number, sizeof(text));
                string_append_bounded(text, " ", sizeof(text));
                positive_into_string(number, system_call_1(syscall(getsid), 0));
                string_append_bounded(text, number, sizeof(text));
                string_append_bounded(text, " ", sizeof(text));
                positive_into_string(number, open);
                string_append_bounded(text, number, sizeof(text));
                string_append_bounded(text, "\n", sizeof(text));
                at = string_length(text);
                system_write_all(ready[1], text, at);
                system_call_1(syscall(exit_group), 0);
        }
        system_close(ready[1]);
        memory_zero(text, sizeof(text));
        got = system_read_once(ready[0], text, sizeof(text) - 1);
        system_close(ready[0]);
        {
                //      Its parent, itself, its session and what it has open.
                string_address at = (string_address)text;
                positive said[4] = {0, 0, 0, 0};
                p8 whole = got > 0;

                for (positive field = 0; field < 4 && whole; field++)
                {
                        whole = string_digits_checked(address_of at, 10, address_of said[field]);
                        if (*at == ' ')
                                at++;
                }
                check("a daemon says what it is", whole);
                check("it is not the child of whoever made it",
                      whole && said[0] != (positive)system_call(syscall(getpid)));
                check("it leads a session of its own", whole && said[1] == said[2]);
                check("it has the three descriptors that are standard, the one kept and "
                      "a directory's open, and nothing else",
                      whole && said[3] == 5);
        }
}

//      A key's text and back, and text that is not quite a key.
static fn key_text(void)
{
        p8 round_trips = true;
        p8 key[32], back[32], text[48];

        for (positive seed = 0; seed < 64; seed++)
        {
                wls_seeded(key, 32, (p8)seed);
                link_key_text(key, text);
                if (!link_key_parse((string_address)text, back) ||
                    memory_compare(key, back, 32))
                        round_trips = false;
        }
        check("a key's text reads back as the key", round_trips);
        text[42] = 'B';
        check("text whose last character sets bits no key has is refused",
              !link_key_parse((string_address)text, back));
        text[42] = '*';
        check("text with a character outside base64 is refused",
              !link_key_parse((string_address)text, back));
}

//      Where a peer is, as text and back: IPv6 with its gap anywhere and of
//      any width, IPv4, ports, and the spellings that are not an address.
static p8 wls_place(string_address text, string_address address,
                      p16 want_port)
{
        p8 into[16], expect[16];
        p16 port;

        if (!link_parse_place(text, into, address_of port) ||
            port != want_port)
                return false;
        if (string_first_of(address, ':'))
        {
                p8 spelled[64];

                string_copy_bounded(spelled, address, sizeof spelled);
                return net_ipv6_literal(spelled,
                                        string_length((string_address)spelled),
                                        expect) &&
                       !memory_compare(into, expect, 16);
        }
        link_address_v4(expect, (p32)string_to_host(address));
        return !memory_compare(into, expect, 16);
}

static fn places(void)
{
        static const p16 spread[5] = {0, 7, 0xab, 0xfff, 0xffff};
        static const string_address refused[] = {
                ":::", "1:2:3:4:5:6:7:8:9", "1::2::3", "1:2:3", "12345::", "1:",
                ":1::", "g::", "1:2:3:4:5:6:7::8", "::1:", "[::1", "[::1]x",
                "[::1]:0", "[::1]:65536", "1.2.3.4:", "[1.2.3.4]", "[[::1]]",
                //      No machine: nobody, a group, everybody.
                "0.0.0.0", "0.255.255.255", "224.0.0.1", "239.255.255.250",
                "240.0.0.1", "255.255.255.255", "255.255.255.255:22348",
                "::", "[::]:9", "ff02::1", "[ff02::1]", "[::ffff:224.0.0.1]"};
        p8 whole = true, gapped = true, none = true;
        p8 address[16], back[16], text[64], spelled[64];
        p16 port;

        for (positive seed = 0; seed < 360; seed++)
        {
                p16 group[8];
                positive from = seed % 8, to = from + seed / 8 % (9 - from);
                positive used = 0;

                for (positive i = 0; i < 8; i++)
                {
                        group[i] = i >= from && i < to
                                           ? 0
                                           : spread[(seed * 7 + i * 13) % 5];
                        network_store_16(address + 2 * i, group[i]);
                }
                if (link_address_mapped(address))
                        continue;
                link_place_text(address, (p16)(seed + 1), text);
                //      The spreads include a first group of ffff, which is
                //      a multicast group and no machine's place.
                if (link_address_unicast(address) &&
                    (!link_parse_place((string_address)text, back,
                                       address_of port) ||
                     memory_compare(address, back, 16) || port != seed + 1))
                        whole = false;
                if (to == from)
                        continue;
                for (positive i = 0; i < from; i++)
                {
                        if (i)
                                spelled[used++] = ':';
                        used += positive_into_base(spelled + used, group[i], 16,
                                                   false);
                }
                spelled[used++] = ':';
                spelled[used++] = ':';
                for (positive i = to; i < 8; i++)
                {
                        if (i > to)
                                spelled[used++] = ':';
                        used += positive_into_base(spelled + used, group[i], 16,
                                                   false);
                }
                spelled[used] = 0;
                if (!net_ipv6_literal(spelled, used, back) ||
                    memory_compare(address, back, 16))
                        gapped = false;
        }
        check("an address printed reads back with its port", whole);
        check("an address with its zero run as :: reads back, wherever the "
              "run is and however long",
              gapped);

        link_address_v4(address, 0x0a000001);
        link_place_text(address, 77, text);
        check("IPv4 prints dotted",
              !string_compare((string_address)text, "10.0.0.1:77"));
        memory_zero(address, 16);
        address[15] = 1;
        link_place_text(address, 9, text);
        check("IPv6 prints bracketed, a zero group as one digit",
              !string_compare((string_address)text, "[0:0:0:0:0:0:0:1]:9"));

        check("the spellings a peer is given read as their addresses",
              wls_place("[::1]:9", "::1", 9) &&
                      wls_place("FE80::a:B", "fe80::a:b", LINK_PORT) &&
                      wls_place("1:2:3:4:5:6:7::", "1:2:3:4:5:6:7:0",
                                LINK_PORT) &&
                      wls_place("::2:3:4:5:6:7:8", "0:2:3:4:5:6:7:8",
                                LINK_PORT) &&
                      wls_place("1.2.3.4", "1.2.3.4", LINK_PORT) &&
                      wls_place("127.0.0.1:9", "127.0.0.1", 9) &&
                      wls_place("[::ffff:1.2.3.4]", "::ffff:1.2.3.4", LINK_PORT) &&
                      wls_place("10.0.0.1:77", "10.0.0.1", 77));
        for (positive at = 0; at < sizeof refused / sizeof refused[0]; at++)
                if (link_parse_place(refused[at], back, address_of port))
                        none = false;
        check("and the ones that are not an address, or that are none one "
              "machine is at, are refused",
              none);
}

/*
        The client's standard output, full: a pipe nobody reads, a socket
        nobody reads. A write that waited there stopped the client's whole
        loop -- keepalives, acknowledgements, the rekey -- until the machine
        gave the link up. Each is filled and handed a frame in a child, which
        must come back with the frame refused for now, not hang; a file is
        written where it stands, its offset kept.
*/
static b32 wls_output_full(positive kind)
{
        struct link_session address_to s = link_self.session;
        b32 ends[2];
        p8 junk[4096];
        p8 frame[] = "Dabc";
        p8 file[8];

        memory_zero(junk, sizeof junk);
        if (kind == 2)
        {
                bipolar out = system_open_at_mode(AT_FDCWD, "/root/wls-out",
                                                  FILE_WRITE | O_CLOEXEC, 0600);

                if (out < 0 || system_write_all((positive)out, "xy", 2) != 2 ||
                    system_descriptor_install((b32)out, 1) < 0)
                        return 64;
        }
        else if ((kind ? system_call_4(syscall(socketpair), 1, 1, 0,
                                       (positive)ends)
                       : system_pipe(ends, 0)) < 0 ||
                 system_descriptor_install(ends[1], 1) < 0)
                return 64;
        //      Filled through a description of its own: 1 stays blocking.
        if (kind == 1)
                while (socket_send(1, junk, sizeof junk, MSG_DONTWAIT, 0, 0) > 0)
                        ;
        else if (!kind)
        {
                bipolar filler = system_open_at(AT_FDCWD, "/proc/self/fd/1",
                                                O_WRONLY | O_NONBLOCK);

                while (filler >= 0 &&
                       system_write_once(filler, junk, sizeof junk) > 0)
                        ;
        }
        if (!link_session_open(s))
                return 65;
        link_stream_own(s->writes, 1, LINK_KEY_OUTPUT, 0, O_WRONLY);
        if (kind == 2)
                return link_stream_take(s, s->writes, frame, 4) &&
                                       s->writes[0].fd == 1 &&
                                       host_read_word("/root/wls-out", file,
                                                      sizeof file) == 5 &&
                                       !memory_compare(file, "xyabc", 5)
                               ? 0
                               : 1;
        return !link_stream_take(s, s->writes, frame, 4) && s->writes[0].quiet
                       ? 0
                       : 1;
}

static fn client_never_waits(void)
{
        b32 code[3] = {255, 255, 255};

        for (positive kind = 0; kind < 3; kind++)
        {
                bipolar child = system_fork();
                positive status = 0;

                if (!child)
                        exit(wls_output_full(kind));
                for (positive tick = 0; child > 0 && tick < 100; tick++)
                {
                        timespec pause = {0, 20000000};

                        if (system_wait4_retry(child, address_of status, 1,
                                               null) == child)
                        {
                                code[kind] = wait_status_code(status);
                                break;
                        }
                        (void)system_call_2(syscall(nanosleep),
                                            (positive)address_of pause, 0);
                }
                if (code[kind] == 255 && child > 0)
                {
                        (void)system_call_2(syscall(kill), (positive)child, 9);
                        (void)system_wait4_retry(child, address_of status, 0,
                                                 null);
                }
        }
        check("sec: a full pipe as the client's standard output never holds "
              "its loop",
              code[0] == 0);
        check("sec: nor does a full socket", code[1] == 0);
        check("and a file is written where it stands, its offset kept",
              code[2] == 0);
}

/*
        A machine with no IPv6 (the modern profile builds without it): the
        link's socket falls back to IPv4 and binds there, a datagram to an
        IPv4 peer goes out and comes back as that peer, and one to an IPv6
        peer is refused. A filter in a child makes the kernel refuse
        AF_INET6 sockets, as such a kernel does.
*/
static fn ipv4_only(void)
{
        bipolar child = system_fork();
        positive status = 0;
        b32 code;

        if (!child)
        {
                floodlight_instruction filter[] = {
                        {BPF_LOAD_WORD, 0, 0, SECCOMP_DATA_ARCH},
                        {BPF_JUMP_EQUAL, 0, 5, FLOODLIGHT_AUDIT_ARCH},
                        {BPF_LOAD_WORD, 0, 0, SECCOMP_DATA_NR},
                        {BPF_JUMP_EQUAL, 0, 3, (p32)syscall(socket)},
                        {BPF_LOAD_WORD, 0, 0, 16}, // the family
                        {BPF_JUMP_EQUAL, 0, 1, AF_INET6},
                        {BPF_RETURN, 0, 0, 0x00050000u | 97}, // EAFNOSUPPORT
                        {BPF_RETURN, 0, 0, SECCOMP_RET_ALLOW},
                };
                floodlight_program program = {array_count(filter), filter};
                socket_address_internet bound;
                b32 size = sizeof bound;
                p8 loopback[16], sixth[16] = {0};
                link_run ran[2];
                p16 port;
                system_poll_descriptor wait;
                b32 wrong = 0;

                if (system_call_5(syscall(prctl), PR_SET_NO_NEW_PRIVS, 1, 0, 0,
                                  0) < 0 ||
                    system_call_3(syscall(seccomp), SECCOMP_SET_MODE_FILTER, 0,
                                  (positive)address_of program) < 0)
                        exit(64);
                link_self.socket = link_socket_open(0, true);
                if (link_self.socket < 0 || !link_self.v4)
                        exit(65);
                if (socket_name((b32)link_self.socket, address_of bound,
                                address_of size) < 0 ||
                    bound.family != AF_INET || !bound.port)
                        exit(66);
                port = network_order_16(bound.port);
                link_address_v4(loopback, 0x7f000001);
                if (link_send_to((p8 address_to) "ping", 4, loopback, port) != 4)
                        wrong |= 4;
                wait = (system_poll_descriptor){(b32)link_self.socket,
                                                SYSTEM_POLL_READ, 0};
                link_wait(address_of wait, null, 1, link_now() + 2000000,
                          link_now());
                if (link_receive(ran) != 1 || ran[0].length != 4 ||
                    ran[0].size != 4 ||
                    memory_compare(link_inbound[0], "ping", 4) ||
                    memory_compare(ran[0].address, loopback, 16) ||
                    ran[0].port != port)
                        wrong |= 8;
                sixth[15] = 1;
                if (link_send_to((p8 address_to) "ping", 4, sixth, port) != -97)
                        wrong |= 16;
                exit(wrong);
        }
        code = child >= 0 &&
                               system_wait4_retry(child, address_of status, 0,
                                                  null) >= 0
                       ? wait_status_code(status)
                       : 255;
        check("with no IPv6 the link's socket falls back to IPv4 and binds "
              "where it was asked",
              code < 32);
        check("and a datagram to an IPv4 peer goes out and comes back as "
              "that peer",
              code < 32 && !(code & 12));
        check("and one to an IPv6 peer is refused, not sent",
              code < 32 && !(code & 16));
}

/*
        Discovery's switches through the network's verb (host/network.c), on
        this namespace's own /root: off is every lookup off and a gather
        then sends nothing, one lookup's switch is that lookup alone, and a
        word that is neither on nor off is refused. CHECK_waterlink holds
        the settings' reading itself, with no verb and no socket.
*/
static fn discovery_switch_rows(void)
{
        string_address off[] = {(string_address) "moonwater", (string_address) "network",
                                (string_address) "discovery", (string_address) "off"};
        string_address on[] = {(string_address) "moonwater", (string_address) "network",
                               (string_address) "discovery", (string_address) "on"};
        string_address stun_off[] = {(string_address) "moonwater", (string_address) "network",
                                     (string_address) "discovery", (string_address) "stun",
                                     (string_address) "off"};
        string_address bad[] = {(string_address) "moonwater", (string_address) "network",
                                (string_address) "discovery", (string_address) "https",
                                (string_address) "maybe"};
        discovery_settings none, all_off, one_off, after;
        discovery_found found;
        discovery_here here;
        positive before = discovery_sent;
        p8 path[DISCOVERY_PATH_ROOM];
        b32 refused;

        host_verb = (string_address) "network";
        for (positive at = 0; at < array_count(discovery_switches); at++)
        {
                discovery_kept_path(DISCOVERY_KEPT, discovery_switches[at].leaf, path);
                (void)system_remove_at(AT_FDCWD, (string_address)path, 0);
        }
        discovery_settings_read(address_of none);
        (void)host_network(off, 4);
        discovery_settings_read(address_of all_off);
        discovery_gather(address_of all_off, address_of found, address_of here);
        (void)host_network(on, 4);
        (void)host_network(stun_off, 5);
        discovery_settings_read(address_of one_off);
        refused = host_network(bad, 5);
        discovery_settings_read(address_of after);
        log_flush();
        check("discovery: every lookup is on when the verb has kept nothing",
              none.ask == (DISCOVERY_ASK_STUN | DISCOVERY_ASK_HTTPS));
        check("sec: network discovery off is every lookup off, and a gather then "
              "sends nothing and says it asked nothing",
              all_off.ask == 0 && discovery_sent == before &&
                  found.udp == DISCOVERY_UDP_NOT_ASKED &&
                  found.https == DISCOVERY_HTTPS_NOT_ASKED && !found.answered);
        check("discovery: network discovery stun off is that lookup alone",
              one_off.ask == DISCOVERY_ASK_HTTPS);
        check("discovery: a word that is not on or off is refused and changes "
              "nothing", refused != 0 && after.ask == one_off.ask);
        for (positive at = 0; at < array_count(discovery_switches); at++)
        {
                discovery_kept_path(DISCOVERY_KEPT, discovery_switches[at].leaf, path);
                (void)system_remove_at(AT_FDCWD, (string_address)path, 0);
        }
}

static fn indexes_and_commands(void)
{
        entropy_down = true;
        check("sec: with no entropy no session index is handed out",
              link_index_new() == 0);
        {
                struct link_session session = {0};

                memory_fill(link_client.mac1, 0xa5, sizeof link_client.mac1);
                check("sec: a client initiation with no entropy reads no "
                      "uninitialized datagram and leaves no stale MAC",
                      !link_client_initiate(address_of session, 1) &&
                              !link_client.ours &&
                              memory_span_byte(link_client.mac1, 0,
                                               sizeof link_client.mac1) ==
                                      sizeof link_client.mac1);
        }
        entropy_down = false;
        check("and with it one is", link_index_new() != 0);

        {
                p8 key[32];
                p8 stamp[WATERLINK_STAMP_BYTES];
                p8 first[32];
                struct waterlink_peer peers[LINK_PEERS_MAX];

                link_self.stamps = 0;
                memory_zero(peers, sizeof peers);
                waterlink_stamp(stamp, 1000, 0);
                for (positive at = 0; at < LINK_PEERS_MAX; at++)
                {
                        memory_zero(key, sizeof key);
                        key[0] = (p8)at;
                        key[1] = 1;
                        if (!at)
                                memory_copy(first, key, sizeof first);
                        memory_copy(peers[at].key, key, sizeof key);
                        string_copy(peers[at].name, "peer");
                        if (link_stamp_new(key, stamp))
                                link_stamp_keep(key, stamp);
                }
                (void)wls_write(LINK_PEERS_PATH, peers, sizeof peers, 0600);
                memory_zero(key, sizeof key);
                key[0] = 0xff;
                key[1] = 1;
                check("sec: a full replay table refuses an unknown identity",
                      link_self.stamps == LINK_PEERS_MAX &&
                              !link_stamp_new(key, stamp));
                check("sec: filling the replay table does not evict its first "
                      "peer's marker",
                      !link_stamp_new(first, stamp));

                (void)system_call_4(syscall(fchmodat),
                                    (positive)(bipolar)AT_FDCWD,
                                    (positive)LINK_PEERS_PATH, 0644, 0);
                check("sec: an unreadable peer authority cannot erase replay "
                      "markers",
                      !link_stamp_new(key, stamp) &&
                              link_self.stamps == LINK_PEERS_MAX &&
                              !link_stamp_new(first, stamp));

                (void)wls_write(LINK_PEERS_PATH, peers, sizeof peers[0], 0600);
                check("a full replay table releases forgotten peers before "
                      "refusing a new identity",
                      link_stamp_new(key, stamp) && link_self.stamps == 1);
        }

        {
                p8 request[6] = {LINK_ASK_SHELL, 0, 0, 0, 0, 0};

                check("sec: a shell request without its complete window size "
                      "is malformed",
                      !link_request_well_formed(LINK_KIND_SHELL, request, 1) &&
                              !link_request_well_formed(LINK_KIND_SHELL,
                                                        request, 4));
                check("sec: a shell request with its complete window size is "
                      "well formed",
                      link_request_well_formed(LINK_KIND_SHELL, request, 5));
                check("sec: an oversized shell terminal name is malformed",
                      !link_request_well_formed(LINK_KIND_SHELL, request,
                                                5 + 32));
        }

        {
                p8 part[32];

                check("sec: a wrapped staging-path length is refused before "
                      "the target is copied",
                      link_part_open("x", positive_max, part, sizeof part,
                                     0600) == -ERROR_NAME_TOO_LONG);
        }

        {
                p8 zero[33];
                p8 text[48];

                memory_zero(zero, sizeof zero);
                link_key_text(zero, text);
                (void)system_remove_at(AT_FDCWD, LINK_PEERS_PATH, 0);
                check("sec: a low-order key is not paired",
                      link_add_locked("low", (string_address)text, null) != 0 &&
                              wls_peers_count() == 0);
        }

        {
                string_address words[] = {"office"};

                (void)system_remove_at(AT_FDCWD, LINK_GROUPS_PATH, 0);
                entropy_down = true;
                check("sec: with no entropy no group secret is made up",
                      link_group_join(words, 1) != 0 &&
                              system_open_at(AT_FDCWD, LINK_GROUPS_PATH,
                                             FILE_READ) < 0);
                entropy_down = false;
        }

        /*      The name a member keeps is kept whole in a file: every byte of
                the buffer is the name's or zero, never what the stack held. */
        {
                static link_peers peers;
                p8 name[WATERLINK_NAME_MAX];
                p8 offered[WATERLINK_NAME_MAX];
                p8 key[32];

                memory_zero(address_of peers, sizeof peers);
                memory_zero(offered, sizeof offered);
                memory_copy(offered, "box", 4);
                memory_fill(key, 0x33, sizeof key);
                memory_fill(name, 0xa5, sizeof name);
                link_name_for(address_of peers, offered, key, name);
                check("a member's name is the name and zeros, nothing of the "
                      "stack after its end",
                      string_equals((string_address)name, "box") &&
                          memory_span_byte(name + 4, 0, sizeof name - 4) ==
                              sizeof name - 4);

                //      A name somebody else has: the key's own suffix, the
                //      same every time, and longer when that is taken too.
                string_copy(peers.peer[0].name, "box");
                peers.count = 1;
                link_name_for(address_of peers, offered, key, name);
                check("sec: a name another machine has is not taken: the "
                      "key's own suffix is added, and the name is one",
                      !string_equals((string_address)name, "box") &&
                          !memory_compare(name, "box-3", 5) &&
                          link_name_good((string_address)name) &&
                          memory_span_byte(name + string_length(
                                                       (string_address)name) + 1,
                                           0, sizeof name -
                                                  string_length((string_address)
                                                                        name) - 1) ==
                              sizeof name - string_length((string_address)name) - 1);
                {
                        p8 again[WATERLINK_NAME_MAX];

                        link_name_for(address_of peers, offered, key, again);
                        check("and the same key gets the same name again",
                              !memory_compare(name, again, sizeof name));
                        string_copy(peers.peer[1].name, (string_address)name);
                        peers.count = 2;
                        link_name_for(address_of peers, offered, key, again);
                        check("sec: one whose suffix is taken too is longer by "
                              "a digit",
                              string_length((string_address)again) ==
                                      string_length((string_address)name) + 1 &&
                                      link_name_good((string_address)again));
                        {
                                p8 named = true;

                                //      The names with four, five and six
                                //      digits taken as well: the last name
                                //      the walk makes is one a machine has.
                                for (positive taken = 2; taken < 5; taken++)
                                {
                                        string_copy(peers.peer[taken].name,
                                                    (string_address)again);
                                        peers.count = taken + 1;
                                        named = link_name_for(address_of peers,
                                                              offered, key,
                                                              again);
                                }
                                check("sec: with every suffix taken there is no "
                                      "name, where the last one made was given "
                                      "to a second machine",
                                      !named &&
                                              link_peer_named(
                                                  address_of peers,
                                                  (string_address)again));
                                peers.count = 2;
                        }
                }

                //      A name that is not one, or is a word link uses, is
                //      the machine's; one at the longest keeps room for its
                //      suffix.
                memory_zero(offered, sizeof offered);
                memory_copy(offered, "run", 4);
                peers.count = 0;
                link_name_for(address_of peers, offered, key, name);
                check("sec: an offered name that is a word link takes in its "
                      "place is called machine",
                      string_equals((string_address)name, "machine"));
                memory_fill(offered, 'n', WATERLINK_NAME_MAX - 1);
                offered[WATERLINK_NAME_MAX - 1] = 0;
                memory_copy(peers.peer[0].name, offered, WATERLINK_NAME_MAX);
                peers.count = 1;
                link_name_for(address_of peers, offered, key, name);
                check("sec: an offered name at the longest is cut to leave "
                      "room for its suffix, and still fits",
                      string_length((string_address)name) <
                              WATERLINK_NAME_MAX &&
                              !string_equals((string_address)name,
                                             (string_address)offered) &&
                              link_name_good((string_address)name));
        }
}

/*
        Keys over time, generated: a session rekeyed again and again, its
        datagrams sealed under the keys it has now, the ones a confirmed
        rekey replaced, ones waiting to be confirmed, ones a later rekey
        overwrote before they were used, and ones from before the peer was
        forgotten; counters fresh, replayed, at both edges of the window,
        far ahead, at the message limit and one under it; bodies new and
        ones already delivered under another key; a bit flipped and then
        the same datagram whole; time moving a little, past the grace an
        old key has and past the age no key outlives. A model of what
        link_carried promises says which are taken, and whatever it says,
        no frame is delivered twice and nothing is taken under a key that
        is gone.
*/
#define EPOCHS_MOST 48
#define EPOCH_TAKEN 1024

static positive epoch_delivered[32768];
static p64 epoch_state;

static p64 epoch_next(void)
{
        XORSHIFT64(epoch_state);
        return epoch_state;
}
static positive epoch_twice;

static p8 epoch_sink(address_any context, struct waterlink_frame address_to head,
                       p8 address_to payload)
{
        p32 id;

        (void)context;
        if (head->length != 4)
                return true;
        memory_copy(address_of id, payload, 4);
        if (id < 32768 && epoch_delivered[id]++)
                epoch_twice++;
        return true;
}

static fn epochs_generated(void)
{
        struct epoch {
                p8 raw[16];
                crypto_aesgcm_key key;
                p32 index;
                p64 made;
                p64 sent;
                p64 top;
                positive taken;
                p64 seen[EPOCH_TAKEN];
        };
        static struct epoch epoch[EPOCHS_MOST];
        static p8 body[64][WATERLINK_PAYLOAD];
        static positive body_used[64];
        struct link_session address_to s = link_self.session;
        struct waterlink_link address_to tx;
        p8 from[16];
        p8 datagram[WATERLINK_DATAGRAM];
        positive asked = 0, wrong = 0, taken = 0, after_forget = 0,
                 dead_taken = 0, edges = 0, limits = 0, graced = 0,
                 bodies = 0, malformed = 0, promoted = 0, misplaced = 0,
                 roamed = 0;
        p8 last[16];
        p16 last_port = 0;
        p64 limit = WATERLINK_REKEY_MESSAGES + (1ull << 20);
        bipolar mapped = system_call_6(syscall(mmap), 0,
                                       sizeof(struct waterlink_link), 3, 0x22,
                                       (positive)(bipolar)-1, 0);

        if (mapped < 0 && mapped > -4096)
                return;
        tx = (struct waterlink_link address_to)mapped;
        memory_copy(from, wls_loopback, sizeof from);
        from[15] = 3;
        memory_zero(epoch_delivered, sizeof epoch_delivered);
        epoch_twice = 0;

        for (positive seed = 1; seed <= 24; seed++)
        {
                bipolar slot[3] = {-1, -1, -1}; // now, next, before
                positive epochs = 0;
                p64 base;
                p64 at = 0;
                p8 forgotten = false;
                p8 again = false;
                p64 again_counter = 0;
                positive again_epoch = 0;
                positive again_body = 0;

                epoch_state = 0x7a3c9e1bull * seed;
                if (!link_session_open(s))
                        return;
                wls_seeded(s->peer, 32, (p8)seed);
                wls_peers_with(s->peer, WATERLINK_MAY_RUN);
                last_port = 0;
                waterlink_link_reset(tx);
                base = link_now();
                bodies = 0;

                for (positive op = 0; op < 900; op++)
                {
                        p64 roll = epoch_next() % 100;

                        //      A rekey: the first keys are the session's,
                        //      every later one waits in next.
                        if (!again && (op == 0 || (roll < 6 && epochs < EPOCHS_MOST && !forgotten)))
                        {
                                struct epoch address_to e = epoch + epochs;
                                struct link_keys address_to keys =
                                        s->now.live ? address_of s->next : address_of s->now;

                                memory_zero(e, sizeof(address_to e));
                                wls_seeded(e->raw, 16, (p8)(seed * 64 + epochs));
                                crypto_aesgcm_prepare(address_of e->key, e->raw);
                                e->index = (p32)(0x10000 * seed + epochs + 1);
                                e->made = base + at;
                                link_keys_install(keys, e->raw, e->raw, e->index, 7);
                                keys->made = e->made;
                                slot[keys == address_of s->now ? 0 : 1] = (bipolar)epochs;
                                epochs++;
                                continue;
                        }
                        if (!again && roll < 14)
                        {
                                p64 jump = epoch_next() % 4;

                                at += jump == 0 ? LINK_GRACE + 1000
                                    : jump == 1 ? (p64)WATERLINK_REJECT_SECONDS * 1000000 / 3
                                                : 1 + epoch_next() % 2000000;
                                continue;
                        }
                        if (!again && roll < 15 && op > 600 && !forgotten)
                        {
                                p8 other[32];

                                memory_fill(other, 0x77, sizeof other);
                                wls_peers_with(other, WATERLINK_MAY_RUN);
                                link_self.granted = 0;
                                link_sessions_granted(base + at);
                                forgotten = true;
                                continue;
                        }

                        //      A datagram.
                        {
                                positive which;
                                positive b;
                                p64 counter;
                                p8 flip = false;
                                struct epoch address_to e;
                                struct waterlink_datagram head = {WATERLINK_KIND_CARRY, 0, 0};
                                positive length;
                                bipolar in = -1;
                                p8 want = false;
                                p8 got;
                                p8 duplicate;

                                if (again)
                                {
                                        which = again_epoch;
                                        counter = again_counter;
                                        b = again_body;
                                        again = false;
                                }
                                else
                                {
                                        p64 pick = epoch_next() % 10;
                                        p64 how = epoch_next() % 12;

                                        which = pick < 5 && slot[0] >= 0 ? (positive)slot[0]
                                              : pick < 7 && slot[1] >= 0 ? (positive)slot[1]
                                              : pick < 9 && slot[2] >= 0 ? (positive)slot[2]
                                                                         : (positive)(epoch_next() % epochs);
                                        e = epoch + which;
                                        counter = how < 5 ? e->sent++
                                                : how == 5 && e->taken ? e->seen[epoch_next() % e->taken]
                                                : how == 6 && e->top >= WATERLINK_REPLAY_WINDOW
                                                        ? e->top - WATERLINK_REPLAY_WINDOW
                                                : how == 7 && e->top + 1 >= WATERLINK_REPLAY_WINDOW
                                                        ? e->top + 1 - WATERLINK_REPLAY_WINDOW
                                                : how == 8 ? (e->sent += 1 + epoch_next() % 3000)
                                                : how == 9 ? limit + 1 - epoch_next() % 3
                                                : how == 10 ? (e->top + 1 + epoch_next() % 64)
                                                            : 0;
                                        edges += how == 6 || how == 7;
                                        limits += how == 9;
                                        duplicate = bodies && epoch_next() % 4 == 0;
                                        if (duplicate)
                                                b = (positive)(epoch_next() % (bodies < 64 ? bodies : 64));
                                        else
                                        {
                                                p32 id = (p32)(seed * 1000 + bodies);
                                                p8 alone = false;

                                                b = bodies % 64;
                                                (void)waterlink_post(tx, (p8)(1 + bodies % 8), WATERLINK_FRAME_DURABLE,
                                                                     address_of id, 4, base + at);
                                                body_used[b] = waterlink_fill(tx, body[b], base + at,
                                                                              address_of alone);
                                                bodies++;
                                        }
                                        flip = epoch_next() % 8 == 0;
                                        if (flip)
                                        {
                                                again = true;
                                                again_epoch = which;
                                                again_counter = counter;
                                                again_body = b;
                                        }
                                }
                                e = epoch + which;
                                head.receiver = e->index;
                                head.counter = counter;
                                memory_copy(datagram, address_of head, sizeof head);
                                memory_copy(datagram + 16, body[b], body_used[b]);
                                length = waterlink_seal(address_of e->key, datagram, body_used[b]);
                                if (flip)
                                {
                                        datagram[16 + epoch_next() % (length - 16)] ^=
                                                (p8)(1u << epoch_next() % 8);
                                        malformed++;
                                }

                                //      The model.
                                for (positive k = 0; k < 3; k++)
                                        if (slot[k] == (bipolar)which)
                                                in = (bipolar)k;
                                if (in >= 0 && !forgotten && !flip &&
                                    base + at - e->made < (p64)WATERLINK_REJECT_SECONDS * 1000000 &&
                                    counter < limit &&
                                    !(in == 2 && base + at - epoch[slot[0]].made > LINK_GRACE))
                                {
                                        want = true;
                                        if (counter <= e->top && (e->taken || counter))
                                        {
                                                want = e->top - counter < WATERLINK_REPLAY_WINDOW;
                                                for (positive t = 0; want && t < e->taken; t++)
                                                        want = e->seen[t] != counter;
                                        }
                                        graced += in == 2;
                                }

                                {
                                        p8 place = (p8)(epoch_next() % 3);
                                        p16 port = (p16)(4000 + place);

                                        from[15] = (p8)(3 + place);
                                        got = link_carried(datagram, length, from, port,
                                                           base + at, epoch_sink);
                                        if (got)
                                        {
                                                roamed += last_port && last_port != port;
                                                memory_copy(last, from, 16);
                                                last_port = port;
                                        }
                                        misplaced += last_port &&
                                                     (memory_compare(s->address, last, 16) ||
                                                      s->port != last_port);
                                }
                                asked++;
                                wrong += got != want;
                                taken += got;
                                after_forget += got && forgotten;
                                dead_taken += got && in < 0;
                                if (want && e->taken < EPOCH_TAKEN)
                                {
                                        if (counter > e->top || !e->taken)
                                                e->top = counter > e->top ? counter : e->top;
                                        e->seen[e->taken++] = counter;
                                }
                                if (want && in == 1)
                                {
                                        slot[2] = slot[0];
                                        slot[0] = slot[1];
                                        slot[1] = -1;
                                        promoted++;
                                }
                        }
                }
                link_session_close(s);
        }
        system_call_2(syscall(munmap), (positive)tx, sizeof(struct waterlink_link));

        string_format(log, "  epochs: %p datagrams, %p taken, %p promotions, %p under "
                           "old keys in grace, %p window edges, %p at the limit, %p flipped, "
                           "%p moves\n",
                      asked, taken, promoted, graced, edges, limits, malformed, roamed);
        check("sec: generated rekeys, replays, window edges, limits, flips and "
              "a forgotten peer: link_carried takes exactly what the model of "
              "its keys and windows takes", wrong == 0);
        check("sec: no frame is delivered twice across rekeys and duplicates",
              epoch_twice == 0);
        check("sec: nothing is taken under a key that was replaced, overwritten "
              "or forgotten", after_forget == 0 && dead_taken == 0);
        check("sec: a session is where its last datagram taken came from, and "
              "no refused one moves it", misplaced == 0 && roamed > 100);
        check("and the generator reached every corner it means to",
              promoted > 24 && graced > 0 && edges > 0 && limits > 0 &&
                  malformed > 0 && taken > 1000);
}

/*
        The listener's mDNS reader, apart (link_mdns_read_apart in
        src/waterlink/nearby.c): its filter in the classic-BPF interpreter, what
        the listener believes of an answer, and a real announcement and noise
        read through the confined reader, in this check's user namespace,
        against the reading the parser makes in place.
*/
#define SHARED_filter_run
#include "../checks.c"
#undef SHARED_filter_run

static fn mdns_apart(void)
{
        static const struct
        {
                string_address name;
                p32 number;
                p8 allowed;
        } rows[] = {
            {"recvfrom", syscall(recvfrom), true}, {"sendto", syscall(sendto), true},
            {"ppoll", syscall(ppoll), true},       {"exit_group", syscall(exit_group), true},
            {"openat", syscall(openat), false},    {"socket", syscall(socket), false},
            {"execve", syscall(execve), false},    {"ptrace", syscall(ptrace), false},
            {"mmap", syscall(mmap), false},        {"write", syscall(write), false},
            {"read", syscall(read), false},        {"kill", syscall(kill), false},
        };
        static const p64 none[6] = {0};
        net_filter filter;
        p8 all = true;
        p8 instance[10] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
        p8 host[6] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xab};
        p8 packet[WATERLINK_MDNS_MAX];
        positive length;
        struct waterlink_found here;
        struct waterlink_found apart;
        p8 read_here;
        p8 read_apart;

        link_mdns_filter_build(address_of filter);
        for (positive at = 0; at < array_count(rows); at++)
                all = all && net_filter_run(address_of filter, NET_APART_ARCH, rows[at].number,
                                            none) ==
                                 (rows[at].allowed ? SECCOMP_RET_ALLOW
                                                   : SECCOMP_RET_KILL_PROCESS);
        check("the mDNS reader's filter allows its pipe's calls and kills the rest", all);
        check("the mDNS reader's filter kills a call from another architecture",
              net_filter_run(address_of filter, NET_APART_ARCH ^ 1, syscall(recvfrom), none) ==
                  SECCOMP_RET_KILL_PROCESS);

        memory_zero(address_of here, sizeof here);
        here.count = WATERLINK_FOUND_MAX;
        check("the listener believes a reading with every count in its array",
              link_mdns_believed(address_of here));
        here.count = WATERLINK_FOUND_MAX + 1;
        check("and not one more", !link_mdns_believed(address_of here));
        here.count = 1;
        here.instance[0].label_length = sizeof(here.instance[0].label) + 1;
        check("nor a label longer than its room", !link_mdns_believed(address_of here));
        here.instance[0].label_length = 0;
        here.question_length = sizeof(here.question) + 1;
        check("nor a question longer than its room", !link_mdns_believed(address_of here));

        length = waterlink_mdns_announce(packet, sizeof packet, instance, host, 22348,
                                         0x7f000001u, 120, 0, null, 0);
        memory_zero(address_of here, sizeof here);
        read_here = waterlink_mdns_read(packet, length, address_of here);
        read_apart = link_mdns_read_apart(packet, length, address_of apart);
        check("an announcement read apart is the reading made in place",
              length && read_here && read_apart && apart.count == here.count &&
                  !memory_compare(address_of apart, address_of here, sizeof here));
        check("the reader is a process of its own, still there for the next packet",
              link_mdns.pid > 0 && link_mdns.channel >= 0 && !link_mdns.refused);
        memory_fill(packet, 0xc0, 64);
        check("noise read apart is no reading",
              !link_mdns_read_apart(packet, 64, address_of apart) && !apart.count);
        {
                bipolar reader = link_mdns.pid;

                check("sec: a packet of no bytes, or of fewer than a header, is "
                      "no reading and costs no reader: an empty one was its "
                      "channel's end to it, a fork of the listener a datagram",
                      !link_mdns_read_apart(packet, 0, address_of apart) &&
                              !link_mdns_read_apart(packet, DNS_HEADER - 1,
                                                    address_of apart) &&
                              reader > 0 && link_mdns.pid == reader &&
                              link_mdns.channel >= 0);
        }
        read_apart = link_mdns_read_apart(packet, length, address_of apart);
        link_mdns_end();
        check("and the reader is let go", link_mdns.pid < 0 && link_mdns.channel < 0);

        //      The reader is a copy of the listener, which keeps the link's
        //      key: what it wipes before it reads, asked of a copy made here.
        {
                positive raw = 1;
                bipolar copy;

                memory_fill(link_self.me.secret, 0x5a, sizeof link_self.me.secret);
                memory_fill(link_nearby.keys[0].psk, 0x5a,
                            sizeof link_nearby.keys[0].psk);
                memory_fill(link_client.cookie, 0x5a, sizeof link_client.cookie);
                memory_fill(link_inbound[1], 0x5a, 64);
                copy = system_fork();
                if (!copy)
                {
                        link_secrets_forget();
                        system_call_1(
                            syscall(exit_group),
                            memory_span_byte(address_of link_self, 0,
                                             sizeof link_self) == sizeof link_self &&
                                    memory_span_byte(address_of link_nearby, 0,
                                                     sizeof link_nearby) ==
                                            sizeof link_nearby &&
                                    memory_span_byte(address_of link_client, 0,
                                                     sizeof link_client) ==
                                            sizeof link_client &&
                                    memory_span_byte(link_inbound, 0,
                                                     sizeof link_inbound) ==
                                            sizeof link_inbound
                                ? 0
                                : 1);
                }
                check("sec: a copy of the listener that has forgotten the link "
                      "holds none of it: its key, the groups' keys, the "
                      "initiation out and the datagrams last opened",
                      copy > 0 &&
                              system_wait4_retry(copy, address_of raw, 0, null) ==
                                      copy &&
                              raw == 0);
                memory_zero(link_self.me.secret, sizeof link_self.me.secret);
                memory_zero(link_nearby.keys[0].psk, sizeof link_nearby.keys[0].psk);
                memory_zero(link_client.cookie, sizeof link_client.cookie);
                memory_zero(link_inbound[1], 64);
        }
}

b32 main(void)
{
        bipolar listener;
        p16 port;

        if (!wls_sandbox())
        {
                string_format(log, "waterlink service: NOT RUN -- no user "
                                   "namespace here\n");
                log_flush();
                return 2;
        }
        wls_identity(address_of wls_server, 81);
        wls_identity(address_of wls_client, 82);
        wls_identity(address_of wls_b, 83);

        listener = wls_listener(address_of port);
        link_self.socket = socket_new(AF_INET6, SOCK_DGRAM | SOCK_CLOEXEC |
                                                    SOCK_NONBLOCK, 0);
        if (listener < 0 || link_self.socket < 0)
        {
                string_format(log, "waterlink service: NOT RUN -- no loopback "
                                   "IPv6 socket here\n");
                log_flush();
                return 2;
        }

        authorization_files();
        mdns_apart();
        identity_creation_races();
        staging();
        publication();
        own_files();
        missing_own_files();
        standard_stream_prepare_failures();
        stream_failures();
        request_answers();
        names_and_words();
        bind_events();
        control_messages();
        private_records_never_wait();
        reaped_commands();
        responder(listener, port);
        session_caps(listener, port);
        cookies(listener, port);
        stamps_outlive(listener, port);
        client_cookie(listener, port);
        initiator_answer();
        carried_is_atomic();
        carried_lengths();
        carried_replay();
        epochs_generated();
        seen_once_a_second();
        segment_runs(listener, port);
        in_order_sends(listener, port);
        coalesced_runs();
        wait_processing_latency();
        idle_grant_refresh();
        terminal_sessions_never_stream();
        quiet_streams();
        control_records_are_canonical();
        greetings(listener, port);
        greeting_answers(listener, port);
        greeting_freshness(listener, port);
        mdns_amplification();
        mdns_interface_selection();
        mdns_names_one();
        mdns_hop_limit();
        pairing_codes(listener, port);
        group_revocations(listener, port);
        nearby_reload();
        nearby_address();
        nearby_tick();
        groups_scrub_edges();
        groups_unreadable();
        nearby_reload_failures();
        records_named();
        machine_names();
        labels();
        wpa_key();
        wifi_key_checks();
        wifi_sae_checks();
        wifi_h2e_checks();
        wifi_sae_frame_checks();
        wifi_unwrap_checks();
        wifi_ptk_checks();
        wifi_rsn_checks();
        wifi_family_checks();
        wifi_interface_checks();
        wifi_features_checks();
        wifi_news_checks();
        wifi_netlink_checks();
        wifi_password_checks();
        wifi_last_checks();
        wifi_scan_checks();
        wifi_wait_checks();
        wifi_keeper_checks();
        wifi_source_checks();
        wifi_air_capacity();
        key_text();
        places();
        ipv4_only();
        client_never_waits();
        indexes_and_commands();
        discovery_switch_rows();
        return test_report(null);
}
#endif /* CHECK_waterlink_service */

