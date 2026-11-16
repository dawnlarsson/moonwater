/*
        src/sh/host/security.c: the verbs dns, security and bind.

        A run of src/sh/host.c, which includes this file where the run stood (test/split.py cut it there): it is
        compiled only as part of that file, with its statics and macros, and never on its own. host_main, at the end
        of src/sh/host.c, names the function each verb runs.
*/

/*
        moonwater dns [ADDRESS...|auto]: the servers names are asked of.

        One to three IPv4 addresses are kept in /root/dns and written to
        /etc/resolv.conf at once, and every lease after writes them in place
        of its own resolver: a lease's server is the network's, which is right
        until it is not (a router that is slow, or that answers for names the
        machine would rather not send there). auto forgets them, and the file
        is written the way the lease would write it. Bare says what the file
        names now and whose choice that is. 1.1.1.1 is neither: it is the
        server of a network that named none, and where a lookup asks when the
        file names no server at all.
*/
/*
        moonwater dns tls [on|off|only]: whether names are asked over TLS
        (the DNS over TLS comment in net.c says how). Kept in /root/dns.tls
        and published in /run/moonwater/dns.tls, which the resolver reads,
        so that a lookup by a user who cannot read /root follows it too; no
        word kept is no published file, and then the tier's default holds.
*/
#define DNS_TLS_KEPT "/root/dns.tls"

//      What the resolver reads is answered, so that a verb can say it was not written: the mode it names is not in force.
static bipolar dns_tls_publish(void)
{
        p8 word[16];
        bipolar failed;

        host_read_word(DNS_TLS_KEPT, word, sizeof(word));
        //      A word that is none of the three is not published: the resolver takes an unknown word for the tier's default
        //      and says nothing, so a kept word damaged by hand would have its setting dropped with no line to say why.
        if (word[0] && !string_equals(word, "on") && !string_equals(word, "off") && !string_equals(word, "only"))
        {
                host_say(log_error, host_label "dns tls: %s in %s is not on, off or only; the tier's default holds\n",
                         word, DNS_TLS_KEPT);
                word[0] = end;
        }
        if (!word[0])
        {
                failed = system_remove_at(AT_FDCWD, DNS_TLS_PATH, 0);
                return failed < 0 && failed != -ENOENT ? failed : 0;
        }
        host_state_ready();
        return radio_write_word(DNS_TLS_PATH, (string_address)word);
}

static string_address dns_tls_word(positive mode)
{
        return mode == DNS_TLS_ON     ? (string_address) "on"
               : mode == DNS_TLS_ONLY ? (string_address) "only"
                                      : (string_address) "off";
}

static fn dns_say(bool label)
{
        p32 servers[DNS_SERVERS_MAX];
        p32 kept[DNS_SERVERS_MAX];
        p8 held[96];
        p8 text[24];
        positive count = dns_servers((string_address)"/etc/resolv.conf",
                                     servers, array_count(servers));

        if (label)
                string_format(log, host_label "dns");
        else
                string_format(log, "  dns");
        for (positive at = 0; at < count; at++)
                string_format(log, " %s", net_host_text(text, servers[at]));
        if (!count)
                string_format(log, " none named, lookups ask %s",
                              net_host_text(text, DNS_FALLBACK));
        else if (!locale_readable(NET_DNS_PATH, held, sizeof(held)))
                string_format(log, " " LOCALE_NEEDS_ROOT);
        else
                string_format(log, "%s", net_dns_kept_at(NET_DNS_PATH, kept) ? " (kept)" : " (the network's)");
        //      The label's escapes are format text: as an argument %s would
        //      print them as the characters they are spelt with.
        host_say(log, label ? "\n" host_label "dns tls %s\n" : "\n  dns tls %s\n",
                 dns_tls_word(dns_tls_mode()));
}

static b32 dns_tls_verb(string_address address_to arguments, positive count)
{
        string_address word = count > 3 ? arguments[3] : null;
        bipolar failed;

        if (count == 4)
        {
                return_if(!string_equals(word, "on") && !string_equals(word, "off") &&
                          !string_equals(word, "only"), host_usage());
                host_need_root();
                //      Kept, and the kept word read again and published: two commands
                //      at once could publish the word the other had already replaced.
                if (!host_acquire())
                        return 1;
                if (host_save("dns tls", DNS_TLS_KEPT, word))
                {
                        host_release();
                        return 1;
                }
                failed = dns_tls_publish();
                host_release();
                return_if(failed < 0, host_fail("dns tls not published to " DNS_TLS_PATH, failed));
        }
        else
                return_if(count != 3, host_usage());
        host_say(log, host_label "dns tls %s\n", dns_tls_word(dns_tls_mode()));
        return 0;
}

static b32 dns_set(string_address address_to words, positive count)
{
        p32 servers[DNS_SERVERS_MAX];
        p8 line[96];
        bipolar failed;

        if (count > DNS_SERVERS_MAX)
                return host_refuse("dns takes %p servers at most, which is all a lookup "
                                   "reads; auto for the network's\n",
                                   (positive)DNS_SERVERS_MAX);
        line[0] = end;
        for (positive at = 0; at < count; at++)
        {
                bipolar host = string_to_host(words[at]);

                if (!net_dns_usable(host))
                        return host_refuse("%s is not a server address: a dotted IPv4 "
                                           "address a machine can be at, or auto for the "
                                           "network's\n", words[at]);
                for (positive before = 0; before < at; before++)
                        if (servers[before] == (p32)host)
                                return host_refuse("%s is named twice\n", words[at]);
                servers[at] = (p32)host;
                if (at)
                        string_append_bounded(line, " ", sizeof(line));
                string_append_bounded(line, words[at], sizeof(line));
        }

        failed = radio_write_word(NET_DNS_PATH, line);
        return_if(failed < 0, host_fail("dns", failed));
        failed = net_write_resolv_servers((string_address) "/etc/resolv.conf",
                                          servers, count);
        return_if(failed < 0, host_fail("kept, but /etc/resolv.conf", failed));
        dns_say(true);
        return 0;
}

static b32 dns_auto(void)
{
        bipolar failed = system_remove_at(AT_FDCWD, NET_DNS_PATH, 0);

        return_if(failed < 0 && failed != -ENOENT, host_fail("dns", failed));
        failed = net_write_resolv(net_dns_lease());
        return_if(failed < 0, host_fail("forgotten, but /etc/resolv.conf", failed));
        dns_say(true);
        return 0;
}

