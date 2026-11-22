/*
        Waterlink over TCP: the stream carrier.

        The link's sealed datagrams, each as it would leave on UDP, on a TCP
        connection with a two-byte big-endian length before it. Nothing about
        the datagram changes: the handshake, the keys, the rekeys and the
        replay window are the ones UDP has, and a datagram from a connection
        enters link_datagram as one from the connection's place. So a session
        is where its place is, and a place is a connection when one is open
        to it: link_send_to finds the connection by the place and writes the
        datagram there, and otherwise sends it on UDP. A datagram that
        arrives from somewhere new moves its session there (link_carried),
        so a session goes from UDP to a connection and back the way it roams.

        What TCP does the link does not do again. A stream delivers what it
        was given, in order, or breaks: what a session sent on one since the
        connection was opened is neither given up on nor sent again, and its
        window is the most (waterlink_transport_stream), so the stream's own
        congestion control is the only one. What was sent before, on UDP, is
        still the link's to resend. Keepalives stay: they are what keeps a
        connection from its idle timeout and the session from LINK_DEAD.

        Bounded, and never waited on. At most LINK_TCP_MOST connections, in
        and out; each holds at most LINK_TCP_OUT bytes the kernel would not
        take yet, and a session fills a datagram only when its connection has
        room for it (link_tcp_room), so a slow reader slows its sender and
        nothing is lost here. Every socket is non-blocking and is read and
        written in the service's one poll. A connection that has carried no
        datagram within LINK_TCP_FIRST of being opened (or is not connected
        by then), that has heard nothing for LINK_TCP_IDLE, or that has had
        bytes waiting and taken none for as long, is closed. A length of zero
        or over a datagram is not the link's, and closes the connection.

        Through a proxy. When HTTPS_PROXY (or https_proxy, ALL_PROXY,
        all_proxy; discovery_proxy reads them) names one, `moonwater network
        proxy off` has not said to ignore it, and NO_PROXY does not say * or
        the peer's address, a connection is made to the proxy and
        asked `CONNECT host:port`; its 200 and the head after it are read,
        and from there on the stream is the link's.

        Dawn Larsson - Apache-2.0 license
        github.com/dawnlarsson/moonwater
*/

#ifndef WATERLINK_STREAM_INCLUDED
#define WATERLINK_STREAM_INCLUDED

#include "../net/discovery.c" // the proxy, and whether it is taken; already in the shell (src/sh/net.c)

#define LINK_TCP_MOST 16
#define LINK_TCP_FRAME_MOST WATERLINK_DATAGRAM
#define LINK_TCP_OUT (64 * 1024)
#define LINK_TCP_READ (64 * 1024) // bytes read from one connection a turn
#define LINK_TCP_FIRST 5000000ull // to connect, and then to carry a datagram
#define LINK_TCP_IDLE 60000000ull // of hearing nothing, or of bytes waiting that go nowhere
#define LINK_TCP_AUTO 1000000ull  // auto: UDP's silence, then TCP
#define LINK_PROXY_HEAD_MOST 1024

//      What /root/link.transport says of the network (modem.c keeps "modem").
#define LINK_CARRIER_NETWORK 0 // UDP, as it always was
#define LINK_CARRIER_TCP 1     // TCP alone
#define LINK_CARRIER_AUTO 2    // UDP, and TCP when UDP is not answered

static const string_address link_carrier_names[] = {"network", "tcp", "auto"};

#define LINK_TCP_FREE 0
#define LINK_TCP_CONNECTING 1
#define LINK_TCP_PROXY 2 // connected to the proxy, its answer to CONNECT read
#define LINK_TCP_OPEN 3

