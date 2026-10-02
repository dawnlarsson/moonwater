/*
        Groups, as the listener keeps them: announcing on the local network,
        greeting every machine it finds, and keeping the members whose
        greetings it can read.

        A greeting is the ordinary handshake's first message addressed to the
        group's own key, which every member derives from the secret, with the
        group's pre-shared key in it (handshake.c): the member's real static
        key rides in it as in any initiation, with its name where a session's
        conversation would be. A machine outside the group cannot even pass
        the gate, since the gate is keyed by the group's key; a member that
        can read it has proof of the secret and of the key, and keeps the
        sender as a peer with what the group grants. A greeting is never
        answered: the greeted machine greets back when the sender was new to
        it, and that is how both sides learn each other -- even when only one
        of them can see the other's announcements. A member that moved is
        found the same way, by its greeting from the new place.

        A group is a line in /root/link.groups -- the namespace, the key
        derived from it and the secret (never the secret itself, and no hash
        of it either: a fast hash beside the slow key let anyone holding the
        file guess at full speed) and the grants members get here. Written
        by `moonwater link group`, read by the listener whenever it changes.

        Discovery is IPv4 only for now, 224.0.0.251 on every interface that
        takes the membership; ff02::fb is not asked or answered. A packet is
        believed only if it arrived with the TTL it was sent with, 255, which
        no router forwards: that is what keeps this on the local link. An
        address is taken from where a packet came from, never from what it
        says.

        Included by service.c, which owns the sockets and the loop.

        Dawn Larsson - Apache-2.0 license
        github.com/dawnlarsson/moonwater
*/

#ifndef WATERLINK_NEARBY_INCLUDED
#define WATERLINK_NEARBY_INCLUDED

#include "discover.c"

#define LINK_GROUPS_PATH "/root/link.groups"
#define LINK_GROUPS_NEXT "/root/link.groups.next"
#define LINK_PEERS_LOCK HOST_STATE "/link.peers.lock"
#define LINK_GROUPS_MAX 8
#define LINK_INTERFACES 16
#define LINK_GREETED 16

#define LINK_ANNOUNCE_EVERY 60000000ull  // after the first three
#define LINK_ASK_EVERY 30000000ull
#define LINK_GREET_AGAIN 10000000ull     // the same place, not sooner
#define LINK_ANSWER_AGAIN 100000ull      // a one-shot asker's answer, not sooner

/*
        A pairing code is a group of one: the namespace is the name of the
        machine that is waiting, the secret is the code, and the group lets
        in one machine and is gone. ONCE says so; expires is when it goes in
        seconds since boot (a code outlives neither its five minutes nor the
        boot), and expect is the crc of the one name that may use it, or zero
        for any. DONE is a code that has taken its machine and is kept half a
        minute, closed to everyone else, so a greeting back that was lost can
        still be answered.
*/
#define LINK_GROUP_ONCE 1u
#define LINK_GROUP_DONE 2u
#define LINK_CLOCK_BOOT 7
#define LINK_PAIR_SECONDS 300
#define LINK_PAIR_GRACE 30

struct link_group_record {
        char namespace[WATERLINK_NAMESPACE_MAX];
        p8 key[32];   // PBKDF2 of namespace and secret
        p8 check[32]; // unused; older files held a SHA-256 of the secret here
        p32 may;
        p32 flags;
        p64 expires;
        p32 expect;
        p8 reserved[12];
};

_Static_assert(sizeof(struct link_group_record) == 128,
               "a group record is 128 bytes");

typedef struct
{
        struct link_group_record record[LINK_GROUPS_MAX];
        positive count;
} link_groups;

static fn link_groups_scrub(positive records);

fn link_groups_load(link_groups address_to groups)
{
        positive got = 0;
        bool old = false;

        memory_zero(groups, sizeof(address_to groups));
        if (link_read_private_records(LINK_GROUPS_PATH,
                                      (p8 address_to)groups->record,
                                      sizeof(groups->record),
                                      sizeof(struct link_group_record),
                                      address_of got) < 0)
                return;
        groups->count = got / sizeof(struct link_group_record);
        for (positive at = 0; at < groups->count; at++)
        {
                groups->record[at].namespace[WATERLINK_NAMESPACE_MAX - 1] = 0;
                old |= memory_span_byte(groups->record[at].check, 0,
                                        sizeof groups->record[at].check) !=
                       sizeof groups->record[at].check;
                memory_zero(groups->record[at].check, sizeof groups->record[at].check);
                if (!link_name_good(groups->record[at].namespace))
                {
                        groups->record[at] = groups->record[--groups->count];
                        at--;
                }
        }
        if (old)
                link_groups_scrub(got / sizeof(struct link_group_record));
}

