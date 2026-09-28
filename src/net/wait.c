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

static bipolar network_wait_until(
    bipolar handle, b16 events,
    const network_deadline address_to deadline)
{
        for (;;)
        {
                positive seconds;
                positive nanoseconds;
                timespec limit;
                system_poll_descriptor waited = {(b32)handle, events, 0};
                bipolar ready;

                if (!network_deadline_left(deadline, address_of seconds,
                                           address_of nanoseconds))
                        return 0;
                limit.tv_sec = (b64)seconds;
                limit.tv_nsec = (b64)nanoseconds;
                ready = system_poll_wait(address_of waited, 1,
                                         address_of limit, null);
                if (ready == NETWORK_INTERRUPTED)
                        continue;
                if (ready > 0 &&
                    (waited.returned & SYSTEM_POLL_INVALID))
                        return -9;
                return ready;
        }
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

/* What is already queued, taken without waiting; an interrupted receive
   is asked again, and NETWORK_TRY_AGAIN says nothing was there. */
static bipolar network_stream_read_now(bipolar handle, p8 address_to into,
                                       positive length)
{
        bipolar got;

        do
                got = socket_receive((b32)handle, into, length, MSG_DONTWAIT,
                                     null, 0);
        while (got == NETWORK_INTERRUPTED);
        return got;
}

/* Deadline-bound stream reads must not enter a blocking read merely because
   one byte was ready: a peer could then trickle the rest forever under the
   socket's renewing idle timeout. Poll the absolute budget and consume only
   bytes immediately available before recomputing what remains. The poll
   comes first: a caller here has usually just asked, and a receive before
   it found nothing nine times in ten, one system call more per answer. */
static bipolar network_stream_read_some_until(
    bipolar handle, p8 address_to into, positive length,
    const network_deadline address_to deadline)
{
        for (;;)
        {
                bipolar ready = network_wait_readable_until(handle, deadline);

                if (ready <= 0)
                        return ready < 0 ? ready : -1;

                bipolar got = network_stream_read_now(handle, into, length);

                if (got != NETWORK_TRY_AGAIN)
                        return got;
        }
}

/* A read in the middle of a stream, where bytes are usually queued already:
   they are taken without a poll, and the clock -- a system call here -- is
   read only once nothing is queued and a wait of that long begins. */
static bipolar network_stream_read_some_for(bipolar handle, p8 address_to into,
                                            positive length, positive seconds,
                                            positive nanoseconds)
{
        network_deadline deadline;
        bipolar got = network_stream_read_now(handle, into, length);

        if (got != NETWORK_TRY_AGAIN)
                return got;
        return network_deadline_begin(address_of deadline, seconds, nanoseconds)
                   ? network_stream_read_some_until(handle, into, length,
                                                    address_of deadline)
                   : -1;
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

static bool network_stream_send_all(bipolar handle, p8 address_to data,
                                    positive length)
{
        positive sent = 0;

        while (sent < length)
        {
                bipolar wrote = socket_send((b32)handle, data + sent,
                                            length - sent, MSG_NOSIGNAL,
                                            null, 0);

                if (wrote == NETWORK_INTERRUPTED)
                        continue;
                if (wrote <= 0 || (positive)wrote > length - sent)
                        return false;
                sent += (positive)wrote;
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