//      What was kept is the file before any lease writes it: a machine that
//      boots with no network at all still asks the servers it was told to.
static fn dns_restore(void)
{
        p32 servers[DNS_SERVERS_MAX];
        positive count = net_dns_kept_at(NET_DNS_PATH, servers);

        (void)dns_tls_publish();
        if (count)
                (void)net_write_resolv_servers((string_address) "/etc/resolv.conf",
                                               servers, count);
}

static b32 host_dns(string_address address_to arguments, positive count)
{
        b32 answer;

        if (count == 2)
        {
                dns_say(true);
                return 0;
        }
        if (string_equals(arguments[2], "tls"))
                return dns_tls_verb(arguments, count);
        host_need_root();
        //      The servers are kept in /root and written to /etc/resolv.conf: two
        //      commands at once each did both, and the files could end naming
        //      different servers (A kept, B kept, B written, A written).
        if (!host_acquire())
                return 1;
        answer = count == 3 && string_equals(arguments[2], "auto") ? dns_auto()
                                                                   : dns_set(arguments + 2, count - 2);
        host_release();
        return answer;
}

/*
        moonwater security: what TLS trusts and refuses besides what is built
        in.

        Two lists, each added to and taken from the same way. trust is the
        anchors: anchors.inc's, and a CA certificate root names (a private or
        a company's CA), which is then trusted as a built-in one is and held
        to the same rules -- a CA, a key that may sign certificates, P-256,
        P-384 or RSA of 2048 to 4096 bits, and no name constraints, since the
        built-in anchors carry none and one added would be trusted outside
        them. block is the keys refused, by the SHA-256 of the DER
        SubjectPublicKeyInfo (what openssl, Chrome's CRLSet and Mozilla's
        OneCRL name a key by, and what a Certificate Transparency log's id
        is): a built-in anchor so named stops being one, a CA under it stops
        being a step to one, a leaf of it is refused, and a CT log of it
        counts no SCT. Taking a built-in key out of trust is blocking it; a
        block wins over a trust of the same key; and tls_blocked_keys, which
        are built in, stay refused, since nothing here can lift them.

        Both are kept in the security list of the image's settings (as init
        and exit are, so they go along with setup install and update) and
        published in /run/moonwater/security, which every TLS client reads
        and only root can write (tls_overlay_read in net.c says how it is
        read). Bare says what that file holds, to anybody.

        The switches go in the same list and the same file: TLS's and
        HTTP's five, and packages and sudo, which are the bowls' and which
        security_switch_on answers to bowl.c and the root check.
*/
#define HOST_SECURITY TLS_OVERLAY_PATH
#define HOST_SECURITY_DER_MOST 8192

/*
        The switches, each its word, the word that is its default, the word
        that turns it (an entry of the security list, TLS_SWITCH_* and
        SECURITY_SWITCH_*), the tier of the region it belongs to (TLS's are
        the net's, packages and sudo the bowl's), and what that tier does
        with it when it is tight. All but sudo only tighten, so none of them
        can take back what a build or a tier asks; sudo loosens, so a tight
        build never has it turned and refuses the word that would.
*/
#define SECURITY_TIGHT_NONE 0   // the list says, at every tier
#define SECURITY_TIGHT_TURNED 1 // turned whatever is kept; its default word is kept and said to stay
#define SECURITY_TIGHT_SHUT 2   // turned whatever is kept, and its default word refused
#define SECURITY_TIGHT_HELD 3   // at its default whatever is kept, and the word that turns it refused

static const struct
{
        string_address word;
        string_address standard;
        string_address turned;
        p16 id;
        p8 tier;
        p8 tight;
} security_switches[] = {
    {"tls12", "on", "off", TLS_SWITCH_TLS12_OFF, MOONWATER_STRICT_NET, SECURITY_TIGHT_NONE},
    {"aia", "on", "off", TLS_SWITCH_AIA_OFF, MOONWATER_STRICT_NET, SECURITY_TIGHT_NONE},
    {"revocation", "soft", "hard", TLS_SWITCH_REVOCATION_HARD, MOONWATER_STRICT_NET,
     SECURITY_TIGHT_TURNED},
    {"ct", "off", "on", TLS_SWITCH_CT_ON, MOONWATER_STRICT_NET, SECURITY_TIGHT_TURNED},
    {"http", "on", "off", TLS_SWITCH_HTTP_OFF, MOONWATER_STRICT_NET, SECURITY_TIGHT_NONE},
    {"packages", "on", "off", SECURITY_SWITCH_PACKAGES_OFF, MOONWATER_STRICT_BOWL,
     SECURITY_TIGHT_SHUT},
    {"sudo", "off", "on", SECURITY_SWITCH_SUDO_ON, MOONWATER_STRICT_BOWL, SECURITY_TIGHT_HELD},
};

/* Whether a switch is turned, from the bits of a switch word as the file has
   them and from what its region's tier does with it when tight. */
static bool security_switch_turned(positive at, p32 kept)
{
        p8 tight = security_switches[at].tier >= STRICT_TIGHT ? security_switches[at].tight
                                                               : SECURITY_TIGHT_NONE;

        if (tight == SECURITY_TIGHT_TURNED || tight == SECURITY_TIGHT_SHUT)
                return true;
        return tight != SECURITY_TIGHT_HELD && ((kept >> (security_switches[at].id - 1)) & 1);
}

