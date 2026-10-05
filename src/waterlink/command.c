/*
        `moonwater link`: the verbs.

                link                    who this machine is linked with and
                                        what each may do
                link pair [NAME]        a code, and wait for a machine to use it
                link NAME CODE          link to the machine called NAME
                link NAME [COMMAND...]  a terminal on NAME, or one command
                link push NAME FILE PATH   a file there, whole or not at all
                link pull NAME PATH FILE   and back
                link log NAME           follow the far kernel log
                link add NAME KEY [HOST[:PORT]]   link by key, with no code
                link remove NAME
                link allow NAME GRANT...
                link deny NAME GRANT...
                link group ...          machines on one network that link
                                        themselves
                link on | off           the listener, kept across boots

        `key` and `serve` are not in the page: the key is in the status, and
        `serve` is how the listener starts itself.

        Linking is by key and both ways, as WireGuard's is: each machine is
        told the other's key, and a machine answers only a key it was told.
        A new machine may do nothing until it is allowed something by name:
        a code gives the two that used it a terminal, a command, files and
        the log, in both directions.

        The grants: shell is a terminal, run is one command, log follows the
        kernel log, and files is push and pull of any file root has, which
        is as good as run -- a file pushed over a script that runs at boot is
        a command -- bar the link's own: its key, machines, groups, stamps,
        state, lock and the machine script are never read or written through
        it.

        The switch, the key and the machines live in /root, beside the
        wireless networks, so install carries them to the disk and wipe keeps
        them.

        Dawn Larsson - Apache-2.0 license
        github.com/dawnlarsson/moonwater
*/

#ifndef WATERLINK_COMMAND_INCLUDED
#define WATERLINK_COMMAND_INCLUDED

#include "service.c"

static fn link_grants_text(p32 may, p8 address_to text, positive room)
{
        text[0] = 0;
        for (positive at = 0; at < array_count(link_grants); at++)
                if (may & link_grants[at].bit)
                {
                        if (text[0])
                                string_append_bounded(text, " ", room);
                        string_append_bounded(text, link_grants[at].name, room);
                }
        if (!text[0])
                string_copy_bounded(text, "nothing", room);
}

// 32 bytes: "119s ago", "119m ago", or hours.
static fn link_ago(p64 then, p8 address_to text)
{
        p64 wall = system_clock_ns(0) / 1000000000ull;
        p64 gone = wall > then ? wall - then : 0;

        text[positive_into(text, (positive)(gone < 120    ? gone
                                            : gone < 7200 ? gone / 60
                                                          : gone / 3600))] = 0;
        string_append_bounded(text, gone < 120    ? "s ago"
                                    : gone < 7200 ? "m ago"
                                                  : "h ago",
                              32);
}

/*
        The page: whether it is on and where, the key to give other machines,
        every peer with what it may do and when and where it was last heard,
        and what is open now. Everything here came out of this machine's own
        files; a place is digits the listener wrote.
*/
/*
        Whether the machine script joins a namespace with a secret on the
        line: "link group NAME" followed by a word that is not "allow".
*/
static bool link_script_names_secret(string_address script,
                                     string_address namespace)
{
        positive length = string_length(namespace);

        for (string_address at = script; at && *at; at++)
        {
                string_address word;

                if (!string_has_prefix(at, "link group "))
                        continue;
                word = at + 11;
                word += string_span_of_set(word, " ");
                if (!host_starts(word, namespace) || word[length] != ' ')
                        continue;
                word += length;
                word += string_span_of_set(word, " ");
                if (*word && *word != '\n' && *word != ';' && *word != '#' &&
                    !string_has_prefix(word, "allow"))
                        return true;
        }
        return false;
}

