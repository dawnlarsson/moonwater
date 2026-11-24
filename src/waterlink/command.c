/*
        `moonwater link`: the verbs.

                link                    who this machine is linked with and
                                        what each may do
                link on | off           the listener, kept across boots
                link on | off NAME      the machine called NAME switched on or
                                        off, kept with its grants either way
                link pair [NAME]        a code, and wait for a machine to use it
                link cancel             end every wait for a code
                link NAME CODE          link to the machine called NAME
                link transport [network|tcp|auto|modem]   how machines reach each other
                link modem key          the modem's key, made on first use, in hex
                link modem send FILE    FILE over the sound card, to the receiver
                link NAME [COMMAND...]  a terminal on NAME, or one command
                link push NAME FILE PATH   a file there, whole or not at all
                link pull NAME PATH FILE   and back
                link log NAME           follow the far kernel log
                link pad NAME [DEVICE]  this machine's game pad played on
                                        NAME, as if plugged in there (pad.c)
                link pad stop           and no more
                link pad                the pads sent from here and made here
                link add NAME KEY [HOST[:PORT]]   link by key, with no code
                link remove NAME
                link allow NAME GRANT...
                link deny NAME GRANT...
                link group ...          machines on one network that link
                                        themselves; new, key, add and stamp
                                        are the group a stick carries (below,
                                        before link_group_leave)
                link doctor             the listener, port, addresses, route and
                                        discovery: each pass or fail, and the fix;
                                        then how this machine is reached
                link paths              the ways in this machine has, and where
                                        each machine linked was last heard

        `key` and `serve` are not in the page: the key is in the status, and
        `serve` is how the listener starts itself.

        Linking is by key and both ways, as WireGuard's is: each machine is
        told the other's key, and a machine answers only a key it was told.
        A new machine may do everything a grant names until it is denied one
        by name: a code, a key added and a group give the two a terminal, a
        command, files, the log and input, in both directions.

        A machine is known by the name it gave when it was paired, and what
        the record is, is its key. Renamed (`moonwater name NEW`, or the
        hostname of a Mac), the machine says the new name in every session
        and every greeting it makes -- a listener within a second, a client at
        its start -- and the record that has its key takes it, with one line
        saying so, so that `link NEW` reaches it and `link OLD` no longer
        does. A name given with link add is the person's, and is left alone.
        A name another machine has here is not taken: the one that was renamed
        is kept under it with its key's suffix, as when it is paired.

        The grants: shell is a terminal, run is one command, log follows the
        kernel log, and files is push and pull of any file root has. files is
        root: it is as powerful as shell, since a file pushed over anything
        root runs is a command, and a person grants it knowing that. A few
        paths are left out of it (the link's own files, /root/rules,
        /lib/moonwater and /run/moonwater, link_path_is_own) as a guard
        against a push that lands somewhere by accident, not to keep a peer
        with files from root.

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

        for (string_address at = script;
             at && (at = string_search(at, "link group ")) != null;
             at += 11)
        {
                string_address word = at + 11;

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

static p8 link_has_route(void);

/*
        The listener's state, one of five, and what each is called and what
        answers it. The status line and link doctor's listener check both say
        these words, so the two never disagree about the one listener.
*/
enum
{
        LINK_LISTENER_ON,           // switched on, and running
        LINK_LISTENER_BY_HAND,      // running with no switch word: link serve
        LINK_LISTENER_STILL,        // switched off, still running
        LINK_LISTENER_NOT_RUNNING,  // switched on, and not running
        LINK_LISTENER_OFF,          // switched off, and not running
};

static const string_address link_listener_names[] = {
    "on, running", "on, running by hand", "off, still running",
    "on, not running", "off"};
static const string_address link_listener_next[] = {
    "moonwater link pair", "moonwater link on, to keep it across boots",
    "moonwater link off", "moonwater link on, which says why it did not start",
    "moonwater link on"};

static positive link_listener_state(bipolar owner, b32 on, b32 switched)
{
        if (owner > 0)
                return !switched ? LINK_LISTENER_BY_HAND
                       : on      ? LINK_LISTENER_ON
                                 : LINK_LISTENER_STILL;
        return on ? LINK_LISTENER_NOT_RUNNING : LINK_LISTENER_OFF;
}

static positive link_listener_status(bipolar owner)
{
        p8 word[16];

        host_read_word(LINK_SWITCH_PATH, word, sizeof word);
        return link_listener_state(owner, string_equals((string_address)word, "on"),
                                   word[0] != 0);
}

/*      The session the listener has open with this name, if one is open now:
        its entry, whose address is where it is heard from now. */
static link_open_entry address_to link_open_of(link_state address_to state,
                                               const p8 address_to key,
                                               string_address name)
{
        for (positive at = 0; at < state->open_count && at < LINK_SESSIONS; at++)
        {
                link_open_entry address_to open = state->open + at;

                if (key && crypto_same(open->key, (p8 address_to)key, 32))
                        return open;
        }
        for (positive at = 0; at < state->open_count && at < LINK_SESSIONS; at++)
        {
                link_open_entry address_to open = state->open + at;

                open->name[WATERLINK_NAME_MAX - 1] = 0;
                if (link_name_good((string_address)open->name) &&
                    string_equals((string_address)open->name, name))
                        return open;
        }
        return null;
}

/*      Whether any session the listener has open is one with a name a machine
        can have: such a session is a link up, whatever the machines file says. */
static b32 link_any_open(link_state address_to state)
{
        for (positive at = 0; at < state->open_count && at < LINK_SESSIONS; at++)
        {
                state->open[at].name[WATERLINK_NAME_MAX - 1] = 0;
                if (link_name_good((string_address)state->open[at].name))
                        return true;
        }
        return false;
}

/*      The listener's state file when the listener is running and the file is
        whole; false and zeroed otherwise. */
static p8 link_state_read(bipolar owner, link_state address_to state)
{
        positive state_length = 0;

        if (owner > 0 &&
            link_read_private_records(LINK_STATE_PATH, (p8 address_to)state,
                                      sizeof(address_to state),
                                      sizeof(address_to state),
                                      address_of state_length) >= 0 &&
            state_length == sizeof(address_to state))
                return true;
        memory_zero(state, sizeof(address_to state));
        return false;
}