struct link_tcp {
        bipolar fd;
        p8 state;
        p8 outgoing;
        p8 quiet;  // serviced, and no wait has heard it since
        p8 broken; // a write failed: closed at the turn
        p8 proxied;
        p8 address[16]; // the place: the peer's when dialled, where it came from when taken
        p16 port;
        p64 opened; // what a session sent on it from then on is never lost
        p64 heard;  // the last byte read
        p64 moved;  // the last byte the kernel took, or the opening
        positive frames; // datagrams it carried in
        positive held;
        positive head;    // the proxy's answer so far
        positive out_from;
        positive out_used;
        p8 hold[2 + LINK_TCP_FRAME_MOST];
        p8 proxy_head[LINK_PROXY_HEAD_MOST];
        p8 out[LINK_TCP_OUT];
};

static struct
{
        struct link_tcp conn[LINK_TCP_MOST];
        bipolar listener; // its descriptor + 1: zero is none
        p8 listener_quiet;
        p8 carrier;
        p8 client;     // a client run: a place with no connection is dialled while TCP is the carrier
        p8 fell_back;  // auto went to TCP
        p64 carrier_read;
        positive taken;   // connections accepted
        positive refused; // accepted and closed at once: every place was held
        positive closed_idle;
} link_tcp_self;

static p8 link_datagram(p8 address_to datagram, positive length,
                        p8 address_to address, p16 port, p64 now);
static fn link_watch(system_poll_descriptor address_to watch,
                     p8 address_to address_to quiet, positive address_to count,
                     bipolar handle, p16 events, p8 address_to flag);

// The pure parts: frames, and the proxy's words ---------------------------------

typedef fn (address_to link_tcp_sink)(address_any context, p8 address_to datagram,
                                      positive length);

/*
        Bytes off a stream into the datagrams they carry, however the reads
        cut them: a frame whole in the bytes is handed on where it lies, and
        one cut by a read waits in hold (2 + LINK_TCP_FRAME_MOST bytes, *held
        of them used) for the rest. Answers the datagrams handed on, or -1 for
        a length of zero or over a datagram, after which the stream is not
        the link's and nothing more of it is read.
*/
static bipolar link_tcp_take(p8 address_to hold, positive address_to held,
                             p8 address_to bytes, positive length,
                             link_tcp_sink sink, address_any context)
{
        bipolar frames = 0;

        while (length)
        {
                positive have = address_to held;
                positive size;
                positive take;

                if (have < 2)
                {
                        hold[have++] = *bytes++;
                        length--;
                        address_to held = have;
                        if (have < 2)
                                continue;
                        size = network_load_16(hold);
                        if (!size || size > LINK_TCP_FRAME_MOST)
                                return -1;
                        if (length >= size)
                        {
                                //      The rest of it is in this read: handed on in place.
                                address_to held = 0;
                                sink(context, bytes, size);
                                bytes += size;
                                length -= size;
                                frames++;
                        }
                        continue;
                }
                size = network_load_16(hold);
                if (!size || size > LINK_TCP_FRAME_MOST || have > 2 + size)
                        return -1;
                take = 2 + size - have;
                if (take > length)
                        take = length;
                memory_copy(hold + have, bytes, take);
                bytes += take;
                length -= take;
                have += take;
                address_to held = have;
                if (have == 2 + size)
                {
                        address_to held = 0;
                        sink(context, hold + 2, size);
                        frames++;
                }
        }
        return frames;
}

//      A datagram as a frame at into: the bytes written, or 0 when it is no frame.
static positive link_tcp_frame(p8 address_to into, p8 address_to datagram,
                               positive length)
{
        if (!length || length > LINK_TCP_FRAME_MOST)
                return 0;
        network_store_16(into, (p16)length);
        memory_copy(into + 2, datagram, length);
        return 2 + length;
}

/*
        What a proxy is asked: CONNECT to the place, with the Host it names.
        The place is an address and a port in digits (link_place_text), so
        nothing of a name is written. Answers the bytes, or 0 with no room.
*/
static positive link_proxy_request(p8 address_to into, positive room,
                                   p8 address_to address, p16 port)
{
        p8 place[64];
        positive length;
        positive used = 0;

        link_place_text(address, port, place);
        length = string_length((string_address)place);
        if (room < 2 * length + 36)
                return 0;
        memory_copy(into, "CONNECT ", 8);
        used = 8;
        memory_copy(into + used, place, length);
        used += length;
        memory_copy(into + used, " HTTP/1.1\r\nHost: ", 17);
        used += 17;
        memory_copy(into + used, place, length);
        used += length;
        memory_copy(into + used, "\r\n\r\n", 4);
        return used + 4;
}