static b32 link_status(void)
{
        struct waterlink_identity me;
        link_peers peers;
        link_groups groups;
        p32 marks[LINK_GROUPS_MAX] = {0};
        link_state state;
        positive state_length = 0;
        bipolar owner = link_lock_owner();
        p8 key[48];

        //      A listener that is off, or older than this, left no state.
        if (link_read_private_records(LINK_STATE_PATH, (p8 address_to)address_of state,
                                      sizeof state, sizeof state,
                                      address_of state_length) < 0 ||
            state_length != sizeof state)
                memory_zero(address_of state, sizeof state);

        {
                p8 word[16];

                host_read_word(LINK_SWITCH_PATH, word, sizeof word);
                string_format(
                        log, host_label "link %s%s, udp %p\n",
                        owner > 0 ? "on" : "off",
                        owner > 0 && string_equals((string_address)word, "on")
                                ? ""
                        : owner > 0 && !word[0] ? ", run by hand"
                        : owner > 0             ? ", switched off but still running"
                        : string_equals((string_address)word, "on")
                                ? ", switched on but not running"
                                : "",
                        (positive)link_port());
        }

        if (link_identity(address_of me, false) >= 0)
        {
                p8 own[WATERLINK_NAME_MAX];

                link_key_text(me.public, key);
                link_machine_name(own);
                string_format(log, "  this machine  %s  %s\n",
                              (string_address)own, (string_address)key);
                crypto_forget(address_of me, sizeof me);
        }
        else
                string_format(log, "  this machine  no key yet: moonwater link on\n");

        //      The groups: which, what members get, and whether the secret
        //      sits in the machine script, where /dev/spark shows it to any
        //      user on the machine.
        link_groups_load(address_of groups);
        {
                p8 script[16384];
                bipolar script_length = file_slurp_regular_at(
                        AT_FDCWD, HOST_MACHINE_SCRIPT, script,
                        sizeof script - 1, 0);

                script[script_length > 0 ? script_length : 0] = 0;

                for (positive g = 0; g < groups.count; g++)
                {
                        struct waterlink_group_keys keys;
                        p8 grants[96];
                        bool shown;

                        if (groups.record[g].flags & LINK_GROUP_ONCE)
                        {
                                p64 now = link_boot_seconds();
                                p64 left = groups.record[g].expires > now
                                                   ? groups.record[g].expires - now
                                                   : 0;

                                //      A code is not a group: what it linked
                                //      is not said to be in one.
                                if (!(groups.record[g].flags & LINK_GROUP_DONE))
                                        string_format(log, "  a pairing code for %s is waiting, "
                                                           "%p seconds left\n",
                                                      (string_address)groups.record[g].namespace,
                                                      (positive)left);
                                continue;
                        }
                        shown = link_script_names_secret(
                                (string_address)script,
                                groups.record[g].namespace);

                        waterlink_group_keys_from(address_of keys,
                                                  groups.record[g].key,
                                                  groups.record[g].namespace);
                        marks[g] = keys.mark;
                        crypto_forget(address_of keys, sizeof keys);
                        link_grants_text(groups.record[g].may, grants,
                                         sizeof grants);
                        string_format(log, "  group %s: machines on this network "
                                           "link themselves, and may %s\n",
                                      (string_address)groups.record[g].namespace,
                                      (string_address)grants);
                        if (shown)
                                string_format(log,
                                              "    the machine script holds "
                                              "its secret, which any user can "
                                              "read through /dev/spark: run "
                                              "moonwater link group %s SECRET "
                                              "once and keep only the name "
                                              "there\n",
                                              (string_address)groups.record[g].namespace);
                }
                crypto_forget(script, sizeof script);
        }

        link_peers_load(address_of peers);
        if (!peers.count)
                string_format(log, "  nobody linked yet: moonwater link pair here, "
                                   "and moonwater link NAME CODE there\n");

        for (positive at = 0; at < peers.count; at++)
        {
                struct waterlink_peer address_to peer = peers.peer + at;
                p8 grants[96];
                p8 place[64];
                p8 short_key[12];
                link_seen_entry address_to seen = null;

                link_key_text(peer->key, key);
                memory_copy(short_key, key, 8);
                short_key[8] = 0;
                link_grants_text(peer->may, grants, sizeof grants);

                {
                        string_address how = "";
                        p8 in_group[48];

                        for (positive g = 0; peer->group && g < groups.count; g++)
                                if (marks[g] == peer->group)
                                {
                                        string_copy((string_address)in_group,
                                                    ", in group ");
                                        string_copy((string_address)in_group + 11,
                                                    groups.record[g].namespace);
                                        how = (string_address)in_group;
                                }
                        string_format(log, "  %s  %s...  may %s%s\n",
                                      (string_address)peer->name,
                                      (string_address)short_key,
                                      (string_address)grants, how);
                }

                //      The listener's word on where it was last heard, over
                //      the address it was paired with.
                for (positive s_at = 0; s_at < state.seen_count &&
                                        s_at < LINK_PEERS_MAX;
                     s_at++)
                        if (crypto_same(state.seen[s_at].key, peer->key, 32))
                                seen = state.seen + s_at;

                if (seen)
                {
                        p8 when[32];

                        link_ago(seen->seen, when);
                        link_place_text(seen->address, seen->port, place);
                        string_format(log, "      heard %s from %s\n",
                                      (string_address)when,
                                      (string_address)place);
                }
                else if (peer->port)
                {
                        link_place_text(peer->address, peer->port, place);
                        string_format(log, "      at %s, not heard this boot\n",
                                      (string_address)place);
                }
                else
                        string_format(log, "      no address; it can reach "
                                           "this machine, not the other way\n");
        }

        for (positive at = 0, shown = 0;
             at < state.open_count && at < LINK_SESSIONS; at++)
        {
                link_open_entry address_to open = state.open + at;
                p8 place[64];

                open->name[WATERLINK_NAME_MAX - 1] = 0;
                if (!link_name_good((string_address)open->name))
                        continue;
                link_place_text(open->address, open->port, place);
                if (!shown++)
                        string_format(log, "  open\n");
                string_format(log, "    %s  %s  %ps  rtt %p.%p ms  from %s\n",
                              (string_address)open->name,
                              link_kind_names[open->kind < array_count(link_kind_names)
                                                      ? open->kind
                                                      : 0],
                              (positive)open->seconds,
                              (positive)(open->rtt / 1000),
                              (positive)(open->rtt % 1000 / 100),
                              (string_address)place);
        }

        crypto_forget(address_of groups, sizeof groups);
        string_format(log, "\n");
        host_rows_write(log, "link");
        return 0;
}

static b32 link_key_verb(void)
{
        struct waterlink_identity me;
        p8 key[48];
        bipolar got = link_identity(address_of me, true);

        if (got == -EPERM)
                return host_refuse("%s is not a private key: it must be a "
                                   "32-byte file only root can read\n",
                                   LINK_KEY_PATH);
        if (got < 0)
                return host_fail(LINK_KEY_PATH, got);

        link_key_text(me.public, key);
        crypto_forget(address_of me, sizeof me);
        string_format(log, "%s\n", (string_address)key);
        log_flush();
        return 0;
}

/*      Under the peers lock. where is the address it is reached at, looked
        up before the lock was taken -- a name is a wait of seconds on a
        resolver, and every other writer of the file waits behind it -- or
        null for none. */
static b32 link_add_locked(string_address name, string_address text,
                          struct waterlink_peer address_to where)
{
        link_peers peers;
        struct waterlink_peer peer;
        struct waterlink_peer address_to found;
        struct waterlink_peer address_to keyed;
        struct waterlink_identity me;

