/*
        `moonwater ssh`: OpenSSH's server, which `bowl profile ssh` puts in the
        Alpine bowl, run by Moonwater from its settings and from nothing else.

                ssh                     on or off, the port, the address, root,
                                        the time limit, the host key, the keys,
                                        the sessions and the last events
                ssh on | off            the server, kept across boots
                ssh root on | off       root may log in, by key only
                ssh port N | default    the tcp port [22]
                ssh address ADDRESS | any       one address, or every one
                ssh limit MINUTES | off | default   an on ends by itself [60]
                ssh key add LABEL KEY   allow a public key, by a label
                ssh key remove LABEL    and take it back; every session ends
                ssh end                 end every session now
                ssh log [N]             the last changes, starts, refusals
                                        and logins [20]

        `serve` is not in the page: it is the supervisor, which `ssh on` and
        the machine process start.

        The settings are the only truth. Everything the server is lives in
        /root/ssh (0700, root's, beside the other settings, so an install
        carries it to the disk and wipe keeps it): the settings, the list of
        keys (the one file AuthorizedKeysFile names), the host key, the
        configuration and the digests the profile recorded. The configuration
        is written by this file from the settings at every start and is the
        only one sshd is given (-f, no Include). No bowl's /etc/ssh, no
        user's ~/.ssh, no package's script and no program of a bowl names
        anything sshd reads.

        How it starts (ssh_view_enter, in a child of the supervisor):
          - in the machine's own mount namespace (PID 1's, whatever namespace
            the command was typed in), a namespace of its own is made, a slave
            of the machine's, so what it mounts goes nowhere else;
          - a tmpfs only that namespace sees takes a copy of each file the
            profile recorded (sshd, sshd-session, sshd-auth, ssh-keygen, the
            loader and the libraries they map), each hashed as it is copied
            and refused whole when one is not the SHA-256 the install took
            from the package's own checksum: what was checked is what runs,
            and the bowl's tree is never executed;
          - that tmpfs is laid over /lib and /usr/lib (the bowl's names for
            the loader and sshd-session), Moonwater's accounts for sshd over
            /etc/passwd and /etc/group, an empty directory over /etc/ssh and
            /var/empty, and the tmpfs made read-only;
          - the host key is made at the first start (ssh-keygen, from the
            copy), never anywhere else;
          - sshd -T is run on the file and every value the settings decide is
            compared with what it says it will use: any difference refuses;
          - sshd is executed by its absolute path with an empty environment:
            no LD_PRELOAD, LD_LIBRARY_PATH or LD_AUDIT, nothing inherited.
        A session is the machine's own: its shell is Moonwater's /bin/sh in
        that namespace, which is the machine's tree but for the copies.

        Enforced while it runs (ssh_keep_machine, the machine process, every
        few seconds): a timed on that ran out is turned off, the server runs
        exactly while the switch says on and is started again when the
        settings it was started with are not the settings now, and any sshd,
        sshd-session or sshd-auth that is not of the server Moonwater started,
        and anything else listening on the server's port, is ended.

        Dawn Larsson - Apache-2.0 license
        github.com/dawnlarsson/moonwater
*/

#ifndef MOONWATER_SSH_INCLUDED
#define MOONWATER_SSH_INCLUDED

// A check build names a scratch directory; every other build keeps it in /root.
#ifndef SSH_STATE
#define SSH_STATE "/root/ssh"
#endif
#define SSH_SETTINGS SSH_STATE "/settings"
#define SSH_KEYS SSH_STATE "/authorized_keys"
#define SSH_CONFIG SSH_STATE "/sshd_config"
#define SSH_HOST_KEY SSH_STATE "/host_ed25519_key"
#define SSH_HOST_PUB SSH_HOST_KEY ".pub"
#define SSH_DIGESTS SSH_STATE "/digests"
#define SSH_LOG SSH_STATE "/log"
#define SSH_LOCK HOST_STATE "/ssh.lock"
#define SSH_RUNNING HOST_STATE "/ssh.running"
// The mount point of the tmpfs only the server's namespace sees.
#define SSH_VIEW HOST_STATE "/ssh"
#define SSH_BOWL BOWL_ROOT_PREFIX "alpine"
#define SSH_PROFILE "ssh"
// The account sshd's unprivileged half runs as: an id no distribution hands out and nothing else of the machine is.
#define SSH_PRIVSEP_ID 65520
#define SSH_PRIVSEP_TEXT "65520"

#define SSH_PORT 22
#define SSH_LIMIT 60
#define SSH_LIMIT_MOST 1440
#define SSH_LABEL_MOST 32
#define SSH_KEYS_MOST 32
#define SSH_KEY_TEXT 2048
#define SSH_KEYS_ROOM (SSH_KEYS_MOST * (SSH_KEY_TEXT + 64))
#define SSH_CONFIG_ROOM 4096
#define SSH_TOLD_ROOM 65536
#define SSH_DIGESTS_MOST 24
#define SSH_DIGESTS_ROOM 8192
#define SSH_LOG_MOST 65536
#define SSH_LOG_LINE 400
#define SSH_LOG_SHOWN 20
#define SSH_LOG_ASKED 1000
#define SSH_LOOK_SECONDS 5
#define SSH_WAIT_NS ((p64)15000000000)
#define SSH_STOP_NS ((p64)5000000000)

/*
        What sshd is told, a line each, which sshd -T is held to. The keys are
        ed25519 and its security-key form; the key exchange is the
        post-quantum hybrid first and X25519; the ciphers are AES-GCM alone,
        which the prefix truncation of Terrapin (CVE-2023-48795) does not
        reach whether or not a client has strict key exchange (ChaCha20-
        Poly1305 and the CBC and encrypt-then-MAC modes it does), and the
        MACs only the encrypt-then-MAC ones, which no AEAD cipher uses. Every
        forwarding is off, and so is every way a file of the user's or of
        the system's could add to the session (environment, rc, principals,
        a command for keys).
*/
#define SSH_KEX "sntrup761x25519-sha512@openssh.com,curve25519-sha256,curve25519-sha256@libssh.org"
#define SSH_CIPHERS "aes256-gcm@openssh.com,aes128-gcm@openssh.com"
#define SSH_MACS "hmac-sha2-512-etm@openssh.com,hmac-sha2-256-etm@openssh.com"
#define SSH_KEY_TYPES "ssh-ed25519,sk-ssh-ed25519@openssh.com"

typedef struct
{
        string_address word;
        string_address value;
} ssh_line;

static const ssh_line ssh_fixed[] = {
    {"AddressFamily", "any"},
    {"HostKeyAlgorithms", "ssh-ed25519"},
    {"KexAlgorithms", SSH_KEX},
    {"Ciphers", SSH_CIPHERS},
    {"MACs", SSH_MACS},
    {"PubkeyAcceptedAlgorithms", SSH_KEY_TYPES},
    {"AllowUsers", "root"},
    {"AuthenticationMethods", "publickey"},
    {"PubkeyAuthentication", "yes"},
    {"PasswordAuthentication", "no"},
    {"KbdInteractiveAuthentication", "no"},
    {"PermitEmptyPasswords", "no"},
    {"HostbasedAuthentication", "no"},
    {"IgnoreRhosts", "yes"},
    {"AuthorizedKeysCommand", "none"},
    {"AuthorizedPrincipalsFile", "none"},
    {"AuthorizedPrincipalsCommand", "none"},
    {"TrustedUserCAKeys", "none"},
    {"StrictModes", "yes"},
    {"MaxAuthTries", "3"},
    {"MaxSessions", "4"},
    {"MaxStartups", "3:50:10"},
    {"PerSourceMaxStartups", "2"},
    {"LoginGraceTime", "20"},
    {"ClientAliveInterval", "30"},
    {"ClientAliveCountMax", "3"},
    {"TCPKeepAlive", "no"},
    {"DisableForwarding", "yes"},
    {"AllowTcpForwarding", "no"},
    {"AllowStreamLocalForwarding", "no"},
    {"AllowAgentForwarding", "no"},
    {"X11Forwarding", "no"},
    {"PermitTunnel", "no"},
    {"GatewayPorts", "no"},
    {"PermitListen", "none"},
    {"PermitOpen", "none"},
    {"PermitUserEnvironment", "no"},
    {"PermitUserRC", "no"},
    {"PermitTTY", "yes"},
    {"PrintMotd", "no"},
    {"UseDNS", "no"},
    {"Compression", "no"},
    {"LogLevel", "VERBOSE"},
    {"PidFile", "none"},
};

/* ---- the settings ---- */

typedef struct
{
        p8 on;
        p8 root;
        p16 port;
        p16 limit;            // minutes an on lasts; 0 for none
        p8 address[48];       // empty for every address; else one, as sshd -T writes it
        p8 boot[40];          // the boot a timed on was made in
        p64 until;            // the boot clock's second it ends at, 0 for none
} ssh_settings;

static fn ssh_settings_standard(ssh_settings address_to settings)
{
        memory_zero(settings, sizeof(address_to settings));
        settings->port = SSH_PORT;
        settings->limit = SSH_LIMIT;
}

// A whole number of digits only, no more than five, from low to high.
static bool ssh_number(string_address word, positive length, positive low, positive high,
                       positive address_to into)
{
        positive value = 0;

        if (!length || length > 5 || (word[0] == '0' && length > 1))
                return false;
        for (positive at = 0; at < length; at++)
        {
                if (word[at] < '0' || word[at] > '9')
                        return false;
                value = value * 10 + (positive)(word[at] - '0');
        }
        if (value < low || value > high)
                return false;
        address_to into = value;
        return true;
}

// How many of the first length bytes of text are each one of set.
static positive ssh_span(string_address text, string_address set, positive length)
{
        positive at = 0;

        while (at < length && text[at] && string_first_of(set, text[at]))
                at++;
        return at;
}

// count bytes from from to into, which is before it: the rest of a buffer moved to its start.
static fn ssh_slide(address_any into, address_any from, positive count)
{
        p8 address_to to = into;
        const p8 address_to source = from;

        for (positive at = 0; at < count; at++)
                to[at] = source[at];
}

// A hexadecimal digit's value, 0 for any other byte.
static positive ssh_hex(p8 digit)
{
        p8 value = memory_hex_nibbles[digit];

        return value == 255 ? 0 : value;
}

/*
        An address sshd may listen on, in the one spelling sshd -T says it
        back in: a dotted quad, or an IPv6 address in the shortest form (RFC
        5952: lower case, no leading zeros, the longest run of two or more
        zero groups as ::). A zone, a bracket, a name or a v4-mapped address
        is none. into takes the spelling; false for anything else.
*/
static bool ssh_address_normal(string_address word, p8 address_to into, positive room)
{
        p8 copy[64];
        p8 bytes[16];
        positive length = string_length(word);
        positive best = 0, best_at = 8, run = 0, run_at = 0, used = 0;
        p16 groups[8];

        if (!length || length >= sizeof(copy) || room < 48)
                return false;
        if (!string_first_of(word, ':'))
        {
                bipolar quad = string_to_host(word);

                if (quad < 0 || length > 15)
                        return false;
                string_copy_bounded(into, word, room);
                return true;
        }
        if (word[0] == '[' || string_first_of(word, '%') || string_first_of(word, '.'))
                return false;
        memory_copy(copy, word, length + 1);
        if (!net_ipv6_literal(copy, length, bytes))
                return false;
        for (positive at = 0; at < 8; at++)
                groups[at] = (p16)(bytes[at * 2] << 8 | bytes[at * 2 + 1]);
        for (positive at = 0; at < 8; at++)
        {
                if (groups[at])
                {
                        run = 0;
                        continue;
                }
                if (!run++)
                        run_at = at;
                if (run > best)
                {
                        best = run;
                        best_at = run_at;
                }
        }
        if (best < 2)
                best_at = 8;
        for (positive at = 0; at < 8; at++)
        {
                static const p8 digits[] = "0123456789abcdef";
                bool started = false;

                if (at == best_at)
                {
                        into[used++] = ':';
                        into[used++] = ':';
                        at += best - 1;
                        continue;
                }
                if (at && !(at == best_at + best && best_at < 8))
                        into[used++] = ':';
                for (positive shift = 12;; shift -= 4)
                {
                        positive digit = (positive)(groups[at] >> shift) & 15;

                        if (digit || started || !shift)
                        {
                                into[used++] = digits[digit];
                                started = true;
                        }
                        if (!shift)
                                break;
                }
        }
        into[used] = end;
        //      ::ffff:a.b.c.d is an IPv4 address that sshd would say as one.
        return !(best_at == 0 && best == 5 && groups[5] == 0xffff);
}

/*
        The settings file: one line a setting, its word and its value, which
        only this file writes. A line it does not know, a value that is none
        of the setting's, or a file that is not root's alone is no settings
        at all, and no settings are off.
*/
static bool ssh_settings_parse(string_address text, positive length, ssh_settings address_to settings)
{
        positive at = 0;

        ssh_settings_standard(settings);
        while (at < length)
        {
                positive stop = at, word, rest;
                string_address line = text + at;
                positive value = 0;

                while (stop < length && text[stop] != '\n')
                        stop++;
                rest = stop - at;
                at = stop + 1;
                if (!rest)
                        continue;
                word = 0;
                while (word < rest && line[word] != ' ')
                        word++;
                if (word == rest)
                {
                        if (word == 2 && !memory_compare(line, "on", 2))
                                settings->on = true;
                        else if (word == 3 && !memory_compare(line, "off", 3))
                                settings->on = false;
                        else
                                return false;
                        continue;
                }
                if (word == 4 && !memory_compare(line, "root", 4))
                {
                        if (rest == 7 && !memory_compare(line + 5, "on", 2))
                                settings->root = true;
                        else if (rest == 8 && !memory_compare(line + 5, "off", 3))
                                settings->root = false;
                        else
                                return false;
                }
                else if (word == 4 && !memory_compare(line, "port", 4))
                {
                        if (!ssh_number(line + 5, rest - 5, 1, 65535, address_of value))
                                return false;
                        settings->port = (p16)value;
                }
                else if (word == 5 && !memory_compare(line, "limit", 5))
                {
                        if (rest == 9 && !memory_compare(line + 6, "off", 3))
                                settings->limit = 0;
                        else if (!ssh_number(line + 6, rest - 6, 1, SSH_LIMIT_MOST, address_of value))
                                return false;
                        else
                                settings->limit = (p16)value;
                }
                else if (word == 7 && !memory_compare(line, "address", 7))
                {
                        p8 typed[64];
                        p8 normal[48];

                        if (rest - 8 >= sizeof(typed))
                                return false;
                        memory_copy(typed, line + 8, rest - 8);
                        typed[rest - 8] = end;
                        if (string_equals(typed, "any"))
                                settings->address[0] = end;
                        else if (!ssh_address_normal(typed, normal, sizeof(normal)) || !string_equals(normal, typed))
                                return false;
                        else
                                string_copy_bounded(settings->address, normal, sizeof(settings->address));
                }
                else if (word == 5 && !memory_compare(line, "until", 5))
                {
                        //      until BOOT SECOND: the boot id is 36 characters of hex and dashes.
                        string_address boot = line + 6;
                        positive boot_length = 0;
                        p64 second = 0;

                        while (6 + boot_length < rest && boot[boot_length] != ' ')
                                boot_length++;
                        if (boot_length != 36 || 6 + boot_length + 1 >= rest ||
                            ssh_span(boot, "0123456789abcdef-", boot_length) != boot_length)
                                return false;
                        for (positive digit = 6 + boot_length + 1; digit < rest; digit++)
                        {
                                if (line[digit] < '0' || line[digit] > '9' || second > 100000000000ull)
                                        return false;
                                second = second * 10 + (p64)(line[digit] - '0');
                        }
                        memory_copy(settings->boot, boot, boot_length);
                        settings->boot[boot_length] = end;
                        settings->until = second;
                }
                else
                        return false;
        }
        return true;
}

