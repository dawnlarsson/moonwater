/*
        The network stack: one dependency tree that was six files.

        rtnetlink, DNS, HTTP -- which is reached only through TLS, which is
        reached only through the hashes and curves it needs -- and DHCP.
        Nothing here is a command. src/sh/net.c has ip, wget and ping, and
        test/checks.c builds this file with no shell around it at all, which
        is why it is its own unit and not folded in behind them.

        Each section keeps its own heading and its own include guard, so it
        still says what it is and what it refuses to know about its
        neighbours. wait.c stays beside this because src/sh/tools.c expands
        it too, and expands it first; anchors.inc stays beside it because it
        is a table.

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
#define NLA_TYPE_MASK 0x3fff

#define NETLINK_PREFER_ANY 0
#define NETLINK_PREFER_WIRED 1
#define NETLINK_PREFER_WIFI 2

#define IFA_ADDRESS 1
#define IFA_LOCAL 2
#define IFA_LABEL 3

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
        p32 message_length;
        netlink_header address_to header;

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
           iproute2's addattr_l places it, whatever the body length was. */
        length = sizeof(netlink_attribute) + size;
        padded = netlink_align(length);
        message_length = netlink_align(
            ((netlink_header address_to)buffer->bytes)->length);

        if (message_length > 0xffffffffu - padded)
        {
                buffer->failed = true;
                return false;
        }

        if (!net_room(buffer, buffer->used + padded))
                return false;

        /* net_room may move the allocation. Never carry an interior pointer
           across it. */
        header = (netlink_header address_to)buffer->bytes;

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

        header->length = (p32)(message_length + padded);

        return true;
}

/*
        A netlink socket, bound.

        The bind is what gives this socket a port so replies can find it, and
        a zero port asks the kernel to pick one -- which matters, because a
        shell may have forked and two processes claiming the same port is the
        one netlink error that looks like a hang rather than a failure.

        EXT_ACK is asked for and not required. It costs one setsockopt and
        turns "-EINVAL" into a sentence naming the attribute that was wrong;
        a kernel too old to know the option says so and everything else works
        exactly as before.
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

/*
        One datagram, however big it turns out to be.

        The first read only looks: MSG_PEEK leaves the datagram where it is
        and MSG_TRUNC makes the kernel answer with its true length rather than
        the length that fitted. So the room is taken knowing the size, and the
        second read is the one that consumes it. Without MSG_TRUNC a message
        larger than the buffer arrives silently shortened, and a dump that
        loses its tail looks exactly like a dump that ended.
*/
static bool netlink_source_is_kernel(
    const socket_address_netlink address_to source, p32 length)
{
        return length >= sizeof(*source) && source->family == AF_NETLINK &&
               source->port == 0;
}

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
        size = socket_receive(handle, null, 0, MSG_PEEK | MSG_TRUNC,
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
                socket_receive(handle, null, 0, 0, 0, 0);
                buffer->used = 0;
                return 0;
        }

        if (!size || (positive)size > NETLINK_DATAGRAM_MAX)
                return -1;

        if (!net_room(buffer, (positive)size + 64))
                return -1;

        source_length = sizeof source;
        memory_fill(address_of source, 0, sizeof source);
        got = socket_receive(handle, buffer->bytes, buffer->room, 0,
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

static COLD bipolar netlink_receive(b32 handle, netlink_buffer address_to buffer,
                               p32 address_to local_port)
{
        socket_address_netlink self_address;
        p32 local_length = sizeof self_address;
        bipolar got;
        bool require_kernel;

        memory_fill(address_of self_address, 0, sizeof self_address);
        got = socket_name(handle, address_of self_address,
                          address_of local_length);
        if (got < 0 || local_length < sizeof(self_address.family))
                return got < 0 ? got : -1;

        require_kernel = self_address.family == AF_NETLINK;
        /* A netlink port is meaningful only when getsockname returned the
           complete netlink address.  Other datagram families remain usable
           by the protocol's byte-level test harness, without weakening the
           kernel-source requirement on a real routing socket. */
        if (require_kernel && local_length < sizeof self_address)
                return -1;
        if (local_port)
                address_to local_port = require_kernel
                                            ? self_address.port : 0;

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

static COLD bipolar netlink_walk(b32 handle, netlink_buffer address_to request,
                            p32 sequence, netlink_buffer address_to reply,
                            netlink_visitor visit, address_any context);

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
        socket is handed back clean.
*/
static bipolar netlink_walk(b32 handle, netlink_buffer address_to request,
                            p32 sequence, netlink_buffer address_to reply,
                            netlink_visitor visit, address_any context)
{
        netlink_header address_to header;
        network_deadline deadline;
        bool enough = false;
        bipolar sent;
        positive at;

        if (request->failed)
                return -1;

        sent = socket_send(handle, request->bytes, request->used, 0, 0, 0);

        if (sent < 0)
                return sent;
        if (!network_deadline_begin(address_of deadline,
                                    NETLINK_TRANSACTION_SECONDS, 0))
                return -1;

        for (;;)
        {
                p32 local_port = 0;
                bipolar got = network_wait_readable_until(
                    handle, address_of deadline);

                if (got <= 0)
                        return got < 0 ? got : -1;

                got = netlink_receive(handle, reply,
                                      address_of local_port);

                /* A signal may land after ppoll reported the datagram but
                   before either receive consumes it.  The packet remains
                   queued, so retain the transaction's absolute deadline and
                   wait for the same reply again instead of turning a caught
                   signal into a failed routing operation. */
                if (got == NETWORK_INTERRUPTED)
                        continue;
                if (got < 0)
                        return got;

                at = 0;

                while (at + NETLINK_HEADER <= reply->used)
                {
                        header = (netlink_header address_to)(reply->bytes + at);

                        if (header->length < NETLINK_HEADER ||
                            at + header->length > reply->used)
                                return -1;

                        /* Kernel unicast replies carry the destination
                           socket's port id in nlmsg_pid.  Match it as well as
                           the sequence so an unrelated kernel notification
                           cannot complete this transaction. */
                        if (header->port != local_port ||
                            header->sequence != sequence)
                        {
                                at += netlink_align(header->length);
                                continue;
                        }

                        if (header->flags & NLM_DUMP_INTERRUPTED)
                                return -1;

                        if (header->type == NLMSG_IS_DONE)
                                return netlink_status(header, true);

                        if (header->type == NLMSG_IS_ERROR)
                                return netlink_status(header, false);

                        if (!enough && header->type != NLMSG_IS_NOOP && visit &&
                            !visit(header, context))
                                enough = true;

                        at += netlink_align(header->length);
                }
        }
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
static COLD address_any netlink_find_span(p8 address_to bytes, positive length,
                                     p16 type, positive address_to size)
{
        positive at = 0;
        netlink_attribute address_to attribute;

        while (at + sizeof(netlink_attribute) <= length)
        {
                attribute = (netlink_attribute address_to)(bytes + at);
                if (attribute->length < sizeof(netlink_attribute) ||
                    at + attribute->length > length)
                        return null;
                if ((attribute->type & NLA_TYPE_MASK) == type)
                {
                        if (size)
                                address_to size = attribute->length -
                                                  sizeof(netlink_attribute);
                        return bytes + at + sizeof(netlink_attribute);
                }
                at += netlink_align(attribute->length);
        }

        return null;
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
        p8 name[IFNAME_SIZE];
        p8 hardware[6];
} netlink_search;

static bipolar netlink_dump(b32 handle, p16 type, positive body, p8 family,
                            netlink_visitor visit, address_any context);

static inline INLINE string_address netlink_link_name(
    netlink_header address_to header, netlink_link address_to address_to link)
{
        positive length = 0;
        string_address name;

        address_to link = null;

        /* Visitors are also used directly by tests and by callers parsing
           multicast frames.  Do not hand either one a body pointer until the
           fixed family-specific body is present in full. */
        if (header->length < NETLINK_HEADER + sizeof(netlink_link))
                return null;

        address_to link = (netlink_link address_to)((p8 address_to)header +
                                                     NETLINK_HEADER);
        name = (string_address)netlink_find(
            header, sizeof(netlink_link), IFLA_IFNAME, address_of length);
        return name && length && memory_first_of(name, 0, length) ? name : null;
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
                if (link->flags & IFF_LOOPBACK)
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
                if (search->found)
                {
                        bool had = (search->flags & IFF_RUNNING) != 0;
                        bool has = (link->flags & IFF_RUNNING) != 0;

                        if (had && !has)
                                return true;
                        if (had == has)
                        {
                                if (search->prefer == NETLINK_PREFER_WIFI)
                                {
                                        if (!(wireless && !search->wireless))
                                                return true;
                                }
                                else if (search->prefer == NETLINK_PREFER_WIRED)
                                {
                                        if (!(!wireless && search->wireless))
                                                return true;
                                }
                                else
                                        return true;
                        }
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
        search->found = search->has_hardware = false;

        bipolar status = netlink_dump(handle, RTM_GETLINK, sizeof(netlink_link),
                                      AF_UNSPEC, netlink_link_seen, search);

        if (status < 0)
                return status;

        return search->found ? 0 : -19;
}

//      IFF_UP raised, and every other flag left exactly as it was found.
static bipolar netlink_link_up(b32 handle, p32 index)
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
        body->flags = IFF_UP;
        body->change = IFF_UP;

        return netlink_transact(handle, address_of request, sequence,
                                null, null);
}

/*
        An address given to a link.

        REPLACE rather than EXCLUSIVE, so running the same command twice is
        the same as running it once. A boot that is retried should not fail
        because the first attempt succeeded.
*/
static bipolar netlink_address_change(b32 handle, p16 type, p16 flags,
                                      p32 index, p32 host, p8 prefix)
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

        return netlink_transact(handle, address_of request, sequence,
                                null, null);
}

#define netlink_address_add(handle, index, host, prefix) netlink_address_change( \
        handle, RTM_NEWADDR, NLM_REQUEST | NLM_ACK | NLM_CREATE | NLM_REPLACE,   \
        index, host, prefix)

/* Automatic configuration must acquire a kernel object before it can later
   claim the right to remove it.  EXCLUSIVE distinguishes a newly installed
   address from an identical address which was already owned by an operator or
   another network manager; REPLACE cannot make that distinction. */
#define netlink_address_acquire(handle, index, host, prefix) netlink_address_change( \
        handle, RTM_NEWADDR, NLM_REQUEST | NLM_ACK | NLM_CREATE | NLM_EXCLUSIVE,     \
        index, host, prefix)

#define netlink_address_delete(handle, index, host, prefix) netlink_address_change( \
        handle, RTM_DELADDR, NLM_REQUEST | NLM_ACK, index, host, prefix)

/*
        A route, with a destination of no bits at all being the default one.

        The gateway has to be reachable already, which for a default route
        means the address added above has to cover it. The kernel says ENETUNREACH
        when it does not, which reads as a network problem and is really an
        ordering one.
*/
static bipolar netlink_route_change(b32 handle, p16 type, p16 flags,
                                    p32 destination, p8 bits, p32 gateway,
                                    p32 index)
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
        destination, bits, gateway, index)

/* An existing route is state, not spare capacity.  Initial DHCP acquisition
   uses EXCLUSIVE so a pre-existing default route is reported as a conflict
   and remains byte-for-byte kernel state owned by whoever installed it. */
#define netlink_route_acquire(handle, destination, bits, gateway, index) netlink_route_change( \
        handle, RTM_NEWROUTE, NLM_REQUEST | NLM_ACK | NLM_CREATE | NLM_EXCLUSIVE,              \
        destination, bits, gateway, index)

#define netlink_route_delete(handle, destination, bits, gateway, index) netlink_route_change( \
        handle, RTM_DELROUTE, NLM_REQUEST | NLM_ACK, destination, bits, gateway, index)

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

        if (used + 1 > room || used + 1 > 255)
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
        same pointer again. The spelling is kept as sent; DNS names compare
        without regard to ASCII case.
*/
static COLD bipolar dns_copy_name(p8 address_to message, positive size,
                             positive at, p8 address_to into, positive room,
                             positive address_to ended)
{
        positive ceiling = size;
        positive used = 0;

        address_to ended = 0;

        for (;;)
        {
                p8 length;

                if (at >= ceiling)
                        return DNS_MALFORMED;

                length = message[at];

                if ((length & 0xc0) == 0xc0)
                {
                        positive target;

                        if (at + 1 >= ceiling)
                                return DNS_MALFORMED;

                        if (!address_to ended)
                                address_to ended = at + 2;

                        target = network_load_16(message + at) & 0x3fff;

                        if (target >= at)
                                return DNS_MALFORMED;

                        ceiling = at;
                        at = target;
                        continue;
                }

                if (length & 0xc0 || length > ceiling - at - 1 ||
                    room - used < (positive)length + 1)
                        return DNS_MALFORMED;

                memory_copy_apart(into + used, message + at, length + 1);
                used += length + 1;
                at += length + 1;

                if (!length)
                {
                        if (!address_to ended)
                                address_to ended = at;
                        return (bipolar)used;
                }
        }
}

//      Where a name ends, for a caller with no use for its spelling.
static COLD bipolar dns_skip_name(p8 address_to message, positive size, positive at)
{
        p8 name[256];
        positive ended;

        return dns_copy_name(message, size, at, name, sizeof name,
                             address_of ended) < 0
                   ? DNS_MALFORMED : (bipolar)ended;
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
        bipolar wanted_length = dns_copy_name(message, size, question_at,
                                              wanted, sizeof wanted,
                                              address_of ended);

        if (wanted_length < 0)
                return DNS_MALFORMED;

        /* A cycle needs no more links than there are answer records to show
           itself.  The extra pass is the one that can find the terminal A. */
        for (positive hop = 0; hop <= (positive)answers; hop++)
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
                            message, size, at, owner, sizeof owner, address_of at);
                        p16 kind;
                        p16 class;
                        p16 data_length;
                        bool is_wanted;

                        if (owner_length < 0 || size - at < 10)
                                return DNS_MALFORMED;

                        kind = network_load_16(message + at);
                        class = network_load_16(message + at + 2);
                        data_length = network_load_16(message + at + 8);
                        at += 10;

                        if (data_length > size - at)
                                return DNS_MALFORMED;

                        is_wanted = owner_length == wanted_length &&
                                    !memory_compare_ascii_case(
                                        owner, wanted, (positive)wanted_length);

                        if (class == DNS_CLASS_IN && is_wanted &&
                            kind == DNS_TYPE_A)
                        {
                                if (data_length != 4)
                                        return DNS_MALFORMED;
                                if (!has_address)
                                        address = network_load_32(message + at);
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
                                    address_of target_end);

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
   TCP has one framed reply and treats an identity mismatch as malformed. */
static COLD bool dns_reply_identity(
    p8 address_to reply, positive size, p16 id,
    p8 address_to request, positive question_length)
{
        return size >= DNS_HEADER && network_load_16(reply) == id &&
               network_load_16(reply + 4) == 1 &&
               size >= DNS_HEADER + question_length &&
               !memory_compare(reply + DNS_HEADER,
                               request + DNS_HEADER, question_length);
}

/* Walk every declared resource record, including sections this IPv4 resolver
   does not otherwise consume. A usable A answer cannot make malformed
   authority/additional framing or unaccounted trailing bytes disappear. */
static COLD bipolar dns_records_end(p8 address_to message, positive size,
                               positive at, positive count)
{
        for (positive record = 0; record < count; record++)
        {
                bipolar ended = dns_skip_name(message, size, at);
                p16 data_length;

                if (ended < 0)
                        return DNS_MALFORMED;
                at = (positive)ended;
                if (at > size || size - at < 10)
                        return DNS_MALFORMED;
                data_length = network_load_16(message + at + 8);
                at += 10;
                if (data_length > size - at)
                        return DNS_MALFORMED;
                at += data_length;
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
                                question_length))
                return DNS_MALFORMED;

        flags = network_load_16(reply + 2);
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

        answers = network_load_16(reply + 6);
        authorities = network_load_16(reply + 8);
        additional = network_load_16(reply + 10);
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
        p8 reply[DNS_MAX_MESSAGE];
        p8 frame[2];
        bipolar handle = dns_stream_connect_until(where, deadline);
        p16 length;
        bipolar result = DNS_NO_REPLY;

        if (handle < 0)
                return DNS_NO_REPLY;

        network_store_16(frame, (p16)request_length);
        if (!network_stream_send_all_until(handle, frame, sizeof frame,
                                           deadline) ||
            !network_stream_send_all_until(handle, request, request_length,
                                           deadline) ||
            !network_stream_read_all(handle, frame, sizeof frame, deadline))
                goto done;

        length = network_load_16(frame);
        if (length > sizeof reply)
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
        server. The wanted-th of them is returned, so a caller walks 0, 1, 2
        until this answers negatively and the number of servers a machine may
        list has no ceiling. Options, search domains and IPv6 servers are read
        past rather than understood.
*/
static COLD bipolar dns_server_at(string_address path, positive wanted)
{
        p8 text[4096];
        bipolar got;
        positive at = 0;

        got = file_slurp(path, text, sizeof text);

        if (got <= 0)
                return DNS_NO_SERVER;

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
                if (host >= 0 && !wanted--)
                        return host;
        }

        return DNS_NO_SERVER;
}

/*
        One question asked, and the first address in the answer.

        The reply is read with a deadline rather than blocked on forever: a
        nameserver that does not answer is the ordinary case on a network that
        is not up yet, and a resolver that hangs there is worse than one that
        gives up and says so. The wait is a poll on the socket rather than a
        receive timeout, which keeps a timeval out of the assembly graph.
*/
static COLD bipolar dns_resolve_at(p32 server, p16 port, string_address name,
                              p32 address_to found, positive seconds)
{
        p8 request[DNS_MAX_MESSAGE];
        p8 reply[DNS_MAX_MESSAGE];
        p16 id;
        bipolar handle;
        bipolar written;
        bipolar got;
        bipolar failure = DNS_NO_REPLY;
        positive question_length;
        network_deadline deadline;

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

        written = dns_write_name(request + DNS_HEADER,
                                 sizeof(request) - DNS_HEADER - 4, name);

        if (written < 0)
                return DNS_MALFORMED;

        memory_fill(request, 0, DNS_HEADER);
        network_store_16(request, id);
        network_store_16(request + 2, DNS_FLAG_RECURSE);
        network_store_16(request + 4, 1);

        network_store_16(request + DNS_HEADER + written, DNS_TYPE_A);
        network_store_16(request + DNS_HEADER + written + 2, DNS_CLASS_IN);

        question_length = (positive)written + 4;

        /* TCP fallback spends only what the original UDP transaction leaves.
           Start the one monotonic budget before any socket operation. */
        if (!network_deadline_begin(address_of deadline, seconds, 0))
                return DNS_NO_REPLY;

        handle = socket_new(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);

        if (handle < 0)
                return DNS_NO_SERVER;

        socket_address_internet where = {
            .family = AF_INET, .port = network_order_16(port),
            .host = network_order_32(server)};

        if (socket_connect((b32)handle, address_of where, sizeof where) < 0)
        {
                failure = DNS_NO_SERVER;
                goto failed;
        }

        if (socket_send((b32)handle, request, DNS_HEADER + question_length,
                        0, 0, 0) < 0)
                goto failed;

        /* A connected UDP socket authenticates the source address, not the
           transaction. An off-path sender can still spoof that address and
           race junk without guessing the random id. Discard replies that do
           not match both id and question, while charging every one to the
           original total deadline. */
        for (;;)
        {
                positive available;

                got = network_wait_readable_until(handle, address_of deadline);

                if (got <= 0)
                        goto failed;

                got = socket_receive((b32)handle, reply, sizeof reply,
                                     MSG_TRUNC, 0, 0);

                if (got == NETWORK_INTERRUPTED)
                        continue;
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
                                        question_length))
                        continue;

                break;
        }

        socket_close((b32)handle);

        failure = dns_reply_result(reply, (positive)got, id, request,
                                   question_length, found);
        if (failure == DNS_TRY_TCP)
                return dns_retry_tcp(address_of where, request,
                                     DNS_HEADER + question_length, id,
                                     question_length, found,
                                     address_of deadline);
        return failure;

failed:
        socket_close((b32)handle);
        return failure;
}

/*
        The servers resolv.conf names, in the order it names them.

        Which servers those are is not decided here -- that is what writes the
        file, and this only reads it. What is decided here is what happens
        when one of them does not answer, and the answer is: ask the next.

        "No such name" does not end the walk either, and that is deliberate
        rather than thorough. The file is written with a public resolver
        first, and a public resolver has never heard of anything inside the
        network it is outside of, so a name that exists only on the local
        network comes back from it as NXDOMAIN. Treating that as final would
        make a machine unable to reach anything on its own network. So it is
        remembered as the answer to fall back on, the rest of the list is
        asked anyway, and only an actual address stops the walk.

        The cost is one extra query for a name that genuinely exists nowhere.

        With no resolv.conf at all there is still somewhere to ask. A machine
        that has not been configured yet should be able to resolve a name, if
        only to fetch the thing that will configure it.
*/
#define DNS_FALLBACK 0x01010101u