        if (!link_name_good(name))
                return host_refuse("%s is not a name: letters, digits, "
                                   ". - _, up to 31, and not one of the words "
                                   "link uses\n",
                                   name);

        memory_zero(address_of peer, sizeof peer);
        if (!link_key_parse(text, peer.key))
                return host_refuse("%s is not a link key: 44 characters of "
                                   "base64, as moonwater link key prints\n",
                                   text);

        {
                p8 scalar[32] = {1};
                p8 shared[32];
                bool valid = crypto_x25519(shared, scalar, peer.key);

                crypto_forget(scalar, sizeof scalar);
                crypto_forget(shared, sizeof shared);
                if (!valid)
                        return host_refuse("%s is not a usable link key\n",
                                           text);
        }

        bool own = link_identity(address_of me, false) >= 0 &&
                   crypto_same(me.public, peer.key, 32);

        crypto_forget(address_of me, sizeof me);
        if (own)
                return host_refuse("that is this machine's own key\n");

        if (where)
        {
                memory_copy(peer.address, where->address, sizeof peer.address);
                peer.port = where->port;
        }

        string_copy(peer.name, name);
        peer.may = WATERLINK_MAY_DEFAULT;

        if (!link_peers_for_change(address_of peers))
                return host_refuse("%s could not be read\n", LINK_PEERS_PATH);
        found = link_peer_named(address_of peers, name);
        keyed = link_peer_keyed(address_of peers, peer.key);
        //      The name is one machine's and the key another's: replacing the
        //      first would leave the key twice, and the listener goes by the
        //      first it finds.
        if (found && keyed && found != keyed)
                return host_refuse("that key is already linked as %s: moonwater "
                                   "link remove it first\n",
                                   (string_address)keyed->name);
        if (!found)
                found = keyed;

        if (found)
        {
                //      Added again: the grants stay with the key they were
                //      given to, and nowhere else, and so does the address
                //      when none is given.
                if (crypto_same(found->key, peer.key, 32))
                {
                        peer.may = found->may;
                        if (!where)
                        {
                                memory_copy(peer.address, found->address,
                                            sizeof peer.address);
                                peer.port = found->port;
                        }
                }
                *found = peer;
        }
        else
        {
                if (peers.count >= LINK_PEERS_MAX)
                        return host_refuse("this machine is linked with 64 "
                                           "machines already\n");
                peers.peer[peers.count++] = peer;
        }

        if (link_peers_save(address_of peers) < 0)
                return host_refuse("%s could not be written\n", LINK_PEERS_PATH);

        string_format(log, host_label "added %s; it may do nothing yet: "
                                      "moonwater link allow %s run\n",
                      name, name);
        log_flush();
        return 0;
}

static b32 link_remove_locked(string_address name)
{
        link_peers peers;
        struct waterlink_peer address_to found;

        link_peers_load(address_of peers);
        found = link_peer_named(address_of peers, name);
        if (!found)
                return host_refuse("no machine is called %s\n", name);

        *found = peers.peer[--peers.count];
        if (link_peers_save(address_of peers) < 0)
                return host_refuse("%s could not be written\n", LINK_PEERS_PATH);

        string_format(log, host_label "removed %s\n", name);
        log_flush();
        return 0;
}

static b32 link_grant_locked(string_address name, bool allow,
                             string_address address_to words, positive count)
{
        link_peers peers;
        struct waterlink_peer address_to found;
        p8 grants[96];

        link_peers_load(address_of peers);
        found = link_peer_named(address_of peers, name);
        if (!found)
                return host_refuse("no machine is called %s\n", name);

        for (positive at = 0; at < count; at++)
        {
                p32 bit = link_grant_bit(words[at]);

                if (!bit)
                        return host_refuse("%s is not a grant: shell run files "
                                           "log\n",
                                           words[at]);
                if (allow)
                        found->may |= bit;
                else
                        found->may &= ~bit;
        }

        if (link_peers_save(address_of peers) < 0)
                return host_refuse("%s could not be written\n", LINK_PEERS_PATH);

        link_grants_text(found->may, grants, sizeof grants);
        string_format(log, host_label "%s may %s\n", name,
                      (string_address)grants);
        log_flush();
        return 0;
}

/*
        `moonwater link serve` in a session of its own, which is how both
        `link on` and the machine process start the listener. From a command,
        detached from whoever asked: nothing open but /dev/null, and not this
        process's child, so a terminal closing takes nothing with it. The
        machine process keeps its own child and restarts it if it dies.
*/
static fn link_serve_start(bool detached)
{
        bipolar child = system_fork();

        if (!child)
        {
                string_address words[] = {"moonwater", "link", "serve", null};

                (void)system_call(syscall(setsid));
                if (detached)
                {
                        bipolar null_handle;

                        if (system_fork())
                                system_call_1(syscall(exit), 0);
                        null_handle = system_open_at(AT_FDCWD, "/dev/null",
                                                     FILE_READ_WRITE);
                        if (null_handle >= 0)
                                for (b32 target = 0; target < 3; target++)
                                        system_descriptor_install(null_handle,
                                                                  target);
                }
                (void)descriptors_close_except(3, descriptors_none, descriptors_none);
                (void)shell_exec_file((string_address) "/proc/self/exe", words,
                                      3, file_environment_all());
                system_call_1(syscall(exit), 127);
        }
        if (detached && child > 0)
                (void)system_call_4(syscall(wait4), (positive)child, 0, 0, 0);
}

static bool link_wait_owner(bool present)
{
        for (positive turn = 0; turn < 60; turn++)
        {
                if ((link_lock_owner() > 0) == present)
                        return true;
                host_pause(50000000);
        }
        return false;
}

