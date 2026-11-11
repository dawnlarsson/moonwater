/*
        `moonwater link`: the verbs.

                link                    who this machine is linked with and
                                        what each may do
                link pair [NAME]        a code, and wait for a machine to use it
                link cancel             end every wait for a code
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
                link doctor             the listener, port, addresses, route and
                                        discovery: each pass or fail, and the fix
                link transport [network|modem]   how machines reach each other
                link modem key          the modem's key, made on first use, in hex
                link modem send FILE    FILE over the sound card, to the receiver

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
        them. On a Mac they live in the user's ~/Library/Application Support/
        moonwater instead (host_darwin.c), and nothing on a Mac is root's.

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
        line: "link group NAME" followed by a word that is not "allow" and
        not "-" (the secret on standard input, which is not in the script).
*/
static p8 link_script_names_secret(string_address script,
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
                //      allow is the word, and not the start of a secret that
                //      happens to begin with it.
                if (*word && *word != '\n' && *word != ';' && *word != '#' &&
                    !(string_has_prefix(word, "allow") &&
                      (!word[5] || string_first_of(" \n;#<", word[5]))) &&
                    !(*word == '-' && (!word[1] || string_first_of(" \n;#<", word[1]))))
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
        bipolar owner = host_lock_owner(LINK_LOCK_PATH);
        p8 key[48];
        p8 word[16];
        b32 on;

        //      A listener that is off, or older than this, left no state. One
        //      that was killed left its state behind, and it says nothing of
        //      sessions or peers heard now: a clean stop removes the file.
        if (owner <= 0 ||
            link_read_private_records(LINK_STATE_PATH, (p8 address_to)address_of state,
                                      sizeof state, sizeof state,
                                      address_of state_length) < 0 ||
            state_length != sizeof state)
                memory_zero(address_of state, sizeof state);

        //      The first line is the listener, and the switch and the process
        //      are two things: each pairing of them says what is wrong and the
        //      command that answers it.
        host_read_word(LINK_SWITCH_PATH, word, sizeof word);
        on = string_equals((string_address)word, "on");
        if (owner > 0 && on)
                string_format(log, "link on, udp %p  next: moonwater link pair\n",
                              (positive)link_port());
        else if (owner > 0 && !word[0])
                string_format(log, "link on, run by hand  next: moonwater link on, "
                                   "to keep it across boots\n");
        else if (owner > 0)
                string_format(log, "link off, but still running  next: moonwater link off\n");
        else if (on)
                string_format(log, "link on, but not running  next: moonwater link on, "
                                   "which says why it did not start\n");
        else
                string_format(log, "link off, udp %p  next: moonwater link on\n",
                              (positive)link_port());

        if (link_identity(address_of me, false) >= 0)
        {
                p8 own[WATERLINK_NAME_MAX];

                link_key_text(me.public, key);
                link_machine_name(own);
                string_format(log, "  this machine  %s  %s  next: moonwater link pair\n",
                              (string_address)own, (string_address)key);
                crypto_forget(address_of me, sizeof me);
        }
        else
                string_format(log, "  this machine  no key yet  next: moonwater link on, "
                                   "which makes one\n");

        //      The groups: which, what members get, and whether the secret
        //      sits in the machine script, where /dev/spark shows it to any
        //      user on the machine.
        if (!link_groups_for_show(address_of groups))
                string_format(log, "  %s could not be read  next: ls -l %s
",
                              LINK_GROUPS_PATH, LINK_GROUPS_PATH);
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
                        p8 shown;

                        if (groups.record[g].flags & LINK_GROUP_ONCE)
                        {
                                p64 now = link_boot_seconds();
                                p64 left = groups.record[g].expires > now
                                                   ? groups.record[g].expires - now
                                                   : 0;

                                //      A code is not a group: what it linked
                                //      is not said to be in one. A code that is
                                //      still there once it has run out is one
                                //      nothing has cleared: cancel clears it.
                                if (!(groups.record[g].flags & LINK_GROUP_DONE))
                                {
                                        if (left)
                                                string_format(log, "  a pairing code for %s is "
                                                                   "waiting, %p seconds left  "
                                                                   "next: moonwater link cancel\n",
                                                              (string_address)groups.record[g].namespace,
                                                              (positive)left);
                                        else
                                                string_format(log, "  a pairing code for %s ran out "
                                                                   "and was not cleared  next: "
                                                                   "moonwater link cancel\n",
                                                              (string_address)groups.record[g].namespace);
                                }
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
                                           "link themselves, and may %s  next: "
                                           "moonwater link group leave %s\n",
                                      (string_address)groups.record[g].namespace,
                                      (string_address)grants,
                                      (string_address)groups.record[g].namespace);
                        if (shown)
                                string_format(log,
                                              "    the machine script holds "
                                              "its secret, which any user can "
                                              "read through /dev/spark: run "
                                              "moonwater link group %s - "
                                              "once, the secret on standard "
                                              "input, and keep only the name "
                                              "there\n",
                                              (string_address)groups.record[g].namespace);
                }
                crypto_forget(script, sizeof script);
        }

        //      A file that is there and cannot be read is not an empty list:
        //      nobody linked is said only of one that is empty.
        if (!link_peers_for_change(address_of peers))
                string_format(log, "  %s could not be read  next: ls -l %s\n",
                              LINK_PEERS_PATH, LINK_PEERS_PATH);
        else if (!peers.count)
                string_format(log, "  nobody linked yet: moonwater link pair, "
                                   "and on the other machine moonwater link NAME CODE\n");

        for (positive at = 0; at < peers.count; at++)
        {
                struct waterlink_peer address_to peer = peers.peer + at;
                p8 grants[96];
                p8 place[64];
                p8 short_key[12];
                link_seen_entry address_to seen = null;
                b32 other = false;

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
                //      the address it was paired with. Another key heard at that
                //      address is a machine that is not this one any more.
                for (positive s_at = 0; s_at < state.seen_count &&
                                        s_at < LINK_PEERS_MAX;
                     s_at++)
                {
                        if (crypto_same(state.seen[s_at].key, peer->key, 32))
                                seen = state.seen + s_at;
                        else if (peer->port && state.seen[s_at].port == peer->port &&
                                 crypto_same(state.seen[s_at].address, peer->address, 16))
                                other = true;
                }

                if (seen)
                {
                        p8 when[32];

                        link_ago(seen->seen, when);
                        link_place_text(seen->address, seen->port, place);
                        if (!peer->may)
                                string_format(log, "      heard %s from %s  next: "
                                                   "moonwater link allow %s run\n",
                                              (string_address)when,
                                              (string_address)place,
                                              (string_address)peer->name);
                        else
                                string_format(log, "      heard %s from %s  next: "
                                                   "moonwater link %s\n",
                                              (string_address)when,
                                              (string_address)place,
                                              (string_address)peer->name);
                }
                else if (owner <= 0)
                        string_format(log, "      not heard: the listener is not running  "
                                           "next: moonwater link on\n");
                else if (other)
                {
                        link_place_text(peer->address, peer->port, place);
                        string_format(log, "      its key does not match: %s answers with "
                                           "another key  next: moonwater link remove %s, "
                                           "then moonwater link pair\n",
                                      (string_address)place,
                                      (string_address)peer->name);
                }
                else if (peer->port)
                {
                        link_place_text(peer->address, peer->port, place);
                        string_format(log, "      not heard at its last address %s this boot: "
                                           "it may be off, or on another network  next: "
                                           "moonwater link doctor\n",
                                      (string_address)place);
                }
                else
                        string_format(log, "      no address; it can reach this machine, not "
                                           "the other way  next: moonwater link add %s KEY "
                                           "HOST[:PORT]\n",
                                      (string_address)peer->name);
        }

        positive shown = 0;

        for (positive at = 0; at < state.open_count && at < LINK_SESSIONS; at++)
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
        return_if(got < 0, host_fail(LINK_KEY_PATH, got));

        link_key_text(me.public, key);
        crypto_forget(address_of me, sizeof me);
        host_say(log, "%s\n", (string_address)key);
        return 0;
}

