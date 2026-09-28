/*
        Waterlink: one authenticated link, many kinds of traffic.

        Not a remote shell. A shell is one thing that can ride this, beside a
        desktop, a log, a file, and whatever an application wants to send --
        and the link does not know which is which.

        Dawn Larsson - Apache-2.0 license
        github.com/dawnlarsson/moonwater

        WHY NOT SSH, AND WHY NOT TLS

        There are no users here, so SSH's authentication half answers a
        question this machine never asks: not "may alice log in" but "is this
        machine willing to talk to that operator, and what may they do". A
        static key pair answers it. TLS would bring certificates and a version
        matrix for a peer we already know by key. The handshake is Noise_IK
        over what lib.c already has -- X25519, HKDF, AES-GCM, SHA-256 -- as
        Noise_IK_25519_AES128GCM_SHA256: the standard's AESGCM is AES-256,
        which lib.c does not carry, and handshake.c says why the name had to
        change with it.

        Datagrams, not a stream, because a keystroke must not wait behind a
        video frame and one TCP connection cannot promise that: a lost segment
        stalls everything behind it. Head of line blocking is not a parameter,
        it is the protocol.

        THE KEY IS THE WHOLE DESIGN

        A frame carries a key, a number below sixty four that the
        application gives its own meaning, and a key is one of two things,
        fixed by its first frame:

                a stream: every frame arrives, once, in order -- a
                terminal's bytes, a file, a request;
                a register: only the newest value matters -- a window's
                size, a cursor, a screen.

        Frames on different keys have no order between them, which is what
        keeps a keystroke from waiting behind a file. A key being one kind
        and not both is what makes this small: no frame ever has to say
        which frame it comes after, since on a stream it is the one before
        and on a register it is none.

        THE RULE THAT MAKES IT CORRECT

        A register frame not yet delivered may be replaced by a newer one.

        That sentence is the scheduler. It throws away superseded state and
        never an event, because events ride streams.

        AND THE ONE THAT IS EASY TO GET WRONG

        Losing a superseded value is correct; losing a stream frame, or the
        last value of a register, is not. So every frame is held by its
        sender until the far side acknowledges it, and the acknowledgement is
        per key and cumulative: "this key is taken through sequence n, and of
        the sixty four after it I hold these". A register frame superseded
        while it waits is not sent again; its slot carries the current value
        instead, under a new sequence, so what is retransmitted is always the
        current value. That is fixed size per slot, which is why the
        datapath allocates nothing.

        Per key and not per datagram, because the key is already the unit of
        order: one number per key says everything a receiver has, where a
        datagram's acknowledgement would still have to be turned back into
        which frames of which keys it carried. It is how QUIC acknowledges
        streams, without QUIC's packet ranges under it.

        THE ACKNOWLEDGEMENT IS THE CREDIT

        Taken means the application took it, not that it arrived. An
        application that cannot take a frame now -- a pipe that is full, a
        disk that is slow -- says so, and the frame is held; the sender hears
        it is held and does not send it again, and a key may run only sixty
        four frames past what was taken, so the sender stops. There is no
        second flow control on top: the window the link keeps anyway is the
        one the reader controls. The one copy a held key's sender does send
        is its oldest frame, when none of the key is in flight, less and less
        often up to a minute: the acknowledgement that the reader came back
        can be lost too, and nothing else would ask.

        A receiver delivers a stream frame when it is the next and holds it
        when it is ahead; it takes a register frame when it is newer than
        what it has. Anything at or behind what the key has taken is dropped,
        so a stream frame arrives exactly once and in order however the
        network reorders, loses or repeats datagrams. The hold-back is a
        fixed pool; a frame that finds it full is dropped unacknowledged and
        comes again.

        A key's sequence counts from one and never wraps: a key carries at
        most 2^32 - 2 frames. LAST ends a key for good, and the receiver
        remembers that it ended, so a copy of the last frame the sender
        repeats because the acknowledgement was lost is not taken for
        something new.

        WHAT A PEER CAN SPEND

        Every table here belongs to one link, and a link is one session with
        one peer: the queue, the hold-back, the key tables, the replay window.
        A peer that opens every key it can and never ends one fills its own
        session's table and stops its own traffic, and nobody else's. That is
        the trust boundary, stated rather than bounded per channel: an
        authenticated peer may waste the session it holds, not the machine.
        What keeps a peer from holding many sessions is the handshake's
        limit, not this file.

        A link is about 450 KB, taken once when the session is made and never
        grown: 300 KB of it the send slots, which are the window, and 150 KB
        the hold-back, which is what exactly-once and the reader's credit
        cost. It is kept rather than cut, because a smaller queue is a
        smaller window and the window is the throughput on any path longer
        than a room.

        GROUPS: MACHINES THAT PAIR BY THEMSELVES

        Pairing by key is one person with two machines in front of them. A
        headless box installed somewhere nobody will stand is the other case,
        and for it a machine joins a group -- a namespace and a secret --
        and pairs by itself with every member it finds on the same local
        network (discover.c finds them, nearby.c greets them). The rules:

        The secret is the grant. Whoever holds it gets, on every member, what
        that member's join line granted, and the verbs when it granted
        nothing. Taking one machine out of a group is changing the secret on
        all the others, and forgetting the one that left; there is no list of
        members to strike a name from, only the secret.

        The secret is also an identity: every member derives the same group
        key pair from it, and pairing is the ordinary handshake addressed to
        that key, with a key from the secret mixed in (Noise IKpsk1). A member
        greets every machine it finds that way, and a machine that can read
        the greeting knows the sender holds the secret and the static key it
        sent, keeps it as a peer carrying the group's mark, and greets it
        back if it was new. There is no second handshake, and no pairing
        state: a greeting is one datagram and is never answered. A record
        that is already there, by hand or by another group, is never replaced
        or widened.

        Nothing announced names the group, the machine or its key. What is on
        the network is that a waterlink machine is here, under labels drawn
        at every start. Discovery never leaves the local link: mDNS on
        224.0.0.251, believed only at TTL 255, which no router forwards.
        There is no rendezvous and no NAT traversal.

        A weak secret can be guessed offline by anyone who captures one
        greeting, so every key comes from the secret through
        WATERLINK_GROUP_ROUNDS of PBKDF2, a protocol constant, and a secret
        the machine makes itself has 160 random bits. The secret itself is
        not kept: /root/link.groups, root's alone, holds what was derived
        from it.

        A machine in no group announces nothing and greets no one.

        HOW MUCH MAY BE IN FLIGHT

        A sender must not put more in flight than the path carries, and must
        not send its window as one burst: it keeps an estimate of the round
        trip, counts a frame lost when frames sent after it have arrived or a
        timer runs out, gives the path less when frames are lost and more when
        they are not, and paces what it sends across the round trip. Which
        algorithm does that is the sender's own and never crosses the wire;
        link.c says which it uses. Acknowledgements and urgent frames are not
        held by it: the first are what open the window, and the second are a
        few bytes somebody is waiting to see.

        URGENCY IS PER SEND

        A caller names the urgency on each send, because a terminal carrying
        a file for a moment is the ordinary case: an urgent frame leaves at
        once and alone, the rest share the window. It crosses the wire only
        because it asks the far side to acknowledge at once.

        This file is the contract and nothing else, included where lib.c's
        types are already in scope.
*/

