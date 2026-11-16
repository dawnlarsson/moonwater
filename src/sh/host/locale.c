/*
        src/sh/host/locale.c: the timezone and the clock (SNTP, NTS, the zone found and set), the keyboard's layout
        and the canvas's modes: the work under the verbs time, timezone, ntp and keyboard, which are debug.c's.

        A run of src/sh/host.c, which includes this file where the run stood (test/split.py cut it there): it is
        compiled only as part of that file, with its statics and macros, and never on its own. host_main, at the end
        of src/sh/host.c, names the function each verb runs.
*/

/* ---- locale: the timezone and the clock, and the SNTP that sets it. ---- */

/*
        Timezone, NTP and keyboard layout, as moonwater verbs.

        Choices live on /root so an image update keeps them. The machine
        starts NTP itself: restore forks the first query before init, and the
        wait loop keeps walking servers until the clock is set. Sampling takes
        five samples and keeps the lowest delay unless /root/ntp.sampling
        says off. The kernel
        adds that offset with adjtimex; a kiss-o-death drops the server.

        The forked query is not asked after with kill. A pid that has
        exited but not been waited for is still a pid, so kill(pid, 0)
        answers zero for a zombie exactly as it does for a live child: the
        poll would see its first query running for ever. It says how it
        went on a pipe (locale_child), and is waited for once it has.

        THE STEP AND THE SLEW HAVE BEEN RUN

        Both have been carried out against a kernel: KVM guests were set and
        slewed by this code and read back against a panel of stratum-1
        servers, one reading a minute (55d6b775 has the figures), and the
        choice between stepping and slewing is checked against crafted
        offsets either side of the threshold in the machine lane. Every ADJ_
        and STA_ value here is also held against uapi/linux/timex.h, and
        every timex word index against the struct's own offsets.

        The reason to read a granted call apart from an accepted one is in
        the constants below. ADJ_SETOFFSET was 0x80, which is ADJ_TAI, for as long as
        this file has had it -- so no correction this program ever
        computed reached the clock. It survived because adjtimex answers a
        wrong mode the same way it answers a right one: with the clock
        state, a non-negative number the caller reads as success. It also
        cleared STA_UNSYNC on the way past, so the machine went on to
        report itself synchronised. Anyone adding a mode here should know
        that a non-negative return proves the call was accepted and
        nothing else, and that there is no test that can tell you
        otherwise, because nothing unprivileged can ask the kernel to
        demonstrate which mode a bit meant.
*/

/* ---- SNTP: how the clock above is asked what the time is. ---- */

/*
        Experimental C standard library

        SNTP: RFC 5905 on-wire offset and delay, then a clock filter

        A single sample's offset error is (d_fwd - d_rev) / 2, and that is
        bounded by half the round-trip delay. Integer-second arithmetic and
        planting the server's transmit fraction as tv_nsec both throw that
        bound away. Five samples with the smallest delay kept is the RFC
        5905 clock filter, not an average: averaging a one-sided queue spike
        poisons the estimate.

        The clock is stepped by that offset with adjtimex ADJ_SETOFFSET, not
        by reading the clock again and planting a new wall time: the gap
        between those two traps would be extra error. A machine whose clock
        is still at the epoch must be allowed a decades-long first step; one
        whose clock already looks like a civil date may not jump more than a
        day, and a refresh of a synchronised clock may not jump more than
        two seconds. Past two seconds no single server is believed: another
        answer has to agree (sntp_choose). A kiss-o-death, a stratum 0, or a
        root delay/dispersion worse than a second abandons that server
        rather than collecting more samples from it.

        No step lands before SNTP_WALL_LEAST, which is not a guess at what
        clocks read but the date of this source (MOONWATER_CLOCK_FLOOR in
        net.c, where the certificate check reads the same floor): a clock
        set earlier than the code that set it is wrong by construction,
        which is systemd's TIME_EPOCH rule. Moving it forward with each
        release is what keeps an answer from taking the clock back to when a
        revoked or expired certificate was still good. And no step lands
        before clock_trust_floor either, the latest time an authenticated
        answer or this machine's own uptime ever put in /root/clock.good, so
        a forged answer cannot take a machine back to a date it was already
        past.

        SNTP_WALL_MOST ends that window at the last second sntp_load_stamp
        can read, 2104-02-26 09:42:23 UTC: a stamp whose high bit is set is
        era 0 (1968 through 2036-02-07 06:28:16, when the 32 bit seconds
        wrap), one whose high bit is clear is era 1 (that moment through
        2104), and a stamp does not say which, so past 2104 the reading
        would be wrong by 136 years. It used to end at 2036-01-01 -- a round
        date before the wrap -- and a clock past it could no longer be set
        from the network at all: every server's answer was out of the window.
        The offset sum is two differences of four terms the window bounds,
        and at 2104 it is 8.47 of the 9.22 the type holds.

        T1 is read, the originate stamp is written, and the packet is sent
        with nothing else in between. T4 is read the moment recv returns.
        The conversion of those timespecs into nanoseconds waits until after
        the trap. This tree has no vDSO: clock_gettime is a syscall, and
        that cost dwarfs the stores, so the C must not add a second one.

        WHAT HAS BEEN MEASURED, AND WHAT HAS NOT

        Against five servers at once, with the box's own clock held
        synchronised by something else as the reference, this file's
        answers sat within 193 microseconds of it; a single-sample client
        with userspace stamps, asked the same servers in the same minute,
        spread to 1207. Both agree on sign and scale, so the difference
        is the five-sample filter and the kernel stamps, not a disagreement
        about what time it is.

        The two kernel stamps are worth, at the median of real exchanges:
        10355 ns for the arrival stamp, and 1729 ns to one server and 2320
        to another for the departure stamp. Both are one-sided, which is
        why they land in the offset at all -- the formula assumes the path
        is symmetric. Round-trip asymmetry itself cannot be measured from
        one end and so cannot be corrected here; across the servers above
        it accounts for a spread of several milliseconds, which is larger
        than everything this file does about anything else.

        What is not proven is the clock being set. Nothing here can take
        CLOCK_REALTIME on a machine that is not ours to disturb, so the
        step and slew paths in the locale section below have never been
        executed against a
        kernel that carried them out. What is checked is the request: the
        mode words against uapi/linux/timex.h, and the decision between
        stepping and slewing against crafted offsets in the machine lane.
        A reader should take "the right thing is asked for" from this and
        not "the asking has been seen to work".

        Dawn Larsson - Apache 2.0 license
        github.com/dawnlarsson/moonwater

        www.dawning.dev
*/

#ifndef STANDARD_MODERN_C_NET_SNTP
#define STANDARD_MODERN_C_NET_SNTP

#define SNTP_PORT 123
#define SNTP_PACKET 48
/* One byte beyond the 48-byte message lets recvmsg tell an exact reply from a
   longer datagram.  The default reads the first 48 bytes of a longer one, as
   ntpd and chrony do (extension fields, a MAC); the tight tier takes only the
   exact shape, because bytes after the message are otherwise accepted without
   being parsed.  A buffer of exactly SNTP_PACKET would make both read 48. */
#define SNTP_REPLY_ROOM (SNTP_PACKET + 1)
#define SNTP_SECONDS 2
#define SNTP_SAMPLES 5
#define SNTP_SERVERS 3
#define SNTP_UNIX 2208988800u
#define SNTP_LI_VN_MODE 0x23
#define SNTP_NANOSECONDS 1000000000ull
#define SNTP_OK 0
//      A query's answers are numbers of their own, far from the errors a
//      system call gives: the clock's refusal to be set is one of those, and
//      -EPERM was SNTP_NO_SERVER and -EIO was SNTP_RATE_LIMITED, so file_reason
//      said "No such process" of an answer out of range.
#define SNTP_ANSWER (-100000)
#define SNTP_NO_SERVER (SNTP_ANSWER - 1)
#define SNTP_NO_REPLY (SNTP_ANSWER - 2)
#define SNTP_MALFORMED (SNTP_ANSWER - 3)
#define SNTP_BAD_SERVER (SNTP_ANSWER - 4)
#define SNTP_RATE_LIMITED (SNTP_ANSWER - 5)
#define SNTP_UNCONFIRMED (SNTP_ANSWER - 6)
#define SNTP_DENIED (SNTP_ANSWER - 7)
//      The answer was earlier than this machine has already been: its build,
//      or the last time it was sure (clock_trust_floor).
#define SNTP_BELOW_FLOOR (SNTP_ANSWER - 8)
//      An NTS server said its cookie was no good (NTSN): establish keys again.
#define SNTP_NTS_NAK (SNTP_ANSWER - 9)
//      A plain answer put the clock further from the time NTS gave this boot
//      than the crystal can have drifted since.
#define SNTP_CONTRADICTS (SNTP_ANSWER - 10)
//      The query could not be confined (net_apart), so nothing was asked; or
//      its confined child ended without an answer that could be believed.
#define SNTP_CONFINE (SNTP_ANSWER - 11)
#define SNTP_APART (SNTP_ANSWER - 12)
#define SNTP_KISS_RATE 0x52415445u /* "RATE" */
#define SNTP_KISS_DENY 0x44454e59u /* "DENY" */
#define SNTP_KISS_RSTR 0x52535452u /* "RSTR" */
#define SNTP_DELAY_MOST_NS ((bipolar)2 * (bipolar)SNTP_NANOSECONDS)
#define SNTP_OFFSET_MOST_NS ((bipolar)24 * 3600 * (bipolar)SNTP_NANOSECONDS)
#define SNTP_OFFSET_SYNCED_NS ((bipolar)2 * (bipolar)SNTP_NANOSECONDS)
#define SNTP_WALL_LEAST MOONWATER_CLOCK_FLOOR
#define SNTP_WALL_MOST 4233462143ll  /* 2104-02-26 09:42:23, the era 1 stamp ends */
#define SNTP_WALL_LEAST_NS \
        ((bipolar)SNTP_WALL_LEAST * (bipolar)SNTP_NANOSECONDS)
#define SNTP_WALL_MOST_NS \
        ((bipolar)SNTP_WALL_MOST * (bipolar)SNTP_NANOSECONDS)
//      What a person who asks for the time by hand may be told to step by:
//      anything the window holds.
#define SNTP_OFFSET_ANY_NS (SNTP_WALL_MOST_NS - SNTP_WALL_LEAST_NS)
#define SNTP_TIMESPEC_SECONDS_MOST 9223372035ull
#define SNTP_ERA ((bipolar)4294967296)
#define SNTP_SHORT_SECOND 0x10000u
#define SNTP_TIMESTAMPNS 35
#define SNTP_TIMESTAMPING 37
#define SNTP_TIMESTAMPING_WANT 2194u /* TX_SOFTWARE|SOFTWARE|OPT_ID|TSONLY */
#define SNTP_ERRQUEUE 0x2000
#define SNTP_DONTWAIT 0x40
#define SNTP_MESSAGE_WORDS 7
#define SNTP_CONTROL_WORDS 24
#define SNTP_MSG_CTRUNC 0x8
#define SNTP_CONTROL_HEAD (sizeof(positive) + 8)
#define SNTP_ERRQUEUE_MOST 4
#define SNTP_SOL_IP 0
#define SNTP_IP_RECVERR 11
#define SNTP_ERROR_TIMESTAMPING 4 /* SO_EE_ORIGIN_TIMESTAMPING */
#define SNTP_ERROR_BYTES 16       /* struct sock_extended_err */
#define SNTP_ERROR_ORIGIN 4       /* ee_origin within it */
#define SNTP_ERROR_SEQUENCE 12    /* ee_data, which carries the id */
#define SNTP_CONTROL_DATA                          \
        ((SNTP_CONTROL_HEAD + sizeof(positive) - 1) & \
         ~(sizeof(positive) - 1))

typedef struct
{
        bipolar offset_ns;
        bipolar delay_ns;
        bipolar distance_ns;
        bool ok;
        //      The server's address, so that one server answering under three
        //      names is counted once.
        p32 address;
} sntp_sample;

static inline INLINE CONST bool sntp_wall_ok(bipolar ns)
{
        return ns >= SNTP_WALL_LEAST_NS && ns <= SNTP_WALL_MOST_NS;
}

static inline INLINE CONST bool sntp_within(bipolar ns, bipolar most)
{
        return ns >= -most && ns <= most;
}

/*
        A local stamp may sit anywhere from the epoch to the end of the
        window, because a machine that has never been told the time boots
        at zero. It may not sit past the window: t1 and t4 are the only
        two terms sntp_offset_delay adds that are not already bounded by
        the wire format, and an unbounded one overflows the sum.
*/
static inline INLINE CONST bool sntp_local_ok(bipolar ns)
{
        return ns >= 0 && ns <= SNTP_WALL_MOST_NS;
}

static inline INLINE CONST bipolar sntp_timespec_ns(p64 seconds, p64 nanoseconds)
{
        if (nanoseconds >= SNTP_NANOSECONDS || seconds > SNTP_TIMESPEC_SECONDS_MOST)
                return -1;
        return (bipolar)seconds * (bipolar)SNTP_NANOSECONDS +
               (bipolar)nanoseconds;
}

static inline INLINE bipolar sntp_now_ns(void)
{
        p64 now[2] = {0, 0};

        if (system_call_2(syscall(clock_gettime), CLOCK_REALTIME,
                          (positive)now) < 0)
                return -1;
        return sntp_timespec_ns(now[0], now[1]);
}

/*
        The transmit stamp goes out to be echoed back, and the echo is the
        only thing telling us a reply is ours. Nothing reads this field as a
        clock: t1 comes from the kernel's transmit timespec. It is therefore
        an authenticator, not a timestamp, and every one of its 64 bits can
        and must be unpredictable.

        This used to put the public wall-clock seconds in the high half and
        ask for the low half with GRND_NONBLOCK. On a machine whose pool was
        not ready it fell back to the equally public clock fraction, leaving
        only the UDP source port between an off-path forged reply and a clock
        change. Blocking for the CSPRNG is the same policy DNS uses for its
        transaction id: no entropy means no security-sensitive transaction.
*/
static inline INLINE bool sntp_put_stamp(p8 address_to field)
{
        return system_random_fill(field, 8, 0) == 0;
}

static inline INLINE PURE bipolar sntp_load_stamp(p8 address_to field)
{
        p32 ntp_seconds = network_load_32(field);
        p32 ntp_frac = network_load_32(field + 4);
        bipolar unix_seconds = (bipolar)ntp_seconds - (bipolar)SNTP_UNIX;
        bipolar unix_nsec =
            (bipolar)(((p64)ntp_frac * SNTP_NANOSECONDS) >> 32);

        if (!(ntp_seconds & 0x80000000u))
                unix_seconds += SNTP_ERA;
        return unix_seconds * (bipolar)SNTP_NANOSECONDS + unix_nsec;
}

static inline INLINE CONST bool sntp_short_ok(p32 word)
{
        return !(word & 0x80000000u) && word <= SNTP_SHORT_SECOND;
}

// An NTP short, sixteen bits of seconds and sixteen of fraction, in
// nanoseconds; sntp_short_ok has held it to a second.
static inline INLINE CONST bipolar sntp_short_ns(p32 word)
{
        return (bipolar)(((p64)word * SNTP_NANOSECONDS) >> 16);
}

/*
        How far this sample's offset can be from the truth: half its own
        round trip, which the path's asymmetry can take all of, plus how far
        the server's clock can be from the source it follows -- half its
        root delay and the whole of its root dispersion. RFC 5905 calls the
        sum the root distance and ranks servers by it. Half the round trip
        alone was what the kernel was told the error was, which leaves out
        the server's own and understates it for any server not at stratum 1.
*/
static inline INLINE CONST bipolar sntp_distance_ns(bipolar delay_ns,
                                                    p32 root_delay,
                                                    p32 root_dispersion)
{
        return delay_ns / 2 + sntp_short_ns(root_delay) / 2 +
               sntp_short_ns(root_dispersion);
}

static inline INLINE fn sntp_offset_delay(bipolar t1, bipolar t2, bipolar t3,
                                          bipolar t4,
                                          bipolar address_to offset_ns,
                                          bipolar address_to delay_ns)
{
        address_to offset_ns = ((t2 - t1) + (t3 - t4)) / 2;
        address_to delay_ns = (t4 - t1) - (t3 - t2);
}

static CONST COLD bool sntp_sample_sane(bipolar t1, bipolar t2, bipolar t3,
                                   bipolar t4, bipolar offset_ns,
                                   bipolar delay_ns, bipolar most)
{
        if (t1 < 0 || t4 < t1 || t3 < t2 || !sntp_wall_ok(t2) || !sntp_wall_ok(t3) ||
            delay_ns < 0 || delay_ns > SNTP_DELAY_MOST_NS)
                return false;
        //      A clock that tells the time already is moved by at most
        //      what was asked; one that does not has nothing to be moved
        //      from, and the window bounds what it can be told.
        if (most <= SNTP_OFFSET_SYNCED_NS)
                return sntp_within(offset_ns, most);
        return !sntp_wall_ok(t1) || sntp_within(offset_ns, most);
}

static COLD bool sntp_target_ok(bipolar now, bipolar offset_ns,
                           bipolar address_to target)
{
        if (offset_ns > 0 && now > bipolar_max - offset_ns)
                return false;
        if (offset_ns < 0 && now < bipolar_min - offset_ns)
                return false;
        address_to target = now + offset_ns;
        return sntp_wall_ok(address_to target);
}

static PURE COLD bipolar sntp_pick(sntp_sample address_to row, positive count)
{
        bipolar best = -1;
        positive at;

        for (at = 0; at < count; at++)
                if (row[at].ok &&
                    (best < 0 || row[at].delay_ns < row[best].delay_ns ||
                     (row[at].delay_ns == row[best].delay_ns &&
                      (bipolar)at > best)))
                        best = (bipolar)at;
        return best;
}