// The file the settings are, as ssh_settings_parse reads it back: its length.
static positive ssh_settings_text(const ssh_settings address_to settings, p8 address_to into, positive room)
{
        p8 number[24];
        positive used = 0;

        into[0] = end;
        (void)string_join(into, room, settings->on ? "on\n" : "off\n", settings->root ? "root on\n" : "root off\n");
        number[positive_into_string(number, settings->port)] = end;
        string_append_bounded(into, "port ", room);
        string_append_bounded(into, (string_address)number, room);
        string_append_bounded(into, "\naddress ", room);
        string_append_bounded(into, settings->address[0] ? (string_address)settings->address : (string_address) "any", room);
        string_append_bounded(into, "\nlimit ", room);
        if (settings->limit)
        {
                number[positive_into_string(number, settings->limit)] = end;
                string_append_bounded(into, (string_address)number, room);
        }
        else
                string_append_bounded(into, "off", room);
        string_append_bounded(into, "\n", room);
        if (settings->until && settings->boot[0])
        {
                number[positive_into_string(number, (positive)settings->until)] = end;
                string_append_bounded(into, "until ", room);
                string_append_bounded(into, (string_address)settings->boot, room);
                string_append_bounded(into, " ", room);
                string_append_bounded(into, (string_address)number, room);
                string_append_bounded(into, "\n", room);
        }
        used = string_length(into);
        return used + 1 < room ? used : 0;
}