/*
        Files written before 2026-09-30 hold a SHA-256 of the secret in the
        check field of each record, a guessing oracle at full speed for
        anyone who can read the file or a copy of the disk, and it stayed
        there until a join or a leave next wrote the file whole -- which a
        machine that only listens never does. So whoever reads a file with
        one in it zeroes the field in the file as well, in place. Nothing
        reads the field and a file written now has nothing in it, so a record
        that was replaced since it was read loses nothing but old bytes.
*/
static fn link_groups_scrub(positive records)
{
        static const p8 zeros[32];
        file_facts facts;
        bipolar handle = system_open_at(AT_FDCWD, LINK_GROUPS_PATH,
                                        FILE_READ_WRITE | O_NOFOLLOW |
                                                O_CLOEXEC);

        if (handle < 0)
                return;
        if (file_look(handle, (string_address) "", AT_EMPTY_PATH,
                      address_of facts) &&
            (facts.mode & MODE_FORMAT) == MODE_FILE && facts.owner == 0 &&
            !(facts.mode & 077) &&
            !(facts.size % sizeof(struct link_group_record)))
        {
                for (positive at = 0;
                     at < records &&
                     (at + 1) * sizeof(struct link_group_record) <= facts.size;
                     at++)
                        (void)system_call_4(
                            syscall(pwrite64), (positive)handle,
                            (positive)zeros, sizeof zeros,
                            at * sizeof(struct link_group_record) +
                                __builtin_offsetof(struct link_group_record,
                                                   check));
                (void)system_call_1(syscall(fsync), (positive)handle);
        }
        system_close(handle);
}

static bipolar link_groups_save(link_groups address_to groups)
{
        return link_file_replace(LINK_GROUPS_NEXT, LINK_GROUPS_PATH,
                                 groups->record,
                                 groups->count * sizeof(struct link_group_record),
                                 true);
}

static p64 link_boot_seconds(void)
{
        return system_clock_ns(LINK_CLOCK_BOOT) / 1000000000ull;
}

//      The check a name carries in a code's record: its CRC-32, which says
//      whether it is the name that was expected and nothing more. A name
//      from a greeting has no terminator to count on, so its room is its
//      length at most.
static p32 link_name_check(string_address name, positive room)
{
        return ~hash_crc32(~0u, (p8 address_to)name,
                           string_length_max(name, room));
}

//      A one-use group that has run out, or has taken its machine and been
//      kept its half minute.
static bool link_group_spent(struct link_group_record address_to record,
                             p64 now)
{
        return (record->flags & LINK_GROUP_ONCE) && record->expires <= now;
}

/*
        The peers file has two writers now, the command and the listener, so
        every load, change and save of it happens under this record lock.
*/
static bipolar link_peers_lock(void)
{
        return link_lock_file(LINK_PEERS_LOCK, 7, null); // F_SETLKW: wait
}

static fn link_peers_unlock(bipolar handle)
{
        if (handle >= 0)
                system_close(handle);
}

/*
        This machine's name as it offers it to a member, and as `link pair`
        prints it: the host name in lowercase, held to what link_name_good
        takes. The far side checks it again all the same.
*/
fn link_machine_name(p8 address_to name)
{
        p8 uts[6 * 65];
        positive used = 0;

        memory_zero(name, WATERLINK_NAME_MAX);
        if (system_call_1(syscall(uname), (positive)uts) >= 0)
                for (positive at = 65; at < 130 && uts[at] &&
                                       used < WATERLINK_NAME_MAX - 1;
                     at++)
                {
                        p8 c = byte_to_lower(uts[at]);

                        if (byte_is_alnum(c) || (used && (c == '-' || c == '_')))
                                name[used++] = c;
                }
        if (!used)
                memory_copy(name, "machine", 8);
}