/*
        The proxy's answer so far: 0 while its head is not whole; the head's
        length once it is and its status is 200; -1 for any other status, a
        first line that is not HTTP/1.x's, or a head longer than
        LINK_PROXY_HEAD_MOST. What follows the head is the stream's.
*/
static bipolar link_proxy_reply(p8 address_to bytes, positive length)
{
        static const char line[] = "HTTP/1.x 200";
        positive check = length < 12 ? length : 12;

        for (positive at = 0; at < check; at++)
        {
                p8 want = (p8)line[at];

                if (at == 7 ? !byte_is_digit(bytes[at]) : bytes[at] != want)
                        return -1;
        }
        if (length > 12 && bytes[12] != ' ' && bytes[12] != '\r')
                return -1;
        if (length >= 16)
        {
                p8 address_to found = memory_search(bytes + 12, length - 12,
                                                    "\r\n\r\n", 4);
                if (found)
                        return (bipolar)(found - bytes + 4);
        }
        return length >= LINK_PROXY_HEAD_MOST ? -1 : 0;
}

/*
        Whether NO_PROXY (or no_proxy) says to go around the proxy to this
        place: a * or an entry that is its address as digits. Commas or
        spaces between entries.
*/
static p8 link_proxy_bypassed(p8 address_to address)
{
        string_address value = getenv("NO_PROXY");
        p8 place[64];
        positive length;

        if (!value || !value[0])
                value = getenv("no_proxy");
        if (!value)
                return false;
        link_place_text(address, 0, place);
        length = string_length((string_address)place);
        //      The address alone: the place's text without ":0", and an
        //      IPv6 one without its brackets.
        length -= 2;
        place[length] = 0;
        for (positive at = 0; value[at];)
        {
                positive from;

                while (value[at] == ',' || value[at] == ' ')
                        at++;
                from = at;
                while (value[at] && value[at] != ',' && value[at] != ' ')
                        at++;
                if (at - from == 1 && value[from] == '*')
                        return true;
                if (place[0] == '[' && at - from == length - 2 &&
                    !memory_compare(value + from, place + 1, length - 2))
                        return true;
                if (at - from == length &&
                    !memory_compare(value + from, place, length))
                        return true;
        }
        return false;
}

/*
        The proxy for a place, when one is named and not bypassed: its address
        and port (a name is looked up as link_parse_place looks one up, and a
        proxy with no port is on 80). False to connect straight there.
*/
static p8 link_proxy_for(p8 address_to place, p8 address_to address,
                         p16 address_to port)
{
        p8 text[96];
        discovery_settings settings;

        discovery_settings_read(address_of settings);
        if (!discovery_proxy(address_of settings, text, sizeof text - 4) ||
            link_proxy_bypassed(place))
                return false;
        {
                string_address close = text[0] == '['
                                               ? string_first_of((string_address)text, ']')
                                               : null;
                string_address colon = string_first_of((string_address)text, ':');

                if (close ? close[1] == 0 : !colon)
                        memory_copy(text + string_length((string_address)text), ":80", 4);
        }
        return link_parse_place((string_address)text, address, port);
}

// The carrier -------------------------------------------------------------------

/*      What /root/link.transport says of the network, read at most once a
        second: tcp, auto, or (anything else) the network as it always was. */