/*
        The switch word of a published security file, or -1 when there is
        none to believe: the name does not open without following a link, or
        it is not a regular file, or a group or others may write it, or it is
        longer than the overlay, or it does not read whole as
        tls_overlay_read reads it, or its owner is neither owner (root, but
        for a check that makes its own files) nor the uid the kernel shows
        for an owner the caller's user namespace does not map, which is how
        root's own file looks from inside a bowl's.
*/
static bipolar security_switches_read_at(string_address path, p32 owner)
{
        static p8 text[TLS_OVERLAY_HEAD + TLS_OVERLAY_ROOM + 2];
        static tls_overlay overlay;
        system_path_identity facts;
        bipolar handle = system_open_at(AT_FDCWD, path,
                                        FILE_READ | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW);
        bipolar got;

        return_if(handle < 0, -1);
        got = system_path_identity_at(handle, (string_address) "", SYSTEM_PATH_AT_EMPTY_PATH,
                                      SYSTEM_PATH_STATX_TYPE | SYSTEM_PATH_STATX_MODE |
                                          SYSTEM_PATH_STATX_UID,
                                      address_of facts);
        if (got >= 0 && ((facts.mode & 0170000) != 0100000 || (facts.mode & 022)))
                got = -1;
        if (got >= 0 && facts.user != owner)
        {
                //      The kernel's overflowuid, which only a sysctl moves from 65534.
                p8 number[16];
                string_address digits = (string_address)number;
                bool over = false;
                positive unmapped = 65534;
                positive read = 0;

                if (file_slurp_once_at(AT_FDCWD, (string_address) "/proc/sys/kernel/overflowuid",
                                       number, sizeof number) > 0)
                        read = string_decimal_saturated(address_of digits, 0xffffffffu,
                                                        address_of over);
                if (digits != (string_address)number && !over)
                        unmapped = read;
                if (facts.user != unmapped)
                        got = -1;
        }
        if (got >= 0)
                got = system_read_retry((positive)handle, text, sizeof text);
        system_close(handle);
        return_if(got < 0 || (positive)got > TLS_OVERLAY_HEAD + TLS_OVERLAY_ROOM ||
                      !tls_overlay_read(text, (positive)got, address_of overlay),
                  -1);
        return (bipolar)overlay.switches;
}

/*
        Whether a switch is on: its word, as moonwater security sets it and
        bare moonwater security says it, is "on". It answers for packages
        (true: a bowl's programs may run the distribution's package manager)
        and for sudo (true: the moonwater command takes a bowl's root as the
        machine's root), and false for a switch whose words are not on and
        off, and for a name that is no switch.

        The answer is what /run/moonwater/security says
        (security_switches_read_at holds the file to root's, written by
        root alone, and read whole), else the default of the switch's tier:
        on for packages at the default tier, off at the tight one whatever
        the file says, and off for sudo everywhere unless a file that counts
        turns it at the default tier. A caller treats an answer it could not
        read as that default, which is never on for sudo.

        The answer is the file at the caller's own name for it. A bowl has a
        /run of its own, where the file is not unless the bowl carries it
        in, so inside one the answer is the default: packages is asked
        before a bowl's namespaces are made. And a process that owns its
        mount namespace shows itself whatever file it likes there, so it
        reads what it planted; nothing that changes the machine takes that
        process's root for root (the kernel asks capable(), in the first
        namespace), so for such a process sudo's answer is the command's
        manners and not a privilege.
*/
static bool security_switch_on(string_address name)
{
        bipolar kept = -1;

        for (positive at = 0; at < array_count(security_switches); at++)
                if (string_equals(name, security_switches[at].word))
                {
                        if (security_switches[at].tier < STRICT_TIGHT ||
                            security_switches[at].tight == SECURITY_TIGHT_NONE)
                                kept = security_switches_read_at(
                                    (string_address)TLS_OVERLAY_PATH, 0);
                        return string_equals(security_switch_turned(at, kept < 0 ? 0 : (p32)kept)
                                                 ? security_switches[at].turned
                                                 : security_switches[at].standard,
                                             "on");
                }
        return false;
}

/*
        The file of what the settings' security list holds, or none when it holds
        nothing, which is the built-in trust as it has always been. What the write
        or the removal failed with is answered: TLS reads this file and not the
        settings, so a list kept and not published is not in force.
*/
static bipolar security_publish(host_settings address_to settings)
{
        static p8 file[TLS_OVERLAY_HEAD + TLS_OVERLAY_ROOM];
        host_setting setting;
        positive used = TLS_OVERLAY_HEAD;
        positive at = 0;
        bipolar failed;

        while (settings && host_settings_next(settings, address_of at, address_of setting))
        {
                positive size = SPARK_SETTINGS_ENTRY + spark_settings_padded(setting.entry.length);

                if (setting.entry.list != SPARK_SETTINGS_SECURITY ||
                    used + size > sizeof file)
                        continue;
                memory_copy_apart(file + used, settings->payload + setting.at, size);
                used += size;
        }
        if (used == TLS_OVERLAY_HEAD)
        {
                failed = system_remove_at(AT_FDCWD, HOST_SECURITY, 0);
                return failed < 0 && failed != -ENOENT ? failed : 0;
        }
        memory_copy_apart(file, TLS_OVERLAY_MAGIC, 8);
        for (positive k = 0; k < 4; k++)
        {
                file[8 + k] = (p8)((used - TLS_OVERLAY_HEAD) >> (8 * k));
                file[12 + k] = 0;
        }
        host_state_ready();
        return host_publish(HOST_SECURITY, file, used, 0644);
}

/* The settings are kept and the file is published, or the verb says the file was not. */
static b32 security_publish_answer(host_settings address_to settings)
{
        bipolar failed = security_publish(settings);

        return failed < 0 ? host_fail("security not published to " HOST_SECURITY, failed) : 0;
}

/* A key as it is named on a line: 64 hexadecimal digits. */
static string_address security_hex(const p8 address_to digest)
{
        static p8 text[65];

        memory_into_hex(text, (address_any)digest, 32);
        text[64] = end;
        return (string_address)text;
}

/* A key's type as a column of eight: P-256, P-384, or RSA and its bits. */
static string_address security_kind(p8 curve, positive length)
{
        static p8 text[16];
        positive used;

        if (curve == 1)
                return (string_address) "P-256   ";
        if (curve == 2)
                return (string_address) "P-384   ";
        string_copy(text, "RSA-");
        used = 4 + positive_into_string(text + 4, length * 8);
        while (used < 8)
                text[used++] = ' ';
        text[used] = end;
        return (string_address)text;
}

/* A key named on the line: 64 hexadecimal digits, or the 44 characters of
   base64 a Certificate Transparency log list spells a log id in. */
static bool security_digest_word(string_address word, p8 address_to digest)
{
        positive length = string_length(word);
        bool loose = false;

        return (length == 64 && memory_from_hex(digest, word, 32)) ||
               (length == 44 && memory_from_base64(digest, word, 44, 32, address_of loose) &&
                !loose);
}

