/*
        Waterlink as a program: the listener, the sessions, and the far end of
        `moonwater link shell` and `moonwater link run`.

        One process, one UDP socket, one poll. The core (link.c), the seal
        (seal.c) and the handshake (handshake.c) are transforms; this is the
        part that owns a socket, a clock, the keys and the children, and it is
        kept to that. Userspace on purpose -- the datagram path is where the
        kernel would be faster, and it is also where a mistake costs the
        machine, so it waits until the protocol has been used.

        WHAT A SESSION CARRIES

        A client opens a session per command, asks for one thing on it (a
        terminal or a command), and the session ends when that thing does.
        The streams are link keys, and each key has one job:

                request   client to machine, durable: what is wanted
                answer    machine to client, durable: yes, or why not
                input     client to machine, durable: keystrokes (urgent,
                          so each leaves alone at once) or standard input
                size      client to machine, replaceable: the window's size,
                          so a drag of the corner sends the last size only
                signal    client to machine, durable: a signal for the
                          command, when there is no terminal to type ^C at
                output    machine to client, durable: the terminal's bytes,
                          or standard output, ending in the exit status
                error     machine to client, durable: standard error

        Every payload starts with one byte naming what it is -- data, end,
        exit -- so a stream ends in order with what it carried.

        Both ends are the same machine: a session has descriptors read into
        frames on a key and keys whose frames are written to descriptors,
        and one loop moves them. A command that reads slowly slows the sender
        by not taking what arrives: the link holds it, and the sender's
        window on that key stops (waterlink.c, the acknowledgement is the
        credit).

        Dawn Larsson - Apache-2.0 license
        github.com/dawnlarsson/moonwater
*/

#ifndef WATERLINK_SERVICE_INCLUDED
#define WATERLINK_SERVICE_INCLUDED

#if defined(MACOS)
/*      Darwin's own names for what the signal and child paths wait on:
        kqueue for a child's exit, a handler and a pipe for the signals, and
        the socket options and multicast membership of discovery. The wait
        itself is lib.util.c's system_poll_wait, which is poll there. These
        are libc calls, made by name, because the system call table has no
        Darwin answer for ppoll, signalfd, pidfd or waitid.
        <unistd.h> is not included: it declares syscall(), which lib.c has as
        a macro, and <time.h> is not included either, since lib.c defines its
        own struct timespec. */
#include <errno.h>
#include <net/if.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/event.h>
#include <sys/fcntl.h>
#include <sys/socket.h>
#include <sys/wait.h>
extern int fcntl(int descriptor, int command, ...);
extern int pipe(int descriptors[2]);
extern long write(int descriptor, const void *data, unsigned long length);
extern void _exit(int status) __attribute__((noreturn));
extern int _NSGetExecutablePath(char *buffer, unsigned int *size);
#ifndef IPV6_V6ONLY
#define IPV6_V6ONLY 27
#endif
#endif

/*      On Darwin the host layer's names come first: the paths below are made
        from them (link_state_path), and host_darwin.c is the only place the
        Mac's directory is chosen. Linux has host.c, which the shell includes. */
#if defined(MACOS)
#include "host_darwin.c"
#endif

#include "link.c"
#include "seal.c"
#include "handshake.c"

/*      `link push` packs a file with zstd's core (the in-memory encoder and
        decoder and the dictionary loader; its command line is not compiled
        here), with the dictionary both ends hold. */
#ifndef ZSTD_CORE_ONLY
#define ZSTD_CORE_ONLY
#endif
#include "../sh/zstd.c"
#include "push_dictionary.h"

/*      "WL", two letters as sixteen bits: 0x574c. Unassigned, above the
        registered ranges people firewall by habit, and easy to remember from
        the name. */
#define LINK_PORT 22348

/*      Every path the link keeps. Linux names them in /root and /run/moonwater;
        Darwin makes each from link_state_path (host_darwin.c), in the user's
        Application Support directory. The home directory a command starts in
        is LINK_HOME_DIR, and the listener's own binary is LINK_EXECUTABLE. */
#if defined(MACOS)
#define LINK_SWITCH_PATH link_state_path(LINK_LEAF_SWITCH)
#define LINK_KEY_PATH link_state_path(LINK_LEAF_KEY)
#define LINK_PEERS_PATH link_state_path(LINK_LEAF_PEERS)
#define LINK_PLACES_PATH link_state_path(LINK_LEAF_PLACES)
#define LINK_GROUPS_PATH link_state_path(LINK_LEAF_GROUPS)
#define LINK_PORT_PATH link_state_path(LINK_LEAF_PORT)
#define LINK_TRANSPORT_PATH link_state_path(LINK_LEAF_TRANSPORT)
#define LINK_MODEM_PATH link_state_path(LINK_LEAF_MODEM)
#define LINK_MODEM_SEQ_PATH link_state_path(LINK_LEAF_MODEM_SEQ)
#define LINK_MODEM_RX_PATH link_state_path(LINK_LEAF_MODEM_RX)
#define LINK_LOCK_PATH link_state_path(LINK_LEAF_LOCK)
#define LINK_STATE_PATH link_state_path(LINK_LEAF_STATE)
#define LINK_STAMPS_PATH link_state_path(LINK_LEAF_STAMPS)
#define LINK_HOME_DIR link_run_home()
#define LINK_EXECUTABLE link_executable()
#else
#define LINK_SWITCH_PATH "/root/link"
#define LINK_KEY_PATH "/root/link.key"
#define LINK_PEERS_PATH "/root/link.peers"
#define LINK_PLACES_PATH "/root/link.places"
#define LINK_GROUPS_PATH "/root/link.groups"
#define LINK_PORT_PATH "/root/link.port"
#define LINK_TRANSPORT_PATH "/root/link.transport"
#define LINK_MODEM_PATH "/root/link.modem"
#define LINK_MODEM_SEQ_PATH "/root/link.modem.seq"
#define LINK_MODEM_RX_PATH "/root/link.modem.rx"
#define LINK_LOCK_PATH HOST_STATE "/link.lock"
#define LINK_STATE_PATH HOST_STATE "/link.state"
#define LINK_STAMPS_PATH "/root/link.stamps"
#define LINK_HOME_DIR "/root"
#define LINK_EXECUTABLE "/proc/self/exe"
#endif

#define LINK_PEERS_MAX 64
#define LINK_SESSIONS 16
#define LINK_SESSIONS_A_PEER 8
#define LINK_REQUEST_MAX 1024

/*      A push's request before its path: the ask, the file's mode, the format
        the file is sent in and, for the dictionary format, the dictionary's
        id, big endian. A raw file is sent as it is; a dictionary file is
        packed by push_pack (below), in blocks of PUSH_BLOCK bytes, each one
        zstd frame of at most PUSH_FRAME_MAX bytes behind its length. */
#define LINK_PUSH_HEAD 10
#define PUSH_FORMAT_RAW 0
#define PUSH_FORMAT_DICTIONARY 1
#define PUSH_BLOCK 131072u
#define PUSH_FRAME_MAX (PUSH_BLOCK + PUSH_BLOCK / 64 + 4096)

#define LINK_KEEPALIVE 10000000ull   // microseconds of silence, then a datagram
#define LINK_DEAD 45000000ull        // of hearing nothing, then the session is gone
#define LINK_GRACE 20000000ull       // old receive keys after a rekey
#define LINK_ATTEMPT 1000000ull      // between initiations
#define LINK_ATTEMPTS 6
#define LINK_QUIET 20000000ull       // of hearing nothing from an up link, its client starts trying again
#define LINK_RETRIES 5               // such tries, backed off, before LINK_DEAD takes the link

/*      The kernel's numbers for the socket options and control messages the
        listener and the greeting ask for, by the names the uapi headers give
        them; wire_constants holds each to the headers. */
#define SOL_IPV6 41
#define SOL_UDP 17
#if defined(MACOS)
/*      Darwin's numbers (sys/socket.h, netinet/in.h). IP_PKTINFO is the option
        that turns the destination info on, which Darwin calls IP_RECVPKTINFO.
        UDP_SEGMENT and UDP_GRO are Linux's alone and stay as they were. */
#define SO_REUSEPORT 0x0200
#define IPV6_V6ONLY 27
#define IP_TTL 4
#define IP_PKTINFO 26
#define IP_RECVTTL 24
#define IP_MULTICAST_IF 9
#define IP_MULTICAST_TTL 10
#define IP_ADD_MEMBERSHIP 12
#else
#define SO_REUSEPORT 15
#define IPV6_V6ONLY 26
#define IP_TTL 2
#define IP_PKTINFO 8
#define IP_RECVTTL 12
#define IP_MULTICAST_IF 32
#define IP_MULTICAST_TTL 33
#define IP_ADD_MEMBERSHIP 35
#endif
#define UDP_SEGMENT 103
#define UDP_GRO 104
#if defined(MACOS)
//      UDP_SEGMENT and UDP_GRO are Linux's, and so is the sendmsg that carries
//      the first: a Darwin datagram goes out one to a call.
#define LINK_GSO false
#else
#define LINK_GSO true
#endif

#define LINK_KEY_REQUEST 1
#define LINK_KEY_ANSWER 2
#define LINK_KEY_INPUT 3
#define LINK_KEY_SIZE 4
#define LINK_KEY_SIGNAL 5
#define LINK_KEY_OUTPUT 7
#define LINK_KEY_ERROR 8

#define LINK_DATA 'D'
#define LINK_END 'E'
#define LINK_EXIT 'X'

#define LINK_ASK_SHELL 'S'
#define LINK_ASK_RUN 'R'
#define LINK_ASK_PUSH 'P'
#define LINK_ASK_PULL 'G'
#define LINK_ASK_LOG 'L'

#define LINK_KIND_NONE 0
#define LINK_KIND_SHELL 1
#define LINK_KIND_RUN 2
#define LINK_KIND_PUSH 3
#define LINK_KIND_PULL 4
#define LINK_KIND_LOG 5

#define LINK_EXIT_BUSY 3
#define LINK_FAILED 255

/*
        Why a link did not come up, the same numbers for the CLI and the UI.
        They are stable: a script may match the words, and a new reason takes
        a new number at the end. A machine that does not know this key, or is
        off, is silence on the wire, so it is NO_REPLY and not a key error.
*/
#define LINK_REASON_LINKED 0
#define LINK_REASON_NO_ROUTE 1
#define LINK_REASON_NO_REPLY 2
#define LINK_REASON_BAD_KEY 3
#define LINK_REASON_CODE_REFUSED 4

/*      The most full datagrams one segment send carries. The run goes to the
        kernel as one UDP datagram before it is cut, and that datagram's
        length has sixteen bits: 65535 less an IPv6 header and a UDP header,
        over 1200, is fifty four, and IPv4's bound gives the same. A run of
        sixty four was refused as too long, and the refusal turned segments
        off for the life of the process, so every transfer sent one datagram
        a call once its window passed fifty four. */
#define LINK_SEGMENTS ((65535 - 40 - 8) / WATERLINK_DATAGRAM)

/*      A grant by name, and what a request needs of one: ask is the
        request byte that needs this grant. */
typedef struct
{
        string_address name;
        p32 bit;
        p8 ask;
        p8 ask_too;
} link_grant;

static const link_grant link_grants[] = {
    {"shell", WATERLINK_MAY_SHELL, LINK_ASK_SHELL, 0},
    {"run", WATERLINK_MAY_RUN, LINK_ASK_RUN, 0},
    {"files", WATERLINK_MAY_FILES, LINK_ASK_PUSH, LINK_ASK_PULL},
    {"log", WATERLINK_MAY_LOG, LINK_ASK_LOG, 0},
};

// The grant a word names, or 0.
static p32 link_grant_bit(string_address word)
{
        positive at = string_table_find(word, link_grants, sizeof link_grants[0],
                                        array_count(link_grants));

        return at < array_count(link_grants) ? link_grants[at].bit : 0;
}

// The grant a request byte needs, as an index of link_grants; past it, none.
static positive link_grant_asked(p8 ask)
{
        positive at = 0;

        while (at < array_count(link_grants) &&
               (!ask || (link_grants[at].ask != ask && link_grants[at].ask_too != ask)))
                at++;
        return at;
}

static p64 link_now(void)
{
#if defined(MACOS)
        //      Darwin's CLOCK_MONOTONIC is 6; the 1 below is Linux's.
        return system_clock_ns(CLOCK_MONOTONIC) / 1000;
#else
        return system_clock_ns(1) / 1000;
#endif
}

// All digits and nothing else, below a million; -1 otherwise.
static bipolar link_decimal(string_address text)
{
        positive value;

        return string_digits_checked_exact(text, 10, address_of value) &&
                               value < 1000000
                       ? (bipolar)value
                       : -1;
}

// Store --------------------------------------------------------------

/*
        A key as WireGuard writes one: 44 characters of base64, ten whole
        groups and a last one of two bytes, padded.
*/
static fn link_key_text(p8 address_to key, p8 address_to text)
{
        memory_into_base64(text, key, 32);
}

//      The last character carries two bits the key does not use; a text with
//      them set is some other text.
static p8 link_key_parse(string_address text, p8 address_to key)
{
        p8 loose;

        return memory_from_base64(key, text, string_length(text), 32,
                                  address_of loose) &&
               !loose;
}

/*
        A peer's name is typed by the person pairing it and printed back in
        every status, so it is held to letters, digits, dot, dash and
        underscore, starting with a letter or digit: nothing a terminal would
        act on, nothing a shell word would split.
*/
/*      The words `moonwater link` itself takes in the place of a machine's
        name, which no machine or group may be called: link NAME is a
        terminal on the one, and link log is not. */
static const string_address link_words[] = {
    "on",   "off",   "key",   "serve", "port", "pair", "add",  "remove",
    "allow", "deny",  "group", "leave", "push", "pull", "log",  "shell",
    "run",  "help",  "transport", "modem", "cancel", "doctor",
};

#define LINK_NAME_CHARACTERS                                                  \
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.-_"

static p8 link_name_good(string_address name)
{
        positive length = string_length(name);

        if (!length || length >= WATERLINK_NAME_MAX ||
            !byte_is_alnum((p8)name[0]) ||
            string_span_of_set(name, LINK_NAME_CHARACTERS) != length)
                return false;
        for (positive word = 0; word < array_count(link_words); word++)
                if (string_equals(name, link_words[word]))
                        return false;
        return true;
}

/*      Records read from a file keep only the names that could have been
        paired: the name is cut at its room, and a record whose name
        link_name_good refuses is replaced by the last one, so the order is
        not kept. The peers and the groups both read their files through
        this. */
static fn link_records_named(p8 address_to records, positive address_to count,
                             positive size, positive name_at, positive room)
{
        for (positive at = 0; at < address_to count; at++)
        {
                p8 address_to record = records + at * size;

                record[name_at + room - 1] = 0;
                if (link_name_good((string_address)(record + name_at)))
                        continue;
                (address_to count)--;
                if (at != address_to count)
                        memory_copy(record, records + address_to count * size,
                                    size);
                at--;
        }
}

/* Everything waterlink keeps is records only root may read: the key, the
   peers, the groups and the listener's state. Refuse a partial, oversized,
   linked, non-root-owned or publicly accessible file rather than
   interpreting the valid-looking prefix of a replaced one, and open it
   nonblocking so a FIFO there is refused rather than waited on. */
static bipolar link_read_private_records(string_address path,
                                         p8 address_to into, positive room,
                                         positive record,
                                         positive address_to got)
{
        file_facts facts;
        bipolar handle = system_open_at(AT_FDCWD, path,
                                        FILE_READ | O_NONBLOCK | O_NOFOLLOW |
                                            O_CLOEXEC);
        bipolar read;

        address_to got = 0;
        if (handle < 0)
                return handle;
        if (!file_look(handle, (string_address)"", AT_EMPTY_PATH,
                       address_of facts) ||
            (facts.mode & MODE_FORMAT) != MODE_FILE || facts.owner != 0 ||
            (facts.mode & 077) || facts.size > room || facts.size % record)
        {
                system_close(handle);
                return -EPERM;
        }

        //      A private file of a few kilobytes arrives in one read, or is
        //      refused: a short one is not half a record.
        read = system_read_retry((positive)handle, into, (positive)facts.size);
        system_close(handle);
        if (read != (bipolar)facts.size)
                return -EIO;
        address_to got = (positive)read;
        return 0;
}

/* Written beside its name and renamed over it: whole or not at all. Synced,
   the bytes are on disk before the name shows them and /root is synced after
   the rename, so a power cut cannot bring back the peers or the groups from
   before a forget, a pair or a leave; the state and the stamps, which only
   say what this session is doing, are not. */
static bipolar link_file_replace(string_address path, address_any bytes,
                                 positive length, p8 sync)
{
        return file_publish_bytes(path, bytes, length, 0600, sync);
}

/*
        The machine's own key, made on first use.

        Published under a name nobody else could have made and never over
        one: two first uses at once cannot leave half a key, whoever got
        there first wins and both read what won, and a name planted at
        /root/link.key -- a link to somewhere, or a file somebody could read
        -- is refused rather than written through or trusted.
*/
static bipolar link_secret_at(string_address path, p8 address_to secret, p8 make)
{
        positive got = 0;
        bipolar read = link_read_private_records(path, secret, 32, 32,
                                                 address_of got);

        if (read == -ENOENT && make)
        {
                p8 fresh[32];
                bipolar made;

                scope_exit(crypto_forget(fresh, sizeof fresh));
                if (system_random_fill(fresh, 32, 0) < 0)
                        return -EIO;
                //      A name that is there already is a first use that got
                //      here first, and is read.
                made = file_publish_with(path, fresh, 32, 0600, true,
                                          FILE_STAGED_NO_REPLACE);
                if (made < 0 && made != -ERROR_EXISTS)
                        return made;

                read = link_read_private_records(path, secret, 32, 32,
                                                 address_of got);
        }

        if (read < 0)
                return read;
        //      An empty file is no key: a key is exactly 32 bytes.
        return got == 32 ? 0 : -EPERM;
}

/*
        The machine's own key, at LINK_KEY_PATH: see link_secret_at.
*/
static bipolar link_secret(p8 address_to secret, p8 make)
{
        return link_secret_at(LINK_KEY_PATH, secret, make);
}

static bipolar link_identity(struct waterlink_identity address_to me, p8 make)
{
        p8 secret[32];

        scope_exit(crypto_forget(secret, sizeof secret));
        bipolar got = link_secret(secret, make);

        if (got < 0)
                return got;
        waterlink_identity_from(me, secret);
        return 0;
}

typedef struct
{
        struct waterlink_peer peer[LINK_PEERS_MAX];
        positive count;
} link_peers;

static bipolar link_peers_read(link_peers address_to peers)
{
        positive got = 0;
        bipolar read;

        memory_zero(peers, sizeof(address_to peers));
        read = link_read_private_records(LINK_PEERS_PATH,
                                         (p8 address_to)peers->peer,
                                         sizeof(peers->peer),
                                         sizeof(struct waterlink_peer),
                                         address_of got);
        if (read < 0)
                return read;

        peers->count = got / sizeof(struct waterlink_peer);

        //      A record with a name that could not have been paired is dropped
        //      rather than printed.
        link_records_named((p8 address_to)peers->peer, address_of peers->count,
                           sizeof(struct waterlink_peer),
                           offset_of(struct waterlink_peer, name),
                           WATERLINK_NAME_MAX);
        return 0;
}

static fn link_peers_load(link_peers address_to peers)
{
        (void)link_peers_read(peers);
}

/* Whether a change to a records file may go on from a read of it: the file
   was read, or was never written and is empty. One that is there and cannot
   be read -- refused for what it is, cut short, a failed read -- is not an
   empty list, and saving over it would forget every record in it. The peers
   and the groups both change their files by this. */
static p8 link_peers_for_change(link_peers address_to peers)
{
        bipolar read = link_peers_read(peers);

        return read >= 0 || read == -ENOENT;
}

static bipolar link_peers_save(link_peers address_to peers)
{
        return link_file_replace(LINK_PEERS_PATH, peers->peer,
                                 peers->count * sizeof(struct waterlink_peer),
                                 true);
}

static struct waterlink_peer address_to link_peer_named(link_peers address_to peers,
                                                         string_address name)
{
        for (positive at = 0; at < peers->count; at++)
                if (string_equals(peers->peer[at].name, name))
                        return peers->peer + at;
        return null;
}

static struct waterlink_peer address_to link_peer_keyed(link_peers address_to peers,
                                                         p8 address_to key)
{
        for (positive at = 0; at < peers->count; at++)
                if (crypto_same(peers->peer[at].key, key, 32))
                        return peers->peer + at;
        return null;
}

/*
        The places a peer was heard at, besides the one its record names. A
        record names the last place the listener heard it from; a machine that
        moved, or whose lease changed, is found at another, and the one before
        is worth a try when the named place does not answer. A place is the
        address and port a packet that opened from the peer came from, after
        its tag was checked, so nothing a stranger sends writes one. Kept in
        /root/link.places, root's alone, and written only when a peer shows a
        place it had not been seen at, so the file changes when a machine
        moves and not with every datagram. An address is a cache here too:
        a place that does not answer is passed over, never believed.
*/
#define LINK_PLACES_MAX 128
#define LINK_PLACES_PER_PEER 4
#define LINK_PLACE_ATTEMPTS 3 // a second and later place, in initiations

struct link_place {
        p8 key[32];
        p8 address[16];
        p16 port;
        p16 reserved; // zero, so the record is whole words
        p32 seen;     // seconds of this machine's clock, when it was heard
};