static b32 link_switch(bool on, bool say)
{
        struct waterlink_identity me;
        p8 key[48];

        if (radio_write_word(LINK_SWITCH_PATH, on ? "on" : "off") < 0)
                return host_refuse("%s could not be written\n",
                                   LINK_SWITCH_PATH);

        if (!on)
        {
                (void)link_lock_signal(15);
                if (!link_wait_owner(false))
                        return host_refuse("the listener did not stop\n");
                if (say)
                {
                        string_format(log, host_label "link off; sessions closed\n");
                        log_flush();
                }
                return 0;
        }

        if (link_identity(address_of me, true) < 0)
                return host_refuse("%s cannot be read or made\n", LINK_KEY_PATH);
        link_key_text(me.public, key);
        crypto_forget(address_of me, sizeof me);

        if (link_lock_owner() <= 0)
                link_serve_start(true);
        if (!link_wait_owner(true))
                return host_refuse("the listener did not start: is udp 22348 "
                                   "taken?\n");

        link_key_text(me.public, key);
        crypto_forget(address_of me, sizeof me);
        if (say)
        {
                string_format(log, host_label "link on, udp %p; this machine is %s\n",
                              (positive)link_port(), (string_address)key);
                log_flush();
        }
        return 0;
}

static p32 link_grants_of(string_address address_to words, positive count,
                          bool address_to good)
{
        p32 may = 0;

        address_to good = true;
        for (positive at = 0; at < count; at++)
        {
                p32 bit = link_grant_bit(words[at]);

                if (!bit)
                        address_to good = false;
                may |= bit;
        }
        return may;
}

/*
        group NAMESPACE [SECRET] [allow GRANT...]

        With a secret, this machine is in the group from now on and across
        boots, install and wipe; the secret itself is never kept, only what
        PBKDF2 makes of it. Without one, the group already joined is joined
        again, or a new group gets a secret of 160 random bits, printed once
        for the other machines.
        Members get the grants named here, and none when none are. The
        link is switched on.
*/
static const char link_base32[] = "abcdefghijklmnopqrstuvwxyz234567";
#define LINK_SECRET_LEAST 8

static b32 link_group_join(string_address address_to words, positive count)
{
        string_address namespace = words[0];
        string_address secret = null;
        link_groups groups;
        struct link_group_record address_to record = null;
        p8 made[256];
        bool generated = false;
        bool granted = false;
        bool good;
        p32 may = 0;
        positive at = 1;
        p8 grants[96];
        bipolar lock;

        if (!link_name_good(namespace) ||
            string_length(namespace) >= WATERLINK_NAMESPACE_MAX)
                return host_refuse("%s is not a group name: letters, digits, "
                                   ". - _, up to 31, and not one of the words "
                                   "link uses\n",
                                   namespace);
        if (at < count && !string_equals(words[at], "allow"))
                secret = words[at++];
        if (at < count)
        {
                if (!string_equals(words[at], "allow") || at + 1 == count)
                        return host_usage();
                may = link_grants_of(words + at + 1, count - at - 1,
                                     address_of good);
                if (!good)
                        return host_refuse("a grant is one of shell run files "
                                           "log\n");
                granted = true;
        }
        /*      A secret typed here is in /proc/PID/cmdline, which every user
                reads, for as long as the command lives: through the slow
                derivation and the listener's start, seconds. It is taken
                into this function's own bytes and the argument wiped, as
                wifi add does with a password. */
        if (secret)
        {
                positive length = string_length(secret);

                if (length < sizeof made)
                        memory_copy(made, secret, length + 1);
                crypto_forget(secret, length);
                if (length >= sizeof made)
                        return host_refuse("that secret is too long\n");
                //      A group with no secret, or one a person could guess in
                //      a sitting, is a group anybody on the network is in.
                if (length < LINK_SECRET_LEAST)
                {
                        crypto_forget(made, sizeof made);
                        return host_refuse("a secret is at least 8 characters: "
                                           "leave it out and one is made\n");
                }
                secret = (string_address)made;
        }

        lock = link_peers_lock();
        if (lock < 0)
        {
                crypto_forget(made, sizeof made);
                return host_refuse("%s could not be locked\n", LINK_PEERS_LOCK);
        }
        link_groups_load(address_of groups);
        for (positive look = 0; look < groups.count; look++)
                if (string_equals(groups.record[look].namespace, namespace) &&
                    !(groups.record[look].flags & LINK_GROUP_ONCE))
                        record = groups.record + look;

        if (!secret && !record)
        {
                p8 random[20];

                if (system_random_fill(random, sizeof random, 0) < 0)
                {
                        link_peers_unlock(lock);
                        crypto_forget(random, sizeof random);
                        crypto_forget(address_of groups, sizeof groups);
                        return host_fail("randomness", -EIO);
                }
                //      160 bits as 32 characters of base32.
                memory_encode_power2(made, random, 4, (string_address)link_base32, 5);
                made[32] = 0;
                crypto_forget(random, sizeof random);
                secret = (string_address)made;
                generated = true;
        }

        if (!record)
        {
                if (groups.count >= LINK_GROUPS_MAX)
                {
                        link_peers_unlock(lock);
                        crypto_forget(made, sizeof made);
                        crypto_forget(address_of groups, sizeof groups);
                        return host_refuse("this machine is in 8 groups "
                                           "already\n");
                }
                record = groups.record + groups.count++;
                memory_zero(record, sizeof(address_to record));
                string_copy(record->namespace, namespace);
                record->may = WATERLINK_MAY_DEFAULT;
        }

        if (secret)
        {
                //      The slow part, every time. A salted hash of the secret
                //      used to sit beside the key so that a script joining at
                //      every boot could skip it, and that hash was a guessing
                //      oracle at SHA-256 speed for anyone who could read the
                //      file, which holds the slow key to check a guess
                //      against only if the guess is made slowly. A script
                //      that joins at every boot says `link join NAMESPACE`
                //      and keeps the group it has.
                waterlink_group_derive(namespace, (p8 address_to)secret,
                                       string_length(secret),
                                       WATERLINK_GROUP_ROUNDS, record->key);
                memory_zero(record->check, sizeof record->check);
        }
        if (granted)
                record->may = may;

        if (link_groups_save(address_of groups) < 0)
        {
                link_peers_unlock(lock);
                crypto_forget(made, sizeof made);
                crypto_forget(address_of groups, sizeof groups);
                return host_refuse("%s could not be written\n", LINK_GROUPS_PATH);
        }
        link_peers_unlock(lock);

        link_grants_text(record->may, grants, sizeof grants);
        string_format(log, host_label "in group %s; its members may %s here\n",
                      namespace, (string_address)grants);
        if (generated)
                string_format(log,
                              host_label "the secret is %s -- on each other "
                                         "machine: moonwater link group %s %s\n",
                              (string_address)made, namespace,
                              (string_address)made);
        else if (secret && string_length(secret) < 20)
                string_format(log, host_label "a secret this short can be "
                                              "guessed offline by anyone on "
                                              "the network; leave it out and "
                                              "one is made\n");
        log_flush();
        crypto_forget(made, sizeof made);
        crypto_forget(address_of groups, sizeof groups);
        return link_switch(true, true);
}