/*
        A member keeps a name that reads as the machine it is, and does not
        take one already in the file: a suffix from its key, the same every
        time it pairs, is added when it would, and then the name is cut to
        leave room for it.
*/
static fn link_name_for(link_peers address_to peers, p8 address_to offered,
                        p8 address_to key, p8 address_to name)
{
        p8 base[WATERLINK_NAME_MAX];
        positive length;

        //      Every byte of the name is written, the tail after its end
        //      too: the caller keeps the whole buffer in a file.
        memory_zero(name, WATERLINK_NAME_MAX);
        offered[WATERLINK_NAME_MAX - 1] = 0;
        memory_zero(base, sizeof base);
        length = string_length((string_address)offered);
        memory_copy(base, offered, length);
        if (!link_name_good((string_address)base))
                string_copy((string_address)base, "machine");

        string_copy((string_address)name, (string_address)base);
        for (positive width = 3; link_peer_named(peers, (string_address)name) &&
                                 width <= 6;
             width++)
        {
                positive used = string_length((string_address)base);
                p8 hex[8];

                if (used > 24)
                        used = 24;
                memory_into_hex(hex, key, 4);
                memory_zero(name, WATERLINK_NAME_MAX);
                memory_copy(name, base, used);
                name[used++] = '-';
                memory_copy(name + used, hex, width);
                name[used + width] = 0;
        }
}

// The listener's side of it ------------------------------------------------

struct link_greeted {
        p8 address[16];
        p16 port;
        p32 group;
        p64 at;
};

typedef struct
{
        link_groups groups;
        struct waterlink_group_keys keys[LINK_GROUPS_MAX];
        p8 instance[10]; // this start's random labels
        p8 host[6];
        p64 groups_changed; // inode and size of the file as last read
        p64 looked;
        bipolar socket;
        b32 interface[LINK_INTERFACES];
        p32 interface_address[LINK_INTERFACES];
        positive interfaces;
        p64 next_announce;
        p64 next_ask;
        positive announced;
        positive asked;
        p64 last_answer;
        p64 last_reply;
        p64 interfaces_looked;
        bool labels_ready;
        struct link_greeted greeted[LINK_GREETED];
        positive greeted_next;
        p8 name[WATERLINK_NAME_MAX];
        p64 expiry_look;
} link_nearby_state;

static link_nearby_state link_nearby;

static bool link_nearby_labels(void)
{
        link_nearby.labels_ready =
                system_random_fill(link_nearby.host, sizeof link_nearby.host,
                                   0) >= 0 &&
                system_random_fill(link_nearby.instance,
                                   sizeof link_nearby.instance, 0) >= 0;
        return link_nearby.labels_ready;
}

//      The one-use group that has taken its machine: closed to anyone else
//      at once, here and in the file, and gone in half a minute.
static fn link_group_close(positive group)
{
        link_groups groups;
        p64 now = link_boot_seconds();
        bipolar lock;

        link_nearby.groups.record[group].flags |= LINK_GROUP_DONE;
        link_nearby.groups.record[group].expires = now + LINK_PAIR_GRACE;

        lock = link_peers_lock();
        link_groups_load(address_of groups);
        for (positive at = 0; at < groups.count; at++)
                if ((groups.record[at].flags & LINK_GROUP_ONCE) &&
                    crypto_same(groups.record[at].key,
                                link_nearby.groups.record[group].key, 32))
                {
                        groups.record[at].flags |= LINK_GROUP_DONE;
                        groups.record[at].expires = now + LINK_PAIR_GRACE;
                }
        (void)link_groups_save(address_of groups);
        link_peers_unlock(lock);
        crypto_forget(address_of groups, sizeof groups);
}

//      One-use groups that ran out, dropped from the file whether or not
//      anything else changed it.
static fn link_groups_expire(void)
{
        link_groups groups;
        p64 now = link_boot_seconds();
        bool spent = false;
        bipolar lock;

        for (positive at = 0; at < link_nearby.groups.count; at++)
                spent |= link_group_spent(link_nearby.groups.record + at, now);
        if (!spent)
                return;

        lock = link_peers_lock();
        link_groups_load(address_of groups);
        for (positive at = 0; at < groups.count; at++)
                if (link_group_spent(groups.record + at, now))
                        groups.record[at--] = groups.record[--groups.count];
        (void)link_groups_save(address_of groups);
        link_peers_unlock(lock);
        crypto_forget(address_of groups, sizeof groups);
}