static COLD bipolar dns_resolve_any(string_address path, string_address name,
                               p32 address_to found, positive seconds)
{
        bipolar definite = DNS_NO_SERVER;
        bipolar status = DNS_NO_SERVER;
        bipolar server;
        positive index = 0;
        bool asked = false;

        while ((server = dns_server_at(path, index++)) >= 0)
        {
                asked = true;
                status = dns_resolve_at((p32)server, DNS_PORT, name, found,
                                        seconds);

                if (status == DNS_OK)
                        return DNS_OK;

                if (definite == DNS_NO_SERVER &&
                    (status == DNS_NO_SUCH_NAME || status == DNS_NO_ADDRESS))
                        definite = status;
        }

        if (!asked)
                return dns_resolve_at(DNS_FALLBACK, DNS_PORT, name, found,
                                      seconds);

        return definite != DNS_NO_SERVER ? definite : status;
}

#endif // STANDARD_MODERN_C_NET_DNS
/* ---- http: http: a URL, and the bytes behind it ---- */

#ifndef STANDARD_MODERN_C_NET_HTTP
#define STANDARD_MODERN_C_NET_HTTP

/*
        TLS 1.3 client for wget.

        One cipher: TLS_AES_128_GCM_SHA256. Groups: X25519, P-256 and
        P-384, each with a ClientHello key share so Chimera's secp384r1
        servers do not HelloRetryRequest. Certificates walk to one of the
        Mozilla TLS roots in anchors.inc: a served certificate carrying an
        anchor's key ends the chain, or the last one served names an anchor
        as its issuer and verifies under it. Chain signatures may be ECDSA
        with SHA-256 or SHA-384 on P-256 or P-384, or RSA PKCS#1 v1.5 with
        SHA-256 or SHA-384.
        Signature algorithms advertised are ecdsa_secp256r1_sha256,
        ecdsa_secp384r1_sha384 and rsa_pss_rsae_sha256. close_notify is a
        clean end of the body, not a handshake failure.
        --no-check-certificate skips the chain and still encrypts.
*/

#ifndef STANDARD_MODERN_C_NET_TLS
#define STANDARD_MODERN_C_NET_TLS

/*
        Hashes, AES-GCM, X25519 and the signature checks HTTPS needs.

        wget speaks TLS 1.3 with AES-128-GCM, X25519, P-256 and P-384. The chain for that
        handshake is ECDSA on P-256 and P-384, RSA PKCS#1 and RSA-PSS SHA-256;
        Alpine and GitHub still present RSA leaves. SHA-256 compression is
        sha256_compress in lib.c, on the same hardware floor as the rest
        of the binary, and SHA-384 is sha512_blocks through the same streaming
        digests the checksum utilities use. X25519 and ECDSA stay C for now. None of
        this is a kernel crypto ABI: AF_ALG is off on Moonwater, and a
        downloader cannot wait on it.
*/

#ifndef STANDARD_MODERN_C_NET_CRYPTO
#define STANDARD_MODERN_C_NET_CRYPTO

typedef unsigned __int128 crypto_wide;

/* Big endian 64 bit fields -- GCM's length block, a key wrap's integrity
   check, a bignum limb -- are an unaligned pair of 32 bit halves, and the
   halves are lib.c's own byte-reversing load and store. */
static p64 crypto_be64(const p8 address_to bytes)
{
        return ((p64)network_load_32((p8 address_to)bytes) << 32) |
               network_load_32((p8 address_to)bytes + 4);
}

static fn crypto_put_be64(p8 address_to bytes, p64 value)
{
        network_store_32(bytes, (p32)(value >> 32));
        network_store_32(bytes + 4, (p32)value);
}

/*
        SHA-256 for the transcript, HKDF and the signature hashes, SHA-384 for
        the P-384 and RSA-SHA384 chains: the library's streaming digests over
        sha256_blocks and sha512_blocks, under the names this file has always
        used. A transcript copy is a digest_state copy.
*/
typedef digest_state crypto_sha256;
typedef digest_state crypto_sha512;

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

static fn crypto_sha384_open(crypto_sha512 address_to hash)
{
        digest_open(hash, DIGEST_SHA384, 48);
}

static fn crypto_sha512_write(crypto_sha512 address_to hash, p8 address_to data,
                              positive length)
{
        digest_write(hash, data, length);
}

static fn crypto_sha384_close(crypto_sha512 address_to hash, p8 address_to out)
{
        digest_close(hash, out);
}

static fn crypto_sha384(p8 address_to data, positive length, p8 address_to out)
{
        crypto_sha512 hash;

        crypto_sha384_open(address_of hash);
        crypto_sha512_write(address_of hash, data, length);
        crypto_sha384_close(address_of hash, out);
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

static fn crypto_hmac_sha256(p8 address_to key, positive key_length,
                             p8 address_to data, positive length,
                             p8 address_to out)
{
        crypto_mac mac;

        crypto_hmac_open(address_of mac, DIGEST_SHA256, 32, key, key_length);
        crypto_hmac_write(address_of mac, data, length);
        crypto_hmac_close(address_of mac, out);
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
        crypto_mac key;
        digest_state inner;
        digest_state outer;
        digest_state work;
        p8 pad[64];
        p8 block[64];
        p8 mix[64];
        p8 number[4];

        crypto_hmac_open(address_of key, algorithm, size, password,
                         password_length);
        inner = key.hash;
        for (positive at = 0; at < 64; at++)
                pad[at] = key.key_block[at] ^ 0x5c;
        digest_open(address_of outer, algorithm, size);
        digest_write(address_of outer, pad, 64);

        for (positive index = 1, done = 0; done < out_length; index++)
        {
                positive take = out_length - done < size ? out_length - done
                                                         : size;

                network_store_32(number, (p32)index);
                work = inner;
                digest_write(address_of work, salt, salt_length);
                digest_write(address_of work, number, 4);
                digest_close(address_of work, block);
                work = outer;
                digest_write(address_of work, block, size);
                digest_close(address_of work, block);
                memory_copy(mix, block, size);

                for (positive round = 1; round < rounds; round++)
                {
                        work = inner;
                        digest_write(address_of work, block, size);
                        digest_close(address_of work, block);
                        work = outer;
                        digest_write(address_of work, block, size);
                        digest_close(address_of work, block);
                        for (positive at = 0; at < size; at++)
                                mix[at] ^= block[at];
                }
                memory_copy(out + done, mix, take);
                done += take;
        }

        crypto_forget(address_of key, sizeof key);
        crypto_forget(address_of inner, sizeof inner);
        crypto_forget(address_of outer, sizeof outer);
        crypto_forget(address_of work, sizeof work);
        crypto_forget(pad, sizeof pad);
        crypto_forget(block, sizeof block);
        crypto_forget(mix, sizeof mix);
}

static fn crypto_hkdf_extract(p8 address_to salt, positive salt_length,
                              p8 address_to ikm, positive ikm_length,
                              p8 address_to prk)
{
        static p8 zeros[32];

        if (!salt || !salt_length)
        {
                salt = zeros;
                salt_length = 32;
        }

        crypto_hmac_sha256(salt, salt_length, ikm, ikm_length, prk);
}

static fn crypto_hkdf_expand(p8 address_to prk, p8 address_to info,
                             positive info_length, p8 address_to out,
                             positive out_length)
{
        p8 previous[32];
        p8 block[32];
        positive have = 0;
        p8 counter = 1;

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

static p8 crypto_aes_substitute(p8 value)
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
        p8 inverse = crypto_aes_field_multiply(
            crypto_aes_field_multiply(x240, x12), x2);

        return (p8)(inverse ^
                    ((inverse << 1) | (inverse >> 7)) ^
                    ((inverse << 2) | (inverse >> 6)) ^
                    ((inverse << 3) | (inverse >> 5)) ^
                    ((inverse << 4) | (inverse >> 4)) ^ 0x63);
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
        crypto_put_be64(padded, (p64)aad_length * 8);
        crypto_put_be64(padded + 8, (p64)text_length * 8);
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
        positive i;
        p8 diff = 0;
        bool valid;

        crypto_aesgcm_crypt(key, iv, aad, aad_length, text, text_length, got,
                            false);
        for (i = 0; i < 16; i++)
                diff |= got[i] ^ tag[i];
        valid = diff == 0;
        if (!valid)
                crypto_forget(text, text_length);
        crypto_forget(got, sizeof got);
        crypto_forget(address_of diff, sizeof diff);
        return valid;
}

/*
        RFC 7748 X25519 on 5 x 51-bit limbs. Field reduce, freeze and the
        Montgomery step follow the public-domain 64-bit curve25519-donna.
*/
typedef p64 crypto_x25519_fe[5];

static p64 crypto_x25519_load64(p8 address_to in)
{
        return (p64)in[0] | ((p64)in[1] << 8) | ((p64)in[2] << 16) |
               ((p64)in[3] << 24) | ((p64)in[4] << 32) | ((p64)in[5] << 40) |
               ((p64)in[6] << 48) | ((p64)in[7] << 56);
}

static fn crypto_x25519_store64(p8 address_to out, p64 in)
{
        out[0] = (p8)in;
        out[1] = (p8)(in >> 8);
        out[2] = (p8)(in >> 16);
        out[3] = (p8)(in >> 24);
        out[4] = (p8)(in >> 32);
        out[5] = (p8)(in >> 40);
        out[6] = (p8)(in >> 48);
        out[7] = (p8)(in >> 56);
}

static fn crypto_x25519_copy(crypto_x25519_fe o, crypto_x25519_fe a)
{
        o[0] = a[0];
        o[1] = a[1];
        o[2] = a[2];
        o[3] = a[3];
        o[4] = a[4];
}

static fn crypto_x25519_load(crypto_x25519_fe out, p8 address_to in)
{
        out[0] = crypto_x25519_load64(in) & 0x7ffffffffffffull;
        out[1] = (crypto_x25519_load64(in + 6) >> 3) & 0x7ffffffffffffull;
        out[2] = (crypto_x25519_load64(in + 12) >> 6) & 0x7ffffffffffffull;
        out[3] = (crypto_x25519_load64(in + 19) >> 1) & 0x7ffffffffffffull;
        out[4] = (crypto_x25519_load64(in + 24) >> 12) & 0x7ffffffffffffull;
}

/*
        One pass of the 51 bit carry chain.

        Each limb hands what will not fit to the one above it. wrap folds the
        carry leaving the top limb back into the bottom one at the weight the
        prime gives it -- 2^255 is 19 -- and is how the chain stays inside
        the field. The pass that finishes a store is the one exception: there
        the carry out of the top is the answer to the conditional
        subtraction, and dropping it is what performs the subtraction.
*/
static fn crypto_x25519_carry(crypto_wide address_to t, bool wrap)
{
        for (positive i = 0; i < 4; i++)
        {
                t[i + 1] += t[i] >> 51;
                t[i] &= 0x7ffffffffffffull;
        }

        if (wrap)
                t[0] += 19 * (t[4] >> 51);
        t[4] &= 0x7ffffffffffffull;
}

static fn crypto_x25519_store(p8 address_to out, crypto_x25519_fe in)
{
        crypto_wide t[5];

        for (positive i = 0; i < 5; i++)
                t[i] = in[i];

        crypto_x25519_carry(t, true);
        crypto_x25519_carry(t, true);

        //      Adding 19 and carrying turns a value in [p, 2p) into one in
        //      [0, p) with the top limb's bit set, which the constants below
        //      then clear; a value already below p is unchanged by the pair.
        t[0] += 19;
        crypto_x25519_carry(t, true);

        t[0] += 0x8000000000000ull - 19;
        for (positive i = 1; i < 5; i++)
                t[i] += 0x8000000000000ull - 1;
        crypto_x25519_carry(t, false);

        crypto_x25519_store64(out, (p64)(t[0] | (t[1] << 51)));
        crypto_x25519_store64(out + 8, (p64)((t[1] >> 13) | (t[2] << 38)));
        crypto_x25519_store64(out + 16, (p64)((t[2] >> 26) | (t[3] << 25)));
        crypto_x25519_store64(out + 24, (p64)((t[3] >> 39) | (t[4] << 12)));
        crypto_forget(t, sizeof t);
}

static fn crypto_x25519_sum(crypto_x25519_fe o, crypto_x25519_fe in)
{
        o[0] += in[0];
        o[1] += in[1];
        o[2] += in[2];
        o[3] += in[3];
        o[4] += in[4];
}

static fn crypto_x25519_diff(crypto_x25519_fe o, crypto_x25519_fe in)
{
        o[0] = in[0] + 0x3fffffffffff68ull - o[0];
        o[1] = in[1] + 0x3ffffffffffff8ull - o[1];
        o[2] = in[2] + 0x3ffffffffffff8ull - o[2];
        o[3] = in[3] + 0x3ffffffffffff8ull - o[3];
        o[4] = in[4] + 0x3ffffffffffff8ull - o[4];
}

static fn crypto_x25519_mul(crypto_x25519_fe o, crypto_x25519_fe in2,
                            crypto_x25519_fe in)
{
        crypto_wide t[5];
        p64 r0, r1, r2, r3, r4, s0, s1, s2, s3, s4, c;

        r0 = in[0];
        r1 = in[1];
        r2 = in[2];
        r3 = in[3];
        r4 = in[4];
        s0 = in2[0];
        s1 = in2[1];
        s2 = in2[2];
        s3 = in2[3];
        s4 = in2[4];

        t[0] = (crypto_wide)r0 * s0;
        t[1] = (crypto_wide)r0 * s1 + (crypto_wide)r1 * s0;
        t[2] = (crypto_wide)r0 * s2 + (crypto_wide)r2 * s0 + (crypto_wide)r1 * s1;
        t[3] = (crypto_wide)r0 * s3 + (crypto_wide)r3 * s0 + (crypto_wide)r1 * s2 +
               (crypto_wide)r2 * s1;
        t[4] = (crypto_wide)r0 * s4 + (crypto_wide)r4 * s0 + (crypto_wide)r3 * s1 +
               (crypto_wide)r1 * s3 + (crypto_wide)r2 * s2;

        t[0] += ((crypto_wide)r4 * 19) * s1 + ((crypto_wide)r1 * 19) * s4 +
                ((crypto_wide)r2 * 19) * s3 + ((crypto_wide)r3 * 19) * s2;
        t[1] += ((crypto_wide)r4 * 19) * s2 + ((crypto_wide)r2 * 19) * s4 +
                ((crypto_wide)r3 * 19) * s3;
        t[2] += ((crypto_wide)r4 * 19) * s3 + ((crypto_wide)r3 * 19) * s4;
        t[3] += ((crypto_wide)r4 * 19) * s4;

        r0 = (p64)t[0] & 0x7ffffffffffffull;
        c = (p64)(t[0] >> 51);
        t[1] += c;
        r1 = (p64)t[1] & 0x7ffffffffffffull;
        c = (p64)(t[1] >> 51);
        t[2] += c;
        r2 = (p64)t[2] & 0x7ffffffffffffull;
        c = (p64)(t[2] >> 51);
        t[3] += c;
        r3 = (p64)t[3] & 0x7ffffffffffffull;
        c = (p64)(t[3] >> 51);
        t[4] += c;
        r4 = (p64)t[4] & 0x7ffffffffffffull;
        c = (p64)(t[4] >> 51);
        r0 += c * 19;
        c = r0 >> 51;
        r0 &= 0x7ffffffffffffull;
        r1 += c;
        c = r1 >> 51;
        r1 &= 0x7ffffffffffffull;
        r2 += c;

        o[0] = r0;
        o[1] = r1;
        o[2] = r2;
        o[3] = r3;
        o[4] = r4;
        crypto_forget(t, sizeof t);
}

static fn crypto_x25519_sqr_n(crypto_x25519_fe o, crypto_x25519_fe a, positive n)
{
        crypto_x25519_fe t;

        crypto_x25519_mul(t, a, a);
        n--;
        while (n)
        {
                crypto_x25519_mul(t, t, t);
                n--;
        }
        crypto_x25519_copy(o, t);
        crypto_forget(t, sizeof t);
}

static fn crypto_x25519_mul121665(crypto_x25519_fe o, crypto_x25519_fe a)
{
        crypto_wide w;

        w = (crypto_wide)a[0] * 121665;
        o[0] = (p64)w & 0x7ffffffffffffull;
        w = (w >> 51) + (crypto_wide)a[1] * 121665;
        o[1] = (p64)w & 0x7ffffffffffffull;
        w = (w >> 51) + (crypto_wide)a[2] * 121665;
        o[2] = (p64)w & 0x7ffffffffffffull;
        w = (w >> 51) + (crypto_wide)a[3] * 121665;
        o[3] = (p64)w & 0x7ffffffffffffull;
        w = (w >> 51) + (crypto_wide)a[4] * 121665;
        o[4] = (p64)w & 0x7ffffffffffffull;
        o[0] += 19 * (p64)(w >> 51);
        crypto_forget(address_of w, sizeof w);
}

static fn crypto_x25519_invert(crypto_x25519_fe o, crypto_x25519_fe z)
{
        crypto_x25519_fe a, t0, b, c;

        crypto_x25519_sqr_n(a, z, 1);
        crypto_x25519_sqr_n(t0, a, 2);
        crypto_x25519_mul(b, t0, z);
        crypto_x25519_mul(a, b, a);
        crypto_x25519_sqr_n(t0, a, 1);
        crypto_x25519_mul(b, t0, b);
        crypto_x25519_sqr_n(t0, b, 5);
        crypto_x25519_mul(b, t0, b);
        crypto_x25519_sqr_n(t0, b, 10);
        crypto_x25519_mul(c, t0, b);
        crypto_x25519_sqr_n(t0, c, 20);
        crypto_x25519_mul(t0, t0, c);
        crypto_x25519_sqr_n(t0, t0, 10);
        crypto_x25519_mul(b, t0, b);
        crypto_x25519_sqr_n(t0, b, 50);
        crypto_x25519_mul(c, t0, b);
        crypto_x25519_sqr_n(t0, c, 100);
        crypto_x25519_mul(t0, t0, c);
        crypto_x25519_sqr_n(t0, t0, 50);
        crypto_x25519_mul(t0, t0, b);
        crypto_x25519_sqr_n(t0, t0, 5);
        crypto_x25519_mul(o, t0, a);

        crypto_forget(a, sizeof a);
        crypto_forget(t0, sizeof t0);
        crypto_forget(b, sizeof b);
        crypto_forget(c, sizeof c);
}

static fn crypto_cswap(crypto_x25519_fe a, crypto_x25519_fe b, p64 swap)
{
        positive i;

        swap = 0 - swap;
        for (i = 0; i < 5; i++)
        {
                p64 t = swap & (a[i] ^ b[i]);
                a[i] ^= t;
                b[i] ^= t;
        }
}

static bool crypto_x25519(p8 address_to out, p8 address_to scalar, p8 address_to u)
{
        p8 e[32];
        crypto_x25519_fe x1, x2, z2, x3, z3;
        crypto_x25519_fe a, b, c, d, aa, bb, ee, da, cb, t;
        positive i;
        p64 bit;
        p64 swap = 0;
        bool valid;

        memory_copy(e, scalar, 32);
        e[0] &= 248;
        e[31] &= 127;
        e[31] |= 64;

        crypto_x25519_load(x1, u);
        memory_fill(x2, 0, sizeof(x2));
        x2[0] = 1;
        memory_fill(z2, 0, sizeof(z2));
        crypto_x25519_copy(x3, x1);
        memory_fill(z3, 0, sizeof(z3));
        z3[0] = 1;

        for (i = 254; i < 256; i--)
        {
                bit = (e[i >> 3] >> (i & 7)) & 1;
                swap ^= bit;
                crypto_cswap(x2, x3, swap);
                crypto_cswap(z2, z3, swap);
                swap = bit;

                crypto_x25519_copy(a, x2);
                crypto_x25519_sum(a, z2);
                crypto_x25519_copy(b, z2);
                crypto_x25519_diff(b, x2);
                crypto_x25519_copy(c, x3);
                crypto_x25519_sum(c, z3);
                crypto_x25519_copy(d, z3);
                crypto_x25519_diff(d, x3);

                crypto_x25519_mul(da, d, a);
                crypto_x25519_mul(cb, c, b);
                crypto_x25519_mul(aa, a, a);
                crypto_x25519_mul(bb, b, b);

                crypto_x25519_copy(t, da);
                crypto_x25519_sum(t, cb);
                crypto_x25519_mul(x3, t, t);

                crypto_x25519_copy(t, cb);
                crypto_x25519_diff(t, da);
                crypto_x25519_mul(t, t, t);
                crypto_x25519_mul(z3, x1, t);

                crypto_x25519_mul(x2, aa, bb);

                crypto_x25519_copy(ee, bb);
                crypto_x25519_diff(ee, aa);
                crypto_x25519_mul121665(t, ee);
                crypto_x25519_sum(t, aa);
                crypto_x25519_mul(z2, ee, t);
        }

        crypto_cswap(x2, x3, swap);
        crypto_cswap(z2, z3, swap);
        crypto_x25519_invert(z2, z2);
        crypto_x25519_mul(x2, x2, z2);
        crypto_x25519_store(out, x2);

        /* RFC 7748's low-order inputs produce the all-zero shared secret.
           Returning its validity lets a protocol reject that public result
           without adding a second, easy-to-forget check at every caller. */
        {
                p8 nonzero = 0;

                for (i = 0; i < 32; i++)
                        nonzero |= out[i];
                valid = nonzero != 0;
                crypto_forget(address_of nonzero, sizeof nonzero);
        }

        crypto_forget(e, sizeof e);
        crypto_forget(x1, sizeof x1);
        crypto_forget(x2, sizeof x2);
        crypto_forget(z2, sizeof z2);
        crypto_forget(x3, sizeof x3);
        crypto_forget(z3, sizeof z3);
        crypto_forget(a, sizeof a);
        crypto_forget(b, sizeof b);
        crypto_forget(c, sizeof c);
        crypto_forget(d, sizeof d);
        crypto_forget(aa, sizeof aa);
        crypto_forget(bb, sizeof bb);
        crypto_forget(ee, sizeof ee);
        crypto_forget(da, sizeof da);
        crypto_forget(cb, sizeof cb);
        crypto_forget(t, sizeof t);
        crypto_forget(address_of bit, sizeof bit);
        crypto_forget(address_of swap, sizeof swap);
        return valid;
}

#define CRYPTO_FE_MAX 6
#define CRYPTO_RSA_LIMBS 64

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
                out[n - 1 - i] = crypto_be64(bytes + i * 8);
}

