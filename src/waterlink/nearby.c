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

#if defined(MACOS)
#define LINK_PEERS_LOCK link_state_path(LINK_LEAF_PEERS_LOCK)
#else
#define LINK_PEERS_LOCK HOST_STATE "/link.peers.lock"
#endif
#define LINK_GROUPS_MAX 8
#define LINK_INTERFACES 16
#define LINK_GREETED 16
#define LINK_GREET_SOURCE LINK_GROUPS_MAX

#define LINK_ANNOUNCE_EVERY 60000000ull  // after the first three
#define LINK_ASK_EVERY 30000000ull
#define LINK_GREET_AGAIN 10000000ull     // the same place, not sooner
#define LINK_ANSWER_AGAIN 100000ull      // a one-shot asker's answer, not sooner
#define LINK_PENDING 4
#define LINK_PREFER_WAIT 5000000ull      // the last-heard place, before the others

/*      Why discovery has not linked a machine: the same numbers and words the
        link's other reasons use, so a status or doctor line reads the same
        whichever path said it. Stable: a new reason takes the next number. */
#ifndef LINK_REASON_LINKED
#define LINK_REASON_LINKED 0
#define LINK_REASON_NO_ROUTE 1
#define LINK_REASON_NO_REPLY 2
#define LINK_REASON_BAD_KEY 3
#define LINK_REASON_CODE_REFUSED 4
#endif

/*      The sweep for a waiting code (link_sweep_turn): four sweeps, at the
        start and after 15, 45 and 105 seconds, each of every host of this
        machine's subnets (a /24 at the most around each interface), and at
        LINK_SWEEP_BATCH addresses a LINK_SWEEP_STEP, which is 32 a second:
        the listener calls more initiations a second than that load, and
        under load a member wants a second MAC it does not get. */
#define LINK_SWEEPS 4
#define LINK_SWEEP_HOSTS 256
#define LINK_SWEEP_BATCH 8
#define LINK_SWEEP_STEP 250000ull

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

static p8 link_groups_scrub(positive records);

static bipolar link_groups_read(link_groups address_to groups,
                                positive address_to old);

/* A look at the groups: none when the file cannot be read, whatever the
   reason. False when an old file's fast verifier could not be removed
   durably. The groups stay loaded, because a command that goes on to save them
   rewrites the file whole and that is the scrub; only the listener, which
   keeps them in memory, refuses to act on them. */
p8 link_groups_load(link_groups address_to groups)
{
        positive old;

        return link_groups_read(groups, address_of old) < 0 || !old ||
               link_groups_scrub(old);
}

/* The groups, to change them: a file that is there and cannot be read is not
   an empty list, and saving over it would forget every group in it. False
   then, with the file as it was; only a file never written is empty. */
static p8 link_groups_for_change(link_groups address_to groups)
{
        positive old;
        bipolar read = link_groups_read(groups, address_of old);

        if (read >= 0 && old)
                (void)link_groups_scrub(old);
        return read >= 0 || read == -ENOENT;
}

/* The groups to show: read and not changed, so a status never scrubs the
   file. False when an old file cannot be read, as for a change. */
static p8 link_groups_for_show(link_groups address_to groups)
{
        positive old;
        bipolar read = link_groups_read(groups, address_of old);

        return read >= 0 || read == -ENOENT;
}

/* The groups as the file says, with no groups and the reason when it cannot
   be read: -ENOENT for a file never written, the refusal of a file that is not
   root's own, private and whole, or what the read said. A record's old check
   is zeroed in memory, and `old` is how many records the file holds when any
   of them had one (the count the scrub is given, taken before a record whose
   name is no name is dropped), or none. */