//      The refusal of a word no machine or group may be called, which says
//      what it was asked to be: a name, or a group name.
#define link_refuse_name(name, what)                                           \
        host_refuse("%s is not a %s: letters, digits, . - _, up to 31, and "   \
                    "not one of the words link uses\n",                        \
                    (name), (what))

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
                return link_refuse_name(name, "name");

        memory_zero(address_of peer, sizeof peer);
        if (!link_key_parse(text, peer.key))
                return host_refuse("%s is not a link key: 44 characters of "
                                   "base64, as moonwater link key prints\n",
                                   text);

        {
                p8 scalar[32] = {1};
                p8 shared[32];
                p8 valid = crypto_x25519(shared, scalar, peer.key);

                crypto_forget(scalar, sizeof scalar);
                crypto_forget(shared, sizeof shared);
                if (!valid)
                        return host_refuse("%s is not a usable link key\n",
                                           text);
        }

        p8 own = link_identity(address_of me, false) >= 0 &&
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
                        //      The group that paired it and the last greeting
                        //      heard from it stay too: without them the machine
                        //      is no longer a member, and a leave forget does not
                        //      take it, and an old greeting would be heard again.
                        peer.may = found->may;
                        peer.group = found->group;
                        peer.seen = found->seen;
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

        host_say(log, host_label "added %s; it may do nothing yet: "
                                 "moonwater link allow %s run\n",
                 name, name);
        return 0;
}

static b32 link_remove_locked(string_address name)
{
        link_peers peers;
        struct waterlink_peer address_to found;

        if (!link_peers_for_change(address_of peers))
                return host_refuse("%s could not be read\n", LINK_PEERS_PATH);
        found = link_peer_named(address_of peers, name);
        if (!found)
                return host_refuse("no machine is called %s\n", name);

        *found = peers.peer[--peers.count];
        if (link_peers_save(address_of peers) < 0)
                return host_refuse("%s could not be written\n", LINK_PEERS_PATH);

        host_say(log, host_label "removed %s\n", name);
        return 0;
}

static b32 link_grant_locked(string_address name, p8 allow,
                             string_address address_to words, positive count)
{
        link_peers peers;
        struct waterlink_peer address_to found;
        p8 grants[96];

        if (!link_peers_for_change(address_of peers))
                return host_refuse("%s could not be read\n", LINK_PEERS_PATH);
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
        host_say(log, host_label "%s may %s\n", name,
                 (string_address)grants);
        return 0;
}

/*
        `moonwater link serve` in a session of its own, which is how both
        `link on` and the machine process start the listener. From a command,
        detached from whoever asked: nothing open but /dev/null, and not this
        process's child, so a terminal closing takes nothing with it. The
        machine process keeps its own child and restarts it if it dies.
*/
static fn link_serve_start(p8 detached, bipolar ready)
{
        p8 digits[12];
        string_address words[] = {"moonwater", "link", "serve", (string_address)digits, null};
        string_address plain[] = {"moonwater", "link", "serve", null};

        if (ready >= 0)
                digits[positive_into_string(digits, (positive)ready)] = end;
        if (detached ? !host_detach((p32)(ready >= 0 ? ready : descriptors_none), descriptors_none)
                     : system_fork() != 0)
                return;
        if (!detached)
        {
                (void)system_call(syscall(setsid));
                (void)descriptors_close_except(3, descriptors_none, descriptors_none);
        }
        //      The listener carries what the one that started it ignored no further.
        (void)system_signal_install(13, 0, 0, 0, null);
        (void)shell_exec_file((string_address)LINK_EXECUTABLE,
                              ready >= 0 ? words : plain, ready >= 0 ? 4 : 3,
                              file_environment_all());
        system_call_1(syscall(exit), 127);
}

/*
        The listener's lock lasts as long as the listener, and the lock names
        its holder. `link off` waits on that pid's pidfd, which wakes at the
        holder's exit (its locks go before the exit is told), and reads the
        lock again. A holder that cannot be opened as a pidfd is waited for a
        turn at a time, as before. Three seconds, then refused.
*/
static p8 link_wait_stopped(void)
{
        p64 until = system_clock_ns(HOST_CLOCK_BOOTTIME) + 3000000000ull;

        for (;;)
        {
                b32 owner = host_lock_owner(LINK_LOCK_PATH);
                p64 now = system_clock_ns(HOST_CLOCK_BOOTTIME);
                bipolar pid;
                timespec limit;

                if (owner <= 0)
                        return true;
                if (now >= until)
                        return false;
                pid = link_child_watch((positive)owner);
                if (pid < 0)
                {
                        host_pause(50000000);
                        continue;
                }
                limit.tv_sec = (p64)(until - now) / 1000000000ull;
                limit.tv_nsec = (p64)(until - now) % 1000000000ull;
                (void)link_wait_readable(pid, address_of limit);
                system_close(pid);
        }
}

/*
        The listener writes one byte to the pipe `link on` gave it once its
        lock is held and its port is open, and closes the pipe. A pipe that
        ends without the byte is a listener that did not start. Three seconds,
        then the same question as before: is the lock held by someone?
*/
static p8 link_wait_started(bipolar ready)
{
        timespec limit = {3, 0};
        p8 byte;

        if (link_wait_readable(ready, address_of limit) > 0 &&
            system_read_retry((positive)ready, address_of byte, 1) == 1)
                return true;
        return host_lock_owner(LINK_LOCK_PATH) > 0;
}

static b32 link_switch(p8 on, p8 say)
{
        struct waterlink_identity me;
        p8 key[48];

        //      Switched on, the word is written only once the listener has a
        //      key to run with: without one a refused `link on` left the switch
        //      on, and the machine process retried a listener that could never
        //      start, at every backoff, for as long as the key stayed unreadable.
        if (on)
        {
                if (link_identity(address_of me, true) < 0)
                        return host_refuse("%s cannot be read or made\n", LINK_KEY_PATH);
                link_key_text(me.public, key);
                crypto_forget(address_of me, sizeof me);
        }

        if (radio_write_word(LINK_SWITCH_PATH, on ? "on" : "off") < 0)
                return host_refuse("%s could not be written\n",
                                   LINK_SWITCH_PATH);

        if (!on)
        {
                (void)host_lock_signal(LINK_LOCK_PATH, 15);
                if (!link_wait_stopped())
                        return host_refuse("the listener did not stop\n");
                if (say)
                {
                        host_say(log, host_label "link off; sessions closed\n");
                }
                return 0;
        }

        if (host_lock_owner(LINK_LOCK_PATH) <= 0)
        {
                b32 ends[2];
                p8 started;
                bipolar piped = system_pipe(ends, 0);

                //      A pipe that cannot be made is the command's failure,
                //      not a listener that could not take the port: the
                //      refusal below is for the second.
                return_if(piped < 0, host_fail("link on", piped));
                link_serve_start(true, (bipolar)ends[1]);
                system_close((bipolar)ends[1]);
                started = link_wait_started((bipolar)ends[0]);
                system_close((bipolar)ends[0]);
                if (!started)
                        return host_refuse("the listener did not start: is udp %p "
                                           "taken?\n", (positive)link_port());
        }

        // The key's text was made above, before the identity was forgotten: made again here it was the
        // forgotten one, 32 zeros, and link on said this machine is AAAA...A=.
        if (say)
        {
                host_say(log, host_label "link on, udp %p; this machine is %s\n",
                         (positive)link_port(), (string_address)key);
        }
        return 0;
}