/*
        Keeping a member whose greeting opened: new, with the group's grants,
        or moved, when the record is the group's own. A record paired by hand
        or by another group is never replaced or widened. True when the
        member was new here.

        The record keeps the seconds of the last greeting it took, which is
        the replay marker that outlives the listener: the markers in memory
        are gone after a restart, and a greeting recorded on the link, played
        back from anywhere, would otherwise move the member there for good.
*/
static bool link_pair_keep(positive group, p8 address_to key,
                           p8 address_to offered, p32 stamped,
                           p8 address_to address, p16 port,
                           bool address_to accepted)
{
        struct waterlink_group_keys address_to keys = link_nearby.keys + group;
        link_peers peers;
        struct waterlink_peer address_to peer;
        bipolar lock = link_peers_lock();
        bool changed = false;
        bool new = false;

        struct link_group_record address_to record =
                link_nearby.groups.record + group;
        bool once = (record->flags & LINK_GROUP_ONCE) != 0;
        bool open = !once ||
                    (!(record->flags & LINK_GROUP_DONE) &&
                     !link_group_spent(record, link_boot_seconds()) &&
                     (!record->expect ||
                      record->expect == link_name_check((string_address)offered,
                                                        WATERLINK_NAME_MAX)));

        address_to accepted = false;

        if (!link_peers_for_change(address_of peers))
        {
                link_peers_unlock(lock);
                return false;
        }
        peer = link_peer_keyed(address_of peers, key);
        if (!peer && open && peers.count < LINK_PEERS_MAX &&
            !crypto_same(key, link_self.me.public, 32))
        {
                p8 name[WATERLINK_NAME_MAX];

                //      Named before it is in the list, or it meets itself.
                link_name_for(address_of peers, offered, key, name);
                peer = peers.peer + peers.count++;
                memory_zero(peer, sizeof(address_to peer));
                memory_copy(peer->key, key, 32);
                memory_copy(peer->name, name, WATERLINK_NAME_MAX);
                peer->may = link_nearby.groups.record[group].may
                                    ? link_nearby.groups.record[group].may
                                    : WATERLINK_MAY_DEFAULT;
                peer->group = keys->mark;
                new = changed = true;
        }
        else if (peer && peer->group == keys->mark && stamped <= peer->seen)
                peer = null;
        if (peer && peer->group == keys->mark)
        {
                changed |= peer->seen != stamped ||
                           memory_compare(peer->address, address, 16) ||
                           peer->port != port;
                memory_copy(peer->address, address, 16);
                peer->port = port;
                peer->seen = stamped;
        }
        if (changed)
        {
                bool saved = link_peers_save(address_of peers) >= 0;

                new = saved && new;
                if (!saved)
                        peer = null;
        }
        address_to accepted = peer != null;
        link_peers_unlock(lock);
        link_self.state_dirty = true;
        if (once && peer && !(record->flags & LINK_GROUP_DONE))
                link_group_close(group);
        return new;
}

typedef struct
{
        p32 multiaddr;
        p32 address;
        b32 index;
} link_mreqn;

/*
        Every interface with an IPv4 address that takes the membership, and
        that address for the A record: net.c's walk of the addresses, which
        is the one the network setup already trusts. A namespace in a test
        has no default route, so joining on "any" would fail there; one by
        one, by index, works everywhere.
*/
static bool link_nearby_address(netlink_header address_to header,
                                address_any context)
{
        netlink_address address_to body =
            netlink_message_body(header, sizeof(netlink_address));
        positive size = 0;
        p8 address_to host;
        link_mreqn join = {network_order_32(WATERLINK_MDNS_GROUP), 0, 0};
        bipolar joined;

        (void)context;
        if (header->type != RTM_NEWADDR || !body ||
            link_nearby.interfaces == LINK_INTERFACES)
                return true;
        host = netlink_find(header, sizeof(netlink_address), IFA_LOCAL,
                             address_of size);
        if (body->family != AF_INET || !host || size < 4)
                return true;
        for (positive at = 0; at < link_nearby.interfaces; at++)
                if (link_nearby.interface[at] == (b32)body->index)
                        return true;

        //      One without multicast is passed over; joined already
        //      (EADDRINUSE) is joined.
        join.index = (b32)body->index;
        joined = socket_option_set((b32)link_nearby.socket, 0,
                                   35, // IP_ADD_MEMBERSHIP
                                   address_of join, sizeof join);
        if (joined < 0 && joined != -98)
                return true;
        link_nearby.interface[link_nearby.interfaces] = (b32)body->index;
        link_nearby.interface_address[link_nearby.interfaces] =
                network_load_32(host);
        link_nearby.interfaces++;
        return true;
}