/* A certificate in a file, PEM (the first CERTIFICATE block) or DER, into der:
   its length, or 0 when the file holds neither. */
static positive security_certificate_read(string_address path, p8 address_to der,
                                          positive room)
{
        static p8 text[HOST_SECURITY_DER_MOST * 2];
        static p8 joined[HOST_SECURITY_DER_MOST * 2];
        static const char begin[] = "-----BEGIN CERTIFICATE-----";
        static const char finish[] = "-----END CERTIFICATE-----";
        bipolar got = file_slurp_regular_at(AT_FDCWD, path, text, sizeof text, 0);
        string_address start;
        string_address stop;
        positive used = 0;
        positive bytes;
        bool loose = false;

        if (got <= 0 || (positive)got >= sizeof text - 1)
                return 0;
        if (text[0] == 0x30)
        {
                if ((positive)got > room)
                        return 0;
                memory_copy(der, text, (positive)got);
                return (positive)got;
        }
        start = string_find((string_address)text, begin);
        stop = start ? string_find(start, finish) : null;
        if (!stop)
                return 0;
        for (start += sizeof begin - 1; start < stop; start++)
                if (*start != '\n' && *start != '\r' && *start != ' ' && *start != '\t')
                        joined[used++] = (p8)*start;
        if (used < 4 || used % 4)
                return 0;
        bytes = used / 4 * 3 - (joined[used - 1] == '=') - (joined[used - 2] == '=');
        if (bytes > room ||
            !memory_from_base64(der, (string_address)joined, used, bytes, address_of loose) ||
            loose)
                return 0;
        return bytes;
}

/* A key named by a word: its digest, from the word or from the certificate
   in the file it names. Says why not and answers false. */
static bool security_digest_of(string_address word, p8 address_to digest)
{
        static p8 der[HOST_SECURITY_DER_MOST];
        tls_cert cert;
        positive length;

        if (security_digest_word(word, digest))
                return true;
        length = security_certificate_read(word, der, sizeof der);
        if (!length)
        {
                host_refuse("%s is not a key's SHA-256 (64 hexadecimal digits, or 44 "
                            "characters of base64) or a file with a certificate in it\n",
                            word);
                return false;
        }
        if (tls_parse_cert(der, length, address_of cert, null) ||
            !tls_spki_digest(address_of cert, true, digest))
        {
                host_refuse("%s holds a certificate this cannot read\n", word);
                return false;
        }
        return true;
}

/* The settings entry an anchor added is kept as: its curve, three zero bytes,
   its RSA exponent low first, the SHA-256 of its subject Name and its key
   (tls_overlay_read). Its length, or 0 with the reason said. */
static positive security_anchor_entry(string_address path, p8 address_to entry,
                                      p8 address_to digest)
{
        static p8 der[HOST_SECURITY_DER_MOST];
        byte_reader text;
        tls_added_anchor added;
        tls_cert cert;
        positive length = security_certificate_read(path, der, sizeof der);
        positive key_length;

        if (!length)
                return host_refuse("%s is not a file with a certificate in it, PEM or "
                                   "DER\n", path), 0;
        if (tls_parse_cert(der, length, address_of cert, null))
                return host_refuse("%s holds a certificate this cannot read\n", path), 0;
        if (!cert.ca || (cert.key_usage && !cert.key_cert_sign))
                return host_refuse("%s is not a CA: it does not say CA:TRUE, or its key may "
                                   "not sign certificates\n", path), 0;
        if (cert.name_constraints)
                return host_refuse("%s has name constraints: the built-in anchors carry "
                                   "none, so it would be trusted for every name\n", path), 0;
        if (tls_key_blocked(address_of cert))
                return host_refuse("%s has a key that is refused: built in as compromised, "
                                   "or one anyone can compute\n", path), 0;
        memory_zero(entry, SPARK_SETTINGS_TRUST_MOST);
        entry[0] = cert.curve;
        if (cert.curve == 3)
        {
                positive skip = 0;

                while (skip < cert.modulus_length && !cert.modulus[skip])
                        skip++;
                key_length = cert.modulus_length - skip;
                if (key_length > 512 || cert.exponent > 0xffffffffull)
                        return host_refuse("%s has a key the built-in anchors could not "
                                           "have: P-256, P-384, or RSA of 2048 to 4096 bits "
                                           "with an odd exponent\n", path), 0;
                for (positive k = 0; k < 4; k++)
                        entry[4 + k] = (p8)(cert.exponent >> (8 * k));
                memory_copy(entry + 40, cert.modulus + skip, key_length);
        }
        else
        {
                key_length = cert.curve == 1 ? 64 : 96;
                memory_copy(entry + 40, cert.qx + 48 - key_length / 2, key_length / 2);
                memory_copy(entry + 40 + key_length / 2, cert.qy + 48 - key_length / 2,
                            key_length / 2);
        }
        crypto_sha256_of(cert.subject, cert.subject_length, entry + 8);
        //      Held to what the reader of the published file takes, by the reader.
        text = byte_reader_open(entry, 40 + key_length);
        if (!tls_overlay_anchor_read(address_of text, address_of added))
                return host_refuse("%s has a key the built-in anchors could not have: "
                                   "P-256, P-384, or RSA of 2048 to 4096 bits with an odd "
                                   "exponent\n", path), 0;
        memory_copy(digest, added.spki, 32);
        return 40 + key_length;
}

/* The digest of a security entry's key, a block's or an anchor's. */
static bool security_entry_digest(host_setting address_to setting, p8 address_to digest)
{
        byte_reader text = byte_reader_open(setting->text, setting->entry.length);
        tls_added_anchor added;

        if (setting->entry.kind == SPARK_SETTINGS_BLOCK && setting->entry.length == 32)
        {
                memory_copy(digest, setting->text, 32);
                return true;
        }
        if (setting->entry.kind != SPARK_SETTINGS_TRUST ||
            !tls_overlay_anchor_read(address_of text, address_of added))
                return false;
        memory_copy(digest, added.spki, 32);
        return true;
}

/* The entry of a kind whose key is digest, or whose key's hex starts with the
   word given (eight digits at least, and only one such): false when none or
   more than one is. */
