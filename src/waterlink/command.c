/*
        `moonwater link`: the verbs.

                link                    what is on, this machine's key, the
                                        peers, what is open
                link on | off           the listener, kept across boots
                link key                this machine's public key
                link pair NAME KEY [HOST[:PORT]]
                link forget NAME
                link allow NAME GRANT...
                link deny NAME GRANT...
                link shell NAME         a terminal on NAME
                link run NAME COMMAND...
                link push NAME FILE PATH   a file there, whole or not at all
                link pull NAME PATH FILE   and back
                link log NAME           follow the far kernel log
                link serve              the listener, in the foreground

        Pairing is by key and both ways, as WireGuard's is: each machine is
        told the other's key, and a machine answers only a key it was told.
        A new peer may do nothing but the moonwater verbs; everything past
        that is a grant somebody gave it by name.

        The switch, the key and the peers live in /root, beside the wireless
        networks, so install carries them to the disk and wipe keeps them.

        Dawn Larsson - Apache-2.0 license
        github.com/dawnlarsson/moonwater
*/

#ifndef WATERLINK_COMMAND_INCLUDED
#define WATERLINK_COMMAND_INCLUDED

#include "service.c"

static fn link_usage_write(writer out)
{
        string_format(out,
                      TERM_BOLD "  link" TERM_RESET
                      "                        " TERM_DIM "on or off, this machine's key, peers, sessions" TERM_RESET "\n"
                      TERM_BOLD "  link on|off" TERM_RESET
                      "                 " TERM_DIM "the listener, udp 22348, kept across boots" TERM_RESET "\n"
                      TERM_BOLD "  link key" TERM_RESET
                      "                    " TERM_DIM "this machine's public key, made on first use" TERM_RESET "\n"
                      TERM_BOLD "  link pair NAME KEY [HOST[:PORT]]" TERM_RESET "\n"
                      "                              " TERM_DIM "know a machine by its key" TERM_RESET "\n"
                      TERM_BOLD "  link join NAMESPACE [SECRET] [allow GRANT...]" TERM_RESET "\n"
                      "                              " TERM_DIM "pair with every machine on this network in it" TERM_RESET "\n"
                      TERM_BOLD "  link leave NAMESPACE [forget]" TERM_RESET "\n"
                      "                              " TERM_DIM "stop, and maybe forget the machines it paired" TERM_RESET "\n"
                      TERM_BOLD "  link forget NAME" TERM_RESET
                      "            " TERM_DIM "stop knowing it" TERM_RESET "\n"
                      TERM_BOLD "  link allow|deny NAME GRANT..." TERM_RESET "\n"
                      "                              " TERM_DIM "run shell files log screen channels verbs" TERM_RESET "\n"
                      TERM_BOLD "  link shell NAME" TERM_RESET
                      "             " TERM_DIM "a terminal on NAME" TERM_RESET "\n"
                      TERM_BOLD "  link run NAME COMMAND..." TERM_RESET
                      "    " TERM_DIM "one command on NAME, its output and status here" TERM_RESET "\n"
                      TERM_BOLD "  link push NAME FILE PATH" TERM_RESET
                      "    " TERM_DIM "a file here to PATH there" TERM_RESET "\n"
                      TERM_BOLD "  link pull NAME PATH FILE" TERM_RESET
                      "    " TERM_DIM "PATH there to a file here" TERM_RESET "\n"
                      TERM_BOLD "  link log NAME" TERM_RESET
                      "               " TERM_DIM "follow NAME's kernel log" TERM_RESET "\n"
                      TERM_BOLD "  link serve" TERM_RESET
                      "                  " TERM_DIM "the listener in the foreground" TERM_RESET "\n");
        log_flush();
}

static b32 link_usage(void)
{
        link_usage_write(log_error);
        return 2;
}

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
        line: "link join NAME" followed by a word that is not "allow".