#ifndef WATERLINK_INCLUDED
#define WATERLINK_INCLUDED

/*      1200 bytes clears the common 1280 minimum with room for an outer
        header and asks nothing of path discovery. A larger frame is the
        sender's to split: reassembly is a queue, and a queue is a place to
        store an attacker's bytes.

        A datagram is cut to whole blocks, as WireGuard's are, unless its box
        has no room left for another frame: then it is padded to this. So a
        keystroke is 48 bytes on the wire and three blocks of cipher, not
        1200 and seventy five, and a run of full frames is still all one
        size -- which is what segment offload needs, and segments are the
        lever: measured on a 9950X, 2516 cycles a datagram sent one at a time,
        2355 batched through sendmmsg, 461 as uniform segments. Batching the
        syscall is worth six percent; batching the segments is worth five
        times.

        What that gives up is hiding a small frame's length. It was never
        hidden well: the moment a datagram leaves says as much as its size,
        and WireGuard makes the same choice. */
#define WATERLINK_DATAGRAM 1200
#define WATERLINK_PAYLOAD (WATERLINK_DATAGRAM - 16 - 16)

/*      Read before any key is in hand, so it carries only what a receiver
        needs to find one. receiver is the index the far side handed out at
        handshake; counter is the nonce and the replay sequence both. */