static bool security_entry_find(host_settings address_to settings, p8 kind,
                                string_address word, const p8 address_to digest,
                                host_setting address_to into)
{
        host_setting setting;
        positive at = 0;
        positive found = 0;
        positive length = string_length(word);

        while (host_settings_next(settings, address_of at, address_of setting))
        {
                p8 have[32];

                if (setting.entry.list != SPARK_SETTINGS_SECURITY || setting.entry.kind != kind ||
                    !security_entry_digest(address_of setting, have))
                        continue;
                if (digest ? !memory_compare(have, digest, 32)
                           : length >= 8 && length < 64 &&
                                 !memory_compare_ascii_case(security_hex(have), word, length))
                {
                        address_to into = setting;
                        found++;
                }
        }
        return found == 1;
}

//      Which of tls_blocked_keys a digest is, or -1.
static bipolar security_built_in_block(const p8 address_to digest)
{
        for (positive i = 0; i < array_count(tls_blocked_keys); i++)
                if (!memory_compare(tls_blocked_keys[i], digest, 32))
                        return (bipolar)i;
        return -1;
}

//      Which of tls_anchors has the key of a digest, or -1.
static bipolar security_built_in_anchor(const p8 address_to digest)
{
        p8 have[32];
        tls_cert root;

        for (positive i = 0; i < array_count(tls_anchors); i++)
                if (tls_anchor_key(tls_anchors + i, address_of root) &&
                    tls_spki_digest(address_of root, false, have) &&
                    !memory_compare(have, digest, 32))
                        return (bipolar)i;
        return -1;
}

/* What the published file holds, as the clients read it, for the lines. */
static tls_overlay security_shown;

static bool security_read(void)
{
        tls_overlay_load(address_of security_shown);
        if (security_shown.state != TLS_OVERLAY_BAD)
                return true;
        host_say(log, host_label "security: " HOST_SECURITY " does not read, so no "
                                 "certificate is trusted until it does; boot and every "
                                 "change here write it again\n");
        return false;
}

static fn security_trust_lines(bool every)
{
        p8 digest[32];
        tls_cert root;

        if (!every)
                string_format(log, "  trust  built-in  %p anchors, which moonwater security "
                                   "trust lists\n",
                              (positive)array_count(tls_anchors));
        for (positive i = 0; every && i < array_count(tls_anchors); i++)
                if (tls_anchor_key(tls_anchors + i, address_of root) &&
                    tls_spki_digest(address_of root, false, digest))
                        string_format(log, "  trust  built-in  %s  %s%s\n",
                                      security_kind(tls_anchors[i].curve, tls_anchors[i].key_length),
                                      security_hex(digest),
                                      tls_anchor_refused(i) ? "  (blocked)" : "");
#ifdef TLS_BENCH_ANCHOR
        //      A harness's own root, which only its builds have.
        if (every)
        {
                p8 key[96];

                memory_copy(key, tls_bench_anchor_x, 48);
                memory_copy(key + 48, tls_bench_anchor_y, 48);
                if (tls_kept_key(2, 0, key, sizeof key, address_of root) &&
                    tls_spki_digest(address_of root, false, digest))
                        string_format(log, "  trust  built-in  %s  %s%s\n", security_kind(2, 96),
                                      security_hex(digest), tls_bench_refused() ? "  (blocked)" : "");
        }
#endif
        for (positive i = 0; i < security_shown.anchors; i++)
                string_format(log, "  trust  added     %s  %s%s\n",
                              security_kind(security_shown.anchor[i].curve,
                                            security_shown.anchor[i].key_length),
                              security_hex(security_shown.anchor[i].spki),
                              security_shown.anchor[i].refused ? "  (blocked)" : "");
}

static fn security_block_lines(void)
{
        for (positive i = 0; i < array_count(tls_blocked_keys); i++)
                string_format(log, "  block  built-in  key       %s\n",
                              security_hex(tls_blocked_keys[i]));
        for (positive i = 0; i < security_shown.blocks; i++)
                string_format(log, "  block  added     key       %s\n",
                              security_hex(security_shown.block[i]));
}

/* A switch's line: its word and where it stands, as the file and the tier
   have it. TLS's are what a TLS client reads; the bowl's are what
   security_switch_on answers, which holds the file to more. */
static fn security_switch_line(positive at, bool label)
{
        bool forced = security_switches[at].tier >= STRICT_TIGHT &&
                      security_switches[at].tight != SECURITY_TIGHT_NONE;
        bool turned = security_switches[at].id < SECURITY_SWITCH_PACKAGES_OFF
                          ? security_switch_turned(at, security_shown.switches)
                          : security_switch_on(security_switches[at].word) ==
                                string_equals(security_switches[at].turned, "on");

        string_format(log, label ? host_label "security %s %s%s\n" : "  %s %s%s\n",
                      security_switches[at].word,
                      turned ? security_switches[at].turned : security_switches[at].standard,
                      forced ? ", as the tight tier has it" : "");
}

static b32 security_say(string_address which)
{
        bool readable = security_read();

        //      The lines name the anchors this process's overlay refuses.
        memory_copy_apart(address_of tls_overlay_now, address_of security_shown,
                          sizeof tls_overlay_now);
        for (positive at = 0; which && at < array_count(security_switches); at++)
                if (string_equals(which, security_switches[at].word))
                        security_switch_line(at, true);
        for (positive at = 0; !which && at < array_count(security_switches); at++)
                security_switch_line(at, false);
        if (!which || string_equals(which, "trust"))
                security_trust_lines(which != null);
        if (!which || string_equals(which, "block"))
                security_block_lines();
        log_flush();
        return readable ? 0 : 1;
}

/* A switch set to a word: its entry added when the word turns it, taken out
   when the word is its default, then saved and published. */
