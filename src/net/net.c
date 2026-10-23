/*
        The network stack: one dependency tree that was six files.

        rtnetlink, DNS, HTTP -- which is reached only through TLS, which is
        reached only through the hashes and curves it needs -- and DHCP.
        Nothing here is a command. src/sh/net.c has ip, host, fetch and wget, and
        test/checks.c builds this file with no shell around it at all, which
        is why it is its own unit and not folded in behind them.

        Each section keeps its own heading and its own include guard, so it
        still says what it is and what it refuses to know about its
        neighbours. wait.c stays beside this because src/sh/tools.c expands
        it too, and expands it first; anchors.inc, ct_logs.inc and
        suffixes.inc stay beside it because they are tables.

        Dawn Larsson - Apache-2.0 license
        github.com/dawnlarsson/moonwater

        www.dawning.dev
*/

#ifndef STANDARD_MODERN_C_NET
#define STANDARD_MODERN_C_NET

/* ---- netlink: rtnetlink: a link brought up, an address given, a route added ---- */

#ifndef STANDARD_MODERN_C_NET_NETLINK
#define STANDARD_MODERN_C_NET_NETLINK

#include "wait.c"

/*
        This is ordinary C on purpose.

        lib.c and everything it includes holds declarations and assembly
        and nothing else, which is checked. What follows is a cursor walking a
        buffer appending length-prefixed attributes -- function bodies, and
        with nothing in them a machine would do differently from any other
        machine. Putting it here rather than in the assembly graph is what
        lets it be written once instead of three times, and what lets the test
        include it directly and run the same bytes on all three targets.

        It depends on lib.c alone. No shell anything: the freestanding
        test builds this file without a shell in sight.

        Everything below is measured, not remembered. The offsets came out of
        offsetof on the build machine and the constants out of the headers
        beside them, and the test pins the ones a wrong guess would silently
        survive.

        The one thing worth saying twice: netlink's own fields are in the
        machine's byte order, not the wire's. Only what travels inside an
        attribute as an address is big endian. Reversing an nlmsg_len is the
        mistake this protocol invites, and it produces a message the kernel
        answers with EINVAL and no explanation.
*/

#define NETLINK_HEADER 16
#define NETLINK_DATAGRAM_MAX (16u * 1024u * 1024u)
#define NETLINK_TRANSACTION_SECONDS 10
//      A dump the kernel changed underneath, and the tries it gets.
#define NETLINK_DUMP_CHANGED (-11) /* EAGAIN, spelled out: the fuzz lifts have no errno names */
#define NETLINK_DUMP_TRIES 3

#define NLM_REQUEST 0x0001
#define NLM_ACK 0x0004
#define NLM_DUMP 0x0300
#define NLM_DUMP_INTERRUPTED 0x0010
#define NLM_REPLACE 0x0100
#define NLM_EXCLUSIVE 0x0200
#define NLM_CREATE 0x0400

#define NLMSG_IS_NOOP 1
#define NLMSG_IS_ERROR 2
#define NLMSG_IS_DONE 3

#define RTM_NEWLINK 16
#define RTM_DELLINK 17
#define RTM_GETLINK 18
#define RTM_NEWADDR 20
#define RTM_DELADDR 21
#define RTM_GETADDR 22
#define RTM_NEWROUTE 24
#define RTM_DELROUTE 25
#define RTM_GETROUTE 26

#define IFLA_ADDRESS 1
#define IFLA_IFNAME 3
#define IFLA_MTU 4
#define IFLA_WIRELESS 11
#define IFLA_LINKINFO 18
#define IFLA_INFO_KIND 1
#define IFLA_CARRIER_DOWN_COUNT 48
#define NLA_TYPE_MASK 0x3fff
#define NLA_F_NESTED 0x8000

#define NETLINK_PREFER_ANY 0
#define NETLINK_PREFER_WIRED 1
#define NETLINK_PREFER_WIFI 2
#define NETLINK_LINK_ETHER 1

#define IFA_ADDRESS 1
#define IFA_LOCAL 2
#define IFA_LABEL 3
#define IFA_CACHEINFO 6
#define IFA_PROTO 11
//      What the DHCP client stamps on an address it leases: the routing
//      protocol number iproute2 calls dhcp.
#define IFA_PROTO_DHCP 16

#define RTA_DST 1
#define RTA_OIF 4
#define RTA_GATEWAY 5

#define RT_TABLE_MAIN 254
#define RT_SCOPE_UNIVERSE 0
#define RT_SCOPE_LINK 253
#define RTPROT_BOOT 3
#define RTN_UNICAST 1

#define IFF_UP 1
#define IFF_BROADCAST 2
#define IFF_LOOPBACK 8
#define IFF_RUNNING 64
#define IFF_LOWER_UP 0x10000

#define IFNAME_SIZE 16

//      Every field here is the machine's own byte order.
typedef struct
{
        p32 length;
        p16 type;
        p16 flags;
        p32 sequence;
        p32 port;
} netlink_header;

//      ifinfomsg. The byte after the family is padding the kernel does read
//      back as zero, so it is named rather than left to whatever was there.
typedef struct
{
        p8 family;
        p8 padding;
        p16 kind;
        p32 index;
        p32 flags;
        p32 change;
} netlink_link;

//      ifaddrmsg
typedef struct
{
        p8 family;
        p8 prefix;
        p8 flags;
        p8 scope;
        p32 index;
} netlink_address;

//      rtmsg
typedef struct
{
        p8 family;
        p8 destination_bits;
        p8 source_bits;
        p8 tos;
        p8 table;
        p8 protocol;
        p8 scope;
        p8 kind;
        p32 flags;
} netlink_route;

//      rtattr
typedef struct
{
        p16 length;
        p16 type;
} netlink_attribute;

#define netlink_align(value) (((value) + 3) & ~(positive)3)

/*
        A message being built, and the room it is being built in.

        Nothing here has a fixed size either. A request is small, but a dump
        of the links on a machine with a hundred interfaces is not, and the
        size of the answer is not known until the kernel has said it: a read
        asking for MSG_TRUNC is told the true length even when it did not fit,
        which is what makes growing to it and reading again possible instead
        of guessing at a ceiling and calling the remainder someone else's
        problem.
*/
typedef struct
{
        p8 address_to bytes;
        positive room;
        positive used;
        bool failed;
} netlink_buffer;

static bool net_room(netlink_buffer address_to buffer, positive want)
{
        if (!array_store_reserve(buffer->bytes, buffer->room,
                                 buffer->used, want, 4096))
        {
                buffer->failed = true;
                return false;
        }

        return true;
}

static fn netlink_forget(netlink_buffer address_to buffer)
{
        array_store_release(buffer->bytes, buffer->room, buffer->used);
        buffer->failed = false;
}

//      A fresh request: the header, then the family-specific body after it.
static bool netlink_begin(netlink_buffer address_to buffer, p16 type, p16 flags,
                          p32 sequence, positive body)
{
        netlink_header address_to header;
        positive used = NETLINK_HEADER + netlink_align(body);

        buffer->used = 0;
        buffer->failed = false;

        /* nlmsg_len is 32 bits. Refuse an unrepresentable body before either
           the alignment or allocation arithmetic can wrap. */
        if (body > 0xffffffffu - NETLINK_HEADER)
        {
                buffer->failed = true;
                return false;
        }

        if (!net_room(buffer, used + 64))
        {
                netlink_forget(buffer);
                return false;
        }

        memory_fill(buffer->bytes, 0, used);

        header = (netlink_header address_to)buffer->bytes;
        header->length = (p32)(NETLINK_HEADER + body);
        header->type = type;
        header->flags = flags;
        header->sequence = sequence;
        header->port = 0;

        buffer->used = used;

        return true;
}

//      The body, for the caller to fill in by name rather than by offset.
static PURE COLD address_any netlink_body(netlink_buffer address_to buffer)
{
        return buffer->bytes + NETLINK_HEADER;
}

/*
        One attribute appended.

        The length written into the attribute is the true length, header
        included and padding excluded; what the cursor moves by is that length
        rounded up to four. Writing the rounded length into the attribute is
        the error that makes a message the kernel accepts and misreads.
*/
static COLD bool netlink_attribute_add(netlink_buffer address_to buffer, p16 type,
                                  address_any data, positive size)
{
        netlink_attribute address_to attribute;
        positive length;
        positive padded;

        if (buffer->failed)
                return false;

        /* rta_len is 16 bits, while nlmsg_len is 32. Truncating either one
           produces a message whose allocated bytes and advertised bytes no
           longer agree, so poison the request rather than emitting it. */
        if (size > 0xffffu - sizeof(netlink_attribute))
        {
                buffer->failed = true;
                return false;
        }

        /* The attribute starts at the aligned end of the message, as
           iproute2's addattr_l places it, whatever the body length was. The
           message's length is that end too: the header is aligned and every
           attribute before this one left the cursor and nlmsg_len equal. */
        length = sizeof(netlink_attribute) + size;
        padded = netlink_align(length);

        if (buffer->used > 0xffffffffu - padded)
        {
                buffer->failed = true;
                return false;
        }

        if (!net_room(buffer, buffer->used + padded))
                return false;

        /* net_room may move the allocation. Never carry an interior pointer
           across it. */
        attribute = (netlink_attribute address_to)(buffer->bytes + buffer->used);
        attribute->length = (p16)length;
        attribute->type = type;

        if (size)
                memory_copy(buffer->bytes + buffer->used +
                                sizeof(netlink_attribute),
                            data, size);

        //      The pad is sent, so it is zeroed rather than left as whatever
        //      the mapping held.
        if (padded > length)
                memory_fill(buffer->bytes + buffer->used + length,
                            0, padded - length);

        buffer->used += padded;
        ((netlink_header address_to)buffer->bytes)->length = (p32)buffer->used;

        return true;
}

/*
        A nested attribute, built around what it holds: the header goes in
        with no length, the attributes of its payload are added after it
        like any others, and the end gives it the length of all of them. The
        start is an offset, which stays good when adding grows the buffer;
        a request that failed on the way stays failed, and the end leaves it.
*/
static COLD positive netlink_nested_begin(netlink_buffer address_to buffer,
                                          p16 type)
{
        positive start = buffer->used;

        netlink_attribute_add(buffer, (p16)(type | NLA_F_NESTED), null, 0);
        return start;
}

static COLD fn netlink_nested_end(netlink_buffer address_to buffer,
                                  positive start)
{
        if (buffer->failed)
                return;
        if (buffer->used - start > 0xffffu)
        {
                buffer->failed = true;
                return;
        }

        ((netlink_attribute address_to)(buffer->bytes + start))->length =
            (p16)(buffer->used - start);
}

/*
        A netlink socket, bound.

        The bind is what gives this socket a port so replies can find it, and
        a zero port asks the kernel to pick one -- which matters, because a
        shell may have forked and two processes claiming the same port is the
        one netlink error that looks like a hang rather than a failure.

        EXT_ACK is asked for and not required. It costs one setsockopt and
        turns "-EINVAL" into a sentence naming the attribute that was wrong.
*/
#define RTNLGRP_LINK_MASK 1

static bipolar netlink_open_protocol(p32 protocol, p32 groups)
{
        bipolar handle = socket_new(
            AF_NETLINK, SOCK_DGRAM | SOCK_CLOEXEC, protocol);
        b32 want = 1;

        if (handle < 0)
                return handle;

        socket_address_netlink self = {.family = AF_NETLINK, .groups = groups};

        if (socket_bind((b32)handle, address_of self, sizeof self) < 0)
        {
                socket_close((b32)handle);
                return -1;
        }

        socket_option_set((b32)handle, SOL_NETLINK, NETLINK_EXT_ACK,
                          address_of want, sizeof want);

        return handle;
}

#define netlink_open_groups(groups) netlink_open_protocol(NETLINK_ROUTE, groups)

static bool netlink_source_is_kernel(
    const socket_address_netlink address_to source, p32 length)
{
        return length >= sizeof(*source) && source->family == AF_NETLINK &&
               source->port == 0;
}

/*
        One datagram, however big it turns out to be.

        The first read only looks: MSG_PEEK leaves the datagram where it is
        and MSG_TRUNC makes the kernel answer with its true length rather than
        the length that fitted. So the room is taken knowing the size, and the
        second read is the one that consumes it. Without MSG_TRUNC a message
        larger than the buffer arrives silently shortened, and a dump that
        loses its tail looks exactly like a dump that ended.
*/
static COLD bipolar netlink_receive_one(b32 handle,
                                   netlink_buffer address_to buffer,
                                   bool require_kernel)
{
        bipolar size;
        bipolar got;
        socket_address_netlink source;
        p32 source_length;

        source_length = sizeof source;
        memory_fill(address_of source, 0, sizeof source);

        /* With MSG_TRUNC a zero-length datagram read still returns its true
           length. Capture its sender at the same time: NETLINK_ROUTE replies
           and notifications are authoritative only from kernel port zero. */
        size = socket_receive(handle, null, 0,
                              MSG_PEEK | MSG_TRUNC | MSG_DONTWAIT,
                              address_of source, address_of source_length);

        if (size < 0)
                return size;

        if (require_kernel &&
            !netlink_source_is_kernel(address_of source, source_length))
        {
                /* Drop a userspace datagram without allocating according to
                   its claimed size. Both callers loop: a transaction waits
                   for its kernel reply on the next call, while an event
                   socket returns to poll. One discard per call also bounds
                   the work a userspace sender can impose before that poll. */
                got = socket_receive(handle, null, 0, MSG_DONTWAIT, 0, 0);
                buffer->used = 0;
                return got < 0 ? got : 0;
        }

        if (!size || (positive)size > NETLINK_DATAGRAM_MAX)
        {
                /* Left queued it would be the next peek too, and the socket
                   would refuse every reply behind it. */
                (void)socket_receive(handle, null, 0, MSG_DONTWAIT, 0, 0);
                buffer->used = 0;
                return -1;
        }

        if (!net_room(buffer, (positive)size + 64))
                return -1;

        source_length = sizeof source;
        memory_fill(address_of source, 0, sizeof source);
        got = socket_receive(handle, buffer->bytes, buffer->room, MSG_DONTWAIT,
                             address_of source, address_of source_length);

        if (got < 0)
                return got;

        if (got != size ||
            (require_kernel &&
             !netlink_source_is_kernel(address_of source, source_length)))
        {
                buffer->used = 0;
                return -1;
        }

        buffer->used = (positive)got;
        return got;
}

static COLD bipolar netlink_socket_identity(b32 handle,
                                            bool address_to require_kernel,
                                            p32 address_to local_port)
{
        socket_address_netlink self_address;
        p32 local_length = sizeof self_address;
        bipolar got;

        memory_fill(address_of self_address, 0, sizeof self_address);
        got = socket_name(handle, address_of self_address,
                          address_of local_length);
        if (got < 0 || local_length < sizeof(self_address.family))
                return got < 0 ? got : -1;

        address_to require_kernel = self_address.family == AF_NETLINK;
        /* A netlink port is meaningful only when getsockname returned the
           complete netlink address.  Other datagram families remain usable
           by the protocol's byte-level test harness, without weakening the
           kernel-source requirement on a real routing socket. */
        if (address_to require_kernel && local_length < sizeof self_address)
                return -1;
        if (local_port)
                address_to local_port = address_to require_kernel
                                            ? self_address.port : 0;

        return 0;
}

static COLD bipolar netlink_receive(b32 handle, netlink_buffer address_to buffer,
                               p32 address_to local_port)
{
        bool require_kernel;
        bipolar got = netlink_socket_identity(handle, address_of require_kernel,
                                              local_port);

        if (got < 0)
                return got;
        return netlink_receive_one(handle, buffer, require_kernel);
}

typedef bool (address_to netlink_visitor)(netlink_header address_to header,
                                          address_any context);

/* NLMSG_DONE and NLMSG_ERROR share a signed status payload convention.
   Multipart DONE may omit it; ERROR may not.  Optional extack data can follow
   the first word.  A positive value is neither success nor a Linux errno, so
   it is malformed rather than a successful transaction. */
static COLD bipolar netlink_status(netlink_header address_to header, bool empty_ok)
{
        b32 status;

        if (empty_ok && header->length == NETLINK_HEADER)
                return 0;
        if (header->length < NETLINK_HEADER + sizeof status)
                return -1;

        status = memory_load_unaligned(
            b32, (p8 address_to)header + NETLINK_HEADER);
        return status <= 0 ? status : -1;
}

/*
        The messages of one datagram in turn, each to visit until it says
        stop. False when one is cut short, a length under its own header or
        past what arrived, and what follows it was not read: that is a
        datagram the kernel did not make. A visitor that stops is no failure.
*/
static COLD bool netlink_each(netlink_buffer address_to reply,
                              netlink_visitor visit, address_any context)
{
        positive at = 0;

        while (at + NETLINK_HEADER <= reply->used)
        {
                netlink_header address_to header =
                    (netlink_header address_to)(reply->bytes + at);

                if (header->length < NETLINK_HEADER ||
                    header->length > reply->used - at)
                        return false;
                at += netlink_align(header->length);
                if (!visit(header, context))
                        return true;
        }
        return true;
}

typedef struct
{
        netlink_visitor visit;
        address_any context;
        p32 sequence;
        p32 port;
        bipolar status;
        bool matched;
        bool done;
        bool enough;
        bool interrupted;
} netlink_turn;

/* One message of a walk: answered to this request or dropped, and the
   status of the one that ends it. */
static COLD bool netlink_turn_take(netlink_header address_to header,
                                   address_any context)
{
        netlink_turn address_to turn = (netlink_turn address_to)context;

        /* Kernel unicast replies carry the destination socket's port id in
           nlmsg_pid.  Match it as well as the sequence so an unrelated
           kernel notification cannot complete this transaction. */
        if (header->port != turn->port || header->sequence != turn->sequence)
                return true;
        turn->matched = true;

        turn->interrupted |= (header->flags & NLM_DUMP_INTERRUPTED) != 0;

        if (header->type == NLMSG_IS_DONE)
        {
                bipolar status = netlink_status(header, true);

                turn->status = turn->interrupted && !status
                                   ? NETLINK_DUMP_CHANGED : status;
                turn->done = true;
                return false;
        }

        if (header->type == NLMSG_IS_ERROR)
        {
                turn->status = netlink_status(header, false);
                turn->done = true;
                return false;
        }

        if (!turn->enough && !turn->interrupted &&
            header->type != NLMSG_IS_NOOP && turn->visit &&
            !turn->visit(header, turn->context))
                turn->enough = true;

        return true;
}

/*
        A dump walked, one message at a time.

        A dump is many messages, possibly spread over several datagrams, and
        it ends with an NLMSG_DONE rather than by running out. Every message
        carries NLM_MULTI until that one.

        DUMP_INTR is the kernel saying the table changed underneath the walk,
        so what was collected may have missed an entry or counted one twice.
        Early boot is exactly when interfaces appear, so it is checked, and
        the answer to it is to start again rather than to hand back a list
        that is quietly wrong.

        A visitor that has seen enough stops being called, but the walk does
        not stop reading. A dump the kernel considers unfinished stays in
        progress on the socket, and the next dump asked for on it is refused
        with EBUSY -- which arrives long after the abandoned one, attached to
        an innocent request, and reads as though the second question were the
        problem. So the remaining messages are drawn and dropped, and the
        socket is handed back clean. An interrupted dump is drained the same
        way before it fails: it returned at its first marked message and left
        the rest of the dump on the socket.
*/
static bipolar netlink_walk(b32 handle, netlink_buffer address_to request,
                            p32 sequence, netlink_buffer address_to reply,
                            netlink_visitor visit, address_any context)
{
        netlink_turn turn = {.visit = visit,
                             .context = context,
                             .sequence = sequence};
        network_deadline deadline;
        positive discarded = 0;
        bipolar sent;
        bool require_kernel;

        if (request->failed)
                return -1;

        sent = socket_send(handle, request->bytes, request->used, 0, 0, 0);

        if (sent < 0)
                return sent;
        if (!network_deadline_begin(address_of deadline,
                                    NETLINK_TRANSACTION_SECONDS, 0))
                return -1;
        /* Sending may auto-bind an unbound netlink socket. Its local port
           and family are then stable for the entire multipart transaction. */
        sent = netlink_socket_identity(handle, address_of require_kernel,
                                       address_of turn.port);
        if (sent < 0)
                return sent;

        for (;;)
        {
                positive seconds;
                positive nanoseconds;
                bipolar got;

                if (!network_deadline_left(address_of deadline,
                                           address_of seconds,
                                           address_of nanoseconds))
                        return -1;

                turn.matched = false;
                got = netlink_receive_one(handle, reply, require_kernel);

                /* Readiness may vanish, or a signal may land before peek or
                   fill. Both receives are nonblocking; retry through the
                   same absolute deadline instead of stalling or failing a
                   routing operation on a transient condition. */
                if (got == NETWORK_INTERRUPTED)
                        continue;
                if (got == NETWORK_TRY_AGAIN)
                {
                        got = network_wait_readable_until(
                            handle, address_of deadline);
                        if (got <= 0)
                                return got < 0 ? got : -1;
                        continue;
                }
                if (got < 0)
                        return got;

                if (!netlink_each(reply, netlink_turn_take, address_of turn))
                        return -1;
                if (turn.done)
                        return turn.status;
                if (!turn.matched && discarded++ == NETWORK_DISCARD_MAX)
                        return -1;
        }
}

/*
        The tail every acknowledged request shares: sent, answered, and both
        buffers given back whatever the answer was.
*/
static COLD bipolar netlink_transact(b32 handle, netlink_buffer address_to request,
                                p32 sequence, netlink_visitor visit,
                                address_any context)
{
        netlink_buffer reply = {0};
        bipolar status = netlink_walk(handle, request, sequence, address_of reply,
                                      visit, context);

        netlink_forget(request);
        netlink_forget(address_of reply);

        return status;
}

static p32 netlink_sequence_next = 1;

/* Sequence zero conventionally labels unsolicited notifications. Keep every
   request/reply correlation in the nonzero namespace even after wraparound. */
static p32 netlink_sequence_take(void)
{
        p32 sequence = netlink_sequence_next;

        netlink_sequence_next++;
        if (!netlink_sequence_next)
                netlink_sequence_next = 1;

        return sequence;
}

/*
        Everything of one kind, walked.

        Links, addresses and routes all begin their body with a family byte,
        so one request shape asks for all three. The body size differs and the
        caller says which, because it is also what tells netlink_find where
        the attributes start.
*/
static bipolar netlink_dump(b32 handle, p16 type, positive body, p8 family,
                            netlink_visitor visit, address_any context)
{
        netlink_buffer request = {0};
        p32 sequence = netlink_sequence_take();
        if (!netlink_begin(address_of request, type, NLM_REQUEST | NLM_DUMP,
                           sequence, body))
                return -1;

        address_to(p8 address_to) netlink_body(address_of request) = family;

        return netlink_transact(handle, address_of request, sequence,
                                visit, context);
}

//      Asked again while the kernel says the table changed under it, with the
//      visitor's state put back to the copy taken before the first try.
static COLD bipolar netlink_dump_again(b32 handle, p16 type, positive body,
                                       p8 family, netlink_visitor visit,
                                       address_any context, address_any first,
                                       positive size)
{
        bipolar status = NETLINK_DUMP_CHANGED;

        for (positive tries = 0; tries < NETLINK_DUMP_TRIES &&
                                 status == NETLINK_DUMP_CHANGED; tries++)
        {
                memory_copy(context, first, size);
                status = netlink_dump(handle, type, body, family, visit, context);
        }
        return status;
}

/*
        The attributes of a message, found by type.

        Attributes are a length-prefixed chain: a header of four bytes, then
        that many bytes of payload, then the next one at the aligned end of
        this one. A length shorter than its own header would walk backwards
        forever, which is the shape a corrupt or hostile message takes, so it
        ends the walk rather than continuing it.

        Two things hold such a chain and they differ only in where it starts.
        A message's own attributes begin after the fixed body its family
        defines, whose length only the caller knows; a nested attribute's
        payload is a chain all by itself. So the span is the routine and the
        message form is the address of its first attribute, worked out once.
*/
typedef bool (address_to netlink_attribute_visitor)(p16 type,
                                                    p8 address_to value,
                                                    positive size,
                                                    address_any context);

/* The attributes of a chain in turn, each to visit as its type (without the
   flags), its payload and the payload's length, until it says stop. False
   when the chain is cut short: what follows an attribute whose length is
   under its own header or past the end was not read. */
static COLD bool netlink_attributes(p8 address_to bytes, positive length,
                                    netlink_attribute_visitor visit,
                                    address_any context)
{
        byte_reader reader = byte_reader_open(bytes, length);

        while (byte_reader_left(&reader) >= sizeof(netlink_attribute))
        {
                positive whole = byte_reader_u16le(&reader);
                p16 kind = byte_reader_u16le(&reader);
                byte_reader value;

                if (whole < sizeof(netlink_attribute))
                        return false;
                value = byte_reader_window(&reader,
                                           whole - sizeof(netlink_attribute));
                if (!byte_reader_ok(&reader))
                        return false;
                if (!visit((p16)(kind & NLA_TYPE_MASK),
                           (p8 address_to)byte_reader_here(&value),
                           byte_reader_left(&value), context))
                        return true;
                //      The next starts on a four byte boundary, which the
                //      last one need not reach.
                (void)byte_reader_skip(&reader,
                                       min(netlink_align(whole) - whole,
                                           byte_reader_left(&reader)));
        }

        return true;
}

typedef struct
{
        p16 type;
        address_any value;
        positive size;
} netlink_finding;

static COLD bool netlink_finding_take(p16 type, p8 address_to value,
                                      positive size, address_any context)
{
        netlink_finding address_to finding = (netlink_finding address_to)context;

        if (type != finding->type)
                return true;
        finding->value = (address_any)value;
        finding->size = size;
        return false;
}

static COLD address_any netlink_find_span(p8 address_to bytes, positive length,
                                     p16 type, positive address_to size)
{
        netlink_finding finding = {.type = type};

        netlink_attributes(bytes, length, netlink_finding_take,
                           address_of finding);
        if (finding.value && size)
                address_to size = finding.size;
        return finding.value;
}

static COLD address_any netlink_find(netlink_header address_to header, positive body,
                                p16 type, positive address_to size)
{
        positive at = NETLINK_HEADER + netlink_align(body);

        //      A message too short to hold its own body carries no
        //      attributes; subtracting first would wrap the span's length.
        if (header->length < at)
                return null;

        return netlink_find_span((p8 address_to)header + at,
                                 header->length - at, type, size);
}

/* The fixed body a family puts after the header. Visitors are also used
   directly by tests and by callers parsing multicast frames, so none is
   handed a body pointer until that body is present in full. */
static COLD address_any netlink_message_body(netlink_header address_to header,
                                             positive body)
{
        return header->length < NETLINK_HEADER + body
                   ? null : (p8 address_to)header + NETLINK_HEADER;
}

/*
        What the four things actually are, on the wire.

        Each is a request built and acknowledged, and every one of them has a
        detail that is only obvious after the kernel has refused it once:

        A link's flags are set with a change mask, and a change mask of zero
        does not mean "change nothing". The kernel reads it as ~0 -- "this is
        the whole flag word now" -- so a request meaning to raise IFF_UP and
        leaving change at zero silently clears everything else the interface
        had. It is a required argument here for that reason, not an optional
        one with a convenient default.

        An IPv4 address needs IFA_LOCAL. Sending only IFA_ADDRESS, which is
        the attribute whose name suggests it holds the address, fails with
        EINVAL and the ext_ack sentence "ipv4: Local address is not supplied".
        Both are sent, which is what iproute2 does, and which is also right
        for a point-to-point link where the two genuinely differ.

        A route needs NLM_CREATE. Without it the kernel is being asked to
        change a route that is not there yet and answers ENOENT.
*/

typedef struct
{
        string_address wanted;
        p32 index;
        p32 flags;
        p8 prefer;
        bool found;
        bool skip_loopback;
        bool has_hardware;
        bool wireless;
        //      Wired links are not candidates: `moonwater wired off`.
        bool skip_wired;
        p8 skip_count;
        p32 skip[8];
        //      The kernel's count of carrier losses, where it says one.
        bool carrier_counted;
        p32 carrier_downs;
        p8 name[IFNAME_SIZE];
        p8 hardware[6];
} netlink_search;

static inline INLINE string_address netlink_link_name(
    netlink_header address_to header, netlink_link address_to address_to link)
{
        positive length = 0;
        string_address name;

        address_to link = netlink_message_body(header, sizeof(netlink_link));
        if (!address_to link)
                return null;
        name = (string_address)netlink_find(
            header, sizeof(netlink_link), IFLA_IFNAME, address_of length);
        return name && length && memory_first_of(name, 0, length) ? name : null;
}

/* IFLA_CARRIER_DOWN_COUNT, which counts every loss of carrier: a cable
   pulled and put back -- into another network, perhaps -- leaves it higher
   even after both events have passed. */
static bool netlink_link_carrier_downs(netlink_header address_to header,
                                       p32 address_to downs)
{
        positive width = 0;
        p8 address_to count = (p8 address_to)netlink_find(
            header, sizeof(netlink_link), IFLA_CARRIER_DOWN_COUNT,
            address_of width);

        if (!count || width != 4)
                return false;
        address_to downs = memory_load_unaligned(p32, count);
        return true;
}

static bool netlink_link_is_wireless(netlink_header address_to header,
                                     string_address name)
{
        positive size = 0;
        p8 address_to info = (p8 address_to)netlink_find(
            header, sizeof(netlink_link), IFLA_LINKINFO, address_of size);
        p8 address_to kind;
        positive kind_length = 0;

        if (info)
        {
                kind = (p8 address_to)netlink_find_span(info, size, IFLA_INFO_KIND,
                                                        address_of kind_length);
                if (kind && kind_length >= 4 && !memory_compare(kind, "wlan", 4))
                        return true;
        }

        if (netlink_find(header, sizeof(netlink_link), IFLA_WIRELESS, null))
                return true;
        return string_has_prefix(name, "wl") || string_has_prefix(name, "wifi");
}

static bool netlink_link_seen(netlink_header address_to header, address_any context)
{
        netlink_search address_to search = (netlink_search address_to)context;
        netlink_link address_to link;
        string_address name = netlink_link_name(header, address_of link);
        bool wireless = false;

        if (!name)
                return true;

        if (search->skip_loopback)
        {
                /*
                        Only a link a lease can be had on, and not one of the
                        wifi links the caller named in skip -- an access point
                        or anything else that is no station. A radio's monitor
                        (hwsim0, ARPHRD_IEEE80211_RADIOTAP) was taken, and DHCP
                        waited out its whole schedule on it before the station
                        was looked at.
                */
                if ((link->flags & IFF_LOOPBACK) || link->kind != NETLINK_LINK_ETHER)
                        return true;
                for (positive at = 0; at < search->skip_count && at < 8; at++)
                        if (search->skip[at] == link->index)
                                return true;

                /*
                        Not the first one found -- the best one.

                        A machine with wired and wireless has both, and the
                        one worth configuring is the one with something
                        plugged into it. IFF_RUNNING is the kernel saying the
                        link has carrier, so a candidate that has it beats one
                        that does not, whatever order the dump arrived in, and
                        the walk goes all the way to the end rather than
                        stopping at whatever came first.

                        When both have carrier, prefer picks wired or wifi.
                        moonwater priority internet writes that choice;
                        missing it means wired.
                */
                wireless = netlink_link_is_wireless(header, name);
                if (search->skip_wired && !wireless)
                        return true;
                if (search->found)
                {
                        bool had = (search->flags & IFF_RUNNING) != 0;
                        bool has = (link->flags & IFF_RUNNING) != 0;
                        bool better = had != has ? has
                            : search->prefer == NETLINK_PREFER_WIFI
                                ? wireless && !search->wireless
                            : search->prefer == NETLINK_PREFER_WIRED
                                ? !wireless && search->wireless
                                : false;

                        if (!better)
                                return true;
                }
        }
        else if (search->found || !string_equals(name, search->wanted))
        {
                return true;
        }
        else
                wireless = netlink_link_is_wireless(header, name);

        search->index = link->index;
        search->flags = link->flags;
        search->found = true;
        search->wireless = wireless;
        search->has_hardware = false;
        string_copy_max_end(search->name, name, IFNAME_SIZE - 1);

        //      The hardware address, which DHCP has to put in the packet and
        //      which is the only way a reply finds its way back before there
        //      is an address to send it to.
        {
                positive width = 0;
                p8 address_to found_hardware = (p8 address_to)netlink_find(
                    header, sizeof(netlink_link), IFLA_ADDRESS, address_of width);

                if (found_hardware && width == 6)
                {
                        memory_copy(search->hardware, found_hardware, 6);
                        search->has_hardware = true;
                }
        }
        search->carrier_counted = netlink_link_carrier_downs(
            header, address_of search->carrier_downs);

        return search->skip_loopback;
}

/*
        The index of a link, by name, or of the first that is not loopback.

        A single-shot RTM_GETLINK can be asked about a name directly, but that
        is no use for discovery: the whole question at boot is what the
        interface is called. So this is always the dump, and the name is
        matched while walking it. /sys would answer the same question, but the
        boot image mounts only devpts, so there is no /sys/class/net to read.
*/
static bipolar netlink_link_find(b32 handle, netlink_search address_to search)
{
        netlink_search first = address_to search;
        bipolar status;

        first.found = first.has_hardware = false;
        status = netlink_dump_again(handle, RTM_GETLINK, sizeof(netlink_link),
                                    AF_UNSPEC, netlink_link_seen, search,
                                    address_of first, sizeof first);
        if (status < 0)
                return status;

        return search->found ? 0 : -19;
}

//      IFF_UP raised or dropped, and every other flag left exactly as it was
//      found: the change mask names the one bit, or the kernel would read a
//      zero as the whole word.
static bipolar netlink_link_flag_up(b32 handle, p32 index, bool up)
{
        netlink_buffer request = {0};
        netlink_link address_to body;
        p32 sequence = netlink_sequence_take();

        if (!netlink_begin(address_of request, RTM_NEWLINK,
                           NLM_REQUEST | NLM_ACK, sequence, sizeof(netlink_link)))
                return -1;

        body = (netlink_link address_to)netlink_body(address_of request);
        body->family = AF_UNSPEC;
        body->index = index;
        body->flags = up ? IFF_UP : 0;
        body->change = IFF_UP;

        return netlink_transact(handle, address_of request, sequence,
                                null, null);
}

#define netlink_link_up(handle, index) netlink_link_flag_up(handle, index, true)

/*
        The wired links: Ethernet framing, not loopback and not a wireless
        station. Sixteen is more than a machine has; one past that is not
        listed.
*/
#define NETLINK_WIRED_MOST 16

typedef struct
{
        p32 index;
        p32 flags;
        p8 name[IFNAME_SIZE];
} netlink_wired_link;

typedef struct
{
        netlink_wired_link link[NETLINK_WIRED_MOST];
        positive count;
} netlink_wired;

static bool netlink_wired_seen(netlink_header address_to header, address_any context)
{
        netlink_wired address_to wired = (netlink_wired address_to)context;
        netlink_link address_to link;
        string_address name = netlink_link_name(header, address_of link);

        if (!name || (link->flags & IFF_LOOPBACK) || link->kind != NETLINK_LINK_ETHER ||
            netlink_link_is_wireless(header, name) || wired->count == NETLINK_WIRED_MOST)
                return true;

        wired->link[wired->count].index = link->index;
        wired->link[wired->count].flags = link->flags;
        string_copy_max_end(wired->link[wired->count].name, name, IFNAME_SIZE - 1);
        wired->count++;
        return true;
}

static bipolar netlink_wired_list(b32 handle, netlink_wired address_to wired)
{
        netlink_wired first = {.count = 0};

        return netlink_dump_again(handle, RTM_GETLINK, sizeof(netlink_link),
                                  AF_UNSPEC, netlink_wired_seen, wired,
                                  address_of first, sizeof first);
}

/*
        An address given to a link.

        REPLACE rather than EXCLUSIVE, so running the same command twice is
        the same as running it once. A boot that is retried should not fail
        because the first attempt succeeded.

        A leased address carries the lease: seconds, when not zero, are its
        valid and preferred lifetimes -- so the kernel removes it at expiry
        even with no client left to -- and IFA_PROTO_DHCP says whose it is,
        which is how a restarted client knows the address it inherited.
*/
static bipolar netlink_address_change(b32 handle, p16 type, p16 flags,
                                      p32 index, p32 host, p8 prefix,
                                      p32 seconds)
{
        netlink_buffer request = {0};
        netlink_address address_to body;
        p32 sequence = netlink_sequence_take();
        p32 wire = network_order_32(host);

        if (!netlink_begin(address_of request, type, flags, sequence,
                           sizeof(netlink_address)))
                return -1;

        body = (netlink_address address_to)netlink_body(address_of request);
        body->family = AF_INET;
        body->prefix = prefix;
        body->scope = RT_SCOPE_UNIVERSE;
        body->index = index;

        netlink_attribute_add(address_of request, IFA_LOCAL, address_of wire, 4);
        netlink_attribute_add(address_of request, IFA_ADDRESS, address_of wire, 4);
        if (seconds)
        {
                //      ifa_cacheinfo: preferred, valid, and two stamps the
                //      kernel keeps for itself.
                p32 lifetimes[4] = {seconds, seconds, 0, 0};
                p8 leased = IFA_PROTO_DHCP;

                netlink_attribute_add(address_of request, IFA_CACHEINFO,
                                      lifetimes, sizeof lifetimes);
                netlink_attribute_add(address_of request, IFA_PROTO,
                                      address_of leased, 1);
        }

        return netlink_transact(handle, address_of request, sequence,
                                null, null);
}

/* A leased address, created exclusively or put in place of the one there,
   its lifetimes the lease's. Automatic configuration must acquire a kernel
   object before it can later claim the right to remove it, so it asks for
   EXCLUSIVE: that tells a newly installed address from an identical one an
   operator or another network manager already owns, which REPLACE cannot. */
#define netlink_address_lease(handle, index, host, prefix, seconds, exclusive) \
        netlink_address_change(                                                \
            handle, RTM_NEWADDR,                                                \
            NLM_REQUEST | NLM_ACK | NLM_CREATE |                                \
                ((exclusive) ? NLM_EXCLUSIVE : NLM_REPLACE),                    \
            index, host, prefix, seconds)

#define netlink_address_add(handle, index, host, prefix) \
        netlink_address_lease(handle, index, host, prefix, 0, false)

#define netlink_address_delete(handle, index, host, prefix) netlink_address_change( \
        handle, RTM_DELADDR, NLM_REQUEST | NLM_ACK, index, host, prefix, 0)

/*
        A route, with a destination of no bits at all being the default one.

        The gateway has to be reachable already, which for a default route
        means the address added above has to cover it. The kernel says ENETUNREACH
        when it does not, which reads as a network problem and is really an
        ordering one -- except for a gateway said to be on the link
        (NETLINK_ROUTE_ONLINK), which the kernel takes without an address
        that covers it: a /32 lease and its router.
*/
#define NETLINK_ROUTE_ONLINK 4 // RTNH_F_ONLINK

static bipolar netlink_route_change(b32 handle, p16 type, p16 flags,
                                    p32 destination, p8 bits, p32 gateway,
                                    p32 index, p32 route_flags)
{
        netlink_buffer request = {0};
        netlink_route address_to body;
        p32 sequence = netlink_sequence_take();
        p32 wire_gateway = network_order_32(gateway);
        p32 wire_destination = network_order_32(destination);

        if (!netlink_begin(address_of request, type, flags, sequence,
                           sizeof(netlink_route)))
                return -1;

        body = (netlink_route address_to)netlink_body(address_of request);
        body->family = AF_INET;
        body->destination_bits = bits;
        body->table = RT_TABLE_MAIN;
        body->protocol = RTPROT_BOOT;
        body->scope = gateway ? RT_SCOPE_UNIVERSE : RT_SCOPE_LINK;
        body->kind = RTN_UNICAST;
        body->flags = route_flags;

        if (bits)
                netlink_attribute_add(address_of request, RTA_DST,
                                      address_of wire_destination, 4);

        if (gateway)
                netlink_attribute_add(address_of request, RTA_GATEWAY,
                                      address_of wire_gateway, 4);

        if (index)
                netlink_attribute_add(address_of request, RTA_OIF,
                                      address_of index, 4);

        return netlink_transact(handle, address_of request, sequence,
                                null, null);
}

//      REPLACE alongside CREATE, for the same reason the address add has
//      it: adding the route a second time is what happens when a link comes
//      back, and it should be the same as having added it once rather than
//      EEXIST.
#define netlink_route_add(handle, destination, bits, gateway, index) netlink_route_change( \
        handle, RTM_NEWROUTE, NLM_REQUEST | NLM_ACK | NLM_CREATE | NLM_REPLACE,            \
        destination, bits, gateway, index, 0)

/* An existing route is state, not spare capacity.  Initial DHCP acquisition
   asks route_change for EXCLUSIVE so a pre-existing default route is reported
   as a conflict and remains byte-for-byte kernel state owned by whoever
   installed it. */
#define netlink_route_delete(handle, destination, bits, gateway, index) netlink_route_change( \
        handle, RTM_DELROUTE, NLM_REQUEST | NLM_ACK, destination, bits, gateway, index, 0)

#define NETLINK_LEASES_MAX 8

/* The addresses on one link stamped as this client's leases: whether one
   is the address asked about, and the others, up to a handful. */
typedef struct
{
        p32 index;
        p32 host;
        p8 prefix;
        bool leased;
        positive others;
        p32 other_host[NETLINK_LEASES_MAX];
        p8 other_prefix[NETLINK_LEASES_MAX];
} netlink_lease_search;

static COLD bool netlink_lease_seen(netlink_header address_to header,
                                    address_any context)
{
        netlink_lease_search address_to search =
            (netlink_lease_search address_to)context;
        netlink_address address_to body =
            netlink_message_body(header, sizeof(netlink_address));
        positive size = 0;
        p8 address_to named;
        p8 address_to protocol;
        p32 host;

        if (!body || body->family != AF_INET || body->index != search->index)
                return true;
        protocol = (p8 address_to)netlink_find(header, sizeof(netlink_address),
                                               IFA_PROTO, address_of size);
        if (!protocol || size != 1 || protocol[0] != IFA_PROTO_DHCP)
                return true;
        named = (p8 address_to)netlink_find(header, sizeof(netlink_address),
                                            IFA_LOCAL, address_of size);
        if (!named || size != 4)
                return true;
        host = network_order_32(memory_load_unaligned(p32, named));
        if (host == search->host && body->prefix == search->prefix)
                search->leased = true;
        else if (search->others < NETLINK_LEASES_MAX)
        {
                search->other_host[search->others] = host;
                search->other_prefix[search->others++] = body->prefix;
        }
        return true;
}

/* Which addresses on a link are leases this client put there -- what a
   restarted watcher inherits from the one before it -- as against anybody
   else's, an operator's static address included. */
static COLD bipolar netlink_leases_on(b32 handle,
                                      netlink_lease_search address_to search)
{
        netlink_lease_search first = address_to search;

        return netlink_dump_again(handle, RTM_GETADDR, sizeof(netlink_address),
                                  AF_INET, netlink_lease_seen, search,
                                  address_of first, sizeof first);
}

#endif // STANDARD_MODERN_C_NET_NETLINK
/* ---- dns: A resolver: a name, and the address behind it ---- */

#ifndef STANDARD_MODERN_C_NET_DNS
#define STANDARD_MODERN_C_NET_DNS

#include "wait.c"

/*
        The smallest resolver that is not wrong.

        DNS is a twelve byte header, a question written as length-prefixed
        labels, and answers in the same shape with a fixed ten byte tail. All
        of it is big endian, unlike netlink next door, which is the sort of
        neighbouring difference that produces a working program and a wrong
        one from the same afternoon.

        Three things a minimal client is tempted to skip and must not:

        Compression. Almost every real answer points into itself rather than
        spelling a name twice -- two octets whose top bits are both set, the
        remaining fourteen an offset from the start of the message. A parser
        that does not follow those reads garbage on nearly every reply. Every
        pointer lowers the traversal ceiling, so labels cannot lead back into
        a pointer already followed.

        Truncation. A reply too big for the buffer arrives shortened, and a
        shortened answer section is indistinguishable from a short one. The
        read asks for MSG_TRUNC so the kernel says the true size, and a reply
        bigger than what was read is refused rather than parsed.

        The question coming back. A reply is matched on its transaction id,
        and an id is sixteen bits, so it is not much of a check on its own.
        The question section is compared against the one that was asked, byte
        for byte, before a single answer is believed.

        rcode zero does not mean there is an address. A name that exists with
        no A record answers NOERROR with no answers at all, which is a
        different thing from the name not existing, and a resolver that
        conflates them reports the wrong reason forever.
*/

#define DNS_PORT 53
#define DNS_HEADER 12

#define DNS_TYPE_A 1
#define DNS_TYPE_CNAME 5
#define DNS_CLASS_IN 1

#define DNS_FLAG_RESPONSE 0x8000
#define DNS_OPCODE_MASK 0x7800
#define DNS_FLAG_TRUNCATED 0x0200
#define DNS_FLAG_RECURSE 0x0100
#define DNS_FLAG_RESERVED 0x0040
#define DNS_CODE_MASK 0x000f

#define DNS_MAX_MESSAGE 4096
#define DNS_CNAME_HOPS 16
/* Shared by every name in one section, or across all CNAME selection passes.
   A 4 KiB reply cannot buy 127 decompression jumps per record per pass. The
   three framing sections and answer selection together spend at most 16,384
   pointer jumps; otherwise-valid excessive compression is refused. */
#define DNS_WORK_POINTERS DNS_MAX_MESSAGE
//      A wire name is at most 255 bytes, so at most 127 labels, and no
//      encoder needs more jumps than a name has labels.
#define DNS_NAME_MAX 255
#define DNS_POINTER_HOPS 127
//      Everything the caller may want to tell apart.
#define DNS_OK 0
#define DNS_NO_SERVER (-1)
#define DNS_NO_REPLY (-2)
#define DNS_MALFORMED (-3)
#define DNS_NO_SUCH_NAME (-4)
#define DNS_NO_ADDRESS (-5)
#define DNS_REFUSED (-6)
#define DNS_NO_RANDOM (-7)

/* Internal result: a validated UDP response asks for the same transaction to
   continue over TCP.  It is never returned to a caller. */
#define DNS_TRY_TCP (-8)
//      Inside one query, not answers: the server sent the question back in
//      lower case (it does not keep the case the query was written in), or
//      answered the EDNS0 option with FORMERR or NOTIMP.
#define DNS_CASE_FOLDED (-9)
#define DNS_FORMAT_ERROR (-10)
#define DNS_EDNS_SILENT (-11)

/*
        The name, as labels.

        "dawning.dev" becomes 7 d a w n i n g 3 d e v 0. A label may not
        exceed sixty three bytes -- the two high bits of a length are what
        marks a compression pointer, so a longer one would be unreadable
        rather than merely unusual -- and the whole name may not exceed 255.
*/
static COLD bipolar dns_write_name(p8 address_to into, positive room, string_address name)
{
        positive used = 0;
        positive mark;
        positive length;

        while (string_get(name))
        {
                string_address dot = string_first_of_or_end(name, '.');

                mark = used++;
                length = (positive)(dot - name);

                if (used >= room || !length || length > 63 ||
                    length > room - used)
                        return DNS_MALFORMED;

                memory_copy_apart(into + used, name, length);
                into[mark] = (p8)length;
                used += length;
                name = dot + string_is(dot, '.');
        }

        if (used + 1 > room || used + 1 > DNS_NAME_MAX)
                return DNS_MALFORMED;

        into[used++] = 0;

        return (bipolar)used;
}

/*
        A name expanded, following pointers but never in a circle.

        The labels are copied uncompressed into the caller's bytes, and ended
        says where the name ENDS in the message, which for a compressed name
        is two bytes on from where it began however far away the pointer led.
        Each jump lowers the ceiling to its own offset: merely moving
        backwards is insufficient because labels can step forwards to that
        same pointer again. The ceiling alone still let a chain of pointers,
        each one two bytes before the last, cost two thousand jumps a name,
        and a reply of names at the end of such a chain four milliseconds, so
        the jumps are counted too. A name longer than 255 bytes is refused
        whatever the room (RFC 1035 3.1). The spelling is kept as sent; DNS
        names compare without regard to ASCII case. A caller that reads many
        names out of one message may pass pointers, the compression jumps they
        may take between them. A successfully expanded name subtracts its
        jumps together; a name that would take one more is refused. Ordinary
        uncompressed labels do no shared-budget work.
*/
static COLD bipolar dns_copy_name(p8 address_to message, positive size,
                             positive at, p8 address_to into, positive room,
                             positive address_to ended,
                             positive address_to pointers)
{
        byte_reader reader = byte_reader_open(message, size);
        byte_store name = {into, min(room, (positive)DNS_NAME_MAX), 0};
        positive ceiling = size;
        positive jumps = 0;

        address_to ended = 0;
        //      A name past the end fails at its first byte, below.
        (void)byte_reader_skip(&reader, at);

        for (;;)
        {
                positive here = ceiling - byte_reader_left(&reader);
                p8 length = byte_reader_u8(&reader);
                const p8 address_to label;

                if (!byte_reader_ok(&reader))
                        return DNS_MALFORMED;

                if ((length & 0xc0) == 0xc0)
                {
                        positive target = (positive)(length & 0x3f) << 8 |
                                          byte_reader_u8(&reader);

                        if (!byte_reader_ok(&reader))
                                return DNS_MALFORMED;

                        if (!address_to ended)
                                address_to ended = here + 2;

                        if (target >= here || ++jumps > DNS_POINTER_HOPS)
                                return DNS_MALFORMED;

                        //      The name goes on at the target, which may
                        //      read nothing at or past the pointer.
                        ceiling = here;
                        reader = byte_reader_open(message, ceiling);
                        (void)byte_reader_skip(&reader, target);
                        continue;
                }

                label = byte_reader_take(&reader, length);
                if (length & 0xc0 || !label ||
                    !byte_store_append_exact(address_of name, address_of length,
                                             1) ||
                    !byte_store_append_exact(address_of name, (p8 address_to)label,
                                             length))
                        return DNS_MALFORMED;

                if (!length)
                {
                        if (pointers)
                        {
                                if (address_to pointers < jumps)
                                        return DNS_MALFORMED;
                                address_to pointers -= jumps;
                        }
                        if (!address_to ended)
                                address_to ended = ceiling -
                                                   byte_reader_left(&reader);
                        return (bipolar)name.used;
                }
        }
}

//      Where a name ends, for a caller with no use for its spelling.
static COLD bipolar dns_skip_name(p8 address_to message, positive size, positive at)
{
        p8 name[256];
        positive ended;

        return dns_copy_name(message, size, at, name, sizeof name,
                             address_of ended, null) < 0
                   ? DNS_MALFORMED : (bipolar)ended;
}

//      A reader on the message, at a name or a record that a name's end
//      pointed to: failed and empty when that is past the message.
static COLD byte_reader dns_message_at(const p8 address_to message,
                                       positive size, positive at)
{
        byte_reader reader = byte_reader_open(message, size);

        (void)byte_reader_skip(&reader, at);
        return reader;
}

/* Find an address only along the name that was asked for and the CNAME chain
   rooted at it.  An answer packet may legally put the terminal A before its
   CNAME, so each bounded pass considers one link rather than trusting record
   order.  Unrelated A records are glue or attacker-controlled distractions,
   never an answer to the question. */
static COLD bipolar dns_answer_address(p8 address_to message, positive size,
                                  positive records_at, p16 answers,
                                  positive question_at,
                                  p32 address_to found)
{
        p8 wanted[256];
        p8 alias[256];
        positive ended;
        positive pointers = DNS_WORK_POINTERS;
        bipolar wanted_length = dns_copy_name(message, size, question_at,
                                              wanted, sizeof wanted,
                                              address_of ended, address_of pointers);

        if (wanted_length < 0)
                return DNS_MALFORMED;

        /* A cycle needs no more links than there are answer records to show
           itself.  The extra pass is the one that can find the terminal A.
           Every pass walks every record, so the passes are the quadratic a
           hostile reply buys: no chain goes past DNS_CNAME_HOPS links, where
           systemd-resolved stops and short of which recursors give up. */
        for (positive hop = 0; hop <= (positive)answers &&
                               hop <= DNS_CNAME_HOPS; hop++)
        {
                positive at = records_at;
                bool has_alias = false;
                bool has_address = false;
                bipolar alias_length = 0;
                p32 address = 0;

                for (positive record = 0; record < answers; record++)
                {
                        p8 owner[256];
                        //      at moves on to where the owner name ended.
                        bipolar owner_length = dns_copy_name(
                            message, size, at, owner, sizeof owner, address_of at,
                            address_of pointers);
                        byte_reader tail = dns_message_at(message, size, at);
                        byte_reader data;
                        positive data_length;
                        p16 kind;
                        p16 class;
                        bool is_wanted;

                        //      The type and class, the time to live, and the
                        //      data behind its length.
                        kind = byte_reader_u16(&tail);
                        class = byte_reader_u16(&tail);
                        (void)byte_reader_skip(&tail, 4);
                        data = byte_reader_vector16(&tail);
                        if (owner_length < 0 || !byte_reader_ok(&tail))
                                return DNS_MALFORMED;
                        data_length = byte_reader_left(&data);
                        at = size - byte_reader_left(&tail) - data_length;

                        is_wanted = owner_length == wanted_length &&
                                    !memory_compare_ascii_case(
                                        owner, wanted, (positive)wanted_length);

                        if (class == DNS_CLASS_IN && is_wanted &&
                            kind == DNS_TYPE_A)
                        {
                                byte_reader value = data;

                                if (data_length != 4)
                                        return DNS_MALFORMED;
                                if (!has_address)
                                        address = byte_reader_u32(&value);
                                has_address = true;
                        }
                        else if (class == DNS_CLASS_IN && is_wanted &&
                                 kind == DNS_TYPE_CNAME)
                        {
                                positive target_end;

                                if (has_alias)
                                        return DNS_MALFORMED;

                                alias_length = dns_copy_name(
                                    message, size, at, alias, sizeof alias,
                                    address_of target_end, address_of pointers);

                                if (alias_length < 0 ||
                                    target_end != at + data_length)
                                        return DNS_MALFORMED;
                                has_alias = true;
                        }

                        at += data_length;
                }

                /* CNAME and other data at one owner are mutually exclusive.
                   Treating a packet containing both as an address choice
                   would make its meaning depend on record order. */
                if (has_alias && has_address)
                        return DNS_MALFORMED;

                if (has_address)
                {
                        if (found)
                                *found = address;
                        return DNS_OK;
                }

                if (!has_alias)
                        return DNS_NO_ADDRESS;

                if (alias_length == wanted_length &&
                    !memory_compare_ascii_case(alias, wanted,
                                               (positive)wanted_length))
                        return DNS_MALFORMED;

                memory_copy(wanted, alias, (positive)alias_length);
                wanted_length = alias_length;
        }

        return DNS_MALFORMED;
}

/* The transaction id and exact echoed question are the reply identity.  UDP
   uses this predicate to discard raced junk within the original deadline;
   TCP has one framed reply and treats an identity mismatch as malformed.
   Folded compares the question under ASCII case, which is how a server that
   does not keep the case it was sent echoes it. */
static COLD bool dns_reply_identity(
    p8 address_to reply, positive size, p16 id,
    p8 address_to request, positive question_length, bool folded)
{
        byte_reader reader = byte_reader_open(reply, size);
        p16 asked = byte_reader_u16(&reader);
        p16 questions;
        const p8 address_to question;

        (void)byte_reader_skip(&reader, 2);
        questions = byte_reader_u16(&reader);
        (void)byte_reader_skip(&reader, 6);
        question = byte_reader_take(&reader, question_length);
        return byte_reader_ok(&reader) && asked == id && questions == 1 &&
               !(folded ? memory_compare_ascii_case(question, request + DNS_HEADER,
                                                    question_length)
                        : memory_compare(question, request + DNS_HEADER,
                                         question_length));
}

/* Walk every declared resource record, including sections this IPv4 resolver
   does not otherwise consume. A usable A answer cannot make malformed
   authority/additional framing or unaccounted trailing bytes disappear. */
static COLD bipolar dns_records_end(p8 address_to message, positive size,
                               positive at, positive count)
{
        positive pointers = DNS_WORK_POINTERS;

        for (positive record = 0; record < count; record++)
        {
                p8 owner[256];
                positive ended;
                bipolar length = dns_copy_name(message, size, at, owner,
                                                sizeof owner, address_of ended,
                                                address_of pointers);
                byte_reader tail;

                if (length < 0)
                        return DNS_MALFORMED;
                //      The type, class and time to live, then the data
                //      behind its length.
                tail = dns_message_at(message, size, (positive)ended);
                (void)byte_reader_skip(&tail, 8);
                (void)byte_reader_vector16(&tail);
                if (!byte_reader_ok(&tail))
                        return DNS_MALFORMED;
                at = size - byte_reader_left(&tail);
        }
        return (bipolar)at;
}

/* UDP and TCP answers pass through the same response, rcode and record
   validation.  Only a validated truncation indication has a distinct internal
   result so the transport can retry it over TCP. */
static COLD bipolar dns_reply_result(
    p8 address_to reply, positive size, p16 id,
    p8 address_to request, positive question_length,
    p32 address_to found)
{
        p16 flags;
        p16 answers;
        p16 authorities;
        p16 additional;
        positive at;

        positive available = size > DNS_MAX_MESSAGE ? DNS_MAX_MESSAGE : size;

        if (!dns_reply_identity(reply, available, id, request,
                                question_length, false))
                return DNS_MALFORMED;

        {
                byte_reader header = byte_reader_open(reply, available);

                (void)byte_reader_skip(&header, 2);
                flags = byte_reader_u16(&header);
                (void)byte_reader_skip(&header, 2);
                answers = byte_reader_u16(&header);
                authorities = byte_reader_u16(&header);
                additional = byte_reader_u16(&header);
        }
        /* This resolver sends only standard QUERY requests.  A response with
           another opcode is not an answer to the transaction merely because
           its id and echoed question happen to match.  The one reserved DNS
           header bit must likewise remain zero; AD and CD have their own bits
           and are intentionally not rejected here. */
        if (!(flags & DNS_FLAG_RESPONSE) ||
            (flags & (DNS_OPCODE_MASK | DNS_FLAG_RESERVED)))
                return DNS_MALFORMED;
        if (flags & DNS_FLAG_TRUNCATED)
                return DNS_TRY_TCP;
        /* MSG_TRUNC reports the datagram's true length.  A matching TC reply
           needs only its complete header and question to authorize TCP; any
           oversized response that claims to be complete remains malformed. */
        if (size > DNS_MAX_MESSAGE)
                return DNS_MALFORMED;

        at = DNS_HEADER + question_length;
        {
                bipolar records_end = dns_records_end(reply, size, at, answers);

                if (records_end < 0)
                        return DNS_MALFORMED;
                records_end = dns_records_end(reply, size,
                                              (positive)records_end,
                                              authorities);
                if (records_end < 0)
                        return DNS_MALFORMED;
                records_end = dns_records_end(reply, size,
                                              (positive)records_end,
                                              additional);
                if (records_end < 0 || (positive)records_end != size)
                        return DNS_MALFORMED;
        }
        /* An error response can carry authority/additional records and is no
           less attacker-controlled than a successful one. Classify its rcode
           only after the entire non-truncated message has been validated. */
        switch (flags & DNS_CODE_MASK)
        {
        case 0:
                break;
        case 3:
                return DNS_NO_SUCH_NAME;
        default:
                return DNS_REFUSED;
        }
        return dns_answer_address(reply, size, at, answers, DNS_HEADER, found);
}

static COLD bipolar dns_stream_connect_until(
    socket_address_internet address_to where,
    const network_deadline address_to deadline)
{
        bipolar handle = socket_new(
            AF_INET, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
        bipolar status;

        if (handle < 0)
                return -1;

        status = socket_connect((b32)handle, where, sizeof *where);
        if (status < 0 && status != -EINPROGRESS && status != -EALREADY &&
            status != NETWORK_INTERRUPTED)
                goto failed;

        if (status < 0)
        {
                b32 error = 0;
                p32 error_size = sizeof error;

                if (network_wait_writable_until(handle, deadline) <= 0 ||
                    socket_option_get((b32)handle, SOL_SOCKET, SO_ERROR,
                                      address_of error, address_of error_size) < 0 ||
                    error_size != sizeof error || error)
                        goto failed;
        }

        return handle;

failed:
        socket_close((b32)handle);
        return -1;
}

static COLD bipolar dns_retry_tcp(
    socket_address_internet address_to where,
    p8 address_to request, positive request_length, p16 id,
    positive question_length, p32 address_to found,
    const network_deadline address_to deadline)
{
        p8 reply[2 + DNS_MAX_MESSAGE];
        p8 address_to frame = reply;
        bipolar handle;
        p16 length;
        bipolar result = DNS_NO_REPLY;

        if (request_length > DNS_MAX_MESSAGE)
                return DNS_MALFORMED;
        handle = dns_stream_connect_until(where, deadline);
        if (handle < 0)
                return DNS_NO_REPLY;

        /* Prefix and query are one small TCP write: a separate two-byte
           send adds a syscall and can leave the query behind Nagle. */
        network_store_16(frame, (p16)request_length);
        memory_copy(frame + 2, request, request_length);
        if (!network_stream_send_all_until(handle, frame, request_length + 2,
                                           deadline) ||
            !network_stream_read_all(handle, frame, 2, deadline))
                goto done;

        length = network_load_16(frame);
        if (length > DNS_MAX_MESSAGE)
        {
                result = DNS_MALFORMED;
                goto done;
        }
        if (!network_stream_read_all(handle, reply, length, deadline))
                goto done;

        result = dns_reply_result(reply, length, id, request,
                                  question_length, found);
        if (result == DNS_TRY_TCP)
                result = DNS_MALFORMED;

done:
        socket_close((b32)handle);
        return result;
}

/*
        The nameserver, out of resolv.conf.

        Only "nameserver A.B.C.D" lines: the keyword, blanks, and the address
        up to the next blank, so a tab or a trailing comment does not hide a
        server. Addresses are collected from one snapshot. Options, search
        domains and IPv6 servers are read past rather than understood.

        A file longer than the buffer is cut mid-line, and a cut
        "nameserver 10.0.0.12" reads as a complete "10.0.0.1": a server the
        file never named. So the last line of a full buffer is not a line.
*/
static COLD positive dns_servers(string_address path, p32 address_to servers,
                                 positive room)
{
        p8 text[4096];
        bipolar got;
        positive at = 0;
        positive count = 0;

        if (!room)
                return 0;

        got = file_slurp(path, text, sizeof text);

        if (got <= 0)
                return 0;

        while (at < (positive)got)
        {
                positive line = at;
                positive stop = at + memory_span_without_byte(
                    text + at, '\n', (positive)got - at);
                positive from = line + 10;
                positive length = 0;
                p8 kept[64];
                bipolar host;

                at = stop + (stop < (positive)got);

                if (stop == sizeof text - 1)
                        break;
                if (stop - line < 12 ||
                    memory_compare(text + line, "nameserver", 10) ||
                    !byte_is_blank(text[from]))
                        continue;

                from += string_span_max(text + from, stop - from,
                                        string_set_blanks);
                while (from + length < stop && text[from + length] != '\r' &&
                       !byte_is_blank(text[from + length]))
                        length++;
                if (!length || length >= sizeof kept)
                        continue;

                string_copy_max_end(kept, text + from, length);
                host = string_to_host(kept);
                if (host >= 0)
                {
                        servers[count++] = (p32)host;
                        if (count == room)
                                break;
                }
        }

        return count;
}

/*
        Hardening a stub resolver can do on its own.

        A reply is believed when its transaction id and its echoed question
        match, and an attacker who cannot see the query has sixteen bits of id
        and the source port to guess. Three things add to that without any
        server's help, each with the fallback a server that cannot do it needs:

        The source port is drawn from the CSPRNG over 1024 to 65535 and bound
        explicitly (a few tries if taken, then the kernel's own pick), not
        left to the kernel's 32768 to 60999. Every query is its own socket,
        so every query has its own port.

        The question is sent in mixed case (draft-vixie-dnsext-dns0x20): each
        letter of the name takes a random case, every server that keeps the
        question as it was sent echoes it so, and a forged reply has to
        guess that too, a bit a letter. A server that answers in lower case
        (some forwarders do) is noticed when its reply matches in every
        respect but case, and the query is asked again once in the case it
        was typed.

        EDNS0 (RFC 6891) advertises 1232 bytes, the size the 2020 DNS flag day
        settled on because it survives an IPv6 minimum MTU without
        fragmentation, so an answer of 513 to 1232 bytes arrives by UDP and
        not by the TCP retry that 512 forces. A server that answers the option
        with FORMERR or NOTIMP is asked again without it (RFC 6891 7).
*/
#define DNS_EDNS_SIZE 1232
#define DNS_EDNS_LENGTH 11
#define DNS_EDNS_PROBE_SECONDS 1

//      The name's letters in random case. False when randomness is refused,
//      and then the name is left as written. The bits are one request for
//      all 32 bytes, nonblocking as the transaction id's is (a pool that is
//      not ready refuses rather than gives a guessable case), not four.
static COLD bool dns_mix_case(p8 address_to name, positive length)
{
        p8 bits[32];

        if (system_random_fill(bits, sizeof bits, 1))
                return false;
        for (positive at = 0; at < length; at++)
        {
                p8 letter = name[at] | 0x20;

                if (letter >= 'a' && letter <= 'z' && (bits[at / 8 % 32] >> (at % 8) & 1))
                        name[at] ^= 0x20;
        }
        return true;
}

//      An unconnected socket bound to a random port (1024..65535), or the
//      caller's own if none can be had.
static COLD fn dns_bind_random_port(b32 handle)
{
        for (positive tries = 0; tries < 8; tries++)
        {
                p16 draw;
                socket_address_internet here = {.family = AF_INET};

                if (!network_transaction_secure(address_of draw, sizeof draw))
                        return;
                here.port = network_order_16((p16)(1024 + draw % 64512));
                if (socket_bind(handle, address_of here, sizeof here) >= 0)
                        return;
        }
}

//      The question for name as a query of id id: the header, the name (its
//      letters in random case when *mixed, which is set false when randomness
//      is refused), type A class IN, and the EDNS0 option when edns. The
//      length of the name as written, or negative when it does not fit.
static COLD bipolar dns_build_request(p8 address_to request, positive room,
                                      string_address name, p16 id,
                                      bool address_to mixed, bool edns,
                                      positive address_to question_length,
                                      positive address_to request_length)
{
        bipolar written;

        if (room < DNS_HEADER + 4 + DNS_EDNS_LENGTH)
                return DNS_MALFORMED;
        written = dns_write_name(request + DNS_HEADER,
                                 room - DNS_HEADER - 4 - DNS_EDNS_LENGTH, name);
        if (written < 0)
                return written;
        if (address_to mixed)
                address_to mixed = dns_mix_case(request + DNS_HEADER,
                                                (positive)written);

        memory_fill(request, 0, DNS_HEADER);
        network_store_16(request, id);
        network_store_16(request + 2, DNS_FLAG_RECURSE);
        network_store_16(request + 4, 1);
        network_store_16(request + DNS_HEADER + written, DNS_TYPE_A);
        network_store_16(request + DNS_HEADER + written + 2, DNS_CLASS_IN);

        address_to question_length = (positive)written + 4;
        address_to request_length = DNS_HEADER + address_to question_length;

        if (edns)
        {
                p8 address_to option = request + address_to request_length;

                network_store_16(request + 10, 1);
                option[0] = 0;                        // the root as owner
                network_store_16(option + 1, 41);     // OPT
                network_store_16(option + 3, DNS_EDNS_SIZE);
                network_store_32(option + 5, 0);      // version 0, no flags
                network_store_16(option + 9, 0);
                address_to request_length += DNS_EDNS_LENGTH;
        }
        return written;
}

/*
        One question asked, and the first address in the answer.

        The reply is read with a deadline rather than blocked on forever: a
        nameserver that does not answer is the ordinary case on a network that
        is not up yet, and a resolver that hangs there is worse than one that
        gives up and says so. The wait is a poll on the socket rather than a
        receive timeout, which keeps a timeval out of the assembly graph.
*/
static COLD bipolar dns_query_once(p32 server, p16 port, string_address name,
                                   p32 address_to found, bool mixed,
                                   bool edns,
                                   const network_deadline address_to budget)
{
        p8 request[DNS_MAX_MESSAGE];
        p8 reply[DNS_MAX_MESSAGE];
        p16 id;
        bipolar handle;
        bipolar written;
        bipolar got;
        bipolar failure = DNS_NO_REPLY;
        positive question_length;
        positive request_length;
        positive discarded = 0;
        positive left[2];
        network_deadline deadline = *budget;

        if (edns && deadline.budget >
                        DNS_EDNS_PROBE_SECONDS * NETWORK_NANOSECONDS)
                deadline.budget = DNS_EDNS_PROBE_SECONDS * NETWORK_NANOSECONDS;

        /*
                A machine with no RDRAND and no virtio-rng seeds the kernel's
                generator from interrupts, which an idle box or guest can take
                minutes to supply, and the nonblocking ask refuses until then:
                every name failed to resolve on a Nehalem or Sandy Bridge
                guest while a Haswell one resolved them at once. Blocking is
                the other answer, and it is what makes the kernel run its
                jitter entropy and finish seeding in about a second. Still the
                CSPRNG either way, never a guessable id.
        */
        if (!network_transaction_secure(address_of id, sizeof id) &&
            system_random_fill(address_of id, sizeof id, 0))
                return DNS_NO_RANDOM;

        written = dns_build_request(request, sizeof(request), name, id,
                                    address_of mixed, edns,
                                    address_of question_length,
                                    address_of request_length);
        if (written < 0)
                return DNS_MALFORMED;

        handle = socket_new(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);

        if (handle < 0)
                return DNS_NO_SERVER;

        dns_bind_random_port((b32)handle);

        socket_address_internet where = {
            .family = AF_INET, .port = network_order_16(port),
            .host = network_order_32(server)};

        if (socket_connect((b32)handle, address_of where, sizeof where) < 0)
        {
                failure = DNS_NO_SERVER;
                goto failed;
        }

        /* A signal before the datagram left is not the server's silence:
           send it again, within the same budget. */
        do
                written = socket_send((b32)handle, request, request_length, 0,
                                      0, 0);
        while (written == NETWORK_INTERRUPTED &&
               network_deadline_left(address_of deadline, left, left + 1));
        if (written < 0)
                goto failed;

        /* A connected UDP socket authenticates the source address, not the
           transaction. An off-path sender can still spoof that address and
           race junk without guessing the random id. Discard replies that do
           not match both id and question, while charging every one to the
           original total deadline. */
        for (;;)
        {
                positive available;

                got = network_receive_next(handle, reply, sizeof reply, null, null,
                                           address_of deadline);

                if (got == NETWORK_SILENT)
                {
                        if (edns)
                                failure = DNS_EDNS_SILENT;
                        goto failed;
                }
                if (got < 0)
                {
                        /* ICMP port unreachable arrives as ECONNREFUSED on
                           the connected socket: nobody will answer here. */
                        failure = DNS_NO_SERVER;
                        goto failed;
                }
                available = (positive)got > sizeof reply
                    ? sizeof reply : (positive)got;
                if (!dns_reply_identity(reply, available, id, request,
                                        question_length, false))
                {
                        //      Right id, the question in another case: a
                        //      server that does not keep the case it is
                        //      sent. Say so; the caller asks once more.
                        if (mixed && dns_reply_identity(
                                         reply, available, id, request,
                                         question_length, true))
                        {
                                failure = DNS_CASE_FOLDED;
                                goto failed;
                        }
                        if (discarded++ == NETWORK_DISCARD_MAX)
                        {
                                /* A peer able to keep this socket readable
                                   must not buy unbounded parse/syscall work
                                   inside one monotonic deadline. */
                                failure = DNS_NO_REPLY;
                                goto failed;
                        }
                        continue;
                }

                break;
        }

        socket_close((b32)handle);

        failure = dns_reply_result(reply, (positive)got, id, request,
                                   question_length, found);
        if (failure == DNS_REFUSED && edns)
        {
                //      FORMERR or NOTIMP to the option: ask again without it.
                p16 code = network_load_16(reply + 2) & DNS_CODE_MASK;

                if (code == 1 || code == 4)
                        return DNS_FORMAT_ERROR;
        }
        if (failure == DNS_TRY_TCP)
                return dns_retry_tcp(address_of where, request,
                                     request_length, id,
                                     question_length, found,
                                     budget);
        return failure;

failed:
        socket_close((b32)handle);
        return failure;
}

/* The query, with its two fallbacks: once without the case mixing when the
   server folded the question, once without EDNS0 when the server did not
   understand the option or silently dropped it. Both share the one budget.
   EDNS gets only the first second of that absolute budget: otherwise silence
   spends the entire deadline and a nominal fallback can never be sent. */
static COLD bipolar dns_resolve_at(p32 server, p16 port, string_address name,
                                   p32 address_to found, positive seconds)
{
        network_deadline budget;
        bool mixed = true;
        bool edns = true;
        bipolar status = DNS_NO_REPLY;

        /* TCP fallback spends only what the original UDP transaction leaves.
           Start the one monotonic budget before any socket operation. */
        if (!network_deadline_begin(address_of budget, seconds, 0))
                return DNS_NO_REPLY;

        for (positive attempt = 0; attempt < 3; attempt++)
        {
                status = dns_query_once(server, port, name, found, mixed, edns,
                                        address_of budget);
                if (status == DNS_CASE_FOLDED && mixed)
                        mixed = false;
                else if (status == DNS_FORMAT_ERROR && edns)
                        edns = false;
                else if (status == DNS_EDNS_SILENT && edns)
                        edns = false;
                else
                        break;
        }
        return status == DNS_CASE_FOLDED || status == DNS_EDNS_SILENT
                   ? DNS_NO_REPLY
               : status == DNS_FORMAT_ERROR ? DNS_REFUSED : status;
}

/*
        The servers resolv.conf names, in the order it names them, asked the
        way glibc and musl ask them.

        The first server that gives an answer about the name is believed: an
        address, "no such name" or "no address for it". Only a server that
        gives none (no reply, refused, a malformed or failed answer, no
        socket) sends the walk on to the next. The walk stops after three
        servers, which is all glibc and musl read (MAXNS): a file listing more
        would otherwise hold a lookup for its timeout once per line.

        That is the rule with the order the file is written in (see
        net_write_resolv_servers: the network's own resolver, or the ones
        moonwater dns kept, and nothing behind them). A name the network says
        does not exist is not sent anywhere else, and neither is one it gave
        no answer to, which a public resolver behind it would have been told.

        With no resolv.conf at all, or one that names no server, there is
        still somewhere to ask: DNS_FALLBACK, and only then. A machine that
        has not been configured yet should be able to resolve a name, if only
        to fetch the thing that will configure it.
*/
#define DNS_FALLBACK 0x01010101u
#define DNS_SERVERS_MAX 3

/*
        The servers, read once and held: what a confined child does
        (net_apart), since a child that may not open a file cannot read
        resolv.conf at the moment a redirect or a caIssuers location names a
        host. It reads the file before its filter goes on, and every lookup
        it makes after that asks the servers it read, whatever path it names.
        Only that child ever holds; every other process reads the file each
        time, as before.
*/
typedef struct
{
        bool held;
        positive count;
        p32 servers[DNS_SERVERS_MAX];
} dns_held_servers;

static dns_held_servers dns_held;

static COLD fn dns_servers_hold(string_address path)
{
        dns_held.count = dns_servers(path, dns_held.servers, DNS_SERVERS_MAX);
        dns_held.held = true;
}

static CONST COLD bool dns_answer_is_final(bipolar status)
{
        return status == DNS_OK || status == DNS_NO_SUCH_NAME ||
               status == DNS_NO_ADDRESS;
}

static COLD bipolar dns_resolve_any(string_address path, string_address name,
                               p32 address_to found, positive seconds)
{
        bipolar status = DNS_NO_SERVER;
        p32 read[DNS_SERVERS_MAX];
        p32 address_to servers = dns_held.held ? dns_held.servers : read;
        positive count = dns_held.held
                             ? dns_held.count
                             : dns_servers(path, read, array_count(read));

        for (positive index = 0; index < count; index++)
        {
                status = dns_resolve_at(servers[index], DNS_PORT, name, found,
                                        seconds);

                if (dns_answer_is_final(status))
                        return status;
        }

        if (!count)
                return dns_resolve_at(DNS_FALLBACK, DNS_PORT, name, found,
                                      seconds);

        return status;
}

/* An IPv4 name as every client of it asks: the dotted address it is, or what
   the resolvers in /etc/resolv.conf say it is. DNS_OK, with the address in
   found, or the resolver's own answer. */
static COLD bipolar net_resolve_v4(string_address name, p32 address_to found,
                                   positive seconds)
{
        bipolar numeric = string_to_host(name);

        if (numeric >= 0)
        {
                address_to found = (p32)numeric;
                return DNS_OK;
        }
        return dns_resolve_any((string_address) "/etc/resolv.conf", name, found,
                               seconds);
}

#endif // STANDARD_MODERN_C_NET_DNS
/* ---- http: http: a URL, and the bytes behind it ---- */

#ifndef STANDARD_MODERN_C_NET_HTTP
#define STANDARD_MODERN_C_NET_HTTP

/*
        TLS 1.3 and 1.2 client for wget.

        TLS 1.3 with TLS_AES_128_GCM_SHA256; TLS 1.2 with ECDHE-ECDSA and
        ECDHE-RSA over AES-128-GCM, the extended master secret required,
        renegotiation refused, and the TLS 1.3 downgrade sentinel checked.
        Groups: X25519, P-256 and P-384; the ClientHello carries an X25519
        share alone, and a HelloRetryRequest may ask once for either of the
        others (Chimera's secp384r1 servers, Microsoft's P-256 ones).
        Certificates walk to one of the
        Mozilla TLS roots in anchors.inc along a path built from the served
        certificates in any order, and at most one issuer fetched from a
        caIssuers location: a served certificate carrying an anchor's key
        ends the chain, or the path's last certificate names an anchor as
        its issuer and verifies under it. Chain signatures may be ECDSA with
        SHA-256, -384 or -512 on P-256 or P-384, or RSA PKCS#1 v1.5 with
        SHA-256, -384 or -512 under keys of 2,048 to 8,192 bits. Name
        constraints bind the path below them, and a wildcard standing on an
        ICANN public suffix names nothing.
        Signature algorithms advertised are ecdsa_secp256r1_sha256,
        ecdsa_secp384r1_sha384, rsa_pss_rsae_sha256, and for TLS 1.2 and
        certificates rsa_pkcs1_sha256 and rsa_pkcs1_sha384. A
        CertificateRequest is answered with no certificate; a KeyUpdate is
        taken and, asked, answered. close_notify is a
        clean end of the body, not a handshake failure.
        --no-check-certificate skips the chain and still encrypts.
*/

#ifndef STANDARD_MODERN_C_NET_TLS
#define STANDARD_MODERN_C_NET_TLS

/*
        Hashes, AES-GCM, X25519 and the signature checks HTTPS needs.

        wget speaks TLS 1.3 and 1.2 with AES-128-GCM, X25519, P-256 and P-384. The chain for
        those handshakes is ECDSA on P-256 and P-384, RSA PKCS#1 and RSA-PSS SHA-256;
        Alpine and GitHub still present RSA leaves. SHA-256 compression is
        sha256_compress in lib.c, on the same hardware floor as the rest
        of the binary, and SHA-384 is sha512_blocks through the same streaming
        digests the checksum utilities use. ECDSA stays C, and X25519 is lib.c's. None of
        this is a kernel crypto ABI: AF_ALG is off on Moonwater, and a
        downloader cannot wait on it.
*/

#ifndef STANDARD_MODERN_C_NET_CRYPTO
#define STANDARD_MODERN_C_NET_CRYPTO

typedef unsigned __int128 crypto_wide;

/*
        SHA-256 for the transcript, HKDF and the signature hashes, SHA-384 for
        the P-384 and RSA-SHA384 chains: the library's streaming digests over
        sha256_blocks and sha512_blocks, under the names this file has always
        used. A transcript copy is a digest_state copy.
*/
typedef digest_state crypto_sha256;

static fn crypto_sha256_open(crypto_sha256 address_to hash)
{
        digest_open(hash, DIGEST_SHA256, 32);
}

static fn crypto_sha256_write(crypto_sha256 address_to hash, p8 address_to data,
                              positive length)
{
        digest_write(hash, data, length);
}

static fn crypto_sha256_close(crypto_sha256 address_to hash, p8 address_to out)
{
        digest_close(hash, out);
}

static fn crypto_sha256_of(p8 address_to data, positive length, p8 address_to out)
{
        crypto_sha256 hash;

        crypto_sha256_open(address_of hash);
        crypto_sha256_write(address_of hash, data, length);
        crypto_sha256_close(address_of hash, out);
}

static fn crypto_sha384(p8 address_to data, positive length, p8 address_to out)
{
        digest_state hash;

        digest_open(address_of hash, DIGEST_SHA384, 48);
        digest_write(address_of hash, data, length);
        digest_close(address_of hash, out);
}

static fn crypto_forget(address_any secret, positive length);

/*
        HMAC, streamed.

        RFC 2104 is one machine whichever digest runs inside it: the key
        padded out to the digest's block, that block xored with 0x36 opening
        the inner hash and xored with 0x5c opening the outer one over the
        inner sum. Only the digest changes, so only the digest is a
        parameter. SHA-1 and SHA-256 -- WPA's PRF and TLS's key schedule,
        which is every caller here -- share the 64 byte block; the SHA-512
        family's is 128 and does not belong in this pad, which is why the
        block is a constant rather than a field.

        It streams because HKDF's expand hashes three spans in a row: a
        one-shot form would make that caller join them in a buffer first,
        which is a copy and a length limit for nothing. The digest remembers
        which one it is, so the closing pass keeps nothing beside it but the
        padded key.
*/
typedef struct
{
        digest_state hash;
        p8 key_block[64];
} crypto_mac;

static inline INLINE fn crypto_hmac_open(crypto_mac address_to mac,
                                         positive algorithm, positive size,
                                         p8 address_to key,
                                         positive key_length)
{
        p8 pad[64];

        digest_open(address_of mac->hash, algorithm, size);
        memory_fill(mac->key_block, 0, 64);

        //      A key longer than the block stands for its own digest.
        if (key_length > 64)
        {
                digest_write(address_of mac->hash, key, key_length);
                digest_close(address_of mac->hash, mac->key_block);
                digest_open(address_of mac->hash, algorithm, size);
        }
        else
                memory_copy(mac->key_block, key, key_length);

        for (positive i = 0; i < 64; i++)
                pad[i] = mac->key_block[i] ^ 0x36;
        digest_write(address_of mac->hash, pad, 64);
        crypto_forget(pad, sizeof pad);
}

static fn crypto_hmac_write(crypto_mac address_to mac, p8 address_to data,
                            positive length)
{
        digest_write(address_of mac->hash, data, length);
}

static inline INLINE fn crypto_hmac_close(crypto_mac address_to mac,
                                          p8 address_to out)
{
        positive algorithm = mac->hash.algorithm;
        positive size = mac->hash.size;
        p8 pad[64];
        p8 inner[64];

        digest_close(address_of mac->hash, inner);
        for (positive i = 0; i < 64; i++)
                pad[i] = mac->key_block[i] ^ 0x5c;

        digest_open(address_of mac->hash, algorithm, size);
        digest_write(address_of mac->hash, pad, 64);
        digest_write(address_of mac->hash, inner, size);
        digest_close(address_of mac->hash, out);

        crypto_forget(mac, sizeof(*mac));
        crypto_forget(pad, sizeof pad);
        crypto_forget(inner, sizeof inner);
}

/* One message under one key, whole: the digest's size bytes of it. */
static fn crypto_hmac(positive algorithm, positive size, p8 address_to key,
                      positive key_length, p8 address_to data, positive length,
                      p8 address_to out)
{
        crypto_mac mac;

        crypto_hmac_open(address_of mac, algorithm, size, key, key_length);
        crypto_hmac_write(address_of mac, data, length);
        crypto_hmac_close(address_of mac, out);
}

static fn crypto_hmac_sha256(p8 address_to key, positive key_length,
                             p8 address_to data, positive length,
                             p8 address_to out)
{
        crypto_hmac(DIGEST_SHA256, 32, key, key_length, data, length, out);
}

/*
        A key made ready: the digests of its two pads, so that a message under
        it costs the blocks it takes and the two that close it, and not the
        two that open it as well. PBKDF2 runs its rounds on these, and a
        listener that checks a datagram against a key of its own every time
        keeps them with the key.
*/
typedef struct
{
        digest_state inner;
        digest_state outer;
} crypto_hmac_key;

static inline INLINE fn crypto_hmac_prepare(positive algorithm, positive size,
                                            p8 address_to key,
                                            positive key_length,
                                            crypto_hmac_key address_to prepared)
{
        crypto_mac mac;
        p8 pad[64];

        crypto_hmac_open(address_of mac, algorithm, size, key, key_length);
        prepared->inner = mac.hash;
        for (positive at = 0; at < 64; at++)
                pad[at] = mac.key_block[at] ^ 0x5c;
        digest_open(address_of prepared->outer, algorithm, size);
        digest_write(address_of prepared->outer, pad, 64);
        crypto_forget(address_of mac, sizeof mac);
        crypto_forget(pad, sizeof pad);
}

/*
        The same for SHA-256, which is every check of a stranger's datagram
        and so the one HMAC anyone can make this machine do by the million.
        Through the digest functions the last block of the message and the one
        that holds the inner sum are built a byte or a word at a time and
        handed to sha256_blocks, which reads a block in sixteen-byte loads, and
        a load that spans several smaller stores waits for them to leave the
        store buffer: two such waits in a check were a fifth of it. Here each
        block is made of sixteen-byte stores -- the whole sixteens of the
        message, what is left of it in two words, the padding and the length
        in words -- so every load meets the one store that made it, and
        nothing goes through a digest state.
*/
typedef p64 crypto_lane __attribute__((vector_size(16), may_alias));
typedef p64 crypto_lane_loose
    __attribute__((vector_size(16), may_alias, aligned(1)));

static inline INLINE p32 crypto_swap32(p32 value)
{
        return value >> 24 | (value >> 8 & 0xff00) | (value << 8 & 0xff0000) |
               value << 24;
}

//      The bytes of a word written big endian, as the word that holds them.
static inline INLINE p64 crypto_bytes_of(p64 value)
{
        return (p64)crypto_swap32((p32)value) << 32 |
               crypto_swap32((p32)(value >> 32));
}

//      Up to eight bytes as a little-endian word, and not one read past them.
static inline INLINE p64 crypto_part(const p8 address_to bytes, positive length)
{
        if (length >= 4)
                return (p64)memory_load_unaligned(p32, bytes) |
                       (p64)memory_load_unaligned(p32, bytes + length - 4)
                           << (8 * (length - 4));

        return length ? (p64)bytes[0] | (p64)bytes[length >> 1] << (8 * (length >> 1)) |
                            (p64)bytes[length - 1] << (8 * (length - 1))
                      : 0;
}

//      Eight words of state as the thirty-two bytes of a digest, two lanes.
static inline INLINE fn crypto_lanes_of_words(crypto_lane_loose address_to into,
                                              const p32 address_to words)
{
        for (positive at = 0; at < 2; at++)
                into[at] = (crypto_lane){
                    (p64)crypto_swap32(words[4 * at]) |
                        (p64)crypto_swap32(words[4 * at + 1]) << 32,
                    (p64)crypto_swap32(words[4 * at + 2]) |
                        (p64)crypto_swap32(words[4 * at + 3]) << 32};
}

static fn crypto_hmac_sha256_prepared(const crypto_hmac_key address_to prepared,
                                      p8 address_to data, positive length,
                                      p8 address_to out)
{
        crypto_lane state[2];
        crypto_lane block[8];
        p32 address_to words = (p32 address_to)state;
        positive whole = length >> 6;
        positive rest = length & 63;
        positive full = rest >> 4;
        positive left = rest & 15;
        positive blocks = rest < 56 ? 1 : 2;
        p8 address_to tail = data + (whole << 6);
        p64 bits = crypto_bytes_of((prepared->inner.bytes + length) << 3);
        p64 low;
        p64 high;

        state[0] = *(const crypto_lane_loose address_to)prepared->inner.state.narrow;
        state[1] = *(const crypto_lane_loose address_to)(prepared->inner.state.narrow + 4);
        if (whole)
                sha256_blocks(words, data, whole);

        for (positive at = 0; at < blocks * 4; at++)
                block[at] = (crypto_lane){0, 0};
        for (positive at = 0; at < full; at++)
                block[at] = *(const crypto_lane_loose address_to)(tail + 16 * at);
        if (left >= 8)
        {
                low = memory_load_unaligned(p64, tail + 16 * full);
                high = crypto_part(tail + 16 * full + 8, left - 8) |
                       (p64)0x80 << (8 * (left - 8));
        }
        else
        {
                low = crypto_part(tail + 16 * full, left) | (p64)0x80 << (8 * left);
                high = 0;
        }
        block[full] = (crypto_lane){low, high};
        if (blocks == 1 && full == 3)
                block[3] = (crypto_lane){low, bits};
        else
                block[blocks * 4 - 1] = (crypto_lane){0, bits};
        sha256_blocks(words, (p8 address_to)block, blocks);

        crypto_lanes_of_words((crypto_lane_loose address_to)block, words);
        block[2] = (crypto_lane){0x80, 0};
        block[3] = (crypto_lane){
            0, crypto_bytes_of((prepared->outer.bytes + 32) << 3)};
        state[0] = *(const crypto_lane_loose address_to)prepared->outer.state.narrow;
        state[1] = *(const crypto_lane_loose address_to)(prepared->outer.state.narrow + 4);
        sha256_blocks(words, (p8 address_to)block, 1);
        crypto_lanes_of_words((crypto_lane_loose address_to)out, words);

        crypto_forget(state, sizeof state);
        crypto_forget(block, sizeof block);
}

//      The HMAC of a message under a prepared key.
static fn crypto_hmac_prepared(const crypto_hmac_key address_to prepared,
                               p8 address_to data, positive length,
                               p8 address_to out)
{
        digest_state work;
        p8 inner[64];

        if (prepared->inner.algorithm == DIGEST_SHA256)
        {
                //      The assembly has the pad blocks' 64 bytes in it.
                if (prepared->inner.bytes != 64 || prepared->outer.bytes != 64 ||
                    !hash_hmac_sha256_prepared(prepared->inner.state.narrow,
                                               prepared->outer.state.narrow,
                                               data, length, out))
                        crypto_hmac_sha256_prepared(prepared, data, length, out);
                return;
        }

        work = prepared->inner;
        digest_write(address_of work, data, length);
        digest_close(address_of work, inner);
        work = prepared->outer;
        digest_write(address_of work, inner, work.size);
        digest_close(address_of work, out);
        crypto_forget(address_of work, sizeof work);
        crypto_forget(inner, sizeof inner);
}

/*
        PBKDF2 over HMAC with SHA-1 or SHA-256 (RFC 8018): WPA's pre-shared
        key and waterlink's group key. Both pads are hashed once and each
        round copies the two prepared states, so a round is two compressions
        rather than the four an HMAC opened afresh would take -- the whole
        cost of PBKDF2 is rounds, so that is the whole cost halved.
*/
static fn crypto_pbkdf2(positive algorithm, positive size,
                        p8 address_to password, positive password_length,
                        p8 address_to salt, positive salt_length,
                        positive rounds, p8 address_to out,
                        positive out_length)
{
        crypto_hmac_key key;
        digest_state work;
        p8 block[64];
        p8 mix[64];
        p8 number[4];

        crypto_hmac_prepare(algorithm, size, password, password_length,
                            address_of key);

        for (positive index = 1, done = 0; done < out_length; index++)
        {
                positive take = out_length - done < size ? out_length - done
                                                         : size;

                network_store_32(number, (p32)index);
                work = key.inner;
                digest_write(address_of work, salt, salt_length);
                digest_write(address_of work, number, 4);
                digest_close(address_of work, block);
                work = key.outer;
                digest_write(address_of work, block, size);
                digest_close(address_of work, block);
                memory_copy(mix, block, size);

                for (positive round = 1; round < rounds; round++)
                {
                        work = key.inner;
                        digest_write(address_of work, block, size);
                        digest_close(address_of work, block);
                        work = key.outer;
                        digest_write(address_of work, block, size);
                        digest_close(address_of work, block);
                        for (positive at = 0; at < size; at++)
                                mix[at] ^= block[at];
                }
                memory_copy(out + done, mix, take);
                done += take;
        }

        crypto_forget(address_of key, sizeof key);
        crypto_forget(address_of work, sizeof work);
        crypto_forget(block, sizeof block);
        crypto_forget(mix, sizeof mix);
}

/* RFC 5869's absent salt is 32 zero bytes, and HMAC pads a key with zeros
   to its block, so no salt at all is the same key. */
static fn crypto_hkdf_extract(p8 address_to salt, positive salt_length,
                              p8 address_to ikm, positive ikm_length,
                              p8 address_to prk)
{
        crypto_hmac_sha256(salt, salt_length, ikm, ikm_length, prk);
}

/* RFC 5869 2.3: L is at most 255 HashLen, the blocks one counter byte
   numbers. A longer ask is refused with its output wiped, where the counter
   used to wrap to zero and go on as something that is not HKDF. */
static bool crypto_hkdf_expand(p8 address_to prk, p8 address_to info,
                               positive info_length, p8 address_to out,
                               positive out_length)
{
        p8 previous[32];
        p8 block[32];
        positive have = 0;
        p8 counter = 1;

        if (out_length > 255 * 32)
        {
                crypto_forget(out, out_length);
                return false;
        }
        while (have < out_length)
        {
                crypto_mac mac;
                positive take = out_length - have;

                //      T(1) has no predecessor; every later block is keyed
                //      by the one before it, which is what chains them.
                crypto_hmac_open(address_of mac, DIGEST_SHA256, 32, prk, 32);
                if (counter > 1)
                        crypto_hmac_write(address_of mac, previous, 32);
                crypto_hmac_write(address_of mac, info, info_length);
                crypto_hmac_write(address_of mac, address_of counter, 1);
                crypto_hmac_close(address_of mac, block);

                if (take > 32)
                        take = 32;
                memory_copy(out + have, block, take);
                have += take;

                memory_copy(previous, block, 32);
                counter++;
        }

        crypto_forget(previous, sizeof previous);
        crypto_forget(block, sizeof block);
        return true;
}

/* The AES state and key are secret, so an ordinary S-box table exposes them
   through the cache.  Invert in GF(2^8) with a fixed addition chain, then
   apply the AES affine transform.  Every input follows the same operations
   and addresses. */
static p8 crypto_aes_field_multiply(p8 left, p8 right)
{
        p8 product = 0;
        positive bit;

        for (bit = 0; bit < 8; bit++)
        {
                p8 selected = (p8)(0 - (right & 1));
                p8 high = left >> 7;

                product ^= left & selected;
                left = (p8)((left << 1) ^
                            (0x1b & (p8)(0 - high)));
                right >>= 1;
        }

        return product;
}

/* x^254, which is the inverse of x and 0 for 0: the addition chain both
   S-boxes run, the same operations for every input. */
static p8 crypto_aes_field_inverse(p8 value)
{
        p8 x2 = crypto_aes_field_multiply(value, value);
        p8 x3 = crypto_aes_field_multiply(x2, value);
        p8 x6 = crypto_aes_field_multiply(x3, x3);
        p8 x12 = crypto_aes_field_multiply(x6, x6);
        p8 x15 = crypto_aes_field_multiply(x12, x3);
        p8 x30 = crypto_aes_field_multiply(x15, x15);
        p8 x60 = crypto_aes_field_multiply(x30, x30);
        p8 x120 = crypto_aes_field_multiply(x60, x60);
        p8 x240 = crypto_aes_field_multiply(x120, x120);

        return crypto_aes_field_multiply(
            crypto_aes_field_multiply(x240, x12), x2);
}

static p8 crypto_aes_substitute(p8 value)
{
        p8 inverse = crypto_aes_field_inverse(value);

        return (p8)(inverse ^
                    ((inverse << 1) | (inverse >> 7)) ^
                    ((inverse << 2) | (inverse >> 6)) ^
                    ((inverse << 3) | (inverse >> 5)) ^
                    ((inverse << 4) | (inverse >> 4)) ^ 0x63);
}

/* The inverse S-box: the affine transform undone, then the same inversion.
   The state it runs on is a key being unwrapped, so no table indexed by it
   either: the one a decrypt wants was built from the forward box and read
   back by the secret itself. */
static p8 crypto_aes_unsubstitute(p8 value)
{
        return crypto_aes_field_inverse(
            (p8)(((value << 1) | (value >> 7)) ^
                 ((value << 3) | (value >> 5)) ^
                 ((value << 6) | (value >> 2)) ^ 0x05));
}

/* Two authenticators compared without telling the peer where they first
   differed.  memory_compare stops at the first differing byte, which turns a
   rejected tag into a measurement of how many leading bytes were right, and
   an authenticator a peer may retry under one key is guessable a byte at a
   time from that.  This reads both spans whole, the way the AEAD tag check
   below already does. */
static bool crypto_same(const p8 address_to left, const p8 address_to right,
                        positive length)
{
        p8 diff = 0;

        for (positive at = 0; at < length; at++)
                diff |= (p8)(left[at] ^ right[at]);

        return !diff;
}

/* Unlike memory_fill alone, these stores cannot be discarded after the final
   use: the empty asm takes the address and clobbers memory, so the compiler
   has to assume something reads the zeros, and an optimizer that drops a
   dead fill cannot drop this one. That is explicit_bzero's shape. A byte
   at a time through a volatile pointer bought the same guarantee at one
   store a byte, which was a quarter of every point double and eight percent
   of a whole connection in wiping the connection itself. */
static fn crypto_forget(address_any secret, positive length)
{
        memory_fill(secret, 0, length);
        __asm__ __volatile__("" : : "r"(secret) : "memory");
}

static fn crypto_aes128_expand(p8 address_to key, p8 address_to round)
{
        static const p8 rcon[10] = {0x01, 0x02, 0x04, 0x08, 0x10,
                                    0x20, 0x40, 0x80, 0x1b, 0x36};
        positive i;

        memory_copy(round, key, 16);
        for (i = 16; i < 176; i += 4)
        {
                p8 t0 = round[i - 4];
                p8 t1 = round[i - 3];
                p8 t2 = round[i - 2];
                p8 t3 = round[i - 1];

                if (i % 16 == 0)
                {
                        p8 k = t0;
                        t0 = crypto_aes_substitute(t1) ^ rcon[i / 16 - 1];
                        t1 = crypto_aes_substitute(t2);
                        t2 = crypto_aes_substitute(t3);
                        t3 = crypto_aes_substitute(k);
                }

                round[i] = round[i - 16] ^ t0;
                round[i + 1] = round[i - 15] ^ t1;
                round[i + 2] = round[i - 14] ^ t2;
                round[i + 3] = round[i - 13] ^ t3;
        }
}

/* One byte of InvMixColumns of a column. The matrix's four rows are the same
   four coefficients rotated, so the row it is wanted for says where to start
   reading them, and the multiply is the constant-time one the S-box runs in. */
static p8 crypto_aes_unmix(const p8 address_to column, positive row)
{
        static const p8 factor[4] = {0x0e, 0x0b, 0x0d, 0x09};
        p8 mixed = 0;

        for (positive at = 0; at < 4; at++)
                mixed ^= crypto_aes_field_multiply(
                    column[at], factor[(4 + at - row) & 3]);

        return mixed;
}

/* One block back through the schedule crypto_aes128_expand made. */
static fn crypto_aes128_decrypt(const p8 address_to round,
                                const p8 address_to in, p8 address_to out)
{
        p8 state[16];
        p8 hold[16];
        positive step;
        positive at;
        positive row;
        p8 temp;

        memory_copy(state, in, 16);
        for (at = 0; at < 16; at++)
                state[at] ^= round[160 + at];

        /* The last round is every other round without InvMixColumns, so the
           loop runs once more and leaves from the middle rather than
           repeating its first three steps underneath itself. */
        for (step = 9;; step--)
        {
                //      InvShiftRows: row r of the state -- the bytes r, r+4,
                //      r+8 and r+12 -- rotates right by r.
                for (row = 1; row < 4; row++)
                        for (positive turn = 0; turn < row; turn++)
                        {
                                temp = state[row + 12];
                                state[row + 12] = state[row + 8];
                                state[row + 8] = state[row + 4];
                                state[row + 4] = state[row];
                                state[row] = temp;
                        }

                for (at = 0; at < 16; at++)
                        state[at] = crypto_aes_unsubstitute(state[at]) ^
                                    round[step * 16 + at];
                if (!step)
                        break;

                memory_copy(hold, state, 16);
                for (positive column = 0; column < 4; column++)
                        for (row = 0; row < 4; row++)
                                state[column * 4 + row] =
                                    crypto_aes_unmix(hold + column * 4, row);
        }

        memory_copy(out, state, 16);
        crypto_forget(state, sizeof(state));
        crypto_forget(hold, sizeof(hold));
}

/*
        RFC 3394 key unwrap under a 16 byte key: the plaintext of a wrap of
        length bytes (a multiple of eight, three blocks to CRYPTO_WRAP_MOST),
        true with the check value in place. WPA's key data is wrapped this
        way under the EAPOL-Key's KEK. The schedule is made once for all the
        blocks, and what is unwrapped is forgotten here whether the check
        held or not.
*/
#define CRYPTO_WRAP_MOST 408

static bool crypto_aes_key_unwrap(p8 address_to kek, p8 address_to wrap,
                                  positive length, p8 address_to plain,
                                  positive address_to plain_length)
{
        p8 round[176];
        p8 block[16];
        p8 a[8];
        p8 r[CRYPTO_WRAP_MOST - 8];
        positive words;
        bool unwrapped;

        if (length < 24 || (length & 7) || length > CRYPTO_WRAP_MOST)
                return false;
        words = (length / 8) - 1;
        crypto_aes128_expand(kek, round);
        memory_copy(a, wrap, 8);
        memory_copy(r, wrap + 8, words * 8);
        for (positive turn = 6; turn > 0; turn--)
                for (positive i = words; i > 0; i--)
                {
                        p64 t = (p64)words * (turn - 1) + i;

                        network_store_64(a, network_load_64(a) ^ t);
                        memory_copy(block, a, 8);
                        memory_copy(block + 8, r + (i - 1) * 8, 8);
                        crypto_aes128_decrypt(round, block, block);
                        memory_copy(a, block, 8);
                        memory_copy(r + (i - 1) * 8, block + 8, 8);
                }

        unwrapped = network_load_64(a) == 0xa6a6a6a6a6a6a6a6ull;
        if (unwrapped)
        {
                memory_copy(plain, r, words * 8);
                address_to plain_length = words * 8;
        }
        crypto_forget(round, sizeof(round));
        crypto_forget(block, sizeof(block));
        crypto_forget(a, sizeof(a));
        crypto_forget(r, sizeof(r));
        return unwrapped;
}

/* AES-128 of one block under the FIPS-197 schedule round. lib.c's AES runs
   in GCM's counter mode, and the keystream of a counter is that block
   encrypted, so one block of zeros through it is the cipher. */
static fn crypto_aes128_encrypt(const p8 address_to round, const p8 address_to in,
                                p8 address_to out)
{
        p8 counter[16];
        p8 zero[16];

        memory_copy(counter, in, 16);
        memory_fill(zero, 0, 16);
        aes128_ctr_blocks(round, counter, zero, out, 1);
        crypto_forget(counter, sizeof(counter));
}

/* A CMAC subkey: the block doubled in GF(2^128), the carry folded back by
   mask rather than by a branch on the key. */
static fn crypto_cmac_double(p8 address_to block)
{
        p8 carry = (p8)(0 - (block[0] >> 7));

        for (positive at = 0; at < 15; at++)
                block[at] = (p8)((block[at] << 1) | (block[at + 1] >> 7));
        block[15] = (p8)((block[15] << 1) ^ (carry & 0x87));
}

/*
        AES-CMAC (RFC 4493) under a 128-bit key: K1 is AES(0) doubled and K2
        is K1 doubled; the blocks are chained, and the last is XORed with K1
        when it is whole and with K2 after 10* padding when it is not or
        there is none. The MIC of the key frames of WPA's descriptor
        version 3 and of SAE.
*/
static fn crypto_aes_cmac(p8 address_to key, const p8 address_to data, positive length,
                          p8 address_to out)
{
        p8 round[176];
        p8 subkey[2][16];
        p8 state[16];
        p8 last[16];
        positive blocks = length ? (length + 15) / 16 : 1;
        positive tail = length - (blocks - 1) * 16;
        bool whole = length && tail == 16;

        crypto_aes128_expand(key, round);
        memory_fill(state, 0, 16);
        crypto_aes128_encrypt(round, state, subkey[0]);
        crypto_cmac_double(subkey[0]);
        memory_copy(subkey[1], subkey[0], 16);
        crypto_cmac_double(subkey[1]);
        for (positive at = 0; at + 1 < blocks; at++)
        {
                for (positive byte = 0; byte < 16; byte++)
                        state[byte] ^= data[at * 16 + byte];
                crypto_aes128_encrypt(round, state, state);
        }
        memory_fill(last, 0, 16);
        if (tail)
                memory_copy(last, data + (blocks - 1) * 16, tail);
        if (!whole)
                last[tail] = 0x80;
        for (positive byte = 0; byte < 16; byte++)
                state[byte] ^= last[byte] ^ subkey[whole ? 0 : 1][byte];
        crypto_aes128_encrypt(round, state, out);
        crypto_forget(round, sizeof(round));
        crypto_forget(subkey, sizeof(subkey));
        crypto_forget(state, sizeof(state));
        crypto_forget(last, sizeof(last));
}

/*
        GHASH over a span, the last partial block zero padded. The multiply
        is ghash_blocks in lib.c over the table ghash_key made; GCM's
        framing stays here.
*/
static fn crypto_ghash_span(p8 address_to state, p8 address_to table,
                            p8 address_to bytes, positive length)
{
        positive whole = length / 16;
        p8 padded[16];

        ghash_blocks(state, table, bytes, whole);

        if (length % 16)
        {
                memory_fill(padded, 0, 16);
                memory_copy(padded, bytes + whole * 16, length % 16);
                ghash_blocks(state, table, padded, 1);
                crypto_forget(padded, sizeof padded);
        }
}

/*
        An AES-128-GCM key prepared once: the FIPS-197 schedule and the GHASH
        table of H's powers. Preparing costs the key expansion, one block and
        forty seven carry-less multiplies, which a connection pays when it
        installs a traffic key rather than on every record. It is key
        material; wipe it with the key.
*/
typedef struct
{
        p8 round[176];
        p8 table[GHASH_KEY_SIZE] __attribute__((aligned(64)));
} crypto_aesgcm_key;

static fn crypto_aesgcm_prepare(crypto_aesgcm_key address_to key,
                                p8 address_to raw)
{
        p8 counter[16];
        p8 h[16];

        crypto_aes128_expand(raw, key->round);
        memory_fill(counter, 0, 16);
        memory_fill(h, 0, 16);
        aes128_ctr_blocks(key->round, counter, h, h, 1);
        ghash_key(key->table, h);
        crypto_forget(h, sizeof h);
}

static fn crypto_aesgcm_crypt(crypto_aesgcm_key address_to key,
                              p8 address_to iv, p8 address_to aad,
                              positive aad_length, p8 address_to text,
                              positive text_length, p8 address_to tag,
                              bool encrypt)
{
        p8 j0[16];
        p8 counter[16];
        p8 s[16];
        p8 padded[16];
        positive whole = text_length / 16;
        positive rest = text_length % 16;

        memory_copy(j0, iv, 12);
        j0[12] = 0;
        j0[13] = 0;
        j0[14] = 0;
        j0[15] = 1;
        memory_copy(counter, j0, 16);
        counter[15] = 2;
        memory_fill(s, 0, 16);

        crypto_ghash_span(s, key->table, aad, aad_length);

        //      GHASH reads the ciphertext both ways: before the counter
        //      stream comes off it on the way in, after it goes on on the
        //      way out.
        if (!encrypt)
                crypto_ghash_span(s, key->table, text, text_length);

        aes128_ctr_blocks(key->round, counter, text, text, whole);
        if (rest)
        {
                memory_fill(padded, 0, 16);
                memory_copy(padded, text + whole * 16, rest);
                aes128_ctr_blocks(key->round, counter, padded, padded, 1);
                memory_copy(text + whole * 16, padded, rest);
        }

        if (encrypt)
                crypto_ghash_span(s, key->table, text, text_length);

        memory_fill(padded, 0, 16);
        network_store_64(padded, (p64)aad_length * 8);
        network_store_64(padded + 8, (p64)text_length * 8);
        ghash_blocks(s, key->table, padded, 1);

        aes128_ctr_blocks(key->round, j0, s, tag, 1);

        crypto_forget(j0, sizeof j0);
        crypto_forget(counter, sizeof counter);
        crypto_forget(s, sizeof s);
        crypto_forget(padded, sizeof padded);
}

static fn crypto_aesgcm_seal(crypto_aesgcm_key address_to key,
                             p8 address_to iv, p8 address_to aad,
                             positive aad_length, p8 address_to text,
                             positive text_length, p8 address_to tag)
{
        crypto_aesgcm_crypt(key, iv, aad, aad_length, text, text_length, tag,
                            true);
}

static bool crypto_aesgcm_open(crypto_aesgcm_key address_to key,
                               p8 address_to iv, p8 address_to aad,
                               positive aad_length, p8 address_to text,
                               positive text_length, p8 address_to tag)
{
        p8 got[16];
        bool valid;

        crypto_aesgcm_crypt(key, iv, aad, aad_length, text, text_length, got,
                            false);
        valid = crypto_same(got, tag, 16);
        if (!valid)
                crypto_forget(text, text_length);
        crypto_forget(got, sizeof got);
        return valid;
}

/*
        AES-SIV-CMAC-256, RFC 5297: the AEAD RFC 8915 makes mandatory for
        NTS (AEAD_AES_SIV_CMAC_256, number 15). The 256-bit key is two
        AES-128 keys, the left for S2V, which turns the associated data and
        the plaintext into a synthetic IV with CMAC (RFC 4493), the right
        for counter mode under that IV. The IV is the tag and leads the
        ciphertext. Nothing is built that the AES-128 schedule and
        lib.c's counter body did not already give: one block is the counter
        body over a zero block, and the two bits RFC 5297 clears in the
        counter are what make the body's 32-bit wrapping increment exact.
        Strings are taken as parts (the associated data, then the nonce
        RFC 5116 asks to be the last of them), and the plaintext is the
        final string S2V takes.
*/
typedef struct
{
        p8 round[176];
        p8 first[16];
        p8 second[16];
} crypto_cmac_key;

typedef struct
{
        crypto_cmac_key mac;
        p8 round[176];
} crypto_siv_key;

static fn crypto_aes128_block(const p8 address_to round, const p8 address_to in,
                              p8 address_to out)
{
        p8 counter[16];

        memory_copy(counter, in, 16);
        memory_fill(out, 0, 16);
        aes128_ctr_blocks(round, counter, out, out, 1);
        crypto_forget(counter, sizeof counter);
}

//      Multiplication by x in GF(2^128) (RFC 5297 2.3, RFC 4493 2.3).
static fn crypto_siv_double(p8 address_to block)
{
        p8 carry = (p8)(block[0] >> 7);

        for (positive at = 0; at < 15; at++)
                block[at] = (p8)((block[at] << 1) | (block[at + 1] >> 7));
        block[15] = (p8)((block[15] << 1) ^ (0x87 & (p8)(0 - carry)));
}

static fn crypto_cmac_prepare(crypto_cmac_key address_to key,
                              const p8 address_to raw)
{
        p8 zero[16];

        memory_fill(zero, 0, 16);
        crypto_aes128_expand((p8 address_to)raw, key->round);
        crypto_aes128_block(key->round, zero, key->first);
        crypto_siv_double(key->first);
        memory_copy(key->second, key->first, 16);
        crypto_siv_double(key->second);
}

//      CMAC of a span. With xorend, the span's last 16 bytes are taken
//      with that block xored in, which is S2V's "xorend" without a copy.
static fn crypto_cmac(const crypto_cmac_key address_to key,
                      const p8 address_to data, positive length,
                      const p8 address_to xorend, p8 address_to out)
{
        positive blocks = length ? (length + 15) / 16 : 1;
        p8 state[16];
        p8 block[16];

        memory_fill(state, 0, 16);
        for (positive at = 0; at < blocks; at++)
        {
                bool last = at + 1 == blocks;

                for (positive in = 0; in < 16; in++)
                {
                        positive index = at * 16 + in;
                        p8 byte = 0;

                        if (index < length)
                        {
                                byte = data[index];
                                if (xorend && length >= 16 && index + 16 >= length)
                                        byte ^= xorend[index + 16 - length];
                        }
                        else if (index == length)
                                byte = 0x80;
                        block[in] = byte ^ state[in];
                }
                if (last)
                {
                        const p8 address_to subkey =
                            length && length % 16 == 0 ? key->first
                                                        : key->second;

                        for (positive in = 0; in < 16; in++)
                                block[in] ^= subkey[in];
                }
                crypto_aes128_block(key->round, block, state);
        }
        memory_copy(out, state, 16);
        crypto_forget(state, sizeof state);
        crypto_forget(block, sizeof block);
}

static fn crypto_siv_prepare(crypto_siv_key address_to key,
                             const p8 address_to raw)
{
        crypto_cmac_prepare(address_of key->mac, raw);
        crypto_aes128_expand((p8 address_to)raw + 16, key->round);
}

//      S2V(K1, part 1 .. part count, text), RFC 5297 2.4.
static fn crypto_siv_s2v(const crypto_siv_key address_to key,
                         const p8 address_to address_to parts,
                         const positive address_to lengths, positive count,
                         const p8 address_to text, positive text_length,
                         p8 address_to v)
{
        p8 zero[16];
        p8 d[16];
        p8 t[16];

        memory_fill(zero, 0, 16);
        crypto_cmac(address_of key->mac, zero, 16, null, d);
        for (positive at = 0; at < count; at++)
        {
                crypto_siv_double(d);
                crypto_cmac(address_of key->mac, parts[at], lengths[at], null, t);
                for (positive in = 0; in < 16; in++)
                        d[in] ^= t[in];
        }
        if (text_length >= 16)
                crypto_cmac(address_of key->mac, text, text_length, d, v);
        else
        {
                crypto_siv_double(d);
                memory_fill(t, 0, 16);
                memory_copy(t, text, text_length);
                t[text_length] = 0x80;
                for (positive in = 0; in < 16; in++)
                        t[in] ^= d[in];
                crypto_cmac(address_of key->mac, t, 16, null, v);
        }
        crypto_forget(d, sizeof d);
        crypto_forget(t, sizeof t);
}

//      Counter mode under the IV with its two bits cleared.
static fn crypto_siv_ctr(const crypto_siv_key address_to key,
                         const p8 address_to v, p8 address_to text,
                         positive length)
{
        p8 counter[16];
        p8 padded[16];
        positive whole = length / 16;
        positive rest = length % 16;

        memory_copy(counter, v, 16);
        counter[8] &= 0x7f;
        counter[12] &= 0x7f;
        aes128_ctr_blocks(key->round, counter, text, text, whole);
        if (rest)
        {
                memory_fill(padded, 0, 16);
                memory_copy(padded, text + whole * 16, rest);
                aes128_ctr_blocks(key->round, counter, padded, padded, 1);
                memory_copy(text + whole * 16, padded, rest);
        }
        crypto_forget(counter, sizeof counter);
        crypto_forget(padded, sizeof padded);
}

static fn crypto_siv_seal(const crypto_siv_key address_to key,
                          const p8 address_to address_to parts,
                          const positive address_to lengths, positive count,
                          p8 address_to text, positive length,
                          p8 address_to tag)
{
        crypto_siv_s2v(key, parts, lengths, count, text, length, tag);
        crypto_siv_ctr(key, tag, text, length);
}

static bool crypto_siv_open(const crypto_siv_key address_to key,
                            const p8 address_to address_to parts,
                            const positive address_to lengths, positive count,
                            p8 address_to text, positive length,
                            const p8 address_to tag)
{
        p8 v[16];
        bool valid;

        crypto_siv_ctr(key, tag, text, length);
        crypto_siv_s2v(key, parts, lengths, count, text, length, v);
        valid = crypto_same(v, tag, 16);
        if (!valid)
                crypto_forget(text, length);
        crypto_forget(v, sizeof v);
        return valid;
}

/*
        RFC 7748 X25519. lib.c's x25519 is the whole of the arithmetic --
        clamp, ladder, inversion, canonical encoding, its own scratch wiped
        -- on four 64-bit limbs on x86_64 and arm64 and five 51-bit ones on
        riscv64. What is left here is the verdict.
*/
static bool crypto_x25519(p8 address_to out, p8 address_to scalar, p8 address_to u)
{
        p8 nonzero = 0;

        x25519(out, scalar, u);

        /* RFC 7748's low-order inputs produce the all-zero shared secret.
           Returning its validity lets a protocol reject that public result
           without adding a second, easy-to-forget check at every caller. */
        for (positive i = 0; i < 32; i++)
                nonzero |= out[i];
        return nonzero != 0;
}

#define CRYPTO_FE_MAX 6
/* RSA moduli to 8,192 bits, as browsers take them (rsa8192.badssl.com).
   lib.c's montgomery_multiply stops at 64 limbs; crypto_rsa_multiply
   carries the rest. */
#define CRYPTO_RSA_LIMBS 128
#define CRYPTO_RSA_BYTES (CRYPTO_RSA_LIMBS * 8)
#define CRYPTO_RSA_EXPONENT_BITS 33 /* BoringSSL's kMaxExponentBits */

/* One, whose Montgomery product with an element in Montgomery form is the
   element as a plain integer. */
static const p64 crypto_unit[CRYPTO_RSA_LIMBS] = {1};

static const p64 crypto_p256_p[4] = {
    0xffffffffffffffffull, 0x00000000ffffffffull, 0x0000000000000000ull,
    0xffffffff00000001ull};
static const p64 crypto_p256_n[4] = {
    0xf3b9cac2fc632551ull, 0xbce6faada7179e84ull, 0xffffffffffffffffull,
    0xffffffff00000000ull};
static const p8 crypto_p256_gx_be[32] = {
    0x6b, 0x17, 0xd1, 0xf2, 0xe1, 0x2c, 0x42, 0x47, 0xf8, 0xbc, 0xe6, 0xe5,
    0x63, 0xa4, 0x40, 0xf2, 0x77, 0x03, 0x7d, 0x81, 0x2d, 0xeb, 0x33, 0xa0,
    0xf4, 0xa1, 0x39, 0x45, 0xd8, 0x98, 0xc2, 0x96};
static const p8 crypto_p256_gy_be[32] = {
    0x4f, 0xe3, 0x42, 0xe2, 0xfe, 0x1a, 0x7f, 0x9b, 0x8e, 0xe7, 0xeb, 0x4a,
    0x7c, 0x0f, 0x9e, 0x16, 0x2b, 0xce, 0x33, 0x57, 0x6b, 0x31, 0x5e, 0xce,
    0xcb, 0xb6, 0x40, 0x68, 0x37, 0xbf, 0x51, 0xf5};
static const p8 crypto_p256_b_be[32] = {
    0x5a, 0xc6, 0x35, 0xd8, 0xaa, 0x3a, 0x93, 0xe7, 0xb3, 0xeb, 0xbd, 0x55,
    0x76, 0x98, 0x86, 0xbc, 0x65, 0x1d, 0x06, 0xb0, 0xcc, 0x53, 0xb0, 0xf6,
    0x3b, 0xce, 0x3c, 0x3e, 0x27, 0xd2, 0x60, 0x4b};

static const p8 crypto_p384_gx_be[48] = {
    0xaa, 0x87, 0xca, 0x22, 0xbe, 0x8b, 0x05, 0x37, 0x8e, 0xb1, 0xc7, 0x1e,
    0xf3, 0x20, 0xad, 0x74, 0x6e, 0x1d, 0x3b, 0x62, 0x8b, 0xa7, 0x9b, 0x98,
    0x59, 0xf7, 0x41, 0xe0, 0x82, 0x54, 0x2a, 0x38, 0x55, 0x02, 0xf2, 0x5d,
    0xbf, 0x55, 0x29, 0x6c, 0x3a, 0x54, 0x5e, 0x38, 0x72, 0x76, 0x0a, 0xb7};
static const p8 crypto_p384_gy_be[48] = {
    0x36, 0x17, 0xde, 0x4a, 0x96, 0x26, 0x2c, 0x6f, 0x5d, 0x9e, 0x98, 0xbf,
    0x92, 0x92, 0xdc, 0x29, 0xf8, 0xf4, 0x1d, 0xbd, 0x28, 0x9a, 0x14, 0x7c,
    0xe9, 0xda, 0x31, 0x13, 0xb5, 0xf0, 0xb8, 0xc0, 0x0a, 0x60, 0xb1, 0xce,
    0x1d, 0x7e, 0x81, 0x9d, 0x7a, 0x43, 0x1d, 0x7c, 0x90, 0xea, 0x0e, 0x5f};
static const p8 crypto_p384_b_be[48] = {
    0xb3, 0x31, 0x2f, 0xa7, 0xe2, 0x3e, 0xe7, 0xe4, 0x98, 0x8e, 0x05, 0x6b,
    0xe3, 0xf8, 0x2d, 0x19, 0x18, 0x1d, 0x9c, 0x6e, 0xfe, 0x81, 0x41, 0x12,
    0x03, 0x14, 0x08, 0x8f, 0x50, 0x13, 0x87, 0x5a, 0xc6, 0x56, 0x39, 0x8d,
    0x8a, 0x2e, 0xd1, 0x9d, 0x2a, 0x85, 0xc8, 0xed, 0xd3, 0xec, 0x2a, 0xef};

static const p64 crypto_p384_p[6] = {
    0x00000000ffffffffull, 0xffffffff00000000ull, 0xfffffffffffffffeull,
    0xffffffffffffffffull, 0xffffffffffffffffull, 0xffffffffffffffffull};
static const p64 crypto_p384_n[6] = {
    0xecec196accc52973ull, 0x581a0db248b0a77aull, 0xc7634d81f4372ddfull,
    0xffffffffffffffffull, 0xffffffffffffffffull, 0xffffffffffffffffull};

/* Montgomery form.  An element a of Z/m is held as aR mod m, R = 2^(64n),
   so a product is reduced by n multiplies of its low limb by -1/m mod 2^64
   rather than by division.  Addition, subtraction, zero tests and equality
   read the same in either form; multiplication, the curve constant b and
   the edges where bytes come in or go out are where the form shows.  one is
   R mod m (the form of 1) and square is R^2 mod m (a*square reduces to aR).
   The constants were computed once from the moduli with exact integers. */
typedef struct
{
        positive n;
        p64 inverse;
        const p64 address_to m;
        const p64 address_to one;
        const p64 address_to square;
} crypto_field;

static const p64 crypto_p256_p_one[4] = {
    0x0000000000000001ull, 0xffffffff00000000ull, 0xffffffffffffffffull,
    0x00000000fffffffeull};
static const p64 crypto_p256_p_square[4] = {
    0x0000000000000003ull, 0xfffffffbffffffffull, 0xfffffffffffffffeull,
    0x00000004fffffffdull};
static const p64 crypto_p256_n_one[4] = {
    0x0c46353d039cdaafull, 0x4319055258e8617bull, 0x0000000000000000ull,
    0x00000000ffffffffull};
static const p64 crypto_p256_n_square[4] = {
    0x83244c95be79eea2ull, 0x4699799c49bd6fa6ull, 0x2845b2392b6bec59ull,
    0x66e12d94f3d95620ull};
static const p64 crypto_p384_p_one[6] = {
    0xffffffff00000001ull, 0x00000000ffffffffull, 0x0000000000000001ull,
    0x0000000000000000ull, 0x0000000000000000ull, 0x0000000000000000ull};
static const p64 crypto_p384_p_square[6] = {
    0xfffffffe00000001ull, 0x0000000200000000ull, 0xfffffffe00000000ull,
    0x0000000200000000ull, 0x0000000000000001ull, 0x0000000000000000ull};
static const p64 crypto_p384_n_one[6] = {
    0x1313e695333ad68dull, 0xa7e5f24db74f5885ull, 0x389cb27e0bc8d220ull,
    0x0000000000000000ull, 0x0000000000000000ull, 0x0000000000000000ull};
static const p64 crypto_p384_n_square[6] = {
    0x2d319b2419b409a9ull, 0xff3d81e5df1aa419ull, 0xbc3e483afcb82947ull,
    0xd40d49174aab1cc5ull, 0x3fb05b7a28266895ull, 0x0c84ee012b39bf21ull};

static const crypto_field crypto_p256_field = {
    4, 0x0000000000000001ull, crypto_p256_p, crypto_p256_p_one,
    crypto_p256_p_square};
static const crypto_field crypto_p256_order = {
    4, 0xccd1c8aaee00bc4full, crypto_p256_n, crypto_p256_n_one,
    crypto_p256_n_square};
static const crypto_field crypto_p384_field = {
    6, 0x0000000100000001ull, crypto_p384_p, crypto_p384_p_one,
    crypto_p384_p_square};
static const crypto_field crypto_p384_order = {
    6, 0x6ed46089e88fdc45ull, crypto_p384_n, crypto_p384_n_one,
    crypto_p384_n_square};

static fn crypto_fe_load_be(p64 address_to out, const p8 address_to bytes, positive n)
{
        positive i;

        for (i = 0; i < n; i++)
                out[n - 1 - i] = network_load_64(bytes + i * 8);
}

static fn crypto_fe_store_be(p8 address_to bytes, const p64 address_to in, positive n)
{
        positive i;

        for (i = 0; i < n; i++)
                network_store_64(bytes + i * 8, in[n - 1 - i]);
}

static bipolar crypto_fe_cmp(const p64 address_to a, const p64 address_to b,
                             positive n)
{
        positive i = n;

        while (i)
        {
                i--;
                if (a[i] > b[i])
                        return 1;
                if (a[i] < b[i])
                        return -1;
        }

        return 0;
}

/* Return the final borrow from a-b.  Unlike crypto_fe_cmp, this always walks
   every limb and is suitable for decisions derived from private points.
   d may be a. */
static p64 crypto_fe_subtract_raw(p64 address_to d,
                                  const p64 address_to a,
                                  const p64 address_to b, positive n)
{
        crypto_wide borrow = 0;

        for (positive i = 0; i < n; i++)
        {
                crypto_wide value = (crypto_wide)a[i] - b[i] - borrow;

                d[i] = (p64)value;
                borrow = (value >> 64) & 1;
        }

        return (p64)borrow;
}

static inline p64 crypto_fe_zero_bit(const p64 address_to a, positive n)
{
        //      Four limbs (P-256) and six (P-384) are straight lines; the
        //      scalar recoding's five and seven take the loop. Which one is
        //      the curve's, so nothing here depends on a value.
        p64 combined;

        if (n == 4)
                combined = a[0] | a[1] | a[2] | a[3];
        else if (n == 6)
                combined = a[0] | a[1] | a[2] | a[3] | a[4] | a[5];
        else
        {
                combined = 0;
                for (positive i = 0; i < n; i++)
                        combined |= a[i];
        }
        return ((combined | (0 - combined)) >> 63) ^ 1;
}

static fn crypto_fe_select(p64 address_to d, const p64 address_to a,
                           const p64 address_to b, positive n, p64 choose_b)
{
        p64 mask = 0 - choose_b;

        for (positive i = 0; i < n; i++)
                d[i] = (a[i] & ~mask) | (b[i] & mask);

        crypto_forget(address_of mask, sizeof mask);
}

static bool crypto_fe_is_zero(const p64 address_to a, positive n)
{
        return crypto_fe_zero_bit(a, n) != 0;
}

/* The two NIST field primes run on lib.c's p256_ and p384_ routines.
   Any other modulus -- the group orders -- and a crypto_field copied to
   another address add and subtract in the C below and multiply in
   lib.c's montgomery_multiply, which is how CHECK_net compares the
   field routines against the generic path. */
static fn crypto_fe_add(p64 address_to d, const p64 address_to a,
                        const p64 address_to b, const crypto_field address_to f)
{
        if (f == address_of crypto_p256_field)
        {
                p256_add(d, a, b);
                return;
        }
        if (f == address_of crypto_p384_field)
        {
                p384_add(d, a, b);
                return;
        }

        p64 sum[CRYPTO_FE_MAX];
        p64 reduced[CRYPTO_FE_MAX];
        crypto_wide carry = 0;
        positive n = f->n;
        p64 borrow;
        p64 reduce;

        for (positive i = 0; i < n; i++)
        {
                carry += (crypto_wide)a[i] + b[i];
                sum[i] = (p64)carry;
                carry >>= 64;
        }

        borrow = crypto_fe_subtract_raw(reduced, sum, f->m, n);
        reduce = (p64)carry | (borrow ^ 1);
        crypto_fe_select(d, sum, reduced, n, reduce);
        crypto_forget(sum, sizeof sum);
        crypto_forget(reduced, sizeof reduced);
        crypto_forget(address_of carry, sizeof carry);
        crypto_forget(address_of borrow, sizeof borrow);
        crypto_forget(address_of reduce, sizeof reduce);
}

static fn crypto_fe_sub(p64 address_to d, const p64 address_to a,
                        const p64 address_to b, const crypto_field address_to f)
{
        if (f == address_of crypto_p256_field)
        {
                p256_subtract(d, a, b);
                return;
        }
        if (f == address_of crypto_p384_field)
        {
                p384_subtract(d, a, b);
                return;
        }

        p64 difference[CRYPTO_FE_MAX];
        p64 restored[CRYPTO_FE_MAX];
        positive n = f->n;
        p64 borrow = crypto_fe_subtract_raw(difference, a, b, n);
        crypto_wide carry = 0;

        for (positive i = 0; i < n; i++)
        {
                carry += (crypto_wide)difference[i] + f->m[i];
                restored[i] = (p64)carry;
                carry >>= 64;
        }

        crypto_fe_select(d, difference, restored, n, borrow);
        crypto_forget(difference, sizeof difference);
        crypto_forget(restored, sizeof restored);
        crypto_forget(address_of borrow, sizeof borrow);
        crypto_forget(address_of carry, sizeof carry);
}

static fn crypto_fe_mul(p64 address_to d, const p64 address_to a,
                        const p64 address_to b, const crypto_field address_to f)
{
        if (f == address_of crypto_p256_field)
                p256_multiply(d, a, b);
        else if (f == address_of crypto_p384_field)
                p384_multiply(d, a, b);
        else
                montgomery_multiply(d, a, b, f->m, f->inverse, f->n);
}

static fn crypto_fe_sqr(p64 address_to d, const p64 address_to a,
                        const crypto_field address_to f)
{
        if (f == address_of crypto_p256_field)
                p256_square(d, a);
        else if (f == address_of crypto_p384_field)
                p384_square(d, a);
        else
                montgomery_multiply(d, a, a, f->m, f->inverse, f->n);
}

/* d = 1/a in Montgomery form, by Fermat: a^(m-2).  The exponent is the
   public modulus, so its 4-bit digits choose which table entry multiplies
   in; the table holds a^0..a^15, and the squarings and multiplies follow
   the modulus alone whatever a is.  a = 0 gives 0. */
static fn crypto_fe_inv(p64 address_to d, const p64 address_to a,
                        const crypto_field address_to f)
{
        p64 table[16][CRYPTO_FE_MAX];
        p64 exponent[CRYPTO_FE_MAX];
        p64 result[CRYPTO_FE_MAX];
        p64 two[CRYPTO_FE_MAX];
        positive n = f->n;
        positive bit = n * 64;

        memory_fill(two, 0, sizeof two);
        two[0] = 2;
        crypto_fe_subtract_raw(exponent, f->m, two, n);
        memory_copy(table[0], f->one, n * 8);
        memory_copy(table[1], a, n * 8);
        for (positive i = 2; i < 16; i++)
                crypto_fe_mul(table[i], table[i - 1], a, f);

        memory_copy(result, f->one, n * 8);
        while (bit)
        {
                positive digit;

                bit -= 4;
                if (bit != n * 64 - 4)
                        for (positive i = 0; i < 4; i++)
                                crypto_fe_sqr(result, result, f);
                digit = (positive)(exponent[bit / 64] >> (bit % 64)) & 15;
                if (digit)
                        crypto_fe_mul(result, result, table[digit], f);
        }

        memory_copy(d, result, n * 8);
        crypto_forget(table, sizeof table);
        crypto_forget(exponent, sizeof exponent);
        crypto_forget(result, sizeof result);
}

/* The word-array steps of crypto_fe_inv_public. */
static bool crypto_limbs_is_one(const p64 address_to x, positive n)
{
        p64 rest = 0;

        for (positive i = 1; i < n; i++)
                rest |= x[i];
        return x[0] == 1 && !rest;
}

/* x >> 1 with top entering the top bit. */
static fn crypto_limbs_halve(p64 address_to x, positive n, p64 top)
{
        for (positive i = 0; i < n; i++)
                x[i] = (x[i] >> 1) | (i + 1 < n ? x[i + 1] << 63 : top << 63);
}

/* x / 2 mod m for x below the odd m: m is added first when x is odd. */
static fn crypto_limbs_halve_mod(p64 address_to x, const p64 address_to m,
                                 positive n)
{
        crypto_wide carry = 0;

        if (x[0] & 1)
                for (positive i = 0; i < n; i++)
                {
                        carry += (crypto_wide)x[i] + m[i];
                        x[i] = (p64)carry;
                        carry >>= 64;
                }
        crypto_limbs_halve(x, n, (p64)carry);
}

/* x - y mod m for both below m. */
static fn crypto_limbs_sub_mod(p64 address_to x, const p64 address_to y,
                               const p64 address_to m, positive n)
{
        if (crypto_fe_subtract_raw(x, x, y, n))
        {
                crypto_wide carry = 0;

                for (positive i = 0; i < n; i++)
                {
                        carry += (crypto_wide)x[i] + m[i];
                        x[i] = (p64)carry;
                        carry >>= 64;
                }
        }
}

/* d = 1/a in Montgomery form for a plain a from 1 to m - 1, by the binary
   extended Euclid: shifts, subtractions and a halving mod m on four to six
   words, a few thousand word operations where the exponentiation above is
   about five hundred multiplies through the generic Montgomery routine.
   It branches on a, so only a public value may come here (a signature's s);
   a = 0 gives 0. */
static fn crypto_fe_inv_public(p64 address_to d, const p64 address_to a,
                               const crypto_field address_to f)
{
        p64 u[CRYPTO_FE_MAX], v[CRYPTO_FE_MAX];
        p64 x1[CRYPTO_FE_MAX], x2[CRYPTO_FE_MAX];
        positive n = f->n;

        if (crypto_fe_is_zero(a, n))
        {
                memory_fill(d, 0, n * 8);
                return;
        }
        memory_copy(u, a, n * 8);
        memory_copy(v, f->m, n * 8);
        memory_fill(x1, 0, sizeof x1);
        memory_fill(x2, 0, sizeof x2);
        x1[0] = 1;
        while (!crypto_limbs_is_one(u, n) && !crypto_limbs_is_one(v, n))
        {
                while (!(u[0] & 1))
                {
                        crypto_limbs_halve(u, n, 0);
                        crypto_limbs_halve_mod(x1, f->m, n);
                }
                while (!(v[0] & 1))
                {
                        crypto_limbs_halve(v, n, 0);
                        crypto_limbs_halve_mod(x2, f->m, n);
                }
                if (crypto_fe_cmp(u, v, n) >= 0)
                {
                        crypto_fe_subtract_raw(u, u, v, n);
                        crypto_limbs_sub_mod(x1, x2, f->m, n);
                }
                else
                {
                        crypto_fe_subtract_raw(v, v, u, n);
                        crypto_limbs_sub_mod(x2, x1, f->m, n);
                }
        }
        crypto_fe_mul(d, crypto_limbs_is_one(u, n) ? x1 : x2, f->square, f);
}

/* d = a^e in Montgomery form for an exponent e that is public and as wide as
   the field: a square every bit and a multiply where e has a one, so what
   is spent follows e and never a. SAE's square roots ((p + 1) / 4) and its
   test for a square take their exponents from the modulus alone. */
static fn crypto_fe_power(p64 address_to d, const p64 address_to a,
                          const p64 address_to e, const crypto_field address_to f)
{
        p64 result[CRYPTO_FE_MAX];
        positive n = f->n;

        memory_copy(result, f->one, n * 8);
        for (positive bit = n * 64; bit; bit--)
        {
                crypto_fe_sqr(result, result, f);
                if ((e[(bit - 1) / 64] >> ((bit - 1) % 64)) & 1)
                        crypto_fe_mul(result, result, a, f);
        }
        memory_copy(d, result, n * 8);
        crypto_forget(result, sizeof(result));
}

/* Jacobian points: (X, Y, Z) is the affine (X/Z^2, Y/Z^3), coordinates in
   the field's Montgomery form, and Z = 0 is infinity.  crypto_point_affine
   is the exit: it leaves x and y as plain integers with z = 1, for output
   and comparison only. */
typedef struct
{
        p64 x[CRYPTO_FE_MAX];
        p64 y[CRYPTO_FE_MAX];
        p64 z[CRYPTO_FE_MAX];
        positive n;
        const crypto_field address_to field;
} crypto_point;

static fn crypto_point_zero(crypto_point address_to q,
                            const crypto_field address_to f)
{
        memory_fill(q, 0, sizeof(*q));
        q->n = f->n;
        q->field = f;
}

/* x and y are plain integers below the field modulus. */
static fn crypto_point_set_xy(crypto_point address_to q, const p64 address_to x,
                              const p64 address_to y,
                              const crypto_field address_to f)
{
        crypto_point_zero(q, f);
        crypto_fe_mul(q->x, x, f->square, f);
        crypto_fe_mul(q->y, y, f->square, f);
        memory_copy(q->z, f->one, f->n * 8);
}

/* The Jacobian formulas' temporaries, one block a call: the private
   callers wipe it once, where wiping each field element was a fill a
   temporary, and the public ones need not. After a sum, h is U2 - U1 and
   rr is S2 - S1, which say whether the points were equal or opposite. */
typedef struct
{
        p64 z1z1[CRYPTO_FE_MAX], z2z2[CRYPTO_FE_MAX];
        p64 u1[CRYPTO_FE_MAX], u2[CRYPTO_FE_MAX];
        p64 s1[CRYPTO_FE_MAX], s2[CRYPTO_FE_MAX];
        p64 h[CRYPTO_FE_MAX], rr[CRYPTO_FE_MAX], hh[CRYPTO_FE_MAX];
        p64 hhh[CRYPTO_FE_MAX], v[CRYPTO_FE_MAX];
        p64 tmp[CRYPTO_FE_MAX], tmp2[CRYPTO_FE_MAX];
        crypto_point out;
} crypto_jacobian_work;

/* dbl-2001-b for a = -3, three multiplies and five squarings:
       delta = Z^2, gamma = Y^2, beta = X gamma,
       alpha = 3 (X - delta)(X + delta),
       X3 = alpha^2 - 8 beta,  Z3 = (Y + Z)^2 - gamma - delta,
       Y3 = alpha (4 beta - X3) - 8 gamma^2.
   Z = 0 gives Z3 = 0, so infinity doubles to itself with no branch.  The
   same operations run for every input, and r may be p. */
static fn crypto_point_double_formula(crypto_point address_to r,
                                      const crypto_point address_to p,
                                      crypto_jacobian_work address_to w)
{
        const crypto_field address_to f = p->field;
        p64 address_to delta = w->z1z1;
        p64 address_to gamma = w->z2z2;
        p64 address_to beta = w->u1;
        p64 address_to alpha = w->u2;
        p64 address_to tmp = w->tmp;
        p64 address_to x3 = w->out.x;
        p64 address_to y3 = w->out.y;
        p64 address_to z3 = w->out.z;

        crypto_fe_sqr(delta, p->z, f);
        crypto_fe_sqr(gamma, p->y, f);
        crypto_fe_mul(beta, p->x, gamma, f);

        crypto_fe_sub(tmp, p->x, delta, f);
        crypto_fe_add(w->tmp2, p->x, delta, f);
        crypto_fe_mul(alpha, tmp, w->tmp2, f);
        crypto_fe_add(tmp, alpha, alpha, f);
        crypto_fe_add(alpha, tmp, alpha, f);

        crypto_fe_add(z3, p->y, p->z, f);
        crypto_fe_sqr(z3, z3, f);
        crypto_fe_sub(z3, z3, gamma, f);
        crypto_fe_sub(z3, z3, delta, f);

        crypto_fe_add(beta, beta, beta, f);
        crypto_fe_add(beta, beta, beta, f);
        crypto_fe_sqr(x3, alpha, f);
        crypto_fe_add(tmp, beta, beta, f);
        crypto_fe_sub(x3, x3, tmp, f);

        crypto_fe_sub(tmp, beta, x3, f);
        crypto_fe_mul(y3, alpha, tmp, f);
        crypto_fe_sqr(tmp, gamma, f);
        crypto_fe_add(tmp, tmp, tmp, f);
        crypto_fe_add(tmp, tmp, tmp, f);
        crypto_fe_add(tmp, tmp, tmp, f);
        crypto_fe_sub(y3, y3, tmp, f);

        w->out.n = p->n;
        w->out.field = f;
        *r = w->out;
}

/* add-2007-bl without its exceptions: into w->out, with w->h and w->rr
   left for the caller that has to tell equal and opposite points apart.
   p and q are read only. */
static fn crypto_point_sum(crypto_jacobian_work address_to w,
                           const crypto_point address_to p,
                           const crypto_point address_to q)
{
        const crypto_field address_to f = p->field;

        crypto_fe_sqr(w->z1z1, p->z, f);
        crypto_fe_sqr(w->z2z2, q->z, f);
        crypto_fe_mul(w->u1, p->x, w->z2z2, f);
        crypto_fe_mul(w->u2, q->x, w->z1z1, f);
        crypto_fe_mul(w->tmp, q->z, w->z2z2, f);
        crypto_fe_mul(w->s1, p->y, w->tmp, f);
        crypto_fe_mul(w->tmp, p->z, w->z1z1, f);
        crypto_fe_mul(w->s2, q->y, w->tmp, f);

        crypto_fe_sub(w->h, w->u2, w->u1, f);
        crypto_fe_sub(w->rr, w->s2, w->s1, f);
        crypto_fe_sqr(w->hh, w->h, f);
        crypto_fe_mul(w->hhh, w->h, w->hh, f);
        crypto_fe_mul(w->v, w->u1, w->hh, f);

        crypto_fe_sqr(w->tmp, w->rr, f);
        crypto_fe_sub(w->tmp, w->tmp, w->hhh, f);
        crypto_fe_add(w->tmp2, w->v, w->v, f);
        crypto_fe_sub(w->out.x, w->tmp, w->tmp2, f);

        crypto_fe_sub(w->tmp, w->v, w->out.x, f);
        crypto_fe_mul(w->tmp2, w->rr, w->tmp, f);
        crypto_fe_mul(w->tmp, w->s1, w->hhh, f);
        crypto_fe_sub(w->out.y, w->tmp2, w->tmp, f);

        crypto_fe_mul(w->tmp, p->z, q->z, f);
        crypto_fe_mul(w->out.z, w->tmp, w->h, f);
        w->out.n = p->n;
        w->out.field = f;
}

/* Public points only: infinity returns at once. */
static fn crypto_point_double(crypto_point address_to r, crypto_point address_to p)
{
        crypto_jacobian_work w;

        if (crypto_fe_is_zero(p->z, p->n))
        {
                *r = *p;
                return;
        }
        crypto_point_double_formula(r, p, address_of w);
}

static fn crypto_point_add(crypto_point address_to r, crypto_point address_to p,
                           crypto_point address_to q)
{
        crypto_jacobian_work w;
        positive n = p->n;

        if (crypto_fe_is_zero(p->z, n))
        {
                *r = *q;
                return;
        }
        if (crypto_fe_is_zero(q->z, n))
        {
                *r = *p;
                return;
        }

        crypto_point_sum(address_of w, p, q);
        if (!crypto_fe_is_zero(w.h, n))
                *r = w.out;
        else if (crypto_fe_is_zero(w.rr, n))
                crypto_point_double(r, p);
        else
                crypto_point_zero(r, p->field);
}

/* The ECDH multiplier cannot use the public-signature helpers above: their
   exceptional-point branches and digit-conditional addition reveal a private
   scalar to a branch or cache observer.  These helpers select infinity cases
   with masks, and crypto_point_scalar_private says why no other exception
   reaches them.  Field reduction is likewise branchless, so every scalar
   follows the same operations and addresses. mask is all ones to take b. */
static fn crypto_point_select(crypto_point address_to d,
                              const crypto_point address_to a,
                              const crypto_point address_to b, p64 mask)
{
        for (positive i = 0; i < a->n; i++)
        {
                d->x[i] = (a->x[i] & ~mask) | (b->x[i] & mask);
                d->y[i] = (a->y[i] & ~mask) | (b->y[i] & mask);
                d->z[i] = (a->z[i] & ~mask) | (b->z[i] & mask);
        }
        d->n = a->n;
        d->field = a->field;
}

static fn crypto_point_double_private(crypto_point address_to r,
                                      const crypto_point address_to p)
{
        crypto_jacobian_work w;

        crypto_point_double_formula(r, p, address_of w);
        crypto_forget(address_of w, sizeof w);
}

static fn crypto_point_add_private(crypto_point address_to r,
                                   const crypto_point address_to p,
                                   const crypto_point address_to q)
{
        crypto_jacobian_work w;
        p64 p_infinity = 0 - crypto_fe_zero_bit(p->z, p->n);
        p64 q_infinity = 0 - crypto_fe_zero_bit(q->z, p->n);

        /* The window multiplier never adds equal points (see
           crypto_point_scalar_private), and opposite points already produce
           z=0.  Only the infinity cases need masked selection.  r may be
           p: nothing is written through it before the last line. */
        crypto_point_sum(address_of w, p, q);
        crypto_point_select(address_of w.out, address_of w.out, p, q_infinity);
        crypto_point_select(address_of w.out, address_of w.out, q, p_infinity);
        *r = w.out;

        crypto_forget(address_of w, sizeof w);
        crypto_forget(address_of p_infinity, sizeof p_infinity);
        crypto_forget(address_of q_infinity, sizeof q_infinity);
}

typedef struct
{
        positive bits;
        positive point_adds;
        positive point_doubles;
        positive conditional_swaps;
        positive conditional_selects;
} crypto_scalar_schedule;

/* k*P for a private k below the group order n, by 4-bit fixed windows.
   The table 0P..15P comes from the public P alone; each window then doubles
   four times and adds the entry its digit names, found by a masked scan of
   all sixteen, so the operations, their order and every address are the
   same for every k.  crypto_point_add_private selects its infinity cases
   with masks, and no other exception can arise: before a window's addition
   the accumulator is K*P, K being the integer value of k's higher windows
   times 16, and the entry is d*P with d below 16.  K = d (mod n) with
   K <= k < n holds only for K = d = 0, both infinity, and K = -d (mod n)
   would need K + d = n, above k.  The table's own additions are (j-1)P + P
   for j from 3, never equal points. */
static fn crypto_point_scalar_private(
    crypto_point address_to r, const crypto_point address_to p,
    const p64 address_to k, crypto_scalar_schedule address_to schedule)
{
        crypto_point table[16];
        crypto_point accumulator;
        crypto_point chosen;
        positive bits = p->n * 64;
        p64 digit = 0;

        if (schedule)
                memory_fill(schedule, 0, sizeof(*schedule));

        crypto_point_zero(address_of table[0], p->field);
        table[1] = *p;
        crypto_point_double_private(address_of table[2], p);
        for (positive j = 3; j < 16; j++)
                crypto_point_add_private(address_of table[j],
                                         address_of table[j - 1], p);
        if (schedule)
        {
                schedule->point_doubles = 1;
                schedule->point_adds = 13;
                schedule->conditional_selects = 13 * 2;
        }

        crypto_point_zero(address_of accumulator, p->field);
        for (positive at = bits; at;)
        {
                at -= 4;
                if (at != bits - 4)
                        for (positive i = 0; i < 4; i++)
                                crypto_point_double_private(
                                    address_of accumulator,
                                    address_of accumulator);
                digit = (k[at / 64] >> (at % 64)) & 15;
                chosen = table[0];
                for (p64 j = 1; j < 16; j++)
                        crypto_point_select(address_of chosen,
                                            address_of chosen,
                                            address_of table[j],
                                            0 - (((j ^ digit) - 1) >> 63));
                crypto_point_add_private(address_of accumulator,
                                         address_of accumulator,
                                         address_of chosen);

                if (schedule)
                {
                        schedule->bits += 4;
                        if (at != bits - 4)
                                schedule->point_doubles += 4;
                        schedule->point_adds++;
                        schedule->conditional_selects += 15 + 2;
                }
        }

        *r = accumulator;
        crypto_forget(table, sizeof table);
        crypto_forget(address_of accumulator, sizeof accumulator);
        crypto_forget(address_of chosen, sizeof chosen);
        crypto_forget(address_of digit, sizeof digit);
}

/* How many low bits of a nonzero x are zero, by halving the range: a public
   value, and no count-trailing-zeros that every architecture has. */
static positive crypto_trailing_zeros(p64 x)
{
        positive zeros = 0;

        for (positive step = 32; step; step >>= 1)
                if (!(x & (((p64)1 << step) - 1)))
                {
                        zeros += step;
                        x >>= step;
                }
        return zeros;
}

/* Width-w non-adjacent form of a public scalar below 2^(64 limbs): each
   digit is zero or odd in [-(2^(w-1) - 1), 2^(w-1) - 1], a nonzero digit is
   followed by at least w - 1 zeros, and the digits weighted by 2^i sum to k.
   Returns how many digits were written, at most 64 limbs + 1. */
static positive crypto_wnaf(b8 address_to digits, const p64 address_to k,
                            positive limbs, positive width)
{
        const p64 whole = (p64)1 << width;
        const p64 half = whole >> 1;
        p64 v[CRYPTO_FE_MAX + 1];
        positive count = 0;
        positive words = limbs + 1;

        memory_copy(v, k, limbs * 8);
        v[limbs] = 0;
        for (;;)
        {
                b8 digit;
                positive zeros;

                /* The zeros between digits go in one shift, and the scalar
                   is looked at for the end only when its low word is. */
                if (!v[0])
                {
                        if (crypto_fe_is_zero(v, words))
                                break;
                        for (positive i = 0; i < limbs; i++)
                                v[i] = v[i + 1];
                        v[limbs] = 0;
                        memory_fill(digits + count, 0, 64);
                        count += 64;
                        continue;
                }
                zeros = crypto_trailing_zeros(v[0]);
                if (zeros)
                {
                        for (positive i = 0; i + 1 < words; i++)
                                v[i] = (v[i] >> zeros) | (v[i + 1] << (64 - zeros));
                        v[words - 1] >>= zeros;
                        memory_fill(digits + count, 0, zeros);
                        count += zeros;
                }

                /* v is odd. */
                {
                        p64 low = v[0] & (whole - 1);

                        if (low >= half)
                        {
                                p64 carry = whole - low;

                                digit = (b8)((bipolar)low - (bipolar)whole);
                                for (positive i = 0; carry && i <= limbs; i++)
                                {
                                        v[i] += carry;
                                        carry = v[i] < carry;
                                }
                        }
                        else
                        {
                                digit = (b8)low;
                                v[0] -= low;
                        }
                }
                digits[count++] = digit;
                for (positive i = 0; i < limbs; i++)
                        v[i] = (v[i] >> 1) | (v[i + 1] << 63);
                v[limbs] >>= 1;
        }

        return count;
}

/* table[j] = (2j + 1) P for j below 8. */
static fn crypto_point_odd_multiples(crypto_point address_to table,
                                     crypto_point address_to p)
{
        crypto_point twice;

        table[0] = *p;
        crypto_point_double(address_of twice, p);
        for (positive j = 1; j < 8; j++)
                crypto_point_add(address_of table[j], address_of table[j - 1],
                                 address_of twice);
}

static fn crypto_point_add_digit(crypto_point address_to r,
                                 crypto_point address_to table, b8 digit)
{
        //      r is both the accumulator and the sum: crypto_point_add reads
        //      both points whole before it writes one. A positive digit adds
        //      its table entry where it stands; only a negative one needs a
        //      copy, to negate its y.
        if (digit > 0)
        {
                crypto_point_add(r, r, table + (digit >> 1));
                return;
        }

        crypto_point entry = table[(-digit) >> 1];
        p64 zero[CRYPTO_FE_MAX];

        memory_fill(zero, 0, sizeof zero);
        crypto_fe_sub(entry.y, zero, entry.y, entry.field);
        crypto_point_add(r, r, address_of entry);
}

/* g_tables begin (python3 test/differential.py --harness g_tables --write) */
/* (2j + 1) G for j below 32, affine, x then y in Montgomery form, as
   limbs, least significant first: ECDSA verify adds them to its
   accumulator by the mixed formulas with the width-7 digits of u1. */
static const p64 crypto_p256_g_table[32][8] = {
    {0x79e730d418a9143cull, 0x75ba95fc5fedb601ull, 0x79fb732b77622510ull, 0x18905f76a53755c6ull,
     0xddf25357ce95560aull, 0x8b4ab8e4ba19e45cull, 0xd2e88688dd21f325ull, 0x8571ff1825885d85ull},
    {0xffac3f904eebc127ull, 0xb027f84a087d81fbull, 0x66ad77dd87cbbc98ull, 0x26936a3fb6ff747eull,
     0xb04c5c1fc983a7ebull, 0x583e47ad0861fe1aull, 0x788208311a2ee98eull, 0xd5f06a29e587cc07ull},
    {0xbe1b8aaec45c61f5ull, 0x90ec649a94b9537dull, 0x941cb5aad076c20cull, 0xc9079605890523c8ull,
     0xeb309b4ae7ba4f10ull, 0x73c568efe5eb882bull, 0x3540a9877e7a1f68ull, 0x73a076bb2dd1e916ull},
    {0x0746354ea0173b4full, 0x2bd20213d23c00f7ull, 0xf43eaab50c23bb08ull, 0x13ba5119c3123e03ull,
     0x2847d0303f5b9d4dull, 0x6742f2f25da67bddull, 0xef933bdc77c94195ull, 0xeaedd9156e240867ull},
    {0x75c96e8f264e20e8ull, 0xabe6bfed59a7a841ull, 0x2cc09c0444c8eb00ull, 0xe05b3080f0c4e16bull,
     0x1eb7777aa45f3314ull, 0x56af7bedce5d45e3ull, 0x2b6e019a88b12f1aull, 0x086659cdfd835f9bull},
    {0xea7d260a6245e404ull, 0x9de407956e7fdfe0ull, 0x1ff3a4158dac1ab5ull, 0x3e7090f1649c9073ull,
     0x1a7685612b944e88ull, 0x250f939ee57f61c8ull, 0x0c0daa891ead643dull, 0x68930023e125b88eull},
    {0xccc425634b2ed709ull, 0x0e356769856fd30dull, 0xbcbcd43f559e9811ull, 0x738477ac5395b759ull,
     0x35752b90c00ee17full, 0x68748390742ed2e3ull, 0x7cd06422bd1f5bc1ull, 0xfbc08769c9e7b797ull},
    {0x72bcd8b7bc60055bull, 0x03cc23ee56e27e4bull, 0xee337424e4819370ull, 0xe2aa0e430ad3da09ull,
     0x40b8524f6383c45dull, 0xd766355442a41b25ull, 0x64efa6de778a4797ull, 0x2042170a7079adf4ull},
    {0x97091dcbd53c5c9dull, 0xf17624b6ac0a177bull, 0xb0f139752cfe2dffull, 0xc1a35c0a6c7a574eull,
     0x227d314693e79987ull, 0x0575bf30e89cb80eull, 0x2f4e247f0d1883bbull, 0xebd512263274c3d0ull},
    {0xfea912baa5659ae8ull, 0x68363aba25e1a16eull, 0xb8842277752c41acull, 0xfe545c282897c3fcull,
     0x2d36e9e7dc4c696bull, 0x5806244afba977c5ull, 0x85665e9be39508c1ull, 0xf720ee256d12597bull},
    {0x562e4cecc135b208ull, 0x74e1b2654783f47dull, 0x6d2a506c5a3f3b30ull, 0xecead9f4c16762fcull,
     0xf29dd4b2e286e5b9ull, 0x1b0fadc083bb3c61ull, 0x7a75023e7fac29a4ull, 0xc086d5f1c9477fa3ull},
    {0xf4f876532de45068ull, 0x37c7a7e89e2e1f6eull, 0xd0825fa2a3584069ull, 0xaf2cea7c1727bf42ull,
     0x0360a4fb9e4785a9ull, 0xe5fda49c27299f4aull, 0x48068e1371ac2f71ull, 0x83d0687b9077666full},
    {0xa4a319acd837879full, 0x6fc1b49eed6b67b0ull, 0xe395993332f1f3afull, 0x966742eb65432a2eull,
     0x4b8dc9feb4966228ull, 0x96cc631243f43950ull, 0x12068859c9b731eeull, 0x7b948dc356f79968ull},
    {0x042c2af497e2feb4ull, 0xd36a42d7aebf7313ull, 0x49d2c9eb084ffdd7ull, 0x9f8aa54b2ef7c76aull,
     0x9200b7ba09895e70ull, 0x3bd0c66fddb7fb58ull, 0x2d97d10878eb4cbbull, 0x2d431068d84bde31ull},
    {0x5e5db46acb66e132ull, 0xf1be963a0d925880ull, 0x944a70270317b9e2ull, 0xe266f95948603d48ull,
     0x98db66735c208899ull, 0x90472447a2fb18a3ull, 0x8a966939777c619full, 0x3798142a2a3be21bull},
    {0xe2f73c696755ff89ull, 0xdd3cf7e7473017e6ull, 0x8ef5689d3cf7600dull, 0x948dc4f8b1fc87b4ull,
     0xd9e9fe814ea53299ull, 0x2d921ca298eb6028ull, 0xfaecedfd0c9803fcull, 0xf38ae8914d7b4745ull},
    {0x871514560f664534ull, 0x85ceae7c4b68f103ull, 0xac09c4ae65578ab9ull, 0x33ec6868f044b10cull,
     0x6ac4832b3a8ec1f1ull, 0x5509d1285847d5efull, 0xf909604f763f1574ull, 0xb16c4303c32f63c4ull},
    {0xfd16847fdec67ef5ull, 0x742ee464233e76b7ull, 0x0b8e4134efc2b4c8ull, 0xca640b8642a3e521ull,
     0x653a01908ceb6aa9ull, 0x313c300c547852d5ull, 0x24e4ab126b237af7ull, 0x2ba901628bb47af8ull},
    {0x00467bc58cce08b5ull, 0xb636458c7f178d55ull, 0xc5748baea677d806ull, 0x2763a387dfa394ebull,
     0xa12b448a7d3cebb6ull, 0xe7adda3e6f20d850ull, 0xf63ebce51558462cull, 0x58b36143620088a8ull},
    {0xa9d89488a059c142ull, 0x6f5ae714ff0b9346ull, 0x068f237d16fb3664ull, 0x5853e4c4363186acull,
     0xe2d87d2363c52f98ull, 0x2ec4a76681828876ull, 0x47b864fae14e7b1cull, 0x0c0bc0e569192408ull},
    {0x624d60492ed22e91ull, 0x6fdfe0b56f072822ull, 0xeeca111539ce2271ull, 0x98100a4fdb01614full,
     0xb6b0daa2a35c628full, 0xb6f94d2ec87e9a47ull, 0xc67732591d57d9ceull, 0xf70bfeec03884a7bull},
    {0x4ff23ffd248a7d06ull, 0x80c5bfb4878873faull, 0xb7d9ad9005745981ull, 0x179c85db3db01994ull,
     0xba41b06261a6966cull, 0x4d82d052eadce5a8ull, 0x9e91cd3ba5e6a318ull, 0x47795f4f95b2dda0ull},
    {0x1ee426ccd5cd79bfull, 0x0032940b946c6e18ull, 0x1b1e8ae057477f58ull, 0xe94f7d346d823278ull,
     0xc747cb96782ba21aull, 0xc5254469f72b33a5ull, 0x772ef6dec7f80c81ull, 0xd73acbfe2cd9e6b5ull},
    {0x283c7513caa76097ull, 0x0a624fa936c83906ull, 0x6b20afec715af2c7ull, 0x4b969974eba78bfdull,
     0x220755ccd921d60eull, 0x9b944e107baeca13ull, 0x04819d515ded93d4ull, 0x9bbff86e6dddfd27ull},
    {0x21950b421ff6acd3ull, 0xffe7048453dc6909ull, 0xff4cd0b228766127ull, 0xabdbe6084fb7db2bull,
     0x837c92285e1109e8ull, 0x26147d27f4645b5aull, 0x4d78f592f7818ed8ull, 0xd394077ef247fa36ull},
    {0x508cec1c3b3f64c9ull, 0xe20bc0ba1e5edf3full, 0xda1deb852f4318d4ull, 0xd20ebe0d5c3fa443ull,
     0x370b4ea773241ea3ull, 0x61f1511c5e1a5f65ull, 0x99a5e23d82681c62ull, 0xd731e383a2f54c2dull},
    {0x97359638546c4d8dull, 0x5f9c3fc492f24679ull, 0x912e8beda8c8acd9ull, 0xec3a318d306634b0ull,
     0x80167f41c31cb264ull, 0x3db82f6f522113f2ull, 0xb155bcd2dcafe197ull, 0xfba1da5943465283ull},
    {0x258bbbf9e7305683ull, 0x31eea5bf07ef5be6ull, 0x0deb0e4a46c814c1ull, 0x5cee8449a7b730ddull,
     0xeab495c5a0182bdeull, 0xee759f879e27a6b4ull, 0xc2cf6a6880e518caull, 0x25e8013ff14cf3f4ull},
    {0x3ec832e77acaca28ull, 0x1bfeea57c7385b29ull, 0x068212e3fd1eaf38ull, 0xc13298306acf8cccull,
     0xb909f2db2aac9e59ull, 0x5748060db661782aull, 0xc5ab2632c79b7a01ull, 0xda44c6c600017626ull},
    {0x69d44ed65c46aa8eull, 0x2100d5d3a8d063d1ull, 0xcb9727eaa2d17c36ull, 0x4c2bab1b8add53b7ull,
     0xa084e90c15426704ull, 0x778afcd3a837ebeaull, 0x6651f7017ce477f8ull, 0xa062499846fb7a8bull},
    {0x3667eb1a7f4c04ccull, 0x59556621a9404f84ull, 0x71cdf6537eceb50aull, 0x994a44a69b8335faull,
     0xd7faf819dbeb9b69ull, 0x473c5680eed4350dull, 0xb6658466da44bba2ull, 0x0d1bc780872bdbf3ull},
    {0xb8d3d9319ff91fe5ull, 0x039c4800f0518eedull, 0x95c376329182cb26ull, 0x0763a43482fc568dull,
     0x707c04d5383e76baull, 0xac98b930824e8197ull, 0x92bf7c8f91230de0ull, 0x90876a0140959b70ull},
};
static const p64 crypto_p384_g_table[32][12] = {
    {0x3dd0756649c0b528ull, 0x20e378e2a0d6ce38ull, 0x879c3afc541b4d6eull, 0x6454868459a30effull, 0x812ff723614ede2bull, 0x4d3aadc2299e1513ull,
     0x23043dad4b03a4feull, 0xa1bfa8bf7bb4a9acull, 0x8bade7562e83b050ull, 0xc6c3521968f4ffd9ull, 0xdd8002263969a840ull, 0x2b78abc25a15c5e9ull},
    {0x05e4dbe6c1dc4073ull, 0xc54ea9fff04f779cull, 0x6b2034e9a170ccf0ull, 0x3a48d732d51c6c3eull, 0xe36f7e2d263aa470ull, 0xd283fe68e7c1c3acull,
     0x7e284821c04ee157ull, 0x92d789a77ae0e36dull, 0x132663c04ef67446ull, 0x68012d5ad2e1d0b4ull, 0xf6db68b15102b339ull, 0x465465fc983292afull},
    {0xbb595eba68f1f0dfull, 0xc185c0cbcc873466ull, 0x7f1eb1b5293c703bull, 0x60db2cf5aacc05e6ull, 0xc676b987e2e8e4c6ull, 0xe1bb26b11d178ffbull,
     0x2b694ba07073fa21ull, 0x22c16e2e72f34566ull, 0x80b61b3101c35b99ull, 0x4b237faf982c0411ull, 0xe6c5944024de236dull, 0x4db1c9d6e209e4a3ull},
    {0xdf13b9d17d69222bull, 0x4ce6415f874774b1ull, 0x731edcf8211faa95ull, 0x5f4215d1659753edull, 0xf893db589db2df55ull, 0x932c9f811c89025bull,
     0x0996b2207706a61eull, 0x135349d5a8641c79ull, 0x65aad76f50130844ull, 0x0ff37c0401fff780ull, 0xf57f238e693b0706ull, 0xd90a16b6af6c9b3eull},
    {0x2f5d200e2353b92full, 0xe35d87293fd7e4f9ull, 0x26094833a96d745dull, 0xdc351dc13cbfff3full, 0x26d464c6dad54d6aull, 0x5cab1d1d53636c6aull,
     0xf2813072b18ec0b0ull, 0x3777e270d742aa2full, 0x27f061c7033ca7c2ull, 0xa6ecaccc68ead0d8ull, 0x7d9429f4ee69a754ull, 0xe770633431e8f5c6ull},
    {0xc7708b19b68b8c7dull, 0x4532077c44377abaull, 0x0dcc67706cdad64full, 0x01b8bf56147b6602ull, 0xf8d89885f0561d79ull, 0x9c19e9fc7ba9c437ull,
     0x764eb146bdc4ba25ull, 0x604fe46bac144b83ull, 0x3ce813298a77e780ull, 0x2e070f36fe9e682eull, 0x41821d0c3a53287aull, 0x9aa62f9f3533f918ull},
    {0x9b7aeb7e75ccbdfbull, 0xb25e28c5f6749a95ull, 0x8a7a8e4633b7d4aeull, 0xdb5203a8d9c1bd56ull, 0xd2657265ed22df97ull, 0xb51c56e18cf23c94ull,
     0xf4d394596c3d812dull, 0xd8e88f1a87cae0c2ull, 0x789a2a48cf4d0fe3ull, 0xb7feac2dfec38d60ull, 0x81fdbd1c3b490ec3ull, 0x4617adb7cc6979e1ull},
    {0x446ad8884709f4a9ull, 0x2b7210e2ec3dabd8ull, 0x83ccf19550e07b34ull, 0x59500917789b3075ull, 0x0fc01fd4eb085993ull, 0xfb62d26f4903026bull,
     0x2309cc9d6fe989bbull, 0x61609cbd144bd586ull, 0x4b23d3a0de06610cull, 0xdddc2866d898f470ull, 0x8733fc41400c5797ull, 0x5a68c6fed0bc2716ull},
    {0x8903e1304b4a3cd0ull, 0x3ea4ea4c8ff1f43eull, 0xe6fc3f2af655a10dull, 0x7be3737d524ffefcull, 0x9f6928555330455eull, 0x524f166ee475ce70ull,
     0x3fcc69cd6c12f055ull, 0x4e23b6ffd5b9c0daull, 0x49ce6993336bf183ull, 0xf87d6d854a54504aull, 0x25eb5df1b3c2677aull, 0xac37986f55b164c9ull},
    {0x82a2ed4abaa84c08ull, 0x22c4cc5f41a8c912ull, 0xca109c3b154aad5eull, 0x23891298fc38538eull, 0xb3b6639c539802aeull, 0xfa0f1f450390d706ull,
     0x46b78e5db0dc21d0ull, 0xa8c72d3cc3da2eacull, 0x9170b3786ff2f643ull, 0x3f5a799bb67f30c3ull, 0x15d1dc778264b672ull, 0xa1d47b23e9577764ull},
    {0x08265e510422ce2full, 0x88e0d496dd2f9e21ull, 0x30128aa06177f75dull, 0x2e59ab62bd9ebe69ull, 0x1b1a0f6c5df0e537ull, 0xab16c626dac012b5ull,
     0x8014214b008c5de7ull, 0xaa740a9e38f17beaull, 0x262ebb498a149098ull, 0xb454111e8527cd59ull, 0x266ad15aacea5817ull, 0x21824f411353ccbaull},
    {0xd1b4e74d12e3683bull, 0x990ed20b569b8ef6ull, 0xb9d3dd25429c0a18ull, 0x1c75b8ab2a351783ull, 0x61e4ca2b905432f0ull, 0x80826a69eea8f224ull,
     0x7fc33a6bec52abadull, 0x0bcca3f0a65e4813ull, 0x7ad8a132a527cebeull, 0xf0138950eaf22c7eull, 0x282d2437566718c1ull, 0x9dfccb0de2212559ull},
    {0x1e93722758ce3b83ull, 0xbb280dfa3cb3fb36ull, 0x57d0f3d2e2be174aull, 0x9bd51b99208abe1eull, 0x3809ab50de248024ull, 0xc29c6e2ca5bb7331ull,
     0x9944fd2e61124f05ull, 0x83ccbc4e9009e391ull, 0x01628f059424a3ccull, 0xd6a2f51dea8e4344ull, 0xda3e1a3d4cebc96eull, 0x1fe6fb42e97809dcull},
    {0xa04482d2467d66e4ull, 0xcf1912934d78291dull, 0x8e0d4168482396f9ull, 0x7228e2d5d18f14d0ull, 0x2f7e8d509c6a58feull, 0xe8ca780e373e5aecull,
     0x42aad1d61b68e9f8ull, 0x58a6d7f569e2f8f4ull, 0xd779adfe31da1beaull, 0x7d26540638c85a85ull, 0x67e67195d44d3cdfull, 0x17820a0bc5134ed7ull},
    {0x019d6ac5d3021470ull, 0x25846b66780443d6ull, 0xce3c15ed55c97647ull, 0x3dc22d490e3feb0full, 0x2065b7cba7df26e4ull, 0xc8b00ae8187cea1full,
     0x1a5284a0865dded3ull, 0x293c164920c83de2ull, 0xab178d26cce851b3ull, 0x8e6db10b404505fbull, 0xf6f57e7190c82033ull, 0x1d2a1c015977f16cull},
    {0xa39c89317c8906a4ull, 0xb6e7ecdd9e821ee6ull, 0x2ecf8340f0df4fe6ull, 0xd42f7dc953c14965ull, 0x1afb51a3e3ba8285ull, 0x6c07c4040a3305d1ull,
     0xdab83288127fc1daull, 0xbc0a699b374c4b08ull, 0x402a9bab42eb20ddull, 0xd7dd464f045a7a1cull, 0x5b3d0d6d36beecc4ull, 0x475a3e756398a19dull},
    {0x61333a382fb3ba63ull, 0xdf330d9d5b943c86ull, 0xbbc7c7ee955ef3afull, 0xda631fc160f09efbull, 0x68af622641d5c400ull, 0xcc9e97a46c833e9dull,
     0x7fd73e8e3a625e76ull, 0x13bf6124c209e55eull, 0x08467cea48b90b91ull, 0x8a416eb9bb6f0abaull, 0x6fcc93a1b8c31072ull, 0xa7fd2b619057dad7ull},
    {0x58a5b5433720ec9bull, 0xbb3800d52d7c2fb4ull, 0x4a508620dde6bd0aull, 0x65f16273a02583fdull, 0x832bd8e34fc78523ull, 0xd6149f75e9417bc6ull,
     0xfeb026e93deeb52aull, 0x0ce18088a55e0956ull, 0x50018998988092a2ull, 0x22f19fab28f35eeeull, 0xac8a877f52ccd35cull, 0xb13a8ad830e23f26ull},
    {0x0202d57de44f61a3ull, 0x4027704bb5630ef2ull, 0xa129e2dff5b54a5dull, 0xacb60a7597482b86ull, 0x9261ede87ef27114ull, 0x1eba28f3defc58b5ull,
     0x6c91c0c98be5589eull, 0x2f1643d514594beeull, 0x2ea912435d2ca034ull, 0xb50649a894047d1full, 0x284fcbb5638ca337ull, 0xfa0e07b7fe85bf85ull},
    {0x7d894f80506e0e42ull, 0xd984244a8e3d2c46ull, 0x6d7edf642b7f006full, 0x36a1cd6dde9b6230ull, 0xc9985040b76c0665ull, 0x587df4d6b89b1fc2ull,
     0x4c0638476a71ae7aull, 0x7b2b0ab3e8294747ull, 0x345c553ab53153b8ull, 0xb646e453436d9fe2ull, 0x1a95355f1cd60340ull, 0x2d7bc128074968fbull},
    {0xad148e87bca6d14cull, 0x41dfd24d456a201eull, 0x73a82933a80d68f3ull, 0x89746c8d852ca035ull, 0xe3bc778895fd71aeull, 0x8764cd2cda92245dull,
     0xa2fe2c4782eb23e2ull, 0x5ac762e00f3c9d6eull, 0x57860ce121646f31ull, 0xbdc9d6c34f9f589aull, 0x679952c7d193272eull, 0x82ea702eeb18f1c5ull},
    {0x37fa935500846d44ull, 0x09112fc50578bc8cull, 0xdad9f5b239c4943dull, 0x7314f5f0416dbd86ull, 0x5cf095a901fefb56ull, 0x35178bad22dab393ull,
     0xcf79fc1b36baf1a7ull, 0x1b7ee42d749e5498ull, 0xbce78aa9ede314bbull, 0xaaf8e0f6bd0628dfull, 0xa974b09415cbf948ull, 0x8f3f1f63c9632b78ull},
    {0xd4c411564fddda5bull, 0xd4af65c673ad9112ull, 0xffe8e0bb39eb8f59ull, 0xb0040c0e8d6fcf13ull, 0x99e1c0c61f2bb599ull, 0x9c94c858b2ac3405ull,
     0x8f8878d76eeed85dull, 0x62b2f54351fcca3full, 0xeb3b44a9e5b56918ull, 0x16f96676b7234e93ull, 0x17477722bd2af19eull, 0x42eb2979db83a485ull},
    {0x6f888f7df0c668caull, 0x65c788785f0dc66cull, 0xbfb185125f5b07a0ull, 0x780abff7d878acd0ull, 0x504f21b1570cf950ull, 0xea5b37c5da233371ull,
     0x487ae8bd22437ed1ull, 0x9c701758249cf9b7ull, 0xf86562a898fb34ffull, 0xdfeea1a265e0fc91ull, 0xeef006912e20fc23ull, 0xac9dfec7dfa72a8bull},
    {0xfa5c3aef697136c6ull, 0x8ea5af63a5ea6fb8ull, 0xa669156542e365a4ull, 0x47c56c115b6e3386ull, 0x1197832bcea03f56ull, 0x0b470bb250e4ea9eull,
     0x3113c74313b25712ull, 0x8d6c174ed2497d48ull, 0xfc4486ee49c9ebe8ull, 0x2487edd57f82bdd3ull, 0x771e64415b57be2full, 0x2d1cc518e28b2bdbull},
    {0x2c4ccac72070ac8dull, 0x1947c0caec4a22b8ull, 0xa5e0fb598c5a78d9ull, 0x464ae8d241a84de7ull, 0x3dba16e9daaabc27ull, 0x16634a504f35cb3cull,
     0xadc18bf9b16ec84full, 0x324d067e7359dd35ull, 0xdaeac0c3570543f0ull, 0x0b2240003c887d36ull, 0xc69489e2373f1a0dull, 0x518b047dcbaa0d97ull},
    {0x3b1bddc6fbde49efull, 0xdaed7c268a0915ccull, 0x0b0110610f0422a2ull, 0xcf485c74a7c54b16ull, 0x642ec4e615c3aae2ull, 0xa8ba8f10e0f383eaull,
     0x2a2054b495618501ull, 0xebec6442089efa8bull, 0x5786a19a4e2fa83eull, 0xd2c71ad139069963ull, 0xadc93d9a481765e2ull, 0xedf2e3eb7ecc9485ull},
    {0xbcab5f60069f3367ull, 0xfd6622bc1718ec3cull, 0xa4fb7867e3a142d6ull, 0x6078d8bf085faeb3ull, 0xfa5cbfda60f4554full, 0xb3fcd5d1690cd408ull,
     0x4ebdee7d281f7884ull, 0x82af23aa180a63a7ull, 0x8de3107c3d079f61ull, 0x17c6b5cbbe2334f8ull, 0x6a91e73997d0fa06ull, 0x7460257314ceeed4ull},
    {0xb14ba61cf97f865cull, 0x73bae4c1694b8b0dull, 0xa14967dfac4bbf62ull, 0x1e9dd1509bf446e0ull, 0xc052f3eb1c99ceefull, 0x814d7fa07a78c189ull,
     0xa101a483ab74b05dull, 0x7788c258a1737b65ull, 0x0d60bab7e809a13cull, 0x8f427bc473c81d5bull, 0xd2e130552952c1fcull, 0x0a823b9a4b26df63ull},
    {0xaf467ce227bf64c9ull, 0xdfca6897f929974cull, 0x64473b595c322738ull, 0x96a917cf1ed0e315ull, 0x3703435b0de64db9ull, 0x9ba039679267b646ull,
     0xdf0c2aae3a522fbeull, 0x41bdb741b335eff0ull, 0xaccf2edd7b059703ull, 0x6fb34b3028463cceull, 0x96d9ba0bd9e3ca19ull, 0xff336f12504655c1ull},
    {0x48da1fd3fc60a6e0ull, 0x54fb5a34222241e8ull, 0x6035e34f772ae080ull, 0x5ff77ff2332982d0ull, 0x2366467300fe51fdull, 0xc93ea049ef6ba006ull,
     0x6640f1177d381266ull, 0x394d32cd6ae9f4acull, 0xe6a7885370d303ebull, 0x0dda19ffe5275767ull, 0xb0a6c77201466d23ull, 0xc4cc11451fc69829ull},
    {0xc5c0e6d7aaed89c0ull, 0x6ce8ead6149a1896ull, 0x7a50f7458c949f8full, 0xcd7e35f76e2b71aaull, 0xf6159e519a049f7aull, 0x1c9bf0b0f1e52d1eull,
     0x3bb6c1f518202c80ull, 0x8d3a5f621ecd7b1aull, 0x3bb034e888d17f19ull, 0xdc89bd4997d4048dull, 0xf5af7b8e3735df22ull, 0x52bb3712a0a689e8ull},
};
/* g_tables end */

/* The affine table of a curve's generator, by its field: 32 entries of x and
   y, or none. */
static const p64 address_to crypto_g_table(const crypto_field address_to f)
{
        if (f == address_of crypto_p256_field)
                return address_of crypto_p256_g_table[0][0];
        if (f == address_of crypto_p384_field)
                return address_of crypto_p384_g_table[0][0];
        return 0;
}

/* r = p + (qx, qy) for an affine point, or its negation: the general sum
   with Z2 = 1, which leaves out the square of z2, the products with it and
   with z2 cubed (eight multiplies and three squares where the general
   addition is eleven and five).  Public points only: infinity and the
   equal and opposite cases branch. */
static fn crypto_point_add_affine(crypto_point address_to r,
                                  crypto_point address_to p,
                                  const p64 address_to qx,
                                  const p64 address_to qy, bool negate)
{
        const crypto_field address_to f = p->field;
        crypto_jacobian_work w;
        p64 y2[CRYPTO_FE_MAX];
        positive n = f->n;

        if (negate)
        {
                p64 zero[CRYPTO_FE_MAX];

                memory_fill(zero, 0, sizeof zero);
                crypto_fe_sub(y2, zero, qy, f);
        }
        else
                memory_copy(y2, qy, n * 8);

        if (crypto_fe_is_zero(p->z, n))
        {
                memory_copy(r->x, qx, n * 8);
                memory_copy(r->y, y2, n * 8);
                memory_copy(r->z, f->one, n * 8);
                r->n = n;
                r->field = f;
                return;
        }

        crypto_fe_sqr(w.z1z1, p->z, f);
        crypto_fe_mul(w.u2, qx, w.z1z1, f);
        crypto_fe_mul(w.tmp, p->z, w.z1z1, f);
        crypto_fe_mul(w.s2, y2, w.tmp, f);
        crypto_fe_sub(w.h, w.u2, p->x, f);
        crypto_fe_sub(w.rr, w.s2, p->y, f);
        if (crypto_fe_is_zero(w.h, n))
        {
                if (crypto_fe_is_zero(w.rr, n))
                        crypto_point_double(r, p);
                else
                        crypto_point_zero(r, f);
                return;
        }
        crypto_fe_sqr(w.hh, w.h, f);
        crypto_fe_mul(w.hhh, w.h, w.hh, f);
        crypto_fe_mul(w.v, p->x, w.hh, f);

        crypto_fe_sqr(w.tmp, w.rr, f);
        crypto_fe_sub(w.tmp, w.tmp, w.hhh, f);
        crypto_fe_add(w.tmp2, w.v, w.v, f);
        crypto_fe_sub(w.out.x, w.tmp, w.tmp2, f);

        crypto_fe_sub(w.tmp, w.v, w.out.x, f);
        crypto_fe_mul(w.tmp2, w.rr, w.tmp, f);
        crypto_fe_mul(w.tmp, p->y, w.hhh, f);
        crypto_fe_sub(w.out.y, w.tmp2, w.tmp, f);

        crypto_fe_mul(w.out.z, p->z, w.h, f);
        w.out.n = n;
        w.out.field = f;
        *r = w.out;
}

/* u1 G + u2 Q for public scalars and points (below 2^(64 limbs)), in one
   chain of doublings: each scalar's digits add a precomputed odd multiple of
   its own point or its negation.  Q's are width 5 and built here (eight
   odd multiples); G's are width 7 and read from the affine table above, so
   no multiples are built for it and its additions are the cheaper mixed
   ones.  Another curve's generator takes the general path: Q's treatment,
   width 5 and a built table.  Everything may branch; nothing is secret. */
static fn crypto_point_double_scalar(crypto_point address_to r,
                                     crypto_point address_to g,
                                     const p64 address_to u1,
                                     crypto_point address_to q,
                                     const p64 address_to u2)
{
        b8 d1[CRYPTO_FE_MAX * 64 + 1];
        b8 d2[CRYPTO_FE_MAX * 64 + 1];
        crypto_point tg[8];
        crypto_point tq[8];
        const p64 address_to table = crypto_g_table(g->field);
        positive limbs = g->n;
        positive n1 = crypto_wnaf(d1, u1, limbs, table ? 7 : 5);
        positive n2 = crypto_wnaf(d2, u2, limbs, 5);
        positive at = n1 > n2 ? n1 : n2;

        crypto_point_zero(r, g->field);
        if (n1 && !table)
                crypto_point_odd_multiples(tg, g);
        if (n2)
                crypto_point_odd_multiples(tq, q);
        while (at)
        {
                at--;
                crypto_point_double(r, r);
                if (at < n1 && d1[at])
                {
                        b8 digit = d1[at];
                        bool negative = digit < 0;
                        positive entry = (positive)(negative ? -digit : digit) >> 1;

                        if (table)
                                crypto_point_add_affine(
                                    r, r, table + entry * 2 * limbs,
                                    table + entry * 2 * limbs + limbs, negative);
                        else
                                crypto_point_add_digit(r, tg, digit);
                }
                if (at < n2 && d2[at])
                        crypto_point_add_digit(r, tq, d2[at]);
        }
}

static fn crypto_point_affine(crypto_point address_to p)
{
        const crypto_field address_to f = p->field;
        p64 zinv[CRYPTO_FE_MAX], z2[CRYPTO_FE_MAX], z3[CRYPTO_FE_MAX];

        if (crypto_fe_is_zero(p->z, p->n))
                goto done;

        crypto_fe_inv(zinv, p->z, f);
        crypto_fe_sqr(z2, zinv, f);
        crypto_fe_mul(z3, z2, zinv, f);
        crypto_fe_mul(p->x, p->x, z2, f);
        crypto_fe_mul(p->y, p->y, z3, f);
        crypto_fe_mul(p->x, p->x, crypto_unit, f);
        crypto_fe_mul(p->y, p->y, crypto_unit, f);
        memory_fill(p->z, 0, sizeof p->z);
        p->z[0] = 1;

done:
        crypto_forget(zinv, sizeof zinv);
        crypto_forget(z2, sizeof z2);
        crypto_forget(z3, sizeof z3);
}

static bool crypto_scalar_from_int_be(p64 address_to out,
                                      const p8 address_to bytes,
                                      positive length, const p64 address_to n,
                                      positive limbs)
{
        p8 padded[48];
        p64 difference[CRYPTO_FE_MAX];
        p64 less;
        bool valid;

        if (!length || length > limbs * 8)
                return false;
        memory_fill(out, 0, limbs * 8);
        memory_fill(padded, 0, sizeof(padded));
        memory_copy(padded + limbs * 8 - length, bytes, length);
        crypto_fe_load_be(out, padded, limbs);
        less = crypto_fe_subtract_raw(difference, out, n, limbs);
        valid = (bool)((crypto_fe_zero_bit(out, limbs) ^ 1) & less);
        crypto_forget(padded, sizeof padded);
        crypto_forget(difference, sizeof difference);
        crypto_forget(address_of less, sizeof less);
        return valid;
}

static bool crypto_point_is_on_curve(const p8 address_to x_bytes,
                                     const p8 address_to y_bytes,
                                     const crypto_field address_to f,
                                     const p8 address_to b_bytes)
{
        p64 x[CRYPTO_FE_MAX], y[CRYPTO_FE_MAX], b[CRYPTO_FE_MAX];
        p64 left[CRYPTO_FE_MAX], right[CRYPTO_FE_MAX];
        p64 x2[CRYPTO_FE_MAX];
        positive limbs = f->n;

        crypto_fe_load_be(x, x_bytes, limbs);
        crypto_fe_load_be(y, y_bytes, limbs);
        crypto_fe_load_be(b, b_bytes, limbs);
        if (crypto_fe_cmp(x, f->m, limbs) >= 0 ||
            crypto_fe_cmp(y, f->m, limbs) >= 0)
                return false;

        /* NIST P-256 and P-384 both use y^2 = x^3 - 3x + b.  Affine
           infinity has no encoding, and (0,0) fails this equation.  Both
           sides are compared in Montgomery form, where equality is still
           equality. */
        crypto_fe_mul(x, x, f->square, f);
        crypto_fe_mul(y, y, f->square, f);
        crypto_fe_mul(b, b, f->square, f);
        crypto_fe_sqr(left, y, f);
        crypto_fe_sqr(x2, x, f);
        crypto_fe_mul(right, x2, x, f);
        crypto_fe_sub(right, right, x, f);
        crypto_fe_sub(right, right, x, f);
        crypto_fe_sub(right, right, x, f);
        crypto_fe_add(right, right, b, f);

        return crypto_fe_cmp(left, right, limbs) == 0;
}

/* Whether the affine x of a point, taken mod n, is r (below n), without the
   inversion that the affine form costs: a field exponentiation, a tenth of
   a verify.  x = X/Z^2 is r when X = r Z^2, and is r + n when that is below
   p and X = (r + n) Z^2: x is below p, so no other x reduces to r.  Both
   sides are in Montgomery form, which r is brought to by the field's R^2. */
static bool crypto_point_x_is(const crypto_point address_to point,
                              const p64 address_to r, const p64 address_to n)
{
        const crypto_field address_to f = point->field;
        positive limbs = f->n;
        p64 z2[CRYPTO_FE_MAX], candidate[CRYPTO_FE_MAX], scaled[CRYPTO_FE_MAX];
        crypto_wide carry = 0;

        crypto_fe_sqr(z2, point->z, f);
        crypto_fe_mul(candidate, r, f->square, f);
        crypto_fe_mul(scaled, candidate, z2, f);
        if (crypto_fe_cmp(scaled, point->x, limbs) == 0)
                return true;

        for (positive i = 0; i < limbs; i++)
        {
                carry += (crypto_wide)r[i] + n[i];
                candidate[i] = (p64)carry;
                carry >>= 64;
        }
        if (carry || crypto_fe_cmp(candidate, f->m, limbs) >= 0)
                return false;
        crypto_fe_mul(candidate, candidate, f->square, f);
        crypto_fe_mul(scaled, candidate, z2, f);
        return crypto_fe_cmp(scaled, point->x, limbs) == 0;
}

/* Everything here is public: the key, the signature and the digest.  The
   scalar multiplies may therefore branch on their bits. */
static bool crypto_ecdsa_verify(p8 address_to hash, positive hash_length,
                                p8 address_to r_bytes, positive r_length,
                                p8 address_to s_bytes, positive s_length,
                                p8 address_to qx, p8 address_to qy,
                                const crypto_field address_to field,
                                const crypto_field address_to order,
                                const p8 address_to gx, const p8 address_to gy,
                                const p8 address_to b)
{
        p64 r[CRYPTO_FE_MAX], s[CRYPTO_FE_MAX], e[CRYPTO_FE_MAX];
        p64 w[CRYPTO_FE_MAX], u1[CRYPTO_FE_MAX], u2[CRYPTO_FE_MAX];
        p64 gx_f[CRYPTO_FE_MAX], gy_f[CRYPTO_FE_MAX], qx_f[CRYPTO_FE_MAX],
            qy_f[CRYPTO_FE_MAX];
        crypto_point g, q, rpoint;
        positive limbs = field->n;
        p8 ehash[48];

        if (!crypto_scalar_from_int_be(r, r_bytes, r_length, order->m, limbs) ||
            !crypto_scalar_from_int_be(s, s_bytes, s_length, order->m, limbs) ||
            !crypto_point_is_on_curve(qx, qy, field, b))
                return false;

        memory_fill(ehash, 0, sizeof(ehash));
        if (hash_length >= limbs * 8)
                memory_copy(ehash, hash, limbs * 8);
        else
                memory_copy(ehash + limbs * 8 - hash_length, hash, hash_length);
        crypto_fe_load_be(e, ehash, limbs);
        while (crypto_fe_cmp(e, order->m, limbs) >= 0)
                crypto_fe_subtract_raw(e, e, order->m, limbs);

        /* w = 1/s in Montgomery form, so multiplying a plain e or r by it
           reduces straight to the plain e/s and r/s. */
        crypto_fe_inv_public(w, s, order);
        crypto_fe_mul(u1, e, w, order);
        crypto_fe_mul(u2, r, w, order);

        crypto_fe_load_be(gx_f, gx, limbs);
        crypto_fe_load_be(gy_f, gy, limbs);
        crypto_fe_load_be(qx_f, qx, limbs);
        crypto_fe_load_be(qy_f, qy, limbs);

        crypto_point_set_xy(address_of g, gx_f, gy_f, field);
        crypto_point_set_xy(address_of q, qx_f, qy_f, field);
        crypto_point_double_scalar(address_of rpoint, address_of g, u1,
                                   address_of q, u2);
        if (crypto_fe_is_zero(rpoint.z, limbs))
                return false;

        return crypto_point_x_is(address_of rpoint, r, order->m);
}

static bool crypto_ecdsa_p256(p8 address_to hash, positive hash_length,
                              p8 address_to r, positive r_length, p8 address_to s,
                              positive s_length, p8 address_to qx, p8 address_to qy)
{
        return crypto_ecdsa_verify(hash, hash_length, r, r_length, s, s_length,
                                   qx, qy, address_of crypto_p256_field,
                                   address_of crypto_p256_order,
                                   crypto_p256_gx_be, crypto_p256_gy_be,
                                   crypto_p256_b_be);
}

static bool crypto_ecdsa_p384(p8 address_to hash, positive hash_length,
                              p8 address_to r, positive r_length, p8 address_to s,
                              positive s_length, p8 address_to qx, p8 address_to qy)
{
        return crypto_ecdsa_verify(hash, hash_length, r, r_length, s, s_length,
                                   qx, qy, address_of crypto_p384_field,
                                   address_of crypto_p384_order,
                                   crypto_p384_gx_be, crypto_p384_gy_be,
                                   crypto_p384_b_be);
}

static bool crypto_scalar_reduce_be(p8 address_to out, const p8 address_to bytes,
                                    positive length, const p64 address_to order,
                                    positive limbs)
{
        p64 k[CRYPTO_FE_MAX];
        p64 reduced[CRYPTO_FE_MAX];
        p64 less;

        if (length != limbs * 8)
                return false;

        crypto_fe_load_be(k, bytes, limbs);
        less = crypto_fe_subtract_raw(reduced, k, order, limbs);
        crypto_fe_select(k, reduced, k, limbs, less);
        if (crypto_fe_is_zero(k, limbs))
        {
                crypto_forget(k, sizeof(k));
                crypto_forget(reduced, sizeof reduced);
                crypto_forget(address_of less, sizeof less);
                return false;
        }

        crypto_fe_store_be(out, k, limbs);
        crypto_forget(k, sizeof(k));
        crypto_forget(reduced, sizeof reduced);
        crypto_forget(address_of less, sizeof less);
        return true;
}

/*
        k G for the base points, by a fixed comb. With w = 5 teeth d bits
        apart (d = 52 for P-256, 77 for P-384), column j of the scalar is the
        five bits j, d + j, 2d + j, 3d + j and 4d + j, and

            k G = sum over columns j of 2^j T[column j],

        T[c] being the sum of 2^(t d) G over the set bits t of c. So the
        multiply is d doublings and d additions from one accumulator, where a
        variable base needs 64n doublings and 16n additions besides building
        its table: a key share is a third of the field operations.

        The 31 nonzero entries are affine in Montgomery form, computed once
        with exact integers (CHECK_net rebuilds every one from G through
        crypto_point_scalar_private). Entry 0 is infinity. Each column reads
        all 31 under masks, so the entry a secret column names leaves no
        trace in which addresses were read.

        The accumulator is homogeneous projective, (X : Y : Z) for (X/Z,
        Y/Z), and it is doubled and added with the complete formulas of
        Renes, Costello and Batina (eprint 2015/1060, algorithms 4 and 6 for
        a = -3): the same operations for every pair of points, infinity and
        equal points included, so there is no exceptional case to argue
        away and nothing to select around.
*/
/* p256: w = 5, d = 52 */
static const p64 crypto_p256_comb[31][2][4] = {
    {{0x79e730d418a9143cull, 0x75ba95fc5fedb601ull, 0x79fb732b77622510ull, 0x18905f76a53755c6ull},
     {0xddf25357ce95560aull, 0x8b4ab8e4ba19e45cull, 0xd2e88688dd21f325ull, 0x8571ff1825885d85ull}},
    {{0x83f49167ceca9754ull, 0x426d2cf64b7939a0ull, 0x2555e355723fd0bfull, 0xa96e6d06c4f144e2ull},
     {0x4768a8dd87880e61ull, 0x15543815e508e4d5ull, 0x09d7e772b1b65e15ull, 0x63439dd6ac302fa0ull}},
    {{0xf2675562a0be5d0eull, 0x4b524d254d1bb068ull, 0xbc2c5ff2a9b75b8cull, 0x4f326643d9a6f548ull},
     {0x50dd68441258835eull, 0x7d21beee676090e0ull, 0xb0b62c65f4a17b42ull, 0x60dfae28b3cec3b0ull}},
    {{0x20d3c982cf7d62d2ull, 0x1f36e29d23ba8150ull, 0x48ae0bf092763f9eull, 0x7a527e6b1d3a7007ull},
     {0xb4a89097581a85e3ull, 0x1f1a520fdc158be5ull, 0xf98db37d167d726eull, 0x8802786e1113e862ull}},
    {{0x531e7b64b113f918ull, 0x26b5d70a920a681dull, 0x04e52f8f24c37044ull, 0xbc7c9542bb7c375bull},
     {0xb63a044bf2e26375ull, 0xd842a342e922a3d0ull, 0x9eed2ecaa9292d57ull, 0xfe27d2c249ac7832ull}},
    {{0xedbd7944f24aab7eull, 0x56e51d9ecd1a1921ull, 0x11c63188962dae55ull, 0x37090565326acd14ull},
     {0xc436e587d71ed134ull, 0x3d96ac3aad89b461ull, 0xcdf570bcdcb718bbull, 0xaaa490e9dcfabde2ull}},
    {{0xb0ab54010b639942ull, 0xa6e12f5719379664ull, 0xc535f8b41d040abcull, 0xef255c54a75eef24ull},
     {0xb236f734aeceb0eaull, 0x38fcc8c19d879e2full, 0x674d8fdc180cacabull, 0x0a18bad4f624df06ull}},
    {{0x488f1185ca8d9d1aull, 0xadf2c77dd987ded2ull, 0x5f3039f060c46124ull, 0xe5d70b7571e095f4ull},
     {0x82d586506260e70full, 0x39d75ea7f750d105ull, 0x8cf3d0b175bac364ull, 0xf3a7564d21d01329ull}},
    {{0x83fc809160530d0aull, 0x58c24f527bc23dc8ull, 0xecde2f1fa653af5aull, 0xb2e2a374b10e511eull},
     {0xf0c54b329bebe1e4ull, 0x239c25dfade42270ull, 0xd866f55e9f22b433ull, 0x1e513ca2ed17efd3ull}},
    {{0x66313dc85bc98e0dull, 0xb13fe4e69a256888ull, 0x74816589ecd6e280ull, 0xdee13cde5ba88474ull},
     {0xae4e1872c53bc78dull, 0x9b79904a2f08a464ull, 0xef6e5ce29da51935ull, 0x9e58df82083c47eaull}},
    {{0x4e066713f5a32632ull, 0x431f75d44b36f498ull, 0x40ae279f70bd5f07ull, 0x252cdb93239ec23dull},
     {0xc18dddf87312a246ull, 0x5b77673c23a9e561ull, 0x020f09c31715fedeull, 0xabef6451a580cfc5ull}},
    {{0x3c8bc3bff2a0d962ull, 0x59f856ee3405a8aaull, 0x2fb6590cb3dc5948ull, 0xc8aa740ced85740eull},
     {0xf8081cfbe9aafe19ull, 0xf7d2e1f32534800dull, 0x355148c28d78d247ull, 0xaf0dc5a4d1557399ull}},
    {{0x34dfbfc4c7f68782ull, 0x2c6a80d608ac2685ull, 0x5479e1bc08d0255bull, 0x42eb9de09110c616ull},
     {0x97991dd810b4acbaull, 0xf36acc8f94d997c7ull, 0xd05ad78b69ddc036ull, 0x1ac7e528e68b4243ull}},
    {{0xdd9f8a00e82c8e2aull, 0x104b85c621f80126ull, 0x1997228d5b17a522ull, 0x706e5ec3923d0bd0ull},
     {0x00c6af271dc33622ull, 0xb3bc76c8271f09e1ull, 0xec1b7c0be36e325aull, 0x128200e268f12bfeull}},
    {{0x8e86cb3da8636d07ull, 0xc79c42ac2be46da2ull, 0xed70e08aaa01e0e1ull, 0x773579fce3b69272ull},
     {0xbc0fe5554d8464c3ull, 0x9e87a057cf54e071ull, 0xda655b0a3913b1d3ull, 0x052774d49a55dba4ull}},
    {{0x75d9bc15adf7cccfull, 0x81a3e5d6dfa1e1b0ull, 0x8c39e444249bc17eull, 0xf37dccb28ea7fd43ull},
     {0xda654873907fba12ull, 0x35daa6da4a372904ull, 0x0564cfc66283a6c5ull, 0xd09fa4f64a9395bfull}},
    {{0xb1f5c026e37542caull, 0x0b860cf372e01034ull, 0x3a7c10e4025289f2ull, 0xd2197d5f92901032ull},
     {0xfa06f835267ca2f6ull, 0x8fcb9a29bf6e43aaull, 0x465f6c117ed9f8e7ull, 0x8a50a5b3e6077aafull}},
    {{0xad76c703d2b59e85ull, 0x0a2306459204c53full, 0x9bbc0bc44a9f1335ull, 0x71603515d0a967e9ull},
     {0x8b6d6d6ea0205375ull, 0x6310418351ad76deull, 0x5abfbc21aabbd0acull, 0x61fb45c3c71f3060ull}},
    {{0x579345df1d323961ull, 0x45b79ead94cd3bc4ull, 0x50b664be423668d2ull, 0x19dd5b7542bc26eaull},
     {0xc7c1fbaa3677ae8full, 0x7b2e711a5d033158ull, 0x8aecb50a8942ac93ull, 0xe255438b8a16718cull}},
    {{0x8025364233396533ull, 0x82cb33a72c5ad150ull, 0x7c147998070ca168ull, 0x077912536aac6636ull},
     {0x160003ae7c78be24ull, 0xbba9fe68a30eeabfull, 0x16c31c403073f0edull, 0xd329cd28789caecaull}},
    {{0x840dbcbf7972bcdfull, 0xb5c8444fbd11900cull, 0x78b2b29016520ceeull, 0xe19f13a3be88d914ull},
     {0x052ddc8949d3c0dfull, 0xc9fc183ce0b4224bull, 0x2c8dd074cf31e0bbull, 0x872c7b95a26b1441ull}},
    {{0xed93585d74c8a327ull, 0xf2fb7d0806be87caull, 0x707d83ca84e36244ull, 0x037f499d3efa6833ull},
     {0xf3218d4299bf5ddeull, 0xbe0a81c069ff7ce3ull, 0x068fbbea9eb7d4c0ull, 0xf4ef6609e6938c78ull}},
    {{0x202e5c5acb22715eull, 0x88e93d23288f8243ull, 0xdf1d1f52dc7eace6ull, 0xc6b38b3b373183f8ull},
     {0x77798b7f3eac9c4bull, 0xa9d37dff6bfa9835ull, 0xaff4a447faac41c9ull, 0xf14fd13c0fcb6036ull}},
    {{0xef5ee27d49ccc093ull, 0x7ff3263d40d359a3ull, 0x885d1942c6d6c0eaull, 0x925abba328c97feeull},
     {0xd73834805d95f52dull, 0x6979981c4eb691dbull, 0x6544e8ae553a29c6ull, 0x28324ef85043559full}},
    {{0xd6c8e4b7300c0e39ull, 0x37ad4a1a3e37f58aull, 0x763330f5e5e8cdfbull, 0x62bf8c2c870ea133ull},
     {0x03fbc63a763ccac9ull, 0xc889d8a5fb1886c0ull, 0xf0486de5be49d9feull, 0xaf9a877862c23338ull}},
    {{0x8a43a2a176aa81b3ull, 0x896021298a0cc3d2ull, 0x49d311e8821f6640ull, 0x8035608f5c734ae4ull},
     {0xa7be0561349adc3bull, 0x328525b296a337b5ull, 0x575413c36bccf78aull, 0x6c7292ec4854960full}},
    {{0x121e6a713c2943ffull, 0x0468565c6374c47eull, 0xd66fe9932826f138ull, 0x4e2cfaf17748e3acull},
     {0xe9baaa2c4708a6c8ull, 0xa3845c8c66ffb5b4ull, 0xad3e293eb77c8facull, 0x00b5cfa9440a35e8ull}},
    {{0x3f55f58c63e06277ull, 0x1a81de8a64ba6e8cull, 0x85cfdc74f4cc043bull, 0x7cbefb98048d26e0ull},
     {0x5bde4b3c82aba891ull, 0x863d8f7586db6f46ull, 0xc7af5c1f845186c5ull, 0x41d7d404cb527cecull}},
    {{0x3b44699483e1a246ull, 0x11c5ced4f6b819a2ull, 0xc79d4660aff79a46ull, 0x423bbdc15f22411aull},
     {0x22652251a964039dull, 0x808d6753e738657bull, 0xc0ca19e34e909dc8ull, 0x0e036e4734ab0d07ull}},
    {{0x233593e77a26f742ull, 0xddc1c79ffc0f14d9ull, 0xb33c89802d359358ull, 0x51df6155730aacfeull},
     {0xa9a6066c0f2c0b8dull, 0xb92122272e706f80ull, 0x3994a53296a5efe9ull, 0xcf3d168b52316b12ull}},
    {{0xbe47dd5027eafcc0ull, 0x23df1041ec7e66dbull, 0x18c977ff78a4ddddull, 0xb51565d79d2d152eull},
     {0x24f6a6d578f4a4deull, 0xbbc15b207d86b2caull, 0xa064d39c1d3b43caull, 0x5524866752200839ull}},
};
static const p64 crypto_p256_b_mont[4] = {0xd89cdf6229c4bddfull, 0xacf005cd78843090ull, 0xe5a220abf7212ed6ull, 0xdc30061d04874834ull};
/* p384: w = 5, d = 77 */
static const p64 crypto_p384_comb[31][2][6] = {
    {{0x3dd0756649c0b528ull, 0x20e378e2a0d6ce38ull, 0x879c3afc541b4d6eull, 0x6454868459a30effull, 0x812ff723614ede2bull, 0x4d3aadc2299e1513ull},
     {0x23043dad4b03a4feull, 0xa1bfa8bf7bb4a9acull, 0x8bade7562e83b050ull, 0xc6c3521968f4ffd9ull, 0xdd8002263969a840ull, 0x2b78abc25a15c5e9ull}},
    {{0x6bd2c54d4cb89afaull, 0xe78c8bfa36527751ull, 0x27f52654e3eee747ull, 0x56f205839598d907ull, 0x5f91c2d027cb3712ull, 0xc501819fa3e33c5bull},
     {0x248490aa4eded738ull, 0xde7ac94427789065ull, 0x20138b3d74f7d38bull, 0xae791f602fb60214ull, 0x6b4fb300bd033d4eull, 0xc69c25d9bdfd1f17ull}},
    {{0xeeacf664b8557d82ull, 0xa57429a94c77cc70ull, 0x59a603b7696b990aull, 0xb43391f64beac9a3ull, 0xd5c3a162c8d57758ull, 0x98017c1cf2f7c3b4ull},
     {0xff2cd9a2468332cbull, 0xaedbd85892a2368dull, 0x03f49686d52ec2e3ull, 0x84d8de683ee6933bull, 0xac7ed137b7b6aca2ull, 0x5d2602277b48d6d2ull}},
    {{0xd25f650804926a41ull, 0x7236b475514045daull, 0x0b36031108b9b08bull, 0x16477aff3fe92e91ull, 0x6e5f6cb103189ddcull, 0x81ff008ec698a38full},
     {0x02a09218c93adb23ull, 0x71fcecd3445d8faeull, 0x55a15eac8fd6b76cull, 0x1e37ec3611ef96b4ull, 0xd1b3b3fc30e433b5ull, 0x4951873351d174c3ull}},
    {{0x24e07819523a8bb4ull, 0x7b2772319833d8e5ull, 0x3c471ddcb04699b8ull, 0x33b27f71bd8508a4ull, 0x41731cca84e5dc2full, 0x46e02a9b0397e396ull},
     {0x2e70a031cc9f27fcull, 0x542eb3d9a7c4152bull, 0xb966c93047867367ull, 0x4387e233c0166702ull, 0xd3e8b42352195b20ull, 0x12b79efe825865f7ull}},
    {{0xf44626fa1f21ae46ull, 0x507a10427193c826ull, 0xc954dbd3332e4497ull, 0x0fc7e409011fe64eull, 0xbf09d38535201839ull, 0x2aca87f8e3f14d65ull},
     {0x664824aafa84b3c2ull, 0x660357c6f4d30784ull, 0x46b5cab5760eb676ull, 0xa55d8983118a70adull, 0xec5b8d9eaa1d5a74ull, 0xae60a033d09ff302ull}},
    {{0x17763bec03b8929dull, 0x20f9df436f4537d7ull, 0xb442a2783f50ef47ull, 0xf3450eda37bae3ecull, 0xb2c15f200fb1329cull, 0x0da364e545e635bbull},
     {0x9d46f6eb475d7731ull, 0xa2fea526edaa9406ull, 0x486e559426571ef5ull, 0x6d401ce9d1b5b927ull, 0xf2b65ecd13da4190ull, 0xda91acf3974de435ull}},
    {{0x406a7e2116960728ull, 0xd03923f85597d8c4ull, 0xd4402eff020748eeull, 0x7827442af39b58dbull, 0x77e3f2768d8cfb04ull, 0xf6eb49c8e45a978full},
     {0x9db0829949247f6aull, 0xce71a74706669fe5ull, 0xe434ce47b82775f5ull, 0xe84995ef63910016ull, 0xa35e8b971e47792full, 0xc779cb3d7c6aaeb9ull}},
    {{0x17bcf9791d424a0cull, 0x4b54b3ed8fefd7b7ull, 0x9f7741e71993315dull, 0x82289c8fa5fc44fdull, 0x8dd8bd79711c4b69ull, 0xe53aaa71722c2f98ull},
     {0x83fca7a8fea26a59ull, 0xaefc892caa73159aull, 0xd5a3fb551633ce08ull, 0xf9db2796f51b137cull, 0xadac646ec3a15474ull, 0xc8f4bccf487214b2ull}},
    {{0xf96de0a85cf00041ull, 0xe7d22cf3bf0a9b63ull, 0x004a9fd05db53399ull, 0xd6748c0f7b83975full, 0x7ed1adf83ac4997full, 0x0f0d6e5e845c29c7ull},
     {0x25b54b834a4b2fa3ull, 0xc20dcf306611b046ull, 0x4aa75a3e1b5eef89ull, 0x34a9ccc268e9c563ull, 0xef515f4f75f4e0a7ull, 0x074a9631abfc4949ull}},
    {{0x6a5c134e80e21ac0ull, 0x5b575f0f1d09e6cdull, 0x7e706cc39fad109aull, 0xf2d4b4d418a54de9ull, 0xaf89472f76d52417ull, 0xf853d14caa027ec1ull},
     {0xd0238fec1a9cc3e3ull, 0xc96dce810f41b4ceull, 0xd8cf075406582da2ull, 0xd5144307c929e254ull, 0x0473761c6ad1a72full, 0x810efc024adcdbf8ull}},
    {{0x2871e6af60d9f404ull, 0xa643672aa63075f5ull, 0x21cf2466979a48baull, 0xf55a914b6c74ec64ull, 0xe3fc17135b549c86ull, 0x0e0852964a82b64aull},
     {0x3392d5a0e029432eull, 0x72f18333dd25ed6full, 0xa26000888f41a56eull, 0x3f52ed20c993579cull, 0x168e5da15a1059aaull, 0x0d85a6437ef52b1aull}},
    {{0x64b9c787f0fdba0aull, 0x27889c73d2d72e13ull, 0x428d200d94c67aefull, 0x7124acca0d57dae2ull, 0xa5b0e6ef7c13e8e2ull, 0x21446337c2060717ull},
     {0xc83b2175ae5b038full, 0x8c2456459271754bull, 0x27bbcb0b150ecf0bull, 0x819e31c701995fa7ull, 0xb55d0888e2ce0e64ull, 0xf6baffd5032d81bcull}},
    {{0x12dee15308a0bb6eull, 0x7ef969fa420c3f7dull, 0xddab08ba9680a92aull, 0xe40bc1edfce0bdeaull, 0x29c72e956fac2134ull, 0x277c5495bb6c0418ull},
     {0xaf288e3f6eb38e44ull, 0xd39db2da026e757aull, 0x69d4fd06047c2172ull, 0x73781beb5e66032cull, 0xfcfe4643651b635cull, 0x2848bc61a086fcb6ull}},
    {{0xb04ace6c5dc9aef7ull, 0x80be1d0f3ee5cc6bull, 0xab9eddcc173feb36ull, 0x03e943c59e8c5575ull, 0x2f6360002199a881ull, 0x7683f4b291f8477eull},
     {0xc64af2318a7a5570ull, 0xdf464e4e8d485378ull, 0x394b6eeca8ca5639ull, 0x44c7ab2dad695607ull, 0x39f1e0876a00750cull, 0x98debda976b8ff8full}},
    {{0x6858b674844626a2ull, 0x610cd40f0cbba6a6ull, 0x324e674e29d9194dull, 0x2dc6fdf6dcb30a51ull, 0x3f3ecb77528aa549ull, 0x0721f8f923ffaa92ull},
     {0xd8efcbd627a77538ull, 0xf4e642bfd6162c9cull, 0x04f2b0b74cf4a16full, 0xbc0bb49fbbf335fdull, 0xc6b6e5bd5a928c36ull, 0x981b01f4d893dd45ull}},
    {{0x6fc651a5cc7288b7ull, 0x2231781f69470dcdull, 0x2aa15b2a92a93fdbull, 0x6eafb0268cdd7a14ull, 0x2af2a07585e28035ull, 0xe8c6303d29f2fbbcull},
     {0x63a2bd809a4d68e5ull, 0x0f8cc5e681c70549ull, 0xe4b37730b6630ba8ull, 0x1717f787c506d3c3ull, 0xf3cfb275c90c4476ull, 0x897451d4aacf2b36ull}},
    {{0x2606938f14f47a5aull, 0x059270a9cd29a96bull, 0xd57c69d69d42a8ffull, 0xf8bd35d927b148cdull, 0xbe327acd320ae33eull, 0x822559924240a328ull},
     {0xac0caddb7a929bbcull, 0x7d07c83de7e596a4ull, 0x54c27dd7487ba67full, 0x65e205d84ebaa953ull, 0x5028c6739218b3dbull, 0x1438558f9616b4ceull}},
    {{0x4e0fb44671f74c5eull, 0x5994b6919cd7c0f9ull, 0x724119e35924f26eull, 0x5e478acb5f65b033ull, 0x9279712187b8ad73ull, 0x48b4ba0999a9406cull},
     {0x9e00e38fae43e531ull, 0x6353a1ae5eb84112ull, 0x15cbdb3e2f7f1b08ull, 0xf3c346af058474f8ull, 0x56d7372b172639b4ull, 0xb452981968ccbdd1ull}},
    {{0x3aa00680cda4aa1cull, 0x236b1bd6cf590b9cull, 0xf097f0d924e8543full, 0xd270ecda4a82fabcull, 0xb474ed66981eebe1ull, 0x896654c1c87091ebull},
     {0x06598a25347cccb3ull, 0x8a66764b1a39bd51ull, 0x5d39d7244521b103ull, 0x6e40cc84c241ae61ull, 0x684bd7bbc17b7e0cull, 0x0205d53d55c2f43aull}},
    {{0x3b9fab5c82aa754full, 0xede2acbcb63f2789ull, 0xe70a749af2e1bf1eull, 0xbcdb8a8936bcbd2aull, 0x97f9788460f0cdd5ull, 0x1e5a7693545741cfull},
     {0x7774b94a5c387ea3ull, 0xbfc98d0a10b11fc5ull, 0x95d72afa1cc36c54ull, 0xc79301f73526ba51ull, 0x003659c3e8280d62ull, 0x3b8cc4d27e94410eull}},
    {{0x6731cde789b404bbull, 0xca01453e3d13be6full, 0x33f4fca55c45289aull, 0x32406a7ebc3eddefull, 0x9c62dad4df48c659ull, 0xe31057d9f77f6e46ull},
     {0xd64754a3e842a7c4ull, 0x4030038faca384bbull, 0xbc06591e4c779c63ull, 0xf497cd7487333cd9ull, 0x214e23b2d99bcb32ull, 0x8c04d0dfc17e7b91ull}},
    {{0x764bfc297ee98cd8ull, 0x83d6ff922b3dfe54ull, 0x73f8cdffb6a14d72ull, 0xf26db00a52e4b958ull, 0xd8e093961a855031ull, 0xec65e759920709f6ull},
     {0xd8377f2c3c64b2e0ull, 0x8dc13be4247240f4ull, 0x5401171a8cbbbd67ull, 0x610ef2f53ab670ebull, 0x98b544ff3bfc675full, 0x548cfcc2d259ffa4ull}},
    {{0x278dd1a38b800e7eull, 0x9bbfdc668dc767b0ull, 0xdaf9524297ea976aull, 0xe406a78db6e64692ull, 0x4b52bd3e83315a4eull, 0x3f9baa35b78f2013ull},
     {0x7f2e5c4d03f9b999ull, 0x34c3d16e46693732ull, 0xd3acba57244e3140ull, 0x5a07c1a54bb75e8dull, 0x2a7e4a32832d8b8full, 0xf63b74bff3a2f27cull}},
    {{0xa0b4d7b3fe28811cull, 0x5d05eccaef1a552full, 0x66fb43377f360449ull, 0xb210953e598aa6d4ull, 0x4be1df9bb6c1759aull, 0x16376676e5c4ec1aull},
     {0x7ab4af2b807bfaa2ull, 0xa6c43ef769ea556aull, 0x928ebb6fac9f05fdull, 0x8d6f436a0b0151a4ull, 0xb8aeed95f4e3448aull, 0x9fcc0d549f7953a5ull}},
    {{0xd1670e87a907c752ull, 0xf780541c239d26c1ull, 0x0be42d52ca8c9d97ull, 0x2f32f6882e806104ull, 0x39276b792be876dfull, 0x263b768a8c86a4cdull},
     {0xb3ef317006de686dull, 0x5ecfee99a6b652f8ull, 0x506d7abbe4ee473eull, 0x2174c181cecaa329ull, 0x60520a237eb51ed2ull, 0xd66712e4b39d6ebfull}},
    {{0xf88a910f6ae4e3ebull, 0x988f3bffa31342c4ull, 0x7baed96d79eeb886ull, 0xf3b6651159db12b9ull, 0x20314a0790638ffaull, 0x1c0ffe17c88ef37full},
     {0xd852f9860b74180aull, 0x6d68989b9fbb4924ull, 0x6c7dd8c5c078fa8dull, 0x7bded43ce741e6a2ull, 0x78c2bb1a98b878a3ull, 0xad714af957eaf758ull}},
    {{0x8088d680272e2db3ull, 0x8ff19a332900ffeeull, 0xbf32bffc3d3816bfull, 0xdae67e27133c5433ull, 0x9ae0cb219d09873eull, 0x33a716aa0f23cf9bull},
     {0xfb304095aa4f004bull, 0x7223a55966a74777ull, 0x87d4255397ae25baull, 0x7548c9d393f48840ull, 0x1f09b4f42ea6c117ull, 0x2125c0e225e5a579ull}},
    {{0x73f810552a219e96ull, 0x64c908d90f7ff162ull, 0xb064cf59d6ad4c6full, 0x3369dafb620664dbull, 0x726b5b472746205cull, 0x8cb469fe8318b089ull},
     {0x8cd34046d11c3476ull, 0xcb2d1330fcfc4dd9ull, 0x9b047d4f30b696f8ull, 0x95c268c2d4c18696ull, 0x4daaba707945a339ull, 0xda75e6ebc93a144aull}},
    {{0xe7958aa865c7af2eull, 0x9dcedd6271707194ull, 0x65d3ca5728c83ea1ull, 0x1741d2c1b90c08a1ull, 0x0b30c45f29f4efc3ull, 0xcc0efadbb3b6f4efull},
     {0x4e933280bf377698ull, 0x7ae10eb1d02bbbd3ull, 0x55fc031936af2a83ull, 0xc7e995616f788466ull, 0x4cb4d959dc367d35ull, 0x887c09498fb9c9cfull}},
    {{0x6102ffcde90d88a4ull, 0xfd7d8998f91aabf0ull, 0x1892ad594dcc3324ull, 0xe79856b96838bb98ull, 0x4c507c9318ff21f4ull, 0x02db41d83c088e65ull},
     {0xd51364567a1a7b21ull, 0x2b4c8d12b838f844ull, 0x0389b4d2f9bfa274ull, 0x9f63c44798677986ull, 0xe0686040114b36f5ull, 0xe5acfc3ada4ac299ull}},
};
static const p64 crypto_p384_b_mont[6] = {0x081188719d412dccull, 0xf729add87a4c32ecull, 0x77f2209b1920022eull, 0xe3374bee94938ae2ull, 0xb62b21f41f022094ull, 0xcd08114b604fbff9ull};

typedef struct
{
        p64 x[CRYPTO_FE_MAX];
        p64 y[CRYPTO_FE_MAX];
        p64 z[CRYPTO_FE_MAX];
} crypto_projective;

//      One block of temporaries, wiped once, as the Jacobian formulas do.
typedef struct
{
        p64 t0[CRYPTO_FE_MAX], t1[CRYPTO_FE_MAX], t2[CRYPTO_FE_MAX];
        p64 t3[CRYPTO_FE_MAX], t4[CRYPTO_FE_MAX];
        p64 x3[CRYPTO_FE_MAX], y3[CRYPTO_FE_MAX], z3[CRYPTO_FE_MAX];
} crypto_projective_work;

/* r = 2 p; r may be p. */
static fn crypto_projective_double(crypto_projective address_to r,
                                   const crypto_projective address_to p,
                                   const p64 address_to b,
                                   const crypto_field address_to f)
{
        crypto_projective_work w;
        positive bytes = f->n * sizeof(p64);

        crypto_fe_sqr(w.t0, p->x, f);
        crypto_fe_sqr(w.t1, p->y, f);
        crypto_fe_sqr(w.t2, p->z, f);
        crypto_fe_mul(w.t3, p->x, p->y, f);
        crypto_fe_add(w.t3, w.t3, w.t3, f);
        crypto_fe_mul(w.z3, p->x, p->z, f);
        crypto_fe_add(w.z3, w.z3, w.z3, f);
        crypto_fe_mul(w.y3, b, w.t2, f);
        crypto_fe_sub(w.y3, w.y3, w.z3, f);
        crypto_fe_add(w.x3, w.y3, w.y3, f);
        crypto_fe_add(w.y3, w.x3, w.y3, f);
        crypto_fe_sub(w.x3, w.t1, w.y3, f);
        crypto_fe_add(w.y3, w.t1, w.y3, f);
        crypto_fe_mul(w.y3, w.x3, w.y3, f);
        crypto_fe_mul(w.x3, w.x3, w.t3, f);
        crypto_fe_add(w.t3, w.t2, w.t2, f);
        crypto_fe_add(w.t2, w.t2, w.t3, f);
        crypto_fe_mul(w.z3, b, w.z3, f);
        crypto_fe_sub(w.z3, w.z3, w.t2, f);
        crypto_fe_sub(w.z3, w.z3, w.t0, f);
        crypto_fe_add(w.t3, w.z3, w.z3, f);
        crypto_fe_add(w.z3, w.z3, w.t3, f);
        crypto_fe_add(w.t3, w.t0, w.t0, f);
        crypto_fe_add(w.t0, w.t3, w.t0, f);
        crypto_fe_sub(w.t0, w.t0, w.t2, f);
        crypto_fe_mul(w.t0, w.t0, w.z3, f);
        crypto_fe_add(w.y3, w.y3, w.t0, f);
        crypto_fe_mul(w.t0, p->y, p->z, f);
        crypto_fe_add(w.t0, w.t0, w.t0, f);
        crypto_fe_mul(w.z3, w.t0, w.z3, f);
        crypto_fe_sub(w.x3, w.x3, w.z3, f);
        crypto_fe_mul(w.z3, w.t0, w.t1, f);
        crypto_fe_add(w.z3, w.z3, w.z3, f);
        crypto_fe_add(w.z3, w.z3, w.z3, f);

        memory_copy(r->x, w.x3, bytes);
        memory_copy(r->y, w.y3, bytes);
        memory_copy(r->z, w.z3, bytes);
        crypto_forget(address_of w, sizeof w);
}

/* r = p + q; r may be p or q. */
static fn crypto_projective_add(crypto_projective address_to r,
                                const crypto_projective address_to p,
                                const crypto_projective address_to q,
                                const p64 address_to b,
                                const crypto_field address_to f)
{
        crypto_projective_work w;
        positive bytes = f->n * sizeof(p64);

        crypto_fe_mul(w.t0, p->x, q->x, f);
        crypto_fe_mul(w.t1, p->y, q->y, f);
        crypto_fe_mul(w.t2, p->z, q->z, f);
        crypto_fe_add(w.t3, p->x, p->y, f);
        crypto_fe_add(w.t4, q->x, q->y, f);
        crypto_fe_mul(w.t3, w.t3, w.t4, f);
        crypto_fe_add(w.t4, w.t0, w.t1, f);
        crypto_fe_sub(w.t3, w.t3, w.t4, f);
        crypto_fe_add(w.t4, p->y, p->z, f);
        crypto_fe_add(w.x3, q->y, q->z, f);
        crypto_fe_mul(w.t4, w.t4, w.x3, f);
        crypto_fe_add(w.x3, w.t1, w.t2, f);
        crypto_fe_sub(w.t4, w.t4, w.x3, f);
        crypto_fe_add(w.x3, p->x, p->z, f);
        crypto_fe_add(w.y3, q->x, q->z, f);
        crypto_fe_mul(w.x3, w.x3, w.y3, f);
        crypto_fe_add(w.y3, w.t0, w.t2, f);
        crypto_fe_sub(w.y3, w.x3, w.y3, f);
        crypto_fe_mul(w.z3, b, w.t2, f);
        crypto_fe_sub(w.x3, w.y3, w.z3, f);
        crypto_fe_add(w.z3, w.x3, w.x3, f);
        crypto_fe_add(w.x3, w.x3, w.z3, f);
        crypto_fe_sub(w.z3, w.t1, w.x3, f);
        crypto_fe_add(w.x3, w.t1, w.x3, f);
        crypto_fe_mul(w.y3, b, w.y3, f);
        crypto_fe_add(w.t1, w.t2, w.t2, f);
        crypto_fe_add(w.t2, w.t1, w.t2, f);
        crypto_fe_sub(w.y3, w.y3, w.t2, f);
        crypto_fe_sub(w.y3, w.y3, w.t0, f);
        crypto_fe_add(w.t1, w.y3, w.y3, f);
        crypto_fe_add(w.y3, w.t1, w.y3, f);
        crypto_fe_add(w.t1, w.t0, w.t0, f);
        crypto_fe_add(w.t0, w.t1, w.t0, f);
        crypto_fe_sub(w.t0, w.t0, w.t2, f);
        crypto_fe_mul(w.t1, w.t4, w.y3, f);
        crypto_fe_mul(w.t2, w.t0, w.y3, f);
        crypto_fe_mul(w.y3, w.x3, w.z3, f);
        crypto_fe_add(w.y3, w.y3, w.t2, f);
        crypto_fe_mul(w.x3, w.t3, w.x3, f);
        crypto_fe_sub(w.x3, w.x3, w.t1, f);
        crypto_fe_mul(w.z3, w.t4, w.z3, f);
        crypto_fe_mul(w.t1, w.t3, w.t0, f);
        crypto_fe_add(w.z3, w.z3, w.t1, f);

        memory_copy(r->x, w.x3, bytes);
        memory_copy(r->y, w.y3, bytes);
        memory_copy(r->z, w.z3, bytes);
        crypto_forget(address_of w, sizeof w);
}

#define CRYPTO_COMB_TEETH 5

/* k G, k private and below the order, as plain affine x and y. False only
   for the point at infinity, which k in [1, n) never gives. */
static bool crypto_comb_base(p64 address_to x, p64 address_to y,
                             const p64 address_to k,
                             const crypto_field address_to f,
                             const p64 address_to table,
                             const p64 address_to b)
{
        positive n = f->n;
        positive bits = n * 64;
        positive d = (bits + CRYPTO_COMB_TEETH - 1) / CRYPTO_COMB_TEETH;
        positive stride = 2 * n;
        crypto_projective r;
        crypto_projective chosen;
        p64 zinv[CRYPTO_FE_MAX];
        p64 digit;
        bool ok;

        memory_fill(address_of r, 0, sizeof r);
        memory_copy(r.y, f->one, n * sizeof(p64));
        for (positive j = d; j--;)
        {
                if (j != d - 1)
                        crypto_projective_double(address_of r, address_of r,
                                                 b, f);
                digit = 0;
                for (positive t = 0; t < CRYPTO_COMB_TEETH; t++)
                {
                        positive at = t * d + j;

                        if (at < bits)
                                digit |= ((k[at / 64] >> (at % 64)) & 1) << t;
                }

                //      Infinity, then each entry kept under a mask that is
                //      all ones for the one the column names. Every entry's
                //      z is one, so z is one unless the column is zero.
                memory_fill(address_of chosen, 0, sizeof chosen);
                memory_copy(chosen.y, f->one, n * sizeof(p64));
                for (p64 e = 1; e < (1u << CRYPTO_COMB_TEETH); e++)
                {
                        const p64 address_to entry = table + (e - 1) * stride;
                        p64 mask = 0 - (((e ^ digit) - 1) >> 63);

                        for (positive i = 0; i < n; i++)
                        {
                                chosen.x[i] = (chosen.x[i] & ~mask) |
                                              (entry[i] & mask);
                                chosen.y[i] = (chosen.y[i] & ~mask) |
                                              (entry[n + i] & mask);
                        }
                }
                for (positive i = 0; i < n; i++)
                        chosen.z[i] = f->one[i] & (0 - ((0 - digit) >> 63));
                crypto_projective_add(address_of r, address_of r,
                                      address_of chosen, b, f);
        }

        ok = !crypto_fe_is_zero(r.z, n);
        if (ok)
        {
                crypto_fe_inv(zinv, r.z, f);
                crypto_fe_mul(x, r.x, zinv, f);
                crypto_fe_mul(y, r.y, zinv, f);
                crypto_fe_mul(x, x, crypto_unit, f);
                crypto_fe_mul(y, y, crypto_unit, f);
        }

        crypto_forget(address_of r, sizeof r);
        crypto_forget(address_of chosen, sizeof chosen);
        crypto_forget(zinv, sizeof zinv);
        crypto_forget(address_of digit, sizeof digit);
        return ok;
}

static bool crypto_ecdh_public(p8 address_to out, p8 address_to scalar,
                               const crypto_field address_to field,
                               const crypto_field address_to order,
                               const p64 address_to table,
                               const p64 address_to b)
{
        p64 k[CRYPTO_FE_MAX];
        p64 x[CRYPTO_FE_MAX];
        p64 y[CRYPTO_FE_MAX];
        positive limbs = field->n;
        bool ok = false;

        if (!crypto_scalar_from_int_be(k, scalar, limbs * 8, order->m, limbs) ||
            !crypto_comb_base(x, y, k, field, table, b))
                goto done;

        out[0] = 4;
        crypto_fe_store_be(out + 1, x, limbs);
        crypto_fe_store_be(out + 1 + limbs * 8, y, limbs);
        ok = true;

done:
        crypto_forget(k, sizeof(k));
        crypto_forget(x, sizeof(x));
        crypto_forget(y, sizeof(y));
        return ok;
}

static bool crypto_ecdh_shared(p8 address_to out, p8 address_to scalar,
                               p8 address_to peer, positive peer_length,
                               const crypto_field address_to field,
                               const crypto_field address_to order,
                               const p8 address_to b)
{
        p64 k[CRYPTO_FE_MAX];
        p64 qx[CRYPTO_FE_MAX];
        p64 qy[CRYPTO_FE_MAX];
        crypto_point q;
        crypto_point r;
        positive limbs = field->n;
        positive coord = limbs * 8;
        bool ok = false;

        if (peer_length != 1 + 2 * coord || peer[0] != 4 ||
            !crypto_point_is_on_curve(peer + 1, peer + 1 + coord, field, b) ||
            !crypto_scalar_from_int_be(k, scalar, coord, order->m, limbs))
                goto done;

        crypto_fe_load_be(qx, peer + 1, limbs);
        crypto_fe_load_be(qy, peer + 1 + coord, limbs);
        crypto_point_set_xy(address_of q, qx, qy, field);
        crypto_point_scalar_private(address_of r, address_of q, k, null);
        if (crypto_fe_is_zero(r.z, limbs))
                goto done;

        crypto_point_affine(address_of r);
        crypto_fe_store_be(out, r.x, limbs);
        ok = true;

done:
        crypto_forget(k, sizeof(k));
        crypto_forget(qx, sizeof(qx));
        crypto_forget(qy, sizeof(qy));
        crypto_forget(address_of q, sizeof(q));
        crypto_forget(address_of r, sizeof(r));
        return ok;
}

static bool crypto_ecdh_p256_public(p8 address_to out, p8 address_to scalar)
{
        return crypto_ecdh_public(out, scalar, address_of crypto_p256_field,
                                  address_of crypto_p256_order,
                                  crypto_p256_comb[0][0], crypto_p256_b_mont);
}

static bool crypto_ecdh_p256_shared(p8 address_to out, p8 address_to scalar,
                                    p8 address_to peer)
{
        return crypto_ecdh_shared(out, scalar, peer, 65,
                                  address_of crypto_p256_field,
                                  address_of crypto_p256_order,
                                  crypto_p256_b_be);
}

static bool crypto_ecdh_p384_public(p8 address_to out, p8 address_to scalar)
{
        return crypto_ecdh_public(out, scalar, address_of crypto_p384_field,
                                  address_of crypto_p384_order,
                                  crypto_p384_comb[0][0], crypto_p384_b_mont);
}

static bool crypto_ecdh_p384_shared(p8 address_to out, p8 address_to scalar,
                                    p8 address_to peer)
{
        return crypto_ecdh_shared(out, scalar, peer, 97,
                                  address_of crypto_p384_field,
                                  address_of crypto_p384_order,
                                  crypto_p384_b_be);
}

/* x = 2x mod m for x below m.  Public moduli only: the reduction branches. */
static fn crypto_rsa_double(p64 address_to x, const p64 address_to m, positive n)
{
        p64 carry = 0;

        for (positive i = 0; i < n; i++)
        {
                p64 limb = x[i];

                x[i] = (limb << 1) | carry;
                carry = limb >> 63;
        }
        if (carry || crypto_fe_cmp(x, m, n) >= 0)
                crypto_fe_subtract_raw(x, x, m, n);
}

/* montgomery_multiply for any n to CRYPTO_RSA_LIMBS: the assembly to its 64
   limbs, and above that the same row-by-row product and reduction in C
   (CIOS: row i adds a b_i and q m, q = t0 (-1/m0) mod 2^64, and shifts
   one limb down), finished by one subtraction of m. Public moduli only:
   the final subtraction branches. */
static fn crypto_rsa_multiply(p64 address_to d, const p64 address_to a,
                              const p64 address_to b, const p64 address_to m,
                              p64 inverse, positive n)
{
        p64 t[CRYPTO_RSA_LIMBS + 2];

        if (n <= 64)
        {
                montgomery_multiply(d, a, b, m, inverse, n);
                return;
        }
        memory_fill(t, 0, (n + 2) * sizeof(p64));
        for (positive i = 0; i < n; i++)
        {
                crypto_wide carry = 0;
                p64 q;

                for (positive j = 0; j < n; j++)
                {
                        carry += (crypto_wide)a[j] * b[i] + t[j];
                        t[j] = (p64)carry;
                        carry >>= 64;
                }
                carry += t[n];
                t[n] = (p64)carry;
                t[n + 1] = (p64)(carry >> 64);
                q = t[0] * inverse;
                carry = ((crypto_wide)q * m[0] + t[0]) >> 64;
                for (positive j = 1; j < n; j++)
                {
                        carry += (crypto_wide)q * m[j] + t[j];
                        t[j - 1] = (p64)carry;
                        carry >>= 64;
                }
                carry += t[n];
                t[n - 1] = (p64)carry;
                t[n] = t[n + 1] + (p64)(carry >> 64);
        }
        if (t[n] || crypto_fe_cmp(t, m, n) >= 0)
                crypto_fe_subtract_raw(t, t, m, n);
        memory_copy(d, t, n * sizeof(p64));
}

/* out = base^exp mod m, for a public odd m of n limbs whose top limb is
   nonzero and base below m, in Montgomery form throughout.  -1/m mod 2^64
   comes by Newton's iteration: an odd m0 is its own inverse to three bits
   and each step doubles the correct bits.  R mod m starts from the top bit
   of m, which is below m, and doubles up to 2^(64n).  R^2 mod m is then the
   Montgomery form of 2^(64n): k doublings of the form of 1 make the form of
   2^k, and s squarings the form of 2^(k 2^s), with k 2^s = 64n. */
static fn crypto_rsa_modexp(p64 address_to out, p64 address_to base, p64 exp,
                            p64 address_to mod, positive n)
{
        p64 one[CRYPTO_RSA_LIMBS];
        p64 square[CRYPTO_RSA_LIMBS];
        p64 b[CRYPTO_RSA_LIMBS];
        p64 result[CRYPTO_RSA_LIMBS];
        p64 inverse = mod[0];
        positive bits;
        positive top;
        positive k;
        positive s = 0;
        positive at;

        memory_fill(out, 0, CRYPTO_RSA_LIMBS * sizeof(p64));
        if (!n || n > CRYPTO_RSA_LIMBS || !mod[n - 1] || !(mod[0] & 1) || !exp)
                return;

        for (positive i = 0; i < 5; i++)
                inverse *= 2 - mod[0] * inverse;
        inverse = 0 - inverse;

        top = 63;
        while (!((mod[n - 1] >> top) & 1))
                top--;
        bits = (n - 1) * 64 + top + 1;
        memory_fill(one, 0, n * 8);
        one[n - 1] = (p64)1 << top;
        for (at = bits - 1; at < n * 64; at++)
                crypto_rsa_double(one, mod, n);

        k = n * 64;
        while (!(k & 1))
        {
                k >>= 1;
                s++;
        }
        memory_copy(square, one, n * 8);
        for (at = 0; at < k; at++)
                crypto_rsa_double(square, mod, n);
        for (at = 0; at < s; at++)
                crypto_rsa_multiply(square, square, square, mod, inverse, n);

        crypto_rsa_multiply(b, base, square, mod, inverse, n);
        top = 63;
        while (!((exp >> top) & 1))
                top--;
        memory_copy(result, b, n * 8);
        while (top)
        {
                top--;
                crypto_rsa_multiply(result, result, result, mod, inverse, n);
                if ((exp >> top) & 1)
                        crypto_rsa_multiply(result, result, b, mod, inverse, n);
        }

        crypto_rsa_multiply(out, result, crypto_unit, mod, inverse, n);
}

/* Decode the public operation once for both RSA signature encodings.  An
   exponent is held to CRYPTO_RSA_EXPONENT_BITS, BoringSSL's ceiling: every
   real key uses 3 or 65537, and an 8192-bit modulus with a 64-bit exponent
   made one verify 15.2M cycles, ~29 of which a hostile server can ask a
   handshake to do; 33 bits halves that and refuses nothing Chrome takes.  The
   signature representative is an integer in [0,n), never an arbitrary byte
   string reduced modulo n, and a usable RSA public exponent is odd and at
   least three. */
static bool crypto_rsa_prepare(p8 address_to n_bytes, positive n_length,
                               p64 exponent, p8 address_to sig,
                               positive sig_length, p64 address_to mod,
                               p64 address_to base,
                               positive address_to limbs)
{
        p8 padded[CRYPTO_RSA_BYTES];

        if (n_length > sizeof(padded) || n_length < 256 ||
            sig_length != n_length || !n_bytes[0] ||
            (n_length == 256 && !(n_bytes[0] & 0x80)) ||
            !(n_bytes[n_length - 1] & 1) || exponent < 3 ||
            !(exponent & 1) || exponent >> CRYPTO_RSA_EXPONENT_BITS)
                return false;

        address_to limbs = (n_length + 7) / 8;
        memory_fill(mod, 0, CRYPTO_RSA_LIMBS * sizeof(p64));
        memory_fill(base, 0, CRYPTO_RSA_LIMBS * sizeof(p64));
        memory_fill(padded, 0, sizeof(padded));
        memory_copy(padded + address_to limbs * 8 - n_length, n_bytes,
                    n_length);
        crypto_fe_load_be(mod, padded, address_to limbs);
        memory_fill(padded, 0, sizeof(padded));
        memory_copy(padded + address_to limbs * 8 - sig_length, sig,
                    sig_length);
        crypto_fe_load_be(base, padded, address_to limbs);

        return crypto_fe_cmp(base, mod, address_to limbs) < 0;
}

/* s^e mod n, for both encodings to read: the n_length bytes of the encoded
   message, big endian, at the address returned inside room, or null when
   crypto_rsa_prepare refuses the key or the signature. */
static p8 address_to crypto_rsa_open(p8 address_to room, p8 address_to n_bytes,
                                     positive n_length, p64 exponent,
                                     p8 address_to sig, positive sig_length)
{
        p64 mod[CRYPTO_RSA_LIMBS];
        p64 base[CRYPTO_RSA_LIMBS];
        p64 out[CRYPTO_RSA_LIMBS];
        positive limbs;

        if (!crypto_rsa_prepare(n_bytes, n_length, exponent, sig, sig_length,
                                mod, base, address_of limbs))
                return null;
        crypto_rsa_modexp(out, base, exponent, mod, limbs);
        crypto_fe_store_be(room, out, limbs);
        return room + limbs * 8 - n_length;
}

/* EMSA-PKCS1-v1_5: 00 01 FF..FF 00 DigestInfo hash, at least eight FF
   bytes, the DigestInfo naming the hash exactly. */
static bool crypto_rsa_pkcs1(p8 address_to n_bytes, positive n_length,
                             p64 exponent, p8 address_to sig,
                             positive sig_length,
                             const p8 address_to digestinfo,
                             positive digestinfo_length, p8 address_to hash,
                             positive hash_length)
{
        p8 room[CRYPTO_RSA_BYTES];
        p8 address_to em = crypto_rsa_open(room, n_bytes, n_length, exponent,
                                           sig, sig_length);
        positive i;

        if (!em || em[0] != 0x00 || em[1] != 0x01)
                return false;

        i = 2 + memory_span_byte(em + 2, 0xff, n_length - 2);
        if (i < 10 || i >= n_length || em[i] != 0x00 ||
            i + 1 + digestinfo_length + hash_length != n_length)
                return false;
        i++;
        return memory_compare(em + i, digestinfo, digestinfo_length) == 0 &&
               memory_compare(em + i + digestinfo_length, hash, hash_length) ==
                   0;
}

/* MGF1-SHA-256 of seed, xored over the want bytes at into. */
static fn crypto_mgf1_sha256(p8 address_to seed, positive seed_length,
                             p8 address_to into, positive want)
{
        for (p32 counter = 0; want; counter++)
        {
                crypto_sha256 hash;
                p8 block[32];
                p8 count[4];
                positive take = want < 32 ? want : 32;

                network_store_32(count, counter);
                crypto_sha256_open(address_of hash);
                crypto_sha256_write(address_of hash, seed, seed_length);
                crypto_sha256_write(address_of hash, count, 4);
                crypto_sha256_close(address_of hash, block);
                for (positive at = 0; at < take; at++)
                        into[at] ^= block[at];
                into += take;
                want -= take;
        }
}

/* TLS 1.3 rsa_pss_rsae_sha256: EMSA-PSS with SHA-256, MGF1-SHA-256, salt 32.
   crypto_rsa_prepare has held n to 2048 bits or more with a nonzero first
   byte, so the encoding is emBits = modBits - 1 bits in k = n_length or
   n_length - 1 bytes, the latter when modBits is one more than a multiple
   of eight and the block's first byte must then be zero. */
static bool crypto_rsa_pss_sha256(p8 address_to n_bytes, positive n_length,
                                  p64 exponent, p8 address_to sig,
                                  positive sig_length, p8 address_to message,
                                  positive message_length)
{
        p8 room[CRYPTO_RSA_BYTES];
        p8 address_to em = crypto_rsa_open(room, n_bytes, n_length, exponent,
                                           sig, sig_length);
        p8 mhash[32];
        p8 hcheck[32];
        p8 prefix[8];
        crypto_sha256 hash;
        positive em_bits = (n_length - 1) * 8 - 1;
        positive k;
        positive unused;
        positive masked;
        positive at;

        if (!em)
                return false;
        for (p8 top = n_bytes[0]; top; top >>= 1)
                em_bits++;
        k = (em_bits + 7) / 8;
        if (k != n_length)
        {
                if (em[0])
                        return false;
                em++;
        }

        unused = 8 * k - em_bits;
        if (em[0] >> (8 - unused) || em[k - 1] != 0xbc)
                return false;

        masked = k - 32 - 1;
        crypto_mgf1_sha256(em + masked, 32, em, masked);
        em[0] &= (p8)(0xff >> unused);

        at = memory_span_byte(em, 0, masked);
        if (at >= masked || em[at] != 0x01 || masked - at - 1 != 32)
                return false;
        at++;

        crypto_sha256_of(message, message_length, mhash);
        memory_fill(prefix, 0, sizeof(prefix));
        crypto_sha256_open(address_of hash);
        crypto_sha256_write(address_of hash, prefix, 8);
        crypto_sha256_write(address_of hash, mhash, 32);
        crypto_sha256_write(address_of hash, em + at, 32);
        crypto_sha256_close(address_of hash, hcheck);
        return memory_compare(hcheck, em + masked, 32) == 0;
}

#endif
#include "wait.c"

/* RFC 8446 5.1 and 5.4: a record carries at most 2^14 bytes of content, and
   under AES-128-GCM a protected one adds the inner type byte and the tag.
   A TLS 1.2 one (RFC 5288 3) adds the eight-byte explicit nonce and the tag
   instead, and that larger size is the room every buffer keeps. */
#define TLS_PLAINTEXT_MAX 16384
#define TLS_RECORD_MAX (TLS_PLAINTEXT_MAX + 1 + 16)
#define TLS12_RECORD_MAX (TLS_PLAINTEXT_MAX + 8 + 16)
/* One receive takes as many whole records as the socket has queued and this
   room holds: about fifteen full records. At least one whole record must
   fit: the unopened tail, at most one partial record, moves to the front
   before every read, and the read has to be able to finish it. */
#ifndef TLS_RECEIVE_ROOM
#define TLS_RECEIVE_ROOM ((positive)1 << 18)
#endif
#define TLS_HS_MAX 16384
#define TLS_HANDSHAKE_SECONDS 30
#define TLS_WRITE_SECONDS 30
#define TLS_OK 0
#define TLS_FAIL (-1)
#define TLS_EOF 1
#define TLS_AGAIN 2
#define TLS_RETRY 3
/* tls_connect's answer when certificate checking refused the server: wget
   says so apart, as GNU wget's status 5 does. */
#define TLS_UNTRUSTED (-2)
/* Or, when the leaf's own dates are the reason, that it is past its
   notAfter or not yet at its notBefore: wget tells the user to look at the
   clock, which before the first NTP answer is decades wrong. */
#define TLS_EXPIRED (-3)
#define TLS_NOT_YET (-4)
/* Or that no name in it was the host's. */
#define TLS_MISMATCH (-5)
/* Or that its issuer says it is revoked, in a CRL or a stapled OCSP
   response; or that whether it is could not be told where the answer is
   required: a must-staple leaf without a good staple in every tier, and an
   unreachable, stale or unreadable source at the tight one. */
#define TLS_REVOKED (-6)
#define TLS_UNCHECKED (-7)
/* Or that the clock is below its floor: it was never set, so no date in a
   certificate can be judged against it. */
#define TLS_CLOCK (-8)
/* Or, at the tight tier, that the certificate was not logged as Certificate
   Transparency asks (tls_ct_holds). */
#define TLS_UNLOGGED (-9)
#define TLS_FAULT_EARLY 1
#define TLS_FAULT_LATE 2
#define TLS_FAULT_NAME 3
#define TLS_FAULT_REVOKED 4
#define TLS_FAULT_UNCHECKED 5
#define TLS_FAULT_CLOCK 6
#define TLS_FAULT_UNLOGGED 7

#define TLS_CT_CCS 20
#define TLS_CT_ALERT 21
#define TLS_CT_HANDSHAKE 22
#define TLS_CT_APP 23

#define TLS_HS_CLIENT_HELLO 1
#define TLS_HS_SERVER_HELLO 2
#define TLS_HS_NEW_SESSION_TICKET 4
#define TLS_HS_ENCRYPTED_EXTS 8
#define TLS_HS_CERTIFICATE 11
#define TLS_HS_SERVER_KEY_EXCHANGE 12
#define TLS_HS_CERT_REQUEST 13
#define TLS_HS_SERVER_HELLO_DONE 14
#define TLS_HS_CLIENT_KEY_EXCHANGE 16
#define TLS_HS_CERT_VERIFY 15
#define TLS_HS_FINISHED 20
#define TLS_HS_CERTIFICATE_STATUS 22
#define TLS_HS_KEY_UPDATE 24

/* One trust anchor from anchors.inc: hashes that find candidates, then the
   key itself (curve 1 P-256, 2 P-384, 3 RSA), key_length raw bytes at key_at
   in tls_anchor_keys. distrust is Mozilla's server-auth distrust-after date
   as YYYYMMDDHHMMSS, the form a certificate's dates are kept in, or 0 for
   none: a chain that ends at this anchor is refused when its leaf's
   notBefore is later than that (NSS applies the same comparison, and Chrome
   and Firefox refuse the same chains). */
typedef struct
{
        p8 name[8];
        p8 key_hash[8];
        p8 curve;
        p32 exponent;
        p16 key_length;
        p32 key_at;
        p64 distrust;
} tls_anchor;

#include "anchors.inc"
#include "suffixes.inc"

/* Intermediate CA keys that are known to be compromised, as the SHA-256 of the
   DER SubjectPublicKeyInfo (what Go, Chrome's CRLSet and Mozilla's OneCRL
   name them by, so a Sectigo cross-sign of the same key is the same row).
   They are eMudhra's eight "G3B" and "G3C" intermediates, EM DV, OV and EV TLS
   and OV code signing: generated in its 2025-04-30 key ceremony with too
   little entropy, so the private keys are whole numbers below 256 and anyone
   can compute them from the public certificates (disclosed 2026-10-17;
   revoked 2026-10-18 and 19). A revocation list does not stop a forger, who
   issues from such a key and leaves the CRL and OCSP pointers out, so the key
   itself is refused at every tier, and a chain through it, served or fetched,
   is never a path to an anchor. */
static const p8 tls_blocked_keys[8][32] = {
    {0xeb, 0xed, 0x64, 0x75, 0xc2, 0xc2, 0xd8, 0x0f, 0xcb, 0x19, 0x9f, 0x3f, 0xe8, 0xdd, 0x6e, 0x6b,
     0x3c, 0xe6, 0x0b, 0x77, 0xeb, 0x15, 0xd4, 0xe4, 0x2c, 0x3d, 0xec, 0x0c, 0x6f, 0x5b, 0x51, 0x05},
    {0x49, 0xdd, 0x35, 0x3d, 0xc2, 0xb9, 0x19, 0xa3, 0x77, 0x06, 0x45, 0x11, 0x7d, 0x0e, 0x60, 0xc1,
     0x61, 0x15, 0xe7, 0xd1, 0xbc, 0x25, 0x9e, 0xef, 0xe7, 0x34, 0xb0, 0x0e, 0xa4, 0x34, 0xfe, 0x5f},
    {0xe0, 0x25, 0x4c, 0x7f, 0xcd, 0xc2, 0x65, 0xe2, 0xf0, 0xf8, 0xa4, 0x28, 0xda, 0xbb, 0x75, 0xab,
     0x4a, 0xe0, 0x39, 0xb4, 0x87, 0x2d, 0x62, 0xf5, 0xee, 0x57, 0xf0, 0x87, 0x7d, 0x5a, 0x8a, 0x41},
    {0x5c, 0x06, 0x60, 0xa2, 0xb1, 0x64, 0x91, 0x0d, 0x8f, 0xf5, 0x89, 0xc1, 0x8e, 0x14, 0x55, 0x2e,
     0x9e, 0x0e, 0xc2, 0x37, 0xdc, 0xef, 0xe2, 0xcb, 0x73, 0x6d, 0x41, 0xa4, 0x81, 0x1f, 0x81, 0xa0},
    {0x43, 0xbf, 0xfa, 0x19, 0xd6, 0xef, 0x65, 0x14, 0x51, 0xf6, 0x84, 0x1e, 0x01, 0x99, 0x31, 0xe9,
     0x6f, 0x6f, 0x2e, 0xae, 0xff, 0x28, 0xab, 0x4b, 0x6f, 0x1c, 0xe4, 0x57, 0x6d, 0xe2, 0x71, 0x7b},
    {0x63, 0xec, 0xef, 0xee, 0x43, 0x1d, 0x49, 0x52, 0x3b, 0xc1, 0x13, 0x6c, 0x4e, 0x72, 0xd0, 0x20,
     0x6a, 0x47, 0x70, 0x86, 0xca, 0xf6, 0x06, 0xcd, 0x5c, 0xf0, 0x25, 0x68, 0x6c, 0x10, 0x9c, 0xf8},
    {0xec, 0x4f, 0xfa, 0x1a, 0xd0, 0xac, 0x88, 0x08, 0x13, 0x14, 0xb7, 0xaa, 0xff, 0xef, 0x6a, 0x91,
     0xf9, 0x7c, 0xd5, 0x72, 0x91, 0xbb, 0x5b, 0xa6, 0x86, 0xc5, 0x41, 0x5f, 0x5e, 0xb4, 0xf6, 0xb6},
    {0x92, 0xc0, 0x63, 0xd6, 0x7e, 0x8f, 0x14, 0xdb, 0x71, 0xd2, 0x88, 0xe1, 0x12, 0x66, 0xff, 0xda,
     0xf6, 0x9c, 0xbf, 0x11, 0x5f, 0x7b, 0xab, 0x14, 0xf3, 0xbc, 0x2f, 0x68, 0xd2, 0x5e, 0x0e, 0x30}};

static const p8 tls_oid_ec[7] = {0x2a, 0x86, 0x48, 0xce, 0x3d, 0x02, 0x01};
static const p8 tls_oid_p256[8] = {0x2a, 0x86, 0x48, 0xce, 0x3d, 0x03, 0x01, 0x07};
static const p8 tls_oid_p384[5] = {0x2b, 0x81, 0x04, 0x00, 0x22};
static const p8 tls_oid_ecdsa_with[7] = {0x2a, 0x86, 0x48, 0xce, 0x3d, 0x04, 0x03};
static const p8 tls_oid_pkcs1[8] = {0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01};
static const p8 tls_oid_rsa[9] = {0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01,
                                  0x01, 0x01};
static const p8 tls_oid_san[3] = {0x55, 0x1d, 0x11};
/* 1.3.6.1.4.1.11129.2.4.2, the embedded SCT list (RFC 6962 3.3). */
static const p8 tls_oid_scts[10] = {0x2b, 0x06, 0x01, 0x04, 0x01, 0xd6, 0x79, 0x02, 0x04, 0x02};
static const p8 tls_oid_basic_constraints[3] = {0x55, 0x1d, 0x13};
static const p8 tls_oid_name_constraints[3] = {0x55, 0x1d, 0x1e};
static const p8 tls_oid_key_usage[3] = {0x55, 0x1d, 0x0f};
static const p8 tls_oid_extended_key_usage[3] = {0x55, 0x1d, 0x25};
static const p8 tls_oid_server_auth[8] = {0x2b, 0x06, 0x01, 0x05,
                                          0x05, 0x07, 0x03, 0x01};
static const p8 tls_oid_authority_info[8] = {0x2b, 0x06, 0x01, 0x05,
                                             0x05, 0x07, 0x01, 0x01};
/* Revocation: the certificate's cRLDistributionPoints, authorityKeyIdentifier
   and TLS Feature (RFC 7633) extensions and id-kp-OCSPSigning; a CRL's
   cRLNumber, deltaCRLIndicator and issuingDistributionPoint and an entry's
   reasonCode and invalidityDate; id-pkix-ocsp-basic; and the two hashes an
   OCSP CertID names its issuer by, SHA-1 and SHA-256. */
static const p8 tls_oid_crl_points[3] = {0x55, 0x1d, 0x1f};
static const p8 tls_oid_authority_key[3] = {0x55, 0x1d, 0x23};
static const p8 tls_oid_tls_feature[8] = {0x2b, 0x06, 0x01, 0x05,
                                          0x05, 0x07, 0x01, 0x18};
static const p8 tls_oid_ocsp_signing[8] = {0x2b, 0x06, 0x01, 0x05,
                                           0x05, 0x07, 0x03, 0x09};
static const p8 tls_oid_crl_number[3] = {0x55, 0x1d, 0x14};
static const p8 tls_oid_delta_crl[3] = {0x55, 0x1d, 0x1b};
static const p8 tls_oid_crl_scope[3] = {0x55, 0x1d, 0x1c};
static const p8 tls_oid_crl_reason[3] = {0x55, 0x1d, 0x15};
static const p8 tls_oid_invalidity[3] = {0x55, 0x1d, 0x18};
static const p8 tls_oid_ocsp_basic[9] = {0x2b, 0x06, 0x01, 0x05, 0x05,
                                         0x07, 0x30, 0x01, 0x01};
static const p8 tls_oid_sha1[5] = {0x2b, 0x0e, 0x03, 0x02, 0x1a};
static const p8 tls_oid_sha256[9] = {0x60, 0x86, 0x48, 0x01, 0x65,
                                     0x03, 0x04, 0x02, 0x01};

typedef struct
{
        bipolar handle;
        bool check_cert;
        bool encrypted;
        bool application;
        bool named;
        string_address host;
        crypto_sha256 transcript;
        /* The one key share on offer: its group and secret scalar. */
        p16 group;
        p8 scalar[48];
        p8 client_random[32];
        p8 hs_secret[32];
        p8 c_hs_traffic[32];
        p8 s_hs_traffic[32];
        /* Kept past the handshake: KeyUpdate derives the next from each. */
        p8 c_ap_traffic[32];
        p8 s_ap_traffic[32];
        bool update_asked;
        bool cert_requested;
        /* The server's Certificate was refused while checking it, and when
           the reason was the leaf's name or its validity dates against the
           clock (which side of them the clock is on), that reason. */
        bool untrusted;
        p8 fault;
        /* status_request went in the hello (only when the chain is checked:
           a staple is read for revocation and nothing else); a TLS 1.2
           server echoed it, so a CertificateStatus may follow its
           Certificate, whose check then waits for it (chain_pending). */
        bool status_requested;
        bool status_echoed;
        bool chain_pending;
        /* The handshake's deadline while it runs: a revocation fetch nests
           in it. */
        const network_deadline address_to deadline;
        /* Network Time Security's key establishment (RFC 8915 4) uses TLS for
           two things besides a connection: an application protocol to offer
           in the ClientHello and to see named back, which is also how this
           client knows it must not settle for TLS 1.2 (no exporter there,
           and RFC 8915 requires 1.3), and a key derived from the finished
           handshake under a label (RFC 8446 7.5). clock_bootstrap says the
           connection is the one that will say what time it is, so the clock
           cannot yet be asked: see tls_verify_chain. */
        const p8 address_to alpn;
        positive alpn_length;
        bool clock_bootstrap;
        p8 exporter[32];
        /* TLS 1.2 (RFC 5246, 5288, 7627): the suite, the server's random,
           the master secret -- the premaster until the client's flight --
           and the client's key exchange point, made when the server's
           arrives. */
        bool tls12;
        p16 suite;
        p8 server_random[32];
        p8 master[48];
        p8 share[97];
        p8 c_iv[12];
        p8 s_iv[12];
        crypto_aesgcm_key c_gcm;
        crypto_aesgcm_key s_gcm;
        p64 seq_read;
        p64 seq_write;
        p8 leaf_qx[48];
        p8 leaf_qy[48];
        p8 leaf_n[CRYPTO_RSA_BYTES];
        positive leaf_n_length;
        p64 leaf_e;
        p8 leaf_curve;
        /* A post-handshake message may span records and reads; close_notify,
           once read, answers every later read. */
        bool closed;
        positive post_handshake_used;
        /* Socket bytes. Records not yet opened lie in
           [receive_start, receive_end); application data is decrypted where
           it lies, and plain_at and plain_used name the part of the last
           opened record not yet handed out. Opened plaintext stays until a
           later receive overwrites it or tls_forget erases the connection,
           which is why receive_high keeps the furthest any read has filled:
           nothing past it was ever written, so nothing past it is wiped. */
        positive receive_start;
        positive receive_end;
        positive receive_high;
        positive plain_at;
        positive plain_used;
        /* The two buffers stay last and are neither zeroed on the way in nor
           wiped past what was used on the way out: they are 272 KiB of a
           connection that fetches a few kilobytes, and filling them whole
           twice was a page fault a page and a byte-at-a-time store each.
           post_handshake holds only its first post_handshake_used bytes; the
           append wipes what it consumes. */
        p8 post_handshake[TLS_HS_MAX];
        p8 receive[TLS_RECEIVE_ROOM];
} tls_conn;

/* The part of a connection zeroed when it opens: everything before the
   buffers. */
#define TLS_CONN_HEAD __builtin_offsetof(tls_conn, post_handshake)

/* RFC 8446 5.5 holds each AES-GCM key under 2^24.5 full records. Half way
   there a direction is rekeyed with KeyUpdate -- the write side by sending
   one, the read side by asking the peer for one -- and a key that still
   reaches 2^24 records ends the connection, which also keeps the sequence
   number from wrapping. */
#define TLS_AES_GCM_RECORD_LIMIT ((p64)1 << 24)
#define TLS_KEY_UPDATE_AT ((p64)1 << 23)

static fn tls_forget(tls_conn address_to tls)
{
        positive held = min(tls->post_handshake_used, (positive)TLS_HS_MAX);
        positive filled = min(tls->receive_high, (positive)TLS_RECEIVE_ROOM);

        crypto_forget(tls->post_handshake, held);
        crypto_forget(tls->receive, filled);
        crypto_forget(tls, TLS_CONN_HEAD);
        tls->handle = -1;
}

static COLD fn tls_expand_label(p8 address_to secret, string_address label,
                           const p8 address_to context, positive context_length,
                           p8 address_to out, positive out_length)
{
        p8 info[256];
        positive label_length = string_length(label);
        positive used = 2;

        // HkdfLabel is 2 + 1 + 6 + label + 1 + context. The copies below
        // use the full lengths, so a truncated length byte is not a bound.
        if (label_length > 249 || context_length > 255 ||
            used + 1 + 6 + label_length + 1 + context_length > sizeof info)
        {
                crypto_forget(out, out_length);
                return;
        }

        network_store_16(info, (p16)out_length);
        info[used++] = (p8)(6 + label_length);
        memory_copy(info + used, "tls13 ", 6);
        used += 6;
        memory_copy(info + used, label, label_length);
        used += label_length;
        info[used++] = (p8)context_length;
        if (context_length)
        {
                memory_copy(info + used, context, context_length);
                used += context_length;
        }

        crypto_hkdf_expand(secret, info, used, out, out_length);
        crypto_forget(info, sizeof info);
}

static COLD fn tls_derive_secret(p8 address_to secret, string_address label,
                            crypto_sha256 address_to transcript,
                            p8 address_to out)
{
        crypto_sha256 copy = *transcript;
        p8 hash[32];

        crypto_sha256_close(address_of copy, hash);
        tls_expand_label(secret, label, hash, 32, out, 32);
        crypto_forget(address_of copy, sizeof copy);
        crypto_forget(hash, sizeof hash);
}

/* With no PSK the early secret is a constant, and so is the salt derived
   from it for the handshake secret; every "derived" is taken over SHA-256
   of nothing (RFC 8446 7.1; the values are RFC 8448's). */
static const p8 tls_derived_early[32] = {
    0x6f, 0x26, 0x15, 0xa1, 0x08, 0xc7, 0x02, 0xc5, 0x67, 0x8f, 0x54, 0xfc,
    0x9d, 0xba, 0xb6, 0x97, 0x16, 0xc0, 0x76, 0x18, 0x9c, 0x48, 0x25, 0x0c,
    0xeb, 0xea, 0xc3, 0x57, 0x6c, 0x36, 0x11, 0xba};
static const p8 tls_empty_sha256[32] = {
    0xe3, 0xb0, 0xc4, 0x42, 0x98, 0xfc, 0x1c, 0x14, 0x9a, 0xfb, 0xf4, 0xc8,
    0x99, 0x6f, 0xb9, 0x24, 0x27, 0xae, 0x41, 0xe4, 0x64, 0x9b, 0x93, 0x4c,
    0xa4, 0x95, 0x99, 0x1b, 0x78, 0x52, 0xb8, 0x55};

/* One direction's key and IV from its traffic secret, and the key prepared
   for AES-GCM; the raw key lives only as long as preparing it takes. */
static COLD fn tls_traffic_keys(p8 address_to traffic,
                                crypto_aesgcm_key address_to gcm,
                                p8 address_to iv)
{
        p8 key[16];

        tls_expand_label(traffic, "key", null, 0, key, 16);
        tls_expand_label(traffic, "iv", null, 0, iv, 12);
        crypto_aesgcm_prepare(gcm, key);
        crypto_forget(key, sizeof key);
}

/* RFC 8446 5.3: the sequence number, big-endian, xored into the IV's end. */
static fn tls_nonce(p8 address_to iv, p64 seq, p8 address_to nonce)
{
        /* The first word is the fixed salt. The sequence occupies the final
           big-endian word, so xor it as one value: besides spelling RFC
           8446 directly, this avoids eight checked byte writes per record.
           The unaligned helpers fold to ordinary loads/stores on x86 and
           AArch64 and safe byte operations on the RV64 floor. */
        memory_store_unaligned(p32, nonce,
                               memory_load_unaligned(p32, iv));
        network_store_64(nonce + 4, network_load_64(iv + 4) ^ seq);
}

static fn tls_record_header(p8 address_to header, p8 type, positive length)
{
        p32 word = (p32)type << 24 | 0x00030300u |
                   (p32)((p16)length >> 8);

        /* This runs for every record. Keeping the swap and unaligned store
           here lets x86 and AArch64 fold them into two instructions instead
           of calling lib.c's general packet-field routine. */
#if RISCV64
        /* GCC lowers the builtin byte swap to __bswapsi2 at the IMAFD
           floor, and this freestanding program has no libgcc. lib.c's
           packet store is the compact bytewise body for that machine. */
        network_store_32(header, word);
#elif defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
        memory_store_unaligned(p32, header, word);
#else
        memory_store_unaligned(p32, header, __builtin_bswap32(word));
#endif
        header[4] = (p8)length;
}

/* RFC 5246 6.2.3.3: a TLS 1.2 record's additional data is its sequence
   number, then its header with the plaintext's length. */
static fn tls12_aad(p8 address_to aad, p64 seq, p8 address_to header,
                    positive length)
{
        network_store_64(aad, seq);
        tls_record_header(aad + 8, header[0], length);
}

/* Seal a record into record, which has room for 5 + TLS_RECORD_MAX bytes,
   and answer its length, or 0 when the body is too long or the write key is
   spent. It is sealed where it lies, so what leaves and what stays behind
   is ciphertext; the plaintext is the caller's. */
static positive tls_seal(tls_conn address_to tls, p8 inner_type,
                         p8 address_to body, positive length,
                         p8 address_to record)
{
        p8 nonce[12];
        p8 aad[13];
        positive at = 5;

        if (length > TLS_PLAINTEXT_MAX ||
            tls->seq_write >= TLS_AES_GCM_RECORD_LIMIT)
                return 0;
        tls_nonce(tls->c_iv, tls->seq_write, nonce);
        if (tls->tls12)
        {
                /* The type is the record's own and the explicit nonce the
                   sequence number, which c_iv's zero tail makes the
                   nonce's; the AAD is sequence, header and length. */
                tls_record_header(record, inner_type, 8 + length + 16);
                memory_copy(record + 5, nonce + 4, 8);
                at += 8;
                tls12_aad(aad, tls->seq_write, record, length);
        }
        else
        {
                tls_record_header(record, TLS_CT_APP, length + 1 + 16);
                record[5 + length] = inner_type;
        }
        memory_copy(record + at, body, length);
        length += !tls->tls12;
        tls->seq_write++;
        crypto_aesgcm_seal(address_of tls->c_gcm, nonce,
                           tls->tls12 ? aad : record, tls->tls12 ? 13 : 5,
                           record + at, length, record + at + length);
        crypto_forget(nonce, sizeof nonce);
        return at + length + 16;
}

/* A record leaves in one send: sent in pieces, everything after the first
   piece waits under Nagle for the peer to acknowledge it. */
static bipolar tls_send_enc_until(tls_conn address_to tls, p8 inner_type,
                                  p8 address_to body, positive length,
                                  const network_deadline address_to deadline)
{
        p8 record[5 + TLS12_RECORD_MAX];
        positive sealed = tls_seal(tls, inner_type, body, length, record);

        if (!sealed || !(deadline
                ? network_stream_send_all_until(tls->handle, record, sealed,
                                                deadline)
                : network_stream_send_all_for(tls->handle, record, sealed,
                                               TLS_WRITE_SECONDS, 0)))
        {
                /* A prefix may already be on the wire. No later record can
                   safely continue that stream or undo the spent sequence. */
                tls->seq_write = TLS_AES_GCM_RECORD_LIMIT;
                return TLS_FAIL;
        }
        return TLS_OK;
}

static bipolar tls_send_enc(tls_conn address_to tls, p8 inner_type,
                            p8 address_to body, positive length)
{
        return tls_send_enc_until(tls, inner_type, body, length, null);
}

/* Open a record where it lies. The inner type is its last nonzero byte;
   zeros after it are padding, and a record of nothing else is refused. A
   TLS 1.2 record's type is its header's, its plaintext follows the
   explicit nonce, and its nonce is the four-byte salt and that. A failed
   open wipes what it decrypted. */
static bipolar tls_decrypt_record(tls_conn address_to tls, p8 address_to payload,
                                  positive payload_length, p8 address_to aad,
                                  positive address_to inner_length,
                                  p8 address_to type)
{
        p8 nonce[12];
        p8 aad12[13];
        positive skip = tls->tls12 ? 8 : 0;
        positive at = payload_length - 16 - skip;
        bool opened;

        if (payload_length < 16 + skip ||
            tls->seq_read >= TLS_AES_GCM_RECORD_LIMIT)
                return TLS_FAIL;
        tls_nonce(tls->s_iv, tls->seq_read, nonce);
        if (skip)
        {
                memory_copy(nonce + 4, payload, 8);
                tls12_aad(aad12, tls->seq_read, aad, at);
        }
        opened = crypto_aesgcm_open(address_of tls->s_gcm, nonce,
                                    skip ? aad12 : aad, skip ? 13 : 5,
                                    payload + skip, at, payload + skip + at);
        crypto_forget(nonce, sizeof nonce);
        if (!opened)
                return TLS_FAIL;

        tls->seq_read++;
        if (skip)
        {
                address_to type = aad[0];
                address_to inner_length = at;
                return TLS_OK;
        }
        if (at && !payload[at - 1])
                at -= memory_span_byte_reverse(payload, 0, at);
        if (!at)
                return TLS_FAIL;
        address_to type = payload[at - 1];
        address_to inner_length = at - 1;
        return TLS_OK;
}

static bool tls_compatibility_ccs_valid(p8 address_to payload,
                                        positive length,
                                        bool application)
{
        return !application && length == 1 && payload[0] == 1;
}

static bool tls_compatibility_ccs_take(bool address_to seen)
{
        if (*seen)
                return false;
        *seen = true;
        return true;
}

static bool tls_record_version_valid(p8 address_to header)
{
        return header[1] == 0x03 && header[2] == 0x03;
}

/* Receive behind receive_end. Opened bytes before receive_start are dropped
   first, so every read has the whole room behind what is still unopened --
   at most one partial record, moved to the front at most once -- and takes
   as much as the socket has queued: a read into the few kilobytes left
   behind a partial record was a second read, and a second writev, for what
   one could carry. The read is tried before any wait, because mid-transfer
   the socket almost always has bytes queued; only an empty socket polls,
   under the deadline, and nothing blocks past it. */
static bool tls_receive(tls_conn address_to tls,
                        const network_deadline address_to deadline)
{
        positive have = tls->receive_end - tls->receive_start;
        p8 address_to into;
        positive room;
        bipolar got;

        if (tls->receive_start)
                memory_copy(tls->receive, tls->receive + tls->receive_start,
                            have);
        tls->receive_start = 0;
        tls->receive_end = have;
        into = tls->receive + have;
        room = sizeof(tls->receive) - have;
        if (!deadline)
                got = system_read_retry((positive)tls->handle, into, room);
        else
                got = network_stream_read_some_until(tls->handle, into, room,
                                                     deadline);

        if (got <= 0 || (positive)got > room)
                return false;
        tls->receive_end += (positive)got;
        if (tls->receive_high < tls->receive_end)
                tls->receive_high = tls->receive_end;
        return true;
}

static bool tls_record_whole(tls_conn address_to tls)
{
        positive have = tls->receive_end - tls->receive_start;
        p8 address_to header = tls->receive + tls->receive_start;

        return have >= 5 &&
               have - 5 >= network_load_16(header + 3);
}

/* Open the next record, receiving until it is whole. Its bytes stay in the
   receive buffer: *inner points at the plaintext, decrypted in place, and is
   valid until the next receive. */
static bipolar tls_next_record(tls_conn address_to tls, p8 address_to type,
                               p8 address_to address_to inner,
                               positive address_to length,
                               const network_deadline address_to deadline)
{
        p8 address_to header;
        p8 address_to payload;
        positive payload_length = 0;
        positive inner_length = 0;
        p8 inner_type = 0;

        for (;;)
        {
                positive have = tls->receive_end - tls->receive_start;

                header = tls->receive + tls->receive_start;
                if (have >= 5)
                {
                        /* TLS 1.3 authenticates these bytes as AAD for
                           encrypted records and fixes legacy_record_version
                           at TLS 1.2 for every server record. */
                        if (!tls_record_version_valid(header))
                                goto refused;
                        payload_length = network_load_16(header + 3);
                        if (!payload_length ||
                            payload_length > (tls->tls12 ? TLS12_RECORD_MAX
                                                         : TLS_RECORD_MAX))
                                goto refused;
                        if (have - 5 >= payload_length)
                                break;
                }
                if (!tls_receive(tls, deadline))
                        return TLS_FAIL;
        }

        payload = header + 5;
        tls->receive_start += 5 + payload_length;

        if (header[0] == TLS_CT_CCS)
        {
                if (!tls_compatibility_ccs_valid(payload, payload_length,
                                                 tls->application))
                        goto refused;
                address_to type = TLS_CT_CCS;
                address_to inner = payload;
                address_to length = 0;
                return TLS_OK;
        }

        if (!tls->encrypted)
        {
                if (header[0] != TLS_CT_HANDSHAKE ||
                    payload_length > TLS_PLAINTEXT_MAX)
                        goto refused;
                address_to type = TLS_CT_HANDSHAKE;
                address_to inner = payload;
                address_to length = payload_length;
                return TLS_OK;
        }

        /* A change_cipher_spec that arrives protected is unexpected. A TLS
           1.2 record names its own type, and only alerts, handshake and
           application data are protected. */
        if ((tls->tls12 ? header[0] != TLS_CT_ALERT &&
                              header[0] != TLS_CT_HANDSHAKE &&
                              header[0] != TLS_CT_APP
                        : header[0] != TLS_CT_APP) ||
            tls_decrypt_record(tls, payload, payload_length, header,
                               address_of inner_length, address_of inner_type) ||
            inner_type == TLS_CT_CCS)
                goto refused;
        payload += tls->tls12 ? 8 : 0;

        if (inner_type == TLS_CT_ALERT)
        {
                if (inner_length == 2 && payload[1] == 0)
                        return TLS_EOF;
                goto refused;
        }

        address_to type = inner_type;
        address_to inner = payload;
        address_to length = inner_length;
        return TLS_OK;

refused:
        /* A refused record is fatal (RFC 8446 5.2, 6): neither direction
           can continue, and no record behind it ever opens. */
        tls->seq_read = tls->seq_write = TLS_AES_GCM_RECORD_LIMIT;
        return TLS_FAIL;
}

static fn tls_transcript_add(tls_conn address_to tls, p8 address_to msg,
                             positive length)
{
        crypto_sha256_write(address_of tls->transcript, msg, length);
}

static COLD bipolar tls_asn1_length(p8 address_to bytes, positive size,
                               positive address_to at, positive address_to length)
{
        byte_reader reader = byte_reader_open(bytes, size);
        byte_reader lead;
        p8 first;
        positive count;
        positive value = 0;

        (void)byte_reader_skip(&reader, address_to at);
        first = byte_reader_u8(&reader);
        if (!byte_reader_ok(&reader))
                return TLS_FAIL;

        if (first < 0x80)
        {
                address_to length = first;
                address_to at = size - byte_reader_left(&reader);
                return TLS_OK;
        }

        //      Long form: one to three length bytes, the first not zero, and
        //      a value the short form could not have said.
        count = first & 0x7f;
        lead = reader;
        if (!count || count > 3 || !byte_reader_u8(&lead))
                return TLS_FAIL;
        for (positive taken = 0; taken < count; taken++)
                value = (value << 8) | byte_reader_u8(&reader);
        if (!byte_reader_ok(&reader) || value < 0x80)
                return TLS_FAIL;
        address_to length = value;
        address_to at = size - byte_reader_left(&reader);
        return TLS_OK;
}

static COLD bipolar tls_asn1_enter(p8 address_to bytes, positive size, p8 tag,
                              positive address_to at, positive address_to stop)
{
        byte_reader reader = byte_reader_open(bytes, size);
        positive length = 0;

        (void)byte_reader_skip(&reader, address_to at);
        if (byte_reader_u8(&reader) != tag || !byte_reader_ok(&reader))
                return TLS_FAIL;
        address_to at = size - byte_reader_left(&reader);
        if (tls_asn1_length(bytes, size, at, address_of length) ||
            length > size - address_to at)
                return TLS_FAIL;
        address_to stop = address_to at + length;
        return TLS_OK;
}

/* Past one value of whatever tag it carries: entering it and then taking its
   end for the next value's start is the whole of skipping it. */
static COLD bipolar tls_asn1_skip(p8 address_to bytes, positive size, positive address_to at)
{
        byte_reader reader = byte_reader_open(bytes, size);
        positive stop = 0;

        (void)byte_reader_skip(&reader, address_to at);
        if (tls_asn1_enter(bytes, size, byte_reader_u8(&reader), at,
                           address_of stop) ||
            !byte_reader_ok(&reader))
                return TLS_FAIL;

        address_to at = stop;
        return TLS_OK;
}

/* One whole value of the given tag, header included, and past it. */
static COLD bipolar tls_asn1_take(p8 address_to bytes, positive size, p8 tag,
                                  positive address_to at,
                                  p8 address_to address_to value,
                                  positive address_to length)
{
        byte_reader reader = byte_reader_open(bytes, size);
        positive start = address_to at;
        positive stop = 0;

        (void)byte_reader_skip(&reader, start);
        if (tls_asn1_enter(bytes, size, tag, at, address_of stop) ||
            !byte_reader_ok(&reader))
                return TLS_FAIL;
        address_to value = (p8 address_to)byte_reader_here(&reader);
        address_to length = stop - start;
        address_to at = stop;
        return TLS_OK;
}

/* A BOOLEAN DEFAULT FALSE: basicConstraints' cA, an Extension's critical.
   DER omits a component carrying its default value, so one that is
   encoded can only be canonical TRUE; accepting FALSE lets BER and DER
   validators disagree over the same signed bytes. None of 1,900
   certificates served by 652 public HTTPS hosts (2026-09-28) spells
   either FALSE. */
static COLD bipolar tls_asn1_true(p8 address_to bytes, positive stop,
                                  positive address_to at, bool address_to value)
{
        positive boolean_stop = 0;

        if (address_to at >= stop || bytes[address_to at] != 0x01)
                return TLS_OK;
        if (tls_asn1_enter(bytes, stop, 0x01, at, address_of boolean_stop) ||
            address_to at + 1 != boolean_stop || bytes[address_to at] != 0xff)
                return TLS_FAIL;
        address_to value = true;
        address_to at = boolean_stop;
        return TLS_OK;
}

/* RSA AlgorithmIdentifier parameters: the historical NULL, or nothing. */
static COLD bool tls_null_or_absent(p8 address_to der, positive at,
                                    positive stop)
{
        return at == stop || (at + 2 == stop && der[at] == 0x05 && !der[at + 1]);
}

static COLD bool tls_oid_is(const p8 address_to bytes, positive length,
                       const p8 address_to oid, positive oid_length)
{
        return length == oid_length && !memory_compare(bytes, oid, oid_length);
}

/* Version is DEFAULT v1. DER omits a field carrying its default value, so an
   explicit wrapper may contain only v2 or v3. Keep this small state transition
   separate so the default, both valid encodings, and the non-canonical v1
   spelling can be tested without constructing a whole signed certificate. */
static COLD bipolar tls_parse_version(p8 address_to der, positive size,
                                 positive address_to at,
                                 p8 address_to version)
{
        positive version_stop = 0;
        positive value_stop = 0;

        address_to version = 0;
        if (address_to at >= size || der[address_to at] != 0xa0)
                return TLS_OK;
        if (tls_asn1_enter(der, size, 0xa0, at, address_of version_stop) ||
            tls_asn1_enter(der, version_stop, 0x02, at, address_of value_stop) ||
            address_to at + 1 != value_stop || value_stop != version_stop ||
            !der[address_to at] || der[address_to at] > 2)
                return TLS_FAIL;
        address_to version = der[address_to at];
        address_to at = version_stop;
        return TLS_OK;
}

/* X.690 DER OBJECT IDENTIFIER content: each subidentifier is base-128 with
   bit 8 as continuation, minimally encoded, so its first octet is never 0x80
   and its last octet clears the continuation bit.  A 0x80 inside an arc is
   seven zero bits and stays valid (81 80 00 is 16384).  Checking contents,
   not only tag and length, matters to extension identity: a non-canonical
   spelling could otherwise evade duplicate-OID detection.  Long-form OID
   length is already refused by tls_asn1_length. */
static COLD bool tls_oid_content_der(const p8 address_to bytes, positive length)
{
        positive at = 0;

        if (!length)
                return false;
        while (at < length)
        {
                if (bytes[at] == 0x80)
                        return false;
                while (bytes[at++] & 0x80)
                        if (at == length)
                                return false;
        }
        return true;
}

static COLD bipolar tls_asn1_enter_oid(p8 address_to bytes, positive size,
                                  positive address_to at, positive address_to stop)
{
        if (tls_asn1_enter(bytes, size, 0x06, at, stop))
                return TLS_FAIL;
        if (!tls_oid_content_der(bytes + address_to at,
                                 address_to stop - address_to at))
                return TLS_FAIL;
        return TLS_OK;
}

/*
        The same DER read through a cursor, for the revocation parsers: a
        CRL and an OCSP response arrive over plain HTTP and in a handshake,
        so nothing in them is trusted until a signature over them verifies,
        and they hold no index into what they read (security_hygiene).

        tls_der_value is one value of the tag at the reader's front: its
        contents as a reader of their own, and the reader past it. A wrong
        tag, a length tls_asn1_length refuses or one longer than what is left
        fails both. tls_der_peek is the tag at the front, or 0 at the end or
        on a failed reader (no DER tag here is 0). tls_der_whole is one value,
        header and all, for a signature over it or a byte comparison.
*/
static COLD byte_reader tls_der_value(byte_reader address_to reader, p8 tag)
{
        p8 address_to bytes = (p8 address_to)byte_reader_here(reader);
        positive at = 0;
        positive stop = 0;

        if (!bytes || tls_asn1_enter(bytes, byte_reader_left(reader), tag,
                                     address_of at, address_of stop))
        {
                (void)byte_reader_take(reader, positive_max);
                return byte_reader_dead();
        }
        (void)byte_reader_skip(reader, at);
        return byte_reader_window(reader, stop - at);
}

static COLD p8 tls_der_peek(byte_reader reader)
{
        return byte_reader_u8(&reader);
}

static COLD p8 address_to tls_der_whole(byte_reader address_to reader, p8 tag,
                                        positive address_to length)
{
        p8 address_to start = (p8 address_to)byte_reader_here(reader);
        positive before = byte_reader_left(reader);
        byte_reader inside = tls_der_value(reader, tag);

        address_to length = 0;
        if (!byte_reader_ok(&inside))
                return null;
        address_to length = before - byte_reader_left(reader);
        return start;
}

/* A DER INTEGER's contents: at least one octet, and no first octet that only
   repeats the sign of the next (X.690 8.3.2). */
static COLD bool tls_der_integer(byte_reader contents)
{
        positive length = byte_reader_left(&contents);
        p8 first = byte_reader_u8(&contents);
        p8 second;

        if (!byte_reader_ok(&contents))
                return false;
        if (length == 1)
                return true;
        second = byte_reader_u8(&contents);
        return !(first == 0 && !(second & 0x80)) &&
               !(first == 0xff && (second & 0x80));
}

/* The one INTEGER whose contents are a single octet below 0x80 and the one
   ENUMERATED: value, or -1 for anything else (a longer or negative one). */
static COLD bipolar tls_der_small(byte_reader address_to reader, p8 tag)
{
        byte_reader contents = tls_der_value(reader, tag);
        p8 value = byte_reader_u8(&contents);

        return byte_reader_end(&contents) && value < 0x80 ? value : -1;
}

/* A BOOLEAN DEFAULT FALSE under an implicit context tag, which DER spells
   only when TRUE (see tls_asn1_true): absent is false, present has to be
   one octet 0xff. False with the reader failed for any other spelling. */
static COLD bool tls_der_flag(byte_reader address_to reader, p8 tag)
{
        byte_reader contents;

        if (tls_der_peek(*reader) != tag)
                return false;
        contents = tls_der_value(reader, tag);
        if (byte_reader_u8(&contents) == 0xff && byte_reader_end(&contents))
                return true;
        (void)byte_reader_take(reader, positive_max);
        return false;
}

/* A named bit list's BIT STRING contents (ReasonFlags), held to DER's one
   spelling as tls_parse_key_usage holds keyUsage: the unused count is the
   final octet's trailing zeros, and that octet is not zero. The empty list is
   one octet, 0. */
static COLD bool tls_der_named_bits(byte_reader contents)
{
        p8 unused = byte_reader_u8(&contents);
        p8 final;
        p8 zeros = 0;

        if (!byte_reader_ok(&contents) || unused > 7)
                return false;
        if (!byte_reader_left(&contents))
                return !unused;
        (void)byte_reader_skip(&contents, byte_reader_left(&contents) - 1);
        final = byte_reader_u8(&contents);
        if (!final)
                return false;
        while (!(final & 1))
        {
                zeros++;
                final >>= 1;
        }
        return zeros == unused;
}

/* GeneralNames' contents (RFC 5280 4.2.1.6): one name or more, each a whole
   value under one of the nine context tags the CHOICE defines. */
static COLD bool tls_general_names_valid(byte_reader names)
{
        if (!byte_reader_left(&names))
                return false;
        while (byte_reader_left(&names))
        {
                p8 tag = tls_der_peek(names);

                if (tag != 0xa0 && tag != 0x81 && tag != 0x82 && tag != 0xa3 &&
                    tag != 0xa4 && tag != 0xa5 && tag != 0x86 && tag != 0x87 &&
                    tag != 0x88)
                        return false;
                (void)tls_der_value(&names, tag);
        }
        return byte_reader_ok(&names);
}

/* Whether two GeneralNames share a name, byte for byte (RFC 5280 6.3.3
   (b)(2)(i), a CRL's issuingDistributionPoint against the distribution
   point the certificate named). Each list is walked TLS_NAMES_MEET names
   deep at most, so a signed but hostile list costs at most its square. */
#define TLS_NAMES_MEET 16

static COLD bool tls_general_names_meet(byte_reader one, byte_reader two)
{
        for (positive i = 0; byte_reader_left(&one) && i < TLS_NAMES_MEET; i++)
        {
                positive length = 0;
                const p8 address_to name = tls_der_whole(&one, tls_der_peek(one),
                                                         address_of length);
                byte_reader other = two;

                if (!name)
                        return false;
                for (positive j = 0; byte_reader_left(&other) && j < TLS_NAMES_MEET;
                     j++)
                {
                        positive size = 0;
                        const p8 address_to candidate = tls_der_whole(
                            &other, tls_der_peek(other), address_of size);

                        if (!candidate)
                                break;
                        if (size == length && !memory_compare(candidate, name, size))
                                return true;
                }
        }
        return false;
}

/* DER INTEGERs used for keys and ECDSA signatures are strictly positive and
   minimally encoded.  Return the magnitude without the one permitted sign
   octet. */
static COLD bool tls_positive_integer(p8 address_to bytes, positive at,
                                 positive stop, positive address_to value_at,
                                 positive address_to value_length)
{
        if (at >= stop || (bytes[at] & 0x80))
                return false;

        if (!bytes[at])
        {
                if (at + 1 >= stop || !(bytes[at + 1] & 0x80))
                        return false;
                at++;
        }

        address_to value_at = at;
        address_to value_length = stop - at;
        return true;
}

/* The certificate signature algorithms: ecdsa-with-SHA256/384/512
   (1.2.840.10045.4.3.2-4) and sha256/384/512WithRSAEncryption
   (1.2.840.113549.1.1.11-13).  The kind is 1-3 for ECDSA and 5-7 for
   PKCS#1 v1.5, its low two bits naming SHA-256, -384 or -512; 0 is any
   other algorithm.  SHA-512 is here because real chains use it: of 640
   public HTTPS hosts openssl verified on 2026-09-28, nine served an
   intermediate signed sha512WithRSAEncryption (Certum, D-Trust, Actalis,
   ACCV, NetLock, e-Szigno). */
static COLD p8 tls_signature_kind(const p8 address_to oid, positive length)
{
        p8 last = length ? oid[length - 1] : 0;

        if (length == 8 && !memory_compare(oid, tls_oid_ecdsa_with, 7) &&
            last >= 2 && last <= 4)
                return (p8)(last - 1);
        if (length == 9 && !memory_compare(oid, tls_oid_pkcs1, 8) &&
            last >= 0x0b && last <= 0x0d)
                return (p8)(last - 0x0b + 5);
        return 0;
}

/* ECDSA parameters must be absent; RSA's historical NULL may be present or
   absent, but no other parameter or trailing value is accepted. */
static COLD bool tls_signature_algorithm(p8 address_to der, positive size,
                                    positive address_to at,
                                    p8 address_to address_to oid,
                                    positive address_to oid_length)
{
        positive alg_stop = 0;
        positive oid_at;
        positive oid_stop = 0;
        p8 kind;

        if (tls_asn1_enter(der, size, 0x30, at, address_of alg_stop))
                return false;
        oid_at = address_to at;
        if (tls_asn1_enter_oid(der, alg_stop, address_of oid_at,
                               address_of oid_stop))
                return false;

        kind = tls_signature_kind(der + oid_at, oid_stop - oid_at);
        if (!kind || (oid_stop != alg_stop &&
                      (!(kind & 4) || !tls_null_or_absent(der, oid_stop, alg_stop))))
                return false;

        address_to oid = der + oid_at;
        address_to oid_length = oid_stop - oid_at;
        address_to at = alg_stop;
        return true;
}

/* The IA5String name forms -- rfc822Name, dNSName, URI -- carry printable
   ASCII here and nothing else. */
static COLD bool tls_printable(const p8 address_to bytes, positive length)
{
        for (positive at = 0; at < length; at++)
                if (bytes[at] < 0x20 || bytes[at] >= 0x7f)
                        return false;
        return true;
}

/* Whether a dNSName of two or more labels is a public suffix under the
   ICANN rules of the Public Suffix List: a rule names it, or a wildcard
   rule names its parent and no exception names it. suffixes.inc keeps the
   names with their labels reversed ("uk.co"): the wildcard parents and the
   exceptions as short lists, and the other multi-label rules sorted and
   front-coded -- an upper-case letter says how many leading bytes an
   entry shares with the one before, 'A' none, and lower-case text follows
   up to the next capital -- in one run a top-level label, each run found
   by binary search over tls_public_suffix_runs. One-label names are
   refused before this (every TLD is a suffix, known or not). */
static COLD bool tls_suffix_in(const char address_to list, positive size,
                               const p8 address_to key, positive length)
{
        for (positive at = 0; at < size && list[at];)
        {
                positive entry = memory_span_without_byte((address_any)(list + at), 0,
                                                          size - at);

                if (entry == length && !memory_compare(list + at, key, length))
                        return true;
                at += entry + 1;
        }
        return false;
}

static COLD bool tls_public_suffix(const p8 address_to name, positive length)
{
        p8 key[256];
        p8 entry[256];
        positive entry_length = 0;
        positive key_length = 0;
        positive label = length;
        positive matched = 0;
        positive low = 0;
        positive high = array_count(tls_public_suffix_runs);
        positive top;
        positive cut;
        bool behind = false;
        const char address_to table;

        if (length >= sizeof key)
                return false;
        while (label)
        {
                positive start = label;

                while (start && name[start - 1] != '.')
                        start--;
                if (key_length)
                        key[key_length++] = '.';
                for (positive i = start; i < label; i++)
                        key[key_length++] = name[i] >= 'A' && name[i] <= 'Z'
                                                ? (p8)(name[i] + 32) : name[i];
                label = start ? start - 1 : 0;
        }
        cut = key_length;
        while (cut && key[cut - 1] != '.')
                cut--;
        top = memory_span_without_byte(key, '.', key_length);
        if (tls_suffix_in(tls_public_suffix_exceptions,
                          sizeof tls_public_suffix_exceptions, key, key_length))
                return false;
        if (cut && tls_suffix_in(tls_public_suffix_wildcards,
                                 sizeof tls_public_suffix_wildcards, key, cut - 1))
                return true;

        /* The run whose top-level label is the key's: each run's first
           entry is spelled whole after its 'A'. */
        while (low < high)
        {
                positive middle = low + (high - low) / 2;
                const char address_to run = tls_public_suffixes +
                                            tls_public_suffix_runs[middle] + 1;
                positive run_top = memory_span_without_byte(
                    (address_any)run, '.',
                    sizeof tls_public_suffixes - tls_public_suffix_runs[middle] - 1);
                b32 order = memory_compare(run, key, run_top < top ? run_top : top);

                if (!order && run_top == top)
                {
                        low = middle;
                        break;
                }
                if (order < 0 || (!order && run_top < top))
                        low = middle + 1;
                else
                        high = middle;
        }
        if (low == array_count(tls_public_suffix_runs))
                return false;

        /* matched counts the bytes of the entry that agree with the key.
           Once an entry is below the key at matched, each entry sharing
           more than matched bytes with it is too and is passed over, and
           the first entry above the key, or the next run, ends the
           search. */
        table = tls_public_suffixes + tls_public_suffix_runs[low];
        do
        {
                positive shared = (positive)(*table++ - 'A');

                if (behind && shared > matched)
                {
                        while (*table && (*table < 'A' || *table > 'Z'))
                                table++;
                        continue;
                }
                entry_length = shared < entry_length ? shared : entry_length;
                matched = shared < matched ? shared : matched;
                while (*table && (*table < 'A' || *table > 'Z'))
                {
                        p8 byte = (p8)*table++;

                        if (entry_length < sizeof entry)
                                entry[entry_length++] = byte;
                }
                while (matched < key_length && matched < entry_length &&
                       entry[matched] == key[matched])
                        matched++;
                if (matched == key_length && entry_length == key_length)
                        return true;
                behind = matched == entry_length ||
                         (matched < key_length && entry[matched] < key[matched]);
                if (!behind)
                        return false;
        } while (*table && *table != 'A');
        return false;
}

/* A presented dNSName against a host already canonical (tls_general_name_
   match). An absolute name's trailing dot is dropped, as Chrome drops it:
   "example.com." and "example.com" are one name. */
static COLD bool tls_host_match(string_address host, p8 address_to name,
                           positive name_length)
{
        positive host_length = string_length(host);
        string_address star;

        if (name_length && name[name_length - 1] == '.')
                name_length--;
        if (name_length == host_length &&
            !memory_compare_ascii_case(name, host, host_length))
                return true;

        if (!name_length || name[0] != '*' || name_length < 3 || name[1] != '.')
                return false;

        /* A wildcard leaves at least two labels beneath it, and those are
           not a public suffix. "*.com" and "*.co.uk" would each stand for
           every host in a whole registry; no issuer means to say that, and
           a client that reads it as written hands one certificate the
           internet. Chrome refuses the same names (ICANN rules only, so
           *.appspot.com stands). */
        {
                positive rest = name_length - 2;
                positive dot = memory_span_without_byte(name + 2, '.', rest);

                if (dot + 1 >= rest)
                        return false;
        }

        /* The star stands for one whole label, never an empty one: a host
           ".example.com" is not a name "*.example.com" covers. The suffix
           list is read last, for the one wildcard that would match. */
        star = string_first_of(host, '.');
        if (!star || star == host || !star[1])
                return false;

        return string_length(star) == name_length - 1 &&
               !memory_compare_ascii_case(star, (string_address)(name + 1),
                                          name_length - 1) &&
               !tls_public_suffix(name + 2, name_length - 2);
}

/* An IPv6 literal, bare or bracketed, as 16 bytes (RFC 4291 2.2): up to
   eight groups of one to four hex digits, one "::" standing for one or more
   zero groups, and a dotted IPv4 tail for the last two. text is writable
   and terminated at length; a zone ("%eth0") or anything else is refused. */
static COLD bool net_ipv6_literal(p8 address_to text, positive length,
                                  p8 address_to out)
{
        p8 groups[16];
        positive count = 0;
        positive gap = positive_max;
        positive at = 0;

        if (length && text[0] == '[')
        {
                if (length < 2 || text[length - 1] != ']')
                        return false;
                text[--length] = end;
                text++;
                length--;
        }
        if (memory_span_without_byte(text, ':', length) == length)
                return false;
        if (length >= 2 && text[0] == ':' && text[1] == ':')
        {
                gap = 0;
                at = 2;
        }
        while (at < length)
        {
                positive rest = length - at;
                positive digits = 0;
                p32 group = 0;

                if (count > 14)
                        return false;
                if (count <= 12 &&
                    memory_span_without_byte(text + at, ':', rest) == rest &&
                    memory_span_without_byte(text + at, '.', rest) < rest)
                {
                        bipolar quad = string_to_host((string_address)(text + at));

                        if (quad < 0)
                                return false;
                        for (positive k = 0; k < 4; k++)
                                groups[count++] = (p8)((p32)quad >> (24 - 8 * k));
                        break;
                }
                for (; at < length && digits < 5; at++, digits++)
                {
                        p8 c = text[at];

                        if (c >= 'A' && c <= 'F')
                                c = (p8)(c + 32);
                        if (c >= '0' && c <= '9')
                                group = group << 4 | (p32)(c - '0');
                        else if (c >= 'a' && c <= 'f')
                                group = group << 4 | (p32)(c - 'a' + 10);
                        else
                                break;
                }
                if (!digits || digits > 4)
                        return false;
                network_store_16(groups + count, (p16)group);
                count += 2;
                if (at == length)
                        break;
                if (text[at++] != ':' || at == length)
                        return false;
                if (text[at] == ':')
                {
                        if (gap != positive_max)
                                return false;
                        gap = count;
                        at++;
                }
        }
        if (gap == positive_max ? count != 16 : count > 14)
                return false;
        gap = gap < count ? gap : count;
        memory_fill(out, 0, 16);
        memory_copy(out, groups, gap);
        memory_copy(out + 16 - (count - gap), groups + gap, count - gap);
        return true;
}

/* The host as RFC 6125 and Chrome compare it: one trailing dot dropped, an
   IPv4 or IPv6 literal only against an iPAddress of its own length, and
   anything else only against a dNSName. */
static COLD bool tls_general_name_match(string_address host, p8 tag,
                                   p8 address_to name, positive name_length)
{
        p8 canonical[256];
        p8 address[16];
        positive length = string_length(host);
        bipolar quad;

        if (length && host[length - 1] == '.')
                length--;
        if (!length || length >= sizeof canonical)
                return false;
        memory_copy(canonical, host, length);
        canonical[length] = end;
        if (net_ipv6_literal(canonical, length, address))
                return tag == 0x87 && name_length == 16 &&
                       !memory_compare(name, address, 16);
        quad = string_to_host((string_address)canonical);
        if (quad >= 0)
                return tag == 0x87 && name_length == 4 &&
                       network_load_32(name) == (p32)quad;
        return tag == 0x82 &&
               tls_host_match((string_address)canonical, name, name_length);
}

/* Parse the signed GeneralNames value once, all the way to its declared end.
   A matching first entry does not authorize the certificate until every
   later entry and the outer DER framing have also been validated. */
static COLD bool tls_parse_san(p8 address_to value, positive length,
                          string_address host, bool address_to matched)
{
        positive at = 0;
        positive stop = 0;
        bool found = false;

        if (tls_asn1_enter(value, length, 0x30, address_of at,
                           address_of stop) ||
            stop != length || at == stop)
                return false;

        while (at < stop)
        {
                positive name_stop = 0;
                p8 tag = value[at];

                if (tag != 0xa0 && tag != 0x81 && tag != 0x82 &&
                    tag != 0xa3 && tag != 0xa4 && tag != 0xa5 &&
                    tag != 0x86 && tag != 0x87 && tag != 0x88)
                        return false;
                if (tls_asn1_enter(value, stop, tag, address_of at,
                                   address_of name_stop))
                        return false;
                if ((tag == 0x81 || tag == 0x82 || tag == 0x86) &&
                    !tls_printable(value + at, name_stop - at))
                        return false;
                if (tag == 0x87 && name_stop - at != 4 &&
                    name_stop - at != 16)
                        return false;
                if (tag == 0x88 &&
                    !tls_oid_content_der(value + at, name_stop - at))
                        return false;
                if (host && tls_general_name_match(host, tag, value + at,
                                                   name_stop - at))
                        found = true;
                at = name_stop;
        }

        if (matched)
                address_to matched = found;
        return at == stop;
}

/* Where a name falls against one subtree base of its own form. */
enum { TLS_OUTSIDE, TLS_UNSURE, TLS_WITHIN };

/* Chrome's DNSNameMatches: trailing dots dropped and case ignored, an empty
   base holds every name, "b" holds b and every name under it and ".b" only
   the names under it. In an excluded subtree a wildcard *.s also falls in
   a base x.s, one label above s, since its star can stand for x. */
static COLD bool tls_dns_within(p8 address_to name, positive length,
                                p8 address_to base, positive base_length,
                                bool excluded)
{
        positive dot = memory_span_without_byte(base, '.', base_length);

        if (length && name[length - 1] == '.')
                length--;
        if (base_length && base[base_length - 1] == '.')
                base_length--;
        if (!base_length)
                return true;
        if (excluded && length > 2 && name[0] == '*' && name[1] == '.' &&
            dot < base_length && base_length - dot - 1 == length - 2 &&
            !memory_compare_ascii_case(base + dot + 1, name + 2, length - 2))
                return true;
        if (length < base_length ||
            memory_compare_ascii_case(name + length - base_length, base,
                                      base_length))
                return false;
        return length == base_length || base[0] == '.' ||
               name[length - base_length - 1] == '.';
}

/* RFC 5280 4.2.1.10 for an rfc822Name: a base holding an '@' names one
   mailbox, its local part exact; ".b" holds every mailbox on a host under
   b, and "b" every mailbox on host b. A name without a mailbox is unsure. */
static COLD p8 tls_email_within(p8 address_to name, positive length,
                                p8 address_to base, positive base_length)
{
        positive host = length;
        positive base_mailbox = memory_span_without_byte(base, '@', base_length);

        while (host && name[host - 1] != '@')
                host--;
        if (!host)
                return TLS_UNSURE;
        if (base_mailbox < base_length)
                return length == base_length &&
                               !memory_compare(name, base, host) &&
                               !memory_compare_ascii_case(name + host, base + host,
                                                          length - host)
                           ? TLS_WITHIN
                           : TLS_OUTSIDE;
        name += host;
        length -= host;
        if (base_length && base[0] == '.'
                ? length > base_length &&
                      !memory_compare_ascii_case(name + length - base_length, base,
                                                 base_length)
                : length == base_length &&
                      !memory_compare_ascii_case(name, base, length))
                return TLS_WITHIN;
        return TLS_OUTSIDE;
}

/* The next byte of a DirectoryString as RFC 4518 and OpenSSL's canonical
   form compare it: ASCII letters folded, leading and trailing white space
   gone and each inner run of it one space; -1 past the end. */
static COLD bipolar tls_folded_next(const p8 address_to text, positive length,
                                    positive address_to at)
{
        positive i = address_to at;
        bool space = false;

        while (i < length && (text[i] == ' ' || (text[i] >= 9 && text[i] <= 13)))
        {
                i++;
                space = true;
        }
        if (i == length)
                return -1;
        if (space && address_to at)
        {
                address_to at = i;
                return ' ';
        }
        address_to at = i + 1;
        return text[i] >= 'A' && text[i] <= 'Z' ? text[i] + 32 : text[i];
}

/* Two attribute values, each a whole TLV. PrintableString, UTF8String,
   IA5String, VisibleString and an ASCII TeletexString compare folded, which
   is how OpenSSL and Chrome read them; BMPString, UniversalString and a
   TeletexString past ASCII would need converting first and are unsure
   unless the bytes agree; any other type compares exactly. */
static COLD p8 tls_directory_value(const p8 address_to one, positive one_length,
                                   const p8 address_to two, positive two_length)
{
        const p8 address_to text[2] = {one, two};
        positive size[2] = {one_length, two_length};
        positive cursor[2] = {0, 0};
        bipolar a;
        bipolar b;

        if (one_length == two_length && !memory_compare(one, two, one_length))
                return TLS_WITHIN;
        for (positive k = 0; k < 2; k++)
        {
                positive at = 0;
                positive stop = 0;
                p8 tag = text[k][0];

                if (tls_asn1_enter((p8 address_to)text[k], size[k], tag,
                                   address_of at, address_of stop) ||
                    stop != size[k])
                        return TLS_UNSURE;
                text[k] += at;
                size[k] = stop - at;
                if (tag == 0x1c || tag == 0x1e ||
                    (tag == 0x14 && !tls_printable(text[k], size[k])))
                        return TLS_UNSURE;
                if (tag != 0x0c && tag != 0x13 && tag != 0x14 && tag != 0x16 &&
                    tag != 0x1a)
                        return TLS_OUTSIDE;
        }
        do
        {
                a = tls_folded_next(text[0], size[0], address_of cursor[0]);
                b = tls_folded_next(text[1], size[1], address_of cursor[1]);
        } while (a == b && a >= 0);
        return a == b ? TLS_WITHIN : TLS_OUTSIDE;
}

/* A Name against a directoryName base, both whole DER Names: within when
   the base's RDNs begin the name's. Equal RDN bytes agree outright; else
   one attribute each of the same type compares by value, and any other
   difference -- a multi-valued RDN, a Name that does not parse -- is
   unsure. */
static COLD p8 tls_directory_within(p8 address_to name, positive length,
                                    p8 address_to base, positive base_length)
{
        p8 address_to der[2] = {name, base};
        positive at[2] = {0, 0};
        positive stop[2] = {0, 0};
        p8 verdict = TLS_WITHIN;

        if (tls_asn1_enter(name, length, 0x30, address_of at[0],
                           address_of stop[0]) ||
            tls_asn1_enter(base, base_length, 0x30, address_of at[1],
                           address_of stop[1]))
                return TLS_UNSURE;
        while (at[1] < stop[1])
        {
                positive rdn[2] = {at[0], at[1]};
                positive rdn_stop[2] = {0, 0};
                positive value[2];
                p8 same = TLS_WITHIN;

                if (at[0] == stop[0])
                        return TLS_OUTSIDE;
                for (positive k = 0; k < 2; k++)
                        if (tls_asn1_enter(der[k], stop[k], 0x31,
                                           address_of rdn[k],
                                           address_of rdn_stop[k]))
                                return TLS_UNSURE;
                if (rdn_stop[0] - at[0] != rdn_stop[1] - at[1] ||
                    memory_compare(name + at[0], base + at[1],
                                   rdn_stop[0] - at[0]))
                {
                        for (positive k = 0; k < 2; k++)
                        {
                                positive ava_stop = 0;

                                if (tls_asn1_enter(der[k], rdn_stop[k], 0x30,
                                                   address_of rdn[k],
                                                   address_of ava_stop) ||
                                    ava_stop != rdn_stop[k])
                                        return TLS_UNSURE;
                                value[k] = rdn[k];
                                if (tls_asn1_skip(der[k], ava_stop,
                                                  address_of value[k]) ||
                                    value[k] >= ava_stop)
                                        return TLS_UNSURE;
                        }
                        /* rdn[k] is each attribute's type, value[k] its
                           value, which runs to the end of the RDN. */
                        if (value[0] - rdn[0] != value[1] - rdn[1] ||
                            memory_compare(name + rdn[0], base + rdn[1],
                                           value[0] - rdn[0]))
                                return TLS_OUTSIDE;
                        same = tls_directory_value(
                            name + value[0], rdn_stop[0] - value[0],
                            base + value[1], rdn_stop[1] - value[1]);
                }
                if (same == TLS_OUTSIDE)
                        return TLS_OUTSIDE;
                verdict = same < verdict ? same : verdict;
                at[0] = rdn_stop[0];
                at[1] = rdn_stop[1];
        }
        return verdict;
}

/* One name a certificate carries -- tag its GeneralName form, and name its
   content, or a Name TLV for a directoryName -- against a NameConstraints
   value (RFC 5280 4.2.1.10): refused if it may fall in an excluded subtree
   of its form, or if permitted subtrees of its form exist and it surely
   falls in none. A form this client cannot evaluate (otherName, x400,
   ediParty, URI, registeredID), or a null name, is unsure, so a subtree of
   that form refuses it either way: RFC 5280 lets a client reject what it
   cannot process, and a name it cannot place is what an issuer's limits
   exist to catch.

   Tag 0 checks the value instead, once, where the extension is parsed:
   permitted then excluded, at least one, neither empty, and each subtree a
   base alone, since minimum and maximum are fixed at 0 and absent. A
   dNSName, rfc822Name or URI base is printable, an iPAddress base an
   address and a contiguous mask of 4 or 16 bytes each, and a
   directoryName base one whole Name. */
static COLD bool tls_name_allowed(p8 address_to constraints, positive length,
                                  p8 tag, p8 address_to name,
                                  positive name_length)
{
        positive at = 0;
        positive stop = 0;
        p8 last = 0;
        bool form = false;
        bool permitted = false;

        if (tls_asn1_enter(constraints, length, 0x30, address_of at,
                           address_of stop) ||
            stop != length || at == stop)
                return false;
        while (at < stop)
        {
                p8 which = constraints[at];
                positive trees_stop = 0;

                if ((which != 0xa0 && which != 0xa1) || which <= last ||
                    tls_asn1_enter(constraints, stop, which, address_of at,
                                   address_of trees_stop) ||
                    at == trees_stop)
                        return false;
                last = which;
                while (at < trees_stop)
                {
                        positive tree_stop = 0;
                        positive base_stop = 0;
                        p8 address_to base;
                        positive base_length;
                        p8 base_tag;
                        p8 verdict;

                        if (tls_asn1_enter(constraints, trees_stop, 0x30,
                                           address_of at, address_of tree_stop) ||
                            at == tree_stop)
                                return false;
                        base_tag = constraints[at];
                        if (tls_asn1_enter(constraints, tree_stop, base_tag,
                                           address_of at, address_of base_stop) ||
                            base_stop != tree_stop)
                                return false;
                        base = constraints + at;
                        base_length = base_stop - at;
                        at = tree_stop;
                        if (!tag)
                        {
                                positive name_at = 0;
                                positive name_stop = 0;
                                p8 partial = 0;

                                if ((base_tag == 0x81 || base_tag == 0x82 ||
                                     base_tag == 0x86) &&
                                    !tls_printable(base, base_length))
                                        return false;
                                if (base_tag == 0xa4 &&
                                    (tls_asn1_enter(base, base_length, 0x30,
                                                    address_of name_at,
                                                    address_of name_stop) ||
                                     name_stop != base_length))
                                        return false;
                                if (base_tag != 0x87)
                                        continue;
                                if (base_length != 8 && base_length != 32)
                                        return false;
                                for (positive i = base_length / 2; i < base_length; i++)
                                {
                                        p8 inverse = (p8)~base[i];

                                        if (partial ? base[i]
                                                    : inverse & (p8)(inverse + 1))
                                                return false;
                                        partial = base[i] != 0xff;
                                }
                                continue;
                        }
                        if (base_tag != tag)
                                continue;
                        if (!name)
                                verdict = TLS_UNSURE;
                        else if (tag == 0x82)
                                verdict = tls_dns_within(name, name_length, base,
                                                         base_length, which == 0xa1)
                                              ? TLS_WITHIN : TLS_OUTSIDE;
                        else if (tag == 0x87)
                        {
                                verdict = base_length == 2 * name_length
                                              ? TLS_WITHIN : TLS_OUTSIDE;
                                for (positive i = 0; verdict && i < name_length; i++)
                                        if ((name[i] ^ base[i]) & base[name_length + i])
                                                verdict = TLS_OUTSIDE;
                        }
                        else if (tag == 0x81)
                                verdict = tls_email_within(name, name_length, base,
                                                           base_length);
                        else if (tag == 0xa4)
                                verdict = tls_directory_within(name, name_length,
                                                               base, base_length);
                        else
                                verdict = TLS_UNSURE;
                        if (which == 0xa1 && verdict != TLS_OUTSIDE)
                                return false;
                        if (which == 0xa0)
                        {
                                form = true;
                                permitted |= verdict == TLS_WITHIN;
                        }
                }
        }
        return !form || permitted;
}

static COLD bipolar tls_parse_ecdsa_sig(p8 address_to sig, positive length,
                                   p8 address_to r, positive address_to r_length,
                                   p8 address_to s, positive address_to s_length)
{
        p8 address_to into[2] = {r, s};
        positive address_to into_length[2] = {r_length, s_length};
        positive at = 0;
        positive stop = 0;

        if (tls_asn1_enter(sig, length, 0x30, address_of at, address_of stop) ||
            stop != length)
                return TLS_FAIL;
        for (positive k = 0; k < 2; k++)
        {
                positive value_stop = 0;
                positive value_at;
                positive value_length;

                if (tls_asn1_enter(sig, stop, 0x02, address_of at,
                                   address_of value_stop) ||
                    !tls_positive_integer(sig, at, value_stop, address_of value_at,
                                          address_of value_length) ||
                    value_length > 48)
                        return TLS_FAIL;
                address_to into_length[k] = value_length;
                memory_copy(into[k], sig + value_at, value_length);
                at = value_stop;
        }
        return at == stop ? TLS_OK : TLS_FAIL;
}

typedef struct
{
        p8 address_to tbs;
        positive tbs_length;
        p8 address_to issuer;
        positive issuer_length;
        p8 address_to subject;
        positive subject_length;
        p8 address_to sig_oid;
        positive sig_oid_length;
        p8 address_to sig;
        positive sig_length;
        /* The subjectAltName GeneralNames and the NameConstraints, each a
           whole DER value checked by the parser, or null when absent. */
        p8 address_to san;
        positive san_length;
        p8 address_to name_constraints;
        positive name_constraints_length;
        /* The first http: caIssuers location, or null (tls_parse_ca_issuers). */
        p8 address_to ca_issuers;
        positive ca_issuers_length;
        /* Revocation. The serial's INTEGER contents, sign octet and all, as
           a CRL entry or an OCSP CertID spells it; the SubjectPublicKey BIT
           STRING past its unused-bits octet, what an OCSP issuerKeyHash and
           a byKey responder hash; the first whole-reason http: CRL
           distribution point and the fullName it is one of (null when there
           is none, tls_parse_crl_points); authorityKeyIdentifier's
           keyIdentifier, or null; and the TLS Feature status_request. */
        p8 address_to serial;
        positive serial_length;
        p8 address_to key_bits;
        positive key_bits_length;
        p8 address_to crl_url;
        positive crl_url_length;
        p8 address_to crl_names;
        positive crl_names_length;
        p8 address_to authority_key;
        positive authority_key_length;
        bool must_staple;
        /* Certificate Transparency: the SubjectPublicKeyInfo whole (what an
           SCT's issuer_key_hash hashes), the extensions' [3] and the SCT list
           extension whole, which the precertificate an SCT signed lacks
           (tls_ct_precert), and the TLS-encoded list inside it, or null. */
        p8 address_to spki;
        positive spki_length;
        p8 address_to extensions;
        p8 address_to sct_extension;
        positive sct_extension_length;
        p8 address_to scts;
        positive scts_length;
        p8 curve;
        p8 qx[48];
        p8 qy[48];
        p8 modulus[CRYPTO_RSA_BYTES];
        positive modulus_length;
        p64 exponent;
        p64 not_before;
        p64 not_after;
        positive path_length;
        bool basic_constraints;
        bool ca;
        bool path_length_present;
        bool key_usage;
        bool digital_signature;
        bool key_cert_sign;
        bool crl_sign;
        bool extended_key_usage;
        bool server_auth;
        bool ocsp_signing;
        bool san_match;
        bool unsupported_critical;
} tls_cert;

static COLD bool tls_date_value(p8 tag, p8 address_to text, positive length,
                           p64 address_to value)
{
        static const p8 days_in_month[12] = {31, 28, 31, 30, 31, 30,
                                              31, 31, 30, 31, 30, 31};
        positive year_digits;
        positive year = 0;
        positive month;
        positive day;
        positive hour;
        positive minute;
        positive second;
        positive i;
        positive limit;
        p64 answer;

        if (tag == 0x17)
                year_digits = 2;
        else if (tag == 0x18)
                year_digits = 4;
        else
                return false;
        if (length != year_digits + 11 || text[length - 1] != 'Z')
                return false;
        for (i = 0; i + 1 < length; i++)
                if (text[i] < '0' || text[i] > '9')
                        return false;

        for (i = 0; i < year_digits; i++)
                year = year * 10 + text[i] - '0';
        if (year_digits == 2)
                year += year >= 50 ? 1900 : 2000;
        /* RFC 5280 fixes the otherwise overlapping ASN.1 time choices:
           1950..2049 use UTCTime and 2050 onward uses GeneralizedTime. A
           GeneralizedTime spelling of a 20xx year is numerically clear but
           non-canonical, and accepting it creates a validator differential
           over signed validity bytes. */
        if (!year || (tag == 0x18 && year < 2050))
                return false;

        month = (positive)(text[year_digits] - '0') * 10 +
                text[year_digits + 1] - '0';
        day = (positive)(text[year_digits + 2] - '0') * 10 +
              text[year_digits + 3] - '0';
        hour = (positive)(text[year_digits + 4] - '0') * 10 +
               text[year_digits + 5] - '0';
        minute = (positive)(text[year_digits + 6] - '0') * 10 +
                 text[year_digits + 7] - '0';
        second = (positive)(text[year_digits + 8] - '0') * 10 +
                 text[year_digits + 9] - '0';
        if (!month || month > 12 || !day || hour > 23 || minute > 59 ||
            second > 59)
                return false;
        limit = days_in_month[month - 1];
        if (month == 2 && (!(year % 4) && (year % 100 || !(year % 400))))
                limit++;
        if (day > limit)
                return false;

        answer = year;
        answer = answer * 100 + month;
        answer = answer * 100 + day;
        answer = answer * 100 + hour;
        answer = answer * 100 + minute;
        answer = answer * 100 + second;
        address_to value = answer;
        return true;
}

/* An OCSP time: GeneralizedTime in every year (RFC 6960 4.2.1), held to the
   one spelling RFC 5280 4.1.2.5.2 allows it, YYYYMMDDHHMMSSZ. From 2050 on
   that is tls_date_value's own GeneralizedTime; before, its digits past the
   century are the UTCTime of the same moment, and the century has to be the
   one that UTCTime implies (so 1950 through 2049). */
static COLD bool tls_date_general(p8 address_to text, positive length,
                                  p64 address_to value)
{
        p64 answer = 0;

        if (tls_date_value(0x18, text, length, value))
                return true;
        if (length != 15 || text[0] < '1' || text[0] > '2' || text[1] < '0' ||
            text[1] > '9' ||
            !tls_date_value(0x17, text + 2, 13, address_of answer) ||
            answer / 1000000000000ull !=
                (p64)(text[0] - '0') * 10 + (p64)(text[1] - '0'))
                return false;
        address_to value = answer;
        return true;
}

/* A date as tls_date_value keeps it, YYYYMMDDHHMMSS, as seconds from the
   start of year 0 of the proleptic Gregorian calendar (the days are
   Hinnant's days_from_civil): how far apart two dates are, for the clock
   skew a revocation answer is allowed. 0 for 0. */
static COLD p64 tls_date_seconds(p64 date)
{
        p64 year = date / 10000000000ull;
        p64 month = date / 100000000 % 100;
        p64 day = date / 1000000 % 100;
        p64 shifted;
        p64 era;
        p64 of_era;
        p64 of_year;
        p64 days;

        if (!year || !month)
                return 0;
        shifted = month <= 2 ? year - 1 : year;
        era = shifted / 400;
        of_era = shifted - era * 400;
        of_year = (153 * (month > 2 ? month - 3 : month + 9) + 2) / 5 + day - 1;
        days = era * 146097 + of_era * 365 + of_era / 4 - of_era / 100 + of_year;
        return ((days * 24 + date / 10000 % 100) * 60 + date / 100 % 100) * 60 +
               date % 100;
}

static COLD bipolar tls_parse_validity(p8 address_to der, positive size,
                                  positive address_to at, tls_cert address_to cert)
{
        p64 address_to times[2] = {address_of cert->not_before,
                                   address_of cert->not_after};
        positive validity_stop = 0;

        if (tls_asn1_enter(der, size, 0x30, at, address_of validity_stop))
                return TLS_FAIL;
        for (positive k = 0; k < 2; k++)
        {
                positive time_stop = 0;
                p8 tag;

                if (address_to at >= validity_stop)
                        return TLS_FAIL;
                tag = der[address_to at];
                if (tls_asn1_enter(der, validity_stop, tag, at,
                                   address_of time_stop) ||
                    !tls_date_value(tag, der + address_to at,
                                    time_stop - address_to at, times[k]))
                        return TLS_FAIL;
                address_to at = time_stop;
        }
        return address_to at == validity_stop &&
                       cert->not_after >= cert->not_before
                   ? TLS_OK
                   : TLS_FAIL;
}

static COLD bipolar tls_parse_basic_constraints(p8 address_to value, positive length,
                                            tls_cert address_to cert)
{
        positive at = 0;
        positive stop = 0;

        if (tls_asn1_enter(value, length, 0x30, address_of at, address_of stop) ||
            stop != length ||
            tls_asn1_true(value, stop, address_of at, address_of cert->ca))
                return TLS_FAIL;
        if (at < stop && value[at] == 0x02)
        {
                positive integer_stop = 0;
                positive bytes;
                positive path = 0;

                if (tls_asn1_enter(value, stop, 0x02, address_of at,
                                   address_of integer_stop))
                        return TLS_FAIL;
                bytes = integer_stop - at;
                if (!bytes || bytes > sizeof(positive) || (value[at] & 0x80) ||
                    (bytes > 1 && !value[at] && !(value[at + 1] & 0x80)))
                        return TLS_FAIL;
                while (at < integer_stop)
                        path = (path << 8) | value[at++];
                cert->path_length = path;
                cert->path_length_present = true;
        }
        if (at != stop || (cert->path_length_present && !cert->ca))
                return TLS_FAIL;
        return TLS_OK;
}

static COLD bipolar tls_parse_key_usage(p8 address_to value, positive length,
                                   tls_cert address_to cert)
{
        byte_reader reader = byte_reader_open(value, length);
        byte_reader bits = tls_der_value(&reader, 0x03);
        positive size;
        p8 first;
        p8 second = 0;

        /* KeyUsage is a named bit list, held to DER's one spelling by
           tls_der_named_bits: the unused-bit count is exactly the trailing-zero
           count in the final nonzero octet, and one or two octets. Merely
           checking that declared unused bits are zero accepts alternate signed
           encodings and trailing zero octets which stricter certificate
           validators reject. */
        if (!byte_reader_end(&reader) || byte_reader_left(&bits) < 2 ||
            !tls_der_named_bits(bits))
                return TLS_FAIL;
        (void)byte_reader_u8(&bits);
        size = byte_reader_left(&bits);
        first = byte_reader_u8(&bits);
        if (size == 2)
                second = byte_reader_u8(&bits);
        /* RFC 5280 defines exactly nine KeyUsage bits. The ninth is
           decipherOnly and is permitted only with keyAgreement. Rejecting
           further set bits matters even though this client does not act on
           them: silently ignoring undefined authorization bits creates a
           profile differential with validators that enforce the schema. */
        if (size > 2 || (size == 2 && (second != 0x80 || !(first & 0x08))))
                return TLS_FAIL;
        cert->digital_signature = (first & 0x80) != 0;
        cert->key_cert_sign = (first & 0x04) != 0;
        cert->crl_sign = (first & 0x02) != 0;
        return TLS_OK;
}

static COLD bipolar tls_parse_extended_key_usage(p8 address_to value, positive length,
                                            tls_cert address_to cert)
{
        positive at = 0;
        positive stop = 0;

        if (tls_asn1_enter(value, length, 0x30, address_of at, address_of stop) ||
            stop != length || at == stop)
                return TLS_FAIL;
        while (at < stop)
        {
                positive oid_stop = 0;

                if (tls_asn1_enter_oid(value, stop, address_of at,
                                       address_of oid_stop))
                        return TLS_FAIL;
                if (tls_oid_is(value + at, oid_stop - at, tls_oid_server_auth, 8))
                        cert->server_auth = true;
                if (tls_oid_is(value + at, oid_stop - at, tls_oid_ocsp_signing, 8))
                        cert->ocsp_signing = true;
                at = oid_stop;
        }
        return TLS_OK;
}

/* The first caIssuers location of an AuthorityInfoAccess value (RFC 5280
   4.2.2.1) that is an http: URI, for tls_verify_chain to fetch when a
   server leaves an intermediate out. It is only a hint -- whatever it
   names is parsed and verified like a served certificate -- so a value
   that does not parse is read as naming nothing. */
static COLD fn tls_parse_ca_issuers(p8 address_to value, positive length,
                                    tls_cert address_to cert)
{
        static const p8 ca_issuers[8] = {0x2b, 0x06, 0x01, 0x05,
                                         0x05, 0x07, 0x30, 0x02};
        positive at = 0;
        positive stop = 0;

        if (tls_asn1_enter(value, length, 0x30, address_of at, address_of stop))
                return;
        while (at < stop && !cert->ca_issuers)
        {
                positive access_stop = 0;
                positive location = 0;
                positive location_stop = 0;

                if (tls_asn1_enter(value, stop, 0x30, address_of at,
                                   address_of access_stop) ||
                    tls_asn1_enter_oid(value, access_stop, address_of at,
                                       address_of location))
                        return;
                if (tls_oid_is(value + at, location - at, ca_issuers, 8) &&
                    !tls_asn1_enter(value, access_stop, 0x86, address_of location,
                                    address_of location_stop) &&
                    location_stop == access_stop && location_stop - location > 7 &&
                    tls_printable(value + location, location_stop - location) &&
                    !memory_compare_ascii_case(value + location, "http://", 7))
                {
                        cert->ca_issuers = value + location;
                        cert->ca_issuers_length = location_stop - location;
                }
                at = access_stop;
        }
}

/* cRLDistributionPoints (RFC 5280 4.2.1.13), every point checked: each a
   distributionPoint [0], reasons [1] and cRLIssuer [2] in that order, not
   reasons alone, the name a fullName [0] of GeneralNames or a
   nameRelativeToCRLIssuer [1]. The one kept is the first point that covers
   every reason and is its own CA's (no reasons, no cRLIssuer) with an http:
   URI in its fullName: that URI is fetched (tls_crl_answer), and the
   fullName is what a partitioned CRL's issuingDistributionPoint has to
   share a name with. A value that does not parse refuses the certificate,
   as OpenSSL refuses it: a distribution point is how the issuer says where
   revocation is published, and one that cannot be read cannot be honoured. */
static COLD bipolar tls_parse_crl_points(p8 address_to value, positive length,
                                         tls_cert address_to cert)
{
        byte_reader reader = byte_reader_open(value, length);
        byte_reader points = tls_der_value(&reader, 0x30);

        if (!byte_reader_end(&reader) || !byte_reader_left(&points))
                return TLS_FAIL;
        while (byte_reader_left(&points))
        {
                byte_reader point = tls_der_value(&points, 0x30);
                byte_reader full = byte_reader_dead();
                bool named = false;
                bool partial = false;
                bool delegated = false;

                if (tls_der_peek(point) == 0xa0)
                {
                        byte_reader name = tls_der_value(&point, 0xa0);
                        p8 form = tls_der_peek(name);
                        byte_reader inside = tls_der_value(&name, form);

                        if ((form != 0xa0 && form != 0xa1) || !byte_reader_end(&name) ||
                            (form == 0xa0 ? !tls_general_names_valid(inside)
                                          : !byte_reader_left(&inside)))
                                return TLS_FAIL;
                        if (form == 0xa0)
                                full = inside;
                        named = true;
                }
                if (tls_der_peek(point) == 0x81)
                {
                        if (!tls_der_named_bits(tls_der_value(&point, 0x81)))
                                return TLS_FAIL;
                        partial = true;
                }
                if (tls_der_peek(point) == 0xa2)
                {
                        if (!tls_general_names_valid(tls_der_value(&point, 0xa2)))
                                return TLS_FAIL;
                        delegated = true;
                }
                if (!byte_reader_end(&point) || (!named && !delegated))
                        return TLS_FAIL;
                if (cert->crl_url || partial || delegated || !byte_reader_ok(&full))
                        continue;
                for (byte_reader names = full; byte_reader_left(&names);)
                {
                        p8 tag = tls_der_peek(names);
                        byte_reader uri = tls_der_value(&names, tag);
                        const p8 address_to text = byte_reader_here(&uri);
                        positive size = byte_reader_left(&uri);

                        if (tag == 0x86 && size > 7 && tls_printable(text, size) &&
                            !memory_compare_ascii_case(text, "http://", 7))
                        {
                                cert->crl_url = (p8 address_to)text;
                                cert->crl_url_length = size;
                                cert->crl_names = (p8 address_to)byte_reader_here(&full);
                                cert->crl_names_length = byte_reader_left(&full);
                                break;
                        }
                }
        }
        return byte_reader_ok(&points) ? TLS_OK : TLS_FAIL;
}

/* authorityKeyIdentifier (RFC 5280 4.2.1.1): keyIdentifier [0], then
   authorityCertIssuer [1] and authorityCertSerialNumber [2], the two only
   together. The identifier is kept; a CRL that names its own has to name
   the same one (tls_crl_answer). */
static COLD bipolar tls_parse_authority_key(p8 address_to value, positive length,
                                            p8 address_to address_to key,
                                            positive address_to key_length)
{
        byte_reader reader = byte_reader_open(value, length);
        byte_reader identifier = tls_der_value(&reader, 0x30);

        address_to key = null;
        address_to key_length = 0;
        if (!byte_reader_end(&reader))
                return TLS_FAIL;
        if (tls_der_peek(identifier) == 0x80)
        {
                byte_reader octets = tls_der_value(&identifier, 0x80);

                address_to key = (p8 address_to)byte_reader_here(&octets);
                address_to key_length = byte_reader_left(&octets);
        }
        if (tls_der_peek(identifier) == 0xa1 &&
            (!tls_general_names_valid(tls_der_value(&identifier, 0xa1)) ||
             !tls_der_integer(tls_der_value(&identifier, 0x82))))
                return TLS_FAIL;
        return byte_reader_end(&identifier) ? TLS_OK : TLS_FAIL;
}

/* The TLS Feature extension (RFC 7633): a SEQUENCE of INTEGERs, each a TLS
   extension number. status_request (5) among them is must-staple: the
   leaf is refused in every tier unless the server staples a good OCSP
   answer. Numbers are non-negative and at most 65535; an extension that
   does not parse refuses the certificate, since it may be the one that
   asked for a staple. */
static COLD bipolar tls_parse_tls_feature(p8 address_to value, positive length,
                                          tls_cert address_to cert)
{
        byte_reader reader = byte_reader_open(value, length);
        byte_reader features = tls_der_value(&reader, 0x30);

        if (!byte_reader_end(&reader))
                return TLS_FAIL;
        while (byte_reader_left(&features))
        {
                byte_reader number = tls_der_value(&features, 0x02);
                positive feature = 0;

                if (!tls_der_integer(number) || byte_reader_left(&number) > 3 ||
                    (tls_der_peek(number) & 0x80))
                        return TLS_FAIL;
                while (byte_reader_left(&number))
                        feature = feature << 8 | byte_reader_u8(&number);
                if (feature > 0xffff)
                        return TLS_FAIL;
                cert->must_staple |= feature == 5;
        }
        return byte_reader_ok(&features) ? TLS_OK : TLS_FAIL;
}

static COLD bipolar tls_parse_extensions(p8 address_to der, positive tbs_stop,
                                    positive at, p8 version,
                                    tls_cert address_to cert,
                                    string_address host)
{
        enum { TLS_CERT_EXTENSIONS_MAX = 64 };
        p8 address_to seen_oid[TLS_CERT_EXTENSIONS_MAX];
        positive seen_length[TLS_CERT_EXTENSIONS_MAX];
        positive seen_count = 0;
        bool issuer_unique = false;
        bool subject_unique = false;
        positive extensions_stop = 0;
        positive sequence_stop = 0;

        while (at < tbs_stop && (der[at] == 0x81 || der[at] == 0x82))
        {
                bool address_to seen = der[at] == 0x81 ? address_of issuer_unique
                                                       : address_of subject_unique;

                /* issuerUniqueID/subjectUniqueID were added in v2.  Treating
                   them as ignorable on a v1 certificate accepts a structure
                   X.509 never defined. */
                if (!version || address_to seen ||
                    tls_asn1_skip(der, tbs_stop, address_of at))
                        return TLS_FAIL;
                address_to seen = true;
        }
        if (at == tbs_stop)
                return TLS_OK;

        /* Extensions exist only in v3.  Without this gate a v1/v2 TBS could
           smuggle v3 constraints into a shape whose version says they do not
           exist, leaving different validators to disagree about the same
           signed bytes. */
        cert->extensions = der + at;
        if (version != 2 ||
            tls_asn1_enter(der, tbs_stop, 0xa3, address_of at,
                           address_of extensions_stop) ||
            extensions_stop != tbs_stop ||
            tls_asn1_enter(der, extensions_stop, 0x30, address_of at,
                           address_of sequence_stop) ||
            sequence_stop != extensions_stop || at == sequence_stop)
                return TLS_FAIL;

        while (at < sequence_stop)
        {
                positive extension_stop = 0;
                positive extension_at = at;
                positive oid_at;
                positive oid_stop = 0;
                positive value_stop = 0;
                bool critical = false;

                if (tls_asn1_enter(der, sequence_stop, 0x30, address_of at,
                                   address_of extension_stop))
                        return TLS_FAIL;
                oid_at = at;
                if (tls_asn1_enter_oid(der, extension_stop, address_of oid_at,
                                       address_of oid_stop))
                        return TLS_FAIL;
                /* RFC 5280 permits one instance of an extension in a
                   certificate. Track unknown OIDs too: accepting two merely
                   because this client does not currently interpret them
                   creates a parser differential the day another component
                   does. The ceiling also bounds comparison work for a signed
                   but hostile certificate. */
                if (seen_count == TLS_CERT_EXTENSIONS_MAX)
                        return TLS_FAIL;
                for (positive seen = 0; seen < seen_count; seen++)
                        if (seen_length[seen] == oid_stop - oid_at &&
                            !memory_compare(seen_oid[seen], der + oid_at,
                                            seen_length[seen]))
                                return TLS_FAIL;
                seen_oid[seen_count] = der + oid_at;
                seen_length[seen_count++] = oid_stop - oid_at;
                at = oid_stop;
                if (tls_asn1_true(der, extension_stop, address_of at,
                                  address_of critical) ||
                    tls_asn1_enter(der, extension_stop, 0x04, address_of at,
                                   address_of value_stop) ||
                    value_stop != extension_stop)
                        return TLS_FAIL;

                if (tls_oid_is(der + oid_at, oid_stop - oid_at,
                               tls_oid_basic_constraints, 3))
                {
                        if (cert->basic_constraints ||
                            tls_parse_basic_constraints(der + at, value_stop - at,
                                                        cert))
                                return TLS_FAIL;
                        cert->basic_constraints = true;
                }
                else if (tls_oid_is(der + oid_at, oid_stop - oid_at,
                                    tls_oid_key_usage, 3))
                {
                        if (cert->key_usage ||
                            tls_parse_key_usage(der + at, value_stop - at, cert))
                                return TLS_FAIL;
                        cert->key_usage = true;
                }
                else if (tls_oid_is(der + oid_at, oid_stop - oid_at,
                                    tls_oid_extended_key_usage, 3))
                {
                        if (cert->extended_key_usage ||
                            tls_parse_extended_key_usage(der + at, value_stop - at,
                                                         cert))
                                return TLS_FAIL;
                        cert->extended_key_usage = true;
                }
                else if (tls_oid_is(der + oid_at, oid_stop - oid_at, tls_oid_san, 3))
                {
                        bool matched = false;

                        if (cert->san ||
                            !tls_parse_san(der + at, value_stop - at, host,
                                           address_of matched))
                                return TLS_FAIL;
                        cert->san = der + at;
                        cert->san_length = value_stop - at;
                        cert->san_match = matched;
                }
                else if (tls_oid_is(der + oid_at, oid_stop - oid_at,
                                    tls_oid_name_constraints, 3))
                {
                        /* Namespace limits bind whether or not the issuer
                           marks them critical, and a form this client
                           cannot evaluate fails closed where it is used
                           (tls_name_allowed), so a value that does not
                           parse is refused here, before any path uses it. */
                        if (!tls_name_allowed(der + at, value_stop - at, 0,
                                              null, 0))
                                return TLS_FAIL;
                        cert->name_constraints = der + at;
                        cert->name_constraints_length = value_stop - at;
                }
                else if (!critical &&
                         tls_oid_is(der + oid_at, oid_stop - oid_at,
                                    tls_oid_authority_info, 8))
                        tls_parse_ca_issuers(der + at, value_stop - at, cert);
                else if (tls_oid_is(der + oid_at, oid_stop - oid_at,
                                    tls_oid_crl_points, 3))
                {
                        if (tls_parse_crl_points(der + at, value_stop - at, cert))
                                return TLS_FAIL;
                }
                else if (tls_oid_is(der + oid_at, oid_stop - oid_at,
                                    tls_oid_authority_key, 3))
                {
                        if (tls_parse_authority_key(der + at, value_stop - at,
                                                    address_of cert->authority_key,
                                                    address_of cert->authority_key_length))
                                return TLS_FAIL;
                }
                else if (tls_oid_is(der + oid_at, oid_stop - oid_at,
                                    tls_oid_tls_feature, 8))
                {
                        if (tls_parse_tls_feature(der + at, value_stop - at, cert))
                                return TLS_FAIL;
                }
                else if (!critical &&
                         tls_oid_is(der + oid_at, oid_stop - oid_at, tls_oid_scts, 10))
                {
                        /* An OCTET STRING of the TLS-encoded list. One that is
                           not is no list: it counts for nothing where CT is
                           asked and is ignored where it is not, as any other
                           client ignores it. */
                        positive list_at = at;
                        positive list_stop = 0;

                        if (!tls_asn1_enter(der, value_stop, 0x04, address_of list_at,
                                            address_of list_stop) &&
                            list_stop == value_stop)
                        {
                                cert->scts = der + list_at;
                                cert->scts_length = list_stop - list_at;
                                cert->sct_extension = der + extension_at;
                                cert->sct_extension_length = extension_stop - extension_at;
                        }
                }
                else if (critical)
                        cert->unsupported_critical = true;

                at = extension_stop;
        }
        return TLS_OK;
}

static COLD bipolar tls_parse_cert(p8 address_to der, positive length,
                              tls_cert address_to cert, string_address host)
{
        positive at = 0;
        positive stop = 0;
        positive tbs_stop = 0;
        positive spki_stop = 0;
        positive alg_stop = 0;
        positive oid_stop = 0;
        positive param_at;
        positive bit_stop = 0;
        p8 version = 0;

        memory_fill(cert, 0, sizeof(*cert));

        if (tls_asn1_enter(der, length, 0x30, address_of at, address_of stop) ||
            stop != length)
                return TLS_FAIL;
        if (tls_asn1_take(der, length, 0x30, address_of at, address_of cert->tbs,
                          address_of cert->tbs_length))
                return TLS_FAIL;
        if (!tls_signature_algorithm(der, stop, address_of at,
                                     address_of cert->sig_oid,
                                     address_of cert->sig_oid_length))
                return TLS_FAIL;
        if (tls_asn1_enter(der, stop, 0x03, address_of at, address_of bit_stop) ||
            bit_stop != stop)
                return TLS_FAIL;
        if (at >= bit_stop || der[at] != 0)
                return TLS_FAIL;
        at++;
        cert->sig = der + at;
        cert->sig_length = bit_stop - at;

        at = (positive)(cert->tbs - der);
        if (tls_asn1_enter(der, length, 0x30, address_of at, address_of tbs_stop))
                return TLS_FAIL;
        if (tls_parse_version(der, tbs_stop, address_of at, address_of version))
                return TLS_FAIL;
        {
                positive serial_stop = 0;
                positive value_at;
                positive value_length;

                if (tls_asn1_enter(der, tbs_stop, 0x02, address_of at,
                                   address_of serial_stop) ||
                    !tls_positive_integer(der, at, serial_stop,
                                          address_of value_at,
                                          address_of value_length))
                        return TLS_FAIL;
                cert->serial = der + at;
                cert->serial_length = serial_stop - at;
                at = serial_stop;
        }
        {
                p8 address_to tbs_oid;
                positive tbs_oid_length;

                if (!tls_signature_algorithm(der, tbs_stop, address_of at,
                                             address_of tbs_oid,
                                             address_of tbs_oid_length) ||
                    tbs_oid_length != cert->sig_oid_length ||
                    memory_compare(tbs_oid, cert->sig_oid, tbs_oid_length))
                        return TLS_FAIL;
        }
        if (tls_asn1_take(der, tbs_stop, 0x30, address_of at,
                          address_of cert->issuer, address_of cert->issuer_length) ||
            tls_parse_validity(der, tbs_stop, address_of at, cert) ||
            tls_asn1_take(der, tbs_stop, 0x30, address_of at,
                          address_of cert->subject, address_of cert->subject_length))
                return TLS_FAIL;

        cert->spki = der + at;
        if (tls_asn1_enter(der, tbs_stop, 0x30, address_of at, address_of spki_stop))
                return TLS_FAIL;
        cert->spki_length = spki_stop - (positive)(cert->spki - der);
        if (tls_asn1_enter(der, spki_stop, 0x30, address_of at, address_of alg_stop))
                return TLS_FAIL;
        param_at = at;
        if (tls_asn1_enter_oid(der, alg_stop, address_of param_at,
                               address_of oid_stop))
                return TLS_FAIL;

        /* Whichever algorithm the OID turns out to name, the key itself is
           the SubjectPublicKey BIT STRING filling the rest of the SPKI, and
           its first content octet counts unused bits -- zero for every key
           shape this verifier accepts. Reading it here rather than twice
           below is the same three refusals in the same order. */
        at = alg_stop;
        if (tls_asn1_enter(der, spki_stop, 0x03, address_of at,
                           address_of bit_stop) ||
            bit_stop != spki_stop)
                return TLS_FAIL;
        if (at >= bit_stop || der[at++] != 0)
                return TLS_FAIL;
        cert->key_bits = der + at;
        cert->key_bits_length = bit_stop - at;

        if (tls_oid_is(der + param_at, oid_stop - param_at, tls_oid_ec, 7))
        {
                positive curve_stop = 0;
                positive coord;

                if (tls_asn1_enter_oid(der, alg_stop, address_of oid_stop,
                                       address_of curve_stop) ||
                    curve_stop != alg_stop)
                        return TLS_FAIL;
                if (tls_oid_is(der + oid_stop, curve_stop - oid_stop, tls_oid_p256, 8))
                        cert->curve = 1;
                else if (tls_oid_is(der + oid_stop, curve_stop - oid_stop,
                                    tls_oid_p384, 5))
                        cert->curve = 2;
                else
                        return TLS_FAIL;

                if (at >= bit_stop || der[at++] != 0x04)
                        return TLS_FAIL;
                coord = cert->curve == 1 ? 32 : 48;
                if (at + coord * 2 != bit_stop)
                        return TLS_FAIL;
                memory_copy(cert->qx + (48 - coord), der + at, coord);
                memory_copy(cert->qy + (48 - coord), der + at + coord, coord);
        }
        else if (tls_oid_is(der + param_at, oid_stop - param_at, tls_oid_rsa, 9))
        {
                positive n_stop = 0;
                positive e_stop = 0;
                positive rsa_stop = 0;
                positive value_at;
                positive value_length;

                if (!tls_null_or_absent(der, oid_stop, alg_stop))
                        return TLS_FAIL;

                cert->curve = 3;
                if (tls_asn1_enter(der, bit_stop, 0x30, address_of at,
                                   address_of rsa_stop) ||
                    rsa_stop != bit_stop ||
                    tls_asn1_enter(der, rsa_stop, 0x02, address_of at,
                                   address_of n_stop) ||
                    !tls_positive_integer(der, at, n_stop,
                                          address_of value_at,
                                          address_of value_length))
                        return TLS_FAIL;
                cert->modulus_length = value_length;
                if (cert->modulus_length < 256 ||
                    cert->modulus_length > sizeof(cert->modulus) ||
                    (cert->modulus_length == 256 &&
                     !(der[value_at] & 0x80)) ||
                    !(der[value_at + value_length - 1] & 1))
                        return TLS_FAIL;
                memory_copy(cert->modulus, der + value_at,
                            cert->modulus_length);
                at = n_stop;
                if (tls_asn1_enter(der, rsa_stop, 0x02, address_of at,
                                   address_of e_stop))
                        return TLS_FAIL;
                if (!tls_positive_integer(der, at, e_stop,
                                          address_of value_at,
                                          address_of value_length) ||
                    value_length > sizeof(cert->exponent))
                        return TLS_FAIL;
                cert->exponent = 0;
                while (value_at < e_stop)
                        cert->exponent = (cert->exponent << 8) |
                                         der[value_at++];
                at = e_stop;
                if (at != rsa_stop || cert->exponent < 3 ||
                    !(cert->exponent & 1) ||
                    cert->exponent >> CRYPTO_RSA_EXPONENT_BITS)
                        return TLS_FAIL;
        }
        else
                return TLS_FAIL;

        return tls_parse_extensions(der, tbs_stop, spki_stop, version,
                                    cert, host);
}

/*
        Revocation answers, read before anything vouches for them.

        An OCSP response (RFC 6960) comes stapled in the handshake and a CRL
        (RFC 5280 5) over plain HTTP from the address a certificate names, so
        both are any peer's bytes until a signature over them verifies, and
        their parsers run first: strict DER, every field in its place, every
        extension known or not critical and none twice, every list under a
        ceiling. What they keep are pointers into the bytes they were given.

          TLS_OCSP_MAX         bytes of a stapled response; one has to fit a
                               handshake message anyway
          TLS_OCSP_RESPONSES   SingleResponses in one response
          TLS_OCSP_CERTS       certificates one carries for a delegated
                               responder
          TLS_LIST_EXTENSIONS  extensions in a CRL, an entry, a response or a
                               single response
          TLS_CRL_ENTRIES      revoked certificates read in one CRL: 1.7
                               times the 311,758 of the largest public one
                               seen (DigiCert Global G2 TLS RSA SHA256 2020
                               CA1, 11.7 MB, 2026-10-09), and fewer than the
                               smallest entries (20 bytes) can fill below
                               the three length octets tls_asn1_length reads,
                               so the count binds before the length does
*/
#define TLS_OCSP_MAX 16384
#define TLS_OCSP_RESPONSES 16
#define TLS_OCSP_CERTS 4
#define TLS_LIST_EXTENSIONS 16
#define TLS_CRL_ENTRIES ((positive)1 << 19)
#define TLS_OCSP_GOOD 0
#define TLS_OCSP_REVOKED 1
#define TLS_OCSP_UNKNOWN 2

typedef struct
{
        const p8 address_to oid[TLS_LIST_EXTENSIONS];
        positive length[TLS_LIST_EXTENSIONS];
        positive count;
} tls_seen;

/* Whether a reader holds exactly these bytes: an OID's contents, say. */
static COLD bool tls_der_is(byte_reader contents, const p8 address_to want,
                            positive length)
{
        return byte_reader_ok(&contents) && byte_reader_left(&contents) == length &&
               !memory_compare(byte_reader_here(&contents), want, length);
}

/* A Time (RFC 5280 4.1.2.5): UTCTime or GeneralizedTime by the certificate
   rule, or -- generalized -- GeneralizedTime in every year, as OCSP and a
   CRL entry's invalidityDate write it. Fails the reader on any other tag. */
static COLD bool tls_der_time(byte_reader address_to reader, bool generalized,
                              p64 address_to value)
{
        p8 tag = tls_der_peek(address_to reader);
        byte_reader text;

        if (tag != 0x18 && (generalized || tag != 0x17))
        {
                (void)byte_reader_take(reader, positive_max);
                return false;
        }
        text = tls_der_value(reader, tag);
        return byte_reader_ok(&text) &&
               (generalized
                    ? tls_date_general((p8 address_to)byte_reader_here(&text),
                                       byte_reader_left(&text), value)
                    : tls_date_value(tag, (p8 address_to)byte_reader_here(&text),
                                     byte_reader_left(&text), value));
}

/* The next Extension of an Extensions list's contents: its OID, its
   critical flag and its value's contents, the list read past it. False at
   the end of the list, or with the list failed on anything malformed, on a
   second of one OID, and past TLS_LIST_EXTENSIONS of them. */
static COLD bool tls_der_extension(byte_reader address_to list,
                                   tls_seen address_to seen,
                                   byte_reader address_to oid,
                                   bool address_to critical,
                                   byte_reader address_to value)
{
        byte_reader extension;

        if (!byte_reader_left(list))
                return false;
        extension = tls_der_value(list, 0x30);
        address_to oid = tls_der_value(&extension, 0x06);
        address_to critical = tls_der_flag(&extension, 0x01);
        address_to value = tls_der_value(&extension, 0x04);
        if (!byte_reader_end(&extension) ||
            !tls_oid_content_der(byte_reader_here(oid), byte_reader_left(oid)) ||
            seen->count == TLS_LIST_EXTENSIONS)
                goto refused;
        for (positive k = 0; k < seen->count; k++)
                if (tls_der_is(address_to oid, seen->oid[k], seen->length[k]))
                        goto refused;
        seen->oid[seen->count] = byte_reader_here(oid);
        seen->length[seen->count++] = byte_reader_left(oid);
        return true;

refused:
        (void)byte_reader_take(list, positive_max);
        return false;
}

/* A CRLReason (RFC 5280 5.3.1): 0 to 10 but the unused 7, and in a whole
   CRL not removeFromCRL (8), which only a delta CRL may say. */
static COLD bool tls_crl_reason(bipolar code, bool whole)
{
        return code >= 0 && code <= 10 && code != 7 && (!whole || code != 8);
}

typedef struct
{
        /* The hash the CertID names its issuer by: 1 SHA-1, 2 SHA-256. */
        p8 hash;
        p8 status;
        positive hash_length;
        const p8 address_to name_hash;
        const p8 address_to key_hash;
        const p8 address_to serial;
        positive serial_length;
        p64 this_update;
        /* 0 when it names none. */
        p64 next_update;
} tls_ocsp_single;

typedef struct
{
        /* ResponseData whole, the signature over it and its algorithm. */
        p8 address_to tbs;
        positive tbs_length;
        p8 address_to sig_oid;
        positive sig_oid_length;
        const p8 address_to sig;
        positive sig_length;
        /* byName's Name, header and all, or byKey's SHA-1 key hash. */
        const p8 address_to responder;
        positive responder_length;
        bool by_key;
        p64 produced;
        /* The SingleResponses' and the certificates' SEQUENCE contents. */
        const p8 address_to responses;
        positive responses_length;
        const p8 address_to certs;
        positive certs_length;
} tls_ocsp;

/* One SingleResponse (RFC 6960 4.2.1), whole: the CertID -- SHA-1 or
   SHA-256 with NULL or absent parameters, both hashes that hash's length,
   a DER serial -- then good [0], revoked [1] (a time and an optional
   reason) or unknown [2], thisUpdate, an optional nextUpdate [0] and
   singleExtensions [1], none critical. */
static COLD bipolar tls_parse_ocsp_single(const p8 address_to bytes,
                                          positive length,
                                          tls_ocsp_single address_to single)
{
        byte_reader reader = byte_reader_open(bytes, length);
        byte_reader response = tls_der_value(&reader, 0x30);
        byte_reader id = tls_der_value(&response, 0x30);
        byte_reader algorithm = tls_der_value(&id, 0x30);
        byte_reader oid = tls_der_value(&algorithm, 0x06);
        byte_reader name_hash;
        byte_reader key_hash;
        byte_reader serial;
        byte_reader status;
        p8 tag;

        memory_fill(single, 0, sizeof(address_to single));
        single->hash = tls_der_is(oid, tls_oid_sha1, 5)     ? 1
                       : tls_der_is(oid, tls_oid_sha256, 9) ? 2
                                                            : 0;
        single->hash_length = single->hash == 1 ? 20 : 32;
        if (byte_reader_left(&algorithm))
        {
                byte_reader nothing = tls_der_value(&algorithm, 0x05);

                if (!byte_reader_end(&nothing))
                        return TLS_FAIL;
        }
        name_hash = tls_der_value(&id, 0x04);
        key_hash = tls_der_value(&id, 0x04);
        serial = tls_der_value(&id, 0x02);
        if (!single->hash || !byte_reader_end(&algorithm) || !byte_reader_end(&id) ||
            byte_reader_left(&name_hash) != single->hash_length ||
            byte_reader_left(&key_hash) != single->hash_length ||
            !tls_der_integer(serial))
                return TLS_FAIL;
        single->name_hash = byte_reader_here(&name_hash);
        single->key_hash = byte_reader_here(&key_hash);
        single->serial = byte_reader_here(&serial);
        single->serial_length = byte_reader_left(&serial);

        tag = tls_der_peek(response);
        status = tls_der_value(&response, tag);
        if (tag == 0x80 || tag == 0x82)
        {
                if (!byte_reader_end(&status))
                        return TLS_FAIL;
                single->status = tag == 0x80 ? TLS_OCSP_GOOD : TLS_OCSP_UNKNOWN;
        }
        else if (tag == 0xa1)
        {
                p64 revoked = 0;

                if (!tls_der_time(&status, true, address_of revoked))
                        return TLS_FAIL;
                if (tls_der_peek(status) == 0xa0)
                {
                        byte_reader reason = tls_der_value(&status, 0xa0);

                        if (!tls_crl_reason(tls_der_small(&reason, 0x0a), false) ||
                            !byte_reader_end(&reason))
                                return TLS_FAIL;
                }
                if (!byte_reader_end(&status))
                        return TLS_FAIL;
                single->status = TLS_OCSP_REVOKED;
        }
        else
                return TLS_FAIL;

        if (!tls_der_time(&response, true, address_of single->this_update))
                return TLS_FAIL;
        if (tls_der_peek(response) == 0xa0)
        {
                byte_reader next = tls_der_value(&response, 0xa0);

                if (!tls_der_time(&next, true, address_of single->next_update) ||
                    !byte_reader_end(&next) ||
                    single->next_update < single->this_update)
                        return TLS_FAIL;
        }
        if (tls_der_peek(response) == 0xa1)
        {
                byte_reader wrapper = tls_der_value(&response, 0xa1);
                byte_reader list = tls_der_value(&wrapper, 0x30);
                byte_reader value;
                bool critical = false;
                tls_seen seen;

                memory_fill(address_of seen, 0, sizeof seen);
                if (!byte_reader_end(&wrapper) || !byte_reader_left(&list))
                        return TLS_FAIL;
                while (tls_der_extension(&list, address_of seen, address_of oid,
                                         address_of critical, address_of value))
                        if (critical)
                                return TLS_FAIL;
                if (!byte_reader_end(&list))
                        return TLS_FAIL;
        }
        return byte_reader_end(&response) && byte_reader_end(&reader) ? TLS_OK
                                                                      : TLS_FAIL;
}

/* An OCSPResponse (RFC 6960 4.2.1) of at most TLS_OCSP_MAX bytes that
   answers: status successful (0, the only one with responseBytes),
   id-pkix-ocsp-basic, and a BasicOCSPResponse of ResponseData (v1, which
   DER leaves out; a responder byName [1] or byKey [2] of 20 bytes;
   producedAt; one to TLS_OCSP_RESPONSES SingleResponses, each whole;
   responseExtensions [1], none critical), a signature algorithm
   tls_signature_algorithm knows, the signature, and up to TLS_OCSP_CERTS
   certificates [0]. Any other status is TLS_FAIL: tryLater and the rest say
   nothing about the certificate. */
static COLD bipolar tls_parse_ocsp(p8 address_to bytes, positive length,
                                   tls_ocsp address_to ocsp)
{
        byte_reader reader = byte_reader_open(bytes,
                                              length > TLS_OCSP_MAX ? 0 : length);
        byte_reader response = tls_der_value(&reader, 0x30);
        bipolar status = tls_der_small(&response, 0x0a);
        byte_reader wrapper = tls_der_value(&response, 0xa0);
        byte_reader kind = tls_der_value(&wrapper, 0x30);
        byte_reader oid = tls_der_value(&kind, 0x06);
        byte_reader octets = tls_der_value(&kind, 0x04);
        byte_reader basic = tls_der_value(&octets, 0x30);
        byte_reader outer;
        byte_reader data;
        byte_reader bits;
        byte_reader list;
        p8 tag;
        positive at = 0;
        positive count = 0;

        memory_fill(ocsp, 0, sizeof(address_to ocsp));
        if (status || !byte_reader_end(&reader) || !byte_reader_end(&response) ||
            !byte_reader_end(&wrapper) || !tls_der_is(oid, tls_oid_ocsp_basic, 9) ||
            !byte_reader_end(&kind) || !byte_reader_end(&octets))
                return TLS_FAIL;
        ocsp->tbs = tls_der_whole(&basic, 0x30, address_of ocsp->tbs_length);
        if (!ocsp->tbs ||
            !tls_signature_algorithm((p8 address_to)byte_reader_here(&basic),
                                     byte_reader_left(&basic), address_of at,
                                     address_of ocsp->sig_oid,
                                     address_of ocsp->sig_oid_length) ||
            !byte_reader_skip(&basic, at))
                return TLS_FAIL;
        bits = tls_der_value(&basic, 0x03);
        if (byte_reader_u8(&bits) || !byte_reader_left(&bits))
                return TLS_FAIL;
        ocsp->sig = byte_reader_here(&bits);
        ocsp->sig_length = byte_reader_left(&bits);
        if (tls_der_peek(basic) == 0xa0)
        {
                byte_reader holder = tls_der_value(&basic, 0xa0);

                list = tls_der_value(&holder, 0x30);
                if (!byte_reader_end(&holder))
                        return TLS_FAIL;
                ocsp->certs = byte_reader_here(&list);
                ocsp->certs_length = byte_reader_left(&list);
                for (count = 0; byte_reader_left(&list); count++)
                {
                        byte_reader certificate = tls_der_value(&list, 0x30);

                        if (count == TLS_OCSP_CERTS || !byte_reader_ok(&certificate))
                                return TLS_FAIL;
                }
                if (!byte_reader_ok(&list))
                        return TLS_FAIL;
        }
        if (!byte_reader_end(&basic))
                return TLS_FAIL;

        outer = byte_reader_open(ocsp->tbs, ocsp->tbs_length);
        data = tls_der_value(&outer, 0x30);
        tag = tls_der_peek(data);
        if (tag == 0xa1)
        {
                byte_reader name = tls_der_value(&data, 0xa1);

                ocsp->responder = tls_der_whole(&name, 0x30,
                                                address_of ocsp->responder_length);
                if (!ocsp->responder || !byte_reader_end(&name))
                        return TLS_FAIL;
        }
        else if (tag == 0xa2)
        {
                byte_reader name = tls_der_value(&data, 0xa2);
                byte_reader hash = tls_der_value(&name, 0x04);

                if (byte_reader_left(&hash) != 20 || !byte_reader_end(&name))
                        return TLS_FAIL;
                ocsp->responder = byte_reader_here(&hash);
                ocsp->responder_length = 20;
                ocsp->by_key = true;
        }
        else
                return TLS_FAIL;
        if (!tls_der_time(&data, true, address_of ocsp->produced))
                return TLS_FAIL;
        list = tls_der_value(&data, 0x30);
        ocsp->responses = byte_reader_here(&list);
        ocsp->responses_length = byte_reader_left(&list);
        if (!byte_reader_left(&list))
                return TLS_FAIL;
        for (count = 0; byte_reader_left(&list); count++)
        {
                positive size = 0;
                const p8 address_to whole = tls_der_whole(&list, 0x30, address_of size);
                tls_ocsp_single single;

                if (count == TLS_OCSP_RESPONSES || !whole ||
                    tls_parse_ocsp_single(whole, size, address_of single))
                        return TLS_FAIL;
        }
        if (tls_der_peek(data) == 0xa1)
        {
                byte_reader holder = tls_der_value(&data, 0xa1);
                byte_reader extensions = tls_der_value(&holder, 0x30);
                byte_reader value;
                bool critical = false;
                tls_seen seen;

                memory_fill(address_of seen, 0, sizeof seen);
                if (!byte_reader_end(&holder) || !byte_reader_left(&extensions))
                        return TLS_FAIL;
                while (tls_der_extension(&extensions, address_of seen, address_of oid,
                                         address_of critical, address_of value))
                        if (critical)
                                return TLS_FAIL;
                if (!byte_reader_end(&extensions))
                        return TLS_FAIL;
        }
        return byte_reader_end(&data) ? TLS_OK : TLS_FAIL;
}

typedef struct
{
        /* The issuingDistributionPoint (RFC 5280 5.2.5): whether there is
           one, its fullName's GeneralNames contents (null when it names no
           point or a relative one), and what it says the CRL leaves out. */
        bool present;
        bool relative;
        bool user_only;
        bool ca_only;
        bool some_reasons;
        bool indirect;
        bool attribute_only;
        const p8 address_to names;
        positive names_length;
} tls_crl_scope;

typedef struct
{
        p8 address_to tbs;
        positive tbs_length;
        p8 address_to sig_oid;
        positive sig_oid_length;
        const p8 address_to sig;
        positive sig_length;
        p8 address_to issuer;
        positive issuer_length;
        p8 address_to authority_key;
        positive authority_key_length;
        tls_crl_scope scope;
        p64 this_update;
        /* 0 when it names none. */
        p64 next_update;
        positive entries;
        /* The serial asked about is on it. */
        bool listed;
} tls_crl;

/* An issuingDistributionPoint's value: not empty, its fields in order, the
   booleans only ever TRUE (DER leaves FALSE out), reasons a named bit list,
   and at most one of user, CA and attribute certificates only. */
static COLD bipolar tls_parse_crl_scope(const p8 address_to value,
                                        positive length,
                                        tls_crl_scope address_to scope)
{
        byte_reader reader = byte_reader_open(value, length);
        byte_reader point = tls_der_value(&reader, 0x30);

        memory_fill(scope, 0, sizeof(address_to scope));
        if (!byte_reader_end(&reader) || !byte_reader_left(&point))
                return TLS_FAIL;
        scope->present = true;
        if (tls_der_peek(point) == 0xa0)
        {
                byte_reader name = tls_der_value(&point, 0xa0);
                p8 form = tls_der_peek(name);
                byte_reader inside = tls_der_value(&name, form);

                if ((form != 0xa0 && form != 0xa1) || !byte_reader_end(&name) ||
                    (form == 0xa0 ? !tls_general_names_valid(inside)
                                  : !byte_reader_left(&inside)))
                        return TLS_FAIL;
                scope->relative = form == 0xa1;
                if (form == 0xa0)
                {
                        scope->names = byte_reader_here(&inside);
                        scope->names_length = byte_reader_left(&inside);
                }
        }
        scope->user_only = tls_der_flag(&point, 0x81);
        scope->ca_only = tls_der_flag(&point, 0x82);
        if (tls_der_peek(point) == 0x83)
        {
                if (!tls_der_named_bits(tls_der_value(&point, 0x83)))
                        return TLS_FAIL;
                scope->some_reasons = true;
        }
        scope->indirect = tls_der_flag(&point, 0x84);
        scope->attribute_only = tls_der_flag(&point, 0x85);
        return byte_reader_end(&point) &&
                       scope->user_only + scope->ca_only + scope->attribute_only <= 1
                   ? TLS_OK
                   : TLS_FAIL;
}

/* A CRL entry's extensions: reasonCode (not removeFromCRL), invalidityDate,
   and nothing else critical -- certificateIssuer, which only an indirect
   CRL carries, among them. */
static COLD bool tls_crl_entry_extensions(byte_reader list)
{
        byte_reader oid;
        byte_reader value;
        bool critical = false;
        tls_seen seen;

        memory_fill(address_of seen, 0, sizeof seen);
        if (!byte_reader_left(&list))
                return false;
        while (tls_der_extension(&list, address_of seen, address_of oid,
                                 address_of critical, address_of value))
        {
                p64 when = 0;

                if (tls_der_is(oid, tls_oid_crl_reason, 3))
                {
                        if (!tls_crl_reason(tls_der_small(&value, 0x0a), true) ||
                            !byte_reader_end(&value))
                                return false;
                }
                else if (tls_der_is(oid, tls_oid_invalidity, 3))
                {
                        if (!tls_der_time(&value, true, address_of when) ||
                            !byte_reader_end(&value))
                                return false;
                }
                else if (critical)
                        return false;
        }
        return byte_reader_end(&list);
}

/* A CertificateList (RFC 5280 5.1), and whether serial is on it: v2, or v1
   with no extension anywhere; the inner signature algorithm the outer one;
   thisUpdate and an optional nextUpdate by the certificate time rule;
   revoked certificates each a DER serial, a time and the extensions
   tls_crl_entry_extensions allows, at most TLS_CRL_ENTRIES of them; and
   crlExtensions, not empty, of which authorityKeyIdentifier, cRLNumber (a
   non-negative INTEGER of at most 20 octets past its sign) and
   issuingDistributionPoint are read, deltaCRLIndicator is refused (a delta
   is not a whole CRL), and anything else must not be critical. */
static COLD bipolar tls_parse_crl(p8 address_to bytes, positive length,
                                  const p8 address_to serial,
                                  positive serial_length, tls_crl address_to crl)
{
        byte_reader reader = byte_reader_open(bytes, length);
        byte_reader list = tls_der_value(&reader, 0x30);
        byte_reader outer;
        byte_reader tbs;
        byte_reader bits;
        p8 address_to tbs_oid = null;
        positive tbs_oid_length = 0;
        positive at = 0;
        bool v2 = false;

        memory_fill(crl, 0, sizeof(address_to crl));
        if (!byte_reader_end(&reader))
                return TLS_FAIL;
        crl->tbs = tls_der_whole(&list, 0x30, address_of crl->tbs_length);
        if (!crl->tbs ||
            !tls_signature_algorithm((p8 address_to)byte_reader_here(&list),
                                     byte_reader_left(&list), address_of at,
                                     address_of crl->sig_oid,
                                     address_of crl->sig_oid_length) ||
            !byte_reader_skip(&list, at))
                return TLS_FAIL;
        bits = tls_der_value(&list, 0x03);
        if (byte_reader_u8(&bits) || !byte_reader_left(&bits) ||
            !byte_reader_end(&list))
                return TLS_FAIL;
        crl->sig = byte_reader_here(&bits);
        crl->sig_length = byte_reader_left(&bits);

        outer = byte_reader_open(crl->tbs, crl->tbs_length);
        tbs = tls_der_value(&outer, 0x30);
        if (tls_der_peek(tbs) == 0x02)
        {
                if (tls_der_small(&tbs, 0x02) != 1)
                        return TLS_FAIL;
                v2 = true;
        }
        at = 0;
        if (!tls_signature_algorithm((p8 address_to)byte_reader_here(&tbs),
                                     byte_reader_left(&tbs), address_of at,
                                     address_of tbs_oid, address_of tbs_oid_length) ||
            !byte_reader_skip(&tbs, at) || tbs_oid_length != crl->sig_oid_length ||
            memory_compare(tbs_oid, crl->sig_oid, tbs_oid_length))
                return TLS_FAIL;
        crl->issuer = tls_der_whole(&tbs, 0x30, address_of crl->issuer_length);
        if (!crl->issuer || !tls_der_time(&tbs, false, address_of crl->this_update))
                return TLS_FAIL;
        if ((tls_der_peek(tbs) == 0x17 || tls_der_peek(tbs) == 0x18) &&
            (!tls_der_time(&tbs, false, address_of crl->next_update) ||
             crl->next_update < crl->this_update))
                return TLS_FAIL;
        if (tls_der_peek(tbs) == 0x30)
        {
                byte_reader revoked = tls_der_value(&tbs, 0x30);

                while (byte_reader_left(&revoked))
                {
                        byte_reader entry = tls_der_value(&revoked, 0x30);
                        byte_reader number = tls_der_value(&entry, 0x02);
                        p64 when = 0;

                        if (crl->entries++ == TLS_CRL_ENTRIES ||
                            !tls_der_integer(number) ||
                            !tls_der_time(&entry, false, address_of when) ||
                            (byte_reader_left(&entry) &&
                             (!v2 || !tls_crl_entry_extensions(
                                         tls_der_value(&entry, 0x30)))) ||
                            !byte_reader_end(&entry))
                                return TLS_FAIL;
                        crl->listed |= tls_der_is(number, serial, serial_length);
                }
                if (!byte_reader_ok(&revoked))
                        return TLS_FAIL;
        }
        if (tls_der_peek(tbs) == 0xa0)
        {
                byte_reader holder = tls_der_value(&tbs, 0xa0);
                byte_reader extensions = tls_der_value(&holder, 0x30);
                byte_reader oid;
                byte_reader value;
                bool critical = false;
                tls_seen seen;

                memory_fill(address_of seen, 0, sizeof seen);
                if (!v2 || !byte_reader_end(&holder) || !byte_reader_left(&extensions))
                        return TLS_FAIL;
                while (tls_der_extension(&extensions, address_of seen, address_of oid,
                                         address_of critical, address_of value))
                {
                        if (tls_der_is(oid, tls_oid_authority_key, 3))
                        {
                                if (tls_parse_authority_key(
                                        (p8 address_to)byte_reader_here(&value),
                                        byte_reader_left(&value),
                                        address_of crl->authority_key,
                                        address_of crl->authority_key_length))
                                        return TLS_FAIL;
                        }
                        else if (tls_der_is(oid, tls_oid_crl_number, 3))
                        {
                                byte_reader number = tls_der_value(&value, 0x02);

                                if (!tls_der_integer(number) ||
                                    (tls_der_peek(number) & 0x80) ||
                                    byte_reader_left(&number) > 21 ||
                                    !byte_reader_end(&value))
                                        return TLS_FAIL;
                        }
                        else if (tls_der_is(oid, tls_oid_crl_scope, 3))
                        {
                                if (tls_parse_crl_scope(byte_reader_here(&value),
                                                        byte_reader_left(&value),
                                                        address_of crl->scope))
                                        return TLS_FAIL;
                        }
                        else if (critical || tls_der_is(oid, tls_oid_delta_crl, 3))
                                return TLS_FAIL;
                }
                if (!byte_reader_end(&extensions))
                        return TLS_FAIL;
        }
        return byte_reader_end(&tbs) && byte_reader_end(&outer) ? TLS_OK : TLS_FAIL;
}

/* Whether a CRL of this scope answers for cert, which named the distribution
   point it was fetched from (RFC 5280 6.3.3 (b)(2)): a CRL without one is
   whole; one with one covers no reasons partly, is no indirect CRL, holds
   the kind of certificate cert is (a CA's or an end entity's), and when it
   names a point shares a name with cert's. A relative name is not read, so
   it covers nothing. */
static COLD bool tls_crl_covers(const tls_crl_scope address_to scope,
                                const tls_cert address_to cert)
{
        if (!scope->present)
                return true;
        if (scope->relative || scope->some_reasons || scope->indirect ||
            scope->attribute_only || (scope->user_only && cert->ca) ||
            (scope->ca_only && !cert->ca))
                return false;
        return !scope->names ||
               (cert->crl_names &&
                tls_general_names_meet(
                    byte_reader_open(scope->names, scope->names_length),
                    byte_reader_open(cert->crl_names, cert->crl_names_length)));
}

/* Whether a revocation answer issued at this_update and good until
   next_update holds now, allowing TLS_REVOCATION_SKEW seconds of clock
   difference either way (OpenSSL's OCSP_check_validity allows the same five
   minutes): issued no later than now, not yet past its next, and naming a
   next at all -- one that names none (RFC 6960 4.2.2.1, RFC 5280 5.1.2.5)
   cannot be told stale, and both CA/Browser Forum profiles require it. */
#define TLS_REVOCATION_SKEW 300

static COLD bool tls_revocation_fresh(p64 this_update, p64 next_update, p64 now)
{
        p64 at = tls_date_seconds(now);

        return next_update && this_update <= next_update &&
               tls_date_seconds(this_update) <= at + TLS_REVOCATION_SKEW &&
               at <= tls_date_seconds(next_update) + TLS_REVOCATION_SKEW;
}

/* A DER header for contents of length bytes at out: its size, which
   tls_der_header_size says without writing. */
static COLD positive tls_der_header_size(positive length)
{
        return length < 0x80 ? 2 : length < 0x100 ? 3 : length < 0x10000 ? 4 : 5;
}

static COLD positive tls_der_header(p8 address_to out, p8 tag, positive length)
{
        positive size = tls_der_header_size(length);

        out[0] = tag;
        if (size == 2)
                out[1] = (p8)length;
        else
        {
                out[1] = (p8)(0x80 | (size - 2));
                for (positive k = 2; k < size; k++)
                        out[k] = (p8)(length >> (8 * (size - 1 - k)));
        }
        return size;
}

/* The SubjectPublicKey BIT STRING contents of a key, past the unused-bits
   octet: what an OCSP issuerKeyHash and a byKey responder hash. A served
   certificate's own bytes; for an anchor, which carries none, the same
   bytes rebuilt -- 04 X Y for a curve, the RSAPublicKey SEQUENCE for RSA --
   which DER makes the one spelling of that key. The length in out, or 0
   when room is short or the key is none of these. */
static COLD positive tls_key_bits(const tls_cert address_to cert,
                                  p8 address_to out, positive room)
{
        positive coord = cert->curve == 1 ? 32 : 48;
        positive skip = 0;
        positive magnitude;
        positive modulus;
        positive power = 0;
        positive exponent;
        positive inner;
        positive at;
        p8 bytes[8];

        if (cert->key_bits)
        {
                if (cert->key_bits_length > room)
                        return 0;
                memory_copy(out, cert->key_bits, cert->key_bits_length);
                return cert->key_bits_length;
        }
        if (cert->curve == 1 || cert->curve == 2)
        {
                if (room < 1 + 2 * coord)
                        return 0;
                out[0] = 4;
                memory_copy(out + 1, cert->qx + 48 - coord, coord);
                memory_copy(out + 1 + coord, cert->qy + 48 - coord, coord);
                return 1 + 2 * coord;
        }
        if (cert->curve != 3 || !cert->modulus_length || !cert->exponent ||
            cert->modulus_length > sizeof cert->modulus)
                return 0;
        while (skip + 1 < cert->modulus_length && !cert->modulus[skip])
                skip++;
        magnitude = cert->modulus_length - skip;
        modulus = magnitude + (cert->modulus[skip] >> 7);
        for (p64 rest = cert->exponent; rest; rest >>= 8)
                bytes[7 - power++] = (p8)rest;
        exponent = power + (bytes[8 - power] >> 7);
        inner = tls_der_header_size(modulus) + modulus +
                tls_der_header_size(exponent) + exponent;
        if (tls_der_header_size(inner) + inner > room)
                return 0;
        at = tls_der_header(out, 0x30, inner);
        at += tls_der_header(out + at, 0x02, modulus);
        if (modulus > magnitude)
                out[at++] = 0;
        memory_copy(out + at, cert->modulus + skip, magnitude);
        at += magnitude;
        at += tls_der_header(out + at, 0x02, exponent);
        if (exponent > power)
                out[at++] = 0;
        memory_copy(out + at, bytes + 8 - power, power);
        return at + power;
}

/* An anchor's key laid out the way tls_parse_cert lays out a served one. */
static COLD bool tls_anchor_key(const tls_anchor address_to anchor,
                           tls_cert address_to root)
{
        positive length = anchor->key_length;
        positive coord = length / 2;
        const p8 address_to key;

        memory_fill(root, 0, sizeof(*root));
        if (!length || length > sizeof root->modulus ||
            anchor->key_at + length > sizeof tls_anchor_keys)
                return false;
        key = tls_anchor_keys + anchor->key_at;
        root->curve = anchor->curve;
        if (anchor->curve == 3)
        {
                memory_copy(root->modulus, key, length);
                root->modulus_length = length;
                root->exponent = anchor->exponent;
                return true;
        }
        if ((anchor->curve != 1 || coord != 32) &&
            (anchor->curve != 2 || coord != 48))
                return false;
        memory_copy(root->qx + 48 - coord, key, coord);
        memory_copy(root->qy + 48 - coord, key + coord, coord);
        return true;
}

/* TLS_BENCH_ANCHOR names a file holding tls_bench_anchor_x and _y, the
   P-384 root that test/differential.py's https_bench and tls_chains harnesses
   generate for a loopback server, trusted served or not. Only they define it;
   build.sh never does, so a shipped binary trusts exactly the anchors in
   anchors.inc. */
#ifdef TLS_BENCH_ANCHOR
#include TLS_BENCH_ANCHOR
/* The bench root carries a distrust-after date only when the file says so,
   so a test can put a chain on either side of one. */
static COLD bool tls_bench_distrusts(p64 leaf_from)
{
#ifdef TLS_BENCH_DISTRUST
        return leaf_from > TLS_BENCH_DISTRUST;
#else
        (void)leaf_from;
        return false;
#endif
}
#endif

/* Whether this anchor's distrust-after date is earlier than the leaf's
   notBefore. */
static COLD bool tls_anchor_distrusts(const tls_anchor address_to anchor,
                                      p64 leaf_from)
{
        return anchor->distrust && leaf_from > anchor->distrust;
}

/* A served certificate carrying an anchor's key ends the chain, whoever
   signed it: the key hash finds candidates and the whole key decides. */
static COLD bool tls_spki_is_anchor(tls_cert address_to cert, p64 leaf_from)
{
        p8 key[96];
        p8 digest[32];
        positive coord = cert->curve == 1 ? 32 : 48;

        if (cert->curve == 3)
                crypto_sha256_of(cert->modulus, cert->modulus_length, digest);
        else if (cert->curve == 1 || cert->curve == 2)
        {
                memory_copy(key, cert->qx + 48 - coord, coord);
                memory_copy(key + coord, cert->qy + 48 - coord, coord);
                crypto_sha256_of(key, coord * 2, digest);
        }
        else
                return false;

#ifdef TLS_BENCH_ANCHOR
        if (cert->curve == 2 &&
            !memory_compare(cert->qx, tls_bench_anchor_x, 48) &&
            !memory_compare(cert->qy, tls_bench_anchor_y, 48))
                return !tls_bench_distrusts(leaf_from);
#endif

        for (positive i = 0; i < array_count(tls_anchors); i++)
        {
                tls_cert root;

                if (tls_anchors[i].curve != cert->curve ||
                    memory_compare(tls_anchors[i].key_hash, digest, 8) ||
                    tls_anchor_distrusts(tls_anchors + i, leaf_from) ||
                    !tls_anchor_key(tls_anchors + i, address_of root))
                        continue;
                if (cert->curve == 3
                        ? root.modulus_length == cert->modulus_length &&
                              root.exponent == cert->exponent &&
                              !memory_compare(root.modulus, cert->modulus,
                                              root.modulus_length)
                        : !memory_compare(root.qx, cert->qx, 48) &&
                              !memory_compare(root.qy, cert->qy, 48))
                        return true;
        }

        return false;
}

/* Whether the certificate's key is one of tls_blocked_keys, or an EC key anyone
   can compute: k*G for a k up to TLS_SMALL_KEYS (tls_small_keys), which is how
   eMudhra's eight were made. The next CA to make that mistake is refused
   before anybody names its keys. */
static COLD bool tls_key_blocked(const tls_cert address_to cert)
{
        p8 digest[32];
        p8 key[96];
        positive coord = cert->curve == 1 ? 32 : 48;

        if (!cert->spki || !cert->spki_length)
                return false;
        crypto_sha256_of(cert->spki, cert->spki_length, digest);
        for (positive i = 0; i < array_count(tls_blocked_keys); i++)
                if (!memory_compare(tls_blocked_keys[i], digest, 32))
                        return true;
        if (cert->curve != 1 && cert->curve != 2)
                return false;
        memory_copy(key, cert->qx + 48 - coord, coord);
        memory_copy(key + coord, cert->qy + 48 - coord, coord);
        crypto_sha256_of(key, coord * 2, digest);
        for (positive k = 0; k < TLS_SMALL_KEYS; k++)
                if (!memory_compare(tls_small_keys + ((cert->curve - 1) * TLS_SMALL_KEYS + k) * 8,
                                    digest, 8))
                        return true;
        return false;
}

static COLD bool tls_certificate_names_chain(const tls_cert address_to child,
                                        const tls_cert address_to issuer)
{
        /* The server provides an already ordered path.  A signature made by
           a reused CA key is not sufficient to choose which CA identity and
           constraints issued the child: issuer and subject Names must also
           identify the same certificate authority.  Exact DER comparison is
           deliberately fail closed; conforming issuers reproduce their
           subject Name in issued certificates. */
        return child->issuer && issuer->subject && child->issuer_length &&
               child->issuer_length == issuer->subject_length &&
               !memory_compare(child->issuer, issuer->subject,
                               child->issuer_length);
}

static COLD bool tls_verify_one(tls_cert address_to child, tls_cert address_to issuer)
{
        static const p8 digests[3] = {DIGEST_SHA256, DIGEST_SHA384,
                                      DIGEST_SHA512};
        p8 kind = tls_signature_kind(child->sig_oid, child->sig_oid_length);
        positive hash_length = 16 + 16 * (positive)(kind & 3);
        digest_state digest;
        p8 hash[64];
        p8 r[48];
        p8 s[48];
        positive r_length = 0;
        positive s_length = 0;
        p8 address_to qx = issuer->qx + (issuer->curve == 1 ? 16 : 0);
        p8 address_to qy = issuer->qy + (issuer->curve == 1 ? 16 : 0);

        if (!kind || (kind & 4 ? issuer->curve != 3
                               : issuer->curve != 1 && issuer->curve != 2))
                return false;
        digest_open(address_of digest, digests[(kind & 3) - 1], hash_length);
        digest_write(address_of digest, child->tbs, child->tbs_length);
        digest_close(address_of digest, hash);
        if (kind & 4)
        {
                /* DigestInfo: the SEQUENCE of AlgorithmIdentifier
                   {id-sha256/384/512 (2.16.840.1.101.3.4.2.1-3), NULL} and
                   the OCTET STRING hash. */
                const p8 info[19] = {
                    0x30, (p8)(17 + hash_length), 0x30, 0x0d, 0x06, 0x09,
                    0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x02,
                    (p8)(kind & 3), 0x05, 0x00, 0x04, (p8)hash_length};

                return crypto_rsa_pkcs1(issuer->modulus, issuer->modulus_length,
                                        issuer->exponent, child->sig,
                                        child->sig_length, info, sizeof info,
                                        hash, hash_length);
        }
        if (tls_parse_ecdsa_sig(child->sig, child->sig_length, r,
                                address_of r_length, s, address_of s_length))
                return false;
        return issuer->curve == 1
                   ? crypto_ecdsa_p256(hash, hash_length, r, r_length, s,
                                       s_length, qx, qy)
                   : crypto_ecdsa_p384(hash, hash_length, r, r_length, s,
                                       s_length, qx, qy);
}

/* The last certificate served names its issuer.  Each anchor with that
   subject Name is tried, and one signature that verifies ends the chain; a
   Name no anchor carries fails without any signature check. The anchor that
   signed goes to signer when there is one to fill: its key is what the
   child's revocation answers are verified under. */
static COLD bool tls_anchor_verifies(tls_cert address_to child, p64 leaf_from,
                                     tls_cert address_to signer)
{
        p8 digest[32];
        tls_cert root;

        if (!child->issuer || !child->issuer_length)
                return false;
#ifdef TLS_BENCH_ANCHOR
        memory_fill(address_of root, 0, sizeof(root));
        root.curve = 2;
        memory_copy(root.qx, tls_bench_anchor_x, 48);
        memory_copy(root.qy, tls_bench_anchor_y, 48);
        if (!tls_bench_distrusts(leaf_from) &&
            tls_verify_one(child, address_of root))
                goto signed_by;
#endif
        crypto_sha256_of(child->issuer, child->issuer_length, digest);
        for (positive i = 0; i < array_count(tls_anchors); i++)
        {
                if (memory_compare(tls_anchors[i].name, digest, 8) ||
                    tls_anchor_distrusts(tls_anchors + i, leaf_from) ||
                    !tls_anchor_key(tls_anchors + i, address_of root))
                        continue;
                if (tls_verify_one(child, address_of root))
                        goto signed_by;
        }

        return false;

signed_by:
        if (signer)
                address_to signer = root;
        return true;
}

static COLD fn tls_keep_leaf(tls_conn address_to tls, tls_cert address_to leaf)
{
        tls->leaf_curve = leaf->curve;
        tls->leaf_n_length = 0;
        tls->leaf_e = 0;
        memory_fill(tls->leaf_qx, 0, sizeof(tls->leaf_qx));
        memory_fill(tls->leaf_qy, 0, sizeof(tls->leaf_qy));
        memory_fill(tls->leaf_n, 0, sizeof(tls->leaf_n));
        if (leaf->curve == 3)
        {
                memory_copy(tls->leaf_n, leaf->modulus, leaf->modulus_length);
                tls->leaf_n_length = leaf->modulus_length;
                tls->leaf_e = leaf->exponent;
        }
        else
        {
                memory_copy(tls->leaf_qx, leaf->qx, 48);
                memory_copy(tls->leaf_qy, leaf->qy, 48);
        }
}

/*
        Whether the clock can be believed at all, before anything asks what
        it says.

        A certificate is only as current as the clock that judges it, and a
        clock that nobody set reads 1970, or whatever a dead RTC battery
        left, which is earlier than the certificate's own notBefore on the
        one hand and, rolled back on purpose, earlier than an expired
        certificate's notAfter on the other. So the clock has a floor, the
        later of two moments this system already knows it is past:

            MOONWATER_CLOCK_FLOOR, the date of the source this was built
            from, which the SNTP client also refuses to step below (it is
            SNTP_WALL_LEAST there), moved forward with each release so that
            nothing older than the code counts as now; and

            /root/clock.good, the latest time this machine knew for sure:
            an authenticated (NTS) answer's own time, or the floor the boot
            began with plus the seconds it has run (locale_clock_persist).
            A file that is not a decimal number of seconds between the
            build floor and the end of the NTP window, or that is not a
            plain file (a link, a FIFO), is ignored, so a damaged or planted
            one can only fail to help, never push the floor out of reach of
            a real clock. Deleting it is always safe. A program that cannot
            read /root (anyone but root) judges by the build's floor alone.

        A clock below the floor is not wrong by some amount, it is unset:
        tls_date_now refuses it and the connection says so, with how to
        fix it, instead of judging dates against a number that means
        nothing. The real time comes from `moonwater time sync` (NTS first,
        plain NTP if no server offers it), or by hand with `date -s`.
*/
#ifndef MOONWATER_CLOCK_FLOOR
#define MOONWATER_CLOCK_FLOOR 1790812800ll /* 2026-10-01 */
#endif
#ifndef CLOCK_GOOD_PATH
#define CLOCK_GOOD_PATH "/root/clock.good"
#endif
//      SNTP_WALL_MOST, the last second an NTP stamp can carry (2104-02-26).
#define CLOCK_GOOD_MOST 4233462143ll

//      The floor a confined child read before its filter went on (net_apart),
//      since it may not open the file after; 0 everywhere else.
static p64 clock_floor_held;

//      The floor, in seconds since the epoch: the build's, or the
//      persisted last-known-good time when that is later.
static COLD p64 clock_trust_floor(void)
{
        p8 text[24];
        bipolar got;
        p64 value = 0;
        positive at = 0;

        if (clock_floor_held)
                return clock_floor_held;
        got = file_slurp_regular_at(AT_FDCWD, (string_address)CLOCK_GOOD_PATH,
                                    text, sizeof(text), O_NOFOLLOW);
        if (got < 1 || got > 12)
                return MOONWATER_CLOCK_FLOOR;
        while (at < (positive)got && text[at] >= '0' && text[at] <= '9')
                value = value * 10 + (p64)(text[at++] - '0');
        while (at < (positive)got && text[at] == '\n')
                at++;
        if (!at || at != (positive)got || value < MOONWATER_CLOCK_FLOOR ||
            value > CLOCK_GOOD_MOST)
                return MOONWATER_CLOCK_FLOOR;
        return value;
}

//      A moment as a certificate's dates are kept: YYYYMMDDHHMMSS.
static COLD bool tls_date_of(time_t stamp, p64 address_to value)
{
        tm calendar;
        p64 answer;

        if (stamp < 0 || !gmtime_r(address_of stamp, address_of calendar))
                return false;
        answer = (p64)(calendar.tm_year + 1900);
        answer = answer * 100 + (p64)(calendar.tm_mon + 1);
        answer = answer * 100 + (p64)calendar.tm_mday;
        answer = answer * 100 + (p64)calendar.tm_hour;
        answer = answer * 100 + (p64)calendar.tm_min;
        answer = answer * 100 + (p64)calendar.tm_sec;
        address_to value = answer;
        return true;
}

static COLD bool tls_date_now(p64 address_to value)
{
        time_t stamp = time(null);

        return stamp >= 0 && (p64)stamp >= clock_trust_floor() &&
               tls_date_of(stamp, value);
}

/* The date the floor is, for the connection that has to go ahead on a clock
   it cannot yet believe (tls->clock_bootstrap). */
static COLD bool tls_date_floor(p64 address_to value)
{
        return tls_date_of((time_t)clock_trust_floor(), value);
}

static COLD bool tls_cert_current(tls_cert address_to cert, p64 now)
{
        return !cert->unsupported_critical && cert->not_before <= now &&
               now <= cert->not_after;
}

/* A certificate that says it is a certificate authority is not an end
   entity. Every issuer that means to authorize a server writes CA:FALSE or
   leaves basicConstraints out, so refusing CA:TRUE here costs nothing real
   and closes the case where an intermediate anywhere in a chain stands in
   for a host its own key was only ever meant to sign for. */
static COLD bool tls_leaf_authorized(tls_cert address_to cert, p64 now)
{
        return tls_cert_current(cert, now) && !cert->ca &&
               (!cert->key_usage || cert->digital_signature) &&
               (!cert->extended_key_usage || cert->server_auth);
}

static COLD bool tls_issuer_authorized(tls_cert address_to cert, positive ca_below,
                                  p64 now)
{
        return tls_cert_current(cert, now) && cert->basic_constraints && cert->ca &&
               (!cert->key_usage || cert->key_cert_sign) &&
               (!cert->extended_key_usage || cert->server_auth) &&
               (!cert->path_length_present || ca_below <= cert->path_length);
}

/* Whether every name cert carries lies within constraints: a non-empty
   subject as a directoryName, an emailAddress attribute in it as an
   rfc822Name no subtree can vouch for (tls_name_allowed with no name), and
   each subjectAltName entry as its own form. A subject whose RDNs do not
   parse is refused, the way an unplaceable name is. */
static COLD bool tls_cert_names_permitted(p8 address_to constraints,
                                          positive length,
                                          tls_cert address_to cert)
{
        static const p8 email[] = {0x06, 0x09, 0x2a, 0x86, 0x48, 0x86,
                                   0xf7, 0x0d, 0x01, 0x09, 0x01};
        p8 address_to subject = cert->subject;
        positive at = 0;
        positive stop = 0;
        bool mailbox = false;

        if (tls_asn1_enter(subject, cert->subject_length, 0x30, address_of at,
                           address_of stop) ||
            (at < stop && !tls_name_allowed(constraints, length, 0xa4, subject,
                                            cert->subject_length)))
                return false;
        while (at < stop)
        {
                positive rdn_stop = 0;

                if (tls_asn1_enter(subject, stop, 0x31, address_of at,
                                   address_of rdn_stop))
                        return false;
                while (at < rdn_stop)
                {
                        positive ava_stop = 0;

                        if (tls_asn1_enter(subject, rdn_stop, 0x30, address_of at,
                                           address_of ava_stop))
                                return false;
                        mailbox |= ava_stop - at >= sizeof email &&
                                   !memory_compare(subject + at, email, sizeof email);
                        at = ava_stop;
                }
        }
        if (mailbox && !tls_name_allowed(constraints, length, 0x81, null, 0))
                return false;

        at = 0;
        if (!cert->san || tls_asn1_enter(cert->san, cert->san_length, 0x30,
                                         address_of at, address_of stop))
                return true;
        while (at < stop)
        {
                p8 tag = cert->san[at];
                positive name_stop = 0;

                if (tls_asn1_enter(cert->san, stop, tag, address_of at,
                                   address_of name_stop) ||
                    !tls_name_allowed(constraints, length, tag, cert->san + at,
                                      name_stop - at))
                        return false;
                at = name_stop;
        }
        return true;
}

/* An issuer's name constraints bind every certificate below it on the
   path, path[0] the leaf, but an intermediate that issued itself (RFC 5280
   6.1.3 (b) and 6.1.4 (b) leave self-issued certificates out). */
static COLD bool tls_path_permitted(tls_cert address_to certs,
                                    const positive address_to path,
                                    positive below, tls_cert address_to issuer)
{
        for (positive k = 0; issuer->name_constraints && k < below; k++)
        {
                tls_cert address_to cert = certs + path[k];

                if (k && cert->issuer_length == cert->subject_length &&
                    !memory_compare(cert->issuer, cert->subject,
                                    cert->subject_length))
                        continue;
                if (!tls_cert_names_permitted(issuer->name_constraints,
                                              issuer->name_constraints_length,
                                              cert))
                        return false;
        }
        return true;
}

// A TLS handshake length: three bytes, most significant first.
static PURE positive tls_load_24(p8 address_to at)
{
        return (positive)network_load_16(at) << 8 | at[2];
}

static COLD bool tls_certificate_body_open(p8 address_to body,
                                      positive body_length,
                                      byte_reader address_to list)
{
        byte_reader reader = byte_reader_open(body, body_length);

        /* The server Certificate in this initial handshake has an empty
           request_context.  Its three-byte list vector must consume the rest
           of the handshake body exactly. */
        if (byte_reader_u8(&reader) != 0)
                return false;
        address_to list = byte_reader_vector24(&reader);
        return byte_reader_end(&reader);
}

/* A CertificateEntry's extensions (RFC 8446 4.4.2): none, or -- when the
   hello offered status_request -- that one alone, a CertificateStatus (RFC
   6066 8) of status_type ocsp and an OCSPResponse of at least a byte, filling
   the extension. The response is handed back unread (only the leaf's is
   read, by tls_ocsp_answer); an SCT list or any other kind answers nothing
   this client asked, and OpenSSL refuses the two it knows too. */
static COLD bool tls_entry_status(const p8 address_to bytes, positive length,
                                  bool asked,
                                  const p8 address_to address_to response,
                                  positive address_to response_length)
{
        byte_reader reader = byte_reader_open(bytes, length);
        byte_reader status;
        byte_reader answer;

        address_to response = null;
        address_to response_length = 0;
        if (!length)
                return true;
        if (!asked || byte_reader_u16(&reader) != 5)
                return false;
        status = byte_reader_vector16(&reader);
        if (!byte_reader_end(&reader) || byte_reader_u8(&status) != 1)
                return false;
        answer = byte_reader_vector24(&status);
        if (!byte_reader_end(&status) || !byte_reader_left(&answer))
                return false;
        address_to response = byte_reader_here(&answer);
        address_to response_length = byte_reader_left(&answer);
        return true;
}

/* The issuer certificate a caIssuers URL names, fetched into room over the
   HTTP client (defined there, beside it): its length, or 0. */
#define TLS_AIA_MAX 16384

static COLD positive tls_aia_fetch(const p8 address_to url, positive length,
                                   p8 address_to into, positive room);

/* Whether the certificates on a verified path are revoked, as their issuers
   answer, which tls_verify_chain asks last: defined with the HTTP client,
   beside the CRL fetch. */
static COLD bool tls_revocation_holds(tls_conn address_to tls,
                                      tls_cert address_to certs,
                                      const positive address_to path,
                                      positive depth, tls_cert address_to top,
                                      const p8 address_to staple,
                                      positive staple_length, p64 now);

/* Whether a verified path's leaf was logged as Certificate Transparency
   asks, which the tight tier holds a public root's chain to: defined beside
   the revocation checks. */
static COLD bool tls_ct_holds(tls_cert address_to certs, const positive address_to path,
                              positive depth, tls_cert address_to top);

/* A path to an anchor: logged where the tier asks, then not revoked. */
static COLD bool tls_trust_holds(tls_conn address_to tls, tls_cert address_to certs,
                                 const positive address_to path, positive depth,
                                 tls_cert address_to top, const p8 address_to staple,
                                 positive staple_length, p64 now)
{
        if (MOONWATER_STRICT_NET >= STRICT_TIGHT && !tls_ct_holds(certs, path, depth, top))
        {
                tls->fault = TLS_FAULT_UNLOGGED;
                return false;
        }
        return tls_revocation_holds(tls, certs, path, depth, top, staple, staple_length, now);
}

/* The leaf comes first; the rest is a pool, as RFC 8446 section 4.4.2 asks
   clients to treat it. Of 640 public HTTPS hosts openssl verified on
   2026-09-28, 25 served intermediates out of order or an extra certificate
   (a legacy root, a SHA-1 cross-certificate) that a served-order walk
   refused. The path grows from the leaf: each step takes the first unused
   served certificate whose subject names the step's issuer, that may issue
   at this depth or carries an anchor key (which ends the path), and whose
   key verifies the step and whose name constraints, if any, hold every
   certificate on the path below it; when none does, the anchors must sign
   for the step. Asking the anchors first would accept nothing more -- a served
   certificate the step verifies under has the signing key, so it carries
   the anchor's key if an anchor signed -- and would cost the https_bench
   anchor a failed P-384 verify per chain. A certificate that fails to
   parse is never a candidate. A chain served in order costs what it did;
   each entry is used once, at most 28 served signature checks.

   A server that leaves an intermediate out (3 of 642 hosts on 2026-09-28:
   gob.mx, monster.com, ssa.gov) gets what browsers give it: when neither
   the pool nor the anchors sign for a step, the issuer that step's
   certificate names in its caIssuers location is fetched once
   (tls_aia_fetch) and joins the pool as one more candidate, held to every
   test a served one is. A fetched certificate's own missing issuer is not
   fetched in turn.

   A path that ends at an anchor is then asked about revocation
   (tls_revocation_holds): the leaf by the OCSP response stapled in its
   entry or by its CRL, each certificate above it by its CRL. */
static COLD bool tls_verify_chain(p8 address_to body, positive body_length,
                             string_address host, tls_conn address_to tls)
{
        tls_cert certs[9];
        tls_cert root;
        positive path[9] = {0};
        p8 fetched[TLS_AIA_MAX];
        positive count = 0;
        byte_reader list;
        positive unusable = 0;
        positive child = 0;
        positive depth = 0;
        p64 now = 0;
        bool asked = false;
        const p8 address_to staple = null;
        positive staple_length = 0;
        bool unset = false;
        p64 leaf_from;

        if (!tls_certificate_body_open(body, body_length, address_of list))
                return false;

        //      Each entry is a certificate and its extensions, which can only
        //      answer what a ClientHello asked for (RFC 8446 4.2, 4.4.2): an
        //      OCSP status when the hello offered status_request, and nothing
        //      else (tls_entry_status).
        while (byte_reader_left(&list) && count < 8)
        {
                byte_reader entry = byte_reader_vector24(&list);
                byte_reader extensions = byte_reader_vector16(&list);
                const p8 address_to status = null;
                positive status_length = 0;

                if (!byte_reader_ok(&list) ||
                    !tls_entry_status(byte_reader_here(&extensions),
                                      byte_reader_left(&extensions),
                                      tls->status_requested, address_of status,
                                      address_of status_length))
                        return false;
                if (!count)
                {
                        staple = status;
                        staple_length = status_length;
                }
                if (tls_parse_cert((p8 address_to)byte_reader_here(&entry),
                                   byte_reader_left(&entry), certs + count,
                                   count ? null : host))
                {
                        if (!count)
                                return false;
                        unusable |= (positive)1 << count;
                }
                count++;
        }

        if (!count || byte_reader_left(&list))
                return false;

        //      The leaf's own notBefore, for Mozilla's distrust-after dates,
        //      whatever the clock's judgement does to the copy below.
        leaf_from = certs[0].not_before;
        tls_keep_leaf(tls, certs);

        if (tls->check_cert && !certs[0].san_match)
        {
                tls->fault = TLS_FAULT_NAME;
                return false;
        }
        if (!tls->check_cert)
                return true;

        //      A compromised CA key (tls_blocked_keys) in a served
        //      certificate: the leaf's refuses the connection, an
        //      intermediate is never a step of the path.
        for (positive at = 0; at < count; at++)
                if (!(unusable >> at & 1) && tls_key_blocked(certs + at))
                {
                        tls->fault = TLS_FAULT_REVOKED;
                        if (!at)
                                return false;
                        unusable |= (positive)1 << at;
                }

        if (tls->clock_bootstrap)
        {
                /* The connection that will say what time it is cannot be
                   judged by the time it has not yet said, whether the clock
                   is unset or only wrong (an RTC two years out reads every
                   certificate expired). It is judged as of the floor: the
                   certificate has to be unexpired on the day this system
                   was built or last sure (an old, stolen one is not), and
                   notBefore is not asked, since a short-lived certificate
                   was issued after that day and the clock cannot say when
                   it is now. chrony's nocerttimecheck does the same, with
                   no floor at all. The caller asks for this only until an
                   authenticated answer has been taken this boot. */
                if (!tls_date_floor(address_of now))
                {
                        tls->fault = TLS_FAULT_CLOCK;
                        return false;
                }
                unset = true;
                for (positive at = 0; at < count; at++)
                        certs[at].not_before = 0;
        }
        else if (!tls_date_now(address_of now))
        {
                tls->fault = TLS_FAULT_CLOCK;
                return false;
        }
        if (certs[0].not_before > now)
                tls->fault = TLS_FAULT_EARLY;
        else if (certs[0].not_after < now)
                tls->fault = TLS_FAULT_LATE;
        if (!tls_leaf_authorized(certs, now) ||
            tls_spki_is_anchor(certs, leaf_from))
                return false;
        for (;;)
        {
                positive next;
                bool anchor = false;

                for (next = 1; next < count; next++)
                        if (!(unusable >> next & 1) &&
                            tls_certificate_names_chain(certs + child,
                                                        certs + next) &&
                            ((anchor = tls_spki_is_anchor(certs + next,
                                                          leaf_from)) ||
                             tls_issuer_authorized(certs + next, depth, now)) &&
                            tls_verify_one(certs + child, certs + next) &&
                            tls_path_permitted(certs, path, depth + 1,
                                               certs + next))
                                break;
                if (next == count)
                {
                        positive length;

                        if (tls_anchor_verifies(certs + child, leaf_from,
                                                address_of root))
                                return tls_trust_holds(tls, certs, path, depth,
                                                       address_of root, staple,
                                                       staple_length, now);
                        if (asked || !certs[child].ca_issuers)
                                return false;
                        asked = true;
                        length = tls_aia_fetch(certs[child].ca_issuers,
                                               certs[child].ca_issuers_length,
                                               fetched, sizeof fetched);
                        if (!length || tls_parse_cert(fetched, length,
                                                      certs + count, null))
                                return false;
                        if (tls_key_blocked(certs + count))
                        {
                                tls->fault = TLS_FAULT_REVOKED;
                                return false;
                        }
                        if (unset)
                                certs[count].not_before = 0;
                        count++;
                        continue;
                }
                if (anchor)
                        return tls_trust_holds(tls, certs, path, depth, certs + next,
                                               staple, staple_length, now);
                unusable |= (positive)1 << next;
                child = next;
                path[++depth] = next;
        }
}

/* A key share on one of the groups supported_groups names, as its group
   id: the scalar in tls->scalar, the public value's length, and the shared
   secret's. X25519 is the one share a first ClientHello carries, as curl's
   and OpenSSL's do; a HelloRetryRequest may ask for P-256 or P-384 instead,
   which costs one round trip on the servers that want it and spares every
   other connection the two NIST key generations. */
static COLD positive tls_share_length(positive group)
{
        return group == 0x001d ? 32 : group == 0x0017 ? 65 : group == 0x0018 ? 97 : 0;
}

static COLD bool tls_share_scalar(tls_conn address_to tls, positive group)
{
        p8 raw[48];
        positive width = group == 0x0018 ? 48 : 32;
        bool made = false;

        tls->group = (p16)group;
        if (group == 0x001d)
                made = system_random_fill(tls->scalar, 32, 0) >= 0;
        for (positive tries = 0; group != 0x001d && !made && tries < 9; tries++)
        {
                if (system_random_fill(raw, width, 0) < 0)
                        break;
                made = crypto_scalar_reduce_be(tls->scalar, raw, width,
                                               width == 48 ? crypto_p384_n
                                                           : crypto_p256_n,
                                               width / 8);
        }
        crypto_forget(raw, sizeof raw);
        return made;
}

/* The public value for the share, or the secret agreed with the peer's: out
   takes tls_share_length bytes, or 48 for a shared secret. The length
   written, 0 on failure. */
static COLD positive tls_share_use(tls_conn address_to tls, p8 address_to peer,
                                   p8 address_to out)
{
        static const p8 base[32] = {9};
        bool made;

        if (tls->group == 0x001d)
                made = crypto_x25519(out, tls->scalar,
                                     peer ? peer : (p8 address_to)base);
        else if (tls->group == 0x0017)
                made = peer ? crypto_ecdh_p256_shared(out, tls->scalar, peer)
                            : crypto_ecdh_p256_public(out, tls->scalar);
        else
                made = peer ? crypto_ecdh_p384_shared(out, tls->scalar, peer)
                            : crypto_ecdh_p384_public(out, tls->scalar);
        if (!made)
                return 0;
        return peer ? (tls->group == 0x0018 ? 48 : 32)
                    : tls_share_length(tls->group);
}

/* The first ClientHello draws the random and an X25519 share; after a
   HelloRetryRequest the same hello goes again with the share the server
   asked for (already drawn into tls) and its cookie echoed, the whole
   cookie extension body, length included (RFC 8446 4.1.2). */
static COLD bipolar tls_client_hello(tls_conn address_to tls, p8 address_to out,
                                positive room, positive address_to used,
                                const p8 address_to cookie,
                                positive cookie_length)
{
        static const p8 prefix[] = {
            TLS_HS_CLIENT_HELLO, 0, 0, 0, 0x03, 0x03};
        static const p8 parameters[] = {
            0,                    // empty legacy session id
            0, 6, 0x13, 0x01,    // TLS_AES_128_GCM_SHA256
            0xc0, 0x2b,           // TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256
            0xc0, 0x2f,           // TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256
            1, 0,                 // null legacy compression
            0, 0};                // extensions length, filled below
        /* ec_point_formats (uncompressed), supported_groups,
           extended_master_secret and an empty renegotiation_info: TLS 1.2
           needs them, TLS 1.3 servers pass them over. */
        static const p8 groups[] = {
            0, 0x0b, 0, 2, 1, 0,
            0, 0x0a, 0, 8, 0, 6, 0, 0x1d, 0, 0x17, 0, 0x18,
            0, 0x17, 0, 0,
            0xff, 0x01, 0, 1, 0};
        static const p8 tail[] = {
            0, 0x2b, 0, 5, 4, 0x03, 0x04, 0x03, 0x03,
            0, 0x0d, 0, 12, 0, 10, 0x04, 0x03, 0x05, 0x03, 0x08, 0x04,
            0x04, 0x01, 0x05, 0x01};
        /* status_request (RFC 6066 8): OCSP, no responder named, no request
           extensions. Only when the chain is checked: a staple is read for
           revocation alone, so an unchecked connection asks for none and
           keeps refusing one. */
        static const p8 status_request[] = {0, 5, 0, 5, 1, 0, 0, 0, 0};
        /* The same, with TLS 1.3 alone on offer, for a connection that may
           not be TLS 1.2 (tls->alpn): supported_versions is the first nine
           bytes of tail. */
        static const p8 only_thirteen[] = {0, 0x2b, 0, 3, 2, 0x03, 0x04};
        p8 share[10 + 97];
        positive share_length;
        positive host_length = string_length(tls->host);
        byte_store hello = {out, room, 0};
        bool letter = false;
        bool colon = false;
        positive ext_len_at;
        bipolar status = TLS_FAIL;

        if (!tls->group &&
            (system_random_fill(tls->client_random, 32, 0) < 0 ||
             !tls_share_scalar(tls, 0x001d)))
                goto done;
        share_length = tls_share_use(tls, null, share + 10);
        if (!share_length)
                goto done;
        share[0] = 0;
        share[1] = 0x33;
        network_store_16(share + 2, (p16)(share_length + 6));
        network_store_16(share + 4, (p16)(share_length + 4));
        network_store_16(share + 6, tls->group);
        network_store_16(share + 8, (p16)share_length);

        if (!byte_store_append_exact(address_of hello, prefix, sizeof prefix) ||
            !byte_store_append_exact(address_of hello, tls->client_random, 32) ||
            !byte_store_append_exact(address_of hello, parameters,
                                     sizeof parameters))
                goto done;

        ext_len_at = hello.used - 2;

        /* server_name names a DNS host (RFC 6066 3): an absolute name
           without its trailing dot, and never an address -- nothing with a
           colon (IPv6, bracketed or bare) and nothing of digits and dots
           alone (IPv4, which no top-level domain can spell). */
        if (host_length && tls->host[host_length - 1] == '.')
                host_length--;
        for (positive i = 0; i < host_length; i++)
        {
                colon |= tls->host[i] == ':';
                letter |= tls->host[i] != '.' &&
                          (tls->host[i] < '0' || tls->host[i] > '9');
        }
        tls->named = letter && !colon && host_length < 256;
        if (tls->named)
        {
                positive n = 5 + host_length;
                p8 sni[] = {
                    0, 0, (p8)(n >> 8), (p8)n,
                    (p8)((n - 2) >> 8), (p8)(n - 2), 0,
                    (p8)(host_length >> 8), (p8)host_length};

                if (!byte_store_append_exact(address_of hello, sni,
                                             sizeof sni) ||
                    !byte_store_append_exact(address_of hello, tls->host,
                                             host_length))
                        goto done;
        }

        if (cookie_length)
        {
                p8 head[4] = {0, 0x2c, (p8)(cookie_length >> 8),
                              (p8)cookie_length};

                if (cookie_length > 0xffff ||
                    !byte_store_append_exact(address_of hello, head,
                                             sizeof head) ||
                    !byte_store_append_exact(address_of hello, cookie,
                                             cookie_length))
                        goto done;
        }

        /* application_layer_protocol_negotiation (RFC 7301 3.1): one
           protocol name, only when the caller names one. */
        if (tls->alpn_length)
        {
                positive n = 3 + tls->alpn_length;
                p8 head[] = {0, 0x10, (p8)(n >> 8), (p8)n,
                             (p8)((n - 2) >> 8), (p8)(n - 2),
                             (p8)tls->alpn_length};

                if (tls->alpn_length > 32 ||
                    !byte_store_append_exact(address_of hello, head,
                                             sizeof head) ||
                    !byte_store_append_exact(address_of hello, tls->alpn,
                                             tls->alpn_length))
                        goto done;
        }

        if (!byte_store_append_exact(address_of hello, groups, sizeof groups) ||
            !byte_store_append_exact(address_of hello, share,
                                     10 + share_length) ||
            (tls->alpn_length
                 ? !byte_store_append_exact(address_of hello, only_thirteen,
                                            sizeof only_thirteen) ||
                       !byte_store_append_exact(address_of hello, tail + 9,
                                                sizeof tail - 9)
                 : !byte_store_append_exact(address_of hello, tail,
                                            sizeof tail)) ||
            (tls->check_cert &&
             !byte_store_append_exact(address_of hello, status_request,
                                      sizeof status_request)))
                goto done;
        tls->status_requested = tls->check_cert;

        {
                positive ext_length = hello.used - ext_len_at - 2;
                network_store_16(out + ext_len_at, (p16)ext_length);
        }
        {
                positive body = hello.used - 4;
                out[1] = (p8)(body >> 16);
                out[2] = (p8)(body >> 8);
                out[3] = (p8)body;
        }

        address_to used = hello.used;
        status = TLS_OK;

done:
        if (status)
                crypto_forget(tls->scalar, sizeof tls->scalar);
        return status;
}

/* HelloRetryRequest is a ServerHello whose random is SHA-256 of
   "HelloRetryRequest" (RFC 8446 4.1.3). */
static const p8 tls_retry_random[32] = {
    0xcf, 0x21, 0xad, 0x74, 0xe5, 0x9a, 0x61, 0x11, 0xbe, 0x1d, 0x8c, 0x02,
    0x1e, 0x65, 0xb8, 0x91, 0xc2, 0xa2, 0x11, 0x16, 0x7a, 0xbb, 0x8c, 0x5e,
    0x07, 0x9e, 0x09, 0xe2, 0xc8, 0xa8, 0x33, 0x9c};

/* A ServerHello answers TLS_OK with the peer's share in peer, on the one
   group tls->group the client sent a share for; a HelloRetryRequest
   answers TLS_RETRY with the group it asks for in group (0 when it asks
   for none) and its cookie extension body in cookie and cookie_length
   (null and 0 when it has none). A retry has to ask for something -- a
   group offered but not already shared, or a cookie -- and may carry
   nothing else (RFC 8446 4.1.4).

   Without supported_versions the answer is TLS 1.2, which sets tls->tls12
   and tls->suite: one of the two ECDHE suites offered, the extended master
   secret (RFC 7627) and an empty renegotiation_info (RFC 5746) required,
   point formats that include uncompressed, a session id the server may
   choose; and never the random's downgrade sentinel, since TLS 1.3 was on
   offer (RFC 8446 4.1.3). An empty status_request when the hello offered
   one says a CertificateStatus may follow the Certificate (RFC 6066 8),
   which tls->status_echoed records; TLS 1.3 carries the staple in the
   Certificate instead, so its ServerHello may not echo it. Each version
   refuses the other's extensions. Both record the server's random. */
static COLD bipolar tls_server_hello_keys(p8 address_to hello, positive length,
                                     tls_conn address_to tls,
                                     p8 address_to peer, positive room,
                                     positive address_to share_length,
                                     positive address_to group,
                                     const p8 address_to address_to cookie,
                                     positive address_to cookie_length)
{
        static const p8 kinds[] = {0x2b, 0x33, 0x2c, 0x17, 0x0b, 0x00};
        byte_reader message = byte_reader_open(hello, length);
        byte_reader body;
        byte_reader extensions = byte_reader_open(null, 0);
        const p8 address_to hello_random;
        positive session;
        positive suite;
        positive seen = 0;
        bool retry;

        if (length < 44 || byte_reader_u8(&message) != TLS_HS_SERVER_HELLO)
                return TLS_FAIL;
        body = byte_reader_window(&message, byte_reader_u24(&message));
        if (!byte_reader_end(&message) || byte_reader_u16(&body) != 0x0303)
                return TLS_FAIL;
        hello_random = byte_reader_take(&body, 32);
        if (!hello_random)
                return TLS_FAIL;

        retry = !memory_compare(hello_random, tls_retry_random, 32);
        memory_copy(tls->server_random, hello_random, 32);
        address_to group = 0;
        address_to cookie = null;
        address_to cookie_length = 0;
        session = byte_reader_u8(&body);
        if (session > 32 || !byte_reader_skip(&body, session))
                return TLS_FAIL;
        suite = byte_reader_u16(&body);
        if (byte_reader_u8(&body) != 0 || !byte_reader_ok(&body))
                return TLS_FAIL;
        //      The extensions, when there are any, fill what is left.
        if (byte_reader_left(&body))
        {
                extensions = byte_reader_vector16(&body);
                if (!byte_reader_end(&body))
                        return TLS_FAIL;
        }

        while (byte_reader_left(&extensions))
        {
                positive id = byte_reader_u16(&extensions);
                byte_reader data = byte_reader_vector16(&extensions);
                positive bit = 8;

                if (!byte_reader_ok(&extensions))
                        return TLS_FAIL;
                /* One bit a kind this client offered, in kinds' order,
                   renegotiation_info and then status_request last; any
                   other kind, or one twice, fails. */
                for (positive kind = 0; kind < sizeof kinds; kind++)
                        if (id == kinds[kind])
                                bit = kind;
                if (id == 0xff01)
                        bit = 6;
                if (id == 0x0005 && tls->status_requested)
                        bit = 7;
                if (bit == 8 || seen >> bit & 1)
                        return TLS_FAIL;
                seen |= (positive)1 << bit;

                if (id == 0x002b)
                {
                        if (byte_reader_u16(&data) != 0x0304 ||
                            !byte_reader_end(&data))
                                return TLS_FAIL;
                }
                else if (id == 0x0033)
                {
                        positive named = byte_reader_u16(&data);
                        positive klen = tls_share_length(named);
                        const p8 address_to share;

                        /* A retry names an offered group not yet shared; a
                           hello answers the share, at its one length. */
                        if (!byte_reader_ok(&data) || !klen ||
                            (named == tls->group) == retry)
                                return TLS_FAIL;
                        if (retry)
                                klen = 0;
                        else if (klen > room ||
                                 byte_reader_u16(&data) != klen)
                                return TLS_FAIL;
                        share = byte_reader_take(&data, klen);
                        if (!share || !byte_reader_end(&data))
                                return TLS_FAIL;
                        memory_copy(peer, share, klen);
                        address_to share_length = klen;
                        address_to group = named;
                }
                else if (id == 0x002c)
                {
                        byte_reader whole = data;
                        positive inner;

                        if (!retry || byte_reader_left(&whole) < 3)
                                return TLS_FAIL;
                        inner = byte_reader_u16(&data);
                        if (inner != byte_reader_left(&data))
                                return TLS_FAIL;
                        address_to cookie = byte_reader_here(&whole);
                        address_to cookie_length = byte_reader_left(&whole);
                }
                else if (id == 0x000b)
                {
                        /* The formats list has to hold uncompressed. */
                        bool uncompressed = false;
                        byte_reader formats = byte_reader_vector8(&data);

                        while (byte_reader_left(&formats))
                                uncompressed |= !byte_reader_u8(&formats);
                        if (!uncompressed || !byte_reader_end(&data))
                                return TLS_FAIL;
                }
                else if ((id == 0x0000 &&
                          (byte_reader_left(&data) || !tls->named)) ||
                         ((id == 0x0017 || id == 0x0005) &&
                          byte_reader_left(&data)) ||
                         (id == 0xff01 &&
                          (byte_reader_u8(&data) || !byte_reader_end(&data))))
                        return TLS_FAIL;
        }

        if (seen & 1)
        {
                /* TLS 1.3: supported_versions, a key share or a retry's
                   request, and nothing of TLS 1.2's. */
                if (session || suite != 0x1301 || seen >> 3)
                        return TLS_FAIL;
                if (retry)
                        return seen & 6 ? TLS_RETRY : TLS_FAIL;
                return seen & 2 ? TLS_OK : TLS_FAIL;
        }
        if (retry || (suite != 0xc02b && suite != 0xc02f) || seen & 6 ||
            (seen & 0x48) != 0x48 ||
            (!memory_compare(hello_random + 24, "DOWNGRD", 7) &&
             hello_random[31] <= 1))
                return TLS_FAIL;
        tls->tls12 = true;
        tls->suite = (p16)suite;
        tls->status_echoed = (seen >> 7 & 1) != 0;
        return TLS_OK;
}

/* The handshake protocol is a byte stream layered over records: the next
   whole message at the front of hs, receiving records until one is there.
   *length is its size, and the call after drops it first; bytes behind it
   stay for the next call, and the caller, which knows where the keys
   change, refuses any left at a change (RFC 8446 5.1). An empty record is
   refused, as RFC 8446 5.1 forbids sending one, and so is the one
   compatibility change_cipher_spec past its first. */
static COLD bipolar tls_handshake_next(tls_conn address_to tls, p8 address_to hs,
                                       positive address_to used,
                                       positive address_to length,
                                       bool address_to seen_ccs,
                                       const network_deadline address_to deadline)
{
        p8 type = 0;
        p8 address_to record = null;
        positive record_length = 0;

        memory_copy(hs, hs + *length, *used - *length);
        *used -= *length;
        *length = 0;
        for (;;)
        {
                if (*used >= 4)
                {
                        positive body = tls_load_24(hs + 1);

                        if (body > TLS_HS_MAX - 4)
                                return TLS_FAIL;
                        if (*used - 4 >= body)
                        {
                                *length = 4 + body;
                                return TLS_OK;
                        }
                }
                if (tls_next_record(tls, address_of type, address_of record,
                                    address_of record_length, deadline))
                        return TLS_FAIL;
                if (type == TLS_CT_CCS)
                {
                        if (!tls_compatibility_ccs_take(seen_ccs))
                                return TLS_FAIL;
                        continue;
                }
                if (type != TLS_CT_HANDSHAKE || !record_length ||
                    record_length > TLS_HS_MAX - *used)
                        return TLS_FAIL;
                memory_copy(hs + *used, record, record_length);
                *used += record_length;
        }
}

/* The handshake secret, its two traffic secrets and their keys: the flight
   after ServerHello is protected under these. tls_connect left both
   sequence numbers at zero. */
static COLD fn tls_install_handshake_keys(tls_conn address_to tls,
                                          p8 address_to shared,
                                          positive shared_length)
{
        crypto_hkdf_extract((p8 address_to)tls_derived_early, 32, shared,
                            shared_length, tls->hs_secret);
        tls_derive_secret(tls->hs_secret, "c hs traffic",
                          address_of tls->transcript, tls->c_hs_traffic);
        tls_derive_secret(tls->hs_secret, "s hs traffic",
                          address_of tls->transcript, tls->s_hs_traffic);
        tls_traffic_keys(tls->c_hs_traffic, address_of tls->c_gcm, tls->c_iv);
        tls_traffic_keys(tls->s_hs_traffic, address_of tls->s_gcm, tls->s_iv);
        tls->encrypted = true;
}

static COLD fn tls_derive_app_keys(tls_conn address_to tls)
{
        p8 zeros[32];
        p8 derived[32];
        p8 master[32];

        memory_fill(zeros, 0, 32);
        tls_expand_label(tls->hs_secret, "derived", tls_empty_sha256, 32,
                         derived, 32);
        crypto_hkdf_extract(derived, 32, zeros, 32, master);
        tls_derive_secret(master, "c ap traffic", address_of tls->transcript,
                          tls->c_ap_traffic);
        tls_derive_secret(master, "s ap traffic", address_of tls->transcript,
                          tls->s_ap_traffic);
        if (tls->alpn_length)
                tls_derive_secret(master, "exp master",
                                  address_of tls->transcript, tls->exporter);

        crypto_forget(derived, sizeof derived);
        crypto_forget(master, sizeof master);
}

static COLD fn tls_use_app_keys(tls_conn address_to tls)
{
        tls_traffic_keys(tls->c_ap_traffic, address_of tls->c_gcm, tls->c_iv);
        tls_traffic_keys(tls->s_ap_traffic, address_of tls->s_gcm, tls->s_iv);
        tls->seq_read = 0;
        tls->seq_write = 0;
        tls->application = true;

        crypto_forget(tls->hs_secret, sizeof tls->hs_secret);
        crypto_forget(tls->c_hs_traffic, sizeof tls->c_hs_traffic);
        crypto_forget(tls->s_hs_traffic, sizeof tls->s_hs_traffic);
        crypto_forget(address_of tls->transcript, sizeof tls->transcript);
}

/* A Finished's verify_data: HMAC over the transcript so far under the
   finished key of one side's handshake traffic secret (RFC 8446 4.4.4). */
static COLD fn tls_finished_mac(tls_conn address_to tls, p8 address_to traffic,
                                p8 address_to out)
{
        p8 key[32];
        p8 hash[32];
        crypto_sha256 copy = tls->transcript;

        tls_expand_label(traffic, "finished", null, 0, key, 32);
        crypto_sha256_close(address_of copy, hash);
        crypto_hmac_sha256(key, 32, hash, 32, out);
        crypto_forget(key, sizeof key);
        crypto_forget(hash, sizeof hash);
        crypto_forget(address_of copy, sizeof copy);
}

/* The TLS 1.2 PRF (RFC 5246 5): P_SHA256(secret, label + seed_a +
   seed_b), one-shot HMACs over a block that holds A(i) with the label and
   seeds behind it. The longest label is "extended master secret" and the
   longest seed the two randoms. */
static COLD fn tls12_prf(p8 address_to secret, positive secret_length,
                         string_address label, const p8 address_to seed_a,
                         positive a_length, const p8 address_to seed_b,
                         positive b_length, p8 address_to out,
                         positive out_length)
{
        p8 block[32 + 22 + 64];
        p8 mac[32];
        positive label_length = string_length(label);
        positive seed_length = label_length + a_length + b_length;

        memory_copy(block + 32, label, label_length);
        memory_copy(block + 32 + label_length, seed_a, a_length);
        memory_copy(block + 32 + label_length + a_length, seed_b, b_length);
        crypto_hmac_sha256(secret, secret_length, block + 32, seed_length,
                           block);
        for (positive at = 0; at < out_length; at += 32)
        {
                crypto_hmac_sha256(secret, secret_length, block,
                                   32 + seed_length, mac);
                memory_copy(out + at, mac, min((positive)32, out_length - at));
                crypto_hmac_sha256(secret, secret_length, block, 32, block);
        }
        crypto_forget(block, sizeof block);
        crypto_forget(mac, sizeof mac);
}

/* A TLS 1.2 Finished's verify_data: the PRF over the transcript's hash. */
static COLD fn tls12_verify_data(tls_conn address_to tls, string_address label,
                                 p8 address_to out)
{
        p8 hash[32];
        crypto_sha256 copy = tls->transcript;

        crypto_sha256_close(address_of copy, hash);
        tls12_prf(tls->master, 48, label, hash, 32, hash, 0, out, 12);
        crypto_forget(address_of copy, sizeof copy);
}

static COLD bipolar tls_check_finished(tls_conn address_to tls, p8 address_to verify,
                                  positive length)
{
        p8 expect[32];
        positive size = tls->tls12 ? 12 : 32;
        bool same;

        if (length != size)
                return TLS_FAIL;
        if (tls->tls12)
                tls12_verify_data(tls, "server finished", expect);
        else
                tls_finished_mac(tls, tls->s_hs_traffic, expect);
        same = crypto_same(expect, verify, size);
        crypto_forget(expect, sizeof expect);
        return same ? TLS_OK : TLS_FAIL;
}

/* Nothing reads the transcript after the client's Finished, so it is sent
   without being added. A requested certificate goes first, empty, as
   OpenSSL's and curl's clients answer when they hold none. The flight is
   sealed into out and leaves in one send. */
static COLD bipolar tls_send_finished(tls_conn address_to tls,
                                      p8 address_to out,
                                      const network_deadline address_to deadline)
{
        p8 msg[36] = {TLS_HS_FINISHED, 0, 0, 32};
        p8 none[8] = {TLS_HS_CERTIFICATE, 0, 0, 4};
        positive at = 0;

        if (tls->cert_requested)
        {
                at = tls_seal(tls, TLS_CT_HANDSHAKE, none, sizeof none, out);
                tls_transcript_add(tls, none, sizeof none);
        }
        tls_finished_mac(tls, tls->c_hs_traffic, msg + 4);
        at += tls_seal(tls, TLS_CT_HANDSHAKE, msg, sizeof msg, out + at);
        crypto_forget(msg, sizeof msg);
        return network_stream_send_all_until(tls->handle, out, at, deadline)
                   ? TLS_OK : TLS_FAIL;
}

/* A signature by the leaf's key over message under a scheme the
   ClientHello offered: ECDSA with SHA-256 or SHA-384, RSA-PSS with
   SHA-256, and in TLS 1.2 RSA PKCS#1 v1.5 with SHA-256 or SHA-384. TLS 1.3
   binds an ECDSA scheme to its curve and keeps PKCS#1 v1.5 to certificates
   (RFC 8446 4.2.3); TLS 1.2's schemes name only the hash. */
static COLD bool tls_signature_valid(tls_conn address_to tls, positive scheme,
                                     p8 address_to message,
                                     positive message_length,
                                     p8 address_to sig, positive sig_length)
{
        p8 hash[48];
        p8 r[48];
        p8 s[48];
        positive r_length = 0;
        positive s_length = 0;
        positive hash_length = scheme >> 8 == 5 ? 48 : 32;
        p8 curve = tls->leaf_curve;

        if (scheme == 0x0804)
                return curve == 3 && tls->leaf_n_length &&
                       crypto_rsa_pss_sha256(tls->leaf_n, tls->leaf_n_length,
                                             tls->leaf_e, sig, sig_length,
                                             message, message_length);
        if (scheme != 0x0403 && scheme != 0x0503 &&
            (!tls->tls12 || (scheme != 0x0401 && scheme != 0x0501)))
                return false;
        if (hash_length == 48)
                crypto_sha384(message, message_length, hash);
        else
                crypto_sha256_of(message, message_length, hash);
        if ((scheme & 0xff) == 1)
        {
                /* DigestInfo: AlgorithmIdentifier {id-sha256 or -384, NULL}
                   and the OCTET STRING hash. */
                const p8 info[19] = {
                    0x30, (p8)(17 + hash_length), 0x30, 0x0d, 0x06, 0x09,
                    0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x02,
                    (p8)(hash_length / 16 - 1), 0x05, 0x00, 0x04,
                    (p8)hash_length};

                return curve == 3 && tls->leaf_n_length &&
                       crypto_rsa_pkcs1(tls->leaf_n, tls->leaf_n_length,
                                        tls->leaf_e, sig, sig_length, info,
                                        sizeof info, hash, hash_length);
        }
        if ((curve != 1 && curve != 2) ||
            (!tls->tls12 && curve != hash_length / 16 - 1) ||
            tls_parse_ecdsa_sig(sig, sig_length, r, address_of r_length, s,
                                address_of s_length))
                return false;
        return curve == 1 ? crypto_ecdsa_p256(hash, hash_length, r, r_length,
                                              s, s_length, tls->leaf_qx + 16,
                                              tls->leaf_qy + 16)
                          : crypto_ecdsa_p384(hash, hash_length, r, r_length,
                                              s, s_length, tls->leaf_qx,
                                              tls->leaf_qy);
}

/* TLS 1.3's CertificateVerify signs 64 spaces, its context string with the
   NUL, and the transcript's hash (RFC 8446 4.4.3). */
static COLD bipolar tls_check_cert_verify(tls_conn address_to tls, p8 address_to msg,
                                     positive length)
{
        static const p8 context[] = "TLS 1.3, server CertificateVerify";
        p8 signed_bytes[64 + sizeof context + 32];
        crypto_sha256 copy = tls->transcript;
        byte_reader reader = byte_reader_open(msg, length);
        positive scheme;
        byte_reader signature;

        //      The message's own four-byte header, the scheme, the signature.
        (void)byte_reader_skip(&reader, 4);
        scheme = byte_reader_u16(&reader);
        signature = byte_reader_vector16(&reader);
        if (!byte_reader_end(&reader))
                return TLS_FAIL;
        memory_fill(signed_bytes, 0x20, 64);
        memory_copy(signed_bytes + 64, context, sizeof context);
        crypto_sha256_close(address_of copy, signed_bytes + 64 + sizeof context);
        return tls_signature_valid(tls, scheme, signed_bytes,
                                   sizeof signed_bytes,
                                   (p8 address_to)byte_reader_here(&signature),
                                   byte_reader_left(&signature))
                   ? TLS_OK
                   : TLS_FAIL;
}

#define TLS_SERVER_FLIGHT_EE 0
#define TLS_SERVER_FLIGHT_REQUEST 1
#define TLS_SERVER_FLIGHT_CERTIFICATE 2
#define TLS_SERVER_FLIGHT_CERT_VERIFY 3
#define TLS_SERVER_FLIGHT_FINISHED 4
#define TLS_SERVER_FLIGHT_COMPLETE 5
#define TLS12_FLIGHT_CERTIFICATE 6
#define TLS12_FLIGHT_FINISHED 11
#define TLS12_FLIGHT_COMPLETE 12

/* This client offers no PSK and resumes nothing, so each version's server
   flight has one legal shape, in which only a CertificateRequest is
   optional, and in TLS 1.2 a CertificateStatus after the Certificate (which
   tls_flight_message takes only when the ServerHello echoed
   status_request): TLS 1.3's from EncryptedExtensions, TLS 1.2's from
   Certificate to ServerHelloDone and, after the client's flight, Finished. A
   zero ends
   each, so neither version's walk reaches the other's messages. Keeping
   the shapes in one transition function prevents a duplicate message from
   overwriting parsed certificate state or an early Finished from
   authenticating an incomplete transcript. An optional step (the high bit)
   is passed over when the message is the next one instead; a refused
   message leaves the state. */
static COLD bool tls_server_flight_step(p8 address_to state, p8 type)
{
        static const p8 expected[] = {
            TLS_HS_ENCRYPTED_EXTS,
            TLS_HS_CERT_REQUEST | 0x80,
            TLS_HS_CERTIFICATE,
            TLS_HS_CERT_VERIFY,
            TLS_HS_FINISHED,
            0,
            TLS_HS_CERTIFICATE,
            TLS_HS_CERTIFICATE_STATUS | 0x80,
            TLS_HS_SERVER_KEY_EXCHANGE,
            TLS_HS_CERT_REQUEST | 0x80,
            TLS_HS_SERVER_HELLO_DONE,
            TLS_HS_FINISHED,
            0,
        };

        for (p8 at = *state; at < sizeof expected && expected[at]; at++)
        {
                if (type == (expected[at] & 0x7f))
                {
                        *state = at + 1;
                        return true;
                }
                if (!(expected[at] & 0x80))
                        break;
        }
        return false;
}

/* RFC 8446 forbids duplicate extensions.  Rescanning every earlier extension
   to find one is quadratic in a block whose size the peer chooses: sixteen
   kilobytes of empty extensions is eight million inner steps, and
   EncryptedExtensions arrives before the certificate is checked, so anyone
   who can terminate the key exchange can spend it.  The kinds already seen go
   in a bitmap of the sixteen-bit space instead and the walk is one pass.  The
   nested scan's own framing tests were unreachable -- it only ever visited
   offsets this walk had already validated and placed -- so the predicate is
   the same one. As answers, in EncryptedExtensions, only what the
   ClientHello asked for may come back (RFC 8446 4.2): of what this client
   sends, supported_groups, and server_name when the host was a name. allowed
   has a bit for each kind below 64 that may appear; a ticket's extensions
   pass positive_max, which allows every kind. */
static COLD bool tls_encrypted_extensions_valid(p8 address_to body,
                                           positive length, positive allowed)
{
        p8 seen[8192];
        byte_reader reader = byte_reader_open(body, length);
        byte_reader list = byte_reader_vector16(&reader);

        if (!byte_reader_end(&reader))
                return false;

        memory_fill(seen, 0, sizeof seen);

        while (byte_reader_left(&list))
        {
                p16 kind = byte_reader_u16(&list);

                (void)byte_reader_vector16(&list);
                if (!byte_reader_ok(&list))
                        return false;

                if ((allowed != positive_max &&
                     (kind > 63 || !(allowed >> kind & 1))) ||
                    seen[kind >> 3] & (p8)(1u << (kind & 7)))
                        return false;
                seen[kind >> 3] |= (p8)(1u << (kind & 7));
        }

        return true;
}

/* What EncryptedExtensions says about the application protocol (RFC 7301
   3.1): when one was offered, exactly that one, named once. */
static COLD bool tls_alpn_named(const tls_conn address_to tls,
                                p8 address_to body, positive length)
{
        byte_reader reader = byte_reader_open(body, length);
        byte_reader list = byte_reader_vector16(&reader);

        while (byte_reader_left(&list))
        {
                p16 kind = byte_reader_u16(&list);
                byte_reader value = byte_reader_vector16(&list);

                if (!byte_reader_ok(&list))
                        return false;
                if (kind == 0x0010)
                {
                        byte_reader names = byte_reader_vector16(&value);
                        byte_reader name = byte_reader_vector8(&names);

                        return byte_reader_end(&value) && byte_reader_end(&names) &&
                               byte_reader_ok(&name) &&
                               byte_reader_left(&name) == tls->alpn_length &&
                               !memory_compare(byte_reader_here(&name),
                                               tls->alpn, tls->alpn_length);
                }
        }
        return false;
}

static COLD bool tls_new_session_ticket_valid(p8 address_to body,
                                         positive length)
{
        byte_reader reader = byte_reader_open(body, length);
        byte_reader ticket;

        //      The lifetime and the age's addend, the nonce, the ticket.
        (void)byte_reader_skip(&reader, 8);
        (void)byte_reader_vector8(&reader);
        ticket = byte_reader_vector16(&reader);
        if (!byte_reader_ok(&reader) || !byte_reader_left(&ticket))
                return false;

        return tls_encrypted_extensions_valid(
            (p8 address_to)byte_reader_here(&reader),
            byte_reader_left(&reader), positive_max);
}

/* KeyUpdate (RFC 8446 4.6.3, 7.2): the next traffic secret replaces the
   one it came from, and the direction's key, IV and sequence start over. */
static COLD fn tls_update_traffic(p8 address_to secret,
                                  crypto_aesgcm_key address_to gcm,
                                  p8 address_to iv, p64 address_to seq)
{
        p8 next[32];

        tls_expand_label(secret, "traffic upd", null, 0, next, 32);
        memory_copy(secret, next, 32);
        crypto_forget(next, sizeof next);
        tls_traffic_keys(secret, gcm, iv);
        address_to seq = 0;
}

/* Send a KeyUpdate under the current write key, then write under the
   next. Asking the peer to update too is how the read side is rekeyed. */
static COLD bipolar tls_key_update_until(
    tls_conn address_to tls, bool ask,
    const network_deadline address_to deadline)
{
        p8 message[5] = {TLS_HS_KEY_UPDATE, 0, 0, 1, (p8)ask};

        if (tls_send_enc_until(tls, TLS_CT_HANDSHAKE, message, sizeof message,
                               deadline))
                return TLS_FAIL;
        tls_update_traffic(tls->c_ap_traffic, address_of tls->c_gcm, tls->c_iv,
                           address_of tls->seq_write);
        tls->update_asked |= ask;
        return TLS_OK;
}

/* This client does not resume sessions, but servers commonly send tickets:
   complete, well-framed NewSessionTickets are ignored. A KeyUpdate rekeys
   the read side and, when the peer asks, answers with one of its own; it
   has to end its record, since the key changes after it (RFC 8446 5.1),
   and its one byte is 0 or 1. Every other post-handshake message fails
   rather than leaving keys or authentication silently stale. */
static COLD bipolar tls_post_handshake_append_until(
    tls_conn address_to tls, p8 address_to fragment, positive length,
    const network_deadline address_to deadline)
{
        p8 address_to held = tls->post_handshake;
        positive address_to held_length = address_of tls->post_handshake_used;
        positive at = 0;

        if (*held_length > TLS_HS_MAX || !length ||
            length > TLS_HS_MAX - *held_length)
                return TLS_FAIL;

        memory_copy(held + *held_length, fragment, length);
        *held_length += length;

        while (at < *held_length)
        {
                positive body_length;

                if (*held_length - at < 4)
                        break;
                body_length = tls_load_24(held + at + 1);
                if (held[at] == TLS_HS_KEY_UPDATE ? body_length != 1
                    : held[at] != TLS_HS_NEW_SESSION_TICKET ||
                          body_length > TLS_HS_MAX - 4)
                        return TLS_FAIL;

                if (body_length > *held_length - at - 4)
                        break;
                if (held[at] == TLS_HS_KEY_UPDATE)
                {
                        if (held[at + 4] > 1 || at + 5 != *held_length)
                                return TLS_FAIL;
                        tls_update_traffic(tls->s_ap_traffic,
                                           address_of tls->s_gcm, tls->s_iv,
                                           address_of tls->seq_read);
                        tls->update_asked = false;
                        if (held[at + 4] &&
                            tls_key_update_until(tls, false, deadline))
                                return TLS_FAIL;
                }
                else if (!tls_new_session_ticket_valid(held + at + 4,
                                                       body_length))
                        return TLS_FAIL;
                at += 4 + body_length;
        }

        if (at)
        {
                positive old_length = *held_length;

                memory_copy(held, held + at, *held_length - at);
                *held_length -= at;
                crypto_forget(held + *held_length,
                              old_length - *held_length);
        }

        return TLS_OK;
}

/* The relaid list in into through the one chain check, and then the leaf's
   key has to be the kind the suite signs with. */
static COLD bool tls12_chain(tls_conn address_to tls, p8 address_to into)
{
        tls->chain_pending = false;
        return tls_verify_chain(into, 4 + tls_load_24(into + 1), tls->host, tls) &&
               (tls->leaf_curve == 3) == (tls->suite == 0xc02f);
}

/* A TLS 1.2 Certificate is the list alone, each entry without
   extensions. Laid out again as TLS 1.3's -- an empty context, two empty
   extension bytes after each certificate -- in into, which has room for
   5 + TLS_PLAINTEXT_MAX bytes, it goes through the one chain check
   (tls12_chain). When the server echoed status_request the check waits for
   the message after: a CertificateStatus (tls12_certificate_status), or
   ServerKeyExchange with no staple. */
static COLD bool tls12_certificate(tls_conn address_to tls, p8 address_to body,
                                   positive length, p8 address_to into)
{
        byte_reader reader = byte_reader_open(body, length);
        byte_reader list = byte_reader_vector24(&reader);
        byte_store out = {into, 5 + TLS_PLAINTEXT_MAX, 4};

        if (!byte_reader_end(&reader))
                return false;
        into[0] = 0;
        while (byte_reader_left(&list))
        {
                byte_reader cert = byte_reader_vector24(&list);
                positive size = byte_reader_left(&cert);
                p8 head[3] = {(p8)(size >> 16), (p8)(size >> 8), (p8)size};
                p8 zero[2] = {0, 0};

                //      Each entry as TLS 1.3 sends it: its length, the
                //      certificate, and no extensions.
                if (!byte_reader_ok(&list) ||
                    !byte_store_append_exact(address_of out, head, 3) ||
                    !byte_store_append_exact(address_of out,
                                             (p8 address_to)byte_reader_here(
                                                 &cert), size) ||
                    !byte_store_append_exact(address_of out, zero, 2))
                        return false;
        }
        into[1] = (p8)((out.used - 4) >> 16);
        network_store_16(into + 2, (p16)(out.used - 4));
        if (tls->status_echoed)
        {
                tls->chain_pending = true;
                return true;
        }
        return tls12_chain(tls, into);
}

/* A TLS 1.2 CertificateStatus (RFC 6066 8), after the Certificate of a
   server that echoed status_request: status_type ocsp and a response of at
   least a byte, the whole body. The response goes into the relaid list as
   the leaf's CertificateEntry status_request extension, the way TLS 1.3
   sends it, and the chain is checked with it there. */
static COLD bool tls12_certificate_status(tls_conn address_to tls,
                                          p8 address_to body, positive length,
                                          p8 address_to into)
{
        byte_reader reader = byte_reader_open(body, length);
        byte_reader answer;
        positive used;
        positive leaf;
        positive size;
        positive extension;

        if (!tls->chain_pending || byte_reader_u8(&reader) != 1)
                return false;
        answer = byte_reader_vector24(&reader);
        size = byte_reader_left(&answer);
        used = 4 + tls_load_24(into + 1);
        //      The leaf's two extension length bytes, after its certificate;
        //      a list with no leaf has none to fill.
        leaf = used >= 9 ? 7 + tls_load_24(into + 4) : used;
        extension = 4 + 1 + 3 + size;
        if (!byte_reader_end(&reader) || !size || extension > 0xffff ||
            leaf + 2 > used || extension > 5 + TLS_PLAINTEXT_MAX - used)
                return false;
        memory_copy(into + leaf + 2 + extension, into + leaf + 2, used - leaf - 2);
        network_store_16(into + leaf, (p16)extension);
        network_store_16(into + leaf + 2, 5);
        network_store_16(into + leaf + 4, (p16)(extension - 4));
        into[leaf + 6] = 1;
        into[leaf + 7] = (p8)(size >> 16);
        network_store_16(into + leaf + 8, (p16)size);
        memory_copy(into + leaf + 10, byte_reader_here(&answer), size);
        used += extension;
        into[1] = (p8)((used - 4) >> 16);
        network_store_16(into + 2, (p16)(used - 4));
        return tls12_chain(tls, into);
}

/* ServerKeyExchange (RFC 8422 5.4): a named curve this client offered and a
   point at that curve's length, signed by the leaf with both randoms in
   front under a scheme offered. The client's share on that curve is drawn
   now: the premaster goes to master and the point to share, for the
   client's flight, and the scalar is spent. */
static COLD bool tls12_key_exchange(tls_conn address_to tls, p8 address_to body,
                                    positive length)
{
        p8 signed_bytes[64 + 4 + 97];
        byte_reader reader = byte_reader_open(body, length);
        byte_reader parameters = reader;
        positive params;
        positive group;
        byte_reader point;
        positive scheme;
        byte_reader signature;
        bool agreed;

        if (byte_reader_u8(&reader) != 3)
                return false;
        group = byte_reader_u16(&reader);
        point = byte_reader_vector8(&reader);
        params = byte_reader_left(&parameters) - byte_reader_left(&reader);
        scheme = byte_reader_u16(&reader);
        signature = byte_reader_vector16(&reader);
        if (!byte_reader_end(&reader) ||
            byte_reader_left(&point) != tls_share_length(group) ||
            !byte_reader_left(&point))
                return false;
        memory_copy(signed_bytes, tls->client_random, 32);
        memory_copy(signed_bytes + 32, tls->server_random, 32);
        memory_copy(signed_bytes + 64, (p8 address_to)byte_reader_here(&parameters),
                    params);
        if (!tls_signature_valid(tls, scheme, signed_bytes, 64 + params,
                                 (p8 address_to)byte_reader_here(&signature),
                                 byte_reader_left(&signature)))
                return false;
        agreed = tls_share_scalar(tls, group) &&
                 tls_share_use(tls, (p8 address_to)byte_reader_here(&point),
                               tls->master) &&
                 tls_share_use(tls, null, tls->share);
        crypto_forget(tls->scalar, sizeof tls->scalar);
        return agreed;
}

/* A TLS 1.2 CertificateRequest (RFC 5246 7.4.4): certificate types, then
   signature algorithms, then authorities, each vector whole. */
static COLD bool tls12_certificate_request(p8 address_to body, positive length)
{
        byte_reader reader = byte_reader_open(body, length);
        byte_reader types = byte_reader_vector8(&reader);

        (void)byte_reader_vector16(&reader);
        (void)byte_reader_vector16(&reader);
        return byte_reader_left(&types) && byte_reader_end(&reader);
}

/* One message of the server's flight, framed by tls_handshake_next and
   placed by tls_server_flight_step: checked, then added to the transcript,
   so a CertificateVerify and a Finished are checked against what came
   before them. As answers, in EncryptedExtensions, only what the
   ClientHello asked for may come back (RFC 8446 4.2): supported_groups,
   and server_name when the host was a name. A TLS 1.3 CertificateRequest
   has an empty context during the handshake (RFC 8446 4.3.2) and
   well-framed extensions; with no certificate to offer, the answer to
   either version's is an empty Certificate and the server decides.
   scratch has room for 5 + TLS_PLAINTEXT_MAX bytes. */
static COLD bipolar tls_flight_message(tls_conn address_to tls,
                                       p8 address_to msg, positive length,
                                       p8 address_to scratch)
{
        p8 address_to body = msg + 4;
        positive body_length = length - 4;
        byte_reader request = byte_reader_open(body, body_length);
        bool valid;

        if (msg[0] == TLS_HS_ENCRYPTED_EXTS)
                valid = tls_encrypted_extensions_valid(
                            body, body_length,
                            (positive)1 << 0x000a | tls->named |
                                (tls->alpn_length ? (positive)1 << 0x0010 : 0)) &&
                        (!tls->alpn_length ||
                         tls_alpn_named(tls, body, body_length));
        else if (msg[0] == TLS_HS_CERT_REQUEST)
                //      TLS 1.3's has an empty context, then extensions.
                valid = tls->cert_requested =
                    tls->tls12 ? tls12_certificate_request(body, body_length)
                               : !byte_reader_u8(&request) &&
                                     byte_reader_ok(&request) &&
                                     tls_encrypted_extensions_valid(
                                         (p8 address_to)byte_reader_here(
                                             &request),
                                         byte_reader_left(&request),
                                         positive_max);
        else if (msg[0] == TLS_HS_CERTIFICATE)
        {
                valid = tls->tls12 ? tls12_certificate(tls, body, body_length,
                                                       scratch)
                                   : tls_verify_chain(body, body_length,
                                                      tls->host, tls);
                tls->untrusted = tls->check_cert && !valid;
        }
        //      A TLS 1.2 check that waited for a staple runs at the
        //      CertificateStatus, or with none at ServerKeyExchange; a
        //      CertificateStatus nothing waited for is refused.
        else if (msg[0] == TLS_HS_CERTIFICATE_STATUS)
        {
                bool waited = tls->chain_pending;

                valid = tls12_certificate_status(tls, body, body_length, scratch);
                tls->untrusted = tls->check_cert && !valid && waited &&
                                 !tls->chain_pending;
        }
        else if (msg[0] == TLS_HS_SERVER_KEY_EXCHANGE)
        {
                valid = true;
                if (tls->chain_pending)
                {
                        valid = tls12_chain(tls, scratch);
                        tls->untrusted = tls->check_cert && !valid;
                }
                valid = valid && tls12_key_exchange(tls, body, body_length);
        }
        else if (msg[0] == TLS_HS_SERVER_HELLO_DONE)
                valid = !body_length;
        else if (msg[0] == TLS_HS_CERT_VERIFY)
                valid = !tls_check_cert_verify(tls, msg, length);
        else
                valid = !tls_check_finished(tls, body, body_length);
        if (!valid)
                return TLS_FAIL;
        tls_transcript_add(tls, msg, length);
        return TLS_OK;
}

/* The client's TLS 1.2 flight, sealed into out and sent at once: an empty
   Certificate when one was requested and ClientKeyExchange in one
   plaintext record, the change_cipher_spec, and Finished under the new
   keys. The master secret is the extended one (RFC 7627), taken over the
   transcript through ClientKeyExchange, so it is bound to this handshake;
   the key block (RFC 5288 3) is the two keys and the two four-byte salts,
   and each IV is its salt before eight zero bytes that tls_nonce fills with
   the sequence. */
static COLD bipolar tls12_client_flight(tls_conn address_to tls,
                                        p8 address_to out,
                                        const network_deadline address_to deadline)
{
        static const p8 no_certificate[7] = {TLS_HS_CERTIFICATE, 0, 0, 3};
        static const p8 change[6] = {TLS_CT_CCS, 3, 3, 0, 1, 1};
        p8 hash[32];
        p8 keys[48];
        p8 finished[16] = {TLS_HS_FINISHED, 0, 0, 12};
        positive point = tls_share_length(tls->group);
        positive at = 5;
        crypto_sha256 copy;
        positive sealed;

        if (tls->cert_requested)
        {
                memory_copy(out + at, no_certificate, sizeof no_certificate);
                at += sizeof no_certificate;
        }
        out[at] = TLS_HS_CLIENT_KEY_EXCHANGE;
        out[at + 1] = 0;
        network_store_16(out + at + 2, (p16)(1 + point));
        out[at + 4] = (p8)point;
        memory_copy(out + at + 5, tls->share, point);
        at += 5 + point;
        tls_record_header(out, TLS_CT_HANDSHAKE, at - 5);
        tls_transcript_add(tls, out + 5, at - 5);
        memory_copy(out + at, change, sizeof change);
        at += sizeof change;

        copy = tls->transcript;
        crypto_sha256_close(address_of copy, hash);
        tls12_prf(tls->master, tls->group == 0x0018 ? 48 : 32,
                  "extended master secret", hash, 32, hash, 0, keys, 48);
        memory_copy(tls->master, keys, 48);
        tls12_prf(tls->master, 48, "key expansion", tls->server_random, 32,
                  tls->client_random, 32, keys, 40);
        crypto_aesgcm_prepare(address_of tls->c_gcm, keys);
        crypto_aesgcm_prepare(address_of tls->s_gcm, keys + 16);
        memory_fill(tls->c_iv, 0, 12);
        memory_fill(tls->s_iv, 0, 12);
        memory_copy(tls->c_iv, keys + 32, 4);
        memory_copy(tls->s_iv, keys + 36, 4);
        crypto_forget(keys, sizeof keys);
        crypto_forget(address_of copy, sizeof copy);

        tls12_verify_data(tls, "client finished", finished + 4);
        tls_transcript_add(tls, finished, sizeof finished);
        sealed = tls_seal(tls, TLS_CT_HANDSHAKE, finished, sizeof finished,
                          out + at);
        crypto_forget(finished, sizeof finished);
        return sealed && network_stream_send_all_until(
                             tls->handle, out, at + sealed, deadline)
                   ? TLS_OK
                   : TLS_FAIL;
}

/* TLS 1.2 after its ServerHello, whose records the flight may share. The
   server's one change_cipher_spec comes after the client's Finished, with
   no handshake bytes pending, and turns on the read keys; a compatibility
   CCS has no place in TLS 1.2, so any other fails. */
static COLD bipolar tls12_handshake(tls_conn address_to tls, p8 address_to hs,
                                    positive used, positive length,
                                    bool seen_ccs, p8 address_to out,
                                    const network_deadline address_to deadline)
{
        p8 flight = TLS12_FLIGHT_CERTIFICATE;
        p8 type = 0;
        p8 address_to record = null;
        positive record_length = 0;

        if (seen_ccs)
                return TLS_FAIL;
        seen_ccs = true;
        crypto_forget(tls->scalar, sizeof tls->scalar);
        while (flight != TLS12_FLIGHT_FINISHED)
                if (tls_handshake_next(tls, hs, address_of used,
                                       address_of length, address_of seen_ccs,
                                       deadline) ||
                    !tls_server_flight_step(address_of flight, hs[0]) ||
                    tls_flight_message(tls, hs, length, out))
                        return TLS_FAIL;
        if (used != length || tls12_client_flight(tls, out, deadline) ||
            tls_next_record(tls, address_of type, address_of record,
                            address_of record_length, deadline) ||
            type != TLS_CT_CCS)
                return TLS_FAIL;
        tls->encrypted = true;
        if (tls_handshake_next(tls, hs, address_of used, address_of length,
                               address_of seen_ccs, deadline) ||
            !tls_server_flight_step(address_of flight, hs[0]) ||
            tls_flight_message(tls, hs, length, out) || used != length)
                return TLS_FAIL;
        tls->application = true;
        crypto_forget(tls->master, sizeof tls->master);
        crypto_forget(address_of tls->transcript, sizeof tls->transcript);
        return TLS_OK;
}

static COLD bipolar tls_handshake(
    tls_conn address_to tls, const network_deadline address_to deadline)
{
        /* A retry's cookie comes back in the second hello, which has to
           fit one record; the client's last flight is sealed here too. */
        p8 out[5 + TLS_PLAINTEXT_MAX];
        p8 peer[97];
        p8 shared[48];
        p8 hs[TLS_HS_MAX];
        positive out_length = 0;
        positive used = 0;
        positive length = 0;
        positive secret_length;
        p8 flight = TLS_SERVER_FLIGHT_EE;
        bool seen_ccs = false;
        bool retried = false;
        bipolar status = TLS_FAIL;
        bipolar hello_kind;
        positive share_length = 0;
        positive group = 0;
        const p8 address_to cookie = null;
        positive cookie_length = 0;

        tls->deadline = deadline;
        crypto_sha256_open(address_of tls->transcript);

        for (;;)
        {
                if (tls_client_hello(tls, out + 5, sizeof out - 5,
                                     address_of out_length, cookie,
                                     cookie_length))
                        goto done;
                tls_transcript_add(tls, out + 5, out_length);
                tls_record_header(out, TLS_CT_HANDSHAKE, out_length);
                if (!network_stream_send_all_until(tls->handle, out,
                                                    5 + out_length, deadline) ||
                    tls_handshake_next(tls, hs, address_of used,
                                       address_of length, address_of seen_ccs,
                                       deadline))
                        goto done;

                hello_kind = tls_server_hello_keys(
                    hs, length, tls, peer, sizeof peer,
                    address_of share_length, address_of group,
                    address_of cookie, address_of cookie_length);
                if (hello_kind != TLS_RETRY)
                        break;

                /* One retry, answered by the next hello, so nothing may
                   follow it. The transcript restarts as message_hash over
                   the first ClientHello, then the retry and the second
                   hello (RFC 8446 4.4.1). */
                if (retried || used != length ||
                    (group && !tls_share_scalar(tls, group)))
                        goto done;
                retried = true;
                {
                        p8 message_hash[4 + 32] = {254, 0, 0, 32};

                        crypto_sha256_close(address_of tls->transcript,
                                            message_hash + 4);
                        crypto_sha256_open(address_of tls->transcript);
                        tls_transcript_add(tls, message_hash,
                                           sizeof message_hash);
                }
                tls_transcript_add(tls, hs, length);
        }

        /* TLS 1.2 is never the answer to a retry. In TLS 1.3 the keys
           change after ServerHello, so nothing may share its records. */
        if (hello_kind || (retried && tls->tls12) ||
            (tls->tls12 && tls->alpn_length) ||
            (!tls->tls12 && used != length))
                goto done;
        tls_transcript_add(tls, hs, length);
        if (tls->tls12)
        {
                status = tls12_handshake(tls, hs, used, length, seen_ccs, out,
                                         deadline);
                goto done;
        }

        secret_length = tls_share_use(tls, peer, shared);
        crypto_forget(tls->scalar, sizeof tls->scalar);
        if (!secret_length)
                goto done;
        tls_install_handshake_keys(tls, shared, secret_length);
        crypto_forget(shared, sizeof shared);

        while (flight != TLS_SERVER_FLIGHT_COMPLETE)
                if (tls_handshake_next(tls, hs, address_of used,
                                       address_of length, address_of seen_ccs,
                                       deadline) ||
                    !tls_server_flight_step(address_of flight, hs[0]) ||
                    tls_flight_message(tls, hs, length, out))
                        goto done;
        /* Nothing follows Finished under the handshake keys. */
        if (used != length)
                goto done;

        tls_derive_app_keys(tls);
        if (tls_send_finished(tls, out, deadline))
                goto done;
        tls_use_app_keys(tls);
        status = TLS_OK;

done:
        crypto_forget(out, sizeof out);
        crypto_forget(peer, sizeof peer);
        crypto_forget(shared, sizeof shared);
        crypto_forget(hs, sizeof hs);
        crypto_forget(tls->scalar, sizeof tls->scalar);
        tls->deadline = null;
        return status;
}

/* What each TLS_FAULT_* answers, in its number's order: none, EARLY, ... */
static const bipolar tls_fault_answer[8] = {
    TLS_UNTRUSTED, TLS_NOT_YET, TLS_EXPIRED, TLS_MISMATCH,
    TLS_REVOKED, TLS_UNCHECKED, TLS_CLOCK, TLS_UNLOGGED};

/* A connection over the caller's deadline (null when it could not be had),
   and for Network Time Security's key establishment an application
   protocol named in the hello and back (TLS 1.3 only), the exporter secret
   kept, and the certificate judged as of the floor while the clock cannot
   be believed (bootstrap). */
static COLD bipolar tls_connect_until(tls_conn address_to tls, bipolar handle,
                                      string_address host, bool check_cert,
                                      const p8 address_to alpn,
                                      positive alpn_length, bool bootstrap,
                                      const network_deadline address_to deadline)
{
        bipolar status;

        memory_fill(tls, 0, TLS_CONN_HEAD);
        tls->handle = handle;
        tls->host = host;
        tls->check_cert = check_cert;
        tls->alpn = alpn;
        tls->alpn_length = alpn_length;
        tls->clock_bootstrap = bootstrap;
        status = deadline ? tls_handshake(tls, deadline) : TLS_FAIL;
        if (status)
        {
                status = !tls->untrusted ? TLS_FAIL
                         : tls->fault < 8 ? tls_fault_answer[tls->fault]
                                          : TLS_UNTRUSTED;
                tls_forget(tls);
        }
        return status;
}

static COLD bipolar tls_connect(tls_conn address_to tls, bipolar handle,
                           string_address host, bool check_cert)
{
        network_deadline deadline;

        return tls_connect_until(tls, handle, host, check_cert, null, 0, false,
                                 network_deadline_begin(address_of deadline,
                                                        TLS_HANDSHAKE_SECONDS, 0)
                                     ? address_of deadline : null);
}

/* RFC 8446 7.5: TLS-Exporter(label, context, length) from the exporter
   master secret kept at the handshake's end, for a connection that named a
   protocol. The caller's length is at most 32, one HKDF block. */
static COLD bool tls_export(tls_conn address_to tls, string_address label,
                            const p8 address_to context,
                            positive context_length, p8 address_to out,
                            positive length)
{
        p8 derived[32];
        p8 hash[32];

        if (!tls->alpn_length || !tls->application || tls->tls12 ||
            length > 32)
                return false;
        crypto_sha256_of((p8 address_to)context, context_length, hash);
        tls_expand_label(tls->exporter, label, tls_empty_sha256, 32, derived, 32);
        tls_expand_label(derived, "exporter", hash, 32, out, length);
        crypto_forget(derived, sizeof derived);
        return true;
}

static bipolar tls_write(tls_conn address_to tls, p8 address_to data,
                         positive length)
{
        while (length)
        {
                positive take = min(length, (positive)TLS_PLAINTEXT_MAX);

                if ((tls->seq_write >= TLS_KEY_UPDATE_AT && !tls->tls12 &&
                     tls_key_update_until(tls, false, null)) ||
                    tls_send_enc(tls, TLS_CT_APP, data, take))
                        return TLS_FAIL;
                data += take;
                length -= take;
        }

        return TLS_OK;
}

/* Up to room bytes of the next application data, lent rather than copied:
   *span points into the receive buffer and stays valid until a later read on
   this connection receives; *got of zero is close_notify, and stays so. Given
   a deadline, every wait shares it. Given seconds or nanoseconds instead, a
   deadline that long starts the first time a wait or control record needs
   it, so ordinary data already whole in the buffer stays clock-free. Neither
   renews: tickets, empty records and partial records all spend one budget.
   hold never receives: when the next record is not yet whole it answers
   TLS_AGAIN, so a writer can gather every record already here while the
   spans it holds stay put. A peer keeping the socket full of records that
   deliver nothing never lets a read wait, so after each such record the
   budget is asked here as well. */
static bipolar tls_take(tls_conn address_to tls, positive room,
                        p8 address_to address_to span, positive address_to got,
                        const network_deadline address_to deadline,
                        positive seconds, positive nanoseconds, bool hold)
{
        network_deadline patience;
        bool waiting = !seconds && !nanoseconds;
        positive left[2];
        p8 type = 0;
        p8 address_to inner = null;
        positive length = 0;
        bipolar status;

        if (deadline &&
            !network_deadline_left(deadline, left, left + 1))
                return TLS_FAIL;
        if (tls->plain_used)
        {
                positive take = min(room, tls->plain_used);

                address_to span = tls->receive + tls->plain_at;
                tls->plain_at += take;
                tls->plain_used -= take;
                address_to got = take;
                return TLS_OK;
        }

        for (bool spent = false;; spent = true)
        {
                if (tls->closed)
                {
                        address_to got = 0;
                        return tls->post_handshake_used ? TLS_FAIL : TLS_OK;
                }
                if (spent && deadline &&
                    !network_deadline_left(deadline, left, left + 1))
                        return TLS_FAIL;
                if (!tls_record_whole(tls))
                {
                        if (hold)
                                return TLS_AGAIN;
                        if (!waiting)
                        {
                                if (!network_deadline_begin_within(
                                        address_of patience, seconds,
                                        nanoseconds, deadline))
                                        return TLS_FAIL;
                                deadline = address_of patience;
                                waiting = true;
                        }
                }
                status = tls_next_record(tls, address_of type, address_of inner,
                                         address_of length, deadline);
                if (status == TLS_EOF)
                {
                        tls->closed = true;
                        continue;
                }
                if (status)
                        return TLS_FAIL;
                /* A buffered control record may request a write before any
                   receive wait. Start the relative budget before that write,
                   while ordinary buffered application data stays clock-free. */
                if ((type == TLS_CT_HANDSHAKE || !length ||
                     (tls->seq_read >= TLS_KEY_UPDATE_AT && !tls->update_asked &&
                      !tls->tls12)) && (!waiting || (!deadline && hold)))
                {
                        if (!network_deadline_begin_within(address_of patience,
                                waiting ? TLS_WRITE_SECONDS : seconds,
                                waiting ? 0 : nanoseconds, deadline))
                                goto refused;
                        deadline = address_of patience;
                        waiting = true;
                }
                /* TLS 1.2 after its handshake has only renegotiation to
                   say in handshake messages, and that is refused. */
                if (type == TLS_CT_HANDSHAKE)
                {
                        if (tls->tls12 ||
                            tls_post_handshake_append_until(tls, inner, length,
                                                            deadline))
                                goto refused;
                        crypto_forget(inner, length);
                }
                else if (type != TLS_CT_APP || tls->post_handshake_used)
                        goto refused;
                /* Half way to the read key's limit, a TLS 1.3 peer is asked
                   for a KeyUpdate, once; its answer starts the count over.
                   TLS 1.2 has no rekeying without renegotiation, so its keys
                   run to the limit. */
                if (tls->seq_read >= TLS_KEY_UPDATE_AT && !tls->update_asked &&
                    !tls->tls12 && tls_key_update_until(tls, true, deadline))
                        goto refused;
                if (type == TLS_CT_HANDSHAKE || !length)
                        continue;
                if (room > length)
                        room = length;
                address_to span = inner;
                address_to got = room;
                tls->plain_at = (positive)(inner - tls->receive) + room;
                tls->plain_used = length - room;
                return TLS_OK;
        }

refused:
        /* A semantic protocol error, or a failed mandatory response after
           rekeying, is fatal. Buffered later records must never resurrect
           the connection or publish application data under spent state. */
        tls->seq_read = tls->seq_write = TLS_AES_GCM_RECORD_LIMIT;
        return TLS_FAIL;
}

static bipolar tls_borrow_within(
    tls_conn address_to tls, positive room, p8 address_to address_to span,
    positive address_to got, positive seconds, positive nanoseconds,
    const network_deadline address_to deadline)
{
        return tls_take(tls, room, span, got, deadline, seconds, nanoseconds,
                        false);
}

/* The next application data already whole in the receive buffer, lent
   without receiving; TLS_AGAIN when none is. */
static bipolar tls_lend_until(
    tls_conn address_to tls, positive room, p8 address_to address_to span,
    positive address_to got, const network_deadline address_to deadline)
{
        return tls_take(tls, room, span, got, deadline, 0, 0, true);
}

static bipolar tls_read_until(
    tls_conn address_to tls, p8 address_to into, positive room,
    positive address_to got, const network_deadline address_to deadline)
{
        p8 address_to span = null;
        bipolar status = tls_take(tls, room, address_of span, got, deadline,
                                  0, 0, false);

        if (!status && address_to got)
                memory_copy(into, span, address_to got);
        return status;
}

#endif

/*
        Enough HTTP to fetch a file, and no more.

        wget speaks https the way BusyBox does: one GET, redirects, and the
        body streamed to a file. fetch stays the small plaintext tool; it
        still slurps. A body ends three ways. Content-Length is the easy one.
        Chunked is what most servers send when the length is not known in
        advance. Close-delimited is the rest: TLS close_notify ends an
        HTTPS body with no length; a plaintext peer that never closes still
        waits.
*/

#define HTTP_PORT 80
#define HTTP_HTTPS_PORT 443
#define HTTP_URL_MAX 2048
#define HTTP_HEAD_MAX 16384
#define HTTP_FETCH_MAX (16 * 1024 * 1024)
//      GNU wget's twenty redirects, and the request after the last.
#define HTTP_HOPS 21
#define HTTP_IDLE_SECONDS 30
#define HTTP_HEAD_SECONDS 30
#define HTTP_BODY_SECONDS 300
//      STRICT_TIGHT's most for a body that declared no length -- one ended by
//      the close, or chunked -- written as it arrives: 1 GiB. GNU wget takes
//      such a body until the peer stops, which the default does too.
#define HTTP_UNDECLARED_MOST ((positive)1 << 30)
//      STRICT_TIGHT's budget for a whole fetch -- every hop of a redirect
//      chain and the body -- where the default, as GNU wget, times each hop
//      alone: one head wait and one body wait. Zero for none.
#define HTTP_CHAIN_SECONDS                                                   \
        (MOONWATER_STRICT_NET >= STRICT_TIGHT                                \
             ? (positive)(HTTP_HEAD_SECONDS + HTTP_BODY_SECONDS) : 0)

#define HTTP_OK 0
#define HTTP_BAD_URL (-1)
#define HTTP_NO_HOST (-2)
#define HTTP_NO_ROUTE (-3)
#define HTTP_NO_REPLY (-4)
#define HTTP_MALFORMED (-5)
#define HTTP_TLS (-7)
#define HTTP_REDIRECTS (-8)
#define HTTP_DOWNGRADE (-9)
#define HTTP_STATUS (-10)
#define HTTP_WRITE (-11)
#define HTTP_SCHEME (-12)
//      The certificate was refused: any reason, its dates behind the clock
//      (expired), or its dates ahead of it (not yet valid).
#define HTTP_UNTRUSTED (-13)
#define HTTP_EXPIRED (-14)
#define HTTP_NOT_YET (-15)
//      Or that none of its names was the host's.
#define HTTP_MISMATCH (-16)
//      STRICT_TIGHT only: a redirect that leaves public address space for
//      an address that is not public.
#define HTTP_PRIVATE (-17)
//      The certificate is revoked, or whether it is could not be told where
//      the answer is required (TLS_REVOKED, TLS_UNCHECKED).
#define HTTP_REVOKED (-18)
#define HTTP_UNCHECKED (-19)
//      The clock is below its floor: it was never set.
#define HTTP_CLOCK (-20)
//      STRICT_TIGHT only: a name (not an address, not localhost) the
//      resolver answered with this machine, "this network" or the link-local
//      block the clouds' metadata service lives in.
#define HTTP_REBOUND (-21)
//      STRICT_TIGHT only: a body that declared no length ran past
//      HTTP_UNDECLARED_MOST.
#define HTTP_UNDECLARED (-22)
//      STRICT_TIGHT only: a public certificate not logged as Certificate
//      Transparency asks (TLS_UNLOGGED).
#define HTTP_UNLOGGED (-23)
//      The fetch's network half (http_run_apart) could not be confined, so
//      nothing was asked; or it ended without a whole answer -- killed by
//      its filter or a signal, or with an answer the caller could not
//      believe.
#define HTTP_CONFINE (-24)
#define HTTP_APART (-25)

typedef byte_store http_buffer;
#define http_forget(buffer) byte_store_release(buffer)

/* HTTP Host and DNS have narrower syntax than an arbitrary URI reg-name:
   letters, digits and the unreserved '-', '.' and '_' (DNS carries the
   underscore, and wget and curl reach such hosts).  The URL parser and the
   request serializer hold a host to this one rule. */
static bool http_host_byte(p8 byte)
{
        return byte_is_alnum(byte) || byte == '-' || byte == '.' || byte == '_';
}

/* The length of the scheme and its ':' in front of a URL or a reference --
   a letter, then letters, digits, '+', '-' and '.' (RFC 3986 3.1) -- or 0
   when there is none.  http_web_scheme says whether it is http or https, in
   any case: the parser and the redirect resolver read "HTTP://" alike, where
   one taking it for a scheme and the other for a relative path sent a
   redirect somewhere neither the server nor a policy meant. */
static positive http_scheme_length(string_address url)
{
        positive length = 0;

        if (!byte_is_alpha(string_get(url)))
                return 0;
        while (byte_is_alnum(url[length]) || url[length] == '+' ||
               url[length] == '-' || url[length] == '.')
                length++;
        return url[length] == ':' ? length + 1 : 0;
}

static bool http_web_scheme(string_address url, positive scheme)
{
        return (scheme == 5 || scheme == 6) &&
               !memory_compare_ascii_case(url, "https", scheme - 1);
}

/* A URL made fit for a request line the way GNU wget and curl make one: after
   the authority, a space, a backslash and every byte past 0x7f become %XX in
   upper case, and every other byte -- '%' included, so an escape already
   there is never doubled and one that is not an escape (%zz) goes on as curl
   sends it -- goes through as it came.  A server's Location with a space or
   UTF-8 in it, or a URL typed with one, is a real answer that both fetch;
   http_split_into's and the request builder's refusal of those bytes stays
   the backstop for a caller that skips this.  The authority is left exactly
   as written for the parser to judge: url is a whole URL, whose authority
   follows "scheme://" or, when bare_host, starts it; otherwise it is a
   reference, whose only authority follows a leading "//".  url is length
   bytes and a terminator; into holds the answer whole with its own, or this
   is HTTP_BAD_URL. */
static bipolar http_url_escape(string_address url, positive length, bool bare_host,
                               p8 address_to into, positive room)
{
        static const p8 hex[] = "0123456789ABCDEF";
        byte_reader reader = byte_reader_open((const p8 address_to)url, length);
        byte_reader slashes = reader;
        byte_store out = {into, room ? room - 1 : 0, 0};
        positive scheme = http_scheme_length(url);
        positive verbatim = 0;

        if (scheme > length)
                scheme = 0;
        byte_reader_skip(address_of slashes, scheme);
        if ((scheme || !bare_host) && byte_reader_u8(address_of slashes) == '/' &&
            byte_reader_u8(address_of slashes) == '/' && byte_reader_ok(address_of slashes))
                verbatim = scheme + 2;
        if (verbatim || bare_host)
        {
                byte_reader rest = reader;

                byte_reader_skip(address_of rest, verbatim);
                for (;;)
                {
                        byte_reader look = rest;
                        p8 byte = byte_reader_u8(address_of look);

                        if (!byte_reader_ok(address_of look) || byte == '/' ||
                            byte == '?' || byte == '#')
                                break;
                        rest = look;
                        verbatim++;
                }
        }
        if (!room || !byte_store_append_exact(address_of out,
                                               byte_reader_take(address_of reader,
                                                                verbatim),
                                               verbatim))
                return HTTP_BAD_URL;
        while (byte_reader_left(address_of reader))
        {
                p8 byte = byte_reader_u8(address_of reader);
                p8 escape[3] = {'%', hex[byte >> 4], hex[byte & 15]};
                bool plain = byte != ' ' && byte != '\\' && byte <= 0x7f;

                if (!byte_store_append_exact(address_of out, plain ? address_of byte
                                                                   : escape,
                                             plain ? 1 : 3))
                        return HTTP_BAD_URL;
        }
        into[out.used] = end;
        return HTTP_OK;
}

/* inet_aton's spellings of an IPv4 address, which GNU wget (through the C
   library) and curl (its own URL parser) both connect to: one to four parts
   joined by '.', each decimal, 0x hex or 0 octal, every part but the last one
   byte and the last filling the bytes left ("127.1", "0x7f.1", "0177.0.0.1",
   "2130706433").  A trailing '.', an empty part, an 8 or 9 in an octal part,
   a bare "0x" (which glibc reads as 0) or a part past its share is no such
   spelling, and the host stays a name for the resolver.  The address is in
   host order, as http_address_public reads one. */
static bool http_ipv4_spelling(const p8 address_to host, positive length,
                               p32 address_to ip)
{
        byte_reader reader = byte_reader_open(host, length);
        p64 address = 0;
        positive parts = 0;

        for (;;)
        {
                byte_reader look = reader;
                p8 first = byte_reader_u8(address_of look);
                p8 second = byte_reader_u8(address_of look);
                positive base = 10;
                positive digits = 0;
                p64 value = 0;

                if (first == '0' && (second | 0x20) == 'x')
                {
                        base = 16;
                        byte_reader_skip(address_of reader, 2);
                }
                else if (first == '0' && byte_is_digit(second))
                {
                        base = 8;
                        byte_reader_skip(address_of reader, 1);
                }
                for (;; digits++)
                {
                        look = reader;
                        positive digit = digit_known(byte_reader_u8(address_of look),
                                                     base);

                        if (!byte_reader_ok(address_of look) || digit >= base)
                                break;
                        value = value * base + digit;
                        if (value > 0xffffffffu)
                                return false;
                        reader = look;
                }
                if (!digits)
                        return false;
                parts++;
                if (!byte_reader_left(address_of reader))
                {
                        if (value >> (8 * (5 - parts)))
                                return false;
                        address_to ip = (p32)(address << (8 * (5 - parts)) | value);
                        return true;
                }
                if (parts == 4 || value > 255 ||
                    byte_reader_u8(address_of reader) != '.')
                        return false;
                address = address << 8 | value;
        }
}

/*
        http://host[:port][/path] taken apart.

        Everything before the first slash after the authority is the host,
        everything from it is the path, and a missing path is "/". A colon in
        the authority is a port, which is how a test talks to a server on a
        port the kernel picked. A URL the client could not carry through
        http_run -- HTTP_URL_MAX bytes or more -- is refused here too, so no
        caller derives an output name or a message from one.

        A host that inet_aton reads as an address is that address, written
        once here as the dotted quad, so the Host field, TLS's choice of an IP
        identity over a DNS one, the lookup and the address policy all see the
        same four octets: curl does the same (GNU wget connects to the address
        and sends the spelling as written in Host). STRICT_TIGHT refuses every
        such spelling but the dotted quad itself, as the ambiguity it is: a
        filter that reads "0x7f.1" as a name is not the parser that connects.
*/
static bipolar http_split_into(string_address url, p8 address_to host, positive room,
                               p16 address_to port, string_address address_to path,
                               bool address_to tls)
{
        string_address at = url;
        positive length;
        positive scheme = http_scheme_length(url);
        bool selected_tls = false;
        string_address scan = url;

        for (; *scan; scan++)
                if (byte_is_control(*scan) || *scan == ' ' || *scan == '\\')
                        return HTTP_BAD_URL;
        if ((positive)(scan - url) >= HTTP_URL_MAX)
                return HTTP_BAD_URL;

        /* A spelling with an explicit web scheme is never a schemeless
           host:port.  In particular, "https:443" used to mean plaintext to
           the DNS name "https" here while an RFC 3986 parser and a policy
           layer read the same bytes as an HTTPS URI.  Keep the convenient
           schemeless h:81 form, but reserve http: and https: in every case;
           after either one only an authority introduced by "//" is a URL
           this client implements.  The tight tier applies the URI rule to
           every scheme token, removing the general `ftp:21`/host:port
           ambiguity; the default retains non-web shorthand compatibility.
           The same strict scheme rule already applies to redirects. Other
           explicit schemes with an authority are always refused. */
        if (scheme &&
            (MOONWATER_STRICT_NET >= STRICT_TIGHT || http_web_scheme(url, scheme)) &&
            (url[scheme] != '/' || url[scheme + 1] != '/'))
                return HTTP_SCHEME;
        if (scheme && url[scheme] == '/' && url[scheme + 1] == '/')
        {
                if (!http_web_scheme(url, scheme))
                        return HTTP_SCHEME;
                selected_tls = scheme == 6;
                at = url + scheme + 2;
        }
        p16 selected_port = selected_tls ? HTTP_HTTPS_PORT : HTTP_PORT;

        /* This client implements no userinfo.  Silently discarding it makes
           logs and allow-list checks easy to read as the text before '@'
           while the connection goes to the text after it, the classic URL
           authority confusion used in SSRF chains. */
        length = string_span_without_set(at, "/?#");
        if (memory_first_of(at, '@', length))
                return HTTP_BAD_URL;

        length = string_span_without_set(at, "/:?#");
        if (length + 1 >= room)
                return HTTP_BAD_URL;

        /* Validate before touching the caller's output so a rejected URL
           cannot leave a plausible partial destination there. */
        for (positive byte = 0; byte < length; byte++)
                if (!http_host_byte(at[byte]))
                        return HTTP_BAD_URL;

        if (!length)
                return HTTP_BAD_URL;

        string_address host_at = at;
        positive host_length = length;
        p8 quad[16];
        p32 numeric;

        if (http_ipv4_spelling(host_at, length, address_of numeric))
        {
                positive quad_length = host_into(quad, numeric);

                if (quad_length != length ||
                    memory_compare(quad, host_at, length))
                {
                        if (MOONWATER_STRICT_NET >= STRICT_TIGHT ||
                            quad_length + 1 >= room)
                                return HTTP_BAD_URL;
                        host_at = quad;
                        host_length = quad_length;
                }
        }
        at += length;

        if (string_is(at, ':'))
        {
                string_address digits;
                positive bound;
                positive value;

                at++;
                bound = string_span_without_set(at, "/?#");
                digits = at;

                if (!bound ||
                    !string_digits_checked(address_of digits, 10,
                                           address_of value) ||
                    (positive)(digits - at) != bound || !value || value > 65535)
                        return HTTP_BAD_URL;

                selected_port = (p16)value;
                at = digits;
        }

        /* Publish every out-parameter together.  Failed parsing is a
           transaction, not a half-written host paired with a default port;
           callers which log or retry after an error cannot accidentally use
           pieces of a rejected authority. */
        memory_copy(host, host_at, host_length);
        host[host_length] = end;
        address_to port = selected_port;
        address_to tls = selected_tls;
        address_to path = string_get(at) && !string_is(at, '#')
                              ? at : (string_address) "/";

        return HTTP_OK;
}

/* Turn the path/query part of a URL into HTTP's origin form.  Fragments are
   local navigation state and must never cross the request boundary; a bare
   query still needs the root slash on the wire. */
static bipolar http_origin_form(string_address path, p8 address_to into,
                                positive room)
{
        string_address hash;
        positive length;
        bool root;

        if (!path)
                return HTTP_BAD_URL;

        hash = string_first_of(path, '#');
        length = hash ? (positive)(hash - path) : string_length(path);
        root = !length || path[0] == '?';
        if ((path[0] != '/' && !root) || length > room ||
            root >= room - length)
                return HTTP_BAD_URL;

        if (root)
                *into++ = '/';
        memory_copy_end(into, path, length);
        return HTTP_OK;
}

//      Where the header ends: the blank line, in either spelling.
static PURE bipolar http_header_end(p8 address_to bytes, positive size)
{
        //      The blank line is an LF, then a bare LF or a CRLF.  The field
        //      walk drops the CR of a blank line as it does of any line, so
        //      "Name: v\n\r\n" ends the head there too, and the finder must
        //      agree or the head waits for bytes that never come.  Both
        //      spellings are looked for at once and the one that starts
        //      first wins; they start at different LFs, so they never tie.
        //
        //      Once the CRLF spelling is found at some offset, only a bare
        //      one starting before it can win, so the second search stops
        //      there rather than walking the rest of the buffer. A block
        //      ending \r\n\r\n holds no \n\n at all, so searching the whole
        //      of it was a scan of every byte after the answer -- on every
        //      read, and once more per informational response ahead of the
        //      final one, which is what a peer sending 1xx after 1xx one
        //      byte at a time was buying.
        p8 address_to crlf = (p8 address_to)memory_search(bytes, size, "\n\r\n", 3);
        positive span = crlf ? (positive)(crlf - bytes) + 1 : size;
        p8 address_to bare = (p8 address_to)memory_search(bytes, span, "\n\n", 2);

        if (crlf && (!bare || crlf < bare))
                return (bipolar)(crlf - bytes) + 3;

        if (bare)
                return (bipolar)(bare - bytes) + 2;

        return -1;
}

/* Validate the complete response head before assigning meaning to any field.
   In particular, do not let a NUL, a bare carriage return, or obsolete line
   folding give this client a different field boundary from a proxy in front
   of it.  A line ends at LF with one CR before it dropped, which is where
   http_header_end, http_status_code and http_header end it too (RFC 9112
   2.2 lets a recipient take a lone LF): a validator that split lines
   anywhere else would itself be the differential.  Response splitting is still relevant to a client: a poisoned
   keep-alive cache or an intercepting proxy can otherwise make the bytes
   authenticated by TLS describe a different response from the one consumed
   here.

   The default reads a head as GNU wget 1.25.0 and curl 8.22.0 both do, and
   refuses what either of them refuses where a second reading would frame or
   redirect differently: it takes a Content-Length that repeats one number
   ("3" twice, "3, 3"), a folded line after an ordinary field, and any byte
   but NUL and a bare CR in the reason phrase (only the code is read); it
   refuses Content-Length values that disagree, an empty element of a list, a
   fold after Content-Length, Transfer-Encoding or a redirect's Location, and
   a control in any field.  STRICT_TIGHT refuses every repeat, list, fold and
   status-line control. */
static bool http_token_byte(p8 byte)
{
        return byte_is_alnum(byte) ||
               memory_first_of("!#$%&'*+-.^_`|~", byte, 15);
}

/* One line of a head or of a trailer, its line end gone.  A field is a
   token, a colon, and a value that is text with no control but tab.  The
   token also refuses obs-fold, whitespace before the colon, and an empty
   field name. */
static bipolar http_field_name(p8 address_to line, positive stop)
{
        positive name = string_span_max((string_address)line, stop,
                                         string_set_http_token);

        if (!name || name == stop || line[name] != ':' ||
            memory_text_span(line + name + 1, stop - name - 1) !=
                stop - name - 1)
                return -1;
        return (bipolar)name;
}

/* A status line is never read past its code (http_status_code), so the
   default takes what wget and curl take in its reason phrase -- DEL, ESC and
   bytes past 0x7f included -- and refuses only a NUL and a CR, the two curl
   refuses; nothing of it is printed or stored.  STRICT_TIGHT holds it to
   text with no control but tab.  (Plain C, not #if: a hosted lift that
   forgets to define the tier fails to build instead of running as the tight
   one.) */
static bool http_line_valid(p8 address_to line, positive stop, bool field)
{
        positive at = 0;

        if (field)
                return http_field_name(line, stop) >= 0;
        for (;;)
        {
                at += memory_text_span(line + at, stop - at);
                if (at == stop)
                        return true;
                if (MOONWATER_STRICT_NET >= STRICT_TIGHT || !line[at] ||
                    line[at] == '\r')
                        return false;
                at++;
        }
}

//      One header's value, by name, without regard to its case.
static string_address http_header(p8 address_to bytes, positive size,
                                  string_address name, positive address_to length,
                                  bool address_to repeated)
{
        positive at = 0;
        positive want = string_length(name);
        string_address found = null;
        positive found_length = 0;

        if (repeated)
                address_to repeated = false;

        while (at < size)
        {
                positive line = at;
                positive stop = at + memory_span_without_byte(
                    bytes + at, '\n', size - at);

                at = stop + (stop < size);

                if (stop > line && bytes[stop - 1] == '\r')
                        stop--;

                if (stop - line <= want || bytes[line + want] != ':')
                        continue;

                if (memory_compare_ascii_case(bytes + line, name, want))
                        continue;

                {
                        positive from = line + want + 1;

                        from += string_span_max((string_address)(bytes + from), stop - from,
                                                string_set_blanks);

                        if (found)
                        {
                                if (repeated)
                                        address_to repeated = true;
                                continue;
                        }

                        /* RFC 9112 5: the optional whitespace around a
                           value is not part of it, at either end. */
                        found = (string_address)(bytes + from);
                        found_length = stop - from;
                        while (found_length &&
                               byte_is_blank(found[found_length - 1]))
                                found_length--;
                }
        }

        if (found && length)
                address_to length = found_length;

        return found;
}

static bool http_chunk_extensions_valid(string_address at,
                                        string_address stop)
{
        while (at < stop)
        {
                string_address name;

                if (*at++ != ';')
                        return false;
                name = at;
                while (at < stop && http_token_byte(*at))
                        at++;
                if (at == name)
                        return false;

                if (at < stop && *at == '=')
                {
                        at++;
                        if (at == stop)
                                return false;
                        if (*at == '"')
                        {
                                /* A quoted-string: an escape takes the next
                                   byte whatever it is, and no byte in it is
                                   a control but tab. */
                                for (at++; at < stop && *at != '"'; at++)
                                        if ((*at == '\\' && ++at == stop) ||
                                            (byte_is_control(*at) && *at != '\t'))
                                                return false;
                                if (at++ == stop)
                                        return false;
                        }
                        else
                        {
                                string_address value = at;

                                while (at < stop && http_token_byte(*at))
                                        at++;
                                if (at == value)
                                        return false;
                        }
                }
        }

        return true;
}

/* Parse one complete chunk-size line for both buffered fetch and streaming
   wget.  Keeping the extension grammar here prevents the two paths from
   disagreeing about controls or ambiguous separators. */
static bipolar http_chunk_line(p8 address_to line, positive line_length,
                               positive address_to chunk_length)
{
        string_address number = (string_address)line;
        string_address stop = (string_address)(line + line_length);
        positive parsed;

        if (!line_length || stop[-1] != '\n')
                return HTTP_MALFORMED;
        stop--;
        if (stop > number && stop[-1] == '\r')
                stop--;

        if (!string_digits_checked(address_of number, 16, address_of parsed))
                return HTTP_MALFORMED;
        if (number < stop && byte_is_blank(number[0]))
                number += string_span_max(number, stop - number,
                                          string_set_blanks);
        if (number < stop && !http_chunk_extensions_valid(number, stop))
                return HTTP_MALFORMED;

        address_to chunk_length = parsed;
        return HTTP_OK;
}

/* Return one for the terminating blank line, zero for a valid trailer field,
   and a negative status for malformed framing. */
static bipolar http_trailer_line(p8 address_to line, positive line_length)
{
        positive stop = line_length;

        if (!stop || line[stop - 1] != '\n')
                return HTTP_MALFORMED;
        stop--;
        if (stop && line[stop - 1] == '\r')
                stop--;
        if (!stop)
                return 1;
        return http_line_valid(line, stop, true) ? 0 : HTTP_MALFORMED;
}

/*
        The whole exchange.

        The caller is handed the body and the status line's code. Redirects
        are reported rather than followed: a client that follows them needs a
        loop limit, a same-host rule and an opinion about relative locations,
        and none of that belongs in the first version.
*/
static bipolar http_status_code(p8 address_to bytes, positive size,
                                 b32 address_to code);

#define HTTP_BODY_CLOSE 0
#define HTTP_BODY_LENGTH 1
#define HTTP_BODY_CHUNKED 2

typedef struct
{
        b32 code;
        p8 body_kind;
        positive body_length;
        string_address location;
        positive location_length;
} http_response;

typedef struct
{
        string_address transfer;
        string_address length;
        positive transfer_size;
        positive length_size;
        //      The number every Content-Length named, at the default tier.
        positive length_value;
} http_framing_fields;

static bool http_response_is_redirect(b32 code)
{
        return code == 300 || code == 301 || code == 302 || code == 303 ||
               code == 307 || code == 308;
}

static bool http_response_is_success(b32 code)
{
        return code >= 200 && code < 300;
}

/* One Content-Length value at the default tier, as wget and curl read it: a
   comma list of decimal numbers with blanks around each, every number the
   same as each other and as every Content-Length before it.  An empty
   element, a sign, any other byte, or a number that disagrees is refused
   (curl refuses each; GNU wget reads the first number and ignores the rest).
   The first number's digits are the span the framing reads, so a list of one
   number frames exactly as the number alone. */
static bool http_length_values(const p8 address_to value, positive size,
                               http_framing_fields address_to fields)
{
        byte_reader reader = byte_reader_open(value, size);

        for (;;)
        {
                byte_reader look;
                const p8 address_to digits;
                positive count = 0;
                positive number = 0;

                for (look = reader; byte_is_blank(byte_reader_u8(address_of look));
                     look = reader)
                        byte_reader_skip(address_of reader, 1);
                digits = byte_reader_here(address_of reader);
                for (look = reader;; look = reader, count++)
                {
                        p8 byte = byte_reader_u8(address_of look);

                        if (!byte_reader_ok(address_of look) || !byte_is_digit(byte))
                                break;
                        if (!positive_append_digit(address_of number,
                                                   (positive)(byte - '0'),
                                                   positive_max))
                                return false;
                        byte_reader_skip(address_of reader, 1);
                }
                if (!count)
                        return false;
                if (!fields->length)
                {
                        fields->length = (string_address)digits;
                        fields->length_size = count;
                        fields->length_value = number;
                }
                else if (number != fields->length_value)
                        return false;
                for (look = reader; byte_is_blank(byte_reader_u8(address_of look));
                     look = reader)
                        byte_reader_skip(address_of reader, 1);
                if (!byte_reader_left(address_of reader))
                        return byte_reader_ok(address_of reader);
                if (byte_reader_u8(address_of reader) != ',')
                        return false;
        }
}

/* Validate each line and collect the framing fields in the same walk.
   Unknown fields still undergo the full grammar and control-byte checks;
   their names need no repeated comparison with fields of other lengths.
   A folded line (one that begins with a blank) continues the field before
   it, whose value here is its first line either way: the default reads one
   after an ordinary field, or straight after the status line, as wget and
   curl do, and refuses one after Content-Length or Transfer-Encoding (curl
   refuses both; GNU wget reads the length and prints a chunked body raw)
   and after the Location of a redirect, which the two follow to different
   places (wget to "/a%20%20%20/b", curl to "/a%20/b" for "/a" folded onto
   "/b"). */
static bool http_response_fields(p8 address_to bytes, positive size,
                                 http_response address_to response,
                                 http_framing_fields address_to fields)
{
        positive at = 0;
        bool status = true;
        bool redirect = http_response_is_redirect(response->code);
        bool framing_before = false;

        memory_zero(fields, sizeof *fields);
        while (at < size)
        {
                positive line = at;
                positive stop = at + memory_span_without_byte(
                    bytes + at, '\n', size - at);

                if (stop == size)
                        return false;
                at = stop + 1;
                if (stop > line && bytes[stop - 1] == '\r')
                        stop--;
                if (stop == line)
                        return !status && at == size;
                if (status)
                {
                        if (!http_line_valid(bytes + line, stop - line, false))
                                return false;
                        status = false;
                        continue;
                }
                if (byte_is_blank(bytes[line]))
                {
                        if (MOONWATER_STRICT_NET >= STRICT_TIGHT || framing_before ||
                            memory_text_span(bytes + line, stop - line) !=
                                stop - line)
                                return false;
                        continue;
                }

                bipolar name = http_field_name(bytes + line, stop - line);
                string_address address_to value = null;
                positive address_to length = null;

                if (name < 0)
                        return false;
                framing_before = false;
                if (name == 17 && !memory_compare_ascii_case(
                        bytes + line, "transfer-encoding", 17))
                {
                        framing_before = true;
                        value = address_of fields->transfer;
                        length = address_of fields->transfer_size;
                }
                else if (name == 14 && !memory_compare_ascii_case(
                        bytes + line, "content-length", 14))
                {
                        framing_before = true;
                        if (MOONWATER_STRICT_NET < STRICT_TIGHT)
                        {
                                if (!http_length_values(bytes + line + 15,
                                                        stop - line - 15, fields))
                                        return false;
                                continue;
                        }
                        value = address_of fields->length;
                        length = address_of fields->length_size;
                }
                else if (redirect && name == 8 && !memory_compare_ascii_case(
                        bytes + line, "location", 8))
                {
                        framing_before = true;
                        value = address_of response->location;
                        length = address_of response->location_length;
                }
                if (!value)
                        continue;
                if (*value)
                        return false;

                positive from = line + (positive)name + 1;

                from += string_span_max((string_address)bytes + from,
                                         stop - from, string_set_blanks);
                while (stop > from &&
                       (bytes[stop - 1] == ' ' || bytes[stop - 1] == '\t'))
                        stop--;
                *value = (string_address)bytes + from;
                *length = stop - from;
        }
        return false;
}

/* Status and body framing have one interpretation in both clients.  This
   rejects duplicate or conflicting declarations before either the buffered
   or streaming body path acts on them. */
static bipolar http_response_framing_scan(p8 address_to bytes, positive size,
                                          positive address_to resume,
                                          positive address_to searched,
                                          positive address_to header_length,
                                          http_response address_to response)
{
        positive scan = size;
        positive at = address_to resume;

        if (!size)
                return HTTP_NO_REPLY;
        if (scan > HTTP_HEAD_MAX)
                scan = HTTP_HEAD_MAX;

        for (;;)
        {
                http_framing_fields fields;
                bipolar header;

                memory_fill(response, 0, sizeof(*response));
                {
                        bipolar status = http_status_code(
                            bytes + at, scan - at,
                            address_of response->code);
                        if (status)
                                return status == HTTP_NO_REPLY &&
                                               scan == HTTP_HEAD_MAX
                                           ? HTTP_MALFORMED
                                           : status;
                }
                if (response->code < 100 || response->code > 599)
                        return HTTP_MALFORMED;

                positive from = max(at, address_to searched);

                header = http_header_end(bytes + from, scan - from);
                if (header < 0)
                {
                        /* Only the last three bytes can begin a terminator
                           completed by a later read. Keep them across reads. */
                        address_to searched = max(at, scan > 3 ? scan - 3 : 0);
                        return scan == HTTP_HEAD_MAX ? HTTP_MALFORMED
                                                     : HTTP_NO_REPLY;
                }
                header += (bipolar)(from - at);
                if (!http_response_fields(bytes + at, (positive)header,
                                           response, address_of fields))
                        return HTTP_MALFORMED;
                if (fields.transfer && fields.length)
                        return HTTP_MALFORMED;

                if (fields.transfer)
                {
                        if (fields.transfer_size != 7 ||
                            memory_compare_ascii_case(fields.transfer, "chunked", 7))
                                return HTTP_MALFORMED;
                        response->body_kind = HTTP_BODY_CHUNKED;
                }
                else if (fields.length)
                {
                        string_address cursor = fields.length;

                        if (!string_digits_checked(
                                address_of cursor, 10,
                                address_of response->body_length) ||
                            (positive)(cursor - fields.length) !=
                                fields.length_size)
                                return HTTP_MALFORMED;
                        response->body_kind = HTTP_BODY_LENGTH;
                }

                /* RFC 9110 15.3.5: a 204 response ends at the blank line;
                   Content-Length is forbidden, and RFC 9112 6.1 likewise
                   forbids Transfer-Encoding.  Section 15.3.6 permits a 205
                   Content-Length only when it is zero.  The default does
                   what wget and curl do -- both accept every one of these,
                   ignore a 204's declared body and print a 205's -- because
                   real servers send 204 with Content-Length: 0 and this
                   client closes the connection after one response, so bytes
                   left unread after the head can never be taken for the next
                   response.  A build that wants the RFC's framing exactly
                   refuses a declaration that cannot describe the no-content
                   response.  (Plain C, not #if: a hosted lift of this section that
                   forgets to define the tier fails to build instead of
                   silently running as the tight one.) */
                if (MOONWATER_STRICT_NET >= STRICT_TIGHT &&
                    ((response->code == 204 &&
                      response->body_kind != HTTP_BODY_CLOSE) ||
                     (response->code == 205 &&
                      response->body_kind == HTTP_BODY_LENGTH &&
                      response->body_length)))
                        return HTTP_MALFORMED;

                /* Informational responses precede, rather than replace, the
                   final response.  They cannot carry message framing, and a
                   protocol switch is outside this connection-close client. */
                if (response->code < 200)
                {
                        if (response->code == 101 ||
                            response->body_kind != HTTP_BODY_CLOSE)
                                return HTTP_MALFORMED;
                        at += (positive)header;
                        /* Only here. An informational response that is
                           whole stays whole however many bytes arrive after
                           it -- http_header_end answers with the FIRST
                           terminator, so a longer buffer cannot produce an
                           earlier one -- so the walk resumes past it rather
                           than deciding it again on every read. Writing the
                           mark anywhere else would skip the final response's
                           own start. */
                        address_to resume = at;
                        address_to searched = at;
                        continue;
                }

                address_to header_length = at + (positive)header;
                return HTTP_OK;
        }
}

/* Stateless callers scan once; a stream carries searched across reads. */
static bipolar http_response_framing_from(p8 address_to bytes, positive size,
                                          positive address_to resume,
                                          positive address_to header_length,
                                          http_response address_to response)
{
        positive searched = address_to resume;

        return http_response_framing_scan(bytes, size, resume,
                                          address_of searched, header_length,
                                          response);
}

/* One whole head in one buffer, for a caller with nothing to resume: the
   checks read a response entire, and a peer that trickles one has
   http_response_head holding the mark for it instead. */
static bipolar http_response_framing(p8 address_to bytes, positive size,
                                     positive address_to header_length,
                                     http_response address_to response)
{
        positive resume = 0;

        return http_response_framing_from(bytes, size, address_of resume,
                                          header_length, response);
}

static bipolar http_get_request(p8 address_to request, positive room,
                                string_address host, p16 port,
                                string_address path, bool tls,
                                p8 version_minor, string_address agent,
                                positive address_to used);

static bipolar http_stream_open(p32 host, p16 port, positive seconds)
{
        socket_address_internet where = {
            .family = AF_INET, .port = network_order_16(port),
            .host = network_order_32(host)};
        b32 one = 1;
        bipolar handle = socket_new(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);

        if (handle < 0)
                return HTTP_NO_ROUTE;
        /* TLS Finished and the HTTP request are consecutive small writes.
           Each is already framed into one send; waiting for an ACK to batch
           the next would add Nagle/delayed-ACK latency to a ready request. */
        if (!network_stream_timeout(handle, seconds, 0) ||
            socket_option_set((b32)handle, IPPROTO_TCP, TCP_NODELAY,
                               address_of one, sizeof one) < 0 ||
            socket_connect((b32)handle, address_of where, sizeof where) < 0)
        {
                socket_close((b32)handle);
                return HTTP_NO_ROUTE;
        }
        return handle;
}

typedef struct
{
        bipolar handle;
        bool tls;
        tls_conn session;
} http_link;

static fn http_link_close(http_link address_to link)
{
        if (link->handle >= 0)
                socket_close((b32)link->handle);
        if (link->tls)
                tls_forget(address_of link->session);
        link->handle = -1;
        link->tls = false;
}

//      tls_connect's refusal as the HTTP client says it.
static bipolar http_tls_status(bipolar verdict)
{
        return verdict == TLS_UNTRUSTED ? HTTP_UNTRUSTED
               : verdict == TLS_EXPIRED ? HTTP_EXPIRED
               : verdict == TLS_NOT_YET ? HTTP_NOT_YET
               : verdict == TLS_MISMATCH ? HTTP_MISMATCH
               : verdict == TLS_REVOKED ? HTTP_REVOKED
               : verdict == TLS_UNCHECKED ? HTTP_UNCHECKED
               : verdict == TLS_CLOCK ? HTTP_CLOCK
               : verdict == TLS_UNLOGGED ? HTTP_UNLOGGED
                                        : HTTP_TLS;
}

static bipolar http_link_open(http_link address_to link, p32 ip, p16 port,
                              string_address host, bool tls, bool check_cert)
{
        //      The session's buffers are tls_connect's to leave alone.
        memory_fill(link, 0, __builtin_offsetof(http_link, session));
        link->handle = http_stream_open(ip, port, HTTP_IDLE_SECONDS);
        if (link->handle < 0)
                return link->handle;
        if (tls)
        {
                bipolar verdict = tls_connect(address_of link->session,
                                              link->handle, host, check_cert);

                if (verdict)
                {
                        http_link_close(link);
                        return http_tls_status(verdict);
                }
                link->tls = true;
        }
        return HTTP_OK;
}

static bipolar http_link_write(http_link address_to link, p8 address_to data,
                               positive length)
{
        if (link->tls)
                return tls_write(address_of link->session, data, length)
                           ? HTTP_NO_REPLY : HTTP_OK;
        return network_stream_send_all_for(link->handle, data, length,
                                            HTTP_IDLE_SECONDS, 0)
                   ? HTTP_OK : HTTP_NO_REPLY;
}

static bipolar http_link_read_until(
    http_link address_to link, p8 address_to into, positive room,
    positive address_to got, const network_deadline address_to deadline)
{
        bipolar n;

        if (link->tls)
                return tls_read_until(address_of link->session, into, room, got,
                                      deadline)
                           ? HTTP_NO_REPLY : HTTP_OK;

        n = network_stream_read_some_until(link->handle, into, room, deadline);
        if (n < 0)
                return HTTP_NO_REPLY;
        address_to got = (positive)n;
        return HTTP_OK;
}

static bipolar http_response_head(
    http_link address_to link, p8 address_to head, positive room,
    positive address_to used, positive address_to header,
    http_response address_to response, positive seconds, positive nanoseconds,
    bool incomplete_is_malformed, const network_deadline address_to outer)
{
        network_deadline deadline;
        /* Carried across the reads: the head is walked once in total rather
           than from byte zero on every read, which is what a peer sending
           informational responses one byte at a time was buying. */
        positive resume = 0;
        positive searched = 0;

        address_to used = 0;
        //      A redirect chain's own deadline, when it has one, holds this.
        if (!network_deadline_begin_within(address_of deadline, seconds,
                                           nanoseconds, outer))
                return HTTP_NO_REPLY;

        for (;;)
        {
                positive got = 0;
                bipolar status = http_response_framing_scan(
                    head, address_to used, address_of resume,
                    address_of searched, header,
                    response);

                if (status != HTTP_NO_REPLY)
                        return status;
                if (address_to used == room)
                        return HTTP_MALFORMED;
                status = http_link_read_until(
                    link, head + address_to used, room - address_to used,
                    address_of got, address_of deadline);
                if (status)
                        return HTTP_NO_REPLY;
                if (!got)
                        return incomplete_is_malformed && address_to used >= 13
                                   ? HTTP_MALFORMED : HTTP_NO_REPLY;
                address_to used += got;
        }
}

static bool http_response_has_no_body(b32 code)
{
        return code == 204 || code == 304;
}

typedef struct
{
        http_link address_to link;
        p8 address_to stash;
        positive stash_used;
        // Streaming reuses the consumed header buffer for split lines and I/O.
        p8 address_to scratch;
        // A memory body compacts payload behind its read cursor.
        p8 address_to output;
        // Buffered fetch appends decoded payload without first slurping framing.
        http_buffer address_to store;
        positive store_limit;
        // Tests can shorten one logical progress wait; zero selects HTTP's idle limit.
        positive read_seconds;
        positive read_nanoseconds;
        // Whole-body budget; zero began is reserved for bounded unit readers.
        network_deadline deadline;
        // The most payload a body that declared no length may deliver, and
        // what a chunked one has delivered so far; zero for no ceiling.
        positive undeclared_most;
        positive undeclared_seen;
} http_body;

static bipolar http_body_borrow(http_body address_to body, positive room,
                                p8 address_to address_to data,
                                positive address_to got);

//      One progress wait, shortened by a whole-body budget when one is active;
//      inline, because a read of an unbudgeted body asks only for the idle limit.
static inline INLINE bool http_body_wait(
    const http_body address_to body, positive address_to seconds,
    positive address_to nanoseconds)
{
        positive idle_seconds = body->read_seconds || body->read_nanoseconds
                                    ? body->read_seconds : HTTP_IDLE_SECONDS;
        positive idle_nanoseconds = body->read_nanoseconds;

        address_to seconds = idle_seconds;
        address_to nanoseconds = idle_nanoseconds;
        if (body->deadline.began)
        {
                positive left_seconds;
                positive left_nanoseconds;

                if (!network_deadline_left(address_of body->deadline,
                                           address_of left_seconds,
                                           address_of left_nanoseconds))
                        return false;
                if (left_seconds < idle_seconds ||
                    (left_seconds == idle_seconds &&
                     left_nanoseconds < idle_nanoseconds))
                {
                        address_to seconds = left_seconds;
                        address_to nanoseconds = left_nanoseconds;
                }
        }
        return address_to seconds || address_to nanoseconds;
}

/* Payload copied into the caller's bytes: the stash's and TLS's through
   http_body_borrow, a plaintext socket's by a read that asks the clock and
   polls only when nothing is queued yet (network_stream_read_some_for). */
static bipolar http_body_read(http_body address_to body, p8 address_to into,
                              positive room, positive address_to got)
{
        bipolar n;
        positive seconds;
        positive nanoseconds;

        if (body->stash_used || (body->link && body->link->tls))
        {
                p8 address_to data = null;
                bipolar status = http_body_borrow(body, room, address_of data, got);

                if (!status && address_to got)
                        memory_copy(into, data, address_to got);
                return status;
        }
        if (!body->link)
        {
                address_to got = 0;
                return HTTP_OK;
        }
        if (!http_body_wait(body, address_of seconds, address_of nanoseconds))
                return HTTP_NO_REPLY;

        n = network_stream_read_some_for(body->link->handle, into, room,
                                         seconds, nanoseconds);
        if (n < 0 || (positive)n > room)
                return HTTP_NO_REPLY;
        address_to got = (positive)n;
        return HTTP_OK;
}

/* Payload for a copy, lent wherever a buffer already holds it: the header
   stash first, then plaintext the TLS record layer decrypted in place. Only a
   plaintext socket read lands in the scratch, which callers size at
   HTTP_HEAD_MAX. */
static bipolar http_body_borrow(http_body address_to body, positive room,
                                p8 address_to address_to data,
                                positive address_to got)
{
        if (body->stash_used)
        {
                positive take = min(room, body->stash_used);

                address_to data = body->stash;
                body->stash += take;
                body->stash_used -= take;
                address_to got = take;
                return HTTP_OK;
        }

        if (body->link && body->link->tls)
        {
                positive seconds;
                positive nanoseconds;

                if (!http_body_wait(body, address_of seconds,
                                    address_of nanoseconds))
                        return HTTP_NO_REPLY;
                return tls_borrow_within(address_of body->link->session,
                                  room, data, got, seconds, nanoseconds,
                                  body->deadline.began ? address_of body->deadline
                                                       : null)
                           ? HTTP_NO_REPLY : HTTP_OK;
        }

        address_to data = body->scratch;
        return http_body_read(body, body->scratch,
                              min(room, (positive)HTTP_HEAD_MAX), got);
}

/* One writev iovec, laid out as the kernel reads it on every LP64 target. */
typedef struct
{
        address_any base;
        positive length;
} http_span;

#define HTTP_WRITE_SPANS 64

/*
        Why the last body write failed, for the line that names it: a full
        disk is "No space left on device", not a server that stopped
        answering. Set only where a write fails, and zero when the failure
        was a count no write can return.
*/
static bipolar http_write_failure;

/* Write every span, resuming after partial progress. Interruption starts
   one absolute retry budget; ordinary output needs no clock. A syscall
   which itself blocks on a disk or pipe is not preempted by that budget. */
static bool http_write_spans(bipolar dest, http_span address_to spans,
                             positive count, positive total)
{
        network_deadline patience = {0};

        while (total)
        {
                positive left[2];

                if (patience.began && !network_deadline_left(
                        address_of patience, left, left + 1))
                {
                        http_write_failure = NETWORK_INTERRUPTED;
                        return false;
                }
                bipolar wrote = (bipolar)system_call_3(
                    syscall(writev), (positive)dest, (positive)spans, count);

                if (wrote == NETWORK_INTERRUPTED)
                {
                        if (!patience.began && !network_deadline_begin(
                                address_of patience, HTTP_IDLE_SECONDS, 0))
                        {
                                http_write_failure = NETWORK_INTERRUPTED;
                                return false;
                        }
                        continue;
                }
                if (wrote <= 0 || (positive)wrote > total)
                {
                        http_write_failure = wrote < 0 ? wrote
                                             : wrote ? 0 : -ENOSPC;
                        return false;
                }
                total -= (positive)wrote;
                while (count && (positive)wrote >= spans->length)
                {
                        wrote -= (bipolar)spans->length;
                        spans++;
                        count--;
                }
                if (count)
                {
                        spans->base = (p8 address_to)spans->base + wrote;
                        spans->length -= (positive)wrote;
                }
        }
        return true;
}

/* One transfer loop for exact lengths, EOF bodies and in-place decoding.
   Toward a file, TLS records go out straight from where they were decrypted,
   every record already whole in the receive buffer in the same writev. */
static bipolar http_copy(http_body address_to body, bipolar dest, positive want,
                          bool exact)
{
        if (!body->link && exact && want > body->stash_used)
                return HTTP_NO_REPLY;
        while (want)
        {
                p8 address_to data = null;
                positive got = 0;
                positive left[2];

                if (body->deadline.began && !network_deadline_left(
                        address_of body->deadline, left, left + 1))
                        return HTTP_NO_REPLY;

                if (http_body_borrow(body, want, address_of data,
                                     address_of got))
                        return HTTP_NO_REPLY;
                if (!got)
                        return exact ? HTTP_MALFORMED : HTTP_OK;
                if (body->output)
                {
                        memory_copy(body->output, data, got);
                        body->output += got;
                }
                else if (body->store)
                {
                        if (body->store->used > body->store_limit ||
                            got > body->store_limit - body->store->used)
                                return HTTP_MALFORMED;
                        if (!byte_store_reserve(
                                body->store, body->store->used + got + 1,
                                4096))
                                return HTTP_NO_REPLY;
                        memory_copy(body->store->bytes + body->store->used,
                                    data, got);
                        body->store->used += got;
                        body->store->bytes[body->store->used] = end;
                }
                else
                {
                        http_span spans[HTTP_WRITE_SPANS];
                        positive count = 1;
                        positive total = got;

                        //      Lending never receives, so the spans already
                        //      gathered stay where they are.
                        spans[0].base = data;
                        spans[0].length = got;
                        while (body->link && body->link->tls &&
                               count < HTTP_WRITE_SPANS && total < want)
                        {
                                p8 address_to more = null;
                                positive more_got = 0;
                                bipolar lent = tls_lend_until(
                                    address_of body->link->session,
                                    want - total, address_of more,
                                    address_of more_got,
                                    body->deadline.began ? address_of body->deadline
                                                         : null);

                                if (lent == TLS_AGAIN || (!lent && !more_got))
                                        break;
                                if (lent)
                                        return HTTP_NO_REPLY;
                                spans[count].base = more;
                                spans[count].length = more_got;
                                count++;
                                total += more_got;
                        }
                        if (!http_write_spans(dest, spans, count, total))
                                return http_write_failure ? HTTP_WRITE
                                                          : HTTP_NO_REPLY;
                        got = total;
                }
                want -= got;
        }
        return HTTP_OK;
}

/* Borrow complete framing lines from the current span. Socket lines split
   across reads use the caller's scratch and retain the streaming line limit;
   complete memory responses have their existing whole-response bound. A
   plaintext socket's reads fill the scratch, so what follows a line -- the
   next chunk's data, usually -- is already in the stash for the copy after
   it: one read where a chunk took three. TLS reads stop at the limit, since
   its data is lent from where it was decrypted and reading on would copy
   it. */
static bipolar http_line(http_body address_to body, positive limit,
                          p8 address_to address_to line,
                          positive address_to length)
{
        positive used = body->stash_used;
        positive span = memory_span_without_byte(body->stash, '\n',
                                                 min(used, limit));
        if (span < min(used, limit))
        {
                *line = body->stash;
                *length = span + 1;
                body->stash += span + 1;
                body->stash_used -= span + 1;
                return HTTP_OK;
        }
        if (!body->link || used >= limit)
                return HTTP_MALFORMED;

        p8 address_to scratch = body->scratch;
        positive reach = body->link->tls ? limit : HTTP_HEAD_MAX;
        memory_copy(scratch, body->stash, used);
        body->stash_used = 0;
        while (used < limit)
        {
                positive got = 0;
                positive seen;
                if (http_body_read(body, scratch + used, reach - used,
                                   address_of got))
                        return HTTP_NO_REPLY;
                if (!got)
                        return HTTP_MALFORMED;
                seen = min(got, limit - used);
                span = memory_span_without_byte(scratch + used, '\n', seen);
                if (span < seen)
                {
                        *line = scratch;
                        *length = used + span + 1;
                        body->stash = scratch + *length;
                        body->stash_used = got - span - 1;
                        return HTTP_OK;
                }
                used += got;
        }
        return HTTP_MALFORMED;
}

static bipolar http_copy_trailers(http_body address_to body)
{
        p8 address_to line;
        positive total = 0;

        while (total < HTTP_HEAD_MAX)
        {
                positive line_length = 0;
                bipolar parsed;

                if (http_line(body, HTTP_HEAD_MAX, address_of line,
                              address_of line_length))
                        return HTTP_MALFORMED;
                if (line_length > HTTP_HEAD_MAX - total)
                        return HTTP_MALFORMED;
                total += line_length;
                parsed = http_trailer_line(line, line_length);
                if (parsed < 0)
                        return parsed;
                if (parsed)
                        return HTTP_OK;
        }

        return HTTP_MALFORMED;
}

static bipolar http_copy_chunked(http_body address_to body, bipolar dest)
{
        p8 address_to line;
        positive line_length = 0;
        positive size = 0;

        for (;;)
        {
                if (http_line(body, 127, address_of line,
                              address_of line_length) ||
                    http_chunk_line(line, line_length, address_of size))
                        return HTTP_MALFORMED;
                if (!size)
                        return http_copy_trailers(body);
                /* A chunk past the ceiling on an undeclared body is refused
                   before a byte of it is written. */
                if (body->undeclared_most)
                {
                        if (size > body->undeclared_most - body->undeclared_seen)
                                return HTTP_UNDECLARED;
                        body->undeclared_seen += size;
                }
                {
                        bipolar copied = http_copy(body, dest, size, true);

                        if (copied)
                                return copied;
                }
                /* The data ends in CRLF or a bare LF: a line of at most two
                   bytes, borrowed like any other rather than read a byte
                   at a time. */
                if (http_line(body, 2, address_of line, address_of line_length) ||
                    (line_length == 2 && line[0] != '\r'))
                        return HTTP_MALFORMED;
        }
}

/* A final response's body, by the framing its head declared.  A memory store
   refuses a close-delimited body beyond its limit as it arrives, so an EOF
   that lands exactly on the limit is still a complete body.  So does a body's
   ceiling on what declared no length (undeclared_most): exactly that many
   bytes and then the close is a body, and one byte more is
   HTTP_UNDECLARED, asked for only once the ceiling is reached and never
   written. */
static bipolar http_copy_body(http_body address_to body, bipolar dest,
                              const http_response address_to response)
{
        bipolar copied;
        p8 address_to more = null;
        positive got = 0;

        if (response->body_kind == HTTP_BODY_CHUNKED)
                return http_copy_chunked(body, dest);
        if (response->body_kind == HTTP_BODY_LENGTH)
                return http_copy(body, dest, response->body_length, true);
        copied = http_copy(body, dest,
                           body->undeclared_most ? body->undeclared_most
                                                 : positive_max,
                           false);
        if (copied || !body->undeclared_most)
                return copied;
        if (http_body_borrow(body, 1, address_of more, address_of got))
                return HTTP_NO_REPLY;
        return got ? HTTP_UNDECLARED : HTTP_OK;
}

/* The memory frontend retains strict whole-body consumption and transfers no
   allocation: the same chunk decoder compacts within the response store. */
static bipolar http_unchunk(p8 address_to bytes, positive size)
{
        http_body body = {.stash = bytes, .stash_used = size, .output = bytes};
        if (http_copy_chunked(address_of body, -1) || body.stash_used)
                return HTTP_MALFORMED;
        return (bipolar)(body.output - bytes);
}

/* Bytes copied into an HTTP/1 request must be safe for the grammar position
   they occupy even when this low-level builder is called without first going
   through http_split_into.  Keeping the check at the serialization boundary
   is important: otherwise a later caller can turn a CR/LF in a target, Host,
   or User-Agent into a second header or a second request.  Origin-form
   targets stay ASCII (no backslash, no high bytes) and refuse percent-
   encodings of NUL/CR/LF; Host is the DNS-ish allowlist; User-Agent may
   still carry obs-text. */
enum
{
        HTTP_REQUEST_FIELD,
        HTTP_REQUEST_TARGET,
        HTTP_REQUEST_HOST,
};

static bool http_request_component_valid(string_address text, p8 kind)
{
        string_address peeled = text;

        if (!text)
                return false;

        if (kind == HTTP_REQUEST_HOST && !string_get(text))
                return false;

        for (; string_get(text); text++)
        {
                p8 byte = string_get(text);

                if (byte_is_control(byte) ||
                    (kind != HTTP_REQUEST_FIELD && byte == ' ') ||
                    (kind == HTTP_REQUEST_TARGET &&
                     (byte == '\\' || byte > 0x7f)))
                        return false;
                if (kind == HTTP_REQUEST_TARGET && byte == '%' &&
                    text >= peeled)
                {
                        /* One decode is not enough: %250d is a CR after two
                           URI decodes, and %25250a needs three. Peel %25
                           whether written as %25 or as a bare 25 hex pair
                           left after a previous peel.  Every '%' a walk
                           passes starts one of its steps, so a walk from it
                           would retrace this one's safe suffix: each byte
                           is walked once, not once per '%' before it.  A
                           digit is read only after the one before it was a
                           digit, never past the terminator. */
                        string_address at = text;
                        bool percent_form = true;

                        for (;;)
                        {
                                string_address pair = at;
                                positive high;
                                positive low;
                                p8 decoded;

                                if (string_get(at) == '%')
                                        pair = at + 1;
                                else if (percent_form)
                                        break;
                                high = digit_known(string_get(pair), 16);
                                if (high >= 16)
                                        break;
                                low = digit_known(string_get(pair + 1), 16);
                                if (low >= 16)
                                        break;
                                decoded = (p8)((high << 4) | low);
                                if (decoded == 0 || decoded == '\r' ||
                                    decoded == '\n')
                                        return false;
                                at = pair + 2;
                                if (decoded != '%')
                                        break;
                                percent_form = false;
                        }
                        peeled = at;
                }
                if (kind == HTTP_REQUEST_HOST && !http_host_byte(byte))
                        return false;
        }

        return true;
}


/* ":port" into into[7] when the port is not the scheme's own; its length,
   and zero when there is nothing to name. */
static positive http_port_suffix(p8 address_to into, bool tls, p16 port)
{
        into[0] = ':';
        return port == (tls ? HTTP_HTTPS_PORT : HTTP_PORT)
                   ? 0 : 1 + positive_into(into + 1, port);
}

/* The request, GET or -- for a caller that asks only whether the resource is
   there, as wget --spider does -- HEAD, whose answer has no content whatever
   its head declares (RFC 9110 9.3.2). */
static bipolar http_request(p8 address_to request, positive room, bool head,
                            string_address host, p16 port, string_address path,
                            bool tls, p8 version_minor, string_address agent,
                            positive address_to used)
{
        p8 target[HTTP_URL_MAX];
        p8 port_text[7];
        byte_store out = {request, room, 0};
        bool ok;

        if (!http_request_component_valid(host, HTTP_REQUEST_HOST) ||
            !http_request_component_valid(path, HTTP_REQUEST_TARGET) ||
            !http_request_component_valid(agent, HTTP_REQUEST_FIELD) ||
            http_origin_form(path, target, sizeof target) ||
            version_minor < '0' || version_minor > '9')
                return HTTP_BAD_URL;

        ok = head ? byte_store_append_exact(address_of out, "HEAD ", 5)
                  : byte_store_append_exact(address_of out, "GET ", 4);
        ok &= byte_store_append_exact(address_of out, target, string_length(target));
        ok &= byte_store_append_exact(address_of out, " HTTP/1.", 8);
        ok &= byte_store_append_exact(address_of out, address_of version_minor, 1);
        ok &= byte_store_append_exact(address_of out, "\r\nHost: ", 8);
        ok &= byte_store_append_exact(address_of out, host, string_length(host));
        ok &= byte_store_append_exact(address_of out, port_text,
                                      http_port_suffix(port_text, tls, port));
        ok &= byte_store_append_exact(address_of out, "\r\nUser-Agent: ", 14);
        ok &= byte_store_append_exact(address_of out, agent, string_length(agent));
        ok &= byte_store_append_exact(
            address_of out, "\r\nAccept: */*\r\nConnection: close\r\n\r\n", 36);
        if (!ok)
                return HTTP_BAD_URL;
        address_to used = out.used;
        return HTTP_OK;
}

static bipolar http_get_request(p8 address_to request, positive room,
                                string_address host, p16 port,
                                string_address path, bool tls,
                                p8 version_minor, string_address agent,
                                positive address_to used)
{
        return http_request(request, room, false, host, port, path, tls,
                            version_minor, agent, used);
}

static bipolar http_put_url(p8 address_to into, positive room, bool tls,
                            string_address host, p16 port, string_address path)
{
        p8 port_text[7];
        //      The last byte of the room is kept for the terminator.
        byte_store out = {into, room ? room - 1 : 0, 0};
        bool ok = room != 0;

        if (!string_get(path))
                path = (string_address) "/";
        ok &= byte_store_append_exact(address_of out, tls ? "https://" : "http://",
                                      tls ? 8 : 7);
        ok &= byte_store_append_exact(address_of out, host, string_length(host));
        ok &= byte_store_append_exact(address_of out, port_text,
                                      http_port_suffix(port_text, tls, port));
        ok &= byte_store_append_exact(address_of out, path, string_length(path));
        if (!ok)
                return HTTP_BAD_URL;
        into[out.used] = end;
        return HTTP_OK;
}

/* A Location resolved against the URL that answered with it, into room
   bytes at into, which may be location itself: the reference is copied
   before a byte is written. */
static bipolar http_absolutize(bool tls, string_address host, p16 port,
                               string_address path, string_address location,
                               p8 address_to into, positive room)
{
        p8 kept[HTTP_URL_MAX];
        p8 base[HTTP_URL_MAX];
        p8 merged[HTTP_URL_MAX];
        positive length = string_length(location);
        positive scheme;
        positive used;
        string_address cut;

        if (length >= sizeof kept ||
            http_origin_form(path, base, sizeof base))
                return HTTP_BAD_URL;
        memory_copy(kept, location, length + 1);
        cut = string_first_of(kept, '#');
        if (cut)
        {
                cut[0] = end;
                length = (positive)(cut - kept);
        }

        /* An absolute reference is http or https followed by "//", in any
           case; http_split_into reads the rest.  Any other scheme -- ftp:,
           javascript:, a bare "http:path" -- is an unsupported scheme, as
           GNU wget names it, never a relative path on the current host.  A
           network-path "//host" keeps the current scheme. */
        scheme = http_scheme_length(kept);
        if (scheme)
        {
                if (!http_web_scheme(kept, scheme) || kept[scheme] != '/' ||
                    kept[scheme + 1] != '/')
                        return HTTP_SCHEME;
                if (length >= room)
                        return HTTP_BAD_URL;
                memory_copy_apart_end(into, kept, length);
                return HTTP_OK;
        }
        if (kept[0] == '/' && kept[1] == '/')
        {
                positive scheme_length = tls ? 6 : 5;

                if (scheme_length + length + 1 > room)
                        return HTTP_BAD_URL;
                memory_copy_apart_end(
                    memory_copy_apart_end(into, tls ? "https:" : "http:",
                                          scheme_length),
                    kept, length);
                return HTTP_OK;
        }

        /* A fragment-only reference identifies the current resource: the
           fragment went above, and path and query stay.  An absolute path
           replaces both; a query replaces the query; anything else replaces
           the last segment of the path, and the query goes with it. The base
           is in origin form, so it starts with the '/' this finds. */
        if (!kept[0])
                return http_put_url(into, room, tls, host, port, base);
        if (kept[0] == '/')
                return http_put_url(into, room, tls, host, port, kept);
        cut = string_first_of(base, '?');
        if (cut)
                cut[0] = end;
        used = kept[0] == '?' ? string_length(base)
                              : (positive)(string_last_of(base, '/') - base) + 1;
        if (used + length + 1 > sizeof merged)
                return HTTP_BAD_URL;
        memory_copy_apart(merged, base, used);
        memory_copy_apart_end(merged + used, kept, length);
        return http_put_url(into, room, tls, host, port, merged);
}

/* 1 for a "." segment, 2 for "..", either spelled with %2e as well, as
   curl and a browser read them (GNU wget 1.25 sends a %2e spelling on
   as written, but resolves the plain dots); 0 for any other. */
static positive http_dot_segment(string_address segment, positive length)
{
        positive dots = 0;

        for (positive at = 0; at < length; dots++)
        {
                if (segment[at] == '.')
                        at++;
                else if (length - at >= 3 && segment[at] == '%' &&
                         segment[at + 1] == '2' && (segment[at + 2] | 0x20) == 'e')
                        at += 3;
                else
                        return 0;
        }
        return dots <= 2 ? dots : 0;
}

/* RFC 3986 5.2.4's remove_dot_segments over the path in front of a query,
   in place, where the path starts with '/': GNU wget and curl resolve the
   plain dot segments of every URL and Location rather than asking the server to,
   and a path climbing above the root stays at it. Output never outruns
   input, so each segment moves down over what was dropped. */
static fn http_path_simplify(p8 address_to path)
{
        positive length = string_span_without_set(path, "?#");
        positive read = 0;
        positive wrote = 0;

        if (path[0] != '/')
                return;
        while (read < length)
        {
                positive start = read + 1;
                positive stop = start + memory_span_without_byte(
                                            path + start, '/', length - start);
                positive dots = http_dot_segment((string_address)path + start,
                                                 stop - start);

                if (!dots)
                {
                        //      Nothing moves until something was dropped,
                        //      so a path with no dot segment -- the root
                        //      http_split_into names as a literal, too --
                        //      is never written.
                        if (wrote != read)
                                memory_copy(path + wrote, path + read,
                                            stop - read);
                        wrote += stop - read;
                }
                else
                {
                        //      ".." drops the last segment written.
                        while (dots == 2 && wrote && path[--wrote] != '/')
                                ;
                        if (stop == length)
                                path[wrote++] = '/';
                }
                read = stop;
        }
        if (wrote != length)
                memory_copy(path + wrote, path + length,
                            string_length(path + length) + 1);
}

/* Once a redirect chain has reached HTTPS, no later Location may discard
   transport authentication.  The caller applies this before name lookup or
   opening the next connection. */
static bool http_transport_allowed(bool address_to secure, bool tls)
{
        if (address_to secure && !tls)
                return false;
        address_to secure |= tls;
        return true;
}

/* localhost, and every name under it, is this machine (RFC 6761 6.3), in
   any case and with or without the absolute name's dot. curl answers those
   names itself and the C library GNU wget asks answers them from its own
   tables, so neither lets a resolver choose where "localhost" goes; one
   that asked the network's DNS server would fetch whatever that server
   pointed localhost at, and tell it the name besides. */
static bool http_host_loopback(string_address host)
{
        positive length = string_length(host);

        if (length && host[length - 1] == '.')
                length--;
        return length >= 9 &&
               !memory_compare_ascii_case(host + length - 9, "localhost", 9) &&
               (length == 9 || host[length - 10] == '.');
}

static bipolar http_status_code(p8 address_to bytes, positive size, b32 address_to code)
{
        if (size < 13)
                return HTTP_NO_REPLY;
        positive digits = 0;
        positive value = string_digits_max((string_address)(bytes + 9), 3,
                                           address_of digits);

        if (string_compare_max(bytes, (string_address) "HTTP/1.", 7) ||
            !byte_is_digit(bytes[7]) || bytes[8] != ' ' || digits != 3 ||
            (bytes[12] != ' ' && bytes[12] != '\r' && bytes[12] != '\n'))
                return HTTP_MALFORMED;
        if (code)
                address_to code = (b32)value;
        return HTTP_OK;
}

static p32 http_lookup(string_address host)
{
        p32 ip = 0;

        if (string_to_host(host) < 0 && http_host_loopback(host))
                return HOST_LOOPBACK;
        return net_resolve_v4(host, address_of ip, 3) == DNS_OK ? ip : 0;
}

/* Whether a fetch whose destination the peer chose may go to ip: not this
   network, loopback, private, shared (100.64/10), link-local, IETF
   protocol (192.0.0/24), benchmarking (198.18/15), multicast or reserved
   space. Checked on the address the name resolved to, so a name that
   rebinds to an inside address is refused too. The harness builds that
   define TLS_BENCH_ANCHOR serve from loopback and may reach it. */
static bool http_address_public(p32 ip)
{
        p8 a = (p8)(ip >> 24);
        p8 b = (p8)(ip >> 16);

#ifdef TLS_BENCH_ANCHOR
        if (a == 127)
                return true;
#endif
        return a && a != 10 && a != 127 && a < 224 &&
               (a != 100 || (b & 0xc0) != 64) && (a != 169 || b != 254) &&
               (a != 172 || (b & 0xf0) != 16) && (a != 192 || b != 168) &&
               (a != 192 || b || (p8)(ip >> 8)) && (a != 198 || (b & 0xfe) != 18);
}

/* Whether host is a name that the resolver answered with an address no
   name of a real service has: this network (0/8), loopback (127/8) or
   link-local (169.254/16, where the clouds keep the metadata service that
   hands out the machine's credentials). That answer is what DNS rebinding
   looks like -- a name its owner, or an on-path forger of plain DNS, points
   at the machine itself so a fetch of an untrusted URL reads what only the
   machine may -- and STRICT_TIGHT refuses it on every hop, the first one
   too. An address written in the URL is the writer's own choice and is
   taken, as is localhost, which never asks a resolver. A name inside the
   network (10/8, 172.16/12, 192.168/16, 100.64/10) is ordinary on an
   intranet, and only the redirect rule keeps a chain that has reached public
   space from going back to one. */
static bool http_name_rebound(string_address host, p32 ip)
{
        p8 a = (p8)(ip >> 24);

        return string_to_host(host) < 0 && !http_host_loopback(host) &&
               (!a || a == 127 || (a == 169 && (p8)(ip >> 16) == 254));
}

#define TLS_AIA_SECONDS 5
/* What a CRL's store grows by while its length is not yet known. */
#define TLS_CRL_STEP ((positive)1 << 16)

/*
        Revocation: stapling and CRLs, in tiers.

        Each certificate on a verified path below its anchor is asked about
        by the issuer that signed it. The leaf first by the OCSP response its
        server stapled (RFC 6066 8, RFC 8446 4.4.2.1), when it stapled one;
        then -- the leaf without a good staple, and every certificate above
        it -- by the CRL its first whole-reason http: distribution point
        names (RFC 5280 6.3), fetched under caIssuers' rules and held in
        memory for the rest of the process (tls_crl_get).

        An answer is good, revoked, none (a source there is that said
        nothing usable: unreachable, too large, unparseable, signed by
        nobody it may be, about another certificate, unknown, or stale) or
        absent (no source: no staple and no distribution point). Revoked
        refuses in every tier. A leaf that asks for a staple (TLS Feature
        status_request, RFC 7633) and has no good one is refused in every
        tier too. None is passed over at the default tier, as browsers that
        still fetch revocation pass over it, and refuses at the tight one.
        Absent is passed in both: a certificate that names no CRL and whose
        server staples nothing has no source to fail, and OCSP is never
        fetched (only stapled), so a responder URL alone is not a source.

        Ceilings: TLS_REVOCATION_SECONDS for every fetch of one handshake
        together (inside the handshake's own deadline; each name lookup has
        its own three seconds), TLS_CRL_MAX bytes a CRL, TLS_CRL_HELD CRLs and
        TLS_CRL_HELD_BYTES held, a failed fetch not tried again for
        TLS_CRL_RETRY seconds.
*/
#define TLS_ANSWER_GOOD 0
#define TLS_ANSWER_REVOKED 1
#define TLS_ANSWER_NONE 2
#define TLS_ANSWER_ABSENT 3
#define TLS_REVOCATION_SECONDS 10
#define TLS_CRL_MAX ((positive)16 << 20)
#define TLS_CRL_HELD 8
#define TLS_CRL_HELD_BYTES ((positive)32 << 20)
#define TLS_CRL_RETRY 300

/* The plain HTTP fetch, defined beside tls_aia_fetch below. */
static COLD positive tls_plain_get(const p8 address_to url, positive length,
                                   byte_store address_to into, positive seconds,
                                   const network_deadline address_to outer);

/* SHA-1 (kind 1, 20 bytes) or SHA-256 (kind 2, 32), the two an OCSP CertID
   names its issuer by. */
static COLD fn tls_revocation_hash(p8 kind, const p8 address_to bytes,
                                   positive length, p8 address_to out)
{
        digest_state digest;

        digest_open(address_of digest, kind == 1 ? DIGEST_SHA1 : DIGEST_SHA256,
                    kind == 1 ? 20 : 32);
        digest_write(address_of digest, (address_any)bytes, length);
        digest_close(address_of digest, out);
}

/* Whether signer's key signed tbs: tls_verify_one, given the signed part of
   an OCSP response or a CRL laid out as a certificate's. */
static COLD bool tls_signed_by(p8 address_to tbs, positive tbs_length,
                               p8 address_to sig_oid, positive sig_oid_length,
                               const p8 address_to sig, positive sig_length,
                               tls_cert address_to signer)
{
        tls_cert part;

        memory_fill(address_of part, 0, sizeof part);
        part.tbs = tbs;
        part.tbs_length = tbs_length;
        part.sig_oid = sig_oid;
        part.sig_oid_length = sig_oid_length;
        part.sig = (p8 address_to)sig;
        part.sig_length = sig_length;
        return tls_verify_one(address_of part, signer);
}

/*
        Certificate Transparency (RFC 6962), at the tight tier.

        A certificate a public CA issues is logged before it is issued: the CA
        sends a precertificate to logs, each answers with a signed certificate
        timestamp (SCT), and the CA embeds the SCTs in the certificate (the
        extension 1.3.6.1.4.1.11129.2.4.2). A certificate nobody logged -- one a
        CA was made to issue quietly, or a forgery under a stolen key -- has
        none that verify, and is the certificate CT exists to expose.
        tls_ct_holds asks what Chrome asks (its CT policy, embedded SCTs):
        every SCT is checked against the log it names in tls_ct_logs, over the
        precertificate it signed -- the leaf's TBSCertificate without the SCT
        list, and the SHA-256 of its issuer's SubjectPublicKeyInfo -- and
        counted only from a log that is usable, qualified or read-only, or
        retired after the SCT was made, and whose temporal interval holds the
        leaf's notAfter, made no later than now. Counted SCTs have to come from
        two operators at least, at least one from a log that has not retired,
        and number two for a leaf of 180 days or less and three for a longer
        one.

        SCTs a server sends in TLS or in a stapled OCSP response are not read:
        a public certificate carries its own. The default and reference tiers
        do not ask, as GNU wget and curl do not; a chain that ends at the
        harness's own root (TLS_BENCH_ANCHOR) is asked only when the harness
        names logs of its own (TLS_BENCH_CT).
*/
typedef struct
{
        p8 id[32];
        p8 x[32];
        p8 y[32];
        p8 operator;
        p64 retired; // milliseconds since the epoch; 0 while it is not
        p64 from;    // the notAfter interval it takes, milliseconds; 0, 0 for any
        p64 until;
} tls_ct_log;

#include "ct_logs.inc"
#ifdef TLS_BENCH_CT
#include TLS_BENCH_CT
#endif

#define TLS_CT_SCTS_MOST 16
#define TLS_CT_SHORT_DAYS 180

//      The log an SCT names, or null.
static COLD const tls_ct_log address_to tls_ct_log_find(const p8 address_to id)
{
        for (positive at = 0; at < array_count(tls_ct_logs); at++)
                if (!memory_compare(tls_ct_logs[at].id, id, 32))
                        return tls_ct_logs + at;
#ifdef TLS_BENCH_CT
        for (positive at = 0; at < array_count(tls_bench_ct_logs); at++)
                if (!memory_compare(tls_bench_ct_logs[at].id, id, 32))
                        return tls_bench_ct_logs + at;
#endif
        return null;
}

/*
        The precertificate's TBSCertificate an embedded SCT signed: the leaf's,
        with the SCT list extension taken out and the lengths of the
        extensions, their [3] and the TBS written again. Its length, or 0 when
        the leaf carries no list or nothing would be left of its extensions.
        out has room for the leaf's own TBS, which this never exceeds.
*/
static COLD positive tls_ct_precert(const tls_cert address_to leaf, p8 address_to out,
                                    positive room)
{
        p8 address_to tbs = leaf->tbs;
        positive tbs_length = leaf->tbs_length;
        positive at = 0;
        positive tbs_stop = 0;
        positive wrap_stop = 0;
        positive list_at;
        positive list_stop = 0;
        positive cut;
        positive cut_length = leaf->sct_extension_length;
        positive prefix;
        positive kept;
        positive list_header;
        positive wrap_header;
        positive used;

        if (!leaf->scts || !leaf->extensions || leaf->extensions < tbs ||
            leaf->sct_extension < leaf->extensions ||
            tls_asn1_enter(tbs, tbs_length, 0x30, address_of at, address_of tbs_stop) ||
            tbs_stop != tbs_length)
                return 0;
        prefix = (positive)(leaf->extensions - tbs) - at;
        at = (positive)(leaf->extensions - tbs);
        if (tls_asn1_enter(tbs, tbs_stop, 0xa3, address_of at, address_of wrap_stop) ||
            wrap_stop != tbs_stop)
                return 0;
        list_at = at;
        if (tls_asn1_enter(tbs, wrap_stop, 0x30, address_of list_at, address_of list_stop) ||
            list_stop != wrap_stop)
                return 0;
        cut = (positive)(leaf->sct_extension - tbs);
        if (cut < list_at || cut_length > list_stop - cut)
                return 0;
        kept = list_stop - list_at - cut_length;
        if (!kept)
                return 0;
        list_header = tls_der_header_size(kept);
        wrap_header = tls_der_header_size(list_header + kept);
        used = prefix + wrap_header + list_header + kept;
        at = tls_der_header_size(used);
        if (at + used > room)
                return 0;
        tls_der_header(out, 0x30, used);
        //      Everything before the extensions, as it was.
        memory_copy(out + at, tbs + (positive)(leaf->extensions - tbs) - prefix, prefix);
        at += prefix;
        at += tls_der_header(out + at, 0xa3, list_header + kept);
        at += tls_der_header(out + at, 0x30, kept);
        memory_copy(out + at, tbs + list_at, cut - list_at);
        at += cut - list_at;
        memory_copy(out + at, tbs + cut + cut_length, list_stop - cut - cut_length);
        at += list_stop - cut - cut_length;
        return at;
}

/*
        One SCT read (RFC 6962 3.2): v1, the log's id, its timestamp in
        milliseconds, its extensions, and an ECDSA signature with SHA-256 as
        a DER Ecdsa-Sig-Value. False for any other shape.
*/
typedef struct
{
        const p8 address_to id;
        p64 timestamp;
        const p8 address_to extensions;
        positive extensions_length;
        const p8 address_to signature;
        positive signature_length;
} tls_ct_sct;

static COLD bool tls_ct_sct_read(byte_reader address_to reader, tls_ct_sct address_to sct)
{
        byte_reader extensions;
        byte_reader signature;
        p32 high;

        if (byte_reader_u8(reader) != 0)
                return false;
        sct->id = byte_reader_take(reader, 32);
        high = byte_reader_u32(reader);
        sct->timestamp = (p64)high << 32 | byte_reader_u32(reader);
        extensions = byte_reader_vector16(reader);
        sct->extensions = byte_reader_here(address_of extensions);
        sct->extensions_length = byte_reader_left(address_of extensions);
        //      SHA-256 (4) and ECDSA (3), the one pair every log uses.
        if (byte_reader_u8(reader) != 4 || byte_reader_u8(reader) != 3)
                return false;
        signature = byte_reader_vector16(reader);
        sct->signature = byte_reader_here(address_of signature);
        sct->signature_length = byte_reader_left(address_of signature);
        return byte_reader_ok(reader) && byte_reader_end(reader) && sct->id &&
               byte_reader_ok(address_of extensions) && byte_reader_ok(address_of signature) &&
               sct->signature_length;
}

/*
        Whether one SCT verifies under its log's key over the precertificate:
        sct_version 0, signature_type certificate_timestamp (0), the
        timestamp, entry_type precert_entry (1), the issuer key hash, the TBS
        with a 24-bit length, the SCT's extensions with a 16-bit one.
*/
static COLD bool tls_ct_sct_verifies(const tls_ct_sct address_to sct,
                                     const tls_ct_log address_to log,
                                     const p8 address_to issuer_key_hash,
                                     const p8 address_to precert, positive precert_length,
                                     byte_store address_to signed_bytes)
{
        static const p8 ecdsa_sha256[8] = {0x2a, 0x86, 0x48, 0xce, 0x3d, 0x04, 0x03, 0x02};
        p8 head[12];
        p8 lengths[3];
        tls_cert signer;

        signed_bytes->used = 0;
        head[0] = 0;
        head[1] = 0;
        network_store_64(head + 2, sct->timestamp);
        head[10] = 0;
        head[11] = 1;
        lengths[0] = (p8)(precert_length >> 16);
        lengths[1] = (p8)(precert_length >> 8);
        lengths[2] = (p8)precert_length;
        if (precert_length >= 1u << 24 ||
            !byte_store_reserve(signed_bytes,
                                sizeof head + 32 + 3 + precert_length + 2 +
                                    sct->extensions_length,
                                4096) ||
            !byte_store_append_exact(signed_bytes, head, sizeof head) ||
            !byte_store_append_exact(signed_bytes, issuer_key_hash, 32) ||
            !byte_store_append_exact(signed_bytes, lengths, 3) ||
            !byte_store_append_exact(signed_bytes, precert, precert_length))
                return false;
        lengths[0] = (p8)(sct->extensions_length >> 8);
        lengths[1] = (p8)sct->extensions_length;
        if (!byte_store_append_exact(signed_bytes, lengths, 2) ||
            !byte_store_append_exact(signed_bytes, sct->extensions, sct->extensions_length))
                return false;
        memory_fill(address_of signer, 0, sizeof signer);
        signer.curve = 1;
        memory_copy(signer.qx + 16, log->x, 32);
        memory_copy(signer.qy + 16, log->y, 32);
        return tls_signed_by(signed_bytes->bytes, signed_bytes->used, (p8 address_to)ecdsa_sha256,
                             sizeof ecdsa_sha256, sct->signature, sct->signature_length,
                             address_of signer);
}

/* Whether a counted SCT's log may count it now: not retired, or retired
   after it was made; taking the leaf's notAfter; made by now. */
static PURE COLD bool tls_ct_log_counts(const tls_ct_log address_to log, p64 timestamp,
                                         p64 not_after_ms, p64 now_ms)
{
        return timestamp <= now_ms && (!log->retired || timestamp < log->retired) &&
               (!log->until || (not_after_ms >= log->from && not_after_ms < log->until));
}

static COLD bool tls_ct_holds(tls_cert address_to certs, const positive address_to path,
                              positive depth, tls_cert address_to top)
{
        tls_cert address_to leaf = certs;
        tls_cert address_to issuer = depth ? certs + path[1] : top;
        byte_store precert = {0};
        byte_store signed_bytes = {0};
        byte_reader list;
        byte_reader entries;
        p8 issuer_key_hash[32];
        p8 operators[TLS_CT_SCTS_MOST];
        positive counted = 0;
        positive distinct = 0;
        bool current = false;
        p64 now_ms;
        p64 not_before_ms;
        p64 not_after_ms;
        positive length;
        positive seen = 0;
        bool held;

#ifdef TLS_BENCH_ANCHOR
#ifndef TLS_BENCH_CT
        //      The harness's own root, with no logs of its own to ask.
        if (top->curve == 2 && !memory_compare(top->qx, tls_bench_anchor_x, 48) &&
            !memory_compare(top->qy, tls_bench_anchor_y, 48))
                return true;
#endif
#endif
        if (!leaf->scts || !issuer->spki || !issuer->spki_length)
                return false;
        //      tls_date_seconds counts from day zero of its calendar, not
        //      from the epoch an SCT's timestamp counts from.
        now_ms = (p64)time(null) * 1000;
        not_before_ms = (tls_date_seconds(leaf->not_before) -
                         tls_date_seconds(19700101000000ull)) * 1000;
        not_after_ms = (tls_date_seconds(leaf->not_after) -
                        tls_date_seconds(19700101000000ull)) * 1000;
        crypto_sha256_of(issuer->spki, issuer->spki_length, issuer_key_hash);
        if (!byte_store_reserve(address_of precert, leaf->tbs_length, 4096))
                return false;
        length = tls_ct_precert(leaf, precert.bytes, leaf->tbs_length);
        list = byte_reader_open(leaf->scts, leaf->scts_length);
        entries = byte_reader_vector16(address_of list);
        if (!length || !byte_reader_ok(address_of list) || !byte_reader_end(address_of list))
        {
                byte_store_release(address_of precert);
                return false;
        }
        while (byte_reader_left(address_of entries) && seen++ < TLS_CT_SCTS_MOST)
        {
                byte_reader one = byte_reader_vector16(address_of entries);
                tls_ct_sct sct;
                const tls_ct_log address_to log;
                bool known = false;

                if (!byte_reader_ok(address_of entries) ||
                    !tls_ct_sct_read(address_of one, address_of sct))
                        break;
                log = tls_ct_log_find(sct.id);
                if (!log || !tls_ct_log_counts(log, sct.timestamp, not_after_ms, now_ms) ||
                    !tls_ct_sct_verifies(address_of sct, log, issuer_key_hash, precert.bytes,
                                         length, address_of signed_bytes))
                        continue;
                counted++;
                current |= !log->retired;
                for (positive at = 0; at < distinct; at++)
                        known |= operators[at] == log->operator;
                if (!known)
                        operators[distinct++] = log->operator;
        }
        held = byte_reader_ok(address_of entries) && distinct >= 2 && current &&
               counted >= (not_after_ms - not_before_ms <=
                                   (p64)TLS_CT_SHORT_DAYS * 86400 * 1000
                               ? 2u
                               : 3u);
        byte_store_release(address_of precert);
        byte_store_release(address_of signed_bytes);
        return held;
}

/* What a stapled OCSP response says of leaf, which issuer signed (RFC 6960
   3.2, 4.2.2.2). It has to parse (tls_parse_ocsp); exactly one of its
   SingleResponses has to name leaf -- the hash of leaf's issuer Name and of
   issuer's key under the CertID's hash, and leaf's serial; and it has to be
   signed by issuer, or by a delegated responder among its certificates:
   issued by issuer (the Name leaf names, issuer's signature), with
   id-kp-OCSPSigning, current, a digitalSignature key if it limits its key
   at all, and the responder the response names (byName its subject, byKey
   the SHA-1 of its key). Revoked so signed is revoked whenever it was said;
   good has to be fresh (tls_revocation_fresh); unknown, stale or anything
   short of the above is none. */
static COLD p8 tls_ocsp_answer(tls_cert address_to leaf, tls_cert address_to issuer,
                               const p8 address_to response, positive length,
                               p64 now)
{
        p8 key[32 + CRYPTO_RSA_BYTES];
        p8 names[2][32];
        p8 keys[2][32];
        positive key_length = tls_key_bits(issuer, key, sizeof key);
        positive matched = 0;
        tls_ocsp ocsp;
        tls_ocsp_single single;
        tls_ocsp_single found;
        tls_cert responder;
        tls_cert address_to signer = null;
        byte_reader list;

        if (!key_length || !leaf->issuer || !leaf->serial ||
            tls_parse_ocsp((p8 address_to)response, length, address_of ocsp))
                return TLS_ANSWER_NONE;
        for (p8 kind = 1; kind <= 2; kind++)
        {
                tls_revocation_hash(kind, leaf->issuer, leaf->issuer_length,
                                    names[kind - 1]);
                tls_revocation_hash(kind, key, key_length, keys[kind - 1]);
        }
        memory_fill(address_of found, 0, sizeof found);
        list = byte_reader_open(ocsp.responses, ocsp.responses_length);
        while (byte_reader_left(&list))
        {
                positive size = 0;
                const p8 address_to whole = tls_der_whole(&list, 0x30, address_of size);

                if (!whole || tls_parse_ocsp_single(whole, size, address_of single))
                        return TLS_ANSWER_NONE;
                if (!memory_compare(single.name_hash, names[single.hash - 1],
                                    single.hash_length) &&
                    !memory_compare(single.key_hash, keys[single.hash - 1],
                                    single.hash_length) &&
                    single.serial_length == leaf->serial_length &&
                    !memory_compare(single.serial, leaf->serial, leaf->serial_length))
                {
                        found = single;
                        matched++;
                }
        }
        //      None names leaf, or two do, which could disagree.
        if (matched != 1)
                return TLS_ANSWER_NONE;

        if (ocsp.by_key ? !memory_compare(ocsp.responder, keys[0], 20)
                        : ocsp.responder_length == leaf->issuer_length &&
                              !memory_compare(ocsp.responder, leaf->issuer,
                                              leaf->issuer_length))
                signer = issuer;
        list = byte_reader_open(ocsp.certs, ocsp.certs_length);
        while (!signer && byte_reader_left(&list))
        {
                positive size = 0;
                p8 address_to whole =
                    (p8 address_to)tls_der_whole(&list, 0x30, address_of size);
                p8 named[20];

                if (!whole)
                        break;
                if (tls_parse_cert(whole, size, address_of responder, null) ||
                    responder.issuer_length != leaf->issuer_length ||
                    memory_compare(responder.issuer, leaf->issuer,
                                   leaf->issuer_length) ||
                    !responder.ocsp_signing ||
                    !tls_cert_current(address_of responder, now) ||
                    (responder.key_usage && !responder.digital_signature))
                        continue;
                if (ocsp.by_key)
                {
                        p8 bits[32 + CRYPTO_RSA_BYTES];
                        positive bits_length = tls_key_bits(address_of responder, bits,
                                                            sizeof bits);

                        tls_revocation_hash(1, bits, bits_length, named);
                        if (!bits_length || memory_compare(named, ocsp.responder, 20))
                                continue;
                }
                else if (responder.subject_length != ocsp.responder_length ||
                         memory_compare(responder.subject, ocsp.responder,
                                        ocsp.responder_length))
                        continue;
                if (tls_verify_one(address_of responder, issuer))
                        signer = address_of responder;
        }
        if (!signer || !tls_signed_by(ocsp.tbs, ocsp.tbs_length, ocsp.sig_oid,
                                      ocsp.sig_oid_length, ocsp.sig,
                                      ocsp.sig_length, signer))
                return TLS_ANSWER_NONE;
        if (found.status == TLS_OCSP_REVOKED)
                return TLS_ANSWER_REVOKED;
        return found.status == TLS_OCSP_GOOD &&
                       tls_revocation_fresh(found.this_update, found.next_update,
                                            now)
                   ? TLS_ANSWER_GOOD
                   : TLS_ANSWER_NONE;
}

/*
        The CRLs this process fetched, by the SHA-256 of their URL: the bytes
        (or a failed fetch, not tried again for TLS_CRL_RETRY seconds), and
        once one verifies, the SHA-256 of the key it verified under and its
        nextUpdate. Memory only: the fetch may run where no file can be
        opened, and a CRL is a few seconds' download at worst. A CRL past its
        nextUpdate is fetched again; the least recently used goes when a new
        one would hold more than TLS_CRL_HELD of them or TLS_CRL_HELD_BYTES.
*/
typedef struct
{
        byte_store crl;
        p8 name[32];
        p8 signer[32];
        p64 next_update;
        p64 fetched;
        positive age;
        bool used;
        bool failed;
} tls_crl_held;

static tls_crl_held tls_crl_cache[TLS_CRL_HELD];
static positive tls_crl_clock;

static COLD fn tls_crl_drop(tls_crl_held address_to held)
{
        byte_store_release(address_of held->crl);
        memory_fill(held, 0, sizeof(address_to held));
}

static COLD tls_crl_held address_to tls_crl_get(const p8 address_to url,
                                               positive length, p64 now,
                                               const network_deadline address_to deadline)
{
        p8 name[32];
        p64 at = tls_date_seconds(now);
        byte_store fetched = {null, 0, 0};
        tls_crl_held address_to slot = null;
        positive seconds;
        positive nanoseconds;
        bool got;

        crypto_sha256_of((p8 address_to)url, length, name);
        for (positive k = 0; k < TLS_CRL_HELD; k++)
        {
                tls_crl_held address_to held = tls_crl_cache + k;

                if (!held->used || memory_compare(held->name, name, 32))
                        continue;
                if (held->failed ? held->fetched + TLS_CRL_RETRY > at
                                 : !held->next_update ||
                                       tls_date_seconds(held->next_update) +
                                               TLS_REVOCATION_SKEW >= at)
                {
                        held->age = ++tls_crl_clock;
                        return held;
                }
                tls_crl_drop(held);
        }
        //      A budget spent by an earlier URL is no attempt: nothing is
        //      looked up, and nothing is held as failed for TLS_CRL_RETRY.
        if (!deadline || !network_deadline_left(deadline, address_of seconds,
                                                address_of nanoseconds))
                return null;
        got = tls_plain_get(url, length, address_of fetched, TLS_REVOCATION_SECONDS,
                            deadline) != 0;
        if (!got)
                byte_store_release(address_of fetched);
        for (;;)
        {
                positive bytes = fetched.used;
                tls_crl_held address_to oldest = null;

                slot = null;
                for (positive k = 0; k < TLS_CRL_HELD; k++)
                {
                        tls_crl_held address_to held = tls_crl_cache + k;

                        if (!held->used)
                        {
                                slot = slot ? slot : held;
                                continue;
                        }
                        bytes += held->crl.used;
                        if (!oldest || held->age < oldest->age)
                                oldest = held;
                }
                if (slot && bytes <= TLS_CRL_HELD_BYTES)
                        break;
                if (!oldest)
                {
                        byte_store_release(address_of fetched);
                        return null;
                }
                tls_crl_drop(oldest);
        }
        slot->crl = fetched;
        memory_copy(slot->name, name, 32);
        slot->fetched = at;
        slot->age = ++tls_crl_clock;
        slot->used = true;
        slot->failed = !got;
        return slot;
}

/* What the CRL at one URL says of cert, which issuer signed (RFC 5280
   6.3.3): fetched or held; parsed with cert's serial looked up
   (tls_parse_crl); issued under the Name cert names and, when both carry one,
   the same authority key identifier; signed by issuer, whose key's SHA-256
   is signer; of a scope that covers cert (tls_crl_covers). Listed so signed
   is revoked; otherwise good when fresh; none for any failure. */
static COLD p8 tls_crl_answer_at(tls_cert address_to cert,
                                 tls_cert address_to issuer,
                                 const p8 address_to url, positive length,
                                 const p8 address_to signer, p64 now,
                                 const network_deadline address_to deadline)
{
        tls_crl_held address_to held = tls_crl_get(url, length, now, deadline);
        tls_crl crl;

        if (!held || held->failed ||
            tls_parse_crl(held->crl.bytes, held->crl.used, cert->serial,
                          cert->serial_length, address_of crl) ||
            crl.issuer_length != cert->issuer_length ||
            memory_compare(crl.issuer, cert->issuer, cert->issuer_length) ||
            (crl.authority_key && cert->authority_key &&
             (crl.authority_key_length != cert->authority_key_length ||
              memory_compare(crl.authority_key, cert->authority_key,
                             crl.authority_key_length))) ||
            !tls_crl_covers(address_of crl.scope, cert))
                return TLS_ANSWER_NONE;
        if (memory_compare(held->signer, signer, 32))
        {
                if (!tls_signed_by(crl.tbs, crl.tbs_length, crl.sig_oid,
                                   crl.sig_oid_length, crl.sig, crl.sig_length,
                                   issuer))
                        return TLS_ANSWER_NONE;
                memory_copy(held->signer, signer, 32);
                held->next_update = crl.next_update;
        }
        if (crl.listed)
                return TLS_ANSWER_REVOKED;
        return tls_revocation_fresh(crl.this_update, crl.next_update, now)
                   ? TLS_ANSWER_GOOD
                   : TLS_ANSWER_NONE;
}

/* What cert's CRL says of it: each http: URI of the distribution point it
   names, in order, until one answers (tls_crl_answer_at), at most
   TLS_CRL_POINTS of them. A CA lists mirrors there, and one may refuse a
   plain GET that another serves: Microsoft's www.microsoft.com answers 403
   where crl2.microsoft.com serves the same CRL. The issuer has to be one
   whose key may sign CRLs (cRLSign, when it limits its key at all). Absent
   when cert names no CRL, none when no URI answers. */
#define TLS_CRL_POINTS 4

static COLD p8 tls_crl_answer(tls_cert address_to cert, tls_cert address_to issuer,
                              p64 now, const network_deadline address_to deadline)
{
        p8 key[32 + CRYPTO_RSA_BYTES];
        p8 signer[32];
        positive key_length;
        positive tried = 0;
        byte_reader names;

        if (!cert->crl_url)
                return TLS_ANSWER_ABSENT;
        key_length = tls_key_bits(issuer, key, sizeof key);
        if (!key_length || !cert->issuer || !cert->serial ||
            (issuer->key_usage && !issuer->crl_sign))
                return TLS_ANSWER_NONE;
        crypto_sha256_of(key, key_length, signer);
        names = byte_reader_open(cert->crl_names, cert->crl_names_length);
        while (byte_reader_left(&names) && tried < TLS_CRL_POINTS)
        {
                p8 tag = tls_der_peek(names);
                byte_reader uri = tls_der_value(&names, tag);
                const p8 address_to text = byte_reader_here(&uri);
                positive size = byte_reader_left(&uri);
                p8 answer;

                if (tag != 0x86 || size <= 7 || !tls_printable(text, size) ||
                    memory_compare_ascii_case(text, "http://", 7))
                        continue;
                tried++;
                answer = tls_crl_answer_at(cert, issuer, text, size, signer, now,
                                           deadline);
                if (answer != TLS_ANSWER_NONE)
                        return answer;
        }
        return TLS_ANSWER_NONE;
}

/* The tiers over a verified path: path[0] the leaf, path[depth] the last
   certificate below the anchor, top the certificate or anchor that signed
   that one. False with tls->fault saying why when the path may not be
   used: revoked, or unchecked where the answer was required. */
static COLD bool tls_revocation_holds(tls_conn address_to tls,
                                      tls_cert address_to certs,
                                      const positive address_to path,
                                      positive depth, tls_cert address_to top,
                                      const p8 address_to staple,
                                      positive staple_length, p64 now)
{
        network_deadline deadline;
        bool timed = network_deadline_begin_within(address_of deadline,
                                                   TLS_REVOCATION_SECONDS, 0,
                                                   tls->deadline);
        bool tight = MOONWATER_STRICT_NET >= STRICT_TIGHT;

        for (positive k = 0; k <= depth; k++)
        {
                tls_cert address_to cert = certs + path[k];
                tls_cert address_to issuer = k < depth ? certs + path[k + 1] : top;
                p8 answer = TLS_ANSWER_ABSENT;

                if (!k && staple)
                        answer = tls_ocsp_answer(cert, issuer, staple, staple_length,
                                                 now);
                if (answer == TLS_ANSWER_GOOD)
                        continue;
                if (answer == TLS_ANSWER_REVOKED)
                        goto revoked;
                if ((!k && cert->must_staple) || (answer == TLS_ANSWER_NONE && tight))
                        goto unchecked;
                answer = tls_crl_answer(cert, issuer, now,
                                        timed ? address_of deadline : null);
                if (answer == TLS_ANSWER_REVOKED)
                        goto revoked;
                if (answer == TLS_ANSWER_NONE && tight)
                        goto unchecked;
        }
        return true;

revoked:
        tls->fault = TLS_FAULT_REVOKED;
        return false;
unchecked:
        tls->fault = TLS_FAULT_UNCHECKED;
        return false;
}

/*
        tls_verify_chain's two fetches over plain HTTP: the certificate a
        caIssuers location names (RFC 5280 4.2.2.1), which browsers fetch
        when a server leaves an intermediate out, and the CRL a distribution
        point names (RFC 5280 4.2.1.13). The URL comes from a certificate,
        and for caIssuers one nothing has verified yet, so the peer may
        choose the destination, and the fetch is held to what a certificate
        or a CRL needs: http: only (https would be a handshake inside a
        handshake), port 80 unless a harness serves it, no userinfo, a public
        address, one HTTP/1.0 GET with no redirect followed, seconds for the
        connection and the reply together -- inside outer, when there is one
        -- a 200, and a body of at most most bytes, which the caller parses
        and verifies -- DER only, so a PKCS#7 bundle or PEM text is refused
        there. A store given room holds what it was given (caIssuers' room on
        the stack), at most that many bytes; one given none grows on the heap
        to hold the head and TLS_CRL_MAX bytes of body (a CRL's). Returns the
        body's length at the front of the store, or 0.
*/
static COLD positive tls_plain_get(const p8 address_to url, positive length,
                                   byte_store address_to into, positive seconds,
                                   const network_deadline address_to outer)
{
        positive most = into->room ? into->room : TLS_CRL_MAX;
        bool grow = !into->room;
        p8 text[HTTP_URL_MAX];
        p8 host[256];
        p8 request[HTTP_URL_MAX + 256];
        string_address path;
        p16 port;
        bool tls;
        p32 ip;
        positive sent = 0;
        positive header = 0;
        positive left = 0;
        positive part = 0;
        http_response response;
        network_deadline deadline;
        bipolar handle;
        bool closed = false;

        into->used = 0;
        if (length >= sizeof text)
                return 0;
        memory_copy(text, url, length);
        text[length] = end;
        if (http_split_into((string_address)text, host, sizeof host,
                            address_of port, address_of path, address_of tls) ||
            tls ||
#ifndef TLS_BENCH_ANCHOR
            port != HTTP_PORT ||
#endif
            http_get_request(request, sizeof request, (string_address)host, port,
                             path, false, '0', (string_address)"Wget",
                             address_of sent) ||
            !(ip = http_lookup((string_address)host)) || !http_address_public(ip) ||
            !network_deadline_begin_within(address_of deadline, seconds, 0, outer) ||
            !network_deadline_left(address_of deadline, address_of left,
                                   address_of part) ||
            (grow && !byte_store_reserve(into, TLS_CRL_STEP, TLS_CRL_STEP)))
                return 0;
        handle = http_stream_open(ip, port, left + (part != 0));
        if (handle < 0)
                return 0;
        if (network_stream_send_all_until(handle, request, sent,
                                           address_of deadline))
                for (;;)
                {
                        bipolar got;
                        bool framed = !http_response_framing(into->bytes, into->used,
                                                             address_of header,
                                                             address_of response);
                        positive wanted;

                        if (framed && response.body_kind == HTTP_BODY_LENGTH &&
                            (response.body_length > most ||
                             into->used - header >= response.body_length))
                                break;
                        if (framed && into->used - header > most)
                                break;
                        if (into->used == into->room)
                        {
                                wanted = framed && response.body_kind == HTTP_BODY_LENGTH
                                             ? header + response.body_length
                                             : into->room + TLS_CRL_STEP;
                                if (!grow || wanted > most + HTTP_HEAD_MAX ||
                                    !byte_store_reserve(into, wanted, TLS_CRL_STEP))
                                        break;
                        }
                        got = network_stream_read_some_until(
                            handle, into->bytes + into->used, into->room - into->used,
                            address_of deadline);
                        if (got <= 0)
                        {
                                closed = !got;
                                break;
                        }
                        into->used += (positive)got;
                }
        socket_close((b32)handle);
        if (http_response_framing(into->bytes, into->used, address_of header,
                                  address_of response) ||
            response.code != 200 || into->used - header > most ||
            (response.body_kind == HTTP_BODY_LENGTH
                 ? into->used - header != response.body_length
                 : response.body_kind != HTTP_BODY_CLOSE || !closed))
        {
                into->used = 0;
                return 0;
        }
        memory_copy(into->bytes, into->bytes + header, into->used - header);
        into->used -= header;
        return into->used;
}

static COLD positive tls_aia_fetch(const p8 address_to url, positive length,
                                   p8 address_to into, positive room)
{
        byte_store store = {into, room, 0};

        return tls_plain_get(url, length, address_of store, TLS_AIA_SECONDS, null);
}

/*
        Network Time Security, key establishment: RFC 8915 4.

        The NTS-KE connection is TLS 1.3 to the server's port (4460), the
        ALPN protocol "ntske/1", one request of three records (NTPv4 is the
        protocol asked for, AEAD_AES_SIV_CMAC_256 the algorithm) and one
        reply: the two it chose, cookies to spend one at a time, and
        optionally another server and port for the time itself. The keys
        are not in the reply. Both ends derive them from the finished
        handshake with the TLS exporter (RFC 8446 7.5) under the label
        "EXPORTER-network-time-security", context the protocol number, the
        algorithm number and one byte for the direction.

        nts_ke_reply is the reply's parser alone, fed bytes from anywhere
        (nts_fuzz): critical records it does not know fail, an error or a
        warning record fails, a number is taken only as the one it asked
        for, a name only as a host name, a port not at zero, and nothing
        follows the end record. A cookie too long for this client's room is
        passed over, since a server can issue any length; with none that
        fit, the exchange fails.

        Whether NTS is asked at all is a word in /root/ntp.nts: on, off, or
        only (NTS and nothing the path could have forged). A word that is
        none of them is the tier's default: on, and at the tight tier only
        (nts_mode_of), so a hardened or locked image never takes plain time
        unless someone said so.
*/
#define NTS_KE_PORT 4460
#define NTS_COOKIES 8
#define NTS_COOKIE_MAX 256
#define NTS_SERVER_MAX 64
#define NTS_AEAD_SIV_CMAC_256 15
#define NTS_NTP_PORT 123
#define NTS_KE_ROOM 4096
#define NTS_KE_SECONDS 6
#define NTS_OFF 0
#define NTS_ON 1
#define NTS_ONLY 2
#if MOONWATER_STRICT_NET >= STRICT_TIGHT
#define NTS_MODE_DEFAULT NTS_ONLY
#else
#define NTS_MODE_DEFAULT NTS_ON
#endif

typedef struct
{
        p8 c2s[32];
        p8 s2c[32];
        /* The key-establishment server this came from, and the time server
           (and port) the exchange named, empty and 123 when it named none. */
        p8 origin[NTS_SERVER_MAX];
        p16 origin_port;
        p8 server[NTS_SERVER_MAX];
        p16 port;
        positive count;
        p16 length[NTS_COOKIES];
        p8 cookie[NTS_COOKIES][NTS_COOKIE_MAX];
} nts_state;

//      The word in /root/ntp.nts as a mode: the tier's default for anything
//      that is not on, off or only, a missing file included.
static COLD positive nts_mode_of(string_address word)
{
        return string_equals(word, "off")    ? NTS_OFF
               : string_equals(word, "on")   ? NTS_ON
               : string_equals(word, "only") ? NTS_ONLY
                                             : NTS_MODE_DEFAULT;
}

/* A host name, or a dotted address: labels of letters, digits and hyphens
   between dots, none empty and none beginning or ending with a hyphen. */
static COLD bool nts_name_ok(const p8 address_to name, positive length)
{
        positive label = 0;

        if (!length || length >= NTS_SERVER_MAX)
                return false;
        for (positive at = 0; at < length; at++)
        {
                if (name[at] == '.')
                {
                        if (!label || name[at - 1] == '-')
                                return false;
                        label = 0;
                }
                else if (byte_is_alnum(name[at]) || (name[at] == '-' && label))
                        label++;
                else
                        return false;
        }
        return label && name[length - 1] != '-';
}

//      Whether the reply so far holds its end record, in whole records.
static COLD bool nts_ke_complete(const p8 address_to data, positive length)
{
        byte_reader reader = byte_reader_open((p8 address_to)data, length);

        while (byte_reader_left(&reader) >= 4)
        {
                p16 type = byte_reader_u16(&reader);

                (void)byte_reader_vector16(&reader);
                if (!byte_reader_ok(&reader))
                        return false;
                if ((type & 0x7fff) == 0)
                        return true;
        }
        return false;
}

#define NTS_KE_OK 0
#define NTS_KE_MALFORMED (-1)
#define NTS_KE_ERROR (-2)

static COLD bipolar nts_ke_reply(const p8 address_to data, positive length,
                                 nts_state address_to state)
{
        byte_reader reader = byte_reader_open((p8 address_to)data, length);
        bool protocol = false;
        bool algorithm = false;
        bool named = false;
        bool ported = false;
        bool done = false;

        state->count = 0;
        state->server[0] = end;
        state->port = NTS_NTP_PORT;
        while (!done && byte_reader_left(&reader))
        {
                p16 type = byte_reader_u16(&reader);
                byte_reader body = byte_reader_vector16(&reader);
                positive size = byte_reader_left(&body);
                bool critical = (type & 0x8000) != 0;

                if (!byte_reader_ok(&reader))
                        return NTS_KE_MALFORMED;
                switch (type & 0x7fff)
                {
                case 0:
                        if (size || !critical)
                                return NTS_KE_MALFORMED;
                        done = true;
                        break;
                case 1:
                        if (protocol || size != 2 || byte_reader_u16(&body))
                                return NTS_KE_MALFORMED;
                        protocol = true;
                        break;
                case 2:
                case 3:
                        //      An error or a warning: RFC 8915 4.1.3 and 4.1.4
                        //      have the client end the exchange on either.
                        return NTS_KE_ERROR;
                case 4:
                        if (algorithm || size != 2 ||
                            byte_reader_u16(&body) != NTS_AEAD_SIV_CMAC_256)
                                return NTS_KE_MALFORMED;
                        algorithm = true;
                        break;
                case 5:
                        if (size && size <= NTS_COOKIE_MAX &&
                            state->count < NTS_COOKIES)
                        {
                                memory_copy(state->cookie[state->count],
                                            byte_reader_here(&body), size);
                                state->length[state->count++] = (p16)size;
                        }
                        break;
                case 6:
                        if (named || !nts_name_ok(byte_reader_here(&body), size))
                                return NTS_KE_MALFORMED;
                        memory_copy(state->server, byte_reader_here(&body),
                                    size);
                        state->server[size] = end;
                        named = true;
                        break;
                case 7:
                        if (ported || size != 2)
                                return NTS_KE_MALFORMED;
                        state->port = byte_reader_u16(&body);
                        if (!state->port)
                                return NTS_KE_MALFORMED;
                        ported = true;
                        break;
                default:
                        if (critical)
                                return NTS_KE_MALFORMED;
                }
        }
        return done && !byte_reader_left(&reader) && protocol && algorithm &&
                       state->count
                   ? NTS_KE_OK : NTS_KE_MALFORMED;
}

/*
        NTS in the packet, RFC 8915 5: extension fields after the 48 bytes.

        A request carries a unique identifier (32 random bytes the reply
        must echo), one cookie (spent: it is taken out of the state as the
        packet is built, and the reply brings a fresh one), a placeholder
        for each cookie the state is then short of eight, and the
        authenticator: a nonce and the AEAD tag over everything before it,
        under the client-to-server key. A reply is believed only when it
        echoes the identifier and its authenticator, the last field,
        opens under the server-to-client key over the packet before it; the
        cookies it carries inside are then kept. A reply that fails either
        is noise, not an answer: the exchange goes on waiting for the real
        one, so a forger who has seen the identifier in the clear spends
        nothing but its own packet (and one of the exchange's discards). The
        one thing an unauthenticated reply may do is say the cookie is no
        good (a kiss code NTSN with the identifier), which sends the caller
        back to key establishment once.

        The plaintext is empty in a request, so the ciphertext is the
        16-byte tag alone. Nonces are 16 random bytes, never reused under
        a key because they are drawn, and the origin timestamp stays the
        random authenticator it always was.
*/
#define NTS_HEADER 48
#define NTS_PACKET_ROOM 1280
#define NTS_EF_UID 0x0104
#define NTS_EF_COOKIE 0x0204
#define NTS_EF_PLACEHOLDER 0x0304
#define NTS_EF_AUTH 0x0404
#define NTS_UID_BYTES 32
#define NTS_NONCE_BYTES 16
#define NTS_TAG_BYTES 16
#define NTS_REPLY_OK 0
#define NTS_REPLY_IGNORE (-1)
#define NTS_REPLY_NAK (-2)
#define NTS_KISS_NTSN 0x4e54534eu /* "NTSN" */

typedef struct
{
        crypto_siv_key c2s;
        crypto_siv_key s2c;
        p8 uid[NTS_UID_BYTES];
        nts_state address_to state;
} nts_exchange;

//      The keys of a state, ready for an exchange.
static COLD fn nts_exchange_open(nts_exchange address_to exchange,
                                 nts_state address_to state)
{
        exchange->state = state;
        crypto_siv_prepare(address_of exchange->c2s, state->c2s);
        crypto_siv_prepare(address_of exchange->s2c, state->s2c);
}

static COLD fn nts_exchange_close(nts_exchange address_to exchange)
{
        crypto_forget(exchange, sizeof(*exchange));
}

//      The fields after the header already in packet, or 0 when there is
//      no cookie left or no room. The cookie is spent, and a placeholder of
//      its size is sent for each the state is short of eight after it (RFC
//      8915 5.5: the server returns one new cookie for each cookie or
//      placeholder), so that a reply that never comes costs a cookie only
//      until the next one that does.
static COLD positive nts_request(nts_exchange address_to exchange,
                                 p8 address_to packet, positive room)
{
        nts_state address_to state = exchange->state;
        const p8 address_to parts[2];
        positive lengths[2];
        p8 nonce[NTS_NONCE_BYTES];
        positive at = NTS_HEADER;
        positive length;
        positive padded;
        positive auth;
        positive more;

        if (!state->count || state->count > NTS_COOKIES)
                return 0;
        length = state->length[state->count - 1];
        padded = (length + 3) & ~(positive)3;
        if (!length || length > NTS_COOKIE_MAX ||
            room < at + 4 + NTS_UID_BYTES + 4 + padded + 4 + 4 +
                       NTS_NONCE_BYTES + NTS_TAG_BYTES ||
            system_random_fill(exchange->uid, NTS_UID_BYTES, 0) ||
            system_random_fill(nonce, NTS_NONCE_BYTES, 0))
                return 0;
        network_store_16(packet + at, NTS_EF_UID);
        network_store_16(packet + at + 2, 4 + NTS_UID_BYTES);
        memory_copy(packet + at + 4, exchange->uid, NTS_UID_BYTES);
        at += 4 + NTS_UID_BYTES;
        network_store_16(packet + at, NTS_EF_COOKIE);
        network_store_16(packet + at + 2, (p16)(4 + padded));
        memory_copy(packet + at + 4, state->cookie[state->count - 1], length);
        memory_fill(packet + at + 4 + length, 0, padded - length);
        at += 4 + padded;
        crypto_forget(state->cookie[--state->count], NTS_COOKIE_MAX);
        state->length[state->count] = 0;

        //      One of the eight is the cookie just sent; the rest, while the
        //      packet has room for them and the authenticator after.
        for (more = NTS_COOKIES - state->count - 1; more; more--)
        {
                if (room < at + 4 + padded + 4 + 4 + NTS_NONCE_BYTES +
                               NTS_TAG_BYTES)
                        break;
                network_store_16(packet + at, NTS_EF_PLACEHOLDER);
                network_store_16(packet + at + 2, (p16)(4 + padded));
                memory_fill(packet + at + 4, 0, padded);
                at += 4 + padded;
        }

        auth = at;
        network_store_16(packet + at, NTS_EF_AUTH);
        network_store_16(packet + at + 2,
                         4 + 4 + NTS_NONCE_BYTES + NTS_TAG_BYTES);
        network_store_16(packet + at + 4, NTS_NONCE_BYTES);
        network_store_16(packet + at + 6, NTS_TAG_BYTES);
        memory_copy(packet + at + 8, nonce, NTS_NONCE_BYTES);
        parts[0] = packet;
        lengths[0] = auth;
        parts[1] = nonce;
        lengths[1] = NTS_NONCE_BYTES;
        crypto_siv_seal(address_of exchange->c2s, parts, lengths, 2,
                        packet + at + 8 + NTS_NONCE_BYTES, 0,
                        packet + at + 8 + NTS_NONCE_BYTES);
        crypto_forget(nonce, sizeof nonce);
        return auth + 4 + 4 + NTS_NONCE_BYTES + NTS_TAG_BYTES;
}

//      The cookies in a reply's decrypted fields, added to the state.
static COLD fn nts_cookies_take(nts_state address_to state,
                                const p8 address_to plain, positive length)
{
        byte_reader reader = byte_reader_open((p8 address_to)plain, length);

        while (byte_reader_left(&reader) >= 4)
        {
                p16 type = byte_reader_u16(&reader);
                positive size = byte_reader_u16(&reader);

                if (size < 4 || size % 4 || size - 4 > byte_reader_left(&reader))
                        return;
                size -= 4;
                if (type == NTS_EF_COOKIE && size && size <= NTS_COOKIE_MAX &&
                    state->count < NTS_COOKIES)
                {
                        memory_copy(state->cookie[state->count],
                                    byte_reader_here(&reader), size);
                        state->length[state->count++] = (p16)size;
                }
                (void)byte_reader_skip(&reader, size);
        }
}

/*
        A reply, read through one cursor: the header's mode and stratum and
        the four bytes a kiss code is in, then the extension fields, each a
        type and a length that covers it whole (at least 16 bytes, a whole
        number of words, RFC 7822). The identifier is echoed once; the
        authenticator is the last field, and opens over everything before it.
*/
static COLD bipolar nts_reply(nts_exchange address_to exchange,
                              const p8 address_to reply, positive length)
{
        byte_reader reader = byte_reader_open((p8 address_to)reply, length);
        byte_reader fields;
        byte_reader auth = byte_reader_open(null, 0);
        const p8 address_to parts[2];
        positive lengths[2];
        p8 plain[NTS_PACKET_ROOM];
        positive before = 0;
        bool sealed = false;
        bool echoed = false;
        p8 stratum;
        p32 kiss;

        (void)byte_reader_u8(&reader);
        stratum = byte_reader_u8(&reader);
        (void)byte_reader_skip(&reader, 10);
        kiss = byte_reader_u32(&reader);
        (void)byte_reader_skip(&reader, NTS_HEADER - 16);
        if (!byte_reader_ok(&reader))
                return NTS_REPLY_IGNORE;
        fields = reader;
        while (byte_reader_left(&fields))
        {
                positive at = length - byte_reader_left(&fields);
                p16 type = byte_reader_u16(&fields);
                positive size = byte_reader_u16(&fields);
                byte_reader body;

                if (!byte_reader_ok(&fields) || size < 16 || size % 4 ||
                    size - 4 > byte_reader_left(&fields))
                        return NTS_REPLY_IGNORE;
                body = byte_reader_window(&fields, size - 4);
                if (type == NTS_EF_UID)
                {
                        if (echoed || size != 4 + NTS_UID_BYTES ||
                            memory_compare(byte_reader_here(&body),
                                           exchange->uid, NTS_UID_BYTES))
                                return NTS_REPLY_IGNORE;
                        echoed = true;
                }
                else if (type == NTS_EF_AUTH)
                {
                        if (sealed || byte_reader_left(&fields))
                                return NTS_REPLY_IGNORE;
                        sealed = true;
                        before = at;
                        auth = body;
                }
        }
        if (!echoed)
                return NTS_REPLY_IGNORE;
        if (!sealed)
                return !stratum && kiss == NTS_KISS_NTSN ? NTS_REPLY_NAK
                                                          : NTS_REPLY_IGNORE;
        {
                positive nonce_length = byte_reader_u16(&auth);
                positive cipher_length = byte_reader_u16(&auth);
                positive nonce_padded = (nonce_length + 3) & ~(positive)3;
                positive cipher_padded = (cipher_length + 3) & ~(positive)3;
                const p8 address_to nonce = byte_reader_take(&auth, nonce_padded);
                const p8 address_to cipher = byte_reader_take(&auth, cipher_padded);
                bool opened;

                if (!byte_reader_ok(&auth) || cipher_length < NTS_TAG_BYTES ||
                    cipher_length - NTS_TAG_BYTES > sizeof(plain))
                        return NTS_REPLY_IGNORE;
                memory_copy(plain, cipher + NTS_TAG_BYTES,
                            cipher_length - NTS_TAG_BYTES);
                parts[0] = reply;
                lengths[0] = before;
                parts[1] = nonce;
                lengths[1] = nonce_length;
                opened = crypto_siv_open(address_of exchange->s2c, parts,
                                         lengths, 2, plain,
                                         cipher_length - NTS_TAG_BYTES, cipher);
                if (opened)
                        nts_cookies_take(exchange->state, plain,
                                         cipher_length - NTS_TAG_BYTES);
                crypto_forget(plain, sizeof plain);
                return opened ? NTS_REPLY_OK : NTS_REPLY_IGNORE;
        }
}

/* The keys a finished handshake yields: RFC 8915 4.2. */
static COLD bool nts_ke_keys(tls_conn address_to tls, nts_state address_to state)
{
        p8 context[5] = {0, 0, 0, NTS_AEAD_SIV_CMAC_256, 0};

        if (!tls_export(tls, "EXPORTER-network-time-security", context, 5,
                        state->c2s, 32))
                return false;
        context[4] = 1;
        return tls_export(tls, "EXPORTER-network-time-security", context, 5,
                          state->s2c, 32);
}

/* One key establishment with host on port: the state it leaves, or the
   reason it did not (an HTTP_ status: no name, no route, the certificate
   refused, or the exchange malformed). bootstrap says the certificate may
   be judged as of the clock's floor, because this is the exchange that will
   set the clock. The name is asked of the network's resolver in plain: the
   certificate check against it is what authenticates the server, so a
   forged answer costs the exchange and never the time, and a clock cannot
   wait for names that need it to be set. One deadline holds the handshake
   and the reply. */
static COLD bipolar nts_ke(string_address host, p16 port, bool bootstrap,
                           nts_state address_to state)
{
        static const p8 request[] = {0x80, 0x01, 0, 2, 0, 0, 0, 4, 0, 2, 0,
                                     NTS_AEAD_SIV_CMAC_256, 0x80, 0, 0, 0};
        http_link link;
        network_deadline deadline;
        p8 reply[NTS_KE_ROOM];
        positive used = 0;
        bipolar status;
        p32 ip = http_lookup(host);

        memory_fill(state, 0, sizeof(*state));
        if (!ip)
                return HTTP_NO_HOST;
        memory_fill(address_of link, 0, __builtin_offsetof(http_link, session));
        link.handle = http_stream_open(ip, port, NTS_KE_SECONDS);
        if (link.handle < 0)
                return link.handle;
        if (!network_deadline_begin(address_of deadline, NTS_KE_SECONDS, 0))
        {
                http_link_close(address_of link);
                return HTTP_NO_REPLY;
        }
        status = tls_connect_until(address_of link.session, link.handle, host,
                                   true, (const p8 address_to) "ntske/1", 7,
                                   bootstrap, address_of deadline);
        if (status)
        {
                http_link_close(address_of link);
                return http_tls_status(status);
        }
        link.tls = true;
        status = http_link_write(address_of link, (p8 address_to)request,
                                 sizeof request);
        while (!status && !nts_ke_complete(reply, used))
        {
                positive got = 0;

                if (used == sizeof reply ||
                    http_link_read_until(address_of link, reply + used,
                                         sizeof reply - used, address_of got,
                                         address_of deadline) ||
                    !got)
                        status = HTTP_NO_REPLY;
                else
                        used += got;
        }
        if (!status &&
            (nts_ke_reply(reply, used, state) != NTS_KE_OK ||
             !nts_ke_keys(address_of link.session, state)))
                status = HTTP_MALFORMED;
        http_link_close(address_of link);
        crypto_forget(reply, sizeof reply);
        if (status)
                memory_fill(state, 0, sizeof(*state));
        return status;
}

/*
        DNS over TLS (RFC 7858), for the public resolvers known to offer it.

        The setting is a word root keeps in /root/dns.tls (`moonwater dns tls
        on|off|only`) and publishes in /run/moonwater/dns.tls, which every
        program can read and only root can write, so a lookup by someone who
        cannot read /root is held to the same rule: off asks every resolver
        over UDP as this resolver always has; on asks over TLS on port 853
        first and, when that cannot be done (no connection, a certificate
        refused, an unset clock), over UDP as before, within two seconds of
        the first try; only asks over TLS alone and sends no name in the
        clear at all. A word that is none of them is the tier's default: off,
        and only at the tight tier.

        Which resolvers are asked over TLS: the ones /etc/resolv.conf names
        that are known to offer it (1.1.1.1, 1.0.0.1, 9.9.9.9 and
        149.112.112.112: Cloudflare and Quad9), or, when it names none of
        them, those four in turn. That is a third party told the names, over
        an authenticated channel, instead of the network's own resolver in
        the clear; a name only the network's resolver knows does not resolve
        with only. The certificate is checked as for https: the chain to a
        Mozilla root, the dates against the clock and its floor, and the
        resolver's own address against an iPAddress name (each of the four
        publishes its address as one). A resolver that presents no acceptable
        certificate is not believed, so an on-path attacker who can block or
        answer TCP 853 gets, with on, the plain query exactly as without the
        setting, which is why only exists.

        Why it is off by default: a captive portal answers or drops port 853
        and a first lease arrives with the clock unset, where every
        certificate is refused, which is the ordinary first minute of a
        machine; on survives both at a cost of two seconds a lookup, which a
        default should not charge. At the tight tier only is the default,
        where the cost is that names do not resolve until NTS has set the
        clock.

        Only the HTTP client (wget, fetch, the zone's question) and `host`
        follow it. The time servers' names, NTS key establishment, a
        certificate's caIssuers location, hostid, logger's server and
        waterlink's peers are asked in plain: a clock cannot wait for names
        that need it to be set, NTS and TLS authenticate the server they reach
        whatever the name resolved to, and a lookup inside a handshake is not
        to open a handshake of its own.
*/
#define DNS_TLS_PORT 853
#define DNS_TLS_SECONDS 2
#define DNS_TLS_FAILED (-12)
//      dns_resolve_apart's own: the child could not be confined, so nothing
//      was asked; or it ended without an answer that could be believed.
#define DNS_CONFINE (-13)
#define DNS_APART (-14)
#define DNS_TLS_OFF 0
#define DNS_TLS_ON 1
#define DNS_TLS_ONLY 2
#ifndef DNS_TLS_PATH
#define DNS_TLS_PATH "/run/moonwater/dns.tls"
#endif
#if MOONWATER_STRICT_NET >= STRICT_TIGHT
#define DNS_TLS_DEFAULT DNS_TLS_ONLY
#else
#define DNS_TLS_DEFAULT DNS_TLS_OFF
#endif

static const p32 dns_tls_public[] = {0x01010101u, 0x09090909u, 0x01000001u,
                                     0x95707070u};

static CONST COLD bool dns_offers_tls(p32 server)
{
        for (positive at = 0; at < array_count(dns_tls_public); at++)
                if (server == dns_tls_public[at])
                        return true;
        return false;
}

//      The word as a mode: the tier's default for anything that is not on,
//      off or only, a missing file included.
static COLD positive dns_tls_mode_of(string_address word)
{
        return string_equals(word, "off")    ? DNS_TLS_OFF
               : string_equals(word, "on")   ? DNS_TLS_ON
               : string_equals(word, "only") ? DNS_TLS_ONLY
                                             : DNS_TLS_DEFAULT;
}

//      The setting a confined child read before its filter went on, plus
//      one, as dns_held holds its servers; 0 everywhere else.
static positive dns_tls_held;

//      The published setting, read as a plain file only.
static COLD positive dns_tls_mode(void)
{
        p8 text[16];
        bipolar got;

        if (dns_tls_held)
                return dns_tls_held - 1;
        got = file_slurp_regular_at(AT_FDCWD, (string_address)DNS_TLS_PATH,
                                    text, sizeof(text), O_NOFOLLOW);
        if (got < 0)
                got = 0;
        while (got && (text[got - 1] == '\n' || text[got - 1] == ' '))
                got--;
        text[got] = end;
        return dns_tls_mode_of((string_address)text);
}

/*
        One query over TLS to server, within the deadline: DNS_TLS_FAILED
        when no answer to it came (no connection, a handshake or certificate
        refused, a stream that ended or went quiet), otherwise what the
        answer says. RFC 7858 3.3: a two-byte length and the query, a
        two-byte length and the reply. The question is asked as written,
        since the channel authenticates the answer, with EDNS0.
*/
static COLD bipolar dns_query_tls(p32 server, string_address name,
                                  p32 address_to found,
                                  const network_deadline address_to deadline)
{
        http_link link;
        p8 request[2 + DNS_MAX_MESSAGE];
        p8 reply[DNS_MAX_MESSAGE];
        p8 host[16];
        socket_address_internet where = {
            .family = AF_INET, .port = network_order_16(DNS_TLS_PORT),
            .host = network_order_32(server)};
        b32 one = 1;
        bool mixed = false;
        positive question_length;
        positive request_length;
        positive got;
        positive length;
        p16 id;
        bipolar status = DNS_TLS_FAILED;

        if (!network_transaction_secure(address_of id, sizeof id) &&
            system_random_fill(address_of id, sizeof id, 0))
                return DNS_NO_RANDOM;
        if (dns_build_request(request + 2, sizeof request - 2, name, id,
                              address_of mixed, true, address_of question_length,
                              address_of request_length) < 0)
                return DNS_MALFORMED;
        network_store_16(request, (p16)request_length);
        host[host_into(host, server)] = end;

        memory_fill(address_of link, 0, __builtin_offsetof(http_link, session));
        link.handle = dns_stream_connect_until(address_of where, deadline);
        if (link.handle < 0)
                return DNS_TLS_FAILED;
        (void)socket_option_set((b32)link.handle, IPPROTO_TCP, TCP_NODELAY,
                                address_of one, sizeof one);
        if (tls_connect_until(address_of link.session, link.handle,
                              (string_address)host, true, null, 0, false,
                              deadline))
        {
                http_link_close(address_of link);
                return DNS_TLS_FAILED;
        }
        link.tls = true;
        if (http_link_write(address_of link, request, 2 + request_length))
                goto done;
        //      The length, then exactly that many bytes, each read held to the
        //      deadline.
        for (got = 0; got < 2;)
        {
                positive more = 0;

                if (http_link_read_until(address_of link, reply + got, 2 - got,
                                         address_of more, deadline) || !more)
                        goto done;
                got += more;
        }
        length = network_load_16(reply);
        if (length > sizeof reply)
        {
                status = DNS_MALFORMED;
                goto done;
        }
        for (got = 0; got < length;)
        {
                positive more = 0;

                if (http_link_read_until(address_of link, reply + got,
                                         length - got, address_of more,
                                         deadline) || !more)
                        goto done;
                got += more;
        }
        status = dns_reply_result(reply, length, id, request + 2,
                                  question_length, found);
        if (status == DNS_TRY_TCP)
                status = DNS_MALFORMED;

done:
        http_link_close(address_of link);
        return status;
}

/*
        A name under the setting: over TLS to the resolvers that offer it
        while the TLS budget lasts (every one with only, two seconds of them
        with on), and the answer the first that answers gives is believed as
        the plain walk believes one; then, with on and no answer over TLS,
        the plain walk over resolv.conf. With only, no resolver is asked in
        the clear, the network's own included.
*/
static COLD bipolar dns_resolve_wanted(string_address path, string_address name,
                                       p32 address_to found, positive seconds)
{
        positive mode = dns_tls_mode();
        p32 named[DNS_SERVERS_MAX];
        p32 asked[array_count(dns_tls_public)];
        positive count = 0;
        positive have;
        network_deadline deadline;
        bipolar status = DNS_NO_REPLY;

        if (mode == DNS_TLS_OFF)
                return dns_resolve_any(path, name, found, seconds);
        if (dns_held.held)
        {
                have = dns_held.count;
                memory_copy(named, dns_held.servers, have * sizeof *named);
        }
        else
                have = dns_servers(path, named, array_count(named));
        for (positive at = 0; at < have; at++)
                if (dns_offers_tls(named[at]))
                        asked[count++] = named[at];
        if (!count)
        {
                memory_copy(asked + count, dns_tls_public + count,
                            (array_count(dns_tls_public) - count) * sizeof(asked[0]));
                count = array_count(dns_tls_public);
        }
        if (network_deadline_begin(address_of deadline,
                                   mode == DNS_TLS_ONLY || seconds < DNS_TLS_SECONDS
                                       ? seconds : DNS_TLS_SECONDS,
                                   0))
                for (positive at = 0; at < count; at++)
                {
                        bipolar answer = dns_query_tls(asked[at], name, found,
                                                       address_of deadline);

                        if (answer != DNS_TLS_FAILED)
                                status = answer;
                        if (dns_answer_is_final(answer))
                                return answer;
                }
        if (mode == DNS_TLS_ONLY)
                return status;
        return dns_resolve_any(path, name, found, seconds);
}

/* The HTTP client's lookup: what http_lookup answers, with a name asked
   under the DNS over TLS setting. */
static p32 http_lookup_wanted(string_address host)
{
        p32 ip = 0;

        if (string_to_host(host) >= 0 || http_host_loopback(host))
                return http_lookup(host);
        return dns_resolve_wanted((string_address) "/etc/resolv.conf", host,
                                  address_of ip, 3) == DNS_OK
                   ? ip : 0;
}

static fn http_url_leaf(string_address path, p8 address_to into, positive room)
{
        p8 target[HTTP_URL_MAX];
        string_address query;
        string_address slash;

        if (!room)
                return;
        if (http_origin_form(path, target, sizeof target))
                target[0] = end;

        query = string_first_of(target, '?');
        if (query)
                query[0] = end;

        /* No last segment, ".", or "..", %2e spellings too: each names a
           directory, which a file cannot replace, and GNU wget saves the
           plain three as index.html (a %2e spelling it saves as %2E, a
           name this client does not want for a path it would resolve). */
        slash = string_last_of(target, '/');
        path = slash ? slash + 1 : target;
        if (!string_get(path) || http_dot_segment(path, string_length(path)))
                path = (string_address) "index.html";
        string_copy_max_end(into, path, room - 1);
}

/*
        The clients, written as the words they disagree about.

        Everything under this -- the connection, the request, the head, the
        framing and the body -- is one machine, and what is left is manners.
        fetch is the plaintext slurp: dawning over HTTP/1.0, no TLS at all,
        no redirect followed, and a head that stops early is a lie about the
        framing, because a plaintext peer that meant to answer had no reason
        to hang up mid-sentence. wget is the streaming download: Wget over
        HTTP/1.1, TLS, twenty redirects and never a step back down to plain, and a
        head that stops early is a peer that went away. The zone's question
        (locale_auto_ask in src/sh/host.c) is a third: Moonwater over HTTP/1.1
        and TLS, no redirect, a few seconds and a few kilobytes of anything a
        peer has to say, which is a line or two of a header or a trace.
*/
typedef struct
{
        string_address agent;
        p8 version_minor;
        bool follow;
        bool allow_tls;
        bool head_cut_is_malformed;
        positive head_seconds;
        positive body_seconds;
        positive body_most;
        //      The most a streamed body that declared no length may write,
        //      or zero for no ceiling: GNU wget's default has none, and
        //      neither has this client's; the tight tier's is
        //      HTTP_UNDECLARED_MOST. A body held in memory is held to
        //      body_most in every tier already.
        positive undeclared_most;
        //      HEAD, not GET: whether the resource is there, and no body.
        bool head;
        //      The whole fetch, every hop's head and the body, in one budget,
        //      or zero for none: the tight tier's is one head wait and one
        //      body wait (HTTP_CHAIN_SECONDS), where the default, as GNU wget,
        //      times each hop alone.
        positive chain_seconds;
} http_manners;

static const http_manners http_manners_fetch = {
    (string_address)"dawning", '0', false, false, true,
    HTTP_HEAD_SECONDS, HTTP_BODY_SECONDS, HTTP_FETCH_MAX, 0, false, HTTP_CHAIN_SECONDS};
static const http_manners http_manners_wget = {
    (string_address)"Wget", '1', true, true, false,
    HTTP_HEAD_SECONDS, HTTP_BODY_SECONDS, HTTP_FETCH_MAX,
    MOONWATER_STRICT_NET >= STRICT_TIGHT ? HTTP_UNDECLARED_MOST : 0, false,
    HTTP_CHAIN_SECONDS};
//      wget --spider: wget's manners, asking HEAD at every hop as GNU wget
//      does, and reading no body however the head frames one.
static const http_manners http_manners_spider = {
    (string_address)"Wget", '1', true, true, false,
    HTTP_HEAD_SECONDS, HTTP_BODY_SECONDS, HTTP_FETCH_MAX, 0, true, HTTP_CHAIN_SECONDS};
static const http_manners http_manners_zone = {
    (string_address)"Moonwater", '1', false, true, false, 10, 10, 4096, 0, false,
    MOONWATER_STRICT_NET >= STRICT_TIGHT ? 20 : 0};

/* The head of the response a fetch ended on, for the caller that reads a
   header of it. */
static bipolar http_head_keep(http_buffer address_to into,
                              const p8 address_to head, positive length)
{
        if (!byte_store_reserve(into, length + 1, 4096))
                return HTTP_NO_REPLY;
        memory_copy(into->bytes, head, length);
        into->used = length;
        into->bytes[length] = end;
        return HTTP_OK;
}

/* One URL fetched under one client's manners, into exactly one sink: a
   descriptor, or a buffer filled here and handed over only on success, so a
   refused response leaves whatever the caller already held. A memory body is
   bounded by the manners' most both by what Content-Length claims and by what
   actually arrives. A head_out is given the head of the final response, with
   no body behind it. */
static bipolar http_run(string_address start, const http_manners address_to how,
                        bool check_cert, bipolar dest,
                        http_buffer address_to into, b32 address_to code,
                        p8 address_to where, http_buffer address_to head_out)
{
        p8 url[HTTP_URL_MAX];
        http_buffer whole = {0};
        positive hop;
        bool secure = false;
        //      The tight tier's budget for the whole fetch; began zero for none.
        network_deadline chain = {0};
#if MOONWATER_STRICT_NET >= STRICT_TIGHT
        bool reached_public = false;
#endif
        bipolar status;

        if (string_length(start) >= sizeof url)
                return HTTP_BAD_URL;
        string_copy(url, start);
        if (how->chain_seconds &&
            !network_deadline_begin(address_of chain, how->chain_seconds, 0))
                return HTTP_NO_REPLY;

        for (hop = 0; hop < HTTP_HOPS; hop++)
        {
                p8 host[256];
                p8 head[HTTP_HEAD_MAX];
                p8 next[HTTP_URL_MAX];
                string_address path;
                p16 port;
                bool tls;
                p32 ip = 0;
                http_link link;
                http_response response;
                positive header = 0;
                positive used = 0;

                positive left[2];

                if (chain.began &&
                    !network_deadline_left(address_of chain, left, left + 1))
                {
                        status = HTTP_NO_REPLY;
                        goto done;
                }

                /* The request is built before any name is looked up or any
                   connection opened: a Location this client cannot put on
                   the wire costs its target no connection and no handshake.
                   It is the head buffer's first bytes until it is sent. */
                status = http_split_into(url, host, sizeof host, address_of port,
                                         address_of path, address_of tls);
                if (!status)
                        http_path_simplify((p8 address_to)path);
                if (!status && tls && !how->allow_tls)
                        status = HTTP_TLS;
                if (!status && !http_transport_allowed(address_of secure, tls))
                        status = HTTP_DOWNGRADE;
                if (!status)
                        status = http_request(
                            head, sizeof head, how->head, host, port, path, tls,
                            how->version_minor, how->agent, address_of used);
                if (!status && !(ip = http_lookup_wanted(host)))
                        status = HTTP_NO_HOST;
                /* GNU wget and curl connect wherever a name resolves, so the
                   default does; the tight tier refuses a name rebound onto
                   this machine or its link, on the first hop as on any. */
                if (!status && MOONWATER_STRICT_NET >= STRICT_TIGHT &&
                    http_name_rebound(host, ip))
                        status = HTTP_REBOUND;
#if MOONWATER_STRICT_NET >= STRICT_TIGHT
                /* Once a chain has reached public space no later hop may
                   go back inside: a name that resolves, or a Location that
                   spells, an address on this host or its network is how a
                   server it was pointed at reaches what it was never given.
                   GNU wget and curl follow such a redirect, so it is the
                   tight tier's; a chain that starts inside may go anywhere,
                   and the address checked is the one connected to. */
                if (!status)
                {
                        bool outside = http_address_public(ip);

                        if (reached_public && !outside)
                                status = HTTP_PRIVATE;
                        reached_public |= outside;
                }
#endif
                if (status)
                        goto done;

                status = http_link_open(address_of link, ip, port, host, tls,
                                        check_cert);
                if (status)
                        goto done;

                status = http_link_write(address_of link, head, used);
                crypto_forget(head, used);
                used = 0;
                if (!status)
                        status = http_response_head(
                            address_of link, head, sizeof head, address_of used,
                            address_of header, address_of response,
                            how->head_seconds, 0, how->head_cut_is_malformed,
                            chain.began ? address_of chain : null);
                if (!status && code)
                        address_to code = response.code;
                if (!status && head_out)
                        status = http_head_keep(head_out, head, header);

                if (!status && how->follow &&
                    http_response_is_redirect(response.code))
                {
                        p8 placed[HTTP_URL_MAX];

                        /* A redirect with nothing to follow, no Location or
                           an empty one, is the server's answer, a status like
                           any other: GNU wget says "Redirection (302) without
                           location" and exits 8, and curl -L hands the body
                           over. */
                        status = !response.location_length ? HTTP_STATUS
                                 : response.location_length >= sizeof placed
                                     ? HTTP_BAD_URL : HTTP_OK;
                        if (status == HTTP_BAD_URL)
                        {
                                /* The URL a failure names is the Location
                                   that was too long, as far as it goes, and
                                   not the page that sent it. */
                                memory_copy(url, response.location, sizeof url - 1);
                                url[sizeof url - 1] = end;
                        }
                        if (!status)
                        {
                                memory_copy(placed, response.location,
                                            response.location_length);
                                placed[response.location_length] = end;
                                /* Escaped into next, and resolved from there
                                   in place: http_absolutize copies the
                                   reference before it writes a byte. */
                                status = http_url_escape(placed,
                                                         response.location_length,
                                                         false, next, sizeof next);
                                if (!status)
                                        status = http_absolutize(tls, host, port, path,
                                                                 next, next,
                                                                 sizeof next);
                                //      The next hop, or the Location a failure
                                //      names; both terminate inside a buffer
                                //      exactly as large as url.
                                string_copy(url, status ? placed : next);
                        }
                        http_link_close(address_of link);
                        if (status)
                                goto done;
                        continue;
                }

                if (!status && !http_response_is_success(response.code))
                        status = HTTP_STATUS;
                else if (!status && !how->head &&
                         !http_response_has_no_body(response.code))
                {
                        /* The default reads a 205's content as any other
                           response's, as wget and curl do.  The tight tier
                           holds it to the RFC: the framing is consumed and a
                           zero-capacity store refuses any content before it
                           reaches the caller's output. */
                        bool reset = MOONWATER_STRICT_NET >= STRICT_TIGHT &&
                                     response.code == 205;
                        http_body body = {
                            .link = address_of link,
                            .stash = head + header,
                            .stash_used = used - header,
                            .scratch = head,
                            .store = reset || into ? address_of whole : null,
                            .store_limit = reset ? 0 : how->body_most,
                            .undeclared_most = into ? 0 : how->undeclared_most,
                        };

                        /* The idle timer catches a stopped peer; this absolute
                           timer catches one which sends the next byte just
                           before every idle expiry.  A body held in memory
                           always has it, in every tier.  A download written
                           as it arrives has it at the tight tier only: GNU
                           wget, the default's reference, puts no total time
                           on one, and a large file over a slow link is not an
                           attack. */
                        if ((into || MOONWATER_STRICT_NET >= STRICT_TIGHT) &&
                            !network_deadline_begin_within(
                                address_of body.deadline, how->body_seconds, 0,
                                chain.began ? address_of chain : null))
                                status = HTTP_NO_REPLY;
                        else
                                status =
                                    into &&
                                         response.body_kind == HTTP_BODY_LENGTH &&
                                         response.body_length > how->body_most
                                     ? HTTP_MALFORMED
                                     : http_copy_body(address_of body,
                                                      reset ? -1 : dest,
                                                      address_of response);
                }

                http_link_close(address_of link);
                goto done;
        }

        //      The redirect budget ended with another destination to follow.
        status = HTTP_REDIRECTS;

done:
        //      Where it ended: the URL a failure names.
        if (where)
                string_copy(where, url);
        //      The store path of http_copy terminates what it appends.
        if (into && !status)
        {
                byte_store_release(into);
                address_to into = whole;
                memory_fill(address_of whole, 0, sizeof whole);
        }
        byte_store_release(address_of whole);

        return status;
}

/* The two clients' engines, run in the calling process. The clients
   themselves never call these: they call http_get_apart and
   http_fetch_to_apart below, which run the same engine in a confined child.
   These are what the checks and the HTTP fuzz target drive directly, the
   way the checks call dhcp_ask. */

//      fetch: no TLS, no redirect followed, and the body only once it is whole.
static bipolar http_get(string_address url, http_buffer address_to body,
                        b32 address_to code)
{
        return http_run(url, address_of http_manners_fetch, false, -1, body,
                        code, null, null);
}

//      wget --spider: wget's request with HEAD, and nothing but the answer.
static bipolar http_spider(string_address start, bool check_cert,
                           b32 address_to code, p8 address_to where)
{
        return http_run(start, address_of http_manners_spider, check_cert, -1,
                        null, code, where, null);
}

//      wget: TLS, redirects, and the body written as it arrives; where, if
//      given, is HTTP_URL_MAX bytes and gets the URL the fetch ended on.
static bipolar http_fetch_to(string_address start, bipolar dest, bool check_cert,
                             b32 address_to code, p8 address_to where)
{
        return http_run(start, address_of http_manners_wget, check_cert, dest,
                        null, code, where, null);
}

#endif // STANDARD_MODERN_C_NET_HTTP
/* ---- apart: the network half of a client, in a child of its own ---- */

#ifndef STANDARD_MODERN_C_NET_APART
#define STANDARD_MODERN_C_NET_APART

/*
        The network half of a client, apart from the client.

        Everything a TLS server, an HTTP server or a resolver sends is read by
        a parser here: the record layer, X.509 and its DER, the HTTP head and
        its framing, a DNS answer. A memory-safety bug in any of them, run in
        the process of the person who typed wget, is code running with
        everything that person may do, and the zone's question at boot
        (locale_auto_ask in src/sh/host.c) runs as root. So a client puts that
        half in a child (net_apart) and keeps the rest itself:

          the client opens or creates whatever is written, makes every
          decision that touches a file, and reads back one answer, of a size
          it bounds, which it believes only as far as it can check;

          the child holds the answer's pipe, the one descriptor it was handed
          to write into (wget's output, or none) and standard error, and
          nothing else; reads resolv.conf, the DNS over TLS setting and the
          clock's floor while it still may (the trust anchors are compiled
          in, the clock is a vDSO read, and a revocation list is fetched
          into memory, never a file); becomes
          nobody when it is root; can no longer be dumped, gain a privilege
          through exec or outlive the client; and then installs a filter that
          kills it at any system call fetching does not need.

        What fetching needs is short: a socket, IPv4 TCP or UDP and nothing
        else -- no unix, netlink, packet, raw or IPv6 socket -- with connect,
        bind (a DNS query's random port), sendto and recvfrom; two socket
        timeouts and TCP_NODELAY to set and SO_ERROR to read back; ppoll; the
        clock and getrandom; write and writev; close; memory that is never
        made executable; riscv_hwprobe on RISC-V, which the hash routines ask
        the first time they run; and the ways out. A redirect, a caIssuers
        fetch or a revocation list is another socket and connect inside the
        same child, which is why those stay. openat, execve, ptrace, kill,
        ioctl, mprotect and everything else is a SIGSYS.

        It fails closed. A kernel that will not take the filter -- one built
        without seccomp, a container whose own filter refuses the call, or
        qemu-user, which cannot filter itself and answers ENOSYS -- fetches
        nothing: NET_APART_UNCONFINED, and the client says so. Moonwater's own
        kernels always have CONFIG_SECCOMP_FILTER (build_essentials in
        src/build/build.c). The DHCP exchange has its own filter
        (dhcp_confine), which also fails closed.

        Every client is a task: a function that runs in the child with the
        caller's memory as it was at the fork, does the exchange, and writes
        its answer to the descriptor it is given; and a caller that hands it
        to net_apart and then reads that answer as it would a packet, held to
        sizes and values it can check. http_run_apart, nts_ke_apart and
        dns_resolve_apart below are the three.
*/
#define NET_APART_OK 0
#define NET_APART_NO_CHILD (-1)
#define NET_APART_UNCONFINED (-2)
#define NET_APART_DIED (-3)

//      The child's exit when it could not confine itself and so did nothing.
#define NET_APART_EXIT_UNCONFINED 3

#define NET_APART_NOBODY 65534

#define PR_SET_PDEATHSIG 1
#define PR_SET_DUMPABLE 4
#define PR_SET_NO_NEW_PRIVS 38
#define SECCOMP_SET_MODE_FILTER 1
#define SECCOMP_RET_KILL_PROCESS 0x80000000u
#define SECCOMP_RET_ALLOW 0x7fff0000u
#define AUDIT_ARCH_X86_64 0xc000003eu
#define AUDIT_ARCH_AARCH64 0xc00000b7u
#define AUDIT_ARCH_RISCV64 0xc00000f3u

#if defined(__x86_64__)
#define NET_APART_ARCH AUDIT_ARCH_X86_64
#elif defined(__aarch64__)
#define NET_APART_ARCH AUDIT_ARCH_AARCH64
#else
#define NET_APART_ARCH AUDIT_ARCH_RISCV64
#define NET_APART_RISCV_HWPROBE 258
#endif

/*
        Classic BPF, as seccomp runs it over struct seccomp_data: the call's
        number at 0, its architecture at 4, then six 64-bit arguments from 16.
        An argument is read by its low word, which on these little-endian
        machines is the word at its offset and is all of an int to the kernel.
*/
#define NET_FILTER_LOAD 0x20    /* BPF_LD | BPF_W | BPF_ABS */
#define NET_FILTER_AND 0x54     /* BPF_ALU | BPF_AND | BPF_K */
#define NET_FILTER_ALWAYS 0x05  /* BPF_JMP | BPF_JA */
#define NET_FILTER_EQUAL 0x15   /* BPF_JMP | BPF_JEQ | BPF_K */
#define NET_FILTER_ANY_BIT 0x45 /* BPF_JMP | BPF_JSET | BPF_K */
#define NET_FILTER_RETURN 0x06  /* BPF_RET | BPF_K */
#define NET_FILTER_NUMBER 0
#define NET_FILTER_ARCH 4
#define NET_FILTER_ARGUMENT(n) (16 + 8 * (n))
#define NET_FILTER_STEPS 96
//      PROT_EXEC, the one protection a mapping in the child may not have.
#define NET_APART_EXECUTABLE 4
//      SO_TIMESTAMPNS and SO_TIMESTAMPING, the two options a time query
//      sets (SNTP_TIMESTAMPNS and SNTP_TIMESTAMPING in src/sh/host.c).
#define NET_APART_SO_TIMESTAMPNS 35
#define NET_APART_SO_TIMESTAMPING 37

//      struct sock_filter.
typedef struct
{
        p16 code;
        p8 jump_true;
        p8 jump_false;
        p32 value;
} net_filter_step;

typedef struct
{
        net_filter_step steps[NET_FILTER_STEPS];
        positive used;
} net_filter;

//      One step. A program that would not fit counts past the end, and
//      net_filter_install refuses it.
static COLD fn net_filter_put(net_filter address_to filter, p16 code,
                              p8 jump_true, p8 jump_false, p32 value)
{
        if (filter->used < NET_FILTER_STEPS)
                filter->steps[filter->used] =
                    (net_filter_step){code, jump_true, jump_false, value};
        filter->used++;
}

//      The answer for every call that reached this step.
static COLD fn net_filter_answer(net_filter address_to filter, p32 answer)
{
        net_filter_put(filter, NET_FILTER_RETURN, 0, 0, answer);
}

//      Argument n's low word, masked, is one of the values, or the call is
//      killed; when it is, the program goes on to the step after.
static COLD fn net_filter_require(net_filter address_to filter, positive argument,
                                  p32 mask, const p32 address_to values,
                                  positive count)
{
        positive after = filter->used + 1 + (mask != ~(p32)0) + count + 1;

        net_filter_put(filter, NET_FILTER_LOAD, 0, 0,
                       (p32)NET_FILTER_ARGUMENT(argument));
        if (mask != ~(p32)0)
                net_filter_put(filter, NET_FILTER_AND, 0, 0, mask);
        for (positive at = 0; at < count; at++)
                net_filter_put(filter, NET_FILTER_EQUAL,
                               (p8)(after - filter->used - 1), 0, values[at]);
        net_filter_answer(filter, SECCOMP_RET_KILL_PROCESS);
}

/* A test on the accumulator whose false branch goes past what follows, to
   wherever net_filter_past is called: a call's own block of argument checks,
   which ends in an answer on every path, or one value's half of one. The
   accumulator still holds what was compared, so the next test reads it. */
static COLD positive net_filter_when(net_filter address_to filter, p32 value)
{
        positive at = filter->used;

        net_filter_put(filter, NET_FILTER_EQUAL, 0, 0, value);
        return at;
}

static COLD fn net_filter_past(net_filter address_to filter, positive when)
{
        if (when < NET_FILTER_STEPS)
                filter->steps[when].jump_false = (p8)(filter->used - when - 1);
}

//      Argument n's low word into the accumulator.
static COLD fn net_filter_load(net_filter address_to filter, positive argument)
{
        net_filter_put(filter, NET_FILTER_LOAD, 0, 0,
                       (p32)NET_FILTER_ARGUMENT(argument));
}

/* The program: the architecture, then the calls that need no more than
   their number, then the four whose arguments decide, then death. The
   numbers come first in the order a download makes them most. x32's
   bit-30 numbers on x86_64 match nothing listed and are killed. A time
   query (clock) also reads its replies and their stamps with recvmsg and
   sets the two timestamp options; nothing else is told them. */
static COLD fn net_filter_build(net_filter address_to filter, bool clock)
{
        static const p32 plain[] = {
            (p32)syscall(recvfrom), (p32)syscall(writev),
            (p32)syscall(sendto), (p32)syscall(ppoll),
            (p32)syscall(clock_gettime), (p32)syscall(write),
            (p32)syscall(getrandom), (p32)syscall(close),
            (p32)syscall(connect), (p32)syscall(bind),
            (p32)syscall(munmap), (p32)syscall(mremap),
            (p32)syscall(madvise), (p32)syscall(exit_group),
            (p32)syscall(exit), (p32)syscall(rt_sigreturn),
            (p32)syscall(restart_syscall),
#ifdef NET_APART_RISCV_HWPROBE
            NET_APART_RISCV_HWPROBE,
#endif
        };
        static const p32 families[] = {AF_INET};
        static const p32 kinds[] = {SOCK_STREAM, SOCK_DGRAM};
        static const p32 protocols[] = {0};
        static const p32 socket_level[] = {SO_RCVTIMEO, SO_SNDTIMEO};
        static const p32 clock_level[] = {SO_RCVTIMEO, SO_SNDTIMEO,
                                          NET_APART_SO_TIMESTAMPNS,
                                          NET_APART_SO_TIMESTAMPING};
        static const p32 tcp_level[] = {TCP_NODELAY};
        static const p32 sol_socket[] = {SOL_SOCKET};
        static const p32 so_error[] = {SO_ERROR};
        positive count = array_count(plain);
        positive call;
        positive level;

        filter->used = 0;
        net_filter_put(filter, NET_FILTER_LOAD, 0, 0, NET_FILTER_ARCH);
        net_filter_put(filter, NET_FILTER_EQUAL, 1, 0, NET_APART_ARCH);
        net_filter_answer(filter, SECCOMP_RET_KILL_PROCESS);
        net_filter_put(filter, NET_FILTER_LOAD, 0, 0, NET_FILTER_NUMBER);
        for (positive at = 0; at < count; at++)
                net_filter_put(filter, NET_FILTER_EQUAL, (p8)(count - at), 0,
                               plain[at]);
        net_filter_put(filter, NET_FILTER_ALWAYS, 0, 0, 1);
        net_filter_answer(filter, SECCOMP_RET_ALLOW);

        //      socket: AF_INET, stream or datagram (with SOCK_NONBLOCK or
        //      SOCK_CLOEXEC or both), protocol 0.
        call = net_filter_when(filter, (p32)syscall(socket));
        net_filter_require(filter, 0, ~(p32)0, families, array_count(families));
        net_filter_require(filter, 1, ~(p32)(SOCK_NONBLOCK | SOCK_CLOEXEC),
                           kinds, array_count(kinds));
        net_filter_require(filter, 2, ~(p32)0, protocols, array_count(protocols));
        net_filter_answer(filter, SECCOMP_RET_ALLOW);
        net_filter_past(filter, call);

        //      setsockopt: SO_RCVTIMEO and SO_SNDTIMEO, or TCP_NODELAY.
        call = net_filter_when(filter, (p32)syscall(setsockopt));
        net_filter_load(filter, 1);
        level = net_filter_when(filter, SOL_SOCKET);
        if (clock)
                net_filter_require(filter, 2, ~(p32)0, clock_level,
                                   array_count(clock_level));
        else
                net_filter_require(filter, 2, ~(p32)0, socket_level,
                                   array_count(socket_level));
        net_filter_answer(filter, SECCOMP_RET_ALLOW);
        net_filter_past(filter, level);
        level = net_filter_when(filter, IPPROTO_TCP);
        net_filter_require(filter, 2, ~(p32)0, tcp_level, array_count(tcp_level));
        net_filter_answer(filter, SECCOMP_RET_ALLOW);
        net_filter_past(filter, level);
        net_filter_answer(filter, SECCOMP_RET_KILL_PROCESS);
        net_filter_past(filter, call);

        //      getsockopt: SO_ERROR, after a connect that did not finish.
        call = net_filter_when(filter, (p32)syscall(getsockopt));
        net_filter_require(filter, 1, ~(p32)0, sol_socket, array_count(sol_socket));
        net_filter_require(filter, 2, ~(p32)0, so_error, array_count(so_error));
        net_filter_answer(filter, SECCOMP_RET_ALLOW);
        net_filter_past(filter, call);

        //      recvmsg, for a time query only.
        if (clock)
        {
                call = net_filter_when(filter, (p32)syscall(recvmsg));
                net_filter_answer(filter, SECCOMP_RET_ALLOW);
                net_filter_past(filter, call);
        }

        //      mmap: anything but executable.
        call = net_filter_when(filter, (p32)syscall(mmap));
        net_filter_load(filter, 2);
        net_filter_put(filter, NET_FILTER_ANY_BIT, 1, 0, NET_APART_EXECUTABLE);
        net_filter_answer(filter, SECCOMP_RET_ALLOW);
        net_filter_answer(filter, SECCOMP_RET_KILL_PROCESS);
        net_filter_past(filter, call);

        net_filter_answer(filter, SECCOMP_RET_KILL_PROCESS);
}

//      True only when the kernel took it. ENOSYS is a refusal like any other.
static COLD bool net_filter_install(net_filter address_to filter)
{
        struct
        {
                p16 count;
                net_filter_step address_to steps;
        } program = {(p16)filter->used, filter->steps};

        return filter->used <= NET_FILTER_STEPS &&
               system_call_3(syscall(seccomp), SECCOMP_SET_MODE_FILTER, 0,
                             (positive)address_of program) == 0;
}

/*
        Root becomes nobody, as the DHCP exchange does. A user namespace that
        maps root and not nobody (unshare -r, which the address-policy rows of
        wget_hostile run under) answers EINVAL for an id it has no mapping
        for, and EPERM for setgroups, which unshare turns off there: that
        root owns nothing outside the namespace, and the filter is the
        boundary. Any other refusal is a refusal.
*/
static COLD bool net_apart_nobody(void)
{
        bipolar status;

        if (system_call_1(syscall(getuid), 0) && system_call_1(syscall(geteuid), 0))
                return true;
        status = system_call_2(syscall(setgroups), 0, 0);
        if (status < 0 && status != -EPERM)
                return false;
        status = system_call_3(syscall(setresgid), NET_APART_NOBODY,
                               NET_APART_NOBODY, NET_APART_NOBODY);
        if (status < 0 && status != -EINVAL)
                return false;
        status = system_call_3(syscall(setresuid), NET_APART_NOBODY,
                               NET_APART_NOBODY, NET_APART_NOBODY);
        return status >= 0 || status == -EINVAL;
}

/* After the ids change, which clears both: no core and no ptrace by its
   owner, killed with the client that forked it (and gone already if the
   client went first), and no exec that grants anything. */
static COLD bool net_apart_tie(bipolar parent)
{
        return system_call_5(syscall(prctl), PR_SET_DUMPABLE, 0, 0, 0, 0) == 0 &&
               system_call_5(syscall(prctl), PR_SET_PDEATHSIG, SIGKILL, 0, 0, 0) == 0 &&
               system_call(syscall(getppid)) == parent &&
               system_call_5(syscall(prctl), PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) == 0;
}

/* What a confined child does: its work, with its answer written to answer,
   a pipe whose other end the caller reads to its end. True when the whole
   answer went in. */
typedef bool (*net_apart_task)(address_any context, bipolar answer);

static DEAD_END COLD fn net_apart_child(net_apart_task task, address_any context,
                                        bipolar keep, b32 answer, bipolar parent,
                                        bool clock)
{
        p32 kept[3] = {(p32)answer, 2, keep >= 0 ? (p32)keep : descriptors_none};
        net_filter filter;

        dns_servers_hold((string_address) "/etc/resolv.conf");
        dns_tls_held = dns_tls_mode() + 1;
        clock_floor_held = clock_trust_floor();
        net_filter_build(address_of filter, clock);
        if (!descriptors_close_outside(0, kept, array_count(kept)) ||
            !net_apart_nobody() || !net_apart_tie(parent) ||
            !net_filter_install(address_of filter))
                system_call_1(syscall(exit_group), NET_APART_EXIT_UNCONFINED);
        system_call_1(syscall(exit_group), task(context, answer) ? 0 : 1);
        __builtin_unreachable();
}

//      The signal the last confined child died of, for the line that says
//      so; 0 when it ended some other way.
static b32 net_apart_signal;

/*
        task in a confined child, with keep (or -1 for none) the one
        descriptor it may write besides its answer, and the answer read into
        answer, which the caller releases: NET_APART_OK when the child wrote
        at most `most` bytes and exited, NET_APART_UNCONFINED when it could not
        confine itself and did nothing, NET_APART_DIED when it died, said too
        much or ended otherwise, NET_APART_NO_CHILD when there was no child.
        clock is a time query's filter (net_filter_build).

        The parent reads to the end of the pipe before it waits, so a large
        answer cannot stall the child. A child that dies of SIGPIPE or SIGXFSZ
        died writing into keep, which would have killed the caller; the caller
        dies of it too, so wget -O - | head ends as it did. With SIGCHLD
        ignored the kernel would reap the child unasked, so the default holds
        while the child runs and is put back after.
*/
static COLD bipolar net_apart(net_apart_task task, address_any context,
                              bipolar keep, byte_store address_to answer,
                              positive most, bool clock)
{
        b32 ends[2];
        bipolar parent = system_call(syscall(getpid));
        bipolar child;
        positive raw = 0;
        positive was[4] = {0};
        bool whole = true;
        bool restore;
        bipolar waited;
        positive signal;

        net_apart_signal = 0;
        restore = system_signal_action(SIGCHLD, null, was, 8) >= 0 &&
                  (was[0] == (positive)SIG_IGN ||
                   (was[1] & SA_NOCLDWAIT)) &&
                  system_signal_install(SIGCHLD, (positive)SIG_DFL, 0, 0, null);
        log_flush();
        child = system_child_fork(ends, O_CLOEXEC);
        if (child < 0)
        {
                if (restore)
                        system_signal_action(SIGCHLD, was, null, 8);
                return NET_APART_NO_CHILD;
        }
        if (!child)
                net_apart_child(task, context, keep, ends[1], parent, clock);

        for (;;)
        {
                positive want = answer->used + 65536;
                bipolar got;

                if (want > most + 1)
                        want = most + 1;
                if (!byte_store_reserve(answer, want, 4096))
                {
                        whole = false;
                        break;
                }
                got = system_read_retry((positive)ends[0],
                                        answer->bytes + answer->used,
                                        want - answer->used);
                if (got <= 0)
                {
                        whole = !got;
                        break;
                }
                answer->used += (positive)got;
                if (answer->used > most)
                {
                        whole = false;
                        break;
                }
        }
        if (!whole)
                system_call_2(syscall(kill), (positive)child, SIGKILL);
        system_close(ends[0]);
        waited = system_wait4_retry(child, address_of raw, 0, null);
        if (restore)
                system_signal_action(SIGCHLD, was, null, 8);
        if (waited != child)
                return NET_APART_DIED;

        signal = raw & 0x7f;
        if (signal)
        {
                net_apart_signal = (b32)signal;
                if (whole && (signal == SIGPIPE || signal == SIGXFSZ))
                        system_call_2(syscall(kill), (positive)parent, signal);
                return NET_APART_DIED;
        }
        if (((raw >> 8) & 0xff) == NET_APART_EXIT_UNCONFINED && !answer->used)
                return NET_APART_UNCONFINED;
        return whole && !((raw >> 8) & 0xff) ? NET_APART_OK : NET_APART_DIED;
}

/*
        http_run, apart.

        The child runs http_run as the caller would have, with the caller's
        arguments as the fork left them -- the output descriptor is the one
        it keeps -- and answers with what http_run left behind: its status,
        the response code, why the last write failed, where it ended, the
        head when one was kept, and the body when it was stored. The caller
        believes that only as far as it can check it: a status http_run can
        return, an errno that is an errno, a URL shorter than a URL may be,
        a head no longer than a head may be, a body within what the manners
        allow, and lengths that add up to exactly what came. Anything else
        is HTTP_APART, as a child that died is.
*/
typedef struct
{
        b64 status;
        b64 code;
        b64 write_failure;
        p64 where_length;
        p64 head_length;
        p64 body_length;
} http_apart_said;

//      head_length when no head was kept.
#define HTTP_APART_NO_HEAD (~(p64)0)

typedef struct
{
        string_address start;
        const http_manners address_to how;
        bool check_cert;
        bipolar dest;
        bool body;
        bool head;
        b32 address_to code;
        p8 address_to where;
} http_apart_ask;

static COLD bool http_apart_write(bipolar answer, const address_any bytes,
                                  positive length)
{
        return !length || system_write_all((positive)answer, bytes, length) == length;
}

static COLD bool http_apart_task(address_any context, bipolar answer)
{
        http_apart_ask address_to ask = context;
        http_buffer body = {0};
        http_buffer head = {0};
        http_apart_said said = {0};

        said.status = http_run(ask->start, ask->how, ask->check_cert, ask->dest,
                               ask->body ? address_of body : null, ask->code,
                               ask->where, ask->head ? address_of head : null);
        said.code = ask->code ? address_to ask->code : 0;
        said.write_failure = http_write_failure;
        said.where_length = ask->where ? string_length(ask->where) : 0;
        said.head_length = head.bytes ? head.used : HTTP_APART_NO_HEAD;
        said.body_length = body.used;
        return http_apart_write(answer, address_of said, sizeof said) &&
               http_apart_write(answer, ask->where, said.where_length) &&
               (!head.bytes || http_apart_write(answer, head.bytes, head.used)) &&
               http_apart_write(answer, body.bytes, body.used);
}

static CONST COLD bool http_apart_status(b64 status)
{
        return status <= HTTP_OK && status >= HTTP_UNLOGGED && status != -6;
}

static COLD bool http_apart_believed(const http_apart_said address_to said,
                                     const http_apart_ask address_to ask,
                                     positive whole)
{
        bool head = said->head_length != HTTP_APART_NO_HEAD;

        return http_apart_status(said->status) &&
               said->code == (b32)said->code &&
               said->write_failure <= 0 && said->write_failure >= -4095 &&
               said->where_length < HTTP_URL_MAX &&
               (ask->where || !said->where_length) &&
               (!head || (ask->head && said->head_length <= HTTP_HEAD_MAX)) &&
               said->body_length <= ask->how->body_most &&
               (!said->body_length || (ask->body && said->status == HTTP_OK)) &&
               whole == sizeof *said + said->where_length +
                            (head ? said->head_length : 0) + said->body_length;
}

static COLD bipolar http_run_apart(string_address start,
                                   const http_manners address_to how,
                                   bool check_cert, bipolar dest,
                                   http_buffer address_to into,
                                   b32 address_to code, p8 address_to where,
                                   http_buffer address_to head_out)
{
        http_apart_ask ask = {start, how, check_cert, dest, into != null,
                              head_out != null, code, where};
        byte_store answer = {0};
        http_apart_said said;
        positive at = sizeof said;
        bipolar status;
        bipolar ran = net_apart(http_apart_task, address_of ask, dest,
                                address_of answer,
                                sizeof said + HTTP_URL_MAX + HTTP_HEAD_MAX +
                                    how->body_most, false);

        if (ran != NET_APART_OK || answer.used < sizeof said)
        {
                byte_store_release(address_of answer);
                return ran == NET_APART_UNCONFINED ? HTTP_CONFINE : HTTP_APART;
        }
        memory_copy(address_of said, answer.bytes, sizeof said);
        if (!http_apart_believed(address_of said, address_of ask, answer.used))
        {
                byte_store_release(address_of answer);
                return HTTP_APART;
        }

        status = (bipolar)said.status;
        if (code)
                address_to code = (b32)said.code;
        if (where)
        {
                memory_copy(where, answer.bytes + at, said.where_length);
                where[said.where_length] = end;
        }
        at += said.where_length;
        if (head_out && said.head_length != HTTP_APART_NO_HEAD)
        {
                bipolar kept = http_head_keep(head_out, answer.bytes + at,
                                              said.head_length);

                if (!status)
                        status = kept;
                at += said.head_length;
        }
        http_write_failure = (bipolar)said.write_failure;
        //      A body only on success, and then it replaces what the caller
        //      held, terminated as http_copy's store terminates it.
        if (into && !status)
        {
                byte_store_release(into);
                if (said.body_length)
                {
                        memory_copy(answer.bytes, answer.bytes + at,
                                    said.body_length);
                        answer.used = said.body_length;
                        answer.bytes[answer.used] = end;
                        address_to into = answer;
                        memory_fill(address_of answer, 0, sizeof answer);
                }
        }
        byte_store_release(address_of answer);
        return status;
}

//      fetch, wget and wget --spider, as the clients call them: http_get,
//      http_fetch_to and http_spider in a confined child.
static bipolar http_get_apart(string_address url, http_buffer address_to body,
                              b32 address_to code)
{
        return http_run_apart(url, address_of http_manners_fetch, false, -1,
                              body, code, null, null);
}

static bipolar http_spider_apart(string_address start, bool check_cert,
                                 b32 address_to code, p8 address_to where)
{
        return http_run_apart(start, address_of http_manners_spider, check_cert,
                              -1, null, code, where, null);
}

static bipolar http_fetch_to_apart(string_address start, bipolar dest,
                                   bool check_cert, b32 address_to code,
                                   p8 address_to where)
{
        return http_run_apart(start, address_of http_manners_wget, check_cert,
                              dest, null, code, where, null);
}

/*
        nts_ke, apart. The machine's NTP child asks it as root, and the key
        establishment is a TLS 1.3 handshake with an X.509 chain and a record
        of the server's own making, so all of it runs in a confined child,
        which answers with its status and the state it made. The state is
        believed only as nts_ke_reply could have written it: at least one
        cookie and no more than there is room for, each of a length a cookie
        may have, a time server that is a name or empty, a port not zero.
        The keys cross the pipe, and every copy but the caller's is wiped.
*/
typedef struct
{
        b64 status;
        nts_state state;
} nts_ke_said;

typedef struct
{
        string_address host;
        p16 port;
        bool bootstrap;
} nts_ke_ask;

static COLD bool nts_ke_task(address_any context, bipolar answer)
{
        nts_ke_ask address_to ask = context;
        nts_ke_said said;
        bool wrote;

        memory_fill(address_of said, 0, sizeof said);
        said.status = nts_ke(ask->host, ask->port, ask->bootstrap,
                             address_of said.state);
        wrote = system_write_all((positive)answer, address_of said, sizeof said) ==
                sizeof said;
        crypto_forget(address_of said, sizeof said);
        return wrote;
}

//      A state as nts_ke_reply and the cookies it took leave it: one to eight
//      cookies, each of a length a cookie may have, a port that is not zero
//      and a time server that is a name or empty. The origin is the caller's.
static COLD bool nts_state_ok(const nts_state address_to state)
{
        if (!state->count || state->count > NTS_COOKIES || !state->port ||
            (state->server[0] &&
             !nts_name_ok(state->server, string_length((string_address)state->server))))
                return false;
        for (positive at = 0; at < state->count; at++)
                if (!state->length[at] || state->length[at] > NTS_COOKIE_MAX)
                        return false;
        return true;
}

static COLD bool nts_ke_believed(nts_ke_said address_to said)
{
        nts_state address_to state = address_of said->state;

        if (said->status)
                return http_apart_status(said->status);
        state->server[sizeof state->server - 1] = end;
        return nts_state_ok(state);
}

static COLD bipolar nts_ke_apart(string_address host, p16 port, bool bootstrap,
                                 nts_state address_to state)
{
        nts_ke_ask ask = {host, port, bootstrap};
        byte_store got = {0};
        nts_ke_said said;
        bipolar ran = net_apart(nts_ke_task, address_of ask, -1, address_of got,
                                sizeof said, false);
        bool whole = ran == NET_APART_OK && got.used == sizeof said;
        bipolar status = ran == NET_APART_UNCONFINED ? HTTP_CONFINE : HTTP_APART;

        memory_fill(state, 0, sizeof(*state));
        if (whole)
                memory_copy(address_of said, got.bytes, sizeof said);
        if (got.bytes)
                crypto_forget(got.bytes, got.used);
        byte_store_release(address_of got);
        if (!whole)
                return status;
        if (!nts_ke_believed(address_of said))
                status = HTTP_APART;
        else if (!(status = (bipolar)said.status))
                memory_copy(state, address_of said.state, sizeof(*state));
        crypto_forget(address_of said, sizeof said);
        return status;
}

/*
        A name asked apart: the DNS answer, and the TLS and X.509 of a
        resolver over TLS, are read in a confined child, which answers with a
        status and an address. The status is believed only as one the
        resolver returns. `host` asks under the DNS over TLS setting or of
        the server it names (dns_resolve_apart); hostid, logger, a Waterlink
        service and NTS's check of the time server it was named ask in plain,
        as net_resolve_v4 does (net_resolve_v4_apart, http_lookup_apart).
*/
typedef struct
{
        b64 status;
        p32 found;
} dns_apart_said;

typedef struct
{
        bipolar server;
        string_address name;
        positive seconds;
        bool plain;
} dns_apart_ask;

static COLD bool dns_apart_task(address_any context, bipolar answer)
{
        dns_apart_ask address_to ask = context;
        dns_apart_said said = {0};
        p32 found = 0;

        said.status =
            ask->plain ? net_resolve_v4(ask->name, address_of found, ask->seconds)
            : ask->server >= 0
                ? dns_resolve_at((p32)ask->server, DNS_PORT, ask->name,
                                 address_of found, ask->seconds)
                : dns_resolve_wanted((string_address) "/etc/resolv.conf",
                                     ask->name, address_of found, ask->seconds);
        said.found = said.status == DNS_OK ? found : 0;
        return system_write_all((positive)answer, address_of said, sizeof said) ==
               sizeof said;
}

//      A status the confined resolver may say: one of the resolver's own,
//      from DNS_OK down to DNS_TLS_FAILED. Anything else is a child that was
//      not the resolver any more.
static CONST COLD bool dns_apart_status(b64 status)
{
        return status <= DNS_OK && status >= DNS_TLS_FAILED;
}

static COLD bipolar dns_apart_ask_apart(dns_apart_ask address_to ask,
                                        p32 address_to found)
{
        byte_store got = {0};
        dns_apart_said said;
        bipolar ran = net_apart(dns_apart_task, ask, -1, address_of got,
                                sizeof said, false);
        bool whole = ran == NET_APART_OK && got.used == sizeof said;

        address_to found = 0;
        if (whole)
                memory_copy(address_of said, got.bytes, sizeof said);
        byte_store_release(address_of got);
        if (!whole)
                return ran == NET_APART_UNCONFINED ? DNS_CONFINE : DNS_APART;
        if (!dns_apart_status(said.status))
                return DNS_APART;
        if (said.status == DNS_OK)
                address_to found = said.found;
        return (bipolar)said.status;
}

//      server, or -1 for the setting's resolvers; a status the resolver
//      returns, DNS_CONFINE or DNS_APART.
static COLD bipolar dns_resolve_apart(bipolar server, string_address name,
                                      p32 address_to found)
{
        dns_apart_ask ask = {server, name, server >= 0 ? 5 : 3, false};

        return dns_apart_ask_apart(address_of ask, found);
}

/* net_resolve_v4, apart; an address is itself without a child, and so is
   localhost and every name under it (RFC 6761 6.3), which is never asked of
   a resolver: what GNU's tools are told by /etc/hosts, and what the HTTP
   client already does (http_lookup). */
static COLD bipolar net_resolve_v4_apart(string_address name,
                                         p32 address_to found, positive seconds)
{
        dns_apart_ask ask = {-1, name, seconds, true};
        bipolar numeric = string_to_host(name);

        if (numeric < 0 && http_host_loopback(name))
                numeric = HOST_LOOPBACK;
        if (numeric >= 0)
        {
                address_to found = (p32)numeric;
                return DNS_OK;
        }
        return dns_apart_ask_apart(address_of ask, found);
}

//      http_lookup, apart.
static COLD p32 http_lookup_apart(string_address host)
{
        p32 ip = 0;

        return net_resolve_v4_apart(host, address_of ip, 3) == DNS_OK ? ip : 0;
}

#endif // STANDARD_MODERN_C_NET_APART
/* ---- dhcp: dhcp: an address obtained rather than typed ---- */

#ifndef STANDARD_MODERN_C_NET_DHCP
#define STANDARD_MODERN_C_NET_DHCP

#include "wait.c"

/*
        The four messages, and the chicken and egg underneath them.

        A machine asking for an address has no address, so it cannot bind a
        socket to one and the server cannot route a reply back to it. DHCP
        works around that by broadcasting in both directions: the client sends
        from 0.0.0.0:68 to 255.255.255.255:67, and the server answers to the
        broadcast address, with the hardware address in the packet being the
        only thing that says which machine it is for.

        Two details make the difference between this working and this looking
        like a network fault:

        SO_BINDTODEVICE. With no address configured there is no route, so a
        send to 255.255.255.255 has no interface to leave by and fails with
        ENETUNREACH. Naming the interface on the socket is what supplies the
        answer the routing table cannot.

        The broadcast flag. A server may unicast its reply to an address the
        client does not have yet, which a UDP socket never receives and the
        packet socket an acquisition reads (dhcp_frames_open) does. Setting
        the flag asks for a broadcast reply, which arrives either way. qemu's
        user-mode network broadcasts regardless and echoes the flag back as
        zero, so this is the one part of the exchange that the boot lane
        structurally cannot check -- it is set because a real network needs
        it, not because a test proved it here.

        The network watcher ARP-probes a newly acknowledged address before it
        installs the lease, announces it once it is in, and schedules
        dhcp_reacquire at half its lifetime.
*/

#define DHCP_CLIENT_PORT 68
#define DHCP_SERVER_PORT 67

#define DHCP_HEAD 236
#define DHCP_COOKIE 0x63825363

#define DHCP_DISCOVER 1
#define DHCP_OFFER 2
#define DHCP_REQUEST 3
#define DHCP_DECLINE 4
#define DHCP_ACK 5
#define DHCP_NAK 6
#define DHCP_RELEASE 7

#define DHCP_OPTION_PAD 0
#define DHCP_OPTION_MASK 1
#define DHCP_OPTION_ROUTER 3
#define DHCP_OPTION_DNS 6
#define DHCP_OPTION_REQUESTED 50
#define DHCP_OPTION_LEASE 51
#define DHCP_OPTION_TYPE 53
#define DHCP_OPTION_SERVER 54
#define DHCP_OPTION_ASK 55
#define DHCP_OPTION_RENEWAL 58
#define DHCP_OPTION_REBINDING 59
#define DHCP_OPTION_END 255

#define DHCP_FLAG_BROADCAST 0x8000

#define DHCP_OK 0
#define DHCP_NO_SOCKET (-1)
#define DHCP_NO_OFFER (-2)
#define DHCP_REFUSED (-3)
#define DHCP_NO_RANDOM (-4)
//      Not outcomes: what the confined child says down its answer pipe
//      while it still works (dhcp_say), so that whoever waits for the
//      exchange knows when a server holds an address for this client --
//      from the REQUEST until its ACK window is over -- and which one.
#define DHCP_REQUESTED (-20)
#define DHCP_UNANSWERED (-21)

typedef struct
{
        p32 address;
        p32 mask;
        p32 router;
        p32 nameserver;
        p32 server;
        p32 seconds;
        p32 renewal;
        p32 rebinding;
} dhcp_lease;

/* One record down the answer pipe of an exchange run apart: the outcome and
   the lease once at the end, and before that DHCP_REQUESTED or
   DHCP_UNANSWERED with the offer a REQUEST names. */
typedef struct
{
        bipolar status;
        dhcp_lease lease;
} dhcp_answer;

/* A transaction id is visible beside the client's public hardware address and
   is the only unpredictable field an off-path reply must guess. Prefer the
   initialized CSPRNG without waiting; if early boot has not initialized it,
   wait for that same CSPRNG rather than drawing from GRND_INSECURE. DHCP runs
   as root and controls the address, gateway and resolver, so boot-time
   availability must not turn its reply identity into a predictable value. */
static bool dhcp_transaction_early(p32 address_to transaction)
{
        return network_transaction_secure(transaction, sizeof(*transaction)) ||
               system_random_fill(transaction, sizeof(*transaction), 0) == 0;
}

/* A subnet mask is a run of one bits followed by a run of zero bits.  Zero is
   retained as the existing "server omitted it" /24 policy; any other broken
   shape would silently configure a different network from the one offered. */
static CONST COLD bool dhcp_mask_valid(p32 mask)
{
        p32 after = ~mask;

        return !mask || !(after & (after + 1));
}

/* All destinations are actual p32 fields. Address comes from the fixed
   header; the remaining fields are options, some containing address lists. */
static const struct { p8 option, offset; bool multiple; } dhcp_fields[] = {
    {0, __builtin_offsetof(dhcp_lease, address), false},
    {DHCP_OPTION_MASK, __builtin_offsetof(dhcp_lease, mask), false},
    {DHCP_OPTION_ROUTER, __builtin_offsetof(dhcp_lease, router), true},
    {DHCP_OPTION_DNS, __builtin_offsetof(dhcp_lease, nameserver), true},
    {DHCP_OPTION_SERVER, __builtin_offsetof(dhcp_lease, server), false},
    {DHCP_OPTION_LEASE, __builtin_offsetof(dhcp_lease, seconds), false},
    {DHCP_OPTION_RENEWAL, __builtin_offsetof(dhcp_lease, renewal), false},
    {DHCP_OPTION_REBINDING, __builtin_offsetof(dhcp_lease, rebinding), false},
};

/* RFC 2131 requires T1 < T2 < expiry.  Each omitted timer gets its standard
   default (one half and seven eighths of the lease); a supplied pair which
   breaks the ordering is discarded as a pair rather than creating a state
   machine which can skip RENEWING or outlive the lease. */
static COLD bool dhcp_lease_timers(dhcp_lease address_to lease)
{
        p32 default_renewal;
        p32 default_rebinding;
        p32 renewal;
        p32 rebinding;

        if (!lease || !lease->seconds)
                return false;

        /* A one- or two-second lease is legal, but there are not enough whole
           seconds to encode both strict state boundaries.  Keep the lease and
           let the watcher wake only for its immediate expiry. */
        if (lease->seconds < 3)
        {
                lease->renewal = 0;
                lease->rebinding = 0;
                return true;
        }

        default_renewal = lease->seconds / 2;
        default_rebinding = lease->seconds -
                            (lease->seconds / 8 +
                             (lease->seconds % 8 != 0));
        renewal = lease->renewal ? lease->renewal : default_renewal;
        rebinding = lease->rebinding ? lease->rebinding : default_rebinding;

        if (!renewal || renewal >= rebinding ||
            rebinding >= lease->seconds)
        {
                renewal = default_renewal;
                rebinding = default_rebinding;
        }

        lease->renewal = renewal;
        lease->rebinding = rebinding;
        return renewal && renewal < rebinding && rebinding < lease->seconds;
}

/*
        One packet, built.

        The fixed part is 236 bytes of BOOTP with the hardware address in it,
        then the magic cookie that says the options which follow are DHCP's
        rather than BOOTP's, then the options themselves ending in 255.
*/
static COLD positive dhcp_build(p8 address_to into, positive room, p8 kind,
                           p32 transaction, p8 address_to hardware, p32 wanted,
                           p32 server, p32 holding, bool broadcast)
{
        positive at;

        if (room < 300)
                return 0;

        memory_fill(into, 0, 300);

        into[0] = 1;   // a request, from a client
        into[1] = 1;   // over ethernet
        into[2] = 6;   // whose addresses are six bytes
        into[3] = 0;   // and which no relay has forwarded

        network_store_32(into + 4, transaction);

        //      Ask to be answered by broadcast, unless we are renewing: a
        //      client that already holds an address can be replied to
        //      directly, and asking for a broadcast then is noise on every
        //      other machine's wire.
        if (broadcast)
                network_store_16(into + 10, DHCP_FLAG_BROADCAST);

        //      ciaddr. Zero while asking for an address; the address we
        //      already hold while asking to keep it, which is what tells the
        //      server this is a renewal rather than a new client.
        network_store_32(into + 12, holding);

        memory_copy(into + 28, hardware, 6);

        network_store_32(into + DHCP_HEAD, DHCP_COOKIE);

        at = DHCP_HEAD + 4;

        into[at++] = DHCP_OPTION_TYPE;
        into[at++] = 1;
        into[at++] = kind;

        //      The address asked for and the server chosen, when there are.
        for (positive i = 0; i < 2; i++)
                if (i ? server : wanted)
                {
                        into[at++] = i ? DHCP_OPTION_SERVER : DHCP_OPTION_REQUESTED;
                        into[at++] = 4;
                        network_store_32(into + at, i ? server : wanted);
                        at += 4;
                }

        //      What we would like to be told, which a server may ignore, and
        //      the end. Short packets are dropped by some servers and by some
        //      switches, so the 261 bytes at most written here are padded to
        //      the length everything accepts. A DECLINE or a RELEASE asks for
        //      nothing: RFC 2131's table 5 says MUST NOT to a parameter list
        //      in either.
        static const p8 ask[] = {DHCP_OPTION_ASK, 3, DHCP_OPTION_MASK,
                                 DHCP_OPTION_ROUTER, DHCP_OPTION_DNS,
                                 DHCP_OPTION_END};

        if (kind == DHCP_DECLINE || kind == DHCP_RELEASE)
                into[at] = DHCP_OPTION_END;
        else
                memory_copy(into + at, ask, sizeof ask);
        return 300;
}

/*
        A reply read for what it says.

        Options are walked rather than indexed: a server sends what it likes
        in whatever order, and the length byte is the only thing that says
        where the next one starts. A length that would run off the end is a
        corrupt packet and ends the walk rather than reading past it.

        A server short of room may carry options in the header's file and
        sname fields too, saying so with option 52 (RFC 2131): 1 for file, 2
        for sname, 3 for both, walked in that order after the options field.
        And any option may come in more than one piece, which is one option
        whose value is the pieces joined in the order they were found (RFC
        3396) -- a lease time split two and two, a router list longer than
        one piece. So each option this reads is gathered across every piece
        first and judged once, whole: a value is taken when the joined length
        is the option's own, or at least one address for a list, and the
        message type when it is one byte.
*/
#define DHCP_OPTION_OVERLOAD 52
#define DHCP_FILE 108
#define DHCP_SNAME 44

typedef struct
{
        p8 first[4];
        positive length;
} dhcp_gathered;

/* One region's options into the gathered pieces: -1 when a length runs past
   it. Option 52 counts only in the options field, where it is set. */
static COLD bipolar dhcp_walk(p8 address_to region, positive size,
                              dhcp_gathered address_to gathered,
                              p8 address_to overload)
{
        byte_reader reader = byte_reader_open(region, size);

        while (byte_reader_left(&reader))
        {
                p8 option = byte_reader_u8(&reader);
                byte_reader value;
                positive before;
                positive taken;

                if (option == DHCP_OPTION_END)
                {
                        /* Bytes after END are padding, not a second hidden
                           option stream. Require canonical PAD bytes so this
                           parser cannot disagree with a middlebox or another
                           client which keeps scanning after option 255. */
                        positive left = byte_reader_left(&reader);

                        return memory_span_byte(byte_reader_take(&reader, left),
                                                 DHCP_OPTION_PAD, left) == left
                                   ? 0 : -1;
                }

                if (option == DHCP_OPTION_PAD)
                        continue;

                value = byte_reader_vector8(&reader);
                if (!byte_reader_ok(&reader))
                        return -1;

                if (option == DHCP_OPTION_OVERLOAD)
                {
                        byte_reader once = value;
                        p8 code;

                        /* Option overload is legal exactly once, only in the
                           primary options area, with its one-byte value in
                           the RFC-defined 1..3 domain. Accepting malformed or
                           repeated controls makes file/sname interpretation
                           depend on which occurrence a parser chooses. */
                        if (!overload || address_to overload ||
                            byte_reader_left(&value) != 1)
                                return -1;
                        code = byte_reader_u8(&once);
                        if (!code || code > 3)
                                return -1;
                        address_to overload = code;
                }

                //      The first four bytes of the option, over every piece
                //      of it, and the length of all of them.
                before = gathered[option].length;
                gathered[option].length += byte_reader_left(&value);
                taken = before < 4 ? min(byte_reader_left(&value), 4 - before)
                                   : 0;
                if (taken)
                        memory_copy(gathered[option].first + before,
                                    byte_reader_take(&value, taken), taken);
        }

        /* RFC 2132 terminates every option stream with option 255. Reaching
           the region boundary through padding or an ordinary option is a
           truncated stream, including in overloaded file/sname regions. */
        return -1;
}

static COLD bipolar dhcp_read(p8 address_to packet, positive size, p32 transaction,
                         p8 address_to hardware, dhcp_lease address_to lease,
                         p8 address_to kind)
{
        dhcp_gathered gathered[256];
        dhcp_lease parsed = {0};
        p8 overload = 0;
        p8 parsed_kind = 0;

        if (size < DHCP_HEAD + 4)
                return -1;

        if (packet[0] != 2)  // not a reply
                return -1;

        /* Ethernet, and an address as long as Ethernet's. The chaddr
           comparison below reads six bytes and calls them a match; that is
           only this client's address if the reply agrees on what the medium
           is and how long an address on it runs. A reply naming another one
           is answering a question nobody here asked. */
        if (packet[1] != 1 || packet[2] != 6)
                return -1;

        if (network_load_32(packet + 4) != transaction)
                return -1;

        if (memory_compare(packet + 28, hardware, 6))
                return -1;

        if (network_load_32(packet + DHCP_HEAD) != DHCP_COOKIE)
                return -1;

        parsed.address = network_load_32(packet + 16);  // yiaddr

        memory_zero(gathered, sizeof(gathered));

        if (dhcp_walk(packet + DHCP_HEAD + 4, size - DHCP_HEAD - 4, gathered,
                      address_of overload) < 0 ||
            ((overload & 1) &&
             dhcp_walk(packet + DHCP_FILE, DHCP_HEAD - DHCP_FILE, gathered, null) < 0) ||
            ((overload & 2) &&
             dhcp_walk(packet + DHCP_SNAME, DHCP_FILE - DHCP_SNAME, gathered, null) < 0))
                return -1;

        if (gathered[DHCP_OPTION_TYPE].length == 1)
                parsed_kind = gathered[DHCP_OPTION_TYPE].first[0];

        for (positive i = 1; i < array_count(dhcp_fields); i++)
        {
                dhcp_gathered address_to one = gathered + dhcp_fields[i].option;

                if (one->length >= 4 &&
                    (one->length == 4 || dhcp_fields[i].multiple))
                        *(p32 *)((p8 *)&parsed + dhcp_fields[i].offset) =
                                network_load_32(one->first);
        }

        if (!parsed_kind || !dhcp_mask_valid(parsed.mask))
                return -1;

        *lease = parsed;
        address_to kind = parsed_kind;

        return 0;
}

//      A mask of n leading bits, said as the prefix length a route wants.
static CONST COLD p8 dhcp_prefix_of(p32 mask)
{
        p8 bits = 0;

        // Walk the 32-bit mask itself. Subtracting 32 from a CLZ assumed
        // a 64-bit positive; on ILP32 that wrapped a /24 into /248.
        while (bits < 32 && (mask & (0x80000000u >> bits)))
                bits++;

        return bits ? bits : 24;
}

/* A DHCPACK is allowed to omit options already supplied by its offer.  Packet
   parsing itself stays replacement-based so unrelated packets cannot bleed
   into one another; only the stateful exchange chooses to retain an earlier
   nonzero field. */
static COLD fn dhcp_lease_merge(dhcp_lease address_to lease,
                           const dhcp_lease address_to fresh)
{
        for (positive i = 0; i < array_count(dhcp_fields); i++)
        {
                p32 value = *(const p32 *)((const p8 *)fresh + dhcp_fields[i].offset);
                if (value)
                        *(p32 *)((p8 *)lease + dhcp_fields[i].offset) = value;
        }
}

/* What a server may hand out. The address, the router and the prefix the mask
   implies (an omitted mask is the /24 policy) must be unicast: not 0/8, 127/8
   or 224/3, so a /1 mask on a 10/8 address cannot route the loopback and every
   multicast group onto the wire. A router is another host, not the leased
   address. On a subnet wider than a /31 the first and last addresses name the
   subnet, not a host. A /32 may have an off-link router (the clouds hand them
   out); a wider prefix needs its router on that link. */
static CONST COLD bool dhcp_address_unicast(p32 address)
{
        return (address >> 24) && (address >> 24) != 127 && address < 0xe0000000u;
}

static CONST COLD bool dhcp_prefix_clear(p32 address, p32 mask)
{
        p32 network;

        if (!mask)
                mask = 0xffffff00u;
        network = address & mask;
        if (!((network ^ 0x00000000u) & mask & 0xff000000u) ||
            !((network ^ 0x7f000000u) & mask & 0xff000000u) ||
            !((network ^ 0xe0000000u) & mask & 0xe0000000u))
                return false;
        return mask >= 0xfffffffeu ||
               (address != network && address != (network | ~mask));
}

static COLD bool dhcp_lease_usable(const dhcp_lease address_to lease)
{
        p32 mask = lease->mask ? lease->mask : 0xffffff00u;

        return lease->address && lease->server && lease->seconds &&
               dhcp_mask_valid(lease->mask) &&
               dhcp_address_unicast(lease->address) &&
               dhcp_address_unicast(lease->server) &&
               (!lease->router || (dhcp_address_unicast(lease->router) &&
                                   lease->router != lease->address &&
                                   (mask == 0xffffffffu ||
                                    (lease->router & mask) ==
                                        (lease->address & mask)))) &&
               dhcp_prefix_clear(lease->address, lease->mask);
}

/* Every ACK starts a new lease interval.  Timer values from the OFFER or the
   preceding lease are relative to that older interval and cannot be inherited
   when the ACK omits options 58/59, especially when option 51 changed. */
static COLD bool dhcp_lease_acknowledge(dhcp_lease address_to lease,
                                   const dhcp_lease address_to answer)
{
        lease->renewal = 0;
        lease->rebinding = 0;
        dhcp_lease_merge(lease, answer);
        //      The caller must judge the merged address, prefix and router:
        //      each nonzero field is legal syntax, not necessarily a lease.
        return dhcp_lease_timers(lease);
}

/* An ACK that completes a REQUEST, taken into the lease it answers. The same
   server's ACK may leave out what it said before (RFC 2131 4.3.1 lets a
   renewal answer carry only what changed), and that is kept. Another
   server's -- which only REBINDING accepts -- starts a lease of its own: the
   mask, router, resolver and lifetime the old server gave are not this
   one's, and an ACK that leaves them out has none of them, as a client that
   reads each lease whole (systemd-networkd, dhclient) has none. An ACK that
   is no lease leaves the lease as it was, for the next answer to be judged
   against. */
static COLD bool dhcp_lease_take(dhcp_lease address_to lease,
                                 const dhcp_lease address_to answer)
{
        dhcp_lease next = *lease;

        if (answer->server != next.server)
        {
                next.mask = 0;
                next.router = 0;
                next.nameserver = 0;
                next.seconds = 0;
        }
        if (!dhcp_lease_acknowledge(address_of next, answer) ||
            !dhcp_lease_usable(address_of next))
                return false;
        *lease = next;
        return true;
}

/* OFFER, ACK and NAK all carry a mandatory server identifier; xid and chaddr
   identify the client, not the server.  Completing a selected OFFER and
   RENEWING are bound to that server, and an ACK must name the offered or held
   address.  REBINDING deliberately accepts an authoritative answer from any
   server, but its ACK must still name the address already in use. */
static COLD bool dhcp_reacquisition_answer_matches(
    p8 kind, const dhcp_lease address_to answer,
    const dhcp_lease address_to lease, bool rebinding)
{
        if (!answer || !lease || !answer->server ||
            (kind != DHCP_ACK && kind != DHCP_NAK))
                return false;
        if (!rebinding && answer->server != lease->server)
                return false;
        return kind != DHCP_ACK || answer->address == lease->address;
}

/*
        A reply as the wire carries it, judged as IP and UDP would have: IPv4
        with a header of five words or more; a total length covering the
        headers and no more than was read (link padding follows); no fragment,
        not even the first of several; UDP; the header checksum; a UDP length
        within the IP payload, the rest cut off as the kernel cuts it; the UDP
        checksum when the sender wrote one, unless the kernel vouches for it
        (dhcp_frame_vouched); and port 67 to port 68. What is left is the DHCP
        message, and the addresses and source port a UDP socket would name.
*/
typedef struct
{
        p32 source;
        p32 destination;
        p16 source_port;
        const p8 address_to message;
        positive length;
} dhcp_frame;

/* The ones' complement sum of a block added to sum, folded to sixteen bits:
   0xffff over a block whose own checksum is in it and right. */
static COLD p32 dhcp_sum(byte_reader block, p32 sum)
{
        while (byte_reader_left(&block) >= 2)
                sum += byte_reader_u16(&block);
        if (byte_reader_left(&block))
                sum += (p32)byte_reader_u8(&block) << 8;
        while (sum >> 16)
                sum = (sum & 0xffff) + (sum >> 16);
        return sum;
}

static COLD bool dhcp_unframe(const p8 address_to frame, positive size,
                              bool vouched, dhcp_frame address_to into)
{
        byte_reader fields = byte_reader_open(frame, size);
        byte_reader rest = fields;
        p8 lead = byte_reader_u8(&fields);
        positive header = (positive)(lead & 15) * 4;
        positive total;
        p16 fragment;
        p8 protocol;
        p32 source;
        p32 destination;
        byte_reader datagram;
        byte_reader udp;
        p16 from;
        p16 to;
        p16 length;
        p16 checksum;

        (void)byte_reader_u8(&fields);           // type of service
        total = byte_reader_u16(&fields);
        (void)byte_reader_u16(&fields);          // identification
        fragment = byte_reader_u16(&fields);
        (void)byte_reader_u8(&fields);           // time to live
        protocol = byte_reader_u8(&fields);
        (void)byte_reader_u16(&fields);          // header checksum
        source = byte_reader_u32(&fields);
        destination = byte_reader_u32(&fields);
        //      The reserved flag is not asked about, as the kernel does not.
        if (!byte_reader_ok(&fields) || lead >> 4 != 4 || header < 20 ||
            total < header + 8 || total > size || (fragment & 0x3fff) ||
            protocol != 17)
                return false;

        datagram = byte_reader_window(&rest, total);
        if (dhcp_sum(byte_reader_window(&datagram, header), 0) != 0xffff)
                return false;
        udp = datagram;
        from = byte_reader_u16(&datagram);
        to = byte_reader_u16(&datagram);
        length = byte_reader_u16(&datagram);
        checksum = byte_reader_u16(&datagram);
        if (from != DHCP_SERVER_PORT || to != DHCP_CLIENT_PORT || length < 8 ||
            length > total - header)
                return false;
        udp = byte_reader_window(&udp, length);
        //      The pseudo-header (both addresses, the protocol, the UDP
        //      length) and the datagram, its checksum in it.
        if (checksum && !vouched &&
            dhcp_sum(udp, (source >> 16) + (source & 0xffff) +
                              (destination >> 16) + (destination & 0xffff) +
                              17 + length) != 0xffff)
                return false;
        (void)byte_reader_skip(&udp, 8);
        if (!byte_reader_ok(&udp))
                return false;
        into->source = source;
        into->destination = destination;
        into->source_port = from;
        into->message = byte_reader_here(&udp);
        into->length = byte_reader_left(&udp);
        return true;
}

/*
        Whether the kernel vouches for a frame's UDP checksum. PACKET_AUXDATA's
        tp_status says CSUMNOTREADY (a frame sent from this machine across a
        virtual wire, its checksum left for a card that never runs) or
        CSUM_VALID (a card checked it on the way in); the UDP layer would not
        have asked either, so neither does dhcp_unframe. The messages are the
        kernel's, not the wire's, and none is believed from a cut-short buffer.
*/
#define DHCP_SOL_PACKET 263
#define DHCP_PACKET_AUXDATA 8
#define DHCP_CHECKSUM_NOT_READY 8
#define DHCP_CHECKSUM_VALID 0x80

static COLD bool dhcp_frame_vouched(const p8 address_to control,
                                    positive size)
{
        byte_reader messages = byte_reader_open(control, size);

        while (byte_reader_left(&messages) >= 16)
        {
                positive low = byte_reader_u32le(&messages);
                positive length = low | (positive)byte_reader_u32le(&messages) << 32;
                p32 level = byte_reader_u32le(&messages);
                p32 type = byte_reader_u32le(&messages);
                byte_reader data;

                if (length < 16)
                        return false;
                data = byte_reader_window(&messages, length - 16);
                if (level == DHCP_SOL_PACKET && type == DHCP_PACKET_AUXDATA)
                {
                        p32 status = byte_reader_u32le(&data);

                        return byte_reader_ok(&data) &&
                               (status & (DHCP_CHECKSUM_NOT_READY |
                                          DHCP_CHECKSUM_VALID));
                }
                (void)byte_reader_skip(&messages, (8 - length % 8) % 8);
        }
        return false;
}

/*
        The exchange, confined.

        A reply is parsed as root, and any host on the wire can write one. So
        the watcher runs each exchange in a child (net_dhcp_apart), which
        opens and binds its sockets while it may and then comes here: every
        descriptor but the client socket, the packet socket an acquisition
        reads (dhcp_frames) and the answer's pipe is closed, every id becomes
        nobody's, no exec may grant anything back, and a filter kills any
        call beyond the exchange's own -- recvfrom, recvmsg, ppoll,
        clock_gettime, close, exit -- and sendto and write, which may name
        only the client socket and the pipe: a packet socket that can be
        written to sends any frame at all. What a crafted packet could make
        of the child is then DHCP from port 68 on that link, which any host
        on it can send already, and one lease, which the parent judges as it
        would a packet.

        No filter is no exchange. A kernel that will not install one -- built
        without SECCOMP_FILTER, which every Moonwater kernel has
        (build_essentials), or qemu-user, which cannot filter itself -- gets
        no DHCP from the child, which refuses before it reads a byte, rather
        than parse hostile packets unfiltered. Only that child is confined;
        the checks that call dhcp_ask directly leave dhcp_apart unset.
*/
static b32 dhcp_apart = -1;

/* The packet socket an acquisition reads its answers from (dhcp_frames_open),
   or -1 when it has none and reads its UDP socket as a renewal does. Open
   only while dhcp_ask runs. */
static bipolar dhcp_frames = -1;

/*
        The exchange's filter, net_apart's kind (net_filter): the architecture,
        then the call. The client socket's send and the answer's write are
        pinned to their descriptors; x32's bit-30 numbers match nothing listed
        and are killed with the rest.
*/
static COLD fn dhcp_filter_build(net_filter address_to filter, b32 handle)
{
        static const p32 allowed[] = {
            (p32)syscall(recvfrom), (p32)syscall(recvmsg), (p32)syscall(ppoll),
            (p32)syscall(clock_gettime), (p32)syscall(close),
            (p32)syscall(exit_group), (p32)syscall(exit),
            (p32)syscall(rt_sigreturn), (p32)syscall(restart_syscall)};
        const p32 pinned[2][2] = {{(p32)syscall(sendto), (p32)handle},
                                  {(p32)syscall(write), (p32)dhcp_apart}};
        positive count = array_count(allowed);

        filter->used = 0;
        net_filter_put(filter, NET_FILTER_LOAD, 0, 0, NET_FILTER_ARCH);
        net_filter_put(filter, NET_FILTER_EQUAL, 1, 0, NET_APART_ARCH);
        net_filter_answer(filter, SECCOMP_RET_KILL_PROCESS);
        net_filter_put(filter, NET_FILTER_LOAD, 0, 0, NET_FILTER_NUMBER);
        //      A pinned call, then its descriptor: the low half of its first
        //      argument, which is all of the int the kernel reads. Another
        //      call goes on to the next block.
        for (positive i = 0; i < array_count(pinned); i++)
        {
                positive call = net_filter_when(filter, pinned[i][0]);

                net_filter_require(filter, 0, ~(p32)0, pinned[i] + 1, 1);
                net_filter_answer(filter, SECCOMP_RET_ALLOW);
                net_filter_past(filter, call);
        }
        for (positive i = 0; i < count; i++)
                net_filter_put(filter, NET_FILTER_EQUAL, (p8)(count - i), 0, allowed[i]);
        net_filter_answer(filter, SECCOMP_RET_KILL_PROCESS);
        net_filter_answer(filter, SECCOMP_RET_ALLOW);
}

static COLD bool dhcp_confine(b32 handle)
{
        p32 keep[3] = {(p32)handle, (p32)dhcp_apart,
                       dhcp_frames >= 0 ? (p32)dhcp_frames : descriptors_none};
        net_filter filter;

        if (dhcp_apart < 0)
                return true;
        if (!descriptors_close_outside(0, keep, 3))
                return false;

        if ((!system_call_1(syscall(getuid), 0) &&
             (system_call_2(syscall(setgroups), 0, 0) < 0 ||
              system_call_3(syscall(setresgid), NET_APART_NOBODY,
                            NET_APART_NOBODY, NET_APART_NOBODY) < 0 ||
              system_call_3(syscall(setresuid), NET_APART_NOBODY,
                            NET_APART_NOBODY, NET_APART_NOBODY) < 0)) ||
            system_call_5(syscall(prctl), PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) < 0)
                return false;
        dhcp_filter_build(address_of filter, handle);
        return net_filter_install(address_of filter);
}

/* Says how far the exchange has got to whoever waits for it apart
   (DHCP_REQUESTED, DHCP_UNANSWERED), when somebody does. */
static COLD fn dhcp_say(bipolar status, const dhcp_lease address_to lease)
{
        dhcp_answer said = {status, *lease};

        if (dhcp_apart >= 0)
                system_write_all((positive)dhcp_apart, address_of said,
                                 sizeof said);
}

static COLD bipolar dhcp_open(string_address device, p32 host, bool broadcast)
{
        bipolar handle = socket_new(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
        b32 one = 1;

        if (handle < 0)
                return handle;

        socket_address_internet mine = {
            .family = AF_INET, .port = network_order_16(DHCP_CLIENT_PORT),
            .host = network_order_32(host)};

        if ((broadcast && socket_option_set((b32)handle, SOL_SOCKET, SO_BROADCAST,
                                            address_of one, sizeof one) < 0) ||
            socket_option_set((b32)handle, SOL_SOCKET, SO_REUSEADDR,
                              address_of one, sizeof one) < 0 ||
            socket_option_set((b32)handle, SOL_SOCKET, SO_BINDTODEVICE, device,
                              string_length(device) + 1) < 0 ||
            socket_bind((b32)handle, address_of mine, sizeof mine) < 0 ||
            !dhcp_confine((b32)handle))
        {
                socket_close((b32)handle);
                return -1;
        }

        return handle;
}

/*
        The replies, read where reverse-path filtering cannot drop them.

        Before the client has an address, rp_filter 1 or 2 drops an OFFER
        whose source has no route back (measured on a KVM tap: OFFERs went
        out and no REQUEST came back). So an acquisition reads its answers
        from a packet socket on the interface, as dhclient and dhcpcd do,
        and still sends on its UDP socket. dhcp_frame_filter keeps UDP to
        port 68 that is not a later fragment; dhcp_unframe judges every
        frame it keeps. The filter is attached and locked before the socket
        is bound, so no frame gets in unseen and the confined child cannot
        remove it. -1 when packet sockets are refused: the acquisition then
        reads its UDP socket. Renewals hold an address and a route and stay
        on UDP.
*/
#define DHCP_SO_ATTACH_FILTER 26
#define DHCP_SO_LOCK_FILTER 44
#define DHCP_SIOCGIFINDEX 0x8933
#define DHCP_PACKET_HOST 0
#define DHCP_PACKET_BROADCAST 1
#define DHCP_MSG_CTRUNC 8
#define DHCP_FRAME_ROOM 1536

static const net_filter_step dhcp_frame_filter[] = {
    {0x30, 0, 0, 9},                    // ldb [9]: the protocol
    {0x15, 0, 6, 17},                   //   UDP, or drop
    {0x28, 0, 0, 6},                    // ldh [6]: flags and fragment offset
    {0x45, 4, 0, 0x1fff},               //   a later fragment: drop
    {0xb1, 0, 0, 0},                    // ldxb 4 * ([0] & 15): header length
    {0x48, 0, 0, 2},                    // ldh [x + 2]: destination port
    {0x15, 0, 1, DHCP_CLIENT_PORT},     //   68, or drop
    {0x06, 0, 0, 0xffff},               // keep it
    {0x06, 0, 0, 0}};                   // drop it

static COLD bipolar dhcp_frames_open(string_address device)
{
        struct
        {
                p16 count;
                const net_filter_step address_to steps;
        } filter = {(p16)array_count(dhcp_frame_filter), dhcp_frame_filter};
        struct
        {
                p8 name[16];
                b32 index;
                p8 rest[20];
        } request = {{0}, 0, {0}};
        socket_address_packet link = {
            .family = AF_PACKET, .protocol = network_order_16(ETH_P_IP)};
        positive length = string_length(device);
        bipolar handle;
        b32 one = 1;

        if (!length || length >= sizeof request.name)
                return -1;
        handle = socket_new(AF_PACKET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
        if (handle < 0)
                return -1;
        memory_copy(request.name, device, length);
        if (system_call_3(syscall(ioctl), (positive)handle, DHCP_SIOCGIFINDEX,
                          (positive)address_of request) < 0 ||
            socket_option_set((b32)handle, DHCP_SOL_PACKET, DHCP_PACKET_AUXDATA,
                              address_of one, sizeof one) < 0 ||
            socket_option_set((b32)handle, SOL_SOCKET, DHCP_SO_ATTACH_FILTER,
                              address_of filter, sizeof filter) < 0 ||
            socket_option_set((b32)handle, SOL_SOCKET, DHCP_SO_LOCK_FILTER,
                              address_of one, sizeof one) < 0)
                goto refused;
        link.index = (p32)request.index;
        if (socket_bind((b32)handle, address_of link, sizeof link) < 0)
                goto refused;
        return handle;

refused:
        socket_close((b32)handle);
        return -1;
}

static COLD fn dhcp_frames_close(void)
{
        if (dhcp_frames >= 0)
                socket_close((b32)dhcp_frames);
        dhcp_frames = -1;
}

typedef struct
{
        address_any base;
        positive length;
} dhcp_vector;

typedef struct
{
        address_any name;
        p32 name_size;
        dhcp_vector address_to vectors;
        positive vector_count;
        address_any control;
        positive control_size;
        b32 flags;
} dhcp_message_header;

/* The next DHCP message off the packet socket, network_receive_next's way:
   its length, with the message copied to `into`; 0 for a frame that is not
   a DHCP reply this machine may read (sent by it, to another station's
   hardware address, or refused by dhcp_unframe), which the caller counts
   against its allowance as it counts any refused datagram; NETWORK_SILENT
   when the deadline went by first; or the error that ended the wait or the
   read. The read never blocks. The peer is the IP source and the UDP source
   port, as a UDP socket would have named them, and `destination` the IP
   destination, which a socket would have kept to itself. */
static COLD bipolar dhcp_frame_next(bipolar frames, p8 address_to into,
                                    positive room,
                                    socket_address_internet address_to peer,
                                    p32 address_to peer_size,
                                    p32 address_to destination,
                                    const network_deadline address_to deadline)
{
        for (;;)
        {
                p8 frame[DHCP_FRAME_ROOM];
                p8 control[64];
                socket_address_packet from;
                dhcp_vector vector = {frame, sizeof frame};
                dhcp_message_header message = {
                    address_of from, sizeof from, address_of vector, 1,
                    control, sizeof control, 0};
                dhcp_frame found;
                positive seconds;
                positive nanoseconds;
                bipolar got;

                if (!network_deadline_left(deadline, address_of seconds,
                                           address_of nanoseconds))
                        return NETWORK_SILENT;
                memory_zero(address_of from, sizeof from);
                got = system_call_3(syscall(recvmsg), (positive)frames,
                                    (positive)address_of message,
                                    MSG_TRUNC | MSG_DONTWAIT);
                if (got == NETWORK_INTERRUPTED)
                        continue;
                if (got == NETWORK_TRY_AGAIN)
                {
                        bipolar ready = network_wait_readable_until(frames,
                                                                     deadline);

                        if (ready <= 0)
                                return ready ? ready : NETWORK_SILENT;
                        continue;
                }
                if (got < 0)
                        return got;
                memory_zero(peer, sizeof *peer);
                address_to peer_size = sizeof *peer;
                if ((positive)got > sizeof frame ||
                    message.name_size <= __builtin_offsetof(socket_address_packet,
                                                            pkttype) ||
                    (from.pkttype != DHCP_PACKET_HOST &&
                     from.pkttype != DHCP_PACKET_BROADCAST) ||
                    !dhcp_unframe(frame, (positive)got,
                                  !(message.flags & DHCP_MSG_CTRUNC) &&
                                      dhcp_frame_vouched(
                                          control, message.control_size),
                                  address_of found) ||
                    !found.length || found.length > room)
                        return 0;
                memory_copy(into, found.message, found.length);
                peer->family = AF_INET;
                peer->port = network_order_16(found.source_port);
                peer->host = network_order_32(found.source);
                address_to destination = found.destination;
                return (bipolar)found.length;
        }
}

/* Packet fields identify the DHCP transaction; the datagram endpoint says
   who supplied them.  Every accepted reply comes from server port 67.
   Acquisition explicitly permits any OFFER host, whose selected transport
   peer is copied out and then required exactly for the completing ACK or NAK.
   Keeping that policy separate from the address matters because 0.0.0.0 is
   itself a possible source and must become an exact peer once selected.
   This also binds a relayed exchange to the relay endpoint that supplied the
   offer rather than confusing option 54 with the UDP sender. */
static COLD bool dhcp_peer_matches(
    const socket_address_internet address_to peer, p32 peer_size,
    const socket_address_internet address_to expected, bool any_host)
{
        return peer && expected && peer_size == sizeof(*peer) &&
               peer->family == AF_INET && expected->family == AF_INET &&
               peer->port == expected->port &&
               (any_host || peer->host == expected->host);
}

static COLD bool dhcp_receive(bipolar handle, p8 address_to packet, positive room,
                         p32 transaction, p8 address_to hardware,
                         dhcp_lease address_to lease, p8 address_to kind,
                         const socket_address_internet address_to expected_peer,
                         bool any_peer_host,
                         socket_address_internet address_to accepted_peer,
                         positive address_to remaining,
                         const network_deadline address_to deadline)
{
        positive own_remaining = NETWORK_DISCARD_MAX;

        if (!remaining)
                remaining = address_of own_remaining;

        for (;;)
        {
                socket_address_internet peer;
                p32 peer_size;
                p32 destination = HOST_BROADCAST;
                bool framed = dhcp_frames >= 0;
                bipolar got = framed
                    ? dhcp_frame_next(dhcp_frames, packet, room, address_of peer,
                                      address_of peer_size,
                                      address_of destination, deadline)
                    : network_receive_next(handle, packet, room, address_of peer,
                                           address_of peer_size, deadline);
                dhcp_lease parsed;

                if (got < 0)
                        return false;

                //      A frame has no socket to have kept out what was sent
                //      to another station's address: it is for this client
                //      when it is broadcast or names the address it offers.
                if (got && (positive)got <= room &&
                    dhcp_peer_matches(address_of peer, peer_size,
                                      expected_peer, any_peer_host) &&
                    dhcp_read(packet, (positive)got, transaction, hardware,
                              address_of parsed, kind) >= 0 &&
                    (destination == HOST_BROADCAST ||
                     (parsed.address && destination == parsed.address)))
                {
                        *lease = parsed;
                        if (accepted_peer)
                                *accepted_peer = peer;
                        return true;
                }
                if (!network_discard_one(remaining))
                        return false;
        }
}

/* The ACK or NAK that completes a REQUEST, from the peer the exchange is
   bound to. Acquisition completes the offer as a renewal would: same server,
   same address. Rebinding accepts any server's answer from any host. */
static COLD bipolar dhcp_complete(bipolar handle, p8 address_to packet,
                             positive room, p32 transaction,
                             p8 address_to hardware,
                             dhcp_lease address_to lease,
                             const socket_address_internet address_to peer,
                             bool rebinding,
                             const network_deadline address_to deadline)
{
        dhcp_lease answer;
        p8 kind = 0;
        positive remaining = NETWORK_DISCARD_MAX;

        while (dhcp_receive(handle, packet, room, transaction, hardware,
                            address_of answer, address_of kind, peer,
                            rebinding, null, address_of remaining, deadline))
                if (dhcp_reacquisition_answer_matches(kind, address_of answer,
                                                      lease, rebinding) &&
                    (kind == DHCP_NAK ||
                     dhcp_lease_take(lease, address_of answer)))
                        return kind == DHCP_ACK ? DHCP_OK : DHCP_REFUSED;
                else if (!network_discard_one(address_of remaining))
                        break;

        return DHCP_NO_OFFER;
}

/*
        The exchange, with a schedule rather than a single try.

        A link brought up a moment ago has not finished negotiating, and the
        first DISCOVERs sent into that gap are lost, so what decides when a
        machine is on the network is how soon after carrier the next one goes
        out: four a second for the first three seconds, then backing off.

        The transaction id comes from the initialized CSPRNG without waiting,
        else from a blocking read of that same CSPRNG, never GRND_INSECURE: a
        guessable xid lets an off-path reply through. The blocking read costs
        about 0.85 s once, on a machine with neither RDRAND nor virtio-rng;
        with either, nothing waits.
*/
static bipolar dhcp_ask(string_address device, p8 address_to hardware,
                        dhcp_lease address_to lease)
{
        p8 packet[1024];
        p8 spread[8];
        p32 transaction;
        bipolar handle;
        bipolar status;
        positive length;
        positive attempt;
        positive wait;

        memory_fill(lease, 0, sizeof(dhcp_lease));
        if (!dhcp_transaction_early(address_of transaction))
                return DHCP_NO_RANDOM;
        //      The backoff's spread, drawn now: the confined child may not
        //      ask for randomness later. The pool answered for the
        //      transaction, so it is ready; a refusal leaves no spread.
        if (system_random_fill(spread, sizeof spread, 1))
                memory_fill(spread, 128, sizeof spread);
        //      The packet socket first: dhcp_open confines the child, after
        //      which nothing could open one.
        dhcp_frames = dhcp_frames_open(device);
        handle = dhcp_open(device, HOST_ANY, true);

        if (handle < 0)
        {
                dhcp_frames_close();
                return DHCP_NO_SOCKET;
        }

        socket_address_internet where = {
            .family = AF_INET, .port = network_order_16(DHCP_SERVER_PORT),
            .host = network_order_32(HOST_BROADCAST)};
        socket_address_internet any_server = {
            .family = AF_INET, .port = network_order_16(DHCP_SERVER_PORT)};
        socket_address_internet selected_peer;

        for (attempt = 0; attempt < 20; attempt++)
        {
                p8 kind = 0;
                network_deadline deadline;
                positive remaining = NETWORK_DISCARD_MAX;

                /*
                        A quarter second apart for the first three seconds,
                        then the backoff, for a network with no server on it.
                        The first waits only 50 ms: a link brought up a moment
                        ago drops the first send while reporting success (on
                        KVM half of all boots put it nowhere, and took the
                        lease 250 ms late).
                */
                //      The backoff, once a server has had three seconds
                //      to answer, moves by up to a second either way (RFC
                //      2131 4.1), so that machines that lost their server
                //      together -- a power cut, a switch restarting -- do
                //      not keep asking it in step.
                wait = !attempt ? 50 : attempt < 12 ? 250
                       : (attempt - 11) * 2000 - 1000 +
                             spread[attempt - 12] * 2000 / 255;
                length = dhcp_build(packet, sizeof packet, DHCP_DISCOVER,
                                    transaction, hardware, 0, 0, 0, true);

                //      A failed send is most likely no route yet. An
                //      unconnected UDP socket routes every send afresh, so
                //      carrier can appear underneath it; the attempt still
                //      waits out its interval rather than spending the whole
                //      schedule in one instant.
                (void)socket_send((b32)handle, packet, length, 0,
                                  address_of where, sizeof where);

                if (!network_deadline_begin(address_of deadline, wait / 1000,
                                            wait % 1000 * 1000000))
                        continue;

                while (dhcp_receive(handle, packet, sizeof packet, transaction,
                                    hardware, lease, address_of kind,
                                    address_of any_server,
                                    true,
                                    address_of selected_peer,
                                    address_of remaining,
                                    address_of deadline))
                {
                        if (kind != DHCP_OFFER || !dhcp_lease_usable(lease))
                        {
                                if (!network_discard_one(address_of remaining))
                                        break;
                                continue;
                        }

                        //      Take the offer, naming the server so that any
                        //      other server that offered knows it lost.
                        length = dhcp_build(packet, sizeof packet, DHCP_REQUEST,
                                            transaction, hardware, lease->address,
                                            lease->server, 0, true);

                        //      A server will hold the offered address for
                        //      this client until the ACK window is over:
                        //      whoever waits for the exchange is told, so
                        //      that link news lets the ACK come first. Told
                        //      before the send, so that no news the REQUEST
                        //      sets off can be read first.
                        dhcp_say(DHCP_REQUESTED, lease);
                        if (socket_send(
                                (b32)handle, packet, length, 0,
                                address_of where, sizeof where) < 0)
                        {
                                /* The REQUEST never left.  Start the next
                                   discovery attempt immediately instead of
                                   spending its whole reply budget waiting for
                                   an answer that cannot exist. */
                                dhcp_say(DHCP_UNANSWERED, lease);
                                break;
                        }

                        //      An ACK is given at least the usual quarter.
                        if (!network_deadline_begin(
                                address_of deadline, wait / 1000,
                                (wait < 250 ? 250 : wait % 1000) * 1000000))
                        {
                                dhcp_say(DHCP_UNANSWERED, lease);
                                break;
                        }

                        status = dhcp_complete(
                            handle, packet, sizeof packet, transaction,
                            hardware, lease, address_of selected_peer, false,
                            address_of deadline);
                        if (status != DHCP_NO_OFFER)
                        {
                                socket_close((b32)handle);
                                dhcp_frames_close();
                                return status;
                        }
                        dhcp_say(DHCP_UNANSWERED, lease);
                        break;
                }
        }

        socket_close((b32)handle);
        dhcp_frames_close();
        return DHCP_NO_OFFER;
}

/*
        Keeping the address we already have.

        Half way through a lease the client asks, unicast to the server that
        gave it, to keep what it has: ciaddr set and no server identifier,
        which is what says "may I keep this" rather than "may I have one". A
        fresh DISCOVER may come back with a different address, and every
        connection open on this one dies with it.

        A timeout is not fatal and does not discard the address: the caller
        retries, switches to broadcast rebinding at T2, and starts discovery
        only after a NAK or the lease's expiry.
*/
static bipolar dhcp_reacquire(string_address device, p8 address_to hardware,
                              dhcp_lease address_to lease, bool rebinding,
                              positive wait)
{
        p8 packet[1024];
        p32 transaction;
        bipolar handle;
        bipolar status = DHCP_NO_OFFER;
        positive length;
        network_deadline deadline;

        if (!dhcp_lease_usable(lease) || !wait)
                return DHCP_NO_OFFER;

        if (!dhcp_transaction_early(address_of transaction))
                return DHCP_NO_RANDOM;
        handle = dhcp_open(device, lease->address, rebinding);

        if (handle < 0)
                return DHCP_NO_SOCKET;

        socket_address_internet where = {
            .family = AF_INET, .port = network_order_16(DHCP_SERVER_PORT),
            .host = network_order_32(rebinding ? HOST_BROADCAST
                                               : lease->server)};
        socket_address_internet expected = {
            .family = AF_INET, .port = network_order_16(DHCP_SERVER_PORT),
            .host = network_order_32(lease->server)};

        length = dhcp_build(packet, sizeof packet, DHCP_REQUEST, transaction,
                            hardware, 0, 0, lease->address, rebinding);

        if (socket_send((b32)handle, packet, length, 0, address_of where,
                        sizeof where) < 0)
                status = DHCP_NO_SOCKET;
        //      An ACK keeps what the renewal said, including the new lease
        //      time, but does not lose what it left out: it need not repeat
        //      every option.
        else if (network_deadline_begin(address_of deadline, wait, 0))
                status = dhcp_complete(handle, packet, sizeof packet,
                                       transaction, hardware, lease,
                                       address_of expected, rebinding,
                                       address_of deadline);

        socket_close((b32)handle);
        return status;
}

/*
        Giving back an address somebody else answers for.

        A station that answers the watcher's ARP probe of a fresh address
        means the server handed out an address in use. RFC 2131 4.4.1: the
        client broadcasts a DHCPDECLINE naming it (option 50) and the server
        (option 54), so the server marks it taken and offers another. Without
        that the same address came back on every DISCOVER until its lease ran
        out. And an exchange the watcher cut while its REQUEST was unanswered
        leaves the offered address held for this client: RFC 2131 4.4.6's
        DHCPRELEASE (ciaddr set, option 54, nothing asked) hands it back.
        Neither is answered, so each is one send from the confined child,
        whose filter already allows sendto. `kind` is DHCP_DECLINE or
        DHCP_RELEASE.
*/
static bipolar dhcp_give_back(string_address device, p8 address_to hardware,
                              const dhcp_lease address_to lease, p8 kind)
{
        p8 packet[300];
        p32 transaction;
        bipolar handle;
        bipolar sent;
        positive length;
        bool declined = kind == DHCP_DECLINE;

        if (!lease->address || !lease->server ||
            (!declined && kind != DHCP_RELEASE))
                return DHCP_NO_OFFER;
        if (!dhcp_transaction_early(address_of transaction))
                return DHCP_NO_RANDOM;
        handle = dhcp_open(device, HOST_ANY, true);
        if (handle < 0)
                return DHCP_NO_SOCKET;

        socket_address_internet where = {
            .family = AF_INET, .port = network_order_16(DHCP_SERVER_PORT),
            .host = network_order_32(HOST_BROADCAST)};

        length = dhcp_build(packet, sizeof packet, kind, transaction, hardware,
                            declined ? lease->address : 0, lease->server,
                            declined ? 0 : lease->address, false);
        sent = socket_send((b32)handle, packet, length, 0, address_of where,
                           sizeof where);
        socket_close((b32)handle);
        return sent == (bipolar)length ? DHCP_OK : DHCP_NO_SOCKET;
}

#endif // STANDARD_MODERN_C_NET_DHCP

#endif