static fn crypto_fe_store_be(p8 address_to bytes, const p64 address_to in, positive n)
{
        positive i;

        for (i = 0; i < n; i++)
                crypto_put_be64(bytes + i * 8, in[n - 1 - i]);
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

static p64 crypto_fe_zero_bit(const p64 address_to a, positive n)
{
        p64 combined = 0;

        for (positive i = 0; i < n; i++)
                combined |= a[i];

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

/* dbl-2001-b for a = -3, three multiplies and five squarings:
       delta = Z^2, gamma = Y^2, beta = X gamma,
       alpha = 3 (X - delta)(X + delta),
       X3 = alpha^2 - 8 beta,  Z3 = (Y + Z)^2 - gamma - delta,
       Y3 = alpha (4 beta - X3) - 8 gamma^2.
   Z = 0 gives Z3 = 0, so infinity doubles to itself with no branch.  The
   same operations run for every input, and r may be p. */
static fn crypto_point_double_formula(crypto_point address_to r,
                                      const crypto_point address_to p)
{
        const crypto_field address_to f = p->field;
        p64 delta[CRYPTO_FE_MAX], gamma[CRYPTO_FE_MAX], beta[CRYPTO_FE_MAX];
        p64 alpha[CRYPTO_FE_MAX], tmp[CRYPTO_FE_MAX], tmp2[CRYPTO_FE_MAX];
        p64 x3[CRYPTO_FE_MAX], y3[CRYPTO_FE_MAX], z3[CRYPTO_FE_MAX];

        crypto_fe_sqr(delta, p->z, f);
        crypto_fe_sqr(gamma, p->y, f);
        crypto_fe_mul(beta, p->x, gamma, f);

        crypto_fe_sub(tmp, p->x, delta, f);
        crypto_fe_add(tmp2, p->x, delta, f);
        crypto_fe_mul(alpha, tmp, tmp2, f);
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

        memory_copy(r->x, x3, sizeof x3);
        memory_copy(r->y, y3, sizeof y3);
        memory_copy(r->z, z3, sizeof z3);
        r->n = p->n;
        r->field = f;

        crypto_forget(delta, sizeof delta);
        crypto_forget(gamma, sizeof gamma);
        crypto_forget(beta, sizeof beta);
        crypto_forget(alpha, sizeof alpha);
        crypto_forget(tmp, sizeof tmp);
        crypto_forget(tmp2, sizeof tmp2);
        crypto_forget(x3, sizeof x3);
        crypto_forget(y3, sizeof y3);
        crypto_forget(z3, sizeof z3);
}

/* Public points only: infinity returns at once. */
static fn crypto_point_double(crypto_point address_to r, crypto_point address_to p)
{
        if (crypto_fe_is_zero(p->z, p->n))
        {
                *r = *p;
                return;
        }
        crypto_point_double_formula(r, p);
}

static fn crypto_point_add(crypto_point address_to r, crypto_point address_to p,
                           crypto_point address_to q)
{
        p64 z1z1[CRYPTO_FE_MAX], z2z2[CRYPTO_FE_MAX];
        p64 u1[CRYPTO_FE_MAX], u2[CRYPTO_FE_MAX], s1[CRYPTO_FE_MAX], s2[CRYPTO_FE_MAX];
        p64 h[CRYPTO_FE_MAX], rr[CRYPTO_FE_MAX], hh[CRYPTO_FE_MAX], hhh[CRYPTO_FE_MAX];
        p64 v[CRYPTO_FE_MAX], tmp[CRYPTO_FE_MAX], tmp2[CRYPTO_FE_MAX];
        positive n = p->n;
        const crypto_field address_to f = p->field;

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

        crypto_fe_sqr(z1z1, p->z, f);
        crypto_fe_sqr(z2z2, q->z, f);
        crypto_fe_mul(u1, p->x, z2z2, f);
        crypto_fe_mul(u2, q->x, z1z1, f);
        crypto_fe_mul(tmp, q->z, z2z2, f);
        crypto_fe_mul(s1, p->y, tmp, f);
        crypto_fe_mul(tmp, p->z, z1z1, f);
        crypto_fe_mul(s2, q->y, tmp, f);

        crypto_fe_sub(h, u2, u1, f);
        crypto_fe_sub(rr, s2, s1, f);

        if (crypto_fe_is_zero(h, n))
        {
                if (crypto_fe_is_zero(rr, n))
                {
                        crypto_point_double(r, p);
                        return;
                }
                crypto_point_zero(r, f);
                return;
        }

        crypto_fe_sqr(hh, h, f);
        crypto_fe_mul(hhh, h, hh, f);
        crypto_fe_mul(v, u1, hh, f);

        crypto_fe_sqr(tmp, rr, f);
        crypto_fe_sub(tmp, tmp, hhh, f);
        crypto_fe_add(tmp2, v, v, f);
        crypto_fe_sub(r->x, tmp, tmp2, f);

        crypto_fe_sub(tmp, v, r->x, f);
        crypto_fe_mul(tmp2, rr, tmp, f);
        crypto_fe_mul(tmp, s1, hhh, f);
        crypto_fe_sub(r->y, tmp2, tmp, f);

        crypto_fe_mul(tmp, p->z, q->z, f);
        crypto_fe_mul(r->z, tmp, h, f);
        r->n = n;
        r->field = f;
}

/* The ECDH multiplier cannot use the public-signature helpers above: their
   exceptional-point branches and digit-conditional addition reveal a private
   scalar to a branch or cache observer.  These helpers select infinity cases
   with masks, and crypto_point_scalar_private says why no other exception
   reaches them.  Field reduction is likewise branchless, so every scalar
   follows the same operations and addresses. */
static fn crypto_point_select(crypto_point address_to d,
                              const crypto_point address_to a,
                              const crypto_point address_to b, p64 choose_b)
{
        p64 mask = 0 - choose_b;

        for (positive i = 0; i < CRYPTO_FE_MAX; i++)
        {
                d->x[i] = (a->x[i] & ~mask) | (b->x[i] & mask);
                d->y[i] = (a->y[i] & ~mask) | (b->y[i] & mask);
                d->z[i] = (a->z[i] & ~mask) | (b->z[i] & mask);
        }
        d->n = a->n;
        d->field = a->field;
        crypto_forget(address_of mask, sizeof mask);
}

static fn crypto_point_double_private(crypto_point address_to r,
                                      const crypto_point address_to p)
{
        crypto_point_double_formula(r, p);
}

static fn crypto_point_add_private(crypto_point address_to r,
                                   const crypto_point address_to p,
                                   const crypto_point address_to q)
{
        p64 z1z1[CRYPTO_FE_MAX], z2z2[CRYPTO_FE_MAX];
        p64 u1[CRYPTO_FE_MAX], u2[CRYPTO_FE_MAX];
        p64 s1[CRYPTO_FE_MAX], s2[CRYPTO_FE_MAX];
        p64 h[CRYPTO_FE_MAX], rr[CRYPTO_FE_MAX], hh[CRYPTO_FE_MAX];
        p64 hhh[CRYPTO_FE_MAX], v[CRYPTO_FE_MAX];
        p64 tmp[CRYPTO_FE_MAX], tmp2[CRYPTO_FE_MAX];
        positive n = p->n;
        const crypto_field address_to f = p->field;
        crypto_point sum;
        p64 p_infinity;
        p64 q_infinity;

        crypto_point_zero(address_of sum, f);
        crypto_fe_sqr(z1z1, p->z, f);
        crypto_fe_sqr(z2z2, q->z, f);
        crypto_fe_mul(u1, p->x, z2z2, f);
        crypto_fe_mul(u2, q->x, z1z1, f);
        crypto_fe_mul(tmp, q->z, z2z2, f);
        crypto_fe_mul(s1, p->y, tmp, f);
        crypto_fe_mul(tmp, p->z, z1z1, f);
        crypto_fe_mul(s2, q->y, tmp, f);

        crypto_fe_sub(h, u2, u1, f);
        crypto_fe_sub(rr, s2, s1, f);
        crypto_fe_sqr(hh, h, f);
        crypto_fe_mul(hhh, h, hh, f);
        crypto_fe_mul(v, u1, hh, f);

        crypto_fe_sqr(tmp, rr, f);
        crypto_fe_sub(tmp, tmp, hhh, f);
        crypto_fe_add(tmp2, v, v, f);
        crypto_fe_sub(sum.x, tmp, tmp2, f);

        crypto_fe_sub(tmp, v, sum.x, f);
        crypto_fe_mul(tmp2, rr, tmp, f);
        crypto_fe_mul(tmp, s1, hhh, f);
        crypto_fe_sub(sum.y, tmp2, tmp, f);

        crypto_fe_mul(tmp, p->z, q->z, f);
        crypto_fe_mul(sum.z, tmp, h, f);

        /* The window multiplier never adds equal points (see
           crypto_point_scalar_private), and opposite points already produce
           z=0.  Only the infinity cases need masked selection.  r may be
           p: nothing is written through it before the last line. */
        p_infinity = crypto_fe_zero_bit(p->z, n);
        q_infinity = crypto_fe_zero_bit(q->z, n);
        crypto_point_select(address_of sum, address_of sum, p, q_infinity);
        crypto_point_select(address_of sum, address_of sum, q, p_infinity);
        *r = sum;

        crypto_forget(z1z1, sizeof z1z1);
        crypto_forget(z2z2, sizeof z2z2);
        crypto_forget(u1, sizeof u1);
        crypto_forget(u2, sizeof u2);
        crypto_forget(s1, sizeof s1);
        crypto_forget(s2, sizeof s2);
        crypto_forget(h, sizeof h);
        crypto_forget(rr, sizeof rr);
        crypto_forget(hh, sizeof hh);
        crypto_forget(hhh, sizeof hhh);
        crypto_forget(v, sizeof v);
        crypto_forget(tmp, sizeof tmp);
        crypto_forget(tmp2, sizeof tmp2);
        crypto_forget(address_of sum, sizeof sum);
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
                                            ((j ^ digit) - 1) >> 63);
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

/* Width-5 non-adjacent form of a public scalar below 2^(64 limbs): each
   digit is zero or odd in [-15, 15], a nonzero digit is followed by at least
   four zeros, and the digits weighted by 2^i sum to k.  Returns how many
   digits were written, at most 64 limbs + 1. */
static positive crypto_wnaf(b8 address_to digits, const p64 address_to k,
                            positive limbs)
{
        p64 v[CRYPTO_FE_MAX + 1];
        positive count = 0;

        memory_copy(v, k, limbs * 8);
        v[limbs] = 0;
        while (!crypto_fe_is_zero(v, limbs + 1))
        {
                b8 digit = 0;

                if (v[0] & 1)
                {
                        p64 low = v[0] & 31;

                        if (low > 15)
                        {
                                p64 carry = 32 - low;

                                digit = (b8)((bipolar)low - 32);
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
                                 const crypto_point address_to table, b8 digit)
{
        crypto_point entry;
        crypto_point sum;

        if (digit > 0)
                entry = table[digit >> 1];
        else
        {
                p64 zero[CRYPTO_FE_MAX];

                memory_fill(zero, 0, sizeof zero);
                entry = table[(-digit) >> 1];
                crypto_fe_sub(entry.y, zero, entry.y, entry.field);
        }
        crypto_point_add(address_of sum, r, address_of entry);
        *r = sum;
}

/* u1 G + u2 Q for public scalars and points (below 2^(64 limbs)), in one
   chain of doublings: each scalar's width-5 digits add a precomputed odd
   multiple of its own point or its negation.  About bits doublings and
   bits/3 additions against bits doublings and bits additions for two
   separate binary multiplies.  Everything may branch; nothing is secret. */
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
        crypto_point doubled;
        positive n1 = crypto_wnaf(d1, u1, g->n);
        positive n2 = crypto_wnaf(d2, u2, g->n);
        positive at = n1 > n2 ? n1 : n2;

        crypto_point_zero(r, g->field);
        if (n1)
                crypto_point_odd_multiples(tg, g);
        if (n2)
                crypto_point_odd_multiples(tq, q);
        while (at)
        {
                at--;
                crypto_point_double(address_of doubled, r);
                *r = doubled;
                if (at < n1 && d1[at])
                        crypto_point_add_digit(r, tg, d1[at]);
                if (at < n2 && d2[at])
                        crypto_point_add_digit(r, tq, d2[at]);
        }
}

static fn crypto_point_affine(crypto_point address_to p)
{
        const crypto_field address_to f = p->field;
        p64 zinv[CRYPTO_FE_MAX], z2[CRYPTO_FE_MAX], z3[CRYPTO_FE_MAX];
        p64 unit[CRYPTO_FE_MAX];

        if (crypto_fe_is_zero(p->z, p->n))
                goto done;

        crypto_fe_inv(zinv, p->z, f);
        crypto_fe_sqr(z2, zinv, f);
        crypto_fe_mul(z3, z2, zinv, f);
        crypto_fe_mul(p->x, p->x, z2, f);
        crypto_fe_mul(p->y, p->y, z3, f);
        memory_fill(unit, 0, sizeof unit);
        unit[0] = 1;
        crypto_fe_mul(p->x, p->x, unit, f);
        crypto_fe_mul(p->y, p->y, unit, f);
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
        valid = !crypto_fe_is_zero(out, limbs) && less;
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
        crypto_fe_mul(w, s, order->square, order);
        crypto_fe_inv(w, w, order);
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
        crypto_point_affine(address_of rpoint);
        while (crypto_fe_cmp(rpoint.x, order->m, limbs) >= 0)
                crypto_fe_subtract_raw(rpoint.x, rpoint.x, order->m, limbs);

        return crypto_fe_cmp(rpoint.x, r, limbs) == 0;
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
        p64 unit[CRYPTO_FE_MAX];
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
                //      all ones for the one the column names.
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
                                chosen.z[i] = (chosen.z[i] & ~mask) |
                                              (f->one[i] & mask);
                        }
                }
                crypto_projective_add(address_of r, address_of r,
                                      address_of chosen, b, f);
        }

        ok = !crypto_fe_is_zero(r.z, n);
        if (ok)
        {
                crypto_fe_inv(zinv, r.z, f);
                crypto_fe_mul(x, r.x, zinv, f);
                crypto_fe_mul(y, r.y, zinv, f);
                memory_fill(unit, 0, sizeof unit);
                unit[0] = 1;
                crypto_fe_mul(x, x, unit, f);
                crypto_fe_mul(y, y, unit, f);
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
        p64 unit[CRYPTO_RSA_LIMBS];
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
                montgomery_multiply(square, square, square, mod, inverse, n);

        montgomery_multiply(b, base, square, mod, inverse, n);
        top = 63;
        while (!((exp >> top) & 1))
                top--;
        memory_copy(result, b, n * 8);
        while (top)
        {
                top--;
                montgomery_multiply(result, result, result, mod, inverse, n);
                if ((exp >> top) & 1)
                        montgomery_multiply(result, result, b, mod, inverse, n);
        }

        memory_fill(unit, 0, n * 8);
        unit[0] = 1;
        montgomery_multiply(out, result, unit, mod, inverse, n);
}

/* Decode the public operation once for both RSA signature encodings.  The
   signature representative is an integer in [0,n), never an arbitrary byte
   string reduced modulo n, and a usable RSA public exponent is odd and at
   least three. */
static bool crypto_rsa_prepare(p8 address_to n_bytes, positive n_length,
                               p64 exponent, p8 address_to sig,
                               positive sig_length, p64 address_to mod,
                               p64 address_to base,
                               positive address_to limbs)
{
        p8 padded[512];

        if (n_length > sizeof(padded) || n_length < 256 ||
            sig_length != n_length || !n_bytes[0] ||
            (n_length == 256 && !(n_bytes[0] & 0x80)) ||
            !(n_bytes[n_length - 1] & 1) || exponent < 3 ||
            !(exponent & 1))
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

/* EMSA-PKCS1-v1_5: 00 01 FF..FF 00 DigestInfo hash, at least eight FF
   bytes, the DigestInfo naming the hash exactly. */
static bool crypto_rsa_pkcs1(p8 address_to n_bytes, positive n_length,
                             p64 exponent, p8 address_to sig,
                             positive sig_length,
                             const p8 address_to digestinfo,
                             positive digestinfo_length, p8 address_to hash,
                             positive hash_length)
{
        p64 mod[CRYPTO_RSA_LIMBS];
        p64 base[CRYPTO_RSA_LIMBS];
        p64 out[CRYPTO_RSA_LIMBS];
        p8 em[512];
        positive limbs;
        positive k;
        positive i;

        if (!crypto_rsa_prepare(n_bytes, n_length, exponent, sig,
                                sig_length, mod, base, address_of limbs))
                return false;

        crypto_rsa_modexp(out, base, exponent, mod, limbs);
        k = n_length;
        memory_fill(em, 0, sizeof(em));
        {
                p8 full[512];
                crypto_fe_store_be(full, out, limbs);
                memory_copy(em, full + limbs * 8 - k, k);
        }

        if (em[0] != 0x00 || em[1] != 0x01)
                return false;

        i = 2;
        i += memory_span_byte(em + i, 0xff, k - i);
        if (i < 10 || i >= k || em[i] != 0x00)
                return false;
        i++;
        if (i + digestinfo_length + hash_length != k)
                return false;
        if (memory_compare(em + i, digestinfo, digestinfo_length))
                return false;
        return memory_compare(em + i + digestinfo_length, hash, hash_length) ==
               0;
}

static bool crypto_rsa_pkcs1_sha256(p8 address_to n_bytes, positive n_length,
                                    p64 exponent, p8 address_to sig,
                                    positive sig_length, p8 address_to hash)
{
        static const p8 digestinfo[19] = {
            0x30, 0x31, 0x30, 0x0d, 0x06, 0x09, 0x60, 0x86, 0x48, 0x01,
            0x65, 0x03, 0x04, 0x02, 0x01, 0x05, 0x00, 0x04, 0x20};

        return crypto_rsa_pkcs1(n_bytes, n_length, exponent, sig, sig_length,
                                digestinfo, sizeof digestinfo, hash, 32);
}

static bool crypto_rsa_pkcs1_sha384(p8 address_to n_bytes, positive n_length,
                                    p64 exponent, p8 address_to sig,
                                    positive sig_length, p8 address_to hash)
{
        static const p8 digestinfo[19] = {
            0x30, 0x41, 0x30, 0x0d, 0x06, 0x09, 0x60, 0x86, 0x48, 0x01,
            0x65, 0x03, 0x04, 0x02, 0x02, 0x05, 0x00, 0x04, 0x30};

        return crypto_rsa_pkcs1(n_bytes, n_length, exponent, sig, sig_length,
                                digestinfo, sizeof digestinfo, hash, 48);
}

static fn crypto_mgf1_sha256(p8 address_to seed, positive seed_length,
                             p8 address_to into, positive want)
{
        positive offset = 0;
        p32 counter = 0;

        while (offset < want)
        {
                crypto_sha256 hash;
                p8 block[32];
                p8 count[4];
                positive take;

                network_store_32(count, counter);
                crypto_sha256_open(address_of hash);
                crypto_sha256_write(address_of hash, seed, seed_length);
                crypto_sha256_write(address_of hash, count, 4);
                crypto_sha256_close(address_of hash, block);
                take = want - offset;
                if (take > 32)
                        take = 32;
                memory_copy(into + offset, block, take);
                offset += take;
                counter++;
        }
}

/* TLS 1.3 rsa_pss_rsae_sha256: EMSA-PSS with SHA-256, MGF1-SHA-256, salt 32. */
static bool crypto_rsa_pss_sha256(p8 address_to n_bytes, positive n_length,
                                  p64 exponent, p8 address_to sig,
                                  positive sig_length, p8 address_to message,
                                  positive message_length)
{
        p64 mod[CRYPTO_RSA_LIMBS];
        p64 base[CRYPTO_RSA_LIMBS];
        p64 out[CRYPTO_RSA_LIMBS];
        p8 em[512];
        p8 mask[512];
        p8 mhash[32];
        p8 hcheck[32];
        p8 prefix[8];
        crypto_sha256 hash;
        positive limbs;
        positive k;
        positive mod_bits = 0;
        positive em_bits;
        positive unused;
        positive masked;
        positive at;
        positive i;

        if (!crypto_rsa_prepare(n_bytes, n_length, exponent, sig,
                                sig_length, mod, base, address_of limbs))
                return false;

        for (i = 0; i < n_length; i++)
                if (n_bytes[i])
                {
                        p8 value = n_bytes[i];
                        positive bits = 0;

                        while (value)
                        {
                                bits++;
                                value >>= 1;
                        }
                        mod_bits = (n_length - i - 1) * 8 + bits;
                        break;
                }

        if (mod_bits < 8 * 64)
                return false;

        em_bits = mod_bits - 1;
        k = (em_bits + 7) / 8;
        if (k > n_length || k < 32 + 32 + 2)
                return false;

        crypto_rsa_modexp(out, base, exponent, mod, limbs);
        {
                p8 full[512];

                crypto_fe_store_be(full, out, limbs);
                memory_copy(em, full + limbs * 8 - n_length, n_length);
        }

        if (n_length != k)
        {
                if (n_length < k)
                        return false;
                for (i = 0; i < n_length - k; i++)
                        if (em[i])
                                return false;
                memory_copy(em, em + n_length - k, k);
        }

        unused = 8 * k - em_bits;
        if (unused && (em[0] >> (8 - unused)))
                return false;
        if (em[k - 1] != 0xbc)
                return false;

        masked = k - 32 - 1;
        crypto_mgf1_sha256(em + masked, 32, mask, masked);
        for (i = 0; i < masked; i++)
                em[i] ^= mask[i];
        if (unused)
                em[0] &= (p8)(0xff >> unused);

        at = 0;
        at += memory_span_byte(em + at, 0, masked - at);
        if (at >= masked || em[at] != 0x01)
                return false;
        at++;
        if (masked - at != 32)
                return false;

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
   under AES-128-GCM a protected one adds the inner type byte and the tag. */
#define TLS_PLAINTEXT_MAX 16384
#define TLS_RECORD_MAX (TLS_PLAINTEXT_MAX + 1 + 16)
/* One receive takes as many whole records as the socket has queued and this
   room holds: about fifteen full records. At least two whole records must
   fit, since the unopened tail moves to the front only when a record would
   not. */
#ifndef TLS_RECEIVE_ROOM
#define TLS_RECEIVE_ROOM ((positive)1 << 18)
#endif
#define TLS_HS_MAX 16384
#define TLS_HANDSHAKE_SECONDS 30
#define TLS_OK 0
#define TLS_FAIL (-1)
#define TLS_EOF 1
#define TLS_AGAIN 2

#define TLS_CT_CCS 20
#define TLS_CT_ALERT 21
#define TLS_CT_HANDSHAKE 22
#define TLS_CT_APP 23

#define TLS_HS_CLIENT_HELLO 1
#define TLS_HS_SERVER_HELLO 2
#define TLS_HS_NEW_SESSION_TICKET 4
#define TLS_HS_ENCRYPTED_EXTS 8
#define TLS_HS_CERTIFICATE 11
#define TLS_HS_CERT_VERIFY 15
#define TLS_HS_FINISHED 20

/* One trust anchor from anchors.inc: hashes that find candidates, then the
   key itself (curve 1 P-256, 2 P-384, 3 RSA), key_length raw bytes at key_at
   in tls_anchor_keys. */
typedef struct
{
        p8 name[8];
        p8 key_hash[8];
        p8 curve;
        p32 exponent;
        p16 key_length;
        p32 key_at;
} tls_anchor;

#include "anchors.inc"

static const p8 tls_oid_ec[7] = {0x2a, 0x86, 0x48, 0xce, 0x3d, 0x02, 0x01};
static const p8 tls_oid_p256[8] = {0x2a, 0x86, 0x48, 0xce, 0x3d, 0x03, 0x01, 0x07};
static const p8 tls_oid_p384[5] = {0x2b, 0x81, 0x04, 0x00, 0x22};
static const p8 tls_oid_ecdsa_sha256[8] = {0x2a, 0x86, 0x48, 0xce, 0x3d, 0x04,
                                           0x03, 0x02};
static const p8 tls_oid_ecdsa_sha384[8] = {0x2a, 0x86, 0x48, 0xce, 0x3d, 0x04,
                                           0x03, 0x03};
static const p8 tls_oid_sha256_rsa[9] = {0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d,
                                         0x01, 0x01, 0x0b};
static const p8 tls_oid_sha384_rsa[9] = {0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d,
                                         0x01, 0x01, 0x0c};
static const p8 tls_oid_rsa[9] = {0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01,
                                  0x01, 0x01};
static const p8 tls_oid_san[3] = {0x55, 0x1d, 0x11};
static const p8 tls_oid_basic_constraints[3] = {0x55, 0x1d, 0x13};
static const p8 tls_oid_name_constraints[3] = {0x55, 0x1d, 0x1e};
static const p8 tls_oid_key_usage[3] = {0x55, 0x1d, 0x0f};
static const p8 tls_oid_extended_key_usage[3] = {0x55, 0x1d, 0x25};
static const p8 tls_oid_server_auth[8] = {0x2b, 0x06, 0x01, 0x05,
                                          0x05, 0x07, 0x03, 0x01};

typedef struct
{
        bipolar handle;
        bool check_cert;
        bool encrypted;
        bool application;
        string_address host;
        crypto_sha256 transcript;
        p8 x25519_scalar[32];
        p8 p256_scalar[32];
        p8 p384_scalar[48];
        p8 hs_secret[32];
        p8 c_hs_traffic[32];
        p8 s_hs_traffic[32];
        p8 c_ap_traffic[32];
        p8 s_ap_traffic[32];
        p8 c_key[16];
        p8 s_key[16];
        p8 c_iv[12];
        p8 s_iv[12];
        crypto_aesgcm_key c_gcm;
        crypto_aesgcm_key s_gcm;
        p64 seq_read;
        p64 seq_write;
        p8 leaf_qx[48];
        p8 leaf_qy[48];
        p8 leaf_n[512];
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

/* RFC 8446 requires each AEAD key to stay within its usage bound.  This
   client intentionally does not implement KeyUpdate, so end the connection
   before AES-GCM reaches the 2^24.5-record analysis bound.  The conservative
   integer limit also makes sequence wrap unreachable. */
#define TLS_AES_GCM_RECORD_LIMIT ((p64)1 << 24)

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
                           p8 address_to context, positive context_length,
                           p8 address_to out, positive out_length)
{
        p8 info[256];
        positive label_length = string_length(label);
        positive used = 2;

        // HkdfLabel is 2 + 1 + 6 + label + 1 + context. The copies below
        // use the full lengths, so a truncated length byte is not a bound.
        if (label_length > 249 || context_length > 255 ||
            used + 1 + 6 + label_length + 1 + context_length > sizeof info)
                return;

        info[0] = (p8)(out_length >> 8);
        info[1] = (p8)out_length;
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

static COLD fn tls_empty_hash(p8 address_to out)
{
        crypto_sha256 hash;

        crypto_sha256_open(address_of hash);
        crypto_sha256_close(address_of hash, out);
        crypto_forget(address_of hash, sizeof hash);
}

static COLD fn tls_traffic_keys(p8 address_to traffic, p8 address_to key,
                           p8 address_to iv)
{
        tls_expand_label(traffic, "key", null, 0, key, 16);
        tls_expand_label(traffic, "iv", null, 0, iv, 12);
}

static fn tls_nonce(p8 address_to iv, p64 seq, p8 address_to nonce)
{
        p8 seq_bytes[12];
        positive i;

        memory_fill(seq_bytes, 0, 12);
        crypto_put_be64(seq_bytes + 4, seq);
        for (i = 0; i < 12; i++)
                nonce[i] = iv[i] ^ seq_bytes[i];
        crypto_forget(seq_bytes, sizeof seq_bytes);
}

/* A record leaves in one send. Sent in pieces, everything after the first
   piece waits under Nagle for the peer to acknowledge it. */
static COLD bipolar tls_send_plain(tls_conn address_to tls, p8 type, p8 address_to body,
                              positive length)
{
        p8 record[5 + TLS_HS_MAX];

        if (length > TLS_HS_MAX)
                return TLS_FAIL;
        record[0] = type;
        record[1] = 0x03;
        record[2] = 0x03;
        record[3] = (p8)(length >> 8);
        record[4] = (p8)length;
        memory_copy(record + 5, body, length);
        return network_stream_send_all(tls->handle, record, 5 + length)
                   ? TLS_OK : TLS_FAIL;
}

static bipolar tls_send_enc(tls_conn address_to tls, p8 inner_type,
                            p8 address_to body, positive length)
{
        p8 record[5 + TLS_RECORD_MAX];
        p8 address_to header = record;
        p8 address_to inner = record + 5;
        p8 nonce[12];
        p8 tag[16];
        p8 aad[5];
        positive inner_length = 0;
        positive record_length = 0;
        bipolar status = TLS_FAIL;

        if (length > TLS_PLAINTEXT_MAX ||
            tls->seq_write >= TLS_AES_GCM_RECORD_LIMIT)
                goto done;
        inner_length = length + 1;
        record_length = inner_length + 16;

        memory_copy(inner, body, length);
        inner[length] = inner_type;

        header[0] = TLS_CT_APP;
        header[1] = 0x03;
        header[2] = 0x03;
        header[3] = (p8)(record_length >> 8);
        header[4] = (p8)record_length;
        memory_copy(aad, header, 5);

        tls_nonce(tls->c_iv, tls->seq_write, nonce);
        crypto_aesgcm_seal(address_of tls->c_gcm, nonce, aad, 5, inner, inner_length,
                              tag);
        tls->seq_write++;

        memory_copy(inner + inner_length, tag, 16);
        status = network_stream_send_all(tls->handle, record, 5 + record_length)
                     ? TLS_OK : TLS_FAIL;

done:
        if (record_length)
                crypto_forget(record, 5 + record_length);
        crypto_forget(nonce, sizeof nonce);
        crypto_forget(tag, sizeof tag);
        crypto_forget(aad, sizeof aad);
        return status;
}

static bipolar tls_decrypt_record(tls_conn address_to tls, p8 address_to payload,
                                  positive payload_length, p8 address_to aad,
                                  p8 address_to inner, positive address_to inner_length,
                                  p8 address_to type)
{
        p8 nonce[12];
        p8 tag[16];
        positive at = 0;
        bipolar status = TLS_FAIL;

        if (payload_length < 16 ||
            tls->seq_read >= TLS_AES_GCM_RECORD_LIMIT)
                goto done;

        // The record layer opens records where they lie: inner is payload.
        if (inner != payload)
                memory_copy(inner, payload, payload_length - 16);
        memory_copy(tag, payload + payload_length - 16, 16);
        tls_nonce(tls->s_iv, tls->seq_read, nonce);
        if (!crypto_aesgcm_open(address_of tls->s_gcm, nonce, aad, 5, inner,
                                   payload_length - 16, tag))
                goto done;

        tls->seq_read++;
        at = payload_length - 16;
        while (at && inner[at - 1] == 0)
                at--;
        if (!at)
                goto done;
        address_to type = inner[at - 1];
        address_to inner_length = at - 1;
        status = TLS_OK;

done:
        if (status && payload_length >= 16)
                crypto_forget(inner, payload_length - 16);
        crypto_forget(nonce, sizeof nonce);
        crypto_forget(tag, sizeof tag);
        return status;
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

/* Receive behind receive_end. Opened bytes before receive_start are dropped:
   for free when nothing unopened remains, and otherwise the unopened tail
   moves to the front only when the room behind it could not hold a whole
   record, so a record arriving in pieces is not moved again per piece. The
   read is tried before any wait, because mid-transfer the socket almost
   always has bytes queued; only an empty socket polls, under the deadline,
   and nothing blocks past it. A peer keeping the socket full of records
   that deliver nothing never lets a read wait, so the deadline is asked
   before every read as well. */
static bool tls_receive(tls_conn address_to tls,
                        const network_deadline address_to deadline)
{
        positive have = tls->receive_end - tls->receive_start;
        positive room;
        positive seconds;
        positive nanoseconds;
        bipolar got;

        if (deadline && !network_deadline_left(deadline, address_of seconds,
                                               address_of nanoseconds))
                return false;

        if (!have)
        {
                tls->receive_start = 0;
                tls->receive_end = 0;
        }
        else if (sizeof(tls->receive) - tls->receive_end < 5 + TLS_RECORD_MAX)
        {
                memory_copy(tls->receive, tls->receive + tls->receive_start,
                            have);
                tls->receive_start = 0;
                tls->receive_end = have;
        }
        room = sizeof(tls->receive) - tls->receive_end;

        do
        {
                p8 address_to into = tls->receive + tls->receive_end;

                if (!deadline)
                        got = system_read_retry((positive)tls->handle, into,
                                                room);
                else
                {
                        got = socket_receive((b32)tls->handle, into, room,
                                             MSG_DONTWAIT, null, 0);
                        if (got == NETWORK_TRY_AGAIN)
                                got = network_stream_read_some_until(
                                    tls->handle, into, room, deadline);
                }
        } while (got == NETWORK_INTERRUPTED);

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
                                return TLS_FAIL;
                        payload_length = network_load_16(header + 3);
                        if (!payload_length || payload_length > TLS_RECORD_MAX)
                                return TLS_FAIL;
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
                        return TLS_FAIL;
                address_to type = TLS_CT_CCS;
                address_to inner = payload;
                address_to length = 0;
                return TLS_OK;
        }

        if (!tls->encrypted)
        {
                if (header[0] != TLS_CT_HANDSHAKE ||
                    payload_length > TLS_PLAINTEXT_MAX)
                        return TLS_FAIL;
                address_to type = TLS_CT_HANDSHAKE;
                address_to inner = payload;
                address_to length = payload_length;
                return TLS_OK;
        }

        /* A change_cipher_spec that arrives protected is unexpected. */
        if (header[0] != TLS_CT_APP ||
            tls_decrypt_record(tls, payload, payload_length, header, payload,
                               address_of inner_length, address_of inner_type) ||
            inner_type == TLS_CT_CCS)
                return TLS_FAIL;

        if (inner_type == TLS_CT_ALERT)
                return inner_length == 2 && payload[1] == 0 ? TLS_EOF
                                                            : TLS_FAIL;

        address_to type = inner_type;
        address_to inner = payload;
        address_to length = inner_length;
        return TLS_OK;
}

/* The handshake's copy of one record. */
static bipolar tls_read_record(tls_conn address_to tls, p8 address_to type,
                               p8 address_to body, positive room,
                               positive address_to length,
                               const network_deadline address_to deadline)
{
        p8 address_to inner = null;
        positive inner_length = 0;
        bipolar status = tls_next_record(tls, type, address_of inner,
                                         address_of inner_length, deadline);

        if (status)
                return status;
        if (inner_length > room)
                return TLS_FAIL;
        memory_copy(body, inner, inner_length);
        crypto_forget(inner, inner_length);
        address_to length = inner_length;
        return TLS_OK;
}

static fn tls_transcript_add(tls_conn address_to tls, p8 address_to msg,
                             positive length)
{
        crypto_sha256_write(address_of tls->transcript, msg, length);
}

static COLD bipolar tls_asn1_length(p8 address_to bytes, positive size,
                               positive address_to at, positive address_to length)
{
        positive i = address_to at;
        p8 first;
        positive count;
        positive value;

        if (i >= size)
                return TLS_FAIL;

        first = bytes[i++];
        if (first < 0x80)
        {
                address_to length = first;
                address_to at = i;
                return TLS_OK;
        }

        count = first & 0x7f;
        value = 0;
        if (!count || count > 3 || i + count > size || !bytes[i])
                return TLS_FAIL;
        while (count)
        {
                value = (value << 8) | bytes[i++];
                count--;
        }
        if (value < 0x80)
                return TLS_FAIL;
        address_to length = value;
        address_to at = i;
        return TLS_OK;
}

static COLD bipolar tls_asn1_enter(p8 address_to bytes, positive size, p8 tag,
                              positive address_to at, positive address_to stop)
{
        positive i = address_to at;
        positive length = 0;

        if (i >= size || bytes[i] != tag)
                return TLS_FAIL;
        i++;
        address_to at = i;
        if (tls_asn1_length(bytes, size, at, address_of length))
                return TLS_FAIL;
        if (address_to at + length > size)
                return TLS_FAIL;
        address_to stop = address_to at + length;
        return TLS_OK;
}

/* Past one value of whatever tag it carries: entering it and then taking its
   end for the next value's start is the whole of skipping it. */
static COLD bipolar tls_asn1_skip(p8 address_to bytes, positive size, positive address_to at)
{
        positive stop = 0;

        if (address_to at >= size ||
            tls_asn1_enter(bytes, size, bytes[address_to at], at,
                           address_of stop))
                return TLS_FAIL;

        address_to at = stop;
        return TLS_OK;
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

/* The verifier implements these three certificate signature algorithms.
   ECDSA parameters must be absent; RSA's historical NULL may be present or
   absent, but no other parameter or trailing value is accepted. */
static COLD bool tls_signature_algorithm(p8 address_to der, positive size,
                                    positive address_to at,
                                    p8 address_to address_to oid,
                                    positive address_to oid_length)
{
        positive alg_stop = 0;
        positive oid_at;
        positive oid_stop = 0;
        bool ecdsa;
        bool rsa;

        if (tls_asn1_enter(der, size, 0x30, at, address_of alg_stop))
                return false;
        oid_at = address_to at;
        if (tls_asn1_enter_oid(der, alg_stop, address_of oid_at,
                               address_of oid_stop))
                return false;

        ecdsa = tls_oid_is(der + oid_at, oid_stop - oid_at,
                           tls_oid_ecdsa_sha256, sizeof tls_oid_ecdsa_sha256) ||
                tls_oid_is(der + oid_at, oid_stop - oid_at,
                           tls_oid_ecdsa_sha384, sizeof tls_oid_ecdsa_sha384);
        rsa = tls_oid_is(der + oid_at, oid_stop - oid_at,
                         tls_oid_sha256_rsa, sizeof tls_oid_sha256_rsa) ||
              tls_oid_is(der + oid_at, oid_stop - oid_at,
                         tls_oid_sha384_rsa, sizeof tls_oid_sha384_rsa);
        if (!ecdsa && !rsa)
                return false;
        if (ecdsa && oid_stop != alg_stop)
                return false;
        if (rsa && oid_stop != alg_stop &&
            (oid_stop + 2 != alg_stop || der[oid_stop] != 0x05 ||
             der[oid_stop + 1] != 0))
                return false;

        address_to oid = der + oid_at;
        address_to oid_length = oid_stop - oid_at;
        address_to at = alg_stop;
        return true;
}

static COLD bool tls_host_match(string_address host, p8 address_to name,
                           positive name_length)
{
        positive host_length = string_length(host);
        string_address star;

        if (name_length == host_length &&
            !memory_compare_ascii_case(name, host, host_length))
                return true;

        if (!name_length || name[0] != '*' || name_length < 3 || name[1] != '.')
                return false;

        /* A wildcard leaves at least two labels beneath it. "*.com" is one
           label, and a certificate carrying it would stand for every host in
           a whole public suffix; no issuer means to say that, and a client
           that reads it as written hands one certificate the internet. */
        {
                positive rest = name_length - 2;
                positive dot = memory_span_without_byte(name + 2, '.', rest);

                if (dot + 1 >= rest)
                        return false;
        }

        star = string_first_of(host, '.');
        if (!star || !star[1])
                return false;

        return string_length(star) == name_length - 1 &&
               !memory_compare_ascii_case(star, (string_address)(name + 1),
                                          name_length - 1);
}

static COLD bool tls_general_name_match(string_address host, p8 tag,
                                   p8 address_to name, positive name_length)
{
        bipolar address = string_to_host(host);

        if (address >= 0)
                return tag == 0x87 && name_length == 4 &&
                       network_load_32(name) == (p32)address;

        return tag == 0x82 && tls_host_match(host, name, name_length);
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
                if (tag == 0x81 || tag == 0x82 || tag == 0x86)
                        for (positive byte = at; byte < name_stop; byte++)
                                if (value[byte] < 0x20 || value[byte] >= 0x7f)
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

static COLD bipolar tls_parse_ecdsa_sig(p8 address_to sig, positive length,
                                   p8 address_to r, positive address_to r_length,
                                   p8 address_to s, positive address_to s_length)
{
        positive at = 0;
        positive stop = 0;
        positive r_stop = 0;
        positive s_stop = 0;
        positive value_at;
        positive value_length;

        if (tls_asn1_enter(sig, length, 0x30, address_of at, address_of stop) ||
            stop != length)
                return TLS_FAIL;
        if (tls_asn1_enter(sig, stop, 0x02, address_of at, address_of r_stop))
                return TLS_FAIL;
        if (!tls_positive_integer(sig, at, r_stop, address_of value_at,
                                  address_of value_length) ||
            value_length > 48)
                return TLS_FAIL;
        address_to r_length = value_length;
        memory_copy(r, sig + value_at, value_length);
        at = r_stop;
        if (tls_asn1_enter(sig, stop, 0x02, address_of at, address_of s_stop))
                return TLS_FAIL;
        if (!tls_positive_integer(sig, at, s_stop, address_of value_at,
                                  address_of value_length) ||
            value_length > 48)
                return TLS_FAIL;
        address_to s_length = value_length;
        memory_copy(s, sig + value_at, value_length);
        return s_stop == stop ? TLS_OK : TLS_FAIL;
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
        p8 curve;
        p8 qx[48];
        p8 qy[48];
        p8 modulus[512];
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
        bool extended_key_usage;
        bool server_auth;
        bool san;
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

static COLD bipolar tls_parse_validity(p8 address_to der, positive size,
                                  positive address_to at, tls_cert address_to cert)
{
        positive validity_stop = 0;
        positive time_stop = 0;
        p8 tag;

        if (tls_asn1_enter(der, size, 0x30, at, address_of validity_stop) ||
            address_to at >= validity_stop)
                return TLS_FAIL;
        tag = der[address_to at];
        if (tls_asn1_enter(der, validity_stop, tag, at, address_of time_stop) ||
            !tls_date_value(tag, der + address_to at, time_stop - address_to at,
                            address_of cert->not_before))
                return TLS_FAIL;
        address_to at = time_stop;
        if (address_to at >= validity_stop)
                return TLS_FAIL;
        tag = der[address_to at];
        if (tls_asn1_enter(der, validity_stop, tag, at, address_of time_stop) ||
            !tls_date_value(tag, der + address_to at, time_stop - address_to at,
                            address_of cert->not_after) ||
            time_stop != validity_stop || cert->not_after < cert->not_before)
                return TLS_FAIL;
        address_to at = validity_stop;
        return TLS_OK;
}

static COLD bipolar tls_parse_basic_constraints(p8 address_to value, positive length,
                                            tls_cert address_to cert)
{
        positive at = 0;
        positive stop = 0;

        if (tls_asn1_enter(value, length, 0x30, address_of at, address_of stop) ||
            stop != length)
                return TLS_FAIL;
        if (at < stop && value[at] == 0x01)
        {
                positive boolean_stop = 0;

                if (tls_asn1_enter(value, stop, 0x01, address_of at,
                                   address_of boolean_stop) ||
                    at + 1 != boolean_stop ||
                    value[at] != 0xff)
                        return TLS_FAIL;
                /* cA also has DEFAULT FALSE and must be omitted when false in
                   DER. An encoded value is therefore canonical TRUE only. */
                cert->ca = true;
                at = boolean_stop;
        }
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
        positive at = 0;
        positive stop = 0;
        p8 unused;
        p8 canonical_unused = 0;
        p8 final;

        if (tls_asn1_enter(value, length, 0x03, address_of at, address_of stop) ||
            stop != length || at >= stop)
                return TLS_FAIL;
        unused = value[at++];
        if (unused > 7 || at >= stop)
                return TLS_FAIL;
        final = value[stop - 1];
        if (!final)
                return TLS_FAIL;
        while (!(final & 1))
        {
                canonical_unused++;
                final >>= 1;
        }
        /* KeyUsage is a named bit list. DER removes every trailing zero bit,
           so the unused-bit count must be exactly the trailing-zero count in
           the final nonzero octet. Merely checking that declared unused bits
           are zero accepts alternate signed encodings and trailing zero
           octets which stricter certificate validators reject. */
        if (unused != canonical_unused)
                return TLS_FAIL;
        /* RFC 5280 defines exactly nine KeyUsage bits. The ninth is
           decipherOnly and is permitted only with keyAgreement. Rejecting
           further set bits matters even though this client does not act on
           them: silently ignoring undefined authorization bits creates a
           profile differential with validators that enforce the schema. */
        if (stop - at > 2 ||
            (stop - at == 2 &&
             (value[at + 1] != 0x80 || !(value[at] & 0x08))))
                return TLS_FAIL;
        cert->digital_signature = (value[at] & 0x80) != 0;
        cert->key_cert_sign = (value[at] & 0x04) != 0;
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
                at = oid_stop;
        }
        return TLS_OK;
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
                if (at < extension_stop && der[at] == 0x01)
                {
                        positive boolean_stop = 0;

                        if (tls_asn1_enter(der, extension_stop, 0x01, address_of at,
                                           address_of boolean_stop) ||
                            at + 1 != boolean_stop ||
                            der[at] != 0xff)
                                return TLS_FAIL;
                        /* critical has DEFAULT FALSE. DER omits a component
                           carrying its default value, so an encoded BOOLEAN
                           can only be canonical TRUE. Accepting explicit
                           FALSE lets BER and DER validators disagree over the
                           same signed extension envelope. */
                        critical = true;
                        at = boolean_stop;
                }
                if (tls_asn1_enter(der, extension_stop, 0x04, address_of at,
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
                        cert->san = true;
                        cert->san_match = matched;
                }
                else if (tls_oid_is(der + oid_at, oid_stop - oid_at,
                                    tls_oid_name_constraints, 3))
                {
                        /* Namespace limits apply even when an issuer marks
                           them non-critical.  Until they are implemented,
                           accepting the chain would authorize names outside
                           the issuer's permitted subtrees. */
                        return TLS_FAIL;
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
        cert->tbs = der + at;
        if (tls_asn1_enter(der, length, 0x30, address_of at, address_of tbs_stop))
                return TLS_FAIL;
        cert->tbs_length = (positive)((der + tbs_stop) - cert->tbs);

        at = tbs_stop;
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
        {
                positive name_at = at;
                positive name_stop = 0;

                if (tls_asn1_enter(der, tbs_stop, 0x30, address_of at,
                                   address_of name_stop))
                        return TLS_FAIL;
                at = name_stop;
                cert->issuer = der + name_at;
                cert->issuer_length = at - name_at;
        }
        if (tls_parse_validity(der, tbs_stop, address_of at, cert))
                return TLS_FAIL;
        {
                positive name_at = at;
                positive name_stop = 0;

                if (tls_asn1_enter(der, tbs_stop, 0x30, address_of at,
                                   address_of name_stop))
                        return TLS_FAIL;
                at = name_stop;
                cert->subject = der + name_at;
                cert->subject_length = at - name_at;
        }
        if (!cert->issuer_length || !cert->subject_length)
                return TLS_FAIL;

        if (tls_asn1_enter(der, tbs_stop, 0x30, address_of at, address_of spki_stop))
                return TLS_FAIL;
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

                if (oid_stop != alg_stop &&
                    (oid_stop + 2 != alg_stop || der[oid_stop] != 0x05 ||
                     der[oid_stop + 1] != 0))
                        return TLS_FAIL;

                cert->curve = 3;
                if (tls_asn1_enter(der, bit_stop, 0x30, address_of at,
                                   address_of rsa_stop) ||
                    rsa_stop != bit_stop)
                        return TLS_FAIL;
                if (tls_asn1_enter(der, rsa_stop, 0x02, address_of at,
                                   address_of n_stop))
                        return TLS_FAIL;
                if (!tls_positive_integer(der, at, n_stop,
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
                    !(cert->exponent & 1))
                        return TLS_FAIL;
        }
        else
                return TLS_FAIL;

        return tls_parse_extensions(der, tbs_stop, spki_stop, version,
                                    cert, host);
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
#endif

/* A served certificate carrying an anchor's key ends the chain, whoever
   signed it: the key hash finds candidates and the whole key decides. */
static COLD bool tls_spki_is_anchor(tls_cert address_to cert)
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
                return true;
#endif

        for (positive i = 0; i < array_count(tls_anchors); i++)
        {
                tls_cert root;

                if (tls_anchors[i].curve != cert->curve ||
                    memory_compare(tls_anchors[i].key_hash, digest, 8) ||
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
        p8 hash[48];
        p8 r[48];
        p8 s[48];
        positive r_length = 0;
        positive s_length = 0;
        p8 address_to qx = issuer->qx + (issuer->curve == 1 ? 16 : 0);
        p8 address_to qy = issuer->qy + (issuer->curve == 1 ? 16 : 0);

        if (tls_oid_is(child->sig_oid, child->sig_oid_length, tls_oid_ecdsa_sha384,
                       8))
        {
                if (issuer->curve != 1 && issuer->curve != 2)
                        return false;
                crypto_sha384(child->tbs, child->tbs_length, hash);
                if (tls_parse_ecdsa_sig(child->sig, child->sig_length, r,
                                        address_of r_length, s, address_of s_length))
                        return false;
                if (issuer->curve == 2)
                        return crypto_ecdsa_p384(hash, 48, r, r_length, s, s_length,
                                                 qx, qy);
                if (issuer->curve == 1)
                        return crypto_ecdsa_p256(hash, 48, r, r_length, s, s_length,
                                                 qx, qy);
                return false;
        }

        if (tls_oid_is(child->sig_oid, child->sig_oid_length, tls_oid_ecdsa_sha256,
                       8))
        {
                if (issuer->curve != 1 && issuer->curve != 2)
                        return false;
                crypto_sha256_of(child->tbs, child->tbs_length, hash);
                if (tls_parse_ecdsa_sig(child->sig, child->sig_length, r,
                                        address_of r_length, s, address_of s_length))
                        return false;
                if (issuer->curve == 1)
                        return crypto_ecdsa_p256(hash, 32, r, r_length, s, s_length,
                                                 qx, qy);
                if (issuer->curve == 2)
                        return crypto_ecdsa_p384(hash, 32, r, r_length, s, s_length,
                                                 qx, qy);
                return false;
        }

        if (tls_oid_is(child->sig_oid, child->sig_oid_length, tls_oid_sha256_rsa, 9))
        {
                if (issuer->curve != 3)
                        return false;
                crypto_sha256_of(child->tbs, child->tbs_length, hash);
                return crypto_rsa_pkcs1_sha256(issuer->modulus, issuer->modulus_length,
                                               issuer->exponent, child->sig,
                                               child->sig_length, hash);
        }

        if (tls_oid_is(child->sig_oid, child->sig_oid_length, tls_oid_sha384_rsa, 9))
        {
                if (issuer->curve != 3)
                        return false;
                crypto_sha384(child->tbs, child->tbs_length, hash);
                return crypto_rsa_pkcs1_sha384(issuer->modulus, issuer->modulus_length,
                                               issuer->exponent, child->sig,
                                               child->sig_length, hash);
        }

        return false;
}

/* The last certificate served names its issuer.  Each anchor with that
   subject Name is tried, and one signature that verifies ends the chain; a
   Name no anchor carries fails without any signature check. */
static COLD bool tls_anchor_verifies(tls_cert address_to child)
{
        p8 digest[32];

        if (!child->issuer || !child->issuer_length)
                return false;
#ifdef TLS_BENCH_ANCHOR
        {
                tls_cert root;

                memory_fill(address_of root, 0, sizeof(root));
                root.curve = 2;
                memory_copy(root.qx, tls_bench_anchor_x, 48);
                memory_copy(root.qy, tls_bench_anchor_y, 48);
                if (tls_verify_one(child, address_of root))
                        return true;
        }
#endif
        crypto_sha256_of(child->issuer, child->issuer_length, digest);
        for (positive i = 0; i < array_count(tls_anchors); i++)
        {
                tls_cert root;

                if (memory_compare(tls_anchors[i].name, digest, 8) ||
                    !tls_anchor_key(tls_anchors + i, address_of root))
                        continue;
                if (tls_verify_one(child, address_of root))
                        return true;
        }

        return false;
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

static COLD bool tls_date_now(p64 address_to value)
{
        time_t stamp = time(null);
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

// A TLS handshake length: three bytes, most significant first.
static PURE positive tls_load_24(p8 address_to at)
{
        return (positive)network_load_16(at) << 8 | at[2];
}

static COLD bool tls_certificate_body_open(p8 address_to body,
                                      positive body_length,
                                      positive address_to entries_at,
                                      positive address_to list_end)
{
        positive at = 1;
        positive list_length;

        /* The server Certificate in this initial handshake has an empty
           request_context.  Its three-byte list vector must consume the rest
           of the handshake body exactly. */
        if (body_length < 4 || body[0] != 0 || at + 3 > body_length)
                return false;

        list_length = tls_load_24(body + at);
        at += 3;
        if (list_length != body_length - at)
                return false;

        *entries_at = at;
        *list_end = at + list_length;
        return true;
}

static COLD bool tls_verify_chain(p8 address_to body, positive body_length,
                             string_address host, tls_conn address_to tls)
{
        tls_cert certs[8];
        positive count = 0;
        positive at;
        positive list_end;
        positive i;
        p64 now = 0;

        if (!tls_certificate_body_open(body, body_length,
                                       address_of at, address_of list_end))
                return false;

        while (at + 3 <= list_end && count < 8)
        {
                positive cert_length = tls_load_24(body + at);
                positive ext_length;

                at += 3;
                if (at + cert_length + 2 > list_end)
                        return false;
                if (tls_parse_cert(body + at, cert_length, certs + count,
                                   count ? null : host))
                        return false;
                at += cert_length;
                ext_length = network_load_16(body + at);
                at += 2;
                if (at + ext_length > list_end)
                        return false;
                at += ext_length;
                count++;
        }

        if (!count || at != list_end)
                return false;

        tls_keep_leaf(tls, certs);

        if (tls->check_cert && (!certs[0].san || !certs[0].san_match))
                return false;
        if (!tls->check_cert)
                return true;

        if (!tls_date_now(address_of now) || !tls_leaf_authorized(certs, now))
                return false;
        for (i = 1; i < count; i++)
        {
                if (tls_spki_is_anchor(certs + i))
                        break;
                if (!tls_issuer_authorized(certs + i, i - 1, now))
                        return false;
        }

        for (i = 0; i < count; i++)
        {
                if (tls_spki_is_anchor(certs + i))
                        return i > 0;
                if (i + 1 < count)
                {
                        if (!tls_certificate_names_chain(certs + i,
                                                         certs + i + 1) ||
                            !tls_verify_one(certs + i, certs + i + 1))
                                return false;
                }
                else
                        return tls_anchor_verifies(certs + i);
        }

        return false;
}

static COLD bool tls_hello_append(p8 address_to out, positive room,
                             positive address_to at,
                             const p8 address_to bytes, positive length)
{
        if (address_to at > room || length > room - address_to at)
                return false;

        memory_copy(out + address_to at, bytes, length);
        address_to at += length;
        return true;
}

static COLD bipolar tls_client_hello(tls_conn address_to tls, p8 address_to out,
                                positive room, positive address_to used)
{
        static const p8 prefix[] = {
            TLS_HS_CLIENT_HELLO, 0, 0, 0, 0x03, 0x03};
        static const p8 parameters[] = {
            0,                    // empty legacy session id
            0, 2, 0x13, 0x01,    // TLS_AES_128_GCM_SHA256
            1, 0,                 // null legacy compression
            0, 0};                // extensions length, filled below
        static const p8 groups[] = {
            0, 0x0a, 0, 8, 0, 6, 0, 0x1d, 0, 0x17, 0, 0x18};
        static const p8 share_intro[] = {
            0, 0x33, 0, 208, 0, 206};
        static const p8 x25519_item[] = {0, 0x1d, 0, 32};
        static const p8 p256_item[] = {0, 0x17, 0, 65};
        static const p8 p384_item[] = {0, 0x18, 0, 97};
        static const p8 tail[] = {
            0, 0x2b, 0, 3, 2, 0x03, 0x04,
            0, 0x0d, 0, 8, 0, 6, 0x04, 0x03, 0x05, 0x03, 0x08, 0x04};
        p8 random[32];
        p8 x25519_public[32];
        p8 p256_public[65];
        p8 p384_public[97];
        p8 base[32];
        p8 raw[48];
        positive host_length = string_length(tls->host);
        bool named = string_to_host(tls->host) < 0;
        positive at = 0;
        positive ext_len_at;
        positive tries;
        bipolar status = TLS_FAIL;

        if (system_random_fill(random, 32, 0) < 0)
                goto done;
        if (system_random_fill(tls->x25519_scalar, 32, 0) < 0)
                goto done;

        memory_fill(base, 0, 32);
        base[0] = 9;
        if (!crypto_x25519(x25519_public, tls->x25519_scalar, base))
                goto done;

        for (tries = 0;; tries++)
        {
                if (tries > 8 || system_random_fill(raw, 32, 0) < 0)
                        goto done;
                if (crypto_scalar_reduce_be(tls->p256_scalar, raw, 32,
                                            crypto_p256_n, 4))
                        break;
        }
        if (!crypto_ecdh_p256_public(p256_public, tls->p256_scalar))
                goto done;

        for (tries = 0;; tries++)
        {
                if (tries > 8 || system_random_fill(raw, 48, 0) < 0)
                        goto done;
                if (crypto_scalar_reduce_be(tls->p384_scalar, raw, 48,
                                            crypto_p384_n, 6))
                        break;
        }
        if (!crypto_ecdh_p384_public(p384_public, tls->p384_scalar))
                goto done;

        if (!tls_hello_append(out, room, address_of at, prefix, sizeof prefix) ||
            !tls_hello_append(out, room, address_of at, random, sizeof random) ||
            !tls_hello_append(out, room, address_of at, parameters,
                              sizeof parameters))
                goto done;

        ext_len_at = at - 2;

        if (named && host_length && host_length < 256)
        {
                positive n = 5 + host_length;
                p8 sni[] = {
                    0, 0, (p8)(n >> 8), (p8)n,
                    (p8)((n - 2) >> 8), (p8)(n - 2), 0,
                    (p8)(host_length >> 8), (p8)host_length};

                if (!tls_hello_append(out, room, address_of at, sni,
                                      sizeof sni) ||
                    !tls_hello_append(out, room, address_of at,
                                      (const p8 address_to)tls->host,
                                      host_length))
                        goto done;
        }

        if (!tls_hello_append(out, room, address_of at, groups, sizeof groups) ||
            !tls_hello_append(out, room, address_of at, share_intro,
                              sizeof share_intro) ||
            !tls_hello_append(out, room, address_of at, x25519_item,
                              sizeof x25519_item) ||
            !tls_hello_append(out, room, address_of at, x25519_public,
                              sizeof x25519_public) ||
            !tls_hello_append(out, room, address_of at, p256_item,
                              sizeof p256_item) ||
            !tls_hello_append(out, room, address_of at, p256_public,
                              sizeof p256_public) ||
            !tls_hello_append(out, room, address_of at, p384_item,
                              sizeof p384_item) ||
            !tls_hello_append(out, room, address_of at, p384_public,
                              sizeof p384_public) ||
            !tls_hello_append(out, room, address_of at, tail, sizeof tail))
                goto done;

        {
                positive ext_length = at - ext_len_at - 2;
                out[ext_len_at] = (p8)(ext_length >> 8);
                out[ext_len_at + 1] = (p8)ext_length;
        }
        {
                positive body = at - 4;
                out[1] = (p8)(body >> 16);
                out[2] = (p8)(body >> 8);
                out[3] = (p8)body;
        }

        address_to used = at;
        status = TLS_OK;

done:
        crypto_forget(random, sizeof random);
        crypto_forget(x25519_public, sizeof x25519_public);
        crypto_forget(p256_public, sizeof p256_public);
        crypto_forget(p384_public, sizeof p384_public);
        crypto_forget(base, sizeof base);
        crypto_forget(raw, sizeof raw);
        if (status)
        {
                crypto_forget(tls->x25519_scalar, sizeof tls->x25519_scalar);
                crypto_forget(tls->p256_scalar, sizeof tls->p256_scalar);
                crypto_forget(tls->p384_scalar, sizeof tls->p384_scalar);
        }
        return status;
}

static COLD bipolar tls_server_hello_keys(p8 address_to hello, positive length,
                                     p8 address_to peer, positive room,
                                     positive address_to share_length,
                                     positive address_to group)
{
        positive at;
        positive ext_end;
        positive session;
        bool seen_share = false;
        bool seen_version = false;

        if (length < 44 || hello[0] != TLS_HS_SERVER_HELLO)
                return TLS_FAIL;
        {
                positive hs = tls_load_24(hello + 1);
                if (hs + 4 != length)
                        return TLS_FAIL;
        }
        if (hello[4] != 0x03 || hello[5] != 0x03)
                return TLS_FAIL;

        at = 4 + 2 + 32;
        session = hello[at++];
        /* The client sent an empty legacy_session_id, so the echo is empty. */
        if (session)
                return TLS_FAIL;
        at += session;
        if (at + 3 > length)
                return TLS_FAIL;
        if (hello[at] != 0x13 || hello[at + 1] != 0x01)
                return TLS_FAIL;
        at += 2;
        if (hello[at++] != 0)
                return TLS_FAIL;
        if (at + 2 > length)
                return TLS_FAIL;
        {
                positive ext_length = network_load_16(hello + at);
                at += 2;
                ext_end = at + ext_length;
                if (ext_end != length)
                        return TLS_FAIL;
        }

        while (at < ext_end)
        {
                if (at + 4 > ext_end)
                        return TLS_FAIL;

                positive id = network_load_16(hello + at);
                positive elen = network_load_16(hello + at + 2);
                at += 4;
                if (at + elen > ext_end)
                        return TLS_FAIL;

                if (id == 0x002b)
                {
                        if (seen_version || elen != 2 || hello[at] != 0x03 ||
                            hello[at + 1] != 0x04)
                                return TLS_FAIL;
                        seen_version = true;
                }
                else if (id == 0x0033)
                {
                        positive named;
                        positive klen;

                        if (seen_share || elen < 4)
                                return TLS_FAIL;
                        named = network_load_16(hello + at);
                        klen = network_load_16(hello + at + 2);
                        if (4 + klen != elen)
                                return TLS_FAIL;
                        if (!((named == 0x001d && klen == 32) ||
                              (named == 0x0017 && klen == 65) ||
                              (named == 0x0018 && klen == 97)))
                                return TLS_FAIL;
                        if (klen > room)
                                return TLS_FAIL;
                        memory_copy(peer, hello + at + 4, klen);
                        address_to share_length = klen;
                        address_to group = named;
                        seen_share = true;
                }
                else
                        return TLS_FAIL;

                at += elen;
        }

        return seen_version && seen_share ? TLS_OK : TLS_FAIL;
}

#define TLS_HANDSHAKE_MORE 0
#define TLS_HANDSHAKE_COMPLETE 1

/* The handshake protocol is a byte stream layered over records.  ServerHello
   may therefore cross record boundaries, but it is the last plaintext
   handshake message: bytes after its declared end cannot legally share that
   plaintext stream. */
static COLD bipolar tls_handshake_one_append(p8 address_to held, positive room,
                                        positive address_to held_length,
                                        p8 address_to fragment,
                                        positive length)
{
        positive body_length;
        positive complete;

        if (address_to held_length > room || !length ||
            length > room - address_to held_length)
                return TLS_FAIL;

        memory_copy(held + address_to held_length, fragment, length);
        address_to held_length += length;

        if (address_to held_length < 4)
                return TLS_HANDSHAKE_MORE;

        body_length = tls_load_24(held + 1);
        if (body_length > room - 4)
                return TLS_FAIL;
        complete = 4 + body_length;
        if (address_to held_length < complete)
                return TLS_HANDSHAKE_MORE;

        return address_to held_length == complete ? TLS_HANDSHAKE_COMPLETE
                                                   : TLS_FAIL;
}

static COLD bipolar tls_install_handshake_keys(tls_conn address_to tls,
                                          p8 address_to shared,
                                          positive shared_length)
{
        p8 early[32];
        p8 zeros[32];
        p8 derived[32];
        p8 empty[32];

        memory_fill(zeros, 0, 32);
        tls_empty_hash(empty);
        crypto_hkdf_extract(zeros, 32, zeros, 32, early);
        tls_expand_label(early, "derived", empty, 32, derived, 32);
        crypto_hkdf_extract(derived, 32, shared, shared_length, tls->hs_secret);
        tls_derive_secret(tls->hs_secret, "c hs traffic",
                          address_of tls->transcript, tls->c_hs_traffic);
        tls_derive_secret(tls->hs_secret, "s hs traffic",
                          address_of tls->transcript, tls->s_hs_traffic);
        tls_traffic_keys(tls->c_hs_traffic, tls->c_key, tls->c_iv);
        tls_traffic_keys(tls->s_hs_traffic, tls->s_key, tls->s_iv);
        crypto_aesgcm_prepare(address_of tls->c_gcm, tls->c_key);
        crypto_aesgcm_prepare(address_of tls->s_gcm, tls->s_key);
        tls->seq_read = 0;
        tls->seq_write = 0;
        tls->encrypted = true;
        tls->application = false;

        crypto_forget(early, sizeof early);
        crypto_forget(zeros, sizeof zeros);
        crypto_forget(derived, sizeof derived);
        crypto_forget(empty, sizeof empty);
        return TLS_OK;
}

static COLD fn tls_derive_app_keys(tls_conn address_to tls)
{
        p8 zeros[32];
        p8 derived[32];
        p8 empty[32];
        p8 master[32];

        memory_fill(zeros, 0, 32);
        tls_empty_hash(empty);
        tls_expand_label(tls->hs_secret, "derived", empty, 32, derived, 32);
        crypto_hkdf_extract(derived, 32, zeros, 32, master);
        tls_derive_secret(master, "c ap traffic", address_of tls->transcript,
                          tls->c_ap_traffic);
        tls_derive_secret(master, "s ap traffic", address_of tls->transcript,
                          tls->s_ap_traffic);

        crypto_forget(zeros, sizeof zeros);
        crypto_forget(derived, sizeof derived);
        crypto_forget(empty, sizeof empty);
        crypto_forget(master, sizeof master);
}

static COLD fn tls_use_app_keys(tls_conn address_to tls)
{
        tls_traffic_keys(tls->c_ap_traffic, tls->c_key, tls->c_iv);
        tls_traffic_keys(tls->s_ap_traffic, tls->s_key, tls->s_iv);
        crypto_aesgcm_prepare(address_of tls->c_gcm, tls->c_key);
        crypto_aesgcm_prepare(address_of tls->s_gcm, tls->s_key);
        tls->seq_read = 0;
        tls->seq_write = 0;
        tls->application = true;

        crypto_forget(tls->hs_secret, sizeof tls->hs_secret);
        crypto_forget(tls->c_hs_traffic, sizeof tls->c_hs_traffic);
        crypto_forget(tls->s_hs_traffic, sizeof tls->s_hs_traffic);
        crypto_forget(tls->c_ap_traffic, sizeof tls->c_ap_traffic);
        crypto_forget(tls->s_ap_traffic, sizeof tls->s_ap_traffic);
        crypto_forget(address_of tls->transcript, sizeof tls->transcript);
}

static COLD bipolar tls_check_finished(tls_conn address_to tls, p8 address_to verify,
                                  positive length)
{
        p8 finished_key[32];
        p8 expect[32];
        crypto_sha256 copy = tls->transcript;
        p8 hash[32];
        bipolar status = TLS_FAIL;

        if (length != 32)
                goto done;
        tls_expand_label(tls->s_hs_traffic, "finished", null, 0, finished_key, 32);
        crypto_sha256_close(address_of copy, hash);
        crypto_hmac_sha256(finished_key, 32, hash, 32, expect);
        status = crypto_same(expect, verify, 32) ? TLS_OK : TLS_FAIL;

done:
        crypto_forget(finished_key, sizeof finished_key);
        crypto_forget(expect, sizeof expect);
        crypto_forget(address_of copy, sizeof copy);
        crypto_forget(hash, sizeof hash);
        return status;
}

static COLD bipolar tls_send_finished(tls_conn address_to tls)
{
        p8 finished_key[32];
        p8 verify[32];
        p8 msg[36];
        crypto_sha256 copy = tls->transcript;
        p8 hash[32];
        bipolar status = TLS_FAIL;

        tls_expand_label(tls->c_hs_traffic, "finished", null, 0, finished_key, 32);
        crypto_sha256_close(address_of copy, hash);
        crypto_hmac_sha256(finished_key, 32, hash, 32, verify);
        msg[0] = TLS_HS_FINISHED;
        msg[1] = 0;
        msg[2] = 0;
        msg[3] = 32;
        memory_copy(msg + 4, verify, 32);
        if (tls_send_enc(tls, TLS_CT_HANDSHAKE, msg, 36))
                goto done;
        tls_transcript_add(tls, msg, 36);
        status = TLS_OK;

done:
        crypto_forget(finished_key, sizeof finished_key);
        crypto_forget(verify, sizeof verify);
        crypto_forget(msg, sizeof msg);
        crypto_forget(address_of copy, sizeof copy);
        crypto_forget(hash, sizeof hash);
        return status;
}

static COLD bipolar tls_check_cert_verify(tls_conn address_to tls, p8 address_to msg,
                                     positive length)
{
        p8 signed_bytes[130];
        p8 hash[32];
        p8 r[48];
        p8 s[48];
        positive r_length = 0;
        positive s_length = 0;
        positive at;
        positive sig_length;
        p16 scheme;
        crypto_sha256 copy = tls->transcript;
        static const p8 context[] = "TLS 1.3, server CertificateVerify";

        if (length < 8)
                return TLS_FAIL;
        at = 4;
        scheme = network_load_16(msg + at);
        at += 2;
        sig_length = network_load_16(msg + at);
        at += 2;
        if (at + sig_length != length)
                return TLS_FAIL;

        memory_fill(signed_bytes, 0x20, 64);
        memory_copy(signed_bytes + 64, context, 33);
        signed_bytes[97] = 0;
        crypto_sha256_close(address_of copy, hash);
        memory_copy(signed_bytes + 98, hash, 32);

        if (scheme == 0x0403)
        {
                crypto_sha256_of(signed_bytes, sizeof(signed_bytes), hash);
                if (tls_parse_ecdsa_sig(msg + at, sig_length, r, address_of r_length,
                                        s, address_of s_length))
                        return TLS_FAIL;
                if (tls->leaf_curve != 1)
                        return TLS_FAIL;
                return crypto_ecdsa_p256(hash, 32, r, r_length, s, s_length,
                                         tls->leaf_qx + 16, tls->leaf_qy + 16)
                           ? TLS_OK
                           : TLS_FAIL;
        }

        if (scheme == 0x0503)
        {
                p8 hash384[48];

                crypto_sha384(signed_bytes, sizeof(signed_bytes), hash384);
                if (tls_parse_ecdsa_sig(msg + at, sig_length, r, address_of r_length,
                                        s, address_of s_length))
                        return TLS_FAIL;
                if (tls->leaf_curve != 2)
                        return TLS_FAIL;
                return crypto_ecdsa_p384(hash384, 48, r, r_length, s, s_length,
                                         tls->leaf_qx, tls->leaf_qy)
                           ? TLS_OK
                           : TLS_FAIL;
        }

        if (scheme == 0x0804)
        {
                if (tls->leaf_curve != 3 || !tls->leaf_n_length)
                        return TLS_FAIL;
                return crypto_rsa_pss_sha256(tls->leaf_n, tls->leaf_n_length,
                                             tls->leaf_e, msg + at, sig_length,
                                             signed_bytes, sizeof(signed_bytes))
                           ? TLS_OK
                           : TLS_FAIL;
        }

        return TLS_FAIL;
}

#define TLS_SERVER_FLIGHT_EE 0
#define TLS_SERVER_FLIGHT_CERTIFICATE 1
#define TLS_SERVER_FLIGHT_CERT_VERIFY 2
#define TLS_SERVER_FLIGHT_FINISHED 3
#define TLS_SERVER_FLIGHT_COMPLETE 4

/* This client offers neither PSK nor client authentication, so the server
   flight has exactly one legal shape.  Keeping that shape in one transition
   function prevents a duplicate message from overwriting parsed certificate
   state or an early Finished from authenticating an incomplete transcript. */
static COLD bool tls_server_flight_step(p8 address_to state, p8 type)
{
        static const p8 expected[] = {
            TLS_HS_ENCRYPTED_EXTS,
            TLS_HS_CERTIFICATE,
            TLS_HS_CERT_VERIFY,
            TLS_HS_FINISHED,
        };

        if (*state >= TLS_SERVER_FLIGHT_COMPLETE ||
            type != expected[*state])
                return false;

        (*state)++;
        return true;
}

/* RFC 8446 forbids duplicate extensions.  Rescanning every earlier extension
   to find one is quadratic in a block whose size the peer chooses: sixteen
   kilobytes of empty extensions is eight million inner steps, and
   EncryptedExtensions arrives before the certificate is checked, so anyone
   who can terminate the key exchange can spend it.  The kinds already seen go
   in a bitmap of the sixteen-bit space instead and the walk is one pass.  The
   nested scan's own framing tests were unreachable -- it only ever visited
   offsets this walk had already validated and placed -- so the predicate is
   the same one. */
static COLD bool tls_encrypted_extensions_valid(p8 address_to body,
                                           positive length)
{
        p8 seen[8192];
        positive at = 2;

        if (length < 2 || network_load_16(body) != length - 2)
                return false;

        memory_fill(seen, 0, sizeof seen);

        while (at < length)
        {
                p16 kind;
                p16 size;

                if (length - at < 4)
                        return false;

                kind = network_load_16(body + at);
                size = network_load_16(body + at + 2);

                if ((positive)size > length - at - 4)
                        return false;

                if (seen[kind >> 3] & (p8)(1u << (kind & 7)))
                        return false;
                seen[kind >> 3] |= (p8)(1u << (kind & 7));

                at += 4 + size;
        }

        return at == length;
}

static COLD bool tls_new_session_ticket_valid(p8 address_to body,
                                         positive length)
{
        positive at = 8;
        positive nonce_length;
        positive ticket_length;

        if (length < 13)
                return false;

        nonce_length = body[at++];
        if (nonce_length > length - at)
                return false;
        at += nonce_length;

        if (length - at < 2)
                return false;
        ticket_length = network_load_16(body + at);
        at += 2;
        if (!ticket_length || ticket_length > length - at)
                return false;
        at += ticket_length;

        return tls_encrypted_extensions_valid(body + at, length - at);
}

/* This client does not resume sessions, but servers commonly send tickets.
   Ignore only complete, well-framed NewSessionTicket messages.  KeyUpdate and
   every other unsupported post-handshake transition fail instead of leaving
   traffic keys or authentication state silently stale. */
static COLD bipolar tls_post_handshake_append(p8 address_to held,
                                         positive address_to held_length,
                                         p8 address_to fragment,
                                         positive length)
{
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
                if (held[at] != TLS_HS_NEW_SESSION_TICKET)
                        return TLS_FAIL;

                body_length = tls_load_24(held + at + 1);
                if (body_length > TLS_HS_MAX - 4)
                        return TLS_FAIL;

                if (body_length > *held_length - at - 4)
                        break;
                if (!tls_new_session_ticket_valid(held + at + 4,
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

static COLD bool tls_post_handshake_valid(p8 address_to messages,
                                     positive length)
{
        p8 held[TLS_HS_MAX];
        positive held_length = 0;
        bool valid;

        valid = tls_post_handshake_append(held, address_of held_length,
                                          messages, length) == TLS_OK &&
                !held_length;
        crypto_forget(held, sizeof held);
        return valid;
}

/* Encrypted server flight: handshake bytes are a stream across records, so
   each plaintext fragment is appended to hs[] and every complete message is
   peeled from the front.  Unlike tls_handshake_one_append, an empty fragment
   is a no-op (no !length guard) and several messages may share one record.
   Leftover bytes after the flight reaches COMPLETE are refused.

   tls may be null: then only framing and tls_server_flight_step run, which is
   how the unit checks exercise the same walk without keys or a peer. */
static COLD bipolar tls_encrypted_flight_append(
    tls_conn address_to tls, p8 address_to hs, positive room,
    positive address_to hs_used, p8 address_to flight,
    p8 address_to fragment, positive length,
    positive address_to messages)
{
        positive msg_at = 0;

        if (*hs_used + length > room)
                return TLS_FAIL;
        memory_copy(hs + *hs_used, fragment, length);
        *hs_used += length;

        while (msg_at + 4 <= *hs_used)
        {
                p8 hs_type = hs[msg_at];
                positive hs_len = tls_load_24(hs + msg_at + 1);

                if (msg_at + 4 + hs_len > *hs_used)
                        break;

                if (!tls_server_flight_step(flight, hs_type))
                        return TLS_FAIL;

                if (tls)
                {
                        if (hs_type == TLS_HS_ENCRYPTED_EXTS)
                        {
                                if (!tls_encrypted_extensions_valid(
                                        hs + msg_at + 4, hs_len))
                                        return TLS_FAIL;
                                tls_transcript_add(tls, hs + msg_at,
                                                   4 + hs_len);
                        }
                        else if (hs_type == TLS_HS_CERTIFICATE)
                        {
                                tls_transcript_add(tls, hs + msg_at,
                                                   4 + hs_len);
                                if (!tls_verify_chain(hs + msg_at + 4, hs_len,
                                                      tls->host, tls))
                                        return TLS_FAIL;
                        }
                        else if (hs_type == TLS_HS_CERT_VERIFY)
                        {
                                if (tls_check_cert_verify(tls, hs + msg_at,
                                                          4 + hs_len))
                                        return TLS_FAIL;
                                tls_transcript_add(tls, hs + msg_at,
                                                   4 + hs_len);
                        }
                        else if (hs_type == TLS_HS_FINISHED)
                        {
                                if (tls_check_finished(tls, hs + msg_at + 4,
                                                       hs_len))
                                        return TLS_FAIL;
                                tls_transcript_add(tls, hs + msg_at,
                                                   4 + hs_len);
                        }
                }

                if (messages)
                        (*messages)++;
                msg_at += 4 + hs_len;
        }

        if (msg_at)
        {
                memory_copy(hs, hs + msg_at, *hs_used - msg_at);
                *hs_used -= msg_at;
        }

        if (*flight == TLS_SERVER_FLIGHT_COMPLETE && *hs_used)
                return TLS_FAIL;
        return TLS_OK;
}

static COLD bipolar tls_handshake(
    tls_conn address_to tls, const network_deadline address_to deadline)
{
        p8 hello[1024];
        p8 record[TLS_RECORD_MAX];
        p8 peer[97];
        p8 shared[48];
        positive hello_length = 0;
        p8 type = 0;
        positive length = 0;
        p8 hs[TLS_HS_MAX];
        positive hs_used = 0;
        p8 flight = TLS_SERVER_FLIGHT_EE;
        bool seen_ccs = false;
        bipolar status = TLS_FAIL;
        positive share_length = 0;
        positive group = 0;

        crypto_sha256_open(address_of tls->transcript);
        tls->receive_start = 0;
        tls->receive_end = 0;
        tls->plain_used = 0;
        tls->closed = false;
        tls->post_handshake_used = 0;
        tls->encrypted = false;
        tls->application = false;

        if (tls_client_hello(tls, hello, sizeof(hello), address_of hello_length))
                goto done;
        tls_transcript_add(tls, hello, hello_length);
        if (tls_send_plain(tls, TLS_CT_HANDSHAKE, hello, hello_length))
                goto done;

        for (;;)
        {
                bipolar assembled;

                if (tls_read_record(tls, address_of type, record,
                                    sizeof(record), address_of length,
                                    deadline))
                        goto done;
                if (type == TLS_CT_CCS)
                {
                        if (!tls_compatibility_ccs_take(address_of seen_ccs))
                                goto done;
                        continue;
                }
                if (type != TLS_CT_HANDSHAKE)
                        goto done;

                assembled = tls_handshake_one_append(
                    hs, sizeof(hs), address_of hs_used, record, length);
                if (assembled == TLS_FAIL)
                        goto done;
                if (assembled == TLS_HANDSHAKE_COMPLETE)
                        break;
        }

        tls_transcript_add(tls, hs, hs_used);
        if (tls_server_hello_keys(hs, hs_used, peer, sizeof peer,
                                  address_of share_length, address_of group))
                goto done;
        hs_used = 0;

        if (group == 0x001d)
        {
                if (share_length != 32 ||
                    !crypto_x25519(shared, tls->x25519_scalar, peer))
                        goto done;
                if (tls_install_handshake_keys(tls, shared, 32))
                        goto done;
        }
        else if (group == 0x0017)
        {
                if (share_length != 65 ||
                    !crypto_ecdh_p256_shared(shared, tls->p256_scalar, peer))
                        goto done;
                if (tls_install_handshake_keys(tls, shared, 32))
                        goto done;
        }
        else if (group == 0x0018)
        {
                if (share_length != 97 ||
                    !crypto_ecdh_p384_shared(shared, tls->p384_scalar, peer))
                        goto done;
                if (tls_install_handshake_keys(tls, shared, 48))
                        goto done;
        }
        else
                goto done;
        crypto_forget(tls->x25519_scalar, sizeof tls->x25519_scalar);
        crypto_forget(tls->p256_scalar, sizeof tls->p256_scalar);
        crypto_forget(tls->p384_scalar, sizeof tls->p384_scalar);
        crypto_forget(shared, sizeof shared);

        while (flight != TLS_SERVER_FLIGHT_COMPLETE)
        {
                if (tls_read_record(tls, address_of type, record, sizeof(record),
                                    address_of length, deadline))
                        goto done;
                if (type == TLS_CT_CCS)
                {
                        if (!tls_compatibility_ccs_take(address_of seen_ccs))
                                goto done;
                        continue;
                }
                if (type != TLS_CT_HANDSHAKE)
                        goto done;
                if (tls_encrypted_flight_append(tls, hs, sizeof(hs),
                                                address_of hs_used,
                                                address_of flight, record,
                                                length, null))
                        goto done;
        }

        tls_derive_app_keys(tls);
        if (tls_send_finished(tls))
                goto done;
        tls_use_app_keys(tls);
        status = TLS_OK;

done:
        crypto_forget(hello, sizeof hello);
        crypto_forget(record, sizeof record);
        crypto_forget(peer, sizeof peer);
        crypto_forget(shared, sizeof shared);
        crypto_forget(hs, sizeof hs);
        crypto_forget(tls->x25519_scalar, sizeof tls->x25519_scalar);
        crypto_forget(tls->p256_scalar, sizeof tls->p256_scalar);
        crypto_forget(tls->p384_scalar, sizeof tls->p384_scalar);
        return status;
}

static COLD bipolar tls_connect(tls_conn address_to tls, bipolar handle,
                           string_address host, bool check_cert)
{
        bipolar status;
        network_deadline deadline;

        memory_fill(tls, 0, TLS_CONN_HEAD);
        tls->handle = handle;
        tls->host = host;
        tls->check_cert = check_cert;
        status = network_deadline_begin(address_of deadline,
                                        TLS_HANDSHAKE_SECONDS, 0)
                     ? tls_handshake(tls, address_of deadline) : TLS_FAIL;
        if (status)
                tls_forget(tls);
        return status;
}

static bipolar tls_write(tls_conn address_to tls, p8 address_to data,
                         positive length)
{
        while (length)
        {
                positive take = min(length, (positive)TLS_PLAINTEXT_MAX);

                if (tls_send_enc(tls, TLS_CT_APP, data, take))
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
   deadline that long starts the first time a wait is needed, so records
   already whole in the buffer are opened without asking the clock. Neither
   renews: tickets, empty records and partial records all spend one budget.
   hold never receives: when the next record is not yet whole it answers
   TLS_AGAIN, so a writer can gather every record already here while the
   spans it holds stay put. */
static bipolar tls_take(tls_conn address_to tls, positive room,
                        p8 address_to address_to span, positive address_to got,
                        const network_deadline address_to deadline,
                        positive seconds, positive nanoseconds, bool hold)
{
        network_deadline patience;
        bool waiting = !seconds && !nanoseconds;
        p8 type = 0;
        p8 address_to inner = null;
        positive length = 0;
        bipolar status;

        if (tls->plain_used)
        {
                positive take = min(room, tls->plain_used);

                address_to span = tls->receive + tls->plain_at;
                tls->plain_at += take;
                tls->plain_used -= take;
                address_to got = take;
                return TLS_OK;
        }

        for (;;)
        {
                if (tls->closed)
                {
                        address_to got = 0;
                        return tls->post_handshake_used ? TLS_FAIL : TLS_OK;
                }
                if (!tls_record_whole(tls))
                {
                        if (hold)
                                return TLS_AGAIN;
                        if (!waiting)
                        {
                                if (!network_deadline_begin(address_of patience,
                                                            seconds,
                                                            nanoseconds))
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
                if (type == TLS_CT_HANDSHAKE)
                {
                        if (tls_post_handshake_append(
                                tls->post_handshake,
                                address_of tls->post_handshake_used,
                                inner, length))
                                return TLS_FAIL;
                        crypto_forget(inner, length);
                        continue;
                }
                if (type != TLS_CT_APP || tls->post_handshake_used)
                        return TLS_FAIL;
                if (!length)
                        continue;
                if (room > length)
                        room = length;
                address_to span = inner;
                address_to got = room;
                tls->plain_at = (positive)(inner - tls->receive) + room;
                tls->plain_used = length - room;
                return TLS_OK;
        }
}

static bipolar tls_borrow(tls_conn address_to tls, positive room,
                          p8 address_to address_to span,
                          positive address_to got, positive seconds,
                          positive nanoseconds)
{
        return tls_take(tls, room, span, got, null, seconds, nanoseconds,
                        false);
}

/* The next application data already whole in the receive buffer, lent
   without receiving; TLS_AGAIN when none is. */
static bipolar tls_lend(tls_conn address_to tls, positive room,
                        p8 address_to address_to span, positive address_to got)
{
        return tls_take(tls, room, span, got, null, 0, 0, true);
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

static bipolar tls_read(tls_conn address_to tls, p8 address_to into,
                        positive room, positive address_to got)
{
        return tls_read_until(tls, into, room, got, null);
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
#define HTTP_HOPS 10
#define HTTP_IDLE_SECONDS 30
#define HTTP_HEAD_SECONDS 30

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

typedef byte_store http_buffer;
#define http_forget(buffer) byte_store_release(buffer)

/*
        http://host[:port][/path] taken apart.

        Everything before the first slash after the authority is the host,
        everything from it is the path, and a missing path is "/". A colon in
        the authority is a port, which is how a test talks to a server on a
        port the kernel picked.
*/
static bipolar http_split_into(string_address url, p8 address_to host, positive room,
                               p16 address_to port, string_address address_to path,
                               bool address_to tls)
{
        string_address at = url;
        positive length;
        p16 selected_port = HTTP_PORT;
        bool selected_tls = false;

        for (string_address scan = url; *scan; scan++)
                if (byte_is_control(*scan) || *scan == ' ' || *scan == '\\')
                        return HTTP_BAD_URL;

        if (!string_compare_max(url, (string_address) "https://", 8))
        {
                selected_tls = true;
                selected_port = HTTP_HTTPS_PORT;
                at = url + 8;
        }
        else if (!string_compare_max(url, (string_address) "http://", 7))
                at = url + 7;

        /* A spelling with an explicit scheme is not a schemeless HTTP URL.
           Treating "gopher://host" as host "gopher" is a parser
           differential: a policy and this client can appear to approve the
           same string while naming different destinations. */
        else
        {
                /* Only a colon ahead of the first '/', '?' or '#' can end a
                   scheme: "host/?next=http://x" is a schemeless URL whose
                   query holds another. */
                string_address scheme = (string_address)memory_first_of(
                    url, ':', string_span_without_set(url, "/?#"));

                if (scheme && scheme[1] == '/' && scheme[2] == '/')
                        return HTTP_BAD_URL;
        }

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

        /* HTTP Host and DNS have narrower syntax than an arbitrary URI
           reg-name: letters, digits and the unreserved '-', '.' and '_'
           (DNS carries the underscore, and wget and curl reach such hosts).
           Validate before touching the caller's output so a rejected URL
           cannot leave a plausible partial destination there. */
        for (positive byte = 0; byte < length; byte++)
                if (!byte_is_alnum(at[byte]) && at[byte] != '-' &&
                    at[byte] != '.' && at[byte] != '_')
                        return HTTP_BAD_URL;

        if (!length)
                return HTTP_BAD_URL;

        string_address host_at = at;
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
        memory_copy(host, host_at, length);
        host[length] = end;
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
        if (!length)
        {
                if (room < 2)
                        return HTTP_BAD_URL;
                into[0] = '/';
                into[1] = end;
                return HTTP_OK;
        }

        root = path[0] == '?';
        if (path[0] != '/' && !root)
                return HTTP_BAD_URL;
        if (length > room || root >= room - length)
                return HTTP_BAD_URL;

        if (root)
                *into++ = '/';
        memory_copy_end(into, path, length);
        return HTTP_OK;
}

//      Where the header ends: the blank line, in either spelling.
static PURE bipolar http_header_end(p8 address_to bytes, positive size)
{
        //      Both spellings are looked for at once and the one that
        //      starts first wins. A header block ending \r\n\r\n contains no
        //      bare \n\n -- there is a \r between them -- so the two can never
        //      both match at the same place.
        //
        //      Once the full spelling is found at some offset, only a bare
        //      one starting before it can win, so the second search stops
        //      there rather than walking the rest of the buffer. A block
        //      ending \r\n\r\n holds no \n\n at all, so searching the whole
        //      of it was a scan of every byte after the answer -- on every
        //      read, and once more per informational response ahead of the
        //      final one, which is what a peer sending 1xx after 1xx one
        //      byte at a time was buying.
        p8 address_to full = (p8 address_to)memory_search(bytes, size, "\r\n\r\n", 4);
        positive span = full ? (positive)(full - bytes) + 1 : size;
        p8 address_to bare = (p8 address_to)memory_search(bytes, span, "\n\n", 2);

        if (full && (!bare || full < bare))
                return (bipolar)(full - bytes) + 4;

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
   here. */
static bool http_token_byte(p8 byte)
{
        return byte_is_alnum(byte) ||
               memory_first_of("!#$%&'*+-.^_`|~", byte, 15);
}

static bool http_header_block_valid(p8 address_to bytes, positive size)
{
        positive at = 0;
        bool status = true;

        while (at < size)
        {
                positive line = at;
                positive stop = at + memory_span_without_byte(
                    bytes + at, '\n', size - at);
                positive colon = line;

                if (stop == size)
                        return false;
                at = stop + 1;
                if (stop > line && bytes[stop - 1] == '\r')
                        stop--;

                if (stop == line)
                        return !status && at == size;

                if (status)
                {
                        status = false;
                        for (positive byte = line; byte < stop; byte++)
                                if ((bytes[byte] < 0x20 && bytes[byte] != '\t') ||
                                    bytes[byte] == 0x7f)
                                        return false;
                        continue;
                }

                /* A field begins with a token.  This also rejects obs-fold,
                   whitespace before the colon, and an empty field name. */
                while (colon < stop && http_token_byte(bytes[colon]))
                        colon++;
                if (colon == line || colon == stop || bytes[colon] != ':')
                        return false;

                for (positive byte = colon + 1; byte < stop; byte++)
                        if ((bytes[byte] < 0x20 && bytes[byte] != '\t') ||
                            bytes[byte] == 0x7f)
                                return false;
        }

        return false;
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

                        found = (string_address)(bytes + from);
                        found_length = stop - from;
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
                                bool closed = false;

                                at++;
                                while (at < stop)
                                {
                                        p8 byte = *at++;

                                        if (byte == '"')
                                        {
                                                closed = true;
                                                break;
                                        }
                                        if (byte == '\\')
                                        {
                                                if (at == stop)
                                                        return false;
                                                byte = *at++;
                                        }
                                        if (byte_is_control(byte) &&
                                            byte != '\t')
                                                return false;
                                }
                                if (!closed)
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
        if (number < stop)
        {
                if (!http_chunk_extensions_valid(number, stop))
                        return HTTP_MALFORMED;
        }

        address_to chunk_length = parsed;
        return HTTP_OK;
}

/* Return one for the terminating blank line, zero for a valid trailer field,
   and a negative status for malformed framing. */
static bipolar http_trailer_line(p8 address_to line, positive line_length)
{
        positive stop = line_length;
        positive colon = 0;

        if (!stop || line[stop - 1] != '\n')
                return HTTP_MALFORMED;
        stop--;
        if (stop && line[stop - 1] == '\r')
                stop--;
        if (!stop)
                return 1;

        while (colon < stop && line[colon] != ':')
        {
                if (!http_token_byte(line[colon]))
                        return HTTP_MALFORMED;
                colon++;
        }
        if (!colon || colon == stop)
                return HTTP_MALFORMED;

        for (positive at = colon + 1; at < stop; at++)
                if (byte_is_control(line[at]) && line[at] != '\t')
                        return HTTP_MALFORMED;

        return 0;
}

static bipolar http_unchunk(p8 address_to bytes, positive size);

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

static bool http_response_is_redirect(b32 code)
{
        return code == 300 || code == 301 || code == 302 || code == 303 ||
               code == 307 || code == 308;
}

static bool http_response_is_success(b32 code)
{
        return code >= 200 && code < 300;
}

/* Status and body framing have one interpretation in both clients.  This
   rejects duplicate or conflicting declarations before either the buffered
   or streaming body path acts on them. */
static bipolar http_response_framing_from(p8 address_to bytes, positive size,
                                          positive address_to resume,
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
                bool transfer_repeated = false;
                bool length_repeated = false;
                positive value_length = 0;
                positive content_length_size = 0;
                string_address transfer;
                string_address content_length;
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

                header = http_header_end(bytes + at, scan - at);
                if (header < 0)
                        return scan == HTTP_HEAD_MAX ? HTTP_MALFORMED
                                                     : HTTP_NO_REPLY;
                if (!http_header_block_valid(bytes + at, (positive)header))
                        return HTTP_MALFORMED;

                transfer = http_header(
                    bytes + at, (positive)header,
                    (string_address)"transfer-encoding",
                    address_of value_length, address_of transfer_repeated);
                content_length = http_header(
                    bytes + at, (positive)header,
                    (string_address)"content-length",
                    address_of content_length_size, address_of length_repeated);
                if (transfer_repeated || length_repeated ||
                    (transfer && content_length))
                        return HTTP_MALFORMED;

                if (transfer)
                {
                        if (value_length < 7 ||
                            memory_compare_ascii_case(transfer, "chunked", 7) ||
                            string_span_max(transfer + 7, value_length - 7,
                                            string_set_blanks) !=
                                value_length - 7)
                                return HTTP_MALFORMED;
                        response->body_kind = HTTP_BODY_CHUNKED;
                }
                else if (content_length)
                {
                        string_address cursor = content_length;
                        positive digits;

                        if (!string_digits_checked(
                                address_of cursor, 10,
                                address_of response->body_length))
                                return HTTP_MALFORMED;
                        digits = (positive)(cursor - content_length);
                        digits += string_span_max(
                            cursor, content_length_size - digits,
                            string_set_blanks);
                        if (digits != content_length_size)
                                return HTTP_MALFORMED;
                        response->body_kind = HTTP_BODY_LENGTH;
                }

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
                        continue;
                }

                if (http_response_is_redirect(response->code))
                {
                        bool repeated = false;

                        response->location = http_header(
                            bytes + at, (positive)header,
                            (string_address)"location",
                            address_of response->location_length,
                            address_of repeated);
                        if (repeated)
                                return HTTP_MALFORMED;
                        /* Controls, NUL included, would silently cut the
                           Location the string calls later copy. */
                        for (positive byte = 0;
                             byte < response->location_length; byte++)
                                if (byte_is_control((p8)response->location[byte]) &&
                                    response->location[byte] != '\t')
                                        return HTTP_MALFORMED;
                }

                address_to header_length = at + (positive)header;
                return HTTP_OK;
        }
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

static bipolar http_stream_open(p32 host, p16 port)
{
        socket_address_internet where = {
            .family = AF_INET, .port = network_order_16(port),
            .host = network_order_32(host)};
        bipolar handle = socket_new(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);

        if (handle < 0)
                return HTTP_NO_ROUTE;
        if (!network_stream_timeout(handle, HTTP_IDLE_SECONDS, 0) ||
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

static bipolar http_link_open(http_link address_to link, p32 ip, p16 port,
                              string_address host, bool tls, bool check_cert)
{
        //      The session's buffers are tls_connect's to leave alone.
        memory_fill(link, 0, __builtin_offsetof(http_link, session));
        link->handle = http_stream_open(ip, port);
        if (link->handle < 0)
                return link->handle;
        if (tls)
        {
                if (tls_connect(address_of link->session, link->handle, host,
                                check_cert))
                {
                        http_link_close(link);
                        return HTTP_TLS;
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
        return network_stream_send_all(link->handle, data, length)
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

/* The request built and put on the wire. Both clients send the same GET and
   differ only in what they call themselves and which minor version they
   claim, so the two words they disagree about are arguments and the wire
   format is not written twice. */
static bipolar http_send_get(http_link address_to link, string_address host,
                             p16 port, string_address path, bool tls,
                             p8 version_minor, string_address agent)
{
        p8 request[2048];
        positive used = 0;
        bipolar built = http_get_request(
            request, sizeof request, host, port, path, tls, version_minor,
            agent, address_of used);
        bipolar status = built ? built : http_link_write(link, request, used);

        crypto_forget(request, sizeof request);
        return status;
}

static bipolar http_response_head(
    http_link address_to link, p8 address_to head, positive room,
    positive address_to used, positive address_to header,
    http_response address_to response, positive seconds, positive nanoseconds,
    bool incomplete_is_malformed)
{
        network_deadline deadline;
        /* Carried across the reads: the head is walked once in total rather
           than from byte zero on every read, which is what a peer sending
           informational responses one byte at a time was buying. */
        positive resume = 0;

        address_to used = 0;
        if (!network_deadline_begin(address_of deadline, seconds, nanoseconds))
                return HTTP_NO_REPLY;

        for (;;)
        {
                positive got = 0;
                bipolar status = http_response_framing_from(
                    head, address_to used, address_of resume, header,
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
        return code == 204 || code == 205 || code == 304;
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
} http_body;

static bipolar http_body_read(http_body address_to body, p8 address_to into,
                              positive room, positive address_to got);
static bipolar http_copy(http_body address_to body, bipolar dest, positive want,
                         bool exact);
static bipolar http_copy_chunked(http_body address_to body, bipolar dest);

/* A final response's body, by the framing its head declared.  A memory store
   refuses a close-delimited body beyond its limit as it arrives, so an EOF
   that lands exactly on the limit is still a complete body. */
static bipolar http_copy_body(http_body address_to body, bipolar dest,
                              const http_response address_to response)
{
        bool exact = response->body_kind == HTTP_BODY_LENGTH;

        if (response->body_kind == HTTP_BODY_CHUNKED)
                return http_copy_chunked(body, dest);
        return http_copy(body, dest,
                         exact ? response->body_length : positive_max, exact);
}

static bipolar http_body_read(http_body address_to body, p8 address_to into,
                              positive room, positive address_to got)
{
        if (body->stash_used)
        {
                positive take = body->stash_used;
                if (take > room)
                        take = room;
                memory_copy(into, body->stash, take);
                body->stash += take;
                body->stash_used -= take;
                address_to got = take;
                return HTTP_OK;
        }

        if (body->link)
        {
                network_deadline deadline;
                positive seconds = body->read_seconds;
                positive nanoseconds = body->read_nanoseconds;

                if (!seconds && !nanoseconds)
                        seconds = HTTP_IDLE_SECONDS;
                if (!network_deadline_begin(address_of deadline, seconds,
                                            nanoseconds))
                        return HTTP_NO_REPLY;
                return http_link_read_until(body->link, into, room, got,
                                            address_of deadline);
        }
        *got = 0;
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
                positive seconds = body->read_seconds;
                positive nanoseconds = body->read_nanoseconds;

                if (!seconds && !nanoseconds)
                        seconds = HTTP_IDLE_SECONDS;
                return tls_borrow(address_of body->link->session, room, data,
                                  got, seconds, nanoseconds)
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

/* Write every span, resuming after partial progress; as with
   system_write_all, a zero or negative result ends it. */
static bool http_write_spans(bipolar dest, http_span address_to spans,
                             positive count, positive total)
{
        while (total)
        {
                bipolar wrote = (bipolar)system_call_3(
                    syscall(writev), (positive)dest, (positive)spans, count);

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
                                bipolar lent = tls_lend(
                                    address_of body->link->session,
                                    want - total, address_of more,
                                    address_of more_got);

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
   complete memory responses have their existing whole-response bound. */
static bipolar http_line(http_body address_to body, positive limit,
                          p8 address_to address_to line,
                          positive address_to length)
{
        positive used = body->stash_used;
        positive span = memory_span_without_byte(body->stash, '\n', used);
        if (span < used && span < limit)
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
        memory_copy(scratch, body->stash, used);
        body->stash_used = 0;
        while (used < limit)
        {
                positive got = 0;
                if (http_body_read(body, scratch + used, limit - used,
                                   address_of got))
                        return HTTP_NO_REPLY;
                if (!got)
                        return HTTP_MALFORMED;
                span = memory_span_without_byte(scratch + used, '\n', got);
                if (span < got)
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

static bipolar http_body_byte(http_body address_to body, p8 address_to byte)
{
        positive got = 0;
        return http_body_read(body, byte, 1, address_of got) || !got
                   ? HTTP_MALFORMED : HTTP_OK;
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

        for (;;)
        {
                positive line_length = 0;
                positive size = 0;
                p8 delimiter;

                if (http_line(body, 127, address_of line,
                              address_of line_length))
                        return HTTP_MALFORMED;
                if (http_chunk_line(line, line_length, address_of size))
                        return HTTP_MALFORMED;
                if (!size)
                        return http_copy_trailers(body);
                {
                        bipolar copied = http_copy(body, dest, size, true);

                        if (copied)
                                return copied;
                }
                if (http_body_byte(body, address_of delimiter))
                        return HTTP_MALFORMED;
                if (delimiter == '\n')
                        continue;
                if (delimiter != '\r' ||
                    http_body_byte(body, address_of delimiter) ||
                    delimiter != '\n')
                        return HTTP_MALFORMED;
        }
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

                if (byte_is_control(byte) || byte == 0x7f ||
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
                if (kind == HTTP_REQUEST_HOST &&
                    !byte_is_alnum(byte) && byte != '-' && byte != '.' &&
                    byte != '_')
                        return false;
        }

        return true;
}


static bipolar http_get_request(p8 address_to request, positive room,
                                string_address host, p16 port,
                                string_address path, bool tls,
                                p8 version_minor, string_address agent,
                                positive address_to used)
{
        p8 target[HTTP_URL_MAX];
        p8 port_text[7] = {':'};
        byte_store out = {request, room, 0};
        bool named_port = (tls && port != HTTP_HTTPS_PORT) ||
                          (!tls && port != HTTP_PORT);
        bool ok;

        if (!http_request_component_valid(host, HTTP_REQUEST_HOST) ||
            !http_request_component_valid(path, HTTP_REQUEST_TARGET) ||
            !http_request_component_valid(agent, HTTP_REQUEST_FIELD) ||
            http_origin_form(path, target, sizeof target) ||
            version_minor < '0' || version_minor > '9')
                return HTTP_BAD_URL;

        ok = byte_store_append_exact(address_of out, "GET ", 4);
        ok &= byte_store_append_exact(address_of out, target, string_length(target));
        ok &= byte_store_append_exact(address_of out, " HTTP/1.", 8);
        ok &= byte_store_append_exact(address_of out, address_of version_minor, 1);
        ok &= byte_store_append_exact(address_of out, "\r\nHost: ", 8);
        ok &= byte_store_append_exact(address_of out, host, string_length(host));
        ok &= byte_store_append_exact(
            address_of out, port_text,
            named_port ? 1 + positive_into(port_text + 1, port) : 0);
        ok &= byte_store_append_exact(address_of out, "\r\nUser-Agent: ", 14);
        ok &= byte_store_append_exact(address_of out, agent, string_length(agent));
        ok &= byte_store_append_exact(
            address_of out, "\r\nAccept: */*\r\nConnection: close\r\n\r\n", 36);
        if (!ok)
                return HTTP_BAD_URL;
        address_to used = out.used;
        return HTTP_OK;
}

static bipolar http_put_url(p8 address_to into, positive room, bool tls,
                            string_address host, p16 port, string_address path)
{
        p8 port_text[7] = {':'};
        //      The last byte of the room is kept for the terminator.
        byte_store out = {into, room ? room - 1 : 0, 0};
        bool named_port = (tls && port != HTTP_HTTPS_PORT) ||
                          (!tls && port != HTTP_PORT);
        bool ok = room != 0;

        if (!string_get(path))
                path = (string_address) "/";
        ok &= byte_store_append_exact(address_of out, tls ? "https://" : "http://",
                                      tls ? 8 : 7);
        ok &= byte_store_append_exact(address_of out, host, string_length(host));
        ok &= byte_store_append_exact(
            address_of out, port_text,
            named_port ? 1 + positive_into(port_text + 1, port) : 0);
        ok &= byte_store_append_exact(address_of out, path, string_length(path));
        if (!ok)
                return HTTP_BAD_URL;
        into[out.used] = end;
        return HTTP_OK;
}

static bipolar http_absolutize(bool tls, string_address host, p16 port,
                               string_address path, string_address location,
                               p8 address_to into, positive room)
{
        p8 kept[HTTP_URL_MAX];
        p8 base[HTTP_URL_MAX];
        positive length = string_length(location);
        string_address hash;

        if (length >= sizeof kept ||
            http_origin_form(path, base, sizeof base))
                return HTTP_BAD_URL;
        memory_copy(kept, location, length + 1);
        hash = string_first_of(kept, '#');
        if (hash)
                hash[0] = end;

        /* A fragment-only reference identifies the current resource.  The
           fragment itself was removed above; retain both path and query. */
        if (!kept[0])
                return http_put_url(into, room, tls, host, port, base);

        if (!string_compare_max(kept, (string_address) "https://", 8) ||
            !string_compare_max(kept, (string_address) "http://", 7))
        {
                if (string_length(kept) >= room)
                        return HTTP_BAD_URL;
                string_copy(into, kept);
                return HTTP_OK;
        }

        if (kept[0] == '/' && kept[1] == '/')
        {
                p8 address_to at = into;
                positive scheme_length = tls ? 6 : 5;
                positive rest = string_length(kept);

                if (scheme_length + rest + 1 > room)
                        return HTTP_BAD_URL;
                at = memory_copy_apart_end(at, tls ? "https:" : "http:",
                                           scheme_length);
                at = memory_copy_apart_end(at, kept, rest);
                at[0] = end;
                return HTTP_OK;
        }

        if (kept[0] == '/')
                return http_put_url(into, room, tls, host, port, kept);

        if (kept[0] == '?')
        {
                p8 merged[HTTP_URL_MAX];
                string_address query = string_first_of(base, '?');
                positive used = query ? (positive)(query - base)
                                      : string_length(base);
                positive rest = string_length(kept);

                if (used + rest + 1 > sizeof merged)
                        return HTTP_BAD_URL;
                memory_copy(merged, base, used);
                memory_copy_apart_end(merged + used, kept, rest);
                return http_put_url(into, room, tls, host, port, merged);
        }

        {
                p8 merged[HTTP_URL_MAX];
                string_address query = string_first_of(base, '?');
                string_address slash;
                positive dir;
                positive used = 0;
                positive rest = string_length(kept);

                if (query)
                        query[0] = end;
                slash = string_last_of(base, '/');
                dir = slash ? (positive)(slash - base) + 1 : 1;
                if (dir >= sizeof merged)
                        return HTTP_BAD_URL;
                memory_copy(merged, base, dir);
                used = dir;
                if (used + rest + 1 > sizeof merged)
                        return HTTP_BAD_URL;
                memory_copy(merged + used, kept, rest);
                used += rest;
                merged[used] = end;
                return http_put_url(into, room, tls, host, port, merged);
        }
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
        bipolar server = string_to_host(host);
        p32 ip = 0;

        if (server >= 0)
                return (p32)server;
        if (dns_resolve_any((string_address) "/etc/resolv.conf", host, address_of ip,
                            3) != DNS_OK)
                return 0;
        return ip;
}

static fn http_url_leaf(string_address path, p8 address_to into, positive room)
{
        p8 target[HTTP_URL_MAX];
        string_address query;
        string_address slash;

        if (!room)
                return;
        if (http_origin_form(path, target, sizeof target))
        {
                into[0] = end;
                return;
        }

        query = string_first_of(target, '?');
        if (query)
                query[0] = end;

        slash = string_last_of(target, '/');
        path = slash ? slash + 1 : target;
        if (!string_get(path))
                path = (string_address) "index.html";
        string_copy_max_end(into, path, room - 1);
}

/*
        The two clients, written as the five words they disagree about.

        Everything under this -- the connection, the request, the head, the
        framing and the body -- is one machine, and what is left is manners.
        fetch is the plaintext slurp: dawning over HTTP/1.0, no TLS at all,
        no redirect followed, and a head that stops early is a lie about the
        framing, because a plaintext peer that meant to answer had no reason
        to hang up mid-sentence. wget is the streaming download: Wget over
        HTTP/1.1, TLS, ten hops and never a step back down to plain, and a
        head that stops early is a peer that went away.
*/
typedef struct
{
        string_address agent;
        p8 version_minor;
        bool follow;
        bool allow_tls;
        bool head_cut_is_malformed;
} http_manners;

static const http_manners http_manners_fetch = {
    (string_address)"dawning", '0', false, false, true};
static const http_manners http_manners_wget = {
    (string_address)"Wget", '1', true, true, false};

/* One URL fetched under one client's manners, into exactly one sink: a
   descriptor, or a buffer filled here and handed over only on success, so a
   refused response leaves whatever the caller already held. A memory body is
   bounded by HTTP_FETCH_MAX both by what Content-Length claims and by what
   actually arrives. */
static bipolar http_run(string_address start, const http_manners address_to how,
                        bool check_cert, bipolar dest,
                        http_buffer address_to into, b32 address_to code)
{
        p8 url[HTTP_URL_MAX];
        http_buffer whole = {0};
        positive hop;
        bool secure = false;
        bipolar status;

        if (string_length(start) >= sizeof url)
                return HTTP_BAD_URL;
        string_copy(url, start);

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

                status = http_split_into(url, host, sizeof host, address_of port,
                                         address_of path, address_of tls);
                if (!status && tls && !how->allow_tls)
                        status = HTTP_TLS;
                if (!status && !http_transport_allowed(address_of secure, tls))
                        status = HTTP_DOWNGRADE;
                if (!status && !(ip = http_lookup(host)))
                        status = HTTP_NO_HOST;
                if (status)
                        goto done;

                status = http_link_open(address_of link, ip, port, host, tls,
                                        check_cert);
                if (status)
                        goto done;

                status = http_send_get(address_of link, host, port, path, tls,
                                       how->version_minor, how->agent);
                if (!status)
                        status = http_response_head(
                            address_of link, head, sizeof head, address_of used,
                            address_of header, address_of response,
                            HTTP_HEAD_SECONDS, 0, how->head_cut_is_malformed);
                if (!status && code)
                        address_to code = response.code;

                if (!status && how->follow &&
                    http_response_is_redirect(response.code))
                {
                        status = !response.location_length ? HTTP_MALFORMED
                                 : response.location_length >= sizeof next
                                     ? HTTP_BAD_URL : HTTP_OK;
                        if (!status)
                        {
                                p8 placed[HTTP_URL_MAX];

                                memory_copy(placed, response.location,
                                            response.location_length);
                                placed[response.location_length] = end;
                                status = http_absolutize(tls, host, port, path, placed,
                                                         next, sizeof next);
                        }
                        http_link_close(address_of link);
                        if (status)
                                goto done;
                        //      http_absolutize terminates inside next, which is
                        //      exactly as large as url.
                        string_copy(url, next);
                        continue;
                }

                if (!status && !http_response_is_success(response.code))
                        status = HTTP_STATUS;
                else if (!status && !http_response_has_no_body(response.code))
                {
                        http_body body = {
                            .link = address_of link,
                            .stash = head + header,
                            .stash_used = used - header,
                            .scratch = head,
                            .store = into ? address_of whole : null,
                            .store_limit = HTTP_FETCH_MAX,
                        };

                        status = into &&
                                         response.body_kind == HTTP_BODY_LENGTH &&
                                         response.body_length > HTTP_FETCH_MAX
                                     ? HTTP_MALFORMED
                                     : http_copy_body(address_of body, dest,
                                                      address_of response);
                }

                http_link_close(address_of link);
                goto done;
        }

        //      Ten hops and the last one still pointed somewhere else.
        status = HTTP_REDIRECTS;

done:
        if (into && !status)
        {
                if (whole.bytes)
                        whole.bytes[whole.used] = end;
                byte_store_release(into);
                address_to into = whole;
                memory_fill(address_of whole, 0, sizeof whole);
        }
        byte_store_release(address_of whole);

        return status;
}

//      fetch: no TLS, no redirect followed, and the body only once it is whole.
static bipolar http_get(string_address url, http_buffer address_to body,
                        b32 address_to code)
{
        return http_run(url, address_of http_manners_fetch, false, -1, body,
                        code);
}

//      wget: TLS, redirects, and the body written as it arrives.
static bipolar http_fetch_to(string_address start, bipolar dest, bool check_cert,
                             b32 address_to code)
{
        return http_run(start, address_of http_manners_wget, check_cert, dest,
                        null, code);
}

#endif // STANDARD_MODERN_C_NET_HTTP
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
        client does not have yet, which some clients receive anyway and some
        do not. Setting the flag asks for a broadcast reply, which always
        arrives. qemu's user-mode network broadcasts regardless and echoes the
        flag back as zero, so this is the one part of the exchange that the
        boot lane structurally cannot check -- it is set because a real
        network needs it, not because a test proved it here.

        ARP conflict probing is not implemented. The network watcher schedules
        dhcp_reacquire at half the lease lifetime.
*/

#define DHCP_CLIENT_PORT 68
#define DHCP_SERVER_PORT 67

#define DHCP_HEAD 236
#define DHCP_COOKIE 0x63825363

#define DHCP_DISCOVER 1
#define DHCP_OFFER 2
#define DHCP_REQUEST 3
#define DHCP_ACK 5
#define DHCP_NAK 6

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

        if (wanted)
        {
                into[at++] = DHCP_OPTION_REQUESTED;
                into[at++] = 4;
                network_store_32(into + at, wanted);
                at += 4;
        }

        if (server)
        {
                into[at++] = DHCP_OPTION_SERVER;
                into[at++] = 4;
                network_store_32(into + at, server);
                at += 4;
        }

        //      What we would like to be told, which a server may ignore, and
        //      the end. Short packets are dropped by some servers and by some
        //      switches, so the 261 bytes at most written here are padded to
        //      the length everything accepts.
        static const p8 ask[] = {DHCP_OPTION_ASK, 3, DHCP_OPTION_MASK,
                                 DHCP_OPTION_ROUTER, DHCP_OPTION_DNS,
                                 DHCP_OPTION_END};

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
        positive at = 0;

        while (at < size)
        {
                p8 option = region[at];
                p8 length;

                if (option == DHCP_OPTION_END)
                {
                        /* Bytes after END are padding, not a second hidden
                           option stream. Require canonical PAD bytes so this
                           parser cannot disagree with a middlebox or another
                           client which keeps scanning after option 255. */
                        at++;
                        return memory_span_byte(region + at, DHCP_OPTION_PAD,
                                                size - at) == size - at
                                   ? 0
                                   : -1;
                }

                if (option == DHCP_OPTION_PAD)
                {
                        at++;
                        continue;
                }

                if (at + 1 >= size)
                        return -1;

                length = region[at + 1];

                if (at + 2 + length > size)
                        return -1;

                if (option == DHCP_OPTION_OVERLOAD)
                {
                        p8 value;

                        /* Option overload is legal exactly once, only in the
                           primary options area, with its one-byte value in
                           the RFC-defined 1..3 domain. Accepting malformed or
                           repeated controls makes file/sname interpretation
                           depend on which occurrence a parser chooses. */
                        if (!overload || address_to overload || length != 1)
                                return -1;
                        value = region[at + 2];
                        if (!value || value > 3)
                                return -1;
                        address_to overload = value;
                }

                for (positive taken = 0; taken < length &&
                                         gathered[option].length + taken < 4; taken++)
                        gathered[option].first[gathered[option].length + taken] =
                                region[at + 2 + taken];
                gathered[option].length += length;

                at += 2 + length;
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

static COLD bool dhcp_lease_usable(const dhcp_lease address_to lease)
{
        return lease->address && lease->server && lease->seconds &&
               dhcp_mask_valid(lease->mask);
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
        //      Merging keeps a usable lease usable: only nonzero fields and a
        //      mask dhcp_read already validated replace anything.
        return dhcp_lease_timers(lease);
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
            socket_bind((b32)handle, address_of mine, sizeof mine) < 0)
        {
                socket_close((b32)handle);
                return -1;
        }

        return handle;
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
                         const network_deadline address_to deadline)
{
        bipolar got;

        for (;;)
        {
                got = network_wait_readable_until(handle, deadline);

                if (got <= 0)
                        return false;

                socket_address_internet peer;
                p32 peer_size = sizeof peer;

                memory_fill(address_of peer, 0, sizeof peer);
                got = socket_receive((b32)handle, packet, room, MSG_TRUNC,
                                     address_of peer, address_of peer_size);

                if (got == NETWORK_INTERRUPTED)
                        continue;
                if (got < 0)
                        return false;
                if (!got)
                        continue;

                if ((positive)got <= room &&
                    dhcp_peer_matches(address_of peer, peer_size,
                                      expected_peer, any_peer_host) &&
                    dhcp_read(packet, (positive)got, transaction, hardware,
                              lease, kind) >= 0)
                {
                        if (accepted_peer)
                                *accepted_peer = peer;
                        return true;
                }
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

        while (dhcp_receive(handle, packet, room, transaction, hardware,
                            address_of answer, address_of kind, peer,
                            rebinding, null, deadline))
                if (dhcp_reacquisition_answer_matches(kind, address_of answer,
                                                      lease, rebinding) &&
                    (kind == DHCP_NAK ||
                     dhcp_lease_acknowledge(lease, address_of answer)))
                        return kind == DHCP_ACK ? DHCP_OK : DHCP_REFUSED;

        return DHCP_NO_OFFER;
}

/*
        The exchange, with a schedule rather than a single try.

        A server that is slow, or a link that has only just come up and whose
        switch port is still learning, is the ordinary case at boot rather
        than the exception. A link brought up a moment ago has not finished
        negotiating, the first DISCOVERs go into that gap and are simply lost,
        and what decides when a machine is on the network is how soon after
        carrier the next one goes out. So it asks four times a second for the
        first three seconds and backs off after that.

        A note on where the time went, because it was not where it looked.

        A boot reached carrier at three seconds and had no address until
        fifteen. Counting showed the whole exchange was one send, one poll,
        one offer and one poll for the acknowledgement -- perhaps two seconds
        of work -- inside fourteen seconds of wall clock, and a direct test
        showed ppoll honouring its timeout to the millisecond.

        It was getrandom. With no flags it waits for the kernel's entropy pool
        to be initialised, and early in boot it is not. Twelve seconds of a
        boot were spent there, before a single packet moved, asking for a
        number to put in a header. That wait used to be avoided with
        GRND_NONBLOCK and the kernel's explicit early-boot insecure stream.
        DHCP now prefers the initialized CSPRNG without waiting, then blocks
        on that same CSPRNG rather than drawing a guessable xid; if neither
        path can fill the transaction id, acquisition fails before I/O.

        The wait is short now: the first blocking getrandom sets the kernel
        generating entropy itself, and crng is ready about 0.85 s later. On
        KVM with no RDRAND/RDSEED and no virtio-rng (kernel 7.2) the lease
        came 0.6-0.95 s later than with GRND_INSECURE, and never failed;
        with RDRAND or virtio-rng the pool is ready before DHCP asks and
        nothing waits.
*/
static bipolar dhcp_ask(string_address device, p8 address_to hardware,
                        dhcp_lease address_to lease)
{
        p8 packet[1024];
        p32 transaction;
        bipolar handle;
        bipolar status;
        positive length;
        positive attempt;
        positive wait;

        memory_fill(lease, 0, sizeof(dhcp_lease));
        if (!dhcp_transaction_early(address_of transaction))
                return DHCP_NO_RANDOM;
        handle = dhcp_open(device, HOST_ANY, true);

        if (handle < 0)
                return DHCP_NO_SOCKET;

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

                /*
                        A quarter second apart while it matters.

                        The link comes up about three seconds into a boot and
                        the DISCOVERs before that are lost, so what decides
                        when a machine is on the network is how soon after
                        carrier the next one goes out. At one second that was
                        the whole of the remaining delay; at a quarter it is
                        within noise of the card itself.

                        Twelve quick tries covers three seconds of that, and
                        the backoff after it is for a network with no server
                        on it, which should not be broadcast at forever.
                */
                wait = attempt < 12 ? 1 : (attempt - 11) * 8;
                length = dhcp_build(packet, sizeof packet, DHCP_DISCOVER,
                                    transaction, hardware, 0, 0, 0, true);

                //      A failed send is most likely no route yet. An
                //      unconnected UDP socket routes every send afresh, so
                //      carrier can appear underneath it; the attempt still
                //      waits out its interval rather than spending the whole
                //      schedule in one instant.
                (void)socket_send((b32)handle, packet, length, 0,
                                  address_of where, sizeof where);

                if (!network_deadline_begin(
                        address_of deadline, wait / 4,
                        (wait % 4) * 250000000))
                        continue;

                while (dhcp_receive(handle, packet, sizeof packet, transaction,
                                    hardware, lease, address_of kind,
                                    address_of any_server,
                                    true,
                                    address_of selected_peer,
                                    address_of deadline))
                {
                        if (kind != DHCP_OFFER || !dhcp_lease_usable(lease))
                                continue;

                        //      Take the offer, naming the server so that any
                        //      other server that offered knows it lost.
                        length = dhcp_build(packet, sizeof packet, DHCP_REQUEST,
                                            transaction, hardware, lease->address,
                                            lease->server, 0, true);

                        if (socket_send(
                                (b32)handle, packet, length, 0,
                                address_of where, sizeof where) < 0)
                                /* The REQUEST never left.  Start the next
                                   discovery attempt immediately instead of
                                   spending its whole reply budget waiting for
                                   an answer that cannot exist. */
                                break;

                        if (!network_deadline_begin(
                                address_of deadline, wait / 4,
                                (wait % 4) * 250000000))
                                break;

                        status = dhcp_complete(
                            handle, packet, sizeof packet, transaction,
                            hardware, lease, address_of selected_peer, false,
                            address_of deadline);
                        if (status != DHCP_NO_OFFER)
                        {
                                socket_close((b32)handle);
                                return status;
                        }
                        break;
                }
        }

        socket_close((b32)handle);
        return DHCP_NO_OFFER;
}

/*
        Keeping the address we already have.

        A lease is a loan with a time on it. Half way through, a client is
        supposed to ask to keep what it has -- unicast to the server that gave
        it, with ciaddr set to the address and no server identifier, which is
        what distinguishes "may I keep this" from "may I have one". The server
        answers with an ACK and a fresh lease time.

        This matters more than it looks on a machine that stays up. qemu hands
        out a lease measured in days and nothing here would ever notice, but a
        home router giving an hour means a machine that has been up since
        yesterday is holding an address the server considers free, and the
        first thing that goes wrong is somebody else being given it.

        Renewing rather than starting over is the whole point: a fresh
        DISCOVER may come back with a different address, and every connection
        open at the time dies with it.

        A timeout here is not fatal and does not discard the address. The
        caller retries within the current state, switches from unicast renewal
        to broadcast rebinding at T2, and starts discovery only after a NAK or
        the lease's actual expiry.
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
        {
                status = DHCP_NO_SOCKET;
                goto done;
        }

        //      An ACK keeps what the renewal said, including the new lease
        //      time, but does not lose what it left out: it need not repeat
        //      every option.
        if (network_deadline_begin(address_of deadline, wait, 0))
                status = dhcp_complete(handle, packet, sizeof packet,
                                       transaction, hardware, lease,
                                       address_of expected, rebinding,
                                       address_of deadline);

done:
        socket_close((b32)handle);
        return status;
}

#endif // STANDARD_MODERN_C_NET_DHCP

#endif