//      Whether the switch says on: the machine process starts the listener
//      from it, and a person's `link on` or Settings switch is what sets it.
static p8 link_switch_is_on(void)
{
        p8 word[16];

        host_read_word(LINK_SWITCH_PATH, word, sizeof word);
        return string_equals((string_address)word, "on");
}

static p32 link_grants_of(string_address address_to words, positive count,
                          p8 address_to good)
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
        group NAMESPACE [SECRET|-] [allow GRANT...]

        "-" reads the secret from standard input, which keeps it out of argv
        and of the shell's history: one line, or a no-echo prompt at a
        terminal. A machine script that joins every boot reads it from a
        file, group NAME - < /root/secret. Given on the line it still works,
        and the tight tier refuses it.

        With a secret, this machine is in the group from now on and across
        boots, install and wipe; the secret itself is never kept, only what
        PBKDF2 makes of it. Without one, the group already joined is joined
        again, or a new group gets a secret of 160 random bits, printed once
        for the other machines.
        Members get the grants named here, and none when none are. A grant
        the group stops giving is taken from the machines it paired, at
        once: what a member got from the group it does not keep after the
        group no longer gives it. A grant the group starts giving is not
        handed to the machines already paired, which are each given more by
        name, as before. A join does not switch the listener on: `link on`
        does, and says the listener is off when it is.
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
        p8 typed[sizeof made + 1];
        p8 generated = false;
        p8 granted = false;
        p8 good;
        p32 may = 0;
        p32 taken = 0;
        p32 mark = 0;
        positive narrowed = 0;
        positive at = 1;
        p8 grants[96];
        bipolar lock;
        p8 fresh[32];
        p8 derived;

        scope_exit(crypto_forget(typed, sizeof typed));
        scope_exit(crypto_forget(fresh, sizeof fresh));

        if (!link_name_good(namespace) ||
            string_length(namespace) >= WATERLINK_NAMESPACE_MAX)
                return link_refuse_name(namespace, "group name");
        if (at < count && !string_equals(words[at], "allow"))
                secret = words[at++];
        if (secret && string_equals(secret, "-"))
        {
                //      One line, which the length check below judges; the
                //      end of input with nothing before it is no answer.
                return_if(radio_password_read(typed, sizeof typed, namespace,
                                              false, true) < 0,
                          host_refuse("nothing saved\n"));
                secret = (string_address)typed;
        }
        else
                return_if(secret && MOONWATER_STRICT_NET >= STRICT_TIGHT,
                          host_refuse("a secret on the command line is "
                                      "refused: give it on standard input "
                                      "with -\n"));
        if (at < count)
        {
                return_if(!string_equals(words[at], "allow") || at + 1 == count, host_usage());
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

        /*      The slow part of a secret that was given is made here, before the
                peers lock: the listener takes that lock for every greeting it
                hears, so a derivation made under it stood the listener still for
                as long as it took. A secret made below, for a new group with none
                given, is derived under the lock, once. */
        derived = secret != null;
        if (derived)
                waterlink_group_derive(namespace, (p8 address_to)secret,
                                       string_length(secret),
                                       WATERLINK_GROUP_ROUNDS, fresh);
        {
                scope_exit(crypto_forget(made, sizeof made);
                           crypto_forget(address_of groups, sizeof groups));
                lock = link_peers_lock();
                if (lock < 0)
                        return host_refuse("%s could not be locked\n", LINK_PEERS_LOCK);
                if (!link_groups_for_change(address_of groups))
                {
                        link_peers_unlock(lock);
                        return host_refuse("%s could not be read\n", LINK_GROUPS_PATH);
                }
                for (positive look = 0; look < groups.count; look++)
                        if (string_equals(groups.record[look].namespace, namespace) &&
                            !(groups.record[look].flags & LINK_GROUP_ONCE))
                                record = groups.record + look;

                if (!secret && !record)
                {
                        p8 random[20];

                        scope_exit(crypto_forget(random, sizeof random));
                        if (system_random_fill(random, sizeof random, 0) < 0)
                        {
                                link_peers_unlock(lock);
                                return host_fail("randomness", -EIO);
                        }
                        //      160 bits as 32 characters of base32.
                        memory_encode_power2(made, random, 4,
                                             (string_address)link_base32, 5);
                        made[32] = 0;
                        secret = (string_address)made;
                        generated = true;
                }

                /*      The machines this group paired carry the mark of the
                        key they were paired under, which is the record's
                        before a new secret derives another. Mark 0 is a
                        machine paired by hand, never a member. */
                if (record)
                {
                        struct waterlink_group_keys keys;

                        waterlink_group_keys_from(address_of keys, record->key,
                                                  namespace);
                        mark = keys.mark;
                        crypto_forget(address_of keys, sizeof keys);
                }

                if (!record)
                {
                        if (groups.count >= LINK_GROUPS_MAX)
                        {
                                link_peers_unlock(lock);
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
                        //      that joins at every boot says `link group NAMESPACE`
                        //      and keeps the group it has.
                        if (derived)
                                memory_copy(record->key, fresh, sizeof fresh);
                        else
                                waterlink_group_derive(namespace, (p8 address_to)secret,
                                                       string_length(secret),
                                                       WATERLINK_GROUP_ROUNDS, record->key);
                        memory_zero(record->check, sizeof record->check);
                }
                if (granted)
                {
                        taken = record->may & ~may;
                        record->may = may;
                }

                /*      What the group stops giving, its members lose, and
                        they lose it first: a failure between the two files
                        leaves less granted than was asked, never more. A
                        peers file that cannot be read or written changes
                        neither, as a groups file that cannot be does. */
                if (taken && mark)
                {
                        link_peers peers;

                        if (!link_peers_for_change(address_of peers))
                        {
                                link_peers_unlock(lock);
                                return host_refuse("%s could not be read\n",
                                                   LINK_PEERS_PATH);
                        }
                        for (positive look = 0; look < peers.count; look++)
                                if (peers.peer[look].group == mark &&
                                    (peers.peer[look].may & taken))
                                {
                                        peers.peer[look].may &= ~taken;
                                        narrowed++;
                                }
                        if (narrowed && link_peers_save(address_of peers) < 0)
                        {
                                link_peers_unlock(lock);
                                return host_refuse("%s could not be written\n",
                                                   LINK_PEERS_PATH);
                        }
                }

                if (link_groups_save(address_of groups) < 0)
                {
                        link_peers_unlock(lock);
                        return host_refuse("%s could not be written\n",
                                           LINK_GROUPS_PATH);
                }
                link_peers_unlock(lock);

                link_grants_text(record->may, grants, sizeof grants);
                string_format(log, host_label "in group %s; its members may %s here\n",
                              namespace, (string_address)grants);
                if (narrowed)
                {
                        link_grants_text(taken, grants, sizeof grants);
                        string_format(log, host_label "the %p machines it paired "
                                                      "may no longer %s\n",
                                      narrowed, (string_address)grants);
                }
                if (generated)
                        string_format(log,
                                      host_label "the secret is %s -- on each other "
                                                 "machine, with it on standard "
                                                 "input: moonwater link group "
                                                 "%s -\n",
                                      (string_address)made, namespace);
                else if (secret && string_length(secret) < 20)
                        host_say(log, host_label "a secret this short can be "
                                                 "guessed offline by anyone on "
                                                 "the network; leave it out and "
                                                 "one is made\n");
        }

        //      A join does not switch the listener on: that is `link on`, a
        //      person's word, and the listener reads the groups it runs with
        //      from the file, so a join made while it runs takes at once.
        if (!link_switch_is_on())
                host_say(log, host_label "the listener is off: moonwater link on starts it\n");
        return 0;
}

// group leave NAMESPACE [forget]: stop announcing it, and maybe its machines too.
static b32 link_group_leave(string_address namespace, p8 forget)
{
        link_groups groups;
        struct waterlink_group_keys keys;
        p8 found = false;
        p8 saved;
        bipolar lock = link_peers_lock();

        if (lock < 0)
                return host_refuse("%s could not be locked\n", LINK_PEERS_LOCK);
        if (!link_groups_for_change(address_of groups))
        {
                link_peers_unlock(lock);
                return host_refuse("%s could not be read\n", LINK_GROUPS_PATH);
        }
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
                p8 read = held >= 0 && link_peers_for_change(address_of peers);
                p8 wrote = false;

                for (positive at = 0; read && at < peers.count; at++)
                        if (peers.peer[at].group == keys.mark)
                        {
                                peers.peer[at--] = peers.peer[--peers.count];
                                dropped++;
                        }
                if (read)
                        wrote = link_peers_save(address_of peers) >= 0;
                link_peers_unlock(held);
                //      The group is gone from its file already; a peers file
                //      that did not take the forget is said, not reported as done.
                if (read && !wrote)
                {
                        crypto_forget(address_of keys, sizeof keys);
                        return host_refuse("left group %s, but %s could not be "
                                           "written: the machines it linked are "
                                           "kept\n",
                                           namespace, LINK_PEERS_PATH);
                }
                if (read)
                        host_say(log, host_label "left group %s and removed the "
                                                 "%p machines it linked\n",
                                 namespace, dropped);
                else
                        host_say(log, host_label "left group %s; %s could not "
                                                 "be locked or read, so the "
                                                 "machines it linked are "
                                                 "kept\n",
                                 namespace, LINK_PEERS_PATH);
        }
        else
                host_say(log, host_label "left group %s; the machines it "
                                         "linked are still known\n",
                         namespace);
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

static p8 link_code_make(p8 address_to code)
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
static p8 link_code_clean(string_address text, p8 address_to code)
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
        positive length = string_length_max(name, WATERLINK_NAME_MAX - 1);

        memory_copy(into, name, length);
        memory_to_lower_ascii(into, length);
        into[length] = 0;
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
        test sees a code run out. A longer wait is never taken.
        WATERLINK_PAIR_SECONDS is test-only: the link lane of
        test/differential.py sets it, and nothing on a machine does. */
static p64 link_pair_seconds(void)
{
        bipolar seconds = link_decimal(
            file_environment((string_address) "WATERLINK_PAIR_SECONDS"));

        return seconds > 0 && seconds < LINK_PAIR_SECONDS ? (p64)seconds
                                                          : LINK_PAIR_SECONDS;
}

/*      This machine's side of a code in the groups file, under the lock the
        listener takes for it: a code for this namespace made before is
        replaced once it has run out, and while it still waits the new one is
        refused with LINK_PAIR_WAITING, and made says when that one runs out.
        Answers 0, or why not: -ENOSPC when the file holds its eight groups,
        -ENODATA when it is there and cannot be read, which leaves it as it
        is, and -EIO when it cannot be written. */
#define LINK_PAIR_WAITING 1

typedef struct
{
        p32 mark;
        p64 expires; // seconds since boot
} link_pair_made;

static p8 link_pair_waiting(struct link_group_record address_to record,
                            string_address namespace, p64 now)
{
        return (record->flags & LINK_GROUP_ONCE) &&
               !(record->flags & LINK_GROUP_DONE) &&
               record->expires > now &&
               string_equals(record->namespace, namespace);
}

static bipolar link_pair_open(string_address namespace, p8 address_to code,
                              string_address expect, link_pair_made address_to made)
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
        scope_exit(crypto_forget(key, sizeof key);
                   crypto_forget(address_of groups, sizeof groups));

        lock = link_peers_lock();
        if (lock < 0)
                return lock;
        scope_exit(link_peers_unlock(lock));
        if (!link_groups_for_change(address_of groups))
                return -ENODATA;
        for (positive at = 0; at < groups.count; at++)
                if (link_pair_waiting(groups.record + at, namespace, now))
                {
                        made->expires = groups.record[at].expires;
                        return LINK_PAIR_WAITING;
                }
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
                made->mark = keys.mark;
                made->expires = record->expires;
                crypto_forget(address_of keys, sizeof keys);
        }
        return answer;
}