// group leave NAMESPACE [forget]: stop announcing it, and maybe its machines too.
static b32 link_group_leave(string_address namespace, bool forget)
{
        link_groups groups;
        struct waterlink_group_keys keys;
        bool found = false;
        bool saved;
        bipolar lock = link_peers_lock();

        if (lock < 0)
                return host_refuse("%s could not be locked\n", LINK_PEERS_LOCK);
        link_groups_load(address_of groups);
        for (positive at = 0; at < groups.count; at++)
                if (string_equals(groups.record[at].namespace, namespace) &&
                    !(groups.record[at].flags & LINK_GROUP_ONCE))
                {
                        waterlink_group_keys_from(address_of keys,
                                                  groups.record[at].key,
                                                  namespace);
                        groups.record[at] = groups.record[--groups.count];
                        found = true;
                        break;
                }
        saved = found && link_groups_save(address_of groups) >= 0;
        link_peers_unlock(lock);
        crypto_forget(address_of groups, sizeof groups);
        if (!found)
                return host_refuse("this machine is not in group %s\n", namespace);
        if (!saved)
        {
                crypto_forget(address_of keys, sizeof keys);
                return host_refuse("%s could not be written\n", LINK_GROUPS_PATH);
        }

        if (forget)
        {
                link_peers peers;
                bipolar held = link_peers_lock();
                positive dropped = 0;
                bool read = held >= 0 && link_peers_for_change(address_of peers);

                for (positive at = 0; read && at < peers.count; at++)
                        if (peers.peer[at].group == keys.mark)
                        {
                                peers.peer[at--] = peers.peer[--peers.count];
                                dropped++;
                        }
                if (read)
                        (void)link_peers_save(address_of peers);
                link_peers_unlock(held);
                if (read)
                        string_format(log, host_label "left group %s and removed the "
                                                      "%p machines it linked\n",
                                      namespace, dropped);
                else
                        string_format(log, host_label "left group %s; %s could not "
                                                      "be locked or read, so the "
                                                      "machines it linked are "
                                                      "kept\n",
                                      namespace, LINK_PEERS_PATH);
        }
        else
                string_format(log, host_label "left group %s; the machines it "
                                              "linked are still known\n",
                              namespace);
        log_flush();
        crypto_forget(address_of keys, sizeof keys);
        return 0;
}

/*
        A code, and the machines that use it.

        pair [NAME] makes six symbols, thirty bits, and waits; NAME CODE is
        said on the other machine. Both sides are the same group of one: the
        namespace is the name of the machine that is waiting, the secret is
        the code, and the key is what PBKDF2 makes of the two, the same slow
        derivation any group has, so a code can be guessed no faster than a
        group's secret can and has five minutes to be. A name typed wrong
        derives another key and finds nobody. The group lets in one machine
        and is gone (nearby.c), and the two know each other by the names they
        are called, which is what `link NAME` takes after.

        Crockford's alphabet: no u, and o, i and l read as 0 and 1, so a code
        read off one screen and typed on another survives a misread letter.
*/
static const char link_code_alphabet[] = "0123456789abcdefghjkmnpqrstvwxyz";
#define LINK_CODE_LENGTH 6
#define LINK_PAIR_MAY (WATERLINK_MAY_SHELL | WATERLINK_MAY_RUN | \
                       WATERLINK_MAY_FILES | WATERLINK_MAY_LOG)

static bool link_code_make(p8 address_to code)
{
        p32 bits;

        if (system_random_fill(address_of bits, sizeof(bits), 0) < 0)
                return false;
        for (positive at = 0; at < LINK_CODE_LENGTH; at++)
                code[at] = (p8)link_code_alphabet[(bits >> (5 * at)) & 31];
        code[LINK_CODE_LENGTH] = 0;
        crypto_forget(address_of bits, sizeof(bits));
        return true;
}

/*      A code as typed: capitals folded, hyphens and spaces left out, the
        look-alikes read as what they stand for. False unless it comes to
        exactly six symbols of the alphabet, which is also how a word after a
        machine's name is told from a command. */
