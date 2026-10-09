/*
        Experimental C standard library

        Waiting for a readable descriptor

        Dawn Larsson - Apache-2.0 license
        github.com/dawnlarsson/moonwater

        www.dawning.dev
*/

#ifndef STANDARD_MODERN_C_NET_WAIT
#define STANDARD_MODERN_C_NET_WAIT

#define NETWORK_INTERRUPTED (-4)
#define NETWORK_TRY_AGAIN (-11)
#define NETWORK_NANOSECONDS 1000000000

/* How many datagrams a loop that waits for one answer may throw away before
   it gives the answer up: a peer able to keep a socket readable must not buy
   unbounded parsing and system calls inside one deadline. */
#define NETWORK_DISCARD_MAX 64

/* One enclosing exchange owns this allowance. Parsing a packet successfully
   must not replenish it when the caller then rejects that packet's meaning. */
static bool network_discard_one(positive address_to remaining)
{
        if (!address_to remaining)
                return false;
        address_to remaining -= 1;
        return true;
}

typedef struct
{
        positive began;
        positive budget;
} network_deadline;

/* One absolute monotonic budget for every packet loop.  Recomputing the
   remaining interval before each poll means junk packets neither buy the
   sender more time nor consume a retry that represented elapsed time. */
static bool network_deadline_begin(network_deadline address_to deadline,
                                   positive seconds, positive nanoseconds)
{
        positive began;

        if (nanoseconds >= NETWORK_NANOSECONDS)
                return false;

        if (seconds > (positive_max - nanoseconds) / NETWORK_NANOSECONDS)
                deadline->budget = positive_max;
        else
                deadline->budget = seconds * NETWORK_NANOSECONDS + nanoseconds;

        began = clock_monotonic_nanoseconds();
        deadline->began = began;
        return began != 0 && deadline->budget != 0;
}

/* A deferred relative wait may live inside an earlier whole-operation
   deadline. Clamp at the same clock observation that starts it, so neither
   work before the first wait nor a later retry can extend the outer budget.
   Copy the enclosing values first, so the two pointers may alias. */
static bool network_deadline_begin_within(
    network_deadline address_to deadline, positive seconds, positive nanoseconds,
    const network_deadline address_to outer)
{
        network_deadline enclosing;
        positive elapsed;
        positive left;

        if (outer)
                enclosing = *outer;
        if (!network_deadline_begin(deadline, seconds, nanoseconds))
                return false;
        if (!outer)
                return true;
        if (!enclosing.began || deadline->began < enclosing.began)
                return false;
        elapsed = deadline->began - enclosing.began;
        if (elapsed >= enclosing.budget)
                return false;
        left = enclosing.budget - elapsed;
        if (deadline->budget > left)
                deadline->budget = left;
        return true;
}

static bool network_deadline_left(
    const network_deadline address_to deadline, positive address_to seconds,
    positive address_to nanoseconds)
{
        positive now = clock_monotonic_nanoseconds();
        positive elapsed;
        positive left;

        if (!deadline->began || !now || now < deadline->began)
                return false;

        elapsed = now - deadline->began;
        if (elapsed >= deadline->budget)
                return false;

        left = deadline->budget - elapsed;
        address_to seconds = left / NETWORK_NANOSECONDS;
        address_to nanoseconds = left % NETWORK_NANOSECONDS;
        return true;
}

/* A DNS id is part of reply authentication, so its availability tradeoff is
   the same CSPRNG discipline DHCP and SNTP now use for their transaction
   tags: refuse to send when the initialized kernel pool is not ready rather
   than exposing a timing/PID-derived value. */
static inline INLINE bool network_transaction_secure(address_any into,
                                                      positive width)
{
        if (!into || !width || width > sizeof(positive))
                return false;

        return system_random_fill(into, width, 1) == 0;
}

/* One wait on any of several descriptors, up to the deadline: how many are
   ready, 0 when it went by with none, -9 when one of them is no descriptor. */
static bipolar network_wait_set(
    system_poll_descriptor address_to set, positive count,
    const network_deadline address_to deadline)
{
        for (;;)
        {
                positive seconds;
                positive nanoseconds;
                timespec limit;
                bipolar ready;

                if (!network_deadline_left(deadline, address_of seconds,
                                           address_of nanoseconds))
                        return 0;
                limit.tv_sec = (b64)seconds;
                limit.tv_nsec = (b64)nanoseconds;
                ready = system_poll_wait(set, count, address_of limit, null);
                if (ready == NETWORK_INTERRUPTED)
                        continue;
                for (positive at = 0; ready > 0 && at < count; at++)
                        if (set[at].returned & SYSTEM_POLL_INVALID)
                                return -9;
                return ready;
        }
}