_Static_assert(sizeof(struct link_place) == 56,
               "a link place record is 56 bytes");

typedef struct
{
        struct link_place place[LINK_PLACES_MAX];
        positive count;
} link_places;

static fn link_places_load(link_places address_to places)
{
        positive got = 0;

        memory_zero(places, sizeof(address_to places));
        if (link_read_private_records(LINK_PLACES_PATH,
                                      (p8 address_to)places->place,
                                      sizeof(places->place),
                                      sizeof(struct link_place),
                                      address_of got) < 0)
                return;
        places->count = got / sizeof(struct link_place);
}

static bipolar link_places_save(link_places address_to places)
{
        return link_file_replace(LINK_PLACES_PATH, places->place,
                                 places->count * sizeof(struct link_place),
                                 true);
}

/* Removes the record at at: the last record takes its place, so the order is
   not kept. Returns the count left. */
static positive link_places_drop(link_places address_to places, positive at)
{
        places->count--;
        if (at != places->count)
                memory_copy(places->place + at, places->place + places->count,
                            sizeof(struct link_place));
        return places->count;
}

/* Keeps a place its peer was heard at: the newest of the peer's, and the
   oldest of the file's dropped to make room. A place is never kept twice. */
static fn link_place_learn(p8 address_to key, p8 address_to address, p16 port)
{
        link_places places;
        positive at;
        positive oldest;
        positive mine = 0;
        p32 now = (p32)(system_clock_ns(0) / 1000000000ull);

        link_places_load(address_of places);
        for (at = 0; at < places.count; at++)
                if (crypto_same(places.place[at].key, key, 32) &&
                    places.place[at].port == port &&
                    !memory_compare(places.place[at].address, address, 16))
                        break;
        if (at < places.count)
        {
                places.place[at].seen = now;
        }
        else
        {
                for (positive look = 0; look < places.count; look++)
                        if (crypto_same(places.place[look].key, key, 32))
                                mine++;
                if (mine >= LINK_PLACES_PER_PEER)
                {
                        oldest = places.count;
                        for (positive look = 0; look < places.count; look++)
                                if (crypto_same(places.place[look].key, key, 32) &&
                                    (oldest == places.count ||
                                     places.place[look].seen < places.place[oldest].seen))
                                        oldest = look;
                        (void)link_places_drop(address_of places, oldest);
                }
                if (places.count == LINK_PLACES_MAX)
                {
                        oldest = 0;
                        for (positive look = 1; look < places.count; look++)
                                if (places.place[look].seen < places.place[oldest].seen)
                                        oldest = look;
                        link_places_drop(address_of places, oldest);
                }
                at = places.count++;
                memory_copy(places.place[at].key, key, 32);
                memory_copy(places.place[at].address, address, 16);
                places.place[at].port = port;
                places.place[at].reserved = 0;
                places.place[at].seen = now;
        }
        (void)link_places_save(address_of places);
        crypto_forget(address_of places, sizeof places);
}

static p8 link_place_known(struct link_place address_to tried, positive count,
                           struct link_place address_to place)
{
        for (positive at = 0; at < count; at++)
                if (tried[at].port == place->port &&
                    !memory_compare(tried[at].address, place->address, 16))
                        return true;
        return false;
}

/*
        The places to try for a peer, in order: the one its record names
        first, then the ones it was heard at, newest first, each at most once,
        up to LINK_PLACES_PER_PEER. Returns how many were put in into.
*/
static positive link_places_for(p8 address_to key, p8 address_to first,
                                p16 first_port, struct link_place address_to into)
{
        link_places places;
        positive count = 1;

        memory_zero(into, sizeof(struct link_place) * LINK_PLACES_PER_PEER);
        memory_copy(into[0].key, key, 32);
        memory_copy(into[0].address, first, 16);
        into[0].port = first_port;
        link_places_load(address_of places);
        while (count < LINK_PLACES_PER_PEER)
        {
                positive best = places.count;

                for (positive at = 0; at < places.count; at++)
                {
                        struct link_place address_to kept = places.place + at;

                        if (!crypto_same(kept->key, key, 32) ||
                            link_place_known(into, count, kept))
                                continue;
                        if (best == places.count ||
                            kept->seen > places.place[best].seen)
                                best = at;
                }
                if (best == places.count)
                        break;
                into[count++] = places.place[best];
        }
        crypto_forget(address_of places, sizeof places);
        return count;
}

static p16 link_port(void)
{
        p8 word[16];
        bipolar port;

        host_read_word(LINK_PORT_PATH, word, sizeof word);
        port = link_decimal((string_address)word);
        return port > 0 && port < 65536 ? (p16)port : LINK_PORT;
}

// Addresses ------------------------------------------------------------

/*
        Every address is held as IPv6, IPv4 mapped when it is IPv4, which is
        what a dual-stack socket reports and what a peer record keeps.
*/
static fn link_address_v4(p8 address_to into, p32 host)
{
        memory_zero(into, 10);
        into[10] = 0xff;
        into[11] = 0xff;
        network_store_32(into + 12, host);
}

static p8 link_address_mapped(p8 address_to address)
{
        static const p8 prefix[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff};

        return !memory_compare(address, prefix, 12);
}

/*
        An address one machine can be at: not 0.0.0.0 or ::, not a multicast
        group (224/4, ff00::/8) and not the reserved block that holds the
        limited broadcast (240/4). A peer recorded at one of them is a
        datagram to nobody, or to everybody, for as long as the record lives.
*/
static p8 link_address_unicast(p8 address_to address)
{
        if (link_address_mapped(address))
        {
                p32 host = network_load_32(address + 12);

                return host >> 24 && host < 0xe0000000u;
        }
        return address[0] != 0xff && memory_span_byte(address, 0, 16) != 16;
}

/*
        HOST, HOST:PORT, [V6] or [V6]:PORT. A name is looked up once, now, over
        IPv4: an address is a cache of where a key last was, and the next
        datagram from the peer corrects it. What it comes to is one machine's
        address, and not the address of none or of many. A host with no port
        is on the one `moonwater link port` kept, the default where none is.
*/
static p8 link_parse_place(string_address text, p8 address_to address,
                             p16 address_to port)
{
        p8 host[256];
        positive length = string_length(text);
        string_address colon = null;

        address_to port = link_port();

        if (text[0] == '[')
        {
                string_address close = string_first_of(text, ']');

                if (!close)
                        return false;
                if (close[1] == ':')
                        colon = close + 1;
                else if (close[1])
                        return false;
                length = (positive)(close - text - 1);
                //      One pair of brackets, which is the literal's own to
                //      take off where there is no port.
                if (length >= sizeof host || text[1] == '[')
                        return false;
                memory_copy(host, text + 1, length);
                host[length] = 0;
                if (!net_ipv6_literal(host, length, address))
                        return false;
        }
        else
        {
                string_address first = string_first_of(text, ':');
                p8 bare = first && string_first_of(first + 1, ':');

                //      A name or an IPv4 address and a port, or an IPv6
                //      address and none.
                colon = bare ? null : first;
                length = colon ? (positive)(colon - text) : length;
                if (!length || length >= sizeof host)
                        return false;
                memory_copy(host, text, length);
                host[length] = 0;

                if (bare)
                {
                        if (!net_ipv6_literal(host, length, address))
                                return false;
                }
                else
                {
                        p32 found = 0;

                        if (net_resolve_v4_apart((string_address)host,
                                                 address_of found, 3) != DNS_OK)
                                return false;
                        link_address_v4(address, found);
                }
        }

        if (colon)
        {
                bipolar value = link_decimal(colon + 1);

                if (value <= 0 || value > 65535)
                        return false;
                address_to port = (p16)value;
        }
        return link_address_unicast(address);
}

// Digits and colons only: what this writes can be printed to a terminal.
static fn link_place_text(p8 address_to address, p16 port, p8 address_to text)
{
        positive used = 0;

        if (link_address_mapped(address))
                used = host_into(text, network_load_32(address + 12));
        else
        {
                text[used++] = '[';
                for (positive at = 0; at < 16; at += 2)
                {
                        used += positive_into_base(text + used,
                                                   network_load_16(address + at),
                                                   16, false);
                        text[used++] = at < 14 ? ':' : ']';
                }
        }
        text[used++] = ':';
        used += positive_into(text + used, port);
        text[used] = 0;
}

static fn link_socket_address(socket_address_internet6 address_to into,
                              p8 address_to address, p16 port)
{
        *into = (socket_address_internet6){
            .family = AF_INET6, .port = network_order_16(port)};
#if defined(MACOS)
        into->length = sizeof *into;
#endif
        memory_copy(into->host, address, 16);
}

// The lock --------------------------------------------------------------

/*
        The listener holds a write lock on /run/moonwater/link.lock for as long
        as it runs. A record lock and not flock, because the kernel will say
        who holds a record lock: `link off` asks, opens the holder as a pidfd,
        asks again, and only then signals -- so a pid that ended and was reused
        between the two is never the one signalled (host_lock_signal).
*/

// Sessions ---------------------------------------------------------------

struct link_keys {
        crypto_aesgcm_key send;
        crypto_aesgcm_key receive;
        struct waterlink_replay replay;
        p64 counter;
        p64 made;
        p32 ours;
        p32 theirs;
        p8 live;
};

struct link_stream {
        bipolar fd;
        positive skip; // bytes of the frame in hand already written
        p8 key;
        p8 flags;
        p8 done;
        p8 quiet;  // it said "not now", and no wait has heard it since
        p8 socket; // asked with MSG_DONTWAIT, since it is not opened again
        bipolar error; // why a read or write failed, as a negative number; 0 while it did not
        p64 written;   // bytes of a transfer's file written
        p64 started;   // and how many of them the disk was asked to begin on
};

struct link_session {
        p8 used;
        p8 finished;
        p8 dialed; // this machine dialled it: the keeper dials it again when it drops
        p8 kind;
        p8 name[WATERLINK_NAME_MAX];
        p8 peer[32];
        p64 conversation;
        p8 address[16];
        p16 port;
        p8 moved; // the address changed under a datagram that passed: the peers file is told at the turn's end
        struct link_keys now;
        struct link_keys next;   // made by a rekey, waiting to be confirmed
        struct link_keys before; // what the last rekey replaced
        p64 opened;
        p64 keyings; // how many handshakes this session has made
        p64 heard;
        p64 spoke;
        p64 noted; // the second the state file last had it seen
        struct waterlink_link address_to link;

        //      The streams, and the command behind them at the machine.
        struct link_stream reads[2];  // a descriptor read into a key
        struct link_stream writes[2]; // a key written to a descriptor
        bipolar pid;
        bipolar pidfd;
        p8 pid_quiet; // still running when last asked
        bipolar terminal; // a shell's, both a read and a write
        p8 exited;
        b32 status;
        p64 exited_at;
        p8 exit_sent;
        p8 failed;
        p8 answered;
        p8 refused;
        p8 refusal[128];
        p32 mode; // what a pulled file is made with: the far file's, when it said
        p8 packed; // a pushed file's staging file holds dictionary frames, to unpack at the end
        p8 part[LINK_REQUEST_MAX + 32]; // a transfer's staging file
        p8 whole[LINK_REQUEST_MAX + 1]; // and the name it becomes
};

/*
        What the listener knows that the files do not: which peers it has
        heard, from where and when, and what is open. It lives in the
        listener as this record and is written as it is, so `moonwater link`
        reads the same bytes back rather than parsing text.
*/
typedef struct
{
        p8 key[32];
        p8 address[16];
        p16 port;
        p64 seen;    // seconds, this machine's clock
        p64 changed; // seconds: when the address last changed, 0 for never
        p8 was[16];  // the address it changed from
        p16 was_port;
} link_seen_entry;

//      The newest initiation stamp taken from a key: older ones are replays.
typedef struct
{
        p8 key[32];
        p8 stamp[WATERLINK_STAMP_BYTES];
} link_stamp_entry;

typedef struct
{
        p8 name[WATERLINK_NAME_MAX];
        p8 kind;
        p8 address[16];
        p16 port;
        p64 seconds; // open for
        p64 rtt;     // microseconds, smoothed
} link_open_entry;

typedef struct
{
        link_seen_entry seen[LINK_PEERS_MAX];
        link_open_entry open[LINK_SESSIONS];
        p32 seen_count;
        p32 open_count;
} link_state;

static const string_address link_kind_names[] = {"open", "shell", "run",
                                                  "push", "pull", "log"};
static const p8 link_kind_asks[] = {0, LINK_ASK_SHELL, LINK_ASK_RUN,
                                    LINK_ASK_PUSH, LINK_ASK_PULL, LINK_ASK_LOG};

typedef struct
{
        bipolar socket;
        p8 socket_quiet; // read to its end, and no wait has heard it since
        p64 now;           // the turn's clock, which a frame posted in it carries
        p64 wall;          // this machine's clock in seconds, read at
        p64 wall_read;     // this turn's clock
        p64 granted;       // when the sessions were last held to the peers
        p8 server;
        p8 gso;
        p8 heard_modem; // the datagram being taken came off the modem: it has no place
        p8 v4; // no IPv6 here: the socket is AF_INET
        struct waterlink_identity me;
        struct waterlink_admission admission;
        struct link_session session[LINK_SESSIONS];
        link_stamp_entry stamp[LINK_PEERS_MAX];
        positive stamps;
        p8 stamps_dirty; // changed since /root/link.stamps was written
        link_state state;
        p64 state_written;
        p8 state_dirty;
        //      A run of full datagrams for one place, sent as segments.
        p8 batch[LINK_SEGMENTS * WATERLINK_DATAGRAM];
        positive batched;
        p8 batch_address[16];
        p16 batch_port;
        p64 put_at; // the time of the flush sending now: what a datagram put in the modem's queue is stamped with
} link_service;

static link_service link_self;

static p32 link_index_new(void)
{
        for (positive attempt = 0; attempt < 128; attempt++)
        {
                p32 index = 0;
                p8 taken = false;

                if (system_random_fill(address_of index, sizeof index, 0) < 0)
                        return 0;
                if (!index)
                        continue;
                for (positive at = 0; at < LINK_SESSIONS; at++)
                {
                        struct link_session address_to s = link_self.session + at;

                        if (s->used && (s->now.ours == index ||
                                        s->next.ours == index ||
                                        s->before.ours == index))
                                taken = true;
                }
                if (!taken)
                        return index;
        }
        return 0;
}

static p8 link_session_open(struct link_session address_to s)
{
        bipolar mapped = system_call_6(syscall(mmap), 0,
                                       sizeof(struct waterlink_link), 3, 0x22,
                                       (positive)(bipolar)-1, 0);

        if (mapped < 0 && mapped > -4096)
                return false;

        memory_zero(s, sizeof(address_to s));
        s->link = (struct waterlink_link address_to)mapped;
        waterlink_link_reset(s->link);
        s->used = true;
        s->pid = 0;
        s->pidfd = s->terminal = -1;
        for (positive at = 0; at < 2; at++)
        {
                s->reads[at].fd = s->writes[at].fd = -1;
                s->reads[at].done = s->writes[at].done = true;
        }
        s->opened = s->heard = s->spoke = link_now();
        return true;
}

static p8 link_part_owned(bipolar handle, p8 address_to path)
{
        file_facts opened;
        file_facts named;

        return handle >= 0 &&
               file_look(handle, (string_address)"", AT_EMPTY_PATH,
                         address_of opened) &&
               file_look(AT_FDCWD, (string_address)path,
                         AT_SYMLINK_NOFOLLOW, address_of named) &&
               file_same_identity(address_of opened, address_of named);
}

/*
        Whether a path is one of the files the link is made of: its key, the
        machines and groups it knows, the stamps it keeps, what the listener
        says of itself and the lock it holds, and the script the machine runs
        at boot. `files` is every file root has, bar these: the key is this
        machine to every machine that knows it, the machines and groups say
        who else may be anything here, which no peer is to be handed by a
        grant for files, and what the listener writes is its own to write,
        or `moonwater link` would say what the listener did not. Looked at by inode, so a link, a second name or a
        path spelled round one is the same file. Reserved names remain
        protected before their files exist, by the parent directory's inode.
*/
static p8 link_path_is_own(string_address path)
{
        //      Not static: on Darwin each path is made at the call (link_state_path),
        //      so the table cannot be a constant.
        string_address own[] = {
            LINK_KEY_PATH, LINK_PEERS_PATH, LINK_GROUPS_PATH, LINK_STAMPS_PATH,
            LINK_STATE_PATH, LINK_LOCK_PATH, HOST_MACHINE_SCRIPT,
            LINK_MODEM_PATH, LINK_MODEM_SEQ_PATH, LINK_MODEM_RX_PATH};
        file_facts target;
        p8 exists = file_look_at(path, address_of target);
        positive leaf_at = memory_after_last(path, '/', string_length(path));

        for (positive at = 0; at < array_count(own); at++)
        {
                file_facts named;

                if (exists && file_look_at(own[at], address_of named) &&
                    file_same_identity(address_of target, address_of named))
                        return true;
                positive own_leaf = memory_after_last(own[at], '/',
                                                       string_length(own[at]));
                if (!string_compare(path + leaf_at, own[at] + own_leaf))
                {
                        p8 leaf[256];
                        bipolar parent = system_open_parent_pinned(
                            AT_FDCWD, path, leaf, sizeof leaf);
                        bipolar reserved = system_open_parent_pinned(
                            AT_FDCWD, own[at], leaf, sizeof leaf);
                        file_facts here, there;
                        p8 protected = parent < 0 || reserved < 0 ||
                            !file_look(parent, "", AT_EMPTY_PATH, address_of here) ||
                            !file_look(reserved, "", AT_EMPTY_PATH, address_of there) ||
                            file_same_identity(address_of here, address_of there);

                        if (parent >= 0)
                                system_close(parent);
                        if (reserved >= 0)
                                system_close(reserved);
                        if (protected)
                                return true;
                }
        }
        return false;
}

/*
        A staging file is removed, or renamed over its name, only while the
        name is still the inode the transfer holds open. A name replaced
        underneath it belongs to somebody else.
*/
static fn link_part_discard(bipolar handle, p8 address_to part)
{
        if (link_part_owned(handle, part))
                system_remove_at(AT_FDCWD, part, 0);
}

static p8 link_part_publish(bipolar handle, p8 address_to part,
                              p8 address_to whole)
{
        if (!link_part_owned(handle, part))
                return false;
        if (system_rename_at(AT_FDCWD, part, AT_FDCWD, whole, 0) >= 0)
                return true;
        link_part_discard(handle, part);
        return false;
}

#if !defined(MACOS)
#define LINK_P_PIDFD 3
//      siginfo: code at 8, the pid at 16 (zero while it runs, under
//      WNOHANG), the status at 24.
#define LINK_SI_PID 4
#define LINK_SI_STATUS 6
#endif

/*
        A handle that becomes readable when a process ends: a pidfd on Linux,
        and on Darwin a kqueue that watches the pid for NOTE_EXIT. A process
        that has ended already cannot be watched, so its kqueue is made
        readable at once instead, and it is waited for as it would be. -1 is
        the same as a pidfd that could not be opened: nothing is watched.
*/
static bipolar link_child_watch(positive pid)
{
#if defined(MACOS)
        struct kevent change;
        bipolar queue = kqueue();

        if (queue < 0)
                return -1;
        (void)fcntl(queue, F_SETFD, FD_CLOEXEC);
        EV_SET(address_of change, (uintptr_t)pid, EVFILT_PROC,
               EV_ADD | EV_ONESHOT, NOTE_EXIT, 0, null);
        if (kevent(queue, address_of change, 1, null, 0, null) < 0)
        {
                //      Only a pid that is gone (ESRCH) is made ready. Any other
                //      refusal leaves nothing watched, as a failed pidfd does:
                //      a live child that reads ready would spin the wait.
                if (errno != ESRCH)
                {
                        system_close(queue);
                        return -1;
                }
                EV_SET(address_of change, 0, EVFILT_USER, EV_ADD | EV_ONESHOT,
                       NOTE_TRIGGER, 0, null);
                if (kevent(queue, address_of change, 1, null, 0, null) < 0)
                {
                        system_close(queue);
                        return -1;
                }
        }
        return queue;
#else
        return system_call_2(syscall(pidfd_open), (positive)pid, 0);
#endif
}

/*
        Whether the command behind a pidfd has ended, and taken off the
        kernel's table when it has, with the status a shell would give it. A
        command that cannot be waited for is as ended as it will be known.
*/
static p8 link_command_ended(bipolar pidfd, bipolar pid, b32 address_to status)
{
#if defined(MACOS)
        //      Darwin's handle is a kqueue, and the child is waited for by its
        //      pid. waitid reaps it, as the pidfd form does.
        siginfo_t information;
        int waited;

        (void)pidfd;
        memory_zero(address_of information, sizeof information);
        waited = waitid(P_PID, (id_t)pid, address_of information, WEXITED | WNOHANG);
        if (waited < 0)
        {
                if (errno != ECHILD)
                        return false;
                address_to status = LINK_FAILED;
                return true;
        }
        if (!information.si_pid)
                return false;
        address_to status = information.si_code == CLD_EXITED
                                    ? information.si_status
                                    : 128 + information.si_status;
        return true;
#else
        p8 information[128];
        bipolar waited;

        (void)pid;
        memory_zero(information, sizeof information);
        waited = system_call_5(syscall(waitid), LINK_P_PIDFD, (positive)pidfd,
                               (positive)information, 4 | 1, 0);
        if (waited == -ECHILD)
        {
                address_to status = LINK_FAILED;
                return true;
        }
        if (waited < 0 || !((b32 address_to)information)[LINK_SI_PID])
                return false;
        {
                b32 address_to siginfo = (b32 address_to)information;

                address_to status = siginfo[2] == 1 ? siginfo[LINK_SI_STATUS]
                                                    : 128 + siginfo[LINK_SI_STATUS];
        }
        return true;
#endif
}