static b32 link_status(void)
{
        struct waterlink_identity me;
        link_peers peers;
        link_groups groups;
        p32 marks[LINK_GROUPS_MAX] = {0};
        link_state state;
        bipolar owner = host_lock_owner(LINK_LOCK_PATH);
        p8 key[48];
        b32 routed = false;
        b32 asked = false;

        //      A listener that is off, or older than this, left no state. One
        //      that was killed left its state behind, and it says nothing of
        //      sessions or peers heard now: a clean stop removes the file.
        (void)link_state_read(owner, address_of state);

        //      The first line is the listener, and the switch and the process
        //      are two things: its state is one of five, each with the command
        //      that answers it, and link doctor says the same words for it.
        {
                positive listener = link_listener_status(owner);

                string_format(log, "link %s, udp %p  next: %s\n",
                              link_listener_names[listener],
                              (positive)link_port(),
                              link_listener_next[listener]);
        }

        if (link_identity(address_of me, false) >= 0)
        {
                p8 own[WATERLINK_NAME_MAX];
                p8 hwid[LINK_HWID_BYTES];
                p8 hwid_hex[33];
                p8 allow_text[96];

                link_key_text(me.public, key);
                link_machine_name(own);
                link_grants_text(link_policy_load(), allow_text, sizeof allow_text);
                if (link_hardware_id(hwid))
                {
                        link_hwid_text(hwid, hwid_hex);
                        hwid_hex[16] = 0;
                        string_format(log, "  this machine  %s  %s  hwid %s  allow %s  next: moonwater link pair\n",
                                      (string_address)own, (string_address)key, (string_address)hwid_hex, (string_address)allow_text);
                }
                else
                        string_format(log, "  this machine  %s  %s  allow %s  next: moonwater link pair\n",
                                      (string_address)own, (string_address)key, (string_address)allow_text);
                crypto_forget(address_of me, sizeof me);
        }
        else
                string_format(log, "  this machine  no key yet  next: moonwater link on, "
                                   "which makes one\n");

        //      The groups: which, what members get, and whether the secret
        //      sits in the machine script, where /dev/spark shows it to any
        //      user on the machine.
        if (!link_groups_for_show(address_of groups))
                string_format(log, "  %s could not be read  next: ls -l %s\n",
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
                                p64 left = link_group_spent(groups.record + g, now)
                                                   ? 0
                                                   : groups.record[g].expires - now;

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
        //      nobody linked is said only of one that is empty, and of none
        //      that has a session open, which is a link up under whatever
        //      name it has (the open section below says which).
        if (!link_peers_for_change(address_of peers))
                string_format(log, "  %s could not be read  next: ls -l %s\n",
                              LINK_PEERS_PATH, LINK_PEERS_PATH);
        else if (!peers.count && !link_any_open(address_of state))
                string_format(log, "  nobody linked yet  next: moonwater link pair, "
                                   "and on the other machine moonwater link NAME CODE\n");

        for (positive at = 0; at < peers.count; at++)
        {
                struct waterlink_peer address_to peer = peers.peer + at;
                p8 grants[96];
                p8 place[64];
                p8 short_key[12];
                link_seen_entry address_to seen = null;
                link_open_entry address_to open_now = null;
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
                                        string_copy_bounded((string_address)in_group,
                                                            ", in group ",
                                                            sizeof in_group);
                                        string_append_bounded((string_address)in_group,
                                                              groups.record[g].namespace,
                                                              sizeof in_group);
                                        how = (string_address)in_group;
                                }
                        string_format(log, "  %s  %s...  may %s%s%s\n",
                                      (string_address)peer->name,
                                      (string_address)short_key,
                                      (string_address)grants, how,
                                      peer->flags & WATERLINK_PEER_OFF ? (string_address) ", off" : (string_address) "");
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
                        else if (peer->port &&
                                 link_place_same(state.seen[s_at].address,
                                                 state.seen[s_at].port,
                                                 peer->address, peer->port))
                                other = true;
                }

                //      Each line says how it was known: a session open now, a
                //      greeting heard since boot, or nothing heard, and why.
                open_now = link_open_of(address_of state, peer->key, (string_address)peer->name);
                if (peer->flags & WATERLINK_PEER_OFF)
                        string_format(log, "      switched off  next: moonwater link on %s\n",
                                      (string_address)peer->name);
                else if (open_now)
                {
                        link_place_text(open_now->address, open_now->port, place);
                        string_format(log, "      open now  \xe2\x86\x91%p \xe2\x86\x93%p pkts  rtt %p.%p ms  from %s  next: "
                                           "moonwater link %s%s%s\n",
                                      (positive)open_now->tx_packets,
                                      (positive)open_now->rx_packets,
                                      (positive)(open_now->rtt / 1000),
                                      (positive)(open_now->rtt % 1000 / 100),
                                      (string_address)place,
                                      !peer->may ? "allow " : "",
                                      (string_address)peer->name,
                                      !peer->may ? " run" : "");
                }
                else if (seen)
                {
                        p8 when[32];

                        discovery_ago(seen->seen, when);
                        link_place_text(seen->address, seen->port, place);
                        string_format(log, "      heard %s from %s  next: "
                                           "moonwater link %s%s%s\n",
                                      (string_address)when,
                                      (string_address)place,
                                      !peer->may ? "allow " : "",
                                      (string_address)peer->name,
                                      !peer->may ? " run" : "");
                        if (seen->changed)
                        {
                                p8 moved_when[32];
                                p8 was[64];

                                discovery_ago(seen->changed, moved_when);
                                link_place_text(seen->was, seen->was_port, was);
                                string_format(log, "      address changed %s, was %s\n",
                                              (string_address)moved_when,
                                              (string_address)was);
                        }
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
                        if (!asked)
                        {
                                routed = link_has_route() != 0;
                                asked = true;
                        }
                        link_place_text(peer->address, peer->port, place);
                        if (routed)
                                string_format(log, "      not heard since boot, last address "
                                                   "%s  next: moonwater link doctor\n",
                                              (string_address)place);
                        else
                                string_format(log, "      no route to a network, last address "
                                                   "%s  next: join a network (moonwater wifi, "
                                                   "or the cable), then moonwater link doctor\n",
                                              (string_address)place);
                }
                else
                        string_format(log, "      never answered: no address to ask it at  "
                                           "next: moonwater link add %s KEY HOST[:PORT]\n",
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
                string_format(log, "    %s  %s  %ps  \xe2\x86\x91%p \xe2\x86\x93%p pkts  rtt %p.%p ms  from %s\n",
                              (string_address)open->name,
                              link_kind_names[open->kind < array_count(link_kind_names)
                                                      ? open->kind
                                                      : 0],
                              (positive)open->seconds,
                              (positive)open->tx_packets,
                              (positive)open->rx_packets,
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

        string_copy_bounded(peer.name, name, sizeof peer.name);
        peer.may = WATERLINK_MAY_DEFAULT;
        //      The name is the person's, whatever the machine calls itself: it
        //      is what they type to reach it, and a rename on the far side does
        //      not take it from them (link_peer_follow).
        peer.flags = WATERLINK_PEER_LABELLED;

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
                        peer.flags |= found->flags & WATERLINK_PEER_SHUT;
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

        {
                p8 grants[96];

                link_grants_text(peer.may, grants, sizeof grants);
                host_say(log, host_label "added %s; it may %s\n", name,
                         (string_address)grants);
        }
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

/*
        A machine switched off or on by its person: off keeps it as it is (its
        key, its grants, its group, its places) and no link opens to it or from
        it (WATERLINK_PEER_OFF); what it has open now ends at the listener's
        next look at the peers, as a removed machine's does. On is the link it
        was, with nothing to pair or allow again.
*/
static b32 link_peer_switch_locked(string_address name, p8 on)
{
        link_peers peers;
        struct waterlink_peer address_to found;

        if (!link_peers_for_change(address_of peers))
                return host_refuse("%s could not be read\n", LINK_PEERS_PATH);
        found = link_peer_named(address_of peers, name);
        if (!found)
                return host_refuse("no machine is called %s\n", name);
        if (on == !(found->flags & WATERLINK_PEER_OFF))
        {
                host_say(log, host_label "%s is %s already\n", name, on ? "on" : "off");
                return 0;
        }
        if (on)
                found->flags &= (unsigned short)~WATERLINK_PEER_OFF;
        else
                found->flags |= WATERLINK_PEER_OFF;
        if (link_peers_save(address_of peers) < 0)
                return host_refuse("%s could not be written\n", LINK_PEERS_PATH);
        if (on)
                host_say(log, host_label "%s is on\n", name);
        else
                host_say(log, host_label "%s is off, and kept with its grants: moonwater link on %s\n", name, name);
        return 0;
}

static b32 link_purge_locked(void)
{
        link_peers peers;
        link_state state;
        positive kept = 0, removed = 0;
        bipolar owner = host_lock_owner(LINK_LOCK_PATH);

        if (!link_state_read(owner, address_of state))
                return host_refuse("the listener is not running; start it first with moonwater link on\n");
        if (!link_peers_for_change(address_of peers))
                return host_refuse("%s could not be read\n", LINK_PEERS_PATH);

        for (positive at = 0; at < peers.count; at++)
        {
                struct waterlink_peer address_to peer = peers.peer + at;

                peer->name[WATERLINK_NAME_MAX - 1] = 0;
                if ((peer->flags & WATERLINK_PEER_SHUT) ||
                    link_open_of(address_of state, peer->key, (string_address)peer->name) != null)
                        peers.peer[kept++] = *peer;
                else
                        removed++;
        }
        peers.count = kept;
        if (removed && link_peers_save(address_of peers) < 0)
                return host_refuse("%s could not be written\n", LINK_PEERS_PATH);

        if (!removed)
                host_say(log, host_label "nothing to purge\n");
        else
                host_say(log, host_label "purged %p unconnected machine%s\n",
                         removed, removed == 1 ? "" : "s");
        return 0;
}

static p8 link_grants_parse(string_address address_to words, positive count,
                            p32 address_to mask, string_address address_to bad)
{
        p32 may = 0;

        for (positive at = 0; at < count; at++)
        {
                p32 bit = link_grant_bit(words[at]);

                if (!bit)
                {
                        if (bad)
                                address_to bad = words[at];
                        return false;
                }
                may |= bit;
        }
        address_to mask = may;
        return true;
}

static b32 link_grant_locked(string_address name, p8 allow,
                             string_address address_to words, positive count)
{
        link_peers peers;
        struct waterlink_peer address_to found;
        p8 grants[96];
        p32 change = 0;
        string_address bad = null;

        if (!link_peers_for_change(address_of peers))
                return host_refuse("%s could not be read\n", LINK_PEERS_PATH);
        found = link_peer_named(address_of peers, name);
        if (!found)
                return host_refuse("no machine is called %s\n", name);

        if (!link_grants_parse(words, count, address_of change, address_of bad))
                return host_refuse("%s is not a grant: shell run files "
                                   "log input\n",
                                   bad);
        if (allow)
                found->may |= change;
        else
                found->may &= ~change;
        if (!allow && !found->may)
                found->flags |= WATERLINK_PEER_BLOCKED;
        else if (allow && found->may)
                found->flags &= (unsigned short)~WATERLINK_PEER_BLOCKED;
        link_hwid_set_block(found->key, (found->flags & WATERLINK_PEER_BLOCKED) != 0);

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

static struct link_group_record address_to link_group_append(
    link_groups address_to groups, string_address namespace)
{
        struct link_group_record address_to record =
                groups->record + groups->count++;

        memory_zero(record, sizeof(address_to record));
        string_copy_bounded(record->namespace, namespace,
                            sizeof record->namespace);
        return record;
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

static p8 link_group_name_good(string_address name);
static struct link_group_record address_to link_group_named(
    link_groups address_to groups, string_address namespace);

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

        if (!link_group_name_good(namespace))
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
                if (!link_grants_parse(words + at + 1, count - at - 1,
                                       address_of may, null))
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
                record = link_group_named(address_of groups, namespace);

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
                        record = link_group_append(address_of groups, namespace);
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
        else
                log_flush();
        return 0;
}

/*
        A group that sticks carry.

        `link group new` makes a group of a key of 256 random bits, and
        `link group add NAME:KEY` joins one by the key text `link group key`
        prints. The key is what a secret derives (link_group_join), so it is
        the secret in every way that matters, and a group joined by it is
        CARRIED: kept in the image's settings (src/moonwater/spark.c, the
        group list) as well as in the groups file. A machine that starts from
        an image that carries one is in the group before anything is typed
        (link_group_restore, from the restores of every start), with the
        switch on if nobody has said otherwise, so a stick that was written
        from the Mac's group answers the Mac, which is a member, and the
        machines of the group link themselves (nearby.c) as the machines of
        any group do. An install takes the image with its settings, so a
        disk is in the group as the stick was. A group joined with its secret
        (`link group NAME SECRET`) is not carried: it is this session's, as
        it was.

        What was chosen, and what it costs. The key is in the image as
        plainly as the image is: ANYBODY WHO HOLDS A STICK OF THE GROUP CAN
        JOIN THE GROUP, a copy of the file is a member, and so is a person
        who reads the stick. Nothing else keeps it: the settings are root's
        in the session (the kernel hands them to root only, and the session's
        copy is 0600), no command prints a key but `link group new` and
        `link group key`, and the groups file is root's and 0600. A machine
        whose image carries no group is exactly as it was. A group is left
        with `link group leave NAME` (the image forgets it too) and a lost
        stick is locked out with `link group new NAME`, which makes another
        key and forgets the machines the old one paired: sticks that carry
        the old key are in a group nobody else is in, until they are stamped
        again (`link group stamp IMAGE`). Members trust each other by the
        key and by nothing else, and the grants are the group's: every one
        for a group made or added here unless `allow` names fewer.

        Why the image and not a file on the stick, and why a key of the
        group and not the machine's own: the machine's own key is its
        identity, and an image copied to another stick or another disk would
        be two machines with one identity; a group's key is shared by design,
        so the copy is what it is for. The image is what an install and an
        update already carry, and the one place that holds root-only text
        the stick writes in place and reads back whole (host_settings_*), so
        nothing here writes the stick's file system.
*/
#define LINK_GROUP_MAY WATERLINK_MAY_ALL
#define LINK_GROUP_TEXT 96
#define LINK_GROUP_ENTRY_KEY 32
#define LINK_GROUP_ENTRY_NAME 36

static const struct
{
        string_address word;
        positive least, most;
} link_group_subcommands[] = {
    {"leave", 5, 6},
    {"new",   4, ~(positive)0},
    {"key",   4, 5},
    {"add",   5, ~(positive)0},
    {"stamp", 5, ~(positive)0},
};

//      The words `link group` takes where a group's name goes.
static p8 link_group_word(string_address name)
{
        return string_table_find(name, link_group_subcommands,
                                 sizeof link_group_subcommands[0],
                                 array_count(link_group_subcommands)) <
               array_count(link_group_subcommands);
}

//      Whether a name is one a group can be called.
static p8 link_group_name_good(string_address name)
{
        return link_name_good(name) && string_length(name) < WATERLINK_NAMESPACE_MAX &&
               !link_group_word(name);
}

//      A group's key as it is carried about: NAME:KEY, the key in base64 as
//      a machine's is, and no way to tell it from text.
static fn link_group_text(string_address namespace, p8 address_to key,
                          p8 address_to text)
{
        positive length = string_length(namespace);

        memory_copy(text, namespace, length);
        text[length++] = ':';
        link_key_text(key, text + length);
}

//      NAME:KEY as a name and a key; false for anything else, a name that is
//      no group's, and a key of nothing.
static p8 link_group_text_parse(string_address text, p8 address_to name,
                                p8 address_to key)
{
        positive length = string_span_of_set(text, LINK_NAME_CHARACTERS);

        if (!length || length >= WATERLINK_NAMESPACE_MAX || text[length] != ':')
                return false;
        memory_copy(name, text, length);
        name[length] = 0;
        return link_group_name_good((string_address)name) &&
               link_key_parse(text + length + 1, key) &&
               memory_span_byte(key, 0, 32) != 32;
}

//      The group of this name that is no code, or null.
static struct link_group_record address_to link_group_named(
    link_groups address_to groups, string_address namespace)
{
        for (positive at = 0; at < groups->count; at++)
                if (!(groups->record[at].flags & LINK_GROUP_ONCE) &&
                    string_equals(groups->record[at].namespace, namespace))
                        return groups->record + at;
        return null;
}

//      The mark the machines a group pairs carry, which is the name's alone.
static p32 link_group_mark(string_address namespace)
{
        struct waterlink_group_keys keys;
        p8 none[32];
        p32 mark;

        memory_zero(none, sizeof none);
        waterlink_group_keys_from(address_of keys, none, namespace);
        mark = keys.mark;
        crypto_forget(address_of keys, sizeof keys);
        return mark;
}

/*
        A group's key into the groups file, under the peers lock: the group
        of that name takes it, with these grants, or one is made. replaced
        says it was one already, whose key is not this one. 0, or a refusal.
*/
static b32 link_group_store(string_address namespace, p8 address_to key,
                            p32 may, p8 address_to replaced)
{
        link_groups groups;
        struct link_group_record address_to record;
        bipolar lock = link_peers_lock();

        address_to replaced = false;
        if (lock < 0)
                return host_refuse("%s could not be locked\n", LINK_PEERS_LOCK);
        if (!link_groups_for_change(address_of groups))
        {
                link_peers_unlock(lock);
                return host_refuse("%s could not be read\n", LINK_GROUPS_PATH);
        }
        record = link_group_named(address_of groups, namespace);
        if (record)
                address_to replaced = !crypto_same(record->key, key, 32);
        else if (groups.count >= LINK_GROUPS_MAX)
        {
                link_peers_unlock(lock);
                crypto_forget(address_of groups, sizeof groups);
                return host_refuse("this machine is in 8 groups already\n");
        }
        else
                record = link_group_append(address_of groups, namespace);
        memory_copy(record->key, key, 32);
        memory_zero(record->check, sizeof record->check);
        record->may = may;
        if (link_groups_save(address_of groups) < 0)
        {
                link_peers_unlock(lock);
                crypto_forget(address_of groups, sizeof groups);
                return host_refuse("%s could not be written\n", LINK_GROUPS_PATH);
        }
        link_peers_unlock(lock);
        crypto_forget(address_of groups, sizeof groups);
        return 0;
}

//      The machines a group paired, forgotten: count removed, -1 if peers
//      could not be locked or read, -2 if peers could not be written.
static bipolar link_group_forget_members(p32 mark)
{
        link_peers peers;
        bipolar lock = link_peers_lock();
        positive dropped = 0;

        if (lock < 0)
                return -1;
        if (!link_peers_for_change(address_of peers))
        {
                link_peers_unlock(lock);
                return -1;
        }
        for (positive at = 0; at < peers.count; at++)
                if (peers.peer[at].group == mark)
                {
                        peers.peer[at--] = peers.peer[--peers.count];
                        dropped++;
                }
        if (dropped && link_peers_save(address_of peers) < 0)
        {
                link_peers_unlock(lock);
                return -2;
        }
        link_peers_unlock(lock);
        return (bipolar)dropped;
}

#if defined(MACOS)
//      A Mac has no image to keep a group in: its groups file is all it has.
static fn link_group_carry(string_address namespace, p8 address_to key, p32 may)
{
        (void)namespace;
        (void)key;
        (void)may;
}

static fn link_group_uncarry(string_address namespace)
{
        (void)namespace;
}
#else
/*
        A group put in a block of settings: its entry replaced, or added in
        the first of the four places that is free. The entry is the key, the
        grants as four bytes little endian, and the name with no end.
        Null when it is in, else why not.
*/
static string_address link_group_entry_put(host_settings address_to settings,
                                           string_address namespace,
                                           p8 address_to key, p32 may)
{
        host_setting setting;
        host_setting found;
        positive at = 0;
        positive length = string_length(namespace);
        p8 text[SPARK_SETTINGS_GROUP_MOST + 1];
        p8 taken[SPARK_SETTINGS_GROUPS + 1];
        p16 id = 0;
        p8 have = false;

        scope_exit(crypto_forget(text, sizeof text));
        memory_zero(taken, sizeof taken);
        while (host_settings_next(settings, address_of at, address_of setting))
        {
                if (setting.entry.list != SPARK_SETTINGS_GROUP ||
                    setting.entry.length < SPARK_SETTINGS_GROUP_LEAST)
                        continue;
                if (setting.entry.id <= SPARK_SETTINGS_GROUPS)
                        taken[setting.entry.id] = true;
                if (setting.entry.length - LINK_GROUP_ENTRY_NAME == length &&
                    !memory_compare(setting.text + LINK_GROUP_ENTRY_NAME,
                                    namespace, length))
                {
                        found = setting;
                        have = true;
                        id = setting.entry.id;
                }
        }
        if (have)
                host_settings_drop(settings, address_of found);
        for (positive place = 1; !id && place <= SPARK_SETTINGS_GROUPS; place++)
                if (!taken[place])
                        id = (p16)place;
        if (!id)
                return "an image carries 4 groups at most";
        memory_copy(text, key, LINK_GROUP_ENTRY_KEY);
        for (positive byte = 0; byte < 4; byte++)
                text[LINK_GROUP_ENTRY_KEY + byte] = (p8)(may >> (8 * byte));
        memory_copy(text + LINK_GROUP_ENTRY_NAME, namespace, length);
        return host_settings_add(settings, SPARK_SETTINGS_GROUP,
                                 SPARK_SETTINGS_GROUP_KEY, (string_address)text,
                                 LINK_GROUP_ENTRY_NAME + length, address_of id);
}

//      An entry as a name, a key and grants; false for one that is no
//      group's (a name no group has is not one).
static p8 link_group_entry_read(host_setting address_to setting, p8 address_to name,
                                p8 address_to key, p32 address_to may)
{
        positive length;

        if (setting->entry.list != SPARK_SETTINGS_GROUP ||
            setting->entry.kind != SPARK_SETTINGS_GROUP_KEY ||
            setting->entry.length < SPARK_SETTINGS_GROUP_LEAST ||
            setting->entry.length > SPARK_SETTINGS_GROUP_MOST)
                return false;
        length = setting->entry.length - LINK_GROUP_ENTRY_NAME;
        memory_copy(key, setting->text, LINK_GROUP_ENTRY_KEY);
        address_to may = (p32)setting->text[32] | (p32)setting->text[33] << 8 |
                         (p32)setting->text[34] << 16 | (p32)setting->text[35] << 24;
        memory_copy(name, setting->text + LINK_GROUP_ENTRY_NAME, length);
        name[length] = 0;
        return link_group_name_good((string_address)name);
}

//      The group kept in this session's settings, and in the image they came
//      from where that can be written, which is said as every setting says it.
static fn link_group_carry(string_address namespace, p8 address_to key, p32 may)
{
        host_settings settings;
        string_address failed;

        if (!host_acquire())
                return;
        host_state_ready();
        host_settings_session(address_of settings);
        failed = link_group_entry_put(address_of settings, namespace, key, may);
        if (failed)
                host_settings_refused("group", failed, address_of settings);
        else
        {
                string_format(log, host_label "group %s is carried by this machine's "
                                              "image: it is in the group at every "
                                              "start; ",
                              namespace);
                (void)host_settings_save(address_of settings);
        }
        crypto_forget(address_of settings, sizeof settings);
        host_release();
}

//      The group taken out of the image's settings, when it was carried.
static fn link_group_uncarry(string_address namespace)
{
        host_settings settings;
        host_setting setting;
        positive at = 0;
        positive length = string_length(namespace);
        p8 have = false;

        //      The settings the session holds, and no look at the disks for
        //      others: a group that was carried is in them, and a machine that
        //      has none has nothing to take out.
        if (!host_acquire())
                return;
        host_state_ready();
        if (!host_settings_kept(address_of settings) &&
            !host_settings_booted(address_of settings))
        {
                host_release();
                return;
        }
        while (!have && host_settings_next(address_of settings, address_of at,
                                           address_of setting))
                have = setting.entry.list == SPARK_SETTINGS_GROUP &&
                       setting.entry.length >= SPARK_SETTINGS_GROUP_LEAST &&
                       setting.entry.length - LINK_GROUP_ENTRY_NAME == length &&
                       !memory_compare(setting.text + LINK_GROUP_ENTRY_NAME,
                                       namespace, length);
        if (have)
        {
                host_settings_drop(address_of settings, address_of setting);
                string_format(log, host_label "group %s is no longer carried by this "
                                              "machine's image; ",
                              namespace);
                (void)host_settings_save(address_of settings);
        }
        crypto_forget(address_of settings, sizeof settings);
        host_release();
}

/*
        The groups the image carries, joined: at every start of the machine
        process and after a disk is taken, so a live session has them from
        the first and an install has them on the disk's own /root. A group
        the file has with this key is left as it is, grants and all; with
        another key (it was made again where the image was stamped) it takes
        the image's, and its grants; one it has not is made. Whether any is
        carried, the switch is turned on only when it says nothing, so a
        person's `link off` stays, and the listener makes the machine's own
        key when it starts. A machine whose image carries none does nothing
        here and reads nothing of /root.
*/
static fn link_group_restore(void)
{
        host_settings settings;
        host_setting setting;
        link_groups groups;
        positive at = 0;
        p8 carried = false;
        p8 changed = false;
        p8 word[16];
        p8 key[32];
        bipolar lock;

        if (!host_settings_kept(address_of settings) &&
            !host_settings_booted(address_of settings))
                return;
        while (host_settings_next(address_of settings, address_of at, address_of setting))
                carried |= setting.entry.list == SPARK_SETTINGS_GROUP;
        if (!carried)
        {
                crypto_forget(address_of settings, sizeof settings);
                return;
        }

        lock = link_peers_lock();
        if (lock < 0 || !link_groups_for_change(address_of groups))
        {
                link_peers_unlock(lock);
                crypto_forget(address_of settings, sizeof settings);
                return;
        }
        at = 0;
        while (host_settings_next(address_of settings, address_of at, address_of setting))
        {
                p8 name[WATERLINK_NAMESPACE_MAX];
                p32 may;
                struct link_group_record address_to record;

                if (!link_group_entry_read(address_of setting, name, key, address_of may))
                        continue;
                record = link_group_named(address_of groups, (string_address)name);
                if (record && crypto_same(record->key, key, 32))
                        continue;
                if (!record)
                {
                        if (groups.count >= LINK_GROUPS_MAX)
                                continue;
                        record = link_group_append(address_of groups, (string_address)name);
                }
                memory_copy(record->key, key, 32);
                memory_zero(record->check, sizeof record->check);
                record->may = may;
                changed = true;
        }
        crypto_forget(key, sizeof key);
        if (changed)
                (void)link_groups_save(address_of groups);
        link_peers_unlock(lock);
        crypto_forget(address_of groups, sizeof groups);
        crypto_forget(address_of settings, sizeof settings);

        word[0] = 0;
        host_read_word(LINK_SWITCH_PATH, word, sizeof word);
        if (!word[0])
                (void)radio_write_word(LINK_SWITCH_PATH, "on");
}
#endif

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
        //      An image that carried it carries it no more, or the next start
        //      would be in the group again.
        link_group_uncarry(namespace);

        if (forget)
        {
                bipolar dropped = link_group_forget_members(keys.mark);

                //      The group is gone from its file already; a peers file
                //      that did not take the forget is said, not reported as done.
                if (dropped == -2)
                {
                        crypto_forget(address_of keys, sizeof keys);
                        return host_refuse("left group %s, but %s could not be "
                                           "written: the machines it linked are "
                                           "kept\n",
                                           namespace, LINK_PEERS_PATH);
                }
                if (dropped < 0)
                {
                        crypto_forget(address_of keys, sizeof keys);
                        return host_refuse("left group %s; %s could not be "
                                           "locked or read, so the machines "
                                           "it linked are kept\n",
                                           namespace, LINK_PEERS_PATH);
                }
                host_say(log, host_label "left group %s and removed the "
                                         "%p machines it linked\n",
                         namespace, (positive)dropped);
        }
        else
                host_say(log, host_label "left group %s; the machines it "
                                         "linked are still known\n",
                         namespace);
        crypto_forget(address_of keys, sizeof keys);
        return 0;
}

/*
        The group of a name, or the one group there is: a name is given, or
        this machine is in exactly one that is no code. Its record is copied
        into into; the refusal's status when there is none.
*/
static b32 link_group_pick(string_address namespace,
                           struct link_group_record address_to into)
{
        link_groups groups;
        struct link_group_record address_to record = null;
        positive found = 0;

        if (!link_groups_for_show(address_of groups))
                return host_refuse("%s could not be read\n", LINK_GROUPS_PATH);
        if (namespace)
                record = link_group_named(address_of groups, namespace);
        else
                for (positive at = 0; at < groups.count; at++)
                        if (!(groups.record[at].flags & LINK_GROUP_ONCE))
                        {
                                record = groups.record + at;
                                found++;
                        }
        if (!namespace && found > 1)
        {
                crypto_forget(address_of groups, sizeof groups);
                return host_refuse("this machine is in %p groups: name one\n", found);
        }
        if (!record)
        {
                crypto_forget(address_of groups, sizeof groups);
                return namespace ? host_refuse("this machine is not in group %s\n", namespace)
                                 : host_refuse("this machine is in no group: "
                                               "moonwater link group new makes one\n");
        }
        memory_copy(into, record, sizeof(address_to into));
        crypto_forget(address_of groups, sizeof groups);
        return 0;
}

//      The grants a verb of the group's was given after its words, or what
//      a carried group gives: every one. 0, or a refusal.
static b32 link_group_allowed(string_address address_to words, positive count,
                              p32 address_to may)
{
        address_to may = LINK_GROUP_MAY;
        if (!count)
                return 0;
        if (!string_equals(words[0], "allow") || count == 1)
                return host_usage();
        if (!link_grants_parse(words + 1, count - 1, may, null))
                return host_refuse("a grant is one of shell run files log input\n");
        return 0;
}

static fn link_group_say(string_address namespace, p32 may)
{
        p8 grants[96];

        link_grants_text(may, grants, sizeof grants);
        host_say(log, host_label "in group %s; its members may %s here\n", namespace,
                 (string_address)grants);
        if (!link_switch_is_on())
                host_say(log, host_label "the listener is off: moonwater link on starts "
                                         "it\n");
}

/*
        group new [NAME] [allow GRANT...]

        A group of a key of 256 random bits, joined here and carried (see
        above); NAME is the one group this machine is in when it is in one,
        which is made again, and this machine's own name when it is in none.
        Made again, the machines the old key paired are forgotten here, which
        is the point: it locks out a stick that was lost. The key is printed,
        once and for whoever asked, and again by `link group key`.
*/
static b32 link_group_new(string_address address_to words, positive count)
{
        string_address namespace = null;
        p8 own[WATERLINK_NAME_MAX];
        p8 key[32];
        p8 text[LINK_GROUP_TEXT];
        p8 replaced;
        p32 may;
        positive at = 0;
        positive dropped = 0;
        b32 refused;

        scope_exit(crypto_forget(key, sizeof key); crypto_forget(text, sizeof text));
        if (count && !string_equals(words[0], "allow"))
                namespace = words[at++];
        refused = link_group_allowed(words + at, count - at, address_of may);
        if (refused)
                return refused;
        if (!namespace)
        {
                link_groups groups;
                positive found = 0;

                if (!link_groups_for_show(address_of groups))
                        return host_refuse("%s could not be read\n", LINK_GROUPS_PATH);
                for (positive g = 0; g < groups.count; g++)
                        if (!(groups.record[g].flags & LINK_GROUP_ONCE))
                        {
                                string_copy_bounded((string_address)own,
                                                    groups.record[g].namespace,
                                                    sizeof own);
                                found++;
                        }
                crypto_forget(address_of groups, sizeof groups);
                if (found > 1)
                        return host_refuse("this machine is in %p groups: moonwater link "
                                           "group new NAME says which\n", found);
                if (!found)
                        link_machine_name(own);
                namespace = (string_address)own;
        }
        if (!link_group_name_good(namespace))
                return link_refuse_name(namespace, "group name");
        return_if(system_random_fill(key, sizeof key, 0) < 0, host_fail("randomness", -EIO));
        refused = link_group_store(namespace, key, may, address_of replaced);
        if (refused)
                return refused;
        if (replaced)
        {
                bipolar forgot = link_group_forget_members(link_group_mark(namespace));

                dropped = forgot > 0 ? (positive)forgot : 0;
                host_say(log, host_label "group %s has a new key, and the %p machines "
                                         "the old one paired are forgotten here: "
                                         "anything that carries the old key is out of "
                                         "the group\n",
                         namespace, dropped);
        }
        link_group_carry(namespace, key, may);
        link_group_text(namespace, key, text);
        host_say(log, host_label "the key of group %s, which anybody who has it can "
                                 "join with: %s\n",
                 namespace, (string_address)text);
        host_say(log, host_label "on another machine, with the key on standard "
                                 "input: moonwater link group add -; in an image: "
                                 "moonwater link group stamp IMAGE\n");
        link_group_say(namespace, may);
        return 0;
}

//      group key [NAME]: the key of a group, which is all there is to say of it.
static b32 link_group_key(string_address namespace)
{
        struct link_group_record record;
        p8 text[LINK_GROUP_TEXT];
        b32 refused = link_group_pick(namespace, address_of record);

        if (refused)
                return refused;
        link_group_text(record.namespace, record.key, text);
        host_say(log, "%s\n", (string_address)text);
        crypto_forget(text, sizeof text);
        crypto_forget(address_of record, sizeof record);
        return 0;
}

#define LINK_GROUP_LINE (LINK_GROUP_TEXT + WATERLINK_NAME_MAX)

/*
        A key text taken from where it is given: the line, wiped from argv
        once it is copied (a key there is in /proc/PID/cmdline for every user,
        and the tight tier refuses it), or "-", one line of standard input.
        Whether it is a group's key is the caller's to say. 0, or a refusal.
*/
static b32 link_group_text_take(string_address word, p8 address_to made)
{
        p8 typed[LINK_GROUP_LINE + 1];

        scope_exit(crypto_forget(typed, sizeof typed));
        memory_zero(made, LINK_GROUP_LINE);
        if (string_equals(word, "-"))
        {
                return_if(radio_password_read(typed, sizeof typed, "group key", false,
                                              true) < 0,
                          host_refuse("nothing saved\n"));
                memory_copy(made, typed, LINK_GROUP_LINE);
                return 0;
        }
        {
                positive length = string_length(word);

                return_if(MOONWATER_STRICT_NET >= STRICT_TIGHT,
                          host_refuse("a key on the command line is refused: give it "
                                      "on standard input with -\n"));
                if (length < LINK_GROUP_LINE)
                        memory_copy(made, word, length + 1);
                crypto_forget(word, length);
                return_if(length >= LINK_GROUP_LINE,
                          host_refuse("that is not a group's key\n"));
        }
        return 0;
}

static b32 link_group_parse_key_and_grants(string_address address_to words,
                                           positive count, p8 address_to name,
                                           p8 address_to key, p32 address_to may)
{
        p8 made[LINK_GROUP_LINE];
        b32 refused;

        scope_exit(crypto_forget(made, sizeof made));
        refused = link_group_text_take(words[0], made);
        if (refused)
                return refused;
        if (!link_group_text_parse((string_address)made, name, key))
                return host_refuse("that is not a group's key: NAME:KEY, as moonwater "
                                   "link group key prints it\n");
        return link_group_allowed(words + 1, count - 1, may);
}

/*
        group add NAME:KEY|- [allow GRANT...]

        A group by its key text, joined and carried. "-" reads the text from
        standard input, as a secret is.
*/
static b32 link_group_add(string_address address_to words, positive count)
{
        p8 name[WATERLINK_NAMESPACE_MAX];
        p8 key[32];
        p8 replaced;
        p32 may;
        b32 refused;

        scope_exit(crypto_forget(key, sizeof key));
        refused = link_group_parse_key_and_grants(words, count, name, key, address_of may);
        if (refused)
                return refused;
        refused = link_group_store((string_address)name, key, may, address_of replaced);
        if (refused)
                return refused;
        link_group_carry((string_address)name, key, may);
        link_group_say((string_address)name, may);
        return 0;
}

/*
        group stamp IMAGE [NAME|NAME:KEY|-] [allow GRANT...]

        The group put in an image that is a file, which a stick is written
        from: the machines that start from it are in the group. The group is
        one this machine is in (NAME, or the only one), or one a key text
        names, on the line or on standard input with -, which takes nothing
        of this machine's own and so needs no root: a build machine that was
        never in the group stamps the images it builds. The key is not
        printed. Linux only, where the image's settings are read; a Mac gives
        its key to a machine that runs Moonwater (`link group key`).
*/
static b32 link_group_stamp(string_address address_to words, positive count)
{
#if defined(MACOS)
        (void)words;
        (void)count;
        return host_refuse("an image is stamped on a machine that runs Moonwater: "
                           "moonwater link group key here, and there moonwater link "
                           "group stamp IMAGE NAME:KEY\n");
#else
        struct link_group_record record;
        host_settings settings;
        string_address failed;
        p8 name[WATERLINK_NAMESPACE_MAX];
        p8 key[32];
        p32 may = LINK_GROUP_MAY;
        b32 refused;
        b32 read;
        bipolar stamped;

        scope_exit(crypto_forget(key, sizeof key);
                   crypto_forget(address_of record, sizeof record));
        memory_zero(address_of record, sizeof record);
        if (count > 1 && (string_equals(words[1], "-") || string_first_of(words[1], ':')))
        {
                refused = link_group_parse_key_and_grants(words + 1, count - 1,
                                                          name, key, address_of may);
                if (refused)
                        return refused;
        }
        else
        {
                refused = count > 2 ? host_usage()
                                    : link_group_pick(count > 1 ? words[1] : null,
                                                      address_of record);
                if (refused)
                        return refused;
                string_copy_bounded((string_address)name, record.namespace, sizeof name);
                memory_copy(key, record.key, 32);
                may = record.may;
        }
        read = host_settings_image(words[0], address_of settings);
        if (read < 0)
                return host_refuse("%s is not an image with settings in it\n", words[0]);
        failed = link_group_entry_put(address_of settings, (string_address)name, key, may);
        if (failed)
        {
                refused = host_settings_refused("group", failed, address_of settings);
                crypto_forget(address_of settings, sizeof settings);
                return refused;
        }
        settings.generation = (read == 1 ? settings.generation : 0) + 1;
        stamped = host_settings_stamp(words[0], address_of settings);
        crypto_forget(address_of settings, sizeof settings);
        if (stamped)
                return host_refuse("%s could not be written\n", words[0]);
        host_say(log, host_label "%s carries group %s: a machine that starts from it "
                                 "is in the group, and so is anybody who has a copy "
                                 "of it\n",
                 words[0], (string_address)name);
        return 0;
#endif
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
#define LINK_PAIR_MAY WATERLINK_MAY_ALL

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
        link_peers had; // the machines linked before the code was made
} link_pair_made;

static p8 link_pair_waiting(struct link_group_record address_to record,
                            string_address namespace, p64 now)
{
        return !(record->flags & LINK_GROUP_DONE) &&
               !link_group_spent(record, now) &&
               (record->flags & LINK_GROUP_ONCE) &&
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
                record = link_group_append(address_of groups, namespace);
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
                //      A machine this code linked: one with the mark that was
                //      not here before. The mark is the name's alone, the same
                //      for every code this machine makes, so a machine an
                //      earlier code linked carries it too, and the wait said
                //      "linked with" that one at once and left the new code
                //      open its five minutes with nobody watching.
                for (positive at = 0; at < peers.count; at++)
                        if (peers.peer[at].group == made->mark &&
                            !link_peer_keyed(address_of made->had,
                                             peers.peer[at].key))
                        {
                                if (tty && shown)
                                        host_say(log, "\n");
                                //      The name was known by another key: the
                                //      listener kept the machine under it, and
                                //      the old key went (link_peer_returned).
                                if (link_peer_named(address_of made->had,
                                                    (string_address)peers.peer[at].name))
                                        host_say(log, host_label "%s's key "
                                                      "changed; the new one "
                                                      "replaces it\n",
                                                 (string_address)peers.peer[at].name);
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
        //      Five ways a wait ends, and each says which, with its own next
        //      step: used (above), refused (a machine had the code and was
        //      already linked), stopped, cancelled, or expired.
        if (closed)
                return host_refuse("refused: a machine used the code, but it is "
                                   "already linked, and keeps what it may do  "
                                   "next: moonwater link shows it\n");
        link_pair_close(made->mark);
        if (stopped)
                return host_refuse("no answer: stopped before a machine used the "
                                   "code, and the code no longer works  next: "
                                   "moonwater link pair makes another\n");
        if (!live && link_boot_seconds() < made->expires)
                return host_refuse("no answer: moonwater link cancel ended the wait "
                                   "before a machine used the code, and the code "
                                   "no longer works  next: moonwater link pair "
                                   "makes another\n");
        //      This machine keeps no record of a try it refused, so the one
        //      thing an expiry can say is that nobody got the code through.
        return host_refuse("expired: nobody used the code in %p seconds, so no "
                           "machine was linked. This machine keeps no record of a "
                           "try it refused, so it cannot say whether one came  "
                           "next: moonwater link pair, and type the code as it is "
                           "printed\n",
                           (positive)link_pair_seconds());
}

//      The code in the groups file and the listener on, or why not.
static b32 link_pair_start(string_address namespace, p8 address_to code,
                           string_address expect, link_pair_made address_to made,
                           link_before address_to before)
{
        bipolar opened;

        //      Before the code exists, so nothing it links is among them.
        link_peers_load(address_of made->had);
        opened = link_pair_open(namespace, code, expect, made);

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
        positive length = string_length(text);

        return length - memory_count(text, '-', length) -
               memory_count(text, ' ', length);
}

static p8 link_has_route(void)
{
        return (discovery_lan_reach() & DISCOVERY_LAN_ROUTE) != 0;
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

        scope_exit(crypto_forget(code, sizeof code); crypto_forget(said, sizeof said));
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

/*      Said by each refusal that comes before anything is sent: the other
        machine is not reached, so a person is not left to wonder whether it
        was. */
#define LINK_UNSENT "nothing was sent, so the other machine was not reached"

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

        scope_exit(crypto_forget(code, sizeof code));
        //      The name, the code and the route are judged here, before
        //      anything goes on the air: a refusal that comes after the wait
        //      says nothing of what was wrong with them.
        if (string_length(typed_name) >= WATERLINK_NAME_MAX)
                return host_refuse("unknown name: %s is not a machine name; "
                                   "letters, digits, . - _, up to 31; " LINK_UNSENT "\n",
                                   typed_name);
        link_fold_name(typed_name, name);
        if (!link_name_good((string_address)name))
                return host_refuse("unknown name: %s is not a machine name; "
                                   "letters, digits, . - _, up to 31, and not one "
                                   "of the words link uses; " LINK_UNSENT "\n",
                                   typed_name);
        link_machine_name(own);
        if (string_equals((string_address)own, (string_address)name))
                return host_refuse("%s is this machine's own name; " LINK_UNSENT "\n",
                                   (string_address)name);
        if (symbols != LINK_CODE_LENGTH)
                return host_refuse("wrong code length: %s has %p symbols, and a code "
                                   "has %p, as moonwater link pair prints it: "
                                   "abc-def; " LINK_UNSENT "\n",
                                   typed, (positive)symbols,
                                   (positive)LINK_CODE_LENGTH);
        if (!link_code_clean(typed, code))
                return host_refuse("code not accepted: %s has a character that no "
                                   "code uses; a code is letters and digits, and "
                                   "no u. Copy it again from moonwater link pair; "
                                   LINK_UNSENT "\n",
                                   typed);
        //      The code is in /proc/PID/cmdline for the five minutes this
        //      waits, which every user reads below the locked tier, and it
        //      opens every grant a code gives: wiped, as a group's secret is.
        crypto_forget(typed, string_length(typed));
        host_read_word(LINK_TRANSPORT_PATH, transport, sizeof transport);
        if (!string_equals((string_address)transport, "modem") && !link_has_route())
                return host_refuse("peer not reachable: this machine has no route to "
                                   "a network, so %s cannot be reached from here; "
                                   LINK_UNSENT ". moonwater link doctor says why\n",
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

/*
        moonwater link paths: the ways in this machine has, and where each
        machine linked was last heard. The local addresses and the public
        mapping are what the network's discovery kept (src/net/discovery.c,
        `moonwater network discovery`), read and never asked of the network
        here. Settings' Waterlink rows read this page, a row a line.
*/
//      Where each machine linked was last heard: its record's place, then
//      the others the listener kept for it, each with its age.
static fn link_paths_places(string_address prefix)
{
        link_peers peers;
        link_places places;
        positive shown = 0;

        link_peers_load(address_of peers);
        link_places_load(address_of places);
        string_format(log, "%splaces", prefix);
        for (positive at = 0; at < peers.count; at++)
        {
                struct waterlink_peer address_to peer = peers.peer + at;
                p8 place[64];
                p8 ago[32];

                if (!link_address_unicast(peer->address))
                        continue;
                link_place_text(peer->address, peer->port, place);
                string_format(log, "%s %s %s", shown++ ? ";" : "",
                              (string_address)peer->name, (string_address)place);
                for (positive seen = 0; seen < places.count; seen++)
                {
                        if (!crypto_same(places.place[seen].key, peer->key, 32) ||
                            link_place_same(places.place[seen].address,
                                            places.place[seen].port,
                                            peer->address, peer->port))
                                continue;
                        link_place_text(places.place[seen].address,
                                        places.place[seen].port, place);
                        discovery_ago(places.place[seen].seen, ago);
                        string_format(log, ", %s %s", (string_address)place,
                                      (string_address)ago);
                }
        }
        string_format(log, "%s\n", shown ? "" : " none: no machine linked has "
                                                "been heard yet");
}

static fn link_paths_report(const discovery_settings address_to settings,
                            const discovery_found address_to found,
                            const discovery_here address_to here,
                            string_address prefix)
{
        positive first = found->answered & 1 ? 0 : 1;
        positive mine = 0;
        p8 text[48];

        for (positive at = 0; at < here->owns; at++)
                mine += here->own[at].scope != NET_SCOPE_LOOPBACK;
        string_format(log, "%spaths udp %p on %p local address%s", prefix,
                      (positive)link_port(), mine, mine == 1 ? "" : "es");
        //      The local network's two switches (discovery_lan_allowed).
        if ((settings->ask & DISCOVERY_ASK_ANNOUNCE) && (settings->ask & DISCOVERY_ASK_LISTEN))
                string_format(log, "; multicast discovery on the network");
        else if (settings->ask & (DISCOVERY_ASK_ANNOUNCE | DISCOVERY_ASK_LISTEN))
                string_format(log, "; multicast discovery on the network, %s only",
                              settings->ask & DISCOVERY_ASK_LISTEN ? "listening" : "announcing");
        if (found->answered)
        {
                discovery_address_text(found->mapped[first], text);
                string_format(log, "; public %s:%p, %s", (string_address)text,
                              (positive)found->mapped_port[first],
                              found->nat == DISCOVERY_NAT_INDEPENDENT ||
                                      found->nat == DISCOVERY_NAT_NONE
                                  ? "a hole can be punched"
                                  : "no hole can be punched yet");
        }
        string_format(log, "\n");
        link_paths_places(prefix);
}

static b32 link_paths_verb(void)
{
        discovery_settings settings;
        discovery_found found;
        discovery_here here;

        discovery_settings_read(address_of settings);
        discovery_found_read(address_of found);
        discovery_here_read(address_of here);
        link_paths_report(address_of settings, address_of found, address_of here, "");
        return 0;
}

/*      link doctor's checks: read only. The listener's state and the port
        are read; the addresses come from the same walk the network setup
        uses; the route is a connect that sends nothing; discovery is one
        multicast join on a socket closed again. No pairing request is sent.
        After the checks the network's discovery looks again at how the
        machine is reached, as far as its switches allow (moonwater network
        discovery refresh), and the link says its paths. */
static p8 link_doctor_list[96];
static positive link_doctor_used;

static fn link_doctor_add(address_any context, b32 index, p32 host, p32 mask)
{
        (void)context;
        (void)index;
        (void)mask;
        if ((host >> 24) == 127 || link_doctor_used + 20 >= sizeof link_doctor_list)
                return;
        if (link_doctor_used)
                link_doctor_list[link_doctor_used++] = ' ';
        link_doctor_used += host_into(link_doctor_list + link_doctor_used, host);
        link_doctor_list[link_doctor_used] = 0;
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

/*      Which carriers there are here, and which one each peer the listener
        has open is on: said, not counted among the checks. */
static fn link_doctor_carriers(bipolar owner, positive port)
{
        link_state state;
        positive shown = 0;
        p8 carrier = link_carrier(0);
        p8 proxy[96];
        discovery_settings settings;

        discovery_settings_read(address_of settings);
        string_format(log, "  carriers (moonwater link transport %s):\n",
                      link_carrier_names[carrier]);
        string_format(log, "        udp %p: %s\n", port,
                      carrier == LINK_CARRIER_TCP ? (string_address) "the listener's; a client run dials tcp"
                                                  : (string_address) "yes");
        if (carrier == LINK_CARRIER_NETWORK)
                string_format(log, "        tcp: no; moonwater link transport tcp or auto "
                                   "listens on tcp %p and dials over it\n",
                              port);
        else
                string_format(log, "        tcp %p: %s, and %s\n", port,
                              owner > 0 ? (string_address) "the listener takes connections"
                                        : (string_address) "the listener takes connections once it is on",
                              carrier == LINK_CARRIER_TCP
                                      ? (string_address) "a client dials over tcp"
                                      : (string_address) "a client dials over tcp when udp is not answered in a second");
        if (discovery_proxy(address_of settings, proxy, sizeof proxy))
                string_format(log, "        proxy %s: a tcp dial goes through its CONNECT "
                                   "(moonwater network proxy off, or NO_PROXY=*, goes around it)\n",
                              (string_address)proxy);
        else
                string_format(log, "        proxy: none, a tcp dial goes straight there\n");

        if (!link_state_read(owner, address_of state))
                return;
        for (positive at = 0; at < state.open_count && at < LINK_SESSIONS; at++)
        {
                link_open_entry address_to open = state.open + at;

                open->name[WATERLINK_NAME_MAX - 1] = 0;
                open->carrier[sizeof open->carrier - 1] = 0;
                if (!link_name_good((string_address)open->name))
                        continue;
                if (!shown++)
                        string_format(log, "        peers the listener has open:\n");
                string_format(log, "          %s over %s\n", (string_address)open->name,
                              open->carrier[0] ? (string_address)open->carrier
                                               : (string_address) "udp");
        }
}

/*      The checks print the words the status line prints, for the same state
        of the same thing, so a check and the status cannot disagree. */
static b32 link_doctor(void)
{
        bipolar owner = host_lock_owner(LINK_LOCK_PATH);
        positive failed = 0;
        positive port = (positive)link_port();
        positive listener = link_listener_status(owner);
        positive reach = discovery_lan_reach();

        if (listener == LINK_LISTENER_ON || listener == LINK_LISTENER_BY_HAND)
                string_format(log, "  pass  listener: %s\n",
                              (string_address)link_listener_names[listener]);
        else
        {
                failed++;
                string_format(log, "  fail  listener: %s\n"
                                   "        fix: %s\n",
                              (string_address)link_listener_names[listener],
                              (string_address)link_listener_next[listener]);
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
        discovery_interfaces_each(link_doctor_add, null);
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

        if (reach & DISCOVERY_LAN_ROUTE)
                string_format(log, "  pass  route: a route to a network\n");
        else
        {
                failed++;
                string_format(log, "  fail  route: no route to a network\n"
                                   "        fix: join a network (moonwater wifi, or the "
                                   "cable), then moonwater link doctor\n");
        }

        if (reach & DISCOVERY_LAN_JOINS)
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

        link_doctor_carriers(owner, port);

        {
                discovery_settings settings;
                discovery_found found;
                discovery_here here;

                string_format(log, "  how this machine is reached "
                                   "(moonwater network discovery):\n");
                discovery_settings_read(address_of settings);
                discovery_gather(address_of settings, address_of found, address_of here);
                discovery_report(log, address_of settings, address_of found,
                                 address_of here, "        ");
                link_paths_report(address_of settings, address_of found, address_of here,
                                  "        ");
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
        moonwater link transport [network|tcp|auto|modem]: how linked machines
        reach each other, kept in /root/link.transport, network when nothing is
        kept. network is UDP; tcp is the link's datagrams on a TCP connection,
        to the peer's port and through HTTPS_PROXY's CONNECT when one is named,
        and a listener on TCP beside UDP; auto is UDP first and TCP when a
        second has gone unanswered, with the same listener (stream.c).
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
#if defined(MACOS)
        //      The modem plays through Moonwater's sound server, which a Mac
        //      has none of; the Mac's side is the receiver (tools/modem_rx.c).
        return (string_address) "a Mac has no Moonwater sound server to play on";
#else
        struct audio_state page;

        if (!audio_test_page(&page))
                return (string_address) "the sound server is not running";
        if (!(page.output_state & AUDIO_STATE_OUT_CARD))
                return (string_address) "no sound card is open on the sound server";
        if (page.device[0] == 0)
                return (string_address) "the sound card has no device";
        return null;
#endif
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
        bipolar got = link_secret_at(LINK_MODEM_PATH, key, true);

        scope_exit(crypto_forget(key, sizeof key); crypto_forget(text, sizeof text));
        if (got == -EPERM)
                return host_refuse("%s is not a modem key: it must be a 32-byte "
                                   "file only root can read\n", LINK_MODEM_PATH);
        return_if(got < 0, host_fail(LINK_MODEM_PATH, got));
        memory_into_hex(text, key, 32);
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

static const string_address link_transport_names[] = {"network", "tcp", "auto", "modem"};

static b32 link_transport_verb(string_address word)
{
        bipolar failed;

        if (!word)
        {
                string_address why = link_modem_why();

                //      What is said is what is used: a file written by hand as
                //      modem, when the modem is not there, is network.
                host_say(log, host_label "transport %s\n",
                         link_modem_in_use() ? (string_address) "modem" : link_carrier_names[link_carrier(0)]);
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
        {
                positive at = string_table_find(word, link_transport_names,
                                                sizeof link_transport_names[0],
                                                array_count(link_transport_names));

                if (at == array_count(link_transport_names))
                        return host_refuse("transport is network, tcp, auto or modem, not %s\n", word);
                if (at == 3)
                {
                        string_address why = link_modem_why();

                        if (why)
                                return host_refuse("the audio modem is not ready: %s, so "
                                                   "the transport stays network\n", why);
                }
        }
        failed = radio_write_word(LINK_TRANSPORT_PATH, word);
        return_if(failed < 0, host_fail("link transport", failed));
        host_say(log, host_label "transport %s\n", word);
        return 0;
}

static b32 link_policy_verb(p8 allow, string_address address_to words, positive count)
{
        p32 mask = link_policy_load();
        p32 change = 0;
        p8 grants[96];
        string_address bad = null;

        if (!count)
                return host_usage();
        if (!link_grants_parse(words, count, address_of change, address_of bad))
                return host_refuse("unknown grant %s (expected shell, run, files, log, input, or all)\n",
                                   bad);
        mask = allow ? (mask | change) : (mask & ~change);
        if (!link_policy_save(mask))
                return host_refuse("%s could not be written\n", LINK_POLICY_PATH);
        link_grants_text(mask, grants, sizeof grants);
        host_say(log, "this machine allows %s\n", (string_address)grants);
        return 0;
}

static b32 link_modem_dispatch(string_address address_to arguments, positive count)
{
        static const struct
        {
                string_address word;
                positive exact;
        } sub[] = {
            {"key", 4},
            {"send", 5},
        };
        positive at = string_table_find(arguments[3], sub, sizeof sub[0],
                                        array_count(sub));

        if (at == array_count(sub) || count != sub[at].exact)
                return host_usage();
        return at == 0 ? link_modem_key_verb() : link_modem_send(arguments[4]);
}

static b32 link_group_dispatch(string_address address_to arguments, positive count)
{
        if (count == 3)
                return link_status();
        positive at = string_table_find(arguments[3], link_group_subcommands,
                                        sizeof link_group_subcommands[0],
                                        array_count(link_group_subcommands));
        if (at == array_count(link_group_subcommands))
                return link_group_join(arguments + 3, count - 3);
        if (count < link_group_subcommands[at].least ||
            count > link_group_subcommands[at].most)
                return host_usage();
        switch (at)
        {
        case 0: return count == 5 || string_equals(arguments[5], "forget")
                           ? link_group_leave(arguments[4], count == 6) : host_usage();
        case 1: return link_group_new(arguments + 4, count - 4);
        case 2: return link_group_key(count == 5 ? arguments[4] : null);
        case 3: return link_group_add(arguments + 4, count - 4);
        case 4: return link_group_stamp(arguments + 4, count - 4);
        }
        return host_usage();
}

static b32 link_peers_dispatch(p8 op, string_address address_to arguments, positive count)
{
        struct waterlink_peer where;
        bipolar lock;
        b32 answer;

        //      An address is looked up before the lock, not under it.
        memory_zero(address_of where, sizeof where);
        if (op == 0 && count == 6 &&
            !link_parse_place(arguments[5], where.address, address_of where.port))
                return host_refuse("%s is not an address this can reach\n", arguments[5]);

        lock = link_peers_lock();
        if (lock < 0)
                return host_refuse("%s could not be locked\n", LINK_PEERS_LOCK);
        answer = op == 0 ? link_add_locked(arguments[3], arguments[4],
                                           count == 6 ? address_of where : null)
               : op == 1 ? link_remove_locked(arguments[3])
               : op == 2 ? link_purge_locked()
               : op == 5 || op == 6 ? link_peer_switch_locked(arguments[3], op == 5)
                         : link_grant_locked(arguments[3], op == 3,
                                             arguments + 4, count - 4);
        link_peers_unlock(lock);
        return answer;
}

/*
        Descriptor table for every built-in verb of `moonwater link`: word,
        argument count bounds, mode + flags (SIGPIPE ignore on mutating verbs),
        mode-specific arg, and optional handler pointer. Unifies shape checks,
        SIGPIPE policy, and dispatch without string_equals chains.
*/
#define LINK_VF_CHANGE 1u // ignores SIGPIPE in link_main
enum {
        LINK_DM_FN0, LINK_DM_FN1, LINK_DM_ARGS, LINK_DM_SWITCH,
        LINK_DM_POLICY, LINK_DM_LOCKED, LINK_DM_KIND,
};

typedef struct
{
        string_address word;
        positive least, most;
        p8 mode, flags, arg;
        union {
                b32 (*fn0)(void);
                b32 (*fn1)(string_address);
                b32 (*args)(string_address address_to, positive);
        } run;
} link_verb_desc;

static const link_verb_desc link_verbs[] = {
    {"on",        3, 4,            LINK_DM_SWITCH, LINK_VF_CHANGE, 1, {0}},
    {"off",       3, 4,            LINK_DM_SWITCH, LINK_VF_CHANGE, 0, {0}},
    {"key",       3, 3,            LINK_DM_FN0,    0,              0, {.fn0 = link_key_verb}},
    {"serve",     3, 4,            LINK_DM_FN1,    0,              1, {.fn1 = link_serve}},
    {"port",      3, 4,            LINK_DM_FN1,    LINK_VF_CHANGE, 0, {.fn1 = link_port_verb}},
    {"transport", 3, 4,            LINK_DM_FN1,    LINK_VF_CHANGE, 0, {.fn1 = link_transport_verb}},
    {"modem",     4, 5,            LINK_DM_ARGS,   0,              0, {.args = link_modem_dispatch}},
    {"pair",      3, 4,            LINK_DM_FN1,    LINK_VF_CHANGE, 0, {.fn1 = link_pair_here}},
    {"add",       5, 6,            LINK_DM_LOCKED, LINK_VF_CHANGE, 0, {0}},
    {"remove",    4, 4,            LINK_DM_LOCKED, LINK_VF_CHANGE, 1, {0}},
    {"purge",     3, 3,            LINK_DM_LOCKED, LINK_VF_CHANGE, 2, {0}},
    {"allow",     5, ~(positive)0, LINK_DM_LOCKED, LINK_VF_CHANGE, 3, {0}},
    {"deny",      5, ~(positive)0, LINK_DM_LOCKED, LINK_VF_CHANGE, 4, {0}},
    {"allow-all", 4, ~(positive)0, LINK_DM_POLICY, LINK_VF_CHANGE, 1, {0}},
    {"deny-all",  4, ~(positive)0, LINK_DM_POLICY, LINK_VF_CHANGE, 0, {0}},
    {"shell",     4, 4,            LINK_DM_KIND,   0,              LINK_KIND_SHELL, {0}},
    {"run",       5, ~(positive)0, LINK_DM_KIND,   0,              LINK_KIND_RUN,   {0}},
    {"push",      6, 6,            LINK_DM_KIND,   0,              LINK_KIND_PUSH,  {0}},
    {"pull",      6, 6,            LINK_DM_KIND,   0,              LINK_KIND_PULL,  {0}},
    {"log",       4, 4,            LINK_DM_KIND,   0,              LINK_KIND_LOG,   {0}},
    {"pad",       3, 5,            LINK_DM_ARGS,   LINK_VF_CHANGE, 0, {.args = link_pad_dispatch}},
    {"cancel",    3, 3,            LINK_DM_FN0,    LINK_VF_CHANGE, 0, {.fn0 = link_cancel}},
    {"doctor",    3, 3,            LINK_DM_FN0,    0,              0, {.fn0 = link_doctor}},
    {"paths",     3, 3,            LINK_DM_FN0,    0,              0, {.fn0 = link_paths_verb}},
    {"group",     3, ~(positive)0, LINK_DM_ARGS,   LINK_VF_CHANGE, 1, {.args = link_group_dispatch}},
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
        p8 folded[WATERLINK_NAME_MAX];
        positive idx = verb ? string_table_find(verb, link_verbs,
                                                sizeof link_verbs[0],
                                                array_count(link_verbs))
                            : array_count(link_verbs);
        const link_verb_desc address_to v =
                idx < array_count(link_verbs) ? link_verbs + idx : null;

        if (v && (count < v->least || count > v->most))
                return host_usage();
        //      serve's one further word is the descriptor link on hands the
        //      listener it starts, digits and nothing else; any other word
        //      started a listener that never came back to say it was wrong.
        if (v && v->mode == LINK_DM_FN1 && v->arg && count == 4)
        {
                positive descriptor;

                if (!string_digits_checked_exact(arguments[3], 10,
                                                 address_of descriptor))
                        return host_usage();
        }

        //      A word that is no verb and no name a machine could have (help
        //      and leave are words of the groups, not verbs) is a machine of
        //      nobody's: what root would be told, anyone is.
        if (verb && !v && !link_name_good(verb))
                return count == 4 && link_code_shaped(arguments[3])
                           ? host_refuse("%s is not a machine name\n", verb)
                           : host_usage();

        //      The key, the machines and the groups are root's files: for
        //      anyone else the page would say "nobody linked" of a machine
        //      that has a dozen. An image stamped with a key that is given
        //      reads none of them (link_group_stamp).
        if (!(v && v->mode == LINK_DM_ARGS && v->arg && count >= 6 &&
              string_equals(arguments[3], "stamp") &&
              (string_equals(arguments[5], "-") || string_first_of(arguments[5], ':'))))
                host_need_root();

        if (!verb)
                return link_status();

        if (v)
                switch (v->mode)
                {
                case LINK_DM_FN0:    return v->run.fn0();
                case LINK_DM_FN1:    return v->run.fn1(count == 4 ? arguments[3] : null);
                case LINK_DM_ARGS:   return v->run.args(arguments, count);
                //      on and off alone are the listener; with a machine's name, that machine (link_peer_switch_locked).
                case LINK_DM_SWITCH: return count == 4 ? link_peers_dispatch(v->arg ? 5 : 6, arguments, count) : link_switch(v->arg, true);
                case LINK_DM_POLICY: return link_policy_verb(v->arg, arguments + 3, count - 3);
                case LINK_DM_LOCKED: return link_peers_dispatch(v->arg, arguments, count);
                case LINK_DM_KIND:   return link_client_run(arguments[3], v->arg,
                                                            arguments + 4, count - 4);
                }

        //      A word that is none of those is a machine: its terminal, one
        //      command on it, or, if it is not known and the word after it
        //      is a code, the pairing that makes it known. A name that is
        //      neither is told how to become one.
        //      (folded is the function's, not a block's: verb may be left
        //      pointing at it, and a block's array is gone at its brace,
        //      where link BOX-A CODE read whatever took its place.)
        link_fold_name(verb, folded);
        if (!link_known(verb) && link_known((string_address)folded))
                verb = (string_address)folded;
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
        string_address verb = count > 2 ? arguments[2] : null;
        positive idx = verb ? string_table_find(verb, link_verbs,
                                                sizeof link_verbs[0],
                                                array_count(link_verbs))
                            : array_count(link_verbs);
        positive previous[4];
        p8 ignored = false;
        b32 answer;

        if (idx < array_count(link_verbs) && (link_verbs[idx].flags & LINK_VF_CHANGE))
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

static fn link_keep_machine(void)
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
