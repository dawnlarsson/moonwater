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

#include "link.c"
#include "seal.c"
#include "handshake.c"

/*      "WL", two letters as sixteen bits: 0x574c. Unassigned, above the
        registered ranges people firewall by habit, and easy to remember from
        the name. */
#define LINK_PORT 22348

#define LINK_SWITCH_PATH "/root/link"
#define LINK_KEY_PATH "/root/link.key"
#define LINK_PEERS_PATH "/root/link.peers"
#define LINK_PEERS_NEXT "/root/link.peers.next"
#define LINK_PORT_PATH "/root/link.port"
#define LINK_LOCK_PATH HOST_STATE "/link.lock"
#define LINK_STATE_PATH HOST_STATE "/link.state"
#define LINK_STATE_NEXT HOST_STATE "/link.state.next"
#define LINK_STAMPS_PATH "/root/link.stamps"
#define LINK_STAMPS_NEXT "/root/link.stamps.next"

#define LINK_PEERS_MAX 64
#define LINK_SESSIONS 16
#define LINK_SESSIONS_A_PEER 8
#define LINK_REQUEST_MAX 1024

#define LINK_KEEPALIVE 10000000ull   // microseconds of silence, then a datagram
#define LINK_DEAD 45000000ull        // of hearing nothing, then the session is gone
#define LINK_GRACE 20000000ull       // old receive keys after a rekey
#define LINK_ATTEMPT 1000000ull      // between initiations
#define LINK_ATTEMPTS 6

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

/*      The most full datagrams one segment send carries. The run goes to the
        kernel as one UDP datagram before it is cut, and that datagram's
        length has sixteen bits: 65535 less an IPv6 header and a UDP header,
        over 1200, is fifty four, and IPv4's bound gives the same. A run of
        sixty four was refused as too long, and the refusal turned segments
        off for the life of the process, so every transfer sent one datagram
        a call once its window passed fifty four. */
#define LINK_SEGMENTS ((65535 - 40 - 8) / WATERLINK_DATAGRAM)

/*      A grant by name, and what a request needs of one: ask is the
        request byte that needs this grant. Only the grants a request asks
        for have a name here: the verbs, screen and channels bits are in the
        protocol for what comes later and nobody can be given them yet. */
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

static p64 link_now(void)
{
        return system_clock_ns(1) / 1000;
}

/*      Elapsed monotonic time, including a timestamp made after the caller's
        snapshot.  The receive loop can install keys while draining one batch;
        a later datagram in that batch must not turn the small ordering gap
        into nearly 2^64 microseconds through unsigned subtraction. */