static b32 security_switch(positive which, string_address word)
{
        host_settings settings;
        host_setting setting;
        host_setting found;
        bool turn = string_equals(word, security_switches[which].turned);
        bool have = false;
        positive at = 0;
        p16 id = security_switches[which].id;
        p8 tight = security_switches[which].tier >= STRICT_TIGHT ? security_switches[which].tight
                                                                  : SECURITY_TIGHT_NONE;
        string_address failed;
        b32 answer = 0;

        return_if(!turn && !string_equals(word, security_switches[which].standard), host_usage());
        host_need_root();
        //      A word the tight tier takes no notice of, where it refuses
        //      one, is not kept.
        if ((tight == SECURITY_TIGHT_SHUT && !turn) || (tight == SECURITY_TIGHT_HELD && turn))
                return host_refuse("security %s stays as the tight tier has it\n", security_switches[which].word);
        if (!host_acquire())
                return 1;
        host_state_ready();
        host_settings_session(address_of settings);
        while (host_settings_next(address_of settings, address_of at, address_of setting))
                if (setting.entry.list == SPARK_SETTINGS_SECURITY &&
                    setting.entry.kind == SPARK_SETTINGS_SWITCH && setting.entry.id == id)
                {
                        found = setting;
                        have = true;
                }
        if (have == turn)
        {
                host_say(log, host_label "security %s %s\n", security_switches[which].word, word);
                //      The file is put right as well: the same word again is how a change
                //      that was kept and not published (the command killed or the disk
                //      full between the two) is finished.
                answer = security_publish_answer(address_of settings);
                goto done;
        }
        if (turn)
        {
                failed = host_settings_add(address_of settings, SPARK_SETTINGS_SECURITY,
                                           SPARK_SETTINGS_SWITCH, (string_address) "", 0,
                                           address_of id);
                if (failed)
                {
                        answer = host_settings_refused("security", failed, address_of settings);
                        goto done;
                }
        }
        else
                host_settings_drop(address_of settings, address_of found);
        string_format(log, host_label "security %s %s; ", security_switches[which].word, word);
        answer = host_settings_save(address_of settings) ? 0 : 1;
        if (!answer)
                answer = security_publish_answer(address_of settings);
        if (!turn && tight == SECURITY_TIGHT_TURNED)
                host_say(log, host_label "security %s stays %s: the tight tier has it so\n",
                         security_switches[which].word, security_switches[which].turned);

done:
        host_release();
        return answer;
}

/* One entry added to or taken from a list, then saved and published. */
static b32 security_change(bool trust, bool adding, string_address word)
{
        static p8 entry[SPARK_SETTINGS_TRUST_MOST];
        host_settings settings;
        host_setting setting;
        string_address list = trust ? (string_address) "trust" : (string_address) "block";
        string_address failed;
        p8 digest[32];
        positive length = 32;
        bool named = true;
        b32 answer = 0;
        p16 id = SPARK_SETTINGS_BY_HAND;

        host_need_root();
        //      What the word names is read before anything waits: a file that
        //      is no certificate costs a line and not the settings' lock. A
        //      remove also takes the first hexadecimal digits of a key that
        //      was added, eight at least, as its line shows them.
        if (trust && adding)
        {
                length = security_anchor_entry(word, entry, digest);
                return_if(!length, 1);
        }
        else if (adding || security_digest_word(word, digest) ||
                 system_access_at(AT_FDCWD, word, 0) == 0)
        {
                return_if(!security_digest_of(word, digest), 1);
        }
        else
        {
                positive size = string_length(word);
                bool odd = false;

                named = false;
                for (positive at = 0; at < size; at++)
                        odd |= !byte_is_hexadecimal((p8)word[at]);
                return_if(odd || size < 8,
                          host_refuse("security %s remove takes a key's SHA-256, eight "
                                      "hexadecimal digits of it at least, or a file with "
                                      "a certificate in it\n", list));
        }
        if (!trust && !adding && named && security_built_in_block(digest) >= 0)
                return host_refuse("%s is built in and stays refused; nothing here lifts "
                                   "it\n", security_hex(digest));
        if (trust && !adding && named && security_built_in_anchor(digest) >= 0)
                return host_refuse("%s is a built-in anchor; moonwater security block add "
                                   "takes it out of trust\n", security_hex(digest));
        //      A trust add of a built-in refused key never gets here: security_anchor_entry refuses that key first.
        if (!trust && adding && security_built_in_block(digest) >= 0)
        {
                host_say(log, host_label "security %s: %s is refused, built in\n", list,
                         security_hex(digest));
                return 0;
        }
        if (trust && adding && security_built_in_anchor(digest) >= 0)
        {
                host_say(log, host_label "security trust: %s is a built-in anchor\n",
                         security_hex(digest));
                return 0;
        }
        if (!host_acquire())
                return 1;
        host_state_ready();
        host_settings_session(address_of settings);

        if (trust && adding &&
            security_entry_find(address_of settings, SPARK_SETTINGS_BLOCK, word, digest,
                                address_of setting))
        {
                answer = host_refuse("security trust: %s is blocked, and a block wins; "
                                     "moonwater security block remove takes it out\n",
                                     security_hex(digest));
                goto done;
        }
        if (adding &&
            security_entry_find(address_of settings,
                                trust ? SPARK_SETTINGS_TRUST : SPARK_SETTINGS_BLOCK, word,
                                digest, address_of setting))
        {
                host_say(log, host_label "security %s: %s is there already\n", list,
                         security_hex(digest));
                answer = security_publish_answer(address_of settings);
                goto done;
        }
        if (adding)
        {
                positive have = 0;
                positive at = 0;

                while (host_settings_next(address_of settings, address_of at, address_of setting))
                        have += setting.entry.list == SPARK_SETTINGS_SECURITY &&
                                setting.entry.kind == (trust ? SPARK_SETTINGS_TRUST
                                                             : SPARK_SETTINGS_BLOCK);
                if (have >= (trust ? TLS_OVERLAY_ANCHORS : TLS_OVERLAY_BLOCKS))
                {
                        answer = host_refuse("security %s holds %p at most; remove one "
                                             "first\n", list,
                                             (positive)(trust ? TLS_OVERLAY_ANCHORS
                                                              : TLS_OVERLAY_BLOCKS));
                        goto done;
                }
                failed = host_settings_add(address_of settings, SPARK_SETTINGS_SECURITY,
                                           trust ? SPARK_SETTINGS_TRUST : SPARK_SETTINGS_BLOCK,
                                           trust ? (string_address)entry : (string_address)digest,
                                           length, address_of id);
                if (failed)
                {
                        answer = host_settings_refused("security", failed, address_of settings);
                        goto done;
                }
                string_format(log, host_label "security %s added %s; ", list,
                              security_hex(digest));
        }
        else
        {
                if (!security_entry_find(address_of settings,
                                         trust ? SPARK_SETTINGS_TRUST : SPARK_SETTINGS_BLOCK,
                                         word, named ? digest : null, address_of setting))
                {
                        //      host_refuse names one thing: the list is in the words.
                        answer = trust ? host_refuse("security trust has no one entry %s\n",
                                                     named ? security_hex(digest) : word)
                                       : host_refuse("security block has no one entry %s\n",
                                                     named ? security_hex(digest) : word);
                        //      An entry taken out and not yet out of the file is finished by
                        //      saying so again, as an add that was kept is.
                        (void)security_publish_answer(address_of settings);
                        goto done;
                }
                security_entry_digest(address_of setting, digest);
                host_settings_drop(address_of settings, address_of setting);
                string_format(log, host_label "security %s removed %s; ", list,
                              security_hex(digest));
        }
        answer = host_settings_save(address_of settings) ? 0 : 1;
        if (!answer)
                answer = security_publish_answer(address_of settings);

done:
        host_release();
        return answer;
}