static bool link_code_clean(string_address text, p8 address_to code)
{
        positive used = 0;

        for (positive at = 0; text[at]; at++)
        {
                p8 c = byte_to_lower((p8)text[at]);

                if (c == '-' || c == ' ')
                        continue;
                if (c == 'o')
                        c = '0';
                else if (c == 'i' || c == 'l')
                        c = '1';
                if (used == LINK_CODE_LENGTH || !string_first_of(link_code_alphabet, c))
                        return false;
                code[used++] = c;
        }
        code[used] = 0;
        return used == LINK_CODE_LENGTH;
}

//      A machine's name as the other one prints it: lowercase.
static fn link_fold_name(string_address name, p8 address_to into)
{
        positive at = 0;

        for (; name[at] && at < WATERLINK_NAME_MAX - 1; at++)
                into[at] = byte_to_lower((p8)name[at]);
        into[at] = 0;
}

//      abc-def, as it is said aloud and typed.
static fn link_code_text(p8 address_to code, p8 address_to text)
{
        memory_copy(text, code, 3);
        text[3] = '-';
        memory_copy(text + 4, code + 3, 3);
        text[7] = 0;
}

/*      Five minutes, or less when the environment says so, which is how a
        test sees a code run out. A longer wait is never taken. */
static p64 link_pair_seconds(void)
{
        bipolar seconds = link_decimal(
            file_environment((string_address) "WATERLINK_PAIR_SECONDS"));

        return seconds > 0 && seconds < LINK_PAIR_SECONDS ? (p64)seconds
                                                          : LINK_PAIR_SECONDS;
}

/*      This machine's side of a code in the groups file, under the lock the
        listener takes for it: a code for this namespace made before is
        replaced, and so is anything that has run out. Answers the mark the
        machines it links will carry. */
static bipolar link_pair_open(string_address namespace, p8 address_to code,
                              string_address expect, p32 address_to mark)
{
        link_groups groups;
        struct link_group_record address_to record;
        struct waterlink_group_keys keys;
        p8 key[32];
        p64 now = link_boot_seconds();
        bipolar lock;
        bipolar answer = -ENOSPC;

        //      The slow part, before the lock is taken.
        waterlink_group_derive(namespace, code, LINK_CODE_LENGTH,
                               WATERLINK_GROUP_ROUNDS, key);

        lock = link_peers_lock();
        if (lock < 0)
        {
                crypto_forget(key, sizeof key);
                return lock;
        }
        link_groups_load(address_of groups);
        for (positive at = 0; at < groups.count; at++)
                if (link_group_spent(groups.record + at, now) ||
                    ((groups.record[at].flags & LINK_GROUP_ONCE) &&
                     string_equals(groups.record[at].namespace, namespace)))
                        groups.record[at--] = groups.record[--groups.count];

        if (groups.count < LINK_GROUPS_MAX)
        {
                record = groups.record + groups.count++;
                memory_zero(record, sizeof(address_to record));
                string_copy(record->namespace, namespace);
                memory_copy(record->key, key, 32);
                record->may = LINK_PAIR_MAY;
                record->flags = LINK_GROUP_ONCE;
                record->expires = now + link_pair_seconds();
                record->expect = expect ? link_name_check(expect, WATERLINK_NAME_MAX)
                                        : 0;
                answer = link_groups_save(address_of groups) < 0 ? -EIO : 0;
                waterlink_group_keys_from(address_of keys, key, namespace);
                address_to mark = keys.mark;
                crypto_forget(address_of keys, sizeof keys);
        }
        link_peers_unlock(lock);
        crypto_forget(key, sizeof key);
        crypto_forget(address_of groups, sizeof groups);
        return answer;
}

//      The code taken back, when nobody used it.
static fn link_pair_close(p32 mark)
{
        link_groups groups;
        struct waterlink_group_keys keys;
        bipolar lock = link_peers_lock();

        //      Without the lock the file is left as it is: the code runs out
        //      by itself, and the listener drops it then.
        if (lock < 0)
                return;
        link_groups_load(address_of groups);
        for (positive at = 0; at < groups.count; at++)
        {
                waterlink_group_keys_from(address_of keys, groups.record[at].key,
                                          groups.record[at].namespace);
                if ((groups.record[at].flags & LINK_GROUP_ONCE) && keys.mark == mark)
                        groups.record[at--] = groups.record[--groups.count];
                crypto_forget(address_of keys, sizeof keys);
        }
        (void)link_groups_save(address_of groups);
        link_peers_unlock(lock);
        crypto_forget(address_of groups, sizeof groups);
}

/*      The listener as it was before a code turned it on: whether it ran,
        and what the switch said, if anything. A code nobody used leaves it so,
        and not on for good. */
typedef struct
{
        bool running;
        p8 word[16];
} link_before;

static fn link_before_take(link_before address_to before)
{
        before->word[0] = 0;
        (void)host_read_word(LINK_SWITCH_PATH, before->word, sizeof before->word);
        before->running = link_lock_owner() > 0;
}

static fn link_before_restore(link_before address_to before)
{
        if (!before->running)
                (void)link_switch(false, false);
        if (before->word[0])
                (void)radio_write_word(LINK_SWITCH_PATH, (string_address)before->word);
        else
                (void)system_remove_at(AT_FDCWD, LINK_SWITCH_PATH, 0);
}