static bipolar network_wait_until(
    bipolar handle, b16 events,
    const network_deadline address_to deadline)
{
        system_poll_descriptor waited = {(b32)handle, events, 0};

        return network_wait_set(address_of waited, 1, deadline);
}

static bipolar network_wait_readable_until(
    bipolar handle, const network_deadline address_to deadline)
{
        return network_wait_until(handle, SYSTEM_POLL_READ, deadline);
}

static bipolar network_wait_writable_until(
    bipolar handle, const network_deadline address_to deadline)
{
        return network_wait_until(handle, SYSTEM_POLL_WRITE, deadline);
}

/* What network_receive_next answers when the deadline went by with nothing to
   read: no errno is as large as this. */
#define NETWORK_SILENT (-4096)

/* The next datagram to reach handle before the deadline, cut to room (the
   length is what it was, MSG_TRUNC's way, so a longer one says so): that
   length, NETWORK_SILENT when the deadline went by first, or the error that
   ended the wait or the receive. Queued datagrams need no readiness poll;
   only EAGAIN waits. Interruption and vanished readiness are
   retried under the same deadline; the receive itself never blocks. A peer
   is filled for the datagram, with its size, when asked for; sntp's, which
   reads the stamps the kernel leaves beside a datagram, is its own. */
static bipolar network_receive_next(
    bipolar handle, p8 address_to into, positive room,
    socket_address_internet address_to peer, p32 address_to peer_size,
    const network_deadline address_to deadline)
{
        for (;;)
        {
                positive seconds;
                positive nanoseconds;
                bipolar got;

                if (!network_deadline_left(deadline, address_of seconds,
                                           address_of nanoseconds))
                        return NETWORK_SILENT;
                if (peer)
                {
                        memory_fill(peer, 0, sizeof *peer);
                        address_to peer_size = sizeof *peer;
                }
                got = socket_receive((b32)handle, into, room,
                                     MSG_TRUNC | MSG_DONTWAIT, peer,
                                     peer_size);
                if (got != NETWORK_INTERRUPTED && got != NETWORK_TRY_AGAIN)
                        return got;
                if (got == NETWORK_TRY_AGAIN)
                {
                        bipolar ready = network_wait_readable_until(handle,
                                                                     deadline);

                        if (ready <= 0)
                                return ready ? ready : NETWORK_SILENT;
                }
        }
}

/* Deadline-bound stream reads must not enter a blocking read merely because
   one byte was ready: a peer could then trickle the rest forever under the
   socket's renewing idle timeout. Poll the absolute budget and consume only
   bytes immediately available before recomputing what remains. Queued data
   needs no readiness syscall; an empty socket waits before trying again. */
static bipolar network_stream_read_some_until(
    bipolar handle, p8 address_to into, positive length,
    const network_deadline address_to deadline)
{
        for (;;)
        {
                positive seconds;
                positive nanoseconds;

                if (!network_deadline_left(deadline, address_of seconds,
                                           address_of nanoseconds))
                        return -1;
                bipolar got = socket_receive((b32)handle, into, length,
                                             MSG_DONTWAIT, null, 0);

                if (got != NETWORK_TRY_AGAIN && got != NETWORK_INTERRUPTED)
                        return got;
                if (got == NETWORK_TRY_AGAIN)
                {
                        bipolar ready = network_wait_readable_until(handle,
                                                                     deadline);

                        if (ready <= 0)
                                return ready < 0 ? ready : -1;
                }
        }
}

/* A read in the middle of a stream, where bytes are usually queued already:
   they are taken without a poll, and the clock -- a system call here -- is
   read only after an empty or interrupted fast attempt starts its budget. */
static bipolar network_stream_read_some_for(bipolar handle, p8 address_to into,
                                            positive length, positive seconds,
                                            positive nanoseconds)
{
        network_deadline deadline;
        bipolar got = socket_receive((b32)handle, into, length, MSG_DONTWAIT,
                                     null, 0);

        if (got != NETWORK_TRY_AGAIN && got != NETWORK_INTERRUPTED)
                return got;
        /* A single fast attempt keeps queued data clock-free. Interruption
           starts the same bounded wait as an empty socket; it must not enter
           the untimed helper's retry loop. An empty socket already needs a
           readiness wait, so avoid a second speculative receive before it. */
        if (!network_deadline_begin(address_of deadline, seconds, nanoseconds))
                return -1;
        if (got == NETWORK_TRY_AGAIN)
        {
                bipolar ready = network_wait_readable_until(handle,
                                                             address_of deadline);

                if (ready <= 0)
                        return ready < 0 ? ready : -1;
        }
        return network_stream_read_some_until(handle, into, length,
                                              address_of deadline);
}