/*
        A state file of root's alone, read whole: a regular file, root's, no
        more than its owner may touch, reached through no link. Anything else
        is no file (-EPERM), whatever it holds.
*/
static bipolar ssh_read_owned(string_address path, p8 address_to into, positive room)
{
        system_path_identity facts;
        bipolar handle = system_open_at(AT_FDCWD, path, FILE_READ | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
        bipolar got;

        if (handle < 0)
                return handle;
        got = system_path_identity_at(handle, (string_address) "", SYSTEM_PATH_AT_EMPTY_PATH,
                                      SYSTEM_PATH_STATX_TYPE | SYSTEM_PATH_STATX_MODE | SYSTEM_PATH_STATX_UID,
                                      address_of facts);
        if (got >= 0 && ((facts.mode & 0170000) != 0100000 || facts.user != 0 || (facts.mode & 077)))
                got = -EPERM;
        if (got >= 0)
                got = system_read_retry((positive)handle, into, room - 1);
        system_close(handle);
        if (got >= 0)
                into[got] = end;
        return got;
}

// The settings now: the standard (off) when there are none, and off when what is there is not this file's.
static bool ssh_settings_read(ssh_settings address_to settings)
{
        p8 text[512];
        bipolar got = ssh_read_owned(SSH_SETTINGS, text, sizeof(text));

        if (got == -ERROR_NO_ENTRY)
        {
                ssh_settings_standard(settings);
                return true;
        }
        if (got < 0 || got >= (bipolar)sizeof(text) - 1 || !ssh_settings_parse(text, (positive)got, settings))
        {
                ssh_settings_standard(settings);
                return false;
        }
        return true;
}

// /root/ssh, root's and no one else's: made, or put back to 0700 where it is a directory of root's.
static bipolar ssh_state_ready(void)
{
        system_path_identity facts;
        bipolar handle;
        bipolar got;

        (void)system_make_directory_at(AT_FDCWD, SSH_STATE, 0700);
        handle = system_open_at(AT_FDCWD, SSH_STATE, FILE_READ | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        if (handle < 0)
                return handle;
        got = system_path_identity_at(handle, (string_address) "", SYSTEM_PATH_AT_EMPTY_PATH,
                                      SYSTEM_PATH_STATX_TYPE | SYSTEM_PATH_STATX_UID, address_of facts);
        if (got >= 0 && facts.user != 0)
                got = -EPERM;
        if (got >= 0)
                got = system_call_2(syscall(fchmod), (positive)handle, 0700);
        system_close(handle);
        return got;
}

static bipolar ssh_settings_save(const ssh_settings address_to settings)
{
        p8 text[512];
        positive length = ssh_settings_text(settings, text, sizeof(text));
        bipolar ready = ssh_state_ready();

        if (ready < 0)
                return ready;
        return length ? host_write_file(SSH_SETTINGS, text, length, 0600, true) : -ENAMETOOLONG;
}

/* ---- the configuration ---- */

static positive ssh_config_line(p8 address_to into, positive room, string_address word, string_address value)
{
        string_append_bounded(into, word, room);
        string_append_bounded(into, " ", room);
        string_append_bounded(into, value, room);
        return string_append_bounded(into, "\n", room);
}

/*
        The file sshd is given, from the settings and nothing else: state is
        the directory the key list and the host key are in (/root/ssh; a
        check names another). Its length, or 0 when room is too small.
*/
static positive ssh_config_text(const ssh_settings address_to settings, string_address state,
                                p8 address_to into, positive room)
{
        p8 number[24];
        p8 path[HOST_PATH_ROOM];
        positive used;

        into[0] = end;
        string_append_bounded(into, "# Written by moonwater ssh from its settings at every start, and overwritten:\n"
                                    "# the server is changed with moonwater ssh, never here.\n",
                              room);
        number[positive_into_string(number, settings->port)] = end;
        ssh_config_line(into, room, "Port", (string_address)number);
        if (settings->address[0])
                ssh_config_line(into, room, "ListenAddress", (string_address)settings->address);
        if (!host_join(path, sizeof(path), state, "/host_ed25519_key"))
                return 0;
        ssh_config_line(into, room, "HostKey", (string_address)path);
        if (!host_join(path, sizeof(path), state, "/authorized_keys"))
                return 0;
        ssh_config_line(into, room, "AuthorizedKeysFile", (string_address)path);
        ssh_config_line(into, room, "PermitRootLogin", settings->root ? "prohibit-password" : "no");
        for (positive at = 0; at < array_count(ssh_fixed); at++)
                ssh_config_line(into, room, ssh_fixed[at].word, ssh_fixed[at].value);
        //      No UsePAM: Alpine's sshd is built without PAM (openssh-server-pam is another package) and says "Unsupported
        //      option" of the line at every start, and a build with PAM is off by default, which sshd -T is held to below.
        used = string_length(into);
        return used + 1 < room ? used : 0;
}

// Whether a line of what sshd -T said is word and value: its keyword without the case of its letters.
static bool ssh_told_is(string_address line, positive length, string_address word, string_address value)
{
        positive word_length = string_length(word);
        positive value_length = string_length(value);

        if (length != word_length + 1 + value_length || line[word_length] != ' ')
                return false;
        for (positive at = 0; at < word_length; at++)
        {
                p8 a = line[at], b = word[at];

                if (b >= 'A' && b <= 'Z')
                        b += 'a' - 'A';
                if (a != b)
                        return false;
        }
        return !memory_compare(line + word_length + 1, value, value_length);
}

// How many lines of told have word as their keyword, and whether one is the value.
static positive ssh_told_count(string_address told, positive length, string_address word, string_address value,
                               bool address_to found)
{
        positive count = 0, word_length = string_length(word), at = 0;

        address_to found = false;
        while (at < length)
        {
                positive stop = at;
                bool same = true;

                while (stop < length && told[stop] != '\n')
                        stop++;
                if (stop - at > word_length && told[at + word_length] == ' ')
                {
                        for (positive letter = 0; letter < word_length && same; letter++)
                        {
                                p8 b = word[letter];

                                if (b >= 'A' && b <= 'Z')
                                        b += 'a' - 'A';
                                same = told[at + letter] == b;
                        }
                        if (same)
                        {
                                count++;
                                if (value && ssh_told_is(told + at, stop - at, word, value))
                                        address_to found = true;
                        }
                }
                at = stop + 1;
        }
        return count;
}

/*
        Whether what sshd -T said (told) is what the settings decide, value
        for value: each line of the file once, with its value, and for the
        addresses exactly the ones the settings name. wrong takes the first
        keyword that is not, as sshd spells it.
*/
static bool ssh_config_verify(const ssh_settings address_to settings, string_address state, string_address told,
                              positive length, p8 address_to wrong, positive room)
{
        p8 number[24];
        p8 path[HOST_PATH_ROOM];
        p8 listen[80];
        bool found;
        positive count;

        wrong[0] = end;
        number[positive_into_string(number, settings->port)] = end;
#define SSH_EXPECT(word, value)                                                         \
        do                                                                              \
        {                                                                               \
                if (ssh_told_count(told, length, word, value, &found) != 1 || !found)   \
                {                                                                       \
                        string_copy_bounded(wrong, word, room);                         \
                        return false;                                                   \
                }                                                                       \
        } while (0)
        SSH_EXPECT("Port", (string_address)number);
        if (!host_join(path, sizeof(path), state, "/host_ed25519_key"))
                return false;
        SSH_EXPECT("HostKey", (string_address)path);
        if (!host_join(path, sizeof(path), state, "/authorized_keys"))
                return false;
        SSH_EXPECT("AuthorizedKeysFile", (string_address)path);
        //      OpenSSH 10 says prohibit-password as it is written, 9 by its first name, without-password: the same switch.
        if (!settings->root || ssh_told_count(told, length, "PermitRootLogin", "without-password", &found) != 1 || !found)
                SSH_EXPECT("PermitRootLogin", settings->root ? "prohibit-password" : "no");
        for (positive at = 0; at < array_count(ssh_fixed); at++)
                SSH_EXPECT(ssh_fixed[at].word, ssh_fixed[at].value);
#undef SSH_EXPECT
        //      A build with PAM says it is off; one without says nothing of it.
        if (ssh_told_count(told, length, "UsePAM", "no", &found) && !found)
        {
                string_copy_bounded(wrong, "UsePAM", room);
                return false;
        }
        //      Every address and the port, one line each: the two wildcards for every address, or the one named.
        count = ssh_told_count(told, length, "ListenAddress", null, &found);
        if (settings->address[0])
        {
                bool six = string_first_of((string_address)settings->address, ':') != null;

                (void)string_join(listen, sizeof(listen), six ? "[" : "", (string_address)settings->address,
                                  six ? "]:" : ":", (string_address)number);
                ssh_told_count(told, length, "ListenAddress", listen, &found);
                if (count != 1 || !found)
                {
                        string_copy_bounded(wrong, "ListenAddress", room);
                        return false;
                }
                return true;
        }
        (void)string_join(listen, sizeof(listen), "0.0.0.0:", (string_address)number);
        ssh_told_count(told, length, "ListenAddress", listen, &found);
        if (count == 2 && found)
        {
                (void)string_join(listen, sizeof(listen), "[::]:", (string_address)number);
                ssh_told_count(told, length, "ListenAddress", listen, &found);
        }
        if (count != 2 || !found)
        {
                string_copy_bounded(wrong, "ListenAddress", room);
                return false;
        }
        return true;
}

/* ---- the keys ---- */

typedef struct
{
        p8 label[SSH_LABEL_MOST + 1];
        p8 type[32];
        p8 blob[SSH_KEY_TEXT];
} ssh_key;

typedef struct
{
        ssh_key keys[SSH_KEYS_MOST];
        positive count;
} ssh_keys;

static bool ssh_label_valid(string_address label)
{
        positive length = string_length(label);

        return length && length <= SSH_LABEL_MOST &&
               string_span_of_set(label, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-") == length &&
               label[0] != '-' && label[0] != '.';
}

// A string of the SSH wire format at at, which ends before stop: its bytes and length, and at moved past it.
static bool ssh_wire_string(const p8 address_to address_to at, const p8 address_to stop, const p8 address_to address_to bytes,
                            positive address_to length)
{
        const p8 address_to here = address_to at;
        positive size;

        if (stop - here < 4)
                return false;
        size = (positive)here[0] << 24 | (positive)here[1] << 16 | (positive)here[2] << 8 | here[3];
        address_to at += 4;
        if ((positive)(stop - address_to at) < size)
                return false;
        address_to bytes = address_to at;
        address_to length = size;
        address_to at += size;
        return true;
}

/*
        A public key as OpenSSH writes it: its type, and its blob in base64,
        which holds the same type and the key and nothing after: 32 bytes of
        Ed25519, and for a security key the application it was made for
        (ssh:, at most 255 bytes). bytes takes the blob, length its size.
*/
static bool ssh_key_blob(string_address type, string_address blob, p8 address_to bytes, positive room,
                         positive address_to length)
{
        positive text = string_length(blob);
        positive padding = text && blob[text - 1] == '=' ? (text > 1 && blob[text - 2] == '=' ? 2 : 1) : 0;
        positive size;
        bool loose = false;
        const p8 address_to at = bytes;
        const p8 address_to part;
        positive part_length;
        bool security = string_equals(type, "sk-ssh-ed25519@openssh.com");

        if (!security && !string_equals(type, "ssh-ed25519"))
                return false;
        if (!text || text % 4 || text >= SSH_KEY_TEXT)
                return false;
        size = text / 4 * 3 - padding;
        if (size > room || !memory_from_base64(bytes, blob, text, size, &loose) || loose)
                return false;
        if (!ssh_wire_string(&at, bytes + size, &part, &part_length) || part_length != string_length(type) ||
            memory_compare(part, type, part_length))
                return false;
        if (!ssh_wire_string(&at, bytes + size, &part, &part_length) || part_length != 32)
                return false;
        if (security)
        {
                if (!ssh_wire_string(&at, bytes + size, &part, &part_length) || part_length < 4 || part_length > 255 ||
                    memory_compare(part, "ssh:", 4))
                        return false;
        }
        address_to length = size;
        return at == bytes + size;
}

// SHA256:... as ssh-keygen -l says it: the blob's SHA-256 in base64, with no padding.
static fn ssh_fingerprint(const p8 address_to blob, positive length, p8 address_to into)
{
        digest_state digest;
        p8 sum[32];
        p8 text[48];
        positive used;

        digest_open(address_of digest, DIGEST_SHA256, 32);
        digest_write(address_of digest, (address_any)blob, length);
        digest_close(address_of digest, sum);
        memory_into_base64(text, sum, sizeof(sum));
        used = string_length(text);
        while (used && text[used - 1] == '=')
                text[--used] = end;
        (void)string_join(into, 56, "SHA256:", (string_address)text);
}

static bool ssh_key_fingerprint(const ssh_key address_to key, p8 address_to into)
{
        p8 bytes[SSH_KEY_TEXT];
        positive length;

        into[0] = end;
        if (!ssh_key_blob((string_address)key->type, (string_address)key->blob, bytes, sizeof(bytes), &length))
                return false;
        ssh_fingerprint(bytes, length, into);
        return true;
}

#define SSH_KEY_OPTIONS "restrict,pty "

/*
        The list of keys, as this file writes it: one line a key, the options
        restrict and pty (no forwarding of any kind, no rc, a terminal), the
        type, the blob and the label. A line that is not that, a key that is
        not well made, a label that is twice, or more keys than the list
        holds, and it is not this file's list: false, and the server does not
        start on it.
*/
static bool ssh_keys_parse(string_address text, positive length, ssh_keys address_to keys)
{
        positive at = 0;

        keys->count = 0;
        while (at < length)
        {
                positive stop = at;
                p8 line[SSH_KEY_TEXT + 128];
                string_address words[3];
                positive count = 0;
                ssh_key address_to key;
                p8 bytes[SSH_KEY_TEXT];
                positive size;

                while (stop < length && text[stop] != '\n')
                        stop++;
                if (stop - at >= sizeof(line) || keys->count == SSH_KEYS_MOST)
                        return false;
                memory_copy(line, text + at, stop - at);
                line[stop - at] = end;
                at = stop + 1;
                if (!host_starts(line, SSH_KEY_OPTIONS))
                        return false;
                for (string_address word = line + sizeof(SSH_KEY_OPTIONS) - 1; *word;)
                {
                        string_address blank = string_first_of(word, ' ');

                        if (count == 3)
                                return false;
                        words[count++] = word;
                        if (!blank)
                                break;
                        *blank = end;
                        word = blank + 1;
                }
                if (count != 3 || !ssh_label_valid(words[2]) || string_length(words[0]) >= sizeof(keys->keys[0].type) ||
                    !ssh_key_blob(words[0], words[1], bytes, sizeof(bytes), &size))
                        return false;
                for (positive other = 0; other < keys->count; other++)
                        if (string_equals((string_address)keys->keys[other].label, words[2]))
                                return false;
                key = &keys->keys[keys->count++];
                string_copy_bounded(key->label, words[2], sizeof(key->label));
                string_copy_bounded(key->type, words[0], sizeof(key->type));
                string_copy_bounded(key->blob, words[1], sizeof(key->blob));
        }
        return true;
}

static positive ssh_keys_text(const ssh_keys address_to keys, p8 address_to into, positive room)
{
        positive used = 0;

        into[0] = end;
        for (positive at = 0; at < keys->count; at++)
        {
                string_append_bounded(into, SSH_KEY_OPTIONS, room);
                string_append_bounded(into, (string_address)keys->keys[at].type, room);
                string_append_bounded(into, " ", room);
                string_append_bounded(into, (string_address)keys->keys[at].blob, room);
                string_append_bounded(into, " ", room);
                string_append_bounded(into, (string_address)keys->keys[at].label, room);
                used = string_append_bounded(into, "\n", room);
        }
        used = string_length(into);
        return used + 1 < room ? used : 0;
}

static p8 ssh_keys_room[SSH_KEYS_ROOM];

// The list now: none when there is no file; false when the file is not this file's list.
static bool ssh_keys_read(ssh_keys address_to keys)
{
        bipolar got = ssh_read_owned(SSH_KEYS, ssh_keys_room, sizeof(ssh_keys_room));

        keys->count = 0;
        if (got == -ERROR_NO_ENTRY)
                return true;
        if (got < 0 || got >= (bipolar)sizeof(ssh_keys_room) - 1)
                return false;
        return ssh_keys_parse(ssh_keys_room, (positive)got, keys);
}

static bipolar ssh_keys_save(const ssh_keys address_to keys)
{
        positive length = ssh_keys_text(keys, ssh_keys_room, sizeof(ssh_keys_room));
        bipolar ready = ssh_state_ready();

        if (ready < 0)
                return ready;
        if (!length && keys->count)
                return -ENAMETOOLONG;
        return host_write_file(SSH_KEYS, ssh_keys_room, length, 0600, true);
}

// The host key's fingerprint, from its .pub, or false when there is none yet.
static bool ssh_host_fingerprint(p8 address_to into)
{
        p8 text[512];
        p8 bytes[256];
        positive size;
        bipolar got = file_slurp_regular_at(AT_FDCWD, SSH_HOST_PUB, text, sizeof(text), O_NOFOLLOW);
        string_address blob;
        string_address stop;

        into[0] = end;
        if (got <= 0 || !host_starts(text, "ssh-ed25519 "))
                return false;
        blob = text + 12;
        stop = blob;
        while (*stop && *stop != ' ' && *stop != '\n')
                stop++;
        *stop = end;
        if (!ssh_key_blob("ssh-ed25519", blob, bytes, sizeof(bytes), &size))
                return false;
        ssh_fingerprint(bytes, size, into);
        return true;
}

/* ---- the log ---- */

/*
        A line of the log, /root/ssh/log: when, in UTC, and what. Root's, 0600,
        only ever added to, and cut to its newer half past SSH_LOG_MOST. What
        sshd says is in it with every byte that is not printable as a ?, so a
        name a client sent cannot write a line of its own. A start, a stop, a
        refusal and a login go to the kernel's log as well.
*/
static fn ssh_log_line(string_address what, bool loud)
{
        static p8 text[SSH_LOG_MOST / 2];
        p8 line[SSH_LOG_LINE + 40];
        p8 when[32];
        time_t stamp = time(null);
        tm broken;
        bipolar handle;
        bipolar size;
        positive used;

        gmtime_r(address_of stamp, address_of broken);
        strftime(when, sizeof(when), "%Y-%m-%dT%H:%M:%SZ ", address_of broken);
        string_copy_bounded(line, (string_address)when, sizeof(line));
        used = string_length(line);
        for (positive at = 0; what[at] && used < SSH_LOG_LINE + 20; at++)
                line[used++] = what[at] >= 0x20 && what[at] < 0x7f ? what[at] : '?';
        line[used++] = '\n';
        line[used] = end;

        if (ssh_state_ready() >= 0)
        {
                handle = host_open_state(AT_FDCWD, SSH_LOG, 0600, FILE_APPEND);
                if (handle >= 0)
                {
                        (void)system_write_all_checked((positive)handle, (const address_any)line, used);
                        size = system_seek(handle, 0, 2);
                        system_close(handle);
                        //      Past its most, the newer half, from its first whole line.
                        if (size > SSH_LOG_MOST)
                        {
                                handle = system_open_at(AT_FDCWD, SSH_LOG, FILE_READ | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
                                if (handle >= 0)
                                {
                                        positive keep = sizeof(text) - 1;
                                        positive from = 0;
                                        bipolar got = file_transfer_exact(syscall(pread64), handle, text, keep, (p64)size - keep);

                                        system_close(handle);
                                        if (got >= 0)
                                        {
                                                from = memory_span_without_byte(text, '\n', keep);
                                                from += from < keep;
                                                (void)host_write_file(SSH_LOG, text + from, keep - from, 0600, true);
                                        }
                                }
                        }
                }
        }
        if (loud)
        {
                string_address parts[] = {"ssh: ", line + string_length((string_address)when), null};

                line[used - 1] = end;
                host_kmsg(parts);
        }
}

// Who asked: the pid, its program and its parent's, which the log says of every change.
static fn ssh_log_change(string_address what)
{
        p8 line[SSH_LOG_LINE];
        p8 self[24];
        p8 name[32];
        p8 parent_name[32];
        p8 path[48];
        positive parent = (positive)system_call(syscall(getppid));

        self[positive_into_string(self, (positive)system_call(syscall(getpid)))] = end;
        (void)string_join(path, sizeof(path), "/proc/", (string_address)self, "/comm");
        if (host_read_kernel((string_address)path, name, sizeof(name)) < 0)
                string_copy_bounded(name, "?", sizeof(name));
        self[positive_into_string(self, parent)] = end;
        (void)string_join(path, sizeof(path), "/proc/", (string_address)self, "/comm");
        if (host_read_kernel((string_address)path, parent_name, sizeof(parent_name)) < 0)
                string_copy_bounded(parent_name, "?", sizeof(parent_name));
        self[positive_into_string(self, (positive)system_call(syscall(getpid)))] = end;
        (void)string_join(line, sizeof(line), what, " (by ", (string_address)name, " ", (string_address)self,
                          ", from ", (string_address)parent_name, ")");
        ssh_log_line((string_address)line, true);
}

/* ---- what the profile installed ---- */

/*
        The files sshd runs from, as the install found them: the server's
        and the key generator's programs (every regular file of the packages
        openssh-server and openssh-keygen that is an ELF program), the loader
        they name and every library they map, followed through the libraries'
        own. Each is checked against the checksum its package has in the
        bowl's apk database, so what is recorded is what apk installed, and
        recorded by its SHA-256 in /root/ssh/digests, which no bowl can see
        at the hardened tier or write at any tier but the default's. A start
        copies each from the bowl, hashing it as it goes, and refuses whole on
        one that is not its digest.

        A line of the record: the SHA-256, the file in the bowl, and where the
        copy goes in the server's tmpfs: tree/ and its path for a program,
        lib/ and the name the loader asks for for the loader and a library.
*/
typedef struct
{
        p8 sum[65];
        p8 bowl[128];
        p8 view[128];
} ssh_digest;

typedef struct
{
        ssh_digest files[SSH_DIGESTS_MOST];
        positive count;
} ssh_digests;

// A record read back: false when it is not one, which is a profile not installed.
static bool ssh_digests_read(ssh_digests address_to digests)
{
        p8 text[SSH_DIGESTS_ROOM];
        bipolar got = ssh_read_owned(SSH_DIGESTS, text, sizeof(text));
        positive at = 0;
        bool server = false;

        digests->count = 0;
        if (got <= 0 || got >= (bipolar)sizeof(text) - 1)
                return false;
        while (at < (positive)got)
        {
                positive stop = at;
                string_address line = text + at;
                string_address second;
                string_address third;
                ssh_digest address_to file;

                while (stop < (positive)got && text[stop] != '\n')
                        stop++;
                text[stop] = end;
                at = stop + 1;
                if (!line[0] || line[0] == '#')
                        continue;
                second = string_first_of(line, ' ');
                third = second ? string_first_of(second + 1, ' ') : null;
                if (!third || second - line != 64 || digests->count == SSH_DIGESTS_MOST ||
                    ssh_span(line, "0123456789abcdef", 64) != 64)
                        return false;
                *second = *third = end;
                file = &digests->files[digests->count++];
                if (string_length(second + 1) >= sizeof(file->bowl) || string_length(third + 1) >= sizeof(file->view) ||
                    (!host_starts(third + 1, "tree/") && !host_starts(third + 1, "lib/")) ||
                    string_find(second + 1, "..") || string_find(third + 1, "..") || second[1] != '/')
                        return false;
                memory_copy(file->sum, line, 64);
                file->sum[64] = end;
                string_copy_bounded(file->bowl, second + 1, sizeof(file->bowl));
                string_copy_bounded(file->view, third + 1, sizeof(file->view));
                server |= string_equals((string_address)file->view, "tree/usr/sbin/sshd");
        }
        return server;
}

// Whether the profile is installed: its marker in the bowl, and the record the install left.
static bool ssh_installed(void)
{
        ssh_digests digests;

        return bowl_profile_marked(SSH_BOWL, SSH_PROFILE) && ssh_digests_read(address_of digests);
}

// A program the record holds, by the view's name: its digest, or null.
static const ssh_digest address_to ssh_digest_of(const ssh_digests address_to digests, string_address view)
{
        for (positive at = 0; at < digests->count; at++)
                if (string_equals((string_address)digests->files[at].view, view))
                        return &digests->files[at];
        return null;
}

/*
        What an ELF program or library names: the loader (PT_INTERP) and its
        DT_NEEDED libraries. Little-endian ELF64 only, which is every machine
        Moonwater runs on; anything else, or a file whose tables are not where
        they say, is false.
*/
#define SSH_NEEDED_MOST 16

typedef struct
{
        p8 loader[96];
        p8 names[SSH_NEEDED_MOST][96];
        positive count;
} ssh_needed;

static bool ssh_read_at(bipolar handle, address_any into, positive length, p64 offset)
{
        return file_transfer_exact(syscall(pread64), handle, into, length, offset) == (bipolar)length;
}

static p64 ssh_word(const p8 address_to at, positive size)
{
        p64 value = 0;

        while (size--)
                value = value << 8 | at[size];
        return value;
}

static bool ssh_elf_needed(bipolar handle, ssh_needed address_to needed, bool address_to elf)
{
        p8 head[64];
        p8 table[56 * 32];
        p8 dynamic[16 * 256];
        p8 name[96];
        p64 phoff, phnum, dynamic_at = 0, dynamic_size = 0, strtab = 0, strsz = 0, strings_at = 0;
        positive used = 0;
        bool have_strtab = false;

        memory_zero(needed, sizeof(address_to needed));
        address_to elf = false;
        if (!ssh_read_at(handle, head, sizeof(head), 0) || memory_compare(head, "\177ELF\2\1", 6))
                return true;
        address_to elf = true;
        phoff = ssh_word(head + 32, 8);
        phnum = ssh_word(head + 56, 2);
        if (ssh_word(head + 54, 2) != 56 || !phnum || phnum > 32 || !ssh_read_at(handle, table, (positive)phnum * 56, phoff))
                return false;
        for (positive at = 0; at < phnum; at++)
        {
                const p8 address_to entry = table + at * 56;
                p64 type = ssh_word(entry, 4), offset = ssh_word(entry + 8, 8), size = ssh_word(entry + 32, 8);

                if (type == 3) // PT_INTERP
                {
                        if (!size || size >= sizeof(needed->loader) || !ssh_read_at(handle, needed->loader, (positive)size, offset) ||
                            needed->loader[size - 1])
                                return false;
                }
                else if (type == 2) // PT_DYNAMIC
                {
                        dynamic_at = offset;
                        dynamic_size = size;
                }
        }
        if (!dynamic_size)
                return true;
        if (dynamic_size > sizeof(dynamic) || dynamic_size % 16 || !ssh_read_at(handle, dynamic, (positive)dynamic_size, dynamic_at))
                return false;
        for (positive at = 0; at + 16 <= dynamic_size; at += 16)
        {
                p64 tag = ssh_word(dynamic + at, 8), value = ssh_word(dynamic + at + 8, 8);

                if (tag == 5) // DT_STRTAB
                {
                        strtab = value;
                        have_strtab = true;
                }
                else if (tag == 10) // DT_STRSZ
                        strsz = value;
                else if (!tag)
                        break;
        }
        if (!have_strtab || !strsz)
                return false;
        //      The table's address is where it is loaded; its place in the file is that of the segment it is in.
        for (positive at = 0; at < phnum && !strings_at; at++)
        {
                const p8 address_to entry = table + at * 56;
                p64 offset = ssh_word(entry + 8, 8), address = ssh_word(entry + 16, 8), size = ssh_word(entry + 32, 8);

                if (ssh_word(entry, 4) == 1 && strtab >= address && strtab + strsz <= address + size)
                        strings_at = strtab - address + offset;
        }
        if (!strings_at)
                return false;
        //      Each name is read where it is, and not the whole table, which a library of thousands of symbols makes large.
        for (positive at = 0; at + 16 <= dynamic_size; at += 16)
        {
                p64 tag = ssh_word(dynamic + at, 8), value = ssh_word(dynamic + at + 8, 8);
                positive room = sizeof(name);
                positive length;

                if (!tag)
                        break;
                if (tag != 1) // DT_NEEDED
                        continue;
                if (value >= strsz || used == SSH_NEEDED_MOST)
                        return false;
                if (strsz - value < room)
                        room = (positive)(strsz - value);
                memory_zero(name, sizeof(name));
                if (!ssh_read_at(handle, name, room, strings_at + value))
                        return false;
                length = string_length(name);
                if (!length || length >= room || string_first_of(name, '/'))
                        return false;
                memory_copy(needed->names[used++], name, length + 1);
        }
        needed->count = used;
        return true;
}

/*
        The apk database's checksum of one file of the bowl, path as the bowl
        names it (no leading slash): Q1 and the SHA-1 in base64, or Q2 and the
        SHA-256. The database is read a line at a time, since a desktop's is
        megabytes: F: names the directory of the R: files after it, and a Z:
        after an R: is that file's checksum.
*/
typedef struct
{
        string_address path;
        p8 sum[48];
        p8 folder[256];
        p8 file[256];
        bool found;
} ssh_apk_ask;

static fn ssh_apk_line(string_address line, positive length, ssh_apk_ask address_to ask)
{
        positive folder = string_length((string_address)ask->folder);

        if (!length)
        {
                ask->folder[0] = ask->file[0] = end;
                return;
        }
        if (length < 2 || line[1] != ':' || length - 2 >= sizeof(ask->folder))
                return;
        if (line[0] == 'F')
        {
                memory_copy(ask->folder, line + 2, length - 2);
                ask->folder[length - 2] = end;
                ask->file[0] = end;
        }
        else if (line[0] == 'R')
        {
                memory_copy(ask->file, line + 2, length - 2);
                ask->file[length - 2] = end;
        }
        else if (line[0] == 'Z' && ask->file[0] && length - 2 < sizeof(ask->sum) && !ask->found)
        {
                positive file = string_length((string_address)ask->file);
                positive path = string_length(ask->path);

                if ((folder ? path == folder + 1 + file && !memory_compare(ask->path, ask->folder, folder) &&
                                  ask->path[folder] == '/' && !memory_compare(ask->path + folder + 1, ask->file, file)
                            : path == file && !memory_compare(ask->path, ask->file, file)))
                {
                        memory_copy(ask->sum, line + 2, length - 2);
                        ask->sum[length - 2] = end;
                        ask->found = true;
                }
                ask->file[0] = end;
        }
}

static bool ssh_apk_checksum(string_address root, string_address path, p8 address_to into, positive room)
{
        static p8 chunk[65536];
        ssh_apk_ask ask;
        positive kept = 0;
        bipolar handle = bowl_open_in_root(root, "/lib/apk/db/installed", FILE_READ | O_NONBLOCK | O_CLOEXEC);
        bipolar got;

        memory_zero(address_of ask, sizeof(ask));
        ask.path = path;
        if (handle < 0)
                return false;
        while ((got = system_read_retry((positive)handle, chunk + kept, sizeof(chunk) - kept)) > 0 && !ask.found)
        {
                positive have = kept + (positive)got, at = 0;

                for (positive stop = 0; stop < have; stop++)
                        if (chunk[stop] == '\n')
                        {
                                ssh_apk_line(chunk + at, stop - at, address_of ask);
                                at = stop + 1;
                        }
                kept = have - at;
                //      A line longer than the chunk is no line of the database's.
                if (kept == sizeof(chunk))
                        kept = 0;
                ssh_slide(chunk, chunk + at, kept);
        }
        system_close(handle);
        if (!ask.found)
                return false;
        string_copy_bounded(into, (string_address)ask.sum, room);
        return true;
}

// The two sums of what a descriptor reads: SHA-1 and SHA-256, in the forms the database and the record keep.
static bool ssh_sums(bipolar handle, p8 address_to sha1, p8 address_to sha256, p8 address_to hex)
{
        static p8 chunk[65536];
        digest_state one, two;
        bipolar got;

        digest_open(address_of one, DIGEST_SHA1, 20);
        digest_open(address_of two, DIGEST_SHA256, 32);
        while ((got = system_read_retry((positive)handle, chunk, sizeof(chunk))) > 0)
        {
                digest_write(address_of one, chunk, (positive)got);
                digest_write(address_of two, chunk, (positive)got);
        }
        digest_close(address_of one, sha1);
        digest_close(address_of two, sha256);
        memory_into_hex(hex, sha256, 32);
        hex[64] = end;
        return got == 0;
}

// Whether the database's checksum (Q1 base64 SHA-1, or Q2 base64 SHA-256) is these sums.
static bool ssh_apk_same(string_address checksum, const p8 address_to sha1, const p8 address_to sha256)
{
        p8 text[48];

        if (host_starts(checksum, "Q1"))
        {
                memory_into_base64(text, sha1, 20);
                return string_equals(checksum + 2, (string_address)text);
        }
        if (host_starts(checksum, "Q2"))
        {
                memory_into_base64(text, sha256, 32);
                return string_equals(checksum + 2, (string_address)text);
        }
        return false;
}

typedef struct
{
        p8 text[SSH_DIGESTS_ROOM];
        p8 seen[SSH_DIGESTS_MOST][128];
        positive count;
} ssh_record;

/*
        One file into the record: opened in the bowl as the bowl names it, its
        real name (the bowl's links followed inside it) checked against the
        database and its digest written with view. needed takes what it names
        when it is an ELF file. A file the database does not have, or has
        another checksum for, is refused by name.
*/
static b32 ssh_record_file(string_address root, string_address path, string_address view, ssh_record address_to record,
                           ssh_needed address_to needed, bool address_to elf)
{
        p8 real[BOWL_PATH_LIMIT];
        p8 checksum[48];
        p8 sha1[20];
        p8 sha256[32];
        p8 hex[65];
        positive root_length = string_length(root);
        bipolar handle = bowl_open_in_root(root, path, FILE_READ | O_NONBLOCK | O_CLOEXEC);
        bool summed;

        if (handle < 0)
                return host_fail(path, handle);
        if (bowl_handle_path(handle, real, sizeof(real)) <= (bipolar)root_length ||
            string_compare_max(real, root, root_length) || real[root_length] != '/' ||
            string_length(real + root_length) >= sizeof(record->seen[0]))
        {
                system_close(handle);
                return host_refuse("%s is not a file of the bowl\n", path);
        }
        if (!ssh_elf_needed(handle, needed, elf))
        {
                system_close(handle);
                return host_refuse("%s is not an ELF file this machine can read\n", path);
        }
        summed = ssh_sums(handle, sha1, sha256, hex);
        system_close(handle);
        if (!summed)
                return host_refuse("%s could not be read\n", path);
        if (!ssh_apk_checksum(root, real + root_length + 1, checksum, sizeof(checksum)))
                return host_refuse("%s is in no package of the bowl's apk database\n", real + root_length);
        if (!ssh_apk_same(checksum, sha1, sha256))
                return host_refuse("%s is not the file its package installed (the apk database has another checksum)\n",
                                   real + root_length);
        if (record->count == SSH_DIGESTS_MOST)
                return host_refuse("the server needs more files than the record holds\n");
        string_copy_bounded(record->seen[record->count++], real + root_length, sizeof(record->seen[0]));
        string_append_bounded(record->text, (string_address)hex, sizeof(record->text));
        string_append_bounded(record->text, " ", sizeof(record->text));
        string_append_bounded(record->text, real + root_length, sizeof(record->text));
        string_append_bounded(record->text, " ", sizeof(record->text));
        string_append_bounded(record->text, view, sizeof(record->text));
        string_append_bounded(record->text, "\n", sizeof(record->text));
        return 0;
}

// A library the loader is asked for by name: where musl finds it in the bowl (/lib, /usr/local/lib, /usr/lib), or false.
static bool ssh_library(string_address root, string_address name, p8 address_to into, positive room)
{
        static const string_address places[] = {"/lib/", "/usr/local/lib/", "/usr/lib/"};

        for (positive at = 0; at < array_count(places); at++)
        {
                bipolar handle;

                if (!host_join(into, room, places[at], name))
                        return false;
                handle = bowl_open_in_root(root, into, O_PATH | O_CLOEXEC);
                if (handle >= 0)
                {
                        system_close(handle);
                        return true;
                }
        }
        return false;
}

// Whether a library's name is musl's own libc, which the loader is and never looks for.
static bool ssh_libc_named(string_address name, string_address loader)
{
        return host_starts(name, "libc.") && string_find(loader, "ld-musl-");
}

/*
        The files of a package in the database: every R: of the package P:
        named, with its F:, into paths (with a leading slash), at most most.
*/
typedef struct
{
        string_address package;
        p8 (address_to paths)[128];
        positive most;
        positive count;
        p8 folder[128];
        bool inside;
} ssh_apk_files;

static fn ssh_apk_files_line(string_address line, positive length, ssh_apk_files address_to files)
{
        positive name = string_length(files->package);

        if (!length)
        {
                files->inside = false;
                files->folder[0] = end;
                return;
        }
        if (length < 2 || line[1] != ':')
                return;
        if (line[0] == 'P')
                files->inside = length - 2 == name && !memory_compare(line + 2, files->package, name);
        else if (line[0] == 'F' && files->inside && length - 2 < sizeof(files->folder))
        {
                memory_copy(files->folder, line + 2, length - 2);
                files->folder[length - 2] = end;
        }
        else if (line[0] == 'R' && files->inside && files->count < files->most)
        {
                p8 file[128];
                p8 address_to path = files->paths[files->count];

                if (length - 2 >= sizeof(file))
                        return;
                memory_copy(file, line + 2, length - 2);
                file[length - 2] = end;
                if (host_join(path, 128, "/", (string_address)files->folder, files->folder[0] ? "/" : "",
                              (string_address)file))
                        files->count++;
        }
}

static positive ssh_apk_package(string_address root, string_address package, p8 (address_to paths)[128], positive most)
{
        static p8 chunk[65536];
        ssh_apk_files files;
        positive kept = 0;
        bipolar handle = bowl_open_in_root(root, "/lib/apk/db/installed", FILE_READ | O_NONBLOCK | O_CLOEXEC);
        bipolar got;

        memory_zero(address_of files, sizeof(files));
        files.package = package;
        files.paths = paths;
        files.most = most;
        if (handle < 0)
                return 0;
        while ((got = system_read_retry((positive)handle, chunk + kept, sizeof(chunk) - kept)) > 0)
        {
                positive have = kept + (positive)got, at = 0;

                for (positive stop = 0; stop < have; stop++)
                        if (chunk[stop] == '\n')
                        {
                                ssh_apk_files_line(chunk + at, stop - at, address_of files);
                                at = stop + 1;
                        }
                kept = have - at;
                if (kept == sizeof(chunk))
                        kept = 0;
                ssh_slide(chunk, chunk + at, kept);
        }
        system_close(handle);
        return files.count;
}

static bool ssh_record_has(const ssh_record address_to record, string_address view)
{
        return string_find((string_address)record->text, view) != null;
}

/*
        What `bowl profile ssh install` adds after apk: the record, and the
        settings off. Nothing is started: the server is off until somebody
        turns it on.
*/
static b32 ssh_profile_record(string_address root)
{
        static ssh_record record;
        static p8 paths[16][128];
        static const string_address packages[] = {"openssh-server", "openssh-keygen"};
        p8 loader[96] = {0};
        p8 libraries[SSH_NEEDED_MOST][96];
        positive library_count = 0;
        ssh_settings settings;
        bipolar failed;

        memory_zero(address_of record, sizeof(record));
        string_copy_bounded(record.text,
                            "# bowl profile ssh: the files sshd runs from, each checked against the checksum of its package at\n"
                            "# install, by their SHA-256; a start copies each and refuses on one that is not this.\n",
                            sizeof(record.text));
        for (positive package = 0; package < array_count(packages); package++)
        {
                positive count = ssh_apk_package(root, packages[package], paths, array_count(paths));

                if (!count)
                        return host_refuse("the bowl's apk database has no %s\n", packages[package]);
                for (positive at = 0; at < count; at++)
                {
                        ssh_needed needed;
                        bool elf = false;
                        p8 view[160];
                        b32 refused;

                        //      Only the programs: the package's man pages and scripts are nothing sshd runs.
                        if (!host_starts(paths[at], "/usr/sbin/") && !host_starts(paths[at], "/usr/bin/") &&
                            !host_starts(paths[at], "/usr/lib/") && !host_starts(paths[at], "/usr/libexec/"))
                                continue;
                        if (!host_join(view, sizeof(view), "tree", (string_address)paths[at]))
                                return host_refuse("%s has too long a name\n", paths[at]);
                        //      Not an ELF file (a script, data): nothing sshd executes, and not copied.
                        {
                                bipolar probe = bowl_open_in_root(root, (string_address)paths[at], FILE_READ | O_NONBLOCK | O_CLOEXEC);
                                bool program = false;

                                if (probe >= 0)
                                {
                                        program = ssh_elf_needed(probe, address_of needed, address_of elf) && elf;
                                        system_close(probe);
                                }
                                if (!program)
                                        continue;
                        }
                        refused = ssh_record_file(root, (string_address)paths[at], (string_address)view, address_of record,
                                                  address_of needed, address_of elf);
                        if (refused)
                                return refused;
                        if (needed.loader[0])
                        {
                                if (loader[0] && !string_equals((string_address)loader, (string_address)needed.loader))
                                        return host_refuse("the programs of the server name two loaders\n");
                                string_copy_bounded(loader, (string_address)needed.loader, sizeof(loader));
                        }
                        for (positive name = 0; name < needed.count; name++)
                        {
                                bool known = false;

                                for (positive other = 0; other < library_count; other++)
                                        known |= string_equals((string_address)libraries[other], (string_address)needed.names[name]);
                                if (!known && library_count < SSH_NEEDED_MOST)
                                        string_copy_bounded(libraries[library_count++], (string_address)needed.names[name], 96);
                        }
                }
        }
        if (!ssh_record_has(address_of record, "tree/usr/sbin/sshd") || !ssh_record_has(address_of record, "tree/usr/bin/ssh-keygen"))
                return host_refuse("the packages hold no /usr/sbin/sshd or no /usr/bin/ssh-keygen\n");
        if (!host_starts(loader, "/lib/") || string_first_of((string_address)loader + 5, '/'))
                return host_refuse("the loader of the server is not in /lib: %s\n", loader[0] ? (string_address)loader : (string_address) "none");
        {
                ssh_needed needed;
                bool elf = false;
                p8 view[160];
                b32 refused;

                (void)string_join(view, sizeof(view), "lib/", (string_address)loader + 5);
                refused = ssh_record_file(root, (string_address)loader, (string_address)view, address_of record,
                                          address_of needed, address_of elf);
                if (refused)
                        return refused;
        }
        //      Every library, and the ones those name, each once.
        for (positive at = 0; at < library_count; at++)
        {
                p8 where[160];
                p8 view[160];
                ssh_needed needed;
                bool elf = false;
                b32 refused;

                if (ssh_libc_named((string_address)libraries[at], (string_address)loader))
                        continue;
                if (!ssh_library(root, (string_address)libraries[at], where, sizeof(where)))
                        return host_refuse("the bowl has no %s, which the server maps\n", libraries[at]);
                (void)string_join(view, sizeof(view), "lib/", (string_address)libraries[at]);
                refused = ssh_record_file(root, (string_address)where, (string_address)view, address_of record,
                                          address_of needed, address_of elf);
                if (refused)
                        return refused;
                for (positive name = 0; name < needed.count; name++)
                {
                        bool known = false;

                        for (positive other = 0; other < library_count; other++)
                                known |= string_equals((string_address)libraries[other], (string_address)needed.names[name]);
                        if (!known && library_count < SSH_NEEDED_MOST)
                                string_copy_bounded(libraries[library_count++], (string_address)needed.names[name], 96);
                }
        }
        failed = ssh_state_ready();
        if (failed >= 0)
                failed = host_write_file(SSH_DIGESTS, record.text, string_length((string_address)record.text), 0600, true);
        if (failed < 0)
                return host_fail(SSH_DIGESTS, failed);
        //      Installed is off, whatever a file from before says.
        (void)ssh_settings_read(address_of settings);
        settings.on = false;
        settings.until = 0;
        settings.boot[0] = end;
        failed = ssh_settings_save(address_of settings);
        if (failed < 0)
                return host_fail(SSH_SETTINGS, failed);
        ssh_log_change("installed: the server's files recorded, the server off");
        host_say(log, host_label "ssh: %p files of the server recorded by their SHA-256; it is off: moonwater ssh on\n",
                 record.count);
        return 0;
}

/* ---- processes ---- */

/*
        A process as /proc says it: its parent, its name and when it started
        (in clock ticks after the boot). False when it is gone.
*/
static bool ssh_process(positive pid, positive address_to parent, p8 address_to name, positive room, p64 address_to started)
{
        p8 path[48];
        p8 text[1024];
        p8 number[24];
        bipolar got;
        string_address open;
        string_address close;
        positive field = 2;

        number[positive_into_string(number, pid)] = end;
        (void)string_join(path, sizeof(path), "/proc/", (string_address)number, "/stat");
        got = file_slurp_once_at(AT_FDCWD, (string_address)path, text, sizeof(text) - 1);
        if (got <= 0)
                return false;
        text[got] = end;
        open = string_first_of(text, '(');
        close = null;
        for (string_address at = text; *at; at++)
                if (*at == ')')
                        close = at;
        if (!open || !close || close < open)
                return false;
        if (name)
        {
                positive length = (positive)(close - open - 1);

                if (length >= room)
                        length = room - 1;
                memory_copy(name, open + 1, length);
                name[length] = end;
        }
        address_to parent = 0;
        if (started)
                address_to started = 0;
        for (string_address at = close + 1; *at; at++)
        {
                if (*at != ' ')
                        continue;
                field++;
                if (field == 4)
                        address_to parent = (positive)string_digits(at + 1, null);
                else if (field == 22 && started)
                {
                        address_to started = (p64)string_digits(at + 1, null);
                        break;
                }
        }
        return true;
}

static bool ssh_named_server(string_address name)
{
        return string_equals(name, "sshd") || string_equals(name, "sshd-session") || string_equals(name, "sshd-auth");
}

// Whether pid is ancestor or has it among its parents.
static bool ssh_descends(positive pid, positive ancestor)
{
        for (positive depth = 0; pid > 1 && depth < 64; depth++)
        {
                positive parent = 0;

                if (pid == ancestor)
                        return true;
                if (!ssh_process(pid, &parent, null, 0, null))
                        return false;
                pid = parent;
        }
        return pid == ancestor && ancestor;
}

// A signal for pid, and never for a process that took its number since it was looked at as named.
static bool ssh_signal(positive pid, b32 signal, string_address named)
{
        bipolar handle = system_call_2(syscall(pidfd_open), pid, 0);
        p8 name[32];
        positive parent;
        bool sent = false;

        if (handle < 0)
                return false;
        if (!named || (ssh_process(pid, &parent, name, sizeof(name), null) && string_equals((string_address)name, named)))
                sent = system_call_4(syscall(pidfd_send_signal), (positive)handle, (positive)signal, 0, 0) >= 0;
        system_close(handle);
        return sent;
}

// The pid in the record of the server running (its listener), and the digest of the file it was started with.
static positive ssh_running(p8 address_to sum, positive room)
{
        p8 text[128];
        bipolar got = ssh_read_owned(SSH_RUNNING, text, sizeof(text));
        string_address blank;
        positive pid;

        if (sum)
                sum[0] = end;
        if (got <= 0 || host_lock_owner(SSH_LOCK) <= 0)
                return 0;
        blank = string_first_of(text, ' ');
        if (!blank)
                return 0;
        pid = (positive)string_digits(text, null);
        if (sum)
        {
                string_copy_bounded(sum, blank + 1, room);
                if (string_first_of(sum, '\n'))
                        *string_first_of(sum, '\n') = end;
        }
        return pid;
}

static fn ssh_config_sum(string_address text, positive length, p8 address_to hex)
{
        digest_state digest;
        p8 sum[32];

        digest_open(address_of digest, DIGEST_SHA256, 32);
        digest_write(address_of digest, (address_any)text, length);
        digest_close(address_of digest, sum);
        memory_into_hex(hex, sum, sizeof(sum));
        hex[64] = end;
}

/*
        The sessions: every child of the listener, which is one connection's
        privileged half each. Who, from what address and port and since when;
        with end, each is ended (its terminal hangs up, and what it ran is
        told so as after any disconnection). The count.
*/
typedef struct
{
        p8 who[40];
        p8 from[64];
        p8 since[16];
} ssh_session;

static positive ssh_btime(void)
{
        p8 text[4096];
        bipolar got = file_slurp_once_at(AT_FDCWD, "/proc/stat", text, sizeof(text) - 1);
        string_address at;

        if (got <= 0)
                return 0;
        text[got] = end;
        at = string_find(text, "\nbtime ");
        return at ? (positive)string_digits(at + 7, null) : 0;
}

// The remote end of a socket inode, from /proc/net/tcp or tcp6: "ADDRESS port PORT", or false.
static bool ssh_peer_of(p64 inode, p8 address_to into, positive room)
{
        static p8 text[262144];
        static const string_address tables[] = {"/proc/net/tcp", "/proc/net/tcp6"};

        for (positive table = 0; table < array_count(tables); table++)
        {
                bipolar got = file_slurp_once_at(AT_FDCWD, tables[table], text, sizeof(text) - 1);
                positive at = 0;

                if (got <= 0)
                        continue;
                text[got] = end;
                while (at < (positive)got)
                {
                        positive stop = at;
                        string_address fields[10];
                        positive count = 0;

                        while (stop < (positive)got && text[stop] != '\n')
                                stop++;
                        text[stop] = end;
                        for (string_address word = text + at; *word && count < 10;)
                        {
                                while (*word == ' ')
                                        word++;
                                if (!*word)
                                        break;
                                fields[count++] = word;
                                while (*word && *word != ' ')
                                        word++;
                        }
                        at = stop + 1;
                        if (count < 10 || (p64)string_digits(fields[9], null) != inode)
                                continue;
                        {
                                string_address remote = fields[2];
                                string_address colon = string_first_of(remote, ':');
                                p8 port[8];
                                positive hex = colon ? (positive)(colon - remote) : 0;
                                p8 address[48];
                                p8 bytes[16];
                                positive digit_count = 0;

                                if (hex != 8 && hex != 32)
                                        return false;
                                for (positive byte = 0; byte < hex / 2; byte++)
                                {
                                        positive high = ssh_hex(remote[byte * 2]);
                                        positive low = ssh_hex(remote[byte * 2 + 1]);

                                        //      The kernel writes each 32-bit word in the host's order, which is little-endian here.
                                        bytes[(byte & ~3u) + 3 - (byte & 3)] = (p8)(high << 4 | low);
                                }
                                if (hex == 8 || (hex == 32 && !memory_compare(bytes, "\0\0\0\0\0\0\0\0\0\0\377\377", 12)))
                                {
                                        p8 address_to quad = hex == 8 ? bytes : bytes + 12;
                                        p8 part[4];

                                        address[0] = end;
                                        for (positive byte = 0; byte < 4; byte++)
                                        {
                                                part[positive_into_string(part, quad[byte])] = end;
                                                string_append_bounded(address, (string_address)part, sizeof(address));
                                                if (byte < 3)
                                                        string_append_bounded(address, ".", sizeof(address));
                                        }
                                }
                                else
                                {
                                        static const p8 digits[] = "0123456789abcdef";
                                        positive used = 0;

                                        for (positive byte = 0; byte < 16; byte++)
                                        {
                                                address[used++] = digits[bytes[byte] >> 4];
                                                address[used++] = digits[bytes[byte] & 15];
                                                if (byte & 1 && byte < 15)
                                                        address[used++] = ':';
                                        }
                                        address[used] = end;
                                }
                                digit_count = ssh_hex(colon[1]) << 12 | ssh_hex(colon[2]) << 8 |
                                              ssh_hex(colon[3]) << 4 | ssh_hex(colon[4]);
                                port[positive_into_string(port, digit_count)] = end;
                                (void)string_join(into, room, (string_address)address, " port ", (string_address)port);
                                return true;
                        }
                }
        }
        return false;
}

// The socket inodes a process holds, the first most of them.
static positive ssh_sockets_of(positive pid, p64 address_to inodes, positive most)
{
        p8 path[48];
        p8 number[24];
        file_walk walk;
        struct linux_dirent64 address_to entry;
        positive count = 0;

        number[positive_into_string(number, pid)] = end;
        (void)string_join(path, sizeof(path), "/proc/", (string_address)number, "/fd");
        if (!file_walk_open(address_of walk, AT_FDCWD, (string_address)path))
                return 0;
        while ((entry = file_walk_next(address_of walk)) && count < most)
        {
                p8 link[64];
                p8 where[96];

                if (file_is_dot(entry->d_name) || !host_join(where, sizeof(where), (string_address)path, "/", entry->d_name) ||
                    file_link_text((string_address)where, link, sizeof(link)) <= 0 || !host_starts(link, "socket:["))
                        continue;
                inodes[count++] = (p64)string_digits(link + 8, null);
        }
        file_walk_close(address_of walk);
        return count;
}

/*
        The rows a status prints, held in memory that grows as sessions are
        found: the first held of room are rows, and a row that could not be
        held (no memory for it) is counted all the same.
*/
typedef struct
{
        ssh_session address_to rows;
        positive room, held;
} ssh_list;

static bool ssh_list_add(ssh_list address_to list, const ssh_session address_to row)
{
        if (list->held == list->room &&
            !memory_reserve((address_any address_to)&list->rows, &list->room, list->held, list->held + 1,
                            sizeof(ssh_session), 16))
                return false;
        list->rows[list->held++] = *row;
        return true;
}

static positive ssh_sessions(positive listener, ssh_list address_to list, bool ended)
{
        file_walk walk;
        struct linux_dirent64 address_to entry;
        positive count = 0;
        positive ticks_at = ssh_btime();

        if (!listener || !file_walk_open(address_of walk, AT_FDCWD, "/proc"))
                return 0;
        while ((entry = file_walk_next(address_of walk)))
        {
                positive pid = (positive)string_digits(entry->d_name, null);
                positive parent = 0;
                p8 name[32];
                p64 started = 0;

                if (!pid || !ssh_process(pid, &parent, name, sizeof(name), &started) || parent != listener ||
                    !ssh_named_server((string_address)name))
                        continue;
                if (ended)
                {
                        count += ssh_signal(pid, 15, (string_address)name);
                        continue;
                }
                if (list)
                {
                        ssh_session row;
                        ssh_session address_to session = &row;
                        p8 path[48];
                        p8 number[24];
                        p8 title[128];
                        p64 inodes[8];
                        positive held;
                        bipolar got;
                        time_t stamp = (time_t)(ticks_at + started / 100);
                        tm broken;

                        number[positive_into_string(number, pid)] = end;
                        (void)string_join(path, sizeof(path), "/proc/", (string_address)number, "/cmdline");
                        got = file_slurp_once_at(AT_FDCWD, (string_address)path, title, sizeof(title) - 1);
                        title[got > 0 ? got : 0] = end;
                        //      sshd-session: root [priv], or the address while it is not yet anyone.
                        string_copy_bounded(session->who, "connecting", sizeof(session->who));
                        if (host_starts(title, "sshd-session: ") && !string_find(title, "[preauth]") &&
                            !string_find(title, "[net]"))
                        {
                                string_address who = title + 14;
                                positive length = 0;

                                while (who[length] && who[length] != ' ' && who[length] != '@' && length + 1 < sizeof(session->who))
                                        length++;
                                if (length && ssh_span(who, "abcdefghijklmnopqrstuvwxyz0123456789_-", length) == length)
                                {
                                        memory_copy(session->who, who, length);
                                        session->who[length] = end;
                                }
                        }
                        string_copy_bounded(session->from, "an address not known", sizeof(session->from));
                        held = ssh_sockets_of(pid, inodes, array_count(inodes));
                        for (positive at = 0; at < held; at++)
                                if (ssh_peer_of(inodes[at], session->from, sizeof(session->from)))
                                        break;
                        localtime_r(address_of stamp, address_of broken);
                        strftime(session->since, sizeof(session->since), "%H:%M:%S", address_of broken);
                        (void)ssh_list_add(list, session);
                }
                count++;
        }
        file_walk_close(address_of walk);
        return count;
}

/*
        What runs that is no server of Moonwater's, ended: every sshd,
        sshd-session and sshd-auth that does not descend from the listener
        Moonwater started (none when it is off), and every process that holds
        a socket listening on the port and is not that listener. Each is
        ended with SIGKILL and said in the log. The count.
*/
static positive ssh_listening(p16 port, p64 address_to inodes, positive most)
{
        static p8 text[262144];
        static const string_address tables[] = {"/proc/net/tcp", "/proc/net/tcp6"};
        positive count = 0;

        for (positive table = 0; table < array_count(tables); table++)
        {
                bipolar got = file_slurp_once_at(AT_FDCWD, tables[table], text, sizeof(text) - 1);
                positive at = 0;

                if (got <= 0)
                        continue;
                text[got] = end;
                while (at < (positive)got && count < most)
                {
                        positive stop = at;
                        string_address fields[10];
                        positive words = 0;

                        while (stop < (positive)got && text[stop] != '\n')
                                stop++;
                        text[stop] = end;
                        for (string_address word = text + at; *word && words < 10;)
                        {
                                while (*word == ' ')
                                        word++;
                                if (!*word)
                                        break;
                                fields[words++] = word;
                                while (*word && *word != ' ')
                                        word++;
                        }
                        at = stop + 1;
                        if (words < 10 || !string_equals(fields[3], "0A") || !string_first_of(fields[1], ':'))
                                continue;
                        {
                                string_address colon = string_first_of(fields[1], ':');
                                positive here = ssh_hex(colon[1]) << 12 | ssh_hex(colon[2]) << 8 |
                                                 ssh_hex(colon[3]) << 4 | ssh_hex(colon[4]);

                                if (here == port)
                                        inodes[count++] = (p64)string_digits(fields[9], null);
                        }
                }
        }
        return count;
}

static positive ssh_enforce(const ssh_settings address_to settings, positive ancestor, positive listener)
{
        file_walk walk;
        struct linux_dirent64 address_to entry;
        p64 listening[16];
        p64 ours[32];
        positive heard = ssh_listening(settings->port, listening, array_count(listening));
        positive own = listener ? ssh_sockets_of(listener, ours, array_count(ours)) : 0;
        positive stray = 0;
        positive ended = 0;

        //      The sockets on the port that are the listener's own are no stray.
        for (positive at = 0; at < heard; at++)
        {
                bool mine = false;

                for (positive other = 0; other < own; other++)
                        mine |= listening[at] == ours[other];
                if (!mine)
                        listening[stray++] = listening[at];
        }
        if (!file_walk_open(address_of walk, AT_FDCWD, "/proc"))
                return 0;
        while ((entry = file_walk_next(address_of walk)))
        {
                positive pid = (positive)string_digits(entry->d_name, null);
                positive parent = 0;
                p8 name[32];
                p8 line[SSH_LOG_LINE];
                p8 number[24];
                bool foreign = false;

                //      Nothing of the supervisor's is a stray: the server it is starting (sshd -T, a key being made) included.
                if (pid <= 1 || pid == (positive)system_call(syscall(getpid)) ||
                    !ssh_process(pid, &parent, name, sizeof(name), null) || parent == 2 || pid == 2 ||
                    (ancestor && ssh_descends(pid, ancestor)))
                        continue;
                foreign = ssh_named_server((string_address)name);
                if (!foreign && stray)
                {
                        p64 held[64];
                        positive count = ssh_sockets_of(pid, held, array_count(held));

                        for (positive at = 0; at < count && !foreign; at++)
                                for (positive other = 0; other < stray && !foreign; other++)
                                        foreign = held[at] == listening[other];
                }
                if (!foreign || !ssh_signal(pid, 9, (string_address)name))
                        continue;
                ended++;
                number[positive_into_string(number, pid)] = end;
                (void)string_join(line, sizeof(line), "ended ", (string_address)name, " ", (string_address)number,
                                  ": a server Moonwater did not start, or a program on its port");
                ssh_log_line((string_address)line, true);
        }
        file_walk_close(address_of walk);
        return ended;
}

/* ---- the server's view, and its start ---- */

// A line of the child's own on its standard error, which is the log's: the supervisor writes it there.
static fn ssh_child_say(string_address first, string_address second)
{
        p8 line[SSH_LOG_LINE];

        (void)string_join(line, sizeof(line) - 1, "moonwater: ", first, second ? second : (string_address) "", "\n");
        (void)system_write_all_checked(2, (const address_any)line, string_length((string_address)line));
}

static DEAD_END fn ssh_child_refuse(string_address first, string_address second, positive status)
{
        ssh_child_say(first, second);
        system_call_1(syscall(exit_group), status);
        for (;;)
                ;
}

// A file of the tmpfs, made with its directories: a copy of one of the bowl's, hashed as it is copied.
static bool ssh_stage(const ssh_digest address_to file)
{
        static p8 chunk[65536];
        p8 path[BOWL_PATH_LIMIT];
        p8 parent[BOWL_PATH_LIMIT];
        p8 hex[65];
        p8 sum[32];
        digest_state digest;
        bipolar from;
        bipolar into;
        bipolar got;
        bool whole = true;

        if (!host_join(path, sizeof(path), SSH_VIEW "/", (string_address)file->view))
                return false;
        string_copy_bounded(parent, (string_address)path, sizeof(parent));
        if (string_last_of(parent, '/'))
                *string_last_of(parent, '/') = end;
        if (bowl_mkdir_parents(parent) < 0)
                return false;
        from = bowl_open_in_root(SSH_BOWL, (string_address)file->bowl, FILE_READ | O_NONBLOCK | O_CLOEXEC);
        if (from < 0)
                return false;
        into = system_open_at_mode(AT_FDCWD, (string_address)path, FILE_WRITE | FILE_EXCLUSIVE | O_CLOEXEC | O_NOFOLLOW, 0755);
        if (into < 0)
        {
                system_close(from);
                return false;
        }
        digest_open(address_of digest, DIGEST_SHA256, 32);
        while ((got = system_read_retry((positive)from, chunk, sizeof(chunk))) > 0)
        {
                digest_write(address_of digest, chunk, (positive)got);
                whole &= system_write_all((positive)into, chunk, (positive)got) == (positive)got;
        }
        system_close(from);
        system_close(into);
        digest_close(address_of digest, sum);
        memory_into_hex(hex, sum, sizeof(sum));
        hex[64] = end;
        return whole && got == 0 && string_equals((string_address)hex, (string_address)file->sum);
}

// A small file of the tmpfs, written whole.
static bool ssh_stage_text(string_address path, string_address text)
{
        bipolar into = system_open_at_mode(AT_FDCWD, path, FILE_WRITE | FILE_EXCLUSIVE | O_CLOEXEC | O_NOFOLLOW, 0644);
        bool whole;

        if (into < 0)
                return false;
        whole = system_write_all((positive)into, text, string_length(text)) == string_length(text);
        system_close(into);
        return whole;
}

// Bound read-only over target, which is made where it is missing (a file for a file).
static bool ssh_bind(string_address source, string_address target, bool directory)
{
        if (system_access_at(AT_FDCWD, target, 0) < 0)
        {
                if (directory ? bowl_mkdir_parents(target) < 0 : !ssh_stage_text(target, ""))
                        return false;
        }
        return bowl_bind_ro(source, target) == 0;
}

// The tmpfs's tree over a directory of the machine: an overlay, the copies on top.
static bool ssh_overlay(string_address upper, string_address target)
{
        p8 options[BOWL_PATH_LIMIT];

        if (bowl_mkdir_parents(target) < 0 || !host_join(options, sizeof(options), "lowerdir=", upper, ":", target))
                return false;
        return system_mount("overlay", target, "overlay", MS_RDONLY, (string_address)options) == 0;
}

// A program of the copies, run in the view with an empty environment, its output into the descriptor given (-1 for none): its status.
static positive ssh_run(string_address address_to words, bipolar output)
{
        string_address empty[] = {null};
        positive status = 0;
        bipolar child = system_fork();

        if (child < 0)
                return 255;
        if (!child)
        {
                //      A program whose output could not be given it is not run.
                if (output >= 0 && system_call_3(syscall(dup3), (positive)output, 1, 0) < 0)
                        system_call_1(syscall(exit_group), 127);
                (void)descriptors_close_except(3, descriptors_none, descriptors_none);
                system_execute(words[0], words, empty);
                system_call_1(syscall(exit_group), 127);
        }
        if (system_wait4_retry(child, address_of status, 0, null) < 0)
                return 255;
        return wait_status_code(status);
}

/*
        The view, in the child the supervisor made: it ends in sshd, or the
        child ends with a line that says why. Status 2 is a view that could
        not be made, 3 a file that is not its record's, 4 a configuration
        sshd would not take as it was written.
*/
static DEAD_END fn ssh_view_enter(const ssh_settings address_to settings)
{
        static ssh_digests digests;
        static p8 told[SSH_TOLD_ROOM];
        static ssh_keys keys;
        string_address empty[] = {null};
        p8 wrong[64];
        bipolar space;
        b32 pipe_ends[2];
        positive got = 0;

        if (!ssh_digests_read(address_of digests))
                ssh_child_refuse("the profile is not installed: moonwater bowl profile ssh install", null, 3);
        if (!ssh_keys_read(address_of keys))
                ssh_child_refuse("refused: " SSH_KEYS " is not the list moonwater ssh writes", null, 3);
        system_call_1(syscall(umask), 022);
        //      The machine's own tree, whatever namespace this was asked from.
        space = system_open_at(AT_FDCWD, "/proc/1/ns/mnt", FILE_READ | O_CLOEXEC);
        if (space < 0 || system_call_2(syscall(setns), (positive)space, CLONE_NEWNS) < 0 ||
            system_call_1(syscall(chdir), (positive)(string_address) "/") < 0)
                ssh_child_refuse("refused: the machine's mount namespace could not be entered", null, 2);
        system_close(space);
        if (system_call_1(syscall(unshare), CLONE_NEWNS) < 0 || system_mount(0, "/", 0, MS_REC | MS_SLAVE, 0) < 0)
                ssh_child_refuse("refused: no mount namespace could be made for the server", null, 2);
        bowl_session_identity();
        if (bowl_mkdir_parents(SSH_VIEW) < 0 ||
            system_mount("tmpfs", SSH_VIEW, "tmpfs", MS_NOSUID | MS_NODEV, "mode=0700,size=128m") < 0)
                ssh_child_refuse("refused: the server's tmpfs could not be made", null, 2);
        for (positive at = 0; at < digests.count; at++)
                if (!ssh_stage(&digests.files[at]))
                        ssh_child_refuse("refused: this file of the Alpine bowl is not the one the profile recorded: ",
                                         (string_address)digests.files[at].bowl, 3);
        if (bowl_mkdir_parents(SSH_VIEW "/etc") < 0 || bowl_mkdir_parents(SSH_VIEW "/empty") < 0 ||
            !ssh_stage_text(SSH_VIEW "/etc/passwd", "root:x:0:0:root:/root:/bin/sh\n"
                                                    "sshd:x:" SSH_PRIVSEP_TEXT ":" SSH_PRIVSEP_TEXT ":sshd:/var/empty:/sbin/nologin\n") ||
            !ssh_stage_text(SSH_VIEW "/etc/group", "root:x:0:\nsshd:x:" SSH_PRIVSEP_TEXT ":\n"))
                ssh_child_refuse("refused: the accounts of the server could not be written", null, 2);
        (void)system_change_mode_at(AT_FDCWD, SSH_VIEW "/empty", 0755);
        if (!ssh_overlay(SSH_VIEW "/lib", "/lib") ||
            (system_access_at(AT_FDCWD, SSH_VIEW "/tree/usr/lib", 0) >= 0 && !ssh_overlay(SSH_VIEW "/tree/usr/lib", "/usr/lib")) ||
            (system_access_at(AT_FDCWD, SSH_VIEW "/tree/usr/libexec", 0) >= 0 &&
             !ssh_overlay(SSH_VIEW "/tree/usr/libexec", "/usr/libexec")) ||
            !ssh_bind(SSH_VIEW "/etc/passwd", "/etc/passwd", false) || !ssh_bind(SSH_VIEW "/etc/group", "/etc/group", false) ||
            (system_access_at(AT_FDCWD, "/etc/ssh", 0) >= 0 && !ssh_bind(SSH_VIEW "/empty", "/etc/ssh", true)) ||
            !ssh_bind(SSH_VIEW "/empty", "/var/empty", true) ||
            system_mount(0, SSH_VIEW, 0, MS_BIND | MS_REMOUNT | MS_RDONLY | MS_NOSUID | MS_NODEV, 0) < 0)
                ssh_child_refuse("refused: the server's view could not be made", null, 2);

        if (system_access_at(AT_FDCWD, SSH_HOST_KEY, 0) < 0)
        {
                string_address make[] = {SSH_VIEW "/tree/usr/bin/ssh-keygen", "-q", "-t", "ed25519", "-N", "",
                                         "-C", "moonwater", "-f", SSH_HOST_KEY, null};
                p8 fingerprint[64];

                system_remove_at(AT_FDCWD, SSH_HOST_PUB, 0);
                if (ssh_run(make, -1) || !ssh_host_fingerprint(fingerprint))
                        ssh_child_refuse("refused: the host key could not be made", null, 2);
                ssh_child_say("the host key was made: ed25519 ", (string_address)fingerprint);
        }

        //      What sshd says it will do with the file, against what the settings decide.
        if (system_pipe(pipe_ends, O_CLOEXEC) < 0)
                ssh_child_refuse("refused: no pipe for sshd -T", null, 2);
        {
                string_address test[] = {SSH_VIEW "/tree/usr/sbin/sshd", "-T", "-f", SSH_CONFIG, null};
                bipolar child = system_fork();
                positive status = 0;
                bipolar read;

                if (child < 0)
                        ssh_child_refuse("refused: sshd -T could not be started", null, 2);
                if (!child)
                {
                        //      With no pipe for what it says, sshd -T says nothing, which refuses the start below.
                        if (system_call_3(syscall(dup3), (positive)pipe_ends[1], 1, 0) < 0)
                                system_call_1(syscall(exit_group), 127);
                        (void)descriptors_close_except(3, descriptors_none, descriptors_none);
                        system_execute(test[0], test, empty);
                        system_call_1(syscall(exit_group), 127);
                }
                system_close(pipe_ends[1]);
                while (got + 1 < sizeof(told) &&
                       (read = system_read_retry((positive)pipe_ends[0], told + got, sizeof(told) - 1 - got)) > 0)
                        got += (positive)read;
                system_close(pipe_ends[0]);
                if (system_wait4_retry(child, address_of status, 0, null) < 0 || wait_status_code(status))
                        ssh_child_refuse("refused: sshd -T does not take the file moonwater wrote", null, 4);
                told[got] = end;
        }
        if (!ssh_config_verify(settings, SSH_STATE, told, got, wrong, sizeof(wrong)))
                ssh_child_refuse("refused: sshd -T does not say what the settings decide for ", (string_address)wrong, 4);

        {
                string_address serve[] = {SSH_VIEW "/tree/usr/sbin/sshd", "-D", "-e", "-f", SSH_CONFIG, null};

                (void)descriptors_close_except(3, descriptors_none, descriptors_none);
                system_execute(serve[0], serve, empty);
        }
        ssh_child_refuse("refused: sshd could not be executed", null, 2);
}

/*
        `moonwater ssh serve [FD]`: the supervisor. It holds the lock for as
        long as the server it starts lives, writes the configuration from the
        settings, starts the view's child and reads what it and sshd say
        into the log, a line at a time. Once sshd says it listens, it records
        the listener and the configuration's digest (/run/moonwater/ssh.running)
        and writes a byte to FD, which `ssh on` waits on. SIGTERM ends every
        session and the server; the server's own end ends the supervisor.
*/
static volatile bool ssh_stop_asked;

static fn ssh_stop_ring(b32 number)
{
        (void)number;
        ssh_stop_asked = true;
}

// The children of this process that are a server's (sshd, sshd-session, sshd-auth), each sent signal, and the dead reaped: how many.
static positive ssh_strays(b32 signal)
{
        file_walk walk;
        struct linux_dirent64 address_to entry;
        positive self = (positive)system_call(syscall(getpid));
        positive found = 0;
        positive status;

        while (system_call_4(syscall(wait4), (positive)-1, (positive)address_of status, 1, 0) > 0)
                ;
        if (!file_walk_open(address_of walk, AT_FDCWD, "/proc"))
                return 0;
        while ((entry = file_walk_next(address_of walk)))
        {
                positive pid = (positive)string_digits(entry->d_name, null);
                positive parent = 0;
                p8 name[32];

                if (pid && ssh_process(pid, &parent, name, sizeof(name), null) && parent == self &&
                    ssh_named_server((string_address)name))
                        found += ssh_signal(pid, signal, (string_address)name);
        }
        file_walk_close(address_of walk);
        return found;
}

static fn ssh_supervisor_end(positive listener, string_address why)
{
        positive status = 0;
        p64 until = system_clock_ns(HOST_CLOCK_BOOTTIME) + SSH_STOP_NS;
        positive sessions = ssh_sessions(listener, null, true);
        p8 line[SSH_LOG_LINE];
        p8 count[24];

        if (listener)
                (void)ssh_signal(listener, 15, null);
        //      A listener that does not go in the time is killed; it is a child of this process, so it is waited for.
        while (listener && system_call_4(syscall(wait4), listener, (positive)address_of status, 1, 0) == 0)
        {
                if (system_clock_ns(HOST_CLOCK_BOOTTIME) > until)
                {
                        (void)ssh_signal(listener, 9, null);
                        system_wait4_retry((bipolar)listener, address_of status, 0, null);
                        break;
                }
                host_pause(50000000);
        }
        //      A connection's half that outlived its listener is this process's now (it is the subreaper): a second
        //      for it to go on SIGTERM, then SIGKILL, so nothing of the server is left for the next look to find.
        until = system_clock_ns(HOST_CLOCK_BOOTTIME) + 1000000000ull;
        while (ssh_strays(15) && system_clock_ns(HOST_CLOCK_BOOTTIME) < until)
                host_pause(50000000);
        (void)ssh_strays(9);
        system_remove_at(AT_FDCWD, SSH_RUNNING, 0);
        count[positive_into_string(count, sessions)] = end;
        (void)string_join(line, sizeof(line), "stopped: ", why, "; ", (string_address)count, " sessions ended");
        ssh_log_line((string_address)line, true);
}

static b32 ssh_serve(bipolar ready)
{
        static p8 config[SSH_CONFIG_ROOM];
        static p8 chunk[4096];
        ssh_settings settings;
        positive length;
        positive kept = 0;
        positive previous[4];
        bipolar lock;
        bipolar failed;
        b32 ends[2];
        bipolar child;
        bool listening = false;
        p8 sum[65];

        if (!ssh_settings_read(address_of settings) || !settings.on)
                return 1;
        lock = host_lock_file(SSH_LOCK, HOST_FILE_SETLK, null);
        if (lock < 0)
                return 1;
        length = ssh_config_text(address_of settings, SSH_STATE, config, sizeof(config));
        failed = length ? ssh_state_ready() : -ENAMETOOLONG;
        if (failed >= 0)
                failed = host_write_file(SSH_CONFIG, config, length, 0600, true);
        if (failed < 0)
        {
                ssh_log_line("refused: the configuration could not be written", true);
                return 1;
        }
        ssh_config_sum(config, length, sum);
        if (system_pipe(ends, O_CLOEXEC) < 0)
                return 1;
        (void)system_signal_install(15, (positive)ssh_stop_ring, SIGNAL_CATCH_FLAGS & ~SIGNAL_RESTART,
                                    SIGNAL_CATCH_RESTORER, previous);
        //      The subreaper of what the server starts (PR_SET_CHILD_SUBREAPER): a connection's half whose listener went
        //      is this process's to end, and not init's to keep.
        if (system_call_5(syscall(prctl), 36, 1, 0, 0, 0) < 0)
                ssh_log_line("the supervisor is no subreaper: a connection that outlives the listener is ended at the next look", false);
        child = system_fork();
        if (child < 0)
                return 1;
        if (!child)
        {
                //      A server whose lines would reach no log is not started.
                if (system_call_3(syscall(dup3), (positive)ends[1], 2, 0) < 0 ||
                    system_call_3(syscall(dup3), (positive)ends[1], 1, 0) < 0)
                        system_call_1(syscall(exit_group), 2);
                (void)system_signal_install(15, 0, 0, 0, null);
                ssh_view_enter(address_of settings);
        }
        system_close(ends[1]);
        ssh_log_line(settings.root ? "starting: root may log in by key" : "starting: no account may log in (root is off)", false);

        for (;;)
        {
                bipolar got;
                positive at = 0;

                if (ssh_stop_asked)
                {
                        ssh_supervisor_end((positive)child, "asked");
                        return 0;
                }
                //      A second at most, so that a SIGTERM that comes between the look above and the read is heard
                //      then and not after sshd's next line; and the read as the call is, interrupted by it.
                {
                        timespec second = {1, 0};
                        bipolar ready_now = link_wait_readable((bipolar)ends[0], address_of second);

                        //      A second gone, or the wait cut short by SIGTERM: the look at the top again, and no
                        //      read that would wait on sshd.
                        if (ready_now == 0 || ready_now == -EINTR)
                                continue;
                        if (ready_now < 0)
                                break;
                }
                got = system_call_3(syscall(read), (positive)ends[0], (positive)(chunk + kept), sizeof(chunk) - 1 - kept);
                if (got == -EINTR)
                        continue;
                if (got <= 0)
                        break;
                kept += (positive)got;
                for (positive stop = 0; stop < kept; stop++)
                {
                        if (chunk[stop] != '\n')
                                continue;
                        chunk[stop] = end;
                        if (stop > at && chunk[stop - 1] == '\r')
                                chunk[stop - 1] = end;
                        {
                                string_address line = chunk + at;
                                bool loud = host_starts(line, "Accepted ") || host_starts(line, "moonwater: refused");
                                p8 said[SSH_LOG_LINE];

                                (void)string_join(said, sizeof(said), host_starts(line, "moonwater: ") ? "" : "sshd: ",
                                                  host_starts(line, "moonwater: ") ? line + 11 : line);
                                ssh_log_line((string_address)said, loud);
                                if (!listening && host_starts(line, "Server listening on "))
                                {
                                        p8 record[128];
                                        p8 pid[24];
                                        p8 byte = '1';

                                        listening = true;
                                        pid[positive_into_string(pid, (positive)child)] = end;
                                        (void)string_join(record, sizeof(record), (string_address)pid, " ", (string_address)sum, "\n");
                                        (void)host_write_file(SSH_RUNNING, record, string_length((string_address)record), 0600, false);
                                        if (ready >= 0)
                                        {
                                                (void)system_write_all_checked((positive)ready, &byte, 1);
                                                system_close(ready);
                                                ready = -1;
                                        }
                                }
                        }
                        at = stop + 1;
                }
                kept -= at;
                ssh_slide(chunk, chunk + at, kept);
                //      A line longer than the room is said in pieces.
                if (kept == sizeof(chunk) - 1)
                {
                        chunk[kept] = end;
                        ssh_log_line(chunk, false);
                        kept = 0;
                }
        }
        {
                positive status = 0;
                p8 line[SSH_LOG_LINE];
                p8 code[24];

                system_wait4_retry(child, address_of status, 0, null);
                code[positive_into_string(code, wait_status_code(status))] = end;
                (void)string_join(line, sizeof(line), "the server ended, status ", (string_address)code);
                ssh_log_line((string_address)line, true);
        }
        system_remove_at(AT_FDCWD, SSH_RUNNING, 0);
        system_close(lock);
        return 1;
}

// The supervisor, started apart from whoever asked: a session of its own, nothing open but what it is given.
static fn ssh_serve_start(bipolar ready)
{
        p8 digits[12];
        string_address words[] = {"moonwater", "ssh", "serve", (string_address)digits, null};
        string_address plain[] = {"moonwater", "ssh", "serve", null};

        if (ready >= 0)
                digits[positive_into_string(digits, (positive)ready)] = end;
        if (!host_detach((p32)(ready >= 0 ? ready : descriptors_none), descriptors_none))
                return;
        (void)system_signal_install(13, 0, 0, 0, null);
        (void)shell_exec_file("/proc/self/exe", ready >= 0 ? words : plain, ready >= 0 ? 4 : 3, file_environment_all());
        system_call_1(syscall(exit_group), 127);
}

// The server stopped: its supervisor told to end, and waited for, a few seconds at most.
static bool ssh_stop(void)
{
        p64 until = system_clock_ns(HOST_CLOCK_BOOTTIME) + SSH_STOP_NS;

        if (host_lock_owner(SSH_LOCK) <= 0)
                return true;
        (void)host_lock_signal(SSH_LOCK, 15);
        while (host_lock_owner(SSH_LOCK) > 0)
        {
                if (system_clock_ns(HOST_CLOCK_BOOTTIME) > until)
                        return false;
                host_pause(50000000);
        }
        return true;
}

// The server started and listening, or false; the log says why not.
static bool ssh_start(void)
{
        b32 ends[2];
        timespec limit = {SSH_WAIT_NS / 1000000000, 0};
        p8 byte = 0;
        bool started;

        if (system_pipe(ends, 0) < 0)
                return false;
        ssh_serve_start((bipolar)ends[1]);
        system_close((bipolar)ends[1]);
        started = link_wait_readable((bipolar)ends[0], address_of limit) > 0 &&
                  system_read_retry((positive)ends[0], address_of byte, 1) == 1 && byte == '1';
        system_close((bipolar)ends[0]);
        return started;
}

/* ---- the machine process ---- */

static p64 ssh_keep_look;
static p64 ssh_keep_next;
static p64 ssh_keep_wait = 1;

// The boot this is: the kernel's id of it.
static fn ssh_boot(p8 address_to into, positive room)
{
        if (host_read_kernel("/proc/sys/kernel/random/boot_id", into, room) < 0)
                into[0] = end;
}

/*
        A timed on that ran out, or that is from another boot, turned off:
        true when it was. A reboot ends a timed on whatever is left of it.
*/
// Whether a timed on is over in the boot boot at second now of its clock.
static bool ssh_expired_at(const ssh_settings address_to settings, string_address boot, p64 now)
{
        return settings->on && settings->until &&
               (!string_equals(boot, (string_address)settings->boot) || now >= settings->until);
}

static bool ssh_expired(ssh_settings address_to settings)
{
        p8 boot[40];
        p64 now = system_clock_ns(HOST_CLOCK_BOOTTIME) / 1000000000ull;

        bool rebooted;

        ssh_boot(boot, sizeof(boot));
        if (!ssh_expired_at(settings, (string_address)boot, now))
                return false;
        rebooted = !string_equals((string_address)boot, (string_address)settings->boot);
        settings->on = false;
        settings->until = 0;
        settings->boot[0] = end;
        (void)ssh_settings_save(settings);
        ssh_log_line(rebooted ? "off: a timed on does not outlive a reboot" : "off: the time limit ran out", true);
        return true;
}

/*
        The machine process's side, every few seconds: the settings are what
        runs. A timed on that ran out is off; off is no server; on is the
        server Moonwater started from the settings now (one started from
        other settings is stopped, and started again at the next look);
        and nothing else of an sshd's, nor anything on its port, runs.
*/
static fn ssh_keep_machine(void)
{
        p64 now = system_clock_ns(HOST_CLOCK_BOOTTIME) / 1000000000ull;
        ssh_settings settings;
        positive listener;
        p8 sum[65];

        if (now < ssh_keep_look)
                return;
        ssh_keep_look = now + SSH_LOOK_SECONDS;
        (void)ssh_settings_read(address_of settings);
        (void)ssh_expired(address_of settings);
        listener = ssh_running(sum, sizeof(sum));
        //      Off, a supervisor still there is told to end its server, which it does with its sessions; once it has, nothing of
        //      an sshd's runs.
        if (!settings.on)
        {
                if (host_lock_owner(SSH_LOCK) > 0)
                        (void)host_lock_signal(SSH_LOCK, 15);
                else
                        (void)ssh_enforce(address_of settings, 0, 0);
                return;
        }
        if (listener)
        {
                static p8 config[SSH_CONFIG_ROOM];
                p8 want[65];
                positive length = ssh_config_text(address_of settings, SSH_STATE, config, sizeof(config));

                ssh_config_sum(config, length, want);
                if (!string_equals((string_address)want, (string_address)sum))
                {
                        ssh_log_line("restarting: the settings are not the ones it was started with", false);
                        (void)host_lock_signal(SSH_LOCK, 15);
                }
                ssh_keep_wait = 1;
        }
        else if (host_lock_owner(SSH_LOCK) <= 0 && now >= ssh_keep_next && ssh_installed())
        {
                ssh_keep_next = now + ssh_keep_wait;
                if (ssh_keep_wait < 60)
                        ssh_keep_wait *= 2;
                ssh_serve_start(-1);
        }
        (void)ssh_enforce(address_of settings, (positive)host_lock_owner(SSH_LOCK), listener);
}

/*
        What `bowl profile ssh remove` does first: the server stopped, every
        session with it, the switch off, and the record gone. The keys, the
        host key and the settings stay, so an install again is the same
        server to whoever knows its fingerprint, and off.
*/
static fn ssh_profile_forget(void)
{
        ssh_settings settings;

        (void)ssh_settings_read(address_of settings);
        settings.on = false;
        settings.until = 0;
        settings.boot[0] = end;
        if (system_access_at(AT_FDCWD, SSH_STATE, 0) >= 0)
                (void)ssh_settings_save(address_of settings);
        (void)ssh_stop();
        (void)ssh_enforce(address_of settings, 0, 0);
        system_remove_at(AT_FDCWD, SSH_DIGESTS, 0);
        if (system_access_at(AT_FDCWD, SSH_STATE, 0) >= 0)
                ssh_log_change("removed: the server stopped, its record gone, off");
}

/* ---- the verbs ---- */

/*
        Only the machine's root changes these: root's id in the machine's own
        user namespace. A process that is root in a namespace of its own (a
        bowl's program that made one, at the default tier the machine's root
        to every file) is not, and cannot read PID 1's namespace to be
        compared with it.
*/
static bool ssh_machine_root(void)
{
        p8 self[64];
        p8 first[64];

        return bowl_is_root() && file_link_text("/proc/self/ns/user", self, sizeof(self)) > 0 &&
               file_link_text("/proc/1/ns/user", first, sizeof(first)) > 0 &&
               string_equals((string_address)self, (string_address)first);
}

#define SSH_NOT_HERE "this machine has no ssh server: moonwater bowl profile ssh install puts OpenSSH in the Alpine bowl\n"

static fn ssh_say_state(const ssh_settings address_to settings)
{
        static ssh_keys keys;
        ssh_list sessions = {0};
        ssh_digests digests;
        p8 fingerprint[64];
        positive listener = ssh_running(null, 0);
        positive count;
        bool listed = ssh_keys_read(address_of keys);

        if (settings->on && listener)
                host_say(log, host_label "ssh on, running\n");
        else if (settings->on)
                host_say(log, host_label "ssh on, not running: moonwater ssh log says why\n");
        else
                host_say(log, host_label "ssh off\n");
        host_say(log, "  port %p\n", (positive)settings->port);
        host_say(log, "  address %s\n", settings->address[0] ? (string_address)settings->address : (string_address) "any");
        host_say(log, "  root %s\n", settings->root ? "on" : "off");
        if (settings->limit)
                host_say(log, "  limit %p minutes\n", (positive)settings->limit);
        else
                host_say(log, "  limit off\n");
        if (settings->on && settings->until)
        {
                p64 now = system_clock_ns(HOST_CLOCK_BOOTTIME) / 1000000000ull;
                time_t stamp = time(null) + (time_t)(settings->until > now ? settings->until - now : 0);
                tm broken;
                p8 when[16];

                localtime_r(address_of stamp, address_of broken);
                strftime(when, sizeof(when), "%H:%M", address_of broken);
                host_say(log, "  until %s\n", (string_address)when);
        }
        if (ssh_host_fingerprint(fingerprint))
                host_say(log, "  host key ed25519 %s\n", (string_address)fingerprint);
        else
                host_say(log, "  host key none: it is made at the first on\n");
        if (ssh_digests_read(address_of digests))
        {
                const ssh_digest address_to bad = null;

                for (positive at = 0; at < digests.count && !bad; at++)
                {
                        bipolar handle = bowl_open_in_root(SSH_BOWL, (string_address)digests.files[at].bowl,
                                                           FILE_READ | O_NONBLOCK | O_CLOEXEC);
                        p8 hex[65];
                        p64 size;

                        if (handle < 0 || !bowl_sha256_of(handle, hex, &size) ||
                            !string_equals((string_address)hex, (string_address)digests.files[at].sum))
                                bad = &digests.files[at];
                        if (handle >= 0)
                                system_close(handle);
                }
                if (bad)
                        host_say(log, "  binaries refused: %s is not the file the profile recorded (bowl profile ssh install records it again)\n",
                                 (string_address)bad->bowl);
                else
                        host_say(log, "  binaries verified: %p files, each the SHA-256 recorded at install\n", digests.count);
        }
        host_say(log, MOONWATER_STRICT_BOWL >= STRICT_TIGHT
                          ? "  bowl tier hardened: no program of a bowl can read or change these settings\n"
                          : "  bowl tier default: a program of a bowl is root to the machine's files, and could change these settings (sec_bowl_hardened)\n");
        if (!listed)
                host_say(log, "  keys refused: " SSH_KEYS " is not the list moonwater ssh writes, and the server does not start on it\n");
        for (positive at = 0; listed && at < keys.count; at++)
        {
                ssh_key_fingerprint(&keys.keys[at], fingerprint);
                host_say(log, "  key %s %s %s\n", (string_address)keys.keys[at].label, (string_address)keys.keys[at].type,
                         (string_address)fingerprint);
        }
        if (listed && !keys.count)
                host_say(log, "  keys none: moonwater ssh key add LABEL KEY\n");
        count = ssh_sessions(listener, address_of sessions, false);
        if (count)
                host_say(log, "  sessions %p\n", count);
        for (positive at = 0; at < sessions.held; at++)
                host_say(log, "  session %s from %s since %s\n", (string_address)sessions.rows[at].who,
                         (string_address)sessions.rows[at].from, (string_address)sessions.rows[at].since);
        if (count > sessions.held)
                host_say(log, "  %p sessions not listed: no memory for their rows\n", count - sessions.held);
        if (sessions.rows)
                memory_release((address_any address_to)&sessions.rows, &sessions.room, &sessions.held,
                               sizeof(ssh_session));
}

static b32 ssh_log_show(positive asked)
{
        static p8 text[SSH_LOG_MOST + 2];
        bipolar got = ssh_read_owned(SSH_LOG, text, sizeof(text));
        positive lines = 0;
        positive from;

        if (got == -ERROR_NO_ENTRY)
        {
                host_say(log, host_label "ssh: nothing logged yet\n");
                return 0;
        }
        return_if(got < 0, host_fail(SSH_LOG, got));
        from = (positive)got;
        while (from && lines <= asked)
        {
                from--;
                if (text[from] == '\n' && from + 1 < (positive)got)
                        lines++;
        }
        if (lines > asked)
                from++;
        log((address_any)(text + from), (positive)got - from);
        log_flush();
        return 0;
}

// The server again with the settings now, when it runs: stopped, and started.
static b32 ssh_apply(const ssh_settings address_to settings, string_address done)
{
        if (!settings->on)
        {
                bool stopped = ssh_stop();

                (void)ssh_enforce(settings, 0, 0);
                return_if(!stopped, host_refuse("ssh: the server did not stop in five seconds\n"));
                host_say(log, host_label "ssh %s\n", done);
                return 0;
        }
        //      One that runs with the file these settings write is the server already, and its sessions go on.
        {
                static p8 config[SSH_CONFIG_ROOM];
                p8 running[65];
                p8 want[65];
                positive length = ssh_config_text(settings, SSH_STATE, config, sizeof(config));

                ssh_config_sum(config, length, want);
                if (ssh_running(running, sizeof(running)) && string_equals((string_address)running, (string_address)want))
                {
                        host_say(log, host_label "ssh %s\n", done);
                        return 0;
                }
        }
        if (!ssh_stop())
                return host_refuse("ssh: the server did not stop in five seconds to start again\n");
        if (!ssh_start())
        {
                host_say(log_error, host_label "ssh: the server did not start; the last of the log:\n");
                (void)ssh_log_show(4);
                return 1;
        }
        host_say(log, host_label "ssh %s\n", done);
        return 0;
}

static b32 ssh_on(ssh_settings address_to settings)
{
        p8 fingerprint[64];
        p8 line[SSH_LOG_LINE];
        p8 number[24];
        b32 failed;

        settings->on = true;
        settings->until = 0;
        settings->boot[0] = end;
        if (settings->limit)
        {
                ssh_boot(settings->boot, sizeof(settings->boot));
                settings->until = system_clock_ns(HOST_CLOCK_BOOTTIME) / 1000000000ull + (p64)settings->limit * 60;
        }
        return_if(ssh_settings_save(settings) < 0, host_refuse("ssh: %s could not be written\n", SSH_SETTINGS));
        number[positive_into_string(number, settings->limit)] = end;
        (void)string_join(line, sizeof(line), "on", settings->limit ? ", for " : ", with no time limit",
                          settings->limit ? (string_address)number : (string_address) "", settings->limit ? " minutes" : "");
        ssh_log_change((string_address)line);
        failed = ssh_apply(settings, settings->root ? "on: root may log in by a key of the list"
                                                    : "on: no account may log in until moonwater ssh root on");
        if (failed)
                return failed;
        if (ssh_host_fingerprint(fingerprint))
                host_say(log, "  host key ed25519 %s: compare it with what the client says before it is trusted\n",
                         (string_address)fingerprint);
        if (settings->limit)
                host_say(log, "  off by itself in %p minutes, and at the next boot\n", (positive)settings->limit);
        return 0;
}

static b32 ssh_key_add(string_address label, string_address type, string_address blob)
{
        static ssh_keys keys;
        p8 bytes[SSH_KEY_TEXT];
        p8 fingerprint[64];
        p8 line[SSH_LOG_LINE];
        positive size;
        ssh_key address_to key;

        return_if(!ssh_label_valid(label),
                  host_refuse("ssh: a label is 1 to 32 letters, digits, dots, dashes and underscores, not %s\n", label));
        return_if(!string_equals(type, "ssh-ed25519") && !string_equals(type, "sk-ssh-ed25519@openssh.com"),
                  host_refuse("ssh: the key is ssh-ed25519 or sk-ssh-ed25519@openssh.com, not %s\n", type));
        return_if(!ssh_key_blob(type, blob, bytes, sizeof(bytes), &size),
                  host_refuse("ssh: that is not a well-made %s key\n", type));
        return_if(!ssh_keys_read(address_of keys),
                  host_refuse("ssh: %s is not the list moonwater ssh writes: remove it to start a new one\n", SSH_KEYS));
        for (positive at = 0; at < keys.count; at++)
        {
                return_if(string_equals((string_address)keys.keys[at].label, label),
                          host_refuse("ssh: a key is already called %s\n", label));
                return_if(string_equals((string_address)keys.keys[at].blob, blob),
                          host_refuse("ssh: that key is already in the list, as %s\n", (string_address)keys.keys[at].label));
        }
        return_if(keys.count == SSH_KEYS_MOST, host_refuse("ssh: the list holds %p keys at most\n", (positive)SSH_KEYS_MOST));
        key = &keys.keys[keys.count++];
        string_copy_bounded(key->label, label, sizeof(key->label));
        string_copy_bounded(key->type, type, sizeof(key->type));
        string_copy_bounded(key->blob, blob, sizeof(key->blob));
        return_if(ssh_keys_save(address_of keys) < 0, host_refuse("ssh: %s could not be written\n", SSH_KEYS));
        ssh_fingerprint(bytes, size, fingerprint);
        (void)string_join(line, sizeof(line), "key added: ", label, " ", type, " ", (string_address)fingerprint);
        ssh_log_change((string_address)line);
        host_say(log, host_label "ssh: %s may log in, as %s %s\n", label, type, (string_address)fingerprint);
        return 0;
}

static b32 ssh_key_remove(string_address label)
{
        static ssh_keys keys;
        p8 line[SSH_LOG_LINE];
        positive found = positive_max;
        positive ended;

        return_if(!ssh_keys_read(address_of keys),
                  host_refuse("ssh: %s is not the list moonwater ssh writes\n", SSH_KEYS));
        for (positive at = 0; at < keys.count; at++)
                if (string_equals((string_address)keys.keys[at].label, label))
                        found = at;
        return_if(found == positive_max, host_refuse("ssh: no key is called %s\n", label));
        ssh_slide(&keys.keys[found], &keys.keys[found + 1], (keys.count - found - 1) * sizeof(keys.keys[0]));
        keys.count--;
        return_if(ssh_keys_save(address_of keys) < 0, host_refuse("ssh: %s could not be written\n", SSH_KEYS));
        ended = ssh_sessions(ssh_running(null, 0), null, true);
        (void)string_join(line, sizeof(line), "key removed: ", label, "; every session ended");
        ssh_log_change((string_address)line);
        host_say(log, host_label "ssh: %s may not log in; %p sessions ended\n", label, ended);
        return 0;
}

// A key from standard input, one line as a .pub file holds it: type, blob, and anything after.
static b32 ssh_key_read(string_address label)
{
        p8 text[SSH_KEY_TEXT + 256];
        bipolar got = system_read_retry(0, text, sizeof(text) - 1);
        string_address words[2];
        positive count = 0;
        string_address at = text;

        return_if(got <= 0, host_refuse("ssh: no key on standard input\n"));
        text[got] = end;
        while (count < 2 && *at)
        {
                while (*at == ' ' || *at == '\t')
                        at++;
                if (!*at || *at == '\n')
                        break;
                words[count++] = at;
                while (*at && *at != ' ' && *at != '\t' && *at != '\n')
                        at++;
                if (*at)
                        *at++ = end;
        }
        return_if(count < 2, host_refuse("ssh: standard input holds no key: TYPE BASE64 [COMMENT]\n"));
        return ssh_key_add(label, words[0], words[1]);
}

static b32 ssh_dispatch(string_address address_to arguments, positive count)
{
        string_address verb = count > 2 ? arguments[2] : null;
        ssh_settings settings;
        bool readable;
        p8 line[SSH_LOG_LINE];

        if (verb && string_equals(verb, "serve") && count <= 4)
        {
                return_if(!ssh_machine_root(), 1);
                return ssh_serve(count == 4 ? (bipolar)string_digits(arguments[3], null) : -1);
        }
        return_if(!ssh_machine_root(), host_refuse("moonwater ssh needs the machine's root\n"));
        readable = ssh_settings_read(address_of settings);
        if (!verb)
        {
                if (!ssh_installed())
                {
                        host_say(log, host_label SSH_NOT_HERE);
                        if (settings.on || host_lock_owner(SSH_LOCK) > 0)
                                host_say(log, "  ssh on in the settings, and not running\n");
                        return 1;
                }
                if (!readable)
                        host_say(log, host_label "ssh: %s is not the settings moonwater ssh writes; it is off\n", SSH_SETTINGS);
                ssh_say_state(address_of settings);
                return 0;
        }
        if (string_equals(verb, "log") && count <= 4)
        {
                positive asked = SSH_LOG_SHOWN;

                if (count == 4 && !ssh_number(arguments[3], string_length(arguments[3]), 1, SSH_LOG_ASKED, &asked))
                        return host_refuse("ssh log takes a count from 1 to %p\n", (positive)SSH_LOG_ASKED);
                return ssh_log_show(asked);
        }
        //      Off, and what takes away, are there whether or not the profile is.
        if (string_equals(verb, "off") && count == 3)
        {
                settings.on = false;
                settings.until = 0;
                settings.boot[0] = end;
                return_if(ssh_settings_save(address_of settings) < 0, host_refuse("ssh: %s could not be written\n", SSH_SETTINGS));
                ssh_log_change("off");
                return ssh_apply(address_of settings, "off: the server is stopped, every session ended, and it stays off across boots");
        }
        if (string_equals(verb, "end") && count == 3)
        {
                positive ended = ssh_sessions(ssh_running(null, 0), null, true);
                p8 number[24];

                number[positive_into_string(number, ended)] = end;
                (void)string_join(line, sizeof(line), "every session ended: ", (string_address)number);
                ssh_log_change((string_address)line);
                host_say(log, host_label "ssh: %p sessions ended\n", ended);
                return 0;
        }
        if (string_equals(verb, "key") && count == 5 && string_equals(arguments[3], "remove"))
                return ssh_key_remove(arguments[4]);
        return_if(!ssh_installed(), host_refuse(SSH_NOT_HERE));
        //      Settings that are not this file's were read as the standard, off; a change writes them whole again.
        if (string_equals(verb, "on") && count == 3)
                return ssh_on(address_of settings);
        if (string_equals(verb, "key") && count >= 5 && string_equals(arguments[3], "add"))
        {
                if (count == 6 && string_equals(arguments[5], "-"))
                        return ssh_key_read(arguments[4]);
                //      moonwater ssh key add LABEL TYPE BASE64, and the comment a .pub file has after them, which is not kept.
                return_if(count != 7 && count != 8,
                          host_refuse("ssh key add LABEL TYPE BASE64 [COMMENT], or LABEL - with the key on standard input\n"));
                return ssh_key_add(arguments[4], arguments[5], arguments[6]);
        }
        if (count == 3 && (string_equals(verb, "root") || string_equals(verb, "port") || string_equals(verb, "address") ||
                           string_equals(verb, "limit")))
        {
                ssh_say_state(address_of settings);
                return 0;
        }
        if (count != 4)
                return host_usage();
        if (string_equals(verb, "root"))
        {
                bipolar word = host_onoff(arguments[3]);

                return_if(word < 0, host_refuse("ssh root is on or off, not %s\n", arguments[3]));
                settings.root = (p8)word;
        }
        else if (string_equals(verb, "port"))
        {
                positive port = SSH_PORT;

                return_if(!string_equals(arguments[3], "default") &&
                              !ssh_number(arguments[3], string_length(arguments[3]), 1, 65535, &port),
                          host_refuse("ssh port is 1 to 65535, or default, not %s\n", arguments[3]));
                settings.port = (p16)port;
        }
        else if (string_equals(verb, "address"))
        {
                if (string_equals(arguments[3], "any"))
                        settings.address[0] = end;
                else
                        return_if(!ssh_address_normal(arguments[3], settings.address, sizeof(settings.address)),
                                  host_refuse("ssh address is an IPv4 or IPv6 address of this machine, or any, not %s\n", arguments[3]));
        }
        else if (string_equals(verb, "limit"))
        {
                positive minutes = SSH_LIMIT;

                if (string_equals(arguments[3], "off"))
                        minutes = 0;
                else
                        return_if(!string_equals(arguments[3], "default") &&
                                      !ssh_number(arguments[3], string_length(arguments[3]), 1, SSH_LIMIT_MOST, &minutes),
                                  host_refuse("ssh limit is 1 to %p minutes, off or default, not %s\n", (positive)SSH_LIMIT_MOST,
                                              arguments[3]));
                settings.limit = (p16)minutes;
                //      A server on now ends as the new limit says, from now.
                if (settings.on)
                {
                        settings.until = 0;
                        settings.boot[0] = end;
                        if (minutes)
                        {
                                ssh_boot(settings.boot, sizeof(settings.boot));
                                settings.until = system_clock_ns(HOST_CLOCK_BOOTTIME) / 1000000000ull + (p64)minutes * 60;
                        }
                }
        }
        else
                return host_usage();
        return_if(ssh_settings_save(address_of settings) < 0, host_refuse("ssh: %s could not be written\n", SSH_SETTINGS));
        (void)string_join(line, sizeof(line), verb, " ", arguments[3]);
        ssh_log_change((string_address)line);
        (void)string_join(line, sizeof(line), verb, " ", arguments[3], settings.on ? ": the server is started again with it" : "");
        if (!settings.on)
        {
                host_say(log, host_label "ssh %s\n", (string_address)line);
                return 0;
        }
        return ssh_apply(address_of settings, (string_address)line);
}

static b32 ssh_main(string_address address_to arguments, positive count)
{
        positive previous[4];
        bool ignored = system_signal_install(13, 1, 0, 0, previous);
        b32 answer = ssh_dispatch(arguments, count);

        log_flush();
        if (ignored)
                system_signal_action(13, previous, null, 8);
        return answer;
}

#endif // MOONWATER_SSH_INCLUDED