//      Whether the listener has closed this namespace's code on a machine.
static bool link_pair_closed(string_address namespace)
{
        link_groups groups;
        bool closed = false;

        link_groups_load(address_of groups);
        for (positive at = 0; at < groups.count; at++)
                if ((groups.record[at].flags & LINK_GROUP_ONCE) &&
                    string_equals(groups.record[at].namespace, namespace))
                        closed = (groups.record[at].flags & LINK_GROUP_DONE) != 0;
        crypto_forget(address_of groups, sizeof groups);
        return closed;
}

/*      Until a machine carrying the mark is among the ones this machine
        knows, the code has run out, or somebody stops it. The listener does
        the pairing and writes the file this reads, a few times a second.
        The listener closes the code after it has written the machine, so the
        code is read first and the machines after: a closed code with no
        machine behind it is one used by a machine that was already linked,
        which keeps what it had. */
static b32 link_pair_wait(p32 mark, string_address namespace,
                          link_before address_to before)
{
        p64 until = link_boot_seconds() + link_pair_seconds() + 2;
        bipolar signals = link_signals_open();
        b32 stopped = 0;
        bool closed = false;

        for (;;)
        {
                link_peers peers;
                timespec limit = {0, 250000000};

                closed = link_pair_closed(namespace);
                link_peers_load(address_of peers);
                for (positive at = 0; at < peers.count; at++)
                        if (peers.peer[at].group == mark)
                        {
                                string_format(log, host_label "linked with %s: it and "
                                                              "this machine may each "
                                                              "use a terminal, "
                                                              "commands, files and "
                                                              "the log of the other\n",
                                              (string_address)peers.peer[at].name);
                                log_flush();
                                return 0;
                        }
                if (closed || link_boot_seconds() >= until)
                        break;
                if (signals < 0)
                        host_pause(250000000);
                else
                {
                        (void)descriptor_wait_readable(signals, address_of limit, null);
                        link_signals_take(signals, address_of stopped);
                        if (stopped)
                                break;
                }
        }

        link_before_restore(before);
        if (closed)
                return host_refuse("the machine that used the code was already "
                                   "linked, and keeps what it may do: "
                                   "moonwater link shows it\n");
        link_pair_close(mark);
        if (stopped)
                return host_refuse("stopped, and the code no longer works\n");
        return host_refuse("nobody used the code: both machines have to be on "
                           "one network, and a code lasts five minutes\n");
}

//      The code in the groups file and the listener on, or why not.
static b32 link_pair_start(string_address namespace, p8 address_to code,
                           string_address expect, p32 address_to mark,
                           link_before address_to before)
{
        bipolar opened = link_pair_open(namespace, code, expect, mark);

        if (opened == -ENOSPC)
                return host_refuse("this machine is in 8 groups already\n");
        if (opened < 0)
                return host_refuse("%s could not be written\n", LINK_GROUPS_PATH);
        link_before_take(before);
        if (!link_switch(true, false))
                return 0;
        link_pair_close(address_to mark);
        link_before_restore(before);
        return 1;
}

static b32 link_pair_here(string_address expect)
{
        p8 name[WATERLINK_NAME_MAX];
        p8 code[LINK_CODE_LENGTH + 1];
        p8 said[8];
        p32 mark;
        b32 refused;
        p8 only[WATERLINK_NAME_MAX];
        link_before before;

        //      Names are lowercase on both machines.
        if (expect)
        {
                link_fold_name(expect, only);
                expect = (string_address)only;
        }
        if (expect && !link_name_good(expect))
                return host_refuse("%s is not a machine name\n", expect);

        //      What peers will call this machine, which is what the other
        //      one types; a machine called by one of link's own words is
        //      told how to be called something else.
        link_machine_name(name);
        if (!link_name_good((string_address)name))
                return host_refuse("%s is not a name to link by: moonwater name "
                                   "NEW gives one that is\n",
                                   (string_address)name);
        if (!link_code_make(code))
                return host_fail("randomness", -EIO);

        refused = link_pair_start((string_address)name, code, expect,
                                  address_of mark, address_of before);
        if (refused)
                return refused;

        link_code_text(code, said);
        string_format(log, host_label "%s %s: on the other machine, moonwater "
                                      "link %s %s\n"
                           host_label "waiting five minutes for it\n",
                      (string_address)name, (string_address)said,
                      (string_address)name, (string_address)said);
        log_flush();
        crypto_forget(code, sizeof code);
        return link_pair_wait(mark, (string_address)name, address_of before);
}

static b32 link_pair_there(string_address typed_name, string_address typed)
{
        p8 code[LINK_CODE_LENGTH + 1];
        p8 name[WATERLINK_NAME_MAX];
        p8 own[WATERLINK_NAME_MAX];
        p32 mark;
        b32 refused;
        link_before before;

        link_fold_name(typed_name, name);
        link_machine_name(own);
        if (string_equals((string_address)own, (string_address)name))
                return host_refuse("%s is this machine's own name\n",
                                   (string_address)name);

        if (!link_code_clean(typed, code))
                return host_refuse("%s is not a code: six letters and digits, as "
                                   "moonwater link pair prints them\n",
                                   typed);
        refused = link_pair_start((string_address)name, code, null,
                                  address_of mark, address_of before);
        crypto_forget(code, sizeof code);
        if (refused)
                return refused;

        string_format(log, host_label "looking for %s on this network\n",
                      (string_address)name);
        log_flush();
        return link_pair_wait(mark, (string_address)name, address_of before);
}

// Whether a name is a machine this one is linked with.
static bool link_known(string_address name)
{
        link_peers peers;

        link_peers_load(address_of peers);
        return link_peer_named(address_of peers, name) != null;
}