/*
        A session that ends hangs up on its command, which is not yet gone
        when it does, and a child nobody waits for stays a zombie until the
        listener ends: a `run` peer could fill the machine's table of
        processes with them. So the pidfd of a command that is still there
        waits here, and every turn asks after each. A command that ignores
        the hangup is waited for when it does end, and past the last place
        the oldest is let go, as every one was before.
*/
#define LINK_REAPING 16
static struct
{
        bipolar handle; // the pidfd; on Darwin, the kqueue that watches the pid
        bipolar pid;
} link_reaping[LINK_REAPING];
static positive link_reaping_count;

static fn link_reap_keep(bipolar pidfd, bipolar pid)
{
        b32 status;

        if (link_command_ended(pidfd, pid, address_of status))
        {
                system_close(pidfd);
                return;
        }
        if (link_reaping_count == LINK_REAPING)
        {
                system_close(link_reaping[0].handle);
                memory_copy(link_reaping, link_reaping + 1,
                            (LINK_REAPING - 1) * sizeof link_reaping[0]);
                link_reaping_count--;
        }
        link_reaping[link_reaping_count].handle = pidfd;
        link_reaping[link_reaping_count].pid = pid;
        link_reaping_count++;
}

static fn link_reap(void)
{
        for (positive at = 0; at < link_reaping_count;)
        {
                b32 status;

                if (!link_command_ended(link_reaping[at].handle,
                                        link_reaping[at].pid, address_of status))
                {
                        at++;
                        continue;
                }
                system_close(link_reaping[at].handle);
                link_reaping[at] = link_reaping[--link_reaping_count];
        }
}

static fn link_session_close(struct link_session address_to s)
{
        if (s->part[0])
                link_part_discard(s->writes[0].fd, s->part);
        //      Signalled before it is waited for, as the group is: once the
        //      command is reaped its number may be another's. On Darwin there
        //      is no pidfd to signal, so the pid itself is, while it is unreaped.
#if defined(MACOS)
        if (s->pid > 0 && !s->exited)
                (void)kill((pid_t)s->pid, SIGHUP);
#else
        if (s->pidfd >= 0)
                (void)system_call_4(syscall(pidfd_send_signal),
                                    (positive)s->pidfd, 1, 0, 0);
#endif
        //      The group too, while the command is not yet waited for: once
        //      it is, its number may be some other process's.
        if (s->pid > 0 && !s->exited)
        {
#if defined(MACOS)
                (void)kill((pid_t)-s->pid, SIGHUP);
#else
                (void)system_call_2(syscall(kill), (positive)-s->pid, 1);
#endif
        }
        if (s->pidfd >= 0)
        {
                if (s->exited)
                        system_close(s->pidfd);
                else
                        link_reap_keep(s->pidfd, s->pid);
        }
        for (positive at = 0; at < 2; at++)
        {
                if (s->reads[at].fd > 2 && s->reads[at].fd != s->terminal)
                        system_close(s->reads[at].fd);
                if (s->writes[at].fd > 2 && s->writes[at].fd != s->terminal)
                        system_close(s->writes[at].fd);
        }
        if (s->terminal >= 0)
                system_close(s->terminal);
        if (s->link)
                system_call_2(syscall(munmap), (positive)s->link,
                              sizeof(struct waterlink_link));
        crypto_forget(s, sizeof(address_to s));
        link_self.state_dirty = true;
}

static fn link_keys_install(struct link_keys address_to keys,
                            p8 address_to send, p8 address_to receive,
                            p32 ours, p32 theirs)
{
        struct link_session address_to owner = null;

        for (positive at = 0; at < LINK_SESSIONS; at++)
                if ((p8 address_to)keys >= (p8 address_to)(link_self.session + at) &&
                    (p8 address_to)keys < (p8 address_to)(link_self.session + at + 1))
                        owner = link_self.session + at;
        if (owner)
                owner->keyings++;
        memory_zero(keys, sizeof(address_to keys));
        crypto_aesgcm_prepare(address_of keys->send, send);
        crypto_aesgcm_prepare(address_of keys->receive, receive);
        keys->ours = ours;
        keys->theirs = theirs;
        keys->made = link_now();
        keys->live = true;
}

// Sending ------------------------------------------------------------------

/*
        Where a datagram goes, or where the socket binds, in the socket's own
        family: an IPv4 address is mapped on a dual-stack socket and plain on
        an IPv4 one.
*/
/*
        The modem carries the link's datagrams while the transport is the modem: these are its parts this file calls before they
        are made. They are in the send section of modem.c, after the sound server's client.
*/
static p8 link_modem_carrying(void);
static p8 link_modem_put(p8 address_to bytes, positive length, p64 when);
static p64 link_modem_turn(p64 now);
static void link_modem_stop(void);
static p64 link_modem_floor(void);
static p64 link_modem_ceiling(void);
static p64 link_modem_queued_from(void);
static positive link_modem_free(void);

static positive link_destination(socket_address_internet6 address_to into,
                                 p8 address_to address, p16 port)
{
        if (link_self.v4)
        {
                *(socket_address_internet address_to)into =
                        (socket_address_internet){
                            .family = AF_INET, .port = network_order_16(port),
                            .host = network_order_32(network_load_32(address + 12))};
#if defined(MACOS)
                ((socket_address_internet address_to)into)->length =
                        sizeof(socket_address_internet);
#endif
                return sizeof(socket_address_internet);
        }
        link_socket_address(into, address, port);
        return sizeof(address_to into);
}

static bipolar link_udp_send_to(p8 address_to bytes, positive length,
                                p8 address_to address, p16 port)
{
        socket_address_internet6 to;
        positive size = link_destination(address_of to, address, port);
        bipolar sent;

        if (link_self.v4 && !link_address_mapped(address))
#if defined(MACOS)
                return -EAFNOSUPPORT; // no IPv6 here
#else
                return -97; // EAFNOSUPPORT: no IPv6 here
#endif
        sent = socket_send((b32)link_self.socket, bytes, length, MSG_NOSIGNAL,
                           address_of to, size);
        return sent;
}

/*
        A datagram of the link's, to one place: onto the socket, or, while the transport is the modem, into the modem's queue
        (modem.c), which plays it at the modem's rate. The place means nothing over the modem. Answers the length sent, or a
        negative error; a full queue loses the datagram, as a full socket does, and the link sends it again.
*/
static bipolar link_send_to(p8 address_to bytes, positive length,
                            p8 address_to address, p16 port)
{
        if (link_modem_carrying())
                return link_modem_put(bytes, length, link_self.put_at)
                               ? (bipolar)length
                               : -EAGAIN;
        return link_udp_send_to(bytes, length, address, port);
}

/*
        The link's view of the transport, told before it fills anything: over the modem its timer is floored at the queue's
        worth of air, its window is no bigger than the queue, and it knows when the oldest datagram the modem holds was sent.
        Over the network the link keeps what it always had.
*/
static fn link_transport_tell(struct link_session address_to s)
{
        if (link_modem_carrying())
                waterlink_transport(s->link, link_modem_floor(), link_modem_ceiling(), link_modem_queued_from());
        else
                waterlink_transport(s->link, 0, WATERLINK_WINDOW_MOST, ~0ull);
}

/*
        A run of full datagrams to one place in one call, cut by the kernel:
        UDP_SEGMENT, which is what the fixed size is for (waterlink.c). A
        kernel or a device that refuses it once is not asked again.
*/
typedef struct
{
        address_any base;
        positive length;
} link_iovec;

typedef struct
{
        address_any name;
        b32 name_length;
        b32 padding;
        link_iovec address_to parts;
        positive part_count;
        address_any control;
        positive control_length;
        b32 flags;
        b32 padding2;
} link_message;

/*
        The data of the first control message with this level and type, among
        the length bytes the kernel filled -- not the room the buffer has,
        which holds whatever an earlier call left. Each cmsghdr is a length
        that counts itself, a level and a type, and is padded to eight bytes;
        a message that claims more than was filled ends the walk. True with
        data a reader on the message's bytes.
*/
static p8 link_control_find(p8 address_to control, positive length,
                              positive room, b32 level, b32 type,
                              byte_reader address_to data)
{
        byte_reader reader = byte_reader_open(control,
                                              length < room ? length : room);

        while (byte_reader_left(&reader) >= 16)
        {
                p64 size = (p32)byte_reader_uint(&reader, 4, false);
                b32 has_level;
                b32 has_type;

                size |= (p64)(p32)byte_reader_uint(&reader, 4, false) << 32;
                has_level = (p32)byte_reader_uint(&reader, 4, false);
                has_type = (p32)byte_reader_uint(&reader, 4, false);
                if (size < 16 || size - 16 > byte_reader_left(&reader))
                        break;
                *data = byte_reader_window(&reader, (positive)size - 16);
                if (has_level == level && has_type == type)
                        return true;
                //      The padding, when the kernel kept it: the last message
                //      may end where the filled length does.
                (void)byte_reader_skip(
                        &reader, (positive)(-size & 7) < byte_reader_left(&reader)
                                         ? (positive)(-size & 7)
                                         : byte_reader_left(&reader));
        }
        return false;
}

//      The int such a message carries, or -1.
static b32 link_control_int(p8 address_to control, positive length,
                            positive room, b32 level, b32 type)
{
        byte_reader data;

        return link_control_find(control, length, room, level, type,
                                 address_of data) &&
                       byte_reader_left(&data) >= 4
                       ? (b32)(p32)byte_reader_uint(&data, 4, false)
                       : -1;
}

static fn link_batch_flush(void)
{
        positive count = link_self.batched;

        link_self.batched = 0;
        if (!count)
                return;

        if (count > 1 && link_self.gso && !link_modem_carrying())
        {
                socket_address_internet6 to;
                link_iovec part = {link_self.batch, count * WATERLINK_DATAGRAM};
                p64 control[3];
                link_message message;
                bipolar sent;

                positive size = link_destination(address_of to,
                                                 link_self.batch_address,
                                                 link_self.batch_port);
                memory_zero(control, sizeof control);
                control[0] = 18;                        // cmsg_len
                ((b32 address_to)control)[2] = SOL_UDP;
                ((b32 address_to)control)[3] = UDP_SEGMENT;
                ((p16 address_to)control)[8] = WATERLINK_DATAGRAM;

                memory_zero(address_of message, sizeof message);
                message.name = address_of to;
                message.name_length = (b32)size;
                message.parts = address_of part;
                message.part_count = 1;
                message.control = control;
                message.control_length = 24;

                sent = system_call_3(syscall(sendmsg), (positive)link_self.socket,
                                     (positive)address_of message,
                                     MSG_NOSIGNAL);
                if (sent >= 0)
                        return;
                //      A full socket takes what has room for it, a datagram at
                //      a time, where the run it could not take whole was lost
                //      to the link whole; anything else turns segments off.
                if (sent != -EAGAIN)
                        link_self.gso = false;
        }

        for (positive at = 0; at < count; at++)
                (void)link_send_to(link_self.batch + at * WATERLINK_DATAGRAM,
                                   WATERLINK_DATAGRAM, link_self.batch_address,
                                   link_self.batch_port);
}

/*
        Everything a session's link will send now: each datagram numbered,
        sealed and sent, the full ones that need not go alone into a run and
        the rest by themselves at their own length.
*/
static fn link_session_flush(struct link_session address_to s, p64 now)
{
        //      Every datagram this flush puts in the modem's queue is stamped
        //      with the time its frames are sent: the same as the frames.
        link_self.put_at = now;
        /* Filling frames cannot move the session. Check a pending batch's
           destination once, and copy this one's only when a new batch starts. */
        if (link_self.batched &&
            (memory_compare(link_self.batch_address, s->address, 16) ||
             link_self.batch_port != s->port))
                link_batch_flush();

        for (positive guard = 0; guard < 256; guard++)
        {
                p8 address_to datagram;
                struct waterlink_datagram head;
                p8 alone = false;
                positive used;
                positive length;

                if (link_self.batched == LINK_SEGMENTS)
                        link_batch_flush();

                link_transport_tell(s);
                //      Over the modem, a datagram is filled only when the queue
                //      has room for it behind those batched: what the window
                //      allows is not a count of datagrams, and a frame sent
                //      into a full queue would be lost there. The rest waits.
                if (link_modem_carrying() && link_modem_free() <= link_self.batched)
                        break;

                datagram = link_self.batch + link_self.batched *
                                                     WATERLINK_DATAGRAM;
                used = waterlink_fill(s->link, datagram + 16, now,
                                      address_of alone);
                if (!used)
                        break;

                head.kind = WATERLINK_KIND_CARRY;
                head.receiver = s->now.theirs;
                head.counter = s->now.counter++;
                memory_copy(datagram, address_of head, 16);
                s->spoke = now;

                length = waterlink_seal(address_of s->now.send, datagram, used);
                if (alone || length < WATERLINK_DATAGRAM)
                {
                        //      What was filled before it leaves before it: a
                        //      datagram sent past the run still in the batch
                        //      reached the far side first, and its
                        //      acknowledgement made the run's frames look
                        //      three transmissions late, lost.
                        link_batch_flush();
                        (void)link_send_to(datagram, length, s->address,
                                           s->port);
                        continue;
                }

                if (!link_self.batched)
                {
                        memory_copy(link_self.batch_address, s->address, 16);
                        link_self.batch_port = s->port;
                }
                link_self.batched++;
        }
        link_batch_flush();
}

// A sealed datagram of nothing: a keepalive, or the end.
static fn link_session_say(struct link_session address_to s, p32 kind)
{
        p8 datagram[64];
        struct waterlink_datagram head = {kind, s->now.theirs,
                                          s->now.counter++};
        positive length;

        memory_copy(datagram, address_of head, 16);
        length = waterlink_seal(address_of s->now.send, datagram, 0);
        (void)link_send_to(datagram, length, s->address, s->port);
        s->spoke = link_now();
}

/*      A frame whose payload is made already, its type byte first: the link
        copies it into its slot, and that is the only copy a read's bytes
        make. */

static p8 link_post(struct link_session address_to s, p8 key, p8 flags,
                      p8 type, p8 address_to data, positive length)
{
        p8 payload[WATERLINK_FRAME_MAX];

        if (length > WATERLINK_FRAME_MAX - 1)
                return false;
        payload[0] = type;
        if (length)
                memory_copy(payload + 1, data, length);
        return waterlink_post(((struct link_session *)(s))->link, key, flags, (p8 *)(payload), (p16)((positive)(length + 1)), link_self.now);
}

/*
        How much may be read now: a read is sized to the slots free, keeping
        one back so an ending always has somewhere to go, so a busy stream
        costs one system call for a run of frames and not one a frame.
*/
#define LINK_READ_FRAMES 48
#define LINK_CHUNK (WATERLINK_FRAME_MAX - 1)

/*      One byte more than the frames hold, in front: a chunk of a read is
        posted from the byte before it, which is the type of its frame, and
        that byte is the end of the chunk before it, posted already. */
static p8 link_read_buffer[1 + LINK_READ_FRAMES * LINK_CHUNK];

static positive link_room(struct link_session address_to s)
{
        p32 room = waterlink_room(s->link);

        return room > 1 ? (room - 1 < LINK_READ_FRAMES ? room - 1
                                                       : LINK_READ_FRAMES)
                        : 0;
}

/*
        A terminal's size on the wire: rows then columns, each little endian.
        Packing answers whether the size is known; 24 by 80 when it is not.
*/
static p8 link_size_pack(p8 address_to packed)
{
        winsize size = {24, 80, 0, 0};
        p8 known = system_control(0, TIOCGWINSZ, address_of size) >= 0;

        memory_store_unaligned(p16, packed, size.rows);
        memory_store_unaligned(p16, packed + 2, size.columns);
        return known;
}

static winsize link_size_unpack(p8 address_to packed)
{
        return (winsize){(p16)(packed[0] | packed[1] << 8),
                         (p16)(packed[2] | packed[3] << 8), 0, 0};
}

// The streams --------------------------------------------------------------

static fn link_stream_set(struct link_stream address_to stream, bipolar fd,
                          p8 key, p8 flags)
{
        stream->fd = fd;
        stream->key = key;
        stream->flags = flags;
        stream->done = fd < 0;
        stream->quiet = false;
        stream->socket = false;
        stream->skip = 0;
        stream->error = 0;
        stream->written = stream->started = 0;
}

/*
        One of the client's standard descriptors, as a stream no read or
        write ever waits on: a write that waited on a full pipe or a paused
        terminal stopped the whole loop with it -- keepalives, acknowledgements
        and the rekey -- until the far side gave the link up. A pipe or a
        terminal is opened again through /proc, a description of this
        process's own that can be nonblocking without changing it for whoever
        shares it. A socket cannot be opened again and is asked with
        MSG_DONTWAIT on each call instead. A file never waits, and is kept as
        it is: opened again it would lose its offset and O_APPEND.

        Darwin has no /proc. Its /dev/fd/N is no new description either: an
        open there is a dup of the descriptor, the same open file, and the
        open flags (O_NONBLOCK among them) are not applied to it. So on Darwin
        the descriptor is passed as it is and made nonblocking itself, which
        is the description the shell that started the client shares; the
        flags it had are kept, and link_stdio_restore puts them back when the
        client ends, so the terminal is not left nonblocking for that shell.
*/
#if defined(MACOS)
static b32 link_stdio_flags[3] = {-1, -1, -1};

static fn link_stdio_restore(void)
{
        for (b32 at = 0; at < 3; at++)
                if (link_stdio_flags[at] >= 0)
                {
                        (void)fcntl((int)at, F_SETFL, (int)link_stdio_flags[at]);
                        link_stdio_flags[at] = -1;
                }
}
#endif

static fn link_stream_own(struct link_stream address_to stream, bipolar fd,
                          p8 key, p8 flags, positive access)
{
#if !defined(MACOS)
        p8 path[] = "/proc/self/fd/0";
#endif
        file_facts facts;
        p32 format;

        link_stream_set(stream, fd, key, flags);
        if (fd < 0 || fd > 2)
                return;
        if (!file_look(fd, (string_address)"", AT_EMPTY_PATH, address_of facts))
        {
                stream->error = -EIO;
                stream->fd = -1;
                stream->done = true;
                return;
        }
        format = facts.mode & MODE_FORMAT;
#if !defined(MACOS)
        path[14] = (p8)('0' + fd);
#endif
        if (format == MODE_SOCKET)
                stream->socket = true;
#if defined(MACOS)
        else if (format == MODE_PIPE || format == MODE_CHARACTER)
        {
                int had = fcntl((int)fd, F_GETFL);

                (void)access;
                if (had >= 0 && (had & O_NONBLOCK))
                        return;
                if (had >= 0 && fcntl((int)fd, F_SETFL, had | O_NONBLOCK) == 0)
                        link_stdio_flags[fd] = (b32)had;
                else
                {
                        /* A blocking fallback would freeze the whole link
                           on empty input or a full output pipe. */
                        stream->error = -(bipolar)errno;
                        stream->fd = -1;
                        stream->done = true;
                }
        }
#else
        else if (format == MODE_PIPE || format == MODE_CHARACTER)
        {
                bipolar own = system_open_at(AT_FDCWD, path,
                                             access | O_NONBLOCK | O_NOCTTY |
                                                     O_CLOEXEC);

                if (own >= 0)
                        stream->fd = own;
                else
                {
                        /* A blocking fallback would freeze the whole link
                           on empty input or a full output pipe. */
                        stream->error = own;
                        stream->fd = -1;
                        stream->done = true;
                }
        }
#endif
}

static fn link_stream_close(struct link_session address_to s,
                            struct link_stream address_to stream)
{
        //      A terminal is both ends of one descriptor, and standard
        //      output and error belong to whoever started the client.
        if (stream->fd > 2 && stream->fd != s->terminal)
                system_close(stream->fd);
        stream->fd = -1;
        stream->done = true;
}

// The session's command has ended, with this status.
static fn link_exited(struct link_session address_to s, b32 status, p64 now)
{
        s->exited = true;
        s->exited_at = now;
        s->status = status;
        //      What was said of the output before the end says nothing of
        //      what the command left in the pipe: it is asked again.
        s->reads[0].quiet = s->reads[1].quiet = false;
}

static p8 link_push_unpack(struct link_session address_to s);

static fn link_push_done(struct link_session address_to s, p64 now)
{
        //      The part file becomes the name only whole, and is removed when
        //      it cannot, here and not at the close: the descriptor that says
        //      it is still this session's is closed by then, with the end of
        //      the stream, and a part file nobody could prove was theirs was
        //      left beside the target by every push that failed.
        if (!s->failed)
                s->failed = system_call_1(syscall(fsync),
                                          (positive)s->writes[0].fd) < 0;
        //      A dictionary file is unpacked into a part file of its own
        //      first, which then is the one published.
        if (!s->failed && s->packed)
                s->failed = !link_push_unpack(s);
        if (!s->failed)
                s->failed = !link_part_publish(s->writes[0].fd, s->part,
                                               s->whole);
        if (s->failed)
                link_part_discard(s->writes[0].fd, s->part);
        s->part[0] = 0;
        link_exited(s, s->failed ? 1 : 0, now);
}