/*
        Which server's answer to believe, of up to SNTP_SERVERS. Each answer
        is an interval, its offset give or take its root distance, and the
        true offset lies in every correct server's. An answer whose interval
        meets fewer than half of the others' is a falseticker -- its clock is
        wrong, or its path is lopsided past what its round trip admits -- and
        is set aside; of the rest the least root distance wins, which is RFC
        5905's rule for the system peer. Taking the first server that
        answered, which is what was done, believed a falseticker whenever one
        answered first.

        Two that disagree cannot say which is wrong, and one cannot disagree
        with anything. They used to all stand, so the least root distance
        won -- a number the answer states about itself, which a forger sets
        to nothing: one on-path answer 2 h out beat the honest server it
        contradicted. Now no answer moves the clock past SNTP_OFFSET_SYNCED_NS
        unless another answer's interval meets it, and one that leaves the
        clock within that needs no second word. -1 when nothing may be
        believed.
*/
static PURE COLD bipolar sntp_choose(sntp_sample address_to row, positive count)
{
        bipolar best = -1;
        bool any_kept = false;
        bool kept[SNTP_SERVERS];
        positive meets[SNTP_SERVERS];

        for (positive at = 0; at < count; at++)
        {
                positive others = 0;

                kept[at] = false;
                meets[at] = 0;
                if (!row[at].ok)
                        continue;
                for (positive other = 0; other < count; other++)
                {
                        if (other == at || !row[other].ok)
                                continue;
                        //      The same server again is no second opinion,
                        //      whatever name it was reached under.
                        if (row[other].address == row[at].address)
                                continue;
                        others++;
                        if (row[at].offset_ns - row[at].distance_ns <=
                                row[other].offset_ns + row[other].distance_ns &&
                            row[other].offset_ns - row[other].distance_ns <=
                                row[at].offset_ns + row[at].distance_ns)
                                meets[at]++;
                }
                kept[at] = others < 2 || meets[at] * 2 >= others;
                any_kept |= kept[at];
        }

        for (positive at = 0; at < count; at++)
                if (row[at].ok && (kept[at] || !any_kept) &&
                    (meets[at] ||
                     sntp_within(row[at].offset_ns, SNTP_OFFSET_SYNCED_NS)) &&
                    (best < 0 || row[at].distance_ns < row[best].distance_ns))
                        best = (bipolar)at;
        return best;
}

/*
        t4 is meant to be when the reply arrived. Read after recv returns
        it is when this process next ran, which is the arrival plus however
        long the packet waited in the socket and however long the scheduler
        took to wake us. Half of that lands in the offset, and on an idle
        machine talking to a real server it measured a shade over eleven
        microseconds -- four hundred times the whole of the arithmetic that
        follows, and nothing the arithmetic can do anything about.

        SO_TIMESTAMPNS makes the kernel record the arrival in the softirq
        that takes the packet off the device and hand it over as a control
        message. Reading it needs recvmsg rather than recvfrom, and the
        socket calls in lib.c are recvfrom, so the trap is made here
        the way this file already traps for clock_gettime.

        msghdr is seven pointer-width words -- the name and its length,
        the vector and its count, the control buffer and its length, and
        the flags -- which is its shape on every target this tree builds.
        A control message is a pointer-width length, then a level and a
        type of four bytes each, then the payload at the next word.

        Nothing here is required to work. A kernel that refuses the option
        or a path that delivers no control message leaves the stamp unset,
        and the caller reads the clock itself exactly as before.
*/
/*
        The walk is apart from the trap because it is the half that fails
        quietly: a wrong offset finds no message, and a receive that found
        no message is indistinguishable from a kernel that sent none. That
        reads as "the timestamp did not help" rather than as a mistake, so
        it is reached here by its own name, and the machine check
        (sntp_math_ok in test/checks.c) hands it buffers laid out by
        hand -- including ones whose length words lie.

        Every field is read from a buffer the kernel filled, and the two
        lengths are believed only as far as the buffer goes: a message
        claiming to be longer than what is left ends the walk.
*/
static p8 address_to sntp_control_find(p8 address_to control, positive length,
                                       b32 level, b32 type, positive least)
{
        positive at = 0;

        while (at + SNTP_CONTROL_DATA <= length)
        {
                positive size = address_to(positive address_to)(control + at);

                if (size < SNTP_CONTROL_DATA || size > length - at)
                        break;
                if (address_to(b32 address_to)(control + at + sizeof(positive)) ==
                        level &&
                    address_to(b32 address_to)(control + at + sizeof(positive) +
                                               4) == type &&
                    size - SNTP_CONTROL_DATA >= least)
                        return control + at + SNTP_CONTROL_DATA;
                at += (size + sizeof(positive) - 1) & ~(sizeof(positive) - 1);
        }
        return null;
}

static bool sntp_control_stamp(p8 address_to control, positive length,
                               b32 kind, p64 address_to arrived)
{
        p8 address_to data = sntp_control_find(control, length, SOL_SOCKET,
                                               kind, 2 * sizeof(p64));

        if (!data)
                return false;
        arrived[0] = address_to(p64 address_to)data;
        arrived[1] = address_to(p64 address_to)(data + sizeof(p64));
        return true;
}

/*
        What of the control buffer is to be believed. The kernel says when
        it had more to put in than there was room for, and the stamps and
        the id are read in pairs, so a buffer it cut short is not read at
        all: the exchange takes its stamps from the clock, as it does with
        none. The buffer holds the three messages a send and a receive
        can leave, 32, 64 and 48 bytes, with room to spare.
*/
static inline INLINE CONST positive sntp_control_held(positive flags,
                                                      positive filled)
{
        return flags & SNTP_MSG_CTRUNC ? 0 : filled;
}

/*
        recvmsg into one buffer and the control words beside it, with the
        control length the kernel filled put in held.
*/
static HOT bipolar sntp_receive_message(b32 handle, p8 address_to into,
                                        positive room,
                                        positive address_to control,
                                        positive address_to held,
                                        positive flags)
{
        positive message[SNTP_MESSAGE_WORDS];
        positive vector[2] = {(positive)into, room};
        bipolar got;

        memory_zero(message, sizeof(message));
        memory_zero(control, SNTP_CONTROL_WORDS * sizeof(positive));
        message[2] = (positive)vector;
        message[3] = 1;
        message[4] = (positive)control;
        message[5] = SNTP_CONTROL_WORDS * sizeof(positive);
        got = system_call_3(syscall(recvmsg), (positive)handle,
                            (positive)message, flags);
        address_to held = got < 0 ? 0 : sntp_control_held(message[6], message[5]);
        return got;
}

static HOT bipolar sntp_receive_stamped(b32 handle, p8 address_to reply,
                                        positive room,
                                        p64 address_to arrived,
                                        bool address_to stamped)
{
        positive control[SNTP_CONTROL_WORDS];
        positive held = 0;
        bipolar got = sntp_receive_message(handle, reply, room, control,
                                           address_of held, SNTP_DONTWAIT);

        address_to stamped = sntp_control_stamp((p8 address_to)control, held,
                                                SNTP_TIMESTAMPNS, arrived);
        return got;
}

/*
        t1 has the same trouble t4 had, at the other end. It is read
        before the send trap, so it is the moment before the kernel is
        entered, and the packet leaves after the protocol stack has run.
        The offset formula assumes the two directions are symmetric, so a
        head start on the send side alone goes straight into the answer at
        half its size: measured against the kernel's own departure stamp,
        a median of 1729 ns to one server and 2320 ns to another.

        SOF_TIMESTAMPING_TX_SOFTWARE records the moment the packet is
        given to the driver and queues it on the socket's error queue.
        Measured on a real route it is already there when send returns, 50
        times out of 50, so one recvmsg that refuses to wait collects it
        and no poll is needed.

        Draining it is not optional once the option is on. A socket with
        anything on its error queue reports POLLERR, and the wait below
        asks about readability and would be woken by that for ever. One
        non-blocking read after each send empties it, which 100 exchanges
        across two servers confirm: no POLLERR survived into the wait.

        OPT_TSONLY keeps the packet itself off the queue, so what comes
        back is the timestamp and the error header beside it. The walk
        steps over anything that is not the timestamp, which is what lets
        a real ICMP error sit there without being mistaken for one.
*/
/*
        OPT_ID is already asked for, so the kernel numbers every transmit
        stamp with a counter that starts at zero when the option is set
        and rises by one per send. Five sends on one socket came back 0,
        1, 2, 3, 4.

        The number does not travel in the timestamp. It is in the error
        header beside it, as ee_data, and the same header says in
        ee_origin whether this queue entry is a timestamp at all or a
        real ICMP error that happens to be sitting there. Reading both is
        what makes the stamp provably the one belonging to the send being
        timed, rather than whichever stamp was on the queue -- a
        distinction that only bites if a drain is ever missed, which is
        exactly the case that cannot be tested from outside.

        A kernel that sends no error header, or one whose numbering does
        not line up, leaves the stamp unclaimed and the exchange falls
        back to the userspace reading, as it does when the option is
        refused outright.
*/
static bool sntp_control_sequence(p8 address_to control, positive length,
                                  p32 address_to sequence)
{
        p8 address_to body = sntp_control_find(control, length, SNTP_SOL_IP,
                                               SNTP_IP_RECVERR,
                                               SNTP_ERROR_BYTES);

        if (!body || body[SNTP_ERROR_ORIGIN] != SNTP_ERROR_TIMESTAMPING)
                return false;
        address_to sequence = address_to(p32 address_to)(body + SNTP_ERROR_SEQUENCE);
        return true;
}

static HOT bool sntp_transmit_stamp(b32 handle, p32 wanted,
                                    p64 address_to departed)
{
        positive control[SNTP_CONTROL_WORDS];
        p8 sink[SNTP_PACKET];
        p64 stamp[2];
        bool found = false;

        for (positive round = 0; round < SNTP_ERRQUEUE_MOST; round++)
        {
                positive held = 0;
                p32 sequence = 0;

                if (sntp_receive_message(handle, sink, sizeof(sink), control,
                                         address_of held,
                                         SNTP_ERRQUEUE | SNTP_DONTWAIT) < 0)
                        break;
                if (sntp_control_stamp((p8 address_to)control, held,
                                       SNTP_TIMESTAMPING, stamp) &&
                    sntp_control_sequence((p8 address_to)control, held,
                                          address_of sequence) &&
                    sequence == wanted)
                {
                        departed[0] = stamp[0];
                        departed[1] = stamp[1];
                        found = true;
                }
        }
        return found;
}

/*
        Everything below is read out of forty-eight bytes a stranger sent.
        The socket is connected, so the kernel has already dropped a
        datagram whose source is not the server's, but an on-path answer
        and a blind one aimed at an open port both arrive here.

        The origin stamp is the check that carries the weight: it is the
        transmit stamp we planted, echoed back, and an answer that does
        not carry it was not an answer to our question. It is dropped and
        the wait resumes rather than ending the exchange, because a late
        reply to an earlier sample is not a reason to give up on this one.

        Stratum zero is a kiss-o-death and the four bytes at 12 say which.
        RATE is the server asking to be asked less often, which is a
        different answer from DENY and RSTR, which tell this machine to
        stay away: each is reported separately so the policy above can wait
        instead of walking to the next server and asking again immediately,
        and wait longer for the one that says no.
*/
static COLD bipolar sntp_reply_ok(p8 address_to reply, p8 address_to request)
{
        if (memory_compare(reply + 24, request + 40, 8))
                return SNTP_NO_REPLY;
        //      Mode 4 is a server's answer; versions 1 to 4 are the ones
        //      there are, and 0, 5, 6 and 7 are not an NTP answer at all.
        if ((reply[0] & 0x7) != 4 || !((reply[0] >> 3) & 7) || ((reply[0] >> 3) & 7) > 4)
                return SNTP_BAD_SERVER;
        if (!reply[1])
        {
                p32 kiss = network_load_32(reply + 12);

                return kiss == SNTP_KISS_RATE ? SNTP_RATE_LIMITED
                       : kiss == SNTP_KISS_DENY || kiss == SNTP_KISS_RSTR
                           ? SNTP_DENIED
                           : SNTP_BAD_SERVER;
        }
        if ((reply[0] >> 6) == 3 || reply[1] >= 16)
                return SNTP_BAD_SERVER;
        if (!sntp_short_ok(network_load_32(reply + 4)) ||
            !sntp_short_ok(network_load_32(reply + 8)))
                return SNTP_BAD_SERVER;
        return SNTP_OK;
}

/*
        One reply made into a sample, or the reason it is not one: the
        checks above on the header, then the four stamps. t1 and t4 are the
        local departure and arrival, t4 as the kernel stamped it.
*/
static COLD bipolar sntp_reply_sample(p8 address_to reply,
                                      p8 address_to request, bipolar t1,
                                      bipolar t4, bipolar most,
                                      sntp_sample address_to into)
{
        bipolar verdict = sntp_reply_ok(reply, request);
        bipolar t2;
        bipolar t3;
        bipolar offset = 0;
        bipolar delay = 0;

        if_rare (verdict < 0)
                return verdict;
        if_rare (!sntp_local_ok(t4))
                return SNTP_MALFORMED;
        t2 = sntp_load_stamp(reply + 32);
        t3 = sntp_load_stamp(reply + 40);
        /*
                The reference stamp is when the server last set its own
                clock, so it sits at or before the stamp it transmits. A
                second of slack, because the two are read at different
                moments and a server whose reference is one tick the wrong
                side of transmit would otherwise be refused for ever: the
                sample loop stops on BAD_SERVER, so that server is not asked
                again. A reference of all zeros is a server that does not
                say: RFC 5905 reads it as never synchronised, and a server
                that keeps no such clock sends it at stratum 1 all the same.
                The echo of our own stamp is what says the reply is ours,
                so it is no reason to refuse one, and only one that states a
                time is held to the window.
        */
        if_rare ((network_load_32(reply + 16) | network_load_32(reply + 20)) &&
                 (!sntp_wall_ok(sntp_load_stamp(reply + 16)) ||
                  sntp_load_stamp(reply + 16) >
                      t3 + (bipolar)SNTP_NANOSECONDS))
                return SNTP_BAD_SERVER;
        sntp_offset_delay(t1, t2, t3, t4, address_of offset, address_of delay);
        if_rare (!sntp_sample_sane(t1, t2, t3, t4, offset, delay, most))
                return SNTP_MALFORMED;
        into->offset_ns = offset;
        into->delay_ns = delay;
        into->distance_ns = sntp_distance_ns(delay, network_load_32(reply + 4),
                                             network_load_32(reply + 8));
        into->ok = true;
        return SNTP_OK;
}


/*
        One request and its reply. With nts the request carries NTS's
        extension fields (nts_request), and a reply is only an answer when
        nts_reply authenticates it: anything else is noise, charged to the
        discard allowance like any other, except an NTSN that says the
        cookie is no good. An NTS reply is longer than 48 bytes by design and
        is parsed whole, so the tight tier's exact size is for plain replies;
        one that fills the room is cut and is no answer.
*/
static HOT bipolar sntp_exchange(b32 handle,
                                 network_deadline address_to deadline,
                                 bipolar most, p32 address_to sequence,
                                 nts_exchange address_to nts,
                                 sntp_sample address_to into)
{
        p8 request[NTS_PACKET_ROOM];
        p8 reply[NTS_PACKET_ROOM + 1];
        positive request_length = SNTP_PACKET;
        p64 sent[2];
        p64 got[2];
        p64 spare[2];
        p32 mine;
        bipolar t1;
        bipolar wait;
        bipolar received;
        bipolar verdict;
        positive discarded = 0;
        bool stamped;

        into->ok = false;
        memory_fill(request, 0, SNTP_PACKET);
        request[0] = SNTP_LI_VN_MODE;

        //      The packet is whole, extension fields and their tag included,
        //      before the clock is read for t1: the crypto is not in the
        //      offset (and t1 and t4 are the kernel's stamps regardless).
        if_rare (!sntp_put_stamp(request + 40))
                return SNTP_NO_REPLY;
        if (nts)
        {
                request_length = nts_request(nts, request, sizeof(request));
                if_rare (!request_length)
                        return SNTP_NO_REPLY;
        }
        if_rare (system_call_2(syscall(clock_gettime), CLOCK_REALTIME,
                               (positive)sent) < 0)
                return SNTP_NO_REPLY;
        if_rare (socket_send(handle, request, request_length, 0, 0, 0) < 0)
                return SNTP_NO_REPLY;
        mine = address_to sequence;
        address_to sequence = mine + 1;
        (void)sntp_transmit_stamp(handle, mine, sent);
        t1 = sntp_timespec_ns(sent[0], sent[1]);
        if_rare (!sntp_local_ok(t1))
                return SNTP_NO_REPLY;

        for (;;)
        {
                wait = network_wait_readable_until(handle, deadline);
                if_rare (wait <= 0)
                        return SNTP_NO_REPLY;
                received = sntp_receive_stamped(handle, reply,
                                                nts ? sizeof(reply)
                                                    : SNTP_REPLY_ROOM,
                                                got, address_of stamped);
                if_rare (!stamped &&
                         system_call_2(syscall(clock_gettime), CLOCK_REALTIME,
                                       (positive)got) < 0)
                        return SNTP_NO_REPLY;
                if_rare (received == -ECONNREFUSED || received == -EHOSTUNREACH ||
                         received == -ENETUNREACH)
                        //      ICMP says nobody is there; waiting out the
                        //      deadline would not make it so.
                        return SNTP_NO_SERVER;
                if_rare (received < 0)
                {
                        /*
                                Nothing was readable, so what woke the
                                wait was the error queue: a transmit
                                stamp that was not yet there when the
                                send drained for it. Take it off now --
                                t1 is already decided, so the stamp is
                                of no further use -- because a socket
                                with anything on that queue reports
                                POLLERR, and leaving it would wake this
                                wait again immediately, and again, until
                                the deadline ran out.
                        */
                        (void)sntp_transmit_stamp(handle, mine, spare);
                        if (discarded++ == NETWORK_DISCARD_MAX)
                                return SNTP_NO_REPLY;
                        continue;
                }
                if_rare (received < SNTP_PACKET ||
                         (nts ? received > NTS_PACKET_ROOM
                              : MOONWATER_STRICT_NET >= STRICT_TIGHT &&
                                    received != SNTP_PACKET))
                {
                        if (discarded++ == NETWORK_DISCARD_MAX)
                                return SNTP_NO_REPLY;
                        continue;
                }
                if (nts)
                {
                        bipolar heard = nts_reply(nts, reply, (positive)received);

                        if (heard == NTS_REPLY_NAK)
                                return SNTP_NTS_NAK;
                        if (heard != NTS_REPLY_OK)
                        {
                                if (discarded++ == NETWORK_DISCARD_MAX)
                                        return SNTP_NO_REPLY;
                                continue;
                        }
                }
                verdict = sntp_reply_sample(reply, request, t1,
                                            sntp_timespec_ns(got[0], got[1]),
                                            most, into);
                if_rare (verdict == SNTP_NO_REPLY)
                {
                        if (discarded++ == NETWORK_DISCARD_MAX)
                                return SNTP_NO_REPLY;
                        continue;
                }
                return verdict;
        }
}