static b32 host_security(string_address address_to arguments, positive count)
{
        bool trust = count > 2 && string_equals(arguments[2], "trust");

        if (count == 2)
                return security_say(null);
        for (positive at = 0; at < array_count(security_switches); at++)
                if (string_equals(arguments[2], security_switches[at].word))
                {
                        return_if(count > 4, host_usage());
                        return count == 3 ? security_say(arguments[2])
                                          : security_switch(at, arguments[3]);
                }
        return_if(!trust && !string_equals(arguments[2], "block"), host_usage());
        if (count == 3)
                return security_say(arguments[2]);
        return_if(count != 5 || (!string_equals(arguments[3], "add") &&
                                 !string_equals(arguments[3], "remove")),
                  host_usage());
        return security_change(trust, string_equals(arguments[3], "add"), arguments[4]);
}

/*
        Bound events, and the init and exit lists, as one verb.

        A kernel event is one line. init and exit stay lists, because more
        than one thing runs at boot and at stop. An empty line puts the
        event's default back. What is not the default is kept in the image
        and put back at the next boot.
*/
static fn host_bind_say(bool label, struct bind_control address_to control)
{
        p16 line = host_machine_event_line(control->event);
        string_address name = (string_address)control->name;
        bool said = false;

        host_prefix(label);
        if (line)
                string_format(log, "%s: %s:%p", name, host_machine_where(),
                              (positive)line);
        else if (control->command[0])
                string_format(log, "%s: %s", name,
                              host_plain((string_address)control->command));
        else if (control->flags & SPARK_BIND_DEFAULT)
                string_format(log, "%s", name);
        else
                //      The kernel keeps a line somebody set from anyone but
                //      root, and clears the DEFAULT flag so that this can say so.
                string_format(log, "%s: set, and root's to read", name);

        if (control->runs)
        {
                string_format(log, "  (ran %p time%s", (positive)control->runs,
                              control->runs == 1 ? "" : "s");
                said = true;
        }
        if (control->flags & SPARK_BIND_RUNNING)
        {
                string_format(log, said ? ", running now" : "  (running now");
                said = true;
        }
        if (control->flags & SPARK_BIND_PENDING)
        {
                string_format(log, said ? ", queued" : "  (queued");
                said = true;
        }
        string_format(log, said ? ")\n" : "\n");
}

static fn host_bind_forget(host_settings address_to settings, p16 event)
{
        host_setting setting;
        positive at = 0;

        //      A drop moves the entries after it down to where it was, so the
        //      scan goes on from there and not from the start again.
        while (host_settings_next(settings, address_of at, address_of setting))
                if (setting.entry.list == SPARK_SETTINGS_BIND && setting.entry.id == event)
                {
                        host_settings_drop(settings, address_of setting);
                        at = setting.at;
                }
}

/*
        Whether the line to be set will fit in the block that keeps it, asked
        before the kernel is: a block full of init and exit entries refused
        the line after the kernel had taken it, and the command said refused
        about a line that was live. An empty line puts the default back and
        needs no room. Null when it fits or when there is no copy to ask --
        the kernel's own refusal comes first for anyone it refuses.
*/
static string_address host_bind_fits(host_settings address_to settings,
                                     unsigned int event, string_address command)
{
        p16 id = (p16)event;

        if (!host_settings_session(settings))
                return null;

        host_bind_forget(settings, id);
        if (!*command)
                return null;

        return host_settings_add(settings, SPARK_SETTINGS_BIND, SPARK_SETTINGS_COMMAND,
                                 command, string_length(command), address_of id);
}

static b32 host_bind_keep(unsigned int event, struct bind_control address_to control)
{
        host_settings settings;
        p16 id = (p16)event;
        string_address failed;

        host_state_ready();
        host_settings_session(address_of settings);
        host_bind_forget(address_of settings, id);
        if (!(control->flags & SPARK_BIND_DEFAULT))
        {
                failed = host_settings_add(address_of settings, SPARK_SETTINGS_BIND,
                                           SPARK_SETTINGS_COMMAND,
                                           (string_address)control->command,
                                           string_length((string_address)control->command),
                                           address_of id);
                return_if(failed, host_settings_refused("bind", failed, address_of settings));
        }
        return host_settings_save(address_of settings) ? 0 : 1;
}

static fn host_bind_apply(host_settings address_to settings)
{
        host_setting setting;
        p8 text[SPARK_SETTINGS_TEXT_MOST + 1];
        struct bind_control control;
        positive at = 0;
        bipolar device;

        device = system_open_at(AT_FDCWD, SPARK_DEVICE, FILE_READ | O_CLOEXEC);
        if (device < 0)
                return;

        while (host_settings_next(settings, address_of at, address_of setting))
        {
                if (setting.entry.list != SPARK_SETTINGS_BIND)
                        continue;

                host_settings_text(text, address_of setting);
                host_bind_ioctl(device, SPARK_BIND_SET, setting.entry.id, text,
                                address_of control);
        }

        system_close(device);
}

//      How many events are asked of the kernel: the number it says it has, and
//      no more than the names this knows (SPARK_BIND_EVENTS), past which there
//      is nothing to say.
static unsigned int host_bind_known(unsigned int offered)
{
        return offered < SPARK_BIND_EVENTS ? offered : SPARK_BIND_EVENTS;
}