// The machine's end ---------------------------------------------------------

static fn link_state_write(p64 now);

/*
        TERM as the client sent it, if it is a plain name; a terminal type
        is a file name to curses, so anything else becomes xterm.
*/
static fn link_term_word(p8 address_to from, positive length,
                         p8 address_to into)
{
        p8 good = length && length < 32;

        //      The bytes are a frame's, with no end of their own: copied
        //      first, and an end inside them ends the span short.
        if (good)
        {
                memory_copy(into, from, length);
                into[length] = 0;
                good = string_span_of_set((string_address)into,
                                          LINK_NAME_CHARACTERS "+") == length;
        }
        if (!good)
                string_copy((string_address)into, "xterm");
}

static DEAD_END fn link_child_exec(string_address address_to words,
                                    positive count, p8 address_to term)
{
        p8 term_line[48] = "TERM=";
        p8 path_line[512] = "PATH=/bin:/sbin:/usr/bin:/usr/sbin";
        string_address path = file_environment((string_address) "PATH");
        string_address environment[8];
        positive at = 0;
#if defined(MACOS)
        p8 home_line[1040] = "HOME=";
        p8 user_line[200] = "USER=";
        p8 logname_line[208] = "LOGNAME=";
        string_address user = file_environment((string_address) "USER");

        //      On a Mac the command is the user's own, in the user's home: not root's.
        string_copy_bounded((string_address)home_line + 5, link_run_home(), sizeof(home_line) - 5);
        string_copy_bounded((string_address)user_line + 5, user ? user : (string_address) "", sizeof(user_line) - 5);
        string_copy_bounded((string_address)logname_line + 8, user ? user : (string_address) "", sizeof(logname_line) - 8);
#endif

        //      Only the three descriptors the caller installed cross into
        //      the command: not the service's sockets and state files, nor
        //      whatever a hand-started `link serve` inherited.
        (void)descriptors_close_except(3, descriptors_none, descriptors_none);
#if !defined(MACOS)
        positive blocked = 0;

        (void)system_signal_mask(2, address_of blocked, null, 8);
        for (b32 signal = 1; signal < 32; signal++)
        {
                p64 action[4] = {0, 0, 0, 0};

                (void)system_call_4(syscall(rt_sigaction), (positive)signal,
                                    (positive)action, 0, 8);
        }
#else
        //      Darwin's handlers are not blocked, and a handler is reset by
        //      the exec itself: the command starts with the defaults.
#endif

        if (path && string_length(path) < sizeof path_line - 6)
        {
                string_copy((string_address)path_line, "PATH=");
                string_copy((string_address)path_line + 5, path);
        }
        string_copy((string_address)term_line + 5, (string_address)term);

        environment[at++] = (string_address)path_line;
        environment[at++] = (string_address)term_line;
#if defined(MACOS)
        environment[at++] = (string_address)home_line;
        environment[at++] = (string_address)user_line;
        environment[at++] = (string_address)logname_line;
#else
        environment[at++] = (string_address) "HOME=/root";
        environment[at++] = (string_address) "USER=root";
        environment[at++] = (string_address) "LOGNAME=root";
#endif
        environment[at++] = (string_address) "SHELL=/bin/sh";
        environment[at] = null;

        (void)system_call_1(syscall(chdir), (positive)(string_address)LINK_HOME_DIR);

        //      The machine's own binary, as whichever utility words[0] names.
        (void)shell_exec_file((string_address)LINK_EXECUTABLE, words, count,
                              environment);
        system_call_1(syscall(exit), 127);
        unreachable_here();
}

static p8 link_nonblocking(bipolar handle)
{
        return system_call_3(syscall(fcntl), (positive)handle, 4,
                             O_NONBLOCK | FILE_READ_WRITE) >= 0;
}

static p8 link_start_shell(struct link_session address_to s,
                             p8 address_to request, positive length)
{
        b32 master = -1;
        b32 slave = -1;
        winsize size = {24, 80, 0, 0};
        p8 term[32];
        bipolar child;

        if (length >= 4)
                size = link_size_unpack(request);
        link_term_word(request + 4, length > 4 ? length - 4 : 0, term);

        if (process_pty_open(address_of master, address_of slave, true) < 0)
                return false;
        system_control(slave, TIOCSWINSZ, address_of size);

        child = system_fork();
        if (child < 0)
        {
                system_close(master);
                system_close(slave);
                return false;
        }
        if (!child)
        {
                string_address words[] = {"sh", "-i", null};

                if (process_pty_child_setup(master, slave, -1, -1) < 0)
                        system_call_1(syscall(exit), 126);
                link_child_exec(words, 2, term);
        }

        system_close(slave);
        s->pid = child;
        s->pidfd = link_child_watch((positive)child);
        s->terminal = master;
        link_stream_set(s->reads, master, LINK_KEY_OUTPUT,
                        WATERLINK_FRAME_DURABLE);
        link_stream_set(s->writes, master, LINK_KEY_INPUT, 0);
        return true;
}

static p8 link_start_command(struct link_session address_to s,
                               string_address address_to words, positive count)
{
        b32 in[2], out[2], err[2];
        bipolar child;

        if (system_pipe(in, O_CLOEXEC) < 0)
                return false;
        if (system_pipe(out, O_CLOEXEC) < 0)
        {
                system_close(in[0]);
                system_close(in[1]);
                return false;
        }
        if (system_pipe(err, O_CLOEXEC) < 0)
        {
                system_close(in[0]);
                system_close(in[1]);
                system_close(out[0]);
                system_close(out[1]);
                return false;
        }

        child = system_fork();
        if (!child)
        {
                (void)system_call(syscall(setsid));
                if (system_descriptor_install(in[0], 0) < 0 ||
                    system_descriptor_install(out[1], 1) < 0 ||
                    system_descriptor_install(err[1], 2) < 0)
                        system_call_1(syscall(exit), 126);
                link_child_exec(words, count, (p8 address_to) "dumb");
        }

        system_close(in[0]);
        system_close(out[1]);
        system_close(err[1]);
        if (child < 0)
        {
                system_close(in[1]);
                system_close(out[0]);
                system_close(err[0]);
                return false;
        }

        link_nonblocking(in[1]);
        link_nonblocking(out[0]);
        link_nonblocking(err[0]);
        s->pid = child;
        s->pidfd = link_child_watch((positive)child);
        link_stream_set(s->reads, out[0], LINK_KEY_OUTPUT,
                        WATERLINK_FRAME_DURABLE);
        link_stream_set(s->reads + 1, err[0], LINK_KEY_ERROR,
                        WATERLINK_FRAME_DURABLE);
        link_stream_set(s->writes, in[1], LINK_KEY_INPUT, 0);
        return true;
}

/*
        push, pull and log: the same streams as run, with a file or the
        kernel log where the command's pipes were and no process behind them.
        A pushed file is written where it is named but never through a link
        standing at that name, and is only there under its name once it is
        whole; a pulled file is read as it is.
*/
static bipolar link_part_open(string_address target, positive length,
                              p8 address_to part, positive room, p32 mode)
{
        /* Check by subtraction: `length` is normally a bounded request or
           argv string, but this helper must not turn a wrapped length into a
           gigantic copy if either boundary ever changes. */
        if (room < 28 || length > room - 28)
                return -ERROR_NAME_TOO_LONG;

        memory_copy(part, target, length);
        memory_copy(part + length, ".link-part.", 11);
        for (positive attempt = 0; attempt < 8; attempt++)
        {
                p8 random[8];
                bipolar handle;

                if (system_random_fill(random, sizeof random, 0) < 0)
                        return -EIO;
                memory_into_hex(part + length + 11, random, 8);
                part[length + 27] = 0;
                //      To read as well as to write: a packed push is read
                //      back from the stage it was packed into, which a
                //      descriptor opened to write alone refuses (EBADF),
                //      and that failed every push that packed.
                handle = system_open_at_mode(
                        AT_FDCWD, part,
                        FILE_READ_WRITE | FILE_CREATE | FILE_EXCLUSIVE |
                                O_NOFOLLOW | O_CLOEXEC,
                        mode);
                if (handle >= 0 || handle != -ERROR_EXISTS)
                        return handle;
        }
        return -ERROR_EXISTS;
}

/*
        The dictionary format of a pushed file. The sender packs the file
        block by block, each block one zstd frame made with the dictionary
        both ends hold, and each frame behind its length in four big-endian
        bytes. The receiver writes the packed bytes as they come, as a raw
        file is written, and unpacks them when the file has ended; the
        sealed Waterlink datagrams carry the packed bytes like any others,
        and nothing below the file is compressed. A frame is never longer
        than PUSH_FRAME_MAX and decodes to at most PUSH_BLOCK bytes, so what
        is written is bounded, and a frame that does not decode with this
        dictionary is a failed push, never a misread one.
*/
static p8 push_block[PUSH_BLOCK];
static p8 push_frame[PUSH_FRAME_MAX];


/* Reads n bytes, or fewer only at the end of the file: what it read, or a negative error. */
static bipolar push_fill(bipolar handle, p8 address_to into, positive n)
{
        positive got = 0;

        while (got < n)
        {
                bipolar took = system_read_retry((positive)handle, into + got,
                                                 n - got);

                if (took < 0)
                        return took;
                if (took == 0)
                        break;
                got += (positive)took;
        }
        return (bipolar)got;
}

static p8 push_put(bipolar handle, p8 address_to from, positive n)
{
        return (bipolar)system_write_all((positive)handle, from, n) ==
               (bipolar)n;
}

/* Packs input into output from its present offset to its end. False when a read, the encoder or a write fails. */
static p8 push_pack(bipolar input, bipolar output)
{
        if (!(p8)(zstd_dictionary_load(push_dictionary, PUSH_DICTIONARY_SIZE)))
                return false;
        for (;;)
        {
                bipolar took = push_fill(input, push_block, PUSH_BLOCK);
                bipolar frame;
                p8 head[4];

                if (took < 0)
                        return false;
                if (took == 0)
                        return true;
                frame = zstd_deflate_mem(push_block, (positive)took, push_frame,
                                         PUSH_FRAME_MAX);
                if (frame <= 0 || (positive)frame > PUSH_FRAME_MAX)
                        return false;
                head[0] = (p8)((frame >> 24) & 0xff);
                head[1] = (p8)((frame >> 16) & 0xff);
                head[2] = (p8)((frame >> 8) & 0xff);
                head[3] = (p8)(frame & 0xff);
                if (!push_put(output, head, 4) ||
                    !push_put(output, push_frame, (positive)frame))
                        return false;
                if ((positive)took < PUSH_BLOCK)
                        return true;
        }
}

/* Unpacks input into output from its present offset to its end. False when the input ends inside a frame, or a frame does not decode with this dictionary. */
static p8 push_unpack(bipolar input, bipolar output)
{
        if (!(p8)(zstd_dictionary_load(push_dictionary, PUSH_DICTIONARY_SIZE)))
                return false;
        for (;;)
        {
                p8 head[4];
                bipolar took = push_fill(input, head, 4);
                positive frame;
                bipolar plain;

                if (took < 0)
                        return false;
                if (took == 0)
                        return true;
                if (took != 4)
                        return false;
                frame = ((positive)(head[0] & 0xff) << 24) |
                        ((positive)(head[1] & 0xff) << 16) |
                        ((positive)(head[2] & 0xff) << 8) |
                        (positive)(head[3] & 0xff);
                if (!frame || frame > PUSH_FRAME_MAX)
                        return false;
                if (push_fill(input, push_frame, frame) != (bipolar)frame)
                        return false;
                plain = zstd_inflate(push_frame, frame, push_block, PUSH_BLOCK);
                if (plain < 0 || !push_put(output, push_block, (positive)plain))
                        return false;
        }
}

/*
        The file a push sends, packed into a file that is unlinked as soon as
        it exists, so only this process holds it. It is staged beside the
        source, as a push is staged beside its target, and rewound to its
        start. Answers the descriptor, or -1 when the file cannot be packed,
        which sends the source as it is; the source is rewound either way.
*/
static bipolar push_pack_stage(string_address path, bipolar input)
{
        p8 part[LINK_REQUEST_MAX + 32];
        bipolar output = link_part_open(path, string_length(path), part,
                                        sizeof part, 0600);
        p8 packed = false;

        if (output >= 0)
        {
                system_remove_at(AT_FDCWD, part, 0);
                packed = push_pack(input, output) &&
                         (bipolar)system_seek(output, 0, 0) >= 0;
                if (!packed)
                {
                        system_close(output);
                        output = -1;
                }
        }
        (void)system_seek(input, 0, 0);
        return packed ? output : -1;
}

/*
        A dictionary file's staging file is unpacked into a part file of its
        own, beside the name and with the file's mode, which then is the staging
        file the publish takes; the packed one is removed. On failure the packed
        file is still the staging file, and the caller removes it as it does any
        failed push.
*/
static p8 link_push_unpack(struct link_session address_to s)
{
        p8 part[LINK_REQUEST_MAX + 32];
        bipolar packed, plain;
        p8 ok;

        if (!link_part_owned(s->writes[0].fd, s->part))
                return false;
        packed = system_open_at(AT_FDCWD, s->part,
                                FILE_READ | O_NOFOLLOW | O_CLOEXEC);
        if (packed < 0)
                return false;
        plain = link_part_open((string_address)s->whole,
                               string_length(s->whole), part, sizeof part, 0600);
        if (plain < 0)
        {
                system_close(packed);
                return false;
        }
        (void)system_call_2(syscall(fchmod), (positive)plain, s->mode);
        ok = push_unpack(packed, plain) &&
             system_call_1(syscall(fsync), (positive)plain) >= 0;
        system_close(packed);
        if (!ok)
        {
                link_part_discard(plain, part);
                system_close(plain);
                return false;
        }
        link_part_discard(s->writes[0].fd, s->part);
        system_close(s->writes[0].fd);
        s->writes[0].fd = plain;
        string_copy((string_address)s->part, (string_address)part);
        return true;
}

static p8 link_start_push(struct link_session address_to s, p8 address_to path,
                            positive length, p32 mode)
{
        p8 part[LINK_REQUEST_MAX + 32];
        bipolar handle;

        /* A fresh exclusive name prevents both collisions between concurrent
           transfers and a planted predictable name from denying every push.
           It remains beside the target so the final rename is atomic. */
        handle = link_part_open((string_address)path, length, part,
                                sizeof part, mode);
        if (handle < 0)
                return false;
        (void)system_call_2(syscall(fchmod), (positive)handle, mode);
        string_copy((string_address)s->whole, (string_address)path);
        string_copy((string_address)s->part, (string_address)part);
        link_stream_set(s->writes, handle, LINK_KEY_INPUT, 0);
        return true;
}

static fn link_refuse(struct link_session address_to s, string_address why)
{
        (void)link_post(s, LINK_KEY_ANSWER,
                        WATERLINK_FRAME_DURABLE | WATERLINK_FRAME_LAST, 'N',
                        (p8 address_to)why, string_length(why));
        //      Ends once the far side has the answer, and asks for nothing
        //      more in the meantime: link_request takes no request of a
        //      session that has said it is over.
        s->exit_sent = true;
        s->kind = LINK_KIND_NONE;
}

/* Validate the shape before any request-specific field is read. A shell
   request always carries its four-byte window size; in particular, do not
   let a short authenticated request make link_start_shell or the mode reader
   look beyond the frame payload. */
static p8 link_request_well_formed(p8 kind, p8 address_to payload,
                                     positive length)
{
        positive skip = kind == LINK_KIND_PUSH ? LINK_PUSH_HEAD : 1;

        if (kind == LINK_KIND_SHELL)
                return length >= 5 && length <= 5 + 31;
        return length >= skip && length - skip <= LINK_REQUEST_MAX &&
               !memory_first_of(payload + skip, 0, length - skip);
}

static fn link_request(struct link_session address_to s, p8 address_to payload,
                       positive length)
{
        link_peers peers;
        struct waterlink_peer address_to peer;
        p32 may = 0;
        p8 started;
        p8 why[96];
        p8 text[LINK_REQUEST_MAX + 1];
        p32 mode = 0;
        positive carried = 0;
        file_facts far;
        positive skip;
        positive grant;
        string_address run[] = {"sh", "-c", (string_address)text, null};
        string_address pull[] = {"cat", "--", (string_address)text, null};
        string_address log[] = {"dmesg", "--follow", null};

        //      One request to a session: a second one, after an answer of
        //      either kind, is nothing.
        if (s->kind != LINK_KIND_NONE || s->exit_sent || length < 1)
                return;

        //      Grants as they are now, not as they were at the handshake.
        link_peers_load(address_of peers);
        peer = link_peer_keyed(address_of peers, s->peer);
        if (peer)
                may = peer->may;

        grant = link_grant_asked(payload[0]);
        if (grant < array_count(link_grants) && !(may & link_grants[grant].bit))
        {
                string_copy_bounded((string_address)why,
                                    link_grants[grant].name, sizeof why);
                string_append_bounded((string_address)why,
                                      " is not granted to ", sizeof why);
                string_append_bounded((string_address)why,
                                      (string_address)s->name, sizeof why);
                link_refuse(s, (string_address)why);
                return;
        }

        for (positive kind = 1; kind < array_count(link_kind_asks); kind++)
                if (link_kind_asks[kind] == payload[0])
                        s->kind = (p8)kind;

        //      A command or a path is one string with no NUL in it, behind a
        //      push's mode; a terminal's request is its size and TERM.
        skip = s->kind == LINK_KIND_PUSH ? LINK_PUSH_HEAD : 1;
        if (!link_request_well_formed(s->kind, payload, length))
        {
                link_refuse(s, "that request is malformed");
                return;
        }
        s->packed = false;
        if (s->kind == LINK_KIND_PUSH)
        {
                p32 dictionary = ((p32)(payload[6] & 0xff) << 24) |
                                 ((p32)(payload[7] & 0xff) << 16) |
                                 ((p32)(payload[8] & 0xff) << 8) |
                                 (p32)(payload[9] & 0xff);

                //      The format and the dictionary are the far side's word,
                //      checked before anything is staged: a file packed another
                //      way, or with a dictionary this machine does not hold, is
                //      refused, not decoded wrongly.
                if (payload[5] == PUSH_FORMAT_DICTIONARY)
                {
                        if (dictionary != PUSH_DICTIONARY_ID)
                        {
                                link_refuse(s, "the far side packs with another dictionary: both ends must run the same moonwater");
                                return;
                        }
                        s->packed = true;
                }
                else if (payload[5] != PUSH_FORMAT_RAW)
                {
                        link_refuse(s, "that file is packed in a format this machine does not know");
                        return;
                }
        }
        if (s->kind != LINK_KIND_SHELL)
        {
                memory_copy(text, payload + skip, length - skip);
                text[length - skip] = 0;
        }
        if (s->kind == LINK_KIND_PUSH)
        {
                //      The bits a file's own owner sets, and no more: a pushed
                //      file made here with the set-id bits is one that runs as
                //      its owner, root when a root receives it. The far file's
                //      mode in a pull's answer is masked the same way.
                memory_copy(address_of mode, payload + 1, 4);
                s->mode = mode & 0777;
        }
        if ((s->kind == LINK_KIND_PUSH || s->kind == LINK_KIND_PULL) &&
            link_path_is_own((string_address)text))
        {
                link_refuse(s, "that file is part of the link itself");
                return;
        }

        //      Pull and log are commands the machine already has, named here
        //      and never parsed by a shell.
        switch (s->kind)
        {
        case LINK_KIND_SHELL:
                started = link_start_shell(s, payload + 1, length - 1);
                break;
        case LINK_KIND_RUN:
                started = text[0] && link_start_command(s, run, 3);
                break;
        case LINK_KIND_PULL:
                //      The file's mode goes with the yes, so what is pulled
                //      is as private here as it was there.
                if (file_look_at((string_address)text, address_of far))
                {
                        mode = far.mode & 0777;
                        carried = sizeof mode;
                }
                started = text[0] && link_start_command(s, pull, 3);
                break;
        case LINK_KIND_LOG:
                started = link_start_command(s, log, 2);
                break;
        case LINK_KIND_PUSH:
                started = text[0] && link_start_push(s, text, length - skip,
                                                     mode & 0777);
                break;
        default:
                link_refuse(s, "that is not something this machine offers");
                return;
        }

        if (!started)
        {
                link_refuse(s, s->kind == LINK_KIND_PUSH
                                       ? "that file cannot be written there"
                                       : "that could not be started");
                return;
        }

        (void)link_post(s, LINK_KEY_ANSWER,
                        WATERLINK_FRAME_DURABLE | WATERLINK_FRAME_LAST, 'O',
                        (p8 address_to)address_of mode, carried);
        link_self.state_dirty = true;
}

/*
        One descriptor to wait on, when it is open and wants something, and
        the flag that says it is quiet: the wait clears it when the
        descriptor answers, and a turn asks only what is not quiet, so a
        wake costs the calls for what woke it and not one for everything.
*/
static fn link_watch(system_poll_descriptor address_to watch,
                     p8 address_to address_to quiet, positive address_to count,
                     bipolar handle, p16 events, p8 address_to flag)
{
        if (handle < 0 || !events)
                return;
        watch[address_to count].descriptor = (b32)handle;
        watch[address_to count].events = events;
        quiet[address_to count] = flag;
        (address_to count)++;
}