static COLD bipolar sntp_query_at(p32 server, p16 port, bool filter,
                                  bipolar most, nts_exchange address_to nts,
                                  sntp_sample address_to answer)
{
        socket_address_internet where = {
            .family = AF_INET,
            .port = network_order_16(port),
            .host = network_order_32(server),
        };
        network_deadline deadline;
        sntp_sample row[SNTP_SAMPLES];
        positive want = filter ? SNTP_SAMPLES : 1;
        b32 want_stamp = 1;
        p32 want_transmit = SNTP_TIMESTAMPING_WANT;
        p32 sequence = 0;
        positive at;
        bipolar handle;
        bipolar best;
        bipolar failed = SNTP_NO_REPLY;

        handle = socket_new(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
        if (handle < 0)
                return SNTP_NO_SERVER;
        scope_exit(socket_close((b32)handle));
        if (socket_connect((b32)handle, address_of where, sizeof(where)) < 0)
                return SNTP_NO_SERVER;
        (void)socket_option_set((b32)handle, SOL_SOCKET, SNTP_TIMESTAMPNS,
                                address_of want_stamp, sizeof(want_stamp));
        (void)socket_option_set((b32)handle, SOL_SOCKET, SNTP_TIMESTAMPING,
                                address_of want_transmit,
                                sizeof(want_transmit));

        memory_zero(row, sizeof(row));
        for (at = 0; at < want; at++)
        {
                //      Each sample has its own wait. They used to share one
                //      of SNTP_SECONDS times the count, so a datagram lost
                //      first spent all of it and the samples after it were
                //      sent with no time to be answered in: one lost packet
                //      in four ended a query with no answer.
                if (!network_deadline_begin(address_of deadline, SNTP_SECONDS,
                                            0))
                        break;
                failed = sntp_exchange((b32)handle, address_of deadline, most,
                                       address_of sequence, nts, row + at);
                //      A server that is not there, or has said no, is not
                //      asked again for the rest of the samples; nor is one
                //      that refused the cookie, which needs new keys first.
                if (failed == SNTP_NO_SERVER || failed == SNTP_BAD_SERVER ||
                    failed == SNTP_RATE_LIMITED || failed == SNTP_DENIED ||
                    failed == SNTP_NTS_NAK)
                        break;
        }

        best = sntp_pick(row, want);
        if (best < 0)
                return failed < 0 ? failed : SNTP_NO_REPLY;
        address_to answer = row[best];
        answer->address = server;
        return SNTP_OK;
}

/*
        A time server asked apart. The machine's NTP child asks as root, and
        what a server sends is read by sntp_reply_sample and, with NTS, by
        nts_reply, so the name's lookup, the exchanges and both readers run
        in a confined child (net_apart, with a time query's filter: recvmsg
        and the two timestamp options besides a fetch's calls). It answers
        with its status, the sample it picked and, with NTS, the cookies the
        replies left. The status is believed as one sntp_query_at returns;
        the sample as one sntp_reply_sample could have made (taken, a delay
        within SNTP_DELAY_MOST_NS, a distance not below half of it, an
        offset inside the NTP window, and inside most when the clock is
        synchronised, from an address); the cookies as nts_ke_believed holds
        them. The keys never come back: the caller still has its own.
*/
_Static_assert(SNTP_TIMESTAMPNS == NET_APART_SO_TIMESTAMPNS &&
                   SNTP_TIMESTAMPING == NET_APART_SO_TIMESTAMPING,
               "the time query's filter allows the options it sets");

typedef struct
{
        b64 status;
        sntp_sample sample;
        p64 count;
        p16 length[NTS_COOKIES];
        p8 cookie[NTS_COOKIES][NTS_COOKIE_MAX];
} sntp_apart_said;

typedef struct
{
        string_address name;
        p16 port;
        bool filter;
        bipolar most;
        nts_exchange address_to nts;
} sntp_apart_ask;

static COLD bool sntp_apart_task(address_any context, bipolar answer)
{
        sntp_apart_ask address_to ask = context;
        sntp_apart_said said;
        //      The name or the address as the HTTP client takes it, which is
        //      the one place a host is looked up.
        p32 host = ask->name && ask->name[0] ? http_lookup(ask->name) : 0;
        bool wrote;

        memory_zero(address_of said, sizeof said);
        said.status = host ? sntp_query_at(host, ask->port, ask->filter, ask->most,
                                           ask->nts, address_of said.sample)
                           : SNTP_NO_SERVER;
        if (ask->nts)
        {
                said.count = ask->nts->state->count;
                memory_copy(said.length, ask->nts->state->length,
                            sizeof said.length);
                memory_copy(said.cookie, ask->nts->state->cookie,
                            sizeof said.cookie);
        }
        wrote = system_write_all((positive)answer, address_of said, sizeof said) ==
                sizeof said;
        crypto_forget(address_of said, sizeof said);
        return wrote;
}

static COLD bool sntp_apart_believed(const sntp_apart_said address_to said,
                                     bipolar most, bool nts)
{
        const sntp_sample address_to sample = address_of said->sample;

        if (said->status != SNTP_OK &&
            (said->status > SNTP_ANSWER - 1 || said->status < SNTP_CONTRADICTS))
                return false;
        if (said->status == SNTP_OK &&
            (!sample->ok || !sample->address || sample->delay_ns < 0 ||
             sample->delay_ns > SNTP_DELAY_MOST_NS ||
             sample->distance_ns < sample->delay_ns / 2 ||
             !sntp_within(sample->offset_ns, SNTP_WALL_MOST_NS) ||
             (most <= SNTP_OFFSET_SYNCED_NS &&
              !sntp_within(sample->offset_ns, most))))
                return false;
        if (!nts)
                return !said->count;
        if (said->count > NTS_COOKIES)
                return false;
        for (positive at = 0; at < said->count; at++)
                if (!said->length[at] || said->length[at] > NTS_COOKIE_MAX)
                        return false;
        return true;
}

static COLD bipolar sntp_query_apart(string_address name, p16 port, bool filter,
                                     bipolar most, nts_exchange address_to nts,
                                     sntp_sample address_to answer)
{
        sntp_apart_ask ask = {name, port, filter, most, nts};
        byte_store got = {0};
        sntp_apart_said said;
        bipolar ran = net_apart(sntp_apart_task, address_of ask, -1, address_of got,
                                sizeof said, true);
        bool whole = ran == NET_APART_OK && got.used == sizeof said;
        bipolar status = ran == NET_APART_UNCONFINED ? SNTP_CONFINE : SNTP_APART;

        if (whole)
                memory_copy(address_of said, got.bytes, sizeof said);
        if (got.bytes)
                crypto_forget(got.bytes, got.used);
        byte_store_release(address_of got);
        if (whole && sntp_apart_believed(address_of said, most, nts != null))
        {
                status = (bipolar)said.status;
                if (status == SNTP_OK)
                        address_to answer = said.sample;
                if (nts)
                {
                        nts->state->count = (positive)said.count;
                        memory_copy(nts->state->length, said.length,
                                    sizeof said.length);
                        memory_copy(nts->state->cookie, said.cookie,
                                    sizeof said.cookie);
                }
        }
        crypto_forget(address_of said, sizeof said);
        return status;
}

#endif


#define LOCALE_ZONE_MODE_PATH "/root/timezone.mode"
#define LOCALE_ZONE_NETWORK_PATH HOST_STATE "/timezone.network"
#define LOCALE_NTP_PATH "/root/ntp"
#define LOCALE_NTP_SERVER_PATH "/root/ntp.server"
#define LOCALE_NTP_SAMPLING_PATH "/root/ntp.sampling"
#define LOCALE_NTS_PATH "/root/ntp.nts"
#define LOCALE_NTS_SERVERS_PATH "/root/ntp.nts.servers"
#define LOCALE_NTS_STATE_PATH HOST_STATE "/nts"
#define LOCALE_NTS_FAILED_PATH HOST_STATE "/nts.failed"
#define LOCALE_NTP_LAST_PATH HOST_STATE "/ntp.last"
#define LOCALE_NTP_LAST_ROOM 96
#define LOCALE_KEYBOARD_PATH "/root/keyboard"
#define LOCALE_CANVAS_MODES_PATH "/root/canvas.modes"
#define LOCALE_NTP_DEFAULT_SERVER "pool.ntp.org"
#define LOCALE_NTP_SERVER_ROOM 80
#define LOCALE_NTP_RETRY_LEAST 1
#define LOCALE_NTP_RETRY_MOST 900
#define LOCALE_NTP_AGAIN 1800
#define LOCALE_NTP_AGAIN_FIRST 256
#define LOCALE_NTP_LEARN_LEAST_NS ((bipolar)60 * 1000000000)
#define LOCALE_NTP_FREQ_MOST ((bipolar)500 << 16)
#define LOCALE_NTP_NOISE_NS ((bipolar)500000)
#define LOCALE_NTP_RATE_AGAIN 300
#define LOCALE_NTP_EXIT_RATE 2
#define LOCALE_NTP_EXIT_LONG 3
#define LOCALE_LOCK_PATH HOST_STATE "/locale.lock"
#define LOCALE_NTP_LOCK_PATH HOST_STATE "/ntp.lock"
#define LOCALE_NTP_STEP_NS ((bipolar)128 * 1000000)
#define LOCALE_NTP_STEP_FIRST_NS ((bipolar)1000000)
#define LOCALE_NTP_TIMECONST 6
#define LOCALE_TIMEX_OFFSET 1
#define LOCALE_TIMEX_FREQUENCY 2
#define LOCALE_TIMEX_CONSTANT 6
/*
        The kernel's own numbering, from uapi/linux/timex.h. ADJ_SETOFFSET
        is 0x0100. It was 0x80 here, which is ADJ_TAI: a request to set the
        TAI offset from the constant word, not to step the clock from the
        time words. The kernel read word 6, which is zero, wrote that as
        the system TAI offset, ignored the offset we had gone to such
        lengths to measure, and returned the clock state -- a number the
        caller reads as success. So every sample, every filter and every
        guard below fed a call that could not set the clock, said it
        had, and cleared STA_UNSYNC on the way out so the machine reported
        itself synchronised.
*/
#define ADJ_OFFSET 0x0001
#define ADJ_FREQUENCY 0x0002
#define ADJ_MAXERROR 0x0004
#define ADJ_ESTERROR 0x0008
#define ADJ_STATUS 0x0010
#define ADJ_TIMECONST 0x0020
#define ADJ_SETOFFSET 0x0100
#define ADJ_NANO 0x2000
#define STA_PLL 0x0001
#define STA_UNSYNC 0x0040
#define STA_FREQHOLD 0x0080
#define STA_NANO 0x2000

/*
        A query that runs in a child of the machine process, and how its
        answer gets back.

        It used to come back as the child's exit status, collected by a
        wait4 on its pid, while the machine loop reaped every child it had
        with wait4(-1): the status was gone before this side asked, and a
        kiss-o-death never slowed anything down. The answer is one byte on a
        pipe, which nothing else in the process reads; a child that dies
        without writing reads as end of file, which is a failure like any
        other. The byte is the last thing the child does, so it is a zombie
        a moment later, and it is waited for here, blocking: the wait that
        did not block found it still running 2,994 times in 3,000 and never
        came back, so a query that ended left its pid, and its pipe, for
        the rest of the boot.
*/
typedef struct
{
        bipolar pid;
        bipolar answer;
} locale_child;

#define LOCALE_CHILD_RUNNING (-1)
#define LOCALE_CHILD_IDLE (-2)

static bipolar locale_child_fork(locale_child address_to child)
{
        b32 ends[2];
        bipolar pid = system_child_fork(ends, O_CLOEXEC | O_NONBLOCK);

        if (pid < 0)
                return pid;
        if (!pid)
        {
                child->pid = 0;
                child->answer = ends[1];
                //      Nothing of the machine process stays open in it: a copy
                //      of a descriptor of its lives as long as the query does,
                //      and the machine's attachment on /dev/spark once lived
                //      with it.
                (void)descriptors_close_except(3, (p32)ends[1], descriptors_none);
                return 0;
        }
        child->pid = pid;
        child->answer = ends[0];
        return pid;
}

static DEAD_END fn locale_child_end(locale_child address_to child, p8 code)
{
        (void)system_write_once(child->answer, address_of code, 1);
        system_call_1(syscall(exit), code);
        unreachable_here();
}

//      A query nobody wants any more, ended and waited for.
static fn locale_child_stop(locale_child address_to child)
{
        if (child->pid <= 0)
                return;
        system_child_reap(child->pid, child->answer, true);
        child->pid = 0;
        child->answer = -1;
}

//      LOCALE_CHILD_IDLE with none running, LOCALE_CHILD_RUNNING while it
//      runs, then the byte it ended with -- once.
static bipolar locale_child_poll(locale_child address_to child)
{
        p8 code = 1;
        bipolar got;

        if (child->pid <= 0)
                return LOCALE_CHILD_IDLE;
        got = system_read_once(child->answer, address_of code, 1);
        if (got == -EAGAIN || got == -EINTR)
                return LOCALE_CHILD_RUNNING;
        //      The byte or the end of file says it is done; any other
        //      answer of read says nothing, and is not waited for.
        system_child_reap(child->pid, child->answer, got < 0);
        child->pid = 0;
        child->answer = -1;
        return got == 1 ? code : 1;
}

//      A query forked to run task, whose answer is the byte it ends with: the
//      child never comes back from here. False when there was no child to
//      make, which the caller waits out as it does a query that failed.
typedef p8 (*locale_task)(address_any context);

static bool locale_child_run(locale_child address_to child, locale_task task,
                             address_any context)
{
        bipolar forked = locale_child_fork(child);

        if (!forked)
                locale_child_end(child, task(context));
        return forked > 0;
}

//      Seconds from now, in the boot clock's nanoseconds.
static p64 locale_after(p64 now, positive seconds)
{
        return now + (p64)seconds * SNTP_NANOSECONDS;
}

//      A wait doubled, to at most the longest it is allowed.
static positive locale_doubled(positive wait, positive most)
{
        return wait * 2 < most ? wait * 2 : most;
}

static p64 locale_ntp_next;
static positive locale_ntp_retry = LOCALE_NTP_RETRY_LEAST;
/*
        When the last query that set the clock ended, and how long until the
        next: the forked query reads the first, which the fork hands it, to
        tell how fast the clock ran. It is the end of the query the machine
        saw and not the start of the walk, which is up to a minute earlier
        and would put that into the frequency it learns.
*/
static p64 locale_ntp_synced;
static positive locale_ntp_every = LOCALE_NTP_AGAIN_FIRST;
static locale_child locale_ntp_child = {0, -1};

static fn locale_ntp_keep(bool online, bool synced, p64 now);
static bipolar locale_ntp_apply(bool by_hand);

//      What the last answer moved the clock by, and who gave it, for the
//      person who asked by hand. The scheduled queries run in a child and
//      nobody reads these there.
static bipolar locale_ntp_moved_ns;
static p8 locale_ntp_answered[LOCALE_NTP_SERVER_ROOM];
//      Why the last NTS server could not be asked, for the person who asked.
static p8 locale_nts_why[112];

/*
        What a setting says to a caller who cannot read it. /root is root's,
        and what a machine nobody has told anything answers -- ntp on, UTC,
        the layout us -- is a fact about that machine and not about a caller
        who could not look: it says so instead.
*/
#define LOCALE_NEEDS_ROOT "(needs root)"

static bool locale_readable(string_address path, p8 address_to into,
                            positive room)
{
        bipolar got = host_read_word(path, into, room);

        return got != -EACCES && got != -EPERM;
}

//      A switch as it is said: on with nothing set, and whatever else it
//      holds is off.
static string_address locale_switch_say(string_address path)
{
        p8 word[16];

        if (!locale_readable(path, word, sizeof(word)))
                return (string_address)LOCALE_NEEDS_ROOT;
        return !word[0] || string_equals(word, "on") ? (string_address) "on"
                                                     : (string_address) "off";
}

static bool locale_switch_on(string_address path)
{
        return !string_equals(locale_switch_say(path), "off");
}

static bool locale_ntp_wanted(void)
{
        return locale_switch_on(LOCALE_NTP_PATH);
}


static bool locale_clock_synced(void)
{
        return logger_clock_synced(null);
}

/*
        Whether a stored zone is a fixed offset, the way clock_zone_offset
        writes one. Its bracket has to name the offset its POSIX part gives:
        "<+0530>-5:30" does, and "<CET>-1<CEST>,M3.5.0,M10.5.0/3" starts with
        a bracket too but is a zone with daylight saving. The bracket is put
        back in the clock's spelling and written again, and what comes out
        has to be the zone whole.
*/
static bool locale_zone_fixed(string_address zone)
{
        p8 clock[8];
        p8 written[24];
        positive digits = 0;
        positive at = 0;

        if (zone[0] != '<' || (zone[1] != '+' && zone[1] != '-'))
                return false;
        while (digits < 5 && byte_is_digit((p8)zone[2 + digits]))
                digits++;
        if ((digits != 2 && digits != 4) || zone[2 + digits] != '>')
                return false;
        clock[at++] = (p8)zone[1];
        memory_copy(clock + at, zone + 2, digits == 4 ? 2 : digits);
        at += digits == 4 ? 2 : digits;
        if (digits == 4)
        {
                clock[at++] = ':';
                memory_copy(clock + at, zone + 4, 2);
                at += 2;
        }
        clock[at] = end;
        return clock_zone_offset((string_address)clock, written, sizeof(written)) &&
               string_equals((string_address)written, zone);
}

/*
        What a stored zone is called when it is shown. A country code or a
        zone name comes back as the table spells it; a fixed offset stored as
        "<+0530>-5:30" is shown as UTC+05:30, which is how it was asked for,
        rather than the POSIX spelling that exists to get the sign right. Any
        other zone is shown as it is typed, bracket and all.
*/
static fn locale_zone_describe(string_address zone, p8 address_to into,
                               positive room)
{
        string_address named = clock_zone_named(zone);
        positive at = 3;

        if (named)
        {
                string_copy_bounded(into, named, room);
                return;
        }
        if (!locale_zone_fixed(zone) || room < 16)
        {
                string_copy_bounded(into, zone[0] ? zone : (string_address) "UTC",
                                    room);
                return;
        }
        memory_copy(into, "UTC", 3);
        for (positive in = 1; zone[in] && zone[in] != '>' && at + 2 < room; in++)
        {
                if (in == 4)
                        into[at++] = ':';
                into[at++] = zone[in];
        }
        into[at] = end;
}

/*
        A zone this machine can keep: a name or country code from the table,
        an offset as a clock shows it, or a POSIX zone string the parser takes
        whole. The written form lands in into -- the table's spelling for a
        name, the POSIX form for an offset -- so what is stored means the same
        to every program that reads it, not only to this one.
*/
static bool locale_zone_resolve(string_address name, p8 address_to into,
                                positive room)
{
        string_address named;
        bool parsed;

        if (!name || !name[0])
                return false;
        named = clock_zone_named(name);
        if (named)
        {
                string_copy_bounded(into, named, room);
                return true;
        }
        if (clock_zone_offset(name, into, room))
                return true;

        //      Read whole: what the grammar leaves over is kept as typed in
        //      the file every bowl copies. clock_tz_whole leaves the
        //      process's zone set to what it read, so the one in force is put
        //      back whichever way it goes.
        parsed = string_length(name) < room && clock_tz_whole(name);
        tzset();
        if (!parsed)
                return false;
        string_copy_bounded(into, name, room);
        return true;
}

/*
        Every name the table holds, as the list a person greps: the country
        codes wrapped on a few lines, then one zone a line with what it is
        called. Any tzdata link also works ("Asia/Calcutta", "US/Eastern"),
        and is stored as the zone it points to.
*/
static b32 locale_zone_list(void)
{
        bool code = false;
        positive column = 0;
        string_address name;
        string_address spoken;

        string_format(log, "  codes");
        column = 7;
        for (positive at = 0; (name = clock_zone_known(at, address_of code));
             at++)
        {
                if (code)
                {
                        if (column + 3 > 78)
                        {
                                string_format(log, "\n       ");
                                column = 7;
                        }
                        string_format(log, " %s", name);
                        column += 3;
                        continue;
                }
                if (column)
                {
                        string_format(log, "\n  zones  " TERM_DIM
                                           "tzdata " CLOCK_ZONE_TZDATA
                                           "; any tzdata link works too"
                                           TERM_RESET "\n");
                        column = 0;
                }
                spoken = clock_zone_spoken(name);
                string_format(log, "    %s  " TERM_DIM "%s" TERM_RESET "\n",
                              name, spoken ? spoken : (string_address) "");
        }
        string_format(log, "  offsets  +1  -5  +5:30  UTC+2"
                           TERM_DIM "   +1 is an hour ahead of UTC,"
                           " with no daylight saving" TERM_RESET "\n");
        host_say(log, "  auto     " TERM_DIM "the zone Cloudflare places "
                      "this network in, asked once per network"
                      TERM_RESET "\n");
        return 0;
}

/*
        A zone as a person reads it: the name it is stored under, and what it
        is called -- "Europe/Stockholm, Central European Time". An offset has
        no name in words, so it says what it is instead.
*/
static fn locale_zone_title(string_address zone, p8 address_to into,
                            positive room)
{
        p8 shown[80];
        string_address spoken = clock_zone_spoken(zone[0] ? zone
                                                          : (string_address) "UTC");

        locale_zone_describe(zone, shown, sizeof(shown));
        string_copy_bounded(into, shown, room);
        if (spoken && !string_equals(spoken, shown))
        {
                string_append_bounded(into, ", ", room);
                string_append_bounded(into, spoken, room);
        }
        else if (locale_zone_fixed(zone))
                string_append_bounded(into,
                                      ", a fixed offset with no daylight saving",
                                      room);
}

//      The zone in force is the one the machine keeps: a TZ in the caller's
//      environment (sudo's env_keep, ssh's AcceptEnv) is the caller's own, and
//      read here it set the before and after of a change, and the offset the
//      kernel is told, to a zone nobody chose for the machine.
static fn locale_zone_own(void)
{
        (void)unsetenv((string_address) "TZ");
}

//      The wall clock now, in the zone in force, with its abbreviation.
static fn locale_zone_moment(p8 address_to into, positive room)
{
        time_t stamp = time(null);
        tm broken;

        tzset();
        if (!localtime_r(address_of stamp, address_of broken) ||
            !strftime(into, room, "%Y-%m-%d %H:%M:%S %Z", address_of broken))
                string_copy_bounded(into, "(the clock could not be read)", room);
}

/*
        How the zone came to be what it is, as /root/timezone.mode says:
        "manual" after moonwater timezone ZONE, "auto" and where the answer
        came from once the network has been asked.

        With no mode file the zone file decides. Before auto existed the only
        way to get a /root/timezone was a person typing one, so a machine
        carrying one from then is manual -- auto would otherwise overwrite
        the zone they chose with wherever Cloudflare places them -- and a
        machine with neither has never been told anything, which is auto.
*/
static fn locale_zone_mode(p8 address_to into, positive room)
{
        p8 zone[80];

        host_read_word(LOCALE_ZONE_MODE_PATH, into, room);
        if (into[0])
                return;
        host_read_word(CLOCK_ZONE_PATH, zone, sizeof(zone));
        if (zone[0])
                string_copy_bounded(into, "manual", room);
}

static bool locale_zone_manual(void)
{
        p8 mode[48];

        locale_zone_mode(mode, sizeof(mode));
        return string_has_prefix(mode, "manual");
}

static fn locale_zone_how(p8 address_to into, positive room)
{
        p8 mode[48];

        locale_zone_mode(mode, sizeof(mode));
        if (string_has_prefix(mode, "manual"))
                string_copy_bounded(into, "(manual)", room);
        else if (string_has_prefix(mode, "auto cloudflare"))
                string_copy_bounded(into, "(auto, from Cloudflare)", room);
        else if (string_has_prefix(mode, "auto country ") && mode[13])
        {
                string_copy_bounded(into, "(auto, from country ", room);
                string_append_bounded(into, mode + 13, room);
                string_append_bounded(into, ")", room);
        }
        else
                string_copy_bounded(into, "(auto, waiting for the network)",
                                    room);
}

//      The zone as it is shown and how it came to be, or false when /root is
//      not the caller's to read.
static bool locale_zone_read(p8 address_to title, positive title_room,
                             p8 address_to how, positive how_room)
{
        p8 zone[80];

        if (!locale_readable(CLOCK_ZONE_PATH, zone, sizeof(zone)))
                return false;
        locale_zone_title(zone, title, title_room);
        locale_zone_how(how, how_room);
        return true;
}


static b32 locale_zone_status(void)
{
        p8 title[128];
        p8 how[48];
        p8 when[48];

        if (!locale_zone_read(title, sizeof(title), how, sizeof(how)))
        {
                host_say(log, host_label "timezone " LOCALE_NEEDS_ROOT "\n");
                return 0;
        }
        locale_zone_moment(when, sizeof(when));
        //      The zone's title and how it came to be, on the line a verb opens with.
        //      Both are read from /root (the zone from /root/timezone, the country of
        //      an auto mode from /root/timezone.mode) and are a disk's to choose, so
        //      they are spelled through writer_terminal_name: a planted newline, tab,
        //      control or high byte reaches the terminal as \xNN, not as the line, the
        //      column or the bidi override it names. A legitimate zone is ASCII and is
        //      shown unchanged.
        string_format(log, host_label "timezone %w " TERM_DIM "%w" TERM_RESET "\n", writer_terminal_name, (string_address)(title), writer_terminal_name, (string_address)(how));
        host_say(log, "  local %s\n", when);
        return 0;
}

/*
        The kernel's own idea of the zone, sys_tz.

        Almost nothing reads it -- programs here read /root/timezone and
        bowls read their /etc/localtime -- but FAT does: every timestamp on
        the EFI partition and on a USB stick is written in local time using
        it, so left at zero a file saved at noon in Stockholm shows as ten in
        the morning on the Mac or Windows machine it is carried to. It is a
        plain offset, so it is re-applied whenever the offset changes, which
        includes the two nights a year daylight saving does.

        The trap is the first call. The first settimeofday that carries a
        zone and no time makes Linux decide the hardware clock keeps local
        time and move the system clock by the offset -- an hour into the
        future for Sweden, the moment the zone is set. Passing a zero zone
        first spends that first call on nothing, because the kernel only
        warps when the offset is not zero. hwclock does the same.
*/
#define LOCALE_ZONE_NOT_APPLIED ((bipolar)1 << 40)

static bipolar locale_zone_kernel_east = LOCALE_ZONE_NOT_APPLIED;
static bool locale_zone_kernel_first_spent;

static bipolar locale_zone_east_now(void)
{
        time_t stamp = time(null);
        tm broken;

        if (!localtime_r(address_of stamp, address_of broken))
                return 0;
        return (bipolar)broken.tm_gmtoff;
}

static bipolar locale_zone_kernel(void)
{
        b32 zone[2] = {0, 0};
        bipolar east = locale_zone_east_now();
        bipolar failed;

        if (east == locale_zone_kernel_east)
                return 0;
        if (!locale_zone_kernel_first_spent)
        {
                failed = system_call_2(syscall(settimeofday), 0,
                                       (positive)zone);
                if (failed < 0)
                        return failed;
                locale_zone_kernel_first_spent = true;
        }
        zone[0] = (b32)(-east / 60);
        failed = system_call_2(syscall(settimeofday), 0, (positive)zone);
        if (failed < 0)
                return failed;
        locale_zone_kernel_east = east;
        return 0;
}

//      The zone, everywhere that does not read /root/timezone for itself:
//      the kernel, and every bowl's /etc/localtime. Says where it went.
static fn locale_zone_list_add(p8 address_to into, positive room,
                                string_address what)
{
        if (into[0])
                string_append_bounded(into, ", ", room);
        string_append_bounded(into, what, room);
}

static fn locale_zone_apply(void)
{
        positive refused = 0;
        bipolar kernel = locale_zone_kernel();
        b32 host = bowl_write_localtime_host();
        positive bowls = bowl_write_localtime_all(address_of refused);
        p8 took[96] = {0};
        p8 kept[128] = {0};
        p8 count[32];
        positive at;

        if (kernel >= 0)
                locale_zone_list_add(took, sizeof(took), "the kernel");
        else
        {
                locale_zone_list_add(kept, sizeof(kept), "the kernel (");
                string_append_bounded(kept, file_reason(kernel), sizeof(kept));
                string_append_bounded(kept, ")", sizeof(kept));
        }
        locale_zone_list_add(host ? kept : took, host ? sizeof(kept) : sizeof(took),
                             "this system");
        for (positive side = 0; side < 2; side++)
        {
                positive n = side ? refused : bowls;

                if (!n)
                        continue;
                at = positive_into(count, n);
                string_copy_bounded(count + at, n == 1 ? " bowl" : " bowls",
                                    sizeof(count) - at);
                locale_zone_list_add(side ? kept : took,
                                     side ? sizeof(kept) : sizeof(took), count);
        }
        if (took[0])
                string_format(log, "  applied to  %s\n", took);
        if (kept[0])
                string_format(log, "  refused by  %s\n", kept);
}

/*
        The zone and how it came to be, as one change. Under a lock, so that
        the machine's own query -- which looks at the mode and writes the
        zone it was told, and meant to leave a zone somebody typed alone --
        cannot do both between a hand's two writes and leave Cloudflare's
        zone with the word manual beside it; and in the order that leaves
        the least when it is cut short: a zone somebody chose is marked
        manual first, so that a crash after that leaves the old zone with
        the word that keeps it, and one the network chose is written first,
        so that nobody reads the new word beside the old zone. A write made
        by the machine says -EBUSY of a zone that has gone manual since it
        asked.
*/
static bipolar locale_zone_write(string_address zone, string_address mode,
                                 bool by_hand)
{
        bipolar lock = host_lock(LOCALE_LOCK_PATH, true);
        bool manual = string_has_prefix(mode, "manual");
        bipolar failed = 0;

        if (lock < 0)
                return lock;
        if (!by_hand && locale_zone_manual())
                failed = -EBUSY;
        else
        {
                if (manual)
                        failed = radio_write_word(LOCALE_ZONE_MODE_PATH, mode);
                //      No zone is the mode alone: auto asked of a zone that
                //      was typed keeps the zone until the network names one.
                if (failed >= 0 && zone)
                        failed = radio_write_word(CLOCK_ZONE_PATH, zone);
                if (failed >= 0 && !manual)
                        failed = radio_write_word(LOCALE_ZONE_MODE_PATH, mode);
        }
        radio_unlock(lock);
        return failed;
}

//      Setting prints what the clock now reads, so a wrong zone -- an offset
//      with the sign the other way round, a country in the wrong half of a
//      continent -- is visible at the moment it is set, not at the next
//      meeting.
static b32 locale_zone_store(string_address zone, string_address mode)
{
        p8 was[80];
        p8 before[48];
        p8 after[48];
        p8 title[128];
        p8 how[48];
        p8 old_shown[80];
        bipolar failed;

        host_read_word(CLOCK_ZONE_PATH, was, sizeof(was));
        locale_zone_describe(was, old_shown, sizeof(old_shown));
        locale_zone_moment(before, sizeof(before));

        failed = locale_zone_write(zone, mode, true);
        return_if(failed < 0, host_fail("timezone", failed));

        locale_zone_moment(after, sizeof(after));
        locale_zone_title(zone, title, sizeof(title));
        locale_zone_how(how, sizeof(how));
        string_format(log, host_label "timezone %w " TERM_DIM "%w" TERM_RESET "\n", writer_terminal_name, (string_address)(title), writer_terminal_name, (string_address)(how));
        string_format(log, "  before  %s  " TERM_DIM "%s" TERM_RESET "\n",
                      before, old_shown);
        string_format(log, "  after   %s\n", after);
        locale_zone_apply();
        log_flush();
        return 0;
}

static b32 locale_zone_set(string_address name)
{
        p8 zone[80];

        if (!radio_text_plain(name, string_length(name)) ||
            !locale_zone_resolve(name, zone, sizeof(zone)))
                return host_refuse("unknown timezone %s -- "
                                   "moonwater timezone list shows the names\n",
                                   name);
        return locale_zone_store(zone, "manual");
}

/*
        Auto: the zone the network says this machine is in.

        Cloudflare geolocates every request it answers, and its speed test
        says what it found in a response header: speed.cloudflare.com's
        /__down?bytes=0 comes back empty with "timezone: Europe/Stockholm"
        and "country: SE". That endpoint is the speed test's own and nobody
        promised it, so there are two ways down from it: the country it gave,
        read as the zone most of that country's people live in, and failing
        that /cdn-cgi/trace, which every Cloudflare host serves and which
        says "loc=SE". A name the table does not hold is never stored -- it
        falls to the country, and with nothing at all the zone stays what it
        was.

        One request, over TLS, is sent when a network appears: at boot once
        there is a default route, and again when the route's interface,
        gateway or gateway's hardware address changes -- a new lease, another
        wifi network. Not more often than every three minutes, backing off to
        an hour while it fails, and never in manual mode. The request carries
        nothing but the connection itself, and Cloudflare sees that address
        either way; moonwater timezone ZONE turns it off.

        The ask runs in a child of the machine process, like the NTP query,
        after the clock has been set if it is going to be, since a clock at
        the epoch fails every certificate. What the answer was asked for is
        kept in /run/moonwater/timezone.network so a hand-run
        moonwater timezone auto and the machine do not ask twice.
*/
#define LOCALE_AUTO_URL "https://speed.cloudflare.com/__down?bytes=0"
#define LOCALE_AUTO_TRACE_URL "https://cloudflare.com/cdn-cgi/trace"
#define LOCALE_AUTO_LEAST 180
#define LOCALE_AUTO_MOST 3600
#define LOCALE_AUTO_CLOCK_WAIT 60
#define LOCALE_NETWORK_ROOM 96
#define LOCALE_ROUTE_GATEWAY 0x2

typedef struct
{
        p8 zone[64];
        p8 mode[32];
} locale_auto_answer;

/*
        Which network this is: the default route's interface and gateway,
        and the gateway's hardware address when the neighbour table has it.
        Empty with no default route. Two networks that both hand out
        192.168.1.1 differ in the last part, which is why it is there.
        Whether there is any route at all is the answer: a machine with an
        address and no gateway is on a network, though nothing that lives
        past it can be asked.
*/
static bool locale_network(p8 address_to into, positive room)
{
        bool any = false;
        p8 table[4096];
        p8 gateway[16] = {0};
        p8 device[20] = {0};
        bipolar got;
        string_address line;

        into[0] = end;
        got = host_read_kernel("/proc/net/route", table, sizeof(table));
        if (got <= 0)
                return false;
        for (line = (string_address)table; line && line[0];)
        {
                string_address next = string_first_of(line, '\n');
                p8 word[5][24];
                positive words = 0;
                string_address at = line;

                if (next)
                        address_to(p8 address_to)next = end;
                while (words < 5 && at[0])
                {
                        positive length = 0;

                        at += string_span(at, string_set_blanks);
                        while (at[0] && at[0] != ' ' && at[0] != '\t' &&
                               length + 1 < sizeof(word[0]))
                                word[words][length++] = (p8)(at++)[0];
                        word[words][length] = end;
                        if (length)
                                words++;
                }
                any |= words >= 4 && !string_equals(word[0], "Iface");
                //      Iface Destination Gateway Flags: a default route
                //      through a gateway.
                if (words >= 4 && string_equals(word[1], "00000000") &&
                    ((positive)string_to_number_unsigned(word[3], null, 16) &
                     LOCALE_ROUTE_GATEWAY))
                {
                        string_copy_bounded(device, word[0], sizeof(device));
                        string_copy_bounded(gateway, word[2], sizeof(gateway));
                        break;
                }
                line = next ? next + 1 : null;
        }
        if (!device[0])
                return any;

        string_copy_bounded(into, device, room);
        string_append_bounded(into, " ", room);
        string_append_bounded(into, gateway, room);

        //      /proc/net/arp names the gateway dotted, the route in the
        //      kernel's own byte order as hex; read the one as the other.
        {
                p32 raw = (p32)string_to_number_unsigned(gateway, null, 16);
                p8 dotted[20];
                positive at = 1;

                dotted[0] = '\n';
                at += host_into(dotted + at, bytes_reverse_32(raw));
                dotted[at++] = ' ';
                dotted[at] = end;
                got = host_read_kernel("/proc/net/arp", table, sizeof(table));
                if (got > 0)
                {
                        //      IP address, HW type, Flags, HW address: the
                        //      first colon on the gateway's line is two
                        //      digits into its hardware address.
                        p8 address_to row = (p8 address_to)memory_search(
                            table, (positive)got, dotted, string_length(dotted));
                        p8 address_to stop = row ? (p8 address_to)memory_search(
                                                       row + 1,
                                                       (positive)(table + got - row - 1),
                                                       "\n", 1)
                                                 : null;
                        positive span = row ? (positive)((stop ? stop : table + got) - row)
                                            : 0;
                        p8 address_to colon = row ? (p8 address_to)memory_search(
                                                        row, span, ":", 1)
                                                  : null;

                        if (colon && colon + 15 <= row + span &&
                            memory_compare(colon - 2, "00:00:00:00:00:00", 17))
                        {
                                p8 mac[18];

                                memory_copy(mac, colon - 2, 17);
                                mac[17] = end;
                                string_append_bounded(into, " ", room);
                                string_append_bounded(into, mac, room);
                        }
                }
        }
        return true;
}

/*
        The same network: the same interface and gateway, and the same
        hardware address unless one side has not learned it yet -- the
        neighbour table fills in after the first packet, and that is not a
        new network.
*/
static positive locale_network_route(string_address text)
{
        positive at = 0;
        positive spaces = 0;

        while (text[at] && !(text[at] == ' ' && ++spaces == 2))
                at++;
        return at;
}

static bool locale_network_same(string_address now, string_address asked)
{
        positive a = locale_network_route(now);
        positive b = locale_network_route(asked);

        if (!now[0] || !asked[0] || a != b || memory_compare(now, asked, a))
                return false;
        if (!now[a] || !asked[b])
                return true;
        return string_equals(now + a, asked + b);
}

//      A header's value or a trace line's, if it is a plain word that fits.
static bool locale_auto_word(string_address value, positive length,
                             p8 address_to into, positive room)
{
        if (!value || !length || length >= room)
                return false;
        for (positive at = 0; at < length; at++)
        {
                p8 c = (p8)value[at];

                if (!byte_is_alnum(c) && c != '/' && c != '_' && c != '-' &&
                    c != '+')
                        return false;
        }
        memory_copy(into, value, length);
        into[length] = end;
        return true;
}

static bool locale_auto_country(string_address code,
                                locale_auto_answer address_to answer)
{
        string_address zone = clock_zone_country(code);

        if (!zone)
                return false;
        string_copy_bounded(answer->zone, zone, sizeof(answer->zone));
        string_copy_bounded(answer->mode, "auto country ", sizeof(answer->mode));
        string_append_bounded(answer->mode, code, sizeof(answer->mode));
        memory_to_upper_ascii(answer->mode + 13, string_length(answer->mode + 13));
        return true;
}

/*
        What an answer says, read the way everything from the network is:
        each word checked for shape, then required to be a name the table
        holds before it is believed. The speed test's headers first -- its
        zone, else its country -- and the trace's loc= line otherwise.
*/
static bool locale_auto_from_headers(p8 address_to head, positive length,
                                     locale_auto_answer address_to answer)
{
        p8 word[64];
        positive size = 0;
        string_address value;
        string_address named;

        value = http_header(head, length, (string_address) "timezone",
                            address_of size, null);
        if (locale_auto_word(value, size, word, sizeof(word)) &&
            string_first_of(word, '/') && (named = clock_zone_named(word)))
        {
                string_copy_bounded(answer->zone, named, sizeof(answer->zone));
                string_copy_bounded(answer->mode, "auto cloudflare",
                                    sizeof(answer->mode));
                return true;
        }
        value = http_header(head, length, (string_address) "country",
                            address_of size, null);
        return locale_auto_word(value, size, word, sizeof(word)) &&
               locale_auto_country(word, answer);
}

static bool locale_auto_from_trace(p8 address_to body, positive length,
                                   locale_auto_answer address_to answer)
{
        p8 word[8];
        positive at = 0;

        while (at + 4 <= length)
        {
                positive stop = at + memory_span_without_byte(body + at, '\n',
                                                              length - at);

                if (!memory_compare(body + at, "loc=", 4))
                {
                        positive size = stop - at - 4;

                        if (size && body[at + 4 + size - 1] == '\r')
                                size--;
                        return locale_auto_word((string_address)(body + at + 4),
                                                size, word, sizeof(word)) &&
                               locale_auto_country(word, answer);
                }
                at = stop + 1;
        }
        return false;
}

//      The whole question, and nothing stored: the zone and how it was
//      found, or the reason none was.
static bipolar locale_auto_ask(locale_auto_answer address_to answer)
{
        http_buffer head = {0};
        http_buffer body = {0};
        bipolar status;
        bool found;

        memory_zero(answer, sizeof(address_to answer));
        status = http_run((string_address)LOCALE_AUTO_URL,
                          address_of http_manners_zone, true, -1,
                          address_of body, null, null, address_of head);
        found = !status &&
                locale_auto_from_headers(head.bytes, head.used, answer);
        byte_store_release(address_of head);
        byte_store_release(address_of body);
        if (found)
                return 0;
        status = http_run((string_address)LOCALE_AUTO_TRACE_URL,
                          address_of http_manners_zone, true, -1,
                          address_of body, null, null, null);
        found = !status &&
                locale_auto_from_trace(body.bytes, body.used, answer);
        byte_store_release(address_of body);
        return status ? status : found ? 0 : HTTP_MALFORMED;
}

/*
        The question, apart. The machine asks it as root, so all of it -- the
        TLS, the HTTP and the reading of the headers and the trace -- runs in
        a confined child (net_apart), and what comes back is a status and two
        words. They are believed only as names the table holds: the zone as
        clock_zone_named spells it, the country through locale_auto_country
        again. What this process writes and logs is the table's spelling,
        never the child's bytes.
*/
typedef struct
{
        b64 status;
        locale_auto_answer answer;
} locale_auto_said;

static bool locale_auto_ask_task(address_any context, bipolar answer)
{
        locale_auto_said said;

        (void)context;
        memory_zero(address_of said, sizeof said);
        said.status = locale_auto_ask(address_of said.answer);
        return system_write_all((positive)answer, address_of said, sizeof said) ==
               sizeof said;
}

//      What the child said, as far as it can be believed: its status when
//      http_run could have returned it, and a zone only as the table spells
//      it -- a zone name, or a country code that names one.
static bipolar locale_auto_believe(locale_auto_said address_to said,
                                   locale_auto_answer address_to answer)
{
        string_address named;

        memory_zero(answer, sizeof(address_to answer));
        if (said->status)
                return http_apart_status(said->status) ? (bipolar)said->status
                                                       : HTTP_APART;
        said->answer.zone[sizeof(said->answer.zone) - 1] = end;
        said->answer.mode[sizeof(said->answer.mode) - 1] = end;
        if (string_equals(said->answer.mode, (string_address) "auto cloudflare"))
        {
                named = clock_zone_named(said->answer.zone);
                if (!named || !string_equals(named, said->answer.zone))
                        return HTTP_MALFORMED;
                string_copy_bounded(answer->zone, named, sizeof(answer->zone));
                string_copy_bounded(answer->mode, "auto cloudflare",
                                    sizeof(answer->mode));
                return 0;
        }
        if (string_length(said->answer.mode) == 15 &&
            !memory_compare(said->answer.mode, "auto country ", 13) &&
            locale_auto_country(said->answer.mode + 13, answer))
                return 0;
        memory_zero(answer, sizeof(address_to answer));
        return HTTP_MALFORMED;
}

static bipolar locale_auto_ask_apart(locale_auto_answer address_to answer)
{
        byte_store got = {0};
        locale_auto_said said;
        bipolar ran = net_apart(locale_auto_ask_task, null, -1, address_of got,
                                sizeof said, false);
        bool whole = ran == NET_APART_OK && got.used == sizeof said;

        memory_zero(answer, sizeof(address_to answer));
        if (whole)
                memory_copy(address_of said, got.bytes, sizeof said);
        byte_store_release(address_of got);
        if (!whole)
                return ran == NET_APART_UNCONFINED ? HTTP_CONFINE : HTTP_APART;
        return locale_auto_believe(address_of said, answer);
}

static string_address locale_auto_reason(bipolar status)
{
        switch (status)
        {
        case HTTP_NO_HOST:
                return (string_address) "the name did not resolve";
        case HTTP_NO_ROUTE:
                return (string_address) "no route to it";
        case HTTP_TLS:
        case HTTP_UNTRUSTED:
        case HTTP_EXPIRED:
        case HTTP_NOT_YET:
        case HTTP_MISMATCH:
        case HTTP_UNCHECKED:
                return (string_address) "TLS failed -- is the clock right?";
        case HTTP_REVOKED:
                return (string_address) "its certificate is revoked";
        case HTTP_UNLOGGED:
                return (string_address) "its certificate was not logged (CT)";
        case HTTP_OVERLAY:
                return (string_address) "what moonwater security published does not read";
        case HTTP_CLOCK:
                return (string_address) "the clock is unset";
        case HTTP_STATUS:
                return (string_address) "it answered with an error";
        case HTTP_MALFORMED:
                return (string_address) "the answer named no zone";
        case HTTP_CONFINE:
                return (string_address) "the question could not be confined here";
        case HTTP_APART:
                return (string_address) "the confined question died";
        }
        return (string_address) "no answer";
}

static fn locale_auto_asked(string_address network)
{
        if (!network || !network[0])
        {
                system_remove_at(AT_FDCWD, LOCALE_ZONE_NETWORK_PATH, 0);
                return;
        }
        host_state_ready();
        radio_write_word(LOCALE_ZONE_NETWORK_PATH, network);
}

/*
        The zone a network answer names, put where a zone goes. By hand it
        says what it did the way moonwater timezone ZONE does; in the
        machine's child it only writes, and the machine's own loop carries
        the new offset to the kernel on its next turn.
*/
static bipolar locale_auto_take(locale_auto_answer address_to answer,
                                bool loud)
{
        p8 was[80];
        p8 mode[48];

        host_read_word(CLOCK_ZONE_PATH, was, sizeof(was));
        host_read_word(LOCALE_ZONE_MODE_PATH, mode, sizeof(mode));
        if (loud)
                return locale_zone_store(answer->zone, answer->mode) ? -1 : 0;
        if (string_equals(was, answer->zone) &&
            string_equals(mode, answer->mode))
                return 0;
        if (locale_zone_write(answer->zone, answer->mode, false) < 0)
                return -1;
        tzset();
        (void)bowl_write_localtime_host();
        (void)bowl_write_localtime_all(null);
        {
                string_address line[] = {"timezone ", answer->zone, " (",
                                         answer->mode, ")", null};

                host_kmsg(line);
        }
        return 0;
}

//      moonwater timezone auto, and the auto half of time sync.
static b32 locale_zone_auto(void)
{
        locale_auto_answer answer;
        p8 network[LOCALE_NETWORK_ROOM];
        p8 zone[80];
        bipolar status;

        if (locale_zone_manual())
        {
                bipolar kept = locale_zone_write(null, "auto", true);

                return_if(kept < 0, host_fail("timezone", kept));
        }
        locale_network(network, sizeof(network));
        status = locale_auto_ask_apart(address_of answer);
        if (status)
        {
                locale_auto_asked(null);
                host_read_word(CLOCK_ZONE_PATH, zone, sizeof(zone));
                //      The kept zone is a disk's to choose (/root/timezone):
                //      spelled, so it cannot forge a line in this error.
                host_say(log_error, host_label "timezone auto: Cloudflare "
                                    "could not be asked (%s); keeping %w "
                                    "until the network answers\n",
                         locale_auto_reason(status), writer_terminal_name,
                         zone[0] ? (string_address)zone
                                 : (string_address) "UTC");
                return 1;
        }
        if (locale_auto_take(address_of answer, true) < 0)
                return 1;
        locale_auto_asked(network);
        return 0;
}

static p64 locale_auto_next;
static positive locale_auto_wait = LOCALE_AUTO_LEAST;
static locale_child locale_auto_child = {0, -1};

//      The question put to Cloudflare in the child, for the network it was
//      forked on: 0 when the zone was taken.
static p8 locale_auto_task(address_any network)
{
        locale_auto_answer answer;
        bool took = !locale_auto_ask_apart(address_of answer) &&
                    !locale_auto_take(address_of answer, false);

        if (took)
                locale_auto_asked((string_address)network);
        return took ? 0 : 1;
}

//      A question that was not put or not answered waits, longer each time.
static fn locale_auto_failed(p64 now)
{
        locale_auto_next = locale_after(now, locale_auto_wait);
        locale_auto_wait = locale_doubled(locale_auto_wait, LOCALE_AUTO_MOST);
}

static fn locale_auto_keep(bool ntp_wanted, bool synced, p64 now,
                           string_address network)
{
        bipolar ended = locale_child_poll(address_of locale_auto_child);
        p8 asked[LOCALE_NETWORK_ROOM];

        if (ended == LOCALE_CHILD_RUNNING)
                return;
        if (ended != LOCALE_CHILD_IDLE)
        {
                if (ended)
                        locale_auto_failed(now);
                else
                {
                        locale_auto_next = locale_after(now, LOCALE_AUTO_LEAST);
                        locale_auto_wait = LOCALE_AUTO_LEAST;
                }
                return;
        }
        //      With no network there is nothing to ask, and nothing to read
        //      a file for.
        if (!network[0])
                return;
        //      The network is looked at once in a while and not at every
        //      turn: what it was asked about and whether the zone was typed
        //      are two files read, and neither changes without a new network
        //      -- which clears this wait -- or a hand that has asked itself.
        if (locale_auto_next && now < locale_auto_next)
                return;
        host_read_word(LOCALE_ZONE_NETWORK_PATH, asked, sizeof(asked));
        if (locale_network_same(network, asked))
        {
                //      The gateway's address arrived after the answer did.
                if (string_length(network) > string_length(asked))
                        locale_auto_asked(network);
                locale_auto_next = locale_after(now, LOCALE_AUTO_LEAST);
                return;
        }
        //      A certificate is only as good as the clock that checks it:
        //      while NTP is on and has not set it, wait a minute for it.
        if (ntp_wanted && !synced &&
            now < locale_after(0, LOCALE_AUTO_CLOCK_WAIT))
                return;
        if (locale_zone_manual())
        {
                locale_auto_next = locale_after(now, LOCALE_AUTO_LEAST);
                return;
        }

        if (!locale_child_run(address_of locale_auto_child, locale_auto_task,
                              (address_any)network))
                locale_auto_failed(now);
}

/*
        moonwater time: the clock now, and by hand what otherwise happens on
        its own -- a network query, and the zone put back everywhere.
*/
static b32 locale_time_status(void)
{
        time_t stamp;
        tm broken;
        p8 when[40];

        locale_zone_status();
        stamp = time(null);
        gmtime_r(address_of stamp, address_of broken);
        strftime(when, sizeof(when), "%Y-%m-%d %H:%M:%S", address_of broken);
        string_format(log, "  utc   %s\n", when);
        if (stamp < 0 || (p64)stamp < clock_trust_floor())
                host_say(log, "  clock unset: it reads earlier than this "
                              "system's build or the last time it was sure, "
                              "so certificates cannot be checked;\n"
                              "  moonwater time sync sets it\n");
        host_say(log, "  ntp %s, %s\n", locale_switch_say(LOCALE_NTP_PATH),
                 locale_clock_synced() ? "synchronised" : "waiting");
        return 0;
}

static b32 locale_time_sync(void)
{
        bipolar failed;

        locale_ntp_moved_ns = 0;
        locale_ntp_answered[0] = end;
        failed = locale_ntp_apply(true);
        //      locale_ntp_answered is the server that answered, named in
        //      /root/ntp.server or /root/ntp.nts.servers, a disk's to choose:
        //      spelled through writer_terminal_name so it cannot forge a line.
        if (failed == SNTP_BELOW_FLOOR)
                string_format(log, host_label "time: %w answered earlier than "
                                   "this machine has already been (its build "
                                   "or the last time it was sure), so the "
                                   "clock was left alone; remove "
                                   "/root/clock.good if that is wrong\n",
                              writer_terminal_name, locale_ntp_answered);
        else if (failed == SNTP_CONTRADICTS)
                //      Before the generic answer below, which it would be
                //      given too: the take names the server before it refuses.
                string_format(log, host_label "time: %w answered, but it is "
                                   "further from the time NTS gave this boot "
                                   "than the clock can have drifted, so the "
                                   "clock was left alone\n",
                              writer_terminal_name, locale_ntp_answered);
        else if (failed < 0 && locale_ntp_answered[0])
                string_format(log, host_label "time: %w answered, but the "
                                   "clock could not be set: %s\n",
                              writer_terminal_name, locale_ntp_answered,
                              failed == SNTP_MALFORMED
                                  ? (string_address) "the time it gave is "
                                                     "outside the years this "
                                                     "machine accepts"
                                  : file_reason(failed));
        else if (failed == SNTP_UNCONFIRMED)
                string_format(log, host_label "time: no second server agreed "
                                   "with a step past 2 s, so the clock was "
                                   "left alone\n");
        else if (failed < 0)
        {
                string_format(log, host_label "time: no server answered%s\n",
                              failed == SNTP_RATE_LIMITED
                                  ? " -- asked too often, try in a minute"
                              : failed == SNTP_DENIED
                                  ? " -- a server told this machine to stay away"
                              : failed == SNTP_CONFINE
                                  ? " -- the query cannot be confined here, so "
                                    "nothing was asked"
                              : failed == SNTP_APART
                                  ? " -- the confined query died"
                                  : "");
                if (locale_nts_why[0])
                        string_format(log, "  NTS %s\n", locale_nts_why);
        }
        else
        {
                bipolar moved = locale_ntp_moved_ns;
                positive whole = (positive)(moved < 0 ? -moved : moved);
                p8 fraction[4];

                positive_into_padded(fraction, whole / 1000000 % 1000, 3, '0');
                fraction[3] = end;
                //      Under a millisecond reads as +0.000, not -0.000.
                string_format(log, host_label "time moved %s%p.%s s by %w\n",
                              moved < 0 && whole >= 1000000 ? "-" : "+",
                              whole / 1000000000,
                              fraction, writer_terminal_name, locale_ntp_answered);
        }
        locale_zone_kernel_east = LOCALE_ZONE_NOT_APPLIED;
        log_flush();
        //      In auto the zone is asked for again as well, which is the one
        //      Cloudflare request a person can make by hand; it prints its
        //      own before and after and applies what it found.
        if (!locale_zone_manual() && !locale_zone_auto())
                return failed < 0 ? 1 : 0;
        locale_zone_status();
        locale_zone_apply();
        log_flush();
        return failed < 0 ? 1 : 0;
}

/*
        The root distance, in the microseconds the kernel keeps its error
        bounds in: half the round trip and the server's own error. That is
        how far the offset can be from the truth, and it is what the kernel
        is told the clock's error now is.

        Telling it is not optional. The kernel adds 500 microseconds a second
        to its maximum error and marks the clock unsynchronised the moment
        that passes sixteen seconds -- and a clock nobody gave an error to
        sits at sixteen seconds already. Every correction here cleared
        STA_UNSYNC and never set ADJ_MAXERROR, so the next second put the
        flag back: moonwater ntp said waiting a second after any answer, and
        the machine, reading that as a failure, asked the pool again every
        few seconds for as long as it ran. With the error given, the flag
        stays down for the eight hours it takes 500 us/s to grow a
        millisecond into sixteen seconds, and the next query comes long
        before that.
*/
#define LOCALE_TIMEX_ESTERROR 4
#define LOCALE_NTP_ERROR_MOST_US 1000000

static CONST positive locale_ntp_error_us(bipolar distance_ns)
{
        bipolar us = (distance_ns < 0 ? 0 : distance_ns) / 1000 + 1;

        return (positive)(us > LOCALE_NTP_ERROR_MOST_US
                              ? LOCALE_NTP_ERROR_MOST_US
                              : us);
}

static fn locale_clock_mark_synced(positive error_us)
{
        positive words[LOGGER_TIMEX_WORDS] = {0};

        words[0] = ADJ_STATUS | ADJ_MAXERROR | ADJ_ESTERROR;
        words[LOGGER_TIMEX_STATUS] = STA_PLL | STA_FREQHOLD;
        words[LOGGER_TIMEX_MAXERROR] = error_us;
        words[LOCALE_TIMEX_ESTERROR] = error_us;
        system_call_1(syscall(adjtimex), (positive)words);
}

/*
        A correction applied once and then left alone is only right at
        the moment it lands. What carries the clock between polls is a
        crystal, and LOCALE_NTP_AGAIN is half an hour: ten parts per
        million, which is an ordinary one, is eighteen milliseconds of
        drift by the next query. That is three orders of magnitude past
        every other error on this path put together, and no amount of
        care measuring the offset touches any of it. Stepping and then
        free-running for 1800 seconds spends the whole measurement in
        the first instant and then throws it away.

        The kernel keeps a phase-locked loop for exactly this, and it
        keeps its frequency estimate across our polls -- which is what
        this program needs, because the query runs in a forked child
        that exits, so nothing held in memory survives to the next one.
        Handing the offset to that loop with ADJ_OFFSET and STA_PLL lets
        the kernel steer the clock's phase; how fast it runs is learned
        below instead (locale_ntp_learned), since the loop was measured
        learning it far too slowly, and polls start at four minutes and
        double to half an hour while that is being learned.

        A step is still right when the clock is far out. Slewing never
        moves time backwards, which is what a log, a build and a file
        timestamp all want, but the kernel slews at a bounded rate, so a
        large offset would take longer to walk off than the gap between
        polls. The split is at 128 ms, where ntpd puts it.

        A step also cancels any slew still in progress, with an
        ADJ_OFFSET of zero in the same request: the pending phase
        adjustment was computed against a clock this request is about to
        move, and applying both would correct twice.
*/
/*
        The first answer after boot is stepped to from a millisecond out,
        not slewed to: the clock then was set from a real-time clock that
        keeps whole seconds, so it starts some tens of milliseconds wrong,
        and the kernel slews those away at its own pace -- measured, 86 ms
        took nineteen minutes to come within one. Nothing has read the time
        yet to see it move, which is when ntpd -g and chrony's makestep
        step too. After that, only 128 ms is worth a step.
*/
static bool locale_ntp_first;

//      Whether a plain answer puts the clock further from the time NTS gave
//      (known seconds, given elapsed boot seconds ago) than a crystal can
//      have drifted since: 1000 ppm, which no real one reaches, and five
//      seconds for the whole-second stamps it was kept in.
static CONST bool locale_plain_apart(bipolar now, bipolar offset_ns, p64 known,
                                     p64 elapsed)
{
        bipolar apart;

        if (!known || now < 0)
                return false;
        apart = now + offset_ns - (bipolar)known * (bipolar)SNTP_NANOSECONDS;
        if (apart < 0)
                apart = -apart;
        return apart > ((bipolar)5 + (bipolar)(elapsed / 1000)) *
                           (bipolar)SNTP_NANOSECONDS;
}

static bool locale_ntp_wants_step(bipolar offset_ns)
{
        bipolar most = locale_ntp_first ? LOCALE_NTP_STEP_FIRST_NS
                                        : LOCALE_NTP_STEP_NS;

        return offset_ns >= most || offset_ns <= -most;
}

/*
        How fast the clock runs is learned here, at every poll, and not by
        the kernel's loop. The loop learned too slowly to matter: handed a
        guest whose tick ran 96 ppm fast, it had learned 31 of them after
        thirteen minutes, and a step -- which ADJ_SETOFFSET makes without
        touching the frequency at all -- taught it nothing, so a crystal
        fast enough to be stepped at every poll, 71 ppm at half an hour,
        was stepped at every poll for ever and ran up its whole drift
        between them: 144 ms, measured, every time.

        What a poll finds past the slew the kernel still has in hand ran up
        since the last poll that set the clock, and over that time is the
        rate the clock is off by. It is learned in the proportion it stands
        above the measurement's own noise, some hundreds of microseconds:
        a clock 20 ms out is learned almost whole at once, and 150 us is
        under a quarter learned, so a reading that is mostly noise moves
        the frequency little. STA_FREQHOLD keeps the kernel's loop to the
        phase, so the two do not both answer the same offset. Not learned:
        an elapsed shorter than a minute, and an offset past a thousand
        parts per million of it, which is a clock that was set wrong
        rather than one that ran fast. Against a panel of stratum-1
        servers, in time compressed four times, a guest whose tick ran 400
        ppm fast was held within 0.2 ms after its second poll, where
        without this it ran 168 ms out before every step; an ordinary
        clock was a median 136 us out against 604.
*/
static CONST bipolar locale_ntp_learned(bipolar offset_ns, bipolar elapsed_ns,
                                        bipolar frequency)
{
        bipolar milliseconds = elapsed_ns / 1000000;
        bipolar magnitude;
        bipolar learned;

        if (elapsed_ns < LOCALE_NTP_LEARN_LEAST_NS ||
            offset_ns > elapsed_ns / 1000 || offset_ns < -(elapsed_ns / 1000))
                return frequency;
        magnitude = offset_ns < 0 ? -offset_ns : offset_ns;
        /*
                Offset over elapsed, in the kernel's 2^-16 ppm, taken whole
                and remainder so neither product can pass 2^63; and the
                gain, whose product with the rate did pass it for an offset
                past 140 s -- 39 hours since the last poll that set the
                clock is enough -- and came out with the wrong sign. Past
                four seconds the gain is within 1e-4 of one and is left out.
        */
        learned = offset_ns / milliseconds * 65536 +
                  offset_ns % milliseconds * 65536 / milliseconds;
        if (magnitude < (bipolar)1 << 32)
                learned = learned * magnitude /
                          (magnitude + LOCALE_NTP_NOISE_NS);
        learned += frequency;
        if (learned > LOCALE_NTP_FREQ_MOST)
                return LOCALE_NTP_FREQ_MOST;
        if (learned < -LOCALE_NTP_FREQ_MOST)
                return -LOCALE_NTP_FREQ_MOST;
        return learned;
}

static fn locale_ntp_discipline_words(bipolar offset_ns, positive error_us,
                                      positive address_to words)
{
        //      The time of a step is whole seconds and the nanoseconds left
        //      after them, which are not negative: a second short is -1 and
        //      999999999 and not 0 and -1.
        bipolar seconds = clock_floor_divide(offset_ns, (bipolar)SNTP_NANOSECONDS);

        memory_zero(words, LOGGER_TIMEX_WORDS * sizeof(positive));
        words[LOGGER_TIMEX_STATUS] = STA_PLL | STA_FREQHOLD;
        words[LOGGER_TIMEX_MAXERROR] = error_us;
        words[LOCALE_TIMEX_ESTERROR] = error_us;
        if (locale_ntp_wants_step(offset_ns))
        {
                words[0] = ADJ_SETOFFSET | ADJ_OFFSET | ADJ_NANO | ADJ_STATUS |
                           ADJ_MAXERROR | ADJ_ESTERROR;
                words[LOCALE_TIMEX_OFFSET] = 0;
                words[LOGGER_TIMEX_TIME_SEC] = (positive)seconds;
                words[LOGGER_TIMEX_TIME_NSEC] =
                    (positive)(offset_ns - seconds * (bipolar)SNTP_NANOSECONDS);
                return;
        }
        words[0] = ADJ_OFFSET | ADJ_TIMECONST | ADJ_NANO | ADJ_STATUS |
                   ADJ_MAXERROR | ADJ_ESTERROR;
        words[LOCALE_TIMEX_OFFSET] = (positive)offset_ns;
        words[LOCALE_TIMEX_CONSTANT] = LOCALE_NTP_TIMECONST;
}


static const char locale_ntp_fallback[][24] = {
        LOCALE_NTP_DEFAULT_SERVER,
        "time.google.com",
        "time.cloudflare.com",
        "216.239.35.0",
        "216.239.35.4",
        "162.159.200.1",
        "162.159.200.123",
};

static bipolar locale_ntp_apply_offset(bipolar offset_ns, bipolar distance_ns)
{
        bipolar now;
        bipolar target = 0;
        positive words[LOGGER_TIMEX_WORDS];
        p64 stamp[2];
        bipolar failed;

        now = sntp_now_ns();
        if (now < 0)
                return now;
        if (!sntp_target_ok(now, offset_ns, address_of target))
                return SNTP_MALFORMED;
        if (target < (bipolar)clock_trust_floor() * (bipolar)SNTP_NANOSECONDS)
                return SNTP_BELOW_FLOOR;

        locale_ntp_first = !locale_ntp_synced;
        locale_ntp_discipline_words(offset_ns, locale_ntp_error_us(distance_ns),
                                    words);
        if (locale_ntp_synced)
        {
                positive state[LOGGER_TIMEX_WORDS] = {0};
                bipolar elapsed = (bipolar)(system_clock_ns(HOST_CLOCK_BOOTTIME) -
                                            locale_ntp_synced);
                bipolar frequency;

                if (system_call_1(syscall(adjtimex), (positive)state) >= 0)
                {
                        bipolar pending = (bipolar)state[LOCALE_TIMEX_OFFSET];

                        if (!(state[LOGGER_TIMEX_STATUS] & STA_NANO))
                                pending *= 1000;
                        frequency = locale_ntp_learned(
                            offset_ns - pending, elapsed,
                            (bipolar)state[LOCALE_TIMEX_FREQUENCY]);
                        if (frequency != (bipolar)state[LOCALE_TIMEX_FREQUENCY])
                        {
                                words[0] |= ADJ_FREQUENCY;
                                words[LOCALE_TIMEX_FREQUENCY] = (positive)frequency;
                        }
                }
        }
        failed = system_call_1(syscall(adjtimex), (positive)words);
        if_common (failed >= 0)
                return 0;

        now = sntp_now_ns();
        if (now < 0)
                return now;
        if (!sntp_target_ok(now, offset_ns, address_of target))
                return SNTP_MALFORMED;
        //      A time in the window is after 1970, so it needs no flooring.
        stamp[0] = (p64)(target / (bipolar)SNTP_NANOSECONDS);
        stamp[1] = (p64)(target % (bipolar)SNTP_NANOSECONDS);
        failed = system_call_2(syscall(clock_settime), CLOCK_REALTIME,
                               (positive)stamp);
        if (failed < 0)
        {
                stamp[1] = stamp[1] / 1000;
                failed = system_call_2(syscall(settimeofday), (positive)stamp, 0);
        }
        if (failed < 0)
                return failed;
        locale_clock_mark_synced(locale_ntp_error_us(distance_ns));
        return 0;
}

/*
        The last time this machine knew for certain, kept in /root/clock.good
        for the next boot's floor (clock_trust_floor, net.c).

        What may be written is what no forged answer can have moved. After an
        authenticated (NTS) answer, /run/moonwater/clock.auth holds the wall
        second it set and the boot second it was set at, and the time now is
        that wall second plus the boot seconds since: the machine's own
        uptime, which no later step of the wall clock changes. With no such
        answer this boot, the wall clock is not read at all: what is written
        is the floor the boot began with (/run/moonwater/clock.base, kept once
        a boot) plus the seconds the machine has run, which is a lower bound
        on the time whatever any answer said, so no forgery, plain or not, can
        put the floor ahead of the truth and lock the real time out; it lags
        the truth by the time the machine spent switched off. The file only
        ever goes forward, is written in place and never through a link, and
        is written after every authenticated answer, every six hours of
        uptime and when the machine stops.
*/
#define LOCALE_CLOCK_AUTH_PATH HOST_STATE "/clock.auth"
#define LOCALE_CLOCK_BASE_PATH HOST_STATE "/clock.base"
#define LOCALE_CLOCK_GOOD_EVERY ((p64)6 * 3600 * SNTP_NANOSECONDS)

static p64 locale_clock_next;

//      A decimal number of at most twelve digits at text + *at, moving *at
//      past it; false when there is none.
static bool locale_clock_number(string_address text, positive address_to at,
                                p64 address_to value)
{
        positive digits = 0;
        p64 number = string_digits_max(text + address_to at, 12, address_of digits);

        address_to at += digits;
        if (!digits)
                return false;
        address_to value = number;
        return true;
}

//      What the authenticated answer puts the time at now, or 0; with
//      elapsed, the boot seconds since it was given.
static p64 locale_clock_authenticated_now(p64 address_to elapsed)
{
        p8 text[48];
        positive at = 0;
        p64 wall;
        p64 booted;
        p64 now_boot = system_clock_ns(HOST_CLOCK_BOOTTIME) / SNTP_NANOSECONDS;

        host_read_word(LOCALE_CLOCK_AUTH_PATH, text, sizeof(text));
        if (!locale_clock_number((string_address)text, address_of at, address_of wall) ||
            text[at++] != ' ' ||
            !locale_clock_number((string_address)text, address_of at, address_of booted) ||
            text[at] || booted > now_boot)
                return 0;
        if (elapsed)
                address_to elapsed = now_boot - booted;
        return wall + (now_boot - booted);
}

//      An authenticated answer has set the clock: the wall second it set
//      and the boot second it was set at.
static fn locale_clock_authenticated(void)
{
        p8 text[48];
        time_t stamp = time(null);
        positive length;

        if (stamp < 0)
                return;
        length = positive_into(text, (positive)stamp);
        text[length++] = ' ';
        length += positive_into(text + length,
                                (positive)(system_clock_ns(HOST_CLOCK_BOOTTIME) /
                                           SNTP_NANOSECONDS));
        text[length++] = '\n';
        host_state_ready();
        (void)host_write_file(LOCALE_CLOCK_AUTH_PATH, text, length, 0644,
                              false);
}

//      The floor this boot started with, which /run/moonwater/clock.base
//      keeps so that every process can ask, or 0.
static p64 locale_clock_base(void)
{
        p8 text[24];
        positive at = 0;
        p64 value;

        host_read_word(LOCALE_CLOCK_BASE_PATH, text, sizeof(text));
        return locale_clock_number((string_address)text, address_of at, address_of value) &&
                       !text[at]
                   ? value : 0;
}

static fn locale_clock_base_keep(void)
{
        p8 text[24];
        positive length;

        if (locale_clock_base())
                return;
        host_state_ready();
        length = positive_into(text, (positive)clock_trust_floor());
        text[length++] = '\n';
        (void)host_write_file(LOCALE_CLOCK_BASE_PATH, text, length, 0644, false);
}

static bool locale_clock_persist(bool sync)
{
        p64 floor = clock_trust_floor();
        p64 value = locale_clock_authenticated_now(null);
        p8 text[24];
        positive length;

        if (!value)
        {
                p64 base = locale_clock_base();

                //      Nothing a forged answer can have moved: the floor the
                //      boot started with and the seconds the machine has run
                //      since.
                if (!base)
                        return false;
                value = base + system_clock_ns(HOST_CLOCK_BOOTTIME) /
                                   SNTP_NANOSECONDS;
        }
        if (value <= floor || value > CLOCK_GOOD_MOST)
                return false;
        length = positive_into(text, (positive)value);
        text[length++] = '\n';
        return host_write_file(CLOCK_GOOD_PATH, text, length, 0644, sync) >= 0;
}

//      Every six hours of uptime, from the machine loop; a minute later
//      when there was nothing yet to keep.
static fn locale_clock_keep(void)
{
        p64 now = system_clock_ns(HOST_CLOCK_BOOTTIME);

        if (now < locale_clock_next)
                return;
        locale_clock_next = now + (locale_clock_persist(true)
                                       ? LOCALE_CLOCK_GOOD_EVERY
                                       : (p64)60 * SNTP_NANOSECONDS);
}

//      When the machine stops, from the shell that stops it.
static fn host_clock_stop(void)
{
        (void)locale_clock_persist(true);
}

static bipolar locale_ntp_ask(string_address server, bool filter, bipolar most,
                              sntp_sample address_to answer)
{
        if (!server || !server[0] ||
            !radio_text_plain(server, string_length(server)))
                return SNTP_NO_SERVER;
        return sntp_query_apart(server, SNTP_PORT, filter, most, null, answer);
}

/*
        An unauthenticated answer does not get to undo an authenticated one.
        When NTS has set the clock this boot, what it said is known to within
        the drift of the crystal since (1000 ppm is a bound no real one
        reaches, plus five seconds), and a plain answer that puts the clock
        further from that is refused: it is a forgery, or a pool server that
        is wrong, and either way the authenticated time is the better one.
*/
static bool locale_plain_contradicts(bipolar offset_ns)
{
        p64 elapsed = 0;
        p64 known = locale_clock_authenticated_now(address_of elapsed);

        return locale_plain_apart(sntp_now_ns(), offset_ns, known, elapsed);
}

static bipolar locale_ntp_take(string_address server,
                               const sntp_sample address_to answer,
                               bool authenticated)
{
        bipolar applied;
        p8 last[LOCALE_NTP_LAST_ROOM];
        positive length;

        locale_ntp_moved_ns = answer->offset_ns;
        string_copy_bounded(locale_ntp_answered, server,
                            sizeof(locale_ntp_answered));
        if (authenticated)
                string_append_bounded(locale_ntp_answered, " (NTS)",
                                      sizeof(locale_ntp_answered));
        else if (locale_plain_contradicts(answer->offset_ns))
                return SNTP_CONTRADICTS;
        applied = locale_ntp_apply_offset(answer->offset_ns, answer->distance_ns);
        if (applied < 0)
                return applied;
        //      What set the clock, for the status page, and what it now
        //      knows for sure.
        string_copy_bounded(last, authenticated ? "nts " : "plain ", sizeof(last));
        string_append_bounded(last, server, sizeof(last));
        length = string_length(last);
        last[length++] = '\n';
        host_state_ready();
        (void)host_write_file(LOCALE_NTP_LAST_PATH, last, length, 0644, false);
        if (authenticated)
        {
                locale_clock_authenticated();
                (void)locale_clock_persist(true);
        }
        return applied;
}

/*
        A server answering RATE is telling us we ask too often. Walking to
        the next name and asking that one immediately is not an answer to
        it, and when the next name is another address of the same pool it
        is the complaint repeated. The verdict is carried out of the walk
        so a cycle that ended in nothing but rate limits waits properly
        instead of coming back in a second and doing it again.
*/
/*
        With sampling on and no server of the machine's own, the walk asks
        on after the first answer until SNTP_SERVERS have answered, and
        sntp_choose picks which to believe. Sampling off takes the first
        answer -- including one from /root/ntp.server -- unless it would
        step a clock that already has a plausible wall time more than two
        seconds, when a second server is asked for: sntp_choose believes no
        such step on one word. A clock nobody has set takes the first answer
        there is, with sampling or not: it has no useful time to compare it
        with, and waiting for the others is a minute on a machine whose
        network is slow to come.
*/
//      Whether the server at address is one of the first count of heard.
static bool locale_ntp_heard(const sntp_sample address_to heard, positive count,
                             p32 address)
{
        return array_any(at, count, heard[at].address == address);
}

/*
        Network Time Security, as the machine uses it.

        NTS is preferred: a machine asks for time from the key-establishment
        servers it knows (the defaults below, or the lines of
        /root/ntp.nts.servers, each a name or name:port) before it asks the
        pool, and an answer that authenticated is what sets an unset clock.
        The pool is the fallback, and only when no NTS server could be
        reached; its answers are marked plain (moonwater ntp says which set
        the clock last), are never written to the floor as known, and are
        refused when they put the clock further from the NTS time of this
        boot than a crystal can have drifted. A network that blocks TCP 4460
        costs one try of a few seconds, and then the pool is used without
        trying NTS again for two minutes (by hand, always). A server named in
        /root/ntp.server is the machine's own choice and is asked alone, in
        plain, as it always was, unless NTS is only: then nothing plain is
        asked, that server included.

        The keys and cookies live in /run/moonwater/nts (0600, in place, never
        through a link, read back as a plain file with every field checked: a
        torn or planted file is no state and the next query establishes
        afresh), since the scheduled query runs in a child of the machine
        process that is gone when it answers.
*/
#define LOCALE_NTS_MAGIC 0x3153544eu /* "NTS1" */
#define LOCALE_NTS_BACKOFF_NS ((p64)120 * SNTP_NANOSECONDS)
#define LOCALE_NTS_SERVERS_MOST 4
#define LOCALE_NTS_ENTRY_ROOM 80
#define LOCALE_NTS_SERVERS_ROOM 1024

static const char locale_nts_default[][24] = {
        "time.cloudflare.com",
        "nts.netnod.se",
        "ptbtime1.ptb.de",
};

static positive locale_nts_mode(void)
{
        p8 word[16];

        host_read_word(LOCALE_NTS_PATH, word, sizeof(word));
        return net_mode_of((string_address)word, nts_modes);
}

//      The mode as it is said, or that the caller cannot read it.
static string_address locale_nts_say(void)
{
        p8 word[16];
        positive mode;

        if (!locale_readable(LOCALE_NTS_PATH, word, sizeof(word)))
                return (string_address)LOCALE_NEEDS_ROOT;
        mode = net_mode_of((string_address)word, nts_modes);
        return mode == NTS_OFF   ? (string_address) "off"
               : mode == NTS_ONLY ? (string_address) "only"
                                  : (string_address) "on";
}

//      "name" or "name:port", as the key-establishment server it names.
static bool locale_nts_entry(string_address entry, p8 address_to host,
                             p16 address_to port)
{
        positive length = string_length(entry);
        positive colon = length;
        positive number = 0;

        for (positive at = 0; at < length; at++)
                if (entry[at] == ':')
                        colon = at;
        if (colon < length)
        {
                if (colon + 1 == length || length - colon > 6)
                        return false;
                positive used = 0;

                number = string_digits_max(entry + colon + 1, length - colon - 1, address_of used);
                if (used != length - colon - 1)
                        return false;
                if (!number || number > 65535)
                        return false;
        }
        if (!nts_name_ok((const p8 address_to)entry, colon))
                return false;
        memory_copy(host, entry, colon);
        host[colon] = end;
        address_to port = colon < length ? (p16)number : NTS_KE_PORT;
        return true;
}

//      The lines of the servers' file that are servers: each a valid entry,
//      of fewer than stride bytes once its trailing blanks are gone. A line
//      is never cut to fit, so one too long is no server rather than its
//      first stride bytes; and when the file was longer than the buffer the
//      last line, cut where the buffer ends, is not one either. At most
//      LOCALE_NTS_SERVERS_MOST are taken; returns the count.
static positive locale_nts_lines(const p8 address_to text, positive length,
                                 bool complete, p8 address_to into,
                                 positive stride)
{
        positive count = 0;
        positive at = 0;

        if (!complete)
        {
                while (length && text[length - 1] != '\n')
                        length--;
        }
        while (at < length && count < LOCALE_NTS_SERVERS_MOST)
        {
                positive stop = at;
                positive size;
                p8 line[LOCALE_NTS_ENTRY_ROOM];
                p8 host[NTS_SERVER_MAX];
                p16 port;

                while (stop < length && text[stop] != '\n')
                        stop++;
                size = stop - at;
                while (size && (text[at + size - 1] == '\r' || text[at + size - 1] == ' '))
                        size--;
                if (size < stride)
                {
                        memory_copy(line, text + at, size);
                        line[size] = end;
                        if (locale_nts_entry((string_address)line, host, address_of port))
                                string_copy_bounded(into + count++ * stride,
                                                    (string_address)line, stride);
                }
                at = stop + 1;
        }
        return count;
}

//      The settings' servers, else the defaults: the lines of the file that
//      are valid entries (locale_nts_lines), to LOCALE_NTS_SERVERS_MOST.
//      Returns the count.
static positive locale_nts_servers(p8 address_to into, positive stride)
{
        p8 text[LOCALE_NTS_SERVERS_ROOM];
        bipolar got = host_read_state(LOCALE_NTS_SERVERS_PATH, text, sizeof(text));
        positive count = 0;

        if (got > 0)
                count = locale_nts_lines(text, (positive)got,
                                         got < (bipolar)sizeof(text) - 1,
                                         into, stride);
        if (count)
                return count;
        for (count = 0; count < array_count(locale_nts_default); count++)
                string_copy_bounded(into + count * stride,
                                    (string_address)locale_nts_default[count],
                                    stride);
        return count;
}

static bool locale_nts_state_ok(const nts_state address_to state)
{
        return nts_state_ok(state) && state->origin_port &&
               nts_name_ok(state->origin, string_length((string_address)state->origin));
}

static bool locale_nts_load(nts_state address_to state)
{
        p8 bytes[4 + sizeof(nts_state) + 2];
        bipolar got = file_slurp_regular_at(AT_FDCWD,
                                            (string_address)LOCALE_NTS_STATE_PATH,
                                            bytes, sizeof(bytes), O_NOFOLLOW);
        bool good = got == (bipolar)(4 + sizeof(nts_state)) &&
                    network_load_32(bytes) == LOCALE_NTS_MAGIC;

        if (good)
        {
                memory_copy(state, bytes + 4, sizeof(nts_state));
                //      Fields that are read as text end where they should.
                state->origin[NTS_SERVER_MAX - 1] = end;
                state->server[NTS_SERVER_MAX - 1] = end;
                good = locale_nts_state_ok(state);
        }
        crypto_forget(bytes, sizeof(bytes));
        if (!good)
                memory_zero(state, sizeof(*state));
        return good;
}

/*      The keys and cookies of the last key establishment, kept so that the
        next query does not make another. A write that fails is not said, as
        none of the clock's /run files' is: all that is lost is the saving.
        The next query finds the cookies this one spent, is refused for them
        (SNTP_NTS_NAK) and establishes again, one handshake; the query that
        asked has its answer already. The loop that keeps the clock calls this
        at every query, so a /run that cannot be written would be said at each
        of them, with nobody to say it to. */
static fn locale_nts_save(const nts_state address_to state)
{
        p8 bytes[4 + sizeof(nts_state)];

        network_store_32(bytes, LOCALE_NTS_MAGIC);
        memory_copy(bytes + 4, state, sizeof(nts_state));
        host_state_ready();
        (void)host_write_file(LOCALE_NTS_STATE_PATH, bytes, sizeof(bytes), 0600,
                              false);
        crypto_forget(bytes, sizeof(bytes));
}

//      A negotiated time server that is not the key-establishment server's
//      own name has to be a public address, unless the key-establishment
//      server is not one: a name in an authenticated reply is still a name
//      someone chose, and this is the same test a redirect gets.
static bool locale_nts_server_ok(string_address host, string_address server)
{
        p32 ip;

        if (!server[0] || string_equals(server, host))
                return true;
        ip = http_lookup_apart(server);
        return ip && (http_address_public(ip) ||
                      !http_address_public(http_lookup_apart(host)));
}

static fn locale_nts_why_set(string_address host, string_address why)
{
        string_copy_bounded((string_address)locale_nts_why, host,
                            sizeof(locale_nts_why));
        string_append_bounded((string_address)locale_nts_why, ": ",
                              sizeof(locale_nts_why));
        string_append_bounded((string_address)locale_nts_why, why,
                              sizeof(locale_nts_why));
}

//      One key-establishment server asked for the time: the exchange with
//      its cookies, or the key establishment first when there is no state
//      or none left, and once more if the server says the cookie is no
//      good. The answer is authenticated or there is none. reached says a
//      name resolved, so a network that blocks NTS is told from one that is
//      not up yet.
static COLD bipolar locale_nts_ask(string_address entry, bool filter,
                                   sntp_sample address_to answer,
                                   bool address_to reached)
{
        static nts_state state;
        nts_exchange exchange;
        p8 host[NTS_SERVER_MAX];
        p16 port;
        bool have;
        bool said = false;
        bipolar status = SNTP_NO_SERVER;

        if (!locale_nts_entry(entry, host, address_of port))
                return SNTP_NO_SERVER;
        have = locale_nts_load(&state) &&
               string_equals((string_address)state.origin, (string_address)host) &&
               state.origin_port == port && state.count;
        for (positive attempt = 0; attempt < 2; attempt++)
        {
                string_address server;

                if (!have)
                {
                        //      Until an authenticated answer has been taken this
                        //      boot, the certificate is judged as of the floor.
                        bipolar why = nts_ke_apart(
                            (string_address)host, port,
                            !locale_clock_authenticated_now(null), &state);

                        if (why)
                        {
                                address_to reached |= why != HTTP_NO_HOST;
                                locale_nts_why_set((string_address)host,
                                                   locale_auto_reason(why));
                                said = true;
                                status = SNTP_NO_SERVER;
                                break;
                        }
                        string_copy_bounded((string_address)state.origin,
                                            (string_address)host,
                                            sizeof(state.origin));
                        state.origin_port = port;
                        if (!locale_nts_state_ok(&state))
                                break;
                        locale_nts_save(&state);
                }
                server = state.server[0] ? (string_address)state.server
                                         : (string_address)host;
                address_to reached = true;
                if (!locale_nts_server_ok((string_address)host, server))
                {
                        locale_nts_why_set((string_address)host,
                                           (string_address) "it named a time "
                                           "server that is not a public "
                                           "address");
                        said = true;
                        status = SNTP_NO_SERVER;
                        break;
                }
                nts_exchange_open(&exchange, &state);
                //      An authenticated answer is believed wherever the window
                //      puts it.
                status = sntp_query_apart(server, state.port, filter,
                                          SNTP_OFFSET_ANY_NS, &exchange, answer);
                nts_exchange_close(&exchange);
                locale_nts_save(&state);
                if (status != SNTP_NTS_NAK)
                        break;
                have = false;
        }
        if (status < 0 && !said)
                locale_nts_why_set((string_address)host,
                                   status == SNTP_NTS_NAK
                                       ? (string_address) "its cookies were "
                                                          "refused twice"
                                       : (string_address) "no authenticated "
                                                          "answer");
        crypto_forget(&state, sizeof(state));
        return status;
}

//      NTS servers have been tried and failed within the last two minutes.
static bool locale_nts_backed_off(bool by_hand)
{
        p8 text[24];
        positive at = 0;
        p64 then;

        host_read_word(LOCALE_NTS_FAILED_PATH, text, sizeof(text));
        return !by_hand &&
               locale_clock_number((string_address)text, address_of at, address_of then) &&
               !text[at] &&
               system_clock_ns(HOST_CLOCK_BOOTTIME) <
                   then * SNTP_NANOSECONDS + LOCALE_NTS_BACKOFF_NS;
}

/*      The time the NTS servers failed, which locale_nts_backed_off reads so
        that they are not asked again at every query. Not said when it cannot
        be written, for locale_nts_save's reason: the cost is a retry sooner
        than the pause says, which is what a machine that kept no mark does. */
static fn locale_nts_failed(void)
{
        p8 text[24];
        positive length = positive_into(
            text, (positive)(system_clock_ns(HOST_CLOCK_BOOTTIME) / SNTP_NANOSECONDS));

        text[length++] = '\n';
        host_state_ready();
        (void)host_write_file(LOCALE_NTS_FAILED_PATH, text, length, 0644, false);
}

/*
        The NTS servers in order, until one answer may be believed: the
        first one does on a clock that is unset (it has nothing to
        contradict) and for the only server there is, and so does one that
        moves a set clock two seconds or less; a step past two seconds on a
        clock that is set is taken on two servers' words, authenticated or
        not, so one wrong or compromised server cannot make it (sntp_choose,
        as for the pool). heard says some answer authenticated, which ends
        the walk whatever became of it: the pool is not asked to overrule
        authenticated time.
*/
static bipolar locale_nts_walk(bool address_to heard_any, bool address_to reached)
{
        p8 servers[LOCALE_NTS_SERVERS_MOST * LOCALE_NTS_ENTRY_ROOM];
        positive count = locale_nts_servers(servers, LOCALE_NTS_ENTRY_ROOM);
        bool filter = locale_switch_on(LOCALE_NTP_SAMPLING_PATH);
        sntp_sample row[SNTP_SERVERS];
        positive from[SNTP_SERVERS];
        positive heard = 0;
        bipolar failed = SNTP_NO_SERVER;
        bool rated = false;
        bool denied = false;
        bipolar now = sntp_now_ns();
        bool alone = count == 1 || now < 0 || !sntp_wall_ok(now) ||
                     now < (bipolar)clock_trust_floor() * (bipolar)SNTP_NANOSECONDS;

        for (positive at = 0; at < count && heard < SNTP_SERVERS; at++)
        {
                failed = locale_nts_ask((string_address)servers + at * LOCALE_NTS_ENTRY_ROOM,
                                        filter, row + heard, reached);
                if (failed < 0)
                {
                        rated |= failed == SNTP_RATE_LIMITED;
                        denied |= failed == SNTP_DENIED;
                        continue;
                }
                if (locale_ntp_heard(row, heard, row[heard].address))
                        continue;
                from[heard++] = at;
                if (alone ||
                    (heard == 1 && sntp_within(row[0].offset_ns, SNTP_OFFSET_SYNCED_NS)) ||
                    (heard > 1 && sntp_choose(row, heard) >= 0))
                        break;
        }
        address_to heard_any = heard != 0;
        if (heard)
        {
                bipolar chosen = alone ? 0 : sntp_choose(row, heard);

                if (chosen < 0)
                        return SNTP_UNCONFIRMED;
                return locale_ntp_take((string_address)servers +
                                           from[chosen] * LOCALE_NTS_ENTRY_ROOM,
                                       row + chosen, true);
        }
        return denied ? SNTP_DENIED : rated ? SNTP_RATE_LIMITED : failed;
}

static bipolar locale_ntp_plain(bool by_hand)
{
        p8 server[LOCALE_NTP_SERVER_ROOM];
        bipolar failed = SNTP_NO_SERVER;
        positive at;
        bool filter = locale_switch_on(LOCALE_NTP_SAMPLING_PATH);
        bipolar now = sntp_now_ns();
        //      A clock nobody has set has no time of its own to be moved
        //      from, and the window bounds what it can be told.
        bool unset = now >= 0 && !sntp_wall_ok(now);
        //      How far an answer may move the clock: two seconds when it
        //      tells the time, a day when it does not, and for a person who
        //      asked, anything the window holds -- a clock that is a week
        //      out is refused by every sample otherwise, with nothing to
        //      bring it back.
        bipolar most = by_hand ? SNTP_OFFSET_ANY_NS
                       : locale_clock_synced() ? SNTP_OFFSET_SYNCED_NS
                                               : SNTP_OFFSET_MOST_NS;
        bool rated = false;
        bool denied = false;
        sntp_sample heard[SNTP_SERVERS];
        b32 heard_from[SNTP_SERVERS];
        positive heard_count = 0;
        positive wanted;

        host_read_word(LOCALE_NTP_SERVER_PATH, server, sizeof(server));
        if (server[0])
        {
                failed = locale_ntp_ask((string_address)server, filter, most,
                                        heard);
                if (failed >= 0)
                {
                        heard_from[0] = array_count(locale_ntp_fallback);
                        heard_count = 1;
                        //      Plain time is unauthenticated: one server's
                        //      word moves a clock that is set by two
                        //      seconds at most, by hand or not, and a
                        //      further step waits for a second server (or
                        //      for NTS, or for `date -s` from a person who
                        //      knows the time).
                        if (unset || sntp_within(heard[0].offset_ns,
                                                 SNTP_OFFSET_SYNCED_NS))
                                return locale_ntp_take((string_address)server,
                                                       heard, false);
                }
                rated = failed == SNTP_RATE_LIMITED;
                denied = failed == SNTP_DENIED;
        }

        //      A named server remains the first choice, but its name is not
        //      an independent confirmation of a large clock step.
        wanted = heard_count ? 2
                 : filter && !server[0] && !unset ? SNTP_SERVERS
                                                  : 1;
        for (at = 0; at < array_count(locale_ntp_fallback) &&
                     heard_count < wanted; at++)
        {
                if (server[0] &&
                    string_equals((string_address)server,
                                  (string_address)locale_ntp_fallback[at]))
                        continue;
                failed = locale_ntp_ask((string_address)locale_ntp_fallback[at],
                                        filter, most, heard + heard_count);
                //      Another name for a server already heard from is that
                //      server saying it again: the network's resolver can
                //      name every one of these the same host. It is not
                //      counted, and the next name is asked.
                if (failed >= 0 &&
                    !locale_ntp_heard(heard, heard_count,
                                      heard[heard_count].address))
                        heard_from[heard_count++] = (b32)at;
                else if (failed == SNTP_RATE_LIMITED)
                        rated = true;
                else if (failed == SNTP_DENIED)
                        denied = true;
                //      A far step is not taken on one server's word.
                if (heard_count == 1 && wanted < 2 && !unset &&
                    !sntp_within(heard[0].offset_ns, SNTP_OFFSET_SYNCED_NS))
                        wanted = 2;
        }

        if (heard_count)
        {
                bipolar chosen = sntp_choose(heard, heard_count);

                if (chosen < 0 && heard_count == 1 && unset)
                        chosen = 0;
                if (chosen < 0)
                        return SNTP_UNCONFIRMED;
                return locale_ntp_take(
                    heard_from[chosen] == array_count(locale_ntp_fallback)
                        ? (string_address)server
                        : (string_address)locale_ntp_fallback[heard_from[chosen]],
                    heard + chosen, false);
        }
        return denied ? SNTP_DENIED : rated ? SNTP_RATE_LIMITED : failed;
}

/*
        NTS first, then the pool. An answer an NTS server authenticated ends
        the walk whatever became of it (taken, refused below the floor, or
        unconfirmed): the pool is not asked to overrule authenticated time.
        With none, a network that answered but would not do NTS is left for
        two minutes (locale_nts_failed), and the pool is asked unless NTS is
        only.
*/
static bipolar locale_ntp_walk(bool by_hand)
{
        p8 named[LOCALE_NTP_SERVER_ROOM];
        positive mode = locale_nts_mode();
        bipolar failed = SNTP_NO_SERVER;
        bool heard = false;
        bool reached = false;

        locale_nts_why[0] = end;
        host_read_word(LOCALE_NTP_SERVER_PATH, named, sizeof(named));
        if (mode != NTS_OFF && (!named[0] || mode == NTS_ONLY))
        {
                if (locale_nts_backed_off(by_hand))
                        string_copy_bounded((string_address)locale_nts_why,
                                            "not asked: none answered in the "
                                            "last two minutes",
                                            sizeof(locale_nts_why));
                else
                {
                        failed = locale_nts_walk(address_of heard,
                                                 address_of reached);
                        if (heard)
                                return failed;
                        if (reached)
                                locale_nts_failed();
                }
        }
        if (mode == NTS_ONLY)
                return failed;
        return locale_ntp_plain(by_hand);
}

/*
        One query at a time, whoever asks. The offset is measured against the
        clock as it is and applied relative to it, so two that measured
        before either applied would both move the clock by the same amount:
        the machine's own query and `moonwater time sync` or `ntp on` typed
        while it runs. The machine's gives way and is asked again, and a
        hand waits its turn, and says so, since a walk is up to a minute.
*/
static bipolar locale_ntp_apply(bool by_hand)
{
        bipolar lock = host_lock(LOCALE_NTP_LOCK_PATH, false);
        bipolar failed;

        if (lock == -EAGAIN)
        {
                if (!by_hand)
                        return SNTP_NO_REPLY;
                host_say(log, host_label "time: another query is running; "
                                         "waiting for it\n");
                lock = host_lock(LOCALE_NTP_LOCK_PATH, true);
        }
        //      Without the lock another query could move the clock too: no walk.
        if (lock < 0)
                return SNTP_NO_REPLY;
        failed = locale_ntp_walk(by_hand);
        radio_unlock(lock);
        return failed;
}

//      The server asked first: the one named, or the pool's name.
static string_address locale_ntp_server_say(p8 address_to into, positive room)
{
        if (!locale_readable(LOCALE_NTP_SERVER_PATH, into, room))
                return (string_address)LOCALE_NEEDS_ROOT;
        if (!into[0])
                string_copy_bounded(into, LOCALE_NTP_DEFAULT_SERVER, room);
        return (string_address)into;
}

//      moonwater ntp nts: whether it is asked for first, who is asked, and
//      what set the clock last (nts: authenticated; plain: the pool, which
//      anyone on the path could have forged).
static fn locale_nts_status(bool label)
{
        p8 servers[LOCALE_NTS_SERVERS_MOST * LOCALE_NTS_ENTRY_ROOM];
        p8 last[LOCALE_NTP_LAST_ROOM];
        string_address said = locale_nts_say();
        positive count;

        if (label)
                string_format(log, host_label "ntp nts %s", said);
        else
                string_format(log, "  ntp nts %s", said);
        if (!string_equals(said, LOCALE_NEEDS_ROOT))
        {
                count = locale_nts_servers(servers, LOCALE_NTS_ENTRY_ROOM);
                string_format(log, ", servers");
                //      Each server is read from /root/ntp.nts.servers, a disk's
                //      to choose: spelled so a planted byte cannot forge a line.
                for (positive at = 0; at < count; at++)
                        string_format(log, " %w", writer_terminal_name,
                                      (string_address)servers + at * LOCALE_NTS_ENTRY_ROOM);
        }
        host_read_word(LOCALE_NTP_LAST_PATH, last, sizeof(last));
        host_say(log, "; the clock was last set by %s\n",
                 last[0] ? (string_address)last
                         : (string_address) "nothing this boot");
}

static b32 locale_ntp_status(void)
{
        p8 server[LOCALE_NTP_SERVER_ROOM];

        //      The server is the one named in /root/ntp.server, a disk's to
        //      choose: spelled through writer_terminal_name so a planted byte
        //      cannot forge a line. The rest are this program's own words.
        host_say(log, host_label "ntp %s, %w, sampling %s, %s\n",
                 locale_switch_say(LOCALE_NTP_PATH),
                 writer_terminal_name,
                 locale_ntp_server_say(server, sizeof(server)),
                 locale_switch_say(LOCALE_NTP_SAMPLING_PATH),
                 locale_clock_synced() ? "synchronised" : "waiting");
        locale_nts_status(false);
        return 0;
}

//      moonwater ntp nts [on|off|only|servers [default|NAME[:PORT]...]]
static b32 locale_nts_command(string_address address_to arguments,
                              positive count)
{
        string_address word = count > 3 ? arguments[3] : null;

        if (count == 3)
        {
                locale_nts_status(true);
                return 0;
        }
        if (string_equals(word, "servers"))
        {
                p8 text[LOCALE_NTS_SERVERS_MOST * LOCALE_NTS_ENTRY_ROOM];
                positive length = 0;
                bipolar kept;

                if (count == 4)
                {
                        locale_nts_status(true);
                        return 0;
                }
                return_if(count - 4 > LOCALE_NTS_SERVERS_MOST, host_usage());
                host_need_root();
                if (count == 5 && string_equals(arguments[4], "default"))
                {
                        kept = system_remove_at(AT_FDCWD, LOCALE_NTS_SERVERS_PATH, 0);
                        return_if(kept < 0 && kept != -ENOENT,
                                  host_fail("ntp nts servers", kept));
                        locale_nts_status(true);
                        return 0;
                }
                for (positive at = 4; at < count; at++)
                {
                        p8 host[NTS_SERVER_MAX];
                        p16 port;
                        positive size = string_length(arguments[at]);

                        //      default stands alone: beside a name it would be
                        //      a server of that name, asked for the time.
                        return_if(string_equals(arguments[at], "default"),
                                  host_usage());
                        if (size >= LOCALE_NTS_ENTRY_ROOM ||
                            !locale_nts_entry(arguments[at], host, address_of port))
                                return host_refuse("%s is not a server: a name or "
                                                   "address, or NAME:PORT\n",
                                                   arguments[at]);
                        memory_copy(text + length, arguments[at], size);
                        length += size;
                        text[length++] = '\n';
                }
                kept = host_write_file(LOCALE_NTS_SERVERS_PATH, text, length,
                                       0644, true);
                return_if(kept < 0, host_fail("ntp nts servers", kept));
                locale_nts_status(true);
                return 0;
        }
        return_if(count != 4 || (!string_equals(word, "on") &&
                                 !string_equals(word, "off") &&
                                 !string_equals(word, "only")),
                  host_usage());
        host_need_root();
        if (host_save("ntp nts", LOCALE_NTS_PATH, word))
                return 1;
        if (!string_equals(word, "off"))
                locale_ntp_next = 0;
        locale_nts_status(true);
        return 0;
}

static b32 locale_ntp_sampling_status(void)
{
        host_say(log, host_label "ntp sampling %s\n",
                 locale_switch_say(LOCALE_NTP_SAMPLING_PATH));
        return 0;
}

//      moonwater ntp on|off, and ntp sampling on|off; turning ntp on asks
//      at once.
static b32 locale_ntp_set(bool sampling, string_address word)
{
        if (host_save("ntp", sampling ? LOCALE_NTP_SAMPLING_PATH : LOCALE_NTP_PATH, word))
                return 1;
        if (!sampling && host_onoff(word) > 0)
        {
                locale_ntp_next = 0;
                if (locale_ntp_apply(true) < 0)
                        word = "on, waiting for a reply";
        }
        host_say(log, host_label "ntp %s%s\n", sampling ? "sampling " : "",
                 word);
        return 0;
}

/*
        moonwater ntp server [NAME|auto]: the server asked first. A name or a
        dotted address that fits the word the walk reads it into, and not a
        longer one cut to fit; auto goes back to the pool and the names
        beside it.
*/
static bool locale_ntp_server_ok(string_address name)
{
        positive length = string_length(name);
        positive label = 0;

        if (!length || length >= LOCALE_NTP_SERVER_ROOM ||
            !byte_is_alnum((p8)name[0]) || string_equals(name, "auto"))
                return false;
        //      Labels as the NTS names are read: none empty, none beginning or
        //      ending in a dash, so a.-b and a..b are refused here too.
        for (positive at = 0; at < length; at++)
        {
                p8 c = (p8)name[at];

                if (c == '.')
                {
                        if (!label || name[at - 1] == '-')
                                return false;
                        label = 0;
                }
                else if (byte_is_alnum(c) || (c == '-' && label))
                {
                        //      The name is asked of the resolver (dns_write_name), which refuses a label of
                        //      more than 63 bytes, as RFC 1035 does: it was kept and then never answered.
                        if (++label > 63)
                                return false;
                }
                else
                        return false;
        }
        //      A name may end in the dot that makes it absolute, as a name
        //      typed that way always has: the resolver takes it, where the NTS
        //      names (nts_name_ok) do not, being a TLS name's, which has none.
        return name[length - 1] != '-' && (label || name[length - 1] == '.');
}

static b32 locale_ntp_server_status(void)
{
        p8 server[LOCALE_NTP_SERVER_ROOM];
        string_address said = locale_ntp_server_say(server, sizeof(server));

        host_say(log, host_label "ntp server %s\n", said);
        return 0;
}

static b32 locale_ntp_server_set(string_address name)
{
        bool automatic = string_equals(name, "auto");
        bipolar kept;

        if (!automatic && !locale_ntp_server_ok(name))
                return host_refuse("%s is not a server name: letters, digits, "
                                   "dots and hyphens, labels of 63 characters at most, "
                                   "under 80 characters, or auto for the pool\n", name);
        if (automatic)
        {
                kept = system_remove_at(AT_FDCWD, LOCALE_NTP_SERVER_PATH, 0);
                if (kept == -ENOENT)
                        kept = 0;
        }
        else
                kept = radio_write_word(LOCALE_NTP_SERVER_PATH, name);
        return_if(kept < 0, host_fail("ntp server", kept));
        //      Asked of the new server at once, as `ntp on` does.
        if (locale_ntp_wanted() && locale_ntp_apply(true) < 0)
                host_say(log, host_label "ntp server %s, waiting for a "
                                         "reply\n", name);
        else
                host_say(log, host_label "ntp server %s\n", name);
        return 0;
}

//      A layout by the name this machine takes, and by the one XKB calls it:
//      uk is gb there, and the country codes sv and nb are se and no.
static const struct
{
        char name[8];
        char xkb[4];
} locale_keyboards[] = {
        {"us", "us"}, {"uk", "gb"}, {"gb", "gb"}, {"de", "de"}, {"se", "se"},
        {"sv", "se"}, {"no", "no"}, {"nb", "no"}, {"dk", "dk"}, {"fi", "fi"},
        {"fr", "fr"}, {"es", "es"}, {"it", "it"}, {"ar", "ara"}, {"il", "il"}, {"ir", "ir"},
};

static bool locale_keyboard_ok(string_address name)
{
        positive at;

        for (at = 0; at < array_count(locale_keyboards); at++)
                if (string_equals(name, (string_address)locale_keyboards[at].name))
                        return true;
        return false;
}

//      The layout that is kept: us when none is, and nothing when it is not
//      the caller's to read.
static string_address locale_keyboard_say(p8 address_to name, positive room)
{
        if (!locale_readable(LOCALE_KEYBOARD_PATH, name, room))
                return (string_address)LOCALE_NEEDS_ROOT;
        if (!name[0])
                string_copy_bounded(name, "us", room);
        return (string_address)name;
}

static b32 locale_keyboard_status(void)
{
        p8 name[16];

        host_say(log, host_label "keyboard %w\n", writer_terminal_name,
                 locale_keyboard_say(name, sizeof(name)));
        return 0;
}

//      The three lines `status` shows of this region's settings.
static fn locale_status_lines(void)
{
        p8 title[128];
        p8 how[48];
        p8 keyboard[16];

        //      title, how and keyboard are a disk's to choose (/root/timezone,
        //      /root/timezone.mode, /root/keyboard): spelled through
        //      writer_terminal_name so a planted control or high byte cannot
        //      forge a status line. The switches are this program's own words.
        if (locale_zone_read(title, sizeof(title), how, sizeof(how)))
                string_format(log, "  timezone %w %w\n", writer_terminal_name,
                              title, writer_terminal_name, how);
        else
                string_format(log, "  timezone " LOCALE_NEEDS_ROOT "\n");
        string_format(log, "  ntp %s, sampling %s\n",
                      locale_switch_say(LOCALE_NTP_PATH),
                      locale_switch_say(LOCALE_NTP_SAMPLING_PATH));
        string_format(log, "  keyboard %w\n",
                      writer_terminal_name,
                      locale_keyboard_say(keyboard, sizeof(keyboard)));
}

static b32 locale_keyboard_list(void)
{
        string_format(log, "  layouts");
        for (positive at = 0; at < array_count(locale_keyboards); at++)
                string_format(log, " %s",
                              (string_address)locale_keyboards[at].name);
        host_say(log, "\n" TERM_DIM "  a layout's code is its country's,"
                      " so moonwater timezone takes it too" TERM_RESET
                      "\n");
        return 0;
}

static b32 locale_keyboard_set(string_address name)
{
        if (!locale_keyboard_ok(name))
                return host_refuse("unknown keyboard layout %s -- "
                                   "moonwater keyboard list shows them\n",
                                   name);
        //      Kept in /root, which the compositor watches: it takes the
        //      layout as soon as the file is written and when it starts.
        if (host_save("keyboard", LOCALE_KEYBOARD_PATH, name))
                return 1;
        host_say(log, host_label "keyboard %s\n", name);
        return 0;
}

/*
        How a real screen is driven: the mode it is at, the largest it lists or the one it marks preferred. It is the
        machine's and not the image's: asked bare, set with a word. A word is tried on the kernel first, which refuses
        what it does not take and keeps what it is given with Canvas off, and only then kept in /root, as the keyboard
        layout is; the machine process puts the kept one back when it starts, and a kernel command line (canvas.modes=
        preferred) gives the first frame the one before that. The scale of a screen is not here: each screen's is its
        displays line's (canvas display NAME scale PERCENT, which the compositor reads as it changes).
*/
static bool locale_canvas_modes_ok(string_address word)
{
        return string_equals(word, "largest") || string_equals(word, "preferred");
}

static bipolar locale_canvas_live(string_address word)
{
        struct canvas_control control;

        memory_zero(address_of control, sizeof(control));
        control.request = SPARK_CANVAS_SET_MODES;
        control.modes = string_equals(word, "preferred")
                            ? SPARK_CANVAS_MODES_PREFERRED : SPARK_CANVAS_MODES_LARGEST;
        return host_spark_once(SPARK_IOCTL_CANVAS, address_of control, FILE_READ);
}

//      What is in force, from the kernel when it can be asked, and else what
//      is kept: the largest mode when nothing is.
static b32 locale_canvas_status(void)
{
        struct canvas_control control;
        string_address path = (string_address)LOCALE_CANVAS_MODES_PATH;
        p8 kept[16];
        bool readable = locale_readable(path, kept, sizeof(kept));
        string_address and_kept = kept[0] ? ", kept across boots" : "";

        if (host_canvas_request(SPARK_CANVAS_STATUS, address_of control) < 0)
        {
                if (!readable)
                        host_say(log, host_label "modes " LOCALE_NEEDS_ROOT "\n");
                else
                        host_say(log, host_label "modes %s, Canvas is not asking\n",
                                 kept[0] ? (string_address)kept : (string_address) "largest");
                return 0;
        }
        host_say(log, host_label "modes %s%s\n",
                 control.modes == SPARK_CANVAS_MODES_PREFERRED ? "preferred" : "largest", and_kept);
        return 0;
}

//      moonwater canvas modes [largest|preferred]
static b32 locale_canvas_setting(string_address address_to arguments, positive count)
{
        string_address word = count > 3 ? arguments[3] : null;
        bipolar live;

        return_if(count > 4, host_usage());
        if (!word)
                return locale_canvas_status();
        if (!locale_canvas_modes_ok(word))
                return host_refuse("modes are largest or preferred, not %s\n", word);
        //      Root is asked before the word is kept, not only by the kernel:
        //      with no Canvas in it the write to /root is the only thing that
        //      could refuse, and that answers with whatever /root's owner says.
        if (!bowl_is_root())
                return host_refuse("setting how Canvas draws needs root (CAP_SYS_ADMIN)\n");
        //      The kernel first, so a refusal keeps nothing; Canvas not being
        //      there at all keeps the word and takes it when it starts.
        live = locale_canvas_live(word);
        if (live == -EPERM)
                return host_refuse("setting how Canvas draws needs root (CAP_SYS_ADMIN)\n");
        if (live == -EINVAL)
                return host_refuse("Canvas does not take %s\n", word);
        if (host_save("canvas modes", LOCALE_CANVAS_MODES_PATH, word))
                return 1;
        if (live < 0 && live != -ENOENT)
        {
                host_say(log_error, host_label "modes %s is kept, but Canvas did not take "
                                    "it: %s\n", word, file_reason(live));
                return 1;
        }
        return locale_canvas_status();
}