static p8 link_carrier(p64 now)
{
        p8 word[16];

        if (link_tcp_self.carrier_read && link_age(now, link_tcp_self.carrier_read) < 1000000)
                return link_tcp_self.carrier;
        link_tcp_self.carrier_read = now ? now : 1;
        host_read_word(LINK_TRANSPORT_PATH, word, sizeof word);
        {
                positive at = string_table_find((string_address)word,
                                                link_carrier_names,
                                                sizeof link_carrier_names[0],
                                                array_count(link_carrier_names));
                link_tcp_self.carrier = at < array_count(link_carrier_names)
                                                ? (p8)at
                                                : LINK_CARRIER_NETWORK;
        }
        return link_tcp_self.carrier;
}

static struct link_tcp address_to link_tcp_for(p8 address_to address, p16 port)
{
        for (positive at = 0; at < LINK_TCP_MOST; at++)
        {
                struct link_tcp address_to c = link_tcp_self.conn + at;

                if (c->state != LINK_TCP_FREE &&
                    link_place_same(c->address, c->port, address, port))
                        return c;
        }
        return null;
}

static fn link_tcp_close(struct link_tcp address_to c)
{
        if (c->state == LINK_TCP_FREE)
                return;
        socket_close((b32)c->fd);
        c->state = LINK_TCP_FREE;
        c->fd = -1;
        c->held = 0;
        c->head = 0;
        c->out_from = 0;
        c->out_used = 0;
        c->frames = 0;
}

static fn link_tcp_close_all(void)
{
        for (positive at = 0; at < LINK_TCP_MOST; at++)
                link_tcp_close(link_tcp_self.conn + at);
        if (link_tcp_self.listener)
                socket_close((b32)(link_tcp_self.listener - 1));
        link_tcp_self.listener = 0;
}

//      A free place for a connection, or none.
static struct link_tcp address_to link_tcp_slot(void)
{
        for (positive at = 0; at < LINK_TCP_MOST; at++)
                if (link_tcp_self.conn[at].state == LINK_TCP_FREE)
                        return link_tcp_self.conn + at;
        return null;
}

/*      A connected stream taken as the link's, from a place: one the listener
        accepted, or (in the checks) one end of a socketpair. */
static struct link_tcp address_to link_tcp_adopt(bipolar fd, p8 address_to address,
                                                 p16 port, p8 outgoing, p64 now)
{
        struct link_tcp address_to c = link_tcp_slot();

        if (!c)
                return null;
        memory_zero(c, offset_of(struct link_tcp, hold));
        c->fd = fd;
        c->state = LINK_TCP_OPEN;
        c->outgoing = outgoing;
        memory_copy(c->address, address, 16);
        c->port = port;
        c->opened = now;
        c->heard = now;
        c->moved = now;
        return c;
}

//      What the kernel takes of what waits, now. A failure is the connection's end, at the turn.
static fn link_tcp_write(struct link_tcp address_to c, p64 now)
{
        while (c->state == LINK_TCP_OPEN && c->out_used && !c->broken)
        {
                bipolar sent = socket_send((b32)c->fd, c->out + c->out_from,
                                           c->out_used, MSG_NOSIGNAL | MSG_DONTWAIT,
                                           null, 0);

                if (sent == -EAGAIN || sent == -EINTR)
                        break;
                if (sent <= 0)
                {
                        c->broken = true;
                        break;
                }
                c->moved = now;
                c->out_from += (positive)sent;
                c->out_used -= (positive)sent;
        }
        if (!c->out_used)
                c->out_from = 0;
}

/*      A datagram onto a connection, behind what waits there: false when it
        has no room, as a full socket loses a datagram. */
static p8 link_tcp_send(struct link_tcp address_to c, p8 address_to datagram,
                        positive length, p64 now)
{
        if (c->broken || !length || length > LINK_TCP_FRAME_MOST ||
            LINK_TCP_OUT - c->out_used < 2 + length)
                return false;
        if (LINK_TCP_OUT - c->out_from - c->out_used < 2 + length)
        {
                memory_copy(c->out, c->out + c->out_from, c->out_used);
                c->out_from = 0;
        }
        c->out_used += link_tcp_frame(c->out + c->out_from + c->out_used,
                                      datagram, length);
        link_tcp_write(c, now);
        return true;
}