/*
        The descriptor wait the listener and the client make: system_poll_wait
        (lib.util.c), which is ppoll on Linux and poll on Darwin, with the same
        pollfd layout and events (POLLIN, POLLOUT, POLLERR, POLLHUP, POLLNVAL,
        as the SYSTEM_POLL names spell them). On Darwin a timeout is rounded up
        to the millisecond, so a timer never fires before its time, and a limit
        longer than poll takes (about 24 days) is clamped to that: the caller
        wakes and waits again.
*/

/*      One descriptor's readiness, for the paths that wait on a single one (a
        pipe's first byte, a child's end, a signal): the answer
        descriptor_wait_readable gives, a count, or -9 for a descriptor that
        is not open, which a readiness probe must not take for readable. */
static bipolar link_wait_readable(bipolar handle, timespec address_to limit)
{
        return descriptor_wait_readable(handle, limit, null);
}

/*
        Until something is ready or wake comes, whichever is first. Account
        for the work since the turn began before sleeping: a busy turn must
        not add its processing time to an already absolute timer.
*/
static fn link_wait(system_poll_descriptor address_to watch,
                    p8 address_to address_to quiet, positive count, p64 wake,
                    p64 now)
{
        timespec limit;

        if (wake > now)
                now = link_now();
        if (wake < now)
                wake = now;
        limit.tv_sec = (wake - now) / 1000000;
        limit.tv_nsec = (wake - now) % 1000000 * 1000;
        if (system_poll_wait(watch, count, address_of limit, null) <= 0 || !quiet)
                return;
        for (positive at = 0; at < count; at++)
                if (watch[at].returned && quiet[at])
                        address_to quiet[at] = false;
}

/*
        A descriptor read into frames on its key while the link has room.
        Its end is the stream's end: the client says so on input, and the
        machine waits for the command.
*/
static fn link_stream_read(struct link_session address_to s,
                           struct link_stream address_to stream)
{
        while (!stream->done && !stream->quiet)
        {
                positive room = link_room(s);

                if (!room)
                        return;
                positive want = room * LINK_CHUNK;
                bipolar got = stream->socket
                                      ? socket_receive((b32)stream->fd,
                                                       link_read_buffer + 1,
                                                       want, MSG_DONTWAIT, 0, 0)
                                      : system_read_once(stream->fd,
                                                         link_read_buffer + 1,
                                                         want);

                if (got == -EAGAIN || got == -4)
                {
                        stream->quiet = got == -EAGAIN;
                        return;
                }
                if (got <= 0)
                {
                        //      A terminal whose last holder closed it reads
                        //      as EIO: that is its end at the machine. The
                        //      client's standard input that cannot be read
                        //      is not at its end, and is not said to be: a
                        //      directory, a file that refuses, a pipe that
                        //      broke would otherwise be an empty input that
                        //      ended well.
                        stream->done = true;
                        if (got < 0 && stream->key == LINK_KEY_INPUT)
                        {
                                stream->error = got;
                                return;
                        }
                        if (stream->key == LINK_KEY_INPUT)
                                (void)link_post(s, LINK_KEY_INPUT,
                                                WATERLINK_FRAME_DURABLE |
                                                        WATERLINK_FRAME_LAST,
                                                LINK_END, null, 0);
                        return;
                }
                //      A read of more than a frame is a paste or a file and not
                //      a key: its frames go in their turn, not each alone and
                //      at once.
                for (positive at = 0; at < (positive)got; at += LINK_CHUNK)
                {
                        positive length = (positive)got - at < LINK_CHUNK
                                                  ? (positive)got - at
                                                  : LINK_CHUNK;

                        link_read_buffer[at] = LINK_DATA;
                        (void)waterlink_post(((struct link_session *)(s))->link, stream->key, (positive)got > LINK_CHUNK ? (p8)(stream->flags & ~WATERLINK_FRAME_URGENT) : stream->flags, (p8 *)(link_read_buffer + at), (p16)((positive)(length + 1)), link_self.now);
                }
                //      Less than was asked for is all there was: another
                //      read now would only say "not now".
                stream->quiet = (positive)got < want;
        }
}

/*
        The loop is one thread, and the fsync that makes a pushed file whole
        has to write all the file the disk was not yet asked to: a gigabyte
        was a second in which no datagram was answered. So the disk is asked
        to begin on what has been written every so many bytes of a transfer's
        file, and what is left to wait for at the end is the last of them.
*/
#define LINK_FLUSH_EVERY (8ull << 20)
#define SYNC_FILE_RANGE_WRITE 2

static fn link_stream_flush(struct link_session address_to s,
                            struct link_stream address_to stream, positive added)
{
        if (!s->part[0] || stream != s->writes || stream->socket)
                return;
        stream->written += added;
        if (stream->written - stream->started < LINK_FLUSH_EVERY)
                return;
#ifdef MACOS
        //      Darwin has no sync_file_range, so this is fsync: it waits for
        //      the whole file, not the range, and until the data has reached
        //      the drive, not its cache (F_FULLFSYNC would ask for that). It
        //      is also a wait where Linux only starts the write, so a stream
        //      that reaches this point stalls for the disk. The bytes are
        //      still the same; the guarantee after a power cut is weaker.
        (void)system_call_1(syscall(fsync), (positive)stream->fd);
#else
        (void)system_call_4(syscall(sync_file_range), (positive)stream->fd,
                            stream->started, stream->written - stream->started,
                            SYNC_FILE_RANGE_WRITE);
#endif
        stream->started = stream->written;
}

/*
        One frame of a key that is written to a descriptor. Data goes as far
        as the descriptor takes it; a descriptor that is full says "not now",
        and the link holds the frame, tells the sender, and hands the frame
        over again at waterlink_resume -- which is all the flow control there
        is. The end of the stream is the end of what it feeds.
*/
static p8 link_stream_take(struct link_session address_to s,
                             struct link_stream address_to stream,
                             p8 address_to payload, positive length)
{
        /*      Stream control words are directional and canonical.  In
                particular, an input stream must not accept the output-only
                exit record and let a peer impersonate the local waitid result. */
        if (!length ||
            (payload[0] == LINK_DATA ? false
             : payload[0] == LINK_END ? length != 1
             : payload[0] == LINK_EXIT ? length != 5 ||
                                                stream->key != LINK_KEY_OUTPUT
                                       : true))
                return true;

        if (payload[0] == LINK_DATA)
        {
                while (stream->fd >= 0 && stream->skip < length - 1)
                {
                        p8 address_to from = payload + 1 + stream->skip;
                        positive left = length - 1 - stream->skip;
                        bipolar wrote =
                                stream->socket
                                        ? socket_send((b32)stream->fd, from,
                                                      left,
                                                      MSG_DONTWAIT |
                                                              MSG_NOSIGNAL,
                                                      0, 0)
                                        : system_write_once(stream->fd, from,
                                                            left);

                        if (wrote == -EAGAIN || wrote == -4)
                        {
                                stream->quiet = wrote == -EAGAIN;
                                return false;
                        }
                        if (wrote <= 0)
                        {
                                //      A disk that refuses: the rest is
                                //      dropped and the push fails, rather
                                //      than reading forever. At the client
                                //      the loop says why and stops, which is
                                //      how a reader that has gone away stops
                                //      the command that fills it.
                                s->failed = true;
                                stream->error = wrote < 0 ? wrote : -EIO;
                                break;
                        }
                        stream->skip += (positive)wrote;
                }
                link_stream_flush(s, stream, length - 1);
                stream->skip = 0;
        }
        else if (payload[0] == LINK_EXIT)
        {
                memory_copy(address_of s->status, payload + 1, 4);
                s->exited = true;
                stream->done = true;
        }
        else if (payload[0] == LINK_END)
        {
                stream->done = true;
                if (s->kind == LINK_KIND_PUSH && stream->key == LINK_KEY_INPUT)
                        link_push_done(s, link_now());
                if (stream->fd != s->terminal)
                        link_stream_close(s, stream);
        }
        return true;
}

/*
        A frame arrives, at either end. The key says which stream or which
        question; the payload's first byte says what it is.
*/
static p8 link_hear(address_any context, struct waterlink_frame address_to head,
                      p8 address_to payload)
{
        struct link_session address_to s = (struct link_session address_to)context;
        positive length = head->length;

        if (!length)
                return true;
        for (positive at = 0; at < 2; at++)
                if (s->writes[at].key == head->key)
                        return link_stream_take(s, s->writes + at, payload,
                                                length);

        switch (head->key)
        {
        case LINK_KEY_REQUEST:
                if (link_self.server)
                        link_request(s, payload, length);
                break;
        case LINK_KEY_ANSWER:
                if (link_self.server)
                        break;
                if ((payload[0] == 'O' && length != 1 &&
                     !(length == 5 && s->kind == LINK_KIND_PULL)) ||
                    (payload[0] != 'O' && payload[0] != 'N'))
                        break;
                s->answered = true;
                //      The far file's mode, to the bits a file's own owner
                //      sets: nothing a far machine says makes a file here
                //      one that runs as somebody.
                if (payload[0] == 'O' && length == 5)
                {
                        memory_copy(address_of s->mode, payload + 1, 4);
                        s->mode &= 0777;
                }
                if (payload[0] == 'N')
                {
                        positive keep = length - 1 < sizeof s->refusal - 1
                                                ? length - 1
                                                : sizeof s->refusal - 1;

                        //      Printed to a terminal: printable bytes only.
                        for (positive at = 0; at < keep; at++)
                                s->refusal[at] = payload[1 + at] >= ' ' &&
                                                                 payload[1 + at] < 127
                                                         ? payload[1 + at]
                                                         : '?';
                        s->refusal[keep] = 0;
                        s->refused = true;
                }
                break;
        case LINK_KEY_SIZE:
                if (length == 5 && payload[0] == 'W' && s->terminal >= 0)
                {
                        winsize size = link_size_unpack(payload + 1);

                        system_control(s->terminal, TIOCSWINSZ,
                                       address_of size);
                }
                break;
        case LINK_KEY_SIGNAL:
                if (length == 2 && payload[0] == 'K' && s->pid > 0 &&
                    !s->exited &&
                    (payload[1] == 1 || payload[1] == 2 || payload[1] == 3 ||
                     payload[1] == 9 || payload[1] == 15))
#if defined(MACOS)
                        (void)kill((pid_t)-s->pid, (int)payload[1]);
#else
                        (void)system_call_2(syscall(kill), (positive)-s->pid,
                                            payload[1]);
#endif
                break;
        default:
                break;
        }
        return true;
}

/*
        The session's streams, moved: what may be read is read and posted,
        what the far side sent and a descriptor would not take is offered
        again, and at the machine's end the command's end is noticed and
        said, after everything it wrote.
*/
static fn link_session_streams(struct link_session address_to s, p64 now)
{
        for (positive at = 0; at < 2; at++)
                if (s->writes[at].fd >= 0 && !s->writes[at].quiet &&
                    waterlink_paused(s->link, s->writes[at].key))
                        waterlink_resume(s->link, s->writes[at].key, link_hear,
                                         s);

        for (positive at = 0; at < 2; at++)
                link_stream_read(s, s->reads + at);

        if (!link_self.server || s->kind == LINK_KIND_NONE)
                return;

        //      The command's end, once: its status, after everything it said,
        //      asked when the pidfd says it is there.
        if (s->pidfd >= 0 && !s->exited && !s->pid_quiet)
        {
                b32 status;

                if (link_command_ended(s->pidfd, s->pid, address_of status))
                        link_exited(s, status, now);
                else
                        s->pid_quiet = true;
        }

        //      A terminal can stay open behind a command that left something
        //      running in the background; a moment after the command ends, the
        //      session ends with it, as ssh's does. Only a stream that has
        //      said "not now" since the end is let go: what the link has not
        //      yet taken from a pipe is the command's output still, however
        //      long a slow path or a stopped reader keeps it waiting.
        if (s->exited && link_age(now, s->exited_at) > 300000)
                for (positive at = 0; at < 2; at++)
                        s->reads[at].done |= s->reads[at].quiet;

        if (s->exited && s->reads[0].done && s->reads[1].done && !s->exit_sent &&
            waterlink_room(s->link) >= 2)
        {
                if (s->reads[1].key)
                        (void)link_post(s, LINK_KEY_ERROR,
                                        WATERLINK_FRAME_DURABLE |
                                                WATERLINK_FRAME_LAST,
                                        LINK_END, null, 0);
                s->exit_sent = link_post(s, LINK_KEY_OUTPUT,
                                         WATERLINK_FRAME_DURABLE |
                                                 WATERLINK_FRAME_LAST,
                                         LINK_EXIT,
                                         (p8 address_to)address_of s->status,
                                         4);
        }
}

// What a session waits on: descriptors with something to read or room to take.
static fn link_session_watch(struct link_session address_to s,
                             system_poll_descriptor address_to watch,
                             p8 address_to address_to quiet,
                             positive address_to count)
{
        p8 room = link_room(s) != 0;

        for (positive at = 0; at < 2; at++)
        {
                link_watch(watch, quiet, count, s->reads[at].fd,
                           room && !s->reads[at].done ? SYSTEM_POLL_READ : 0,
                           address_of s->reads[at].quiet);
                link_watch(watch, quiet, count, s->writes[at].fd,
                           waterlink_paused(s->link, s->writes[at].key)
                                   ? SYSTEM_POLL_WRITE
                                   : 0,
                           address_of s->writes[at].quiet);
        }
        link_watch(watch, quiet, count, s->pidfd,
                   s->exited ? 0 : SYSTEM_POLL_READ, address_of s->pid_quiet);
}

/*
        The keeper's table (see link_keep_turn, which dials): the peers it
        keeps, paired by a code or linked while this listener ran, with the
        wait after a dial that failed. It lives in the listener and nowhere
        else, so a restart forgets what it linked and only a code or a session
        makes a peer kept again.
*/
#define LINK_KEEP_TRIES 4
#define LINK_KEEP_FIRST 2000000ull   // the first wait after a dial that failed
#define LINK_KEEP_LAST 300000000ull  // the longest one
#define LINK_KEEP_LOOK 2000000ull    // how often the peers are read

struct link_keep_entry
{
        p8 key[32];
        p8 used;
        p8 linked; // a session came up with it while this listener ran
        p8 place;  // which of the peer's places the next dial takes; the next after a failure
        p64 next;  // when a dial is next due, in microseconds; 0 for now
        p64 wait;  // the wait after the last dial that failed; 0 for none
};

static struct link_keep_entry link_keep[LINK_PEERS_MAX];
static struct link_session address_to link_keep_dial; // the dial in progress
static p8 link_keep_rekey;                             // ... and it is a rekey
static p64 link_keep_looked;

static struct link_keep_entry address_to link_keep_for(p8 address_to key,
                                                       p8 create)
{
        struct link_keep_entry address_to free_slot = null;

        for (positive at = 0; at < LINK_PEERS_MAX; at++)
        {
                struct link_keep_entry address_to entry = link_keep + at;

                if (entry->used && crypto_same(entry->key, key, 32))
                        return entry;
                if (!entry->used && !free_slot)
                        free_slot = entry;
        }
        if (!create || !free_slot)
                return null;
        memory_zero(free_slot, sizeof(address_to free_slot));
        memory_copy(free_slot->key, key, 32);
        free_slot->used = true;
        return free_slot;
}

/* A session with this peer came up: the peer is linked, and kept. */
static fn link_keep_linked(p8 address_to key)
{
        struct link_keep_entry address_to entry = link_keep_for(key, true);

        if (entry)
                entry->linked = true;
}

/* A greeting from a kept peer: it is dialled now, not when its wait ends. */
static fn link_keep_heard(p8 address_to key)
{
        struct link_keep_entry address_to entry = link_keep_for(key, false);

        if (entry)
                entry->next = 0;
}

/*      What a process made of the listener to read strangers' bytes drops
        before it reads one (link_mdns_child): the reader is apart because
        the listener keeps the link's key, and a fork is a copy of the
        listener, the key included. Defined at the end of this file, where
        everything it wipes has been declared. */
static fn link_secrets_forget(void);

#include "nearby.c"

// The handshake, at the machine's end -----------------------------------------

static positive link_stamp_at(p8 address_to key)
{
        positive at;

        for (at = 0; at < link_self.stamps; at++)
                if (crypto_same(link_self.stamp[at].key, key, 32))
                        break;

        return at;
}

/* Peer removal must eventually release its replay slot. The listener may
   outlive `link remove`, so compact stale markers when capacity matters
   rather than letting forgotten peers permanently deny a new identity. */
static fn link_stamps_prune(void)
{
        link_peers peers;
        positive at = 0;

        //      A file that cannot be read is not an empty list: every
        //      marker stays.
        if (link_peers_read(address_of peers) < 0)
                return;
        while (at < link_self.stamps)
        {
                if (link_peer_keyed(address_of peers, link_self.stamp[at].key))
                {
                        at++;
                        continue;
                }
                link_self.stamp[at] = link_self.stamp[--link_self.stamps];
                crypto_forget(link_self.stamp + link_self.stamps,
                              sizeof(link_stamp_entry));
                link_self.stamps_dirty = true;
        }
}

static p8 link_stamp_new(p8 address_to key, p8 address_to stamp)
{
        positive at = link_stamp_at(key);

        if (at < link_self.stamps)
                return waterlink_stamp_newer(stamp, link_self.stamp[at].stamp);
        if (link_self.stamps == LINK_PEERS_MAX)
                link_stamps_prune();
        /* Every paired peer fits in this table. Once it is full, refusing an
           unknown identity preserves every resident peer's replay marker;
           replacing slot zero would let a group member rotate invented
           static keys until an old initiation from that peer became new. */
        return link_self.stamps < LINK_PEERS_MAX;
}

static fn link_stamp_keep(p8 address_to key, p8 address_to stamp)
{
        positive at = link_stamp_at(key);

        if (at == link_self.stamps)
        {
                if (link_self.stamps == LINK_PEERS_MAX)
                        return;
                link_self.stamps++;
        }
        memory_copy(link_self.stamp[at].key, key, 32);
        memory_copy(link_self.stamp[at].stamp, stamp, WATERLINK_STAMP_BYTES);
        link_self.stamps_dirty = true;
}

static fn link_sessions_granted(p64 now);

static fn link_server_initiation(p8 address_to datagram, positive length,
                                 p8 address_to address, p16 port, p64 now)
{
        struct waterlink_noise noise;
        struct waterlink_identity address_to me = address_of link_self.me;
        p8 address_to psk = null;
        positive group = 0;
        p8 who[32];
        p8 hello[WATERLINK_HELLO_BYTES];
        p8 ephemeral[32];
        p8 answer[WATERLINK_DATAGRAM];
        p8 send[16], receive[16];
        link_peers peers;
        struct waterlink_peer address_to peer;
        struct link_session address_to s = null;
        positive of_peer = 0;
        p64 conversation;
        p32 theirs;
        p32 ours;
        bipolar admitted;

        //      To this machine's key, or a member's greeting to a group's.
        if (!waterlink_gate_passes(me, datagram, length))
        {
                while (group < link_nearby.groups.count &&
                       !waterlink_gate_passes(
                               address_of link_nearby.keys[group].identity,
                               datagram, length))
                        group++;
                if (group == link_nearby.groups.count)
                        return;
                me = address_of link_nearby.keys[group].identity;
                psk = link_nearby.keys[group].psk;
        }
        /*      The cookies' secret, made again once it is two minutes old;
                under load an initiation without a good mac2 for where it
                came from is answered with its cookie and goes no further. A
                group's greeter asks nothing again, so under load greetings
                wait for the load to pass. */
        if (!waterlink_cookie_fresh(address_of link_self.admission, now) &&
            system_random_fill(link_self.admission.secret, 32, 0) >= 0)
                link_self.admission.secret_made = now ? now : 1;
        admitted = waterlink_admit(address_of link_self.admission, datagram,
                                   address, port, now);
        if (admitted < 0 && system_random_fill(ephemeral, 24, 0) >= 0)
        {
                waterlink_cookie_reply(me, address_of link_self.admission,
                                       datagram, address, port, ephemeral,
                                       answer);
                (void)link_send_to(answer, WATERLINK_COOKIE_DATAGRAM, address,
                                   port);
        }
        if (admitted <= 0)
                goto forget;
        if (!waterlink_accept(address_of noise, me, psk, datagram, who, hello))
                goto forget;
        if (psk)
        {
                if (link_stamp_new(who, hello) &&
                    link_pair_greeted(group, who, hello, address, port, now))
                        link_stamp_keep(who, hello);
                goto forget;
        }

        link_peers_load(address_of peers);
        peer = link_peer_keyed(address_of peers, who);
        memory_copy(address_of conversation, hello + WATERLINK_STAMP_BYTES, 8);
        memory_copy(address_of theirs, hello + WATERLINK_STAMP_BYTES + 8, 4);
        /*      Proof of a key no longer paired: what it has open ends now,
                before a datagram queued behind this one can use its keys. */
        if (!peer)
        {
                link_self.granted = 0;
                link_sessions_granted(now);
                goto forget;
        }
        /* Zero is not a session index: link_index_new deliberately never
           issues it, and no carried reply can find it. Refuse it before it
           can reserve a session or consume the peer's replay stamp. */
        if (!link_stamp_new(who, hello) || !theirs)
                goto forget;

        for (positive at = 0; at < LINK_SESSIONS; at++)
        {
                struct link_session address_to look = link_self.session + at;

                if (!look->used || !crypto_same(look->peer, who, 32))
                        continue;
                of_peer++;
                if (look->conversation == conversation)
                        s = look;
        }

        /* Make the whole answer before reserving a session: an entropy
           outage or a refused DH must not leave initiations holding slots
           of the session table that are never keyed. */
        ours = link_index_new();
        if (!ours || system_random_fill(ephemeral, 32, 0) < 0 ||
            !waterlink_respond(address_of noise, ephemeral, theirs, ours,
                               answer))
                goto forget;

        if (!s)
        {
                if (of_peer < LINK_SESSIONS_A_PEER)
                        for (positive at = 0; at < LINK_SESSIONS && !s; at++)
                                if (!link_self.session[at].used)
                                        s = link_self.session + at;
                if (!s || !link_session_open(s))
                        goto forget;
                memory_copy(s->peer, who, 32);
                memory_copy(s->name, peer->name, WATERLINK_NAME_MAX);
                s->conversation = conversation;
        }

        /* Only a handshake that can actually become a session spends its
           replay stamp. An entropy outage or a full session table must not
           turn a client's retransmission into a replay and strand it until
           it creates a new initiation. */
        link_stamp_keep(who, hello);

        waterlink_split(address_of noise, false, send, receive);

        //      A session keyed again sends under the old keys until the
        //      initiator has shown it holds the new ones.
        if (s->now.live)
                link_keys_install(address_of s->next, send, receive, ours,
                                  theirs);
        else
                link_keys_install(address_of s->now, send, receive, ours,
                                  theirs);
        crypto_forget(send, sizeof send);
        crypto_forget(receive, sizeof receive);

        //      A place not heard from before is kept for the peer's fallbacks;
        //      a rekey from the place it is at is not.
        if (memory_compare(s->address, address, 16) || s->port != port)
        {
                s->moved = true;
                link_place_learn(who, address, port);
        }
        memory_copy(s->address, address, 16);
        s->port = port;
        s->heard = now;
        link_keep_linked(who);
        (void)link_send_to(answer, WATERLINK_DATAGRAM, address, port);
        link_self.state_dirty = true;
forget:
        crypto_forget(address_of noise, sizeof noise);
        crypto_forget(ephemeral, sizeof ephemeral);
}

