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

        Finding the machines is the network's own discovery
        (src/net/discovery.c): the mDNS and DNS-SD records, the socket they
        travel on (IPv4, 224.0.0.251 on every interface that takes the
        membership, a packet believed only with the TTL it was sent with)
        and the confined reader of what arrives. What is here is what a
        group does with it: what it announces and asks (group.c's service),
        and whom it greets for what it hears. An address is taken from where
        a packet came from, never from what it says. Both follow the
        network's switches (link_nearby_switches): LAN announce is what it
        announces and answers, LAN listen what it asks, greets and sweeps,
        and with both off it holds no socket at all.

        Included by service.c, which owns the sockets and the loop.

        Dawn Larsson - Apache-2.0 license
        github.com/dawnlarsson/moonwater
*/

#ifndef WATERLINK_NEARBY_INCLUDED
#define WATERLINK_NEARBY_INCLUDED

#include "group.c"

#if defined(MACOS)
#define LINK_PEERS_LOCK link_state_path(LINK_LEAF_PEERS_LOCK)
#else
#define LINK_PEERS_LOCK HOST_STATE "/link.peers.lock"
#endif
#define LINK_GROUPS_MAX 8
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
            (facts.mode & MODE_FORMAT) == MODE_FILE && facts.owner == LINK_STATE_OWNER &&
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
/*      A code is spent when its time is past, and when it has more time left
        than any code is given: expires is seconds since boot, and the record
        is on the disk, so after a boot the clock is near zero again and a
        code made an hour into the last boot read as an hour from running
        out. It was open, with every grant a code gives, for as long as the
        machine had been up when it was made. */
static p8 link_group_spent(struct link_group_record address_to record,
                             p64 now)
{
        return (record->flags & LINK_GROUP_ONCE) &&
               (record->expires <= now ||
                record->expires - now > LINK_PAIR_SECONDS);
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
        {
                positive len = string_length_max((string_address)host,
                                                 WATERLINK_NAME_MAX - 1);

                memory_copy(name, host, len);
                memory_to_lower_ascii(name, len);
        }
#else
        if (system_call_1(syscall(uname), (positive)uts) >= 0)
        {
                positive len = string_length_max((string_address)(uts + 65),
                                                 WATERLINK_NAME_MAX - 1);

                memory_copy(name, uts + 65, len);
                memory_to_lower_ascii(name, len);
        }
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
//      False when the name with its longest suffix is taken as well: the
//      loop looked up every name but the last it made, and a member that had
//      taken that one too was kept under a name another machine has, where
//      link allow NAME and link remove NAME act on whichever is first.
static p8 link_name_for(link_peers address_to peers, p8 address_to offered,
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
        return !link_peer_named(peers, (string_address)name);
}

/*
        A machine that is back under a name this machine knows, with a key it
        does not know: the same machine with a new key (a live stick starts
        with a new one at every boot), and not a second one for the name to
        be cut to fit beside the first. Its record takes the new key and
        keeps the name, so what is typed to reach it, `link NAME` and every
        script that says it, still reaches it. The old key goes with the
        record's old bytes; the grants are what the record had and no more
        than the group gives, so a new key is never given more than the old
        one had; where it was last heard is not known until it is.

        Whose word that is: a code is single use -- the one machine that used
        it is the only one kept under it, since the listener closes it in the
        turn that kept the first, so two machines are never answering one
        code -- and a person typed it on both machines, which is that person's
        own say that this is the machine. Any record of that name is its old
        one, however it was paired. A group's word is the group's: a member
        that comes back under a name takes the record of a member of the same
        group, and a record paired by hand or by another group is never
        replaced, for anybody who holds the group's key can say any name.
        Such a name is cut to fit beside it, as before.

        The offered name must be one a machine could have been paired as
        (link_name_good): a name that is not is not a name to take from
        another. The record that was replaced, or null when there is none.
*/
static struct waterlink_peer address_to link_peer_returned(
    link_peers address_to peers, p8 address_to offered, p8 address_to key,
    p8 once, p32 mark, p32 may)
{
        struct waterlink_peer address_to peer;

        offered[WATERLINK_NAME_MAX - 1] = 0;
        if (!link_name_good((string_address)offered))
                return null;
        peer = link_peer_named(peers, (string_address)offered);
        if (!peer || (!once && (!mark || peer->group != mark)))
                return null;
        memory_copy(peer->key, key, 32);
        peer->may &= may;
        peer->group = mark;
        memory_zero(peer->address, sizeof peer->address);
        peer->port = 0;
        peer->seen = 0;
        return peer;
}

/*
        A machine that has been renamed, heard by its key: the record that
        has the key takes the name the machine says it has now, and the name
        it had goes -- `moonwater link NEW` reaches it and `link OLD` no
        longer does. The key is the machine, and it was proved to this
        machine by the handshake that carried the name, so nobody but the
        machine itself can rename its record, and only its name moves: its
        grants, its place and its group stay as they were.

        What is left alone: a record a person named -- one paired by hand
        (link add NAME KEY, which no group made: group zero) or marked
        WATERLINK_PEER_LABELLED (link add over a machine a code or a group
        paired), a name that is theirs and not what the machine calls itself
        -- a name that is not one a machine could be paired as
        (link_name_good), and one the record has already. A name another
        record has is not taken: the machine is kept as the same name with
        its key's suffix, as when it is paired, and it is the offered name
        that is another machine's here, for the caller to say. The record's
        own name is taken out while the new one is chosen, so that the name
        it has is not found as another's and moved on to a longer suffix at
        every contact.

        True when the record's name changed; was is the name it had and now
        the one it has, whole buffers of WATERLINK_NAME_MAX. offered is
        folded to lowercase and ended, as a name is when it is typed.
*/
static p8 link_peer_follow(link_peers address_to peers,
                           struct waterlink_peer address_to peer,
                           p8 address_to offered, p8 address_to was,
                           p8 address_to now)
{
        p8 changed;

        if (!peer)
                return false;
        offered[WATERLINK_NAME_MAX - 1] = 0;
        memory_to_lower_ascii(offered, string_length((string_address)offered));
        if (!peer->group || (peer->flags & WATERLINK_PEER_LABELLED) ||
            !link_name_good((string_address)offered) ||
            string_equals(peer->name, (string_address)offered))
                return false;
        memory_copy(was, peer->name, WATERLINK_NAME_MAX);
        memory_zero(peer->name, WATERLINK_NAME_MAX);
        changed = link_name_for(peers, offered, peer->key, now) &&
                  !string_equals((string_address)now, (string_address)was);
        memory_copy(peer->name, changed ? now : was, WATERLINK_NAME_MAX);
        return changed;
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

/*      A member's rename a greeting brought, kept for the turn that says it
        (link_names_said): the greeting is read where nothing is written to a
        log. The record has the new name already, in the file. */
#define LINK_RENAMED 4
struct link_renamed {
        p8 key[32];
        p8 was[WATERLINK_NAME_MAX];
        p8 now[WATERLINK_NAME_MAX];
        p8 offered[WATERLINK_NAME_MAX];
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
        discovery_lan lan; // the network's socket for the local link, the interfaces it joined on and the LAN switches
        p64 switches_looked;
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
        //      This machine's name as it is now (link_name_look), what greetings
        //      and sessions say of it. A session says it again when name_gen has
        //      moved past the one it said, and the members of a group are
        //      greeted under it once more when regreet is set.
        p8 name[WATERLINK_NAME_MAX];
        p32 name_gen;
        p64 name_looked;
        p8 regreet;
        struct link_renamed renamed[LINK_RENAMED];
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

static fn link_nearby_socket(void);

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
        or by another group is never moved or widened, and is replaced only
        by a code, which is a person's own word (link_peer_returned). True
        when the member was new here, which is also a machine that came back
        under a name with a new key.

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
        p8 renamed = false;
        p8 name[WATERLINK_NAME_MAX];
        p8 was[WATERLINK_NAME_MAX];
        p8 asked[WATERLINK_NAME_MAX];

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
        //      A machine back under its name with a new key keeps the name
        //      (link_peer_returned); another is named before it is in the
        //      list, or it meets itself, and with no name left that nobody
        //      has, it is not kept.
        if (!peer && open && !crypto_same(key, link_self.me.public, 32))
        {
                peer = link_peer_returned(address_of peers, offered, key, once,
                                          keys->mark, held.may);
                if (peer)
                        new = changed = true;
                else if (peers.count < LINK_PEERS_MAX &&
                         link_name_for(address_of peers, offered, key, name))
                {
                        peer = peers.peer + peers.count++;
                        memory_zero(peer, sizeof(address_to peer));
                        memory_copy(peer->key, key, 32);
                        memory_copy(peer->name, name, WATERLINK_NAME_MAX);
                        peer->may = held.may;
                        peer->group = keys->mark;
                        new = changed = true;
                }
        }
        else if (peer && peer->group == keys->mark && stamped <= peer->seen)
                peer = null;
        //      A member that is known by its key and says another name than the
        //      record has was renamed: the key is what proved it is the same
        //      machine, so the record follows it (link_peer_follow), whichever
        //      group or code paired it, and not one paired by hand, which is a
        //      person's own name; a new member has the name it offered already.
        if (peer && !new && open)
        {
                memory_copy(asked, offered, WATERLINK_NAME_MAX);
                renamed = link_peer_follow(address_of peers, peer, asked, was,
                                           name);
                changed |= renamed;
        }
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
                renamed = saved && renamed;
                if (!saved)
                        peer = null;
        }
        address_to accepted = peer != null;
        if (renamed)
                for (positive at = 0; at < LINK_RENAMED; at++)
                {
                        struct link_renamed address_to slot = link_nearby.renamed + at;

                        if (slot->used && at + 1 < LINK_RENAMED)
                                continue;
                        memory_copy(slot->key, peer->key, 32);
                        memory_copy(slot->was, was, WATERLINK_NAME_MAX);
                        memory_copy(slot->now, name, WATERLINK_NAME_MAX);
                        memory_copy(slot->offered, asked, WATERLINK_NAME_MAX);
                        slot->used = true;
                        break;
                }
        link_peers_unlock(lock);
        crypto_forget(address_of held, sizeof held);
        link_self.state_dirty = true;
        if (once && peer && !(record->flags & LINK_GROUP_DONE))
                link_group_close(group);
        return new;
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
        What this machine is called, as the link says it, is the kernel's host
        name and nothing kept beside it -- `moonwater name` sets that, and a
        Mac's hostname is it -- so it is looked at again, once a second, by
        every loop that says it: the listener's and a client's. A rename is
        said at the next turn, with no restart and no pairing again, and no
        copy of the old name is left anywhere to say. What says it:

                greetings, from link_nearby.name, and the members of the
                groups this machine is in are greeted under the new name at
                once (regreet), since a member already known is greeted by
                nothing else: its announcements are not answered;
                sessions, on their own key (LINK_KEY_NAME), when they come up
                and again when name_gen has moved past what they said.

        The first look only learns the name: nothing was said under another.
*/
static fn link_name_look(p64 now)
{
        p8 fresh[WATERLINK_NAME_MAX];

        if (link_nearby.name_looked &&
            link_age(now, link_nearby.name_looked) < 1000000)
                return;
        link_nearby.name_looked = now ? now : 1;
        link_machine_name(fresh);
        if (!memory_compare(fresh, link_nearby.name, WATERLINK_NAME_MAX))
                return;
        if (link_nearby.name[0])
                link_nearby.regreet = true;
        memory_copy(link_nearby.name, fresh, WATERLINK_NAME_MAX);
        link_nearby.name_gen++;
}

//      This machine's name, said to the far end of a session.
static fn link_name_post(struct link_session address_to s)
{
        if (link_post(s, LINK_KEY_NAME, WATERLINK_FRAME_DURABLE, LINK_NAME_SAY,
                      link_nearby.name,
                      string_length((string_address)link_nearby.name)))
                s->named = link_nearby.name_gen;
}

//      What is said of a rename, in one line: the old name is now called the
//      new one, and when the new one is not what the machine asked for, whose
//      name that is here instead.
static fn link_name_line(p8 address_to was, p8 address_to now,
                         p8 address_to offered, p8 address_to line,
                         positive room)
{
        string_copy_bounded((string_address)line, (string_address)was, room);
        string_append_bounded((string_address)line, " is now called ", room);
        string_append_bounded((string_address)line, (string_address)now, room);
        if (!string_equals((string_address)now, (string_address)offered))
        {
                string_append_bounded((string_address)line, ": ", room);
                string_append_bounded((string_address)line,
                                      (string_address)offered, room);
                string_append_bounded((string_address)line,
                                      " is another machine here", room);
        }
}

/*      A line of what the link found out by itself, where whoever looks will
        see it: the listener's goes to the kernel log, which the log window
        shows (nothing reads its standard error), and a client's to its
        standard error, which a person is reading and the output of a command
        it ran is not. A terminal a shell has raw does not process output, so
        its line ends in both bytes. */
static p8 link_terminal_raw;

static fn link_note(p8 address_to line)
{
#if !defined(MACOS)
        if (link_self.server)
        {
                string_address parts[] = {(string_address)line, null};

                host_kmsg(parts);
                return;
        }
#endif
        string_format(log_error, host_label "%s%s", (string_address)line,
                      link_terminal_raw ? (string_address) "\r\n"
                                        : (string_address) "\n");
        log_flush();
}

/*      A record was renamed, and is already in the file: the sessions that
        have it say the new name where the state file shows them, and one line
        says what happened. */
static fn link_name_said(p8 address_to key, p8 address_to was, p8 address_to now,
                         p8 address_to offered)
{
        p8 line[3 * WATERLINK_NAME_MAX + 40];

        for (positive at = 0; at < LINK_SESSIONS; at++)
        {
                struct link_session address_to s = link_self.session + at;

                if (s->used && crypto_same(s->peer, key, 32))
                        memory_copy(s->name, now, WATERLINK_NAME_MAX);
        }
        link_self.state_dirty = true;
        link_name_line(was, now, offered, line, sizeof line);
        link_note(line);
}

//      The renames the greetings brought (link_pair_keep), said.
static fn link_names_said(void)
{
        for (positive at = 0; at < LINK_RENAMED; at++)
        {
                struct link_renamed slot = link_nearby.renamed[at];

                if (!slot.used)
                        continue;
                link_nearby.renamed[at].used = false;
                link_name_said(slot.key, slot.was, slot.now, slot.offered);
        }
}

/*
        The far end of a session says its name: 'M' and up to 31 bytes of it
        (LINK_KEY_NAME). The record that has the session's key takes it, and
        the line is said. It is the machine that was proved by the handshake
        that says it, and a name one is held to is the one a machine could be
        paired as; what is not, a name with a byte that ends it before its
        length, or one this session's record has already, is not worth a
        look at the file. A session changes its record only so many times
        (LINK_NAMES_MOST): each is a file written and synced, and an end that
        may rename itself may not do it a thousand times a second.
*/
static fn link_name_heard(struct link_session address_to s,
                          p8 address_to payload, positive length)
{
        p8 offered[WATERLINK_NAME_MAX];
        p8 was[WATERLINK_NAME_MAX];
        p8 now[WATERLINK_NAME_MAX];
        link_peers peers;
        bipolar lock;
        p8 renamed = false;

        if (length < 2 || length > WATERLINK_NAME_MAX ||
            payload[0] != LINK_NAME_SAY || s->names >= LINK_NAMES_MOST)
                return;
        memory_zero(offered, sizeof offered);
        memory_copy(offered, payload + 1, length - 1);
        if (string_length((string_address)offered) != length - 1 ||
            string_equals((string_address)s->name, (string_address)offered))
                return;
        lock = link_peers_lock();
        if (lock < 0)
                return;
        if (link_peers_for_change(address_of peers))
        {
                renamed = link_peer_follow(address_of peers,
                                           link_peer_keyed(address_of peers, s->peer),
                                           offered, was, now) &&
                          link_peers_save(address_of peers) >= 0;
                s->names += renamed;
        }
        link_peers_unlock(lock);
        if (renamed)
                link_name_said(s->peer, was, now, offered);
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

        for (positive at = 0; at < link_nearby.lan.interfaces; at++)
        {
                p32 address = link_nearby.lan.address[at];
                p32 mask = link_nearby.lan.mask[at];
                p32 network;
                p32 last;

                if (!mask || mask >= 0xfffffffeu)
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

/* A group greeting datagram: the first message to the group's key, from this
   machine's key and with its name. Shared by sweeps and unicast greetings.
   False when it could not be made. */
static p8 link_greeting_make(positive group, p8 address_to datagram)
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
                                  hello, datagram);
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
            !(link_nearby.lan.ask & DISCOVERY_ASK_LISTEN) ||
            (link_nearby.sweeping && group != link_nearby.sweep_group))
        {
                //      The code is used or gone, the listener has no port, or
                //      LAN listen is off (the sweep is finding machines on
                //      the local network): what is left of the sweeps is not
                //      sent.
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
                if (!link_nearby.sweep_count || !link_greeting_make(group, link_nearby.sweep_datagram))
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
        crypto_forget(link_nearby.keys, sizeof link_nearby.keys);
        for (positive at = 0; at < link_nearby.groups.count; at++)
                waterlink_group_keys_from(link_nearby.keys + at,
                                          link_nearby.groups.record[at].key,
                                          link_nearby.groups.record[at].namespace);

        link_nearby_socket();

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

/*
        The LAN switches (discovery_settings: LAN announce, LAN listen, and
        discovery's own, which is both), read every two seconds into the
        socket's own record, whose senders refuse what a switch keeps in
        (discovery_lan_allowed). Turned on, the quick announcements and
        questions start over; with both off there is no socket at all, and
        nothing is heard either.
*/
static fn link_nearby_socket(void)
{
        p8 wanted = link_nearby.groups.count && link_nearby.lan.ask;

        if (wanted && link_nearby.lan.socket < 0)
                discovery_lan_open(address_of link_nearby.lan);
        if (!wanted && link_nearby.lan.socket >= 0)
                discovery_lan_close(address_of link_nearby.lan);
}

static fn link_nearby_switches(p64 now)
{
        discovery_settings settings;
        positive was = link_nearby.lan.ask;

        if (link_nearby.switches_looked && link_age(now, link_nearby.switches_looked) < 2000000)
                return;
        link_nearby.switches_looked = now;
        discovery_settings_read(address_of settings);
        link_nearby.lan.ask = settings.ask & (DISCOVERY_ASK_ANNOUNCE | DISCOVERY_ASK_LISTEN);
        if (link_nearby.lan.ask & ~was & DISCOVERY_ASK_ANNOUNCE)
        {
                link_nearby.announced = 0;
                link_nearby.next_announce = now;
        }
        if (link_nearby.lan.ask & ~was & DISCOVERY_ASK_LISTEN)
        {
                link_nearby.asked = 0;
                link_nearby.next_ask = now;
        }
        link_nearby_socket();
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
        return link_nearby_outcome(
            discovery_lan_send(address_of link_nearby.lan, interface_at, packet, length),
            length);
}

static p8 link_nearby_announce(p32 ttl)
{
        p8 packet[DISCOVERY_MDNS_MAX];
        p8 sent = false;

        for (positive at = 0; at < link_nearby.lan.interfaces; at++)
        {
                positive length = discovery_mdns_announce(
                        address_of waterlink_mdns_service, packet, sizeof packet,
                        link_nearby.instance, link_nearby.host, link_port(),
                        link_nearby.lan.address[at], ttl, 0, null, 0);

                if (length)
                        sent |= link_nearby_send(packet, length, at);
        }
        return sent;
}

static fn link_nearby_ask(void)
{
        p8 packet[64];
        positive length = discovery_mdns_query(address_of waterlink_mdns_service,
                                               packet, sizeof packet);

        for (positive at = 0; at < link_nearby.lan.interfaces; at++)
                link_nearby_send(packet, length, at);
}

// Goodbye, with TTL zero, when the listener stops.
static fn link_nearby_stop(void)
{
        if (link_nearby.lan.socket >= 0 && link_nearby.groups.count)
                link_nearby_announce(0);
        discovery_lan_close(address_of link_nearby.lan);
        discovery_mdns_reader_end();
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
        p8 datagram[WATERLINK_DATAGRAM];

        if (!answer && link_greeted_recent(address, keys->mark, port, true, now))
                return;
        //      Counted whether or not it could be sent: the curve work is
        //      done, and a place that refuses it is neither greeted again at
        //      once nor outside the budget.
        if (link_greeting_make(group, datagram))
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
        //      A new member is greeted back at once. One kept already is too,
        //      as any place is, not sooner than LINK_GREET_AGAIN after the
        //      last greeting sent there: it greets because it does not have
        //      this machine, the greeting back having been lost, and nothing
        //      else would ever send it another -- it is known here, so its
        //      announcements are not greeted, and it stayed a peer of this
        //      machine that refused this machine, for good.
        if (link_pair_keep(group, key, hello + WATERLINK_STAMP_BYTES,
                           network_load_32(hello + 4), address, port,
                           address_of accepted))
                link_pair_begin(group, address, port, now, true);
        else if (accepted && link_greet_allowed(address, now))
                link_pair_begin(group, address, port, now, false);
        if (accepted)
                link_nearby.linked = true;
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
        struct discovery_mdns_found found;
        link_peers peers;

        //      Read in the network's confined reader, a copy of this
        //      listener that forgets the link's secrets before it reads.
        if (!discovery_mdns_read_apart(address_of waterlink_mdns_service,
                                       link_secrets_forget, packet, length,
                                       address_of found))
                return;

        if (found.asked && link_nearby.groups.count)
        {
                //      Unicast to wherever the packet says it came from, which
                //      nobody checked: a few a second, or a stream of spoofed
                //      questions is a stream of answers four times their size
                //      at somebody else.
                if (source_port && source_port != DISCOVERY_MDNS_PORT &&
                    found.question_length &&
                    link_age(now, link_nearby.last_reply) >= LINK_ANSWER_AGAIN)
                {
                        //      A one-shot asker gets the answer back to
                        //      itself, its ID and question with it.
                        p8 reply[DISCOVERY_MDNS_MAX];
                        positive reply_length = discovery_mdns_announce(
                                address_of waterlink_mdns_service, reply, sizeof reply,
                                link_nearby.instance, link_nearby.host, link_port(),
                                local_address
                                    ? local_address
                                    : (link_nearby.lan.interfaces
                                           ? link_nearby.lan.address[0]
                                           : 0),
                                10, found.id, found.question,
                                found.question_length);

                        if (reply_length)
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
                                bipolar sent = discovery_lan_reply(
                                    address_of link_nearby.lan,
                                    network_load_32(address + 12), source_port,
                                    reply, reply_length);

                                if (sent == (bipolar)reply_length)
                                        link_nearby.last_reply = now;
                        }
                }
                else if (source_port == DISCOVERY_MDNS_PORT &&
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
           what it announces is not greeted (Avahi drops it the same way).
           With LAN listen off nothing heard is greeted either: the socket
           is open for the questions LAN announce answers. */
        if (source_port != DISCOVERY_MDNS_PORT ||
            !(link_nearby.lan.ask & DISCOVERY_ASK_LISTEN))
                found.count = 0;
        if (found.count)
                link_peers_load(address_of peers);
        for (positive at = 0; at < found.count; at++)
        {
                struct discovery_mdns_instance address_to instance =
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

/*
        The listener's turn: reload the groups if they changed, and send what
        is due. Answers when it next wants a turn.
*/
static p64 link_nearby_tick(p64 now)
{
        p64 wake = now + 1000000;

        link_nearby_switches(now);
        link_nearby_reload(now);
        if (link_nearby.lan.socket < 0 || !link_nearby.groups.count)
                return wake;
        if (!link_nearby.labels_ready && !link_nearby_labels())
                return wake;

        //      Renamed: every member of a group this machine is in that it knows
        //      a place for is greeted, once, under the new name. A greeting is
        //      what carries the name to a member that is in no session with
        //      this machine, and the member keeps the sender under it as it
        //      keeps any greeting (link_pair_keep). A code's group is a pairing
        //      and not a place members are.
        if (link_nearby.regreet)
        {
                link_peers peers;

                link_nearby.regreet = false;
                link_peers_load(address_of peers);
                for (positive group = 0; group < link_nearby.groups.count; group++)
                {
                        if (link_nearby.groups.record[group].flags & LINK_GROUP_ONCE)
                                continue;
                        for (positive at = 0; at < peers.count; at++)
                                if (peers.peer[at].port &&
                                    peers.peer[at].group == link_nearby.keys[group].mark)
                                        link_pair_begin(group, peers.peer[at].address,
                                                        peers.peer[at].port, now, true);
                }
        }

        //      Interfaces come and go, and an address comes late -- a lease
        //      that arrives after the listener started is the ordinary case
        //      on a machine that boots straight into it. Every five seconds
        //      the memberships are asked for again, and an address that is
        //      new starts the quick announcements over.
        if (link_age(now, link_nearby.interfaces_looked) >= 5000000)
        {
                p32 before[DISCOVERY_LAN_INTERFACES];
                positive had = link_nearby.lan.interfaces;
                p8 changed;

                memory_copy(before, link_nearby.lan.address,
                            sizeof before);
                link_nearby.interfaces_looked = now;
                discovery_lan_interfaces(address_of link_nearby.lan);
                changed = had != link_nearby.lan.interfaces ||
                          memory_compare(before, link_nearby.lan.address,
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

// What is waiting on the network's socket for the local link, each datagram
// it believes (discovery_lan_receive) heard.
static fn link_nearby_receive(p64 now)
{
        for (positive turn = 0; turn < 64 && link_nearby.lan.socket >= 0; turn++)
        {
                p8 packet[DISCOVERY_MDNS_MAX + 1];
                p8 address[16];
                p32 source = 0, arrived = 0;
                p16 port = 0;
                bipolar got = discovery_lan_receive(address_of link_nearby.lan, packet,
                                                    address_of source, address_of port,
                                                    address_of arrived);

                if (got < 0)
                        return;
                if (!got)
                        continue;
                link_address_v4(address, source);
                link_nearby_heard(packet, (positive)got, address, port, arrived, now);
        }
}

#endif // WATERLINK_NEARBY_INCLUDED