//      The code taken back, when nobody used it.
static fn link_pair_close(p32 mark)
{
        link_groups groups;
        struct waterlink_group_keys keys;
        bipolar lock = link_peers_lock();

        //      Without the lock, or with a file that cannot be read, the file
        //      is left as it is: the code runs out by itself, and the
        //      listener drops it then.
        if (lock < 0)
                return;
        if (link_groups_for_change(address_of groups))
        {
                for (positive at = 0; at < groups.count; at++)
                {
                        waterlink_group_keys_from(address_of keys,
                                                  groups.record[at].key,
                                                  groups.record[at].namespace);
                        if ((groups.record[at].flags & LINK_GROUP_ONCE) &&
                            keys.mark == mark)
                                groups.record[at--] = groups.record[--groups.count];
                        crypto_forget(address_of keys, sizeof keys);
                }
                (void)link_groups_save(address_of groups);
        }
        link_peers_unlock(lock);
        crypto_forget(address_of groups, sizeof groups);
}

/*      The listener as it was before a code turned it on: whether it ran,
        and what the switch said, if anything. A code nobody used leaves it so,
        and not on for good. */
typedef struct
{
        p8 running;
        p8 word[16];
} link_before;

static fn link_before_take(link_before address_to before)
{
        before->word[0] = 0;
        (void)host_read_word(LINK_SWITCH_PATH, before->word, sizeof before->word);
        before->running = host_lock_owner(LINK_LOCK_PATH) > 0;
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

/*      A pair that is killed (SIGKILL, the OOM killer) cannot take its code
        back or put the listener as it was: the switch stayed on for good and
        the code lasted its five minutes with nobody waiting for it. So a
        process of its own, in a session of its own, holds the read end of a
        pipe the waiting command holds the write end of; the command writes a
        byte when it has done what it does, and an end without one is a
        command that did not, which the process then does for it. */
static bipolar link_pair_leash = -1;

static fn link_pair_guard(p32 mark, link_before address_to before)
{
        b32 ends[2];
        p8 done;

#if defined(MACOS)
        if (pipe(ends) < 0)
                return;
        (void)fcntl(ends[0], F_SETFD, FD_CLOEXEC);
        (void)fcntl(ends[1], F_SETFD, FD_CLOEXEC);
#else
        if (system_pipe(ends, O_CLOEXEC) < 0)
                return;
#endif
        if (host_detach((p32)ends[0], descriptors_none))
        {
                if (system_read_retry((positive)ends[0], address_of done, 1) != 1)
                {
                        link_pair_close(mark);
                        link_before_restore(before);
                }
#if defined(MACOS)
                _exit(0);
#else
                system_call_1(syscall(exit_group), 0);
#endif
        }
        system_close((bipolar)ends[0]);
        link_pair_leash = (bipolar)ends[1];
}

static fn link_pair_release(void)
{
        p8 done = 1;

        if (link_pair_leash < 0)
                return;
        system_write_all((positive)link_pair_leash, address_of done, 1);
        system_close(link_pair_leash);
        link_pair_leash = -1;
}

//      What the groups file says of this namespace's codes: closed when one was
//      used and is kept its half minute, and live while one still waits. A
//      file that cannot be read says nothing, so it is taken as live.
static fn link_pair_look(string_address namespace, p8 address_to closed,
                         p8 address_to live)
{
        link_groups groups;

        address_to closed = false;
        address_to live = false;
        if (!link_groups_for_change(address_of groups))
                address_to live = true;
        else
                for (positive at = 0; at < groups.count; at++)
                        if ((groups.record[at].flags & LINK_GROUP_ONCE) &&
                            string_equals(groups.record[at].namespace, namespace))
                        {
                                if (groups.record[at].flags & LINK_GROUP_DONE)
                                        address_to closed = true;
                                else
                                        address_to live = true;
                        }
        crypto_forget(address_of groups, sizeof groups);
}

/*      The countdown on a terminal, once a second: the line goes back to its
        start and is written over, so the wait is one line that counts down. */
static fn link_pair_show(p64 left)
{
        string_format(log, "\r" host_label "waiting for the other machine: %p "
                           "seconds left   ", (positive)left);
        log_flush();
}

/*      Until a machine carrying the mark is among the ones this machine
        knows, the code has run out, or somebody stops it. The listener does
        the pairing and writes the file this reads, a few times a second.
        The listener closes the code after it has written the machine, so the
        code is read first and the machines after: a closed code with no
        machine behind it is one used by a machine that was already linked,
        which keeps what it had.

        The wait ends in one of three ways, and says which: linked (0),
        expired (the code's five minutes ran out with nobody using it), or no
        answer (stopped, cancelled with link cancel, or used by a machine that
        was already linked). A code that a cancel removed is seen gone within a
        quarter of a second; one that runs out is removed by the listener. */
static b32 link_pair_wait(link_pair_made address_to made, string_address namespace,
                          link_before address_to before)
{
        bipolar signals = link_signals_open(false);
        b32 stopped = 0;
        p8 closed = false;
        p8 live = true;
        p8 tty = stream_is_terminal(1);
        p64 shown = 0;

        scope_exit(if (signals >= 0) link_signals_close(signals));
        scope_exit(link_pair_release());

        for (;;)
        {
                link_peers peers;
                timespec limit = {0, 250000000};
                p64 now = link_boot_seconds();

                link_pair_look(namespace, address_of closed, address_of live);
                link_peers_load(address_of peers);
                for (positive at = 0; at < peers.count; at++)
                        if (peers.peer[at].group == made->mark)
                        {
                                if (tty && shown)
                                        host_say(log, "\n");
                                host_say(log, host_label "linked with %s: it and "
                                                         "this machine may each "
                                                         "use a terminal, "
                                                         "commands, files and "
                                                         "the log of the other\n",
                                         (string_address)peers.peer[at].name);
                                return 0;
                        }
                if (closed || !live || now >= made->expires)
                        break;
                if (tty && now != shown)
                {
                        link_pair_show(made->expires - now);
                        shown = now;
                }
                if (signals < 0)
                        host_pause(250000000);
                else
                {
                        (void)link_wait_readable(signals, address_of limit);
                        link_signals_take(signals, address_of stopped);
                        if (stopped)
                                break;
                }
        }

        if (tty && shown)
                host_say(log, "\n");
        link_before_restore(before);
        if (closed)
                return host_refuse("no answer: a machine used the code, but it is "
                                   "already linked, and keeps what it may do: "
                                   "moonwater link shows it\n");
        link_pair_close(made->mark);
        if (stopped)
                return host_refuse("no answer: stopped, and the code no longer "
                                   "works: moonwater link pair makes another\n");
        if (!live && link_boot_seconds() < made->expires)
                return host_refuse("no answer: moonwater link cancel ended the wait, "
                                   "and the code no longer works\n");
        return host_refuse("expired: nobody used the code in %p seconds, so the "
                           "machines were not linked. Both need to be on one "
                           "network, and the code typed as it was printed; "
                           "moonwater link doctor says what this machine can "
                           "reach\n",
                           (positive)link_pair_seconds());
}

//      The code in the groups file and the listener on, or why not.
static b32 link_pair_start(string_address namespace, p8 address_to code,
                           string_address expect, link_pair_made address_to made,
                           link_before address_to before)
{
        bipolar opened = link_pair_open(namespace, code, expect, made);

        if (opened == LINK_PAIR_WAITING)
        {
                p64 now = link_boot_seconds();
                p64 left = made->expires > now ? made->expires - now : 0;

                return host_refuse("a code for %s is already waiting, %p seconds "
                                   "left: moonwater link cancel ends that wait, "
                                   "then pair again\n",
                                   namespace, (positive)left);
        }
        if (opened == -ENOSPC)
                return host_refuse("this machine is in 8 groups already\n");
        if (opened == -ENODATA)
                return host_refuse("%s could not be read\n", LINK_GROUPS_PATH);
        if (opened < 0)
                return host_refuse("%s could not be written\n", LINK_GROUPS_PATH);
        link_before_take(before);
        if (!link_switch(true, false))
        {
                link_pair_guard(made->mark, before);
                return 0;
        }
        link_pair_close(made->mark);
        link_before_restore(before);
        return 1;
}

//      The symbols a code is typed as: all but the hyphens and spaces that may
//      be put among them, which are not counted.
static positive link_code_symbols(string_address text)
{
        positive used = 0;

        for (positive at = 0; text[at]; at++)
                if (text[at] != '-' && text[at] != ' ')
                        used++;
        return used;
}

/*      Whether this machine has a route to the discovery group. A datagram
        socket connected to it sends nothing: the connect fails only when the
        kernel has no route, which is the answer. */
static p8 link_has_route(void)
{
        socket_address_internet to = {
            .family = AF_INET, .port = network_order_16(WATERLINK_MDNS_PORT),
            .host = network_order_32(WATERLINK_MDNS_GROUP)};
        bipolar handle = link_datagram_socket(AF_INET);
        p8 routed;

        if (handle < 0)
                return false;
#if defined(MACOS)
        to.length = sizeof to;
#endif
        routed = socket_connect((b32)handle, address_of to, sizeof to) >= 0;
        socket_close((b32)handle);
        return routed;
}

static b32 link_pair_here(string_address expect)
{
        p8 name[WATERLINK_NAME_MAX];
        p8 code[LINK_CODE_LENGTH + 1];
        p8 said[8];
        b32 refused;
        p8 only[WATERLINK_NAME_MAX];
        link_pair_made made;
        link_before before;

        //      Names are lowercase on both machines. One too long for a name
        //      is refused, and not cut to its first 31 characters and paired
        //      as those: the code would then admit the machine with that
        //      shorter name, which is not the one typed.
        if (expect && string_length(expect) >= WATERLINK_NAME_MAX)
                return host_refuse("%s is not a machine name\n", expect);
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
        return_if(!link_code_make(code), host_fail("randomness", -EIO));

        refused = link_pair_start((string_address)name, code, expect,
                                  address_of made, address_of before);
        if (refused)
        {
                crypto_forget(code, sizeof code);
                return refused;
        }

        //      The name the other machine is given is this machine's own, in
        //      full, so it is typed as it is printed here.
        link_code_text(code, said);
        host_say(log, host_label "code %s; this machine is called %s\n",
                 (string_address)said, (string_address)name);
        if (expect)
                host_say(log, host_label "only the machine called %s may use it\n",
                         (string_address)expect);
        host_say(log, host_label "on the other machine, run: moonwater link %s %s\n",
                 (string_address)name, (string_address)said);
        host_say(log, host_label "waiting five minutes for it; the code runs out "
                                 "after %p seconds, and moonwater link cancel ends "
                                 "the wait\n",
                 (positive)link_pair_seconds());
        crypto_forget(code, sizeof code);
        return link_pair_wait(address_of made, (string_address)name,
                              address_of before);
}

static b32 link_pair_there(string_address typed_name, string_address typed)
{
        p8 code[LINK_CODE_LENGTH + 1];
        p8 name[WATERLINK_NAME_MAX];
        p8 own[WATERLINK_NAME_MAX];
        p8 transport[16];
        positive symbols = link_code_symbols(typed);
        link_pair_made made;
        b32 refused;
        link_before before;

        //      The name, the code and the route are judged here, before
        //      anything goes on the air: a refusal that comes after the wait
        //      says nothing of what was wrong with them.
        if (string_length(typed_name) >= WATERLINK_NAME_MAX)
                return host_refuse("unknown name: %s is not a machine name; "
                                   "letters, digits, . - _, up to 31\n", typed_name);
        link_fold_name(typed_name, name);
        if (!link_name_good((string_address)name))
                return host_refuse("unknown name: %s is not a machine name; "
                                   "letters, digits, . - _, up to 31, and not one "
                                   "of the words link uses\n", typed_name);
        link_machine_name(own);
        if (string_equals((string_address)own, (string_address)name))
                return host_refuse("%s is this machine's own name\n",
                                   (string_address)name);
        if (symbols != LINK_CODE_LENGTH)
                return host_refuse("wrong code length: %s has %p symbols, and a code "
                                   "has %p, as moonwater link pair prints it: "
                                   "abc-def\n",
                                   typed, (positive)symbols,
                                   (positive)LINK_CODE_LENGTH);
        if (!link_code_clean(typed, code))
                return host_refuse("code not accepted: %s has a character that no "
                                   "code uses; a code is letters and digits, and "
                                   "no u. Copy it again from moonwater link pair\n",
                                   typed);
        host_read_word(LINK_TRANSPORT_PATH, transport, sizeof transport);
        if (!string_equals((string_address)transport, "modem") && !link_has_route())
                return host_refuse("peer not reachable: this machine has no route to "
                                   "a network, so %s cannot be reached from here; "
                                   "moonwater link doctor says why\n",
                                   (string_address)name);

        refused = link_pair_start((string_address)name, code, null,
                                  address_of made, address_of before);
        crypto_forget(code, sizeof code);
        if (refused)
                return refused;

        host_say(log, host_label "looking for %s on this network, for five "
                                 "minutes at most; the code runs out after %p "
                                 "seconds\n",
                 (string_address)name, (positive)link_pair_seconds());
        return link_pair_wait(address_of made, (string_address)name,
                              address_of before);
}

/*      The codes this machine has waiting, cleared: a pairing that is waiting
        ends with no answer, and the switch goes back as it was, which the
        waiting command does when it sees its code gone. A code that was taken
        keeps its half minute, as the listener keeps it. */
static b32 link_cancel(void)
{
        link_groups groups;
        positive cleared = 0;
        bipolar lock = link_peers_lock();

        if (lock < 0)
                return host_refuse("%s could not be locked\n", LINK_PEERS_LOCK);
        if (!link_groups_for_change(address_of groups))
        {
                link_peers_unlock(lock);
                return host_refuse("%s could not be read\n", LINK_GROUPS_PATH);
        }
        for (positive at = 0; at < groups.count;)
                if ((groups.record[at].flags & LINK_GROUP_ONCE) &&
                    !(groups.record[at].flags & LINK_GROUP_DONE))
                {
                        groups.record[at] = groups.record[--groups.count];
                        cleared++;
                }
                else
                        at++;
        if (cleared && link_groups_save(address_of groups) < 0)
        {
                link_peers_unlock(lock);
                crypto_forget(address_of groups, sizeof groups);
                return host_refuse("%s could not be written\n", LINK_GROUPS_PATH);
        }
        link_peers_unlock(lock);
        crypto_forget(address_of groups, sizeof groups);
        if (!cleared)
                host_say(log, host_label "no code is waiting\n");
        else
                host_say(log, host_label "%p code%s cleared: a wait for one ends with "
                                         "no answer\n",
                         (positive)cleared, cleared == 1 ? "" : "s");
        return 0;
}

// Whether a name is a machine this one is linked with.
static p8 link_known(string_address name)
{
        link_peers peers;

        link_peers_load(address_of peers);
        return link_peer_named(address_of peers, name) != null;
}

/*      link doctor's checks: read only. The listener's state and the port
        are read; the addresses come from the same walk the network setup
        uses; the route is a connect that sends nothing; discovery is one
        multicast join on a socket closed again. No pairing request is sent. */
static p8 link_doctor_list[96];
static positive link_doctor_used;

static fn link_doctor_add(p32 host)
{
        if ((host >> 24) == 127 || link_doctor_used + 20 >= sizeof link_doctor_list)
                return;
        if (link_doctor_used)
                link_doctor_list[link_doctor_used++] = ' ';
        link_doctor_used += host_into(link_doctor_list + link_doctor_used, host);
        link_doctor_list[link_doctor_used] = 0;
}

#if !defined(MACOS)
/* The netlink walk's callback: one IPv4 address of the dump. */
static p8 link_doctor_address(netlink_header address_to header,
                              address_any context)
{
        netlink_address address_to body =
            netlink_message_body(header, sizeof(netlink_address));
        positive size = 0;
        p8 address_to host;

        (void)context;
        if (header->type != RTM_NEWADDR || !body)
                return true;
        host = netlink_find(header, sizeof(netlink_address), IFA_LOCAL,
                            address_of size);
        if (body->family != AF_INET || !host || size < 4)
                return true;
        link_doctor_add(network_load_32(host));
        return true;
}

static fn link_doctor_walk(void)
{
        bipolar handle = netlink_open_groups(0);

        if (handle < 0)
                return;
        (void)netlink_dump((b32)handle, RTM_GETADDR, sizeof(netlink_address),
                           AF_INET, link_doctor_address, null);
        system_close((b32)handle);
}
#else
static fn link_doctor_walk(void)
{
        struct ifaddrs *list = null;

        if (getifaddrs(&list) < 0)
                return;
        for (struct ifaddrs *entry = list; entry; entry = entry->ifa_next)
        {
                p8 address_to raw;

                if (!entry->ifa_addr || entry->ifa_addr->sa_family != AF_INET)
                        continue;
                //      A sockaddr_in is sin_len, sin_family, sin_port, then the
                //      address in network order at offset 4.
                raw = (p8 address_to)(void address_to)entry->ifa_addr;
                link_doctor_add(network_load_32(raw + 4));
        }
        freeifaddrs(list);
}
#endif

/*      Whether a multicast join takes on this machine: the join discovery
        makes, on a socket that is closed straight after. */
static b32 link_doctor_multicast(void)
{
#if defined(MACOS)
        link_ip_mreq join = {network_order_32(WATERLINK_MDNS_GROUP), 0};
#else
        link_mreqn join = {network_order_32(WATERLINK_MDNS_GROUP), 0, 0};
#endif
        bipolar handle = link_datagram_socket(AF_INET);
        bipolar joined;

        if (handle < 0)
                return false;
        joined = link_option_set((b32)handle, IPPROTO_IP, IP_ADD_MEMBERSHIP,
                                 address_of join, sizeof join);
        socket_close((b32)handle);
        return joined >= 0 || joined == -EADDRINUSE;
}

/*      Whether the port is free to take: a socket bound to it, closed again. */
static b32 link_doctor_port_free(void)
{
        bipolar handle = link_socket_open(link_port(), true);

        if (handle < 0)
                return false;
        socket_close((b32)handle);
        return true;
}

static b32 link_doctor(void)
{
        p8 word[16];
        bipolar owner = host_lock_owner(LINK_LOCK_PATH);
        b32 on;
        positive failed = 0;
        positive port = (positive)link_port();

        host_read_word(LINK_SWITCH_PATH, word, sizeof word);
        on = string_equals((string_address)word, "on");

        if (owner > 0)
                string_format(log, "  pass  listener: running\n");
        else if (on)
        {
                failed++;
                string_format(log, "  fail  listener: switched on, but not running\n"
                                   "        fix: moonwater link on, which says why it "
                                   "did not start\n");
        }
        else
        {
                failed++;
                string_format(log, "  fail  listener: off\n"
                                   "        fix: moonwater link on\n");
        }

        if (owner > 0)
                string_format(log, "  pass  udp %p: the listener has it\n", port);
        else if (link_doctor_port_free())
                string_format(log, "  pass  udp %p: free for the listener\n", port);
        else
        {
                failed++;
                string_format(log, "  fail  udp %p: another program has it\n"
                                   "        fix: moonwater link port N, for a N that "
                                   "is free, then moonwater link on\n",
                              port);
        }

        link_doctor_used = 0;
        link_doctor_list[0] = 0;
        link_doctor_walk();
        if (link_doctor_used)
                string_format(log, "  pass  addresses: %s\n",
                              (string_address)link_doctor_list);
        else
        {
                failed++;
                string_format(log, "  fail  addresses: none but loopback\n"
                                   "        fix: join a network (moonwater wifi, or the "
                                   "cable), then moonwater link doctor\n");
        }

        if (link_has_route())
                string_format(log, "  pass  route: this machine has a route to a network\n");
        else
        {
                failed++;
                string_format(log, "  fail  route: no route to a network\n"
                                   "        fix: join a network (moonwater wifi, or the "
                                   "cable), then moonwater link doctor\n");
        }

        if (link_doctor_multicast())
                string_format(log, "  pass  discovery: multicast works, so machines "
                                   "find each other with no address\n");
        else
        {
                failed++;
                string_format(log, "  fail  discovery: multicast is refused here\n"
                                   "        fix: moonwater link add NAME KEY HOST[:PORT], "
                                   "with the other machine's address; that needs no "
                                   "discovery\n");
        }

        if (failed)
                return host_refuse("%p of the five checks fail; each fix is above\n",
                                   (positive)failed);
        host_say(log, host_label "all five checks pass\n");
        return 0;
}

/*
        moonwater link port [N|auto]: the udp port the listener takes, and the
        one `link add NAME KEY HOST` means where HOST has no :PORT -- a fleet
        on one other port says it once and not at every add. Kept in
        /root/link.port, read when the listener starts: one that is running
        keeps the port it has until it is switched off and on. auto goes back
        to the default.
*/
static b32 link_port_verb(string_address word)
{
        p8 text[16];
        bipolar port;
        bipolar failed;
        p8 running = host_lock_owner(LINK_LOCK_PATH) > 0;
        positive before = (positive)link_port();

        if (word && !string_equals(word, "auto"))
        {
                port = link_decimal(word);
                if (port <= 0 || port > 65535)
                        return host_refuse("%s is not a port: 1 to 65535, or auto for %p\n",
                                           word, (positive)LINK_PORT);
                text[positive_into(text, (positive)port)] = end;
                failed = radio_write_word(LINK_PORT_PATH, (string_address)text);
                return_if(failed < 0, host_fail("link port", failed));
        }
        else if (word)
        {
                failed = system_remove_at(AT_FDCWD, LINK_PORT_PATH, 0);
                return_if(failed < 0 && failed != -ENOENT, host_fail("link port", failed));
        }

        port = (bipolar)link_port();
        string_format(log, host_label "link port %p%s", (positive)port,
                      (positive)port == LINK_PORT ? ", the default" : "");
        if (running && before != (positive)port)
                string_format(log, "; the listener running keeps udp %p until "
                                   "link off, then link on", before);
        host_say(log, "\n");
        return 0;
}

/*
        moonwater link transport [network|modem]: how linked machines reach
        each other, kept in /root/link.transport, network when nothing is kept.
        The audio modem is one of the transports: the sound card carries the
        link when it is ready, and a word that asks for it is refused while it
        is not, with the reason. The state is the one place that says so, so a
        row that shows it is never wrong.
*/
//      The forward declarations of the two things this part calls that are
//      made later in the one unit: the sound server's page, and the send
//      (the send section of src/waterlink/modem.c, after the sound server's client).
static b32 modem_send_file(string_address path);
static int audio_test_page(struct audio_state *copy);
static void link_modem_streams_say(void);

/*
        The reason the audio modem is not ready, in words, or null when it is.
        It is ready when the sound server has a sound card open for it to play
        on: its page is there, the output is a card and not a clock (a clock is
        where the sound goes to nothing), and the card has a device. A server
        that is not running leaves no page to read.
*/
static string_address link_modem_why(void)
{
        struct audio_state page;

        if (!audio_test_page(&page))
                return (string_address) "the sound server is not running";
        if (!(page.output_state & AUDIO_STATE_OUT_CARD))
                return (string_address) "no sound card is open on the sound server";
        if (page.device[0] == 0)
                return (string_address) "the sound card has no device";
        return null;
}

static p8 link_modem_ready(void)
{
        return !link_modem_why();
}

/*
        The transport is the modem when /root/link.transport says so and the
        modem is ready: what is said is what is used.
*/
static p8 link_modem_in_use(void)
{
        p8 kept[16];

        host_read_word(LINK_TRANSPORT_PATH, kept, sizeof kept);
        return string_equals((string_address)kept, "modem") && link_modem_ready();
}

/*
        The modem's key, made on first use and said in hex: the receiver is
        given the same, in a file of its own. It is root's, as the link key is.
*/
static b32 link_modem_key_verb(void)
{
        p8 key[32];
        p8 text[65];
        static const char digits[] = "0123456789abcdef";
        bipolar got = link_secret_at(LINK_MODEM_PATH, key, true);

        scope_exit(crypto_forget(key, sizeof key));
        if (got == -EPERM)
                return host_refuse("%s is not a modem key: it must be a 32-byte "
                                   "file only root can read\n", LINK_MODEM_PATH);
        return_if(got < 0, host_fail(LINK_MODEM_PATH, got));
        for (positive at = 0; at < 32; at++)
        {
                text[2 * at] = digits[((p8)key[at] >> 4) & 15];
                text[2 * at + 1] = digits[(p8)key[at] & 15];
        }
        text[64] = 0;
        host_say(log, "%s\n", (string_address)text);
        return 0;
}

static b32 link_modem_send(string_address path)
{
        string_address why = link_modem_why();

        if (why)
                return host_refuse("the audio modem is not ready: %s (moonwater "
                                   "audio says which)\n", why);
        return modem_send_file(path);
}

static b32 link_transport_verb(string_address word)
{
        p8 kept[16];
        bipolar failed;

        host_read_word(LINK_TRANSPORT_PATH, kept, sizeof kept);
        if (!word)
        {
                string_address why = link_modem_why();

                //      What is said is what is used: a file written by hand as
                //      modem, when the modem is not there, is network.
                host_say(log, host_label "transport %s\n",
                         link_modem_in_use() ? "modem" : "network");
                if (why)
                        host_say(log, host_label "audio modem not ready: %s\n", why);
                else
                {
                        host_say(log, host_label "audio modem ready\n");
                        if (link_modem_in_use())
                                link_modem_streams_say();
                }
                return 0;
        }
        if (string_equals(word, "modem"))
        {
                string_address why = link_modem_why();

                if (why)
                        return host_refuse("the audio modem is not ready: %s, so "
                                           "the transport stays network\n", why);
        }
        if (!string_equals(word, "network") && !string_equals(word, "modem"))
                return host_refuse("transport is network or modem, not %s\n", word);
        failed = radio_write_word(LINK_TRANSPORT_PATH, word);
        return_if(failed < 0, host_fail("link transport", failed));
        host_say(log, host_label "transport %s\n", word);
        return 0;
}

/*
        How many words each verb that takes a fixed number is run with, the
        two that name the program and the verb among them. A word of these
        with the wrong number is a usage page for anyone: root's refusal is
        for what would have been done, and a person who cannot do any of it
        is still told how it is spelled.
*/
static const struct
{
        string_address word;
        positive least;
        positive most;
} link_shapes[] = {
    {"on", 3, 3},   {"off", 3, 3},   {"key", 3, 3},  {"serve", 3, 4},
    {"port", 3, 4},  {"transport", 3, 4}, {"modem", 4, 5},
    {"pair", 3, 4}, {"add", 5, 6},   {"remove", 4, 4},
    {"allow", 5, ~(positive)0},      {"deny", 5, ~(positive)0},
    {"shell", 4, 4}, {"run", 5, ~(positive)0},
    {"push", 6, 6}, {"pull", 6, 6}, {"log", 4, 4},
    {"cancel", 3, 3}, {"doctor", 3, 3},
};

//      The code as it is printed, abc-def: a word of six letters is as likely
//      to be a command (whoami, reboot) as a code, and a misspelt machine
//      would otherwise start a five minute wait.
static p8 link_code_shaped(string_address word)
{
        p8 code[LINK_CODE_LENGTH + 1];

        return string_length(word) == LINK_CODE_LENGTH + 1 && word[3] == '-' &&
               link_code_clean(word, code);
}

static b32 link_dispatch(string_address address_to arguments, positive count)
{
        string_address verb = count > 2 ? arguments[2] : null;
        positive kind;
        positive shape = verb ? string_table_find(verb, link_shapes,
                                                  sizeof link_shapes[0],
                                                  array_count(link_shapes))
                              : array_count(link_shapes);

        if (shape < array_count(link_shapes) &&
            (count < link_shapes[shape].least || count > link_shapes[shape].most))
                return host_usage();
        //      serve's one further word is the descriptor link on hands the
        //      listener it starts, digits and nothing else; any other word
        //      started a listener that never came back to say it was wrong.
        if (shape < array_count(link_shapes) && count == 4 &&
            string_equals(verb, "serve"))
        {
                positive descriptor;

                if (!string_digits_checked_exact(arguments[3], 10,
                                                 address_of descriptor))
                        return host_usage();
        }

        //      A word that is no verb and no name a machine could have (help
        //      and leave are words of the groups, not verbs) is a machine of
        //      nobody's: what root would be told, anyone is.
        if (verb && shape == array_count(link_shapes) &&
            !string_equals(verb, "group") && !link_name_good(verb))
                return count == 4 && link_code_shaped(arguments[3])
                           ? host_refuse("%s is not a machine name\n", verb)
                           : host_usage();

        //      The key, the machines and the groups are root's files: for
        //      anyone else the page would say "nobody linked" of a machine
        //      that has a dozen.
        host_need_root();

        if (!verb)
                return link_status();

        if (string_equals(verb, "on") || string_equals(verb, "off"))
                return link_switch(string_equals(verb, "on"), true);
        if (string_equals(verb, "key"))
                return link_key_verb();
        if (string_equals(verb, "port"))
                return link_port_verb(count == 4 ? arguments[3] : null);
        if (string_equals(verb, "transport"))
                return link_transport_verb(count == 4 ? arguments[3] : null);
        if (string_equals(verb, "modem"))
        {
                if (count == 4 && string_equals(arguments[3], "key"))
                        return link_modem_key_verb();
                if (count == 5 && string_equals(arguments[3], "send"))
                        return link_modem_send(arguments[4]);
                return host_usage();
        }
        if (string_equals(verb, "serve"))
                return link_serve(count == 4 ? arguments[3] : null);
        if (string_equals(verb, "pair"))
                return link_pair_here(count == 4 ? arguments[3] : null);
        if (string_equals(verb, "cancel"))
                return link_cancel();
        if (string_equals(verb, "doctor"))
                return link_doctor();

        //      The peers file has a second writer, the listener pairing
        //      members: each change made here happens under the same lock.
        if (string_equals(verb, "add") || string_equals(verb, "remove") ||
            string_equals(verb, "allow") || string_equals(verb, "deny"))
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
                //      leave is a word no group may be called, so a leave
                //      that is not spelled group leave NAME [forget] is a
                //      misspelt leave, and the page is what it is owed.
                if (string_equals(arguments[3], "leave"))
                        return count == 5 ||
                                       (count == 6 && string_equals(arguments[5], "forget"))
                                   ? link_group_leave(arguments[4], count == 6)
                                   : host_usage();
                if (count >= 4)
                        return link_group_join(arguments + 3, count - 3);
        }

        //      push, pull, log, shell and run: a machine and what the kind
        //      takes, which the shapes above held it to.
        kind = string_table_find(verb, link_kind_names, sizeof link_kind_names[0],
                                 array_count(link_kind_names));
        if (kind && kind < array_count(link_kind_names))
                return link_client_run(arguments[3], (p8)kind, arguments + 4,
                                       count - 4);

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
        //      A code after a name pairs, linked or not: a code is never a
        //      command, and the Settings pairing field sends NAME CODE for any
        //      name, so a linked name would otherwise run the code as a command.
        //      A word with a hyphen after a name that is not linked is a code
        //      that was meant: link_pair_there says what is wrong with it, and
        //      nothing goes on the air.
        if (count == 4 && (link_code_shaped(arguments[3]) ||
                           (!link_known(verb) && string_first_of(arguments[3], '-'))))
                return link_pair_there(verb, arguments[3]);
        if (link_known(verb))
                return link_client_run(verb,
                                       count == 3 ? LINK_KIND_SHELL : LINK_KIND_RUN,
                                       arguments + 3, count - 3);
        return host_refuse("no machine is called %s: moonwater link pair "
                           "on it, then moonwater link NAME CODE here\n",
                           verb);
}