static bipolar host_bind_each(bool label, bool required)
{
        struct bind_control control;
        bipolar device;
        bipolar failed;
        unsigned int event;
        unsigned int count = SPARK_BIND_EVENTS;

        device = system_open_at(AT_FDCWD, SPARK_DEVICE, FILE_READ | O_CLOEXEC);
        if (device < 0)
                return required ? device : 0;

        for (event = 1; event <= count; event++)
        {
                failed = host_bind_ioctl(device, SPARK_BIND_GET, event, null,
                                         address_of control);
                if (failed < 0)
                {
                        system_close(device);
                        return (required && event == 1) ? failed : 0;
                }
                if (event == 1 && control.count)
                        count = host_bind_known(control.count);
                host_bind_say(label, address_of control);
        }

        system_close(device);
        return 0;
}

static b32 host_bind_events(void)
{
        bipolar failed = host_bind_each(false, true);

        return_if(failed < 0, host_fail(SPARK_DEVICE, failed));

        host_say(log, "  reset is the keyboard's reset/restart key; "
                      "a case reset button cannot be bound\n");
        return 0;
}

static fn host_bind_names(writer out)
{
        unsigned int event;
        bool first = true;

        for (event = 0; event < SPARK_BIND_EVENTS; event++)
        {
                string_format(out, "%s%s", first ? "" : ", ",
                              (string_address)spark_bind_event_name[event]);
                first = false;
        }
}

static unsigned int host_bind_named(string_address first, string_address second)
{
        p8 wanted[SPARK_BIND_NAME_MAX];
        unsigned int event;

        wanted[0] = end;
        string_append_bounded(wanted, first, sizeof(wanted));
        if (second)
        {
                string_append_bounded(wanted, " ", sizeof(wanted));
                string_append_bounded(wanted, second, sizeof(wanted));
        }

        for (event = 0; event < SPARK_BIND_EVENTS; event++)
                if (string_equals((string_address)spark_bind_event_name[event], wanted))
                        return event + 1;

        return 0;
}

static b32 host_bind_told(unsigned int event, string_address command)
{
        struct bind_control control;
        host_settings settings;
        p16 line = host_machine_event_line(event);
        string_address full;
        bipolar failed;

        if (line)
        {
                host_machine_refused(event && event <= SPARK_BIND_EVENTS
                                         ? (string_address)spark_bind_event_name[event - 1]
                                         : (string_address) "that event",
                                     line);
                return 1;
        }

        //      The line the event already runs, said again as a script says
        //      it at every boot: nothing is set, and the boot image is not
        //      written over with what it holds.
        if (*command && host_bind_request(SPARK_BIND_GET, event, null, address_of control) >= 0 &&
            !(control.flags & SPARK_BIND_DEFAULT) &&
            string_equals((string_address)control.command, command))
        {
                host_bind_say(true, address_of control);
                log_flush();
                return 0;
        }

        full = host_bind_fits(address_of settings, event, command);
        if (full)
        {
                host_settings_refused("bind", full, address_of settings);
                return 1;
        }

        failed = host_bind_request(SPARK_BIND_SET, event, command,
                                   address_of control);

        if (failed == -EPERM)
                return host_refuse(control.flags & SPARK_BIND_BOOT
                                           ? "setting what %s runs needs root (CAP_SYS_ADMIN and CAP_SYS_BOOT)\n"
                                           : "setting what %s runs needs root (CAP_SYS_ADMIN)\n",
                                   control.name[0] ? (string_address)control.name
                                                   : (string_address)"that event");
        if (failed == -ENAMETOOLONG)
                return host_refuse("that command is longer than the 255 bytes a bound event holds\n");
        return_if(failed < 0, host_fail(SPARK_DEVICE, failed));

        if (host_bind_keep(event, address_of control))
                return 1;
        host_bind_say(true, address_of control);
        log_flush();
        return 0;
}

/* Setting an event reads the kept settings and writes them back, one command at
   a time like the lists; anybody else is refused by the kernel before any of it. */
static b32 host_bind_tell(unsigned int event, string_address command)
{
        b32 outcome;

        return_if(!bowl_is_root(), host_bind_told(event, command));

        if (!host_acquire())
                return 1;

        outcome = host_bind_told(event, command);
        host_release();
        return outcome;
}

/* moonwater bind [init|exit|EVENT ...] */
static b32 host_bind(string_address address_to arguments, positive count)
{
        struct bind_control probe;
        p8 text[SPARK_BIND_COMMAND_MAX];
        unsigned int event;
        string_address second = null;
        positive words;
        positive length = 0;

        if (count == 2)
                return host_bind_events();

        if (string_equals(arguments[2], "init") || string_equals(arguments[2], "exit"))
        {
                arguments[1] = arguments[2];
                array_close_gap(arguments, count, 2);
                return host_settings_command(arguments, count - 1);
        }

        event = 0;
        words = 3;
        if (count >= 4)
        {
                event = host_bind_named(arguments[2], arguments[3]);
                if (event)
                {
                        second = arguments[3];
                        words = 4;
                }
        }
        if (!event)
                event = host_bind_named(arguments[2], null);

        if (!event)
        {
                bipolar failed = host_bind_request(SPARK_BIND_GET, 1, null, address_of probe);

                if (failed < 0)
                        return host_fail(SPARK_DEVICE, failed);

                if (second)
                        string_format(log_error,
                                      host_label "%s %s is not a bound event; the events are ",
                                      arguments[2], arguments[3]);
                else
                        string_format(log_error, host_label "%s is not a bound event; the events are ",
                                      arguments[2]);
                host_bind_names(log_error);
                host_say(log_error, "\n");
                return host_usage();
        }

        if (count == words)
        {
                struct bind_control control;
                bipolar failed = host_bind_request(SPARK_BIND_GET, event, null,
                                                   address_of control);

                return_if(failed < 0, host_fail(SPARK_DEVICE, failed));
                host_bind_say(true, address_of control);
                log_flush();
                return 0;
        }

        if (!host_words_join(text, sizeof(text), arguments + words, count - words,
                                 address_of length))
                return host_refuse("that command is longer than the 255 bytes a bound event holds\n");

        return host_bind_tell(event, text);
}