/*      How many whole datagrams a place's connection has room for, ~0 for a
        place with none: a session fills no more than this. */
static positive link_tcp_room(p8 address_to address, p16 port)
{
        struct link_tcp address_to c = link_tcp_for(address, port);

        if (!c)
                return ~(positive)0;
        return (LINK_TCP_OUT - c->out_used) / (2 + WATERLINK_DATAGRAM);
}

static bipolar link_stream_socket(void)
{
        b32 zero = 0;
        b32 family = link_self.v4 ? AF_INET : AF_INET6;
        bipolar handle = socket_new(family, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
        if (handle >= 0 && !link_self.v4)
                (void)socket_option_set((b32)handle, SOL_IPV6, IPV6_V6ONLY,
                                        address_of zero, sizeof zero);
        return handle;
}

/*
        A connection to a place, made now and not waited on: straight there,
        or to the proxy. The one there already when there is one. Null when
        every place is held or the socket cannot be made; a datagram for the
        place then goes on UDP, as it would have.
*/
static struct link_tcp address_to link_tcp_dial(p8 address_to address, p16 port,
                                                p64 now)
{
        struct link_tcp address_to c = link_tcp_for(address, port);
        socket_address_internet6 to;
        p8 there[16];
        p16 there_port = port;
        p8 proxied;
        bipolar fd;
        bipolar status;
        b32 one = 1;

        if (c)
                return c;
        if (!link_tcp_slot() || !link_address_unicast(address))
                return null;
        memory_copy(there, address, 16);
        proxied = link_proxy_for(address, there, address_of there_port);
        if (link_self.v4 && !link_address_mapped(there))
                return null;
        fd = link_stream_socket();
        if (fd < 0)
                return null;
        (void)socket_option_set((b32)fd, IPPROTO_TCP, TCP_NODELAY, address_of one,
                                sizeof one);
        status = socket_connect((b32)fd, address_of to,
                                link_destination(address_of to, there, there_port));
        if (status < 0 && status != -EINPROGRESS && status != -EINTR)
        {
                socket_close((b32)fd);
                return null;
        }
        c = link_tcp_adopt(fd, address, port, true, now);
        if (!c)
        {
                socket_close((b32)fd);
                return null;
        }
        c->state = LINK_TCP_CONNECTING;
        c->proxied = proxied;
        return c;
}

/*      Whether a place the client tries is dialled over TCP now: always with
        tcp, and with auto once UDP has gone `waited` unanswered. */
static p8 link_tcp_wanted(p64 waited, p64 now)
{
        p8 carrier = link_carrier(now);

        if (carrier == LINK_CARRIER_AUTO && waited >= LINK_TCP_AUTO)
                link_tcp_self.fell_back = true;
        return carrier == LINK_CARRIER_TCP ||
               (carrier == LINK_CARRIER_AUTO && link_tcp_self.fell_back);
}

static fn link_tcp_heard(address_any context, p8 address_to datagram,
                         positive length)
{
        struct link_tcp address_to c = context;

        if (link_datagram(datagram, length, c->address, c->port, link_self.now))
        {
                c->frames++;
                c->heard = link_self.now;
        }
        else if (!c->outgoing && !c->frames)
                c->broken = true;
}

/*      A connect that finished: its error, or on through the proxy's CONNECT
        to the link's own stream. */
static fn link_tcp_connected(struct link_tcp address_to c, p64 now)
{
        b32 error = 0;
        p32 size = sizeof error;
        p8 request[160];
        positive length;

        if (socket_option_get((b32)c->fd, SOL_SOCKET, SO_ERROR, address_of error,
                              address_of size) < 0 ||
            error)
        {
                link_tcp_close(c);
                return;
        }
        {
                socket_address_internet6 peer;
                b32 peer_size = sizeof peer;

                //      Not connected yet: the wait woke for something else.
                if (system_call_3(syscall(getpeername), (positive)c->fd,
                                  (positive)address_of peer,
                                  (positive)address_of peer_size) < 0)
                        return;
        }
        c->moved = now;
        if (!c->proxied)
        {
                c->state = LINK_TCP_OPEN;
                link_tcp_write(c, now);
                return;
        }
        length = link_proxy_request(request, sizeof request, c->address, c->port);
        //      A new connection's socket takes the 70 bytes whole, or the proxy is not one to use.
        if (!length || socket_send((b32)c->fd, request, length,
                                   MSG_NOSIGNAL | MSG_DONTWAIT, null, 0) != (bipolar)length)
        {
                link_tcp_close(c);
                return;
        }
        c->state = LINK_TCP_PROXY;
}

/*      What one connection has to read, a turn's worth at most: the proxy's
        answer first while it is the proxy's, then frames. False when the
        connection is to be closed. */
static p8 link_tcp_read(struct link_tcp address_to c, p64 now)
{
        p8 bytes[16384];
        positive read = 0;

        while (read < LINK_TCP_READ &&
               (c->state == LINK_TCP_OPEN || c->state == LINK_TCP_PROXY))
        {
                bipolar got = socket_receive((b32)c->fd, bytes, sizeof bytes,
                                             MSG_DONTWAIT, null, null);
                p8 address_to from = bytes;

                if (got == -EAGAIN || got == -EINTR)
                        return true;
                //      The end, or an error: a frame it cut short is never handed on.
                if (got <= 0)
                        return false;
                read += (positive)got;
                if (c->state == LINK_TCP_PROXY)
                {
                        positive room = LINK_PROXY_HEAD_MOST - c->head;
                        positive take = (positive)got < room ? (positive)got : room;
                        bipolar head;

                        c->heard = now;
                        memory_copy(c->proxy_head + c->head, bytes, take);
                        c->head += take;
                        head = link_proxy_reply(c->proxy_head, c->head);
                        if (head < 0)
                                return false;
                        if (!head)
                                continue;
                        //      What came after the head is the stream's.
                        from = bytes + take - (c->head - (positive)head);
                        got = (bipolar)(bytes + got - from);
                        c->state = LINK_TCP_OPEN;
                        c->moved = now;
                        link_tcp_write(c, now);
                }
                if (link_tcp_take(c->hold, address_of c->held, from, (positive)got,
                                  link_tcp_heard, c) < 0 || c->broken)
                        return false;
        }
        return true;
}

/*      The listener beside the UDP socket, on the same port, while the
        carrier is tcp or auto; none under network or the modem. */
static fn link_tcp_listen(p16 port, p64 now)
{
        p8 wanted = link_carrier(now) != LINK_CARRIER_NETWORK;
        bipolar fd;
        b32 one = 1;
        p8 anywhere[16] = {0};
        socket_address_internet6 self;

        if (!wanted && link_tcp_self.listener)
        {
                socket_close((b32)(link_tcp_self.listener - 1));
                link_tcp_self.listener = 0;
        }
        if (!wanted || link_tcp_self.listener)
                return;
        fd = link_stream_socket();
        if (fd < 0)
                return;
        (void)socket_option_set((b32)fd, SOL_SOCKET, SO_REUSEADDR, address_of one,
                                sizeof one);
        if (socket_bind((b32)fd, address_of self,
                        link_destination(address_of self, anywhere, port)) < 0 ||
            socket_listen((b32)fd, LINK_TCP_MOST) < 0)
        {
                socket_close((b32)fd);
                return;
        }
        link_tcp_self.listener = fd + 1;
        link_tcp_self.listener_quiet = false;
}

//      Every connection the listener has waiting, taken while a place is free.
static fn link_tcp_accept(p64 now)
{
        while (link_tcp_self.listener)
        {
                socket_address_internet6 from;
                b32 size = sizeof from;
                p8 address[16];
                p16 port;
                memory_zero(address_of from, sizeof from);
                bipolar fd = socket_accept((b32)(link_tcp_self.listener - 1),
                                           address_of from, address_of size,
                                           SOCK_NONBLOCK | SOCK_CLOEXEC);

                if (fd < 0)
                        return;
                if (!link_address_from_sockaddr(address_of from, address, address_of port))
                {
                        link_tcp_self.refused++;
                        socket_close((b32)fd);
                        continue;
                }
                link_tcp_self.taken++;
                {
                        positive unauth_all = 0;
                        positive unauth_ip = 0;

                        for (positive at = 0; at < LINK_TCP_MOST; at++)
                        {
                                struct link_tcp address_to cur = link_tcp_self.conn + at;

                                if (cur->state != LINK_TCP_FREE && !cur->outgoing && !cur->frames)
                                {
                                        unauth_all++;
                                        if (!memory_compare(cur->address, address, 16))
                                                unauth_ip++;
                                }
                        }
                        if (unauth_ip >= 2 || unauth_all >= LINK_TCP_MOST / 2 ||
                            link_tcp_for(address, port) ||
                            !link_tcp_adopt(fd, address, port, false, now))
                        {
                                link_tcp_self.refused++;
                                socket_close((b32)fd);
                        }
                }
        }
}

/*
        One turn of every connection: the listener's new ones, the connects
        that finished, what each has to read and to write, and the ones whose
        time ran out. A connection no wait has heard since its last turn is
        only looked at for its time.
*/
static fn link_tcp_turn(p64 now)
{
        if (link_tcp_self.listener && !link_tcp_self.listener_quiet)
        {
                link_tcp_accept(now);
                link_tcp_self.listener_quiet = true;
        }
        for (positive at = 0; at < LINK_TCP_MOST; at++)
        {
                struct link_tcp address_to c = link_tcp_self.conn + at;
                p8 keep = true;

                if (c->state == LINK_TCP_FREE)
                        continue;
                if (!c->quiet)
                {
                        if (c->state == LINK_TCP_CONNECTING)
                                link_tcp_connected(c, now);
                        if (c->state != LINK_TCP_FREE)
                        {
                                keep = link_tcp_read(c, now);
                                link_tcp_write(c, now);
                        }
                        c->quiet = true;
                }
                if (c->state == LINK_TCP_FREE)
                        continue;
                if (!keep || c->broken ||
                    ((c->state != LINK_TCP_OPEN || !c->frames) &&
                     link_age(now, c->opened) > LINK_TCP_FIRST) ||
                    link_age(now, c->heard) > LINK_TCP_IDLE ||
                    (c->out_used && link_age(now, c->moved) > LINK_TCP_IDLE))
                {
                        link_tcp_self.closed_idle += keep && !c->broken;
                        link_tcp_close(c);
                }
        }
}

static fn link_tcp_watch(system_poll_descriptor address_to watch,
                         p8 address_to address_to quiet, positive address_to count)
{
        if (link_tcp_self.listener)
                link_watch(watch, quiet, count, link_tcp_self.listener - 1,
                           SYSTEM_POLL_READ, address_of link_tcp_self.listener_quiet);
        for (positive at = 0; at < LINK_TCP_MOST; at++)
        {
                struct link_tcp address_to c = link_tcp_self.conn + at;

                if (c->state == LINK_TCP_FREE)
                        continue;
                link_watch(watch, quiet, count, c->fd,
                           c->state == LINK_TCP_CONNECTING ? SYSTEM_POLL_WRITE
                           : c->out_used ? SYSTEM_POLL_READ | SYSTEM_POLL_WRITE
                                         : SYSTEM_POLL_READ,
                           address_of c->quiet);
        }
}

//      The words a place's carrier is called by, for the state and the doctor.
static string_address link_tcp_carrier_word(p8 address_to address, p16 port)
{
        struct link_tcp address_to c = link_tcp_for(address, port);

        if (link_modem_carrying())
                return (string_address) "modem";
        if (!c)
                return (string_address) "udp";
        return c->proxied ? (string_address) "tcp, through the proxy" : (string_address) "tcp";
}

#endif // WATERLINK_STREAM_INCLUDED