struct waterlink_datagram {
        unsigned int kind;     // WATERLINK_KIND_*
        unsigned int receiver;
        unsigned long counter;
};

_Static_assert(sizeof(struct waterlink_datagram) == 16,
               "waterlink datagram header must be exactly 16 bytes");

#define WATERLINK_KIND_INITIATE 1u // Noise_IK message one
#define WATERLINK_KIND_RESPOND 2u  // Noise_IK message two
#define WATERLINK_KIND_CARRY 3u    // frames, once the session is up
#define WATERLINK_KIND_CLOSE 4u
#define WATERLINK_KIND_COOKIE 5u   // a listener under load: ask again with this

/*      A frame, inside the box, so the code that parses attacker-shaped bytes
        only ever runs on bytes that were already authenticated.

        On the wire a frame is its flags byte, its key byte, and two numbers
        -- the sequence and the payload's length -- each seven bits a byte,
        low first, with the top bit saying another follows (LEB128); then the
        payload. A keystroke's frame is four bytes of header where a fixed
        layout took twenty eight. Each number has one spelling -- no trailing
        zero byte, nothing past sixty four bits -- so a body means one thing.
        A zero flags byte is where the frames stop: every frame has a class
        or is an acknowledgement, and the rest of the box is zeros.

        In memory it is this, whole: what a receiver hands on, and what it
        holds back. */
struct waterlink_frame {
        p32 sequence; // per key, from one, increasing
        p16 length;   // the payload's bytes
        p8 key;       // below WATERLINK_KEYS; the application's meaning
        p8 flags;     // WATERLINK_FRAME_*
};

#define WATERLINK_KEYS 64

// The most a frame's header takes: flags, key, a 32-bit sequence, a length.
#define WATERLINK_HEADER_MOST (1 + 1 + 5 + 2)

// A frame's key class: a register (replaceable) or a stream (durable).
#define WATERLINK_FRAME_REPLACEABLE 0x01u
#define WATERLINK_FRAME_DURABLE 0x02u
#define WATERLINK_FRAME_LAST 0x04u // the key ends with this frame

/*      The link's own frame, and the only one with neither class: the flags
        byte, the key, and two numbers -- what the key is taken through, and
        a mask of which of the sixty four sequences after that are held. One
        per key, back to back with the frames, so the acknowledgement of a
        keystroke is four bytes. A caller cannot post one. */
#define WATERLINK_FRAME_ACK 0x08u
#define WATERLINK_ACK_MOST (1 + 1 + 5 + 10)

#define WATERLINK_FRAME_URGENT 0x10u // a keystroke; nothing waits behind it

#define WATERLINK_FRAME_WIRE                                                   \
        (WATERLINK_FRAME_REPLACEABLE | WATERLINK_FRAME_DURABLE |              \
         WATERLINK_FRAME_LAST | WATERLINK_FRAME_URGENT)