static p64 link_age(p64 now, p64 then)
{
        return now > then ? now - then : 0;
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

static const char link_alphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/*
        A key as WireGuard writes one: 44 characters of base64, ten whole
        groups and a last one of two bytes, padded. lib.c's codec does the
        groups; the tail is one more group of the two bytes and a zero.
*/
static fn link_key_text(p8 address_to key, p8 address_to text)
{
        p8 last[3] = {key[30], key[31], 0};

        memory_encode_power2(text, key, 10, (string_address)link_alphabet, 6);
        memory_encode_power2(text + 40, last, 1, (string_address)link_alphabet,
                             6);
        text[43] = '=';
        text[44] = 0;
}

static bool link_key_parse(string_address text, p8 address_to key)
{
        static p8 values[256];
        p8 quad[4];
        p8 last[3];

        if (!values[0])
        {
                memory_fill(values, 255, sizeof values);
                for (positive at = 0; at < 64; at++)
                        values[(p8)link_alphabet[at]] = (p8)at;
        }
        if (string_length(text) != 44 || text[43] != '=' ||
            memory_decode_power2(key, text, 10, values, 6) != 10)
                return false;

        //      The last character carries two bits the key does not use; a
        //      text with them set is some other text, and decodes them into
        //      the third byte here.
        memory_copy(quad, text + 40, 3);
        quad[3] = 'A';
        if (memory_decode_power2(last, quad, 1, values, 6) != 1 || last[2])
                return false;
        key[30] = last[0];
        key[31] = last[1];
        return true;
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
    "on",    "off",  "key",  "serve", "pair", "add",  "remove", "allow",
    "deny",  "group", "leave", "push", "pull", "log",  "shell", "run",  "help",
};

static bool link_name_good(string_address name)
{
        positive length = string_length(name);

        if (!length || length >= WATERLINK_NAME_MAX)
                return false;
        for (positive word = 0; word < array_count(link_words); word++)
                if (string_equals(name, link_words[word]))
                        return false;

        for (positive at = 0; at < length; at++)
        {
                p8 c = (p8)name[at];

                if (!byte_is_alnum(c) &&
                    (!at || (c != '.' && c != '-' && c != '_')))
                        return false;
        }
        return true;
}

/* Everything waterlink keeps is records only root may read: the key, the
   peers, the groups and the listener's state. Refuse a partial, oversized,
   linked, non-root-owned or publicly accessible file rather than
   interpreting the valid-looking prefix of a replaced one. */
static bipolar link_read_private_records(string_address path,
                                         p8 address_to into, positive room,
                                         positive record,
                                         positive address_to got)
{
        file_facts facts;
        bipolar handle = system_open_at(AT_FDCWD, path,
                                        FILE_READ | O_NOFOLLOW | O_CLOEXEC);
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

// Written beside its name and renamed over it: whole or not at all.
static bipolar link_file_replace(string_address next, string_address path,
                                 address_any bytes, positive length, bool sync)
{
        bipolar failed = host_write_file(next, (p8 address_to)bytes, length,
                                         0600, sync);

        if (failed < 0)
                return failed;
        return system_rename_at(AT_FDCWD, next, AT_FDCWD, path, 0);
}

/*
        The machine's own key, made on first use.

        Made under a name nobody else could have chosen and linked into place,
        so two first uses at once cannot leave half a key, and a name planted
        at /root/link.key -- a link to somewhere, or a file somebody could read
        -- is refused rather than written through or trusted.
*/
static bipolar link_secret(p8 address_to secret, bool make)
{
        positive got = 0;
        bipolar read = link_read_private_records(LINK_KEY_PATH, secret, 32, 32,
                                                 address_of got);

        if (read == -ENOENT && make)
        {
                p8 fresh[32];
                p8 name[64] = "/root/.link.key.";
                p8 tail[8];
                positive used = string_length((string_address)name);
                bipolar made;

                if (system_random_fill(fresh, 32, 0) < 0 ||
                    system_random_fill(tail, 8, 0) < 0)
                {
                        crypto_forget(fresh, sizeof fresh);
                        return -EIO;
                }
                memory_into_hex(name + used, tail, 8);
                name[used + 16] = 0;

                made = system_open_at_mode(AT_FDCWD, name,
                                           FILE_WRITE | FILE_EXCLUSIVE |
                                                   O_NOFOLLOW | O_CLOEXEC,
                                           0600);
                if (made >= 0 &&
                    (system_write_all((positive)made, fresh, 32) != 32 ||
                     system_call_1(syscall(fsync), (positive)made) < 0))
                {
                        system_close(made);
                        system_remove_at(AT_FDCWD, name, 0);
                        made = -EIO;
                }
                crypto_forget(fresh, sizeof fresh);
                if (made < 0)
                        return made;
                system_close(made);

                //      linkat does not replace: whoever got there first wins,
                //      and both read what won.
                made = system_call_5(syscall(linkat), (positive)(bipolar)AT_FDCWD,
                                     (positive)name,
                                     (positive)(bipolar)AT_FDCWD,
                                     (positive)LINK_KEY_PATH, 0);
                system_remove_at(AT_FDCWD, name, 0);
                if (made < 0 && made != -ERROR_EXISTS)
                        return made;

                read = link_read_private_records(LINK_KEY_PATH, secret, 32, 32,
                                                 address_of got);
        }

        if (read < 0)
                return read;
        //      An empty file is no key: a key is exactly 32 bytes.
        return got == 32 ? 0 : -EPERM;
}

static bipolar link_identity(struct waterlink_identity address_to me, bool make)
{
        p8 secret[32];
        bipolar got = link_secret(secret, make);

        if (got < 0)
                return got;
        waterlink_identity_from(me, secret);
        crypto_forget(secret, sizeof secret);
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
        for (positive at = 0; at < peers->count; at++)
        {
                peers->peer[at].name[WATERLINK_NAME_MAX - 1] = 0;
                if (!link_name_good(peers->peer[at].name))
                {
                        peers->peer[at] = peers->peer[--peers->count];
                        at--;
                }
        }
        return 0;
}

static fn link_peers_load(link_peers address_to peers)
{
        (void)link_peers_read(peers);
}

//      The peers, to change them: a file that is there and cannot be read
//      is not an empty list, and saving over it would forget every peer in
//      it. Only a file never written is empty.
static bool link_peers_for_change(link_peers address_to peers)
{
        bipolar read = link_peers_read(peers);

        return read >= 0 || read == -ENOENT;
}

static bipolar link_peers_save(link_peers address_to peers)
{
        return link_file_replace(LINK_PEERS_NEXT, LINK_PEERS_PATH, peers->peer,
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

static p16 link_port(void)
{
        p8 word[16];
        bipolar port;

        host_read_text(LINK_PORT_PATH, word, sizeof word);
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

static bool link_address_mapped(p8 address_to address)
{
        static const p8 prefix[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff};

        return !memory_compare(address, prefix, 12);
}

static bool link_hex_group(string_address text, positive length,
                           p16 address_to into)
{
        positive used = 0;

        address_to into = (p16)string_digits_hexadecimal_max(text, length,
                                                             address_of used);
        return length && length <= 4 && used == length;
}

// IPv6 text, with one :: at most and no embedded IPv4.
static bool link_parse_v6(string_address text, positive length,
                          p8 address_to into)
{
        p16 group[8];
        positive count = 0;
        positive gap = 9; // the group the :: stands before; 9 is none
        positive at = 0;

        if (length >= 2 && text[0] == ':' && text[1] == ':')
        {
                gap = 0;
                at = 2;
        }
        while (at < length)
        {
                positive start = at;

                at += memory_span_without_byte(text + at, ':', length - at);
                if (count == 8 ||
                    !link_hex_group(text + start, at - start, group + count++))
                        return false;
                if (at < length)
                {
                        at++;
                        if (at < length && text[at] == ':')
                        {
                                if (gap <= 8)
                                        return false;
                                gap = count;
                                at++;
                        }
                        else if (at == length)
                                return false;
                }
        }
        if (gap > 8 ? count != 8 : count > 7)
                return false;

        memory_zero(into, 16);
        for (positive i = 0; i < count; i++)
                network_store_16(into + 2 * (i < gap ? i : i + 8 - count),
                                 group[i]);
        return true;
}

/*
        HOST, HOST:PORT, [V6] or [V6]:PORT. A name is looked up once, now, over
        IPv4: an address is a cache of where a key last was, and the next
        datagram from the peer corrects it.
*/
static bool link_parse_place(string_address text, p8 address_to address,
                             p16 address_to port)
{
        p8 host[256];
        positive length = string_length(text);
        string_address colon = null;
        bipolar numeric;

        address_to port = LINK_PORT;

        if (text[0] == '[')
        {
                string_address close = string_first_of(text, ']');

                if (!close)
                        return false;
                if (close[1] == ':')
                        colon = close + 1;
                else if (close[1])
                        return false;
                if (!link_parse_v6(text + 1, (positive)(close - text - 1),
                                   address))
                        return false;
        }
        else
        {
                string_address first = string_first_of(text, ':');

                if (first && string_first_of(first + 1, ':'))
                        return link_parse_v6(text, length, address);
                colon = first;
                length = colon ? (positive)(colon - text) : length;
                if (!length || length >= sizeof host)
                        return false;
                memory_copy(host, text, length);
                host[length] = 0;

                numeric = string_to_host((string_address)host);
                if (numeric >= 0)
                        link_address_v4(address, (p32)numeric);
                else
                {
                        p32 found = 0;

                        if (dns_resolve_any((string_address) "/etc/resolv.conf",
                                            (string_address)host,
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
        return true;
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
        memory_copy(into->host, address, 16);
}

// The lock --------------------------------------------------------------

/*
        The listener holds a write lock on /run/moonwater/link.lock for as long
        as it runs. A record lock and not flock, because the kernel will say
        who holds a record lock: `link off` asks, opens the holder as a pidfd,
        asks again, and only then signals -- so a pid that ended and was reused
        between the two is never the one signalled.
*/
typedef struct
{
        b16 type;
        b16 whence;
        b32 padding;
        bipolar start;
        bipolar length;
        b32 pid;
        b32 padding2;
} link_record_lock;

#define LINK_F_GETLK 5
#define LINK_F_SETLK 6
#define LINK_F_WRLCK 1
#define LINK_F_UNLCK 2

/*
        A lock file in /run/moonwater, root's own, asked command -- F_GETLK,
        F_SETLK or F_SETLKW -- of a write lock over all of it: the handle or
        the error, and with owner, who holds it (0 for nobody).
*/
static bipolar link_lock_file(string_address path, positive command,
                              b32 address_to owner)
{
        link_record_lock lock = {LINK_F_WRLCK, 0, 0, 0, 0, 0, 0};
        bipolar handle;
        bipolar asked;

        host_state_ready();
        handle = system_open_at_mode(AT_FDCWD, path,
                                     FILE_READ_WRITE | FILE_CREATE | O_NOFOLLOW |
                                             O_CLOEXEC,
                                     0600);
        if (handle < 0)
                return handle;
        do
                asked = system_call_3(syscall(fcntl), (positive)handle, command,
                                      (positive)address_of lock);
        while (asked == -4);
        if (asked < 0)
        {
                system_close(handle);
                return asked;
        }
        if (owner)
                address_to owner = lock.type == LINK_F_UNLCK ? 0 : lock.pid;
        return handle;
}

static bipolar link_lock_owner(void)
{
        b32 owner = 0;
        bipolar handle = link_lock_file(LINK_LOCK_PATH, LINK_F_GETLK,
                                        address_of owner);

        if (handle >= 0)
                system_close(handle);
        return owner;
}

static bipolar link_lock_take(void)
{
        return link_lock_file(LINK_LOCK_PATH, LINK_F_SETLK, null);
}

// A signal for whoever holds the lock, and never for a pid reused since.
static bool link_lock_signal(b32 signal)
{
        bipolar owner = link_lock_owner();
        bipolar handle;
        bool sent;

        if (owner <= 0)
                return false;
        handle = system_call_2(syscall(pidfd_open), (positive)owner, 0);
        if (handle < 0)
                return false;
        sent = link_lock_owner() == owner &&
               system_call_4(syscall(pidfd_send_signal), (positive)handle,
                             (positive)signal, 0, 0) >= 0;
        system_close(handle);
        return sent;
}

// Sessions ---------------------------------------------------------------

struct link_keys {
        crypto_aesgcm_key send;
        crypto_aesgcm_key receive;
        struct waterlink_replay replay;
        p64 counter;
        p64 made;
        p32 ours;
        p32 theirs;
        bool live;
};

struct link_stream {
        bipolar fd;
        positive skip; // bytes of the frame in hand already written
        p8 key;
        p8 flags;
        bool done;
        bool quiet;  // it said "not now", and no wait has heard it since
        bool socket; // asked with MSG_DONTWAIT, since it is not opened again
};

struct link_session {
        bool used;
        bool finished;
        p8 kind;
        p8 name[WATERLINK_NAME_MAX];
        p8 peer[32];
        p32 may;
        p64 conversation;
        p8 address[16];
        p16 port;
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
        bool pid_quiet; // still running when last asked
        bipolar terminal; // a shell's, both a read and a write
        bool exited;
        b32 status;
        p64 exited_at;
        bool exit_sent;
        bool failed;
        bool answered;
        bool refused;
        p8 refusal[128];
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
        p64 seen; // seconds, this machine's clock
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
        bool socket_quiet; // read to its end, and no wait has heard it since
        p64 now;           // the turn's clock, which a frame posted in it carries
        p64 wall;          // this machine's clock in seconds, read at
        p64 wall_read;     // this turn's clock
        bool server;
        bool gso;
        bool v4; // no IPv6 here: the socket is AF_INET
        struct waterlink_identity me;
        struct waterlink_admission admission;
        struct link_session session[LINK_SESSIONS];
        link_stamp_entry stamp[LINK_PEERS_MAX];
        positive stamps;
        bool stamps_dirty; // changed since /root/link.stamps was written
        link_state state;
        p64 state_written;
        bool state_dirty;
        //      A run of full datagrams for one place, sent as segments.
        p8 batch[LINK_SEGMENTS * WATERLINK_DATAGRAM];
        positive batched;
        p8 batch_address[16];
        p16 batch_port;
} link_service;

static link_service link_self;

static p32 link_index_new(void)
{
        for (positive attempt = 0; attempt < 128; attempt++)
        {
                p32 index = 0;
                bool taken = false;

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

static bool link_session_open(struct link_session address_to s)
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

static bool link_part_owned(bipolar handle, p8 address_to path)
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
        A staging file is removed, or renamed over its name, only while the
        name is still the inode the transfer holds open. A name replaced
        underneath it belongs to somebody else.
*/
static fn link_part_discard(bipolar handle, p8 address_to part)
{
        if (link_part_owned(handle, part))
                system_remove_at(AT_FDCWD, part, 0);
}

static bool link_part_publish(bipolar handle, p8 address_to part,
                              p8 address_to whole)
{
        if (!link_part_owned(handle, part))
                return false;
        if (system_rename_at(AT_FDCWD, part, AT_FDCWD, whole, 0) >= 0)
                return true;
        link_part_discard(handle, part);
        return false;
}

static fn link_session_close(struct link_session address_to s)
{
        if (s->part[0])
                link_part_discard(s->writes[0].fd, s->part);
        if (s->pidfd >= 0)
        {
                (void)system_call_4(syscall(pidfd_send_signal),
                                    (positive)s->pidfd, 1, 0, 0);
                system_close(s->pidfd);
        }
        if (s->pid > 0)
                (void)system_call_2(syscall(kill), (positive)-s->pid, 1);
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
static positive link_destination(socket_address_internet6 address_to into,
                                 p8 address_to address, p16 port)
{
        if (link_self.v4)
        {
                *(socket_address_internet address_to)into =
                        (socket_address_internet){
                            .family = AF_INET, .port = network_order_16(port),
                            .host = network_order_32(network_load_32(address + 12))};
                return sizeof(socket_address_internet);
        }
        link_socket_address(into, address, port);
        return sizeof(address_to into);
}

static bipolar link_send_to(p8 address_to bytes, positive length,
                            p8 address_to address, p16 port)
{
        socket_address_internet6 to;
        positive size = link_destination(address_of to, address, port);

        if (link_self.v4 && !link_address_mapped(address))
                return -97; // EAFNOSUPPORT: no IPv6 here
        return socket_send((b32)link_self.socket, bytes, length, MSG_NOSIGNAL,
                           address_of to, size);
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
        The int a control message carries, by its level and type, or -1:
        each cmsghdr is a length, a level and a type, padded to eight bytes,
        and nothing is read past what the kernel said it wrote.
*/
static b32 link_control_int(p8 address_to control, positive length,
                            positive room, b32 level, b32 type)
{
        b32 value = -1;

        for (positive at = 0; at + 16 <= length && at + 16 <= room;)
        {
                p64 size;
                b32 has[2];

                memory_copy(address_of size, control + at, 8);
                memory_copy(has, control + at + 8, 8);
                if (size < 16 || size > room - at)
                        break;
                if (has[0] == level && has[1] == type && size >= 20)
                        memory_copy(address_of value, control + at + 16, 4);
                at += (size + 7) & ~7ull;
        }
        return value;
}

static fn link_batch_flush(void)
{
        positive count = link_self.batched;

        link_self.batched = 0;
        if (!count)
                return;

        if (count > 1 && link_self.gso)
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
                ((b32 address_to)control)[2] = 17;      // SOL_UDP
                ((b32 address_to)control)[3] = 103;     // UDP_SEGMENT
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
                if (sent >= 0 || sent == -EAGAIN)
                        return;
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
        for (positive guard = 0; guard < 256; guard++)
        {
                p8 address_to datagram;
                struct waterlink_datagram head;
                bool alone = false;
                positive used;
                positive length;

                if (link_self.batched == LINK_SEGMENTS ||
                    (link_self.batched &&
                     (memory_compare(link_self.batch_address, s->address, 16) ||
                      link_self.batch_port != s->port)))
                        link_batch_flush();

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

                memory_copy(link_self.batch_address, s->address, 16);
                link_self.batch_port = s->port;
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

static bool link_post(struct link_session address_to s, p8 key, p8 flags,
                      p8 type, p8 address_to data, positive length)
{
        p8 payload[WATERLINK_FRAME_MAX];

        if (length > WATERLINK_FRAME_MAX - 1)
                return false;
        payload[0] = type;
        if (length)
                memory_copy(payload + 1, data, length);
        return waterlink_post(s->link, key, flags, payload, (p16)(length + 1),
                              link_self.now);
}

/*
        How much may be read now: a read is sized to the slots free, keeping
        one back so an ending always has somewhere to go, so a busy stream
        costs one system call for a run of frames and not one a frame.
*/
#define LINK_READ_FRAMES 48
#define LINK_CHUNK (WATERLINK_FRAME_MAX - 1)

static p8 link_read_buffer[LINK_READ_FRAMES * LINK_CHUNK];

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
static bool link_size_pack(p8 address_to packed)
{
        winsize size = {24, 80, 0, 0};
        bool known = system_control(0, TIOCGWINSZ, address_of size) >= 0;

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
*/
static fn link_stream_own(struct link_stream address_to stream, bipolar fd,
                          p8 key, p8 flags, positive access)
{
        p8 path[] = "/proc/self/fd/0";
        file_facts facts;
        p32 format;

        link_stream_set(stream, fd, key, flags);
        if (fd < 0 || fd > 2 ||
            !file_look(fd, (string_address)"", AT_EMPTY_PATH, address_of facts))
                return;
        format = facts.mode & MODE_FORMAT;
        path[14] = (p8)('0' + fd);
        if (format == MODE_SOCKET)
                stream->socket = true;
        else if (format == MODE_PIPE || format == MODE_CHARACTER)
        {
                bipolar own = system_open_at(AT_FDCWD, path,
                                             access | O_NONBLOCK | O_NOCTTY |
                                                     O_CLOEXEC);

                if (own >= 0)
                        stream->fd = own;
        }
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

static fn link_push_done(struct link_session address_to s, p64 now)
{
        //      The part file becomes the name only whole. Either way it is
        //      no longer this session's to remove at close.
        if (!s->failed)
        {
                s->failed = system_call_1(syscall(fsync),
                                          (positive)s->writes[0].fd) < 0 ||
                            !link_part_publish(s->writes[0].fd, s->part,
                                               s->whole);
                s->part[0] = 0;
        }
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
        bool good = length && length < 32;

        for (positive at = 0; good && at < length; at++)
        {
                p8 c = from[at];

                good = byte_is_alnum(c) || c == '-' || c == '.' || c == '+' ||
                       c == '_';
        }

        if (!good)
        {
                string_copy((string_address)into, "xterm");
                return;
        }
        memory_copy(into, from, length);
        into[length] = 0;
}

static DEAD_END fn link_child_exec(string_address address_to words,
                                    positive count, p8 address_to term)
{
        p8 term_line[48] = "TERM=";
        p8 path_line[512] = "PATH=/bin:/sbin:/usr/bin:/usr/sbin";
        string_address path = file_environment((string_address) "PATH");
        string_address environment[8];
        positive at = 0;
        positive blocked = 0;

        //      Only the three descriptors the caller installed cross into
        //      the command: not the service's sockets and state files, nor
        //      whatever a hand-started `link serve` inherited.
        (void)system_call_3(syscall(close_range), 3, ~0u, 0);
        (void)system_signal_mask(2, address_of blocked, null, 8);
        for (b32 signal = 1; signal < 32; signal++)
        {
                p64 action[4] = {0, 0, 0, 0};

                (void)system_call_4(syscall(rt_sigaction), (positive)signal,
                                    (positive)action, 0, 8);
        }

        if (path && string_length(path) < sizeof path_line - 6)
        {
                string_copy((string_address)path_line, "PATH=");
                string_copy((string_address)path_line + 5, path);
        }
        string_copy((string_address)term_line + 5, (string_address)term);

        environment[at++] = (string_address)path_line;
        environment[at++] = (string_address)term_line;
        environment[at++] = (string_address) "HOME=/root";
        environment[at++] = (string_address) "USER=root";
        environment[at++] = (string_address) "LOGNAME=root";
        environment[at++] = (string_address) "SHELL=/bin/sh";
        environment[at] = null;

        (void)system_call_1(syscall(chdir), (positive)(string_address) "/root");

        //      The machine's own binary, as whichever utility words[0] names.
        (void)shell_exec_file((string_address) "/proc/self/exe", words, count,
                              environment);
        system_call_1(syscall(exit), 127);
        __builtin_unreachable();
}

static bool link_nonblocking(bipolar handle)
{
        return system_call_3(syscall(fcntl), (positive)handle, 4,
                             O_NONBLOCK | FILE_READ_WRITE) >= 0;
}

static bool link_start_shell(struct link_session address_to s,
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
        s->pidfd = system_call_2(syscall(pidfd_open), (positive)child, 0);
        s->terminal = master;
        link_stream_set(s->reads, master, LINK_KEY_OUTPUT,
                        WATERLINK_FRAME_DURABLE);
        link_stream_set(s->writes, master, LINK_KEY_INPUT, 0);
        return true;
}

static bool link_start_command(struct link_session address_to s,
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
        s->pidfd = system_call_2(syscall(pidfd_open), (positive)child, 0);
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
        if (length + 28 > room)
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
                handle = system_open_at_mode(
                        AT_FDCWD, part,
                        FILE_WRITE | FILE_EXCLUSIVE | O_NOFOLLOW | O_CLOEXEC,
                        mode);
                if (handle >= 0 || handle != -ERROR_EXISTS)
                        return handle;
        }
        return -ERROR_EXISTS;
}

static bool link_start_push(struct link_session address_to s, p8 address_to path,
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
        //      Ends once the far side has the answer.
        s->exit_sent = true;
        s->kind = LINK_KIND_NONE;
}

/* Validate the shape before any request-specific field is read. A shell
   request always carries its four-byte window size; in particular, do not
   let a short authenticated request make link_start_shell or the mode reader
   look beyond the frame payload. */
static bool link_request_well_formed(p8 kind, p8 address_to payload,
                                     positive length)
{
        positive skip = kind == LINK_KIND_PUSH ? 5 : 1;

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
        bool started;
        p8 why[96];
        p8 text[LINK_REQUEST_MAX + 1];
        p32 mode = 0;
        positive skip;
        string_address run[] = {"sh", "-c", (string_address)text, null};
        string_address pull[] = {"cat", "--", (string_address)text, null};
        string_address log[] = {"dmesg", "--follow", null};

        if (s->kind != LINK_KIND_NONE || length < 1)
                return;

        //      Grants as they are now, not as they were at the handshake.
        link_peers_load(address_of peers);
        peer = link_peer_keyed(address_of peers, s->peer);
        if (peer)
                may = peer->may;
        s->may = may;

        for (positive at = 0; at < array_count(link_grants); at++)
                if (link_grants[at].ask &&
                    (payload[0] == link_grants[at].ask ||
                     payload[0] == link_grants[at].ask_too) &&
                    !(may & link_grants[at].bit))
                {
                        string_copy_bounded((string_address)why,
                                            link_grants[at].name, sizeof why);
                        string_append_bounded((string_address)why,
                                              " is not granted to ", sizeof why);
                        string_append_bounded((string_address)why,
                                              (string_address)s->name,
                                              sizeof why);
                        link_refuse(s, (string_address)why);
                        return;
                }

        for (positive kind = 1; kind < array_count(link_kind_asks); kind++)
                if (link_kind_asks[kind] == payload[0])
                        s->kind = (p8)kind;

        //      A command or a path is one string with no NUL in it, behind a
        //      push's mode; a terminal's request is its size and TERM.
        skip = s->kind == LINK_KIND_PUSH ? 5 : 1;
        if (!link_request_well_formed(s->kind, payload, length))
        {
                link_refuse(s, "that request is malformed");
                return;
        }
        if (s->kind != LINK_KIND_SHELL)
        {
                memory_copy(text, payload + skip, length - skip);
                text[length - skip] = 0;
        }
        if (s->kind == LINK_KIND_PUSH)
                memory_copy(address_of mode, payload + 1, 4);

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
                started = text[0] && link_start_command(s, pull, 3);
                break;
        case LINK_KIND_LOG:
                started = link_start_command(s, log, 2);
                break;
        case LINK_KIND_PUSH:
                started = text[0] && link_start_push(s, text, length - 5,
                                                     mode & 07777);
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
                        null, 0);
        link_self.state_dirty = true;
}

/*
        One descriptor to wait on, when it is open and wants something, and
        the flag that says it is quiet: the wait clears it when the
        descriptor answers, and a turn asks only what is not quiet, so a
        wake costs the calls for what woke it and not one for everything.
*/
static fn link_watch(system_poll_descriptor address_to watch,
                     bool address_to address_to quiet, positive address_to count,
                     bipolar handle, p16 events, bool address_to flag)
{
        if (handle < 0 || !events)
                return;
        watch[address_to count].descriptor = (b32)handle;
        watch[address_to count].events = events;
        quiet[address_to count] = flag;
        (address_to count)++;
}

/*
        Until something is ready or wake comes, whichever is first. The
        turn's clock stands for now: a wait shorter than the least a timer
        means is timed from the clock read again, since there the turn's own
        length would show.
*/
static fn link_wait(system_poll_descriptor address_to watch,
                    bool address_to address_to quiet, positive count, p64 wake,
                    p64 now)
{
        timespec limit;

        if (wake > now && wake - now < WATERLINK_GRANULE)
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
        while (!stream->done && !stream->quiet && link_room(s))
        {
                positive want = link_room(s) * LINK_CHUNK;
                bipolar got = stream->socket
                                      ? socket_receive((b32)stream->fd,
                                                       link_read_buffer, want,
                                                       MSG_DONTWAIT, 0, 0)
                                      : system_read_once(stream->fd,
                                                         link_read_buffer,
                                                         want);

                if (got == -EAGAIN || got == -4)
                {
                        stream->quiet = got == -EAGAIN;
                        return;
                }
                if (got <= 0)
                {
                        //      A terminal whose last holder closed it reads
                        //      as EIO: that is its end.
                        stream->done = true;
                        if (stream->key == LINK_KEY_INPUT)
                                (void)link_post(s, LINK_KEY_INPUT,
                                                WATERLINK_FRAME_DURABLE |
                                                        WATERLINK_FRAME_LAST,
                                                LINK_END, null, 0);
                        return;
                }
                for (positive at = 0; at < (positive)got; at += LINK_CHUNK)
                        (void)link_post(s, stream->key, stream->flags, LINK_DATA,
                                        link_read_buffer + at,
                                        (positive)got - at < LINK_CHUNK
                                                ? (positive)got - at
                                                : LINK_CHUNK);
                //      Less than was asked for is all there was: another
                //      read now would only say "not now".
                stream->quiet = (positive)got < want;
        }
}

/*
        One frame of a key that is written to a descriptor. Data goes as far
        as the descriptor takes it; a descriptor that is full says "not now",
        and the link holds the frame, tells the sender, and hands the frame
        over again at waterlink_resume -- which is all the flow control there
        is. The end of the stream is the end of what it feeds.
*/
static bool link_stream_take(struct link_session address_to s,
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
                                //      than reading forever.
                                s->failed = true;
                                break;
                        }
                        stream->skip += (positive)wrote;
                }
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
static bool link_hear(address_any context, struct waterlink_frame address_to head,
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
                if ((payload[0] == 'O' && length != 1) ||
                    (payload[0] != 'O' && payload[0] != 'N'))
                        break;
                s->answered = true;
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
                        (void)system_call_2(syscall(kill), (positive)-s->pid,
                                            payload[1]);
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
                p8 information[128];

                memory_zero(information, sizeof information);
                //      siginfo: code at 8, the pid at 16 (zero while it
                //      runs, under WNOHANG), the status at 24.
                if (system_call_5(syscall(waitid), 3, (positive)s->pidfd,
                                  (positive)information, 4 | 1, 0) >= 0 &&
                    ((b32 address_to)information)[4])
                {
                        b32 code = ((b32 address_to)information)[2];
                        b32 value = ((b32 address_to)information)[6];

                        link_exited(s, code == 1 ? value : 128 + value, now);
                }
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
                             bool address_to address_to quiet,
                             positive address_to count)
{
        bool room = link_room(s) != 0;

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

static bool link_stamp_new(p8 address_to key, p8 address_to stamp)
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

/*
        An authenticated initiation proves possession of peer even when its
        authorization record has just been removed.  Do not merely refuse
        that rekey and leave the old traffic keys usable: close every live
        conversation for the identity before another queued datagram can use
        them.  Unknown identities still cost only the fixed session-table
        walk and close nothing.
*/
static fn link_sessions_forget_peer(p8 address_to peer)
{
        for (positive at = 0; at < LINK_SESSIONS; at++)
                if (link_self.session[at].used &&
                    crypto_same(link_self.session[at].peer, peer, 32))
                {
                        struct link_session address_to s =
                                link_self.session + at;

                        /* Make queued carry datagrams unaddressable now;
                           link_sessions_turn performs the descriptor/process
                           teardown at the next turn. */
                        crypto_forget(address_of s->now, sizeof(s->now));
                        crypto_forget(address_of s->next, sizeof(s->next));
                        crypto_forget(address_of s->before, sizeof(s->before));
                        s->finished = true;
                }
}

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
        if (!peer)
        {
                link_sessions_forget_peer(who);
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
                s->may = peer->may;
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

        memory_copy(s->address, address, 16);
        s->port = port;
        s->heard = now;
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

                if (!s->used)
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

static fn link_session_end(struct link_session address_to s, bool tell)
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

static bool link_carried(p8 address_to datagram, positive length,
                         p8 address_to address, p16 port, p64 now,
                         waterlink_sink sink)
{
        struct waterlink_datagram head;
        struct link_session address_to s = null;
        struct link_keys address_to keys;
        struct waterlink_part parts[WATERLINK_PARTS];
        bipolar count = 0;
        bool moved;

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
        if (waterlink_session_spent(link_age(now, keys->made), head.counter))
                return false;
        if (keys == address_of s->before &&
            link_age(now, s->now.made) > LINK_GRACE)
                return false;
        if (!waterlink_replay_new(address_of keys->replay, head.counter))
                return false;

        if (keys == address_of s->next)
        {
                s->before = s->now;
                s->now = s->next;
                s->next.live = false;
        }

        moved = memory_compare(s->address, address, 16) || s->port != port;
        link_self.state_dirty |= moved;
        memory_copy(s->address, address, 16);
        s->port = port;
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
        count = system_call_5(syscall(recvmmsg), (positive)link_self.socket,
                              (positive)got, 2, MSG_DONTWAIT, 0);

        for (bipolar at = 0; at < count; at++)
        {
                b32 segment = link_control_int((p8 address_to)control[at],
                                               got[at].message.control_length,
                                               sizeof control[at], 17,
                                               104); // SOL_UDP, UDP_GRO

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
        (void)link_file_replace(LINK_STATE_NEXT, LINK_STATE_PATH, state,
                                sizeof(address_to state), false);
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
        (void)link_file_replace(LINK_STAMPS_NEXT, LINK_STAMPS_PATH,
                                link_self.stamp,
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

static bipolar link_socket_open(p16 port, bool any)
{
        p8 anywhere[16] = {0};
        b32 zero = 0;
        b32 one = 1;
        b32 big = 4 << 20;
        socket_address_internet6 self;
        bipolar handle = socket_new(AF_INET6,
                                    SOCK_DGRAM | SOCK_CLOEXEC | SOCK_NONBLOCK,
                                    0);

        link_self.v4 = handle < 0;
        if (link_self.v4)
                handle = socket_new(AF_INET,
                                    SOCK_DGRAM | SOCK_CLOEXEC | SOCK_NONBLOCK,
                                    0);
        if (handle < 0)
                return handle;
        if (!link_self.v4)
                (void)socket_option_set((b32)handle, 41, 26, address_of zero,
                                        sizeof zero); // IPV6_V6ONLY off
        (void)socket_option_set((b32)handle, SOL_SOCKET, SO_RCVBUF,
                                address_of big, sizeof big);
        (void)socket_option_set((b32)handle, SOL_SOCKET, SO_SNDBUF,
                                address_of big, sizeof big);
        (void)socket_option_set((b32)handle, 17, 104, address_of one,
                                sizeof one); // SOL_UDP, UDP_GRO: link_receive
        if (any && socket_bind((b32)handle, address_of self,
                               link_destination(address_of self, anywhere,
                                                port)) < 0)
        {
                socket_close((b32)handle);
                return -EADDRINUSE;
        }
        return handle;
}

static bipolar link_signals_open(void)
{
        positive blocked = 1ul << (1 - 1) | 1ul << (2 - 1) | 1ul << (15 - 1) |
                           1ul << (17 - 1) | 1ul << (13 - 1) | 1ul << (28 - 1);

        (void)system_signal_mask(0, address_of blocked, null, 8);
        return system_call_4(syscall(signalfd4), (positive)(bipolar)-1,
                             (positive)address_of blocked, 8,
                             O_CLOEXEC | O_NONBLOCK);
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

/*
        The loop both ends run, in three parts: a turn of every session --
        its streams, its sends, its timers, and at the machine the end of
        the ones that are over -- then a wait on everything any of them
        waits on, then every datagram that came.
*/
static p64 link_sessions_turn(p64 now)
{
        p64 wake = now + 1000000;

        for (positive at = 0; at < LINK_SESSIONS; at++)
        {
                struct link_session address_to s = link_self.session + at;
                p64 due;

                if (!s->used)
                        continue;
                if (s->now.live)
                        link_session_streams(s, now);

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
                {
                        link_session_flush(s, now);
                        if (link_age(now, s->spoke) > LINK_KEEPALIVE)
                                link_session_say(s, WATERLINK_KIND_CARRY);
                }

                due = waterlink_wake(s->link, now);
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
                                    bool address_to address_to quiet,
                                    bipolar signals, bool address_to signalled)
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
        else if (!link_self.server)
        {
                if (head.kind == WATERLINK_KIND_RESPOND)
                        link_client_answered(datagram, length);
                else if (head.kind == WATERLINK_KIND_COOKIE)
                        link_client_cookie(datagram, length, now);
        }
        else if (head.kind == WATERLINK_KIND_INITIATE)
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
        `moonwater link serve`: the listener in the foreground. `link on` and
        the machine process start exactly this, detached.
*/
static b32 link_serve(void)
{
        system_poll_descriptor watch[3 + LINK_SESSIONS * 5];
        bool address_to quiet[3 + LINK_SESSIONS * 5];
        bool signals_quiet = false;
        bipolar lock;
        bipolar signals;
        b32 stop = 0;
        p16 port = link_port();

        if (link_identity(address_of link_self.me, true) < 0)
                return host_refuse("%s cannot be read or made\n", LINK_KEY_PATH);

        lock = link_lock_take();
        if (lock < 0)
        {
                string_format(log_error, host_label "the link is already on\n");
                log_flush();
                return LINK_EXIT_BUSY;
        }

        link_self.socket = link_socket_open(port, true);
        if (link_self.socket < 0)
                return host_fail("the link's port", link_self.socket);
        link_nearby.socket = -1;
        link_self.server = true;
        //      Segment runs unless told otherwise, which is how the cost of
        //      sending one datagram at a time is measured.
        link_self.gso = !file_environment((string_address) "WATERLINK_NO_SEGMENTS");
        link_self.state_dirty = true;
        link_stamps_load();

        signals = link_signals_open();

        for (;;)
        {
                p64 now = link_self.now = link_now();
                p64 wake;
                p64 due;

                if (signals >= 0 && !signals_quiet)
                {
                        link_signals_take(signals, address_of stop);
                        signals_quiet = true;
                }
                if (stop)
                        break;

                wake = link_sessions_turn(now);
                link_state_write(now);
                due = link_nearby_tick(now);
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
typedef struct
{
        struct waterlink_noise noise;
        p32 ours;      // an initiation waiting for its answer, or 0
        p64 initiated; // when it went
        positive attempts;
        p8 mac1[16];   // its mac1, which a cookie reply to it is sealed to
        bool cookied;  // it carried a cookie
        p8 cookie[16]; // what the machine last handed out, under load
        p64 cookie_at; // when; 0 for never
} link_client_state;

static link_client_state link_client;

/*      The client's terminal goes raw as the line editor's does, and back:
        edit.c's pair, which the shell includes after this. */
static bool edit_terminal_raw();
static fn edit_terminal_restore();

// The initiator's half of the handshake, for a new session or a rekey.
static bool link_client_initiate(struct link_session address_to s, p64 now)
{
        p8 datagram[WATERLINK_DATAGRAM];
        p8 hello[WATERLINK_HELLO_BYTES];
        p8 ephemeral[32];
        p64 wall = system_clock_ns(0);
        bool sent;

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
        if (sent && link_client.cookied)
                waterlink_mac2(link_client.cookie, datagram);
        memory_copy(link_client.mac1,
                    datagram + 16 + WATERLINK_INITIATE_BYTES - 16, 16);
        sent = sent && link_send_to(datagram, WATERLINK_DATAGRAM, s->address,
                                    s->port) >= 0;
        crypto_forget(ephemeral, sizeof ephemeral);
        if (!sent)
        {
                crypto_forget(address_of link_client.noise,
                              sizeof link_client.noise);
                link_client.ours = 0;
        }
        return sent;
}

static bool link_client_answer(struct link_session address_to s,
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
                crypto_forget(address_of candidate, sizeof candidate);
                return false;
        }

        waterlink_split(address_of candidate, true, send, receive);
        crypto_forget(noise, sizeof(address_to noise));
        if (s->now.live)
                s->before = s->now;
        link_keys_install(address_of s->now, send, receive, ours, theirs);
        crypto_forget(send, sizeof send);
        crypto_forget(receive, sizeof receive);
        return true;
}

static fn link_client_answered(p8 address_to datagram, positive length)
{
        if (link_client.ours &&
            link_client_answer(link_self.session, address_of link_client.noise,
                               link_client.ours, datagram, length))
                link_client.ours = 0;
}

/*      A cookie reply to the initiation out: kept, and asked again under it
        at once if that initiation carried none -- only then, so a forged
        reply cannot keep the client initiating; otherwise the next attempt
        carries it. Either way it is an attempt. */
static fn link_client_cookie(p8 address_to datagram, positive length, p64 now)
{
        if (!link_client.ours ||
            !waterlink_cookie_take(link_self.session->peer, link_client.mac1,
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
        if (seconds > 0 && seconds < WATERLINK_REKEY_SECONDS)
                return (p64)seconds * 1000000;
        return (p64)WATERLINK_REKEY_SECONDS * 1000000;
}

static b32 link_client_run(string_address name, p8 kind,
                           string_address address_to words, positive count)
{
        system_poll_descriptor watch[3 + 5];
        bool address_to quiet[3 + 5];
        bool signals_quiet = false;
        link_peers peers;
        struct waterlink_peer address_to peer;
        struct link_session address_to s = link_self.session;
        p8 request[LINK_REQUEST_MAX + 64];
        positive request_length = 0;
        bipolar input = kind == LINK_KIND_PULL || kind == LINK_KIND_LOG ? -1 : 0;
        p64 rekey_after = link_rekey_after();
        bool asked = false;
        bool input_waiting = true;
        bipolar signals;
        b32 stopped = 0;
        b32 answer = LINK_FAILED;

        if (link_identity(address_of link_self.me, false) < 0)
                return host_refuse("this machine has no link key; "
                                   "moonwater link on makes one%s\n",
                                   "");

        link_peers_load(address_of peers);
        peer = link_peer_named(address_of peers, name);
        if (!peer)
                return host_refuse("no peer is called %s\n", name);
        if (!peer->port)
                return host_refuse("%s has no address: moonwater link add "
                                   "it again with one\n",
                                   name);

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

                        input = system_open_at(AT_FDCWD, words[0],
                                               FILE_READ | O_CLOEXEC);
                        if (input < 0)
                                return host_fail(words[0], input);
                        mode = file_look(input, (string_address) "",
                                         AT_EMPTY_PATH, address_of facts)
                                       ? facts.mode & 0777
                                       : 0644;
                        memory_copy(request + request_length, address_of mode, 4);
                        request_length += 4;
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
                                                   "%s bytes\n",
                                                   "1024");
                        if (at)
                                request[request_length++] = ' ';
                        memory_copy(request + request_length, words[at],
                                    length);
                        request_length += length;
                }

        link_self.socket = link_socket_open(0, false);
        if (link_self.socket < 0)
                return host_fail("a socket", link_self.socket);
        link_self.gso = !file_environment((string_address) "WATERLINK_NO_SEGMENTS");
        //      The client greets nobody. Left at zero, the wait watched
        //      descriptor 0, standard input, as the greeting socket, and an
        //      input at its end or on a file woke it at once, every turn.
        link_nearby.socket = -1;
        if (!link_session_open(s))
                return host_fail("memory", -ENOMEM);
        memory_copy(s->peer, peer->key, 32);
        memory_copy(s->name, peer->name, WATERLINK_NAME_MAX);
        memory_copy(s->address, peer->address, 16);
        s->port = peer->port;
        s->kind = kind;
        if (system_random_fill(address_of s->conversation, 8, 0) < 0)
        {
                link_session_close(s);
                return host_fail("randomness", -EIO);
        }

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
        signals = link_signals_open();

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

                                if (link_client.attempts++ < LINK_ATTEMPTS &&
                                    link_client_initiate(s, now))
                                        ;
                                else
                                {
                                        link_place_text(s->address, s->port,
                                                        place);
                                        string_format(log_error,
                                                      host_label "%s did not "
                                                                 "answer at %s: "
                                                                 "it is off, "
                                                                 "unreachable, or "
                                                                 "does not know "
                                                                 "this machine's "
                                                                 "key\n",
                                                      name,
                                                      (string_address)place);
                                        log_flush();
                                        break;
                                }
                        }
                }
                else if (!asked)
                {
                        if (kind == LINK_KIND_PULL)
                        {
                                bipolar part = link_part_open(
                                        words[1], string_length(words[1]),
                                        s->part, sizeof s->part, 0644);

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
                    system_call_1(syscall(fsync), (positive)s->writes[0].fd) >= 0 &&
                    link_part_publish(s->writes[0].fd, s->part, s->whole))
                        s->part[0] = 0;
                else if (!answer)
                        answer = 1;
        }
        //      What the link did, for whoever is measuring it.
        if (file_environment((string_address) "WATERLINK_STATS"))
        {
                struct waterlink_link address_to l = s->link;

                string_format(log_error,
                              "link: sent %p again %p lost %p timeouts %p "
                              "delivered %p held %p spilled %p rtt %p us "
                              "window %p\n",
                              (positive)l->sent, (positive)l->retransmitted,
                              (positive)l->lost, (positive)l->timeouts,
                              (positive)l->delivered, (positive)l->kept,
                              (positive)l->spilled, (positive)l->smoothed,
                              (positive)l->window);
                log_flush();
        }
        if (rekey_after < (p64)WATERLINK_REKEY_SECONDS * 1000000)
        {
                string_format(log_error, host_label "keyed %p times\n",
                              (positive)s->keyings);
                log_flush();
        }
        link_session_close(s);
        return answer;
}

#endif // WATERLINK_SERVICE_INCLUDED