// Receiving, at either end ----------------------------------------------------

static struct link_keys address_to link_keys_for(p32 index,
                                                 struct link_session address_to address_to found)
{
        for (positive at = 0; at < LINK_SESSIONS; at++)
        {
                struct link_session address_to s = link_self.session + at;

                if (!s->used || s->finished)
                        continue;
                address_to found = s;
                if (s->now.live && s->now.ours == index)
                        return address_of s->now;
                if (s->next.live && s->next.ours == index)
                        return address_of s->next;
                if (s->before.live && s->before.ours == index)
                        return address_of s->before;
        }
        return null;
}

static fn link_session_end(struct link_session address_to s, p8 tell)
{
        if (tell && s->now.live)
        {
                link_session_say(s, WATERLINK_KIND_CLOSE);
                link_session_say(s, WATERLINK_KIND_CLOSE);
        }
        s->finished = true;
}

/*
        One sealed datagram at either end: the index finds the session and the
        keys, the tag and the replay window decide whether it counts, and only
        then does it move the session -- to the address it came from, which is
        all roaming is, and to the keys it proved, which is how a rekey is
        confirmed.
*/
static fn link_note_seen(struct link_session address_to s, p64 wall);

/*      This machine's clock in seconds, for when a peer was last seen: read
        again once the link's own clock has moved a second, and not for every
        datagram, which it was, a system call each. */
static p64 link_wall(p64 now)
{
        if (!link_self.wall_read || link_age(now, link_self.wall_read) >= 1000000)
        {
                link_self.wall = system_clock_ns(0) / 1000000000ull;
                link_self.wall_read = now;
        }
        return link_self.wall;
}

static p8 link_carried(p8 address_to datagram, positive length,
                         p8 address_to address, p16 port, p64 now,
                         waterlink_sink sink)
{
        struct waterlink_datagram head;
        struct link_session address_to s = null;
        struct link_keys address_to keys;
        struct waterlink_part parts[WATERLINK_PARTS];
        bipolar count = 0;
        p8 moved;

        //      The header is read before the tag is, so a datagram too short
        //      to be one is refused before that read, whoever calls.
        if (length < 48 || length > WATERLINK_DATAGRAM || length % 16)
                return false;
        memory_copy(address_of head, datagram, 16);
        keys = link_keys_for(head.receiver, address_of s);
        if (!keys || !waterlink_open(address_of keys->receive, datagram,
                                     length))
                return false;

        //      A tag says who sent a datagram, not that it is one: its kind
        //      and its whole body are judged before its counter is spent or
        //      its source believed. A close carries nothing.
        if (head.kind == WATERLINK_KIND_CARRY)
                count = waterlink_judge(datagram + 16, length - 32, parts);
        else if (head.kind != WATERLINK_KIND_CLOSE ||
                 memory_span_byte(datagram + 16, 0, length - 32) != length - 32)
                return false;
        if (count < 0)
                return false;

        /*      The received counter is the peer's use of this receiving key.
                Our sending counter can remain small while a one-way peer
                exhausts the AEAD message limit, so it cannot enforce the
                receiving half's nonce budget. */
        if (waterlink_session_spent(link_age(now, keys->made), head.counter) ||
            (keys == address_of s->before &&
             link_age(now, s->now.made) > LINK_GRACE) ||
            !waterlink_replay_new(address_of keys->replay, head.counter))
                return false;

        if (keys == address_of s->next)
        {
                s->before = s->now;
                s->now = s->next;
                s->next.live = false;
        }

        moved = !link_self.heard_modem &&
                (memory_compare(s->address, address, 16) || s->port != port);
        link_self.state_dirty |= moved;
        if (!link_self.heard_modem)
        {
                if (moved)
                        link_place_learn(s->peer, address, port);
                memory_copy(s->address, address, 16);
                s->port = port;
        }
        //      A roam: the peer answers from somewhere new, and its record
        //      is told where, so the next attempt starts there.
        s->moved |= moved;
        s->heard = now;
        //      Seen, to the second and from where: once a second a session,
        //      or when it moves, and not every datagram -- a walk of every
        //      peer seen, 180 ticks a datagram with 64 of them.
        if (link_self.server && (s->noted != link_wall(now) || moved))
        {
                s->noted = link_self.wall;
                link_note_seen(s, s->noted);
                link_self.state_dirty = true;
        }

        if (head.kind == WATERLINK_KIND_CLOSE)
        {
                s->finished = true;
                return true;
        }

        waterlink_apply(s->link, datagram + 16, parts, (positive)count, now,
                        sink, s);
        return true;
}

/*
        What came, a run at a time, into link_inbound. The socket coalesces
        (UDP_GRO) datagrams from one sender that are all one size but the
        last -- what the far side sent as segments -- and says that size, so
        a run of fifty four is one read where it was fifty four. Two runs are
        asked for in one call (recvmmsg), and a call that brings back one has
        found the socket empty: a keystroke's datagram is one call, not a
        second to hear "not now". Answers how many runs, or the error.
*/
typedef struct
{
        link_message message;
        b32 length;
        b32 padding;
} link_received;

typedef struct
{
        p8 address[16];
        p16 port;
        positive length; // the run's bytes
        positive size;   // each datagram's, the last perhaps shorter
} link_run;

static p8 link_inbound[2][65536];

static bipolar link_receive(link_run address_to run)
{
        socket_address_internet6 from[2];
        link_iovec part[2];
        p64 control[2][4];
        link_received got[2];
        bipolar count;

        memory_zero(got, sizeof got);
        for (positive at = 0; at < 2; at++)
        {
                part[at] = (link_iovec){link_inbound[at], sizeof link_inbound[at]};
                got[at].message.name = from + at;
                got[at].message.name_length = sizeof from[at];
                got[at].message.parts = part + at;
                got[at].message.part_count = 1;
                got[at].message.control = control[at];
                got[at].message.control_length = sizeof control[at];
        }
#ifdef MACOS
        //      Darwin has no recvmmsg, so the two runs are two recvfroms, and
        //      the second is asked for only when the first came. UDP_GRO is
        //      Linux's, so a Darwin run is always one datagram.
        count = 0;
        for (positive at = 0; at < 2; at++)
        {
                b32 fromlen = sizeof from[at];
                bipolar bytes = socket_receive((b32)link_self.socket, link_inbound[at],
                                               sizeof link_inbound[at], MSG_DONTWAIT,
                                               address_of from[at], address_of fromlen);

                if (bytes < 0)
                {
                        if (!at)
                                count = bytes;
                        break;
                }
                got[at].length = (b32)bytes;
                got[at].message.control_length = 0;
                count = (bipolar)(at + 1);
        }
#else
        count = system_call_5(syscall(recvmmsg), (positive)link_self.socket,
                              (positive)got, 2, MSG_DONTWAIT, 0);
#endif

        for (bipolar at = 0; at < count; at++)
        {
                b32 segment = link_control_int((p8 address_to)control[at],
                                               got[at].message.control_length,
                                               sizeof control[at], SOL_UDP,
                                               UDP_GRO);

                run[at].length = got[at].length;
                run[at].size = segment > 0 && (positive)segment < run[at].length
                                       ? (positive)segment
                                       : run[at].length;

                if (from[at].family == AF_INET6)
                {
                        memory_copy(run[at].address, from[at].host, 16);
                        run[at].port = network_order_16(from[at].port);
                }
                else
                {
                        socket_address_internet address_to v4 =
                                (socket_address_internet address_to)(from + at);

                        link_address_v4(run[at].address,
                                        network_order_32(v4->host));
                        run[at].port = network_order_16(v4->port);
                }
        }
        return count;
}

// The state file, for `moonwater link` -----------------------------------------

static fn link_note_seen(struct link_session address_to s, p64 wall)
{
        link_state address_to state = address_of link_self.state;
        positive at;

        for (at = 0; at < state->seen_count; at++)
                if (crypto_same(state->seen[at].key, s->peer, 32))
                        break;
        if (at == state->seen_count)
        {
                if (state->seen_count == LINK_PEERS_MAX)
                        return;
                state->seen_count++;
                state->seen[at].changed = 0;
                memory_zero(state->seen[at].was, sizeof state->seen[at].was);
                state->seen[at].was_port = 0;
        }
        else if (memory_compare(state->seen[at].address, s->address, 16) ||
                 state->seen[at].port != s->port)
        {
                //      Heard from elsewhere than last noted: the address
                //      changed at this second, and the old one is kept.
                memory_copy(state->seen[at].was, state->seen[at].address, 16);
                state->seen[at].was_port = state->seen[at].port;
                state->seen[at].changed = wall;
        }
        memory_copy(state->seen[at].key, s->peer, 32);
        memory_copy(state->seen[at].address, s->address, 16);
        state->seen[at].port = s->port;
        state->seen[at].seen = wall;
}

//      Never more than once a fifth of a second, and only when it changed.
static fn link_state_write(p64 now)
{
        link_state address_to state = address_of link_self.state;

        if (!link_self.state_dirty ||
            link_age(now, link_self.state_written) < 200000)
                return;
        link_self.state_dirty = false;
        link_self.state_written = now;

        state->open_count = 0;
        for (positive at = 0; at < LINK_SESSIONS; at++)
        {
                struct link_session address_to s = link_self.session + at;
                link_open_entry address_to open = state->open + state->open_count;

                if (!s->used || !s->now.live)
                        continue;
                memory_copy(open->name, s->name, WATERLINK_NAME_MAX);
                open->kind = s->kind;
                memory_copy(open->address, s->address, 16);
                open->port = s->port;
                open->seconds = link_age(now, s->opened) / 1000000;
                open->rtt = s->link->smoothed;
                state->open_count++;
        }
        (void)link_file_replace(LINK_STATE_PATH, state, sizeof(address_to state),
                                false);
}

/*
        The replay markers outlive the listener. In memory only, a restart
        forgot every peer's newest stamp, and an initiation recorded off the
        wire before it was new again: it held a session slot for LINK_DEAD
        and drew an answer. So the table is kept in /root/link.stamps and
        read back when the listener starts, written once a turn in which a
        stamp moved -- after the turn's answers went, so an initiation's
        answer does not wait on it. Without fsync: about ten microseconds a
        write on the NVMe ext4 this was measured on, where rewriting the
        peers file with its fsync, the other place a marker could live, took
        600 to 720 and stalled every session with it; a stamp lost to a
        power cut is the restart case again, for that one peer.
*/
static fn link_stamps_save(void)
{
        if (!link_self.stamps_dirty)
                return;
        link_self.stamps_dirty = false;
        (void)link_file_replace(LINK_STAMPS_PATH, link_self.stamp,
                                link_self.stamps * sizeof(link_stamp_entry),
                                false);
}

/*      A stamp is the peer's own clock. One dated past a day ahead of this
        machine's -- a peer that booted with its clock in 2099, before NTP
        -- would lock it out for good once its clock was put right, where a
        restart used to forget it; so such a marker is not read back. A
        replay was recorded in the past and is still refused; a machine
        whose own clock is behind at start forgets markers, as before. */
#define LINK_STAMP_AHEAD 86400

static fn link_stamps_load(void)
{
        positive got = 0;
        p64 wall = system_clock_ns(0) / 1000000000ull + (1ull << 62) +
                   LINK_STAMP_AHEAD;

        (void)link_read_private_records(LINK_STAMPS_PATH,
                                        (p8 address_to)link_self.stamp,
                                        sizeof link_self.stamp,
                                        sizeof(link_stamp_entry),
                                        address_of got);
        link_self.stamps = 0;
        for (positive at = 0; at < got / sizeof(link_stamp_entry); at++)
        {
                p8 address_to stamp = link_self.stamp[at].stamp;
                p64 seconds = network_load_64(stamp);

                if (seconds <= wall)
                        link_self.stamp[link_self.stamps++] =
                                link_self.stamp[at];
        }
        link_self.stamps_dirty = link_self.stamps != got / sizeof(link_stamp_entry);
}


// The listener ------------------------------------------------------------------

/*      A datagram socket that does not wait and is not inherited across an
        exec. Linux says both in the type it asks for. Darwin's socket() takes
        no flags (it has no SOCK_NONBLOCK or SOCK_CLOEXEC), so there it is a
        plain socket and two fcntl calls, and a socket they fail on is closed
        and its error returned, not used blocking. */