static fn link_nearby_interfaces(void)
{
        bipolar handle = netlink_open_groups(0);

        link_nearby.interfaces = 0;
        if (handle < 0)
                return;
        (void)netlink_dump((b32)handle, RTM_GETADDR, sizeof(netlink_address),
                           AF_INET, link_nearby_address, null);
        system_close((b32)handle);
}

static fn link_nearby_close(void)
{
        if (link_nearby.socket > 0)
                socket_close((b32)link_nearby.socket);
        link_nearby.socket = -1;
        link_nearby.interfaces = 0;
}

static fn link_nearby_open(void)
{
        b32 one = 1;
        b32 ttl = 255;
        socket_address_internet self = {
            .family = AF_INET, .port = network_order_16(WATERLINK_MDNS_PORT)};

        link_nearby.socket = socket_new(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC |
                                                        SOCK_NONBLOCK,
                                        0);
        if (link_nearby.socket < 0)
                return;
        (void)socket_option_set((b32)link_nearby.socket, SOL_SOCKET,
                                SO_REUSEADDR, address_of one, sizeof one);
        (void)socket_option_set((b32)link_nearby.socket, SOL_SOCKET, 15, // REUSEPORT
                                address_of one, sizeof one);
        (void)socket_option_set((b32)link_nearby.socket, 0, 33, // MULTICAST_TTL
                                address_of ttl, sizeof ttl);
        (void)socket_option_set((b32)link_nearby.socket, 0, 12, // RECVTTL
                                address_of one, sizeof one);
        (void)socket_option_set((b32)link_nearby.socket, 0, 8, // IP_PKTINFO
                                address_of one, sizeof one);

        if (socket_bind((b32)link_nearby.socket, address_of self,
                        sizeof self) < 0)
        {
                link_nearby_close();
                return;
        }
        link_nearby_interfaces();
}

/*
        The groups as the file says now, when it has changed: keys derived
        (cheaply -- the slow derivation happened at join), labels drawn, the
        socket opened for the first group and closed after the last.
*/
static fn link_nearby_reload(p64 now)
{
        file_facts facts;
        p64 changed = 0;

        if (now - link_nearby.expiry_look >= 2000000)
        {
                link_nearby.expiry_look = now;
                link_groups_expire();
        }

        //      Twice a second at most. Every save is a rename, so the inode
        //      says whether the file is the one already read.
        if (link_nearby.looked && now - link_nearby.looked < 500000)
                return;
        link_nearby.looked = now;
        if (file_look_at(LINK_GROUPS_PATH, address_of facts))
                changed = facts.inode * 31 + facts.size + 1;
        if (changed == link_nearby.groups_changed)
                return;
        link_nearby.groups_changed = changed;

        link_groups_load(address_of link_nearby.groups);
        for (positive at = 0; at < link_nearby.groups.count; at++)
                waterlink_group_keys_from(link_nearby.keys + at,
                                          link_nearby.groups.record[at].key,
                                          link_nearby.groups.record[at].namespace);
        link_machine_name(link_nearby.name);

        if (link_nearby.groups.count && link_nearby.socket < 0)
                link_nearby_open();
        if (!link_nearby.groups.count && link_nearby.socket >= 0)
                link_nearby_close();

        (void)link_nearby_labels();
        link_nearby.announced = 0;
        link_nearby.asked = 0;
        link_nearby.next_announce = now;
        link_nearby.next_ask = now;
}