/* Stream protocols share exact-record reads and complete writes.  A read
   returns false on either EOF or an error before the requested span; a send
   suppresses SIGPIPE and owns both interruption and short progress. */
static bool network_stream_read_all(
    bipolar handle, p8 address_to into, positive length,
    const network_deadline address_to deadline)
{
        positive used = 0;

        while (used < length)
        {
                bipolar got = deadline
                    ? network_stream_read_some_until(
                          handle, into + used, length - used, deadline)
                    : system_read_retry((positive)handle, into + used,
                                        length - used);

                if (got <= 0 || (positive)got > length - used)
                        return false;
                used += (positive)got;
        }

        return true;
}

/* A nonblocking stream write consumes one absolute budget across partial
   progress, interruption and backpressure.  This is the write-side companion
   to network_stream_read_all(..., deadline). */
static bool network_stream_send_all_until(
    bipolar handle, p8 address_to data, positive length,
    const network_deadline address_to deadline)
{
        positive sent = 0;

        while (sent < length)
        {
                positive seconds;
                positive nanoseconds;
                bipolar wrote;

                if (!network_deadline_left(deadline, address_of seconds,
                                           address_of nanoseconds))
                        return false;
                wrote = socket_send((b32)handle, data + sent, length - sent,
                                    MSG_DONTWAIT | MSG_NOSIGNAL, null, 0);
                if (wrote == NETWORK_INTERRUPTED)
                        continue;
                if (wrote == NETWORK_TRY_AGAIN)
                {
                        if (network_wait_writable_until(handle, deadline) <= 0)
                                return false;
                        continue;
                }
                if (wrote <= 0 || (positive)wrote > length - sent)
                        return false;
                sent += (positive)wrote;
        }

        return true;
}

/* A usual small write completes in one nonblocking send, with no clock or
   poll. Partial progress, backpressure and interruption start one absolute
   budget for the remainder; none renews it. A refused first send does not
   need a timer, and an empty span does not enter the kernel at all. */
static bool network_stream_send_all_for(
    bipolar handle, p8 address_to data, positive length,
    positive seconds, positive nanoseconds)
{
        network_deadline deadline;
        bipolar wrote;

        if (!length)
                return true;
        wrote = socket_send((b32)handle, data, length,
                            MSG_DONTWAIT | MSG_NOSIGNAL, null, 0);
        if (wrote > 0)
        {
                if ((positive)wrote > length)
                        return false;
                if ((positive)wrote == length)
                        return true;
                data += wrote;
                length -= (positive)wrote;
        }
        else if (wrote != NETWORK_INTERRUPTED && wrote != NETWORK_TRY_AGAIN)
                return false;
        if (!network_deadline_begin(address_of deadline, seconds, nanoseconds))
                return false;
        if (wrote == NETWORK_TRY_AGAIN &&
            network_wait_writable_until(handle, address_of deadline) <= 0)
                return false;
        return network_stream_send_all_until(handle, data, length,
                                              address_of deadline);
}

/*
        A stream which has stopped making progress must eventually give its
        caller back control.  This is installed once, before connect, and
        Linux then applies the send timeout to connect and writes and the
        receive timeout to reads, without every TLS and HTTP loop growing its
        own timer state.

        This is an idle timeout rather than a limit on the size or duration of
        a transfer: every successful system call starts a fresh wait.  Large
        downloads therefore remain possible, while a peer which accepts a
        connection and says nothing cannot hold wget -- or its synchronous
        bowl caller -- forever.
*/
static bool network_stream_timeout(bipolar handle, positive seconds,
                                   positive microseconds)
{
        timeval limit;

        if ((!seconds && !microseconds) || microseconds >= 1000000)
                return false;

        limit.tv_sec = (b64)seconds;
        limit.tv_usec = (b64)microseconds;

        return socket_option_set((b32)handle, SOL_SOCKET, SO_RCVTIMEO,
                                 address_of limit, sizeof limit) >= 0 &&
               socket_option_set((b32)handle, SOL_SOCKET, SO_SNDTIMEO,
                                 address_of limit, sizeof limit) >= 0;
}

#endif