/*
        The verbs that change what the machine keeps end what they began when
        the reader of their output has gone, as host_main's finishing verbs do:
        `link group lab SECRET | true` died of SIGPIPE at its first line, with
        the group not joined and the listener not started. The ones that carry
        a stream (a terminal, a command, push, pull, log, serve) keep the
        signal as they found it, since ending with the reader is what they
        are for.
*/
static b32 link_main(string_address address_to arguments, positive count)
{
        static const string_address changes[] = {"on",  "off",    "port", "pair", "add",
                                                 "remove", "allow", "deny", "group",
                                                 "cancel"};
        string_address verb = count > 2 ? arguments[2] : null;
        positive previous[4];
        p8 ignored = false;
        b32 answer;

        for (positive at = 0; verb && at < array_count(changes); at++)
                ignored |= string_equals(verb, changes[at]);
        if (ignored)
                ignored = system_signal_install(13, 1, 0, 0, previous);
        answer = link_dispatch(arguments, count);
        if (ignored)
        {
                log_flush();
                system_signal_action(13, previous, null, 8);
        }
        return answer;
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
                    host_lock_owner(LINK_LOCK_PATH) > 0)
                        (void)host_lock_signal(LINK_LOCK_PATH, 15);
                return;
        }

        owner = host_lock_owner(LINK_LOCK_PATH);

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
        link_serve_start(false, -1);
}

#endif // WATERLINK_COMMAND_INCLUDED