static fn link_nearby_send(p8 address_to packet, positive length,
                           positive interface_at)
{
        link_mreqn out = {0, 0, link_nearby.interface[interface_at]};
        socket_address_internet to = {
            .family = AF_INET, .port = network_order_16(WATERLINK_MDNS_PORT),
            .host = network_order_32(WATERLINK_MDNS_GROUP)};

        (void)socket_option_set((b32)link_nearby.socket, 0, 32, // MULTICAST_IF
                                address_of out, sizeof out);
        (void)socket_send((b32)link_nearby.socket, packet, length,
                          MSG_NOSIGNAL, address_of to, sizeof to);
}

static fn link_nearby_announce(p32 ttl)
{
        p8 packet[WATERLINK_MDNS_MAX];

        for (positive at = 0; at < link_nearby.interfaces; at++)
        {
                positive length = waterlink_mdns_announce(
                        packet, sizeof packet, link_nearby.instance,
                        link_nearby.host, link_port(),
                        link_nearby.interface_address[at], ttl, 0, null, 0);

                if (length)
                        link_nearby_send(packet, length, at);
        }
}

static fn link_nearby_ask(void)
{
        p8 packet[64];
        positive length = waterlink_mdns_query(packet, sizeof packet);

        for (positive at = 0; at < link_nearby.interfaces; at++)
                link_nearby_send(packet, length, at);
}

// Goodbye, with TTL zero, when the listener stops.
static fn link_nearby_stop(void)
{
        if (link_nearby.socket >= 0 && link_nearby.groups.count)
                link_nearby_announce(0);
        link_nearby_close();
}

// The same place is greeted once in a while, not at every announcement.
static bool link_greeted_lately(p32 group, p8 address_to address, p16 port,
                                p64 now)
{
        for (positive at = 0; at < LINK_GREETED; at++)
        {
                struct link_greeted address_to greeted = link_nearby.greeted + at;

                if (greeted->at && now - greeted->at < LINK_GREET_AGAIN &&
                    greeted->group == group &&
                    greeted->port == port &&
                    !memory_compare(greeted->address, address, 16))
                        return true;
        }
        return false;
}

/*
        Greet a machine as a member of a group: the ordinary first message,
        to the group's key and with its pre-shared key, from this machine's
        own key and with its name. Greeted lately is not greeted again,
        except in answer to a greeting that opened: that machine is waiting
        for it, and the earlier one may have reached it before it had the
        group (a code made a moment ago), which is all the member that
        answers was told.
*/
static fn link_pair_begin(positive group, p8 address_to address, p16 port,
                          p64 now, bool answer)
{
        struct waterlink_group_keys address_to keys = link_nearby.keys + group;
        struct waterlink_noise noise;
        p8 datagram[WATERLINK_DATAGRAM];
        p8 hello[WATERLINK_HELLO_BYTES];
        p8 ephemeral[32];
        p64 wall = system_clock_ns(0);

        if ((!answer && link_greeted_lately(keys->mark, address, port, now)) ||
            system_random_fill(ephemeral, 32, 0) < 0)
                return;
        waterlink_stamp(hello, wall / 1000000000ull,
                        (p32)(wall % 1000000000ull));
        memory_copy(hello + WATERLINK_STAMP_BYTES, link_nearby.name,
                    WATERLINK_NAME_MAX);
        //      Counted whether or not it could be sent: the curve work is
        //      done, and a place that refuses it (port zero, say) is neither
        //      greeted again at once nor outside the budget.
        if (waterlink_initiate(address_of noise, address_of link_self.me,
                               keys->identity.public, keys->psk, ephemeral,
                               hello, datagram))
        {
                struct link_greeted address_to next =
                        link_nearby.greeted + link_nearby.greeted_next;

                (void)link_send_to(datagram, WATERLINK_DATAGRAM, address, port);
                link_nearby.greeted_next =
                        (link_nearby.greeted_next + 1) % LINK_GREETED;
                memory_copy(next->address, address, 16);
                next->port = port;
                next->group = keys->mark;
                next->at = now ? now : 1;
        }
        crypto_forget(ephemeral, sizeof ephemeral);
        crypto_forget(address_of noise, sizeof noise);
}

//      A greeting that opened: the member is kept, and greeted back if new.
static bool link_pair_greeted(positive group, p8 address_to key,
                              p8 address_to hello, p8 address_to address,
                              p16 port, p64 now)
{
        bool accepted;

        if (link_pair_keep(group, key, hello + WATERLINK_STAMP_BYTES,
                           network_load_32(hello + 4), address, port,
                           address_of accepted))
                link_pair_begin(group, address, port, now, true);
        return accepted;
}