static bipolar link_datagram_socket(b32 family)
{
#if defined(MACOS)
        bipolar handle = socket_new(family, SOCK_DGRAM, 0);

        if (handle >= 0 && (fcntl((int)handle, F_SETFD, FD_CLOEXEC) < 0 ||
                            fcntl((int)handle, F_SETFL, O_NONBLOCK) < 0))
        {
                bipolar error = -(bipolar)errno;

                socket_close((b32)handle);
                return error;
        }
        return handle;
#else
        return socket_new(family, SOCK_DGRAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
#endif
}

static bipolar link_socket_open(p16 port, p8 any)
{
        p8 anywhere[16] = {0};
        b32 zero = 0;
        b32 one = 1;
        b32 big = 4 << 20;
        socket_address_internet6 self;
        bipolar handle = link_datagram_socket(AF_INET6);

        link_self.v4 = handle < 0;
        if (link_self.v4)
                handle = link_datagram_socket(AF_INET);
        if (handle < 0)
                return handle;
        if (!link_self.v4)
                (void)socket_option_set((b32)handle, SOL_IPV6, IPV6_V6ONLY,
                                        address_of zero, sizeof zero);
        (void)socket_option_set((b32)handle, SOL_SOCKET, SO_RCVBUF,
                                address_of big, sizeof big);
        (void)socket_option_set((b32)handle, SOL_SOCKET, SO_SNDBUF,
                                address_of big, sizeof big);
        (void)socket_option_set((b32)handle, SOL_UDP, UDP_GRO, address_of one,
                                sizeof one); // link_receive reads what it makes
        if (any && socket_bind((b32)handle, address_of self,
                               link_destination(address_of self, anywhere,
                                                port)) < 0)
        {
                socket_close((b32)handle);
                return -EADDRINUSE;
        }
        return handle;
}

/*      Only a client's window size is a signal it reads (28, SIGWINCH). Anywhere
        else a resize is no reason to stop: a terminal's `link serve` or
        `link pair` was ended by it, and SIGWINCH's own default is to be
        ignored, which is what it is left to be. */
#if defined(MACOS)
/*      Darwin has no signalfd, so the signals are caught instead: a handler
        writes the number of the signal into a pipe, and the pipe's read end is
        what the wait watches, as the signalfd was. Each is a handler, not a
        blocked signal, so that a command which execs has the defaults back.
        Both ends are nonblocking: a read finds nothing, and a handler never
        waits on a full pipe. The pipe lasts until link_signals_close. */
static int link_signal_pipe_read = -1;
static volatile int link_signal_pipe_write = -1;

static void link_signal_note(int number)
{
        unsigned char byte = (unsigned char)number;

        if (link_signal_pipe_write >= 0)
                (void)write(link_signal_pipe_write, address_of byte, 1);
}

static fn link_signal_catch(int number)
{
        struct sigaction action;

        memory_zero(address_of action, sizeof action);
        action.sa_handler = link_signal_note;
        sigemptyset(address_of action.sa_mask);
        action.sa_flags = SA_RESTART;
        (void)sigaction(number, address_of action, null);
}

static bipolar link_signals_open(p8 resize)
{
        int ends[2];

        if (link_signal_pipe_read < 0)
        {
                if (pipe(ends) < 0)
                        return -1;
                (void)fcntl(ends[0], F_SETFL, O_NONBLOCK);
                (void)fcntl(ends[1], F_SETFL, O_NONBLOCK);
                (void)fcntl(ends[0], F_SETFD, FD_CLOEXEC);
                (void)fcntl(ends[1], F_SETFD, FD_CLOEXEC);
                link_signal_pipe_read = ends[0];
                link_signal_pipe_write = ends[1];
        }
        //      The same set the signalfd took on Linux: the hangup, the
        //      interrupt and the terminate, the child's end (read and dropped),
        //      and a broken pipe, which is a write that failed, not a death.
        link_signal_catch(SIGHUP);
        link_signal_catch(SIGINT);
        link_signal_catch(SIGTERM);
        link_signal_catch(SIGCHLD);
        link_signal_catch(SIGPIPE);
        if (resize)
                link_signal_catch(SIGWINCH);
        return link_signal_pipe_read;
}

static fn link_signals_close(bipolar handle)
{
        int reader = link_signal_pipe_read;
        int writer = link_signal_pipe_write;

        if (handle < 0 || reader < 0 || handle != reader)
                return;
        //      The writer goes first, so a handler that runs now writes nothing;
        //      then the dispositions go back to the defaults, and the pipe.
        link_signal_pipe_write = -1;
        link_signal_pipe_read = -1;
        (void)signal(SIGHUP, SIG_DFL);
        (void)signal(SIGINT, SIG_DFL);
        (void)signal(SIGTERM, SIG_DFL);
        (void)signal(SIGCHLD, SIG_DFL);
        (void)signal(SIGPIPE, SIG_DFL);
        (void)signal(SIGWINCH, SIG_DFL);
        system_close(reader);
        if (writer >= 0)
                system_close(writer);
}

static fn link_signals_take(bipolar handle, b32 address_to last)
{
        p8 information[128];
        bipolar got;

        //      One byte a signal, so a read of 128 is 128 of them, and it is
        //      read again while it is full.
        do
        {
                got = system_read_once(handle, information, sizeof information);
                for (positive at = 0; got > 0 && at < (positive)got; at++)
                        if (information[at] != SIGCHLD && information[at] != SIGPIPE)
                                address_to last = (b32)information[at];
        } while (got == (bipolar)sizeof information);
}
#else
static bipolar link_signals_open(p8 resize)
{
        positive blocked = 1ul << (1 - 1) | 1ul << (2 - 1) | 1ul << (15 - 1) |
                           1ul << (17 - 1) | 1ul << (13 - 1) |
                           (resize ? 1ul << (28 - 1) : 0);

        (void)system_signal_mask(0, address_of blocked, null, 8);
        return system_call_4(syscall(signalfd4), (positive)(bipolar)-1,
                             (positive)address_of blocked, 8,
                             O_CLOEXEC | O_NONBLOCK);
}

static fn link_signals_close(bipolar handle)
{
        system_close(handle);
}

static fn link_signals_take(bipolar handle, b32 address_to last)
{
        p8 information[128];

        while (system_read_once(handle, information, sizeof information) ==
               sizeof information)
        {
                p32 signal;

                memory_copy(address_of signal, information, 4);
                if (signal != 17 && signal != 13)
                        address_to last = (b32)signal;
        }
}
#endif

/*
        The loop both ends run, in three parts: a turn of every session --
        its streams, its sends, its timers, and at the machine the end of
        the ones that are over -- then a wait on everything any of them
        waits on, then every datagram that came.
*/
/*      Grants as they are now for what is open, too: once a second a session
        whose peer was forgotten, or whose command's grant was taken back, is
        ended, where it went on across every rekey. */
static fn link_sessions_granted(p64 now)
{
        link_peers peers;
        positive first = 0;

        if (link_age(now, link_self.granted) < 1000000)
                return;
        /* An idle listener has no grants to revoke. Keep the old refresh
           time so its first active session need not wait for an idle tick. */
        while (first < LINK_SESSIONS &&
               (!link_self.session[first].used ||
                link_self.session[first].finished))
                first++;
        if (first == LINK_SESSIONS)
                return;
        link_self.granted = now;
        link_peers_load(address_of peers);
        for (positive at = first; at < LINK_SESSIONS; at++)
        {
                struct link_session address_to s = link_self.session + at;
                if (!s->used || s->finished)
                        continue;
                struct waterlink_peer address_to peer =
                        link_peer_keyed(address_of peers, s->peer);
                positive need = link_grant_asked(link_kind_asks[s->kind]);

                if (!peer || (need < array_count(link_grants) &&
                              !(peer->may & link_grants[need].bit)))
                        link_session_end(s, true);
        }
}

#define LINK_MODEM_WAIT 20000 /* the wait while the modem's queue is full: the modem turn plays it at this pace */

/*
        A roam is written to the peers file, where the peer's address is kept,
        so the next attempt -- a new `moonwater link`, or the listener after a
        restart -- starts where the peer was last heard. The mark is set only
        by a datagram that passed its tag, its counter and its replay window,
        or by an initiation that was answered; the write is made here, at the
        turn's end under the peers lock, not in the receive path. A write that
        fails keeps every mark for the next turn.
*/
static fn link_peers_moved(void)
{
        link_peers peers;
        bipolar lock;
        p8 changed = false;
        p8 pending = false;

        for (positive at = 0; at < LINK_SESSIONS; at++)
                pending |= link_self.session[at].used && link_self.session[at].moved;
        if (!pending)
                return;
        lock = link_peers_lock();
        if (lock < 0)
                return;
        if (!link_peers_for_change(address_of peers))
        {
                link_peers_unlock(lock);
                return;
        }
        for (positive at = 0; at < LINK_SESSIONS; at++)
        {
                struct link_session address_to s = link_self.session + at;
                struct waterlink_peer address_to peer;

                if (!s->used || !s->moved)
                        continue;
                peer = link_peer_keyed(address_of peers, s->peer);
                if (peer && (memory_compare(peer->address, s->address, 16) ||
                             peer->port != s->port))
                {
                        memory_copy(peer->address, s->address, 16);
                        peer->port = s->port;
                        changed = true;
                }
        }
        if (changed && link_peers_save(address_of peers) < 0)
        {
                link_peers_unlock(lock);
                return;
        }
        for (positive at = 0; at < LINK_SESSIONS; at++)
                link_self.session[at].moved = false;
        link_peers_unlock(lock);
}

static p64 link_sessions_turn(p64 now)
{
        p64 wake = now + 1000000;

        link_peers_moved();
        if (link_self.server)
                link_sessions_granted(now);

        for (positive at = 0; at < LINK_SESSIONS; at++)
        {
                struct link_session address_to s = link_self.session + at;
                p64 due;

                if (!s->used)
                        continue;

                /* Revoked, closed or expired sessions have no remaining
                   authority over their descriptors. In particular, a held
                   upload end must not publish its file after revocation. */
                if (link_self.server)
                {
                        if (s->exit_sent && waterlink_idle(s->link))
                                s->finished = true;
                        if (link_age(now, s->heard) > LINK_DEAD ||
                            (s->now.live &&
                             waterlink_session_spent(link_age(now, s->now.made),
                                                     s->now.counter)))
                                s->finished = true;
                        if (s->finished)
                        {
                                if (s->now.live && s->exit_sent)
                                        link_session_end(s, true);
                                link_session_close(s);
                                continue;
                        }
                }

                if (s->now.live)
                        link_session_streams(s, now);
                if (s->now.live)
                {
                        link_session_flush(s, now);
                        if (link_age(now, s->spoke) > LINK_KEEPALIVE)
                                link_session_say(s, WATERLINK_KIND_CARRY);
                }

                due = waterlink_wake(s->link, now);
                //      With the modem's queue full, nothing this session sends
                //      can go until the queue plays: wait for it, do not spin.
                if (link_modem_carrying() && !link_modem_free() &&
                    due < now + LINK_MODEM_WAIT)
                        due = now + LINK_MODEM_WAIT;
                if (link_self.server && s->exited && !s->exit_sent &&
                    s->exited_at + 300000 >= now &&
                    s->exited_at + 300000 < due)
                        due = s->exited_at + 300000;
                if (due < wake)
                        wake = due;
        }
        return wake;
}

static positive link_sessions_watch(system_poll_descriptor address_to watch,
                                    p8 address_to address_to quiet,
                                    bipolar signals, p8 address_to signalled)
{
        positive count = 0;

        link_watch(watch, quiet, address_of count, link_self.socket,
                   SYSTEM_POLL_READ, address_of link_self.socket_quiet);
        link_watch(watch, quiet, address_of count, signals, SYSTEM_POLL_READ,
                   signalled);
        link_watch(watch, quiet, address_of count, link_nearby.socket,
                   SYSTEM_POLL_READ, null);
        for (positive at = 0; at < LINK_SESSIONS; at++)
                if (link_self.session[at].used)
                        link_session_watch(link_self.session + at, watch, quiet,
                                           address_of count);
        return count;
}

static fn link_client_answered(p8 address_to datagram, positive length);
static fn link_client_cookie(p8 address_to datagram, positive length, p64 now);

typedef struct
{
        struct waterlink_noise noise;
        p32 ours;      // an initiation waiting for its answer, or 0
        p64 initiated; // when it went
        positive attempts;
        p8 mac1[16];   // its mac1, which a cookie reply to it is sealed to
        p8 cookied;  // it carried a cookie
        p8 cookie[16]; // what the machine last handed out, under load
        p64 cookie_at; // when; 0 for never
        positive tries;   // initiations sent: they alternate between the known addresses
        positive retries; // tries made while the up link was quiet
        p64 gap;          // the wait before the next of those, doubled each time
        positive sent;      // initiations that left, for the reason a link fails
        bipolar send_error; // the last send's error, or 0
        p8 unverified;      // an answer to our initiation that did not verify
        struct link_session address_to session; // the session its keys are for
} link_client_state;

static link_client_state link_client;

static fn link_datagram(p8 address_to datagram, positive length,
                        p8 address_to address, p16 port, p64 now)
{
        struct waterlink_datagram head;

        if (length < 16)
                return;
        memory_copy(address_of head, datagram, 16);
        if (head.kind == WATERLINK_KIND_CARRY || head.kind == WATERLINK_KIND_CLOSE)
                (void)link_carried(datagram, length, address, port, now,
                                   link_hear);
        /* An answer or a cookie is for the initiation this machine has out,
           if it has one: a client run's, or the listener's keeper's. With none
           out they are ignored, as they were. */
        else if (head.kind == WATERLINK_KIND_RESPOND)
        {
                p8 pending = link_client.ours != 0;
                struct link_session address_to s = link_client.session;

                link_client_answered(datagram, length);
                //      The answer proved the keys and says where the
                //      peer is now: the session is heard and moved there.
                if (pending && !link_client.ours)
                {
                        s->moved |= memory_compare(s->address, address, 16) ||
                                    s->port != port;
                        memory_copy(s->address, address, 16);
                        s->port = port;
                        s->heard = now;
                }
        }
        else if (head.kind == WATERLINK_KIND_COOKIE)
                link_client_cookie(datagram, length, now);
        else if (link_self.server && head.kind == WATERLINK_KIND_INITIATE)
                link_server_initiation(datagram, length, address, port, now);
}

static fn link_receive_all(p64 now)
{
        for (positive turn = 0; turn < 256 && !link_self.socket_quiet;)
        {
                link_run run[2];
                bipolar count = link_receive(run);

                if (count < 2)
                        link_self.socket_quiet = true;
                for (bipolar at = 0; at < count; at++)
                {
                        p8 address_to bytes = link_inbound[at];
                        positive length = run[at].length;
                        positive size = run[at].size;

                        turn++;
                        for (positive from = 0; from < length;
                             from += size, turn++)
                                link_datagram(bytes + from,
                                              length - from < size
                                                      ? length - from
                                                      : size,
                                              run[at].address, run[at].port,
                                              now);
                }
        }
}

/*
        A datagram the modem's receiver completed (modem.c): it enters the link as one from nowhere in particular, so the session
        keeps the place it had, and the answers go out over the modem as well.
*/
static fn link_modem_heard(p8 address_to datagram, positive length, p64 now)
{
        p8 nowhere[16];

        memory_zero(nowhere, sizeof nowhere);
        link_self.heard_modem = true;
        link_datagram(datagram, length, nowhere, 0, now);
        link_self.heard_modem = false;
}

/*
        `moonwater link serve`: the listener in the foreground. `link on` and
        the machine process start exactly this, detached.
*/
static p64 link_keep_turn(p64 now);

static b32 link_serve(string_address ready_text)
{
        system_poll_descriptor watch[3 + LINK_SESSIONS * 5];
        p8 address_to quiet[3 + LINK_SESSIONS * 5];
        p8 signals_quiet = false;
        bipolar lock;
        bipolar signals;
        b32 stop = 0;
        p16 port = link_port();
        bipolar ready = -1;

        if (link_identity(address_of link_self.me, true) < 0)
                return host_refuse("%s cannot be read or made\n", LINK_KEY_PATH);

        //      A relative path in a push is where a command's is, in /root
        //      (the user's home on a Mac), and not wherever whoever started
        //      the listener was.
        (void)system_call_1(syscall(chdir), (positive)(string_address)LINK_HOME_DIR);
        lock = host_lock_file(LINK_LOCK_PATH, HOST_FILE_SETLK, null);
        if (lock < 0)
        {
                string_format(log_error, host_label "the link is already on\n");
                log_flush();
                return LINK_EXIT_BUSY;
        }

        link_self.socket = link_socket_open(port, true);
        return_if(link_self.socket < 0, host_fail("the link's port", link_self.socket));
        //      `link on` waits for this byte, or for the pipe to end, which is
        //      what a listener that could not start does. Written once, then closed.
        if (ready_text && byte_is_digit(ready_text[0]))
        {
                p8 over = false;
                string_address at = ready_text;
                p8 mark = '1';

                ready = (bipolar)string_decimal_saturated(address_of at, 1 << 20,
                                                          address_of over);
                (void)system_write_all((positive)ready, address_of mark, 1);
                system_close(ready);
        }
        link_nearby.socket = -1;
        link_self.server = true;
        link_self.gso = LINK_GSO;
        link_self.state_dirty = true;
        link_stamps_load();

        signals = link_signals_open(false);

        for (;;)
        {
                p64 now = link_self.now = link_now();
                p64 wake;
                p64 due;
                p64 modem_due;

                if (signals >= 0 && !signals_quiet)
                {
                        link_signals_take(signals, address_of stop);
                        signals_quiet = true;
                }
                if (stop)
                        break;

                wake = link_sessions_turn(now);
                {
                        p64 keep = link_keep_turn(now);

                        if (keep < wake)
                                wake = keep;
                }
                link_reap();
                link_state_write(now);
                due = link_nearby_tick(now);
                modem_due = link_modem_turn(now);
                if (modem_due < due)
                        due = modem_due;
                link_wait(watch, quiet,
                          link_sessions_watch(watch, quiet, signals,
                                              address_of signals_quiet),
                          due < wake ? due : wake, now);
                if (link_self.socket_quiet && link_nearby.socket < 0)
                        continue;
                now = link_self.now = link_now();
                link_receive_all(now);
                link_nearby_receive(now);
                link_stamps_save();
        }

        link_modem_stop();
        link_nearby_stop();
        //      Off: every session is told, and every command hung up on.
        for (positive at = 0; at < LINK_SESSIONS; at++)
                if (link_self.session[at].used)
                {
                        link_session_end(link_self.session + at, true);
                        link_session_close(link_self.session + at);
                }
        system_remove_at(AT_FDCWD, LINK_STATE_PATH, 0);
        system_close(lock);
        return 0;
}

// The client ----------------------------------------------------------------------

/*
        The client is the same loop with one session, which it keys itself:
        an initiation a second until one is answered, and again before the
        keys run out, under the same conversation so the streams go on as
        they were.
*/

/* Why the client's link failed, as the words the CLI and the UI print. The
   order is the order of the reason codes. */
static string_address link_reason_text(positive reason)
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

/* The reason a client's failure is, from what it heard: an answer that did
   not verify is a key; nothing sent with a route error is no route; anything
   else is silence. */
static positive link_client_reason(void)
{
        if (link_client.unverified)
                return LINK_REASON_BAD_KEY;
        if (!link_client.sent && link_client.send_error &&
            link_client.send_error != -EAGAIN)
                return LINK_REASON_NO_ROUTE;
        return LINK_REASON_NO_REPLY;
}

/*
        The next place to try: its address and port, and a fresh start there,
        an initiation at once. A cookie is the place's own, and is not sent
        to another, so it is forgotten with the place it came from.
*/
static fn link_client_place(struct link_session address_to s,
                            struct link_place address_to place)
{
        memory_copy(s->address, place->address, 16);
        s->port = place->port;
        link_client.attempts = 0;
        link_client.ours = 0;
        link_client.cookie_at = 0;
        link_client.cookied = false;
}

/*      The client's terminal goes raw as the line editor's does, and back:
        edit.c's pair, which the shell includes after this. */
static p8 edit_terminal_raw();
static fn edit_terminal_restore();

/*
        The peer's other known address: the one its record has, when that is
        not where the session is. A roam or a discovery writes the record, so
        it is read again rather than kept.
*/
static p8 link_client_other(struct link_session address_to s, p8 address_to into,
                            p16 address_to port)
{
        link_peers peers;
        struct waterlink_peer address_to peer;

        link_peers_load(address_of peers);
        peer = link_peer_keyed(address_of peers, s->peer);
        if (!peer || !peer->port ||
            (!memory_compare(peer->address, s->address, 16) &&
             peer->port == s->port))
                return false;
        memory_copy(into, peer->address, 16);
        address_to port = peer->port;
        return true;
}

// The initiator's half of the handshake, for a new session or a rekey.
static p8 link_client_initiate(struct link_session address_to s, p64 now)
{
        p8 datagram[WATERLINK_DATAGRAM];
        p8 hello[WATERLINK_HELLO_BYTES];
        p8 ephemeral[32];
        p64 wall = system_clock_ns(0);
        p8 target[16];
        p16 port = s->port;
        p8 sent;

        //      Every other try goes to the record's address, when it has one
        //      that is not the session's: the last address heard and the one
        //      the peer was paired at, in turn.
        memory_copy(target, s->address, 16);
        if (link_client.tries++ & 1)
                (void)link_client_other(s, target, address_of port);

        /* A failed index or entropy draw leaves no initiation.  Do not retain
           an earlier MAC, and do not read the still-uninitialized datagram in
           the failure path. */
        memory_zero(link_client.mac1, sizeof link_client.mac1);
        //      An initiation out is this session's, said where it is made: an
        //      answer or a cookie finds the session by it, and no caller can
        //      leave one out with no session behind it.
        link_client.session = s;
        link_client.ours = link_index_new();
        link_client.initiated = now;
        memory_zero(hello, sizeof hello);
        waterlink_stamp(hello, wall / 1000000000ull,
                        (p32)(wall % 1000000000ull));
        memory_copy(hello + WATERLINK_STAMP_BYTES, address_of s->conversation, 8);
        memory_copy(hello + WATERLINK_STAMP_BYTES + 8, address_of link_client.ours,
                    4);
        sent = link_client.ours &&
               system_random_fill(ephemeral, 32, 0) >= 0 &&
               waterlink_initiate(address_of link_client.noise,
                                  address_of link_self.me, s->peer, null,
                                  ephemeral, hello, datagram);
        //      Under a cookie the machine handed out, while it is good.
        link_client.cookied =
                link_client.cookie_at &&
                link_age(now, link_client.cookie_at) <
                        (p64)(WATERLINK_COOKIE_SECONDS - 5) * 1000000;
        if (sent)
        {
                if (link_client.cookied)
                        waterlink_mac2(link_client.cookie, datagram);
                memory_copy(link_client.mac1,
                            datagram + 16 + WATERLINK_INITIATE_BYTES - 16, 16);
                {
                        bipolar wrote = link_send_to(datagram, WATERLINK_DATAGRAM,
                                                     target, port);

                        sent = wrote >= 0;
                        if (sent)
                                link_client.sent++;
                        else
                                link_client.send_error = wrote;
                }
        }
        crypto_forget(ephemeral, sizeof ephemeral);
        if (!sent)
        {
                crypto_forget(address_of link_client.noise,
                              sizeof link_client.noise);
                link_client.ours = 0;
        }
        return sent;
}

static p8 link_client_answer(struct link_session address_to s,
                               struct waterlink_noise address_to noise,
                               p32 ours, p8 address_to datagram,
                               positive length)
{
        struct waterlink_datagram head;
        struct waterlink_noise candidate;
        p8 send[16], receive[16];
        p32 theirs = 0;

        if (length != WATERLINK_DATAGRAM)
                return false;
        memory_copy(address_of head, datagram, 16);
        if (head.kind != WATERLINK_KIND_RESPOND || head.receiver != ours ||
            !waterlink_gate_passes(address_of link_self.me, datagram, length))
                return false;

        /* A forged answer must not advance the live transcript and spoil the
           real answer which follows it. Commit the candidate only after every
           DH and tag has verified. */
        candidate = *noise;
        if (!waterlink_answered(address_of candidate, address_of link_self.me,
                                datagram, address_of theirs))
        {
                //      Addressed to this key and answering this initiation,
                //      yet not made by the key the peer record names.
                link_client.unverified = true;
                crypto_forget(address_of candidate, sizeof candidate);
                return false;
        }

        waterlink_split(address_of candidate, true, send, receive);
        crypto_forget(noise, sizeof(address_to noise));
        if (s->now.live)
                s->before = s->now;
        link_keys_install(address_of s->now, send, receive, ours, theirs);
        link_keep_linked(s->peer);
        crypto_forget(send, sizeof send);
        crypto_forget(receive, sizeof receive);
        return true;
}

static fn link_client_answered(p8 address_to datagram, positive length)
{
        //      Only for a session still open: one closed since its initiation
        //      left (a close from the far side, then its answer) has no keys
        //      to take, and its record is wiped.
        if (link_client.ours && link_client.session->used &&
            link_client_answer(link_client.session, address_of link_client.noise,
                               link_client.ours, datagram, length))
                link_client.ours = 0;
}

/*      A cookie reply to the initiation out: kept, and asked again under it
        at once if that initiation carried none -- only then, so a forged
        reply cannot keep the client initiating; otherwise the next attempt
        carries it. Either way it is an attempt. */
static fn link_client_cookie(p8 address_to datagram, positive length, p64 now)
{
        if (!link_client.ours || !link_client.session->used ||
            !waterlink_cookie_take(link_client.session->peer, link_client.mac1,
                                   datagram, length, link_client.cookie))
                return;
        link_client.cookie_at = now ? now : 1;
        if (!link_client.cookied)
                link_client.initiated = now > LINK_ATTEMPT ? now - LINK_ATTEMPT - 1
                                                           : 0;
}

static p64 link_rekey_after(void)
{
        string_address text = file_environment(
            (string_address) "WATERLINK_REKEY_SECONDS");
        bipolar seconds = link_decimal(text);

        //      Sooner is always safe, and is how a test sees a rekey.
        //      WATERLINK_REKEY_SECONDS is test-only: the link lane of
        //      test/differential.py sets it, and nothing on a machine does.
        if (seconds > 0 && seconds < WATERLINK_REKEY_SECONDS)
                return (p64)seconds * 1000000;
        return (p64)WATERLINK_REKEY_SECONDS * 1000000;
}

//      The transport's two halves, made in command.c: whether /root/link.transport says modem and the modem is ready, and the
//      modem's send of a file (modem.c).
static p8 link_modem_in_use(void);
static b32 modem_send_file(string_address path);

/*
        The keeper, in the listener: a link that drops is dialled again on
        its own, with no new code. It keeps the peers paired by a code and
        the peers a session came up with while this listener ran (link_keep).
        A dial is a session under a conversation of its own, with the client
        run's initiation, once a second and LINK_KEEP_TRIES at most; one not
        answered is closed, and the next dial waits, doubling from two seconds
        to five minutes. A greeting from a kept peer makes it due at once. A
        session that is up is dialled again before its keys run out, as a
        client run dials. One dial is in progress at a time, and no peer is
        dialled while a session with it is up or being made, either way; two
        machines that dial at once keep two sessions, which is harmless.
*/

/* Whether a session with this peer is up, or being made, in either direction. */
static p8 link_keep_has(p8 address_to key)
{
        for (positive at = 0; at < LINK_SESSIONS; at++)
        {
                struct link_session address_to s = link_self.session + at;

                if (s->used && crypto_same(s->peer, key, 32))
                        return true;
        }
        return false;
}

/*
        A peer's record names the place that answered, when it is not the one
        it names: the record's place goes to the peer's places, and the one
        that worked is the record's, so the next connect tries it first.
*/
static fn link_peer_moved(p8 address_to key, p8 address_to address, p16 port)
{
        link_peers peers;
        struct waterlink_peer address_to peer;
        bipolar lock = link_peers_lock();

        link_peers_load(address_of peers);
        peer = link_peer_keyed(address_of peers, key);
        if (peer && (peer->port != port ||
                     memory_compare(peer->address, address, 16)))
        {
                link_place_learn(peer->key, peer->address, peer->port);
                memory_copy(peer->address, address, 16);
                peer->port = port;
                (void)link_peers_save(address_of peers);
        }
        link_peers_unlock(lock);
}

/* The peers the keeper keeps, as the file has them now. */
static fn link_keep_look(void)
{
        link_peers peers;

        link_peers_load(address_of peers);
        for (positive at = 0; at < LINK_PEERS_MAX; at++)
        {
                struct link_keep_entry address_to entry = link_keep + at;

                if (entry->used && !link_peer_keyed(address_of peers, entry->key))
                        entry->used = false;
        }
        for (positive at = 0; at < peers.count; at++)
                if (peers.peer[at].group)
                        (void)link_keep_for(peers.peer[at].key, true);
}

/* The dial in progress has its answer: a dial that was not a rekey makes its
   peer one that is up, and the peer is dialled again when it drops -- not
   sooner than LINK_KEEP_FIRST after this, so a peer that drops at once is not
   dialled in a loop. */
static fn link_keep_made(p64 now)
{
        if (!link_keep_rekey)
        {
                struct link_keep_entry address_to entry =
                        link_keep_for(link_keep_dial->peer, false);

                if (entry)
                {
                        if (entry->place)
                                link_peer_moved(link_keep_dial->peer,
                                                link_keep_dial->address,
                                                link_keep_dial->port);
                        entry->place = 0;
                        entry->wait = 0;
                        entry->next = now + LINK_KEEP_FIRST;
                }
        }
        link_keep_dial = null;
        link_keep_rekey = false;
}

/* The dial in progress has failed: its session is closed and the peer waits
   longer, unless it was a rekey, which ends and leaves its session as it is
   until its keys run out. */
static fn link_keep_fail(p64 now)
{
        struct link_session address_to s = link_keep_dial;
        struct link_keep_entry address_to entry;

        link_keep_dial = null;
        link_client.ours = 0;
        if (link_keep_rekey)
        {
                link_keep_rekey = false;
                return;
        }
        entry = link_keep_for(s->peer, false);
        link_session_close(s);
        if (!entry)
                return;
        entry->place = (entry->place + 1) % LINK_PLACES_PER_PEER;
        entry->wait = entry->wait
                              ? (entry->wait * 2 < LINK_KEEP_LAST ? entry->wait * 2
                                                                  : LINK_KEEP_LAST)
                              : LINK_KEEP_FIRST;
        entry->next = now + entry->wait;
}

/* Starts a dial on the session s, a new one or a rekey, with its first
   initiation. False when that could not be sent. */
static p8 link_keep_begin(struct link_session address_to s, p64 now, p8 rekey)
{
        link_keep_dial = s;
        link_keep_rekey = rekey;
        link_client.attempts = 1;
        link_client.ours = 0;
        if (!rekey)
                link_client.cookie_at = 0;
        return link_client_initiate(s, now);
}

/* A dial to the first kept peer that is due and has no session: the session
   is taken, the peer's own address and name are put in it, and the first
   initiation goes at once. */
static fn link_keep_dial_new(p64 now)
{
        link_peers peers;

        for (positive at = 0; at < LINK_PEERS_MAX && !link_keep_dial; at++)
        {
                struct link_keep_entry address_to entry = link_keep + at;
                struct waterlink_peer address_to peer;
                struct link_session address_to s = null;

                if (!entry->used || entry->next > now || link_keep_has(entry->key))
                        continue;
                link_peers_load(address_of peers);
                peer = link_peer_keyed(address_of peers, entry->key);
                if (!peer)
                {
                        entry->used = false;
                        continue;
                }
                if (!peer->port || !(peer->group || entry->linked))
                {
                        entry->next = now + LINK_KEEP_LOOK;
                        continue;
                }
                for (positive slot = 0; slot < LINK_SESSIONS && !s; slot++)
                        if (!link_self.session[slot].used)
                                s = link_self.session + slot;
                if (!s || !link_session_open(s))
                {
                        entry->next = now + LINK_KEEP_LOOK;
                        continue;
                }
                {
                        struct link_place places[LINK_PLACES_PER_PEER];
                        positive count = link_places_for(peer->key, peer->address,
                                                         peer->port, places);

                        memory_copy(s->address, places[entry->place % count].address, 16);
                        s->port = places[entry->place % count].port;
                }
                memory_copy(s->peer, peer->key, 32);
                memory_copy(s->name, peer->name, WATERLINK_NAME_MAX);
                s->dialed = true;
                if (system_random_fill(address_of s->conversation, 8, 0) < 0)
                {
                        link_session_close(s);
                        entry->next = now + LINK_KEEP_LOOK;
                        continue;
                }
                if (!link_keep_begin(s, now, false))
                        link_keep_fail(now);
        }
}

/*
        The keeper's turn: the dial in progress, a rekey when a session that
        was dialled is due for one, or a new dial when a kept peer is due.
        Returns when it next wants a turn.
*/
static p64 link_keep_turn(p64 now)
{
        if (!link_keep_looked || link_age(now, link_keep_looked) >= LINK_KEEP_LOOK)
        {
                link_keep_looked = now ? now : 1;
                link_keep_look();
        }

        if (link_keep_dial)
        {
                struct link_session address_to s = link_keep_dial;

                if (!s->used)
                {
                        link_keep_dial = null;
                        link_keep_rekey = false;
                        link_client.ours = 0;
                }
                else if (!link_client.ours)
                        link_keep_made(now);
                else if (link_age(now, link_client.initiated) < LINK_ATTEMPT)
                        return link_client.initiated + LINK_ATTEMPT;
                else
                {
                        if (link_client.attempts < LINK_KEEP_TRIES)
                        {
                                link_client.attempts++;
                                if (link_client_initiate(s, now))
                                        return link_client.initiated + LINK_ATTEMPT;
                        }
                        link_keep_fail(now);
                }
        }

        for (positive at = 0; at < LINK_SESSIONS && !link_keep_dial; at++)
        {
                struct link_session address_to s = link_self.session + at;

                if (s->used && s->dialed && s->now.live &&
                    link_age(now, s->now.made) >= link_rekey_after())
                {
                        if (!link_keep_begin(s, now, true))
                                link_keep_fail(now);
                }
        }
        if (!link_keep_dial)
                link_keep_dial_new(now);
        if (link_keep_dial)
                return link_client.initiated + LINK_ATTEMPT;
        return now + LINK_KEEP_LOOK;
}

static b32 link_client_run(string_address name, p8 kind,
                           string_address address_to words, positive count)
{
        system_poll_descriptor watch[3 + 5];
        p8 address_to quiet[3 + 5];
        p8 signals_quiet = false;
        link_peers peers;
        struct waterlink_peer address_to peer;
        struct link_session address_to s = link_self.session;
        struct link_place places[LINK_PLACES_PER_PEER];
        positive place_count;
        positive tried = 0;
        p8 request[LINK_REQUEST_MAX + 64];
        positive request_length = 0;
        bipolar input = kind == LINK_KIND_PULL || kind == LINK_KIND_LOG ? -1 : 0;
        p64 rekey_after = link_rekey_after();
        p8 asked = false;
        p8 input_waiting = true;
        bipolar signals;
        b32 stopped = 0;
        b32 answer = LINK_FAILED;

        link_client.sent = 0;
        link_client.send_error = 0;
        link_client.unverified = false;

        //      Every send reads the transport once, here. The audio modem takes a push, a file over the sound card to the
        //      receiver, which writes it where it chooses, so the machine and the path are not used; it sends nothing back, so
        //      the other kinds, which wait for an answer, are refused while it is the transport.
        if (link_modem_in_use())
        {
                if (kind != LINK_KIND_PUSH)
                        return host_refuse("the transport is the audio modem, "
                                           "which only carries a push: moonwater "
                                           "link transport network makes the "
                                           "network the transport\n");
                host_say(log, host_label "the transport is the audio modem: %s "
                                          "goes over the sound card, not to %s, "
                                          "and %s is not used\n",
                         words[0], name, words[1]);
                return modem_send_file(words[0]);
        }

        if (link_identity(address_of link_self.me, false) < 0)
                return host_refuse("this machine has no link key; "
                                   "moonwater link on makes one\n");

        link_peers_load(address_of peers);
        peer = link_peer_named(address_of peers, name);
        if (!peer)
                return host_refuse("no peer is called %s\n", name);
        if (!peer->port)
                return host_refuse("%s has no address: moonwater link add "
                                   "it again with one\n",
                                   name);
        place_count = link_places_for(peer->key, peer->address, peer->port,
                                      places);

        //      The request, before anything is sent: a command that cannot be
        //      asked for is refused here.
        request[request_length++] = link_kind_asks[kind];
        if (kind == LINK_KIND_PUSH || kind == LINK_KIND_PULL)
        {
                //      words: the source, then where it goes.
                string_address far = kind == LINK_KIND_PUSH ? words[1] : words[0];
                positive length = string_length(far);

                if (!length || length > LINK_REQUEST_MAX - 8)
                        return host_refuse("%s is not a path the far side "
                                           "can take\n",
                                           far);
                if (kind == LINK_KIND_PUSH)
                {
                        file_facts facts;
                        p32 mode;
                        bipolar packed;

                        input = system_open_at(AT_FDCWD, words[0],
                                               FILE_READ | O_CLOEXEC);
                        return_if(input < 0, host_fail(words[0], input));
                        mode = file_look(input, (string_address) "",
                                         AT_EMPTY_PATH, address_of facts)
                                       ? facts.mode & 0777
                                       : 0644;
                        memory_copy(request + request_length, address_of mode, 4);
                        request_length += 4;
                        //      The file goes packed with the dictionary when it can be packed, and
                        //      raw when it cannot: the request says which, and the id of the
                        //      dictionary it was packed with, which the far side checks.
                        packed = push_pack_stage(words[0], input);
                        if (packed >= 0)
                        {
                                system_close(input);
                                input = packed;
                        }
                        request[request_length++] = packed >= 0 ? PUSH_FORMAT_DICTIONARY
                                                                : PUSH_FORMAT_RAW;
                        request[request_length++] = (p8)((PUSH_DICTIONARY_ID >> 24) & 0xff);
                        request[request_length++] = (p8)((PUSH_DICTIONARY_ID >> 16) & 0xff);
                        request[request_length++] = (p8)((PUSH_DICTIONARY_ID >> 8) & 0xff);
                        request[request_length++] = (p8)(PUSH_DICTIONARY_ID & 0xff);
                }
                else if (string_length(words[1]) >= sizeof s->whole ||
                         string_length(words[1]) + 28 > sizeof s->part)
                        return host_refuse("%s is too long a name\n", words[1]);
                memory_copy(request + request_length, far, length);
                request_length += length;
        }
        else if (kind == LINK_KIND_SHELL)
        {
                string_address term = file_environment((string_address) "TERM");

                (void)link_size_pack(request + request_length);
                request_length += 4;
                if (term && string_length(term) < 32)
                {
                        memory_copy(request + request_length, term,
                                    string_length(term));
                        request_length += string_length(term);
                }
        }
        else if (kind == LINK_KIND_RUN)
                for (positive at = 0; at < count; at++)
                {
                        positive length = string_length(words[at]);

                        if (request_length + length + 1 > LINK_REQUEST_MAX)
                                return host_refuse("the command is longer than "
                                                   "1024 bytes\n");
                        if (at)
                                request[request_length++] = ' ';
                        memory_copy(request + request_length, words[at],
                                    length);
                        request_length += length;
                }

        link_self.socket = link_socket_open(0, false);
        return_if(link_self.socket < 0, host_fail("a socket", link_self.socket));
        link_self.gso = LINK_GSO;
        //      The client greets nobody. Left at zero, the wait watched
        //      descriptor 0, standard input, as the greeting socket, and an
        //      input at its end or on a file woke it at once, every turn.
        link_nearby.socket = -1;
        return_if(!link_session_open(s), host_fail("memory", -ENOMEM));
        scope_exit(link_session_close(s));
        memory_copy(s->peer, peer->key, 32);
        memory_copy(s->name, peer->name, WATERLINK_NAME_MAX);
        memory_copy(s->address, peer->address, 16);
        s->port = peer->port;
        s->kind = kind;
        s->mode = 0644;
        if (system_random_fill(address_of s->conversation, 8, 0) < 0)
                return host_fail("randomness", -EIO);

        //      Input is read once the machine has said yes to the request.
        //      Neither it nor what comes back is ever waited on.
        link_stream_own(s->reads, input, LINK_KEY_INPUT,
                        WATERLINK_FRAME_DURABLE |
                                (kind == LINK_KIND_SHELL ? WATERLINK_FRAME_URGENT
                                                         : 0),
                        FILE_READ);
        s->reads[0].done = true;
        link_stream_own(s->writes, 1, LINK_KEY_OUTPUT, 0, O_WRONLY);
        link_stream_own(s->writes + 1, 2, LINK_KEY_ERROR, 0, O_WRONLY);
#if defined(MACOS)
        scope_exit(link_stdio_restore());
#endif
        signals = link_signals_open(true);

        for (;;)
        {
                p64 now = link_self.now = link_now();
                p64 wake;

                if (signals >= 0 && !signals_quiet)
                {
                        link_signals_take(signals, address_of stopped);
                        signals_quiet = true;
                }

                if (!s->now.live)
                {
                        if (!link_client.ours ||
                            link_age(now, link_client.initiated) >= LINK_ATTEMPT)
                        {
                                p8 place[64];
                                positive budget = tried ? LINK_PLACE_ATTEMPTS
                                                        : LINK_ATTEMPTS;
                                p8 spent = link_client.attempts >= budget;

                                //      An initiation that could not be sent
                                //      spends its place at once: no route is
                                //      not a datagram lost.
                                if (!spent)
                                {
                                        link_client.attempts++;
                                        spent = !link_client_initiate(s, now);
                                }
                                if (spent && tried + 1 < place_count)
                                        link_client_place(s, places + ++tried);
                                else if (spent)
                                {
                                        link_place_text(s->address, s->port,
                                                        place);
                                        string_format(log_error,
                                                      host_label "%s did not "
                                                                 "answer at %s: "
                                                                 "%s\n",
                                                      name,
                                                      (string_address)place,
                                                      link_reason_text(
                                                              link_client_reason()));
                                        log_flush();
                                        break;
                                }
                        }
                }
                else if (!asked)
                {
                        if (tried)
                        {
                                p8 answered_at[64];

                                //      Not the place the record names: the
                                //      place that answered is kept first.
                                link_place_text(s->address, s->port, answered_at);
                                host_say(log, host_label "%s answered at %s, "
                                                         "not at the place "
                                                         "its record names\n",
                                         name, (string_address)answered_at);
                                link_peer_moved(s->peer, s->address, s->port);
                        }
                        if (kind == LINK_KIND_PULL)
                        {
                                //      Private until it is whole and its
                                //      mode is the far file's.
                                bipolar part = link_part_open(
                                        words[1], string_length(words[1]),
                                        s->part, sizeof s->part, 0600);

                                if (part < 0)
                                {
                                        answer = host_fail((string_address)s->part,
                                                           part);
                                        s->part[0] = 0;
                                        break;
                                }
                                string_copy((string_address)s->whole, words[1]);
                                link_stream_close(s, s->writes);
                                link_stream_set(s->writes, part,
                                                LINK_KEY_OUTPUT, 0);
                        }
                        (void)waterlink_post(s->link, LINK_KEY_REQUEST,
                                             WATERLINK_FRAME_DURABLE |
                                                     WATERLINK_FRAME_URGENT |
                                                     WATERLINK_FRAME_LAST,
                                             request, (p16)request_length, now);
                        if (kind == LINK_KIND_SHELL)
                                (void)edit_terminal_raw();
                        asked = true;
                }
                else if (link_age(now, s->now.made) >= rekey_after &&
                         (!link_client.ours ||
                          link_age(now, link_client.initiated) > LINK_ATTEMPT) &&
                         !link_client_initiate(s, now))
                        break;

                //      An up link that has heard nothing for LINK_QUIET is tried
                //      again, from both known addresses, a wait longer each
                //      time, until LINK_RETRIES or LINK_DEAD ends it. A heard
                //      datagram starts the count again.
                if (s->now.live && asked && link_age(now, s->heard) <= LINK_QUIET)
                        link_client.retries = 0;
                else if (s->now.live && asked && link_client.retries < LINK_RETRIES &&
                         link_age(now, s->heard) > LINK_QUIET &&
                         (!link_client.ours ||
                          link_age(now, link_client.initiated) >= link_client.gap))
                {
                        link_client.gap = LINK_ATTEMPT << link_client.retries;
                        link_client.retries++;
                        (void)link_client_initiate(s, now);
                }

                if (input_waiting && s->answered && !s->refused)
                {
                        s->reads[0].done = s->reads[0].fd < 0;
                        input_waiting = false;
                }

                //      A window's new size replaces the one not yet sent; ^C
                //      with no terminal goes to the command.
                if (stopped == 28)
                {
                        p8 packed[4];

                        stopped = 0;
                        if (link_size_pack(packed))
                                (void)link_post(s, LINK_KEY_SIZE,
                                                WATERLINK_FRAME_REPLACEABLE |
                                                        WATERLINK_FRAME_URGENT,
                                                'W', packed, 4);
                }
                else if (stopped == 2 && kind == LINK_KIND_RUN)
                {
                        p8 signal = 2;

                        stopped = 0;
                        (void)link_post(s, LINK_KEY_SIGNAL,
                                        WATERLINK_FRAME_DURABLE |
                                                WATERLINK_FRAME_URGENT,
                                        'K', address_of signal, 1);
                }
                else if (stopped)
                {
                        link_session_end(s, true);
                        answer = 128 + stopped;
                        break;
                }

                if (s->refused)
                {
                        link_session_end(s, true);
                        edit_terminal_restore();
                        string_format(log_error, host_label "%s: %s\n", name,
                                      (string_address)s->refusal);
                        log_flush();
                        break;
                }
                //      A stream that failed ends the session, and the command
                //      behind it with the hangup: a reader that went away
                //      (`link run b yes | head -c 10`) stops what fills it
                //      as it would a command run here, and an input that
                //      cannot be read is not an input that ended.
                {
                        struct link_stream address_to broken =
                                s->reads[0].error < 0    ? s->reads
                                : s->writes[0].error < 0 ? s->writes
                                : s->writes[1].error < 0 ? s->writes + 1
                                                         : null;

                        if (broken)
                        {
                                string_address what = (string_address) "standard error";

                                if (broken == s->reads)
                                        what = kind == LINK_KIND_PUSH
                                                       ? words[0]
                                                       : (string_address) "standard input";
                                else if (broken == s->writes)
                                        what = kind == LINK_KIND_PULL
                                                       ? words[1]
                                                       : (string_address) "standard output";
                                link_session_end(s, true);
                                edit_terminal_restore();
                                if (broken->error != -EPIPE)
                                        (void)host_fail(what, broken->error);
                                answer = broken == s->reads        ? LINK_FAILED
                                         : broken->error == -EPIPE ? 128 + 13
                                                                   : 1;
                                break;
                        }
                }
                if (s->writes[0].done &&
                    (kind == LINK_KIND_SHELL || kind == LINK_KIND_PUSH ||
                     s->writes[1].done))
                {
                        //      Acknowledge what ended it, then say goodbye.
                        link_session_flush(s, now);
                        link_session_end(s, true);
                        answer = s->status;
                        break;
                }
                if (s->now.live &&
                    (link_age(now, s->heard) > LINK_DEAD || s->finished))
                {
                        edit_terminal_restore();
                        string_format(log_error,
                                      s->finished
                                              ? host_label "%s closed the link\n"
                                              : host_label "the link to %s was "
                                                           "lost\n",
                                      name);
                        log_flush();
                        break;
                }

                wake = link_sessions_turn(now);
                if (link_client.ours &&
                    link_client.initiated + LINK_ATTEMPT < wake)
                        wake = link_client.initiated + LINK_ATTEMPT;
                link_wait(watch, quiet,
                          link_sessions_watch(watch, quiet, signals,
                                              address_of signals_quiet),
                          wake, now);
                if (!link_self.socket_quiet)
                        link_receive_all(link_self.now = link_now());
        }

        edit_terminal_restore();
        crypto_forget(address_of link_client.noise, sizeof link_client.noise);

        //      A pulled file is only there under its name once it is whole.
        if (kind == LINK_KIND_PULL && s->part[0])
        {
                if (!answer && !s->failed &&
                    system_call_2(syscall(fchmod), (positive)s->writes[0].fd,
                                  s->mode & ~file_umask()) >= 0 &&
                    system_call_1(syscall(fsync), (positive)s->writes[0].fd) >= 0 &&
                    link_part_publish(s->writes[0].fd, s->part, s->whole))
                        s->part[0] = 0;
                else if (!answer)
                        answer = 1;
        }
        if (rekey_after < (p64)WATERLINK_REKEY_SECONDS * 1000000)
        {
                string_format(log_error, host_label "keyed %p times\n",
                              (positive)s->keyings);
                log_flush();
        }
        return answer;
}

/*      Everything of the link a copy of the listener holds, gone: each
        session's frames unmapped, then the key, the cookies' secret, the
        sessions with their keys, the groups with theirs, the initiation out
        and the datagrams last opened where they lay. Nothing here closes a
        descriptor: the caller keeps the ones it needs and closes the rest. */
static fn link_secrets_forget(void)
{
        for (positive at = 0; at < LINK_SESSIONS; at++)
                if (link_self.session[at].used && link_self.session[at].link)
                        system_call_2(syscall(munmap),
                                      (positive)link_self.session[at].link,
                                      sizeof(struct waterlink_link));
        crypto_forget(address_of link_self, sizeof link_self);
        crypto_forget(address_of link_nearby, sizeof link_nearby);
        crypto_forget(address_of link_client, sizeof link_client);
        crypto_forget(link_inbound, sizeof link_inbound);
}

#endif // WATERLINK_SERVICE_INCLUDED