/*      Which is why sending is two paths and not one. Segments only go out
        in a run, and a run is built by waiting for the next frame -- so an
        urgent frame leaves alone, immediately, at the unbatched price, and
        bulk accumulates into a run for one peer at one size. A scheduler that
        put a keystroke in a segment run would be holding it for forty frames
        it has nothing to do with, which is the thing this link exists to
        refuse. The price of being right here is known and small: an urgent
        frame costs 2516 cycles where a bulk one costs 461. */

/*      What a peer may do, granted one at a time and starting at none. VERBS
        is the moonwater vocabulary the machine already answers to and is what
        a freshly paired peer gets: it cannot execute, open a terminal or
        write a file. Everything past it is a decision somebody made. */
#define WATERLINK_MAY_VERBS 0x0001u
#define WATERLINK_MAY_RUN 0x0002u      // one command, no terminal
#define WATERLINK_MAY_SHELL 0x0004u    // a terminal
#define WATERLINK_MAY_SCREEN 0x0008u   // watch the desktop
#define WATERLINK_MAY_FILES 0x0010u    // push and pull
#define WATERLINK_MAY_LOG 0x0020u      // follow the kernel log
#define WATERLINK_MAY_CHANNELS 0x0040u // open channels of its own

#define WATERLINK_MAY_DEFAULT WATERLINK_MAY_VERBS

/*      AES-128-GCM, and not 256: it is the key size lib.c carries in assembly
        on all three machines, with a bitsliced floor that a kernel build
        keeps. With no cipher agility the choice is made once, and it is made
        for the one that is fast everywhere this link runs. */
#define WATERLINK_KEY_BYTES 32  // X25519
#define WATERLINK_AEAD_BYTES 16 // AES-128
#define WATERLINK_TAG_BYTES 16
#define WATERLINK_NAME_MAX 32

/*      The datagram is whole AES blocks, and that is chosen, not found: the
        cleartext header is one block and is exactly GCM's associated data,
        the box is seventy three, and the tag is one. So sealing a datagram is
        one counter-mode call over header-and-box and one GHASH call over all
        seventy five, the lengths block borrowing the tag's slot -- no partial
        block anywhere, and no tail for any of the assembly under it. */
_Static_assert(WATERLINK_DATAGRAM % 16 == 0 && WATERLINK_PAYLOAD % 16 == 0,
               "a waterlink datagram must be whole AES blocks");
_Static_assert(WATERLINK_DATAGRAM == 16 + WATERLINK_PAYLOAD + WATERLINK_TAG_BYTES,
               "header, box and tag must be the whole datagram");

/*      A peer, as this machine keeps it -- the far side never sees this
        record. An address is a cache and not an identity: a machine that
        moves keeps its key and loses its address, which is the ordinary case
        on wifi and the reason a name binds to a key rather than a place.
        Kept in /root/link.peers, root's alone. */
struct waterlink_peer {
        unsigned char key[WATERLINK_KEY_BYTES];
        char name[WATERLINK_NAME_MAX];
        unsigned int may;   // WATERLINK_MAY_*
        unsigned int group; // 0 paired by hand, or the mark of the group
                            // that paired it (discover.c)
        unsigned char address[16]; // last seen, v6 or v4 mapped
        unsigned short port;
        unsigned short address_flags;
        unsigned int seen; // group-paired: the member's last greeting stamp,
                           // older ones are replays; 0 for never
};

_Static_assert(sizeof(struct waterlink_peer) == 96,
               "waterlink peer record must be exactly 96 bytes");

/*      Rekeying is a correctness rule and not a policy: the counter is the
        nonce, so a session ends long before it could repeat one. WireGuard's
        numbers, for WireGuard's reason -- whichever comes first. */
#define WATERLINK_REKEY_MESSAGES (1UL << 40)
#define WATERLINK_REKEY_SECONDS 120
#define WATERLINK_REJECT_SECONDS 180

// How far behind the highest counter a datagram may arrive and still be new.
#define WATERLINK_REPLAY_WINDOW 2048

#endif // WATERLINK_INCLUDED