/*
        One mDNS packet from the local link: a question about the service is
        answered, and every machine an announcement names is greeted for
        each group, unless a member of that group is already known there.
*/
static fn link_nearby_heard(p8 address_to packet, positive length,
                            p8 address_to address, p16 source_port,
                            p32 local_address, p64 now)
{
        struct waterlink_found found;
        link_peers peers;

        if (!waterlink_mdns_read(packet, length, address_of found))
                return;

        if (found.asked && link_nearby.groups.count)
        {
                //      Unicast to wherever the packet says it came from, which
                //      nobody checked: a few a second, or a stream of spoofed
                //      questions is a stream of answers four times their size
                //      at somebody else.
                if (source_port != WATERLINK_MDNS_PORT && found.question_length)
                {
                        //      A one-shot asker gets the answer back to
                        //      itself, its ID and question with it.
                        p8 reply[WATERLINK_MDNS_MAX];
                        positive reply_length = waterlink_mdns_announce(
                                reply, sizeof reply, link_nearby.instance,
                                link_nearby.host, link_port(),
                                local_address
                                    ? local_address
                                    : (link_nearby.interfaces
                                           ? link_nearby.interface_address[0]
                                           : 0),
                                10, found.id, found.question,
                                found.question_length);
                        socket_address_internet to = {
                            .family = AF_INET,
                            .port = network_order_16(source_port),
                            .host = network_order_32(network_load_32(address + 12))};

                        if (reply_length &&
                            link_age(now, link_nearby.last_reply) >= LINK_ANSWER_AGAIN)
                        {
                                link_nearby.last_reply = now;
                                (void)socket_send((b32)link_nearby.socket, reply,
                                                  reply_length, MSG_NOSIGNAL,
                                                  address_of to, sizeof to);
                        }
                }
                else if (now - link_nearby.last_answer > 1000000)
                {
                        link_nearby.last_answer = now;
                        link_nearby_announce(4500);
                }
        }

        if (found.count)
                link_peers_load(address_of peers);
        for (positive at = 0; at < found.count; at++)
        {
                struct waterlink_found_instance address_to instance =
                        found.instance + at;
                p8 ours[23];

                //      Our own, back through the loop.
                memory_copy(ours, "wl-", 3);
                memory_into_hex(ours + 3, link_nearby.instance, 10);
                if (!instance->has_port ||
                    (instance->label_length == sizeof ours &&
                     !memory_compare(instance->label, ours, sizeof ours)))
                        continue;

                for (positive group = 0; group < link_nearby.groups.count; group++)
                {
                        bool known = false;
                        p64 oldest = link_nearby.greeted[link_nearby.greeted_next].at;
                        p32 once = link_nearby.groups.record[group].flags;

                        //      A code that is used up greets nobody, and one that
                        //      waits for a named machine only answers: it cannot
                        //      tell whose announcement this is, and a greeting to
                        //      the wrong machine would link that machine to this
                        //      one, which would not link back.
                        if ((once & LINK_GROUP_ONCE) &&
                            ((once & LINK_GROUP_DONE) ||
                             link_nearby.groups.record[group].expect))
                                continue;

                        for (positive p = 0; p < peers.count && !known; p++)
                                known = peers.peer[p].group ==
                                                link_nearby.keys[group].mark &&
                                        peers.peer[p].port == instance->port &&
                                        !memory_compare(peers.peer[p].address,
                                                        address, 16);
                        //      Anyone may announce, naming any port, as many as
                        //      a packet holds: the ring of places greeted is
                        //      also the budget, LINK_GREETED greetings in
                        //      LINK_GREET_AGAIN, so a made-up announcement
                        //      cannot turn this machine into a sprayer of
                        //      handshakes. A greeting back to a member whose
                        //      own greeting opened is not held to it.
                        if (!known && (!oldest || link_age(now, oldest) >=
                                                          LINK_GREET_AGAIN))
                                link_pair_begin(group, address, instance->port,
                                                now, false);
                }
        }
}

/* The local IPv4 address the kernel says received this packet. IP_PKTINFO's
   data is ifindex, spec_dst and the header's destination, each four bytes;
   spec_dst is the local address selected for the incoming packet. */