static b32 link_main(string_address address_to arguments, positive count)
{
        string_address verb = count > 2 ? arguments[2] : null;
        positive kind;

        //      The key, the machines and the groups are root's files: for
        //      anyone else the page would say "nobody linked" of a machine
        //      that has a dozen.
        host_need_root();

        if (!verb)
                return link_status();

        if ((string_equals(verb, "on") || string_equals(verb, "off")) &&
            count == 3)
                return link_switch(string_equals(verb, "on"), true);
        if (string_equals(verb, "key") && count == 3)
                return link_key_verb();
        if (string_equals(verb, "serve") && count == 3)
                return link_serve();
        if (string_equals(verb, "pair") && (count == 3 || count == 4))
                return link_pair_here(count == 4 ? arguments[3] : null);

        //      The peers file has a second writer, the listener pairing
        //      members: each change made here happens under the same lock.
        if ((string_equals(verb, "add") && (count == 5 || count == 6)) ||
            (string_equals(verb, "remove") && count == 4) ||
            ((string_equals(verb, "allow") || string_equals(verb, "deny")) &&
             count >= 5))
        {
                struct waterlink_peer where;
                bipolar lock;
                b32 answer;

                //      An address is looked up before the lock, not under it.
                memory_zero(address_of where, sizeof where);
                if (string_equals(verb, "add") && count == 6 &&
                    !link_parse_place(arguments[5], where.address,
                                      address_of where.port))
                        return host_refuse("%s is not an address this can "
                                           "reach\n",
                                           arguments[5]);

                lock = link_peers_lock();
                if (lock < 0)
                        return host_refuse("%s could not be locked\n",
                                           LINK_PEERS_LOCK);
                answer =
                        string_equals(verb, "add")
                                ? link_add_locked(arguments[3], arguments[4],
                                                  count == 6 ? address_of where
                                                             : null)
                        : string_equals(verb, "remove")
                                ? link_remove_locked(arguments[3])
                                : link_grant_locked(arguments[3],
                                                    string_equals(verb, "allow"),
                                                    arguments + 4, count - 4);

                link_peers_unlock(lock);
                return answer;
        }

        if (string_equals(verb, "group"))
        {
                if (count == 3)
                        return link_status();
                if (string_equals(arguments[3], "leave") &&
                    (count == 5 ||
                     (count == 6 && string_equals(arguments[5], "forget"))))
                        return link_group_leave(arguments[4], count == 6);
                if (count >= 4)
                        return link_group_join(arguments + 3, count - 3);
        }

        //      push, pull, log, shell and run: a machine and what the kind
        //      takes.
        {
                static const positive least[] = {0, 4, 5, 6, 6, 4};
                static const positive most[] = {0, 4, ~(positive)0, 6, 6, 4};

                kind = string_table_find(verb, link_kind_names,
                                         sizeof link_kind_names[0],
                                         array_count(link_kind_names));
                if (kind && kind < array_count(link_kind_names))
                        return count >= least[kind] && count <= most[kind]
                                       ? link_client_run(arguments[3], (p8)kind,
                                                         arguments + 4, count - 4)
                                       : host_usage();
        }

        //      A word that is none of those is a machine: its terminal, one
        //      command on it, or, if it is not known and the word after it
        //      is a code, the pairing that makes it known. A name that is
        //      neither is told how to become one.
        {
                p8 folded[WATERLINK_NAME_MAX];

                link_fold_name(verb, folded);
                if (!link_known(verb) && link_known((string_address)folded))
                        verb = (string_address)folded;
        }
        if (link_known(verb))
                return link_client_run(verb,
                                       count == 3 ? LINK_KIND_SHELL : LINK_KIND_RUN,
                                       arguments + 3, count - 3);
        //      The code as it is printed, abc-def: a word of six letters is
        //      as likely to be a command (whoami, reboot) as a code, and a
        //      misspelt machine would otherwise start a five minute wait.
        if (count == 4)
        {
                p8 code[LINK_CODE_LENGTH + 1];

                if (string_length(arguments[3]) == LINK_CODE_LENGTH + 1 &&
                    arguments[3][3] == '-' &&
                    link_code_clean(arguments[3], code))
                        return link_name_good(verb)
                                       ? link_pair_there(verb, arguments[3])
                                       : host_refuse("%s is not a machine "
                                                     "name\n",
                                                     verb);
        }
        if (link_name_good(verb))
                return host_refuse("no machine is called %s: moonwater link pair "
                                   "on it, then moonwater link NAME CODE here\n",
                                   verb);
        return host_usage();
}

/*
        The machine process's side, once a loop: the listener runs while the
        switch is on and something is not already running it, restarted after
        one second, then two, four and on to a minute if it keeps dying; and
        stopped when the switch is off. Whoever holds the lock is the
        listener, whoever started it -- `link on`, this, or a person running
        `link serve` by hand.
*/
static p64 link_keep_next;
static p64 link_keep_wait = 1;

static fn link_keep(void)
{
        p64 now = system_clock_ns(HOST_CLOCK_BOOTTIME) / 1000000000ull;
        p8 word[16];
        bipolar owner;

        //      No switch at all is nobody's decision: a listener somebody ran
        //      by hand is left alone. Off is somebody's.
        host_read_word(LINK_SWITCH_PATH, word, sizeof word);
        if (!string_equals((string_address)word, "on"))
        {
                if (string_equals((string_address)word, "off") &&
                    link_lock_owner() > 0)
                        (void)link_lock_signal(15);
                return;
        }

        owner = link_lock_owner();

        if (owner > 0)
        {
                link_keep_wait = 1;
                return;
        }

        if (now < link_keep_next)
                return;

        link_keep_next = now + link_keep_wait;
        if (link_keep_wait < 60)
                link_keep_wait *= 2;
        link_serve_start(false);
}

#endif // WATERLINK_COMMAND_INCLUDED