*/
static bool link_script_names_secret(string_address script,
                                     string_address namespace)
{
        positive length = string_length(namespace);

        for (string_address at = script; at && *at; at++)
        {
                string_address word;

                if (!host_starts(at, "link join "))
                        continue;
                word = at + 10;
                word += string_span_of_set(word, " ");
                if (!host_starts(word, namespace) || word[length] != ' ')
                        continue;
                word += length;
                word += string_span_of_set(word, " ");
                if (*word && *word != '\n' && *word != ';' && *word != '#' &&
                    !host_starts(word, "allow"))
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

                host_read_text(LINK_SWITCH_PATH, word, sizeof word);
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
                link_key_text(me.public, key);
                string_format(log, "  this machine  %s\n", (string_address)key);
                crypto_forget(address_of me, sizeof me);
        }
        else
                string_format(log, "  this machine  no key yet: moonwater link key\n");

        //      The groups: which, what members get, and whether the secret
        //      sits in the machine script, where /dev/spark shows it to any
        //      user on the machine.
        link_groups_load(address_of groups);
        {
                p8 script[16384];
                bipolar script_length = file_slurp_once_at(
                        AT_FDCWD, "/root/main.moonwater.sh", script,
                        sizeof script - 1);

                script[script_length > 0 ? script_length : 0] = 0;

                for (positive g = 0; g < groups.count; g++)
                {
                        struct waterlink_group_keys keys;
                        p8 grants[96];
                        bool shown = link_script_names_secret(
                                (string_address)script,
                                groups.record[g].namespace);

                        waterlink_group_keys_from(address_of keys,
                                                  groups.record[g].key,
                                                  groups.record[g].namespace);
                        marks[g] = keys.mark;
                        crypto_forget(address_of keys, sizeof keys);
                        link_grants_text(groups.record[g].may, grants,
                                         sizeof grants);
                        string_format(log, "  in %s: members on this network "
                                           "pair by themselves, and may %s\n",
                                      (string_address)groups.record[g].namespace,
                                      (string_address)grants);
                        if (shown)
                                string_format(log,
                                              "    the machine script holds "
                                              "its secret, which any user can "
                                              "read through /dev/spark: run "
                                              "moonwater link join %s SECRET "
                                              "once and keep only the name "
                                              "there\n",
                                              (string_address)groups.record[g].namespace);
                }
        }

        link_peers_load(address_of peers);
        if (!peers.count)
                string_format(log, "  no peers: moonwater link pair NAME KEY HOST\n");

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
                        string_address how = "paired by hand";
                        p8 in_group[48];

                        if (peer->group)
                        {
                                how = "paired by a group this machine left";
                                for (positive g = 0; g < groups.count; g++)
                                        if (marks[g] == peer->group)
                                        {
                                                string_copy((string_address)in_group,
                                                            "paired in ");
                                                string_copy((string_address)in_group + 10,
                                                            groups.record[g].namespace);
                                                how = (string_address)in_group;
                                        }
                        }
                        string_format(log, "  %s  %s...  may %s, %s\n",
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

        string_format(log, "\n");
        link_usage_write(log);
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

static b32 link_pair_locked(string_address name, string_address text,
                            string_address place)
{
        link_peers peers;
        struct waterlink_peer peer;
        struct waterlink_peer address_to found;
        struct waterlink_identity me;

        if (!link_name_good(name))
                return host_refuse("%s is not a peer name: letters, digits, "
                                   ". - _, up to 31\n",
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

        if (link_identity(address_of me, false) >= 0 &&
            crypto_same(me.public, peer.key, 32))
                return host_refuse("that is this machine's own key%s\n", "");

        if (place && !link_parse_place(place, peer.address, address_of peer.port))
                return host_refuse("%s is not an address this can reach\n",
                                   place);

        string_copy(peer.name, name);
        peer.may = WATERLINK_MAY_DEFAULT;

        if (!link_peers_for_change(address_of peers))
                return host_refuse("%s could not be read\n", LINK_PEERS_PATH);
        found = link_peer_named(address_of peers, name);
        if (!found)
                found = link_peer_keyed(address_of peers, peer.key);

        if (found)
        {
                //      Paired again: the grants stay with the key they were
                //      given to, and nowhere else.
                if (crypto_same(found->key, peer.key, 32))
                        peer.may = found->may;
                *found = peer;
        }
        else
        {
                if (peers.count >= LINK_PEERS_MAX)
                        return host_refuse("this machine knows %s peers "
                                           "already\n",
                                           "64");
                peers.peer[peers.count++] = peer;
        }

        if (link_peers_save(address_of peers) < 0)
                return host_refuse("%s could not be written\n", LINK_PEERS_PATH);

        string_format(log, host_label "paired %s; it may use the moonwater "
                                      "verbs, and more once allowed\n",
                      name);
        log_flush();
        return 0;
}

static b32 link_forget_locked(string_address name)
{
        link_peers peers;
        struct waterlink_peer address_to found;

        link_peers_load(address_of peers);
        found = link_peer_named(address_of peers, name);
        if (!found)
                return host_refuse("no peer is called %s\n", name);

        *found = peers.peer[--peers.count];
        if (link_peers_save(address_of peers) < 0)
                return host_refuse("%s could not be written\n", LINK_PEERS_PATH);

        string_format(log, host_label "forgot %s\n", name);
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
                return host_refuse("no peer is called %s\n", name);

        for (positive at = 0; at < count; at++)
        {
                p32 bit = link_grant_bit(words[at]);

                if (!bit)
                        return host_refuse("%s is not a grant: run shell files "
                                           "log screen channels verbs\n",
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
                (void)system_call_3(syscall(close_range), 3, ~0u, 0);
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

static b32 link_switch(bool on)
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
                        return host_refuse("the listener did not stop%s\n", "");
                string_format(log, host_label "link off; sessions closed\n");
                log_flush();
                return 0;
        }

        if (link_identity(address_of me, true) < 0)
                return host_refuse("%s cannot be read or made\n", LINK_KEY_PATH);

        if (link_lock_owner() <= 0)
                link_serve_start(true);
        if (!link_wait_owner(true))
                return host_refuse("the listener did not start: is udp %s "
                                   "taken?\n",
                                   "22348");

        link_key_text(me.public, key);
        crypto_forget(address_of me, sizeof me);
        string_format(log, host_label "link on, udp %p; this machine is %s\n",
                      (positive)link_port(), (string_address)key);
        log_flush();
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
        join NAMESPACE [SECRET] [allow GRANT...]

        With a secret, this machine is in the group from now on and across
        boots, install and wipe; the secret itself is never kept, only what
        PBKDF2 makes of it and a salted hash that lets a machine script
        joining every boot skip the derivation when nothing changed. Without
        one, the group already joined is joined again, or a new group gets a
        secret of 160 random bits, printed once for the other machines.
        Members get the grants named here, and the verbs when none are. The
        link is switched on.
*/
static const char link_base32[] = "abcdefghijklmnopqrstuvwxyz234567";

static b32 link_join(string_address address_to words, positive count)
{
        string_address namespace = words[0];
        string_address secret = null;
        link_groups groups;
        struct link_group_record address_to record = null;
        p8 made[40];
        bool generated = false;
        bool granted = false;
        bool good;
        p32 may = 0;
        positive at = 1;
        p8 grants[96];

        if (!link_name_good(namespace) ||
            string_length(namespace) >= WATERLINK_NAMESPACE_MAX)
                return host_refuse("%s is not a namespace: letters, digits, "
                                   ". - _, up to 31\n",
                                   namespace);
        if (at < count && !string_equals(words[at], "allow"))
                secret = words[at++];
        if (at < count)
        {
                if (!string_equals(words[at], "allow") || at + 1 == count)
                        return link_usage();
                may = link_grants_of(words + at + 1, count - at - 1,
                                     address_of good);
                if (!good)
                        return host_refuse("a grant is one of run shell files "
                                           "log screen channels verbs%s\n",
                                           "");
                granted = true;
        }

        link_groups_load(address_of groups);
        for (positive look = 0; look < groups.count; look++)
                if (string_equals(groups.record[look].namespace, namespace))
                        record = groups.record + look;

        if (!secret && !record)
        {
                p8 random[20];

                if (system_random_fill(random, sizeof random, 0) < 0)
                {
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
                        return host_refuse("this machine is in %s groups "
                                           "already\n",
                                           "8");
                record = groups.record + groups.count++;
                memory_zero(record, sizeof(address_to record));
                string_copy(record->namespace, namespace);
                record->may = WATERLINK_MAY_DEFAULT;
        }

        if (secret)
        {
                p8 check[32];

                link_group_check(namespace, (p8 address_to)secret,
                                 string_length(secret), check);
                //      The slow part, once: a script that joins at every boot
                //      with the same secret does not pay it again.
                if (!crypto_same(check, record->check, 32))
                {
                        waterlink_group_derive(namespace, (p8 address_to)secret,
                                               string_length(secret),
                                               WATERLINK_GROUP_ROUNDS,
                                               record->key);
                        memory_copy(record->check, check, 32);
                }
                crypto_forget(check, sizeof check);
        }
        if (granted)
                record->may = may;

        if (link_groups_save(address_of groups) < 0)
                return host_refuse("%s could not be written\n", LINK_GROUPS_PATH);

        link_grants_text(record->may, grants, sizeof grants);
        string_format(log, host_label "in %s; members may %s here\n", namespace,
                      (string_address)grants);
        if (generated)
                string_format(log,
                              host_label "the secret is %s -- on each other "
                                         "machine: moonwater link join %s %s\n",
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
        return link_switch(true);
}

// leave NAMESPACE [forget]: stop announcing it, and maybe its peers too.
static b32 link_leave(string_address namespace, bool forget)
{
        link_groups groups;
        struct waterlink_group_keys keys;
        bool found = false;

        link_groups_load(address_of groups);
        for (positive at = 0; at < groups.count; at++)
                if (string_equals(groups.record[at].namespace, namespace))
                {
                        waterlink_group_keys_from(address_of keys,
                                                  groups.record[at].key,
                                                  namespace);
                        groups.record[at] = groups.record[--groups.count];
                        found = true;
                        break;
                }
        if (!found)
                return host_refuse("this machine is not in %s\n", namespace);
        if (link_groups_save(address_of groups) < 0)
                return host_refuse("%s could not be written\n", LINK_GROUPS_PATH);

        if (forget)
        {
                link_peers peers;
                bipolar lock = link_peers_lock();
                positive dropped = 0;
                bool read = link_peers_for_change(address_of peers);

                for (positive at = 0; read && at < peers.count; at++)
                        if (peers.peer[at].group == keys.mark)
                        {
                                peers.peer[at--] = peers.peer[--peers.count];
                                dropped++;
                        }
                if (read)
                        (void)link_peers_save(address_of peers);
                link_peers_unlock(lock);
                if (read)
                        string_format(log, host_label "left %s and forgot the "
                                                      "%p machines it paired\n",
                                      namespace, dropped);
                else
                        string_format(log, host_label "left %s; %s could not "
                                                      "be read, so the "
                                                      "machines it paired are "
                                                      "kept\n",
                                      namespace, LINK_PEERS_PATH);
        }
        else
                string_format(log, host_label "left %s; the machines it paired "
                                              "are still known\n",
                              namespace);
        log_flush();
        crypto_forget(address_of keys, sizeof keys);
        return 0;
}

static b32 link_main(string_address address_to arguments, positive count)
{
        string_address verb = count > 2 ? arguments[2] : null;

        if (!verb)
                return link_status();

        if (string_equals(verb, "-h") || string_equals(verb, "--help") ||
            string_equals(verb, "help"))
        {
                link_usage_write(log);
                return 0;
        }

        if (!bowl_is_root())
                return host_refuse("%s needs root\n", "moonwater link");

        if ((string_equals(verb, "on") || string_equals(verb, "off")) &&
            count == 3)
                return link_switch(string_equals(verb, "on"));
        if (string_equals(verb, "key") && count == 3)
                return link_key_verb();
        if (string_equals(verb, "serve") && count == 3)
                return link_serve();
        if (string_equals(verb, "join") && count >= 4)
                return link_join(arguments + 3, count - 3);
        if (string_equals(verb, "leave") && (count == 4 ||
                                             (count == 5 &&
                                              string_equals(arguments[4],
                                                            "forget"))))
                return link_leave(arguments[3], count == 5);
        //      The peers file has a second writer, the listener pairing
        //      members: each change made here happens under the same lock.
        if ((string_equals(verb, "pair") && (count == 5 || count == 6)) ||
            (string_equals(verb, "forget") && count == 4) ||
            ((string_equals(verb, "allow") || string_equals(verb, "deny")) &&
             count >= 5))
        {
                bipolar lock = link_peers_lock();
                b32 answer =
                        string_equals(verb, "pair")
                                ? link_pair_locked(arguments[3], arguments[4],
                                                   count == 6 ? arguments[5]
                                                              : null)
                        : string_equals(verb, "forget")
                                ? link_forget_locked(arguments[3])
                                : link_grant_locked(arguments[3],
                                                    string_equals(verb, "allow"),
                                                    arguments + 4, count - 4);

                link_peers_unlock(lock);
                return answer;
        }
        //      shell, run, push, pull and log: a peer and what the kind takes.
        {
                static const positive least[] = {0, 4, 5, 6, 6, 4};
                static const positive most[] = {0, 4, ~(positive)0, 6, 6, 4};
                positive kind = string_table_find(verb, link_kind_names,
                                                  sizeof link_kind_names[0],
                                                  array_count(link_kind_names));

                if (kind && kind < array_count(link_kind_names) &&
                    count >= least[kind] && count <= most[kind])
                        return link_client_run(arguments[3], (p8)kind,
                                               arguments + 4, count - 4);
        }
        return link_usage();
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
        host_read_text(LINK_SWITCH_PATH, word, sizeof word);
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