static p32 link_nearby_local(p8 address_to control, positive length,
                             positive room)
{
        if (length > room)
                length = room;
        for (positive at = 0; at + 16 <= length;)
        {
                p64 size;
                b32 has[2];

                memory_copy(address_of size, control + at, 8);
                memory_copy(has, control + at + 8, 8);
                if (size < 16 || size > length - at)
                        break;
                if (!has[0] && has[1] == 8 && size >= 28) // IPPROTO_IP, IP_PKTINFO
                        return network_load_32(control + at + 20);
                at += (size + 7) & ~7ull;
        }
        return 0;
}

/*
        The listener's turn: reload the groups if they changed, and send what
        is due. Answers when it next wants a turn.
*/
static p64 link_nearby_tick(p64 now)
{
        p64 wake = now + 1000000;

        link_nearby_reload(now);
        if (link_nearby.socket < 0 || !link_nearby.groups.count)
                return wake;
        if (!link_nearby.labels_ready && !link_nearby_labels())
                return wake;

        //      Interfaces come and go, and an address comes late -- a lease
        //      that arrives after the listener started is the ordinary case
        //      on a machine that boots straight into it. Every five seconds
        //      the memberships are asked for again, and an address that is
        //      new starts the quick announcements over.
        if (now - link_nearby.interfaces_looked >= 5000000)
        {
                p32 before[LINK_INTERFACES];
                positive had = link_nearby.interfaces;
                bool changed;

                memory_copy(before, link_nearby.interface_address,
                            sizeof before);
                link_nearby.interfaces_looked = now;
                link_nearby_interfaces();
                changed = had != link_nearby.interfaces ||
                          memory_compare(before, link_nearby.interface_address,
                                         had * sizeof(p32));
                if (changed)
                {
                        link_nearby.announced = 0;
                        link_nearby.asked = 0;
                        link_nearby.next_announce = now;
                        link_nearby.next_ask = now;
                }
        }

        //      Three quick announcements and questions at the start, as
        //      RFC 6762 asks of a responder, then the steady pace.
        if (now >= link_nearby.next_announce)
        {
                link_nearby_announce(4500);
                link_nearby.announced++;
                link_nearby.next_announce =
                        now + (link_nearby.announced < 3 ? 1000000
                                                         : LINK_ANNOUNCE_EVERY);
        }
        if (now >= link_nearby.next_ask)
        {
                link_nearby_ask();
                link_nearby.asked++;
                link_nearby.next_ask =
                        now + (link_nearby.asked < 3 ? 1000000 : LINK_ASK_EVERY);
        }

        if (link_nearby.next_announce < wake)
                wake = link_nearby.next_announce;
        if (link_nearby.next_ask < wake)
                wake = link_nearby.next_ask;
        return wake;
}

// Read what is waiting on the mDNS socket, believing only TTL 255.
static fn link_nearby_receive(p64 now)
{
        for (positive turn = 0; turn < 64 && link_nearby.socket >= 0; turn++)
        {
                p8 packet[WATERLINK_MDNS_MAX + 1];
                socket_address_internet from;
                link_iovec part = {packet, sizeof packet};
                p64 control[8];
                link_message message;
                bipolar got;
                p8 address[16];

                memory_zero(address_of message, sizeof message);
                message.name = address_of from;
                message.name_length = sizeof from;
                message.parts = address_of part;
                message.part_count = 1;
                message.control = control;
                message.control_length = sizeof control;
                got = system_call_3(syscall(recvmsg), (positive)link_nearby.socket,
                                    (positive)address_of message, MSG_DONTWAIT);
                if (got < 0)
                        return;
                if ((positive)got > WATERLINK_MDNS_MAX)
                        continue;

                if (link_control_int((p8 address_to)control,
                                     message.control_length, sizeof control, 0,
                                     2) != 255) // IPPROTO_IP, IP_TTL
                        continue;

                link_address_v4(address, network_order_32(from.host));
                link_nearby_heard(packet, (positive)got, address,
                                  network_order_16(from.port),
                                  link_nearby_local((p8 address_to)control,
                                                    message.control_length,
                                                    sizeof control), now);
        }
}

#endif // WATERLINK_NEARBY_INCLUDED