static bipolar link_groups_read(link_groups address_to groups,
                                positive address_to old)
{
        positive got = 0;
        bipolar read;

        memory_zero(groups, sizeof(address_to groups));
        address_to old = 0;
        read = link_read_private_records(LINK_GROUPS_PATH,
                                         (p8 address_to)groups->record,
                                         sizeof(groups->record),
                                         sizeof(struct link_group_record),
                                         address_of got);
        if (read < 0)
                return read;
        groups->count = got / sizeof(struct link_group_record);
        for (positive at = 0; at < groups->count; at++)
        {
                if (memory_span_byte(groups->record[at].check, 0,
                                     sizeof groups->record[at].check) !=
                    sizeof groups->record[at].check)
                        address_to old = groups->count;
                memory_zero(groups->record[at].check, sizeof groups->record[at].check);
        }
        link_records_named((p8 address_to)groups->record,
                           address_of groups->count,
                           sizeof(struct link_group_record),
                           offset_of(struct link_group_record, namespace),
                           WATERLINK_NAMESPACE_MAX);
        return 0;
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
static p8 link_groups_scrub(positive records)
{
        static const p8 zeros[32];
        file_facts facts;
        bipolar handle = system_open_at(AT_FDCWD, LINK_GROUPS_PATH,
                                        FILE_READ_WRITE | O_NOFOLLOW |
                                                O_CLOEXEC);
        p8 scrubbed = false;

        if (handle < 0)
                return false;
        if (file_look(handle, (string_address) "", AT_EMPTY_PATH,
                      address_of facts) &&
            (facts.mode & MODE_FORMAT) == MODE_FILE && facts.owner == 0 &&
            !(facts.mode & 077) &&
            !(facts.size % sizeof(struct link_group_record)))
        {
                scrubbed = true;
                for (positive at = 0;
                     at < records &&
                     (at + 1) * sizeof(struct link_group_record) <= facts.size;
                     at++)
                        scrubbed &= file_transfer_exact(
                            syscall(pwrite64), handle, (p8 address_to)zeros,
                            sizeof zeros,
                            at * sizeof(struct link_group_record) +
                                offset_of(struct link_group_record,
                                                   check)) ==
                                    (bipolar)sizeof zeros;
                scrubbed &= system_call_1(syscall(fsync), (positive)handle) >= 0;
        }
        system_close(handle);
        return scrubbed;
}

static bipolar link_groups_save(link_groups address_to groups)
{
        return link_file_replace(LINK_GROUPS_PATH, groups->record,
                                 groups->count * sizeof(struct link_group_record),
                                 true);
}

//      CLOCK_BOOTTIME is the library's: on Darwin, which has no boot clock
//      under that name, it is CLOCK_MONOTONIC (6 there), which like it keeps
//      counting while the machine sleeps.
static p64 link_boot_seconds(void)
{
        return system_clock_ns(CLOCK_BOOTTIME) / 1000000000ull;
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
static p8 link_group_spent(struct link_group_record address_to record,
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
        return host_lock_file(LINK_PEERS_LOCK, HOST_FILE_SETLKW, null);
}

static fn link_peers_unlock(bipolar handle)
{
        if (handle >= 0)
                system_close(handle);
}

/*
        This machine's name as it offers it to a member, and as `link pair`
        prints it: the host name in lowercase, as far as it is letters,
        digits, dashes and underscores and starting with a letter or digit,
        which is what link_name_good takes. The far side checks it again all
        the same.
*/
fn link_machine_name(p8 address_to name)
{
#ifdef MACOS
        p8 host[256];
        p32 mib[2] = {1, 10}; // CTL_KERN, KERN_HOSTNAME
        p64 hostlen = sizeof host;
#else
        p8 uts[6 * 65];
#endif
        positive used;

        memory_zero(name, WATERLINK_NAME_MAX);
#ifdef MACOS
        //      Darwin has no uname the kernel takes. The nodename uname
        //      reports is the kernel's hostname, which sysctl gives directly.
        memory_zero(host, sizeof host);
        if (system_call_6(syscall(sysctl), (positive)mib, 2, (positive)host,
                          (positive)address_of hostlen, 0, 0) >= 0)
                for (positive at = 0;
                     at < WATERLINK_NAME_MAX - 1 && host[at]; at++)
                        name[at] = byte_to_lower(host[at]);
#else
        if (system_call_1(syscall(uname), (positive)uts) >= 0)
                for (positive at = 0;
                     at < WATERLINK_NAME_MAX - 1 && uts[65 + at]; at++)
                        name[at] = byte_to_lower(uts[65 + at]);
#endif
        used = string_span_of_set((string_address)name,
                                  "abcdefghijklmnopqrstuvwxyz0123456789-_");
        memory_zero(name + used, WATERLINK_NAME_MAX - used);
        if (!used || !byte_is_alnum(name[0]))
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

        //      Every byte of the name is written, the tail after its end
        //      too: the caller keeps the whole buffer in a file.
        memory_zero(name, WATERLINK_NAME_MAX);
        offered[WATERLINK_NAME_MAX - 1] = 0;
        memory_zero(base, sizeof base);
        string_copy((string_address)base, (string_address)offered);
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

/*
        An announcement from a place a known member is not at, for a member
        the announcement may be: the member's last-heard place is greeted at
        once, and the place the announcement came from only if that place has
        not answered by LINK_PREFER_WAIT. The announcement carries no key, so
        it cannot say which member it is; the answer does, and the record's
        address and stamp say whether the last-heard place answered.
*/
struct link_pending {
        p8 key[32];
        p8 last[16];
        p16 last_port;
        p32 seen;
        p8 heard[16];
        p16 heard_port;
        positive group;
        p64 due;
        p8 used;
};

typedef struct
{
        link_groups groups;
        struct waterlink_group_keys keys[LINK_GROUPS_MAX];
        p8 instance[10]; // this start's random labels
        p8 host[6];
        p64 groups_mark[6]; // what the file was when last read, or nothing
        p64 looked;
        bipolar socket;
        b32 interface[LINK_INTERFACES];
        p32 interface_address[LINK_INTERFACES];
        p32 interface_mask[LINK_INTERFACES]; // host order; 0 when not known
        positive interfaces;
        p64 next_announce;
        p64 next_ask;
        positive announced;
        positive asked;
        p64 last_answer;
        p64 last_reply;
        p64 interfaces_looked;
        p8 labels_ready;
        struct link_greeted greeted[LINK_GREETED];
        struct link_pending pending[LINK_PENDING];
        p8 linked;       // a member's greeting opened here
        p8 refused_code; // a one-use code turned a greeting away
        p8 no_route;     // the last send found no route to its place
        positive greeted_next;
        p8 name[WATERLINK_NAME_MAX];
        p64 expiry_look;
        positive sweep_left; // sweeps still to start for the waiting code
        p8 sweeping;
        positive sweep_group;
        positive sweep_count;
        positive sweep_at;
        p64 sweep_next;
        p32 sweep_target[LINK_SWEEP_HOSTS];
        p8 sweep_datagram[WATERLINK_DATAGRAM];
} link_nearby_state;

static link_nearby_state link_nearby;

static p8 link_nearby_labels(void)
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

        //      Without the lock, or with a file that cannot be read (which is
        //      no empty list to save over), the file keeps the code a little
        //      longer: it is spent by its time in memory and dropped from the
        //      file at the next turn that has both.
        lock = link_peers_lock();
        if (lock < 0)
                return;
        if (link_groups_for_change(address_of groups))
        {
                for (positive at = 0; at < groups.count; at++)
                        if ((groups.record[at].flags & LINK_GROUP_ONCE) &&
                            crypto_same(groups.record[at].key,
                                        link_nearby.groups.record[group].key, 32))
                        {
                                groups.record[at].flags |= LINK_GROUP_DONE;
                                groups.record[at].expires = now + LINK_PAIR_GRACE;
                        }
                (void)link_groups_save(address_of groups);
        }
        link_peers_unlock(lock);
        crypto_forget(address_of groups, sizeof groups);
}

//      One-use groups that ran out, dropped from the file whether or not
//      anything else changed it.
static fn link_groups_expire(void)
{
        link_groups groups;
        p64 now = link_boot_seconds();
        p8 spent = false;
        bipolar lock;

        for (positive at = 0; at < link_nearby.groups.count; at++)
                spent |= link_group_spent(link_nearby.groups.record + at, now);
        if (!spent)
                return;

        lock = link_peers_lock();
        if (lock < 0)
                return;
        if (link_groups_for_change(address_of groups))
        {
                for (positive at = 0; at < groups.count; at++)
                        if (link_group_spent(groups.record + at, now))
                                groups.record[at--] = groups.record[--groups.count];
                (void)link_groups_save(address_of groups);
        }
        link_peers_unlock(lock);
        crypto_forget(address_of groups, sizeof groups);
}

/*
        The group a greeting opened, as the groups file holds it now: the
        record with the key and the namespace of the listener's copy, read
        under the lock every change to the file is made under. The copy is
        read again at most twice a second (link_nearby_reload), so a group
        left, narrowed or given another secret, or a code taken back, stays
        in it for that long after the command that did it has returned; and
        `link group leave NAME forget` forgets the members before then. Kept
        by the copy, a member greeting in between was written back with the
        grants the group no longer gives, for good. So the copy only says
        which keys a greeting may open; what it may become is the file's.
        False when the file cannot be read or no longer holds the group.
*/
static p8 link_group_held(positive group,
                            struct link_group_record address_to held)
{
        struct link_group_record address_to copy =
                link_nearby.groups.record + group;
        link_groups groups;
        positive old;
        p8 found = false;

        if (link_groups_read(address_of groups, address_of old) >= 0)
                for (positive at = 0; at < groups.count && !found; at++)
                        if (crypto_same(groups.record[at].key, copy->key, 32) &&
                            string_equals((string_address)groups.record[at].namespace,
                                          (string_address)copy->namespace))
                        {
                                address_to held = groups.record[at];
                                found = true;
                        }
        crypto_forget(address_of groups, sizeof groups);
        return found;
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
static p8 link_pair_keep(positive group, p8 address_to key,
                           p8 address_to offered, p32 stamped,
                           p8 address_to address, p16 port,
                           p8 address_to accepted)
{
        struct waterlink_group_keys address_to keys = link_nearby.keys + group;
        link_peers peers;
        struct waterlink_peer address_to peer;
        struct link_group_record held;
        bipolar lock = link_peers_lock();
        p8 changed = false;
        p8 new = false;

        struct link_group_record address_to record =
                link_nearby.groups.record + group;
        p8 once;
        p8 open;

        address_to accepted = false;

        //      A member is kept only under the lock: without it the greeting
        //      is as if it had not come, and the next one is asked again. And
        //      only as the group is in the file under that lock, not as the
        //      listener last read it (link_group_held).
        if (lock < 0 || !link_peers_for_change(address_of peers) ||
            !link_group_held(group, address_of held))
        {
                link_peers_unlock(lock);
                return false;
        }
        //      Closed here is closed, whether or not the file could be told.
        once = (held.flags & LINK_GROUP_ONCE) != 0;
        open = !once ||
               (!((held.flags | record->flags) & LINK_GROUP_DONE) &&
                !link_group_spent(address_of held, link_boot_seconds()) &&
                (!held.expect ||
                 held.expect == link_name_check((string_address)offered,
                                                WATERLINK_NAME_MAX)));
        if (!open)
                link_nearby.refused_code = true;
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
                peer->may = held.may;
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
                if (memory_compare(peer->address, address, 16) || peer->port != port)
                        link_place_learn(peer->key, address, port);
                memory_copy(peer->address, address, 16);
                peer->port = port;
                peer->seen = stamped;
                link_keep_heard(peer->key);
        }
        if (changed)
        {
                p8 saved = link_peers_save(address_of peers) >= 0;

                new = saved && new;
                if (!saved)
                        peer = null;
        }
        address_to accepted = peer != null;
        link_peers_unlock(lock);
        crypto_forget(address_of held, sizeof held);
        link_self.state_dirty = true;
        if (once && peer && !(record->flags & LINK_GROUP_DONE))
                link_group_close(group);
        return new;
}

#if !defined(MACOS)
typedef struct
{
        p32 multiaddr;
        p32 address;
        b32 index;
} link_mreqn;
#endif

/*      A socket option set as socket_option_set sets it: zero, or the negative
        errno. On Darwin it is setsockopt from libc, with Darwin's errno, so
        the "already joined" answer of a membership is Darwin's EADDRINUSE. */
static bipolar link_option_set(b32 handle, b32 level, b32 name,
                               address_any value, positive length)
{
#if defined(MACOS)
        if (setsockopt(handle, level, name, value, (socklen_t)length) < 0)
                return -errno;
        return 0;
#else
        return socket_option_set(handle, level, name, value, length);
#endif
}

/*
        Joins the group on one interface, which is named by its index where
        the platform takes ip_mreqn (Linux) and by its IPv4 address where it
        takes only ip_mreq (Darwin). address is the interface's address in host
        order, as network_load_32 gives it. A namespace in a test has no default
        route, so joining on "any" would fail there; one by one works everywhere.
        One without multicast is passed over; joined already (EADDRINUSE) is
        joined.
*/
static fn link_nearby_join(b32 index, p32 address, p32 mask)
{
#if defined(MACOS)
        link_ip_mreq join;
#else
        link_mreqn join = {network_order_32(WATERLINK_MDNS_GROUP), 0, 0};
#endif
        bipolar joined;

        for (positive at = 0; at < link_nearby.interfaces; at++)
                if (link_nearby.interface[at] == index)
                        return;
        if (link_nearby.interfaces == LINK_INTERFACES)
                return;
#if defined(MACOS)
        join.multiaddr = network_order_32(WATERLINK_MDNS_GROUP);
        join.interface = network_order_32(address);
#else
        join.index = index;
#endif
        joined = link_option_set((b32)link_nearby.socket, IPPROTO_IP,
                                 IP_ADD_MEMBERSHIP, address_of join,
                                 sizeof join);
        if (joined < 0 && joined != -EADDRINUSE)
                return;
        link_nearby.interface[link_nearby.interfaces] = index;
        link_nearby.interface_address[link_nearby.interfaces] = address;
        link_nearby.interface_mask[link_nearby.interfaces] = mask;
        link_nearby.interfaces++;
}

#if !defined(MACOS)
/* The netmask of a prefix length, in host order; 0 for none. */
static p32 link_prefix_mask(p8 prefix)
{
        if (!prefix || prefix > 32)
                return 0;
        return 0xffffffffu << (32 - prefix);
}

/* The netlink walk's callback: one address of the dump, IPv4 and with a local address. */
static p8 link_nearby_address(netlink_header address_to header,
                                address_any context)
{
        netlink_address address_to body =
            netlink_message_body(header, sizeof(netlink_address));
        positive size = 0;
        p8 address_to host;

        (void)context;
        if (header->type != RTM_NEWADDR || !body ||
            link_nearby.interfaces == LINK_INTERFACES)
                return true;
        host = netlink_find(header, sizeof(netlink_address), IFA_LOCAL,
                             address_of size);
        if (body->family != AF_INET || !host || size < 4)
                return true;
        link_nearby_join((b32)body->index, network_load_32(host),
                         link_prefix_mask(body->prefix));
        return true;
}
#endif

/*
        Every interface with an IPv4 address that takes the membership, and
        that address for the A record: net.c's walk of the addresses, which
        is the one the network setup already trusts. Linux walks them with
        netlink; Darwin has no netlink, and walks them with getifaddrs, which
        is the BSD routine for the same list.
*/
#if defined(MACOS)
static fn link_nearby_interfaces(void)
{
        struct ifaddrs *list = null;

        link_nearby.interfaces = 0;
        if (getifaddrs(&list) < 0)
                return;
        for (struct ifaddrs *entry = list; entry; entry = entry->ifa_next)
        {
                p8 address_to raw;
                p32 mask = 0;
                b32 index;

                if (!entry->ifa_addr || entry->ifa_addr->sa_family != AF_INET)
                        continue;
                index = (b32)if_nametoindex(entry->ifa_name);
                if (!index)
                        continue;
                //      A sockaddr_in is sin_len, sin_family, sin_port, then the
                //      address in network order at offset 4, and its netmask
                //      the same way.
                raw = (p8 address_to)(void address_to)entry->ifa_addr;
                if (entry->ifa_netmask && entry->ifa_netmask->sa_family == AF_INET)
                        mask = network_load_32((p8 address_to)(void address_to)
                                                       entry->ifa_netmask + 4);
                link_nearby_join(index, network_load_32(raw + 4), mask);
        }
        freeifaddrs(list);
}
#else
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
#endif

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
#if !defined(MACOS)
        socket_address_internet self = {
            .family = AF_INET, .port = network_order_16(WATERLINK_MDNS_PORT)};
#endif

#if defined(MACOS)
        //      A datagram socket from libc, made close-on-exec and nonblocking
        //      by fcntl, since Darwin has no SOCK_* flags on socket().
        link_nearby.socket = socket(AF_INET, SOCK_DGRAM, 0);
        if (link_nearby.socket >= 0)
        {
                (void)fcntl(link_nearby.socket, F_SETFD, FD_CLOEXEC);
                (void)fcntl(link_nearby.socket, F_SETFL, O_NONBLOCK);
        }
#else
        link_nearby.socket = socket_new(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC |
                                                        SOCK_NONBLOCK,
                                        0);
#endif
        if (link_nearby.socket < 0)
                return;
        (void)link_option_set((b32)link_nearby.socket, SOL_SOCKET,
                              SO_REUSEADDR, address_of one, sizeof one);
        (void)link_option_set((b32)link_nearby.socket, SOL_SOCKET,
                              SO_REUSEPORT, address_of one, sizeof one);
        (void)link_option_set((b32)link_nearby.socket, IPPROTO_IP,
                              IP_MULTICAST_TTL, address_of ttl, sizeof ttl);
        (void)link_option_set((b32)link_nearby.socket, IPPROTO_IP,
                              IP_RECVTTL, address_of one, sizeof one);
        (void)link_option_set((b32)link_nearby.socket, IPPROTO_IP,
                              IP_PKTINFO, address_of one, sizeof one);

#if defined(MACOS)
        {
                struct sockaddr_in local;

                memory_zero(address_of local, sizeof local);
                local.sin_len = (unsigned char)sizeof local;
                local.sin_family = AF_INET;
                local.sin_port = network_order_16(WATERLINK_MDNS_PORT);
                if (bind((b32)link_nearby.socket, (struct sockaddr *)address_of local,
                         sizeof local) < 0)
                {
                        link_nearby_close();
                        return;
                }
        }
#else
        if (socket_bind((b32)link_nearby.socket, address_of self,
                        sizeof self) < 0)
        {
                link_nearby_close();
                return;
        }
#endif
        link_nearby_interfaces();
}

/*
        A read of the groups file that says nothing about what it holds: not
        the file's absence, nor a file that is not what a groups file is (not
        root's, private and whole, or a link), which are no groups, but a
        read that failed for itself -- the disk, no descriptor, no memory.
*/
static p8 link_groups_unknown(bipolar read)
{
        return read < 0 && read != -ENOENT && read != -EPERM && read != -ELOOP;
}

/*
        The sweep, for a code that waits. Multicast does not cross every
        network: a Wi-Fi or a virtual network may drop it while it carries
        unicast, and a code's two machines then never hear each other. So
        while a code waits, its greeting, the group's ordinary first message,
        is sent from the listener's own port to every host of this machine's
        subnets, one at a time. A member that opens it keeps the sender and
        greets back exactly as it does for a multicast announcement; a machine
        outside the group drops it at the first gate. Only codes sweep: a
        group that is not a code is found by its multicast, as before.

        The sweep is bounded: four a code, LINK_SWEEP_HOSTS addresses each at
        most, paced, and none once the code is used or gone. A sweep is about
        a kilobyte and a quarter for each host, and about eight seconds.
*/

/* The first group that is a code still waiting for its machine, or the
   number of groups when there is none. */
static positive link_sweep_waiting(void)
{
        for (positive group = 0; group < link_nearby.groups.count; group++)
        {
                p32 flags = link_nearby.groups.record[group].flags;

                if ((flags & LINK_GROUP_ONCE) && !(flags & LINK_GROUP_DONE))
                        return group;
        }
        return link_nearby.groups.count;
}

/* The hosts to sweep: every host of every interface's subnet, a /24 at the
   most around the interface's address and never the address itself. Returns
   how many were put in sweep_target. */
static positive link_sweep_fill(void)
{
        positive count = 0;

        for (positive at = 0; at < link_nearby.interfaces; at++)
        {
                p32 address = link_nearby.interface_address[at];
                p32 mask = link_nearby.interface_mask[at];
                p32 network;
                p32 last;

                if (!mask)
                        continue;
                if (mask < 0xffffff00u)
                        mask = 0xffffff00u;
                network = address & mask;
                last = network | ~mask;
                for (p32 host = network + 1; host < last && count < LINK_SWEEP_HOSTS;
                     host++)
                        if (host != address)
                                link_nearby.sweep_target[count++] = host;
        }
        return count;
}

/* The greeting a sweep sends, made once for the group, as link_pair_begin
   makes one: the first message to the group's key, from this machine's key
   and with its name. False when it could not be made. */
static p8 link_sweep_make(positive group)
{
        struct waterlink_group_keys address_to keys = link_nearby.keys + group;
        struct waterlink_noise noise;
        p8 hello[WATERLINK_HELLO_BYTES];
        p8 ephemeral[32];
        p64 wall = system_clock_ns(0);
        p8 made = false;

        if (system_random_fill(ephemeral, 32, 0) < 0)
                return false;
        waterlink_stamp(hello, wall / 1000000000ull,
                        (p32)(wall % 1000000000ull));
        memory_copy(hello + WATERLINK_STAMP_BYTES, link_nearby.name,
                    WATERLINK_NAME_MAX);
        made = waterlink_initiate(address_of noise, address_of link_self.me,
                                  keys->identity.public, keys->psk, ephemeral,
                                  hello, link_nearby.sweep_datagram);
        crypto_forget(ephemeral, sizeof ephemeral);
        crypto_forget(address_of noise, sizeof noise);
        return made;
}

/* When the next sweep is due: the gaps grow, and none comes after the last. */
static p64 link_sweep_later(p64 now)
{
        static const p64 gaps[LINK_SWEEPS - 1] = {15000000ull, 30000000ull,
                                                  60000000ull};

        if (!link_nearby.sweep_left)
                return 0;
        link_nearby.sweep_next = now + gaps[LINK_SWEEPS - 1 - link_nearby.sweep_left];
        return link_nearby.sweep_next;
}

/*
        One step of the sweep, and when the next is due: nothing while no code
        waits; a sweep begun when its time comes; LINK_SWEEP_BATCH addresses
        at a step. Returns the time of the next step, or 0 for none.
*/
static p64 link_sweep_turn(p64 now)
{
        positive group = link_sweep_waiting();

        if (group == link_nearby.groups.count || link_self.socket < 0 ||
            (link_nearby.sweeping && group != link_nearby.sweep_group))
        {
                //      The code is used or gone, or the listener has no port:
                //      what is left of the sweeps is not sent.
                link_nearby.sweeping = false;
                link_nearby.sweep_left = 0;
                return 0;
        }
        if (!link_nearby.sweeping)
        {
                if (!link_nearby.sweep_left)
                        return 0;
                if (now < link_nearby.sweep_next)
                        return link_nearby.sweep_next;
                link_nearby.sweep_left--;
                link_nearby.sweep_count = link_sweep_fill();
                if (!link_nearby.sweep_count || !link_sweep_make(group))
                        return link_sweep_later(now);
                link_nearby.sweeping = true;
                link_nearby.sweep_group = group;
                link_nearby.sweep_at = 0;
        }
        for (positive sent = 0; sent < LINK_SWEEP_BATCH &&
                                link_nearby.sweep_at < link_nearby.sweep_count;
             sent++)
        {
                p8 address[16];

                link_address_v4(address,
                                link_nearby.sweep_target[link_nearby.sweep_at++]);
                (void)link_udp_send_to(link_nearby.sweep_datagram,
                                       WATERLINK_DATAGRAM, address, link_port());
        }
        if (link_nearby.sweep_at < link_nearby.sweep_count)
                return now + LINK_SWEEP_STEP;
        link_nearby.sweeping = false;
        return link_sweep_later(now);
}

/*
        The groups as the file says now, when it has changed: keys derived
        (cheaply -- the slow derivation happened at join), labels drawn, the
        socket opened for the first group and closed after the last. A read
        that failed for itself leaves the groups last read as they are, and
        the next look asks again.
*/
static fn link_nearby_reload(p64 now)
{
        link_groups fresh;
        file_facts facts;
        positive old = 0;
        bipolar read;
        p64 mark[6] = {0};

        if (link_age(now, link_nearby.expiry_look) >= 2000000)
        {
                link_nearby.expiry_look = now;
                link_groups_expire();
        }

        //      Twice a second at most. Every save is a rename, so the inode
        //      says whether the file is the one already read -- unless the
        //      inode is one the save before freed and the same records make
        //      it the same size, which the times it was changed and written,
        //      to the nanosecond, tell apart.
        if (link_nearby.looked && link_age(now, link_nearby.looked) < 500000)
                return;
        link_nearby.looked = now;
        if (file_look_at(LINK_GROUPS_PATH, address_of facts))
        {
                mark[0] = facts.inode;
                mark[1] = facts.size;
                mark[2] = (p64)facts.changed.seconds;
                mark[3] = facts.changed.nanoseconds;
                mark[4] = (p64)facts.modified.seconds;
                mark[5] = facts.modified.nanoseconds;
        }
        if (!memory_compare(mark, link_nearby.groups_mark, sizeof mark))
                return;
        memory_copy(link_nearby.groups_mark, mark, sizeof mark);

        read = link_groups_read(address_of fresh, address_of old);
        if (link_groups_unknown(read))
        {
                //      A mark no file has, so that the next look reads it
                //      again even if the file is gone by then.
                crypto_forget(address_of fresh, sizeof fresh);
                memory_fill(link_nearby.groups_mark, 0xff, sizeof mark);
                return;
        }
        if (old && !link_groups_scrub(old))
        {
                /* Do not activate a secret whose fast verifier could not be
                   removed durably; the next look retries the migration. */
                crypto_forget(address_of fresh, sizeof fresh);
                memory_zero(link_nearby.groups_mark, sizeof mark);
        }
        link_nearby.groups = fresh;
        crypto_forget(address_of fresh, sizeof fresh);
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
        //      Deferred places index the groups as they were: none survives a change.
        memory_zero(link_nearby.pending, sizeof link_nearby.pending);
        link_nearby.announced = 0;
        link_nearby.asked = 0;
        link_nearby.next_announce = now;
        link_nearby.next_ask = now;
        link_nearby.sweeping = false;
        link_nearby.sweep_left = link_sweep_waiting() < link_nearby.groups.count
                                         ? LINK_SWEEPS
                                         : 0;
        link_nearby.sweep_next = now;
}

/*      The outcome of one send, from the sender's answer (its length, or a
        negative errno). Whichever way it went, the last send says whether it
        found no route, which is what discovery reports as no route. */
static p8 link_nearby_outcome(bipolar sent, positive length)
{
        link_nearby.no_route = sent == -ENETUNREACH || sent == -EHOSTUNREACH ||
                               sent == -ENETDOWN;
        return sent == (bipolar)length;
}

static p8 link_nearby_send(p8 address_to packet, positive length,
                             positive interface_at)
{
#if defined(MACOS)
        /* Darwin names the interface by its address, in network order, and
           its sockaddr_in carries a length first. Both are built here, for
           libc, since the lib's socket address is the Linux layout. */
        struct in_addr out;
        struct sockaddr_in to;

        memory_zero(address_of to, sizeof to);
        to.sin_len = (unsigned char)sizeof to;
        to.sin_family = AF_INET;
        to.sin_port = network_order_16(WATERLINK_MDNS_PORT);
        to.sin_addr.s_addr = network_order_32(WATERLINK_MDNS_GROUP);
        out.s_addr = network_order_32(link_nearby.interface_address[interface_at]);

        /* IP_MULTICAST_IF is part of the destination, not a best-effort hint.
           If it fails, send would retain the preceding interface selection
           and put this interface's announcement onto another link. */
        bipolar set = link_option_set((b32)link_nearby.socket, IPPROTO_IP,
                                      IP_MULTICAST_IF, address_of out, sizeof out);
        ssize_t sent;

        if (set < 0)
                return link_nearby_outcome(set, length);
        sent = sendto((b32)link_nearby.socket, packet, length, 0,
                      (struct sockaddr *)address_of to, sizeof to);
        return link_nearby_outcome(sent < 0 ? -errno : (bipolar)sent, length);
#else
        link_mreqn out = {0, 0, link_nearby.interface[interface_at]};
        socket_address_internet to = {
            .family = AF_INET, .port = network_order_16(WATERLINK_MDNS_PORT),
            .host = network_order_32(WATERLINK_MDNS_GROUP)};

        /* IP_MULTICAST_IF is part of the destination, not a best-effort hint.
           If it fails, send would retain the preceding interface selection
           and put this interface's announcement onto another link. */
        bipolar set = socket_option_set((b32)link_nearby.socket, IPPROTO_IP,
                                        IP_MULTICAST_IF, address_of out, sizeof out);

        if (set < 0)
                return link_nearby_outcome(set, length);
        return link_nearby_outcome(
            socket_send((b32)link_nearby.socket, packet, length, MSG_NOSIGNAL,
                        address_of to, sizeof to),
            length);
#endif
}

/*
        Why discovery has not linked a machine, as a number for a status or
        doctor to keep and the words it prints. Linked is a greeting that
        opened; a one-use code that turned one away is a code refused; a send
        that found no route is no route; anything else is no reply on the port
        (discovery hears nothing it can answer, and cannot tell a machine that
        is off from one that does not know this key). Bad key is never said
        here: discovery never reads a reply, and the reply that does not verify
        is the client's to say.
*/
static __attribute__((unused)) positive link_discovery_reason(void)
{
        if (link_nearby.linked)
                return LINK_REASON_LINKED;
        if (link_nearby.refused_code)
                return LINK_REASON_CODE_REFUSED;
        if (link_nearby.no_route)
                return LINK_REASON_NO_ROUTE;
        return LINK_REASON_NO_REPLY;
}

/* The words for a reason, as the link's other reasons print them. */
static __attribute__((unused)) string_address link_discovery_reason_text(positive reason)
{
        static const string_address texts[] = {
            "linked",
            "no route to the address",
            "no reply on the link port (it is off, unreachable, or does not know this machine's key)",
            "reply but bad key: the machine there is not the one paired",
            "code refused: it was used already, or by another machine",
        };

        return reason < array_count(texts) ? (string_address)texts[reason] : (string_address)"unknown reason";
}

static p8 link_nearby_announce(p32 ttl)
{
        p8 packet[WATERLINK_MDNS_MAX];
        p8 sent = false;

        for (positive at = 0; at < link_nearby.interfaces; at++)
        {
                positive length = waterlink_mdns_announce(
                        packet, sizeof packet, link_nearby.instance,
                        link_nearby.host, link_port(),
                        link_nearby.interface_address[at], ttl, 0, null, 0);

                if (length)
                        sent |= link_nearby_send(packet, length, at);
        }
        return sent;
}

static fn link_nearby_ask(void)
{
        p8 packet[64];
        positive length = waterlink_mdns_query(packet, sizeof packet);

        for (positive at = 0; at < link_nearby.interfaces; at++)
                link_nearby_send(packet, length, at);
}

// Goodbye, with TTL zero, when the listener stops.
static COLD fn link_mdns_end(void);

static fn link_nearby_stop(void)
{
        if (link_nearby.socket >= 0 && link_nearby.groups.count)
                link_nearby_announce(0);
        link_nearby_close();
        link_mdns_end();
}

// The same place is greeted once in a while, not at every announcement.
/* How many greetings of this address are still inside LINK_GREET_AGAIN; with
   exact, only those to this group and port. */
static positive link_greeted_recent(p8 address_to address, p32 group, p16 port,
                                    p8 exact, p64 now)
{
        positive recent = 0;

        for (positive at = 0; at < LINK_GREETED; at++)
        {
                struct link_greeted address_to greeted = link_nearby.greeted + at;

                recent += greeted->at &&
                          link_age(now, greeted->at) < LINK_GREET_AGAIN &&
                          (!exact || (greeted->group == group &&
                                      greeted->port == port)) &&
                          !memory_compare(greeted->address, address, 16);
        }
        return recent;
}

/* An announcement may start a greeting when the ring has a slot that has aged
   out, and the source has not already spent LINK_GREET_SOURCE of them. An
   unauthenticated address may not consume the whole machine-wide ring: this
   keeps enough private capacity to greet one machine in every possible group
   and leaves half the ring for another source. Raw address rotation still
   meets the global bound. */
static p8 link_greet_allowed(p8 address_to address, p64 now)
{
        p64 oldest = link_nearby.greeted[link_nearby.greeted_next].at;

        return (!oldest || link_age(now, oldest) >= LINK_GREET_AGAIN) &&
               link_greeted_recent(address, 0, 0, false, now) <
                   LINK_GREET_SOURCE;
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
                          p64 now, p8 answer)
{
        struct waterlink_group_keys address_to keys = link_nearby.keys + group;
        struct waterlink_noise noise;
        p8 datagram[WATERLINK_DATAGRAM];
        p8 hello[WATERLINK_HELLO_BYTES];
        p8 ephemeral[32];
        p64 wall = system_clock_ns(0);

        if ((!answer && link_greeted_recent(address, keys->mark, port, true, now)) ||
            system_random_fill(ephemeral, 32, 0) < 0)
                return;
        waterlink_stamp(hello, wall / 1000000000ull,
                        (p32)(wall % 1000000000ull));
        memory_copy(hello + WATERLINK_STAMP_BYTES, link_nearby.name,
                    WATERLINK_NAME_MAX);
        //      Counted whether or not it could be sent: the curve work is
        //      done, and a place that refuses it is neither greeted again at
        //      once nor outside the budget.
        if (waterlink_initiate(address_of noise, address_of link_self.me,
                               keys->identity.public, keys->psk, ephemeral,
                               hello, datagram))
        {
                struct link_greeted address_to next =
                        link_nearby.greeted + link_nearby.greeted_next;

                (void)link_udp_send_to(datagram, WATERLINK_DATAGRAM, address, port);
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

/*
        A place an announcement came from, deferred behind the member's
        last-heard place (link_pending). With the table full nothing is
        deferred, and the next announcement from the place asks again.
*/
static fn link_pair_defer(positive group, struct waterlink_peer address_to peer,
                          p8 address_to heard, p16 port, p64 now)
{
        struct link_pending address_to slot = null;

        for (positive at = 0; at < LINK_PENDING && !slot; at++)
                if (!link_nearby.pending[at].used)
                        slot = link_nearby.pending + at;
        if (!slot)
                return;
        memory_copy(slot->key, peer->key, 32);
        memory_copy(slot->last, peer->address, 16);
        slot->last_port = peer->port;
        slot->seen = peer->seen;
        memory_copy(slot->heard, heard, 16);
        slot->heard_port = port;
        slot->group = group;
        slot->due = now + LINK_PREFER_WAIT;
        slot->used = true;
}

/*
        A deferred place whose time is up: the member's record says whether its
        last-heard place answered, which it did if the record has moved or been
        stamped since the place was greeted. If it did not, the place the
        announcement came from is greeted now. A record that has gone since is
        not greeted for.
*/
static fn link_pending_due(struct link_pending address_to slot, p64 now)
{
        link_peers peers;
        p8 silent = false;

        link_peers_load(address_of peers);
        for (positive at = 0; at < peers.count; at++)
        {
                struct waterlink_peer address_to peer = peers.peer + at;

                if (!crypto_same(peer->key, slot->key, 32))
                        continue;
                silent = !memory_compare(peer->address, slot->last, 16) &&
                         peer->port == slot->last_port && peer->seen == slot->seen;
                break;
        }
        if (silent && link_greet_allowed(slot->heard, now))
                link_pair_begin(slot->group, slot->heard, slot->heard_port, now,
                                false);
}

/*
        The deferred places that are due, and when the next one is (0 for
        none). Each is looked at once and dropped, greeted or not.
*/
static p64 link_pending_turn(p64 now)
{
        p64 next = 0;

        for (positive at = 0; at < LINK_PENDING; at++)
        {
                struct link_pending address_to slot = link_nearby.pending + at;

                if (!slot->used)
                        continue;
                if (now < slot->due)
                {
                        if (!next || slot->due < next)
                                next = slot->due;
                        continue;
                }
                slot->used = false;
                link_pending_due(slot, now);
        }
        return next;
}

/*
        A greeting says when its sender sent it, and one that is not about now
        is a recording played back: from a member that was forgotten, and
        whose marker has been dropped since, it would keep that member again,
        at the address of whoever played it, with what the group grants. So
        once this machine's clock reads a time -- a machine that was never
        told it reads 1970 and its uptime, and the floor is a date after this
        was written -- a greeting to a group that stays is dated within
        LINK_GREET_FRESH seconds of it, either way. A code is single use and
        its own end bounds what is played back, and a machine that has no
        time to give is greeted again when it has: its announcement is heard
        every minute.
*/
#define LINK_CLOCK_FLOOR 1767225600ull // 2026-01-01
#define LINK_GREET_FRESH 3600

static p8 link_greeting_fresh(positive group, p8 address_to hello)
{
        p64 wall = system_clock_ns(0) / 1000000000ull;
        p64 sent = network_load_64(hello) - (1ull << 62);

        return (link_nearby.groups.record[group].flags & LINK_GROUP_ONCE) ||
               wall < LINK_CLOCK_FLOOR ||
               (link_age(wall, sent) <= LINK_GREET_FRESH &&
                link_age(sent, wall) <= LINK_GREET_FRESH);
}

//      A greeting that opened: the member is kept, and greeted back if new.
static p8 link_pair_greeted(positive group, p8 address_to key,
                              p8 address_to hello, p8 address_to address,
                              p16 port, p64 now)
{
        p8 accepted;

        if (!link_greeting_fresh(group, hello))
                return false;
        if (link_pair_keep(group, key, hello + WATERLINK_STAMP_BYTES,
                           network_load_32(hello + 4), address, port,
                           address_of accepted))
                link_pair_begin(group, address, port, now, true);
        if (accepted)
                link_nearby.linked = true;
        return accepted;
}

/*
        The mDNS reader, apart.

        Every host on the link may send this listener mDNS, and reading it is a
        walk of names, compression pointers and records of the sender's own
        lengths (waterlink_mdns_read). The listener is root, keeps the link's
        key and runs commands for linked machines, so the walk runs in a
        reader of its own: a child that is nobody, cannot be dumped, dies with
        the listener, holds only its end of a socket of whole messages and
        stdio, and kills itself at any call but reading a packet from it and
        sending the reading back (link_mdns_filter_build). What it answers is
        the fixed-size waterlink_found, believed only with every count and
        length inside its array. A reader that cannot be confined (no seccomp,
        qemu-user) reads nothing: the listener then hears no machine nearby,
        which is the link without discovery, not discovery unconfined. One
        that dies or answers what cannot be believed is ended and started
        again for the next packet.

        Linux confines the child with seccomp (net.c's filter, net_apart_nobody
        and net_apart_tie). Darwin has no seccomp, so its child is confined by
        Seatbelt (link_mdns_confine, below). The two differ in that one
        function and in the socket kind the pair is made with; everything
        else in the reader, the parent's side included, is the same code on
        both systems. A child that cannot be confined on either reads
        nothing, so a Mac that cannot confine it hears no machine nearby,
        as a Linux box without seccomp does.
*/
#define LINK_MDNS_SOCK_SEQPACKET 5
#define LINK_MDNS_ANSWER_SECONDS 1
/* Darwin's unix socket pairs have no SOCK_SEQPACKET; a datagram is a whole
   message there too, and it is the kind the pair is made with on that system. */
#if defined(MACOS)
#define LINK_MDNS_SOCK_KIND 2 /* SOCK_DGRAM */
#define LINK_MDNS_DESCRIPTORS 1024 /* the child closes these bar its channel */
#else
#define LINK_MDNS_SOCK_KIND (LINK_MDNS_SOCK_SEQPACKET | SOCK_CLOEXEC)
#endif

struct link_mdns_said
{
        b64 read;
        struct waterlink_found found;
};

static struct
{
        b32 channel;
        bipolar pid;
        p8 refused;
} link_mdns = {-1, -1, false};

#if !defined(MACOS)
static COLD fn link_mdns_filter_build(net_filter address_to filter)
{
        static const p32 plain[] = {
            (p32)syscall(recvfrom), (p32)syscall(sendto), (p32)syscall(ppoll),
            (p32)syscall(clock_gettime), (p32)syscall(munmap), (p32)syscall(madvise),
            (p32)syscall(exit_group), (p32)syscall(exit), (p32)syscall(rt_sigreturn),
            (p32)syscall(restart_syscall),
#ifdef NET_APART_RISCV_HWPROBE
            NET_APART_RISCV_HWPROBE,
#endif
        };
        positive count = array_count(plain);

        filter->used = 0;
        net_filter_put(filter, NET_FILTER_LOAD, 0, 0, NET_FILTER_ARCH);
        net_filter_put(filter, NET_FILTER_EQUAL, 1, 0, NET_APART_ARCH);
        net_filter_answer(filter, SECCOMP_RET_KILL_PROCESS);
        net_filter_put(filter, NET_FILTER_LOAD, 0, 0, NET_FILTER_NUMBER);
        for (positive at = 0; at < count; at++)
                net_filter_put(filter, NET_FILTER_EQUAL, (p8)(count - at), 0, plain[at]);
        net_filter_answer(filter, SECCOMP_RET_KILL_PROCESS);
        net_filter_answer(filter, SECCOMP_RET_ALLOW);
}

/*      The child's confinement: seccomp, and the descriptors but the channel
        closed, the child made nobody's. False when any of it did not take,
        which is the child's exit to say. */
static COLD bool link_mdns_confine(b32 channel, bipolar parent)
{
        p32 kept[1] = {(p32)channel};
        net_filter filter;

        link_mdns_filter_build(address_of filter);
        return descriptors_close_outside(3, kept, 1) && net_apart_nobody() &&
               net_apart_tie(parent) && net_filter_install(address_of filter);
}
#else
/*      Darwin's child: Seatbelt, which is what Darwin has in place of
        seccomp. sandbox_init is libsystem's; it is declared here and not taken
        from <sandbox.h>, whose deprecation would reach the build. The profile
        denies everything, and allows only what a read of a packet is made of
        (the sysctls libc consults and the metadata of files it already has
        open). The descriptors are closed first, since a profile does not close
        what is open: the channel alone is kept. A profile that does not take,
        or a descriptor that will not close, leaves the child unconfined and it
        exits so, as on Linux. */
extern int sandbox_init(const char *profile, unsigned long long flags, char **error);

#define LINK_MDNS_PROFILE                                                    \
        "(version 1)(deny default)(allow sysctl-read)(allow file-read-metadata)"

static COLD bool link_mdns_confine(b32 channel, bipolar parent)
{
        char *error = null;

        (void)parent;
        for (positive fd = 3; fd < LINK_MDNS_DESCRIPTORS; fd++)
                if (fd != (positive)channel)
                        (void)system_call_1(syscall(close), fd);
        return sandbox_init(LINK_MDNS_PROFILE, 0, &error) == 0;
}
#endif

static DEAD_END COLD fn link_mdns_child(b32 channel, bipolar parent)
{
        if (!link_mdns_confine(channel, parent))
                system_call_1(syscall(exit_group), NET_APART_EXIT_UNCONFINED);
        for (;;)
        {
                p8 packet[WATERLINK_MDNS_MAX + 1];
                struct link_mdns_said said;
                network_deadline deadline;
                bipolar ready;
                bipolar got;

                if (!network_deadline_begin(address_of deadline, 3600, 0))
                        break;
                ready = network_wait_readable_until(channel, address_of deadline);
                if (!ready)
                        continue;
                got = ready < 0 ? -1
                                : socket_receive(channel, packet, sizeof packet, MSG_DONTWAIT,
                                                 null, null);
                if (got <= 0)
                        break;
                memory_zero(address_of said, sizeof said);
                said.read = waterlink_mdns_read(packet, (positive)got, address_of said.found);
                if (socket_send(channel, address_of said, sizeof said, MSG_NOSIGNAL, null, 0) !=
                    (bipolar)sizeof said)
                        break;
        }
        system_call_1(syscall(exit_group), 0);
        unreachable_here();
}

static COLD fn link_mdns_end(void)
{
        positive raw = 0;

        if (link_mdns.channel >= 0)
                system_close(link_mdns.channel);
        if (link_mdns.pid > 0)
        {
                system_call_2(syscall(kill), (positive)link_mdns.pid, SIGKILL);
                (void)system_wait4_retry(link_mdns.pid, address_of raw, 0, null);
        }
        link_mdns.channel = -1;
        link_mdns.pid = -1;
}

static COLD p8 link_mdns_start(void)
{
        b32 ends[2];
        bipolar parent = system_call(syscall(getpid));
        bipolar child;

        if (system_call_4(syscall(socketpair), AF_UNIX, LINK_MDNS_SOCK_KIND, 0,
                          (positive)ends) < 0)
                return false;
        child = system_fork();
        if (!child)
        {
                system_close(ends[0]);
                link_mdns_child(ends[1], parent);
        }
        system_close(ends[1]);
        if (child < 0)
        {
                system_close(ends[0]);
                return false;
        }
        link_mdns.channel = ends[0];
        link_mdns.pid = child;
        return true;
}

static PURE COLD p8 link_mdns_believed(const struct waterlink_found address_to found)
{
        if (found->count > WATERLINK_FOUND_MAX ||
            found->question_length > sizeof(found->question))
                return false;
        for (positive at = 0; at < found->count; at++)
                if (found->instance[at].label_length > sizeof(found->instance[at].label))
                        return false;
        return true;
}

/* waterlink_mdns_read, apart: the reading the reader made, or false. */
static COLD p8 link_mdns_read_apart(const p8 address_to packet, positive length,
                                      struct waterlink_found address_to found)
{
        struct link_mdns_said said;
        network_deadline deadline;

        memory_zero(found, sizeof(*found));
        if (link_mdns.refused || length > WATERLINK_MDNS_MAX)
                return false;
        if (link_mdns.channel < 0 && !link_mdns_start())
                return false;
        if (socket_send(link_mdns.channel, packet, length, MSG_NOSIGNAL | MSG_DONTWAIT, null,
                        0) != (bipolar)length ||
            !network_deadline_begin(address_of deadline, LINK_MDNS_ANSWER_SECONDS, 0) ||
            network_wait_readable_until(link_mdns.channel, address_of deadline) <= 0 ||
            socket_receive(link_mdns.channel, address_of said, sizeof said, MSG_DONTWAIT, null,
                           null) != (bipolar)sizeof said ||
            !link_mdns_believed(address_of said.found))
        {
                positive raw = 0;

                //      A reader that could not be confined says so by its exit,
                //      once: there is no reading here from then on.
                if (link_mdns.pid > 0 &&
                    system_wait4_retry(link_mdns.pid, address_of raw, 1 /* WNOHANG */, null) ==
                        link_mdns.pid &&
                    (raw & 0x7f) == 0 && ((raw >> 8) & 0xff) == NET_APART_EXIT_UNCONFINED)
                {
                        link_mdns.pid = -1;
                        link_mdns.refused = true;
                }
                link_mdns_end();
                return false;
        }
        address_to found = said.found;
        return said.read != 0;
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

        if (!link_mdns_read_apart(packet, length, address_of found))
                return;

        if (found.asked && link_nearby.groups.count)
        {
                //      Unicast to wherever the packet says it came from, which
                //      nobody checked: a few a second, or a stream of spoofed
                //      questions is a stream of answers four times their size
                //      at somebody else.
                if (source_port && source_port != WATERLINK_MDNS_PORT &&
                    found.question_length)
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

#if defined(MACOS)
                        to.length = sizeof to;
#endif
                        if (reply_length &&
                            link_age(now, link_nearby.last_reply) >= LINK_ANSWER_AGAIN)
                        {
                                /* Charge the shared amplification budget only
                                   when a reply actually left.  Source port
                                   zero is refused above: a raw same-link
                                   sender can forge it, but Linux cannot send
                                   the reply it requests.  Advancing last_reply
                                   before send let packets which produced no
                                   answer suppress every honest legacy-unicast
                                   mDNS answer.  A successful spoofed reflection
                                   is still held by this global budget; a failed
                                   reflection gets no authority over it. */
                                bipolar sent = socket_send(
                                    (b32)link_nearby.socket, reply,
                                    reply_length, MSG_NOSIGNAL,
                                    address_of to, sizeof to);

                                if (sent == (bipolar)reply_length)
                                        link_nearby.last_reply = now;
                        }
                }
                else if (source_port == WATERLINK_MDNS_PORT &&
                         link_age(now, link_nearby.last_answer) > 1000000)
                {
                        /* As with legacy-unicast replies above, a refused
                           send has emitted no amplification and owns no part
                           of the shared answer budget. */
                        if (link_nearby_announce(4500))
                                link_nearby.last_answer = now;
                }
        }

        /* RFC 6762 6: a response from any port but 5353 is not mDNS, and
           what it announces is not greeted (Avahi drops it the same way). */
        if (source_port != WATERLINK_MDNS_PORT)
                found.count = 0;
        if (found.count)
                link_peers_load(address_of peers);
        for (positive at = 0; at < found.count; at++)
        {
                struct waterlink_found_instance address_to instance =
                        found.instance + at;
                p8 ours[23];

                //      Our own, back through the loop. A port of zero is
                //      nowhere to greet, and no datagram goes there.
                memory_copy(ours, "wl-", 3);
                memory_into_hex(ours + 3, link_nearby.instance, 10);
                if (!instance->has_port || !instance->port ||
                    (instance->label_length == sizeof ours &&
                     !memory_compare(instance->label, ours, sizeof ours)))
                        continue;

                for (positive group = 0; group < link_nearby.groups.count; group++)
                {
                        p8 known = false;
                        struct waterlink_peer address_to last = null;
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

                        //      A member of this group on this port, heard at the
                        //      place the announcement came from (known), or at
                        //      another (last): the first such one is kept.
                        for (positive p = 0; p < peers.count && !known; p++)
                        {
                                struct waterlink_peer address_to peer = peers.peer + p;

                                if (peer->group != link_nearby.keys[group].mark ||
                                    peer->port != instance->port)
                                        continue;
                                if (!memory_compare(peer->address, address, 16))
                                        known = true;
                                else if (!last)
                                        last = peer;
                        }
                        if (known)
                                continue;
                        //      The last-heard place of a known member is greeted
                        //      first, and the place the announcement came from
                        //      only if that one does not answer (link_pending).
                        //      Anyone may announce, naming any port, as many as
                        //      a packet holds: the ring of places greeted is
                        //      also the budget, LINK_GREETED greetings in
                        //      LINK_GREET_AGAIN, so a made-up announcement
                        //      cannot turn this machine into a sprayer of
                        //      handshakes. A greeting back to a member whose
                        //      own greeting opened is not held to it.
                        if (last)
                        {
                                if (link_greet_allowed(last->address, now))
                                        link_pair_begin(group, last->address,
                                                        last->port, now, false);
                                link_pair_defer(group, last, address,
                                                instance->port, now);
                        }
                        else if (link_greet_allowed(address, now))
                                link_pair_begin(group, address, instance->port,
                                                now, false);
                }
                //      A machine announces the one instance that is it, so the
                //      first a packet names is where it is greeted: a packet
                //      that names eight is eight handshakes to the address it
                //      came from, which a spoofed one made somebody else's.
                break;
        }
}

#if !defined(MACOS)
/* The local IPv4 address the kernel says received this packet. IP_PKTINFO's
   data is ifindex, spec_dst and the header's destination, each four bytes;
   spec_dst is the local address selected for the incoming packet. */
static p32 link_nearby_local(p8 address_to control, positive length,
                             positive room)
{
        byte_reader data;

        //      ifindex, spec_dst, then the header's destination: each four.
        if (!link_control_find(control, length, room, IPPROTO_IP, IP_PKTINFO,
                               address_of data) ||
            byte_reader_left(&data) < 12)
                return 0;
        (void)byte_reader_skip(&data, 4);
        return (p32)byte_reader_uint(&data, 4, true);
}
#endif

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
        if (link_age(now, link_nearby.interfaces_looked) >= 5000000)
        {
                p32 before[LINK_INTERFACES];
                positive had = link_nearby.interfaces;
                p8 changed;

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

        {
                p64 sweep = link_sweep_turn(now);

                if (sweep && sweep < wake)
                        wake = sweep;
        }
        if (link_nearby.next_announce < wake)
                wake = link_nearby.next_announce;
        if (link_nearby.next_ask < wake)
                wake = link_nearby.next_ask;
        {
                p64 due = link_pending_turn(now);

                if (due && due < wake)
                        wake = due;
        }
        return wake;
}

// Read what is waiting on the mDNS socket, believing only TTL 255.
#if defined(MACOS)
/*
        Read what is waiting on the mDNS socket, believing only TTL 255, with
        Darwin's recvmsg from libc. It is the same call with the same fields as
        the Linux read below -- the sender's address, one buffer, and a control
        buffer for the TTL and the receiving interface -- in Darwin's layouts,
        which differ in three places:

          struct msghdr has its control length in a 32-bit field, its flags
          after it, and a 32-bit iovec count, so it is the libc struct and
          not the Linux record link_message;
          a control message opens with a 32-bit length, a level and a type
          (twelve bytes, aligned to four), where Linux's length is 64 bits
          and its data starts at sixteen. CMSG_FIRSTHDR, CMSG_NXTHDR and
          CMSG_DATA walk them, and bound the walk by the length the kernel
          filled;
          the TTL arrives as a message of type IP_RECVTTL (24), one byte,
          where Linux's is type IP_TTL with an int; and IP_PKTINFO's
          message leaves spec_dst zero and names the receiving interface by
          index, which is looked up among the interfaces joined for its
          address.

        The reading of the packet is link_nearby_heard, the same on both
        systems: the packet goes to the confined reader (link_mdns_read_apart).
*/
static fn link_nearby_receive(p64 now)
{
        for (positive turn = 0; turn < 64 && link_nearby.socket >= 0; turn++)
        {
                p8 packet[WATERLINK_MDNS_MAX + 1];
                struct sockaddr_in from;
                struct iovec part = {packet, sizeof packet};
                p64 control[8];
                struct msghdr message;
                struct cmsghdr address_to header;
                ssize_t got;
                p8 address[16];
                p32 local = 0;
                b32 ttl = -1;

                memory_zero(address_of from, sizeof from);
                memory_zero(address_of message, sizeof message);
                message.msg_name = address_of from;
                message.msg_namelen = sizeof from;
                message.msg_iov = address_of part;
                message.msg_iovlen = 1;
                message.msg_control = control;
                message.msg_controllen = sizeof control;
                got = recvmsg((int)link_nearby.socket, address_of message,
                              MSG_DONTWAIT);
                if (got < 0)
                        return;
                if ((positive)got > WATERLINK_MDNS_MAX)
                        continue;

                for (header = CMSG_FIRSTHDR(address_of message); header;
                     header = CMSG_NXTHDR(address_of message, header))
                {
                        if (header->cmsg_level != IPPROTO_IP)
                                continue;
                        if (header->cmsg_type == IP_RECVTTL &&
                            header->cmsg_len >= CMSG_LEN(1))
                                ttl = *(p8 address_to)CMSG_DATA(header);
                        else if (header->cmsg_type == IP_PKTINFO &&
                                 header->cmsg_len >= CMSG_LEN(12))
                        {
                                b32 index;

                                memory_copy_apart(address_of index,
                                                  CMSG_DATA(header),
                                                  sizeof index);
                                for (positive at = 0;
                                     at < link_nearby.interfaces; at++)
                                        if (link_nearby.interface[at] == index)
                                                local = link_nearby
                                                            .interface_address[at];
                        }
                }
                if (ttl != 255)
                        continue;

                link_address_v4(address,
                                network_order_32(from.sin_addr.s_addr));
                link_nearby_heard(packet, (positive)got, address,
                                  network_order_16(from.sin_port), local, now);
        }
}
#else
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
                                     message.control_length, sizeof control,
                                     IPPROTO_IP, IP_TTL) != 255)
                        continue;

                link_address_v4(address, network_order_32(from.host));
                link_nearby_heard(packet, (positive)got, address,
                                  network_order_16(from.port),
                                  link_nearby_local((p8 address_to)control,
                                                    message.control_length,
                                                    sizeof control), now);
        }
}
#endif

#endif // WATERLINK_NEARBY_INCLUDED
